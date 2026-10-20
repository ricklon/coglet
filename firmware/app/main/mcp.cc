#include "mcp.h"

#include "audio.h"
#include "board.h"
#include "hub.h"
#include "wifi.h"
#include "coglet_controller.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cstring>
#include <stdexcept>

#define TAG "mcp"

namespace mcp {
namespace {

struct Param {
    const char* name;
    bool integer;      // otherwise a string
    bool required;
    int minimum = 0, maximum = 0;
};

using Handler = std::function<std::string(const cJSON* args)>;

struct Tool {
    const char* name;
    const char* description;
    std::vector<Param> params;
    Handler handler;
};

std::vector<Tool> s_tools;

std::string Coglet(const std::string& command) {
    return CogletController::Instance().Command(command);
}

int IntArg(const cJSON* args, const char* name) {
    return cJSON_GetObjectItem(args, name)->valueint;
}

std::string DeviceStatus() {
    cJSON* root = cJSON_CreateObject();
    cJSON* speaker = cJSON_AddObjectToObject(root, "audio_speaker");
    cJSON_AddNumberToObject(speaker, "volume", audio::Volume());
    cJSON* network = cJSON_AddObjectToObject(root, "network");
    cJSON_AddStringToObject(network, "type", "wifi");
    cJSON_AddStringToObject(network, "ssid", wifi::Ssid().c_str());
    cJSON_AddNumberToObject(network, "rssi", wifi::Rssi());
    cJSON_AddStringToObject(root, "firmware", esp_app_get_description()->version);
    cJSON_AddNumberToObject(root, "uptime_seconds", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    char* text = cJSON_PrintUnformatted(root);
    std::string out(text);
    cJSON_free(text);
    cJSON_Delete(root);
    return out;
}

cJSON* Schema(const Tool& tool) {
    cJSON* schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON* properties = cJSON_AddObjectToObject(schema, "properties");
    cJSON* required = cJSON_CreateArray();
    for (const auto& p : tool.params) {
        cJSON* prop = cJSON_AddObjectToObject(properties, p.name);
        cJSON_AddStringToObject(prop, "type", p.integer ? "integer" : "string");
        if (p.integer && p.minimum != p.maximum) {
            cJSON_AddNumberToObject(prop, "minimum", p.minimum);
            cJSON_AddNumberToObject(prop, "maximum", p.maximum);
        }
        if (p.required) {
            cJSON_AddItemToArray(required, cJSON_CreateString(p.name));
        } else if (p.integer) {
            cJSON_AddNumberToObject(prop, "default", 0);
        }
    }
    if (cJSON_GetArraySize(required) > 0) {
        cJSON_AddItemToObject(schema, "required", required);
    } else {
        cJSON_Delete(required);
    }
    return schema;
}

// Fills in defaults and checks types and ranges. Returns an error message, or
// an empty string when the arguments are usable.
std::string Validate(const Tool& tool, cJSON* args) {
    for (const auto& p : tool.params) {
        cJSON* value = cJSON_GetObjectItem(args, p.name);
        if (value == nullptr) {
            if (p.required) return std::string("Missing valid argument: ") + p.name;
            if (p.integer) cJSON_AddNumberToObject(args, p.name, 0);
            continue;
        }
        if (p.integer) {
            if (!cJSON_IsNumber(value) || value->valuedouble != (double)value->valueint) {
                return std::string("Argument must be an integer: ") + p.name;
            }
            if (p.minimum != p.maximum && (value->valueint < p.minimum || value->valueint > p.maximum)) {
                return std::string("Argument out of range: ") + p.name;
            }
        } else if (!cJSON_IsString(value)) {
            return std::string("Argument must be a string: ") + p.name;
        }
    }
    return "";
}

cJSON* Response(const cJSON* id) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, true));
    return root;
}

cJSON* Error(const cJSON* id, int code, const std::string& message) {
    cJSON* root = Response(id);
    cJSON* error = cJSON_AddObjectToObject(root, "error");
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message.c_str());
    return root;
}

cJSON* Initialize(const cJSON* id) {
    cJSON* root = Response(id);
    cJSON* result = cJSON_AddObjectToObject(root, "result");
    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
    cJSON* capabilities = cJSON_AddObjectToObject(result, "capabilities");
    cJSON_AddObjectToObject(capabilities, "tools");
    cJSON_AddItemToObject(capabilities, "xiaozhi", hub::Capabilities());
    cJSON* info = cJSON_AddObjectToObject(result, "serverInfo");
    cJSON_AddStringToObject(info, "name", BOARD_NAME);
    cJSON_AddStringToObject(info, "version", esp_app_get_description()->version);
    return root;
}

// The whole list fits in one page, so no nextCursor is ever returned.
cJSON* ListTools(const cJSON* id) {
    cJSON* root = Response(id);
    cJSON* result = cJSON_AddObjectToObject(root, "result");
    cJSON* tools = cJSON_AddArrayToObject(result, "tools");
    for (const auto& tool : s_tools) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", tool.name);
        cJSON_AddStringToObject(item, "description", tool.description);
        cJSON_AddItemToObject(item, "inputSchema", Schema(tool));
        cJSON_AddItemToArray(tools, item);
    }
    return root;
}

