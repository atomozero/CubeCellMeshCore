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
void sendAdvertNoFlags();
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

#if defined(MINIMAL_DEBUG) && defined(LITE_MODE)
// Minimal command handler - essential commands only
void processCommand(char* cmd) {
    if (strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0) {
        LOG_RAW("Cmds:status stats advert nodes contacts neighbours telemetry identity\n\r"
                "name[n] location[lat lon] time[ts] nodetype passwd sleep rxboost\n\r"
                "alert[on|off|dest|clear|test] newid reset save reboot\n\r");
    }
    else if (strcmp(cmd, "status") == 0) {
        LOG_RAW("FW:%s Node:%s Hash:%02X\n\r", FIRMWARE_VERSION, nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
        LOG_RAW("Freq:%.3f BW:%.1f SF:%d CR:4/%d TX:%ddBm\n\r", MC_FREQUENCY, MC_BANDWIDTH, MC_SPREADING, MC_CODING_RATE, MC_TX_POWER);
        LOG_RAW("Time:%s RSSI:%d SNR:%d.%d\n\r", timeSync.isSynchronized()?"sync":"nosync", lastRssi, lastSnr/4, abs(lastSnr%4)*25);
    }
    else if (strcmp(cmd, "stats") == 0) {
        LOG_RAW("RX:%lu TX:%lu FWD:%lu ERR:%lu\n\r", rxCount, txCount, fwdCount, errCount);
        LOG_RAW("ADV TX:%lu RX:%lu Q:%d/%d\n\r", advTxCount, advRxCount, txQueue.getCount(), MC_TX_QUEUE_SIZE);
    }
    else if (strcmp(cmd, "advert") == 0) {
        sendAdvert(true);
    }
    else if (strcmp(cmd, "nodes") == 0) {
        LOG_RAW("Nodes:%d\n\r", seenNodes.getCount());
        for (uint8_t i = 0; i < seenNodes.getCount(); i++) {
            const SeenNode* n = seenNodes.getNode(i);
            if (n) LOG_RAW(" %02X %s %ddBm\n\r", n->hash, n->name[0]?n->name:"-", n->lastRssi);
        }
    }
    else if (strcmp(cmd, "newid") == 0) {
        LOG_RAW("Generating new identity...\n\r");
        nodeIdentity.reset();
        LOG_RAW("New: %s (hash:%02X) - reboot now\n\r", nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
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
        LOG_RAW("Node type: CHAT (0x%02X) - send advert\n\r", flags);
    }
    else if (strcmp(cmd, "nodetype repeater") == 0) {
        uint8_t flags = nodeIdentity.getFlags();
        flags = (flags & 0xF0) | MC_TYPE_REPEATER;
        nodeIdentity.setFlags(flags);
        nodeIdentity.save();
        LOG_RAW("Node type: REPEATER (0x%02X) - send advert\n\r", flags);
    }
    else if (strcmp(cmd, "passwd") == 0) {
        LOG_RAW("Admin: %s  Guest: %s\n\r",
            sessionManager.getAdminPassword(),
            sessionManager.getGuestPassword());
    }
    else if (strncmp(cmd, "passwd admin ", 13) == 0) {
        sessionManager.setAdminPassword(cmd + 13);
        saveConfig();
        LOG_RAW("Admin password set: %s\n\r", cmd + 13);
    }
    else if (strncmp(cmd, "passwd guest ", 13) == 0) {
        sessionManager.setGuestPassword(cmd + 13);
        saveConfig();
        LOG_RAW("Guest password set: %s\n\r", cmd + 13);
    }
    else if (strcmp(cmd, "sleep on") == 0) {
        deepSleepEnabled = true;
        saveConfig();
        LOG_RAW("Deep sleep: ON\n\r");
    }
    else if (strcmp(cmd, "sleep off") == 0) {
        deepSleepEnabled = false;
        saveConfig();
        LOG_RAW("Deep sleep: OFF (serial always active)\n\r");
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
    else if (strcmp(cmd, "location clear") == 0) {
        nodeIdentity.clearLocation();
        nodeIdentity.save();
        LOG_RAW("Location cleared\n\r");
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
    else if (strcmp(cmd, "alert") == 0) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (alertDestPubKey[i] != 0) { keySet = true; break; }
        }
        LOG_RAW("Alert: %s  Dest: %s\n\r",
            alertEnabled ? "ON" : "OFF",
            keySet ? "set" : "not set");
        if (keySet) {
            LOG_RAW("  Hash: %02X\n\r", alertDestPubKey[0]);
        }
    }
    else if (strcmp(cmd, "alert on") == 0) {
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (alertDestPubKey[i] != 0) { keySet = true; break; }
        }
        if (keySet) {
            alertEnabled = true;
            saveConfig();
            LOG_RAW("Node alert: ON\n\r");
        } else {
            LOG_RAW("Set destination first: alert dest <pubkey>\n\r");
        }
    }
    else if (strcmp(cmd, "alert off") == 0) {
        alertEnabled = false;
        saveConfig();
        LOG_RAW("Node alert: OFF\n\r");
    }
    else if (strncmp(cmd, "alert dest ", 11) == 0) {
        const char* arg = cmd + 11;
        // Try to find contact by name first
        Contact* c = contactMgr.findByName(arg);
        if (c) {
            memcpy(alertDestPubKey, c->pubKey, REPORT_PUBKEY_SIZE);
            saveConfig();
            LOG_RAW("Alert dest: %s (%02X)\n\r", c->name, alertDestPubKey[0]);
        }
        // Otherwise try hex pubkey (64 chars = 32 bytes)
        else if (strlen(arg) >= 64) {
            for (int i = 0; i < 32; i++) {
                char byte[3] = {arg[i*2], arg[i*2+1], 0};
                alertDestPubKey[i] = strtoul(byte, NULL, 16);
            }
            saveConfig();
            LOG_RAW("Alert dest set: %02X%02X%02X%02X...\n\r",
                alertDestPubKey[0], alertDestPubKey[1],
                alertDestPubKey[2], alertDestPubKey[3]);
        } else {
            LOG_RAW("Contact '%s' not found\n\r", arg);
        }
    }
    else if (strcmp(cmd, "alert clear") == 0) {
        memset(alertDestPubKey, 0, REPORT_PUBKEY_SIZE);
        alertEnabled = false;
        saveConfig();
        LOG_RAW("Alert destination cleared\n\r");
    }
    else if (strcmp(cmd, "alert test") == 0) {
        if (sendNodeAlert("TestNode", 0xAA, 1, -50)) {
            LOG_RAW("Test alert sent\n\r");
        } else {
            LOG_RAW("Alert not configured or time not synced\n\r");
        }
    }
    else if (strcmp(cmd, "reset") == 0) {
        resetConfig();
        applyPowerSettings();
        LOG_RAW("Config reset to defaults\n\r");
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
#else
// Full command handler with ANSI formatting
void processCommand(char* cmd) {
    if (strcmp(cmd, "status") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "        SYSTEM STATUS          " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Firmware    " ANSI_GREEN "v%-16s" ANSI_RESET ANSI_CYAN "│\n\r", FIRMWARE_VERSION);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Node ID     " ANSI_YELLOW "%-16lX" ANSI_RESET ANSI_CYAN "│\n\r", nodeId);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Identity    " ANSI_YELLOW "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", nodeIdentity.getNodeName());
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        char freqStr[18]; snprintf(freqStr, sizeof(freqStr), "%.3f MHz", MC_FREQUENCY);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Frequency   " ANSI_CYAN "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", freqStr);
        char bwStr[18]; snprintf(bwStr, sizeof(bwStr), "%.1f kHz", MC_BANDWIDTH);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Bandwidth   " ANSI_CYAN "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", bwStr);
        char sfStr[18]; snprintf(sfStr, sizeof(sfStr), "SF%d", MC_SPREADING);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Spreading   " ANSI_CYAN "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", sfStr);
        char crStr[18]; snprintf(crStr, sizeof(crStr), "4/%d", MC_CODING_RATE);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Coding Rate " ANSI_CYAN "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", crStr);
        char pwrStr[18]; snprintf(pwrStr, sizeof(pwrStr), "%d dBm", MC_TX_POWER);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " TX Power    " ANSI_CYAN "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", pwrStr);
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "stats") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "          STATISTICS           " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│ " ANSI_GREEN "RX " ANSI_WHITE " Received    %-14lu" ANSI_RESET ANSI_CYAN "│\n\r", rxCount);
        LOG_RAW(ANSI_CYAN "│ " ANSI_MAGENTA "TX " ANSI_WHITE " Transmitted %-14lu" ANSI_RESET ANSI_CYAN "│\n\r", txCount);
        LOG_RAW(ANSI_CYAN "│ " ANSI_BLUE "FWD" ANSI_WHITE " Forwarded   %-14lu" ANSI_RESET ANSI_CYAN "│\n\r", fwdCount);
        LOG_RAW(ANSI_CYAN "│ " ANSI_RED "ERR" ANSI_WHITE " Errors      %-14lu" ANSI_RESET ANSI_CYAN "│\n\r", errCount);
        LOG_RAW(ANSI_CYAN "│ " ANSI_RED "CRC" ANSI_WHITE " CRC Errors  %-14lu" ANSI_RESET ANSI_CYAN "│\n\r", crcErrCount);
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│ " ANSI_YELLOW "ADV" ANSI_WHITE " TX: %-6lu RX: %-6lu  " ANSI_RESET ANSI_CYAN "│\n\r", advTxCount, advRxCount);
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Queue        " ANSI_CYAN "%d / %d            " ANSI_RESET ANSI_CYAN "│\n\r", txQueue.getCount(), MC_TX_QUEUE_SIZE);
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "rssi") == 0) {
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " RSSI: " ANSI_GREEN "%4d" ANSI_WHITE " dBm  SNR: " ANSI_GREEN "%2d.%02d" ANSI_WHITE " dB  " ANSI_RESET ANSI_CYAN "│\n\r",
            lastRssi, lastSnr / 4, abs(lastSnr % 4) * 25);
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "power") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "        POWER SETTINGS         " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        const char* modeStr = powerSaveMode == 0 ? "Performance" : powerSaveMode == 1 ? "Balanced" : "PowerSave";
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Mode        " ANSI_YELLOW "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", modeStr);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " RX Boost    %s%-16s" ANSI_RESET ANSI_CYAN "│\n\r",
            rxBoostEnabled ? ANSI_GREEN : ANSI_RED, rxBoostEnabled ? "ON" : "OFF");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Deep Sleep  %s%-16s" ANSI_RESET ANSI_CYAN "│\n\r",
            deepSleepEnabled ? ANSI_GREEN : ANSI_RED, deepSleepEnabled ? "ON" : "OFF");
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "rxboost on") == 0) {
        rxBoostEnabled = true;
        applyPowerSettings();
        saveConfig();
        LOG(TAG_OK " RX Boost enabled\n\r");
    }
    else if (strcmp(cmd, "rxboost off") == 0) {
        rxBoostEnabled = false;
        applyPowerSettings();
        saveConfig();
        LOG(TAG_OK " RX Boost disabled\n\r");
    }
    else if (strcmp(cmd, "deepsleep on") == 0) {
        deepSleepEnabled = true;
        saveConfig();
        LOG(TAG_OK " Deep sleep enabled\n\r");
    }
    else if (strcmp(cmd, "deepsleep off") == 0) {
        deepSleepEnabled = false;
        saveConfig();
        LOG(TAG_OK " Deep sleep disabled\n\r");
    }
    else if (strcmp(cmd, "nodetype chat") == 0) {
        // Change node type to CHAT (for testing visibility in MeshCore app)
        uint8_t flags = nodeIdentity.getFlags();
        flags = (flags & 0xF0) | MC_TYPE_CHAT_NODE;  // Keep upper flags, change type
        nodeIdentity.setFlags(flags);
        LOG(TAG_OK " Node type changed to CHAT (0x%02X) - send 'advert' to announce\n\r", flags);
    }
    else if (strcmp(cmd, "nodetype repeater") == 0) {
        // Change node type back to REPEATER
        uint8_t flags = nodeIdentity.getFlags();
        flags = (flags & 0xF0) | MC_TYPE_REPEATER;  // Keep upper flags, change type
        nodeIdentity.setFlags(flags);
        LOG(TAG_OK " Node type changed to REPEATER (0x%02X) - send 'advert' to announce\n\r", flags);
    }
    else if (strcmp(cmd, "mode 0") == 0 || strcmp(cmd, "mode perf") == 0) {
        powerSaveMode = 0;
        saveConfig();
        LOG(TAG_OK " Power mode: Performance\n\r");
    }
    else if (strcmp(cmd, "mode 1") == 0 || strcmp(cmd, "mode bal") == 0) {
        powerSaveMode = 1;
        saveConfig();
        LOG(TAG_OK " Power mode: Balanced\n\r");
    }
    else if (strcmp(cmd, "mode 2") == 0 || strcmp(cmd, "mode save") == 0) {
        powerSaveMode = 2;
        saveConfig();
        LOG(TAG_OK " Power mode: PowerSave\n\r");
    }
    else if (strcmp(cmd, "save") == 0) {
        saveConfig();
    }
    else if (strcmp(cmd, "reset") == 0) {
        resetConfig();
        applyPowerSettings();
    }
    else if (strcmp(cmd, "newid") == 0) {
        LOG(TAG_WARN " Generating new identity with orlp crypto...\n\r");
        nodeIdentity.reset();
        LOG(TAG_OK " New identity: %s (hash: %02X)\n\r", nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
        LOG(TAG_INFO " Reboot to apply changes\n\r");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        LOG(TAG_INFO " Rebooting...\n\r");
        delay(100);
        #ifdef CUBECELL
        NVIC_SystemReset();
        #endif
    }
    else if (strcmp(cmd, "ping") == 0) {
        sendPing();
    }
    else if (strcmp(cmd, "advert compat on") == 0) {
        // Enable MeshCore 1.11.0 compatibility mode (no flags byte)
        advertGen.setCompatMode(true);
        LOG(TAG_OK " ADVERT compat mode: ON (no flags byte)\n\r");
    }
    else if (strcmp(cmd, "advert compat off") == 0) {
        // Disable compatibility mode (standard format with flags)
        advertGen.setCompatMode(false);
        LOG(TAG_OK " ADVERT compat mode: OFF (standard format)\n\r");
    }
    else if (strcmp(cmd, "advert compat") == 0) {
        // Show current compat mode
        LOG(TAG_INFO " ADVERT compat mode: %s\n\r", advertGen.isCompatMode() ? "ON" : "OFF");
    }
    else if (strcmp(cmd, "advert") == 0) {
        sendAdvert(true);  // Send flood ADVERT
    }
    else if (strcmp(cmd, "advert local") == 0) {
        sendAdvert(false);  // Send zero-hop ADVERT
    }
    else if (strncmp(cmd, "advert interval ", 16) == 0) {
        // Parse "advert interval SECONDS" command
        uint32_t interval = strtoul(cmd + 16, NULL, 10);
        if (interval >= 60 && interval <= 86400) {  // 1 min to 24 hours
            advertGen.setInterval(interval * 1000);
            LOG(TAG_OK " ADVERT interval set: %lus\n\r", interval);
        } else {
            LOG(TAG_ERROR " Invalid interval (60-86400 seconds)\n\r");
        }
    }
    else if (strcmp(cmd, "advert interval") == 0) {
        LOG(TAG_INFO " ADVERT interval: %lus\n\r", advertGen.getInterval() / 1000);
        LOG(TAG_INFO " Next in: %lus\n\r", advertGen.getTimeUntilNext());
    }
    else if (strcmp(cmd, "advert debug") == 0) {
        // Build ADVERT and show raw bytes for debugging
        MCPacket pkt;
        if (advertGen.buildFlood(&pkt)) {
            LOG(TAG_ADVERT " Debug - ADVERT packet (%d bytes):\n\r", pkt.payloadLen);
            LOG_RAW("  Header: 0x%02X (Route=%s Type=%s)\n\r",
                pkt.header.raw,
                mcRouteTypeName(pkt.header.getRouteType()),
                mcPayloadTypeName(pkt.header.getPayloadType()));
            LOG_RAW("  PathLen: %d  PayloadLen: %d\n\r", pkt.pathLen, pkt.payloadLen);
            LOG_RAW("  PubKey[0-7]: ");
            for (int i = 0; i < 8; i++) LOG_RAW("%02X", pkt.payload[i]);
            LOG_RAW("...\n\r");
            uint32_t ts = pkt.payload[32] | (pkt.payload[33] << 8) |
                         (pkt.payload[34] << 16) | (pkt.payload[35] << 24);
            LOG_RAW("  Timestamp: %lu (bytes: %02X %02X %02X %02X)\n\r",
                ts, pkt.payload[32], pkt.payload[33], pkt.payload[34], pkt.payload[35]);
            LOG_RAW("  Signature[0-7]: ");
            for (int i = 36; i < 44; i++) LOG_RAW("%02X", pkt.payload[i]);
            LOG_RAW("...\n\r");
            LOG_RAW("  Flags: 0x%02X\n\r", pkt.payload[100]);
            if (pkt.payload[100] & 0x10) {  // Has location
                int32_t lat = pkt.payload[101] | (pkt.payload[102] << 8) |
                             (pkt.payload[103] << 16) | (pkt.payload[104] << 24);
                int32_t lon = pkt.payload[105] | (pkt.payload[106] << 8) |
                             (pkt.payload[107] << 16) | (pkt.payload[108] << 24);
                LOG_RAW("  Location: %.6f, %.6f\n\r", lat / 1000000.0f, lon / 1000000.0f);
            }
            if (pkt.payload[100] & 0x80) {  // Has name
                uint8_t nameOffset = (pkt.payload[100] & 0x10) ? 109 : 101;
                LOG_RAW("  Name: ");
                for (int i = nameOffset; i < pkt.payloadLen; i++) {
                    LOG_RAW("%c", pkt.payload[i]);
                }
                LOG_RAW("\n\r");
            }
            LOG_RAW("  Raw payload: ");
            for (int i = 0; i < min((int)pkt.payloadLen, 32); i++) {
                LOG_RAW("%02X", pkt.payload[i]);
            }
            if (pkt.payloadLen > 32) LOG_RAW("...");
            LOG_RAW("\n\r");
        } else {
            LOG(TAG_ERROR " Failed to build ADVERT\n\r");
        }
    }
    else if (strcmp(cmd, "identity") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "        NODE IDENTITY          " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Name        " ANSI_YELLOW "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", nodeIdentity.getNodeName());
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Hash        " ANSI_YELLOW "0x%02X            " ANSI_RESET ANSI_CYAN "│\n\r", nodeIdentity.getNodeHash());
        const uint8_t* pk = nodeIdentity.getPublicKey();
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " PubKey      " ANSI_DIM "%02X%02X%02X%02X..      " ANSI_RESET ANSI_CYAN "│\n\r",
                 pk[0], pk[1], pk[2], pk[3]);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Flags       " ANSI_CYAN "0x%02X            " ANSI_RESET ANSI_CYAN "│\n\r", nodeIdentity.getFlags());
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        if (nodeIdentity.hasLocation()) {
            char locStr[20];
            snprintf(locStr, sizeof(locStr), "%.4f,%.4f",
                     nodeIdentity.getLatitudeFloat(), nodeIdentity.getLongitudeFloat());
            LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Location    " ANSI_GREEN "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", locStr);
        } else {
            LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Location    " ANSI_RED "not set         " ANSI_RESET ANSI_CYAN "│\n\r");
        }
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " ADVERT Int  " ANSI_GREEN "%-14lus  " ANSI_RESET ANSI_CYAN "│\n\r", advertGen.getInterval() / 1000);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Next in     " ANSI_YELLOW "%-14lus  " ANSI_RESET ANSI_CYAN "│\n\r", advertGen.getTimeUntilNext());
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        if (timeSync.isSynchronized()) {
            LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Time        " ANSI_GREEN "synced          " ANSI_RESET ANSI_CYAN "│\n\r");
        } else if (timeSync.hasPendingSync()) {
            LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Time        " ANSI_YELLOW "pending (1/2)   " ANSI_RESET ANSI_CYAN "│\n\r");
        } else {
            LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Time        " ANSI_RED "not synced      " ANSI_RESET ANSI_CYAN "│\n\r");
        }
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "identity reset") == 0) {
        LOG(TAG_WARN " Generating new identity...\n\r");
        nodeIdentity.reset();
        LOG(TAG_OK " New identity created: %s\n\r", nodeIdentity.getNodeName());
    }
    else if (strncmp(cmd, "location ", 9) == 0) {
        // Parse "location LAT LON" command using atof (more compatible)
        char* args = cmd + 9;
        char* space = strchr(args, ' ');
        if (space != NULL) {
            *space = '\0';  // Split at space
            float lat = atof(args);
            float lon = atof(space + 1);
            if ((lat != 0.0f || args[0] == '0') && (lon != 0.0f || space[1] == '0')) {
                if (lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f) {
                    nodeIdentity.setLocation(lat, lon);
                    nodeIdentity.save();
                    LOG(TAG_OK " Location set: %.6f, %.6f\n\r", lat, lon);
                    LOG(TAG_INFO " Send 'advert' to broadcast new location\n\r");
                } else {
                    LOG(TAG_ERROR " Invalid range (lat: -90~90, lon: -180~180)\n\r");
                }
            } else {
                LOG(TAG_ERROR " Could not parse coordinates\n\r");
            }
        } else {
            LOG(TAG_ERROR " Usage: location LAT LON\n\r");
            LOG(TAG_INFO " Example: location 45.464 9.191\n\r");
        }
    }
    else if (strcmp(cmd, "location clear") == 0) {
        nodeIdentity.clearLocation();
        nodeIdentity.save();
        LOG(TAG_OK " Location cleared\n\r");
    }
    else if (strcmp(cmd, "location") == 0) {
        if (nodeIdentity.hasLocation()) {
            LOG(TAG_INFO " Location: %.6f, %.6f\n\r",
                nodeIdentity.getLatitudeFloat(), nodeIdentity.getLongitudeFloat());
        } else {
            LOG(TAG_INFO " Location: not set\n\r");
            LOG(TAG_INFO " Use: location LAT LON (e.g., location 45.464 9.191)\n\r");
        }
    }
    else if (strncmp(cmd, "name ", 5) == 0) {
        // Parse "name NEWNAME" command
        const char* newName = cmd + 5;
        if (strlen(newName) > 0 && strlen(newName) < 16) {
            nodeIdentity.setNodeName(newName);
            nodeIdentity.save();
            LOG(TAG_OK " Name set: %s\n\r", nodeIdentity.getNodeName());
        } else {
            LOG(TAG_ERROR " Name must be 1-15 characters\n\r");
        }
    }
    else if (strncmp(cmd, "time ", 5) == 0) {
        // Parse "time TIMESTAMP" command - set Unix timestamp manually
        uint32_t ts = strtoul(cmd + 5, NULL, 10);
        if (ts > 1577836800 && ts < 4102444800UL) {  // 2020-2100
            timeSync.setTime(ts);
            LOG(TAG_OK " Time set: %lu\n\r", ts);
            // Schedule ADVERT with new time
            pendingAdvertTime = millis() + ADVERT_AFTER_SYNC_MS;
            LOG(TAG_INFO " Will send ADVERT in %d seconds\n\r", ADVERT_AFTER_SYNC_MS / 1000);
        } else {
            LOG(TAG_ERROR " Invalid timestamp (must be 2020-2100)\n\r");
        }
    }
    else if (strcmp(cmd, "time") == 0) {
        // Show current time
        if (timeSync.isSynchronized()) {
            uint32_t ts = timeSync.getTimestamp();
            LOG(TAG_INFO " Time: %lu (synced)\n\r", ts);
        } else if (timeSync.hasPendingSync()) {
            LOG(TAG_INFO " Time: not synced (pending: %lu)\n\r", timeSync.getPendingTimestamp());
        } else {
            LOG(TAG_INFO " Time: not synced\n\r");
            LOG(TAG_INFO " Use: time <unix_timestamp> (e.g., time 1737312000)\n\r");
        }
    }
    else if (strcmp(cmd, "telemetry") == 0) {
        telemetry.update();
        const TelemetryData* t = telemetry.getData();
        char uptimeStr[32];
        telemetry.formatUptime(uptimeStr, sizeof(uptimeStr));

        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "          TELEMETRY            " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        // Battery with color based on level
        uint8_t batPct = telemetry.getBatteryPercent();
        const char* batColor = batPct > 50 ? ANSI_GREEN : batPct > 20 ? ANSI_YELLOW : ANSI_RED;
        char batStr[20]; snprintf(batStr, sizeof(batStr), "%dmV (%d%%)", t->batteryMv, batPct);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Battery     %s%-16s" ANSI_RESET ANSI_CYAN "│\n\r", batColor, batStr);
        char tempStr[16]; snprintf(tempStr, sizeof(tempStr), "%dC", t->temperature);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Temperature " ANSI_CYAN "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", tempStr);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Uptime      " ANSI_WHITE "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", uptimeStr);
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│ " ANSI_GREEN "RX " ANSI_WHITE "%-8lu " ANSI_MAGENTA "TX " ANSI_WHITE "%-8lu   " ANSI_RESET ANSI_CYAN "│\n\r", t->rxCount, t->txCount);
        LOG_RAW(ANSI_CYAN "│ " ANSI_BLUE "FWD" ANSI_WHITE " %-8lu " ANSI_RED "ERR" ANSI_WHITE " %-8lu   " ANSI_RESET ANSI_CYAN "│\n\r", t->fwdCount, t->errorCount);
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        char rssiStr[24]; snprintf(rssiStr, sizeof(rssiStr), "%ddBm / %d.%02ddB",
            t->lastRssi, t->lastSnr / 4, abs(t->lastSnr % 4) * 25);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " RSSI/SNR    " ANSI_GREEN "%-16s" ANSI_RESET ANSI_CYAN "│\n\r", rssiStr);
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "nodes") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE " SEEN NODES: %-2d                                        " ANSI_RESET ANSI_CYAN "│\n\r", seenNodes.getCount());
        LOG_RAW(ANSI_CYAN "├──────┬────────────┬─────────┬─────────┬──────┬────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Hash " ANSI_CYAN "│" ANSI_WHITE "   Name     " ANSI_CYAN "│" ANSI_WHITE "  RSSI   " ANSI_CYAN "│" ANSI_WHITE "   SNR   " ANSI_CYAN "│" ANSI_WHITE " Pkts " ANSI_CYAN "│" ANSI_WHITE "   Ago  " ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├──────┼────────────┼─────────┼─────────┼──────┼────────┤" ANSI_RESET "\n\r");
        if (seenNodes.getCount() == 0) {
            LOG_RAW(ANSI_CYAN "│" ANSI_DIM "              No nodes detected yet                    " ANSI_RESET ANSI_CYAN "│\n\r");
        } else {
            for (uint8_t i = 0; i < seenNodes.getCount(); i++) {
                const SeenNode* n = seenNodes.getNode(i);
                if (n) {
                    uint32_t ago = (millis() - n->lastSeen) / 1000;
                    // Display name if available, otherwise show hash-based ID
                    char nameStr[12];
                    if (n->name[0] != '\0') {
                        strncpy(nameStr, n->name, sizeof(nameStr) - 1);
                        nameStr[sizeof(nameStr) - 1] = '\0';
                    } else {
                        snprintf(nameStr, sizeof(nameStr), "Node-%02X", n->hash);
                    }
                    LOG_RAW(ANSI_CYAN "│" ANSI_YELLOW "  %02X  " ANSI_RESET ANSI_CYAN "│" ANSI_GREEN " %-10s " ANSI_RESET ANSI_CYAN "│" ANSI_WHITE " %4ddBm " ANSI_RESET ANSI_CYAN "│" ANSI_WHITE " %2d.%02ddB " ANSI_RESET ANSI_CYAN "│" ANSI_WHITE " %4d " ANSI_RESET ANSI_CYAN "│" ANSI_WHITE " %5lus " ANSI_RESET ANSI_CYAN "│\n\r",
                        n->hash, nameStr, n->lastRssi,
                        n->lastSnr / 4, abs(n->lastSnr % 4) * 25,
                        n->pktCount, ago);
                }
            }
        }
        LOG_RAW(ANSI_CYAN "└──────┴────────────┴─────────┴─────────┴──────┴────────┘" ANSI_RESET "\n\r");
    }
    // ==================== REPEATER COMMANDS ====================
    else if (strcmp(cmd, "neighbours") == 0 || strcmp(cmd, "neighbors") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "          NEIGHBOUR REPEATERS          " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├──────────────┬─────────┬─────────┬─────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE "  PubKey     " ANSI_CYAN "│" ANSI_WHITE "  RSSI   " ANSI_CYAN "│" ANSI_WHITE "   SNR   " ANSI_CYAN "│" ANSI_WHITE " Age " ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├──────────────┼─────────┼─────────┼─────┤" ANSI_RESET "\n\r");

        uint8_t cnt = repeaterHelper.getNeighbours().getCount();
        if (cnt == 0) {
            LOG_RAW(ANSI_CYAN "│" ANSI_DIM "      No neighbours detected          " ANSI_RESET ANSI_CYAN "│\n\r");
        } else {
            for (uint8_t i = 0; i < cnt && i < 20; i++) {
                const NeighbourInfo* n = repeaterHelper.getNeighbours().getNeighbour(i);
                if (n) {
                    uint32_t ago = (millis() - n->lastHeard) / 1000;
                    LOG_RAW(ANSI_CYAN "│" ANSI_YELLOW " %02X%02X%02X%02X%02X%02X " ANSI_RESET ANSI_CYAN "│" ANSI_WHITE " %4ddBm " ANSI_RESET ANSI_CYAN "│" ANSI_WHITE " %2d.%02ddB " ANSI_RESET ANSI_CYAN "│" ANSI_WHITE "%4lus" ANSI_RESET ANSI_CYAN "│\n\r",
                        n->pubKeyPrefix[0], n->pubKeyPrefix[1], n->pubKeyPrefix[2],
                        n->pubKeyPrefix[3], n->pubKeyPrefix[4], n->pubKeyPrefix[5],
                        n->rssi, n->snr / 4, abs(n->snr % 4) * 25, ago);
                }
            }
        }
        LOG_RAW(ANSI_CYAN "└──────────────┴─────────┴─────────┴─────┘" ANSI_RESET "\n\r");
    }
    else if (strncmp(cmd, "set password ", 13) == 0) {
        const char* pwd = cmd + 13;
        if (strlen(pwd) > 0 && strlen(pwd) <= 15) {
            sessionManager.setAdminPassword(pwd);
            saveConfig();  // Persist to EEPROM
            LOG(TAG_OK " Admin password set and saved\n\r");
        } else {
            LOG(TAG_ERROR " Password must be 1-15 characters\n\r");
        }
    }
    else if (strncmp(cmd, "set guest ", 10) == 0) {
        const char* pwd = cmd + 10;
        if (strlen(pwd) <= 15) {
            sessionManager.setGuestPassword(pwd);
            saveConfig();  // Persist to EEPROM
            LOG(TAG_OK " Guest password set and saved (empty = no guest access)\n\r");
        } else {
            LOG(TAG_ERROR " Password must be 0-15 characters\n\r");
        }
    }
    else if (strcmp(cmd, "set repeat on") == 0) {
        repeaterHelper.setRepeatEnabled(true);
        LOG(TAG_OK " Packet repeating enabled\n\r");
    }
    else if (strcmp(cmd, "set repeat off") == 0) {
        repeaterHelper.setRepeatEnabled(false);
        LOG(TAG_OK " Packet repeating disabled\n\r");
    }
    else if (strncmp(cmd, "set flood.max ", 14) == 0) {
        uint8_t hops = atoi(cmd + 14);
        if (hops >= 1 && hops <= 15) {
            repeaterHelper.setMaxFloodHops(hops);
            LOG(TAG_OK " Max flood hops set: %d\n\r", hops);
        } else {
            LOG(TAG_ERROR " Hops must be 1-15\n\r");
        }
    }
    else if (strcmp(cmd, "log on") == 0) {
        packetLogger.setEnabled(true);
        LOG(TAG_OK " Packet logging enabled\n\r");
    }
    else if (strcmp(cmd, "log off") == 0) {
        packetLogger.setEnabled(false);
        LOG(TAG_OK " Packet logging disabled\n\r");
    }
    else if (strcmp(cmd, "log dump") == 0) {
        packetLogger.dump();
    }
    else if (strcmp(cmd, "log clear") == 0) {
        packetLogger.clear();
        LOG(TAG_OK " Packet log cleared\n\r");
    }
    else if (strcmp(cmd, "radiostats") == 0) {
        const RadioStats& rs = repeaterHelper.getRadioStats();
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "        RADIO STATISTICS        " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Last RSSI   " ANSI_GREEN "%4d" ANSI_WHITE " dBm          " ANSI_RESET ANSI_CYAN "│\n\r", rs.lastRssi);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Last SNR    " ANSI_GREEN "%2d.%02d" ANSI_WHITE " dB          " ANSI_RESET ANSI_CYAN "│\n\r", rs.lastSnr / 4, abs(rs.lastSnr % 4) * 25);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " TX Airtime  " ANSI_YELLOW "%lu" ANSI_WHITE " sec           " ANSI_RESET ANSI_CYAN "│\n\r", rs.txAirTimeSec);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " RX Airtime  " ANSI_YELLOW "%lu" ANSI_WHITE " sec           " ANSI_RESET ANSI_CYAN "│\n\r", rs.rxAirTimeSec);
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "packetstats") == 0) {
        const PacketStats& ps = repeaterHelper.getPacketStats();
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "        PACKET STATISTICS       " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Total RX    " ANSI_GREEN "%-14lu  " ANSI_RESET ANSI_CYAN "│\n\r", ps.numRecvPackets);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Total TX    " ANSI_MAGENTA "%-14lu  " ANSI_RESET ANSI_CYAN "│\n\r", ps.numSentPackets);
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " RX Flood    " ANSI_YELLOW "%-14lu  " ANSI_RESET ANSI_CYAN "│\n\r", ps.numRecvFlood);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " RX Direct   " ANSI_YELLOW "%-14lu  " ANSI_RESET ANSI_CYAN "│\n\r", ps.numRecvDirect);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " TX Flood    " ANSI_YELLOW "%-14lu  " ANSI_RESET ANSI_CYAN "│\n\r", ps.numSentFlood);
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " TX Direct   " ANSI_YELLOW "%-14lu  " ANSI_RESET ANSI_CYAN "│\n\r", ps.numSentDirect);
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "acl") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "          ACCESS CONTROL LIST          " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Admin pwd: " ANSI_YELLOW "%-24s  " ANSI_RESET ANSI_CYAN "│\n\r", sessionManager.getAdminPassword());
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Guest pwd: " ANSI_YELLOW "%-24s  " ANSI_RESET ANSI_CYAN "│\n\r",
            strlen(sessionManager.getGuestPassword()) > 0 ? sessionManager.getGuestPassword() : "(disabled)");
        LOG_RAW(ANSI_CYAN "├──────────────┬─────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE "  PubKey     " ANSI_CYAN "│" ANSI_WHITE "      Permission        " ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├──────────────┼─────────────────────────┤" ANSI_RESET "\n\r");

        uint8_t cnt = sessionManager.getSessionCount();
        if (cnt == 0) {
            LOG_RAW(ANSI_CYAN "│" ANSI_DIM "        No active sessions             " ANSI_RESET ANSI_CYAN "│\n\r");
        } else {
            for (uint8_t i = 0; i < MAX_CLIENT_SESSIONS; i++) {
                const ClientSession* s = sessionManager.getSession(i);
                if (s) {
                    const char* permStr = s->permissions == PERM_ACL_ADMIN ? "ADMIN" :
                                         s->permissions == PERM_ACL_GUEST ? "GUEST" : "NONE";
                    LOG_RAW(ANSI_CYAN "│" ANSI_YELLOW " %02X%02X%02X%02X%02X%02X " ANSI_RESET ANSI_CYAN "│" ANSI_WHITE " %-23s " ANSI_RESET ANSI_CYAN "│\n\r",
                        s->pubKey[0], s->pubKey[1], s->pubKey[2],
                        s->pubKey[3], s->pubKey[4], s->pubKey[5], permStr);
                }
            }
        }
        LOG_RAW(ANSI_CYAN "└──────────────┴─────────────────────────┘" ANSI_RESET "\n\r");
    }
    else if (strcmp(cmd, "repeat") == 0) {
        LOG(TAG_INFO " Repeat: %s, Max hops: %d\n\r",
            repeaterHelper.isRepeatEnabled() ? "ON" : "OFF",
            repeaterHelper.getMaxFloodHops());
    }
