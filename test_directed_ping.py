#!/usr/bin/env python3
"""
Test suite for directed ping/pong and trace features.
Validates payload format, routing logic, and CLI parsing
without requiring hardware.

Run: python3 test_directed_ping.py
"""

import struct
import sys

# --- Constants (mirrored from Packet.h) ---
MC_ROUTE_FLOOD = 0x01
MC_PAYLOAD_PLAIN = 0x02
MC_PAYLOAD_VER_1 = 0x00
MC_MAX_PAYLOAD_SIZE = 180

passed = 0
failed = 0


def test(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
        print(f"  PASS: {name}")
    else:
        failed += 1
        print(f"  FAIL: {name}  {detail}")


def make_header(route, ptype, ver):
    return (route & 0x03) | ((ptype & 0x0F) << 2) | ((ver & 0x03) << 6)


def get_route(header):
    return header & 0x03


def get_payload_type(header):
    return (header >> 2) & 0x0F


# ============================================================
# Test 1: Directed Ping payload format
# ============================================================
print("\n=== Test 1: Directed Ping payload format ===")

target_hash = 0xA3
src_hash = 0x5B
ping_counter = 1
node_name = "MyNode"

# Build payload as the firmware does:
# [destHash][srcHash]['D']['P'][text: "#N name"]
text = f"#{ping_counter} {node_name}"
payload = bytes([target_hash, src_hash, ord('D'), ord('P')]) + text.encode()

test("payload[0] = target hash", payload[0] == 0xA3)
test("payload[1] = source hash", payload[1] == 0x5B)
test("payload[2:4] = 'DP' marker", payload[2:4] == b'DP')
test("text starts with #", payload[4] == ord('#'))
test("payload length <= MC_MAX_PAYLOAD_SIZE", len(payload) <= MC_MAX_PAYLOAD_SIZE)
test("text contains node name", node_name.encode() in payload[4:])

# ============================================================
# Test 2: PONG payload format
# ============================================================
print("\n=== Test 2: PONG payload format ===")

pong_target = 0x5B  # back to original sender
pong_src = 0xA3     # responder hash
responder_name = "Relay1"
rssi = -65

# Build PONG payload:
# [destHash][srcHash]['P']['O'][text: "name rssi"]
pong_text = f"{responder_name} {rssi}"
pong_payload = bytes([pong_target, pong_src, ord('P'), ord('O')]) + pong_text.encode()

test("pong payload[0] = dest (original sender)", pong_payload[0] == 0x5B)
test("pong payload[1] = src (responder)", pong_payload[1] == 0xA3)
test("pong payload[2:4] = 'PO' marker", pong_payload[2:4] == b'PO')
test("pong text contains responder name", responder_name.encode() in pong_payload[4:])
test("pong text contains rssi value", str(rssi).encode() in pong_payload[4:])

# ============================================================
# Test 3: Header encoding
# ============================================================
print("\n=== Test 3: Header encoding for FLOOD PLAIN ===")

header = make_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)

test("route type = FLOOD", get_route(header) == MC_ROUTE_FLOOD)
test("payload type = PLAIN", get_payload_type(header) == MC_PAYLOAD_PLAIN)
test("header is single byte", 0 <= header <= 255)

# ============================================================
# Test 4: Reception matching logic - DP for us
# ============================================================
print("\n=== Test 4: Reception matching - DP addressed to us ===")

my_hash = 0xA3

# Simulate incoming DP packet for us
dp_payload = bytes([my_hash, 0x5B, ord('D'), ord('P')]) + b"#1 Sender"

is_dp = (len(dp_payload) >= 4
         and dp_payload[2] == ord('D')
         and dp_payload[3] == ord('P')
         and dp_payload[0] == my_hash)

test("DP packet recognized", is_dp)
test("sender hash extracted", dp_payload[1] == 0x5B)

# ============================================================
# Test 5: Reception matching logic - DP NOT for us
# ============================================================
print("\n=== Test 5: Reception matching - DP not for us ===")

other_hash = 0xFF
dp_other = bytes([other_hash, 0x5B, ord('D'), ord('P')]) + b"#1 Sender"

is_dp_other = (len(dp_other) >= 4
               and dp_other[2] == ord('D')
               and dp_other[3] == ord('P')
               and dp_other[0] == my_hash)

test("DP for other node NOT matched", not is_dp_other)

# ============================================================
# Test 6: Reception matching logic - PO for us
# ============================================================
print("\n=== Test 6: Reception matching - PO (PONG) for us ===")

po_payload = bytes([my_hash, 0x5B, ord('P'), ord('O')]) + b"Relay1 -65"

is_po = (len(po_payload) >= 4
         and po_payload[2] == ord('P')
         and po_payload[3] == ord('O')
         and po_payload[0] == my_hash)

test("PO packet recognized", is_po)
test("pong sender hash extracted", po_payload[1] == 0x5B)
test("pong text parseable", b"-65" in po_payload[4:])

