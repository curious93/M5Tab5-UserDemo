/*
 * Motion Detector — fullscreen camera preview with a bounding-box overlay.
 *
 * Architecture (v4 — fluid display):
 *   Camera task (hal_camera.cpp, Core 1): captures frames, runs PPA mirror,
 *   publishes ready frames via HalEsp32::consumeReadyFrame(). Never touches
 *   LVGL directly — zero lock-contention with the LVGL task.
 *
 *   lv_timer callback (canvas_refresh_cb, fires every 33 ms inside LVGL task):
 *   Pulls the latest frame, draws the bbox directly into the RGB565 pixel
 *   buffer, then calls lv_canvas_set_buffer(). Running inside the LVGL task
 *   means the lock is already owned — no cross-task wait.
 *
 *   app_main loop: trivial 1-second sleep; all real work is in the two tasks.
 */
#include <stdio.h>
#include <algorithm>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/hal_esp32.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "hal/components/motion_detector.h"

static const char* TAG = "app";
static HalEsp32 device_hal;

// Display geometry.
static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

// Bounding box appearance.
static constexpr int BOX_BORDER_MIN = 3;
static constexpr int BOX_BORDER_MAX = 8;
static constexpr int BOX_MIN_W      = 60;
static constexpr int BOX_MIN_H      = 60;

// Intensity scaling: moved_pixels count that maps to maximum border width.
static constexpr int INTENSITY_FULL_PX = 1500;

// RGB565 colours for the bbox drawn directly into the pixel buffer.
// 0x00E5FF (cyan) → R5=0, G6=57, B5=31 → (0<<11)|(57<<5)|31 = 0x073F
// 0x444455 (dim)  → R5=8, G6=17, B5=10 → (8<<11)|(17<<5)|10 = 0x422A
static constexpr uint16_t COL_ACTIVE_565 = 0x073F;
static constexpr uint16_t COL_IDLE_565   = 0x422A;

// HUD colours (LVGL labels — small, cheap to render).
static constexpr uint32_t COL_HUD_BG = 0x000000;
static constexpr uint32_t COL_HUD_FG = 0xB0C8FF;

// UI handles.
static lv_obj_t* cam_canvas  = nullptr;
static lv_obj_t* hud_box     = nullptr;
static lv_obj_t* hud_title   = nullptr;
static lv_obj_t* hud_status  = nullptr;

// ---------------------------------------------------------------------------
// Raw RGB565 bounding-box drawing
// ---------------------------------------------------------------------------

// Draw a rectangle outline `thick` pixels wide into an RGB565 framebuffer.
// Coordinates are clamped to the frame dimensions before drawing.
static void draw_bbox_rgb565(uint16_t* fb, int x0, int y0, int x1, int y1,
                              uint16_t color, int thick)
{
    x0 = std::clamp(x0, 0, SCREEN_W - 1);
    x1 = std::clamp(x1, 0, SCREEN_W - 1);
    y0 = std::clamp(y0, 0, SCREEN_H - 1);
    y1 = std::clamp(y1, 0, SCREEN_H - 1);
    if (x0 >= x1 || y0 >= y1) return;

    for (int t = 0; t < thick; ++t) {
        // Top and bottom horizontal lines.
        for (int x = x0; x <= x1; ++x) {
            int yt = y0 + t;
            int yb = y1 - t;
            if (yt < SCREEN_H) fb[yt * SCREEN_W + x] = color;
            if (yb >= 0)       fb[yb * SCREEN_W + x] = color;
        }
        // Left and right vertical lines.
        for (int y = y0; y <= y1; ++y) {
            int xl = x0 + t;
            int xr = x1 - t;
            if (xl < SCREEN_W) fb[y * SCREEN_W + xl] = color;
            if (xr >= 0)       fb[y * SCREEN_W + xr] = color;
        }
    }
}

// ---------------------------------------------------------------------------
// lv_timer callback — runs inside the LVGL task (no lock acquisition needed)
// ---------------------------------------------------------------------------

