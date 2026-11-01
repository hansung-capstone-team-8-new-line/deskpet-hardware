"""DeskPet 케이스 3D 모델 rev D를 만들고 검사한 뒤 STEP·STL·3MF·미리보기 그림을 내보낸다.

사용: python build.py [출력 폴더]      (기본: 이 폴더. 검사 실패가 있으면 exit 1)
검사: 부품끼리 겹침, 배선이 출력물을 뚫는지, 팬 위 공기층, 귀 회전·멈춤턱, 트레이(보드) 빼기, 귀 무게, 프린터 출력 범위.
"""
import itertools, os, sys
import numpy as np
from build123d import Color, Compound, Mesher, Pos, Rot, export_step, export_stl
import deskpet_case as m

xyz = lambda v: (v.X, v.Y, v.Z)

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
EPS = 0.5           # mm³ 이하는 면 접촉으로 본다
PLA = 1.24          # g/cm³ (PETG 1.27)

printed, comps, cabs, inserts = m.printed_parts(), m.components(), m.cables(), m.ear_inserts()
lines, fails = [], []

def log(s=""):
    print(s); lines.append(s)

def bb_overlap(a, b):
    A, B = a.bounding_box(), b.bounding_box()
    return all(xyz(A.min)[i] < xyz(B.max)[i] and xyz(B.min)[i] < xyz(A.max)[i] for i in range(3))

def overlap(a, b):
    if not bb_overlap(a, b):
        return 0.0
    r = a.intersect(b)
    if r is None:
        return 0.0
    return sum(s.volume for s in r) if isinstance(r, list) else r.volume

def check(title, pairs):
    bad = [(n1, n2, v) for n1, s1, n2, s2 in pairs if (v := overlap(s1, s2)) > EPS]
    log(f"[{'PASS' if not bad else 'FAIL'}] {title}")
    for n1, n2, v in bad:
        log(f"    - {n1} ↔ {n2}: {v:.1f} mm³")
        fails.append(f"{title}: {n1} ↔ {n2}")

def oriented(name, shape):
    """출력 방향으로 돌린 뒤 바닥 왼쪽 앞 모서리를 원점에 맞춘다."""
    s = m.PRINT_ROT[name[:2]] * shape
    bb = s.bounding_box()
    return Pos(-bb.min.X, -bb.min.Y, -bb.min.Z) * s

def rotate_ear(shape, x, y, ang):
    return Pos(x, y, 0) * (Rot(0, 0, ang) * (Pos(-x, -y, 0) * shape))

log("DeskPet 케이스 3D 모델 rev D 검사 (앞면 전체 LCD, [추정] 값은 실측 후 deskpet_case.py 치수 표에서 수정)")
log(f"외형 {m.W} × {m.D} × {m.H} mm (귀 포함 높이 {max(s.bounding_box().max.Z for s in printed.values()):.0f}, 범퍼 5 mm 별도)")
face_h = m.H - 2 * m.T
log(f"앞틀 {m.W} × {face_h:.0f} mm 중 LCD 유리 {m.LCD['w']} × {m.LCD['h']} = {m.LCD['w'] * m.LCD['h'] / (m.W * face_h) * 100:.0f}%, "
    f"테두리 좌우 {m.LX0:.1f} · 위아래 {m.LZ0:.1f} mm, 창 {m.LCD['w'] - 2 * m.OVERLAP:.1f} × {m.LCD['h'] - 2 * m.OVERLAP:.1f} "
    f"(표시 영역 {m.LCD['aa_w']} × {m.LCD['aa_h']}는 모두 보임)")
log()

solids = {**printed, **comps}
check("출력물·부품끼리 겹침 없음", [(a, solids[a], b, solids[b]) for a, b in itertools.combinations(solids, 2)])
check("2색용 안쪽 귀 장식이 다른 것과 겹치지 않음", [(i, s, n, t) for i, s in inserts.items() for n, t in solids.items()
                                              if not n.startswith("P6")])
