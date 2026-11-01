"""DeskPet 케이스 3D 모델 rev D (build123d). 출력물 P1~P7과 실제 부품·배선 더미를 같은 좌표에 만든다.

rev D = 앞면 전체가 7인치 LCD (C)인 직육면체. LCD를 가운데에 두고 테두리를 사방 13.5 mm로 맞췄다
(위·아래는 LCD 모서리 나사 탭 124.27 mm가 정하고, 좌우는 그 폭으로 오른쪽 옆면 단자의 ㄱ자 플러그를 숨긴다).
Jetson은 뒤쪽 문과 한 몸인 트레이(P5)에 얹혀 나사 2개만 풀면 뒤로 빠진다. 다른 부품은 케이스에 남는다.

좌표 (mm): x = 앞에서 봤을 때 왼쪽 → 오른쪽, y = 앞면(0) → 뒤(150), z = 바닥(0) → 위(134).
치수 출처 표시: [사양] 판매처·제조사 자료, [계획] 케이스 설계 위키, [추정] 실물 확인 전 가정값 → 도착 후 실측.
"""
import math
from build123d import Align, Axis, Box, Cylinder, Plane, Polygon, Pos, Rot, Solid, Vector, extrude, fillet

# ------------------------------------------------------------------ 치수 표 (한 곳에서 관리)
LCD = dict(w=164.9, h=106.96, t=8, aa_w=154.21, aa_h=85.92,          # [사양] Waveshare 7inch HDMI LCD (C) Rev4.1 치수도
           tab_h=124.27, hole_dx=156.9, hole_dz=114.96, tab_w=8,     # [사양] 모서리 탭 포함 높이, 탭 구멍 중심 간격
           tab_y=5.5, tab_t=1.2,                                     # [추정] 유리 앞면 → 탭 앞면 거리, 탭 두께
           hdmi_dz=16, usb_dz=37,                                    # [사양 그림에서 잰 값] 위 끝 → HDMI·micro USB 중심 (오른쪽 옆면)
           hdmi_yc=7.5, usb_yc=9.0)                                  # [추정] 단자 중심 깊이 (케이스 y)
BEZEL_GAP = 0.5        # 앞틀 안쪽 ↔ 유리 (얇은 양면 폼 자리)
OVERLAP = 1.0          # 앞틀이 유리 가장자리를 덮는 폭 (표시 영역은 안 가림)
CASE = dict(W=192, D=150, H=134, wall=2.5, plate=3, back=2.5, lip=2.0, corner_r=4)   # rev D 외형 192 × 134 × 150
JET = dict(w=103, d=90.5, h=35, z0=5)                                # [사양] 캐리어 103 × 90.5, [추정] 받침대 포함 높이 35
FAN_AIR = 30                                                         # [계획] 팬 위 공기층
SPK = dict(w=70, d=30.5, h=17)                                       # [사양] Adafruit 3351
RSP = dict(w=86, d=35)                                               # [사양] ReSpeaker Lite 86 × 35, 마이크 위치 [추정]
PCA = dict(w=62.2, d=25.4, holes=(55.9, 19.1))                       # [추정] Adafruit 호환 PCA9685 모듈
SG90 = dict(l=22.5, w=12.2, h=22.7, flange=32.3, above_flange=6.8, shaft_off=5.9)   # [사양] SG90 일반 치수
GPIO = dict(w=66, h=55)                                              # [추정] SMP0080 T형 분배보드
EARS = ((36, 52), (156, 52))                                         # 귀 축 (가운데 96 기준 ±60), y는 윗판 앞쪽
EAR = dict(base=36, height=34, t=9, wall=1.2, tip_r=4, hub_d=30, hub_h=6,   # [계획] 밑변 36 · 높이 34 · 벽 1.2
           stop_r=10.5, stop_arc=45)                                 # 멈춤 나사 반지름, 홈 범위 ±45° → 귀는 약 ±32°에서 멈춤
BEDS = {"X1C": (256, 256, 256), "H2D": (325, 320, 325)}              # [사양] Bambu Lab X1 Carbon / H2D(단일 노즐) 출력 범위
M25 = dict(hole=2.8, nut_af=5.3, nut_t=2.4, head_d=5.2, head_h=1.6)  # M2.5 나일론 나사·너트 (BRT 세트)

