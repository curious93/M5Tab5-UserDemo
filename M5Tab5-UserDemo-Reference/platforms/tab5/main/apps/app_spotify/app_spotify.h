#pragma once
#include "mooncake.h"
#include "lvgl.h"

class AppSpotify : public mooncake::AppAbility {
public:
    AppSpotify();
    ~AppSpotify() override = default;

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

    void requestClose();

private:
    lv_obj_t* _root       = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    lv_obj_t* _spinner     = nullptr;
    uint32_t  _tickLast    = 0;

    void buildUI();
    void destroyUI();
    void setStatus(const char* text);
};
