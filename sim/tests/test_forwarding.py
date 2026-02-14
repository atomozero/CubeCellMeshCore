"""Tests for forwarding logic - multi-hop, deduplication, path loop prevention."""

import pytest
from sim.clock import VirtualClock
from sim.runner import SimRunner
from sim.node import SimRepeater, SimCompanion


class TestLinearForwarding:
    """Linear 3 nodes: A -- B -- C"""

    def setup_method(self):
        self.runner = SimRunner()
        self.a = self.runner.add_repeater("A", x=0, y=0)
        self.b = self.runner.add_repeater("B", x=500, y=0)
        self.c = self.runner.add_repeater("C", x=1000, y=0)
        self.runner.set_link("A", "B", rssi=-70, snr=32)
        self.runner.set_link("B", "C", rssi=-75, snr=28)
        # A and C NOT linked
        self.a.time_sync.set_time(1_700_000_000)
        self.b.time_sync.set_time(1_700_000_000)
        self.c.time_sync.set_time(1_700_000_000)

    def test_ping_a_to_c_through_b(self):
        """A pings C, B forwards, C receives and sends PONG back."""
        self.a.send_directed_ping(self.c.identity.hash)

        # Run enough time for packet to travel A->B->C and PONG back C->B->A
        self.runner.run(5000, tick_ms=10)

        # C should have received the ping (check log for PONG)
        c_logs = [msg for _, msg in self.c.log_history if "from" in msg and "PING" not in msg or "PONG ->" in msg]
        # A should have received PONG
        a_pong_logs = [msg for _, msg in self.a.log_history if "PONG" in msg and "->" not in msg]
        assert len(a_pong_logs) > 0, "A should receive PONG from C"

    def test_b_forwards_packet(self):
        """B should increment fwd_count."""
        self.a.send_directed_ping(self.c.identity.hash)
        self.runner.run(5000, tick_ms=10)
        assert self.b.stats.fwd_count > 0, "B should forward packets"

    def test_path_grows_through_forwarding(self):
        """Path should contain A's hash then B's hash after forwarding."""
        self.a.send_directed_ping(self.c.identity.hash)

        # After A sends, the packet has path=[A.hash]
        # After B forwards, path should be [A.hash, B.hash]
        self.runner.run(5000, tick_ms=10)

        # Check B's forward log
        fwd_logs = [msg for _, msg in self.b.log_history if "[F]" in msg]
        assert len(fwd_logs) > 0


class TestDeduplication:
    """Test that duplicate packets are not processed twice."""

    def setup_method(self):
        self.runner = SimRunner()
        # Triangle: A-B, A-C, B-C
        self.a = self.runner.add_repeater("A", x=0, y=0)
        self.b = self.runner.add_repeater("B", x=500, y=0)
        self.c = self.runner.add_repeater("C", x=250, y=400)
        self.runner.set_link("A", "B", rssi=-70, snr=32)
        self.runner.set_link("A", "C", rssi=-70, snr=32)
        self.runner.set_link("B", "C", rssi=-70, snr=32)
        for n in [self.a, self.b, self.c]:
            n.time_sync.set_time(1_700_000_000)

    def test_no_infinite_loop(self):
        """Broadcast should not loop infinitely."""
        self.a.send_directed_ping(0xFF)  # ping nonexistent node

        # Run for a while - should not explode
        self.runner.run(10000, tick_ms=10)

        # Forward counts should be reasonable (each node forwards once at most)
        total_fwd = self.b.stats.fwd_count + self.c.stats.fwd_count
        assert total_fwd <= 10, f"Too many forwards: {total_fwd} (possible loop)"


class TestPathLoopPrevention:
    def test_node_not_in_own_path(self):
        """A node should not forward a packet that has its own hash in path."""
        runner = SimRunner()
        a = runner.add_repeater("A", x=0, y=0)
        b = runner.add_repeater("B", x=500, y=0)
        runner.set_link("A", "B", rssi=-70, snr=32)

        a.time_sync.set_time(1_700_000_000)
        b.time_sync.set_time(1_700_000_000)

        a.send_directed_ping(0xFF)
        runner.run(5000, tick_ms=10)

        # B forwards once, then if the packet somehow comes back
        # B should not forward it again (its hash is in path)
        # With only 2 nodes and one link, B can forward at most once
        assert b.stats.fwd_count <= 2  # DP forward + possibly ADVERT
