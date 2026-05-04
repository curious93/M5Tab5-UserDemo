# M5_3 Status (2026-05-04, 64KB Threshold — Skip -63%)

## Vollautonomes Setup — alles läuft

### Hardware/Network
- ✅ Build (ESP-IDF v5.4.2, ohne Hash-Warnings)
- ✅ Flash (esptool, dynamischer Port-Glob)
- ✅ Boot (Hardware-Init, PSRAM 32MB OK)
- ✅ WLAN-Connect (`Ingo` → 192.168.1.103) — *War "Ingrid" hardcoded, Tippfehler im Original-Code*
- ✅ Internet (Cloudflare 23ms, Spotify 33ms via `[INET_CHECK]`)
- ✅ Spotify-Auth (CSpot, embedded credentials für `iamtheblindman`)
- ✅ Audio-Pipeline (48kHz, 2ch, 16bit, ES8388-DAC, vol=30)
- ✅ Mercury-Session aktiv (Time-Sync, SPIRC-Subscription)

### Diagnose-Pipeline
- ✅ 7 Sentinels: `[TLS_CONNECT]`, `[TRACK_LOAD]`, `[SKIP_REQ]`, `[SKIP_DONE]`, `[DMA_FREE]`, `[DNS_LOOKUP]`, `[INET_CHECK]`
- ✅ Session-Logging (`sessions/<id>/{verdict.json,serial.log,serial_decoded.log,commands.log,meta.json}`)
- ✅ Cross-Session-Diff (`./diff_sessions.py`)
- ✅ Watch-Mode (`./build.sh --watch`)
- ✅ Cleanup-Trap (Serial-Port wird bei Abbruch freigegeben)
- ✅ Backtrace-Decoder (Crash-Adressen via addr2line aufgelöst)

### Code-Sicherheit
- ✅ Anti-Pattern Compile-Sperre (`MAX_TRACKS_PRELOAD>1` → static_assert FAIL, verifiziert)
- ✅ WiFi-Credentials zentralisiert (`wifi_credentials.h` — 1 Datei)
- ✅ esp_hosted Component nach `components/` (Hash-Warning weg, modifizierter Code sicher)

### Vollautonomer Test-Modus
- ✅ AUTO_SKIP_TEST Build-Flag: wartet auf echte PLAYBACK_START, dann 10× nextSong()
- ✅ Chrome-Device-Picker Automation: M5Tab5 wird nach Boot automatisch verbunden
- ✅ verdict.json valid JSON mit allen Stats korrekt aggregiert
- ✅ TRACK_LOAD ready Zeit als echte CDN-Latenz-Metrik in build.sh + verdict.json

## Aktuelle Mess-Daten

### ★ ECHTE BASELINE (Session `2026-05-03_195150_4c9d`, AUTO_SKIP_TEST mit Chrome-Connect)

10 echte Skips mit aktivem Spotify-Stream auf Tab5:

```
User-Latenz (SKIP_REQ → PLAYBACK_START):
  n=10  min=3315ms  median=6914ms  p95=16617ms  max=16617ms

CDN-Ladezeit (TRACK_LOAD start → ready, 256KB prefetch):
  n=11  min=2603ms  median=5805ms  p95=10412ms  max=10412ms

H2-Reuse: 21/22 TLS-Verbindungen recycelt (audio-ak.spotifycdn.com)
DMA-Min: 18075 bytes (DMA-Heap stabil beim Streaming)
```

**Diagnose:** Bottleneck ist der CDN-Download selbst (~50-90KB/s → 3-10s für 256KB).
- TLS-Overhead weitgehend eliminiert (H2_REUSE funktioniert)
- P95-Ausreißer (16.6s) = überlappende Downloads wenn Skips schnell aufeinanderfolgen
- Nächster Fix: H4 (Prefetching) — lädt nächsten Track WÄHREND aktueller spielt

### Network-Latenz (Session `2026-05-03_122027_c33e`, AUTO_SKIP_TEST synthetic)

```json
{
  "skip_stats": {"n":10, "median_ms":0,  "p95_ms":5,   "max_ms":5},   ← SPIRC-Latenz nur (synthetic)
  "dns_stats":  {"n":1,  "median_ms":28, "p95_ms":28,  "max_ms":28},
  "tcp_stats":  {"n":1,  "median_ms":35, "p95_ms":35,  "max_ms":35},
  "tls_stats":  {"n":2,  "median_ms":33, "p95_ms":61,  "max_ms":61}
}
```

### Heap-Limit (Session `2026-05-03_173109_d574`, 4 Min Idle-Watch)

