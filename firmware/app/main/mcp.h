#pragma once

#include <cJSON.h>
#include <functional>
#include <string>
#include <vector>

// The robot's MCP server: JSON-RPC 2.0 carried inside the voice WebSocket as
// {"type":"mcp","payload":{...}}. agent-hub sends initialize, pages through
// tools/list, then calls tools/call. Only conversation tools are registered:
// the LLM can stop the robot but never engage, calibrate or drive raw servos.
namespace mcp {

using Reply = std::function<void(cJSON* payload)>;  // takes ownership

void RegisterTools();
// Handles one request. Replies, if any, go through reply.
void Handle(const cJSON* payload, const Reply& reply);
std::vector<std::string> ToolNames();

}  // namespace mcp
