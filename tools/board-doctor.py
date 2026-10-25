#!/usr/bin/env python3
"""Read-only pre-build diagnostics. Never opens I2C devices or changes the OS."""
import argparse
import glob
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess


def read(path):
    try:
        return Path(path).read_text(errors="replace").replace("\0", "").strip()
    except OSError:
        return None


def command(args):
    if not shutil.which(args[0]):
        return {"status": "MISSING", "command": args}
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=10)
        return {"status": "PASS" if result.returncode == 0 else "FAIL", "output": (result.stdout + result.stderr).strip()}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"status": "FAIL", "output": str(exc)}


def diagnose():
    os_release = read("/etc/os-release") or ""
    l4t = read("/etc/nv_tegra_release")
    model = read("/proc/device-tree/model")
    supported = platform.machine() == "aarch64" and 'VERSION_ID="22.04"' in os_release and bool(l4t and l4t.startswith("# R36")) and bool(model and "Orin Nano" in model)
    return {
        "schemaVersion": 1, "readOnly": True,
        "host": {"architecture": platform.machine(), "kernel": platform.release(), "model": model,
                 "os": os_release, "l4t": l4t, "memory": read("/proc/meminfo"), "disk": dict(zip(("total", "used", "free"), shutil.disk_usage(".")))},
        "support": {"status": "PASS" if supported else "FAIL", "required": "Jetson Orin / aarch64 / L4T R36 (JetPack 6.x) / Ubuntu 22.04",
                    "nextAction": "Run install-jetson.sh" if supported else "Confirm the loaned board and OS. Prepare JetPack 6.x separately; no automatic flashing."},
        "tools": {name: command([name, "--version"]) for name in ("g++", "cmake", "swift", "python3")},
        "session": {name: os.environ.get(name) for name in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "XDG_SESSION_TYPE")},
        "audio": {"capture": command(["arecord", "-l"]), "playback": command(["aplay", "-l"])},
        "usb": command(["lsusb"]),
        "i2c": [{"device": "/dev/" + Path(p).name, "name": read(p + "/name"), "accessible": os.access("/dev/" + Path(p).name, os.R_OK | os.W_OK)} for p in glob.glob("/sys/class/i2c-dev/*")],
        "input": [{"event": Path(p).name, "name": read(p + "/device/name"), "capabilities": read(p + "/device/capabilities/abs")} for p in glob.glob("/sys/class/input/event*")],
        "physicalTests": {name: "SKIP: requires physical acceptance" for name in ("LCD", "touch", "ReSpeaker", "servo", "power", "thermal")},
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-supported", action="store_true")
    args = parser.parse_args()
    report = diagnose()
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    raise SystemExit(2 if args.require_supported and report["support"]["status"] != "PASS" else 0)
