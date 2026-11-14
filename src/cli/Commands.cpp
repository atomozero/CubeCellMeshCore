#include "../main.h"
#include "../core/Led.h"
#include "../core/Config.h"

// Parse decimal string to fixed-point int32 with 6 decimal places
// "45.123456" -> 45123456, "-12.5" -> -12500000
// Returns true on success
static bool parseFixed6(const char* s, int32_t* out) {
    if (!s || !*s) return false;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    int32_t whole = 0;
    while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s - '0'); s++; }
    int32_t frac = 0;
    if (*s == '.') {
        s++;
        int32_t mul = 100000;
        for (int i = 0; i < 6 && *s >= '0' && *s <= '9'; i++) {
            frac += (*s - '0') * mul;
            mul /= 10;
            s++;
        }
    }
    *out = (whole * 1000000 + frac) * (neg ? -1 : 1);
    return true;
}

// Parse decimal string to uint32 with 3 decimal places (for MHz)
// "869.618" -> 869618
static bool parseMHz3(const char* s, uint32_t* out) {
    if (!s || !*s) return false;
    uint32_t whole = 0;
    while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s - '0'); s++; }
    uint32_t frac = 0;
    if (*s == '.') {
        s++;
        uint32_t mul = 100;
        for (int i = 0; i < 3 && *s >= '0' && *s <= '9'; i++) {
            frac += (*s - '0') * mul;
            mul /= 10;
            s++;
        }
    }
    *out = whole * 1000 + frac;
    return true;
}

// Parse decimal string to uint32 with 1 decimal place (for kHz bandwidth)
// "62.5" -> 625
static bool parseBW1(const char* s, uint32_t* out) {
    if (!s || !*s) return false;
    uint32_t whole = 0;
    while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s - '0'); s++; }
    uint32_t frac = 0;
    if (*s == '.') {
        s++;
        if (*s >= '0' && *s <= '9') frac = *s - '0';
    }
    *out = whole * 10 + frac;
    return true;
}


//=============================================================================
// Unified CLI Output Abstraction
//=============================================================================
// CmdCtx allows shared command handlers to output to either Serial or a buffer
struct CmdCtx {
    char* buf;        // NULL for serial output
    uint16_t len;     // current position in buffer
    uint16_t maxLen;  // buffer capacity (0 for serial)
};

// Framework's printf implementation (port/printf.c). Using it directly avoids
// linking newlib's vsnprintf as a second printf engine (~1.2KB Flash).
extern "C" int _vsnprintf(void (*out)(char, void*, size_t, size_t), char* buffer,
                          const size_t maxlen, const char* format, va_list va);
static void cmdOutBuf(char c, void* buf, size_t idx, size_t maxlen) {
    if (idx < maxlen) ((char*)buf)[idx] = c;
}
static inline int cmdVsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
    return _vsnprintf(cmdOutBuf, buf, n, fmt, ap);
}

