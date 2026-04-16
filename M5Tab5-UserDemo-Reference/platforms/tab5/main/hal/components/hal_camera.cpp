/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Camera capture task for the Tab5 (ESP32-P4).
 *
 * Architecture (v4 — fluid display):
 *   Camera Task (Core 1)
 *     DQBUF → motion_detect → PPA mirror_x → publish to ping-pong buffer → QBUF
 *     ZERO LVGL API calls — no lock contention with LVGL task.
 *
 *   LVGL Task (Core 0) — via lv_timer registered in app_main
 *     Calls consumeReadyFrame() → draws bbox pixels → lv_canvas_set_buffer()
 *     Runs inside LVGL task → owns the lock intrinsically → zero wait time.
 *
 * Ping-pong buffer:
 *   g_ppa_buf[0/1]  — two 1280×720 RGB565 PSRAM buffers
 *   g_ready_idx     — atomic: -1 = none ready, 0/1 = index of latest frame
 *   g_write_idx     — which buffer the camera task writes (never shared)
 *
 *   After PPA: atomic exchange publishes new frame; unconsumed frames are
 *   recycled as the next write target so memory is never wasted.
 */
#include "hal/hal_esp32.h"
#include "../utils/task_controller/task_controller.h"
#include <mooncake_log.h>
#include <vector>
#include <driver/gpio.h>
#include <memory>
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <atomic>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "driver/i2c_master.h"
#include "driver/ppa.h"
#include "imlib.h"
#include "freertos/queue.h"
#include "motion_detector.h"

#define CAMERA_WIDTH  1280
#define CAMERA_HEIGHT 720

static lv_obj_t* camera_canvas;
static QueueHandle_t queue_camera_ctrl = NULL;

#define TASK_CONTROL_PAUSE  0
#define TASK_CONTROL_RESUME 1
#define TASK_CONTROL_EXIT   2

static bool is_camera_capturing = false;
static std::mutex camera_mutex;

static const char* TAG = "camera";

#define EXAMPLE_VIDEO_BUFFER_COUNT 2
#define MEMORY_TYPE                V4L2_MEMORY_MMAP
#define CAM_DEV_PATH               ESP_VIDEO_MIPI_CSI_DEVICE_NAME
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

typedef struct cam {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint8_t* buffer[EXAMPLE_VIDEO_BUFFER_COUNT];
} cam_t;

typedef enum {
    EXAMPLE_VIDEO_FMT_RAW8   = V4L2_PIX_FMT_SBGGR8,
    EXAMPLE_VIDEO_FMT_RAW10  = V4L2_PIX_FMT_SBGGR10,
    EXAMPLE_VIDEO_FMT_GREY   = V4L2_PIX_FMT_GREY,
    EXAMPLE_VIDEO_FMT_RGB565 = V4L2_PIX_FMT_RGB565,
    EXAMPLE_VIDEO_FMT_RGB888 = V4L2_PIX_FMT_RGB24,
    EXAMPLE_VIDEO_FMT_YUV422 = V4L2_PIX_FMT_YUV422P,
    EXAMPLE_VIDEO_FMT_YUV420 = V4L2_PIX_FMT_YUV420,
} example_fmt_t;

int app_video_open(char* dev, example_fmt_t init_fmt)
{
    struct v4l2_format default_format;
    struct v4l2_capability capability;
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Open video failed");
        return -1;
    }

    if (ioctl(fd, VIDIOC_QUERYCAP, &capability)) {
        ESP_LOGE(TAG, "failed to get capability");
        goto exit_0;
    }

    ESP_LOGI(TAG, "version: %d.%d.%d", (uint16_t)(capability.version >> 16), (uint8_t)(capability.version >> 8),
             (uint8_t)capability.version);
    ESP_LOGI(TAG, "driver:  %s", capability.driver);
    ESP_LOGI(TAG, "card:    %s", capability.card);
    ESP_LOGI(TAG, "bus:     %s", capability.bus_info);

    memset(&default_format, 0, sizeof(struct v4l2_format));
    default_format.type = type;
    if (ioctl(fd, VIDIOC_G_FMT, &default_format) != 0) {
        ESP_LOGE(TAG, "failed to get format");
        goto exit_0;
    }

    ESP_LOGI(TAG, "width=%" PRIu32 " height=%" PRIu32, default_format.fmt.pix.width, default_format.fmt.pix.height);

    if (default_format.fmt.pix.pixelformat != init_fmt) {
        struct v4l2_format format = {.type = type,
                                     .fmt  = {.pix = {.width       = default_format.fmt.pix.width,
                                                      .height      = default_format.fmt.pix.height,
                                                      .pixelformat = init_fmt}}};

        if (ioctl(fd, VIDIOC_S_FMT, &format) != 0) {
            ESP_LOGE(TAG, "failed to set format");
            goto exit_0;
        }
    }

    return fd;
