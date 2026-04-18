#include "Tab5AudioSink.h"
#include <bsp/m5stack_tab5.h>
#include <esp_log.h>
#include <driver/i2s_types.h>

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

    size_t written = 0;
    esp_err_t err = codec->i2s_write((void*)buffer, bytes, &written, portMAX_DELAY);
    s_total_written += written;

    if ((++s_frames_called & 0xFF) == 0) {  // every 256 calls
        ESP_LOGI(TAG, "feedPCMFrames: calls=%lu, last err=0x%x, asked=%u, written=%u, total=%lu",
                 (unsigned long)s_frames_called, err, (unsigned)bytes,
                 (unsigned)written, (unsigned long)s_total_written);
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
