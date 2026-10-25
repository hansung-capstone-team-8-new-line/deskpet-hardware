# Jetson 설치와 첫 구동

## 기준 장치

- Jetson Orin Nano 8GB 정품 개발자 키트, P3768 캐리어
- JetPack 6.x / Ubuntu 22.04 ARM64
- 10.1인치 IPS 터치 LCD 1024×600, mini HDMI 입력·USB-C 터치 (2026-10-16 발주, 11/3 도착 예정)
- ReSpeaker Lite Voice Assistant Kit, USB 마이크·키트 동봉 밀폐형 스피커
- PCA9685, SG90 위치제어 서보 2개, 독립 5V 5A 서보 전원

7.x/Ubuntu 24.04나 다른 캐리어 보드에서의 호환성은 검증하지 않았습니다. 설치기는 OS·펌웨어·핀mux를 자동 변경하지 않습니다.

## 연결

전원을 끄고 다음 기준으로 연결합니다. 아래는 **최신 HDMI/USB/PCA9685 구성의 코드용 배선 제안**입니다. 기존 SPI/I2S Fritzing 배선표와 혼용하지 않습니다.

| 연결 | 설정·동작 |
| --- | --- |
| Jetson DisplayPort → DP-HDMI 변환기(NEXI NXC067) → LCD 동봉 HDMI 케이블 → LCD mini HDMI | LCD가 1024×600 가로 모드로 잡히는지 먼저 확인 |
| Jetson USB → LCD USB-C | 터치 신호와 전원 공급 여부를 실물·설명서로 확인. SDL 터치 이벤트는 `touch.subscribe`로 수신 |
| Jetson USB-A → USB-C 데이터 케이블 → ReSpeaker Lite | 내장 마이크로 입력, 이 모듈의 스피커 단자로 출력 |
| ReSpeaker 스피커 단자 → 키트 동봉 밀폐형 스피커 | 실물의 좌우 채널 및 +/− 표기를 확인. 증폭 출력의 어느 쪽도 GND에 연결하지 않음 |
| Jetson 헤더 물리 1번 3.3V → PCA9685 VCC | I2C 풀업도 3.3V로 유지. 서보 전원 V+와 구분 |
| Jetson 물리 3번 SDA·5번 SCL → PCA9685 SDA·SCL | 실제 Linux I2C 어댑터는 doctor로 식별 |
| PCA9685 채널 0·1 → 좌우 SG90 신호 | 채널 및 좌우 반전은 설정으로 변경 가능 |
| 독립 5V 5A 어댑터 → DC 연장선(암단자 쪽 절단) → MINI 3A 퓨즈 → PCA9685 서보 V+ | 보드의 5V 출력으로 서보 전원을 공급하지 않음 |
| Jetson·PCA9685·서보 전원 GND | 공통 접지. 독립 전원의 +극과 Jetson의 +극은 연결하지 않음 |
| PCA9685 OE | GND 고정. GPIO·버퍼 없이 아래 FULL_OFF 방식 사용 |

전해 콘덴서는 서보 전원 쪽에 배치하고 극성을 확인합니다. USB 오디오와 화면의 스피커 단자는 서로 다른 출력입니다. 본 코드는 **USB 오디오**를 사용하며 HDMI 오디오로 자동 전환하지 않습니다.

### OE=GND와 PWM 정지

현재 KiCad 수정안 A는 OE를 GND에 고정한다. GPIO 7/16번, HC125, OE 풀업을 추가하지 않는다. I2C 초기화 첫 쓰기와 정상 종료·취소·연결 만료·검출한 오류에서는 `ALL_LED_OFF_H=0x10`으로 모든 PWM을 끈다. 중립값을 미리 쓸 때 각 채널 FULL_OFF를 유지하고 명시적 enable에서 CH0/CH1만 해제한다. 정지 쓰기가 실패하면 `SERVO_STOP_UNCONFIRMED`다.

OE가 상시 활성인 회로이므로 프로세스 강제 종료·커널 정지·I2C 단절에서 PWM 차단을 보장할 수 없다. 독립 서보 전원 차단 수단을 확보하고 전원 순서·부팅·재시작을 실측한다. `motion.commanded`는 위치 센서 확인이 아니다. 현재 회로는 [위키 회로도](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-wiring-schematic.md)를 따른다.

## 설치

일반 사용자로 프로젝트 폴더에서 실행합니다.

```sh
bash tools/install-jetson.sh real
deskpet-hardware doctor
```

설치기는 의존성, `/usr/local/bin` 실행 파일, 하드웨어 접근 그룹·udev 규칙, 사용자 서비스를 설치합니다. 기존 사용자 설정은 덮어쓰지 않고 자동 시작을 새로 활성화하지 않습니다. 새 그룹 권한을 적용하려면 로그아웃·로그인이 필요합니다. 보드 단독·Swift 설치와 팀 통합은 [보드 시험 안내](board-test.md)를 따릅니다.

`~/.config/deskpet-hardware/config.json`에 다음 값을 채웁니다.

