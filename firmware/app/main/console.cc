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
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace console {
namespace {

constexpr int kServerHistoryMax = 5;

std::string Format(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

bool Is(const char* line, const char* command, const char** args) {
    size_t n = strlen(command);
    if (strncmp(line, command, n) != 0 || (line[n] != ' ' && line[n] != '\0')) return false;
    *args = line[n] == ' ' ? line + n + 1 : "";
    return true;
}

// Restart a second from now, so the caller (serial or an MCP reply) can say so
// first.
void RebootSoon() {
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }, "reboot", 2048, nullptr, 1, nullptr);
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
std::string HostToUrl(const std::string& arg, std::string& out) {
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
        out += "Dropped the previous URL's query string: tokens belong to the hub that issued them.\n";
    }
    if (path.empty() || path == "/") path = "/xiaozhi/ota/";
    if (port.empty()) {
        size_t c = authority.find(':');
        port = c != std::string::npos ? authority.substr(c) : ":8003";
    }
    return "http://" + host + port + path;
}

std::string Server(const char* args) {
    auto history = ServerHistory();
    if (*args == '\0') {
        return "OTA URL: " + device::RedactUrl(device::OtaUrl()) +
               "\nUsage: !server IP | !server URL | !server list | !server N";
    }
    if (strcmp(args, "list") == 0) {
        if (history.empty()) return "No recent servers.";
        std::string out;
        for (size_t i = 0; i < history.size(); i++) {
            out += Format("  [%d] ", (int)i + 1) + device::RedactUrl(history[i]) + "\n";
        }
        return out;
    }
    std::string out;
    std::string url = args;
    bool index = true;
    for (char c : url) index = index && isdigit((unsigned char)c);
    if (index) {
        int n = atoi(args);
        if (n < 1 || n > (int)history.size()) return Format("No recent server #%d. Try: !server list", n);
        url = history[n - 1];
    } else if (url.rfind("http", 0) != 0) {
        url = HostToUrl(url, out);
    }
    settings::SetString("wifi", "ota_url", url);
    RememberServer(url);
    RebootSoon();
    return out + "Server set to " + device::RedactUrl(url) + ". Rebooting.";
}

std::string Wifi(const char* args) {
    if (*args == '\0' || strcmp(args, "list") == 0) {
        auto saved = wifi::Saved();
        if (saved.empty()) return "No saved networks. Add one: !wifi SSID PASSWORD";
        std::string out;
        for (size_t i = 0; i < saved.size(); i++) out += Format("  [%d] ", (int)i + 1) + saved[i].ssid + "\n";
        return out;
    }
    if (strcmp(args, "clear") == 0) {
        wifi::Clear();
        return "Saved networks cleared.";
    }
    const char* rest;
    if (Is(args, "remove", &rest)) {
        int n = atoi(rest);
        return Format(wifi::Remove(n - 1) ? "Removed network #%d." : "No saved network #%d.", n);
    }
    // SSID and password separated by the last space, so SSIDs may contain spaces.
    std::string text = args;
    size_t space = text.rfind(' ');
    std::string ssid = space == std::string::npos ? text : text.substr(0, space);
    std::string password = space == std::string::npos ? "" : text.substr(space + 1);
    if (!wifi::Add(ssid, password)) return "SSID must be 1-32 bytes and the password at most 64.";
    wifi::Rescan();
    return "Saved " + ssid + ". Connecting.";
}

std::string Status() {
    std::string out;
    out += Format("Firmware : %s (%s)\n", esp_app_get_description()->version, BOARD_NAME);
    out += "Wi-Fi    : " + (wifi::Connected() ? wifi::Ssid() : std::string("(not connected)")) + " " +
           wifi::Ip() + Format(" rssi %d\n", wifi::Rssi());
    out += "OTA URL  : " + device::RedactUrl(device::OtaUrl()) + "\n";
    out += "Hub      : " + (session::CheckedIn() ? session::WebsocketUrl() : std::string("(not checked in)")) + "\n";
    out += std::string("Session  : ") + session::StateName(session::GetState()) +
           (session::Quiet() ? " (quiet)" : "") + "\n";
    out += "Device id: " + device::MacAddress() + "\n";
    out += Format("Free heap: %lu (min %lu)", (unsigned long)esp_get_free_heap_size(),
                  (unsigned long)esp_get_minimum_free_heap_size());
    return out;
}

std::string Mic(const char* args) {
    const char* rest;
    if (strcmp(args, "mute") == 0) {
        audio::SetMicMuted(true);
    } else if (strcmp(args, "unmute") == 0) {
        audio::SetMicMuted(false);
    } else if (Is(args, "gain", &rest) && *rest != '\0') {
        audio::SetMicGain(atof(rest));
    } else if (*args != '\0' && strcmp(args, "status") != 0) {
        return "Usage: !mic [gain N|mute|unmute|status]";
    }
    return Format("Mic: %s, gain %.1f, peak over the last second %d",
                  audio::MicMuted() ? "muted" : "live", audio::MicGain(), audio::MicPeak());
}

std::string Speaker(const char* args) {
    const char* rest;
    if (*args == '\0' || strcmp(args, "test") == 0) {
        audio::PlayTestTone();
        return Format("Playing a test tone at volume %d.", audio::Volume());
    }
    if (Is(args, "vol", &rest) && *rest != '\0') {
        audio::SetVolume(atoi(rest));
        return Format("Volume %d.", audio::Volume());
    }
    if (strcmp(args, "status") == 0) return Format("Volume %d.", audio::Volume());
    return "Usage: !speaker [test] | !speaker vol N | !speaker status";
}

std::string Quiet(const char* args) {
    if (strcmp(args, "off") == 0) {
        session::SetQuiet(false);
        return "Quiet mode off: listening and speaking.";
    }
    if (strcmp(args, "status") == 0) return std::string("Quiet mode: ") + (session::Quiet() ? "on" : "off");
    if (*args == '\0' || strcmp(args, "on") == 0) {
        session::SetQuiet(true);
        return "Quiet mode on: mic and speaker off; MCP stays connected.";
    }
    return "Usage: !quiet [on|off|status]";
}

std::string Help() {
    return "  !wifi SSID PASSWORD  add a network      !wifi list | remove N | clear\n"
           "  !server IP|URL       set the hub and reboot   !server | list | N\n"
           "  !status              Wi-Fi, hub, session, heap\n"
           "  !mic [gain N|mute|unmute|status]\n"
           "  !speaker [test|vol N|status]\n"
           "  !stop                close the voice session (it reopens)\n"
           "  !quiet [on|off|status]    mic and speaker off; MCP stays up\n"
           "  !reboot\n"
           "  !coglet COMMAND      robot commands; see docs/OPERATION.md";
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
                printf("%s\n", Run(trimmed).c_str());
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

std::string Run(const std::string& text) {
    const char* line = text.c_str();
    const char* args;
    if (Is(line, "!coglet", &args)) {
        try {
            return CogletController::Instance().Command(*args ? args : "state");
        } catch (const std::exception& e) {
            return std::string("Coglet error: ") + e.what();
        }
    }
    if (Is(line, "!wifi", &args)) return Wifi(args);
    if (Is(line, "!server", &args)) return Server(args);
    if (Is(line, "!status", &args)) return Status();
    if (Is(line, "!mic", &args)) return Mic(args);
    if (Is(line, "!speaker", &args)) return Speaker(args);
    if (Is(line, "!quiet", &args)) return Quiet(args);
    if (Is(line, "!help", &args)) return Help();
    if (Is(line, "!stop", &args)) {
        session::Restart();
        return "Voice session closed; it reopens on its own.";
    }
    if (Is(line, "!reboot", &args)) {
        RebootSoon();
        return "Rebooting.";
    }
    return std::string("Unknown command: ") + line + " (try !help)";
}

void Start() {
    xTaskCreate(ConsoleTask, "console", 6144, nullptr, 5, nullptr);
}

}  // namespace console
