# CubeCellMeshCore v0.4.0 - Command Reference

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

## Daily Report

| Command | Description |
|---------|-------------|
| `report` | Show report status (on/off, time, destination) |
| `report on` | Enable daily report (requires destination key) |
| `report off` | Disable daily report |
| `report test` | Send a test report immediately |
| `report time HH:MM` | Set report send time (24h format) |
| `report clear` | Clear destination key and disable report |

The destination key is set automatically when an admin logs in from the MeshCore app.

## Admin

| Command | Description |
|---------|-------------|
| `save` | Save configuration to EEPROM |
| `savestats` | Force save statistics to EEPROM |
| `alert on\|off` | Enable/disable alerts |
| `ratelimit` | Show rate limiter status |
| `newid` | Generate new Ed25519 identity |
| `power` | Show power mode, RX boost, sleep status |
| `acl` | Show passwords and active sessions |
| `repeat` | Show repeat status and max hops |
| `rssi` | Show last RSSI and SNR |
| `mode 0\|1\|2` | Set power mode (0=perf, 1=balanced, 2=powersave) |
| `set repeat on\|off` | Enable/disable packet repeating |
| `set flood.max <n>` | Set max flood hops (1-15) |
| `ping` | Send test packet |
| `reset` | Reset configuration to defaults |
| `reboot` | Restart device |

## Security

### Rate Limits (default)
- **Login**: 5 attempts per minute (brute-force protection)
- **Request**: 30 requests per minute (spam protection)
- **Forward**: 100 packets per minute (flood protection)

## Remote CLI (via MeshCore app)

Once authenticated via the MeshCore app, you can send CLI commands remotely.

### Guest-allowed commands (remote)
- `status`, `stats`, `lifetime`, `telemetry`
- `radiostats`, `packetstats`, `radio`
- `nodes`, `contacts`, `neighbours`, `identity`
- `time`, `advert interval`

### Admin-only commands (remote)
- `name`, `location`, `passwd`, `nodetype`
- `set repeat/password/guest/flood.max`
- `ping`, `rxboost [on|off]`
- `advert`, `save`, `reset`, `reboot`

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
