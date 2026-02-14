/**
 * globals.cpp - Global variable definitions for CubeCellMeshCore
 *
 * This file contains the actual definitions of all global variables
 * declared as extern in globals.h
 */

#include "globals.h"

// CubeCell specific includes for radio
#ifdef CUBECELL
#include "CubeCell_NeoPixel.h"
#endif

// Default values from configuration
#ifndef MC_DEEP_SLEEP_ENABLED
#define MC_DEEP_SLEEP_ENABLED true
#endif
#ifndef MC_RX_BOOST_ENABLED
#define MC_RX_BOOST_ENABLED false
#endif
#ifndef MC_NODE_ID
#define MC_NODE_ID 0
#endif

//=============================================================================
// Radio instance
//=============================================================================
SX1262 radio = new Module(RADIOLIB_BUILTIN_MODULE);

//=============================================================================
// NeoPixel
//=============================================================================
#ifdef MC_SIGNAL_NEOPIXEL
CubeCell_NeoPixel pixels(1, RGB, NEO_GRB + NEO_KHZ800);
#endif

//=============================================================================
// Radio state
//=============================================================================
volatile bool dio1Flag = false;
bool isReceiving = false;
int radioError = RADIOLIB_ERR_NONE;

//=============================================================================
// Temporary radio parameters
//=============================================================================
bool tempRadioActive = false;
float tempFrequency = 0;
float tempBandwidth = 0;
uint8_t tempSpreadingFactor = 0;
uint8_t tempCodingRate = 0;

//=============================================================================
// Power saving
//=============================================================================
bool deepSleepEnabled = MC_DEEP_SLEEP_ENABLED;
bool rxBoostEnabled = MC_RX_BOOST_ENABLED;
uint8_t powerSaveMode = 1;

//=============================================================================
// Timing
//=============================================================================
uint32_t bootTime = 0;
uint32_t pendingAdvertTime = 0;
uint32_t activeReceiveStart = 0;
uint32_t preambleTimeMsec = 50;
uint32_t maxPacketTimeMsec = 500;
uint32_t slotTimeMsec = 20;

//=============================================================================
// Statistics
//=============================================================================
uint32_t rxCount = 0;
uint32_t txCount = 0;
uint32_t fwdCount = 0;
uint32_t errCount = 0;
uint32_t crcErrCount = 0;
uint32_t advTxCount = 0;
uint32_t advRxCount = 0;

//=============================================================================
// Last packet info
//=============================================================================
int16_t lastRssi = 0;
int8_t lastSnr = 0;

//=============================================================================
// Error recovery
//=============================================================================
uint8_t radioErrorCount = 0;

//=============================================================================
// Pending reboot
//=============================================================================
bool pendingReboot = false;
uint32_t rebootTime = 0;

//=============================================================================
// Daily report configuration
//=============================================================================
bool reportEnabled = false;
uint8_t reportHour = 8;
uint8_t reportMinute = 0;
uint8_t reportDestPubKey[REPORT_PUBKEY_SIZE] = {0};
uint32_t lastReportDay = 0;

//=============================================================================
// Node alert configuration
//=============================================================================
bool alertEnabled = false;
uint8_t alertDestPubKey[REPORT_PUBKEY_SIZE] = {0};

//=============================================================================
// Node ID
//=============================================================================
uint32_t nodeId = MC_NODE_ID;

//=============================================================================
// Packet ID cache implementation
//=============================================================================
void PacketIdCache::clear() {
    memset(ids, 0, sizeof(ids));
    pos = 0;
}

bool PacketIdCache::addIfNew(uint32_t id) {
    for (uint8_t i = 0; i < MC_PACKET_ID_CACHE; i++) {
        if (ids[i] == id) return false;
    }
    ids[pos] = id;
    pos = (pos + 1) % MC_PACKET_ID_CACHE;
    return true;
}

PacketIdCache packetCache;

//=============================================================================
// Seen Nodes Tracker implementation
//=============================================================================
void SeenNodesTracker::clear() {
    memset(nodes, 0, sizeof(nodes));
    count = 0;
}

bool SeenNodesTracker::update(uint8_t hash, int16_t rssi, int8_t snr, const char* name) {
    for (uint8_t i = 0; i < count; i++) {
        if (nodes[i].hash == hash) {
            nodes[i].lastRssi = rssi;
            nodes[i].lastSnr = snr;
            // EMA: avg = avg*7/8 + snr/8
            nodes[i].snrAvg = (int8_t)(((int16_t)nodes[i].snrAvg * 7 + snr) / 8);
            nodes[i].offlineAlerted = false;  // Node is back
            nodes[i].lastSeen = millis();
            if (nodes[i].pktCount < 255) nodes[i].pktCount++;
            if (name && name[0] != '\0' && nodes[i].name[0] == '\0') {
                strncpy(nodes[i].name, name, sizeof(nodes[i].name) - 1);
                nodes[i].name[sizeof(nodes[i].name) - 1] = '\0';
            }
            return false;
        }
    }

    if (count < MC_MAX_SEEN_NODES) {
        nodes[count].hash = hash;
        nodes[count].lastRssi = rssi;
        nodes[count].lastSnr = snr;
        nodes[count].snrAvg = snr;
        nodes[count].offlineAlerted = false;
        nodes[count].pktCount = 1;
        nodes[count].lastSeen = millis();
        if (name && name[0] != '\0') {
            strncpy(nodes[count].name, name, sizeof(nodes[count].name) - 1);
            nodes[count].name[sizeof(nodes[count].name) - 1] = '\0';
        } else {
            nodes[count].name[0] = '\0';
        }
        count++;
        return true;
    } else {
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
        nodes[oldest].snrAvg = snr;
        nodes[oldest].offlineAlerted = false;
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

uint8_t SeenNodesTracker::getCount() const { return count; }

const SeenNode* SeenNodesTracker::getNode(uint8_t idx) const {
    if (idx < count) return &nodes[idx];
    return nullptr;
}

SeenNodesTracker seenNodes;

//=============================================================================
// TX Queue implementation
//=============================================================================
void TxQueue::clear() {
    count = 0;
    for (uint8_t i = 0; i < MC_TX_QUEUE_SIZE; i++) {
        queue[i].clear();
    }
}

bool TxQueue::add(const MCPacket* pkt) {
    if (count >= MC_TX_QUEUE_SIZE) {
        for (uint8_t i = 0; i < MC_TX_QUEUE_SIZE - 1; i++) {
            queue[i] = queue[i + 1];
        }
        count = MC_TX_QUEUE_SIZE - 1;
    }
    queue[count++] = *pkt;
    return true;
}

bool TxQueue::pop(MCPacket* pkt) {
    if (count == 0) return false;
    *pkt = queue[0];
    for (uint8_t i = 0; i < count - 1; i++) {
        queue[i] = queue[i + 1];
    }
    count--;
    return true;
}

uint8_t TxQueue::getCount() const { return count; }

TxQueue txQueue;

//=============================================================================
// Global Managers
//=============================================================================
IdentityManager nodeIdentity;
TimeSync timeSync;
AdvertGenerator advertGen;
TelemetryManager telemetry;
RepeaterHelper repeaterHelper;
#ifdef ENABLE_PACKET_LOG
PacketLogger packetLogger;
#endif
SessionManager sessionManager;
MeshCrypto meshCrypto;
ContactManager contactMgr;
MessageCrypto msgCrypto;

//=============================================================================
// ISR callback
//=============================================================================
void onDio1Rise() {
    dio1Flag = true;
}
