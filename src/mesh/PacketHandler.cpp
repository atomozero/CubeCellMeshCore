#include "../main.h"
#include "../core/Led.h"
#include "../core/Config.h"

// Replies to directed ping / trace requests (used only by packet processing)
static void sendPong(uint8_t targetHash, MCPacket* rxPkt) {
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    uint8_t myHash = nodeIdentity.getNodeHash();
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Payload: [destHash][srcHash]['P']['O'][text: "name rssi"]
    pkt.payload[0] = targetHash;
    pkt.payload[1] = myHash;
    pkt.payload[2] = 'P';
    pkt.payload[3] = 'O';
    pkt.payloadLen = 4 + snprintf((char*)&pkt.payload[4], MC_MAX_PAYLOAD_SIZE - 4,
                                   "%s %d", nodeIdentity.getNodeName(), rxPkt->rssi);

    LOG(TAG_PING " PONG -> %02X\n\r", targetHash);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);
    txQueue.add(&pkt);
    txCount++;
}

static void sendTraceResponse(uint8_t targetHash, MCPacket* rxPkt) {
    MCPacket pkt;
    pkt.clear();
    pkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);

    uint8_t myHash = nodeIdentity.getNodeHash();
    pkt.path[0] = myHash;
    pkt.pathLen = 1;

    // Payload: [destHash][srcHash]['T']['R'][text: "name rssi hops"]
    pkt.payload[0] = targetHash;
    pkt.payload[1] = myHash;
    pkt.payload[2] = 'T';
    pkt.payload[3] = 'R';
    pkt.payloadLen = 4 + snprintf((char*)&pkt.payload[4], MC_MAX_PAYLOAD_SIZE - 4,
                                   "%s %d %d", nodeIdentity.getNodeName(), rxPkt->rssi, rxPkt->pathHashCount());

    LOG(TAG_PING " TR -> %02X\n\r", targetHash);

    uint32_t id = getPacketId(&pkt);
    packetCache.addIfNew(id);
    txQueue.add(&pkt);
    txCount++;
}


//=============================================================================
// Packet Processing
//=============================================================================

// Packet ID for duplicate detection, same inputs as MeshCore's
// Packet::calculatePacketHash(): payload type + whole payload (+ path_len for
// TRACE). Path and route type are excluded so the same packet relayed by
// different repeaters is recognised as a duplicate. 32-bit FNV-1a.
uint32_t getPacketId(MCPacket* pkt) {
    uint32_t hash = 2166136261UL;
    uint8_t type = pkt->header.getPayloadType();
    hash = (hash ^ type) * 16777619UL;
    if (type == MC_PAYLOAD_PATH_TRACE) {
        hash = (hash ^ pkt->pathLen) * 16777619UL;
    }
    for (uint8_t i = 0; i < pkt->payloadLen; i++) {
        hash = (hash ^ pkt->payload[i]) * 16777619UL;
    }
    return hash;
}

// True if 'hash' (n bytes) is a prefix of our public key (MeshCore isHashMatch)
static bool isSelfHash(const uint8_t* hash, uint8_t n) {
    return memcmp(hash, nodeIdentity.getPublicKey(), n) == 0;
}

bool shouldForward(MCPacket* pkt) {
    bool isFlood = pkt->header.isFlood();
    bool isDirect = pkt->header.isDirect();

    if (!isFlood && !isDirect) {
        return false;
    }

    // Repeating disabled by 'set repeat off'
    if (!repeaterHelper.isRepeatEnabled()) {
        return false;
    }

    // TRACE packets are routed by their payload, handled in processReceivedPacket()
    if (pkt->header.getPayloadType() == MC_PAYLOAD_PATH_TRACE) {
        return false;
    }

    // RSSI threshold: don't forward packets with very weak signal
    if (pkt->rssi < MC_MIN_RSSI_FORWARD) {
        return false;
    }

    // Region filter for flood packets (MeshCore 1.10.0+)
    if (isFlood) {
        if (pkt->hasTransportCodes()) {
            // TRANSPORT_FLOOD: check region map for matching transport code
            if (regionMap.getCount() > 0) {
                if (!regionMap.findMatch(pkt, REGION_DENY_FLOOD)) {
                    return false;
                }
            }
        } else {
            // Legacy FLOOD: check wildcard entry
            if (regionMap.getWildcard().flags & REGION_DENY_FLOOD) {
                return false;
            }
        }
    }

    // DIRECT routing: check if we are the next hop (first path entry == our hash)
    if (isDirect) {
        if (pkt->pathHashCount() == 0) return false;
        if (!isSelfHash(pkt->path, pkt->pathHashSize())) return false;
    }

    // Don't forward packets specifically addressed to us
    uint8_t payloadType = pkt->header.getPayloadType();
    if (payloadType == MC_PAYLOAD_ANON_REQ ||
        payloadType == MC_PAYLOAD_REQUEST ||
        payloadType == MC_PAYLOAD_RESPONSE) {
        if (pkt->payloadLen > 0 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            return false;
        }
    }

    // Check packet ID cache (dedup)
    uint32_t id = getPacketId(pkt);
    if (!packetCache.addIfNew(id)) {
        return false;
    }

    // FLOOD: check path length and loop detection
    if (isFlood) {
        uint8_t hashSize = pkt->pathHashSize();
        uint8_t hops = pkt->pathHashCount();

        // Room to append our own hash? (MeshCore: (n + 1) * size <= MAX_PATH_SIZE)
        if ((uint16_t)(hops + 1) * hashSize > MC_MAX_PATH_SIZE || hops + 1 > 63) {
            return false;
        }

        // 'flood.max' limits ADVERT floods only (like MeshCore flood_max_advert,
        // default 8). Other floods are limited only by the path space above.
        if (pkt->header.getPayloadType() == MC_PAYLOAD_ADVERT &&
            hops >= repeaterHelper.getMaxFloodHops()) {
            return false;
        }

        // Loop detection with configurable strictness
        if (loopDetectMode != LOOP_DETECT_OFF) {
            uint8_t occurrences = 0;

            // Count how many times we appear in the path
            for (uint8_t i = 0; i < hops; i++) {
                if (isSelfHash(&pkt->path[i * hashSize], hashSize)) {
                    occurrences++;
                }
            }

            // Threshold per mode and hash size (same tables as MeshCore
            // simple_repeater): longer hashes give fewer false matches
            static const uint8_t maxLoop[3][3] = {
                { 4, 2, 1 },   // minimal:  1-, 2-, 3-byte hashes
                { 2, 1, 1 },   // moderate
                { 1, 1, 1 },   // strict
            };
            uint8_t mode = loopDetectMode;
            if (mode < LOOP_DETECT_MINIMAL || mode > LOOP_DETECT_STRICT) mode = LOOP_DETECT_STRICT;
            uint8_t maxOccurrences = maxLoop[mode - LOOP_DETECT_MINIMAL][hashSize - 1];

            if (occurrences >= maxOccurrences) {
                return false;  // Loop detected
            }
        }
    }

    return true;
}

