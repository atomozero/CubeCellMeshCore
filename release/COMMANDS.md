# CubeCellMeshCore v0.8.0 - Command Reference

Serial console at 115200 baud. Type `help` for command list.

Commands marked **MeshCore** use the standard MeshCore CLI naming.

## Status & Info

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `status` | Firmware version, node name/hash, uptime, time sync |
| `stats` | Session counters: RX/TX/FWD/ERR, ADV TX/RX, queue length |
| `stats-core` | Battery voltage/percent, uptime, queue status **MeshCore** |
| `ver` | Show firmware version **MeshCore** |
| `board` | Show hardware board name (HTCC-AB01) **MeshCore** |
| `clock` | Show current time (alias of `time`) **MeshCore** |
| `time` | Show current time and sync status |
| `lifetime` | Persistent stats: boot count, total uptime, RX/TX/FWD |
| `radiostats` | Noise floor, last RSSI/SNR, airtime TX/RX |
| `packetstats` | Packet breakdown: flood/direct RX/TX |
| `telemetry` | Battery mV/%, uptime |
| `identity` | Node name, hash, location, public key |
| `location` | Show current GPS coordinates (read-only) |
| `nodes` | Discovered nodes (hash, name, RSSI, SNR, packet count, last seen) |
| `nodes <page>` | Paginated node list (3 per page: `nodes 0`, `nodes 1`, ...) **MeshCore** |
| `contacts` | Known contacts with public keys (serial only) |
| `neighbours` / `neighbors` | Direct repeater neighbours (0-hop ADVERTs) |
| `health` | System dashboard: uptime, battery, network, mailbox, alerts, problem nodes |
| `mailbox` | Store-and-forward mailbox status (slots, EEPROM/RAM, age) |
| `power` | Show power mode, RX boost, deep sleep status |
| `powersaving` | Show current power save mode (0/1/2) **MeshCore** |
| `radio` | Show current radio parameters (+ temp radio if active) |
| `rssi` | Show last RSSI and SNR |
| `acl` | Show admin/guest passwords and active session count |
| `repeat` | Show repeat status and max hops |
| `advert interval` | Show ADVERT interval (minutes) and next scheduled |
| `txpower` | Show current TX power, max, auto status |
| `get name` | Show current node name **MeshCore** |
| `get lat` | Show current latitude **MeshCore** |
| `get lon` | Show current longitude **MeshCore** |
| `get freq` | Show current radio frequency **MeshCore** |
| `get flood.max` | Show max flood hops **MeshCore** |
| `get guest.password` | Show guest password **MeshCore** |
| `get public.key` | Show public key (hex, serial only) **MeshCore** |
| `get owner.info` / `set owner.info` | Show node owner info text **MeshCore** |
| `get af` / `set af` | Show airtime/duty-cycle factor (tenths: 10=1.0x) **MeshCore** |
| `get adc.multiplier` / `set adc.multiplier` | Show battery ADC calibration (tenths: 10=1.0x) **MeshCore** |
| `get txdelay` / `set txdelay` | Show TX jitter delay factor (0-500, default 100) **MeshCore** |
| `get rxdelay` / `set rxdelay` | Show RX delay factor (0-500, default 100) **MeshCore** |
| `get direct.txdelay` / `set direct.txdelay` | Show direct TX delay factor (0-500, default 100) **MeshCore** |
| `get agc.reset.interval` / `set agc.reset.interval` | Show AGC reset interval in seconds (0=disabled) **MeshCore** |
| `get flood.advert.interval` / `set flood.advert.interval` | Show flood ADVERT interval in hours (0=auto) **MeshCore** |

## Node Configuration

