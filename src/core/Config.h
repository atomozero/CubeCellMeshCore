/**
 * Config.h - EEPROM configuration management for CubeCellMeshCore
 *
 * Handles loading, saving and resetting of node configuration
 */

#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "globals.h"

//=============================================================================
// EEPROM Configuration defines (duplicated from main.h to avoid include issues)
//=============================================================================
#ifndef EEPROM_SIZE
#define EEPROM_SIZE         512
#endif
#ifndef EEPROM_MAGIC
#define EEPROM_MAGIC        0xCC3C
#endif
#ifndef EEPROM_VERSION
#define EEPROM_VERSION      4
#endif
#ifndef CONFIG_PASSWORD_LEN
#define CONFIG_PASSWORD_LEN 16
#endif

//=============================================================================
// NodeConfig structure
//=============================================================================
#ifndef NODECONFIG_DEFINED
#define NODECONFIG_DEFINED
struct NodeConfig {
    uint16_t magic;
    uint8_t version;
    uint8_t powerSaveMode;
    bool rxBoostEnabled;
    bool deepSleepEnabled;
    char adminPassword[CONFIG_PASSWORD_LEN];
    char guestPassword[CONFIG_PASSWORD_LEN];
    bool reportEnabled;
    uint8_t reportHour;
    uint8_t reportMinute;
    uint8_t reportDestPubKey[REPORT_PUBKEY_SIZE];
    bool alertEnabled;
    uint8_t alertDestPubKey[REPORT_PUBKEY_SIZE];
    uint8_t reserved[4];
};
#endif

// Default configuration - declared extern, defined in Config.cpp
extern const NodeConfig defaultConfig;

//=============================================================================
// Configuration Functions
//=============================================================================

void loadConfig();
void saveConfig();
void resetConfig();
