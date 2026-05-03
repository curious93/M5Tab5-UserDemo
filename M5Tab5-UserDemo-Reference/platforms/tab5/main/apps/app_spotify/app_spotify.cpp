#include "app_spotify.h"
#include "../ui_header.h"
#include "cspot_idf.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/hal_esp32.h"
#include "wifi_credentials.h"
#include <cstdio>

extern HalEsp32* GetTab5Hal();

namespace {
constexpr uint32_t COL_BG      = 0x0A1A0A;
constexpr uint32_t COL_GREEN   = 0x1DB954;  // Spotify green
constexpr uint32_t COL_TEXT    = 0xE0FFE0;
constexpr uint32_t COL_DIM     = 0x80A080;
constexpr uint32_t COL_BTN_BG  = 0x1A301A;
const char* TAG = "app_spotify";

void format_mmss(char* buf, size_t cap, uint32_t ms) {
    uint32_t s = ms / 1000;
    std::snprintf(buf, cap, "%lu:%02lu",
                  (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// Static event callbacks (LVGL style)
void btn_prev_cb(lv_event_t*)        { cspot_ctrl_prev(); }
void btn_next_cb(lv_event_t*)        { cspot_ctrl_next(); }
void btn_playpause_cb(lv_event_t*)   {
    cspot_ui_state_t s = cspot_ui_state_get();
    cspot_ctrl_set_pause(!s.is_paused);
}
void vol_slider_cb(lv_event_t* e) {
    auto code = lv_event_get_code(e);
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_VALUE_CHANGED) {
        int v = lv_slider_get_value(slider);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        cspot_ctrl_set_volume_pct((uint8_t)v);
    }
}

} // namespace

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
    GetTab5Hal()->startWifiSta(WIFI_PRIMARY_SSID, WIFI_PRIMARY_PASSWORD);
    cspot_start(CONFIG_CSPOT_DEVICE_NAME);
    setStatus("Waiting for Spotify…\n\nOpen Spotify on your phone or open.spotify.com\nand select \"" CONFIG_CSPOT_DEVICE_NAME "\"");
}

void AppSpotify::onRunning()
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - _tickLast < 500) return;
    _tickLast = now;

    if (!cspot_is_running()) {
        setStatus("Spotify Connect stopped.\nRestart app to reconnect.");
        return;
    }
    refreshFromState();
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

    _statusLabel = lv_label_create(_root);
    lv_label_set_text(_statusLabel, "Starting…");
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_statusLabel, LV_PCT(85));
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(_statusLabel, LV_ALIGN_CENTER, 0, 0);

    lvgl_port_unlock();
}

