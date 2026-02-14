"""
SimNode, SimRepeater, SimCompanion - the core simulation nodes.
Port of the logic in src/main.cpp.
"""

from __future__ import annotations
from sim.clock import VirtualClock
from sim.packet import (
    MCPacket, MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_ADVERT,
    MC_PAYLOAD_ANON_REQ, MC_PAYLOAD_REQUEST, MC_PAYLOAD_RESPONSE,
    MC_PAYLOAD_VER_1, MC_TYPE_REPEATER, MC_TYPE_CHAT_NODE,
    MC_FLAG_HAS_NAME, MC_MAX_PATH_SIZE, payload_type_name, route_type_name,
)
from sim.identity import Identity
from sim.advert import (
    TimeSync, build_advert, parse_advert, extract_timestamp, AdvertInfo,
)
from sim.config import (
    NodeConfig, Stats, SeenNodesTracker, PacketIdCache, TxQueue, RateLimiter,
    Mailbox, HEALTH_OFFLINE_MS,
    RATE_LIMIT_FORWARD_MAX, RATE_LIMIT_FORWARD_SECS,
    DEFAULT_ADVERT_INTERVAL_MS,
)

# Log tag prefixes (match firmware)
TAG_RX = "[R]"
TAG_FWD = "[F]"
TAG_PING = "[P]"
TAG_ADVERT = "[A]"
TAG_NODE = "[N]"
TAG_OK = "[OK]"
TAG_ERROR = "[E]"
TAG_INFO = "[I]"

ADVERT_AFTER_SYNC_MS = 5000  # 5s delay after time sync before sending ADVERT


