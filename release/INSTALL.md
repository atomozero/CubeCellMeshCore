# CubeCellMeshCore v0.3.3 - Installation Guide

## Requirements

- Heltec CubeCell HTCC-AB01 board
- USB cable (Micro-USB)
- Windows/macOS/Linux computer

## Installation Methods

### Method 1: Using CubeCellTool (Recommended for Windows)

1. Download CubeCellTool from Heltec: https://resource.heltec.cn/download/tools/CubeCellTool.exe
2. Connect the CubeCell board via USB
3. Open CubeCellTool
4. Select the correct COM port
5. Click "Browse" and select `firmware.cyacd`
6. Click "Program" to flash the firmware

### Method 2: Using PlatformIO

1. Install PlatformIO IDE or CLI
2. Clone the repository or extract the source
3. Run:
   ```bash
   pio run -t upload
   ```

### Method 3: Using esptool/cyflash (Linux/macOS)

1. Install the CubeCell flash tool:
   ```bash
   pip install cyflash
   ```
2. Connect the board and find the port:
   ```bash
   ls /dev/ttyUSB*   # Linux
   ls /dev/cu.*      # macOS
   ```
3. Flash the firmware:
   ```bash
   cyflash --serial /dev/ttyUSB0 firmware.cyacd
   ```

## First Boot

After flashing, the node will:
1. Generate a new Ed25519 identity (first boot only)
2. Start broadcasting ADVERT packets every 5 minutes
3. Begin listening for mesh traffic

## Serial Console

Connect at **115200 baud** to access the serial console.

### Basic Commands

| Command | Description |
|---------|-------------|
| `help` | Show all commands |
| `status` | System status |
| `stats` | Packet statistics |
| `identity` | Show node public key |
| `nodes` | List discovered nodes |
| `telemetry` | Show telemetry data |

### Configuration Commands

| Command | Description |
|---------|-------------|
| `name <name>` | Set node name (max 15 chars) |
| `location <lat> <lon>` | Set GPS coordinates |
| `adminpw <password>` | Set admin password |
| `guestpw <password>` | Set guest password |
| `save` | Save config to EEPROM |
| `reboot` | Restart device |

### Network Commands

| Command | Description |
|---------|-------------|
| `advert` | Send ADVERT now |
| `contacts` | List known contacts |
| `ping` | Send test packet |

## MeshCore App Connection

1. Install MeshCore app (Android/iOS)
2. Go to Settings > LoRa Radio Configuration
3. Select "EU/UK Narrow" preset (or your region)
4. The repeater will appear in the node list
5. Tap on the repeater to connect
6. Enter admin or guest password when prompted

## Passwords

Default passwords are empty. Set them via serial console:

```
adminpw myAdminPassword
guestpw myGuestPassword
save
```

- **Admin**: Full access to CLI commands and configuration
- **Guest**: Read-only access to status and telemetry

## Radio Settings (EU868)

| Parameter | Value |
|-----------|-------|
| Frequency | 869.618 MHz |
| Bandwidth | 62.5 kHz |
| Spreading Factor | SF8 |
| Coding Rate | 4/8 |
| TX Power | 14 dBm |
| Sync Word | 0x12 |

## Troubleshooting

### No serial output
- Check USB connection
- Verify correct COM port
- Try different USB cable

### Not appearing in MeshCore app
- Verify radio settings match (frequency, bandwidth)
- Check that ADVERT is being sent (`advert` command)
- Ensure app is set to same region preset

### Login fails
- Check password is correct
- Verify passwords are saved (`save` command)
- Try resetting passwords via serial

## LED Indicators

- **Green pulse**: Packet received
- **Blue pulse**: Packet transmitted
- **Purple pulse**: Packet forwarded
- **Red flash**: Error

## Power Consumption

- Active RX: ~10mA
- Deep Sleep: ~20uA
- TX (14dBm): ~120mA

## Files Included

- `firmware.cyacd` - Flash image for CubeCellTool
- `firmware.hex` - Intel HEX format
- `INSTALL.md` - This file
- `COMMANDS.md` - Full command reference

## Support

- GitHub: https://github.com/atomozero/CubeCellMeshCore
- MeshCore: https://github.com/meshcore-dev/MeshCore
