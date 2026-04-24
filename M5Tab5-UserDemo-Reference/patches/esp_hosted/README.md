# ESP-Hosted Patches für ESP32-P4 Tab5 cspot Audio

## Warum

`managed_components/` wird vom IDF Component Manager neu generiert und ist **nicht in Git getrackt**. Diese Patches sichern die für sustained Spotify-Playback nötigen Änderungen am ESP-Hosted SDIO-Treiber.

## Geänderte Dateien

Die Originaldateien liegen unter:
```
platforms/tab5/managed_components/espressif__esp_hosted/host/drivers/
```

| Datei in diesem Patch-Verzeichnis | Ziel | Zweck |
|---|---|---|
| `mempool.c` | `mempool/mempool.c` | **Hauptfix:** verarbeitete RX-Pakete in PSRAM statt DMA-Heap |
| `sdio_drv.c` | `transport/sdio/sdio_drv.c` | Optionale `sdio_rx_prewarm()` Funktion (experimentell) |
| `sdio_drv.h` | `transport/sdio/sdio_drv.h` | Public-Deklaration für `sdio_rx_prewarm()` |

## Was die Patches genau ändern

### `mempool.c` — der eigentliche Fix
Originalverhalten: jeder frische Pool-Block wird mit `MEM_ALLOC` (`esp_dma_capable_malloc`) angefordert → wächst dauerhaft im DMA-Heap.

Geändert: `heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. Das routet processed RX-Packets in PSRAM (oder als Fallback in non-DMA Internal). Diese Buffer müssen NICHT DMA-fähig sein — sie wurden bereits per memcpy aus dem echten DMA-Buffer (`double_buf`) extrahiert. Belegt durch Issue #597 und der ESP-Hosted-Architekturanalyse.

Anwenden:
```bash
cp patches/esp_hosted/mempool.c \
   platforms/tab5/managed_components/espressif__esp_hosted/host/drivers/mempool/mempool.c
```

### `sdio_drv.c` und `sdio_drv.h` — Prewarm (experimentell)
Fügt eine Funktion `sdio_rx_prewarm(uint32_t size)` hinzu, die beide Slots des SDIO-Streaming-Double-Buffers vorab auf `size` Bytes alloziert. Wird optional von `app_main.cpp` aufgerufen. Wirkung allein noch nicht eindeutig belegt — der Mempool-Fix ist die belegte Ursache der Crashes.

## Anwendung nach `idf.py reconfigure` / Komponenten-Update

Wenn `managed_components/` regeneriert wurde, einfach die Dateien aus diesem Verzeichnis zurückkopieren:

```bash
PROJ=platforms/tab5/managed_components/espressif__esp_hosted/host/drivers
cp patches/esp_hosted/mempool.c   $PROJ/mempool/mempool.c
cp patches/esp_hosted/sdio_drv.c  $PROJ/transport/sdio/sdio_drv.c
cp patches/esp_hosted/sdio_drv.h  $PROJ/transport/sdio/sdio_drv.h
idf.py build
```

## Bestätigte Beobachtung (2026-04-24)

Vor Patch (b92abe1, ohne Mempool-Fix):
- DMA-Heap fällt von ~184 KB bei Boot auf ~3 KB beim ersten CDN Range-Request
- Crash nach ~3584 feedPCMFrames (~40 s Musik) auf `sdio_rx_get_buffer:670 (*buf)` Assert

Nach Patch — noch zu verifizieren in einer ≥3 min Sustained-Playback-Session.