#ifndef LITE_MODE
    // Daily report commands
    else if (strcmp(cmd, "report") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "        DAILY REPORT           " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Status      %s%-16s" ANSI_RESET ANSI_CYAN "│\n\r",
            reportEnabled ? ANSI_GREEN : ANSI_RED, reportEnabled ? "ENABLED" : "DISABLED");
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Time        " ANSI_YELLOW "%02d:%02d            " ANSI_RESET ANSI_CYAN "│\n\r",
            reportHour, reportMinute);
        // Check if destination key is set
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keySet = true; break; }
        }
        LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Dest key    %s%-16s" ANSI_RESET ANSI_CYAN "│\n\r",
            keySet ? ANSI_GREEN : ANSI_RED, keySet ? "SET" : "NOT SET");
        if (keySet) {
            LOG_RAW(ANSI_CYAN "│" ANSI_WHITE " Key hash    " ANSI_YELLOW "0x%02X             " ANSI_RESET ANSI_CYAN "│\n\r",
                reportDestPubKey[0]);
        }
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
        if (!keySet) {
            LOG(TAG_INFO " Login as admin from app to set destination key\n\r");
        }
    }
    else if (strcmp(cmd, "report on") == 0) {
        // Check if destination key is set
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keySet = true; break; }
        }
        if (!keySet) {
            LOG(TAG_ERROR " Cannot enable: no destination key set\n\r");
            LOG(TAG_INFO " Login as admin from app first\n\r");
        } else {
            reportEnabled = true;
            saveConfig();
            LOG(TAG_OK " Daily report enabled (sends at %02d:%02d)\n\r", reportHour, reportMinute);
        }
    }
    else if (strcmp(cmd, "report off") == 0) {
        reportEnabled = false;
        saveConfig();
        LOG(TAG_OK " Daily report disabled\n\r");
    }
    else if (strcmp(cmd, "report clear") == 0) {
        reportEnabled = false;
        memset(reportDestPubKey, 0, REPORT_PUBKEY_SIZE);
        saveConfig();
        LOG(TAG_OK " Daily report destination cleared\n\r");
    }
    else if (strcmp(cmd, "report test") == 0) {
        // Check if destination key is set
        bool keySet = false;
        for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
            if (reportDestPubKey[i] != 0) { keySet = true; break; }
        }
        if (!keySet) {
            LOG(TAG_ERROR " Cannot send: no destination key set\n\r");
        } else {
            LOG(TAG_INFO " Sending test report...\n\r");
            extern bool sendDailyReport();
            if (sendDailyReport()) {
                LOG(TAG_OK " Test report queued for transmission\n\r");
            } else {
                LOG(TAG_ERROR " Failed to send test report\n\r");
            }
        }
    }
    else if (strncmp(cmd, "report time ", 12) == 0) {
        // Parse HH:MM format
        int h, m;
        if (sscanf(cmd + 12, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            reportHour = h;
            reportMinute = m;
            saveConfig();
            LOG(TAG_OK " Report time set to %02d:%02d\n\r", reportHour, reportMinute);
        } else {
            LOG(TAG_ERROR " Invalid time format. Use: report time HH:MM\n\r");
        }
    }
#endif // LITE_MODE - end of daily report commands
    else if (strcmp(cmd, "help") == 0) {
        LOG_RAW("\n\r");
        LOG_RAW(ANSI_CYAN "┌────────────────────────────────┐\n\r");
        LOG_RAW(ANSI_CYAN "│" ANSI_BOLD ANSI_WHITE "      AVAILABLE COMMANDS       " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤" ANSI_RESET "\n\r");
        LOG_RAW(ANSI_CYAN "│ " ANSI_BOLD ANSI_WHITE "Information                  " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "status" ANSI_WHITE "     System info      " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "stats" ANSI_WHITE "      Packet stats     " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "rssi" ANSI_WHITE "       Signal quality   " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "nodes" ANSI_WHITE "      Seen nodes       " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "power" ANSI_WHITE "      Power settings   " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "identity" ANSI_WHITE "   Node identity    " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "telemetry" ANSI_WHITE "  Battery & stats  " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "location" ANSI_WHITE "   GPS location     " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "time" ANSI_WHITE "       Show/set time   " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "neighbours" ANSI_WHITE " Nearby repeaters" ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "radiostats" ANSI_WHITE " Radio stats    " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "packetstats" ANSI_WHITE " Pkt breakdown" ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "acl" ANSI_WHITE "        Access control " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "repeat" ANSI_WHITE "     Repeat status  " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_GREEN "report" ANSI_WHITE "     Daily report   " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│ " ANSI_BOLD ANSI_WHITE "Actions                      " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_YELLOW "ping" ANSI_WHITE "       Send test pkt   " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_YELLOW "advert" ANSI_WHITE "     ADVERT (flood)  " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_YELLOW "advert local" ANSI_WHITE " ADVERT (0hop)" ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_YELLOW "advert interval <s>" ANSI_WHITE "        " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_YELLOW "advert debug" ANSI_WHITE " Show raw pkt" ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│ " ANSI_BOLD ANSI_WHITE "Config (EEPROM)              " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "name <n>" ANSI_WHITE "   Set node name  " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "location <lat> <lon>" ANSI_WHITE "       " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "location clear" ANSI_WHITE " Remove GPS " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "rxboost on|off" ANSI_WHITE "             " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "deepsleep on|off" ANSI_WHITE "           " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "mode 0|1|2" ANSI_WHITE " Power mode   " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "identity reset" ANSI_WHITE " New keys  " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "set password <p>" ANSI_WHITE " Admin pw " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "set guest <p>" ANSI_WHITE " Guest pw   " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "set repeat on|off" ANSI_WHITE "          " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "set flood.max <n>" ANSI_WHITE " Hops    " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "log on|off|dump|clear" ANSI_WHITE "      " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "report on|off" ANSI_WHITE " Daily rpt  " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "report time HH:MM" ANSI_WHITE "          " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "report test" ANSI_WHITE " Send now    " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_MAGENTA "report clear" ANSI_WHITE " Clear dest " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "├────────────────────────────────┤\n\r");
        LOG_RAW(ANSI_CYAN "│ " ANSI_BOLD ANSI_WHITE "System                       " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_RED "save" ANSI_WHITE "       Save config      " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_RED "reset" ANSI_WHITE "      Factory reset    " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "│  " ANSI_RED "reboot" ANSI_WHITE "     Restart system   " ANSI_RESET ANSI_CYAN "│\n\r");
        LOG_RAW(ANSI_CYAN "└────────────────────────────────┘" ANSI_RESET "\n\r");
    }
    // Contact and messaging commands
#ifndef LITE_MODE
    else if (strcmp(cmd, "contacts") == 0) {
        contactMgr.printContacts();
    }
    else if (strncmp(cmd, "msg ", 4) == 0) {
        // Parse "msg <name> <message>" command
        // Find first space after "msg "
        char* nameStart = cmd + 4;
        char* msgStart = strchr(nameStart, ' ');
        if (msgStart) {
            *msgStart = '\0';  // Terminate name
            msgStart++;  // Start of message
            if (strlen(msgStart) > 0) {
                sendDirectMessage(nameStart, msgStart);
            } else {
                LOG(TAG_ERROR " Empty message\n\r");
            }
        } else {
            LOG(TAG_ERROR " Usage: msg <name> <message>\n\r");
        }
    }
#endif
    else if (strlen(cmd) > 0) {
        LOG(TAG_ERROR " Unknown command: %s\n\r", cmd);
    }
}
#endif // MINIMAL_DEBUG && LITE_MODE

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
        if (_rc > 0) len += _rc; \
    } while(0)

    // === Read-only commands (guest + admin) ===

    if (strcmp(cmd, "status") == 0) {
        RESP_APPEND("Firmware: v%s\n", FIRMWARE_VERSION);
        RESP_APPEND("Node: %s (0x%02X)\n", nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
        RESP_APPEND("Uptime: %lus\n", millis() / 1000);
        RESP_APPEND("Time: %s\n", timeSync.isSynchronized() ? "synced" : "not synced");
    }
    else if (strcmp(cmd, "stats") == 0) {
        RESP_APPEND("RX: %lu TX: %lu FWD: %lu ERR: %lu\n", rxCount, txCount, fwdCount, errCount);
        RESP_APPEND("ADV TX: %lu RX: %lu\n", advTxCount, advRxCount);
        RESP_APPEND("Queue: %d/%d\n", txQueue.getCount(), MC_TX_QUEUE_SIZE);
    }
    else if (strcmp(cmd, "time") == 0) {
        if (timeSync.isSynchronized()) {
            RESP_APPEND("Time: %lu (synced)\n", timeSync.getTimestamp());
        } else {
            RESP_APPEND("Time: not synchronized\n");
        }
    }
    else if (strcmp(cmd, "telemetry") == 0) {
        telemetry.update();
        RESP_APPEND("Battery: %dmV\n", telemetry.getBatteryMv());
        RESP_APPEND("Uptime: %lus\n", millis() / 1000);
    }
    else if (strcmp(cmd, "nodes") == 0) {
        uint8_t count = seenNodes.getCount();
        RESP_APPEND("Seen nodes: %d\n", count);
        for (uint8_t i = 0; i < count && len < maxLen - 32; i++) {
            const SeenNode* n = seenNodes.getNode(i);
            if (n && n->lastSeen > 0) {
                RESP_APPEND("%02X %ddBm %d.%ddB\n", n->hash, n->lastRssi, n->lastSnr/4, abs(n->lastSnr%4)*25);
            }
        }
    }
    else if (strcmp(cmd, "neighbours") == 0 || strcmp(cmd, "neighbors") == 0) {
        NeighbourTracker& nb = repeaterHelper.getNeighbours();
        uint8_t count = nb.getCount();
        RESP_APPEND("Neighbours: %d\n", count);
    }
    else if (strcmp(cmd, "repeat") == 0) {
        RESP_APPEND("Repeat: %s\n", repeaterHelper.isRepeatEnabled() ? "on" : "off");
        RESP_APPEND("Max hops: %d\n", repeaterHelper.getMaxFloodHops());
    }
    else if (strcmp(cmd, "identity") == 0) {
        RESP_APPEND("Name: %s\n", nodeIdentity.getNodeName());
        RESP_APPEND("Hash: 0x%02X\n", nodeIdentity.getNodeHash());
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
            RESP_APPEND("No location set\n");
        }
    }
    else if (strcmp(cmd, "advert interval") == 0) {
        RESP_APPEND("Interval: %lus\n", advertGen.getInterval() / 1000);
        RESP_APPEND("Next in: %lus\n", advertGen.getTimeUntilNext());
    }

    // === Admin-only commands ===

    else if (!isAdmin) {
        RESP_APPEND("Error: admin required\n");
        return len;
    }

    // From here, all commands require admin

    else if (strcmp(cmd, "set repeat on") == 0) {
        repeaterHelper.setRepeatEnabled(true);
        RESP_APPEND("OK: repeat on\n");
    }
    else if (strcmp(cmd, "set repeat off") == 0) {
        repeaterHelper.setRepeatEnabled(false);
        RESP_APPEND("OK: repeat off\n");
    }
    else if (strncmp(cmd, "set flood.max ", 14) == 0) {
        uint8_t hops = atoi(cmd + 14);
        if (hops >= 1 && hops <= 15) {
            repeaterHelper.setMaxFloodHops(hops);
            RESP_APPEND("OK: flood.max %d\n", hops);
        } else {
            RESP_APPEND("Error: 1-15\n");
        }
    }
    else if (strncmp(cmd, "set password ", 13) == 0) {
        const char* pwd = cmd + 13;
        if (strlen(pwd) > 0 && strlen(pwd) <= 15) {
            sessionManager.setAdminPassword(pwd);
            saveConfig();
            RESP_APPEND("OK: password set\n");
        } else {
            RESP_APPEND("Error: 1-15 chars\n");
        }
    }
    else if (strncmp(cmd, "set guest ", 10) == 0) {
        const char* pwd = cmd + 10;
        if (strlen(pwd) <= 15) {
            sessionManager.setGuestPassword(pwd);
            saveConfig();
            RESP_APPEND("OK: guest set\n");
        } else {
            RESP_APPEND("Error: 0-15 chars\n");
        }
    }
    else if (strncmp(cmd, "name ", 5) == 0) {
        const char* newName = cmd + 5;
        if (strlen(newName) > 0 && strlen(newName) < MC_NODE_NAME_MAX) {
            nodeIdentity.setNodeName(newName);
            RESP_APPEND("OK: name=%s\n", newName);
        } else {
            RESP_APPEND("Error: 1-15 chars\n");
        }
    }
    else if (strncmp(cmd, "location ", 9) == 0) {
        float lat, lon;
        if (sscanf(cmd + 9, "%f %f", &lat, &lon) == 2) {
            if (lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
                nodeIdentity.setLocation(lat, lon);
                RESP_APPEND("OK: %.6f,%.6f\n", lat, lon);
            } else {
                RESP_APPEND("Error: invalid coords\n");
            }
        } else {
            RESP_APPEND("Error: location LAT LON\n");
        }
    }
    else if (strcmp(cmd, "location clear") == 0) {
        nodeIdentity.clearLocation();
        RESP_APPEND("OK: location cleared\n");
    }
    else if (strncmp(cmd, "advert interval ", 16) == 0) {
        uint32_t interval = strtoul(cmd + 16, NULL, 10);
        if (interval >= 60 && interval <= 86400) {
            advertGen.setInterval(interval * 1000);
            RESP_APPEND("OK: interval %lus\n", interval);
        } else {
            RESP_APPEND("Error: 60-86400\n");
        }
    }
    else if (strcmp(cmd, "advert") == 0) {
        sendAdvert(true);
        RESP_APPEND("OK: advert sent\n");
    }
    else if (strcmp(cmd, "advert local") == 0) {
        sendAdvert(false);
        RESP_APPEND("OK: advert local sent\n");
    }
    else if (strcmp(cmd, "save") == 0) {
        saveConfig();
        RESP_APPEND("OK: config saved\n");
    }
    else if (strcmp(cmd, "reset") == 0) {
        resetConfig();
        RESP_APPEND("OK: config reset\n");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        RESP_APPEND("OK: rebooting...\n");
        // Schedule reboot after response is sent
        // (handled by caller)
    }
    else if (strcmp(cmd, "help") == 0) {
        RESP_APPEND("Commands: status stats time telemetry nodes neighbours repeat identity location\n");
        RESP_APPEND("Admin: set repeat/password/guest/flood.max, name, location, advert, save, reset, reboot\n");
    }
    else {
        RESP_APPEND("Error: unknown cmd\n");
    }

    #undef RESP_APPEND
    return len;
}

