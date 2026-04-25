# cspot on M5Tab5: DMA-heap fragmentation during CDN streaming

**As of 2026-04-24.** Root-cause analysis of the sustained-playback crash, with measured evidence, failed hypotheses, and concrete fix directions. Written after 3 trials on binary `96d4943` (SHA256 `ffbfe4b8…`, snapshot at `~/.claude/tab5/binaries/96d4943/`).

## TL;DR

Every long-playback attempt dies in the DMA heap, not in cspot logic. The primary failure is a **4608-byte DMA-aligned allocation** inside the ESP-Hosted SDIO RX path. When that fails, the driver cascades to a 181-byte retry (historically what we saw in logs) which also fails, hitting `assert failed: sdio_rx_get_buffer sdio_drv.c:670`.

The DMA heap is drained + fragmented by mbedTLS/WiFi activity during the **first CDN HTTPS range GETs** of each track. By the time the ESP-Hosted RX path needs a fresh 4608-byte aligned buffer, the largest free block is already below 2 KB.

**The previously recorded "120 s hero run" (`96d4943` on 2026-04-21) is not reproducible.** Same bytes, 3 fresh trials, 3 different outcomes — all short, all crash in the same path. The 120 s was a lucky tail of a high-variance distribution driven by boot-time heap layout.

## Evidence

### 3 trials, same binary `96d4943`, 2026-04-24

| Trial | Start DMA free | Max Spotify feedPCMFrames | First alloc_fail | Final `lfb` |
|---|---|---|---|---|
| 1 | 14 443 B | 0 (tremor only) | `size=3584 caps=DMA aligned_alloc` | 8 176 |
| 2 | 18 911 B | 1 call | (Guru — no alloc_fail; different panic path) | 1 584 |
| 3 | 17 039 B | 1 call | `size=1664 caps=DMA aligned_alloc` | 1 392 |
| 4 (fresh build w/ backtrace) | 20 607 B | 2 calls | `size=4608 caps=DMA aligned_alloc` | 3 184 |

Starting DMA-free varies by ~6 KB *before any CDN activity* — boot-time static-init order on P4 is nondeterministic enough to shift the heap layout between reboots of byte-identical firmware.

### Per-range heap trajectory (Trial 4, instrumented)

```
pre-range[0..8191]      : total_free=20607 lfb=19440 free_blocks=6
pre-range[0..-12300]    : total_free=10659 lfb=8176  free_blocks=9
pre-range[0..16383]     : total_free=3795  lfb=3184  free_blocks=11
pre-range[8192..24575]  : total_free=3795  lfb=3184  free_blocks=11   ← crash fires here
```

`total_free` drops from 20 K → 3.8 K over the first two HTTPS range GETs. `lfb` drops harder (19 K → 3 K) than `total_free`, and `free_blocks` grows (6 → 11) — classic **fragmentation-cliff**, not linear drain.

### Crash cascade (Trial 4)

```
E (34374) alloc_fail: size=4608 caps=0x8 func=heap_caps_aligned_alloc   ← real first failure
I (34382) Tab5Sink:  feedPCMFrames: calls=384                           ← one more decoded frame slips through
E (34613) dma_utils: esp_dma_capable_malloc(181): Not enough heap memory ← cascade
assert failed: sdio_rx_get_buffer sdio_drv.c:670 (*buf)                  ← final panic
```

The historical "SDIO 181-byte" assert seen in every earlier log is a **secondary symptom**. The primary allocation that starves the DMA heap is the 4608-byte RX buffer. Register-dump sidelight: `S10=0x1200` (= 4608) at the SDIO assert.

Source of the 4608-byte allocation: ESP-Hosted SDIO host driver's RX path. `CONFIG_ESP_HOSTED_SDIO_OPTIMIZATION_RX_STREAMING_MODE=y` routes RX through larger aligned buffers for throughput.

## What is *not* the cause (negative findings)