| Command | Description |
|---------|-------------|
| `set name <name>` | Set node name (1-15 chars), saves to EEPROM **MeshCore** |
| `set lat <latitude>` | Set latitude (decimal, e.g. `45.123456`), saves to EEPROM **MeshCore** |
| `set lon <longitude>` | Set longitude (decimal, e.g. `7.654321`), saves to EEPROM **MeshCore** |
| `set repeat on\|off` | Enable/disable packet repeating |
| `set flood.max <n>` | Set max flood hops (1-15) |
| `set owner.info <text>` | Set node owner info (max 31 chars) **MeshCore** |
| `set af <n>` | Set airtime factor (tenths: 0=off, 10=1.0x, max 90=9.0x) **MeshCore** |
| `set adc.multiplier <n>` | Set battery ADC multiplier (tenths: 0=auto, 10=1.0x, max 100) **MeshCore** |
| `set txdelay <n>` | Set TX jitter delay factor (0-500, default 100) **MeshCore** |
| `set rxdelay <n>` | Set RX delay factor (0-500, default 100) **MeshCore** |
| `set direct.txdelay <n>` | Set direct TX delay factor (0-500, default 100) **MeshCore** |
| `set agc.reset.interval <sec>` | Set AGC reset interval (0=disabled, multiples of 4) **MeshCore** |
| `set flood.advert.interval <hours>` | Set flood ADVERT interval (0=auto, 3-48h) **MeshCore** |
| `mode 0\|1\|2` | Set power mode (0=performance, 1=balanced, 2=powersave) |
| `powersaving on` | Set power save mode 2 (maximum savings) **MeshCore** |
| `powersaving off` | Set power save mode 0 (performance) **MeshCore** |
| `sleep on\|off` | Enable/disable deep sleep |
| `rxboost on\|off` | Enable/disable RX gain boost |
| `nodetype chat\|repeater` | Set node type (serial only) |
| `time <timestamp>` | Set Unix time manually (serial only) |

## Radio

| Command | Description |
|---------|-------------|
| `radio` | Show current radio parameters |
| `get freq` | Show current frequency only **MeshCore** |
| `set freq <MHz>` | Set frequency only (keeps other params), activates temp radio **MeshCore** |
| `set radio <freq>,<bw>,<sf>,<cr>` | Set radio parameters (comma-separated, serial only) **MeshCore** |
| `set radio <f>,<bw>,<sf>,<cr>,<min>` | Set radio with auto-revert timeout in minutes **MeshCore** |
| `tempradio <freq> <bw> <sf> <cr>` | Set temporary radio params (space-separated, serial only) |
| `tempradio <f> <bw> <sf> <cr> <min>` | Temp radio with auto-revert timeout in minutes **MeshCore** |
| `tempradio off` | Restore default radio params |
| `tempradio` | Show temp radio status |

Example: `set radio 869.618,62.5,8,8` or `tempradio 869.618 62.5 8 8`
Example with timeout: `set radio 869.618,62.5,8,8,30` (reverts after 30 minutes)

## TX Power

| Command | Description |
|---------|-------------|
| `txpower` | Show current TX power and auto status |
| `set tx <dBm>` | Set manual TX power (disables auto) **MeshCore** |
| `set tx auto on` | Enable adaptive TX power **MeshCore** |
| `set tx auto off` | Disable auto, restore max power **MeshCore** |

### Adaptive TX Power (v0.7.0+)

Automatic transmission power adjustment based on neighbour link quality:

- **Enabled**: `set tx auto on`
- **Disabled**: `set tx auto off`
- **Manual**: `set tx 14` (sets 14 dBm, disables auto)
- **Status**: `txpower` shows: `TxP:14dBm max:21 auto:on`

**How it works:**
- Evaluates every 60 seconds
- Calculates average SNR of all neighbours
- If avg SNR > +10dB: **Reduces** power by 2 dBm (energy saving)
- If avg SNR < -5dB: **Increases** power by 2 dBm (better coverage)
- Range: 5 dBm (minimum) to 21 dBm (maximum, hardware limit)
- Step size: 2 dBm

**Benefits:**
- Energy saving when signal is strong
- Automatic coverage optimization when signal is weak
- Reduced interference on the mesh network

## ADVERT & Network

| Command | Description |
|---------|-------------|
| `advert` | Send ADVERT packet immediately (flood) |
| `advert local` | Send ADVERT packet (direct, 0-hop only) |
| `set advert.interval <min>` | Set ADVERT interval in minutes (1-1440) **MeshCore** |
| `advert interval` | Show current interval and next scheduled |
| `ping` | Send broadcast test packet (FLOOD) |
| `ping <hash>` | Directed ping to node by hex hash, auto-PONG reply |
| `trace <hash>` | Trace route to node, shows path and hop count |

## Passwords & Security

| Command | Description |
|---------|-------------|
| `password` | Show admin and guest passwords |
| `password admin <pwd>` | Set admin password (serial only) |
| `password guest <pwd>` | Set guest password (serial only) |
| `password <pwd>` | Set admin password (remote CLI) **MeshCore** |
| `set guest.password <pwd>` | Set guest password (remote CLI) **MeshCore** |
| `get guest.password` | Show guest password **MeshCore** |
| `setperm <pubkey> <perm>` | Set per-node ACL permission (0-255, serial only) **MeshCore** |

