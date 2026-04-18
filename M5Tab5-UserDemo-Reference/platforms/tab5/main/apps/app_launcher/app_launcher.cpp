#include "app_launcher.h"
#include "../app_registry.h"
#include "hal/hal_esp32.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace {

// Fixed landscape dimensions after LV_DISPLAY_ROTATION_90.
static int SCREEN_W = 1280;
static int SCREEN_H = 720;
constexpr int HEADER_H     = 96;
constexpr int GRID_COLS    = 3;
constexpr int GRID_ROWS    = 4;
constexpr int GRID_SLOTS   = GRID_COLS * GRID_ROWS;
constexpr int ICON_SIZE    = 160;
constexpr int ICON_PAD     = 24;
constexpr int LABEL_GAP    = 8;
constexpr int LABEL_H      = 28;

constexpr uint32_t COL_BG        = 0xFF0000;  // TEST: bright red
constexpr uint32_t COL_HEADER_BG = 0x0000FF;  // TEST: bright blue
constexpr uint32_t COL_TEXT      = 0xFFFF00;  // TEST: yellow
constexpr uint32_t COL_MUTED     = 0x00FF00;  // TEST: bright green
constexpr uint32_t COL_EMPTY     = 0xFF00FF;  // TEST: magenta

const char* TAG = "launcher";

struct IconUserData {
    AppLauncher* launcher;
    int          registry_index;
};

void icon_click_cb(lv_event_t* e)
{
    auto* data = static_cast<IconUserData*>(lv_event_get_user_data(e));
    if (!data || !data->launcher) return;
    data->launcher->requestLaunch(data->registry_index);
}

} // namespace

AppLauncher::AppLauncher(HalEsp32* hal) : _hal(hal)
{
    setAppInfo().name = "Launcher";
}

void AppLauncher::onCreate()
{
    ESP_LOGI(TAG, "onCreate");
    open();
}

void AppLauncher::onOpen()
{
    ESP_LOGI(TAG, "onOpen");
    buildUI();
}

void AppLauncher::onRunning()
{
    if (_pending_launch < 0) return;

    const auto& registry = GetAppRegistry();
    int idx = _pending_launch;
    _pending_launch = -1;
    if (idx < 0 || idx >= static_cast<int>(registry.size())) return;

    const auto& entry = registry[idx];
    ESP_LOGI(TAG, "launching '%s'", entry.name.c_str());

    auto& mc = mooncake::GetMooncake();
    _active_target = mc.installApp(entry.factory());
    mc.openApp(_active_target);
    close();
}

void AppLauncher::onClose()
{
    ESP_LOGI(TAG, "onClose");
    destroyUI();
}

void AppLauncher::onSleeping()
{
    if (_active_target < 0) return;
    auto& mc = mooncake::GetMooncake();
    if (!mc.isAppExist(_active_target)) {
        _active_target = -1;
        open();
        return;
    }
    if (mc.getAppCurrentState(_active_target) == mooncake::AppAbility::StateSleeping) {
        ESP_LOGI(TAG, "target app finished — waking launcher");
        mc.uninstallApp(_active_target);
        _active_target = -1;
        open();
    }
}

void AppLauncher::requestLaunch(int registry_index)
{
    _pending_launch = registry_index;
}