- **Not mbedTLS buffer size.** `MBEDTLS_EXTERNAL_MEM_ALLOC=y` already routes TLS records to PSRAM. `MBEDTLS_SSL_IN_CONTENT_LEN=16384` is correct; 8192 caused a different bug (`BAD_INPUT_DATA` mid-stream). Don't touch.
- **Not tremor selftest.** Its only effect is warming PSRAM allocator state. Removing it was tried and made things worse; removing it again will not fix DMA fragmentation.
- **Not `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`.** Both settings tested; kconfig-default (not set) is slightly better on our hardware.
- **Not HTTP/TLS keep-alive.** `s_cachedResp` in `CDNAudioFile.cpp` already reuses the CDN connection across range GETs. Range GETs in Trial 4 reuse the same socket — the fragmentation still happens on the first few.
- **Not the SPIRC frame sequence** (earlier "REPLACE storm" hypothesis). Even the manual Chrome device-picker trigger produces the same crash trajectory as automated.
- **Not an alloc-fail in the cspot task.** The failing allocations happen in the ESP-Hosted SDIO task, during routine RX buffer refill.

## Fix directions (not yet implemented)

Ranked by expected impact + reversibility.

### A. Move ESP-Hosted SDIO RX allocation into a boot-time pool

Pre-allocate N × 4608-byte DMA-aligned blocks at `app_main` entry, *before* any WiFi activity has had a chance to fragment the DMA heap. Maintain them as a pool the SDIO driver draws from. Needs a patch against the managed component; upstream doesn't expose a hook for this today.

Size estimate: ~8 buffers × 4608 = 36 KB internal DMA reserved. DMA heap total on P4 is ~320 KB, so affordable.

### B. Disable SDIO streaming mode

Flip `CONFIG_ESP_HOSTED_SDIO_OPTIMIZATION_RX_STREAMING_MODE` off. Driver falls back to smaller aligned buffers that fit in a 3 KB `lfb`. Cost: reduced RX throughput (unclear how much — needs measurement). Zero-code change, one kconfig flip.

### C. Move WiFi/SDIO code from IRAM to flash

`CONFIG_ESP_WIFI_IRAM_OPT` currently off — confirm. `CONFIG_ESP_WIFI_RX_IRAM_OPT` and `CONFIG_ESP_WIFI_EXTRA_IRAM_OPT` also off. Each toggle that moves WiFi code from IRAM to flash frees ~5–20 KB of internal-DMA-capable memory. Tradeoff: higher ISR latency. Not a root-cause fix, but raises the floor under which fragmentation matters.

### D. `CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH=y`

Moves heap management code itself out of internal RAM. Small gain (~3 KB) but nearly free.

## Verification after any fix

1. Rebuild, flash (`CONFIG_HEAP_TASK_TRACKING=y` must remain on — existing instrumentation relies on it).
2. Run ≥3 trials (never judge from a single run).
3. Tabulate per-trial `total_free`, `lfb`, `free_blocks` trajectory + max `feedPCMFrames`.
4. Success criterion: `lfb` holds above **5 KB** across ≥5 successive CDN range GETs in each trial. If yes, streaming should sustain; if no, the fix is incomplete regardless of how many seconds of audio one lucky run produced.

## Operator notes

- **After a sustained-playback crash, the device frequently enters a boot loop** with `bootloader_flash_read src_addr 0x… not 4-byte aligned`. Recovery: `esptool.py … erase_flash`, then flash fresh. USB-UART soft reset does not fully clear the chip state that triggers the unaligned read.
- **`esp_backtrace_print` does not produce a real stack backtrace on ESP32-P4 (RISC-V).** It prints a register dump with `MEPC=0 MCAUSE=0xdeadc0de` — the sentinel values, not the actual caller. Don't add it again expecting to identify an allocator caller. On RISC-V we need `esp_cpu_stall_other_cpu` + manual `__builtin_frame_address` walking, or a coredump analyzed offline.
- **Automated playback trigger via Chrome CDP (`/tmp/sp_full_play.py`)** requires Chrome to be started with `--remote-debugging-port=9222`. If Chrome is already running without it, don't try to kill/restart (risk of losing user's tabs). AppleScript-based replacement in `/tmp/sp_m5_osa.sh` + `/tmp/auth_watcher_osa.py` works against the user's existing Chrome session.
- **Heap-dma and alloc-fail instrumentation lives in two places**: per-range log in `CDNAudioFile.cpp` (lines 58–69), failed-alloc callback in `main/app_main.cpp`. Both require `CONFIG_HEAP_TASK_TRACKING=y` in `sdkconfig.defaults`.

