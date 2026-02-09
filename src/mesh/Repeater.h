#pragma once
#include <Arduino.h>
#include "Packet.h"
#include "Identity.h"

/**
 * MeshCore Repeater Functionality
 * Implements: Login/Auth, Stats, Neighbors, Discovery, Telemetry
 *
 * Based on MeshCore protocol specification
 */

//=============================================================================
// Constants (Request/Control types now in Packet.h)
//=============================================================================

// ACL Permissions (MeshCore compatible)
#define PERM_ACL_NONE               0x00
#define PERM_ACL_ADMIN              0x01  // Full admin access
#define PERM_ACL_GUEST              0x02  // Read-only stats
#define PERM_ACL_READONLY           0x02  // Alias for guest
#define PERM_ACL_READWRITE          0x03  // Read-write access

// Stats sub-types
#define STATS_TYPE_CORE             0x00
#define STATS_TYPE_RADIO            0x01
#define STATS_TYPE_PACKETS          0x02

// Limits
#define MAX_NEIGHBOURS              50
#define MAX_ACL_ENTRIES             16
#define MAX_PASSWORD_LEN            15
#define NEIGHBOUR_TIMEOUT_MS        3600000  // 1 hour
#define DISCOVER_RATE_LIMIT_MS      30000    // 30 sec between discover responses
#define MAX_DISCOVER_PER_WINDOW     4        // Max 4 responses per window
#define DISCOVER_WINDOW_MS          120000   // 2 minute window

// Rate limiting defaults
#define RATE_LIMIT_LOGIN_MAX        5        // Max login attempts per window
#define RATE_LIMIT_LOGIN_SECS       60       // Login window: 1 minute
#define RATE_LIMIT_REQUEST_MAX      30       // Max requests per window
#define RATE_LIMIT_REQUEST_SECS     60       // Request window: 1 minute
#define RATE_LIMIT_FORWARD_MAX      100      // Max forwards per window
#define RATE_LIMIT_FORWARD_SECS     60       // Forward window: 1 minute

// Default passwords
#define DEFAULT_ADMIN_PASSWORD      "password"
#define DEFAULT_GUEST_PASSWORD      "hello"

//=============================================================================
// Rate Limiter Class
//=============================================================================

/**
 * Generic rate limiter using sliding window
 * Limits operations to a maximum count within a time window
 */
class RateLimiter {
private:
    uint32_t windowStart;   // Start of current window (seconds)
    uint32_t windowSecs;    // Window duration in seconds
    uint16_t maxCount;      // Max allowed in window
    uint16_t count;         // Current count in window
    uint32_t totalBlocked;  // Total blocked requests (stats)
    uint32_t totalAllowed;  // Total allowed requests (stats)

public:
    RateLimiter(uint16_t maximum, uint32_t secs)
        : windowStart(0), windowSecs(secs), maxCount(maximum), count(0),
          totalBlocked(0), totalAllowed(0) {}

    /**
     * Check if operation is allowed
     * @param nowSecs Current time in seconds (millis()/1000)
     * @return true if allowed, false if rate limited
     */
    bool allow(uint32_t nowSecs) {
        if (nowSecs < windowStart + windowSecs) {
            // Still in current window
            count++;
            if (count > maxCount) {
                totalBlocked++;
                return false;  // Rate limited
            }
        } else {
            // Window expired, start new one
            windowStart = nowSecs;
            count = 1;
        }
        totalAllowed++;
        return true;
    }

    /**
     * Check without incrementing counter (peek)
     */
    bool wouldAllow(uint32_t nowSecs) const {
        if (nowSecs < windowStart + windowSecs) {
            return (count < maxCount);
        }
        return true;  // New window would start
    }

    /**
     * Get current count in window
     */
    uint16_t getCount() const { return count; }

    /**
     * Get max allowed
     */
    uint16_t getMax() const { return maxCount; }

    /**
     * Get window duration
     */
    uint32_t getWindowSecs() const { return windowSecs; }

    /**
     * Get total blocked count
     */
    uint32_t getTotalBlocked() const { return totalBlocked; }

    /**
     * Get total allowed count
     */
    uint32_t getTotalAllowed() const { return totalAllowed; }

    /**
     * Reset statistics
     */
    void resetStats() {
        totalBlocked = 0;
        totalAllowed = 0;
    }

    /**
     * Reconfigure limits
     */
    void configure(uint16_t maximum, uint32_t secs) {
        maxCount = maximum;
        windowSecs = secs;
        // Reset window on reconfigure
        windowStart = 0;
        count = 0;
    }
};

//=============================================================================
// Structures
//=============================================================================

/**
 * Neighbor info - tracks nearby repeaters
 */
struct NeighbourInfo {
    uint8_t pubKeyPrefix[6];    // First 6 bytes of public key
    uint32_t lastHeard;         // millis() when last heard
    int8_t snr;                 // SNR * 4
    int16_t rssi;               // RSSI in dBm
    bool valid;                 // Entry is valid

    void clear() {
        memset(pubKeyPrefix, 0, 6);
        lastHeard = 0;
        snr = 0;
        rssi = 0;
        valid = false;
    }
};

/**
 * ACL Entry - access control list
 */
struct ACLEntry {
    uint8_t pubKeyPrefix[6];    // First 6 bytes of public key
    uint8_t permissions;        // Permission level
    uint32_t lastTimestamp;     // Last request timestamp (replay protection)
    bool valid;                 // Entry is valid

    void clear() {
        memset(pubKeyPrefix, 0, 6);
        permissions = PERM_ACL_NONE;
        lastTimestamp = 0;
        valid = false;
    }
};

/**
 * Core Stats structure
 */
struct CoreStats {
    uint16_t battMillivolts;
    uint32_t uptimeSecs;
    uint16_t errFlags;
    uint8_t queueLen;
};

/**
 * Radio Stats structure
 */
struct RadioStats {
    int16_t noiseFloor;
    int8_t lastRssi;
    int8_t lastSnr;         // SNR * 4
    uint32_t txAirTimeSec;
    uint32_t rxAirTimeSec;
};

/**
 * Packet Stats structure
 */
struct PacketStats {
    uint32_t numRecvPackets;
    uint32_t numSentPackets;
    uint32_t numSentFlood;
    uint32_t numSentDirect;
    uint32_t numRecvFlood;
    uint32_t numRecvDirect;
};

//=============================================================================
// Neighbor Tracker Class
//=============================================================================

