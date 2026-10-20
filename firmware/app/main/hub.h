#pragma once

#include <cJSON.h>
#include <string>

// agent-hub's HTTP side: check-in at the OTA URL, and the liveness heartbeat.
namespace hub {

struct Config {
    std::string websocket_url;
    std::string websocket_token;
    std::string heartbeat_url;
    std::string heartbeat_token;
    int heartbeat_interval = 60;
};

// POSTs the device description to the OTA URL and returns where to open the
// voice session. Also sets the clock from the hub's server_time.
bool CheckIn(Config& config);

// activity: idle, listening, speaking, thinking or paused.
bool Heartbeat(const Config& config, const char* activity);

// The board/features/firmware block sent at check-in and in MCP initialize.
cJSON* Capabilities();

}  // namespace hub
