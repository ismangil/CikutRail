"""Generate the first PCB placement for the dual-GreenHat carrier board.

Uses the parts and nets from build_schematic.py, the LCSC footprints cached
in fp/ (keyed by LCSC number), and the floorplan in PLACE below. Writes an
EasyEDA Standard PCB source with every footprint placed, every pad on its
net, the board outline, mounting holes and track-width rules. Routing is
done in EasyEDA (by hand or its autorouter) and kept in routes.json,
which this script adds back to the board along with the GND pours.

    python build_pcb.py            # uses the fp/ cache only
    python build_pcb.py --fetch    # downloads missing footprints first
    python build_pcb.py --no-routes    # placement only, for a fresh autoroute
    python build_pcb.py --take FILE    # keep the routing exported from EasyEDA

Outputs (next to this script):
    DualGreenHat_pcb.json   EasyEDA Standard PCB source
    placement.svg           top-view sketch of the placement, for review
    print_1to1.svg          A4 sheet with the pads at true size, for
                            checking the module and connectors on paper
"""

import hashlib
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
#   top edge     J201 (ch6-4)  J101 (ch3-1)   [hole]   J1 12 V in
#   band 2       U203  U202  U103  U102 (DRV8313)      F1, D1, bulk
#   band 3       driver caps, bulk 22 uF               buck U1/L1 ...
#   bottom left  Stamp-S3Bat (J2 G-row on top, USB-C at the left edge)
#   bottom mid   U101/U201 74HCT86 + RC delays
# --------------------------------------------------------------------------

PLACE = {
    # connectors along the top edge (wires leave upwards)
    # each terminal sits over its two drivers' outputs; channels run 6..1
    # from left to right (see driver_block)
    "J201": (14.0, 4.3, 0), "J101": (35.0, 4.3, 0), "J1": (62.0, 5.5, 0),
    # Stamp-S3Bat DIP: rows 15.24 mm apart; pin 1 at the right, pin 9 at
    # the left next to USB-C (rotation 180 puts pad 1 on the right)
    "J2": (13.16, 38.0, 180), "J3": (13.16, 53.24, 180),
    # power entry, right column
    "F1": (61.0, 13.0, 0), "D1": (61.0, 17.8, 180),
    "C1": (57.6, 22.2, 0), "C2": (64.8, 22.2, 0),
    # Buck converter, U1 rotated 90: pins 1-3 (GND, SW, VIN) face down,
    # pins 4-6 (FB, EN, BST) face up. Each part sits at the pin it serves:
    # This follows the TPS563201 datasheet's layout example (figure 7-18):
    #   C3   straddles VIN and GND just below the pins (the switching
    #        loop), with C5 and C4 stacked below it on the same two columns
    #   SW   drops through a via in the gap between its pin and C3, and
    #        runs on the bottom layer to L1 and to C6 (bootstrap). Don't
    #        route it on the top layer under U1 (datasheet 7.4.1, rule 5)
    #   R2/R3 (feedback) and R1 (EN) directly above their pins
    #   C7/C8 at L1's output pad; R4 next to them; R5 feeds VM_BUCK from VM
    "U1": (59.5, 29.5, 90), "L1": (65.4, 29.5, 0),
    "C3": (59.5, 33.3, 180), "C5": (59.5, 35.1, 180), "C4": (59.5, 36.9, 180),
    "C6": (56.9, 29.0, 90),
    "R1": (59.1, 26.2, 90), "R2": (60.6, 25.7, 90), "R3": (62.1, 26.1, 270),
    "C7": (66.5, 33.0, 180), "C8": (66.5, 35.5, 180), "R4": (66.5, 37.8, 180),
    "R5": (62.75, 35.3, 270),
    # Sensors and I2C. J4-J8 are upright 3-pin 2.54 mm headers, paired
    # 2.8 mm apart so two plugs fit side by side. J9 (Qwiic) is a side-entry
    # JST SH: rotation 0 opens towards the bottom edge.
    #   3.3 V regulator below the buck, input cap next to VIN
    "U2": (58.5, 44.5, 0), "C10": (55.8, 39.4, 0), "C9": (60.6, 39.4, 0),
    #   left edge, above the module's USB-C end: G7 and G8, GND at the top
    "J4": (3.5, 30.0, 90), "R6": (9.4, 31.0, 0),
    "J5": (6.3, 30.0, 90), "R7": (9.4, 29.0, 0),
    #   right edge: G9, GND at the bottom
    "J6": (66.5, 45.2, 270), "R8": (66.5, 40.3, 0),
    #   bottom edge, between the mounting holes: G10 above G11 (GND at the
    #   left), then Qwiic
    "J7": (45.0, 49.85, 0), "J8": (45.0, 52.65, 0), "J9": (54.5, 52.5, 0),
    "R9": (59.6, 49.6, 0), "R10": (59.6, 50.9, 0),
    "R11": (61.9, 49.6, 0), "R12": (61.9, 50.9, 0), "C11": (59.6, 52.2, 0),
}


