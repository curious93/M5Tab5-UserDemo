# Firmware binary snapshots

Binaries from specific commits frozen outside the git tree, with enough detail to reflash the exact bits without rebuilding. Snapshot path is host-local (`~/.claude/tab5/binaries/`) to keep this repo small.

## `96d4943` — cspot 2-min-plus sustained audio (2026-04-21)

- **Tag:** `cspot-audio-2min-plus-2026-04-21`
- **Commit:** `96d4943` (main) / `a985672` (cspot) — 2026-04-21 10:10 +0200
- **Snapshot path:** `~/.claude/tab5/binaries/96d4943/`

**SHA256:**
| File | SHA256 | Size |
|---|---|---|
| `bootloader.bin` | `5893be960d89ea517aecf1202cb8bcb3ef7df71354d5d30b42e80094da678a76` | 23 264 B |
| `partition-table.bin` | `97ce22d502aaae32ae3de440ee5de7dc4782501c4faca6a91aef0dfe90ad256c` | 3 072 B |
| `m5stack_tab5.bin` | `ffbfe4b856dcfbd3a733ebf734ae7b57ee1af52995c93b301667be63e70f802d` | ~3.6 MB |

**Flash command:**
```bash
source ~/esp/esp-idf-v5.4.2-tab5/export.sh
esptool.py --chip esp32p4 --port /dev/cu.usbmodem101 --baud 921600 \
  --before=default_reset --after=hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x2000  ~/.claude/tab5/binaries/96d4943/bootloader.bin \
  0x8000  ~/.claude/tab5/binaries/96d4943/partition-table.bin \
  0x10000 ~/.claude/tab5/binaries/96d4943/m5stack_tab5.bin
```

**Why frozen:**

Longest stable Spotify playback of the session (11 328 `feedPCMFrames` ≈ 120 s within the post-flash 180 s monitoring window, no `sdio_rx_get_buffer` assert, user-confirmed audible). Previous record from `b92abe1`: 7 520 frames / ~86 s. The difference from `b92abe1`:
- `CONFIG_HEAP_TASK_TRACKING=y` added to `sdkconfig.defaults`
- Per-range `heap_caps_get_info` log + panic alloc-failed callback added to `CDNAudioFile.cpp` and `app_main.cpp` (both compiled in but their output did not appear in the log for reasons yet to be explained — the longer playback happened anyway)
- Full rebuild of `libcspot.a` (manually removed the cached `.obj` + `.a`)

Hypothesis: `HEAP_TASK_TRACKING`'s +4 bytes per allocation shifts DMA-heap geometry enough that the small (~181 B) SDIO RX allocation still fits contiguously where it previously could not. Not yet validated by instrumentation.

## `b92abe1` — cspot sustained-audio (2026-04-21)

- **Tag:** `cspot-audio-sustained-manual-trigger-2026-04-21`
- **Commit:** `b92abe1` (2026-04-21 01:16 +0200)
- **Snapshot path:** `~/.claude/tab5/binaries/b92abe1/`

**SHA256:**
| File | SHA256 | Size |
|---|---|---|
| `bootloader.bin` | `5893be960d89ea517aecf1202cb8bcb3ef7df71354d5d30b42e80094da678a76` | 23 264 B |
| `partition-table.bin` | `97ce22d502aaae32ae3de440ee5de7dc4782501c4faca6a91aef0dfe90ad256c` | 3 072 B |
| `m5stack_tab5.bin` | `7e36ad8cc76e65cfcfa558b5fedc4cb00dd2ed81f05b96aea511f86ec4676b80` | 3 647 360 B |

**Flash command (verified working):**
```bash
source ~/esp/esp-idf-v5.4.2-tab5/export.sh
esptool.py --chip esp32p4 --port /dev/cu.usbmodem101 --baud 921600 \
  --before=default_reset --after=hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x2000  ~/.claude/tab5/binaries/b92abe1/bootloader.bin \
  0x8000  ~/.claude/tab5/binaries/b92abe1/partition-table.bin \
  0x10000 ~/.claude/tab5/binaries/b92abe1/m5stack_tab5.bin
```

**Why frozen:**

First commit observed to sustain multi-minute Spotify playback on M5Tab5 when the user manually clicks M5Tab5 in Chrome's Spotify device picker and presses Play. Crashes after ~10 s under automated `sp_full_play.py` triggering — see memory `project_cspot_audio_sustained.md`. The firmware is correct; the next investigation is the SPIRC-frame-sequence difference between the two trigger paths.

**Key config (from `b92abe1:M5Tab5-UserDemo-Reference/platforms/tab5/sdkconfig`):**
- `tremor_selftest_run()` ACTIVE in `app_main.cpp` (warms PSRAM allocator)
- `# CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP is not set` → `STATIC_RX=10`
- `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` (8 KB causes BAD_INPUT_DATA mid-stream)
- `CONFIG_LWIP_TCP_WND_DEFAULT=65535`
- cspot submodule `8e2a891` (keep-alive `s_cachedResp`, PSRAM prefetch wrapped in `if(false)`)
- bell submodule `6c1fdd9` (diag log on TLS `BAD_INPUT_DATA`)

**Rebuild from source (should reproduce the same bits, barring toolchain drift):**
```bash
cd M5Tab5-UserDemo-Reference
git checkout cspot-audio-sustained-manual-trigger-2026-04-21
cd platforms/tab5
source ~/esp/esp-idf-v5.4.2-tab5/export.sh
export ESP_IDF_VERSION=5.4
idf.py build
shasum -a 256 build/m5stack_tab5.bin  # compare against SHA256 above
```

---

## Factory rescue (reference, not frozen by this project)

Per [CLAUDE.md](../../CLAUDE.md), the factory RESCUE binary is at:
- `~/.gemini/antigravity/scratch/M5_3/M5Tab5-UserDemo-Reference/binaries/official_factory_v0.2_rev1.3.bin`
- SHA256: `f3e0c860001c8eeda4fc4c3664077f408782ff213a3138e32c9f489f0d158827`
- Flash ab `0x0000` (singled-image dump, not split into bootloader/partition/app)
