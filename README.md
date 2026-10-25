# DeskPet Hardware

Jetson Orin Nano 8GB, 10.1인치 IPS 터치 LCD(1024×600, mini HDMI·USB-C), ReSpeaker Lite Voice Assistant Kit, PCA9685와 SG90 귀 서보 2개를 위한 구동 패키지입니다. **실제 Linux 드라이버와 하드웨어 없는 모의 실행을 같은 인터페이스로 제공합니다.**

STT·LLM·TTS는 팀 파이프라인에서 연결합니다. 이 패키지는 PCM 녹음·재생, 표정·한글 화면, 귀 동작과 장치 오류 보고를 담당합니다. LCD 터치는 2026-10-16 발주한 10.1인치 화면부터 하드웨어로 확보했지만 입력 처리 코드는 아직 없습니다.

이 저장소는 **뉴라인 팀 조직의 하드웨어 전용 저장소**입니다. 코드, 기술 문서(`docs/`), 회로도·배선 원본(`hardware-design/`)을 여기에서 관리하고 수정·커밋·PR도 여기에서 진행합니다. 팀 위키 `knowledge`에는 진행상황·결정·구매 계획과 팀 간 인터페이스 명세를 둡니다. 초기 구현 커밋 `d81c287`의 코드 이력을 이어갑니다.

