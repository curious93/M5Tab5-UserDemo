/*
 * System info — CPU temp, free heap/PSRAM, uptime.
 * Refreshed at 1 Hz via an lv_timer running in the LVGL task.
 */
#pragma once
#include "mooncake.h"
#include "lvgl.h"

class AppSystemInfo : public mooncake::AppAbility {
public:
    AppSystemInfo();
    ~AppSystemInfo() override = default;

    void onCreate() override;
    void onOpen() override;
    void onClose() override;

    void requestClose();

private:
    lv_obj_t*    _root      = nullptr;
    lv_obj_t*    _lbl_temp  = nullptr;
    lv_obj_t*    _lbl_heap  = nullptr;
    lv_obj_t*    _lbl_psram = nullptr;
    lv_obj_t*    _lbl_up    = nullptr;
    lv_timer_t*  _timer     = nullptr;

    void buildUI();
    void destroyUI();
    void refresh();
    friend void system_info_timer_cb(lv_timer_t* t);
};
