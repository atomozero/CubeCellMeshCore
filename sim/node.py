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

# Circuit breaker constants
CB_SNR_THRESHOLD = -40   # SNR*4 = -10dB
CB_TIMEOUT_MS = 300000   # 5 min → half-open
CB_STATE_CLOSED = 0
CB_STATE_OPEN = 1
CB_STATE_HALF_OPEN = 2

# Adaptive TX power constants
ADAPTIVE_TX_HIGH_SNR = 40    # SNR*4 = +10dB → reduce power
ADAPTIVE_TX_LOW_SNR = -20    # SNR*4 = -5dB → increase power
ADAPTIVE_TX_STEP = 2         # dBm per step
ADAPTIVE_TX_MIN_POWER = 5    # dBm floor
DEFAULT_TX_POWER = 14        # EU default max

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

# SNR adaptive delay constants (MeshCore-style)
MC_MIN_RSSI_FORWARD = -120  # dBm minimum RSSI to forward a packet

# Delay multipliers x1000, index 0 = worst SNR (-20dB), 10 = best (+15dB)
SNR_DELAY_TABLE = [1293, 1105, 936, 783, 645, 521, 410, 310, 220, 139, 65]


def calc_snr_score(snr: int) -> int:
    """Map SNR (in 0.25dB units, i.e. SNR*4) to index [0-10].
    -20dB (*4=-80) -> 0, +15dB (*4=60) -> 10."""
    clamped = max(-80, min(60, snr))
    return (clamped + 80) * 10 // 140


def calc_rx_delay(score_idx: int, airtime_ms: int) -> int:
    """Calculate RX delay from SNR score and airtime.
    Better SNR = higher index = shorter delay."""
    idx = max(0, min(10, score_idx))
    return SNR_DELAY_TABLE[idx] * airtime_ms // 1000