class SimNode:
    """Base class for all simulated nodes."""

    def __init__(self, name: str, node_type: int, clock: VirtualClock):
        self.name = name
        self.node_type = node_type
        self.clock = clock

        self.identity = Identity(name)
        self.identity.flags = node_type | MC_FLAG_HAS_NAME

        self.time_sync = TimeSync(clock)
        self.seen_nodes = SeenNodesTracker()
        self.packet_cache = PacketIdCache()
        self.tx_queue = TxQueue()
        self.stats = Stats()

        self.log_buffer: list[tuple[int, str]] = []  # (ms, message) - drained by runner
        self.log_history: list[tuple[int, str]] = []  # persistent copy
        self.ping_counter: int = 0

        self._advert_interval_ms = DEFAULT_ADVERT_INTERVAL_MS
        self._last_advert_time: int = 0
        self._pending_advert_time: int = 0

    def _log(self, msg: str):
        entry = (self.clock.millis(), msg)
        self.log_buffer.append(entry)
        self.log_history.append(entry)

    # --- Packet reception dispatch ---

    def on_rx_packet(self, pkt: MCPacket, rssi: int, snr: int):
        """Process a received packet. Port of processReceivedPacket()."""
        pkt.rssi = rssi
        pkt.snr = snr
        pkt.rx_time = self.clock.millis()
        self.stats.rx_count += 1

        pt = pkt.payload_type

        if pt == MC_PAYLOAD_ADVERT:
            self._process_advert(pkt)
        elif pt == MC_PAYLOAD_PLAIN:
            self._process_plain(pkt)

        # Track nodes from path
        if pkt.path_len > 0:
            self.seen_nodes.update(
                pkt.path[0], rssi, snr, now_ms=self.clock.millis()
            )
            if pkt.path_len > 1:
                last_hop = pkt.path[-1]
                if last_hop != pkt.path[0]:
                    self.seen_nodes.update(last_hop, rssi, snr, now_ms=self.clock.millis())

    def _process_advert(self, pkt: MCPacket):
        """Process ADVERT packet."""
        self.stats.adv_rx_count += 1

        advert_time = extract_timestamp(pkt.payload)
        if advert_time > 0:
            sync_result = self.time_sync.sync_from_advert(advert_time)
            if sync_result == 1:
                self._log(f"{TAG_OK} Time sync {self.time_sync.get_timestamp()}")
                self._pending_advert_time = self.clock.millis() + ADVERT_AFTER_SYNC_MS
            elif sync_result == 2:
                self._log(f"{TAG_OK} Time resync {self.time_sync.get_timestamp()}")
                self._pending_advert_time = self.clock.millis() + ADVERT_AFTER_SYNC_MS

        info = parse_advert(pkt.payload)
        if info:
            self._log(f"{TAG_NODE} {info.name}"
                       f"{' R' if info.is_repeater else ''}"
                       f"{' C' if info.is_chat_node else ''}"
                       f" {info.pub_key_hash:02X}")
            is_new = self.seen_nodes.update(
                info.pub_key_hash, pkt.rssi, pkt.snr,
                name=info.name, now_ms=self.clock.millis()
            )
            if is_new:
                self._log(f"{TAG_NODE} New node")

    def _process_plain(self, pkt: MCPacket):
        """Process PLAIN packet - directed ping/pong/trace."""
        if pkt.payload_len < 4:
            return

        dest_hash = pkt.payload[0]
        src_hash = pkt.payload[1]
        marker = pkt.payload[2:4]

        my_hash = self.identity.hash

        if marker == b'DP' and dest_hash == my_hash:
            # Directed PING for us
            text = pkt.payload[4:].decode('utf-8', errors='replace') if pkt.payload_len > 4 else ""
            self._log(f"{TAG_PING} from {src_hash:02X} {text}")
            self._send_pong(src_hash, pkt)

        elif marker == b'PO' and dest_hash == my_hash:
            # PONG for us
            text = pkt.payload[4:].decode('utf-8', errors='replace') if pkt.payload_len > 4 else ""
            self._log(f"{TAG_PING} PONG {src_hash:02X} {text} rssi={pkt.rssi} "
                       f"snr={pkt.snr // 4}.{abs(pkt.snr % 4) * 25}dB p={pkt.path_len}")

        elif marker == b'DT' and dest_hash == my_hash:
            # Directed TRACE for us
            text = pkt.payload[4:].decode('utf-8', errors='replace') if pkt.payload_len > 4 else ""
            self._log(f"{TAG_PING} TRACE from {src_hash:02X} {text}")
            self._send_trace_response(src_hash, pkt)

        elif marker == b'TR' and dest_hash == my_hash:
            # Trace response for us
            text = pkt.payload[4:].decode('utf-8', errors='replace') if pkt.payload_len > 4 else ""
            self._log(f"{TAG_PING} TRACE {src_hash:02X} {text} rssi={pkt.rssi} "
                       f"snr={pkt.snr // 4}.{abs(pkt.snr % 4) * 25}dB p={pkt.path_len}")

    # --- TX helpers ---

    def send_advert(self, flood: bool = True):
        """Build and enqueue ADVERT."""
        route = MC_ROUTE_FLOOD if flood else 0x02  # DIRECT for zero-hop
        pkt = build_advert(self.identity, self.time_sync, route)

        pkt_id = pkt.get_packet_id()
        self.packet_cache.add_if_new(pkt_id)

        self.tx_queue.add(pkt)
        self.stats.tx_count += 1
        self.stats.adv_tx_count += 1
        self._last_advert_time = self.clock.millis()
        self._log(f"{TAG_ADVERT} {'flood' if flood else 'local'} {self.identity.name}")

    def send_directed_ping(self, target_hash: int):
        """Send directed ping DP."""
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [self.identity.hash]

        self.ping_counter += 1
        text = f"#{self.ping_counter} {self.identity.name}"
        pkt.payload = bytes([target_hash, self.identity.hash, ord('D'), ord('P')]) + text.encode()

        self._log(f"{TAG_PING} -> {target_hash:02X} #{self.ping_counter}")

        pkt_id = pkt.get_packet_id()
        self.packet_cache.add_if_new(pkt_id)
        self.tx_queue.add(pkt)
        self.stats.tx_count += 1

    def send_directed_trace(self, target_hash: int):
        """Send directed trace DT."""
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [self.identity.hash]

        self.ping_counter += 1
        text = f"#{self.ping_counter} {self.identity.name}"
        pkt.payload = bytes([target_hash, self.identity.hash, ord('D'), ord('T')]) + text.encode()

        self._log(f"{TAG_PING} ~> {target_hash:02X} #{self.ping_counter}")

        pkt_id = pkt.get_packet_id()
        self.packet_cache.add_if_new(pkt_id)
        self.tx_queue.add(pkt)
        self.stats.tx_count += 1

    def _send_pong(self, target_hash: int, rx_pkt: MCPacket):
        """Send PONG response."""
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [self.identity.hash]

        text = f"{self.identity.name} {rx_pkt.rssi}"
        pkt.payload = bytes([target_hash, self.identity.hash, ord('P'), ord('O')]) + text.encode()

        self._log(f"{TAG_PING} PONG -> {target_hash:02X}")

        pkt_id = pkt.get_packet_id()
        self.packet_cache.add_if_new(pkt_id)
        self.tx_queue.add(pkt)
        self.stats.tx_count += 1

    def _send_trace_response(self, target_hash: int, rx_pkt: MCPacket):
        """Send trace response TR."""
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [self.identity.hash]

        text = f"{self.identity.name} {rx_pkt.rssi} {rx_pkt.path_len}"
        pkt.payload = bytes([target_hash, self.identity.hash, ord('T'), ord('R')]) + text.encode()

        self._log(f"{TAG_PING} TR -> {target_hash:02X}")

        pkt_id = pkt.get_packet_id()
        self.packet_cache.add_if_new(pkt_id)
        self.tx_queue.add(pkt)
        self.stats.tx_count += 1

    # --- Tick ---

    def tick(self) -> list[MCPacket]:
        """Advance one tick. Returns packets to transmit."""
        now = self.clock.millis()

        # Check pending advert after time sync
        if self._pending_advert_time > 0 and now >= self._pending_advert_time:
            self._pending_advert_time = 0
            self.send_advert(True)

        # Regular beacon
        if (self.time_sync.is_synchronized() and
                (now - self._last_advert_time) >= self._advert_interval_ms):
            self.send_advert(True)

        # Drain TX queue
        packets = []
        while self.tx_queue.count > 0:
            pkt = self.tx_queue.pop()
            if pkt:
                packets.append(pkt)
        return packets