check("배선이 출력물을 뚫지 않음", [(c, s, p, ps) for c, s in cabs.items() for p, ps in printed.items()])
ko = m.keepouts()
check("Jetson 팬 위 공기층 30 mm 비어 있음", [(k, ks, n, s) for k, ks in ko.items() for n, s in {**solids, **cabs}.items()
                                             if not n.startswith("Jetson")])

# 귀 회전: 서보 축 기준 ±30° (소프트웨어는 ±25°까지 사용)
ear_pairs = []
for (x, y), side in zip(m.EARS, ("왼쪽", "오른쪽")):
    ear = printed[f"P6 {side} 귀"].fuse(inserts[f"P6 {side} 귀 안쪽"])
    for ang in (-30, -15, 15, 30):
        moved = rotate_ear(ear, x, y, ang)
        for n, s in solids.items():
            if not n.startswith("P6") and "SG90" not in n:
                ear_pairs.append((f"{side} 귀 {ang:+d}°", moved, n, s))
check("귀가 ±30° 돌 때 부딪힘 없음 (멈춤 나사 포함)", ear_pairs)

# 멈춤턱: 귀 허브 아래 홈이 윗판의 M2.5 나사 머리에 걸려 ±40° 전에 멈춰야 한다
stop = comps["귀 멈춤 나사 M2.5 ×2"]
hits = [overlap(rotate_ear(printed[f"P6 {side} 귀"], x, y, ang), stop)
        for (x, y), side in zip(m.EARS, ("왼쪽", "오른쪽")) for ang in (-40, 40)]
ok = min(hits) > EPS
log(f"[{'PASS' if ok else 'FAIL'}] 귀를 ±40°까지 돌리면 멈춤 나사에 걸림 (최소 겹침 {min(hits):.1f} mm³ → 서보 스톨 전에 기계적으로 멈춤)")
if not ok:
    fails.append("귀 멈춤턱이 ±40°에서 걸리지 않음")

# 반납: 문 나사 2개를 풀고 트레이를 Jetson째 뒤로 끝까지 뺀다. 뒤판·윗판·바닥판은 그대로.
check("뒤판을 열지 않고 Jetson 트레이를 뒤로 끝까지 뺄 수 있음", [("트레이·문 통로", m.tray_sweep(), n, s) for n, s in solids.items()
                                                 if n != "P5 트레이" and not n.startswith("Jetson")])

g = (printed["P6 왼쪽 귀"].volume + inserts["P6 왼쪽 귀 안쪽"].volume) / 1000 * PLA
log(f"[{'PASS' if g <= 10 else 'FAIL'}] 귀 1개 무게 {g:.1f} g (속 채움 100% 상한, 기준 10 g 이하)")
if g > 10:
    fails.append("귀 무게 초과")

# 프린터 출력 범위 (출력 방향으로 돌린 크기)
log()
log("출력 범위 (출력 방향 기준 가로 × 세로 × 높이) · 무게는 속 채움 100% PLA 기준 상한")
for n, s in {**printed, **inserts}.items():
    if n == "P6 오른쪽 귀 안쪽":
        continue
    sz = xyz(oriented(n, s).bounding_box().size)
    fit = {k: all(a <= b for a, b in zip(sz, bed)) for k, bed in m.BEDS.items()}
    if not any(fit.values()):
        fails.append(f"출력 범위 초과: {n}")
    size = " × ".join(f"{d:.0f}" for d in sz)
    log(f"  [{'PASS' if all(fit.values()) else 'FAIL'}] {n:12s} {size:>15s} mm  부피 {s.volume / 1000:6.1f} cm³  "
        f"최대 {s.volume / 1000 * PLA:5.0f} g  출력 가능: {' · '.join(k for k, v in fit.items() if v) or '없음'}")

log()
log(f"참고: LCD 오른쪽 끝 x={m.LX1:.2f}, 옆벽 안쪽 x={m.XIN1}. 남는 {m.XIN1 - m.LX1:.1f} mm에 HDMI FPC ㄱ자 플러그(8.6 mm)와 "
    f"ㄱ자 micro USB(7 mm)를 넣었다. 일자 플러그·일반 ㄱ자 HDMI 젠더(약 20 mm 이상)는 들어가지 않는다.")
