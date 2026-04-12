/*
 * SPDX-FileCopyrightText: 2025
 *
 * SPDX-License-Identifier: MIT
 *
 * IMU Sensor Visualizer for M5Stack Tab5
 * ----------------------------------------
 * Three live views driven by the onboard BMI270:
 *
 *  1. Bubble Level   – accel X/Y moves a glowing bubble inside a ring.
 *                      A centre cross + tilt-angle label complete the look.
 *
 *  2. Gyro Trail     – the last TRAIL_LEN gyro (X,Y) magnitude points are
 *                      drawn as a fading polyline on an LVGL canvas.
 *
 *  3. Accel Bars     – three animated bars (X / Y / Z) that change colour
 *                      green → yellow → red as the value grows.
 *
 * All animations use smooth_ui_toolkit::AnimateValue (spring physics).
 */
#include "app_imu_visualizer.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <fmt/core.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

static const char* TAG = "imu-viz";

// ─────────────────────────────────────────────────────────────────────────────
//  Colour palette
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint32_t COL_BG        = 0x0D0D14;   // near-black blue
static constexpr uint32_t COL_RING      = 0x2A2A40;   // dim ring
static constexpr uint32_t COL_CROSS     = 0x1E1E30;   // subtle crosshair
static constexpr uint32_t COL_CENTER    = 0x3A3A55;
static constexpr uint32_t COL_BUBBLE    = 0x00E5FF;   // cyan glow
static constexpr uint32_t COL_TRAIL     = 0xFF4081;   // pink trail
static constexpr uint32_t COL_BAR_LOW   = 0x00E676;   // green
static constexpr uint32_t COL_BAR_MID   = 0xFFD740;   // yellow
static constexpr uint32_t COL_BAR_HIGH  = 0xFF1744;   // red
static constexpr uint32_t COL_BAR_BG    = 0x1A1A2E;
static constexpr uint32_t COL_TEXT      = 0xB0B0CC;
static constexpr uint32_t COL_TITLE     = 0x00E5FF;

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
AppImuVisualizer::AppImuVisualizer()
{
    setAppInfo().name = "AppImuVisualizer";
}

void AppImuVisualizer::onCreate()
{
    mclog::tagInfo(TAG, "onCreate");
    open();
}

void AppImuVisualizer::onOpen()
{
    mclog::tagInfo(TAG, "onOpen");

    // Pre-allocate trail buffer
    _trail.assign(TRAIL_LEN, {W / 2, TRAIL_Y + TRAIL_H / 2});
    _trail_head = 0;

    LvglLockGuard lock;
    _build_ui();
}

void AppImuVisualizer::onRunning()
{
    uint32_t now = GetHAL()->millis();
    if (now - _last_update < 30) return;   // ~33 fps cap
    _last_update = now;

    GetHAL()->updateImuData();
    float ax = GetHAL()->imuData.accelX;
    float ay = GetHAL()->imuData.accelY;
    float az = GetHAL()->imuData.accelZ;
    float gx = GetHAL()->imuData.gyroX;
    float gy = GetHAL()->imuData.gyroY;

    LvglLockGuard lock;
    _update_bubble(ax, ay);
    _update_bars(ax, ay, az);
    _update_trail(gx, gy);
}