W, D, H, T = CASE["W"], CASE["D"], CASE["H"], CASE["plate"]
WL = CASE["wall"]
ZTOP = H - T                       # 윗판 아랫면 131
YBACK = D - CASE["back"]           # 뒤판 안쪽면 147.5
XC, ZC = W / 2, (T + ZTOP) / 2     # LCD 중심 (96, 67)
LX0, LX1 = XC - LCD["w"] / 2, XC + LCD["w"] / 2               # LCD 13.55 ~ 178.45
LZ0, LZ1 = ZC - LCD["h"] / 2, ZC + LCD["h"] / 2               # LCD 13.52 ~ 120.48
LY0 = CASE["lip"] + BEZEL_GAP                                 # 유리 앞면 2.5
LY1 = LY0 + LCD["t"]                                          # LCD 뒷면 10.5
TAB_Y = LY0 + LCD["tab_y"]                                    # 탭 앞면 8.0 = 앞틀 보스 끝
HOLES = [(XC + sx * LCD["hole_dx"] / 2, ZC + sz * LCD["hole_dz"] / 2) for sx in (-1, 1) for sz in (-1, 1)]
HDMI_Z, USB_Z = LZ1 - LCD["hdmi_dz"], LZ1 - LCD["usb_dz"]     # 104.5, 83.5
XIN0, XIN1 = WL, W - WL                                       # 옆벽 안쪽 2.5, 189.5
DOOR = dict(x0=39, x1=153, z1=52)                             # 트레이 문 (뒤판 아래 가운데 홈에 끼움)
DOOR_SCREWS = ((33, 45), (159, 45))                           # 문 → 뒤판 나사 (x, z)
J0 = (XC - JET["w"] / 2, YBACK - 1.5 - JET["d"])              # Jetson 캐리어 왼쪽 앞 모서리 (44.5, 55.5)

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
    """XY 평면 다각형을 z0~z1로 세운 기둥. 꼭짓점 방향과 무관하게 위로 세운다."""
    area2 = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1]))
    pts = pts if area2 > 0 else pts[::-1]
    return Pos(0, 0, z0) * extrude(Polygon(*pts, align=None), amount=z1 - z0)

def prism_xz(pts, y0, y1):
    """XZ 평면 다각형 (x, z)를 y0~y1로 민 기둥 (문 나사 탭의 45° 받침 등)."""
    pl = Plane(origin=(0, y1, 0), x_dir=(1, 0, 0), z_dir=(0, -1, 0))
    return extrude(pl * Polygon(*_ccw(pts), align=None), amount=y1 - y0)

def prism_yz(pts, x0, x1):
    """YZ 평면 다각형 (y, z)를 x0~x1로 민 기둥."""
    pl = Plane(origin=(x0, 0, 0), x_dir=(0, 1, 0), z_dir=(1, 0, 0))
    return extrude(pl * Polygon(*_ccw(pts), align=None), amount=x1 - x0)

def _ccw(pts):
    area2 = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1]))
    return pts if area2 > 0 else pts[::-1]

def slot_x(x0, x1, zc, h, y0, y1):
    """y 방향으로 뚫는 둥근 끝 가로 구멍."""
    r = h / 2
    return box(x0 + r, y0, zc - r, x1 - r, y1, zc + r).fuse(cyl_y(x0 + r, y0, zc, h, y1 - y0), cyl_y(x1 - r, y0, zc, h, y1 - y0))

def slot_z(xc, zc, w, h, y0, y1):
    """y 방향으로 뚫는 둥근 끝 세로 구멍 (폭 w, 높이 h)."""
    r = w / 2
    return box(xc - r, y0, zc - h / 2 + r, xc + r, y1, zc + h / 2 - r).fuse(
        cyl_y(xc, y0, zc - h / 2 + r, w, y1 - y0), cyl_y(xc, y0, zc + h / 2 - r, w, y1 - y0))

def rrect_y(cx, cz, w, h, r, y0, y1):
    """y 방향으로 뚫는 모서리 둥근 직사각형."""
    x0, x1, z0, z1 = cx - w / 2, cx + w / 2, cz - h / 2, cz + h / 2
    return fuse(box(x0 + r, y0, z0, x1 - r, y1, z1), box(x0, y0, z0 + r, x1, y1, z1 - r),
                *[cyl_y(x, y0, z, 2 * r, y1 - y0) for x in (x0 + r, x1 - r) for z in (z0 + r, z1 - r)])

def side_slot(x0, x1, yc, zc, w, h):
    """x 방향(옆벽)으로 뚫는 둥근 끝 세로 구멍."""
    r = w / 2
    return box(x0, yc - r, zc - h / 2 + r, x1, yc + r, zc + h / 2 - r).fuse(
        cyl_x(x0, yc, zc - h / 2 + r, w, x1 - x0), cyl_x(x0, yc, zc + h / 2 - r, w, x1 - x0))

def rounded_block(z0, z1, r=CASE["corner_r"]):
    """외형 블록 W × D, 앞쪽 세로 모서리 2곳만 반지름 r로 둥글게 (OCCT 필렛)."""
    blk = box(0, 0, z0, W, D, z1)
    return fillet(blk.edges().filter_by(Axis.Z).filter_by_position(Axis.Y, -0.1, 0.1), r)

def fuse(*shapes):
    return shapes[0].fuse(*shapes[1:]) if len(shapes) > 1 else shapes[0]

