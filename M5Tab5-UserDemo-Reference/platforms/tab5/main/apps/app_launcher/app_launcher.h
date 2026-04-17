/*
 * AppLauncher — the home grid app.
 *
 * Lifecycle:
 *   onCreate  → calls open()
 *   onOpen    → build the LVGL grid (header + 3×4 icons)
 *   onRunning → poll pending_launch_idx set by icon taps, launch target, close()
 *   onClose   → destroy LVGL UI
 *   onSleeping→ when target app has finished, uninstall it + open() self again
 */
#pragma once
#include "mooncake.h"
#include "lvgl.h"

class HalEsp32;

class AppLauncher : public mooncake::AppAbility {
public:
    explicit AppLauncher(HalEsp32* hal);
    ~AppLauncher() override = default;

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
    void onSleeping() override;

    // Called by icon tap callback (runs inside LVGL task).
    void requestLaunch(int registry_index);

private:
    HalEsp32* _hal            = nullptr;
    lv_obj_t* _root           = nullptr;
    int       _pending_launch = -1;
    int       _active_target  = -1;

    void buildUI();
    void destroyUI();
    void addIcon(lv_obj_t* parent, int registry_index, int slot_index);
    void addEmptySlot(lv_obj_t* parent, int slot_index);
};
