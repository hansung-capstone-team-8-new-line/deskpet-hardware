# Jetson 설치와 첫 구동

## 기준 장치

- Jetson Orin Nano 8GB 정품 개발자 키트, P3768 캐리어
- JetPack 6.x / Ubuntu 22.04 ARM64
- Waveshare 7inch HDMI LCD (C) 1024×600 IPS, HDMI 입력·micro USB 터치/전원 (2026-10-26 재구매 계획, rev C)
- Seeed ReSpeaker Lite(USB 마이크) + Adafruit 3351 3W 4Ω 밀폐형 스피커(JST-PH 2핀)
- PCA9685(VLT-MD032, 1000µF 내장), SG90 위치제어 서보 2개, 독립 5V 4A 서보 전원

7.x/Ubuntu 24.04나 다른 캐리어 보드에서의 호환성은 검증하지 않았습니다. 설치기는 OS·펌웨어·핀mux를 자동 변경하지 않습니다.

## 연결

전원을 끄고 다음 기준으로 연결합니다. 아래는 **최신 HDMI/USB/PCA9685 구성의 코드용 배선 제안**입니다. 기존 SPI/I2S Fritzing 배선표와 혼용하지 않습니다.

| 연결 | 설정·동작 |
| --- | --- |
| Jetson DisplayPort → DP-HDMI 변환기(NEXI NXC067) → LCD 동봉 HDMI 케이블 → LCD HDMI | LCD가 1024×600 가로 모드로 잡히는지 먼저 확인 |
| Jetson USB → LCD micro USB (동봉 USB-A→micro B) | 터치와 전원(5V 약 0.5A)을 함께 공급. Ubuntu에서 단일·다중 터치 여부는 실물 확인. SDL 터치 이벤트는 `touch.subscribe`로 수신 |
| Jetson USB-A → USB-C 데이터 케이블 → ReSpeaker Lite | 내장 마이크로 입력, 이 모듈의 스피커 단자로 출력 |
| ReSpeaker 스피커 단자(JST-PH 2핀) → Adafruit 3351 밀폐형 스피커 | 실물의 좌우 채널 및 +/− 표기를 확인. 증폭 출력의 어느 쪽도 GND에 연결하지 않음 |
| Jetson 헤더 물리 1번 3.3V → 40p 리본 → GPIO 분배보드(SMP0080) → PCA9685 VCC | I2C 풀업도 3.3V로 유지. 서보 전원 V+와 구분 |
| Jetson 물리 3번 SDA·5번 SCL → PCA9685 SDA·SCL | 실제 Linux I2C 어댑터는 doctor로 식별 |
| PCA9685 채널 0·1 → 좌우 SG90 신호 | 채널 및 좌우 반전은 설정으로 변경 가능 |
| 독립 5V 4A 어댑터(코드 일체형) → 스위치 연장선 MT202 → DC 클립 터미널 TC5521F → MINI 3A 퓨즈 → PCA9685 서보 V+ | 보드의 5V 출력으로 서보 전원을 공급하지 않음 |
| Jetson·PCA9685·서보 전원 GND | 공통 접지. 독립 전원의 +극과 Jetson의 +극은 연결하지 않음 |
| PCA9685 OE | GND 고정. GPIO·버퍼 없이 아래 FULL_OFF 방식 사용 |

전해 콘덴서는 서보 전원 쪽에 배치하고 극성을 확인합니다. USB 오디오와 화면의 스피커 단자는 서로 다른 출력입니다. 본 코드는 **USB 오디오**를 사용하며 HDMI 오디오로 자동 전환하지 않습니다.

### OE=GND와 PWM 정지

현재 KiCad 수정안 A는 OE를 GND에 고정한다. GPIO 7/16번, HC125, OE 풀업을 추가하지 않는다. I2C 초기화 첫 쓰기와 정상 종료·취소·연결 만료·검출한 오류에서는 `ALL_LED_OFF_H=0x10`으로 모든 PWM을 끈다. 중립값을 미리 쓸 때 각 채널 FULL_OFF를 유지하고 명시적 enable에서 CH0/CH1만 해제한다. 정지 쓰기가 실패하면 `SERVO_STOP_UNCONFIRMED`다.