//=============================================================================
// Node Discovery (CONTROL packets)
//=============================================================================

/**
 * Process CONTROL packet (node discovery)
 *
 * CONTROL packet format (DISCOVER_REQ):
 * [0]    flags - upper nibble: 0x8 (discover), lower bit: prefix_only
 * [1]    type_filter - bit for each ADV_TYPE_* (bit 1 = repeater)
 * [2-5]  tag - random sender value (4 bytes, little-endian)
 * [6-9]  since - optional epoch timestamp (4 bytes, little-endian)
 *
 * @param pkt Received CONTROL packet
 * @return true if discovery response sent
 */
bool processDiscoverRequest(MCPacket* pkt) {
    if (pkt->payloadLen < 6) {
        return false;  // Too short
    }

    uint8_t flags = pkt->payload[0];
    uint8_t typeFilter = pkt->payload[1];

    // Check if this is a discover request (upper nibble = 0x8)
    if ((flags & 0xF0) != CTL_TYPE_DISCOVER_REQ) {
        return false;  // Not a discover request
    }

    // Check if repeater type is requested (bit 1 = repeater)
    // ADV_TYPE: 0=unknown, 1=client, 2=repeater, 3=room
    if (!(typeFilter & (1 << MC_TYPE_REPEATER))) {
        return false;  // Repeater not requested
    }

    // Extract request tag (for correlation)
    uint32_t requestTag = pkt->payload[2] |
                          (pkt->payload[3] << 8) |
                          (pkt->payload[4] << 16) |
                          (pkt->payload[5] << 24);

    // Check rate limiting
    if (!repeaterHelper.canRespondToDiscover()) {
        LOG(TAG_DISCOVERY " Rate limited\n\r");
        return false;
    }

    LOG(TAG_DISCOVERY " REQ %02X\n\r", typeFilter);

    // Build discover response
    MCPacket respPkt;
    respPkt.clear();

    // Use same route type for response (usually FLOOD)
    respPkt.header.set(pkt->header.getRouteType(), MC_PAYLOAD_CONTROL, MC_PAYLOAD_VER_1);

    // Copy incoming path for response (if any)
    respPkt.pathLen = pkt->pathLen;
    if (pkt->pathByteLen() > 0) {
        memcpy(respPkt.path, pkt->path, pkt->pathByteLen());
    }

    // Build response payload
    // [0]    flags - upper nibble: 0x8 (response), lower: 0x1
    // [1]    node type (MC_TYPE_REPEATER)
    // [2]    inbound SNR
    // [3-6]  request tag (correlation)
    // [7-14] public key prefix (8 bytes)
    uint16_t pos = 0;
    respPkt.payload[pos++] = CTL_TYPE_DISCOVER_RESP;     // Response flag
    respPkt.payload[pos++] = MC_TYPE_REPEATER;           // We are a repeater
    respPkt.payload[pos++] = (uint8_t)pkt->snr;          // Inbound SNR

    // Request tag for correlation
    respPkt.payload[pos++] = requestTag & 0xFF;
    respPkt.payload[pos++] = (requestTag >> 8) & 0xFF;
    respPkt.payload[pos++] = (requestTag >> 16) & 0xFF;
    respPkt.payload[pos++] = (requestTag >> 24) & 0xFF;

    // Public key prefix (first 8 bytes)
    memcpy(&respPkt.payload[pos], nodeIdentity.getPublicKey(), 8);
    pos += 8;

    respPkt.payloadLen = pos;

    // Add random delay to spread responses from multiple repeaters
    uint32_t airtime = calculatePacketAirtime(respPkt.getTotalSize());
    uint8_t score = calcSnrScore(pkt->snr);
    uint32_t rxDel = calcRxDelay(score, airtime);
    uint32_t randomDelay = rxDel + calcTxJitter(airtime);

    LOG(TAG_DISCOVERY " RESP %lums\n\r", randomDelay);

    // Queue with delay
    delay(randomDelay);
    txQueue.add(&respPkt);

    return true;
}

//=============================================================================
// TXT_MSG CLI Command handling (MC_PAYLOAD_PLAIN with TXT_TYPE_CLI)
//=============================================================================

/**
 * Process encrypted TXT_MSG containing CLI command
 *
 * TXT_MSG packet format (MC_PAYLOAD_PLAIN):
 * [0]     dest_hash (1 byte)
 * [1]     src_hash (1 byte)
 * [2-3]   MAC (2 bytes)
 * [4+]    ciphertext (variable)
 *
 * Decrypted plaintext:
 * [0-3]   timestamp (4 bytes)
 * [4]     txt_type | attempt (upper 6 bits = type, lower 2 = attempt)
 * [5+]    text/command (null-terminated string)
 *
 * @param pkt Received TXT_MSG packet
 * @return true if command processed and response sent
 */