# A "G" on the silkscreen beside pin 1 (GND) of each sensor header, which
# isn't keyed: -1 puts it to the left of the header, 1 to the right.
GND_MARK = {"J4": -1, "J5": 1, "J6": 1, "J7": -1, "J8": -1}

# Board title and licence notice on the silkscreen: (x mm, y mm, text), the
# anchor being the text's bottom-left corner.
TITLE = [(17.5, 25.5 + 1.5 * i, t) for i, t in enumerate(
    ["CikutRail", "dual GreenHat", "TAPR OHL", "based on IoTT", "GreenHat"])]

# Designators are placed automatically (place_labels). A part listed here
# gets its designator on that side only.
LABEL_SIDE = {"J201": "left", "J101": "left"}


def driver_block(u, x, c):
    """DRV8313 at (x, 15), rotated so pins 1-14 (charge pump, VM, coil
    outputs) face the terminals and pins 15-28 (logic) face the XORs.
    Pin 1 is then at the top right, pin 15 at the bottom left, so the
    outputs run OUT3, OUT2, OUT1 from left to right. The drivers are
    therefore placed in reverse order (U203, U202, U103, U102) and the
    terminals numbered to match: every coil trace runs straight up to its
    terminal pin without crossing another."""
    PLACE[u] = (x, 15.0, 180)
    PLACE[f"C{c}"] = (x + 4.3, 9.9, 90)      # CP1-CP2, next to pins 1-2
    PLACE[f"C{c+1}"] = (x + 2.9, 9.9, 90)    # VCP-VM, pins 3-4
    PLACE[f"C{c+2}"] = (x - 4.2, 20.1, 90)   # V3P3OUT, pin 15
    PLACE[f"C{c+3}"] = (x - 2.6, 9.9, 90)    # VM, pin 11
    PLACE[f"C{c+4}"] = (x + 1.9, 20.1, 90)   # second VM bypass (via to VM)


driver_block("U203", 8.0, 213)
driver_block("U202", 20.0, 208)
driver_block("U103", 32.0, 113)
driver_block("U102", 44.0, 108)
PLACE["C218"] = (14.0, 25.4, 0)
PLACE["C118"] = (38.0, 25.4, 0)


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

# Nets that carry coil or supply current get wider tracks. The width is
# limited by the narrowest pad a net has to reach:
#   Supply  12 V input, only large pads (J1, F1, D1)
#   Buck    the buck converter's nets, down to U1's SOT-23-6 pins (0.95 mm
#           pitch, 0.6 mm pads)
#   Power   everything that reaches a DRV8313 pin (0.65 mm pitch): 0.4 mm
#           is the widest track that fits. Widen the main runs by hand, or
#           use pours, where current matters.
NET_CLASSES = {
    "Supply": (1.0, ["V12_IN", "V12_F"]),
    "Buck": (0.6, ["VM_BUCK", "BUCK_SW", "5V_BUCK", "5VIN"]),
    "Power": (0.4, ["VM", "GND", "3V3_S"] + [f"T{k}_{s}" for k in range(1, 7) for s in "AB"]),
}
CLEARANCE = 0.152                      # mm, what the autorouter is asked to keep, on every net
DRC_CLEARANCE = 0.127                  # mm (5 mil), what DRC checks: the router lands up to 0.02 mm
                                       # under its target. JLCPCB's minimum spacing is 0.1 mm.
ROUTES = HERE / "routes.json"          # tracks and vias kept from EasyEDA, see load_routes

