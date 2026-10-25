#!/usr/bin/env python3
"""DeskPet local control and bring-up tools. Python standard library only."""
import argparse
import json
import math
import os
import queue
import socket
import struct
import sys
import threading
import time
import uuid
import wave
from pathlib import Path

MAX_JSON, MAX_PCM = 16384, 65536


class DeviceError(RuntimeError):
    def __init__(self, code, message):
        super().__init__(f"{code}: {message}")
        self.code = code


def default_socket():
    return str(Path(os.environ.get("XDG_RUNTIME_DIR", f"/tmp/deskpet-{os.getuid()}")) /
               ("deskpet-hardware.sock" if "XDG_RUNTIME_DIR" in os.environ else "hardware.sock"))


def encode(meta, data=b""):
    header = json.dumps(meta, ensure_ascii=False, separators=(",", ":")).encode()
    if not 0 < len(header) <= MAX_JSON or len(data) > MAX_PCM:
        raise ValueError("Message exceeds protocol limits")
    return struct.pack("!II", len(header), len(data)) + header + data


def receive(sock):
    def exact(n):
        result = bytearray()
        while len(result) < n:
            data = sock.recv(n - len(result))
            if not data:
                raise EOFError("Hardware connection closed")
            result.extend(data)
        return bytes(result)
    j, p = struct.unpack("!II", exact(8))
    if not 0 < j <= MAX_JSON or p > MAX_PCM:
        raise ValueError("Invalid frame lengths")
    meta = json.loads(exact(j))
    if meta.get("version") != 1:
        raise ValueError("Unsupported protocol version")
    return meta, exact(p)


