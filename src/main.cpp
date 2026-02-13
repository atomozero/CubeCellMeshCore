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
uint32_t getPacketId(MCPacket* pkt);

//=============================================================================
// Serial Command Handler
//=============================================================================
#ifndef SILENT
char cmdBuffer[48];  // Reduced from 64 to save RAM
uint8_t cmdPos = 0;

// Compact command handler - no ANSI box-drawing tables
void processCommand(char* cmd) {
    if (strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0) {
        LOG_RAW("status stats lifetime radiostats packetstats advert nodes contacts\n\r"
                "neighbours telemetry identity name location time nodetype passwd\n\r"
                "sleep rxboost radio tempradio ratelimit savestats alert newid\n\r"
                "power acl repeat ping rssi mode set report\n\r"
                "reset save reboot\n\r");
    }
    else if (strcmp(cmd, "status") == 0) {
        LOG_RAW("FW:%s Node:%s Hash:%02X\n\r", FIRMWARE_VERSION, nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
        LOG_RAW("Freq:%.3f BW:%.1f SF:%d CR:4/%d TX:%ddBm%s\n\r",
            getCurrentFrequency(), getCurrentBandwidth(), getCurrentSpreadingFactor(),
            getCurrentCodingRate(), MC_TX_POWER, tempRadioActive ? " [TEMP]" : "");
        LOG_RAW("Time:%s RSSI:%d SNR:%d.%d\n\r", timeSync.isSynchronized()?"sync":"nosync", lastRssi, lastSnr/4, abs(lastSnr%4)*25);
    }
    else if (strcmp(cmd, "stats") == 0) {
        LOG_RAW("RX:%lu TX:%lu FWD:%lu ERR:%lu\n\r", rxCount, txCount, fwdCount, errCount);
        LOG_RAW("ADV TX:%lu RX:%lu Q:%d/%d\n\r", advTxCount, advRxCount, txQueue.getCount(), MC_TX_QUEUE_SIZE);
    }
    else if (strcmp(cmd, "lifetime") == 0) {
        const PersistentStats* ps = getPersistentStats();
        LOG_RAW("Boots:%d Up:%lus\n\r", ps->bootCount, statsGetTotalUptime());
        LOG_RAW("RX:%lu TX:%lu FWD:%lu\n\r", ps->totalRxPackets, ps->totalTxPackets, ps->totalFwdPackets);
        LOG_RAW("Nodes:%lu Login:%lu/%lu RLim:%lu\n\r",
            ps->totalUniqueNodes, ps->totalLogins, ps->totalLoginFails, ps->totalRateLimited);
    }
    else if (strcmp(cmd, "savestats") == 0) {
        savePersistentStats();
        LOG_RAW("Stats saved\n\r");
    }
    else if (strcmp(cmd, "ratelimit") == 0) {
        LOG_RAW("RateLimit: %s\n\r", repeaterHelper.isRateLimitEnabled() ? "ON" : "OFF");
        LOG_RAW("Login:%lu/%d Request:%lu/%d Fwd:%lu/%d\n\r",
            repeaterHelper.getLoginLimiter().getTotalBlocked(), RATE_LIMIT_LOGIN_MAX,
            repeaterHelper.getRequestLimiter().getTotalBlocked(), RATE_LIMIT_REQUEST_MAX,
            repeaterHelper.getForwardLimiter().getTotalBlocked(), RATE_LIMIT_FORWARD_MAX);
    }
    else if (strncmp(cmd, "ratelimit ", 10) == 0) {
        if (strcmp(cmd + 10, "on") == 0) {
            repeaterHelper.setRateLimitEnabled(true);
            LOG_RAW("RateLimit ON\n\r");
        } else if (strcmp(cmd + 10, "off") == 0) {
            repeaterHelper.setRateLimitEnabled(false);
            LOG_RAW("RateLimit OFF\n\r");
        } else if (strcmp(cmd + 10, "reset") == 0) {
            repeaterHelper.resetRateLimitStats();
            LOG_RAW("RateLimit reset\n\r");
        }
    }
    else if (strcmp(cmd, "advert") == 0) {
        sendAdvert(true);
    }
    else if (strcmp(cmd, "nodes") == 0) {
        LOG_RAW("Nodes:%d\n\r", seenNodes.getCount());
        for (uint8_t i = 0; i < seenNodes.getCount(); i++) {
            const SeenNode* n = seenNodes.getNode(i);
            if (n) {
                uint32_t ago = (millis() - n->lastSeen) / 1000;
                if (timeSync.isSynchronized()) {
                    uint32_t ts = timeSync.getTimestamp() - ago;
                    TimeSync::DateTime dt;
                    TimeSync::timestampToDateTime(ts, dt);
                    LOG_RAW(" %02X %s %ddBm %02d/%02d/%02d %02d:%02d:%02d\n\r", n->hash, n->name[0]?n->name:"-", n->lastRssi, dt.day, dt.month, dt.year % 100, dt.hour, dt.minute, dt.second);
                } else {
                    LOG_RAW(" %02X %s %ddBm %lus ago\n\r", n->hash, n->name[0]?n->name:"-", n->lastRssi, ago);
                }
            }
        }
    }
    else if (strcmp(cmd, "newid") == 0) {
        LOG_RAW("Gen new ID...\n\r");
        nodeIdentity.reset();
        LOG_RAW("New: %s %02X - reboot\n\r", nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
    }
    #ifdef ENABLE_CRYPTO_TESTS
    else if (strcmp(cmd, "test") == 0) {
        // RFC 8032 Test Vector 1: empty message - VERIFY test
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

        // RFC 8032 Test Vector 1: SIGN test
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
    #endif // ENABLE_CRYPTO_TESTS
    else if (strcmp(cmd, "nodetype chat") == 0) {
        uint8_t flags = nodeIdentity.getFlags();
        flags = (flags & 0xF0) | MC_TYPE_CHAT_NODE;
        nodeIdentity.setFlags(flags);
        nodeIdentity.save();
        LOG_RAW("Type: CHAT 0x%02X\n\r", flags);
    }
    else if (strcmp(cmd, "nodetype repeater") == 0) {
        uint8_t flags = nodeIdentity.getFlags();
        flags = (flags & 0xF0) | MC_TYPE_REPEATER;
        nodeIdentity.setFlags(flags);
        nodeIdentity.save();
        LOG_RAW("Type: RPT 0x%02X\n\r", flags);
    }
    else if (strcmp(cmd, "passwd") == 0) {
        LOG_RAW("Admin: %s  Guest: %s\n\r",
            sessionManager.getAdminPassword(),
            sessionManager.getGuestPassword());
    }
    else if (strncmp(cmd, "passwd admin ", 13) == 0) {
        sessionManager.setAdminPassword(cmd + 13);
        saveConfig();
        LOG_RAW("Admin pwd: %s\n\r", cmd + 13);
    }
    else if (strncmp(cmd, "passwd guest ", 13) == 0) {
        sessionManager.setGuestPassword(cmd + 13);
        saveConfig();
        LOG_RAW("Guest pwd: %s\n\r", cmd + 13);
    }
    else if (strcmp(cmd, "sleep on") == 0) {
        deepSleepEnabled = true;
        saveConfig();
        LOG_RAW("Deep sleep: ON\n\r");
    }
    else if (strcmp(cmd, "sleep off") == 0) {
        deepSleepEnabled = false;
        saveConfig();
        LOG_RAW("Sleep: OFF\n\r");
    }
    else if (strcmp(cmd, "sleep") == 0) {
        LOG_RAW("Deep sleep: %s\n\r", deepSleepEnabled ? "ON" : "OFF");
    }
    else if (strcmp(cmd, "rxboost on") == 0) {
        rxBoostEnabled = true;
        applyPowerSettings();
        saveConfig();
        LOG_RAW("RX Boost: ON\n\r");
    }
    else if (strcmp(cmd, "rxboost off") == 0) {
        rxBoostEnabled = false;
        applyPowerSettings();
        saveConfig();
        LOG_RAW("RX Boost: OFF\n\r");
    }
    else if (strcmp(cmd, "rxboost") == 0) {
        LOG_RAW("RX Boost: %s\n\r", rxBoostEnabled ? "ON" : "OFF");
    }
    else if (strcmp(cmd, "time") == 0) {
        if (timeSync.isSynchronized()) {
            LOG_RAW("Time: %lu (synced)\n\r", timeSync.getTimestamp());
        } else {
            LOG_RAW("Time: not synced\n\r");
        }
    }
    else if (strncmp(cmd, "time ", 5) == 0) {
        uint32_t ts = strtoul(cmd + 5, NULL, 10);
        if (ts > 1577836800) {
            timeSync.setTime(ts);
            LOG_RAW("Time set: %lu\n\r", ts);
        } else {
            LOG_RAW("Invalid timestamp\n\r");
        }
    }
    else if (strncmp(cmd, "name ", 5) == 0) {
        const char* newName = cmd + 5;
        if (strlen(newName) > 0 && strlen(newName) < 16) {
            nodeIdentity.setNodeName(newName);
            nodeIdentity.save();
            LOG_RAW("Name set: %s\n\r", nodeIdentity.getNodeName());
        } else {
            LOG_RAW("Name must be 1-15 chars\n\r");
        }
    }
    else if (strcmp(cmd, "name") == 0) {
        LOG_RAW("Name: %s\n\r", nodeIdentity.getNodeName());
    }
    else if (strcmp(cmd, "location clear") == 0) {
        nodeIdentity.clearLocation();
        nodeIdentity.save();
        LOG_RAW("Location cleared\n\r");
    }
    else if (strncmp(cmd, "location ", 9) == 0) {
        char* args = (char*)(cmd + 9);
        char* space = strchr(args, ' ');
        if (space != NULL) {
            *space = '\0';
            float lat = atof(args);
            float lon = atof(space + 1);
            if (lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f) {
                nodeIdentity.setLocation(lat, lon);
                nodeIdentity.save();
                LOG_RAW("Location: %.6f, %.6f\n\r", lat, lon);
            } else {
                LOG_RAW("Invalid coords\n\r");
            }
        } else {
            LOG_RAW("Usage: location LAT LON\n\r");
        }
    }
    else if (strcmp(cmd, "location") == 0) {
        if (nodeIdentity.hasLocation()) {
            LOG_RAW("Location: %.6f, %.6f\n\r",
                nodeIdentity.getLatitudeFloat(), nodeIdentity.getLongitudeFloat());
        } else {
            LOG_RAW("Location: not set\n\r");
        }
    }
    else if (strcmp(cmd, "identity") == 0) {
        LOG_RAW("Name: %s  Hash: %02X  Type: %d\n\r",
            nodeIdentity.getNodeName(), nodeIdentity.getNodeHash(),
            nodeIdentity.getFlags() & 0x0F);
        const uint8_t* pk = nodeIdentity.getPublicKey();
        LOG_RAW("PubKey: ");
        for (int i = 0; i < 32; i++) LOG_RAW("%02x", pk[i]);
        LOG_RAW("\n\r");
    }
    else if (strcmp(cmd, "contacts") == 0) {
        LOG_RAW("Contacts: %d\n\r", contactMgr.getCount());
        for (uint8_t i = 0; i < contactMgr.getCount(); i++) {
            Contact* c = contactMgr.getContact(i);
            if (c) LOG_RAW(" %02X %s %ddBm\n\r", c->getHash(),
                c->name[0] ? c->name : "-", c->lastRssi);
        }
    }
    else if (strncmp(cmd, "contact ", 8) == 0) {
        // Show full pubkey for a contact by hash: contact XX
        uint8_t hash = strtoul(cmd + 8, NULL, 16);
        Contact* c = contactMgr.findByHash(hash);
        if (c) {
            LOG_RAW("Contact: %s (hash %02X)\n\r", c->name, c->getHash());
            LOG_RAW("PubKey: ");
            for (int i = 0; i < 32; i++) LOG_RAW("%02X", c->pubKey[i]);
            LOG_RAW("\n\r");
        } else {
            LOG_RAW("Contact %02X not found\n\r", hash);
        }
    }
    else if (strcmp(cmd, "neighbours") == 0 || strcmp(cmd, "neighbors") == 0) {
        NeighbourTracker& nb = repeaterHelper.getNeighbours();
        uint8_t cnt = nb.getCount();
        LOG_RAW("Neighbours: %d\n\r", cnt);
        for (uint8_t i = 0; i < cnt; i++) {
            const NeighbourInfo* n = nb.getNeighbour(i);
            if (n) {
                uint32_t ago = (millis() - n->lastHeard) / 1000;
                LOG_RAW(" %02X%02X%02X%02X%02X%02X rssi=%d snr=%d ago=%lus\n\r",
                    n->pubKeyPrefix[0], n->pubKeyPrefix[1], n->pubKeyPrefix[2],
                    n->pubKeyPrefix[3], n->pubKeyPrefix[4], n->pubKeyPrefix[5],
                    n->rssi, n->snr, ago);
            }
        }
    }
    else if (strncmp(cmd, "advert interval ", 16) == 0) {
        uint32_t interval = strtoul(cmd + 16, NULL, 10);
        if (interval >= 60 && interval <= 86400) {
            advertGen.setInterval(interval * 1000);
            LOG_RAW("ADVERT interval: %lus\n\r", interval);
        } else {
            LOG_RAW("Invalid (60-86400)\n\r");
        }
    }
    else if (strcmp(cmd, "advert interval") == 0) {
        LOG_RAW("ADVERT interval: %lus (next in %lus)\n\r",
            advertGen.getInterval() / 1000, advertGen.getTimeUntilNext());
    }
    else if (strcmp(cmd, "telemetry") == 0) {
        telemetry.update();
        const TelemetryData* t = telemetry.getData();
        LOG_RAW("Battery: %dmV (%d%%)\n\r", t->batteryMv, telemetry.getBatteryPercent());
        LOG_RAW("Temp: %dC  Uptime: %lus\n\r", t->temperature, t->uptime);
        LOG_RAW("RX:%lu TX:%lu FWD:%lu ERR:%lu\n\r",
            t->rxCount, t->txCount, t->fwdCount, t->errorCount);
    }
    #ifdef ENABLE_BATTDEBUG
    else if (strcmp(cmd, "battdebug") == 0) {
        #ifdef CUBECELL
        extern volatile int16 ADC_SAR_Seq_offset[];
        extern volatile int32 ADC_SAR_Seq_countsPer10Volt[];
        pinMode(VBAT_ADC_CTL, OUTPUT);
        digitalWrite(VBAT_ADC_CTL, LOW);
        delay(100);
        uint16_t r = analogRead(ADC);
        pinMode(VBAT_ADC_CTL, INPUT);
        LOG_RAW("ADC raw=%u\n\r", r);
        LOG_RAW("ch0 off=%d gain=%ld\n\r", ADC_SAR_Seq_offset[0], (long)ADC_SAR_Seq_countsPer10Volt[0]);
        int32_t g0 = ADC_SAR_Seq_countsPer10Volt[0];
        if (g0 != 0) {
            float mv = ((float)((int32_t)r - ADC_SAR_Seq_offset[0]) * 10000.0f) / (float)g0;
            LOG_RAW("cal: batt=%.0fmV\n\r", mv * 2.0f);
        }
        telemetry.update();
        LOG_RAW("Telemetry: %dmV (%d%%)\n\r", telemetry.getBatteryMv(), telemetry.getBatteryPercent());
        #endif
    }
    #endif
    else if (strcmp(cmd, "alert") == 0) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (alertDestPubKey[i] != 0) { keySet = true; break; }
        }
        LOG_RAW("Alert:%s Dest:%s\n\r",
            alertEnabled ? "ON" : "OFF",
            keySet ? "set" : "none");
    }
    else if (strcmp(cmd, "alert on") == 0) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (alertDestPubKey[i] != 0) { keySet = true; break; }
        }
        if (keySet) {
            alertEnabled = true;
            saveConfig();
            LOG_RAW("Alert ON\n\r");
        } else {
            LOG_RAW("No dest\n\r");
        }
    }
    else if (strcmp(cmd, "alert off") == 0) {
        alertEnabled = false;
        saveConfig();
        LOG_RAW("Alert OFF\n\r");
    }
    else if (strncmp(cmd, "alert dest ", 11) == 0) {
        const char* arg = cmd + 11;
        // Try to find contact by name first
        Contact* c = contactMgr.findByName(arg);
        if (c) {
            memcpy(alertDestPubKey, c->pubKey, REPORT_PUBKEY_SIZE);
            saveConfig();
            LOG_RAW("Dest:%s(%02X)\n\r", c->name, alertDestPubKey[0]);
        }
        // Otherwise try hex pubkey (64 chars = 32 bytes)
        else if (strlen(arg) >= 64) {
            for (int i = 0; i < 32; i++) {
                char byte[3] = {arg[i*2], arg[i*2+1], 0};
                alertDestPubKey[i] = strtoul(byte, NULL, 16);
            }
            saveConfig();
            LOG_RAW("Dest:%02X%02X%02X%02X\n\r",
                alertDestPubKey[0], alertDestPubKey[1],
                alertDestPubKey[2], alertDestPubKey[3]);
        } else {
            LOG_RAW("'%s' not found\n\r", arg);
        }
    }
    else if (strcmp(cmd, "alert clear") == 0) {
        memset(alertDestPubKey, 0, REPORT_PUBKEY_SIZE);
        alertEnabled = false;
        saveConfig();
        LOG_RAW("Alert cleared\n\r");
    }
    else if (strcmp(cmd, "alert test") == 0) {
        if (sendNodeAlert("TestNode", 0xAA, 1, -50)) {
            LOG_RAW("Test alert sent\n\r");
        } else {
            LOG_RAW("Alert not set\n\r");
        }
    }
    // Radio parameters
    else if (strcmp(cmd, "radio") == 0) {
        LOG_RAW("Def: %.3f BW%.1f SF%d CR%d %ddBm\n\r",
            (float)MC_FREQUENCY, (float)MC_BANDWIDTH, MC_SPREADING, MC_CODING_RATE, MC_TX_POWER);
        if (tempRadioActive) {
            LOG_RAW("Tmp: %.3f BW%.1f SF%d CR%d [ON]\n\r",
                tempFrequency, tempBandwidth, tempSpreadingFactor, tempCodingRate);
        }
    }
    // Temporary radio: tempradio <freq> <bw> <sf> <cr>
    else if (strncmp(cmd, "tempradio ", 10) == 0) {
        float freq, bw;
        int sf, cr;
        if (sscanf(cmd + 10, "%f %f %d %d", &freq, &bw, &sf, &cr) == 4) {
            // Validate parameters
            if (freq < 150.0f || freq > 960.0f) {
                LOG_RAW("Error: freq 150-960 MHz\n\r");
            } else if (bw < 7.8f || bw > 500.0f) {
                LOG_RAW("Error: bw 7.8-500 kHz\n\r");
            } else if (sf < 6 || sf > 12) {
                LOG_RAW("Error: sf 6-12\n\r");
            } else if (cr < 5 || cr > 8) {
                LOG_RAW("Error: cr 5-8\n\r");
            } else {
                tempFrequency = freq;
                tempBandwidth = bw;
                tempSpreadingFactor = sf;
                tempCodingRate = cr;
                tempRadioActive = true;
                LOG_RAW("Tmp: %.3f BW%.1f SF%d CR%d\n\r", freq, bw, sf, cr);
                setupRadio();
                startReceive();
                calculateTimings();
                LOG_RAW("OK\n\r");
            }
        } else if (strcmp(cmd + 10, "off") == 0) {
            if (tempRadioActive) {
                tempRadioActive = false;
                LOG_RAW("Reverting...\n\r");
                setupRadio();
                startReceive();
                calculateTimings();
                LOG_RAW("OK\n\r");
            } else {
                LOG_RAW("Temp radio not active\n\r");
            }
        } else {
            LOG_RAW("tempradio <freq> <bw> <sf> <cr> | off\n\r");
        }
    }
    else if (strcmp(cmd, "tempradio") == 0) {
        if (tempRadioActive) {
            LOG_RAW("Tmp: %.3f BW%.1f SF%d CR%d [ON]\n\r",
                tempFrequency, tempBandwidth, tempSpreadingFactor, tempCodingRate);
        } else {
            LOG_RAW("Tmp radio off\n\r");
        }
    }
    else if (strcmp(cmd, "radiostats") == 0) {
        const RadioStats& rs = repeaterHelper.getRadioStats();
        LOG_RAW("Noise:%ddBm RSSI:%d SNR:%d.%ddB\n\r",
            rs.noiseFloor, rs.lastRssi, rs.lastSnr/4, abs(rs.lastSnr%4)*25);
        LOG_RAW("Airtime TX:%lus RX:%lus\n\r", rs.txAirTimeSec, rs.rxAirTimeSec);
    }
    else if (strcmp(cmd, "packetstats") == 0) {
        const PacketStats& ps = repeaterHelper.getPacketStats();
        LOG_RAW("RX:%lu TX:%lu\n\r", ps.numRecvPackets, ps.numSentPackets);
        LOG_RAW("Flood RX:%lu TX:%lu Direct RX:%lu TX:%lu\n\r",
            ps.numRecvFlood, ps.numSentFlood, ps.numRecvDirect, ps.numSentDirect);
    }
    else if (strcmp(cmd, "power") == 0) {
        const char* modeStr = powerSaveMode == 0 ? "Perf" : powerSaveMode == 1 ? "Bal" : "PwrSave";
        LOG_RAW("Mode:%s RxBoost:%s Sleep:%s\n\r", modeStr,
            rxBoostEnabled ? "ON" : "OFF", deepSleepEnabled ? "ON" : "OFF");
    }
    else if (strncmp(cmd, "mode ", 5) == 0) {
        char m = cmd[5];
        if (m == '0') { powerSaveMode = 0; saveConfig(); LOG_RAW("Mode: Perf\n\r"); }
        else if (m == '1') { powerSaveMode = 1; saveConfig(); LOG_RAW("Mode: Bal\n\r"); }
        else if (m == '2') { powerSaveMode = 2; saveConfig(); LOG_RAW("Mode: PwrSave\n\r"); }
    }
    else if (strcmp(cmd, "acl") == 0) {
        LOG_RAW("Admin:%s Guest:%s Sessions:%d\n\r",
            sessionManager.getAdminPassword(),
            strlen(sessionManager.getGuestPassword()) > 0 ? sessionManager.getGuestPassword() : "(off)",
            sessionManager.getSessionCount());
    }
    else if (strcmp(cmd, "repeat") == 0) {
        LOG_RAW("Repeat:%s Hops:%d\n\r",
            repeaterHelper.isRepeatEnabled() ? "ON" : "OFF",
            repeaterHelper.getMaxFloodHops());
    }
    else if (strcmp(cmd, "set repeat on") == 0) {
        repeaterHelper.setRepeatEnabled(true); LOG_RAW("Repeat ON\n\r");
    }
    else if (strcmp(cmd, "set repeat off") == 0) {
        repeaterHelper.setRepeatEnabled(false); LOG_RAW("Repeat OFF\n\r");
    }
    else if (strncmp(cmd, "set flood.max ", 14) == 0) {
        uint8_t hops = atoi(cmd + 14);
        if (hops >= 1 && hops <= 15) { repeaterHelper.setMaxFloodHops(hops); LOG_RAW("Flood max:%d\n\r", hops); }
    }
    else if (strcmp(cmd, "ping") == 0) {
        sendPing();
    }
    else if (strncmp(cmd, "ping ", 5) == 0) {
        uint8_t h = (uint8_t)strtoul(cmd + 5, NULL, 16);
        if (h != 0) { sendDirectedPing(h); }
        else { LOG_RAW("Use: ping <hex>\n\r"); }
    }
    else if (strcmp(cmd, "rssi") == 0) {
        LOG_RAW("RSSI:%d SNR:%d.%02ddB\n\r", lastRssi, lastSnr/4, abs(lastSnr%4)*25);
    }
    else if (strcmp(cmd, "advert local") == 0) {
        sendAdvert(false);
    }
