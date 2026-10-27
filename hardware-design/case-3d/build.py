"""DeskPet 케이스 3D 모델을 만들고 검사한 뒤 STEP·STL·미리보기 그림을 내보낸다.

사용: python build.py [출력 폴더]      (기본: 이 폴더. 검사 실패가 있으면 exit 1)
검사: 부품끼리 겹침, 배선이 출력물을 뚫는지, 팬 위 공기층, 귀 회전 범위, Jetson 트레이 빼기, 프린터 출력 범위.
"""
import itertools, os, sys
import numpy as np
from build123d import Color, Compound, Pos, Rot, export_step, export_stl
import deskpet_case as m

xyz = lambda v: (v.X, v.Y, v.Z)

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
EPS = 0.5           # mm³ 이하는 면 접촉으로 본다
PLA = 1.24          # g/cm³

printed, comps, cabs = m.printed_parts(), m.components(), m.cables()
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

log("DeskPet 케이스 3D 모델 검사 (rev C 계획 치수, [추정] 값은 실측 후 deskpet_case.py 치수 표에서 수정)")
log(f"외형 {m.W} × {m.D} × {m.H} mm (귀 포함 높이 {max(s.bounding_box().max.Z for s in printed.values()):.0f}, 범퍼 5 mm 별도)")
log()

solids = {**printed, **comps}
check("출력물·부품끼리 겹침 없음", [(a, solids[a], b, solids[b]) for a, b in itertools.combinations(solids, 2)])
check("배선이 출력물을 뚫지 않음", [(c, s, p, ps) for c, s in cabs.items() for p, ps in printed.items()])
ko = m.keepouts()
check("Jetson 팬 위 공기층 30 mm 비어 있음", [(k, ks, n, s) for k, ks in ko.items() for n, s in {**solids, **cabs}.items()
                                             if not n.startswith("Jetson")])

# 귀 회전: 서보 축 기준 ±30°
ear_pairs = []
for i, (x, y) in enumerate(m.EARS):
    ear = printed[f"P6 {'왼쪽' if i == 0 else '오른쪽'} 귀"]
    for ang in (-30, -15, 15, 30):
        moved = Pos(x, y, 0) * (Rot(0, 0, ang) * (Pos(-x, -y, 0) * ear))
        for n, s in solids.items():
            if not n.startswith("P6") and "SG90" not in n:
                ear_pairs.append((f"귀{i + 1} {ang:+d}°", moved, n, s))
check("귀가 ±30° 돌 때 부딪힘 없음", ear_pairs)

# Jetson 트레이를 뒤로 뺄 때 지나가는 공간 (뒤판을 연 상태, 케이블 분리 후)
sweep = m.box(47.5, 53, m.T, 156.5, m.D + 1, m.JET["z0"] + m.JET["h"])
check("Jetson 트레이를 뒤로 뺄 수 있음", [("트레이 통로", sweep, n, s) for n, s in solids.items()
                                    if n not in ("P5 트레이", "P1 바닥판", "P4 뒤판") and not n.startswith("Jetson")])

# 프린터 출력 범위
log()
bed = sorted(m.BED)
log(f"출력 범위 (프린터 {m.BED[0]} × {m.BED[1]} × {m.BED[2]} mm로 가정) · 무게는 속 채움 100% 기준 상한")
for n, s in printed.items():
    dims = sorted(xyz(s.bounding_box().size))
    ok = all(d <= b for d, b in zip(dims, bed))
    if not ok:
        fails.append(f"출력 범위 초과: {n}")
    size = " × ".join(f"{d:.0f}" for d in xyz(s.bounding_box().size))
    log(f"  [{'PASS' if ok else 'FAIL'}] {n:14s} {size:>16s} mm  부피 {s.volume / 1000:6.1f} cm³  최대 {s.volume / 1000 * PLA:5.0f} g")

# 참고: LCD 옆 단자에 일자 플러그를 꽂으면 필요한 폭
lcd_x1 = comps["7인치 LCD (C)"].bounding_box().max.X
log()
log(f"참고: LCD 오른쪽 끝 x={lcd_x1:.1f}, 케이스 안쪽 벽 x={m.W - m.CASE['wall']}. 일자 HDMI 플러그(약 40) + 꺾임(약 20)이면 "
    f"x≈{lcd_x1 + 60:.0f}까지 필요 → 케이블 칸 안에 들어가지 않아 ㄱ자 HDMI 젠더·ㄱ자 micro USB 케이블로 그렸다.")
log()
log("결과: " + ("모든 검사 통과" if not fails else f"실패 {len(fails)}건"))

# ------------------------------------------------------------------ 내보내기
os.makedirs(f"{OUT}/stl", exist_ok=True)
os.makedirs(f"{OUT}/renders", exist_ok=True)
stl_names = {"P1 바닥판": "P1-base", "P2 몸통": "P2-body", "P3 윗판": "P3-top", "P4 뒤판": "P4-back",
             "P5 트레이": "P5-tray", "P6 왼쪽 귀": "P6-ear-x2", "P7 스피커 받침": "P7-speaker-cradle"}
