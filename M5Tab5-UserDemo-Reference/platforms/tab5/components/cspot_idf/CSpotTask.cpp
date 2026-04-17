#include "CSpotTask.h"
#include "cspot_idf.h"

#include <CSpotContext.h>
#include <LoginBlob.h>
#include <SpircHandler.h>
#include <TrackPlayer.h>
#include <BellHTTPServer.h>
#include <MDNSService.h>
#include <Logger.h>
#include <BellUtils.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <nvs_flash.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>

static const char* TAG = "cspot";

// ── global state ──────────────────────────────────────────────────────────────
static std::unique_ptr<CSpotTask>   s_task;
static std::atomic<bool>            s_running{false};

// ── CSpotPlayer ───────────────────────────────────────────────────────────────
CSpotPlayer::CSpotPlayer(std::shared_ptr<cspot::SpircHandler> handler)
    : bell::Task("cspot_player", 8 * 1024, 5, 1)
    , _handler(handler)
{
    _sink = std::make_unique<Tab5AudioSink>();
    _sink->setParams(44100, 2, 16);
    _sink->volumeChanged(32768);   // 50% initial volume

    // 512 KB circular buffer — fits ~3 s at 44100/16/stereo
    _buf = std::make_unique<bell::CircularBuffer>(512 * 1024);

    // DataCallback returns size_t (bytes consumed) and receives codec as string_view
    _handler->getTrackPlayer()->setDataCallback(
        [this](uint8_t* data, size_t bytes, std::string_view) -> size_t {
            feedData(data, bytes);
            return bytes;
        });

    _handler->setEventHandler(
        [this](std::unique_ptr<cspot::SpircHandler::Event> ev) {
            switch (ev->eventType) {
                case cspot::SpircHandler::EventType::PLAY_PAUSE:
                    _paused = std::get<bool>(ev->data);
                    break;
                case cspot::SpircHandler::EventType::FLUSH:
                case cspot::SpircHandler::EventType::SEEK:
                case cspot::SpircHandler::EventType::PLAYBACK_START:
                    _buf->emptyBuffer();
                    break;
                default:
                    break;
            }
        });

    startTask();
}

void CSpotPlayer::feedData(uint8_t* data, size_t len) {
    size_t remaining = len;
    while (remaining > 0) {
        size_t written = _buf->write(data + (len - remaining), remaining);
        if (written == 0) BELL_SLEEP_MS(5);
        remaining -= written;
    }
}

void CSpotPlayer::runTask() {
    std::vector<uint8_t> chunk(2048);
    while (true) {
        if (!_paused) {
            size_t n = _buf->read(chunk.data(), chunk.size());
            if (n > 0) {
                _sink->feedPCMFrames(chunk.data(), n);
            } else {
                BELL_SLEEP_MS(10);
            }
        } else {
            BELL_SLEEP_MS(50);
        }
    }
}

// ── CSpotTask ─────────────────────────────────────────────────────────────────
CSpotTask::CSpotTask(const std::string& deviceName)
    : bell::Task("cspot_main", 32 * 1024, 4, 1)
    , _deviceName(deviceName)
{
    startTask();
}

void CSpotTask::runTask() {
    bell::setDefaultLogger();
    ESP_LOGI(TAG, "cspot starting, device='%s'", _deviceName.c_str());

    // Disable modem sleep — cspot is latency-sensitive
    esp_wifi_set_ps(WIFI_PS_NONE);

    mdns_init();
    mdns_hostname_set("cspot");

    auto blob = std::make_shared<cspot::LoginBlob>(_deviceName);

    std::atomic<bool> gotBlob{false};

    auto server = std::make_unique<bell::BellHTTPServer>(8080);

    server->registerGet("/spotify_info",
        [&server, &blob](struct mg_connection* conn)
            -> std::unique_ptr<bell::BellHTTPServer::HTTPResponse> {
            return server->makeJsonResponse(blob->buildZeroconfInfo());
        });

    server->registerPost("/spotify_info",
        [&server, &blob, &gotBlob](struct mg_connection* conn)
            -> std::unique_ptr<bell::BellHTTPServer::HTTPResponse> {
            nlohmann::json resp;
            resp["status"] = 101;
            resp["spotifyError"] = 0;
            resp["statusString"] = "ERROR-OK";

            auto* info = mg_get_request_info(conn);
            if (info && info->content_length > 0) {
                std::string body(info->content_length, '\0');
                mg_read(conn, body.data(), info->content_length);

                mg_header hd[16];
                int n = mg_split_form_urlencoded(body.data(), hd, 16);
                std::map<std::string, std::string> q;
                for (int i = 0; i < n; i++) q[hd[i].name] = hd[i].value;

                blob->loadZeroconfQuery(q);
                gotBlob = true;
            }
            return server->makeJsonResponse(resp.dump());
        });

    bell::MDNSService::registerService(
        blob->getDeviceName(), "_spotify-connect", "_tcp", "", 8080,
        {{"VERSION", "1.0"}, {"CPath", "/spotify_info"}, {"Stack", "SP"}});

    ESP_LOGI(TAG, "waiting for Spotify to connect on port 8080...");
    while (!gotBlob) {
        BELL_SLEEP_MS(1000);
    }

    ESP_LOGI(TAG, "credentials received, authenticating...");

    auto ctx = cspot::Context::createFromBlob(blob);
    ctx->session->connectWithRandomAp();
    auto token = ctx->session->authenticate(blob);

    if (token.empty()) {
        ESP_LOGE(TAG, "authentication failed");
        s_running = false;
        return;
    }

    ESP_LOGI(TAG, "authenticated OK");
    ctx->session->startTask();

    auto handler = std::make_shared<cspot::SpircHandler>(ctx);
    handler->subscribeToMercury();
    auto player = std::make_shared<CSpotPlayer>(handler);

    while (s_running) {
        ctx->session->handlePacket();
    }

    handler->disconnect();
    ESP_LOGI(TAG, "cspot stopped");
}

// ── C API ─────────────────────────────────────────────────────────────────────
extern "C" {

void cspot_start(const char* device_name) {
    if (s_running.exchange(true)) {
        ESP_LOGW(TAG, "already running");
        return;
    }
    s_task = std::make_unique<CSpotTask>(device_name ? device_name : "M5Tab5");
}

void cspot_stop(void) {
    s_running = false;
    s_task.reset();
}

bool cspot_is_running(void) {
    return s_running;
}

} // extern "C"
