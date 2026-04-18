#include "app_spotify.h"
#include "../ui_header.h"
#include "cspot_idf.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/hal_esp32.h"

extern HalEsp32* GetTab5Hal();

namespace {
constexpr uint32_t COL_BG      = 0x0A1A0A;
constexpr uint32_t COL_GREEN   = 0x1DB954;  // Spotify green
constexpr uint32_t COL_TEXT    = 0xE0FFE0;
const char* TAG = "app_spotify";
}

AppSpotify::AppSpotify()
{
    setAppInfo().name = "Spotify Connect";
}

void AppSpotify::onCreate()
{
    ESP_LOGI(TAG, "onCreate");
}

void AppSpotify::onOpen()
{
    ESP_LOGI(TAG, "onOpen");
    buildUI();
    GetTab5Hal()->startWifiSta("Ingrid", "Loggo03!");
    cspot_start(CONFIG_CSPOT_DEVICE_NAME);
    setStatus("Waiting for Spotify...\n\nOpen Spotify on your phone\nand select \"" CONFIG_CSPOT_DEVICE_NAME "\"");
}

void AppSpotify::onRunning()
{
    // Poll status every 500ms without blocking
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - _tickLast < 500) return;
    _tickLast = now;

    if (!cspot_is_running()) {
        setStatus("Spotify Connect stopped.\nRestart app to reconnect.");
    }
}

void AppSpotify::onClose()
{
    ESP_LOGI(TAG, "onClose");
    cspot_stop();
    destroyUI();
}

void AppSpotify::requestClose()
{
    close();
}

void AppSpotify::buildUI()
{
    if (!lvgl_port_lock(-1)) return;

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    _root = lv_obj_create(scr);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(_root, 0, 0);
    lv_obj_set_style_bg_color(_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    ui_header_create(_root, "Spotify Connect", [](void* user) {
        static_cast<AppSpotify*>(user)->requestClose();
    }, this);

    // Spotify logo circle
    lv_obj_t* circle = lv_obj_create(_root);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 80, 80);
    lv_obj_set_style_bg_color(circle, lv_color_hex(COL_GREEN), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(circle, 40, 0);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t* note = lv_label_create(circle);
    lv_label_set_text(note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(note, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_32, 0);
    lv_obj_center(note);

    _statusLabel = lv_label_create(_root);
    lv_label_set_text(_statusLabel, "Starting...");
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_statusLabel, LV_PCT(85));
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(_statusLabel, LV_ALIGN_TOP_MID, 0, 180);

    lvgl_port_unlock();
}

void AppSpotify::destroyUI()
{
    if (!_root) return;
    if (!lvgl_port_lock(-1)) return;
    lv_obj_del(_root);
    _root        = nullptr;
    _statusLabel = nullptr;
    _spinner     = nullptr;
    lvgl_port_unlock();
}

void AppSpotify::setStatus(const char* text)
{
    if (!_statusLabel) return;
    if (!lvgl_port_lock(-1)) return;
    lv_label_set_text(_statusLabel, text);
    lvgl_port_unlock();
}
