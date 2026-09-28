"""Generate the dual-GreenHat carrier board schematic for EasyEDA Standard.

The circuit is defined below as parts plus pin-to-net maps. Each part's
symbol comes from the EasyEDA/LCSC library (cached in lib/, keyed by LCSC
number) and every connected pin gets a net port, so the sheets need no
drawn wires. Run it, then open the output in EasyEDA Standard
(File > Open > EasyEDA Source) or import it into EasyEDA Pro.

    python build_schematic.py            # uses lib/ cache only
    python build_schematic.py --fetch    # downloads missing symbols first

Outputs (next to this script):
    DualGreenHat_schematic.json   EasyEDA Standard schematic source
    netlist.txt                   net -> pins, for review and diffing
    bom.csv                       JLCPCB-style BOM (Comment, Designator,
                                  Footprint, LCSC)
"""

import json
import re
import sys
import urllib.request
import uuid
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIB = HERE / "lib"
API = "https://easyeda.com/api/products/{}/components?version=6.4.19.5"

# --------------------------------------------------------------------------
# Part catalogue: key -> (LCSC number, value text shown on the schematic)
# --------------------------------------------------------------------------

CAT = {
    "R10K": ("C25744", "10k"),          # 0402, Basic
    "R100K": ("C25741", "100k"),        # 0402, Basic
    "R18K": ("C25810", "18k"),          # 0603, Basic
    "R0": ("C17477", "0R"),             # 0805, Basic (5 V link)
    "C100N": ("C307331", "100nF 50V"),  # 0402, Basic
    "C10N": ("C15195", "10nF 50V"),     # 0402, Basic
    "C470N": ("C47339", "470nF 10V"),   # 0402
    "C10U": ("C15850", "10uF 25V"),     # 0805, Basic
    "C22U0805": ("C45783", "22uF 25V"), # 0805, Basic
    "C22U1206": ("C12891", "22uF 25V"), # 1206, Basic
    "XOR": ("C6005", "74HCT86D"),
    "DRV": ("C92482", "DRV8313PWPR"),
    "BUCK": ("C116592", "TPS563201DDCR"),
    "L3U3": ("C15269", "3.3uH"),
    "FUSE": ("C20812", "PTC 2A 16V"),
    "SS34": ("C8678", "SS34"),
    "TERM6": ("C474924", "KF128-2.54-6P"),
    "PWRTERM": ("C474940", "KF128L-5.08-2P"),
    "SOCKET9": ("C22438159", "1x9 female 2.54"),
}

# --------------------------------------------------------------------------
# Circuit
# --------------------------------------------------------------------------

PARTS = []  # (ref, key, {pin_number: net}, sheet)


def part(sheet, ref, key, pins):
    PARTS.append((ref, key, {str(p): n for p, n in pins.items()}, sheet))


def power_sheet():
    s = "Power and S3Bat"
    # 12 V in -> PTC fuse -> SS34 -> VM
    part(s, "J1", "PWRTERM", {1: "V12_IN", 2: "GND"})
    part(s, "F1", "FUSE", {1: "V12_IN", 2: "V12_F"})
    part(s, "D1", "SS34", {2: "V12_F", 1: "VM"})  # 2 = anode, 1 = cathode
    part(s, "C1", "C22U1206", {1: "VM", 2: "GND"})
    part(s, "C2", "C22U1206", {1: "VM", 2: "GND"})
    # 12 V -> 5 V buck (TPS563201, datasheet table 7-2 for 5 V out)
    part(s, "U1", "BUCK", {1: "GND", 2: "BUCK_SW", 3: "VM", 4: "BUCK_FB",
                           5: "BUCK_EN", 6: "BUCK_BST"})
    part(s, "C3", "C10U", {1: "VM", 2: "GND"})
    part(s, "C4", "C10U", {1: "VM", 2: "GND"})
    part(s, "C5", "C100N", {1: "VM", 2: "GND"})
    part(s, "R1", "R100K", {1: "VM", 2: "BUCK_EN"})
    part(s, "C6", "C100N", {1: "BUCK_BST", 2: "BUCK_SW"})
    part(s, "L1", "L3U3", {1: "BUCK_SW", 2: "5V_BUCK"})
    part(s, "C7", "C22U0805", {1: "5V_BUCK", 2: "GND"})
    part(s, "C8", "C22U0805", {1: "5V_BUCK", 2: "GND"})
    part(s, "R2", "R100K", {1: "5V_BUCK", 2: "BUCK_FB"})  # upper divider
    part(s, "R3", "R18K", {1: "BUCK_FB", 2: "GND"})       # 0.768*(1+100/18)=5.03 V
    part(s, "R4", "R0", {1: "5V_BUCK", 2: "5VIN"})        # remove to run from USB-C only
    # Stamp-S3Bat DIP sockets; pin 1 is the end away from USB-C
    part(s, "J2", "SOCKET9", {1: "CH1", 2: "CH2", 3: "CH3", 4: "CH4", 5: "CH5",
                              6: "GND", 7: "VCC5V", 8: "CH6"})  # 9 = G7, unused
    part(s, "J3", "SOCKET9", {1: "5VIN", 9: "GND"})  # 2 3V3, 3 VBAT, 4-7 G8-G11, 8 WAKE: unused