exit_0:
    close(fd);
    return -1;
}

static esp_err_t new_cam(int cam_fd, cam_t** ret_wc)
{
    int ret;
    struct v4l2_format format;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_requestbuffers req;
    cam_t* wc;

    printf("new_cam: getting format...\n");
    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam_fd, VIDIOC_G_FMT, &format) != 0) {
        ESP_LOGE(TAG, "Failed get fmt");
        return ESP_FAIL;
    }

    wc = (cam_t*)malloc(sizeof(cam_t));
    if (!wc) {
        return ESP_ERR_NO_MEM;
    }

    wc->fd           = cam_fd;
    wc->width        = format.fmt.pix.width;
    wc->height       = format.fmt.pix.height;
    wc->pixel_format = format.fmt.pix.pixelformat;

    memset(&req, 0, sizeof(req));
    req.count  = ARRAY_SIZE(wc->buffer);
    req.type   = type;
    req.memory = MEMORY_TYPE;
    if (ioctl(wc->fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "failed to req buffers");
        ret = ESP_FAIL;
        goto errout;
    }
    printf("new_cam: reqbufs done. mmap loop...\n");

    for (int i = 0; i < ARRAY_SIZE(wc->buffer); i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type   = type;
        buf.memory = MEMORY_TYPE;
        buf.index  = i;
        if (ioctl(wc->fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "failed to query buffer");
            ret = ESP_FAIL;
            goto errout;
        }

        wc->buffer[i] = (uint8_t*)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, wc->fd, buf.m.offset);
        if (!wc->buffer[i]) {
            ESP_LOGE(TAG, "failed to map buffer");
            ret = ESP_FAIL;
            goto errout;
        }

        if (ioctl(wc->fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "failed to queue frame buffer");
            ret = ESP_FAIL;
            goto errout;
        }
        printf("new_cam: buffer %d queued\n", i);
    }

    printf("new_cam: mmap done. starting stream...\n");
    if (ioctl(wc->fd, VIDIOC_STREAMON, &type)) {
        ESP_LOGE(TAG, "failed to start stream");
        ret = ESP_FAIL;
        goto errout;
    }
    printf("new_cam: stream started!\n");

    *ret_wc = wc;
    return ESP_OK;

errout:
    free(wc);
    return ret;
}

static bool cam_is_initial = false;
static cam_t* camera       = NULL;

// Ping-pong display buffers — camera task writes, LVGL lv_timer reads.
// g_ready_idx: -1 = no frame ready, 0/1 = index of latest PPA-processed frame.
// g_write_idx: buffer the camera task is currently writing (never shared).
static uint8_t* g_ppa_buf[2]         = {nullptr, nullptr};
static std::atomic<int> g_ready_idx  {-1};
static int              g_write_idx  = 0;

