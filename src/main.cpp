/**
 * CubeCellMeshCore - MeshCore Compatible Repeater
 * Lightweight repeater firmware for Heltec CubeCell boards
 *
 * Based on MeshCore protocol: https://github.com/meshcore-dev/MeshCore
 */

#include "main.h"
#include "core/Led.h"
#include "core/Config.h"

//=============================================================================
// Forward declarations
//=============================================================================
#ifndef LITE_MODE
void sendDirectMessage(const char* recipientName, const char* message);
#endif
bool transmitPacket(MCPacket* pkt);
void startReceive();
bool sendNodeAlert(const char* nodeName, uint8_t nodeHash, uint8_t nodeType, int16_t rssi);
bool sendAlertAsChatNode(const char* text);
uint32_t getPacketId(MCPacket* pkt);

// Health monitor thresholds
#define HEALTH_OFFLINE_MS       1800000  // 30 minutes
#define HEALTH_SNR_DROP_THRESH  24       // 6dB drop in SNR*4 units

// Helper: check if a pubkey buffer is non-zero
static inline bool isPubKeySet(const uint8_t* key) {
    for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
        if (key[i] != 0) return true;
    }
    return false;
}

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

static void cmdPrint(CmdCtx* ctx, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void cmdPrint(CmdCtx* ctx, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (ctx->buf) {
        int rc = vsnprintf(ctx->buf + ctx->len, ctx->maxLen - ctx->len, fmt, ap);
        if (rc > 0 && ctx->len + rc < ctx->maxLen) ctx->len += rc;
    } else {
        #ifndef SILENT
        static char tmp[96];
        vsnprintf(tmp, sizeof(tmp), fmt, ap);
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
#ifdef ENABLE_PACKET_LOG
    else if (strcmp(cmd, "log") == 0) {
        CP("Log:%s N:%lu/%d\n", packetLogger.isEnabled() ? "on" : "off",
            packetLogger.getTotalLogged(), packetLogger.getCount());
        uint8_t show = packetLogger.getCount();
        if (show > 8) show = 8;
        for (uint8_t i = 0; i < show; i++) {
            if (ctx.buf && ctx.len >= ctx.maxLen - 32) break;
            const PacketLogEntry* e = packetLogger.getEntry(i);
            if (e) CP(" %s r%d t%d %02X>%02X s%d\n",
                e->direction ? "TX" : "RX", e->routeType, e->payloadType,
                e->srcHash, e->dstHash, e->snr);
        }
    }
#endif
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
    #ifdef ENABLE_CRYPTO_TESTS
    else if (strcmp(cmd, "test") == 0) {
        const uint8_t pubkey[] = {
            0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
            0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
            0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
            0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a
        };
        const uint8_t sig[] = {
            0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72,
            0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
            0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74,
            0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55,
            0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac,
            0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
            0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24,
            0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b
        };
        bool ok = IdentityManager::verify(sig, pubkey, NULL, 0);
        LOG_RAW("RFC8032 Verify (empty msg): %s\n\r", ok ? "PASS" : "FAIL");
        const uint8_t seed[] = {
            0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60,
            0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
            0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19,
            0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60
        };
        const uint8_t expected_sig[] = {
            0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72,
            0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
            0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74,
            0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55,
            0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac,
            0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
            0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24,
            0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b
        };
        uint8_t test_pubkey[32];
        uint8_t test_privkey[64];
        ed25519_create_keypair(test_pubkey, test_privkey, seed);
        bool pubkey_ok = (memcmp(test_pubkey, pubkey, 32) == 0);
        LOG_RAW("RFC8032 Keypair gen: %s\n\r", pubkey_ok ? "PASS" : "FAIL");
        uint8_t test_sig[64];
        ed25519_sign(test_sig, NULL, 0, test_pubkey, test_privkey);
        bool sign_ok = (memcmp(test_sig, expected_sig, 64) == 0);
        LOG_RAW("RFC8032 Sign (empty msg): %s\n\r", sign_ok ? "PASS" : "FAIL");
    }
    #endif
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
#ifdef ENABLE_PACKET_LOG
    else if (strcmp(cmd, "log start") == 0) {
        packetLogger.setEnabled(true); LOG_RAW("Log:on\n\r");
    }
    else if (strcmp(cmd, "log stop") == 0) {
        packetLogger.setEnabled(false); LOG_RAW("Log:off\n\r");
    }
    else if (strcmp(cmd, "log erase") == 0) {
        packetLogger.clear(); LOG_RAW("Log erased\n\r");
    }
#endif
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
#ifdef ENABLE_DAILY_REPORT
    else if (strcmp(cmd, "report") == 0) {
        LOG_RAW("Report:%s Time:%02d:%02d Dest:%s\n\r",
            reportEnabled ? "ON" : "OFF", reportHour, reportMinute,
            isPubKeySet(reportDestPubKey) ? "set" : "none");
    }
    else if (strcmp(cmd, "report on") == 0) {
        if (isPubKeySet(reportDestPubKey)) { reportEnabled = true; saveConfig(); LOG_RAW("Report ON\n\r"); }
        else LOG_RAW("No dest key\n\r");
    }
    else if (strcmp(cmd, "report off") == 0) {
        reportEnabled = false; saveConfig(); LOG_RAW("Report OFF\n\r");
    }
    else if (strcmp(cmd, "report clear") == 0) {
        reportEnabled = false; memset(reportDestPubKey, 0, REPORT_PUBKEY_SIZE);
        saveConfig(); LOG_RAW("Report cleared\n\r");
    }
    else if (strcmp(cmd, "report test") == 0) {
        if (isPubKeySet(reportDestPubKey)) {
            extern bool sendDailyReport();
            LOG_RAW(sendDailyReport() ? "Report sent\n\r" : "Report fail\n\r");
        } else LOG_RAW("No dest key\n\r");
    }
    else if (strncmp(cmd, "report dest ", 12) == 0) {
        Contact* c = contactMgr.findByName(cmd + 12);
        if (c) { memcpy(reportDestPubKey, c->pubKey, REPORT_PUBKEY_SIZE); saveConfig(); LOG_RAW("Dest:%s\n\r", c->name); }
        else LOG_RAW("'%s' not found\n\r", cmd + 12);
    }
    else if (strncmp(cmd, "report time ", 12) == 0) {
        int h, m;
        if (sscanf(cmd + 12, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            reportHour = h; reportMinute = m; saveConfig();
            LOG_RAW("Report time: %02d:%02d\n\r", reportHour, reportMinute);
        } else LOG_RAW("Use: report time HH:MM\n\r");
    }
#endif
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
#ifdef ENABLE_DAILY_REPORT
    else if (strcmp(cmd, "report") == 0) {
        RESP_APPEND("Rpt:%s %02d:%02d D:%02X%s\n",
            reportEnabled ? "ON" : "OFF", reportHour, reportMinute,
            reportDestPubKey[0], isPubKeySet(reportDestPubKey) ? "" : "(no)");
    }
    else if (strncmp(cmd, "report dest ", 12) == 0) {
        Contact* c = contactMgr.findByName(cmd + 12);
        if (c) { memcpy(reportDestPubKey, c->pubKey, REPORT_PUBKEY_SIZE); saveConfig(); RESP_APPEND("Dest:%s\n", c->name); }
        else RESP_APPEND("E:not found\n");
    }
    else if (strcmp(cmd, "report on") == 0) {
        if (isPubKeySet(reportDestPubKey)) { reportEnabled = true; saveConfig(); RESP_APPEND("Rpt ON\n"); }
        else RESP_APPEND("E:no dest\n");
    }
    else if (strcmp(cmd, "report off") == 0) {
        reportEnabled = false; saveConfig(); RESP_APPEND("Rpt OFF\n");
    }
    else if (strcmp(cmd, "report test") == 0) {
        extern uint16_t generateReportContent(char*, uint16_t);
        ctx.len = generateReportContent(response, maxLen - 1);
    }
    else if (strcmp(cmd, "report nodes") == 0) {
        extern uint16_t generateNodesReport(char*, uint16_t);
        ctx.len = generateNodesReport(response, maxLen - 1);
    }
    else if (strncmp(cmd, "report time ", 12) == 0) {
        int h, m;
        if (sscanf(cmd + 12, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            reportHour = h; reportMinute = m; saveConfig();
            RESP_APPEND("Rpt %02d:%02d\n", h, m);
        } else RESP_APPEND("E:HH:MM\n");
    }
#endif
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

//=============================================================================
// Power Management
//=============================================================================
void applyPowerSettings() {
    radio.setRxBoostedGainMode(rxBoostEnabled, true);
    LOG(TAG_CONFIG " RxB=%s DS=%s M=%d\n\r",
        rxBoostEnabled ? "1" : "0",
        deepSleepEnabled ? "1" : "0",
        powerSaveMode);
}

void enterDeepSleep() {
#ifdef CUBECELL
    #ifndef SILENT
    UART_1_Sleep;
    #endif
    pinMode(P4_1, ANALOG);  // SPI MISO low power
    CySysPmDeepSleep();
    systime = (uint32_t)RtcGetTimerValue();
    pinMode(P4_1, INPUT);
    #ifndef SILENT
    UART_1_Wakeup;
    #endif
#endif
}

void enterLightSleep(uint8_t ms) {
#ifdef CUBECELL
    pinMode(P4_1, ANALOG);
#endif
    delay(ms);
#ifdef CUBECELL
    pinMode(P4_1, INPUT);
#endif
}

//=============================================================================
// Node ID and Timing
//=============================================================================
uint32_t generateNodeId() {
#ifdef CUBECELL
    // Use CubeCell built-in getID() function
    uint64_t chipId = getID();
    // Hash the 64-bit ID down to 24 bits, keep CC prefix
    uint32_t hash = (uint32_t)(chipId ^ (chipId >> 32));
    hash = ((hash >> 16) ^ hash) & 0x00FFFFFF;
    return 0xCC000000 | hash;
#else
    return 0xCC000000 | (random(0xFFFFFF));
#endif
}

void calculateTimings() {
    // Calculate timing based on LoRa settings using integer math (microseconds)
    uint32_t bwHz = (uint32_t)(MC_BANDWIDTH * 1000.0f);
    // tSym in microseconds: (2^SF * 1000000) / bwHz
    uint32_t tSymUs = ((uint32_t)(1 << MC_SPREADING) * 1000000UL) / bwHz;

    // Preamble time = (preambleLen + 4.25) * tSym = (preambleLen*4 + 17) * tSym / 4
    preambleTimeMsec = ((uint32_t)(MC_PREAMBLE_LEN * 4 + 17) * tSymUs) / 4000;

    // Slot time for CSMA: (8.5 symbols + margin) = (17 * tSym / 2 + 10ms)
    slotTimeMsec = (17 * tSymUs) / 2000 + 10;

    // Max packet time for 255 bytes payload
    // PayloadSymbols = 8 + max(ceil((8*PL - 4*SF + 28 + 16) / (4*SF)) * CR, 0)
    int32_t num = 8 * 255 - 4 * MC_SPREADING + 28 + 16;
    int32_t den = 4 * MC_SPREADING;
    int32_t numPayloadSym = 8;
    if (num > 0) numPayloadSym += ((num + den - 1) / den) * MC_CODING_RATE;

    // Total: (preambleLen*4 + 17) * tSym / 4 + payloadSym * tSym
    uint32_t preambleUs = ((uint32_t)(MC_PREAMBLE_LEN * 4 + 17) * tSymUs) / 4;
    maxPacketTimeMsec = (preambleUs + (uint32_t)numPayloadSym * tSymUs) / 1000 + 50;

    LOG(TAG_RADIO " T: p=%lu s=%lu m=%lu\n\r",
        preambleTimeMsec, slotTimeMsec, maxPacketTimeMsec);
}

/**
 * Calculate airtime for a given packet length using current LoRa parameters
 * @param packetLen Total packet length in bytes (header + path + payload)
 * @return Airtime in milliseconds
 */
uint32_t calculatePacketAirtime(uint16_t packetLen) {
    uint32_t bwHz = (uint32_t)(getCurrentBandwidth() * 1000.0f);
    uint8_t sf = getCurrentSpreadingFactor();
    uint8_t cr = getCurrentCodingRate();
    // tSym in microseconds: (2^SF * 1000000) / bwHz
    uint32_t tSymUs = ((uint32_t)(1 << sf) * 1000000UL) / bwHz;

    // Preamble: (preambleLen + 4.25) * tSym = (preambleLen*4 + 17) * tSym / 4
    uint32_t preambleUs = ((uint32_t)(MC_PREAMBLE_LEN * 4 + 17) * tSymUs) / 4;

    // Payload symbols: 8 + max(ceil((8*PL - 4*SF + 28 + 16) / (4*SF)) * CR, 0)
    int32_t num = 8 * (int32_t)packetLen - 4 * sf + 28 + 16;
    int32_t den = 4 * sf;
    int32_t payloadSym = 8;
    if (num > 0) payloadSym += ((num + den - 1) / den) * cr;

    return (preambleUs + (uint32_t)payloadSym * tSymUs) / 1000 + 1;
}

// SNR-weighted delay lookup table (MeshCore-style)
// Index 0 = worst SNR (-20dB), index 10 = best SNR (+15dB)
// Values are delay multipliers x1000 (integer math, no float)
static const uint16_t snrDelayTable[] = {
    1293, 1105, 936, 783, 645, 521, 410, 310, 220, 139, 65
};

uint8_t calcSnrScore(int8_t snr) {
    // snr is in 0.25dB units (SNR*4). Map to index [0-10].
    // -20dB (*4=-80) -> 0, +15dB (*4=60) -> 10
    int16_t clamped = snr;
    if (clamped < -80) clamped = -80;
    if (clamped > 60) clamped = 60;
    // Linear map: (clamped + 80) * 10 / 140
    return (uint8_t)(((int16_t)(clamped + 80) * 10) / 140);
}

uint32_t calcRxDelay(uint8_t scoreIdx, uint32_t airtimeMs) {
    // Better SNR = lower index value = shorter delay = faster retransmission
    if (scoreIdx > 10) scoreIdx = 10;
    return (uint32_t)snrDelayTable[scoreIdx] * airtimeMs / 1000;
}

uint32_t calcTxJitter(uint32_t airtimeMs) {
    uint32_t slotTime = airtimeMs * 2;
    return random(0, 7) * slotTime;  // 0-6 slots
}

bool isActivelyReceiving() {
    uint16_t irq = radio.getIrqStatus();
    bool detected = (irq & (RADIOLIB_SX126X_IRQ_HEADER_VALID |
                            RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED));

    if (detected) {
        uint32_t now = millis();
        if (activeReceiveStart == 0) {
            activeReceiveStart = now;
        } else if ((now - activeReceiveStart > 2 * preambleTimeMsec) &&
                   !(irq & RADIOLIB_SX126X_IRQ_HEADER_VALID)) {
            // False preamble detection
            activeReceiveStart = 0;
            return false;
        } else if (now - activeReceiveStart > maxPacketTimeMsec) {
            // Timeout, should have received by now
            activeReceiveStart = 0;
            return false;
        }
    }
    return detected;
}

void feedWatchdog() {
#if MC_WATCHDOG_ENABLED && defined(CUBECELL)
    // CubeCell internal watchdog feed
    feedInnerWdt();
#endif
}

void handleRadioError() {
    radioErrorCount++;
    errCount++;

    if (radioErrorCount >= MC_MAX_RADIO_ERRORS) {
        LOG(TAG_WARN " Radio err limit, reset\n\r");
        radio.reset();
        delay(100);
        setupRadio();
        radioErrorCount = 0;
    }

    if (errCount >= MC_MAX_TOTAL_ERRORS) {
        LOG(TAG_FATAL " Err limit, reboot\n\r");
        delay(100);
        #ifdef CUBECELL
        NVIC_SystemReset();
        #endif
    }
}

//=============================================================================
// Radio Functions
//=============================================================================

// Get current radio parameters (temporary if active, otherwise default)
// Radio parameter helpers are now defined as inline in main.h

void setupRadio() {
    // Use temporary parameters if active, otherwise defaults
    float freq = getCurrentFrequency();
    float bw = getCurrentBandwidth();
    uint8_t sf = getCurrentSpreadingFactor();
    uint8_t cr = getCurrentCodingRate();

    LOG(TAG_RADIO " SX1262 init%s\n\r", tempRadioActive ? " TMP" : "");

    radioError = radio.begin(
        freq,
        bw,
        sf,
        cr,
        MC_SYNCWORD,
        MC_TX_POWER,
        MC_PREAMBLE_LEN
    );

    if (radioError != RADIOLIB_ERR_NONE) {
        LOG(TAG_FATAL " Radio fail %d\n\r", radioError);
        while (true) delay(1000);
    }

    // Explicitly enable CRC for MeshCore compatibility
    radioError = radio.setCRC(2);  // 2 = CRC-16
    if (radioError != RADIOLIB_ERR_NONE) {
        LOG(TAG_WARN " CRC cfg fail %d\n\r", radioError);
    }

    // Set DIO1 interrupt
    radio.setDio1Action(onDio1Rise);

    // Apply power settings
    applyPowerSettings();

    { uint32_t fM = (uint32_t)(freq * 1000); uint32_t bT = (uint32_t)(bw * 10);
    LOG(TAG_RADIO " %lu.%03lu BW%lu.%lu SF%d CR%d\n\r", fM/1000, fM%1000, bT/10, bT%10, sf, cr); }
}

void startReceive() {
    radio.finishTransmit();
    dio1Flag = false;
    activeReceiveStart = 0;  // Reset active receive state

    // Use duty cycle RX with proper timing
    // rxPeriod should be at least 2x preamble time to reliably detect packets
    uint16_t rxPeriodMs = (preambleTimeMsec > 0) ? (preambleTimeMsec * 2 + 10) : 100;

    radioError = radio.startReceiveDutyCycleAuto(
        MC_PREAMBLE_LEN, rxPeriodMs,
        RADIOLIB_SX126X_IRQ_RX_DEFAULT |
        RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED |
        RADIOLIB_SX126X_IRQ_HEADER_VALID
    );

    if (radioError != RADIOLIB_ERR_NONE) {
        LOG(TAG_ERROR " RX fail %d\n\r", radioError);
        // Try reset
        radio.reset();
        delay(100);
        setupRadio();
    }

    isReceiving = true;
}

bool transmitPacket(MCPacket* pkt) {
    uint8_t buf[MC_RX_BUFFER_SIZE];
    uint16_t len = pkt->serialize(buf, sizeof(buf));

    if (len == 0) {
        LOG(TAG_ERROR " Pkt serial fail\n\r");
        return false;
    }

    // Update repeater statistics
    bool isFlood = pkt->header.isFlood();
    repeaterHelper.recordTx(isFlood);

    #ifdef ENABLE_PACKET_LOG
    packetLogger.log(pkt, true);
    #endif

    radio.finishTransmit();
    dio1Flag = false;

    ledTxOn();
    LOG(TAG_TX " r%d t%d p=%d l=%d\n\r",
        pkt->header.getRouteType(), pkt->header.getPayloadType(),
        pkt->pathLen, pkt->payloadLen);

    radioError = radio.startTransmit(buf, len);
    isReceiving = false;

    if (radioError != RADIOLIB_ERR_NONE) {
        LOG(TAG_ERROR " TX err %d\n\r", radioError);
        ledOff();
        return false;
    }

    // Wait for TX done with timeout
    // Use polling to avoid losing RX packets during sleep
    uint32_t txStart = millis();
    uint32_t txTimeout = maxPacketTimeMsec + 100;  // Max packet time + margin

    while (!dio1Flag && (millis() - txStart < txTimeout)) {
        feedWatchdog();
        delay(1);
    }

    // Check TX done
    uint16_t irq = radio.getIrqStatus();
    if (irq & RADIOLIB_SX126X_IRQ_TX_DONE) {
        txCount++;
        statsRecordTx();  // Persistent stats
        // Track TX airtime
        repeaterHelper.addTxAirTime(calculatePacketAirtime(len));
        LOG(TAG_TX " Complete\n\r");
        ledOff();
        return true;
    }

    LOG(TAG_ERROR " TX timeout\n\r");
    ledOff();
    errCount++;
    return false;
}

//=============================================================================
// Ping / Test Packet
//=============================================================================
static uint16_t pingCounter = 0;

void sendPing() {
    MCPacket pkt;
    pkt.clear();

    // Create a FLOOD packet with PLAIN text payload
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    // Add our node hash as first path entry
    uint8_t myHash = (nodeId >> 24) ^ (nodeId >> 16) ^ (nodeId >> 8) ^ nodeId;
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Create payload: "PING #xxx from CCXXXXXX"
    pingCounter++;
    pkt.payloadLen = snprintf((char*)pkt.payload, MC_MAX_PAYLOAD_SIZE,
                               "PING #%u from %08lX", pingCounter, nodeId);

    LOG(TAG_PING " #%u\n\r", pingCounter);

    // Add to packet cache so we don't re-forward our own ping
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    // Transmit directly (no queue delay for ping)
    if (transmitPacket(&pkt)) {
        LOG(TAG_PING " TX ok\n\r");
    } else {
        LOG(TAG_PING " TX fail\n\r");
    }

    startReceive();
}

void sendDirectedPing(uint8_t targetHash) {
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    uint8_t myHash = nodeIdentity.getNodeHash();
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Payload: [destHash][srcHash]['D']['P'][text: "#N name"]
    pingCounter++;
    pkt.payload[0] = targetHash;
    pkt.payload[1] = myHash;
    pkt.payload[2] = 'D';
    pkt.payload[3] = 'P';
    pkt.payloadLen = 4 + snprintf((char*)&pkt.payload[4], MC_MAX_PAYLOAD_SIZE - 4,
                                   "#%u %s", pingCounter, nodeIdentity.getNodeName());

    LOG(TAG_PING " -> %02X #%u\n\r", targetHash, pingCounter);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_PING " TX ok\n\r");
    } else {
        LOG(TAG_PING " TX fail\n\r");
    }
    startReceive();
}

static void sendPong(uint8_t targetHash, MCPacket* rxPkt) {
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    uint8_t myHash = nodeIdentity.getNodeHash();
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Payload: [destHash][srcHash]['P']['O'][text: "name rssi"]
    pkt.payload[0] = targetHash;
    pkt.payload[1] = myHash;
    pkt.payload[2] = 'P';
    pkt.payload[3] = 'O';
    pkt.payloadLen = 4 + snprintf((char*)&pkt.payload[4], MC_MAX_PAYLOAD_SIZE - 4,
                                   "%s %d", nodeIdentity.getNodeName(), rxPkt->rssi);

    LOG(TAG_PING " PONG -> %02X\n\r", targetHash);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);
    txQueue.add(&pkt);
    txCount++;
}

void sendDirectedTrace(uint8_t targetHash) {
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    uint8_t myHash = nodeIdentity.getNodeHash();
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Payload: [destHash][srcHash]['D']['T'][text: "#N name"]
    pingCounter++;
    pkt.payload[0] = targetHash;
    pkt.payload[1] = myHash;
    pkt.payload[2] = 'D';
    pkt.payload[3] = 'T';
    pkt.payloadLen = 4 + snprintf((char*)&pkt.payload[4], MC_MAX_PAYLOAD_SIZE - 4,
                                   "#%u %s", pingCounter, nodeIdentity.getNodeName());

    LOG(TAG_PING " ~> %02X #%u\n\r", targetHash, pingCounter);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_PING " TX ok\n\r");
    } else {
        LOG(TAG_PING " TX fail\n\r");
    }
    startReceive();
}

