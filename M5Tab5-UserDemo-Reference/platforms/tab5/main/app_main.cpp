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
    ESP_LOGI(TAG, "Starting Level 1 Hardware Control...");

    // Initialize HAL (this handles display, i2c, etc.)
    device_hal.init();

    // Get the display handle
    lv_display_t * lvDisp = device_hal.lvDisp;
    if (lvDisp == NULL) {
        ESP_LOGE(TAG, "Failed to get display handle!");
        return;
    }

    ESP_LOGI(TAG, "HAL Initialization complete. Taking LVGL lock...");

    // Basic UI for verification
    if (lvgl_port_lock(-1)) {
        lv_obj_t * scr = lv_scr_act();
        
        // Clear screen with a visible background color
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x050505), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        // Create a blue rectangle
        lv_obj_t * rect = lv_obj_create(scr);
        lv_obj_set_size(rect, 400, 300);
        lv_obj_center(rect);
        lv_obj_set_style_bg_color(rect, lv_color_hex(0x0000FF), 0);
        lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rect, 5, 0);
        lv_obj_set_style_border_color(rect, lv_color_hex(0xFFFFFF), 0);

        // Add text
        lv_obj_t * label = lv_label_create(rect);
        lv_label_set_text(label, "CONTROL ESTABLISHED\nM5Stack Tab5 Stable");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);

        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "LVGL UI initialized.");

    // Life-Sign Loop (The BSP background task handles lv_timer_handler)
    while (1) {
        ESP_LOGI(TAG, "heartbeat loop...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
