#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "../mesh/Packet.h"

/**
 * Store-and-Forward Mailbox
 * Persistent: 2 slots in EEPROM (172 bytes at offset 340)
 * Overflow: 4 slots in RAM (volatile, lost on reboot)
 * Re-delivers when destination node comes back online
 */

#define MAILBOX_EEPROM_OFFSET   340
#define MAILBOX_MAGIC           0xBB0F
#define MAILBOX_VERSION         1
#define MAILBOX_SLOTS           2       // EEPROM persistent slots
#define MAILBOX_RAM_SLOTS       4       // RAM overflow slots
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
    uint8_t count;              // Number of occupied EEPROM slots
    uint8_t reserved[4];
};

class Mailbox {
private:
    MailboxHeader header;
    MailboxSlot slots[MAILBOX_SLOTS];       // EEPROM-backed (persistent)
    MailboxSlot ramSlots[MAILBOX_RAM_SLOTS]; // RAM-only (volatile)

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

    // Store into a specific slot array. Returns true if stored.
    bool storeInArray(MailboxSlot* arr, uint8_t arrSize, uint8_t destHash,
                      const uint8_t* buf, uint8_t len, uint32_t unixTime) {
        // Find empty slot
        for (uint8_t i = 0; i < arrSize; i++) {
            if (arr[i].pktLen == 0) {
                arr[i].destHash = destHash;
                arr[i].timestamp = unixTime;
                arr[i].pktLen = len;
                memcpy(arr[i].pktData, buf, len);
                return true;
            }
        }
        // No empty - overwrite oldest
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < arrSize; i++) {
            if (arr[i].timestamp < arr[oldest].timestamp) oldest = i;
        }
        arr[oldest].destHash = destHash;
        arr[oldest].timestamp = unixTime;
        arr[oldest].pktLen = len;
        memcpy(arr[oldest].pktData, buf, len);
        return true;
    }

public:
    void load() {
        // Clear RAM slots
        memset(ramSlots, 0, sizeof(ramSlots));

        EEPROM.get(MAILBOX_EEPROM_OFFSET, header);
        if (header.magic != MAILBOX_MAGIC || header.version != MAILBOX_VERSION) {
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
    // Priority: EEPROM first (persistent), then RAM overflow (volatile).
    bool store(uint8_t destHash, MCPacket* pkt, uint32_t unixTime) {
        uint8_t buf[MAILBOX_PKT_MAX];
        uint16_t len = pkt->serialize(buf, MAILBOX_PKT_MAX);
        if (len == 0 || len > MAILBOX_PKT_MAX) return false;

        // Try EEPROM slots first (persistent across reboot)
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

        // EEPROM full - overflow to RAM
        return storeInArray(ramSlots, MAILBOX_RAM_SLOTS, destHash, buf, len, unixTime);
    }

    // Count messages for a given dest hash (EEPROM + RAM).
    uint8_t countFor(uint8_t destHash) const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            if (slots[i].pktLen > 0 && slots[i].destHash == destHash) n++;
        }
        for (uint8_t i = 0; i < MAILBOX_RAM_SLOTS; i++) {
            if (ramSlots[i].pktLen > 0 && ramSlots[i].destHash == destHash) n++;
        }
        return n;
    }

    // Retrieve and remove one message for destHash. EEPROM first, then RAM.
    bool popFor(uint8_t destHash, MCPacket* outPkt) {
        // Check EEPROM slots first
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            if (slots[i].pktLen > 0 && slots[i].destHash == destHash) {
                if (outPkt->deserialize(slots[i].pktData, slots[i].pktLen)) {
                    slots[i].pktLen = 0;
                    if (header.count > 0) header.count--;
                    writeSlot(i);
                    return true;
                }
            }
        }
        // Check RAM slots
        for (uint8_t i = 0; i < MAILBOX_RAM_SLOTS; i++) {
            if (ramSlots[i].pktLen > 0 && ramSlots[i].destHash == destHash) {
                if (outPkt->deserialize(ramSlots[i].pktData, ramSlots[i].pktLen)) {
                    ramSlots[i].pktLen = 0;
                    return true;
                }
            }
        }
        return false;
    }

    // Expire messages older than TTL (EEPROM + RAM).
    void expireOld(uint32_t currentUnixTime) {
        for (uint8_t i = 0; i < MAILBOX_SLOTS; i++) {
            if (slots[i].pktLen > 0 && currentUnixTime > slots[i].timestamp &&
                (currentUnixTime - slots[i].timestamp) > MAILBOX_TTL_SEC) {
                slots[i].pktLen = 0;
                if (header.count > 0) header.count--;
                writeSlot(i);
            }
        }
        for (uint8_t i = 0; i < MAILBOX_RAM_SLOTS; i++) {
            if (ramSlots[i].pktLen > 0 && currentUnixTime > ramSlots[i].timestamp &&
                (currentUnixTime - ramSlots[i].timestamp) > MAILBOX_TTL_SEC) {
                ramSlots[i].pktLen = 0;
            }
        }
    }

    // Clear all slots (EEPROM + RAM)
    void clear() {
        memset(slots, 0, sizeof(slots));
        memset(ramSlots, 0, sizeof(ramSlots));
        header.count = 0;
        writeToEeprom();
    }

    // Total occupied count (EEPROM + RAM)
    uint8_t getCount() const {
        uint8_t n = header.count;
        for (uint8_t i = 0; i < MAILBOX_RAM_SLOTS; i++) {
            if (ramSlots[i].pktLen > 0) n++;
        }
        return n;
    }

    uint8_t getTotalSlots() const { return MAILBOX_SLOTS + MAILBOX_RAM_SLOTS; }
    uint8_t getEepromCount() const { return header.count; }
    uint8_t getRamCount() const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < MAILBOX_RAM_SLOTS; i++) {
            if (ramSlots[i].pktLen > 0) n++;
        }
        return n;
    }

    const MailboxSlot* getSlot(uint8_t idx) const {
        if (idx < MAILBOX_SLOTS) return &slots[idx];
        idx -= MAILBOX_SLOTS;
        if (idx < MAILBOX_RAM_SLOTS) return &ramSlots[idx];
        return nullptr;
    }

    // Is a given slot index in EEPROM (persistent)?
    bool isEepromSlot(uint8_t idx) const { return idx < MAILBOX_SLOTS; }
};