static void sendTraceResponse(uint8_t targetHash, MCPacket* rxPkt) {
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    uint8_t myHash = nodeIdentity.getNodeHash();
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Payload: [destHash][srcHash]['T']['R'][text: "name rssi hops"]
    pkt.payload[0] = targetHash;
    pkt.payload[1] = myHash;
    pkt.payload[2] = 'T';
    pkt.payload[3] = 'R';
    pkt.payloadLen = 4 + snprintf((char*)&pkt.payload[4], MC_MAX_PAYLOAD_SIZE - 4,
                                   "%s %d %d", nodeIdentity.getNodeName(), rxPkt->rssi, rxPkt->pathLen);

    LOG(TAG_PING " TR -> %02X\n\r", targetHash);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);
    txQueue.add(&pkt);
    txCount++;
}

//=============================================================================
// ADVERT Beacon
//=============================================================================

void sendAdvert(bool flood) {
    if (!nodeIdentity.isInitialized()) {
        LOG(TAG_ERROR " No ID\n\r");
        return;
    }

    MCPacket pkt;
    bool success;

    if (flood) {
        success = advertGen.buildFlood(&pkt);
    } else {
        success = advertGen.buildZeroHop(&pkt);
    }

    if (!success) {
        LOG(TAG_ERROR " ADV build fail\n\r");
        return;
    }

    LOG(TAG_ADVERT " %s %s\n\r",
        flood ? "flood" : "local",
        nodeIdentity.getNodeName());

    // Add to packet cache so we don't re-forward our own ADVERT
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_ADVERT " TX ok\n\r");
        advertGen.markSent();
        advTxCount++;
    } else {
        LOG(TAG_ADVERT " TX fail\n\r");
    }

    startReceive();
}

