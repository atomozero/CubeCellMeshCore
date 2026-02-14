"""Tests for directed ping/pong and trace."""

import pytest
from sim.clock import VirtualClock
from sim.runner import SimRunner
from sim.node import SimRepeater, SimCompanion
from sim.packet import MCPacket, MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1


class TestDirectedPing:
    def test_dp_po_roundtrip_direct(self):
        """Two directly connected nodes: A pings B, B responds with PONG."""
        runner = SimRunner()
        a = runner.add_repeater("A", x=0, y=0)
        b = runner.add_repeater("B", x=500, y=0)
        runner.set_link("A", "B", rssi=-65, snr=36)
        a.time_sync.set_time(1_700_000_000)
        b.time_sync.set_time(1_700_000_000)

        a.send_directed_ping(b.identity.hash)
        runner.run(3000, tick_ms=10)

        # A should see PONG
        a_pong = [msg for _, msg in a.log_history if "PONG" in msg and "->" not in msg]
        assert len(a_pong) > 0, "A should receive PONG from B"

    def test_dp_po_through_relay(self):
        """A -- R -- B: ping through relay."""
        runner = SimRunner()
        a = runner.add_repeater("A", x=0, y=0)
        r = runner.add_repeater("R", x=500, y=0)
        b = runner.add_repeater("B", x=1000, y=0)
        runner.set_link("A", "R", rssi=-70, snr=32)
        runner.set_link("R", "B", rssi=-70, snr=32)
        for n in [a, r, b]:
            n.time_sync.set_time(1_700_000_000)

        a.send_directed_ping(b.identity.hash)
        runner.run(5000, tick_ms=10)

        # R should forward
        assert r.stats.fwd_count > 0

        # A should get PONG
        a_pong = [msg for _, msg in a.log_history if "PONG" in msg and "->" not in msg]
        assert len(a_pong) > 0


class TestDirectedTrace:
    def test_dt_tr_roundtrip(self):
        """A traces B directly connected."""
        runner = SimRunner()
        a = runner.add_repeater("A", x=0, y=0)
        b = runner.add_repeater("B", x=500, y=0)
        runner.set_link("A", "B", rssi=-65, snr=36)
        a.time_sync.set_time(1_700_000_000)
        b.time_sync.set_time(1_700_000_000)

        a.send_directed_trace(b.identity.hash)
        runner.run(3000, tick_ms=10)

        # A should see TRACE response
        a_trace = [msg for _, msg in a.log_history if "TRACE" in msg and "~>" not in msg and "->" not in msg]
        assert len(a_trace) > 0, "A should receive trace response from B"

    def test_trace_4_hop(self):
        """A -- B -- C -- D: trace through 3 relays."""
        runner = SimRunner()
        a = runner.add_repeater("A", x=0, y=0)
        b = runner.add_repeater("B", x=300, y=0)
        c = runner.add_repeater("C", x=600, y=0)
        d = runner.add_repeater("D", x=900, y=0)
        runner.set_link("A", "B", rssi=-70, snr=32)
        runner.set_link("B", "C", rssi=-75, snr=28)
        runner.set_link("C", "D", rssi=-80, snr=24)
        for n in [a, b, c, d]:
            n.time_sync.set_time(1_700_000_000)

        a.send_directed_trace(d.identity.hash)
        runner.run(10000, tick_ms=10)

        # A should receive trace response
        a_trace = [msg for _, msg in a.log_history if "TRACE" in msg and "~>" not in msg and "->" not in msg]
        assert len(a_trace) > 0, "A should receive trace response"

        # B and C should forward
        assert b.stats.fwd_count > 0
        assert c.stats.fwd_count > 0


class TestBroadcastPing:
    def test_broadcast_ping_payload(self):
        """Broadcast ping format: "PING #N from XXXXXXXX" """
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [0x5B]

        node_id = 0x12345678
        ping_counter = 1
        pkt.payload = f"PING #{ping_counter} from {node_id:08X}".encode()

        assert pkt.payload.startswith(b"PING")
        assert b"12345678" in pkt.payload
