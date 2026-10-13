#!/usr/bin/env python3
"""Real Unix socket tests against the compiled daemon; no physical devices needed."""
import json
import os
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from deskpetctl import Client, DeviceError, encode, receive, tone

BINARY = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else str(ROOT / "build/deskpet-hardware")


class Daemon:
    def __init__(self, **overrides):
        self.temp = tempfile.TemporaryDirectory(prefix="deskpet-test-", dir="/tmp")
        self.path = str(Path(self.temp.name) / "hardware.sock")
        config = json.loads((ROOT / "config/mock.json").read_text())
        config["display"]["headless"] = True
        config["leaseMs"] = 1000
        for key, value in overrides.items():
            config[key] = value
        path = Path(self.temp.name) / "config.json"
        path.write_text(json.dumps(config))
        self.log = open(Path(self.temp.name) / "daemon.log", "w+")
        self.process = subprocess.Popen([BINARY, "--config", str(path), "--socket", self.path], stdout=self.log, stderr=self.log)
        deadline = time.monotonic() + 5
        while not Path(self.path).exists():
            if self.process.poll() is not None or time.monotonic() > deadline:
                self.log.seek(0)
                message = self.log.read()
                if self.process.poll() is None:
                    self.process.kill()
                self.process.wait(timeout=5)
                self.log.close()
                self.temp.cleanup()
                raise RuntimeError(message)
            time.sleep(0.01)

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            raise AssertionError("Daemon did not shut down cooperatively")
        self.log.seek(0)
        log = self.log.read()
        self.log.close()
        self.temp.cleanup()
        if self.process.returncode != 0:
            raise AssertionError(f"Daemon exited {self.process.returncode}: {log}")