log()
log("결과: " + ("모든 검사 통과" if not fails else f"실패 {len(fails)}건"))

# ------------------------------------------------------------------ 내보내기
os.makedirs(f"{OUT}/stl", exist_ok=True)
os.makedirs(f"{OUT}/3mf", exist_ok=True)
os.makedirs(f"{OUT}/renders", exist_ok=True)
for d_, ext in (("stl", ".stl"), ("3mf", ".3mf")):
    for old in os.listdir(f"{OUT}/{d_}"):
        if old.endswith(ext):
            os.remove(f"{OUT}/{d_}/{old}")
stl_names = {"P1 바닥판": "P1-base", "P2 몸통": "P2-body", "P3 윗판": "P3-top", "P4 뒤판": "P4-back",
             "P5 트레이": "P5-tray-door", "P6 왼쪽 귀": "P6-ear-left", "P6 오른쪽 귀": "P6-ear-right",
             "P6 왼쪽 귀 안쪽": "P6-ear-inner-x2", "P7 스피커 받침": "P7-speaker-cradle"}
allp = {**printed, **inserts}
for n, f in stl_names.items():
    export_stl(oriented(n, allp[n]), f"{OUT}/stl/{f}.stl", tolerance=0.05, angular_tolerance=0.2)

# 출력판 3MF: 출력 방향으로 돌려 판 위에 배치. Bambu Studio에서 열면 판 하나가 된다 (설정은 PRINTING.md).
def ear_pair(side):
    ear, ins = printed[f"P6 {side} 귀"], inserts[f"P6 {side} 귀 안쪽"]
    bb = ear.bounding_box()
    off = Pos(-bb.min.X, -bb.min.Y, -bb.min.Z)
    return [(f"P6 {side} 귀", off * ear), (f"P6 {side} 귀 안쪽 (2색)", off * ins)]

def plate(path, bed, rows):
    """rows = [[(이름, 도형 또는 [(이름, 도형)…] 묶음), …], …] 줄마다 왼쪽부터 놓고 판 가운데로 옮긴다."""
    placed, y = [], 0.0
    for row in rows:
        x, depth = 0.0, 0.0
        for item in row:
            group = item if isinstance(item, list) else [item]
            bb = group[0][1].bounding_box()
            placed += [(n, Pos(x - bb.min.X, y - bb.min.Y, -bb.min.Z) * s) for n, s in group]
            x += bb.size.X + 12
            depth = max(depth, bb.size.Y)
        y += depth + 12
    lo = np.min([xyz(s.bounding_box().min) for _, s in placed], axis=0)
    hi = np.max([xyz(s.bounding_box().max) for _, s in placed], axis=0)
    size = hi - lo
    assert size[0] <= bed[0] and size[1] <= bed[1], f"{path}: 판 밖 {size}"
    sx, sy = (bed[0] - size[0]) / 2 - lo[0], (bed[1] - size[1]) / 2 - lo[1]
    mesher = Mesher()
    for n, s in placed:
        mesher.add_shape(Pos(sx, sy, 0) * s, linear_deflection=0.05, angular_deflection=0.2, part_number=n)
    mesher.write(path)
    return f"{os.path.basename(path)}: " + ", ".join(n for n, _ in placed) + f" (판 위 {size[0]:.0f} × {size[1]:.0f} mm)"

o = {n: oriented(n, s) for n, s in printed.items()}
plates = [
    plate(f"{OUT}/3mf/X1C-1-P2-body.3mf", m.BEDS["X1C"], [[("P2 몸통", o["P2 몸통"])]]),
    plate(f"{OUT}/3mf/X1C-2-P5-tray-door.3mf", m.BEDS["X1C"], [[("P5 트레이", o["P5 트레이"])]]),
    plate(f"{OUT}/3mf/H2D-1-P1-base-P3-top.3mf", m.BEDS["H2D"], [[("P1 바닥판", o["P1 바닥판"])], [("P3 윗판", o["P3 윗판"])]]),
    plate(f"{OUT}/3mf/H2D-2-P4-back-P7-ears.3mf", (300, 320),          # 2색(두 노즐) 출력 영역 300 × 320
          [[("P4 뒤판", o["P4 뒤판"]), ("P7 스피커 받침", o["P7 스피커 받침"])], [ear_pair("왼쪽"), ear_pair("오른쪽")]]),
]
log()
log("출력판 3MF (Bambu Studio에서 열기, 설정은 PRINTING.md)")
for p_ in plates:
    log("  " + p_)