class NeighbourTracker {
private:
    NeighbourInfo neighbours[MAX_NEIGHBOURS];
    uint8_t count;

public:
    NeighbourTracker() : count(0) {
        for (int i = 0; i < MAX_NEIGHBOURS; i++) {
            neighbours[i].clear();
        }
    }

    /**
     * Update or add a neighbor
     * @param pubKey Full public key (32 bytes)
     * @param snr SNR * 4
     * @param rssi RSSI in dBm
     * @return true if new neighbor added
     */
    bool update(const uint8_t* pubKey, int8_t snr, int16_t rssi) {
        uint32_t now = millis();

        // Check if already exists
        for (int i = 0; i < MAX_NEIGHBOURS; i++) {
            if (neighbours[i].valid &&
                memcmp(neighbours[i].pubKeyPrefix, pubKey, 6) == 0) {
                // Update existing
                neighbours[i].lastHeard = now;
                neighbours[i].snr = snr;
                neighbours[i].rssi = rssi;
                return false;  // Not new
            }
        }

        // Find empty slot or evict oldest
        int slot = -1;
        uint32_t oldest = UINT32_MAX;
        int oldestIdx = 0;

        for (int i = 0; i < MAX_NEIGHBOURS; i++) {
            if (!neighbours[i].valid) {
                slot = i;
                break;
            }
            // Track oldest for eviction
            uint32_t age = now - neighbours[i].lastHeard;
            if (age > oldest || (oldest == UINT32_MAX)) {
                oldest = age;
                oldestIdx = i;
            }
        }

        // Evict oldest if no empty slot
        if (slot < 0) {
            slot = oldestIdx;
        }

        // Add new neighbor
        memcpy(neighbours[slot].pubKeyPrefix, pubKey, 6);
        neighbours[slot].lastHeard = now;
        neighbours[slot].snr = snr;
        neighbours[slot].rssi = rssi;
        neighbours[slot].valid = true;

        if (slot >= count) count = slot + 1;
        return true;  // New neighbor
    }

    /**
     * Get neighbor count
     */
    uint8_t getCount() const {
        uint8_t cnt = 0;
        for (int i = 0; i < MAX_NEIGHBOURS; i++) {
            if (neighbours[i].valid) cnt++;
        }
        return cnt;
    }

    /**
     * Get neighbor by index
     */
    const NeighbourInfo* getNeighbour(uint8_t idx) const {
        uint8_t cnt = 0;
        for (int i = 0; i < MAX_NEIGHBOURS; i++) {
            if (neighbours[i].valid) {
                if (cnt == idx) return &neighbours[i];
                cnt++;
            }
        }
        return nullptr;
    }

    /**
     * Clean expired neighbors
     */
    void cleanExpired() {
        uint32_t now = millis();
        for (int i = 0; i < MAX_NEIGHBOURS; i++) {
            if (neighbours[i].valid) {
                if ((now - neighbours[i].lastHeard) > NEIGHBOUR_TIMEOUT_MS) {
                    neighbours[i].clear();
                }
            }
        }
    }

    /**
     * Serialize neighbors list for response (MeshCore format)
     * Format per entry: pubkey_prefix + seconds_since_heard (4 bytes) + snr (1 byte)
     * @param buf Output buffer
     * @param maxLen Maximum buffer size
     * @param offset Start from this neighbor index
     * @param prefixLen How many bytes of pubkey prefix to include (1-6)
     * @return Bytes written
     */
    uint16_t serialize(uint8_t* buf, uint16_t maxLen, uint8_t offset = 0, uint8_t prefixLen = 6) {
        if (prefixLen > 6) prefixLen = 6;
        uint16_t pos = 0;
        uint8_t cnt = 0;
        uint32_t now = millis();

        // Entry size: prefix + 4 bytes (seconds_since) + 1 byte (snr)
        uint8_t entrySize = prefixLen + 5;

        for (int i = 0; i < MAX_NEIGHBOURS && pos + entrySize <= maxLen; i++) {
            if (neighbours[i].valid) {
                if (cnt >= offset) {
                    // Write pubkey prefix
                    memcpy(&buf[pos], neighbours[i].pubKeyPrefix, prefixLen);
                    pos += prefixLen;

                    // Write seconds since heard (4 bytes, little-endian)
                    uint32_t secsSince = (now - neighbours[i].lastHeard) / 1000;
                    buf[pos++] = secsSince & 0xFF;
                    buf[pos++] = (secsSince >> 8) & 0xFF;
                    buf[pos++] = (secsSince >> 16) & 0xFF;
                    buf[pos++] = (secsSince >> 24) & 0xFF;

                    // Write SNR (already stored as snr*4)
                    buf[pos++] = (uint8_t)neighbours[i].snr;
                }
                cnt++;
            }
        }
        return pos;
    }
};

//=============================================================================
// ACL Manager Class
//=============================================================================

class ACLManager {
private:
    ACLEntry entries[MAX_ACL_ENTRIES];
    char adminPassword[MAX_PASSWORD_LEN + 1];
    char guestPassword[MAX_PASSWORD_LEN + 1];

public:
    ACLManager() {
        for (int i = 0; i < MAX_ACL_ENTRIES; i++) {
            entries[i].clear();
        }
        strncpy(adminPassword, DEFAULT_ADMIN_PASSWORD, MAX_PASSWORD_LEN);
        adminPassword[MAX_PASSWORD_LEN] = '\0';
        strncpy(guestPassword, DEFAULT_GUEST_PASSWORD, MAX_PASSWORD_LEN);
        guestPassword[MAX_PASSWORD_LEN] = '\0';
    }

    /**
     * Set admin password
     */
    void setAdminPassword(const char* pwd) {
        strncpy(adminPassword, pwd, MAX_PASSWORD_LEN);
        adminPassword[MAX_PASSWORD_LEN] = '\0';
    }

    /**
     * Set guest password
     */
    void setGuestPassword(const char* pwd) {
        strncpy(guestPassword, pwd, MAX_PASSWORD_LEN);
        guestPassword[MAX_PASSWORD_LEN] = '\0';
    }

    /**
     * Get admin password
     */
    const char* getAdminPassword() const {
        return adminPassword;
    }

    /**
     * Get guest password
     */
    const char* getGuestPassword() const {
        return guestPassword;
    }

