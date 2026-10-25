"""DeskPet KiCad 7 schematic generator.

모든 연결은 NETS 표 한 곳에서 정의하고, 같은 표로 회로도와 검증 기대값을 만든다.
좌표 단위: 2.54 mm 격자(G). 모든 심볼은 회전 없이 배치한다.
"""
import sys, os, uuid, json
sys.path.insert(0, os.path.dirname(__file__))
from sexp import extract, pins as lib_pins

G = 2.54
LIB = "/usr/share/kicad/symbols/"
OUT = sys.argv[1] if len(sys.argv) > 1 else "out"
os.makedirs(OUT, exist_ok=True)
KR = '(font (face "Noto Sans CJK KR") (size {s} {s}))'
U = lambda: str(uuid.uuid4())
ROOT = U()

def q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

# ------------------------------------------------------------------ 라이브러리 심볼
LIBSYMS = {}      # lib_id -> sexpr text
PINS = {}         # lib_id -> {pin_number: dict}
BOXTOP = {}       # custom lib_id -> 상자 윗변(심볼 좌표, mm)

def use_lib(lib, name):
    lid = f"{lib}:{name}"
    if lid in LIBSYMS:
        return lid
    t = open(LIB + lib + ".kicad_sym", encoding="utf-8").read()
    b = extract(t, name)
    b = b.replace(f'(symbol "{name}"', f'(symbol "{lid}"', 1)
    LIBSYMS[lid] = b
    PINS[lid] = {p["num"]: p for p in lib_pins(b)}
    return lid

def custom(name, left, right, w=12, title=None, notes=()):
    """left/right: list of (num, name, type) or None(빈 칸). w: 상자 폭(G)."""
    lid = f"DeskPet:{name}"
    rows = max(len(left), len(right))
    top = (rows - 1) / 2.0  # 첫 핀 y (G, 위쪽 +)
    pins, P = [], {}
    def add(lst, side):
        for i, p in enumerate(lst):
            if p is None:
                continue
            num, nm, typ = p
            y = (top - i) * G
            x = (-(w / 2) - 1) * G if side == "L" else ((w / 2) + 1) * G
            ang = 0 if side == "L" else 180
            pins.append(f'(pin {typ} line (at {x:.2f} {y:.2f} {ang}) (length 2.54) '
                        f'(name {q(nm)} (effects (font (size 1.27 1.27)))) (number {q(num)} (effects (font (size 1.27 1.27)))))')
            P[num] = dict(num=num, name=nm, type=typ, x=x, y=y, ang=ang)
    add(left, "L"); add(right, "R")
    y1, y2 = (top + 1) * G, (top - rows) * G
    x1, x2 = -(w / 2) * G, (w / 2) * G
    body = [f'(rectangle (start {x1:.2f} {y1:.2f}) (end {x2:.2f} {y2:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))']
    for k, (txt, ty) in enumerate(notes):
        body.append(f'(text {q(txt)} (at 0 {ty * G:.2f} 0) (effects {KR.format(s=1.1)}))')
    s = (f'(symbol {q(lid)} (in_bom yes) (on_board yes)\n'
         f'  (property "Reference" "U" (at 0 {(top + 2.2) * G:.2f} 0) (effects (font (size 1.27 1.27))))\n'
         f'  (property "Value" {q(title or name)} (at 0 {(top + 1.5) * G:.2f} 0) (effects {KR.format(s=1.27)}))\n'
         f'  (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n'
         f'  (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n'
         f'  (symbol "{name}_0_1" {" ".join(body)})\n'
         f'  (symbol "{name}_1_1" {" ".join(pins)})\n)')
    LIBSYMS[lid] = s
    PINS[lid] = P
    BOXTOP[lid] = y1
    return lid

# ------------------------------------------------------------------ 부품 정의
def S(p): return p  # 가독성용
PWRIN, PWROUT, PAS, IN, OUT_, BI = "power_in", "power_out", "passive", "input", "output", "bidirectional"

