# CubeCellMeshCore Test Procedure

## Overview

This document describes the test procedures for verifying CubeCellMeshCore firmware functionality. Tests are performed via serial console at 115200 baud.

## Prerequisites

- CubeCell HTCC-AB01 with firmware flashed
- Serial terminal (PlatformIO monitor, PuTTY, or similar)
- Connection: `pio device monitor -b 115200`

---

## Test Categories

### 1. Basic System Tests

#### 1.1 Help Command
```
Command: help
Expected: List of available commands
Pass: Command list is displayed
```

#### 1.2 Status Check
```
Command: status
Expected: Firmware version, node name, hash, radio parameters, time sync status
Pass: All fields displayed with valid values
```

#### 1.3 Statistics
```
Command: stats
Expected: RX/TX/FWD/ERR counters, ADV counters, queue status
Pass: Counters displayed (may be 0 if no traffic)
```

---

### 2. Identity Tests

#### 2.1 View Identity
```
Command: identity
Expected: Public key (64 hex chars), node name, node hash
Pass: Valid hex public key displayed
```

#### 2.2 View/Change Node Name
```
Command: name
Expected: Current node name displayed

Command: name TestNode
Expected: Name changed confirmation
Pass: Name updated and persists after reboot
```

#### 2.3 Generate New Identity
```
Command: newid
Expected: New keypair generated, new public key displayed
Pass: Public key changes from previous value
WARNING: This resets the node identity!
```

---

### 3. Radio/Network Tests

#### 3.1 Send ADVERT
```
Command: advert
Expected: ADVERT packet transmitted, TX counter increments
Pass: "ADVERT sent" message, advTxCount increases
```

#### 3.2 View Discovered Nodes
```
Command: nodes
Expected: List of seen nodes (may be empty if no other nodes nearby)
Pass: Command executes without error
```

#### 3.3 View Contacts
```
Command: contacts
Expected: List of known contacts from received ADVERTs
Pass: Command executes, shows contacts if any received
```

#### 3.4 View Neighbours
```
Command: neighbours
Expected: List of direct neighbours with RSSI/SNR
Pass: Shows neighbour list (may be empty)
```

#### 3.5 Broadcast Ping
```
Command: ping
Expected: FLOOD test packet sent, TX counter increments
Pass: "[P] #N" and "[P] TX ok" messages displayed
```

#### 3.6 Directed Ping
```
Command: ping A3
Expected: Directed ping sent to node A3, waits for PONG
Pass: "[P] -> A3 #N" and "[P] TX ok" displayed
      If node A3 is reachable: "[P] PONG A3 <name> rssi=<val> snr=<val>dB p=<hops>"
Note: Requires a second node with hash A3 to verify PONG response
```

---

### 4. Telemetry Tests

#### 4.1 Read Telemetry
```
Command: telemetry
Expected: Battery voltage (mV), temperature (C), uptime
Pass: Valid readings displayed
Example: "Batt: 4200mV Temp: 25C Uptime: 120s"
```

---

### 5. Configuration Tests

#### 5.1 Power Mode
```
Command: sleep
Expected: Current deep sleep status displayed

Command: sleep on
Expected: Deep sleep enabled

Command: sleep off
Expected: Deep sleep disabled
Pass: Settings change and persist after save
```

#### 5.2 RX Boost
```
Command: rxboost
Expected: Current RX boost status

Command: rxboost on
Expected: RX boost enabled (better sensitivity, more power)

Command: rxboost off
Expected: RX boost disabled
Pass: Settings change and persist after save
```

#### 5.3 Save Configuration
```
Command: save
Expected: "Saved to EEPROM" message
Pass: Settings persist after reboot
```

#### 5.4 Reset to Defaults
```
Command: reset
Expected: All settings reset to factory defaults
Pass: Config reset, passwords reset to admin/guest
```

---

### 6. Location Tests

#### 6.1 View Location
```
Command: location
Expected: Current lat/lon or "not set"
```

#### 6.2 Set Location
```
Command: location 45.464161 9.191383
Expected: Location updated confirmation
Pass: Location saved and included in ADVERT
```

