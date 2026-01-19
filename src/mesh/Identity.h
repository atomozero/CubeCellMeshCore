#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include <Ed25519.h>
#include <RNG.h>
#include <SHA512.h>

/**
 * MeshCore Node Identity Management
 * Handles Ed25519 key generation, storage, and ADVERT signing
 *
 * Based on MeshCore protocol specification
 */

// EEPROM layout for identity (starts after NodeConfig)
#define IDENTITY_EEPROM_OFFSET  32      // After NodeConfig
#define IDENTITY_MAGIC          0x4D43  // "MC" for MeshCore
#define IDENTITY_VERSION        1

// Key sizes
#define MC_PRIVATE_KEY_SIZE     32
#define MC_PUBLIC_KEY_SIZE      32
#define MC_SIGNATURE_SIZE       64
#define MC_NODE_NAME_MAX        16

// Node type (lower 4 bits of flags byte) - mutually exclusive values
#define MC_TYPE_MASK            0x0F
#define MC_TYPE_CHAT_NODE       0x01
#define MC_TYPE_REPEATER        0x02
#define MC_TYPE_ROOM_SERVER     0x03
#define MC_TYPE_SENSOR          0x04

// Flags (upper 4 bits of flags byte) - can be combined
#define MC_FLAG_HAS_LOCATION    0x10
#define MC_FLAG_FEATURE1        0x20
#define MC_FLAG_FEATURE2        0x40
#define MC_FLAG_HAS_NAME        0x80

/**
 * Node Identity stored in EEPROM
 */
struct NodeIdentity {
    uint16_t magic;                         // Magic number to validate
    uint8_t version;                        // Identity version
    uint8_t privateKey[MC_PRIVATE_KEY_SIZE]; // Ed25519 private key
    uint8_t publicKey[MC_PUBLIC_KEY_SIZE];   // Ed25519 public key
    char nodeName[MC_NODE_NAME_MAX];         // Node display name
    uint8_t flags;                          // Node type flags
    int32_t latitude;                       // Latitude * 1000000 (optional)
    int32_t longitude;                      // Longitude * 1000000 (optional)
    uint8_t reserved[8];                    // Reserved for future use
};

/**
 * Identity Manager Class
 * Handles key generation, storage, and cryptographic operations
 */
class IdentityManager {
private:
    NodeIdentity identity;
    bool initialized;

    // Seed RNG with hardware entropy
    void seedRNG() {
        // Use multiple entropy sources
        uint32_t seed = 0;

        #ifdef CUBECELL
        // Use chip ID
        uint64_t chipId = getID();
        seed ^= (uint32_t)(chipId ^ (chipId >> 32));

        // Use ADC noise
        for (int i = 0; i < 8; i++) {
            seed ^= analogRead(ADC) << (i * 4);
            delay(1);
        }

        // Use millis
        seed ^= millis();
        #endif

        // Stir in the seed
        RNG.stir((const uint8_t*)&seed, sizeof(seed));
    }

public:
    IdentityManager() : initialized(false) {}

    /**
     * Initialize identity manager
     * Loads from EEPROM or generates new identity
     * @return true if identity is valid and ready
     */
    bool begin() {
        // Seed RNG first
        seedRNG();

        // Try to load existing identity
        if (load()) {
            initialized = true;
            return true;
        }

        // Generate new identity
        if (generate()) {
            save();
            initialized = true;
            return true;
        }

        return false;
    }

    /**
     * Load identity from EEPROM
     * @return true if valid identity found
     */
    bool load() {
        EEPROM.get(IDENTITY_EEPROM_OFFSET, identity);

        if (identity.magic == IDENTITY_MAGIC &&
            identity.version == IDENTITY_VERSION) {
            return true;
        }

        return false;
    }

    /**
     * Save identity to EEPROM
     * @return true if save successful
     */
    bool save() {
        identity.magic = IDENTITY_MAGIC;
        identity.version = IDENTITY_VERSION;

        EEPROM.put(IDENTITY_EEPROM_OFFSET, identity);
        return EEPROM.commit();
    }