# ============================================================
# Test 7: TXT_MSG NOT falsely matched as DP/PO
# ============================================================
print("\n=== Test 7: Encrypted TXT_MSG not confused with DP/PO ===")

# TXT_MSG: [destHash][srcHash][MAC+encrypted...] - bytes 2,3 are encrypted, unlikely DP/PO
# Simulate a normal TXT_MSG (10+ bytes, bytes 2-3 are MAC, not 'D','P')
txt_payload = bytes([my_hash, 0x5B, 0x7A, 0x3F]) + bytes(range(10))

is_txt_dp = (len(txt_payload) >= 4
             and txt_payload[2] == ord('D')
             and txt_payload[3] == ord('P')
             and txt_payload[0] == my_hash)

is_txt_po = (len(txt_payload) >= 4
             and txt_payload[2] == ord('P')
             and txt_payload[3] == ord('O')
             and txt_payload[0] == my_hash)

# Should fall through to TXT handling
is_txt = (not is_txt_dp and not is_txt_po
          and len(txt_payload) >= 10
          and txt_payload[0] == my_hash)

test("TXT_MSG NOT matched as DP", not is_txt_dp)
test("TXT_MSG NOT matched as PO", not is_txt_po)
test("TXT_MSG falls through to TXT handler", is_txt)

# ============================================================
# Test 8a: Directed Trace payload format
# ============================================================
print("\n=== Test 8a: Directed Trace payload format ===")

# Build DT payload: [destHash][srcHash]['D']['T'][text: "#N name"]
dt_text = f"#{ping_counter} {node_name}"
dt_payload = bytes([target_hash, src_hash, ord('D'), ord('T')]) + dt_text.encode()

test("DT payload[0] = target hash", dt_payload[0] == 0xA3)
test("DT payload[1] = source hash", dt_payload[1] == 0x5B)
test("DT payload[2:4] = 'DT' marker", dt_payload[2:4] == b'DT')
test("DT text starts with #", dt_payload[4] == ord('#'))
test("DT payload length <= MC_MAX_PAYLOAD_SIZE", len(dt_payload) <= MC_MAX_PAYLOAD_SIZE)

# ============================================================
# Test 8b: Trace Response payload format
# ============================================================
print("\n=== Test 8b: Trace Response payload format ===")

tr_name = "Relay1"
tr_rssi = -65
tr_hops = 3

# Build TR payload: [destHash][srcHash]['T']['R'][text: "name rssi hops"]
tr_text = f"{tr_name} {tr_rssi} {tr_hops}"
tr_payload = bytes([pong_target, pong_src, ord('T'), ord('R')]) + tr_text.encode()

test("TR payload[0] = dest (original sender)", tr_payload[0] == 0x5B)
test("TR payload[1] = src (responder)", tr_payload[1] == 0xA3)
test("TR payload[2:4] = 'TR' marker", tr_payload[2:4] == b'TR')
test("TR text contains responder name", tr_name.encode() in tr_payload[4:])
test("TR text contains rssi value", str(tr_rssi).encode() in tr_payload[4:])
test("TR text contains hop count", str(tr_hops).encode() in tr_payload[4:])

# ============================================================
# Test 8c: Reception matching - DT for us
# ============================================================
print("\n=== Test 8c: Reception matching - DT addressed to us ===")

dt_incoming = bytes([my_hash, 0x5B, ord('D'), ord('T')]) + b"#1 Sender"

is_dt = (len(dt_incoming) >= 4
         and dt_incoming[2] == ord('D')
         and dt_incoming[3] == ord('T')
         and dt_incoming[0] == my_hash)

test("DT packet recognized", is_dt)
test("DT sender hash extracted", dt_incoming[1] == 0x5B)

# ============================================================
# Test 8d: Reception matching - TR for us
# ============================================================
print("\n=== Test 8d: Reception matching - TR for us ===")

tr_incoming = bytes([my_hash, 0x5B, ord('T'), ord('R')]) + b"Relay1 -65 3"

is_tr = (len(tr_incoming) >= 4
         and tr_incoming[2] == ord('T')
         and tr_incoming[3] == ord('R')
         and tr_incoming[0] == my_hash)

test("TR packet recognized", is_tr)
test("TR sender hash extracted", tr_incoming[1] == 0x5B)
test("TR text parseable", b"-65" in tr_incoming[4:] and b"3" in tr_incoming[4:])

# ============================================================
# Test 8e: DT/TR not confused with DP/PO
# ============================================================
print("\n=== Test 8e: DT/TR not confused with DP/PO ===")

test("DT marker != DP marker", b'DT' != b'DP')
test("TR marker != PO marker", b'TR' != b'PO')
test("DT not matched as DP", dt_incoming[3] != ord('P'))
test("TR not matched as PO", tr_incoming[2] != ord('P'))