OE가 상시 활성인 회로이므로 프로세스 강제 종료·커널 정지·I2C 단절에서 PWM 차단을 보장할 수 없다. 서보 전원 경로의 스위치 연장선(MT202)으로 서보 전원을 수동 차단하고, 전원 순서·부팅·재시작을 실측한다. `motion.commanded`는 위치 센서 확인이 아니다. 현재 회로는 [위키 회로도](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-wiring-schematic.md)를 따른다.

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
4. `display`: width=1024, height=600, rotation=0, fontSize=36. OS 회전과 중복 적용하지 않는다. 표정 도트는 `display.face`(기본 dotSize 8, 수염, 말할 때만 입, 번짐)로 조정한다([도트 표정](face-catalog.md)).
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

GNOME 없이 부팅하자마자 DeskPet 화면만 띄워 메모리를 아끼려면 [키오스크 모드](kiosk.md)(`bash tools/install-kiosk.sh`)를 사용합니다. 이 경우 자동 로그인은 필요 없습니다.

## 코드를 받은 뒤 다시 빌드

설치 때 만든 `build-jetson` 폴더를 그대로 쓰면 **바뀐 파일만** 다시 컴파일하고 링크합니다. 처음부터 전체를 빌드할 필요는 없습니다. 프로젝트 폴더에서:

```sh
git fetch
git switch <브랜치>        # 또는 main에서 git pull
cmake --build build-jetson -j 2
sudo cmake --install build-jetson
```

그다음 쓰는 방식에 맞게 재시작합니다.

```sh
systemctl --user restart deskpet-hardware        # 로그인 후 자동 시작을 쓸 때
sudo systemctl restart deskpet-kiosk@<계정>       # 키오스크 모드일 때
```

- `build-jetson` 폴더를 지우지 않습니다. 지우면 처음부터 전체 빌드가 됩니다.
- `CMakeLists.txt`가 바뀌었으면 `cmake --build`가 스스로 다시 구성합니다. 별도 명령은 필요 없습니다.
- `include/deskpet/` 아래 헤더가 바뀌면 그 헤더를 쓰는 소스가 모두 다시 컴파일되어 오래 걸립니다. `src/` 파일 하나만 바뀌면 그 파일과 링크만 합니다.
- `bash tools/install-jetson.sh real`을 다시 실행해도 같은 `build-jetson` 폴더를 쓰므로 빌드는 바뀐 파일만 합니다. 다만 apt 패키지 확인과 설치 단계를 다시 거쳐서 느립니다. 의존성이 바뀌었을 때만 씁니다. 기존 `config.json`은 덮어쓰지 않습니다.
- 빌드 중 메모리가 부족해 컴파일러가 죽으면 `-j 1`로 다시 실행합니다.
- **파이프라인 실행기로 띄울 때는 다른 실행 파일을 씁니다.** `deskpet-pipeline`의 젯슨 실행기는 같은 소켓에 이미 떠 있는 데몬(서비스·키오스크의 `/usr/local/bin/deskpet-hardware`)이 있으면 그걸 그대로 재사용합니다. 없을 때만 형제 폴더 `deskpet-hardware`(또는 `DESKPET_HARDWARE_DIR`)의 `build/deskpet-hardware`를 빌드해 띄우고, 이때 설정은 저장소의 `config/jetson.json`입니다. 실행기는 `git pull`을 하지 않습니다.
  - 그래서 코드를 받은 뒤 화면이 그대로라면 ① 그 폴더에서 `git log -1 --oneline`이 최신인지, ② 떠 있는 서비스·키오스크 데몬을 재시작했는지(재사용 경로), ③ 실행기 로그가 '기존 하드웨어 데몬에 연결합니다'인지 '하드웨어 데몬 빌드'인지 확인합니다.
  - 서비스·키오스크를 쓰면 위의 `cmake --install`까지 해야 `/usr/local/bin`이 바뀝니다. 실행기가 직접 띄우는 경우는 `build/`(이 문서의 `build-jetson/`과 다른 폴더)가 바뀝니다.