//=============================================================================
// Power Management
//=============================================================================
void applyPowerSettings() {
    radio.setRxBoostedGainMode(rxBoostEnabled, true);
    LOG(TAG_CONFIG " RxBoost=%s DeepSleep=%s Mode=%d\n\r",
        rxBoostEnabled ? "ON" : "OFF",
        deepSleepEnabled ? "ON" : "OFF",
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

    LOG(TAG_RADIO " Timing: preamble=%lums slot=%lums max_pkt=%lums\n\r",
        preambleTimeMsec, slotTimeMsec, maxPacketTimeMsec);
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
        LOG(TAG_WARN " Radio error threshold reached, resetting radio\n\r");
        radio.reset();
        delay(100);
        setupRadio();
        radioErrorCount = 0;
    }

    if (errCount >= MC_MAX_TOTAL_ERRORS) {
        LOG(TAG_FATAL " Error threshold exceeded, rebooting system\n\r");
        delay(100);
        #ifdef CUBECELL
        NVIC_SystemReset();
        #endif
    }
}

//=============================================================================
// Radio Functions
//=============================================================================
void setupRadio() {
    LOG(TAG_RADIO " Initializing SX1262\n\r");

    radioError = radio.begin(
        MC_FREQUENCY,
        MC_BANDWIDTH,
        MC_SPREADING,
        MC_CODING_RATE,
        MC_SYNCWORD,
        MC_TX_POWER,
        MC_PREAMBLE_LEN
    );

    if (radioError != RADIOLIB_ERR_NONE) {
        LOG(TAG_FATAL " Radio initialization failed (code %d)\n\r", radioError);
        while (true) delay(1000);
    }

    // Explicitly enable CRC for MeshCore compatibility
    radioError = radio.setCRC(2);  // 2 = CRC-16
    if (radioError != RADIOLIB_ERR_NONE) {
        LOG(TAG_WARN " CRC configuration failed (code %d)\n\r", radioError);
    }

    // Set DIO1 interrupt
    radio.setDio1Action(onDio1Rise);

    // Apply power settings
    applyPowerSettings();

    LOG(TAG_RADIO " Ready: %.3f MHz  BW=%.1f kHz  SF%d  CR=4/%d  CRC=ON\n\r",
        MC_FREQUENCY, MC_BANDWIDTH, MC_SPREADING, MC_CODING_RATE);
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
        LOG(TAG_ERROR " RX start failed (code %d)\n\r", radioError);
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
        LOG(TAG_ERROR " Packet serialization failed\n\r");
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
        LOG(TAG_ERROR " TX failed (code %d)\n\r", radioError);
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

    LOG(TAG_PING " Sending test packet #%u\n\r", pingCounter);

    // Add to packet cache so we don't re-forward our own ping
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    // Transmit directly (no queue delay for ping)
    if (transmitPacket(&pkt)) {
        LOG(TAG_PING " Transmission successful\n\r");
    } else {
        LOG(TAG_PING " Transmission failed\n\r");
    }

    startReceive();
}

