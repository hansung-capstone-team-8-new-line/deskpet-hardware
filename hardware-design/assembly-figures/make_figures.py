"""DeskPet 조립 설명서·케이스 배치 그림 생성기 (2026-10-26 재구매 목록, 케이스 계획안 rev C).

SVG를 만든 뒤 rsvg-convert로 PNG를 만든다. 글꼴은 Noto Sans CJK KR.
사용: python3 make_figures.py <출력 폴더>
치수는 모두 CASE 표 한 곳에서 가져온다. 실물 측정 후 이 표만 고치고 다시 실행한다.
"""
import os, subprocess, sys
from xml.sax.saxutils import escape

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"
os.makedirs(OUT, exist_ok=True)

# ------------------------------------------------------------------ 치수 (mm, 계획값)
LCD = dict(w=164.9, h=106.96, t=8, aa_w=154.21, aa_h=85.92)  # Waveshare 7inch HDMI LCD (C) 위키 사양
CASE = dict(
    wall=4, cable_bay=30, top=6, bottom=8, depth=150,
    pocket_w=166, pocket_h=108, pocket_d=9, win_w=156, win_h=88,
)
CASE["W"] = CASE["wall"] * 2 + CASE["pocket_w"] + CASE["cable_bay"]       # 204
CASE["H"] = CASE["pocket_h"] + CASE["top"] + CASE["bottom"]               # 122
JET = dict(w=103, d=90.5, h=35)

C = dict(navy="#243C5A", ink="#1B2733", sub="#6B7785", accent="#E07A5F", beige="#F3EFE7", panel="#E8EDF3",
         lcd="#2E4A6A", lcdedge="#13202E", jet="#7C9CC5", pca="#B9A3E3", mic="#E8957A", servo="#F0CC8E",
         green="#2E8B57", red="#D62828", black="#2B2B2B", grey="#9AA3AE", line="#C9D1DA", warn="#B23A2A")
FONT = "Noto Sans CJK KR"