class Client:
    def __init__(self, path=None, heartbeat=True):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path or default_socket())
        self.write_lock, self.lock = threading.Lock(), threading.Lock()
        self.pending = {}
        self.events = queue.Queue(256)
        self.frames = queue.Queue(50)
        self.closed = threading.Event()
        self.capture_error = None
        self.capture_id = None
        self.sequence = None
        self.revision = 0
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.info = self.request("hello")
        if heartbeat:
            self.pinger = threading.Thread(target=self._heartbeat, daemon=True)
            self.pinger.start()

    def _heartbeat(self):
        while not self.closed.wait(max(0.1, self.info["leaseMs"] / 3000)):
            try:
                self.request("ping", timeout=2)
            except Exception:
                self.close()

    def _read(self):
        error = None
        try:
            while not self.closed.is_set():
                meta, data = receive(self.sock)
                if meta.get("type") == "reply":
                    with self.lock:
                        waiter = self.pending.get(meta["commandId"])
                    if waiter:
                        waiter.put_nowait((meta, data))
                elif meta.get("event") == "audio.frame":
                    if meta.get("captureId") != self.capture_id:
                        continue
                    if self.sequence is not None and meta["sequence"] != self.sequence + 1:
                        raise DeviceError("AUDIO_DISCONTINUITY", "Capture sequence gap")
                    self.sequence = meta["sequence"]
                    try:
                        self.frames.put_nowait((meta, data))
                    except queue.Full:
                        raise DeviceError("AUDIO_DISCONTINUITY", "Client capture queue overflow") from None
                else:
                    if meta.get("event") == "device.error" and meta.get("device") == "capture":
                        self.capture_error = DeviceError(meta["code"], meta["message"])
                    if meta.get("event") == "capture.stopped" and meta.get("captureId") == self.capture_id:
                        self.frames.put_nowait((meta, b""))
                    try:
                        self.events.put_nowait((meta, data))
                    except queue.Full:
                        raise DeviceError("EVENT_BACKLOG", "Consume hardware events before the event queue fills") from None
        except Exception as exc:
            error = exc
        finally:
            self.closed.set()
            self.capture_error = error or EOFError("Connection closed")
            with self.lock:
                for waiter in self.pending.values():
                    try:
                        waiter.put_nowait(self.capture_error)
                    except queue.Full:
                        pass
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def request(self, command, data=b"", timeout=5, **fields):
        if self.closed.is_set():
            raise EOFError("Hardware connection is closed")
        ident = str(uuid.uuid4())
        waiter = queue.Queue(1)
        with self.lock:
            self.pending[ident] = waiter
        try:
            wire = encode(dict(fields, version=1, type="command", commandId=ident, command=command), data)
            with self.write_lock:
                self.sock.sendall(wire)
            result = waiter.get(timeout=timeout)
            if isinstance(result, Exception):
                raise result
            meta, _ = result
            if meta["status"] != "ok":
                raise DeviceError(meta["error"]["code"], meta["error"]["message"])
            return meta["result"]
        except queue.Empty:
            # Do not leave an operation running after its result becomes unknown.
            self.close()
            raise DeviceError("TIMEOUT", f"No reply for {command}; connection closed") from None
        finally:
            with self.lock:
                self.pending.pop(ident, None)

    def show(self, activity="idle", text=None, status=None, output_id=None, reminder_key=None):
        self.revision += 1
        view = {"displayMode": "text" if text else "face", "activity": activity}
        if text:
            view["displayText"] = text
        if status:
            view["actionStatus"] = status
        identifiers = {"requestId": "cli-request", "outputId": output_id or str(uuid.uuid4())}
        if reminder_key is not None:
            identifiers["reminderKey"] = reminder_key
        return self.request("display.show", revision=self.revision, view=view, **identifiers)

    def capture_start(self):
        self.capture_id = str(uuid.uuid4())
        self.sequence, self.capture_error = None, None
        while not self.frames.empty():
            self.frames.get_nowait()
        self.request("capture.start", captureId=self.capture_id)

    def next_frame(self, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.capture_error:
                raise self.capture_error
            try:
                meta, data = self.frames.get(timeout=0.05)
                return None if not data else (meta, data)
            except queue.Empty:
                continue
        raise DeviceError("TIMEOUT", "No capture frame")

    def capture_stop(self):
        self.request("capture.stop", captureId=self.capture_id)

    def play(self, pcm, rate=16000, channels=1):
        job = str(uuid.uuid4())
        self.request("speaker.begin", jobId=job, requestId="cli-request", outputId=job,
                     format={"sampleRate": rate, "channels": channels, "encoding": "s16le"})
        block = max(2 * channels, rate // 50 * channels * 2)
        for offset in range(0, len(pcm), block):
            while True:
                try:
                    self.request("speaker.write", jobId=job, data=pcm[offset:offset + block])
                    break
                except DeviceError as error:
                    if error.code != "QUEUE_FULL":
                        raise
                    time.sleep(0.02)
        return self.request("speaker.finish", jobId=job, timeout=8)

    def close(self):
        self.closed.set()
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def tone(seconds=0.8, rate=16000):
    count = int(rate * seconds)
    # Low level and a short fade avoid a loud click during the first speaker test.
    return b"".join(struct.pack("<h", int(1500 * math.sin(2 * math.pi * 440 * i / rate) *
                    min(1, i / 160, (count - i) / 160))) for i in range(count))


def calibrate(client, config_path):
    config = json.loads(Path(config_path).read_text())
    saved = {}
    print("서보 혼/귀를 분리한 상태에서 중립부터 확인하세요. q: 출력 차단 후 종료")
    client.request("motion.calibration.begin")
    l, r = config["servo"]["left"]["neutralUs"], config["servo"]["right"]["neutralUs"]
    try:
        for side in ("left", "right"):
            print(f"\n{side}: ±20us씩 이동합니다. -, +, n(중립 저장), a(최솟값), b(최댓값), done")
            values = {}
            while True:
                answer = input(f"left={l}us right={r}us > ").strip()
                if answer == "q":
                    return
                current = l if side == "left" else r
                if answer in ("+", "-"):
                    value = current + (20 if answer == "+" else -20)
                    if not 1000 <= value <= 2000:
                        print("허용 범위는 1000..2000us입니다.")
                        continue
                    if side == "left":
                        l = value
                    else:
                        r = value
                    client.request("motion.calibration.set", leftUs=l, rightUs=r)
                elif answer in ("n", "a", "b"):
                    values[{"n": "neutralUs", "a": "minUs", "b": "maxUs"}[answer]] = current
                elif answer == "done":
                    if set(values) != {"neutralUs", "minUs", "maxUs"} or not 1000 <= values["minUs"] < values["neutralUs"] < values["maxUs"] <= 2000:
                        print("n, a, b를 모두 저장하고 min < neutral < max를 확인하세요.")
                        continue
                    values["inverted"] = input("정규화된 + 방향을 반전할까요? [y/N] ").lower() == "y"
                    saved[side] = values
                    if side == "left":
                        l = values["neutralUs"]
                    else:
                        r = values["neutralUs"]
                    client.request("motion.calibration.set", leftUs=l, rightUs=r)
                    break
        for side, values in saved.items():
            config["servo"][side].update(values)
        config["servo"]["calibrated"] = True
        target = Path(config_path).with_name("local.calibrated.json")
        if target.exists():
            target = target.with_name(f"local.calibrated-{int(time.time())}.json")
        target.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
        print(f"저장 완료: {target}\n이 설정으로 프로그램을 재시작한 뒤 motion enable을 실행하세요.")
    finally:
        client.request("motion.calibration.end")


def main():
    parser = argparse.ArgumentParser(description="DeskPet 장치 시험·모의 실행 도구")
    parser.add_argument("--socket", default=default_socket())
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("health")
    face = sub.add_parser("face")
    face.add_argument("--activity", default="idle", choices=["idle", "listening", "processing", "speaking", "awaiting_answer"])
    face.add_argument("--status", choices=["succeeded", "failed", "unknown"])
    face.add_argument("--text")
    face.add_argument("--seconds", type=float, default=15)
    face.add_argument("--snapshot")
    rec = sub.add_parser("record")
    rec.add_argument("output")
    rec.add_argument("--seconds", type=float, default=5)
    play = sub.add_parser("play")
    play.add_argument("wav", nargs="?")
    motion = sub.add_parser("motion")
    motion.add_argument("preset", choices=["enable", "disable", "neutral", "perk", "droop", "wiggle"])
    motion.add_argument("--intensity", type=float, default=1)
    touch = sub.add_parser("touch")
    touch.add_argument("--seconds", type=float, default=30)
    touch.add_argument("--inject", action="store_true")
    sub.add_parser("calibrate").add_argument("--config", required=True)
    demo = sub.add_parser("demo")
    demo.add_argument("--fail-speaker", action="store_true")
    args = parser.parse_args()
    with Client(args.socket) as client:
        if args.command == "health":
            print(json.dumps(client.request("health"), ensure_ascii=False, indent=2))
        elif args.command == "face":
            client.show(args.activity, args.text, args.status)
            if args.snapshot:
                client.request("display.snapshot", path=str(Path(args.snapshot).resolve()))
            time.sleep(max(0, args.seconds))
            client.show()
        elif args.command == "record":
            client.capture_start()
            with wave.open(args.output, "wb") as wav:
                wav.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
                try:
                    for _ in range(max(1, int(args.seconds * 50))):
                        frame = client.next_frame()
                        if frame is None:
                            break
                        wav.writeframesraw(frame[1])
                finally:
                    try:
                        client.capture_stop()
                    except DeviceError as error:
                        if error.code != "UNKNOWN_CAPTURE":
                            raise
            print(f"녹음 저장: {args.output}")
        elif args.command == "play":
            if args.wav:
                with wave.open(args.wav, "rb") as wav:
                    if wav.getsampwidth() != 2 or wav.getcomptype() != "NONE":
                        raise ValueError("WAV must contain signed PCM16")
                    if wav.getnframes() > wav.getframerate() * 120:
                        raise ValueError("This test tool limits WAV playback to 120 seconds")
                    client.play(wav.readframes(wav.getnframes()), wav.getframerate(), wav.getnchannels())
            else:
                client.play(tone())
            print("재생 완료" + (" (모의)" if client.info["mode"] == "mock" else ""))
        elif args.command == "touch":
            client.request("touch.subscribe")
            if args.inject:
                for phase, x in (("down", .2), ("move", .5), ("up", .8)):
                    client.request("mock.touch", phase=phase, contactId="cli", x=x, y=.5)
            deadline = time.monotonic() + max(0, args.seconds)
            while time.monotonic() < deadline:
                try:
                    meta, _ = client.events.get(timeout=.1)
                    if meta.get("event") == "touch.event":
                        print(json.dumps(meta, ensure_ascii=False), flush=True)
                except queue.Empty:
                    pass
            client.request("touch.subscribe", enabled=False)
        elif args.command == "motion":
            if args.preset in ("enable", "disable"):
                print(client.request("motion." + args.preset))
            else:
                client.request("motion.enable")
                print(client.request("motion.set", preset=args.preset, intensity=args.intensity))
                time.sleep(0.3)
                client.request("motion.disable")
        elif args.command == "calibrate":
            calibrate(client, args.config)
        elif args.command == "demo":
            print("실행 모드:", client.info["mode"])
            for state in ("idle", "listening", "processing"):
                client.show(state)
                time.sleep(0.6)
            client.capture_start()
            captured = []
            collecting = threading.Event()
            def collect():
                while not collecting.is_set():
                    try:
                        frame = client.next_frame()
                        if frame is None:
                            break
                        captured.append(frame[0]["sequence"])
                    except (DeviceError, EOFError):
                        break
            worker = threading.Thread(target=collect, daemon=True)
            worker.start()
            try:
                client.show("speaking", status="succeeded")
                if args.fail_speaker:
                    client.request("mock.fail", device="speaker")
                try:
                    client.play(tone(1))
                except DeviceError as error:
                    print("음성 실패, 텍스트로 전환:", error)
                    client.show(text="요청은 처리했어요.\n음성 출력 장치를 확인해 주세요.")
                    time.sleep(15)
            finally:
                client.capture_stop()
                collecting.set()
                worker.join(timeout=3)
            try:
                client.request("motion.enable")
                client.request("motion.set", preset="wiggle")
            except DeviceError as error:
                print("귀 동작 생략:", error)
            finally:
                client.request("motion.disable")
            # This deadline belongs to the demo pipeline, never the device server.
            reminder_deadline = time.monotonic() + 18
            client.show("awaiting_answer")
            time.sleep(15)
            client.show()
            print("질문 무응답 15초: 데모의 대기를 종료했습니다. 외부 작업 취소는 하지 않습니다.")
            time.sleep(max(0, reminder_deadline - time.monotonic()))
            client.show(text="실물 점검 알림이에요.\n장치가 도착하면 화면과 음성부터 확인해 주세요.",
                        reminder_key="bring-up-demo")
            print("리마인더 타이머: 예약한 점검 알림을 표시했습니다.")
            time.sleep(15)
            client.show()
            print(f"음성 출력 시험 동안 마이크 {len(captured)}프레임을 받았습니다.")
            print("통합 데모 완료")


if __name__ == "__main__":
    try:
        main()
    except (DeviceError, OSError, EOFError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        pass
