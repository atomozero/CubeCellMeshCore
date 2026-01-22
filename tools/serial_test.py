#!/usr/bin/env python3
"""
CubeCellMeshCore Serial Test Script

Automated testing of firmware functionality via serial console.

Usage:
    python serial_test.py [port] [--full]

    port: Serial port (default: auto-detect)
    --full: Run full test suite including destructive tests

Example:
    python serial_test.py COM3
    python serial_test.py /dev/ttyUSB0 --full
"""

import serial
import serial.tools.list_ports
import time
import sys
import re
from dataclasses import dataclass
from typing import Optional, List, Tuple

# Test configuration
BAUD_RATE = 115200
TIMEOUT = 2.0
COMMAND_DELAY = 0.5

@dataclass
class TestResult:
    name: str
    command: str
    passed: bool
    expected: str
    actual: str
    details: str = ""

class SerialTester:
    def __init__(self, port: str):
        self.port = port
        self.ser = None
        self.results: List[TestResult] = []

    def connect(self) -> bool:
        """Connect to serial port"""
        try:
            self.ser = serial.Serial(self.port, BAUD_RATE, timeout=TIMEOUT)
            time.sleep(2)  # Wait for device to be ready
            self.ser.reset_input_buffer()
            print(f"[OK] Connected to {self.port}")
            return True
        except Exception as e:
            print(f"[ERROR] Failed to connect: {e}")
            return False

    def disconnect(self):
        """Close serial connection"""
        if self.ser:
            self.ser.close()

    def send_command(self, cmd: str, wait: float = COMMAND_DELAY) -> str:
        """Send command and return response"""
        if not self.ser:
            return ""

        self.ser.reset_input_buffer()
        self.ser.write(f"{cmd}\r\n".encode())
        time.sleep(wait)

        response = ""
        while self.ser.in_waiting:
            response += self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
            time.sleep(0.1)

        return response.strip()

    def test(self, name: str, command: str, expected_pattern: str,
             wait: float = COMMAND_DELAY) -> bool:
        """Run a single test"""
        print(f"  Testing: {name}...", end=" ")

        response = self.send_command(command, wait)

        # Check if expected pattern is in response
        if re.search(expected_pattern, response, re.IGNORECASE):
            print("[PASS]")
            self.results.append(TestResult(
                name=name,
                command=command,
                passed=True,
                expected=expected_pattern,
                actual=response[:100]
            ))
            return True
        else:
            print("[FAIL]")
            print(f"    Expected pattern: {expected_pattern}")
            print(f"    Got: {response[:200]}")
            self.results.append(TestResult(
                name=name,
                command=command,
                passed=False,
                expected=expected_pattern,
                actual=response[:200]
            ))
            return False

    def run_basic_tests(self):
        """Run basic non-destructive tests"""
        print("\n=== Basic System Tests ===")

        # Help command
        self.test("Help command", "help", r"(status|stats|advert|Commands)")

        # Status
        self.test("Status check", "status", r"(FW:|Freq:|Hash:)")

        # Stats
        self.test("Statistics", "stats", r"(RX:|TX:|ADV)")

        # Identity
        self.test("Identity", "identity", r"(PubKey:|[0-9A-Fa-f]{32})")

        # Telemetry
        self.test("Telemetry", "telemetry", r"(Batt|mV|Temp|Uptime)")

        # Nodes list
        self.test("Nodes list", "nodes", r"(nodes|Seen|No nodes|Hash)")

        # Contacts
        self.test("Contacts", "contacts", r"(contacts|Contact|No contacts|empty)")

        # Time
        self.test("Time check", "time", r"(Time|sync|timestamp|\d+)")

        # Sleep status
        self.test("Sleep status", "sleep", r"(sleep|Deep|ON|OFF)")

        # RX Boost status
        self.test("RX Boost status", "rxboost", r"(boost|RX|ON|OFF)")

    def run_radio_tests(self):
        """Run radio/network tests"""
        print("\n=== Radio Tests ===")

        # Get initial stats
        initial_response = self.send_command("stats")
        initial_tx = 0
        match = re.search(r'TX:(\d+)', initial_response)
        if match:
            initial_tx = int(match.group(1))

        # Send ADVERT
        self.test("Send ADVERT", "advert", r"(ADVERT|sent|TX)", wait=1.0)

        # Check TX counter increased
        time.sleep(0.5)
        after_response = self.send_command("stats")
        match = re.search(r'TX:(\d+)', after_response)
        if match:
            after_tx = int(match.group(1))
            if after_tx > initial_tx:
                print(f"  TX counter: {initial_tx} -> {after_tx} [PASS]")
                self.results.append(TestResult(
                    name="TX counter increment",
                    command="stats",
                    passed=True,
                    expected=f"TX > {initial_tx}",
                    actual=f"TX = {after_tx}"
                ))
            else:
                print(f"  TX counter: {initial_tx} -> {after_tx} [FAIL]")
                self.results.append(TestResult(
                    name="TX counter increment",
                    command="stats",
                    passed=False,
                    expected=f"TX > {initial_tx}",
                    actual=f"TX = {after_tx}"
                ))

        # Neighbours
        self.test("Neighbours list", "neighbours", r"(neighbour|neighbor|direct|No |Hash)")

    def run_config_tests(self):
        """Run configuration tests"""
        print("\n=== Configuration Tests ===")

        # Location
        self.test("Location", "location", r"(location|Lat|Lon|not set|\d+\.\d+)")

        # Node type
        self.test("Node type info", "status", r"(repeater|chat|0x[0-9a-fA-F]+)")

        # Password display
        self.test("Password display", "passwd", r"(admin|guest|password|Admin:|Guest:)")

        # Alert status
        self.test("Alert status", "alert", r"(alert|enabled|disabled|destination|OFF|ON)")

    def run_full_tests(self):
        """Run full test suite including state-changing tests"""
        print("\n=== Full Tests (State-Changing) ===")
        print("WARNING: These tests modify device configuration!")

        # Test sleep toggle
        self.send_command("sleep off")
        time.sleep(0.3)
        self.test("Sleep off", "sleep", r"(OFF|disabled)")

        self.send_command("sleep on")
        time.sleep(0.3)
        self.test("Sleep on", "sleep", r"(ON|enabled)")

        # Test RX boost toggle
        self.send_command("rxboost off")
        time.sleep(0.3)
        self.test("RX Boost off", "rxboost", r"(OFF|disabled)")

        self.send_command("rxboost on")
        time.sleep(0.3)
        self.test("RX Boost on", "rxboost", r"(ON|enabled)")

        # Test name change
        original_name = ""
        response = self.send_command("name")
        match = re.search(r'Name:\s*(\S+)', response)
        if match:
            original_name = match.group(1)

        self.send_command("name TestNode123")
        time.sleep(0.3)
        self.test("Name change", "name", r"TestNode123")

        # Restore original name if we had one
        if original_name:
            self.send_command(f"name {original_name}")

        # Test save
        self.test("Save config", "save", r"(Saved|EEPROM|OK)")

    def print_summary(self):
        """Print test summary"""
        print("\n" + "="*50)
        print("TEST SUMMARY")
        print("="*50)

        passed = sum(1 for r in self.results if r.passed)
        failed = sum(1 for r in self.results if not r.passed)
        total = len(self.results)

        print(f"Passed: {passed}/{total}")
        print(f"Failed: {failed}/{total}")

        if failed > 0:
            print("\nFailed tests:")
            for r in self.results:
                if not r.passed:
                    print(f"  - {r.name}: expected '{r.expected}', got '{r.actual[:50]}...'")

        print("\n" + "="*50)
        return failed == 0


