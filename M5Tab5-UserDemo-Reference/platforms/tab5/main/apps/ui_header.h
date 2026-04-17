/*
 * Standard app header: back button on the left, title to the right of it.
 * Back button fires `on_back(user)` on tap (runs on LVGL task).
 */
#pragma once
#include "lvgl.h"

using ui_header_back_cb_t = void (*)(void* user);

lv_obj_t* ui_header_create(lv_obj_t* parent,
                           const char* title,
                           ui_header_back_cb_t on_back,
                           void* user);
