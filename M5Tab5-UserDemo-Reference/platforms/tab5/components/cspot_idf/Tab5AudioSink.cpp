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

    ESP_LOGI(TAG, "audio params: %lu Hz, %u ch, %u bit", (unsigned long)sampleRate, channelCount, bitDepth);
    return true;
}

void Tab5AudioSink::feedPCMFrames(const uint8_t* buffer, size_t bytes) {
    auto* codec = bsp_get_codec_handle();
    if (!codec || !codec->i2s_write) return;

    size_t written = 0;
    codec->i2s_write((void*)buffer, bytes, &written, portMAX_DELAY);
}

void Tab5AudioSink::volumeChanged(uint16_t volume) {
    auto* codec = bsp_get_codec_handle();
    if (!codec || !codec->set_volume) return;

    // cspot sends 0–65535; Tab5 codec expects 0–100
    int vol = (int)((volume * 100UL) / 65535UL);
    codec->set_volume(vol);
    ESP_LOGI(TAG, "volume: %u → %d%%", volume, vol);
}
