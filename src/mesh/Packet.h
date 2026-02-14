#pragma once
#include <Arduino.h>

/**
 * MeshCore Packet Structure
 * Based on MeshCore protocol specification
 * https://github.com/meshcore-dev/MeshCore
 */

// Maximum sizes
#define MC_MAX_PACKET_SIZE      255
#define MC_MAX_PATH_SIZE        64
#define MC_MAX_PAYLOAD_SIZE     180

// Header bit masks and shifts
#define MC_HEADER_ROUTE_MASK    0x03
#define MC_HEADER_ROUTE_SHIFT   0
#define MC_HEADER_TYPE_MASK     0x0F
#define MC_HEADER_TYPE_SHIFT    2
#define MC_HEADER_VER_MASK      0x03
#define MC_HEADER_VER_SHIFT     6

// Route types (2 bits)
#define MC_ROUTE_TRANSPORT_FLOOD    0x00  // Flood with transport codes
#define MC_ROUTE_FLOOD              0x01  // Flood, path built up
#define MC_ROUTE_DIRECT             0x02  // Direct with supplied path
#define MC_ROUTE_TRANSPORT_DIRECT   0x03  // Direct with transport codes

// Payload types (4 bits)
#define MC_PAYLOAD_REQUEST          0x00  // Encrypted request
#define MC_PAYLOAD_RESPONSE         0x01  // Response to request
#define MC_PAYLOAD_PLAIN            0x02  // Plain text message
#define MC_PAYLOAD_ACK              0x03  // Acknowledgment
#define MC_PAYLOAD_ADVERT           0x04  // Node advertisement
#define MC_PAYLOAD_GROUP_TEXT       0x05  // Group text message
#define MC_PAYLOAD_GROUP_DATA       0x06  // Group datagram
#define MC_PAYLOAD_ANON_REQ         0x07  // Anonymous request
#define MC_PAYLOAD_PATH_RETURN      0x08  // Returned path
#define MC_PAYLOAD_PATH_TRACE       0x09  // Path trace with SNR
#define MC_PAYLOAD_MULTIPART        0x0A  // Multipart packet
#define MC_PAYLOAD_CONTROL          0x0B  // Control/discovery
#define MC_PAYLOAD_RAW              0x0F  // Raw custom packet

// Payload versions
#define MC_PAYLOAD_VER_1            0x00  // 1-byte hashes, 2-byte MAC

// Text message types (upper 6 bits of type+attempt byte)
#define TXT_TYPE_PLAIN              0x00  // Plain text message
#define TXT_TYPE_CLI                0x01  // CLI command

// Request types (inside encrypted REQUEST payload)
#define REQ_TYPE_GET_STATUS         0x01  // Get node status/stats
#define REQ_TYPE_KEEP_ALIVE         0x02  // Keep connection alive
#define REQ_TYPE_GET_TELEMETRY      0x03  // Get telemetry data
#define REQ_TYPE_GET_MINMAXAVG      0x04  // Get radio min/max/avg stats
#define REQ_TYPE_GET_ACCESS_LIST    0x05  // Get ACL entries (admin only)
#define REQ_TYPE_GET_NEIGHBOURS     0x06  // Get neighbour list
#define REQ_TYPE_SEND_CLI           0x07  // Send CLI command (admin only)
#define REQ_TYPE_RESET_PATH         0x08  // Reset contact path

// Control packet types (upper nibble of first byte)
#define CTL_TYPE_DISCOVER_REQ       0x80  // Node discovery request
#define CTL_TYPE_DISCOVER_RESP      0x81  // Node discovery response

/**
 * MeshCore Packet Header
 * Single byte encoding route type, payload type, and version
 */
struct MCPacketHeader {
    uint8_t raw;

    // Get route type (bits 0-1)
    inline uint8_t getRouteType() const {
        return (raw >> MC_HEADER_ROUTE_SHIFT) & MC_HEADER_ROUTE_MASK;
    }

    // Get payload type (bits 2-5)
    inline uint8_t getPayloadType() const {
        return (raw >> MC_HEADER_TYPE_SHIFT) & MC_HEADER_TYPE_MASK;
    }

