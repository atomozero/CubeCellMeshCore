/**
 * globals.h - Global variable declarations for CubeCellMeshCore
 *
 * This file contains extern declarations for all global variables.
 * The actual definitions are in globals.cpp
 */

#pragma once

#include <Arduino.h>
#include <RadioLib.h>

//=============================================================================
// Configuration constants
//=============================================================================
#define REPORT_PUBKEY_SIZE      32
#define MC_TX_QUEUE_SIZE        4
#define MC_PACKET_ID_CACHE      32
#define MC_MAX_SEEN_NODES       16

// LED signaling - must be defined here for both main.cpp and globals.cpp
#define MC_SIGNAL_NEOPIXEL      // Use NeoPixel for status
//#define MC_SIGNAL_GPIO13      // Use GPIO13 LED (HTCC-AB02A)

// Power saving defaults
#ifndef MC_DEEP_SLEEP_ENABLED
#define MC_DEEP_SLEEP_ENABLED   true
#endif
#ifndef MC_RX_BOOST_ENABLED
#define MC_RX_BOOST_ENABLED     false
#endif
#ifndef MC_NODE_ID
#define MC_NODE_ID              0
#endif

// Mesh types - order matters for dependencies!
#include "../mesh/Packet.h"
#include "../mesh/Identity.h"
#include "../mesh/Advert.h"
#include "../mesh/Contacts.h"
#include "../mesh/Telemetry.h"
#include "../mesh/Repeater.h"    // Must be before Crypto.h (defines PERM_ACL_*)
#include "../mesh/Crypto.h"
#include "../mesh/Mailbox.h"
#include "../mesh/RegionMap.h"

//=============================================================================
// Radio instance
//=============================================================================
extern SX1262 radio;

//=============================================================================
// NeoPixel (CubeCell specific)
//=============================================================================
#ifdef MC_SIGNAL_NEOPIXEL
#include "CubeCell_NeoPixel.h"
extern CubeCell_NeoPixel pixels;
#endif

//=============================================================================
// Radio state
//=============================================================================
extern volatile bool dio1Flag;
extern bool isReceiving;
extern int radioError;

//=============================================================================
// Temporary radio parameters (not saved to EEPROM)
//=============================================================================
extern bool tempRadioActive;        // true if using temporary parameters
extern float tempFrequency;         // MHz
extern float tempBandwidth;         // kHz
extern uint8_t tempSpreadingFactor; // 6-12
extern uint8_t tempCodingRate;      // 5-8 (for 4/5 to 4/8)
extern uint32_t tempRadioExpireTime; // millis() when temp radio auto-expires (0=never)

//=============================================================================
// Configurable forwarding delays (percentage: 100 = 1.0x default)
//=============================================================================
extern uint32_t floodAdvertIntervalMs;   // Separate flood ADVERT interval (0=use local)
extern uint32_t lastFloodAdvertTime;     // Last time flood ADVERT was sent
extern uint16_t configTxDelayFactor;     // TX jitter factor (0-500, default 100)
extern uint16_t configRxDelayFactor;     // RX delay factor (0-500, default 100)
extern uint16_t configDirectTxDelay;     // Direct TX delay factor (0-500, default 100)
extern uint8_t configAirtimeFactor;      // Airtime/duty cycle factor (tenths: 10=1.0x, 0=off, max 90=9.0)
extern uint8_t configAdcMultiplier;      // Battery ADC calibration (tenths: 10=1.0x, 0=auto, max 100=10.0)
extern uint16_t configAgcResetInterval;  // AGC reset interval in seconds (0=disabled, multiples of 4)
extern uint32_t lastAgcResetTime;        // Last AGC reset timestamp

//=============================================================================
// Power saving
//=============================================================================
extern bool deepSleepEnabled;
extern bool rxBoostEnabled;
extern uint8_t powerSaveMode;

//=============================================================================
// Loop Detection
//=============================================================================
extern uint8_t loopDetectMode;  // 0=off, 1=minimal, 2=moderate, 3=strict

