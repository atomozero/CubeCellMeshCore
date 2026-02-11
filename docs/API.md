# CubeCellMeshCore API Reference

## Core Modules

### Led (core/Led.h)

```cpp
void initLed();           // Initialize LED hardware
void ledRxOn();           // Show RX activity (green)
void ledTxOn();           // Show TX activity (viola)
void ledRedSolid();       // Show error state (red)
void ledGreenBlink();     // Quick success blink
void ledBlueDoubleBlink(); // Special event signal
void ledOff();            // Turn off LED and Vext
```

### Config (core/Config.h)

```cpp
void loadConfig();        // Load from EEPROM (or defaults)
void saveConfig();        // Save current config to EEPROM
void resetConfig();       // Reset to factory defaults
void applyPowerSettings(); // Apply RxBoost/DeepSleep settings
void enterDeepSleep();    // Enter lowest power state
void enterLightSleep(uint8_t ms); // Brief low-power delay
uint32_t generateNodeId(); // Generate unique node ID from chip
```

### Radio (core/Radio.h)

```cpp
void setupRadio();        // Initialize SX1262
void startReceive();      // Enter duty-cycle RX mode
bool transmitPacket(MCPacket* pkt); // Transmit a packet
void calculateTimings();  // Compute LoRa timing parameters
uint32_t getTxDelayWeighted(int8_t snr); // CSMA backoff delay
bool isActivelyReceiving(); // Check if RX in progress
void feedWatchdog();      // Feed hardware watchdog
void handleRadioError();  // Handle and recover from errors
```

## Mesh Protocol (mesh/)

### Identity (mesh/Identity.h)

```cpp
class IdentityManager {
    bool begin();                    // Initialize (load or generate)
    bool generate();                 // Generate new keypair
    bool load();                     // Load from EEPROM
    bool save();                     // Save to EEPROM
    void reset();                    // Generate new and save

    // Getters
    uint8_t getNodeHash();           // First byte of public key
    const uint8_t* getPublicKey();   // 32-byte public key
    const uint8_t* getPrivateKey();  // 64-byte private key
    const char* getNodeName();       // Node display name
    uint8_t getFlags();              // Type + feature flags

    // Setters
    void setNodeName(const char* name);
    void setFlags(uint8_t flags);
    void setLocation(float lat, float lon);
    void clearLocation();

    // Crypto
    void sign(uint8_t* sig, const uint8_t* data, size_t len);
    static bool verify(const uint8_t* sig, const uint8_t* pubKey,
                       const uint8_t* data, size_t len);
};
```

### Packet (mesh/Packet.h)

```cpp
struct MCPacket {
    MCPacketHeader header;
    uint8_t pathLen;
    uint8_t payloadLen;
    uint8_t path[64];
    uint8_t payload[180];

    uint16_t getTotalSize();
    uint16_t serialize(uint8_t* buf, uint16_t maxLen);
    bool deserialize(const uint8_t* buf, uint16_t len);
    void clear();
};

// Header encoding
#define MC_ROUTE_FLOOD         0x01
#define MC_ROUTE_DIRECT        0x02
#define MC_PAYLOAD_ADVERT      0x04
#define MC_PAYLOAD_REQUEST     0x00
// ... see Packet.h for full list
```

### Advert (mesh/Advert.h)

```cpp
class TimeSync {
    uint8_t syncFromAdvert(uint32_t unixTime); // 0=none, 1=first, 2=resync
    uint32_t getTimestamp();          // Current time (or uptime)
    bool isSynchronized();
    void setTime(uint32_t unixTime);  // Manual set
};

class AdvertGenerator {
    void begin(IdentityManager* id, TimeSync* ts);
    void setInterval(uint32_t ms);
    bool shouldSend();
    void markSent();

    bool build(MCPacket* pkt, uint8_t routeType);
    bool buildFlood(MCPacket* pkt);
    bool buildZeroHop(MCPacket* pkt);

    static bool parseAdvert(const uint8_t* payload, uint16_t len,
                            AdvertInfo* info);
    static uint32_t extractTimestamp(const uint8_t* payload, uint16_t len);
};
```

### Contacts (mesh/Contacts.h)

```cpp
struct Contact {
    uint8_t pubKey[32];
    char name[16];
    int16_t lastRssi;
    uint32_t lastSeen;
    uint8_t getHash();
};

class ContactManager {
    bool addOrUpdate(const uint8_t* pubKey, const char* name,
                     int16_t rssi);
    Contact* findByHash(uint8_t hash);
    Contact* findByName(const char* name);
    Contact* findByPubKey(const uint8_t* pubKey);
    uint8_t getCount();
    Contact* getContact(uint8_t index);
};
```

## Serial Commands

### Basic Commands
| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `status` | System status overview |
| `stats` | Packet statistics |
| `advert` | Send ADVERT now |
| `nodes` | List seen nodes |
| `contacts` | List known contacts |
| `neighbours` | List mesh neighbours |
| `telemetry` | Battery, temperature, uptime |
| `identity` | Show identity and public key |

### Configuration Commands
| Command | Description |
|---------|-------------|
| `name <name>` | Set node name (1-15 chars) |
| `location <lat> <lon>` | Set GPS coordinates |
| `location clear` | Clear location |
| `nodetype chat\|repeater` | Set node type |
| `advert interval <sec>` | Set ADVERT interval (60-86400) |
| `passwd admin <pw>` | Set admin password |
| `passwd guest <pw>` | Set guest password |
| `time <unix>` | Manually set time |

### Power Commands
| Command | Description |
|---------|-------------|
| `sleep on\|off` | Enable/disable deep sleep |
| `rxboost on\|off` | Enable/disable RX boost |
| `save` | Save config to EEPROM |
| `reset` | Reset to factory defaults |
| `reboot` | Reboot system |

### Alert Commands
| Command | Description |
|---------|-------------|
| `alert` | Show alert status |
| `alert on\|off` | Enable/disable alerts |
| `alert dest <name\|pubkey>` | Set alert destination |
| `alert clear` | Clear alert destination |
| `alert test` | Send test alert |

### Debug Commands
| Command | Description |
|---------|-------------|
| `test` | Run RFC 8032 Ed25519 tests |
| `newid` | Generate new identity |
| `contact <hash>` | Show contact details |

## Remote CLI Commands

Commands available via encrypted mesh messages:

### Read-only (Guest + Admin)
- `status` - Node status
- `stats` - Packet statistics
- `time` - Current time
- `telemetry` - Sensor data
- `radio` - Radio parameters (freq, BW, SF, CR)
- `nodes` - Seen nodes count
- `neighbours` - Neighbour list

### Admin Only
- `set repeat on|off` - Forwarding control
- `set password <pw>` - Change admin password
- `set guest <pw>` - Change guest password
- `set flood.max <n>` - Max flood hops
- `name <name>` - Change node name
- `location <lat> <lon>` - Set location
- `ping` - Send test packet
- `rxboost [on|off]` - Show or toggle RX boost
- `advert` - Trigger ADVERT
- `advert interval <s>` - Set interval
- `reboot` - Remote reboot