## Region Filtering (v0.8.0+)

Deny-based flood filtering aligned with MeshCore 1.10.0+ protocol. Each region entry controls whether flood packets with matching transport codes are forwarded or dropped. Default: forward everything (no filtering when no regions configured).

| Command | Description |
|---------|-------------|
| `region` | List wildcard and all region entries with flood status (A=allow, D=deny) |
| `region put <name>` | Add region entry (admin only) |
| `region remove <name>` | Remove region entry (admin only) |
| `region allowf <name>` | Allow flood forwarding for region or `*` (admin only) |
| `region denyf <name>` | Deny flood forwarding for region or `*` (admin only) |
| `region save` | Persist region map to EEPROM (admin only) |
| `region load` | Load region map from EEPROM (admin only) |

**Region names:** ISO 3166-1 alpha-2 codes (e.g., `nl`, `au`, `us`) or UNECE subdivision codes (e.g., `nl-li`, `au-nsw`). Max 11 characters (12 in EEPROM). Use `*` for the wildcard (controls legacy FLOOD packets).

**Example:**
```
region put it          # Add Italy region
region put it-lom      # Add Lombardy sub-region
region denyf au        # Block Australian flood traffic
region allowf *        # Allow legacy flood packets
region save            # Persist to EEPROM (survives reboot)
```

**How it works:**
- Transport codes are cryptographic: `HMAC-SHA256(SHA256(region_name), payload)` truncated to 2 bytes
- 4 region slots + 1 wildcard entry
- TRANSPORT_FLOOD packets: matched against region map via transport code verification
- Legacy FLOOD packets: controlled by wildcard `*` entry only
- Direct messages (DMs) are never filtered

## Store-and-Forward Mailbox

The repeater stores messages for offline nodes and re-delivers them when the node comes back online (sends an ADVERT). Storage: 2 persistent EEPROM slots + 4 volatile RAM slots = 6 messages max, 24h TTL.

| Command | Description |
|---------|-------------|
| `mailbox` | Show mailbox status: used/total, EEPROM (E) and RAM (R) counts |
| `mailbox clear` | Clear all mailbox slots (admin only) |

## Health Monitor

Automatic mesh health monitoring. Checks for offline nodes (>30 min) and sends alerts.

| Command | Description |
|---------|-------------|
| `health` | Dashboard: uptime, battery, online/offline nodes, problem nodes |
| `alert` | Show alert status (on/off, destination) |
| `alert on\|off` | Enable/disable automatic health alerts |
| `alert dest <name>` | Set alert destination (by contact name) |
| `alert clear` | Clear alert configuration |
| `alert test` | Send a test alert (serial only) |


## Quiet Hours

Reduce forward rate during configurable hours. Requires TimeSync.

| Command | Description |
|---------|-------------|
| `quiet` | Show quiet hours status |
| `quiet <start> <end>` | Set quiet hours (0-23, e.g. `quiet 22 6`) |
| `quiet off` | Disable quiet hours |

## Circuit Breaker

Blocks DIRECT forwarding to neighbours with degraded links (SNR < -10dB). After 5 min, transitions to half-open. Good SNR closes breaker. FLOOD is never blocked.

| Command | Description |
|---------|-------------|
| `cb` | Show count of open circuit breakers |

## Neighbours

| Command | Description |
|---------|-------------|
| `neighbours` / `neighbors` | List direct repeater neighbours with link quality statistics |
| `neighbor.remove <hex>` | Remove neighbour by pubkey hex prefix **MeshCore** |

The `neighbours` command shows enhanced link quality metrics:
- **Current and Average RSSI/SNR**: Compare instantaneous vs smoothed values (EMA)
- **Packet Count**: Total packets received from each neighbour
- **Circuit Breaker State**: Link health indicator (ok/OPEN/half)

Example output:
```
Nbr:2
 A3 Node1 -82dBm(-84) s:9.0(8.5)dB p:156 cb:ok 5s
 F7 Node2 -95dBm(-93) s:3.5(4.0)dB p:89 cb:ok 12s
```

## Rate Limiting

| Command | Description |
|---------|-------------|
| `ratelimit` | Show rate limiter status and blocked counts |
| `ratelimit on\|off` | Enable/disable rate limiting |
| `ratelimit reset` | Reset rate limit counters |

Default limits: Login 5/min, Request 30/min, Forward 100/min.

## Statistics & Maintenance