//=============================================================================
// Direct Message - Send encrypted message to a contact
//=============================================================================
#ifndef LITE_MODE
/**
 * Send an encrypted direct message to a contact
 * Uses MeshCore TXT_MSG format: [dest_hash][src_hash][MAC+encrypted(timestamp+type+text)]
 */
void sendDirectMessage(const char* recipientName, const char* message) {
    if (!nodeIdentity.isInitialized()) {
        LOG(TAG_ERROR " No ID\n\r");
        return;
    }

    if (!timeSync.isSynchronized()) {
        LOG(TAG_ERROR " No time sync\n\r");
        return;
    }

    // Find contact by name
    Contact* contact = contactMgr.findByName(recipientName);
    if (!contact) {
        LOG(TAG_ERROR " Contact '%s' ?\n\r", recipientName);
        contactMgr.printContacts();
        return;
    }

    LOG(TAG_INFO " Msg to %s\n\r", contact->name);

    // Get or calculate shared secret
    const uint8_t* sharedSecret = contactMgr.getSharedSecret(contact);
    if (!sharedSecret) {
        LOG(TAG_ERROR " ECDH fail\n\r");
        return;
    }

    // Build plaintext: [timestamp 4B][type+attempt 1B][message]
    uint8_t plaintext[MC_MAX_MSG_PLAINTEXT];
    uint16_t plaintextLen = 0;

    // Timestamp (little-endian)
    uint32_t timestamp = timeSync.getTimestamp();
    plaintext[plaintextLen++] = timestamp & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 8) & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 16) & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 24) & 0xFF;

    // Type + attempt byte: upper 6 bits = type (0 = plain), lower 2 bits = attempt (0)
    plaintext[plaintextLen++] = (TXT_TYPE_PLAIN << 2) | 0;

    // Message text (null-terminated)
    uint16_t msgLen = strlen(message);
    if (msgLen > MC_MAX_MSG_PLAINTEXT - plaintextLen - 1) {
        msgLen = MC_MAX_MSG_PLAINTEXT - plaintextLen - 1;
    }
    memcpy(&plaintext[plaintextLen], message, msgLen);
    plaintextLen += msgLen;
    plaintext[plaintextLen++] = '\0';  // Null terminator

    // Encrypt with MAC
    uint8_t encrypted[MC_MAX_MSG_ENCRYPTED];
    uint16_t encryptedLen = msgCrypto.encryptThenMAC(sharedSecret, encrypted,
                                                       plaintext, plaintextLen);
    if (encryptedLen == 0) {
        LOG(TAG_ERROR " Encrypt fail\n\r");
        return;
    }

    // Build packet
    MCPacket pkt;
    pkt.clear();

    // Header: FLOOD + TXT_MSG type
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);
    pkt.pathLen = 0;

    // Payload: [dest_hash 1B][src_hash 1B][MAC+ciphertext]
    uint16_t pos = 0;
    pkt.payload[pos++] = contact->getHash();                    // Destination hash
    pkt.payload[pos++] = nodeIdentity.getPublicKey()[0];        // Source hash

    // Copy MAC + ciphertext
    memcpy(&pkt.payload[pos], encrypted, encryptedLen);
    pos += encryptedLen;

    pkt.payloadLen = pos;

    // Transmit
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_OK " Sent to %s\n\r", contact->name);
        txCount++;
    } else {
        LOG(TAG_ERROR " TX fail\n\r");
    }

    startReceive();
}
#endif // LITE_MODE

//=============================================================================
// Daily Report - Send encrypted status message to admin
//=============================================================================
#ifdef ENABLE_DAILY_REPORT
/**
 * Generate report content string
 * @param buf Output buffer
 * @param maxLen Buffer size
 * @return Length of generated string
 */
uint16_t generateReportContent(char* buf, uint16_t maxLen) {
    uint16_t len = 0;

    // Format: "NodeName: Report\nUp:XXh\nRX:X TX:X FWD:X ERR:X\nBat:XmV"
    int n = snprintf(buf, maxLen,
        "%s: Report\n"
        "Up:%luh\n"
        "RX:%lu TX:%lu FWD:%lu ERR:%lu\n"
        "Bat:%dmV",
        nodeIdentity.getNodeName(),
        millis() / 3600000,  // Hours
        rxCount, txCount, fwdCount, errCount,
        telemetry.getBatteryMv()
    );

    if (n > 0 && n < (int)maxLen) {
        len = n;
    }

    return len;
}

uint16_t generateNodesReport(char* buf, uint16_t maxLen) {
    uint8_t cnt = seenNodes.getCount();
    int n = snprintf(buf, maxLen, "Nodes(%d):", cnt);
    for (uint8_t i = 0; i < cnt && n < (int)maxLen - 1; i++) {
        const SeenNode* nd = seenNodes.getNode(i);
        if (nd) {
            n += snprintf(buf + n, maxLen - n, "\n%s[%d]", nd->name, nd->lastRssi);
        }
    }
    return (n > 0 && n < (int)maxLen) ? n : 0;
}

#endif // ENABLE_DAILY_REPORT

/**
 * Send encrypted text message to a destination public key
 * Used by node alerts (and daily report if enabled)
 */
bool sendEncryptedToAdmin(const uint8_t* destPubKey, const char* text, uint16_t textLen) {
    if (!isPubKeySet(destPubKey) || !timeSync.isSynchronized()) return false;

    // Calculate shared secret
    uint8_t sharedSecret[MC_SHARED_SECRET_SIZE];
    if (!MeshCrypto::calcSharedSecret(sharedSecret, nodeIdentity.getPrivateKey(), destPubKey))
        return false;

    // Build plaintext: [timestamp:4][txt_type|attempt:1][message]
    uint8_t plaintext[104];
    uint32_t timestamp = timeSync.getTimestamp();
    plaintext[0] = timestamp & 0xFF;
    plaintext[1] = (timestamp >> 8) & 0xFF;
    plaintext[2] = (timestamp >> 16) & 0xFF;
    plaintext[3] = (timestamp >> 24) & 0xFF;
    plaintext[4] = (TXT_TYPE_PLAIN << 2) | 0;
    memcpy(&plaintext[5], text, textLen);
    uint16_t plaintextLen = 5 + textLen;

    // Encrypt
    uint8_t encrypted[120];
    uint16_t encLen = meshCrypto.encryptThenMAC(encrypted, plaintext, plaintextLen,
                                                 sharedSecret, sharedSecret);
    memset(sharedSecret, 0, sizeof(sharedSecret));
    memset(plaintext, 0, sizeof(plaintext));
    if (encLen == 0) return false;

    // Build packet
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);
    pkt.pathLen = 0;
    pkt.payload[0] = destPubKey[0];
    pkt.payload[1] = nodeIdentity.getNodeHash();
    memcpy(&pkt.payload[2], encrypted, encLen);
    pkt.payloadLen = 2 + encLen;

    LOG(TAG_INFO " Msg to %02X %dB\n\r", destPubKey[0], pkt.payloadLen);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);
    txQueue.add(&pkt);
    txCount++;
    return true;
}

#ifdef ENABLE_DAILY_REPORT
bool sendReportMessage(const char* text, uint16_t textLen) {
    return sendEncryptedToAdmin(reportDestPubKey, text, textLen);
}

/**
 * Send daily report (stats + nodes list)
 */
bool sendDailyReport() {
    char reportText[96];
    uint16_t textLen = generateReportContent(reportText, sizeof(reportText) - 1);
    if (textLen == 0) return false;

    if (!sendReportMessage(reportText, textLen)) return false;

    // Send nodes report
    uint16_t nodesLen = generateNodesReport(reportText, sizeof(reportText) - 1);
    if (nodesLen > 0) {
        sendReportMessage(reportText, nodesLen);
    }
    return true;
}

/**
 * Check if it's time to send daily report
 * Called from main loop
 */
