# MeshCore CubeCell Repeater

Firmware minimale per Heltec CubeCell (HTCC-AB01) compatibile con la rete MeshCore.

## Caratteristiche

- **Ed25519 Completo**: Utilizza la libreria originale orlp/ed25519 per piena compatibilita con MeshCore
- **ADVERT Firmati**: Trasmette e verifica pacchetti ADVERT con firma crittografica
- **Ripetitore FLOOD**: Inoltra automaticamente i pacchetti con deduplicazione
- **Sincronizzazione Tempo**: Sincronizza automaticamente l'orario da ADVERT ricevuti
- **Persistenza Identita**: Salva chiavi e configurazione in EEPROM

## Compatibilita MeshCore

Il firmware implementa il protocollo MeshCore standard:

| Parametro | Valore |
|-----------|--------|
| Frequenza | 869.618 MHz (EU868) |
| Bandwidth | 62.5 kHz |
| Spreading Factor | 8 |
| Coding Rate | 4/8 |
| Sync Word | 0x12 |
| Preamble | 8 simboli |

### Formato ADVERT

```
[PublicKey:32][Timestamp:4][Signature:64][Appdata:variabile]
```

Dati firmati: `PublicKey + Timestamp + Appdata`

### Formato Appdata

```
[Flags:1][Name:0-32][Location:0-8]
```

Flags byte:
- Bit 0-3: Tipo nodo (0x02 = Repeater)
- Bit 4: Ha location
- Bit 7: Ha nome

## Struttura Progetto

```
ed25519_test/
├── platformio.ini          # Configurazione PlatformIO
├── README.md               # Questa documentazione
├── lib/
│   └── ed25519/
│       └── src/            # Libreria orlp/ed25519 originale
│           ├── ed25519.h
│           ├── fe.c, ge.c, sc.c
│           ├── sha512.c
│           ├── keypair.c
│           ├── sign.c
│           ├── verify.c
│           └── precomp_data.h
└── src/
    ├── main.cpp            # Firmware principale
    └── mesh/
        ├── Identity.h      # Gestione identita Ed25519
        ├── Packet.h        # Struttura pacchetti MeshCore
        ├── Radio.h         # Interfaccia LoRa SX1262
        ├── Advert.h        # Gestione ADVERT TX/RX
        └── Repeater.h      # Ripetizione pacchetti
```

## Compilazione

```bash
# Compilare
pio run

# Caricare su CubeCell
pio run -t upload

# Monitor seriale
pio device monitor
```

## Comandi Seriali

| Comando | Descrizione |
|---------|-------------|
| `status` | Mostra stato del nodo |
| `advert` | Invia ADVERT manualmente |
| `stats` | Statistiche ripetitore |
| `nodes` | Lista nodi conosciuti |
| `nodetype chat` | Cambia tipo a CHAT |
| `nodetype repeater` | Cambia tipo a REPEATER |
| `passwd` | Mostra password attuali |
| `passwd admin <pwd>` | Imposta password admin |
| `passwd guest <pwd>` | Imposta password guest |
| `newid` | Genera nuova identita |
| `reboot` | Riavvia il dispositivo |
| `help` | Mostra aiuto |

## Test di Avvio

All'avvio il firmware esegue test automatici:

1. **RFC 8032 verify**: Verifica vettore test Ed25519 standard
2. **Identity init**: Carica/genera identita
3. **Sign/Verify**: Test firma e verifica
4. **ADVERT build**: Test costruzione pacchetto ADVERT

Output atteso:
```
=== Startup Tests ===

RFC 8032 verify: PASS
Identity init: PASS
Sign/Verify: PASS
ADVERT build: PASS
```

## Utilizzo

1. **Prima esecuzione**: Il firmware genera automaticamente una nuova identita Ed25519
2. **Sincronizzazione**: Attende un ADVERT da un altro nodo MeshCore per sincronizzare l'orario
3. **Trasmissione**: Una volta sincronizzato, invia ADVERT ogni 5 minuti
4. **Ripetizione**: Inoltra automaticamente pacchetti FLOOD ricevuti

### Sincronizzazione Manuale

Se non ci sono altri nodi MeshCore disponibili:
```
time 1737312000
```
(Sostituire con timestamp Unix corrente)

## Risorse

- Flash: 92.3% (120956 / 131072 bytes)
- RAM: 20.1% (3296 / 16384 bytes)

## Dipendenze

- [RadioLib](https://github.com/jgromes/RadioLib) v6.6.0
- [orlp/ed25519](https://github.com/orlp/ed25519) (incluso in lib/)

## Note Tecniche

### Perche orlp/ed25519?

La libreria orlp/ed25519 e stata scelta perche:
- Passa tutti i vettori test RFC 8032
- Compatibile al 100% con le firme MeshCore
- Funziona correttamente su architettura ASR6501 (CubeCell)

### Deduplicazione

Il ripetitore usa un hash FNV-1a dei pacchetti con cache di 32 entry e timeout di 30 secondi per evitare loop di ripetizione.

### Fair Channel Access

Il ritardo di trasmissione e calcolato in base al SNR: nodi con segnale migliore attendono piu a lungo, dando priorita a nodi piu lontani.

## Funzionalita in Sviluppo

### ANON_REQ / Telemetry Login (NON FUNZIONANTE)

Il login remoto tramite app MeshCore (ANON_REQ) e' implementato ma **non ancora funzionante**.

**Problema noto**: L'app MeshCore invia ANON_REQ con `destHash` errato (invia al nodo del telefono invece che al repeater). I pacchetti ricevuti hanno anche lunghezza insufficiente (19 bytes invece di 51+).

**Stato**: In attesa di debug. Il codice per processare ANON_REQ e' presente in `main.cpp`:
- `processAnonRequest()` - Decripta e verifica login
- `sendLoginResponse()` - Invia risposta criptata
- `SessionManager` - Gestione sessioni e password

**Password di default**:
- Admin: `admin`
- Guest: `guest`

## Licenza

MIT License

## Autore

Sviluppato per compatibilita con [MeshCore](https://github.com/ripplebiz/MeshCore)
