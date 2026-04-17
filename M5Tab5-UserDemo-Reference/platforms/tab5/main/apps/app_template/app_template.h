/*
 * AppTemplate — minimal app body used to validate launcher ↔ app round trip.
 * Header with back button, centered "Hello" label.
 */
#pragma once
#include "mooncake.h"
#include "lvgl.h"

class AppTemplate : public mooncake::AppAbility {
public:
    AppTemplate();
    ~AppTemplate() override = default;

    void onCreate() override;
    void onOpen() override;
    void onClose() override;

    // Called from LVGL task by the back-button handler.
    void requestClose();

private:
    lv_obj_t* _root = nullptr;

    void buildUI();
    void destroyUI();
};