    /**
     * Verify login credentials
     * @param pubKey Public key of requester (32 bytes)
     * @param password Password string
     * @param timestamp Request timestamp
     * @return Permission level (0 if failed)
     */
    uint8_t verifyLogin(const uint8_t* pubKey, const char* password, uint32_t timestamp) {
        // Check admin password
        if (strcmp(password, adminPassword) == 0) {
            addOrUpdateEntry(pubKey, PERM_ACL_ADMIN, timestamp);
            return PERM_ACL_ADMIN;
        }

        // Check guest password (empty = open guest access)
        if (guestPassword[0] == '\0' || strcmp(password, guestPassword) == 0) {
            addOrUpdateEntry(pubKey, PERM_ACL_GUEST, timestamp);
            return PERM_ACL_GUEST;
        }

        return PERM_ACL_NONE;
    }

    /**
     * Check if request is valid (not replay)
     * @param pubKey Public key of requester
     * @param timestamp Request timestamp
     * @return Permission level if valid, 0 if replay or not authorized
     */
    uint8_t checkRequest(const uint8_t* pubKey, uint32_t timestamp) {
        for (int i = 0; i < MAX_ACL_ENTRIES; i++) {
            if (entries[i].valid &&
                memcmp(entries[i].pubKeyPrefix, pubKey, 6) == 0) {
                // Check replay protection
                if (timestamp <= entries[i].lastTimestamp) {
                    return PERM_ACL_NONE;  // Replay attack
                }
                entries[i].lastTimestamp = timestamp;
                return entries[i].permissions;
            }
        }
        return PERM_ACL_NONE;  // Not authorized
    }

    /**
     * Get entry count
     */
    uint8_t getCount() const {
        uint8_t cnt = 0;
        for (int i = 0; i < MAX_ACL_ENTRIES; i++) {
            if (entries[i].valid) cnt++;
        }
        return cnt;
    }

    /**
     * Get entry by index
     */
    const ACLEntry* getEntry(uint8_t idx) const {
        uint8_t cnt = 0;
        for (int i = 0; i < MAX_ACL_ENTRIES; i++) {
            if (entries[i].valid) {
                if (cnt == idx) return &entries[i];
                cnt++;
            }
        }
        return nullptr;
    }

    /**
     * Remove entry by pubkey prefix
     */
    bool removeEntry(const uint8_t* pubKeyPrefix) {
        for (int i = 0; i < MAX_ACL_ENTRIES; i++) {
            if (entries[i].valid &&
                memcmp(entries[i].pubKeyPrefix, pubKeyPrefix, 6) == 0) {
                entries[i].clear();
                return true;
            }
        }
        return false;
    }

private:
    void addOrUpdateEntry(const uint8_t* pubKey, uint8_t permissions, uint32_t timestamp) {
        // Check if exists
        for (int i = 0; i < MAX_ACL_ENTRIES; i++) {
            if (entries[i].valid &&
                memcmp(entries[i].pubKeyPrefix, pubKey, 6) == 0) {
                entries[i].permissions = permissions;
                entries[i].lastTimestamp = timestamp;
                return;
            }
        }

        // Find empty slot
        for (int i = 0; i < MAX_ACL_ENTRIES; i++) {
            if (!entries[i].valid) {
                memcpy(entries[i].pubKeyPrefix, pubKey, 6);
                entries[i].permissions = permissions;
                entries[i].lastTimestamp = timestamp;
                entries[i].valid = true;
                return;
            }
        }

        // No empty slot - evict oldest by timestamp
        uint32_t oldest = UINT32_MAX;
        int oldestIdx = 0;
        for (int i = 0; i < MAX_ACL_ENTRIES; i++) {
            if (entries[i].lastTimestamp < oldest) {
                oldest = entries[i].lastTimestamp;
                oldestIdx = i;
            }
        }

        memcpy(entries[oldestIdx].pubKeyPrefix, pubKey, 6);
        entries[oldestIdx].permissions = permissions;
        entries[oldestIdx].lastTimestamp = timestamp;
        entries[oldestIdx].valid = true;
    }
};

//=============================================================================
// Discovery Rate Limiter
//=============================================================================

class DiscoverLimiter {
private:
    uint32_t responseTimes[MAX_DISCOVER_PER_WINDOW];
    uint8_t count;

public:
    DiscoverLimiter() : count(0) {
        for (int i = 0; i < MAX_DISCOVER_PER_WINDOW; i++) {
            responseTimes[i] = 0;
        }
    }

    /**
     * Check if we can send a discovery response
     * @return true if allowed
     */
    bool allow() {
        uint32_t now = millis();

        // Clean expired entries
        uint8_t validCount = 0;
        for (int i = 0; i < MAX_DISCOVER_PER_WINDOW; i++) {
            if (responseTimes[i] > 0 && (now - responseTimes[i]) < DISCOVER_WINDOW_MS) {
                validCount++;
            } else {
                responseTimes[i] = 0;
            }
        }

        if (validCount >= MAX_DISCOVER_PER_WINDOW) {
            return false;  // Rate limited
        }

        // Record this response
        for (int i = 0; i < MAX_DISCOVER_PER_WINDOW; i++) {
            if (responseTimes[i] == 0) {
                responseTimes[i] = now;
                break;
            }
        }

        return true;
    }
};

//=============================================================================
// Repeater Helper Class
//=============================================================================

class RepeaterHelper {
private:
    IdentityManager* identity;
    NeighbourTracker neighbours;
    ACLManager acl;
    DiscoverLimiter discoverLimiter;

    // Rate limiters
    RateLimiter loginLimiter;
    RateLimiter requestLimiter;
    RateLimiter forwardLimiter;

    // Statistics
    PacketStats pktStats;
    RadioStats radioStats;
    uint32_t startTime;

    // Airtime accumulators (sub-second precision)
    uint32_t txAirTimeAccumMs;
    uint32_t rxAirTimeAccumMs;

    // Configuration
    bool repeatEnabled;
    uint8_t maxFloodHops;
    bool rateLimitEnabled;

public:
    RepeaterHelper() : identity(nullptr), startTime(0),
                       txAirTimeAccumMs(0), rxAirTimeAccumMs(0),
                       repeatEnabled(true), maxFloodHops(8),
                       rateLimitEnabled(true),
                       loginLimiter(RATE_LIMIT_LOGIN_MAX, RATE_LIMIT_LOGIN_SECS),
                       requestLimiter(RATE_LIMIT_REQUEST_MAX, RATE_LIMIT_REQUEST_SECS),
                       forwardLimiter(RATE_LIMIT_FORWARD_MAX, RATE_LIMIT_FORWARD_SECS) {
        memset(&pktStats, 0, sizeof(pktStats));
        memset(&radioStats, 0, sizeof(radioStats));
    }

