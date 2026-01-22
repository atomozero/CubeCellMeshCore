#pragma once
#include <Arduino.h>

/**
 * CubeCellMeshCore - MeshCore Compatible Repeater
 * Lightweight repeater firmware for Heltec CubeCell boards
 *
 * Based on MeshCore protocol: https://github.com/meshcore-dev/MeshCore
 */

#define FIRMWARE_VERSION    "0.2.8"

//=============================================================================
// Configuration
//=============================================================================

//#define SILENT                  // Disable ALL serial output
#define LITE_MODE                 // Remove non-essential features for space saving
#define MINIMAL_DEBUG             // Minimal debug output (no fancy ANSI tables)
#define ANSI_COLORS               // Enable ANSI colors even with MINIMAL_DEBUG

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
    #define MC_TX_POWER         14          // dBm (EU limit)
    #define MC_PREAMBLE_LEN     16
#endif

#ifdef MC_REGION_US915
    #define MC_FREQUENCY        910.525f    // MHz
    #define MC_BANDWIDTH        62.5f       // kHz (narrow)
    #define MC_SPREADING        7           // SF7
    #define MC_CODING_RATE      5           // 4/5
    #define MC_TX_POWER         20          // dBm
    #define MC_PREAMBLE_LEN     16
#endif

#ifdef MC_REGION_AU915
    #define MC_FREQUENCY        916.525f    // MHz
    #define MC_BANDWIDTH        62.5f       // kHz (narrow)
    #define MC_CODING_RATE      5           // 4/5
    #define MC_TX_POWER         20          // dBm
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
    // No output at all
    #define ANSI_RESET ""
    #define ANSI_BOLD ""
    #define ANSI_DIM ""
    #define ANSI_RED ""
    #define ANSI_GREEN ""
    #define ANSI_YELLOW ""
    #define ANSI_BLUE ""
    #define ANSI_MAGENTA ""
    #define ANSI_CYAN ""
    #define ANSI_WHITE ""
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
#elif defined(MINIMAL_DEBUG)
    // Minimal output with optional ANSI colors, no fancy formatting
    #ifdef ANSI_COLORS
    #define ANSI_RESET      "\033[0m"
    #define ANSI_BOLD       "\033[1m"
    #define ANSI_DIM        "\033[2m"
    #define ANSI_RED        "\033[31m"
    #define ANSI_GREEN      "\033[32m"
    #define ANSI_YELLOW     "\033[33m"
    #define ANSI_BLUE       "\033[34m"
    #define ANSI_MAGENTA    "\033[35m"
    #define ANSI_CYAN       "\033[36m"
    #define ANSI_WHITE      "\033[37m"
    #define TAG_INFO    ANSI_CYAN "[I]" ANSI_RESET
    #define TAG_OK      ANSI_GREEN "[OK]" ANSI_RESET
    #define TAG_WARN    ANSI_YELLOW "[W]" ANSI_RESET
    #define TAG_ERROR   ANSI_RED "[E]" ANSI_RESET
    #define TAG_FATAL   ANSI_BOLD ANSI_RED "[!]" ANSI_RESET
    #define TAG_RX      ANSI_GREEN "[RX]" ANSI_RESET
    #define TAG_TX      ANSI_MAGENTA "[TX]" ANSI_RESET
    #define TAG_FWD     ANSI_BLUE "[FW]" ANSI_RESET
    #define TAG_NODE    ANSI_YELLOW "[N]" ANSI_RESET
    #define TAG_RADIO   ANSI_CYAN "[R]" ANSI_RESET
    #define TAG_CONFIG  ANSI_WHITE "[C]" ANSI_RESET
    #define TAG_SYSTEM  ANSI_BOLD ANSI_CYAN "[S]" ANSI_RESET
    #define TAG_PING    ANSI_MAGENTA "[P]" ANSI_RESET
    #define TAG_ADVERT  ANSI_YELLOW "[A]" ANSI_RESET
    #define TAG_AUTH    ANSI_CYAN "[AU]" ANSI_RESET
    #define TAG_DISCOVERY ANSI_BLUE "[D]" ANSI_RESET
    #else
    #define ANSI_RESET ""
    #define ANSI_BOLD ""
    #define ANSI_DIM ""
    #define ANSI_RED ""
    #define ANSI_GREEN ""
    #define ANSI_YELLOW ""
    #define ANSI_BLUE ""
    #define ANSI_MAGENTA ""
    #define ANSI_CYAN ""
    #define ANSI_WHITE ""
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
    #endif
    inline void printTimestamp() {
        Serial.printf("%lu ", millis()/1000);
    }
    #define LOG(...) do { printTimestamp(); Serial.printf(__VA_ARGS__); } while(0)
    #define LOG_RAW(...) Serial.printf(__VA_ARGS__)
    #define LOG_HEX(buf, len) do { \
        for(uint16_t i=0; i<len; i++) Serial.printf("%02X", buf[i]); \
    } while(0)
