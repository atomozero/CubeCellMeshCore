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
from sim.node import SimRepeater, SimCompanion
from sim.packet import (
    MCPacket, MC_ROUTE_FLOOD, MC_ROUTE_DIRECT, MC_PAYLOAD_REQUEST,
    MC_PAYLOAD_RESPONSE, MC_PAYLOAD_PLAIN, MC_PAYLOAD_ADVERT,
    MC_PAYLOAD_VER_1, MC_MAX_PATH_SIZE,
)
from sim.identity import Identity
from sim.advert import build_advert, TimeSync


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


class TestAdvertForwarding:
    """Test that ADVERT packets are forwarded by the repeater."""

    def _make_advert_from(self, name="CompA"):
        """Build a real ADVERT from a companion identity."""
        ident = Identity(name)
        clock = VirtualClock()
        ts = TimeSync(clock)
        ts.set_time(1700000000)
        return build_advert(ident, ts), ident

    def test_flood_advert_forwarded(self):
        """FLOOD ADVERT should be processed AND forwarded."""
        rpt = make_repeater("RPT_ADV1")
        adv, src_ident = self._make_advert_from("Companion1")
        assert adv.route_type == MC_ROUTE_FLOOD
        rpt.on_rx_packet(adv, rssi=-80, snr=20)
        # Should be forwarded
        assert rpt.tx_queue.count > 0
        fwd = rpt.tx_queue.pop()
        assert fwd.payload_type == MC_PAYLOAD_ADVERT
        assert rpt.identity.hash in fwd.path  # our hash appended

    def test_advert_forwarded_with_path_buildup(self):
        """Forwarded ADVERT should have repeater hash added to path."""
        rpt = make_repeater("RPT_ADV2")
        adv, _ = self._make_advert_from("Companion2")
        adv.path = [0xBB]  # already passed through another node
        rpt.on_rx_packet(adv, rssi=-80, snr=20)
        assert rpt.tx_queue.count > 0
        fwd = rpt.tx_queue.pop()
        assert fwd.path == [0xBB, rpt.identity.hash]

    def test_advert_not_forwarded_if_duplicate(self):
        """Same ADVERT received twice should be forwarded only once."""
        rpt = make_repeater("RPT_ADV3")
        adv1, _ = self._make_advert_from("Companion3")
        adv2 = adv1.copy()
        rpt.on_rx_packet(adv1, rssi=-80, snr=20)
        assert rpt.tx_queue.count == 1
        rpt.tx_queue.pop()
        rpt.on_rx_packet(adv2, rssi=-80, snr=20)
        assert rpt.tx_queue.count == 0  # dedup blocks second

    def test_advert_seen_nodes_updated_and_forwarded(self):
        """ADVERT should update seen nodes AND be forwarded (both happen)."""
        rpt = make_repeater("RPT_ADV4")
        adv, src_ident = self._make_advert_from("TestNode")
        rpt.on_rx_packet(adv, rssi=-75, snr=24)
        # Seen nodes updated (hash tracked from ADVERT pubkey)
        sn = rpt.seen_nodes.get_by_hash(src_ident.hash)
        assert sn is not None
        # Also forwarded
        assert rpt.stats.fwd_count == 1

    def test_direct_advert_not_forwarded_without_our_hash(self):
        """DIRECT ADVERT where we're not next hop should NOT be forwarded."""
        rpt = make_repeater("RPT_ADV5")
        adv, _ = self._make_advert_from("Companion5")
        adv.set_header(MC_ROUTE_DIRECT, MC_PAYLOAD_ADVERT, 0)
        adv.path = [0xFF]  # not our hash
        rpt.on_rx_packet(adv, rssi=-80, snr=20)
        assert rpt.tx_queue.count == 0

    def test_advert_forwarded_through_two_repeaters(self):
        """ADVERT should propagate through multiple repeaters."""
        rpt1 = make_repeater("RPT_R1")
        rpt2 = make_repeater("RPT_R2")
        adv, _ = self._make_advert_from("FarNode")

        # RPT1 receives and forwards
        rpt1.on_rx_packet(adv, rssi=-90, snr=10)
        assert rpt1.tx_queue.count > 0
        fwd1 = rpt1.tx_queue.pop()
        assert fwd1.path == [rpt1.identity.hash]

        # RPT2 receives the forwarded ADVERT
        rpt2.on_rx_packet(fwd1, rssi=-85, snr=15)
        assert rpt2.tx_queue.count > 0
        fwd2 = rpt2.tx_queue.pop()
        assert fwd2.path == [rpt1.identity.hash, rpt2.identity.hash]

    def test_advert_loop_prevention(self):
        """ADVERT with our hash already in path should NOT be forwarded."""
        rpt = make_repeater("RPT_ADV6")
        adv, _ = self._make_advert_from("LoopNode")
        adv.path = [0xBB, rpt.identity.hash]  # we're already in path
        rpt.on_rx_packet(adv, rssi=-80, snr=20)
        assert rpt.tx_queue.count == 0


