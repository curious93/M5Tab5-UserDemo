# M5_3 Postmortems

Dokumentation gescheiterter Versuche, damit sie nicht wiederholt werden.

Format pro Eintrag:
- **Commit / Datum**
- **Hypothese die getestet wurde**
- **Symptom (was crashte / brach)**
- **Vermuteter Root Cause**
- **Was nötig wäre für erneuten Versuch**

---

## 2026-05-04 — Audio-Stutter Root Cause: I2S ISR Priority

**Symptom:** Music plays cleanly within a track, but at every track transition (especially during AUTO_SKIP_TEST every 8s), audio stutters with 100-950ms gaps. User reports "ruckelt manchmal".

**Failed hypotheses (all disproved by data):**
1. `cdn_dl` task priority (5→3): no effect
2. `cdn_dl` core pinning (NO_AFFINITY → core 0): minor effect only
3. CircularBuffer too small (512KB→1MB): zero effect — BUF_FILL sentinel showed buffer at 67-100% during glitches
4. `FLUSH/SEEK` event clearing buffer: doesn't fire on skips (verified via grep on serial log)

**Verified root cause:** Default I2S ISR priority is `ESP_INTR_FLAG_LOWMED` (level 1-3). WiFi/TCP ISRs at higher levels delayed I2S DMA ISR by 100-500+ms during CDN download bursts. With only 31ms default DMA headroom (6 desc × 256 frames × 4B), DMA drained empty before ISR ran → `i2s_write` blocked waiting for buffer space → audible glitch.

**Diagnostic chain (data-driven):**
1. Added `BUF_FILL`/`BUF_EMPTY` sentinels in CSpotPlayer::runTask → ruled out buffer drain
2. Added `I2S_SLOW` sentinel around `codec->i2s_write` in Tab5AudioSink → found 1:1 correlation between `write_ms=311ms` and `AUDIO_GLITCH gap=530ms`
3. Increased `dma_desc_num` 6→16 → reduced glitch FREQUENCY (small stalls now fit in 85ms headroom) but big stalls (300-500ms) still hit
4. Set `chan_cfg.intr_priority = 5` in `bsp_audio_init` → I2S_SLOW dropped from 22 to 2, AUDIO_GLITCH from 10 to 1

**Fix applied:** `M5Tab5-UserDemo-Reference/platforms/tab5/components/m5stack_tab5/m5stack_tab5.c` — `bsp_audio_init`:
- `chan_cfg.dma_desc_num = 16` (default 6)
- `chan_cfg.intr_priority = 5` (default 0/LOWMED)

**Side effect to watch:** DMA budget tighter. `dma_largest=816` observed during CDN burst with 16 desc. Acceptable but monitor.

**Lehre für die Zukunft:**
- Bei Audio-Stutter IMMER zuerst `i2s_write` selbst messen, nicht nur den Buffer/Decoder
- ESP-IDF default I2S ISR priority ist zu niedrig wenn WiFi parallel läuft
- 1:1-Korrelation zwischen Sentinel A und Sentinel B ist Beweis, nicht Indiz

---

## 2026-05-04 — cdn_dl Stack Overflow von std::regex auf Spotify-CDN-URLs

**Symptom:** Nach intr_priority=5 Fix: Gerät crasht mit `Guru Meditation: Stack protection fault` in `cdn_dl` task wenige Sekunden nach Track-Skip / Track-Load.

**Falsche initial-Hypothese:** ISR-Frames auf cdn_dl Stack durch erhöhte I2S ISR-Frequenz overflownen den 24KB Stack.

**Tatsächlicher Root Cause (verifiziert via addr2line):** `bell::URLParser::parse()` ruft `std::regex_match` mit dem Regex `^(?:([^:/?#]+):)?(?://([^/?#]*))?([^?#]*)(\\?(?:[^#]*))?(#(?:.*))?` auf Spotify-CDN-URLs (200+ chars mit `__token__=...&hmac=...&...` query params). std::regex DFS-Backtracking rekursiert tief — `_M_dfs` self-recursion ~32+ Frames sichtbar im Stack-Dump, jeder Frame ~32 Bytes → >1KB pro Rekursionsstufe → 24KB Stack overflowed.

