#include "Tab5AudioSink.h"
#include <bsp/m5stack_tab5.h>
#include <esp_log.h>
#include <driver/i2s_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "Tab5Sink";

Tab5AudioSink::Tab5AudioSink() {
    softwareVolumeControl = false;
}

bool Tab5AudioSink::setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {
    _sampleRate = sampleRate;
    _channels   = channelCount;
    _bitDepth   = bitDepth;

    auto* codec = bsp_get_codec_handle();
    if (!codec || !codec->i2s_reconfig_clk_fn) {
        ESP_LOGE(TAG, "codec handle not ready");
        return false;
    }

    i2s_slot_mode_t slotMode = (channelCount == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;

    // Factory order (hal_audio.cpp): mute → reconfig → unmute.
    if (codec->set_mute) codec->set_mute(true);
    esp_err_t ret = codec->i2s_reconfig_clk_fn(sampleRate, bitDepth, slotMode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_reconfig_clk_fn failed: %s", esp_err_to_name(ret));
        return false;
    }
    if (codec->set_mute) codec->set_mute(false);
    ESP_LOGI(TAG, "audio params: %lu Hz, %u ch, %u bit", (unsigned long)sampleRate, channelCount, bitDepth);
    return true;
}

static uint32_t s_frames_called = 0;
static uint32_t s_total_written = 0;

void Tab5AudioSink::feedPCMFrames(const uint8_t* buffer, size_t bytes) {
    auto* codec = bsp_get_codec_handle();
    if (!codec || !codec->i2s_write) {
        if ((s_frames_called++ & 0xFF) == 0) {
            ESP_LOGW(TAG, "feedPCMFrames: codec/i2s_write NULL");
        }
        return;
    }

    if (s_frames_called == 0) {
        ESP_LOGI(TAG, "feedPCMFrames: FIRST CALL, bytes=%u (entering resample+write path)",
                 (unsigned)bytes);
        // Force unmute + full volume on first real feed. hal_esp32.cpp:75 calls
        // set_mute(true) right after bsp_codec_init and nothing unmutes until
        // our setParams — which may run before the codec is fully enabled.
        if (codec->set_mute)   codec->set_mute(false);
        if (codec->set_volume) codec->set_volume(80);  // 80% — clearly audible
        ESP_LOGI(TAG, "forced unmute + vol=80 before first i2s_write");
    }

    // Resample 44.1 kHz → 48 kHz (Q16 phase, linear interpolation).
    // Step per output sample = 44100/48000 in input-index units = 60218 (Q16).
    static constexpr uint32_t kStep = (uint32_t)((44100ULL << 16) / 48000ULL);
    const int16_t* in = reinterpret_cast<const int16_t*>(buffer);
    const size_t inFrames = bytes / 4;  // 2 ch × 16-bit
    if (inFrames == 0) return;

    // Virtual input of length inFrames+1: [prev, in[0], ..., in[inFrames-1]].
    // Output count bounded by inFrames * 48000/44100 + margin.
    // Fixed-size buffer — no heap alloc on hot path. 2 KiB input (CSpotPlayer's
    // chunk size) resamples to ~2.24 KiB stereo output; 8 KiB margin is plenty.
    static int16_t outBuf[4096];  // 8 KiB, up to 2048 stereo frames
    const size_t maxOut = inFrames * 48000 / 44100 + 4;
    if (maxOut * 2 > sizeof(outBuf) / sizeof(outBuf[0])) return;  // input too large

    uint32_t phase = _phase;
    int16_t prevL = _prevL, prevR = _prevR;
    size_t outFrames = 0;

    while (phase < (uint32_t)(inFrames << 16)) {
        uint32_t idx  = phase >> 16;
        uint32_t frac = phase & 0xFFFF;
        int32_t aL = (idx == 0) ? prevL : in[(idx - 1) * 2];
        int32_t aR = (idx == 0) ? prevR : in[(idx - 1) * 2 + 1];
        int32_t bL = in[idx * 2];
        int32_t bR = in[idx * 2 + 1];
        int32_t oL = aL + (((bL - aL) * (int32_t)frac) >> 16);
        int32_t oR = aR + (((bR - aR) * (int32_t)frac) >> 16);
        outBuf[outFrames * 2]     = (int16_t)oL;
        outBuf[outFrames * 2 + 1] = (int16_t)oR;
        ++outFrames;
        phase += kStep;
    }

    _phase = phase - (uint32_t)(inFrames << 16);
    _prevL = in[(inFrames - 1) * 2];
    _prevR = in[(inFrames - 1) * 2 + 1];

    // Test tone: override the first ~50 calls (~600ms @ 48kHz/2KB chunks)
    // with a 1 kHz square wave. If we hear a beep, the DAC/amp path is alive
    // and the problem is codec config / encoding; if we hear nothing, the
    // output stage itself is dead.
    if (s_frames_called < 50) {
        for (size_t i = 0; i < outFrames; ++i) {
            int16_t sample = ((i / 24) & 1) ? 12000 : -12000;  // ~1kHz @ 48kHz
            outBuf[i * 2]     = sample;
            outBuf[i * 2 + 1] = sample;
        }
    }

    size_t written = 0;
    // Factory uses portMAX_DELAY (hal_audio.cpp:71). bsp_i2s_write ignores
    // timeout anyway — esp_codec_dev_write blocks until DMA descriptor frees.
    uint32_t t_start = xTaskGetTickCount();
    esp_err_t err = codec->i2s_write((void*)outBuf, outFrames * 4,
                                     &written, portMAX_DELAY);
    uint32_t t_elapsed = xTaskGetTickCount() - t_start;
    s_total_written += written;

    // First 8 calls: log every one to diagnose DMA drain.
    if (s_frames_called < 8) {
        ESP_LOGI(TAG, "feedPCMFrames[%lu]: err=0x%x, in=%u, out=%u, written=%u, took=%lums",
                 (unsigned long)s_frames_called, err, (unsigned)bytes,
                 (unsigned)(outFrames * 4), (unsigned)written,
                 (unsigned long)(t_elapsed * portTICK_PERIOD_MS));
    }
    if ((++s_frames_called & 0x1F) == 0) {  // every 32 calls
        ESP_LOGI(TAG, "feedPCMFrames: calls=%lu, err=0x%x, in=%u, out=%u, written=%u, total=%lu",
                 (unsigned long)s_frames_called, err, (unsigned)bytes,
                 (unsigned)(outFrames * 4), (unsigned)written,
                 (unsigned long)s_total_written);
    }
}

void Tab5AudioSink::volumeChanged(uint16_t volume) {
    auto* codec = bsp_get_codec_handle();
    if (!codec || !codec->set_volume) return;

    // cspot sends 0–65535; Tab5 codec expects 0–100
    int vol = (int)((volume * 100UL) / 65535UL);
    codec->set_volume(vol);
    ESP_LOGI(TAG, "volume: %u → %d%%", volume, vol);
}
