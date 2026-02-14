"""
MeshCore Packet - exact port of src/mesh/Packet.h

Wire format: [header:1][pathLen:1][path:0-64][payload:0-180]
Header byte: (route & 0x03) | ((type & 0x0F) << 2) | ((ver & 0x03) << 6)
"""

from __future__ import annotations

# Maximum sizes
MC_MAX_PACKET_SIZE = 255
MC_MAX_PATH_SIZE = 64
MC_MAX_PAYLOAD_SIZE = 180

# Route types (2 bits)
MC_ROUTE_TRANSPORT_FLOOD = 0x00
MC_ROUTE_FLOOD = 0x01
MC_ROUTE_DIRECT = 0x02
MC_ROUTE_TRANSPORT_DIRECT = 0x03

# Payload types (4 bits)
MC_PAYLOAD_REQUEST = 0x00
MC_PAYLOAD_RESPONSE = 0x01
MC_PAYLOAD_PLAIN = 0x02
MC_PAYLOAD_ACK = 0x03
MC_PAYLOAD_ADVERT = 0x04
MC_PAYLOAD_GROUP_TEXT = 0x05
MC_PAYLOAD_GROUP_DATA = 0x06
MC_PAYLOAD_ANON_REQ = 0x07
MC_PAYLOAD_PATH_RETURN = 0x08
MC_PAYLOAD_PATH_TRACE = 0x09
MC_PAYLOAD_MULTIPART = 0x0A
MC_PAYLOAD_CONTROL = 0x0B
MC_PAYLOAD_RAW = 0x0F

# Payload version
MC_PAYLOAD_VER_1 = 0x00

# Header bit masks
MC_HEADER_ROUTE_MASK = 0x03
MC_HEADER_ROUTE_SHIFT = 0
MC_HEADER_TYPE_MASK = 0x0F
MC_HEADER_TYPE_SHIFT = 2
MC_HEADER_VER_MASK = 0x03
MC_HEADER_VER_SHIFT = 6

# Node types
MC_TYPE_CHAT_NODE = 0x01
MC_TYPE_REPEATER = 0x02
MC_TYPE_ROOM_SERVER = 0x03
MC_TYPE_SENSOR = 0x04
MC_TYPE_MASK = 0x0F

# Flags
MC_FLAG_HAS_LOCATION = 0x10
MC_FLAG_HAS_NAME = 0x80


def make_header(route: int, payload_type: int, version: int = 0) -> int:
    return ((route & MC_HEADER_ROUTE_MASK) << MC_HEADER_ROUTE_SHIFT |
            (payload_type & MC_HEADER_TYPE_MASK) << MC_HEADER_TYPE_SHIFT |
            (version & MC_HEADER_VER_MASK) << MC_HEADER_VER_SHIFT)


def get_route_type(header: int) -> int:
    return (header >> MC_HEADER_ROUTE_SHIFT) & MC_HEADER_ROUTE_MASK


def get_payload_type(header: int) -> int:
    return (header >> MC_HEADER_TYPE_SHIFT) & MC_HEADER_TYPE_MASK


def get_version(header: int) -> int:
    return (header >> MC_HEADER_VER_SHIFT) & MC_HEADER_VER_MASK


def route_type_name(rt: int) -> str:
    return {
        MC_ROUTE_TRANSPORT_FLOOD: "TF",
        MC_ROUTE_FLOOD: "FL",
        MC_ROUTE_DIRECT: "DR",
        MC_ROUTE_TRANSPORT_DIRECT: "TD",
    }.get(rt, "??")


def payload_type_name(pt: int) -> str:
    return {
        MC_PAYLOAD_REQUEST: "REQ",
        MC_PAYLOAD_RESPONSE: "RSP",
        MC_PAYLOAD_PLAIN: "TXT",
        MC_PAYLOAD_ACK: "ACK",
        MC_PAYLOAD_ADVERT: "ADV",
        MC_PAYLOAD_GROUP_TEXT: "GTX",
        MC_PAYLOAD_GROUP_DATA: "GDT",
        MC_PAYLOAD_ANON_REQ: "ANO",
        MC_PAYLOAD_PATH_RETURN: "PTH",
        MC_PAYLOAD_PATH_TRACE: "TRC",
        MC_PAYLOAD_MULTIPART: "MUL",
        MC_PAYLOAD_CONTROL: "CTL",
        MC_PAYLOAD_RAW: "RAW",
    }.get(pt, "???")


