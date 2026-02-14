"""
Tests for DIRECT routing support in the repeater.
Validates that packets with ROUTE_DIRECT are forwarded correctly
by peeling the path (removing path[0]) instead of appending.
"""

import pytest
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from sim.clock import VirtualClock
from sim.node import SimRepeater
from sim.packet import (
    MCPacket, MC_ROUTE_FLOOD, MC_ROUTE_DIRECT, MC_PAYLOAD_REQUEST,
    MC_PAYLOAD_RESPONSE, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1,
    MC_MAX_PATH_SIZE,
)


def make_repeater(name="RPT"):
    clock = VirtualClock()
    return SimRepeater(name, clock)


def make_flood_pkt(dest_hash=0xAA, src_hash=0xBB, path=None):
    """Create a FLOOD REQUEST packet."""
    pkt = MCPacket()
    pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_REQUEST, MC_PAYLOAD_VER_1)
    pkt.payload = bytes([dest_hash, src_hash]) + b'\x00' * 20
    pkt.path = list(path) if path else [src_hash]
    return pkt


def make_direct_pkt(path, dest_hash=0xAA, src_hash=0xBB):
    """Create a DIRECT REQUEST packet with given path."""
    pkt = MCPacket()
    pkt.set_header(MC_ROUTE_DIRECT, MC_PAYLOAD_REQUEST, MC_PAYLOAD_VER_1)
    pkt.payload = bytes([dest_hash, src_hash]) + b'\x00' * 20
    pkt.path = list(path)
    return pkt


class TestDirectRoutingShouldForward:
    """Test _should_forward() with DIRECT packets."""

    def test_direct_pkt_forwarded_when_we_are_next_hop(self):
        """DIRECT packet with path[0]==our hash should be forwarded."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash
        pkt = make_direct_pkt(path=[my_hash, 0xCC, 0xDD])
        assert rpt._should_forward(pkt) is True

    def test_direct_pkt_dropped_when_not_next_hop(self):
        """DIRECT packet with path[0]!=our hash should be dropped."""
        rpt = make_repeater()
        pkt = make_direct_pkt(path=[0xFF, 0xCC, 0xDD])
        assert rpt._should_forward(pkt) is False

    def test_direct_pkt_dropped_when_path_empty(self):
        """DIRECT packet with empty path should be dropped."""
        rpt = make_repeater()
        pkt = make_direct_pkt(path=[])
        assert rpt._should_forward(pkt) is False

    def test_direct_pkt_addressed_to_us_dropped(self):
        """DIRECT packet addressed to us (payload[0]==our hash) should be dropped."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash
        pkt = make_direct_pkt(path=[my_hash, 0xCC], dest_hash=my_hash)
        assert rpt._should_forward(pkt) is False

    def test_direct_pkt_dedup(self):
        """Duplicate DIRECT packets should be deduplicated."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash
        pkt1 = make_direct_pkt(path=[my_hash, 0xCC])
        pkt2 = make_direct_pkt(path=[my_hash, 0xCC])
        assert rpt._should_forward(pkt1) is True
        assert rpt._should_forward(pkt2) is False  # duplicate

    def test_flood_still_works(self):
        """FLOOD packets should still be forwarded normally."""
        rpt = make_repeater("RPTF1")
        pkt = make_flood_pkt(dest_hash=0x11, src_hash=0x22, path=[0x22])
        assert rpt._should_forward(pkt) is True

    def test_flood_loop_prevention(self):
        """FLOOD packet with our hash in path should be dropped."""
        rpt = make_repeater("RPTF2")
        my_hash = rpt.identity.hash
        pkt = make_flood_pkt(dest_hash=0x33, src_hash=0x44, path=[0x44, my_hash])
        assert rpt._should_forward(pkt) is False


class TestDirectRoutingPathHandling:
    """Test the path modification in on_rx_packet."""

    def test_direct_pkt_peels_path(self):
        """DIRECT forwarding should remove path[0] (peel)."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash
        pkt = make_direct_pkt(path=[my_hash, 0xCC, 0xDD])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        # Check TX queue has the forwarded packet
        assert rpt.tx_queue.count > 0
        fwd = rpt.tx_queue.pop()
        # Path should have our hash removed, leaving [0xCC, 0xDD]
        assert fwd.path == [0xCC, 0xDD]
        assert fwd.route_type == MC_ROUTE_DIRECT

    def test_direct_pkt_single_hop_peels_to_empty(self):
        """DIRECT with path=[our_hash] should peel to empty path."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash
        pkt = make_direct_pkt(path=[my_hash])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        assert rpt.tx_queue.count > 0
        fwd = rpt.tx_queue.pop()
        assert fwd.path == []

    def test_flood_pkt_appends_hash(self):
        """FLOOD forwarding should append our hash to path."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash
        pkt = make_flood_pkt(path=[0xBB])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        assert rpt.tx_queue.count > 0
        fwd = rpt.tx_queue.pop()
        assert fwd.path == [0xBB, my_hash]

    def test_direct_not_forwarded_wrong_hop(self):
        """DIRECT packet not addressed to us as next hop should not be forwarded."""
        rpt = make_repeater()
        pkt = make_direct_pkt(path=[0xFF, 0xCC])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        assert rpt.tx_queue.count == 0

    def test_fwd_count_incremented_for_direct(self):
        """Forward count should increment for DIRECT packets."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash
        pkt = make_direct_pkt(path=[my_hash, 0xCC])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        assert rpt.stats.fwd_count == 1

    def test_direct_log_message(self):
        """Direct forwarding should produce a log entry."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash
        pkt = make_direct_pkt(path=[my_hash, 0xCC])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        logs = [msg for _, msg in rpt.log_history if "Direct" in msg]
        assert len(logs) > 0


