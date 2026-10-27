"""DeskPet 케이스 3D 모델 (build123d). 출력물 P1~P7과 실제 부품·배선 더미를 같은 좌표에 만든다.

좌표 (mm): x = 앞에서 봤을 때 왼쪽 → 오른쪽, y = 앞면(0) → 뒤(150), z = 바닥(0) → 위(122).
치수 출처 표시: [사양] 판매처·제조사 자료, [계획] 케이스 설계 위키, [추정] 실물 확인 전 가정값 → 도착 후 실측.
"""
from build123d import (Align, Axis, Box, Color, Compound, Cylinder, Plane, Polygon, Pos, Rot, Solid, Vector, extrude)

# ------------------------------------------------------------------ 치수 표 (한 곳에서 관리)
CASE = dict(W=204, D=150, H=122, wall=4, plate=3, back=2.5, lip=3)   # [계획] rev C 외형
LCD = dict(w=164.9, h=106.96, t=8, aa_w=154.21, aa_h=85.92)           # [사양] Waveshare 7inch HDMI LCD (C)
POCKET = dict(x0=4, x1=170, z0=8.5, depth=9)                          # [계획] LCD 포켓 166 × (위로 열림) × 9
WINDOW = dict(w=156, h=88)                                            # [계획] 표시 영역 + 여유, 실측 후 위치 확정
LCD_PORTS = dict(hdmi_zc=88, usb_zc=55)                               # [추정] 오른쪽 옆면 HDMI·micro USB 높이(LCD 바닥 기준 아님, 케이스 z)
JET = dict(w=103, d=90.5, h=35, x0=50.5, y0=55.5, z0=5)               # [사양] 캐리어 103 × 90.5, [추정] 받침대 포함 높이 35
FAN_AIR = 30                                                          # [계획] 팬 위 공기층
SPK = dict(w=70, d=30.5, h=17)                                        # [사양] Adafruit 3351
RSP = dict(w=86, d=35)                                                # [사양] ReSpeaker Lite 86 × 35, 마이크 위치 [추정]
PCA = dict(w=62.2, d=25.4, holes=(55.9, 19.1))                        # [추정] Adafruit 호환 PCA9685 모듈
SG90 = dict(l=22.5, w=12.2, h=22.7, flange=32.3, above_flange=6.8, shaft_off=5.9)  # [사양] SG90 일반 치수
GPIO = dict(w=66, h=55)                                               # [추정] SMP0080 T형 분배보드
EARS = ((46, 52), (158, 52))                                          # [계획] 귀 축 x 46·158, y는 윗판 앞쪽
EAR = dict(base=36, height=34, t=9, wall=1.2, hub_d=16, hub_h=5)      # [계획] 귀 밑변 36 · 높이 34 · 벽 1.2
BED = (220, 220, 250)                                                 # [추정] 학교 프린터 출력 범위
M25 = dict(hole=2.8, nut_af=5.3, nut_t=2.4, head_d=5.2, head_h=1.6)   # M2.5 나일론 나사·너트

W, D, H, T = CASE["W"], CASE["D"], CASE["H"], CASE["plate"]
ZTOP = H - T                       # 윗판 아랫면 119
YBACK = D - CASE["back"]           # 뒤판 안쪽면 147.5

# ------------------------------------------------------------------ 도형 도우미
def box(x0, y0, z0, x1, y1, z1):
    return Pos(x0, y0, z0) * Box(x1 - x0, y1 - y0, z1 - z0, align=(Align.MIN,) * 3)

def cyl_z(cx, cy, z0, d, h):
    return Pos(cx, cy, z0) * Cylinder(d / 2, h, align=(Align.CENTER, Align.CENTER, Align.MIN))

def cyl_x(x0, cy, cz, d, h):
    return Pos(x0, cy, cz) * (Rot(0, 90, 0) * Cylinder(d / 2, h, align=(Align.CENTER, Align.CENTER, Align.MIN)))

def cyl_y(cx, y0, cz, d, h):
    return Pos(cx, y0, cz) * (Rot(-90, 0, 0) * Cylinder(d / 2, h, align=(Align.CENTER, Align.CENTER, Align.MIN)))

def prism_xy(pts, z0, z1):
    """XY 평면 다각형을 z0~z1로 세운 기둥 (출력 지지용 45° 경사 등). 꼭짓점 방향과 무관하게 위로 세운다."""
    area2 = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1]))
    pts = pts if area2 > 0 else pts[::-1]
    return Pos(0, 0, z0) * extrude(Polygon(*pts, align=None), amount=z1 - z0)

