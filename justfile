# Coglet — build, test, flash and serve the Web Programmer.
#
# The C3 builds from this repo's own ESP-IDF app, firmware/app. The S3 still
# builds through the xiaozhi-esp32 fork, a submodule at firmware/xiaozhi-esp32;
# to use another checkout of it (for example a sibling ../xiaozhi-esp32 with
# unpushed changes), set XIAOZHI_DIR.

# ESP-IDF's export.sh needs bash; under sh it fails silently.
set shell := ["bash", "-c"]

app := justfile_directory() / "firmware/app"
xiaozhi := env("XIAOZHI_DIR", justfile_directory() / "firmware/xiaozhi-esp32")
idf := ". ${IDF_PATH:-$HOME/esp/esp-idf}/export.sh >/dev/null 2>&1"

# board names: c3 -> coglet-c3, s3 -> coglet (wake word "Computer"),
# s3-phrase -> coglet/phrase (a typed wake phrase, "hey coglet" by default)
board_c3 := "coglet-c3"
board_s3 := "coglet"
board_s3_phrase := "coglet/phrase"

default:
    @just --list

# Fetch the xiaozhi-esp32 fork (only the S3 needs it)
setup:
    git submodule update --init --recursive

# Host tests: the real controller against a fake ESP-IDF; opens no device
test:
    python3 firmware/tests/run_host_tests.py

# Build firmware for c3 (firmware/app/build), s3 or s3-phrase (the fork's build-coglet, build-coglet-phrase)
build board:
    {{ if board == "c3" { "cd " + app + " && " + idf + " && idf.py build" } else if board == "s3" { "cd " + xiaozhi + " && " + idf + " && ./switch-board.sh " + board_s3 + " build" } else if board == "s3-phrase" { "cd " + xiaozhi + " && " + idf + " && ./switch-board.sh " + board_s3_phrase + " build" } else { error("board is c3, s3 or s3-phrase") } }}

# Flash over USB (cut servo V+ first: opening the port can reset the MCU)
flash board port="/dev/ttyACM0":
    {{ if board == "c3" { "cd " + app + " && " + idf + " && idf.py -p " + port + " flash" } else if board == "s3" { "cd " + xiaozhi + " && " + idf + " && ./switch-board.sh " + board_s3 + " flash " + port } else if board == "s3-phrase" { "cd " + xiaozhi + " && " + idf + " && ./switch-board.sh " + board_s3_phrase + " flash " + port } else { error("board is c3, s3 or s3-phrase") } }}

# Serial console at 115200 (Ctrl-] to quit)
monitor port="/dev/ttyACM0":
    {{idf}} && python3 -m serial.tools.miniterm --raw {{port}} 115200

# Package built firmware into web-programmer/ and dist/ (add --build to build first)
package *args:
    XIAOZHI_DIR={{xiaozhi}} python3 scripts/package_firmware.py {{args}}

# Serve the Web Programmer on localhost
serve port="8080":
    web-programmer/serve.sh {{port}}

# Refresh the vendored esp-web-tools (for offline events)
vendor-programmer:
    web-programmer/get_esp_web_tools.sh

# Turn a calibration export into reviewable restore commands
restore-commands file:
    python3 tools/restore_commands.py {{file}}