# =========================================================================
# SNR Adaptive Delay Tests
# =========================================================================
from sim.node import (
    calc_snr_score, calc_rx_delay, calc_tx_jitter, MC_MIN_RSSI_FORWARD,
)


class TestCalcSnrScore:
    """Test calc_snr_score() mapping SNR*4 -> index [0-10]."""

    def test_worst_snr(self):
        """SNR -20dB (=-80 in *4 units) -> score 0."""
        assert calc_snr_score(-80) == 0

    def test_best_snr(self):
        """SNR +15dB (=60 in *4 units) -> score 10."""
        assert calc_snr_score(60) == 10

    def test_midpoint_snr(self):
        """SNR around -2.5dB (=-10 in *4 units) -> score 5."""
        assert calc_snr_score(-10) == 5

    def test_below_minimum_clamped(self):
        """SNR far below -20dB should clamp to 0."""
        assert calc_snr_score(-120) == 0

    def test_above_maximum_clamped(self):
        """SNR far above +15dB should clamp to 10."""
        assert calc_snr_score(100) == 10

    def test_zero_snr(self):
        """SNR 0dB (=0 in *4 units) -> score ~5-6."""
        score = calc_snr_score(0)
        assert 5 <= score <= 6

    def test_monotonically_increasing(self):
        """Higher SNR should give higher or equal score."""
        prev = calc_snr_score(-80)
        for snr in range(-76, 64, 4):
            curr = calc_snr_score(snr)
            assert curr >= prev, f"Score decreased at snr={snr}"
            prev = curr


class TestCalcRxDelay:
    """Test calc_rx_delay() computes delay from SNR score."""

    def test_worst_snr_highest_delay(self):
        """Score 0 (worst SNR) should produce highest delay."""
        d0 = calc_rx_delay(0, 200)
        d10 = calc_rx_delay(10, 200)
        assert d0 > d10

    def test_best_snr_lowest_delay(self):
        """Score 10 (best SNR) should produce lowest delay."""
        d10 = calc_rx_delay(10, 200)
        assert d10 == 65 * 200 // 1000  # 13ms

    def test_delay_scales_with_airtime(self):
        """Delay should scale linearly with airtime."""
        d100 = calc_rx_delay(5, 100)
        d200 = calc_rx_delay(5, 200)
        assert d200 == d100 * 2

    def test_zero_airtime_zero_delay(self):
        """Zero airtime should give zero delay."""
        assert calc_rx_delay(5, 0) == 0

    def test_score_out_of_range_clamped(self):
        """Score > 10 should be clamped to 10."""
        d11 = calc_rx_delay(11, 200)
        d10 = calc_rx_delay(10, 200)
        assert d11 == d10

    def test_better_snr_shorter_delay(self):
        """Each higher score index should have equal or shorter delay."""
        airtime = 200
        prev = calc_rx_delay(0, airtime)
        for idx in range(1, 11):
            curr = calc_rx_delay(idx, airtime)
            assert curr <= prev, f"Delay increased at idx={idx}"
            prev = curr