def slot_x(x0, x1, zc, h, y0, y1):
    """y 방향으로 뚫는 둥근 끝 가로 구멍."""
    r = h / 2
    return box(x0 + r, y0, zc - r, x1 - r, y1, zc + r).fuse(cyl_y(x0 + r, y0, zc, h, y1 - y0), cyl_y(x1 - r, y0, zc, h, y1 - y0))

def fuse(*shapes):
    return shapes[0].fuse(*shapes[1:]) if len(shapes) > 1 else shapes[0]

# ------------------------------------------------------------------ 결합 보스 위치 (P1·P3 ↔ P2, P4 ↔ P2)
BOSS_Y_BOTTOM = {"L": (25, 85, 137), "R": (25, 85)}       # 오른쪽 뒤 아래는 DC 단자 자리라 비움
BOSS_Y_TOP = {"L": (25, 85, 137), "R": (25, 85, 137)}
BOSS_Z_BACK = (40, 100)

def _wall_boss(side, y, z0, z1, nut_z):
    """옆벽에 붙은 8 × 8 보스. 앞(-y)면은 앞면을 바닥에 두고 출력할 때를 위해 45° 경사."""
    x0, x1, xc = (4, 12, 8) if side == "L" else (192, 200, 196)
    b = box(x0, y - 4, z0, x1, y + 4, z1)
    wall_x, in_x = (4, 12) if side == "L" else (200, 192)
    b = b.fuse(prism_xy([(wall_x, y - 4), (in_x, y - 4), (wall_x, y - 12)], z0, z1))
    b = b.cut(cyl_z(xc, y, z0 - 0.1, M25["hole"], z1 - z0 + 0.2))
    nx0, nx1 = (xc - M25["nut_af"] / 2, x1 + 0.1) if side == "L" else (x0 - 0.1, xc + M25["nut_af"] / 2)
    return b.cut(box(nx0, y - M25["nut_af"] / 2, nut_z, nx1, y + M25["nut_af"] / 2, nut_z + M25["nut_t"]))

def _back_boss(side, zc):
    x0, x1, xc = (4, 12, 8) if side == "L" else (192, 200, 196)
    y0, y1 = YBACK - 8, YBACK
    wall_x, in_x = (4, 12) if side == "L" else (200, 192)
    b = box(x0, y0, zc - 5, x1, y1, zc + 5).fuse(prism_xy([(wall_x, y0), (in_x, y0), (wall_x, y0 - 8)], zc - 5, zc + 5))
    b = b.cut(cyl_y(xc, y0 - 0.1, zc, M25["hole"], 8.2))
    return b.cut(box(xc - M25["nut_af"] / 2, y1 - 5.5, zc - M25["nut_af"] / 2, xc + M25["nut_af"] / 2, y1 - 5.5 + M25["nut_t"], zc + 5.1))

def screw_points():
    pts = {"P1": [], "P3": [], "P4": []}
    for s, ys in BOSS_Y_BOTTOM.items():
        pts["P1"] += [(8 if s == "L" else 196, y) for y in ys]
    for s, ys in BOSS_Y_TOP.items():
        pts["P3"] += [(8 if s == "L" else 196, y) for y in ys]
    pts["P4"] = [(x, z) for x in (8, 196) for z in BOSS_Z_BACK]
    return pts