class SimRepeater(SimNode):
    """Repeater node (type 0x02) - forwards packets."""

    def __init__(self, name: str, clock: VirtualClock):
        super().__init__(name, MC_TYPE_REPEATER, clock)
        self.config = NodeConfig()
        self.forward_limiter = RateLimiter(RATE_LIMIT_FORWARD_MAX, RATE_LIMIT_FORWARD_SECS)
        self.neighbours: list[dict] = []  # [{hash, rssi, snr, last_seen}]
        self.mailbox = Mailbox()

    def on_rx_packet(self, pkt: MCPacket, rssi: int, snr: int):
        """Process received packet + forwarding logic."""
        # First do base processing
        super().on_rx_packet(pkt, rssi, snr)

        # Track neighbours from 0-hop ADVERTs
        if pkt.payload_type == MC_PAYLOAD_ADVERT and pkt.path_len == 0:
            info = parse_advert(pkt.payload)
            if info and info.is_repeater:
                self._update_neighbour(info.pub_key_hash, rssi, snr)

        # Store-and-forward: deliver pending messages when node comes back
        if pkt.payload_type == MC_PAYLOAD_ADVERT:
            info = parse_advert(pkt.payload)
            if info and self.mailbox.count_for(info.pub_key_hash) > 0:
                while True:
                    data = self.mailbox.pop_for(info.pub_key_hash)
                    if data is None:
                        break
                    fwd_pkt = MCPacket()
                    if fwd_pkt.deserialize(data):
                        self.tx_queue.add(fwd_pkt)
                        self._log(f"{TAG_INFO} Mbox fwd {info.pub_key_hash:02X}")

        # Store-and-forward: save packets for offline nodes
        pt = pkt.payload_type
        if (pkt.payload_len >= 2 and
                pt in (MC_PAYLOAD_REQUEST, MC_PAYLOAD_RESPONSE,
                       MC_PAYLOAD_PLAIN, MC_PAYLOAD_ANON_REQ)):
            dest_hash = pkt.payload[0]
            if dest_hash != self.identity.hash and dest_hash != 0:
                sn = self.seen_nodes.get_by_hash(dest_hash)
                if (sn and sn.pkt_count >= 2 and
                        (self.clock.millis() - sn.last_seen) > HEALTH_OFFLINE_MS):
                    if self.time_sync.is_synchronized():
                        serialized = pkt.serialize()
                        if self.mailbox.store(dest_hash, serialized,
                                              self.time_sync.get_timestamp()):
                            self._log(f"{TAG_INFO} Mbox store {dest_hash:02X}")

        # Forwarding logic
        if self._should_forward(pkt):
            now_secs = self.clock.millis() // 1000
            if not self.forward_limiter.allow(now_secs):
                self._log(f"{TAG_FWD} Rate lim")
                return

            fwd_pkt = pkt.copy()
            if fwd_pkt.is_direct():
                # DIRECT: remove ourselves from path[0] (peel)
                fwd_pkt.path = fwd_pkt.path[1:]
                self._log(f"{TAG_FWD} Direct p={fwd_pkt.path_len}")
            else:
                # FLOOD: add our hash to path
                fwd_pkt.path.append(self.identity.hash)
            self.tx_queue.add(fwd_pkt)
            self.stats.fwd_count += 1
            self._log(f"{TAG_FWD} Q p={fwd_pkt.path_len}")

    def _should_forward(self, pkt: MCPacket) -> bool:
        """Port of shouldForward(). Supports FLOOD and DIRECT routing."""
        is_flood = pkt.is_flood()
        is_direct = pkt.is_direct()

        if not is_flood and not is_direct:
            return False

        # DIRECT routing: check if we are the next hop (path[0] == our hash)
        if is_direct:
            if pkt.path_len == 0:
                return False
            if pkt.path[0] != self.identity.hash:
                return False

        # Don't forward packets addressed to us
        pt = pkt.payload_type
        if pt in (MC_PAYLOAD_ANON_REQ, MC_PAYLOAD_REQUEST, MC_PAYLOAD_RESPONSE):
            if pkt.payload_len > 0 and pkt.payload[0] == self.identity.hash:
                return False

        # Check packet ID cache (deduplication)
        pkt_id = pkt.get_packet_id()
        if not self.packet_cache.add_if_new(pkt_id):
            return False

        # FLOOD: loop prevention and path length check
        if is_flood:
            if self.identity.hash in pkt.path:
                return False
            if pkt.path_len >= MC_MAX_PATH_SIZE - 1:
                return False

        return True

    def _update_neighbour(self, hash_val: int, rssi: int, snr: int):
        for n in self.neighbours:
            if n['hash'] == hash_val:
                n['rssi'] = rssi
                n['snr'] = snr
                n['last_seen'] = self.clock.millis()
                return
        self.neighbours.append({
            'hash': hash_val, 'rssi': rssi, 'snr': snr,
            'last_seen': self.clock.millis()
        })

    def process_command(self, cmd: str) -> str:
        """Process CLI command. Port of processCommand()."""
        cmd = cmd.strip()
        if not cmd:
            return ""

        parts = cmd.split()
        command = parts[0].lower()

        if command == "status":
            return self._cmd_status()
        elif command == "stats":
            return self._cmd_stats()
        elif command == "nodes":
            return self._cmd_nodes()
        elif command == "ping" and len(parts) > 1:
            return self._cmd_ping(parts[1])
        elif command == "trace" and len(parts) > 1:
            return self._cmd_trace(parts[1])
        elif command == "advert":
            self.send_advert(True)
            return f"{TAG_ADVERT} sent"
        elif command == "help":
            return "status stats nodes ping <hash> trace <hash> advert help"
        else:
            return f"Unknown: {cmd}"

    def _cmd_status(self) -> str:
        ts = self.time_sync.get_timestamp()
        synced = "yes" if self.time_sync.is_synchronized() else "no"
        return (f"{self.identity.name} {self.identity.hash:02X}\n"
                f"Time: {ts} sync={synced}\n"
                f"RX:{self.stats.rx_count} TX:{self.stats.tx_count} "
                f"FWD:{self.stats.fwd_count}")

    def _cmd_stats(self) -> str:
        return (f"RX:{self.stats.rx_count} TX:{self.stats.tx_count} "
                f"FWD:{self.stats.fwd_count} ERR:{self.stats.err_count}\n"
                f"ADV TX:{self.stats.adv_tx_count} RX:{self.stats.adv_rx_count}\n"
                f"Nodes:{len(self.seen_nodes.nodes)} Nbr:{len(self.neighbours)}")

    def _cmd_nodes(self) -> str:
        if not self.seen_nodes.nodes:
            return "No nodes seen"
        lines = []
        for n in self.seen_nodes.nodes:
            lines.append(f"  {n.hash:02X} {n.name or '?':12s} rssi={n.last_rssi} pkt={n.pkt_count}")
        return "\n".join(lines)

    def _cmd_ping(self, target: str) -> str:
        try:
            h = int(target, 16) & 0xFF
        except ValueError:
            return f"{TAG_ERROR} Invalid hash"
        if h == 0:
            return f"{TAG_ERROR} Invalid hash 0"
        self.send_directed_ping(h)
        return f"{TAG_PING} -> {h:02X}"

    def _cmd_trace(self, target: str) -> str:
        try:
            h = int(target, 16) & 0xFF
        except ValueError:
            return f"{TAG_ERROR} Invalid hash"
        if h == 0:
            return f"{TAG_ERROR} Invalid hash 0"
        self.send_directed_trace(h)
        return f"{TAG_PING} ~> {h:02X}"