# ------------------------------------------------------------------ 결합 보스 위치 (P1·P3 ↔ P2, P4 ↔ P2)
BOSS_Y_BOTTOM = {"L": (25, 85, 137), "R": (25, 85)}       # 오른쪽 뒤 아래는 DC 단자 자리라 비움
BOSS_Y_TOP = {"L": (25, 85, 137), "R": (25, 85, 137)}
BOSS_Z_BACK = (70, 115)
BX = {"L": (XIN0, XIN0 + 8, XIN0 + 4), "R": (XIN1 - 8, XIN1, XIN1 - 4)}   # 보스 x0, x1, 나사 중심

def _wall_boss(side, y, z0, z1, nut_z):
    """옆벽에 붙은 8 × 8 보스. 앞(-y)면은 앞면을 바닥에 두고 출력할 때를 위해 45° 경사."""
    x0, x1, xc = BX[side]
    wall_x, in_x = (x0, x1) if side == "L" else (x1, x0)
    b = box(x0, y - 4, z0, x1, y + 4, z1).fuse(prism_xy([(wall_x, y - 4), (in_x, y - 4), (wall_x, y - 12)], z0, z1))
    b = b.cut(cyl_z(xc, y, z0 - 0.1, M25["hole"], z1 - z0 + 0.2))
    nx0, nx1 = (xc - M25["nut_af"] / 2, x1 + 0.1) if side == "L" else (x0 - 0.1, xc + M25["nut_af"] / 2)
    return b.cut(box(nx0, y - M25["nut_af"] / 2, nut_z, nx1, y + M25["nut_af"] / 2, nut_z + M25["nut_t"]))

def _back_boss(side, zc):
    x0, x1, xc = BX[side]
    y0, y1 = YBACK - 8, YBACK
    wall_x, in_x = (x0, x1) if side == "L" else (x1, x0)
    b = box(x0, y0, zc - 5, x1, y1, zc + 5).fuse(prism_xy([(wall_x, y0), (in_x, y0), (wall_x, y0 - 8)], zc - 5, zc + 5))
    b = b.cut(cyl_y(xc, y0 - 0.1, zc, M25["hole"], 8.2))
    return b.cut(box(xc - M25["nut_af"] / 2, y1 - 5.5, zc - M25["nut_af"] / 2, xc + M25["nut_af"] / 2, y1 - 5.5 + M25["nut_t"], zc + 5.1))

def screw_points():
    pts = {"P1": [], "P3": [], "P4": []}
    for s, ys in BOSS_Y_BOTTOM.items():
        pts["P1"] += [(BX[s][2], y) for y in ys]
    for s, ys in BOSS_Y_TOP.items():
        pts["P3"] += [(BX[s][2], y) for y in ys]
    pts["P4"] = [(BX[s][2], z) for s in "LR" for z in BOSS_Z_BACK]
    return pts

# ------------------------------------------------------------------ 출력물
def p2_body():
    """몸통: 앞틀(LCD 창·탭 보스) + 좌우 벽(아래 흡기) + 결합 보스. 앞면을 바닥에 두고 분할 없이 출력."""
    z0, z1, lip = T, ZTOP, CASE["lip"]
    shell = rounded_block(z0, z1).cut(box(WL, lip, z0 - 0.1, W - WL, D + 0.1, z1 + 0.1),   # 앞틀 + 좌우 벽
                                      rrect_y(XC, ZC, LCD["w"] - 2 * OVERLAP, LCD["h"] - 2 * OVERLAP, 2, -0.1, lip + 0.1))
    # LCD 모서리 탭 보스: 탭 구멍에 M2.5 나사, 보스 옆 홈에 너트 (탭은 LCD 위·아래 테두리 밖이라 화면을 안 가림)
    tab_bosses = []
    for hx, hz in HOLES:
        b = box(hx - 3.5, lip, hz - 3.5, hx + 3.5, TAB_Y, hz + 3.5).cut(cyl_y(hx, lip + 1.0, hz, M25["hole"], TAB_Y - lip))
        out = -1 if hx < XC else 1
        nx0, nx1 = (hx - 4, hx + M25["nut_af"] / 2) if out < 0 else (hx - M25["nut_af"] / 2, hx + 4)
        tab_bosses.append(b.cut(box(nx0, lip + 1.6, hz - M25["nut_af"] / 2, nx1, lip + 1.6 + M25["nut_t"], hz + M25["nut_af"] / 2)))
    guides = [box(LX0 - 2.55, lip, 40, LX0 - 0.3, LY1, 94),                       # LCD 좌우 위치 맞춤 (단자 높이는 피함)
              box(LX1 + 0.3, lip, 25, LX1 + 2.55, LY1, 60)]
    bosses = [_wall_boss(s, y, z0, z0 + 10, z0 + 4) for s, ys in BOSS_Y_BOTTOM.items() for y in ys]
    bosses += [_wall_boss(s, y, z1 - 10, z1, z1 - 4 - M25["nut_t"]) for s, ys in BOSS_Y_TOP.items() for y in ys]
    bosses += [_back_boss(s, zc) for s in "LR" for zc in BOSS_Z_BACK]
    body = fuse(shell, *tab_bosses, *guides, *bosses)
    # 옆면 아래 흡기 (보스·분배보드 지지대 자리를 피함)
    avoid = {"L": BOSS_Y_BOTTOM["L"], "R": BOSS_Y_BOTTOM["R"] + (120, 128, 136, 144)}
    cuts = []
    for s, (xa, xb) in (("L", (-0.1, WL + 0.1)), ("R", (W - WL - 0.1, W + 0.1))):
        for yc in range(45, 141, 5):
            if all(abs(yc - a) > 7 for a in avoid[s]):
                cuts.append(side_slot(xa, xb, yc, 10, 2.4, 10))
    cuts += [cyl_x(-0.1, y, z, M25["hole"], WL + 0.2) for y in (63, 123) for z in (19, 68)]   # 분배보드 지지대 나사
    return body.cut(*cuts)