//=============================================================================
// ADVERT Beacon
//=============================================================================

/**
 * Send ADVERT without flags byte (to match buggy MeshCore firmware format)
 * Appdata format: [name] only (no flags, no location)
 */
void sendAdvertNoFlags() {
    if (!nodeIdentity.isInitialized()) {
        LOG(TAG_ERROR " Identity not initialized\n\r");
        return;
    }

    if (!timeSync.isSynchronized()) {
        LOG(TAG_ERROR " Time not synchronized\n\r");
        return;
    }

    MCPacket pkt;
    pkt.clear();

    // Set header: FLOOD + ADVERT type
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_ADVERT, MC_PAYLOAD_VER_1);
    pkt.pathLen = 0;

    uint8_t* payload = pkt.payload;
    uint16_t pos = 0;

    // [0-31] Public Key
    memcpy(&payload[pos], nodeIdentity.getPublicKey(), MC_PUBLIC_KEY_SIZE);
    pos += MC_PUBLIC_KEY_SIZE;

    // [32-35] Timestamp
    uint32_t timestamp = timeSync.getTimestamp();
    payload[pos++] = timestamp & 0xFF;
    payload[pos++] = (timestamp >> 8) & 0xFF;
    payload[pos++] = (timestamp >> 16) & 0xFF;
    payload[pos++] = (timestamp >> 24) & 0xFF;

    // Build appdata WITHOUT flags byte - just the name
    uint8_t appdata[32];
    const char* name = nodeIdentity.getNodeName();
    uint8_t nameLen = strlen(name);
    memcpy(appdata, name, nameLen);
    uint8_t appdataLen = nameLen;

    // Calculate signature over: pubkey + timestamp + appdata
    uint8_t signData[MC_PUBLIC_KEY_SIZE + 4 + 32];
    uint16_t signLen = 0;
    memcpy(&signData[signLen], nodeIdentity.getPublicKey(), MC_PUBLIC_KEY_SIZE);
    signLen += MC_PUBLIC_KEY_SIZE;
    memcpy(&signData[signLen], &payload[32], 4);  // timestamp
    signLen += 4;
    memcpy(&signData[signLen], appdata, appdataLen);
    signLen += appdataLen;

    // [36-99] Signature
    nodeIdentity.sign(&payload[pos], signData, signLen);

    // Self-verify signature
    bool sigOk = IdentityManager::verify(&payload[pos], nodeIdentity.getPublicKey(), signData, signLen);
    LOG("[SIG] Self-verify: %s\n\r", sigOk ? "OK" : "FAIL");

    pos += MC_SIGNATURE_SIZE;

    // [100+] Appdata (just name, no flags)
    memcpy(&payload[pos], appdata, appdataLen);
    pos += appdataLen;

    pkt.payloadLen = pos;

    LOG(TAG_ADVERT " Sending ADVERT noflags (%s) ts=%lu len=%d\n\r",
        name, timestamp, pkt.payloadLen);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_ADVERT " Transmission successful\n\r");
        advTxCount++;
    } else {
        LOG(TAG_ADVERT " Transmission failed\n\r");
    }

    startReceive();
}

