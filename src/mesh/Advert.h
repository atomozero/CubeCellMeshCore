#pragma once
#include <Arduino.h>
#include "Packet.h"
#include "Identity.h"

/**
 * MeshCore ADVERT Packet Generation
 *
 * ADVERT payload structure:
 * [0-31]   Public Key (32 bytes)
 * [32-35]  Timestamp (4 bytes, little-endian)
 * [36-99]  Signature (64 bytes) - signs: pubkey + timestamp + appdata
 * [100]    Flags (1 byte)
 * [101-104] Latitude (4 bytes, optional)
 * [105-108] Longitude (4 bytes, optional)
 * [109+]   Name (variable, optional)
 */

/**
 * Time synchronization from received ADVERTs
 * Since CubeCell has no RTC/WiFi, we sync time from other nodes
 * - First ADVERT: sync immediately
 * - Already synced: requires 2 different timestamps within 1 hour to re-sync
 */
class TimeSync {
private:
    uint32_t baseTimestamp;      // Unix timestamp when synced
    uint32_t baseMillis;         // millis() when synced
    bool synchronized;

    // Re-sync consensus tracking (only used when already synchronized)
    uint32_t pendingTimestamp;   // Different timestamp received (waiting for confirmation)
    uint32_t pendingMillis;      // When we received the pending timestamp
    static const uint32_t CONSENSUS_WINDOW_MS = 3600000;  // 1 hour window
    static const uint32_t MAX_TIMESTAMP_DIFF = 300;       // Max 5 minutes difference for consensus

public:
    TimeSync() : baseTimestamp(0), baseMillis(0), synchronized(false),
                 pendingTimestamp(0), pendingMillis(0) {}

    /**
     * Sync time from a received ADVERT timestamp
     * - First time: sync immediately
     * - Already synced: need 2 matching different timestamps to re-sync
     * @param unixTime Unix timestamp from received ADVERT
     * @return 0 = no change, 1 = first sync, 2 = re-sync (consensus)
     */
    uint8_t syncFromAdvert(uint32_t unixTime) {
        // Basic sanity check: timestamp should be after 2020 and before 2100
        // 2020-01-01 = 1577836800, 2100-01-01 = 4102444800
        if (unixTime < 1577836800 || unixTime > 4102444800UL) {
            return 0;  // Invalid timestamp, ignore
        }

        uint32_t now = millis();

        // FIRST SYNC: Not yet synchronized - use first valid timestamp immediately
        if (!synchronized) {
            baseTimestamp = unixTime;
            baseMillis = now;
            synchronized = true;
            pendingTimestamp = 0;
            pendingMillis = 0;
            return 1;  // First sync
        }

        // ALREADY SYNCHRONIZED: Check if received time differs from ours
        uint32_t ourTime = baseTimestamp + ((now - baseMillis) / 1000);
        int32_t diff = (int32_t)unixTime - (int32_t)ourTime;

        // If difference is small (< 5 min), we're still in sync - ignore
        if (abs(diff) < (int32_t)MAX_TIMESTAMP_DIFF) {
            pendingTimestamp = 0;  // Clear any pending, times match
            pendingMillis = 0;
            return 0;
        }

        // Received time differs significantly from ours
        // Check if we have a pending different timestamp waiting for confirmation
        if (pendingTimestamp > 0 && (now - pendingMillis) < CONSENSUS_WINDOW_MS) {
            // Adjust pending for elapsed time
            uint32_t pendingAdjusted = pendingTimestamp + ((now - pendingMillis) / 1000);
            int32_t pendingDiff = (int32_t)unixTime - (int32_t)pendingAdjusted;

            // Check if this timestamp confirms the pending one (within 5 min)
            if (abs(pendingDiff) < (int32_t)MAX_TIMESTAMP_DIFF) {
                // Consensus reached! Two different sources agree - re-sync
                uint32_t avgTime = (unixTime + pendingAdjusted) / 2;
                baseTimestamp = avgTime;
                baseMillis = now;
                pendingTimestamp = 0;
                pendingMillis = 0;
                return 2;  // Re-sync via consensus
            }
        }

        // Store this different timestamp as pending, wait for confirmation
        pendingTimestamp = unixTime;
        pendingMillis = now;
        return 0;
    }

