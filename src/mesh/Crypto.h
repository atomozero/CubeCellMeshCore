#pragma once
#include <Arduino.h>
#include <AES.h>
#include <SHA256.h>
#include <RNG.h>
#include "Identity.h"
#include "ed25519_orlp.h"  // orlp/ed25519 for key exchange

/**
 * MeshCore Encryption Module
 * Implements X25519 ECDH key exchange and AES-128-ECB + HMAC encryption
 *
 * Based on MeshCore protocol specification:
 * - Ed25519 keys used for ECDH via ed25519_key_exchange
 * - AES-128-ECB block cipher (NOT CBC!)
 * - SHA256-HMAC truncated to 2 bytes
 * - Encrypt-then-MAC construction
 *
 * ANON_REQ payload format (MeshCore V1):
 * [dest_hash: 1 byte][ephemeral_pubkey: 32 bytes][MAC: 2 bytes][ciphertext: variable]
 *
 * Encrypted plaintext format:
 * [timestamp: 4 bytes][password: up to 15 bytes]
 */

// Crypto sizes (from MeshCore MeshCore.h)
#define MC_SHARED_SECRET_SIZE   32
#define MC_AES_KEY_SIZE         16      // CIPHER_KEY_SIZE = 16
#define MC_AES_BLOCK_SIZE       16
#define MC_CIPHER_MAC_SIZE      2       // CIPHER_MAC_SIZE = 2 (truncated HMAC-SHA256)

// ANON_REQ payload structure
#define ANON_REQ_TIMESTAMP_SIZE 4

// LOGIN response codes
#define RESP_SERVER_LOGIN_OK    0x00
#define RESP_SERVER_LOGIN_FAIL  0x01

/**
 * MeshCore Crypto Operations
 */
class MeshCrypto {
private:
    uint8_t sharedSecret[MC_SHARED_SECRET_SIZE];
    bool hasSecret;

    // AES-128 cipher instance
    AESTiny128 aes;

    /**
     * XOR a block with another
     */
    static void xorBlock(uint8_t* dest, const uint8_t* src, size_t len) {
        for (size_t i = 0; i < len; i++) {
            dest[i] ^= src[i];
        }
    }

    /**
     * Zero-pad data to AES block boundary
     * @param padded Output buffer (must be large enough)
     * @param data Input data
     * @param len Input length
     * @return Padded length (multiple of 16)
     */
    static uint16_t zeroPad(uint8_t* padded, const uint8_t* data, uint16_t len) {
        memcpy(padded, data, len);
        uint16_t paddedLen = ((len + MC_AES_BLOCK_SIZE - 1) / MC_AES_BLOCK_SIZE) * MC_AES_BLOCK_SIZE;
        if (paddedLen > len) {
            memset(padded + len, 0, paddedLen - len);
        }
        return paddedLen > 0 ? paddedLen : MC_AES_BLOCK_SIZE;
    }

public:
    MeshCrypto() : hasSecret(false) {}

    /**
     * Calculate ECDH shared secret using Ed25519 key exchange
     * MeshCore uses ed25519_key_exchange which converts Ed25519→X25519 internally
     *
     * @param secret Output shared secret (32 bytes)
     * @param myPrivateKey Our Ed25519 private key (64-byte expanded key)
     * @param theirPublicKey Their Ed25519 public key (32 bytes)
     * @return true if successful
     */
    static bool calcSharedSecret(uint8_t* secret, const uint8_t* myPrivateKey,
                                 const uint8_t* theirPublicKey) {
        // Debug: show keys
        Serial.printf("[KX] myPriv[0-7]: ");
        for(int i=0; i<8; i++) Serial.printf("%02X ", myPrivateKey[i]);
        Serial.printf("\n\r");
        Serial.printf("[KX] theirPub[0-7]: ");
        for(int i=0; i<8; i++) Serial.printf("%02X ", theirPublicKey[i]);
        Serial.printf("\n\r");

        // Use ed25519_key_exchange (converts Ed25519 pubkey to X25519 internally)
        // This is what MeshCore uses
        ed25519_key_exchange(secret, theirPublicKey, myPrivateKey);

        Serial.printf("[KX] Ed25519 result[0-7]: ");
        for(int i=0; i<8; i++) Serial.printf("%02X ", secret[i]);
        Serial.printf("\n\r");

        return true;
    }

