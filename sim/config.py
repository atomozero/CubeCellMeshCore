"""
Node configuration and data structures.
Port of src/core/Config.h and globals.h structures.
"""

from __future__ import annotations
from dataclasses import dataclass, field

MC_PACKET_ID_CACHE = 32
MC_MAX_SEEN_NODES = 16
MC_TX_QUEUE_SIZE = 4

# Rate limit defaults
RATE_LIMIT_LOGIN_MAX = 5
RATE_LIMIT_LOGIN_SECS = 60
RATE_LIMIT_REQUEST_MAX = 30
RATE_LIMIT_REQUEST_SECS = 60
RATE_LIMIT_FORWARD_MAX = 100
RATE_LIMIT_FORWARD_SECS = 60

DEFAULT_ADVERT_INTERVAL_MS = 300_000  # 5 minutes


@dataclass
class NodeConfig:
    admin_password: str = "password"
    guest_password: str = "hello"
    advert_interval_ms: int = DEFAULT_ADVERT_INTERVAL_MS
    deep_sleep_enabled: bool = True
    rx_boost_enabled: bool = False
    max_flood_hops: int = 8
    repeat_enabled: bool = True


@dataclass
class Stats:
    rx_count: int = 0
    tx_count: int = 0
    fwd_count: int = 0
    err_count: int = 0
    adv_tx_count: int = 0
    adv_rx_count: int = 0


@dataclass
class SeenNode:
    hash: int = 0
    last_rssi: int = 0
    last_snr: int = 0
    pkt_count: int = 0
    last_seen: int = 0
    name: str = ""


class SeenNodesTracker:
    """Tracks seen nodes, max MC_MAX_SEEN_NODES entries."""

    def __init__(self):
        self.nodes: list[SeenNode] = []

    def update(self, hash_val: int, rssi: int, snr: int,
               name: str | None = None, now_ms: int = 0) -> bool:
        """Update or add node. Returns True if new node."""
        for n in self.nodes:
            if n.hash == hash_val:
                n.last_rssi = rssi
                n.last_snr = snr
                n.pkt_count += 1
                n.last_seen = now_ms
                if name:
                    n.name = name
                return False

        node = SeenNode(
            hash=hash_val, last_rssi=rssi, last_snr=snr,
            pkt_count=1, last_seen=now_ms, name=name or ""
        )
        if len(self.nodes) < MC_MAX_SEEN_NODES:
            self.nodes.append(node)
        else:
            # Evict oldest
            oldest_idx = 0
            oldest_seen = self.nodes[0].last_seen
            for i, n in enumerate(self.nodes):
                if n.last_seen < oldest_seen:
                    oldest_seen = n.last_seen
                    oldest_idx = i
            self.nodes[oldest_idx] = node
        return True

    def get_by_hash(self, hash_val: int) -> SeenNode | None:
        for n in self.nodes:
            if n.hash == hash_val:
                return n
        return None

    def clear(self):
        self.nodes.clear()


class PacketIdCache:
    """Circular buffer for packet ID deduplication."""

    def __init__(self, size: int = MC_PACKET_ID_CACHE):
        self._ids: list[int] = [0] * size
        self._pos: int = 0
        self._size = size

    def add_if_new(self, pkt_id: int) -> bool:
        """Add ID if not already in cache. Returns True if new (added)."""
        if pkt_id in self._ids:
            return False
        self._ids[self._pos] = pkt_id
        self._pos = (self._pos + 1) % self._size
        return True

    def clear(self):
        self._ids = [0] * self._size
        self._pos = 0


class TxQueue:
    """TX queue with max size."""

    def __init__(self, max_size: int = MC_TX_QUEUE_SIZE):
        self._queue: list = []
        self._max_size = max_size

    def add(self, pkt) -> bool:
        if len(self._queue) >= self._max_size:
            return False
        self._queue.append(pkt.copy())
        return True

    def pop(self):
        if self._queue:
            return self._queue.pop(0)
        return None

    @property
    def count(self) -> int:
        return len(self._queue)

    def clear(self):
        self._queue.clear()