def colored(name, shape, rgb):
    shape.label, shape.color = name, Color(*rgb)
    return shape

COMP_RGB = (0.35, 0.42, 0.52)
COMP_COLORS = {"Jetson": (0.49, 0.61, 0.77), "7인치 LCD": (0.12, 0.17, 0.24), "ReSpeaker": (0.91, 0.58, 0.48),
               "Adafruit": (0.30, 0.30, 0.32), "PCA9685": (0.73, 0.64, 0.89), "SG90": (0.20, 0.35, 0.70),
               "GPIO": (0.75, 0.25, 0.20), "DC 클립": (0.15, 0.15, 0.15), "퓨즈": (0.85, 0.20, 0.20),
               "실리콘": (0.25, 0.25, 0.25), "HDMI": (0.15, 0.15, 0.15), "micro USB": (0.15, 0.15, 0.15),
               "멈춤 나사": (0.92, 0.92, 0.88)}
CABLE_COLORS = {"HDMI FPC": (0.85, 0.60, 0.20), "HDMI": (0.1, 0.1, 0.1), "LCD micro": (0.25, 0.25, 0.3),
                "ReSpeaker USB": (0.95, 0.95, 0.95), "스피커": (0.85, 0.3, 0.3), "40핀": (0.55, 0.55, 0.85),
                "신호": (0.3, 0.7, 0.4), "왼쪽 귀": (0.9, 0.6, 0.1), "오른쪽 귀": (0.9, 0.6, 0.1), "서보 전원": (0.85, 0.1, 0.1)}
INSERT_RGB = (0.98, 0.62, 0.62)

def rgb_for(name, table, default):
    return next((c for k, c in table.items() if k in name), default)

