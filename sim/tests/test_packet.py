"""Tests for MCPacket - serialize/deserialize, header encoding, packet ID."""

import pytest
from sim.packet import (
    MCPacket, MC_ROUTE_FLOOD, MC_ROUTE_DIRECT, MC_PAYLOAD_PLAIN,
    MC_PAYLOAD_ADVERT, MC_PAYLOAD_VER_1, MC_MAX_PAYLOAD_SIZE,
    make_header, get_route_type, get_payload_type, get_version,
)


class TestHeaderEncoding:
    def test_flood_plain_header(self):
        h = make_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        assert get_route_type(h) == MC_ROUTE_FLOOD
        assert get_payload_type(h) == MC_PAYLOAD_PLAIN
        assert get_version(h) == MC_PAYLOAD_VER_1

    def test_direct_advert_header(self):
        h = make_header(MC_ROUTE_DIRECT, MC_PAYLOAD_ADVERT, 0)
        assert get_route_type(h) == MC_ROUTE_DIRECT
        assert get_payload_type(h) == MC_PAYLOAD_ADVERT

    def test_header_is_single_byte(self):
        h = make_header(0x03, 0x0F, 0x03)
        assert 0 <= h <= 255

    def test_all_route_types(self):
        for rt in range(4):
            h = make_header(rt, 0, 0)
            assert get_route_type(h) == rt

    def test_all_payload_types(self):
        for pt in range(16):
            h = make_header(0, pt, 0)
            assert get_payload_type(h) == pt


class TestSerializeDeserialize:
    def test_basic_roundtrip(self):
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [0x5B]
        pkt.payload = bytes([0xA3, 0x5B, ord('D'), ord('P')]) + b"#1 TestNode"

        wire = pkt.serialize()
        restored = MCPacket.deserialize(wire)

        assert restored is not None
        assert restored.header == pkt.header
        assert restored.path == pkt.path
        assert restored.payload == pkt.payload

    def test_empty_path(self):
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_ADVERT, 0)
        pkt.payload = b'\x01' * 101

        wire = pkt.serialize()
        restored = MCPacket.deserialize(wire)

        assert restored is not None
        assert restored.path == []
        assert restored.payload == pkt.payload

    def test_multi_hop_path(self):
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, 0)
        pkt.path = [0xA3, 0x5B, 0xFF]
        pkt.payload = b"test"

        wire = pkt.serialize()
        restored = MCPacket.deserialize(wire)

        assert restored.path == [0xA3, 0x5B, 0xFF]
        assert restored.payload == b"test"

    def test_empty_payload(self):
        pkt = MCPacket()
        pkt.header = 0x09
        pkt.path = [0x01]
        pkt.payload = b''

        wire = pkt.serialize()
        restored = MCPacket.deserialize(wire)

        assert restored.payload == b''
        assert restored.path == [0x01]

    def test_wire_format_matches_firmware(self):
        """Wire format: [header][pathLen][path...][payload...]
        Compatible with test_directed_ping.py Test 12."""
        header = make_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        path = bytes([0x5B])
        payload = bytes([0xA3, 0x5B, ord('D'), ord('P')]) + b"#1 TestNode"

        # Build wire manually (firmware format)
        expected_wire = bytes([header, len(path)]) + path + payload

        # Build wire via MCPacket
        pkt = MCPacket()
        pkt.header = header
        pkt.path = list(path)
        pkt.payload = payload
        actual_wire = pkt.serialize()

        assert actual_wire == expected_wire

    def test_deserialize_too_short(self):
        assert MCPacket.deserialize(b'') is None
        assert MCPacket.deserialize(b'\x00') is None

    def test_deserialize_invalid_path_len(self):
        # pathLen > 64
        assert MCPacket.deserialize(bytes([0x00, 0xFF])) is None


