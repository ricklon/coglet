# AGENTS.md

Guidance for coding agents working in this repo.

## What this is

Coglet is a voice-and-servo robot that **agent-hub drives as an MCP device**.
The C3 firmware is this repo's own ESP-IDF app (`firmware/app`), which speaks
agent-hub's xiaozhi wire protocol, plus the servo controller
(`firmware/coglet_controller`). The S3 still builds from the xiaozhi-esp32 fork
(submodule `firmware/xiaozhi-esp32`). Read
[README.md](README.md) and [docs/decisions.md](docs/decisions.md) first.

## Where things live

| Change | Where |
|---|---|
| Servo behaviour, calibration, `!coglet` commands, builder page | `firmware/coglet_controller/` (this repo) |
| Host tests and the fake IDF | `firmware/tests/` |
| C3: MCP tools, session, audio, Wi-Fi, console, quiet logic | `firmware/app/main/` |
| C3 pins | `firmware/app/main/board.h` (then update `hardware/WIRING.md`) |
| S3: MCP tool names, descriptions and arguments | fork: `main/boards/common/coglet_mcp_tools.cc` |
| S3 pins | fork: `main/boards/coglet/config.h` (then update `hardware/WIRING.md`) |
| S3 board startup, audio, camera, auto-reconnect, quiet logic | fork: `main/boards/coglet/*.cc` |
| Flasher page, packaging, releases | `web-flasher/`, `scripts/package_firmware.py`, `.github/workflows/` |

A tool change on one board should be mirrored on the other. Changes inside
the fork are committed and pushed there, and then the submodule
pointer here is bumped. Never edit the submodule without saying so.

## Commands

```bash
just test                 # always run after touching the controller
just build c3             # firmware/app; ESP-IDF 5.5.x
just build s3             # the fork
XIAOZHI_DIR=../xiaozhi-esp32 just build s3   # against a sibling fork checkout
just package              # firmware into web-flasher/
```

## Rules

- **Never flash, open a serial port, or call motion tools on a real robot
  unless the user asks you to in this conversation.** Opening a port can
  reset the board. Ask whether the servo rail is off first.
- **Don't add MCP tools that engage, calibrate, or move raw servos.** The LLM
  may stop the robot but never start it. See decisions.md.
- **Every boot stays released.** Don't add startup motion.
- **Positions are commands, not measurements.** Don't write docs or tool
  descriptions that claim otherwise.
- **Don't record physical verification you didn't watch.** A build or host
  test isn't a qualification stage. See docs/QUALIFICATION.md.
- **No secrets in the repo or in public firmware.** Enrollment tokens, Wi-Fi
  credentials, raw flash dumps. The packager refuses token-bearing builds.
- Keep the docs in step with the code: tool table in README.md and
  docs/AGENT-HUB.md, commands in docs/OPERATION.md, pins in hardware/WIRING.md.
