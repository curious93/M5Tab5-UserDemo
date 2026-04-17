#include "ui_header.h"

namespace {
constexpr int HEADER_H       = 96;
constexpr int BACK_SIZE      = 72;
constexpr int BACK_MARGIN    = 16;
constexpr uint32_t COL_BG    = 0x1B2236;
constexpr uint32_t COL_BACK  = 0x2D3650;
constexpr uint32_t COL_TEXT  = 0xE6EEFF;

struct HeaderCb {
    ui_header_back_cb_t fn;
    void*               user;
};

void back_clicked_cb(lv_event_t* e)
{
    auto* cb = static_cast<HeaderCb*>(lv_event_get_user_data(e));
    if (cb && cb->fn) cb->fn(cb->user);
}

void header_deleted_cb(lv_event_t* e)
{
    auto* cb = static_cast<HeaderCb*>(lv_event_get_user_data(e));
    delete cb;
}
}

lv_obj_t* ui_header_create(lv_obj_t* parent,
                           const char* title,
                           ui_header_back_cb_t on_back,
                           void* user)
{
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back = lv_obj_create(header);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, BACK_SIZE, BACK_SIZE);
    lv_obj_set_pos(back, BACK_MARGIN, (HEADER_H - BACK_SIZE) / 2);
    lv_obj_set_style_bg_color(back, lv_color_hex(COL_BACK), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back, 16, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* arrow = lv_label_create(back);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(arrow, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_32, 0);
    lv_obj_center(arrow);

    auto* cb = new HeaderCb{on_back, user};
    lv_obj_add_event_cb(back, back_clicked_cb, LV_EVENT_CLICKED, cb);
    // Free the heap-allocated callback data when header is destroyed.
    lv_obj_add_event_cb(header, header_deleted_cb, LV_EVENT_DELETE, cb);

    lv_obj_t* title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_32, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, BACK_MARGIN + BACK_SIZE + 20, 0);

    return header;
}
