# DeskPet KiCad 배선 회로도 (2026-10-13 구성, 발주품 반영 전)

KiCad 7로 만든 회로도와 검증 기록이다. 실물 배선은 `wiring-table.png`의 순서를 따른다.

**2026-10-16 발주품과 부품명·화면 연결이 다르다.** 화면은 10.1인치 터치 LCD(mini HDMI·USB-C), 서보 전원 입력은 DC잭 터미널 대신 DC 연장선 암단자, 분배보드는 T-Cobbler Plus, 스피커는 ReSpeaker 키트 동봉품이다. 서보 전원(5V → 3A 퓨즈 → V+)·I2C·OE=GND 연결 구조는 같다. 부품명을 고친 뒤 PDF·PNG·ERC를 다시 내보내야 한다.

2026-10-13 RC070S·ReSpeaker Lite·OE=GND 구성과 대조했다. 저장된 넷리스트 검사는 재실행했으며 ERC 보고서는 2026-10-09 기록을 유지한다. 현재 C++ FULL_OFF 제어와 전원 순서는 [배선 위키](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-wiring-schematic.md)를 따른다. 10.1인치 LCD 발주로 터치 입력을 쓸 수 있게 됐지만 처리 코드는 아직 없다.

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

1. **KiCad 공식 ERC** (eeschema 7.0.11): 오류 0, 경고 0. 제외된 검사는 SPICE 모델 1종(시뮬레이션 전용)뿐이다.
2. **넷리스트 대조**: KiCad가 내보낸 넷리스트를 손으로 작성한 별도 기준표(16개 넷)와 1:1 비교하고, 연결 금지 핀 6개(Jetson 5V 2·4번, PCA9685 헤더 V+ 등)가 비어 있는지 확인한다.
3. **안전 규칙**: 콘덴서 극성, 퓨즈 직렬, 공통 접지, 3.3V↔서보 5V 분리, SDA/SCL 방향, 서보 전원 핀.
4. **검사기가 진짜 잡는지 시험**: SDA/SCL 바꿈, Jetson 5V 연결, 콘덴서 역방향, 퓨즈 우회, 헤더 V+ 사용, 공통 접지 누락 — 6종 모두 FAIL로 잡혔다.

## 이 검증이 보장하지 않는 것

- 회로도의 논리 연결이 맞다는 것까지다. **부품 실물의 실크 표시(핀 이름·극성)가 이 회로도와 같은지**는 받은 뒤 눈으로 확인해야 한다. 특히 PCA9685 모듈(발주품은 VLT-MD032가 아닌 동급품)의 헤더 순서, DC 연장선을 자른 쪽의 +/− 선, 서보 커넥터 방향.
- 전류·온도·10.1인치 LCD 1024×600 화면 인식 같은 실물 동작은 시험해야 안다. 계산상 SG90 2개 스톨 합계 약 1.5 A < 퓨즈 3 A < 어댑터 5 A.

## 다시 만들기

```text
python3 tools/gen.py out                      # 회로도 생성
kicad-cli sch export netlist --format kicadsexpr -o out/deskpet.net out/deskpet.kicad_sch
python3 tools/verify.py out/deskpet.net       # PASS여야 함
MUTATION=swap_i2c python3 tools/gen.py mut    # 틀린 회로 → verify가 FAIL이어야 함
```

KiCad 7의 명령줄 도구에는 ERC가 없어 `tools/run_erc.sh`가 가상 화면에서 eeschema의 ERC 버튼을 눌러 `ERC.rpt`를 저장한다. KiCad 8 이상이면 `kicad-cli sch erc`를 쓴다.