# ------------------------------------------------------------------ 출력물
def p2_body():
    """몸통: 앞틀(LCD 창·포켓) + 좌우 벽 + 결합 보스. 앞면을 바닥에 두고 분할 없이 출력."""
    z0, z1 = T, ZTOP
    lcd_cx, lcd_cz = (POCKET["x0"] + POCKET["x1"]) / 2, POCKET["z0"] + LCD["h"] / 2
    lip = box(0, 0, z0, W, CASE["lip"], z1).cut(
        box(lcd_cx - WINDOW["w"] / 2, -0.1, lcd_cz - WINDOW["h"] / 2, lcd_cx + WINDOW["w"] / 2, CASE["lip"] + 0.1, lcd_cz + WINDOW["h"] / 2))
    py1 = CASE["lip"] + POCKET["depth"]                                     # 포켓 뒤 끝 y 12
    ledge = box(POCKET["x0"], CASE["lip"], z0, POCKET["x1"], py1, POCKET["z0"])   # LCD 받침턱
    rwall = box(POCKET["x1"], CASE["lip"], z0, POCKET["x1"] + 3, py1, z1)         # 포켓 오른쪽 벽 (케이블 칸 경계)
    for zc, hh in ((LCD_PORTS["hdmi_zc"], 24), (LCD_PORTS["usb_zc"], 14)):          # 단자 플러그 통로
        rwall = rwall.cut(box(POCKET["x1"] - 0.1, CASE["lip"] - 0.1, zc - hh / 2, POCKET["x1"] + 3.1, py1 + 0.1, zc + hh / 2))
    rib_l = box(POCKET["x0"], py1, POCKET["z0"], POCKET["x0"] + 4, py1 + 2.5, POCKET["z0"] + LCD["h"] - 0.5)   # LCD 뒤 누름턱
    rib_r = box(POCKET["x1"] - 4, py1, POCKET["z0"], POCKET["x1"], py1 + 2.5, LCD_PORTS["usb_zc"] - 7)
    walls = [box(0, 0, z0, CASE["wall"], D, z1), box(W - CASE["wall"], 0, z0, W, D, z1)]
    bosses = [_wall_boss(s, y, z0, z0 + 10, z0 + 4) for s, ys in BOSS_Y_BOTTOM.items() for y in ys]
    bosses += [_wall_boss(s, y, z1 - 10, z1, z1 - 4 - M25["nut_t"]) for s, ys in BOSS_Y_TOP.items() for y in ys]
    bosses += [_back_boss(s, zc) for s in "LR" for zc in BOSS_Z_BACK]
    return fuse(lip, ledge, rwall, rib_l, rib_r, *walls, *bosses)

def _plate_screw_cuts(points, z_face, outward):
    cuts = []
    for x, y in points:
        cuts.append(cyl_z(x, y, -1 if outward < 0 else z_face - T - 0.1, M25["hole"], T + 1.2))
        hz = z_face - 0.1 if outward > 0 else -0.1
        cuts.append(cyl_z(x, y, hz - (M25["head_h"] if outward > 0 else 0), M25["head_d"], M25["head_h"] + 0.1))
    return cuts

def p1_base():
    """바닥판: 스피커 창, 트레이 레일, 흡기, DC 단자 받침, 퓨즈 홀더 타이 구멍."""
    p = box(0, 0, 0, W, D, T)
    cuts = [box(72, 22.5 + i * 4, -0.1, 132, 24.5 + i * 4, T + 0.1) for i in range(7)]      # 스피커 창 (스피커가 아래를 봄)
    cuts += [box(64, 104 + i * 6, -0.1, 140, 107 + i * 6, T + 0.1) for i in range(6)]        # 흡기 (트레이 바닥 구멍과 겹침)
    cuts += [box(x, y, -0.1, x + 3, y + 6, T + 0.1) for x in (166, 187) for y in (82, 102)]  # 퓨즈 홀더 케이블 타이
    cuts += _plate_screw_cuts(screw_points()["P1"], T, -1)
    adds = [box(45, 53, T, 47, YBACK, T + 6), box(157, 53, T, 159, YBACK, T + 6)]            # 트레이 레일
    adds += [box(176.5, 118, T, 178.5, 145, T + 7), box(193.5, 118, T, 195.5, 145, T + 7)]   # DC 클립 터미널 받침
    adds += [box(62, 17.5, T, 64, 52, T + 3), box(140, 17.5, T, 142, 52, T + 3)]             # P7 위치 맞춤 턱
    p = fuse(p, *adds).cut(*cuts)
    # 범퍼 자리 표시 (깊이 0.6)
    return p.cut(*[cyl_z(x, y, -0.1, 11.6, 0.7) for x in (15.5, W - 15.5) for y in (15.5, D - 15.5)])