def _plate_screw_cuts(points, z_face, outward):
    cuts = []
    for x, y in points:
        cuts.append(cyl_z(x, y, -1 if outward < 0 else z_face - T - 0.1, M25["hole"], T + 1.2))
        hz = z_face - 0.1 if outward > 0 else -0.1
        cuts.append(cyl_z(x, y, hz - (M25["head_h"] if outward > 0 else 0), M25["head_d"], M25["head_h"] + 0.1))
    return cuts

def p1_base():
    """바닥판: 스피커 창, 트레이 레일, 흡기, DC 단자 받침, 퓨즈 홀더 타이 구멍, 범퍼 자리."""
    p = rounded_block(0, T)
    cuts = [box(66, 22.5 + i * 4, -0.1, 126, 24.5 + i * 4, T + 0.1) for i in range(7)]      # 스피커 창 (스피커가 아래를 봄)
    cuts += [box(58, 104 + i * 6, -0.1, 134, 107 + i * 6, T + 0.1) for i in range(6)]        # 흡기 (트레이 바닥 구멍과 겹침)
    cuts += [box(x, y, -0.1, x + 3, y + 6, T + 0.1) for x in (160, 180.5) for y in (92, 106)]  # 퓨즈 홀더 케이블 타이
    cuts += _plate_screw_cuts(screw_points()["P1"], T, -1)
    adds = [box(39, 53, T, 41, 147, T + 6), box(151, 53, T, 153, 147, T + 6)]               # 트레이 레일
    adds += [box(169, 118, T, 171, 145, T + 7), box(185, 118, T, 187, 145, T + 7)]           # DC 클립 터미널 받침
    adds += [box(56, 17.5, T, 58, 52, T + 3), box(134, 17.5, T, 136, 52, T + 3)]             # P7 위치 맞춤 턱
    p = fuse(p, *adds).cut(*cuts)
    return p.cut(*[cyl_z(x, y, -0.1, 11.6, 0.7) for x in (15.5, W - 15.5) for y in (15.5, D - 15.5)])   # 범퍼 자리 (깊이 0.6)

def p3_top():
    """윗판: 귀 서보 축 구멍·보스, 귀 멈춤 나사 구멍, 마이크 구멍, ReSpeaker 레일, PCA9685 지지대 구멍."""
    p = rounded_block(ZTOP, H)
    cuts = [cyl_z(x, y, ZTOP - 0.1, 12.5, T + 0.2) for x, y in EARS]
    cuts += [cyl_z(x + side * EAR["stop_r"], y, ZTOP - 0.1, M25["hole"], T + 0.2) for (x, y), side in zip(EARS, (1, -1))]
    for mx in (61, 131):                                                                      # 마이크 위 구멍 7개씩 [추정 위치]
        cuts += [cyl_z(mx + dx, 39.5 + dy, ZTOP - 0.1, 1.6, T + 0.2)
                 for dx, dy in ((0, 0), (2.6, 0), (-2.6, 0), (1.3, 2.25), (-1.3, 2.25), (1.3, -2.25), (-1.3, -2.25))]
    hx, hy = PCA["holes"]
    cuts += [cyl_z(XC + sx * hx / 2, 109 + sy * hy / 2, ZTOP - 0.1, M25["hole"], T + 0.2) for sx in (-1, 1) for sy in (-1, 1)]
    cuts += _plate_screw_cuts(screw_points()["P3"], H, 1)
    adds = []
    for x, y in EARS:                                                                         # SG90 날개 나사 보스 (축이 앞쪽)
        cy = y - SG90["shaft_off"] + SG90["l"] / 2
        for yy in (cy - 13.75, cy + 13.75):
            adds.append(cyl_z(x, yy, ZTOP - SG90["above_flange"], 4.4, SG90["above_flange"]).cut(
                cyl_z(x, yy, ZTOP - SG90["above_flange"] - 0.1, 1.8, 5)))
    rz = ZTOP - 6.6                                                                           # ReSpeaker 레일: 기판 아랫면에 받침
    for ya, yb, lip0, lip1 in ((19.5, 21.5, 19.5, 24), (57.5, 59.5, 55, 59.5)):
        adds += [box(53, ya, rz, 139, yb, ZTOP), box(53, lip0, rz, 139, lip1, rz + 2)]
    adds.append(box(51, 19.5, rz, 53, 59.5, ZTOP))                                            # 끝막이 (반대쪽에서 밀어 넣음)
    return fuse(p, *adds).cut(*cuts)

