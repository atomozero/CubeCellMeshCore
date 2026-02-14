#!/usr/bin/env python3
"""
CubeCellMeshCore Simulator - Entry point.

Usage:
  python sim/main.py web [--port PORT]    - Start GUI (default: localhost:8765)
  python sim/main.py scenario <name>      - Run scenario (linear, star)
  python sim/main.py interactive          - Interactive CLI
  python sim/main.py test                 - Run tests
"""

import argparse
import sys
import os

# Add parent directory to path so 'sim' package is importable
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run_scenario(name: str):
    from sim.runner import SimRunner

    runner = SimRunner()

    if name == "linear":
        _setup_linear(runner)
    elif name == "star":
        _setup_star(runner)
    elif name == "companion":
        _setup_companion(runner)
    else:
        print(f"Unknown scenario: {name}")
        print("Available: linear, star, companion")
        return

    runner.print_topology()

    # Give all nodes time sync via first advert
    print("--- Running scenario (60s sim time) ---\n")
    runner.run(60_000, tick_ms=10)

    # Print final stats
    print("\n=== Final Stats ===")
    for name, node in runner.nodes.items():
        s = node.stats
        print(f"  {name} ({node.identity.hash:02X}): "
              f"RX={s.rx_count} TX={s.tx_count} FWD={s.fwd_count}")

    # Print last logs
    print("\n=== Recent Events ===")
    for ev in runner.events[-30:]:
        if ev['type'] == 'log':
            print(f"  [{ev['ts']:>8}ms] {ev['node']:10s} {ev['msg']}")


def _setup_linear(runner):
    """Linear topology: A -- B -- C"""
    a = runner.add_repeater("RelayA", x=0, y=0)
    b = runner.add_repeater("RelayB", x=500, y=0)
    c = runner.add_repeater("RelayC", x=1000, y=0)

    runner.set_link("RelayA", "RelayB", rssi=-70, snr=32)
    runner.set_link("RelayB", "RelayC", rssi=-75, snr=28)
    # A and C cannot see each other directly

    # Give A a synced time so ADVERT works
    a.time_sync.set_time(1_700_000_000)

    # Trigger initial ping from A to C
    a.send_directed_ping(c.identity.hash)


def _setup_star(runner):
    """Star topology: B,C,D,E around center A"""
    a = runner.add_repeater("Center", x=500, y=500)
    b = runner.add_repeater("North", x=500, y=0)
    c = runner.add_repeater("East", x=1000, y=500)
    d = runner.add_repeater("South", x=500, y=1000)
    e = runner.add_repeater("West", x=0, y=500)

    for name in ["North", "East", "South", "West"]:
        runner.set_link("Center", name, rssi=-65, snr=36)

    a.time_sync.set_time(1_700_000_000)

    # Ping from North to South (must go through Center)
    b.send_directed_ping(d.identity.hash)


def _setup_companion(runner):
    """Companion scenario: Comp1 -- RepA -- RepB -- Comp2"""
    c1 = runner.add_companion("Comp1", x=0, y=0)
    ra = runner.add_repeater("RepA", x=300, y=0)
    rb = runner.add_repeater("RepB", x=700, y=0)
    c2 = runner.add_companion("Comp2", x=1000, y=0)

    runner.set_link("Comp1", "RepA", rssi=-65, snr=36)
    runner.set_link("RepA", "RepB", rssi=-70, snr=32)
    runner.set_link("RepB", "Comp2", rssi=-65, snr=36)

    ra.time_sync.set_time(1_700_000_000)

    c1.send_directed_ping(c2.identity.hash)


def run_interactive():
    from sim.runner import SimRunner

    runner = SimRunner()
    _setup_linear(runner)
    runner.print_topology()

    print("Interactive mode. Commands:")
    print("  step [ms]          - advance simulation")
    print("  cmd <node> <cmd>   - send CLI command to node")
    print("  state              - print state")
    print("  quit               - exit")
    print()

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not line:
            continue

        parts = line.split(maxsplit=2)
        cmd = parts[0].lower()

        if cmd == "quit" or cmd == "exit":
            break
        elif cmd == "step":
            ms = int(parts[1]) if len(parts) > 1 else 1000
            events = []
            end = runner.clock.millis() + ms
            while runner.clock.millis() < end:
                events.extend(runner.run_step(10))
            for ev in events:
                if ev['type'] == 'log':
                    print(f"  [{ev['ts']:>8}ms] {ev['node']:10s} {ev['msg']}")
        elif cmd == "cmd" and len(parts) >= 3:
            result = runner.inject_command(parts[1], parts[2])
            print(result)
            # Also step to process
            for ev in runner.run_step(10):
                if ev['type'] == 'log':
                    print(f"  [{ev['ts']:>8}ms] {ev['node']:10s} {ev['msg']}")
        elif cmd == "state":
            import json
            state = runner.get_state()
            print(json.dumps(state, indent=2, default=str))
        else:
            print(f"Unknown command: {cmd}")


def run_web(port: int):
    try:
        import uvicorn
        from sim.web.app import create_app
        app = create_app()
        print(f"Starting CubeCellMeshCore Simulator at http://localhost:{port}")
        uvicorn.run(app, host="0.0.0.0", port=port, log_level="info")
    except ImportError as e:
        print(f"Missing dependency: {e}")
        print("Install with: pip install -r sim/requirements.txt")
        sys.exit(1)


def run_tests():
    import subprocess
    result = subprocess.run(
        [sys.executable, "-m", "pytest", "sim/tests/", "-v"],
        cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )
    sys.exit(result.returncode)


def main():
    parser = argparse.ArgumentParser(description="CubeCellMeshCore Simulator")
    sub = parser.add_subparsers(dest="command")

    web_p = sub.add_parser("web", help="Start GUI web server")
    web_p.add_argument("--port", type=int, default=8765)

    sc_p = sub.add_parser("scenario", help="Run a scenario")
    sc_p.add_argument("name", help="Scenario name (linear, star, companion)")

    sub.add_parser("interactive", help="Interactive CLI mode")
    sub.add_parser("test", help="Run tests")

    args = parser.parse_args()

    if args.command == "web":
        run_web(args.port)
    elif args.command == "scenario":
        run_scenario(args.name)
    elif args.command == "interactive":
        run_interactive()
    elif args.command == "test":
        run_tests()
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
