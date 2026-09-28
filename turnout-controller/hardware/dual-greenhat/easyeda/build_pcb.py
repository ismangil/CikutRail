"""Generate the first PCB placement for the dual-GreenHat carrier board.

Uses the parts and nets from build_schematic.py, the LCSC footprints cached
in fp/ (keyed by LCSC number), and the floorplan in PLACE below. Writes an
EasyEDA Standard PCB source with every footprint placed, every pad on its
net, the board outline, mounting holes and track-width rules. Routing is
left to EasyEDA (by hand or its autorouter).

    python build_pcb.py            # uses the fp/ cache only
    python build_pcb.py --fetch    # downloads missing footprints first

Outputs (next to this script):
    DualGreenHat_pcb.json   EasyEDA Standard PCB source
    placement.svg           top-view sketch of the placement, for review
"""

import json
import math
import re
import sys
import urllib.request
import uuid
from pathlib import Path

import build_schematic as sch

HERE = Path(__file__).resolve().parent
FP = HERE / "fp"
MM = 1 / 0.254          # EasyEDA PCB units are 10 mil
ORIGIN = (4000.0, 3000.0)

BOARD_W, BOARD_H = 70.0, 56.0          # mm
HOLES = [(46.0, 4.5), (66.5, 52.5), (33.5, 52.5)]  # M3, mm from top-left
HOLE_D = 3.2

# --------------------------------------------------------------------------
# Floorplan: ref -> (x mm, y mm, rotation deg). Board top-left is (0, 0),
# y grows downwards; rotation is clockwise as seen from the top.
#
#   top edge     J101 (ch1-3)  J201 (ch4-6)   [hole]   J1 12 V in
#   band 2       U102  U103  U202  U203 (DRV8313)      F1, D1, bulk
#   band 3       driver caps, bulk 22 uF               buck U1/L1 ...
#   bottom left  Stamp-S3Bat (J2 G-row on top, USB-C at the left edge)
#   bottom mid   U101/U201 74HCT86 + RC delays
# --------------------------------------------------------------------------

PLACE = {
    # connectors along the top edge (wires leave upwards)
    "J101": (10.0, 4.3, 0), "J201": (27.0, 4.3, 0), "J1": (62.0, 5.5, 0),
    # Stamp-S3Bat DIP: rows 15.24 mm apart; pin 1 at the right, pin 9 at
    # the left next to USB-C (rotation 180 puts pad 1 on the right)
    "J2": (13.16, 38.0, 180), "J3": (13.16, 53.24, 180),
    # power entry, right column
    "F1": (61.0, 13.0, 0), "D1": (61.0, 17.8, 180),
    "C1": (57.6, 22.2, 0), "C2": (64.8, 22.2, 0),
    # buck converter
    "U1": (60.0, 29.0, 0), "L1": (66.0, 29.5, 90),
    "C3": (54.5, 27.5, 90), "C4": (54.5, 32.0, 90), "C5": (57.0, 33.5, 0),
    "R1": (62.5, 33.5, 0), "C6": (62.5, 25.8, 0),
    "C7": (66.5, 35.5, 90), "C8": (66.5, 40.0, 90),
    "R2": (60.0, 36.0, 0), "R3": (60.0, 38.3, 0), "R4": (60.0, 41.5, 0),
}


def driver_block(u, x, c):
    """DRV8313 at (x, 15), rotated so pins 1-14 (charge pump, VM, coil
    outputs) face the terminals and pins 15-28 (logic) face the XORs.
    Pin 1 is then at the top right, pin 15 at the bottom left."""
    PLACE[u] = (x, 15.0, 180)
    PLACE[f"C{c}"] = (x + 4.3, 9.9, 90)      # CP1-CP2, next to pins 1-2
    PLACE[f"C{c+1}"] = (x + 2.9, 9.9, 90)    # VCP-VM, pins 3-4
    PLACE[f"C{c+2}"] = (x - 4.2, 20.1, 90)   # V3P3OUT, pin 15
    PLACE[f"C{c+3}"] = (x - 2.6, 9.9, 90)    # VM, pin 11
    PLACE[f"C{c+4}"] = (x + 1.9, 20.1, 90)   # second VM bypass (via to VM)


driver_block("U102", 8.0, 108)
driver_block("U103", 20.0, 113)
driver_block("U202", 32.0, 208)
driver_block("U203", 44.0, 213)
PLACE["C118"] = (14.0, 25.4, 0)
PLACE["C218"] = (38.0, 25.4, 0)


