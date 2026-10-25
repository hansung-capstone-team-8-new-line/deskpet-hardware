#!/usr/bin/env python3
"""Native build + actual socket integration + bounded soak; writes PASS/FAIL/SKIP evidence."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--seconds", type=int, default=1800)
    p.add_argument("--host-check", action="store_true", help="Explicit non-Jetson software validation; never marks board acceptance PASS")
    args = p.parse_args()
    if not 0 <= args.seconds <= 86400:
        p.error("seconds must be 0..86400")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    pipeline = ROOT.parent / "deskpet-pipeline"
    report = {"started": datetime.now(timezone.utc).isoformat(), "architecture": platform.machine(),
              "peripherals": "simulated", "models": "fixture", "boardAcceptance": "SKIP" if args.host_check else "PENDING",
              "stages": [], "physicalTests": {k: "SKIP" for k in ("LCD", "touch", "ReSpeaker", "servo", "power", "thermal")}}
    def run(name, command, timeout=600, env=None):
        started = time.monotonic()
        log = out / (name + ".log")
        entry = {"name": name, "command": list(map(str, command)), "log": log.name, "status": "FAIL"}
        report["stages"].append(entry)
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout, env=env, text=True)
        entry["seconds"] = round(time.monotonic() - started, 3)
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}); see {log}")
        entry["status"] = "PASS"
        print(f"PASS {name}", flush=True)
    try:
        run("doctor", [sys.executable, ROOT / "tools/board-doctor.py", "--output", out / "doctor.json"] + ([] if args.host_check else ["--require-supported"]))
        swift = shutil.which("swift")
        if not swift:
            raise RuntimeError("Swift 6.0.3 is required. Run tools/setup-swift.sh, then source its env.sh")
        version = subprocess.check_output([swift, "--version"], text=True)
        if "Swift version 6.0.3" not in version:
            raise RuntimeError("Expected Swift 6.0.3; found " + version)
        report["swift"] = version.strip()
        report["sources"] = {}
        for repo in (ROOT, pipeline):
            manifest = ROOT.parent / "manifest.json"
            if (repo / ".git").exists():
                report["sources"][repo.name] = {"head": subprocess.check_output(["git", "-C", repo, "rev-parse", "HEAD"], text=True).strip(),
                    "dirty": bool(subprocess.check_output(["git", "-C", repo, "status", "--porcelain"], text=True))}
            elif manifest.exists():
                report["sources"][repo.name] = json.loads(manifest.read_text())["repositories"][repo.name]
        build = out / "hardware-build"
        swift_build = out / "pipeline-build"
        run("cmake", ["cmake", "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Debug", "-DDESKPET_LINUX=ON", "-DDESKPET_SDL=ON"])
        run("cpp-build", ["cmake", "--build", build, "-j", "2"], 1200)
        run("cpp-tests", ["ctest", "--test-dir", build, "--output-on-failure"], 180)
        binary = build / "deskpet-hardware"
        run("hardware-swift", [sys.executable, ROOT / "tests/swift_live.py", binary, swift, out / "hardware-swift-build"], 1200)
        with tempfile.TemporaryDirectory(prefix="deskpet-board-") as temp:
            socket = Path(temp) / "hardware.sock"
            with (out / "daemon.log").open("w") as log:
                daemon = subprocess.Popen([binary, "--config", ROOT / "config/board-only.json", "--socket", socket], stdout=log, stderr=log)
                try:
                    for _ in range(500):
                        if socket.exists():
                            break
                        if daemon.poll() is not None:
                            raise RuntimeError("Daemon failed to start")
                        time.sleep(.01)
                    else:
                        raise RuntimeError("Daemon startup timeout")
                    env = dict(os.environ, DESKPET_TEST_SOCKET=str(socket))
                    run("pipeline-tests", [swift, "test", "--package-path", pipeline, "--scratch-path", swift_build, "-j", "2", "-Xswiftc", "-strict-concurrency=complete", "-Xswiftc", "-warnings-as-errors"], 1200, env)
                    time.sleep(.1)
                    run("board-smoke", [swift_build / "debug/deskpet-board", "--socket", socket, "--seconds", str(args.seconds)], args.seconds + 120)
                finally:
                    daemon.terminate()
                    try:
                        daemon.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        daemon.kill(); daemon.wait(); raise RuntimeError("Daemon shutdown timed out")
                    if daemon.returncode != 0:
                        raise RuntimeError(f"Daemon shutdown failed: {daemon.returncode}")
        report["status"] = "PASS"
        if not args.host_check:
            report["boardAcceptance"] = "PASS: native build and simulated-peripheral integration only"
    except Exception as exc:
        report["status"] = "FAIL"; report["error"] = str(exc)
        if not args.host_check:
            report["boardAcceptance"] = "FAIL"
        print(str(exc), file=sys.stderr)
    finally:
        report["finished"] = datetime.now(timezone.utc).isoformat()
        (out / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
