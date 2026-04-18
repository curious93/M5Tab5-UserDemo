#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hal/hal_esp32.h"
#include "cspot_idf.h"

static const char* TAG = "app";
static HalEsp32 g_hal;

HalEsp32* GetTab5Hal() { return &g_hal; }

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "booting...");
    g_hal.init();

    ESP_LOGI(TAG, "starting WiFi STA...");
    g_hal.startWifiSta("Ingrid", "Loggo03!");

    // Give WiFi time to connect before cspot needs the network
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "starting cspot...");
    cspot_start("M5Tab5");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