    /**
     * Get current Unix timestamp
     * @return Unix timestamp, or uptime if not synced
     */
    uint32_t getTimestamp() {
        if (synchronized) {
            uint32_t elapsed = (millis() - baseMillis) / 1000;
            return baseTimestamp + elapsed;
        }
        // Not synced: return uptime (will likely be rejected by MeshCore)
        return millis() / 1000;
    }

    /**
     * Check if time is synchronized
     */
    bool isSynchronized() const {
        return synchronized;
    }

    /**
     * Manually set time (e.g., from serial command)
     */
    void setTime(uint32_t unixTime) {
        baseTimestamp = unixTime;
        baseMillis = millis();
        synchronized = true;
        pendingTimestamp = 0;
        pendingMillis = 0;
    }

    /**
     * Check if we have a pending timestamp waiting for confirmation
     */
    bool hasPendingSync() const {
        if (pendingTimestamp == 0) return false;
        return (millis() - pendingMillis) < CONSENSUS_WINDOW_MS;
    }

    /**
     * Get pending timestamp (adjusted for elapsed time)
     */
    uint32_t getPendingTimestamp() const {
        if (pendingTimestamp == 0) return 0;
        return pendingTimestamp + ((millis() - pendingMillis) / 1000);
    }
};

#define ADVERT_PUBKEY_OFFSET    0
#define ADVERT_TIMESTAMP_OFFSET 32
#define ADVERT_SIGNATURE_OFFSET 36
#define ADVERT_FLAGS_OFFSET     100
#define ADVERT_LOCATION_OFFSET  101
#define ADVERT_NAME_OFFSET_NO_LOC 101
#define ADVERT_NAME_OFFSET_WITH_LOC 109

// Minimum ADVERT size: pubkey + timestamp + signature + flags
#define ADVERT_MIN_SIZE         101

// Maximum ADVERT size with name
#define ADVERT_MAX_SIZE         (101 + 8 + MC_NODE_NAME_MAX)

/**
 * Parsed ADVERT info structure
 */
struct AdvertInfo {
    uint8_t pubKeyHash;           // First byte of public key
    uint8_t flags;                // Node flags
    bool hasLocation;             // Has location data
    float latitude;               // Latitude if present
    float longitude;              // Longitude if present
    bool hasName;                 // Has name
    char name[MC_NODE_NAME_MAX];  // Node name if present
    bool isRepeater;              // Is repeater node
    bool isChatNode;              // Is chat node
};

/**
 * ADVERT Generator Class
 */
class AdvertGenerator {
private:
    IdentityManager* identity;
    TimeSync* timeSync;
    uint32_t lastAdvertTime;
    uint32_t advertInterval;
    bool enabled;

public:
    AdvertGenerator() : identity(nullptr), timeSync(nullptr), lastAdvertTime(0),
                        advertInterval(300000), enabled(true) {} // 5 min default

    /**
     * Initialize with identity manager and time sync
     */
    void begin(IdentityManager* id, TimeSync* ts = nullptr) {
        identity = id;
        timeSync = ts;
        lastAdvertTime = 0;
    }

    /**
     * Set ADVERT broadcast interval
     * @param intervalMs Interval in milliseconds
     */
    void setInterval(uint32_t intervalMs) {
        advertInterval = intervalMs;
    }

    /**
     * Get current interval
     */
    uint32_t getInterval() const {
        return advertInterval;
    }

    /**
     * Enable/disable ADVERT broadcasting
     */
    void setEnabled(bool en) {
        enabled = en;
    }

    /**
     * Check if ADVERT is enabled
     */
    bool isEnabled() const {
        return enabled;
    }

    /**
     * Check if it's time to send an ADVERT
     */
    bool shouldSend() {
        if (!enabled || !identity || !identity->isInitialized()) {
            return false;
        }
        return (millis() - lastAdvertTime) >= advertInterval;
    }

    /**
     * Mark ADVERT as sent
     */
    void markSent() {
        lastAdvertTime = millis();
    }

