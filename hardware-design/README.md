# 하드웨어 설계 자료

회로도·배선 원본과 이를 재생성·검증하는 도구입니다. 2026-10-25 팀 위키 `knowledge`의 `projects/deskpet/outputs/kicad-wiring`에서 옮겼습니다. 위키에는 문서에 넣는 그림(회로도 PDF·PNG, 배선 작업표 PNG, ERC 화면)만 남아 있습니다. 과거 Fritzing 배선도와 Python 참조 구현은 옮기지 않았으며 위키 저장소의 Git 기록에서 볼 수 있습니다.

배선이 무엇을 결정했고 지금 어디까지 왔는지는 위키의 [배선 회로도 요약](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-wiring-schematic.md)과 [하드웨어 진행상황](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-hardware.md)에서 확인합니다.

| 폴더 | 내용 |
| --- | --- |
| [kicad-wiring/](kicad-wiring/README.md) | KiCad 회로도·넷리스트·BOM·배선 작업표, 생성·ERC·넷리스트 검증 도구 |

**2026-10-16 발주와 다른 점:** 회로도는 10-13 구성(RC070S 7인치, DC잭 터미널, SMP0080 분배보드, 별도 스피커)의 부품명으로 그려져 있습니다. 서보 전원·I2C 연결 구조는 발주품에서도 같지만, 화면(10.1인치 mini HDMI·USB-C 터치)·DC 연장선·T-Cobbler 분배보드·키트 스피커로 부품명과 화면 연결을 고친 뒤 KiCad에서 PDF·PNG·ERC를 다시 내보내야 합니다.

## 검증 명령

저장소 루트에서 실행합니다. Windows 콘솔 인코딩 문제가 있으면 `python -X utf8`을 사용합니다.

```sh
python3 hardware-design/kicad-wiring/tools/verify.py hardware-design/kicad-wiring/deskpet.net
```

구매 목록을 검사하는 `verify_current_purchase.py`는 구매 파일과 함께 위키 저장소에 있습니다.
