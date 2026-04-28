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
    lv_obj_t* _root         = nullptr;
    lv_obj_t* _statusLabel  = nullptr;
    lv_obj_t* _trackLabel   = nullptr;
    lv_obj_t* _artistLabel  = nullptr;
    lv_obj_t* _wifiLabel    = nullptr;
    lv_obj_t* _timeLabel    = nullptr;
    lv_obj_t* _progressBar  = nullptr;
    lv_obj_t* _btnPrev      = nullptr;
    lv_obj_t* _btnPlayPause = nullptr;
    lv_obj_t* _btnNext      = nullptr;
    lv_obj_t* _btnPlayPauseLabel = nullptr;
    lv_obj_t* _volSlider    = nullptr;
    lv_obj_t* _spinner      = nullptr;
    uint32_t  _tickLast     = 0;
    bool      _uiBuilt      = false;
    bool      _userDraggingSlider = false;

    void buildUI();
    void buildPlayerUI();
    void destroyUI();
    void setStatus(const char* text);
    void refreshFromState();
};
