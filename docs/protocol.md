# 파이프라인 연결 계약 v1

## 연결과 소유권

Unix stream socket을 사용합니다. 기본 경로는 `$XDG_RUNTIME_DIR/deskpet-hardware.sock`, 해당 환경 변수가 없으면 `/tmp/deskpet-<uid>/hardware.sock`입니다. 소켓은 소유자만 접근할 수 있고 같은 사용자 클라이언트 하나만 허용합니다. 두 번째 연결은 닫습니다. 첫 명령은 `hello`입니다.

연결의 기본 유효 시간은 3초입니다. Python·Swift 클라이언트는 1초마다 `ping`을 보냅니다. 연결 종료·유효 시간 만료 시 캡처·재생·귀 동작을 취소하고 화면을 대기로 되돌립니다. 명령 timeout 또는 Swift Task 취소도 연결을 닫습니다. 결과가 불명확한 명령을 자동 재실행하지 않습니다.

헤더는 **네트워크 바이트 순서의 uint32 JSON 길이 + uint32 PCM 길이**, 이어서 UTF-8 JSON, raw PCM입니다. JSON 최대 16KiB, PCM 최대 64KiB. 프레임이 잘못됐으면 연결을 닫으며, 명령 값이 잘못됐으면 해당 명령에 오류를 반환합니다.

```json
{"version":1,"type":"command","commandId":"unique-id","command":"health"}
```

```json
{"version":1,"type":"reply","commandId":"unique-id","status":"ok","result":{}}
```

```json
{"version":1,"type":"reply","commandId":"unique-id","status":"error","error":{"code":"MOTION_DISABLED","message":"Enable motion first"}}
```

`commandId`는 매번 새로 생성합니다. 최근 512개의 중복 명령 ID를 거절하며 재실행하지 않습니다. 영속적인 exactly-once 보장은 아닙니다. `requestId`·`outputId`·`jobId`·`captureId`·`reminderKey`가 있으면 관련 응답·이벤트에 보존합니다. 식별자는 1–128 UTF-8 바이트입니다. 클라이언트가 소유한 PCM은 직렬화 시 복사되므로 네이티브 포인터 수명을 공유하지 않습니다.

## 명령

| 명령 | 추가 정보 | 성공 응답 시점 |
| --- | --- | --- |
| `hello`, `ping`, `health` | 없음 | 버전·모드·상태 확인 |
| `capture.start` | `captureId` | 마이크 열기 성공 |
| `capture.stop` | `captureId` | 스트림 종료·장치 해제 후 |
| `speaker.begin` | `requestId`, `outputId`, `jobId`, `format` | 재생 장치 준비, 아직 재생 완료 아님 |
| `speaker.write` | `jobId`, PCM payload | bounded 큐에 수락, 아직 재생 완료 아님 |
| `speaker.finish` | `jobId` | 모든 수락한 PCM의 재생 버퍼 drain 완료 |
| `speaker.stop` | `jobId` | 재생 drop·취소 후 |
| `display.show` | `requestId`, `outputId`, `revision`, `view` | 해당 프레임 렌더러 제출 후 |
| `display.idle` | `revision` | 기본 표정 프레임 제출 후 |
| `motion.enable`, `motion.disable` | 없음 | OE 전환. enable은 보정 필요 |
| `motion.set` | `preset` 또는 정규화된 `left`, `right` | 설정 범위로 PWM 명령 전달 후 |
| `motion.calibration.begin/set/end` | set에는 `leftUs`, `rightUs` | 사용자가 명시적으로 시작한 보정 세션에 한정 |
| `display.snapshot` | `path` | 모의 SDL 화면 BMP 저장 |
| `mock.fail` | `device`: capture/speaker/display/motion | 다음 해당 장치 작업에 모의 오류 주입 |

PCM `format`은 `{"encoding":"s16le","sampleRate":16000,"channels":1}`입니다. 출력은 8–48kHz, 1·2채널을 지원하며 TTS job 안에서 형식을 변경할 수 없습니다. 스피커 큐는 1초 분량, 모션 명령 큐는 16개입니다. `QUEUE_FULL`인 write payload는 수락되지 않았으므로 같은 데이터에 새 commandId로 재시도할 수 있습니다. 이미 수락한 데이터는 재전송하지 않습니다.

## 입력과 완료 이벤트

이벤트에는 `version:1`, `type:event`, `event`, `timestampUnixNs`가 있습니다.

- `audio.frame`: `captureId`, 연속 `sequence`, PCM `format`, 640바이트 payload. 16kHz·모노·PCM16의 20ms입니다. 타임스탬프는 호스트에서 프레임을 읽은 시점으로 ADC의 정밀 표본 시각이 아닙니다.
- `capture.stopped`: EOF·취소·오류 등 `reason`을 포함합니다.
- `audio.started`: 첫 PCM 재생 시작을 확인했을 때. `begin` 수락을 뜻하지 않습니다.
- `audio.completed`: 마지막 PCM의 drain 이후에만 발생합니다. 빈 job은 `NO_AUDIO`로 실패합니다.
- `audio.failed`, `audio.cancelled`: 실패 또는 취소. 이 출력은 전달 완료로 기록하지 않습니다.
- `display.displayed`: 렌더러에 새 프레임을 제출했음을 뜻합니다. 패널 센서가 없으므로 실제 화면 발광이나 사용자의 읽음을 검증한 것은 아닙니다.
- `motion.commanded`: 설정 범위 내 PWM 명령 완료. `positionVerified:false`이며 실제 귀 위치에 대한 피드백은 없습니다.
- `device.error`: 실패 장치·오류 코드·메시지를 전달합니다.

