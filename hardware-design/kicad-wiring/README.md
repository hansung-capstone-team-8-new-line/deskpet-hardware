# DeskPet KiCad 배선 회로도 (rev C — 2026-10-26 재구매 계획 기준)

KiCad 7로 만든 회로도와 검증 기록이다. 실물 배선은 `wiring-table.png`의 순서를 따른다.

**rev C (2026-10-26):** 재구매 계획에 맞춰 부품명을 바꿨다. 화면은 Waveshare 7inch HDMI LCD (C)(HDMI 입력, micro USB 하나로 터치·전원), 스피커는 Adafruit 3351 3W 4Ω, 서보 전원은 코드 일체형 5V 4A 어댑터 → DC 클립 터미널 TC5521F, 분배보드는 리본이 함께 오는 SMP0080, 서보 쪽 1000µF(C1)는 PCA9685 모듈에 실장된 것을 쓴다(별도 구매 없음, 도착 후 확인). 어댑터와 클립 터미널 사이에 서보 전원 수동 차단용 스위치 연장선(MAXTEK MT202)을 넣는다 — 작업표 S1 행에만 있고 **회로도(gen.py·verify.py 정답표) 반영은 다음 회로도 작업에서 한다**. **넷 정답표는 rev B와 같다**(J1 = Jetson 40핀 = 분배보드 물리 번호, C1 = 보드 내장 콘덴서). `tools/gen.py`·`tools/wiring_table.py`와 작업표(CSV·PNG)는 갱신·대조했고 `verify.py`는 기존 넷리스트로 PASS다. `deskpet.kicad_sch`·PDF·PNG·넷리스트·BOM·ERC는 KiCad 7 환경(WSL)에서 `gen.py`부터 다시 만들어야 하며, 그 전까지 이 산출물들의 부품 이름은 rev B 표기다.

**rev B (2026-10-25):** 발주품으로 부품명과 화면 연결을 바꿨다. 화면은 10.1인치 IPS 터치 LCD(mini HDMI 입력, USB-C 하나로 터치·전원), 서보 전원 입력은 DC 연장선 암단자(잘라서 사용), 분배보드는 T-Cobbler Plus, 스피커는 ReSpeaker Lite 키트 동봉품, PCA9685는 동급 모듈이다. 서보 전원(5V → 3A 퓨즈 → V+)·I2C·OE=GND 연결 구조는 rev A와 같아 넷 정답표는 그대로다. KiCad 7.0.11로 다시 생성해 ERC·넷리스트 대조·틀린 회로 6종·작업표 대조를 다시 실행했다. 현재 C++ FULL_OFF 제어와 전원 순서는 [배선 위키](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-wiring-schematic.md)를 따른다. 터치 입력 처리 코드는 아직 없다.

[회로도 PDF](deskpet-schematic.pdf) · [회로도 PNG](deskpet-schematic.png) · [배선 작업표](wiring-table.png) · [CSV](wiring-table.csv) · [검증 기록](verification.txt) · [조립설명서](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-assembly-manual.md)

| 파일 | 내용 |
| --- | --- |
| `deskpet.kicad_sch` · `deskpet.kicad_pro` · `deskpet.kicad_sym` · `sym-lib-table` | KiCad 프로젝트. KiCad 7 이상에서 `deskpet.kicad_pro`를 연다 |
| `deskpet-schematic.pdf` / `.png` | 회로도 출력본 (A3) |
| `wiring-table.png` / `.csv` | 선 하나씩 연결하는 배선 작업표 (회로도 넷과 자동 대조) |
| `ERC.rpt` · `erc-result-screenshot.png` | KiCad 공식 전기 규칙 검사 결과: 오류 0, 경고 0 |
| `deskpet.net` · `deskpet-bom.xml` | KiCad가 내보낸 넷리스트와 부품 목록 |
| `verification.txt` | ERC, 넷리스트 대조, 일부러 틀린 회로 6종 검출 시험 결과 |
| `tools/` | 회로도 생성(`gen.py`), 검증(`verify.py`), 작업표(`wiring_table.py`), ERC 실행(`run_erc.sh`) |

## 검증한 것

1. **KiCad 공식 ERC** (eeschema 7.0.11, 2026-10-25 rev B 재실행): 오류 0, 경고 0. 제외된 검사는 SPICE 모델 1종(시뮬레이션 전용)뿐이다.
2. **넷리스트 대조**: KiCad가 내보낸 넷리스트를 손으로 작성한 별도 기준표(16개 넷)와 1:1 비교하고, 연결 금지 핀 6개(Jetson 5V 2·4번, PCA9685 헤더 V+ 등)가 비어 있는지 확인한다.
3. **안전 규칙**: 콘덴서 극성, 퓨즈 직렬, 공통 접지, 3.3V↔서보 5V 분리, SDA/SCL 방향, 서보 전원 핀.
4. **검사기가 진짜 잡는지 시험**: SDA/SCL 바꿈, Jetson 5V 연결, 콘덴서 역방향, 퓨즈 우회, 헤더 V+ 사용, 공통 접지 누락 — 6종 모두 FAIL로 잡혔다.

## 이 검증이 보장하지 않는 것

- 회로도의 논리 연결이 맞다는 것까지다. **부품 실물의 실크 표시(핀 이름·극성)가 이 회로도와 같은지**는 받은 뒤 눈으로 확인해야 한다. 특히 PCA9685(VLT-MD032)의 헤더 순서와 내장 콘덴서 극성·용량, DC 클립 터미널의 +/− 표시, 서보 커넥터 방향, 리본 1번 방향.
- 전류·온도·7인치 LCD (C) 1024×600 화면 인식 같은 실물 동작은 시험해야 안다. 계산상 SG90 2개 스톨 합계 약 1.5 A < 퓨즈 3 A < 어댑터 4 A.

## 다시 만들기

```text
python3 tools/gen.py out                      # 회로도 생성
kicad-cli sch export netlist --format kicadsexpr -o out/deskpet.net out/deskpet.kicad_sch
python3 tools/verify.py out/deskpet.net       # PASS여야 함
MUTATION=swap_i2c python3 tools/gen.py mut    # 틀린 회로 → verify가 FAIL이어야 함
```

KiCad 7의 명령줄 도구에는 ERC가 없어 `tools/run_erc.sh`가 가상 화면에서 eeschema의 ERC 버튼을 눌러 `ERC.rpt`를 저장한다. KiCad 8 이상이면 `kicad-cli sch erc`를 쓴다.

처음 실행하는 KiCad는 전역 심볼 라이브러리 설정 창을 띄워 `run_erc.sh`가 멈춘다. `~/.config/kicad/7.0/`에 `/usr/share/kicad/template/`의 `sym-lib-table`·`fp-lib-table`을 먼저 복사해 둔다. `.sh`·`.py`는 저장소의 `.gitattributes`가 LF로 고정하므로, Windows에서 받은 저장소도 WSL에서 그대로 실행된다.
