#pragma once
#include <Arduino.h>
#include <AES.h>
#include <SHA256.h>
#include <Curve25519.h>
#include "Packet.h"
#include "Identity.h"
#include "ed25519_orlp.h"  // orlp/ed25519 library

/**
 * MeshCore Contact Management and Message Encryption
 *
 * Handles:
 * - Storage of known node public keys
 * - ECDH shared secret calculation (Ed25519 → X25519 conversion)
 * - AES-128 encryption with HMAC-SHA256 (Encrypt-then-MAC)
 *
 * Based on MeshCore protocol specification
 *
 * Key Exchange Algorithm:
 * Ed25519 keys are converted to X25519 for ECDH:
 * - Private key: SHA512(seed), then clamp bits 0,1,2,255,254 and set bit 254
 * - Public key: Edwards to Montgomery conversion using (1+y)/(1-y)
 */

// Contact storage limits
#define MC_MAX_CONTACTS         8
#define MC_CONTACT_NAME_MAX     16
#define MC_SHARED_SECRET_SIZE   32
#define MC_AES_BLOCK_SIZE       16
#define MC_MAC_SIZE             2       // Truncated HMAC

// Message limits
#define MC_MAX_MSG_PLAINTEXT    160
#define MC_MAX_MSG_ENCRYPTED    180

/**
 * Contact information structure
 */
struct Contact {
    uint8_t pubKey[MC_PUBLIC_KEY_SIZE];     // Full 32-byte public key
    uint8_t sharedSecret[MC_SHARED_SECRET_SIZE]; // Cached shared secret
    char name[MC_CONTACT_NAME_MAX];         // Node name
    uint32_t lastSeen;                      // millis() when last seen
    int16_t lastRssi;                       // Last RSSI
    int8_t lastSnr;                         // Last SNR
    bool hasSharedSecret;                   // Shared secret calculated
    bool valid;                             // Entry is valid

    void clear() {
        memset(pubKey, 0, sizeof(pubKey));
        memset(sharedSecret, 0, sizeof(sharedSecret));
        memset(name, 0, sizeof(name));
        lastSeen = 0;
        lastRssi = 0;
        lastSnr = 0;
        hasSharedSecret = false;
        valid = false;
    }

    uint8_t getHash() const {
        return pubKey[0];
    }
};

/**
 * Contact Manager Class
 */
class ContactManager {
private:
    Contact contacts[MC_MAX_CONTACTS];
    IdentityManager* identity;

public:
    ContactManager() : identity(nullptr) {
        clear();
    }

    void begin(IdentityManager* id) {
        identity = id;
        clear();
    }

    void clear() {
        for (uint8_t i = 0; i < MC_MAX_CONTACTS; i++) {
            contacts[i].clear();
        }
    }

    /**
     * Add or update contact from received ADVERT
     * @param pubKey 32-byte public key
     * @param name Node name (can be null)
     * @param rssi Signal strength
     * @param snr Signal-to-noise ratio
     * @return Pointer to contact, or nullptr if failed
     */
    Contact* updateFromAdvert(const uint8_t* pubKey, const char* name,
                               int16_t rssi, int8_t snr) {
        if (!pubKey) return nullptr;

        uint8_t hash = pubKey[0];

        // Check if already known (by hash - first byte of pubkey)
        for (uint8_t i = 0; i < MC_MAX_CONTACTS; i++) {
            if (contacts[i].valid && contacts[i].pubKey[0] == hash) {
                // Found contact with same hash - update it
                // Note: We update the pubkey in case it changed (some firmwares have bugs)
                memcpy(contacts[i].pubKey, pubKey, MC_PUBLIC_KEY_SIZE);
                contacts[i].lastSeen = millis();
                contacts[i].lastRssi = rssi;
                contacts[i].lastSnr = snr;
                contacts[i].hasSharedSecret = false;  // Recalculate if key changed
                if (name && name[0] != '\0') {
                    strncpy(contacts[i].name, name, MC_CONTACT_NAME_MAX - 1);
                    contacts[i].name[MC_CONTACT_NAME_MAX - 1] = '\0';
                }
                return &contacts[i];
            }
        }

        // Find empty slot or oldest contact
        int8_t emptySlot = -1;
        int8_t oldestSlot = 0;
        uint32_t oldestTime = UINT32_MAX;

        for (uint8_t i = 0; i < MC_MAX_CONTACTS; i++) {
            if (!contacts[i].valid) {
                emptySlot = i;
                break;
            }
            if (contacts[i].lastSeen < oldestTime) {
                oldestTime = contacts[i].lastSeen;
                oldestSlot = i;
            }
        }

        uint8_t slot = (emptySlot >= 0) ? emptySlot : oldestSlot;

        // Add new contact
        contacts[slot].clear();
        memcpy(contacts[slot].pubKey, pubKey, MC_PUBLIC_KEY_SIZE);
        contacts[slot].lastSeen = millis();
        contacts[slot].lastRssi = rssi;
        contacts[slot].lastSnr = snr;
        contacts[slot].valid = true;

        if (name && name[0] != '\0') {
            strncpy(contacts[slot].name, name, MC_CONTACT_NAME_MAX - 1);
            contacts[slot].name[MC_CONTACT_NAME_MAX - 1] = '\0';
        }

        Serial.printf("[CONTACT] Added: %s (hash=%02X)\n\r",
                      contacts[slot].name[0] ? contacts[slot].name : "Unknown",
                      hash);

        return &contacts[slot];
    }

