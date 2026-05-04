# M5_3 Postmortems

Dokumentation gescheiterter Versuche, damit sie nicht wiederholt werden.

Format pro Eintrag:
- **Commit / Datum**
- **Hypothese die getestet wurde**
- **Symptom (was crashte / brach)**
- **Vermuteter Root Cause**
- **Was nötig wäre für erneuten Versuch**

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
