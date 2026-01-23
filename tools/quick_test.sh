#!/bin/bash
# CubeCellMeshCore Quick Test Script
# Usage: ./quick_test.sh [port]
# Example: ./quick_test.sh /dev/ttyUSB0

PORT=${1:-/dev/ttyUSB0}
BAUD=115200

echo "=== CubeCellMeshCore Quick Test ==="
echo "Port: $PORT"
echo ""

# Function to send command and wait for response
send_cmd() {
    echo ">>> $1"
    echo -e "$1\r" > $PORT
    sleep 1
    cat < $PORT &
    sleep 0.5
    kill $! 2>/dev/null
    echo ""
}

# Configure port
stty -F $PORT $BAUD cs8 -cstopb -parenb -echo

echo "=== Basic Tests ==="
send_cmd "status"
send_cmd "stats"
send_cmd "identity"
send_cmd "telemetry"

echo "=== Network Tests ==="
send_cmd "nodes"
send_cmd "contacts"
send_cmd "advert"
sleep 2
send_cmd "stats"

echo "=== Config Tests ==="
send_cmd "sleep"
send_cmd "rxboost"
send_cmd "location"
send_cmd "alert"

echo "=== Test Complete ==="
