/*
 * SPDX-FileCopyrightText: 2025
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <memory>
#include <vector>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <lvgl.h>

class AppImuVisualizer : public mooncake::AppAbility {
public:
    AppImuVisualizer();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    // ── Layout constants ────────────────────────────────────────────────
    static constexpr int W  = 720;
    static constexpr int H  = 1280;

    // Bubble level area  (top half)
    static constexpr int LEVEL_CX   = W / 2;
    static constexpr int LEVEL_CY   = 340;
    static constexpr int LEVEL_R    = 220;   // outer ring radius
    static constexpr int BUBBLE_R   = 38;    // bubble radius

    // Motion trail canvas
    static constexpr int TRAIL_X    = 0;
    static constexpr int TRAIL_Y    = 680;
    static constexpr int TRAIL_W    = W;
    static constexpr int TRAIL_H    = 200;
    static constexpr int TRAIL_LEN  = 80;    // number of trail points

    // Bar chart area (bottom)
    static constexpr int BAR_Y      = 920;
    static constexpr int BAR_H_MAX  = 240;
    static constexpr int BAR_W      = 120;
    static constexpr int BAR_GAP    = 60;
    static constexpr int BAR_BASE_Y = BAR_Y + BAR_H_MAX;

    // ── LVGL objects ────────────────────────────────────────────────────
    // Background
    lv_obj_t* _scr          = nullptr;

    // Bubble level
    lv_obj_t* _ring         = nullptr;   // outer ring
    lv_obj_t* _crosshair_h  = nullptr;
    lv_obj_t* _crosshair_v  = nullptr;
    lv_obj_t* _center_dot   = nullptr;  // small center marker
    lv_obj_t* _bubble       = nullptr;

    // Gyro trail (drawn on canvas via lv_line)
    lv_obj_t* _trail_canvas = nullptr;
    lv_draw_buf_t _trail_draw_buf{};

    // Accel bars (X, Y, Z)
    lv_obj_t* _bar[3]         = {};
    lv_obj_t* _bar_label[3]   = {};
    lv_obj_t* _bar_val[3]     = {};

    // Title & tilt angle
    lv_obj_t* _title_label  = nullptr;
    lv_obj_t* _angle_label  = nullptr;

    // ── Animation values ────────────────────────────────────────────────
    smooth_ui_toolkit::AnimateValue _bub_x;
    smooth_ui_toolkit::AnimateValue _bub_y;
    smooth_ui_toolkit::AnimateValue _bar_anim[3];

    // ── Trail ring buffer ────────────────────────────────────────────────
    struct Point { int16_t x, y; };
    std::vector<Point> _trail;
    int _trail_head = 0;

    // ── State ────────────────────────────────────────────────────────────
    uint32_t _last_update = 0;

    // ── Helpers ──────────────────────────────────────────────────────────
    void _build_ui();
    void _update_bubble(float ax, float ay);
    void _update_bars(float ax, float ay, float az);
    void _update_trail(float gx, float gy);
    void _redraw_trail();
    lv_color_t _bar_color(float val);
};