bool processTxtMsgCLI(MCPacket* pkt) {
    if (pkt->payloadLen < 10) {
        LOG(TAG_AUTH " TXT short %d\n\r", pkt->payloadLen);
        return false;
    }

    uint8_t destHash = pkt->payload[0];
    uint8_t srcHash = pkt->payload[1];

    // Check if addressed to us
    if (destHash != nodeIdentity.getNodeHash()) {
        return false;  // Not for us
    }

    LOG(TAG_AUTH " TXT %02X\n\r", srcHash);

    // Find client session by src_hash
    ClientSession* session = nullptr;
    for (uint8_t i = 0; i < MAX_CLIENT_SESSIONS; i++) {
        const ClientSession* s = sessionManager.getSession(i);
        if (s && s->active && s->pubKey[0] == srcHash) {
            session = const_cast<ClientSession*>(s);
            break;
        }
    }

    if (!session) {
        LOG(TAG_AUTH " No session for %02X\n\r", srcHash);
        return false;
    }

    // Decrypt using session's shared secret
    const uint8_t* encrypted = &pkt->payload[2];
    uint16_t encryptedLen = pkt->payloadLen - 2;

    uint8_t decrypted[128];
    uint16_t decryptedLen = meshCrypto.MACThenDecrypt(
        decrypted, sizeof(decrypted), encrypted, encryptedLen,
        session->sharedSecret, session->sharedSecret);

    if (decryptedLen == 0) {
        LOG(TAG_AUTH " TXT decrypt fail\n\r");
        return false;
    }

    // Extract timestamp and check replay
    uint32_t timestamp = decrypted[0] |
                         (decrypted[1] << 8) |
                         (decrypted[2] << 16) |
                         (decrypted[3] << 24);

    if (timestamp <= session->lastTimestamp) {
        LOG(TAG_AUTH " TXT replay\n\r");
        return false;
    }
    session->lastTimestamp = timestamp;
    session->lastActivity = millis();

    // Extract txt_type from byte[4]
    uint8_t txtTypeByte = decrypted[4];
    uint8_t txtType = (txtTypeByte >> 2) & 0x3F;  // Upper 6 bits

    LOG(TAG_AUTH " TXT t=%d l=%d\n\r", txtType, decryptedLen);

    // Only process CLI commands (TXT_TYPE_CLI = 0x01)
    if (txtType != TXT_TYPE_CLI) {
        LOG(TAG_AUTH " Not CLI\n\r");
        return false;
    }

    // Only admin can execute CLI commands
    if (session->permissions != PERM_ACL_ADMIN) {
        LOG(TAG_AUTH " Need admin\n\r");
        return false;
    }

    // Extract command string (starts at byte 5)
    char cmdStr[40];
    uint16_t cmdLen = decryptedLen - 5;
    if (cmdLen > sizeof(cmdStr) - 1) cmdLen = sizeof(cmdStr) - 1;
    memcpy(cmdStr, &decrypted[5], cmdLen);
    cmdStr[cmdLen] = '\0';

    // Trim trailing whitespace/newlines
    while (cmdLen > 0 && (cmdStr[cmdLen-1] == '\n' || cmdStr[cmdLen-1] == '\r' ||
           cmdStr[cmdLen-1] == ' ' || cmdStr[cmdLen-1] == '\0')) {
        cmdStr[--cmdLen] = '\0';
    }

    LOG(TAG_AUTH " CLI: %s\n\r", cmdStr);

    // Process command
    char cliResponse[96];
    uint16_t cliLen = processRemoteCommand(cmdStr, cliResponse, sizeof(cliResponse), true);

    // Build response: [timestamp:4][txt_type:1][response_text]
    uint8_t responseData[128];
    uint16_t responseLen = 0;

    // Echo back the request timestamp (for correlation)
    responseData[responseLen++] = timestamp & 0xFF;
    responseData[responseLen++] = (timestamp >> 8) & 0xFF;
    responseData[responseLen++] = (timestamp >> 16) & 0xFF;
    responseData[responseLen++] = (timestamp >> 24) & 0xFF;

    // Response type: TXT_TYPE_CLI (0x01) in upper 6 bits
    responseData[responseLen++] = (TXT_TYPE_CLI << 2) | 0;

    // Append CLI response text
    if (cliLen > 0 && responseLen + cliLen < sizeof(responseData)) {
        memcpy(&responseData[responseLen], cliResponse, cliLen);
        responseLen += cliLen;
    }

    // Encrypt response
    uint8_t encryptedResponse[160];
    uint16_t encLen = meshCrypto.encryptResponse(
        encryptedResponse, responseData, responseLen, session->sharedSecret);

    if (encLen == 0) {
        return false;
    }

    // Build response packet (TXT_MSG / MC_PAYLOAD_PLAIN)
    MCPacket respPkt;
    respPkt.clear();
    respPkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1);
    respPkt.pathLen = 0;

    // Payload: [dest_hash:1][src_hash:1][encrypted]
    respPkt.payload[0] = srcHash;                        // Destination (client)
    respPkt.payload[1] = nodeIdentity.getNodeHash();     // Source (us)
    memcpy(&respPkt.payload[2], encryptedResponse, encLen);
    respPkt.payloadLen = 2 + encLen;

    // Queue for transmission
    txQueue.add(&respPkt);

    // Handle reboot command
    if (strcmp(cmdStr, "reboot") == 0) {
        pendingReboot = true;
        rebootTime = millis() + 500;
    }

    LOG(TAG_AUTH " Resp %dB\n\r", respPkt.payloadLen);
    return true;
}

//=============================================================================
// Authenticated REQUEST handling
//=============================================================================

