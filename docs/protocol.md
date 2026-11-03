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
| `motion.enable`, `motion.disable` | 없음 | PWM FULL_OFF 설정·해제. OE=GND, enable은 보정 필요 |
| `motion.set` | `preset` 또는 정규화된 `left`, `right` | 설정 범위로 PWM 명령 전달 후 |
| `motion.calibration.begin/set/end` | set에는 `leftUs`, `rightUs` | 사용자가 명시적으로 시작한 보정 세션에 한정 |
| `display.snapshot` | `path` | 모의 SDL 화면 BMP 저장 |
| `mock.fail` | `device`: capture/speaker/speaker-stop/display/motion | 다음 해당 장치 작업에 모의 오류 주입 |

PCM `format`은 `{"encoding":"s16le","sampleRate":16000,"channels":1}`입니다. 출력은 8–48kHz, 1·2채널을 지원하며 TTS job 안에서 형식을 변경할 수 없습니다. 스피커 큐는 1초 분량, 모션 명령 큐는 16개입니다. `QUEUE_FULL`인 write payload는 수락되지 않았으므로 같은 데이터에 새 commandId로 재시도할 수 있습니다. 이미 수락한 데이터는 재전송하지 않습니다.

## 입력과 완료 이벤트

이벤트에는 `version:1`, `type:event`, `event`, `timestampUnixNs`가 있습니다.

- `audio.frame`: `captureId`, 연속 `sequence`, PCM `format`, 640바이트 payload, `captureEarliestMonotonicNs`(실제 캡처 버퍼 길이를 뺀 monotonic 하한). 16kHz·모노·PCM16의 20ms입니다. 타임스탬프는 호스트에서 프레임을 읽은 시점으로 ADC의 정밀 표본 시각이 아닙니다.
- `capture.stopped`: EOF·취소·오류 등 `reason`을 포함합니다.
- `audio.started`: 첫 PCM 재생 시작을 확인했을 때. `begin` 수락을 뜻하지 않습니다.
- `audio.completed`: 마지막 PCM의 drain 이후에만 발생합니다. 빈 job은 `NO_AUDIO`로 실패합니다.
- `audio.failed`, `audio.cancelled`: 실패 또는 취소. `stoppedConfirmed`로 drain/drop 정지 확인 여부를 보고하며 false면 STT gate를 유지합니다. 이 출력은 전달 완료로 기록하지 않습니다.
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

### Current Audio Contract — 2026-10-13

ReSpeaker Lite 실제 출력은 **16kHz·모노 s16le**다. 소켓의 8–48kHz·1/2채널 입력 계약은 유지하며 C++ SoXR가 청크 간 상태를 유지해 모노 혼합·변환하고 마지막 필터 표본까지 drain한다. `audio.playbackGain`은 0–0.25(기본 0.25)이고 필터 overshoot도 제한한다. 디지털 진폭 제한은 3W 전력 보장이 아니므로 낮은 음량부터 실제 스피커 왜곡·발열·출력을 측정한다.

동일한 `HardwareClient`를 마이크·스피커가 공유해야 한다. `speaker.begin` 전 클라이언트가 입력 epoch를 바꾸고 큐를 비운 뒤 `input.invalidated`를 발생시킨다. 파이프라인은 이 이벤트에서 VAD/STT 누적 상태를 초기화하고, 프레임의 `inputEpoch`를 발화에 보존하며 늦은 STT final을 업무로 보내기 직전에 `client.isCurrentInputEpoch(epoch)`로 거절한다. 프레임 수신만 다시 시작하고 이전 STT 결과를 수용하면 안 된다.

