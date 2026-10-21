#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "cJSON.h"

// Where the PCA9685 is wired. Each board passes its own pins, so the
// controller has no compile-time dependency on a board's config.h.
struct CogletPins {
    i2c_port_num_t i2c_port;
    gpio_num_t sda;
    gpio_num_t scl;
    gpio_num_t oe;     // PCA9685 /OE, driven high (outputs off) before setup
    uint8_t address;   // usually 0x40
    // The standard wiring: the PCA9685 channel each role plugs into, in role
    // order (base, neck_tilt, neck_roll, eye_pan, eye_tilt, lids, jaw,
    // ear_left, ear_right). "fit ROLE" assigns a role from here once its servo
    // is fitted; nothing is assigned just because it's listed.
    std::array<int8_t, 9> channels = {0, 1, 2, 3, 4, 5, 6, 7, 8};
};

// All entry points and the device timer serialize through one mutex.
// No servo feedback: cached values are successful commands only.
class CogletController {
public:
    static CogletController& Instance();
    void Start(const CogletPins& pins);
    // Every command, from MCP or the serial console; one command set.
    std::string Command(const std::string& text);
    // True while a builder is calibrating. Boards use it to keep the voice
    // quiet (mic and speaker off, MCP session up): a robot chatting through
    // the speaker makes endpoint hunting impossible.
    bool BuilderMode() const { return builder_; }
    // The animation catalogue, for tool descriptions. eyes: it moves the eyes
    // and lids; otherwise it moves the rest of the head.
    struct AnimationInfo { const char* name; bool eyes; };
    static std::vector<AnimationInfo> Animations();

private:
    struct Axis {
        int channel = -1;
        float low = 90, high = 90; // semantic endpoints; may run backwards
        int min_us = 1000, max_us = 2000, trim_us = 0;
        uint32_t confirmed = 0;
    };
    struct Calibration {
        // v2 collapsed the lid_left/lid_right pair into one shared lids axis;
        // v3 covers the whole nine-servo mechanism, neck and ears included.
        uint32_t version = 3;
        std::array<Axis, 9> axes;
        float lid_trim = .85f;
        float upper_coeff = .8f;
    } cal_;
    CogletPins pins_{};
    std::mutex mutex_;
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    bool ready_ = false, released_ = true;
    std::atomic<bool> builder_{false};
    esp_err_t last_error_ = ESP_OK, release_error_ = ESP_OK;
    std::array<float, 9> commanded_;
    // Target per role as a fraction of its calibrated window, low to high.
    // NaN means no target: the role isn't driven. A NaN lid follows the gaze.
    static constexpr std::array<float, 9> kRestPose = {NAN, NAN, NAN, .5f, .5f, NAN, NAN, NAN, NAN};
    std::array<float, 9> pose_ = kRestPose, from_ = pose_;
    int animation_ = -1, frame_ = 0;
    // Endpoint hunting: the explored axis moves outside its calibrated window,
    // so it is deliberately not reflected in commanded_.
    int exploring_ = -1;
    float explore_angle_ = 90;
    int64_t frame_start_ = 0, next_blink_ = 0, blink_until_ = 0;
    // Servo test: sweeps roles through their calibrated windows on the motion
    // timer, so release and stop cut it short.
    int testing_ = -1, test_leg_ = 0;
    bool test_all_ = false;
    int64_t test_start_ = 0;
    float test_from_ = NAN;
    esp_err_t Register(uint8_t reg, uint8_t value);
    esp_err_t Probe();
    esp_err_t InitHardware();
    esp_err_t Release();
    esp_err_t Fail(esp_err_t error);
    esp_err_t Write(int axis, float degrees);
    esp_err_t Pulse(int channel, float us);
    float Micros(const Axis& axis, float degrees) const;
    esp_err_t Apply(const std::array<float, 9>& pose);
    esp_err_t Tick(int64_t now);
    bool Testable(int axis) const;
    int NextTestable(int after) const;
    void StartTest(int axis, int64_t now);
    esp_err_t TestTick(int64_t now);
    bool Valid(const Calibration& cal, bool complete) const;
    esp_err_t Save();
    void Load();
    cJSON* State();
};

// Runs a "!coglet ..." serial line; returns false for any other line.
bool CogletSerialCommand(const char* line);
