#!/usr/bin/env python3
"""
Test script for Adaptive TX Power and Enhanced Link Quality features
Verifies the new functionality added to the firmware.
"""

def read_firmware_sources():
    """Concatenate all firmware .cpp sources (main.cpp was split into modules)."""
    import glob
    content = ""
    for path in sorted(glob.glob('../src/**/*.cpp', recursive=True)):
        with open(path, 'r') as f:
            content += f.read()
    return content

def test_neighbour_info_structure():
    """Verify NeighbourInfo struct has new fields"""
    print("Testing NeighbourInfo structure...")

    # Expected new fields in NeighbourInfo:
    # - int16_t rssiAvg (RSSI EMA)
    # - int8_t snrAvg (SNR EMA * 4)
    # - uint16_t pktCount (total packets)
    # - uint16_t pktCountWindow (packets in current window)
    # - uint32_t windowStartTime (measurement window start)

    expected_fields = [
        "rssiAvg",
        "snrAvg",
        "pktCount",
        "pktCountWindow",
        "windowStartTime"
    ]

    # Read Repeater.h to verify fields exist
    with open('../src/mesh/Repeater.h', 'r') as f:
        content = f.read()

    # Find NeighbourInfo struct
    struct_start = content.find('struct NeighbourInfo')
    if struct_start == -1:
        print("  ❌ NeighbourInfo struct not found")
        return False

    struct_end = content.find('};', struct_start)
    struct_content = content[struct_start:struct_end]

    # Check for new fields
    missing_fields = []
    for field in expected_fields:
        if field not in struct_content:
            missing_fields.append(field)

    if missing_fields:
        print(f"  ❌ Missing fields: {', '.join(missing_fields)}")
        return False

    print("  ✅ All expected fields present")
    return True

def test_adaptive_tx_commands():
    """Verify adaptive TX power commands exist in the firmware sources"""
    print("\nTesting Adaptive TX Power commands...")

    expected_commands = [
        '"set tx auto on"',
        '"set tx auto off"',
        'evaluateAdaptiveTxPower()'
    ]

    content = read_firmware_sources()

    missing = []
    for cmd in expected_commands:
        if cmd not in content:
            missing.append(cmd)

    if missing:
        print(f"  ❌ Missing commands: {', '.join(missing)}")
        return False

    print("  ✅ All adaptive TX commands present")
    return True

def test_ema_calculation():
    """Verify EMA calculation is present in update() method"""
    print("\nTesting EMA calculation logic...")

    with open('../src/mesh/Repeater.h', 'r') as f:
        content = f.read()

    # Check for EMA formula comments
    if 'EMA: new = old * 0.875 + sample * 0.125' not in content:
        print("  ❌ EMA calculation not found")
        return False

    # Check for actual calculation
    if 'rssiAvg * 7 + rssi' not in content:
        print("  ❌ RSSI EMA calculation missing")
        return False

    if 'snrAvg * 7 + snr' not in content:
        print("  ❌ SNR EMA calculation missing")
        return False

    print("  ✅ EMA calculations present")
    return True

def test_neighbours_command_output():
    """Verify neighbours command shows new statistics"""
    print("\nTesting neighbours command output format...")

    content = read_firmware_sources()

    # Look for updated CP format in neighbours command
    # Should show rssiAvg, snrAvg, pktCount
    if 'n->rssiAvg' not in content or 'n->snrAvg' not in content or 'n->pktCount' not in content:
        print("  ❌ neighbours command not showing new statistics")
        return False

    print("  ✅ neighbours command updated with new statistics")
    return True

def test_window_reset_logic():
    """Verify packet window reset logic exists"""
    print("\nTesting packet window reset logic...")

    with open('../src/mesh/Repeater.h', 'r') as f:
        content = f.read()

    # Check for window reset every 60 seconds
    if 'now - neighbours[i].windowStartTime > 60000' not in content:
        print("  ❌ Window reset logic not found")
        return False

    if 'pktCountWindow = 0' not in content:
        print("  ❌ Window counter reset not found")
        return False

    print("  ✅ Packet window reset logic present")
    return True

def main():
    """Run all tests"""
    print("=" * 60)
    print("Adaptive TX Power & Link Quality Test Suite")
    print("=" * 60)

    tests = [
        test_neighbour_info_structure,
        test_adaptive_tx_commands,
        test_ema_calculation,
        test_neighbours_command_output,
        test_window_reset_logic
    ]

    results = []
    for test in tests:
        try:
            result = test()
            results.append(result)
        except Exception as e:
            print(f"  ❌ Test failed with exception: {e}")
            results.append(False)

    print("\n" + "=" * 60)
    passed = sum(results)
    total = len(results)
    print(f"Tests Passed: {passed}/{total}")

    if passed == total:
        print("✅ All tests passed!")
        return 0
    else:
        print("❌ Some tests failed")
        return 1

if __name__ == '__main__':
    import sys
    import os

    # Change to tools directory
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    sys.exit(main())