//=============================================================================
// Auto-add Max Hops Filter
//=============================================================================
extern uint8_t autoAddMaxHops;  // 0=no limit, 1-64=max hops for auto-add contacts

//=============================================================================
// Timing
//=============================================================================
extern uint32_t bootTime;
extern uint32_t pendingAdvertTime;
extern uint32_t activeReceiveStart;
extern uint32_t preambleTimeMsec;
extern uint32_t maxPacketTimeMsec;
extern uint32_t slotTimeMsec;

//=============================================================================
// Statistics
//=============================================================================
extern uint32_t rxCount;
extern uint32_t txCount;
extern uint32_t fwdCount;
extern uint32_t errCount;
extern uint32_t crcErrCount;
extern uint32_t advTxCount;
extern uint32_t advRxCount;

//=============================================================================
// Last packet info
//=============================================================================
extern int16_t lastRssi;
extern int8_t lastSnr;

//=============================================================================
// Error recovery
//=============================================================================
extern uint8_t radioErrorCount;

//=============================================================================
// Pending reboot
//=============================================================================
extern bool pendingReboot;
extern uint32_t rebootTime;

//=============================================================================
// Daily report configuration
//=============================================================================
extern bool reportEnabled;
extern uint8_t reportHour;
extern uint8_t reportMinute;
extern uint8_t reportDestPubKey[REPORT_PUBKEY_SIZE];
extern uint32_t lastReportDay;

//=============================================================================
// Node alert configuration
//=============================================================================
extern bool alertEnabled;
extern uint8_t alertDestPubKey[REPORT_PUBKEY_SIZE];

//=============================================================================
// Node ID
//=============================================================================
extern uint32_t nodeId;

//=============================================================================
// Owner info
//=============================================================================
#define MC_OWNER_INFO_MAX 32
extern char ownerInfo[MC_OWNER_INFO_MAX];

//=============================================================================
// Packet ID cache class
//=============================================================================
class PacketIdCache {
private:
    uint32_t ids[MC_PACKET_ID_CACHE];
    uint8_t pos;

public:
    void clear();
    bool addIfNew(uint32_t id);
};

extern PacketIdCache packetCache;

//=============================================================================
// Seen Nodes Tracker
//=============================================================================
struct SeenNode {
    uint8_t hash;
    int16_t lastRssi;
    int8_t lastSnr;
    int8_t snrAvg;          // EMA of SNR (x4), for health monitoring
    uint8_t pktCount;
    uint32_t lastSeen;
    char name[12];
    bool offlineAlerted;    // true if offline alert already sent
};

class SeenNodesTracker {
private:
    SeenNode nodes[MC_MAX_SEEN_NODES];
    uint8_t count;

public:
    void clear();
    bool update(uint8_t hash, int16_t rssi, int8_t snr, const char* name = nullptr);
    uint8_t getCount() const;
    const SeenNode* getNode(uint8_t idx) const;
};

extern SeenNodesTracker seenNodes;

//=============================================================================
// TX Queue
//=============================================================================
class TxQueue {
private:
    MCPacket queue[MC_TX_QUEUE_SIZE];
    uint8_t count;

public:
    void clear();
    bool add(const MCPacket* pkt);
    bool pop(MCPacket* pkt);
    uint8_t getCount() const;
};

extern TxQueue txQueue;

//=============================================================================
// Global Managers
//=============================================================================
extern IdentityManager nodeIdentity;
extern TimeSync timeSync;
extern AdvertGenerator advertGen;
extern TelemetryManager telemetry;
extern RepeaterHelper repeaterHelper;
extern SessionManager sessionManager;
extern MeshCrypto meshCrypto;
extern ContactManager contactMgr;
extern MessageCrypto msgCrypto;
extern Mailbox mailbox;
extern RegionMap regionMap;

//=============================================================================
// ISR callback
//=============================================================================
void onDio1Rise();