def xor_block(n, y):
    """74HCT86 and its three channels' RC parts, one row per channel."""
    PLACE[f"U{n+1}"] = (37.0, y + 3.5, 90)
    PLACE[f"C{n+7}"] = (32.2, y + 3.5, 90)
    for i in range(3):
        yy = y + 0.6 + 2.6 * i
        PLACE[f"R{n+1+i}"] = (42.6, yy, 0)   # pull-up
        PLACE[f"C{n+1+i}"] = (44.9, yy, 0)   # input filter
        PLACE[f"R{n+4+i}"] = (47.2, yy, 0)   # RC series
        PLACE[f"C{n+4+i}"] = (50.7, yy, 0)   # RC 10 uF


xor_block(100, 28.5)
xor_block(200, 40.0)

# Nets that carry coil or supply current get wider tracks.
POWER_NETS = ["V12_IN", "V12_F", "VM", "GND", "BUCK_SW", "5V_BUCK", "5VIN"] + [
    f"T{k}_{s}" for k in range(1, 7) for s in "AB"]

# --------------------------------------------------------------------------
# Footprints
# --------------------------------------------------------------------------


def load_fp(lcsc, fetch):
    f = FP / f"{lcsc}.json"
    if not f.exists():
        if not fetch:
            sys.exit(f"missing fp/{f.name}; run with --fetch")
        req = urllib.request.Request(sch.API.format(lcsc), headers={"User-Agent": "curl/8"})
        with urllib.request.urlopen(req) as r:
            pk = json.load(r)["result"]["packageDetail"]
        keep = {"uuid": pk["uuid"], "title": pk["title"], "lcsc": lcsc,
                "dataStr": {"head": pk["dataStr"]["head"], "shape": pk["dataStr"]["shape"]}}
        FP.mkdir(exist_ok=True)
        f.write_text(json.dumps({"result": keep}, indent=1, ensure_ascii=False), encoding="utf-8")
    return json.loads(f.read_text(encoding="utf-8"))["result"]


class Xf:
    """Rotate about the footprint origin, then move it to the target."""

    def __init__(self, ox, oy, tx, ty, rot):
        self.ox, self.oy, self.tx, self.ty = ox, oy, tx, ty
        self.rot = rot % 360
        a = math.radians(self.rot)
        self.c, self.s = round(math.cos(a), 12), round(math.sin(a), 12)

    def pt(self, x, y):
        dx, dy = x - self.ox, y - self.oy
        return self.tx + dx * self.c - dy * self.s, self.ty + dx * self.s + dy * self.c


def n(v):
    v = round(v, 4)
    return str(int(v)) if v == int(v) else str(v)


def xf_points(s, xf):
    v = [float(t) for t in s.split()]
    out = []
    for i in range(0, len(v) - 1, 2):
        x, y = xf.pt(v[i], v[i + 1])
        out += [n(x), n(y)]
    return " ".join(out)


_TOK = re.compile(r"[MLAZmlaz]|-?\d*\.?\d+(?:[eE]-?\d+)?")


def xf_path(p, xf):
    toks, out, cmd, buf = _TOK.findall(p), [], None, []

    def flush():
        if cmd in ("M", "L"):
            for i in range(0, len(buf) - 1, 2):
                x, y = xf.pt(buf[i], buf[i + 1])
                out.extend([n(x), n(y)])
        elif cmd == "A":
            for i in range(0, len(buf) - 6, 7):
                rx, ry, rotx, large, sweep, x, y = buf[i:i + 7]
                x, y = xf.pt(x, y)
                out.extend([n(rx), n(ry), n((rotx + xf.rot) % 360), n(large), n(sweep), n(x), n(y)])
        elif buf:
            raise ValueError(f"unhandled path command {cmd}")

    for t in toks:
        if t.isalpha():
            flush()
            cmd, buf = t.upper(), []
            out.append(cmd)
        else:
            buf.append(float(t))
    flush()
    return " ".join(out)