/**
 * Process authenticated REQUEST packet
 *
 * REQUEST packet format:
 * [0]     dest_hash (1 byte)
 * [1]     src_hash (1 byte)
 * [2-3]   MAC (2 bytes)
 * [4+]    ciphertext (variable)
 *
 * Decrypted plaintext:
 * [0-3]   timestamp (4 bytes)
 * [4]     request_type
 * [5+]    request_data (type-specific)
 *
 * @param pkt Received REQUEST packet
 * @return true if request processed and response sent
 */
bool processAuthenticatedRequest(MCPacket* pkt) {
    if (pkt->payloadLen < 20) {
        return false;
    }

    uint8_t destHash = pkt->payload[0];
    uint8_t srcHash = pkt->payload[1];

    // Check if addressed to us
    if (destHash != nodeIdentity.getNodeHash()) {
        return false;  // Not for us
    }

    // Find client session by src_hash prefix
    ClientSession* session = nullptr;
    for (uint8_t i = 0; i < MAX_CLIENT_SESSIONS; i++) {
        const ClientSession* s = sessionManager.getSession(i);
        if (s && s->active && s->pubKey[0] == srcHash) {
            session = const_cast<ClientSession*>(s);
            break;
        }
    }

    if (!session) {
        return false;
    }

    // Decrypt request using session's shared secret
    // Encrypted part starts at offset 2: [MAC:2][ciphertext]
    const uint8_t* encrypted = &pkt->payload[2];
    uint16_t encryptedLen = pkt->payloadLen - 2;

    uint8_t decrypted[128];
    uint16_t decryptedLen = meshCrypto.MACThenDecrypt(
        decrypted, sizeof(decrypted), encrypted, encryptedLen,
        session->sharedSecret, session->sharedSecret);

    if (decryptedLen == 0) {
        return false;
    }

    // Extract timestamp and check replay
    uint32_t timestamp = decrypted[0] |
                         (decrypted[1] << 8) |
                         (decrypted[2] << 16) |
                         (decrypted[3] << 24);

    if (timestamp <= session->lastTimestamp) {
        return false;  // Replay attack
    }
    session->lastTimestamp = timestamp;
    session->lastActivity = millis();

    // Extract request type
    uint8_t reqType = decrypted[4];

    // Handle request based on type
    uint8_t responseData[128];
    uint16_t responseLen = 0;

    // All responses start with sender's timestamp (used as tag for matching)
    // MeshCore reflects the request timestamp back, not server time
    responseData[0] = timestamp & 0xFF;
    responseData[1] = (timestamp >> 8) & 0xFF;
    responseData[2] = (timestamp >> 16) & 0xFF;
    responseData[3] = (timestamp >> 24) & 0xFF;
    responseLen = 4;

    switch (reqType) {
        case REQ_TYPE_GET_STATUS:
            responseLen += repeaterHelper.serializeRepeaterStats(
                &responseData[responseLen],
                telemetry.getBatteryMv(),
                txQueue.getCount(),
                lastRssi, lastSnr);
            break;

        case REQ_TYPE_GET_TELEMETRY:
            {
                telemetry.update();
                CayenneLPP lpp(&responseData[responseLen], sizeof(responseData) - responseLen);
                // Battery voltage (channel 1) - integer mV
                lpp.addVoltageMv(1, telemetry.getBatteryMv());
                // Temperature (channel 2) - integer Celsius
                lpp.addTemperatureInt(2, telemetry.getTemperature());
                // Node count as analog input (channel 3) - integer
                lpp.addAnalogInputInt(3, (int16_t)seenNodes.getCount());
                // Uptime as analog input in hours (channel 4) - integer
                lpp.addAnalogInputInt(4, (int16_t)(telemetry.getUptime() / 3600));
                responseLen += lpp.getSize();
            }
            break;

        case REQ_TYPE_GET_NEIGHBOURS:
            {
                NeighbourTracker& neighbours = repeaterHelper.getNeighbours();
                uint8_t count = neighbours.getCount();

                // neighbours_count (2 bytes)
                responseData[responseLen++] = count & 0xFF;
                responseData[responseLen++] = (count >> 8) & 0xFF;

                // results_count (2 bytes) - same as count for now
                responseData[responseLen++] = count & 0xFF;
                responseData[responseLen++] = (count >> 8) & 0xFF;

                // Serialize neighbours (6-byte prefix + 1 SNR + 1 RSSI per entry)
                responseLen += neighbours.serialize(
                    &responseData[responseLen],
                    sizeof(responseData) - responseLen,
                    0, 6);  // offset=0, prefix=6 bytes
            }
            break;

        case REQ_TYPE_GET_MINMAXAVG:
            responseLen += repeaterHelper.serializeRadioStats(&responseData[responseLen]);
            break;

        case REQ_TYPE_GET_ACCESS_LIST:
            if (session->permissions != PERM_ACL_ADMIN) {
                return false;
            }
            {
                ACLManager& acl = repeaterHelper.getACL();
                uint8_t count = acl.getCount();
                responseData[responseLen++] = count;
            }
            break;

        case REQ_TYPE_KEEP_ALIVE:
            break;

        case REQ_TYPE_SEND_CLI:
            if (session->permissions != PERM_ACL_ADMIN) {
                return false;
            }
            {
                char cmdStr[40];
                uint16_t cmdLen = decryptedLen - 5;
                if (cmdLen > sizeof(cmdStr) - 1) cmdLen = sizeof(cmdStr) - 1;
                memcpy(cmdStr, &decrypted[5], cmdLen);
                cmdStr[cmdLen] = '\0';

                while (cmdLen > 0 && (cmdStr[cmdLen-1] == '\n' || cmdStr[cmdLen-1] == '\r' ||
                       cmdStr[cmdLen-1] == ' ' || cmdStr[cmdLen-1] == '\0')) {
                    cmdStr[--cmdLen] = '\0';
                }

                // Process command and get response
                char cliResponse[96];  // Reduced from 128 to save stack
                bool isAdmin = (session->permissions == PERM_ACL_ADMIN);
                uint16_t cliLen = processRemoteCommand(cmdStr, cliResponse, sizeof(cliResponse), isAdmin);

                // Append CLI response to responseData (after timestamp)
                if (cliLen > 0 && responseLen + cliLen < sizeof(responseData)) {
                    memcpy(&responseData[responseLen], cliResponse, cliLen);
                    responseLen += cliLen;
                }

                // Handle reboot command specially
                if (strcmp(cmdStr, "reboot") == 0) {
                    // Schedule reboot after sending response
                    pendingReboot = true;
                    rebootTime = millis() + 500;  // Reboot in 500ms
                }
            }
            break;

        default:
            return false;
    }

    // Encrypt response
    uint8_t encryptedResponse[160];
    uint16_t encLen = meshCrypto.encryptResponse(
        encryptedResponse, responseData, responseLen, session->sharedSecret);

    if (encLen == 0) {
        return false;
    }

    // Build response packet
    MCPacket respPkt;
    respPkt.clear();

    // Use FLOOD route for response (client may not have direct path)
    respPkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_RESPONSE, MC_PAYLOAD_VER_1);
    respPkt.pathLen = 0;

    // Payload: [dest_hash:1][src_hash:1][encrypted]
    respPkt.payload[0] = srcHash;                        // Destination (client)
    respPkt.payload[1] = nodeIdentity.getNodeHash();     // Source (us)
    memcpy(&respPkt.payload[2], encryptedResponse, encLen);
    respPkt.payloadLen = 2 + encLen;

    // Queue for transmission
    txQueue.add(&respPkt);

    return true;
}

