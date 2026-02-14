# CubeCellMeshCore v0.5.0 - Command Reference

Serial console at 115200 baud.

## Status & Info

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `status` | Firmware, frequency, time sync, RSSI/SNR |
| `stats` | Session counters: RX/TX/FWD/ERR, ADV, queue |
| `lifetime` | Persistent stats: boots, totals, logins |
| `radiostats` | Noise floor, last RSSI/SNR, airtime TX/RX |
| `packetstats` | Packet breakdown: flood/direct RX/TX |
| `telemetry` | Battery mV/%, temperature, uptime |
| `identity` | Node name, hash, public key |
| `nodes` | Discovered nodes (hash, name, RSSI, last seen date/time) |
| `contacts` | Known contacts with public keys |
| `neighbours` | Direct repeater neighbours (0-hop) |
| `health` | System dashboard: uptime, battery, network, mailbox, alerts |
| `mailbox` | Store-and-forward mailbox status (slots, E/R, age) |

## Configuration

| Command | Description |
|---------|-------------|
| `name <name>` | Set node name (1-15 chars) |
| `location <lat> <lon>` | Set GPS coordinates |
| `location` | Show current location |
| `location clear` | Clear location |
| `time [timestamp]` | Show or set Unix time |
| `nodetype chat\|repeater` | Set node type |
| `passwd` | Show admin/guest passwords |
| `passwd admin <pwd>` | Set admin password |
| `passwd guest <pwd>` | Set guest password |
| `sleep on\|off` | Enable/disable deep sleep |
| `rxboost on\|off` | Enable/disable RX gain boost |

## Radio

| Command | Description |
|---------|-------------|
| `radio` | Show current radio parameters |
| `tempradio <freq> <bw> <sf> <cr>` | Set temporary radio params (lost on reboot) |
| `tempradio off` | Restore default radio params |

## Network

| Command | Description |
|---------|-------------|
| `advert` | Send ADVERT packet immediately |
| `advert on\|off` | Enable/disable periodic ADVERT |
| `advert interval` | Show ADVERT interval and next scheduled |

## Store-and-Forward Mailbox

The repeater stores messages for offline nodes and re-delivers them when the node comes back online (sends an ADVERT). Storage: 2 persistent EEPROM slots + 4 volatile RAM slots = 6 messages max.

| Command | Description |
|---------|-------------|
| `mailbox` | Show mailbox status: used/total, EEPROM (E) and RAM (R) counts |
| `mailbox clear` | Clear all mailbox slots (admin only) |

Output example: `Mbox:2/6 E:1 R:1` means 2 messages stored, 1 in EEPROM (persistent), 1 in RAM (lost on reboot). Each slot shows `E0` or `R3` prefix to indicate storage type.

## Health Monitor

Automatic mesh health monitoring. Checks every 60 seconds for offline nodes (>30 min, at least 3 packets seen) and sends alerts to the configured destination.

| Command | Description |
|---------|-------------|
| `health` | Show node count, offline count, per-node SNR and last seen |
| `alert on\|off` | Enable/disable automatic health alerts |
| `alert dest <name>` | Set alert destination (by contact name) |
| `alert clear` | Clear alert configuration |
| `alert test` | Send a test alert immediately |
| `alert` | Show alert status (on/off, destination) |

## Daily Report

| Command | Description |
|---------|-------------|
| `report` | Show report status (on/off, time, destination) |
| `report on` | Enable daily report (requires destination key) |
| `report off` | Disable daily report |
| `report test` | Send a test report immediately |
| `report nodes` | Send a nodes report immediately |
| `report time HH:MM` | Set report send time (24h format) |
| `report clear` | Clear destination key and disable report |

The destination key is set automatically when an admin logs in from the MeshCore app.

## Quiet Hours

Reduce forward rate during configurable hours (e.g., 22:00-06:00). Saves battery. Requires TimeSync; without sync, full rate is used. Config is RAM-only (lost on reboot).

| Command | Description |
|---------|-------------|
| `quiet` | Show quiet hours status |
| `quiet <start> <end>` | Set quiet hours (0-23, admin) |
| `quiet off` | Disable quiet hours (admin) |

## Circuit Breaker

Automatically blocks DIRECT forwarding to neighbours with degraded links (SNR < -10dB). After 5 minutes, transitions to half-open (test). Good SNR closes the breaker. FLOOD is never blocked.

