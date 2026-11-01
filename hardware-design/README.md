# 하드웨어 설계 자료

회로도·배선 원본, 조립 설명서 그림 생성기, 그리고 이를 재생성·검증하는 도구입니다. **2026-10-26 재구매 계획 기준(rev C)** 입니다. 7인치 LCD (C), GPIO 분배보드 SMP0080(리본 세트), 코드 일체형 5V 4A 어댑터, DC 클립 터미널, PCA9685 내장 콘덴서를 반영했습니다. KiCad 원본은 2026-10-25 팀 위키 `knowledge`의 `projects/deskpet/outputs/kicad-wiring`에서 옮겼고, 위키에는 문서에 넣는 그림만 남아 있습니다. 과거 Fritzing 배선도와 Python 참조 구현은 옮기지 않았으며 위키 저장소의 Git 기록에서 볼 수 있습니다.

진행 상황과 결정은 위키의 [하드웨어 진행상황](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-hardware.md), 조립 절차는 [조립 설명서](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-assembly-manual.md)에서 확인합니다.

| 폴더 | 내용 | 상태 |
| --- | --- | --- |
| [kicad-wiring/](kicad-wiring/README.md) | KiCad 회로도·넷리스트·BOM·배선 작업표, 생성·ERC·넷리스트 검증 도구 | rev C 전체 재생성(2026-10-27): 서보 전원 스위치 SW1 반영, ERC 오류 0·경고 0, 넷 17개 대조 통과, 틀린 회로 7종 검출, 작업표 19행 일치 |
| [assembly-figures/](assembly-figures/README.md) | 조립 설명서 그림 15장과 케이스 배치 3면도 생성기. 케이스 계획 치수 표(`CASE`)를 한 곳에서 관리 | rev C 계획안 204 × 122 × 150 mm 기준 — 케이스 rev D(192 × 134 × 150)로 아직 갱신 안 함 |
| [case-3d/](case-3d/README.md) | 케이스 3D 모델(build123d): 출력물 P1~P7 STL·3MF(Bambu X1C·H2D)·조립 STEP, 실제 부품·배선 더미와 겹침·냉각·귀 회전·멈춤턱·보드 반납 통로·출력 범위 자동 검사, [출력 안내](case-3d/PRINTING.md) | **rev D** 192 × 134 × 150 mm, 앞면 전체 LCD·문 일체형 트레이로 모든 검사 통과. `[추정]` 치수는 부품 도착 후 실측해 고침 |

## 다시 만들기

KiCad 7.0.11·Noto Sans CJK·matplotlib·librsvg가 있는 Linux(WSL Ubuntu 22.04 확인)에서 실행합니다. Windows 콘솔에서 Python을 쓸 때는 `python -X utf8`을 사용합니다.

```sh
python3 hardware-design/kicad-wiring/tools/verify.py hardware-design/kicad-wiring/deskpet.net
python3 hardware-design/assembly-figures/make_figures.py out/figures
```

회로도 전체 재생성·ERC 절차는 [kicad-wiring/README.md](kicad-wiring/README.md)를 따릅니다. 만든 PNG·PDF는 위키 `projects/deskpet/outputs/assembly-manual/`과 `projects/deskpet/outputs/kicad-wiring/`에도 같은 이름으로 교체합니다.

구매 목록을 검사하는 `verify_current_purchase.py`는 구매 파일과 함께 위키 저장소에 있습니다.