//=============================================================================
// Authentication - ANON_REQ / LOGIN handling
//=============================================================================

/**
 * Send encrypted LOGIN response to client
 *
 * @param clientPubKey Client's public key (32 bytes)
 * @param sharedSecret Pre-computed shared secret with client
 * @param isAdmin true if admin login successful
 * @param permissions Permission byte
 * @param outPath Return path to client
 * @param outPathLen Return path length
 * @return true if response sent successfully
 */
bool sendLoginResponse(const uint8_t* clientPubKey, const uint8_t* sharedSecret,
                       bool isAdmin, uint8_t permissions,
                       const uint8_t* outPath, uint8_t outPathLen) {
    // Build response data
    uint8_t responseData[16];
    // Use unique timestamp (current + 1 to ensure it's always newer)
    uint32_t responseTs = timeSync.getTimestamp() + 1;
    uint8_t responseLen = MeshCrypto::buildLoginOKResponse(
        responseData,
        responseTs,
        isAdmin,
        permissions,
        60,  // Keep-alive interval (ignored)
        2    // Firmware version: 2.x
    );

    // Encrypt response
    uint8_t encryptedResponse[64];
    uint16_t encLen = meshCrypto.encryptResponse(
        encryptedResponse, responseData, responseLen, sharedSecret);

    if (encLen == 0) {
        return false;
    }

    // Build response packet
    MCPacket respPkt;
    respPkt.clear();

    // Use FLOOD route for response (client may not have direct path)
    // MeshCore always uses sendFlood for login responses
    respPkt.header.set(MC_ROUTE_FLOOD, MC_PAYLOAD_RESPONSE, MC_PAYLOAD_VER_1);
    respPkt.pathLen = 0;  // Flood has no path

    // Payload: [dest_hash:1][src_hash:1][MAC:2][ciphertext]
    respPkt.payload[0] = clientPubKey[0];  // Destination hash
    respPkt.payload[1] = nodeIdentity.getNodeHash();  // Source hash (our hash)
    memcpy(&respPkt.payload[2], encryptedResponse, encLen);
    respPkt.payloadLen = 2 + encLen;

    // Queue for transmission
    txQueue.add(&respPkt);

    return true;
}

/**
 * Process ANON_REQ (anonymous login request)
 *
 * ANON_REQ payload format (MeshCore V1):
 * [0]      dest_hash (1 byte) - first byte of destination pubkey (already verified)
 * [1-32]   ephemeral_pubkey (32 bytes) - sender's temp key for ECDH
 * [33-34]  MAC (2 bytes) - HMAC-SHA256 truncated
 * [35+]    ciphertext (variable) - encrypted [timestamp:4][password:N]
 *
 * @param pkt Received ANON_REQ packet
 * @return true if login successful
 */
