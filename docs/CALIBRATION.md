# Calibrating Coglet (one servo at a time)

The mechanism is built up one servo at a time, so every step here works on a
partly wired robot. Nothing in this procedure requires the other roles to exist.

Roles, in the order the servos are fitted (bottom of the mechanism upward):
`base`, `neck_tilt`, `neck_roll`, `eye_pan`, `eye_tilt`, `lids`, `jaw`,
`ear_left`, `ear_right`. One servo drives both top lids, so `lids` is a single
axis and there are no per-eye lid roles.
Endpoints are **semantic**, and may run numerically backwards:

| Role | `low` means | `high` means |
|---|---|---|
| base | left | right |
| neck_tilt | down | up |
| neck_roll | left | right |
| eye_pan | left | right |
| eye_tilt | down | up |
| lids | closed | open |
| jaw | closed | open |
| ear_left, ear_right | back | forward |

Every calibration command is an MCP tool (`self.coglet.fit`, `explore`,
`nudge`, `mark`, `confirm`, `save`, ...) and also runs at the serial console
as `!coglet ...`; the steps below use the serial form. Most are **builder
mode only**. Pulses never leave the hard 1000-2000 us bound, whatever is asked
for. Someone still has to watch the servo: `confirm` means a person saw both
endpoints.

## Silence first

A voice agent answering room noise through the robot's own speaker makes
endpoint hunting impossible, so calibration runs with the voice side quiet:

- **Builder mode makes it quiet automatically.** It stops any reply and turns
  the mic and speaker off. On the C3 the session stays open, so MCP keeps
  working; on the S3 the session closes until `!quiet off`.
- **Quiet does not lift by itself** when builder mode ends. Turn it off with
  `self.voice.quiet` (`on: false`) or `!quiet off` when you're done.
- **`self.voice.quiet` / `!quiet on|off|status`** does the same by hand, and
  lasts until turned off or the robot reboots.
- **Unplugging the speaker** is the hardware fallback and needs no firmware.
- The hub's **listen mode** silences replies without touching the robot, but it
  keeps the session open; builder mode is the better fit while calibrating.

## Procedure for one servo

Servo power on, one servo fitted, hand near the power switch. Plug each servo
into its standard channel ([WIRING.md](../hardware/WIRING.md#servo-channels)):
base 0, neck_tilt 1, neck_roll 2, eye_pan 3, eye_tilt 4, lids 5, jaw 6,
ear_left 7, ear_right 8.

```bash
!coglet builder              # outputs on; nothing moves; voice agent pauses
!coglet identify 0           # wiggles raw channel 0 ~1.4s: check the part you
                             # just fitted is the one that twitches
!coglet release              # channel mapping is edited while released
!coglet fit base             # assign base to its standard channel with a 90/90
                             # window: that permits only a 90 degree
                             # horn-seating command
!coglet builder
!coglet explore base         # centres the servo at 1500 us and starts hunting
!coglet nudge -2             # step up to +-5 degrees at a time
!coglet nudge -2             # ... keep going, stopping SHORT of binding
!coglet mark low             # this position is the LEFT end
!coglet nudge 2              # ... walk out to the other end
!coglet mark high            # this position is the RIGHT end
!coglet confirm base         # a human watched both ends
!coglet save                 # persists; allowed in builder mode
```

Then check the result:

```bash
!coglet servo base 90        # inside the calibrated window now
!coglet jog base 5
!coglet test base            # sweeps centre, low, high, centre (~5 s)
!coglet state                # confirmed:true, low/high as marked
```

Notes:

- `mark` clears `confirmed` for that role, so a moved endpoint always has to be
  watched again before the robot will use it.
- `explore` deliberately leaves `commanded_degrees` null: an explored position
  is outside the calibrated window and is not a pose.
- `identify` holds the bus for about 1.4 s while it wiggles.
- Release before editing a channel mapping; `fit` and `configure` refuse while
  engaged or in builder mode.
- A servo on a non-standard channel: `!coglet configure ROLE CH 90 90 1000 2000 0 0`
  instead of `fit`. The standard map is `SERVO_ROLE_CHANNELS` in
  `firmware/app/main/board.h`.

## When enough servos are fitted

`!coglet test` (in builder mode) sweeps every calibrated role in turn, base
upward, which is a quick check that nothing binds or was mis-marked before you
engage. `!coglet release` ends it immediately.

`engage` needs at least one role fitted and every *assigned* role confirmed.
Roles left unassigned are treated as not fitted and are skipped.

Once engaged, every role takes motion commands: `gaze` drives `eye_pan` and
`eye_tilt` (the lids track it), `head` drives `base`, `neck_tilt` and
`neck_roll`, and `jaw`, `ears` and `lids` drive the rest. Animations move the
eyes or the rest of the head:

```bash
!coglet release
!coglet engage
!coglet gaze 0 0             # needs eye_pan and eye_tilt fitted
!coglet gaze -100 0          # eyes full left
```

`blink` and automatic blinking need the lid servo; without it, blink returns
`ESP_ERR_NOT_SUPPORTED` and auto-blink stays off. `gaze` and `animate` likewise
return `ESP_ERR_NOT_SUPPORTED` until both eye axes are fitted — expected while
working bottom-up, where the base is calibrated long before the eyes exist.

## Backing up

Copy the JSON line that `!coglet export` prints into a file (the serial
console shows it; it is one line), then generate restore commands offline:

```bash
just restore-commands coglet-calibration.json > restore.txt
# or: python3 tools/restore_commands.py coglet-calibration.json > restore.txt
```

Review `restore.txt` before applying it over serial. It releases, restores the
mapping, saves and exports for comparison; it never engages. Only restore
calibration measured for the same unchanged mechanism.

Calibration is stored in NVS namespace `coglet`, key `cal_v1`. The JSON export
is the portable format; the binary NVS blob is not a cross-version format.
A calibration export holds no secrets, but it describes one specific
mechanism, and raw flash backups do hold Wi-Fi and hub credentials. `.gitignore`
keeps both kinds of file out of this repo.

## Before reading the procedure

- Start with a **narrow builder-approved window** and `confirmed=0`. A 90/90
  window permits only a 90° horn-seating command.
- Pulse mapping is linear over 0–180° across `MIN_US..MAX_US` plus trim. The
  1000–2000 µs defaults are placeholders, not measured values — the fitted
  servo's real range is still unknown (see [WIRING.md](../hardware/WIRING.md#unknowns-to-confirm-at-the-bench)).
- Use one unloaded servo, then a single 2° direction test after any horn change.
- Never infer endpoints from another servo. Stop short of binding.
- For channel swaps, unassign the affected roles first (`CH=-1`, `confirmed=0`).

Note `!coglet lid_coupling TRIM COEFF` sets how the lids track the gaze;
`!coglet lids OPEN` moves the lids; `lids` is also the **role** name used by
`fit`, `explore` and `confirm`.

## Still to do

- Voice-guided calibration, where the robot walks you through this by talking.
- Neck motion during conversation: the head could follow gaze beyond the eyes'
  travel. Left out until the neck is calibrated and its load is understood.
- Jaw motion tied to speech, and any ear choreography.