void app_camera_display(void* arg)
{
    /* camera config */
    static esp_video_init_csi_config_t csi_config = {
        .sccb_config =
            {
                .init_sccb  = false,
                .i2c_handle = NULL,
                .freq       = 400000,
            },
        .reset_pin = -1,
        .pwdn_pin  = -1,
    };
    csi_config.sccb_config.i2c_handle = bsp_i2c_get_handle();

    esp_video_init_config_t cam_config = {
        .csi  = &csi_config,
        .dvp  = NULL,
        .jpeg = NULL,
    };

    if (!cam_is_initial) {
        camera = (cam_t*)malloc(sizeof(cam_t));
        printf("\n============= video init ==============\n");
        cam_is_initial = true;
        ESP_ERROR_CHECK(esp_video_init(&cam_config));
        printf("\n============= video open ==============\n");
        int video_cam_fd = app_video_open(CAM_DEV_PATH, EXAMPLE_VIDEO_FMT_RGB565);
        if (video_cam_fd < 0) {
            ESP_LOGE(TAG, "video cam open failed");
            goto done;
        }
        ESP_ERROR_CHECK(new_cam(video_cam_fd, &camera));
    }

    {
        struct v4l2_buffer buf;
        const uint32_t img_show_size = CAMERA_WIDTH * CAMERA_HEIGHT * 2;

        // Allocate two ping-pong PPA output buffers (each 1280×720×2 = 1.84 MB).
        g_ppa_buf[0] = (uint8_t*)heap_caps_calloc(img_show_size, 1, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
        g_ppa_buf[1] = (uint8_t*)heap_caps_calloc(img_show_size, 1, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
        if (!g_ppa_buf[0] || !g_ppa_buf[1]) {
            ESP_LOGE(TAG, "PPA buffer alloc failed");
            goto done;
        }
        g_write_idx = 0;
        g_ready_idx.store(-1, std::memory_order_relaxed);

        ppa_client_handle_t ppa_srm_handle = NULL;
        ppa_client_config_t ppa_srm_config = {
            .oper_type             = PPA_OPERATION_SRM,
            .max_pending_trans_num = 1,
        };
        ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));

        motion_detector::init();

        int task_control       = 0;
        uint32_t fps_frame_count = 0;
        int64_t  fps_last_print  = esp_timer_get_time();

        // Per-stage profiling accumulators (µs, summed over 1-second window).
        int64_t prof_dqbuf_us  = 0;
        int64_t prof_motion_us = 0;
        int64_t prof_ppa_us    = 0;
        int64_t prof_qbuf_us   = 0;

        while (1) {
            int64_t t0 = esp_timer_get_time();

            memset(&buf, 0, sizeof(buf));
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = MEMORY_TYPE;
            if (ioctl(camera->fd, VIDIOC_DQBUF, &buf) != 0) {
                ESP_LOGE(TAG, "failed to receive video frame");
                break;
            }
            int64_t t1 = esp_timer_get_time();

            // Motion detection: runs on every frame from the raw (unmirrored)
            // camera buffer. flip_x=true corrects coordinates to match the
            // mirrored display output produced by PPA below.
            motion_detector::process_frame(
                reinterpret_cast<const uint16_t*>(camera->buffer[buf.index]),
                CAMERA_WIDTH,
                /*flip_x=*/true);
            int64_t t2 = esp_timer_get_time();

            // PPA: horizontal mirror into the current write buffer.
            // Runs on every frame — no N-skipping needed now that the camera
            // task never touches LVGL (zero lock-contention).
            ppa_srm_oper_config_t srm_config = {
                .in  = {.buffer         = camera->buffer[buf.index],
                        .pic_w          = 1280,
                        .pic_h          = 720,
                        .block_w        = 1280,
                        .block_h        = 720,
                        .block_offset_x = 0,
                        .block_offset_y = 0,
                        .srm_cm         = PPA_SRM_COLOR_MODE_RGB565},
                .out = {.buffer         = g_ppa_buf[g_write_idx],
                        .buffer_size    = img_show_size,
                        .pic_w          = 1280,
                        .pic_h          = 720,
                        .block_offset_x = 0,
                        .block_offset_y = 0,
                        .srm_cm         = PPA_SRM_COLOR_MODE_RGB565},
                .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
                .scale_x        = 1,
                .scale_y        = 1,
                .mirror_x       = true,
                .mirror_y       = false,
                .rgb_swap       = false,
                .byte_swap      = false,
                .mode           = PPA_TRANS_MODE_BLOCKING,
            };
            ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config);
            int64_t t3 = esp_timer_get_time();

            // Atomically publish the new frame. If the LVGL timer hasn't
            // consumed the previous frame yet, recycle that buffer as the
            // next write target (avoids ever re-using the buffer LVGL is
            // currently reading from).
            {
                int old_ready = g_ready_idx.exchange(g_write_idx, std::memory_order_release);
                g_write_idx   = (old_ready >= 0) ? old_ready : (g_write_idx ^ 1);
            }

            if (ioctl(camera->fd, VIDIOC_QBUF, &buf) != 0) {
                ESP_LOGE(TAG, "failed to free video frame");
            }
            int64_t t4 = esp_timer_get_time();

            prof_dqbuf_us  += (t1 - t0);
            prof_motion_us += (t2 - t1);
            prof_ppa_us    += (t3 - t2);
            prof_qbuf_us   += (t4 - t3);

            ++fps_frame_count;
            int64_t now_us = esp_timer_get_time();
            if (now_us - fps_last_print >= 1000000) {
                uint32_t n = fps_frame_count ? fps_frame_count : 1;
                printf("CAM: %lu fps | motion=%s target=(%+.2f, %+.2f) | "
                       "avg us: dqbuf=%lld motion=%lld ppa=%lld qbuf=%lld\n",
                       static_cast<unsigned long>(fps_frame_count),
                       motion_detector::is_detected() ? "yes" : " no",
                       motion_detector::target_x(),
                       motion_detector::target_y(),
                       (long long)(prof_dqbuf_us  / n),
                       (long long)(prof_motion_us / n),
                       (long long)(prof_ppa_us    / n),
                       (long long)(prof_qbuf_us   / n));
                fps_frame_count = 0;
                fps_last_print  = now_us;
                prof_dqbuf_us = prof_motion_us = prof_ppa_us = prof_qbuf_us = 0;
            }

            if (xQueueReceive(queue_camera_ctrl, &task_control, 0) == pdPASS) {
                if (task_control == TASK_CONTROL_PAUSE) {
                    ESP_LOGI(TAG, "task pause");
                    if (xQueueReceive(queue_camera_ctrl, &task_control, portMAX_DELAY) == pdPASS) {
                        if (task_control == TASK_CONTROL_EXIT) {
                            break;
                        } else {
                            ESP_LOGI(TAG, "task resume");
                        }
                    }
                }
            }

            // Yield once per frame so LVGL task, idle task, and watchdog get
            // serviced. DQBUF already blocks ~28 ms waiting for the sensor —
            // no artificial delay needed.
            taskYIELD();
        }

        ppa_unregister_client(ppa_srm_handle);
    }