캡처 자체는 계속되지만 Swift는 재생 중 및 실제 drain/drop 확인 후 300ms까지 프레임을 버린다. 서버의 보수적 `captureEarliestMonotonicNs`로 지연 도착 프레임도 버리며 재전달하지 않는다. `audio.failed`·`audio.cancelled`의 `stoppedConfirmed:false` 또는 연결 상실은 차단 유지다. 장치 정지 원인을 확인하고 세션을 새로 연결한다. 원시 소켓/`deskpetctl`은 진단용 연속 캡처를 제공하므로 직접 사용하는 파이프라인은 같은 차단·epoch 계약을 별도로 구현해야 한다. 새 Swift 클라이언트와 새 데몬을 함께 배포한다.

## Swift 어댑터 요약

| 연결 클래스 | 호출 | 역할 |
| --- | --- | --- |
| `HardwareClient` | `connect`, `command`, `events`, `close` | 소켓 연결·명령·이벤트 수신. `command("health")`로 상태 조회 |
| `HardwareMicrophoneSource` | `start`, `nextFrame`, `stop` | 마이크 프레임 스트림 |
| `HardwareSpeakerAdapter` | `begin`, `write`, `finish`, `stop` | 형식을 유지하는 스트리밍 재생 |
| `HardwareDisplayAdapter` | `show`, `idle` | 화면 출력과 revision 관리 |
| `HardwareMotionAdapter` | `enable`, `apply`, `disable` | 귀 활성화·동작·차단 |

[Swift 데모](../swift/Sources/DeskPetHardwareDemo/main.swift)는 이벤트를 소비하는 Task와 마이크를 소비하는 Task를 두고, 마이크를 받는 동안 440Hz PCM을 재생한 뒤 귀와 화면을 정리합니다.

## 화면 명령 직접 보내기

Swift 클라이언트를 쓰면 아래 봉투와 패킷을 직접 구현할 필요가 없습니다. 다른 언어에서 직접 연결할 때는 첫 명령 `hello`, 주기적 `ping`, 단일 제어 연결, 응답·이벤트 소비를 함께 구현해야 합니다. 다른 장치를 제어 중인 연결과 별도로 화면 연결을 하나 더 여는 방식은 지원하지 않습니다.

```json
{
  "version": 1,
  "type": "command",
  "commandId": "cmd-104",
  "command": "display.show",
  "requestId": "req-018",
  "outputId": "out-018",
  "revision": 4,
  "view": {
    "activity": "speaking",
    "emotion": "happy",
    "actionStatus": "succeeded",
    "displayState": "default",
    "displayMode": "face",
    "displayText": ""
  }
}
```

화면 명령의 PCM 길이는 `0`입니다. 개행으로 구분하는 JSON 문자열이나 HTTP 요청을 보내는 프로토콜이 아닙니다. 표시 성공 시 `display.displayed` 이벤트와 해당 `commandId`의 성공 응답이 나옵니다. Swift의 `show`는 성공 응답을 기다리고, `client.events`는 같은 표시 결과 이벤트를 전달하므로 상위 업무를 이중 실행하지 않습니다.

`display.idle`의 Swift 호출에는 출력 ID 인자가 없습니다. 연결을 추적해야 하는 화면 복귀라면 `OutputEvent(requestId: requestId, outputId: outputId, activity: "idle", emotion: "neutral", displayMode: "face", actionStatus: nil)`를 `show`로 보냅니다.

## 화면 오류

| 오류 | 원인·처리 |
| --- | --- |
| `INVALID_COMMAND` | 허용하지 않은 `activity`·모드, 빈 텍스트, 잘못된 타입·ID 등. 전송 값 수정 |
| `DISPLAY_TEXT_TOO_LONG` | 실제 글꼴 기준 화면을 넘음. 사실을 유지한 더 짧은 문장으로 새 요청 |
| `STALE_OUTPUT` | 이미 적용한 번호 이하의 revision. 어댑터 중복·명령 순서 확인 |
| `SUPERSEDED` | 표시 전에 새 화면으로 대체됨. 현재 상태를 유지하고 옛 화면 재전송 금지 |
| `DISPLAY_ERROR` | 글꼴·SDL·렌더링 또는 주입한 오류. health 확인, 정상 표시로 기록하지 않음 |
| 연결 종료·timeout | 화면·오디오·모션 연결 수명 확인. 전송한 명령의 결과를 임의로 성공 처리하지 않음 |