    // Get payload version (bits 6-7)
    inline uint8_t getVersion() const {
        return (raw >> MC_HEADER_VER_SHIFT) & MC_HEADER_VER_MASK;
    }

    // Set header fields
    inline void set(uint8_t route, uint8_t type, uint8_t ver) {
        raw = ((route & MC_HEADER_ROUTE_MASK) << MC_HEADER_ROUTE_SHIFT) |
              ((type & MC_HEADER_TYPE_MASK) << MC_HEADER_TYPE_SHIFT) |
              ((ver & MC_HEADER_VER_MASK) << MC_HEADER_VER_SHIFT);
    }

    // Check if this is a flood packet
    inline bool isFlood() const {
        uint8_t rt = getRouteType();
        return (rt == MC_ROUTE_FLOOD || rt == MC_ROUTE_TRANSPORT_FLOOD);
    }

    // Check if this is a direct routed packet
    inline bool isDirect() const {
        uint8_t rt = getRouteType();
        return (rt == MC_ROUTE_DIRECT || rt == MC_ROUTE_TRANSPORT_DIRECT);
    }
};

/**
 * MeshCore Packet
 * Complete packet structure for sending/receiving
 */
struct MCPacket {
    MCPacketHeader header;
    uint8_t pathLen;
    uint8_t payloadLen;
    uint8_t path[MC_MAX_PATH_SIZE];
    uint8_t payload[MC_MAX_PAYLOAD_SIZE];

    // Metadata (not transmitted)
    uint32_t rxTime;
    int8_t snr;         // SNR * 4 for 0.25dB resolution
    int16_t rssi;

    // Calculate total packet size for TX
    // MeshCore format: [header 1B][pathLen 1B][path N B][payload M B]
    // NOTE: payloadLen is NOT transmitted - it's derived from total length
    inline uint16_t getTotalSize() const {
        return 1 + 1 + pathLen + payloadLen;  // header + pathLen_byte + path + payload
    }

    // Serialize packet to buffer for transmission
    // MeshCore wire format: [header][pathLen][path...][payload...]
    // IMPORTANT: payloadLen is NOT in the wire format - receiver calculates it
    uint16_t serialize(uint8_t* buf, uint16_t maxLen) const {
        if (getTotalSize() > maxLen) return 0;

        uint16_t pos = 0;
        buf[pos++] = header.raw;
        buf[pos++] = pathLen;
        // NOTE: payloadLen is NOT transmitted in MeshCore protocol

        if (pathLen > 0) {
            memcpy(&buf[pos], path, pathLen);
            pos += pathLen;
        }

        if (payloadLen > 0) {
            memcpy(&buf[pos], payload, payloadLen);
            pos += payloadLen;
        }

        return pos;
    }

    // Deserialize packet from received buffer
    // MeshCore wire format: [header][pathLen][path...][payload...]
    // payloadLen is calculated from total length minus header/path
    bool deserialize(const uint8_t* buf, uint16_t len) {
        if (len < 2) return false;  // Minimum: header + pathLen

        uint16_t pos = 0;
        header.raw = buf[pos++];
        pathLen = buf[pos++];

        // Validate path length
        if (pathLen > MC_MAX_PATH_SIZE) return false;

        // Check if we have enough data for path
        if (pos + pathLen > len) return false;

        if (pathLen > 0) {
            memcpy(path, &buf[pos], pathLen);
            pos += pathLen;
        }

        // Payload is everything remaining after path
        // This is how MeshCore determines payload length
        uint16_t availablePayload = len - pos;
        if (availablePayload > MC_MAX_PAYLOAD_SIZE) {
            availablePayload = MC_MAX_PAYLOAD_SIZE;
        }
        payloadLen = availablePayload;

        if (payloadLen > 0) {
            memcpy(payload, &buf[pos], payloadLen);
            pos += payloadLen;
        }

        return true;
    }

    // Clear packet
    void clear() {
        header.raw = 0;
        pathLen = 0;
        payloadLen = 0;
        rxTime = 0;
        snr = 0;
        rssi = 0;
    }
};