class TestPacketId:
    def test_djb2_deterministic(self):
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, 0)
        pkt.path = [0xA3]
        pkt.payload = b"hello"

        id1 = pkt.get_packet_id()
        id2 = pkt.get_packet_id()
        assert id1 == id2

    def test_different_payload_different_id(self):
        pkt1 = MCPacket()
        pkt1.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, 0)
        pkt1.path = [0xA3]
        pkt1.payload = b"hello"

        pkt2 = MCPacket()
        pkt2.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, 0)
        pkt2.path = [0xA3]
        pkt2.payload = b"world"

        assert pkt1.get_packet_id() != pkt2.get_packet_id()

    def test_djb2_uses_max_8_path_16_payload(self):
        """Firmware hashes at most 8 path bytes and 16 payload bytes."""
        pkt1 = MCPacket()
        pkt1.header = 0x09
        pkt1.path = list(range(20))
        pkt1.payload = bytes(range(32))

        pkt2 = MCPacket()
        pkt2.header = 0x09
        pkt2.path = list(range(8)) + [99] * 12  # first 8 same
        pkt2.payload = bytes(range(16)) + bytes([99] * 16)  # first 16 same

        assert pkt1.get_packet_id() == pkt2.get_packet_id()

    def test_id_is_32bit(self):
        pkt = MCPacket()
        pkt.header = 0xFF
        pkt.path = [0xFF] * 8
        pkt.payload = bytes([0xFF] * 16)
        pid = pkt.get_packet_id()
        assert 0 <= pid <= 0xFFFFFFFF


class TestDirectedPingFormat:
    """Test compatibility with test_directed_ping.py format."""

    def test_dp_payload_format(self):
        target_hash = 0xA3
        src_hash = 0x5B
        text = "#1 MyNode"
        payload = bytes([target_hash, src_hash, ord('D'), ord('P')]) + text.encode()

        assert payload[0] == 0xA3
        assert payload[1] == 0x5B
        assert payload[2:4] == b'DP'
        assert payload[4] == ord('#')

    def test_pong_payload_format(self):
        target = 0x5B
        src = 0xA3
        text = "Relay1 -65"
        payload = bytes([target, src, ord('P'), ord('O')]) + text.encode()

        assert payload[0] == 0x5B
        assert payload[1] == 0xA3
        assert payload[2:4] == b'PO'
        assert b"Relay1" in payload[4:]
        assert b"-65" in payload[4:]

    def test_dt_payload_format(self):
        payload = bytes([0xA3, 0x5B, ord('D'), ord('T')]) + b"#1 MyNode"
        assert payload[2:4] == b'DT'

    def test_tr_payload_format(self):
        payload = bytes([0x5B, 0xA3, ord('T'), ord('R')]) + b"Relay1 -65 3"
        assert payload[2:4] == b'TR'
        assert b"-65" in payload[4:]
        assert b"3" in payload[4:]

    def test_max_payload_fits(self):
        long_name = "A" * 15
        dp = bytes([0xA3, 0x5B, ord('D'), ord('P')]) + f"#65535 {long_name}".encode()
        assert len(dp) <= MC_MAX_PAYLOAD_SIZE

        po = bytes([0x5B, 0xA3, ord('P'), ord('O')]) + f"{long_name} -120".encode()
        assert len(po) <= MC_MAX_PAYLOAD_SIZE


class TestPacketProperties:
    def test_is_flood(self):
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, 0)
        assert pkt.is_flood()
        assert not pkt.is_direct()

    def test_is_direct(self):
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_DIRECT, MC_PAYLOAD_PLAIN, 0)
        assert pkt.is_direct()
        assert not pkt.is_flood()

    def test_copy(self):
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, 0)
        pkt.path = [0xA3, 0x5B]
        pkt.payload = b"test"
        pkt.rssi = -70

        copy = pkt.copy()
        assert copy.header == pkt.header
        assert copy.path == pkt.path
        assert copy.payload == pkt.payload
        assert copy.rssi == pkt.rssi

        # Modifying copy doesn't affect original
        copy.path.append(0xFF)
        assert len(pkt.path) == 2
