#include "session.h"

#include "audio.h"
#include "hub.h"
#include "mcp.h"
#include "settings.h"
#include "wifi.h"
#include "coglet_controller.h"

#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>

#define TAG "session"

namespace session {
namespace {

constexpr int kTickMs = 100;
constexpr int64_t kHelloTimeoutUs = 10 * 1000000LL;
// The client pings every 10 s and the hub's pongs count as traffic, so this
// only fires on a dead link.
constexpr int64_t kSilenceTimeoutUs = 60 * 1000000LL;

using Work = std::function<void()>;
QueueHandle_t s_queue = nullptr;

// Owned by the session task.
hub::Config s_hub;
std::atomic<bool> s_checked_in{false};
int s_checkin_failures = 0;
int s_open_failures = 0;
int64_t s_checkin_at = 0;   // when to retry check-in, 0 = not scheduled
int64_t s_open_at = 0;      // when to open the session, 0 = not scheduled
int64_t s_hello_deadline = 0;
bool s_reached_listening = false;
bool s_draining = false;    // tts stop seen; waiting for the speaker to finish
bool s_builder_quieted = false;
std::string s_session_id;

// Shared with the WebSocket, audio and console tasks.
std::atomic<State> s_state{State::Offline};
std::atomic<bool> s_quiet{false};
std::atomic<bool> s_accept_audio{false};
std::atomic<int64_t> s_last_rx{0};
std::atomic<int> s_generation{0};
std::mutex s_ws_mutex;  // guards s_ws against send-during-destroy
esp_websocket_client_handle_t s_ws = nullptr;
std::string s_rx_text;  // WebSocket task only: reassembles fragmented text
std::mutex s_hub_mutex;  // guards s_hub for the heartbeat task and console

void OnWebsocketEvent(int gen, int32_t id, esp_websocket_event_data_t* event);

void Post(Work work) {
    auto* item = new Work(std::move(work));
    // Never block: the WebSocket task posts here, and the session task may be
    // waiting for that task to exit.
    if (xQueueSend(s_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Session queue full; dropping work");
        delete item;
    }
}

void SetState(State state) {
    State old = s_state.exchange(state);
    if (old != state) ESP_LOGI(TAG, "%s -> %s", StateName(old), StateName(state));
}

int64_t Now() {
    return esp_timer_get_time();
}

bool SendText(const std::string& text) {
    std::lock_guard<std::mutex> lock(s_ws_mutex);
    if (s_ws == nullptr || !esp_websocket_client_is_connected(s_ws)) return false;
    return esp_websocket_client_send_text(s_ws, text.data(), text.size(), pdMS_TO_TICKS(2000)) >= 0;
}

void SendAudio(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(s_ws_mutex);
    if (s_ws == nullptr || !esp_websocket_client_is_connected(s_ws)) return;
    esp_websocket_client_send_bin(s_ws, (const char*)data, size, pdMS_TO_TICKS(500));
}

void SendJson(cJSON* root) {
    char* text = cJSON_PrintUnformatted(root);
    if (text != nullptr) {
        SendText(text);
        cJSON_free(text);
    }
    cJSON_Delete(root);
}

cJSON* Message(const char* type) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", s_session_id.c_str());
    cJSON_AddStringToObject(root, "type", type);
    return root;
}

void SendListen(const char* state) {
    cJSON* root = Message("listen");
    cJSON_AddStringToObject(root, "state", state);
    if (strcmp(state, "start") == 0) cJSON_AddStringToObject(root, "mode", "auto");
    SendJson(root);
}

void SendHello() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON* features = cJSON_AddObjectToObject(root, "features");
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON* params = cJSON_AddObjectToObject(root, "audio_params");
    cJSON_AddStringToObject(params, "format", "opus");
    cJSON_AddNumberToObject(params, "sample_rate", 16000);
    cJSON_AddNumberToObject(params, "channels", 1);
    cJSON_AddNumberToObject(params, "frame_duration", audio::kMicFrameMs);
    SendJson(root);
}

// Listening again after a reply, or for the first time after hello.
void StartListening() {
    s_draining = false;
    s_reached_listening = true;
    s_open_failures = 0;
    SetState(State::Listening);
    SendListen("start");
    audio::SetMicEnabled(true);
}

void Teardown() {
    s_generation++;
    s_accept_audio = false;
    audio::SetMicEnabled(false);
    audio::FlushPlayback();
    esp_websocket_client_handle_t ws;
    {
        std::lock_guard<std::mutex> lock(s_ws_mutex);
        ws = s_ws;
        s_ws = nullptr;
    }
    if (ws != nullptr) esp_websocket_client_destroy(ws);
    s_session_id.clear();
    s_draining = false;
    s_hello_deadline = 0;
}

void ScheduleOpen(int64_t delay_us) {
    s_open_at = Now() + delay_us;
}

void OnSessionEnded() {
    Teardown();
    if (s_state == State::Paused || s_state == State::Offline) return;
    SetState(State::Idle);
    if (!s_reached_listening) {
        s_open_failures++;
        // A hub restart invalidates the session token: check in again.
        if (s_open_failures >= 3) {
            s_checked_in = false;
            s_checkin_at = Now();
            return;
        }
    }
    int64_t delay = 1000000LL << std::min(s_open_failures, 5);  // 1 s .. 32 s
    ScheduleOpen(delay);
}

void Open() {
    if (s_state != State::Idle || s_quiet || !s_checked_in) return;
    SetState(State::Connecting);
    s_reached_listening = false;

    std::string headers;
    if (!s_hub.websocket_token.empty()) {
        headers += "Authorization: Bearer " + s_hub.websocket_token + "\r\n";
    }
    headers += "Protocol-Version: 1\r\n";
    headers += "Device-Id: " + device::MacAddress() + "\r\n";
    headers += "Client-Id: " + device::ClientId() + "\r\n";

    esp_websocket_client_config_t config = {};
    config.uri = s_hub.websocket_url.c_str();
    config.headers = headers.c_str();
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_reconnect = true;
    config.buffer_size = 2048;
    config.task_stack = 6144;
    config.network_timeout_ms = 10000;
    config.ping_interval_sec = 10;
    config.pingpong_timeout_sec = 30;  // the client drops a link that stops answering

    esp_websocket_client_handle_t ws = esp_websocket_client_init(&config);
    if (ws == nullptr) {
        ESP_LOGE(TAG, "WebSocket init failed");
        OnSessionEnded();
        return;
    }
    int generation = ++s_generation;
    esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY,
        [](void* arg, esp_event_base_t, int32_t id, void* data) {
            int gen = (int)(intptr_t)arg;
            if (gen != s_generation) return;
            OnWebsocketEvent(gen, id, (esp_websocket_event_data_t*)data);
        }, (void*)(intptr_t)generation);
    {
        std::lock_guard<std::mutex> lock(s_ws_mutex);
        s_ws = ws;
    }
    s_last_rx = Now();
    s_hello_deadline = Now() + kHelloTimeoutUs;
    ESP_LOGI(TAG, "Opening %s", s_hub.websocket_url.c_str());
    if (esp_websocket_client_start(ws) != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket start failed");
        OnSessionEnded();
    }
}

