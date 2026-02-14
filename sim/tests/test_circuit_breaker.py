"""Tests for Circuit Breaker (Feature 10)."""
import pytest
from sim.clock import VirtualClock
from sim.node import (
    SimRepeater, SimCompanion, TAG_FWD,
    CB_STATE_CLOSED, CB_STATE_OPEN, CB_STATE_HALF_OPEN,
    CB_SNR_THRESHOLD, CB_TIMEOUT_MS,
)
from sim.packet import MCPacket, MC_ROUTE_FLOOD, MC_ROUTE_DIRECT, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1
from sim.advert import build_advert, TimeSync
from sim.radio_env import RadioEnvironment


def make_repeater(name="R1"):
    clock = VirtualClock()
    r = SimRepeater(name, clock)
    return r, clock


class TestCBStateTransitions:
    def test_good_snr_stays_closed(self):
        r, _ = make_repeater()
        r._update_neighbour(0xAA, -60, 20)  # SNR*4=20 > threshold
        assert r.neighbours[0]['cb_state'] == CB_STATE_CLOSED

    def test_bad_snr_opens_breaker(self):
        r, _ = make_repeater()
        r._update_neighbour(0xAA, -110, -50)  # SNR*4=-50 < -40
        assert r.neighbours[0]['cb_state'] == CB_STATE_CLOSED  # first add is always closed
        # Second update with bad SNR
        r._update_neighbour(0xAA, -110, -50)
        assert r.neighbours[0]['cb_state'] == CB_STATE_OPEN

    def test_good_snr_closes_open_breaker(self):
        r, _ = make_repeater()
        r._update_neighbour(0xAA, -110, -50)
        r._update_neighbour(0xAA, -110, -50)  # now OPEN
        assert r.neighbours[0]['cb_state'] == CB_STATE_OPEN
        r._update_neighbour(0xAA, -60, 20)  # good SNR → close
        assert r.neighbours[0]['cb_state'] == CB_STATE_CLOSED

    def test_timeout_to_half_open(self):
        r, clock = make_repeater()
        r._update_neighbour(0xAA, -110, -50)
        r._update_neighbour(0xAA, -110, -50)  # OPEN
        assert r.neighbours[0]['cb_state'] == CB_STATE_OPEN
        clock.advance(CB_TIMEOUT_MS + 1)
        r._tick_circuit_breakers()
        assert r.neighbours[0]['cb_state'] == CB_STATE_HALF_OPEN


class TestCBForwarding:
    def test_direct_blocked_when_open(self):
        """DIRECT packet with next hop having open CB is not forwarded."""
        r, clock = make_repeater()
        target_hash = 0xBB
        # Create neighbour with open CB
        r._update_neighbour(target_hash, -110, -50)
        r._update_neighbour(target_hash, -110, -50)  # OPEN

        # Build a DIRECT packet: path = [our_hash, target_hash]
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_DIRECT, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)  # DIRECT
        pkt.path = [r.identity.hash, target_hash]
        pkt.payload = bytes([0xFF, 0xCC, ord('D'), ord('P')]) + b"test"
        # Ensure unique packet ID
        pkt_id = pkt.get_packet_id()

        r.on_rx_packet(pkt, -80, 20)

        # Check that CB log entry was made
        cb_logs = [msg for _, msg in r.log_history if "CB" in msg]
        assert len(cb_logs) > 0

    def test_direct_allowed_when_closed(self):
        """DIRECT packet with closed CB is forwarded normally."""
        r, clock = make_repeater()
        target_hash = 0xBB
        r._update_neighbour(target_hash, -60, 20)  # good SNR, closed

        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_DIRECT, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)  # DIRECT
        pkt.path = [r.identity.hash, target_hash]
        pkt.payload = bytes([0xFF, 0xCC, ord('D'), ord('P')]) + b"test"

        r.on_rx_packet(pkt, -60, 20)

        # Should see Direct forwarding log, not CB
        fwd_logs = [msg for _, msg in r.log_history if "Direct" in msg]
        assert len(fwd_logs) > 0

    def test_flood_not_blocked(self):
        """FLOOD packets are never blocked by circuit breaker."""
        r, clock = make_repeater()
        # Neighbour with open CB
        r._update_neighbour(0xBB, -110, -50)
        r._update_neighbour(0xBB, -110, -50)

        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [0xCC]
        pkt.payload = bytes([0xFF, 0xDD, ord('D'), ord('P')]) + b"test"

        r.on_rx_packet(pkt, -60, 20)

        fwd_logs = [msg for _, msg in r.log_history if "Flood" in msg]
        assert len(fwd_logs) > 0

    def test_count_method(self):
        r, _ = make_repeater()
        assert r.get_circuit_breaker_count() == 0
        r._update_neighbour(0xAA, -110, -50)
        r._update_neighbour(0xAA, -110, -50)  # OPEN
        assert r.get_circuit_breaker_count() == 1
