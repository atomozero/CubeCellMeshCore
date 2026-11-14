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