# Routes drawn here and not by the autorouter, in mm from the board's
# top-left. EasyEDA's cloud router rips up tracks it finds on a net it is
# routing, so they go in twice:
#   --no-routes   as solid copper regions on their net, which the router
#                 leaves alone and routes the other nets around;
#   --take FILE   as real tracks and vias, added to what the router made.
# The router still routes these nets itself. Its VM tracks are kept, in
# parallel with the bus; its tracks on the nets in PRE_ONLY are dropped.
#   track: (net, layer, width, [(x, y), ...])   layer 1 top, 2 bottom
#   via:   (net, x, y)
PRE_ONLY = {"BUCK_SW"}
PRE_TRACKS = [
    # VM: D1's cathode down to C2, two vias, then 1.5 mm on the bottom layer
    # to a bus that runs under the drivers' VM bypass capacitors.
    ("VM", 1, 1.5, [(63.2, 17.8), (63.2, 22.2)]),
    ("VM", 2, 1.5, [(63.7, 20.0), (52.0, 20.0), (52.0, 8.6), (5.4, 8.6)]),
    ("VM", 1, 0.6, [(56.01, 22.2), (56.01, 20.0)]),            # C1 to the feed
    # Buck SW: pin 2 to a via in the gap above C3, along the bottom layer,
    # and up again beside L1 (TPS563201 datasheet 7.4.1, rule 5).
    ("BUCK_SW", 1, 0.6, [(59.5, 30.85), (59.5, 32.0)]),
    ("BUCK_SW", 2, 0.6, [(59.5, 32.0), (61.0, 30.5), (61.95, 30.5)]),
    ("BUCK_SW", 1, 0.6, [(61.95, 30.5), (63.8, 30.5)]),
    # and on along the bottom layer to the bootstrap capacitor C6
    ("BUCK_SW", 2, 0.6, [(59.5, 32.0), (57.4, 32.0), (56.0, 30.6), (56.0, 29.42)]),
    ("BUCK_SW", 1, 0.6, [(56.0, 29.42), (56.9, 29.42)]),
]
PRE_VIAS = [
    ("VM", 62.7, 20.0), ("VM", 63.7, 20.0), ("VM", 56.01, 20.0),
    ("BUCK_SW", 59.5, 32.0), ("BUCK_SW", 61.95, 30.5), ("BUCK_SW", 56.0, 29.42),
]
# Each DRV8313: pins 1-4 (CP1, CP2, VCP, VM) and their two capacitors. The
# router tends to wrap VCP round the VM pad and shut pin 4 in, so these four
# short connections are drawn: VCP down the gap between the capacitors, VM
# straight from its capacitor to pin 4. The pin-11 capacitor's VM pad gets a
# via to the bus just above it. x is the driver's centre (see driver_block).
for _u, _x in (("U203", 8.0), ("U202", 20.0), ("U103", 32.0), ("U102", 44.0)):
    PRE_ONLY |= {f"{_u}_CP1", f"{_u}_CP2", f"{_u}_VCP"}
    PRE_TRACKS += [
        (f"{_u}_CP1", 1, 0.254, [(_x + 4.3, 9.48), (_x + 4.82, 9.48), (_x + 4.89, 9.55),
                                 (_x + 4.89, 11.47), (_x + 4.23, 12.13)]),
        (f"{_u}_CP2", 1, 0.254, [(_x + 3.58, 12.13), (_x + 3.58, 11.3), (_x + 4.05, 10.82),
                                 (_x + 4.3, 10.82), (_x + 4.3, 10.32)]),
        (f"{_u}_VCP", 1, 0.254, [(_x + 2.9, 9.48), (_x + 3.6, 9.48), (_x + 3.6, 10.6),
                                 (_x + 2.93, 11.27), (_x + 2.93, 11.6)]),
        ("VM", 1, 0.4, [(_x + 2.9, 10.32), (_x + 2.28, 10.94), (_x + 2.28, 11.6)]),
        ("VM", 1, 0.4, [(_x - 2.6, 9.48), (_x - 2.6, 8.85)]),
    ]
    PRE_VIAS += [("VM", _x - 2.6, 8.85)]


def pre_shapes(gid, reserve):
    """The PRE_ routes as EasyEDA shapes: real tracks and vias, or (reserve)
    solid regions covering the same copper on the same nets."""
    U = lambda x, y: (ORIGIN[0] + x * MM, ORIGIN[1] + y * MM)
    out = []
    for net, layer, w, pts in PRE_TRACKS:
        if not reserve:
            xy = " ".join(f"{n(a)} {n(b)}" for a, b in (U(*p) for p in pts))
            out.append(f"TRACK~{n(w * MM)}~{layer}~{net}~{xy}~{gid()}~0")
            continue
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):   # one rectangle per segment, ends extended by w/2
            d = math.hypot(x1 - x0, y1 - y0)
            ux, uy = (x1 - x0) / d * w / 2, (y1 - y0) / d * w / 2
            c = [(x0 - ux + uy, y0 - uy - ux), (x1 + ux + uy, y1 + uy - ux),
                 (x1 + ux - uy, y1 + uy + ux), (x0 - ux - uy, y0 - uy + ux)]
            path = "M " + " L ".join(f"{n(a)} {n(b)}" for a, b in (U(*p) for p in c)) + " Z"
            out.append(f"SOLIDREGION~{layer}~{net}~{path}~solid~{gid()}~~~~0")
    for net, x, y in PRE_VIAS:
        if not reserve:
            cx, cy = U(x, y)
            out.append(f"VIA~{n(cx)}~{n(cy)}~{n(0.6 * MM)}~{net}~{n(0.15 * MM)}~{gid()}~0")
            continue
        c = [(x - 0.3, y - 0.3), (x + 0.3, y - 0.3), (x + 0.3, y + 0.3), (x - 0.3, y + 0.3)]
        path = "M " + " L ".join(f"{n(a)} {n(b)}" for a, b in (U(*p) for p in c)) + " Z"
        out += [f"SOLIDREGION~{layer}~{net}~{path}~solid~{gid()}~~~~0" for layer in (1, 2)]
    return out


