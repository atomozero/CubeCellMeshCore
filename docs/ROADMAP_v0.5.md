# CubeCellMeshCore v0.5 - Roadmap Feature Analysis

## Stato risorse hardware (HTCC-AB01)

### Flash (131,072 bytes totali)
- **Pre-Fase 0**: 128,728 B usati (98.2%) - 2,344 B liberi
- **Post-Fase 0**: 128,280 B usati (97.9%) - **2,792 B liberi**
- **Critico**: ogni feature deve essere misurata in bytes. Un `snprintf` costa ~200B, un `strcmp` ~20B.

### RAM (16,384 bytes totali)
- **Pre-Fase 0**: 8,136 B statici (49.7%)
- **Post-Fase 0**: 7,616 B statici (46.5%) - **8,768 B liberi**
- **Principali consumatori**:
  - TxQueue (4 x MCPacket): ~1,052 B
  - SessionManager (8 sessioni): ~640 B
  - ContactManager (8 contatti): ~560 B
  - SeenNodesTracker (16 nodi): ~352 B
  - PacketIdCache (32 entry): ~128 B
  - NeighbourTracker (50 entry): ~600 B
  - PacketLogger (32 entry): ~384 B

### EEPROM (512 bytes totali)
```
Offset  Dim   Contenuto
------  ----  ---------
0       110   NodeConfig (password, alert, report dest)
128     132   Identity (chiavi Ed25519, nome, location)
280     60    PersistentStats (contatori lifetime)
340     172   ** LIBERI **
```
- **Liberi**: 172 bytes - sufficienti per 2-3 messaggi piccoli o config aggiuntiva

---

## Feature 1: Store-and-Forward Mailbox

### Concetto
Il repeater memorizza messaggi destinati a nodi offline e li riconsegna quando il nodo torna visibile (riceve un ADVERT o un pacchetto dal nodo).

### Analisi fattibilita'

**Flash necessaria**: ~1,500-2,500 bytes
- Logica store: ~400B (check destinatario offline, salva in EEPROM)
- Logica forward: ~400B (check nodo tornato online, ritrasmetti)
- Comandi seriali/remote CLI: ~300B
- Gestione EEPROM mailbox: ~400B
- Totale realistico: **~1,500B minimo** con codice compatto

**RAM necessaria**: ~50-100 bytes (puntatori EEPROM, contatori)
- I messaggi stanno in EEPROM, non in RAM
- Serve solo un flag "mailbox non vuota" e indice

**EEPROM necessaria**: 172 bytes disponibili
- Header mailbox: 8 bytes (magic, count, write_idx)
- Per messaggio: ~80 bytes (dest_hash:1 + src_hash:1 + timestamp:4 + ttl:1 + payload_len:1 + payload:64 + flags:1 + reserved:7)
- **Con 172 bytes**: header(8) + 2 messaggi(160) = 168 bytes -- **SI PUO' FARE**
- Alternativa: payload 48 bytes = header(8) + 2 messaggi(120) + margine

**Compatibilita' MeshCore**: ALTA
- Usa pacchetti PLAIN/DIRECT standard gia' esistenti
- Il nodo mittente non sa che c'e' store-and-forward, funziona trasparente
- La riconsegna usa il path costruito dall'ADVERT del nodo destinatario

### Vincoli e problemi
1. **Solo 2 messaggi** con 172 bytes EEPROM - e' poco ma unico
2. **Nessun ACK end-to-end** - non si sa se il messaggio e' stato letto
3. **Flash critica** - serve tagliare ~1.5KB da qualche parte
4. **Scadenza messaggi** - serve TTL (es. 24h) per non intasare
5. **Sicurezza** - i messaggi in EEPROM sono in chiaro (non cifrati a riposo)