class Svg:
    def __init__(self, w=1560, h=1000):
        self.w, self.h, self.items = w, h, []
    def add(self, s):
        self.items.append(s)
    def rect(self, x, y, w, h, fill="none", stroke=C["ink"], sw=2, rx=0, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')
    def line(self, pts, stroke=C["ink"], sw=2, dash=None, arrow=False):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        m = ' marker-end="url(#arr)"' if arrow else ""
        p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        self.add(f'<polyline points="{p}" fill="none" stroke="{stroke}" stroke-width="{sw}" stroke-linejoin="round"{d}{m}/>')
    def poly(self, pts, fill, stroke=C["ink"], sw=2):
        p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        self.add(f'<polygon points="{p}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
    def circle(self, x, y, r, fill, stroke=C["ink"], sw=2):
        self.add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
    def text(self, x, y, s, size=15, fill=C["ink"], weight="normal", anchor="start", lh=1.35):
        for i, ln in enumerate(str(s).split("\n")):
            self.add(f'<text x="{x:.1f}" y="{y + i * size * lh:.1f}" font-family="{FONT}" font-size="{size}" '
                     f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{escape(ln)}</text>')
    def badge(self, n, x, y, r=17):
        self.circle(x, y, r, C["accent"], "#ffffff", 3)
        self.text(x, y + r * 0.38, n, r * 0.95, "#ffffff", "bold", "middle")
    def header(self, step, title, sub):
        x = 120 if step else 30
        if step:
            self.circle(58, 54, 39, C["navy"], C["navy"], 1)
            self.text(58, 68, step, 36, "#ffffff", "bold", "middle")
        self.text(x, 52, title, 30, C["navy"], "bold")
        self.text(x, 90, sub, 17, C["sub"])
    def partsbox(self, items, x=1115, y=90, w=415):
        h = 52 + 31 * len(items)
        self.rect(x, y, w, h, "#FFFDF6", "#D8C9A0", 2, 10)
        self.text(x + 18, y + 30, "이번 단계 부품", 17, "#7A5C12", "bold")
        for i, (q, name) in enumerate(items):
            self.text(x + 18, y + 62 + i * 31, q, 16, C["accent"], "bold")
            self.text(x + 72, y + 62 + i * 31, name, 15.5)
    def steps(self, lines, x, y, gap=34, size=16):
        """번호 목록. 캔버스 오른쪽 끝에 맞춰 자동 줄바꿈한다."""
        per = max(8, int((self.w - x - 60) / (size * 0.93)))
        for i, s in enumerate(lines):
            rows = [""]
            for word in s.split(" "):
                if rows[-1] and len(rows[-1]) + 1 + len(word) > per:
                    rows.append(word)
                else:
                    rows[-1] = (rows[-1] + " " + word).strip()
            self.badge(str(i + 1), x + 14, y - 5, 14)
            self.text(x + 38, y, "\n".join(rows), size)
            y += gap + (len(rows) - 1) * size * 1.35
    def footer(self, s, color=None):
        self.text(self.w / 2, self.h - 40, s, 17, color or C["warn"], "bold", "middle")
    def save(self, name):
        defs = ('<defs><marker id="arr" markerWidth="10" markerHeight="10" refX="8" refY="5" orient="auto">'
                f'<path d="M0,0 L10,5 L0,10 z" fill="{C["grey"]}"/></marker></defs>')
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}">'
               f'<rect width="100%" height="100%" fill="#ffffff"/>{defs}' + "".join(self.items) + "</svg>")
        path = os.path.join(OUT, name + ".svg")
        open(path, "w", encoding="utf-8").write(svg)
        subprocess.run(["rsvg-convert", "-o", os.path.join(OUT, name + ".png"), path], check=True)
        os.remove(path)

# ------------------------------------------------------------------ 부품 그림 조각
def lcd_front(s, x, y, w, h, face=True):
    s.rect(x, y, w, h, C["lcdedge"], C["lcdedge"], 2, 6)
    s.rect(x + w * 0.032, y + h * 0.055, w * 0.936, h * 0.86, C["lcd"], C["lcd"], 1)
    if face:
        r = h * 0.09
        s.circle(x + w * 0.3, y + h * 0.42, r, "#ffffff", "#ffffff", 1)
        s.circle(x + w * 0.7, y + h * 0.42, r, "#ffffff", "#ffffff", 1)
        s.line([(x + w * 0.44, y + h * 0.68), (x + w * 0.5, y + h * 0.72), (x + w * 0.56, y + h * 0.68)], "#ffffff", 4)

def jetson(s, x, y, w, h, label=True):
    s.rect(x, y, w, h, C["jet"], C["ink"], 2, 4)
    s.circle(x + w / 2, y + h / 2, min(w, h) * 0.28, "#5E7FA8")
    if label:
        s.text(x + w / 2, y + h + 22, "Jetson Orin Nano (대여)", 14, C["ink"], anchor="middle")

def pca(s, x, y, w=180, h=70):
    s.rect(x, y, w, h, C["pca"], C["ink"], 2, 4)
    for i in range(6):
        s.rect(x + 6, y + 10 + i * 9, 8, 7, "#E9A93B", C["ink"], 1)
        s.rect(x + w - 14, y + 10 + i * 9, 8, 7, "#E9A93B", C["ink"], 1)
    s.rect(x + w * 0.42, y - 2, 40, 18, C["green"], C["ink"], 1)
    s.text(x + w * 0.42 + 20, y + 11, "V+ 단자", 10, "#ffffff", anchor="middle")
    for i in range(12):
        s.rect(x + 26 + i * 11, y + h - 22, 8, 6, C["red"], "none", 0)
        s.rect(x + 26 + i * 11, y + h - 14, 8, 6, C["black"], "none", 0)
    s.text(x + w / 2, y + h / 2 + 2, "PCA9685", 15, C["ink"], anchor="middle")

def servo(s, x, y, label="SG90 호환"):
    s.rect(x, y, 70, 60, C["servo"], C["ink"], 2)
    s.rect(x - 12, y + 12, 94, 9, C["servo"], C["ink"], 1.5)
    s.circle(x + 18, y - 8, 9, "#ffffff")
    s.text(x + 35, y + 44, label, 11.5, C["ink"], anchor="middle")

def respeaker(s, x, y, w=190, h=80, speaker=True):
    s.rect(x, y, w, h, C["mic"], C["ink"], 2, 6)
    s.circle(x + 22, y + h / 2, 6, C["black"]); s.circle(x + w - 22, y + h / 2, 6, C["black"])
    s.text(x + w / 2, y + h / 2 - 4, "ReSpeaker Lite", 13, C["ink"], anchor="middle")
    s.rect(x + w / 2 - 15, y + h - 8, 30, 9, "#CFD5DC", C["ink"], 1)
    s.text(x + w / 2, y + h - 14, "USB-C", 10, C["ink"], anchor="middle")
    if speaker:
        s.rect(x + w + 30, y + 10, 70, 60, "#7FB39A", C["ink"], 2, 8)
        for i in range(3):
            for j in range(4):
                s.circle(x + w + 45 + j * 13, y + 25 + i * 15, 2.5, C["ink"], "none", 0)
        s.text(x + w + 65, y + 88, "3W 스피커", 12, C["ink"], anchor="middle")
        s.line([(x + w, y + 30), (x + w + 30, y + 30)], C["black"], 3)

def cobbler(s, x, y, label=True):
    s.rect(x, y, 46, 120, "#C0392B", C["ink"], 2, 3)
    for i in range(20):
        s.rect(x + 10, y + 6 + i * 5.5, 4, 3.5, "#E9A93B", "none", 0)
        s.rect(x + 32, y + 6 + i * 5.5, 4, 3.5, "#E9A93B", "none", 0)
    if label:
        s.text(x + 23, y + 140, "GPIO 분배보드", 12, C["ink"], anchor="middle")

def adapter(s, x, y, w=200, h=110, label="5V 4A"):
    s.rect(x, y, w, h, C["black"], C["black"], 1, 10)
    s.text(x + w / 2, y + h / 2 + 6, label, 15, "#ffffff", anchor="middle")

def fuse_holder(s, x, y):
    s.rect(x, y, 120, 48, "#333", C["ink"], 2, 8)
    s.rect(x + 40, y - 18, 40, 20, "#E9A93B", C["ink"], 1.5)
    s.text(x + 60, y + 30, "BE151 + 3A", 12, "#ffffff", anchor="middle")

# ------------------------------------------------------------------ 00 완성 모습
def fig_overview():
    s = Svg()
    s.header(None, "DeskPet 완성 모습과 구성 (계획안 rev C)", "직육면체 몸통 · 앞면 7인치 터치 LCD · 고양이 귀 2개 · 오른쪽 케이블 칸 · 보드는 뒤로 넣고 빼는 트레이")
    k = 2.7  # mm → px
    W, H = CASE["W"] * k, CASE["H"] * k
    x0, y0 = 40, 300
    s.poly([(x0 + W * 0.12, y0), (x0 + W * 0.25, y0 - 95), (x0 + W * 0.33, y0)], C["servo"])
    s.poly([(x0 + W * 0.67, y0), (x0 + W * 0.80, y0 - 95), (x0 + W * 0.88, y0)], C["servo"])
    s.rect(x0, y0, W, H, C["panel"], C["ink"], 3, 10)
    pw, ph = CASE["pocket_w"] * k, CASE["pocket_h"] * k
    lcd_front(s, x0 + CASE["wall"] * k, y0 + CASE["top"] * k, pw, ph)
    s.rect(x0 + W - (CASE["cable_bay"] + CASE["wall"]) * k + 4, y0 + 10, CASE["cable_bay"] * k - 8, H - 20, "#DDE3EA", C["grey"], 1.5, 4)
    s.text(x0 + W - (CASE["cable_bay"] / 2 + CASE["wall"]) * k, y0 + H / 2, "케이블\n칸", 13, C["sub"], anchor="middle")
    for i in range(6):
        s.circle(x0 + W * 0.36 + i * 18, y0 - 4, 4, C["accent"], "none", 0)
    for fx in (x0 + 25, x0 + W - 70):
        s.rect(fx, y0 + H, 45, 9, "#555", C["ink"], 1)
    s.text(x0 + W / 2, y0 + H + 55, "완성 모습 (앞)", 19, C["navy"], "bold", "middle")
    s.text(x0 + W / 2, y0 + H + 85, f"가로 {CASE['W']} × 높이 {CASE['H']} × 깊이 {CASE['depth']} mm (계획값, LCD 실측 후 확정)", 15, C["sub"], anchor="middle")
    ex = 700
    rows = [("P6 귀 ×2", 130), ("P3 윗판\n+ 서보·PCA9685·ReSpeaker", 260), ("P2 몸통 (앞틀·좌우 벽)\n+ 7인치 LCD 포켓", 430),
            ("P5 트레이 + Jetson", 600), ("P1 바닥판\n+ 스피커·분배보드·DC 단자·퓨즈", 720), ("P4 뒤판\n(배기·보드 개구부·DC 단자 구멍)", 850)]
    s.poly([(ex + 100, 150), (ex + 180, 70), (ex + 195, 150)], C["servo"]); s.poly([(ex + 400, 150), (ex + 480, 70), (ex + 495, 150)], C["servo"])
    s.rect(ex, 255, 600, 22, C["beige"]); s.rect(ex + 25, 277, 70, 50, C["servo"]); s.rect(ex + 505, 277, 70, 50, C["servo"])
    s.rect(ex + 120, 277, 90, 22, C["mic"]); s.rect(ex + 230, 277, 200, 22, C["pca"])
    s.rect(ex, 370, 600, 160, C["beige"], C["ink"], 2, 0, "6,5"); s.rect(ex, 370, 30, 160, C["lcd"])
    s.rect(ex + 200, 555, 340, 70, C["jet"]); s.rect(ex + 180, 625, 390, 16, C["beige"])
    s.rect(ex + 40, 675, 150, 45, "#7FB39A"); s.rect(ex, 720, 600, 22, C["beige"])
    s.rect(ex + 25, 742, 40, 18, "#555"); s.rect(ex + 535, 742, 40, 18, "#555")
    s.rect(ex + 570, 800, 24, 90, C["grey"])
    for _, y in rows:
        pass
    for label, y in rows:
        s.text(ex + 625, y + 8, label, 15)
    for y in (170, 380, 550, 700):
        s.line([(ex - 35, y), (ex - 35, y + 60)], C["grey"], 2, arrow=True)
    s.text(ex + 300, 960, "옆에서 본 분해도 (왼쪽 = 앞면)", 18, C["navy"], "bold", "middle")
    s.save("00-overview")

# ------------------------------------------------------------------ 01 부품 확인표
def fig_parts():
    s = Svg()
    s.header(None, "부품 확인표 — 봉투별로 나눠 두기", "2026-10-26 재구매 21종 기준 (엘레파츠·디바이스마트). 꺼내기 전에 개수를 세고 체크 표시하세요.")
    bags = [
        ("봉투 A · 전자 모듈", [("1×", "Jetson Orin Nano (대여)", "jet"), ("1×", "7인치 터치 LCD (C) + HDMI·USB 케이블", "lcd"),
                              ("1+1", "ReSpeaker Lite + 3W 4Ω 스피커", "mic"), ("1×", "PCA9685 16채널 (콘덴서 내장)", "pca")]),
        ("봉투 B · 구동·연결", [("2×", "SG90 호환 서보 (+혼·나사)", "servo"), ("1+1", "GPIO 분배보드 SMP0080 + 40p 리본", "cob"),
                              ("1×", "DP→HDMI 젠더 NXC067", "dp"), ("1×", "USB-A→C 데이터 케이블", "usb")]),
        ("봉투 C · 서보 전원", [("1+1", "5V 4A 어댑터 + 스위치 연장선 MT202", "psu"), ("1×", "DC 클립 터미널 TC5521F", "dc"),
                              ("1+10", "퓨즈 홀더 BE151 + 3A 퓨즈", "fuse"), ("1", "18AWG 적·흑 20cm (XT30 절단)", "wire")]),
        ("봉투 D · 선·고정·완충", [("40", "F/F 점퍼선 20cm", "jump"), ("1", "USB 케이블·열수축 3/6mm", "misc"),
                                ("240", "M2.5 나일론 볼트·너트·지지대", "m3"), ("1봉", "타이·EVA·실리콘 범퍼 4", "misc")]),
    ]
    for r, (bag, items) in enumerate(bags):
        y = 130 + r * 180
        s.text(30, y + 85, bag, 18, C["navy"], "bold")
        for c, (q, name, kind) in enumerate(items):
            x = 268 + c * 310
            s.rect(x, y, 296, 160, "#ffffff", "#D9DEE5", 2, 8)
            s.text(x + 18, y + 30, q, 18, C["accent"], "bold")
            s.text(x + 148, y + 142, name, 13.5, C["ink"], anchor="middle")
            cx, cy = x + 148, y + 70
            if kind == "jet": jetson(s, cx - 50, cy - 35, 100, 70, False)
            elif kind == "lcd": lcd_front(s, cx - 82, cy - 45, 164, 100, False)
            elif kind == "mic":
                s.rect(cx - 95, cy - 30, 110, 55, C["mic"], C["ink"], 2, 6); s.rect(cx + 35, cy - 30, 55, 55, "#7FB39A", C["ink"], 2, 8)
            elif kind == "pca": pca(s, cx - 85, cy - 32, 170, 64)
            elif kind == "servo": servo(s, cx - 100, cy - 20, "SG90"); servo(s, cx + 25, cy - 20, "SG90")
            elif kind == "cob": cobbler(s, cx - 90, cy - 58, False); s.rect(cx - 20, cy - 25, 110, 40, "#BFC6CE", C["ink"], 2)
            elif kind == "dp": s.rect(cx - 45, cy - 22, 55, 50, C["black"]); s.rect(cx + 10, cy - 15, 50, 36, "#555")
            elif kind == "usb": s.line([(cx - 90, cy), (cx + 90, cy)], C["black"], 5); s.rect(cx - 100, cy - 9, 20, 18, "#BFC6CE"); s.rect(cx + 85, cy - 6, 14, 12, "#BFC6CE")
            elif kind == "psu": adapter(s, cx - 75, cy - 40, 150, 85)
            elif kind == "dc": s.rect(cx - 50, cy - 18, 60, 36, C["black"]); s.rect(cx + 10, cy - 14, 34, 28, C["green"]); s.circle(cx - 50, cy, 9, "#555")
            elif kind == "fuse": fuse_holder(s, cx - 60, cy - 15)
            elif kind == "cap":
                for i in range(3): s.rect(cx - 55 + i * 40, cy - 30, 26, 62, "#2A3A6E", C["ink"], 2, 5)
            elif kind == "wire": s.line([(cx - 100, cy - 10), (cx + 100, cy - 10)], C["red"], 5); s.line([(cx - 100, cy + 12), (cx + 100, cy + 12)], C["black"], 5)
            elif kind == "jump":
                for i, col in enumerate(["#D62828", "#2B2B2B", "#E9A93B", "#2E8B57", "#3A6FD8", "#9AA3AE", "#C04CD6", "#F28C28"]):
                    s.line([(cx - 85 + i * 24, cy - 35), (cx - 85 + i * 24, cy + 30)], col, 4)
            elif kind == "m3":
                s.rect(cx - 70, cy - 35, 140, 70, "#333", C["ink"], 2, 6)
                for i in range(4):
                    for j in range(2): s.rect(cx - 60 + i * 32, cy - 28 + j * 32, 26, 24, "#555", "#777", 1)
            elif kind == "misc":
                s.line([(cx - 95, cy - 25), (cx + 95, cy - 25)], "#555", 3); s.rect(cx - 70, cy - 10, 140, 16, C["black"])
                s.rect(cx - 90, cy + 15, 90, 12, "#3B3B3B"); s.circle(cx + 50, cy + 22, 9, "#CFCFCF")
    s.footer("보유·대여품: Jetson 키트·microSD·공구 · 케이스는 학교 3D 프린터로 무료 출력")
    s.save("01-parts")

# ------------------------------------------------------------------ 02 출력물·공구
PRINTS = [
    ("P1 바닥판", f"{CASE['W']} × {CASE['depth']} × 3", "트레이 레일 2줄, 스피커 창, 흡기 구멍, 실리콘 범퍼 자리 Ø11 ×4, 분배보드·DC 단자·퓨즈 홀더 고정부"),
    ("P2 몸통 (앞틀+좌우 벽)", f"앞틀 {CASE['W']} × {CASE['H']}, 벽 깊이 {CASE['depth']}",
     f"LCD 포켓 {CASE['pocket_w']} × {CASE['pocket_h']} × {CASE['pocket_d']}, 창 {CASE['win_w']} × {CASE['win_h']}, 오른쪽 케이블 칸 {CASE['cable_bay']}"),
    ("P3 윗판", f"{CASE['W']} × {CASE['depth']} × 3", "귀 축 슬롯 2, 마이크 구멍, 서보·PCA9685 보스, ReSpeaker 클램프"),
    ("P4 뒤판", f"{CASE['W'] - 8} × {CASE['H'] - 6} × 2.5", "보드 개구부 110 × 42, 배기 그릴, DC 단자 구멍 Ø11"),
    ("P5 트레이", "받침대 실측 + 여유", "나사 없음: Jetson 키트 받침대 외곽 홈·걸림턱·누름턱, 손잡이"),
    ("P6 귀 ×2", "밑변 36, 높이 34, 벽 1.2", "속 빈 삼각형, 바닥에 서보 혼 홈, 각 10 g 이하"),
    ("P7 스피커 받침", "72 × 32 (3351 70×30)", "EVA 스펀지 자리, 바닥 스피커 창 위"),
    ("P8 케이블 클램프 ×4", "20 × 10", "LCD 옆면 HDMI·micro USB 케이블 꺾임 고정"),
    ("P9 LCD 뒤 누름쇠 ×4", "24 × 12 × 4", "LCD 기판 뒷면 가장자리를 눌러 포켓에 고정 (M2.5 나일론, 기판 구멍 실측)"),
]
def fig_prints():
    s = Svg(1560, 1000)
    s.header(None, "3D 출력물과 공구 (계획안 rev C)", "치수는 계획값(mm). 7인치 LCD 도착 후 실측해 P2·P9부터 확정합니다.")
    y = 130
    s.rect(30, y, 1500, 40, C["navy"], C["navy"], 1)
    for x, t in ((45, "출력물"), (390, "계획 치수 (mm)"), (760, "주요 형상")):
        s.text(x, y + 27, t, 16, "#ffffff", "bold")
    for i, (a, b, c) in enumerate(PRINTS):
        yy = y + 40 + i * 52
        if i % 2 == 0: s.rect(30, yy, 1500, 52, "#F0F4F8", "none", 0)
        s.text(45, yy + 33, a, 15.5, C["ink"], "bold"); s.text(390, yy + 33, b, 15); s.text(760, yy + 33, c, 14.5)
    y2 = y + 40 + len(PRINTS) * 52 + 40
    s.text(30, y2, "출력 설정 초기값", 17, C["navy"], "bold")
    s.text(30, y2 + 30, "노즐 0.4mm · 레이어 0.2mm · 벽 3겹 · 인필 15~20% · PLA. 가장 큰 부품이 204 × 150 mm라 220 mm급 프린터에서 분할 없이 출력합니다.\n출력 전 구멍·LCD 모서리·서보 고정부 시험편을 먼저 뽑습니다.", 15)
    s.text(30, y2 + 110, "공구", 17, C["navy"], "bold")
    s.text(30, y2 + 140, "멀티미터 · 납땜인두 · 와이어 스트리퍼 · 니퍼 · 소형 십자 드라이버 · 열풍기(또는 라이터) · 캘리퍼 · 저울 · 커터(EVA 재단)", 15)
    s.save("02-prints-tools")

# ------------------------------------------------------------------ 03~14 단계 그림
def fig_step1():
    s = Svg()
    s.header("1", "책상 위 단독 시험 — 케이스 작업 전", "납땜 없이 동봉 케이블과 점퍼선만 씁니다. 서보는 2단계 전원선을 만든 뒤 시험합니다.")
    s.partsbox([("1×", "Jetson + microSD"), ("1×", "7인치 LCD + 동봉 케이블"), ("1×", "DP→HDMI 젠더"), ("1+1", "ReSpeaker + 스피커"),
                ("1×", "USB-A→C 케이블"), ("1+1", "분배보드 + 리본"), ("4", "F/F 점퍼")])
    jetson(s, 470, 420, 220, 160)
    lcd_front(s, 60, 160, 330, 200)
    s.text(225, 385, "7인치 LCD (C) 164.9×107", 14, C["ink"], anchor="middle")
    s.rect(392, 230, 22, 50, "#555"); s.text(420, 225, "옆면 단자", 12, C["sub"])
    s.line([(690, 450), (760, 450), (760, 255), (414, 255)], C["black"], 4); s.rect(735, 440, 40, 26, C["black"])
    s.text(780, 440, "DP→HDMI → HDMI", 13)
    s.line([(690, 500), (800, 500), (800, 272), (414, 272)], C["grey"], 4); s.text(805, 520, "USB-A → LCD micro USB (터치·전원)", 13)
    respeaker(s, 140, 700)
    s.line([(470, 560), (380, 560), (380, 740), (330, 740)], C["black"], 4); s.text(250, 680, "USB-A → C", 13)
    cobbler(s, 820, 640); s.line([(690, 560), (760, 560), (760, 700), (820, 700)], "#9AA3AE", 8); s.text(705, 610, "40p 리본", 13)
    pca(s, 950, 680)
    for i, col in enumerate([C["red"], "#3A6FD8", "#E9A93B", C["black"]]):
        s.line([(866, 650 + i * 6), (930, 650 + i * 6), (930, 700 + i * 9), (956, 700 + i * 9)], col, 3)
    s.badge("1", 225, 145); s.badge("2", 145, 690); s.badge("3", 840, 625)
    s.text(1140, 420, "확인", 18, C["navy"], "bold")
    chk = ["화면 1024×600 가로, 잘림 없음 (xrandr)", "최대 밝기에서 꺼짐·깜빡임 없음", "터치가 입력 장치로 잡힘 (libinput list-devices)",
           "LCD 외형·두께·구멍·단자 위치 캘리퍼 실측", "arecord/aplay에 ReSpeaker, 낮은 볼륨부터", "큰 소리에서 USB 끊김 없음 (dmesg)",
           "40핀 I2C 버스에서 0x40 보임"]
    for i, c in enumerate(chk):
        s.rect(1140, 445 + i * 38, 16, 16, "none", C["green"], 2); s.text(1166, 459 + i * 38, c, 14.5)
    s.footer("LCD 실측값으로 케이스 CAD(P2·P9)를 확정한 뒤에 본 출력을 합니다")
    s.save("03-step01-bench-test")

def solder_panels(s, y=640):
    labels = [("a", "피복 7 mm 벗기기"), ("b", "꼬아서 겹치기"), ("c", "납땜"), ("d", "수축튜브 덮고 가열")]
    for i, (k, t) in enumerate(labels):
        x = 52 + i * 370
        s.rect(x, y, 334, 230, "#F7F9FB", "#D9DEE5", 2, 8)
        s.text(x + 22, y + 34, k, 20, C["accent"], "bold"); s.text(x + 167, y + 210, t, 15, C["ink"], anchor="middle")
        cy = y + 115
        if k == "a": s.line([(x + 30, cy), (x + 200, cy)], C["red"], 8); s.line([(x + 200, cy), (x + 260, cy)], "#C8742B", 3)
        if k == "b": s.line([(x + 20, cy), (x + 150, cy)], C["red"], 8); s.line([(x + 186, cy), (x + 316, cy)], C["red"], 8); s.poly([(x + 150, cy), (x + 168, cy - 17), (x + 186, cy), (x + 168, cy + 17)], "none", "#C8742B", 4)
        if k == "c": s.line([(x + 20, cy), (x + 316, cy)], C["red"], 8); s.circle(x + 168, cy, 20, "#C9CED4"); s.line([(x + 210, cy - 30), (x + 278, cy - 85)], "#555", 5)
        if k == "d": s.line([(x + 20, cy), (x + 316, cy)], C["red"], 8); s.rect(x + 108, cy - 24, 120, 48, "#111")

def fig_step2():
    s = Svg()
    s.header("2", "서보 전원선 만들기 (어댑터 → 스위치 → DC 클립 터미널 → 퓨즈 → PCA9685 V+)", "귀 모터 전기는 젯슨과 따로 공급합니다. 빨강 = +5V, 검정 = GND.")
    adapter(s, 40, 250, 190, 110)
    s.line([(230, 305), (300, 305)], C["black"], 6); s.rect(244, 292, 40, 26, "#444", C["ink"], 1.5, 4); s.rect(256, 297, 16, 16, C["accent"], "none", 0)
    s.text(264, 280, "스위치", 12, C["accent"], "bold", "middle"); s.rect(300, 290, 40, 30, C["black"]); s.text(320, 345, "플러그", 12, C["sub"], anchor="middle")
    s.rect(340, 284, 50, 42, "#444"); s.rect(390, 288, 40, 34, C["green"]); s.text(385, 270, "TC5521F", 12, C["sub"], anchor="middle")
    s.text(413, 345, "클립 단자\n+ / − 표시", 12, C["sub"], anchor="middle")
    s.line([(440, 298), (560, 298)], C["red"], 5); s.line([(440, 312), (900, 312), (900, 160), (1255, 160), (1255, 260)], C["black"], 5)
    fuse_holder(s, 560, 275)
    s.line([(680, 298), (880, 298), (880, 180), (1200, 180), (1200, 260)], C["red"], 5)
    pca(s, 1010, 255, 450, 180)
    s.rect(1390, 300, 40, 95, "#2A3A6E", C["ink"], 2, 5); s.text(1410, 420, "보드 내장\n1000µF", 12, C["ink"], anchor="middle")
    s.badge("1", 413, 240); s.badge("2", 620, 235); s.badge("3", 1040, 160); s.badge("4", 1410, 285)
    s.steps(["XT30 연장선의 양쪽 커넥터를 잘라 빨강·검정 18AWG 두 가닥을 만들고 TC5521F 클립 단자 +, −에 끼움 (테스터로 극성 확인)",
             "빨강 선 중간에 BE151 퓨즈 홀더를 이어 붙이기(a~d). 퓨즈는 3A MINI",
             "빨강 → PCA9685 V+ 단자 +, 검정 → − (단자 옆 실크 표시 확인)",
             "PCA9685 보드의 내장 전해 콘덴서(띠 = −)와 용량 확인. 없으면 1000µF 16V를 단자 바로 앞에 추가"], 50, 500, gap=34)
    solder_panels(s, 655)
    s.footer("주의  꽂기 전 멀티미터로 +와 − 사이 단락이 없는지 확인. 콘덴서 때문에 처음 '삑' 하고 멈추는 건 정상")
    s.save("04-step02-servo-power")

def fig_step3():
    s = Svg()
    s.header("3", "신호선 4가닥 + OE 점퍼", "분배보드 번호 = Jetson 40핀 물리 번호. 라즈베리파이 이름(GPIO2 등)은 무시합니다.")
    s.partsbox([("1+1", "분배보드 SMP0080 + 40p 리본"), ("5", "F/F 점퍼 (빨·파·노·검·초)"), ("1", "PCA9685")])
    cobbler(s, 160, 230); s.text(215, 222, "← 1번 핀(네모 표시)", 13, C["accent"])
    pca(s, 640, 330, 420, 170)
    pins = [("1 (3.3V)", "VCC", C["red"]), ("3 (SDA)", "SDA", "#3A6FD8"), ("5 (SCL)", "SCL", "#E9A93B"), ("6 (GND)", "GND", C["black"])]
    for i, (a, b, col) in enumerate(pins):
        y1, y2 = 240 + i * 11, 345 + i * 9
        s.line([(206, y1), (400 + i * 20, y1), (400 + i * 20, y2), (646, y2)], col, 4)
        s.text(215 + 0, 620 + i * 26, f"분배보드 {a} → PCA9685 왼쪽 헤더 {b}", 15)
        s.rect(196, 612 + i * 26 - 10, 12, 12, col, "none", 0)
    s.line([(1046, 350), (1085, 350), (1085, 380), (1046, 380)], C["green"], 4); s.text(1095, 370, "OE ↔ GND 짧은 점퍼", 14, C["green"])
    s.text(1095, 420, "헤더 V+ 핀: 아무것도 꽂지 않음", 14, C["warn"], "bold")
    s.badge("1", 150, 215); s.badge("2", 520, 300); s.badge("3", 1080, 330)
    s.text(640, 560, "PCA9685 모듈은 Voltly VLT-MD032입니다. 헤더 순서와 주소 점퍼(기본 0x40)를 실크로 확인합니다.", 15, C["warn"])
    s.text(640, 740, "출력 정지: 현재 C++ 런타임이 초기화·종료·취소·오류 때 PWM FULL_OFF를 씁니다.\nOE가 GND라 강제 종료·커널 정지·I2C 단절 때는 차단을 보장하지 않습니다 → 서보 전원 스위치(MT202)를 OFF로 끕니다.", 15)
    s.footer("서보 전기는 2단계의 V+ 나사 단자로만 들어옵니다. 40핀·점퍼로 서보 전류를 보내지 않습니다")
    s.save("05-step03-signal-wires")

def top_view(s, x0, y0, k, show_top=False):
    """위에서 본 내부 (아래쪽 = 앞면). 좌표 mm → px."""
    W, D = CASE["W"], CASE["depth"]
    X = lambda mm: x0 + mm * k
    Y = lambda mm: y0 + (D - mm) * k          # 앞면(0)이 아래
    s.rect(X(0), Y(D), W * k, D * k, C["panel"], C["ink"], 3)
    s.rect(X(4), Y(13), CASE["pocket_w"] * k, 9 * k, C["lcd"], C["ink"], 1.5)
    s.text(X(4 + CASE["pocket_w"] / 2), Y(13) + 7 * k, "7인치 LCD (두께 8)", 12, "#ffffff", anchor="middle")
    bx = 4 + CASE["pocket_w"]
    s.rect(X(bx + 2), Y(70), (CASE["cable_bay"] - 4) * k, 66 * k, "#DDE3EA", C["grey"], 1.5, 3, "5,4")
    s.text(X(bx + CASE["cable_bay"] / 2), Y(40), "케이블 칸", 12, C["sub"], anchor="middle")
    jx = (W - JET["w"]) / 2
    s.rect(X(jx), Y(146), JET["w"] * k, JET["d"] * k, C["jet"], C["ink"], 2, 3)
    s.text(X(jx + JET["w"] / 2), Y(84), "Jetson Orin Nano 103 × 90.5 (트레이)", 13, C["ink"], anchor="middle")
    s.rect(X(jx - 3), Y(150), (JET["w"] + 6) * k, 4 * k, C["grey"], C["ink"], 1)
    s.rect(X(10), Y(140), 18 * k, 55 * k, "#C0392B", C["ink"], 2); s.text(X(19), Y(78), "분배보드\n(바닥)", 11, C["ink"], anchor="middle")
    s.rect(X(158), Y(146), 40 * k, 30 * k, "#C0504D", C["ink"], 2); s.text(X(178), Y(134), "DC 단자\n+3A 퓨즈", 12, "#ffffff", anchor="middle")
    s.rect(X(67), Y(52), 70 * k, 30 * k, "#7FB39A", C["ink"], 2); s.text(X(102), Y(40), "3W 스피커\n(바닥, 아래 향함)", 12, C["ink"], anchor="middle")
    if show_top:
        s.rect(X(59), Y(57), 86 * k, 35 * k, C["mic"], C["ink"], 2, 0, "6,4"); s.text(X(102), Y(44), "ReSpeaker (윗판 아래)", 12, C["ink"], anchor="middle")
        s.rect(X(34), Y(70), 24 * k, 30 * k, C["servo"], C["ink"], 2); s.text(X(46), Y(58), "왼쪽 귀\nSG90", 11, C["ink"], anchor="middle")
        s.rect(X(146), Y(70), 24 * k, 30 * k, C["servo"], C["ink"], 2); s.text(X(158), Y(58), "오른쪽 귀\nSG90", 11, C["ink"], anchor="middle")
        s.rect(X(71), Y(122), 62 * k, 26 * k, C["pca"], C["ink"], 2, 0, "6,4"); s.text(X(102), Y(110), "PCA9685 (윗판 아래)", 12, C["ink"], anchor="middle")
    for fx, fy in ((10, 10), (W - 21, 10), (10, D - 21), (W - 21, D - 21)):
        s.circle(X(fx + 5.5), Y(fy + 5.5), 5.5 * k, "none", "#555", 1.5)
    return X, Y

def fig_step4():
    s = Svg()
    s.header("4", "바닥판(P1) 꾸미기 — 위에서 본 모습 (아래쪽 = 앞면)", "스피커는 EVA 스펀지 위에 띄우고, 분배보드는 왼쪽 뒤, DC 단자·퓨즈 홀더는 오른쪽 뒤에 고정합니다.")
    s.partsbox([("1", "P1 바닥판"), ("1", "3W 스피커 + P7 받침"), ("1", "EVA 스펀지 (재단)"), ("1", "GPIO 분배보드"),
                ("1", "퓨즈 홀더·DC 클립 단자"), ("4", "실리콘 범퍼"), ("8", "M2.5 나일론 볼트·지지대")])
    top_view(s, 60, 160, 4.2)
    s.badge("1", 60 + 102 * 4.2, 160 + 150 * 4.2 - 70); s.badge("2", 60 + 19 * 4.2, 160 + 20); s.badge("3", 60 + 178 * 4.2, 160 + 20); s.badge("4", 60 + 16 * 4.2, 160 + 150 * 4.2 - 20)
    s.steps(["스피커를 P7 받침 위 EVA에 얹음. 그릴은 아래(스피커 창), 나사로 직결하지 않음",
             "GPIO 분배보드를 왼쪽 뒤에 나일론 지지대 4개로 고정 (리본 소켓이 Jetson 쪽)",
             "DC 클립 단자를 뒤판 구멍 쪽에, 퓨즈 홀더는 오른쪽 뒤에 케이블 타이로",
             "바닥 네 모서리에 실리콘 범퍼(높이 5mm) — 소리가 막히면 받침을 높임"], 1080, 470)
    s.footer("확인: 트레이 레일 2줄 사이 가운데가 비어 있다")
    s.save("06-step04-base")

def fig_step5():
    s = Svg()
    s.header("5", "트레이(P5)에 Jetson 끼우기 — 나사 없음", "키트 플라스틱 받침대를 떼지 않고 받침대째 트레이 홈에 밀어 넣습니다. 단자 쪽과 손잡이는 뒤쪽.")
    s.partsbox([("1", "P5 트레이 (받침대 실측 홈)"), ("1", "Jetson Orin Nano (받침대 포함)"), ("1", "EVA 테이프 (헐거울 때만)")])
    s.rect(200, 300, 550, 500, C["beige"], C["ink"], 3, 6)
    s.rect(225, 325, 500, 450, "#D9D2C3", C["ink"], 2, 4, "6,4")
    s.text(475, 318, "받침대 홈", 13, C["sub"], anchor="middle")
    jetson(s, 245, 340, 460, 400, False)
    for x in (300, 650): s.rect(x - 30, 318, 60, 14, C["accent"], C["ink"], 1.5)
    s.text(475, 790, "앞뒤 걸림턱 + 위 누름턱(주황)", 13, C["accent"], anchor="middle")
    s.rect(400, 800, 150, 40, C["beige"], C["ink"], 2, 6); s.text(475, 826, "손잡이 (뒤)", 14, C["ink"], anchor="middle")
    s.text(475, 290, "앞 ↑", 14, C["sub"], anchor="middle")
    s.badge("1", 245, 330); s.badge("2", 475, 540); s.badge("3", 720, 760)
    s.steps(["Jetson 키트의 플라스틱 받침대를 떼지 않는다 (대여품 분해 없음)",
             "받침대째 트레이 홈에 앞에서 밀어 넣고, 위 누름턱이 받침대 가장자리를 잡게 한다",
             "헐거우면 홈에 EVA 테이프를 덧대고, 빡빡하면 받침대 실측값으로 트레이를 다시 출력"], 850, 520)
    s.footer("확인: 흔들리지 않고, SD카드 슬롯에 손이 닿으며, 반납할 때 받침대째 빠진다", C["ink"])
    s.save("07-step05-tray")

def fig_step6():
    s = Svg()
    s.header("6", "몸통(P2) 앞틀에 7인치 LCD 넣기 — 몸통 안쪽(뒤)에서 본 모습", "LCD를 포켓에 넣고 기판 뒷면 가장자리를 P9 누름쇠로 고정합니다. 단자는 오른쪽 케이블 칸으로.")
    s.partsbox([("1", "P2 몸통"), ("1", "7인치 LCD (C) 164.9×107×8"), ("4", "P9 LCD 누름쇠 + M2.5"), ("1+1", "동봉 HDMI·USB 케이블"),
                ("1", "DP→HDMI 젠더"), ("4", "P8 케이블 클램프")])
    k = 3.3; x0, y0 = 50, 330
    s.rect(x0, y0, CASE["W"] * k, CASE["H"] * k, C["beige"], C["ink"], 3)
    lx, ly = x0 + CASE["wall"] * k, y0 + CASE["top"] * k
    s.rect(lx, ly, LCD["w"] * k, LCD["h"] * k, "#C5CCD6", C["ink"], 2, 6)
    s.text(lx + LCD["w"] * k / 2, ly + LCD["h"] * k / 2, "LCD (C) 기판 뒷면\n(라즈베리파이 고정 구멍 — 실측 후 P9 위치 결정)", 15, C["ink"], anchor="middle")
    for cx, cy in ((lx - 10, ly + 20), (lx + LCD["w"] * k - 30, ly + 20), (lx - 10, ly + LCD["h"] * k - 60), (lx + LCD["w"] * k - 30, ly + LCD["h"] * k - 60)):
        s.rect(cx, cy, 40, 40, C["accent"], C["ink"], 2)
    px = lx + LCD["w"] * k
    s.rect(px - 12, ly + 120, 14, 30, "#555"); s.rect(px - 12, ly + 160, 14, 22, "#555"); s.rect(px - 12, ly + 190, 14, 22, "#555")
    s.text(px + 8, ly + 112, "HDMI", 12, C["ink"]); s.text(px + 40, ly + 175, "micro USB\n(터치·전원)", 12, C["ink"])
    bay = x0 + (CASE["W"] - CASE["wall"] - CASE["cable_bay"]) * k
    s.rect(bay + 4, y0 + 10, CASE["cable_bay"] * k - 8, CASE["H"] * k - 20, "none", C["grey"], 1.5, 4, "6,4")
    s.line([(px + 2, ly + 135), (bay + 70, ly + 135), (bay + 70, y0 + CASE["H"] * k + 40)], C["black"], 5)
    s.line([(px + 2, ly + 200), (bay + 50, ly + 200), (bay + 50, y0 + CASE["H"] * k + 40)], C["grey"], 5)
    s.rect(bay + 55, y0 + CASE["H"] * k + 40, 34, 26, C["black"]); s.text(bay + 100, y0 + CASE["H"] * k + 60, "DP 젠더", 13)
    for i in range(3): s.rect(bay + 30, ly + 260 + i * 60, 50, 16, "#D3C7A8", C["ink"], 1.5)
    s.badge("1", x0 + 30, y0 + 30); s.badge("2", px + 40, ly + 70); s.badge("3", bay + 60, ly + 250)
    s.steps(["화면 면이 앞을 향하게 LCD를 포켓에 넣고 P9 누름쇠 4개로 뒷면 가장자리 고정",
             "HDMI·micro USB 케이블을 꽂고 HDMI 끝에 DP 젠더를 끼워 둠 (케이스에 남김). 단자 위치는 실물로 확인",
             "케이블은 오른쪽 케이블 칸을 따라 P8 클램프로 고정해 뒤로 보냄"], 1110, 470)
    s.footer("확인: 커넥터에 케이블 무게가 걸리지 않고, 앞에서 누름쇠가 보이지 않으며, 터치 면을 가리지 않는다")
    s.save("08-step06-lcd")

def fig_step7():
    s = Svg()
    s.header("7", "윗판(P3) 아래면 조립 — 아래에서 본 모습", "마이크는 윗면 앞쪽 구멍에, 서보는 좌우, PCA9685는 가운데에 둡니다.")
    s.partsbox([("1", "P3 윗판"), ("1", "ReSpeaker 보드"), ("1", "EVA 가스켓 3mm"), ("2", "SG90 호환 서보"), ("1", "PCA9685"), ("8", "M2.5 나일론 볼트·지지대")])
    k = 3.3; x0, y0 = 50, 220
    s.rect(x0, y0, CASE["W"] * k, CASE["depth"] * k, C["beige"], C["ink"], 3)
    s.text(x0 + CASE["W"] * k / 2, y0 + CASE["depth"] * k + 30, "앞면 ↓", 14, C["sub"], anchor="middle")
    respeaker(s, x0 + 59 * k, y0 + CASE["depth"] * k - 95, 86 * k, 70, False)
    for i in range(5): s.circle(x0 + 80 * k + i * 30, y0 + CASE["depth"] * k - 108, 5, C["accent"], "none", 0)
    servo(s, x0 + 34 * k, y0 + 70 * k); servo(s, x0 + 146 * k, y0 + 70 * k)
    pca(s, x0 + 61 * k, y0 + 20 * k, 82 * k, 90)
    s.line([(x0 + 46 * k, y0 + 70 * k), (x0 + 46 * k, y0 + 60 * k), (x0 + 61 * k, y0 + 60 * k)], C["servo"], 3)
    s.line([(x0 + 158 * k, y0 + 70 * k), (x0 + 158 * k, y0 + 60 * k), (x0 + 143 * k, y0 + 60 * k)], C["servo"], 3)
    s.badge("1", x0 + 59 * k, y0 + CASE["depth"] * k - 110); s.badge("2", x0 + 34 * k, y0 + 64 * k); s.badge("3", x0 + 61 * k, y0 + 15 * k)
    s.steps(["ReSpeaker 마이크 2개를 윗판 마이크 구멍에 맞추고 EVA 가스켓을 끼워 출력 클램프로 누름 (나사 없음)",
             "서보 2개는 축이 윗판 슬롯 위로 나오게 보스에 고정 (혼은 아직 끼우지 않음)",
             "PCA9685를 가운데 지지대에 고정. CH0 = 왼쪽 귀, CH1 = 오른쪽 귀, 갈색 = GND 줄"], 1050, 480)
    s.footer("마이크 구멍 위치는 실물 기판을 재서 정합니다", C["ink"])
    s.save("09-step07-top-plate")

def fig_step8():
    s = Svg()
    s.header("8", "귀 중립 맞추고 달기", "혼 없이 중립을 먼저 맞춘 뒤 귀를 끼우고, ±25°부터 천천히 움직입니다.")
    s.partsbox([("2", "P6 귀"), ("2", "서보 혼 + 나사"), ("1", "저울")])
    for i, (x, t) in enumerate(((150, "① 중립 명령 (약 1.5 ms)"), (560, "② 전원 끄고 혼·귀 장착"), (970, "③ ±25°부터 시험"))):
        s.rect(x, 300, 340, 380, "#F7F9FB", "#D9DEE5", 2, 10)
        s.text(x + 170, 340, t, 17, C["navy"], "bold", "middle")
        s.rect(x + 100, 520, 140, 100, C["servo"], C["ink"], 2); s.circle(x + 170, 510, 16, "#fff")
        if i >= 1: s.poly([(x + 120, 500), (x + 170, 390), (x + 220, 500)], C["servo"])
        if i == 2:
            s.line([(x + 170, 500), (x + 110, 400)], C["grey"], 2, "6,4"); s.line([(x + 170, 500), (x + 230, 400)], C["grey"], 2, "6,4")
            s.text(x + 170, 660, "멈춤턱에 닿지 않는 범위만", 14, C["ink"], anchor="middle")
    s.footer("귀 1개 10 g 이하. 걸리거나 '지익' 소리가 나면 바로 서보 전원을 끄고 범위를 줄입니다. 듣는 중에는 귀를 멈춥니다")
    s.save("10-step08-ears")

def fig_step9():
    s = Svg()
    s.header("9", "몸통·바닥판 결합과 내부 배선", "윗판을 덮기 전에 선 4종을 연결합니다. 전원선과 오디오 선은 서로 다른 벽으로.")
    s.partsbox([("1", "P1+P2 결합 (M2.5 ×6)"), ("4", "신호 점퍼"), ("1", "서보 전원선 (2단계)"), ("1", "스피커 케이블"), ("1", "ReSpeaker USB 케이블"), ("10", "케이블 타이")])
    X, Y = top_view(s, 60, 160, 4.2, True)
    s.line([(X(28), Y(110)), (X(45), Y(110)), (X(45), Y(128)), (X(71), Y(128))], "#3A6FD8", 4)
    s.line([(X(178), Y(116)), (X(150), Y(116)), (X(150), Y(110)), (X(133), Y(110))], C["red"], 4)
    s.line([(X(102), Y(52)), (X(102), Y(57))], C["black"], 4)
    s.line([(X(145), Y(40)), (X(160), Y(40)), (X(160), Y(60)), (X(153), Y(60))], C["grey"], 4)
    s.steps(["신호 4가닥: 분배보드 → PCA9685 왼쪽 헤더 (왼쪽 벽 따라)", "서보 전원 빨강·검정 → PCA9685 V+ 단자 (오른쪽 벽 따라)",
             "3W 스피커 JST 커넥터 → ReSpeaker 스피커 단자", "ReSpeaker USB 케이블 → 뒤쪽 보드 자리까지", "팬 위 30mm 공간을 비우고 윗판을 덮어 M2.5로 고정"], 1080, 470)
    s.footer("뒤판은 10단계 뒤에 닫습니다. 선이 끼지 않았는지 확인", C["ink"])
    s.save("11-step09-close-case")

def fig_step10():
    s = Svg()
    s.header("10", "보드 넣고 연결하기 — 뒤에서 본 모습", "모든 전원이 꺼진 상태에서 시작합니다. 반납 후 다시 장착할 때도 같은 순서입니다.")
    k = 3.3; x0, y0 = 60, 260
    s.rect(x0, y0, CASE["W"] * k, CASE["H"] * k, C["panel"], C["ink"], 3)
    jx = x0 + (CASE["W"] - 110) / 2 * k
    s.rect(jx, y0 + CASE["H"] * k - 42 * k - 20, 110 * k, 42 * k, C["jet"], C["ink"], 2)
    s.text(jx + 55 * k, y0 + CASE["H"] * k - 21 * k - 20, "Jetson 단자 (DP · USB · DC)", 15, C["ink"], anchor="middle")
    s.circle(x0 + 182 * k, y0 + 100 * k, 4 * k, "#333"); s.text(x0 + 182 * k, y0 + 88 * k, "서보 DC", 12, C["ink"], anchor="middle")
    seq = ["트레이를 레일에 반쯤 밀어 넣기", "40p 리본 꽂기 (1번 표시끼리, 약 15cm 여유)", "트레이를 끝까지 밀기", "DP 젠더 → Jetson DP",
           "USB: LCD 케이블 (LCD 쪽은 micro USB)", "USB: ReSpeaker 케이블", "Jetson 전원 어댑터 → DC", "뒤판(P4) 닫기", "서보 5V 어댑터 → 스위치 연장선(OFF) → DC 클립 단자 (맨 마지막)"]
    s.steps(seq, 1050, 200, gap=40)
    s.rect(60, 860, 1440, 70, "#FFF4F0", "#E8B4A6", 2, 8)
    s.text(780, 903, "켜는 순서: Jetson 전원 → OS 부팅 → 프로그램 시작(서보 출력 OFF) → 서보 스위치 ON → 귀 동작 시작", 17, C["warn"], "bold", "middle")
    s.save("12-step10-dock-connect")

def fig_step11():
    s = Svg()
    s.header("11", "첫 전원과 검사", "한 줄씩 통과해야 다음 줄로 넘어갑니다. 결과는 실물 인수표에 실측값으로 기록합니다.")
    rows = [("무전원 검사", "+5V↔GND 단락 없음, 리본 1번 방향, 스피커 선 확인"), ("Jetson 전원만", "부팅 완료, 팬 회전"),
            ("화면", "1024×600 가로 전체, 잘림·깜빡임 없음, 최대 밝기에서 꺼짐 없음"), ("터치", "Jetson이 입력 장치로 인식 (처리 코드는 별도)"),
            ("ReSpeaker", "장치 인식, 녹음·재생 (낮은 볼륨부터)"), ("I2C", "40핀 버스에서 0x40"), ("서보 전원", "V+ 단자 5V, 의도치 않은 움직임 없음"),
            ("귀 한쪽씩 → 동시", "±25° 반복, 걸림·과열·재부팅 없음"), ("통합 30분", "음성·화면·귀 함께, 온도·메모리 기록"), ("최종 2시간", "케이스 닫고 반복, 윗판·보드 온도 기록")]
    y = 140
    s.rect(60, y, 1440, 44, C["navy"], C["navy"], 1)
    s.text(120, y + 30, "순서", 17, "#ffffff", "bold"); s.text(420, y + 30, "통과 기준", 17, "#ffffff", "bold")
    for i, (a, b) in enumerate(rows):
        yy = y + 44 + i * 64
        if i % 2 == 0: s.rect(60, yy, 1440, 64, "#F0F4F8", "none", 0)
        s.rect(78, yy + 22, 20, 20, "none", C["green"], 2)
        s.text(120, yy + 39, a, 16, C["ink"], "bold"); s.text(420, yy + 39, b, 16)
    s.save("13-step11-first-power")

def fig_step12():
    s = Svg()
    s.header("12", "분리·반납", "켜는 순서의 반대로 끕니다. 보드만 반납하고 나머지는 케이스에 남깁니다.")
    seq = ["프로그램에서 서보 출력 OFF", "서보 스위치 OFF 후 5V 어댑터 뽑기", "Jetson OS 종료 (sudo shutdown now)", "Jetson 전원 어댑터 뽑기", "뒤판 열기",
           "USB 2개·DP 젠더 뽑기", "트레이를 반쯤 빼고 40p 리본 분리", "트레이에서 받침대째 Jetson을 들어냄 (나사 없음)", "SD카드는 팀 소유 — 빼서 보관·백업"]
    s.steps(seq, 60, 170, gap=44)
    s.rect(820, 160, 690, 300, "#F2F8F4", "#A9CDB5", 2, 10)
    s.text(845, 200, "케이스에 남는 것", 18, C["green"], "bold")
    s.text(845, 240, "7인치 LCD·HDMI·USB 케이블·DP 젠더\nReSpeaker·스피커·USB 케이블\nPCA9685·귀 서보·서보 전원선·퓨즈\nGPIO 분배보드·리본, 트레이·지지대", 16)
    s.rect(820, 500, 690, 200, "#FFF4F0", "#E8B4A6", 2, 10)
    s.text(845, 540, "반납하는 것", 18, C["warn"], "bold")
    s.text(845, 580, "Jetson 보드(팬·방열판 포함), Jetson 전원 어댑터\n반납 전후 펌웨어 버전·구성품·외관 상태 기록", 16)
    s.save("14-step12-remove")

# ------------------------------------------------------------------ 케이스 배치 3면도
def fig_case_layout():
    s = Svg(2400, 960)
    s.text(30, 50, f"DeskPet 케이스 배치 계획안 rev C — 7인치 터치 LCD 기준 (가로 {CASE['W']} × 높이 {CASE['H']} × 깊이 {CASE['depth']} mm, LCD 실측 후 확정)", 28, C["ink"], "bold")
    k = 3.4
    # ① 앞
    x0, y0 = 60, 260
    s.text(x0, 150, "① 앞에서 본 모습", 22, C["ink"], "bold")
    W, H = CASE["W"] * k, CASE["H"] * k
    s.poly([(x0 + W * 0.12, y0), (x0 + W * 0.25, y0 - 110), (x0 + W * 0.33, y0)], C["servo"]); s.poly([(x0 + W * 0.67, y0), (x0 + W * 0.80, y0 - 110), (x0 + W * 0.88, y0)], C["servo"])
    s.rect(x0, y0, W, H, C["panel"], C["ink"], 3)
    lx, ly = x0 + CASE["wall"] * k, y0 + CASE["top"] * k
    s.rect(lx, ly, LCD["w"] * k, LCD["h"] * k, C["lcdedge"], C["lcdedge"], 1, 4)
    wx, wy = lx + (LCD["w"] - CASE["win_w"]) / 2 * k, ly + (LCD["h"] - CASE["win_h"]) / 2 * k  # 창은 기판 가운데 가정 (실측)
    s.rect(wx, wy, CASE["win_w"] * k, CASE["win_h"] * k, C["lcd"], "none", 0)
    s.text(lx + LCD["w"] * k / 2, ly + LCD["h"] * k / 2 - 10,
           f"7인치 IPS 터치 LCD (1024×600)\n외형 {LCD['w']} × {LCD['h']} × {LCD['t']} (Waveshare 사양)\n표시 영역 {LCD['aa_w']} × {LCD['aa_h']}\n앞틀 창 {CASE['win_w']} × {CASE['win_h']} (계획)", 14, "#ffffff", anchor="middle")
    bx = x0 + (CASE["W"] - CASE["wall"] - CASE["cable_bay"]) * k
    s.rect(bx + 3, y0 + 12, CASE["cable_bay"] * k - 6, H - 24, "#DDE3EA", C["grey"], 1.5, 4, "6,4")
    s.text(bx + CASE["cable_bay"] * k / 2, y0 + H / 2, "케이블\n칸\n30 mm\n(HDMI·\nUSB)", 13, C["sub"], anchor="middle")
    for i in range(6): s.circle(x0 + W * 0.40 + i * 16, y0 - 5, 4, C["accent"], "none", 0)
    s.text(x0 + W * 0.5, y0 - 25, "윗면 앞쪽 마이크 구멍", 13, C["accent"], anchor="middle")
    for fx in (x0 + 20, x0 + W - 52): s.rect(fx, y0 + H, 32, 10, "#555")
    s.text(x0 + W / 2, y0 + H + 45, f"외형 가로 {CASE['W']} = 벽 4 + LCD 포켓 {CASE['pocket_w']} + 케이블 칸 {CASE['cable_bay']} + 벽 4", 15, C["ink"], anchor="middle")
    s.text(x0 + W / 2, y0 + H + 72, f"높이 {CASE['H']} = 바닥 테두리 8 + 포켓 {CASE['pocket_h']} + 위 테두리 6 · 실리콘 범퍼 Ø11 × 5 mm", 15, C["ink"], anchor="middle")
    # ② 위
    s.text(880, 150, "② 위에서 본 내부 배치 (아래쪽 = 앞면)", 22, C["ink"], "bold")
    top_view(s, 880, 190, 3.4, True)
    s.text(880 + CASE["W"] * 1.7, 190 + CASE["depth"] * 3.4 + 35, f"깊이 {CASE['depth']} mm · 뒤쪽: 배기·보드 탈착·DC 단자", 15, C["ink"], anchor="middle")
    # ③ 옆 단면
    x3, y3, k3 = 1700, 290, 3.6
    s.text(x3, 150, "③ 옆에서 자른 단면 (왼쪽 = 앞면)", 22, C["ink"], "bold")
    D, Hh = CASE["depth"] * k3, CASE["H"] * k3
    s.poly([(x3 + 120, y3), (x3 + 180, y3 - 100), (x3 + 210, y3)], C["servo"])
    s.rect(x3, y3, D, Hh, C["panel"], C["ink"], 3)
    s.rect(x3 + 3 * k3, y3 + CASE["top"] * k3, LCD["t"] * k3, CASE["pocket_h"] * k3, C["lcd"], C["ink"], 1.5)
    s.text(x3 + 7 * k3, y3 + 80 * k3, "LCD", 10, "#ffffff", anchor="middle")
    s.rect(x3 + 20 * k3, y3 + 6 * k3, 35 * k3, 9 * k3, C["mic"], C["ink"], 1.5); s.text(x3 + 42 * k3, y3 + 12 * k3, "ReSpeaker", 11, C["ink"], anchor="middle")
    s.rect(x3 + 65 * k3, y3 + 6 * k3, 20 * k3, 26 * k3, C["servo"], C["ink"], 1.5); s.text(x3 + 75 * k3, y3 + 20 * k3, "SG90", 11, C["ink"], anchor="middle")
    s.rect(x3 + 90 * k3, y3 + 6 * k3, 50 * k3, 8 * k3, C["pca"], C["ink"], 1.5); s.text(x3 + 115 * k3, y3 + 11.5 * k3, "PCA9685", 11, C["ink"], anchor="middle")
    s.rect(x3 + 55.5 * k3, y3 + (CASE["H"] - 3 - 8 - 35) * k3, JET["d"] * k3, 35 * k3, C["jet"], C["ink"], 1.5)
    s.text(x3 + (52 + JET["d"] / 2) * k3, y3 + (CASE["H"] - 3 - 8 - 17) * k3, "Jetson + 방열판·팬 (35)", 12, C["ink"], anchor="middle")
    s.rect(x3 + 50 * k3, y3 + (CASE["H"] - 3 - 8) * k3, 96 * k3, 4 * k3, C["grey"])
    s.rect(x3 + 52 * k3, y3 + (CASE["H"] - 3 - 8 - 35 - 30) * k3, JET["d"] * k3, 30 * k3, "#F7F9FB", C["grey"], 1, 0, "4,4")
    s.text(x3 + (52 + JET["d"] / 2) * k3, y3 + (CASE["H"] - 3 - 8 - 35 - 14) * k3, "팬 위 공기층 30 이상", 12, C["sub"], anchor="middle")
    s.rect(x3 + 22 * k3, y3 + (CASE["H"] - 3 - 17) * k3, 30 * k3, 17 * k3, "#7FB39A", C["ink"], 1.5); s.text(x3 + 37 * k3, y3 + (CASE["H"] - 3 - 7) * k3, "스피커", 11, C["ink"], anchor="middle")
    for fx in (x3 + 10, x3 + D - 42): s.rect(fx, y3 + Hh, 32, 10, "#555")
    s.text(x3 + D / 2, y3 + Hh + 45, f"높이 {CASE['H']}: 바닥 3 + 트레이 8 + Jetson 35 + 공기층 30↑ + 여유 + 윗판 부품 12 + 윗판 3", 14, C["ink"], anchor="middle")
    s.text(x3 + D / 2, y3 + Hh + 70, "LCD가 가벼워져(약 0.4 kg) 앞쏠림이 줄었지만 Jetson은 뒤쪽 낮게 둠", 14, C["ink"], anchor="middle")
    s.save("case-layout")

for f in (fig_overview, fig_parts, fig_prints, fig_step1, fig_step2, fig_step3, fig_step4, fig_step5, fig_step6,
          fig_step7, fig_step8, fig_step9, fig_step10, fig_step11, fig_step12, fig_case_layout):
    f()
print("ok", len(os.listdir(OUT)))