def take(path):
    """Write routes.json from the tracks and vias exported from EasyEDA
    after an autoroute of the --no-routes board."""
    got = json.loads(Path(path).read_text(encoding="utf-8"))
    got = [s for s in got if s.split("~")[0] in ("TRACK", "VIA")
           and s.split("~")[3 if s.startswith("TRACK") else 4] not in PRE_ONLY]
    counter = [0]

    def gid():
        counter[0] += 1
        return f"ggepre{counter[0]}"
    doc = {"placement": placement_key(),
           "source": "EasyEDA Standard cloud autorouter, plus the PRE_ routes in build_pcb.py",
           "shapes": sorted(got) + pre_shapes(gid, False)}
    ROUTES.write_text(json.dumps(doc, indent=1) + chr(10), encoding="utf-8", newline=chr(10))
    print(f"routes.json: {len(got)} from the router, {len(doc['shapes']) - len(got)} drawn here")


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
    name = (f"TEXT~N~{n(bx[0])}~{n(bx[3] + 5)}~0.6~0~0~12~~3~{value}~~none~{gid()}~~0~pinpart")
    # the designator text is added by build(), once every part is in place
    return [lib] + out, name, bx, bbox(out, tight=True), pads


def bbox(shapes, tight=False):
    """Extent of a footprint's pads and outline. The default takes each pad
    as a circle of its longer side (a margin for the overlap check); tight
    takes the pad's real rectangle, for placing silkscreen text."""
    xs, ys = [], []
    for s in shapes:
        f = s.split("~")
        k = f[0]
        if k == "PAD":
            cx, cy, w, h = map(float, f[2:6])
            pts = [float(t) for t in f[10].split()] if f[10].strip() else []
            if tight and len(pts) >= 6:
                xs += pts[0::2]; ys += pts[1::2]
                continue
            if tight and round(float(f[11] or 0)) % 180 == 90:
                w, h = h, w
            rx, ry = (w / 2, h / 2) if tight else (max(w, h) / 2,) * 2
            xs += [cx - rx, cx + rx]; ys += [cy - ry, cy + ry]
        elif k == "TRACK" and f[2] in ("3", "1", "2"):
            v = [float(t) for t in f[4].split()]
            xs += v[0::2]; ys += v[1::2]
    return min(xs), min(ys), max(xs), max(ys)


# --------------------------------------------------------------------------
# Silkscreen text
#
# EasyEDA draws the glyphs itself, so the sizes here are measured from a
# board it imported: at font size 4 (1 mm) a character advances at most
# 3.6 units and the glyphs reach from 4.2 to 0.2 units above the anchor.
# --------------------------------------------------------------------------

TEXT_GAP = 0.2 * MM      # between a label and anything else on the silkscreen


def text_box(x, y, text, rot=0):
    """Box of a silkscreen text anchored at (x, y), stroke width included.
    Rotation 90 is EasyEDA's: the text turns about its anchor and reads
    from the bottom of the board upwards."""
    w = 3.6 * len(text)
    if rot == 90:
        return (x - 4.5, y - w - 0.1, x + 0.1, y + 0.3)
    return (x - 0.3, y - 4.5, x + w + 0.1, y + 0.1)


def box_gap(a, b):
    """Distance between two boxes; negative when they overlap."""
    dx = max(a[0] - b[2], b[0] - a[2])
    dy = max(a[1] - b[3], b[1] - a[3])
    return math.hypot(max(dx, 0), max(dy, 0)) if dx > 0 or dy > 0 else max(dx, dy)


