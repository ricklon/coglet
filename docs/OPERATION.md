# Operating Coglet

## Stopping it

In order of how much they can be trusted:

1. **The servo-rail switch.** Cuts servo power. It works with a crashed MCU,
   a dead I²C bus, or no firmware at all. Keep a hand on it whenever servos
   are powered.
2. **`!coglet release`** (serial), the builder page's *Release outputs* button,
   or `self.coglet.release` (MCP). Latches the outputs off: drives `/OE` high,
   if it's wired, and sends the PCA9685 ALL_LED full-off. It needs a live MCU
   and a working I²C bus. It doesn't cut power or prove the servos are limp.
3. **`!coglet stop`** / `self.coglet.stop`. Freezes the current pose and doesn't
   latch anything. Auto-blink can resume after a settling interval.

If `/OE` isn't wired to D10 with a pull-up (see
[WIRING.md](../hardware/WIRING.md#oe)), release depends on I²C alone and can't
stop PWM if that bus fails.

## States

```
          boot (always)
              │
              ▼
        ┌──────────┐   !coglet builder   ┌──────────┐
        │ released │ ──────────────────▶ │ builder  │  one-servo tests, calibration
        │ (latched)│ ◀────────────────── │          │  voice paused
        └──────────┘    release          └──────────┘
          │      ▲
 !coglet  │      │ release (serial, page, MCP), or any hardware fault
  engage  ▼      │
        ┌──────────┐
        │ engaged  │  gaze, blink, animations; MCP motion allowed
        └──────────┘
```

- **Every boot is released**, whatever is stored in NVS. No neutral pose,
  random gaze, or startup movement is ever commanded.
- **`engage`** needs at least one fitted role, and every *assigned* role
  confirmed. It clears per-channel pulses before enabling outputs, and it
  commands no position. Nothing moves until a gaze or animation command.
- **`builder`** enables outputs for isolated servo tests. The voice agent
  pauses, see [Quiet mode](#quiet-mode).
- **Any later I²C error** during an animation or timer step aborts motion and
  latches release.

`engage` and `builder` are local only: serial or the builder page. No MCP
tool can leave the released state.

## Serial console

115200 baud over the XIAO's native USB (USB-Serial-JTAG). Use the web
flasher's console, `just monitor`, or any terminal. **Cut servo V+ before
opening the port.** Opening it can reset the board.

### Device commands

`firmware/app` (C3) and the xiaozhi fork (S3) accept the same commands, with the
differences noted.

| Command | Effect |
|---|---|
| `!wifi SSID PASSWORD` | Add a network |
| `!wifi list` / `!wifi remove N` / `!wifi clear` | Manage saved networks |
| `!server URL` | Set the full OTA (agent-hub check-in) URL and reboot |
| `!server IP` | Replace only the host, keep the path, and reboot |
| `!server` / `!server list` / `!server N` | Show the current URL / recent URLs / reuse one |
| `!status` | Firmware, board, Wi-Fi, IP, OTA URL, heap |
| `!mic [gain N\|mute\|unmute\|status]` | Microphone level and mute |
| `!speaker` / `!speaker vol N` / `!speaker status` | Test tone (C3) or chime (S3), volume |
| `!camera` | Capture one frame (S3 with a confirmed camera) |
| `!stop` | Close the current voice session |
| `!quiet [on\|off\|status]` | Pause or resume listening and speaking |
| `!reboot` | Reboot |
| `!help` | List the commands |
| anything else | S3: sent as a chat message, which agent-hub ignores. C3: rejected |

On the C3, `!wifi SSID PASSWORD` splits at the last space, so an SSID may
contain spaces but a password may not. `!mic` shows the peak level over the last
second.

### Robot commands

| Command | Mode | Effect |
|---|---|---|
| `!coglet state` / `!coglet export` | any | JSON diagnostics plus full calibration |
| `!coglet scan [SDA SCL]` | any | Sweep 0x08–0x77 on the servo bus. With pins, rebuilds the bus on them first (reboot after) |
| `!coglet release` | any | Latched output release |
| `!coglet configure ROLE CH LOW HIGH MIN_US MAX_US TRIM_US CONFIRMED` | released | Set one role. `CH` −1 unassigns it. `CONFIRMED` is 0 or 1 |
| `!coglet lids TRIM COEFF` | released | Upper-lid tracking, 0–1 each |
| `!coglet save` | released or builder | Persist calibration to NVS. Reports failures |
| `!coglet builder` | released | Enable outputs for isolated tests. Nothing moves |
| `!coglet identify CH` | builder | Wiggle a raw channel for about 1.4 s to find which servo is on it |
| `!coglet explore ROLE` | builder | Centre at 1500 µs and start hunting endpoints |
| `!coglet nudge DELTA` | builder | Up to ±5°, clamped to the hard 1000–2000 µs bound |
| `!coglet mark low\|high` | builder | Record the explored position as that endpoint. Clears `confirmed` |
| `!coglet confirm ROLE` | any | A person watched both endpoints |
| `!coglet servo ROLE DEG` / `!coglet jog ROLE DELTA` | builder | Move within the calibrated window. Jog is at most ±5° |
| `!coglet engage` | released | Enter normal mode (see above) |
| `!coglet gaze X Y` | engaged | −100..100 onto the calibrated eye endpoints |
| `!coglet blink` | engaged | One blink after an established gaze |
| `!coglet animate NAME` | engaged | `look`, `roll`, `side_eye`, `wink`, `surprise`, `sleepy` |
| `!coglet stop` | engaged | Freeze. Doesn't latch |
| `!coglet web on\|off` | any | Builder page on or off, for this boot only |

Roles, bottom of the mechanism upward: `base`, `neck_tilt`, `neck_roll`,
`eye_pan`, `eye_tilt`, `lids`, `jaw`, `ear_left`, `ear_right`. Channels are
0–15 and must be unique.

## Builder web page

```
!coglet web on
```

Then browse to `http://DEVICE_IP:8080/`. The page runs the same command text
as `!coglet ...` (`POST /command` with a plain-text body) and has a *Release
outputs* button. It's a local, **unauthenticated** bench tool:

- off at every boot; turn it on per session and off when you're done;
- use it only on a trusted network;
- no CORS, and browser requests whose `Origin` doesn't match the host are rejected.

Wi-Fi is set over serial (`!wifi`), so there's no recovery access point. The
S3's xiaozhi firmware also has a setup hotspot; the C3's doesn't.

## Quiet mode

A robot answering room noise through its own speaker makes calibration
impossible. TTS also competes for the I²S bus.

- **`!coglet builder` pauses the voice agent automatically.** It aborts any
  reply, pauses listening, and stops reconnecting.
- **Quiet doesn't lift by itself** when builder mode ends. The LLM can call
  `self.coglet.release`, and an auto-resume once let the robot talk its own way
  out of a calibration session. Say `!quiet off` when you're done.
- **`!quiet on|off|status`** does the same by hand at any time. It lasts until
  toggled or rebooted.
- **Unplugging the speaker** is the hardware fallback.
- agent-hub's **listen mode** silences replies but keeps the session open. See
  [AGENT-HUB.md](AGENT-HUB.md#listen-mode).

## What survives what

| | Reflash (same partitions) | Erase flash / erase-and-install | Partition table change |
|---|---|---|---|
| Wi-Fi networks, OTA URL | kept | lost | lost |
| Coglet calibration (`coglet`/`cal_v1`, NVS) | kept | lost | lost |
| Released-at-boot | always | always | always |

Export calibration before any erase. See [CALIBRATION.md](CALIBRATION.md#backing-up).

## Motion details

- The motion task runs at about 100 Hz. Blinks come at random 2–7 s gaps:
  70 ms closed, 70 ms to reopen.
- Expressions suppress blinking and leave a settling interval.
- The upper lids track vertical gaze:
  `open = (0.5 + 0.5·trim) · (1 − coeff·(1 − vertical))`, with trim 0.85 and
  coefficient 0.8 by default. These are tunable aesthetic values, not
  measurements.
- Keyframes, durations and interpolation for the animations come from
  eyemech-esp32-xiao's `eye_motion.c`. See [decisions.md](decisions.md).
- PWM uses a nominal 25 MHz oscillator with prescaler 121, for about 50 Hz.
  The oscillator hasn't been measured.
- One mutex serialises serial, HTTP, MCP and the timer. PCA MODE1 is checked
  even when a repeated target would skip the write.
- A coordinated move isn't electrically atomic. Earlier channels can receive
  their command before a later channel fails.