    /**
     * Initialize with identity manager
     */
    void begin(IdentityManager* id) {
        identity = id;
        startTime = millis();
    }

    /**
     * Get neighbor tracker
     */
    NeighbourTracker& getNeighbours() {
        return neighbours;
    }

    /**
     * Get ACL manager
     */
    ACLManager& getACL() {
        return acl;
    }

    /**
     * Check if repeating is enabled
     */
    bool isRepeatEnabled() const {
        return repeatEnabled;
    }

    /**
     * Set repeat enabled
     */
    void setRepeatEnabled(bool en) {
        repeatEnabled = en;
    }

    /**
     * Get max flood hops
     */
    uint8_t getMaxFloodHops() const {
        return maxFloodHops;
    }

    /**
     * Set max flood hops
     */
    void setMaxFloodHops(uint8_t hops) {
        maxFloodHops = hops;
    }

    //=========================================================================
    // Rate Limiting
    //=========================================================================

    /**
     * Check if rate limiting is enabled
     */
    bool isRateLimitEnabled() const {
        return rateLimitEnabled;
    }

    /**
     * Enable/disable rate limiting
     */
    void setRateLimitEnabled(bool en) {
        rateLimitEnabled = en;
    }

    /**
     * Check if login attempt is allowed
     * @return true if allowed, false if rate limited
     */
    bool allowLogin() {
        if (!rateLimitEnabled) return true;
        return loginLimiter.allow(millis() / 1000);
    }

    /**
     * Check if request is allowed
     * @return true if allowed, false if rate limited
     */
    bool allowRequest() {
        if (!rateLimitEnabled) return true;
        return requestLimiter.allow(millis() / 1000);
    }

    /**
     * Check if forward is allowed
     * @return true if allowed, false if rate limited
     */
    bool allowForward() {
        if (!rateLimitEnabled) return true;
        return forwardLimiter.allow(millis() / 1000);
    }

    /**
     * Get rate limiter stats
     */
    void getRateLimitStats(uint32_t* loginBlocked, uint32_t* requestBlocked,
                           uint32_t* forwardBlocked) const {
        if (loginBlocked) *loginBlocked = loginLimiter.getTotalBlocked();
        if (requestBlocked) *requestBlocked = requestLimiter.getTotalBlocked();
        if (forwardBlocked) *forwardBlocked = forwardLimiter.getTotalBlocked();
    }

    /**
     * Get login limiter reference (for detailed stats)
     */
    const RateLimiter& getLoginLimiter() const { return loginLimiter; }

    /**
     * Get request limiter reference
     */
    const RateLimiter& getRequestLimiter() const { return requestLimiter; }

    /**
     * Get forward limiter reference
     */
    const RateLimiter& getForwardLimiter() const { return forwardLimiter; }

    /**
     * Reset all rate limiter stats
     */
    void resetRateLimitStats() {
        loginLimiter.resetStats();
        requestLimiter.resetStats();
        forwardLimiter.resetStats();
    }

    /**
     * Update packet statistics
     */
    void recordRx(bool isFlood) {
        pktStats.numRecvPackets++;
        if (isFlood) pktStats.numRecvFlood++;
        else pktStats.numRecvDirect++;
    }

    void recordTx(bool isFlood) {
        pktStats.numSentPackets++;
        if (isFlood) pktStats.numSentFlood++;
        else pktStats.numSentDirect++;
    }

    /**
     * Update radio statistics including noise floor estimate
     * Noise floor = RSSI - SNR (approximation for SX1262)
     */
    void updateRadioStats(int8_t rssi, int8_t snr) {
        radioStats.lastRssi = rssi;
        radioStats.lastSnr = snr;

        // Estimate noise floor: RSSI - SNR (in dB)
        // snr is stored as snr*4, so divide by 4 for actual dB
        int16_t snrDb = snr / 4;
        int16_t noiseEst = (int16_t)rssi - snrDb;

        // Use exponential moving average to smooth noise floor
        if (radioStats.noiseFloor == 0) {
            radioStats.noiseFloor = noiseEst;
        } else {
            // EMA: new = old * 0.875 + sample * 0.125 (shift-based for efficiency)
            radioStats.noiseFloor = (int16_t)(((int32_t)radioStats.noiseFloor * 7 + noiseEst) / 8);
        }
    }

    void addTxAirTime(uint32_t ms) {
        txAirTimeAccumMs += ms;
        if (txAirTimeAccumMs >= 1000) {
            radioStats.txAirTimeSec += txAirTimeAccumMs / 1000;
            txAirTimeAccumMs %= 1000;
        }
    }

    void addRxAirTime(uint32_t ms) {
        rxAirTimeAccumMs += ms;
        if (rxAirTimeAccumMs >= 1000) {
            radioStats.rxAirTimeSec += rxAirTimeAccumMs / 1000;
            rxAirTimeAccumMs %= 1000;
        }
    }

    /**
     * Get core stats
     */
    CoreStats getCoreStats(uint16_t battMv, uint8_t queueLen) {
        CoreStats stats;
        stats.battMillivolts = battMv;
        stats.uptimeSecs = (millis() - startTime) / 1000;
        stats.errFlags = 0;
        stats.queueLen = queueLen;
        return stats;
    }

    /**
     * Get radio stats
     */
    const RadioStats& getRadioStats() const {
        return radioStats;
    }

    /**
     * Get packet stats
     */
    const PacketStats& getPacketStats() const {
        return pktStats;
    }

