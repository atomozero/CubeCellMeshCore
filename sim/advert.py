"""
MeshCore ADVERT build/parse and TimeSync.
Port of src/mesh/Advert.h.

ADVERT payload:
[0-31]   Public Key (32 bytes)
[32-35]  Timestamp (4 bytes, LE)
[36-99]  Signature (64 bytes) - signs: pubkey + timestamp + appdata
[100]    Flags (1 byte)
[101-108] Location (optional, 8 bytes)
[109+]   Name (variable)
"""

from __future__ import annotations
import struct
from sim.packet import (
    MCPacket, MC_ROUTE_FLOOD, MC_ROUTE_DIRECT, MC_PAYLOAD_ADVERT,
    MC_PAYLOAD_VER_1, MC_FLAG_HAS_LOCATION, MC_FLAG_HAS_NAME,
    MC_TYPE_MASK, MC_TYPE_REPEATER, MC_TYPE_CHAT_NODE,
)
from sim.identity import Identity, MC_PUBLIC_KEY_SIZE, MC_SIGNATURE_SIZE, MC_NODE_NAME_MAX

ADVERT_PUBKEY_OFFSET = 0
ADVERT_TIMESTAMP_OFFSET = 32
ADVERT_SIGNATURE_OFFSET = 36
ADVERT_FLAGS_OFFSET = 100
ADVERT_MIN_SIZE = 101

# TimeSync constants
CONSENSUS_WINDOW_MS = 3_600_000  # 1 hour
MAX_TIMESTAMP_DIFF = 300  # 5 minutes


class AdvertInfo:
    """Parsed ADVERT information."""
    __slots__ = ('pub_key_hash', 'public_key', 'timestamp', 'flags', 'has_location',
                 'latitude', 'longitude', 'has_name', 'name',
                 'is_repeater', 'is_chat_node')

    def __init__(self):
        self.pub_key_hash: int = 0
        self.public_key: bytes = b''
        self.timestamp: int = 0
        self.flags: int = 0
        self.has_location: bool = False
        self.latitude: int = 0
        self.longitude: int = 0
        self.has_name: bool = False
        self.name: str = ""
        self.is_repeater: bool = False
        self.is_chat_node: bool = False