void OnServerHello(const std::string& session_id, int frame_ms) {
    if (s_state != State::Connecting) return;
    s_session_id = session_id;
    s_hello_deadline = 0;
    if (frame_ms > 0) audio::SetDownlinkFrameMs(frame_ms);
    ESP_LOGI(TAG, "Session %s open", session_id.c_str());
    StartListening();
}

void OnTts(const std::string& state, const std::string& text) {
    if (state == "start") {
        if (s_state != State::Listening && s_state != State::Speaking) return;
        audio::SetMicEnabled(false);
        s_draining = false;
        SetState(State::Speaking);
    } else if (state == "stop") {
        if (s_state == State::Speaking) s_draining = true;
    } else if (state == "sentence_start" && !text.empty()) {
        ESP_LOGI(TAG, "<< %s", text.c_str());
    }
}

void OnMcp(cJSON* payload) {
    mcp::Handle(payload, [](cJSON* reply) {
        cJSON* root = Message("mcp");
        cJSON_AddItemToObject(root, "payload", reply);
        SendJson(root);
    });
    cJSON_Delete(payload);
}

// Runs on the WebSocket task. Audio goes straight to the decoder queue; the
// rest is handed to the session task.
void OnText(int gen, const std::string& text) {
    cJSON* root = cJSON_Parse(text.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Unparseable message: %.80s", text.c_str());
        return;
    }
    cJSON* type_item = cJSON_GetObjectItem(root, "type");
    std::string type = cJSON_IsString(type_item) ? type_item->valuestring : "";
    cJSON* state_item = cJSON_GetObjectItem(root, "state");
    std::string state = cJSON_IsString(state_item) ? state_item->valuestring : "";
    cJSON* text_item = cJSON_GetObjectItem(root, "text");
    std::string said = cJSON_IsString(text_item) ? text_item->valuestring : "";

    if (type == "hello") {
        cJSON* transport = cJSON_GetObjectItem(root, "transport");
        cJSON* session_id = cJSON_GetObjectItem(root, "session_id");
        cJSON* params = cJSON_GetObjectItem(root, "audio_params");
        cJSON* frame = cJSON_GetObjectItem(params, "frame_duration");
        if (!cJSON_IsString(transport) || strcmp(transport->valuestring, "websocket") != 0) {
            ESP_LOGE(TAG, "Hub hello without websocket transport");
        } else {
            std::string id = cJSON_IsString(session_id) ? session_id->valuestring : "";
            int ms = cJSON_IsNumber(frame) ? frame->valueint : 0;
            Post([gen, id, ms] { if (gen == s_generation) OnServerHello(id, ms); });
        }
    } else if (type == "tts") {
        // Gate audio here, not on the session task, so the first packets of a
        // reply aren't dropped while the state change is still queued.
        if (state == "start") s_accept_audio = !s_quiet;
        if (state == "stop") s_accept_audio = false;
        Post([gen, state, said] { if (gen == s_generation) OnTts(state, said); });
    } else if (type == "stt") {
        ESP_LOGI(TAG, ">> %s", said.c_str());
    } else if (type == "mcp") {
        cJSON* payload = cJSON_GetObjectItem(root, "payload");
        if (cJSON_IsObject(payload)) {
            cJSON* copy = cJSON_Duplicate(payload, true);
            Post([gen, copy] {
                if (gen == s_generation) OnMcp(copy); else cJSON_Delete(copy);
            });
        }
    } else if (type == "system") {
        cJSON* command = cJSON_GetObjectItem(root, "command");
        if (cJSON_IsString(command) && strcmp(command->valuestring, "reboot") == 0) {
            Post([] { esp_restart(); });
        }
    } else if (type == "alert") {
        cJSON* message = cJSON_GetObjectItem(root, "message");
        ESP_LOGW(TAG, "Hub alert: %s", cJSON_IsString(message) ? message->valuestring : "");
    } else if (type != "llm") {
        ESP_LOGD(TAG, "Ignoring %s message", type.c_str());
    }
    cJSON_Delete(root);
}