void sendAdvert(bool flood) {
    if (!nodeIdentity.isInitialized()) {
        LOG(TAG_ERROR " Identity not initialized\n\r");
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
        LOG(TAG_ERROR " Failed to build ADVERT packet\n\r");
        return;
    }

    LOG(TAG_ADVERT " Sending %s ADVERT (%s)\n\r",
        flood ? "flood" : "zero-hop",
        nodeIdentity.getNodeName());

    // Add to packet cache so we don't re-forward our own ADVERT
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_ADVERT " Transmission successful\n\r");
        advertGen.markSent();
        advTxCount++;
    } else {
        LOG(TAG_ADVERT " Transmission failed\n\r");
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
        LOG(TAG_ERROR " Identity not initialized\n\r");
        return;
    }

    if (!timeSync.isSynchronized()) {
        LOG(TAG_ERROR " Time not synchronized - cannot send message\n\r");
        return;
    }

    // Find contact by name
    Contact* contact = contactMgr.findByName(recipientName);
    if (!contact) {
        LOG(TAG_ERROR " Contact '%s' not found\n\r", recipientName);
        LOG(TAG_INFO " Known contacts:\n\r");
        contactMgr.printContacts();
        return;
    }

    LOG(TAG_INFO " Sending message to %s...\n\r", contact->name);

    // Get or calculate shared secret
    const uint8_t* sharedSecret = contactMgr.getSharedSecret(contact);
    if (!sharedSecret) {
        LOG(TAG_ERROR " Failed to calculate shared secret\n\r");
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
        LOG(TAG_ERROR " Encryption failed\n\r");
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
        LOG(TAG_OK " Message sent to %s\n\r", contact->name);
        txCount++;
    } else {
        LOG(TAG_ERROR " Transmission failed\n\r");
    }

    startReceive();
}
#endif // LITE_MODE

