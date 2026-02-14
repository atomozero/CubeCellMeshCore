"""
SimRunner - orchestrator for multi-node mesh simulation.
"""

from __future__ import annotations
from sim.clock import VirtualClock
from sim.radio_env import RadioEnvironment
from sim.node import SimNode, SimRepeater, SimCompanion


class SimRunner:
    """Orchestrates the mesh simulation."""

    def __init__(self):
        self.clock = VirtualClock()
        self.radio = RadioEnvironment(self.clock)
        self.nodes: dict[str, SimNode] = {}
        self.events: list[dict] = []
        self.paused: bool = True
        self.speed: float = 1.0  # multiplier
        self.tick_ms: int = 10

    def add_repeater(self, name: str, x: float = 0.0, y: float = 0.0) -> SimRepeater:
        node = SimRepeater(name, self.clock)
        self.nodes[name] = node
        self.radio.add_node(node, x, y)
        return node

    def add_companion(self, name: str, x: float = 0.0, y: float = 0.0) -> SimCompanion:
        node = SimCompanion(name, self.clock)
        self.nodes[name] = node
        self.radio.add_node(node, x, y)
        return node

    def remove_node(self, name: str):
        self.nodes.pop(name, None)
        self.radio.remove_node(name)

    def set_link(self, node_a: str, node_b: str, rssi: int = -70, snr: int = 32):
        self.radio.set_link(node_a, node_b, rssi, snr)

    def remove_link(self, node_a: str, node_b: str):
        self.radio.remove_link(node_a, node_b)

    def inject_command(self, node_name: str, cmd: str) -> str:
        """Send CLI command to a node."""
        node = self.nodes.get(node_name)
        if not node:
            return f"Node '{node_name}' not found"
        if isinstance(node, (SimRepeater, SimCompanion)):
            return node.process_command(cmd)
        return "Node has no CLI"

    def run_step(self, tick_ms: int | None = None) -> list[dict]:
        """Run a single simulation step. Returns events."""
        dt = tick_ms or self.tick_ms
        self.radio.events.clear()
        step_events = []

        # Advance clock
        self.clock.advance(dt)

        # Tick all nodes - collect packets to transmit
        for name, node in self.nodes.items():
            packets = node.tick()
            for pkt in packets:
                self.radio.transmit(node, pkt)

            # Collect log events
            while node.log_buffer:
                ts, msg = node.log_buffer.pop(0)
                event = {'type': 'log', 'node': name, 'msg': msg, 'ts': ts}
                step_events.append(event)

        # Process radio (deliver packets whose airtime elapsed)
        self.radio.tick()

        # Collect radio events
        step_events.extend(self.radio.events)

        self.events.extend(step_events)
        return step_events

    def run(self, duration_ms: int, tick_ms: int = 10):
        """Run simulation for given duration."""
        end_time = self.clock.millis() + duration_ms
        while self.clock.millis() < end_time:
            self.run_step(tick_ms)

    def get_state(self) -> dict:
        """Get full simulation state snapshot for GUI."""
        nodes_state = {}
        for name, node in self.nodes.items():
            pos = self.radio.get_node_position(name)
            nodes_state[name] = {
                'name': name,
                'hash': f"{node.identity.hash:02X}",
                'type': 'repeater' if isinstance(node, SimRepeater) else 'companion',
                'x': pos[0] if pos else 0,
                'y': pos[1] if pos else 0,
                'stats': {
                    'rx': node.stats.rx_count,
                    'tx': node.stats.tx_count,
                    'fwd': node.stats.fwd_count,
                    'err': node.stats.err_count,
                    'adv_tx': node.stats.adv_tx_count,
                    'adv_rx': node.stats.adv_rx_count,
                },
                'time_synced': node.time_sync.is_synchronized(),
                'seen_nodes': [
                    {'hash': f"{n.hash:02X}", 'name': n.name, 'rssi': n.last_rssi,
                     'pkt_count': n.pkt_count}
                    for n in node.seen_nodes.nodes
                ],
                'flags': f"0x{node.identity.flags:02X}",
            }
            if isinstance(node, SimRepeater):
                nodes_state[name]['neighbours'] = node.neighbours

        links_state = []
        for (a, b), lc in self.radio.get_links().items():
            links_state.append({
                'node_a': a, 'node_b': b,
                'rssi': lc.rssi, 'snr': lc.snr, 'enabled': lc.enabled,
            })

        return {
            'time_ms': self.clock.millis(),
            'nodes': nodes_state,
            'links': links_state,
            'paused': self.paused,
            'speed': self.speed,
        }

    def reset(self):
        """Reset simulation."""
        self.clock.reset()
        self.nodes.clear()
        self.radio = RadioEnvironment(self.clock)
        self.events.clear()
        self.paused = True

    def print_topology(self):
        """Print topology to console."""
        print(f"\n=== Topology ({len(self.nodes)} nodes) ===")
        for name, node in self.nodes.items():
            pos = self.radio.get_node_position(name)
            ntype = "R" if isinstance(node, SimRepeater) else "C"
            print(f"  [{ntype}] {name} ({node.identity.hash:02X}) "
                  f"pos=({pos[0]:.0f},{pos[1]:.0f})")
        print("Links:")
        for (a, b), lc in self.radio.get_links().items():
            print(f"  {a} <-> {b}  rssi={lc.rssi} snr={lc.snr // 4}.{abs(lc.snr % 4) * 25}dB")
        print()
