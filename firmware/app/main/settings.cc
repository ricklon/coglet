#include "settings.h"

#include <esp_mac.h>
#include <esp_random.h>
#include <nvs.h>
#include <sdkconfig.h>
#include <cstdio>

namespace settings {

std::string GetString(const char* ns, const char* key, const std::string& fallback) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return fallback;
    size_t length = 0;
    std::string value = fallback;
    if (nvs_get_str(h, key, nullptr, &length) == ESP_OK && length > 0) {
        value.resize(length);
        if (nvs_get_str(h, key, value.data(), &length) == ESP_OK) {
            value.resize(length - 1);  // drop the terminator
        } else {
            value = fallback;
        }
    }
    nvs_close(h);
    return value;
}

bool SetString(const char* ns, const char* key, const std::string& value) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, key, value.c_str()) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

int GetInt(const char* ns, const char* key, int fallback) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return fallback;
    int32_t value = fallback;
    if (nvs_get_i32(h, key, &value) != ESP_OK) value = fallback;
    nvs_close(h);
    return value;
}

bool SetInt(const char* ns, const char* key, int value) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_i32(h, key, value) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool Erase(const char* ns, const char* key) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_erase_key(h, key);
    bool ok = (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

}  // namespace settings

namespace device {

std::string MacAddress() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char text[18];
    snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return text;
}

std::string ClientId() {
    std::string uuid = settings::GetString("board", "uuid");
    if (!uuid.empty()) return uuid;
    uint8_t b[16];
    esp_fill_random(b, sizeof(b));
    b[6] = (b[6] & 0x0F) | 0x40;  // version 4
    b[8] = (b[8] & 0x3F) | 0x80;  // variant 1
    char text[37];
    snprintf(text, sizeof(text),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    settings::SetString("board", "uuid", text);
    return text;
}

std::string OtaUrl() {
    std::string url = settings::GetString("wifi", "ota_url");
    return url.empty() ? CONFIG_OTA_URL : url;
}

std::string RedactUrl(const std::string& url) {
    const std::string key = "enrollment_token=";
    std::string out = url;
    size_t start = out.find(key);
    if (start == std::string::npos) return out;
    start += key.size();
    size_t end = out.find('&', start);
    out.replace(start, end == std::string::npos ? std::string::npos : end - start, "<redacted>");
    return out;
}

}  // namespace device
