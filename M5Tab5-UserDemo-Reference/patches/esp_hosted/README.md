# ESP-Hosted Patches für ESP32-P4 Tab5 cspot Audio

## Warum

`managed_components/` wird vom IDF Component Manager neu generiert und ist **nicht in Git getrackt**. Diese Patches sichern die für sustained Spotify-Playback nötigen Änderungen am ESP-Hosted SDIO-Treiber.

## Geänderte Dateien

Die Originaldateien liegen unter:
```
platforms/tab5/managed_components/espressif__esp_hosted/host/drivers/
```

| Datei in diesem Patch-Verzeichnis | Ziel | Geändert? | Zweck |
|---|---|---|---|
| `sdio_drv.c` | `transport/sdio/sdio_drv.c` | **JA (Fix)** | Streaming-RX-Puffer aus PSRAM statt DMA-Heap |
| `sdio_drv.h` | `transport/sdio/sdio_drv.h` | nein (original) | Vollständigkeit |
| `mempool.c` | `mempool/mempool.c` | nein (original) | Vollständigkeit — **nicht ändern** |

## Was die Patches genau ändern

### `sdio_drv.c` — der eigentliche Fix (verifiziert 2026-04-25)

**Funktion `sdio_push_data_to_queue`** (Streaming-Mode RX-Pfad):

Originalverhalten: `pkt_rxbuff` wird via `sdio_buffer_alloc()` aus dem DMA-fähigen Mempool geholt. Dieser Puffer ist nach dem `memcpy` aus `double_buf` CPU-only-data — DMA-Fähigkeit ist nicht mehr nötig, aber der DMA-Heap wächst mit jedem empfangenen WiFi-Paket.

Geändert:
```c
/* Allocate rx buffer from PSRAM-first (no DMA needed after memcpy
 * out of double_buf; keeping these out of DMA heap prevents
 * exhaustion under sustained WiFi+Vorbis load). */
pkt_rxbuff = (uint8_t *)heap_caps_malloc_prefer(
    MEMPOOL_ALIGNED(MAX_SDIO_BUFFER_SIZE), 2,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
```

Außerdem: `sdio_push_pkt_to_queue()` nimmt jetzt einen `free_fn`-Parameter. Streaming-RX-Pakete werden mit `sdio_rx_pkt_free` (ruft `free()` direkt auf) freigegeben; der nicht-streaming Pfad nutzt weiterhin `sdio_buffer_free` (gibt an den DMA-Mempool zurück).

**Wichtig:** `mempool.c` bleibt original. TX-`sendbuf` in `sdio_write_task` MUSS DMA-fähig sein (SDIO CMD53 Anforderung). Ein PSRAM-Patch auf `mempool.c` würde TX brechen und `Failed to send data: 258 (ESP_ERR_INVALID_RESPONSE)` erzeugen.

## Anwendung nach `idf.py reconfigure` / Komponenten-Update

Wenn `managed_components/` regeneriert wurde, nur `sdio_drv.c` zurückkopieren:

```bash
PROJ=platforms/tab5/managed_components/espressif__esp_hosted/host/drivers
cp patches/esp_hosted/sdio_drv.c  $PROJ/transport/sdio/sdio_drv.c
idf.py build
```

## Verifikation (2026-04-25)

Vor Fix (Baseline ohne sdio_drv.c-Patch):
- Crash nach 352 feedPCMFrames (~7.5 s Musik)
- `alloc_fail: size=4608 caps=0x8` — DMA-Heap erschöpft
- DMA `lfb` fällt unter 3 KB beim ersten CDN Range-Request

Nach Fix (sdio_drv.c PSRAM streaming RX):
- **8256+ feedPCMFrames** über 90 s Monitoring, kein Crash
- DMA `lfb=4336` beim CDN Range-Request — bleibt stabil
- Keine `alloc_fail`, keine `Rebooting`