- 저장소: [hansung-capstone-team-8-new-line/deskpet-hardware](https://github.com/hansung-capstone-team-8-new-line/deskpet-hardware)
- 진행상황: [하드웨어 진행상황(위키)](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-hardware.md)
- 접근: 비공개 저장소입니다. 해당 조직의 권한이 있는 GitHub 계정으로 로그인하세요.

```sh
git clone https://github.com/hansung-capstone-team-8-new-line/deskpet-hardware.git
cd deskpet-hardware
```

아래 실행 명령은 이 저장소 루트에서 사용합니다.

## 문서와 원본 코드

[구현 개요](docs/architecture.md)에서 전체 구조와 문서 읽는 순서를 확인하고, 출력 모델 담당자는 [표정 명세](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-display-interface.md) → [화면 연결 예제](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-display-integration.md) 순서로 확인하세요.

| 작업 | 원본 파일 |
| --- | --- |
| 표정·눈·입·한글 수정 | [display.cpp](src/display.cpp) |
| 입력 필드·Swift 타입 확인 | [Contracts.swift](swift/Sources/DeskPetHardware/Contracts.swift) |
| Swift에서 화면·음성·귀 호출 | [Adapters.swift](swift/Sources/DeskPetHardware/Adapters.swift) |
| 전체 호출 흐름 예제 | [Swift 데모](swift/Sources/DeskPetHardwareDemo/main.swift) |
| 연결·패킷·결과 이벤트 | [Client.swift](swift/Sources/DeskPetHardware/Client.swift), [server.cpp](src/server.cpp), [protocol.cpp](src/protocol.cpp) |
| 장치 작업 실행·취소 | [runtime.cpp](src/runtime.cpp) |
| 실제 USB 오디오·귀 드라이버 | [audio_alsa.cpp](src/audio_alsa.cpp), [motion_linux.cpp](src/motion_linux.cpp) |
| 모의 장치·설정 | [audio.cpp](src/audio.cpp), [motion.cpp](src/motion.cpp), [config.cpp](src/config.cpp) |
| 기본 설정 파일 | [모의 실행](config/mock.json), [Jetson](config/jetson.json) |
| 실행·보정·설치 | [deskpetctl.py](tools/deskpetctl.py), [install-jetson.sh](tools/install-jetson.sh) |
| 검사 수정 | [통합](tests/integration.py), [화면](tests/visual.py), [C++](tests/core.cpp), [Swift](swift/Tests/DeskPetHardwareTests/WireTests.swift) |
| 회로도·배선 원본과 검증 | [hardware-design/](hardware-design/README.md) |

원본 코드를 고친 뒤 이 저장소에서 커밋합니다. `build/`, `out/`, Swift 빌드 캐시, WAV·BMP, 로컬 보정 설정은 `.gitignore`로 제외합니다. 2026-10-12 분리 당시에는 실행 로직을 유지했습니다. 2026-10-13 구성 정렬에서 1024×600 화면·ReSpeaker·FULL_OFF·입력 게이트를 변경했습니다. 2026-10-16 발주에서 화면이 10.1인치 터치 LCD로 바뀌었으나 해상도가 같아 화면 코드는 그대로입니다. 이전 구현은 Git 이력에 보존됩니다.

## 지금 실행하기

macOS에서 화면까지 보려면 CMake, SDL2, SDL2_ttf가 필요합니다.

```sh
brew install cmake sdl2 sdl2_ttf
cmake -S . -B build -DDESKPET_LINUX=OFF
cmake --build build -j 2
./build/deskpet-hardware --config config/mock.json
```

다른 터미널에서 실행합니다. 모의 모드는 실제 마이크·스피커·서보에 접근하지 않습니다.

```sh
python3 tools/deskpetctl.py health
python3 tools/deskpetctl.py face --activity listening
python3 tools/deskpetctl.py face --text '보고서 제출 기한을 변경했어요.'
python3 tools/deskpetctl.py demo
```

창 없이 통신·상태 흐름만 확인할 수도 있습니다. SDL이 없는 빌드는 `-DDESKPET_SDL=OFF`로 구성하세요.

```sh
./build/deskpet-hardware --config config/mock.json --headless
```

모의 녹음은 기본적으로 16kHz 모노 무음입니다. `audio.wavInput`에 **16kHz·모노·PCM16 WAV** 경로를 넣으면 파일을 20ms씩 실제 시간 간격으로 전달하고 EOF에서 종료합니다. 원본 파일을 수정하지 않습니다.

## 장치가 도착하면

1. [Jetson 설치와 배선](docs/jetson-setup.md)에 따라 화면·USB 오디오·PCA9685를 연결합니다.
2. `bash tools/install-jetson.sh`로 JetPack 6.x/Ubuntu 22.04 환경을 준비합니다.
3. `deskpet-hardware doctor` 결과로 ReSpeaker 오디오·I2C·OE=GND 설정을 채웁니다.
4. 녹음·재생·화면을 각각 시험하고 귀를 보정합니다.
5. [실물 검증표](docs/validation.md)를 진행한 뒤 Swift 파이프라인에 연결합니다.

`config/jetson.json`의 장치 이름을 비워 둔 것은 실물 식별값을 추측하지 않기 위해서입니다. 귀는 `calibrated: false`로 시작하며, 보정 없이 활성화할 수 없습니다. 모의 모드 성공은 실제 전원·배선·음질·모터 동작 검증을 뜻하지 않습니다.

## Swift 파이프라인 연결

Swift 5.10 이상에서 빌드하며 Swift 6의 동시성 검사도 검증 대상으로 둡니다. 외부 패키지 의존성은 없습니다.

```sh
swift build --package-path swift
swift run --package-path swift deskpet-swift-demo
```

예제는 같은 소켓에 연결해 화면을 표시하고, 녹음 중 PCM을 재생한 뒤 귀 동작을 시험합니다. 모의 서버를 먼저 실행하세요. 한 번에 한 클라이언트만 제어할 수 있으므로 Python 데모와 Swift 데모를 동시에 실행하지 않습니다.

- `HardwareMicrophoneSource`: `start / nextFrame / stop`
- `HardwareSpeakerAdapter`: `begin / write / finish / stop`
- `HardwareDisplayAdapter`: `show / idle`
- `HardwareMotionAdapter`: `enable / apply / disable`

[연동 계약](docs/protocol.md)에 메시지 형식·오류·완료 의미와 팀 타입 연결 방법을 정리했습니다. 팀의 비공개 Swift 저장소에 접근할 수 없어 독립 DTO와 연결 예제를 제공합니다. 실제 팀 패키지의 프로토콜 적합성은 소스 확보 후 확인해야 합니다.

## 시험 도구

```sh
python3 tools/deskpetctl.py record out.wav --seconds 5
python3 tools/deskpetctl.py play out.wav
python3 tools/deskpetctl.py play                  # 작은 음량의 440Hz 테스트음
python3 tools/deskpetctl.py motion wiggle
python3 tools/deskpetctl.py demo --fail-speaker   # 모의 오류 → 텍스트 대체
python3 tools/deskpetctl.py face --activity processing --snapshot /tmp/face.bmp
```

텍스트 데모는 15초 뒤 기본 표정으로 돌아갑니다. `face --seconds`로 확인 시간을 바꿀 수 있습니다. 통합 데모는 동시 녹음·재생, 귀 동작, 질문 대기 15초 종료, 예약한 점검 알림과 텍스트 15초 종료를 재현합니다. 이 타이머는 명령 도구에 있으며 하드웨어 서버의 정책이 아닙니다. 귀 시험 명령은 종료 시 출력을 끕니다. `motion enable`만 실행해도 명령 도구가 연결을 닫으면 출력이 다시 꺼지므로, 지속 제어는 하나의 클라이언트 세션 안에서 수행합니다.

## 자동 검증

Swift 6에서 gate 실연결 검사는 `python3 tests/swift_live.py build/deskpet-hardware`로 실행한다. 두 번째·세 번째 인자로 Swift 실행 파일과 scratch 경로를 지정할 수 있다. Swift 5.10에서는 제품의 strict build와 일반 `swift test`를 나눠 실행한다(XCTest 자동 생성 코드의 strict-concurrency 경고 때문).

```sh
ctest --test-dir build --output-on-failure
swift test --package-path swift
DESKPET_SWIFT_DEMO="$(swift build --package-path swift --show-bin-path)/deskpet-swift-demo" \
  python3 tests/integration.py build/deskpet-hardware
```

macOS Command Line Tools만 있는 환경은 XCTest가 없을 수 있습니다. Swift 실행 파일 빌드·왕복 시험은 가능하며 XCTest 단위 검사는 Xcode 또는 Linux Swift 환경에서 실행합니다.

Linux ARM64 검증 환경도 포함했습니다. 이는 실제 Jetson 연결 시험과 별도입니다.

```sh
docker build --platform linux/arm64 -f deploy/Dockerfile.check -t deskpet-check .
docker run --rm --platform linux/arm64 -v "$PWD:/work:ro" deskpet-check
```

코드 구조는 `src/`의 장치·서버 구현, `swift/`의 연결 모듈, `tools/`의 시험·설치 도구, `config/`의 모드별 설정으로 나뉩니다. [검증 기록](docs/validation.md)에서 소프트웨어 검사와 실물 확인 항목을 확인하세요.

### Current Audio Contract — 2026-10-13

ReSpeaker Lite 실제 출력은 **16kHz·모노 s16le**다. 소켓의 8–48kHz·1/2채널 입력 계약은 유지하며 C++ SoXR가 청크 간 상태를 유지해 모노 혼합·변환하고 마지막 필터 표본까지 drain한다. `audio.playbackGain`은 0–0.25(기본 0.25)이고 필터 overshoot도 제한한다. 디지털 진폭 제한은 3W 전력 보장이 아니므로 낮은 음량부터 실제 스피커 왜곡·발열·출력을 측정한다.

동일한 `HardwareClient`를 마이크·스피커가 공유해야 한다. `speaker.begin` 전 클라이언트가 입력 epoch를 바꾸고 큐를 비운 뒤 `input.invalidated`를 발생시킨다. 파이프라인은 이 이벤트에서 VAD/STT 누적 상태를 초기화하고, 프레임의 `inputEpoch`를 발화에 보존하며 늦은 STT final을 업무로 보내기 직전에 `client.isCurrentInputEpoch(epoch)`로 거절한다. 프레임 수신만 다시 시작하고 이전 STT 결과를 수용하면 안 된다.

캡처 자체는 계속되지만 Swift는 재생 중 및 실제 drain/drop 확인 후 300ms까지 프레임을 버린다. 서버의 보수적 `captureEarliestMonotonicNs`로 지연 도착 프레임도 버리며 재전달하지 않는다. `audio.failed`·`audio.cancelled`의 `stoppedConfirmed:false` 또는 연결 상실은 차단 유지다. 장치 정지 원인을 확인하고 세션을 새로 연결한다. 원시 소켓/`deskpetctl`은 진단용 연속 캡처를 제공하므로 직접 사용하는 파이프라인은 같은 차단·epoch 계약을 별도로 구현해야 한다. 새 Swift 클라이언트와 새 데몬을 함께 배포한다.
