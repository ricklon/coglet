# Assembling Coglet

How to put a Coglet together and bring it up: electronics first, then the
servos one at a time from the base upward, checking each stage before the
next. This guide covers the electronics and the bring-up order. **The
mechanical steps (printed parts, horns, linkages) aren't written yet**; they're
marked *TODO* where they belong.

Pin and power details live in [WIRING.md](../hardware/WIRING.md), calibration
in [CALIBRATION.md](CALIBRATION.md), and what to record at each stage in
[QUALIFICATION.md](QUALIFICATION.md). The
[Web Programmer](https://ricklon.github.io/coglet/) walks through flashing
and testing with the same steps.

## Rules for the whole build

- **The servo-rail switch is the emergency stop.** Keep it in reach whenever
  servo power is on. Firmware can't see a stalled or binding servo.
- **Servo power off whenever USB goes in or a serial port opens.** Opening the
  port can reset the board.
- **Every boot is released.** A fresh board has no servos assigned and moves
  nothing until a person fits, calibrates and engages each one.
- **One servo at a time.** A partly built robot is normal.
- **Every command here is also an MCP tool.** The steps use the serial form
  (`!coglet fit base`); over MCP the same step is `self.coglet.fit` with
  `role: base`. See [OPERATION.md](OPERATION.md#robot-commands).

## 1. Gather the parts

See the [parts list](../hardware/WIRING.md#parts):

- Seeed XIAO ESP32-S3 Sense (the main board) or XIAO ESP32-C3
- INMP441 I²S microphone
- MAX98357A I²S amp and a 4–8 Ω speaker
- PCA9685 16-channel servo board
- Up to nine hobby servos
- A separate, switched 4.8–6 V servo supply sized for the servos you fit
- Hook-up wire and headers, a USB-C cable

## 2. Prepare the boards

1. Solder headers to the XIAO, the INMP441, the MAX98357A and the PCA9685.
2. **INMP441:** tie **L/R to GND**. The firmware reads the left I²S slot, and
   the mic only drives the left slot with L/R low.
3. **MAX98357A:** leave SD and GAIN unconnected unless you know you need them.
4. **PCA9685:** check the address jumpers are all open (address 0x40).
5. **PCA9685 /OE:** many breakouts pull /OE *down*, which keeps the outputs on
   across a reset no matter what the firmware does. For outputs that stay off
   through a reset, replace that pull-down with a pull-up (for example 10 kΩ)
   to the PCA9685's 3.3 V logic rail. Inspect your breakout before changing
   anything. See [Power and safety](../hardware/WIRING.md#oe).

## 3. Wire the logic harness

Wire by **pad**. Every board uses the same pads, so a C3 and an S3 take the same
harness; the firmware maps the pads to GPIOs.

| Connect | XIAO pad |
|---|---|
| Amp DIN | D0 |
| Amp BCLK **and** mic SCK | D1 |
| Amp LRC **and** mic WS | D2 |
| Mic SD | D3 |
| PCA9685 SDA | D4 |
| PCA9685 SCL | D5 |
| PCA9685 /OE | D10 |

Power for the logic side:

| From | To |
|---|---|
| XIAO 3V3 | INMP441 VDD, PCA9685 VCC (logic) |
| XIAO 5V | MAX98357A Vin |
| XIAO GND | INMP441 GND, MAX98357A GND, PCA9685 GND |
| Amp + / − | Speaker |

The two pairs people swap: D2/D3 (the amp hisses and the mic reads zero) and
D4/D5 (nothing answers on the I²C bus).

## 4. Wire the servo power

1. Run the servo supply **through its switch** to the PCA9685's V+ terminal
   block.
2. Join the servo supply's ground to the PCA9685 ground, so the XIAO, the
   PCA9685 and the supply share one ground.
3. **Never power servos from the XIAO** or from USB.
4. Leave the switch **off**.

## 5. First power-up, no servos

Servo switch off, no servos plugged in.

1. Plug the XIAO into USB and flash it with the
   [Web Programmer](https://ricklon.github.io/coglet/) (or `just flash`, see
   [FLASHING.md](FLASHING.md)).
2. Open the serial console and run the bench checks:

   | Command | Expect |
   |---|---|
   | `!status` | the firmware version and board |
   | `!coglet scan` | `0x40` answers |
   | `!coglet state` | `"released": true`, `"pca_initialized": true` |
   | `!speaker` | a short tone |
   | `!mic status` (while talking) | a peak well above zero |

These are qualification stages 1–3 (inspection, speaker, microphone). Record
what you saw in [QUALIFICATION.md](QUALIFICATION.md).

## 6. Connect to agent-hub, still no servos

```
!wifi YourSSID YourPassword
!server http://HUB_HOST:8003/xiaozhi/ota/
```

After it reboots, `!status` should show **Session: listening** and the robot
appears on the hub dashboard. Talk to it and check you hear the reply. Getting
voice working now means the servo stages only have servo problems. See
[AGENT-HUB.md](AGENT-HUB.md).

## 7. Fit the servos, base upward

Work through the servos in this order. Each plugs into its standard PCA9685
channel ([Servo channels](../hardware/WIRING.md#servo-channels)).

| Order | Servo | Channel | Mechanical steps |
|---|---|---|---|
| 1 | `base` | 0 | *TODO* |
| 2 | `neck_tilt` | 1 | *TODO* |
| 3 | `neck_roll` | 2 | *TODO* |
| 4 | `eye_pan` | 3 | *TODO* |
| 5 | `eye_tilt` | 4 | *TODO* |
| 6 | `lids` (both top lids) | 5 | *TODO* |
| 7 | `jaw` | 6 | *TODO* |
| 8 | `ear_left` | 7 | *TODO* |
| 9 | `ear_right` | 8 | *TODO* |

For each servo:

1. **Servo switch off.** *TODO: mount the servo and seat its horn.* If you can,
   leave the linkage disconnected for the first calibration (stage 5 asks for
   an unloaded servo).
2. Plug the servo into its channel.
3. Assign it, while released:
   ```
   !coglet fit base
   ```
4. **Servo switch on**, hand on the switch. Calibrate it, following
   [CALIBRATION.md](CALIBRATION.md#procedure-for-one-servo):
   ```
   !coglet builder           # outputs on, voice paused, nothing moves yet
   !coglet identify 0        # the part you just fitted should twitch
   !coglet explore base
   !coglet nudge -2          # ... toward one end, stop short of binding
   !coglet mark low
   !coglet nudge 2           # ... to the other end
   !coglet mark high
   !coglet confirm base      # you watched both ends
   !coglet save
   ```
5. Check it:
   ```
   !coglet test base         # centre, low, high, centre
   !coglet release
   ```
6. **Servo switch off.** *TODO: connect the linkage*, then repeat the
   endpoint check (`builder`, `test`, adjusting with `explore`/`mark` if the
   linkage changed the safe range) before moving on.
7. Export a backup: `!coglet export`, and save the output somewhere safe.

## 8. Whole-robot checks

When the servos you plan to fit are in:

1. `!coglet builder`, then `!coglet test` sweeps every calibrated servo in
   turn. Watch for binding, collisions and supply droop. `!coglet release`
   stops it at once.
2. `!coglet release`, then `!coglet engage`. Nothing should move until a
   command arrives.
3. `!quiet off` to bring the voice back (builder mode paused it).
4. Run qualification stages 6–8: coordinated motion, MCP discovery and calls,
   then combined operation. See [QUALIFICATION.md](QUALIFICATION.md).

## Still to write

- The mechanical steps: printed parts, servo mounting, horns, linkages and
  clearances for each stage of the table above.
- Photos or drawings of the finished harness.
