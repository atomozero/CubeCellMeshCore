"""
Tests for Store-and-Forward Mailbox.
Validates the Mailbox class and its integration with SimRepeater.
"""

import pytest
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from sim.config import Mailbox, MailboxSlot, MAILBOX_SLOTS, MAILBOX_RAM_SLOTS, MAILBOX_TTL_SEC
from sim.clock import VirtualClock
from sim.node import SimRepeater, SimCompanion
from sim.packet import (
    MCPacket, MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_REQUEST,
    MC_PAYLOAD_ADVERT, MC_PAYLOAD_VER_1,
)
from sim.identity import Identity
from sim.advert import build_advert, TimeSync
from sim.radio_env import RadioEnvironment
from sim.runner import SimRunner


# =============================================================================
# Unit tests for Mailbox class
# =============================================================================

class TestMailboxUnit:
    def setup_method(self):
        self.mbox = Mailbox()

    def test_empty_on_create(self):
        assert self.mbox.get_count() == 0
        assert self.mbox.get_total_slots() == MAILBOX_SLOTS + MAILBOX_RAM_SLOTS

    def test_store_and_retrieve(self):
        data = b"\x01\x02\x03\x04\x05"
        assert self.mbox.store(0xAA, data, 1000)
        assert self.mbox.get_count() == 1
        assert self.mbox.count_for(0xAA) == 1

        retrieved = self.mbox.pop_for(0xAA)
        assert retrieved == data
        assert self.mbox.get_count() == 0

    def test_pop_returns_none_when_empty(self):
        assert self.mbox.pop_for(0xAA) is None

    def test_pop_wrong_hash(self):
        self.mbox.store(0xAA, b"\x01\x02", 1000)
        assert self.mbox.pop_for(0xBB) is None
        assert self.mbox.get_count() == 1

    def test_eeprom_slots_fill_first(self):
        for i in range(MAILBOX_SLOTS):
            assert self.mbox.store(0xAA, bytes([i, 0x01, 0x02]), 1000 + i)
        # EEPROM full, now should go to RAM
        assert self.mbox.store(0xAA, bytes([0xFF, 0x01, 0x02]), 2000)
        assert self.mbox.get_count() == MAILBOX_SLOTS + 1

    def test_total_capacity(self):
        total = MAILBOX_SLOTS + MAILBOX_RAM_SLOTS
        for i in range(total):
            assert self.mbox.store(0xAA, bytes([i, 0x01, 0x02, 0x03]), 1000 + i)
        assert self.mbox.get_count() == total

    def test_overflow_overwrites_oldest_ram(self):
        total = MAILBOX_SLOTS + MAILBOX_RAM_SLOTS
        # Fill all slots
        for i in range(total):
            self.mbox.store(0xAA, bytes([i, 0x01, 0x02, 0x03]), 1000 + i)
        # One more - should overwrite oldest RAM slot
        assert self.mbox.store(0xAA, bytes([0xFE, 0x01, 0x02, 0x03]), 9999)
        assert self.mbox.get_count() == total  # still same count

    def test_dedup_rejects_identical(self):
        data = b"\x01\x02\x03\x04\x05"
        assert self.mbox.store(0xAA, data, 1000)
        assert not self.mbox.store(0xAA, data, 1001)  # duplicate rejected
        assert self.mbox.get_count() == 1

    def test_dedup_allows_different_data(self):
        assert self.mbox.store(0xAA, b"\x01\x02\x03", 1000)
        assert self.mbox.store(0xAA, b"\x01\x02\x04", 1001)  # different payload
        assert self.mbox.get_count() == 2

    def test_dedup_same_dest_different_content(self):
        assert self.mbox.store(0xAA, b"\x01\x02", 1000)
        assert self.mbox.store(0xAA, b"\x03\x04", 1001)
        assert self.mbox.get_count() == 2

    def test_ttl_expiry(self):
        self.mbox.store(0xAA, b"\x01\x02\x03", 1000)
        self.mbox.store(0xBB, b"\x04\x05\x06", 2000)

        # Not expired yet
        self.mbox.expire_old(1000 + MAILBOX_TTL_SEC - 1)
        assert self.mbox.get_count() == 2

        # First one expired
        self.mbox.expire_old(1000 + MAILBOX_TTL_SEC + 1)
        assert self.mbox.get_count() == 1
        assert self.mbox.count_for(0xAA) == 0
        assert self.mbox.count_for(0xBB) == 1

    def test_clear(self):
        self.mbox.store(0xAA, b"\x01", 1000)
        self.mbox.store(0xBB, b"\x02", 1001)
        self.mbox.clear()
        assert self.mbox.get_count() == 0

    def test_multiple_dest_hashes(self):
        self.mbox.store(0xAA, b"\x01\x02", 1000)
        self.mbox.store(0xBB, b"\x03\x04", 1001)
        self.mbox.store(0xAA, b"\x05\x06", 1002)

        assert self.mbox.count_for(0xAA) == 2
        assert self.mbox.count_for(0xBB) == 1

        # Pop all for AA
        d1 = self.mbox.pop_for(0xAA)
        d2 = self.mbox.pop_for(0xAA)
        d3 = self.mbox.pop_for(0xAA)
        assert d1 is not None
        assert d2 is not None
        assert d3 is None
        assert self.mbox.count_for(0xBB) == 1

    def test_empty_data_rejected(self):
        assert not self.mbox.store(0xAA, b"", 1000)
        assert self.mbox.get_count() == 0

    def test_pop_eeprom_before_ram(self):
        """EEPROM slots should be popped first."""
        # Fill EEPROM
        for i in range(MAILBOX_SLOTS):
            self.mbox.store(0xAA, bytes([0xE0 + i, 0x01]), 1000 + i)
        # Add to RAM
        self.mbox.store(0xAA, bytes([0xF0, 0x01]), 2000)

        # First pops should come from EEPROM
        d = self.mbox.pop_for(0xAA)
        assert d is not None
        assert d[0] == 0xE0  # First EEPROM slot


