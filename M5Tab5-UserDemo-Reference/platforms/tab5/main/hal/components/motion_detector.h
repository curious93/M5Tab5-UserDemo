/*
 * SPDX-FileCopyrightText: 2026
 * SPDX-License-Identifier: MIT
 *
 * Lightweight frame-difference motion detector for the Tab5 camera pipeline.
 *
 * Designed to run inside the camera capture task on the ESP32-P4. Operates on
 * a downsampled 160x90 luminance grid extracted from a 1280x720 RGB565 frame
 * (8x decimation in both axes). Tracks a smoothed centroid of pixels whose
 * brightness changed beyond a threshold since the previous frame.
 *
 * Read paths are unsynchronized — consumers see eventually-consistent values
 * which is fine for UI animations but not for safety-critical use.
 */
#pragma once
#include <cstdint>

namespace motion_detector {

constexpr int kGridW = 160;
constexpr int kGridH = 90;
constexpr int kSubsample = 8;        // 1280/160, 720/90

void init();
void reset();

// Process one full RGB565 frame. `stride_px` is the number of pixels per row
// of the source buffer (typically 1280). When `flip_x` is true, the reported
// target_x is flipped horizontally — use this when feeding the raw camera
// buffer (which is not mirrored) but downstream consumers expect coordinates
// from a horizontally-mirrored display.
void process_frame(const uint16_t* rgb565, int stride_px, bool flip_x = false);

bool  is_detected();
float target_x();   // -1.0 .. 1.0
float target_y();   // -1.0 .. 1.0

}  // namespace motion_detector

// Legacy C-linkage globals — kept so existing app_main.cpp `extern` declarations
// continue to resolve without a touch. Eventually replace with the namespaced
// API above.
extern "C" {
extern float vision_target_x;
extern float vision_target_y;
extern bool  vision_detected;
}
