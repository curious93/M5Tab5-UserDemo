#include "app_template.h"
#include "../ui_header.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace {
constexpr uint32_t COL_BG   = 0x0F1420;
constexpr uint32_t COL_TEXT = 0xE6EEFF;
const char* TAG = "template";
}

AppTemplate::AppTemplate()
{
    setAppInfo().name = "Template";
}

void AppTemplate::onCreate()
{
    ESP_LOGI(TAG, "onCreate");
    open();
}

void AppTemplate::onOpen()
{
    ESP_LOGI(TAG, "onOpen");
    buildUI();
}

void AppTemplate::onClose()
{
    ESP_LOGI(TAG, "onClose");
    destroyUI();
}

void AppTemplate::requestClose()
{
    close();
}

void AppTemplate::buildUI()
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

    ui_header_create(_root, "Template", [](void* user) {
        static_cast<AppTemplate*>(user)->requestClose();
    }, this);

    lv_obj_t* hello = lv_label_create(_root);
    lv_label_set_text(hello, "Hello");
    lv_obj_set_style_text_color(hello, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(hello, &lv_font_montserrat_32, 0);
    lv_obj_center(hello);

    lvgl_port_unlock();
}

void AppTemplate::destroyUI()
{
    if (!_root) return;
    if (!lvgl_port_lock(-1)) return;
    lv_obj_del(_root);
    _root = nullptr;
    lvgl_port_unlock();
}