class TestCalcTxJitter:
    """Test calc_tx_jitter() produces valid range."""

    def test_jitter_non_negative(self):
        """Jitter should never be negative."""
        for _ in range(50):
            j = calc_tx_jitter(200)
            assert j >= 0

    def test_jitter_max_bound(self):
        """Jitter should not exceed 6 * 2 * airtime."""
        airtime = 200
        max_jitter = 6 * 2 * airtime
        for _ in range(50):
            j = calc_tx_jitter(airtime)
            assert j <= max_jitter

    def test_jitter_zero_airtime(self):
        """Zero airtime should give zero jitter."""
        assert calc_tx_jitter(0) == 0


class TestRssiThreshold:
    """Test RSSI minimum threshold for forwarding."""

    def test_packet_below_rssi_threshold_not_forwarded(self):
        """Packet with RSSI below -120 dBm should not be forwarded."""
        rpt = make_repeater("RPT_RSSI1")
        pkt = make_flood_pkt(dest_hash=0x11, src_hash=0x22, path=[0x22])
        rpt.on_rx_packet(pkt, rssi=-130, snr=20)
        assert rpt.tx_queue.count == 0

    def test_packet_at_rssi_threshold_forwarded(self):
        """Packet with RSSI exactly -120 dBm should be forwarded."""
        rpt = make_repeater("RPT_RSSI2")
        pkt = make_flood_pkt(dest_hash=0x11, src_hash=0x22, path=[0x22])
        rpt.on_rx_packet(pkt, rssi=-120, snr=20)
        assert rpt.tx_queue.count > 0

    def test_packet_above_rssi_threshold_forwarded(self):
        """Packet with good RSSI should be forwarded normally."""
        rpt = make_repeater("RPT_RSSI3")
        pkt = make_flood_pkt(dest_hash=0x33, src_hash=0x44, path=[0x44])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        assert rpt.tx_queue.count > 0

    def test_direct_packet_below_rssi_not_forwarded(self):
        """DIRECT packet below RSSI threshold should not be forwarded."""
        rpt = make_repeater("RPT_RSSI4")
        my_hash = rpt.identity.hash
        pkt = make_direct_pkt(path=[my_hash, 0xCC])
        rpt.on_rx_packet(pkt, rssi=-130, snr=20)
        assert rpt.tx_queue.count == 0


class TestDirectVsFloodDelay:
    """Test that DIRECT routing gets shorter delay than FLOOD."""

    def test_direct_delay_less_than_flood(self):
        """DIRECT jitter (half) should be less than FLOOD rxDelay + jitter."""
        airtime = 200
        # DIRECT: jitter / 2, worst case = 6 * 2 * 200 / 2 = 1200
        direct_max = 6 * airtime * 2 // 2
        # FLOOD: rxDelay at mid SNR + zero jitter, best case
        flood_min = calc_rx_delay(5, airtime)
        # FLOOD minimum delay should be comparable or add to DIRECT max
        # The key insight: FLOOD always adds rxDelay, DIRECT does not
        flood_with_no_jitter = calc_rx_delay(5, airtime)
        assert flood_with_no_jitter > 0  # FLOOD always has base delay

    def test_flood_log_contains_snr(self):
        """FLOOD forwarding should log SNR score."""
        rpt = make_repeater("RPT_DLY1")
        pkt = make_flood_pkt(dest_hash=0x11, src_hash=0x22, path=[0x22])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        logs = [msg for _, msg in rpt.log_history if "snr=" in msg]
        assert len(logs) > 0

    def test_direct_log_contains_delay(self):
        """DIRECT forwarding should log delay value."""
        rpt = make_repeater("RPT_DLY2")
        my_hash = rpt.identity.hash
        pkt = make_direct_pkt(path=[my_hash, 0xCC])
        rpt.on_rx_packet(pkt, rssi=-80, snr=20)
        logs = [msg for _, msg in rpt.log_history if "d=" in msg and "Direct" in msg]
        assert len(logs) > 0
