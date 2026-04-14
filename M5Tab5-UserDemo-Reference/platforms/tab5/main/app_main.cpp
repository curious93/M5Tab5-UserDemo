#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/hal_esp32.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "app";
static HalEsp32 device_hal;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting Color Diagnostic...");

    device_hal.init();

    lv_display_t * lvDisp = device_hal.lvDisp;
    if (lvDisp == NULL) {
        ESP_LOGE(TAG, "Failed to get display handle!");
        return;
    }

    ESP_LOGI(TAG, "HAL OK. Building diagnostic UI...");

    if (lvgl_port_lock(-1)) {
        lv_obj_t * scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        // Drei horizontale Streifen für Kanal-Diagnose
        // Landscape: 1280 wide x 720 high
        struct { uint32_t color; const char* label; } stripes[] = {
            { 0xFF0000, "R=FF G=00 B=00" },
            { 0x00FF00, "R=00 G=FF B=00" },
            { 0x0000FF, "R=00 G=00 B=FF" },
        };

        for (int i = 0; i < 3; i++) {
            lv_obj_t * stripe = lv_obj_create(scr);
            lv_obj_set_size(stripe, lv_pct(100), lv_pct(33));
            lv_obj_set_pos(stripe, 0, i * (720 / 3));
            lv_obj_set_style_bg_color(stripe, lv_color_hex(stripes[i].color), 0);
            lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(stripe, 0, 0);
            lv_obj_set_style_pad_all(stripe, 0, 0);
            lv_obj_set_style_radius(stripe, 0, 0);

            lv_obj_t * lbl = lv_label_create(stripe);
            lv_label_set_text(lbl, stripes[i].label);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_center(lbl);
        }

        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Diagnostic UI rendered.");

    while (1) {
        ESP_LOGI(TAG, "heartbeat...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