//=============================================================================
// Daily Report - Send encrypted status message to admin
//=============================================================================
#ifndef LITE_MODE
/**
 * Generate report content string
 * @param buf Output buffer
 * @param maxLen Buffer size
 * @return Length of generated string
 */
uint16_t generateReportContent(char* buf, uint16_t maxLen) {
    uint16_t len = 0;

    // Format: "NodeName: Daily Report\nUptime: XXh\nRX:X TX:X FWD:X ERR:X\nBatt: XmV"
    int n = snprintf(buf, maxLen,
        "%s: Daily Report\n"
        "Uptime: %luh\n"
        "RX:%lu TX:%lu FWD:%lu ERR:%lu\n"
        "Batt: %dmV",
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

/**
 * Send daily report as encrypted text message to admin
 * Uses FLOOD routing since we don't know the path to admin
 *
 * Packet format (MC_PAYLOAD_PLAIN):
 * [dest_hash:1][src_hash:1][MAC:2][encrypted_payload]
 *
 * Encrypted payload format:
 * [timestamp:4][txt_type|attempt:1][message:variable]
 *
 * @return true if report queued for transmission
 */
bool sendDailyReport() {
    // Check if destination key is set
    bool keySet = false;
    for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
        if (reportDestPubKey[i] != 0) { keySet = true; break; }
    }
    if (!keySet) {
        LOG(TAG_INFO " Report: no destination key set\n\r");
        return false;
    }

    // Check time sync
    if (!timeSync.isSynchronized()) {
        LOG(TAG_INFO " Report: time not synchronized\n\r");
        return false;
    }

    // Calculate shared secret with destination
    uint8_t sharedSecret[MC_SHARED_SECRET_SIZE];
    if (!MeshCrypto::calcSharedSecret(sharedSecret, nodeIdentity.getPrivateKey(), reportDestPubKey)) {
        LOG(TAG_ERROR " Report: ECDH failed\n\r");
        return false;
    }

    // Generate report content
    char reportText[128];
    uint16_t textLen = generateReportContent(reportText, sizeof(reportText) - 1);
    if (textLen == 0) {
        LOG(TAG_ERROR " Report: failed to generate content\n\r");
        return false;
    }

    // Build plaintext: [timestamp:4][txt_type|attempt:1][message]
    uint8_t plaintext[140];
    uint32_t timestamp = timeSync.getTimestamp();
    plaintext[0] = timestamp & 0xFF;
    plaintext[1] = (timestamp >> 8) & 0xFF;
    plaintext[2] = (timestamp >> 16) & 0xFF;
    plaintext[3] = (timestamp >> 24) & 0xFF;
    plaintext[4] = (TXT_TYPE_PLAIN << 2) | 0;  // txt_type in upper 6 bits, attempt=0 in lower 2
    memcpy(&plaintext[5], reportText, textLen);
    uint16_t plaintextLen = 5 + textLen;

    // Encrypt: output is [MAC:2][ciphertext]
    uint8_t encrypted[160];
    uint16_t encLen = meshCrypto.encryptThenMAC(encrypted, plaintext, plaintextLen,
                                                 sharedSecret, sharedSecret);
    if (encLen == 0) {
        LOG(TAG_ERROR " Report: encryption failed\n\r");
        memset(sharedSecret, 0, sizeof(sharedSecret));
        return false;
    }

    // Build packet
    MCPacket pkt;
    pkt.clear();

    // Use FLOOD routing (we don't know the path to admin)
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    // Path starts empty (will be built by repeaters)
    pkt.pathLen = 0;

    // Payload: [dest_hash:1][src_hash:1][encrypted_data]
    pkt.payload[0] = reportDestPubKey[0];  // Destination hash
    pkt.payload[1] = nodeIdentity.getNodeHash();  // Source hash
    memcpy(&pkt.payload[2], encrypted, encLen);
    pkt.payloadLen = 2 + encLen;

    // Clear sensitive data
    memset(sharedSecret, 0, sizeof(sharedSecret));
    memset(plaintext, 0, sizeof(plaintext));

    // Log
    LOG(TAG_INFO " Sending daily report to 0x%02X (%d bytes)\n\r",
        reportDestPubKey[0], pkt.payloadLen);

    // Add to packet cache so we don't re-forward our own message
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    // Queue for transmission
    txQueue.add(&pkt);
    txCount++;

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
        LOG(TAG_INFO " Daily report scheduled time reached (%02d:%02d)\n\r",
            reportHour, reportMinute);

        if (sendDailyReport()) {
            lastReportDay = dayNumber;  // Mark as sent today
            LOG(TAG_OK " Daily report sent successfully\n\r");
        } else {
            LOG(TAG_ERROR " Daily report failed\n\r");
        }
    }
}
#endif // LITE_MODE

/**
 * Send node alert when new node/repeater is discovered
 * @param nodeName Name of the discovered node
 * @param nodeHash Hash of the discovered node
 * @param nodeType Type (1=CHAT, 2=REPEATER, etc)
 * @param rssi Signal strength
 * @return true if alert sent
 */