bool processAnonRequest(MCPacket* pkt) {
    // Minimum size: 1 (dest_hash) + 32 (ephemeral) + 2 (MAC) + 16 (min ciphertext)
    if (pkt->payloadLen < 51) {
        LOG(TAG_AUTH " ANON short %d\n\r", pkt->payloadLen);
        return false;
    }

    // Extract components (dest_hash at [0] already verified by caller)
    const uint8_t* ephemeralPub = &pkt->payload[1];     // Ephemeral pubkey at [1-32]

    // Decrypt the request - pass from sender pubkey onwards
    // ANON_REQ format: [destHash:1][sender_pubkey:32][MAC:2][ciphertext]
    // decryptAnonReq expects: [sender_pubkey:32][MAC:2][ciphertext]
    // Note: byte[1] (7E) is first byte of sender pubkey, NOT srcHash!
    uint32_t timestamp;
    char password[32];

    // Skip only destHash[0], sender pubkey starts at [1]
    uint8_t pwdLen = meshCrypto.decryptAnonReq(
        &timestamp, password, sizeof(password) - 1,
        &pkt->payload[1],     // From sender pubkey onwards (skip only destHash)
        pkt->payloadLen - 1,  // Length excluding destHash
        nodeIdentity.getPrivateKey()
    );

    if (pwdLen == 0) {
        return false;
    }

    // Refuse remote admin login with the factory password: anyone knows it.
    // It must be changed from the serial console first ('password admin <pw>').
    if (strcmp(sessionManager.getAdminPassword(), MC_DEFAULT_ADMIN_PASSWORD) == 0 &&
        strcmp(password, MC_DEFAULT_ADMIN_PASSWORD) == 0) {
        memset(password, 0, sizeof(password));
        statsRecordLoginFail();
        LOG(TAG_AUTH " Login refused: default admin password\n\r");
        return false;
    }

    // Process login through session manager
    uint8_t permissions = sessionManager.processLogin(
        ephemeralPub,  // Use ephemeral key as client identifier
        password,
        timestamp,
        nodeIdentity.getPrivateKey(),
        pkt->path,
        pkt->pathLen
    );

    // Clear password from memory
    memset(password, 0, sizeof(password));

    if (permissions == 0) {
        statsRecordLoginFail();  // Persistent stats
        LOG(TAG_AUTH " Login FAILED\n\r");
        return false;
    }

    statsRecordLogin();  // Persistent stats
    bool isAdmin = (permissions == PERM_ACL_ADMIN);
    LOG(TAG_AUTH " Login OK (%s)\n\r", isAdmin ? "admin" : "guest");

    // Capture admin public key for daily report
    if (isAdmin) {
        // Check if this is a new admin key (different from current)
        bool isNewKey = (memcmp(reportDestPubKey, ephemeralPub, REPORT_PUBKEY_SIZE) != 0);
        if (isNewKey || !isPubKeySet(reportDestPubKey)) {
            memcpy(reportDestPubKey, ephemeralPub, REPORT_PUBKEY_SIZE);
            saveConfig();
        }
    }

    // Get shared secret from session for response encryption
    ClientSession* session = sessionManager.findSession(ephemeralPub);
    if (!session) {
        return false;
    }

    // Send encrypted response
    return sendLoginResponse(
        ephemeralPub,
        session->sharedSecret,
        isAdmin,
        permissions,
        pkt->path,
        pkt->pathLen
    );
}

