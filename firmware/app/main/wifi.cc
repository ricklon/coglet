#include "wifi.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <nvs.h>
#include <atomic>
#include <cstring>
#include <mutex>

#define TAG "wifi"

namespace wifi {
namespace {

std::function<void()> s_on_up, s_on_down;
esp_netif_t* s_netif = nullptr;
TimerHandle_t s_retry = nullptr;
std::atomic<bool> s_connected{false};
std::mutex s_mutex;  // guards s_ssid
std::string s_ssid;

std::string Key(const char* base, int i) {
    return i == 0 ? std::string(base) : std::string(base) + std::to_string(i);
}

void Save(const std::vector<Network>& list) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) return;
    for (int i = 0; i < kMaxNetworks; i++) {
        if (i < (int)list.size()) {
            nvs_set_str(h, Key("ssid", i).c_str(), list[i].ssid.c_str());
            nvs_set_str(h, Key("password", i).c_str(), list[i].password.c_str());
        } else {
            nvs_erase_key(h, Key("ssid", i).c_str());
            nvs_erase_key(h, Key("password", i).c_str());
        }
    }
    nvs_commit(h);
    nvs_close(h);
}

void StartScan() {
    if (Saved().empty()) {
        ESP_LOGW(TAG, "No saved networks. Add one with: !wifi SSID PASSWORD");
        return;
    }
    wifi_scan_config_t config = {};
    config.show_hidden = true;
    if (esp_wifi_scan_start(&config, false) != ESP_OK) {
        xTimerStart(s_retry, 0);
    }
}

// Joins the strongest visible saved network. Hidden networks don't show up in
// a scan, so if none is visible, try the first saved one blind.
void OnScanDone() {
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    std::vector<wifi_ap_record_t> records(count);
    if (count > 0) esp_wifi_scan_get_ap_records(&count, records.data());
    auto saved = Saved();
    const Network* best = nullptr;
    int best_rssi = -1000;
    for (const auto& ap : records) {
        for (const auto& net : saved) {
            if (net.ssid == (const char*)ap.ssid && ap.rssi > best_rssi) {
                best = &net;
                best_rssi = ap.rssi;
            }
        }
    }
    if (best == nullptr && !saved.empty()) best = &saved[0];
    if (best == nullptr) return;

    wifi_config_t config = {};
    strncpy((char*)config.sta.ssid, best->ssid.c_str(), sizeof(config.sta.ssid));
    strncpy((char*)config.sta.password, best->password.c_str(), sizeof(config.sta.password));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_ssid = best->ssid;
    }
    ESP_LOGI(TAG, "Joining %s (rssi %d)", best->ssid.c_str(), best_rssi);
    esp_wifi_set_config(WIFI_IF_STA, &config);
    esp_wifi_connect();
}

void OnEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        StartScan();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        OnScanDone();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        bool was_connected = s_connected.exchange(false);
        if (was_connected && s_on_down) s_on_down();
        xTimerStart(s_retry, 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        esp_wifi_set_ps(WIFI_PS_NONE);  // audio streaming wants low latency
        if (s_on_up) s_on_up();
    }
}

}  // namespace

std::vector<Network> Saved() {
    std::vector<Network> list;
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return list;
    for (int i = 0; i < kMaxNetworks; i++) {
        char ssid[33];
        char password[65];
        size_t length = sizeof(ssid);
        if (nvs_get_str(h, Key("ssid", i).c_str(), ssid, &length) != ESP_OK) continue;
        length = sizeof(password);
        if (nvs_get_str(h, Key("password", i).c_str(), password, &length) != ESP_OK) continue;
        list.push_back({ssid, password});
    }
    nvs_close(h);
    return list;
}

bool Add(const std::string& ssid, const std::string& password) {
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64) return false;
    auto list = Saved();
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].ssid == ssid) {
            list.erase(list.begin() + i);
            break;
        }
    }
    list.insert(list.begin(), {ssid, password});
    if ((int)list.size() > kMaxNetworks) list.resize(kMaxNetworks);
    Save(list);
    return true;
}

bool Remove(int index) {
    auto list = Saved();
    if (index < 0 || index >= (int)list.size()) return false;
    list.erase(list.begin() + index);
    Save(list);
    return true;
}

void Clear() {
    Save({});
}

void Start(std::function<void()> on_up, std::function<void()> on_down) {
    s_on_up = std::move(on_up);
    s_on_down = std::move(on_down);
    s_retry = xTimerCreate("wifi_retry", pdMS_TO_TICKS(5000), pdFALSE, nullptr,
                           [](TimerHandle_t) { StartScan(); });

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, "coglet");

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, OnEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, OnEvent, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void Rescan() {
    if (s_connected) {
        esp_wifi_disconnect();  // the disconnect event schedules the scan
    } else {
        StartScan();
    }
}

bool Connected() {
    return s_connected;
}

std::string Ssid() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_connected ? s_ssid : "";
}

std::string Ip() {
    esp_netif_ip_info_t info = {};
    if (!s_connected || esp_netif_get_ip_info(s_netif, &info) != ESP_OK) return "";
    char text[16];
    snprintf(text, sizeof(text), IPSTR, IP2STR(&info.ip));
    return text;
}

int Rssi() {
    wifi_ap_record_t ap = {};
    return s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

int Channel() {
    wifi_ap_record_t ap = {};
    return s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.primary : 0;
}

}  // namespace wifi