class TimeSync:
    """Time synchronization from received ADVERTs. Port of firmware TimeSync."""

    def __init__(self, clock):
        self._clock = clock
        self.base_timestamp: int = 0
        self.base_millis: int = 0
        self.synchronized: bool = False
        self.pending_timestamp: int = 0
        self.pending_millis: int = 0

    def sync_from_advert(self, unix_time: int) -> int:
        """
        Sync time from ADVERT timestamp.
        Returns: 0=no change, 1=first sync, 2=re-sync consensus.
        """
        if unix_time < 1_577_836_800 or unix_time > 4_102_444_800:
            return 0

        now = self._clock.millis()

        if not self.synchronized:
            self.base_timestamp = unix_time
            self.base_millis = now
            self.synchronized = True
            self.pending_timestamp = 0
            self.pending_millis = 0
            return 1

        our_time = self.base_timestamp + ((now - self.base_millis) // 1000)
        diff = unix_time - our_time

        if abs(diff) < MAX_TIMESTAMP_DIFF:
            self.pending_timestamp = 0
            self.pending_millis = 0
            return 0

        if self.pending_timestamp > 0 and (now - self.pending_millis) < CONSENSUS_WINDOW_MS:
            pending_adjusted = self.pending_timestamp + ((now - self.pending_millis) // 1000)
            pending_diff = unix_time - pending_adjusted

            if abs(pending_diff) < MAX_TIMESTAMP_DIFF:
                avg_time = (unix_time + pending_adjusted) // 2
                self.base_timestamp = avg_time
                self.base_millis = now
                self.pending_timestamp = 0
                self.pending_millis = 0
                return 2

        self.pending_timestamp = unix_time
        self.pending_millis = now
        return 0

    def get_timestamp(self) -> int:
        if self.synchronized:
            elapsed = (self._clock.millis() - self.base_millis) // 1000
            return self.base_timestamp + elapsed
        return self._clock.millis() // 1000

    def is_synchronized(self) -> bool:
        return self.synchronized

    def set_time(self, unix_time: int):
        self.base_timestamp = unix_time
        self.base_millis = self._clock.millis()
        self.synchronized = True
        self.pending_timestamp = 0
        self.pending_millis = 0


def build_advert(identity: Identity, time_sync: TimeSync,
                 route_type: int = MC_ROUTE_FLOOD) -> MCPacket:
    """Build ADVERT packet - exact port of AdvertGenerator::build()."""
    pkt = MCPacket()
    pkt.set_header(route_type, MC_PAYLOAD_ADVERT, MC_PAYLOAD_VER_1)
    pkt.path = []

    payload = bytearray()

    # [0-31] Public Key
    payload.extend(identity.public_key)

    # [32-35] Timestamp (LE)
    timestamp = time_sync.get_timestamp()
    payload.extend(struct.pack('<I', timestamp))

    # Build appdata
    appdata = _build_appdata(identity)

    # Sign: pubkey + timestamp + appdata
    sign_data = identity.public_key + struct.pack('<I', timestamp) + appdata
    signature = identity.sign(sign_data)

    # [36-99] Signature
    payload.extend(signature)

    # [100+] Appdata
    payload.extend(appdata)

    pkt.payload = bytes(payload)
    return pkt


def _build_appdata(identity: Identity) -> bytes:
    """Build appdata: [flags][location?][name]."""
    buf = bytearray()
    buf.append(identity.flags)

    if identity.has_location():
        buf.extend(struct.pack('<i', identity.latitude))
        buf.extend(struct.pack('<i', identity.longitude))

    if identity.flags & MC_FLAG_HAS_NAME:
        name_bytes = identity.name.encode('utf-8')[:MC_NODE_NAME_MAX - 1]
        buf.extend(name_bytes)

    return bytes(buf)


def extract_timestamp(payload: bytes) -> int:
    """Extract timestamp from ADVERT payload."""
    if len(payload) < ADVERT_MIN_SIZE:
        return 0
    return struct.unpack_from('<I', payload, ADVERT_TIMESTAMP_OFFSET)[0]


def parse_advert(payload: bytes) -> AdvertInfo | None:
    """Parse ADVERT payload into AdvertInfo. Port of AdvertGenerator::parseAdvert()."""
    if len(payload) < ADVERT_MIN_SIZE:
        return None

    info = AdvertInfo()
    info.public_key = bytes(payload[ADVERT_PUBKEY_OFFSET:ADVERT_PUBKEY_OFFSET + MC_PUBLIC_KEY_SIZE])
    info.pub_key_hash = payload[ADVERT_PUBKEY_OFFSET]
    info.timestamp = struct.unpack_from('<I', payload, ADVERT_TIMESTAMP_OFFSET)[0]
    info.flags = payload[ADVERT_FLAGS_OFFSET]

    pos = ADVERT_FLAGS_OFFSET

    node_type = info.flags & MC_TYPE_MASK
    has_valid_flags = (info.flags & 0x80) != 0 and node_type <= 0x04

    if has_valid_flags:
        pos += 1  # skip flags byte
        info.is_repeater = (node_type == MC_TYPE_REPEATER)
        info.is_chat_node = (node_type == MC_TYPE_CHAT_NODE)
        info.has_location = (info.flags & MC_FLAG_HAS_LOCATION) != 0
        info.has_name = (info.flags & MC_FLAG_HAS_NAME) != 0

        if info.has_location and len(payload) >= pos + 8:
            info.latitude = struct.unpack_from('<i', payload, pos)[0]
            info.longitude = struct.unpack_from('<i', payload, pos + 4)[0]
            pos += 8
    else:
        info.flags = MC_TYPE_CHAT_NODE | MC_FLAG_HAS_NAME
        info.is_chat_node = True
        info.has_name = True

    if info.has_name and len(payload) > pos:
        name_len = min(len(payload) - pos, MC_NODE_NAME_MAX - 1)
        info.name = payload[pos:pos + name_len].decode('utf-8', errors='replace')
    else:
        info.name = f"Node-{info.pub_key_hash:02X}"

    return info