static void cmdPrint(CmdCtx* ctx, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void cmdPrint(CmdCtx* ctx, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (ctx->buf) {
        int rc = cmdVsnprintf(ctx->buf + ctx->len, ctx->maxLen - ctx->len, fmt, ap);
        if (rc > 0 && ctx->len + rc < ctx->maxLen) ctx->len += rc;
    } else {
        #ifndef SILENT
        static char tmp[96];
        cmdVsnprintf(tmp, sizeof(tmp), fmt, ap);
        Serial.print(tmp);
        #endif
    }
    va_end(ap);
}

#define CP(...) cmdPrint(&ctx, __VA_ARGS__)

// Returns true if command was handled
static bool dispatchSharedCommand(const char* cmd, CmdCtx& ctx, bool isAdmin) {
    // --- Read-only commands ---
    if (strcmp(cmd, "status") == 0) {
        CP("FW:%s %s(%02X) Up:%lus T:%s\n",
            FIRMWARE_VERSION, nodeIdentity.getNodeName(), nodeIdentity.getNodeHash(),
            millis() / 1000, timeSync.isSynchronized() ? "sync" : "nosync");
    }
    else if (strcmp(cmd, "stats") == 0) {
        CP("RX:%lu TX:%lu FWD:%lu E:%lu ADV:%lu/%lu Q:%d\n",
            rxCount, txCount, fwdCount, errCount, advTxCount, advRxCount, txQueue.getCount());
    }
    else if (strcmp(cmd, "stats-core") == 0) {
        telemetry.update();
        CP("Batt:%dmV(%d%%) Up:%lus Q:%d/%d\n",
            telemetry.getBatteryMv(), telemetry.getBatteryPercent(),
            millis() / 1000, txQueue.getCount(), MC_TX_QUEUE_SIZE);
    }
    else if (strcmp(cmd, "ver") == 0) {
        CP("%s\n", FIRMWARE_VERSION);
    }
    else if (strcmp(cmd, "board") == 0) {
        CP("HTCC-AB01\n");
    }
    else if (strcmp(cmd, "time") == 0 || strcmp(cmd, "clock") == 0) {
        if (timeSync.isSynchronized()) CP("T:%lu sync\n", timeSync.getTimestamp());
        else CP("T:nosync\n");
    }
    else if (strcmp(cmd, "telemetry") == 0) {
        telemetry.update();
        CP("Batt:%dmV(%d%%) Up:%lus\n", telemetry.getBatteryMv(), telemetry.getBatteryPercent(), millis() / 1000);
    }
    else if (strcmp(cmd, "nodes") == 0 || strncmp(cmd, "nodes ", 6) == 0) {
        uint8_t count = seenNodes.getCount();
        uint8_t start = 0, end = count;
        const uint8_t PAGE_SIZE = 3;
        if (cmd[5] == ' ') {
            uint8_t page = (uint8_t)atoi(cmd + 6);
            start = page * PAGE_SIZE;
            end = start + PAGE_SIZE;
            if (start >= count) { CP("Nodes:%d pg:%d empty\n", count, page); }
            else {
                if (end > count) end = count;
                CP("Nodes:%d pg:%d/%d\n", count, page, (count + PAGE_SIZE - 1) / PAGE_SIZE - 1);
            }
        } else {
            CP("Nodes:%d\n", count);
        }
        for (uint8_t i = start; i < end; i++) {
            const SeenNode* n = seenNodes.getNode(i);
            if (n) {
                if (ctx.buf && ctx.len >= ctx.maxLen - 48) break;
                uint32_t ago = (millis() - n->lastSeen) / 1000;
                if (timeSync.isSynchronized()) {
                    uint32_t ts = timeSync.getTimestamp() - ago;
                    TimeSync::DateTime dt;
                    TimeSync::timestampToDateTime(ts, dt);
                    CP("%02X %s %ddBm s:%d.%ddB p:%d %02d/%02d %02d:%02d\n", n->hash, n->name[0]?n->name:"-", n->lastRssi, n->lastSnr/4, abs(n->lastSnr%4)*25, n->pktCount, dt.day, dt.month, dt.hour, dt.minute);
                } else {
                    CP("%02X %s %ddBm s:%d.%ddB p:%d %lus\n", n->hash, n->name[0]?n->name:"-", n->lastRssi, n->lastSnr/4, abs(n->lastSnr%4)*25, n->pktCount, ago);
                }
            }
        }
    }
    else if (strcmp(cmd, "neighbours") == 0 || strcmp(cmd, "neighbors") == 0) {
        NeighbourTracker& nb = repeaterHelper.getNeighbours();
        uint8_t cnt = nb.getCount();
        CP("Nbr:%d\n", cnt);
        for (uint8_t i = 0; i < cnt; i++) {
            const NeighbourInfo* n = nb.getNeighbour(i);
            if (n) {
                if (ctx.buf && ctx.len >= ctx.maxLen - 80) break;
                uint32_t ago = (millis() - n->lastHeard) / 1000;
                // Resolve name from SeenNodesTracker via hash (pubKeyPrefix[0])
                const char* name = "-";
                for (uint8_t j = 0; j < seenNodes.getCount(); j++) {
                    const SeenNode* sn = seenNodes.getNode(j);
                    if (sn && sn->hash == n->pubKeyPrefix[0] && sn->name[0]) {
                        name = sn->name; break;
                    }
                }
                const char* cb = n->cbState == 0 ? "ok" : (n->cbState == 1 ? "OPEN" : "half");
                // Show current and average values + packet count
                CP(" %02X %s %ddBm(%d) s:%d.%d(%d.%d)dB p:%u cb:%s %lus\n",
                    n->pubKeyPrefix[0], name,
                    n->rssi, n->rssiAvg,
                    n->snr/4, abs(n->snr%4)*25,
                    n->snrAvg/4, abs(n->snrAvg%4)*25,
                    n->pktCount, cb, ago);
            }
        }
    }
    else if (strcmp(cmd, "identity") == 0) {
        CP("%s %02X\n", nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
        if (nodeIdentity.hasLocation()) {
            int32_t lat = nodeIdentity.getLatitude();
            int32_t lon = nodeIdentity.getLongitude();
            CP("Loc:%ld.%06ld,%ld.%06ld\n", lat/1000000, abs(lat%1000000), lon/1000000, abs(lon%1000000));
        }
        if (!ctx.buf) {
            const uint8_t* pk = nodeIdentity.getPublicKey();
            CP("PK:");
            for (int i = 0; i < 32; i++) CP("%02x", pk[i]);
            CP("\n");
        }
    }
    else if (strcmp(cmd, "location") == 0) {
        if (nodeIdentity.hasLocation()) {
            int32_t lat = nodeIdentity.getLatitude();
            int32_t lon = nodeIdentity.getLongitude();
            CP("%ld.%06ld,%ld.%06ld\n", lat/1000000, abs(lat%1000000), lon/1000000, abs(lon%1000000));
        } else CP("No loc\n");
    }
    else if (strcmp(cmd, "repeat") == 0) {
        CP("Rpt:%s hops:%d\n", repeaterHelper.isRepeatEnabled() ? "on" : "off", repeaterHelper.getMaxFloodHops());
    }
    else if (strcmp(cmd, "advert interval") == 0) {
        CP("Int:%lum next:%lus\n", advertGen.getInterval() / 60000, advertGen.getTimeUntilNext());
    }
    else if (strcmp(cmd, "radiostats") == 0) {
        const RadioStats& rs = repeaterHelper.getRadioStats();
        CP("Noise:%ddBm RSSI:%d SNR:%d.%ddB\n", rs.noiseFloor, rs.lastRssi, rs.lastSnr/4, abs(rs.lastSnr%4)*25);
        CP("Airtime TX:%lus RX:%lus\n", rs.txAirTimeSec, rs.rxAirTimeSec);
    }
    else if (strcmp(cmd, "packetstats") == 0) {
        const PacketStats& ps = repeaterHelper.getPacketStats();
        CP("RX:%lu TX:%lu FL:%lu/%lu DR:%lu/%lu\n",
            ps.numRecvPackets, ps.numSentPackets,
            ps.numRecvFlood, ps.numSentFlood, ps.numRecvDirect, ps.numSentDirect);
    }
    else if (strcmp(cmd, "radio") == 0) {
        { uint32_t fM = (uint32_t)(MC_FREQUENCY * 1000); uint32_t bT = (uint32_t)(MC_BANDWIDTH * 10);
        CP("%lu.%03lu BW%lu.%lu SF%d CR%d %ddBm\n", fM/1000, fM%1000, bT/10, bT%10, MC_SPREADING, MC_CODING_RATE, MC_TX_POWER); }
        if (tempRadioActive) {
            uint32_t fM = (uint32_t)(tempFrequency * 1000); uint32_t bT = (uint32_t)(tempBandwidth * 10);
            CP("Tmp:%lu.%03lu BW%lu.%lu SF%d CR%d\n", fM/1000, fM%1000, bT/10, bT%10, tempSpreadingFactor, tempCodingRate);
        }
    }
    else if (strcmp(cmd, "lifetime") == 0) {
        const PersistentStats* ps = getPersistentStats();
        CP("Boots:%u Up:%lus RX:%lu TX:%lu FWD:%lu\n",
            ps->bootCount, statsGetTotalUptime(), ps->totalRxPackets, ps->totalTxPackets, ps->totalFwdPackets);
    }
    else if (strcmp(cmd, "health") == 0) {
        uint8_t cnt = seenNodes.getCount();
        uint32_t now = millis();
        uint8_t offline = 0;
        for (uint8_t i = 0; i < cnt; i++) {
            const SeenNode* n = seenNodes.getNode(i);
            if (n && (now - n->lastSeen) > HEALTH_OFFLINE_MS) offline++;
        }
        // Line 1: System vitals
        telemetry.update();
        CP("Up:%lus Bat:%dmV(%d%%) T:%s\n", now / 1000,
            telemetry.getBatteryMv(), telemetry.getBatteryPercent(),
            timeSync.isSynchronized() ? "sync" : "no");
        // Line 2: Network summary
        CP("N:%d On:%d Off:%d Alrt:%s\n", cnt, cnt - offline, offline,
            alertEnabled ? "on" : "off");
        // Line 3: Subsystems
        CP("Mbox:%d/%d RL:%lu/%lu/%lu E:%lu\n",
            mailbox.getCount(), mailbox.getTotalSlots(),
            repeaterHelper.getLoginLimiter().getTotalBlocked(),
            repeaterHelper.getRequestLimiter().getTotalBlocked(),
            repeaterHelper.getForwardLimiter().getTotalBlocked(),
            errCount);
        // Line 4+: Only problematic nodes (offline or SNR degraded)
        for (uint8_t i = 0; i < cnt; i++) {
            if (ctx.buf && ctx.len >= ctx.maxLen - 40) break;
            const SeenNode* n = seenNodes.getNode(i);
            if (!n) continue;
            bool isOff = (now - n->lastSeen) > HEALTH_OFFLINE_MS;
            int8_t snrDrop = n->snrAvg - n->lastSnr;  // positive = degraded
            if (isOff) {
                uint32_t offMin = (now - n->lastSeen) / 60000;
                CP(" %02X %s OFF %lum\n", n->hash, n->name[0] ? n->name : "-", offMin);
            } else if (snrDrop >= (int8_t)(HEALTH_SNR_DROP_THRESH)) {
                CP(" %02X %s snr%+ddB\n", n->hash, n->name[0] ? n->name : "-",
                    (n->lastSnr - n->snrAvg) / 4);
            }
        }
    }
    else if (strcmp(cmd, "mailbox") == 0) {
        CP("Mbox:%d/%d E:%d R:%d\n", mailbox.getCount(), mailbox.getTotalSlots(),
            mailbox.getEepromCount(), mailbox.getRamCount());
        for (uint8_t i = 0; i < mailbox.getTotalSlots(); i++) {
            const MailboxSlot* s = mailbox.getSlot(i);
            if (s && s->pktLen > 0) {
                uint32_t age = timeSync.isSynchronized() ? (timeSync.getTimestamp() - s->timestamp) : 0;
                CP(" %c%d:%02X %dB %lus\n", mailbox.isEepromSlot(i) ? 'E' : 'R',
                    i, s->destHash, s->pktLen, age);
            }
        }
    }
    else if (strcmp(cmd, "power") == 0) {
        CP("M:%d RxB:%s DS:%s\n", powerSaveMode,
            rxBoostEnabled ? "on" : "off", deepSleepEnabled ? "on" : "off");
    }
    else if (strcmp(cmd, "alert") == 0) {
        CP("Alert:%s Dest:%s\n", alertEnabled ? "on" : "off",
            isPubKeySet(alertDestPubKey) ? "set" : "none");
    }
    else if (strcmp(cmd, "ratelimit") == 0) {
        CP("RL:%s L:%lu R:%lu F:%lu\n",
            repeaterHelper.isRateLimitEnabled() ? "on" : "off",
            repeaterHelper.getLoginLimiter().getTotalBlocked(),
            repeaterHelper.getRequestLimiter().getTotalBlocked(),
            repeaterHelper.getForwardLimiter().getTotalBlocked());
    }
    else if (strcmp(cmd, "sleep") == 0) {
        CP("Sleep:%s\n", deepSleepEnabled ? "on" : "off");
    }
    else if (strcmp(cmd, "rxboost") == 0) {
        CP("RxB:%s\n", rxBoostEnabled ? "on" : "off");
    }
    else if (strcmp(cmd, "rssi") == 0) {
        CP("RSSI:%d SNR:%d.%02ddB\n", lastRssi, lastSnr/4, abs(lastSnr%4)*25);
    }
    else if (strcmp(cmd, "acl") == 0) {
        CP("Admin:%s Guest:%s S:%d\n",
            sessionManager.getAdminPassword(),
            strlen(sessionManager.getGuestPassword()) > 0 ? sessionManager.getGuestPassword() : "(off)",
            sessionManager.getSessionCount());
    }
    else if (strcmp(cmd, "quiet") == 0) {
        if (repeaterHelper.isQuietHoursEnabled())
            CP("Quiet:%d-%d max:%d %s\n", repeaterHelper.getQuietStartHour(),
                repeaterHelper.getQuietEndHour(), repeaterHelper.getQuietForwardMax(),
                repeaterHelper.isInQuietPeriod() ? "ACTIVE" : "idle");
        else CP("Quiet:off\n");
    }
    else if (strcmp(cmd, "cb") == 0) {
        CP("CB:%d\n", repeaterHelper.getNeighbours().getCircuitBreakerCount());
    }
    else if (strcmp(cmd, "txpower") == 0) {
        CP("TxP:%ddBm max:%d auto:%s\n", repeaterHelper.getCurrentTxPower(),
            MC_TX_POWER, repeaterHelper.isAdaptiveTxEnabled() ? "on" : "off");
    }
    else if (strcmp(cmd, "powersaving") == 0) {
        CP("PS:%d\n", powerSaveMode);
    }
    else if (strcmp(cmd, "get name") == 0) {
        CP("Name:%s\n", nodeIdentity.getNodeName());
    }
    else if (strcmp(cmd, "get lat") == 0) {
        if (nodeIdentity.hasLocation()) {
            int32_t lat = nodeIdentity.getLatitude();
            CP("%ld.%06ld\n", lat/1000000, abs(lat%1000000));
        } else CP("0\n");
    }
    else if (strcmp(cmd, "get lon") == 0) {
        if (nodeIdentity.hasLocation()) {
            int32_t lon = nodeIdentity.getLongitude();
            CP("%ld.%06ld\n", lon/1000000, abs(lon%1000000));
        } else CP("0\n");
    }
    else if (strcmp(cmd, "get freq") == 0) {
        uint32_t fM = tempRadioActive ? (uint32_t)(tempFrequency * 1000) : (uint32_t)(MC_FREQUENCY * 1000);
        CP("%lu.%03lu\n", fM/1000, fM%1000);
    }
    else if (strcmp(cmd, "get flood.max") == 0) {
        CP("%d\n", repeaterHelper.getMaxFloodHops());
    }
    else if (strcmp(cmd, "get guest.password") == 0) {
        CP("%s\n", strlen(sessionManager.getGuestPassword()) > 0 ? sessionManager.getGuestPassword() : "(off)");
    }
    else if (strcmp(cmd, "get public.key") == 0) {
        const uint8_t* pk = nodeIdentity.getPublicKey();
        for (int i = 0; i < 32; i++) CP("%02x", pk[i]);
        CP("\n");
    }
    else if (strcmp(cmd, "get owner.info") == 0 || strcmp(cmd, "set owner.info") == 0) {
        if (ownerInfo[0]) {
            for (const char* p = ownerInfo; *p; p++) {
                if (*p == '|') CP("\n"); else CP("%c", *p);
            }
            CP("\n");
        } else CP("(none)\n");
    }
    else if (strcmp(cmd, "set txdelay") == 0 || strcmp(cmd, "get txdelay") == 0) {
        CP("txdelay:%d\n", configTxDelayFactor);
    }
    else if (strcmp(cmd, "set rxdelay") == 0 || strcmp(cmd, "get rxdelay") == 0) {
        CP("rxdelay:%d\n", configRxDelayFactor);
    }
    else if (strcmp(cmd, "set direct.txdelay") == 0 || strcmp(cmd, "get direct.txdelay") == 0) {
        CP("direct.txdelay:%d\n", configDirectTxDelay);
    }
    else if (strcmp(cmd, "set af") == 0 || strcmp(cmd, "get af") == 0) {
        CP("af:%d.%d\n", configAirtimeFactor / 10, configAirtimeFactor % 10);
    }
    else if (strcmp(cmd, "set adc.multiplier") == 0 || strcmp(cmd, "get adc.multiplier") == 0) {
        CP("adc.mul:%d.%d\n", configAdcMultiplier / 10, configAdcMultiplier % 10);
    }
    else if (strcmp(cmd, "set agc.reset.interval") == 0 || strcmp(cmd, "get agc.reset.interval") == 0) {
        CP("agc.rst:%ds\n", configAgcResetInterval);
    }
    else if (strcmp(cmd, "set flood.advert.interval") == 0 || strcmp(cmd, "get flood.advert.interval") == 0) {
        CP("flood.adv.int:%luh\n", floodAdvertIntervalMs > 0 ? floodAdvertIntervalMs / 3600000UL : 0);
    }
    // --- Admin-only commands ---
    else if (!isAdmin) {
        return false;  // not a read-only command; caller handles admin gate
    }
    else if (strcmp(cmd, "set repeat on") == 0) {
        repeaterHelper.setRepeatEnabled(true); CP("rpt:on\n");
    }
    else if (strcmp(cmd, "set repeat off") == 0) {
        repeaterHelper.setRepeatEnabled(false); CP("rpt:off\n");
    }
    else if (strncmp(cmd, "set flood.max ", 14) == 0) {
        uint8_t hops = atoi(cmd + 14);
        if (hops >= 1 && hops <= 15) { repeaterHelper.setMaxFloodHops(hops); saveConfig(); CP("hops:%d\n", hops); }
    }
    else if (strcmp(cmd, "get loop.detect") == 0) {
        const char* mode = (loopDetectMode == LOOP_DETECT_OFF) ? "off" :
                           (loopDetectMode == LOOP_DETECT_MINIMAL) ? "minimal" :
                           (loopDetectMode == LOOP_DETECT_MODERATE) ? "moderate" : "strict";
        CP("> %s\n", mode);
    }
    else if (strncmp(cmd, "set loop.detect ", 16) == 0) {
        const char* arg = cmd + 16;
        uint8_t newMode = 0xFF;
        if (strcmp(arg, "off") == 0) newMode = LOOP_DETECT_OFF;
        else if (strcmp(arg, "minimal") == 0) newMode = LOOP_DETECT_MINIMAL;
        else if (strcmp(arg, "moderate") == 0) newMode = LOOP_DETECT_MODERATE;
        else if (strcmp(arg, "strict") == 0) newMode = LOOP_DETECT_STRICT;

        if (newMode != 0xFF) {
            loopDetectMode = newMode;
            saveConfig();
            const char* mode = (loopDetectMode == LOOP_DETECT_OFF) ? "off" :
                               (loopDetectMode == LOOP_DETECT_MINIMAL) ? "minimal" :
                               (loopDetectMode == LOOP_DETECT_MODERATE) ? "moderate" : "strict";
            CP("loop.detect=%s\n", mode);
        } else {
            CP("E:off|minimal|moderate|strict\n");
        }
    }
    else if (strcmp(cmd, "get autoadd.maxhops") == 0) {
        CP("> %d\n", autoAddMaxHops);
    }
    else if (strncmp(cmd, "set autoadd.maxhops ", 20) == 0) {
        int hops = atoi(cmd + 20);
        if (hops >= 0 && hops <= 64) {
            autoAddMaxHops = (uint8_t)hops;
            saveConfig();
            CP("autoadd.maxhops=%d\n", autoAddMaxHops);
        } else {
            CP("E:0-64\n");
        }
    }
    else if (strncmp(cmd, "set name ", 9) == 0 || strncmp(cmd, "name ", 5) == 0) {
        const char* n = strncmp(cmd, "set name ", 9) == 0 ? cmd + 9 : cmd + 5;
        if (strlen(n) > 0 && strlen(n) < 16) {
            nodeIdentity.setNodeName(n); nodeIdentity.save();
            CP("name=%s\n", n);
        } else CP("E:1-15\n");
    }
    else if (strcmp(cmd, "name") == 0) {
        CP("Name:%s\n", nodeIdentity.getNodeName());
    }
    else if (strncmp(cmd, "set lat ", 8) == 0) {
        int32_t latE6;
        if (parseFixed6(cmd + 8, &latE6) && latE6 >= -90000000 && latE6 <= 90000000) {
            int32_t lonE6 = nodeIdentity.hasLocation() ? nodeIdentity.getLongitude() : 0;
            nodeIdentity.setLocationInt(latE6, lonE6); nodeIdentity.save();
            CP("lat=%ld.%06ld\n", latE6/1000000, abs(latE6%1000000));
        } else CP("E:lat\n");
    }
    else if (strncmp(cmd, "set lon ", 8) == 0) {
        int32_t lonE6;
        if (parseFixed6(cmd + 8, &lonE6) && lonE6 >= -180000000 && lonE6 <= 180000000) {
            int32_t latE6 = nodeIdentity.hasLocation() ? nodeIdentity.getLatitude() : 0;
            nodeIdentity.setLocationInt(latE6, lonE6); nodeIdentity.save();
            CP("lon=%ld.%06ld\n", lonE6/1000000, abs(lonE6%1000000));
        } else CP("E:lon\n");
    }
    else if (strncmp(cmd, "set freq ", 9) == 0) {
        uint32_t freqM;
        if (parseMHz3(cmd + 9, &freqM) && freqM >= 150000 && freqM <= 960000) {
            tempFrequency = freqM / 1000.0f;
            if (!tempRadioActive) {
                tempBandwidth = MC_BANDWIDTH;
                tempSpreadingFactor = MC_SPREADING;
                tempCodingRate = MC_CODING_RATE;
            }
            tempRadioActive = true;
            setupRadio(); startReceive(); calculateTimings();
            CP("freq=%lu.%03lu\n", freqM/1000, freqM%1000);
        } else CP("E:freq\n");
    }
    else if (strncmp(cmd, "set advert.interval ", 20) == 0) {
        uint32_t minutes = strtoul(cmd + 20, NULL, 10);
        if (minutes >= 1 && minutes <= 1440) {
            advertGen.setInterval(minutes * 60000);
            saveConfig();
            CP("int:%lum\n", minutes);
        } else CP("E:1-1440\n");
    }
    else if (strncmp(cmd, "advert interval ", 16) == 0) {
        uint32_t interval = strtoul(cmd + 16, NULL, 10);
        if (interval >= 60 && interval <= 86400) {
            advertGen.setInterval(interval * 1000);
            saveConfig();
            CP("int:%lus\n", interval);
        } else CP("E:60-86400\n");
    }
    else if (strcmp(cmd, "advert") == 0) {
        sendAdvert(true); CP("adv sent\n");
    }
    else if (strcmp(cmd, "advert local") == 0) {
        sendAdvert(false); CP("adv local\n");
    }
    else if (strcmp(cmd, "ping") == 0) {
        sendPing(); CP("ping sent\n");
    }
    else if (strncmp(cmd, "ping ", 5) == 0) {
        uint8_t h = (uint8_t)strtoul(cmd + 5, NULL, 16);
        if (h != 0) { sendDirectedPing(h); CP("ping->%02X\n", h); }
        else CP("E:hex\n");
    }
    else if (strncmp(cmd, "trace ", 6) == 0) {
        uint8_t h = (uint8_t)strtoul(cmd + 6, NULL, 16);
        if (h != 0) { sendDirectedTrace(h); CP("trace->%02X\n", h); }
        else CP("E:hex\n");
    }
    else if (strcmp(cmd, "rxboost on") == 0) {
        rxBoostEnabled = true; applyPowerSettings(); saveConfig();
        CP("RxB:ON\n");
    }
    else if (strcmp(cmd, "rxboost off") == 0) {
        rxBoostEnabled = false; applyPowerSettings(); saveConfig();
        CP("RxB:OFF\n");
    }
    else if (strcmp(cmd, "sleep on") == 0) {
        deepSleepEnabled = true; saveConfig(); CP("sleep:on\n");
    }
    else if (strcmp(cmd, "sleep off") == 0) {
        deepSleepEnabled = false; saveConfig(); CP("sleep:off\n");
    }
    else if (strcmp(cmd, "ratelimit on") == 0) {
        repeaterHelper.setRateLimitEnabled(true); CP("RL:on\n");
    }
    else if (strcmp(cmd, "ratelimit off") == 0) {
        repeaterHelper.setRateLimitEnabled(false); CP("RL:off\n");
    }
    else if (strcmp(cmd, "ratelimit reset") == 0) {
        repeaterHelper.resetRateLimitStats(); CP("RL reset\n");
    }
    else if (strcmp(cmd, "alert on") == 0) {
        if (isPubKeySet(alertDestPubKey)) { alertEnabled = true; saveConfig(); CP("alert:on\n"); }
        else CP("E:no dest\n");
    }
    else if (strcmp(cmd, "alert off") == 0) {
        alertEnabled = false; saveConfig(); CP("alert:off\n");
    }
    else if (strcmp(cmd, "alert clear") == 0) {
        memset(alertDestPubKey, 0, REPORT_PUBKEY_SIZE);
        alertEnabled = false; saveConfig(); CP("alert clr\n");
    }
    else if (strncmp(cmd, "alert dest ", 11) == 0) {
        const char* arg = cmd + 11;
        Contact* c = contactMgr.findByName(arg);
        if (c) {
            memcpy(alertDestPubKey, c->pubKey, REPORT_PUBKEY_SIZE);
            saveConfig(); CP("alert->%s\n", c->name);
        }
        else if (!ctx.buf && strlen(arg) >= 64) {
            // Hex pubkey input only from serial
            for (int i = 0; i < 32; i++) {
                char byte[3] = {arg[i*2], arg[i*2+1], 0};
                alertDestPubKey[i] = strtoul(byte, NULL, 16);
            }
            saveConfig();
            CP("Dest:%02X%02X%02X%02X\n", alertDestPubKey[0], alertDestPubKey[1], alertDestPubKey[2], alertDestPubKey[3]);
        }
        else CP("E:not found\n");
    }
    else if (strncmp(cmd, "mode ", 5) == 0) {
        char m = cmd[5];
        if (m >= '0' && m <= '2') { powerSaveMode = m - '0'; saveConfig(); CP("mode:%c\n", m); }
        else CP("E:0-2\n");
    }
    else if (strcmp(cmd, "mailbox clear") == 0) {
        mailbox.clear(); CP("mbox clr\n");
    }
    else if (strncmp(cmd, "quiet ", 6) == 0) {
        if (strcmp(cmd + 6, "off") == 0) {
            repeaterHelper.disableQuietHours(); CP("quiet:off\n");
        } else {
            char* args = (char*)(cmd + 6);
            char* space = strchr(args, ' ');
            if (space) {
                *space = '\0';
                uint8_t start = (uint8_t)atoi(args);
                uint8_t end = (uint8_t)atoi(space + 1);
                if (start <= 23 && end <= 23) {
                    repeaterHelper.setQuietHours(start, end);
                    CP("quiet:%d-%d\n", start, end);
                } else CP("E:0-23\n");
            } else CP("E:quiet <start> <end>\n");
        }
    }
    else if (strcmp(cmd, "set tx auto on") == 0) {
        repeaterHelper.setAdaptiveTxEnabled(true); CP("TxP auto:on\n");
    }
    else if (strcmp(cmd, "set tx auto off") == 0) {
        repeaterHelper.setAdaptiveTxEnabled(false);
        repeaterHelper.setTxPower(MC_TX_POWER);
        radio.setOutputPower(MC_TX_POWER);
        CP("TxP:%ddBm auto:off\n", MC_TX_POWER);
    }
    else if (strncmp(cmd, "set tx ", 7) == 0 && strncmp(cmd, "set tx auto", 11) != 0) {
        int8_t p = (int8_t)atoi(cmd + 7);
        if (p >= ADAPTIVE_TX_MIN_POWER && p <= MC_TX_POWER) {
            repeaterHelper.setAdaptiveTxEnabled(false);
            repeaterHelper.setTxPower(p);
            radio.setOutputPower(p);
            CP("TxP:%ddBm\n", p);
        } else CP("E:%d-%d\n", ADAPTIVE_TX_MIN_POWER, MC_TX_POWER);
    }
    else if (strncmp(cmd, "txpower ", 8) == 0) {
        int8_t p = (int8_t)atoi(cmd + 8);
        if (p >= ADAPTIVE_TX_MIN_POWER && p <= MC_TX_POWER) {
            repeaterHelper.setAdaptiveTxEnabled(false);
            repeaterHelper.setTxPower(p);
            radio.setOutputPower(p);
            CP("TxP:%ddBm\n", p);
        } else CP("E:%d-%d\n", ADAPTIVE_TX_MIN_POWER, MC_TX_POWER);
    }
    else if (strcmp(cmd, "clear stats") == 0) {
        rxCount = 0; txCount = 0; fwdCount = 0; errCount = 0;
        advTxCount = 0; advRxCount = 0;
        CP("stats clr\n");
    }
    else if (strcmp(cmd, "powersaving on") == 0) {
        powerSaveMode = 2; saveConfig(); CP("PS:2\n");
    }
    else if (strcmp(cmd, "powersaving off") == 0) {
        powerSaveMode = 0; saveConfig(); CP("PS:0\n");
    }
    else if (strncmp(cmd, "neighbor.remove ", 16) == 0) {
        const char* prefix = cmd + 16;
        uint8_t prefixBytes[6];
        uint8_t prefixLen = 0;
        for (int i = 0; prefix[i*2] && prefix[i*2+1] && i < 6; i++) {
            char byte[3] = {prefix[i*2], prefix[i*2+1], 0};
            prefixBytes[i] = (uint8_t)strtoul(byte, NULL, 16);
            prefixLen++;
        }
        if (prefixLen > 0) {
            bool removed = repeaterHelper.getNeighbours().removeByPrefix(prefixBytes, prefixLen);
            CP(removed ? "nbr rm\n" : "E:not found\n");
        } else CP("E:hex prefix\n");
    }
    else if (strncmp(cmd, "setperm ", 8) == 0) {
        // setperm <pubkey_hex> <0-3> or setperm <pubkey_hex> (remove)
        char* args = (char*)(cmd + 8);
        char* space = strchr(args, ' ');
        if (space) {
            *space = '\0';
            uint8_t perm = (uint8_t)atoi(space + 1);
            if (perm <= 3 && strlen(args) >= 12) {
                uint8_t pk[6];
                for (int i = 0; i < 6; i++) {
                    char b[3] = {args[i*2], args[i*2+1], 0};
                    pk[i] = (uint8_t)strtoul(b, NULL, 16);
                }
                repeaterHelper.getACL().setPermission(pk, perm);
                CP("perm:%02X%02X%02X=%d\n", pk[0], pk[1], pk[2], perm);
            } else CP("E:setperm <12+hex> <0-3>\n");
        } else if (strlen(args) >= 12) {
            // Remove permission
            uint8_t pk[6];
            for (int i = 0; i < 6; i++) {
                char b[3] = {args[i*2], args[i*2+1], 0};
                pk[i] = (uint8_t)strtoul(b, NULL, 16);
            }
            bool ok = repeaterHelper.getACL().removeEntry(pk);
            CP(ok ? "perm rm\n" : "E:not found\n");
        } else CP("E:setperm <12+hex> [0-3]\n");
    }
    else if (strncmp(cmd, "set txdelay ", 12) == 0) {
        uint16_t v = (uint16_t)atoi(cmd + 12);
        if (v <= 500) { configTxDelayFactor = v; CP("txdelay:%d\n", v); }
        else CP("E:0-500\n");
    }
    else if (strncmp(cmd, "set rxdelay ", 12) == 0) {
        uint16_t v = (uint16_t)atoi(cmd + 12);
        if (v <= 500) { configRxDelayFactor = v; CP("rxdelay:%d\n", v); }
        else CP("E:0-500\n");
    }
    else if (strncmp(cmd, "set direct.txdelay ", 19) == 0) {
        uint16_t v = (uint16_t)atoi(cmd + 19);
        if (v <= 500) { configDirectTxDelay = v; CP("direct.txdelay:%d\n", v); }
        else CP("E:0-500\n");
    }
    else if (strncmp(cmd, "set flood.advert.interval ", 25) == 0) {
        uint32_t hours = strtoul(cmd + 25, NULL, 10);
        if (hours == 0) { floodAdvertIntervalMs = 0; saveConfig(); CP("flood.adv.int:auto\n"); }
        else if (hours >= 3 && hours <= 48) { floodAdvertIntervalMs = hours * 3600000UL; saveConfig(); CP("flood.adv.int:%luh\n", hours); }
        else CP("E:0,3-48\n");
    }
    else if (strncmp(cmd, "set agc.reset.interval ", 23) == 0) {
        uint16_t secs = (uint16_t)atoi(cmd + 23);
        secs = (secs / 4) * 4;  // Round down to multiple of 4
        if (secs <= 3600) { configAgcResetInterval = secs; CP("agc.rst:%ds\n", secs); }
        else CP("E:0-3600\n");
    }
    else if (strncmp(cmd, "set af ", 7) == 0) {
        // Parse "X.Y" as tenths (e.g. "1.5" -> 15)
        const char* s = cmd + 7;
        uint8_t whole = (uint8_t)atoi(s);
        uint8_t frac = 0;
        const char* dot = strchr(s, '.');
        if (dot && dot[1] >= '0' && dot[1] <= '9') frac = dot[1] - '0';
        uint8_t val = whole * 10 + frac;
        if (val <= 90) { configAirtimeFactor = val; CP("af:%d.%d\n", val / 10, val % 10); }
        else CP("E:0.0-9.0\n");
    }
    else if (strncmp(cmd, "set adc.multiplier ", 19) == 0) {
        const char* s = cmd + 19;
        uint8_t whole = (uint8_t)atoi(s);
        uint8_t frac = 0;
        const char* dot = strchr(s, '.');
        if (dot && dot[1] >= '0' && dot[1] <= '9') frac = dot[1] - '0';
        uint8_t val = whole * 10 + frac;
        if (val <= 100) { configAdcMultiplier = val; CP("adc.mul:%d.%d\n", val / 10, val % 10); }
        else CP("E:0.0-10.0\n");
    }
    else if (strncmp(cmd, "set owner.info ", 15) == 0) {
        const char* txt = cmd + 15;
        strncpy(ownerInfo, txt, MC_OWNER_INFO_MAX - 1);
        ownerInfo[MC_OWNER_INFO_MAX - 1] = '\0';
        CP("owner.info set\n");
    }
    else if (strcmp(cmd, "save") == 0) {
        saveConfig(); CP("saved\n");
    }
    else if (strcmp(cmd, "reset") == 0) {
        resetConfig(); applyPowerSettings(); CP("reset\n");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        CP("reboot\n");
    }
    else if (strcmp(cmd, "clkreboot") == 0) {
        timeSync = TimeSync();
        CP("clkreboot\n");
    }
    // Region commands (MeshCore 1.10.0+)
    else if (strcmp(cmd, "region") == 0) {
        CP("*:%c\n", (regionMap.getWildcard().flags & REGION_DENY_FLOOD) ? 'D' : 'A');
        for (uint8_t i = 0; i < regionMap.getCount(); i++) {
            const RegionEntry* e = regionMap.getEntry(i);
            CP("%s:%c\n", e->name, (e->flags & REGION_DENY_FLOOD) ? 'D' : 'A');
        }
    }
    else if (isAdmin && strncmp(cmd, "region put ", 11) == 0) {
        CP(regionMap.put(cmd + 11) ? "OK\n" : "ERR\n");
    }
    else if (isAdmin && strncmp(cmd, "region remove ", 14) == 0) {
        CP(regionMap.remove(cmd + 14) ? "OK\n" : "ERR\n");
    }
    else if (isAdmin && strncmp(cmd, "region allowf ", 14) == 0) {
        CP(regionMap.allowFlood(cmd + 14) ? "OK\n" : "ERR\n");
    }
    else if (isAdmin && strncmp(cmd, "region denyf ", 13) == 0) {
        CP(regionMap.denyFlood(cmd + 13) ? "OK\n" : "ERR\n");
    }
    else if (isAdmin && strcmp(cmd, "region save") == 0) {
        CP(regionMap.save() ? "OK\n" : "ERR\n");
    }
    else if (isAdmin && strcmp(cmd, "region load") == 0) {
        CP(regionMap.load() ? "OK\n" : "ERR\n");
    }
    else {
        return false;  // command not handled
    }
    return true;
}

//=============================================================================
// Serial Command Handler
//=============================================================================
#ifndef SILENT
char cmdBuffer[48];
uint8_t cmdPos = 0;

// Unified radio param parser for tempradio and set radio
static void parseAndApplyRadio(const char* args, char sep) {
    static char buf[48];
    strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf)-1] = 0;
    if (sep == ',') { for (char* q = buf; *q; q++) if (*q == ',') *q = ' '; }
    char* p = buf;
    char* tok[5]; uint8_t ti = 0;
    while (*p && ti < 5) {
        while (*p == ' ') p++;
        if (!*p) break;
        tok[ti++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    uint32_t freqM, bwT;
    if (ti >= 4 && parseMHz3(tok[0], &freqM) && parseBW1(tok[1], &bwT)) {
        int sf = atoi(tok[2]), cr = atoi(tok[3]);
        if (freqM < 150000 || freqM > 960000) LOG_RAW("E:freq\n\r");
        else if (bwT < 78 || bwT > 5000) LOG_RAW("E:bw\n\r");
        else if (sf < 6 || sf > 12) LOG_RAW("E:sf\n\r");
        else if (cr < 5 || cr > 8) LOG_RAW("E:cr\n\r");
        else {
            tempFrequency = freqM / 1000.0f;
            tempBandwidth = bwT / 10.0f;
            tempSpreadingFactor = sf; tempCodingRate = cr;
            tempRadioActive = true;
            if (ti == 5) {
                uint32_t mins = strtoul(tok[4], NULL, 10);
                tempRadioExpireTime = (mins > 0 && mins <= 1440) ? millis() + mins * 60000UL : 0;
            } else tempRadioExpireTime = 0;
            setupRadio(); startReceive(); calculateTimings();
            LOG_RAW("%lu.%03lu BW%lu.%lu SF%d CR%d OK\n\r",
                freqM/1000, freqM%1000, bwT/10, bwT%10, sf, cr);
        }
    } else LOG_RAW("E:<freq> <bw> <sf> <cr>\n\r");
}

void processCommand(char* cmd) {
    CmdCtx ctx = { NULL, 0, 0 };

    // Try shared dispatcher first (serial is always admin)
    if (dispatchSharedCommand(cmd, ctx, true)) {
        // "reboot" and "clkreboot" need special handling for serial
        if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "clkreboot") == 0) {
            delay(100);
            #ifdef CUBECELL
            NVIC_SystemReset();
            #endif
        }
        return;
    }

    // Serial-only commands
    if (strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0) {
        LOG_RAW("status stats nodes neighbours ver time advert radio\n\r"
                "set name|lat|lon|tx|repeat|flood.max|radio\n\r"
                "password region acl save reset reboot\n\r");
    }
    else if (strcmp(cmd, "newid") == 0) {
        LOG_RAW("Gen new ID...\n\r");
        nodeIdentity.reset();
        LOG_RAW("New: %s %02X - reboot\n\r", nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
    }
    else if (strcmp(cmd, "get prv.key") == 0) {
        const uint8_t* pk = nodeIdentity.getPrivateKey();
        for (int i = 0; i < MC_PRIVATE_KEY_SIZE; i++) LOG_RAW("%02x", pk[i]);
        LOG_RAW("\n\r");
    }
    else if (strncmp(cmd, "set prv.key ", 12) == 0) {
        const char* hex = cmd + 12;
        if (strlen(hex) == 64) {
            uint8_t seed[32];
            for (int i = 0; i < 32; i++) {
                char b[3] = {hex[i*2], hex[i*2+1], 0};
                seed[i] = (uint8_t)strtoul(b, NULL, 16);
            }
            nodeIdentity.importSeed(seed);
            nodeIdentity.save();
            LOG_RAW("Key set %02X - reboot\n\r", nodeIdentity.getNodeHash());
        } else LOG_RAW("E:64 hex chars (32-byte seed)\n\r");
    }
    else if (strcmp(cmd, "nodetype chat") == 0) {
        uint8_t flags = nodeIdentity.getFlags();
        flags = (flags & 0xF0) | MC_TYPE_CHAT_NODE;
        nodeIdentity.setFlags(flags); nodeIdentity.save();
        LOG_RAW("Type: CHAT 0x%02X\n\r", flags);
    }
    else if (strcmp(cmd, "nodetype repeater") == 0) {
        uint8_t flags = nodeIdentity.getFlags();
        flags = (flags & 0xF0) | MC_TYPE_REPEATER;
        nodeIdentity.setFlags(flags); nodeIdentity.save();
        LOG_RAW("Type: RPT 0x%02X\n\r", flags);
    }
    else if (strcmp(cmd, "password") == 0) {
        LOG_RAW("A:%s G:%s\n\r",
            sessionManager.getAdminPassword(), sessionManager.getGuestPassword());
    }
    else if (strncmp(cmd, "password admin ", 15) == 0) {
        sessionManager.setAdminPassword(cmd + 15); saveConfig();
        LOG_RAW("A:%s\n\r", cmd + 15);
    }
    else if (strncmp(cmd, "password guest ", 15) == 0) {
        sessionManager.setGuestPassword(cmd + 15); saveConfig();
        LOG_RAW("G:%s\n\r", cmd + 15);
    }
    else if (strcmp(cmd, "savestats") == 0) {
        savePersistentStats(); LOG_RAW("Stats saved\n\r");
    }
    else if (strcmp(cmd, "contacts") == 0) {
        LOG_RAW("Contacts: %d\n\r", contactMgr.getCount());
        for (uint8_t i = 0; i < contactMgr.getCount(); i++) {
            Contact* c = contactMgr.getContact(i);
            if (c) LOG_RAW(" %02X %s %ddBm\n\r", c->getHash(), c->name[0] ? c->name : "-", c->lastRssi);
        }
    }
    else if (strncmp(cmd, "contact ", 8) == 0) {
        uint8_t hash = strtoul(cmd + 8, NULL, 16);
        Contact* c = contactMgr.findByHash(hash);
        if (c) {
            LOG_RAW("Contact: %s (hash %02X)\n\r", c->name, c->getHash());
            LOG_RAW("PubKey: ");
            for (int i = 0; i < 32; i++) LOG_RAW("%02X", c->pubKey[i]);
            LOG_RAW("\n\r");
        } else LOG_RAW("Contact %02X not found\n\r", hash);
    }
    else if (strncmp(cmd, "time ", 5) == 0) {
        uint32_t ts = strtoul(cmd + 5, NULL, 10);
        if (ts > 1577836800) { timeSync.setTime(ts); LOG_RAW("Time set: %lu\n\r", ts); }
        else LOG_RAW("Invalid timestamp\n\r");
    }
    else if (strcmp(cmd, "alert test") == 0) {
        if (sendAlertAsChatNode("Test alert")) LOG_RAW("Test alert sent\n\r");
        else LOG_RAW("Alert not set\n\r");
    }
    // Temporary radio: tempradio <freq> <bw> <sf> <cr>
    else if (strncmp(cmd, "tempradio ", 10) == 0) {
        if (strcmp(cmd + 10, "off") == 0) {
            if (tempRadioActive) {
                tempRadioActive = false; tempRadioExpireTime = 0;
                setupRadio(); startReceive(); calculateTimings();
                LOG_RAW("OK\n\r");
            } else LOG_RAW("not active\n\r");
        } else {
            parseAndApplyRadio(cmd + 10, ' ');
        }
    }
    else if (strcmp(cmd, "tempradio") == 0) {
        if (tempRadioActive) {
            uint32_t fM = (uint32_t)(tempFrequency * 1000);
            uint32_t bT = (uint32_t)(tempBandwidth * 10);
            LOG_RAW("%lu.%03lu BW%lu.%lu SF%d CR%d\n\r",
                fM/1000, fM%1000, bT/10, bT%10, tempSpreadingFactor, tempCodingRate);
        }
        else LOG_RAW("off\n\r");
    }
    else if (strncmp(cmd, "set radio ", 10) == 0) {
        parseAndApplyRadio(cmd + 10, ',');
    }
#ifndef LITE_MODE
    else if (strncmp(cmd, "msg ", 4) == 0) {
        char* nameStart = cmd + 4;
        char* msgStart = strchr(nameStart, ' ');
        if (msgStart) {
            *msgStart = '\0'; msgStart++;
            if (strlen(msgStart) > 0) sendDirectMessage(nameStart, msgStart);
            else LOG_RAW("Empty msg\n\r");
        } else LOG_RAW("msg <name> <message>\n\r");
    }
#endif
    else if (strlen(cmd) > 0) {
        LOG_RAW("Unknown: %s\n\r", cmd);
    }
}
// End of processCommand

void checkSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdPos > 0) {
                cmdBuffer[cmdPos] = '\0';
                processCommand(cmdBuffer);
                cmdPos = 0;
            }
        } else if (cmdPos < sizeof(cmdBuffer) - 1) {
            cmdBuffer[cmdPos++] = c;
        }
    }
}
#endif