MAILBOX_SLOTS = 2       # EEPROM persistent
MAILBOX_RAM_SLOTS = 4   # RAM volatile
MAILBOX_TTL_SEC = 86400 # 24 hours
HEALTH_OFFLINE_MS = 1_800_000  # 30 minutes


@dataclass
class MailboxSlot:
    dest_hash: int = 0
    timestamp: int = 0
    pkt_data: bytes = b""   # raw serialized packet

    @property
    def is_empty(self) -> bool:
        return len(self.pkt_data) == 0

    def clear(self):
        self.dest_hash = 0
        self.timestamp = 0
        self.pkt_data = b""


class Mailbox:
    """Store-and-forward mailbox. Port of src/mesh/Mailbox.h"""

    def __init__(self):
        self.eeprom_slots = [MailboxSlot() for _ in range(MAILBOX_SLOTS)]
        self.ram_slots = [MailboxSlot() for _ in range(MAILBOX_RAM_SLOTS)]

    def _all_slots(self) -> list[MailboxSlot]:
        return self.eeprom_slots + self.ram_slots

    def is_duplicate(self, data: bytes) -> bool:
        for s in self._all_slots():
            if not s.is_empty and s.pkt_data == data:
                return True
        return False

    def store(self, dest_hash: int, pkt_data: bytes, unix_time: int) -> bool:
        """Store serialized packet for offline node. Returns True if stored."""
        if len(pkt_data) == 0:
            return False
        if self.is_duplicate(pkt_data):
            return False

        # Try EEPROM first
        for s in self.eeprom_slots:
            if s.is_empty:
                s.dest_hash = dest_hash
                s.timestamp = unix_time
                s.pkt_data = pkt_data
                return True

        # Try RAM overflow
        for s in self.ram_slots:
            if s.is_empty:
                s.dest_hash = dest_hash
                s.timestamp = unix_time
                s.pkt_data = pkt_data
                return True

        # All full - overwrite oldest across RAM slots only
        oldest = min(self.ram_slots, key=lambda s: s.timestamp)
        oldest.dest_hash = dest_hash
        oldest.timestamp = unix_time
        oldest.pkt_data = pkt_data
        return True

    def count_for(self, dest_hash: int) -> int:
        return sum(1 for s in self._all_slots()
                   if not s.is_empty and s.dest_hash == dest_hash)

    def pop_for(self, dest_hash: int):
        """Retrieve and remove one message. EEPROM first, then RAM. Returns bytes or None."""
        for s in self.eeprom_slots + self.ram_slots:
            if not s.is_empty and s.dest_hash == dest_hash:
                data = s.pkt_data
                s.clear()
                return data
        return None

    def expire_old(self, current_unix_time: int):
        for s in self._all_slots():
            if not s.is_empty and (current_unix_time - s.timestamp) > MAILBOX_TTL_SEC:
                s.clear()

    def get_count(self) -> int:
        return sum(1 for s in self._all_slots() if not s.is_empty)

    def get_total_slots(self) -> int:
        return MAILBOX_SLOTS + MAILBOX_RAM_SLOTS

    def clear(self):
        for s in self._all_slots():
            s.clear()


class RateLimiter:
    """Generic sliding window rate limiter. Port of firmware RateLimiter."""

    def __init__(self, max_count: int, window_secs: int):
        self.window_start: int = 0
        self.window_secs: int = window_secs
        self.max_count: int = max_count
        self.count: int = 0
        self.total_blocked: int = 0
        self.total_allowed: int = 0

    def allow(self, now_secs: int) -> bool:
        if now_secs < self.window_start + self.window_secs:
            self.count += 1
            if self.count > self.max_count:
                self.total_blocked += 1
                return False
        else:
            self.window_start = now_secs
            self.count = 1
        self.total_allowed += 1
        return True

    def reset(self):
        self.window_start = 0
        self.count = 0
        self.total_blocked = 0
        self.total_allowed = 0
