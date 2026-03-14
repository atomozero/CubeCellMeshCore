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
#define EEPROM_SIZE         576
#endif
#ifndef EEPROM_MAGIC
#define EEPROM_MAGIC        0xCC3C
#endif
#ifndef EEPROM_VERSION
#define EEPROM_VERSION      6
#endif
#ifndef CONFIG_PASSWORD_LEN
#define CONFIG_PASSWORD_LEN 16
#endif


//=============================================================================
// Loop Detection Modes
//=============================================================================
#define LOOP_DETECT_OFF       0  // No loop detection (allow unlimited)
#define LOOP_DETECT_MINIMAL   1  // Minimal: allow 4+ repeats in path
#define LOOP_DETECT_MODERATE  2  // Moderate: allow 2+ repeats
#define LOOP_DETECT_STRICT    3  // Strict: allow only 1 occurrence (original behavior)

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
    uint8_t loopDetectMode;   // 0=off, 1=minimal, 2=moderate, 3=strict
    uint8_t autoAddMaxHops;   // 0=no limit, 1-64=max hops for auto-add contacts
    uint8_t maxFloodHops;       // 0=default(8), 1-15=max flood hops
    uint8_t advertIntervalMin;  // 0=default(5), 1-240=advert interval in minutes
    uint8_t floodAdvertHrs;     // 0=auto, 3-48=flood advert interval in hours
    uint8_t reserved1;          // Reserved for future use
};
#endif

// Default configuration - declared extern, defined in Config.cpp
extern const NodeConfig defaultConfig;

//=============================================================================
// Persistent Statistics defines
//=============================================================================
#ifndef STATS_EEPROM_OFFSET
#define STATS_EEPROM_OFFSET     280
#endif
#ifndef STATS_EEPROM_MAGIC
#define STATS_EEPROM_MAGIC      0x5754
#endif
#ifndef STATS_EEPROM_VERSION
#define STATS_EEPROM_VERSION    1
#endif
#ifndef STATS_SAVE_INTERVAL_MS
#define STATS_SAVE_INTERVAL_MS  300000
#endif

#ifndef PERSISTENTSTATS_DEFINED
#define PERSISTENTSTATS_DEFINED
struct PersistentStats {
    uint16_t magic;
    uint8_t version;
    uint8_t reserved;
    uint32_t totalRxPackets;
    uint32_t totalTxPackets;
    uint32_t totalFwdPackets;
    uint32_t totalUniqueNodes;
    uint32_t totalUptime;
    uint32_t totalLogins;
    uint32_t totalLoginFails;
    uint32_t totalRateLimited;
    uint16_t bootCount;
    uint16_t lastBootReason;
    uint32_t firstBootTime;
    uint32_t lastSaveTime;
    uint16_t checksum;
};
#endif

//=============================================================================
// Configuration Functions
//=============================================================================

void loadConfig();
void saveConfig();
void resetConfig();

//=============================================================================
// Persistent Statistics Functions
//=============================================================================

void loadPersistentStats();
void savePersistentStats();
void checkStatsSave();

// Stat recording functions
void statsRecordRx();
void statsRecordTx();
void statsRecordFwd();
void statsRecordUniqueNode();
void statsRecordLogin();
void statsRecordLoginFail();
void statsRecordRateLimited();
void statsSetFirstBootTime(uint32_t unixTime);
uint32_t statsGetTotalUptime();
const PersistentStats* getPersistentStats();
