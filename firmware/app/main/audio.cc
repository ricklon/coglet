#include "audio.h"

#include "board.h"
#include "settings.h"

#include <driver/i2s_std.h>
#include <encoder/impl/esp_opus_enc.h>
#include <decoder/impl/esp_opus_dec.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>

#define TAG "audio"

namespace audio {
namespace {

constexpr int kRate = AUDIO_SAMPLE_RATE;
constexpr int kMicSamples = kRate / 1000 * kMicFrameMs;
constexpr size_t kMaxEncodeFrames = 4;    // 240 ms of mic backlog
constexpr size_t kMaxDecodePackets = 100; // ~2 s of 20 ms speech
constexpr size_t kMaxPlayFrames = 6;
constexpr int kMaxDecodeSamples = kRate / 1000 * 120;  // longest Opus frame

i2s_chan_handle_t s_tx = nullptr, s_rx = nullptr;
void* s_encoder = nullptr;
void* s_decoder = nullptr;
int s_decoder_ms = 0;
int s_encoder_out_bytes = 0;
Sender s_sender;

std::mutex s_mutex;
std::condition_variable s_cv;
std::deque<std::vector<int16_t>> s_encode;
std::deque<std::vector<uint8_t>> s_decode;
std::deque<std::vector<int16_t>> s_play;
bool s_playing = false;  // speaker task holds a frame

std::atomic<bool> s_mic_on{false};
std::atomic<bool> s_muted{false};
std::atomic<float> s_gain{1.0f};
std::atomic<int> s_volume{70};
std::atomic<int> s_downlink_ms{60};
std::atomic<int> s_peak{0};

esp_opus_dec_frame_duration_t DecoderDuration(int ms) {
    switch (ms) {
        case 10: return ESP_OPUS_DEC_FRAME_DURATION_10_MS;
        case 20: return ESP_OPUS_DEC_FRAME_DURATION_20_MS;
        case 40: return ESP_OPUS_DEC_FRAME_DURATION_40_MS;
        case 60: return ESP_OPUS_DEC_FRAME_DURATION_60_MS;
        case 80: return ESP_OPUS_DEC_FRAME_DURATION_80_MS;
        case 100: return ESP_OPUS_DEC_FRAME_DURATION_100_MS;
        case 120: return ESP_OPUS_DEC_FRAME_DURATION_120_MS;
        default: return ESP_OPUS_DEC_FRAME_DURATION_INVALID;
    }
}

void InitI2s() {
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 6;
    chan.dma_frame_num = 240;
    chan.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &s_tx, &s_rx));

    // Same slot setup as the fork's NoAudioCodecDuplex, which runs on this
    // wiring: 32-bit mono, left slot.
    i2s_std_config_t std = {};
    std.clk_cfg.sample_rate_hz = kRate;
    std.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    std.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    std.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    std.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_32BIT;
    std.slot_cfg.bit_shift = true;
    std.slot_cfg.left_align = true;
    std.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std.gpio_cfg.bclk = AUDIO_I2S_GPIO_BCLK;
    std.gpio_cfg.ws = AUDIO_I2S_GPIO_WS;
    std.gpio_cfg.dout = AUDIO_I2S_GPIO_DOUT;
    std.gpio_cfg.din = AUDIO_I2S_GPIO_DIN;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
}

void OpenDecoder(int ms) {
    if (s_decoder != nullptr) {
        esp_opus_dec_close(s_decoder);
        s_decoder = nullptr;
    }
    esp_opus_dec_cfg_t config = {
        .sample_rate = (uint32_t)kRate,
        .channel = ESP_AUDIO_MONO,
        .frame_duration = DecoderDuration(ms),
        .self_delimited = false,
    };
    auto err = esp_opus_dec_open(&config, sizeof(config), &s_decoder);
    if (s_decoder == nullptr) {
        ESP_LOGE(TAG, "Opus decoder open failed: %d", err);
    }
    s_decoder_ms = ms;
}

void OpenEncoder() {
    esp_opus_enc_config_t config = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = ESP_OPUS_BITRATE_AUTO,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,  // the cheapest setting; the C3 is single core
        .enable_fec = false,
        .enable_dtx = true,
        .enable_vbr = true,
    };
    auto err = esp_opus_enc_open(&config, sizeof(config), &s_encoder);
    if (s_encoder == nullptr) {
        ESP_LOGE(TAG, "Opus encoder open failed: %d", err);
        return;
    }
    int in_bytes = 0;
    esp_opus_enc_get_frame_size(s_encoder, &in_bytes, &s_encoder_out_bytes);
    if (in_bytes != kMicSamples * (int)sizeof(int16_t)) {
        ESP_LOGE(TAG, "Unexpected encoder frame: %d bytes", in_bytes);
    }
}

