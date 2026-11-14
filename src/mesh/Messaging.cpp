#include "../main.h"
#include "../core/Led.h"
#include "../core/Config.h"


//=============================================================================
// Ping / Test Packet
//=============================================================================
static uint16_t pingCounter = 0;

void sendPing() {
    MCPacket pkt;
    pkt.clear();

    // Create a FLOOD packet with PLAIN text payload
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    // Add our node hash as first path entry
    uint8_t myHash = (nodeId >> 24) ^ (nodeId >> 16) ^ (nodeId >> 8) ^ nodeId;
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Create payload: "PING #xxx from CCXXXXXX"
    pingCounter++;
    pkt.payloadLen = snprintf((char*)pkt.payload, MC_MAX_PAYLOAD_SIZE,
                               "PING #%u from %08lX", pingCounter, nodeId);

    LOG(TAG_PING " #%u\n\r", pingCounter);

    // Add to packet cache so we don't re-forward our own ping
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    // Transmit directly (no queue delay for ping)
    if (transmitPacket(&pkt)) {
        LOG(TAG_PING " TX ok\n\r");
    } else {
        LOG(TAG_PING " TX fail\n\r");
    }

    startReceive();
}

void sendDirectedPing(uint8_t targetHash) {
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    uint8_t myHash = nodeIdentity.getNodeHash();
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Payload: [destHash][srcHash]['D']['P'][text: "#N name"]
    pingCounter++;
    pkt.payload[0] = targetHash;
    pkt.payload[1] = myHash;
    pkt.payload[2] = 'D';
    pkt.payload[3] = 'P';
    pkt.payloadLen = 4 + snprintf((char*)&pkt.payload[4], MC_MAX_PAYLOAD_SIZE - 4,
                                   "#%u %s", pingCounter, nodeIdentity.getNodeName());

    LOG(TAG_PING " -> %02X #%u\n\r", targetHash, pingCounter);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_PING " TX ok\n\r");
    } else {
        LOG(TAG_PING " TX fail\n\r");
    }
    startReceive();
}

void sendDirectedTrace(uint8_t targetHash) {
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    uint8_t myHash = nodeIdentity.getNodeHash();
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Payload: [destHash][srcHash]['D']['T'][text: "#N name"]
    pingCounter++;
    pkt.payload[0] = targetHash;
    pkt.payload[1] = myHash;
    pkt.payload[2] = 'D';
    pkt.payload[3] = 'T';
    pkt.payloadLen = 4 + snprintf((char*)&pkt.payload[4], MC_MAX_PAYLOAD_SIZE - 4,
                                   "#%u %s", pingCounter, nodeIdentity.getNodeName());

    LOG(TAG_PING " ~> %02X #%u\n\r", targetHash, pingCounter);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_PING " TX ok\n\r");
    } else {
        LOG(TAG_PING " TX fail\n\r");
    }
    startReceive();
}

//=============================================================================
// ADVERT Beacon
//=============================================================================

void sendAdvert(bool flood) {
    if (!nodeIdentity.isInitialized()) {
        LOG(TAG_ERROR " No ID\n\r");
        return;
    }

    MCPacket pkt;
    bool success;

    if (flood) {
        success = advertGen.buildFlood(&pkt);
    } else {
        success = advertGen.buildZeroHop(&pkt);
    }

    if (!success) {
        LOG(TAG_ERROR " ADV build fail\n\r");
        return;
    }

    LOG(TAG_ADVERT " %s %s\n\r",
        flood ? "flood" : "local",
        nodeIdentity.getNodeName());

    // Add to packet cache so we don't re-forward our own ADVERT
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_ADVERT " TX ok\n\r");
        advertGen.markSent();
        advTxCount++;
    } else {
        LOG(TAG_ADVERT " TX fail\n\r");
    }

    startReceive();
}

//=============================================================================
// Direct Message - Send encrypted message to a contact
//=============================================================================
#ifndef LITE_MODE
/**
 * Send an encrypted direct message to a contact
 * Uses MeshCore TXT_MSG format: [dest_hash][src_hash][MAC+encrypted(timestamp+type+text)]
 */
