"""End-to-end scenario tests."""

import pytest
from sim.runner import SimRunner
from sim.node import SimRepeater, SimCompanion
from sim.packet import MC_TYPE_REPEATER, MC_TYPE_CHAT_NODE, MC_FLAG_HAS_NAME


class TestScenario1Linear:
    """Scenario 1: A -- B -- C linear topology."""

    def test_ping_through_relay(self):
        runner = SimRunner()
        a = runner.add_repeater("A", x=0, y=0)
        b = runner.add_repeater("B", x=500, y=0)
        c = runner.add_repeater("C", x=1000, y=0)
        runner.set_link("A", "B", rssi=-70, snr=32)
        runner.set_link("B", "C", rssi=-75, snr=28)

        for n in [a, b, c]:
            n.time_sync.set_time(1_700_000_000)

        a.send_directed_ping(c.identity.hash)
        runner.run(10_000, tick_ms=10)

        # A should get PONG
        a_pong = [msg for _, msg in a.log_history if "PONG" in msg and "->" not in msg]
        assert len(a_pong) > 0, "A should receive PONG from C via B"

        # B should forward
        assert b.stats.fwd_count > 0, "B must forward packets"


class TestScenario4Trace:
    """Scenario 4: A -- B -- C -- D trace."""

    def test_trace_4_hop(self):
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
        runner.run(15_000, tick_ms=10)

        # A should receive trace response
        a_trace = [msg for _, msg in a.log_history
                   if "TRACE" in msg and "~>" not in msg and "->" not in msg]
        assert len(a_trace) > 0, "A should receive trace response from D"

        # All relays should forward
        assert b.stats.fwd_count > 0
        assert c.stats.fwd_count > 0


class TestScenario5RateLimiting:
    """Scenario 5: Rate limiting."""

    def test_forward_rate_limit(self):
        runner = SimRunner()
        a = runner.add_repeater("A", x=0, y=0)
        b = runner.add_repeater("B", x=500, y=0)
        runner.set_link("A", "B", rssi=-70, snr=32)

        a.time_sync.set_time(1_700_000_000)
        b.time_sync.set_time(1_700_000_000)

        # Send many pings rapidly
        for i in range(150):
            a.send_directed_ping(0xFF)  # to nonexistent node
            runner.run_step(10)

        runner.run(5000, tick_ms=10)

        # B should have hit the rate limit (100 fwd per 60s window)
        # Some packets should be blocked
        rate_logs = [msg for _, msg in b.log_history if "Rate lim" in msg]
        assert len(rate_logs) > 0, "B should rate-limit some forwards"


class TestScenario6Star:
    """Scenario 6: Star topology."""

    def test_star_neighbours(self):
        runner = SimRunner()
        a = runner.add_repeater("Center", x=500, y=500)
        b = runner.add_repeater("North", x=500, y=0)
        c = runner.add_repeater("East", x=1000, y=500)
        d = runner.add_repeater("South", x=500, y=1000)
        e = runner.add_repeater("West", x=0, y=500)

        for name in ["North", "East", "South", "West"]:
            runner.set_link("Center", name, rssi=-65, snr=36)

        for n in [a, b, c, d, e]:
            n.time_sync.set_time(1_700_000_000)

        # All send ADVERT
        for n in [a, b, c, d, e]:
            n.send_advert(True)
        runner.run(10_000, tick_ms=10)

        # Center should see 4 nodes
        assert len(a.seen_nodes.nodes) >= 4

    def test_star_ping_through_center(self):
        runner = SimRunner()
        a = runner.add_repeater("Center", x=500, y=500)
        b = runner.add_repeater("North", x=500, y=0)
        e = runner.add_repeater("South", x=500, y=1000)

        runner.set_link("Center", "North", rssi=-65, snr=36)
        runner.set_link("Center", "South", rssi=-65, snr=36)
        # North and South NOT linked

        for n in [a, b, e]:
            n.time_sync.set_time(1_700_000_000)

        b.send_directed_ping(e.identity.hash)
        runner.run(10_000, tick_ms=10)

        # North should get PONG from South
        b_pong = [msg for _, msg in b.log_history if "PONG" in msg and "->" not in msg]
        assert len(b_pong) > 0, "North should receive PONG from South via Center"


class TestScenario7Companion:
    """Scenario 7: Companion1 -- RepA -- RepB -- Companion2"""

    def setup_method(self):
        self.runner = SimRunner()
        self.c1 = self.runner.add_companion("Comp1", x=0, y=0)
        self.ra = self.runner.add_repeater("RepA", x=300, y=0)
        self.rb = self.runner.add_repeater("RepB", x=700, y=0)
        self.c2 = self.runner.add_companion("Comp2", x=1000, y=0)

        self.runner.set_link("Comp1", "RepA", rssi=-65, snr=36)
        self.runner.set_link("RepA", "RepB", rssi=-70, snr=32)
        self.runner.set_link("RepB", "Comp2", rssi=-65, snr=36)

        for n in [self.c1, self.ra, self.rb, self.c2]:
            n.time_sync.set_time(1_700_000_000)

    def test_companion_does_not_forward(self):
        """Companions should not forward packets."""
        self.c1.send_directed_ping(self.c2.identity.hash)
        self.runner.run(10_000, tick_ms=10)

        # Companions should have fwd_count = 0
        assert self.c1.stats.fwd_count == 0, "Companion1 should not forward"
        assert self.c2.stats.fwd_count == 0, "Companion2 should not forward"

    def test_repeaters_forward(self):
        """Repeaters should forward packets."""
        self.c1.send_directed_ping(self.c2.identity.hash)
        self.runner.run(10_000, tick_ms=10)

        assert self.ra.stats.fwd_count > 0, "RepA should forward"
        assert self.rb.stats.fwd_count > 0, "RepB should forward"

    def test_companion_flags(self):
        """Companion flags should be 0x81, repeater flags 0x82."""
        assert self.c1.identity.flags == (MC_TYPE_CHAT_NODE | MC_FLAG_HAS_NAME)
        assert self.c1.identity.flags == 0x81
        assert self.ra.identity.flags == (MC_TYPE_REPEATER | MC_FLAG_HAS_NAME)
        assert self.ra.identity.flags == 0x82

    def test_ping_through_repeaters(self):
        """Companion1 pings Companion2 through 2 repeaters."""
        self.c1.send_directed_ping(self.c2.identity.hash)
        self.runner.run(15_000, tick_ms=10)

        # C1 should get PONG from C2
        c1_pong = [msg for _, msg in self.c1.log_history if "PONG" in msg and "->" not in msg]
        assert len(c1_pong) > 0, "Comp1 should receive PONG from Comp2"