**Fix:** `cspot/bell/CMakeLists.txt:31` — `BELL_DISABLE_REGEX` von `OFF` auf `ON`. URLParser fällt zurück auf manuelle `parse()` mit sscanf+strstr ohne Rekursion. Auch `<cstring>` include zu URLParser.cpp hinzugefügt (war fehlend in der bestehenden Fallback-Implementierung).

**War kein Regression von intr_priority=5** — pre-existing latent bug. Wahrscheinlich vorher seltener getriggert oder hat sich in andere Crashes versteckt.

**Lehre:**
- `std::regex` auf Embedded mit kleinem Stack ist Russisch Roulette — IMMER `BELL_DISABLE_REGEX=ON` für Embedded-Builds
- Stack-Dump mit repetitivem RA/PC-Pattern = Rekursion, nicht ISR-Overhead
- `addr2line` mit korrektem Pfad (`~/.espressif/tools/riscv32-esp-elf/.../riscv32-esp-elf-addr2line`) ist essentiell für Crash-Diagnose

---

## 2026-04-28 — MAX_TRACKS_PRELOAD=2 (a8a047c, 8cc311a) → reverted

### Commits
- `a8a047c` — `submodule(cspot): bump 39203d0 — MAX_TRACKS_PRELOAD=2 for fast skip`
- `8cc311a` — `submodule(cspot): bump 51e097a — fast skip patches` (Folge-Bump)
- `04403bd`, `968cc74` — Reverts beider 3 Tage später

### Hypothese
Schnellerer Skip, indem CSpot zwei statt einen Track im Voraus puffert. Skip-Latenz solle dadurch von "neu öffnen + buffern" auf "Pointer-Wechsel im Buffer" reduziert werden.

### Symptom
**Nicht dokumentiert in den Revert-Commit-Messages.** Aufgrund des Kontexts (Commit `10b6e28 fix(dma): SPIRAM_MALLOC_RESERVE_INTERNAL 32K → 64K` direkt davor) und der Tatsache dass beide Bumps reverted wurden, vermutet:
- Crash nach mehreren Tracks (DMA-Heap erschöpft)
- ODER: Boot-Loop weil Pre-Buffering bei Init nicht passt zu Setup
- ODER: Audio-Glitches durch konkurrierende DMA-Allocations

### Vermuteter Root Cause
**DMA-fähiger Internal-RAM-Heap erschöpft:** Bei PRELOAD=2 würden zwei parallele OPUS-Header-Buffer + zwei TLS-Connection-Buffer + zwei HTTP-Response-Buffer allokiert. **Der Heap ist viel zu klein dafür.**

Echte Messung (2026-05-03, Idle nach Auth):
```
[HEAP_DMA total_free=34227 largest=20476 alloc=265 free=20]
[HEAP_PSRAM total_free=21658736 largest=21495804]
```

- DMA-Heap im Idle: **34 KB total, 20 KB größter Block**
- PSRAM-Heap: 21 MB frei (kein Problem)

Mit OPUS_HEADER_SIZE ~10KB pro Track + TLS/HTTP-Overhead pro Connection (~2-4 KB) wäre PRELOAD=2 schon am Idle-Limit. Bei aktivem Audio + zusätzlicher Track-Load → garantiertes ENOMEM. Indizien: Commit-Sequenz davor zeigt SPIRAM-Tuning (`033e8b2`, `b05758e`, `ed842f2`), die alle CDN-Backpressure adressieren.

### Was nötig wäre für erneuten Versuch
**Vor PRELOAD=2-Wiederansatz (status: SCHWER, möglicherweise nicht möglich):**
1. ✅ **Messen erledigt:** `[HEAP_DMA]` Sentinel periodisch (alle 5s in `CSpotTask.cpp`)
2. **DMA-Pool zwingend nötig:** Bei nur 20 KB largest-free-block kann PRELOAD=2 nicht funktionieren ohne dass Buffer pre-allokiert + recycelt werden statt `new`/`malloc` pro Track
3. **Internal-RAM erweitern wenn möglich:** sdkconfig `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` evtl. erhöhen (aktuell 32K → könnte 64K oder mehr sein, falls andere Subsysteme das tolerieren)
4. **Atomic Commit:** Erst Buffer-Pool, verifizieren mit Skip-Test (10×, kein Crash), DANN PRELOAD anheben — nicht beides gleichzeitig