    /**
     * Get time until next ADVERT in seconds
     */
    uint32_t getTimeUntilNext() const {
        uint32_t elapsed = millis() - lastAdvertTime;
        if (elapsed >= advertInterval) return 0;
        return (advertInterval - elapsed) / 1000;
    }

    /**
     * Build an ADVERT packet
     * @param pkt Output packet
     * @param routeType MC_ROUTE_FLOOD or MC_ROUTE_DIRECT
     * @return true if packet built successfully
     */
    bool build(MCPacket* pkt, uint8_t routeType = MC_ROUTE_FLOOD) {
        if (!identity || !identity->isInitialized()) {
            return false;
        }

        pkt->clear();

        // Set header: FLOOD/DIRECT + ADVERT type
        pkt->header.set(routeType, MC_PAYLOAD_ADVERT, MC_PAYLOAD_VER_1);

        // Path starts empty for new ADVERT
        pkt->pathLen = 0;

        // Build payload
        uint8_t* payload = pkt->payload;
        uint16_t pos = 0;

        // [0-31] Public Key
        memcpy(&payload[pos], identity->getPublicKey(), MC_PUBLIC_KEY_SIZE);
        pos += MC_PUBLIC_KEY_SIZE;

        // [32-35] Timestamp (little-endian)
        uint32_t timestamp = getTimestamp();
        payload[pos++] = timestamp & 0xFF;
        payload[pos++] = (timestamp >> 8) & 0xFF;
        payload[pos++] = (timestamp >> 16) & 0xFF;
        payload[pos++] = (timestamp >> 24) & 0xFF;

        // Build appdata first (we need it for signature)
        uint8_t appdata[32];
        uint8_t appdataLen = buildAppdata(appdata);

        // Calculate signature over: pubkey + timestamp + appdata
        uint8_t signData[MC_PUBLIC_KEY_SIZE + 4 + 32];
        uint16_t signLen = 0;
        memcpy(&signData[signLen], identity->getPublicKey(), MC_PUBLIC_KEY_SIZE);
        signLen += MC_PUBLIC_KEY_SIZE;
        memcpy(&signData[signLen], &payload[ADVERT_TIMESTAMP_OFFSET], 4);
        signLen += 4;
        memcpy(&signData[signLen], appdata, appdataLen);
        signLen += appdataLen;

        // [36-99] Signature
        identity->sign(&payload[pos], signData, signLen);
        pos += MC_SIGNATURE_SIZE;

        // [100+] Appdata
        memcpy(&payload[pos], appdata, appdataLen);
        pos += appdataLen;

        pkt->payloadLen = pos;

        return true;
    }

    /**
     * Build a "Zero Hop" ADVERT (DIRECT route, local announcement only)
     */
    bool buildZeroHop(MCPacket* pkt) {
        return build(pkt, MC_ROUTE_DIRECT);
    }

    /**
     * Build a "Flood Routed" ADVERT (propagates through network)
     */
    bool buildFlood(MCPacket* pkt) {
        return build(pkt, MC_ROUTE_FLOOD);
    }

private:
    /**
     * Get current timestamp
     * Uses synced time if available, otherwise uptime
     */
    uint32_t getTimestamp() {
        if (timeSync) {
            return timeSync->getTimestamp();
        }
        // Fallback: use uptime (will likely be rejected by MeshCore)
        return millis() / 1000;
    }