void checkDailyReport() {
    // Only check if report is enabled and time is synced
    if (!reportEnabled || !timeSync.isSynchronized()) {
        return;
    }

    // Get current time components
    uint32_t now = timeSync.getTimestamp();
    uint32_t dayNumber = now / 86400;  // Days since epoch

    // Already sent today?
    if (dayNumber == lastReportDay) {
        return;
    }

    // Calculate seconds since midnight
    uint32_t secondsToday = now % 86400;
    uint32_t targetSeconds = (uint32_t)reportHour * 3600 + (uint32_t)reportMinute * 60;

    // Check if it's time (within a 60-second window to avoid missing)
    if (secondsToday >= targetSeconds && secondsToday < targetSeconds + 60) {
        LOG(TAG_INFO " Rpt time %02d:%02d\n\r",
            reportHour, reportMinute);

        if (sendDailyReport()) {
            lastReportDay = dayNumber;  // Mark as sent today
            LOG(TAG_OK " Rpt sent\n\r");
        } else {
            LOG(TAG_ERROR " Rpt fail\n\r");
        }
    }
}
#endif // ENABLE_DAILY_REPORT

/**
 * Send node alert when new node/repeater is discovered
 * @param nodeName Name of the discovered node
 * @param nodeHash Hash of the discovered node
 * @param nodeType Type (1=CHAT, 2=REPEATER, etc)
 * @param rssi Signal strength
 * @return true if alert sent
 */
bool sendNodeAlert(const char* nodeName, uint8_t nodeHash, uint8_t nodeType, int16_t rssi) {
    if (!alertEnabled) return false;

    char message[40];
    const char* typeStr = nodeType == 1 ? "CHAT" : nodeType == 2 ? "RPT" : "NODE";
    snprintf(message, sizeof(message), "NEW %s: %s [%02X] %ddBm",
             typeStr, nodeName[0] ? nodeName : "?", nodeHash, rssi);

    return sendEncryptedToAdmin(alertDestPubKey, message, strlen(message));
}

//=============================================================================
// Mesh Health Monitor
//=============================================================================

/**
 * Send alert by temporarily becoming a chat node.
 * MeshCore app ignores messages from repeaters, so we:
 * 1. Switch flags to CHAT_NODE, send ADVERT (app registers us as contact)
 * 2. Send the encrypted message (app shows it)
 * 3. Switch back to REPEATER at next scheduled ADVERT
 */
bool sendAlertAsChatNode(const char* text) {
    if (!isPubKeySet(alertDestPubKey) || !timeSync.isSynchronized()) return false;

    // Save original flags and switch to chat node
    uint8_t origFlags = nodeIdentity.getFlags();
    nodeIdentity.setFlags((origFlags & 0xF0) | MC_TYPE_CHAT_NODE);

    // Send ADVERT as chat node so app registers us
    sendAdvert(true);

    // Send the actual message
    bool ok = sendEncryptedToAdmin(alertDestPubKey, text, strlen(text));

    // Restore repeater flags (next scheduled ADVERT will broadcast as repeater)
    nodeIdentity.setFlags(origFlags);

    LOG(TAG_INFO " Alert%s: %s\n\r", ok ? "" : " FAIL", text);
    return ok;
}

void healthCheck() {
    if (!alertEnabled || !isPubKeySet(alertDestPubKey)) return;

    uint32_t now = millis();
    for (uint8_t i = 0; i < seenNodes.getCount(); i++) {
        const SeenNode* n = seenNodes.getNode(i);
        if (!n || n->lastSeen == 0 || n->pktCount < 3) continue;

        if ((now - n->lastSeen) > HEALTH_OFFLINE_MS && !n->offlineAlerted) {
            ((SeenNode*)n)->offlineAlerted = true;
            char msg[40];
            snprintf(msg, sizeof(msg), "%02X %s off %lum",
                n->hash, n->name[0] ? n->name : "?", (now - n->lastSeen) / 60000);
            sendAlertAsChatNode(msg);
        }
    }
}

void checkAdvertBeacon() {
    // Check for pending ADVERT after time sync
    // Use signed subtraction to handle millis() wraparound correctly
    if (pendingAdvertTime > 0 && (int32_t)(millis() - pendingAdvertTime) >= 0) {
        pendingAdvertTime = 0;  // Clear pending
        LOG(TAG_ADVERT " Sched ADV post-sync\n\r");
        sendAdvert(true);
        lastFloodAdvertTime = millis();
        return;  // Don't send another one immediately
    }

    if (!timeSync.isSynchronized()) return;

    // Separate flood interval mode
    if (floodAdvertIntervalMs > 0) {
        // Local ADVERT on normal interval
        if (advertGen.shouldSend()) {
            sendAdvert(false);  // local only
        }
        // Flood ADVERT on longer interval
        uint32_t now = millis();
        if (lastFloodAdvertTime == 0 || (now - lastFloodAdvertTime) >= floodAdvertIntervalMs) {
            lastFloodAdvertTime = now;
            sendAdvert(true);  // flood
        }
    } else {
        // Default: all ADVERTs are flood
        if (advertGen.shouldSend()) {
            sendAdvert(true);
        }
    }
}

//=============================================================================
// Packet Processing
//=============================================================================

// Generate a simple packet ID from payload hash
uint32_t getPacketId(MCPacket* pkt) {
    uint32_t hash = 5381;
    hash = ((hash << 5) + hash) ^ pkt->header.raw;
    for (uint8_t i = 0; i < pkt->pathLen && i < 8; i++) {
        hash = ((hash << 5) + hash) ^ pkt->path[i];
    }
    for (uint8_t i = 0; i < pkt->payloadLen && i < 16; i++) {
        hash = ((hash << 5) + hash) ^ pkt->payload[i];
    }
    return hash;
}

bool shouldForward(MCPacket* pkt) {
    bool isFlood = pkt->header.isFlood();
    bool isDirect = pkt->header.isDirect();

    if (!isFlood && !isDirect) {
        return false;
    }

    // RSSI threshold: don't forward packets with very weak signal
    if (pkt->rssi < MC_MIN_RSSI_FORWARD) {
        return false;
    }

    // Region filter for flood packets (MeshCore 1.10.0+)
    if (isFlood) {
        if (pkt->hasTransportCodes()) {
            // TRANSPORT_FLOOD: check region map for matching transport code
            if (regionMap.getCount() > 0) {
                if (!regionMap.findMatch(pkt, REGION_DENY_FLOOD)) {
                    return false;
                }
            }
        } else {
            // Legacy FLOOD: check wildcard entry
            if (regionMap.getWildcard().flags & REGION_DENY_FLOOD) {
                return false;
            }
        }
    }

    // DIRECT routing: check if we are the next hop (path[0] == our hash)
    if (isDirect) {
        if (pkt->pathLen == 0) return false;
        if (pkt->path[0] != nodeIdentity.getNodeHash()) return false;
    }

    // Don't forward packets specifically addressed to us
    uint8_t payloadType = pkt->header.getPayloadType();
    if (payloadType == MC_PAYLOAD_ANON_REQ ||
        payloadType == MC_PAYLOAD_REQUEST ||
        payloadType == MC_PAYLOAD_RESPONSE) {
        if (pkt->payloadLen > 0 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            return false;
        }
    }

    // Check packet ID cache (dedup)
    uint32_t id = getPacketId(pkt);
    if (!packetCache.addIfNew(id)) {
        return false;
    }

    // FLOOD: check path length and loop detection
    if (isFlood) {
        if (pkt->pathLen >= MC_MAX_PATH_SIZE - 1) {
            return false;
        }

        // Loop detection with configurable strictness
        if (loopDetectMode != LOOP_DETECT_OFF) {
            uint8_t myHash = nodeIdentity.getNodeHash();
            uint8_t occurrences = 0;

            // Count how many times we appear in the path
            for (uint8_t i = 0; i < pkt->pathLen; i++) {
                if (pkt->path[i] == myHash) {
                    occurrences++;
                }
            }

            // Check against threshold based on mode
            uint8_t maxOccurrences;
            switch (loopDetectMode) {
                case LOOP_DETECT_MINIMAL:
                    maxOccurrences = 4;  // Allow up to 4 occurrences
                    break;
                case LOOP_DETECT_MODERATE:
                    maxOccurrences = 2;  // Allow up to 2 occurrences
                    break;
                case LOOP_DETECT_STRICT:
                default:
                    maxOccurrences = 1;  // Allow only 1 occurrence (original behavior)
                    break;
            }

            if (occurrences >= maxOccurrences) {
                return false;  // Loop detected
            }
        }
    }

    return true;
}

//=============================================================================
// Node Discovery (CONTROL packets)
//=============================================================================

/**
 * Process CONTROL packet (node discovery)
 *
 * CONTROL packet format (DISCOVER_REQ):
 * [0]    flags - upper nibble: 0x8 (discover), lower bit: prefix_only
 * [1]    type_filter - bit for each ADV_TYPE_* (bit 1 = repeater)
 * [2-5]  tag - random sender value (4 bytes, little-endian)
 * [6-9]  since - optional epoch timestamp (4 bytes, little-endian)
 *
 * @param pkt Received CONTROL packet
 * @return true if discovery response sent
 */
bool processDiscoverRequest(MCPacket* pkt) {
    if (pkt->payloadLen < 6) {
        return false;  // Too short
    }

    uint8_t flags = pkt->payload[0];
    uint8_t typeFilter = pkt->payload[1];

    // Check if this is a discover request (upper nibble = 0x8)
    if ((flags & 0xF0) != CTL_TYPE_DISCOVER_REQ) {
        return false;  // Not a discover request
    }

    // Check if repeater type is requested (bit 1 = repeater)
    // ADV_TYPE: 0=unknown, 1=client, 2=repeater, 3=room
    if (!(typeFilter & (1 << MC_TYPE_REPEATER))) {
        return false;  // Repeater not requested
    }

    // Extract request tag (for correlation)
    uint32_t requestTag = pkt->payload[2] |
                          (pkt->payload[3] << 8) |
                          (pkt->payload[4] << 16) |
                          (pkt->payload[5] << 24);

    // Check rate limiting
    if (!repeaterHelper.canRespondToDiscover()) {
        LOG(TAG_DISCOVERY " Rate limited\n\r");
        return false;
    }

    LOG(TAG_DISCOVERY " REQ %02X\n\r", typeFilter);

    // Build discover response
    MCPacket respPkt;
    respPkt.clear();

    // Use same route type for response (usually FLOOD)
    respPkt.header.set(pkt->header.getRouteType(), MC_PAYLOAD_CONTROL, MC_PAYLOAD_VER_1);

    // Copy incoming path for response (if any)
    respPkt.pathLen = pkt->pathLen;
    if (pkt->pathLen > 0) {
        memcpy(respPkt.path, pkt->path, pkt->pathLen);
    }

    // Build response payload
    // [0]    flags - upper nibble: 0x8 (response), lower: 0x1
    // [1]    node type (MC_TYPE_REPEATER)
    // [2]    inbound SNR
    // [3-6]  request tag (correlation)
    // [7-14] public key prefix (8 bytes)
    uint16_t pos = 0;
    respPkt.payload[pos++] = CTL_TYPE_DISCOVER_RESP;     // Response flag
    respPkt.payload[pos++] = MC_TYPE_REPEATER;           // We are a repeater
    respPkt.payload[pos++] = (uint8_t)pkt->snr;          // Inbound SNR

    // Request tag for correlation
    respPkt.payload[pos++] = requestTag & 0xFF;
    respPkt.payload[pos++] = (requestTag >> 8) & 0xFF;
    respPkt.payload[pos++] = (requestTag >> 16) & 0xFF;
    respPkt.payload[pos++] = (requestTag >> 24) & 0xFF;

    // Public key prefix (first 8 bytes)
    memcpy(&respPkt.payload[pos], nodeIdentity.getPublicKey(), 8);
    pos += 8;

    respPkt.payloadLen = pos;

    // Add random delay to spread responses from multiple repeaters
    uint32_t airtime = calculatePacketAirtime(respPkt.payloadLen + respPkt.pathLen + 2);
    uint8_t score = calcSnrScore(pkt->snr);
    uint32_t rxDel = calcRxDelay(score, airtime);
    uint32_t randomDelay = rxDel + calcTxJitter(airtime);

    LOG(TAG_DISCOVERY " RESP %lums\n\r", randomDelay);

    // Queue with delay
    delay(randomDelay);
    txQueue.add(&respPkt);

    return true;
}