#else
    // Full ANSI color output
    #define ANSI_RESET      "\033[0m"
    #define ANSI_BOLD       "\033[1m"
    #define ANSI_DIM        "\033[2m"
    #define ANSI_RED        "\033[31m"
    #define ANSI_GREEN      "\033[32m"
    #define ANSI_YELLOW     "\033[33m"
    #define ANSI_BLUE       "\033[34m"
    #define ANSI_MAGENTA    "\033[35m"
    #define ANSI_CYAN       "\033[36m"
    #define ANSI_WHITE      "\033[37m"
    #define TAG_INFO    ANSI_CYAN "[INFO]" ANSI_RESET
    #define TAG_OK      ANSI_GREEN "[OK]" ANSI_RESET
    #define TAG_WARN    ANSI_YELLOW "[WARN]" ANSI_RESET
    #define TAG_ERROR   ANSI_RED "[ERROR]" ANSI_RESET
    #define TAG_FATAL   ANSI_BOLD ANSI_RED "[FATAL]" ANSI_RESET
    #define TAG_RX      ANSI_GREEN "[RX]" ANSI_RESET
    #define TAG_TX      ANSI_MAGENTA "[TX]" ANSI_RESET
    #define TAG_FWD     ANSI_BLUE "[FWD]" ANSI_RESET
    #define TAG_NODE    ANSI_YELLOW "[NODE]" ANSI_RESET
    #define TAG_RADIO   ANSI_CYAN "[RADIO]" ANSI_RESET
    #define TAG_CONFIG  ANSI_WHITE "[CONFIG]" ANSI_RESET
    #define TAG_SYSTEM  ANSI_BOLD ANSI_CYAN "[SYSTEM]" ANSI_RESET
    #define TAG_PING    ANSI_MAGENTA "[PING]" ANSI_RESET
    #define TAG_ADVERT  ANSI_YELLOW "[ADVERT]" ANSI_RESET
    #define TAG_AUTH    ANSI_CYAN "[AUTH]" ANSI_RESET
    #define TAG_DISCOVERY ANSI_BLUE "[DISC]" ANSI_RESET
    inline void printTimestamp() {
        uint32_t sec = millis() / 1000;
        uint32_t min = sec / 60;
        uint32_t hr = min / 60;
        Serial.printf(ANSI_DIM "%02lu:%02lu:%02lu" ANSI_RESET " ", hr % 100, min % 60, sec % 60);
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
#define EEPROM_SIZE         256     // Increased for Identity storage
#define EEPROM_MAGIC        0xCC3C      // Magic number to validate config
#define EEPROM_VERSION      4           // Config version (4 = added node alert)

// Password length (max 15 chars + null terminator)
#define CONFIG_PASSWORD_LEN 16

// Public key size for destinations
#define REPORT_PUBKEY_SIZE  32          // Ed25519 public key size

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
    uint8_t reserved[4];                    // Reserved for future use
};

// Default configuration
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

#include "cyPm.c"
#include "innerWdt.h"
extern uint32_t systime;

// Radio instance
SX1262 radio = new Module(RADIOLIB_BUILTIN_MODULE);

// NeoPixel
#ifdef MC_SIGNAL_NEOPIXEL
#include "CubeCell_NeoPixel.h"
CubeCell_NeoPixel pixels(1, RGB, NEO_GRB + NEO_KHZ800);
#endif

#endif // CUBECELL

//=============================================================================
// Global state
//=============================================================================

// Radio state
volatile bool dio1Flag = false;
bool isReceiving = false;
int radioError = RADIOLIB_ERR_NONE;

// Power saving
bool deepSleepEnabled = MC_DEEP_SLEEP_ENABLED;
bool rxBoostEnabled = MC_RX_BOOST_ENABLED;
uint8_t powerSaveMode = 1;  // 0=perf, 1=balanced, 2=powersave