    /**
     * Find contact by hash (first byte of public key)
     * @param hash 1-byte hash
     * @return Pointer to contact, or nullptr if not found
     */
    Contact* findByHash(uint8_t hash) {
        for (uint8_t i = 0; i < MC_MAX_CONTACTS; i++) {
            if (contacts[i].valid && contacts[i].pubKey[0] == hash) {
                return &contacts[i];
            }
        }
        return nullptr;
    }

    /**
     * Find contact by name (partial match, case-insensitive)
     * @param name Name to search
     * @return Pointer to contact, or nullptr if not found
     */
    Contact* findByName(const char* name) {
        if (!name || name[0] == '\0') return nullptr;

        size_t searchLen = strlen(name);

        for (uint8_t i = 0; i < MC_MAX_CONTACTS; i++) {
            if (contacts[i].valid && contacts[i].name[0] != '\0') {
                // Case-insensitive partial match
                const char* contactName = contacts[i].name;
                size_t contactLen = strlen(contactName);

                // Check if search string is found anywhere in contact name
                for (size_t j = 0; j <= contactLen - searchLen; j++) {
                    bool match = true;
                    for (size_t k = 0; k < searchLen; k++) {
                        char c1 = contactName[j + k];
                        char c2 = name[k];
                        // Convert to lowercase
                        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                        if (c1 != c2) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        return &contacts[i];
                    }
                }
            }
        }
        return nullptr;
    }

    /**
     * Calculate shared secret for a contact using ECDH
     * Uses orlp/ed25519 ed25519_key_exchange which handles Ed25519→X25519 conversion
     * @param contact Contact to calculate secret for
     * @return true if successful
     */
    bool calculateSharedSecret(Contact* contact) {
        if (!contact || !identity || !identity->isInitialized()) {
            return false;
        }

        // orlp/ed25519 key exchange uses 32-byte private key seed directly
        ed25519_key_exchange(contact->sharedSecret,
                             contact->pubKey,              // Their Ed25519 public key
                             identity->getPrivateKey());   // Our 32-byte private key seed

        contact->hasSharedSecret = true;
        return true;
    }

    /**
     * Get shared secret for a contact, calculating if needed
     * @param contact Contact
     * @return Pointer to 32-byte shared secret, or nullptr
     */
    const uint8_t* getSharedSecret(Contact* contact) {
        if (!contact) return nullptr;

        if (!contact->hasSharedSecret) {
            if (!calculateSharedSecret(contact)) {
                return nullptr;
            }
        }

        return contact->sharedSecret;
    }

    /**
     * Get contact count
     */
    uint8_t getCount() const {
        uint8_t count = 0;
        for (uint8_t i = 0; i < MC_MAX_CONTACTS; i++) {
            if (contacts[i].valid) count++;
        }
        return count;
    }

    /**
     * Get contact by index
     */
    Contact* getContact(uint8_t idx) {
        uint8_t count = 0;
        for (uint8_t i = 0; i < MC_MAX_CONTACTS; i++) {
            if (contacts[i].valid) {
                if (count == idx) return &contacts[i];
                count++;
            }
        }
        return nullptr;
    }

    /**
     * Print contact list
     */
    void printContacts() {
        Serial.printf("\n\r[CONTACTS] %d known:\n\r", getCount());
        for (uint8_t i = 0; i < MC_MAX_CONTACTS; i++) {
            if (contacts[i].valid) {
                uint32_t ago = (millis() - contacts[i].lastSeen) / 1000;
                Serial.printf("  [%02X] %s  rssi=%ddBm snr=%.2fdB ago=%lus secret=%s\n\r",
                              contacts[i].getHash(),
                              contacts[i].name[0] ? contacts[i].name : "(no name)",
                              contacts[i].lastRssi,
                              contacts[i].lastSnr / 4.0f,
                              ago,
                              contacts[i].hasSharedSecret ? "yes" : "no");
            }
        }
    }
};

/**
 * Message Encryption Utility Class
 * Implements MeshCore's Encrypt-then-MAC scheme
 */