items = [(n, s, m.COLORS[n[:2]]) for n, s in printed.items()]
items += [(n, s, INSERT_RGB) for n, s in inserts.items()]
items += [(n, s, rgb_for(n, COMP_COLORS, COMP_RGB)) for n, s in comps.items()]
items += [(n, s, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n, s in cabs.items()]
asm = Compound(label="DeskPet case rev D", children=[colored(n, s, c) for n, s, c in items])
export_step(asm, f"{OUT}/deskpet-case-assembly.step")

# ------------------------------------------------------------------ 미리보기 그림 (VTK로 깊이 버퍼 렌더링 → matplotlib으로 제목·범례)
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
from matplotlib import font_manager
import vtk
from vtk.util.numpy_support import numpy_to_vtk, numpy_to_vtkIdTypeArray, vtk_to_numpy
_have = {f.name for f in font_manager.fontManager.ttflist}
plt.rcParams["font.family"] = [f for f in ("Malgun Gothic", "Noto Sans CJK KR", "AppleGothic") if f in _have][:1] + ["DejaVu Sans"]

MESH = {}
def mesh(name, shape):
    if name not in MESH:
        v, t = shape.tessellate(0.1, 0.2)
        MESH[name] = (np.array([xyz(p) for p in v]), np.array(t, dtype=np.int64))
    return MESH[name]

def polydata(v, t):
    pd = vtk.vtkPolyData()
    pts = vtk.vtkPoints(); pts.SetData(numpy_to_vtk(np.ascontiguousarray(v), deep=True)); pd.SetPoints(pts)
    cells = np.hstack([np.full((len(t), 1), 3, dtype=np.int64), t]).ravel()
    ca = vtk.vtkCellArray(); ca.SetCells(len(t), numpy_to_vtkIdTypeArray(cells, deep=True)); pd.SetPolys(ca)
    nf = vtk.vtkPolyDataNormals(); nf.SetInputData(pd); nf.SetFeatureAngle(30); nf.SplittingOn(); nf.Update()
    return nf.GetOutput()

def render(path, title, entries, elev, azim, legend=(), ortho=False, size=(13, 9), zoom=1.0):
    ren = vtk.vtkRenderer(); ren.SetBackground(1, 1, 1)
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for key, shape, rgb, offset, alpha in entries:
        v, t = mesh(key, shape)
        v = v + np.array(offset)
        lo, hi = np.minimum(lo, v.min(0)), np.maximum(hi, v.max(0))
        pd = polydata(v, t)
        mp = vtk.vtkPolyDataMapper(); mp.SetInputData(pd)
        ac = vtk.vtkActor(); ac.SetMapper(mp)
        pr_ = ac.GetProperty(); pr_.SetColor(*rgb); pr_.SetOpacity(alpha); pr_.SetAmbient(0.25); pr_.SetDiffuse(0.75); pr_.SetSpecular(0.08)
        ren.AddActor(ac)
        if key[:1] == "P":                                   # 출력물은 모서리 선
            fe = vtk.vtkFeatureEdges(); fe.SetInputData(pd); fe.BoundaryEdgesOn(); fe.FeatureEdgesOn(); fe.SetFeatureAngle(35)
            fe.ManifoldEdgesOff(); fe.NonManifoldEdgesOff(); fe.ColoringOff()
            em = vtk.vtkPolyDataMapper(); em.SetInputConnection(fe.GetOutputPort())
            em.SetResolveCoincidentTopologyToPolygonOffset()
            ea = vtk.vtkActor(); ea.SetMapper(em); ea.GetProperty().SetColor(0.35, 0.33, 0.30); ea.GetProperty().SetLineWidth(1.2)
            ren.AddActor(ea)
    c = (lo + hi) / 2
    e, a = np.radians(elev), np.radians(azim)
    d = np.array([np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)])
    cam = ren.GetActiveCamera()
    cam.SetFocalPoint(*c); cam.SetPosition(*(c + d * 1000)); cam.SetViewUp(0, 0, 1)
    if ortho:
        cam.ParallelProjectionOn()
    ren.ResetCamera(); cam.Zoom(1.15 * zoom)
    lk = vtk.vtkLightKit(); lk.AddLightsToRenderer(ren)
    W_, H_ = int(size[0] * 130), int((size[1] - 0.9) * 130)
    win = vtk.vtkRenderWindow(); win.SetOffScreenRendering(1); win.AddRenderer(ren); win.SetSize(W_, H_)
    win.SetMultiSamples(8); win.Render()
    grab = vtk.vtkWindowToImageFilter(); grab.SetInput(win); grab.Update()
    img = grab.GetOutput()
    w, h, _ = img.GetDimensions()
    arr = vtk_to_numpy(img.GetPointData().GetScalars()).reshape(h, w, -1)[::-1]
    fig = plt.figure(figsize=size, dpi=130)
    ax = fig.add_axes([0, 0, 1, (size[1] - 0.9) / size[1]]); ax.imshow(arr); ax.set_axis_off()
    fig.text(0.02, 1 - 0.45 / size[1], title, fontsize=15, fontweight="bold", va="center")
    if legend:
        fig.legend(handles=[Patch(color=c_, label=l) for l, c_ in legend], loc="lower left", fontsize=9, ncol=2, frameon=False)
    fig.savefig(path, facecolor="white")
    plt.close(fig)
    win.Finalize()