class Integration(unittest.TestCase):
    def setUp(self):
        self.daemon = Daemon()
        self.client = Client(self.daemon.path)

    def tearDown(self):
        self.client.close()
        self.daemon.close()

    def error(self, code, fn):
        with self.assertRaises(DeviceError) as result:
            fn()
        self.assertEqual(result.exception.code, code)

    def event(self, name, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                meta, data = self.client.events.get(timeout=0.1)
                if meta.get("event") == name:
                    return meta, data
            except queue.Empty:
                pass
        self.fail("Missing event: " + name)

    def begin(self, job="job"):
        return self.client.request("speaker.begin", jobId=job, requestId="request", outputId="output",
                                   format={"sampleRate": 16000, "channels": 1, "encoding": "s16le"})

    def test_full_duplex_and_delivery_order(self):
        self.client.capture_start()
        received = []
        def capture():
            while True:
                frame = self.client.next_frame()
                if frame is None:
                    return
                received.append(frame)
        worker = threading.Thread(target=capture)
        worker.start()
        self.begin()
        start = time.monotonic()
        self.client.request("speaker.write", jobId="job", data=tone(0.3))
        self.client.request("speaker.finish", jobId="job")
        elapsed = time.monotonic() - start
        self.client.capture_stop()
        worker.join(timeout=3)
        self.assertFalse(worker.is_alive())
        self.assertGreater(elapsed, 0.27)
        self.assertGreater(len(received), 8)
        self.assertEqual([x[0]["sequence"] for x in received], list(range(len(received))))
        self.assertTrue(all(len(x[1]) == 640 for x in received))
        started, _ = self.event("audio.started")
        completed, _ = self.event("audio.completed")
        self.assertEqual((started["jobId"], completed["outputId"], completed["requestId"]), ("job", "output", "request"))
        self.assertTrue(completed["simulated"])

    def test_cancel_during_finish_never_completes(self):
        self.begin()
        self.client.request("speaker.write", jobId="job", data=tone(0.8))
        outcome = []
        def finish():
            try:
                self.client.request("speaker.finish", jobId="job")
                outcome.append("completed")
            except DeviceError as error:
                outcome.append(error.code)
        worker = threading.Thread(target=finish)
        worker.start()
        time.sleep(0.05)
        self.client.request("speaker.stop", jobId="job")
        worker.join(timeout=3)
        self.assertEqual(outcome, ["CANCELLED"])
        self.event("audio.cancelled")
        while not self.client.events.empty():
            self.assertNotEqual(self.client.events.get()[0]["event"], "audio.completed")

    def test_empty_and_misaligned_audio(self):
        self.begin()
        self.error("INVALID_PCM", lambda: self.client.request("speaker.write", jobId="job", data=b"a"))
        self.error("NO_AUDIO", lambda: self.client.request("speaker.finish", jobId="job"))
        self.event("audio.failed")
        self.error("INVALID_FORMAT", lambda: self.client.request("speaker.begin", jobId="bad", requestId="r", outputId="o",
                   format={"sampleRate": 96000, "channels": 1}))

    def test_unconfirmed_stop_is_not_success(self):
        self.begin()
        self.client.request("speaker.write", jobId="job", data=tone(0.5))
        self.client.request("mock.fail", device="speaker-stop")
        self.error("AUDIO_STOP_UNCONFIRMED", lambda: self.client.request("speaker.stop", jobId="job"))
        cancelled, _ = self.event("audio.cancelled")
        self.assertFalse(cancelled["stoppedConfirmed"])

    def test_playback_backpressure(self):
        self.begin()
        results = []
        for _ in range(4):
            try:
                self.client.request("speaker.write", jobId="job", data=tone(0.8))
                results.append("ok")
            except DeviceError as error:
                results.append(error.code)
        self.assertIn("QUEUE_FULL", results)
        self.client.request("speaker.stop", jobId="job")

    def test_stale_display_and_channel_failure_isolation(self):
        self.client.show("processing")
        self.error("STALE_OUTPUT", lambda: self.client.request("display.show", requestId="old", outputId="old",
                   revision=1, view={"activity": "idle"}))
        self.client.request("mock.fail", device="display")
        self.error("DISPLAY_ERROR", lambda: self.client.show("speaking"))
        self.client.show(text="보고서 제출 기한을 변경했어요.", status="succeeded")
        self.begin()
        self.client.request("mock.fail", device="speaker")
        failure, _ = self.event("audio.failed")
        self.assertEqual(failure["code"], "AUDIO_OUTPUT_ERROR")
        self.client.show(text="보고서 제출 기한을 변경했어요.", status="succeeded")
        self.assertTrue(self.client.request("health")["display"]["available"])

    def test_capture_fault_stops_stream_and_restarts_at_zero(self):
        self.client.capture_start()
        self.client.next_frame()
        self.client.request("mock.fail", device="capture")
        meta, _ = self.event("device.error")
        self.assertEqual(meta["device"], "capture")
        self.event("capture.stopped")
        with self.assertRaises(DeviceError):
            self.client.next_frame()
        self.client.capture_start()
        self.assertEqual(self.client.next_frame()[0]["sequence"], 0)
        self.client.capture_stop()

    def test_client_capture_overflow_is_visible(self):
        self.client.capture_start()
        self.assertTrue(self.client.closed.wait(2.5))
        self.error("AUDIO_DISCONTINUITY", self.client.next_frame)

    def test_servo_bounds_fault_and_disable(self):
        self.client.request("motion.enable")
        result = self.client.request("motion.set", preset="perk")
        self.assertEqual((result["leftUs"], result["rightUs"]), (1600, 1400))
        self.assertFalse(result["positionVerified"])
        self.error("MOTION_RANGE", lambda: self.client.request("motion.set", left=2, right=0))
        self.client.request("mock.fail", device="motion")
        self.error("SERVO_ERROR", lambda: self.client.request("motion.set", preset="neutral"))
        self.assertFalse(self.client.request("health")["motion"]["enabled"])

    def test_disconnect_disables_servo(self):
        self.client.request("motion.enable")
        self.client.close()
        time.sleep(0.1)
        self.client = Client(self.daemon.path)
        self.assertFalse(self.client.request("health")["motion"]["enabled"])

    def test_single_controller_and_lease(self):
        second = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        second.settimeout(2)
        second.connect(self.daemon.path)
        self.assertEqual(second.recv(1), b"")
        second.close()
        self.client.close()
        time.sleep(0.1)
        self.client = Client(self.daemon.path, heartbeat=False)
        self.client.request("motion.enable")
        self.assertTrue(self.client.closed.wait(2))
        self.client.close()
        time.sleep(0.1)
        self.client = Client(self.daemon.path)
        self.assertFalse(self.client.request("health")["motion"]["enabled"])

    def test_malformed_packet_disconnect_and_recovery(self):
        self.client.close()
        time.sleep(0.1)
        raw = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        raw.settimeout(2)
        raw.connect(self.daemon.path)
        raw.sendall(struct.pack("!II", 16385, 0))
        self.assertEqual(raw.recv(1), b"")
        raw.close()
        time.sleep(0.1)
        self.client = Client(self.daemon.path)
        self.assertEqual(self.client.request("health")["mode"], "mock")

    def test_duplicate_command_not_reexecuted(self):
        self.client.close()
        time.sleep(0.1)
        raw = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        raw.settimeout(2)
        raw.connect(self.daemon.path)
        message = encode({"version": 1, "type": "command", "command": "hello", "commandId": "fixed"})
        raw.sendall(message + message)
        self.assertEqual(receive(raw)[0]["status"], "ok")
        self.assertEqual(receive(raw)[0]["error"]["code"], "DUPLICATE_COMMAND")
        raw.close()

    def test_calibration_gate_and_manual_pulses(self):
        config = json.loads((ROOT / "config/jetson.json").read_text())["servo"]
        daemon = Daemon(servo=config)
        try:
            with Client(daemon.path) as client:
                self.error("CALIBRATION_REQUIRED", lambda: client.request("motion.enable"))
                client.request("motion.calibration.begin")
                pose = client.request("motion.calibration.set", leftUs=1480, rightUs=1520)
                self.assertEqual(pose["leftUs"], 1480)
                self.error("MOTION_RANGE", lambda: client.request("motion.calibration.set", leftUs=2500, rightUs=1500))
                client.request("motion.calibration.end")
                self.assertFalse(client.request("health")["motion"]["enabled"])
        finally:
            daemon.close()

    def test_wav_input_eof_and_padding(self):
        path = Path(self.daemon.temp.name) / "input.wav"
        data = struct.pack("<h", 1000) * 400
        with wave.open(str(path), "wb") as wav:
            wav.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
            wav.writeframes(data)
        daemon = Daemon(audio={"wavInput": str(path)})
        try:
            with Client(daemon.path) as client:
                client.capture_start()
                received = []
                while True:
                    frame = client.next_frame()
                    if frame is None:
                        break
                    received.append(frame[1])
                self.assertEqual(len(received), 2)
                self.assertEqual(b"".join(received), data + bytes(480))
        finally:
            daemon.close()

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux driver error paths")
    def test_real_mode_missing_devices_does_not_fall_back_to_mock(self):
        daemon = Daemon(mode="real", audio={"captureDevice": "", "playbackDevice": ""})
        try:
            with Client(daemon.path) as client:
                self.assertEqual(client.info["mode"], "real")
                self.error("AUDIO_DEVICE_ERROR", client.capture_start)
                self.error("AUDIO_DEVICE_ERROR", lambda: client.request("speaker.begin", jobId="real", requestId="r", outputId="o",
                           format={"sampleRate": 16000, "channels": 1}))
                self.error("SERVO_ERROR", lambda: client.request("motion.enable"))
                self.assertFalse(client.request("health")["motion"]["enabled"])
        finally:
            daemon.close()

    @unittest.skipUnless(os.environ.get("DESKPET_SWIFT_DEMO"), "Set DESKPET_SWIFT_DEMO for the Swift round-trip test")
    def test_swift_round_trip(self):
        self.client.close()
        time.sleep(0.1)
        result = subprocess.run([os.environ["DESKPET_SWIFT_DEMO"], self.daemon.path], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Swift round trip passed", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
