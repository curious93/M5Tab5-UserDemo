#include "cspot_ui_state.h"

#include <cstring>
#include <atomic>
#include <mutex>

#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "SpircHandler.h"

namespace {

SemaphoreHandle_t g_mtx = nullptr;
cspot_ui_state_t  g_state = {};
std::atomic<cspot::SpircHandler*> g_handler{nullptr};

inline void copy_str(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = std::strlen(src);
    if (n > cap - 1) n = cap - 1;
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}

} // anonymous namespace

extern "C" void cspot_ui_state_init(void) {
    if (g_mtx) return;
    g_mtx = xSemaphoreCreateMutex();
    std::memset(&g_state, 0, sizeof(g_state));
    g_state.volume_pct = 50;
}

extern "C" cspot_ui_state_t cspot_ui_state_get(void) {
    cspot_ui_state_t snap = {};
    if (!g_mtx) return snap;
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    snap = g_state;
    xSemaphoreGive(g_mtx);
    return snap;
}

extern "C" void cspot_ui_state_set_track(const char* name, const char* artist,
                                         const char* album, const char* image_url,
                                         uint32_t duration_ms) {
    if (!g_mtx) return;
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    copy_str(g_state.track,     sizeof(g_state.track),     name);
    copy_str(g_state.artist,    sizeof(g_state.artist),    artist);
    copy_str(g_state.album,     sizeof(g_state.album),     album);
    copy_str(g_state.image_url, sizeof(g_state.image_url), image_url);
    g_state.duration_ms    = duration_ms;
    g_state.position_ms    = 0;
    g_state.position_ts_us = esp_timer_get_time();
    g_state.track_valid    = (name != nullptr && name[0] != '\0');
    xSemaphoreGive(g_mtx);
}

extern "C" void cspot_ui_state_set_position(uint32_t position_ms) {
    if (!g_mtx) return;
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_state.position_ms    = position_ms;
    g_state.position_ts_us = esp_timer_get_time();
    xSemaphoreGive(g_mtx);
}

extern "C" void cspot_ui_state_set_paused(bool paused) {
    if (!g_mtx) return;
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_state.is_paused = paused;
    xSemaphoreGive(g_mtx);
}

extern "C" void cspot_ui_state_set_volume(uint8_t pct) {
    if (!g_mtx) return;
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_state.volume_pct = pct;
    xSemaphoreGive(g_mtx);
}

extern "C" void cspot_ui_state_refresh_wifi(void) {
    if (!g_mtx) return;
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        xSemaphoreTake(g_mtx, portMAX_DELAY);
        copy_str(g_state.wifi_ssid, sizeof(g_state.wifi_ssid),
                 reinterpret_cast<const char*>(ap.ssid));
        g_state.wifi_rssi = ap.rssi;
        xSemaphoreGive(g_mtx);
    }
}

// ---- internal hook: CSpotPlayer registers the active handler -------------

namespace cspot_ui_internal {
void register_handler(cspot::SpircHandler* h) {
    g_handler.store(h, std::memory_order_release);
}
} // namespace cspot_ui_internal

// ---- Remote control proxies ----------------------------------------------

extern "C" void cspot_ctrl_set_pause(bool pause) {
    auto* h = g_handler.load(std::memory_order_acquire);
    if (h) h->setPause(pause);
    cspot_ui_state_set_paused(pause);
}

extern "C" void cspot_ctrl_next(void) {
    auto* h = g_handler.load(std::memory_order_acquire);
    if (h) h->nextSong();
}

extern "C" void cspot_ctrl_prev(void) {
    auto* h = g_handler.load(std::memory_order_acquire);
    if (h) h->previousSong();
}

extern "C" void cspot_ctrl_set_volume_pct(uint8_t pct) {
    auto* h = g_handler.load(std::memory_order_acquire);
    if (pct > 100) pct = 100;
    int v = static_cast<int>(pct) * 65535 / 100;
    if (h) h->setRemoteVolume(v);
    cspot_ui_state_set_volume(pct);
}
