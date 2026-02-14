"""Tests for TimeSync - synchronization from ADVERT timestamps."""

import pytest
from sim.clock import VirtualClock
from sim.advert import TimeSync


class TestTimeSync:
    def test_first_sync_immediate(self):
        clock = VirtualClock()
        ts = TimeSync(clock)
        assert not ts.is_synchronized()

        result = ts.sync_from_advert(1_700_000_000)
        assert result == 1  # first sync
        assert ts.is_synchronized()
        assert ts.get_timestamp() == 1_700_000_000

    def test_same_time_no_resync(self):
        clock = VirtualClock()
        ts = TimeSync(clock)
        ts.sync_from_advert(1_700_000_000)

        clock.advance(5000)
        result = ts.sync_from_advert(1_700_000_005)  # 5 seconds later, matches
        assert result == 0  # no change (within tolerance)

    def test_invalid_timestamp_ignored(self):
        clock = VirtualClock()
        ts = TimeSync(clock)

        result = ts.sync_from_advert(100)  # before 2020
        assert result == 0
        assert not ts.is_synchronized()

    def test_resync_needs_consensus(self):
        clock = VirtualClock()
        ts = TimeSync(clock)
        ts.sync_from_advert(1_700_000_000)

        # Receive a very different time - should be stored as pending
        clock.advance(10_000)
        result = ts.sync_from_advert(1_700_001_000)  # 1000s different
        assert result == 0  # pending, not yet resynced

        # Second different source confirming the new time
        clock.advance(1000)
        result = ts.sync_from_advert(1_700_001_001)  # close to pending
        assert result == 2  # consensus reached, resynced

    def test_time_advances_with_clock(self):
        clock = VirtualClock()
        ts = TimeSync(clock)
        ts.sync_from_advert(1_700_000_000)

        clock.advance(60_000)  # 60 seconds
        assert ts.get_timestamp() == 1_700_000_060

    def test_set_time_manual(self):
        clock = VirtualClock()
        ts = TimeSync(clock)
        ts.set_time(1_700_000_000)

        assert ts.is_synchronized()
        assert ts.get_timestamp() == 1_700_000_000

    def test_advert_propagates_time(self):
        """Test time sync through ADVERT in runner."""
        from sim.runner import SimRunner

        runner = SimRunner()
        a = runner.add_repeater("A", x=0, y=0)
        b = runner.add_repeater("B", x=500, y=0)
        c = runner.add_repeater("C", x=1000, y=0)
        runner.set_link("A", "B", rssi=-70, snr=32)
        runner.set_link("B", "C", rssi=-75, snr=28)

        # Only A has time
        a.time_sync.set_time(1_700_000_000)

        # A sends ADVERT which propagates time
        a.send_advert(True)
        runner.run(10_000, tick_ms=10)

        # B should be synced now
        assert b.time_sync.is_synchronized(), "B should sync from A's ADVERT"
        # C should also sync (B forwards the ADVERT)
        # C might need B to send its own ADVERT after syncing
        # At minimum, B should be synced
        b_time = b.time_sync.get_timestamp()
        a_time = a.time_sync.get_timestamp()
        assert abs(b_time - a_time) <= 2, f"B time {b_time} should be close to A time {a_time}"