L_JET = custom("Jetson_OrinNano_DevKit", [("1", "DC_IN", PWRIN)],
               [("2", "DP", PAS), ("3", "USB_A1", PAS), ("4", "USB_A2", PAS), ("5", "USB_A3", PAS), ("6", "USB_A4", PAS)],
               w=14, title="Jetson Orin Nano 8GB 개발 키트 (대여)",
               notes=[("40핀 헤더 = J1", -1.0), ("(리본 1:1 → 분배보드)", -2.0)])
L_PS1 = custom("PSU_Jetson", [], [("1", "DC_OUT", PWROUT)], w=10, title="Jetson 전원 어댑터 (키트 동봉)")
L_ADP = custom("DP_HDMI_Adapter", [("1", "DP", PAS)], [("2", "HDMI", PAS)], w=10, title="DP→HDMI 젠더 NXC067")
L_LCD = custom("Elecrow_RC070S_7in", [("1", "HDMI_IN", PAS), ("2", "microUSB_PWR", PAS)], [],
               w=14, title="7인치 LCD RC070S (1024×600)", notes=[("micro USB = 전원+터치(터치 미사용)", -2.6)])
L_MIC = custom("ReSpeaker_Lite", [("1", "USB-C", PAS)], [("2", "SPK_1", PAS), ("3", "SPK_2", PAS), ("4", "3.5mm_JACK", PAS)],
               w=14, title="ReSpeaker Lite (USB 펌웨어)", notes=[("스피커: JST-PH 2핀, 내장 앰프", -2.6)])
pca_left = [("1", "GND", PWRIN), ("2", "OE", IN), ("3", "SCL", BI), ("4", "SDA", BI), ("5", "VCC", PWRIN), ("6", "V+", PAS)]
pca_right = [("7", "GND", PWRIN), ("8", "OE", IN), ("9", "SCL", BI), ("10", "SDA", BI), ("11", "VCC", PWRIN), ("12", "V+", PAS),
             None, ("13", "TERM_V+", PAS), ("14", "TERM_GND", PWRIN),
             None, ("15", "CH0_PWM", OUT_), ("16", "CH0_V+", PAS), ("17", "CH0_GND", PWRIN),
             None, ("18", "CH1_PWM", OUT_), ("19", "CH1_V+", PAS), ("20", "CH1_GND", PWRIN)]
L_PCA = custom("PCA9685_Module", pca_left, pca_right, w=20, title="PCA9685 서보 드라이버 모듈 (VLT-MD032)",
               notes=[("← 왼쪽 헤더   오른쪽 헤더 →", 5.5), ("단자대 →", 0.5),
                      ("CH0: 왼쪽 귀 →", -3.0), ("CH1: 오른쪽 귀 →", -7.0),
                      ("같은 이름 핀 = 내부 연결", -8.4)])
L_PS2 = custom("PSU_5V5A", [], [("1", "+5V", PWROUT), ("2", "GND", PWROUT)], w=10, title="5V 5A 어댑터 (서보 전용)")
L_JACK = custom("DC_Jack_Terminal", [("1", "IN(+)", PAS), ("2", "IN(-)", PAS)], [("3", "+", PAS), ("4", "-", PAS)],
                w=10, title="DC잭 터미널 VLT-DC001", notes=[("플러그 → 나사 단자", -1.6)])
L_HDR = use_lib("Connector_Generic", "Conn_02x20_Odd_Even")
L_SERVO = use_lib("Motor", "Motor_Servo")
L_FUSE = use_lib("Device", "Fuse")
L_CAP = use_lib("Device", "C_Polarized")
L_SPK = use_lib("Device", "Speaker")
L_JP = use_lib("Connector_Generic", "Conn_01x02")
L_FLAG = use_lib("power", "PWR_FLAG")

