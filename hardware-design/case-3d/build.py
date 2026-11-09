"""DeskPet 케이스 3D 모델 rev E를 만들고 검사한 뒤 STEP·STL·3MF·미리보기 그림을 내보낸다.

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

printed, comps, cabs = m.printed_parts(), m.components(), m.cables()
paint = m.paint_areas()            # 그림에서만 분홍으로 보여 주는 도색 자리 (출력물 아님)
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

log("DeskPet 케이스 3D 모델 rev E 검사 (앞면 전체 LCD · 둥근 외형, [추정] 값은 실측 후 deskpet_case.py 치수 표에서 수정)")
log(f"외형 {m.W} × {m.D} × {m.H} mm (귀 포함 높이 {max(s.bounding_box().max.Z for s in printed.values()):.0f}, "
    f"아래로 발 {m.FEET['h']} + 범퍼 {5 - m.FEET['bumper_depth']} mm 별도)")
face_h = m.H - 2 * m.T
log(f"앞틀 {m.W} × {face_h:.0f} mm 중 LCD 유리 {m.LCD['w']} × {m.LCD['h']} = {m.LCD['w'] * m.LCD['h'] / (m.W * face_h) * 100:.0f}%, "
    f"테두리 좌우 {m.LX0:.1f} · 위아래 {m.LZ0:.1f} mm, 창 {m.LCD['w'] - 2 * m.OVERLAP:.1f} × {m.LCD['h'] - 2 * m.OVERLAP:.1f} "
    f"(표시 영역 {m.LCD['aa_w']} × {m.LCD['aa_h']}는 모두 보임)")
log()

solids = {**printed, **comps}
check("출력물·부품끼리 겹침 없음", [(a, solids[a], b, solids[b]) for a, b in itertools.combinations(solids, 2)])
check("배선이 출력물을 뚫지 않음", [(c, s, p, ps) for c, s in cabs.items() for p, ps in printed.items()])
ko = m.keepouts()
check("Jetson 팬 위 공기층 30 mm 비어 있음", [(k, ks, n, s) for k, ks in ko.items() for n, s in {**solids, **cabs}.items()
                                             if not n.startswith("Jetson")])

# 귀 회전: 서보 축 기준 ±30° (소프트웨어는 ±25°까지 사용)
ear_pairs = []
for (x, y), side in zip(m.EARS, ("왼쪽", "오른쪽")):
    ear = printed[f"P6 {side} 귀"]
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

g = printed["P6 왼쪽 귀"].volume / 1000 * PLA
log(f"[{'PASS' if g <= 10 else 'FAIL'}] 귀 1개 무게 {g:.1f} g (속 채움 100% 상한, 기준 10 g 이하)")
if g > 10:
    fails.append("귀 무게 초과")

# 첫 층 들뜸: 출력 방향에서 바닥 2 mm 안에 45°보다 눕힌 아래 향한 경사면이 있으면 그 부분이 공중에서 시작한다
# (Bambu Studio "떠 있는 구조" 경고). 수평 천장(나사 머리 자리·혼 홈 등 짧은 다리)은 따로 세어 참고로만 적는다.
def bed_overhang(shape):
    v, t = shape.tessellate(0.05, 0.1)
    V, T_ = np.array([xyz(p) for p in v]), np.array(t)
    a, b, c = V[T_[:, 0]], V[T_[:, 1]], V[T_[:, 2]]
    nrm = np.cross(b - a, c - a)
    ln = np.linalg.norm(nrm, axis=1)
    area, nz, zc = ln / 2, nrm[:, 2] / np.maximum(ln, 1e-12), (a[:, 2] + b[:, 2] + c[:, 2]) / 3
    near = (zc > 0.05) & (zc < 2.0)
    return area[near & (nz < -0.73) & (nz > -0.995)].sum(), area[near & (nz <= -0.995)].sum()

bad = []
for n, s in printed.items():
    o = oriented(n, s if not n.startswith("P8") else m.p8_foot())
    slope, bridge = bed_overhang(o)
    if slope > 5:
        bad.append(f"{n} {slope:.0f} mm²")
    elif bridge > 0:
        lines.append(f"    참고: {n} 바닥 2 mm 안 수평 다리 {bridge:.0f} mm² (나사 머리 자리·혼 홈 등)")
log(f"[{'PASS' if not bad else 'FAIL'}] 출력 방향에서 바닥 2 mm 안에 45°보다 눕힌 경사면 없음 (첫 층부터 받침이 있음)")
for b_ in bad:
    log(f"    - {b_}")
    fails.append(f"첫 층 들뜸: {b_}")

# 프린터 출력 범위 (출력 방향으로 돌린 크기)
log()
log("출력 범위 (출력 방향 기준 가로 × 세로 × 높이) · 무게는 속 채움 100% PLA 기준 상한")
for n, s in printed.items():
    if n == "P6 오른쪽 귀":
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
             "P7 스피커 받침": "P7-speaker-cradle", "P8 발": "P8-foot-x4"}
allp = {**printed, "P8 발": m.p8_foot()}
for n, f in stl_names.items():
    export_stl(oriented(n, allp[n]), f"{OUT}/stl/{f}.stl", tolerance=0.05, angular_tolerance=0.2)

# 출력판 3MF: 출력 방향으로 돌려 판 위에 배치. Bambu Studio에서 열면 판 하나가 된다 (설정은 PRINTING.md).
FIL = 1.24           # g/cm³. Bambu PLA Basic (출력 무게 상한 계산 기준)
JOB_LIMIT = 300      # g. H2D 한 번 출력 허용량
JOB_MARGIN = 0.9     # 퍼지·브림·스커트 몫으로 10% 남김 → 물체 무게 상한 270 g
GAP = 20             # mm. 출력판 위 물체 사이 간격 (브림·떼어내기 여유)
SAFE = (275, 300)    # mm. H2D 판 325 × 320에서 양옆 "한쪽 노즐 전용" 띠(각 약 25 mm)와 앞뒤 여유 10 mm를 뺀 가운데 영역

def plate(path, bed, rows):
    """rows = [[항목, …], …]. 항목은 (이름, 도형) 하나 또는 {"col": [(이름, 도형), …]}(세로로 쌓는 묶음).
    줄마다 왼쪽부터 놓고 판 가운데로 옮긴다. 돌려준 값: (요약 문자열, 물체 무게 상한 g)."""
    placed, y = [], 0.0
    for row in rows:
        x, depth = 0.0, 0.0
        for item in row:
            col = item["col"] if isinstance(item, dict) else [item]
            yy, width = y, 0.0
            for n, sh in col:
                bb = sh.bounding_box()
                placed.append((n, Pos(x - bb.min.X, yy - bb.min.Y, -bb.min.Z) * sh))
                yy += bb.size.Y + GAP
                width = max(width, bb.size.X)
            x += width + GAP
            depth = max(depth, yy - GAP - y)
        y += depth + GAP
    lo = np.min([xyz(sh.bounding_box().min) for _, sh in placed], axis=0)
    hi = np.max([xyz(sh.bounding_box().max) for _, sh in placed], axis=0)
    size = hi - lo
    assert size[0] <= bed[0] and size[1] <= bed[1] and size[2] <= bed[2], f"{path}: 판 밖 {size}"
    sx, sy = (bed[0] - size[0]) / 2 - lo[0], (bed[1] - size[1]) / 2 - lo[1]
    mesher = Mesher()
    for n, sh in placed:
        mesher.add_shape(Pos(sx, sy, 0) * sh, linear_deflection=0.05, angular_deflection=0.2, part_number=n)
    mesher.write(path)
    grams = sum(sh.volume for _, sh in placed) / 1000 * FIL
    return (f"{os.path.basename(path)}: " + ", ".join(n for n, _ in placed)
            + f" (판 위 {size[0]:.0f} × {size[1]:.0f} × {size[2]:.0f} mm)", grams, size)

o = {n: oriented(n, s) for n, s in printed.items()}
feet = [(f"P8 발 {i + 1}", oriented("P8", m.p8_foot())) for i in range(4)]
fit = printed["P2 몸통"] & m.box(-1, -1, -1, m.W + 1, 12, m.H + 1)                     # 앞틀 맞춤 시험편: 앞면부터 12 mm
H2D = m.BEDS["H2D"]
jobs = [
    ("시험", plate(f"{OUT}/3mf/H2D-0-P2-fit-test.3mf", H2D, [[("P2 앞틀 시험편 12 mm", oriented("P2", fit))]])),
    ("1회차", plate(f"{OUT}/3mf/H2D-1-P2-body-P7-P8-feet.3mf", H2D,
                   [[("P2 몸통", o["P2 몸통"])], [("P7 스피커 받침", o["P7 스피커 받침"])] + feet])),
    ("2회차", plate(f"{OUT}/3mf/H2D-2-P1-base-P4-back.3mf", H2D,
                   [[("P1 바닥판", o["P1 바닥판"])], [("P4 뒤판", o["P4 뒤판"])]])),
    ("3회차", plate(f"{OUT}/3mf/H2D-3-P3-top-P5-tray-P6-ears.3mf", H2D,
                   [[("P3 윗판", o["P3 윗판"])],
                    [("P5 트레이", o["P5 트레이"]), {"col": [("P6 왼쪽 귀", o["P6 왼쪽 귀"]), ("P6 오른쪽 귀", o["P6 오른쪽 귀"])]}]])),
]
log()
log(f"H2D 출력 계획 (한 번에 {JOB_LIMIT} g까지, 퍼지·브림 몫 10%를 빼고 물체 무게 상한 {JOB_LIMIT * JOB_MARGIN:.0f} g 이하, "
    f"속 채움 100% PLA 기준 · 물체 간격 {GAP} mm · 가운데 {SAFE[0]} × {SAFE[1]} mm 안)")
for job, (desc, g, size) in jobs:
    ok_w, ok_s = g <= JOB_LIMIT * JOB_MARGIN, size[0] <= SAFE[0] and size[1] <= SAFE[1]
    log(f"  [{'PASS' if ok_w and ok_s else 'FAIL'}] {job} {desc} · 최대 {g:.0f} g")
    if not ok_w:
        fails.append(f"출력 무게 초과: {job}")
    if not ok_s:
        fails.append(f"한쪽 노즐 전용 띠 침범: {job}")
total = sum(g for job, (_, g, _s) in jobs if job != "시험")
log(f"  본 출력 합계 최대 {total:.0f} g (시험편 제외)")
log()
log("결과: " + ("모든 검사 통과" if not fails else f"실패 {len(fails)}건"))

def colored(name, shape, rgb):
    shape.label, shape.color = name, Color(*rgb)
    return shape

COMP_RGB = (0.35, 0.42, 0.52)
COMP_COLORS = {"Jetson": (0.49, 0.61, 0.77), "7인치 LCD": (0.12, 0.17, 0.24), "ReSpeaker": (0.91, 0.58, 0.48),
               "Adafruit": (0.30, 0.30, 0.32), "PCA9685": (0.73, 0.64, 0.89), "SG90": (0.20, 0.35, 0.70),
               "GPIO": (0.75, 0.25, 0.20), "DC 클립": (0.15, 0.15, 0.15), "퓨즈": (0.85, 0.20, 0.20),
               "실리콘": (0.25, 0.25, 0.25), "HDMI": (0.15, 0.15, 0.15), "micro USB": (0.15, 0.15, 0.15),
               "멈춤 나사": (0.92, 0.92, 0.88), "발 나사": (0.92, 0.92, 0.88)}
CABLE_COLORS = {"HDMI FPC": (0.85, 0.60, 0.20), "HDMI": (0.1, 0.1, 0.1), "LCD micro": (0.25, 0.25, 0.3),
                "ReSpeaker USB": (0.95, 0.95, 0.95), "스피커": (0.85, 0.3, 0.3), "40핀": (0.55, 0.55, 0.85),
                "신호": (0.3, 0.7, 0.4), "왼쪽 귀": (0.9, 0.6, 0.1), "오른쪽 귀": (0.9, 0.6, 0.1), "서보 전원": (0.85, 0.1, 0.1)}
INSERT_RGB = m.PINK

def rgb_for(name, table, default):
    return next((c for k, c in table.items() if k in name), default)

items = [(n, s, m.COLORS[n[:2]]) for n, s in printed.items()]
items += [(n, s, rgb_for(n, COMP_COLORS, COMP_RGB)) for n, s in comps.items()]
items += [(n, s, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n, s in cabs.items()]
asm = Compound(label="DeskPet case rev E", children=[colored(n, s, c) for n, s, c in items])
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
pr.update({n: E(n, s, INSERT_RGB) for n, s in paint.items()})
co = {n: E(n, s, rgb_for(n, COMP_COLORS, COMP_RGB)) for n, s in comps.items()}
ca = {n: E(n, s, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n, s in cabs.items()}
leg_parts = [(n, rgb_for(n, COMP_COLORS, COMP_RGB)) for n in comps if not any(k in n for k in ("범퍼", "ㄱ자", "멈춤", "암 단자", "발 나사"))]
leg_cables = [(n, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n in cabs]
outside = [co["7인치 LCD (C)"], co["실리콘 범퍼 ×4"], co["귀 멈춤 나사 M2.5 ×2"]]

render(f"{OUT}/renders/01-closed.png", "DeskPet 케이스 rev E — 둥근 외형 · 허브를 감춘 기운 귀 · 둥근 발 (분홍 = 도색 자리)",
       list(pr.values()) + outside, 20, -60)
render(f"{OUT}/renders/01b-front.png", "정면 — 화면 창 R18, 바깥 모서리 R10, 귀 간격 ±52",
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
           "P8 발 ×4": (0, 0, -95)}
render(f"{OUT}/renders/05-exploded.png", "출력물 분해도 — P1 바닥판 · P2 몸통 · P3 윗판 · P4 뒤판 · P5 트레이+문 · P6 귀 · P7 스피커 받침 · P8 발",
       [E(n, s, pr[n][2], explode[n]) for n, s in printed.items()], 18, -52, size=(13, 11))
pull = (0, 115, 0)
render(f"{OUT}/renders/06-board-return.png", "보드 반납 — 문 나사 2개를 풀고 트레이를 Jetson째 뒤로 뺀다 (뒤판·윗판은 그대로)",
       [v for k, v in pr.items() if k != "P5 트레이"]
       + [E("P5 트레이", printed["P5 트레이"], m.COLORS["P5"], pull),
          E("Jetson Orin Nano (받침대 포함)", comps["Jetson Orin Nano (받침대 포함)"], COMP_COLORS["Jetson"], pull)], 24, 135)

with open(f"{OUT}/check-report.txt", "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
sys.exit(1 if fails else 0)
