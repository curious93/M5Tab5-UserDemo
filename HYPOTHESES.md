# Skip-Performance Hypothesen mit 1:1-Referenz-Patterns

Vier Hypothesen zur Reduzierung der Skip-Latenz im Spotify-Streamer. **Jede hat einen funktionierenden Referenz-Pattern in ESP-IDF v5.4.2** — keine Erfindung nötig.

Klassifizierung nach Claude.md §6: alle Klasse C (Hypothesen-Fixes mit minimaler Adaption).

Reihenfolge: nach Aufwand × erwartetem Nutzen sortiert.

---

## H1 — DNS-Caching für Spotify CDN

**Hypothese:** Spotify-CDN-Hosts (`audio-*.spotifycdn.com`) werden pro Track neu DNS-aufgelöst. Latenz pro Lookup ~50-200ms je nach Netzwerk.

**Referenz:** `~/esp/esp-idf-v5.4.2-tab5/examples/protocols/http_request/main/http_request_example_main.c:50-62`

```c
int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
freeaddrinfo(res);
```

**lwip-Cache:** `~/esp/esp-idf-v5.4.2-tab5/components/lwip/lwip/src/core/dns.c` — `dns_lookup()` cached automatisch wenn `DNS_TABLE_SIZE` gesetzt ist.

**Adapter:** Im sdkconfig prüfen ob `CONFIG_LWIP_DNS_MAX_HOST_IP > 1`. Falls nicht: erhöhen, dann verifizieren via `[DMA_FREE]` + Skip-Latenz-Vergleich.

**Ziel-File:** CSpot's `ApResolve.cpp` oder `MercurySession.cpp`

**Erwartung:** Skip-Latenz Median -100 bis -300ms.

---

## H2 — HTTP Connection-Pooling **[DONE — VERBESSERT 2026-05-03]**

**Hypothese:** CSpot öffnet pro Track neue TCP-Connection zum CDN. TCP-Handshake + TLS-Handshake = ~200-500ms.

**Referenz:** `~/esp/esp-idf-v5.4.2-tab5/examples/protocols/esp_http_client/main/esp_http_client_example.c:155-182`

**Status:** 
- Phase 1 (Host+Path Reuse): Bereits implementiert als `s_cachedResp` + `urlHostPath()`
- Phase 2 (Host-Level Reuse): **Implementiert 2026-05-03** — `s_cachedHost` statt `s_cachedHostPath`

**Was Phase 2 bringt:** Verschiedene Tracks auf demselben CDN-Host (z.B. `audio-fa.scdn.co`) teilen jetzt dieselbe TLS-Connection. Skip von Track A → Track B auf gleicher CDN-Domain: kein TLS-Handshake nötig.

**Sentinel:** `[H2_REUSE host=X]` erscheint bei jedem erfolgreichen Reuse. `h2_reuse_count` in `verdict.json`.

**Erwartung:** TLS-Latenz bei Tracks auf gleicher CDN-Domain: ~0ms statt 33-61ms. Erst mit echten Skip-Daten verifizierbar.

---

## H3 — mbedTLS-Buffer-Reduktion für PRELOAD=2 **[BLOCKIERT, Analyse abgeschlossen]**

**Ursprüngliche Hypothese:** DMA-Buffer-Pool für `header`/`httpBuffer` in CDNAudioFile.

**REVISION (2026-05-03):** sdkconfig-Analyse zeigt:
- `CONFIG_SPIRAM_USE_MALLOC=y` + `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0`
- → alle `malloc`/`std::vector` gehen nach **PSRAM**, nicht DMA!
- `header` (8KB) und `httpBuffer` (16KB) sind in PSRAM → BufferPool würde PSRAM-Fragmentation helfen, aber NICHT das DMA-Limit

**Echter DMA-Bottleneck:**
- `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` → **16KB DMA pro TLS-Connection**
- `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096` → 4KB DMA
- Pro TLS-Connection: ~20KB DMA → exakt das Idle-Limit (20476 bytes)
- PRELOAD=2 bräuchte 2 TLS-Connections = 40KB DMA → **unmöglich**

