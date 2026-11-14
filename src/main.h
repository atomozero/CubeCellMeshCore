#pragma once
#include <Arduino.h>

/**
 * CubeCellMeshCore - MeshCore Compatible Repeater
 * Lightweight repeater firmware for Heltec CubeCell boards
 *
 * Based on MeshCore protocol: https://github.com/meshcore-dev/MeshCore
 */

#define FIRMWARE_VERSION    "0.8.0"

//=============================================================================
// Configuration
//=============================================================================

//#define SILENT                  // Disable ALL serial output
//#define LITE_MODE               // Remove non-essential features (direct messages) for space saving

// Node identity - set to 0 to auto-generate from chip ID
#define MC_NODE_ID          0

// Default location (set your coordinates here, or use 0 for none)
// These are used only on first boot, then stored in EEPROM
// Use "location LAT LON" serial command to change
#define MC_DEFAULT_LATITUDE     0.0f        // e.g., 45.464161
#define MC_DEFAULT_LONGITUDE    0.0f        // e.g., 9.191383
// Node name (max 15 chars) - leave empty to auto-generate from public key
#define MC_DEFAULT_NAME         ""          // e.g., "MyRepeater"

// Region presets - uncomment one
#define MC_REGION_EU868             // Europe 868MHz
//#define MC_REGION_US915           // USA 915MHz
//#define MC_REGION_AU915           // Australia 915MHz

// LoRa settings for MeshCore compatibility
// Default: MeshCore "EU/UK Narrow" preset (869.618 MHz from MeshCore app)
// Change MC_FREQUENCY to match your network if different
#ifdef MC_REGION_EU868
    #define MC_FREQUENCY        869.618f    // MHz - EU/UK Narrow (MeshCore app)
    #define MC_BANDWIDTH        62.5f       // kHz (narrow)
    #define MC_SPREADING        8           // SF8
    #define MC_CODING_RATE      8           // 4/8
    #define MC_TX_POWER         21          // dBm (max HTCC-AB01 hardware)
    #define MC_PREAMBLE_LEN     16
#endif

#ifdef MC_REGION_US915
    #define MC_FREQUENCY        910.525f    // MHz
    #define MC_BANDWIDTH        62.5f       // kHz (narrow)
    #define MC_SPREADING        7           // SF7
    #define MC_CODING_RATE      5           // 4/5
    #define MC_TX_POWER         21          // dBm (max HTCC-AB01 hardware)
    #define MC_PREAMBLE_LEN     16
#endif

#ifdef MC_REGION_AU915
    #define MC_FREQUENCY        916.525f    // MHz
    #define MC_BANDWIDTH        62.5f       // kHz (narrow)
    #define MC_CODING_RATE      5           // 4/5
    #define MC_TX_POWER         21          // dBm (max HTCC-AB01 hardware)
    #define MC_PREAMBLE_LEN     16
#endif

// Syncword for MeshCore (different from Meshtastic 0x2B)
#define MC_SYNCWORD         0x12

// Buffer sizes
#define MC_RX_BUFFER_SIZE   256
#define MC_TX_QUEUE_SIZE    4
#define MC_PACKET_ID_CACHE  32

// Timing
#define MC_TX_DELAY_MIN     50      // ms minimum delay before TX
#define MC_TX_DELAY_MAX     500     // ms maximum random delay
#define MC_MIN_RSSI_FORWARD -120    // dBm minimum RSSI to forward a packet

// Watchdog and error recovery
#define MC_WATCHDOG_ENABLED     true
#define MC_MAX_RADIO_ERRORS     5       // Reset radio after this many errors
#define MC_MAX_TOTAL_ERRORS     10      // Reboot after this many total errors

// Power saving
#define MC_DEEP_SLEEP_ENABLED   true
#define MC_RX_BOOST_ENABLED     false

// LED signaling
#define MC_SIGNAL_NEOPIXEL      // Use NeoPixel for status
//#define MC_SIGNAL_GPIO13      // Use GPIO13 LED (HTCC-AB02A)

//=============================================================================
// Debug output macros
//=============================================================================
#ifdef SILENT
    #define TAG_INFO    ""
    #define TAG_OK      ""
    #define TAG_WARN    ""
    #define TAG_ERROR   ""
    #define TAG_FATAL   ""
    #define TAG_RX      ""
    #define TAG_TX      ""
    #define TAG_FWD     ""
    #define TAG_NODE    ""
    #define TAG_RADIO   ""
    #define TAG_CONFIG  ""
    #define TAG_SYSTEM  ""
    #define TAG_PING    ""
    #define TAG_ADVERT  ""
    #define TAG_AUTH    ""
    #define TAG_DISCOVERY ""
    #define LOG(...)
    #define LOG_RAW(...)
    #define LOG_HEX(buf, len)