    /**
     * Set pre-computed shared secret (for cached contacts)
     */
    void setSharedSecret(const uint8_t* secret) {
        memcpy(sharedSecret, secret, MC_SHARED_SECRET_SIZE);
        hasSecret = true;
    }

    /**
     * Get the current shared secret
     */
    const uint8_t* getSharedSecret() const {
        return hasSecret ? sharedSecret : nullptr;
    }

    /**
     * Compute HMAC-SHA256 (truncated to 2 bytes for MeshCore V1)
     * @param mac Output MAC (2 bytes)
     * @param key HMAC key (shared secret, 32 bytes)
     * @param data Data to authenticate
     * @param len Data length
     */
    static void computeHMAC(uint8_t* mac, const uint8_t* key,
                            const uint8_t* data, uint16_t len) {
        SHA256 sha;
        uint8_t fullMac[32];

        sha.resetHMAC(key, MC_SHARED_SECRET_SIZE);
        sha.update(data, len);
        sha.finalizeHMAC(key, MC_SHARED_SECRET_SIZE, fullMac, 32);

        // Truncate to 2 bytes (MeshCore CIPHER_MAC_SIZE = 2)
        memcpy(mac, fullMac, MC_CIPHER_MAC_SIZE);
    }

    /**
     * Verify HMAC-SHA256
     * @param mac Expected MAC (2 bytes)
     * @param key HMAC key (shared secret, 32 bytes)
     * @param data Data to verify
     * @param len Data length
     * @return true if MAC matches
     */
    static bool verifyHMAC(const uint8_t* mac, const uint8_t* key,
                           const uint8_t* data, uint16_t len) {
        uint8_t computed[MC_CIPHER_MAC_SIZE];
        computeHMAC(computed, key, data, len);

        // Debug: show MAC comparison
        Serial.printf("[MAC] received=%02X%02X computed=%02X%02X over %d bytes\n\r",
                      mac[0], mac[1], computed[0], computed[1], len);

        // Constant-time comparison
        uint8_t diff = 0;
        for (int i = 0; i < MC_CIPHER_MAC_SIZE; i++) {
            diff |= mac[i] ^ computed[i];
        }
        return diff == 0;
    }

    /**
     * Encrypt data using AES-128-ECB (MeshCore style)
     * Then compute HMAC over ciphertext (encrypt-then-MAC)
     *
     * MeshCore format: [MAC:2][ciphertext]
     *
     * @param output Output buffer: [MAC:2][ciphertext]
     * @param input Plaintext data
     * @param len Input length
     * @param key Encryption key (first 16 bytes of shared secret)
     * @param macKey MAC key (full 32-byte shared secret)
     * @return Output length (2 bytes MAC + padded ciphertext)
     */
    uint16_t encryptThenMAC(uint8_t* output, const uint8_t* input, uint16_t len,
                            const uint8_t* key, const uint8_t* macKey) {
        // Pad input to block size
        uint8_t padded[256];
        uint16_t paddedLen = zeroPad(padded, input, len);

        // Set AES key (first 16 bytes)
        aes.setKey(key, MC_AES_KEY_SIZE);

        // Encrypt blocks (ECB mode - each block independently)
        uint16_t pos = 0;
        uint16_t outPos = MC_CIPHER_MAC_SIZE;  // Leave room for MAC at start

        while (pos < paddedLen) {
            // Encrypt block directly (ECB = no chaining)
            aes.encryptBlock(&output[outPos], &padded[pos]);
            pos += MC_AES_BLOCK_SIZE;
            outPos += MC_AES_BLOCK_SIZE;
        }

        // Compute HMAC over ciphertext and put at beginning
        computeHMAC(output, macKey, &output[MC_CIPHER_MAC_SIZE], paddedLen);

        aes.clear();
        return MC_CIPHER_MAC_SIZE + paddedLen;
    }

