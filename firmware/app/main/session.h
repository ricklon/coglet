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
// later. The session is also the robot's MCP connection, so it stays open in
// quiet mode, with the mic and speaker off. Everything runs on one session
// task; the public functions only post work to it.
namespace session {

enum class State { Offline, Idle, Connecting, Listening, Speaking, Paused };

void Start();
void NetworkUp();
void NetworkDown();

// Quiet mode: no listening and no speaking until turned off; MCP stays up.
// Entering builder mode turns it on; it stays on after builder mode ends.
void SetQuiet(bool on);
bool Quiet();
// Stop a reply that's playing (the boot button).
void Interrupt();
// Close the current session. It reopens on its own.
void Restart();

State GetState();
const char* StateName(State state);
bool CheckedIn();
std::string WebsocketUrl();

}  // namespace session
