#include "console.h"

#include "audio.h"
#include "board.h"
#include "session.h"
#include "settings.h"
#include "wifi.h"
#include "coglet_controller.h"

#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace console {
namespace {

constexpr int kServerHistoryMax = 5;

bool Is(const char* line, const char* command, const char** args) {
    size_t n = strlen(command);
    if (strncmp(line, command, n) != 0 || (line[n] != ' ' && line[n] != '\0')) return false;
    *args = line[n] == ' ' ? line + n + 1 : "";
    return true;
}

// Recent servers, newline-separated, most recent first (the fork's srv_hist).
std::vector<std::string> ServerHistory() {
    std::string blob = settings::GetString("wifi", "srv_hist");
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t nl = blob.find('\n', pos);
        std::string item = blob.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!item.empty()) out.push_back(item);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}

void RememberServer(const std::string& url) {
    auto history = ServerHistory();
    for (size_t i = 0; i < history.size(); i++) {
        if (history[i] == url) {
            history.erase(history.begin() + i);
            break;
        }
    }
    history.insert(history.begin(), url);
    if ((int)history.size() > kServerHistoryMax) history.resize(kServerHistoryMax);
    std::string blob;
    for (const auto& item : history) blob += item + "\n";
    settings::SetString("wifi", "srv_hist", blob);
}

// A bare host or host:port keeps the current path. It drops the query string:
// an enrollment token belongs to the hub that issued it, and carrying it to
// another hub makes every check-in fail with 401 while the URL looks right.
std::string HostToUrl(const std::string& arg) {
    std::string base = device::OtaUrl();
    std::string host = arg, port;
    size_t colon = arg.find(':');
    if (colon != std::string::npos) {
        host = arg.substr(0, colon);
        port = arg.substr(colon);
    }
    size_t scheme = base.find("://");
    size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    size_t end = base.find('/', start);
    std::string authority = base.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string path = end == std::string::npos ? "" : base.substr(end);
    size_t query = path.find('?');
    if (query != std::string::npos) {
        path = path.substr(0, query);
        printf("Dropped the previous URL's query string: tokens belong to the hub that issued them.\n");
    }
    if (path.empty() || path == "/") path = "/xiaozhi/ota/";
    if (port.empty()) {
        size_t c = authority.find(':');
        port = c != std::string::npos ? authority.substr(c) : ":8003";
    }
    return "http://" + host + port + path;
}

void Server(const char* args) {
    auto history = ServerHistory();
    if (*args == '\0') {
        printf("OTA URL: %s\n", device::RedactUrl(device::OtaUrl()).c_str());
        printf("Usage: !server IP | !server URL | !server list | !server N\n");
        return;
    }
    if (strcmp(args, "list") == 0) {
        if (history.empty()) printf("No recent servers.\n");
        for (size_t i = 0; i < history.size(); i++) {
            printf("  [%d] %s\n", (int)i + 1, device::RedactUrl(history[i]).c_str());
        }
        return;
    }
    std::string url = args;
    bool index = true;
    for (char c : url) index = index && isdigit((unsigned char)c);
    if (index) {
        int n = atoi(args);
        if (n < 1 || n > (int)history.size()) {
            printf("No recent server #%d. Try: !server list\n", n);
            return;
        }
        url = history[n - 1];
    } else if (url.rfind("http", 0) != 0) {
        url = HostToUrl(url);
    }
    settings::SetString("wifi", "ota_url", url);
    RememberServer(url);
    printf("Server set to %s. Rebooting.\n", device::RedactUrl(url).c_str());
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void Wifi(const char* args) {
    if (*args == '\0' || strcmp(args, "list") == 0) {
        auto saved = wifi::Saved();
        if (saved.empty()) printf("No saved networks. Add one: !wifi SSID PASSWORD\n");
        for (size_t i = 0; i < saved.size(); i++) printf("  [%d] %s\n", (int)i + 1, saved[i].ssid.c_str());
        return;
    }
    if (strcmp(args, "clear") == 0) {
        wifi::Clear();
        printf("Saved networks cleared.\n");
        return;
    }
    const char* rest;
    if (Is(args, "remove", &rest)) {
        int n = atoi(rest);
        printf(wifi::Remove(n - 1) ? "Removed network #%d.\n" : "No saved network #%d.\n", n);
        return;
    }
    // SSID and password separated by the last space, so SSIDs may contain spaces.
    std::string text = args;
    size_t space = text.rfind(' ');
    std::string ssid = space == std::string::npos ? text : text.substr(0, space);
    std::string password = space == std::string::npos ? "" : text.substr(space + 1);
    if (!wifi::Add(ssid, password)) {
        printf("SSID must be 1-32 bytes and the password at most 64.\n");
        return;
    }
    printf("Saved %s. Connecting.\n", ssid.c_str());
    wifi::Rescan();
}

void Status() {
    printf("Firmware : %s (%s)\n", esp_app_get_description()->version, BOARD_NAME);
    printf("Wi-Fi    : %s %s rssi %d\n", wifi::Connected() ? wifi::Ssid().c_str() : "(not connected)",
           wifi::Ip().c_str(), wifi::Rssi());
    printf("OTA URL  : %s\n", device::RedactUrl(device::OtaUrl()).c_str());
    printf("Hub      : %s\n", session::CheckedIn() ? session::WebsocketUrl().c_str() : "(not checked in)");
    printf("Session  : %s\n", session::StateName(session::GetState()));
    printf("Device id: %s\n", device::MacAddress().c_str());
    printf("Free heap: %lu (min %lu)\n", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)esp_get_minimum_free_heap_size());
}