#else
    #define TAG_INFO    "[I]"
    #define TAG_OK      "[OK]"
    #define TAG_WARN    "[W]"
    #define TAG_ERROR   "[E]"
    #define TAG_FATAL   "[!]"
    #define TAG_RX      "[RX]"
    #define TAG_TX      "[TX]"
    #define TAG_FWD     "[FW]"
    #define TAG_NODE    "[N]"
    #define TAG_RADIO   "[R]"
    #define TAG_CONFIG  "[C]"
    #define TAG_SYSTEM  "[S]"
    #define TAG_PING    "[P]"
    #define TAG_ADVERT  "[A]"
    #define TAG_AUTH    "[AU]"
    #define TAG_DISCOVERY "[D]"
    inline void printTimestamp() {
        Serial.printf("%lu ", millis()/1000);
    }
    #define LOG(...) do { printTimestamp(); Serial.printf(__VA_ARGS__); } while(0)
    #define LOG_RAW(...) Serial.printf(__VA_ARGS__)
    #define LOG_HEX(buf, len) do { \
        for(uint16_t i=0; i<len; i++) Serial.printf("%02X", buf[i]); \
    } while(0)
#endif

//=============================================================================
// Includes
//=============================================================================
#include <RadioLib.h>
#include <EEPROM.h>
#include "mesh/Packet.h"
#include "mesh/Identity.h"
#include "mesh/Advert.h"
#include "mesh/Telemetry.h"
#include "mesh/Repeater.h"
#include "mesh/Crypto.h"
#include "mesh/Contacts.h"

//=============================================================================
// EEPROM Configuration
//=============================================================================
#define EEPROM_SIZE         584     // Config@0, Identity@128, Stats@280, Mailbox@340-523, Region@524-580
#define EEPROM_MAGIC        0xCC3C      // Magic number to validate config
#define EEPROM_VERSION      6           // Config version (6 = removed invalid region/scope fields)

// Password length (max 15 chars + null terminator)
#define CONFIG_PASSWORD_LEN 16

// Public key size for destinations
#define REPORT_PUBKEY_SIZE  32          // Ed25519 public key size

#ifndef NODECONFIG_DEFINED
#define NODECONFIG_DEFINED
struct NodeConfig {
    uint16_t magic;                         // Magic number to validate
    uint8_t version;                        // Config version
    uint8_t powerSaveMode;                  // 0=perf, 1=balanced, 2=powersave
    bool rxBoostEnabled;
    bool deepSleepEnabled;
    char adminPassword[CONFIG_PASSWORD_LEN]; // Admin password (persistent)
    char guestPassword[CONFIG_PASSWORD_LEN]; // Guest password (persistent)
    // Daily report settings (v3)
    bool reportEnabled;                     // Daily report enabled
    uint8_t reportHour;                     // Hour to send report (0-23)
    uint8_t reportMinute;                   // Minute to send report (0-59)
    uint8_t reportDestPubKey[REPORT_PUBKEY_SIZE]; // Destination public key
    // Node alert settings (v4)
    bool alertEnabled;                      // Node alert enabled
    uint8_t alertDestPubKey[REPORT_PUBKEY_SIZE]; // Alert destination public key
    // Loop detection & auto-add (v5)
    uint8_t loopDetectMode;                 // 0=off, 1=minimal, 2=moderate, 3=strict
    uint8_t autoAddMaxHops;                 // 0=no limit, 1-64=max hops for auto-add contacts
    uint8_t reserved[4];                    // Reserved for future use
};
#endif

// Default configuration - defined in core/Config.cpp
extern const NodeConfig defaultConfig;

//=============================================================================
// Persistent Statistics (EEPROM)
//=============================================================================
#define STATS_EEPROM_OFFSET     280     // After Identity (~260 bytes)
#define STATS_EEPROM_MAGIC      0x5754  // "ST" for stats
#define STATS_EEPROM_VERSION    1
#define STATS_SAVE_INTERVAL_MS  1800000 // Auto-save every 30 minutes

#ifndef PERSISTENTSTATS_DEFINED
#define PERSISTENTSTATS_DEFINED
struct PersistentStats {
    uint16_t magic;             // Magic number to validate
    uint8_t version;            // Stats version
    uint8_t reserved;           // Padding
    // Lifetime counters (never reset)
    uint32_t totalRxPackets;    // Total packets received
    uint32_t totalTxPackets;    // Total packets transmitted
    uint32_t totalFwdPackets;   // Total packets forwarded
    uint32_t totalUniqueNodes;  // Unique nodes ever seen
    uint32_t totalUptime;       // Total uptime in seconds
    uint32_t totalLogins;       // Total successful logins
    uint32_t totalLoginFails;   // Total failed login attempts
    uint32_t totalRateLimited;  // Total rate-limited requests
    uint16_t bootCount;         // Number of boots
    uint16_t lastBootReason;    // Last boot reason code
    uint32_t firstBootTime;     // Unix timestamp of first boot
    uint32_t lastSaveTime;      // Unix timestamp of last save
    // Checksum for integrity
    uint16_t checksum;          // CRC16 of data
};
#endif