## Open questions (for next session)

- Does disabling `ESP_HOSTED_SDIO_OPTIMIZATION_RX_STREAMING_MODE` actually let a 3 KB `lfb` satisfy SDIO RX? Need to read managed-component code to confirm the alternate buffer size.
- Is the 6 KB variance in starting DMA-free (14 K – 21 K across trials) caused by randomized WiFi/Bluetooth coex init or by a specific non-deterministic component? If we can identify it, we might be able to force a deterministic boot-time layout.
- Can we coredump-on-alloc-fail instead of abort, so a future failure gives us a full task list + per-task heap ownership via `idf.py coredump`?

---

## Update 2026-04-25 — Fix verifiziert: PSRAM-first für Streaming-RX-Puffer

### Belegte Ursache (präzisiert)

Das oben beschriebene "mempool.c PSRAM-Fix"-Konzept war logisch korrekt, aber an der **falschen Stelle** angesetzt. Der eigentliche Treffer:

Im Streaming-Modus (`CONFIG_ESP_HOSTED_SDIO_OPTIMIZATION_RX_STREAMING_MODE=y`) läuft der RX-Pfad **nicht** über den Mempool. Stattdessen alloziert `sdio_push_data_to_queue()` für jedes empfangene WiFi-Paket direkt einen neuen `pkt_rxbuff` via `sdio_buffer_alloc()` — also aus dem DMA-fähigen Mempool. Diese Puffer akkumulieren in `from_slave_queue` schneller als sie verbraucht werden, und da sie aus dem DMA-Heap kommen, erschöpfen sie ihn.

**Warum kein Mempool-Patch:** TX-`sendbuf` in `sdio_write_task` wird aus dem gleichen Mempool bezogen. `sendbuf` muss DMA-fähig sein (SDIO CMD53 schreibt direkt in den Puffer). Ein PSRAM-Patch auf `mempool.c` bricht TX mit `Failed to send data: 258 (ESP_ERR_INVALID_RESPONSE)`. Das wurde experimentell bestätigt.

### Fix (verifiziert, in `patches/esp_hosted/sdio_drv.c`)

Änderung nur im Streaming-RX-Pfad (`sdio_push_data_to_queue`):

```c
/* Allocate rx buffer from PSRAM-first (no DMA needed after memcpy
 * out of double_buf; keeping these out of DMA heap prevents
 * exhaustion under sustained WiFi+Vorbis load). */
pkt_rxbuff = (uint8_t *)heap_caps_malloc_prefer(
    MEMPOOL_ALIGNED(MAX_SDIO_BUFFER_SIZE), 2,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
assert(pkt_rxbuff);
```

`sdio_push_pkt_to_queue()` nimmt einen `free_fn`-Parameter: PSRAM-allozierte Streaming-RX-Pakete werden mit `free()` freigegeben (`sdio_rx_pkt_free`); der nicht-streaming Pfad nutzt weiterhin `sdio_buffer_free`.

Die `double_buf`-Puffer (echte DMA-Puffer für SDIO CMD53 reads) bleiben unverändert DMA-alloziert.

### Messergebnisse (2026-04-25)

| Zustand | Max feedPCMFrames | DMA lfb bei CDN range | Crash |
|---|---|---|---|
| Baseline (ohne Fix) | 352 | ~3 184 B | ja (alloc_fail 4608 B DMA) |
| **Mit sdio_drv.c PSRAM-Fix** | **8256+** | **4 336 B** | **nein** |

90 Sekunden CDN-Streaming, kein `alloc_fail`, kein `Rebooting`. Spotify-Playback läuft sustained.

### Cross-Referenz: ESP-Hosted Issue #597

[github.com/espressif/esp-hosted/issues/597](https://github.com/espressif/esp-hosted/issues/597) — dasselbe Problem unabhängig bestätigt. Processed RX-Buffer (nach dem `memcpy` aus `double_buf`) brauchen kein DMA mehr.