    /**
     * Generate new Ed25519 keypair
     * @return true if generation successful
     */
    bool generate() {
        // Wait for RNG to have enough entropy
        RNG.loop();

        // Generate private key
        Ed25519::generatePrivateKey(identity.privateKey);

        // Derive public key
        Ed25519::derivePublicKey(identity.publicKey, identity.privateKey);

        // Set default name - use MC_DEFAULT_NAME if defined and not empty
        #ifdef MC_DEFAULT_NAME
        const char* defaultName = MC_DEFAULT_NAME;
        if (defaultName[0] != '\0') {
            strncpy(identity.nodeName, defaultName, MC_NODE_NAME_MAX - 1);
            identity.nodeName[MC_NODE_NAME_MAX - 1] = '\0';
        } else {
            snprintf(identity.nodeName, MC_NODE_NAME_MAX, "CC-%02X%02X%02X",
                     identity.publicKey[0], identity.publicKey[1], identity.publicKey[2]);
        }
        #else
        snprintf(identity.nodeName, MC_NODE_NAME_MAX, "CC-%02X%02X%02X",
                 identity.publicKey[0], identity.publicKey[1], identity.publicKey[2]);
        #endif

        // Set as repeater by default (type in lower bits, flags in upper bits)
        identity.flags = MC_TYPE_REPEATER | MC_FLAG_HAS_NAME;

        // Set default location if defined
        #if defined(MC_DEFAULT_LATITUDE) && defined(MC_DEFAULT_LONGITUDE)
        if (MC_DEFAULT_LATITUDE != 0.0f || MC_DEFAULT_LONGITUDE != 0.0f) {
            identity.latitude = (int32_t)(MC_DEFAULT_LATITUDE * 1000000.0f);
            identity.longitude = (int32_t)(MC_DEFAULT_LONGITUDE * 1000000.0f);
            identity.flags |= MC_FLAG_HAS_LOCATION;
        } else {
            identity.latitude = 0;
            identity.longitude = 0;
        }
        #else
        identity.latitude = 0;
        identity.longitude = 0;
        #endif

        memset(identity.reserved, 0, sizeof(identity.reserved));

        return true;
    }

    /**
     * Get node hash (first byte of public key)
     * Used as compact node identifier in paths
     */
    uint8_t getNodeHash() const {
        return identity.publicKey[0];
    }

    /**
     * Get pointer to public key
     */
    const uint8_t* getPublicKey() const {
        return identity.publicKey;
    }

    /**
     * Get pointer to private key
     */
    const uint8_t* getPrivateKey() const {
        return identity.privateKey;
    }

    /**
     * Get node name
     */
    const char* getNodeName() const {
        return identity.nodeName;
    }

    /**
     * Set node name
     */
    void setNodeName(const char* name) {
        strncpy(identity.nodeName, name, MC_NODE_NAME_MAX - 1);
        identity.nodeName[MC_NODE_NAME_MAX - 1] = '\0';
        identity.flags |= MC_FLAG_HAS_NAME;
    }

    /**
     * Get flags byte
     */
    uint8_t getFlags() const {
        return identity.flags;
    }

    /**
     * Set location
     * @param lat Latitude in degrees
     * @param lon Longitude in degrees
     */
    void setLocation(float lat, float lon) {
        identity.latitude = (int32_t)(lat * 1000000.0f);
        identity.longitude = (int32_t)(lon * 1000000.0f);
        if (lat != 0.0f || lon != 0.0f) {
            identity.flags |= MC_FLAG_HAS_LOCATION;
        } else {
            identity.flags &= ~MC_FLAG_HAS_LOCATION;
        }
    }

    /**
     * Clear location
     */
    void clearLocation() {
        identity.latitude = 0;
        identity.longitude = 0;
        identity.flags &= ~MC_FLAG_HAS_LOCATION;
    }

    /**
     * Get latitude as float
     */
    float getLatitudeFloat() const {
        return identity.latitude / 1000000.0f;
    }

    /**
     * Get longitude as float
     */
    float getLongitudeFloat() const {
        return identity.longitude / 1000000.0f;
    }

    /**
     * Check if location is set
     */
    bool hasLocation() const {
        return (identity.flags & MC_FLAG_HAS_LOCATION) != 0;
    }

    /**
     * Get latitude
     */
    int32_t getLatitude() const {
        return identity.latitude;
    }

    /**
     * Get longitude
     */
    int32_t getLongitude() const {
        return identity.longitude;
    }

    /**
     * Sign data with Ed25519
     * @param signature Output buffer for 64-byte signature
     * @param data Data to sign
     * @param len Length of data
     */
    void sign(uint8_t* signature, const uint8_t* data, size_t len) {
        Ed25519::sign(signature, identity.privateKey, identity.publicKey, data, len);
    }

    /**
     * Verify Ed25519 signature
     * @param signature 64-byte signature
     * @param publicKey 32-byte public key
     * @param data Signed data
     * @param len Length of data
     * @return true if signature is valid
     */
    static bool verify(const uint8_t* signature, const uint8_t* publicKey,
                       const uint8_t* data, size_t len) {
        return Ed25519::verify(signature, publicKey, data, len);
    }

    /**
     * Check if identity is initialized
     */
    bool isInitialized() const {
        return initialized;
    }

    /**
     * Reset identity (generate new)
     */
    void reset() {
        generate();
        save();
    }

    /**
     * Print identity info to Serial
     */
    void printInfo() {
        Serial.printf("Node Name: %s\n\r", identity.nodeName);
        Serial.printf("Node Hash: %02X\n\r", getNodeHash());
        Serial.print("Public Key: ");
        for (int i = 0; i < 8; i++) {
            Serial.printf("%02X", identity.publicKey[i]);
        }
        Serial.println("...");
        Serial.printf("Flags: 0x%02X\n\r", identity.flags);
        if (hasLocation()) {
            Serial.printf("Location: %.6f, %.6f\n\r",
                         identity.latitude / 1000000.0f,
                         identity.longitude / 1000000.0f);
        }
    }
};
