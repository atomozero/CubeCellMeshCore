# Correzione Compatibilità Crittografica MeshCore

## STATO ATTUALE: BLOCCATO - Memoria insufficiente

Il passaggio a chiave privata 64 byte richiede l'uso di `ed25519_sign` da orlp/ed25519,
che porta con sé `precomp_data.h` (97KB di tabelle precomputate). Questo causa overflow
della Flash memory (131KB disponibili, ~160KB richiesti).

### Stato memoria attuale (funzionante con 32-byte key):
```
Flash: 95.6% (125368 / 131072 bytes) - ~5.7KB liberi
RAM:   49.3% (8072 / 16384 bytes)
```

### Opzioni per risolvere:

1. **Usare una libreria Ed25519 più compatta** (es. μNaCl, TweetNaCl, Ed25519-donna)
   - TweetNaCl: ~6KB totali, ma più lento
   - Ed25519-donna: versioni con/senza precomp tables

2. **Rimuovere features dal firmware** per liberare ~30KB
   - Rimuovere telemetry handler
   - Rimuovere comandi seriali non essenziali
   - Usare SILENT mode

3. **Usare un chip con più Flash** (non praticabile per HTCC-AB01)
   - HTCC-AB02 ha stessa limitazione

4. **Implementare solo sign senza verify nel firmware**
   - Il progetto di riferimento non ha verify.c
   - Risparmia ~2-3KB ma perdiamo verifica firme RX

### Progetto di riferimento
https://github.com/ViezeVingertjes/Heltec-Cubecell-MeshCore-Repeater
- Usa stessa libreria orlp/ed25519
- NON include verify.c (non verifica firme ricevute)
- PRV_KEY_SIZE = 64 byte

---


## Problema Identificato

Le firme Ed25519 generate dal nostro repeater **non sono verificabili** dall'app MeshCore e viceversa.

### Causa Root

**Formato chiave privata incompatibile:**

| Libreria | Chiave Privata | Chiave Pubblica |
|----------|----------------|-----------------|
| Arduino Crypto Ed25519 | 32 byte (seed) | 32 byte |
| MeshCore (orlp/ed25519) | **64 byte** (SHA-512 del seed) | 32 byte |

La libreria Arduino Crypto usa il seed diretto come chiave privata, mentre MeshCore/orlp/ed25519 espande il seed tramite SHA-512 prima di usarlo.

### Come funziona orlp/ed25519

```c
void ed25519_create_keypair(public_key, private_key, seed) {
    sha512(seed, 32, private_key);  // seed 32B → private_key 64B
    private_key[0] &= 248;          // clamping
    private_key[31] &= 63;
    private_key[31] |= 64;
    // ... deriva public_key
}
```

La funzione `ed25519_sign` poi usa:
- `private_key[32:64]` per il primo hash (r value)
- `private_key[0:32]` per la moltiplicazione scalare

---

## Task da Completare

### TASK 1: Pulizia Codice Debug (liberare ~2KB Flash)

**File da pulire:**

1. **src/main.cpp** - Rimuovere:
   - [ ] Linea con `[SIG] RX ADVERT` (se ancora presente)
   - [ ] Eventuali altri `Serial.printf` di debug rimasti

2. **src/mesh/Advert.h** - Già pulito

3. **src/mesh/Crypto.h** - Verificare se ci sono debug

### TASK 2: Modificare NodeIdentity per chiave privata 64 byte

**File: src/mesh/Identity.h**

1. [ ] Cambiare `MC_PRIVATE_KEY_SIZE` da 32 a 64:
   ```cpp
   #define MC_PRIVATE_KEY_SIZE     64  // Era 32
   ```

2. [ ] Aggiornare struct `NodeIdentity`:
   ```cpp
   uint8_t privateKey[MC_PRIVATE_KEY_SIZE]; // Ora 64 byte
   ```

3. [ ] Aggiornare `IDENTITY_EEPROM_OFFSET` se necessario (la struct è più grande di 32 byte)

### TASK 3: Modificare generazione chiavi

**File: src/mesh/Identity.h**

1. [ ] Modificare `generate()` per usare `ed25519_create_keypair`:
   ```cpp
   bool generate() {
       // Genera seed random di 32 byte
       uint8_t seed[32];
       RNG.rand(seed, 32);

       // Genera keypair con orlp/ed25519
       // Questo crea private_key di 64 byte e public_key di 32 byte
       ed25519_create_keypair(identity.publicKey, identity.privateKey, seed);

       // ... resto del codice (nome, flags, etc.)
   }
   ```

2. [ ] Rimuovere chiamate a `Ed25519::generatePrivateKey` e `Ed25519::derivePublicKey`

### TASK 4: Modificare funzione sign()

**File: src/mesh/Identity.h**

1. [ ] Usare `ed25519_sign` da orlp/ed25519:
   ```cpp
   void sign(uint8_t* signature, const uint8_t* data, size_t len) {
       ed25519_sign(signature, data, len, identity.publicKey, identity.privateKey);
   }
   ```

### TASK 5: Verificare funzione verify()

**File: src/mesh/Identity.h**

1. [ ] Già usa `ed25519_verify` da orlp - OK

### TASK 6: Aggiornare migrazione EEPROM

**File: src/main.h e src/main.cpp**

1. [ ] Incrementare `EEPROM_VERSION` (da 4 a 5)
2. [ ] Nella migrazione, forzare rigenerazione identità (la vecchia chiave a 32 byte non è compatibile)

### TASK 7: Test

1. [ ] Compilare e verificare che entra in Flash
2. [ ] Caricare firmware
3. [ ] Verificare che genera nuova identità
4. [ ] Verificare che `[SIG] RX ADVERT: OK` per pacchetti da Andrea
5. [ ] Verificare che Andrea vede il repeater nell'app MeshCore
6. [ ] Testare `alert test` - Andrea dovrebbe ricevere il messaggio

---

## Codice di Riferimento

### Progetto di riferimento funzionante
https://github.com/ViezeVingertjes/Heltec-Cubecell-MeshCore-Repeater

Usa:
- `lib/ed25519/` - libreria orlp/ed25519
- `PRV_KEY_SIZE = 64`
- `PUB_KEY_SIZE = 32`

### Struttura firma MeshCore (confermata dal codice sorgente)

Dati firmati per ADVERT:
```
[PublicKey:32][Timestamp:4][Appdata:variabile]
```

Formato pacchetto ADVERT:
```
[PublicKey:32][Timestamp:4][Signature:64][Appdata:variabile]
```

---

## Note Importanti

1. **Dopo questa modifica, l'identità precedente sarà invalida** - il dispositivo genererà una nuova identità al primo avvio

2. **La libreria Arduino Crypto (rweather/Crypto) serve ancora** per:
   - RNG (generazione numeri random)
   - AES (crittografia messaggi)
   - SHA256 (hashing)

   NON rimuoverla dalle dipendenze.

3. **La libreria orlp/ed25519 in lib/ed25519/** sarà usata per:
   - Generazione keypair (`ed25519_create_keypair`)
   - Firma (`ed25519_sign`)
   - Verifica (`ed25519_verify`)
   - Key exchange (`ed25519_key_exchange`)

---

## Stato Attuale Memoria

```
Flash: 96.0% (125840 / 131072 bytes)
RAM:   49.3% (8072 / 16384 bytes)
```

Dopo la pulizia e le modifiche, dovremmo avere ~4-5KB di margine per future funzionalità.
