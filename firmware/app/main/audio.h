#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// Duplex I2S (INMP441 mic, MAX98357A amp) and Opus, both at 16 kHz mono.
//
//   mic task   -> PCM frames  -> codec task (encode) -> sender (WebSocket)
//   WebSocket  -> Opus packets -> codec task (decode) -> speaker task -> amp
//
// The hub sends 24 kHz Opus. The decoder outputs 16 kHz directly, which Opus
// supports for any stream, so there's no resampler.
namespace audio {

constexpr int kMicFrameMs = 60;  // what the hello advertises

using Sender = std::function<void(const uint8_t* data, size_t size)>;

void Start(Sender sender);

// While on, mic frames are encoded and handed to the sender.
void SetMicEnabled(bool on);
// Packet duration the hub announced in its hello.
void SetDownlinkFrameMs(int ms);
// Queue one Opus packet for playback. Blocks briefly when the queue is full,
// which pushes back on the WebSocket instead of dropping speech.
void PushOpus(const uint8_t* data, size_t size);
// Drop everything queued for the speaker.
void FlushPlayback();
// True when nothing is queued or playing.
bool PlaybackIdle();
// A short tone, for checking the amp.
void PlayTestTone();

int Volume();            // 0..100, persisted
void SetVolume(int volume);
bool MicMuted();
void SetMicMuted(bool muted);
float MicGain();
void SetMicGain(float gain);
int MicPeak();           // peak sample level over the last second, 0..32767

}  // namespace audio
