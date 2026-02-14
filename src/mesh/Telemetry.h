#pragma once
#include <Arduino.h>

/**
 * CubeCell Telemetry Module
 * Reads battery voltage, temperature, and system stats
 */

// CubeCell battery ADC workaround for framework 1.6.0
// getBatteryVoltage()/analogReadmV() broken: ch3 calibration not initialized

/**
 * Telemetry data structure
 */
struct TelemetryData {
    uint16_t batteryMv;     // Battery voltage in mV
    int8_t temperature;     // Temperature in Celsius
    uint32_t uptime;        // Uptime in seconds
    uint32_t rxCount;       // Packets received
    uint32_t txCount;       // Packets transmitted
    uint32_t fwdCount;      // Packets forwarded
    uint32_t errorCount;    // Error count
    int16_t lastRssi;       // Last RSSI
    int8_t lastSnr;         // Last SNR (x4)
};

/**
 * Telemetry Manager Class
 */
class TelemetryManager {
private:
    TelemetryData data;
    uint32_t lastReadTime;
    uint32_t readInterval;

    // External stat pointers
    uint32_t* pRxCount;
    uint32_t* pTxCount;
    uint32_t* pFwdCount;
    uint32_t* pErrorCount;
    int16_t* pLastRssi;
    int8_t* pLastSnr;

public:
    TelemetryManager() : lastReadTime(0), readInterval(60000),
                         pRxCount(nullptr), pTxCount(nullptr),
                         pFwdCount(nullptr), pErrorCount(nullptr),
                         pLastRssi(nullptr), pLastSnr(nullptr) {
        memset(&data, 0, sizeof(data));
    }

    /**
     * Initialize telemetry with external stat pointers
     */
    void begin(uint32_t* rxCnt, uint32_t* txCnt, uint32_t* fwdCnt,
               uint32_t* errCnt, int16_t* rssi, int8_t* snr) {
        pRxCount = rxCnt;
        pTxCount = txCnt;
        pFwdCount = fwdCnt;
        pErrorCount = errCnt;
        pLastRssi = rssi;
        pLastSnr = snr;

        // Initial read
        update();
    }

    /**
     * Set read interval
     */
    void setInterval(uint32_t intervalMs) {
        readInterval = intervalMs;
    }

    /**
     * Check if update is needed
     */
    bool shouldUpdate() {
        return (millis() - lastReadTime) >= readInterval;
    }

    /**
     * Update all telemetry readings
     */
    void update() {
        readBattery();
        readTemperature();
        updateStats();
        lastReadTime = millis();
    }

    /**
     * Read battery voltage.
     * Framework 1.6.0 bugs: analogReadmV()/getBatteryVoltage() broken
     * (ch3 calibration not initialized).
     * Workaround: analogRead() + ch0 calibration + VBAT_ADC_CTL.
     */
    void readBattery() {
        #ifdef CUBECELL
        extern volatile int16 ADC_SAR_Seq_offset[];
        extern volatile int32 ADC_SAR_Seq_countsPer10Volt[];

        // Enable VBAT measurement circuit
        pinMode(VBAT_ADC_CTL, OUTPUT);
        digitalWrite(VBAT_ADC_CTL, LOW);
        delay(100);

        uint16_t counts = analogRead(ADC);

        pinMode(VBAT_ADC_CTL, INPUT);

        // Convert using ch0 calibration, apply x2 voltage divider
        int32_t gain = ADC_SAR_Seq_countsPer10Volt[0];
        if (gain != 0) {
            int32_t adj = (int32_t)counts - ADC_SAR_Seq_offset[0];
            data.batteryMv = (uint16_t)((adj * 20000L) / gain);
        } else {
            data.batteryMv = 0;
        }
        #else
        data.batteryMv = 0;
        #endif
    }

    /**
     * Read internal temperature
     * Note: CubeCell doesn't have a built-in temp sensor easily accessible
     * This is a placeholder for future implementation or external sensor
     */
    void readTemperature() {
        #ifdef CUBECELL
        // CubeCell doesn't expose internal temp easily
        // Placeholder: estimate based on chip characteristics
        // In real implementation, add external sensor (DS18B20, etc.)
        data.temperature = 25; // Default room temp
        #else
        data.temperature = 25;
        #endif
    }

    /**
     * Update statistics from external counters
     */
    void updateStats() {
        data.uptime = millis() / 1000;

        if (pRxCount) data.rxCount = *pRxCount;
        if (pTxCount) data.txCount = *pTxCount;
        if (pFwdCount) data.fwdCount = *pFwdCount;
        if (pErrorCount) data.errorCount = *pErrorCount;
        if (pLastRssi) data.lastRssi = *pLastRssi;
        if (pLastSnr) data.lastSnr = *pLastSnr;
    }

    /**
     * Get battery voltage in mV
     */
    uint16_t getBatteryMv() const {
        return data.batteryMv;
    }

    /**
     * Get battery percentage (rough estimate)
     * Based on typical LiPo discharge curve
     */
    uint8_t getBatteryPercent() const {
        if (data.batteryMv >= 4200) return 100;
        if (data.batteryMv <= 3300) return 0;

        // Linear approximation between 3.3V (0%) and 4.2V (100%)
        return (uint8_t)(((data.batteryMv - 3300) * 100) / 900);
    }

    /**
     * Get temperature in Celsius
     */
    int8_t getTemperature() const {
        return data.temperature;
    }

    /**
     * Get uptime in seconds
     */
    uint32_t getUptime() const {
        return data.uptime;
    }

    /**
     * Get full telemetry data
     */
    const TelemetryData* getData() const {
        return &data;
    }

    /**
     * Format uptime as string (HH:MM:SS)
     */
    void formatUptime(char* buf, size_t len) const {
        uint32_t sec = data.uptime;
        uint32_t min = sec / 60;
        uint32_t hr = min / 60;
        uint32_t days = hr / 24;

        if (days > 0) {
            snprintf(buf, len, "%lud %02lu:%02lu:%02lu",
                    days, hr % 24, min % 60, sec % 60);
        } else {
            snprintf(buf, len, "%02lu:%02lu:%02lu",
                    hr, min % 60, sec % 60);
        }
    }

};
