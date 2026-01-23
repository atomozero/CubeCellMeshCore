/**
 * Config.cpp - EEPROM configuration management for CubeCellMeshCore
 *
 * Handles loading, saving and resetting of node configuration
 */

#include "Config.h"

// Simple logging for Config module (avoid main.h include issues)
#ifndef SILENT
#define CONFIG_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CONFIG_LOG(...)
#endif

//=============================================================================
// Default configuration
//=============================================================================
const NodeConfig defaultConfig = {
    EEPROM_MAGIC,
    EEPROM_VERSION,
    1,      // powerSaveMode = balanced
    false,  // rxBoostEnabled
    true,   // deepSleepEnabled
    "admin",  // Default admin password
    "guest",  // Default guest password
    false,  // reportEnabled
    8,      // reportHour (08:00)
    0,      // reportMinute
    {0},    // reportDestPubKey (empty)
    false,  // alertEnabled
    {0},    // alertDestPubKey (empty)
    {0}     // reserved
};

//=============================================================================
// Configuration (EEPROM)
//=============================================================================

void loadConfig() {
    EEPROM.begin(EEPROM_SIZE);

    NodeConfig config;
    EEPROM.get(0, config);

    // Check if config is valid
    if (config.magic == EEPROM_MAGIC && config.version == EEPROM_VERSION) {
        powerSaveMode = config.powerSaveMode;
        rxBoostEnabled = config.rxBoostEnabled;
        deepSleepEnabled = config.deepSleepEnabled;

        // Load passwords into SessionManager
        config.adminPassword[CONFIG_PASSWORD_LEN - 1] = '\0';  // Ensure null-terminated
        config.guestPassword[CONFIG_PASSWORD_LEN - 1] = '\0';
        sessionManager.setAdminPassword(config.adminPassword);
        sessionManager.setGuestPassword(config.guestPassword);

        // Load daily report settings
        reportEnabled = config.reportEnabled;
        reportHour = config.reportHour;
        reportMinute = config.reportMinute;
        memcpy(reportDestPubKey, config.reportDestPubKey, REPORT_PUBKEY_SIZE);

        // Load node alert settings
        alertEnabled = config.alertEnabled;
        memcpy(alertDestPubKey, config.alertDestPubKey, REPORT_PUBKEY_SIZE);

        CONFIG_LOG("[C] Loaded (report=%s, alert=%s)\n\r",
            reportEnabled ? "on" : "off",
            alertEnabled ? "on" : "off");
    } else {
        // First boot or version mismatch - use defaults
        powerSaveMode = defaultConfig.powerSaveMode;
        rxBoostEnabled = defaultConfig.rxBoostEnabled;
        deepSleepEnabled = defaultConfig.deepSleepEnabled;

        // Set default passwords
        sessionManager.setAdminPassword(defaultConfig.adminPassword);
        sessionManager.setGuestPassword(defaultConfig.guestPassword);

        // Set default report settings
        reportEnabled = defaultConfig.reportEnabled;
        reportHour = defaultConfig.reportHour;
        reportMinute = defaultConfig.reportMinute;
        memset(reportDestPubKey, 0, REPORT_PUBKEY_SIZE);

        // Set default alert settings
        alertEnabled = defaultConfig.alertEnabled;
        memset(alertDestPubKey, 0, REPORT_PUBKEY_SIZE);

        CONFIG_LOG("[C] First boot, using defaults\n\r");
        saveConfig();  // Save defaults
    }
}

void saveConfig() {
    NodeConfig config;
    config.magic = EEPROM_MAGIC;
    config.version = EEPROM_VERSION;
    config.powerSaveMode = powerSaveMode;
    config.rxBoostEnabled = rxBoostEnabled;
    config.deepSleepEnabled = deepSleepEnabled;

    // Save passwords from SessionManager
    strncpy(config.adminPassword, sessionManager.getAdminPassword(), CONFIG_PASSWORD_LEN - 1);
    config.adminPassword[CONFIG_PASSWORD_LEN - 1] = '\0';
    strncpy(config.guestPassword, sessionManager.getGuestPassword(), CONFIG_PASSWORD_LEN - 1);
    config.guestPassword[CONFIG_PASSWORD_LEN - 1] = '\0';

    // Save daily report settings
    config.reportEnabled = reportEnabled;
    config.reportHour = reportHour;
    config.reportMinute = reportMinute;
    memcpy(config.reportDestPubKey, reportDestPubKey, REPORT_PUBKEY_SIZE);

    // Save node alert settings
    config.alertEnabled = alertEnabled;
    memcpy(config.alertDestPubKey, alertDestPubKey, REPORT_PUBKEY_SIZE);

    memset(config.reserved, 0, sizeof(config.reserved));

    EEPROM.put(0, config);
    if (EEPROM.commit()) {
        CONFIG_LOG("[C] Saved to EEPROM\n\r");
    } else {
        CONFIG_LOG("[E] EEPROM write failed\n\r");
    }
}