#ifdef ENABLE_DAILY_REPORT
    else if (strcmp(cmd, "report") == 0) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keySet = true; break; }
        }
        LOG_RAW("Report:%s Time:%02d:%02d Dest:%s\n\r",
            reportEnabled ? "ON" : "OFF", reportHour, reportMinute,
            keySet ? "set" : "none");
    }
    else if (strcmp(cmd, "report on") == 0) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keySet = true; break; }
        }
        if (keySet) {
            reportEnabled = true; saveConfig();
            LOG_RAW("Report ON (%02d:%02d)\n\r", reportHour, reportMinute);
        } else LOG_RAW("No dest key\n\r");
    }
    else if (strcmp(cmd, "report off") == 0) {
        reportEnabled = false; saveConfig(); LOG_RAW("Report OFF\n\r");
    }
    else if (strcmp(cmd, "report clear") == 0) {
        reportEnabled = false;
        memset(reportDestPubKey, 0, REPORT_PUBKEY_SIZE);
        saveConfig(); LOG_RAW("Report cleared\n\r");
    }
    else if (strcmp(cmd, "report test") == 0) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keySet = true; break; }
        }
        if (keySet) {
            extern bool sendDailyReport();
            LOG_RAW(sendDailyReport() ? "Report sent\n\r" : "Report fail\n\r");
        } else LOG_RAW("No dest key\n\r");
    }
    else if (strncmp(cmd, "report dest ", 12) == 0) {
        const char* arg = cmd + 12;
        Contact* c = contactMgr.findByName(arg);
        if (c) {
            memcpy(reportDestPubKey, c->pubKey, REPORT_PUBKEY_SIZE);
            saveConfig();
            LOG_RAW("Dest:%s(%02X)\n\r", c->name, reportDestPubKey[0]);
        } else LOG_RAW("'%s' not found\n\r", arg);
    }
    else if (strncmp(cmd, "report time ", 12) == 0) {
        int h, m;
        if (sscanf(cmd + 12, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            reportHour = h; reportMinute = m; saveConfig();
            LOG_RAW("Report time: %02d:%02d\n\r", reportHour, reportMinute);
        } else LOG_RAW("Use: report time HH:MM\n\r");
    }
#endif // ENABLE_DAILY_REPORT
#ifndef LITE_MODE
    else if (strcmp(cmd, "contacts") == 0) {
        contactMgr.printContacts();
    }
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
    else if (strcmp(cmd, "reset") == 0) {
        resetConfig();
        applyPowerSettings();
        LOG_RAW("Config reset\n\r");
    }
    else if (strcmp(cmd, "save") == 0) {
        saveConfig();
        LOG_RAW("Config saved\n\r");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        LOG_RAW("Rebooting...\n\r");
        delay(100);
        #ifdef CUBECELL
        NVIC_SystemReset();
        #endif
    }
    else if (strlen(cmd) > 0) {
        LOG_RAW("Unknown: %s\n\r", cmd);
    }
}
// End of processCommand - ANSI table version removed to save Flash

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
    uint16_t len = 0;

    // Helper macro to append to response
    #define RESP_APPEND(...) do { \
        int _rc = snprintf(response + len, maxLen - len, __VA_ARGS__); \
        if (_rc > 0 && len + _rc < maxLen) len += _rc; \
    } while(0)

    // === Read-only commands (guest + admin) ===

    if (strcmp(cmd, "status") == 0) {
        RESP_APPEND("FW:v%s %s(%02X) Up:%lus T:%s\n", FIRMWARE_VERSION,
            nodeIdentity.getNodeName(), nodeIdentity.getNodeHash(),
            millis() / 1000, timeSync.isSynchronized() ? "sync" : "nosync");
    }
    else if (strcmp(cmd, "stats") == 0) {
        RESP_APPEND("RX:%lu TX:%lu FWD:%lu E:%lu ADV:%lu/%lu Q:%d\n",
            rxCount, txCount, fwdCount, errCount, advTxCount, advRxCount, txQueue.getCount());
    }
    else if (strcmp(cmd, "time") == 0) {
        if (timeSync.isSynchronized()) {
            RESP_APPEND("T:%lu sync\n", timeSync.getTimestamp());
        } else {
            RESP_APPEND("T:nosync\n");
        }
    }
    else if (strcmp(cmd, "telemetry") == 0) {
        telemetry.update();
        RESP_APPEND("Batt:%dmV Up:%lus\n", telemetry.getBatteryMv(), millis() / 1000);
    }
    else if (strcmp(cmd, "nodes") == 0) {
        uint8_t count = seenNodes.getCount();
        RESP_APPEND("Nodes:%d\n", count);
        for (uint8_t i = 0; i < count && len < maxLen - 48; i++) {
            const SeenNode* n = seenNodes.getNode(i);
            if (n && n->lastSeen > 0) {
                uint32_t ago = (millis() - n->lastSeen) / 1000;
                if (timeSync.isSynchronized()) {
                    uint32_t ts = timeSync.getTimestamp() - ago;
                    TimeSync::DateTime dt;
                    TimeSync::timestampToDateTime(ts, dt);
                    RESP_APPEND("%02X %s %ddBm %d.%ddB %02d/%02d/%02d %02d:%02d\n", n->hash, n->name[0]?n->name:"-", n->lastRssi, n->lastSnr/4, abs(n->lastSnr%4)*25, dt.day, dt.month, dt.year % 100, dt.hour, dt.minute);
                } else {
                    RESP_APPEND("%02X %s %ddBm %d.%ddB %lus\n", n->hash, n->name[0]?n->name:"-", n->lastRssi, n->lastSnr/4, abs(n->lastSnr%4)*25, ago);
                }
            }
        }
    }
    else if (strcmp(cmd, "neighbours") == 0 || strcmp(cmd, "neighbors") == 0) {
        NeighbourTracker& nb = repeaterHelper.getNeighbours();
        uint8_t count = nb.getCount();
        RESP_APPEND("Nbr:%d\n", count);
    }
    else if (strcmp(cmd, "repeat") == 0) {
        RESP_APPEND("Rpt:%s hops:%d\n", repeaterHelper.isRepeatEnabled() ? "on" : "off",
            repeaterHelper.getMaxFloodHops());
    }
    else if (strcmp(cmd, "identity") == 0) {
        RESP_APPEND("%s %02X\n", nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
        if (nodeIdentity.hasLocation()) {
            RESP_APPEND("Loc: %.6f,%.6f\n",
                nodeIdentity.getLatitude() / 1000000.0f,
                nodeIdentity.getLongitude() / 1000000.0f);
        }
    }
    else if (strcmp(cmd, "location") == 0) {
        if (nodeIdentity.hasLocation()) {
            RESP_APPEND("%.6f,%.6f\n",
                nodeIdentity.getLatitude() / 1000000.0f,
                nodeIdentity.getLongitude() / 1000000.0f);
        } else {
            RESP_APPEND("No loc\n");
        }
    }
    else if (strcmp(cmd, "advert interval") == 0) {
        RESP_APPEND("Int:%lus next:%lus\n", advertGen.getInterval() / 1000, advertGen.getTimeUntilNext());
    }
    else if (strcmp(cmd, "radiostats") == 0) {
        const RadioStats& rs = repeaterHelper.getRadioStats();
        RESP_APPEND("Noise:%ddBm RSSI:%d SNR:%d.%ddB\n",
            rs.noiseFloor, rs.lastRssi, rs.lastSnr/4, abs(rs.lastSnr%4)*25);
        RESP_APPEND("Airtime TX:%lus RX:%lus\n", rs.txAirTimeSec, rs.rxAirTimeSec);
    }
    else if (strcmp(cmd, "packetstats") == 0) {
        const PacketStats& ps = repeaterHelper.getPacketStats();
        RESP_APPEND("RX:%lu TX:%lu\n", ps.numRecvPackets, ps.numSentPackets);
        RESP_APPEND("FL RX:%lu TX:%lu DR RX:%lu TX:%lu\n",
            ps.numRecvFlood, ps.numSentFlood, ps.numRecvDirect, ps.numSentDirect);
    }
    else if (strcmp(cmd, "radio") == 0) {
        RESP_APPEND("%.3f BW%.1f SF%d CR%d\n",
            (float)MC_FREQUENCY, (float)MC_BANDWIDTH, MC_SPREADING, MC_CODING_RATE);
    }
    else if (strcmp(cmd, "lifetime") == 0) {
        const PersistentStats* ps = getPersistentStats();
        RESP_APPEND("Boots:%u RX:%lu TX:%lu FWD:%lu Nodes:%lu\n",
            ps->bootCount, ps->totalRxPackets, ps->totalTxPackets,
            ps->totalFwdPackets, ps->totalUniqueNodes);
    }

    // === Admin-only commands ===

    else if (!isAdmin) {
        RESP_APPEND("Err:admin\n");
        return len;
    }

    // From here, all commands require admin

    else if (strcmp(cmd, "set repeat on") == 0) {
        repeaterHelper.setRepeatEnabled(true);
        RESP_APPEND("rpt:on\n");
    }
    else if (strcmp(cmd, "set repeat off") == 0) {
        repeaterHelper.setRepeatEnabled(false);
        RESP_APPEND("rpt:off\n");
    }
    else if (strncmp(cmd, "set flood.max ", 14) == 0) {
        uint8_t hops = atoi(cmd + 14);
        if (hops >= 1 && hops <= 15) {
            repeaterHelper.setMaxFloodHops(hops);
            RESP_APPEND("hops:%d\n", hops);
        } else RESP_APPEND("E:1-15\n");
    }
    else if (strncmp(cmd, "set password ", 13) == 0) {
        const char* pwd = cmd + 13;
        if (strlen(pwd) > 0 && strlen(pwd) <= 15) {
            sessionManager.setAdminPassword(pwd);
            saveConfig();
            RESP_APPEND("pwd set\n");
        } else RESP_APPEND("E:1-15\n");
    }
    else if (strncmp(cmd, "set guest ", 10) == 0) {
        const char* pwd = cmd + 10;
        if (strlen(pwd) <= 15) {
            sessionManager.setGuestPassword(pwd);
            saveConfig();
            RESP_APPEND("guest set\n");
        } else RESP_APPEND("E:0-15\n");
    }
    else if (strncmp(cmd, "name ", 5) == 0) {
        const char* newName = cmd + 5;
        if (strlen(newName) > 0 && strlen(newName) < MC_NODE_NAME_MAX) {
            nodeIdentity.setNodeName(newName);
            RESP_APPEND("name=%s\n", newName);
        } else RESP_APPEND("E:1-15\n");
    }
    else if (strcmp(cmd, "location clear") == 0) {
        nodeIdentity.clearLocation();
        RESP_APPEND("loc clr\n");
    }
    else if (strncmp(cmd, "location ", 9) == 0) {
        float lat, lon;
        if (sscanf(cmd + 9, "%f %f", &lat, &lon) == 2 &&
            lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
            nodeIdentity.setLocation(lat, lon);
            RESP_APPEND("%.6f,%.6f\n", lat, lon);
        } else RESP_APPEND("E:loc\n");
    }
    else if (strncmp(cmd, "advert interval ", 16) == 0) {
        uint32_t interval = strtoul(cmd + 16, NULL, 10);
        if (interval >= 60 && interval <= 86400) {
            advertGen.setInterval(interval * 1000);
            RESP_APPEND("int:%lus\n", interval);
        } else RESP_APPEND("E:60-86400\n");
    }
    else if (strcmp(cmd, "advert") == 0) {
        sendAdvert(true);
        RESP_APPEND("adv sent\n");
    }
    else if (strcmp(cmd, "advert local") == 0) {
        sendAdvert(false);
        RESP_APPEND("adv local\n");
    }
    else if (strcmp(cmd, "ping") == 0) {
        sendPing();
        RESP_APPEND("ping sent\n");
    }
    else if (strncmp(cmd, "ping ", 5) == 0) {
        uint8_t h = (uint8_t)strtoul(cmd + 5, NULL, 16);
        if (h != 0) { sendDirectedPing(h); RESP_APPEND("ping->%02X\n", h); }
        else { RESP_APPEND("E:hex\n"); }
    }
    else if (strcmp(cmd, "rxboost on") == 0) {
        rxBoostEnabled = true;
        applyPowerSettings(); saveConfig();
        RESP_APPEND("RxB:ON\n");
    }
    else if (strcmp(cmd, "rxboost off") == 0) {
        rxBoostEnabled = false;
        applyPowerSettings(); saveConfig();
        RESP_APPEND("RxB:OFF\n");
    }
    else if (strcmp(cmd, "rxboost") == 0) {
        RESP_APPEND("RxB:%s\n", rxBoostEnabled ? "ON" : "OFF");
    }
    else if (strcmp(cmd, "save") == 0) {
        saveConfig();
        RESP_APPEND("saved\n");
    }
    else if (strcmp(cmd, "reset") == 0) {
        resetConfig();
        RESP_APPEND("reset\n");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        RESP_APPEND("reboot\n");
    }
#ifdef ENABLE_DAILY_REPORT
    else if (strcmp(cmd, "report") == 0) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keySet = true; break; }
        }
        RESP_APPEND("Rpt:%s %02d:%02d D:%02X%s\n",
            reportEnabled ? "ON" : "OFF", reportHour, reportMinute,
            reportDestPubKey[0], keySet ? "" : "(no)");
    }
    else if (strncmp(cmd, "report dest ", 12) == 0 && isAdmin) {
        const char* arg = cmd + 12;
        Contact* c = contactMgr.findByName(arg);
        if (c) {
            memcpy(reportDestPubKey, c->pubKey, REPORT_PUBKEY_SIZE);
            saveConfig();
            RESP_APPEND("Dest:%s(%02X)\n", c->name, reportDestPubKey[0]);
        } else RESP_APPEND("E:not found\n");
    }
    else if (strcmp(cmd, "report on") == 0 && isAdmin) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keySet = true; break; }
        }
        if (keySet) {
            reportEnabled = true; saveConfig();
            RESP_APPEND("Rpt ON %02d:%02d\n", reportHour, reportMinute);
        } else RESP_APPEND("E:no dest\n");
    }
    else if (strcmp(cmd, "report off") == 0 && isAdmin) {
        reportEnabled = false; saveConfig();
        RESP_APPEND("Rpt OFF\n");
    }
    else if (strcmp(cmd, "report test") == 0 && isAdmin) {
        extern uint16_t generateReportContent(char*, uint16_t);
        len = generateReportContent(response, maxLen - 1);
    }
    else if (strcmp(cmd, "report nodes") == 0 && isAdmin) {
        extern uint16_t generateNodesReport(char*, uint16_t);
        len = generateNodesReport(response, maxLen - 1);
    }
    else if (strncmp(cmd, "report time ", 12) == 0 && isAdmin) {
        int h, m;
        if (sscanf(cmd + 12, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            reportHour = h; reportMinute = m; saveConfig();
            RESP_APPEND("Rpt %02d:%02d\n", h, m);
        } else RESP_APPEND("E:HH:MM\n");
    }
#endif
    else if (strcmp(cmd, "help") == 0) {
        RESP_APPEND("status stats time nodes identity\n");
        RESP_APPEND("telemetry radio location\n");
        RESP_APPEND("ping rxboost advert save reboot");
    }
    else {
        RESP_APPEND("E:?\n");
    }

    #undef RESP_APPEND
    return len;
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
    // Calculate timing based on LoRa settings
    // tSym = 2^SF / BW (in seconds)
    float bandwidthHz = MC_BANDWIDTH * 1000.0f;
    float tSymSec = (float)(1 << MC_SPREADING) / bandwidthHz;
    float tSymMs = tSymSec * 1000.0f;  // Convert to milliseconds

    // Preamble time = (preambleLen + 4.25) * tSym
    preambleTimeMsec = (uint32_t)((MC_PREAMBLE_LEN + 4.25f) * tSymMs);

    // Slot time for CSMA (simplified)
    slotTimeMsec = (uint32_t)(tSymMs * 8.5f + 10);  // ~8.5 symbols + margin

    // Max packet time for 255 bytes payload
    // PayloadSymbols = 8 + max(ceil((8*PL - 4*SF + 28 + 16) / (4*SF)) * CR, 0)
    float payloadBits = 8.0f * 255;  // Max payload
    float numerator = payloadBits - 4.0f * MC_SPREADING + 28 + 16;
    float denominator = 4.0f * MC_SPREADING;
    float numPayloadSym = 8 + max(ceil(numerator / denominator) * MC_CODING_RATE, 0.0f);

    // Total packet time = preamble + payload symbols
    float totalSymbols = (MC_PREAMBLE_LEN + 4.25f) + numPayloadSym;
    maxPacketTimeMsec = (uint32_t)(totalSymbols * tSymMs) + 50;  // +50ms margin

    LOG(TAG_RADIO " T: p=%lu s=%lu m=%lu\n\r",
        preambleTimeMsec, slotTimeMsec, maxPacketTimeMsec);
}

