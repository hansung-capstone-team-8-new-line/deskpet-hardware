# 조립 설명서 그림 생성기

`make_figures.py`가 위키 [조립 설명서](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-assembly-manual.md)의 그림 15장(`00-overview` ~ `14-step12-remove`)과 [케이스 설계](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-enclosure-design.md)의 3면도(`case-layout`)를 SVG로 그린 뒤 PNG로 바꿉니다. 2026-10-16 발주품과 케이스 계획안 rev B 기준입니다.

```sh
python3 make_figures.py <출력 폴더>      # rsvg-convert, Noto Sans CJK KR 필요
```

## 치수 표

모든 치수는 파일 맨 위 `LCD`·`CASE`·`JET` 표 한 곳에서 가져옵니다. LCD가 도착해 실측하면 이 표만 고치고 다시 실행합니다.

| 값 | 현재 | 근거 |
| --- | --- | --- |
| LCD 외형 | 237 × 145 × 14 mm | 판매 페이지 10인치 금속 케이스 도면 (실측 전) |
| 케이스 외형 | 282 × 160 × 150 mm | 벽 4 + LCD 포켓 238 + 케이블 칸 36 + 벽 4 / 바닥 테두리 8 + 포켓 146 + 위 테두리 6 / 깊이는 Jetson·트레이·케이블 공간 |
| 앞틀 창 | 226 × 130 mm | 1024×600 10.1인치 표시 영역 계산값 약 222 × 125 + 여유 (실측 후 확정) |
| 케이블 칸 | 36 mm | LCD 옆면 mini HDMI·USB-C 플러그와 꺾임 공간 |

LCD 금속 케이스의 VESA 75×75 M4 구멍은 이번 구성에서 쓰지 않습니다(M4 나사 미구매). 포켓과 P9 누름쇠(M3 나일론)로 고정합니다.