def block_sheet(b, chans):
    """One GreenHat circuit: three channels, 74HCT86, two DRV8313s."""
    s = f"Channels {chans[0]}-{chans[-1]}"
    n = 100 * b
    xor, ua, ub = f"U{n+1}", f"U{n+2}", f"U{n+3}"
    gates = [(1, 2, 3), (4, 5, 6), (9, 10, 8)]  # (A, B, Y)
    xor_pins = {7: "GND", 14: "VCC5V", 12: "GND", 13: "GND"}  # gate 4 unused
    for i, k in enumerate(chans):
        part(s, f"R{n+1+i}", "R10K", {1: "VCC5V", 2: f"CH{k}"})        # input pull-up
        part(s, f"C{n+1+i}", "C100N", {1: f"CH{k}", 2: "GND"})         # input filter
        # RC delay: 10k x 10uF sets the pulse length. The GreenHat's 1M
        # trimmer is left out; it was set fully counter-clockwise (0 ohm).
        part(s, f"R{n+4+i}", "R10K", {1: f"CH{k}", 2: f"RD{k}"})
        part(s, f"C{n+4+i}", "C10U", {1: f"RD{k}", 2: "GND"})
        a, bb, y = gates[i]
        xor_pins.update({a: f"RD{k}", bb: "GND", y: f"DL{k}"})         # XOR as buffer
    part(s, xor, "XOR", xor_pins)
    # One 6-way terminal per block: pins 1-2 first channel's coil, 3-4 second, 5-6 third
    term = {}
    for i, k in enumerate(chans):
        term[2 * i + 1], term[2 * i + 2] = f"T{k}_A", f"T{k}_B"
    part(s, f"J{n+1}", "TERM6", term)
    part(s, f"C{n+7}", "C100N", {1: "VCC5V", 2: "GND"})

    k1, k2, k3 = chans
    # Half-bridge assignment as on the GreenHat: each channel's coil sits
    # between its direct (CHk) and delayed (DLk) half-bridges.
    drivers = {
        ua: ({27: f"CH{k1}", 25: f"DL{k1}", 23: f"CH{k2}"},
             {5: f"T{k1}_A", 8: f"T{k1}_B", 9: f"T{k2}_A"}),
        ub: ({27: f"DL{k2}", 25: f"CH{k3}", 23: f"DL{k3}"},
             {5: f"T{k2}_B", 8: f"T{k3}_A", 9: f"T{k3}_B"}),
    }
    c = n + 8
    for u, (ins, outs) in drivers.items():
        pins = {1: f"{u}_CP1", 2: f"{u}_CP2", 3: f"{u}_VCP", 4: "VM", 11: "VM",
                6: "GND", 7: "GND", 10: "GND", 12: "GND", 13: "GND",
                14: "GND", 20: "GND", 28: "GND", 29: "GND",
                15: f"{u}_V3P3", 16: "VCC5V", 17: "VCC5V",
                22: "VCC5V", 24: "VCC5V", 26: "VCC5V"}  # 18 nFAULT, 19 nCOMPO, 21 NC: open
        pins.update(ins)
        pins.update(outs)
        part(s, u, "DRV", pins)
        # DRV8313 datasheet values (the GreenHat used 100 nF / 4.7 nF here)
        part(s, f"C{c}", "C10N", {1: f"{u}_CP1", 2: f"{u}_CP2"})
        part(s, f"C{c+1}", "C100N", {1: f"{u}_VCP", 2: "VM"})
        part(s, f"C{c+2}", "C470N", {1: f"{u}_V3P3", 2: "GND"})
        part(s, f"C{c+3}", "C100N", {1: "VM", 2: "GND"})  # one per VM pin
        part(s, f"C{c+4}", "C100N", {1: "VM", 2: "GND"})
        c += 5
    part(s, f"C{c}", "C22U1206", {1: "VM", 2: "GND"})  # bulk near the drivers