def place_labels(tight, fixed, vias):
    """Find a place for each part's designator: beside its own part, clear
    of every part, hole, fixed text and other designator, and nearer to
    its own part than to any other. Returns {ref: (x, y, rotation)}; a
    part with no such place is left out, and its designator is hidden."""
    x0, y0 = ORIGIN[0] + 0.4 * MM, ORIGIN[1] + 0.4 * MM
    x1, y1 = ORIGIN[0] + (BOARD_W - 0.4) * MM, ORIGIN[1] + (BOARD_H - 0.4) * MM
    holes = [(ORIGIN[0] + (hx - HOLE_D / 2) * MM, ORIGIN[1] + (hy - HOLE_D / 2) * MM,
              ORIGIN[0] + (hx + HOLE_D / 2) * MM, ORIGIN[1] + (hy + HOLE_D / 2) * MM) for hx, hy in HOLES]
    taken = list(fixed) + holes
    placed = {}
    g, step = TEXT_GAP, 0.25 * MM

    def span(lo, hi):
        return [lo + step * i for i in range(int((hi - lo) / step) + 1)] + [hi]

    # connectors and ICs first: those are the ones looked for on the board
    rank = {"J": 0, "U": 1, "F": 2, "D": 2, "L": 2}
    for ref in sorted(tight, key=lambda r: (rank.get(r[0], 3), r)):
        a = tight[ref]
        cx, cy = (a[0] + a[2]) / 2, (a[1] + a[3]) / 2
        others = [tight[q] for q in tight if q != ref]
        best = None
        for rot in (0, 90):
            w, h = 3.6 * len(ref) + 0.4, 4.6
            if rot:
                w, h = h, w
            for e in (g, g + 0.5 * MM, g + 1.0 * MM, g + 1.5 * MM):   # distance from the part
                sides = {"above": [(x, a[1] - e - h) for x in span(a[0] - w, a[2])],
                         "below": [(x, a[3] + e) for x in span(a[0] - w, a[2])],
                         "left": [(a[0] - e - w, y) for y in span(a[1] - h, a[3])],
                         "right": [(a[2] + e, y) for y in span(a[1] - h, a[3])]}
                cands = sides[LABEL_SIDE[ref]] if ref in LABEL_SIDE else sum(sides.values(), [])
                for bx0, by0 in cands:
                    b = (bx0, by0, bx0 + w, by0 + h)
                    if b[0] < x0 or b[1] < y0 or b[2] > x1 or b[3] > y1:
                        continue
                    # a via hole under the text breaks the glyphs: use such
                    # a place only when there is no other
                    cost = (math.hypot((b[0] + b[2]) / 2 - cx, (b[1] + b[3]) / 2 - cy)
                            + (0.3 * MM if rot else 0)
                            + 10 * MM * sum(1 for v in vias if box_gap(b, v) < 0))
                    if best and cost >= best[0]:
                        continue
                    own = box_gap(b, a)
                    if any(box_gap(b, t) < g for t in taken):
                        continue
                    if any(box_gap(b, o) < own + 0.1 * MM for o in others):
                        continue
                    best = (cost, b, rot)
        if best:
            _, b, rot = best
            taken.append(b)
            placed[ref] = (b[2] - 0.1, b[3] - 0.3, 90) if rot else (b[0] + 0.3, b[3] - 0.1, 0)
    return placed


# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------


def outline():
    x0, y0 = ORIGIN
    x1, y1 = x0 + BOARD_W * MM, y0 + BOARD_H * MM
    pts = f"{n(x0)} {n(y0)} {n(x1)} {n(y0)} {n(x1)} {n(y1)} {n(x0)} {n(y1)} {n(x0)} {n(y0)}"
    return f"TRACK~1~10~~{pts}~gge_outline~0"


def placement_key():
    """Changes whenever a part moves or a pad changes net, which is when
    kept routing stops being valid."""
    parts = [(ref, key, sorted(pins.items()), PLACE.get(ref)) for ref, key, pins, _ in sch.PARTS]
    return hashlib.sha1(repr(parts).encode()).hexdigest()[:12]


def load_routes():
    """Tracks and vias routed in EasyEDA and saved back to routes.json, as
    EasyEDA shape strings. They only fit the placement they were routed on,
    so they are dropped (with a warning) when the placement has changed."""
    if not ROUTES.exists():
        return []
    r = json.loads(ROUTES.read_text(encoding="utf-8"))
    if r.get("placement") != placement_key():
        print("  ! routes.json was routed on a different placement; not used")
        return []
    return r["shapes"]