# ref, lib_id, value, (gx, gy), footprint-less
PARTS = {
    "PS1": (L_PS1, "Jetson 전원 어댑터(키트)", (10, 24)),
    "U1": (L_JET, "Jetson Orin Nano 8GB 개발 키트", (34, 24)),
    "J1": (L_HDR, "Jetson 40핀 = 분배보드(리본 1:1)", (26, 62)),
    "A1": (L_ADP, "DP→HDMI 젠더 NXC067", (64, 14)),
    "U2": (L_LCD, "Elecrow RC070S 7in 1024x600", (90, 16)),
    "U3": (L_MIC, "ReSpeaker Lite 107990273", (90, 34)),
    "LS1": (L_SPK, "밀폐형 스피커 3W 4Ω ada-3351", (110, 54)),
    "U4": (L_PCA, "PCA9685 모듈 VLT-MD032", (78, 66)),
    "JP1": (L_JP, "OE↔GND 점퍼선", (106, 58)),
    "M1": (L_SERVO, "SG90 왼쪽 귀", (110, 72)),
    "M2": (L_SERVO, "SG90 오른쪽 귀", (110, 80)),
    "PS2": (L_PS2, "5V 5A 어댑터(서보 전용)", (14, 92)),
    "J3": (L_JACK, "DC잭 터미널 VLT-DC001", (38, 92)),
    "F1": (L_FUSE, "MINI 퓨즈 3A + 홀더 BE151", (58, 92)),
    "C1": (L_CAP, "1000µF 16V 전해", (76, 92)),
    "#FLG01": (L_FLAG, "PWR_FLAG", (12, 52)),
    "#FLG02": (L_FLAG, "PWR_FLAG", (86, 86)),
}

# 넷 표 — 유일한 연결 정의 (ref, pin)
NETS = {
    "+3V3": [("J1", "1"), ("U4", "5"), ("#FLG01", "1")],
    "I2C_SDA": [("J1", "3"), ("U4", "4")],
    "I2C_SCL": [("J1", "5"), ("U4", "3")],
    "GND": [("J1", "6"), ("U4", "1"), ("U4", "7"), ("U4", "14"), ("U4", "17"), ("U4", "20"),
            ("JP1", "2"), ("M1", "3"), ("M2", "3"), ("C1", "2"), ("J3", "4"), ("J3", "2"), ("PS2", "2")],
    "PCA_OE": [("U4", "8"), ("JP1", "1")],
    "SERVO_L_PWM": [("U4", "15"), ("M1", "1")],
    "SERVO_R_PWM": [("U4", "18"), ("M2", "1")],
    "+5V_RAW": [("PS2", "1"), ("J3", "1"), ("J3", "3"), ("F1", "1")],
    "+5V_SERVO": [("F1", "2"), ("C1", "1"), ("U4", "13"), ("U4", "16"), ("U4", "19"), ("M1", "2"), ("M2", "2"), ("#FLG02", "1")],
    "DC_JETSON": [("PS1", "1"), ("U1", "1")],
    "DP_LINK": [("U1", "2"), ("A1", "1")],
    "HDMI_LINK": [("A1", "2"), ("U2", "1")],
    "USB_LCD_PWR": [("U1", "3"), ("U2", "2")],
    "USB_RESPEAKER": [("U1", "4"), ("U3", "1")],
    "SPK_1": [("U3", "2"), ("LS1", "1")],
    "SPK_2": [("U3", "3"), ("LS1", "2")],
}
MUT = os.environ.get("MUTATION", "")
if MUT == "swap_i2c":
    NETS["I2C_SDA"] = [("J1", "3"), ("U4", "3")]; NETS["I2C_SCL"] = [("J1", "5"), ("U4", "4")]
elif MUT == "jetson5v_to_servo":
    NETS["+5V_SERVO"].append(("J1", "2"))
elif MUT == "cap_reversed":
    NETS["+5V_SERVO"][1] = ("C1", "2"); NETS["GND"][9] = ("C1", "1")
elif MUT == "fuse_bypassed":
    NETS["+5V_RAW"].append(("U4", "13")); NETS["+5V_SERVO"].remove(("U4", "13"))
elif MUT == "header_vplus_used":
    NETS["+5V_SERVO"].append(("U4", "12"))
elif MUT == "no_common_gnd":
    NETS["GND"].remove(("J1", "6")); NETS["JETSON_GND_ONLY"] = [("J1", "6")]
