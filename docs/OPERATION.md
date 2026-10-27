# Operating Coglet

## One interface

Coglet is an MCP device. Every command is an MCP tool that agent-hub, Claude or
any MCP client can call through the hub. The serial console runs the same
commands through the same code, and exists for a robot that isn't on a network
yet (or one you're bringing up on the bench).

## Stopping it

In order of how much they can be trusted:

1. **The servo-rail switch.** Cuts servo power. It works with a crashed MCU,
   a dead I²C bus, or no firmware at all. Keep a hand on it whenever servos
   are powered. It's the only stop that doesn't depend on software.
2. **`self.coglet.release`** / `!coglet release`. Latches the outputs off:
   drives `/OE` high, if it's wired, and sends the PCA9685 ALL_LED full-off.
   It needs a live MCU and a working I²C bus. It doesn't cut power or prove
   the servos are limp.
3. **`self.coglet.stop`** / `!coglet stop`. Ends any animation or servo test
   and holds the current pose. Doesn't latch anything; auto-blink can resume
   after a settling interval.

If `/OE` isn't wired to D10 with a pull-up (see
[WIRING.md](../hardware/WIRING.md#oe)), release depends on I²C alone and can't
stop PWM if that bus fails.

Anything that can reach the hub can engage the robot, so treat hub access as
access to the servos.

## States

```
          boot (always)
              │
              ▼
        ┌──────────┐      calibrate       ┌─────────────┐
        │ released │ ───────────────────▶ │ calibration │  endpoints, servo tests
        │ (latched)│ ◀─────────────────── │             │  voice quiet
        └──────────┘       release        └─────────────┘
          │      ▲
  engage  │      │ release, or any hardware fault
          ▼      │
        ┌──────────┐
        │ engaged  │  gaze, head, jaw, ears, lids, blink, animations
        └──────────┘
```

- **Every boot is released**, whatever is stored in NVS. No neutral pose,
  random gaze, or startup movement is ever commanded.
- **`engage`** needs at least one fitted role, and every *assigned* role
  confirmed. It clears per-channel pulses before enabling outputs, and it
  commands no position. Nothing moves until a motion command.
- **`calibrate`** enables outputs for calibration and servo tests, and puts the
  voice in [quiet mode](#quiet-mode). It works over the serial console before
  there's any hub (first setup), and over MCP once agent-hub is connected.
- **Any later I²C error** during an animation or timer step aborts motion and
  latches release.
- **Every position stays inside each servo's calibrated window**, and is a
  command, not a measurement: no servo reports where it is.

## Robot commands

Each is an MCP tool; the serial form is `!coglet` plus the command.

| MCP tool | Serial | Mode | Effect |
|---|---|---|---|
| `self.coglet.state` / `export` | `!coglet state` / `export` | any | JSON diagnostics plus full calibration |
| `self.coglet.scan` | `!coglet scan [SDA SCL]` | any | Sweep 0x08–0x77 on the servo bus. With pins, rebuilds the bus on them first (reboot after) |
| `self.coglet.release` | `!coglet release` | any | Latched output release |
| `self.coglet.stop` | `!coglet stop` | any | End an animation or servo test and hold the pose. Doesn't latch |
| `self.coglet.engage` | `!coglet engage` | released | Normal motion (see above) |
| `self.coglet.calibrate` | `!coglet calibrate` | released | Outputs on for calibration. Nothing moves |
| `self.coglet.fit` | `!coglet fit ROLE` | released | Assign a fitted servo to its standard channel ([WIRING.md](../hardware/WIRING.md#servo-channels)), 90/90 window, unconfirmed |
| `self.coglet.configure` | `!coglet configure ROLE CH LOW HIGH MIN_US MAX_US TRIM_US CONFIRMED` | released | Set one role by hand, e.g. on a non-standard channel. `CH` −1 unassigns it |
| `self.coglet.lid_coupling` | `!coglet lid_coupling TRIM COEFF` | released | How the top lids track the gaze, 0–1 each |
| `self.coglet.save` | `!coglet save` | released or calibration | Persist calibration to NVS. Reports failures |
| `self.coglet.identify` | `!coglet identify CH` | calibration | Wiggle a raw channel for about 1.4 s to find which servo is on it |
| `self.coglet.explore` | `!coglet explore ROLE` | calibration | Centre at 1500 µs and start hunting endpoints |
| `self.coglet.nudge` | `!coglet nudge DELTA` | calibration | Up to ±5°, clamped to the hard 1000–2000 µs bound |
| `self.coglet.mark` | `!coglet mark low\|high` | calibration | Record the explored position as that endpoint. Clears `confirmed` |
| `self.coglet.confirm` | `!coglet confirm ROLE` | any | Someone watched both endpoints |
| `self.coglet.servo` / `jog` | `!coglet servo ROLE DEG` / `jog ROLE DELTA` | calibration | Move within the calibrated window. Jog is at most ±5° |
| `self.coglet.test` | `!coglet test [ROLE]` | calibration | Sweep a role centre, low, high, centre, about 5 s. No role: every calibrated role in turn, base upward. `release`, `stop` or any other calibration move ends it |
| `self.coglet.gaze` | `!coglet gaze X Y` | engaged | Eyes, −100..100 each; the lids track the gaze |
| `self.coglet.head` | `!coglet head PAN TILT ROLL` | engaged | Base pan, neck tilt and neck roll, −100..100 each, 0 centre |
| `self.coglet.jaw` | `!coglet jaw OPEN` | engaged | 0 closed to 100 open |
| `self.coglet.ears` | `!coglet ears LEFT RIGHT` | engaged | 0 back to 100 forward, each |
| `self.coglet.lids` | `!coglet lids OPEN` | engaged | 0 closed to 100 open, until the next gaze |
| `self.coglet.blink` | `!coglet blink` | engaged | One blink after an established gaze |
| `self.coglet.animate` | `!coglet animate NAME` | engaged | Eyes: `look`, `roll`, `side_eye`, `wink`, `surprise`, `sleepy`. Head: `nod`, `shake`, `tilt`, `perk`, `droop`, `talk`, `look_around` |

Roles, bottom of the mechanism upward: `base`, `neck_tilt`, `neck_roll`,
`eye_pan`, `eye_tilt`, `lids`, `jaw`, `ear_left`, `ear_right`. Channels are
0–15 and must be unique. Servos that aren't fitted are skipped; a command or
animation with nothing fitted to move returns `ESP_ERR_NOT_SUPPORTED`.

On the S3 (still the xiaozhi fork), fractional arguments such as `degrees` are
strings.

## Device commands

| MCP tool | Serial | Effect |
|---|---|---|
| `self.voice.quiet` | `!quiet [on\|off\|status]` | [Quiet mode](#quiet-mode) on or off |
| `self.voice.restart` | `!stop` | Close and reopen the voice session |
| `self.audio_speaker.set_volume` | `!speaker vol N` | Volume 0–100, kept across reboots |
| `self.audio_speaker.test_tone` | `!speaker` / `!speaker test` | A short tone |
| `self.audio_mic.status` | `!mic status` | Mute, gain, and the peak level over the last second |
| `self.audio_mic.set` | `!mic mute\|unmute`, `!mic gain N` | Mute and gain |
| `self.get_device_status` | | Volume, mic, Wi-Fi, firmware, uptime, heap, as JSON |
| `self.device.status` | `!status` | Firmware, Wi-Fi, hub URL, session state, heap |
| `self.wifi.list` / `add` / `remove` | `!wifi list`, `!wifi SSID PASSWORD`, `!wifi remove N` | Saved networks (also `!wifi clear`) |
| `self.hub.set_server` | `!server URL\|IP` | Set the agent-hub check-in URL and reboot (also `!server list`, `!server N`) |
| `self.reboot` | `!reboot` | Restart. It comes back released |
| | `!help` | List the serial commands |

Both boards have all of these. On the S3 (xiaozhi's properties have no
number type, and no way to leave a boolean out): `self.audio_mic.set` takes
`muted` as `"true"`/`"false"` and `gain` as a decimal string, either left empty
to keep it; `self.audio_mic.status` reports mute and gain but no peak level;
`self.wifi.add` saves the network for the next connect rather than switching
now; `self.hub.set_server` with a bare host uses port 8003 and
`/xiaozhi/ota/`. The S3 also has `self.camera.take_photo` when a camera is
detected.
`!wifi SSID PASSWORD` splits at the last space, so an SSID may contain spaces
but a password may not. Wi-Fi is set over serial or MCP; the C3 has no setup
hotspot (the S3's xiaozhi firmware does).

## Serial console

115200 baud over the XIAO's native USB (USB-Serial-JTAG). Use the Web
Programmer's console, `just monitor`, or any terminal. **Cut servo V+ before
opening the port.** Opening it can reset the board.

## Quiet mode

A robot answering room noise through its own speaker makes calibration
impossible.

- **Calibration mode turns quiet on.** It stops any reply, turns the mic and
  speaker off, and keeps the session open, so MCP keeps working. On the S3 a
  session that drops while quiet is reopened, still quiet, within about 10 s.
- **Quiet doesn't lift by itself** when calibration mode ends. Turn it off with
  `self.voice.quiet` (`on: false`) or `!quiet off`.
- **`self.voice.quiet` / `!quiet on|off|status`** does the same by hand at any
  time. It lasts until turned off or the robot reboots.
- **Unplugging the speaker** is the hardware fallback.
- agent-hub's **listen mode** silences replies but keeps the mic. See
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
- One mutex serialises serial, MCP and the timer. PCA MODE1 is checked
  even when a repeated target would skip the write.
- A coordinated move isn't electrically atomic. Earlier channels can receive
  their command before a later channel fails.