//=============================================================================
// TXT_MSG CLI Command handling (MC_PAYLOAD_PLAIN with TXT_TYPE_CLI)
//=============================================================================

/**
 * Process encrypted TXT_MSG containing CLI command
 *
 * TXT_MSG packet format (MC_PAYLOAD_PLAIN):
 * [0]     dest_hash (1 byte)
 * [1]     src_hash (1 byte)
 * [2-3]   MAC (2 bytes)
 * [4+]    ciphertext (variable)
 *
 * Decrypted plaintext:
 * [0-3]   timestamp (4 bytes)
 * [4]     txt_type | attempt (upper 6 bits = type, lower 2 = attempt)
 * [5+]    text/command (null-terminated string)
 *
 * @param pkt Received TXT_MSG packet
 * @return true if command processed and response sent
 */
bool processTxtMsgCLI(MCPacket* pkt) {
    if (pkt->payloadLen < 10) {
        LOG(TAG_AUTH " TXT short %d\n\r", pkt->payloadLen);
        return false;
    }

    uint8_t destHash = pkt->payload[0];
    uint8_t srcHash = pkt->payload[1];

    // Check if addressed to us
    if (destHash != nodeIdentity.getNodeHash()) {
        return false;  // Not for us
    }

    LOG(TAG_AUTH " TXT %02X\n\r", srcHash);

    // Find client session by src_hash
    ClientSession* session = nullptr;
    for (uint8_t i = 0; i < MAX_CLIENT_SESSIONS; i++) {
        const ClientSession* s = sessionManager.getSession(i);
        if (s && s->active && s->pubKey[0] == srcHash) {
            session = const_cast<ClientSession*>(s);
            break;
        }
    }

    if (!session) {
        LOG(TAG_AUTH " No session for %02X\n\r", srcHash);
        return false;
    }

    // Decrypt using session's shared secret
    const uint8_t* encrypted = &pkt->payload[2];
    uint16_t encryptedLen = pkt->payloadLen - 2;

    uint8_t decrypted[128];
    uint16_t decryptedLen = meshCrypto.MACThenDecrypt(
        decrypted, encrypted, encryptedLen,
        session->sharedSecret, session->sharedSecret);

    if (decryptedLen == 0) {
        LOG(TAG_AUTH " TXT decrypt fail\n\r");
        return false;
    }

    // Extract timestamp and check replay
    uint32_t timestamp = decrypted[0] |
                         (decrypted[1] << 8) |
                         (decrypted[2] << 16) |
                         (decrypted[3] << 24);

    if (timestamp <= session->lastTimestamp) {
        LOG(TAG_AUTH " TXT replay\n\r");
        return false;
    }
    session->lastTimestamp = timestamp;
    session->lastActivity = millis();

    // Extract txt_type from byte[4]
    uint8_t txtTypeByte = decrypted[4];
    uint8_t txtType = (txtTypeByte >> 2) & 0x3F;  // Upper 6 bits

    LOG(TAG_AUTH " TXT t=%d l=%d\n\r", txtType, decryptedLen);

    // Only process CLI commands (TXT_TYPE_CLI = 0x01)
    if (txtType != TXT_TYPE_CLI) {
        LOG(TAG_AUTH " Not CLI\n\r");
        return false;
    }

    // Only admin can execute CLI commands
    if (session->permissions != PERM_ACL_ADMIN) {
        LOG(TAG_AUTH " Need admin\n\r");
        return false;
    }

    // Extract command string (starts at byte 5)
    char cmdStr[40];
    uint16_t cmdLen = decryptedLen - 5;
    if (cmdLen > sizeof(cmdStr) - 1) cmdLen = sizeof(cmdStr) - 1;
    memcpy(cmdStr, &decrypted[5], cmdLen);
    cmdStr[cmdLen] = '\0';

    // Trim trailing whitespace/newlines
    while (cmdLen > 0 && (cmdStr[cmdLen-1] == '\n' || cmdStr[cmdLen-1] == '\r' ||
           cmdStr[cmdLen-1] == ' ' || cmdStr[cmdLen-1] == '\0')) {
        cmdStr[--cmdLen] = '\0';
    }

    LOG(TAG_AUTH " CLI: %s\n\r", cmdStr);

    // Process command
    char cliResponse[96];
    uint16_t cliLen = processRemoteCommand(cmdStr, cliResponse, sizeof(cliResponse), true);

    // Build response: [timestamp:4][txt_type:1][response_text]
    uint8_t responseData[128];
    uint16_t responseLen = 0;

    // Echo back the request timestamp (for correlation)
    responseData[responseLen++] = timestamp & 0xFF;
    responseData[responseLen++] = (timestamp >> 8) & 0xFF;
    responseData[responseLen++] = (timestamp >> 16) & 0xFF;
    responseData[responseLen++] = (timestamp >> 24) & 0xFF;

    // Response type: TXT_TYPE_CLI (0x01) in upper 6 bits
    responseData[responseLen++] = (TXT_TYPE_CLI << 2) | 0;

    // Append CLI response text
    if (cliLen > 0 && responseLen + cliLen < sizeof(responseData)) {
        memcpy(&responseData[responseLen], cliResponse, cliLen);
        responseLen += cliLen;
    }

    // Encrypt response
    uint8_t encryptedResponse[160];
    uint16_t encLen = meshCrypto.encryptResponse(
        encryptedResponse, responseData, responseLen, session->sharedSecret);

    if (encLen == 0) {
        return false;
    }

    // Build response packet (TXT_MSG / MC_PAYLOAD_PLAIN)
    MCPacket respPkt;
    respPkt.clear();
    respPkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);
    respPkt.pathLen = 0;

    // Payload: [dest_hash:1][src_hash:1][encrypted]
    respPkt.payload[0] = srcHash;                        // Destination (client)
    respPkt.payload[1] = nodeIdentity.getNodeHash();     // Source (us)
    memcpy(&respPkt.payload[2], encryptedResponse, encLen);
    respPkt.payloadLen = 2 + encLen;

    // Queue for transmission
    txQueue.add(&respPkt);

    // Handle reboot command
    if (strcmp(cmdStr, "reboot") == 0) {
        pendingReboot = true;
        rebootTime = millis() + 500;
    }

    LOG(TAG_AUTH " Resp %dB\n\r", respPkt.payloadLen);
    return true;
}

//=============================================================================
// Authenticated REQUEST handling
//=============================================================================

/**
 * Process authenticated REQUEST packet
 *
 * REQUEST packet format:
 * [0]     dest_hash (1 byte)
 * [1]     src_hash (1 byte)
 * [2-3]   MAC (2 bytes)
 * [4+]    ciphertext (variable)
 *
 * Decrypted plaintext:
 * [0-3]   timestamp (4 bytes)
 * [4]     request_type
 * [5+]    request_data (type-specific)
 *
 * @param pkt Received REQUEST packet
 * @return true if request processed and response sent
 */
