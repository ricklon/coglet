#include "hub.h"

#include "board.h"
#include "mcp.h"
#include "settings.h"
#include "wifi.h"

#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_crt_bundle.h>
#include <esp_flash.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <sdkconfig.h>
#include <sys/time.h>

#define TAG "hub"

namespace hub {
namespace {

std::string Print(cJSON* root) {
    char* text = cJSON_PrintUnformatted(root);
    std::string out(text ? text : "");
    cJSON_free(text);
    cJSON_Delete(root);
    return out;
}

std::string UserAgent() {
    return std::string(BOARD_TYPE "/") + esp_app_get_description()->version;
}

// POSTs a JSON body and returns the HTTP status (or -1), filling response.
int Post(const std::string& url, const std::string& body,
         const std::string& authorization, std::string& response) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) return -1;

    std::string user_agent = UserAgent();
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Device-Id", device::MacAddress().c_str());
    esp_http_client_set_header(client, "Client-Id", device::ClientId().c_str());
    esp_http_client_set_header(client, "User-Agent", user_agent.c_str());
    esp_http_client_set_header(client, "Accept-Language", "en-US");
    esp_http_client_set_header(client, "Activation-Version", "1");
    if (!authorization.empty()) {
        esp_http_client_set_header(client, "Authorization", authorization.c_str());
    }