def p4_back():
    """뒤판: 아래 가운데는 트레이 문 자리(홈), HDMI 패널 단자, USB 케이블 통과 구멍, DC 단자, 배기, 문 나사 보스."""
    p = box(XIN0, YBACK, T, XIN1, D, ZTOP)
    cuts = [box(DOOR["x0"] - 0.4, YBACK - 0.1, T - 0.1, DOOR["x1"] + 0.4, D + 0.1, DOOR["z1"] + 0.4)]   # 트레이 문 홈
    cuts.append(box(162, YBACK - 0.1, 66, 176, D + 0.1, 74))                                  # HDMI 암 단자 [추정 크기]
    cuts += [cyl_y(x, YBACK - 0.1, 70, M25["hole"], CASE["back"] + 0.2) for x in (160, 178)]  # 단자 보드 고정
    cuts.append(slot_x(165, 187, 37, 14, YBACK - 0.1, D + 0.1))                               # LCD·ReSpeaker USB 케이블 통과
    cuts.append(cyl_y(178, YBACK - 0.1, 9.5, 11, CASE["back"] + 0.2))                         # DC 클립 터미널
    cuts += [slot_x(30, 158, 88 + i * 7, 3.5, YBACK - 0.1, D + 0.1) for i in range(6)]       # 배기 (위쪽)
    cuts += [cyl_y(x, YBACK - 0.1, z, M25["hole"], CASE["back"] + 0.2) for x, z in screw_points()["P4"]]
    adds = []
    for x, z in DOOR_SCREWS:                                                                  # 문 나사 너트 보스 (안쪽)
        b = box(x - 4, YBACK - 8, z - 4, x + 4, YBACK, z + 4).cut(cyl_y(x, YBACK - 8.1, z, M25["hole"], 8.2))
        adds.append(b.cut(box(x - M25["nut_af"] / 2, YBACK - 5.5, z - M25["nut_af"] / 2, x + M25["nut_af"] / 2,
                              YBACK - 5.5 + M25["nut_t"], z + 4.1)))
    p = fuse(p, *adds).cut(*cuts)
    return p.cut(*[cyl_y(x, YBACK - 8.1, z, M25["hole"], 10.8) for x, z in DOOR_SCREWS])

def p5_tray():
    """Jetson 트레이 + 뒤 문: 나사 없이 키트 받침대째 끼움. 문 나사 2개만 풀면 뒤로 빠진다."""
    x0, x1, yd = 41.5, 150.5, YBACK - 0.1
    t = fuse(box(x0, 53, T, x1, yd, T + 2), box(x0, 53, T + 2, x0 + 2, yd, T + 8),
             box(x1 - 2, 53, T + 2, x1, yd, T + 8), box(x0 + 2, 53, T + 2, x1 - 2, 55, T + 8))
    t = t.cut(*[box(58, 104 + i * 6, T - 0.1, 134, 107 + i * 6, T + 2.1) for i in range(6)])
    door = box(DOOR["x0"], yd, T + 0.3, DOOR["x1"], D + 1, DOOR["z1"]).cut(
        box(J0[0] - 0.5, yd - 0.1, 9, J0[0] + JET["w"] + 0.5, D + 1.1, 31))                   # Jetson 단자 창 104 × 22
    x0d, x1d = DOOR["x0"], DOOR["x1"]                                                         # 나사 탭 (뒤판 바깥면에 닿음, 아래 45°)
    tabs = [prism_xz([(x0d + 4, 30), (x0d, 30), (28, 41), (28, 50), (x0d + 4, 50)], D, D + 2.5),
            prism_xz([(x1d - 4, 30), (x1d, 30), (164, 41), (164, 50), (x1d - 4, 50)], D, D + 2.5)]
    handle = prism_yz([(D, 35), (D + 9, 44), (D + 9, 50), (D, 50)], 76, 116)                 # 손잡이 (아래 45°, 서포트 없이 출력)
    door = fuse(door, *tabs, handle).cut(*[cyl_y(x, D - 0.1, z, M25["hole"], 2.7) for x, z in DOOR_SCREWS])
    return t.fuse(door)