cJSON* CallTool(const cJSON* id, const cJSON* params) {
    cJSON* name = cJSON_GetObjectItem(params, "name");
    if (!cJSON_IsString(name)) return Error(id, -32602, "Missing name");
    const Tool* tool = nullptr;
    for (const auto& t : s_tools) {
        if (strcmp(t.name, name->valuestring) == 0) tool = &t;
    }
    if (tool == nullptr) return Error(id, -32602, std::string("Unknown tool: ") + name->valuestring);

    cJSON* given = cJSON_GetObjectItem(params, "arguments");
    if (given != nullptr && !cJSON_IsObject(given)) return Error(id, -32602, "Invalid arguments");
    cJSON* args = given ? cJSON_Duplicate(given, true) : cJSON_CreateObject();
    std::string problem = Validate(*tool, args);
    if (!problem.empty()) {
        cJSON_Delete(args);
        return Error(id, -32602, problem);
    }

    std::string text;
    try {
        text = tool->handler(args);
    } catch (const std::exception& e) {
        cJSON_Delete(args);
        ESP_LOGW(TAG, "%s failed: %s", tool->name, e.what());
        return Error(id, -32603, e.what());
    }
    cJSON_Delete(args);

    cJSON* root = Response(id);
    cJSON* result = cJSON_AddObjectToObject(root, "result");
    cJSON* content = cJSON_AddArrayToObject(result, "content");
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text.c_str());
    cJSON_AddItemToArray(content, item);
    cJSON_AddBoolToObject(result, "isError", false);
    return root;
}

}  // namespace

void RegisterTools() {
    s_tools = {
        {"self.get_device_status",
         "Speaker volume, Wi-Fi network and signal, firmware version, uptime and free memory.",
         {}, [](const cJSON*) { return DeviceStatus(); }},
        {"self.audio_speaker.set_volume",
         "Set the speaker volume, 0 to 100.",
         {{"volume", true, true, 0, 100}},
         [](const cJSON* a) {
             audio::SetVolume(IntArg(a, "volume"));
             return std::string("true");
         }},
        // Conversation tools only. Engagement, mapping, calibration and raw
        // servo commands stay local (serial or the builder page), so the LLM
        // can stop the robot but can never re-energise it.
        {"self.coglet.gaze",
         "Bounded base/tilt gaze (-100..100 within calibrated endpoints), with upper-lid tracking. Requires local engagement.",
         {{"horizontal", true, false, -100, 100}, {"vertical", true, false, -100, 100}},
         [](const cJSON* a) {
             return Coglet("gaze " + std::to_string(IntArg(a, "horizontal")) + " " +
                           std::to_string(IntArg(a, "vertical")));
         }},
        {"self.coglet.blink",
         "Start one device-timed blink; requires established gaze and local engagement.",
         {}, [](const cJSON*) { return Coglet("blink"); }},
        {"self.coglet.animate",
         "Start a device-timed expression: look, roll, side_eye, wink, surprise, sleepy. Poll state for completion or later faults.",
         {{"name", false, true}},
         [](const cJSON* a) {
             return Coglet(std::string("animate ") + cJSON_GetObjectItem(a, "name")->valuestring);
         }},
        {"self.coglet.stop",
         "Stop animation at the last commanded pose. Automatic blinking resumes after a settling interval; use release to prevent all motion.",
         {}, [](const cJSON*) { return Coglet("stop"); }},
        {"self.coglet.release",
         "Latch all servo outputs off. Only local builder controls can engage again. This does not cut servo power.",
         {}, [](const cJSON*) { return Coglet("release"); }},
        {"self.coglet.state",
         "Diagnostic state, calibration and commanded positions (no position feedback), release latch and hardware errors.",
         {}, [](const cJSON*) { return Coglet("state"); }},
    };
}

void Handle(const cJSON* payload, const Reply& reply) {
    cJSON* version = cJSON_GetObjectItem(payload, "jsonrpc");
    cJSON* method = cJSON_GetObjectItem(payload, "method");
    if (!cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0 ||
        !cJSON_IsString(method)) {
        ESP_LOGW(TAG, "Ignoring a message that isn't a JSON-RPC 2.0 request");
        return;
    }
    if (strncmp(method->valuestring, "notifications", 13) == 0) return;

    cJSON* id = cJSON_GetObjectItem(payload, "id");
    if (id == nullptr || !(cJSON_IsNumber(id) || cJSON_IsString(id) || cJSON_IsNull(id))) {
        ESP_LOGW(TAG, "Request %s has no usable id", method->valuestring);
        return;
    }
    cJSON* params = cJSON_GetObjectItem(payload, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        reply(Error(id, -32602, "Invalid params"));
        return;
    }

    std::string name = method->valuestring;
    if (name == "initialize") {
        reply(Initialize(id));
    } else if (name == "tools/list") {
        reply(ListTools(id));
    } else if (name == "tools/call") {
        if (params == nullptr) {
            reply(Error(id, -32602, "Missing params"));
        } else {
            reply(CallTool(id, params));
        }
    } else {
        reply(Error(id, -32601, "Method not implemented: " + name));
    }
}

std::vector<std::string> ToolNames() {
    std::vector<std::string> names;
    for (const auto& tool : s_tools) names.push_back(tool.name);
    return names;
}

}  // namespace mcp
