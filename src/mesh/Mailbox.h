#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "../mesh/Packet.h"

/**
 * Store-and-Forward Mailbox
 * Stores packets for offline nodes in EEPROM (172 bytes at offset 340)
 * Re-delivers when destination node comes back online
 */

#define MAILBOX_EEPROM_OFFSET   340
#define MAILBOX_MAGIC           0xBB0F
#define MAILBOX_VERSION         1
#define MAILBOX_SLOTS           2
#define MAILBOX_PKT_MAX         76      // Max serialized packet size per slot
#define MAILBOX_TTL_SEC         86400   // 24 hours

struct MailboxSlot {
    uint8_t destHash;           // Destination node hash (payload[0])
    uint32_t timestamp;         // Unix time when stored
    uint8_t pktLen;             // Serialized packet length (0 = empty)
    uint8_t pktData[MAILBOX_PKT_MAX]; // Raw serialized packet
};

struct MailboxHeader {
    uint16_t magic;
    uint8_t version;
    uint8_t count;              // Number of occupied slots
    uint8_t reserved[4];
};

class Mailbox {
private:
    MailboxHeader header;
    MailboxSlot slots[MAILBOX_SLOTS];

    void writeToEeprom() {
        EEPROM.put(MAILBOX_EEPROM_OFFSET, header);
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            EEPROM.put(MAILBOX_EEPROM_OFFSET + sizeof(MailboxHeader) + i * sizeof(MailboxSlot), slots[i]);
        }
        EEPROM.commit();
    }

    void writeSlot(uint8_t idx) {
        EEPROM.put(MAILBOX_EEPROM_OFFSET + sizeof(MailboxHeader) + idx * sizeof(MailboxSlot), slots[idx]);
        EEPROM.put(MAILBOX_EEPROM_OFFSET, header);
        EEPROM.commit();
    }

public:
    void load() {
        EEPROM.get(MAILBOX_EEPROM_OFFSET, header);
        if (header.magic != MAILBOX_MAGIC || header.version != MAILBOX_VERSION) {
            // Initialize fresh
            header.magic = MAILBOX_MAGIC;
            header.version = MAILBOX_VERSION;
            header.count = 0;
            memset(header.reserved, 0, sizeof(header.reserved));
            memset(slots, 0, sizeof(slots));
            writeToEeprom();
            return;
        }
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            EEPROM.get(MAILBOX_EEPROM_OFFSET + sizeof(MailboxHeader) + i * sizeof(MailboxSlot), slots[i]);
        }
    }

    // Store a packet for offline node. Returns true if stored.
    bool store(uint8_t destHash, MCPacket* pkt, uint32_t unixTime) {
        // Serialize packet
        uint8_t buf[MAILBOX_PKT_MAX];
        uint16_t len = pkt->serialize(buf, MAILBOX_PKT_MAX);
        if (len == 0 || len > MAILBOX_PKT_MAX) return false;

        // Find empty slot
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            if (slots[i].pktLen == 0) {
                slots[i].destHash = destHash;
                slots[i].timestamp = unixTime;
                slots[i].pktLen = len;
                memcpy(slots[i].pktData, buf, len);
                header.count++;
                writeSlot(i);
                return true;
            }
        }

        // No empty slot - overwrite oldest
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < MAILBOX_SLOTS; i++) {
            if (slots[i].timestamp < slots[oldest].timestamp) oldest = i;
        }
        slots[oldest].destHash = destHash;
        slots[oldest].timestamp = unixTime;
        slots[oldest].pktLen = len;
        memcpy(slots[oldest].pktData, buf, len);
        writeSlot(oldest);
        return true;
    }

    // Check if there are messages for a given dest hash. Returns count.
    uint8_t countFor(uint8_t destHash) const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            if (slots[i].pktLen > 0 && slots[i].destHash == destHash) n++;
        }
        return n;
    }

    // Retrieve and remove one message for destHash. Returns true if found.
    // Writes deserialized packet to outPkt.
    bool popFor(uint8_t destHash, MCPacket* outPkt) {
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            if (slots[i].pktLen > 0 && slots[i].destHash == destHash) {
                if (outPkt->deserialize(slots[i].pktData, slots[i].pktLen)) {
                    // Clear slot
                    slots[i].pktLen = 0;
                    if (header.count > 0) header.count--;
                    writeSlot(i);
                    return true;
                }
            }
        }
        return false;
    }

    // Expire messages older than TTL. Call periodically.
    void expireOld(uint32_t currentUnixTime) {
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            if (slots[i].pktLen > 0 && currentUnixTime > slots[i].timestamp) {
                if ((currentUnixTime - slots[i].timestamp) > MAILBOX_TTL_SEC) {
                    slots[i].pktLen = 0;
                    if (header.count > 0) header.count--;
                    writeSlot(i);
                }
            }
        }
    }

    // Clear all slots
    void clear() {
        memset(slots, 0, sizeof(slots));
        header.count = 0;
        writeToEeprom();
    }

    uint8_t getCount() const { return header.count; }
    uint8_t getSlots() const { return MAILBOX_SLOTS; }

    const MailboxSlot* getSlot(uint8_t idx) const {
        if (idx < MAILBOX_SLOTS) return &slots[idx];
        return nullptr;
    }
};