E = lambda n, s, c, off=(0, 0, 0), a=1.0: (n, s, c, off, a)
pr = {n: E(n, s, m.COLORS[n[:2]]) for n, s in printed.items()}
pr.update({n: E(n, s, INSERT_RGB) for n, s in inserts.items()})
co = {n: E(n, s, rgb_for(n, COMP_COLORS, COMP_RGB)) for n, s in comps.items()}
ca = {n: E(n, s, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n, s in cabs.items()}
leg_parts = [(n, rgb_for(n, COMP_COLORS, COMP_RGB)) for n in comps if not any(k in n for k in ("범퍼", "ㄱ자", "멈춤", "암 단자"))]
leg_cables = [(n, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n in cabs]
outside = [co["7인치 LCD (C)"], co["실리콘 범퍼 ×4"], co["귀 멈춤 나사 M2.5 ×2"]]

render(f"{OUT}/renders/01-closed.png", "DeskPet 케이스 rev D — 앞면 전체 LCD (앞·오른쪽 위)",
       list(pr.values()) + outside, 20, -60)
render(f"{OUT}/renders/01b-front.png", "정면 — LCD 유리가 앞면을 채움, 테두리 사방 13.5 mm",
       list(pr.values()) + outside, 0, -90, ortho=True, size=(11, 9.6))
render(f"{OUT}/renders/02-back.png", "뒤 — 트레이 문(Jetson 단자 창·손잡이), HDMI 단자, USB 통과 구멍, DC 단자, 배기",
       list(pr.values()) + [co[n] for n in comps] + list(ca.values()), 18, 118)
inside = [v for k, v in pr.items() if k in ("P1 바닥판", "P5 트레이", "P7 스피커 받침")]
render(f"{OUT}/renders/03-inside-back-right.png", "내부 배치와 배선 (뒤·오른쪽 위) — 몸통·윗판·뒤판을 감춤",
       inside + list(co.values()) + list(ca.values()), 38, 48, legend=leg_parts + leg_cables, size=(14, 10))
render(f"{OUT}/renders/03b-inside-front-left.png", "내부 (앞·왼쪽 위) — LCD 탭 나사 보스, 오른쪽 ㄱ자 플러그, 왼쪽 벽 분배보드",
       [pr["P1 바닥판"], pr["P2 몸통"], pr["P5 트레이"], pr["P7 스피커 받침"]]
       + [v for k, v in co.items() if k != "7인치 LCD (C)"] + list(ca.values()), 40, -125, size=(14, 10))
render(f"{OUT}/renders/04-top-plate-underside.png", "윗판(P3) 아래 — 귀 서보·멈춤 나사·ReSpeaker·PCA9685 (아래에서 올려다봄)",
       [pr["P3 윗판"], co["SG90 왼쪽 귀"], co["SG90 오른쪽 귀"], co["ReSpeaker Lite"], co["PCA9685 + 지지대"],
        co["귀 멈춤 나사 M2.5 ×2"], ca["왼쪽 귀 서보선"], ca["오른쪽 귀 서보선"]], -55, -70)
explode = {"P1 바닥판": (0, 0, -60), "P2 몸통": (0, -40, 0), "P3 윗판": (0, 0, 70), "P4 뒤판": (0, 60, 0),
           "P5 트레이": (0, 130, -10), "P6 왼쪽 귀": (0, 0, 110), "P6 오른쪽 귀": (0, 0, 110), "P7 스피커 받침": (0, 0, -35),
           "P6 왼쪽 귀 안쪽": (0, -6, 110), "P6 오른쪽 귀 안쪽": (0, -6, 110)}
render(f"{OUT}/renders/05-exploded.png", "출력물 분해도 — P1 바닥판 · P2 몸통 · P3 윗판 · P4 뒤판 · P5 트레이+문 · P6 귀 · P7 스피커 받침",
       [E(n, s, pr[n][2], explode[n]) for n, s in {**printed, **inserts}.items()], 18, -52, size=(13, 11))
pull = (0, 115, 0)
render(f"{OUT}/renders/06-board-return.png", "보드 반납 — 문 나사 2개를 풀고 트레이를 Jetson째 뒤로 뺀다 (뒤판·윗판은 그대로)",
       [v for k, v in pr.items() if k != "P5 트레이"]
       + [E("P5 트레이", printed["P5 트레이"], m.COLORS["P5"], pull),
          E("Jetson Orin Nano (받침대 포함)", comps["Jetson Orin Nano (받침대 포함)"], COMP_COLORS["Jetson"], pull)], 24, 135)

with open(f"{OUT}/check-report.txt", "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
sys.exit(1 if fails else 0)