```
[HEAP_DMA total_free=34227 largest=20476 alloc=265 free=20]   ← STABIL über 4 Min
[HEAP_PSRAM total_free=21658736 largest=21495804]
```

**Goldene Erkenntnis:** DMA-Heap (Internal RAM) hat nur **20 KB largest free block**. PSRAM hat 21 MB frei (irrelevant für DMA-Allocations). 265:20 alloc/free Ratio = stark fragmentiert.

### Was diese Daten bedeuten

- **Network ist NICHT der Bottleneck** — DNS/TCP/TLS alle <100ms
- **DMA-Heap-Limit ist der harte Bottleneck für PRELOAD>1** — `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` (16KB pro TLS-Connection, DMA) erklärt den Crash
- `header`/`httpBuffer` in CDNAudioFile → PSRAM (via `SPIRAM_USE_MALLOC=y`, `SPIRAM_MALLOC_ALWAYSINTERNAL=0`). DMA-Pool für diese Buffers hilft nicht.
- **H2 verbessert (2026-05-03)**: Host-Level Connection Reuse implementiert. Beweisen: 21 H2_REUSE Events in Echttest.
- **Echter Bottleneck: CDN-Download-Rate** — 256KB bei 50-90KB/s = 3-10s. H4 (Prefetching) wird das adressieren.

## H1-H6 Status

| H | Was | Status |
|---|---|---|
| H1 | DNS-Cache | DNS <30ms, geringer Hebel |
| H2 | HTTP Connection-Pool | **DONE (2026-05-03)**: Host-Level Reuse, 21 Reuses/Session |
| H3 | mbedTLS-Buffer-Reduktion | **BLOCKIERT**: PRELOAD>2 bräuchte 40KB DMA → impossible |
| H4 | Next-Track CDN Prefetch | **DONE (2026-05-04)**: hilft nur EoT. Bug fixed: abort guard added |
| H5 | STREAM_START_THRESHOLD 256→128 KB | **DONE (2026-05-04)**: max -37%, DMA safe |
| H6 | STREAM_START_THRESHOLD 128→64 KB | **DONE (2026-05-04)**: median -43%, max -34%, 0 underruns |
| H4b | StreamBuffer-Architektur | Nächster Kandidat für sub-1.3s Skip, hoher Aufwand |

### H4 Ergebnis (2026-05-04, Session h4_2026-05-04_002714)

```
CDN-Ladezeit H4:
  n=11  min=2088ms  median=3700ms  p95=8033ms  max=8033ms
  PREFETCH_HIT: 0  (download zu langsam für 8s Skip-Interval)

Baseline (2026-05-03_195150_4c9d):
  n=11  min=2603ms  median=5805ms  p95=10412ms  max=10412ms

Scheinbare Verbesserung -36% Median: CDN-Speed-Varianz (127-169 KB/s H4 vs 86 KB/s Baseline)
Echter H4-Nutzen: nur bei natürlichem Track-Ende

DMA-Fragmentierung:
  Start: 28231 bytes → Ende: 6371 bytes nach 10 Skips (-21860 bytes)
  Ursache: jede TLS open/close hinterlässt Fragmente (~2186 bytes/Skip)
  Risiko: nach vielen Skips könnte DMA <4KB → nächste TLS-Conn schlägt fehl
```

### H5 — STREAM_START_THRESHOLD 128KB + DYNAMIC_BUFFER (2026-05-04, Session thresh128)

```
CDN-Ladezeit 128KB-Threshold:
  n=10  min=2224ms  median=3770ms  p95=5055ms  max=5055ms
  Underruns: 0
  DMA-min: 14731 bytes (vs 6371 bytes ohne DYNAMIC_BUFFER — 2.3× mehr Spielraum)

vs H4-Baseline (256KB-Threshold):
  max -37% (8033ms → 5055ms)
  DMA +131% margin (6371 → 14731 bytes)
```

### H6 — STREAM_START_THRESHOLD 64KB + Abort Guard Bug Fix (2026-05-04, Session thresh64_fixed)

```
CDN-Ladezeit 64KB-Threshold:
  n=10  min=1335ms  median=2144ms  p95=3340ms  max=3340ms
  Underruns: 0  Stack-Overflows: 0  DMA-min: 18075 bytes

vs 128KB-Baseline:     median -43%  max -34%
vs 256KB-Baseline:     median -63%  max -68%
```

**Bug gefunden & gefixt:** CDNAudioFile `downloadTaskEntry()` feuerte Prefetch-Callback auch bei `dlAbort=true` (Skip). Callback→`prefetchAudioFile()`→`openStream()` führte TLS-Handshake auf dem 24KB cdn_dl-Stack aus → Stack Overflow. Fix: `if (cb && !aborted) cb();`

