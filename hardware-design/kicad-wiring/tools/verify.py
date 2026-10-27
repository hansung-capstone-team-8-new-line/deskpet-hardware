"""KiCad가 내보낸 넷리스트를 독립 기준표·안전 규칙과 대조한다.

사용: python3 verify.py deskpet.net   → 통과 시 exit 0, 실패 시 exit 1
기준표(GOLDEN)는 gen.py와 별도로 손으로 작성했다(생성기의 실수를 잡기 위해).
"""
import re, sys

GOLDEN = {  # 넷 이름: {(부품, 핀)} — PWR_FLAG(#FLG)는 가상 기호라 넷리스트에 없음
    "+3V3": {("J1", "1"), ("U4", "5")},
    "I2C_SDA": {("J1", "3"), ("U4", "4")},
    "I2C_SCL": {("J1", "5"), ("U4", "3")},
    "GND": {("J1", "6"), ("U4", "1"), ("U4", "7"), ("U4", "14"), ("U4", "17"), ("U4", "20"), ("JP1", "2"),
            ("M1", "3"), ("M2", "3"), ("C1", "2"), ("J3", "2"), ("J3", "4"), ("PS2", "2")},
    "PCA_OE": {("U4", "8"), ("JP1", "1")},
    "SERVO_L_PWM": {("U4", "15"), ("M1", "1")},
    "SERVO_R_PWM": {("U4", "18"), ("M2", "1")},
    "+5V_ADP": {("PS2", "1"), ("SW1", "1")},
    "+5V_RAW": {("SW1", "2"), ("J3", "1"), ("J3", "3"), ("F1", "1")},
    "+5V_SERVO": {("F1", "2"), ("C1", "1"), ("U4", "13"), ("U4", "16"), ("U4", "19"), ("M1", "2"), ("M2", "2")},
    "DC_JETSON": {("PS1", "1"), ("U1", "1")},
    "DP_LINK": {("U1", "2"), ("A1", "1")},
    "HDMI_LINK": {("A1", "2"), ("U2", "1")},
    "USB_LCD_PWR": {("U1", "3"), ("U2", "2")},
    "USB_RESPEAKER": {("U1", "4"), ("U3", "1")},
    "SPK_1": {("U3", "2"), ("LS1", "1")},
    "SPK_2": {("U3", "3"), ("LS1", "2")},
}
MUST_BE_UNCONNECTED = {("J1", "2"): "Jetson 5V", ("J1", "4"): "Jetson 5V", ("J1", "17"): "Jetson 3.3V(2번째)",
                       ("U4", "6"): "PCA 왼쪽 헤더 V+", ("U4", "12"): "PCA 오른쪽 헤더 V+",
                       ("U4", "2"): "PCA 왼쪽 OE(오른쪽만 사용)"}

def parse(path):
    t = open(path, encoding="utf-8").read()
    nets = {}
    for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "([^"]*)"\)(.*?)\)\s*(?=\(net |\)\s*\)\s*$)', t, re.S):
        name = m.group(1).lstrip("/")
        nodes = set(re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', m.group(2)))
        nets[name] = nodes
    return nets

def check(nets):
    errs = []
    real = {k: v for k, v in nets.items() if not k.startswith("unconnected-")}
    # 1) 기준 넷과 정확히 일치
    for name, want in GOLDEN.items():
        got = real.get(name)
        if got is None:
            errs.append(f"넷 없음: {name}")
        elif got != want:
            errs.append(f"{name}: 빠짐 {sorted(want - got)} / 잘못 추가 {sorted(got - want)}")
    for name in real:
        if name not in GOLDEN:
            errs.append(f"기준에 없는 넷: {name} {sorted(real[name])}")
    # 2) 반드시 비어 있어야 하는 핀
    for (ref, pin), why in MUST_BE_UNCONNECTED.items():
        for name, nodes in real.items():
            if (ref, pin) in nodes:
                errs.append(f"연결 금지 핀 사용: {ref}-{pin} ({why}) → {name}")
    # 3) 안전 규칙 (기준표와 별개로 다시 확인)
    def net_of(ref, pin):
        for n, nodes in real.items():
            if (ref, pin) in nodes:
                return n
    if net_of("C1", "1") != "+5V_SERVO" or net_of("C1", "2") != "GND":
        errs.append("C1 극성 오류: + 다리는 +5V_SERVO, − 다리는 GND여야 함")
    if net_of("J3", "3") == net_of("U4", "13"):
        errs.append("퓨즈 우회: 클립 터미널 +와 PCA 단자 V+가 퓨즈 없이 직결")
    if net_of("PS2", "1") == net_of("J3", "1"):
        errs.append("스위치 우회: 어댑터 +가 서보 전원 스위치 없이 클립 터미널에 직결")
    if net_of("J1", "6") != net_of("PS2", "2"):
        errs.append("공통 접지 없음: Jetson GND와 서보 전원 GND가 분리됨")
    if net_of("J1", "1") in ("+5V_SERVO", "+5V_RAW"):
        errs.append("3.3V와 서보 5V 단락")
    if net_of("J1", "3") != net_of("U4", "4") or net_of("J1", "5") != net_of("U4", "3"):
        errs.append("I2C SDA/SCL 바뀜")
    for m, pwm in (("M1", "U4-15"), ("M2", "U4-18")):
        if net_of(m, "2") != "+5V_SERVO" or net_of(m, "3") != "GND":
            errs.append(f"{m} 서보 전원 핀 오류")
    return errs

if __name__ == "__main__":
    nets = parse(sys.argv[1])
    errs = check(nets)
    if errs:
        print("FAIL")
        for e in errs:
            print(" -", e)
        sys.exit(1)
    print(f"PASS: 넷 {len(GOLDEN)}개 일치, 연결 금지 핀 {len(MUST_BE_UNCONNECTED)}개 비어 있음, 안전 규칙 7종 통과")