- 서비스를 재시작한 뒤 `journalctl --user -u deskpet-hardware -n 50`(키오스크는 `journalctl -u deskpet-kiosk@<계정> -n 50`)으로 시작 오류가 없는지 봅니다.

### 2026-11-22 표정 조정 (`feat/face-subtle-emotions`)

이 업데이트를 받을 때 해야 할 일입니다.

1. **바뀐 것:** [src/display.cpp](../src/display.cpp)와 문서만 바뀌었습니다. 다시 컴파일되는 소스는 `display.cpp` 하나입니다. 설정 파일은 고치지 않아도 됩니다.
2. **빌드:** 위 명령 그대로 하되 `<브랜치>`에 `feat/face-subtle-emotions`를 넣습니다(main에 머지된 뒤라면 main에서 `git pull`).
3. **처음 하는 전체 컴파일:** 개발 PC에는 SDL이 없어서 표정 로직만 따로 컴파일하고 돌려 봤습니다. SDL 화면 코드까지 함께 컴파일되는 건 이번 젯슨 빌드가 처음입니다. 오류가 나면 메시지 전체를 남겨 주세요.
4. **설정 의미 변경:** `display.face.timeOfDay`는 이제 대기 중 혼자 둔 시간의 심심함·졸기만 켜고 끕니다. 저녁·밤에 눈꺼풀이 내려오고 화면이 어두워지던 동작과 아침 기지개는 없어졌습니다.
5. **실물 확인:** 서비스를 멈추고 수동 실행한 상태에서([첫 장치 시험](#첫-장치-시험)) 다른 터미널로 하나씩 띄워 봅니다. 표정별 의도는 [도트 표정](face-catalog.md)에 있습니다.

   ```sh
   deskpetctl face --activity idle                                       # 평소 눈: 윗눈꺼풀이 살짝 내려온 느긋한 눈
   deskpetctl face --emotion happy                                       # 눈꺼풀을 다 올린 큰 타원 눈, 자주 ^ ^
   deskpetctl face --emotion sad                                         # 바깥 눈꼬리로 처진 분홍 눈, 느릿한 움직임
   deskpetctl face --activity processing                                 # 위쪽 옆을 볼 때 입·수염이 같이 따라오는지
   deskpetctl face --activity idle --emotion happy --status succeeded    # 뿌듯함: 끄덕임, 따뜻한 도트 물결 한 번
   deskpetctl face --activity idle --emotion sad --status failed         # 머쓱함: 슬픔보다 덜 처진 눈, 갸웃
   deskpetctl face --activity idle --status unknown                      # 노랑, 두 눈 크기가 살짝 다름
   deskpetctl face --emotion surprised                                   # 들어올 때 한 번 튀고 가라앉음
   ```

   - [ ] 실물 LCD에서 평소 눈꺼풀(0.13)이 졸려 보이기보다 느긋해 보이는지
   - [ ] 기쁨과 평소 눈의 차이가 멀리서도 보이는지
   - [ ] 슬픔이 과하거나 어색하지 않은지, 깜박일 때 눈이 찌그러지지 않는지
   - [ ] 생각 중에 눈과 입 사이 간격이 자연스러운지
   - [ ] 슬픔 상태에서 화면을 두 번 톡 하면 ^ ^ 대신 눈꼬리가 풀리며 작게 웃는지
6. **되돌리기:** 문제가 있으면 `git switch main` 뒤 같은 빌드·설치·재시작 명령을 실행합니다. 이때도 `display.cpp`만 다시 컴파일합니다.

### 2026-11-24 귀 자동 반응 (`feat/ear-reactions`)

1. **바뀐 것:** 표정에 맞춰 귀가 스스로 움직입니다([도트 표정의 귀 반응](face-catalog.md#귀-반응)). `include/deskpet/display.hpp`·`types.hpp`도 바뀌어서 이번에는 대부분의 소스가 다시 컴파일됩니다(위 빌드 명령 그대로, 시간이 더 걸림).
2. **설정:** 새 키 `servo.autoEars`(기본 `true`)입니다. 기존 `config.json`에 없어도 켜집니다. 끄려면 `"autoEars": false`를 넣습니다.
3. **켜지는 조건:** 귀 보정(`calibrated: true`)이 끝났고, 파이프라인이나 `deskpetctl`이 데몬에 연결돼 있고, 화면이 표정을 그리고 있을 때만 움직입니다. 보정 전에는 PWM을 켜지 않습니다. 파이프라인 실행기가 데몬을 직접 띄우면 저장소의 `config/jetson.json`을 쓰므로, 그 파일의 `servo`(I2C 장치·보정 값·`calibrated`)도 채워져 있어야 합니다.
4. **실물 확인:** 서비스를 멈추고 수동 실행한 상태에서 다른 터미널로 상태를 바꿔 가며 봅니다. `deskpetctl face`가 연결돼 있는 동안 귀가 움직입니다.

   ```sh
   deskpetctl health                                                     # motion.autoEars.active가 true인지
   deskpetctl face --activity listening --seconds 10                     # 두 귀 세우고 거의 멈춤
   deskpetctl face --emotion sad --seconds 10                            # 바깥으로 눕히고 드물게 작게 까딱
   deskpetctl face --activity processing --seconds 10                    # 보는 쪽 귀만 서고 자주 까딱
   deskpetctl face --activity idle --status unknown --seconds 10         # 좌우가 엇갈림
   ```

   - [ ] 귀가 보정 범위 끝에서 떨리거나 걸리지 않는지
   - [ ] 까딱 빈도가 과하지 않은지, 소음이 마이크 인식(VAD)을 건드리지 않는지
   - [ ] 화면 위 양쪽 모서리를 톡 하면 그쪽 귀가 털듯이 까딱하는지
   - [ ] 연결을 끊으면(`deskpetctl` 종료) 귀 출력이 꺼지는지
5. **되돌리기:** `"autoEars": false`로 끄거나, `git switch main` 뒤 다시 빌드합니다.

### 2026-11-24 화면 효과·효과음 (`feat/sound-and-effects`)

1. **바뀐 것:** 하트·반짝이·땀방울·물음표·음표·놀람선·생각 점과 짧은 효과음이 추가됐습니다([화면 효과](face-catalog.md#화면-효과), [효과음](face-catalog.md#효과음)). 새 소스 `src/effects.cpp`가 생겨서 `cmake --build`가 스스로 다시 구성합니다. 헤더도 바뀌어 대부분 다시 컴파일됩니다.
2. **설정:** `audio.effects`(기본 `true`), `audio.effectsVolume`(기본 0.5). 기존 `config.json`·`config/jetson.json`에 없어도 켜집니다. 끄려면 `"effects": false`.
3. **실물 확인:** 수동 실행 상태에서 대기 화면을 톡·두 번 톡·연타·문지르기 해 봅니다.
   - [ ] 소리가 너무 크거나 날카롭지 않은지(필요하면 `effectsVolume`을 낮춤)
   - [ ] 효과음 뒤 바로 말해도 인식이 잘 되는지, 효과음이 '듣는 중'을 잘못 켜지 않는지
   - [ ] 파이프라인이 말하기 시작하면 효과음이 바로 끊기는지

## 근거

- [ReSpeaker Lite](https://wiki.seeedstudio.com/reSpeaker_lite/)
- [PCA9685 데이터시트](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf)
- [Linux I2C 사용자 공간 인터페이스](https://www.kernel.org/doc/html/latest/i2c/dev-interface.html)
- [systemd 그래픽 세션 준비 target](https://github.com/systemd/systemd/blob/v249/units/user/graphical-session-pre.target)