for n, f in stl_names.items():
    s = printed[n]
    bb = s.bounding_box()
    export_stl(Pos(-bb.min.X, -bb.min.Y, -bb.min.Z) * s, f"{OUT}/stl/{f}.stl", tolerance=0.05, angular_tolerance=0.2)

def colored(name, shape, rgb):
    shape.label, shape.color = name, Color(*rgb)
    return shape

COMP_RGB = (0.35, 0.42, 0.52)
COMP_COLORS = {"Jetson": (0.49, 0.61, 0.77), "7인치 LCD": (0.12, 0.17, 0.24), "ReSpeaker": (0.91, 0.58, 0.48),
               "Adafruit": (0.30, 0.30, 0.32), "PCA9685": (0.73, 0.64, 0.89), "SG90": (0.20, 0.35, 0.70),
               "GPIO": (0.75, 0.25, 0.20), "DC 클립": (0.15, 0.15, 0.15), "퓨즈": (0.85, 0.20, 0.20),
               "실리콘": (0.25, 0.25, 0.25), "HDMI": (0.15, 0.15, 0.15), "micro USB": (0.15, 0.15, 0.15)}
CABLE_COLORS = {"HDMI": (0.1, 0.1, 0.1), "LCD micro": (0.25, 0.25, 0.3), "ReSpeaker USB": (0.95, 0.95, 0.95),
                "스피커": (0.85, 0.3, 0.3), "40핀": (0.55, 0.55, 0.85), "신호": (0.3, 0.7, 0.4),
                "왼쪽 귀": (0.9, 0.6, 0.1), "오른쪽 귀": (0.9, 0.6, 0.1), "서보 전원": (0.85, 0.1, 0.1)}

def rgb_for(name, table, default):
    return next((c for k, c in table.items() if k in name), default)

items = [(n, s, m.COLORS[n[:2]]) for n, s in printed.items()]
items += [(n, s, rgb_for(n, COMP_COLORS, COMP_RGB)) for n, s in comps.items()]
items += [(n, s, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n, s in cabs.items()]
asm = Compound(label="DeskPet case rev C", children=[colored(n, s, c) for n, s, c in items])
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
co = {n: E(n, s, rgb_for(n, COMP_COLORS, COMP_RGB)) for n, s in comps.items()}
ca = {n: E(n, s, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n, s in cabs.items()}
leg_parts = [(n, rgb_for(n, COMP_COLORS, COMP_RGB)) for n in comps if "범퍼" not in n and "ㄱ자" not in n and "micro" not in n]
leg_cables = [(n, rgb_for(n, CABLE_COLORS, (0.3, 0.3, 0.3))) for n in cabs]

render(f"{OUT}/renders/01-closed.png", "DeskPet 케이스 rev C — 조립 완성 (앞·오른쪽 위)",
       list(pr.values()) + [co["7인치 LCD (C)"], co["실리콘 범퍼 ×4"]], 22, -58)
render(f"{OUT}/renders/02-back.png", "뒤에서 본 모습 — Jetson 단자 창, 케이블 통과 구멍, DC 단자, 배기",
       list(pr.values()) + [co[n] for n in comps] + list(ca.values()), 20, 120)
inside = [v for k, v in pr.items() if k in ("P1 바닥판", "P5 트레이", "P7 스피커 받침")]
render(f"{OUT}/renders/03-inside-back-right.png", "내부 배치와 배선 (뒤·오른쪽 위) — 몸통·윗판·뒤판을 감춤",
       inside + list(co.values()) + list(ca.values()), 38, 48, legend=leg_parts + leg_cables, size=(14, 10))
render(f"{OUT}/renders/03b-inside-front-left.png", "내부 배치와 배선 (앞·왼쪽 위) — 왼쪽 벽 GPIO 분배보드, 리본·신호선",
       inside + list(co.values()) + list(ca.values()), 40, -125, size=(14, 10))
render(f"{OUT}/renders/04-top-plate-underside.png", "윗판(P3) 아래 — 귀 서보·ReSpeaker·PCA9685 (아래에서 올려다봄)",
       [pr["P3 윗판"], co["SG90 왼쪽 귀"], co["SG90 오른쪽 귀"], co["ReSpeaker Lite"], co["PCA9685 + 지지대"],
        ca["왼쪽 귀 서보선"], ca["오른쪽 귀 서보선"]], -55, -70)
explode = {"P1 바닥판": (0, 0, -60), "P2 몸통": (0, -40, 0), "P3 윗판": (0, 0, 70), "P4 뒤판": (0, 70, 0),
           "P5 트레이": (0, 0, -25), "P6 왼쪽 귀": (0, 0, 110), "P6 오른쪽 귀": (0, 0, 110), "P7 스피커 받침": (0, 0, -35)}
render(f"{OUT}/renders/05-exploded.png", "출력물 분해도 — P1 바닥판 · P2 몸통 · P3 윗판 · P4 뒤판 · P5 트레이 · P6 귀 · P7 스피커 받침",
       [E(n, s, m.COLORS[n[:2]], explode[n]) for n, s in printed.items()], 18, -52, size=(13, 11))

with open(f"{OUT}/check-report.txt", "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
sys.exit(1 if fails else 0)
