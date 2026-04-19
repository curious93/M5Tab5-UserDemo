/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/hal_esp32.h"
extern "C" {
#include "utils/rx8130/rx8130.h"
}
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <bsp/m5stack_tab5.h>
#include <algorithm> // for std::clamp

extern esp_lcd_touch_handle_t _lcd_touch_handle;

static const std::string _tag = "hal";

static void lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    if (_lcd_touch_handle == NULL) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    esp_lcd_touch_read_data(_lcd_touch_handle);
    bool touchpad_pressed =
        esp_lcd_touch_get_coordinates(_lcd_touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
    // mclog::tagInfo(_tag, "touchpad pressed: {}", touchpad_pressed);

    if (!touchpad_pressed) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = touch_x[0];
        data->point.y = touch_y[0];
    }
}

void HalEsp32::init()
{
    ESP_LOGI(_tag.c_str(), "init");

    ESP_LOGI(_tag.c_str(), "camera osc init");
    bsp_cam_osc_init();

    ESP_LOGI(_tag.c_str(), "i2c init");
    bsp_i2c_init();

    ESP_LOGI(_tag.c_str(), "io expander init");
    i2c_master_bus_handle_t i2c_bus_handle = bsp_i2c_get_handle();
    bsp_io_expander_pi4ioe_init(i2c_bus_handle);

    setChargeQcEnable(true);
    delay(50);
    setChargeEnable(true);
 
    ESP_LOGI(_tag.c_str(), "camera hardware reset pulse");
    bsp_camera_reset(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    bsp_camera_reset(false);
    vTaskDelay(pdMS_TO_TICKS(100));
 
    ESP_LOGI(_tag.c_str(), "codec init");
    delay(200);
    bsp_codec_init();

    // Boot-time audio self-test: 1.5 s of 1 kHz square wave @ 48 kHz stereo.
    // Proves ES8388 + PI4IO SPK_EN + amp path works before cspot runs.
    // Tone is generated in a 4 KB buffer (500 stereo frames) and looped.
    {
        auto* codec = bsp_get_codec_handle();
        if (codec && codec->i2s_write && codec->set_volume &&
            codec->set_mute && codec->i2s_reconfig_clk_fn) {
            ESP_LOGI(_tag.c_str(), "audio self-test: 1 kHz beep via ES8388");
            codec->set_volume(80);
            codec->set_mute(false);
            codec->i2s_reconfig_clk_fn(48000, 16, I2S_SLOT_MODE_STEREO);
            int16_t chunk[1000];  // 500 stereo frames = ~10 ms @ 48 kHz
            for (int i = 0; i < 500; ++i) {
                int16_t s = ((i / 24) & 1) ? 12000 : -12000;  // ~1 kHz
                chunk[i * 2]     = s;
                chunk[i * 2 + 1] = s;
            }
            size_t total = 0;
            for (int n = 0; n < 150; ++n) {  // 150 × 10 ms = 1.5 s
                size_t w = 0;
                codec->i2s_write(chunk, sizeof(chunk), &w, portMAX_DELAY);
                total += w;
            }
            ESP_LOGI(_tag.c_str(), "audio self-test: wrote=%u bytes", (unsigned)total);
        }
    }

    ESP_LOGI(_tag.c_str(), "imu init");
    imu_init();

    ESP_LOGI(_tag.c_str(), "ina226 init");
    ina226.begin(i2c_bus_handle, 0x41);
    ina226.configure(INA226_AVERAGES_16, INA226_BUS_CONV_TIME_1100US, INA226_SHUNT_CONV_TIME_1100US,
                     INA226_MODE_SHUNT_BUS_CONT);
    ina226.calibrate(0.005, 8.192);
    ESP_LOGI(_tag.c_str(), "bus voltage: %f", ina226.readBusVoltage());

    ESP_LOGI(_tag.c_str(), "rx8130 init");
    rx8130.begin(i2c_bus_handle, 0x32);
    rx8130.initBat();
    clearRtcIrq();
    update_system_time();

    ESP_LOGI(_tag.c_str(), "display init");
    bsp_reset_tp();
    bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                             .buffer_size   = BSP_LCD_H_RES * BSP_LCD_V_RES,
                             .double_buffer = true,
                             .flags         = {
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
                                 .buff_dma = false,
#else
                                 .buff_dma = true,
#endif
                                 .buff_spiram = true,
                                 .sw_rotate   = true,
                             }};
    lvDisp = bsp_display_start_with_config(&cfg);
    lv_display_set_rotation(lvDisp, LV_DISPLAY_ROTATION_90);
    bsp_display_backlight_on();
    bsp_display_brightness_set(25); // reduced for camera color verification

    // Touchpad lvgl indev
    ESP_LOGI(_tag.c_str(), "create lvgl touchpad indev");
    lv_indev_t* lvTouchpad = lv_indev_create();
    lv_indev_set_type(lvTouchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvTouchpad, lvgl_read_cb);
    lv_indev_set_display(lvTouchpad, lvDisp);

    // mclog::tagInfo(_tag, "usb host init");
    // bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, true);

    // mclog::tagInfo(_tag, "hid init");
    // hid_init();

    // mclog::tagInfo(_tag, "rs485 init");
    // rs485_init();

    ESP_LOGI(_tag.c_str(), "set gpio output capability");
    set_gpio_output_capability();

    bsp_display_unlock();
}

