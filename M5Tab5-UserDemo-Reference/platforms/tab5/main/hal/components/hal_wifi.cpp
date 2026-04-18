/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/hal_esp32.h"
#include <mooncake_log.h>
#include <vector>
#include <memory>
#include <string.h>
#include <bsp/m5stack_tab5.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>

#define TAG "wifi"

#define WIFI_SSID    "M5Tab5-UserDemo-WiFi"
#define WIFI_PASS    ""
#define MAX_STA_CONN 4

// ── STA event handling ────────────────────────────────────────────────────────
static volatile bool s_sta_connected = false;

static void sta_event_handler(void* arg, esp_event_base_t base,
                               int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        ESP_LOGW(TAG, "STA disconnected, retrying...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        // Explicitly set Google DNS — ESP-Hosted sometimes doesn't propagate
        // the DHCP-provided DNS server to lwIP
        ip_addr_t dns1, dns2;
        IP_ADDR4(&dns1, 8, 8, 8, 8);
        IP_ADDR4(&dns2, 8, 8, 4, 4);
        dns_setserver(0, &dns1);
        dns_setserver(1, &dns2);
        ESP_LOGI(TAG, "DNS set to 8.8.8.8 / 8.8.4.4");
        s_sta_connected = true;
    }
}

// HTTP 处理函数
esp_err_t hello_get_handler(httpd_req_t* req)
{
    const char* html_response = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Hello</title>
            <style>
                body {
                    display: flex;
                    flex-direction: column;
                    justify-content: center;
                    align-items: center;
                    height: 100vh;
                    margin: 0;
                    font-family: sans-serif;
                    background-color: #f0f0f0;
                }
                h1 {
                    font-size: 48px;
                    color: #333;
                    margin: 0;
                }
                p {
                    font-size: 18px;
                    color: #666;
                    margin-top: 10px;
                }
            </style>
        </head>
        <body>
            <h1>Hello World</h1>
            <p>From M5Tab5</p>
        </body>
        </html>
    )rawliteral";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// URI 路由
httpd_uri_t hello_uri = {.uri = "/", .method = HTTP_GET, .handler = hello_get_handler, .user_ctx = nullptr};

// 启动 Web Server
httpd_handle_t start_webserver()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = nullptr;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &hello_uri);
    }
    return server;
}

// 初始化 Wi-Fi AP 模式
void wifi_init_softap()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), WIFI_SSID, sizeof(wifi_config.ap.ssid));
    std::strncpy(reinterpret_cast<char*>(wifi_config.ap.password), WIFI_PASS, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len       = std::strlen(WIFI_SSID);
    wifi_config.ap.max_connection = MAX_STA_CONN;
    wifi_config.ap.authmode       = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP started. SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
}

static void wifi_ap_test_task(void* param)
{
    wifi_init_softap();
    start_webserver();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

bool HalEsp32::wifi_init()
{
    mclog::tagInfo(TAG, "wifi init");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(wifi_ap_test_task, "ap", 4096, nullptr, 5, nullptr);
    return true;
}

void HalEsp32::setExtAntennaEnable(bool enable)
{
    _ext_antenna_enable = enable;
    mclog::tagInfo(TAG, "set ext antenna enable: {}", _ext_antenna_enable);
    bsp_set_ext_antenna_enable(_ext_antenna_enable);
}

bool HalEsp32::getExtAntennaEnable()
{
    return _ext_antenna_enable;
}

void HalEsp32::startWifiAp()
{
    wifi_init();
}

// ── STA init task ─────────────────────────────────────────────────────────────
// The Tab5 WiFi runs on an ESP32-C6 slave via SDIO (ESP-Hosted).
// startWifiSta() may be the very first WiFi call — wifi_init() / startWifiAp()
// is NOT called at boot.  This task owns the full init sequence.
// We MUST NOT call esp_wifi_stop() — it sends an SDIO command that panics when
// the transport isn't fully up, causing heap corruption (tlsf_free assert).
struct StaInitArgs {
    char ssid[32];
    char pass[64];
};

static void sta_init_task(void* arg)
{
    auto* a = static_cast<StaInitArgs*>(arg);
    ESP_LOGI(TAG, "sta_init_task: SSID=%s", a->ssid);

    // ── One-time WiFi stack init ──────────────────────────────────────────
    static bool s_wifi_init_done = false;
    if (!s_wifi_init_done) {
        // nvs_flash_init() was already called in HalEsp32::wifi_init(); call
        // again in case that path was skipped — ESP-IDF silently returns OK.
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }

        // esp_netif_init / event_loop may already be done; ignore "already init".
        esp_netif_init();
        esp_event_loop_create_default();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
            delete a; vTaskDelete(nullptr); return;
        }
        s_wifi_init_done = true;
        ESP_LOGI(TAG, "WiFi driver initialized");
    }

    // ── STA netif (once) ─────────────────────────────────────────────────
    static bool s_sta_netif_created = false;
    if (!s_sta_netif_created) {
        esp_netif_create_default_wifi_sta();
        s_sta_netif_created = true;
    }

    // ── Event handlers ────────────────────────────────────────────────────
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                &sta_event_handler, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                &sta_event_handler, nullptr);

    // ── Set STA mode + config BEFORE start (standard sequence) ───────────
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_mode STA: %s", esp_err_to_name(err));
        delete a; vTaskDelete(nullptr); return;
    }

    wifi_config_t sta_cfg = {};
    memcpy(sta_cfg.sta.ssid,     a->ssid, sizeof(sta_cfg.sta.ssid));
    memcpy(sta_cfg.sta.password, a->pass, sizeof(sta_cfg.sta.password));
    delete a;

    err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config: %s", esp_err_to_name(err));
        vTaskDelete(nullptr); return;
    }

    // ── Start WiFi — fires WIFI_EVENT_STA_START → sta_event_handler connects
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
        vTaskDelete(nullptr); return;
    }

    ESP_LOGI(TAG, "STA started, awaiting IP...");
    vTaskDelete(nullptr);
}

void HalEsp32::startWifiSta(const char* ssid, const char* pass)
{
    ESP_LOGI(TAG, "startWifiSta requested: SSID=%s", ssid);
    auto* args = new StaInitArgs{};
    memcpy(args->ssid, ssid, sizeof(args->ssid));
    memcpy(args->pass, pass, sizeof(args->pass));
    xTaskCreate(sta_init_task, "sta_init", 4096, args, 5, nullptr);
}

bool HalEsp32::isWifiStaConnected()
{
    return s_sta_connected;
}