# 의도적으로 비워 두는 핀 (No Connect)
NC = {("U4", "2"), ("U4", "6"), ("U4", "9"), ("U4", "10"), ("U4", "11"), ("U4", "12"),
      ("U1", "5"), ("U1", "6"), ("U3", "4")}
NC |= {("J1", str(n)) for n in range(1, 41) if str(n) not in {"1", "3", "5", "6"}}

# ------------------------------------------------------------------ 배치·배선 생성
items = []
def sym_instance(ref, lid, value, gx, gy):
    x, y = gx * G, gy * G
    pinlines = " ".join(f'(pin {q(n)} (uuid {U()}))' for n in PINS[lid])
    virt = ref.startswith("#")
    if lid in BOXTOP:                       # 직접 만든 상자: 위쪽에 부품 이름
        top = y - BOXTOP[lid]
        rx, ry, vx, vy, just = x, top - 3.6, x, top - 1.2, ""
    else:                                   # 라이브러리 심볼: 오른쪽에 이름
        rx, ry, vx, vy, just = x + 3.2 * G, y - 1.0, x + 3.2 * G, y + 1.6, " (justify left)"
    items.append(
        f'(symbol (lib_id {q(lid)}) (at {x:.2f} {y:.2f} 0) (unit 1) (in_bom {"no" if virt else "yes"}) (on_board yes) (dnp no) (uuid {U()})\n'
        f'  (property "Reference" {q(ref)} (at {rx:.2f} {ry:.2f} 0) (effects (font (size 1.4 1.4) bold){just}{" hide" if virt else ""}))\n'
        f'  (property "Value" {q(value)} (at {vx:.2f} {vy:.2f} 0) (effects {KR.format(s=1.3)}{just}{" hide" if virt else ""}))\n'
        f'  (property "Footprint" "" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) hide))\n'
        f'  (property "Datasheet" "" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) hide))\n'
        f'  {pinlines}\n'
        f'  (instances (project "deskpet" (path "/{ROOT}" (reference {q(ref)}) (unit 1)))))')

PINS_TOP = {}
def pin_point(ref, num):
    lid, _, (gx, gy) = PARTS[ref]
    p = PINS[lid][num]
    return gx * G + p["x"], gy * G - p["y"], p["ang"]