**Klassifizierung:** Klasse B (GESPERRT) bis Buffer-Pool + Heap-Erweiterung beide gemacht und einzeln verifiziert.

### Heap-Daten Konkret

Aktuell (Idle, kein Audio aktiv):
- DMA Heap: `total_free=34227, largest_free_block=20476` Bytes
- PSRAM: `total_free=21658736, largest=21495804` Bytes (kein Mangel)
- 265 allocated blocks vs. 20 free blocks → **stark fragmentiert** (ratio 13:1)

Während Audio-Wiedergabe (zu messen): vermutlich noch weniger DMA frei.

---

## Template für künftige Postmortems

```
## YYYY-MM-DD — <Beschreibung> (<commit-hash>) → reverted

### Commits
- `<hash>` — <message>

### Hypothese
<Was sollte geändert werden, mit welcher Erwartung?>

### Symptom
<Was crashte / brach, mit Log-Excerpts wenn möglich>

### Root Cause
<Was war wirklich los, falls bekannt — sonst "vermutet: X">

### Was nötig wäre für erneuten Versuch
<Konkrete Schritte: messen, isolieren, vergleichen, dann erneut probieren>
```

---

## 2026-05-04 — STREAM_START_THRESHOLD 64KB (H6) — immediate CDN stall on all tracks

### Commits
- H6 test: STREAM_START_THRESHOLD 64*1024 in CDNAudioFile.h
- Reverted same session after quality monitoring confirmed severe stutter

### Hypothese
Reducing threshold to 64KB (from 128KB) would allow Vorbis decode to start earlier,
reducing skip latency (median was 5805ms at 128KB). Expected: median <2500ms with zero CDN stalls.

### Symptom
Confirmed: median skip -63% (5805→2144ms). But audio quality session revealed severe stutter:
- [CDN_STALL need=65703 have=65536 gap=0 KB] immediately on first track decode call
- [CDN_STALL_END waited=Xms] after stall resolved
- [AUDIO_GLITCH gap=Xms] audible stutter on virtually every track

### Root Cause
Vorbis setup headers (containing codebooks) for some Spotify tracks reach 65,703 bytes —
167 bytes past the 65,536-byte (64KB) boundary. The Vorbis decoder's ov_open_callbacks()
performs several sequential seeks into the setup header during initialization. When the
threshold is exactly 64KB, readBytes() delivers data up to byte 65,536, then blocks
immediately because byte 65,537 onwards hasn't been downloaded yet.
This manifests as a CDN stall within milliseconds of decode starting, causing an audible
gap in output > 80ms.

### Root Cause (abort guard)
Separate bug uncovered during H6 testing: when a skip fires while a CDN download is in
progress, CDNAudioFile's destructor sets dlAbort=true and the download task continues
running on its 24KB stack. The original code fired downloadCompleteCallback regardless of
abort state. That callback → prefetchAudioFile() → CDNAudioFile::openStream() → TLS
handshake all ran on the cdn_dl task's 24KB stack → stack overflow crash.
Fix: `if (cb && !aborted) cb()` — abort guard before firing prefetch callback.
(Fix committed in 1310bb3, permanent — no revert needed.)

### What's needed for re-attempt
64KB cannot work because Spotify's Vorbis format allows setup headers up to ~96KB.
Any threshold below ~96KB risks this stall. Verified threshold: 128KB (zero stalls in
thresh128 quality session, confirmed by [CDN_STALL] sentinel absence).

To reduce skip latency without lowering the threshold:
- H4b: CDN connection pooling — reuse TLS connection across tracks (currently ~155ms TLS
  per track regardless of threshold)
- H3: Audio buffer pre-allocation (DMA pool) to reduce per-track malloc cost
- H7 (future): Investigate Vorbis-format-specific header prefetch to speed up ov_open

**Klassifizierung:** THRESHOLD<128KB → Klasse B (GESPERRT). Verwende immer ≥128KB.