def place_fp(ref, key, pins, fp, x_mm, y_mm, rot, gid):
    head = fp["dataStr"]["head"]
    tx, ty = ORIGIN[0] + x_mm * MM, ORIGIN[1] + y_mm * MM
    xf = Xf(float(head["x"]), float(head["y"]), tx, ty, rot)
    out, pads = [], []
    for s in fp["dataStr"]["shape"]:
        f = s.split("~")
        k = f[0]
        if k == "SVGNODE":
            continue  # 3D model outline; EasyEDA re-attaches models by package
        if k == "PAD":
            x, y = xf.pt(float(f[2]), float(f[3]))
            f[2], f[3] = n(x), n(y)
            f[7] = pins.get(f[8], "")
            if f[10].strip():
                f[10] = xf_points(f[10], xf)
            if f[14].strip() and re.fullmatch(r"[-\d. ]+", f[14].strip()):
                f[14] = xf_points(f[14], xf)
            f[11] = n((float(f[11] or 0) + rot) % 360)
            f[19] = f"{n(x)},{n(y)}"
            pads.append((x, y, f[7]))
        elif k == "TRACK":
            f[4] = xf_points(f[4], xf)
        elif k in ("CIRCLE", "VIA", "HOLE"):
            x, y = xf.pt(float(f[1]), float(f[2]))
            f[1], f[2] = n(x), n(y)
            if k == "VIA":  # thermal vias inside a footprint belong to its GND pad
                f[4] = "GND"
        elif k == "ARC":
            f[4] = xf_path(f[4], xf)
        elif k == "SOLIDREGION":
            f[3] = xf_path(f[3], xf)
        elif k == "RECT":
            x0, y0 = float(f[1]), float(f[2])
            w, h = float(f[3]), float(f[4])
            corners = [xf.pt(x0, y0), xf.pt(x0 + w, y0 + h)]
            f[1], f[2] = n(min(c[0] for c in corners)), n(min(c[1] for c in corners))
            f[3], f[4] = (n(h), n(w)) if rot in (90, 270) else (n(w), n(h))
        else:
            raise ValueError(f"{ref}: unhandled footprint shape {k}")
        out.append(re.sub(r"(gge|rep)[0-9A-Za-z]+", lambda m: gid(), "~".join(f)))

    lcsc, value = sch.CAT[key]
    sym_head = sch.load_symbol(lcsc, False)["dataStr"]["head"]["c_para"]
    cp = {"package": fp["title"], "BOM_Supplier": "LCSC", "BOM_Supplier Part": lcsc,
          "BOM_Manufacturer": sym_head.get("Manufacturer", ""),
          "BOM_Manufacturer Part": sym_head.get("Manufacturer Part", ""),
          "BOM_JLCPCB Part Class": sym_head.get("JLCPCB Part Class", ""),
          "spicePre": re.sub(r"[^A-Z]", "", ref)[:1]}
    cpara = "`".join(f"{k}`{v}" for k, v in cp.items()) + "`"
    lib = (f"LIB~{n(tx)}~{n(ty)}~{cpara}~{rot}~~{gid()}~1~{fp['uuid']}~0~0~~yes~~")
    bx = bbox(out)
    # designator on the silkscreen above the part; EasyEDA draws the glyphs
    prefix = (f"TEXT~P~{n(bx[0])}~{n(bx[1] - 1.5)}~0.6~0~0~3~~4~{ref}~~~{gid()}~~0~pinpart")
    name = (f"TEXT~N~{n(bx[0])}~{n(bx[3] + 5)}~0.6~0~0~12~~3~{value}~~none~{gid()}~~0~pinpart")
    return "#@$".join([lib] + out + [prefix, name]), bx, pads


def bbox(shapes):
    xs, ys = [], []
    for s in shapes:
        f = s.split("~")
        k = f[0]
        if k == "PAD":
            cx, cy, w, h = map(float, f[2:6])
            r = max(w, h) / 2
            xs += [cx - r, cx + r]; ys += [cy - r, cy + r]
        elif k == "TRACK" and f[2] in ("3", "1", "2"):
            v = [float(t) for t in f[4].split()]
            xs += v[0::2]; ys += v[1::2]
    return min(xs), min(ys), max(xs), max(ys)


# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------


def outline():
    x0, y0 = ORIGIN
    x1, y1 = x0 + BOARD_W * MM, y0 + BOARD_H * MM
    pts = f"{n(x0)} {n(y0)} {n(x1)} {n(y0)} {n(x1)} {n(y1)} {n(x0)} {n(y1)} {n(x0)} {n(y0)}"
    return f"TRACK~1~10~~{pts}~gge_outline~0"