def p3_top():
    """윗판: 귀 서보 축 구멍·보스, 마이크 구멍, ReSpeaker 레일, PCA9685 지지대 구멍, LCD 누름 리브."""
    p = box(0, 0, ZTOP, W, D, H)
    cuts = [cyl_z(x, y, ZTOP - 0.1, 12.5, T + 0.2) for x, y in EARS]
    for mx in (67, 137):                                                                      # 마이크 위 구멍 7개씩 [추정 위치]
        cuts += [cyl_z(mx + dx, 39.5 + dy, ZTOP - 0.1, 1.6, T + 0.2)
                 for dx, dy in ((0, 0), (2.6, 0), (-2.6, 0), (1.3, 2.25), (-1.3, 2.25), (1.3, -2.25), (-1.3, -2.25))]
    hx, hy = PCA["holes"]
    cuts += [cyl_z(102 + sx * hx / 2, 109 + sy * hy / 2, ZTOP - 0.1, M25["hole"], T + 0.2) for sx in (-1, 1) for sy in (-1, 1)]
    cuts += _plate_screw_cuts(screw_points()["P3"], H, 1)
    adds = []
    for x, y in EARS:                                                                         # SG90 날개 나사 보스 (축이 앞쪽)
        cy = y - SG90["shaft_off"] + SG90["l"] / 2
        for yy in (cy - 13.75, cy + 13.75):
            adds.append(cyl_z(x, yy, ZTOP - SG90["above_flange"], 4.4, SG90["above_flange"]).cut(cyl_z(x, yy, ZTOP - SG90["above_flange"] - 0.1, 1.8, 5)))
    rz = ZTOP - 6.6                                                                           # ReSpeaker 레일: 기판 아랫면 114.4에 받침
    for ya, yb, lip0, lip1 in ((19.5, 21.5, 19.5, 24), (57.5, 59.5, 55, 59.5)):
        adds += [box(59, ya, rz, 145, yb, ZTOP), box(59, lip0, rz, 145, lip1, rz + 2)]
    adds.append(box(57, 19.5, rz, 59, 59.5, ZTOP))                                            # 끝막이 (반대쪽에서 밀어 넣음)
    adds.append(box(6, 3.5, POCKET["z0"] + LCD["h"] + 0.3, 168, 11.5, ZTOP))                  # LCD 위 누름 리브 (EVA 0.3)
    return fuse(p, *adds).cut(*cuts)

def p4_back():
    """뒤판: Jetson 단자 창, 케이블 통과 구멍, DC 단자 구멍, 배기 그릴."""
    p = box(4, YBACK, T, W - 4, D, ZTOP)
    cuts = [box(47, YBACK - 0.1, 5, 157, D + 0.1, 47)]                                          # Jetson 단자 창 110 × 42
    cuts.append(slot_x(163, 195, 38, 20, YBACK - 0.1, D + 0.1))                               # LCD·ReSpeaker 케이블 통과
    cuts.append(cyl_y(186, YBACK - 0.1, 9.5, 11, CASE["back"] + 0.2))                         # DC 클립 터미널
    cuts += [slot_x(40, 164, 72 + i * 7, 3.5, YBACK - 0.1, D + 0.1) for i in range(6)]       # 배기
    cuts += [cyl_y(x, YBACK - 0.1, z, M25["hole"], CASE["back"] + 0.2) for x, z in screw_points()["P4"]]
    return p.cut(*cuts)

def p5_tray():
    """Jetson 트레이: 나사 없이 키트 받침대째 끼움. 바닥 환기 구멍, 뒤쪽 손잡이."""
    t = fuse(box(47.5, 53, T, 156.5, YBACK - 0.1, T + 2), box(47.5, 53, T + 2, 49.5, YBACK - 0.1, T + 8),
             box(154.5, 53, T + 2, 156.5, YBACK - 0.1, T + 8), box(49.5, 53, T + 2, 154.5, 55, T + 8),
             box(47.5, 136, T + 8, 49.5, YBACK - 0.1, T + 19), box(154.5, 136, T + 8, 156.5, YBACK - 0.1, T + 19))
    return t.cut(*[box(64, 104 + i * 6, T - 0.1, 140, 107 + i * 6, T + 2.1) for i in range(6)])

def p6_ear():
    """귀 1개 (2개 출력). 속 빈 삼각형 + 서보 혼 허브. 축 중심이 원점."""
    b, h, t, wl = EAR["base"], EAR["height"], EAR["t"], EAR["wall"]
    def tri(inset, depth):
        pts = [(-b / 2 + inset * 1.8, inset), (b / 2 - inset * 1.8, inset), (0, h - inset * 2.2)]
        return Pos(0, depth / 2, EAR["hub_h"]) * (Rot(90, 0, 0) * extrude(Polygon(*pts, align=None), amount=depth))
    ear = tri(0, t).cut(tri(wl, t - 2 * wl))
    hub = cyl_z(0, 0, 0, EAR["hub_d"], EAR["hub_h"]).cut(cyl_z(0, 0, -0.1, 12.6, 1.3), cyl_z(0, 0, -0.1, 7, 4))  # 기어 탑·혼 자리 (실측 후 맞춤)
    return ear.fuse(hub)