def build(fetch, routes=True):
    counter = [0]

    def gid():
        counter[0] += 1
        return f"gge{counter[0]}"

    shapes, boxes, pad_nets = [outline()], {}, {}
    missing = [ref for ref, *_ in sch.PARTS if ref not in PLACE]
    if missing:
        sys.exit(f"no placement for {missing}")
    libs, tight = {}, {}
    for ref, key, pins, _ in sch.PARTS:
        fp = load_fp(sch.CAT[key][0], fetch)
        x, y, rot = PLACE[ref]
        pieces, name, bx, tbx, pads = place_fp(ref, key, pins, fp, x, y, rot, gid)
        libs[ref] = (pieces, name)
        boxes[ref] = bx
        tight[ref] = tbx
        pad_nets[ref] = pads
    kept = load_routes() if routes else []

    # Silkscreen text. The fixed texts go down first, then the designators
    # are fitted around them (place_labels).
    vias = []
    for sh in kept + [v for v in pre_shapes(gid, False) if v.startswith("VIA")]:
        f = sh.split("~")
        if f[0] == "VIA":
            r = float(f[5])
            vias.append((float(f[1]) - r, float(f[2]) - r, float(f[1]) + r, float(f[2]) + r))
    marks, fixed, texts = [], [], []
    for ref, side in GND_MARK.items():
        px, py = next((x, y) for x, y, net in pad_nets[ref] if net == "GND")
        tx, ty = px + (1.7 if side > 0 else -2.6) * MM, py + 0.5 * MM
        texts.append((tx, ty, "G"))
        marks.append((tx, ty))
    for x_mm, y_mm, t in TITLE:
        texts.append((ORIGIN[0] + x_mm * MM, ORIGIN[1] + y_mm * MM, t))
    for tx, ty, t in texts:
        b = text_box(tx, ty, t)
        hit = ([q for q in tight if box_gap(b, tight[q]) < TEXT_GAP]
               + [t2 for t2, b2 in fixed if box_gap(b, b2) < TEXT_GAP]
               + ["a via"] * any(box_gap(b, v) < 0 for v in vias))
        if hit:
            print(f"  ! silkscreen text '{t}' runs into {hit}")
        fixed.append((t, b))
    labels = place_labels(tight, [b for _, b in fixed], vias)
    hidden = sorted(r for r in tight if r not in labels)
    if hidden:
        print(f"  designator not printed, no room ({len(hidden)}): {' '.join(hidden)}")
    for ref, (pieces, name) in libs.items():
        bx = boxes[ref]
        lx, ly, lrot = labels.get(ref, (bx[0], bx[1] - 1.5, 0))
        show = "" if ref in labels else "none"
        prefix = f"TEXT~P~{n(lx)}~{n(ly)}~0.6~{lrot}~0~3~~4~{ref}~~{show}~{gid()}~~0~pinpart"
        shapes.append("#@$".join(pieces + [prefix, name]))
    for hx, hy in HOLES:
        shapes.append(f"HOLE~{n(ORIGIN[0] + hx * MM)}~{n(ORIGIN[1] + hy * MM)}~"
                      f"{n(HOLE_D / 2 * MM)}~{gid()}~0")
    shapes += [re.sub(r"gge[0-9A-Za-z]+", lambda m: gid(), s) for s in kept]
    if not routes:   # for the autorouter: reserve the copper of the routes drawn here
        shapes += pre_shapes(gid, True)
    if kept:
        # GND pour on both layers, 0.3 mm inside the outline, and cut back
        # 0.5 mm from the Stamp-S3Bat's outline: no pour under the module,
        # for its antenna. EasyEDA fills the pour when the board is opened.
        (mx, my, mw, mh), _ = module_outline(pad_nets)
        pts = [(0.3, 0.3), (BOARD_W - 0.3, 0.3), (BOARD_W - 0.3, BOARD_H - 0.3),
               (mx + mw + 0.5, BOARD_H - 0.3), (mx + mw + 0.5, my - 0.5), (0.3, my - 0.5)]
        path = "M " + " L ".join(f"{n(ORIGIN[0] + x * MM)} {n(ORIGIN[1] + y * MM)}" for x, y in pts) + " Z"
        for layer in (1, 2):
            shapes.append(f"COPPERAREA~1~{layer}~GND~{path}~1~solid~{gid()}~spoke~none~[]~0")
    for tx, ty, t in texts:
        shapes.append(f"TEXT~L~{n(tx)}~{n(ty)}~0.6~0~0~3~~4~{t}~~~{gid()}~~0~")

    # Via rules are 0.6 / 0.3 mm on every net: the DRV8313 footprint's own
    # thermal vias are that size, and a larger rule makes EasyEDA's DRC flag
    # all 32 of them.
    classes = {name: {"trackWidth": n(w * MM), "clearance": n(DRC_CLEARANCE * MM),
                      "viaHoleDiameter": n(0.6 * MM), "viaHoleD": n(0.3 * MM), "nets": nets}
               for name, (w, nets) in NET_CLASSES.items()}
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
        "DRCRULE": {"Default": {"trackWidth": n(0.254 * MM), "clearance": n(DRC_CLEARANCE * MM),
                                "viaHoleDiameter": n(0.6 * MM), "viaHoleD": n(0.3 * MM)},
                    **classes, "isRealtime": True, "isDrcOnRoutingOrPlaceVia": False,
                    "checkObjectToCopperarea": True, "showDRCRangeLine": True},
        "routerRule": {"unit": "mm", "trackWidth": 0.254, "trackClearance": CLEARANCE,
                       "viaHoleD": 0.3, "viaDiameter": 0.6, "routerLayers": [1, 2],
                       "smdClearance": CLEARANCE,
                       "specialNets": [{"net": nn, "width": n(w * MM) + "px",
                                        "clearance": n(CLEARANCE * MM) + "px",
                                        "viaHoleD": n(0.3 * MM) + "px",
                                        "viaHoleDiameter": n(0.6 * MM) + "px"}
                                       for w, nets in NET_CLASSES.values() for nn in nets]},
        "preference": {"hideFootprints": "", "hideNets": ""},
        "netColors": {},
    }
    (HERE / "DualGreenHat_pcb.json").write_text(json.dumps(doc, ensure_ascii=False), encoding="utf-8")
    return boxes, pad_nets, shapes, marks


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
    # Module over the sockets, USB-C at the left. From M5Stack's footprint:
    # the module's PCB is 18 x 28.0 mm and starts 1.3 mm outside pin 9; the
    # USB-C shell (9 mm wide) sticks out another 1.85 mm, which makes the
    # 29.8 mm overall length and brings the shell to the carrier's edge.
    px0 = min(p[0] for p in pad_nets["J2"])
    mx0 = (px0 - ORIGIN[0]) / MM - 1.3
    my0 = (pad_nets["J2"][0][1] - ORIGIN[1]) / MM - 1.38
    el.append(f'<rect x="{mx0*s:.1f}" y="{my0*s:.1f}" width="{28.0*s}" height="{18*s}" fill="none" '
              f'stroke="#9cf" stroke-dasharray="4 3"/>')
    el.append(f'<rect x="{(mx0-1.85)*s:.1f}" y="{(my0+4.5)*s:.1f}" width="{1.85*s}" height="{9*s}" '
              f'fill="none" stroke="#9cf" stroke-dasharray="4 3"/>')
    el.append(f'<text x="{2.2*s}" y="{46*s}" fill="#9cf" font-size="11">USB-C</text>')
    el.append(f'<text x="{9*s}" y="{46.5*s}" fill="#9cf" font-size="11">Stamp-S3Bat (above sockets)</text>')
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


