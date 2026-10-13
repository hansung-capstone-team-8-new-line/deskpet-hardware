#!/usr/bin/env python3
"""Run Swift unit/live gate tests against a fresh mock daemon.

Usage: python3 tests/swift_live.py build/deskpet-hardware [swift] [scratch-path]
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[1]
binary = str(Path(sys.argv[1]).resolve())
swift = sys.argv[2] if len(sys.argv) > 2 else "swift"
scratch = sys.argv[3] if len(sys.argv) > 3 else str(root / "swift/.build")
with tempfile.TemporaryDirectory(prefix="deskpet-swift-", dir="/tmp") as temp:
    socket = str(Path(temp) / "hardware.sock")
    process = subprocess.Popen([binary, "--mock", "--headless", "--socket", socket])
    try:
        for _ in range(500):
            if Path(socket).exists():
                break
            if process.poll() is not None:
                raise RuntimeError("Mock daemon exited")
            time.sleep(.01)
        else:
            raise RuntimeError("Mock socket did not appear")
        result = subprocess.run([swift, "test", "--package-path", str(root / "swift"),
            "--scratch-path", scratch, "-Xswiftc", "-strict-concurrency=complete",
            "-Xswiftc", "-warnings-as-errors"], env=dict(os.environ, DESKPET_TEST_SOCKET=socket))
        if result.returncode:
            raise SystemExit(result.returncode)
    finally:
        process.terminate()
        process.wait(timeout=5)