void resetConfig() {
    powerSaveMode = defaultConfig.powerSaveMode;
    rxBoostEnabled = defaultConfig.rxBoostEnabled;
    deepSleepEnabled = defaultConfig.deepSleepEnabled;

    // Reset passwords to defaults
    sessionManager.setAdminPassword(defaultConfig.adminPassword);
    sessionManager.setGuestPassword(defaultConfig.guestPassword);

    // Reset daily report settings
    reportEnabled = defaultConfig.reportEnabled;
    reportHour = defaultConfig.reportHour;
    reportMinute = defaultConfig.reportMinute;
    memset(reportDestPubKey, 0, REPORT_PUBKEY_SIZE);

    // Reset node alert settings
    alertEnabled = defaultConfig.alertEnabled;
    memset(alertDestPubKey, 0, REPORT_PUBKEY_SIZE);

    saveConfig();
    CONFIG_LOG("[C] Reset to factory defaults\n\r");
}

//=============================================================================
// Persistent Statistics (EEPROM)
//=============================================================================

// Global persistent stats instance
PersistentStats persistentStats;
static uint32_t lastStatsSaveTime = 0;
static uint32_t sessionStartTime = 0;

/**
 * Simple CRC16 for data integrity
 */
static uint16_t calcCRC16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

/**
 * Load persistent stats from EEPROM
 */
void loadPersistentStats() {
    EEPROM.get(STATS_EEPROM_OFFSET, persistentStats);

    // Verify magic, version and checksum
    if (persistentStats.magic == STATS_EEPROM_MAGIC &&
        persistentStats.version == STATS_EEPROM_VERSION) {
        // Verify checksum
        uint16_t savedCrc = persistentStats.checksum;
        persistentStats.checksum = 0;
        uint16_t calcCrc = calcCRC16((uint8_t*)&persistentStats, sizeof(PersistentStats));
        persistentStats.checksum = savedCrc;

        if (savedCrc == calcCrc) {
            // Valid stats loaded
            persistentStats.bootCount++;
            CONFIG_LOG("[S] Stats loaded (boots=%d, rx=%lu, tx=%lu, nodes=%lu)\n\r",
                persistentStats.bootCount,
                persistentStats.totalRxPackets,
                persistentStats.totalTxPackets,
                persistentStats.totalUniqueNodes);
            sessionStartTime = millis();
            return;
        }
        CONFIG_LOG("[S] Stats checksum mismatch, resetting\n\r");
    } else {
        CONFIG_LOG("[S] No valid stats, initializing\n\r");
    }

    // Initialize fresh stats
    memset(&persistentStats, 0, sizeof(PersistentStats));
    persistentStats.magic = STATS_EEPROM_MAGIC;
    persistentStats.version = STATS_EEPROM_VERSION;
    persistentStats.bootCount = 1;
    persistentStats.firstBootTime = 0;  // Will be set when time syncs
    sessionStartTime = millis();
    savePersistentStats();
}

/**
 * Save persistent stats to EEPROM
 */
void savePersistentStats() {
    // Update uptime before saving
    uint32_t sessionUptime = (millis() - sessionStartTime) / 1000;
    persistentStats.totalUptime += sessionUptime;
    sessionStartTime = millis();  // Reset for next interval

    // Calculate checksum
    persistentStats.checksum = 0;
    persistentStats.checksum = calcCRC16((uint8_t*)&persistentStats, sizeof(PersistentStats));

    EEPROM.put(STATS_EEPROM_OFFSET, persistentStats);
    if (EEPROM.commit()) {
        lastStatsSaveTime = millis();
        CONFIG_LOG("[S] Stats saved\n\r");
    } else {
        CONFIG_LOG("[E] Stats save failed\n\r");
    }
}

/**
 * Check if stats need auto-save
 */
void checkStatsSave() {
    if ((millis() - lastStatsSaveTime) >= STATS_SAVE_INTERVAL_MS) {
        savePersistentStats();
    }
}

/**
 * Increment RX counter
 */
void statsRecordRx() {
    persistentStats.totalRxPackets++;
}

/**
 * Increment TX counter
 */
void statsRecordTx() {
    persistentStats.totalTxPackets++;
}

/**
 * Increment FWD counter
 */
void statsRecordFwd() {
    persistentStats.totalFwdPackets++;
}

/**
 * Record unique node seen
 */
void statsRecordUniqueNode() {
    persistentStats.totalUniqueNodes++;
}

/**
 * Record successful login
 */
void statsRecordLogin() {
    persistentStats.totalLogins++;
}

/**
 * Record failed login
 */
void statsRecordLoginFail() {
    persistentStats.totalLoginFails++;
}

/**
 * Record rate limited request
 */
void statsRecordRateLimited() {
    persistentStats.totalRateLimited++;
}

/**
 * Set first boot time (called when time syncs)
 */
void statsSetFirstBootTime(uint32_t unixTime) {
    if (persistentStats.firstBootTime == 0) {
        persistentStats.firstBootTime = unixTime;
    }
    persistentStats.lastSaveTime = unixTime;
}

/**
 * Get current uptime including session
 */
uint32_t statsGetTotalUptime() {
    uint32_t sessionUptime = (millis() - sessionStartTime) / 1000;
    return persistentStats.totalUptime + sessionUptime;
}

/**
 * Get pointer to stats structure (read-only)
 */
const PersistentStats* getPersistentStats() {
    return &persistentStats;
}