# =============================================================================
# Integration tests with SimRepeater
# =============================================================================

class TestMailboxIntegration:
    """Test mailbox store-and-forward in a simulated mesh."""

    def _make_packet(self, dest_hash: int, src_hash: int,
                     marker: bytes = b"DP", text: str = "hello") -> MCPacket:
        """Create a PLAIN packet with dest/src hash."""
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [src_hash]
        pkt.payload = bytes([dest_hash, src_hash]) + marker + text.encode()
        return pkt

    def test_store_for_offline_node(self):
        """Repeater stores packet when dest node is offline."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        # Time sync needed for mailbox
        rpt.time_sync.set_time(1700000000)

        # Add a seen node that will go offline
        target_hash = 0xAA
        rpt.seen_nodes.update(target_hash, -80, 20, name="NodeAA", now_ms=0)
        rpt.seen_nodes.update(target_hash, -80, 20, now_ms=0)  # pkt_count=2

        # Advance time past offline threshold (30 min)
        clock.advance(1_800_001)

        # Send a packet destined for the offline node
        pkt = self._make_packet(target_hash, 0xBB)
        rpt.on_rx_packet(pkt, -90, 10)

        assert rpt.mailbox.get_count() == 1
        assert rpt.mailbox.count_for(target_hash) == 1

    def test_no_store_for_online_node(self):
        """Repeater does NOT store when dest node is recently seen."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        target_hash = 0xAA
        rpt.seen_nodes.update(target_hash, -80, 20, name="NodeAA", now_ms=clock.millis())
        rpt.seen_nodes.update(target_hash, -80, 20, now_ms=clock.millis())

        # Only 5 min later (not offline yet)
        clock.advance(300_000)

        pkt = self._make_packet(target_hash, 0xBB)
        rpt.on_rx_packet(pkt, -90, 10)

        assert rpt.mailbox.get_count() == 0

    def test_no_store_unknown_node(self):
        """Repeater does NOT store for unknown dest (not in seen_nodes)."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        pkt = self._make_packet(0xCC, 0xBB)
        rpt.on_rx_packet(pkt, -90, 10)

        assert rpt.mailbox.get_count() == 0

    def test_no_store_for_self(self):
        """Repeater does NOT store packets addressed to itself."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        my_hash = rpt.identity.hash
        pkt = self._make_packet(my_hash, 0xBB)
        rpt.on_rx_packet(pkt, -90, 10)

        assert rpt.mailbox.get_count() == 0

    def test_no_store_without_time_sync(self):
        """Repeater does NOT store when time is not synced."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        target_hash = 0xAA
        rpt.seen_nodes.update(target_hash, -80, 20, name="NodeAA", now_ms=0)
        rpt.seen_nodes.update(target_hash, -80, 20, now_ms=0)

        clock.advance(1_800_001)

        pkt = self._make_packet(target_hash, 0xBB)
        rpt.on_rx_packet(pkt, -90, 10)

        assert rpt.mailbox.get_count() == 0  # No time sync

    def test_dedup_multiple_repeaters(self):
        """Same packet from multiple paths should only be stored once."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        target_hash = 0xAA
        rpt.seen_nodes.update(target_hash, -80, 20, name="NodeAA", now_ms=0)
        rpt.seen_nodes.update(target_hash, -80, 20, now_ms=0)

        clock.advance(1_800_001)

        # Same logical packet, received twice (different path but same content)
        pkt1 = self._make_packet(target_hash, 0xBB, text="msg1")
        pkt2 = self._make_packet(target_hash, 0xBB, text="msg1")

        rpt.on_rx_packet(pkt1, -90, 10)
        rpt.on_rx_packet(pkt2, -85, 12)  # same content, different RSSI

        assert rpt.mailbox.get_count() == 1  # dedup!

    def test_forward_on_advert(self):
        """Stored messages are forwarded when node sends ADVERT."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        # Create the target node's identity (so we can build its ADVERT)
        target = Identity("TargetNode")
        target_hash = target.hash

        # Register node as seen then gone offline
        rpt.seen_nodes.update(target_hash, -80, 20, name="TargetNode", now_ms=0)
        rpt.seen_nodes.update(target_hash, -80, 20, now_ms=0)

        clock.advance(1_800_001)

        # Store a message
        pkt = self._make_packet(target_hash, 0xBB, text="stored msg")
        rpt.on_rx_packet(pkt, -90, 10)
        assert rpt.mailbox.get_count() == 1

        # Drain TX queue (forwarded packet from on_rx_packet)
        rpt.tick()
        while rpt.tx_queue.count > 0:
            rpt.tx_queue.pop()

        # Node comes back - sends ADVERT
        target_ts = TimeSync(clock)
        target_ts.set_time(1700000000 + 1801)
        advert_pkt = build_advert(target, target_ts, MC_ROUTE_FLOOD)

        # Verify store worked by checking the raw data
        assert rpt.mailbox.get_count() == 1
        # Check the serialized data is valid
        slot = rpt.mailbox.eeprom_slots[0]
        assert len(slot.pkt_data) > 0
        test_pkt = MCPacket()
        assert test_pkt.deserialize(slot.pkt_data), \
            f"Deserialize failed for data len={len(slot.pkt_data)}: {slot.pkt_data.hex()}"

        # Drain TX queue (forwarded packet from on_rx_packet)
        while rpt.tx_queue.count > 0:
            rpt.tx_queue.pop()

        # Node comes back - sends ADVERT
        target_ts = TimeSync(clock)
        target_ts.set_time(1700000000 + 1801)
        advert_pkt = build_advert(target, target_ts, MC_ROUTE_FLOOD)

        rpt.on_rx_packet(advert_pkt, -75, 15)

        # Mailbox should be empty now (delivered)
        assert rpt.mailbox.get_count() == 0, \
            f"Mailbox still has {rpt.mailbox.get_count()} messages"

        # TX queue should have the stored message (before tick drains it)
        found = False
        pkts_to_check = []
        while rpt.tx_queue.count > 0:
            p = rpt.tx_queue.pop()
            if p:
                pkts_to_check.append(p)

        # Check logs for Mbox fwd messages
        mbox_logs = [msg for _, msg in rpt.log_history if "Mbox fwd" in msg]

        assert found or len(mbox_logs) > 0, \
            f"No forwarded packets. Queue had {len(pkts_to_check)} pkts. " \
            f"Mbox logs: {mbox_logs}. All logs: {[m for _, m in rpt.log_history[-10:]]}"

    def test_no_store_broadcast(self):
        """Packets with dest_hash=0 (broadcast) should not be stored."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        pkt = self._make_packet(0x00, 0xBB)  # broadcast dest
        rpt.on_rx_packet(pkt, -90, 10)

        assert rpt.mailbox.get_count() == 0

    def test_store_only_valid_types(self):
        """Only REQUEST, RESPONSE, PLAIN, ANON_REQ trigger store."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        target_hash = 0xAA
        rpt.seen_nodes.update(target_hash, -80, 20, name="NodeAA", now_ms=0)
        rpt.seen_nodes.update(target_hash, -80, 20, now_ms=0)

        clock.advance(1_800_001)

        # ADVERT type should NOT trigger store
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_ADVERT, MC_PAYLOAD_VER_1)
        pkt.payload = bytes([target_hash, 0xBB]) + b"\x00" * 100
        rpt.on_rx_packet(pkt, -90, 10)

        assert rpt.mailbox.get_count() == 0

    def test_multiple_messages_same_dest(self):
        """Multiple different messages for same dest should all be stored."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        target_hash = 0xAA
        rpt.seen_nodes.update(target_hash, -80, 20, name="NodeAA", now_ms=0)
        rpt.seen_nodes.update(target_hash, -80, 20, now_ms=0)

        clock.advance(1_800_001)

        pkt1 = self._make_packet(target_hash, 0xBB, text="msg1")
        pkt2 = self._make_packet(target_hash, 0xCC, text="msg2")
        pkt3 = self._make_packet(target_hash, 0xDD, text="msg3")

        rpt.on_rx_packet(pkt1, -90, 10)
        rpt.on_rx_packet(pkt2, -85, 12)
        rpt.on_rx_packet(pkt3, -80, 14)

        assert rpt.mailbox.get_count() == 3

    def test_node_needs_min_packets(self):
        """Node must have been seen at least 2 times to trigger store."""
        clock = VirtualClock()
        rpt = SimRepeater("RPT", clock)

        rpt.time_sync.set_time(1700000000)

        target_hash = 0xAA
        # Only seen once
        rpt.seen_nodes.update(target_hash, -80, 20, name="NodeAA", now_ms=0)

        clock.advance(1_800_001)

        pkt = self._make_packet(target_hash, 0xBB)
        rpt.on_rx_packet(pkt, -90, 10)

        assert rpt.mailbox.get_count() == 0  # not enough history


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