done:
    if (g_ppa_buf[0]) { heap_caps_free(g_ppa_buf[0]); g_ppa_buf[0] = nullptr; }
    if (g_ppa_buf[1]) { heap_caps_free(g_ppa_buf[1]); g_ppa_buf[1] = nullptr; }
    g_ready_idx.store(-1, std::memory_order_relaxed);

    camera_mutex.lock();
    is_camera_capturing = false;
    camera_mutex.unlock();

    vTaskDelete(NULL);
}

// Called from the LVGL lv_timer callback (app_main.cpp) to retrieve the
// latest PPA-processed frame. Returns a pointer to the RGB565 buffer
// (1280×720) or nullptr if no new frame has arrived since the last call.
// Atomically marks the frame as consumed so the camera task can recycle it.
uint8_t* HalEsp32::consumeReadyFrame()
{
    int idx = g_ready_idx.exchange(-1, std::memory_order_acquire);
    return (idx >= 0) ? g_ppa_buf[idx] : nullptr;
}

void HalEsp32::startCameraCapture(lv_obj_t* imgCanvas)
{
    mclog::tagInfo(TAG, "start camera capture");

    camera_canvas = imgCanvas;

    queue_camera_ctrl = xQueueCreate(10, sizeof(int));
    if (queue_camera_ctrl == NULL) {
        ESP_LOGD(TAG, "Failed to create semaphore\n");
    }

    is_camera_capturing = true;
    xTaskCreatePinnedToCore(app_camera_display, "cam", 8 * 1024, NULL, 5, NULL, 1);
}

void HalEsp32::stopCameraCapture()
{
    mclog::tagInfo(TAG, "stop camera capture");

    int control_state = 0;  // pause
    xQueueSend(queue_camera_ctrl, &control_state, portMAX_DELAY);

    control_state = 2;  // exit
    xQueueSend(queue_camera_ctrl, &control_state, portMAX_DELAY);
}

bool HalEsp32::isCameraCapturing()
{
    std::lock_guard<std::mutex> lock(camera_mutex);
    return is_camera_capturing;
}
