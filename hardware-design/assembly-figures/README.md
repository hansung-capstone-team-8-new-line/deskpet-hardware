# 조립 설명서 그림 생성기

`make_figures.py`가 위키 [조립 설명서](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-assembly-manual.md)의 그림 15장(`00-overview` ~ `14-step12-remove`)과 [케이스 설계](https://github.com/hansung-capstone-team-8-new-line/knowledge/blob/master/wiki/deskpet-enclosure-design.md)의 3면도(`case-layout`)를 SVG로 그린 뒤 PNG로 바꿉니다. 2026-10-26 재구매 계획과 케이스 계획안 rev C 기준입니다.

```sh
python3 make_figures.py <출력 폴더>      # rsvg-convert, Noto Sans CJK KR 필요 (없으면 다른 한글 글꼴로 대체됨)
```

## 치수 표

모든 치수는 파일 맨 위 `LCD`·`CASE`·`JET` 표 한 곳에서 가져옵니다. LCD가 도착해 실측하면 이 표만 고치고 다시 실행합니다.

| 값 | 현재 | 근거 |
| --- | --- | --- |
| LCD 외형 | 164.9 × 106.96 × 8 mm | Waveshare 7inch HDMI LCD (C) 위키 사양 (실측 전) |
| 케이스 외형 | 204 × 122 × 150 mm | 벽 4 + LCD 포켓 166 + 케이블 칸 30 + 벽 4 / 바닥 테두리 8 + 포켓 108 + 위 테두리 6 / 깊이는 Jetson·트레이·케이블 공간. 220 mm급 프린터에서 분할 없이 출력 |
| 앞틀 창 | 156 × 88 mm | 표시 영역 154.21 × 85.92 + 여유 (실측 후 확정) |
| 케이블 칸 | 30 mm | LCD 옆면 HDMI·micro USB 플러그와 꺾임 공간 (단자 위치 실측 후 조정) |

LCD는 포켓과 P9 누름쇠(M3 나일론)로 기판 뒷면 가장자리를 눌러 고정합니다. 누름쇠 위치는 실물 기판 구멍을 재서 정합니다.