def p7_speaker_cradle():
    return box(65, 17.5, T, 139, 52, T + 6).cut(box(67, 19.5, T - 0.1, 137, 50, T + 6.1)).fuse(
        box(65, 17.5, T, 139, 52, T + 0.8).cut(box(70, 22.5, T - 0.1, 134, 47, T + 1)))      # 가장자리 EVA 자리

def printed_parts():
    ears = [Pos(x, y, H + 0.5) * p6_ear() for x, y in EARS]
    return {"P1 바닥판": p1_base(), "P2 몸통": p2_body(), "P3 윗판": p3_top(), "P4 뒤판": p4_back(),
            "P5 트레이": p5_tray(), "P6 왼쪽 귀": ears[0], "P6 오른쪽 귀": ears[1], "P7 스피커 받침": p7_speaker_cradle()}

# ------------------------------------------------------------------ 구매 부품 더미 (충돌·배치 확인용)
def sg90(x, y, flip=False):
    """축이 (x, y), 몸통은 뒤(+y)로. 몸통 윗면이 윗판 아랫면에 닿음."""
    y0 = y - SG90["shaft_off"]
    zt = ZTOP
    body = box(x - SG90["w"] / 2, y0, zt - SG90["h"], x + SG90["w"] / 2, y0 + SG90["l"], zt)
    cy = y0 + SG90["l"] / 2
    fz = zt - SG90["above_flange"]
    flange = box(x - SG90["w"] / 2, cy - SG90["flange"] / 2, fz - 2.5, x + SG90["w"] / 2, cy + SG90["flange"] / 2, fz)
    flange = flange.cut(*[cyl_z(x, cy + s * 13.75, fz - 3, 2.4, 4) for s in (-1, 1)])
    return fuse(body, flange, cyl_z(x, y, zt, 11.6, 4), cyl_z(x, y, zt + 4, 4.8, 3))

def components():
    j = JET
    jx1, jy1 = j["x0"] + j["w"], j["y0"] + j["d"]
    hx, hy = PCA["holes"]
    lcd_x0 = POCKET["x0"] + (POCKET["x1"] - POCKET["x0"] - LCD["w"]) / 2
    lcd_x1 = lcd_x0 + LCD["w"]
    ly0, ly1 = CASE["lip"], CASE["lip"] + LCD["t"]
    hz, uz = LCD_PORTS["hdmi_zc"], LCD_PORTS["usb_zc"]
    parts = {
        "Jetson Orin Nano (받침대 포함)": fuse(
            box(j["x0"], j["y0"], j["z0"], jx1, jy1, j["z0"] + 5), box(j["x0"], j["y0"], j["z0"] + 5, jx1, jy1, j["z0"] + 6.6),
            box(52, 128, j["z0"] + 6.6, 152, jy1, j["z0"] + 22.6),                              # 뒤쪽 단자 줄 [추정]
            box(69, 68, j["z0"] + 6.6, 139, 126, j["z0"] + j["h"]),                              # 방열판·팬
            box(60, 58, j["z0"] + 6.6, 111, 63, j["z0"] + 15.1)),                               # 40핀 헤더 (보드 앞쪽)
        "7인치 LCD (C)": box(lcd_x0, ly0, POCKET["z0"], lcd_x1, ly1, POCKET["z0"] + LCD["h"]),
        "HDMI ㄱ자 젠더 + 플러그": fuse(box(lcd_x1, ly0 + 0.5, hz - 11, 181, ly0 + 11, hz + 11),
                                     box(168, ly0 + 11, hz - 10, 181, 30, hz + 10), box(169, 30, hz - 10.5, 180, 68, hz + 10.5)),
        "micro USB ㄱ자 플러그": fuse(box(lcd_x1, ly0 + 2, uz - 4, 176, ly1, uz + 4), box(168, ly1, uz - 3.5, 176, 32, uz + 3.5)),
        "ReSpeaker Lite": fuse(box(59, 22, ZTOP - 4.6, 145, 57, ZTOP - 3), box(62, 25, ZTOP - 11, 142, 54, ZTOP - 4.6),
                               box(141, 34, ZTOP - 9, 146.5, 43, ZTOP - 4.6), box(59.5, 28, ZTOP - 9, 65, 36, ZTOP - 4.6)),
        "Adafruit 3351 스피커": box(67, 19.5, T + 3, 67 + SPK["w"], 19.5 + SPK["d"], T + 3 + SPK["h"]),
        "PCA9685 + 지지대": fuse(box(71, 96.3, ZTOP - 7.6, 71 + PCA["w"], 96.3 + PCA["d"], ZTOP - 6),
                               box(72, 97, ZTOP - 19, 132, 121, ZTOP - 7.6),
                               *[cyl_z(102 + sx * hx / 2, 109 + sy * hy / 2, ZTOP - 6, 4.6, 6) for sx in (-1, 1) for sy in (-1, 1)]),
        "SG90 왼쪽 귀": sg90(*EARS[0]),
        "SG90 오른쪽 귀": sg90(*EARS[1]),
        "GPIO 분배보드 SMP0080 + 지지대": fuse(
            box(10, 70, 16, 11.6, 70 + GPIO["w"], 16 + GPIO["h"]), box(11.6, 76, 57, 20.5, 134, 66), box(11.6, 78, 22, 20, 132, 27),
            *[cyl_x(4, y, z, 4.6, 6) for y in (73, 133) for z in (19, 68)]),
        "DC 클립 터미널 TC5521F": box(179, 115.5, T, 193, YBACK, T + 12),
        "퓨즈 홀더 BE151": box(170, 75, T, 186, 115, T + 12),
        "실리콘 범퍼 ×4": fuse(*[cyl_z(x, y, -5, 11, 5) for x in (15.5, W - 15.5) for y in (15.5, D - 15.5)]),
    }
    return parts