    /**
     * Serialize RepeaterStats in MeshCore format (52 bytes)
     * Matches MeshCore's RepeaterStats struct exactly
     */
    uint16_t serializeRepeaterStats(uint8_t* buf, uint16_t battMv, uint8_t queueLen,
                                     int16_t rssi, int8_t snr) {
        uint16_t pos = 0;
        uint32_t uptimeSecs = millis() / 1000;

        // batt_milli_volts (uint16_t)
        buf[pos++] = battMv & 0xFF;
        buf[pos++] = (battMv >> 8) & 0xFF;

        // curr_tx_queue_len (uint16_t)
        buf[pos++] = queueLen & 0xFF;
        buf[pos++] = 0;

        // noise_floor (int16_t)
        buf[pos++] = radioStats.noiseFloor & 0xFF;
        buf[pos++] = (radioStats.noiseFloor >> 8) & 0xFF;

        // last_rssi (int16_t)
        buf[pos++] = rssi & 0xFF;
        buf[pos++] = (rssi >> 8) & 0xFF;

        // n_packets_recv (uint32_t)
        buf[pos++] = pktStats.numRecvPackets & 0xFF;
        buf[pos++] = (pktStats.numRecvPackets >> 8) & 0xFF;
        buf[pos++] = (pktStats.numRecvPackets >> 16) & 0xFF;
        buf[pos++] = (pktStats.numRecvPackets >> 24) & 0xFF;

        // n_packets_sent (uint32_t)
        buf[pos++] = pktStats.numSentPackets & 0xFF;
        buf[pos++] = (pktStats.numSentPackets >> 8) & 0xFF;
        buf[pos++] = (pktStats.numSentPackets >> 16) & 0xFF;
        buf[pos++] = (pktStats.numSentPackets >> 24) & 0xFF;

        // total_air_time_secs (uint32_t)
        buf[pos++] = radioStats.txAirTimeSec & 0xFF;
        buf[pos++] = (radioStats.txAirTimeSec >> 8) & 0xFF;
        buf[pos++] = (radioStats.txAirTimeSec >> 16) & 0xFF;
        buf[pos++] = (radioStats.txAirTimeSec >> 24) & 0xFF;

        // total_up_time_secs (uint32_t)
        buf[pos++] = uptimeSecs & 0xFF;
        buf[pos++] = (uptimeSecs >> 8) & 0xFF;
        buf[pos++] = (uptimeSecs >> 16) & 0xFF;
        buf[pos++] = (uptimeSecs >> 24) & 0xFF;

        // n_sent_flood (uint32_t)
        buf[pos++] = pktStats.numSentFlood & 0xFF;
        buf[pos++] = (pktStats.numSentFlood >> 8) & 0xFF;
        buf[pos++] = (pktStats.numSentFlood >> 16) & 0xFF;
        buf[pos++] = (pktStats.numSentFlood >> 24) & 0xFF;

        // n_sent_direct (uint32_t)
        buf[pos++] = pktStats.numSentDirect & 0xFF;
        buf[pos++] = (pktStats.numSentDirect >> 8) & 0xFF;
        buf[pos++] = (pktStats.numSentDirect >> 16) & 0xFF;
        buf[pos++] = (pktStats.numSentDirect >> 24) & 0xFF;

        // n_recv_flood (uint32_t)
        buf[pos++] = pktStats.numRecvFlood & 0xFF;
        buf[pos++] = (pktStats.numRecvFlood >> 8) & 0xFF;
        buf[pos++] = (pktStats.numRecvFlood >> 16) & 0xFF;
        buf[pos++] = (pktStats.numRecvFlood >> 24) & 0xFF;

        // n_recv_direct (uint32_t)
        buf[pos++] = pktStats.numRecvDirect & 0xFF;
        buf[pos++] = (pktStats.numRecvDirect >> 8) & 0xFF;
        buf[pos++] = (pktStats.numRecvDirect >> 16) & 0xFF;
        buf[pos++] = (pktStats.numRecvDirect >> 24) & 0xFF;

        // err_events (uint16_t)
        buf[pos++] = 0;
        buf[pos++] = 0;

        // last_snr (int16_t) - MeshCore uses SNR * 4
        int16_t snr4 = snr * 4;
        buf[pos++] = snr4 & 0xFF;
        buf[pos++] = (snr4 >> 8) & 0xFF;

        // n_direct_dups (uint16_t)
        buf[pos++] = 0;
        buf[pos++] = 0;

        // n_flood_dups (uint16_t)
        buf[pos++] = 0;
        buf[pos++] = 0;

        // total_rx_air_time_secs (uint32_t)
        buf[pos++] = radioStats.rxAirTimeSec & 0xFF;
        buf[pos++] = (radioStats.rxAirTimeSec >> 8) & 0xFF;
        buf[pos++] = (radioStats.rxAirTimeSec >> 16) & 0xFF;
        buf[pos++] = (radioStats.rxAirTimeSec >> 24) & 0xFF;

        return pos;  // Should be 52
    }

    /**
     * Serialize core stats for response (legacy format)
     */
    uint16_t serializeCoreStats(uint8_t* buf, uint16_t battMv, uint8_t queueLen) {
        CoreStats stats = getCoreStats(battMv, queueLen);
        uint16_t pos = 0;

        buf[pos++] = STATS_TYPE_CORE;
        buf[pos++] = stats.battMillivolts & 0xFF;
        buf[pos++] = (stats.battMillivolts >> 8) & 0xFF;
        buf[pos++] = stats.uptimeSecs & 0xFF;
        buf[pos++] = (stats.uptimeSecs >> 8) & 0xFF;
        buf[pos++] = (stats.uptimeSecs >> 16) & 0xFF;
        buf[pos++] = (stats.uptimeSecs >> 24) & 0xFF;
        buf[pos++] = stats.errFlags & 0xFF;
        buf[pos++] = (stats.errFlags >> 8) & 0xFF;
        buf[pos++] = stats.queueLen;

        return pos;
    }

    /**
     * Serialize radio stats for response
     */
    uint16_t serializeRadioStats(uint8_t* buf) {
        uint16_t pos = 0;

        buf[pos++] = STATS_TYPE_RADIO;
        buf[pos++] = radioStats.noiseFloor & 0xFF;
        buf[pos++] = (radioStats.noiseFloor >> 8) & 0xFF;
        buf[pos++] = (uint8_t)radioStats.lastRssi;
        buf[pos++] = (uint8_t)radioStats.lastSnr;
        buf[pos++] = radioStats.txAirTimeSec & 0xFF;
        buf[pos++] = (radioStats.txAirTimeSec >> 8) & 0xFF;
        buf[pos++] = (radioStats.txAirTimeSec >> 16) & 0xFF;
        buf[pos++] = (radioStats.txAirTimeSec >> 24) & 0xFF;
        buf[pos++] = radioStats.rxAirTimeSec & 0xFF;
        buf[pos++] = (radioStats.rxAirTimeSec >> 8) & 0xFF;
        buf[pos++] = (radioStats.rxAirTimeSec >> 16) & 0xFF;
        buf[pos++] = (radioStats.rxAirTimeSec >> 24) & 0xFF;

        return pos;
    }

