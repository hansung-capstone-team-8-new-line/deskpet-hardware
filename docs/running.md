# 모의 실행과 문제 해결

빌드와 첫 실행 명령은 [README](../README.md#지금-실행하기)에 있습니다. 이 문서는 모의 모드가 실물과 어떻게 다른지, 표정을 하나씩 시험하는 방법, 처음 실행할 때 자주 생기는 문제를 정리합니다. Jetson으로 옮기는 절차는 [Jetson 설치](jetson-setup.md)를 따릅니다.

## 모의 모드와 실물의 차이

| 항목 | 하드웨어가 없는 지금 | 실물이 연결된 뒤 |
| --- | --- | --- |
| 화면 | SDL 창에 실제 렌더링 코드로 표정·한글 미리보기 | HDMI 화면에 같은 내용을 표시 |
| 마이크 | 무음 또는 지정한 WAV를 20ms씩 전달 | ALSA로 USB 마이크 PCM 수집 |
| 스피커 | 데이터 길이에 맞춰 가상 재생·완료 처리. 실제 소리는 내지 않음 | USB 출력으로 PCM 재생·drain 확인 |
| 귀 | 계산된 좌우 펄스와 활성 상태를 모의로 변경 | I2C로 PCA9685 PWM/FULL_OFF 제어, OE=GND |
| 오류 | `mock.fail`로 장치별 오류를 주입 | 장치 열기·입출력·통신 오류를 보고 |

`demo`는 대기 → 듣기 → 처리 → 응답, 동시 녹음·재생, 귀 흔들기, 질문 무응답 15초 종료, 예약한 점검 알림, 텍스트 15초 후 기본 화면 복귀를 보여줍니다. `demo --fail-speaker`는 음성 실패 후 한글 안내로 전환하는 경로를 추가합니다.

이 데모는 테스트음을 사용하며 STT·LLM·TTS를 호출하지 않습니다. 질문 대기는 인식 결과 없이 무응답 경로를 보여줍니다. 예약 알림도 데모 내부 타이머이며 Eureka의 실제 일정 조회·저장 기능은 아닙니다. 같은 소켓을 제어하는 클라이언트는 한 번에 하나만 허용하므로 Python 데모와 Swift 데모를 동시에 실행하지 않습니다.

## 표정 하나씩 시험하기

모의 서버(`./build/deskpet-hardware --config config/mock.json`)를 켠 뒤 다른 터미널의 같은 폴더에서 하나씩 실행합니다. 명령 도구는 표시를 잠시 유지한 뒤 기본 표정으로 되돌립니다.

```sh
python3 tools/deskpetctl.py face --activity listening --seconds 2
python3 tools/deskpetctl.py face --activity processing --seconds 2
python3 tools/deskpetctl.py face --activity speaking --status succeeded --seconds 2
python3 tools/deskpetctl.py face --activity speaking --status failed --seconds 2
python3 tools/deskpetctl.py face --status unknown --seconds 2
python3 tools/deskpetctl.py face --activity awaiting_answer --seconds 2
python3 tools/deskpetctl.py face --emotion happy --seconds 2
python3 tools/deskpetctl.py face --emotion sleepy --seconds 2
python3 tools/deskpetctl.py face --text '음성 대신 이 문구를 보여줘요.' --seconds 2
```

`face --activity speaking`은 입만 움직이는 화면 시험입니다. 실제 음성 재생은 `play`나 Swift 스피커 어댑터로 별도 실행합니다. 감정은 `--emotion`(neutral·happy·sad와 확장 값 petted·sleepy·surprised)으로 시험합니다. 표정 목록과 도트 설정은 [도트 표정](face-catalog.md)을 참고하세요.

## 처음 실행할 때 흔한 문제

| 증상 | 확인할 내용 |
| --- | --- |
| 클라이언트가 연결되지 않음 | 서버 실행 여부·소켓 경로·같은 사용자 여부·이미 연결된 제어 클라이언트 확인 |
| 모의 모드에서 소리가 안 남 | 정상 동작. 실제 소리 출력은 Linux 실제 장치 모드에서 시험 |
| `MOTION_DISABLED` 또는 보정 관련 오류 | 실물 보정 결과 적용과 같은 세션에서의 `motion.enable` 확인 |
| 명령 도구 종료 후 귀가 비활성화됨 | 연결 종료 시 정리 동작. 지속 제어는 하나의 클라이언트 연결을 유지 |
| `AUDIO_DEVICE_ERROR` | 실제 USB 카드 ID·접근 권한·다른 프로그램의 장치 점유 확인 |
| `AUDIO_DISCONTINUITY` | 프레임 유실 원인 확인 후 새 캡처 시작. 끊긴 발화를 정상 입력으로 계속 사용하지 않음 |
| `DISPLAY_TEXT_TOO_LONG` | 전달 문구를 줄이거나 글꼴 설정 확인 |
| 화면이 반대로 회전하거나 잘림 | Ubuntu 화면 설정과 앱의 `display.rotation`을 함께 확인 |