### Cosa serve tagliare per fare spazio Flash
| Candidato | Risparmio stimato | Impatto |
|-----------|-------------------|---------|
| PacketLogger (32 entry) | ~400-600B Flash + 384B RAM | Poco usato |
| mcPayloadTypeName + mcRouteTypeName | ~200B | Solo debug |
| ENABLE_BATTDEBUG code | ~200B | Solo dev |
| Ridurre MC_MAX_SEEN_NODES da 16 a 12 | ~100B RAM | Minimo |
| Compattare stringhe serial commands | ~300-500B | Verbose |

### Verdetto: FATTIBILE con sacrifici
- 2 messaggi in EEPROM
- Serve rimuovere PacketLogger e compattare stringhe per liberare ~1.5KB Flash
- Valore altissimo: nessun repeater MeshCore su hardware piccolo lo fa

---

## Feature 3: Mesh Health Monitor

### Concetto
Il repeater monitora la salute della rete: nodi che spariscono, degradamento SNR nel tempo, packet loss. Invia alert automatici quando rileva anomalie.

### Analisi fattibilita'

**Flash necessaria**: ~800-1,200 bytes
- Check nodo scomparso (gia' hai SeenNodesTracker): ~200B
- Calcolo SNR trend (EMA gia' usata in radioStats): ~150B
- Logica alert (hai gia' sendNodeAlert): ~100B (solo wrapper)
- Comandi CLI healthcheck: ~300B
- Totale realistico: **~800B**

**RAM necessaria**: ~100-200 bytes
- Per-node SNR history: 16 nodi x 4 bytes (snr_avg + last_alert_time ridotto) = ~64B
- Soglie configurabili: ~20B
- Flag e timer: ~30B

**EEPROM necessaria**: 0-20 bytes
- Opzionale: soglie custom salvate (threshold_snr, timeout_offline)
- Puo' usare i 4 bytes `reserved` in NodeConfig

**Compatibilita' MeshCore**: ALTA
- Gli alert usano il sistema gia' esistente (sendNodeAlert)
- Non modifica il protocollo
- Le metriche sono esposte via remote CLI (standard)

### Logica proposta
```
Ogni 60 secondi:
  Per ogni SeenNode:
    1. Se lastSeen > OFFLINE_THRESHOLD (es. 30 min):
       -> Alert "Nodo XX offline da N min"
       -> Flag per non re-alertare (1 alert per nodo per evento)
    2. Se SNR medio calato > 6dB rispetto alla media storica:
       -> Alert "Link XX degradato: SNR -XdB"
    3. Se pktCount/tempo < soglia minima:
       -> Alert "Nodo XX: traffico anomalo"
```

### Infrastruttura gia' esistente
- `SeenNodesTracker` con hash, RSSI, SNR, lastSeen, pktCount
- `sendNodeAlert()` gia' funzionante
- `alertEnabled` + `alertDestPubKey` gia' in EEPROM
- Noise floor EMA gia' implementata in `RepeaterHelper::updateRadioStats`

### Vincoli e problemi
1. **Flash**: ~800B e' fattibile ma stretta con store-and-forward
2. **Rate limiting alert**: non bombardare il proprietario (max 1 alert/nodo/30min)
3. **False positive**: un nodo che va in deep sleep non e' "offline"
4. **No SNR storico persistente**: si perde al reboot (accettabile)

### Verdetto: MOLTO FATTIBILE
- Basso costo in risorse, alto valore
- 70% dell'infrastruttura gia' esiste
- Puo' coesistere con store-and-forward se si e' efficienti

---

## Feature 9: OTA Configuration via MeshCore App

### Concetto
Configurazione completa del repeater dall'app MeshCore senza cavo seriale, tramite remote CLI cifrata gia' esistente.

### Analisi fattibilita'

**Nota importante**: questa feature e' in gran parte GIA' IMPLEMENTATA.

### Comandi remote CLI gia' funzionanti (via processRemoteCommand)
| Comando | Tipo | Stato |
|---------|------|-------|
| `status` | Read | OK |
| `stats` | Read | OK |
| `time` | Read | OK |
| `telemetry` | Read | OK |
| `nodes` | Read | OK |
| `neighbours` | Read | OK |
| `identity` | Read | OK |
| `location` | Read | OK |
| `radio` | Read | OK |
| `radiostats` | Read | OK |
| `packetstats` | Read | OK |
| `lifetime` | Read | OK |
| `repeat` | Read | OK |
| `advert interval` | Read | OK |
| `set repeat on/off` | Admin | OK |
| `set flood.max N` | Admin | OK |
| `set password X` | Admin | OK |
| `set guest X` | Admin | OK |
| `name X` | Admin | OK |
| `location LAT LON` | Admin | OK |
| `location clear` | Admin | OK |
| `advert` | Admin | OK |
| `advert interval N` | Admin | OK |
| `ping / ping XX` | Admin | OK |
| `trace XX` | Admin | OK |
| `rxboost on/off` | Admin | OK |
| `save` | Admin | OK |
| `reset` | Admin | OK |
| `reboot` | Admin | OK |
| `help` | Read | OK |

### Comandi MANCANTI (presenti in serial ma non in remote CLI)
| Comando | Costo Flash | Priorita' |
|---------|-------------|-----------|
| `sleep on/off` | ~60B | ALTA - gestione power remota |
| `nodetype chat/repeater` | ~80B | MEDIA - raramente cambiato |
| `ratelimit on/off/reset` | ~100B | ALTA - sicurezza remota |
| `alert on/off/dest/clear` | ~200B | ALTA - gestione alert remota |
| `newid` | ~40B | BASSA - pericoloso da remoto |
| `mode 0/1/2` | ~60B | MEDIA - power mode remoto |
| `tempradio` | ~150B | MEDIA - debug remoto |
| `savestats` | ~30B | BASSA - auto-save gia' attivo |

**Flash necessaria per completare**: ~500-700 bytes

**RAM necessaria**: 0 (usa buffer response esistente da 96B)

**EEPROM necessaria**: 0 (salva nelle strutture esistenti)

### Vincolo principale: buffer response 96 bytes
- Alcuni comandi producono output lungo (es. `nodes` con 16 nodi)
- Gia' gestito con troncamento nel codice esistente
- Per comandi lunghi: paginazione (es. `nodes 0`, `nodes 8`)

### Verdetto: QUASI GRATIS
- 80% gia' fatto
- ~500B Flash per completare tutti i comandi mancanti
- Nessun impatto su RAM o EEPROM
- Altissimo valore per l'utente

---

## Analisi combinata: tutte e 3 insieme?

### Budget Flash

| Momento | Flash usata | Libera | Note |
|---------|-------------|--------|------|
| Baseline v0.4.0 | 128,728 B | 2,344 B | |
| Post Fase 0 | 128,280 B | 2,792 B | -448 B recuperati |
| Post Fase 1 (OTA) | 129,304 B | **1,768 B** | +1,024 B per 15 nuovi comandi |

| Feature | Flash stimata | Flash reale | RAM | EEPROM |
|---------|---------------|-------------|-----|--------|
| OTA Config completa | ~500B | **1,024B** | 0B | 0B |
| Health Monitor | ~800B | **1,120B** | 64B | 0B |
| Store-and-Forward | ~1,500B | TBD | ~100B | ~168B |

---

## Fase 0: Preparazione (COMPLETATA)

### Risultati misurati

| Azione | Flash | RAM | Note |
|--------|-------|-----|------|
| PacketLogger dietro #ifdef | -232 B | -520 B | ENABLE_PACKET_LOG, default off |
| Rimuovere type name helpers | -208 B | 0 B | Log ora usa formato numerico r%d t%d |
| isPubKeySet() helper | -8 B | 0 B | 9 loop sostituiti con funzione |
| ENABLE_CRYPTO_TESTS | 0 B | 0 B | Gia' dietro ifdef, non compilato |
| **Totale Fase 0** | **-448 B** | **-520 B** | |

**Stato post-Fase 0**: Flash 128,280/131,072 (97.9%) - **2,792 B liberi**

## Fase 1: OTA Config completa (COMPLETATA)

### Comandi remote CLI aggiunti
- `sleep on/off` - gestione deep sleep da remoto
- `ratelimit on/off/reset` - controllo rate limiting
- `ratelimit` (read) - statistiche rate limiting
- `alert on/off/clear` - gestione alert da remoto
- `alert dest <name>` - impostare destinazione alert
- `alert` (read) - stato alert
- `mode 0/1/2` - cambio power mode
- `power` (read) - stato power/sleep/rxboost

**Costo reale**: 1,024 B Flash, 0 RAM, 0 EEPROM
**Stato post-Fase 1**: Flash 129,304/131,072 (98.7%) - **1,768 B liberi**

## Fase 2: Mesh Health Monitor (COMPLETATA)

### Implementazione
- `snrAvg` EMA (7/8 + 1/8) aggiunto a SeenNode
- `offlineAlerted` flag per evitare alert ripetuti (reset quando nodo torna)
- `healthCheck()` chiamata ogni 60s nel loop, scorre SeenNodesTracker
- Alert automatico quando un nodo (con almeno 3 pacchetti visti) e' offline >30min
- Comando `health` disponibile sia via serial che via remote CLI
- Output: conteggio nodi, offline, SNR corrente vs media, tempo dall'ultimo pacchetto

### Alert tramite chat node impersonation
Il client MeshCore ignora i messaggi ricevuti da nodi di tipo repeater.
Soluzione: prima di inviare un alert, il repeater:
1. Cambia temporaneamente i flags a CHAT_NODE (0x81)
2. Invia un ADVERT flood (l'app lo registra come contatto)
3. Invia il messaggio cifrato (l'app lo mostra come messaggio normale)
4. Torna a REPEATER al prossimo ADVERT schedulato

Questo meccanismo e' usato sia da `healthCheck()` che da `alert test`.

**Costo reale**: 1,120 B Flash (+184B per chat node trick), 64 B RAM, 0 EEPROM
**Stato post-Fase 2**: Flash 130,360/131,072 (99.5%) - **712 B liberi**

### Fase 3: Store-and-Forward Mailbox (v0.5.0) - Sforzo ALTO
1. Struttura `MailboxEntry` in EEPROM (offset 340, 2 slot)
2. Hook in `processReceivedPacket`: se dest offline, salva
3. Hook in ADVERT RX / pacchetto RX: se mittente torna, consegna
4. Comandi: `mailbox` (stato), `mailbox clear`
5. TTL 24h, auto-cleanup
6. ~1,500B Flash, 100B RAM, 168B EEPROM

### Ordine consigliato: Fase 0 -> 1 -> 2 -> 3
- Ogni fase e' indipendente e rilasciabile
- Si misura Flash reale dopo ogni fase prima di procedere
- Se lo spazio finisce, Fase 3 puo' essere esclusa con #ifdef

---

## Rischi principali

| Rischio | Probabilita' | Mitigazione |
|---------|--------------|-------------|
| Flash insufficiente per fase 3 | MEDIA | Misurare dopo fase 2; usare #ifdef |
| False positive health monitor | ALTA | Soglie conservative, cooldown 30min |
| Mailbox EEPROM corruption | BASSA | Magic number + CRC (come PersistentStats) |
| Overflow buffer response 96B | MEDIA | Troncamento gia' gestito; paginazione |
| Messaggi mailbox non cifrati in EEPROM | BASSA | Accettabile: EEPROM non accessibile da remoto |

---

## Metriche di successo

- **OTA Config**: tutti i comandi serial disponibili anche via remote CLI
- **Health Monitor**: alert entro 5 minuti dalla scomparsa di un nodo
- **Mailbox**: messaggio consegnato entro 30s dal ritorno del nodo destinatario
- **Stabilita'**: zero regressioni sui 66 test firmware esistenti
- **Flash**: margine residuo > 500 bytes dopo tutte le feature