    /**
     * Serialize packet stats for response
     */
    uint16_t serializePacketStats(uint8_t* buf) {
        uint16_t pos = 0;

        buf[pos++] = STATS_TYPE_PACKETS;

        // numRecvPackets
        buf[pos++] = pktStats.numRecvPackets & 0xFF;
        buf[pos++] = (pktStats.numRecvPackets >> 8) & 0xFF;
        buf[pos++] = (pktStats.numRecvPackets >> 16) & 0xFF;
        buf[pos++] = (pktStats.numRecvPackets >> 24) & 0xFF;

        // numSentPackets
        buf[pos++] = pktStats.numSentPackets & 0xFF;
        buf[pos++] = (pktStats.numSentPackets >> 8) & 0xFF;
        buf[pos++] = (pktStats.numSentPackets >> 16) & 0xFF;
        buf[pos++] = (pktStats.numSentPackets >> 24) & 0xFF;

        // numSentFlood
        buf[pos++] = pktStats.numSentFlood & 0xFF;
        buf[pos++] = (pktStats.numSentFlood >> 8) & 0xFF;
        buf[pos++] = (pktStats.numSentFlood >> 16) & 0xFF;
        buf[pos++] = (pktStats.numSentFlood >> 24) & 0xFF;

        // numSentDirect
        buf[pos++] = pktStats.numSentDirect & 0xFF;
        buf[pos++] = (pktStats.numSentDirect >> 8) & 0xFF;
        buf[pos++] = (pktStats.numSentDirect >> 16) & 0xFF;
        buf[pos++] = (pktStats.numSentDirect >> 24) & 0xFF;

        // numRecvFlood
        buf[pos++] = pktStats.numRecvFlood & 0xFF;
        buf[pos++] = (pktStats.numRecvFlood >> 8) & 0xFF;
        buf[pos++] = (pktStats.numRecvFlood >> 16) & 0xFF;
        buf[pos++] = (pktStats.numRecvFlood >> 24) & 0xFF;

        // numRecvDirect
        buf[pos++] = pktStats.numRecvDirect & 0xFF;
        buf[pos++] = (pktStats.numRecvDirect >> 8) & 0xFF;
        buf[pos++] = (pktStats.numRecvDirect >> 16) & 0xFF;
        buf[pos++] = (pktStats.numRecvDirect >> 24) & 0xFF;

        return pos;
    }

    /**
     * Check if discovery response is allowed (rate limiting)
     */
    bool canRespondToDiscover() {
        return discoverLimiter.allow();
    }

    /**
     * Build discovery response payload
     * @param buf Output buffer
     * @param inboundSnr SNR of discovery request
     * @param requestTag Tag from request (for correlation)
     * @return Bytes written
     */
    uint16_t buildDiscoverResponse(uint8_t* buf, int8_t inboundSnr, uint32_t requestTag) {
        if (!identity) return 0;

        uint16_t pos = 0;

        // Response type (upper nibble = response flag)
        buf[pos++] = CTL_TYPE_DISCOVER_RESP;

        // Node type flags (repeater)
        buf[pos++] = MC_TYPE_REPEATER;

        // Inbound SNR
        buf[pos++] = (uint8_t)inboundSnr;

        // Request tag (for correlation)
        buf[pos++] = requestTag & 0xFF;
        buf[pos++] = (requestTag >> 8) & 0xFF;
        buf[pos++] = (requestTag >> 16) & 0xFF;
        buf[pos++] = (requestTag >> 24) & 0xFF;

        // Public key prefix (first 8 bytes)
        memcpy(&buf[pos], identity->getPublicKey(), 8);
        pos += 8;

        return pos;
    }

    /**
     * Parse discovery request
     * @param payload Control packet payload
     * @param payloadLen Payload length
     * @param filterMask Output: node type filter bitmask
     * @param sinceTimestamp Output: only respond if modified since this time
     * @param requestTag Output: tag for response correlation
     * @return true if valid discover request for repeater
     */
    bool parseDiscoverRequest(const uint8_t* payload, uint16_t payloadLen,
                              uint8_t* filterMask, uint32_t* sinceTimestamp, uint32_t* requestTag) {
        if (payloadLen < 1) return false;

        // Check type
        if ((payload[0] & 0xF0) != (CTL_TYPE_DISCOVER_REQ & 0xF0)) {
            return false;
        }

        // Default values
        *filterMask = 0xFF;  // All types
        *sinceTimestamp = 0;
        *requestTag = 0;

        // Parse optional fields
        if (payloadLen >= 2) {
            *filterMask = payload[1];
        }

        if (payloadLen >= 6) {
            *sinceTimestamp = payload[2] | (payload[3] << 8) |
                             (payload[4] << 16) | (payload[5] << 24);
        }

        if (payloadLen >= 10) {
            *requestTag = payload[6] | (payload[7] << 8) |
                         (payload[8] << 16) | (payload[9] << 24);
        }

        // Check if we match the filter
        return (*filterMask & (1 << MC_TYPE_REPEATER)) != 0;
    }

    /**
     * Clean up expired data
     */
    void cleanup() {
        neighbours.cleanExpired();
    }
};

//=============================================================================
// Region Filtering (simplified for CubeCell)
//=============================================================================

#define MAX_REGIONS             4
#define MAX_TRANSPORT_CODES     8

/**
 * Region definition for packet filtering
 */
struct RegionDef {
    uint8_t transportCodes[MAX_TRANSPORT_CODES];  // Allowed transport codes
    uint8_t numCodes;                              // Number of codes in list
    bool denyByDefault;                            // Deny packets not matching
    bool valid;                                    // Entry is valid

    void clear() {
        memset(transportCodes, 0, MAX_TRANSPORT_CODES);
        numCodes = 0;
        denyByDefault = false;
        valid = false;
    }
};

/**
 * Region Manager - filters packets by transport codes
 */
class RegionManager {
private:
    RegionDef regions[MAX_REGIONS];
    bool filterEnabled;

public:
    RegionManager() : filterEnabled(false) {
        for (int i = 0; i < MAX_REGIONS; i++) {
            regions[i].clear();
        }
    }

    /**
     * Enable/disable region filtering
     */
    void setEnabled(bool en) {
        filterEnabled = en;
    }

    bool isEnabled() const {
        return filterEnabled;
    }