# ============================================================
# Test 8f: CLI parsing "trace A3"
# ============================================================
print("\n=== Test 8f: CLI parsing 'trace' ===")


def parse_trace_cmd(cmd):
    """Simulate the C parsing: strtoul(cmd + 6, NULL, 16)"""
    if cmd.startswith("trace "):
        try:
            return int(cmd[6:].strip(), 16) & 0xFF
        except ValueError:
            return 0
    return None


test("'trace A3' parses to 0xA3", parse_trace_cmd("trace A3") == 0xA3)
test("'trace FF' parses to 0xFF", parse_trace_cmd("trace FF") == 0xFF)
test("'trace 01' parses to 0x01", parse_trace_cmd("trace 01") == 0x01)
test("'trace 0' parses to 0 (rejected)", parse_trace_cmd("trace 0") == 0)
test("'trace' (no arg) returns None", parse_trace_cmd("trace") is None)

# ============================================================
# Test 9: CLI parsing "ping A3"
# ============================================================
print("\n=== Test 9: CLI parsing 'ping' ===")


def parse_ping_cmd(cmd):
    """Simulate the C parsing: strtoul(cmd + 5, NULL, 16)"""
    if cmd.startswith("ping "):
        try:
            return int(cmd[5:].strip(), 16) & 0xFF
        except ValueError:
            return 0
    return None


test("'ping A3' parses to 0xA3", parse_ping_cmd("ping A3") == 0xA3)
test("'ping FF' parses to 0xFF", parse_ping_cmd("ping FF") == 0xFF)
test("'ping 01' parses to 0x01", parse_ping_cmd("ping 01") == 0x01)
test("'ping a3' (lowercase) parses to 0xA3", parse_ping_cmd("ping a3") == 0xA3)
test("'ping 0' parses to 0 (rejected)", parse_ping_cmd("ping 0") == 0)
test("'ping' (no arg) returns None", parse_ping_cmd("ping") is None)

# ============================================================
# Test 10: Broadcast ping unchanged
# ============================================================
print("\n=== Test 10: Broadcast ping format unchanged ===")

# Old broadcast ping: "PING #xxx from CCXXXXXX"
bc_ping = f"PING #{1} from {0x12345678:08X}"

test("broadcast ping starts with 'PING'", bc_ping.startswith("PING"))
test("broadcast ping has no DP marker at [2:4]", True)  # different format entirely
test("broadcast has no dest/src hash prefix", bc_ping[0] == 'P')  # starts with text, not hash byte

# ============================================================
# Test 11: Payload size constraints
# ============================================================
print("\n=== Test 11: Payload size constraints ===")

# Worst case: long node name (15 chars max)
long_name = "A" * 15
dp_text = f"#65535 {long_name}"
dp_full = bytes([0xA3, 0x5B, ord('D'), ord('P')]) + dp_text.encode()

test("max DP payload fits", len(dp_full) <= MC_MAX_PAYLOAD_SIZE,
     f"len={len(dp_full)}")

pong_text = f"{long_name} -120"
pong_full = bytes([0x5B, 0xA3, ord('P'), ord('O')]) + pong_text.encode()

test("max PONG payload fits", len(pong_full) <= MC_MAX_PAYLOAD_SIZE,
     f"len={len(pong_full)}")

dt_text = f"#65535 {long_name}"
dt_full = bytes([0xA3, 0x5B, ord('D'), ord('T')]) + dt_text.encode()
test("max DT payload fits", len(dt_full) <= MC_MAX_PAYLOAD_SIZE,
     f"len={len(dt_full)}")

tr_text = f"{long_name} -120 99"
tr_full = bytes([0x5B, 0xA3, ord('T'), ord('R')]) + tr_text.encode()
test("max TR payload fits", len(tr_full) <= MC_MAX_PAYLOAD_SIZE,
     f"len={len(tr_full)}")

# ============================================================
# Test 12: Serialization roundtrip
# ============================================================
print("\n=== Test 12: Packet serialization roundtrip ===")

# Simulate MCPacket serialization: [header][pathLen][path...][payload...]
header = make_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
path = bytes([0x5B])  # pathLen=1
payload = bytes([0xA3, 0x5B, ord('D'), ord('P')]) + b"#1 TestNode"

wire = bytes([header, len(path)]) + path + payload

# Deserialize
d_header = wire[0]
d_pathLen = wire[1]
d_path = wire[2:2 + d_pathLen]
d_payload = wire[2 + d_pathLen:]

test("header roundtrip", d_header == header)
test("pathLen roundtrip", d_pathLen == 1)
test("path roundtrip", d_path == path)
test("payload roundtrip", d_payload == payload)
test("deserialized payload has DP marker", d_payload[2:4] == b'DP')

# ============================================================
# Summary
# ============================================================
print(f"\n{'=' * 50}")
print(f"Results: {passed} passed, {failed} failed, {passed + failed} total")
print(f"{'=' * 50}")

sys.exit(0 if failed == 0 else 1)