def module_outline(pad_nets):
    """Stamp-S3Bat over the sockets, in mm from the board's top-left: the
    module's PCB (28.0 x 18 mm, starting 1.3 mm outside pin 9) and its
    USB-C shell (9 mm wide, 1.85 mm beyond the PCB)."""
    mx0 = (min(p[0] for p in pad_nets["J2"]) - ORIGIN[0]) / MM - 1.3
    my0 = (pad_nets["J2"][0][1] - ORIGIN[1]) / MM - 1.38
    return (mx0, my0, 28.0, 18.0), (mx0 - 1.85, my0 + 4.5, 1.85, 9.0)


def print_sheet(boxes, pad_nets, shapes, marks):
    """A4 page in millimetres with the board at true size: pads, holes,
    silkscreen outlines and designators. Print at 100 % ("actual size")
    and check the 50 mm rulers before trusting it."""
    ox, oy = 30.0, 40.0                      # board's top-left on the page

    # Everything is written in page millimetres, with no transform: some
    # viewers mis-draw a scaled group with offsets in the thousands.
    def X(v):
        return round(ox + (float(v) - ORIGIN[0]) / MM, 3)

    def Y(v):
        return round(oy + (float(v) - ORIGIN[1]) / MM, 3)

    def P(pts):
        return " ".join(f"{X(pts[i])},{Y(pts[i + 1])}" for i in range(0, len(pts) - 1, 2))

    def L(v):
        return round(float(v) / MM, 3)

    el = []
    for s in shapes:
        for sub in s.split("#@$"):
            f = sub.split("~")
            if f[0] == "PAD":
                cx, cy, w, h = map(float, f[2:6])
                pts = [float(t) for t in f[10].split()] if f[10].strip() else []
                if f[1] == "OVAL" and len(pts) == 4:
                    el.append(f'<line x1="{X(pts[0])}" y1="{Y(pts[1])}" x2="{X(pts[2])}" y2="{Y(pts[3])}" '
                              f'stroke="#000" stroke-width="{L(min(w, h))}" stroke-linecap="round"/>')
                elif len(pts) >= 6:
                    el.append(f'<polygon points="{P(pts)}" fill="#000"/>')
                else:
                    el.append(f'<circle cx="{X(cx)}" cy="{Y(cy)}" r="{L(w / 2)}" fill="#000"/>')
                if float(f[9] or 0):
                    el.append(f'<circle cx="{X(cx)}" cy="{Y(cy)}" r="{L(f[9])}" fill="#fff"/>')
            elif f[0] == "TRACK" and f[2] == "3":
                el.append(f'<polyline points="{P([float(t) for t in f[4].split()])}" fill="none" '
                          f'stroke="#888" stroke-width="0.1"/>')
            elif f[0] == "HOLE":
                x, y, r = X(f[1]), Y(f[2]), L(f[3])
                el.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="none" stroke="#000" stroke-width="0.1"/>'
                          f'<path d="M{round(x - r, 3)} {y}h{2 * r}M{x} {round(y - r, 3)}v{2 * r}" '
                          f'stroke="#000" stroke-width="0.08"/>')
    for tx, ty in marks:
        el.append(f'<text x="{X(tx)}" y="{Y(ty)}" font-size="1.3" fill="#c00">G</text>')
    for r, (a0, a1, a2, a3) in boxes.items():
        if r[0] in "RC":
            continue  # label the parts worth finding on paper, not the passives
        el.append(f'<text x="{X((a0 + a2) / 2)}" y="{round(Y(a1) - 0.15, 3)}" font-size="1.5" fill="#06c" '
                  f'text-anchor="middle">{r}</text>')
    el.append(f'<rect x="{ox}" y="{oy}" width="{BOARD_W}" height="{BOARD_H}" '
              f'fill="none" stroke="#000" stroke-width="0.1"/>')
    board = "".join(el)
    mod = "".join(f'<rect x="{ox + x:.2f}" y="{oy + y:.2f}" width="{w}" height="{h}" fill="none" '
                  f'stroke="#06c" stroke-width="0.15" stroke-dasharray="1 0.6"/>'
                  for x, y, w, h in module_outline(pad_nets))

    def ruler(x, y, dx, dy):
        tick = "".join(f'<path d="M{x + dx * i} {y + dy * i}{"v" if dx else "h"}{-2 if i % 5 == 0 else -1}" '
                       f'stroke="#000" stroke-width="0.15"/>' for i in range(51))
        return (f'<path d="M{x} {y}L{x + 50 * dx} {y + 50 * dy}" stroke="#000" stroke-width="0.15"/>' + tick)

    notes = [
        "CikutRail dual-GreenHat carrier, top view, 1:1",
        "Print at 100 % (actual size, no fit-to-page). Each ruler must measure 50 mm.",
        "Black: pads (white centre: drilled). Grey: silkscreen outline. Blue dashes: Stamp-S3Bat and its USB-C.",
        "Red G: GND pin of each sensor header (J4-J8). Check a Qwiic cable against J9.",
    ]
    text = "".join(f'<text x="30" y="{14 + 5 * i}" font-size="{4 if i == 0 else 3}">{t}</text>'
                   for i, t in enumerate(notes))
    svg = ('<?xml version="1.0" encoding="UTF-8"?>' + chr(10) +
           '<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="210mm" height="297mm" viewBox="0 0 210 297" '
           'font-family="sans-serif"><rect width="210" height="297" fill="#fff"/>'
           + text + board + mod
           + ruler(ox, oy + BOARD_H + 14, 1, 0)
           + f'<text x="{ox + 52}" y="{oy + BOARD_H + 15}" font-size="3">50 mm</text>'
           + ruler(ox + BOARD_W + 14, oy, 0, 1)
           + f'<text x="{ox + BOARD_W + 11}" y="{oy + 55}" font-size="3">50 mm</text>'
           + "</svg>")
    (HERE / "print_1to1.svg").write_text(svg, encoding="utf-8")


if __name__ == "__main__":
    if "--take" in sys.argv:
        take(sys.argv[sys.argv.index("--take") + 1])
    boxes, pads, shapes, marks = build("--fetch" in sys.argv, "--no-routes" not in sys.argv)
    preview(boxes, pads)
    print_sheet(boxes, pads, shapes, marks)
    problems = check(boxes)
    print(f"{len(boxes)} footprints on {BOARD_W:g} x {BOARD_H:g} mm")
    for p in problems:
        print("  !", p)
