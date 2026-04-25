# cspot Skip Fix — SDIO DMA Pre-Alloc + CDN Backpressure 2KB

**Date:** 2026-04-25  
**Commit:** `9ec2f2c` (main), cspot submodule `033e8b2`  
**Tag:** (see git log)

## Problem

Track skip (SPIRC LOAD frame while playing) caused DMA alloc_fail cascade on ESP32-P4 + C6-SDIO setup, leading to WiFi death and eventual reboot.

### Root Cause Chain

1. **CDN burst during track change** — New track CDN download starts immediately after SPIRC LOAD. TLS + CDN + Vorbis decode all compete for DMA heap.

2. **DMA fragmentation** — Total DMA free ~6.1 KB but largest contiguous block only 2.9 KB. SDIO RX needs 4608 bytes aligned (9 × 512-byte blocks, worst case 7168 = 14 × 512) for a single receive frame.

3. **alloc_fail(4608/7168) cascade** — `sdio_rx_get_buffer()` in `sdio_drv.c` calls `heap_caps_aligned_alloc(64, len, MALLOC_CAP_DMA)`. Fails → falls back to PSRAM. PSRAM is not DMA-capable → every subsequent `_h_sdio_read_block()` returns err=258. WiFi effectively dead.

4. **CDN timeout** — CDN download stalls because WiFi is dead → TLS timeout → Mercury reconnect failures → after 6 consecutive failures → device reboot.

### Previous Fix (insufficient)

The `buf_size=0` reset after err=258 + 1000ms DMA retry (commit `a82556d`) prevented the persistent PSRAM death-spiral but didn't prevent the initial alloc_fail. alloc_fail still occurred at rate ~3/test during CDN bursts.

### CDN Backpressure Iteration History

| Threshold | Result |
|-----------|--------|
| `total_free < 4 KB` (original) | alloc_fail(4608) — 4KB threshold below floor only at true exhaustion, not fragmentation |
| `largest_free_block < 6 KB` | Caught 4608 failures but alloc_fail(7168) appeared |
| `largest_free_block < 8 KB` | CDN permanently throttled to 3 KB/s — DMA total_free during streaming is only ~3.2 KB, always below 8KB → TLS timeout |
| `total_free < 2 KB` (current) | Works with pre-alloc — SDIO never needs runtime realloc |

## Fix

### 1. Pre-allocate SDIO double_buf at boot (`sdio_drv.c`)

In `transport_init_internal()`, after `double_buf.write_index = 0`:

```c
// Pre-allocate SDIO RX DMA buffers at init time (before CDN starts
// fragmenting DMA heap). SDIO RX frames can reach 7168 bytes (14 × 512
// blocks) during CDN bursts. Reserving 8192 bytes per slot guarantees
// the sdio_rx_get_buffer() "len > buf_size" realloc never fires at
// runtime — avoiding DMA alloc_fail → PSRAM fallback → err=258 chain.
for (int _i = 0; _i < 2; _i++) {
    double_buf.buffer[_i].buf = (uint8_t *)heap_caps_aligned_alloc(
        64, 8192, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (double_buf.buffer[_i].buf) {
        double_buf.buffer[_i].buf_size = 8192;
        ESP_LOGI(TAG, "sdio_drv: pre-alloc double_buf[%d] 8192 bytes DMA OK", _i);
    } else {
        ESP_LOGW(TAG, "sdio_drv: pre-alloc double_buf[%d] failed, runtime alloc", _i);
    }
}
```

**Why 8192?** The `sdio_rx_get_buffer()` condition is `if (len > double_buf.buffer[write_index].buf_size)`. If pre-alloc size ≥ max possible frame size (7168), the realloc branch never fires. 8192 = next power-of-2 above 7168, with alignment headroom.

**Why at init?** `transport_init_internal()` runs at tick ~3000ms, before WiFi connects (~9000ms) and before CDN starts (~300+ seconds). DMA heap is fully unfragmented at this point.

**Note:** Pre-alloc log messages (`sdio_drv: pre-alloc double_buf[N] 8192 bytes DMA OK`) are NOT visible in serial log capture because they print before the serial reader starts. Presence verified via `strings build.bin | grep pre-alloc`.

### 2. CDN backpressure threshold 4KB → 2KB (`CDNAudioFile.cpp`)

```cpp
// Back-pressure: pause CDN download when DMA is critically low.
// SDIO double_buf buffers are pre-allocated at init (8192 bytes each)
// so no runtime DMA realloc is needed for SDIO — this guard only
// protects other DMA consumers (TLS, lwIP TX).  Threshold 2 KB.
if (heap_caps_get_free_size(MALLOC_CAP_DMA) < 2 * 1024 && !this->dlAbort) {
    int paused_ms = 0;
    while (heap_caps_get_free_size(MALLOC_CAP_DMA) < 2 * 1024
           && !this->dlAbort && paused_ms < 2000) {
        vTaskDelay(pdMS_TO_TICKS(20));
        paused_ms += 20;
    }
}
```

With pre-alloc, DMA stays at ~11.6 KB during CDN streaming. The 2KB threshold acts as a pure safety valve and never triggers during normal operation.

## Verification (skip11 test, 2026-04-25)

**Setup:** Phone as controller (`95a14dbd...`), Tab5 as speaker, Chrome playing "drop dead"

| Metric | Result |
|--------|--------|
| Track 1 (SPIRC LOAD → play) | 45 MB PCM, 200s audio |
| Skip (SPIRC LOAD for track 2) | Received at t=386.5s |
| Track 2 CDN download | 3529 KB @ 124 KB/s, DMA stable 11.6 KB |
| Track 2 decode start | t=569.3s (3s after EOF) |
| Track 2 audio | ~176s (full track, 3529 KB @ 160kbps) |
| alloc_fail(4608) | 0 |
| alloc_fail(7168) | 0 |
| Reboots | 0 |
| Mercury alive after both tracks | ✅ (heartbeats at t+636s, t+756s) |

**DMA comparison:**

| Phase | DMA total_free | DMA lfb |
|-------|----------------|---------|
| Before fix (skip8, CDN burst) | 6147 bytes | 2944 bytes |
| After fix (skip11, CDN burst) | 12083 bytes | 11248 bytes |

## Files Changed

| File | Change |
|------|--------|
| `managed_components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c` | Pre-alloc double_buf at init |
| `patches/esp_hosted/sdio_drv.c` | Synced with managed_components |
| `components/cspot/cspot/src/CDNAudioFile.cpp` | Threshold 4KB → 2KB |

## Known Limitations

- N=1 skip test so far. More tests needed for statistical baseline (≥3 skips).
- 180s delay between SPIRC LOAD and CDN start for second track (t=386s → t=566s) is unexplained — likely access token renewal or TrackQueue processing overhead.
- Pre-alloc log messages not visible in serial capture (printed before reader starts).