    /**
     * Verify MAC then decrypt (MAC-then-decrypt)
     * MeshCore format: [MAC:2][ciphertext]
     *
     * @param output Output buffer for plaintext
     * @param input Encrypted data: [MAC:2][ciphertext]
     * @param len Input length (including MAC)
     * @param key Decryption key (first 16 bytes of shared secret)
     * @param macKey MAC key (full 32-byte shared secret)
     * @return Decrypted length, 0 if MAC failed
     */
    uint16_t MACThenDecrypt(uint8_t* output, const uint8_t* input, uint16_t len,
                            const uint8_t* key, const uint8_t* macKey) {
        if (len < MC_CIPHER_MAC_SIZE + MC_AES_BLOCK_SIZE) {
            return 0;  // Too short
        }

        uint16_t cipherLen = len - MC_CIPHER_MAC_SIZE;
        const uint8_t* mac = input;
        const uint8_t* ciphertext = &input[MC_CIPHER_MAC_SIZE];

        // Verify HMAC first (MAC is over ciphertext only)
        if (!verifyHMAC(mac, macKey, ciphertext, cipherLen)) {
            return 0;  // MAC verification failed
        }

        // Set AES key
        aes.setKey(key, MC_AES_KEY_SIZE);

        // Decrypt blocks (ECB mode - each block independently)
        uint16_t pos = 0;

        while (pos < cipherLen) {
            // Decrypt block directly (ECB = no chaining)
            aes.decryptBlock(&output[pos], &ciphertext[pos]);
            pos += MC_AES_BLOCK_SIZE;
        }

        aes.clear();

        // Return decrypted length
        return cipherLen;
    }

