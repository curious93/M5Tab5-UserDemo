#pragma once
//
// cspot_ui_state.h — thread-safe snapshot of the current Spotify playback
// state, written by CSpotPlayer's event handler and read by the UI layer
// (app_spotify) without touching SpircHandler internals.
//
// All accessors are C-linkage so the LVGL-side code can include this header
// without dragging C++ in.
//

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     track[128];
    char     artist[128];
    char     album[128];
    char     image_url[256];
    char     wifi_ssid[33];
    int8_t   wifi_rssi;
    uint32_t duration_ms;
    uint32_t position_ms;        // anchor — last known absolute position
    uint64_t position_ts_us;     // esp_timer_get_time() at position anchor
    uint8_t  volume_pct;         // 0..100
    bool     is_paused;
    bool     track_valid;
} cspot_ui_state_t;

// Initialise the mutex once at boot. Safe to call before SpircHandler exists.
void cspot_ui_state_init(void);

// Atomic snapshot of the current state. Returns a copy by value.
cspot_ui_state_t cspot_ui_state_get(void);

// Internal writers used by CSpotPlayer's event handler.
void cspot_ui_state_set_track(const char* name, const char* artist,
                              const char* album, const char* image_url,
                              uint32_t duration_ms);
void cspot_ui_state_set_position(uint32_t position_ms);
void cspot_ui_state_set_paused(bool paused);
void cspot_ui_state_set_volume(uint8_t pct);
void cspot_ui_state_refresh_wifi(void);

// Remote-control proxies — UI buttons call these. Implementations look up
// the active SpircHandler and forward the call. No-op if cspot is not running.
void cspot_ctrl_set_pause(bool pause);
void cspot_ctrl_next(void);
void cspot_ctrl_prev(void);
void cspot_ctrl_set_volume_pct(uint8_t pct);

#ifdef __cplusplus
}
#endif