class MessageCrypto {
private:
    AES128 aes;
    SHA256 sha256;

public:
    /**
     * Encrypt message using AES-128 and add HMAC-SHA256 (truncated to 2 bytes)
     *
     * @param sharedSecret 32-byte shared secret (first 16 bytes used as AES key)
     * @param output Output buffer for MAC + ciphertext
     * @param input Plaintext input
     * @param inputLen Length of plaintext
     * @return Length of output (MAC + ciphertext), or 0 on error
     */
    uint16_t encryptThenMAC(const uint8_t* sharedSecret, uint8_t* output,
                            const uint8_t* input, uint16_t inputLen) {
        if (!sharedSecret || !output || !input || inputLen == 0) {
            return 0;
        }

        // Pad to AES block size
        uint16_t paddedLen = ((inputLen + MC_AES_BLOCK_SIZE - 1) / MC_AES_BLOCK_SIZE) * MC_AES_BLOCK_SIZE;
        if (paddedLen > MC_MAX_MSG_ENCRYPTED - MC_MAC_SIZE) {
            return 0;  // Too long
        }

        // Create padded plaintext
        uint8_t padded[MC_MAX_MSG_ENCRYPTED];
        memset(padded, 0, paddedLen);
        memcpy(padded, input, inputLen);

        // Encrypt using AES-128-ECB (each block separately)
        // Output ciphertext starts after MAC position
        uint8_t* ciphertext = &output[MC_MAC_SIZE];
        aes.setKey(sharedSecret, 16);  // Use first 16 bytes of shared secret

        for (uint16_t i = 0; i < paddedLen; i += MC_AES_BLOCK_SIZE) {
            aes.encryptBlock(&ciphertext[i], &padded[i]);
        }

        // Calculate HMAC-SHA256 over ciphertext
        uint8_t mac[32];
        sha256.resetHMAC(sharedSecret, MC_SHARED_SECRET_SIZE);
        sha256.update(ciphertext, paddedLen);
        sha256.finalizeHMAC(sharedSecret, MC_SHARED_SECRET_SIZE, mac, 32);

        // Truncate MAC to 2 bytes and prepend
        output[0] = mac[0];
        output[1] = mac[1];

        return MC_MAC_SIZE + paddedLen;
    }

    /**
     * Verify MAC and decrypt message
     *
     * @param sharedSecret 32-byte shared secret
     * @param output Output buffer for plaintext
     * @param input MAC + ciphertext
     * @param inputLen Length of input
     * @return Length of plaintext, or 0 on MAC failure/error
     */
    uint16_t macThenDecrypt(const uint8_t* sharedSecret, uint8_t* output,
                            const uint8_t* input, uint16_t inputLen) {
        if (!sharedSecret || !output || !input || inputLen <= MC_MAC_SIZE) {
            return 0;
        }

        uint16_t ciphertextLen = inputLen - MC_MAC_SIZE;
        const uint8_t* ciphertext = &input[MC_MAC_SIZE];

        // Verify ciphertext is block-aligned
        if (ciphertextLen % MC_AES_BLOCK_SIZE != 0) {
            Serial.printf("[CRYPTO] Error: ciphertext not block-aligned\n\r");
            return 0;
        }

        // Calculate HMAC over ciphertext
        uint8_t mac[32];
        sha256.resetHMAC(sharedSecret, MC_SHARED_SECRET_SIZE);
        sha256.update(ciphertext, ciphertextLen);
        sha256.finalizeHMAC(sharedSecret, MC_SHARED_SECRET_SIZE, mac, 32);

        // Verify MAC (first 2 bytes)
        if (input[0] != mac[0] || input[1] != mac[1]) {
            Serial.printf("[CRYPTO] MAC verification failed\n\r");
            return 0;
        }

        // Decrypt using AES-128-ECB
        aes.setKey(sharedSecret, 16);

        for (uint16_t i = 0; i < ciphertextLen; i += MC_AES_BLOCK_SIZE) {
            aes.decryptBlock(&output[i], &ciphertext[i]);
        }

        return ciphertextLen;
    }

    /**
     * Calculate ACK hash for a message
     * SHA256(timestamp + type_attempt + text + sender_pubkey), truncated to 4 bytes
     *
     * @param output 4-byte output buffer
     * @param timestamp Message timestamp
     * @param typeAttempt Type + attempt byte
     * @param text Message text
     * @param senderPubKey 32-byte sender public key
     */
    void calculateAckHash(uint8_t* output, uint32_t timestamp, uint8_t typeAttempt,
                          const char* text, const uint8_t* senderPubKey) {
        sha256.reset();
        sha256.update(&timestamp, 4);
        sha256.update(&typeAttempt, 1);
        sha256.update(text, strlen(text));
        sha256.update(senderPubKey, MC_PUBLIC_KEY_SIZE);

        uint8_t hash[32];
        sha256.finalize(hash, 32);

        // Truncate to 4 bytes
        memcpy(output, hash, 4);
    }
};

// Global instances
extern ContactManager contactMgr;
extern MessageCrypto msgCrypto;
