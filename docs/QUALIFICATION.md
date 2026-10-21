# Physical qualification

How to take a Coglet from "it compiles" to "it is safe to leave talking to
agent-hub". Record date, firmware hash (`metadata.json` or the build's
`xiaozhi.bin` SHA-256), board/servo/camera identities, calibration export hash,
supply voltage and current rating, and observations for every stage.

**A compile or a mocked host test is an automated check, not physical
evidence.** Don't record a stage as passed unless someone watched it.

## Stages

Record date, firmware hash, board/servo/camera identities, calibration export
hash, supply voltage/current rating and observations for every stage. A compile
or mocked test is an automated check, **not** physical evidence.

1. **Power-off inspection and backup:** verify channels, common ground, rail
   polarity, actual /OE wiring/pull-up, target identity and verified backup.
   With V+ off, boot/reset and confirm released state, no automatic engagement.
   Scope /OE and PWM across warm/cold reset before servo-powered tests.
2. **Speaker alone, V+ off:** set low volume, run `!speaker test`; record audible
   chime, distortion and supply behavior. A successful I²S write is insufficient.
3. **Microphone alone, V+ off:** use `!mic` diagnostics and a voice session;
   record nonzero changing capture levels and intelligible transcription of a
   known phrase. Check mute/unmute. Software levels do not prove intelligibility.
4. **Camera, V+ off:** confirm fitted model, select/build its configuration, then
   capture with `!camera` and camera MCP. Record logged PID and an inspected
   image, resolution, orientation and successful Hub image handling.
5. **Individual servos:** one unloaded channel at a time, builder mode, known
   conservative pulse/window, hand on the rail switch. Confirm role/direction
   with one 2° step, measure endpoints with clearance, release, save and export.
   Observe mouth and ears separately. Reboot and verify released state and exact
   calibration round-trip before reconnecting all linkages.
6. **Coordinated behavior:** after all calibration is confirmed, engage;
   verify no motion until a command. Test center then small bounded gaze, lid
   tracking, blink, and each named expression. Check reversed lid endpoints,
   collision clearance and supply droop. Confirm stop, then release, and wait
   >10 seconds to verify auto-blink cannot reenergize anything. Use the physical
   rail switch immediately if binding or buzzing occurs.
7. **MCP discovery/calls:** on Hub's normal voice session, capture check-in and
   MCP handshake logs; verify the full Coglet tool list (see OPERATION.md),
   paged through `nextCursor`. Call state and release first. While released,
   every motion tool must error. Engage over MCP, then test each motion tool
   and animation with valid calls and invalid bounds/names, and builder mode
   over MCP with the session staying up (C3). With V+ off and logic powered, disconnect PCA I²C for a missing
   device test: immediate calls must error; a timed fault must latch release
   and appear in state. Reboot with V+ off to recover a failed initialization.
8. **Combined operation:** voice input/output plus repeated camera captures and
   all motions, logging heap, I²C errors, resets, audio dropouts and frame failures
   over a recorded duration. Exercise concurrent serial/browser/MCP stop and
   release. Repeat warm/cold reset tests with properly wired /OE, observing PWM;
   verify disconnected Wi-Fi does not disable serial release. Cut the rail to
   demonstrate the independent physical stop. Lack of servo feedback means
   firmware cannot detect rail loss, sag, stalled linkages or actual angles.

Automated coverage: real controller compiled against fake IDF I²C/NVS, testing
released boot, no-motion engagement, mapping/calibration validation, reversed
lid tracking, all animation completion, builder isolation, error propagation,
failed-write cache behavior, absent-PCA detection and latched release.
Firmware build results and outstanding physical checks should be recorded with
any deployment; do not turn this procedure into a claim that it has been run.

## Automated coverage

`just test` compiles the real controller against a fake ESP-IDF (I²C, GPIO, NVS,
HTTP) and checks: released boot, no-motion engagement, mapping and calibration
validation, reversed lid tracking, every animation running to completion, builder
isolation, error propagation, failed-write cache behaviour, absent-PCA detection,
latched release, and the calibration restore-command generator. It opens no
device. CI runs it on every push.

## Verification record

Newest first. Add an entry for every build that goes onto a robot.

### 2026-10-20 — C3 firmware without the fork (`firmware/app`)

- New ESP-IDF app for the C3 speaking the xiaozhi protocol: check-in, voice
  WebSocket, Opus (`espressif/esp_audio_codec` 2.4.1), MCP, heartbeat, serial
  console. Controller unchanged.
- `idf.py build` with ESP-IDF 5.5.4: PASS, no warnings in Coglet code. Image
  0x164f70 bytes. Host tests: PASS.
- Not run on any device. Protocol compatibility was checked by reading
  agent-hub's `checkin.py`, `protocol.py` and `ws_session.py`, not by a live
  session. Physical stages 1–8: **NOT RUN** on this firmware.

### 2026-10-13 — Split into this repo

- Controller moved from the fork's `main/boards/common/` to
  `firmware/coglet_controller/`; pins now passed at runtime (`CogletPins`), MCP
  tool registration moved to the fork's `boards/common/coglet_mcp_tools.cc`.
  Behaviour unchanged.
- Host tests: PASS. `coglet-c3` and `coglet` firmware: built with ESP-IDF
  5.5.4 against this repo's controller. No device flashed for this change.

### C3 bench (September 2026)

- Coglet C3 running on the bench against agent-hub: voice session, MCP tool
  discovery, listen mode, and tool calls through the device MCP proxy.
- C3 wiring as built, confirmed on hardware: D2 = mic data, D3 = WS; SCL on
  D4, SDA on D5. (Superseded 2026-10-20: the standard pad map follows the S3,
  so this robot needs D2/D3 and D4/D5 swapped. See
  [WIRING.md](../hardware/WIRING.md#pad-map-every-board).)
- Servos being fitted and calibrated from the base upward; stages 5–8 not yet
  complete for the full mechanism.

### 2026-09-08 — S3 profile written (xiaozhi fork)

- Reviewed Eyemech checkout at `f662248` and Agent Hub at `696ae18`.
- `./switch-board.sh coglet build` in the fork: PASS with installed ESP-IDF **5.5.4**.
  The repository guide names 5.5.2; that SDK version was not exercised here.
- Default firmware: camera unconfirmed/disabled, roles unassigned, released boot.
  `build-coglet/xiaozhi.bin` SHA-256:
  `018c03fe02c0fb1d7cf814853fa93d00ad8f5ae1ca3160a5719784f4895ff6a0`.
- OV2640, OV3660 and OV5640 board initialization branches: cross-compile PASS
  with the build's toolchain/includes and temporary preprocessor selections.
  These were object compilation checks, not three full firmware builds and not
  fitted-sensor identification.
- Host controller and calibration restore checks: PASS. No serial/device access.
- Build emitted existing esp_video/lwIP `_IO`, `_IOR`, `_IOW` macro redefinition
  warnings; no build errors. Existing board profiles were not rebuilt.
- Physical stages 1–8: **NOT RUN**. Target identity, actual channel mapping,
  camera identity, hardware calibration backup and power state remain unverified.
