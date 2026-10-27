#!/usr/bin/env python3
"""Build and package Coglet firmware for the Web Programmer and GitHub releases.

    python3 scripts/package_firmware.py              # package existing builds, every board
    python3 scripts/package_firmware.py --build      # build first
    python3 scripts/package_firmware.py --board c3   # one board

The C3 firmware is this repo's own ESP-IDF app (firmware/app). The S3 is still
built by the xiaozhi-esp32 fork (firmware/xiaozhi-esp32, or XIAOZHI_DIR) with
./switch-board.sh, which finds this repo's controller on its own. Output:

    web-programmer/firmware/<id>/*.bin       what the Web Programmer serves
    web-programmer/manifest-<id>.json        esp-web-tools manifest per board
    dist/release-zips/*.zip               one zip per board, for a GitHub release

Standard library only. ESP-IDF must be installed for --build and merge-bin.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "firmware" / "app"
XIAOZHI = Path(os.environ.get("XIAOZHI_DIR", ROOT / "firmware" / "xiaozhi-esp32")).resolve()
PROGRAMMER = ROOT / "web-programmer"
DIST = ROOT / "dist"

# source: "app" (firmware/app) or "xiaozhi" (the fork); board: the xiaozhi
# board name; id: the Web Programmer id, which is also what the firmware reports to
# agent-hub as its board id and manifest.
BOARDS = {
    "c3": {"source": "app", "board": "coglet-c3", "id": "coglet-c3",
           "name": "Coglet C3 (XIAO ESP32-C3)", "manifest": "manifest-coglet-c3.json"},
    "s3": {"source": "xiaozhi", "board": "coglet", "id": "coglet",
           "name": "Coglet S3 (XIAO ESP32-S3 Sense)", "manifest": "manifest-coglet.json"},
    "s3-phrase": {"source": "xiaozhi", "board": "coglet/phrase", "id": "coglet-phrase",
                  "name": "Coglet S3, typed wake phrase (XIAO ESP32-S3 Sense)",
                  "manifest": "manifest-coglet-phrase.json"},
}

CHIP_FAMILIES = {"esp32c3": "ESP32-C3", "esp32s3": "ESP32-S3"}
IMAGES = ("bootloader/bootloader.bin", "partition_table/partition-table.bin",
          "ota_data_initial.bin", "xiaozhi.bin", "coglet.bin", "generated_assets.bin")


def run(cmd: list[str], cwd: Path) -> str:
    return subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True).stdout


def idf_env() -> dict[str, str]:
    env = os.environ.copy()
    if shutil.which("idf.py"):
        return env
    export = Path(env.get("IDF_PATH", Path.home() / "esp" / "esp-idf")) / "export.sh"
    out = subprocess.run(
        ["bash", "-c", f'. "{export}" >/dev/null 2>&1 && python3 -c "import os,json;print(json.dumps(dict(os.environ)))"'],
        check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def firmware_version(project: Path) -> str:
    for line in (project / "CMakeLists.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith('set(PROJECT_VER "'):
            return line.split('"')[1]
    raise RuntimeError(f"PROJECT_VER not found in {project / 'CMakeLists.txt'}")


def describe(repo: Path) -> str:
    try:
        return run(["git", "describe", "--always", "--dirty", "--tags"], repo).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def embedded_token(sdkconfig: Path) -> bool:
    """switch-board.sh bakes agent-hub's enrollment token into fork builds when it
    finds ../agent-hub/.env, and anyone can set one in firmware/app's sdkconfig.
    Fine on your own bench; not for a public page."""
    for line in sdkconfig.read_text(encoding="utf-8").splitlines():
        if line.startswith("CONFIG_OTA_URL=") and "enrollment_token=" in line:
            return True
    return False


def package(key: str, build: bool, env: dict[str, str], allow_token: bool) -> None:
    entry = BOARDS[key]
    if entry["source"] == "app":
        project, build_dir, app_bin = APP, APP / "build", "coglet.bin"
        sdkconfig = APP / "sdkconfig"
        if build:
            subprocess.run(["idf.py", "build"], cwd=APP, check=True, env=env)
    else:
        if not (XIAOZHI / "switch-board.sh").exists():
            raise SystemExit(f"xiaozhi-esp32 not found at {XIAOZHI}; run: just setup (or set XIAOZHI_DIR)")
        # switch-board.sh names nested boards' build dirs with dashes.
        project, build_dir, app_bin = XIAOZHI, XIAOZHI / f"build-{entry['board'].replace('/', '-')}", "xiaozhi.bin"
        sdkconfig = build_dir / "sdkconfig"
        if build:
            subprocess.run(["bash", "./switch-board.sh", entry["board"], "build"], cwd=XIAOZHI, check=True, env=env)
    if not (build_dir / app_bin).exists():
        raise SystemExit(f"No firmware at {build_dir / app_bin}; run with --build")
    if embedded_token(sdkconfig) and not allow_token:
        raise SystemExit(
            f"{entry['board']}: CONFIG_OTA_URL carries an agent-hub enrollment token. Anyone who\n"
            "downloads this firmware can read it. Rebuild with AGENT_HUB_ENV_FILE=/dev/null for a\n"
            "public Web Programmer, or pass --allow-enrollment-token for a private, local-only one.")
    subprocess.run(["idf.py", "-B", str(build_dir), f"-DSDKCONFIG={sdkconfig}", "merge-bin"],
                   cwd=project, check=True, env=env)

    out = PROGRAMMER / "firmware" / entry["id"]
    for old in out.glob("*.bin"):
        old.unlink()
    out.mkdir(parents=True, exist_ok=True)
    for image in IMAGES + ("merged-binary.bin",):
        src = build_dir / image
        if src.exists():
            shutil.copy2(src, out / src.name)

    flasher_args = json.loads((build_dir / "flasher_args.json").read_text(encoding="utf-8"))
    chip = flasher_args.get("extra_esptool_args", {}).get("chip", "")
    parts = []
    for offset, path in sorted(flasher_args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        name = Path(path).name
        if (out / name).exists():
            parts.append({"path": f"firmware/{entry['id']}/{name}", "offset": int(offset, 16)})

    version = firmware_version(project)
    manifest = {
        "name": entry["name"],
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": CHIP_FAMILIES.get(chip, chip.upper()), "parts": parts}],
    }
    (PROGRAMMER / entry["manifest"]).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    metadata = {"board": entry["board"], "flasher_board_id": entry["id"], "manifest": entry["manifest"],
                "firmware_version": version, "coglet": describe(ROOT)}
    if entry["source"] == "xiaozhi":
        metadata["xiaozhi"] = describe(XIAOZHI)
    (out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    zips = DIST / "release-zips"
    zips.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zips / f"coglet-{key}_v{version}.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(out.iterdir()):
            if path.name != ".gitkeep":
                archive.write(path, arcname=path.name)
    print(f"{entry['id']}: {len(parts)} parts -> web-programmer/{entry['manifest']}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--board", choices=sorted(BOARDS), help="package one board (default: all)")
    parser.add_argument("--build", action="store_true", help="build first")
    parser.add_argument("--allow-enrollment-token", action="store_true",
                        help="package a build whose OTA URL embeds an agent-hub enrollment token")
    args = parser.parse_args()
    env = idf_env()
    for key in [args.board] if args.board else sorted(BOARDS):
        package(key, args.build, env, args.allow_enrollment_token)


if __name__ == "__main__":
    main()
