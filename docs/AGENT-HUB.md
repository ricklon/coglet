# Coglet and agent-hub

Coglet is built to be driven by [agent-hub](https://github.com/ricklon/agent-hub).
It speaks the xiaozhi device protocol, and agent-hub is a xiaozhi server. The C3
runs this repo's own `firmware/app`; the S3 runs the xiaozhi fork. The hub
can't tell them apart. That gives Coglet
three things:

- a **voice session**: the hub transcribes the mic, runs a persona's LLM turn,
  and streams speech back;
- **MCP discovery**: the hub lists the robot's tools over that same session;
- **tool calls**: the persona's LLM moves the robot by calling those tools.

There's no separate MCP transport, bridge, or `/agent/register` step. MCP
rides inside the voice WebSocket. You don't need to change anything in the hub
for Coglet.

## How a session comes up

```
Coglet                                    agent-hub
  │  POST /xiaozhi/ota/  (device-id, client-id,  │
  │   board: coglet-c3, features, firmware)      │  registers the device (kind: xiaozhi)
  │ ◀────────── WebSocket URL + token ──────────  │
  │  WS hello { features: { mcp: true } }        │
  │ ◀──────────────── initialize ────────────────  │
  │ ◀──────────── tools/list (paginated) ────────  │  caches the tool list
  │  ⇄ audio frames, STT, TTS                    │
  │ ◀──────────── tools/call self.coglet.* ──────  │  when the LLM decides to move
```

1. **Check-in.** The firmware posts to its OTA URL. It sends its device and
   client IDs, the board ID (`coglet-c3` or `coglet`), feature flags (mic,
   speaker, camera) and firmware metadata, including the web-programmer manifest
   name. The hub registers the device and returns the voice WebSocket URL and token.
2. **Voice session.** The hello advertises MCP. The hub sends `initialize`, then
   pages through `tools/list`.
3. **Listening.** The session, and MCP with it, stays open in every mode;
   only the mic changes.
   - **S3: wake word.** The robot waits for its wake word with the session
     open, then streams the mic. That's "Computer" on the `s3` build, or a
     phrase you choose on `s3-phrase` ("Hey Coglet" to start; `self.voice.wake_word`
     changes it). It goes back to waiting 12 s after the last voice
     it heard, which leaves room for follow-up questions. The wake word itself
     isn't sent. The boot button starts listening too.
   - **C3: always listening.** There's no wake word, VAD or AEC on the device,
     so it streams the mic and the hub's VAD decides where each utterance ends.
     The boot button is the only way to interrupt a reply.

   On both, the mic isn't sent while a reply plays, and a dropped session
   reopens on its own.

## Pointing Coglet at a hub

Over the serial console (or the Web Programmer's console):

```
!wifi YourSSID YourPassword
!server http://HUB_HOST:8003/xiaozhi/ota/
```

Use the hub's real reachable address. For a hub that requires enrollment, use
its HTTPS check-in URL with `?enrollment_token=...`, and supply it privately:

```
!server https://HUB_HOSTNAME/xiaozhi/ota/?enrollment_token=TOKEN
```

The token is a shared fleet secret. Don't commit it, and don't put a build
with it baked in on a public Web Programmer. The build tooling refuses to package one,
see [FLASHING.md](FLASHING.md#enrollment-tokens). `!status` shows the OTA URL
in use.

## The tools

MCP is the robot's one interface: every command is a tool. The full list, with
arguments and modes, is in [OPERATION.md](OPERATION.md#robot-commands). The
ones a conversation uses most:

| Tool | Arguments | Notes |
|---|---|---|
| `self.coglet.gaze` | `horizontal`, `vertical`: −100..100 | Eyes, with upper-lid tracking. Needs both eye axes fitted |
| `self.coglet.head` | `pan`, `tilt`, `roll`: −100..100 | Base pan, neck tilt, neck roll; 0 is centre |
| `self.coglet.jaw` | `open`: 0..100 | 0 closed |
| `self.coglet.ears` | `left`, `right`: 0..100 | 0 back, 100 forward |
| `self.coglet.lids` | `open`: 0..100 | Holds until the next gaze |
| `self.coglet.blink` | none | Needs the `lids` servo and an established gaze |
| `self.coglet.animate` | `name` | Eyes: `look`, `roll`, `side_eye`, `wink`, `surprise`, `sleepy`. Head: `nod`, `shake`, `tilt`, `perk`, `droop`, `talk`, `look_around` |
| `self.coglet.stop` | none | Ends an animation or test, holds the pose |
| `self.coglet.release` | none | Latches all outputs off |
| `self.coglet.engage` | none | Outputs on for motion. Needs every assigned servo confirmed |
| `self.coglet.state` | none | JSON: `released`, `animation`, `testing`, `last_hardware_error`, `release_error`, per-role calibration, last commanded angles |

Rules the hub and persona should know:

- **Motion needs the robot engaged.** While released, motion tools return
  `ESP_ERR_INVALID_STATE`. `self.coglet.engage` works only once every assigned
  servo is calibrated and confirmed, and commands no pose by itself.
- **Anything with hub access can engage the robot.** The servo-rail switch is
  the stop that doesn't depend on software.
- **Timed actions return "started", not "done".** `blink`, `animate` and
  `test` return once the first frame is commanded. Poll `self.coglet.state`.
  A fault partway through latches release.
- **Partly built robots are normal.** Unfitted servos are skipped; a command
  or animation with nothing fitted to move returns `ESP_ERR_NOT_SUPPORTED`.
- **Calibration mode makes the voice quiet.** The session (and MCP) stays
  open with the mic and speaker off; `self.voice.quiet` with `on: false` brings
  the voice back.
- **Positions aren't measured.** `state` reports commands, not where the
  servos actually are.

Synchronous hardware failures, like a missing PCA9685 or an I²C error, come back
as JSON-RPC errors (code −32603) with the ESP-IDF error name as the message.

## Listen mode

agent-hub's **listen mode** needs no firmware support. Say "listen mode" or use
the dashboard toggle, and say "interact again" to leave it. The hub keeps
transcribing and logging what it hears, but runs no LLM turn and calls no
device tool, so the servos stay still. The robot speaks only when the mode
changes. The mode is held in hub memory per MAC, so it survives a robot reboot
but not a hub restart.

For calibration, prefer the firmware's own quiet mode (`!coglet calibrate` or
`!quiet on`), which turns the mic and speaker off and keeps the session, and
MCP, open. See
[OPERATION.md](OPERATION.md#quiet-mode).

## Driving Coglet from Claude Code (or any MCP client)

A xiaozhi device's MCP server can only be reached through the hub. agent-hub's
`scripts/device_mcp_proxy.py` re-exposes one hub-connected device to a local
MCP client over stdio. `tools/list` comes from the hub's live copy, and
`tools/call` goes through the hub to the robot.

```bash
claude mcp add coglet-c3 -- python3 /path/to/agent-hub/scripts/device_mcp_proxy.py \
    --device 9c:9e:6e:f7:16:0c          # the robot's MAC, from the hub dashboard
```

- Device tools show up as `self_coglet_state`, `self_coglet_release` and so on.
- By default the proxy exposes only tools that **can't start motion**: status,
  state, stop and release. Use `--tools a,b` or `--all-tools` to widen that.
- It adds two hub tools. `hub_speak` says a line verbatim. `hub_ask` runs a
  full voice turn as if the robot heard the text. Note that `hub_ask` hands the
  text to the persona's LLM, which can call any tool the *persona* allows,
  motion included.
- If the dashboard has a password, set `AGENT_HUB_SERVER_DASHBOARD_USERNAME` and
  `AGENT_HUB_SERVER_DASHBOARD_PASSWORD`.

> As of 2026-10-13 the proxy is on agent-hub's `feat/device-mcp-proxy` branch
> and not yet on `main`. A Claude Code config that points at it fails to
> connect from a `main` checkout.

## Optional hub-side extras

None of these are needed. Ideas, not existing features:

- a persona prompt that describes the tools, the engage rule and the animations;
- a dashboard label or Coglet-specific test buttons.

## Troubleshooting

| Symptom | Check |
|---|---|
| Never appears on the dashboard | `!status`: Wi-Fi up? OTA URL right? `curl http://HUB_HOST:8003/xiaozhi/ota/` from the same network |
| Appears but no Coglet tools | Hub logs for `initialize`/`tools/list`. Confirm the firmware is a Coglet build (`!status` board name) |
| Every motion tool errors | `self.coglet.state`. If `released: true`, call `self.coglet.engage` (needs calibration confirmed). `last_hardware_error` set? Call `self.coglet.scan` |
| `gaze` returns `ESP_ERR_NOT_SUPPORTED` | `eye_pan`/`eye_tilt` not mapped yet; expected while building bottom-up |
| Robot answers room noise while you work on it | `!quiet on`, or `!coglet calibrate`, which quiets the voice until `!quiet off` |