#### 6.3 Clear Location
```
Command: location clear
Expected: Location cleared
Pass: ADVERT no longer includes location
```

---

### 7. Time Sync Tests

#### 7.1 View Time
```
Command: time
Expected: Current timestamp and sync status
Pass: Shows timestamp (0 if not synced)
```

#### 7.2 Set Time (Manual)
```
Command: time 1705936800
Expected: Time set to specified Unix timestamp
Pass: Time updated, isSynchronized() returns true
```

---

### 8. Node Type Tests

#### 8.1 Set as Chat Node
```
Command: nodetype chat
Expected: Node type changed to CHAT (0x81)
Pass: ADVERT flags show chat type
```

#### 8.2 Set as Repeater
```
Command: nodetype repeater
Expected: Node type changed to REPEATER (0x01)
Pass: ADVERT flags show repeater type
```

---

### 9. Alert System Tests

#### 9.1 View Alert Status
```
Command: alert
Expected: Alert enabled/disabled status, destination if set
```

#### 9.2 Enable/Disable Alerts
```
Command: alert on
Expected: Alerts enabled (requires destination to be set)

Command: alert off
Expected: Alerts disabled
```

#### 9.3 Set Alert Destination
```
Command: alert dest <32-byte-pubkey-hex>
Expected: Alert destination set
```

#### 9.4 Test Alert
```
Command: alert test
Expected: Test alert sent to configured destination
Pass: Alert message transmitted
```

---

### 10. Password Tests

#### 10.1 View Passwords
```
Command: passwd
Expected: Current admin and guest passwords displayed
```

#### 10.2 Change Passwords
```
Command: passwd admin newadminpass
Command: passwd guest newguestpass
Expected: Passwords updated
Pass: New passwords work for remote CLI auth
```

---

### 11. System Tests

#### 11.1 Reboot
```
Command: reboot
Expected: Device reboots, reconnects to serial
Pass: Device restarts, shows boot messages
```

---

## Automated Test Script

Connect to serial and send these commands in sequence:

```bash
# Basic functionality test sequence
echo "help" > /dev/ttyUSB0
sleep 1
echo "status" > /dev/ttyUSB0
sleep 1
echo "stats" > /dev/ttyUSB0
sleep 1
echo "identity" > /dev/ttyUSB0
sleep 1
echo "telemetry" > /dev/ttyUSB0
sleep 1
echo "nodes" > /dev/ttyUSB0
sleep 1
echo "contacts" > /dev/ttyUSB0
sleep 1
echo "advert" > /dev/ttyUSB0
sleep 2
echo "stats" > /dev/ttyUSB0
```

---

## LED Indicator Tests

| Action | Expected LED |
|--------|--------------|
| Boot | Brief flash |
| RX packet | Green flash |
| TX packet | Violet (purple) flash |
| Error | Red solid |

---

## MeshCore App Compatibility Test

1. Install MeshCore app (Android/iOS)
2. Configure app for same frequency/region as node
3. Start app scanning
4. Send `advert` command on node
5. **Pass**: Node appears in app with correct name and type

---

## Test Results Template

| Test | Command | Expected | Actual | Pass/Fail |
|------|---------|----------|--------|-----------|
| 1.1 | help | Command list | | |
| 1.2 | status | System info | | |
| 1.3 | stats | Counters | | |
| 2.1 | identity | Public key | | |
| 3.1 | advert | TX sent | | |
| 4.1 | telemetry | Batt/Temp | | |
| 5.3 | save | EEPROM saved | | |
| 11.1 | reboot | Restart | | |

---

## Known Limitations

- Time sync requires receiving ADVERT from another synced node
- Alert/Report features require valid destination public key
- Some commands only available in non-LITE_MODE builds
- Deep sleep may interfere with serial reception

## Troubleshooting

**No serial output**: Check baud rate (115200), verify SILENT mode not enabled

**Commands not recognized**: Ensure complete command sent with newline

**ADVERT not visible in app**: Verify frequency/region match, check syncword (0x12)

**Radio errors**: Check antenna connection, verify radio initialization in boot log
