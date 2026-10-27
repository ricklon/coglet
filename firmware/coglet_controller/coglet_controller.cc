// Motion keyframes and upper-lid coupling adapted from eyemech-esp32-xiao
// components/eye_motion/eye_motion.c. See README.md for deliberate changes.
#include "coglet_controller.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <cstdio>

namespace {
// Roles run bottom of the mechanism upward, the order the servos are fitted
// in. Every role takes MCP commands: gaze drives the two eye axes (the shared
// top-lid servo tracks it), head drives base, neck_tilt and neck_roll, and
// jaw, ears and lids drive the rest. Animations move either the eyes or the
// rest of the head.
constexpr const char* names[] = {"base", "neck_tilt", "neck_roll",
                                 "eye_pan", "eye_tilt", "lids",
                                 "jaw", "ear_left", "ear_right"};
constexpr int kRoles = 9;
constexpr int kTestLegMs = 1200;  // one leg of a servo test sweep
constexpr int kEyePan = 3, kEyeTilt = 4, kLids = 5;
// The pose vector is (eye pan, eye tilt, lids); this maps it onto roles.
constexpr int kMotion[3] = {kEyePan, kEyeTilt, kLids};
// Keyframe targets as fractions of each role's calibrated window; NaN leaves a
// role alone (or, for the lids in an eye animation, hands them to the gaze).
struct Frame {
    float lr, ud, lid; int ms;
    float base = NAN, neck_tilt = NAN, neck_roll = NAN, jaw = NAN, ear_left = NAN, ear_right = NAN;
};
static const Frame s_frames_look[] = {
    { 0.50f, 0.50f, 1.00f, 400 },   /* open, centred             */
    { 0.00f, NAN,   NAN, 700 },   /* look left                 */
    { 1.00f, NAN, NAN, 1100 },   /* sweep across to the right */
    { 0.50f, NAN,   NAN, 600 },   /* back to centre            */
    { NAN,   NAN,   0.00f, 400 },   /* close                     */
};

/* A circle in gaze space. Lids left to the coupling throughout, so the eyes
 * hood through the bottom and widen over the top — that is most of what makes
 * it read as a roll rather than a mechanical sweep. */
static const Frame s_frames_roll[] = {
    { 0.50f, 0.50f, 1.00f, 500 },   /* open, centred   */
    { 0.50f, 0.92f, NAN, 500 },   /* up              */
    { 0.80f, 0.80f, NAN, 350 },
    { 0.92f, 0.50f, NAN, 350 },   /* right           */
    { 0.80f, 0.20f, NAN, 350 },
    { 0.50f, 0.08f, NAN, 350 },   /* down            */
    { 0.20f, 0.20f, NAN, 350 },
    { 0.08f, 0.50f, NAN, 350 },   /* left            */
    { 0.20f, 0.80f, NAN, 350 },
    { 0.50f, 0.92f, NAN, 350 },   /* back to the top */
    { 0.50f, 0.50f, NAN, 600 },   /* settle centred  */
};

/* Suspicion. The snap across is quick, the lids narrow to a slit, and then it
 * HOLDS — the hold is the whole emote. Coming back is slower than going. */
static const Frame s_frames_side_eye[] = {
    { 0.50f, 0.50f, 0.90f, 300 },
    { 0.12f, 0.56f, 0.45f, 350 },   /* dart across, lids narrow */
    { 0.12f, 0.56f, 0.45f, 1300 },   /* hold the look            */
    { 0.50f, 0.50f, 0.90f, 550 },   /* unhurried return         */
};

/* Written for per-eye lids. This mechanism has one servo for both top lids,
 * so the shut-one-eye track drives that single servo and wink reads as a
 * slower, held blink. */
static const Frame s_frames_wink[] = {
    { 0.50f, 0.50f, 0.95f, 300 },   /* open            */
    { NAN,   NAN,   0.00f, 170 },   /* lids shut, fast */
    { NAN,   NAN,   0.00f, 200 },   /* held shut       */
    { NAN,   NAN,   0.95f, 260 },   /* and back        */
};

/* Fast attack, long hold, slow release — the shape of a startle. */
static const Frame s_frames_surprise[] = {
    { 0.50f, 0.50f, 0.55f, 250 },   /* half-lidded first, for contrast */
    { 0.50f, 0.70f, 1.00f, 110 },   /* snap wide, gaze lifts           */
    { 0.50f, 0.70f, 1.00f, 950 },   /* hold                            */
    { 0.50f, 0.50f, 0.85f, 800 },   /* settle back down                */
};

/* Everything slow. Heaviness is pace, not position. */
static const Frame s_frames_sleepy[] = {
    { 0.50f, 0.50f, 0.85f, 600 },
    { 0.47f, 0.35f, 0.35f, 1500 },   /* lids droop, gaze sinks */
    { 0.46f, 0.30f, 0.00f, 900 },   /* slow close             */
    { 0.46f, 0.30f, 0.00f, 800 },   /* stays shut a beat      */
    { 0.50f, 0.44f, 0.55f, 1200 },   /* half open, still heavy */
};

/* The rest of the head. These leave the eyes and lids where they are, and
 * skip any role that isn't fitted. Fields after ms: base, neck_tilt,
 * neck_roll, jaw, ear_left, ear_right. */
constexpr float _ = NAN;

/* Yes: a dip, a smaller lift, a smaller dip, settle. */
static const Frame s_frames_nod[] = {
    { _, _, _, 250, _, 0.50f },
    { _, _, _, 220, _, 0.30f },
    { _, _, _, 220, _, 0.62f },
    { _, _, _, 220, _, 0.38f },
    { _, _, _, 300, _, 0.50f },
};

/* No: the base swings left, right, left, home. */
static const Frame s_frames_shake[] = {
    { _, _, _, 200, 0.50f },
    { _, _, _, 260, 0.30f },
    { _, _, _, 380, 0.70f },
    { _, _, _, 380, 0.32f },
    { _, _, _, 300, 0.50f },
};

/* Curious: the head rolls over and holds, ears come forward. */
static const Frame s_frames_tilt[] = {
    { _, _, _, 250, _, _, 0.50f, _, 0.50f, 0.50f },
    { _, _, _, 450, _, _, 0.25f, _, 0.85f, 0.85f },
    { _, _, _, 1200, _, _, 0.25f, _, 0.85f, 0.85f },
    { _, _, _, 550, _, _, 0.50f, _, 0.50f, 0.50f },
};

/* Attention: ears snap forward, the head lifts a little, then eases back. */
static const Frame s_frames_perk[] = {
    { _, _, _, 150, _, 0.50f, _, _, 0.50f, 0.50f },
    { _, _, _, 150, _, 0.58f, _, _, 1.00f, 1.00f },
    { _, _, _, 900, _, 0.58f, _, _, 1.00f, 1.00f },
    { _, _, _, 500, _, 0.50f, _, _, 0.65f, 0.65f },
};

/* Sad, and slow: ears fold back and the head sinks, holds, recovers. */
static const Frame s_frames_droop[] = {
    { _, _, _, 300, _, 0.50f, _, _, 0.50f, 0.50f },
    { _, _, _, 900, _, 0.30f, _, _, 0.00f, 0.00f },
    { _, _, _, 1200, _, 0.30f, _, _, 0.00f, 0.00f },
    { _, _, _, 900, _, 0.50f, _, _, 0.50f, 0.50f },
};

/* A burst of jaw movement, uneven so it doesn't read as a metronome. */
static const Frame s_frames_talk[] = {
    { _, _, _, 80, _, _, _, 0.00f },
    { _, _, _, 120, _, _, _, 0.60f },
    { _, _, _, 100, _, _, _, 0.15f },
    { _, _, _, 120, _, _, _, 0.50f },
    { _, _, _, 100, _, _, _, 0.10f },
    { _, _, _, 130, _, _, _, 0.70f },
    { _, _, _, 150, _, _, _, 0.00f },
};

/* The whole head looks left, holds, sweeps right, holds, comes home. */
static const Frame s_frames_look_around[] = {
    { _, _, _, 300, 0.50f },
    { _, _, _, 700, 0.25f },
    { _, _, _, 400, 0.25f },
    { _, _, _, 1100, 0.75f },
    { _, _, _, 400, 0.75f },
    { _, _, _, 600, 0.50f },
};

// eyes: an eye animation, which needs both eye axes fitted and couples the
// lids to the gaze. The others need at least one of their roles fitted.
struct Animation { const char* name; const Frame* frames; int count; bool eyes; };
#define EYES(n) {#n, s_frames_##n, int(sizeof(s_frames_##n)/sizeof(Frame)), true}
#define HEAD(n) {#n, s_frames_##n, int(sizeof(s_frames_##n)/sizeof(Frame)), false}
const Animation animations[] = {
    EYES(look), EYES(roll), EYES(side_eye), EYES(wink), EYES(surprise), EYES(sleepy),
    HEAD(nod), HEAD(shake), HEAD(tilt), HEAD(perk), HEAD(droop), HEAD(talk), HEAD(look_around),
};
#undef EYES
#undef HEAD
constexpr int kAnimations = int(sizeof(animations)/sizeof(animations[0]));
// A frame's targets in role order.
std::array<float, 9> Targets(const Frame& f) {
    return {f.base, f.neck_tilt, f.neck_roll, f.lr, f.ud, f.lid, f.jaw, f.ear_left, f.ear_right};
}
int64_t Now() { return esp_timer_get_time()/1000; }
int64_t BlinkTime(int64_t now) { return now + 2000 + esp_random()%5001; }
int AxisIndex(const std::string& name) {
    for (int i=0; i<kRoles; ++i) if (name == names[i]) return i;
    return -1;
}
bool Unit(float v) { return std::isfinite(v) && v >= 0 && v <= 1; }
std::string Json(cJSON* root) {
    char* text = cJSON_PrintUnformatted(root);
    std::string result = text ? text : "{}";
    cJSON_free(text); cJSON_Delete(root); return result;
}
}