void OnWebsocketEvent(int gen, int32_t id, esp_websocket_event_data_t* event) {
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            Post([gen] {
                if (gen == s_generation && s_state == State::Connecting) SendHello();
            });
            break;
        case WEBSOCKET_EVENT_DATA:
            s_last_rx = Now();
            if (event->op_code == 0x2) {
                // Whole packets only: the hub's 20 ms Opus frames are far
                // smaller than the receive buffer.
                if (s_accept_audio && event->payload_offset == 0 &&
                    event->data_len == event->payload_len) {
                    audio::PushOpus((const uint8_t*)event->data_ptr, event->data_len);
                }
            } else if (event->op_code == 0x1 || (event->op_code == 0x0 && !s_rx_text.empty())) {
                if (event->payload_offset == 0) s_rx_text.clear();
                s_rx_text.append(event->data_ptr, event->data_len);
                if (event->payload_offset + event->data_len >= event->payload_len && event->fin) {
                    std::string text;
                    text.swap(s_rx_text);
                    OnText(gen, text);
                }
            }
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
        case WEBSOCKET_EVENT_ERROR:
            s_accept_audio = false;
            Post([gen] {
                if (gen != s_generation) return;
                ESP_LOGI(TAG, "Session closed");
                OnSessionEnded();
            });
            break;
        default:
            break;
    }
}

void TryCheckIn() {
    s_checkin_at = 0;
    if (!wifi::Connected()) return;
    hub::Config config;
    bool ok = hub::CheckIn(config);
    if (ok) {
        std::lock_guard<std::mutex> lock(s_hub_mutex);
        s_hub = config;
    }
    if (ok) {
        s_checked_in = true;
        s_checkin_failures = 0;
        s_open_failures = 0;
        if (s_state == State::Offline) SetState(s_quiet ? State::Paused : State::Idle);
        ScheduleOpen(0);
    } else {
        s_checkin_failures++;
        int64_t delay = std::min<int64_t>(60, 5LL << std::min(s_checkin_failures - 1, 4));
        ESP_LOGW(TAG, "Check-in retry in %lld s", delay);
        s_checkin_at = Now() + delay * 1000000LL;
    }
}

const char* Activity() {
    switch (s_state.load()) {
        case State::Paused: return "paused";
        case State::Listening: return "listening";
        case State::Speaking: return "speaking";
        case State::Connecting: return "thinking";
        default: return "idle";
    }
}

