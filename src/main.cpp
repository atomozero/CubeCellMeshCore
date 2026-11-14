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

#include "cli/Commands.inc"
#include "radio/Radio.inc"
#include "mesh/Messaging.inc"
#include "mesh/PacketHandler.inc"
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