    /**
     * Build appdata section
     * @param appdata Output buffer
     * @return Length of appdata
     */
    uint8_t buildAppdata(uint8_t* appdata) {
        uint8_t pos = 0;

        // Flags byte
        appdata[pos++] = identity->getFlags();

        // Location (if set) - int32 * 1000000, little-endian
        if (identity->hasLocation()) {
            // Use stored int32 values directly (already * 1000000)
            int32_t lat = identity->getLatitude();
            int32_t lon = identity->getLongitude();

            // Latitude as int32 (little-endian)
            appdata[pos++] = lat & 0xFF;
            appdata[pos++] = (lat >> 8) & 0xFF;
            appdata[pos++] = (lat >> 16) & 0xFF;
            appdata[pos++] = (lat >> 24) & 0xFF;

            // Longitude as int32 (little-endian)
            appdata[pos++] = lon & 0xFF;
            appdata[pos++] = (lon >> 8) & 0xFF;
            appdata[pos++] = (lon >> 16) & 0xFF;
            appdata[pos++] = (lon >> 24) & 0xFF;
        }

        // Name (if set)
        if (identity->getFlags() & MC_FLAG_HAS_NAME) {
            const char* name = identity->getNodeName();
            uint8_t nameLen = strlen(name);
            if (nameLen > 0 && nameLen < MC_NODE_NAME_MAX) {
                memcpy(&appdata[pos], name, nameLen);
                pos += nameLen;
            }
        }

        return pos;
    }

public:
    /**
     * Parse received ADVERT payload to extract node info
     * @param payload ADVERT payload data
     * @param payloadLen Length of payload
     * @param info Output structure for parsed info
     * @return true if successfully parsed
     */
    /**
     * Extract timestamp from ADVERT payload
     * @param payload ADVERT payload data
     * @param payloadLen Length of payload
     * @return Unix timestamp, or 0 if invalid
     */
    static uint32_t extractTimestamp(const uint8_t* payload, uint16_t payloadLen) {
        if (payloadLen < ADVERT_MIN_SIZE) {
            return 0;
        }
        // Timestamp is at bytes 32-35 (little-endian)
        return payload[ADVERT_TIMESTAMP_OFFSET] |
               (payload[ADVERT_TIMESTAMP_OFFSET + 1] << 8) |
               (payload[ADVERT_TIMESTAMP_OFFSET + 2] << 16) |
               (payload[ADVERT_TIMESTAMP_OFFSET + 3] << 24);
    }

    static bool parseAdvert(const uint8_t* payload, uint16_t payloadLen, AdvertInfo* info) {
        if (!info || payloadLen < ADVERT_MIN_SIZE) {
            return false;
        }

        memset(info, 0, sizeof(AdvertInfo));

        // Get public key hash (first byte)
        info->pubKeyHash = payload[ADVERT_PUBKEY_OFFSET];

        // Get flags byte (at position 100)
        info->flags = payload[ADVERT_FLAGS_OFFSET];

        // Decode node type (lower 4 bits) and flags (upper 4 bits)
        uint8_t nodeType = info->flags & MC_TYPE_MASK;
        info->isRepeater = (nodeType == MC_TYPE_REPEATER);
        info->isChatNode = (nodeType == MC_TYPE_CHAT_NODE);
        info->hasLocation = (info->flags & MC_FLAG_HAS_LOCATION) != 0;
        info->hasName = (info->flags & MC_FLAG_HAS_NAME) != 0;

        uint16_t pos = ADVERT_FLAGS_OFFSET + 1;  // After flags byte

        // Parse location if present (int32 * 1000000, little-endian)
        if (info->hasLocation && payloadLen >= pos + 8) {
            // Latitude as int32 * 1000000
            int32_t latInt = (int32_t)(payload[pos] | (payload[pos+1] << 8) |
                              (payload[pos+2] << 16) | (payload[pos+3] << 24));
            info->latitude = latInt / 1000000.0f;

            // Longitude as int32 * 1000000
            int32_t lonInt = (int32_t)(payload[pos+4] | (payload[pos+5] << 8) |
                              (payload[pos+6] << 16) | (payload[pos+7] << 24));
            info->longitude = lonInt / 1000000.0f;
            pos += 8;
        }

        // Parse name if present (name is remaining bytes after optional fields, no length prefix)
        if (info->hasName && payloadLen > pos) {
            uint8_t nameLen = payloadLen - pos;
            if (nameLen > MC_NODE_NAME_MAX - 1) {
                nameLen = MC_NODE_NAME_MAX - 1;
            }
            memcpy(info->name, &payload[pos], nameLen);
            info->name[nameLen] = '\0';
        } else {
            // No name, generate from pubkey hash
            snprintf(info->name, MC_NODE_NAME_MAX, "Node-%02X", info->pubKeyHash);
        }

        return true;
    }
};
