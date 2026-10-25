# 하드웨어 설계 자료

회로도·배선 원본, 조립 설명서 그림 생성기, 그리고 이를 재생성·검증하는 도구입니다. 모두 **2026-10-16 발주품 기준(rev B, 2026-10-25)** 입니다. KiCad 원본은 2026-10-25 팀 위키 `knowledge`의 `projects/deskpet/outputs/kicad-wiring`에서 옮겼고, 위키에는 문서에 넣는 그림만 남아 있습니다. 과거 Fritzing 배선도와 Python 참조 구현은 옮기지 않았으며 위키 저장소의 Git 기록에서 볼 수 있습니다.

진행 상황과 결정은 위키의 [하드웨어 진행상황](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-hardware.md), 조립 절차는 [조립 설명서](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-assembly-manual.md)에서 확인합니다.

| 폴더 | 내용 | 상태 |
| --- | --- | --- |
| [kicad-wiring/](kicad-wiring/README.md) | KiCad 회로도·넷리스트·BOM·배선 작업표, 생성·ERC·넷리스트 검증 도구 | rev B: 발주품 부품명, ERC 0·넷리스트 검사 통과 |
| [assembly-figures/](assembly-figures/README.md) | 조립 설명서 그림 15장과 케이스 배치 3면도 생성기. 케이스 계획 치수 표(`CASE`)를 한 곳에서 관리 | rev B 계획안 (LCD 실측 후 확정) |

## 다시 만들기

KiCad 7.0.11·Noto Sans CJK·matplotlib·librsvg가 있는 Linux(WSL Ubuntu 22.04 확인)에서 실행합니다. Windows 콘솔에서 Python을 쓸 때는 `python -X utf8`을 사용합니다.

```sh
python3 hardware-design/kicad-wiring/tools/verify.py hardware-design/kicad-wiring/deskpet.net
python3 hardware-design/assembly-figures/make_figures.py out/figures
```

회로도 전체 재생성·ERC 절차는 [kicad-wiring/README.md](kicad-wiring/README.md)를 따릅니다. 만든 PNG·PDF는 위키 `projects/deskpet/outputs/assembly-manual/`과 `projects/deskpet/outputs/kicad-wiring/`에도 같은 이름으로 교체합니다.

구매 목록을 검사하는 `verify_current_purchase.py`는 구매 파일과 함께 위키 저장소에 있습니다.
