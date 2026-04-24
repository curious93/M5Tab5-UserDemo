#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_debug_helpers.h"
#include "hal/hal_esp32.h"
#include "cspot_idf.h"
#include "sdio_drv.h"

static const char* TAG = "app";
static HalEsp32 g_hal;

HalEsp32* GetTab5Hal() { return &g_hal; }

// DIAG: invoked by the heap subsystem whenever any heap_caps_* allocation
// fails, *before* panic/abort runs. Prints the failing request + backtrace
// of the calling task so we can identify WHO demanded the DMA block (the
// reported function_name is just heap_caps_aligned_alloc — useless for
// attribution). Callback runs in the failing task's context — must NOT
// allocate; esp_backtrace_print and the heap dumps use stack only.
static void on_alloc_failed(size_t size, uint32_t caps, const char* function_name)
{
    ESP_LOGE("alloc_fail", "size=%u caps=0x%lx func=%s",
             (unsigned)size, (unsigned long)caps,
             function_name ? function_name : "?");
    heap_caps_print_heap_info(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "booting...");
    heap_caps_register_failed_alloc_callback(on_alloc_failed);
    {
        multi_heap_info_t i;
        heap_caps_get_info(&i, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "DMA-heap @boot: total=%u free=%u lfb=%u blk=%u",
                 (unsigned)i.total_allocated_bytes + (unsigned)i.total_free_bytes,
                 (unsigned)i.total_free_bytes,
                 (unsigned)i.largest_free_block,
                 (unsigned)i.allocated_blocks);
    }

    // DIAG: verbose logs for the HTTP/TLS stack so we can see why
    // esp_http_client_read returns -1 after a 206 response is received.
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_VERBOSE);
    esp_log_level_set("mbedtls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_ssl", ESP_LOG_VERBOSE);

    // cspot_player blocks CPU1 in I2S writes long enough to starve IDLE1,
    // which trips the Task Watchdog. Disable TWDT entirely — no-one here
    // relies on its error-detection. (esp_task_wdt_delete on IDLE leaves the
    // IDLE hook spamming "task not found", so we deinit the whole thing.)
    esp_task_wdt_deinit();

    g_hal.init();

    // Factory firmware starts SDIO/C6 init at ~7.5s uptime (after display +
    // audio + launcher are all up). We need to give the C6 similar time to
    // fully boot — otherwise SDIO init races the C6 ROM and fails.
    vTaskDelay(pdMS_TO_TICKS(6000));

    ESP_LOGI(TAG, "starting WiFi STA...");
    g_hal.startWifiSta("Ingrid", "Loggo03!");

    // Give WiFi time to connect before cspot needs the network
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Tremor selftest runs BEFORE cspot: decodes an embedded 350 KB OGG
    // through the full Vorbis stack. Its purpose is NOT the audible sine —
    // it's to warm up the allocator. Tremor uses heap_caps_malloc_prefer
    // (PSRAM first, internal fallback); running the full ov_open→ov_read→
    // ov_clear cycle establishes the PSRAM-first pattern for Vorbis
    // allocations. Without this, cspot's first TrackPlayer Vorbis state
    // lands in internal DMA-capable heap instead of PSRAM, and the few KB
    // it permanently steals is what pushes SDIO RX over the edge later.
    //
    // Measured 10:39 CEST today (live.log.1 lines 8914–11593): with
    // selftest ON, feedPCMFrames reached 4128 (~3 min sustained). With
    // selftest removed (2026-04-21 00:48 baseline) only 896 frames (~10 s).
    ESP_LOGI(TAG, "running tremor selftest (PSRAM allocator warmup)...");
    tremor_selftest_run();

    ESP_LOGI(TAG, "starting cspot...");
    cspot_start("M5Tab5");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