| Command | Description |
|---------|-------------|
| `cb` | Show count of open circuit breakers |

## Adaptive TX Power

Adjusts TX power based on average neighbour SNR. High SNR (>+10dB) reduces power by 2dBm. Low SNR (<-5dB) increases by 2dBm. Range: 5-14 dBm (EU). Config is RAM-only.

| Command | Description |
|---------|-------------|
| `txpower` | Show current TX power and auto status |
| `txpower auto on` | Enable adaptive TX power (admin) |
| `txpower auto off` | Disable and restore max power (admin) |
| `txpower <N>` | Set manual TX power in dBm (admin, disables auto) |

## Admin

| Command | Description |
|---------|-------------|
| `save` | Save configuration to EEPROM |
| `savestats` | Force save statistics to EEPROM |
| `resetstats` | Reset session statistics counters |
| `ratelimit` | Show rate limiter status |
| `ratelimit on\|off` | Enable/disable rate limiting |
| `ratelimit reset` | Reset rate limit counters |
| `newid` | Generate new Ed25519 identity |
| `power` | Show power mode, RX boost, sleep status |
| `acl` | Show passwords and active sessions |
| `repeat` | Show repeat status and max hops |
| `rssi` | Show last RSSI and SNR |
| `mode 0\|1\|2` | Set power mode (0=perf, 1=balanced, 2=powersave) |
| `set repeat on\|off` | Enable/disable packet repeating |
| `set flood.max <n>` | Set max flood hops (1-15) |
| `ping` | Send broadcast test packet (FLOOD) |
| `ping <hash>` | Directed ping to node `<hash>`, auto-PONG reply |
| `trace <hash>` | Trace route to node `<hash>`, shows path and hop count |
| `reset` | Reset configuration to defaults |
| `reboot` | Restart device |

## Security

### Rate Limits (default)
- **Login**: 5 attempts per minute (brute-force protection)
- **Request**: 30 requests per minute (spam protection)
- **Forward**: 100 packets per minute (flood protection)

### Sessions
- Up to 8 concurrent sessions
- Idle sessions expire after 1 hour
- Admin and guest permission levels

## Remote Configuration (via MeshCore app)

All CLI commands are available remotely via the MeshCore app's encrypted CLI channel. No USB cable needed - manage your repeater from anywhere in the mesh.

**Note**: This is configuration-only, NOT firmware OTA. The firmware itself can only be updated via USB cable. Remote configuration lets you change settings, monitor status, and manage the repeater without physical access.

### Guest-allowed commands (read-only)
- `status`, `stats`, `lifetime`, `telemetry`
- `radiostats`, `packetstats`, `radio`
- `nodes`, `contacts`, `neighbours`, `identity`
- `time`, `location`, `advert interval`
- `repeat`, `power`, `health`, `mailbox`
- `quiet`, `cb`, `txpower`
- `rssi`, `help`

### Admin-only commands (read-write)
- `name`, `location`, `location clear`
- `set repeat on/off`, `set flood.max`, `set password`, `set guest`
- `sleep on/off`, `rxboost on/off`, `mode 0/1/2`
- `alert on/off/dest/clear/test`
- `report on/off/dest/time/test/nodes`
- `mailbox clear`, `ratelimit on/off/reset`, `resetstats`
- `advert`, `advert interval`, `advert on/off`
- `ping`, `ping <hash>`, `trace <hash>`
- `quiet <start> <end>`, `quiet off`
- `txpower auto on/off`, `txpower <N>`
- `save`, `reset`, `reboot`

## Radio Settings (EU868)

| Parameter | Value |
|-----------|-------|
| Frequency | 869.618 MHz |
| Bandwidth | 62.5 kHz |
| Spreading Factor | SF8 |
| Coding Rate | 4/8 |
| TX Power | 14 dBm |
| Sync Word | 0x12 |

## Tips

1. Always `save` after changing configuration
2. Set passwords before deploying in the field
3. Use `radiostats` and `packetstats` to monitor link quality
4. Use `tempradio` to test different radio parameters without saving
5. Check `lifetime` to see accumulated statistics across reboots
6. Use `health` to monitor mesh link quality and node availability
7. Enable `alert` to get automatic notifications when nodes go offline
8. The `mailbox` stores messages for offline nodes automatically - no config needed