power_sheet()
block_sheet(1, [1, 2, 3])
block_sheet(2, [4, 5, 6])
SHEETS = ["Power and S3Bat", "Channels 1-3", "Channels 4-6"]

# --------------------------------------------------------------------------
# Library access
# --------------------------------------------------------------------------


def load_symbol(lcsc, fetch):
    f = LIB / f"{lcsc}.json"
    if not f.exists():
        if not fetch:
            sys.exit(f"missing {f.name}; run with --fetch")
        req = urllib.request.Request(API.format(lcsc), headers={"User-Agent": "curl/8"})
        with urllib.request.urlopen(req) as r:
            raw = json.load(r)
        if not raw.get("success"):
            sys.exit(f"EasyEDA API has no component for {lcsc}")
        res = raw["result"]
        keep = {"uuid": res["uuid"], "title": res["title"], "lcsc": lcsc,
                "puuid": res["dataStr"]["head"].get("puuid"),
                "dataStr": {"head": res["dataStr"]["head"],
                            "shape": res["dataStr"]["shape"]}}
        LIB.mkdir(exist_ok=True)
        f.write_text(json.dumps({"result": keep}, indent=1, ensure_ascii=False),
                     encoding="utf-8")
    res = json.loads(f.read_text(encoding="utf-8"))["result"]
    if "puuid" not in res:
        res["puuid"] = res["dataStr"]["head"].get("puuid")
    return res


# --------------------------------------------------------------------------
# EasyEDA Standard shape helpers
# --------------------------------------------------------------------------

_gid = [0]


def gid():
    _gid[0] += 1
    return f"gge{_gid[0]}"


def num(v):
    v = round(v, 3)
    return str(int(v)) if v == int(v) else str(v)


_TOK = re.compile(r"[MLHVCSQTAZmlhvcsqtaz]|-?\d*\.?\d+(?:[eE]-?\d+)?")


def tpath(p, dx, dy):
    out, cmd, i = [], None, 0
    for t in _TOK.findall(p):
        if t.isalpha():
            cmd, i = t, 0
            out.append(t)
            continue
        v = float(t)
        if cmd in ("M", "L", "T", "C", "S", "Q"):
            v += dx if i % 2 == 0 else dy
        elif cmd == "H":
            v += dx
        elif cmd == "V":
            v += dy
        elif cmd == "A":
            v += dx if i % 7 == 5 else dy if i % 7 == 6 else 0
        i += 1
        out.append(num(v))
    return " ".join(out)


def tpoints(s, dx, dy):
    v = [float(x) for x in s.split()]
    return " ".join(num(x + (dx if j % 2 == 0 else dy)) for j, x in enumerate(v))


def txy(fields, ix, iy, dx, dy):
    fields[ix] = num(float(fields[ix]) + dx)
    fields[iy] = num(float(fields[iy]) + dy)


def translate(shape, dx, dy):
    """Move one symbol shape string by (dx, dy) and give it fresh ids."""
    kind = shape.split("~", 1)[0]
    if kind == "P":
        seg = shape.split("^^")
        f0 = seg[0].split("~"); txy(f0, 4, 5, dx, dy); seg[0] = "~".join(f0)
        f1 = seg[1].split("~"); txy(f1, 0, 1, dx, dy); seg[1] = "~".join(f1)
        f2 = seg[2].split("~"); f2[0] = tpath(f2[0], dx, dy); seg[2] = "~".join(f2)
        for k in (3, 4):
            fk = seg[k].split("~"); txy(fk, 1, 2, dx, dy); seg[k] = "~".join(fk)
        f5 = seg[5].split("~"); txy(f5, 1, 2, dx, dy); seg[5] = "~".join(f5)
        if len(seg) > 6:
            f6 = seg[6].split("~"); f6[1] = tpath(f6[1], dx, dy); seg[6] = "~".join(f6)
        out = "^^".join(seg)
    else:
        f = shape.split("~")
        if kind in ("R", "E"):
            txy(f, 1, 2, dx, dy)
        elif kind in ("PL", "PG"):
            f[1] = tpoints(f[1], dx, dy)
        elif kind in ("PT", "A"):
            f[1] = tpath(f[1], dx, dy)
        elif kind == "T":
            txy(f, 2, 3, dx, dy)
        else:
            raise ValueError(f"unhandled shape {kind}")
        out = "~".join(f)
    return re.sub(r"gge[0-9A-Za-z]+", lambda m: gid(), out)


