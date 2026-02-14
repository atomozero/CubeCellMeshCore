"""Tests for Quiet Hours (Feature 4)."""
import pytest
from sim.clock import VirtualClock
from sim.node import SimRepeater, TAG_FWD
from sim.config import RATE_LIMIT_FORWARD_MAX


def make_repeater(name="R1"):
    clock = VirtualClock()
    r = SimRepeater(name, clock)
    return r, clock


class TestQuietHoursConfig:
    def test_disabled_by_default(self):
        r, _ = make_repeater()
        assert not r.is_quiet_hours_enabled()
        assert not r._in_quiet_period

    def test_enable_quiet_hours(self):
        r, _ = make_repeater()
        r.set_quiet_hours(22, 6)
        assert r.is_quiet_hours_enabled()
        assert r.quiet_start_hour == 22
        assert r.quiet_end_hour == 6
        assert r.quiet_forward_max == 30

    def test_custom_max(self):
        r, _ = make_repeater()
        r.set_quiet_hours(22, 6, max_fwd=10)
        assert r.quiet_forward_max == 10

    def test_disable_restores_limit(self):
        r, _ = make_repeater()
        r.set_quiet_hours(22, 6)
        r._evaluate_quiet_hours(23)
        assert r._in_quiet_period
        r.disable_quiet_hours()
        assert not r._in_quiet_period
        assert r.forward_limiter.max_count == RATE_LIMIT_FORWARD_MAX


class TestQuietHoursEvaluation:
    def test_overnight_wrap_active(self):
        """22:00-06:00 range, hour=23 → active."""
        r, _ = make_repeater()
        r.set_quiet_hours(22, 6)
        r._evaluate_quiet_hours(23)
        assert r._in_quiet_period
        assert r.forward_limiter.max_count == 30

    def test_overnight_wrap_inactive(self):
        """22:00-06:00 range, hour=12 → not active."""
        r, _ = make_repeater()
        r.set_quiet_hours(22, 6)
        r._evaluate_quiet_hours(12)
        assert not r._in_quiet_period
        assert r.forward_limiter.max_count == RATE_LIMIT_FORWARD_MAX

    def test_same_day_range(self):
        """08:00-18:00 range, hour=10 → active."""
        r, _ = make_repeater()
        r.set_quiet_hours(8, 18)
        r._evaluate_quiet_hours(10)
        assert r._in_quiet_period

    def test_transition_on_off(self):
        """Enter quiet, then leave."""
        r, _ = make_repeater()
        r.set_quiet_hours(22, 6)
        r._evaluate_quiet_hours(23)
        assert r._in_quiet_period
        assert r.forward_limiter.max_count == 30

        r._evaluate_quiet_hours(7)
        assert not r._in_quiet_period
        assert r.forward_limiter.max_count == RATE_LIMIT_FORWARD_MAX
