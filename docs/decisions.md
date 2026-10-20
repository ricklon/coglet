# Decisions

Why Coglet is the way it is. Newest first.

## 2026-10-20: The C3 runs Coglet's own firmware, not the xiaozhi fork

The fork carried about 40 unrelated boards, a second build system, and a
submodule pointer that had to be bumped for every change. The fork's Coglet
layer was a few hundred lines, but the rest of xiaozhi was a dependency
Coglet only used a slice of.

**What we chose:** `firmware/app`, a plain ESP-IDF app that still speaks the
**xiaozhi wire protocol**: check-in at `/xiaozhi/ota/`, the WebSocket hello,
Opus audio, and MCP JSON-RPC inside the socket. agent-hub needs no changes,
and a fork-built robot and an app-built one look the same to the hub.

**Alternatives considered:**

- *A new Coglet protocol* (raw PCM, first-class robot events). Smaller
  firmware, but agent-hub would need a second device ingress, and raw 16 kHz
  PCM is about 256 kbit/s each way: fine on a LAN, poor to a remote hub.
- *Servo-only MCP over HTTP, like eyemech.* Simplest firmware, but Coglet
  would stop being a robot you talk to.

**Details:**

- Opus comes from `espressif/esp_audio_codec`, the library the fork used, with
  the same encoder settings (16 kHz, 60 ms, complexity 0). It's a prebuilt,
  Espressif-only component pinned in `main/idf_component.yml`. The decoder
  outputs 16 kHz directly from the hub's 24 kHz stream, so there's no
  resampler.
- The controller is now also an IDF component (`coglet_controller/CMakeLists.txt`).
  The fork still compiles it as plain sources, and the host tests don't use
  either.
- NVS stays where the fork's 4 MB table put it, with the fork's namespaces and
  keys, so a robot moved off the fork should keep its Wi-Fi, hub URL, client id,
  volume and calibration.
- Left out of the first version: OTA firmware updates (the web flasher does
  that), the SoftAP Wi-Fi setup page (use `!wifi`), wake word, and serial text
  chat (agent-hub ignores it).
- The S3 stays on the fork until its camera and board are ported. The
  submodule is kept for it.

## 2026-10-13: Coglet gets its own repo, and the xiaozhi fork stays the build engine

Coglet started as two board profiles inside the xiaozhi-esp32 fork, with its
docs spread across two board READMEs, two docs files, Kconfig and CMake.

**Why not a standalone firmware like eyemech-esp32-xiao?** Eyemech is a
voiceless mechanism that serves MCP over HTTP by itself. Coglet is a voice
agent, and agent-hub is a xiaozhi server: the check-in, the voice WebSocket,
audio, OTA, and MCP discovery inside that WebSocket all come from xiaozhi.
Going standalone would mean rewriting all of that, or dropping voice.

**Why not copy the whole fork as "coglet"?** It would bring along about 40
unrelated boards and make every upstream merge painful.

**What we did instead:**

- This repo is the product home: hardware, docs, controller, host tests, web
  flasher and releases.
- The fork is a submodule at `firmware/xiaozhi-esp32`. It keeps only what
  xiaozhi's build registry requires: the `coglet` and `coglet-c3` board
  folders, their Kconfig and CMake entries, the serial-command hook, and
  `boards/common/coglet_mcp_tools.cc`.
- The controller has no compile-time dependency on xiaozhi. Pins arrive at
  runtime as `CogletPins`, and the MCP tool registration lives in the fork.
  It depends only on ESP-IDF (I²C, GPIO, NVS, httpd, cJSON), which is also why
  it can be host-tested against a fake IDF.
- The fork's CMake compiles the controller from this repo as extra sources,
  not as an IDF component. The xiaozhi project uses `MINIMAL_BUILD`, so a
  component would need a Kconfig-conditional entry in `main`'s requirements.
  Plain sources avoid that.
- Coglet left the fork's supported-board matrix and web flasher. Its flasher
  and releases live here, and the fork's flasher links to this repo.

## Conversation tools can stop the robot but never start it

Only `gaze`, `blink`, `animate`, `stop`, `release` and `state` are MCP tools.
Engaging, mapping, calibrating and raw servo moves are local only, through
serial or the builder page. An LLM that misreads a situation can always make
things safer, but never less safe. `release` from MCP is therefore a one-way
door until a person engages again.

## Every boot is released

No stored calibration, mode or flag makes the robot move at boot. Engaging
commands no pose, and auto-blink starts only after an explicit gaze or
animation. A brown-out or watchdog reset in the middle of the night must never
lead to motion.

## Builder mode silences the voice, and leaving it doesn't bring the voice back

A robot answering room noise makes endpoint hunting impossible. An earlier
auto-resume let the LLM call `self.coglet.release` and talk its way out of a
calibration session. Quiet now ends only on `!quiet off`.

## Nine roles, one shared lid servo (confirmed 2026-09-15)

`base`, `neck_tilt`, `neck_roll`, `eye_pan`, `eye_tilt`, `lids`, `jaw`,
`ear_left`, `ear_right`. One servo drives both top lids, there are no lower
lids, and the ears are independent. Duplicate channels must never simulate
coupling. If the mechanism changes, change the role model.

## Motion adapted from Eyemech, not copied

The keyframes, durations, interpolation and upper-lid coupling come from
eyemech-esp32-xiao's `components/eye_motion/eye_motion.c`. These differ:

- Eyemech's lower-lid coefficient is dropped, since there are no lower lids.
- `stop` freezes the pose. `release` latches until a person engages.
- Every I²C error propagates, and a fault partway through an animation latches
  release.
- Only successful writes update the commanded-position cache, and faults
  invalidate it.
- There's no Grove Vision subsystem. Networking belongs to the firmware
  around the controller (`firmware/app` on the C3, xiaozhi on the S3).
- Eyemech's measured angles and channel assignment were never carried over.
  They belong to a different mechanism.

## Pulse range defaults are placeholders

1000–2000 µs with 0–180° mapped linearly. These aren't measurements, and the
fitted servo's real range is still unknown. Every pulse is clamped to that
hard bound until a servo is characterised.