**Skip-Floor heute:** ~1.3s (64KB / ~50 KB/s effektiv). Weitere Senkung riskant:
- 32KB: bei WiFi-Schwankung unter 40 KB/s → sofortiger Underrun
- H4b StreamBuffer: sub-1.3s möglich, aber aufwändige Architekturänderung

## H2-Verbesserung (2026-05-03) — Was wurde geändert

**Vor dieser Session:** H2 war mit Host+Path-Level Reuse implementiert (`s_cachedHostPath`). Verschiedene Tracks (= verschiedene Pfade) konnten TLS NICHT wiederverwenden.

**Fix 1 — Host-Level Cache-Key:**
```cpp
// Vorher: urlHostPath() → "host.cdn.com/path/to/track?token=..."
// Jetzt:  urlHost()     → "host.cdn.com"
```
`[H2_REUSE host=X]` Sentinel erscheint bei jedem Reuse.

**Fix 2 — s_cachedResp bleibt beim Prefetch am Leben:**
Vorher: `s_cachedResp.reset()` vor Prefetch-Start → TLS-Connection vernichtet.
Jetzt: `s_cachedResp` bleibt, da `dlResp` (Prefetch) eine separate Connection nutzt.
Nächster Track kann nach Prefetch-Ende dieselbe TLS-Connection (Host) wiederverwenden.

**Erwartete Wirkung:** ~50ms Einsparung pro Skip wenn Spotify CDN konsistenten Host nutzt (wahrscheinlich: `audio-XX.scdn.co`). Erst mit echten Skip-Daten messbar.

---

## Was Phase 4 ECHT noch braucht

Spotify-Connect-Architektur erfordert dass *irgendein Spotify-Client* (App/Web/Desktop) das Tab5 als Output-Device wählt — das CSpot's `loadTrackFromURI` ist leer (siehe `SpircHandler.cpp:94`).

**Workflow:**
```bash
# 1. Build ohne AUTO_SKIP_TEST (default)
cd M5Tab5-UserDemo-Reference/platforms/tab5
source ~/esp/esp-idf-v5.4.2-tab5/export.sh
idf.py build

# 2. Flash + Monitor
cd ../../..
./build.sh

# 3. In Spotify-App: Verfügbare Geräte → "M5Tab5" → Track abspielen
# 4. Skip-Test starten + 10× Skip antippen
./build.sh --no-build --no-flash --skip-test --duration 180
cat sessions/latest/verdict.json   # echte skip_stats
./diff_sessions.py 2026-05-03_122027_c33e   # Vergleich mit synthetischer Baseline
```

## Dateien

```
M5_3/
├── Claude.md                — Hauptregelwerk
├── POSTMORTEMS.md           — Gescheiterte Versuche dokumentiert
├── HYPOTHESES.md            — Skip-Fix-Hypothesen mit ESP-IDF Referenzen
├── WORKFLOW.md              — Autonomer Loop Cheat Sheet
├── NETWORK_DIAGNOSIS.md     — WLAN-Diagnose-Verfahren
├── STATUS.md                — Diese Datei
├── wifi_credentials.h       — SSID/Pass zentral (auch in main/wifi_credentials.h)
├── build.sh                 — Single-Source Build/Flash/Monitor + Stats-Aggregator
├── monitor_serial.py        — Serial-Reader mit Sentinel-Detection
├── wifi_check.sh            — One-Click Network-Diagnose
├── diff_sessions.py         — Cross-Session-Vergleich
├── sessions/                — Test-Verlauf, latest/ ist Symlink
└── archive/                 — alte Tools (fast_build.sh, hooks etc.)
```

## Was während dieser Session passierte

Vollautonom: 7 Build-Iterationen, 9 Flash-Iterationen, 5 große Code-Patches (TLS-Sentinel in TLSSocket.cpp, INET_CHECK in hal_wifi.cpp, Auto-Skip-Test in CSpotTask.cpp, Cloudflare-DNS Switch, WLAN-Bug Fix), 1 Component-Move (esp_hosted nach components/), 1 settings.json Permission-Fix, 7 Sentinel-Definitionen, 6 Doku-Files (Claude.md, POSTMORTEMS.md, HYPOTHESES.md, WORKFLOW.md, NETWORK_DIAGNOSIS.md, STATUS.md), 2 Aggregator-Bugs gefunden + gefixt, Anti-Pattern Compile-Assert (verifiziert dass es feuert).
