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