def build(fetch):
    counter = [0]

    def gid():
        counter[0] += 1
        return f"gge{counter[0]}"

    shapes, boxes, pad_nets = [outline()], {}, {}
    missing = [ref for ref, *_ in sch.PARTS if ref not in PLACE]
    if missing:
        sys.exit(f"no placement for {missing}")
    for ref, key, pins, _ in sch.PARTS:
        fp = load_fp(sch.CAT[key][0], fetch)
        x, y, rot = PLACE[ref]
        lib, bx, pads = place_fp(ref, key, pins, fp, x, y, rot, gid)
        shapes.append(lib)
        boxes[ref] = bx
        pad_nets[ref] = pads
    for hx, hy in HOLES:
        shapes.append(f"HOLE~{n(ORIGIN[0] + hx * MM)}~{n(ORIGIN[1] + hy * MM)}~"
                      f"{n(HOLE_D / 2 * MM)}~{gid()}~0")
    shapes.append(f"TEXT~L~{n(ORIGIN[0] + 2 * MM)}~{n(ORIGIN[1] + (BOARD_H - 1.5) * MM)}~0.6~0~0~3~~4~"
                  f"CikutRail dual GreenHat  TAPR OHL  based on IoTT GreenHat~~~{gid()}~~0~")

    power = {"trackWidth": n(0.8 * MM), "clearance": n(0.2 * MM),
             "viaHoleDiameter": n(0.8 * MM), "viaHoleD": n(0.4 * MM), "nets": POWER_NETS}
    doc = {
        "head": {"docType": "3", "editorVersion": "6.5.34", "newgId": True, "c_para": {},
                 "hasIdFlag": True, "x": n(ORIGIN[0]), "y": n(ORIGIN[1]),
                 "importFlag": 0, "transformList": "", "uuid": uuid.uuid4().hex},
        "canvas": (f"CA~1000~1000~#000000~yes~#FFFFFF~10~1000~1000~line~0.5~mm~1~45~"
                   f"visible~0.5~{n(ORIGIN[0])}~{n(ORIGIN[1])}~1~yes"),
        "shape": shapes,
        "layers": ["1~TopLayer~#FF0000~true~true~true~", "2~BottomLayer~#0000FF~true~false~true~",
                   "3~TopSilkLayer~#FFCC00~true~false~true~", "4~BottomSilkLayer~#66CC33~true~false~true~",
                   "5~TopPasteMaskLayer~#808080~true~false~true~", "6~BottomPasteMaskLayer~#800000~true~false~true~",
                   "7~TopSolderMaskLayer~#800080~true~false~true~0.3", "8~BottomSolderMaskLayer~#AA00FF~true~false~true~0.3",
                   "9~Ratlines~#6464FF~true~false~true~", "10~BoardOutLine~#FF00FF~true~false~true~",
                   "11~Multi-Layer~#C0C0C0~true~false~true~", "12~Document~#FFFFFF~true~false~true~",
                   "13~TopAssembly~#33CC99~false~false~false~", "14~BottomAssembly~#5555FF~false~false~false~",
                   "15~Mechanical~#F022F0~false~false~false~", "19~3DModel~#66CCFF~false~false~false~",
                   "99~ComponentShapeLayer~#00CCCC~false~false~false~0.4",
                   "100~LeadShapeLayer~#CC9999~false~false~false~",
                   "101~ComponentPolarityLayer~#66FFCC~false~false~false~"],
        "BBox": {"x": ORIGIN[0] - 10, "y": ORIGIN[1] - 10,
                 "width": BOARD_W * MM + 20, "height": BOARD_H * MM + 20},
        "DRCRULE": {"Default": {"trackWidth": n(0.254 * MM), "clearance": n(0.152 * MM),
                                "viaHoleDiameter": n(0.61 * MM), "viaHoleD": n(0.305 * MM)},
                    "Power": power, "isRealtime": True, "isDrcOnRoutingOrPlaceVia": False,
                    "checkObjectToCopperarea": True, "showDRCRangeLine": True},
        "routerRule": {"unit": "mm", "trackWidth": 0.254, "trackClearance": 0.152,
                       "viaHoleD": 0.305, "viaDiameter": 0.61, "routerLayers": [1, 2],
                       "smdClearance": 0.152,
                       "specialNets": [{"net": nn, "width": n(0.8 * MM) + "px",
                                        "clearance": n(0.2 * MM) + "px",
                                        "viaHoleD": n(0.4 * MM) + "px",
                                        "viaHoleDiameter": n(0.8 * MM) + "px"}
                                       for nn in POWER_NETS]},
        "preference": {"hideFootprints": "", "hideNets": ""},
        "netColors": {},
    }
    (HERE / "DualGreenHat_pcb.json").write_text(json.dumps(doc, ensure_ascii=False), encoding="utf-8")
    return boxes, pad_nets


