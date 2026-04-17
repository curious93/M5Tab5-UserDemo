#include "app_system_info.h"
#include "../ui_header.h"
#include "hal/hal_esp32.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdio.h>

extern HalEsp32* GetTab5Hal();

namespace {
constexpr uint32_t COL_BG    = 0x0F1420;
constexpr uint32_t COL_TEXT  = 0xE6EEFF;
constexpr uint32_t COL_MUTED = 0x8594B2;
constexpr int      ROW_GAP   = 64;
constexpr int      LEFT_MARGIN = 40;
const char* TAG = "sysinfo";
}

void system_info_timer_cb(lv_timer_t* t)
{
    auto* self = static_cast<AppSystemInfo*>(lv_timer_get_user_data(t));
    if (self) self->refresh();
}

AppSystemInfo::AppSystemInfo()
{
    setAppInfo().name = "System Info";
}

void AppSystemInfo::onCreate()
{
    ESP_LOGI(TAG, "onCreate");
    open();
}

void AppSystemInfo::onOpen()
{
    ESP_LOGI(TAG, "onOpen");
    buildUI();
}

void AppSystemInfo::onClose()
{
    ESP_LOGI(TAG, "onClose");
    destroyUI();
}

void AppSystemInfo::requestClose()
{
    close();
}

static lv_obj_t* make_row(lv_obj_t* parent, const char* caption, int y)
{
    lv_obj_t* cap = lv_label_create(parent);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(cap, LEFT_MARGIN, y);

    lv_obj_t* val = lv_label_create(parent);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(val, LEFT_MARGIN, y + 22);
    return val;
}

void AppSystemInfo::buildUI()
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

    ui_header_create(_root, "System Info", [](void* user) {
        static_cast<AppSystemInfo*>(user)->requestClose();
    }, this);

    int y = 96 + 40;
    _lbl_temp  = make_row(_root, "CPU Temperature", y);  y += ROW_GAP + 20;
    _lbl_heap  = make_row(_root, "Internal Heap",   y);  y += ROW_GAP + 20;
    _lbl_psram = make_row(_root, "PSRAM Free",      y);  y += ROW_GAP + 20;
    _lbl_up    = make_row(_root, "Uptime",          y);

    _timer = lv_timer_create(system_info_timer_cb, 1000, this);
    lvgl_port_unlock();

    refresh();
}

void AppSystemInfo::destroyUI()
{
    if (!_root) return;
    if (!lvgl_port_lock(-1)) return;
    if (_timer) { lv_timer_del(_timer); _timer = nullptr; }
    lv_obj_del(_root);
    _root = nullptr;
    lvgl_port_unlock();
}

void AppSystemInfo::refresh()
{
    auto* hal = GetTab5Hal();
    char buf[64];

    if (_lbl_temp && hal) {
        snprintf(buf, sizeof(buf), "%d °C", hal->getCpuTemp());
        lv_label_set_text(_lbl_temp, buf);
    }
    if (_lbl_heap) {
        size_t free_b = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(free_b / 1024));
        lv_label_set_text(_lbl_heap, buf);
    }
    if (_lbl_psram) {
        size_t free_b = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(free_b / 1024));
        lv_label_set_text(_lbl_psram, buf);
    }
    if (_lbl_up) {
        int64_t sec = esp_timer_get_time() / 1000000;
        int h  = (int)(sec / 3600);
        int m  = (int)((sec % 3600) / 60);
        int s  = (int)(sec % 60);
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        lv_label_set_text(_lbl_up, buf);
    }
}
