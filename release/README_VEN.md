# CubeCellMeshCore v0.5.0

Firmware par repeater MeshCore par el Heltec CubeCell HTCC-AB01.

## Cossa ghe xe de novo ne la v0.5.0

- **Routing DIRECT** - Suporto completo par i pacchetti co rotta DIRECT (path peeling). Prima el repeater el inoltrava solo pacchetti FLOOD, desso anca i messaggi direti tra companion i passa.
- **Casseta de Posta (Mailbox)** - I messagi par i nodi che no xe in linea i vien salvai e riconsegnai in automatico. 2 posti in EEPROM + 4 in RAM. Deduplicassion par no salvar el stesso paco da piu' repeater.
- **Dashboard Salute del Sistema** - El comando `health` el mostra vitali (uptime, bateria, sync), stato rete (online/offline), sotosistemi (mailbox, rate limit, errori), e segnala solo i nodi problematici.
- **Configurassion Remota Completa** - Tuti i 50+ comandi CLI i xe disponibili da remoto tramite el canal cifra' de la app MeshCore. No serve piu' el cavo USB.
- **Sicuressa de le Sessioni** - Le sessioni inative le scade dopo 1 ora.
- **Prevension dei Loop** - El forwarding FLOOD el controla se el nostro hash el xe za nel path par evitar loop de routing.
- **Otimizassion del Codice** - Unio i handler CLI duplicai e eliminao el parsing float. Risparmiado 12.9 KB de Flash (prima 98.2%, desso 91.0%).

## Caratteristiche

- Compatibile col protocollo MeshCore (app Android/iOS)
- Identita' e firme Ed25519
- Broadcasting ADVERT co sincronizassion del tempo
- Forwarding pacchetti con CSMA/CA basado sul SNR
- Casseta de posta par nodi offline (store-and-forward)
- Monitor de la salute de la rete con alerte automatiche
- Configurassion remota completa tramite CLI cifra'
- Report giornaliero automatico a l'admin
- Deep sleep (~20 uA de consumo)
- Telemetria bateria e temperatura
- Tracking dei visini (repeater direti a 0-hop)
- Rate limiting (login, richieste, forward)
- Statistiche persistenti in EEPROM

## Hardware

- **Scheda**: Heltec CubeCell HTCC-AB01
- **MCU**: ASR6501 (ARM Cortex-M0+ @ 48 MHz + SX1262)
- **Flash**: 128 KB (91.0% doparadi, ~12 KB libari)
- **RAM**: 16 KB (47.9% doparadi)
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
3. Meti le password: `passwd admin <pwd>` e dopo `save`
4. Meti el nome: `name ElMeRepeater` e dopo `save`
5. El nodo el scominsia a mandar ADVERT e a inoltrare pacchetti

Varda `INSTALL.md` par le istrussioni detajae.

## Link

- Sorgenti: https://github.com/atomozero/CubeCellMeshCore
- MeshCore: https://github.com/meshcore-dev/MeshCore

---
*Fato col cuor a Venessia* 🦁