// Boot safe period - disable deep sleep for first N seconds to allow serial commands
#define BOOT_SAFE_PERIOD_MS  120000  // 2 minutes
uint32_t bootTime = 0;

// Pending ADVERT after time sync
uint32_t pendingAdvertTime = 0;  // 0 = no pending, >0 = millis() when to send

// Statistics
uint32_t rxCount = 0;
uint32_t txCount = 0;
uint32_t fwdCount = 0;      // Forwarded packets
uint32_t errCount = 0;      // Error count
uint32_t crcErrCount = 0;   // CRC errors
uint32_t advTxCount = 0;    // ADVERT packets sent
uint32_t advRxCount = 0;    // ADVERT packets received

// Last packet info
int16_t lastRssi = 0;
int8_t lastSnr = 0;

// Error recovery
uint8_t radioErrorCount = 0;

// Pending reboot (for CLI reboot command)
bool pendingReboot = false;
uint32_t rebootTime = 0;

// Daily report configuration
bool reportEnabled = false;
uint8_t reportHour = 8;
uint8_t reportMinute = 0;
uint8_t reportDestPubKey[REPORT_PUBKEY_SIZE] = {0};
uint32_t lastReportDay = 0;  // Day of last sent report (to avoid duplicates)

// Node alert configuration
bool alertEnabled = false;
uint8_t alertDestPubKey[REPORT_PUBKEY_SIZE] = {0};

// Node ID (generated at startup if MC_NODE_ID is 0)
uint32_t nodeId = MC_NODE_ID;

// Timing for actively receiving detection
uint32_t activeReceiveStart = 0;
uint32_t preambleTimeMsec = 50;     // Calculated at startup
uint32_t maxPacketTimeMsec = 500;   // Calculated at startup
uint32_t slotTimeMsec = 20;         // Calculated at startup

//=============================================================================
// Packet ID cache (to avoid re-forwarding)
//=============================================================================
class PacketIdCache {
private:
    uint32_t ids[MC_PACKET_ID_CACHE];
    uint8_t pos;

public:
    void clear() {
        memset(ids, 0, sizeof(ids));
        pos = 0;
    }

    // Returns true if ID is new (not seen before)
    bool addIfNew(uint32_t id) {
        // Check if already seen
        for (uint8_t i = 0; i < MC_PACKET_ID_CACHE; i++) {
            if (ids[i] == id) return false;
        }
        // Add new ID
        ids[pos] = id;
        pos = (pos + 1) % MC_PACKET_ID_CACHE;
        return true;
    }
};

PacketIdCache packetCache;

//=============================================================================
// Seen Nodes Tracker
//=============================================================================
#define MC_MAX_SEEN_NODES   16

struct SeenNode {
    uint8_t hash;           // 1-byte node hash from path
    int16_t lastRssi;       // Last RSSI
    int8_t lastSnr;         // Last SNR (x4 for 0.25dB resolution)
    uint8_t pktCount;       // Packets seen from this node (saturates at 255)
    uint32_t lastSeen;      // millis() when last seen
    char name[12];          // Node name (truncated, from ADVERT)
};

class SeenNodesTracker {
private:
    SeenNode nodes[MC_MAX_SEEN_NODES];
    uint8_t count;

public:
    void clear() {
        memset(nodes, 0, sizeof(nodes));
        count = 0;
    }