void sendDirectMessage(const char* recipientName, const char* message) {
    if (!nodeIdentity.isInitialized()) {
        LOG(TAG_ERROR " No ID\n\r");
        return;
    }

    if (!timeSync.isSynchronized()) {
        LOG(TAG_ERROR " No time sync\n\r");
        return;
    }

    // Find contact by name
    Contact* contact = contactMgr.findByName(recipientName);
    if (!contact) {
        LOG(TAG_ERROR " Contact '%s' ?\n\r", recipientName);
        contactMgr.printContacts();
        return;
    }

    LOG(TAG_INFO " Msg to %s\n\r", contact->name);

    // Get or calculate shared secret
    const uint8_t* sharedSecret = contactMgr.getSharedSecret(contact);
    if (!sharedSecret) {
        LOG(TAG_ERROR " ECDH fail\n\r");
        return;
    }

    // Build plaintext: [timestamp 4B][type+attempt 1B][message]
    uint8_t plaintext[MC_MAX_MSG_PLAINTEXT];
    uint16_t plaintextLen = 0;

    // Timestamp (little-endian)
    uint32_t timestamp = timeSync.getTimestamp();
    plaintext[plaintextLen++] = timestamp & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 8) & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 16) & 0xFF;
    plaintext[plaintextLen++] = (timestamp >> 24) & 0xFF;

    // Type + attempt byte: upper 6 bits = type (0 = plain), lower 2 bits = attempt (0)
    plaintext[plaintextLen++] = (TXT_TYPE_PLAIN << 2) | 0;

    // Message text (null-terminated)
    uint16_t msgLen = strlen(message);
    if (msgLen > MC_MAX_MSG_PLAINTEXT - plaintextLen - 1) {
        msgLen = MC_MAX_MSG_PLAINTEXT - plaintextLen - 1;
    }
    memcpy(&plaintext[plaintextLen], message, msgLen);
    plaintextLen += msgLen;
    plaintext[plaintextLen++] = '\0';  // Null terminator

    // Encrypt with MAC
    uint8_t encrypted[MC_MAX_MSG_ENCRYPTED];
    uint16_t encryptedLen = msgCrypto.encryptThenMAC(sharedSecret, encrypted,
                                                       plaintext, plaintextLen);
    if (encryptedLen == 0) {
        LOG(TAG_ERROR " Encrypt fail\n\r");
        return;
    }

    // Build packet
    MCPacket pkt;
    pkt.clear();

    // Header: FLOOD + TXT_MSG type
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);
    pkt.pathLen = 0;

    // Payload: [dest_hash 1B][src_hash 1B][MAC+ciphertext]
    uint16_t pos = 0;
    pkt.payload[pos++] = contact->getHash();                    // Destination hash
    pkt.payload[pos++] = nodeIdentity.getPublicKey()[0];        // Source hash

    // Copy MAC + ciphertext
    memcpy(&pkt.payload[pos], encrypted, encryptedLen);
    pos += encryptedLen;

    pkt.payloadLen = pos;

    // Transmit
    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);

    if (transmitPacket(&pkt)) {
        LOG(TAG_OK " Sent to %s\n\r", contact->name);
        txCount++;
    } else {
        LOG(TAG_ERROR " TX fail\n\r");
    }

    startReceive();
}
#endif // LITE_MODE

//=============================================================================
// Encrypted messages to admin (node alerts)
//=============================================================================

/**
 * Send encrypted text message to a destination public key
 * Used by node alerts
 */
bool sendEncryptedToAdmin(const uint8_t* destPubKey, const char* text, uint16_t textLen) {
    if (!isPubKeySet(destPubKey) || !timeSync.isSynchronized()) return false;

    // Calculate shared secret
    uint8_t sharedSecret[MC_SHARED_SECRET_SIZE];
    if (!MeshCrypto::calcSharedSecret(sharedSecret, nodeIdentity.getPrivateKey(), destPubKey))
        return false;

    // Build plaintext: [timestamp:4][txt_type|attempt:1][message]
    uint8_t plaintext[104];
    uint32_t timestamp = timeSync.getTimestamp();
    plaintext[0] = timestamp & 0xFF;
    plaintext[1] = (timestamp >> 8) & 0xFF;
    plaintext[2] = (timestamp >> 16) & 0xFF;
    plaintext[3] = (timestamp >> 24) & 0xFF;
    plaintext[4] = (TXT_TYPE_PLAIN << 2) | 0;
    memcpy(&plaintext[5], text, textLen);
    uint16_t plaintextLen = 5 + textLen;

    // Encrypt
    uint8_t encrypted[120];
    uint16_t encLen = meshCrypto.encryptThenMAC(encrypted, plaintext, plaintextLen,
                                                 sharedSecret, sharedSecret);
    memset(sharedSecret, 0, sizeof(sharedSecret));
    memset(plaintext, 0, sizeof(plaintext));
    if (encLen == 0) return false;

    // Build packet
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);
    pkt.pathLen = 0;
    pkt.payload[0] = destPubKey[0];
    pkt.payload[1] = nodeIdentity.getNodeHash();
    memcpy(&pkt.payload[2], encrypted, encLen);
    pkt.payloadLen = 2 + encLen;

    LOG(TAG_INFO " Msg to %02X %dB\n\r", destPubKey[0], pkt.payloadLen);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);
    txQueue.add(&pkt);
    txCount++;
    return true;
}


