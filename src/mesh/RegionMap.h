#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include <SHA256.h>
#include "Packet.h"

#define REGION_MAX_ENTRIES    4
#define REGION_NAME_LEN       16
#define REGION_KEY_LEN        16
#define REGION_DENY_FLOOD     0x01

// EEPROM persistence
#define REGION_EEPROM_OFFSET  524       // After mailbox (340 + 8 + 2*88); was 512 (overlapped mailbox)
#define REGION_EEPROM_MAGIC   0x5247    // "RG"
#define REGION_SAVE_NAME_LEN  12        // Compact name in EEPROM (enough for "au-nsw" etc.)
#define REGION_EEPROM_BYTES   (2 + 1 + 1 + REGION_MAX_ENTRIES * (REGION_SAVE_NAME_LEN + 1))  // 57

struct RegionEntry {
    char name[REGION_NAME_LEN];
    uint8_t flags;
    uint8_t key[REGION_KEY_LEN];    // SHA256(name) truncated to 16 bytes
};

class RegionMap {
private:
    uint8_t _count;
    RegionEntry _entries[REGION_MAX_ENTRIES];
    RegionEntry _wildcard;    // Always present, name = "*"

    void deriveKey(const char* name, uint8_t* key) {
        SHA256 sha;
        sha.reset();
        sha.update(name, strlen(name));
        uint8_t hash[32];
        sha.finalize(hash, sizeof(hash));
        memcpy(key, hash, REGION_KEY_LEN);
    }

    // Compute expected transport code for a packet using region key
    uint16_t calcTransportCode(const uint8_t* key, const MCPacket* pkt) {
        SHA256 sha;
        sha.resetHMAC(key, REGION_KEY_LEN);
        uint8_t ptype = pkt->header.getPayloadType();
        sha.update(&ptype, 1);
        sha.update(pkt->payload, pkt->payloadLen);
        uint16_t code;
        sha.finalizeHMAC(key, REGION_KEY_LEN, &code, 2);
        if (code == 0x0000) code = 0x0001;
        if (code == 0xFFFF) code = 0xFFFE;
        return code;
    }

public:
    RegionMap() : _count(0) {
        memset(_entries, 0, sizeof(_entries));
        memset(&_wildcard, 0, sizeof(_wildcard));
        _wildcard.name[0] = '*';
        _wildcard.name[1] = '\0';
        _wildcard.flags = 0;    // Default: allow flood
    }

    RegionEntry& getWildcard() { return _wildcard; }
    uint8_t getCount() const { return _count; }
    const RegionEntry* getEntry(uint8_t i) const {
        return (i < _count) ? &_entries[i] : nullptr;
    }

    // Find region matching packet transport codes
    // Returns entry pointer (allow) or NULL (deny/no match)
    RegionEntry* findMatch(const MCPacket* pkt, uint8_t denyMask) {
        for (uint8_t i = 0; i < _count; i++) {
            uint16_t expected = calcTransportCode(_entries[i].key, pkt);
            if (expected == pkt->transport_codes[0]) {
                if (_entries[i].flags & denyMask) return nullptr;
                return &_entries[i];
            }
        }
        // No specific match — check wildcard
        if (_wildcard.flags & denyMask) return nullptr;
        return &_wildcard;
    }

    // Add or update a region entry
    bool put(const char* name) {
        if (!name || name[0] == '\0') return false;
        // Check if already exists
        for (uint8_t i = 0; i < _count; i++) {
            if (strncmp(_entries[i].name, name, REGION_NAME_LEN - 1) == 0) {
                return true;    // Already exists
            }
        }
        if (_count >= REGION_MAX_ENTRIES) return false;
        strncpy(_entries[_count].name, name, REGION_NAME_LEN - 1);
        _entries[_count].name[REGION_NAME_LEN - 1] = '\0';
        _entries[_count].flags = 0;
        deriveKey(name, _entries[_count].key);
        _count++;
        return true;
    }

