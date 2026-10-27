"""배선 작업표 생성. 각 행의 양 끝이 검증된 기준 넷(GOLDEN)에서 같은 넷인지 확인한 뒤 그린다."""
import sys, os, csv
sys.path.insert(0, os.path.dirname(__file__))
from verify import GOLDEN
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager as fm
for f in fm.findSystemFonts():
    if "NotoSansCJK" in f and ("Regular" in f or "Bold" in f):
        fm.fontManager.addfont(f)
plt.rcParams["font.family"] = [f for f in ("Noto Sans CJK KR", "Noto Sans CJK JP", "Apple SD Gothic Neo") if f in {x.name for x in fm.fontManager.ttflist}][:1] + ["DejaVu Sans"]

OUT = sys.argv[1]
# (번호, 출발, 도착, 선·부품, 색, 기준 넷 확인용 끝점 쌍들)
ROWS = [
 ("R", "Jetson 40핀 헤더", "GPIO 분배보드 SMP0080", "40p 무지개 리본(세트 동봉)", "1번 = 리본 첫 줄", []),
 ("W1", "분배보드 1번 (3.3V)", "PCA9685 왼쪽 헤더 VCC", "F/F 점퍼", "빨강", [(("J1", "1"), ("U4", "5"))]),
 ("W2", "분배보드 3번 (SDA)", "PCA9685 왼쪽 헤더 SDA", "F/F 점퍼", "파랑", [(("J1", "3"), ("U4", "4"))]),
 ("W3", "분배보드 5번 (SCL)", "PCA9685 왼쪽 헤더 SCL", "F/F 점퍼", "노랑", [(("J1", "5"), ("U4", "3"))]),
 ("W4", "분배보드 6번 (GND)", "PCA9685 왼쪽 헤더 GND", "F/F 점퍼", "검정", [(("J1", "6"), ("U4", "1"))]),
 ("W5", "PCA9685 오른쪽 헤더 OE", "PCA9685 오른쪽 헤더 GND", "짧은 F/F 점퍼 (JP1)", "초록", [(("U4", "8"), ("JP1", "1")), (("U4", "7"), ("JP1", "2"))]),
 ("W6", "5V 4A 어댑터 플러그 (코드 일체형)", "스위치 연장선 MT202 암 단자 (SW1)", "플러그 끼우기", "-", [(("PS2", "1"), ("SW1", "1"))]),
 ("S1", "스위치 연장선 MT202 수 플러그", "DC 클립 터미널 TC5521F", "MT202 M/F 29cm, 처음엔 OFF", "-", [(("SW1", "2"), ("J3", "1")), (("PS2", "2"), ("J3", "2"))]),
 ("W7", "클립 터미널 + (테스터로 확인)", "퓨즈 홀더 BE151 한쪽 선", "18AWG(XT30 선 절단) + 납땜·수축튜브 3/6mm", "빨강", [(("J3", "3"), ("F1", "1"))]),
 ("W8", "퓨즈 홀더 다른 쪽 선", "PCA9685 단자대 V+", "18AWG", "빨강", [(("F1", "2"), ("U4", "13"))]),
 ("W9", "클립 터미널 −", "PCA9685 단자대 GND", "18AWG", "검정", [(("J3", "4"), ("U4", "14"))]),
 ("W10", "PCA9685 내장 1000µF 확인", "단자대 V+ / GND 옆 (기판 실장)", "확인만: 극성·용량 (없으면 1000µF 16V 추가)", "-", [(("C1", "1"), ("U4", "13")), (("C1", "2"), ("U4", "14"))]),
 ("W11", "SG90 호환 왼쪽 귀 플러그", "PCA9685 CH0 (갈색=GND, 빨강=V+, 주황=PWM)", "서보 선 그대로", "갈·빨·주", [(("M1", "1"), ("U4", "15")), (("M1", "2"), ("U4", "16")), (("M1", "3"), ("U4", "17"))]),
 ("W12", "SG90 호환 오른쪽 귀 플러그", "PCA9685 CH1 (같은 방향)", "서보 선 그대로", "갈·빨·주", [(("M2", "1"), ("U4", "18")), (("M2", "2"), ("U4", "19")), (("M2", "3"), ("U4", "20"))]),
 ("C1", "Jetson DP", "DP→HDMI 젠더 → HDMI 케이블 → LCD HDMI", "NXC067 + LCD 동봉 HDMI", "-", [(("U1", "2"), ("A1", "1")), (("A1", "2"), ("U2", "1"))]),
 ("C2", "Jetson USB-A", "LCD micro USB (터치·전원)", "LCD 동봉 USB-A→micro B 케이블", "-", [(("U1", "3"), ("U2", "2"))]),
 ("C3", "Jetson USB-A", "ReSpeaker USB-C", "HT-CA3100W USB-A→C 데이터 케이블", "-", [(("U1", "4"), ("U3", "1"))]),
 ("C4", "ReSpeaker 스피커 단자", "Adafruit 3351 밀폐형 스피커", "스피커 JST-PH 케이블", "-", [(("U3", "2"), ("LS1", "1")), (("U3", "3"), ("LS1", "2"))]),
 ("C5", "Jetson 전원 어댑터", "Jetson DC 입력", "키트 동봉", "-", [(("PS1", "1"), ("U1", "1"))]),
]

