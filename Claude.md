# M5_3 — Spotify Streamer auf M5Stack Tab5

Dies ist die zentrale Steuerungsdatei. Bei Konflikten gilt: diese Datei → `M5Tab5-UserDemo-Reference/` Konventionen → ESP-IDF Defaults.

---

## 1. Projektziel

**Spotify-Streamer mit M5Stack Tab5 als Hardware.**

- **Audio-Output aktuell:** ES8388 DAC → eingebauter Speaker (I2S, 48 kHz, 16-bit)
- **Audio-Output Ziel:** USB-Audio + externer DAC → 3.5mm Klinke (Phase 7)
- **Steuerung:** Touch (5" Display, ST7123)
- **UI:** Native Player UI (Track/Artist/Progress/Transport/Volume) — implementiert in Commit `21240cd`
- **Stack:** ESP-IDF v5.4.2 + LVGL + CSpot (Submodule)

### Realer Pain Point

**Skip-Forward/Backward zwischen Songs ist langsam oder schlägt fehl.**

Zwei Skip-Fix-Versuche Ende April 2026 mussten reverted werden:
- `a8a047c` — `submodule(cspot): bump 39203d0 — MAX_TRACKS_PRELOAD=2` → revert `968cc74`
- `8cc311a` — `submodule(cspot): bump 51e097a — fast skip patches` → revert `04403bd`

**Beide ohne dokumentierten Postmortem.** Vermutung: PRELOAD=2 → DMA Fragmentation auf SPIRAM.
Aktueller Stand: Baseline mit `MAX_TRACKS_PRELOAD=1`, CSpot @ `7dfafe6`.

---

## 2. Top-Level Rules (nicht verhandelbar)

1. **Keine Improvisation.** Funktionierender Code wird 1:1 übernommen, niemals "vereinfacht". Empirisch bewiesen schlägt logisch plausibel — immer.
2. **Jede Änderung braucht eine Referenz-Quelle.** Datei:Zeile zitieren, sonst kein Code.
3. **Bei 2+ Fehlschlägen: STOP, User fragen.** Keine Patch-Stapel.
4. **Question == Read-Only.** Fragen nicht mit Code-Änderungen beantworten.
5. **Source Rule:** Vor jeder Aktion: *"Quelle: [X]. Ich tue [Y] weil [Z]."* Keine Quelle = keine Aktion.
6. **Bekannte Anti-Patterns niemals ohne Postmortem retten.** Insbesondere `MAX_TRACKS_PRELOAD>1` ist gesperrt bis dokumentiert ist warum `a8a047c` und `8cc311a` crashten.
7. **`Claude.md` und `M5Tab5-UserDemo-Reference/` niemals löschen** — bei Bedarf aktualisieren.

---

## 3. Architektur

```
M5_3/
├── Claude.md                              ← diese Datei
├── platforms/tab5/                        ← AKTIVES ESP-IDF Multi-Komponenten-Build
│   └── main/app_main.cpp                  ← ECHTER Entry-Point (extern "C" void app_main())
├── app/                                   ← Plattform-agnostischer App-Layer
│   ├── apps/app_launcher                  ← UI-Launcher
│   ├── apps/app_startup_anim              ← Boot-Animation
│   └── apps/app_imu_visualizer            ← (nur Scaffolding)
├── M5Tab5-UserDemo-Reference/             ← UI-Demo + CSpot Component
│   └── platforms/tab5/components/cspot/
│       └── cspot/                         ← CSpot Submodule (@ 7dfafe6)
│           ├── include/                   ← TrackQueue.h, SpircHandler.h, etc.
│           └── src/                       ← Implementierung
├── flash_env/                             ← Eigener Python-venv für esptool
├── monitor_serial.py                      ← Primary Serial-Reader (siehe §8)
├── build.sh                               ← Konsolidierter Build-Wrapper (siehe §8)
└── sessions/                              ← Strukturierte Test-Session-Logs (siehe §9)
```

### Entry-Point Flow

```
app_main.cpp (platforms/tab5/main/)
  → app::Init()                           [app/app.cpp]
    → HalEsp32 Injection                  [Hardware-Abstraction für ESP32-P4]
      → CSpot Mercury Session Init        [WiFi-Login, Spotify-Auth]
        → TrackQueue / TrackPlayer        [Track-Handling, Pre-Loading]
          → CDNAudioFile (Vorbis Stream)  [HTTP-Streaming von CDN]
            → I2S → ES8388 DAC → Speaker
```

### Tote Reste (NICHT BENUTZEN)

- `src/main.cpp` — alter PlatformIO-Rest, **DEPRECATED**
- `platformio.ini` — nicht aktiv, ESP-IDF wird genutzt

---

## 4. Workflow (zwingend, in Reihenfolge)

1. **Quelle finden** — Referenz-Code in CSpot, `M5Tab5-UserDemo-Reference/`, ESP-IDF Examples (`~/esp/esp-idf-v5.4.2-tab5/examples/`)
2. **Vollständig lesen** — nicht überfliegen, nicht raten
3. **Pre-Flight-Checkliste** ausfüllen (siehe §5)
4. **Plan schreiben** → User-Freigabe (bei Audio/Buffer/CSpot-Änderungen zwingend Plan Mode)
5. **Code 1:1 übernehmen** aus Referenz
6. **`./build.sh`** → Build + Flash + Serial-Monitor in einem Schritt
7. **Session-Logs prüfen** unter `sessions/latest/` (verdict.json + serial.log)
8. **Bericht** vor jedem nächsten Schritt

---

## 5. Pre-Flight-Checkliste

Plan ohne alle 5 Säulen verifiziert = strukturell defekt = verboten.

- [ ] **Init:** Welche Peripherie wird initialisiert? In welcher Reihenfolge? (PI4IOE muss VOR Display/Audio/WiFi 5V freischalten!)
- [ ] **Output:** Audio-Pfad: I2S-Config? DMA-Buffer-Größe? PCM-Sample-Rate?
- [ ] **Input:** Touch-Driver an LVGL gebunden? Sample-Rate für Mikrofon (falls relevant)?
- [ ] **Lifecycle:** Cleanup, Mutex, Memory? FreeRTOS-Tasks? Wer freed Buffer wann?
- [ ] **Audio-spezifisch:** DMA-Allocation (heap_caps_malloc mit MALLOC_CAP_DMA), Buffer-Recycling oder pro Track neu allokieren? SPIRAM vs Internal RAM?

---

## 6. Bekannte Anti-Patterns (NICHT machen)

**Gesperrte Patterns** sind nur dann zu öffnen, wenn ein Postmortem unter `POSTMORTEMS.md` dokumentiert ist und alle dort genannten Pre-Conditions erfüllt sind. Bevor irgendwas in dieser Tabelle erneut versucht wird: **STOP, lies POSTMORTEMS.md, eskaliere an User.**

| Anti-Pattern | Folge | Status |
|---|---|---|
| `MAX_TRACKS_PRELOAD>1` ohne DMA-Buffer-Pool | Crash / Boot-Loop (vermutlich DMA-Fragmentation) | **GESPERRT** — siehe `POSTMORTEMS.md#max-tracks-preload-2` |
| ESP-IDF v5.5+ kompilieren | Illegal Instruction beim Boot (Chip Rev 1.3) | **GESPERRT** — nur v5.4.2 nutzen |
| esptool < v4.11 | `Invalid head of packet 0x45`, Chip wird als v0.0 erkannt | **GESPERRT** |
| PlatformIO / Arduino / `M5Unified` | Display bleibt schwarz (PI4IOE wird nicht initialisiert) | **GESPERRT** |
| Bootloader auf `0x0000` flashen | Endlosloop `invalid header: 0x...` | **FALSCH** — ESP32-P4 will Bootloader auf `0x2000` |
| `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` | Linker-Error | **GESPERRT** |
| `esp_codec_dev >= 1.4.0` | API breaking change | Pin auf `<1.4.0` |
| UI-Update aus Background-Task ohne Mutex | Race / Crash | `bsp_display_lock(0)` ... `bsp_display_unlock()` |
| Block in `lv_event_cb_t` | UI-Freeze | FreeRTOS-Queue/Flag, Verarbeitung in eigener Task |
| NVS-Calls im LVGL-Callback | Brownout + Boot-Loop | NVS in eigene Task |

---

## 7. Hardware (verifiziert 2026-04-15)

- **SoC:** ESP32-P4, **Chip Rev v1.3** (frühes Engineering Sample, "Frankenstein-Batch" Post-Okt 2025)
- **CPU:** 360 MHz, Dual-Core
- **Crystal:** 40 MHz
- **Flash:** 16 MB, SPI Mode QIO, 80 MHz
- **PSRAM:** 32 MB HEX-PSRAM (AP, Generation 4, 256 Mbit), 200 MHz, X16 Mode
- **MAC:** `30:ed:a0:ea:8e:2f`
- **USB-Modus:** USB-Serial/JTAG

### Display & Touch
- **Controller:** ST7123 (Display + Touch Kombi-IC), I2C `0x55`
- **Interface:** MIPI DSI, 720 × 1280 px
- **LCD ID:** `80 A0 FB`
- **Touch:** Firmware v3 (1.71.1.3), max 10 Touchpunkte
- **Reset GPIO:** 23, **Backlight GPIO:** 22 (LEDC)

### Audio
- **DAC (Speaker):** ES8388, I2C `0x10`
- **ADC (Mics):** ES7210, 4× MIC, TDM, 48 kHz, 16-bit, I2C `0x40`
- **I2S:** STD Mode, 32/16-bit, 48 kHz

### IMU & Power
- **IMU:** BMI270, I2C `0x68`
- **Power Monitor:** INA226, I2C `0x41`
- **RTC:** RX8130, I2C `0x32`
- **IO-Expander:** PI4IOE, I2C `0x43` — **Master-Schalter für EXT5V_EN, MUSS zuerst initialisiert werden**

### I2C Bus Scan (vollständig)

| Adresse | Gerät |
|---|---|
| `0x10` | ES8388 (Audio DAC) |
| `0x28` | ? |
| `0x32` | RX8130 (RTC) |
| `0x36` | ? |
| `0x40` | ES7210 (Audio ADC) |
| `0x41` | INA226 (Power Monitor) |
| `0x43` | PI4IOE (IO-Expander) |
| `0x44` | ? |
| `0x55` | ST7123 (Display + Touch) |
| `0x68` | BMI270 (IMU) |

### WiFi / Connectivity
- **WiFi-Chip:** ESP32-C6 (Slave via SDIO), Board Type 13, Chip ID 12
- **SDIO:** 4-bit, 40 MHz, CLK[12] CMD[13] D0[11] D1[10] D2[9] D3[8], Slave_Reset[15]

### Partition Table (Factory)

| Label | Type | Offset | Größe |
|---|---|---|---|
| nvs | WiFi data | 0x009000 | 24 KB |
| phy_init | RF data | 0x00F000 | 4 KB |
| factory | App | 0x010000 | 10 MB |
| human_face_det | Data | 0xA10000 | 400 KB |
| storage | Data | 0xA74000 | 2 MB |

---

## 8. Tools

### Build & Flash

```bash
./build.sh                # Build + Flash + Monitor (Default-Workflow)
./build.sh --silent       # Output in sessions/latest/commands.log
./build.sh --no-flash     # nur Build
./build.sh --no-monitor   # Build + Flash, kein Serial-Read
./build.sh --skip-test    # Phase 4: 10× Skip-Test mit Statistik
./build.sh --webcam       # Diagnose-Snapshot via Anker-Webcam
```

### Serial-Reader

```bash
./monitor_serial.py       # 2 Mbaud, dynamisches Port-Globbing, ms-Zeitstempel
```

### ESP-IDF Setup

```bash
source ~/esp/esp-idf-v5.4.2-tab5/export.sh
```

### Flash-Befehl manuell (falls build.sh nicht geht)

ESP32-P4: Bootloader bei **`0x2000`**, NICHT bei `0x0000`.

```bash
esptool.py --chip esp32p4 --port /dev/cu.usbmodem* --baud 921600 \
  --before=default_reset --after=hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x2000  build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/m5stack_tab5.bin
```

### Recovery (offizielle Factory-Binary)

```bash
# Pfad zur Factory-Binary
FACTORY=/Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/M5Tab5-UserDemo-Reference/binaries/official_factory_v0.2_rev1.3.bin

esptool.py --chip esp32p4 --port /dev/cu.usbmodem* --baud 115200 \
  write_flash --erase-all 0x0000 $FACTORY
```

SHA256 der Factory: `f3e0c860001c8eeda4fc4c3664077f408782ff213a3138e32c9f489f0d158827`

---

## 9. Session-Logging

Pro `build.sh`-Run ein eigenes Verzeichnis unter `M5_3/sessions/`:

```
sessions/2026-05-02_143012_a1b2/
├── meta.json          (Session-ID, Commit-Hash, sdkconfig-Hash, Sentinel)
├── commands.log       (build.sh Aktionen mit Zeitstempeln)
├── serial.log         (raw Serial mit ms-Zeitstempel pro Zeile)
├── serial_decoded.log (Backtraces via addr2line aufgelöst)
├── webcam.jpg         (nur bei FAIL oder --webcam)
└── verdict.json       (PASS/FAIL, Timing, Skip-Latenz)
```

Symlink `sessions/latest` → neueste Session.
Retention: letzte 30 Sessions.

### CSpot-Sentinels

Im CSpot-Submodule sind ESP_LOGI-Marker eingebaut (Phase 3.2):

```
[BOOT] tab5_spotify v0.X
[CSPOT_READY]                      Mercury-Session aufgebaut, Login OK
[TRACK_LOAD start uri=spotify:...]
[TRACK_LOAD ready in Xms]
[SKIP_REQ direction=fwd|back]
[SKIP_DONE in Xms]
[CDN_REQ bytes=N]
[CDN_OK got=N in=Xms]
[DMA_FREE N]                       periodisch alle 5s
```

`build.sh` extrahiert diese in `verdict.json` für maschinenlesbare Auswertung.

---

## 10. Code-Qualität

- Modernes C++ (RAII, `std::unique_ptr`/`std::shared_ptr` statt raw `new`/`delete`)
- Const-Correctness konsequent
- Keine globalen Variablen — Dependency Injection oder bewusste Singletons
- Keine Magic Numbers — Pins, I2C-Adressen, Konstanten in `include/config.h` oder als `static constexpr`
- Hardware/Network-Logik in eigenen Modulen, nie im UI-Task

---

## 11. Git & Commits

- **Micro-Commits:** Jeder verifizierte Zustand = Commit (z.B. "Audio spielt 30s ohne Glitch" ist ein Commit-Wert)
- **Go-Lock:** Vor großen Änderungen aktuellen Stand committen. Bricht der Build → `git restore` und neuer Versuch
- **Commit-Format:** `<scope>: <was funktioniert jetzt>` — niemals "fix things" oder "wip"
- **Submodule-Bumps:** Nur mit Postmortem des Vorgängerstands. CSpot-Bump = User-Eskalation Pflicht
- **Reverts:** Begründung als Commit-Body, nicht nur "Revert X" — was war das Symptom, was die Hypothese?

---

## 12. Sprache

- Code, Funktionsnamen, Inline-Comments → **Englisch**
- Chat → **Deutsch**

---

## 13. Hilfreiche Pfade

```
~/esp/esp-idf-v5.4.2-tab5/                              ESP-IDF v5.4.2 (PFLICHT)
~/esp/esp-idf-v5.4.2-tab5/examples/peripherals/i2s_*/   I2S-Referenz für Audio-Patterns
~/esp/esp-idf-v5.4.2-tab5/examples/peripherals/lcd/     Display-Referenz
~/esp/esp-idf-v5.4.2-tab5/examples/protocols/http_*/    HTTP-Client-Patterns (für CDN-Hypothesen)
~/esp/M5Tab5-UserDemo/                                  BSP-Referenz (separates Repo)
M5Tab5-UserDemo-Reference/platforms/tab5/components/cspot/cspot/  CSpot-Quellcode (Submodule)
```
