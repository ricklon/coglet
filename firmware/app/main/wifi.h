#pragma once

#include <functional>
#include <string>
#include <vector>

// Station-only Wi-Fi. Networks are added over serial (!wifi); there is no
// setup hotspot. Saved networks use the fork's NVS keys: namespace "wifi",
// ssid/password, ssid1/password1 ... ssid9/password9.
namespace wifi {

struct Network {
    std::string ssid;
    std::string password;
};

constexpr int kMaxNetworks = 10;

std::vector<Network> Saved();
bool Add(const std::string& ssid, const std::string& password);  // replaces a same-SSID entry
bool Remove(int index);
void Clear();

// Starts the driver and keeps reconnecting to the strongest saved network.
// on_up runs once each time an address is obtained, on_down on every loss.
void Start(std::function<void()> on_up, std::function<void()> on_down);
void Rescan();  // after the saved list changes

bool Connected();
std::string Ssid();
std::string Ip();
int Rssi();
int Channel();

}  // namespace wifi