    /**
     * Add a region with transport codes
     * @param codes Array of transport codes
     * @param numCodes Number of codes
     * @param deny If true, deny packets matching these codes
     * @return Region index, or -1 if full
     */
    int addRegion(const uint8_t* codes, uint8_t numCodes, bool deny = false) {
        for (int i = 0; i < MAX_REGIONS; i++) {
            if (!regions[i].valid) {
                uint8_t n = (numCodes > MAX_TRANSPORT_CODES) ? MAX_TRANSPORT_CODES : numCodes;
                memcpy(regions[i].transportCodes, codes, n);
                regions[i].numCodes = n;
                regions[i].denyByDefault = deny;
                regions[i].valid = true;
                return i;
            }
        }
        return -1;  // No room
    }

    /**
     * Remove a region
     */
    bool removeRegion(uint8_t idx) {
        if (idx < MAX_REGIONS && regions[idx].valid) {
            regions[idx].clear();
            return true;
        }
        return false;
    }

    /**
     * Clear all regions
     */
    void clearAll() {
        for (int i = 0; i < MAX_REGIONS; i++) {
            regions[i].clear();
        }
        filterEnabled = false;
    }

    /**
     * Check if packet should be forwarded based on transport code
     * @param transportCode Transport code from packet (first byte after path)
     * @return true if packet should be forwarded
     */
    bool shouldForward(uint8_t transportCode) {
        if (!filterEnabled) return true;  // No filtering

        // Check each region
        for (int i = 0; i < MAX_REGIONS; i++) {
            if (!regions[i].valid) continue;

            bool matches = false;
            for (int j = 0; j < regions[i].numCodes; j++) {
                if (regions[i].transportCodes[j] == transportCode) {
                    matches = true;
                    break;
                }
            }

            if (regions[i].denyByDefault) {
                // Deny list - if matches, don't forward
                if (matches) return false;
            } else {
                // Allow list - if matches, forward
                if (matches) return true;
            }
        }

        // Default: forward if no deny rules matched
        return true;
    }

    /**
     * Get region count
     */
    uint8_t getCount() const {
        uint8_t cnt = 0;
        for (int i = 0; i < MAX_REGIONS; i++) {
            if (regions[i].valid) cnt++;
        }
        return cnt;
    }

    /**
     * Get region by index
     */
    const RegionDef* getRegion(uint8_t idx) const {
        if (idx < MAX_REGIONS && regions[idx].valid) {
            return &regions[idx];
        }
        return nullptr;
    }
};

//=============================================================================
// Packet Logger (simplified - stores to RAM ring buffer)
//=============================================================================

#define PACKET_LOG_SIZE         32  // Number of log entries (limited RAM)

struct PacketLogEntry {
    uint32_t timestamp;         // millis() when logged
    uint8_t direction;          // 0=RX, 1=TX
    uint8_t routeType;          // Route type
    uint8_t payloadType;        // Payload type
    uint8_t pathLen;            // Path length
    int8_t snr;                 // SNR * 4
    int8_t rssi;                // RSSI (clamped to int8)
    uint8_t srcHash;            // Source hash (first byte of path or payload)
    uint8_t dstHash;            // Destination hash
    bool valid;

    void clear() {
        timestamp = 0;
        direction = 0;
        routeType = 0;
        payloadType = 0;
        pathLen = 0;
        snr = 0;
        rssi = 0;
        srcHash = 0;
        dstHash = 0;
        valid = false;
    }
};

class PacketLogger {
private:
    PacketLogEntry entries[PACKET_LOG_SIZE];
    uint8_t writeIdx;
    bool enabled;
    uint32_t totalLogged;

public:
    PacketLogger() : writeIdx(0), enabled(false), totalLogged(0) {
        for (int i = 0; i < PACKET_LOG_SIZE; i++) {
            entries[i].clear();
        }
    }

    void setEnabled(bool en) {
        enabled = en;
    }

    bool isEnabled() const {
        return enabled;
    }

    /**
     * Log a packet
     * @param pkt Packet to log
     * @param isTx true if TX, false if RX
     */
    void log(const MCPacket* pkt, bool isTx) {
        if (!enabled) return;

        PacketLogEntry& e = entries[writeIdx];
        e.timestamp = millis();
        e.direction = isTx ? 1 : 0;
        e.routeType = pkt->header.getRouteType();
        e.payloadType = pkt->header.getPayloadType();
        e.pathLen = pkt->pathLen;
        e.snr = pkt->snr;
        e.rssi = (pkt->rssi < -128) ? -128 : (pkt->rssi > 127) ? 127 : (int8_t)pkt->rssi;

        // Extract hashes from payload if available
        if (pkt->payloadLen >= 2) {
            e.dstHash = pkt->payload[0];
            e.srcHash = pkt->payload[1];
        } else {
            e.dstHash = 0;
            e.srcHash = 0;
        }

        e.valid = true;
        totalLogged++;

        writeIdx = (writeIdx + 1) % PACKET_LOG_SIZE;
    }

    /**
     * Get entry by index (0 = newest)
     */
    const PacketLogEntry* getEntry(uint8_t idx) const {
        if (idx >= PACKET_LOG_SIZE) return nullptr;

        // Calculate actual index (ring buffer)
        int actualIdx = (writeIdx - 1 - idx + PACKET_LOG_SIZE) % PACKET_LOG_SIZE;
        if (entries[actualIdx].valid) {
            return &entries[actualIdx];
        }
        return nullptr;
    }

    /**
     * Get total logged count
     */
    uint32_t getTotalLogged() const {
        return totalLogged;
    }

    /**
     * Get current entry count (up to PACKET_LOG_SIZE)
     */
    uint8_t getCount() const {
        uint8_t cnt = 0;
        for (int i = 0; i < PACKET_LOG_SIZE; i++) {
            if (entries[i].valid) cnt++;
        }
        return cnt;
    }

    /**
     * Clear all entries
     */
    void clear() {
        for (int i = 0; i < PACKET_LOG_SIZE; i++) {
            entries[i].clear();
        }
        writeIdx = 0;
        totalLogged = 0;
    }

    /**
     * Dump log to serial (for debugging)
     */
    void dump() {
        Serial.printf("=== Packet Log (%lu total) ===\n\r", totalLogged);
        for (int i = 0; i < PACKET_LOG_SIZE; i++) {
            const PacketLogEntry* e = getEntry(i);
            if (e) {
                Serial.printf("%lu %s R=%d T=%d P=%d SNR=%d RSSI=%d %02X->%02X\n\r",
                    e->timestamp,
                    e->direction ? "TX" : "RX",
                    e->routeType,
                    e->payloadType,
                    e->pathLen,
                    e->snr / 4,
                    e->rssi,
                    e->srcHash,
                    e->dstHash);
            }
        }
    }
};

