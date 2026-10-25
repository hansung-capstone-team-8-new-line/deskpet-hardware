# Orin Nano 보드 시험 — 2026-10-26

내일 목표는 **보드에서 네이티브 빌드와 팀 파이프라인↔장치 프로그램의 실제 소켓 연결**을 확인하는 것입니다.
주변 장치는 모의 실행하고 STT·라우터·업무 API·TTS도 fixture를 사용합니다. `PASS`는 실물 화면·음향·서보 시험을 뜻하지 않습니다.

## 준비한 코드

- `deskpet-hardware`: C++20 ALSA/SDL/PCA9685 프로그램, Swift 클라이언트, 터치 구독, 서보 진폭 제한.
- 형제 폴더 `deskpet-pipeline`: 기존 팀 코어 + `DeskPetHardwareIntegration` + `deskpet-board`.
- 기준: JetPack 6.x / L4T R36 / Ubuntu 22.04 ARM64, Swift **6.0.3**.
- 배포 묶음은 소스입니다. 패키지 설치와 Swift 최초 다운로드에는 네트워크가 필요합니다.

## 1. 압축 해제와 진단

배포 파일과 `.sha256`을 보드에 복사합니다. SSH를 쓸 경우 학교에서 확인한 보드 주소·계정을 사용하세요.

```sh
sha256sum -c deskpet-board-2026-10-26.tar.gz.sha256
tar xzf deskpet-board-2026-10-26.tar.gz
cd deskpet-board
sha256sum -c SHA256SUMS
cd deskpet-hardware
python3 tools/board-doctor.py --output "$HOME/deskpet-doctor.json"
```

`support.status`가 `FAIL`이면 `host.os`, `host.l4t`, `host.model`부터 확인합니다. 이 도구는 OS·펌웨어·핀mux를 바꾸거나 I2C 장치에 쓰지 않습니다. JetPack 5 계열 또는 미부팅 보드는 대여 조건에 맞춰 별도로 OS를 준비한 뒤 진행합니다.

## 2. 보드 설치

일반 사용자 계정으로 실행합니다. 기존 설정·보정 파일은 덮어쓰지 않습니다.

```sh
bash tools/install-jetson.sh board-only
bash tools/setup-swift.sh
source "$HOME/.local/share/deskpet/toolchains/env.sh"
swift --version
```

설치기는 서비스를 등록하지만 새 자동 시작을 활성화하지 않습니다. 이전에 활성화한 서비스가 있다면 시험 전에 `systemctl --user stop deskpet-hardware`로 멈춥니다. 그룹 변경은 다음 로그인부터 적용됩니다.

## 3. 내일 한 번에 실행할 시험

```sh
python3 tools/test-board.py --seconds 1800 --output "$HOME/deskpet-results/2026-10-26"
```

진단 → C++ 실제 Linux 드라이버 빌드 → CTest → Swift 장치 테스트 → 팀 파이프라인·실소켓 테스트 → 30분 반복 실행을 순서대로 수행합니다. 보드는 주변 장치 없이 동작하며, root 실행은 필요 없습니다.

결과 폴더의 `result.json`에서 전체 `status`, 단계별 `PASS/FAIL`, `boardAcceptance`를 확인합니다. 실패한 단계의 `*.log`를 함께 보관하세요. 장치 프로그램은 시험용 임시 소켓을 사용하고 종료 때 정리됩니다. 정상 종료가 지연되어도 실패로 기록합니다.

- 입력은 C++ 모의 캡처의 16kHz mono s16le 640바이트 프레임이 실제 소켓을 통과합니다.
- fixture VAD/STT는 6개 프레임으로 고정 문장을 만듭니다. 실제 음성 인식이 아닙니다.
- 팀 mock 출력의 24kHz PCM을 첫 청크에서 확인하여 재생 프로그램에 전달합니다.
- 실제 재생 완료 ack 이후 300ms와 듣기 화면 ack를 기다려 새 입력을 받습니다.
- 모델·외부 업무 서버는 mock이며 외부 업무 변경을 실행하지 않습니다.
- LCD·터치·USB 전력·실제 서보 이동·온도는 `SKIP`으로 남습니다.