void processReceivedPacket(MCPacket* pkt) {
    rxCount++;
    statsRecordRx();  // Persistent stats

    // Update repeater statistics
    bool isFlood = pkt->header.isFlood();
    repeaterHelper.recordRx(isFlood);
    repeaterHelper.updateRadioStats(pkt->rssi, pkt->snr);


    LOG(TAG_RX " r%d t%d p=%d l=%d %d/%d.%d\n\r",
        pkt->header.getRouteType(), pkt->header.getPayloadType(),
        pkt->pathLen, pkt->payloadLen,
        pkt->rssi, pkt->snr / 4, abs(pkt->snr % 4) * 25);

    // Handle ANON_REQ (login request)
    if (pkt->header.getPayloadType() == MC_PAYLOAD_ANON_REQ) {
        if (pkt->payloadLen >= 51 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Rate limit login attempts
            if (!repeaterHelper.allowLogin()) {
                statsRecordRateLimited();  // Persistent stats
                LOG(TAG_AUTH " Login lim\n\r");
            } else {
                processAnonRequest(pkt);
            }
        }
    }
    // Handle REQUEST (authenticated request)
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_REQUEST) {
        if (pkt->payloadLen >= 20 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Rate limit requests
            if (!repeaterHelper.allowRequest()) {
                statsRecordRateLimited();  // Persistent stats
                LOG(TAG_AUTH " Req lim\n\r");
            } else {
                processAuthenticatedRequest(pkt);
            }
        }
    }
    // Handle MC_PAYLOAD_PLAIN: directed ping/pong or TXT_MSG CLI
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_PLAIN) {
        if (pkt->payloadLen >= 4 && pkt->payload[2] == 'D' && pkt->payload[3] == 'P'
            && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Directed PING for us - respond with PONG
            LOG(TAG_PING " from %02X %s\n\r", pkt->payload[1],
                pkt->payloadLen > 4 ? (char*)&pkt->payload[4] : "");
            sendPong(pkt->payload[1], pkt);
        }
        else if (pkt->payloadLen >= 4 && pkt->payload[2] == 'P' && pkt->payload[3] == 'O'
                 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // PONG response for us
            LOG(TAG_PING " PONG %02X %s rssi=%d snr=%d.%ddB p=%d\n\r",
                pkt->payload[1],
                pkt->payloadLen > 4 ? (char*)&pkt->payload[4] : "",
                pkt->rssi, pkt->snr / 4, abs(pkt->snr % 4) * 25,
                pkt->pathLen);
        }
        else if (pkt->payloadLen >= 4 && pkt->payload[2] == 'D' && pkt->payload[3] == 'T'
                 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Directed TRACE for us - respond with trace response
            LOG(TAG_PING " TRACE from %02X %s\n\r", pkt->payload[1],
                pkt->payloadLen > 4 ? (char*)&pkt->payload[4] : "");
            sendTraceResponse(pkt->payload[1], pkt);
        }
        else if (pkt->payloadLen >= 4 && pkt->payload[2] == 'T' && pkt->payload[3] == 'R'
                 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // Trace response for us
            LOG(TAG_PING " TRACE %02X %s rssi=%d snr=%d.%ddB p=%d\n\r",
                pkt->payload[1],
                pkt->payloadLen > 4 ? (char*)&pkt->payload[4] : "",
                pkt->rssi, pkt->snr / 4, abs(pkt->snr % 4) * 25,
                pkt->pathLen);
        }
        else if (pkt->payloadLen >= 10 && pkt->payload[0] == nodeIdentity.getNodeHash()) {
            // TXT_MSG CLI
            if (!repeaterHelper.allowRequest()) {
                statsRecordRateLimited();
                LOG(TAG_AUTH " TXT lim\n\r");
            } else {
                processTxtMsgCLI(pkt);
            }
        }
    }
    // Handle CONTROL (node discovery, etc.)
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_CONTROL) {
        if (pkt->payloadLen >= 6) {
            processDiscoverRequest(pkt);
        }
    }
    // Handle TRACE (MeshCore): DIRECT packet whose payload is
    // [tag:4][auth:4][flags:1][hop hashes...]; flags & 3 = log2(hash size).
    // path[] holds one SNR byte per hop already traversed. Only the node
    // matching the next hop hash appends its SNR and forwards, once.
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_PATH_TRACE) {
        if (pkt->header.isDirect() && pkt->payloadLen >= 9 &&
            pkt->pathLen < MC_MAX_PATH_SIZE && repeaterHelper.isRepeatEnabled()) {
            uint8_t pathSz = pkt->payload[8] & 0x03;
            uint8_t hashLen = 1 << pathSz;
            uint16_t offset = (uint16_t)pkt->pathLen << pathSz;
            uint16_t hopsLen = pkt->payloadLen - 9;
            if (offset + hashLen <= hopsLen &&
                isSelfHash(&pkt->payload[9 + offset], hashLen) &&
                packetCache.addIfNew(getPacketId(pkt))) {
                pkt->path[pkt->pathLen++] = (int8_t)(pkt->snr);
                txQueue.add(pkt);
                fwdCount++;
                statsRecordFwd();
                LOG(TAG_FWD " Trace hop %d\n\r", pkt->pathLen);
            }
        }
    }
    // Our own ADVERT repeated back by a neighbour: don't learn ourselves as a
    // node/contact and don't take time sync from our own clock.
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_ADVERT &&
             pkt->payloadLen >= ADVERT_PUBKEY_OFFSET + 32 &&
             isSelfHash(&pkt->payload[ADVERT_PUBKEY_OFFSET], 32)) {
        LOG(TAG_INFO " Own ADV echo\n\r");
    }
    // Parse and display ADVERT info
    else if (pkt->header.getPayloadType() == MC_PAYLOAD_ADVERT) {
        advRxCount++;
        // Try to sync time from ADVERT timestamp
        // First ADVERT: sync immediately. Already synced: need 2 matching different times to re-sync
        uint32_t advertTime = AdvertGenerator::extractTimestamp(pkt->payload, pkt->payloadLen);

        #ifdef DEBUG_VERBOSE
        // Debug: show raw timestamp bytes from received ADVERT
        Serial.printf("[RX-ADV] Raw ts bytes[32-35]: %02X %02X %02X %02X -> unix=%lu\n\r",
                      pkt->payload[32], pkt->payload[33], pkt->payload[34], pkt->payload[35], advertTime);

        // Debug: show appdata (starts at byte 100)
        Serial.printf("[RX-ADV] Appdata[100+]: ");
        for (int i = 100; i < pkt->payloadLen && i < 116; i++) {
            Serial.printf("%02X ", pkt->payload[i]);
        }
        Serial.printf(" (len=%d)\n\r", pkt->payloadLen - 100);
        #endif

        if (advertTime > 0) {
            uint8_t syncResult = timeSync.syncFromAdvert(advertTime);

            if (syncResult == 1) {
                // First sync - use immediately
                ledBlueDoubleBlink();  // Signal time sync acquired
                LOG(TAG_OK " Time sync %lu\n\r", timeSync.getTimestamp());
                statsSetFirstBootTime(timeSync.getTimestamp());  // Persistent stats
                // Schedule our own ADVERT after sync
                pendingAdvertTime = millis() + ADVERT_AFTER_SYNC_MS;
                LOG(TAG_INFO " ADV in %ds\n\r", ADVERT_AFTER_SYNC_MS / 1000);
            } else if (syncResult == 2) {
                // Re-sync via consensus (2 different sources agreed)
                ledBlueDoubleBlink();  // Signal time re-sync
                LOG(TAG_OK " Time resync %lu\n\r", timeSync.getTimestamp());
                // Schedule new ADVERT with updated time
                pendingAdvertTime = millis() + ADVERT_AFTER_SYNC_MS;
                LOG(TAG_INFO " ADV in %ds\n\r", ADVERT_AFTER_SYNC_MS / 1000);
            } else if (timeSync.hasPendingSync()) {
                // Received different time, stored as pending - waiting for confirmation
                LOG(TAG_INFO " Time drift %lu pending\n\r", advertTime);
            }
        }

        AdvertInfo advInfo;
        if (AdvertGenerator::parseAdvert(pkt->payload, pkt->payloadLen, &advInfo)) {
            // Show node info
            LOG(TAG_NODE " %s", advInfo.name);
            if (advInfo.isRepeater) LOG_RAW(" R");
            if (advInfo.isChatNode) LOG_RAW(" C");
            LOG_RAW(" %02X", advInfo.pubKeyHash);
            if (advInfo.hasLocation) {
                int32_t lat = advInfo.latitude, lon = advInfo.longitude;
                LOG_RAW(" %ld.%04ld,%ld.%04ld", lat/1000000, abs(lat%1000000)/100, lon/1000000, abs(lon%1000000)/100);
            }
            LOG_RAW("\n\r");

            // Update seen nodes with pubkey hash and name from ADVERT
            bool isNew = seenNodes.update(advInfo.pubKeyHash, pkt->rssi, pkt->snr, advInfo.name);
            if (isNew) {
                statsRecordUniqueNode();  // Persistent stats
                LOG(TAG_NODE " New node\n\r");
                // Send alert for new node
                uint8_t nodeType = advInfo.isChatNode ? 1 : advInfo.isRepeater ? 2 : 0;
                sendNodeAlert(advInfo.name, advInfo.pubKeyHash, nodeType, pkt->rssi);
            }

            // Add to contact manager (stores full public key for messaging)
            // Filter by hop count if autoAddMaxHops is set (0 = no limit)
            const uint8_t* pubKey = &pkt->payload[ADVERT_PUBKEY_OFFSET];
            if (autoAddMaxHops == 0 || pkt->pathHashCount() <= autoAddMaxHops) {
                contactMgr.updateFromAdvert(pubKey, advInfo.name, pkt->rssi, pkt->snr);
            } else {
                LOG(TAG_INFO " Skip contact (hops=%d > max=%d)\n\r", pkt->pathHashCount(), autoAddMaxHops);
            }

            // Store-and-forward: deliver pending messages for this node
            if (mailbox.countFor(advInfo.pubKeyHash) > 0) {
                MCPacket fwdPkt;
                while (mailbox.popFor(advInfo.pubKeyHash, &fwdPkt)) {
                    txQueue.add(&fwdPkt);
                    LOG(TAG_INFO " Mbox fwd %02X\n\r", advInfo.pubKeyHash);
                }
            }

            // If this is a repeater AND received directly (0-hop), add to neighbours list
            // Only 0-hop ADVERTs indicate direct neighbours (no relay)
            if (advInfo.isRepeater && pkt->pathHashCount() == 0 && pkt->payloadLen >= 32) {
                bool newNeighbour = repeaterHelper.getNeighbours().update(
                    pkt->payload,  // First 32 bytes are pubkey
                    pkt->snr, pkt->rssi);
                if (newNeighbour) {
                    LOG(TAG_NODE " Nbr: %s\n\r", advInfo.name);
                }
            }
        }
    }
    // Track nodes - either from path or from payload hash for direct packets
    else if (pkt->pathHashCount() > 0 && pkt->header.getPayloadType() != MC_PAYLOAD_PATH_TRACE) {
        // First byte is originator (first byte of its pubkey, any hash size)
        bool isNew = seenNodes.update(pkt->path[0], pkt->rssi, pkt->snr);
        if (isNew) {
            statsRecordUniqueNode();  // Persistent stats
            LOG(TAG_NODE " New %02X\n\r", pkt->path[0]);
        }
        // If path has multiple hops, also track the last hop (direct neighbor)
        if (pkt->pathHashCount() > 1) {
            uint8_t lastHop = pkt->path[(pkt->pathHashCount() - 1) * pkt->pathHashSize()];
            if (lastHop != pkt->path[0]) {
                seenNodes.update(lastHop, pkt->rssi, pkt->snr);
            }
        }
    } else if (pkt->payloadLen >= 6) {
        // Path=0: Direct packet from nearby node
        // Generate hash from first 6 bytes of payload (usually contains sender ID)
        uint8_t hash = pkt->payload[0];
        for (uint8_t i = 1; i < 6; i++) {
            hash ^= pkt->payload[i];
        }
        // Mark as direct with 0x80 prefix to distinguish from path hashes
        hash = (hash & 0x7F) | 0x80;
        bool isNew = seenNodes.update(hash, pkt->rssi, pkt->snr);
        if (isNew) {
            statsRecordUniqueNode();  // Persistent stats
            LOG(TAG_NODE " New %02X\n\r", hash);
        }
    }

    // Store-and-forward: save packets for offline nodes
    {
        uint8_t pt = pkt->header.getPayloadType();
        // Only for packet types that have dest_hash at payload[0]
        if (pkt->payloadLen >= 2 &&
            (pt == MC_PAYLOAD_REQUEST || pt == MC_PAYLOAD_RESPONSE ||
             pt == MC_PAYLOAD_PLAIN || pt == MC_PAYLOAD_ANON_REQ)) {
            uint8_t destHash = pkt->payload[0];
            // Not for us (already handled), not broadcast (hash 0)
            if (destHash != nodeIdentity.getNodeHash() && destHash != 0) {
                // Check if dest node is known but offline
                for (uint8_t i = 0; i < seenNodes.getCount(); i++) {
                    const SeenNode* sn = seenNodes.getNode(i);
                    if (sn && sn->hash == destHash && sn->pktCount >= 2 &&
                        (millis() - sn->lastSeen) > HEALTH_OFFLINE_MS) {
                        // Node is offline - store if we have time
                        if (timeSync.isSynchronized()) {
                            if (mailbox.store(destHash, pkt, timeSync.getTimestamp())) {
                                LOG(TAG_INFO " Mbox store %02X\n\r", destHash);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // Check if we should forward
    if (shouldForward(pkt)) {
        // Rate limit forwarding
        if (!repeaterHelper.allowForward()) {
            statsRecordRateLimited();  // Persistent stats
            LOG(TAG_FWD " Rate lim\n\r");
        } else {
            if (pkt->header.isDirect()) {
                uint8_t hashSize = pkt->pathHashSize();
                uint8_t hops = pkt->pathHashCount();
                // Circuit breaker: check next hop before peel (neighbours keyed by 1st byte)
                if (hops >= 2) {
                    uint8_t nextHop = pkt->path[hashSize];
                    if (repeaterHelper.getNeighbours().isCircuitOpen(nextHop)) {
                        LOG(TAG_FWD " CB %02X\n\r", nextHop);
                        goto skipForward;
                    }
                }
                // DIRECT routing: remove ourselves (first entry) from the path
                pkt->setPathHashCount(hops - 1);
                memmove(pkt->path, &pkt->path[hashSize], (hops - 1) * hashSize);
                LOG(TAG_FWD " Direct p=%d\n\r", pkt->pathHashCount());
            } else {
                // FLOOD routing: append our hash using the originator's hash size
                uint8_t hashSize = pkt->pathHashSize();
                uint8_t hops = pkt->pathHashCount();
                memcpy(&pkt->path[hops * hashSize], nodeIdentity.getPublicKey(), hashSize);
                pkt->setPathHashCount(hops + 1);
            }

            // Add to TX queue
            txQueue.add(pkt);
            fwdCount++;
            statsRecordFwd();  // Persistent stats
            LOG(TAG_FWD " Q p=%d\n\r", pkt->pathHashCount());
        }
        skipForward:;
    }
}