def net_of(ep):
    for n, nodes in GOLDEN.items():
        if ep in nodes:
            return n
    return None

for r in ROWS:
    for a, b in r[5]:
        na, nb = net_of(a), net_of(b)
        assert na is not None and na == nb, f"{r[0]}: {a}->{na}, {b}->{nb}"
covered = {ep for r in ROWS for pair in r[5] for ep in pair}
# 넷의 모든 실물 끝점이 작업표에 한 번 이상 나오는지 확인
missing = [(n, ep) for n, nodes in GOLDEN.items() for ep in nodes if ep not in covered and ep[0] not in ("J3",) ]
assert not missing, missing
print("wiring table consistent with GOLDEN nets:", len(ROWS), "rows")

with open(f"{OUT}/wiring-table.csv", "w", newline="", encoding="utf-8-sig") as f:
    w = csv.writer(f); w.writerow(["번호", "출발", "도착", "선·부품", "색", "넷"])
    for r in ROWS:
        w.writerow([r[0], r[1], r[2], r[3], r[4], ", ".join(sorted({net_of(a) for a, b in r[5]})) or "리본 1:1"])

fig = plt.figure(figsize=(16, 10.5)); ax = fig.add_axes([0, 0, 1, 1]); ax.axis("off")
ax.text(0.02, 0.965, "DeskPet 배선 작업표 (rev C, 2026-10-26 재구매품) — 위에서부터 차례대로, 모든 전원을 끈 상태에서", fontsize=17, weight="bold", color="#243C5A")
ax.text(0.02, 0.935, "KiCad 회로도(ERC 0 오류)의 넷과 자동 대조한 표입니다. 각 행을 연결한 뒤 멀티미터로 도통을 확인하고 체크하세요.", fontsize=10.5, color="#555")
cols = ["체크", "번호", "출발", "도착", "선·부품", "색", "넷"]
xs = [0.02, 0.06, 0.10, 0.31, 0.58, 0.77, 0.835]
y = 0.895
for x, c in zip(xs, cols):
    ax.text(x, y, c, fontsize=11, weight="bold", color="white", va="center")
ax.add_patch(plt.Rectangle((0.015, y - 0.018), 0.97, 0.036, color="#243C5A", zorder=0))
for i, r in enumerate(ROWS):
    yy = y - 0.041 * (i + 1)
    if i % 2 == 0:
        ax.add_patch(plt.Rectangle((0.015, yy - 0.0195), 0.97, 0.039, color="#F0F4F8", zorder=0))
    ax.add_patch(plt.Rectangle((0.027, yy - 0.009), 0.014, 0.018, fill=False, ec="#2D6A4F", lw=1.3))
    nets = ", ".join(sorted({net_of(a) for a, b in r[5]})) or "리본 1:1"
    vals = [r[0], r[1], r[2], r[3], r[4], nets]
    for x, v in zip(xs[1:], vals):
        ax.text(x, yy, v, fontsize=9.0 if x != xs[-1] else 8.2, va="center", color="#B23A2A" if (r[0] in ("W8", "W9", "W10") and x == xs[1]) else "#1B2733")
ax.text(0.02, 0.06, "주의: Jetson 5V 핀(2·4번)과 PCA9685 헤더의 V+ 핀은 어디에도 연결하지 않습니다. 보드 내장 콘덴서의 띠(−)가 GND 쪽인지 확인.",
        fontsize=10.5, weight="bold", color="#B23A2A")
ax.text(0.02, 0.03, "W7~W9 연결과 W10 확인 후, 어댑터를 꽂기 전에 V+↔GND 단락 검사. 켜는 순서: Jetson → 프로그램(서보 출력 OFF) → 서보 스위치 ON. 이상하면 스위치 OFF.",
        fontsize=10.5, color="#1B2733")
fig.savefig(f"{OUT}/wiring-table.png", dpi=150, facecolor="white")