//=============================================================================
// Remote CLI Command Processing
//=============================================================================

/**
 * Process a CLI command received via mesh network
 * Returns response in the provided buffer
 *
 * Supported remote commands (admin only):
 * - status, stats, time, telemetry, nodes, neighbours
 * - set repeat on/off, set password, set guest
 * - set flood.max, name, location, reboot
 * - advert, advert interval
 *
 * @param cmd Command string (null-terminated)
 * @param response Output buffer for response
 * @param maxLen Maximum response length
 * @param isAdmin true if sender has admin permissions
 * @return Length of response, 0 if command not allowed
 */
uint16_t processRemoteCommand(const char* cmd, char* response, uint16_t maxLen, bool isAdmin) {
    CmdCtx ctx = { response, 0, maxLen };

    // Try shared dispatcher first
    if (dispatchSharedCommand(cmd, ctx, isAdmin)) {
        // Handle reboot for remote
        if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "clkreboot") == 0) {
            pendingReboot = true;
            rebootTime = millis() + 500;
        }
        return ctx.len;
    }

    // If not admin and command wasn't a read-only shared command
    if (!isAdmin) {
        int rc = snprintf(response + ctx.len, maxLen - ctx.len, "Err:admin\n");
        if (rc > 0 && ctx.len + rc < maxLen) ctx.len += rc;
        return ctx.len;
    }

    #define RESP_APPEND(...) do { \
        int _rc = snprintf(response + ctx.len, maxLen - ctx.len, __VA_ARGS__); \
        if (_rc > 0 && ctx.len + _rc < maxLen) ctx.len += _rc; \
    } while(0)

    // Remote-only admin commands
    if (strncmp(cmd, "password ", 9) == 0) {
        const char* pwd = cmd + 9;
        if (strlen(pwd) > 0 && strlen(pwd) <= 15) {
            sessionManager.setAdminPassword(pwd); saveConfig();
            RESP_APPEND("pwd set\n");
        } else RESP_APPEND("E:1-15\n");
    }
    else if (strncmp(cmd, "set password ", 13) == 0) {
        const char* pwd = cmd + 13;
        if (strlen(pwd) > 0 && strlen(pwd) <= 15) {
            sessionManager.setAdminPassword(pwd); saveConfig();
            RESP_APPEND("pwd set\n");
        } else RESP_APPEND("E:1-15\n");
    }
    else if (strncmp(cmd, "set guest.password ", 19) == 0 || strncmp(cmd, "set guest ", 10) == 0) {
        const char* pwd = strncmp(cmd, "set guest.password ", 19) == 0 ? cmd + 19 : cmd + 10;
        if (strlen(pwd) <= 15) {
            sessionManager.setGuestPassword(pwd); saveConfig();
            RESP_APPEND("guest set\n");
        } else RESP_APPEND("E:0-15\n");
    }
    else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        RESP_APPEND("status stats time ver clock nodes identity telemetry\n");
        RESP_APPEND("radio location ping rxboost sleep alert powersaving\n");
        RESP_APPEND("set name set lat set lon set tx set advert.interval\n");
        RESP_APPEND("password set guest.password clear stats neighbor.remove\n");
        RESP_APPEND("ratelimit mode power mailbox advert save erase reboot");
    }
    else {
        RESP_APPEND("E:?\n");
    }

    #undef RESP_APPEND
    return ctx.len;
}