현재 Swift 클라이언트는 명령 timeout이나 진행 중 명령 Task 취소 시 연결을 닫습니다. 화면만 취소할 생각으로 공유 하드웨어 명령 Task를 임의 취소하면 다른 장치에도 연결 종료가 영향을 줄 수 있습니다. 이전 출력은 가능한 한 제출 전에 현재 토큰 검사로 걸러냅니다.

## 터치 확장 (v1, 선택적 구독)

- `touch.subscribe`의 `enabled` 기본값은 true입니다. false는 구독과 접촉 상태를 비웁니다. 구독은 연결 세션에 귀속되며 재접속 시 다시 요청합니다.
- `touch.event`: `phase=down/move/up/cancel`, `contactId` 문자열, 회전 역변환 후 `x/y` 정규화 좌표(0..1), `timestampUnixNs`, `simulated`를 보냅니다. PCM payload는 없습니다. `contactId=*`인 cancel은 모든 접촉을 취소합니다.
- SDL 이동은 렌더 주기별 접촉당 하나로 합치고, 송신 큐의 이동도 합칩니다. 터치 큐 상한 초과 시 cancel을 보내고 구독을 다시 요청할 때까지 터치 전달을 중단합니다. 장치 완료·오류를 위한 큐 공간을 남깁니다.
- 중복 down과 활성 접촉 없는 move/up은 무시합니다. 접촉은 최대 10개이며 과도한 down/up은 cancel로 종료합니다.
- `mock.touch`는 `phase/contactId/x/y`로 합성 입력을 넣습니다. real에서는 `MOCK_ONLY`입니다. 마우스 입력도 mock 모드에서만 터치로 변환합니다.
- 터치 자체를 음성 중지·업무 확인으로 해석하지 않습니다. Swift `TouchEvent`와 팀 `TouchInput`으로 전달합니다.
- 화면은 구독 여부와 상관없이 터치에 스스로 반응합니다(누른 곳 바라보기, 길게 누르면 기대기, 눈·입·수염 톡, 두 번 톡 방긋, 연타 깜짝, 스와이프 따라보기, 문지르면 3초 쓰다듬기). 화면 반응은 메시지를 보내지 않으며 `touch.event` 전달 방식은 그대로입니다. 자세한 동작은 [도트 표정](face-catalog.md#상호작용과-환경-반응)을 참고하세요.

`motion.set`에는 선택적 `intensity`(기본 1, 유한한 0..1)가 추가되었습니다. preset의 정규화 진폭에 곱한 뒤 각 귀의 보정 범위로 변환하며 범위 밖 값은 `MOTION_RANGE`입니다.

## 팀 통합 입력 수명

`DeskPetHardwareIntegration.HardwareBridge`는 한 클라이언트와 한 이벤트 소비자를 공유합니다. 실제 캡처는 계속 드레인하고 입력 전달만 보류합니다. `holdInputDelivery` 후 `resumeInputDelivery(epoch:)`는 현재 epoch와 실제 재생 종료+300ms를 검사하고 복귀 시각보다 오래된 프레임을 버립니다. 전달 보류 동안에도 캡처 watchdog은 1초 이상 장치 프레임이 없으면 오류로 연결을 종료합니다.

파이프라인은 프레임·음성 세션의 epoch를 검사하고 재생 시작 전에 세션을 무효화합니다. 취소 호출이 비동기로 지연되어도 늦은 결과는 유효해지지 않습니다. 듣기 화면 ack와 VAD reset 뒤 전달을 재개하며 정지 불명은 차단을 유지합니다. `ManagedMicrophoneSource`만 자체 watchdog을 사용하고 일반 입력 소스의 기존 timeout은 유지합니다.