void AppLauncher::buildUI()
{
    if (lvgl_port_lock(-1) == false) {
        ESP_LOGE(TAG, "buildUI: lvgl_port_lock FAILED — no UI drawn");
        return;
    }

    lv_obj_t* scr = lv_scr_act();
    int w = lv_display_get_horizontal_resolution(lv_obj_get_display(scr));
    int h = lv_display_get_vertical_resolution(lv_obj_get_display(scr));
    ESP_LOGI(TAG, "buildUI: screen=%dx%d  using SCREEN_W=%d SCREEN_H=%d", w, h, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    _root = lv_obj_create(scr);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(_root, 0, 0);
    lv_obj_set_style_bg_color(_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    ESP_LOGI(TAG, "buildUI: root created %dx%d COL_BG=0x%06lX", SCREEN_W, SCREEN_H, (unsigned long)COL_BG);

    // Header bar
    lv_obj_t* header = lv_obj_create(_root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, SCREEN_W, HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COL_HEADER_BG), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Launcher");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 24, 0);

    // Grid container
    const int grid_y    = HEADER_H + 24;
    const int cell_w    = ICON_SIZE + ICON_PAD * 2;
    const int cell_h    = ICON_SIZE + LABEL_GAP + LABEL_H + ICON_PAD;
    const int grid_w    = GRID_COLS * cell_w;
    const int grid_left = (SCREEN_W - grid_w) / 2;

    lv_obj_t* grid = lv_obj_create(_root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, grid_w, GRID_ROWS * cell_h);
    lv_obj_set_pos(grid, grid_left, grid_y);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    const auto& registry = GetAppRegistry();
    for (int slot = 0; slot < GRID_SLOTS; ++slot) {
        if (slot < static_cast<int>(registry.size())) {
            addIcon(grid, slot, slot);
        } else {
            addEmptySlot(grid, slot);
        }
    }

    lvgl_port_unlock();
}

void AppLauncher::addIcon(lv_obj_t* parent, int registry_index, int slot_index)
{
    const auto& entry = GetAppRegistry()[registry_index];

    const int row    = slot_index / GRID_COLS;
    const int col    = slot_index % GRID_COLS;
    const int cell_w = ICON_SIZE + ICON_PAD * 2;
    const int cell_h = ICON_SIZE + LABEL_GAP + LABEL_H + ICON_PAD;
    const int ox     = col * cell_w + ICON_PAD;
    const int oy     = row * cell_h;

    lv_obj_t* icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, ICON_SIZE, ICON_SIZE);
    lv_obj_set_pos(icon, ox, oy);
    lv_obj_set_style_bg_color(icon, lv_color_hex(entry.icon_color & 0x00FFFFFF), 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(icon, 24, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    // Use heap-allocated user data so lambda capture isn't required.
    auto* ud = new IconUserData{this, registry_index};
    lv_obj_add_event_cb(icon, icon_click_cb, LV_EVENT_CLICKED, ud);
    // Leak is negligible (N icons, lifetime of launcher). Freed via lv_obj delete in destroyUI.
    lv_obj_set_user_data(icon, ud);

    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, entry.name.c_str());
    lv_obj_set_style_text_color(label, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, ICON_SIZE);
    lv_obj_set_pos(label, ox, oy + ICON_SIZE + LABEL_GAP);
}

void AppLauncher::addEmptySlot(lv_obj_t* parent, int slot_index)
{
    const int row    = slot_index / GRID_COLS;
    const int col    = slot_index % GRID_COLS;
    const int cell_w = ICON_SIZE + ICON_PAD * 2;
    const int cell_h = ICON_SIZE + LABEL_GAP + LABEL_H + ICON_PAD;
    const int ox     = col * cell_w + ICON_PAD;
    const int oy     = row * cell_h;

    lv_obj_t* slot = lv_obj_create(parent);
    lv_obj_remove_style_all(slot);
    lv_obj_set_size(slot, ICON_SIZE, ICON_SIZE);
    lv_obj_set_pos(slot, ox, oy);
    lv_obj_set_style_bg_color(slot, lv_color_hex(COL_EMPTY), 0);
    lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(slot, 24, 0);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
}

void AppLauncher::destroyUI()
{
    if (!_root) return;
    if (lvgl_port_lock(-1) == false) return;

    // Free IconUserData blobs attached via lv_obj_set_user_data before deleting.
    // LVGL will cascade delete children; we walk descendants to free our allocs.
    // Simpler: iterate grid children and free their user_data if set.
    uint32_t child_count = lv_obj_get_child_count(_root);
    for (uint32_t i = 0; i < child_count; ++i) {
        lv_obj_t* c = lv_obj_get_child(_root, i);
        uint32_t gc = lv_obj_get_child_count(c);
        for (uint32_t j = 0; j < gc; ++j) {
            lv_obj_t* gcobj = lv_obj_get_child(c, j);
            auto* ud = static_cast<IconUserData*>(lv_obj_get_user_data(gcobj));
            if (ud) {
                delete ud;
                lv_obj_set_user_data(gcobj, nullptr);
            }
        }
    }

    lv_obj_del(_root);
    _root = nullptr;
    lvgl_port_unlock();
}