void AppSpotify::buildPlayerUI()
{
    if (_uiBuilt) return;
    if (!lvgl_port_lock(-1)) return;

    // Hide status label, build full player layout
    if (_statusLabel) {
        lv_obj_add_flag(_statusLabel, LV_OBJ_FLAG_HIDDEN);
    }

    // Big album art placeholder (top-center)
    lv_obj_t* art = lv_obj_create(_root);
    lv_obj_remove_style_all(art);
    lv_obj_set_size(art, 280, 280);
    lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_color(art, lv_color_hex(COL_BTN_BG), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(art, 12, 0);
    lv_obj_clear_flag(art, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* note = lv_label_create(art);
    lv_label_set_text(note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(note, lv_color_hex(COL_GREEN), 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_44, 0);
    lv_obj_center(note);

    // Track title
    _trackLabel = lv_label_create(_root);
    lv_label_set_text(_trackLabel, "—");
    lv_obj_set_style_text_color(_trackLabel, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(_trackLabel, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(_trackLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_trackLabel, LV_PCT(90));
    lv_label_set_long_mode(_trackLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(_trackLabel, LV_ALIGN_TOP_MID, 0, 390);

    // Artist
    _artistLabel = lv_label_create(_root);
    lv_label_set_text(_artistLabel, "");
    lv_obj_set_style_text_color(_artistLabel, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(_artistLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(_artistLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_artistLabel, LV_PCT(90));
    lv_label_set_long_mode(_artistLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(_artistLabel, LV_ALIGN_TOP_MID, 0, 432);

    // Progress bar
    _progressBar = lv_bar_create(_root);
    lv_obj_set_size(_progressBar, 480, 8);
    lv_obj_align(_progressBar, LV_ALIGN_TOP_MID, 0, 480);
    lv_bar_set_range(_progressBar, 0, 1000);
    lv_obj_set_style_bg_color(_progressBar, lv_color_hex(COL_BTN_BG), 0);
    lv_obj_set_style_bg_color(_progressBar, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);

    // Time label below bar
    _timeLabel = lv_label_create(_root);
    lv_label_set_text(_timeLabel, "0:00 / 0:00");
    lv_obj_set_style_text_color(_timeLabel, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(_timeLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(_timeLabel, LV_ALIGN_TOP_MID, 0, 498);

    // Control buttons (Prev / PlayPause / Next)
    auto make_btn = [&](int x, const char* sym, lv_event_cb_t cb) {
        lv_obj_t* btn = lv_btn_create(_root);
        lv_obj_set_size(btn, 96, 96);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, x, 530);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BTN_BG), 0);
        lv_obj_set_style_radius(btn, 48, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, sym);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        return std::make_pair(btn, lbl);
    };
    auto [prev, prevLbl] = make_btn(-130, LV_SYMBOL_PREV,  btn_prev_cb);
    auto [pp,   ppLbl  ] = make_btn(   0, LV_SYMBOL_PLAY,  btn_playpause_cb);
    auto [nxt,  nxtLbl ] = make_btn( 130, LV_SYMBOL_NEXT,  btn_next_cb);
    _btnPrev = prev; _btnPlayPause = pp; _btnPlayPauseLabel = ppLbl; _btnNext = nxt;
    (void)prevLbl; (void)nxtLbl;

    // Volume slider
    _volSlider = lv_slider_create(_root);
    lv_obj_set_size(_volSlider, 480, 12);
    lv_obj_align(_volSlider, LV_ALIGN_TOP_MID, 0, 658);
    lv_slider_set_range(_volSlider, 0, 100);
    lv_slider_set_value(_volSlider, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_volSlider, lv_color_hex(COL_BTN_BG), 0);
    lv_obj_set_style_bg_color(_volSlider, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_volSlider, lv_color_hex(COL_GREEN), LV_PART_KNOB);
    lv_obj_add_event_cb(_volSlider, vol_slider_cb, LV_EVENT_RELEASED, nullptr);

    // WiFi info bottom
    _wifiLabel = lv_label_create(_root);
    lv_label_set_text(_wifiLabel, "");
    lv_obj_set_style_text_color(_wifiLabel, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(_wifiLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(_wifiLabel, LV_ALIGN_TOP_MID, 0, 690);

    _uiBuilt = true;
    lvgl_port_unlock();
}

void AppSpotify::refreshFromState()
{
    cspot_ui_state_t s = cspot_ui_state_get();
    if (!s.track_valid) {
        return;  // keep "Waiting for Spotify…" status until first TRACK_INFO
    }
    if (!_uiBuilt) {
        buildPlayerUI();
    }
    if (!_uiBuilt) return;
    if (!lvgl_port_lock(-1)) return;

    lv_label_set_text(_trackLabel,  s.track[0]  ? s.track  : "—");
    lv_label_set_text(_artistLabel, s.artist[0] ? s.artist : "");

    // Position: anchor + esp_timer drift; clamp to duration
    uint32_t pos = s.position_ms;
    if (!s.is_paused && s.position_ts_us) {
        uint64_t now_us  = (uint64_t)esp_timer_get_time();
        uint64_t drift_ms = (now_us - s.position_ts_us) / 1000;
        pos += (uint32_t)drift_ms;
    }
    if (s.duration_ms && pos > s.duration_ms) pos = s.duration_ms;

    if (s.duration_ms) {
        int32_t v = (int32_t)((uint64_t)pos * 1000 / s.duration_ms);
        lv_bar_set_value(_progressBar, v, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(_progressBar, 0, LV_ANIM_OFF);
    }

    char tposLine[32], tdurLine[32], tline[80];
    format_mmss(tposLine, sizeof(tposLine), pos);
    format_mmss(tdurLine, sizeof(tdurLine), s.duration_ms);
    std::snprintf(tline, sizeof(tline), "%s / %s", tposLine, tdurLine);
    lv_label_set_text(_timeLabel, tline);

    if (_btnPlayPauseLabel) {
        lv_label_set_text(_btnPlayPauseLabel,
                          s.is_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    }

    if (!_userDraggingSlider) {
        lv_slider_set_value(_volSlider, s.volume_pct, LV_ANIM_OFF);
    }

    if (s.wifi_ssid[0]) {
        char w[80];
        std::snprintf(w, sizeof(w), LV_SYMBOL_WIFI " %s  %d dBm",
                      s.wifi_ssid, (int)s.wifi_rssi);
        lv_label_set_text(_wifiLabel, w);
    }

    lvgl_port_unlock();
}

void AppSpotify::destroyUI()
{
    if (!_root) return;
    if (!lvgl_port_lock(-1)) return;
    lv_obj_del(_root);
    _root              = nullptr;
    _statusLabel       = nullptr;
    _trackLabel        = nullptr;
    _artistLabel       = nullptr;
    _wifiLabel         = nullptr;
    _timeLabel         = nullptr;
    _progressBar       = nullptr;
    _btnPrev           = nullptr;
    _btnPlayPause      = nullptr;
    _btnNext           = nullptr;
    _btnPlayPauseLabel = nullptr;
    _volSlider         = nullptr;
    _spinner           = nullptr;
    _uiBuilt           = false;
    lvgl_port_unlock();
}

void AppSpotify::setStatus(const char* text)
{
    if (!_statusLabel || _uiBuilt) return;
    if (!lvgl_port_lock(-1)) return;
    lv_label_set_text(_statusLabel, text);
    lvgl_port_unlock();
}