bool processAuthenticatedRequest(MCPacket* pkt) {
    if (pkt->payloadLen < 20) {
        return false;
    }

    uint8_t destHash = pkt->payload[0];
    uint8_t srcHash = pkt->payload[1];

    // Check if addressed to us
    if (destHash != nodeIdentity.getNodeHash()) {
        return false;  // Not for us
    }

    // Find client session by src_hash prefix
    ClientSession* session = nullptr;
    for (uint8_t i = 0; i < MAX_CLIENT_SESSIONS; i++) {
        const ClientSession* s = sessionManager.getSession(i);
        if (s && s->active && s->pubKey[0] == srcHash) {
            session = const_cast<ClientSession*>(s);
            break;
        }
    }

    if (!session) {
        return false;
    }

    // Decrypt request using session's shared secret
    // Encrypted part starts at offset 2: [MAC:2][ciphertext]
    const uint8_t* encrypted = &pkt->payload[2];
    uint16_t encryptedLen = pkt->payloadLen - 2;

    uint8_t decrypted[128];
    uint16_t decryptedLen = meshCrypto.MACThenDecrypt(
        decrypted, encrypted, encryptedLen,
        session->sharedSecret, session->sharedSecret);

    if (decryptedLen == 0) {
        return false;
    }

    // Extract timestamp and check replay
    uint32_t timestamp = decrypted[0] |
                         (decrypted[1] << 8) |
                         (decrypted[2] << 16) |
                         (decrypted[3] << 24);

    if (timestamp <= session->lastTimestamp) {
        return false;  // Replay attack
    }
    session->lastTimestamp = timestamp;
    session->lastActivity = millis();

    // Extract request type
    uint8_t reqType = decrypted[4];

    // Handle request based on type
    uint8_t responseData[128];
    uint16_t responseLen = 0;

    // All responses start with sender's timestamp (used as tag for matching)
    // MeshCore reflects the request timestamp back, not server time
    responseData[0] = timestamp & 0xFF;
    responseData[1] = (timestamp >> 8) & 0xFF;
    responseData[2] = (timestamp >> 16) & 0xFF;
    responseData[3] = (timestamp >> 24) & 0xFF;
    responseLen = 4;

    switch (reqType) {
        case REQ_TYPE_GET_STATUS:
            responseLen += repeaterHelper.serializeRepeaterStats(
                &responseData[responseLen],
                telemetry.getBatteryMv(),
                txQueue.getCount(),
                lastRssi, lastSnr);
            break;

        case REQ_TYPE_GET_TELEMETRY:
            {
                telemetry.update();
                CayenneLPP lpp(&responseData[responseLen], sizeof(responseData) - responseLen);
                // Battery voltage (channel 1) - integer mV
                lpp.addVoltageMv(1, telemetry.getBatteryMv());
                // Temperature (channel 2) - integer Celsius
                lpp.addTemperatureInt(2, telemetry.getTemperature());
                // Node count as analog input (channel 3) - integer
                lpp.addAnalogInputInt(3, (int16_t)seenNodes.getCount());
                // Uptime as analog input in hours (channel 4) - integer
                lpp.addAnalogInputInt(4, (int16_t)(telemetry.getUptime() / 3600));
                responseLen += lpp.getSize();
            }
            break;

        case REQ_TYPE_GET_NEIGHBOURS:
            {
                NeighbourTracker& neighbours = repeaterHelper.getNeighbours();
                uint8_t count = neighbours.getCount();

                // neighbours_count (2 bytes)
                responseData[responseLen++] = count & 0xFF;
                responseData[responseLen++] = (count >> 8) & 0xFF;

                // results_count (2 bytes) - same as count for now
                responseData[responseLen++] = count & 0xFF;
                responseData[responseLen++] = (count >> 8) & 0xFF;

                // Serialize neighbours (6-byte prefix + 1 SNR + 1 RSSI per entry)
                responseLen += neighbours.serialize(
                    &responseData[responseLen],
                    sizeof(responseData) - responseLen,
                    0, 6);  // offset=0, prefix=6 bytes
            }
            break;

        case REQ_TYPE_GET_MINMAXAVG:
            responseLen += repeaterHelper.serializeRadioStats(&responseData[responseLen]);
            break;

        case REQ_TYPE_GET_ACCESS_LIST:
            if (session->permissions != PERM_ACL_ADMIN) {
                return false;
            }
            {
                ACLManager& acl = repeaterHelper.getACL();
                uint8_t count = acl.getCount();
                responseData[responseLen++] = count;
            }
            break;

        case REQ_TYPE_KEEP_ALIVE:
            break;

        case REQ_TYPE_SEND_CLI:
            if (session->permissions != PERM_ACL_ADMIN) {
                return false;
            }
            {
                char cmdStr[40];
                uint16_t cmdLen = decryptedLen - 5;
                if (cmdLen > sizeof(cmdStr) - 1) cmdLen = sizeof(cmdStr) - 1;
                memcpy(cmdStr, &decrypted[5], cmdLen);
                cmdStr[cmdLen] = '\0';

                while (cmdLen > 0 && (cmdStr[cmdLen-1] == '\n' || cmdStr[cmdLen-1] == '\r' ||
                       cmdStr[cmdLen-1] == ' ' || cmdStr[cmdLen-1] == '\0')) {
                    cmdStr[--cmdLen] = '\0';
                }

                // Process command and get response
                char cliResponse[96];  // Reduced from 128 to save stack
                bool isAdmin = (session->permissions == PERM_ACL_ADMIN);
                uint16_t cliLen = processRemoteCommand(cmdStr, cliResponse, sizeof(cliResponse), isAdmin);

                // Append CLI response to responseData (after timestamp)
                if (cliLen > 0 && responseLen + cliLen < sizeof(responseData)) {
                    memcpy(&responseData[responseLen], cliResponse, cliLen);
                    responseLen += cliLen;
                }

                // Handle reboot command specially
                if (strcmp(cmdStr, "reboot") == 0) {
                    // Schedule reboot after sending response
                    pendingReboot = true;
                    rebootTime = millis() + 500;  // Reboot in 500ms
                }
            }
            break;

        default:
            return false;
    }

    // Encrypt response
    uint8_t encryptedResponse[160];
    uint16_t encLen = meshCrypto.encryptResponse(
        encryptedResponse, responseData, responseLen, session->sharedSecret);

    if (encLen == 0) {
        return false;
    }

    // Build response packet
    MCPacket respPkt;
    respPkt.clear();

    // Use FLOOD route for response (client may not have direct path)
    respPkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_RESPONSE, MC_PAYLOAD_VER_1);
    respPkt.pathLen = 0;

    // Payload: [dest_hash:1][src_hash:1][encrypted]
    respPkt.payload[0] = srcHash;                        // Destination (client)
    respPkt.payload[1] = nodeIdentity.getNodeHash();     // Source (us)
    memcpy(&respPkt.payload[2], encryptedResponse, encLen);
    respPkt.payloadLen = 2 + encLen;

    // Queue for transmission
    txQueue.add(&respPkt);

    return true;
}

//=============================================================================
// Authentication - ANON_REQ / LOGIN handling
//=============================================================================

/**
 * Send encrypted LOGIN response to client
 *
 * @param clientPubKey Client's public key (32 bytes)
 * @param sharedSecret Pre-computed shared secret with client
 * @param isAdmin true if admin login successful
 * @param permissions Permission byte
 * @param outPath Return path to client
 * @param outPathLen Return path length
 * @return true if response sent successfully
 */
bool sendLoginResponse(const uint8_t* clientPubKey, const uint8_t* sharedSecret,
                       bool isAdmin, uint8_t permissions,
                       const uint8_t* outPath, uint8_t outPathLen) {
    // Build response data
    uint8_t responseData[16];
    // Use unique timestamp (current + 1 to ensure it's always newer)
    uint32_t responseTs = timeSync.getTimestamp() + 1;
    uint8_t responseLen = MeshCrypto::buildLoginOKResponse(
        responseData,
        responseTs,
        isAdmin,
        permissions,
        60,  // Keep-alive interval (ignored)
        2    // Firmware version: 2.x
    );

    // Encrypt response
    uint8_t encryptedResponse[64];
    uint16_t encLen = meshCrypto.encryptResponse(
        encryptedResponse, responseData, responseLen, sharedSecret);

    if (encLen == 0) {
        return false;
    }

    // Build response packet
    MCPacket respPkt;
    respPkt.clear();

    // Use FLOOD route for response (client may not have direct path)
    // MeshCore always uses sendFlood for login responses
    respPkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_RESPONSE, MC_PAYLOAD_VER_1);
    respPkt.pathLen = 0;  // Flood has no path

    // Payload: [dest_hash:1][src_hash:1][MAC:2][ciphertext]
    respPkt.payload[0] = clientPubKey[0];  // Destination hash
    respPkt.payload[1] = nodeIdentity.getNodeHash();  // Source hash (our hash)
    memcpy(&respPkt.payload[2], encryptedResponse, encLen);
    respPkt.payloadLen = 2 + encLen;

    // Queue for transmission
    txQueue.add(&respPkt);

    return true;
}

/**
 * Process ANON_REQ (anonymous login request)
 *
 * ANON_REQ payload format (MeshCore V1):
 * [0]      dest_hash (1 byte) - first byte of destination pubkey (already verified)
 * [1-32]   ephemeral_pubkey (32 bytes) - sender's temp key for ECDH
 * [33-34]  MAC (2 bytes) - HMAC-SHA256 truncated
 * [35+]    ciphertext (variable) - encrypted [timestamp:4][password:N]
 *
 * @param pkt Received ANON_REQ packet
 * @return true if login successful
 */
bool processAnonRequest(MCPacket* pkt) {
    // Minimum size: 1 (dest_hash) + 32 (ephemeral) + 2 (MAC) + 16 (min ciphertext)
    if (pkt->payloadLen < 51) {
        LOG(TAG_AUTH " ANON short %d\n\r", pkt->payloadLen);
        return false;
    }

    // Extract components (dest_hash at [0] already verified by caller)
    const uint8_t* ephemeralPub = &pkt->payload[1];     // Ephemeral pubkey at [1-32]

    // Decrypt the request - pass from sender pubkey onwards
    // ANON_REQ format: [destHash:1][sender_pubkey:32][MAC:2][ciphertext]
    // decryptAnonReq expects: [sender_pubkey:32][MAC:2][ciphertext]
    // Note: byte[1] (7E) is first byte of sender pubkey, NOT srcHash!
    uint32_t timestamp;
    char password[32];

    // Skip only destHash[0], sender pubkey starts at [1]
    uint8_t pwdLen = meshCrypto.decryptAnonReq(
        &timestamp, password, sizeof(password) - 1,
        &pkt->payload[1],     // From sender pubkey onwards (skip only destHash)
        pkt->payloadLen - 1,  // Length excluding destHash
        nodeIdentity.getPrivateKey()
    );

    if (pwdLen == 0) {
        return false;
    }

    // Process login through session manager
    uint8_t permissions = sessionManager.processLogin(
        ephemeralPub,  // Use ephemeral key as client identifier
        password,
        timestamp,
        nodeIdentity.getPrivateKey(),
        pkt->path,
        pkt->pathLen
    );

    // Clear password from memory
    memset(password, 0, sizeof(password));

    if (permissions == 0) {
        statsRecordLoginFail();  // Persistent stats
        LOG(TAG_AUTH " Login FAILED\n\r");
        return false;
    }

    statsRecordLogin();  // Persistent stats
    bool isAdmin = (permissions == PERM_ACL_ADMIN);
    LOG(TAG_AUTH " Login OK (%s)\n\r", isAdmin ? "admin" : "guest");

    // Capture admin public key for daily report
    if (isAdmin) {
        // Check if this is a new admin key (different from current)
        bool isNewKey = (memcmp(reportDestPubKey, ephemeralPub, REPORT_PUBKEY_SIZE) != 0);
        if (isNewKey || !isPubKeySet(reportDestPubKey)) {
            memcpy(reportDestPubKey, ephemeralPub, REPORT_PUBKEY_SIZE);
            saveConfig();
        }
    }

    // Get shared secret from session for response encryption
    ClientSession* session = sessionManager.findSession(ephemeralPub);
    if (!session) {
        return false;
    }

    // Send encrypted response
    return sendLoginResponse(
        ephemeralPub,
        session->sharedSecret,
        isAdmin,
        permissions,
        pkt->path,
        pkt->pathLen
    );
}

