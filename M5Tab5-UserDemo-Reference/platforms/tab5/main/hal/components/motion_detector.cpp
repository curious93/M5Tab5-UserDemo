/*
 * SPDX-FileCopyrightText: 2026
 * SPDX-License-Identifier: MIT
 */
#include "motion_detector.h"
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>

// Legacy globals — populated by namespaced state below.
float vision_target_x = 0.0f;
float vision_target_y = 0.0f;
bool  vision_detected = false;

namespace motion_detector {

namespace {

// Brightness delta (0..255) above which a pixel counts as "moved".
constexpr int kPixelDiffThreshold = 25;

// Minimum number of changed pixels in a frame for the centroid to be trusted.
// 50 / (160*90) ≈ 0.35% of the grid.
constexpr int kMinMovedPixels = 50;

// IIR low-pass smoothing factor for the target coordinate.
// new = old * (1 - kSmoothing) + raw * kSmoothing
constexpr float kSmoothing = 0.30f;

uint8_t* g_prev_gray = nullptr;

}  // namespace

void init()
{
    if (g_prev_gray) return;
    g_prev_gray = static_cast<uint8_t*>(
        heap_caps_malloc(kGridW * kGridH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_prev_gray) {
        std::memset(g_prev_gray, 0, kGridW * kGridH);
    }
}

void reset()
{
    if (g_prev_gray) {
        std::memset(g_prev_gray, 0, kGridW * kGridH);
    }
    vision_target_x = 0.0f;
    vision_target_y = 0.0f;
    vision_detected = false;
}

void process_frame(const uint16_t* rgb565, int stride_px, bool flip_x)
{
    if (!g_prev_gray || !rgb565) return;

    long sum_x = 0;
    long sum_y = 0;
    long count = 0;

    for (int y = 0; y < kGridH; ++y) {
        const uint16_t* src_row = rgb565 + (y * kSubsample) * stride_px;
        uint8_t* prev_row = g_prev_gray + y * kGridW;

        for (int x = 0; x < kGridW; ++x) {
            uint16_t pixel = src_row[x * kSubsample];

            // RGB565 → 8-bit luminance (Rec.601 weights, fixed-point).
            // Y = (77*R + 150*G + 29*B) >> 8, where R,G,B are 8-bit.
            int r = ((pixel >> 11) & 0x1F) << 3;
            int g = ((pixel >>  5) & 0x3F) << 2;
            int b = ( pixel        & 0x1F) << 3;
            uint8_t gray = static_cast<uint8_t>((77 * r + 150 * g + 29 * b) >> 8);

            int diff = static_cast<int>(gray) - static_cast<int>(prev_row[x]);
            if (diff < 0) diff = -diff;
            if (diff > kPixelDiffThreshold) {
                sum_x += x;
                sum_y += y;
                ++count;
            }
            prev_row[x] = gray;
        }
    }

    if (count > kMinMovedPixels) {
        float raw_x = (static_cast<float>(sum_x) / count / kGridW) * 2.0f - 1.0f;
        float raw_y = (static_cast<float>(sum_y) / count / kGridH) * 2.0f - 1.0f;
        if (flip_x) raw_x = -raw_x;
        vision_target_x = vision_target_x * (1.0f - kSmoothing) + raw_x * kSmoothing;
        vision_target_y = vision_target_y * (1.0f - kSmoothing) + raw_y * kSmoothing;
        vision_detected = true;
    } else {
        vision_detected = false;
    }
}

bool  is_detected() { return vision_detected; }
float target_x()    { return vision_target_x; }
float target_y()    { return vision_target_y; }

}  // namespace motion_detector