def _ear_profile(inset, base_z):
    """귀 옆모습(XZ): 밑변이 base_z, 끝이 둥근 삼각형. inset만큼 안쪽으로 줄인 윤곽."""
    b2, r = EAR["base"] / 2, EAR["tip_r"]
    h0 = EAR["height"]
    for _ in range(30):                                  # 끝을 반지름 r로 둥글린 뒤 높이가 EAR["height"]가 되는 꼭짓점 높이 h0
        h0 = EAR["height"] + r / math.sin(math.atan2(b2, h0)) - r
    alpha = math.atan2(b2, h0)
    cz = h0 - r / math.sin(alpha)                        # 둥근 끝 중심 높이
    L = math.hypot(b2, h0)
    nx, nz = h0 / L, b2 / L                              # 오른쪽 변 바깥 법선
    c_line = nx * b2 - inset                             # 안쪽으로 inset 옮긴 변: nx*x + nz*z = c_line
    zb = inset                                           # 밑변도 inset만큼 올림
    xb = (c_line - nz * zb) / nx
    th0 = math.atan2(nz, nx)
    rr = r - inset
    pts = [(-xb, zb), (xb, zb)]
    pts += [(rr * math.cos(th0 + (math.pi - 2 * th0) * k / 12), cz + rr * math.sin(th0 + (math.pi - 2 * th0) * k / 12)) for k in range(13)]
    return [(x, z + base_z) for x, z in pts]

def _xz_solid(pts, y0, y1):
    return prism_xz(pts, y0, y1)

def p6_ear(side, insert=False):
    """귀 1개. 속 빈 둥근 삼각형 + 서보 혼 허브 + 아래 멈춤 홈. 축 중심이 원점, 허브 바닥이 z=0.
    side = +1(왼쪽 귀, 멈춤 홈이 +x) / -1(오른쪽 귀). insert=True면 앞면 안쪽 귀 장식(2색 출력용)만 돌려준다."""
    t, wl, hh = EAR["t"], EAR["wall"], EAR["hub_h"]
    if insert:
        inner = _ear_profile(3.5, hh)
        return _xz_solid(inner, -t / 2 - 0.8, -t / 2)
    shell = _xz_solid(_ear_profile(0, hh), -t / 2, t / 2).cut(_xz_solid(_ear_profile(wl, hh), -t / 2 + wl, t / 2 - wl))
    top = max(z for _, z in _ear_profile(0, hh))
    shell = shell.cut(cyl_z(0, 0, top - 6, 4, 8))                                            # 혼 가운데 나사용 드라이버 구멍
    hub = cyl_z(0, 0, 0, EAR["hub_d"], hh)
    a = math.radians(EAR["stop_arc"])
    r0, r1 = EAR["stop_r"] - 3, EAR["stop_r"] + 3
    arc = [-a + 2 * a * k / 12 for k in range(13)]                                         # 멈춤 홈: 부채꼴 고리
    groove = prism_xy([(side * r1 * math.cos(q), r1 * math.sin(q)) for q in arc] +
                      [(side * r0 * math.cos(q), r0 * math.sin(q)) for q in arc[::-1]], -0.1, 2.2)
    hub = hub.cut(cyl_z(0, 0, -0.1, 12.6, 1.4),                                              # SG90 기어 탑
                  cyl_z(0, 0, -0.1, 7.4, 5.2),                                               # 혼 가운데 (스플라인)
                  box(-3.2, -13, -0.1, 3.2, 13, 4.2),                                          # 한쪽 혼 팔 (앞뒤 방향, 팔을 13 mm로 잘라 끼움)
                  cyl_z(0, 0, 0, 2.4, hh + 0.1),                                             # 혼 가운데 나사
                  groove)
    return shell.fuse(hub)

def p7_speaker_cradle():
    return box(59, 17.5, T, 133, 52, T + 6).cut(box(61, 19.5, T - 0.1, 131, 50, T + 6.1)).fuse(
        box(59, 17.5, T, 133, 52, T + 0.8).cut(box(64, 22.5, T - 0.1, 128, 47, T + 1)))      # 가장자리 EVA 자리

EAR_Z = H + 0.5            # 귀 허브 바닥 (윗판 위 0.5 띄움)

def printed_parts():
    ears = [Pos(x, y, EAR_Z) * p6_ear(s) for (x, y), s in zip(EARS, (1, -1))]
    return {"P1 바닥판": p1_base(), "P2 몸통": p2_body(), "P3 윗판": p3_top(), "P4 뒤판": p4_back(),
            "P5 트레이": p5_tray(), "P6 왼쪽 귀": ears[0], "P6 오른쪽 귀": ears[1], "P7 스피커 받침": p7_speaker_cradle()}

def ear_inserts():
    """2색 출력(H2D)용 안쪽 귀 장식. 1색이면 출력하지 않는다."""
    return {f"P6 {n} 귀 안쪽": Pos(x, y, EAR_Z) * p6_ear(1, insert=True) for (x, y), n in zip(EARS, ("왼쪽", "오른쪽"))}