def stub_dir(ang):
    # 핀 각도(심볼 좌표) → 회로도에서 바깥쪽 방향 (dx, dy)
    return {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[int(ang)]

def wire(x1, y1, x2, y2):
    items.append(f'(wire (pts (xy {x1:.2f} {y1:.2f}) (xy {x2:.2f} {y2:.2f})) (stroke (width 0) (type default)) (uuid {U()}))')

def label(net, x, y, d):
    ang, just = {(-1, 0): (180, "right"), (1, 0): (0, "left"), (0, 1): (270, "right"), (0, -1): (90, "left")}[d]
    items.append(f'(label {q(net)} (at {x:.2f} {y:.2f} {ang}) (fields_autoplaced) (effects (font (size 1.27 1.27)) (justify {just} bottom)) (uuid {U()}))')

def text(s, gx, gy, size=1.6, bold=False):
    b = " bold" if bold else ""
    items.append(f'(text {q(s)} (at {gx * G:.2f} {gy * G:.2f} 0) (effects (font (face "Noto Sans CJK KR") (size {size} {size}){b}) (justify left top)) (uuid {U()}))')

for ref, (lid, val, (gx, gy)) in PARTS.items():
    sym_instance(ref, lid, val, gx, gy)

STUB = {"#FLG01": 1, "#FLG02": 1}
for net, members in NETS.items():
    for ref, num in members:
        x, y, ang = pin_point(ref, num)
        dx, dy = stub_dir(ang)
        n = STUB.get(ref, 2)
        x2, y2 = x + dx * n * G, y + dy * n * G
        wire(x, y, x2, y2)
        label(net, x2, y2, (dx, dy))
for ref, num in NC - {m for v in NETS.values() for m in v}:
    x, y, _ = pin_point(ref, num)
    items.append(f'(no_connect (at {x:.2f} {y:.2f}) (uuid {U()}))')

# ------------------------------------------------------------------ 주석
text("DeskPet 배선 회로도 — 구매 수정안 A (2026-10-09)", 4, 3, 3.0, True)
text("같은 이름의 라벨끼리 연결된다. X 표시 핀에는 아무것도 연결하지 않는다.", 4, 6.2, 1.8)
notes = [
    "배선 규칙 (회로가 망가지지 않게)",
    "1. J1 번호 = Jetson 40핀 물리 번호. 분배보드의 라즈베리파이 이름(GPIO2 등)은 무시.",
    "2. PCA9685 VCC는 J1-1(3.3V)에만. Jetson 5V 핀(2·4)은 어디에도 연결하지 않는다.",
    "3. 서보 전기(+5V_SERVO)는 5V 5A 어댑터 → 3A 퓨즈 → PCA9685 나사 단자로만 들어간다.",
    "4. PCA9685 헤더의 V+ 핀(6·12)은 연결 금지. Jetson과 서보 전원은 GND만 공유한다.",
    "5. OE는 오른쪽 헤더에서 GND와 점퍼(JP1) → 출력 항상 활성. 끄기는 프로그램이 담당.",
    "6. C1은 극성 주의: + = 긴 다리 = +5V_SERVO, − = 띠 = GND. 정격 16V ≥ 5V.",
    "7. 꽂기 전 멀티미터: +5V_RAW↔GND, +5V_SERVO↔GND, +3V3↔GND 단락 없음 확인.",
    "8. 연결·분리는 모든 전원을 끈 상태에서만. 켜는 순서: Jetson → 프로그램 → 서보 5V.",
    "",
    "전류·정격 점검",
    "· SG90 2개 동시 정지(스톨) 합계 약 1.5 A 이하 < 퓨즈 3 A < 어댑터 5 A",
    "· I2C 3.3 V: PCA9685 VCC 3.3 V라 모듈 풀업도 3.3 V (Jetson 핀 보호)",
    "· 서보 신호는 3.3 V PWM (SG90 입력으로 일반적으로 인식, 실물 확인)",
    "· LCD(micro USB)·ReSpeaker는 Jetson USB 전원 사용 (LCD 최대 밝기 전류 실측)",
]
for i, s in enumerate(notes):
    text(s, 120, 10 + i * 2.6, 1.5 if i not in (0, 10) else 1.9, bold=i in (0, 10))

# ------------------------------------------------------------------ 파일 쓰기
sch = (f'(kicad_sch (version 20230121) (generator eeschema)\n(uuid {ROOT})\n(paper "A3")\n'
       f'(title_block (title "DeskPet wiring schematic - plan A") (date "2026-10-09") (rev "A") '
       f'(company "Hansung capstone team 8") (comment 1 "Jetson Orin Nano + PCA9685 + SG90 x2 + ReSpeaker Lite + RC070S 7in LCD"))\n'
       f'(lib_symbols\n' + "\n".join(LIBSYMS.values()) + '\n)\n' + "\n".join(items) +
       f'\n(sheet_instances (path "/" (page "1")))\n)\n')
open(f"{OUT}/deskpet.kicad_sch", "w", encoding="utf-8").write(sch)
# 프로젝트 로컬 심볼 라이브러리 (편집용)
custom_syms = [v for k, v in LIBSYMS.items() if k.startswith("DeskPet:")]
lib = "(kicad_symbol_lib (version 20220914) (generator deskpet_gen)\n" + "\n".join(
    s.replace('(symbol "DeskPet:', '(symbol "', 1) for s in custom_syms) + "\n)\n"
open(f"{OUT}/deskpet.kicad_sym", "w", encoding="utf-8").write(lib)
open(f"{OUT}/sym-lib-table", "w").write('(sym_lib_table\n  (lib (name "DeskPet")(type "KiCad")(uri "${KIPRJMOD}/deskpet.kicad_sym")(options "")(descr "DeskPet modules"))\n)\n')
NC_FINAL = NC - {m for v in NETS.values() for m in v}
json.dump({"nets": NETS, "nc": sorted(map(list, NC_FINAL))}, open(f"{OUT}/expected.json", "w"), ensure_ascii=False, indent=1)
print("ok", len(items))