개발 PC의 Linux에서는 `--host-check`를 명시하면 동일 소프트웨어 검증을 할 수 있습니다. 이 경우 `boardAcceptance`는 항상 `SKIP`입니다. 현장 보드 시험에는 이 옵션을 사용하지 않습니다.

## 4. 개별 실행과 터치 진단

수동 실행에서는 하나의 클라이언트만 소켓을 소유합니다. 파이프라인과 `deskpetctl`을 동시에 연결하지 마세요.

```sh
deskpet-hardware --config config/board-only.json
# 다른 터미널, 동일 사용자:
source "$HOME/.local/share/deskpet/toolchains/env.sh"
swift run --package-path ../deskpet-pipeline deskpet-board --seconds 10
```

화면이 있는 개발 환경에서 `--config config/mock.json`으로 띄우면 마우스로 터치를 시험할 수 있습니다. mock의 마우스·합성 입력은 `simulated=true`로 표시합니다. 실제 터치 장치의 SDL 이벤트는 별도로 `simulated=false`입니다. 실제 모드에서는 합성 입력 명령을 거절합니다.

`deskpetctl touch --seconds 30`은 입력 이벤트를 표시합니다. `deskpetctl touch --inject`는 모의 모드에서 down/move/up 이벤트를 넣습니다. 파이프라인 실행 시 터치 이벤트는 진단 스트림으로 전달되며 재생 중지·업무 확인으로 해석하지 않습니다.

## 5. 부품 도착 후 실제 모드

1. `docs/jetson-setup.md`와 최신 회로도로 전원·배선을 확인합니다. PCA9685 OE=GND, 서보 독립 5V 전원·공통 접지를 유지합니다.
2. `deskpet-hardware doctor`로 명시적인 ALSA 카드 이름과 I2C 어댑터를 찾습니다. `/dev/i2c-N` 번호를 추측하지 않습니다.
3. `~/.config/deskpet-hardware/config.json`의 `mode=real`, 캡처·재생 카드, I2C 경로, 화면 설정을 채웁니다.
4. 화면 → 터치 → 5초 녹음·저음량 재생 → 혼 분리 후 서보 중립·범위 보정 순서로 진행합니다. 실제 장치 실패는 모의 성공으로 대체하지 않습니다.
5. 실제 장치 데몬에 `deskpet-board --real`을 연결할 수 있습니다. 여기서도 모델은 fixture입니다. 보정이 끝난 귀만 `--motion`으로 활성화합니다.
6. 수동 시험을 마친 뒤 `bash tools/enable-service.sh`로 로그인 세션 자동 시작을 활성화합니다. 중지는 `systemctl --user stop deskpet-hardware`, 자동 시작 해제는 `systemctl --user disable deskpet-hardware`입니다.

귀 명령 완료는 PWM 명령 완료이며 위치 센서 확인이 아닙니다. OE=GND 회로는 강제 프로세스 종료·I2C 단절에서 PWM 정지를 보장하지 못하므로 기존 설명서의 독립 서보 전원 차단 절차를 유지합니다.

## 소스와 재현성

`manifest.json`은 두 저장소의 기준 HEAD, 작업 트리 변경 포함 여부, 파일별 SHA256을 기록합니다. `SHA256SUMS`로 압축 해제 후 내용을 검사할 수 있습니다. 소스 폴더를 서로 나란히 유지하세요. 파이프라인의 하드웨어 통합 target은 `../deskpet-hardware/swift`를 참조합니다.

```sh
python3 tools/package-board.py ../deskpet-board-2026-10-26.tar.gz
```

빌드 디렉터리·로그·개인 보정 설정은 소스 배포 묶음에 넣지 않습니다. 실제 시험 결과는 `result.json`과 단계 로그를 별도로 공유합니다.
