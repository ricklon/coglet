# Flashing and releasing Coglet

**Before any of this: switch the servo supply off.** Opening a serial port or
running esptool can reset the MCU, and a board with stored calibration and a
powered rail shouldn't be reset with your hands in the mechanism.

## Option 1: Web Programmer (no toolchain)

`web-programmer/` is a static page built on
[esp-web-tools](https://esphome.github.io/esp-web-tools/), with a Web Serial
console for `!wifi`, `!server` and `!coglet` commands. It needs Chrome or Edge on
a desktop, served from `localhost` or HTTPS.

- **Published:** each `v*` tag deploys the page to GitHub Pages, with that
  release's firmware (see [Releases](#releases)).
- **Local:**
  ```bash
  just build c3 && just build s3     # or: just package --build
  just package                       # copy images + write manifests into web-programmer/
  just serve                         # http://localhost:8080
  ```
  The page lists both boards. A board with no packaged firmware has no
  manifest, so its button fails until you package it.

The esp-web-tools bundle is vendored in `web-programmer/vendor/`, so the page works
offline, for example at a build night with no internet. Refresh it with
`just vendor-programmer`.

"Erase and install" wipes NVS: Wi-Fi, OTA URL **and calibration**. Export
calibration first (`!coglet export`). An update install without erasing keeps
them, as long as the partition table hasn't changed.

The firmware also reports its manifest name (`manifest-coglet-c3.json` or
`manifest-coglet.json`) to agent-hub at check-in, so the hub knows which
Web Programmer build matches the device.

## Option 2: build and flash from source

Needs ESP-IDF 5.5.x (CI uses `espressif/idf:v5.5.2`; 5.5.4 also builds) and
[`just`](https://github.com/casey/just).

```bash
just test                     # host tests, no hardware
just build c3                 # -> firmware/app/build/
just flash c3 /dev/ttyACM0
just monitor /dev/ttyACM0
```

The C3 is a plain ESP-IDF project, so `idf.py` works in `firmware/app` too. Its
first build fetches `espressif/esp_audio_codec` and
`espressif/esp_websocket_client` from the component registry, pinned in
`firmware/app/dependencies.lock`. Set a default hub with `idf.py menuconfig` →
*Coglet*, or at runtime with `!server`.

The S3 Sense profile still builds from the xiaozhi fork:

```bash
just setup                    # git submodule update --init --recursive
just build s3                 # -> firmware/xiaozhi-esp32/build-coglet/
```

### Moving a C3 from the fork firmware to firmware/app

`firmware/app` uses a different partition table (one factory app, no OTA data
or assets partition), but NVS is at the same offset and size, with the same
keys. An update install without erasing should keep Wi-Fi, the hub URL, the
client id, volume and calibration. That hasn't been checked on a robot yet, so
export calibration first (`!coglet export`).

### Building the S3 against another fork checkout

To build against a different xiaozhi checkout, for example a sibling
`../xiaozhi-esp32` with unpushed changes:

```bash
XIAOZHI_DIR=../xiaozhi-esp32 just build s3
```

The fork's `main/CMakeLists.txt` finds the controller in this order:

1. `COGLET_CONTROLLER_DIR` (environment or CMake cache);
2. `../coglet_controller` next to the fork, which is the submodule layout;
3. `../coglet/firmware/coglet_controller`, which is the sibling layout.

If it finds none of them, the build stops and says where to look.

## Enrollment tokens

If `switch-board.sh` finds an agent-hub `.env`, it bakes the hub's public host
and enrollment token into **coglet-c3** builds as `CONFIG_OTA_URL`. By default
it looks for `../agent-hub/.env` relative to the fork, so a sibling checkout
picks it up, and so do `AGENT_HUB_ENV_FILE` and
`AGENT_HUB_SERVER_ENROLLMENT_TOKEN` when they're set. That's convenient on your
own bench, but anyone who downloads the image can read the token.

`scripts/package_firmware.py` refuses to package a build with a token in its
OTA URL. To package one:

- for a public Web Programmer, rebuild without the token:
  `AGENT_HUB_ENV_FILE=/dev/null just build c3`. CI always does this.
- for a private, local-only Web Programmer, pass `just package --allow-enrollment-token`.

## Packaging

```bash
just package                # both boards, from existing builds
just package --build        # build first
just package --board c3     # one board
```

This writes:

| Output | Used by |
|---|---|
| `web-programmer/firmware/<id>/*.bin` + `metadata.json` | the Web Programmer |
| `web-programmer/manifest-coglet-c3.json`, `manifest-coglet.json` | esp-web-tools |
| `dist/release-zips/coglet-<c3\|s3>_v<version>.zip` | GitHub releases |

`metadata.json` records the firmware version and the `git describe` of both
this repo and the fork, so you can tell which build is flashed. Built images
and generated manifests are gitignored.

## Releases

`.github/workflows/release.yml`:

- **every push and PR:** host tests, then both firmware builds;
- **`v*` tag:** also publishes a GitHub release with the zips, and deploys
  `web-programmer/` with that firmware to GitHub Pages.

To make Pages work, set the repo's Pages source to **GitHub Actions** once.

```bash
git tag v0.1.0 && git push origin v0.1.0
```

## Before reflashing a board that came from somewhere else

Do this if the board ran other firmware, such as Eyemech or an older xiaozhi
build, and might hold anything worth keeping:

1. Establish the board's identity: label, USB identity, chip and flash size.
   Work out which mechanism it's attached to and whether the logic and servo
   rails are powered.
2. With servo V+ off, capture `!status` and `!coglet export`, or the old
   firmware's equivalent, to files.
3. Make a read-only esptool backup of the **whole detected flash** and the
   partition table, then hash the files. Raw flash holds Wi-Fi and hub
   credentials, so store it privately and outside any build folder.
4. Coglet calibration doesn't carry over from Eyemech. They're different
   mechanisms with a different NVS namespace and a different partition size.
   Xiaozhi's 8 MB layout has NVS at `0x9000` with length `0x4000`, and
   Eyemech's has length `0x6000`. Never write an old raw NVS blob into the new
   layout. Recalibrate instead.