    /**
     * Decrypt anonymous request (ANON_REQ)
     *
     * ANON_REQ payload format (MeshCore V1):
     * [dest_hash: 1][ephemeral_pubkey: 32][MAC: 2][ciphertext: variable]
     *
     * Note: Caller should pass payload starting from ephemeral_pubkey
     *       (i.e., skip the dest_hash byte which was already verified)
     *
     * Encrypted plaintext format:
     * [timestamp: 4 bytes][password: up to 15 bytes]
     *
     * @param timestamp Output timestamp
     * @param password Output password buffer
     * @param maxPwdLen Max password buffer size
     * @param payload ANON_REQ payload starting from ephemeral pubkey
     * @param payloadLen Payload length (from ephemeral pubkey to end)
     * @param myPrivateKey Our Ed25519 private key
     * @return Password length, 0 if decryption failed
     */
    uint8_t decryptAnonReq(uint32_t* timestamp, char* password, uint8_t maxPwdLen,
                           const uint8_t* payload, uint16_t payloadLen,
                           const uint8_t* myPrivateKey) {
        // Minimum: 32 (ephemeral pubkey) + 2 (MAC) + 15 (min encrypted - timestamp + short pwd)
        // Note: 51-byte ANON_REQ with srcHash means: 51 - 2(hashes) = 49 bytes for ephemeral+MAC+cipher
        Serial.printf("[CRYPTO] decryptAnonReq: payloadLen=%d\n\r", payloadLen);
        if (payloadLen < 32 + MC_CIPHER_MAC_SIZE + 15) {
            Serial.printf("[CRYPTO] Payload too short: %d < %d\n\r", payloadLen, 32 + MC_CIPHER_MAC_SIZE + 15);
            return 0;
        }

        // Extract ephemeral public key (first 32 bytes)
        const uint8_t* ephemeralPub = payload;
        // Encrypted data starts after pubkey: [MAC:2][ciphertext]
        const uint8_t* encrypted = &payload[32];
        uint16_t encryptedLen = payloadLen - 32;

        // Calculate shared secret with ephemeral key
        uint8_t secret[MC_SHARED_SECRET_SIZE];
        if (!calcSharedSecret(secret, myPrivateKey, ephemeralPub)) {
            Serial.printf("[CRYPTO] calcSharedSecret failed\n\r");
            return 0;
        }

        // Debug: show shared secret (first 8 bytes)
        Serial.printf("[CRYPTO] SharedSecret[0-7]: ");
        for(int i=0; i<8; i++) Serial.printf("%02X ", secret[i]);
        Serial.printf("\n\r");

        // Debug: show MAC and ciphertext
        Serial.printf("[CRYPTO] encryptedLen=%d MAC=%02X%02X cipher[0-3]=%02X%02X%02X%02X\n\r",
                      encryptedLen, encrypted[0], encrypted[1],
                      encrypted[2], encrypted[3], encrypted[4], encrypted[5]);

        // Decrypt (handles [MAC:2][ciphertext] format)
        uint8_t decrypted[128];
        uint16_t decryptedLen = MACThenDecrypt(decrypted, encrypted, encryptedLen,
                                                secret, secret);

        // Debug: show decryption result
        Serial.printf("[CRYPTO] MACThenDecrypt returned %d bytes\n\r", decryptedLen);
        if (decryptedLen > 0) {
            Serial.printf("[CRYPTO] Decrypted ALL: ");
            for(int i=0; i<decryptedLen; i++) Serial.printf("%02X ", decrypted[i]);
            Serial.printf("\n\r");
            // Try to show as ASCII
            Serial.printf("[CRYPTO] As ASCII: '");
            for(int i=0; i<decryptedLen; i++) {
                char c = decrypted[i];
                Serial.printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            Serial.printf("'\n\r");
        }

        // Clear secret immediately
        memset(secret, 0, MC_SHARED_SECRET_SIZE);

        if (decryptedLen == 0) {
            return 0;  // Decryption failed (MAC mismatch)
        }

        // Extract timestamp (little-endian)
        *timestamp = decrypted[0] |
                     (decrypted[1] << 8) |
                     (decrypted[2] << 16) |
                     (decrypted[3] << 24);

        Serial.printf("[CRYPTO] Extracted timestamp: %lu (0x%08lX)\n\r", *timestamp, *timestamp);
        Serial.printf("[CRYPTO] Password bytes[4-11]: ");
        for(int i=4; i<12 && i<decryptedLen; i++) Serial.printf("%02X ", decrypted[i]);
        Serial.printf("\n\r");

        // Extract password (remaining bytes, may include padding zeros)
        uint8_t pwdLen = 0;
        for (uint8_t i = ANON_REQ_TIMESTAMP_SIZE; i < decryptedLen && i < maxPwdLen + ANON_REQ_TIMESTAMP_SIZE; i++) {
            if (decrypted[i] == 0) break;  // Null terminator or padding
            password[pwdLen++] = decrypted[i];
        }
        password[pwdLen] = '\0';

        // Clear decrypted data
        memset(decrypted, 0, sizeof(decrypted));

        return pwdLen;
    }

    /**
     * Encrypt response data
     *
     * @param output Output buffer: [Encrypted data][HMAC:16]
     * @param input Response data
     * @param len Input length
     * @param sharedSecret Pre-computed shared secret with client
     * @return Output length
     */
    uint16_t encryptResponse(uint8_t* output, const uint8_t* input, uint16_t len,
                             const uint8_t* sharedSecret) {
        return encryptThenMAC(output, input, len, sharedSecret, sharedSecret);
    }

    /**
     * Build LOGIN_OK response
     *
     * Response format:
     * [0-3]  Server timestamp (4 bytes, little-endian)
     * [4]    Response code (0 = LOGIN_OK)
     * [5]    Keep-alive interval (seconds / 4)
     * [6]    Admin flag (1=admin, 0=guest)
     * [7]    Full permission byte
     * [8-11] Random blob (4 bytes)
     * [12]   Firmware version
     *
     * @param output Output buffer for response data (unencrypted)
     * @param serverTime Current server Unix timestamp
     * @param isAdmin true if admin login
     * @param permissions Permission byte
     * @param keepAliveInterval Keep-alive in seconds
     * @param firmwareVersion Firmware version number
     * @return Response length (13 bytes)
     */
    static uint8_t buildLoginOKResponse(uint8_t* output, uint32_t serverTime,
                                        bool isAdmin, uint8_t permissions,
                                        uint8_t keepAliveInterval,
                                        uint8_t firmwareVersion) {
        // Timestamp (little-endian)
        output[0] = serverTime & 0xFF;
        output[1] = (serverTime >> 8) & 0xFF;
        output[2] = (serverTime >> 16) & 0xFF;
        output[3] = (serverTime >> 24) & 0xFF;

        // Response code
        output[4] = RESP_SERVER_LOGIN_OK;

        // Keep-alive interval (divided by 4 to fit in byte)
        output[5] = keepAliveInterval / 4;

        // Admin flag
        output[6] = isAdmin ? 1 : 0;

        // Full permissions
        output[7] = permissions;

        // Random blob for uniqueness
        RNG.rand(&output[8], 4);

        // Firmware version
        output[12] = firmwareVersion;

        return 13;
    }

    /**
     * Clear all sensitive data
     */
    void clear() {
        memset(sharedSecret, 0, MC_SHARED_SECRET_SIZE);
        hasSecret = false;
        aes.clear();
    }
};

/**
 * Session Manager for authenticated clients
 * Extends ACLManager with shared secrets
 */
struct ClientSession {
    uint8_t pubKey[MC_PUBLIC_KEY_SIZE];     // Client's public key
    uint8_t sharedSecret[MC_SHARED_SECRET_SIZE]; // Pre-computed ECDH secret
    uint8_t permissions;                     // Permission level
    uint32_t lastTimestamp;                  // Last seen timestamp (replay protection)
    uint32_t lastActivity;                   // Last activity (millis)
    uint8_t outPath[8];                      // Return path for direct routing
    uint8_t outPathLen;                      // Return path length
    bool active;                             // Session active flag
};

#define MAX_CLIENT_SESSIONS 8

class SessionManager {
private:
    ClientSession sessions[MAX_CLIENT_SESSIONS];
    char adminPassword[16];
    char guestPassword[16];

public:
    SessionManager() {
        memset(sessions, 0, sizeof(sessions));
        strcpy(adminPassword, "admin");  // Default
        strcpy(guestPassword, "");       // No guest by default
    }