//=============================================================================
// CayenneLPP Telemetry Encoder
//=============================================================================

// CayenneLPP Data Types
#define LPP_DIGITAL_INPUT       0x00
#define LPP_DIGITAL_OUTPUT      0x01
#define LPP_ANALOG_INPUT        0x02  // 0.01 signed
#define LPP_ANALOG_OUTPUT       0x03
#define LPP_LUMINOSITY          0x65  // 101
#define LPP_PRESENCE            0x66  // 102
#define LPP_TEMPERATURE         0x67  // 103 - 0.1°C signed
#define LPP_RELATIVE_HUMIDITY   0x68  // 104 - 0.5% unsigned
#define LPP_ACCELEROMETER       0x71  // 113
#define LPP_BAROMETRIC_PRESSURE 0x73  // 115 - 0.1 hPa
#define LPP_VOLTAGE             0x74  // 116 - 0.01V unsigned (MeshCore)
#define LPP_GYROMETER           0x86  // 134
#define LPP_GPS                 0x88  // 136 - lat/lon/alt

class CayenneLPP {
private:
    uint8_t* buffer;
    uint16_t maxSize;
    uint16_t cursor;

public:
    CayenneLPP(uint8_t* buf, uint16_t size) : buffer(buf), maxSize(size), cursor(0) {}

    void reset() {
        cursor = 0;
    }

    uint16_t getSize() const {
        return cursor;
    }

    /**
     * Add voltage (MeshCore format, LPP type 116)
     * @param channel Channel number (TELEM_CHANNEL_SELF = 1)
     * @param voltage Voltage in volts (e.g., 4.12)
     */
    bool addVoltage(uint8_t channel, float voltage) {
        if (cursor + 4 > maxSize) return false;

        uint16_t val = (uint16_t)(voltage * 100);
        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_VOLTAGE;
        buffer[cursor++] = (val >> 8) & 0xFF;
        buffer[cursor++] = val & 0xFF;
        return true;
    }

    /**
     * Add analog input (standard CayenneLPP)
     * @param channel Channel number
     * @param value Value * 100 (e.g., 4.12V = 412)
     */
    bool addAnalogInput(uint8_t channel, float value) {
        if (cursor + 4 > maxSize) return false;

        int16_t val = (int16_t)(value * 100);
        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_ANALOG_INPUT;
        buffer[cursor++] = (val >> 8) & 0xFF;
        buffer[cursor++] = val & 0xFF;
        return true;
    }

    /**
     * Add temperature
     * @param channel Channel number
     * @param celsius Temperature in Celsius
     */
    bool addTemperature(uint8_t channel, float celsius) {
        if (cursor + 4 > maxSize) return false;

        int16_t val = (int16_t)(celsius * 10);
        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_TEMPERATURE;
        buffer[cursor++] = (val >> 8) & 0xFF;
        buffer[cursor++] = val & 0xFF;
        return true;
    }

    /**
     * Add humidity
     * @param channel Channel number
     * @param humidity Humidity percentage (0-100)
     */
    bool addRelativeHumidity(uint8_t channel, float humidity) {
        if (cursor + 3 > maxSize) return false;

        uint8_t val = (uint8_t)(humidity * 2);
        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_RELATIVE_HUMIDITY;
        buffer[cursor++] = val;
        return true;
    }

    /**
     * Add barometric pressure
     * @param channel Channel number
     * @param hPa Pressure in hPa
     */
    bool addBarometricPressure(uint8_t channel, float hPa) {
        if (cursor + 4 > maxSize) return false;

        uint16_t val = (uint16_t)(hPa * 10);
        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_BAROMETRIC_PRESSURE;
        buffer[cursor++] = (val >> 8) & 0xFF;
        buffer[cursor++] = val & 0xFF;
        return true;
    }

    /**
     * Add GPS coordinates
     * @param channel Channel number
     * @param latitude Latitude in degrees
     * @param longitude Longitude in degrees
     * @param altitude Altitude in meters
     */
    bool addGPS(uint8_t channel, float latitude, float longitude, float altitude) {
        if (cursor + 11 > maxSize) return false;

        int32_t lat = (int32_t)(latitude * 10000);
        int32_t lon = (int32_t)(longitude * 10000);
        int32_t alt = (int32_t)(altitude * 100);

        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_GPS;

        // Latitude (3 bytes, MSB first)
        buffer[cursor++] = (lat >> 16) & 0xFF;
        buffer[cursor++] = (lat >> 8) & 0xFF;
        buffer[cursor++] = lat & 0xFF;

        // Longitude (3 bytes, MSB first)
        buffer[cursor++] = (lon >> 16) & 0xFF;
        buffer[cursor++] = (lon >> 8) & 0xFF;
        buffer[cursor++] = lon & 0xFF;

        // Altitude (3 bytes, MSB first)
        buffer[cursor++] = (alt >> 16) & 0xFF;
        buffer[cursor++] = (alt >> 8) & 0xFF;
        buffer[cursor++] = alt & 0xFF;

        return true;
    }

    /**
     * Add digital input
     * @param channel Channel number
     * @param value 0 or 1
     */
    bool addDigitalInput(uint8_t channel, uint8_t value) {
        if (cursor + 3 > maxSize) return false;

        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_DIGITAL_INPUT;
        buffer[cursor++] = value ? 1 : 0;
        return true;
    }

    /**
     * Add presence sensor
     * @param channel Channel number
     * @param value 0 or 1
     */
    bool addPresence(uint8_t channel, uint8_t value) {
        if (cursor + 3 > maxSize) return false;

        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_PRESENCE;
        buffer[cursor++] = value ? 1 : 0;
        return true;
    }

    /**
     * Add luminosity
     * @param channel Channel number
     * @param lux Luminosity in lux
     */
    bool addLuminosity(uint8_t channel, uint16_t lux) {
        if (cursor + 4 > maxSize) return false;

        buffer[cursor++] = channel;
        buffer[cursor++] = LPP_LUMINOSITY;
        buffer[cursor++] = (lux >> 8) & 0xFF;
        buffer[cursor++] = lux & 0xFF;
        return true;
    }
};