def pin_info(shape):
    """-> (number, x, y, direction the label should extend: L/R/U/D)."""
    seg = shape.split("^^")
    x, y = (float(v) for v in seg[1].split("~")[:2])
    number = seg[4].split("~")[4]
    # The pin's rotation says which side of the body its end point is on.
    # (The pin line's path can't be used: symbols draw it in either direction.)
    rot = int(float(seg[0].split("~")[6] or 0)) % 360
    way = {0: "R", 90: "U", 180: "L", 270: "D"}[rot]
    return number, x, y, way


def net_port(x, y, way, name):
    geo = {  # rotation, outline, text x/y offset, text rotation, anchor
        "L": (0, [(0, 0), (-5, 5), (-20, 5), (-20, -5), (-5, -5), (0, 0)], (-21.5, 3), 0, "end"),
        "R": (180, [(0, 0), (5, -5), (20, -5), (20, 5), (5, 5), (0, 0)], (21.8, 3), 0, "start"),
        "U": (270, [(0, 0), (-5, -5), (-5, -20), (5, -20), (5, -5), (0, 0)], (3, -21.8), 270, "start"),
        "D": (90, [(0, 0), (5, 5), (5, 20), (-5, 20), (-5, 5), (0, 0)], (-3, 21.8), 90, "start"),
    }[way]
    rot, outline, (tx, ty), trot, anchor = geo
    pts = " ".join(f"{num(x + a)} {num(y + b)}" for a, b in outline)
    return (f"F~part_netLabel_netPort~{num(x)}~{num(y)}~{rot}~{gid()}~~0^^"
            f"{num(x)}~{num(y)}^^{name}~#0000FF~{num(x + tx)}~{num(y + ty)}~{trot}~"
            f"{anchor}~1~Times New Roman~8pt~flag_{gid()}^^"
            f"PL~{pts}~#0000FF~1~0~transparent~{gid()}~0")


def text(kind, x, y, s):
    return (f"T~{kind}~{num(x)}~{num(y)}~0~#000080~Arial~~~~~comment~{s}~1~start~"
            f"{gid()}~0~")


def bbox(shapes):
    xs, ys = [], []
    for s in shapes:
        k = s.split("~", 1)[0]
        if k == "P":
            info = pin_info(s)
            xs.append(info[1]); ys.append(info[2])
        elif k == "R":
            f = s.split("~")
            x, y, w, h = float(f[1]), float(f[2]), float(f[5]), float(f[6])
            xs += [x, x + w]; ys += [y, y + h]
        elif k in ("PL", "PG"):
            v = [float(t) for t in s.split("~")[1].split()]
            xs += v[0::2]; ys += v[1::2]
    return min(xs), min(ys), max(xs), max(ys)


# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------

LABEL_W = 5.5  # px per character of an 8 pt label


def place_part(ref, key, pins, sym, x0, y0):
    """Place a symbol with its top-left at (x0, y0). Returns (shapes, bbox)."""
    shapes = sym["dataStr"]["shape"]
    bx0, by0, bx1, by1 = bbox(shapes)
    dx, dy = x0 - bx0, y0 - by0
    lcsc, value = CAT[key]
    head = sym["dataStr"]["head"]["c_para"]
    cpara = {
        "package": head.get("package", ""),
        "BOM_Supplier": "LCSC",
        "BOM_Supplier Part": lcsc,
        "BOM_Manufacturer": head.get("Manufacturer", ""),
        "BOM_Manufacturer Part": head.get("Manufacturer Part", sym["title"]),
        "BOM_JLCPCB Part Class": head.get("JLCPCB Part Class", ""),
        "spicePre": re.sub(r"[^A-Z]", "", ref)[:1] or "X",
        "spiceSymbolName": head.get("Manufacturer Part", sym["title"]),
    }
    cp = "`".join(f"{k}`{v}" for k, v in cpara.items()) + "`"
    lib = (f"LIB~{num(x0 - bx0 + float(sym['dataStr']['head']['x']))}~"
           f"{num(y0 - by0 + float(sym['dataStr']['head']['y']))}~{cp}~~0~{gid()}~"
           f"{sym['puuid']}~{sym['uuid']}~0~{uuid.uuid4().hex[:16]}~yes~yes")
    body = [translate(s, dx, dy) for s in shapes]
    w, h = bx1 - bx0, by1 - by0
    body.append(text("P", x0, y0 + h + 12, ref))
    body.append(text("N", x0, y0 + h + 22, value))
    ports, seen = [], set()
    for s in shapes:
        if not s.startswith("P~"):
            continue
        number, px, py, way = pin_info(s)
        seen.add(number)
        if number in pins:
            ports.append(net_port(px + dx, py + dy, way, pins[number]))
    unknown = set(pins) - seen
    if unknown:
        sys.exit(f"{ref}: pins {sorted(unknown)} not in symbol {sym['title']}")
    return "#@$".join([lib] + body), ports, (w, h)