/**
 * Send node alert when new node/repeater is discovered
 * @param nodeName Name of the discovered node
 * @param nodeHash Hash of the discovered node
 * @param nodeType Type (1=CHAT, 2=REPEATER, etc)
 * @param rssi Signal strength
 * @return true if alert sent
 */
bool sendNodeAlert(const char* nodeName, uint8_t nodeHash, uint8_t nodeType, int16_t rssi) {
    if (!alertEnabled) return false;

    char message[40];
    const char* typeStr = nodeType == 1 ? "CHAT" : nodeType == 2 ? "RPT" : "NODE";
    snprintf(message, sizeof(message), "NEW %s: %s [%02X] %ddBm",
             typeStr, nodeName[0] ? nodeName : "?", nodeHash, rssi);

    return sendEncryptedToAdmin(alertDestPubKey, message, strlen(message));
}

//=============================================================================
// Mesh Health Monitor
//=============================================================================

/**
 * Send alert by temporarily becoming a chat node.
 * MeshCore app ignores messages from repeaters, so we:
 * 1. Switch flags to CHAT_NODE, send ADVERT (app registers us as contact)
 * 2. Send the encrypted message (app shows it)
 * 3. Switch back to REPEATER at next scheduled ADVERT
 */
bool sendAlertAsChatNode(const char* text) {
    if (!isPubKeySet(alertDestPubKey) || !timeSync.isSynchronized()) return false;

    // Save original flags and switch to chat node
    uint8_t origFlags = nodeIdentity.getFlags();
    nodeIdentity.setFlags((origFlags & 0xF0) | MC_TYPE_CHAT_NODE);

    // Send ADVERT as chat node so app registers us
    sendAdvert(true);

    // Send the actual message
    bool ok = sendEncryptedToAdmin(alertDestPubKey, text, strlen(text));

    // Restore repeater flags (next scheduled ADVERT will broadcast as repeater)
    nodeIdentity.setFlags(origFlags);

    LOG(TAG_INFO " Alert%s: %s\n\r", ok ? "" : " FAIL", text);
    return ok;
}

void healthCheck() {
    if (!alertEnabled || !isPubKeySet(alertDestPubKey)) return;

    uint32_t now = millis();
    for (uint8_t i = 0; i < seenNodes.getCount(); i++) {
        const SeenNode* n = seenNodes.getNode(i);
        if (!n || n->lastSeen == 0 || n->pktCount < 3) continue;

        if ((now - n->lastSeen) > HEALTH_OFFLINE_MS && !n->offlineAlerted) {
            ((SeenNode*)n)->offlineAlerted = true;
            char msg[40];
            snprintf(msg, sizeof(msg), "%02X %s off %lum",
                n->hash, n->name[0] ? n->name : "?", (now - n->lastSeen) / 60000);
            sendAlertAsChatNode(msg);
        }
    }
}

void checkAdvertBeacon() {
    // Check for pending ADVERT after time sync
    // Use signed subtraction to handle millis() wraparound correctly
    if (pendingAdvertTime > 0 && (int32_t)(millis() - pendingAdvertTime) >= 0) {
        pendingAdvertTime = 0;  // Clear pending
        LOG(TAG_ADVERT " Sched ADV post-sync\n\r");
        sendAdvert(true);
        lastFloodAdvertTime = millis();
        return;  // Don't send another one immediately
    }

    if (!timeSync.isSynchronized()) return;

    // Separate flood interval mode
    if (floodAdvertIntervalMs > 0) {
        // Local ADVERT on normal interval
        if (advertGen.shouldSend()) {
            sendAdvert(false);  // local only
        }
        // Flood ADVERT on longer interval
        uint32_t now = millis();
        if (lastFloodAdvertTime == 0 || (now - lastFloodAdvertTime) >= floodAdvertIntervalMs) {
            lastFloodAdvertTime = now;
            sendAdvert(true);  // flood
        }
    } else {
        // Default: all ADVERTs are flood
        if (advertGen.shouldSend()) {
            sendAdvert(true);
        }
    }
}