class MCPacket:
    """Exact port of firmware MCPacket struct."""

    __slots__ = ('header', 'path', 'payload', 'rx_time', 'snr', 'rssi')

    def __init__(self):
        self.header: int = 0  # raw header byte
        self.path: list[int] = []  # list of uint8 hashes
        self.payload: bytes = b''
        # Metadata (not transmitted)
        self.rx_time: int = 0
        self.snr: int = 0  # SNR * 4
        self.rssi: int = 0

    @property
    def route_type(self) -> int:
        return get_route_type(self.header)

    @property
    def payload_type(self) -> int:
        return get_payload_type(self.header)

    @property
    def version(self) -> int:
        return get_version(self.header)

    @property
    def path_len(self) -> int:
        return len(self.path)

    @property
    def payload_len(self) -> int:
        return len(self.payload)

    def is_flood(self) -> bool:
        rt = self.route_type
        return rt == MC_ROUTE_FLOOD or rt == MC_ROUTE_TRANSPORT_FLOOD

    def is_direct(self) -> bool:
        rt = self.route_type
        return rt == MC_ROUTE_DIRECT or rt == MC_ROUTE_TRANSPORT_DIRECT

    def set_header(self, route: int, payload_type: int, version: int = 0):
        self.header = make_header(route, payload_type, version)

    def get_total_size(self) -> int:
        return 1 + 1 + len(self.path) + len(self.payload)

    def serialize(self) -> bytes:
        """Serialize to wire format: [header][pathLen][path...][payload...]"""
        buf = bytearray()
        buf.append(self.header)
        buf.append(len(self.path))
        buf.extend(self.path)
        buf.extend(self.payload)
        return bytes(buf)

    @staticmethod
    def deserialize(data: bytes) -> MCPacket | None:
        """Deserialize from wire format."""
        if len(data) < 2:
            return None

        pkt = MCPacket()
        pkt.header = data[0]
        path_len = data[1]

        if path_len > MC_MAX_PATH_SIZE:
            return None
        if 2 + path_len > len(data):
            return None

        pkt.path = list(data[2:2 + path_len])

        payload_start = 2 + path_len
        payload_data = data[payload_start:]
        if len(payload_data) > MC_MAX_PAYLOAD_SIZE:
            payload_data = payload_data[:MC_MAX_PAYLOAD_SIZE]
        pkt.payload = bytes(payload_data)

        return pkt

    def get_packet_id(self) -> int:
        """DJB2 hash for deduplication - exact port of firmware getPacketId()."""
        h = 5381
        h = ((h << 5) + h) ^ self.header
        h &= 0xFFFFFFFF
        for i in range(min(len(self.path), 8)):
            h = ((h << 5) + h) ^ self.path[i]
            h &= 0xFFFFFFFF
        for i in range(min(len(self.payload), 16)):
            h = ((h << 5) + h) ^ self.payload[i]
            h &= 0xFFFFFFFF
        return h

    def clear(self):
        self.header = 0
        self.path = []
        self.payload = b''
        self.rx_time = 0
        self.snr = 0
        self.rssi = 0

    def copy(self) -> MCPacket:
        pkt = MCPacket()
        pkt.header = self.header
        pkt.path = list(self.path)
        pkt.payload = self.payload
        pkt.rx_time = self.rx_time
        pkt.snr = self.snr
        pkt.rssi = self.rssi
        return pkt

    def __repr__(self):
        rt = route_type_name(self.route_type)
        pt = payload_type_name(self.payload_type)
        return f"MCPacket({rt} {pt} path={self.path} payload={len(self.payload)}B)"
