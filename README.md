# DeskPet Hardware

Jetson Orin Nano 8GB, Waveshare 6.25인치 HDMI LCD, USB TO AUDIO, PCA9685와 SG90 귀 서보 2개를 위한 구동 패키지입니다. **실제 Linux 드라이버와 하드웨어 없는 모의 실행을 같은 인터페이스로 제공합니다.**

STT·LLM·TTS는 팀 파이프라인에서 연결합니다. 이 패키지는 PCM 녹음·재생, 표정·한글 화면, 귀 동작과 장치 오류 보고를 담당합니다. 터치 입력은 사용하지 않습니다.

이 저장소는 **뉴라인 팀 조직의 하드웨어 전용 소스 저장소**입니다. 이후 코드 수정·커밋·PR은 여기에서 진행합니다. 팀 위키 `knowledge`에는 설계·인터페이스·검증 문서와 이 저장소 링크를 유지합니다. 초기 구현 커밋 `d81c287`의 코드 이력을 이어갑니다.

- 저장소: [hansung-capstone-team-8-new-line/deskpet-hardware](https://github.com/hansung-capstone-team-8-new-line/deskpet-hardware)
- 팀 문서: [하드웨어 문서 목차](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/topics/deskpet-hardware.md)
- 접근: 비공개 저장소입니다. 해당 조직의 권한이 있는 GitHub 계정으로 로그인하세요.

```sh
git clone https://github.com/hansung-capstone-team-8-new-line/deskpet-hardware.git
cd deskpet-hardware
```

아래 실행 명령은 이 저장소 루트에서 사용합니다.

## 문서와 원본 코드

[구현 개요](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-hardware-runtime.md)에서 전체 구조를 읽고, 출력 모델 담당자는 [표정 명세](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-display-interface.md) → [화면 연결 예제](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-display-integration.md) 순서로 확인하세요.

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

원본 코드를 고친 뒤 이 저장소에서 커밋합니다. `build/`, `out/`, Swift 빌드 캐시, WAV·BMP, 로컬 보정 설정은 `.gitignore`로 제외합니다. 저장소를 분리하면서 하드웨어 실행 로직과 설정 예제는 변경하지 않았으며, 문서와 연결 경로만 독립 저장소에 맞췄습니다.

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
3. `deskpet-hardware doctor` 결과로 오디오·I2C·OE 설정을 채웁니다.
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

코드 구조는 `src/`의 장치·서버 구현, `swift/`의 연결 모듈, `tools/`의 시험·설치 도구, `config/`의 모드별 설정으로 나뉩니다. [위키 검증 기록](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-hardware-validation.md)에서 소프트웨어 검사와 실물 확인 항목을 확인하세요.
