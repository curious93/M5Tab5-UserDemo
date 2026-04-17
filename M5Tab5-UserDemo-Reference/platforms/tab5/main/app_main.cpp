/*
 * App Launcher entry point for the Tab5.
 *
 * Installs the launcher app and pumps the Mooncake scheduler in the main
 * task. UI and input run inside their own LVGL task started by HalEsp32::init().
 */
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hal/hal_esp32.h"
#include "mooncake.h"
#include "apps/app_launcher/app_launcher.h"

static const char* TAG = "app";
static HalEsp32 g_hal;

// Simple accessor used by apps (we don't link the hal::Inject singleton).
HalEsp32* GetTab5Hal() { return &g_hal; }

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Launcher booting...");

    g_hal.init();

    if (g_hal.lvDisp == nullptr) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }

    auto& mc = mooncake::GetMooncake();
    int launcher_id = mc.installApp(std::make_unique<AppLauncher>(&g_hal));
    mc.openApp(launcher_id);

    ESP_LOGI(TAG, "Launcher installed (id=%d)", launcher_id);

    while (true) {
        mc.update();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