CogletController& CogletController::Instance() { static CogletController c; return c; }

esp_err_t CogletController::Register(uint8_t reg, uint8_t value) {
    if (!device_) return ESP_ERR_INVALID_STATE;
    uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(device_, bytes, sizeof(bytes), 50);
}
esp_err_t CogletController::Probe() {
    if (!ready_) return last_error_ == ESP_OK ? ESP_ERR_INVALID_STATE : last_error_;
    uint8_t reg=0, mode=0;
    auto e = i2c_master_transmit_receive(device_, &reg, 1, &mode, 1, 50);
    if (e == ESP_OK && (mode & 0x30) != 0x20) e = ESP_ERR_INVALID_STATE;
    return e;
}
esp_err_t CogletController::InitHardware() {
    // Name the failing step: "PCA init: ESP_ERR_INVALID_STATE" alone cannot
    // distinguish a busy I2C port from a servo board that never answers.
    auto step = [](const char* what, esp_err_t err) {
        if (err != ESP_OK) ESP_LOGE("Coglet", "PCA init %s: %s", what, esp_err_to_name(err));
        return err;
    };
    // Assert OE BEFORE configuring the output direction; external pull-up is
    // required for the reset window. Existing Eyemech OE may be UNWIRED.
    auto e = step("oe_level", gpio_set_level(pins_.oe, 1));
    if (e != ESP_OK) return e;
    if ((e = step("oe_dir", gpio_set_direction(pins_.oe, GPIO_MODE_OUTPUT))) != ESP_OK) return e;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = pins_.i2c_port;
    bus.sda_io_num = pins_.sda; bus.scl_io_num = pins_.scl;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    if ((e = step("new_bus", i2c_new_master_bus(&bus, &bus_))) != ESP_OK) return e;
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7; dev.device_address = pins_.address;
    dev.scl_speed_hz = 100000;
    if ((e = step("add_device", i2c_master_bus_add_device(bus_, &dev, &device_))) != ESP_OK) return e;
    // Full-off ALL outputs before waking/reprogramming a warm PCA9685.
    if ((e = step("release", Release())) != ESP_OK) return e;
    if ((e = Register(0, 0x30)) != ESP_OK) return e; // sleep + auto-increment
    if ((e = Register(0xfe, 121)) != ESP_OK) return e; // nominal 25 MHz, ~50 Hz
    if ((e = Register(1, 4)) != ESP_OK) return e;
    if ((e = Register(0, 0x20)) != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(5));
    return Register(0, 0xa0);
}
esp_err_t CogletController::Release() {
    released_ = true; calibrating_ = false; animation_ = -1; blink_until_ = 0; exploring_ = -1;
    testing_ = -1;
    pose_ = kRestPose;  // a later engage must not resume an old head or jaw pose
    auto oe = gpio_set_level(pins_.oe, 1);
    // Single-register full-off works even before auto-increment is configured.
    auto off = Register(0xfd, 0x10);
    commanded_.fill(NAN);
    release_error_ = oe != ESP_OK ? oe : off;
    return release_error_;
}
esp_err_t CogletController::Fail(esp_err_t error) {
    if (error != ESP_OK) { last_error_ = error; Release(); }
    return error;
}
bool CogletController::Valid(const Calibration& cal, bool complete) const {
    if (cal.version != 3 || !Unit(cal.lid_trim) || !Unit(cal.upper_coeff)) return false;
    unsigned used = 0;
    unsigned assigned = 0;
    for (int i = 0; i < kRoles; ++i) {
        const auto& a = cal.axes[i];
        if (a.confirmed > 1 || a.channel < -1 || a.channel > 15) return false;
        if (a.channel >= 0) {
            if (used & (1u << a.channel)) return false;
            used |= 1u << a.channel;
        }
        if (!std::isfinite(a.low) || !std::isfinite(a.high) ||
            a.low < 0 || a.low > 180 || a.high < 0 || a.high > 180 ||
            a.min_us < 300 || a.max_us > 3000 || a.min_us >= a.max_us ||
            a.trim_us < -2700 || a.trim_us > 2700 ||
            a.min_us + a.trim_us < 300 || a.max_us + a.trim_us > 3000) return false;
        // Confirmation means a human watched this axis reach both endpoints.
        if (a.confirmed && (a.channel < 0 || a.low == a.high)) return false;
        // The mechanism is built up one servo at a time, so an unassigned role
        // is a role not fitted yet, not an incomplete calibration. Anything
        // assigned must be confirmed before the robot may drive it.
        if (complete && a.channel >= 0 && !a.confirmed) return false;
        if (a.channel >= 0) ++assigned;
    }
    // Engaging a mechanism with nothing fitted would be a no-op with live outputs.
    return !complete || assigned > 0;
}
void CogletController::Load() {
    nvs_handle_t h;
    if (nvs_open("coglet", NVS_READONLY, &h) != ESP_OK) return;
    Calibration candidate;
    size_t size = sizeof(candidate);
    auto e = nvs_get_blob(h, "cal_v3", &candidate, &size);
    nvs_close(h);
    if (e == ESP_OK && size == sizeof(candidate) && Valid(candidate, false)) cal_ = candidate;
}
esp_err_t CogletController::Save() {
    nvs_handle_t h;
    auto e = nvs_open("coglet", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, "cal_v3", &cal_, sizeof(cal_));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h); return e;
}
esp_err_t CogletController::Write(int axis, float degrees) {
    if (released_ || !ready_) return ESP_ERR_INVALID_STATE;
    if (axis < 0 || axis >= kRoles || !std::isfinite(degrees)) return ESP_ERR_INVALID_ARG;
    const auto& a = cal_.axes[axis];
    if (a.channel < 0 || degrees < std::min(a.low,a.high) || degrees > std::max(a.low,a.high)) return ESP_ERR_INVALID_ARG;
    if (commanded_[axis] == degrees) return ESP_OK;
    float us = a.min_us + (a.max_us-a.min_us)*degrees/180.f + a.trim_us;
    // Use the actual programmed prescaler, rather than assuming exactly 20ms.
    int ticks = std::lround(us * 25.f / 122.f);
    uint8_t bytes[] = {uint8_t(6+4*a.channel), 0, 0, uint8_t(ticks), uint8_t(ticks>>8)};
    auto e = i2c_master_transmit(device_, bytes, sizeof(bytes), 50);
    if (e == ESP_OK) commanded_[axis] = degrees; // commit only after success
    return e;
}
// Raw per-channel pulse for endpoint hunting and channel identification:
// bypasses the calibrated window, but never the hard 1000-2000us bound.
esp_err_t CogletController::Pulse(int channel, float us) {
    if (released_ || !ready_) return ESP_ERR_INVALID_STATE;
    if (channel < 0 || channel > 15 || !(us >= 1000 && us <= 2000)) return ESP_ERR_INVALID_ARG;
    int ticks = std::lround(us * 25.f / 122.f);
    uint8_t bytes[] = {uint8_t(6+4*channel), 0, 0, uint8_t(ticks), uint8_t(ticks>>8)};
    return i2c_master_transmit(device_, bytes, sizeof(bytes), 50);
}
float CogletController::Micros(const Axis& a, float degrees) const {
    return a.min_us + (a.max_us-a.min_us)*degrees/180.f + a.trim_us;
}
std::vector<CogletController::AnimationInfo> CogletController::Animations() {
    std::vector<AnimationInfo> out;
    for (const auto& a : animations) out.push_back({a.name, a.eyes});
    return out;
}
esp_err_t CogletController::Apply(const std::array<float,9>& pose) {
    auto target = pose;
    // Eyemech upper-lid tracking: trim scales measured open end, then hood
    // by 0.8 * (1 - normalized vertical gaze). Coglet has no lower eyelids.
    if (std::isnan(target[kLids]) && std::isfinite(pose[kEyeTilt]))
        target[kLids] = (0.5f + 0.5f*cal_.lid_trim)*(1.f-cal_.upper_coeff*(1.f-pose[kEyeTilt]));
    for (int i=0; i<kRoles; ++i) {
        const auto& a=cal_.axes[i];
        if (a.channel < 0 || std::isnan(target[i])) continue; // not fitted, or no target
        auto e = Write(i, a.low+(a.high-a.low)*target[i]);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}
esp_err_t CogletController::Tick(int64_t now) {
    if (released_) return ESP_OK;
    auto e = Probe(); // detects absent/reset PCA even if cached targets repeat
    if (e != ESP_OK) return e;
    if (calibrating_) return TestTick(now);
    if (animation_ >= 0) {
        const auto& a=animations[animation_]; const auto& f=a.frames[frame_];
        float k=std::min(1.f, float(now-frame_start_)/f.ms);
        auto dest=Targets(f);
        auto next=pose_;
        for (int i=0;i<kRoles;++i) {
            if (std::isnan(dest[i])) { if (i==kLids && a.eyes) next[i]=NAN; }
            else next[i]=std::isnan(from_[i]) ? dest[i] : from_[i]+(dest[i]-from_[i])*k;
        }
        if ((e=Apply(next)) != ESP_OK) return e;
        pose_=next;
        if (k>=1) {
            if (++frame_ >= a.count) { animation_=-1; next_blink_=BlinkTime(now+900); }
            else { from_=pose_; frame_start_=now; }
        }
    } else if (blink_until_ && now>=blink_until_) {
        if ((e=Apply(pose_)) != ESP_OK) return e;
        blink_until_=0; next_blink_=BlinkTime(now+70);
    } else if (!blink_until_ && now>=next_blink_) {
        auto closed=pose_; closed[kLids]=0;
        if ((e=Apply(closed)) != ESP_OK) return e;
        blink_until_=now+70;
    }
    return ESP_OK;
}
// A role can be tested once it's assigned and has a calibrated window.
bool CogletController::Testable(int axis) const {
    const auto& a=cal_.axes[axis];
    return a.channel>=0 && a.low!=a.high;
}
int CogletController::NextTestable(int after) const {
    for (int i=after+1;i<kRoles;++i) if (Testable(i)) return i;
    return -1;
}
void CogletController::StartTest(int axis, int64_t now) {
    testing_=axis; test_leg_=0; test_start_=now; test_from_=commanded_[axis];
}
// Centre, low end, high end, centre: each leg eased over kTestLegMs, and every
// command inside the calibrated window.
esp_err_t CogletController::TestTick(int64_t now) {
    if (testing_<0) return ESP_OK;
    const auto& a=cal_.axes[testing_];
    float mid=(a.low+a.high)/2;
    const float legs[]={mid,a.low,a.high,mid};
    float to=legs[test_leg_];
    float k=std::min(1.f, float(now-test_start_)/kTestLegMs);
    float angle=std::isfinite(test_from_) ? test_from_+(to-test_from_)*k : to;
    auto e=Write(testing_, angle);
    if (e!=ESP_OK) return e;
    if (k>=1) {
        if (++test_leg_<4) { test_from_=to; test_start_=now; }
        else {
            int next=test_all_ ? NextTestable(testing_) : -1;
            if (next>=0) StartTest(next, now); else testing_=-1;
        }
    }
    return ESP_OK;
}
cJSON* CogletController::State() {
    auto* root=cJSON_CreateObject();
    cJSON_AddBoolToObject(root,"released",released_);
    cJSON_AddBoolToObject(root,"calibrated",Valid(cal_,true));
    cJSON_AddBoolToObject(root,"calibrating",calibrating_);
    cJSON_AddBoolToObject(root,"pca_initialized",ready_);
    cJSON_AddStringToObject(root,"last_hardware_error",esp_err_to_name(last_error_));
    cJSON_AddStringToObject(root,"release_error",esp_err_to_name(release_error_));
    cJSON_AddStringToObject(root,"animation",animation_<0 ? "none" : animations[animation_].name);
    if (testing_>=0) cJSON_AddStringToObject(root,"testing",names[testing_]);
    else cJSON_AddNullToObject(root,"testing");
    cJSON_AddStringToObject(root,"position_semantics","commanded positions; no feedback; null after release");
    cJSON_AddNumberToObject(root,"calibration_version",cal_.version);
    if (exploring_>=0) {
        cJSON_AddStringToObject(root,"exploring",names[exploring_]);
        cJSON_AddNumberToObject(root,"explore_angle",explore_angle_);
    } else {
        cJSON_AddNullToObject(root,"exploring");
    }
    cJSON_AddNumberToObject(root,"lid_trim",cal_.lid_trim);
    cJSON_AddNumberToObject(root,"upper_coeff",cal_.upper_coeff);
    auto* axes=cJSON_AddArrayToObject(root,"axes");
    for (int i=0;i<kRoles;++i) {
        auto* v=cJSON_CreateObject(); const auto& a=cal_.axes[i];
        cJSON_AddStringToObject(v,"role",names[i]); cJSON_AddNumberToObject(v,"channel",a.channel);
        cJSON_AddNumberToObject(v,"standard_channel",pins_.channels[i]);
        cJSON_AddNumberToObject(v,"low",a.low); cJSON_AddNumberToObject(v,"high",a.high);
        cJSON_AddNumberToObject(v,"min_us",a.min_us); cJSON_AddNumberToObject(v,"max_us",a.max_us);
        cJSON_AddNumberToObject(v,"trim_us",a.trim_us); cJSON_AddBoolToObject(v,"confirmed",a.confirmed);
        if (std::isfinite(commanded_[i])) cJSON_AddNumberToObject(v,"commanded_degrees",commanded_[i]);
        else cJSON_AddNullToObject(v,"commanded_degrees");
        cJSON_AddItemToArray(axes,v);
    }
    return root;
}

std::string CogletController::Command(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::istringstream in(text);
    std::string verb; in >> verb;
    auto end = [&]() { std::string extra; return !(in >> extra); };
    auto check = [&](esp_err_t e) {
        if (e != ESP_OK) throw std::runtime_error(esp_err_to_name(e));
    };
    auto hardware = [&](esp_err_t e) { check(Fail(e)); };
    if (verb == "state" || verb == "export") {
        if (!end()) check(ESP_ERR_INVALID_ARG);
        if (ready_) Fail(Probe());
        return Json(State());
    }
    if (verb == "release") {
        if (!end()) check(ESP_ERR_INVALID_ARG);
        check(Release()); return Json(State());
    }
    // Diagnostic: who is actually on the servo I2C bus? A PCA9685 that
    // does not answer at its address is either unpowered, on different
    // pins, or strapped to another address; only a sweep tells them apart.
    if (verb == "scan") {
        // Optional pin override, so a suspected SDA/SCL swap or a harness on
        // different pads can be ruled out without reflashing. This tears the
        // servo bus down; the board stays released until the next reboot.
        int sda = -1, scl = -1;
        if (in >> sda) { if (!(in >> scl) || !end()) check(ESP_ERR_INVALID_ARG); }
        if (sda >= 0) {
            Release();
            if (device_) { i2c_master_bus_rm_device(device_); device_ = nullptr; }
            if (bus_) { i2c_del_master_bus(bus_); bus_ = nullptr; }
            ready_ = false;
            i2c_master_bus_config_t rescan = {};
            rescan.i2c_port = pins_.i2c_port;
            rescan.sda_io_num = decltype(rescan.sda_io_num)(sda);
            rescan.scl_io_num = decltype(rescan.scl_io_num)(scl);
            rescan.clk_source = I2C_CLK_SRC_DEFAULT; rescan.glitch_ignore_cnt = 7;
            rescan.flags.enable_internal_pullup = true;
            check(i2c_new_master_bus(&rescan, &bus_));
        }
        if (!bus_) check(ESP_ERR_INVALID_STATE);
        std::string found;
        for (int addr = 0x08; addr <= 0x77; ++addr) {
            if (i2c_master_probe(bus_, addr, 50) == ESP_OK) {
                char hex[8];
                snprintf(hex, sizeof(hex), "0x%02x", addr);
                if (!found.empty()) found += ", ";
                found += hex;
            }
        }
        char pins[96];
        snprintf(pins, sizeof(pins), "I2C port %d, SDA GPIO%d, SCL GPIO%d: ",
                 (int)pins_.i2c_port,
                 sda >= 0 ? sda : (int)pins_.sda,
                 sda >= 0 ? scl : (int)pins_.scl);
        return std::string(pins) + (found.empty() ? "no devices responded" : found);
    }
    if (verb == "stop") {
        if (!end()) check(ESP_ERR_INVALID_ARG);
        // Freeze the successful commanded pose, including lids, mid-animation.
        for (int i=0;i<kRoles;++i) {
            const auto& a=cal_.axes[i];
            if (std::isfinite(commanded_[i]) && a.low != a.high)
                pose_[i]=(commanded_[i]-a.low)/(a.high-a.low);
        }
        animation_=-1; testing_=-1; blink_until_=0; next_blink_=BlinkTime(Now()+900);
        if (cal_.axes[kLids].channel<0) next_blink_=INT64_MAX;
        for (int i=0;i<3;++i)
            if (cal_.axes[kMotion[i]].channel>=0 && !std::isfinite(commanded_[kMotion[i]])) next_blink_=INT64_MAX;
        hardware(Probe()); return Json(State());
    }
    if (verb == "engage" || verb == "calibrate") {
        if (!end()) check(ESP_ERR_INVALID_ARG);
        if (!released_) check(ESP_ERR_INVALID_STATE);
        if (verb == "engage" && !Valid(cal_,true)) check(ESP_ERR_INVALID_STATE);
        hardware(Probe());
        // Clear old per-channel pulses while global full-off and OE remain set.
        for (int ch=0;ch<16;++ch) {
            uint8_t bytes[]={uint8_t(6+4*ch),0,0,0,0x10};
            hardware(i2c_master_transmit(device_,bytes,sizeof(bytes),50));
        }
        hardware(Register(0xfd,0));
        hardware(gpio_set_level(pins_.oe,0));
        released_=false; calibrating_=verb=="calibrate"; last_error_=ESP_OK;
        // Engagement alone commands no pose; auto-blink starts after gaze/anim.
        next_blink_=INT64_MAX;
        return Json(State());
    }
    // Endpoint hunting, for a mechanism whose servos are fitted one at a time.
    // These move one axis outside its calibrated window, so they are
    // calibration-only, and every pulse stays inside the hard 1000-2000us bound.
    // Any other calibration move takes over from a running servo test.
    if (verb=="identify" || verb=="explore" || verb=="nudge" || verb=="servo" || verb=="jog") testing_=-1;
    if (verb == "test") {
        // Sweep one role, or every calibrated role base upward, through its
        // window. Calibration only: it's for checking a calibration by eye.
        std::string role;
        if (!calibrating_ || released_) check(ESP_ERR_INVALID_STATE);
        bool all=!(in>>role);
        if (!end()) check(ESP_ERR_INVALID_ARG);
        int i=all ? NextTestable(-1) : AxisIndex(role);
        if (all && i<0) check(ESP_ERR_INVALID_STATE);  // nothing calibrated yet
        if (i<0 || !Testable(i)) check(ESP_ERR_INVALID_ARG);
        hardware(Probe());
        exploring_=-1; test_all_=all; StartTest(i, Now());
        return Json(State());
    }
    if (verb == "identify") {
        int channel=-1;
        if (!calibrating_ || released_ || !(in>>channel) || !end() || channel<0 || channel>15) check(ESP_ERR_INVALID_ARG);
        hardware(Probe());
        // A small visible wiggle answers "which part is on this channel?"
        // before any role is assigned to it.
        for (int k=0;k<2;++k) {
            hardware(Pulse(channel,1440)); vTaskDelay(pdMS_TO_TICKS(180));
            hardware(Pulse(channel,1560)); vTaskDelay(pdMS_TO_TICKS(180));
        }
        hardware(Pulse(channel,1500));
        return Json(State());
    }
    if (verb == "explore") {
        std::string role;
        if (!calibrating_ || released_ || !(in>>role) || !end()) check(ESP_ERR_INVALID_ARG);
        int i=AxisIndex(role);
        if (i<0 || cal_.axes[i].channel<0) check(ESP_ERR_INVALID_ARG);
        hardware(Probe());
        explore_angle_=90; exploring_=i;
        hardware(Pulse(cal_.axes[i].channel, Micros(cal_.axes[i], explore_angle_)));
        commanded_[i]=NAN; // outside the calibrated window; not a pose
        return Json(State());
    }
    if (verb == "nudge") {
        float delta;
        if (!calibrating_ || released_ || exploring_<0 || !(in>>delta) || !end() ||
            !std::isfinite(delta) || std::abs(delta)>5) check(ESP_ERR_INVALID_ARG);
        float next=std::min(180.f, std::max(0.f, explore_angle_+delta));
        hardware(Probe());
        hardware(Pulse(cal_.axes[exploring_].channel, Micros(cal_.axes[exploring_], next)));
        explore_angle_=next;
        return Json(State());
    }
    if (verb == "mark") {
        std::string which;
        if (exploring_<0 || !(in>>which) || !end() ||
            (which!="low" && which!="high")) check(ESP_ERR_INVALID_ARG);
        auto candidate=cal_; auto& a=candidate.axes[exploring_];
        (which=="low" ? a.low : a.high)=explore_angle_;
        a.confirmed=0; // a moved endpoint has to be watched again
        if (!Valid(candidate,false)) check(ESP_ERR_INVALID_ARG);
        cal_=candidate;
        return Json(State());
    }
    if (verb == "confirm") {
        std::string role;
        if (!(in>>role) || !end()) check(ESP_ERR_INVALID_ARG);
        int i=AxisIndex(role);
        if (i<0) check(ESP_ERR_INVALID_ARG);
        auto candidate=cal_; candidate.axes[i].confirmed=1;
        if (!Valid(candidate,false)) check(ESP_ERR_INVALID_STATE);
        cal_=candidate;
        if (exploring_==i) exploring_=-1;
        return Json(State());
    }
    if (verb == "configure") {
        std::string role; int confirmed;
        Axis a;
        if (!released_ || !(in>>role>>a.channel>>a.low>>a.high>>a.min_us>>a.max_us>>a.trim_us>>confirmed) || !end()) check(ESP_ERR_INVALID_ARG);
        int i=AxisIndex(role);
        if (i<0 || (confirmed!=0 && confirmed!=1)) check(ESP_ERR_INVALID_ARG);
        a.confirmed=confirmed; auto candidate=cal_; candidate.axes[i]=a;
        if (!Valid(candidate,false)) check(ESP_ERR_INVALID_ARG);
        cal_=candidate; return Json(State());
    }
    if (verb == "fit") {
        // A servo plugged into its standard channel: assign it with a 90/90
        // window and default pulses, unconfirmed. Nothing moves; explore,
        // mark and confirm come next.
        std::string role;
        if (!released_ || !(in>>role) || !end()) check(ESP_ERR_INVALID_ARG);
        int i=AxisIndex(role);
        if (i<0) check(ESP_ERR_INVALID_ARG);
        Axis a; a.channel=pins_.channels[i];
        auto candidate=cal_; candidate.axes[i]=a;
        if (!Valid(candidate,false)) check(ESP_ERR_INVALID_ARG); // e.g. channel already taken
        cal_=candidate; return Json(State());
    }
    if (verb == "lid_coupling") {
        float trim, coeff;
        if (!released_ || !(in>>trim>>coeff) || !end() || !Unit(trim) || !Unit(coeff)) check(ESP_ERR_INVALID_ARG);
        cal_.lid_trim=trim; cal_.upper_coeff=coeff; return Json(State());
    }
    if (verb == "save") {
        // Saving moves nothing, and incremental bring-up wants each servo
        // persisted as it is finished, without leaving calibration mode.
        if ((!released_ && !calibrating_) || !end()) check(ESP_ERR_INVALID_STATE);
        check(Save()); return Json(State());
    }
    if (verb == "servo" || verb == "jog") {
        std::string role; float angle;
        if (!calibrating_ || released_ || !(in>>role>>angle) || !end() || !std::isfinite(angle)) check(ESP_ERR_INVALID_ARG);
        int i=AxisIndex(role); if (i<0) check(ESP_ERR_INVALID_ARG);
        if (verb=="jog") {
            if (std::abs(angle)>5 || !std::isfinite(commanded_[i])) check(ESP_ERR_INVALID_ARG);
            angle+=commanded_[i];
        }
        const auto& a=cal_.axes[i];
        if (a.channel<0 || angle<std::min(a.low,a.high) || angle>std::max(a.low,a.high)) check(ESP_ERR_INVALID_ARG);
        hardware(Probe()); hardware(Write(i,angle)); return Json(State());
    }
    if (verb != "gaze" && verb != "blink" && verb != "animate" && verb != "head" &&
        verb != "jaw" && verb != "ears" && verb != "lids") check(ESP_ERR_NOT_SUPPORTED);
    if (released_ || calibrating_ || !Valid(cal_,true)) check(ESP_ERR_INVALID_STATE);
    if (verb=="head" || verb=="jaw" || verb=="ears" || verb=="lids") {
        // Direct poses for the rest of the head, as fractions of each role's
        // calibrated window. Roles that aren't fitted are skipped; a command
        // with nothing fitted to move is NOT_SUPPORTED.
        auto next=pose_; bool fitted=false;
        auto arg=[&](int role, float lo, float hi) {
            float v;
            if (!(in>>v) || !std::isfinite(v) || v<lo || v>hi) check(ESP_ERR_INVALID_ARG);
            next[role]=(v-lo)/(hi-lo); fitted=fitted || cal_.axes[role].channel>=0;
        };
        if (verb=="head") { arg(0,-100,100); arg(1,-100,100); arg(2,-100,100); }
        else if (verb=="jaw") arg(6,0,100);
        else if (verb=="ears") { arg(7,0,100); arg(8,0,100); }
        else arg(kLids,0,100);
        if (!end()) check(ESP_ERR_INVALID_ARG);
        if (!fitted) check(ESP_ERR_NOT_SUPPORTED);
        hardware(Probe()); hardware(Apply(next)); pose_=next; animation_=-1;
        auto* result=State();
        cJSON_AddStringToObject(result,"action_status","commanded");
        return Json(result);
    }
    // Gaze and the eye animations point the eyes: without both eye axes
    // fitted there is nothing to point, whatever else is on the mechanism.
    int index=-1;
    if (verb == "animate") {
        std::string name; if (!(in>>name) || !end()) check(ESP_ERR_INVALID_ARG);
        for (int i=0;i<kAnimations;++i) if (name==animations[i].name) index=i;
        if (index<0) check(ESP_ERR_INVALID_ARG);
    }
    bool eyes=verb=="gaze" || (index>=0 && animations[index].eyes);
    if (eyes && (cal_.axes[kEyePan].channel<0 || cal_.axes[kEyeTilt].channel<0))
        check(ESP_ERR_NOT_SUPPORTED);
    if (index>=0 && !eyes) {
        bool fitted=false;
        for (int f=0;f<animations[index].count;++f) {
            auto t=Targets(animations[index].frames[f]);
            for (int i=0;i<kRoles;++i) fitted=fitted || (std::isfinite(t[i]) && cal_.axes[i].channel>=0);
        }
        if (!fitted) check(ESP_ERR_NOT_SUPPORTED);
    }
    if (verb == "gaze") {
        float x,y;
        if (!(in>>x>>y) || !end() || !std::isfinite(x) || !std::isfinite(y) || x< -100 || x>100 || y< -100 || y>100) check(ESP_ERR_INVALID_ARG);
        auto next=pose_;
        next[kEyePan]=(x+100)/200; next[kEyeTilt]=(y+100)/200; next[kLids]=NAN;
        hardware(Probe()); hardware(Apply(next)); pose_=next;
        animation_=-1; blink_until_=0;
        next_blink_=cal_.axes[kLids].channel>=0 ? BlinkTime(Now()) : INT64_MAX;
    } else if (verb == "blink") {
        if (!end()) check(ESP_ERR_INVALID_ARG);
        if (animation_>=0 || blink_until_) check(ESP_ERR_INVALID_STATE);
        // No lid servo fitted, nothing to blink with.
        if (cal_.axes[kLids].channel<0) check(ESP_ERR_NOT_SUPPORTED);
        // Blink only after a gaze/animation has established a commanded pose.
        for (int i=0;i<3;++i)
            if (cal_.axes[kMotion[i]].channel>=0 && !std::isfinite(commanded_[kMotion[i]])) check(ESP_ERR_INVALID_STATE);
        auto closed=pose_; closed[kLids]=0;
        hardware(Probe()); hardware(Apply(closed)); blink_until_=Now()+70;
    } else {
        hardware(Probe());
        // Establish the current logical pose synchronously so missing hardware
        // fails this MCP call. Remaining frames run on the device timer.
        hardware(Apply(pose_));
        animation_=index; frame_=0; from_=pose_; frame_start_=Now(); blink_until_=0;
    }
    auto* result=State();
    cJSON_AddStringToObject(result,"action_status",verb=="gaze" ? "commanded" : "started; poll state for later hardware faults");
    return Json(result);
}

void CogletController::Start(const CogletPins& pins) {
    pins_=pins; commanded_.fill(NAN); Load();
    last_error_=InitHardware(); ready_=last_error_==ESP_OK;
    if (!ready_) {Release(); ESP_LOGE("Coglet","PCA init: %s",esp_err_to_name(last_error_));}
    if (xTaskCreate([](void*) {
        auto& c=Instance();
        for (;;) {
            {std::lock_guard<std::mutex> lock(c.mutex_); c.Fail(c.Tick(Now()));}
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    },"coglet_motion",4096,nullptr,4,nullptr)!=pdPASS) {
        ready_=false; Fail(ESP_ERR_NO_MEM);
    }
}

bool CogletSerialCommand(const char* line) {
    if (strncmp(line,"!coglet",7) || (line[7] && line[7]!=' ')) return false;
    try {printf("\r\n%s\r\n",CogletController::Instance().Command(line[7]?line+8:"state").c_str());}
    catch(const std::exception& e) {printf("\r\nCoglet error: %s\r\n",e.what());}
    return true;
}