모의 장치 결과에는 `simulated:true`를 표시합니다. `hello`도 `mode:mock`을 반환합니다. 화면·음성·모션 성공 여부는 서로 독립적이며 업무 성공·실패·사용자 확인 상태를 변경하지 않습니다.

캡처 큐는 서버와 클라이언트에서 각각 50프레임으로 제한합니다. 서버 포화·ALSA overrun/underrun·클라이언트 프레임 유실은 `AUDIO_DISCONTINUITY` 등 오류로 노출합니다. 파이프라인은 해당 STT 세션을 무효화하고 새 캡처 세션으로 재시작해야 합니다. 누락 구간을 정상 발화로 이어 붙이지 않습니다. 클라이언트의 이벤트 큐도 bounded이며 소비하지 않아 포화되면 연결을 닫습니다.

## 화면·업무 정책

`view`의 `displayMode`는 `face` 또는 `text`, `activity`는 `idle/listening/processing/speaking/awaiting_answer`입니다. `emotion`의 `happy/sad`와 `actionStatus`의 `succeeded/failed/unknown`을 기본 얼굴에 반영합니다. 그 외 감정은 중립으로 표시합니다. `displayState`는 전달·확장용이며 현재 화면 선택은 `activity`, `emotion`, `actionStatus`로 정합니다.

텍스트는 `displayText`의 실제 전달 문구를 렌더링합니다. 한 화면에 들어가지 않으면 `DISPLAY_TEXT_TOO_LONG`으로 거절하며, 중요한 날짜·업무 내용을 자동 잘라내지 않습니다. 파이프라인이 짧은 대체 문장을 제공해야 합니다. 이 검사는 실제 글꼴을 사용하는 SDL 모드에서 수행하며 headless 모드에서는 시각적 배치를 검증하지 않습니다.

`revision`은 연결 안에서 단조 증가하는 정수입니다. 이전 번호는 `STALE_OUTPUT`, 아직 렌더링되지 않은 출력이 교체되면 `SUPERSEDED`로 거절합니다. 새 연결은 revision을 다시 시작합니다. 서로 다른 요청 사이의 업무 유효성 판단은 파이프라인 책임입니다.

하드웨어 드라이버는 TTS 실패를 이유로 Eureka 작업을 재실행하거나 업무 결과를 바꾸지 않습니다. 음성 실패 시 텍스트 fallback, 텍스트 15초 복귀, 질문 무응답 15초, 긴 작업·리마인더 타이머는 파이프라인에서 관리합니다. 내장 도구는 화면 표시·15초 종료·질문 대기 종료·예약 알림 타이머와 음성 실패 fallback을 재현합니다. 질문 대기 데모는 음성 인식 없이 무응답 경로만 보여줍니다.

## Swift 연결 예시

팀 패키지에 로컬 의존성을 추가합니다. 다음은 팀 프로젝트와 `deskpet-hardware` 저장소가 같은 상위 폴더에 있는 예시이며 실제 폴더 배치에 맞춰 경로를 조정합니다.

```swift
.package(name: "DeskPetHardware", path: "../deskpet-hardware/swift")
```

사용 target의 의존성에는 `.product(name: "DeskPetHardware", package: "DeskPetHardware")`를 추가합니다.

```swift
let client = try await HardwareClient.connect()
let microphone = HardwareMicrophoneSource(client: client)
let speaker = HardwareSpeakerAdapter(client: client)
let display = HardwareDisplayAdapter(client: client)

// 한 별도 Task에서 client.events를 계속 소비해 오류·취소·장치 상태를 연결합니다.
try await microphone.start()
while let frame = try await microphone.nextFrame() {
    // 팀 AudioFrame(timestamp: frame.timestamp, chunk: ...)으로 변환해 VAD에 전달
}
```

실제 파이프라인의 `SpeakerAdapter.begin(jobId,event)`에는 format이 없으므로, 연결 래퍼에서 TTS 출력 형식을 알고 있다면 begin에 넘깁니다. 형식을 첫 chunk에서만 알 수 있다면 그 래퍼가 첫 chunk까지 장치 begin을 보류해야 합니다. 이 패키지의 speaker write는 format 변경을 거절합니다. 같은 job의 begin → write들 → finish는 순서대로 await합니다.

이 패키지의 `PCMChunk`, `AudioFrame`, `OutputEvent`는 **독립 DTO**입니다. 실제 팀 타입을 중복 정의하거나 import했다고 가정하지 않습니다. 확보한 팀 소스에서 protocol의 정확한 async/throws/Sendable 조건과 타입 생성자를 확인한 뒤 변환 래퍼를 추가하세요. 현재 제공하는 실행 가능한 연결 예제는 `swift/Sources/DeskPetHardwareDemo/main.swift`입니다.