    /**
     * Set admin password
     */
    void setAdminPassword(const char* pwd) {
        strncpy(adminPassword, pwd, 15);
        adminPassword[15] = '\0';
    }

    /**
     * Set guest password
     */
    void setGuestPassword(const char* pwd) {
        strncpy(guestPassword, pwd, 15);
        guestPassword[15] = '\0';
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
     * Verify login and create session
     * @param pubKey Client's public key
     * @param password Provided password
     * @param timestamp Request timestamp
     * @param myPrivateKey Our private key (for ECDH)
     * @param outPath Return path from packet
     * @param outPathLen Return path length
     * @return Permission level (0 = failed, PERM_ACL_ADMIN or PERM_ACL_GUEST)
     */
    uint8_t processLogin(const uint8_t* pubKey, const char* password,
                         uint32_t timestamp, const uint8_t* myPrivateKey,
                         const uint8_t* outPath, uint8_t outPathLen) {
        // Check passwords
        uint8_t permissions = 0;

        if (strlen(adminPassword) > 0 && strcmp(password, adminPassword) == 0) {
            permissions = PERM_ACL_ADMIN;
        } else if (strlen(guestPassword) > 0 && strcmp(password, guestPassword) == 0) {
            permissions = PERM_ACL_GUEST;
        } else {
            return 0;  // Invalid password
        }

        // Find or create session
        int freeSlot = -1;
        int existingSlot = -1;

        for (int i = 0; i < MAX_CLIENT_SESSIONS; i++) {
            if (sessions[i].active) {
                if (memcmp(sessions[i].pubKey, pubKey, MC_PUBLIC_KEY_SIZE) == 0) {
                    existingSlot = i;
                    break;
                }
            } else if (freeSlot < 0) {
                freeSlot = i;
            }
        }

        int slot = (existingSlot >= 0) ? existingSlot : freeSlot;
        if (slot < 0) {
            // No free slots, find oldest
            uint32_t oldest = 0xFFFFFFFF;
            for (int i = 0; i < MAX_CLIENT_SESSIONS; i++) {
                if (sessions[i].lastActivity < oldest) {
                    oldest = sessions[i].lastActivity;
                    slot = i;
                }
            }
        }

        // Check replay attack
        if (existingSlot >= 0 && timestamp <= sessions[existingSlot].lastTimestamp) {
            return 0;  // Replay attack detected
        }

        // Setup session
        ClientSession* s = &sessions[slot];
        memcpy(s->pubKey, pubKey, MC_PUBLIC_KEY_SIZE);

        // Calculate shared secret for future encrypted communication
        MeshCrypto::calcSharedSecret(s->sharedSecret, myPrivateKey, pubKey);

        s->permissions = permissions;
        s->lastTimestamp = timestamp;
        s->lastActivity = millis();
        s->active = true;

        // Store return path
        s->outPathLen = (outPathLen > 8) ? 8 : outPathLen;
        if (s->outPathLen > 0) {
            memcpy(s->outPath, outPath, s->outPathLen);
        }

        return permissions;
    }

    /**
     * Find session by public key
     * @return Pointer to session, or nullptr if not found
     */
    ClientSession* findSession(const uint8_t* pubKey) {
        for (int i = 0; i < MAX_CLIENT_SESSIONS; i++) {
            if (sessions[i].active &&
                memcmp(sessions[i].pubKey, pubKey, MC_PUBLIC_KEY_SIZE) == 0) {
                return &sessions[i];
            }
        }
        return nullptr;
    }

    /**
     * Check if a request is authorized
     * @param pubKey Client's public key
     * @param timestamp Request timestamp
     * @param requiredPerm Required permission level
     * @return true if authorized
     */
    bool checkAuth(const uint8_t* pubKey, uint32_t timestamp, uint8_t requiredPerm) {
        ClientSession* s = findSession(pubKey);
        if (!s) return false;

        // Check replay
        if (timestamp <= s->lastTimestamp) {
            return false;
        }

        // Check permissions
        if ((s->permissions & requiredPerm) == 0) {
            return false;
        }

        // Update timestamp
        s->lastTimestamp = timestamp;
        s->lastActivity = millis();

        return true;
    }

    /**
     * Get session count
     */
    uint8_t getSessionCount() const {
        uint8_t count = 0;
        for (int i = 0; i < MAX_CLIENT_SESSIONS; i++) {
            if (sessions[i].active) count++;
        }
        return count;
    }

    /**
     * Get session by index
     */
    const ClientSession* getSession(uint8_t index) const {
        if (index >= MAX_CLIENT_SESSIONS) return nullptr;
        return sessions[index].active ? &sessions[index] : nullptr;
    }

    /**
     * Clear inactive sessions (older than timeout)
     */
    void cleanupSessions(uint32_t timeoutMs = 3600000) {  // 1 hour default
        uint32_t now = millis();
        for (int i = 0; i < MAX_CLIENT_SESSIONS; i++) {
            if (sessions[i].active && (now - sessions[i].lastActivity) > timeoutMs) {
                memset(&sessions[i], 0, sizeof(ClientSession));
            }
        }
    }
};