    // Update or add a node, returns true if new node
    bool update(uint8_t hash, int16_t rssi, int8_t snr, const char* name = nullptr) {
        // Check if already known
        for (uint8_t i = 0; i < count; i++) {
            if (nodes[i].hash == hash) {
                nodes[i].lastRssi = rssi;
                nodes[i].lastSnr = snr;
                nodes[i].lastSeen = millis();
                if (nodes[i].pktCount < 255) nodes[i].pktCount++;
                // Update name if provided and node doesn't have one yet
                if (name && name[0] != '\0' && nodes[i].name[0] == '\0') {
                    strncpy(nodes[i].name, name, sizeof(nodes[i].name) - 1);
                    nodes[i].name[sizeof(nodes[i].name) - 1] = '\0';
                }
                return false;  // Not new
            }
        }

        // Add new node
        if (count < MC_MAX_SEEN_NODES) {
            nodes[count].hash = hash;
            nodes[count].lastRssi = rssi;
            nodes[count].lastSnr = snr;
            nodes[count].pktCount = 1;
            nodes[count].lastSeen = millis();
            if (name && name[0] != '\0') {
                strncpy(nodes[count].name, name, sizeof(nodes[count].name) - 1);
                nodes[count].name[sizeof(nodes[count].name) - 1] = '\0';
            } else {
                nodes[count].name[0] = '\0';
            }
            count++;
            return true;  // New node
        } else {
            // Replace oldest node
            uint8_t oldest = 0;
            uint32_t oldestTime = nodes[0].lastSeen;
            for (uint8_t i = 1; i < MC_MAX_SEEN_NODES; i++) {
                if (nodes[i].lastSeen < oldestTime) {
                    oldest = i;
                    oldestTime = nodes[i].lastSeen;
                }
            }
            nodes[oldest].hash = hash;
            nodes[oldest].lastRssi = rssi;
            nodes[oldest].lastSnr = snr;
            nodes[oldest].pktCount = 1;
            nodes[oldest].lastSeen = millis();
            if (name && name[0] != '\0') {
                strncpy(nodes[oldest].name, name, sizeof(nodes[oldest].name) - 1);
                nodes[oldest].name[sizeof(nodes[oldest].name) - 1] = '\0';
            } else {
                nodes[oldest].name[0] = '\0';
            }
            return true;
        }
    }

    uint8_t getCount() const { return count; }

    const SeenNode* getNode(uint8_t idx) const {
        if (idx < count) return &nodes[idx];
        return nullptr;
    }
};

SeenNodesTracker seenNodes;

// Contact management and message crypto
ContactManager contactMgr;
MessageCrypto msgCrypto;

//=============================================================================
// TX Queue
//=============================================================================
class TxQueue {
private:
    MCPacket queue[MC_TX_QUEUE_SIZE];
    uint8_t count;

public:
    void clear() {
        count = 0;
        for (uint8_t i = 0; i < MC_TX_QUEUE_SIZE; i++) {
            queue[i].clear();
        }
    }

    bool add(const MCPacket* pkt) {
        if (count >= MC_TX_QUEUE_SIZE) {
            // Queue full, drop oldest
            for (uint8_t i = 0; i < MC_TX_QUEUE_SIZE - 1; i++) {
                queue[i] = queue[i + 1];
            }
            count = MC_TX_QUEUE_SIZE - 1;
        }
        queue[count++] = *pkt;
        return true;
    }

    bool pop(MCPacket* pkt) {
        if (count == 0) return false;
        *pkt = queue[0];
        // Shift remaining
        for (uint8_t i = 0; i < count - 1; i++) {
            queue[i] = queue[i + 1];
        }
        count--;
        return true;
    }

    uint8_t getCount() const { return count; }
};

TxQueue txQueue;

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
uint32_t getTxDelayWeighted(int8_t snr);
bool isActivelyReceiving();
void feedWatchdog();
void handleRadioError();

// Power management
void enterDeepSleep();
void enterLightSleep(uint8_t ms);
void applyPowerSettings();

// LED signaling
void initLed();
void ledRxOn();
void ledTxOn();
void ledRedSolid();
void ledGreenBlink();
void ledBlueDoubleBlink();
void ledOff();

// Serial commands
#ifndef SILENT
void checkSerial();
void processCommand(char* cmd);
#endif

// Configuration
void loadConfig();
void saveConfig();
void resetConfig();

// Ping / Test
void sendPing();
uint32_t getPacketId(MCPacket* pkt);

// ADVERT
void sendAdvert(bool flood = true);
void checkAdvertBeacon();

// Authentication
bool processAnonRequest(MCPacket* pkt);
bool sendLoginResponse(const uint8_t* clientPubKey, const uint8_t* sharedSecret,
                       bool isAdmin, uint8_t permissions, const uint8_t* outPath, uint8_t outPathLen);

//=============================================================================
// Global Managers
//=============================================================================
IdentityManager nodeIdentity;
TimeSync timeSync;
AdvertGenerator advertGen;
TelemetryManager telemetry;
RepeaterHelper repeaterHelper;
PacketLogger packetLogger;
SessionManager sessionManager;
MeshCrypto meshCrypto;

// ADVERT beacon timing
#define ADVERT_INTERVAL_MS      300000  // 5 minutes default
#define ADVERT_ENABLED          true
#define ADVERT_AFTER_SYNC_MS    5000    // Send ADVERT 5 seconds after time sync

// ISR - CubeCell doesn't use IRAM_ATTR
void onDio1Rise() {
    dio1Flag = true;
}