static const gpio_num_t _driver_gpios[] = {
    // EXT I2C
    GPIO_NUM_0,
    GPIO_NUM_1,
    // esp-hosted esp32c6
    GPIO_NUM_8,
    GPIO_NUM_9,
    GPIO_NUM_10,
    GPIO_NUM_11,
    GPIO_NUM_12,
    GPIO_NUM_13,
    GPIO_NUM_15,
    // Display
    GPIO_NUM_22,
    GPIO_NUM_23,
    // Audio
    GPIO_NUM_26,
    GPIO_NUM_27,
    GPIO_NUM_28,
    GPIO_NUM_29,
    GPIO_NUM_30,
    // SYS I2C
    GPIO_NUM_31,
    GPIO_NUM_32,
    // uSD card
    GPIO_NUM_39,
    GPIO_NUM_40,
    GPIO_NUM_41,
    GPIO_NUM_42,
    GPIO_NUM_43,
    GPIO_NUM_44,
};

void HalEsp32::set_gpio_output_capability()
{
    // gpio_set_drive_capability((gpio_num_t)48, GPIO_DRIVE_CAP_0);
    for (int i = 0; i < sizeof(_driver_gpios) / sizeof(_driver_gpios[0]); i++) {
        gpio_num_t gpio = _driver_gpios[i];
        esp_err_t ret   = gpio_set_drive_capability(gpio, GPIO_DRIVE_CAP_0);
        if (ret == ESP_OK) {
            printf("GPIO %d drive capability set to GPIO_DRIVE_CAP_0\n", gpio);
        } else {
            printf("Failed to set GPIO %d drive capability: %s\n", gpio, esp_err_to_name(ret));
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                                   System                                   */
/* -------------------------------------------------------------------------- */
#include <driver/temperature_sensor.h>
static temperature_sensor_handle_t _temp_sensor = nullptr;

void HalEsp32::delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t HalEsp32::millis()
{
    return esp_timer_get_time() / 1000;
}

int HalEsp32::getCpuTemp()
{
    if (_temp_sensor == nullptr) {
        temperature_sensor_config_t temp_sensor_config = {
            .range_min = 20,
            .range_max = 100,
        };
        temperature_sensor_install(&temp_sensor_config, &_temp_sensor);
        temperature_sensor_enable(_temp_sensor);
    }

    float temp = 0;
    temperature_sensor_get_celsius(_temp_sensor, &temp);

    return temp;
}

/* -------------------------------------------------------------------------- */
/*                                   Display                                  */
/* -------------------------------------------------------------------------- */
void HalEsp32::setDisplayBrightness(uint8_t brightness)
{
    _current_lcd_brightness = std::clamp((int)brightness, 0, 100);
    ESP_LOGI("hal", "set display brightness: %d%%", _current_lcd_brightness);
    bsp_display_brightness_set(_current_lcd_brightness);
}

uint8_t HalEsp32::getDisplayBrightness()
{
    return _current_lcd_brightness;
}

void HalEsp32::lvglLock()
{
    lvgl_port_lock(0);
}

void HalEsp32::lvglUnlock()
{
    lvgl_port_unlock();
}

/* -------------------------------------------------------------------------- */
/*                                     RTC                                    */
/* -------------------------------------------------------------------------- */
void HalEsp32::clearRtcIrq()
{
    ESP_LOGI(_tag.c_str(), "clear rtc irq");
    rx8130.clearIrqFlags();
    rx8130.disableIrq();
}

void HalEsp32::setRtcTime(tm time)
{
    ESP_LOGI(_tag.c_str(), "set rtc time to %d/%02d/%02d %02d:%02d:%02d", time.tm_year + 1900, time.tm_mon + 1,
                   time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
    rx8130.setTime(&time);
    delay(50);

    update_system_time();
}

void HalEsp32::update_system_time()
{
    ESP_LOGI(_tag.c_str(), "update system time");
    struct tm time;
    rx8130.getTime(&time);
    ESP_LOGI(_tag.c_str(), "sync to rtc time: %d-%02d-%02d %02d:%02d:%02d", time.tm_year + 1900,
                   time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
    struct timeval now;
    now.tv_sec  = mktime(&time);
    now.tv_usec = 0;
    settimeofday(&now, NULL);
}

/* -------------------------------------------------------------------------- */
/*                                   SD Card                                  */
/* -------------------------------------------------------------------------- */
#include <dirent.h>
#include <sys/types.h>

bool HalEsp32::isSdCardMounted()
{
    return true;
}

std::vector<hal::HalBase::FileEntry_t> HalEsp32::scanSdCard(const std::string& dirPath)
{
    std::vector<hal::HalBase::FileEntry_t> file_entries;

    ESP_LOGI(_tag.c_str(), "init sd card");
    if (bsp_sdcard_init("/sd", 25) != ESP_OK) {
        ESP_LOGE(_tag.c_str(), "failed to mount sd card");
        return file_entries;
    }

    std::string target_path = "/sd/" + dirPath;

    DIR* dir = opendir(target_path.c_str());
    if (dir == nullptr) {
        ESP_LOGE(_tag.c_str(), "failed to open directory: %s", target_path.c_str());
        return file_entries;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") {
            continue;
        }

        hal::HalBase::FileEntry_t file_entry;
        file_entry.name  = entry->d_name;
        file_entry.isDir = (entry->d_type == DT_DIR);
        file_entries.push_back(file_entry);
    }

    closedir(dir);

    ESP_LOGI(_tag.c_str(), "deinit sd card");
    bsp_sdcard_deinit("/sd");

    return file_entries;
}

/* -------------------------------------------------------------------------- */
/*                                  Interface                                 */
/* -------------------------------------------------------------------------- */
bool HalEsp32::usbCDetect()
{
    return bsp_usb_c_detect();
    // return false;
}

bool HalEsp32::headPhoneDetect()
{
    return bsp_headphone_detect();
}

std::vector<uint8_t> HalEsp32::i2cScan(bool isInternal)
{
    i2c_master_bus_handle_t i2c_bus_handle;
    std::vector<uint8_t> addrs;

    if (isInternal) {
        i2c_bus_handle = bsp_i2c_get_handle();
    } else {
        i2c_bus_handle = bsp_ext_i2c_get_handle();
    }

    esp_err_t ret;
    uint8_t address;

    for (int i = 16; i < 128; i += 16) {
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            ret     = i2c_master_probe(i2c_bus_handle, address, 50);
            if (ret == ESP_OK) {
                addrs.push_back(address);
            }
        }
    }

    return addrs;
}

void HalEsp32::initPortAI2c()
{
    ESP_LOGI(_tag.c_str(), "init port a i2c");
    bsp_ext_i2c_init();
}

void HalEsp32::deinitPortAI2c()
{
    ESP_LOGI(_tag.c_str(), "deinit port a i2c");
    bsp_ext_i2c_deinit();
}

void HalEsp32::gpioInitOutput(uint8_t pin)
{
    gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
}

void HalEsp32::gpioSetLevel(uint8_t pin, bool level)
{
    gpio_set_level((gpio_num_t)pin, level);
}

void HalEsp32::gpioReset(uint8_t pin)
{
    gpio_set_level((gpio_num_t)pin, false);
}
