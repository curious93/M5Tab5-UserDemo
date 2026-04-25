# CDNAudioFile: Prefetch-Latenz — Analyse & Fix-Plan

**Status: ANALYSIERT — Fix noch nicht implementiert**

## Problem

Inter-Track-Gap und manueller Songwechsel dauern 17–53 Sekunden je nach Trackgröße.

| Track | Größe | Prefetch-Dauer | Ø Download-Speed |
|-------|-------|---------------|-----------------|
| Hold Me Closer | 3.8 MB | ~17 s | ~220 KB/s |
| Your Song | 4.6 MB | ~30 s | ~155 KB/s |
| Track 3 | 7.0 MB | 53 s | 132 KB/s |

## Root Cause: Zwei kombinierte Probleme

### 1. Viele kleine HTTP-Range-Requests

Serial-Log zeigt:
```
HTTP range fresh: total=7203908 content_len=8192
readBody iter=0 n=8192 got=0/8192   ← erster Versuch 0 Bytes
HTTP range reuse: content_len=12292
```

`CDNAudioFile.cpp` liest in 8–16 KB Range-Requests. Jeder Request hat:
- TCP Round-Trip via SDIO ESP32-C6 → P4 → CDN
- TLS-Record-Overhead (mbedTLS)
- Spotify CDN Latenz ~50–100 ms

Bei 7 MB in 8 KB Chunks ≈ **900 Requests × ~50 ms RTT ≈ 45 s** — erklärt fast die gesamte Wartezeit.

Beweis: Erster Chunk kommt mit **1142 KB/s** (Burst-Bandbreite des Netzwerks ist also kein Engpass), fällt dann sofort auf 54–134 KB/s — typisches RTT-Throttling bei vielen kleinen Requests.

### 2. Full-Download vor Decode

Aktuelles Design: vollständiger Download + Decrypt in PSRAM-`fullFile`-Buffer, erst dann `ov_open_callbacks`. Der Nutzer hört nichts bis der letzte Byte entschlüsselt ist.

**Ursprüngliche Motivation:** Vor golden build (82fcd8e) verursachte ein offener CDN-Socket während der Decode-Phase DMA-Heap-Fragmentation → Crash. Full-Prefetch vermied den concurrent CDN+SDIO-RX-Konflikt.

**Heute obsolet:** Der PSRAM-Fallback in `sdio_rx_get_buffer()` fängt DMA-Erschöpfung ab. Concurrent CDN+Decode ist jetzt sicher (höchstens ~50 ms Ruckeln bei alloc_fail, kein Crash).

## Geplanter Fix (noch nicht implementiert)

**Zwei unabhängige Verbesserungen in `CDNAudioFile.cpp`:**

### Fix A — Einen einzigen HTTP-GET statt Range-Requests

Statt 8 KB Chunks: eine HTTP-Range-Request für die gesamte Payload. TCP-Streaming über eine persistente Verbindung → Download-Speed kehrt zur Burst-Rate (~1 MB/s) zurück.

Erwartete Wirkung: 7 MB / 1 MB/s = ~7 s statt 53 s.

### Fix B — Streaming-Decode ab ~512 KB Buffer-Threshold

Nach Fix A: Decode starten sobald erste 512 KB in PSRAM liegen, Rest parallel im Hintergrund downloaden.

Erwartete Wartezeit bis Audio: **~0.5 s** (512 KB / 1 MB/s).

## Risiko-Assessment

- CDN-Socket bleibt während Decode offen → DMA-Druck wie vor golden build
- Aber: `sdio_rx_get_buffer()` PSRAM-Fallback fängt das ab (verifiziert 3×)
- Concurrent CDN+Decode: schlimmstenfalls ~50 ms Ruckeln bei DMA-Erschöpfung, kein Crash

## Betroffene Datei

```
platforms/tab5/components/cspot/cspot/src/CDNAudioFile.cpp
```

Isolierter Eingriff — kein sdio_drv, kein cspot-Kern, kein LVGL.

## Baseline vor Fix (verifiziert 2026-04-25)

Golden Build `82fcd8e` mit vollem Prefetch:
- 3× Hold Me Closer: alle vollständig durchgespielt, 0 Crashes
- Your Song (4.6 MB): vollständig, 0 Crashes
- Track 3 (7.0 MB): vollständig, 0 Crashes
- Gesamt: 70976 feedPCMFrames, 157 MB PCM, 0 Reboots seit Boot