class SimCompanion(SimNode):
    """Companion/client node (type 0x01) - does NOT forward packets."""

    def __init__(self, name: str, clock: VirtualClock):
        super().__init__(name, MC_TYPE_CHAT_NODE, clock)

    def on_rx_packet(self, pkt: MCPacket, rssi: int, snr: int):
        """Process received packet - NO forwarding."""
        super().on_rx_packet(pkt, rssi, snr)
        # Companions do not forward

    def process_command(self, cmd: str) -> str:
        """Limited CLI for companion."""
        cmd = cmd.strip()
        parts = cmd.split()
        if not parts:
            return ""
        command = parts[0].lower()

        if command == "ping" and len(parts) > 1:
            try:
                h = int(parts[1], 16) & 0xFF
            except ValueError:
                return f"{TAG_ERROR} Invalid hash"
            self.send_directed_ping(h)
            return f"{TAG_PING} -> {h:02X}"
        elif command == "trace" and len(parts) > 1:
            try:
                h = int(parts[1], 16) & 0xFF
            except ValueError:
                return f"{TAG_ERROR} Invalid hash"
            self.send_directed_trace(h)
            return f"{TAG_PING} ~> {h:02X}"
        elif command == "advert":
            self.send_advert(True)
            return f"{TAG_ADVERT} sent"
        elif command == "status":
            return f"{self.identity.name} {self.identity.hash:02X} (companion)"
        elif command == "help":
            return "status ping <hash> trace <hash> advert help"
        else:
            return f"Unknown: {cmd}"
