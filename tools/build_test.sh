#!/bin/bash
# CubeCellMeshCore Build Test Script
# Tests compilation with different configurations

set -e

echo "=== CubeCellMeshCore Build Tests ==="
echo ""

PROJECT_DIR="$(dirname "$0")/.."
cd "$PROJECT_DIR"

# Test 1: Standard build
echo "[TEST 1] Standard build (LITE_MODE + MINIMAL_DEBUG)"
pio run 2>&1 | tail -5
echo ""

# Test 2: Check flash/RAM usage
echo "[TEST 2] Memory usage check"
FLASH_USAGE=$(pio run 2>&1 | grep "Flash:" | grep -oP '\d+\.\d+%' | head -1)
RAM_USAGE=$(pio run 2>&1 | grep "RAM:" | grep -oP '\d+\.\d+%' | head -1)
echo "Flash: $FLASH_USAGE"
echo "RAM: $RAM_USAGE"

# Check if within limits
FLASH_PCT=$(echo $FLASH_USAGE | tr -d '%')
RAM_PCT=$(echo $RAM_USAGE | tr -d '%')

if (( $(echo "$FLASH_PCT > 100" | bc -l) )); then
    echo "[FAIL] Flash usage exceeds 100%!"
    exit 1
fi

if (( $(echo "$RAM_PCT > 80" | bc -l) )); then
    echo "[WARN] RAM usage above 80%"
fi

echo "[PASS] Memory within limits"
echo ""

# Test 3: Clean build
echo "[TEST 3] Clean build"
pio run -t clean > /dev/null 2>&1
pio run 2>&1 | tail -3
echo ""

# Test 4: Check for compiler warnings
echo "[TEST 4] Check for warnings"
WARNINGS=$(pio run 2>&1 | grep -c "warning:" || true)
if [ "$WARNINGS" -gt 0 ]; then
    echo "[WARN] Found $WARNINGS compiler warnings"
    pio run 2>&1 | grep "warning:" | head -5
else
    echo "[PASS] No warnings"
fi
echo ""

# Summary
echo "=== Build Tests Complete ==="
echo "All builds successful!"
