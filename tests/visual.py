#!/usr/bin/env python3
"""Render the actual SDL code to BMPs and verify text overflow and rotation."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from deskpetctl import Client, DeviceError

binary = str(Path(sys.argv[1]).resolve())
output = Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / "out/preview").resolve()
output.mkdir(parents=True, exist_ok=True)
scenes = [
    ("idle", "idle", None, None, None),
    ("listening", "listening", None, None, None),
    ("processing", "processing", None, None, None),
    ("speaking", "speaking", None, "succeeded", "happy"),
    ("awaiting-answer", "awaiting_answer", None, None, None),
    ("happy", "idle", None, None, "happy"),
    ("sad", "idle", None, None, "sad"),
    ("failed", "speaking", None, "failed", "sad"),
    ("unknown", "idle", None, "unknown", None),
    ("petted", "idle", None, None, "petted"),
    ("sleepy", "idle", None, None, "sleepy"),
    ("surprised", "idle", None, None, "surprised"),
    ("korean-text", "idle", "실험 결과를 비교하고 분석 보고서를 제출해야 해요.\n마감은 2026년 10월 16일 오후 3시예요.", None, None),
]
for rotation in (0, 90):
    with tempfile.TemporaryDirectory(prefix="deskpet-visual-", dir="/tmp") as temp:
        config = json.loads((ROOT / "config/mock.json").read_text())
        config["display"]["rotation"] = rotation
        config["display"]["fullscreen"] = False
        config_path = Path(temp) / "config.json"
        config_path.write_text(json.dumps(config))
        socket_path = str(Path(temp) / "hardware.sock")
        env = dict(os.environ, SDL_VIDEODRIVER="dummy")
        process = subprocess.Popen([binary, "--config", str(config_path), "--socket", socket_path], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            for _ in range(300):
                if Path(socket_path).exists():
                    break
                if process.poll() is not None:
                    raise RuntimeError(process.communicate())
                time.sleep(0.01)
            with Client(socket_path) as client:
                health = client.request("health")
                assert health["display"]["available"], health
                for name, activity, text, status, emotion in scenes:
                    client.show(activity, text, status, emotion=emotion)
                    target = output / f"{name}-{rotation}.bmp"
                    client.request("display.snapshot", path=str(target))
                    with target.open("rb") as image:
                        image.seek(18)
                        dimensions = struct.unpack("<ii", image.read(8))
                    assert dimensions == ((1024, 600) if rotation == 0 else (600, 1024)), dimensions
                try:
                    client.show(text="마감 날짜와 시간을 빠뜨리지 마세요. " * 100)
                    raise AssertionError("Overlong text was silently accepted")
                except DeviceError as error:
                    assert error.code in ("DISPLAY_TEXT_TOO_LONG", "INVALID_COMMAND"), error
                client.show(text="새 안내를 정상적으로 표시해요.")
        finally:
            process.terminate()
            stdout, stderr = process.communicate(timeout=5)
            if process.returncode != 0:
                raise RuntimeError(stdout + stderr)
print(f"Rendered {len(scenes) * 2} screens; Korean overflow and rotation checks passed: {output}")
