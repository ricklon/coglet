# Decisions

Why Coglet is the way it is. Newest first.

## 2026-10-27: The S3 waits for a wake word with the session open

agent-hub only has a device's MCP tools while its voice session is open, so
closing the session between conversations would make the robot unreachable.
The S3 keeps the session open and gates only the mic: it waits for
"Computer", listens, and goes back to waiting 12 s after the last voice it
heard. The C3 has no wake word engine and stays always listening. The wake
word is one setting in the fork's `boards/coglet/sdkconfig.defaults`.

## 2026-10-27: The S3 detects its camera

The S3 Sense has shipped with different sensors (OV2640 or OV3660),
and a module can be swapped. The firmware accepts whichever supported sensor
answers and logs it, instead of staying off until a build confirms the model.
menuconfig can still pin one model, or turn the camera off. The camera's I2C
port and clock channel were also moved off the servo bus and the LED, which
had kept it from starting at all.

## 2026-10-27: Builder mode is calibration mode, and it keeps the session

The mode that turns servo outputs on for endpoint hunting and servo tests is
now `calibrate` (`!coglet calibrate`, `self.coglet.calibrate`; `state` reports
`calibrating`). "Builder" said who used it, not what it does.

Calibration has to work before there's a hub: a robot is flashed and
calibrated over USB first, then pointed at agent-hub. The serial console runs
the same commands, so that path needs no network. Once the hub is connected,
the same mode is there over MCP.

That only works if entering the mode over MCP leaves MCP reachable. The S3
used to close the voice session when quiet mode started, leaving the servos
powered and no MCP until someone typed `!quiet off`. Both boards now keep the
session open while quiet; the S3 reopens a dropped session without listening
and has `self.voice.quiet`.

## 2026-10-20: MCP is the robot's only interface

Coglet is an MCP device for agent-hub, Claude and other MCP clients, so every
command is an MCP tool: motion for all nine servos, engage and calibration mode,
calibration, servo tests, diagnostics, audio and device settings. The serial
console runs the same commands through the same code and exists for a robot
that isn't on a network yet. The builder web page, a second interface, is gone.

This replaces "the LLM can stop the robot but never start it". What still
holds:

- Every boot is released, and `engage` commands no pose.
- Every position stays inside each servo's calibrated window; calibration
  moves stay inside the hard 1000–2000 µs bound.
- `stop` and `release` always work, and a hardware fault latches release.
- The servo-rail switch is the stop that doesn't depend on software. Anything
  with hub access can now engage the robot.

Quiet mode (which calibration mode turns on) now keeps the voice session open
with the mic and speaker off, because MCP rides inside that session. (The S3
caught up on 2026-10-27, above.)

New motion: `head` (base pan, neck tilt, neck roll), `jaw`, `ears` and `lids`,
and animations for the rest of the head (`nod`, `shake`, `tilt`, `perk`,
`droop`, `talk`, `look_around`). The calibration command that sets lid
coupling is now `lid_coupling`, since `lids` moves the lids.

## 2026-10-20: One pad map for every board, set by the S3

Every Coglet is wired pad-for-pad the same, so a robot can take either a C3 or
an S3 without rewiring; each board's firmware maps the pads to its GPIOs. The
S3 is the main board, so its layout is the reference: D0 amp DIN, D1 BCLK,
D2 WS, D3 mic SD, D4 SDA, D5 SCL, D10 /OE. The C3 bench robot had D2/D3 and
D4/D5 the other way round and has to be rewired once.

## 2026-10-20: One standard servo wiring, assigned as servos are fitted

Every Coglet plugs role *n* into PCA9685 channel *n* (base 0 … ear_right 8).
The table is in the firmware, but listing a channel doesn't assign it:
`engage` needs every assigned role confirmed, and a partly built robot is
normal, so a role is assigned only when a person runs `!coglet fit ROLE` after
fitting the servo. `configure` remains for anything wired differently.

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
- Left out of the first version: OTA firmware updates (the Web Programmer does
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

- This repo is the product home: hardware, docs, controller, host tests, Web
  Programmer and releases.
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
- Coglet left the fork's supported-board matrix and web flasher. Its Web
  Programmer and releases live here, and the fork's web flasher links to this
  repo.

## Conversation tools can stop the robot but never start it (superseded 2026-10-20)

Superseded by "MCP is the robot's only interface", above. Kept for history:

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

## Calibration mode silences the voice, and leaving it doesn't bring the voice back

(Called builder mode until 2026-10-27. Since 2026-10-20, quiet keeps the
session open for MCP.)

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
