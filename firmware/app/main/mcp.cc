#include "mcp.h"

#include "audio.h"
#include "board.h"
#include "console.h"
#include "hub.h"
#include "wifi.h"
#include "coglet_controller.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cmath>
#include <cstring>
#include <stdexcept>

#define TAG "mcp"

namespace mcp {
namespace {

enum class Type { Integer, Number, String, Boolean };

struct Param {
    const char* name;
    Type type;
    bool required = true;
    double minimum = 0, maximum = 0;  // checked when they differ
    bool defaults_to_zero = false;    // an optional number that's 0 when absent
    bool spaces = false;              // a string that may contain spaces (last argument only)
};

using Handler = std::function<std::string(const cJSON* args)>;

struct Tool {
    const char* name;
    std::string description;
    std::vector<Param> params;
    Handler handler;
};

std::vector<Tool> s_tools;
constexpr size_t kPageBytes = 6000;  // tools/list pages stay well under a WS frame

Param Int(const char* name, double lo, double hi) { return {name, Type::Integer, true, lo, hi}; }
Param IntOr0(const char* name, double lo, double hi) { return {name, Type::Integer, false, lo, hi, true}; }
Param Num(const char* name, double lo, double hi) { return {name, Type::Number, true, lo, hi}; }
Param Str(const char* name, bool required = true) { return {name, Type::String, required}; }
Param Bool(const char* name) { return {name, Type::Boolean}; }

// Runs a controller command; its errors become MCP errors.
std::string Coglet(const std::string& command) {
    return CogletController::Instance().Command(command);
}

// Runs a serial-form device command and returns what it printed.
std::string Device(const std::string& line) {
    return console::Run(line);
}

std::string Arg(const cJSON* args, const char* name) {
    cJSON* v = cJSON_GetObjectItem(args, name);
    if (cJSON_IsString(v)) return v->valuestring;
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? "true" : "false";
    if (cJSON_IsNumber(v)) {
        char text[32];
        if (v->valuedouble == (double)v->valueint) snprintf(text, sizeof(text), "%d", v->valueint);
        else snprintf(text, sizeof(text), "%g", v->valuedouble);
        return text;
    }
    return "";
}

bool Has(const cJSON* args, const char* name) {
    return cJSON_GetObjectItem(args, name) != nullptr;
}

// "verb a b c" from the named arguments, in order.
Handler Command(const char* verb, std::vector<const char*> names = {}) {
    return [verb, names](const cJSON* args) {
        std::string line = verb;
        for (const char* n : names) {
            if (Has(args, n)) line += " " + Arg(args, n);
        }
        return Coglet(line);
    };
}

std::string DeviceStatus() {
    cJSON* root = cJSON_CreateObject();
    cJSON* speaker = cJSON_AddObjectToObject(root, "audio_speaker");
    cJSON_AddNumberToObject(speaker, "volume", audio::Volume());
    cJSON* mic = cJSON_AddObjectToObject(root, "audio_mic");
    cJSON_AddBoolToObject(mic, "muted", audio::MicMuted());
    cJSON_AddNumberToObject(mic, "gain", audio::MicGain());
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

const char* TypeName(Type type) {
    switch (type) {
        case Type::Integer: return "integer";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Boolean: return "boolean";
    }
    return "string";
}

cJSON* Schema(const Tool& tool) {
    cJSON* schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON* properties = cJSON_AddObjectToObject(schema, "properties");
    cJSON* required = cJSON_CreateArray();
    for (const auto& p : tool.params) {
        cJSON* prop = cJSON_AddObjectToObject(properties, p.name);
        cJSON_AddStringToObject(prop, "type", TypeName(p.type));
        if (p.minimum != p.maximum) {
            cJSON_AddNumberToObject(prop, "minimum", p.minimum);
            cJSON_AddNumberToObject(prop, "maximum", p.maximum);
        }
        if (p.defaults_to_zero) cJSON_AddNumberToObject(prop, "default", 0);
        if (p.required) cJSON_AddItemToArray(required, cJSON_CreateString(p.name));
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
            if (p.defaults_to_zero) cJSON_AddNumberToObject(args, p.name, 0);
            continue;
        }
        bool ok = true;
        switch (p.type) {
            case Type::Integer:
                ok = cJSON_IsNumber(value) && value->valuedouble == std::floor(value->valuedouble);
                break;
            case Type::Number:
                ok = cJSON_IsNumber(value) && std::isfinite(value->valuedouble);
                break;
            case Type::String:
                // A command argument is one word, so no spaces sneak in extra
                // ones, except where the command takes the rest of the line.
                ok = cJSON_IsString(value) && (p.spaces || strchr(value->valuestring, ' ') == nullptr);
                break;
            case Type::Boolean:
                ok = cJSON_IsBool(value);
                break;
        }
        if (!ok) return std::string("Argument must be a single ") + TypeName(p.type) + ": " + p.name;
        if ((p.type == Type::Integer || p.type == Type::Number) && p.minimum != p.maximum &&
            (value->valuedouble < p.minimum || value->valuedouble > p.maximum)) {
            return std::string("Argument out of range: ") + p.name;
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

cJSON* Describe(const Tool& tool) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "name", tool.name);
    cJSON_AddStringToObject(item, "description", tool.description.c_str());
    cJSON_AddItemToObject(item, "inputSchema", Schema(tool));
    return item;
}

// Pages by size: a page ends before the tool that would push it past
// kPageBytes, and nextCursor names that tool.
cJSON* ListTools(const cJSON* id, const cJSON* params) {
    cJSON* cursor = cJSON_GetObjectItem(params, "cursor");
    size_t start = 0;
    if (cJSON_IsString(cursor) && cursor->valuestring[0] != '\0') {
        start = s_tools.size();
        for (size_t i = 0; i < s_tools.size(); i++) {
            if (strcmp(s_tools[i].name, cursor->valuestring) == 0) start = i;
        }
        if (start == s_tools.size()) return Error(id, -32602, "Unknown cursor");
    }
    cJSON* root = Response(id);
    cJSON* result = cJSON_AddObjectToObject(root, "result");
    cJSON* tools = cJSON_AddArrayToObject(result, "tools");
    size_t bytes = 0;
    for (size_t i = start; i < s_tools.size(); i++) {
        cJSON* item = Describe(s_tools[i]);
        char* text = cJSON_PrintUnformatted(item);
        size_t size = text ? strlen(text) : 0;
        cJSON_free(text);
        if (bytes > 0 && bytes + size > kPageBytes) {
            cJSON_Delete(item);
            cJSON_AddStringToObject(result, "nextCursor", s_tools[i].name);
            break;
        }
        bytes += size;
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

std::string AnimationList(bool eyes) {
    std::string out;
    for (const auto& a : CogletController::Animations()) {
        if (a.eyes != eyes) continue;
        if (!out.empty()) out += ", ";
        out += a.name;
    }
    return out;
}

}  // namespace

void RegisterTools() {
    const std::string roles =
        "Roles: base, neck_tilt, neck_roll, eye_pan, eye_tilt, lids, jaw, ear_left, ear_right.";
    const std::string window = " Positions are commands, not measurements: no servo reports where it is.";
    s_tools = {
        // --- Motion. Needs the robot engaged; every position stays inside
        // each servo's calibrated window.
        {"self.coglet.gaze",
         "Point the eyes: horizontal -100 (left) to 100 (right), vertical -100 (down) to 100 (up), within the "
         "calibrated range. The top lids track the gaze. Needs the robot engaged and both eye servos fitted." + window,
         {IntOr0("horizontal", -100, 100), IntOr0("vertical", -100, 100)},
         Command("gaze", {"horizontal", "vertical"})},
        {"self.coglet.head",
         "Point the head: pan -100 (left) to 100 (right) on the base, tilt -100 (down) to 100 (up), roll -100 (left) "
         "to 100 (right). 0 is centre. Unfitted servos are skipped. Needs the robot engaged." + window,
         {IntOr0("pan", -100, 100), IntOr0("tilt", -100, 100), IntOr0("roll", -100, 100)},
         Command("head", {"pan", "tilt", "roll"})},
        {"self.coglet.jaw",
         "Open the jaw: 0 closed to 100 open. Needs the robot engaged." + window,
         {Int("open", 0, 100)}, Command("jaw", {"open"})},
        {"self.coglet.ears",
         "Set each ear: 0 folded back to 100 forward. Needs the robot engaged." + window,
         {Int("left", 0, 100), Int("right", 0, 100)}, Command("ears", {"left", "right"})},
        {"self.coglet.lids",
         "Set the top lids: 0 closed to 100 open. They hold there until the next gaze, which hands them back to "
         "gaze tracking. Needs the robot engaged." + window,
         {Int("open", 0, 100)}, Command("lids", {"open"})},
        {"self.coglet.blink",
         "One blink, timed on the robot. Needs the robot engaged, the lid servo fitted and a gaze already set.",
         {}, Command("blink")},
        {"self.coglet.animate",
         "Start an animation, timed on the robot. Eyes and lids: " + AnimationList(true) +
             ". Rest of the head: " + AnimationList(false) +
             ". Returns once it starts; poll self.coglet.state for completion or faults. Needs the robot engaged.",
         {Str("name")}, Command("animate", {"name"})},
        {"self.coglet.stop",
         "Stop any animation or servo test and hold the last commanded pose. Outputs stay on; use release to turn "
         "them off.",
         {}, Command("stop")},
        {"self.coglet.release",
         "Turn every servo output off and latch it off until engage or builder. Doesn't cut servo power: the "
         "rail switch does that.",
         {}, Command("release")},
        {"self.coglet.state",
         "Robot state: released, builder, animation, servo test, per-role calibration and last commanded angles, "
         "and hardware errors." + window,
         {}, Command("state")},
        // --- Modes.
        {"self.coglet.engage",
         "Turn servo outputs on for normal motion. Needs every assigned servo calibrated and confirmed. Commands "
         "no pose by itself. Only from released.",
         {}, Command("engage")},
        {"self.coglet.builder",
         "Turn servo outputs on for calibration and servo tests. Puts the voice in quiet mode (mic and speaker "
         "off; MCP stays connected) until it's turned off. Only from released.",
         {}, Command("builder")},
        // --- Calibration, one servo at a time.
        {"self.coglet.fit",
         "Assign a fitted servo to its standard PCA9685 channel (base 0 through ear_right 8), unconfirmed, with "
         "no range yet. Only while released. " + roles,
         {Str("role")}, Command("fit", {"role"})},
        {"self.coglet.configure",
         "Set one role's calibration by hand: channel (-1 unassigns), low and high endpoint angles (0-180), pulse "
         "range in microseconds, trim, and confirmed (0 or 1). Only while released. " + roles,
         {Str("role"), Int("channel", -1, 15), Num("low", 0, 180), Num("high", 0, 180),
          Int("min_us", 300, 3000), Int("max_us", 300, 3000), Int("trim_us", -2700, 2700), Int("confirmed", 0, 1)},
         Command("configure", {"role", "channel", "low", "high", "min_us", "max_us", "trim_us", "confirmed"})},
        {"self.coglet.lid_coupling",
         "Set how the top lids track the gaze: trim and coefficient, 0 to 1 each. Only while released.",
         {Num("trim", 0, 1), Num("coefficient", 0, 1)}, Command("lid_coupling", {"trim", "coefficient"})},
        {"self.coglet.identify",
         "Wiggle a raw PCA9685 channel for about 1.4 s to see which part is on it. Builder mode only.",
         {Int("channel", 0, 15)}, Command("identify", {"channel"})},
        {"self.coglet.explore",
         "Start hunting a role's endpoints: centre it at 1500 us, then nudge. Builder mode only. " + roles,
         {Str("role")}, Command("explore", {"role"})},
        {"self.coglet.nudge",
         "Move the role being explored by up to 5 degrees, inside the hard 1000-2000 us bound. Builder mode only.",
         {Num("degrees", -5, 5)}, Command("nudge", {"degrees"})},
        {"self.coglet.mark",
         "Record the explored position as the role's low or high endpoint. Clears its confirmation.",
         {Str("end")}, Command("mark", {"end"})},
        {"self.coglet.confirm",
         "Confirm a role's endpoints after someone has watched the servo reach both. " + roles,
         {Str("role")}, Command("confirm", {"role"})},
        {"self.coglet.save",
         "Save the calibration on the robot. Released or builder mode.",
         {}, Command("save")},
        {"self.coglet.export",
         "The full calibration and state as JSON, for a backup.",
         {}, Command("export")},
        {"self.coglet.servo",
         "Move one role to an angle inside its calibrated window. Builder mode only. " + roles,
         {Str("role"), Num("degrees", 0, 180)}, Command("servo", {"role", "degrees"})},
        {"self.coglet.jog",
         "Move one role by up to 5 degrees from its last commanded angle, inside its window. Builder mode only.",
         {Str("role"), Num("degrees", -5, 5)}, Command("jog", {"role", "degrees"})},
        {"self.coglet.test",
         "Servo test: sweep a role centre, low, high, centre (about 5 s), or with no role every calibrated role "
         "in turn. Builder mode only. Stop or release ends it. " + roles,
         {Str("role", false)}, Command("test", {"role"})},
        {"self.coglet.scan",
         "List the addresses answering on the servo I2C bus (the PCA9685 is 0x40). With sda and scl GPIOs, "
         "rebuilds the bus on those pins first, which releases the robot until a reboot.",
         {{"sda", Type::Integer, false, 0, 48}, {"scl", Type::Integer, false, 0, 48}},
         [](const cJSON* a) {
             if (Has(a, "sda") != Has(a, "scl")) throw std::invalid_argument("Give both sda and scl, or neither");
             return Coglet(Has(a, "sda") ? "scan " + Arg(a, "sda") + " " + Arg(a, "scl") : "scan");
         }},
        // --- Voice, audio and the device.
        {"self.voice.quiet",
         "Quiet mode on or off. On: the mic and speaker stop and the robot neither listens nor speaks; this MCP "
         "connection stays up. Builder mode turns it on.",
         {Bool("on")}, [](const cJSON* a) { return Device(std::string("!quiet ") + (cJSON_IsTrue(cJSON_GetObjectItem(a, "on")) ? "on" : "off")); }},
        {"self.voice.restart",
         "Close and reopen the voice session.",
         {}, [](const cJSON*) { return Device("!stop"); }},
        {"self.audio_speaker.set_volume",
         "Set the speaker volume, 0 to 100. Kept across reboots.",
         {Int("volume", 0, 100)}, [](const cJSON* a) { return Device("!speaker vol " + Arg(a, "volume")); }},
        {"self.audio_speaker.test_tone",
         "Play a short tone through the speaker.",
         {}, [](const cJSON*) { return Device("!speaker test"); }},
        {"self.audio_mic.status",
         "Mic mute state, gain and the peak level over the last second (0 means no signal).",
         {}, [](const cJSON*) { return Device("!mic status"); }},
        {"self.audio_mic.set",
         "Mute or unmute the mic, and/or set its gain (1.0 is unity).",
         {{"muted", Type::Boolean, false}, {"gain", Type::Number, false, 0, 64}},
         [](const cJSON* a) {
             std::string out;
             if (Has(a, "muted")) out = Device(std::string("!mic ") + (cJSON_IsTrue(cJSON_GetObjectItem(a, "muted")) ? "mute" : "unmute"));
             if (Has(a, "gain")) out = Device("!mic gain " + Arg(a, "gain"));
             return out.empty() ? Device("!mic status") : out;
         }},
        {"self.get_device_status",
         "Volume, mic, Wi-Fi network and signal, firmware version, uptime and free memory.",
         {}, [](const cJSON*) { return DeviceStatus(); }},
        {"self.device.status",
         "Firmware, Wi-Fi, hub URL, voice session state and memory, as text.",
         {}, [](const cJSON*) { return Device("!status"); }},
        {"self.wifi.list",
         "Saved Wi-Fi networks.",
         {}, [](const cJSON*) { return Device("!wifi list"); }},
        {"self.wifi.add",
         "Save a Wi-Fi network (2.4 GHz) and switch to the strongest saved one. Changing networks drops this "
         "connection until the robot is back on the hub.",
         {{"ssid", Type::String, true, 0, 0, false, true}, Str("password")},
         // !wifi splits at the last space, so the SSID may have spaces and the
         // password may not.
         [](const cJSON* a) { return Device("!wifi " + Arg(a, "ssid") + " " + Arg(a, "password")); }},
        {"self.wifi.remove",
         "Forget a saved Wi-Fi network by its number in self.wifi.list.",
         {Int("index", 1, 10)}, [](const cJSON* a) { return Device("!wifi remove " + Arg(a, "index")); }},
        {"self.hub.set_server",
         "Set the agent-hub check-in URL (or a bare host) and reboot. The robot reconnects to that hub.",
         {Str("url")}, [](const cJSON* a) { return Device("!server " + Arg(a, "url")); }},
        {"self.reboot",
         "Restart the robot. It comes back released, with the servo outputs off.",
         {}, [](const cJSON*) { return Device("!reboot"); }},
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
        reply(ListTools(id, params));
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