def check(boxes):
    """Report parts whose pad/silk boxes overlap or leave the board."""
    gap = 0.25 * MM
    refs = sorted(boxes)
    problems = []
    x0, y0 = ORIGIN
    x1, y1 = x0 + BOARD_W * MM, y0 + BOARD_H * MM
    for r in refs:
        a = boxes[r]
        if a[0] < x0 or a[1] < y0 or a[2] > x1 or a[3] > y1:
            problems.append(f"{r} leaves the board")
    for i, r in enumerate(refs):
        for q in refs[i + 1:]:
            a, b = boxes[r], boxes[q]
            if a[0] < b[2] + gap and b[0] < a[2] + gap and a[1] < b[3] + gap and b[1] < a[3] + gap:
                problems.append(f"{r} overlaps {q}")
    for hx, hy in HOLES:
        cx, cy, rr = x0 + hx * MM, y0 + hy * MM, (HOLE_D / 2 + 1.5) * MM
        for r in refs:
            a = boxes[r]
            if a[0] < cx + rr and cx - rr < a[2] and a[1] < cy + rr and cy - rr < a[3]:
                problems.append(f"hole at ({hx}, {hy}) mm hits {r}")
    return problems


def preview(boxes, pad_nets):
    s = 8  # px per mm
    el = [f'<rect x="0" y="0" width="{BOARD_W*s}" height="{BOARD_H*s}" fill="#1a4d2e" stroke="#f0f"/>']
    # module body (18 x 29.8 mm) over the sockets, USB-C at the left
    el.append(f'<rect x="{1.7*s}" y="{36.6*s}" width="{29.8*s}" height="{18*s}" fill="none" '
              f'stroke="#9cf" stroke-dasharray="4 3"/>')
    el.append(f'<text x="{2.2*s}" y="{46*s}" fill="#9cf" font-size="11">USB-C</text>')
    el.append(f'<text x="{12*s}" y="{46.5*s}" fill="#9cf" font-size="11">Stamp-S3Bat (above sockets)</text>')
    for hx, hy in HOLES:
        el.append(f'<circle cx="{hx*s}" cy="{hy*s}" r="{HOLE_D/2*s}" fill="#000" stroke="#ccc"/>')
    for r, (a0, a1, a2, a3) in boxes.items():
        x, y = (a0 - ORIGIN[0]) / MM * s, (a1 - ORIGIN[1]) / MM * s
        w, h = (a2 - a0) / MM * s, (a3 - a1) / MM * s
        el.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" fill="#2e7d4f" '
                  f'stroke="#ffd54f" stroke-width="1"/>')
        fs = 9 if w > 40 else 6
        el.append(f'<text x="{x+w/2:.1f}" y="{y+h/2+3:.1f}" fill="#fff" font-size="{fs}" '
                  f'text-anchor="middle">{r}</text>')
        for px, py, _ in pad_nets[r]:
            el.append(f'<circle cx="{(px-ORIGIN[0])/MM*s:.1f}" cy="{(py-ORIGIN[1])/MM*s:.1f}" '
                      f'r="1.6" fill="#e0b050"/>')
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-8 -8 {BOARD_W*s+16} {BOARD_H*s+16}" '
           f'width="{BOARD_W*s+16}" height="{BOARD_H*s+16}" font-family="sans-serif">'
           f'<rect x="-8" y="-8" width="{BOARD_W*s+16}" height="{BOARD_H*s+16}" fill="#fff"/>'
           + "".join(el) + "</svg>")
    (HERE / "placement.svg").write_text(svg, encoding="utf-8")


if __name__ == "__main__":
    boxes, pads = build("--fetch" in sys.argv)
    preview(boxes, pads)
    problems = check(boxes)
    print(f"{len(boxes)} footprints on {BOARD_W:g} x {BOARD_H:g} mm")
    for p in problems:
        print("  !", p)