/**
 * Calculate airtime for a given packet length using current LoRa parameters
 * @param packetLen Total packet length in bytes (header + path + payload)
 * @return Airtime in milliseconds
 */
uint32_t calculatePacketAirtime(uint16_t packetLen) {
    float bwHz = getCurrentBandwidth() * 1000.0f;
    uint8_t sf = getCurrentSpreadingFactor();
    uint8_t cr = getCurrentCodingRate();
    float tSymMs = (float)(1 << sf) / bwHz * 1000.0f;

    // Preamble time
    float preamble = (MC_PREAMBLE_LEN + 4.25f) * tSymMs;

    // Payload symbols: 8 + max(ceil((8*PL - 4*SF + 28 + 16) / (4*SF)) * CR, 0)
    float num = 8.0f * packetLen - 4.0f * sf + 28 + 16;
    float den = 4.0f * sf;
    float payloadSym = 8 + max(ceil(num / den) * cr, 0.0f);

    return (uint32_t)(preamble + payloadSym * tSymMs) + 1;  // +1ms rounding
}

uint32_t getTxDelayWeighted(int8_t snr) {
    // High SNR = longer delay (let weaker nodes go first)
    // Low SNR = shorter delay (we might be far away)
    const int8_t SNR_MIN = -20 * 4;  // -20dB in 0.25dB units
    const int8_t SNR_MAX = 15 * 4;   // +15dB

    // Map SNR to CW size (2-8)
    uint8_t cwSize = map(constrain(snr, SNR_MIN, SNR_MAX), SNR_MIN, SNR_MAX, 2, 8);

    return random(0, 2 * cwSize) * slotTimeMsec;
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
float getCurrentFrequency() {
    return tempRadioActive ? tempFrequency : MC_FREQUENCY;
}
float getCurrentBandwidth() {
    return tempRadioActive ? tempBandwidth : MC_BANDWIDTH;
}
uint8_t getCurrentSpreadingFactor() {
    return tempRadioActive ? tempSpreadingFactor : MC_SPREADING;
}
uint8_t getCurrentCodingRate() {
    return tempRadioActive ? tempCodingRate : MC_CODING_RATE;
}

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

    LOG(TAG_RADIO " %.3f BW%.1f SF%d CR%d\n\r", freq, bw, sf, cr);
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

    // Log packet if enabled
    packetLogger.log(pkt, true);  // true = TX

    radio.finishTransmit();
    dio1Flag = false;

    ledTxOn();
    LOG(TAG_TX " %s %s path=%d len=%d\n\r",
        mcRouteTypeName(pkt->header.getRouteType()),
        mcPayloadTypeName(pkt->header.getPayloadType()),
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
    // Check if destination key is set
    bool keySet = false;
    for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
        if (destPubKey[i] != 0) { keySet = true; break; }
    }
    if (!keySet || !timeSync.isSynchronized()) return false;

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

void checkAdvertBeacon() {
    // Check for pending ADVERT after time sync
    if (pendingAdvertTime > 0 && millis() >= pendingAdvertTime) {
        pendingAdvertTime = 0;  // Clear pending
        LOG(TAG_ADVERT " Sched ADV post-sync\n\r");
        sendAdvert(true);
        return;  // Don't send another one immediately
    }

    // Regular beacon interval (only if time is synced)
    if (advertGen.shouldSend() && timeSync.isSynchronized()) {
        sendAdvert(true);
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
    // Only forward flood packets
    if (!pkt->header.isFlood()) {
        return false;
    }

    // Don't forward packets specifically addressed to us
    // ANON_REQ, REQUEST, RESPONSE all have dest_hash at payload[0]
    uint8_t payloadType = pkt->header.getPayloadType();
    if (payloadType == MC_PAYLOAD_ANON_REQ ||
        payloadType == MC_PAYLOAD_REQUEST ||
        payloadType == MC_PAYLOAD_RESPONSE) {
        if (pkt->payloadLen > 0 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Addressed to us - don't forward
            return false;
        }
    }

    // Check packet ID cache
    uint32_t id = getPacketId(pkt);
    if (!packetCache.addIfNew(id)) {
        return false;
    }

    // Check path length (max 64 hops)
    if (pkt->pathLen >= MC_MAX_PATH_SIZE - 1) {
        return false;
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
    // Delay = retransmit_delay * 4 * random factor
    uint32_t baseDelay = getTxDelayWeighted(pkt->snr);
    uint32_t randomDelay = random(baseDelay * 2, baseDelay * 6);

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
                // Battery voltage (channel 1)
                lpp.addVoltage(1, telemetry.getBatteryMv() / 1000.0f);
                // Temperature (channel 2)
                lpp.addTemperature(2, (float)telemetry.getTemperature());
                // Node count as analog input (channel 3)
                lpp.addAnalogInput(3, (float)seenNodes.getCount());
                // Uptime as analog input in hours (channel 4)
                lpp.addAnalogInput(4, telemetry.getUptime() / 3600.0f);
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
        bool keyEmpty = true;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keyEmpty = false; break; }
        }

        if (isNewKey || keyEmpty) {
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

    // Log packet if enabled
    packetLogger.log(pkt, false);  // false = RX

    LOG(TAG_RX " %s %s path=%d len=%d rssi=%ddBm snr=%d.%ddB\n\r",
        mcRouteTypeName(pkt->header.getRouteType()),
        mcPayloadTypeName(pkt->header.getPayloadType()),
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
            pkt->path[pkt->pathLen++] = (int8_t)(pkt->snr);
            // Forward the trace packet
            txQueue.add(pkt);
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
            if (advInfo.hasLocation) LOG_RAW(" %.4f,%.4f", advInfo.latitude, advInfo.longitude);
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
            const uint8_t* pubKey = &pkt->payload[ADVERT_PUBKEY_OFFSET];
            contactMgr.updateFromAdvert(pubKey, advInfo.name, pkt->rssi, pkt->snr);

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

    // Check if we should forward
    if (shouldForward(pkt)) {
        // Rate limit forwarding
        if (!repeaterHelper.allowForward()) {
            statsRecordRateLimited();  // Persistent stats
            LOG(TAG_FWD " Rate lim\n\r");
        } else {
            // Add our node hash to path (simplified: just add a byte)
            uint8_t myHash = (nodeId >> 24) ^ (nodeId >> 16) ^
                             (nodeId >> 8) ^ nodeId;
            pkt->path[pkt->pathLen++] = myHash;

            // Add to TX queue
            txQueue.add(pkt);
            fwdCount++;
            statsRecordFwd();  // Persistent stats
            LOG(TAG_FWD " Q p=%d\n\r", pkt->pathLen);
        }
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

    // Handle pending reboot from CLI command
    if (pendingReboot && millis() >= rebootTime) {
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
                        // Debug: show raw packet info
                        LOG(TAG_ERROR " Bad pkt l=%d h=%02X\n\r", len, buf[0]);
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
            // SNR-weighted delay (higher SNR = longer wait)
            uint32_t txDelay = MC_TX_DELAY_MIN + getTxDelayWeighted(lastSnr);
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

    // Periodic cleanup of expired neighbors (every 60 seconds)
    {
        static uint32_t lastCleanup = 0;
        if (millis() - lastCleanup > 60000) {
            repeaterHelper.cleanup();
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