// Reads the mic continuously so the RX DMA never overruns; frames are only
// queued for encoding while the mic is enabled.
void MicTask(void*) {
    std::vector<int32_t> raw(kMicSamples);
    int peak = 0, counted = 0;
    while (true) {
        size_t bytes = 0;
        if (i2s_channel_read(s_rx, raw.data(), raw.size() * sizeof(int32_t), &bytes,
                             pdMS_TO_TICKS(200)) != ESP_OK) {
            continue;
        }
        int samples = bytes / sizeof(int32_t);
        std::vector<int16_t> pcm(samples);
        float gain = s_gain;
        bool muted = s_muted;
        for (int i = 0; i < samples; i++) {
            float v = muted ? 0 : (raw[i] >> 12) * gain;
            v = v > INT16_MAX ? INT16_MAX : v < -INT16_MAX ? -INT16_MAX : v;
            pcm[i] = (int16_t)v;
            peak = std::max(peak, std::abs((int)pcm[i]));
        }
        counted += samples;
        if (counted >= kRate) {
            s_peak = peak;
            peak = counted = 0;
        }
        if (!s_mic_on || samples != kMicSamples) continue;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_encode.size() >= kMaxEncodeFrames) s_encode.pop_front();
            s_encode.push_back(std::move(pcm));
        }
        s_cv.notify_all();
    }
}

void CodecTask(void*) {
    std::vector<uint8_t> packet;
    while (true) {
        std::unique_lock<std::mutex> lock(s_mutex);
        s_cv.wait(lock, [] {
            return !s_encode.empty() || (!s_decode.empty() && s_play.size() < kMaxPlayFrames);
        });

        if (!s_decode.empty() && s_play.size() < kMaxPlayFrames) {
            auto opus = std::move(s_decode.front());
            s_decode.pop_front();
            lock.unlock();
            s_cv.notify_all();

            if (s_downlink_ms != s_decoder_ms) OpenDecoder(s_downlink_ms);
            std::vector<int16_t> pcm(kMaxDecodeSamples);
            esp_audio_dec_in_raw_t in = {
                .buffer = opus.data(),
                .len = (uint32_t)opus.size(),
                .consumed = 0,
                .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
            };
            esp_audio_dec_out_frame_t out = {};
            out.buffer = (uint8_t*)pcm.data();
            out.len = pcm.size() * sizeof(int16_t);
            esp_audio_dec_info_t info = {};
            if (s_decoder != nullptr &&
                esp_opus_dec_decode(s_decoder, &in, &out, &info) == ESP_AUDIO_ERR_OK) {
                pcm.resize(out.decoded_size / sizeof(int16_t));
                lock.lock();
                s_play.push_back(std::move(pcm));
                lock.unlock();
                s_cv.notify_all();
            } else {
                ESP_LOGW(TAG, "Dropped an undecodable packet (%u bytes)", (unsigned)opus.size());
            }
            lock.lock();
        }

        if (!s_encode.empty()) {
            auto pcm = std::move(s_encode.front());
            s_encode.pop_front();
            lock.unlock();
            if (s_encoder == nullptr || !s_mic_on) continue;
            packet.resize(s_encoder_out_bytes);
            esp_audio_enc_in_frame_t in = {
                .buffer = (uint8_t*)pcm.data(),
                .len = (uint32_t)(pcm.size() * sizeof(int16_t)),
            };
            esp_audio_enc_out_frame_t out = {};
            out.buffer = packet.data();
            out.len = packet.size();
            if (esp_opus_enc_process(s_encoder, &in, &out) == ESP_AUDIO_ERR_OK &&
                out.encoded_bytes > 0 && s_sender) {
                s_sender(packet.data(), out.encoded_bytes);
            }
        }
    }
}

void WritePcm(const std::vector<int16_t>& pcm) {
    // Volume follows the fork's curve: (volume/100)^2, applied into the top
    // 16 bits of a 32-bit slot.
    int32_t factor = std::pow(s_volume / 100.0, 2) * 65536;
    std::vector<int32_t> buffer(pcm.size());
    for (size_t i = 0; i < pcm.size(); i++) {
        int64_t v = (int64_t)pcm[i] * factor;
        buffer[i] = v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v;
    }
    size_t written = 0;
    i2s_channel_write(s_tx, buffer.data(), buffer.size() * sizeof(int32_t), &written, portMAX_DELAY);
}