# 출력 방향: 좌표계 그대로의 모양을 프린터 바닥에 놓는 회전 (STL·3MF 내보낼 때 사용)
PRINT_ROT = {"P1": Rot(0, 0, 0), "P2": Rot(90, 0, 0), "P3": Rot(180, 0, 0), "P4": Rot(-90, 0, 0),
             "P5": Rot(0, 0, 0), "P6": Rot(0, 0, 0), "P7": Rot(0, 0, 0)}

# ------------------------------------------------------------------ 구매 부품 더미 (충돌·배치 확인용)
def sg90(x, y):
    """축이 (x, y), 몸통은 뒤(+y)로. 몸통 윗면이 윗판 아랫면에 닿음."""
    y0, zt = y - SG90["shaft_off"], ZTOP
    body = box(x - SG90["w"] / 2, y0, zt - SG90["h"], x + SG90["w"] / 2, y0 + SG90["l"], zt)
    cy, fz = y0 + SG90["l"] / 2, zt - SG90["above_flange"]
    flange = box(x - SG90["w"] / 2, cy - SG90["flange"] / 2, fz - 2.5, x + SG90["w"] / 2, cy + SG90["flange"] / 2, fz)
    flange = flange.cut(*[cyl_z(x, cy + s * 13.75, fz - 3, 2.4, 4) for s in (-1, 1)])
    return fuse(body, flange, cyl_z(x, y, zt, 11.6, 4), cyl_z(x, y, zt + 4, 4.8, 3))

def lcd():
    body = box(LX0, LY0, LZ0, LX1, LY1, LZ1)
    tabs = [box(x0, TAB_Y, z0, x0 + LCD["tab_w"], TAB_Y + LCD["tab_t"], z0 + (LCD["tab_h"] - LCD["h"]) / 2)
            for x0 in (LX0, LX1 - LCD["tab_w"]) for z0 in (ZC - LCD["tab_h"] / 2, LZ1)]
    tabs = [t.cut(*[cyl_y(hx, TAB_Y - 0.1, hz, 3.2, 2) for hx, hz in HOLES]) for t in tabs]
    return fuse(body, *tabs)

def components():
    jx, jy = J0
    jx1, jy1, jz = jx + JET["w"], jy + JET["d"], JET["z0"]
    hx, hy = PCA["holes"]
    parts = {
        "Jetson Orin Nano (받침대 포함)": fuse(
            box(jx, jy, jz, jx1, jy1, jz + 5), box(jx, jy, jz + 5, jx1, jy1, jz + 6.6),
            box(jx + 1.5, 128, jz + 6.6, jx + 101.5, jy1, jz + 22.6),                         # 뒤쪽 단자 줄 [추정]
            box(jx + 18.5, 68, jz + 6.6, jx + 88.5, 126, jz + JET["h"]),                      # 방열판·팬
            box(jx + 9.5, 58, jz + 6.6, jx + 60.5, 63, jz + 15.1)),                           # 40핀 헤더 (보드 앞쪽)
        "7인치 LCD (C)": lcd(),
        "HDMI FPC ㄱ자 플러그": fuse(box(LX1, 4.5, HDMI_Z - 8, 187.0, 12.5, HDMI_Z + 8),
                                 box(181, 12.5, HDMI_Z - 7, 187.0, 22, HDMI_Z + 7)),
        "micro USB ㄱ자 플러그": fuse(box(LX1, 6, USB_Z - 4, 185.5, 12, USB_Z + 4), box(180, 12, USB_Z - 3, 185.5, 20, USB_Z + 3)),
        "HDMI FPC 암 단자 보드": fuse(box(158, 141, 63, 180, 147.5, 77), box(162.3, 147.5, 66.3, 175.7, 150, 73.7)),
        "ReSpeaker Lite": fuse(box(53, 22, ZTOP - 4.6, 139, 57, ZTOP - 3), box(56, 25, ZTOP - 11, 136, 54, ZTOP - 4.6),
                               box(135, 34, ZTOP - 9, 140.5, 43, ZTOP - 4.6), box(53.5, 28, ZTOP - 9, 59, 36, ZTOP - 4.6)),
        "Adafruit 3351 스피커": box(61, 19.5, T + 3, 61 + SPK["w"], 19.5 + SPK["d"], T + 3 + SPK["h"]),
        "PCA9685 + 지지대": fuse(box(XC - PCA["w"] / 2, 96.3, ZTOP - 7.6, XC + PCA["w"] / 2, 96.3 + PCA["d"], ZTOP - 6),
                               box(66, 97, ZTOP - 19, 126, 121, ZTOP - 7.6),
                               *[cyl_z(XC + sx * hx / 2, 109 + sy * hy / 2, ZTOP - 6, 4.6, 6) for sx in (-1, 1) for sy in (-1, 1)]),
        "SG90 왼쪽 귀": sg90(*EARS[0]),
        "SG90 오른쪽 귀": sg90(*EARS[1]),
        "귀 멈춤 나사 M2.5 ×2": fuse(*[fuse(cyl_z(x + s * EAR["stop_r"], y, H, 4.6, 1.7), cyl_z(x + s * EAR["stop_r"], y, ZTOP - 3.5, 2.5, H - ZTOP + 3.5),
                                            cyl_z(x + s * EAR["stop_r"], y, ZTOP - 2.4, 5.8, 2.4))
                                       for (x, y), s in zip(EARS, (1, -1))]),
        "GPIO 분배보드 SMP0080 + 지지대": fuse(
            box(XIN0 + 6, 60, 16, XIN0 + 7.6, 60 + GPIO["w"], 16 + GPIO["h"]), box(XIN0 + 7.6, 66, 57, XIN0 + 16.5, 124, 66),
            box(XIN0 + 7.6, 68, 22, XIN0 + 16, 122, 27), *[cyl_x(XIN0, y, z, 4.6, 6) for y in (63, 123) for z in (19, 68)]),
        "DC 클립 터미널 TC5521F": box(171, 115.5, T, 185, YBACK, T + 12),
        "퓨즈 홀더 BE151": box(164, 75, T, 180, 115, T + 12),
        "실리콘 범퍼 ×4": fuse(*[cyl_z(x, y, -5, 11, 5) for x in (15.5, W - 15.5) for y in (15.5, D - 15.5)]),
    }
    return parts

