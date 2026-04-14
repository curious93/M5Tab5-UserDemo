#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/hal_esp32.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "app";
static HalEsp32 device_hal;
extern float vision_target_x;
extern float vision_target_y;
extern bool vision_detected;

// UI Elements
static lv_obj_t * eye_l;
static lv_obj_t * eye_r;
static lv_obj_t * pupil_l;
static lv_obj_t * pupil_r;
static lv_obj_t * mouth;

static void update_avatar(bool touched, int px, int py, float ax, float ay) {
    if (touched) {
        // Augen verengen bei Berührung
        lv_obj_set_height(eye_l, 10);
        lv_obj_set_height(eye_r, 10);
        lv_obj_set_style_bg_color(mouth, lv_color_hex(0x00FF00), 0); // Grüner Mund
        lv_obj_add_flag(pupil_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pupil_r, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Normale Augen
        lv_obj_set_height(eye_l, 80);
        lv_obj_set_height(eye_r, 80);
        lv_obj_set_style_bg_color(mouth, lv_color_hex(0x00AAFF), 0); // Blauer Mund
        lv_obj_remove_flag(pupil_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(pupil_r, LV_OBJ_FLAG_HIDDEN);

        // Pupillen-Bewegung basierend auf IMU (Tilt)
        // Pupillen-Bewegung: Vision hat Priorität vor IMU (Tilt)
        int move_x, move_y;
        if (vision_detected) {
            move_x = (int)(vision_target_x * 40.0f);
            move_y = (int)(vision_target_y * 45.0f);
        } else {
            move_x = (int)(ax * 30.0f);
            move_y = (int)((ay + 0.5f) * 40.0f); 
        }
 
        lv_obj_set_pos(pupil_l, move_x, move_y);
        lv_obj_set_pos(pupil_r, move_x, move_y);

        // Mund-Animation (Vibration/Sprechen)
        static int mouth_phase = 0;
        int mouth_h = 20 + (int)(sin(mouth_phase * 0.2f) * 10.0f);
        lv_obj_set_height(mouth, mouth_h);
        mouth_phase++;
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Initializing Robo-Avatar with IMU Sensing...");

    device_hal.init();

    lv_display_t * lvDisp = device_hal.lvDisp;
    if (lvDisp == NULL) {
        ESP_LOGE(TAG, "Failed to get display handle!");
        return;
    }

    if (lvgl_port_lock(-1)) {
        lv_obj_t * scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        // Gesicht-Container
        lv_obj_t * face = lv_obj_create(scr);
        lv_obj_set_size(face, 1280, 720);
        lv_obj_set_style_bg_opa(face, 0, 0);
        lv_obj_set_style_border_width(face, 0, 0);
        lv_obj_center(face);

        // Linkes Auge
        eye_l = lv_obj_create(face);
        lv_obj_set_size(eye_l, 100, 80);
        lv_obj_align(eye_l, LV_ALIGN_CENTER, -200, -50);
        lv_obj_set_style_bg_color(eye_l, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_radius(eye_l, 40, 0);
        lv_obj_set_style_clip_corner(eye_l, true, 0); // Wichtig für Pupillen

        pupil_l = lv_obj_create(eye_l);
        lv_obj_set_size(pupil_l, 30, 30);
        lv_obj_center(pupil_l);
        lv_obj_set_style_bg_color(pupil_l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(pupil_l, LV_RADIUS_CIRCLE, 0);

        // Rechtes Auge
        eye_r = lv_obj_create(face);
        lv_obj_set_size(eye_r, 100, 80);
        lv_obj_align(eye_r, LV_ALIGN_CENTER, 200, -50);
        lv_obj_set_style_bg_color(eye_r, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_radius(eye_r, 40, 0);
        lv_obj_set_style_clip_corner(eye_r, true, 0);

        pupil_r = lv_obj_create(eye_r);
        lv_obj_set_size(pupil_r, 30, 30);
        lv_obj_center(pupil_r);
        lv_obj_set_style_bg_color(pupil_r, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(pupil_r, LV_RADIUS_CIRCLE, 0);

        // Mund
        mouth = lv_obj_create(face);
        lv_obj_set_size(mouth, 400, 20);
        lv_obj_align(mouth, LV_ALIGN_CENTER, 0, 150);
        lv_obj_set_style_bg_color(mouth, lv_color_hex(0x00AAFF), 0);
        lv_obj_set_style_radius(mouth, 10, 0);

        // Status Text
        lv_obj_t * label = lv_label_create(scr);
        lv_label_set_text(label, "ROBO-AVATAR V2.0\nVISION ACTIVATED");
        lv_obj_set_style_text_color(label, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -50);

        // Camera Canvas (Hidden or background)
        lv_obj_t* cam_canvas = lv_canvas_create(scr);
        lv_obj_set_size(cam_canvas, 1280, 720);
        lv_obj_align(cam_canvas, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_background(cam_canvas); // Put behind eyes

        // Start Camera
        device_hal.startCameraCapture(cam_canvas);

        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Robo-Avatar UI Ready.");

    int log_counter = 0;
    while (1) {
        lv_indev_t * indev = lv_indev_get_next(NULL);
        lv_indev_data_t data_indev;
        bool touched = false;
        int px = 0, py = 0;

        if (indev) {
            lv_indev_get_point(indev, &data_indev.point);
            touched = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
            px = data_indev.point.x;
            py = data_indev.point.y;
        }
        
        // IMU Updates
        device_hal.updateImuData();
        auto& imu = device_hal.imuData;
        
        if (lvgl_port_lock(-1)) {
            update_avatar(touched, px, py, imu.accelX, imu.accelY);
            lvgl_port_unlock();
        }

        if (log_counter++ >= 33) { // ca. 1 Sekunde bei 30ms Delay
            ESP_LOGI(TAG, "IMU Accel: X=%.2f, Y=%.2f, Z=%.2f", imu.accelX, imu.accelY, imu.accelZ);
            log_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