void processReceivedPacket(MCPacket* pkt) {
    rxCount++;
    statsRecordRx();  // Persistent stats

    // Update repeater statistics
    bool isFlood = pkt->header.isFlood();
    repeaterHelper.recordRx(isFlood);
    repeaterHelper.updateRadioStats(pkt->rssi, pkt->snr);

    #ifdef ENABLE_PACKET_LOG
    packetLogger.log(pkt, false);
    #endif

    LOG(TAG_RX " r%d t%d p=%d l=%d %d/%d.%d\n\r",
        pkt->header.getRouteType(), pkt->header.getPayloadType(),
        pkt->pathLen, pkt->payloadLen,
        pkt->rssi, pkt->snr / 4, abs(pkt->snr % 4) * 25);

    // Handle ANON_REQ (login request)
    if (pkt->header.getPayloadType() == MC_PAYLOAD_ANON_REQ) {
        if (pkt->payloadLen >= 51 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Rate limit login attempts
            if (!repeaterHelper.allowLogin()) {
                statsRecordRateLimited();  // Persistent stats
                LOG(TAG_AUTH " Login lim\n\r");
            } else {
                processAnonRequest(pkt);
            }
        }
    }
    // Handle REQUEST (authenticated request)
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_REQUEST) {
        if (pkt->payloadLen >= 20 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Rate limit requests
            if (!repeaterHelper.allowRequest()) {
                statsRecordRateLimited();  // Persistent stats
                LOG(TAG_AUTH " Req lim\n\r");
            } else {
                processAuthenticatedRequest(pkt);
            }
        }
    }
    // Handle MC_PAYLOAD_PLAIN: directed ping/pong or TXT_MSG CLI
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_PLAIN) {
        if (pkt->payloadLen >= 4 && pkt->payload[2] == 'D' && pkt->payload[3] == 'P'
            && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Directed PING for us - respond with PONG
            LOG(TAG_PING " from %02X %s\n\r", pkt->payload[1],
                pkt->payloadLen > 4 ? (char*)&pkt->payload[4] : "");
            sendPong(pkt->payload[1], pkt);
        }
        else if (pkt->payloadLen >= 4 && pkt->payload[2] == 'P' && pkt->payload[3] == 'O'
                 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // PONG response for us
            LOG(TAG_PING " PONG %02X %s rssi=%d snr=%d.%ddB p=%d\n\r",
                pkt->payload[1],
                pkt->payloadLen > 4 ? (char*)&pkt->payload[4] : "",
                pkt->rssi, pkt->snr / 4, abs(pkt->snr % 4) * 25,
                pkt->pathLen);
        }
        else if (pkt->payloadLen >= 4 && pkt->payload[2] == 'D' && pkt->payload[3] == 'T'
                 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Directed TRACE for us - respond with trace response
            LOG(TAG_PING " TRACE from %02X %s\n\r", pkt->payload[1],
                pkt->payloadLen > 4 ? (char*)&pkt->payload[4] : "");
            sendTraceResponse(pkt->payload[1], pkt);
        }
        else if (pkt->payloadLen >= 4 && pkt->payload[2] == 'T' && pkt->payload[3] == 'R'
                 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Trace response for us
            LOG(TAG_PING " TRACE %02X %s rssi=%d snr=%d.%ddB p=%d\n\r",
                pkt->payload[1],
                pkt->payloadLen > 4 ? (char*)&pkt->payload[4] : "",
                pkt->rssi, pkt->snr / 4, abs(pkt->snr % 4) * 25,
                pkt->pathLen);
        }
        else if (pkt->payloadLen >= 10 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // TXT_MSG CLI
            if (!repeaterHelper.allowRequest()) {
                statsRecordRateLimited();
                LOG(TAG_AUTH " TXT lim\n\r");
            } else {
                processTxtMsgCLI(pkt);
            }
        }
    }
    // Handle CONTROL (node discovery, etc.)
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_CONTROL) {
        if (pkt->payloadLen >= 6) {
            processDiscoverRequest(pkt);
        }
    }
    // Handle TRACE (ping) - add our SNR and forward
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_PATH_TRACE) {
        // Add our SNR to the path (SNR * 4 as signed byte)
        if (pkt->pathLen < MC_MAX_PATH_SIZE) {
            //crossreferenced with https://github.com/meshcore-dev/MeshCore/blob/a3a1aa5e3be34b42d8ac8c2cc244d30af6cdd71e/src/Mesh.cpp#L42
            uint8_t flags = pkt->payload[8];
            uint8_t path_hash_size = flags & 0x03;  // NEW v1.11+: lower 2 bits is path hash size
            uint16_t offset = (uint16_t)pkt->pathLen << path_hash_size;
            bool forward_trace = memcmp(&pkt->payload[9 + offset], nodeIdentity.getPublicKey(), 1 << path_hash_size) == 0;
            LOG(TAG_PING " f%d hsz%d o%d id%02x\n\r",forward_trace,path_hash_size,offset,pkt->payload[9 + offset]);
            if (forward_trace) { //only forward traces we're part of
                LOG(TAG_PING " fwd\r\n");
                pkt->path[pkt->pathLen++] = (int8_t)(pkt->snr);
                // Forward the trace packet
                txQueue.add(pkt);
            }
        }
    }
    // Parse and display ADVERT info
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_ADVERT) {
        advRxCount++;
        // Try to sync time from ADVERT timestamp
        // First ADVERT: sync immediately. Already synced: need 2 matching different times to re-sync
        uint32_t advertTime = AdvertGenerator::extractTimestamp(pkt->payload, pkt->payloadLen);

        #ifdef DEBUG_VERBOSE
        // Debug: show raw timestamp bytes from received ADVERT
        Serial.printf("[RX-ADV] Raw ts bytes[32-35]: %02X %02X %02X %02X -> unix=%lu\n\r",
                      pkt->payload[32], pkt->payload[33], pkt->payload[34], pkt->payload[35], advertTime);

        // Debug: show appdata (starts at byte 100)
        Serial.printf("[RX-ADV] Appdata[100+]: ");
        for (int i = 100; i < pkt->payloadLen && i < 116; i++) {
            Serial.printf("%02X ", pkt->payload[i]);
        }
        Serial.printf(" (len=%d)\n\r", pkt->payloadLen - 100);
        #endif

        if (advertTime > 0) {
            uint8_t syncResult = timeSync.syncFromAdvert(advertTime);

            if (syncResult == 1) {
                // First sync - use immediately
                ledBlueDoubleBlink();  // Signal time sync acquired
                LOG(TAG_OK " Time sync %lu\n\r", timeSync.getTimestamp());
                statsSetFirstBootTime(timeSync.getTimestamp());  // Persistent stats
                // Schedule our own ADVERT after sync
                pendingAdvertTime = millis() + ADVERT_AFTER_SYNC_MS;
                LOG(TAG_INFO " ADV in %ds\n\r", ADVERT_AFTER_SYNC_MS / 1000);
            } else if (syncResult == 2) {
                // Re-sync via consensus (2 different sources agreed)
                ledBlueDoubleBlink();  // Signal time re-sync
                LOG(TAG_OK " Time resync %lu\n\r", timeSync.getTimestamp());
                // Schedule new ADVERT with updated time
                pendingAdvertTime = millis() + ADVERT_AFTER_SYNC_MS;
                LOG(TAG_INFO " ADV in %ds\n\r", ADVERT_AFTER_SYNC_MS / 1000);
            } else if (timeSync.hasPendingSync()) {
                // Received different time, stored as pending - waiting for confirmation
                LOG(TAG_INFO " Time drift %lu pending\n\r", advertTime);
            }
        }

        AdvertInfo advInfo;
        if (AdvertGenerator::parseAdvert(pkt->payload, pkt->payloadLen, &advInfo)) {
            // Show node info
            LOG(TAG_NODE " %s", advInfo.name);
            if (advInfo.isRepeater) LOG_RAW(" R");
            if (advInfo.isChatNode) LOG_RAW(" C");
            LOG_RAW(" %02X", advInfo.pubKeyHash);
            if (advInfo.hasLocation) {
                int32_t lat = advInfo.latitude, lon = advInfo.longitude;
                LOG_RAW(" %ld.%04ld,%ld.%04ld", lat/1000000, abs(lat%1000000)/100, lon/1000000, abs(lon%1000000)/100);
            }
            LOG_RAW("\n\r");

            // Update seen nodes with pubkey hash and name from ADVERT
            bool isNew = seenNodes.update(advInfo.pubKeyHash, pkt->rssi, pkt->snr, advInfo.name);
            if (isNew) {
                statsRecordUniqueNode();  // Persistent stats
                LOG(TAG_NODE " New node\n\r");
                // Send alert for new node
                uint8_t nodeType = advInfo.isChatNode ? 1 : advInfo.isRepeater ? 2 : 0;
                sendNodeAlert(advInfo.name, advInfo.pubKeyHash, nodeType, pkt->rssi);
            }

            // Add to contact manager (stores full public key for messaging)
            // Filter by hop count if autoAddMaxHops is set (0 = no limit)
            const uint8_t* pubKey = &pkt->payload[ADVERT_PUBKEY_OFFSET];
            if (autoAddMaxHops == 0 || pkt->pathLen <= autoAddMaxHops) {
                contactMgr.updateFromAdvert(pubKey, advInfo.name, pkt->rssi, pkt->snr);
            } else {
                LOG(TAG_INFO " Skip contact (hops=%d > max=%d)\n\r", pkt->pathLen, autoAddMaxHops);
            }

            // Store-and-forward: deliver pending messages for this node
            if (mailbox.countFor(advInfo.pubKeyHash) > 0) {
                MCPacket fwdPkt;
                while (mailbox.popFor(advInfo.pubKeyHash, &fwdPkt)) {
                    txQueue.add(&fwdPkt);
                    LOG(TAG_INFO " Mbox fwd %02X\n\r", advInfo.pubKeyHash);
                }
            }

            // If this is a repeater AND received directly (0-hop), add to neighbours list
            // Only 0-hop ADVERTs indicate direct neighbours (no relay)
            if (advInfo.isRepeater && pkt->pathLen == 0 && pkt->payloadLen >= 32) {
                bool newNeighbour = repeaterHelper.getNeighbours().update(
                    pkt->payload,  // First 32 bytes are pubkey
                    pkt->snr, pkt->rssi);
                if (newNeighbour) {
                    LOG(TAG_NODE " Nbr: %s\n\r", advInfo.name);
                }
            }
        }
    }
    // Track nodes - either from path or from payload hash for direct packets
    else if (pkt->pathLen > 0) {
        // First byte is originator
        bool isNew = seenNodes.update(pkt->path[0], pkt->rssi, pkt->snr);
        if (isNew) {
            statsRecordUniqueNode();  // Persistent stats
            LOG(TAG_NODE " New %02X\n\r", pkt->path[0]);
        }
        // If path has multiple hops, also track the last hop (direct neighbor)
        if (pkt->pathLen > 1) {
            uint8_t lastHop = pkt->path[pkt->pathLen - 1];
            if (lastHop != pkt->path[0]) {
                seenNodes.update(lastHop, pkt->rssi, pkt->snr);
            }
        }
    } else if (pkt->payloadLen >= 6) {
        // Path=0: Direct packet from nearby node
        // Generate hash from first 6 bytes of payload (usually contains sender ID)
        uint8_t hash = pkt->payload[0];
        for (uint8_t i = 1; i < 6; i++) {
            hash ^= pkt->payload[i];
        }
        // Mark as direct with 0x80 prefix to distinguish from path hashes
        hash = (hash & 0x7F) | 0x80;
        bool isNew = seenNodes.update(hash, pkt->rssi, pkt->snr);
        if (isNew) {
            statsRecordUniqueNode();  // Persistent stats
            LOG(TAG_NODE " New %02X\n\r", hash);
        }
    }

    // Store-and-forward: save packets for offline nodes
    {
        uint8_t pt = pkt->header.getPayloadType();
        // Only for packet types that have dest_hash at payload[0]
        if (pkt->payloadLen >= 2 &&
            (pt == MC_PAYLOAD_REQUEST || pt == MC_PAYLOAD_RESPONSE ||
             pt == MC_PAYLOAD_PLAIN || pt == MC_PAYLOAD_ANON_REQ)) {
            uint8_t destHash = pkt->payload[0];
            // Not for us (already handled), not broadcast (hash 0)
            if (destHash != nodeIdentity.getNodeHash() && destHash != 0) {
                // Check if dest node is known but offline
                for (uint8_t i = 0; i < seenNodes.getCount(); i++) {
                    const SeenNode* sn = seenNodes.getNode(i);
                    if (sn && sn->hash == destHash && sn->pktCount >= 2 &&
                        (millis() - sn->lastSeen) > HEALTH_OFFLINE_MS) {
                        // Node is offline - store if we have time
                        if (timeSync.isSynchronized()) {
                            if (mailbox.store(destHash, pkt, timeSync.getTimestamp())) {
                                LOG(TAG_INFO " Mbox store %02X\n\r", destHash);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // Check if we should forward
    if (shouldForward(pkt)) {
        // Rate limit forwarding
        if (!repeaterHelper.allowForward()) {
            statsRecordRateLimited();  // Persistent stats
            LOG(TAG_FWD " Rate lim\n\r");
        } else {
            if (pkt->header.isDirect()) {
                // Circuit breaker: check next hop before peel
                if (pkt->pathLen >= 2) {
                    uint8_t nextHop = pkt->path[1];
                    if (repeaterHelper.getNeighbours().isCircuitOpen(nextHop)) {
                        LOG(TAG_FWD " CB %02X\n\r", nextHop);
                        goto skipForward;
                    }
                }
                // DIRECT routing: remove ourselves from path[0] (peel)
                pkt->pathLen--;
                for (uint8_t i = 0; i < pkt->pathLen; i++) {
                    pkt->path[i] = pkt->path[i + 1];
                }
                LOG(TAG_FWD " Direct p=%d\n\r", pkt->pathLen);
            } else {
                // FLOOD routing: add our hash to path
                pkt->path[pkt->pathLen++] = nodeIdentity.getNodeHash();
            }

            // Add to TX queue
            txQueue.add(pkt);
            fwdCount++;
            statsRecordFwd();  // Persistent stats
            LOG(TAG_FWD " Q p=%d\n\r", pkt->pathLen);
        }
        skipForward:;
    }
}

//=============================================================================
// Main Setup & Loop
//=============================================================================
void setup() {
#ifndef SILENT
    Serial.begin(115200);
    delay(1000);
#endif

    LOG_RAW("\n\rCubeCellMeshCore v%s EU868\n\r", FIRMWARE_VERSION);

    // Load configuration from EEPROM
    loadConfig();

    // Load persistent statistics
    loadPersistentStats();

    // Load store-and-forward mailbox
    mailbox.load();

    // Enable watchdog
#if MC_WATCHDOG_ENABLED && defined(CUBECELL)
    innerWdtEnable(true);
    LOG(TAG_SYSTEM " WDT on\n\r");
#endif

    // Generate node ID if not set
    if (nodeId == 0) {
        nodeId = generateNodeId();
    }
    LOG(TAG_SYSTEM " Node ID: %08lX\n\r", nodeId);

    // Initialize node identity (Ed25519 keys)
    LOG(TAG_SYSTEM " Init ID\n\r");
    if (nodeIdentity.begin()) {
        LOG(TAG_OK " ID: %s %02X\n\r",
            nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
    } else {
        LOG(TAG_ERROR " ID init fail!\n\r");
    }

    // Initialize ADVERT generator with time sync
    advertGen.begin(&nodeIdentity, &timeSync);
    advertGen.setInterval(ADVERT_INTERVAL_MS);
    advertGen.setEnabled(ADVERT_ENABLED);
    LOG(TAG_INFO " ADV: %s %lus\n\r",
        ADVERT_ENABLED ? "on" : "off",
        ADVERT_INTERVAL_MS / 1000);

    // Initialize telemetry
    telemetry.begin(&rxCount, &txCount, &fwdCount, &errCount, &lastRssi, &lastSnr);
    // Initialize repeater helper
    repeaterHelper.begin(&nodeIdentity);

    // Initialize contact manager for direct messaging
    contactMgr.begin(&nodeIdentity);

    // Load region map from EEPROM
    regionMap.load();

    initLed();
    packetCache.clear();
    txQueue.clear();
    seenNodes.clear();

    setupRadio();
    calculateTimings();
    applyPowerSettings();  // Apply loaded config
    startReceive();

    bootTime = millis();
    LOG(TAG_SYSTEM " Ready\n\r");
    LOG(TAG_INFO " Boot %ds\n\r", BOOT_SAFE_PERIOD_MS / 1000);
}

void loop() {
    feedWatchdog();

    // Auto-expire temporary radio settings
    // Use signed subtraction to handle millis() wraparound correctly
    if (tempRadioActive && tempRadioExpireTime > 0 && (int32_t)(millis() - tempRadioExpireTime) >= 0) {
        tempRadioActive = false;
        tempRadioExpireTime = 0;
        setupRadio(); startReceive(); calculateTimings();
        LOG(TAG_INFO " TmpRadio expired\n\r");
    }

    // Periodic AGC reset (restarts receiver to reset gain control)
    if (configAgcResetInterval > 0) {
        uint32_t now = millis();
        if ((now - lastAgcResetTime) >= (uint32_t)configAgcResetInterval * 1000UL) {
            lastAgcResetTime = now;
            if (!dio1Flag) startReceive();
        }
    }

    // Handle pending reboot from CLI command
    // Use signed subtraction to handle millis() wraparound correctly
    if (pendingReboot && (int32_t)(millis() - rebootTime) >= 0) {
        LOG(TAG_SYSTEM " Rebooting...\n\r");
        delay(100);  // Let serial flush
        HW_Reset(0);
    }

#ifndef SILENT
    checkSerial();
#endif

    // Check for received packet
    if (dio1Flag) {
        dio1Flag = false;
        uint16_t irq = radio.getIrqStatus();

        if (irq & RADIOLIB_SX126X_IRQ_RX_DONE) {
            ledRxOn();
            activeReceiveStart = 0;  // Reset active receive

            uint8_t buf[MC_RX_BUFFER_SIZE];
            uint16_t len = radio.getPacketLength();

            if (len > 0 && len <= sizeof(buf)) {
                radioError = radio.readData(buf, len);

                if (radioError == RADIOLIB_ERR_NONE) {
                    radioErrorCount = 0;  // Reset error counter on success

                    // Track RX airtime
                    repeaterHelper.addRxAirTime(calculatePacketAirtime(len));

                    MCPacket pkt;
                    pkt.clear();
                    pkt.rxTime = millis();
                    pkt.snr = (int8_t)(radio.getSNR() * 4);
                    pkt.rssi = (int16_t)radio.getRSSI();

                    // Store last RSSI/SNR for stats
                    lastRssi = pkt.rssi;
                    lastSnr = pkt.snr;

                    if (pkt.deserialize(buf, len)) {
                        processReceivedPacket(&pkt);
                    } else {
                        // Decode header for better diagnostics
                        if (len > 0) {
                            uint8_t route = buf[0] & 0x03;
                            uint8_t type = (buf[0] >> 2) & 0x0F;
                            uint8_t ver = (buf[0] >> 6) & 0x03;
                            // Show pathLen byte if available for debugging
                            uint8_t pathLenByte = (len > 1) ? buf[1] : 0;
                            LOG(TAG_ERROR " Bad pkt l=%d h=%02X(r=%d,t=%d,v=%d) pL=%d\n\r",
                                len, buf[0], route, type, ver, pathLenByte);
                        } else {
                            LOG(TAG_ERROR " Bad pkt l=%d\n\r", len);
                        }
                        errCount++;
                    }
                } else if (radioError == RADIOLIB_ERR_CRC_MISMATCH) {
                    LOG(TAG_ERROR " CRC err\n\r");
                    crcErrCount++;
                } else {
                    LOG(TAG_ERROR " RX err %d\n\r", radioError);
                    handleRadioError();
                }
            }

            ledOff();
            startReceive();
        }

        if (irq & RADIOLIB_SX126X_IRQ_TX_DONE) {
            ledOff();
            startReceive();
        }
    }

    // Process TX queue
    if (txQueue.getCount() > 0 && !dio1Flag) {
        MCPacket pkt;
        if (txQueue.pop(&pkt)) {
            // SNR adaptive delay using per-packet SNR
            uint32_t airtime = calculatePacketAirtime(pkt.payloadLen + pkt.pathLen + 2);
            uint32_t txDelay;
            if (pkt.header.isDirect()) {
                // DIRECT: half jitter only (higher priority), scaled by direct.txdelay
                txDelay = MC_TX_DELAY_MIN + calcTxJitter(airtime) / 2 * configDirectTxDelay / 100;
            } else {
                // FLOOD: rxDelay (SNR-weighted) + full jitter, scaled by factors
                uint8_t score = calcSnrScore(pkt.snr);
                txDelay = MC_TX_DELAY_MIN + calcRxDelay(score, airtime) * configRxDelayFactor / 100
                        + calcTxJitter(airtime) * configTxDelayFactor / 100;
            }
            // Apply airtime factor (tenths: 10=1.0x)
            if (configAirtimeFactor != 10 && configAirtimeFactor > 0) {
                txDelay = txDelay * configAirtimeFactor / 10;
            }
            LOG(TAG_TX " Wait %lums\n\r", txDelay);

            activeReceiveStart = 0;
            uint32_t start = millis();
            bool aborted = false;

            while (millis() - start < txDelay) {
                feedWatchdog();

                // Check for new packet or active reception
                if (dio1Flag || isActivelyReceiving()) {
                    LOG(TAG_TX " Busy\n\r");
                    txQueue.add(&pkt);  // Re-queue packet
                    aborted = true;
                    break;
                }
                delay(5);
            }

            if (!aborted && !dio1Flag && !isActivelyReceiving()) {
                ledGreenBlink();  // Green blink when forwarding
                transmitPacket(&pkt);
            }

            // Always ensure we're back in RX mode
            startReceive();
        }
    }

    // Check ADVERT beacon timer
    checkAdvertBeacon();

    // Check if persistent stats need auto-save
    checkStatsSave();

    // Periodic telemetry update (battery, temperature, stats)
    if (telemetry.shouldUpdate()) {
        telemetry.update();
    }

    // Periodic cleanup, health check, and mailbox TTL (every 60 seconds)
    {
        static uint32_t lastCleanup = 0;
        if (millis() - lastCleanup > 60000) {
            repeaterHelper.cleanup();
            healthCheck();
            sessionManager.cleanupSessions();  // Expire idle sessions (1h)
            if (timeSync.isSynchronized()) {
                mailbox.expireOld(timeSync.getTimestamp());
                // Quiet hours evaluation
                if (repeaterHelper.isQuietHoursEnabled()) {
                    TimeSync::DateTime dt;
                    TimeSync::timestampToDateTime(timeSync.getTimestamp(), dt);
                    repeaterHelper.evaluateQuietHours(dt.hour);
                }
            }
            // Adaptive TX power evaluation
            {
                int8_t newPower = repeaterHelper.evaluateAdaptiveTxPower();
                if (newPower >= 0) {
                    radio.setOutputPower(newPower);
                    LOG(TAG_INFO " TxP:%ddBm\n\r", newPower);
                }
            }
            lastCleanup = millis();
        }
    }

#ifdef ENABLE_DAILY_REPORT
    // Check daily report scheduler
    checkDailyReport();
#endif

    // Power saving when idle
    if (txQueue.getCount() == 0 && !dio1Flag) {
#ifndef SILENT
        delay(10);  // Let serial complete
#endif

        // LED status: red solid if no time sync, off otherwise
        if (!timeSync.isSynchronized()) {
            ledRedSolid();  // Red = waiting for time sync
        } else {
            ledOff();
        }

        // Skip deep sleep during boot safe period (allows serial commands)
        bool inBootSafe = (millis() - bootTime) < BOOT_SAFE_PERIOD_MS;

        if (deepSleepEnabled && powerSaveMode >= 1 && !inBootSafe) {
            enterDeepSleep();
        } else {
            uint8_t sleepMs = (powerSaveMode == 0) ? 5 : (inBootSafe ? 5 : 20);
            enterLightSleep(sleepMs);
        }
    }
}
