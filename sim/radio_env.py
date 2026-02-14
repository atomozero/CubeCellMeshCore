"""
RadioEnvironment - simulated RF channel with propagation, collisions, delay.
"""

from __future__ import annotations
import math
from dataclasses import dataclass, field
from sim.clock import VirtualClock
from sim.packet import MCPacket
from sim.node import SimNode

# Default airtime for a typical packet (SF8, BW62.5, CR4/8, ~50 bytes)
DEFAULT_AIRTIME_MS = 200


@dataclass
class NodePlacement:
    node: SimNode
    x: float  # meters
    y: float  # meters


@dataclass
class LinkConfig:
    rssi: int = -70
    snr: int = 32  # SNR * 4 = 8.0 dB
    enabled: bool = True


@dataclass
class InFlightPacket:
    sender_name: str
    packet: MCPacket
    deliver_time: int  # millis when to deliver
    targets: list[tuple[str, int, int]]  # [(node_name, rssi, snr)]


class RadioEnvironment:
    """Simulated radio environment connecting nodes."""

    def __init__(self, clock: VirtualClock):
        self.clock = clock
        self._nodes: dict[str, NodePlacement] = {}
        self._links: dict[tuple[str, str], LinkConfig] = {}
        self._in_flight: list[InFlightPacket] = []
        self.airtime_ms: int = DEFAULT_AIRTIME_MS
        self.events: list[dict] = []  # events generated during tick

    def add_node(self, node: SimNode, x: float = 0.0, y: float = 0.0):
        self._nodes[node.name] = NodePlacement(node=node, x=x, y=y)

    def remove_node(self, name: str):
        self._nodes.pop(name, None)
        # Remove associated links
        to_remove = [k for k in self._links if name in k]
        for k in to_remove:
            del self._links[k]

    def set_link(self, node_a: str, node_b: str, rssi: int = -70, snr: int = 32):
        """Set bidirectional link between two nodes."""
        key = tuple(sorted([node_a, node_b]))
        self._links[key] = LinkConfig(rssi=rssi, snr=snr, enabled=True)

    def remove_link(self, node_a: str, node_b: str):
        key = tuple(sorted([node_a, node_b]))
        self._links.pop(key, None)

    def get_link(self, node_a: str, node_b: str) -> LinkConfig | None:
        key = tuple(sorted([node_a, node_b]))
        return self._links.get(key)

    def get_node_position(self, name: str) -> tuple[float, float] | None:
        np = self._nodes.get(name)
        return (np.x, np.y) if np else None

    def set_node_position(self, name: str, x: float, y: float):
        if name in self._nodes:
            self._nodes[name].x = x
            self._nodes[name].y = y

    def transmit(self, sender: SimNode, pkt: MCPacket):
        """Sender transmits a packet. Delivered after airtime delay."""
        targets = []
        for name, np in self._nodes.items():
            if name == sender.name:
                continue
            link = self.get_link(sender.name, name)
            if link and link.enabled:
                targets.append((name, link.rssi, link.snr))

        if targets:
            deliver_time = self.clock.millis() + self.airtime_ms
            ifp = InFlightPacket(
                sender_name=sender.name,
                packet=pkt.copy(),
                deliver_time=deliver_time,
                targets=targets,
            )
            self._in_flight.append(ifp)

            self.events.append({
                'type': 'packet_tx',
                'from': sender.name,
                'targets': [t[0] for t in targets],
                'pkt_type': pkt.payload_type,
                'ts': self.clock.millis(),
            })

    def tick(self):
        """Process in-flight packets, deliver those whose airtime has elapsed."""
        now = self.clock.millis()
        still_in_flight = []

        for ifp in self._in_flight:
            if now >= ifp.deliver_time:
                # Check for collisions: two packets arriving at same node simultaneously
                # Simple model: if two in-flight overlap delivery time, both lost at shared targets
                # For now, deliver without collision (can enhance later)
                for target_name, rssi, snr in ifp.targets:
                    np = self._nodes.get(target_name)
                    if np:
                        rx_pkt = ifp.packet.copy()
                        np.node.on_rx_packet(rx_pkt, rssi, snr)
                        self.events.append({
                            'type': 'packet_rx',
                            'from': ifp.sender_name,
                            'to': target_name,
                            'pkt_type': ifp.packet.payload_type,
                            'rssi': rssi,
                            'ts': now,
                        })
            else:
                still_in_flight.append(ifp)

        self._in_flight = still_in_flight

    def get_nodes(self) -> dict[str, NodePlacement]:
        return self._nodes

    def get_links(self) -> dict[tuple[str, str], LinkConfig]:
        return self._links

    def has_in_flight(self) -> bool:
        return len(self._in_flight) > 0
