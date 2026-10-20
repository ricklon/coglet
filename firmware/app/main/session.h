#pragma once

#include <string>

// The voice session with agent-hub, in the xiaozhi wire protocol:
//
//   check-in (HTTP) -> WebSocket -> hello / hello -> listen start
//   mic Opus up ......................... hub VAD ends the utterance
//   tts start, Opus down, tts stop ...... mic is off while the reply plays
//   mcp {initialize, tools/list, tools/call} at any time
//
// The robot is always listening: a dropped session reopens about a second
// later unless quiet mode is on. Everything runs on one session task; the
// public functions only post work to it.
namespace session {

enum class State { Offline, Idle, Connecting, Listening, Speaking, Paused };

void Start();
void NetworkUp();
void NetworkDown();

// Quiet mode: no session, no listening, no speaking, until turned off.
// Entering builder mode turns it on; only a person turns it off.
void SetQuiet(bool on);
bool Quiet();
// Stop a reply that's playing (the boot button).
void Interrupt();
// Close the current session. It reopens on its own unless quiet.
void Restart();

State GetState();
const char* StateName(State state);
bool CheckedIn();
std::string WebsocketUrl();

}  // namespace session