    int status = -1;
    if (esp_http_client_open(client, body.size()) == ESP_OK &&
        esp_http_client_write(client, body.data(), body.size()) == (int)body.size() &&
        esp_http_client_fetch_headers(client) >= 0) {
        status = esp_http_client_get_status_code(client);
        char buffer[512];
        int n;
        while ((n = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
            response.append(buffer, n);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

std::string String(const cJSON* object, const char* key) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

// The body the fork sent. agent-hub reads application.version and
// board.{type,name,ssid,rssi,channel,ip,mac}; the rest is for compatibility.
std::string CheckInBody() {
    auto app = esp_app_get_description();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "language", "en-US");
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    cJSON_AddNumberToObject(root, "flash_size", flash_size);
    cJSON_AddStringToObject(root, "minimum_free_heap_size",
                            std::to_string(esp_get_minimum_free_heap_size()).c_str());
    cJSON_AddStringToObject(root, "mac_address", device::MacAddress().c_str());
    cJSON_AddStringToObject(root, "uuid", device::ClientId().c_str());
    cJSON_AddStringToObject(root, "chip_model_name", CONFIG_IDF_TARGET);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    cJSON* chip_info = cJSON_AddObjectToObject(root, "chip_info");
    cJSON_AddNumberToObject(chip_info, "model", chip.model);
    cJSON_AddNumberToObject(chip_info, "cores", chip.cores);
    cJSON_AddNumberToObject(chip_info, "revision", chip.revision);
    cJSON_AddNumberToObject(chip_info, "features", chip.features);

    cJSON* application = cJSON_AddObjectToObject(root, "application");
    cJSON_AddStringToObject(application, "name", app->project_name);
    cJSON_AddStringToObject(application, "version", app->version);
    cJSON_AddStringToObject(application, "compile_time",
                            (std::string(app->date) + "T" + app->time + "Z").c_str());
    cJSON_AddStringToObject(application, "idf_version", app->idf_ver);
    char sha[65];
    for (int i = 0; i < 32; i++) snprintf(sha + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    cJSON_AddStringToObject(application, "elf_sha256", sha);

    cJSON* board = cJSON_AddObjectToObject(root, "board");
    cJSON_AddStringToObject(board, "type", BOARD_TYPE);
    cJSON_AddStringToObject(board, "name", BOARD_NAME);
    cJSON_AddItemToObject(board, "mcp", Capabilities());
    cJSON_AddStringToObject(board, "ssid", wifi::Ssid().c_str());
    cJSON_AddNumberToObject(board, "rssi", wifi::Rssi());
    cJSON_AddNumberToObject(board, "channel", wifi::Channel());
    cJSON_AddStringToObject(board, "ip", wifi::Ip().c_str());
    cJSON_AddStringToObject(board, "mac", device::MacAddress().c_str());

    cJSON_AddItemToObject(root, "mcp", Capabilities());
    return Print(root);
}

}  // namespace

cJSON* Capabilities() {
    cJSON* root = cJSON_CreateObject();
    cJSON* board = cJSON_AddObjectToObject(root, "board");
    cJSON_AddStringToObject(board, "type", BOARD_TYPE);
    cJSON_AddStringToObject(board, "name", BOARD_NAME);

    cJSON* features = cJSON_AddObjectToObject(root, "features");
    cJSON_AddBoolToObject(features, "audio_input", true);
    cJSON_AddBoolToObject(features, "audio_output", true);
    cJSON_AddBoolToObject(features, "display", false);
    cJSON_AddBoolToObject(features, "backlight", false);
    cJSON_AddBoolToObject(features, "camera", false);
    cJSON_AddBoolToObject(features, "touch", false);
    cJSON_AddBoolToObject(features, "battery", false);
    cJSON_AddBoolToObject(features, "status_led", false);

    cJSON* firmware = cJSON_AddObjectToObject(root, "firmware");
    cJSON_AddStringToObject(firmware, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(firmware, "default_ota_url", CONFIG_OTA_URL);
    cJSON_AddBoolToObject(firmware, "web_flasher_supported", true);
    cJSON_AddStringToObject(firmware, "board_id", BOARD_TYPE);
    cJSON_AddStringToObject(firmware, "manifest", BOARD_MANIFEST);
    return root;
}

bool CheckIn(Config& config) {
    std::string url = device::OtaUrl();
    std::string response;
    int status = Post(url, CheckInBody(), "", response);
    if (status != 200) {
        ESP_LOGW(TAG, "Check-in at %s failed: HTTP %d", device::RedactUrl(url).c_str(), status);
        return false;
    }
    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Check-in reply isn't JSON");
        return false;
    }

    cJSON* websocket = cJSON_GetObjectItem(root, "websocket");
    config.websocket_url = String(websocket, "url");
    config.websocket_token = String(websocket, "token");

    // agent-hub only sends heartbeat details when it issued a token.
    cJSON* heartbeat = cJSON_GetObjectItem(root, "heartbeat");
    cJSON* enabled = cJSON_GetObjectItem(heartbeat, "enabled");
    cJSON* interval = cJSON_GetObjectItem(heartbeat, "interval");
    config.heartbeat_url = cJSON_IsTrue(enabled) ? String(heartbeat, "url") : "";
    config.heartbeat_token = String(heartbeat, "token");
    config.heartbeat_interval = cJSON_IsNumber(interval) ? std::max(15, interval->valueint) : 60;

    cJSON* server_time = cJSON_GetObjectItem(root, "server_time");
    cJSON* timestamp = cJSON_GetObjectItem(server_time, "timestamp");
    if (cJSON_IsNumber(timestamp)) {
        double ms = timestamp->valuedouble;
        timeval tv = {(time_t)(ms / 1000), (suseconds_t)((long long)ms % 1000) * 1000};
        settimeofday(&tv, nullptr);
    }

    // Firmware updates come from the web flasher, so the firmware section is
    // only reported.
    cJSON* firmware = cJSON_GetObjectItem(root, "firmware");
    std::string offered = String(firmware, "version");
    if (!String(firmware, "url").empty()) {
        ESP_LOGI(TAG, "Hub offers firmware %s; update with the web flasher", offered.c_str());
    }
    cJSON_Delete(root);

    if (config.websocket_url.empty()) {
        ESP_LOGW(TAG, "Check-in reply has no websocket.url");
        return false;
    }
    ESP_LOGI(TAG, "Checked in; voice session at %s", config.websocket_url.c_str());
    return true;
}

bool Heartbeat(const Config& config, const char* activity) {
    if (config.heartbeat_url.empty() || config.heartbeat_token.empty()) return false;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "health", "healthy");
    cJSON_AddStringToObject(root, "activity", activity);
    cJSON_AddNumberToObject(root, "uptime_seconds", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "minimum_free_heap", esp_get_minimum_free_heap_size());
    cJSON* tools = cJSON_AddArrayToObject(root, "mcp_tools");
    for (const auto& name : mcp::ToolNames()) {
        cJSON_AddItemToArray(tools, cJSON_CreateString(name.c_str()));
    }
    std::string response;
    int status = Post(config.heartbeat_url, Print(root), "Bearer " + config.heartbeat_token, response);
    if (status != 200) {
        ESP_LOGW(TAG, "Heartbeat failed: HTTP %d", status);
        return false;
    }
    return true;
}

}  // namespace hub
