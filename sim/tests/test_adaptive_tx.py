"""Tests for Adaptive TX Power (Feature 11)."""
import pytest
from sim.clock import VirtualClock
from sim.node import (
    SimRepeater, TAG_INFO,
    ADAPTIVE_TX_HIGH_SNR, ADAPTIVE_TX_LOW_SNR,
    ADAPTIVE_TX_STEP, ADAPTIVE_TX_MIN_POWER, DEFAULT_TX_POWER,
)


def make_repeater(name="R1"):
    clock = VirtualClock()
    r = SimRepeater(name, clock)
    return r, clock


class TestAdaptiveTxConfig:
    def test_disabled_by_default(self):
        r, _ = make_repeater()
        assert not r.adaptive_tx_enabled
        assert r.current_tx_power == DEFAULT_TX_POWER

    def test_enable_disable(self):
        r, _ = make_repeater()
        r.adaptive_tx_enabled = True
        assert r.adaptive_tx_enabled
        r.adaptive_tx_enabled = False
        assert not r.adaptive_tx_enabled

    def test_returns_minus1_when_disabled(self):
        r, _ = make_repeater()
        r._update_neighbour(0xAA, -60, 60)
        assert r.evaluate_adaptive_tx_power() == -1

    def test_returns_minus1_no_neighbours(self):
        r, _ = make_repeater()
        r.adaptive_tx_enabled = True
        assert r.evaluate_adaptive_tx_power() == -1


class TestAdaptiveTxPowerAdjustment:
    def test_high_snr_reduces_power(self):
        """All neighbours with good SNR → reduce power."""
        r, _ = make_repeater()
        r.adaptive_tx_enabled = True
        # Add neighbours with high SNR (> ADAPTIVE_TX_HIGH_SNR=40)
        r._update_neighbour(0xAA, -50, 60)  # SNR*4=60
        r._update_neighbour(0xBB, -45, 52)  # SNR*4=52

        result = r.evaluate_adaptive_tx_power()
        assert result == DEFAULT_TX_POWER - ADAPTIVE_TX_STEP
        assert r.current_tx_power == DEFAULT_TX_POWER - ADAPTIVE_TX_STEP

    def test_low_snr_increases_power(self):
        """All neighbours with weak SNR → increase power."""
        r, _ = make_repeater()
        r.adaptive_tx_enabled = True
        r.current_tx_power = 10  # start lower
        r._update_neighbour(0xAA, -110, -30)  # SNR*4=-30
        r._update_neighbour(0xBB, -115, -28)  # SNR*4=-28

        result = r.evaluate_adaptive_tx_power()
        assert result == 10 + ADAPTIVE_TX_STEP

    def test_no_change_in_middle_range(self):
        """SNR in middle range → no change."""
        r, _ = make_repeater()
        r.adaptive_tx_enabled = True
        r._update_neighbour(0xAA, -80, 10)  # SNR*4=10, between -20 and 40
        r._update_neighbour(0xBB, -75, 20)

        result = r.evaluate_adaptive_tx_power()
        assert result == -1
        assert r.current_tx_power == DEFAULT_TX_POWER

    def test_floor_at_min_power(self):
        """Power doesn't go below ADAPTIVE_TX_MIN_POWER."""
        r, _ = make_repeater()
        r.adaptive_tx_enabled = True
        r.current_tx_power = ADAPTIVE_TX_MIN_POWER
        r._update_neighbour(0xAA, -40, 60)

        result = r.evaluate_adaptive_tx_power()
        assert result == -1  # already at min, no change
        assert r.current_tx_power == ADAPTIVE_TX_MIN_POWER

    def test_ceiling_at_max_power(self):
        """Power doesn't exceed max."""
        r, _ = make_repeater()
        r.adaptive_tx_enabled = True
        r.current_tx_power = DEFAULT_TX_POWER
        r._update_neighbour(0xAA, -110, -30)

        result = r.evaluate_adaptive_tx_power()
        assert result == -1  # already at max, no change
        assert r.current_tx_power == DEFAULT_TX_POWER

    def test_successive_reductions(self):
        """Multiple evaluations reduce power step by step."""
        r, _ = make_repeater()
        r.adaptive_tx_enabled = True
        r._update_neighbour(0xAA, -40, 60)  # high SNR

        p1 = r.evaluate_adaptive_tx_power()
        assert p1 == DEFAULT_TX_POWER - ADAPTIVE_TX_STEP

        p2 = r.evaluate_adaptive_tx_power()
        assert p2 == DEFAULT_TX_POWER - 2 * ADAPTIVE_TX_STEP

    def test_tick_triggers_evaluation(self):
        """Tick periodic should trigger adaptive TX evaluation."""
        r, clock = make_repeater()
        r.adaptive_tx_enabled = True
        r._update_neighbour(0xAA, -40, 60)

        # Sync time so tick can run
        r.time_sync.sync_from_advert(1000000)

        # Advance 60s to trigger periodic
        clock.advance(60001)
        r.tick()

        # Should see TxP log
        tx_logs = [msg for _, msg in r.log_history if "TxP:" in msg]
        assert len(tx_logs) > 0
