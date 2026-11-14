# CubeCellMeshCore v0.8.0

Firmware par repeater MeshCore par el Heltec CubeCell HTCC-AB01.

## Cossa ghe xe de novo ne la v0.8.0

- **Basta trafego perso** - I pacheti co hash del percorso da 2 o 3 byte (MeshCore 1.11+) i vegniva butai via come `Bad pkt`; desso i vien leti, deduplicai e inoltrai giusti. Provà par aria fin a 9 salti
- **Pacheti TRACE** inoltrai come fa MeshCore
- **Deduplicassion** compagna de MeshCore
- **`flood.max`** desso el funsiona: el limita quanto lontan va i ADVERT (predefinio 8)
- **Eco del proprio ADVERT** no'l mete pi el repeater tra i so nodi
- **Sicuressa**
  - Sistemà un overflow de stack che se podeva tirar fora co un pacheto de login sensa autenticassion
  - El login admin da remoto el vien rifiutà fin che la password admin xe ancora `admin` (aviso a l'avvio)
  - `acl` el mostra le password solo a l'admin
- **Corression** - Sovraposission EEPROM tra mailbox e region map; tornai i alias MeshCore (`get tx`, `get radio`, `get repeat`, `get advert.interval`, `stats-radio`, `stats-packets`, `erase`)
- **Firmware pi picolo** - ~15 KB de Flash libari, co i messagi direti ativi
- **Ocio co l'agiornamento** - La configurassion la ga cambià formato: dopo el flash se torna ai valori predefinii. Rimeti nome, password e opzioni radio

## Funzionalità Precedenti (v0.5.0-v0.7.0)

- **Statistiche Link Quality** - Medie mobili de RSSI/SNR par ogni visìn
- **Potensa TX Adativa** - `set tx auto on/off`, `txpower <5-21>`
- **Routing DIRECT** - Suporto completo par pacheti co rotta DIRECT (path peeling)
- **Casseta de Posta (Mailbox)** - 2 posti EEPROM + 4 in RAM
- **Dashboard Salute** - Comando `health` co vitali e nodi problematici
- **Configurassion Remota** - 50+ comandi CLI via canal cifra'
- **Sistema Loop Detection** - Modi configurabili: off, minimal, moderate, strict
- **Filtro Max Hops** - Auto-add contati limitai par hop count
- **Quiet Hours** - Rate limiting noturno par risparmiar bateria
- **Circuit Breaker** - Bloca DIRECT forwarding a visìn degradai

## Caratteristiche

- Compatibile col protocollo MeshCore (app Android/iOS)
- Identita' e firme Ed25519
- Broadcasting ADVERT co sincronizassion del tempo
- Forwarding pacchetti con CSMA/CA basado sul SNR
- Casseta de posta par nodi offline (store-and-forward)
- Monitor de la salute de la rete con alerte automatiche
- Configurassion remota completa tramite CLI cifra'
- Deep sleep (~20 uA de consumo)
- Telemetria bateria e temperatura
- Tracking dei visini (repeater direti a 0-hop)
- Rate limiting (login, richieste, forward)
- Statistiche persistenti in EEPROM

## Hardware

- **Scheda**: Heltec CubeCell HTCC-AB01
- **MCU**: ASR6501 (ARM Cortex-M0+ @ 48 MHz + SX1262)
- **Flash**: 128 KB (88.5% doparadi, ~15 KB libari)
- **RAM**: 16 KB (52.4% doparadi)
- **Radio**: SX1262 LoRa (EU868: 869.618 MHz, BW 62.5 kHz, SF8, CR 4/8)

## File nel pacchetto

| File | Descrission |
|------|-------------|
| `firmware.cyacd` | Imagine Flash par CubeCellTool (Windows) |
| `firmware.hex` | Formato Intel HEX |
| `INSTALL.md` | Guida de instalassion e primo avvio |
| `COMMANDS.md` | Riferimento comandi completo (50+ comandi) |
| `README.md` | Versione inglese |
| `README_VEN.md` | Sta pàgina qua |

## Come partire

1. Flasha `firmware.cyacd` co CubeCellTool o PlatformIO
2. Colegate la seriale a 115200 baud
3. Meti le password: `password admin <pwd>` e `password guest <pwd>` e dopo `save`
4. Meti el nome: `set name ElMeRepeater` e dopo `save`
5. El nodo el scominsia a mandar ADVERT e a inoltrare pacchetti

Varda `INSTALL.md` par le istrussioni detajae.

## Autor

**Andrea Bernardi** - Creador del projeto e svilupador prinsipal

## Link

- Sorgenti: https://github.com/atomozero/CubeCellMeshCore
- MeshCore: https://github.com/meshcore-dev/MeshCore

## Licensa

Licensa MIT - Varda el file LICENSE par i detagli.

---
*Fato col cuor a Venessia* 🦁
