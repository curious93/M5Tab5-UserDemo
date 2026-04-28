#include "CSpotTask.h"
#include "cspot_idf.h"
#include "cspot_ui_state.h"

#include <CSpotContext.h>
#include <LoginBlob.h>
#include <SpircHandler.h>
#include <TrackPlayer.h>
#include <BellHTTPServer.h>
#include <MDNSService.h>
#include <Logger.h>
#include <BellUtils.h>

// internal hook from cspot_ui_state.cpp
namespace cspot_ui_internal { void register_handler(cspot::SpircHandler*); }

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <driver/i2s_types.h>
#include <bsp/m5stack_tab5.h>
#include <mdns.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <nlohmann/json.hpp>

// Host-generated: non-empty if host has a backed-up blob (survives NVS wipe).
#include "generated_blob.h"

namespace {
constexpr const char* NVS_NS  = "cspot";
constexpr const char* NVS_KEY = "blob_json";

bool loadStoredBlob(std::string& out) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, NVS_KEY, nullptr, &len);
    if (err != ESP_OK || len == 0) { nvs_close(h); return false; }
    out.resize(len);
    err = nvs_get_str(h, NVS_KEY, out.data(), &len);
    nvs_close(h);
    if (err != ESP_OK) return false;
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return true;
}

void saveStoredBlob(const std::string& json) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY, json.c_str());
    nvs_commit(h);
    nvs_close(h);
}