bool sendNodeAlert(const char* nodeName, uint8_t nodeHash, uint8_t nodeType, int16_t rssi) {
    // Check if alert is enabled
    if (!alertEnabled) return false;

    // Check if we have a destination
    bool keySet = false;
    for (uint8_t i = 0; i < REPORT_PUBKEY_SIZE; i++) {
        if (alertDestPubKey[i] != 0) { keySet = true; break; }
    }
    if (!keySet) return false;

    // Check if time is synced
    if (!timeSync.isSynchronized()) return false;

    // Build alert message
    char message[64];
    const char* typeStr = nodeType == 1 ? "CHAT" : nodeType == 2 ? "RPT" : "NODE";
    snprintf(message, sizeof(message), "NEW %s: %s [%02X] %ddBm",
             typeStr, nodeName[0] ? nodeName : "?", nodeHash, rssi);

    // Calculate shared secret with destination
    uint8_t sharedSecret[32];
    if (!MeshCrypto::calcSharedSecret(sharedSecret, nodeIdentity.getPrivateKey(), alertDestPubKey)) {
        return false;
    }

    // Encrypt the message
    uint8_t plaintext[64];
    uint8_t plaintextLen = 0;

    // Format: [timestamp:4][text_type:1][message]
    uint32_t timestamp = timeSync.getTimestamp();
    plaintext[plaintextLen++] = timestamp & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 8) & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 16) & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 24) & 0xFF;
    plaintext[plaintextLen++] = (TXT_TYPE_PLAIN << 2) | 0;  // txt_type in upper 6 bits, attempt=0

    uint8_t msgLen = strlen(message);
    memcpy(&plaintext[plaintextLen], message, msgLen);
    plaintextLen += msgLen;

    // Encrypt
    uint8_t encrypted[80];
    uint16_t encLen = meshCrypto.encryptResponse(encrypted, plaintext, plaintextLen, sharedSecret);
    if (encLen == 0) return false;

    // Build packet
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);
    pkt.pathLen = 0;

    // Payload: [dest_hash:1][src_hash:1][encrypted]
    pkt.payload[0] = alertDestPubKey[0];  // Destination hash
    pkt.payload[1] = nodeIdentity.getNodeHash();  // Source hash
    memcpy(&pkt.payload[2], encrypted, encLen);
    pkt.payloadLen = 2 + encLen;

    LOG(TAG_INFO " Sending node alert to 0x%02X\n\r", alertDestPubKey[0]);

    // Add to cache and queue
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);
    txQueue.add(&pkt);
    txCount++;

    return true;
}

