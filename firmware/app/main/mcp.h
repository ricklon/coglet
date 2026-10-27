#pragma once

#include <cJSON.h>
#include <functional>
#include <string>
#include <vector>

// The robot's MCP server: JSON-RPC 2.0 carried inside the voice WebSocket as
// {"type":"mcp","payload":{...}}. agent-hub sends initialize, pages through
// tools/list, then calls tools/call. Every robot command is a tool, engage and
// calibration included, so anything with hub access can move the robot; the
// servo rail switch is the stop that doesn't depend on software.
namespace mcp {

using Reply = std::function<void(cJSON* payload)>;  // takes ownership

void RegisterTools();
// Handles one request. Replies, if any, go through reply.
void Handle(const cJSON* payload, const Reply& reply);
std::vector<std::string> ToolNames();

}  // namespace mcp
