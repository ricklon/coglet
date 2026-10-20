# Coglet

Coglet is a small talking robot head built to be an **MCP device for
[agent-hub](https://github.com/ricklon/agent-hub)**. It has a three-axis neck,
two-axis eyes, a shared top-lid servo, a jaw and two ears: nine hobby servos on
a PCA9685. There's a microphone and speaker for voice, and an optional camera on
the S3 build. The brain is a Seeed XIAO ESP32. The C3 runs this repo's own firmware, which
speaks the [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) protocol that
agent-hub serves; the S3 still runs a fork of xiaozhi-esp32 itself.

You don't program Coglet directly. It checks in with agent-hub, opens a voice
session, and advertises its abilities as MCP tools. The hub's persona hears what
people say, decides what to do, and calls `self.coglet.gaze`,
`self.coglet.animate` and the rest on the robot.

```
 person ──voice──▶ Coglet ──xiaozhi WebSocket (audio + MCP)──▶ agent-hub ──▶ LLM persona
                    ▲                                              │
                    └──────────── tools/call self.coglet.* ◀───────┘
```

## Status

| | |
|---|---|
| Coglet C3 (XIAO ESP32-C3) | On the bench, talking to agent-hub on the fork firmware; servos being fitted and calibrated from the base upward. The new `firmware/app` builds but hasn't run on the robot yet |
| Coglet S3 (XIAO ESP32-S3 Sense) | Builds. Camera stays off until the fitted module is confirmed. Not yet physically qualified |
| Mechanism | Nine roles defined (confirmed 2026-09-15). Servos fitted one at a time; a partly built robot is normal |

No servo gives position feedback. Every position the firmware reports is the
last one it *commanded*, not a measurement. The **servo-rail switch is the
emergency stop**.

## Quick start

1. **Wire it.** See [hardware/WIRING.md](hardware/WIRING.md). The Fritzing
   schematic is [hardware/esp32c3-voice-agent.fzz](hardware/esp32c3-voice-agent.fzz).
2. **Flash it.** Servo supply off. Use the web flasher
   (Chrome or Edge), or build from source:
   ```bash
   just build c3     # firmware/app
   just flash c3 /dev/ttyACM0
   just setup        # S3 only: fetch the xiaozhi-esp32 fork (submodule)
   just build s3
   ```
   Details, including how to run the flasher locally, are in
   [docs/FLASHING.md](docs/FLASHING.md).
3. **Connect it to agent-hub.** On the serial console:
   ```
   !wifi YourSSID YourPassword
   !server http://HUB_HOST:8003/xiaozhi/ota/
   ```
   It shows up on the hub dashboard with its `self.coglet.*` tools. See
   [docs/AGENT-HUB.md](docs/AGENT-HUB.md).
4. **Calibrate before anything moves.** Every boot is released with no roles
   mapped. Map and measure each servo locally:
   [docs/CALIBRATION.md](docs/CALIBRATION.md).

## MCP tools

What agent-hub (and its LLM) can call:

| Tool | Does |
|---|---|
| `self.coglet.gaze` | Point the eyes, `horizontal`/`vertical` −100..100 inside the calibrated endpoints, with upper-lid tracking |
| `self.coglet.blink` | One device-timed blink |
| `self.coglet.animate` | `look`, `roll`, `side_eye`, `wink`, `surprise`, `sleepy` |
| `self.coglet.stop` | Freeze the current pose |
| `self.coglet.release` | Latch all servo outputs off |
| `self.coglet.state` | Diagnostics, calibration, last commanded positions, errors |

Plus `self.get_device_status` and `self.audio_speaker.set_volume` and, on the S3
with a confirmed camera, the camera. **Engaging, calibrating and raw servo moves aren't
MCP tools.** The LLM can stop the robot, but only a person at the serial console
or the builder page can make it move again.

## Repository layout

```
firmware/
  app/                 C3 firmware: Wi-Fi, agent-hub check-in, voice session, Opus, MCP, console
  coglet_controller/   servo controller: PCA9685, calibration, motion, builder page
  tests/               host tests: the real controller against a fake ESP-IDF
  xiaozhi-esp32/       submodule: the xiaozhi fork, still used for the S3
hardware/              Fritzing schematic, wiring and parts
web-flasher/           browser flasher (esp-web-tools) and serial console
scripts/               firmware packaging for the flasher and releases
tools/                 calibration restore-command generator
docs/                  agent-hub, operation, calibration, flashing, qualification
```

On the C3, everything is in this repo: `firmware/app` speaks agent-hub's
xiaozhi protocol itself, with Opus from Espressif's `esp_audio_codec`. On the
S3, the voice stack, Wi-Fi, OTA and MCP server still come from the xiaozhi fork,
which holds a thin board layer. [docs/decisions.md](docs/decisions.md) explains
both.

## Documentation

| Doc | What it covers |
|---|---|
| [docs/AGENT-HUB.md](docs/AGENT-HUB.md) | **Start here.** Check-in, MCP discovery, the tools, listen mode, driving Coglet from Claude Code |
| [hardware/WIRING.md](hardware/WIRING.md) | Parts, pin maps for C3 and S3, power, `/OE`, bench checks |
| [docs/FLASHING.md](docs/FLASHING.md) | Web flasher, CLI builds, packaging, releases, backups before reflashing |
| [docs/OPERATION.md](docs/OPERATION.md) | Serial commands, release/engage/builder, the builder web page, quiet mode |
| [docs/CALIBRATION.md](docs/CALIBRATION.md) | One-servo-at-a-time calibration, backup and restore |
| [docs/QUALIFICATION.md](docs/QUALIFICATION.md) | Staged physical bring-up checklist and the verification record |
| [docs/decisions.md](docs/decisions.md) | Why things are the way they are |
| [docs/roadmap.md](docs/roadmap.md) | What's done and what's next |
| [AGENTS.md](AGENTS.md) | Guidance for coding agents working in this repo |

## Related projects

- [agent-hub](https://github.com/ricklon/agent-hub): the server Coglet is built for.
- [ricklon/xiaozhi-esp32](https://github.com/ricklon/xiaozhi-esp32): the
  firmware fork, carried here as a submodule for the S3. The C3 firmware
  follows its protocol and storage layout.
- [eyemech-esp32-xiao](https://github.com/ricklon/eyemech-esp32-xiao): the
  animatronic eye project Coglet's motion keyframes and lid coupling were
  adapted from. Eyemech serves MCP over HTTP by itself, with no voice. Coglet
  rides the xiaozhi voice session instead.

## License

TBD for this repo's code. The xiaozhi-esp32 submodule is MIT licensed, and
`firmware/app` follows its wire protocol and storage layout. Espressif's
`esp_audio_codec` is under the Espressif Modified MIT license, for use on
Espressif chips only.
