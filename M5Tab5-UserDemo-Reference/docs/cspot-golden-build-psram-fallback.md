# Golden Build: cspot Spotify-Wiedergabe stabil (2026-04-25)

**Status: VERIFIZIERT — Audio sauber, stabil, ruckelfrei am M5Tab5.**

| Metadatum | Wert |
|---|---|
| Commit | `82fcd8e` |
| Git-Tag | `cspot-golden-psram-fallback-2026-04-25` |
| bin_sha (16) | `1606a3faeb6cc60f` |
| Backup | `~/.claude/tab5/binaries/82fcd8e_golden_psramfix_1606a3faeb6cc60f/` |
| Hardware | M5Tab5 (ESP32-P4 Rev 1.3 + ESP32-C6 SDIO) |
| ESP-IDF | v5.4.2 |
| Datum | 2026-04-25 |

## Verifizierte Messung (1 sauberer Run, 100s Beobachtung)

```
0 Crashes / 0 Reboots / 0 Asserts
2 alloc_fail (DMA-Heap erschöpft) → BEIDE von PSRAM-Fallback gefangen
7424 feedPCMFrames calls, 16 MB PCM ausgegeben, err=0x0
Track-Prefetch: 3734 KB in 17.7s @ 210 KB/s
Spotify-State: play="Pause" (= aktiv spielend)
```

## Kern-Fix: DMA→PSRAM-Fallback in sdio_rx_get_buffer

**Problem:** Bei sustained CDN-Streaming fragmentiert der DMA-Heap auf P4
durch die Kombination aus 1664-Byte lwip-pbufs (DMA-aligned) und ~6 KB
SDIO-RX-Bursts. `largest_free_block` fällt auf <4608 → der nächste
SDIO-RX-Buffer-Realloc schlägt fehl.

**Vorherige (kaputte) Ansätze:**
- Bare `assert(*buf)`: deterministischer Crash, Reset-Loop bei Long Tracks
- Reines Drop-and-Continue: silent Stillstand, SDIO-RX-State desynced

**Funktionierender Fix:** Bei `MEM_ALLOC` (DMA) Failure auf
64-byte-aligned **PSRAM-Allokation** zurückfallen. Der SDIO-Peripheral
auf P4 kann via Cache-Alignment in PSRAM DMA-en (gleicher Mechanismus
wie der `pkt_rxbuff`-Fix in commit `97a165b`).

```c
// In sdio_rx_get_buffer() — managed_components/.../sdio_drv.c
if (len > double_buf.buffer[index].buf_size) {
    uint8_t *new_buf = (uint8_t *)MEM_ALLOC(len);  // DMA first
    if (!new_buf) {
        new_buf = (uint8_t *)heap_caps_aligned_alloc(64, len,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // PSRAM fallback
        if (new_buf) {
            ESP_LOGW(TAG, "sdio_rx: DMA exhausted, alloc(%ld) from PSRAM", (long)len);
        }
    }
    if (!new_buf) return NULL;  // both pools exhausted (graceful drop)
    if (*buf) g_h.funcs->_h_free(*buf);
    *buf = new_buf;
    double_buf.buffer[index].buf_size = len;
}
```

Caller (`sdio_read_task`) entfernt `assert(rxbuff)` und droppt das Paket
bei `NULL`.

## Reproduzierbarkeit

```bash
# Auschecken
git checkout cspot-golden-psram-fallback-2026-04-25
cd M5Tab5-UserDemo-Reference/platforms/tab5

# managed_components/ ist gitignored — Patches synchronisieren
cp ../../patches/esp_hosted/sdio_drv.c \
   managed_components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c

# Bauen
source ~/esp/esp-idf-v5.4.2-tab5/export.sh
export ESP_IDF_VERSION=5.4
idf.py build

# Flashen
PORT=$(ls /dev/cu.usbmodem* | head -1)
esptool.py --chip esp32p4 --port $PORT --baud 921600 \
  --before=default_reset --after=hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x2000  build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/m5stack_tab5.bin
```

## Was NICHT funktionierte

| Ansatz | Symptom |
|---|---|
| `assert(*buf)` original | deterministischer Crash bei alloc_fail (Reset-Loop) |
| Drop ohne Fallback | silent SDIO-RX-Stillstand, kein PCM mehr |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` allein | reicht nicht — die ~6KB SDIO-RX-Allokation fragmentiert weiter |

Nur der echte PSRAM-Fallback im RX-Buffer-Allokator löst das Problem
robust.

## Kritische Boot-Constraints (aus CLAUDE.md)

- ESP-IDF **v5.4.2 zwingend** (v5.5+ generiert P4-Rev3.1-Code → crash auf Rev1.3)
- Bootloader bei `0x2000` (NICHT `0x0000`)
- Reset GPIO 15 (kein Pin-Konflikt mit SDIO D1)
