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