**Was PRELOAD=2 erlauben würde:**
1. `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` — Buffers nur während Send/Recv (war disabled, Grund unbekannt)
2. `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096` — riskant, Spotify-CDN-Records könnten >4KB sein

**Status:** Analyse abgeschlossen. PRELOAD=2 bleibt GESPERRT bis mbedTLS-Buffer-Strategie klar ist.

**Echte Messung (2026-05-03):**
```
[HEAP_DMA total_free=34227 largest=20476 alloc=265 free=20]
[HEAP_PSRAM total_free=21658736 largest=21495804]
```

---

## H4 — Next-Track CDN Prefetch (download-complete callback) **[DONE — 2026-05-04]**

**Ursprüngliche Hypothese:** Nach abgeschlossenem CDN-Download des aktuellen Tracks sofort den nächsten Track (256 KB) vorab herunterladen. Bei Skip: PREFETCH_HIT → sofortiger Start.

**Implementierung:**
- `downloadCompleteCallback` in CDNAudioFile (fires when background dl done)
- `prefetchAudioFile()` in QueuedTrack (calls openStream() on next track)
- `getAudioFile()` returns prefetchedAudioFile fast path if available
- TrackPlayer registriert Callback via weak_ptr nach trackLoaded()

**Ergebnis (Session h4_2026-05-04_002714, n=11):**
- PREFETCH_HIT: **0** — Prefetch feuerte NICHT während Skip-Test
- Median: 3700ms vs Baseline 5805ms (-36%) — aber NICHT durch Prefetch bedingt
- Ursache Verbesserung: CDN-Speed in dieser Session höher (127-169 KB/s vs 86 KB/s Baseline)
- Ursache kein Prefetch: Track-Download dauert 30-90s (4-11 MB bei 127 KB/s); Skip feuert alle 8s → Callback kommt nie vor Skip

**Wann hilft H4:** Nur bei natürlichem Track-Ende (EoT). Wenn Track zu Ende läuft, download-complete fired rechtzeitig → nächster Track instant. **Nicht hilfreich für manuelle Skips.**

**Kritischer DMA-Befund:**
- DMA start: 28231 bytes → DMA min: 6371 bytes nach 10 Skips
- Graduelle DMA-Fragmentierung durch 10× TLS open/close: -21860 bytes
- Bestätigt: PRELOAD=2 nicht möglich solange jede TLS-Conn ~16 KB DMA kostet

**Status:** DONE — Code bleibt (hilft bei EoT), Skip-Latenz-Problem für manuelle Skips ungelöst.

---

## H4b — FreeRTOS StreamBuffer für Pre-Fetching (ursprünglich als H4 geplant)

**Hypothese:** Download-Pipeline als Producer-Task, Decoder als Consumer. Bei Skip: nur Producer reset, Decoder spielt aus Buffer weiter bis neuer Track startet.

**Referenz:** `~/esp/esp-idf-v5.4.2-tab5/components/freertos/FreeRTOS-Kernel/include/freertos/stream_buffer.h`

```c
StreamBufferHandle_t buf = xStreamBufferCreate(SIZE, 1);
xStreamBufferSend(buf, data, len, 0);
xStreamBufferReceive(buf, dst, len, pdMS_TO_TICKS(10));
```

**Ziel-File:** `cspot/src/TrackPlayer.cpp` + `CDNAudioFile.cpp` — invasivste Änderung

**Erwartung:** Nur hilfreich wenn Download weit genug voraus. Bei 8s Skips und 127 KB/s = 1000 KB voraus — das reicht wenn STREAM_START_THRESHOLD sinkt.

**Status:** Noch nicht probiert. Für Skip-Latenz der vielversprechendste Ansatz, aber hoher Aufwand.

---

## Mess-Sentinels (bereits eingebaut)

Vor jeder Hypothesen-Implementation **erst messen** — die Sentinels sagen welcher Teil dominant ist:

| Sentinel | Quelle | Misst |
|---|---|---|
| `[DNS_LOOKUP host=X took=Yms]` | PlainConnection.cpp:55 | DNS-Latenz pro Lookup |
| `[TCP_CONNECT host=X took=Yms]` | PlainConnection.cpp | TCP-Handshake-Zeit |
| `[TRACK_LOAD start url=...]` | CDNAudioFile.cpp:298 | Track-Load-Anfang |
| `[TRACK_LOAD ready in Xms]` | CDNAudioFile.cpp | Buffer komplett |
| `[DMA_FREE N largest=M]` | CDNAudioFile.cpp | DMA-Heap-Status |
| `[SKIP_REQ direction=fwd\|back]` | SpircHandler.cpp | Skip-Trigger |
| `[SKIP_DONE in Xms]` | SpircHandler.cpp | Skip-API-Latenz |

**`build.sh` aggregiert automatisch in `verdict.json`:** `dns_stats`, `tcp_stats`, `skip_stats`, `dma_min_bytes`.

**Diagnostischer Workflow vor Patch:**
1. Skip-Test laufen lassen
2. `verdict.json` prüfen welcher Stat dominiert
3. **Hypothese wählen die das dominante Symptom adressiert** (nicht blind H1 zuerst)
4. Erst DANN Code ändern

---

## Reihenfolge der Erprobung (revidiert 2026-05-04)

1. **H2 (Connection-Pool verbessert) — DONE** — Host-Level Reuse, 21 Reuses pro Session.
2. **H4 (Next-Track CDN Prefetch) — DONE** — Hilft bei EoT, nicht bei manuellen Skips.
3. **H1 (DNS-Cache)** — wenig Hebel (<30ms DNS), skip floor bleibt ~2s wegen CDN-Download.
4. **H4b (StreamBuffer-Architektur)** — nächster Kandidat wenn 2s Skip-Floor unakzeptabel.
5. **H3 (PRELOAD=2 via mbedTLS-Tuning)** — BLOCKIERT bis DMA-Fragmentierungs-Problem gelöst.

**Aktueller Skip-Floor:** ~2s (konstrained durch 256 KB / CDN-Speed). Unter 2s nur möglich wenn:
- STREAM_START_THRESHOLD reduziert (z.B. 64 KB → ~0.5s), ODER
- Next-Track 256 KB ist schon komplett im RAM (H4b StreamBuffer), ODER
- mbedTLS Dynamic Buffers freigegeben + PRELOAD=2 (H3, noch BLOCKIERT)

## Workflow pro Hypothese (autonomer Loop)

```
1. Diff zur Baseline messen (verdict.json vergleichen)
2. Hypothese implementieren (Referenz 1:1 adaptiert)
3. ./build.sh --skip-test (n=10)
4. Diff zur Baseline:
   - Median -10% oder besser → COMMIT, weiter
   - Median ±5%             → REVERT, nächste Hypothese
   - Median schlechter      → REVERT, in POSTMORTEMS.md eintragen
   - Crash                  → REVERT + Backtrace in POSTMORTEMS.md
5. Bei 2 erfolglosen in Folge → STOP, User eskalieren
```

## Was Klasse-B (gesperrt)

`MAX_TRACKS_PRELOAD>1` bleibt **gesperrt** bis H3 erfolgreich (siehe POSTMORTEMS.md).

---

## H7: STREAM_START_THRESHOLD = 96 KB (2026-05-04)

**Status:** TESTING

**Rationale:** Observed max Vorbis setup header = 65,703 bytes (from quality session with 64KB threshold).
96KB = 98,304 bytes → 32KB margin over observed max ≈ 800ms buffer at 40KB/s.
128KB was safe but adds ~800ms of unnecessary wait vs 96KB.

**Expected change vs 128KB baseline:**
- Median: 3770ms → ~3100ms (-18%)
- Max: 5055ms → ~4100ms (-19%)
- CDN_STALL: 0 (if no track has setup header >96KB)

**Abort condition:** ANY [CDN_STALL] → revert to 128KB, document max observed header size.

**Changes:**
- CDNAudioFile.h:106 — STREAM_START_THRESHOLD = 96 * 1024