def label_extent(sym, pins):
    left = right = up = 0.0
    for s in sym["dataStr"]["shape"]:
        if s.startswith("P~"):
            number, _, _, way = pin_info(s)
            ln = 25 + LABEL_W * len(pins.get(number, ""))
            if number in pins:
                if way == "L": left = max(left, ln)
                if way == "R": right = max(right, ln)
                if way == "U": up = max(up, ln)
    return left, right, up


def build(fetch):
    syms = {k: load_symbol(v[0], fetch) for k, v in CAT.items()}
    sheets = []
    for title in SHEETS:
        shapes, row_w = [], 1500
        x, y, row_h = 0.0, 0.0, 0.0
        for ref, key, pins, sheet in PARTS:
            if sheet != title:
                continue
            sym = syms[key]
            bx0, by0, bx1, by1 = bbox(sym["dataStr"]["shape"])
            w, h = bx1 - bx0, by1 - by0
            left, right, up = label_extent(sym, pins)
            cell_w = left + w + right + 30
            if x + cell_w > row_w and x > 0:
                x, y, row_h = 0.0, y + row_h + 40, 0.0
            lib, ports, _ = place_part(ref, key, pins, sym, x + left, y + up)
            shapes.append(lib)
            shapes += ports
            x += cell_w
            row_h = max(row_h, up + h + 30)
        sheets.append({
            "docType": "1", "title": title, "description": "",
            "dataStr": {
                "head": {"docType": "1", "editorVersion": "6.4.20.6", "newgId": True,
                         "c_para": {"Prefix Start": "1"}, "c_spiceCmd": "null",
                         "hasIdFlag": True, "uuid": uuid.uuid4().hex,
                         "x": "0", "y": "0", "importFlag": 0, "transformList": ""},
                "canvas": "CA~1600~1200~#FFFFFF~yes~#CCCCCC~5~1000~1000~line~5~pixel~5~0~0",
                "shape": shapes,
                "BBox": {"x": -100, "y": -100, "width": 1700, "height": y + row_h + 200},
                "colors": {},
            },
        })
    doc = {"editorVersion": "6.4.20.6", "docType": "5",
           "title": "CikutRail Dual GreenHat Carrier",
           "description": "Stamp-S3Bat DIP carrier with two IoTT GreenHat coil-driver "
                          "circuits (6 Kato turnouts). Derived from IoTT GreenHat Coil "
                          "Driver rev 1.0, TAPR OHL v1.0.",
           "colors": {}, "schematics": sheets}
    (HERE / "DualGreenHat_schematic.json").write_text(
        json.dumps(doc, ensure_ascii=False), encoding="utf-8")
    return syms


def reports(syms):
    nets = defaultdict(list)
    for ref, key, pins, _ in PARTS:
        for p, n in pins.items():
            nets[n].append(f"{ref}.{p}")
    lines = []
    for n in sorted(nets):
        lines.append(f"{n}: {' '.join(sorted(nets[n]))}")
    (HERE / "netlist.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    singles = [n for n, v in nets.items() if len(v) < 2]
    if singles:
        sys.exit(f"nets with one connection: {singles}")

    groups = defaultdict(list)
    for ref, key, _, _ in PARTS:
        groups[key].append(ref)
    rows = ["Comment,Designator,Footprint,LCSC"]
    for key, refs in groups.items():
        lcsc, value = CAT[key]
        pkg = syms[key]["dataStr"]["head"]["c_para"].get("package", "")
        rows.append(f'"{value}","{",".join(sorted(refs))}","{pkg}","{lcsc}"')
    (HERE / "bom.csv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"{len(PARTS)} parts, {len(nets)} nets, {len(groups)} BOM lines")


if __name__ == "__main__":
    reports(build("--fetch" in sys.argv))