void Tick() {
    int64_t now = Now();

    // Builder mode silences the voice agent. Leaving it does NOT bring the
    // voice back: the LLM can call self.coglet.release, and an auto-resume let
    // the robot talk its own way out of a calibration session. Quiet ends only
    // when a person says so, with !quiet off.
    if (CogletController::Instance().BuilderMode() && !s_builder_quieted) {
        s_builder_quieted = true;
        ESP_LOGI(TAG, "Builder mode: voice paused until !quiet off");
        SetQuiet(true);
    } else if (s_builder_quieted && !s_quiet) {
        s_builder_quieted = false;
    }

    if (!s_checked_in && s_checkin_at != 0 && now >= s_checkin_at) TryCheckIn();

    State state = s_state;
    if (state == State::Idle && s_open_at != 0 && now >= s_open_at) {
        s_open_at = 0;
        Open();
    }
    if (state == State::Connecting && s_hello_deadline != 0 && now >= s_hello_deadline) {
        ESP_LOGW(TAG, "No hello from the hub within 10 s");
        OnSessionEnded();
    }
    if ((state == State::Listening || state == State::Speaking) &&
        now - s_last_rx > kSilenceTimeoutUs) {
        ESP_LOGW(TAG, "Nothing from the hub for 60 s; reopening");
        OnSessionEnded();
    }
    // Reopen the mic only once the reply has finished playing, or it hears
    // its own voice.
    if (state == State::Speaking && s_draining && audio::PlaybackIdle()) {
        StartListening();
    }
}

void HeartbeatTask(void*) {
    // The first beat registers the device for liveness; after that, one per
    // interval the hub asked for.
    while (true) {
        if (s_checked_in && wifi::Connected()) {
            hub::Config config;
            {
                std::lock_guard<std::mutex> lock(s_hub_mutex);
                config = s_hub;  // a copy: check-in may rewrite it
            }
            hub::Heartbeat(config, Activity());
            vTaskDelay(pdMS_TO_TICKS(config.heartbeat_interval * 1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

void SessionTask(void*) {
    audio::Start(SendAudio);
    xTaskCreate(HeartbeatTask, "heartbeat", 8192, nullptr, 2, nullptr);
    int64_t next_tick = Now();
    while (true) {
        Work* item = nullptr;
        TickType_t wait = pdMS_TO_TICKS(std::max<int64_t>(0, (next_tick - Now()) / 1000));
        if (xQueueReceive(s_queue, &item, wait) == pdTRUE) {
            (*item)();
            delete item;
        }
        if (Now() >= next_tick) {
            Tick();
            next_tick = Now() + kTickMs * 1000;
        }
    }
}

}  // namespace

void Start() {
    s_queue = xQueueCreate(32, sizeof(Work*));
    xTaskCreate(SessionTask, "session", 10240, nullptr, 5, nullptr);
}

void NetworkUp() {
    Post([] {
        if (!s_checked_in) {
            TryCheckIn();
        } else if (s_state == State::Offline) {
            SetState(s_quiet ? State::Paused : State::Idle);
            ScheduleOpen(0);
        }
    });
}

void NetworkDown() {
    Post([] {
        Teardown();
        SetState(State::Offline);
    });
}

void SetQuiet(bool on) {
    s_quiet = on;
    if (on) s_accept_audio = false;
    Post([on] {
        if (on) {
            if (s_state == State::Offline) return;
            if (s_state == State::Listening) SendListen("stop");
            Teardown();
            SetState(State::Paused);
        } else if (s_state == State::Paused) {
            SetState(State::Idle);
            s_open_failures = 0;
            ScheduleOpen(0);
        }
    });
}

bool Quiet() {
    return s_quiet;
}

void Interrupt() {
    Post([] {
        if (s_state == State::Speaking) {
            ESP_LOGI(TAG, "Reply interrupted");
            s_accept_audio = false;
            SendJson(Message("abort"));
            audio::FlushPlayback();
            StartListening();
        } else if (s_state == State::Paused) {
            ESP_LOGI(TAG, "Quiet mode is on; say !quiet off to resume");
        }
    });
}

void Restart() {
    Post([] {
        if (s_ws != nullptr) {
            if (s_state == State::Listening) SendListen("stop");
            s_reached_listening = true;  // a requested close isn't a failure
            OnSessionEnded();
        }
    });
}

State GetState() {
    return s_state;
}

const char* StateName(State state) {
    switch (state) {
        case State::Offline: return "offline";
        case State::Idle: return "idle";
        case State::Connecting: return "connecting";
        case State::Listening: return "listening";
        case State::Speaking: return "speaking";
        case State::Paused: return "quiet";
    }
    return "?";
}

bool CheckedIn() {
    return s_checked_in;
}

std::string WebsocketUrl() {
    std::lock_guard<std::mutex> lock(s_hub_mutex);
    return s_hub.websocket_url;
}

}  // namespace session