void Mic(const char* args) {
    const char* rest;
    if (strcmp(args, "mute") == 0) {
        audio::SetMicMuted(true);
    } else if (strcmp(args, "unmute") == 0) {
        audio::SetMicMuted(false);
    } else if (Is(args, "gain", &rest) && *rest != '\0') {
        audio::SetMicGain(atof(rest));
    } else if (*args != '\0' && strcmp(args, "status") != 0) {
        printf("Usage: !mic [gain N|mute|unmute|status]\n");
        return;
    }
    printf("Mic: %s, gain %.1f, peak over the last second %d\n",
           audio::MicMuted() ? "muted" : "live", audio::MicGain(), audio::MicPeak());
}

void Speaker(const char* args) {
    const char* rest;
    if (*args == '\0') {
        audio::PlayTestTone();
        printf("Playing a test tone at volume %d.\n", audio::Volume());
    } else if (Is(args, "vol", &rest) && *rest != '\0') {
        audio::SetVolume(atoi(rest));
        printf("Volume %d.\n", audio::Volume());
    } else if (strcmp(args, "status") == 0) {
        printf("Volume %d.\n", audio::Volume());
    } else {
        printf("Usage: !speaker | !speaker vol N | !speaker status\n");
    }
}

void Quiet(const char* args) {
    if (strcmp(args, "off") == 0) {
        session::SetQuiet(false);
        printf("Quiet mode off: listening resumed.\n");
    } else if (strcmp(args, "status") == 0) {
        printf("Quiet mode: %s\n", session::Quiet() ? "on" : "off");
    } else if (*args == '\0' || strcmp(args, "on") == 0) {
        session::SetQuiet(true);
        printf("Quiet mode on: not listening, not speaking.\n");
    } else {
        printf("Usage: !quiet [on|off|status]\n");
    }
}

void Help() {
    printf("  !wifi SSID PASSWORD  add a network      !wifi list | remove N | clear\n");
    printf("  !server IP|URL       set the hub and reboot   !server | list | N\n");
    printf("  !status              Wi-Fi, hub, session, heap\n");
    printf("  !mic [gain N|mute|unmute|status]\n");
    printf("  !speaker [vol N|status]   no argument plays a test tone\n");
    printf("  !stop                close the voice session (it reopens)\n");
    printf("  !quiet [on|off|status]    pause listening and speaking\n");
    printf("  !reboot\n");
    printf("  !coglet state | release | scan    see docs/OPERATION.md for the rest\n");
}

void HandleLine(const char* line) {
    const char* args;
    if (CogletSerialCommand(line)) return;
    if (Is(line, "!wifi", &args)) return Wifi(args);
    if (Is(line, "!server", &args)) return Server(args);
    if (Is(line, "!status", &args)) return Status();
    if (Is(line, "!mic", &args)) return Mic(args);
    if (Is(line, "!speaker", &args)) return Speaker(args);
    if (Is(line, "!quiet", &args)) return Quiet(args);
    if (Is(line, "!help", &args)) return Help();
    if (Is(line, "!stop", &args)) {
        session::Restart();
        printf("Voice session closed.\n");
        return;
    }
    if (Is(line, "!reboot", &args)) {
        printf("Rebooting.\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    printf("Unknown command: %s (try !help)\n", line);
}

void ConsoleTask(void*) {
    // The default USB-Serial-JTAG console only wires up non-blocking output.
    // Install the driver so stdin blocks and delivers bytes.
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin), F_SETFL, 0);
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&config) == ESP_OK) usb_serial_jtag_vfs_use_driver();

    char buf[256];
    int pos = 0;
    while (true) {
        int ch = fgetc(stdin);
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            fputc('\n', stdout);
            buf[pos] = '\0';
            pos = 0;
            // Skip line noise before the command (e.g. an escape sequence).
            const char* line = strchr(buf, '!');
            if (line != nullptr) {
                std::string trimmed = line;
                while (!trimmed.empty() && isspace((unsigned char)trimmed.back())) trimmed.pop_back();
                HandleLine(trimmed.c_str());
            } else if (buf[0] != '\0') {
                printf("Commands start with ! (try !help)\n");
            }
            fflush(stdout);
            continue;
        }
        if (ch < 0x20 && ch != '\t') continue;
        if (pos < (int)sizeof(buf) - 1) {
            buf[pos++] = (char)ch;
            fputc(ch, stdout);
            fflush(stdout);
        }
    }
}

}  // namespace

void Start() {
    xTaskCreate(ConsoleTask, "console", 6144, nullptr, 5, nullptr);
}

}  // namespace console