class TestDirectRoutingMultiHop:
    """Test DIRECT routing through multiple repeaters."""

    def test_two_hop_direct_relay(self):
        """Packet should traverse two repeaters via DIRECT routing."""
        rpt1 = make_repeater("RPT1")
        rpt2 = make_repeater("RPT2")
        h1 = rpt1.identity.hash
        h2 = rpt2.identity.hash

        # Packet needs to go through RPT1 then RPT2
        pkt = make_direct_pkt(path=[h1, h2, 0xDD])

        # RPT1 receives and forwards
        rpt1.on_rx_packet(pkt, rssi=-80, snr=20)
        assert rpt1.tx_queue.count > 0
        fwd1 = rpt1.tx_queue.pop()
        assert fwd1.path == [h2, 0xDD]

        # RPT2 receives the forwarded packet and forwards
        rpt2.on_rx_packet(fwd1, rssi=-80, snr=20)
        assert rpt2.tx_queue.count > 0
        fwd2 = rpt2.tx_queue.pop()
        assert fwd2.path == [0xDD]

    def test_direct_response_uses_reverse_path(self):
        """Simulate FLOOD discovery -> DIRECT response pattern."""
        rpt = make_repeater()
        my_hash = rpt.identity.hash

        # Step 1: FLOOD from companion A through repeater (builds path)
        flood_pkt = make_flood_pkt(dest_hash=0xAA, src_hash=0xBB, path=[0xBB])
        rpt.on_rx_packet(flood_pkt, rssi=-80, snr=20)
        fwd = rpt.tx_queue.pop()
        assert fwd.path == [0xBB, my_hash]  # path built up

        # Step 2: Response from companion B via DIRECT with reverse path
        # B saw path [0xBB, my_hash], so it sends back via [my_hash, 0xBB]
        direct_pkt = make_direct_pkt(
            path=[my_hash, 0xBB],
            dest_hash=0xBB, src_hash=0xAA
        )
        rpt.on_rx_packet(direct_pkt, rssi=-80, snr=20)
        assert rpt.tx_queue.count > 0
        fwd2 = rpt.tx_queue.pop()
        assert fwd2.path == [0xBB]  # peeled, next hop is 0xBB
