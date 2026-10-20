#pragma once

#include <string>

// NVS key/value access. Namespaces and keys match what the xiaozhi fork wrote
// ("wifi"/ota_url, "board"/uuid, "audio"/output_volume), so a robot moved off
// the fork keeps its configuration.
namespace settings {
std::string GetString(const char* ns, const char* key, const std::string& fallback = "");
bool SetString(const char* ns, const char* key, const std::string& value);
int GetInt(const char* ns, const char* key, int fallback);
bool SetInt(const char* ns, const char* key, int value);
bool Erase(const char* ns, const char* key);
}  // namespace settings

// Device identity and configuration shared by the check-in, session and console.
namespace device {
std::string MacAddress();   // aa:bb:cc:dd:ee:ff, agent-hub's Device-Id
std::string ClientId();     // random UUID, generated once and kept in NVS
std::string OtaUrl();       // the !server URL, else CONFIG_OTA_URL
std::string RedactUrl(const std::string& url);  // hides enrollment_token
}  // namespace device
