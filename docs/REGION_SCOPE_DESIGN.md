# Region System - Design Document

**Version:** 0.8.0 (planned)
**MeshCore Compatibility:** 1.10.0+
**Status:** Design Phase - Not yet implemented
**Author:** Andrea Bernardi

---

## Overview

This document describes the planned implementation of the **Region** system for CubeCellMeshCore, aligned with the actual MeshCore protocol as found in the upstream source code.

---

## MeshCore Protocol Reference

### What Regions Are

Regions are a **deny-based flood filtering** mechanism. Each repeater maintains a **RegionMap** — a flat set of named regions, each with flags controlling whether flood packets with matching transport codes should be forwarded or dropped.

Key principles:
- **Default: forward everything** — with no region entries configured, all packets are forwarded
- **Deny-based** — `REGION_DENY_FLOOD` flag blocks forwarding for a specific region
- **Flat matching** — regions are matched by full name via cryptographic transport codes (no hierarchy)
- **Wildcard `*`** = fallback for unscoped packets; forwards only packets **without** a region scope (not all packets)
- **Applies only to channels** — does not affect direct messages (DMs)

### What Regions Are NOT

- NOT an allow-list of geographic codes
- NOT frequency band selection (that's `MC_REGION_EU868`/`US915`/`AU915`)
- NOT a simple string matching system

### Region Names

Region codes follow this convention:
- **Countries:** ISO 3166-1 alpha-2 (e.g., `nl`, `au`, `us`)
- **Subdivisions:** UNECE codes with hyphen (e.g., `nl-li`, `au-nsw`)
- **Valid chars:** alphanumeric, accented chars, `-`, `$`, `#`
- **Max length:** 31 characters
- **Matching:** full string match (or hash), no hierarchical inheritance

---

## Transport Codes (Wire Format)

### Packet Structure

Route types 0x00 (`ROUTE_TYPE_TRANSPORT_FLOOD`) and 0x03 (`ROUTE_TYPE_TRANSPORT_DIRECT`) carry transport codes. The wire format is:

```
[header 1B] [transport_codes 4B] [path_len 1B] [path...] [payload...]
```

**Transport codes are 4 bytes on the wire:** two `uint16_t` values written via `memcpy()`.

For legacy route types (0x01 `ROUTE_TYPE_FLOOD`, 0x02 `ROUTE_TYPE_DIRECT`), transport codes are absent and the wire format is:

```
[header 1B] [path_len 1B] [path...] [payload...]
```

### Transport Code Generation (Cryptographic)

Transport codes are **NOT** plain region names. They are derived cryptographically:

1. **Region key:** `SHA256(region_name)` → 16-byte AES key
2. **Transport code:** `HMAC-SHA256(region_key, payload_type_byte + payload)` → truncated to **2 bytes (uint16_t)**
3. **Two codes per packet:** `transport_codes[0]` and `transport_codes[1]` (the second appears to be for a parent/related region)
4. **Reserved values:** 0x0000 and 0xFFFF are avoided (incremented/decremented)

This means:
- Transport codes are **per-packet** — same region produces different codes for different payloads
- A repeater must have the region key (derived from name) to verify if a packet matches
- The `findMatch()` method iterates regions, computes expected transport code, and compares with packet

### Serialization (from Packet.cpp)

```cpp
// writeTo()
if (hasTransportCodes()) {
    memcpy(&dest[i], &transport_codes[0], 2); i += 2;
    memcpy(&dest[i], &transport_codes[1], 2); i += 2;
}

// readFrom()
if (hasTransportCodes()) {
    memcpy(&transport_codes[0], &src[i], 2); i += 2;
    memcpy(&transport_codes[1], &src[i], 2); i += 2;
} else {
    transport_codes[0] = transport_codes[1] = 0;
}
```

---

## Forwarding Logic (from MeshCore Source)

### filterRecvFloodPacket() (MyMesh.cpp)

Called during packet reception to determine region membership:

```cpp
bool MyMesh::filterRecvFloodPacket(Packet* pkt) {
    if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
        // Iterate regions, compute expected transport code, compare with packet
        recv_pkt_region = region_map.findMatch(pkt, REGION_DENY_FLOOD);
    } else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
        // Legacy flood: check wildcard entry
        if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
            recv_pkt_region = NULL;
        } else {
            recv_pkt_region = &region_map.getWildcard();
        }
    } else {
        recv_pkt_region = NULL;
    }
    return false;  // continue normal processing
}
```

### allowPacketForward() (MyMesh.cpp)

Called to decide whether to rebroadcast:

```cpp
bool MyMesh::allowPacketForward(const Packet* packet) {
    if (_prefs.disable_fwd) return false;
    if (packet->isRouteFlood() && packet->getPathHashCount() >= _prefs.flood_max)
        return false;
    if (packet->isRouteFlood() && recv_pkt_region == NULL) {
        // Unknown transport code or wildcard denied
        return false;
    }
    // Loop detection checks...
    return true;
}
```

### findMatch() Logic (RegionMap.cpp)

For each region entry where `(flags & REGION_DENY_FLOOD) == 0`:
1. Load the region's transport key (SHA256 of name → 16-byte key)
2. Compute expected transport code: `HMAC-SHA256(key, payload)` → 2 bytes
3. Compare with `packet->transport_codes[0]`
4. If match found, return the entry (non-NULL = allow)

If a matching region has `REGION_DENY_FLOOD` set, return NULL (= block).

---

## CLI Commands (MeshCore Reference)

| Command | Description |
|---------|-------------|
| `region` | List all defined regions and flood permissions (serial only) |
| `region get {* \| name}` | Show region name, parent, flood status |
| `region put {name} {* \| parent}` | Add/update region definition |
| `region remove {name}` | Remove region (must have no children) |
| `region allowf {* \| name}` | Allow flood forwarding for region |
| `region denyf {* \| name}` | Deny flood forwarding (avoid on `*`) |
| `region home` | Show current home region |
| `region home {* \| name}` | Set home region |
| `region list {allowed\|denied}` | List regions by flood permission |
| `region save` | Persist region map to storage |
| `region load` | Load region hierarchy via serial (multi-line) |

---

## CubeCellMeshCore Implementation Plan

### Hardware Constraints

The HTCC-AB01 (ASR6501) has severe resource limits:
- **Flash:** 131 KB (currently 99.8% used — ~256 bytes free)
- **RAM:** 16 KB (currently 53.1% used)
- **EEPROM:** 576 bytes (extended from 512 for region persistence)
- **Crypto:** `rweather/Crypto` library already included (SHA256, HMAC available)

### Feasibility Analysis

The region system requires:
1. **SHA256 key derivation** — already available via Crypto library
2. **HMAC-SHA256 computation per region per packet** — CPU cost per match
3. **Region storage** — names + flags + derived keys

**Critical issue:** With ~900 bytes of Flash free, a full implementation won't fit.
Options:
- Make it a compile-time feature (`#ifdef ENABLE_REGIONS`)
- Disable other features in LITE_MODE to free space
- Implement only transport code parsing (read-only, no filtering)

### Proposed Simplified Design

Minimal implementation with 4 region slots:

```cpp
#define REGION_MAX_ENTRIES    4
#define REGION_NAME_LEN       16    // Max region name (e.g., "au-nsw")
#define REGION_DENY_FLOOD     0x01

struct RegionEntry {
    char name[REGION_NAME_LEN];     // Region name
    uint8_t flags;                  // REGION_DENY_FLOOD etc.
    uint8_t key[16];               // SHA256(name) truncated - cached
};

struct RegionMap {
    uint8_t count;                                  // 0 = no filtering
    RegionEntry entries[REGION_MAX_ENTRIES];
    char home[REGION_NAME_LEN];
};
```

**Memory cost:** ~4 * (16+1+16) + 16 + 1 = ~149 bytes RAM/EEPROM

### Implementation Phases

#### Phase 1: Transport Code Support in Packet.h --- DONE
- Added `uint16_t transport_codes[2]` to MCPacket
- Updated `serialize()`/`deserialize()` for transport code wire format
- Added `hasTransportCodes()` helper
- **No behavioral change** — all packets still forwarded
- **Cost:** +48 bytes Flash, +16 bytes RAM (4 queue slots x 4 bytes)

#### Phase 2: RegionMap Data Structure --- DONE
- Created `src/mesh/RegionMap.h` with RegionEntry/RegionMap classes
- SHA256 key derivation from region name (uses existing Crypto lib)
- HMAC-SHA256 transport code verification via findMatch()
- put/remove/allowFlood/denyFlood operations
- 4 region slots + wildcard entry
- **Cost:** +272 bytes Flash, +168 bytes RAM

#### Phase 3: Forwarding Integration --- DONE
- Added region filter in `shouldForward()` in main.cpp
- TRANSPORT_FLOOD: checks regionMap.findMatch() against transport codes
- Legacy FLOOD: checks wildcard REGION_DENY_FLOOD flag
- Default behavior: forward everything (no regions configured = no filtering)

#### Phase 4: CLI Commands --- DONE
- `region` — list wildcard and all entries with flood status (A=allow, D=deny)
- `region put {name}` — add region entry (admin only)
- `region remove {name}` — remove region entry (admin only)
- `region allowf {name}` — allow flood for region or `*` (admin only)
- `region denyf {name}` — deny flood for region or `*` (admin only)
- Available via serial and remote CLI
- **Cost:** ~744 bytes Flash (freed by optimizing debug output and CLI aliases)

#### Phase 5: EEPROM Persistence --- DONE
- Extended EEPROM_SIZE from 512 to 576 bytes
- Region data at offset 512: magic(2) + count(1) + wildcard_flags(1) + 4×(name[12]+flags)
- Keys NOT saved in EEPROM — derived via SHA256(name) on load (saves 64 bytes)
- `region save` — persist region map to EEPROM (admin only)
- `region load` — load region map from EEPROM (admin only, also called at boot)
- **Cost:** ~672 bytes Flash, +64 bytes EEPROM

#### Phase 6: Testing (TODO)
- Transport code serialization/deserialization
- SHA256 key derivation from names
- HMAC-SHA256 transport code computation
- Region matching with deny/allow
- EEPROM save/load round-trip
- Backward compatibility with legacy FLOOD packets
- Interoperability with MeshCore upstream

### Flash Budget

| Component | Actual Size |
|-----------|-------------|
| Transport code serialize/deserialize (Phase 1) | +48 bytes Flash, +16 bytes RAM |
| RegionMap + findMatch (Phase 2-3) | +272 bytes Flash, +168 bytes RAM |
| CLI commands (Phase 4) | +744 bytes Flash |
| EEPROM save/load (Phase 5) | +672 bytes Flash, +64 bytes EEPROM |
| Flash freed by optimizing debug output and CLI aliases | -1,088 bytes |
| **Net total** | **+648 bytes Flash, +184 bytes RAM, +64 bytes EEPROM** |

After all phases: Flash 99.8% (~256 bytes free), RAM 53.1%

---

## Known Issues in MeshCore Protocol

Per [Issue #1747](https://github.com/meshcore-dev/MeshCore/issues/1747):
- Wildcard `*` behavior is documented inconsistently
- Unclear if `*` means "repeat all traffic" or "repeat only untagged traffic"
- Region-tagged ADVERT packets may be dropped by wildcard-only repeaters
- The protocol is still evolving — implementation should be conservative

---

## References

- [MeshCore GitHub](https://github.com/meshcore-dev/MeshCore)
- [MeshCore Packet.h](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — wire format, route types
- [MeshCore Packet.cpp](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.cpp) — transport code serialization
- [MeshCore helpers/RegionMap.h](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.h) — RegionEntry, findMatch
- [MeshCore helpers/RegionMap.cpp](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.cpp) — region matching logic
- [MeshCore helpers/TransportKeyStore.h](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/TransportKeyStore.h) — key derivation
- [MeshCore helpers/TransportKeyStore.cpp](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/TransportKeyStore.cpp) — HMAC-SHA256 code calc
- [MeshCore simple_repeater/MyMesh.cpp](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp) — forwarding logic
- [MeshCore CLI Reference](https://github.com/meshcore-dev/MeshCore/wiki/Repeater-&-Room-Server-CLI-Reference)
- [MeshCore Issue #1747](https://github.com/meshcore-dev/MeshCore/issues/1747) — region behavior ambiguity
- [MeshCore Region Info](https://github.com/luckystriike22/meshcore_region_info) — community region guide

---

## Changelog

### 2026-03-06 v2 - Corrected with upstream source code analysis
- Transport codes are uint16_t[2] (4 bytes on wire), not UTF-8 strings
- Transport codes are cryptographic: HMAC-SHA256(SHA256(name), payload) → 2 bytes
- Added complete wire format from Packet.cpp
- Added findMatch() crypto verification logic
- Added TransportKeyStore key derivation details
- Added known protocol issues (Issue #1747)
- Updated flash budget with crypto costs
- Referenced actual source files in MeshCore repo

### 2026-03-06 v1 - Design document rewritten
- Removed incorrect allow-list geographic scope design
- Aligned with MeshCore deny-based RegionMap protocol