def calc_tx_jitter(airtime_ms: int) -> int:
    """Calculate random TX jitter: 0-6 slots of 2x airtime.
    Returns max possible jitter (deterministic for testing)."""
    import random as _rng
    slot_time = airtime_ms * 2
    return _rng.randint(0, 6) * slot_time


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
        self.neighbours: list[dict] = []  # [{hash, rssi, snr, last_seen, cb_state}]
        self.mailbox = Mailbox()

        # Quiet Hours
        self.quiet_start_hour: int = 0xFF  # disabled
        self.quiet_end_hour: int = 0
        self.quiet_forward_max: int = 30
        self._in_quiet_period: bool = False
        self._last_quiet_eval: int = 0

        # Adaptive TX Power
        self.current_tx_power: int = DEFAULT_TX_POWER
        self.max_tx_power: int = DEFAULT_TX_POWER
        self.adaptive_tx_enabled: bool = False
        self._last_adaptive_eval: int = 0

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
            # Compute SNR adaptive delay (logged, not enforced in sim)
            airtime_est = 200  # default airtime estimate in ms
            if fwd_pkt.is_direct():
                # Circuit breaker: check next hop before peel
                if fwd_pkt.path_len >= 2:
                    next_hop = fwd_pkt.path[1]
                    if self._is_circuit_open(next_hop):
                        self._log(f"{TAG_FWD} CB {next_hop:02X}")
                        return
                # DIRECT: remove ourselves from path[0] (peel)
                fwd_pkt.path = fwd_pkt.path[1:]
                fwd_delay = calc_tx_jitter(airtime_est) // 2
                self._log(f"{TAG_FWD} Direct p={fwd_pkt.path_len} d={fwd_delay}ms")
            else:
                # FLOOD: add our hash to path
                fwd_pkt.path.append(self.identity.hash)
                score = calc_snr_score(pkt.snr)
                fwd_delay = calc_rx_delay(score, airtime_est) + calc_tx_jitter(airtime_est)
                self._log(f"{TAG_FWD} Flood p={fwd_pkt.path_len} snr={score} d={fwd_delay}ms")
            self.tx_queue.add(fwd_pkt)
            self.stats.fwd_count += 1
            self._log(f"{TAG_FWD} Q p={fwd_pkt.path_len}")

    def _should_forward(self, pkt: MCPacket) -> bool:
        """Port of shouldForward(). Supports FLOOD and DIRECT routing."""
        is_flood = pkt.is_flood()
        is_direct = pkt.is_direct()

        if not is_flood and not is_direct:
            return False

        # RSSI threshold: don't forward packets with very weak signal
        if pkt.rssi < MC_MIN_RSSI_FORWARD:
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
                # Circuit breaker: update state based on SNR
                if snr < CB_SNR_THRESHOLD:
                    if n.get('cb_state', CB_STATE_CLOSED) == CB_STATE_CLOSED:
                        n['cb_state'] = CB_STATE_OPEN
                elif n.get('cb_state', CB_STATE_CLOSED) != CB_STATE_CLOSED:
                    n['cb_state'] = CB_STATE_CLOSED  # good SNR → close
                return
        self.neighbours.append({
            'hash': hash_val, 'rssi': rssi, 'snr': snr,
            'last_seen': self.clock.millis(), 'cb_state': CB_STATE_CLOSED,
        })

    # --- Circuit Breaker ---

    def _is_circuit_open(self, hash_val: int) -> bool:
        for n in self.neighbours:
            if n['hash'] == hash_val:
                return n.get('cb_state', CB_STATE_CLOSED) == CB_STATE_OPEN
        return False

    def get_circuit_breaker_count(self) -> int:
        return sum(1 for n in self.neighbours
                   if n.get('cb_state', CB_STATE_CLOSED) == CB_STATE_OPEN)

    def _tick_circuit_breakers(self):
        now = self.clock.millis()
        for n in self.neighbours:
            if (n.get('cb_state', CB_STATE_CLOSED) == CB_STATE_OPEN and
                    (now - n['last_seen']) > CB_TIMEOUT_MS):
                n['cb_state'] = CB_STATE_HALF_OPEN

    # --- Quiet Hours ---

    def set_quiet_hours(self, start: int, end: int, max_fwd: int = 30):
        self.quiet_start_hour = start
        self.quiet_end_hour = end
        self.quiet_forward_max = max_fwd

    def disable_quiet_hours(self):
        self.quiet_start_hour = 0xFF
        self.quiet_end_hour = 0
        self._in_quiet_period = False
        self.forward_limiter.max_count = RATE_LIMIT_FORWARD_MAX

    def is_quiet_hours_enabled(self) -> bool:
        return self.quiet_start_hour != 0xFF

    def _evaluate_quiet_hours(self, current_hour: int):
        if self.quiet_start_hour == 0xFF:
            return
        if self.quiet_start_hour <= self.quiet_end_hour:
            should_be_quiet = (current_hour >= self.quiet_start_hour and
                               current_hour < self.quiet_end_hour)
        else:
            # Overnight wrap (e.g., 22-06)
            should_be_quiet = (current_hour >= self.quiet_start_hour or
                               current_hour < self.quiet_end_hour)
        if should_be_quiet != self._in_quiet_period:
            self._in_quiet_period = should_be_quiet
            if should_be_quiet:
                self.forward_limiter.max_count = self.quiet_forward_max
            else:
                self.forward_limiter.max_count = RATE_LIMIT_FORWARD_MAX

    # --- Adaptive TX Power ---

    def evaluate_adaptive_tx_power(self) -> int:
        """Returns new power if changed, -1 otherwise."""
        if not self.adaptive_tx_enabled:
            return -1
        if not self.neighbours:
            return -1
        avg_snr = sum(n['snr'] for n in self.neighbours) // len(self.neighbours)
        old_power = self.current_tx_power
        if avg_snr > ADAPTIVE_TX_HIGH_SNR:
            self.current_tx_power -= ADAPTIVE_TX_STEP
            if self.current_tx_power < ADAPTIVE_TX_MIN_POWER:
                self.current_tx_power = ADAPTIVE_TX_MIN_POWER
        elif avg_snr < ADAPTIVE_TX_LOW_SNR:
            self.current_tx_power += ADAPTIVE_TX_STEP
            if self.current_tx_power > self.max_tx_power:
                self.current_tx_power = self.max_tx_power
        return self.current_tx_power if self.current_tx_power != old_power else -1

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


    def tick(self) -> list[MCPacket]:
        """Advance one tick with periodic maintenance."""
        now = self.clock.millis()

        # Periodic (every 60s): quiet hours, circuit breakers, adaptive TX
        if now - self._last_quiet_eval >= 60000:
            self._last_quiet_eval = now
            # Quiet hours
            if self.is_quiet_hours_enabled() and self.time_sync.is_synchronized():
                ts = self.time_sync.get_timestamp()
                hour = (ts % 86400) // 3600
                self._evaluate_quiet_hours(hour)
            # Circuit breaker timeouts
            self._tick_circuit_breakers()
            # Adaptive TX power
            new_power = self.evaluate_adaptive_tx_power()
            if new_power >= 0:
                self._log(f"{TAG_INFO} TxP:{new_power}dBm")

        return super().tick()


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