| Command | Description |
|---------|-------------|
| `clear stats` | Reset session counters (RX/TX/FWD/ERR/ADV) **MeshCore** |
| `savestats` | Force save persistent statistics to EEPROM (serial only) |

## System

| Command | Description |
|---------|-------------|
| `save` | Save configuration to EEPROM |
| `reset` | Reset configuration to factory defaults |
| `reboot` | Restart device |
| `clkreboot` | Reset internal clock sync and reboot **MeshCore** |
| `newid` | Generate new Ed25519 identity (serial only) |
| `get prv.key` | Export Ed25519 private key seed (64 hex chars, serial only) **MeshCore** |
| `set prv.key <hex>` | Import Ed25519 seed and regenerate identity (serial only) **MeshCore** |
| `get public.key` | Show public key (hex, serial only) **MeshCore** |


## Serial-Only Commands

These commands are only available via USB serial console, not remotely:

| Command | Description |
|---------|-------------|
| `contacts` | List known contacts |
| `contact <hash>` | Show contact details and public key |
| `nodetype chat\|repeater` | Set node type |
| `time <timestamp>` | Set Unix time |
| `newid` | Generate new identity |
| `get prv.key` | Export Ed25519 private key seed |
| `set prv.key <hex>` | Import Ed25519 seed |
| `get public.key` | Show public key hex |
| `setperm <pubkey> <perm>` | Set per-node ACL permission |
| `savestats` | Force save stats to EEPROM |
| `tempradio ...` | Temporary radio parameters |
| `set radio ...` | Set radio parameters |
| `set freq <MHz>` | Set frequency only |
| `alert test` | Send test alert |
| `msg <name> <message>` | Send direct message to contact (only in builds without `LITE_MODE`) |

## Remote Configuration (via MeshCore app)

All shared commands are available remotely via the MeshCore app's encrypted CLI channel.

### Guest-allowed commands (read-only)

```
status  stats  stats-core  ver  board  clock  time
lifetime  radiostats  packetstats  telemetry  identity
location  nodes  nodes <page>  neighbours  health  mailbox
power  powersaving  radio  rssi  acl  quiet  cb  log
repeat  advert interval  txpower  region
get name  get lat  get lon  get freq
get flood.max  get guest.password
get owner.info  get af  get adc.multiplier  get txdelay
get rxdelay  get direct.txdelay  get agc.reset.interval
get flood.advert.interval
set owner.info  set af  set adc.multiplier  set txdelay
set rxdelay  set direct.txdelay  set agc.reset.interval
set flood.advert.interval
alert  ratelimit  sleep  rxboost  help
```

### Admin-only commands (read-write)

```
set name <X>  set lat <X>  set lon <X>
set repeat on/off  set flood.max <N>
set advert.interval <min>  set tx <dBm>  set tx auto on/off
set owner.info <text>  set af <N>  set adc.multiplier <N>
set txdelay <N>  set rxdelay <N>  set direct.txdelay <N>
set agc.reset.interval <sec>  set flood.advert.interval <hours>
password <pwd>  set guest.password <pwd>
powersaving on/off  mode 0/1/2
sleep on/off  rxboost on/off
advert  advert local  ping  ping <hash>  trace <hash>
alert on/off/dest/clear  mailbox clear
quiet <start> <end>  quiet off
region put/remove/allowf/denyf <name>  region save  region load
ratelimit on/off/reset  clear stats  neighbor.remove <hex>
save  reset  reboot  clkreboot
```

## Radio Settings (EU868 default)

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
2. Set passwords before deploying: `password admin myAdminPwd` and `password guest myGuestPwd`
3. Use `set lat` and `set lon` separately to set GPS coordinates
4. Use `set advert.interval 5` for 5-minute ADVERT interval
5. Use `set tx auto on` to enable adaptive TX power
6. Use `powersaving on` for maximum battery life (mode 2)
7. Use `health` to monitor mesh link quality and node availability
8. Enable `alert on` to get automatic notifications when nodes go offline
9. The `mailbox` stores messages for offline nodes automatically - no config needed
10. Use `tempradio` to test radio parameters without persisting, `set radio` to apply
11. Use `get prv.key` to backup your identity seed before flashing new firmware
12. Use `set af` to adjust duty cycle (0=off, 10=1.0x, 20=2.0x slower)
13. Use `set agc.reset.interval 60` to reset receiver gain every 60 seconds
14. Use `tempradio 869.618 62.5 8 8 30` to auto-revert radio params after 30 minutes
15. Use `region put <code>` to add region entries for flood filtering (MeshCore 1.10.0+)