# ------------------------------------------------------------------ 배선 (중심선 + 굵기). 끝점은 단자 위치
CABLES = {
    "HDMI (LCD ← DP 젠더)": (3.5, [(174.5, 68, 88), (186, 90, 88), (188, 120, 60), (186, 140, 42), (178, 149, 38),
                                   (165, 168, 30), (110, 175, 22), (80, 160, 20)]),
    "LCD micro USB": (2.0, [(172, 32, 55), (190, 45, 55), (194, 100, 50), (187, 140, 36), (170, 149, 36), (140, 168, 22), (118, 158, 20)]),
    "ReSpeaker USB": (2.0, [(146.5, 38.5, 112), (170, 40, 108), (190, 70, 100), (190, 120, 70), (188, 140, 44), (186, 149, 40),
                            (130, 170, 20), (105, 158, 20)]),
    "스피커 JST": (1.5, [(67, 34.5, 14), (56, 36, 30), (56, 32, 100), (62, 32, 109)]),
    "40핀 리본": (3.0, [(85, 60.5, 23), (85, 58, 35), (35, 62, 45), (24, 90, 61.5), (21, 105, 61.5)]),
    "신호 점퍼 4가닥 + OE": (2.0, [(21, 105, 24.5), (30, 110, 35), (30, 112, 95), (60, 110, 104), (73, 109, 104)]),
    "왼쪽 귀 서보선": (1.6, [(46, 68.6, 100), (50, 90, 100), (75, 98, 102)]),
    "오른쪽 귀 서보선": (1.6, [(158, 68.6, 100), (150, 90, 100), (130, 98, 102)]),
    "서보 전원 18AWG (단자 → 퓨즈)": (2.2, [(186, 118, 15), (178, 116, 13)]),
    "서보 전원 18AWG (퓨즈 → PCA9685)": (2.2, [(178, 75, 9), (178, 70, 20), (172, 85, 70), (150, 105, 100), (134, 112, 104)]),
}

def cable(r, pts):
    segs = []
    for a, b in zip(pts, pts[1:]):
        va, vb = Vector(*a), Vector(*b)
        segs.append(Solid.make_cylinder(r, (vb - va).length, Plane(origin=va, z_dir=vb - va)))
    segs += [Pos(*p) * Solid.make_sphere(r) for p in pts[1:-1]]
    return fuse(*segs)

def cables():
    return {name: cable(r, pts) for name, (r, pts) in CABLES.items()}

# ------------------------------------------------------------------ 검사용 공간
def keepouts():
    return {"Jetson 팬 위 공기층 30": box(69, 68, JET["z0"] + JET["h"], 139, 126, JET["z0"] + JET["h"] + FAN_AIR)}

COLORS = {
    "P1": (0.93, 0.91, 0.86), "P2": (0.96, 0.94, 0.90), "P3": (0.93, 0.91, 0.86), "P4": (0.88, 0.86, 0.82),
    "P5": (0.70, 0.78, 0.86), "P6": (0.95, 0.78, 0.55), "P7": (0.62, 0.78, 0.66),
}