//=============================================================================
// CubeCell specific
//=============================================================================
#ifdef CUBECELL

// Fix Heltec Arduino.h issues
#ifdef __cplusplus
#undef min
#undef max
#undef abs
#include <algorithm>
using std::abs;
using std::max;
using std::min;
#endif

// cyPm.c / innerWdt.h contain function definitions: included only by main.cpp
#include "cyPm.h"
void feedInnerWdt();   // defined in framework innerWdt.h (included by main.cpp)
extern uint32_t systime;

#endif // CUBECELL

//=============================================================================
// Include globals (extern declarations for all global variables)
//=============================================================================
#include "core/globals.h"

//=============================================================================
// Constants
//=============================================================================

// Boot safe period - disable deep sleep for first N seconds to allow serial commands
#define BOOT_SAFE_PERIOD_MS  120000  // 2 minutes

// ADVERT beacon timing
#define ADVERT_INTERVAL_MS      300000  // 5 minutes default
#define ADVERT_ENABLED          true
#define ADVERT_AFTER_SYNC_MS    5000    // Send ADVERT 5 seconds after time sync

//=============================================================================
// Function prototypes
//=============================================================================

// Core functions
void setupRadio();
void startReceive();
bool transmitPacket(MCPacket* pkt);
void processReceivedPacket(MCPacket* pkt);
bool shouldForward(MCPacket* pkt);
uint32_t generateNodeId();
void calculateTimings();
uint32_t calculatePacketAirtime(uint16_t packetLen);
uint8_t calcSnrScore(int8_t snr);
uint32_t calcRxDelay(uint8_t scoreIdx, uint32_t airtimeMs);
uint32_t calcTxJitter(uint32_t airtimeMs);
bool isActivelyReceiving();
void feedWatchdog();
void handleRadioError();

// Command hash function for dispatcher optimization
// Maps command strings to unique 32-bit hashes (no collisions for 108 commands)
static constexpr inline uint32_t hash_command(const char* s) {
    uint32_t h = 0;
    while (*s) {
        h = ((h << 5) + h) ^ (uint8_t)(*s++);
    }
    return h;
}

// Radio parameter helpers (for tempradio support) - inline for optimization
// Declared extern in main.cpp, but inlined here for zero-cost abstraction
extern bool tempRadioActive;
extern float tempFrequency, tempBandwidth;
extern uint8_t tempSpreadingFactor, tempCodingRate;

static inline float getCurrentFrequency() {
    return tempRadioActive ? tempFrequency : MC_FREQUENCY;
}
static inline float getCurrentBandwidth() {
    return tempRadioActive ? tempBandwidth : MC_BANDWIDTH;
}
static inline uint8_t getCurrentSpreadingFactor() {
    return tempRadioActive ? tempSpreadingFactor : MC_SPREADING;
}
static inline uint8_t getCurrentCodingRate() {
    return tempRadioActive ? tempCodingRate : MC_CODING_RATE;
}

// Power management
void enterDeepSleep();
void enterLightSleep(uint8_t ms);
void applyPowerSettings();

// LED signaling - see core/Led.h

// Serial commands
#ifndef SILENT
void checkSerial();
void processCommand(char* cmd);
#endif

// Configuration - see core/Config.h

// Ping / Test / Trace
void sendPing();
void sendDirectedPing(uint8_t targetHash);
void sendDirectedTrace(uint8_t targetHash);
uint32_t getPacketId(MCPacket* pkt);

// ADVERT
void sendAdvert(bool flood = true);
void checkAdvertBeacon();

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

// Messaging (mesh/Messaging.cpp)
#ifndef LITE_MODE
void sendDirectMessage(const char* recipientName, const char* message);
#endif
bool sendEncryptedToAdmin(const uint8_t* destPubKey, const char* text, uint16_t textLen);
bool sendNodeAlert(const char* nodeName, uint8_t nodeHash, uint8_t nodeType, int16_t rssi);
bool sendAlertAsChatNode(const char* text);
void healthCheck();

// Packet handling (mesh/PacketHandler.cpp)
bool processDiscoverRequest(MCPacket* pkt);
bool processAuthenticatedRequest(MCPacket* pkt);

// CLI (cli/Commands.cpp)
uint16_t processRemoteCommand(const char* cmd, char* response, uint16_t maxLen, bool isAdmin);

// Authentication
bool processAnonRequest(MCPacket* pkt);
bool processTxtMsgCLI(MCPacket* pkt);
bool sendLoginResponse(const uint8_t* clientPubKey, const uint8_t* sharedSecret,
                       bool isAdmin, uint8_t permissions, const uint8_t* outPath, uint8_t outPathLen);