# ------------------------------------------------------------------ 배선 (중심선 + 굵기). 끝점은 단자 위치
JP = J0[1] + JET["d"] + 0.5        # Jetson 단자 끝 바로 뒤 y 146.5 (케이블은 문 창으로 들어감)
CABLES = {
    "HDMI FPC (LCD → 뒤판 HDMI 단자)": (1.2, [(184, 22, HDMI_Z), (187, 35, HDMI_Z), (187.2, 110, 100), (186.5, 126, 86),
                                            (176, 136, 71), (170, 141, 70)]),
    "LCD micro USB": (2.0, [(183, 20, USB_Z), (184, 30, 82), (184.5, 100, 75), (181, 128, 52), (176, 140, 40),
                            (176, 152, 37), (150, 160, 25), (125, 156, 20), (120, JP, 19.5)]),
    "ReSpeaker USB": (2.0, [(140.5, 38.5, ZTOP - 6.8), (146, 36, 118), (170, 34, 112), (184, 50, 108), (185, 100, 90),
                            (182, 128, 58), (178, 140, 40), (178, 153, 37), (140, 162, 24), (110, 156, 20), (105, JP, 19.5)]),
    "HDMI (DP 젠더 → 뒤판 단자, 밖)": (3.5, [(169, 156, 70), (169, 166, 60), (120, 170, 32), (85, 160, 20), (80, JP + 2, 19.5)]),
    "스피커 JST": (1.5, [(61, 34.5, 14), (50, 36, 30), (50, 32, 112), (56, 32, ZTOP - 7)]),
    "40핀 리본": (3.0, [(J0[0] + 35, 60.5, 21), (J0[0] + 35, 58, 35), (33, 62, 45), (22.5, 80, 61.5), (XIN0 + 16.5, 95, 61.5)]),
    "신호 점퍼 4가닥 + OE": (2.0, [(XIN0 + 16.5, 95, 24.5), (28, 100, 35), (28, 108, 100), (55, 110, 115), (67, 109, 117)]),
    "왼쪽 귀 서보선": (1.6, [(36, 68.6, 112), (40, 90, 112), (69, 98, 116)]),
    "오른쪽 귀 서보선": (1.6, [(156, 68.6, 112), (152, 90, 112), (124, 98, 116)]),
    "서보 전원 18AWG (단자 → 퓨즈)": (2.2, [(178, 118, 15), (172, 116, 13)]),
    "서보 전원 18AWG (퓨즈 → PCA9685)": (2.2, [(172, 75, 9), (172, 70, 20), (166, 85, 80), (144, 105, 112), (128, 112, 116)]),
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
    jx, jz = J0[0], JET["z0"] + JET["h"]
    return {"Jetson 팬 위 공기층 30": box(jx + 18.5, 68, jz, jx + 88.5, 126, jz + FAN_AIR)}

def tray_sweep():
    """트레이(문 포함)와 Jetson을 뒤로 끝까지 뺄 때 지나가는 공간."""
    return fuse(box(41.5, 53, T + 0.05, 150.5, D + 120, JET["z0"] + JET["h"] + 0.5),
                box(DOOR["x0"], YBACK - 0.1, T + 0.35, DOOR["x1"], D + 120, DOOR["z1"]))

COLORS = {
    "P1": (0.93, 0.91, 0.86), "P2": (0.96, 0.94, 0.90), "P3": (0.93, 0.91, 0.86), "P4": (0.88, 0.86, 0.82),
    "P5": (0.70, 0.78, 0.86), "P6": (0.95, 0.78, 0.55), "P7": (0.62, 0.78, 0.66),
}