1. `audio.captureDevice`·`audio.playbackDevice`: doctor에 나온 USB 카드의 `plughw:CARD=<실제 카드 ID>,DEV=0` 이름. 두 장치를 모두 해당 USB 카드로 지정합니다. 동일 이름의 USB 장치가 여러 개면 장치 식별을 먼저 해결합니다.
2. `servo.i2cDevice`: 물리 3·5번에 연결된 어댑터의 `/dev/i2c-N`. 번호만 추측하거나 모든 버스에 쓰지 않습니다.
3. `servo.oeMode`: `grounded`. 예전 `gpioChipLabel`/`gpioLine` 설정은 오류로 거절하므로 기존 설정을 백업한 뒤 새 예제와 수동 병합한다.
4. `display`: width=1024, height=600, rotation=0, fontSize=36. OS 회전과 중복 적용하지 않는다.
5. `audio.playbackGain`: 0–0.25. ReSpeaker Lite 카드의 입력·출력을 모두 선택하고 낮은 음량부터 확인한다.

앱 내부 디자인 좌표는 기본 1024×600입니다. 실제 화면의 해상도는 Ubuntu 디스플레이 설정에서 먼저 확인합니다. 코드가 시스템 전체의 해상도를 임의로 변경하지 않습니다. 한국어 글꼴은 `fonts-noto-cjk` 패키지를 사용합니다.

## 첫 장치 시험

자동 서비스와 수동 실행을 동시에 띄우지 않습니다.

```sh
systemctl --user stop deskpet-hardware
deskpet-hardware --config "$HOME/.config/deskpet-hardware/config.json"
```

다른 터미널에서:

```sh
deskpetctl health
deskpetctl face --activity listening
deskpetctl face --text '화면과 한글 출력을 확인하고 있어요.'
deskpetctl record /tmp/deskpet-mic.wav --seconds 5
deskpetctl play /tmp/deskpet-mic.wav
```

마이크와 스피커는 별도 ALSA 스트림입니다. 선택한 장치가 다른 프로그램에 점유돼 있으면 오류가 표시됩니다. Ubuntu 소리 설정에서 USB 카드가 다른 앱에 사용 중인지 확인하고, 장치 이름을 다시 확인하세요. 전체 오디오 시스템을 임의로 중지하지 않습니다.

재생 중 캡처는 계속됩니다. 이 패키지에는 AEC가 없으므로 스피커 소리가 마이크로 들어오는 정도를 측정한 뒤 팀 파이프라인의 AEC를 연결해야 합니다. 반향 문제를 마이크 음소거로 숨기지 않습니다.

## 귀 보정

초기에는 서보 혼과 귀 기구를 분리하고 중립부터 확인합니다. 설정의 1400–1600µs는 예시이며 해당 기구의 안전 범위라는 보장이 아닙니다. 자동 끝점 왕복은 하지 않습니다.

```sh
deskpetctl calibrate --config "$HOME/.config/deskpet-hardware/config.json"
```

좌우를 20µs 단위로 이동해 중립·최솟값·최댓값·반전 여부를 저장합니다. 기구를 연결한 후 간섭이 생기기 전 범위를 사용하세요. 1000–2000µs를 넘어가는 펄스는 거절합니다. 완료하면 같은 폴더에 `local.calibrated.json`을 저장하고 출력을 끕니다. 설정 파일 이름이 이미 존재하면 다른 이름으로 저장합니다.

기존 `config.json`을 별도로 보관하고 보정 결과를 서비스 설정으로 사용한 뒤 프로그램을 재시작합니다. 보정 파일의 `calibrated: true`는 사용자가 확인한 보정의 기록이며 센서 측정 결과가 아닙니다.

```sh
deskpetctl motion neutral
deskpetctl motion perk
deskpetctl motion droop
deskpetctl motion wiggle
```

## 로그인 후 자동 시작

Ubuntu 그래픽 세션에서 다음을 실행합니다.

```sh
systemctl --user import-environment DISPLAY XAUTHORITY WAYLAND_DISPLAY
systemctl --user start deskpet-hardware
systemctl --user status deskpet-hardware
journalctl --user -u deskpet-hardware -f
```

서비스는 그래픽 로그인 뒤 시작하고 로그아웃 때 종료합니다. 전원 투입 후 로그인 없이 화면을 띄우려면 기기 운영 계정의 자동 로그인을 별도로 설정해야 합니다. 설치기는 이를 자동으로 켜지 않습니다. 서비스 중지는 `systemctl --user stop deskpet-hardware`, 자동 시작 해제는 `systemctl --user disable deskpet-hardware`입니다.

## 근거

- [ReSpeaker Lite](https://wiki.seeedstudio.com/reSpeaker_lite/)
- [PCA9685 데이터시트](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf)
- [Linux I2C 사용자 공간 인터페이스](https://www.kernel.org/doc/html/latest/i2c/dev-interface.html)
- [systemd 그래픽 세션 준비 target](https://github.com/systemd/systemd/blob/v249/units/user/graphical-session-pre.target)