void AppImuVisualizer::onClose()
{
    mclog::tagInfo(TAG, "onClose");

    LvglLockGuard lock;

    // Release canvas draw buffer before LVGL deletes the object
    if (_trail_canvas) {
        lv_obj_del(_trail_canvas);
        _trail_canvas = nullptr;
        lv_draw_buf_free(&_trail_draw_buf);
    }

    if (_scr) {
        lv_obj_clean(_scr);
        _scr = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  UI construction
// ─────────────────────────────────────────────────────────────────────────────
void AppImuVisualizer::_build_ui()
{
    _scr = lv_screen_active();
    lv_obj_set_style_bg_color(_scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Title ────────────────────────────────────────────────────────────
    _title_label = lv_label_create(_scr);
    lv_obj_set_style_text_color(_title_label, lv_color_hex(COL_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_font(_title_label, &lv_font_montserrat_26, LV_PART_MAIN);
    lv_label_set_text(_title_label, "IMU VISUALIZER");
    lv_obj_align(_title_label, LV_ALIGN_TOP_MID, 0, 28);

    // ── Outer ring ───────────────────────────────────────────────────────
    _ring = lv_obj_create(_scr);
    lv_obj_set_size(_ring, LEVEL_R * 2, LEVEL_R * 2);
    lv_obj_align(_ring, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_opa(_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(_ring, lv_color_hex(COL_RING), LV_PART_MAIN);
    lv_obj_set_style_border_width(_ring, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_remove_flag(_ring, LV_OBJ_FLAG_SCROLLABLE);

    // Inner safe-zone ring (half radius)
    lv_obj_t* inner_ring = lv_obj_create(_scr);
    lv_obj_set_size(inner_ring, LEVEL_R, LEVEL_R);
    lv_obj_align(inner_ring, LV_ALIGN_TOP_MID, 0, 80 + LEVEL_R / 2);
    lv_obj_set_style_bg_opa(inner_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(inner_ring, lv_color_hex(COL_RING), LV_PART_MAIN);
    lv_obj_set_style_border_width(inner_ring, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(inner_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_opa(inner_ring, 100, LV_PART_MAIN);
    lv_obj_remove_flag(inner_ring, LV_OBJ_FLAG_SCROLLABLE);

    // Crosshair horizontal
    _crosshair_h = lv_obj_create(_scr);
    lv_obj_set_size(_crosshair_h, LEVEL_R * 2 - 20, 1);
    lv_obj_align(_crosshair_h, LV_ALIGN_TOP_MID, 0, 80 + LEVEL_R - 1);
    lv_obj_set_style_bg_color(_crosshair_h, lv_color_hex(COL_CROSS), LV_PART_MAIN);
    lv_obj_set_style_border_width(_crosshair_h, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_crosshair_h, 0, LV_PART_MAIN);

    // Crosshair vertical
    _crosshair_v = lv_obj_create(_scr);
    lv_obj_set_size(_crosshair_v, 1, LEVEL_R * 2 - 20);
    lv_obj_align(_crosshair_v, LV_ALIGN_TOP_MID, 0, 80 + 10);
    lv_obj_set_style_bg_color(_crosshair_v, lv_color_hex(COL_CROSS), LV_PART_MAIN);
    lv_obj_set_style_border_width(_crosshair_v, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_crosshair_v, 0, LV_PART_MAIN);

    // Centre dot
    _center_dot = lv_obj_create(_scr);
    lv_obj_set_size(_center_dot, 10, 10);
    lv_obj_align(_center_dot, LV_ALIGN_TOP_MID, 0, 80 + LEVEL_R - 5);
    lv_obj_set_style_bg_color(_center_dot, lv_color_hex(COL_CENTER), LV_PART_MAIN);
    lv_obj_set_style_border_width(_center_dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_center_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);

    // Bubble (starts at centre)
    _bubble = lv_obj_create(_scr);
    lv_obj_set_size(_bubble, BUBBLE_R * 2, BUBBLE_R * 2);
    lv_obj_align(_bubble, LV_ALIGN_TOP_MID, 0, 80 + LEVEL_R - BUBBLE_R);
    lv_obj_set_style_bg_color(_bubble, lv_color_hex(COL_BUBBLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_bubble, 200, LV_PART_MAIN);
    lv_obj_set_style_border_color(_bubble, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(_bubble, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(_bubble, 80, LV_PART_MAIN);
    lv_obj_set_style_radius(_bubble, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(_bubble, lv_color_hex(COL_BUBBLE), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(_bubble, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(_bubble, 160, LV_PART_MAIN);
    lv_obj_remove_flag(_bubble, LV_OBJ_FLAG_SCROLLABLE);

    // Tilt angle label
    _angle_label = lv_label_create(_scr);
    lv_obj_set_style_text_color(_angle_label, lv_color_hex(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(_angle_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_label_set_text(_angle_label, "0.0°");
    lv_obj_align(_angle_label, LV_ALIGN_TOP_MID, 0, 80 + LEVEL_R * 2 + 14);

    // Bubble anim init
    _bub_x.springOptions().visualDuration = 0.25f;
    _bub_x.springOptions().bounce         = 0.3f;
    _bub_x.teleport(0);
    _bub_x.play();

    _bub_y.springOptions().visualDuration = 0.25f;
    _bub_y.springOptions().bounce         = 0.3f;
    _bub_y.teleport(0);
    _bub_y.play();

    // ── Gyro Trail canvas ─────────────────────────────────────────────────
    // Allocate draw buffer: RGB565 strip
    uint32_t buf_size = LV_CANVAS_BUF_SIZE(TRAIL_W, TRAIL_H, 16, LV_DRAW_BUF_STRIDE_ALIGN);
    void* buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT);
    lv_draw_buf_init(&_trail_draw_buf, TRAIL_W, TRAIL_H, LV_COLOR_FORMAT_RGB565,
                     LV_STRIDE_AUTO, buf, buf_size);

    _trail_canvas = lv_canvas_create(_scr);
    lv_canvas_set_draw_buf(_trail_canvas, &_trail_draw_buf);
    lv_obj_set_pos(_trail_canvas, TRAIL_X, TRAIL_Y);
    lv_canvas_fill_bg(_trail_canvas, lv_color_hex(COL_BG), LV_OPA_COVER);

    // Section label
    lv_obj_t* trail_lbl = lv_label_create(_scr);
    lv_obj_set_style_text_color(trail_lbl, lv_color_hex(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(trail_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(trail_lbl, "GYRO TRAIL");
    lv_obj_set_pos(trail_lbl, 20, TRAIL_Y - 26);

    // ── Accel Bars ────────────────────────────────────────────────────────
    static const char* bar_names[] = {"X", "Y", "Z"};
    int total_bars_w = 3 * BAR_W + 2 * BAR_GAP;
    int bar_start_x  = (W - total_bars_w) / 2;

    for (int i = 0; i < 3; i++) {
        int bx = bar_start_x + i * (BAR_W + BAR_GAP);

        // Bar background
        lv_obj_t* bg = lv_obj_create(_scr);
        lv_obj_set_pos(bg, bx, BAR_Y);
        lv_obj_set_size(bg, BAR_W, BAR_H_MAX);
        lv_obj_set_style_bg_color(bg, lv_color_hex(COL_BAR_BG), LV_PART_MAIN);
        lv_obj_set_style_border_color(bg, lv_color_hex(COL_RING), LV_PART_MAIN);
        lv_obj_set_style_border_width(bg, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(bg, 8, LV_PART_MAIN);
        lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

        // Actual bar (fills from bottom, starts at 0 height)
        _bar[i] = lv_obj_create(_scr);
        lv_obj_set_pos(_bar[i], bx, BAR_BASE_Y);
        lv_obj_set_size(_bar[i], BAR_W, 0);
        lv_obj_set_style_bg_color(_bar[i], lv_color_hex(COL_BAR_LOW), LV_PART_MAIN);
        lv_obj_set_style_border_width(_bar[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(_bar[i], 8, LV_PART_MAIN);
        lv_obj_remove_flag(_bar[i], LV_OBJ_FLAG_SCROLLABLE);

        // Axis label below
        _bar_label[i] = lv_label_create(_scr);
        lv_obj_set_style_text_color(_bar_label[i], lv_color_hex(COL_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(_bar_label[i], &lv_font_montserrat_20, LV_PART_MAIN);
        lv_label_set_text(_bar_label[i], bar_names[i]);
        lv_obj_set_pos(_bar_label[i], bx + BAR_W / 2 - 7, BAR_BASE_Y + 10);

        // Value label
        _bar_val[i] = lv_label_create(_scr);
        lv_obj_set_style_text_color(_bar_val[i], lv_color_hex(COL_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(_bar_val[i], &lv_font_montserrat_16, LV_PART_MAIN);
        lv_label_set_text(_bar_val[i], "0.0");
        lv_obj_set_pos(_bar_val[i], bx + 4, BAR_BASE_Y + 38);

        // Anim init
        _bar_anim[i].springOptions().visualDuration = 0.2f;
        _bar_anim[i].springOptions().bounce         = 0.1f;
        _bar_anim[i].teleport(0);
        _bar_anim[i].play();
    }

    // Bottom label
    lv_obj_t* bar_hdr = lv_label_create(_scr);
    lv_obj_set_style_text_color(bar_hdr, lv_color_hex(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(bar_hdr, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(bar_hdr, "ACCEL  m/s²");
    lv_obj_align(bar_hdr, LV_ALIGN_TOP_MID, 0, BAR_Y - 26);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Update helpers
// ─────────────────────────────────────────────────────────────────────────────
void AppImuVisualizer::_update_bubble(float ax, float ay)
{
    // Clamp inside ring, leaving room for bubble radius
    float max_offset = LEVEL_R - BUBBLE_R - 4;
    float ox = std::clamp(ax * 80.0f, -max_offset, max_offset);
    float oy = std::clamp(ay * 80.0f, -max_offset, max_offset);

    _bub_x = (int)ox;
    _bub_y = (int)oy;

    // Pixel position: ring centre + offset - bubble radius
    int px = LEVEL_CX + (int)_bub_x.directValue() - BUBBLE_R;
    int py = 80 + LEVEL_R + (int)_bub_y.directValue() - BUBBLE_R;
    lv_obj_set_pos(_bubble, px, py);

    // Tilt angle
    float tilt = std::sqrt(ax * ax + ay * ay);
    float deg  = std::atan2(tilt, std::abs(az < 0 ? -1.0f : 1.0f)) * 180.0f / M_PI;
    // Clamp display
    deg = std::min(deg, 90.0f);
    lv_label_set_text(_angle_label, fmt::format("{:.1f}°", deg).c_str());

    // Bubble colour: green when level, cyan off-level, red tilted a lot
    uint32_t col;
    if (tilt < 0.1f)       col = 0x00FF88;
    else if (tilt < 0.5f)  col = COL_BUBBLE;
    else                   col = COL_BAR_HIGH;
    lv_obj_set_style_bg_color(_bubble, lv_color_hex(col), LV_PART_MAIN);
    lv_obj_set_style_shadow_color(_bubble, lv_color_hex(col), LV_PART_MAIN);
}

void AppImuVisualizer::_update_bars(float ax, float ay, float az)
{
    float vals[3] = {ax, ay, az};
    for (int i = 0; i < 3; i++) {
        float abs_val  = std::abs(vals[i]);
        float clamped  = std::min(abs_val, 2.0f);   // 2 m/s² = full bar
        int   target_h = (int)(clamped / 2.0f * BAR_H_MAX);

        _bar_anim[i] = target_h;
        int h = (int)_bar_anim[i].directValue();

        int total_bars_w = 3 * BAR_W + 2 * BAR_GAP;
        int bar_start_x  = (W - total_bars_w) / 2;
        int bx = bar_start_x + i * (BAR_W + BAR_GAP);

        lv_obj_set_pos(_bar[i], bx, BAR_BASE_Y - h);
        lv_obj_set_size(_bar[i], BAR_W, h);
        lv_obj_set_style_bg_color(_bar[i], _bar_color(clamped / 2.0f), LV_PART_MAIN);

        lv_label_set_text(_bar_val[i], fmt::format("{:.2f}", vals[i]).c_str());
    }
}

void AppImuVisualizer::_update_trail(float gx, float gy)
{
    // Map gyro rates to canvas pixels
    float cx   = TRAIL_W / 2.0f;
    float cy   = TRAIL_H / 2.0f;
    float scale = 1.8f;

    int16_t nx = (int16_t)std::clamp(cx + gx * scale, 2.0f, (float)TRAIL_W - 3);
    int16_t ny = (int16_t)std::clamp(cy + gy * scale, 2.0f, (float)TRAIL_H - 3);

    _trail[_trail_head] = {nx, ny};
    _trail_head         = (_trail_head + 1) % TRAIL_LEN;

    _redraw_trail();
}

void AppImuVisualizer::_redraw_trail()
{
    lv_canvas_fill_bg(_trail_canvas, lv_color_hex(COL_BG), LV_OPA_COVER);

    // Draw fading segments
    for (int i = 0; i < TRAIL_LEN - 1; i++) {
        int idx_a = (_trail_head + i) % TRAIL_LEN;
        int idx_b = (_trail_head + i + 1) % TRAIL_LEN;

        // Opacity grows from 20 → 255 towards the newest point
        uint8_t opa = (uint8_t)(20 + (i * 235) / (TRAIL_LEN - 1));

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color     = lv_color_hex(COL_TRAIL);
        dsc.opa       = opa;
        dsc.width     = 2 + (i > TRAIL_LEN - 8 ? 2 : 0);  // thicker at tip
        dsc.round_end = true;

        lv_point_t pts[2] = {
            {_trail[idx_a].x, _trail[idx_a].y},
            {_trail[idx_b].x, _trail[idx_b].y}
        };
        lv_canvas_draw_line(_trail_canvas, pts, 2, &dsc);
    }

    // Tip dot
    int tip = (_trail_head + TRAIL_LEN - 1) % TRAIL_LEN;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color     = lv_color_hex(COL_TRAIL);
    arc.opa       = LV_OPA_COVER;
    arc.width     = 5;
    lv_canvas_draw_arc(_trail_canvas, _trail[tip].x, _trail[tip].y, 5, 0, 360, &arc);
}

lv_color_t AppImuVisualizer::_bar_color(float t)
{
    // t: 0.0 → 1.0
    if (t < 0.5f) {
        // green → yellow
        uint8_t r = (uint8_t)(t * 2.0f * 0xFF);
        return lv_color_make(r, 0xFF, 0x00);
    } else {
        // yellow → red
        uint8_t g = (uint8_t)((1.0f - (t - 0.5f) * 2.0f) * 0xFF);
        return lv_color_make(0xFF, g, 0x00);
    }
}