static void canvas_refresh_cb(lv_timer_t*)
{
    uint8_t* frame = device_hal.consumeReadyFrame();
    if (!frame) return;  // camera hasn't produced a new frame yet

    bool det = motion_detector::is_detected();

    if (det) {
        // Map bbox edges from [-1, 1] to screen pixels.
        // motion_detector already applies flip_x so coordinates are in
        // display-space (matching the PPA-mirrored frame).
        float xmin_n = motion_detector::bbox_x_min();
        float xmax_n = motion_detector::bbox_x_max();
        float ymin_n = motion_detector::bbox_y_min();
        float ymax_n = motion_detector::bbox_y_max();

        int x0 = (int)(SCREEN_W / 2.0f + xmin_n * (SCREEN_W / 2.0f));
        int x1 = (int)(SCREEN_W / 2.0f + xmax_n * (SCREEN_W / 2.0f));
        int y0 = (int)(SCREEN_H / 2.0f + ymin_n * (SCREEN_H / 2.0f));
        int y1 = (int)(SCREEN_H / 2.0f + ymax_n * (SCREEN_H / 2.0f));

        // Enforce minimum box size.
        if (x1 - x0 < BOX_MIN_W) {
            int cx = (x0 + x1) / 2;
            x0 = cx - BOX_MIN_W / 2;
            x1 = cx + BOX_MIN_W / 2;
        }
        if (y1 - y0 < BOX_MIN_H) {
            int cy = (y0 + y1) / 2;
            y0 = cy - BOX_MIN_H / 2;
            y1 = cy + BOX_MIN_H / 2;
        }

        // Border width scales with motion intensity.
        int moved = motion_detector::moved_pixels();
        int span  = BOX_BORDER_MAX - BOX_BORDER_MIN;
        int add   = std::min((moved * span) / INTENSITY_FULL_PX, span);
        int thick = BOX_BORDER_MIN + add;

        draw_bbox_rgb565(reinterpret_cast<uint16_t*>(frame),
                         x0, y0, x1, y1, COL_ACTIVE_565, thick);
    }

    lv_canvas_set_buffer(cam_canvas, frame, SCREEN_W, SCREEN_H,
                         LV_COLOR_FORMAT_RGB565);

    char buf[64];
    snprintf(buf, sizeof(buf), "detected=%s  moved=%5d px",
             det ? "YES" : " no", motion_detector::moved_pixels());
    lv_label_set_text(hud_status, buf);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Motion Detector v4 starting...");

    device_hal.init();

    lv_display_t* lvDisp = device_hal.lvDisp;
    if (lvDisp == NULL) {
        ESP_LOGE(TAG, "Failed to get display handle!");
        return;
    }

    if (lvgl_port_lock(-1)) {
        lv_obj_t* scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        // Fullscreen camera canvas — pixel data is swapped in by canvas_refresh_cb.
        cam_canvas = lv_canvas_create(scr);
        lv_obj_set_size(cam_canvas, SCREEN_W, SCREEN_H);
        lv_obj_align(cam_canvas, LV_ALIGN_CENTER, 0, 0);

        // HUD (top-left).
        hud_box = lv_obj_create(scr);
        lv_obj_set_size(hud_box, 440, 88);
        lv_obj_set_pos(hud_box, 16, 16);
        lv_obj_set_style_bg_color(hud_box, lv_color_hex(COL_HUD_BG), 0);
        lv_obj_set_style_bg_opa(hud_box, 170, 0);
        lv_obj_set_style_border_width(hud_box, 1, 0);
        lv_obj_set_style_border_color(hud_box, lv_color_hex(COL_HUD_FG), 0);
        lv_obj_set_style_border_opa(hud_box, 80, 0);
        lv_obj_set_style_radius(hud_box, 6, 0);
        lv_obj_set_style_pad_all(hud_box, 10, 0);
        lv_obj_remove_flag(hud_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(hud_box, LV_OBJ_FLAG_CLICKABLE);

        hud_title = lv_label_create(hud_box);
        lv_label_set_text(hud_title, "MOTION DETECTOR");
        lv_obj_set_style_text_color(hud_title, lv_color_hex(COL_HUD_FG), 0);
        lv_obj_set_style_text_font(hud_title, &lv_font_montserrat_24, 0);
        lv_obj_align(hud_title, LV_ALIGN_TOP_LEFT, 0, 0);

        hud_status = lv_label_create(hud_box);
        lv_label_set_text(hud_status, "detected= no  moved=    0 px");
        lv_obj_set_style_text_color(hud_status, lv_color_hex(COL_HUD_FG), 0);
        lv_obj_set_style_text_font(hud_status, &lv_font_montserrat_16, 0);
        lv_obj_align(hud_status, LV_ALIGN_TOP_LEFT, 0, 36);

        // Register the display refresh timer (30 fps target).
        // Runs inside the LVGL task — no cross-task lock needed.
        lv_timer_create(canvas_refresh_cb, 33, nullptr);

        // Start camera capture (spawns camera task on Core 1).
        device_hal.startCameraCapture(cam_canvas);

        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Motion Detector UI ready.");

    // Camera task and LVGL task handle everything.
    // app_main just stays alive to keep the FreeRTOS scheduler happy.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
