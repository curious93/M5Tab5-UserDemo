# cspot on M5Tab5 (ESP32-P4 Rev 1.3) — Status 2026-04-19

## TL;DR

Everything works **except** reading Spotify CDN body bytes. The failure is a
known ESP-IDF 5.4.2 mbedTLS bug on ESP32-P4 Rev 1.3 that Espressif declined
to fix upstream ([esp-idf #15043](https://github.com/espressif/esp-idf/issues/15043)).

## What works (verified end-to-end)

- **Audio hardware**: ES8388 DAC + PI4IO SPK_EN + amp. Proven by 1.5 s 1 kHz
  self-test tone at boot ([hal_esp32.cpp:71-95](../../main/hal/hal_esp32.cpp#L71-L95))
  — user confirmed audible.
- **Tremor decoder on P4 + PSRAM**: standalone self-test with embedded 2 s
  OGG decodes cleanly, peak=2900 PCM samples, audible sine tone
  ([cspot_idf/tremor_selftest.cpp](tremor_selftest.cpp), embedded
  [test_tone.ogg](test_tone.ogg)). Tremor PSRAM malloc routing in
  [bell/external/tremor/os_types.h](../cspot/cspot/bell/external/tremor/os_types.h#L35-L62).
- **Resampler 44.1 → 48 kHz (Q16 linear)**: IN/OUT peaks identical within
  ± 5 samples during Tremor selftest
  ([Tab5AudioSink.cpp](Tab5AudioSink.cpp#L58-L95)).
- **cspot auth + blob persistence + LOAD-frame handling**: authenticated OK
  via NVS/embedded blob, Web Player transfer reaches TrackPlayer with
  correct track IDs.
- **I2S out channel**: STEREO 48 kHz + ES8388 i2s_reconfig_clk_fn + mute/unmute
  sequence producing audible output via `codec_dev->i2s_write`.

## What doesn't work

**CDN Range GET response body reads return 0 bytes** via *any* HTTP layer
we've tried:

1. `bell::HTTPClient` (cspot's upstream HTTP client): `mbedtls_ssl_read`
   returns `-0x7100` (MBEDTLS_ERR_SSL_WANT_READ) in an infinite loop.
2. `esp_http_client` (ESP-IDF-native): first `esp_http_client_read` returns
   ESP_FAIL (-1). Internal log shows `esp-tls-mbedtls: read error :-0x7100`
   with `errno=Success` — same underlying WANT_READ issue.

HTTP response headers arrive correctly (`status=206`, `content_len=16384`
or `262144`), so the TLS handshake and header exchange work. Only the
body-read path fails.

## Root cause (from research)

[esp-idf #15043](https://github.com/espressif/esp-idf/issues/15043):
`mbedtls_net_recv` returns `MBEDTLS_ERR_SSL_WANT_READ` when the socket
`recv()` returns EAGAIN, even on notionally-blocking sockets. No layer
above retries. Closed as "Won't Do".

On P4 Rev 1.3 with ESP-IDF 5.4.2 + managed mbedtls component, reading a
Spotify CDN range body reliably triggers this. The first initial read
(header request) often works because cold TCP/TLS records arrive alone;
every subsequent range request hits the bug when header + body TLS records
coalesce.

## Workarounds attempted (did not resolve)

1. Loop `.read()` with back-off + `clear()` (retry loop up to 15 s).
2. Replace `mbedtls_net_recv` with `mbedtls_net_recv_timeout` +
   `mbedtls_ssl_conf_read_timeout(5000)` — still returns WANT_READ,
   not TIMEOUT.
3. Close prior `httpConnection` before fresh TLS connection (not two
   simultaneous TLSSocket instances).
4. Bump `CONFIG_LWIP_MAX_SOCKETS` 10 → 16.
5. Swap `bell::HTTPClient` for ESP-IDF-native `esp_http_client` with
   `esp_crt_bundle_attach`.
6. Drop chunk size 256 KB → 16 KB (librespot reference value).
7. Bump `CONFIG_LWIP_TCP_WND_DEFAULT` 5760 → 65535 and enable
   `CONFIG_LWIP_SO_RCVBUF`.
8. Preserve header-buffer body tail (body bytes that came in same TLS
   record as headers) via `Response::readBody()` —
   [HTTPClient.cpp:94-116](../cspot/cspot/bell/main/io/HTTPClient.cpp#L94-L116).

Each one is correct / defensive but none independently unblocks the body
read — the underlying TLS-layer EAGAIN→WANT_READ bug precedes them.

## Next steps (not attempted today)

1. **ESP-IDF 5.3.x**: pre-dates some mbedTLS-component changes. Would need
   fresh clone (not present locally). P4 Rev 1.3 was supported since 5.2 so
   it should boot.
2. **ESP-IDF 5.5.4** with `CONFIG_ESP32P4_REV_MIN_0` (or similar) to force
   compiling for pre-3.1 instructions. CLAUDE.md warns 5.5's *default*
   instruction set crashes Rev 1.3, but the Kconfig option explicitly
   targets older revs.
3. **Raw lwIP socket + manual mbedtls_ssl_* calls** bypassing esp-tls
   entirely, with our own WANT_READ retry semantic in the BIO callback.

All three are multi-hour undertakings with uncertain outcomes.

## Diagnostic scaffolding left in place

- `tremor_selftest_run()` called before `cspot_start()` in
  [app_main.cpp](../../main/app_main.cpp). Boot-time audible sanity check.
- PCM peak log every 32 `feedPCMFrames` calls
  ([Tab5AudioSink.cpp](Tab5AudioSink.cpp#L116-L138)).
- DIAG decrypt + OggS scanner after each `decrypt()`
  ([CDNAudioFile.cpp](../cspot/cspot/src/CDNAudioFile.cpp#L297-L325)).
- `esp_http_range_get` status + content_len + total log
  ([CDNAudioFile.cpp:91](../cspot/cspot/src/CDNAudioFile.cpp#L91)).
- Per-read `n` / `complete` log for first 5 reads of each range fetch.
- Verbose ESP_LOG levels for HTTP_CLIENT / esp-tls / mbedtls in
  [app_main.cpp](../../main/app_main.cpp#L17-L24).

Leave these in when picking up — they pinpointed the bug in a single
test cycle after the research identified the candidate.

## Key files

- [CDNAudioFile.cpp](../cspot/cspot/src/CDNAudioFile.cpp) — HTTP range
  fetcher (now using `esp_http_client` + retry loop).
- [CMakeLists.txt](CMakeLists.txt) — adds `esp_http_client` + `esp-tls` to
  cspot target's REQUIRES and PRIVATE libs.
- [sdkconfig.defaults](../../sdkconfig.defaults) — TCP window 64 KB,
  SO_RCVBUF, LWIP_MAX_SOCKETS=16.
- [Tab5AudioSink.cpp](Tab5AudioSink.cpp) — resampler + i2s_write sink.
- [tremor_selftest.cpp](tremor_selftest.cpp) — standalone decoder
  validation (passes).

## Commits

Auto-commit hook at `~/.claude/hooks/auto_commit_on_build.py` has captured
every green build today as `checkpoint(main)` / `checkpoint(cspot)` /
`checkpoint(bell)`. `git log --oneline` in each shows the full iteration
trail.