def find_serial_port() -> Optional[str]:
    """Auto-detect serial port"""
    ports = serial.tools.list_ports.comports()

    for port in ports:
        # Look for common CubeCell identifiers
        if any(x in port.description.lower() for x in ['cp210', 'ch340', 'usb', 'serial']):
            print(f"Found: {port.device} - {port.description}")
            return port.device

    if ports:
        print(f"Using first available port: {ports[0].device}")
        return ports[0].device

    return None


def main():
    print("="*50)
    print("CubeCellMeshCore Serial Test")
    print("="*50)

    # Parse arguments
    port = None
    full_test = False

    for arg in sys.argv[1:]:
        if arg == "--full":
            full_test = True
        elif not arg.startswith("-"):
            port = arg

    # Find port if not specified
    if not port:
        print("\nAuto-detecting serial port...")
        port = find_serial_port()
        if not port:
            print("[ERROR] No serial port found. Please specify port as argument.")
            print("Usage: python serial_test.py [port] [--full]")
            sys.exit(1)

    # Create tester and connect
    tester = SerialTester(port)

    if not tester.connect():
        sys.exit(1)

    try:
        # Run tests
        tester.run_basic_tests()
        tester.run_radio_tests()
        tester.run_config_tests()

        if full_test:
            tester.run_full_tests()
        else:
            print("\n[INFO] Use --full flag to run state-changing tests")

        # Print summary
        success = tester.print_summary()

        sys.exit(0 if success else 1)

    finally:
        tester.disconnect()


if __name__ == "__main__":
    main()