// The amp is only clocked while there's something to say, and is switched off
// again after two quiet seconds so it doesn't hiss between replies.
void SpeakerTask(void*) {
    bool enabled = false;
    while (true) {
        std::unique_lock<std::mutex> lock(s_mutex);
        bool ready = s_cv.wait_for(lock, std::chrono::seconds(2), [] { return !s_play.empty(); });
        if (!ready) {
            lock.unlock();
            if (enabled) {
                i2s_channel_disable(s_tx);
                enabled = false;
            }
            continue;
        }
        auto pcm = std::move(s_play.front());
        s_play.pop_front();
        s_playing = true;
        lock.unlock();
        s_cv.notify_all();

        if (!enabled) {
            i2s_channel_enable(s_tx);
            enabled = true;
        }
        WritePcm(pcm);

        lock.lock();
        s_playing = false;
        lock.unlock();
        s_cv.notify_all();
    }
}

}  // namespace

void Start(Sender sender) {
    s_sender = std::move(sender);
    s_volume = settings::GetInt("audio", "output_volume", 70);
    if (s_volume <= 0) s_volume = 10;  // a stored zero would make the robot mute forever
    InitI2s();
    OpenEncoder();
    OpenDecoder(s_downlink_ms);
    xTaskCreate(MicTask, "audio_mic", 4096, nullptr, 8, nullptr);
    // Opus needs a deep stack; the fork gives its codec task the same 24 KB.
    xTaskCreate(CodecTask, "audio_codec", 2048 * 12, nullptr, 2, nullptr);
    xTaskCreate(SpeakerTask, "audio_spk", 4096, nullptr, 4, nullptr);
}

void SetMicEnabled(bool on) {
    s_mic_on = on;
    if (!on) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_encode.clear();
    }
}

void SetDownlinkFrameMs(int ms) {
    if (DecoderDuration(ms) == ESP_OPUS_DEC_FRAME_DURATION_INVALID) {
        ESP_LOGW(TAG, "Hub frame duration %d ms not supported; keeping %d", ms, (int)s_downlink_ms);
        return;
    }
    s_downlink_ms = ms;
}

void PushOpus(const uint8_t* data, size_t size) {
    std::unique_lock<std::mutex> lock(s_mutex);
    if (!s_cv.wait_for(lock, std::chrono::seconds(3),
                       [] { return s_decode.size() < kMaxDecodePackets; })) {
        ESP_LOGW(TAG, "Playback stalled; dropping a packet");
        return;
    }
    s_decode.emplace_back(data, data + size);
    lock.unlock();
    s_cv.notify_all();
}

void FlushPlayback() {
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_decode.clear();
        s_play.clear();
    }
    s_cv.notify_all();
}

bool PlaybackIdle() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_decode.empty() && s_play.empty() && !s_playing;
}

void PlayTestTone() {
    // 440 Hz for 300 ms, faded at both ends so the amp doesn't click.
    constexpr int kSamples = kRate * 3 / 10;
    constexpr int kChunk = kRate / 50;
    for (int start = 0; start < kSamples; start += kChunk) {
        std::vector<int16_t> pcm(kChunk);
        for (int i = 0; i < kChunk; i++) {
            int n = start + i;
            float envelope = std::min(1.0f, std::min(n, kSamples - n) / (kRate * 0.02f));
            pcm[i] = (int16_t)(12000 * envelope * std::sin(2 * M_PI * 440 * n / kRate));
        }
        std::unique_lock<std::mutex> lock(s_mutex);
        s_play.push_back(std::move(pcm));
        lock.unlock();
        s_cv.notify_all();
    }
}

int Volume() {
    return s_volume;
}

void SetVolume(int volume) {
    s_volume = std::max(0, std::min(100, volume));
    settings::SetInt("audio", "output_volume", s_volume);
}

bool MicMuted() {
    return s_muted;
}

void SetMicMuted(bool muted) {
    s_muted = muted;
}

float MicGain() {
    return s_gain;
}

void SetMicGain(float gain) {
    s_gain = std::max(0.0f, std::min(64.0f, gain));
}

int MicPeak() {
    return s_peak;
}

}  // namespace audio
