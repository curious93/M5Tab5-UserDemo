#include "Tab5AudioSink.h"
#include <bsp/m5stack_tab5.h>
#include <esp_log.h>
#include <driver/i2s_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

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
    }

    // Resample 44.1 kHz → 48 kHz (Q16 phase, linear interpolation).
    // Step per output sample = 44100/48000 in input-index units = 60218 (Q16).
    static constexpr uint32_t kStep = (uint32_t)((44100ULL << 16) / 48000ULL);
    const int16_t* in = reinterpret_cast<const int16_t*>(buffer);
    const size_t inFrames = bytes / 4;  // 2 ch × 16-bit
    if (inFrames == 0) return;

    // Virtual input of length inFrames+1: [prev, in[0], ..., in[inFrames-1]].
    // Output count bounded by inFrames * 48000/44100 + margin.
    static std::vector<int16_t> outBuf;
    const size_t maxOut = inFrames * 48000 / 44100 + 4;
    if (outBuf.size() < maxOut * 2) outBuf.resize(maxOut * 2);

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

    size_t written = 0;
    // 200 ms timeout instead of portMAX_DELAY — avoid unbounded hang if I2S
    // channel is misconfigured; errors surface in the periodic log.
    esp_err_t err = codec->i2s_write((void*)outBuf.data(), outFrames * 4,
                                     &written, pdMS_TO_TICKS(200));
    s_total_written += written;

    if ((++s_frames_called & 0x1F) == 0) {  // every 32 calls (~350 ms @ 48kHz)
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
