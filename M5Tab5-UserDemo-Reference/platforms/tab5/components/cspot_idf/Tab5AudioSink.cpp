#include "Tab5AudioSink.h"
#include <bsp/m5stack_tab5.h>
#include <esp_log.h>
#include <driver/i2s_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "Tab5Sink";

static uint32_t s_frames_called = 0;
static uint32_t s_total_written = 0;
static uint64_t s_last_feed_us  = 0;

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

    // Match the exact sequence of the boot-time self-test beep (hal_esp32.cpp)
    // that produces loud audible output. The previous mute(true) → reconfig →
    // mute(false) dance silenced the DAC output stage permanently in our
    // tests: writes returned err=0x0 + all bytes "written" but no sound.
    if (codec->set_volume) codec->set_volume(30);
    if (codec->set_mute)   codec->set_mute(false);
    // Reset PCM gap timer on new track so inter-track silence isn't flagged as a glitch.
    s_last_feed_us = 0;
    esp_err_t ret = codec->i2s_reconfig_clk_fn(sampleRate, bitDepth, slotMode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_reconfig_clk_fn failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "audio params: %lu Hz, %u ch, %u bit (vol=30, unmuted)",
             (unsigned long)sampleRate, channelCount, bitDepth);
    return true;
}

void Tab5AudioSink::feedPCMFrames(const uint8_t* buffer, size_t bytes) {
    // Detect gaps in PCM feed — a gap > 80 ms means the Vorbis decoder stalled
    // (CDN data not ready), causing an audible glitch in the output.
    uint64_t now_us = esp_timer_get_time();
    if (s_last_feed_us > 0) {
        uint32_t gap_ms = (uint32_t)((now_us - s_last_feed_us) / 1000);
        if (gap_ms > 80) {
            ESP_LOGW(TAG, "[AUDIO_GLITCH gap=%lu ms]", (unsigned long)gap_ms);
        }
    }
    s_last_feed_us = now_us;
    auto* codec = bsp_get_codec_handle();
    if (!codec || !codec->i2s_write) {
        if ((s_frames_called++ & 0xFF) == 0) {
            ESP_LOGW(TAG, "feedPCMFrames: codec/i2s_write NULL");
        }
        return;
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

    if (s_frames_called < 64 || (s_frames_called & 0x1F) == 0) {
        // input peak (from Vorbis decoder output, pre-resample)
        int32_t inPeak = 0;
        int64_t inSumSq = 0;
        for (size_t i = 0; i < inFrames * 2; ++i) {
            int32_t v = in[i]; if (v < 0) v = -v;
            if (v > inPeak) inPeak = v;
            inSumSq += (int64_t)in[i] * in[i];
        }
        uint32_t inRms = inFrames ? (uint32_t)__builtin_sqrt(inSumSq / (inFrames * 2)) : 0;
        // output peak (post-resample, goes to i2s_write)
        int32_t outPeak = 0;
        int64_t outSumSq = 0;
        for (size_t i = 0; i < outFrames * 2; ++i) {
            int32_t v = outBuf[i]; if (v < 0) v = -v;
            if (v > outPeak) outPeak = v;
            outSumSq += (int64_t)outBuf[i] * outBuf[i];
        }
        uint32_t outRms = outFrames ? (uint32_t)__builtin_sqrt(outSumSq / (outFrames * 2)) : 0;
        ESP_LOGI(TAG, "PCM IN peak=%ld rms=%lu first=[%d,%d,%d,%d] | OUT peak=%ld rms=%lu",
                 (long)inPeak, (unsigned long)inRms, in[0], in[1], in[2], in[3],
                 (long)outPeak, (unsigned long)outRms);
    }

    size_t written = 0;
    esp_err_t err = codec->i2s_write((void*)outBuf, outFrames * 4,
                                     &written, portMAX_DELAY);
    s_total_written += written;

    if ((++s_frames_called & 0x1F) == 0) {  // every 32 calls (~350 ms @ 48 kHz)
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