void checkAdvertBeacon() {
    // Check for pending ADVERT after time sync
    if (pendingAdvertTime > 0 && millis() >= pendingAdvertTime) {
        pendingAdvertTime = 0;  // Clear pending
        LOG(TAG_ADVERT " Sending scheduled ADVERT after time sync\n\r");
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
        LOG(TAG_DISCOVERY " Rate limited, skipping response\n\r");
        return false;
    }

    LOG(TAG_DISCOVERY " Received DISCOVER_REQ (filter=0x%02X, tag=0x%08lX)\n\r",
        typeFilter, requestTag);

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

    LOG(TAG_DISCOVERY " Sending DISCOVER_RESP (delay=%lums)\n\r", randomDelay);

    // Queue with delay
    delay(randomDelay);
    txQueue.add(&respPkt);

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
    // Minimum: dest_hash(1) + src_hash(1) + MAC(2) + min_cipher(16) = 20
    if (pkt->payloadLen < 20) {
        LOG(TAG_AUTH " REQUEST too short: %d bytes\n\r", pkt->payloadLen);
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
        LOG(TAG_AUTH " No session for %02X (have %d sessions)\n\r",
            srcHash, sessionManager.getSessionCount());
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
        LOG(TAG_AUTH " REQUEST decryption failed (bad MAC)\n\r");
        return false;
    }

    // Extract timestamp and check replay
    uint32_t timestamp = decrypted[0] |
                         (decrypted[1] << 8) |
                         (decrypted[2] << 16) |
                         (decrypted[3] << 24);

    if (timestamp <= session->lastTimestamp) {
        LOG(TAG_AUTH " REQUEST replay detected (ts=%lu <= %lu)\n\r",
            timestamp, session->lastTimestamp);
        return false;
    }
    session->lastTimestamp = timestamp;
    session->lastActivity = millis();

    // Extract request type
    uint8_t reqType = decrypted[4];

    LOG(TAG_AUTH " REQUEST type=0x%02X from %02X\n\r", reqType, srcHash);

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
            // Return RepeaterStats in MeshCore format (52 bytes)
            LOG(TAG_AUTH " -> GET_STATUS\n\r");
            responseLen += repeaterHelper.serializeRepeaterStats(
                &responseData[responseLen],
                telemetry.getBatteryMv(),
                txQueue.getCount(),
                lastRssi, lastSnr);
            break;

        case REQ_TYPE_GET_TELEMETRY:
            // Return telemetry in MeshCore CayenneLPP format
            LOG(TAG_AUTH " -> GET_TELEMETRY\n\r");
            {
                // Update battery reading before responding
                telemetry.update();

                CayenneLPP lpp(&responseData[responseLen], sizeof(responseData) - responseLen);
                // Use LPP_VOLTAGE (116) for MeshCore compatibility
                // Channel 1 = TELEM_CHANNEL_SELF
                lpp.addVoltage(1, telemetry.getBatteryMv() / 1000.0f);
                if (nodeIdentity.hasLocation()) {
                    float lat = nodeIdentity.getLatitude() / 1000000.0f;
                    float lon = nodeIdentity.getLongitude() / 1000000.0f;
                    lpp.addGPS(2, lat, lon, 0);
                }
                responseLen += lpp.getSize();
            }
            break;

        case REQ_TYPE_GET_NEIGHBOURS:
            // Return neighbour list
            LOG(TAG_AUTH " -> GET_NEIGHBOURS\n\r");
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
            // Return radio stats
            LOG(TAG_AUTH " -> GET_MINMAXAVG (radio stats)\n\r");
            responseLen += repeaterHelper.serializeRadioStats(&responseData[responseLen]);
            break;

        case REQ_TYPE_GET_ACCESS_LIST:
            // Return ACL (admin only)
            LOG(TAG_AUTH " -> GET_ACCESS_LIST\n\r");
            if (session->permissions != PERM_ACL_ADMIN) {
                LOG(TAG_AUTH " Permission denied (not admin)\n\r");
                return false;
            }
            {
                ACLManager& acl = repeaterHelper.getACL();
                uint8_t count = acl.getCount();
                responseData[responseLen++] = count;
                // Entries would follow but we keep it minimal
            }
            break;

        case REQ_TYPE_KEEP_ALIVE:
            // Just acknowledge with timestamp
            LOG(TAG_AUTH " -> KEEP_ALIVE\n\r");
            break;

        case REQ_TYPE_SEND_CLI:
            // Execute CLI command (admin only)
            LOG(TAG_AUTH " -> SEND_CLI\n\r");
            if (session->permissions != PERM_ACL_ADMIN) {
                LOG(TAG_AUTH " Permission denied (not admin)\n\r");
                return false;
            }
            {
                // Extract command string from decrypted payload (after timestamp + type)
                // Command starts at offset 5, null-terminated or until end of decrypted data
                char cmdStr[64];
                uint16_t cmdLen = decryptedLen - 5;
                if (cmdLen > sizeof(cmdStr) - 1) cmdLen = sizeof(cmdStr) - 1;
                memcpy(cmdStr, &decrypted[5], cmdLen);
                cmdStr[cmdLen] = '\0';

                // Remove trailing whitespace/newlines
                while (cmdLen > 0 && (cmdStr[cmdLen-1] == '\n' || cmdStr[cmdLen-1] == '\r' ||
                       cmdStr[cmdLen-1] == ' ' || cmdStr[cmdLen-1] == '\0')) {
                    cmdStr[--cmdLen] = '\0';
                }

                LOG(TAG_AUTH " CLI cmd: '%s'\n\r", cmdStr);

                // Process command and get response
                char cliResponse[96];  // Reduced from 128 to save stack
                bool isAdmin = (session->permissions == PERM_ACL_ADMIN);
                uint16_t cliLen = processRemoteCommand(cmdStr, cliResponse, sizeof(cliResponse), isAdmin);

                // Append CLI response to responseData (after timestamp)
                if (cliLen > 0 && responseLen + cliLen < sizeof(responseData)) {
                    memcpy(&responseData[responseLen], cliResponse, cliLen);
                    responseLen += cliLen;
                }

                LOG(TAG_AUTH " CLI response: %d bytes\n\r", cliLen);

                // Handle reboot command specially
                if (strcmp(cmdStr, "reboot") == 0) {
                    // Schedule reboot after sending response
                    pendingReboot = true;
                    rebootTime = millis() + 500;  // Reboot in 500ms
                }
            }
            break;

        default:
            LOG(TAG_AUTH " Unknown request type 0x%02X\n\r", reqType);
            return false;
    }

    // Encrypt response
    uint8_t encryptedResponse[160];
    uint16_t encLen = meshCrypto.encryptResponse(
        encryptedResponse, responseData, responseLen, session->sharedSecret);

    if (encLen == 0) {
        LOG(TAG_AUTH " Failed to encrypt response\n\r");
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

    LOG(TAG_AUTH " Sending RESPONSE (type=0x%02X, len=%d)\n\r", reqType, respPkt.payloadLen);

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
        LOG(TAG_AUTH " Failed to encrypt response\n\r");
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

    // Debug: show response payload
    LOG(TAG_AUTH " RSP: d=%02X s=%02X MAC=%02X%02X enc=%d\n\r",
        respPkt.payload[0], respPkt.payload[1],
        respPkt.payload[2], respPkt.payload[3], encLen);

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
        LOG(TAG_AUTH " ANON_REQ too short: %d bytes\n\r", pkt->payloadLen);
        return false;
    }

    // Extract components (dest_hash at [0] already verified by caller)
    const uint8_t* ephemeralPub = &pkt->payload[1];     // Ephemeral pubkey at [1-32]

    // Debug: show ANON_REQ structure - trying different format interpretations
    LOG(TAG_AUTH " ANON_REQ payload (%d bytes):\n\r", pkt->payloadLen);
    LOG(TAG_AUTH "   [0] destHash: %02X\n\r", pkt->payload[0]);
    LOG(TAG_AUTH "   [1] srcHash?: %02X (Andrea=7E)\n\r", pkt->payload[1]);
    LOG(TAG_AUTH "   [2-9] ephemeral?: ");
    for(int i=2; i<=9 && i<pkt->payloadLen; i++) LOG_RAW("%02X ", pkt->payload[i]);
    LOG_RAW("\n\r");
    LOG(TAG_AUTH "   Full payload hex: ");
    for(int i=0; i<pkt->payloadLen && i<51; i++) LOG_RAW("%02X ", pkt->payload[i]);
    LOG_RAW("\n\r");

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
        LOG(TAG_AUTH " ANON_REQ decryption failed (bad MAC or format)\n\r");
        return false;
    }

    LOG(TAG_AUTH " Login request: timestamp=%lu, password='%s'\n\r", timestamp, password);

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
        LOG(TAG_AUTH " Login " ANSI_RED "FAILED" ANSI_RESET " - invalid password or replay\n\r");
        return false;
    }

    bool isAdmin = (permissions == PERM_ACL_ADMIN);
    LOG(TAG_AUTH " Login " ANSI_GREEN "OK" ANSI_RESET " - %s access granted\n\r",
        isAdmin ? "ADMIN" : "GUEST");

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
            LOG(TAG_AUTH " Admin pubkey captured for daily report\n\r");
        }
    }

    // Get shared secret from session for response encryption
    ClientSession* session = sessionManager.findSession(ephemeralPub);
    if (!session) {
        LOG(TAG_AUTH " Session not found after login\n\r");
        return false;
    }

    // Debug: show session details
    LOG(TAG_AUTH " Session: hash=%02X perm=%02X ts=%lu\n\r",
        session->pubKey[0], session->permissions, session->lastTimestamp);

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

    // Handle ANON_REQ (login request) - must be addressed to us
    // Format: [dest_hash:1][ephemeral_pub:32][MAC:2][ciphertext]
    if (pkt->header.getPayloadType() == MC_PAYLOAD_ANON_REQ) {
        uint8_t destHash = pkt->payload[0];
        LOG(TAG_AUTH " ANON_REQ: destHash=%02X myHash=%02X len=%d\n\r",
            destHash, nodeIdentity.getNodeHash(), pkt->payloadLen);
        // Min size: 1 (dest_hash) + 32 (ephemeral) + 2 (MAC) + 16 (min block)
        if (pkt->payloadLen >= 51) {
            // Check destination hash (first byte = dest_hash)
            if (destHash == nodeIdentity.getNodeHash()) {
                LOG(TAG_AUTH " Received ANON_REQ addressed to us (hash=%02X)\n\r", destHash);
                processAnonRequest(pkt);
            } else {
                LOG(TAG_AUTH " ANON_REQ for other node (dest=%02X)\n\r", destHash);
            }
        } else {
            LOG(TAG_AUTH " ANON_REQ too short: %d bytes (need 51+)\n\r", pkt->payloadLen);
        }
    }
    // Handle REQUEST (authenticated request from logged-in client)
    // Format: [dest_hash:1][src_hash:1][MAC:2][ciphertext]
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_REQUEST) {
        LOG(TAG_AUTH " REQUEST: len=%d dest=%02X src=%02X myHash=%02X\n\r",
            pkt->payloadLen,
            pkt->payloadLen > 0 ? pkt->payload[0] : 0,
            pkt->payloadLen > 1 ? pkt->payload[1] : 0,
            nodeIdentity.getNodeHash());
        if (pkt->payloadLen >= 20) {
            uint8_t destHash = pkt->payload[0];
            if (destHash == nodeIdentity.getNodeHash()) {
                processAuthenticatedRequest(pkt);
            } else {
                LOG(TAG_AUTH " REQUEST not for us (dest=%02X)\n\r", destHash);
            }
        } else {
            LOG(TAG_AUTH " REQUEST too short\n\r");
        }
    }
    // Handle CONTROL (node discovery, etc.)
    // Format: [flags:1][type_filter:1][tag:4][since:4(optional)]
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_CONTROL) {
        if (pkt->payloadLen >= 6) {
            processDiscoverRequest(pkt);
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
                LOG(TAG_OK " " ANSI_GREEN "Time synchronized!" ANSI_RESET " Unix: %lu\n\r", timeSync.getTimestamp());
                // Schedule our own ADVERT after sync
                pendingAdvertTime = millis() + ADVERT_AFTER_SYNC_MS;
                LOG(TAG_INFO " Will send ADVERT in %d seconds\n\r", ADVERT_AFTER_SYNC_MS / 1000);
            } else if (syncResult == 2) {
                // Re-sync via consensus (2 different sources agreed)
                ledBlueDoubleBlink();  // Signal time re-sync
                LOG(TAG_OK " " ANSI_GREEN "Time re-synchronized!" ANSI_RESET " (consensus) Unix: %lu\n\r", timeSync.getTimestamp());
                // Schedule new ADVERT with updated time
                pendingAdvertTime = millis() + ADVERT_AFTER_SYNC_MS;
                LOG(TAG_INFO " Will send ADVERT in %d seconds\n\r", ADVERT_AFTER_SYNC_MS / 1000);
            } else if (timeSync.hasPendingSync()) {
                // Received different time, stored as pending - waiting for confirmation
                LOG(TAG_INFO " Time drift detected: %lu (pending confirmation)\n\r", advertTime);
            }
        }

        AdvertInfo advInfo;
        if (AdvertGenerator::parseAdvert(pkt->payload, pkt->payloadLen, &advInfo)) {
            // Show node info
            LOG(TAG_NODE " " ANSI_BOLD "%s" ANSI_RESET, advInfo.name);
            if (advInfo.isRepeater) {
                LOG_RAW(ANSI_CYAN " [RPT]" ANSI_RESET);
            }
            if (advInfo.isChatNode) {
                LOG_RAW(ANSI_GREEN " [CHAT]" ANSI_RESET);
            }
            LOG_RAW(" hash=%02X", advInfo.pubKeyHash);
            if (advInfo.hasLocation) {
                LOG_RAW(" loc=%.4f,%.4f", advInfo.latitude, advInfo.longitude);
            }
            LOG_RAW("\n\r");

            // Update seen nodes with pubkey hash and name from ADVERT
            bool isNew = seenNodes.update(advInfo.pubKeyHash, pkt->rssi, pkt->snr, advInfo.name);
            if (isNew) {
                LOG(TAG_NODE " New node discovered via ADVERT\n\r");
                // Send alert for new node
                uint8_t nodeType = advInfo.isChatNode ? 1 : advInfo.isRepeater ? 2 : 0;
                sendNodeAlert(advInfo.name, advInfo.pubKeyHash, nodeType, pkt->rssi);
            }

            // Add to contact manager (stores full public key for messaging)
            const uint8_t* pubKey = &pkt->payload[ADVERT_PUBKEY_OFFSET];
            contactMgr.updateFromAdvert(pubKey, advInfo.name, pkt->rssi, pkt->snr);

            // If this is a repeater, add to neighbours list (full pubkey available)
            if (advInfo.isRepeater && pkt->payloadLen >= 32) {
                bool newNeighbour = repeaterHelper.getNeighbours().update(
                    pkt->payload,  // First 32 bytes are pubkey
                    pkt->snr, pkt->rssi);
                if (newNeighbour) {
                    LOG(TAG_NODE " New neighbour repeater added\n\r");
                }
            }
        }
    }
    // Track nodes - either from path or from payload hash for direct packets
    else if (pkt->pathLen > 0) {
        // First byte is originator
        bool isNew = seenNodes.update(pkt->path[0], pkt->rssi, pkt->snr);
        if (isNew) {
            LOG(TAG_NODE " New relay node detected: %02X\n\r", pkt->path[0]);
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
            LOG(TAG_NODE " New direct node detected: %02X\n\r", hash);
        }
    }

    // Check if we should forward
    if (shouldForward(pkt)) {
        // Add our node hash to path (simplified: just add a byte)
        uint8_t myHash = (nodeId >> 24) ^ (nodeId >> 16) ^
                         (nodeId >> 8) ^ nodeId;
        pkt->path[pkt->pathLen++] = myHash;

        // Add to TX queue
        txQueue.add(pkt);
        fwdCount++;
        LOG(TAG_FWD " Queued for relay (path=%d)\n\r", pkt->pathLen);
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

    LOG_RAW("\n\r");
    LOG_RAW(ANSI_CYAN "╔══════════════════════════════════════════════════════════╗\n\r");
    LOG_RAW("║" ANSI_BOLD ANSI_WHITE "         CubeCellMeshCore Repeater v%s                 " ANSI_RESET ANSI_CYAN "║\n\r", FIRMWARE_VERSION);
    LOG_RAW("║" ANSI_WHITE "         MeshCore Compatible - EU868 Region               " ANSI_CYAN "║\n\r");
    LOG_RAW("╚══════════════════════════════════════════════════════════╝" ANSI_RESET "\n\r");
    LOG_RAW("\n\r");

    // Load configuration from EEPROM
    loadConfig();

    // Enable watchdog
#if MC_WATCHDOG_ENABLED && defined(CUBECELL)
    innerWdtEnable(true);
    LOG(TAG_SYSTEM " Watchdog timer enabled\n\r");
#endif

    // Generate node ID if not set
    if (nodeId == 0) {
        nodeId = generateNodeId();
    }
    LOG(TAG_SYSTEM " Node ID: %08lX\n\r", nodeId);

    // Initialize node identity (Ed25519 keys)
    LOG(TAG_SYSTEM " Initializing identity...\n\r");
    if (nodeIdentity.begin()) {
        LOG(TAG_OK " Identity ready: %s (hash: %02X)\n\r",
            nodeIdentity.getNodeName(), nodeIdentity.getNodeHash());
    } else {
        LOG(TAG_ERROR " Identity initialization failed!\n\r");
    }

    // Initialize ADVERT generator with time sync
    advertGen.begin(&nodeIdentity, &timeSync);
    advertGen.setInterval(ADVERT_INTERVAL_MS);
    advertGen.setEnabled(ADVERT_ENABLED);
    LOG(TAG_INFO " ADVERT beacon: %s (interval: %lus)\n\r",
        ADVERT_ENABLED ? "enabled" : "disabled",
        ADVERT_INTERVAL_MS / 1000);

    // Initialize telemetry
    telemetry.begin(&rxCount, &txCount, &fwdCount, &errCount, &lastRssi, &lastSnr);
    LOG(TAG_INFO " Telemetry initialized\n\r");

    // Initialize repeater helper
    repeaterHelper.begin(&nodeIdentity);
    LOG(TAG_INFO " Repeater helper initialized\n\r");

    // Initialize contact manager for direct messaging
    contactMgr.begin(&nodeIdentity);
    LOG(TAG_INFO " Contact manager initialized\n\r");

    initLed();
    packetCache.clear();
    txQueue.clear();
    seenNodes.clear();

    setupRadio();
    calculateTimings();
    applyPowerSettings();  // Apply loaded config
    startReceive();

    bootTime = millis();
    LOG(TAG_SYSTEM " Ready - listening for packets\n\r");
    LOG(TAG_INFO " Serial console active for %d seconds\n\r", BOOT_SAFE_PERIOD_MS / 1000);
    LOG(TAG_INFO " Waiting for time sync before sending ADVERT...\n\r");
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
                        LOG(TAG_ERROR " Invalid packet (len=%d hdr=0x%02X pL=%d pyL=%d)\n\r",
                            len, buf[0], (len > 1) ? buf[1] : 0, (len > 2) ? buf[2] : 0);
                        errCount++;
                    }
                } else if (radioError == RADIOLIB_ERR_CRC_MISMATCH) {
                    LOG(TAG_ERROR " CRC mismatch - packet discarded\n\r");
                    crcErrCount++;
                } else {
                    LOG(TAG_ERROR " RX error (code %d)\n\r", radioError);
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
            LOG(TAG_TX " Waiting %lums (backoff SNR=%ddB)\n\r", txDelay, lastSnr / 4);

            activeReceiveStart = 0;
            uint32_t start = millis();
            bool aborted = false;

            while (millis() - start < txDelay) {
                feedWatchdog();

                // Check for new packet or active reception
                if (dio1Flag || isActivelyReceiving()) {
                    LOG(TAG_TX " Aborted - channel busy\n\r");
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

#ifndef LITE_MODE
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
