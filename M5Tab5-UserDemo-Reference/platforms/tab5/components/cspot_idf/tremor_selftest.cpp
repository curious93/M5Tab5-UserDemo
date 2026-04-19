// Tremor standalone self-test. Decodes the embedded test_tone.ogg (1 kHz
// sine, 2 s, 44.1 kHz stereo, Vorbis 96 kbps) via Tremor's `ov_open_callbacks`
// → `ov_read` loop, exactly like TrackPlayer does with CDN data. The
// resampler + i2s_write path is the same `Tab5AudioSink` cspot uses at
// runtime. Outcome reading:
//
//   audible tone ⇒ Tremor+sink work fine; cspot's near-zero PCM is then a
//     CDNAudioFile issue (AES-CTR chunk boundary / readBytes recursion).
//   silent       ⇒ Tremor itself outputs silence on P4 (PSRAM corruption,
//     ODR, linker issue) regardless of input. Different fix required.

#include "cspot_idf.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <vector>

#include <ivorbisfile.h>

#include "Tab5AudioSink.h"

extern "C" {
extern const uint8_t test_tone_ogg_start[] asm("_binary_test_tone_ogg_start");
extern const uint8_t test_tone_ogg_end[]   asm("_binary_test_tone_ogg_end");
}

static const char* TAG = "tremor-selftest";

namespace {

struct BufferReader {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

size_t read_cb(void* ptr, size_t size, size_t nmemb, void* user) {
    auto* r = static_cast<BufferReader*>(user);
    size_t want = size * nmemb;
    size_t avail = (r->pos < r->size) ? (r->size - r->pos) : 0;
    size_t n = (want < avail) ? want : avail;
    if (n) {
        memcpy(ptr, r->data + r->pos, n);
        r->pos += n;
    }
    return n;
}

int seek_cb(void* /*user*/, ogg_int64_t /*offset*/, int /*whence*/) {
    // Non-seekable: Tremor will fall back to linear read.
    return -1;
}

int close_cb(void* /*user*/) { return 0; }

long tell_cb(void* user) {
    auto* r = static_cast<BufferReader*>(user);
    return (long)r->pos;
}

}  // namespace

// Inner body, runs from a dedicated 32 KB-stack task.
static void selftest_body() {
    const size_t bytes = test_tone_ogg_end - test_tone_ogg_start;
    ESP_LOGI(TAG, "embedded test_tone.ogg: %u bytes", (unsigned)bytes);

    BufferReader rdr{test_tone_ogg_start, bytes, 0};
    OggVorbis_File vf{};
    ov_callbacks cbs{read_cb, seek_cb, close_cb, tell_cb};

    ESP_LOGI(TAG, "calling ov_open_callbacks...");
    int ret = ov_open_callbacks(&rdr, &vf, nullptr, 0, cbs);
    ESP_LOGI(TAG, "ov_open_callbacks returned %d", ret);
    if (ret != 0) return;

    Tab5AudioSink sink;
    sink.setParams(48000, 2, 16);
    sink.volumeChanged(16384);  // ~25% — keeps the test audible but quiet

    std::vector<uint8_t> pcm(1024);
    int section = 0;
    uint32_t calls = 0;
    uint32_t sumBytes = 0;
    int32_t  globalPeak = 0;

    while (true) {
        long n = ov_read(&vf, (char*)pcm.data(), pcm.size(), &section);
        if (n == 0) { ESP_LOGI(TAG, "clean EOF after %lu reads, %lu bytes",
                                (unsigned long)calls, (unsigned long)sumBytes); break; }
        if (n < 0)  { ESP_LOGE(TAG, "ov_read error %ld at call %lu",
                                n, (unsigned long)calls); break; }
        sumBytes += n;

        // Peak per read.
        int16_t* s = (int16_t*)pcm.data();
        int32_t peak = 0;
        for (long i = 0; i < n / 2; ++i) {
            int32_t v = s[i]; if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
        if (peak > globalPeak) globalPeak = peak;

        if ((calls & 0xF) == 0) {
            ESP_LOGI(TAG, "read[%lu] n=%ld peak=%ld first=[%d,%d,%d,%d]",
                     (unsigned long)calls, n, (long)peak,
                     s[0], s[1], s[2], s[3]);
        }

        // Play through the EXACT same sink cspot uses at runtime.
        sink.feedPCMFrames(pcm.data(), (size_t)n);
        ++calls;
    }

    ov_clear(&vf);
    ESP_LOGI(TAG, "selftest done — globalPeak=%ld, %lu reads",
             (long)globalPeak, (unsigned long)calls);
    if (globalPeak > 1000) {
        ESP_LOGI(TAG, "VERDICT: Tremor decodes fine — cspot silent bug is in CDNAudioFile");
    } else {
        ESP_LOGE(TAG, "VERDICT: Tremor itself outputs silence — decoder build problem");
    }
}

static TaskHandle_t s_selftest_task = nullptr;
static volatile bool s_selftest_done = false;

static void selftest_task(void*) {
    selftest_body();
    s_selftest_done = true;
    vTaskDelete(nullptr);
}

extern "C" void tremor_selftest_run() {
    // Run on a dedicated 32 KB-stack task (Tremor's ov_open_callbacks uses
    // a deep call stack; the default app_main ~3.5 KB stack overflows).
    s_selftest_done = false;
    xTaskCreate(selftest_task, "tremor_selftest", 32 * 1024, nullptr, 5,
                &s_selftest_task);
    while (!s_selftest_done) vTaskDelay(pdMS_TO_TICKS(100));
}