void eraseStoredBlob() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, NVS_KEY);
    nvs_commit(h);
    nvs_close(h);
}
}  // namespace

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
    // Tab5's I2S out channel is bound to the mic channel at 48 kHz (TDM pair).
    // Reconfiguring to 44.1 kHz fails with "Mode 1 conflict", leaving i2s_write
    // to block forever on portMAX_DELAY. Run the DAC at 48 kHz and upsample
    // Spotify's 44.1 kHz Vorbis output in feedPCMFrames (160/147).
    _sink->setParams(48000, 2, 16);
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
                case cspot::SpircHandler::EventType::PLAY_PAUSE: {
                    bool paused = std::get<bool>(ev->data);
                    ESP_LOGI(TAG, "event PLAY_PAUSE paused=%d", paused ? 1 : 0);
                    _paused = paused;
                    cspot_ui_state_set_paused(paused);
                    break;
                }
                case cspot::SpircHandler::EventType::TRACK_INFO: {
                    auto& ti = std::get<cspot::TrackInfo>(ev->data);
                    ESP_LOGI(TAG, "event TRACK_INFO: '%s' / '%s' (%u ms)",
                             ti.name.c_str(), ti.artist.c_str(),
                             (unsigned)ti.duration);
                    cspot_ui_state_set_track(ti.name.c_str(), ti.artist.c_str(),
                                             ti.album.c_str(), ti.imageUrl.c_str(),
                                             ti.duration);
                    cspot_ui_state_refresh_wifi();
                    break;
                }
                case cspot::SpircHandler::EventType::VOLUME: {
                    int v = std::get<int>(ev->data);
                    uint8_t pct = (uint8_t)((v * 100 + 32767) / 65535);
                    cspot_ui_state_set_volume(pct);
                    break;
                }
                case cspot::SpircHandler::EventType::FLUSH:
                case cspot::SpircHandler::EventType::SEEK:
                    ESP_LOGI(TAG, "event FLUSH/SEEK — emptyBuffer");
                    _buf->emptyBuffer();
                    if (ev->eventType == cspot::SpircHandler::EventType::SEEK) {
                        cspot_ui_state_set_position(std::get<int>(ev->data));
                    }
                    break;
                case cspot::SpircHandler::EventType::PLAYBACK_START:
                    // Do NOT emptyBuffer here: PLAYBACK_START fires at the
                    // start of every new track via trackLoadedCallback, which
                    // happens *after* the previous track's Vorbis decode has
                    // finished and its remaining PCM (~up to 512 KB, ~2.7 s at
                    // 48 kHz stereo) is still sitting in the circular buffer
                    // waiting to be drained to the DAC. Flushing here throws
                    // that tail away, so each track becomes audible for only
                    // the ~350 ms that reached the DMA before EOF.
                    //
                    // Always resume output: if trackLoadedCallback fires paused=1
                    // (e.g. because natural EOF set state=Paused before REPLACE
                    // arrived), the new track would silently decode into the buffer
                    // but never reach the DAC.  A skip/next always means play.
                    ESP_LOGI(TAG, "event PLAYBACK_START — keep buffer, unpause");
                    _paused = false;
                    cspot_ui_state_set_paused(false);
                    cspot_ui_state_set_position(0);
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
    // (Removed the cspot-runtime self-test beep — the pre-cspot Tremor
    // self-test already proves codec health at this point, no second beep.)
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
    : bell::Task("cspot_main", 64 * 1024, 4, 1)
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

    // Credential priority: 1) host-embedded blob (survives factory reset) →
    //                      2) NVS-stored blob → 3) ZeroConf (iPhone tap)
    std::string storedJson;
    bool loaded = false;
    const char* embedded = CSPOT_EMBEDDED_BLOB_JSON;
    if (embedded && embedded[0] != '\0') {
        try {
            blob->loadJson(embedded);
            loaded = true;
            ESP_LOGI(TAG, "loaded embedded credentials for user '%s' (host backup)",
                     blob->getUserName().c_str());
        } catch (...) {
            ESP_LOGW(TAG, "embedded credentials invalid, falling back to NVS");
        }
    }
    if (!loaded && loadStoredBlob(storedJson)) {
        try {
            blob->loadJson(storedJson);
            loaded = true;
            ESP_LOGI(TAG, "loaded NVS credentials for user '%s'", blob->getUserName().c_str());
        } catch (...) {
            ESP_LOGW(TAG, "NVS credentials invalid, clearing");
            eraseStoredBlob();
        }
    }
    if (loaded) gotBlob = true;

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

    // Wait for WiFi STA to have an IP before touching the network.
    // (When NVS-stored creds short-circuit the ZeroConf wait, we may reach here
    // before STA is up.)
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip{};
    for (int i = 0; i < 60; ++i) {
        if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) break;
        BELL_SLEEP_MS(500);
    }
    if (ip.ip.addr == 0) {
        ESP_LOGE(TAG, "no IP after 30s, aborting cspot");
        s_running = false;
        return;
    }
    ESP_LOGI(TAG, "STA ready, ip=" IPSTR, IP2STR(&ip.ip));

    // After IP assignment DNS/routing may still be settling. Give it a moment.
    BELL_SLEEP_MS(2000);

    auto ctx = cspot::Context::createFromBlob(blob);

    // Retry AP connection on transient network failures
    bool ap_ok = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        try {
            ctx->session->connectWithRandomAp();
            ap_ok = true;
            break;
        } catch (std::exception& e) {
            ESP_LOGW(TAG, "AP connect attempt %d failed: %s, retrying", attempt+1, e.what());
            BELL_SLEEP_MS(2000);
        }
    }
    if (!ap_ok) {
        ESP_LOGE(TAG, "could not reach any Spotify AP, aborting");
        s_running = false;
        return;
    }

    std::vector<uint8_t> token;
    try {
        token = ctx->session->authenticate(blob);
    } catch (std::exception& e) {
        ESP_LOGE(TAG, "authenticate threw: %s", e.what());
        eraseStoredBlob();
        s_running = false;
        return;
    }

    if (token.empty()) {
        ESP_LOGE(TAG, "authentication failed");
        // Wipe stored blob so we fall back to ZeroConf on next boot
        eraseStoredBlob();
        s_running = false;
        return;
    }

    ESP_LOGI(TAG, "authenticated OK");

    // Persist reusable credentials. NVS = device-local, + serial-log with
    // CSPOT_BLOB_BEGIN/END markers so the host scraper hook can back up
    // the blob to ~/.claude/tab5/cspot_blob.json and .secrets/cspot_blob.json.
    try {
        blob->authData = token;
        blob->authType = 1;  // AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS
        std::string j = blob->toJson();
        saveStoredBlob(j);
        ESP_LOGI(TAG, "stored reusable credentials to NVS");
        ESP_LOGI(TAG, "CSPOT_BLOB_BEGIN %s CSPOT_BLOB_END", j.c_str());
    } catch (...) {
        ESP_LOGW(TAG, "failed to persist credentials");
    }
    ctx->session->startTask();

    auto handler = std::make_shared<cspot::SpircHandler>(ctx);
    handler->subscribeToMercury();
    auto player = std::make_shared<CSpotPlayer>(handler);
    cspot_ui_internal::register_handler(handler.get());

    while (s_running) {
        ctx->session->handlePacket();
    }

    cspot_ui_internal::register_handler(nullptr);
    handler->disconnect();
    ESP_LOGI(TAG, "cspot stopped");
}

// ── C API ─────────────────────────────────────────────────────────────────────
extern "C" {

void cspot_start(const char* device_name) {
    cspot_ui_state_init();   // safe to call repeatedly; idempotent
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