    // Remove a region entry by name
    bool remove(const char* name) {
        for (uint8_t i = 0; i < _count; i++) {
            if (strncmp(_entries[i].name, name, REGION_NAME_LEN - 1) == 0) {
                // Shift remaining entries
                for (uint8_t j = i; j < _count - 1; j++) {
                    _entries[j] = _entries[j + 1];
                }
                _count--;
                memset(&_entries[_count], 0, sizeof(RegionEntry));
                return true;
            }
        }
        return false;
    }

    // Set/clear deny flood flag
    bool allowFlood(const char* name) {
        if (name[0] == '*') { _wildcard.flags &= ~REGION_DENY_FLOOD; return true; }
        for (uint8_t i = 0; i < _count; i++) {
            if (strncmp(_entries[i].name, name, REGION_NAME_LEN - 1) == 0) {
                _entries[i].flags &= ~REGION_DENY_FLOOD;
                return true;
            }
        }
        return false;
    }

    bool denyFlood(const char* name) {
        if (name[0] == '*') { _wildcard.flags |= REGION_DENY_FLOOD; return true; }
        for (uint8_t i = 0; i < _count; i++) {
            if (strncmp(_entries[i].name, name, REGION_NAME_LEN - 1) == 0) {
                _entries[i].flags |= REGION_DENY_FLOOD;
                return true;
            }
        }
        return false;
    }

    void clear() {
        _count = 0;
        memset(_entries, 0, sizeof(_entries));
        _wildcard.flags = 0;
    }

    // EEPROM persistence - compact format (no key cache, derived on load)
    // Layout: magic(2) + count(1) + wc_flags(1) + entries[4]*(name[12]+flags(1)) = 57 bytes
    bool save() {
        uint16_t offset = REGION_EEPROM_OFFSET;
        uint16_t magic = REGION_EEPROM_MAGIC;
        EEPROM.put(offset, magic);          offset += 2;
        EEPROM.put(offset, _count);         offset += 1;
        EEPROM.put(offset, _wildcard.flags); offset += 1;
        for (uint8_t i = 0; i < REGION_MAX_ENTRIES; i++) {
            // Write compact name (12 bytes)
            char compactName[REGION_SAVE_NAME_LEN];
            memset(compactName, 0, REGION_SAVE_NAME_LEN);
            if (i < _count) {
                strncpy(compactName, _entries[i].name, REGION_SAVE_NAME_LEN - 1);
            }
            for (uint8_t j = 0; j < REGION_SAVE_NAME_LEN; j++) {
                EEPROM.write(offset + j, compactName[j]);
            }
            offset += REGION_SAVE_NAME_LEN;
            EEPROM.write(offset, (i < _count) ? _entries[i].flags : 0);
            offset += 1;
        }
        return EEPROM.commit();
    }

    bool load() {
        uint16_t offset = REGION_EEPROM_OFFSET;
        uint16_t magic;
        EEPROM.get(offset, magic);          offset += 2;
        if (magic != REGION_EEPROM_MAGIC) return false;

        uint8_t count;
        EEPROM.get(offset, count);          offset += 1;
        uint8_t wcFlags;
        EEPROM.get(offset, wcFlags);        offset += 1;

        if (count > REGION_MAX_ENTRIES) return false;

        _wildcard.flags = wcFlags;
        _count = 0;

        for (uint8_t i = 0; i < REGION_MAX_ENTRIES; i++) {
            char compactName[REGION_SAVE_NAME_LEN];
            for (uint8_t j = 0; j < REGION_SAVE_NAME_LEN; j++) {
                compactName[j] = EEPROM.read(offset + j);
            }
            offset += REGION_SAVE_NAME_LEN;
            uint8_t flags = EEPROM.read(offset);
            offset += 1;

            if (i < count && compactName[0] != '\0') {
                strncpy(_entries[_count].name, compactName, REGION_NAME_LEN - 1);
                _entries[_count].name[REGION_NAME_LEN - 1] = '\0';
                _entries[_count].flags = flags;
                deriveKey(_entries[_count].name, _entries[_count].key);
                _count++;
            }
        }
        return true;
    }
};
