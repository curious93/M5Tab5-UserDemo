#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "hal/hal_esp32.h"
#include "cspot_idf.h"

static const char* TAG = "app";
static HalEsp32 g_hal;

HalEsp32* GetTab5Hal() { return &g_hal; }

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "booting...");

    // Remove IDLE tasks from Task Watchdog — cspot_player does long I2S-blocking
    // writes which starve IDLE and trip the WDT. Safe because we don't rely on
    // the IDLE WDT for anything here.
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));

    g_hal.init();

    // Factory firmware starts SDIO/C6 init at ~7.5s uptime (after display +
    // audio + launcher are all up). We need to give the C6 similar time to
    // fully boot — otherwise SDIO init races the C6 ROM and fails.
    vTaskDelay(pdMS_TO_TICKS(6000));

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
