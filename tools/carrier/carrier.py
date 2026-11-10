#!/usr/bin/env python3
"""Coglet carrier board: model, checks and generator.

The carrier is an ElectroCookie mini solderable breadboard (1/4 size, 38.1 x 50.8 mm, 17 rows, strips A-E and
F-J either side of a 0.3 in channel). It carries the XIAO and stands the MAX98357A amp and INMP441 mic on
header pins; the PCA9685 and the speaker stay off the board on short leads.

This file is the single source: the holes, the pins fitted, the underside links and their routes. From it:

  python3 tools/carrier/carrier.py          writes the hole map, one picture per step, docs/CARRIER.md,
                                            docs/CARRIER-STEPS.md and web-programmer/carrier.html
  python3 tools/carrier/carrier.py --check  changes nothing: checks that the strips, pins and links make exactly
                                            the nets in hardware/wires.csv, that no two links touch, and that
                                            the generated files on disk are up to date

DESIGN ONLY: nobody has built this board yet. Pin orders of the amp and mic breakouts are assumed.
"""
import csv
import html
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
OUT = ROOT / "web-programmer" / "carrier"

# ---------------------------------------------------------------- the board
COLS = "ABCDEFGHIJ"
IDX = dict(A=0, B=1, C=2, D=3, E=4, F=7, G=8, H=9, I=10, J=11)  # the channel takes the place of two columns
ROWS = range(1, 18)
BOARD_W, BOARD_H = 38.1, 50.8
PITCH = 2.54


def printed(col):  # the underside print is mirrored: A<->J, B<->I, C<->H, D<->G, E<->F
    return COLS[len(COLS) - 1 - COLS.index(col)]


def hcol(h):
    return h[0]


def hrow(h):
    return int(h[1:])


def side(col):
    return "L" if col in "ABCDE" else "R"


def strip(h):
    return (hrow(h), side(hcol(h)))


def mm_u(col):
    return (IDX[col] - 5.5) * PITCH


def mm_v(row):
    return (row - 9) * PITCH


def hole_mm(h):
    return (mm_u(hcol(h)), mm_v(hrow(h)))


def under(h):
    return printed(hcol(h)) + str(hrow(h))


# ---------------------------------------------------------------- the pins fitted (pad -> hole)
# XIAO, component side up, USB-C toward row 1: D0..D5 in column D (rows 5-10), 5V, GND, 3V3, D10 in column H
# (rows 5-8). Pins D6, D7, D8 and D9 are left out so their strips stay free.
PADS = {
    "XIAO.D0": "D5", "XIAO.D1": "D6", "XIAO.D2": "D7", "XIAO.D3": "D8", "XIAO.D4": "D9", "XIAO.D5": "D10",
    "XIAO.5V": "H5", "XIAO.GND": "H6", "XIAO.3V3": "H7", "XIAO.D10": "H8",
    # MAX98357A standing in column A, rows 1-7. Assumed pin order Vin, GND, SD, GAIN, DIN, BCLK, LRC; SD and GAIN
    # are left out (unconnected).
    "MAX98357A.Vin": "A1", "MAX98357A.GND": "A2", "MAX98357A.DIN": "A5", "MAX98357A.BCLK": "A6",
    "MAX98357A.LRC": "A7",
    # INMP441 standing in column J, rows 12-17. ASSUMED pin order SCK, WS, SD, L/R, VDD, GND: check your board.
    "INMP441.SCK": "J12", "INMP441.WS": "J13", "INMP441.SD": "J14", "INMP441.L/R": "J15", "INMP441.VDD": "J16",
    "INMP441.GND": "J17",
    # The lead to the PCA9685: a 5-pin header in column J, rows 6-10, in the order the lead is wired.
    "PCA9685.GND": "J6", "PCA9685.VCC": "J7", "PCA9685./OE": "J8", "PCA9685.SDA": "J9", "PCA9685.SCL": "J10",
}
PAD_AT = {h: p for p, h in PADS.items()}
PART_NAME = {"XIAO": "XIAO", "MAX98357A": "amp", "INMP441": "mic", "PCA9685": "PCA lead"}

# ---------------------------------------------------------------- the links (underside, 30 AWG stranded)
COLORS = {"red": "#d12a1f", "black": "#1b1b1b", "orange": "#e8801a", "yellow": "#e0b800", "green": "#1f9a3c",
          "blue": "#2f66d8", "violet": "#8a3fc0", "grey": "#808890", "white": "#ffffff"}
# id, from hole, to hole, colour, lane (a column index between holes: 5-6 is the channel), what it does
LINKS = [
    ("L1", "E9", "F9", "violet", 5.5, "SDA: D4's strip to the lead's SDA pin."),
    ("L2", "E10", "F10", "grey", 5.5, "SCL: D5's strip to the lead's SCL pin."),
    ("L3", "F15", "F17", "black", 6.5, "Ties the mic's L/R pin to its GND pin, so it drives the left slot."),
    ("L4", "E1", "F5", "red", 5.5, "+5 V from the XIAO's 5V strip to the amp's Vin."),
    ("L5", "E2", "F6", "black", 5.0, "Ground from the XIAO's GND strip to the amp's GND."),
    ("L6", "F16", "G7", "orange", 7.5, "3.3 V from the XIAO's 3V3 strip to the mic's VDD."),
    ("L7", "G17", "G6", "black", 8.5, "Ground from the XIAO's GND strip to the mic's GND."),
    ("L8", "E6", "F12", "yellow", 6.0, "I2S bit clock: D1's strip (the amp's BCLK) to the mic's SCK."),
    ("L9", "E7", "F13", "green", 5.5, "I2S word select: D2's strip (the amp's LRC) to the mic's WS."),
    ("L10", "E8", "F14", "blue", 5.0, "Mic data: D3's strip to the mic's SD."),
]


def route_mm(link):
    """The underside route of a link in board millimetres: hole, jog, lane, jog, hole."""
    _, a, b, _, lane, _ = link
    (x1, y1), (x2, y2) = hole_mm(a), hole_mm(b)
    if abs(y1 - y2) < 1e-6:
        return [(x1, y1), (x2, y2)]
    lx = (lane - 5.5) * PITCH
    dn = 1 if y2 > y1 else -1
    return [(x1, y1), (lx, y1 + dn * 0.9), (lx, y2 - dn * 0.9), (x2, y2)]


def length(pts):
    return sum(math.dist(p, q) for p, q in zip(pts, pts[1:]))


def cut_mm(link):  # the cam-rotation-starter rule: routed length x 1.15 + 10 mm, rounded up to 5 mm
    return int(math.ceil((length(route_mm(link)) * 1.15 + 10) / 5.0) * 5)


# ---------------------------------------------------------------- checks
def nets():
    """Union the holes: every strip, every link. Return the groups of pads that end up joined."""
    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        parent[find(a)] = find(b)

    for r in ROWS:
        for s in "LR":
            cols = [c for c in COLS if side(c) == s]
            for c in cols[1:]:
                union((r, s), (r, s))  # a strip is one node
    for _, a, b, *_ in LINKS:
        union(strip(a), strip(b))
    groups = {}
    for pad, h in PADS.items():
        groups.setdefault(find(strip(h)), set()).add(pad)
    return sorted((frozenset(g) for g in groups.values()), key=lambda g: sorted(g))


def expected_nets():
    """The nets in hardware/wires.csv, restricted to what the carrier holds (the speaker and the servo supply stay off it)."""
    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            x = parent[x]
        return x

    def pad(s):
        part, _, pin = s.partition(" ")
        return f"{part}.{pin}"

    with open(ROOT / "hardware" / "wires.csv") as f:
        for row in csv.DictReader(f):
            a, b = pad(row["from"]), pad(row["to"])
            if a in PADS and b in PADS:
                parent[find(a)] = find(b)
    groups = {}
    for p in list(parent):
        groups.setdefault(find(p), set()).add(p)
    return sorted((frozenset(g) for g in groups.values()), key=lambda g: sorted(g))


def seg_dist(p, q, r, s):
    """Smallest distance between segments pq and rs, and whether they cross."""
    def orient(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def pdist(pt, a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        t = 0 if dx == dy == 0 else max(0, min(1, ((pt[0] - a[0]) * dx + (pt[1] - a[1]) * dy) / (dx * dx + dy * dy)))
        return math.dist(pt, (a[0] + t * dx, a[1] + t * dy))

    cross = orient(p, q, r) * orient(p, q, s) < 0 and orient(r, s, p) * orient(r, s, q) < 0
    return (0.0 if cross else min(pdist(p, r, s), pdist(q, r, s), pdist(r, p, q), pdist(s, p, q))), cross


def check():
    problems = []
    got, want = nets(), expected_nets()
    if got != want:
        for g in got:
            if g not in want:
                problems.append("carrier net not in wires.csv: " + ", ".join(sorted(g)))
        for g in want:
            if g not in got:
                problems.append("wires.csv net missing on the carrier: " + ", ".join(sorted(g)))
    # every link end is a free hole, and no hole is used twice
    seen = {}
    for lid, a, b, *_ in LINKS:
        for h in (a, b):
            if h in PAD_AT:
                problems.append(f"{lid} ends in {h}, which holds the pin {PAD_AT[h]}")
            if h in seen:
                problems.append(f"{lid} and {seen[h]} both end in {h}")
            seen[h] = lid
    # links keep clear of each other: they may cross (insulated wire) but never run closer than 0.5 mm
    routes = {l[0]: route_mm(l) for l in LINKS}
    ids = list(routes)
    for i, x in enumerate(ids):
        for y in ids[i + 1:]:
            for p, q in zip(routes[x], routes[x][1:]):
                for r, s in zip(routes[y], routes[y][1:]):
                    d, crossed = seg_dist(p, q, r, s)
                    if not crossed and d < 0.5:
                        problems.append(f"{x} and {y} run within {d:.2f} mm of each other")
    # a pin hole must not sit under another link's route (a bare solder joint against a wire)
    for lid, pts in routes.items():
        for h in PAD_AT:
            if h in (l[1] for l in LINKS if l[0] == lid):
                continue
            for p, q in zip(pts, pts[1:]):
                if seg_dist(hole_mm(h), hole_mm(h), p, q)[0] < 0.7:
                    problems.append(f"{lid} runs within 0.7 mm of the pin hole {h} ({PAD_AT[h]})")
    return problems


def crossings():
    routes = {l[0]: route_mm(l) for l in LINKS}
    ids, n = list(routes), []
    for i, x in enumerate(ids):
        for y in ids[i + 1:]:
            if any(seg_dist(p, q, r, s)[1] for p, q in zip(routes[x], routes[x][1:]) for r, s in zip(routes[y], routes[y][1:])):
                n.append((x, y))
    return n


# ---------------------------------------------------------------- pictures
S = 8  # px per mm
VW = 600
OX = (VW - BOARD_W * S) / 2
OY = 112
VH = int(OY + BOARD_H * S + 70)
INK, MUTE, DONE, RING, BOARD = "#1c2729", "#5d6b6e", "#a7aeb2", "#d4147a", "#e8eee8"
FONT = 'font-family="IBM Plex Sans,ui-sans-serif,system-ui,sans-serif"'
MONO = 'font-family="IBM Plex Mono,ui-monospace,SFMono-Regular,Menlo,monospace"'
esc = html.escape


class View:
    def __init__(self, mirror):
        self.mirror = mirror

    def x(self, u):
        return OX + (BOARD_W / 2 + (-u if self.mirror else u)) * S

    @staticmethod
    def y(v):
        return OY + (BOARD_H / 2 + v) * S

    def hole(self, h):
        return self.x(mm_u(hcol(h))), self.y(mm_v(hrow(h)))

    def pt(self, p):
        return self.x(p[0]), self.y(p[1])


def board_svg(view, title, subtitle, body, label_pads=True):
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {VW} {VH}" role="img" aria-label="{esc(title)}. {esc(subtitle)}">',
         f'<rect width="{VW}" height="{VH}" fill="#f6f8f8"/>',
         f'<text x="{VW / 2}" y="30" text-anchor="middle" font-size="19" font-weight="700" fill="{INK}" {FONT}>{esc(title)}</text>',
         f'<text x="{VW / 2}" y="52" text-anchor="middle" font-size="13" fill="{MUTE}" {FONT}>{esc(subtitle)}</text>']
    o.append(f'<rect x="{OX:.1f}" y="{OY:.1f}" width="{BOARD_W * S:.1f}" height="{BOARD_H * S:.1f}" rx="6" fill="{BOARD}" stroke="#9aa6a0" stroke-width="2"/>')
    # strips
    for r in ROWS:
        for s in "LR":
            cs = [c for c in COLS if side(c) == s]
            x0, x1 = view.x(mm_u(cs[0])), view.x(mm_u(cs[-1]))
            xa, xb = min(x0, x1) - 8, max(x0, x1) + 8
            o.append(f'<rect x="{xa:.1f}" y="{view.y(mm_v(r)) - 7:.1f}" width="{xb - xa:.1f}" height="14" rx="7" fill="#d9e3dc"/>')
    # holes
    for r in ROWS:
        for c in COLS:
            x, y = view.hole(c + str(r))
            o.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3" fill="#ffffff" stroke="#8d9a93" stroke-width="1"/>')
    # column and row labels (the underside is labelled with the PRINTED letters)
    for c in COLS:
        x = view.x(mm_u(c))
        top = printed(c) if view.mirror else c
        small = f"({c})" if view.mirror else ""
        o.append(f'<text x="{x:.1f}" y="{OY - 20}" text-anchor="middle" font-size="13" font-weight="700" fill="{INK}" {MONO}>{top}</text>')
        if small:
            o.append(f'<text x="{x:.1f}" y="{OY - 7}" text-anchor="middle" font-size="8" fill="{MUTE}" {MONO}>{small}</text>')
    for r in ROWS:
        y = view.y(mm_v(r)) + 4
        o.append(f'<text x="{OX - 12:.1f}" y="{y:.1f}" text-anchor="end" font-size="10" fill="{MUTE}" {MONO}>{r}</text>')
        o.append(f'<text x="{OX + BOARD_W * S + 12:.1f}" y="{y:.1f}" font-size="10" fill="{MUTE}" {MONO}>{r}</text>')
    o += body
    cap = "Underside, as you see it turned over like a page: the letters are the ones printed there." if view.mirror else \
          "Component side, row 1 at the top, column A at the left."
    o.append(f'<text x="{VW / 2}" y="{VH - 24}" text-anchor="middle" font-size="12" fill="{MUTE}" {FONT}>{cap}</text>')
    o.append("</svg>")
    return "\n".join(o)


HALO = 'stroke="#f6f8f8" stroke-width="3" paint-order="stroke"'


def pad_dots(view, highlight=(), names=True):
    o = []
    for pad, h in PADS.items():
        x, y = view.hole(h)
        hot = pad in highlight
        o.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{5 if hot else 3.6}" fill="{INK if hot else "#6f7d77"}"/>')
        if names and not view.mirror:
            part, _, pin = pad.partition(".")
            left = side(hcol(h)) == "L"
            dx = -10 if (hcol(h) == "A") else (10 if hcol(h) in "DE" else (-10 if hcol(h) in "H" else 10))
            if hcol(h) == "D":
                dx = -10
            anchor = "end" if dx < 0 else "start"
            o.append(f'<text x="{x + dx:.1f}" y="{y + 3.5:.1f}" text-anchor="{anchor}" font-size="9" fill="{INK if hot else MUTE}" {HALO} {MONO}>{esc(pin)}</text>')
    return o


def polyline(view, link, color, width, opacity=1.0, halo=False):
    pts = " ".join(f"{view.pt(p)[0]:.1f},{view.pt(p)[1]:.1f}" for p in route_mm(link))
    o = []
    if halo:
        o.append(f'<polyline points="{pts}" fill="none" stroke="#ffffff" stroke-width="{width + 4}" stroke-linecap="round" stroke-linejoin="round"/>')
    if color == "#1b1b1b" and width > 3:  # black wires need an outline to read on the board
        o.append(f'<polyline points="{pts}" fill="none" stroke="#7a8388" stroke-width="{width + 1.6}" stroke-linecap="round" stroke-linejoin="round"/>')
    o.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round" opacity="{opacity}"/>')
    return o


def ring(view, h, big, small, dx):
    x, y = view.hole(h)
    return [f'<circle cx="{x:.1f}" cy="{y:.1f}" r="8" fill="none" stroke="{RING}" stroke-width="2.4"/>',
            f'<text x="{x + dx:.1f}" y="{y + 5:.1f}" text-anchor="{"start" if dx > 0 else "end"}" font-size="15" font-weight="700" fill="{RING}" {HALO} {MONO}>{big}</text>',
            f'<text x="{x + dx:.1f}" y="{y + 17:.1f}" text-anchor="{"start" if dx > 0 else "end"}" font-size="9" fill="{MUTE}" {HALO} {MONO}>{small}</text>']


def footprints(view):
    """Where the modules sit on the component side."""
    o = []
    # XIAO: body 21 x 17.8 mm over the pins; USB-C toward row 1
    u0, u1 = mm_u("D") - 1.27, mm_u("H") + 1.27
    v0, v1 = mm_v(5) - 2.9, mm_v(11) + 2.9
    xa, xb = sorted((view.x(u0), view.x(u1)))
    o.append(f'<rect x="{xa:.1f}" y="{view.y(v0):.1f}" width="{xb - xa:.1f}" height="{(v1 - v0) * S:.1f}" rx="4" fill="#cfe0ef" fill-opacity="0.55" stroke="#4f7da8" stroke-width="1.6" stroke-dasharray="5 3"/>')
    cx = (xa + xb) / 2
    o.append(f'<rect x="{cx - 24:.1f}" y="{view.y(v0) - 7:.1f}" width="48" height="12" rx="3" fill="#b8c6d3" stroke="#4f7da8"/>')
    o.append(f'<text x="{cx:.1f}" y="{view.y(v0) + 18:.1f}" text-anchor="middle" font-size="10" fill="#2f5a82" {FONT}>USB-C</text>')
    o.append(f'<text x="{cx:.1f}" y="{view.y((v0 + v1) / 2) + 4:.1f}" text-anchor="middle" font-size="13" font-weight="700" fill="#2f5a82" {FONT}>XIAO</text>')
    # amp standing in column A rows 1-7; mic standing in column J rows 12-17; PCA lead header J6-J10
    labels = {"A1": ("A9", "start", "amp, standing", "rows 1–7"), "J12": ("J18", "end", "mic, standing", "rows 12–17"),
              "J6": ("J5", "end", "PCA lead", "rows 6–10")}
    for hole_a, hole_b, label, col in (("A1", "A7", "amp (standing)", "#caa24a"), ("J12", "J17", "mic (standing)", "#6aa56a"),
                                       ("J6", "J10", "PCA lead", "#b07ac0")):
        (xa_, ya_), (xb_, yb_) = view.hole(hole_a), view.hole(hole_b)
        o.append(f'<rect x="{min(xa_, xb_) - 7:.1f}" y="{min(ya_, yb_) - 7:.1f}" width="14" height="{abs(yb_ - ya_) + 14:.1f}" rx="4" fill="{col}" fill-opacity="0.35" stroke="{col}" stroke-width="1.6"/>')
        lh, anchor, l1, l2 = labels[hole_a]
        if hole_a == "A1":      # below the amp, in the free corner left of the XIAO
            lx, ly, anchor = view.x(mm_u("A")) - 6, view.y(mm_v(13)), "start"
        elif hole_a == "J12":   # below the board's last row of pins
            lx, ly, anchor = view.x(mm_u("J")), view.y(mm_v(17)) + 24, "middle"
        else:                   # between the PCA lead's last pin and the mic's first
            lx, ly, anchor = view.x(mm_u("J")), view.y(mm_v(11)) + 4, "middle"
        o.append(f'<text x="{lx:.1f}" y="{ly:.1f}" text-anchor="{anchor}" font-size="10" font-weight="600" fill="{MUTE}" {HALO} {FONT}>{l1}</text>')
    return o


def hole_map_svg():
    v = View(False)
    body = footprints(v) + pad_dots(v)
    for l in LINKS:  # links are on the other side: drawn faint and dashed
        pts = " ".join(f"{v.pt(p)[0]:.1f},{v.pt(p)[1]:.1f}" for p in route_mm(l))
        body.append(f'<polyline points="{pts}" fill="none" stroke="{COLORS[l[3]]}" stroke-width="2" stroke-dasharray="4 3" opacity="0.55"/>')
    return board_svg(v, "Coglet carrier: hole map", "Component side. Dots are fitted pins; dashed lines are the underside links.", body)


def step_svg(step):
    kind = step["kind"]
    if kind == "link":
        v = View(True)
        link = next(l for l in LINKS if l[0] == step["link"])
        i = LINKS.index(link)
        body = pad_dots(v, names=False)
        for l in LINKS[:i]:
            body += polyline(v, l, DONE, 3.2, 0.9)
        body += polyline(v, link, COLORS[link[3]], 5, halo=True)
        for h in (link[1], link[2]):
            dx = 14 if view_x_is_left(v, h) else -14
            body += ring(v, h, under(h), "top " + h, dx)
        title = f"Step {step['n']}: {link[0]}, {link[3]}, {under(link[1])} → {under(link[2])}"
        sub = f"Underside. {link[0]} is drawn bold; earlier links are grey."
        return board_svg(v, title, sub, body)
    v = View(False)
    hot = step.get("pads", [])
    body = footprints(v) + pad_dots(v, highlight=hot)
    for p in hot:
        x, y = v.hole(PADS[p])
        body.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="8" fill="none" stroke="{RING}" stroke-width="2.4"/>')
    return board_svg(v, f"Step {step['n']}: {step['title']}", step.get("sub", "Component side."), body)


def view_x_is_left(view, h):
    return view.hole(h)[0] < VW / 2


# ---------------------------------------------------------------- the steps
def link_steps():
    out = []
    for l in sorted(LINKS, key=lambda l: length(route_mm(l))):
        pass
    return out


def build_steps():
    steps = []

    def add(**k):
        k["n"] = len(steps) + 1
        steps.append(k)

    xiaopads = [p for p in PADS if p.startswith("XIAO.")]
    add(kind="bench", title="Prepare the modules", sub="Component side. The dots are the pins you fit.",
        pads=list(PADS), view="top",
        do=["Fit header pins **only where the hole map shows a dot**, and pull the others out with pliers. Each strip that has no pin in it stays free for links.",
            "**XIAO:** left row D0–D5, right row 5V, GND, 3V3, D10. Leave **D6, D7, D8 and D9 out**. Fit the pins to the module with the perfboard as the jig, so they come out square, and solder them to the module only.",
            "**MAX98357A amp:** a 7-pin header on its pin edge, keeping Vin, GND, DIN, BCLK and LRC. Pull SD and GAIN. *Assumed pin order Vin, GND, SD, GAIN, DIN, BCLK, LRC: check your board's labels.*",
            "**INMP441 mic:** a 6-pin header, all six kept. *Assumed pin order SCK, WS, SD, L/R, VDD, GND: check your board's labels. If yours differs, keep the same nets and move the pins and links to suit.*",
            "**PCA9685 lead:** a 5-pin header in the order GND, VCC, /OE, SDA, SCL.",
            "**PCA9685 /OE:** swap the breakout's pull-down for a pull-up as in the [wiring guide](wiring.html), stage 1."],
        facts=[("Board", "ElectroCookie mini solderable breadboard, 38.1 × 50.8 mm, 17 rows"),
               ("Check", "Hold each header over the hole map: every fitted pin lands on a dotted hole.")],
        check=["Nothing is soldered to the carrier yet."])
    for l in sorted(LINKS, key=lambda l: length(route_mm(l))):
        lid, a, b, col, lane, job = l
        sa, sb = strip(a), strip(b)
        def strip_pads(s):
            return sorted(p for p, h in PADS.items() if strip(h) == s)
        pa, pb = strip_pads(sa), strip_pads(sb)
        near = []  # must-not-connect: the strips either side of each end that hold pins
        for h in (a, b):
            r, s = strip(h)
            for rr in (r - 1, r + 1):
                near += [f"{p} (strip {rr}{'L' if s == 'L' else 'R'})" for p in sorted(p for p, hh in PADS.items() if strip(hh) == (rr, s))]
        joins = ", ".join(pa + pb) if (pa or pb) else "nothing yet"
        add(kind="link", link=lid, title=f"{lid}, {col}, {under(a)} → {under(b)}", view="under",
            do=["Turn the board over (underside up).",
                f"Start at hole printed **{under(a)}** (top {a}): push the tinned end in and solder it.",
                "Lay the wire flat along the drawn path; pinch the corners and hold them with Kapton tape.",
                f"End at hole printed **{under(b)}** (top {b}): push it in and solder it.",
                "Check, as below."],
            facts=[("Wire", f"{lid}, {col}, 30 AWG stranded"),
                   ("Cut", f"{cut_mm(l)} mm, estimated from the drawn route: trim to fit"),
                   ("From → to", f"{a} → {b} (printed {under(a)} → {under(b)} on the underside)"),
                   ("Side", "underside (link)"),
                   ("What it does", job)],
            check=[f"{a} connects to {b}." + (f" Both strips will join {joins}." if (pa or pb) else ""),
                   "It must not connect to: " + (", ".join(dict.fromkeys(near)) if near else "any other hole.")])
    xiao = [p for p in PADS if p.startswith("XIAO.")]
    add(kind="part", title="Solder the XIAO", sub="Component side. The XIAO's pins go in the circled holes.", pads=xiao, view="top",
        do=["Component side up, **USB-C toward row 1** (the top edge). Left pin row in column D (D0 at D5 … D5 at D10), right row in column H (5V at H5, GND, 3V3, D10 at H8).",
            "Solder each pin from the underside. Check the pins you pulled (D6–D9) are not in the board.",
            "S3 Sense: the camera board stacks on top of the XIAO, so allow height above it."],
        facts=[("Pins", ", ".join(f"{p.split('.')[1]} {PADS[p]}" for p in xiao))],
        check=["Each XIAO pin hole reads the same net as its link end (see the continuity check below)."])
    amp = [p for p in PADS if p.startswith("MAX98357A.")]
    add(kind="part", title="Solder the amp, standing", sub="Component side. The amp's pins go in column A.", pads=amp, view="top",
        do=["Stand the MAX98357A on its header in column A, rows 1–7, its board edge-on to the board and the pin edge down. Vin at A1, GND at A2, DIN at A5, BCLK at A6, LRC at A7. SD and GAIN (A3, A4) have no pin.",
            "Solder from the underside. The speaker's two wires go to the amp's own terminal later."],
        facts=[("Pins", ", ".join(f"{p.split('.')[1]} {PADS[p]}" for p in amp)), ("Assumed", "the amp's pin order; check your board")],
        check=["A5, A6 and A7 are already joined to D0, D1 and D2 by their strips."])
    mic = [p for p in PADS if p.startswith("INMP441.")]
    add(kind="part", title="Solder the mic, standing", sub="Component side. The mic's pins go in column J.", pads=mic, view="top",
        do=["Stand the INMP441 on its header in column J, rows 12–17: SCK J12, WS J13, SD J14, L/R J15, VDD J16, GND J17. Its sound port should face where the mechanism will have an opening; that is not designed yet.",
            "Solder from the underside."],
        facts=[("Pins", ", ".join(f"{p.split('.')[1]} {PADS[p]}" for p in mic)), ("Assumed", "the mic's pin order; check your board")],
        check=["Mic placement is flagged open: its position depends on the mechanical design."])
    pca = [p for p in PADS if p.startswith("PCA9685.")]
    add(kind="part", title="Solder the PCA9685 lead header", sub="Component side. The lead's header goes in column J.", pads=pca, view="top",
        do=["Stand the 5-pin header in column J, rows 6–10, in the order GND (J6), VCC (J7), /OE (J8), SDA (J9), SCL (J10).",
            "Solder from the underside. GND, VCC and /OE already sit in the strips of the XIAO's GND, 3V3 and D10; SDA and SCL were joined by L1 and L2."],
        facts=[("Pins", ", ".join(f"{p.split('.')[1]} {PADS[p]}" for p in pca))],
        check=["J6 reads as ground, J7 as 3.3 V, J8 as D10."])
    add(kind="check", title="Continuity check", view="top", pads=list(PADS),
        sub="Component side. Probe the holes of the pins in each row of the table.",
        do=["Meter on continuity: connected beeps. Probe from the component side."],
        facts=[], check=[], table=True)
    add(kind="leads", title="Leads off the board", view=None,
        do=["Make the PCA9685 lead: a 5-wire lead with a female 5-pin connector at the carrier end and the breakout's pin spacing at the other: GND, VCC, /OE, SDA, SCL in the carrier's order.",
            "Speaker: its two wires to the amp's own screw terminal (+ and −).",
            "Servo power stays off the carrier: the supply's + goes through the switch to the PCA9685's V+ terminal, and its − to the PCA9685's GND terminal, as in stage 8 of the [wiring guide](wiring.html).",
            "Leave the switch off and every servo unplugged."],
        facts=[], check=["Then run the bench checks on the [Check](console.html) page."], leads=True)
    return steps


LEADS = [("GND", "J6", "PCA9685 GND", "black"), ("VCC", "J7", "PCA9685 VCC", "orange"), ("/OE", "J8", "PCA9685 /OE", "brown"),
         ("SDA", "J9", "PCA9685 SDA", "violet"), ("SCL", "J10", "PCA9685 SCL", "grey")]


def net_name(g):
    names = {"XIAO.3V3": "+3V3", "XIAO.5V": "+5V", "XIAO.GND": "GND"}
    for p in sorted(g):
        if p in names:
            return names[p]
    return next(p for p in sorted(g) if p.startswith("XIAO.")).split(".")[1]


def net_table():
    rows = []
    for g in nets():
        name = net_name(g)
        holes = ", ".join(f"{PADS[p]} ({p.replace('MAX98357A', 'amp').replace('INMP441', 'mic').replace('PCA9685', 'PCA lead')})" for p in sorted(g, key=lambda p: (hcol(PADS[p]), hrow(PADS[p]))))
        rows.append((name, holes))
    order = ["+3V3", "+5V", "GND", "D0", "D1", "D2", "D3", "D4", "D5", "D10"]
    return sorted(rows, key=lambda r: order.index(r[0]))


# ---------------------------------------------------------------- documents
def md_inline(s):  # **bold** and *italic* to HTML
    s = esc(s, quote=False)
    out, bold, ital = [], False, False
    i = 0
    while i < len(s):
        if s.startswith("**", i):
            out.append("</strong>" if bold else "<strong>")
            bold = not bold
            i += 2
        elif s[i] == "*":
            out.append("</em>" if ital else "<em>")
            ital = not ital
            i += 1
        else:
            out.append(s[i])
            i += 1
    t = "".join(out)
    import re
    return re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', t)


def steps_markdown(steps):
    o = ["# Step-by-step: the Coglet carrier board", "",
         "> **Design, not yet built.** Nobody has assembled this board. The layout is checked by computer (the nets, the link routes) "
         "but not on a bench, and the amp's and mic's pin orders are assumed. This page is generated by "
         "[`tools/carrier/carrier.py`](../tools/carrier/carrier.py); don't edit it by hand.", "",
         "The [hole map](../web-programmer/carrier/hole-map.svg) and [CARRIER.md](CARRIER.md) show the whole board at once; this page takes it "
         "one step at a time. Hole names are the **component side's**. The underside print on this board is **mirrored**: wherever you work from "
         "underneath, the guide gives the printed name too.", ""]
    for s in steps:
        o += [f"## Step {s['n']}: {s['title']}", ""]
        if s["view"]:
            o += [f"![Step {s['n']}](../web-programmer/carrier/step-{s['n']:02d}.svg)", ""]
        o += ["**Do this:**", ""] + [f"{i}. {t}" for i, t in enumerate(s["do"], 1)] + [""]
        if s.get("table"):
            o += ["| Net | Holes that must connect |", "|---|---|"] + [f"| {n} | {h} |" for n, h in net_table()] + [""]
            o += ["Then check that these nets are **not** connected to each other: +3V3, +5V and GND against every other net and each other, and D1 against D2.", ""]
        if s.get("leads"):
            o += ["| Carrier pin | Hole | Goes to | Suggested colour |", "|---|---|---|---|"] + [f"| {a} | {h} | {t} | {c} |" for a, h, t, c in LEADS] + [""]
        o += [f"- **{k}:** {v}" for k, v in s["facts"]]
        o += [f"- **Check:** {c}" for c in s["check"]]
        o += [""]
    return "\n".join(o) + "\n"


def carrier_markdown():
    o = ["# Carrier board", "",
         "> **Design, not yet built.** See [the step-by-step guide](CARRIER-STEPS.md) for the order of work. Generated by "
         "[`tools/carrier/carrier.py`](../tools/carrier/carrier.py).", "",
         "The XIAO, the MAX98357A amp and the INMP441 mic go on an **ElectroCookie mini solderable breadboard** (1/4 size, 38.1 × 50.8 mm), "
         "the board the N20 drive in cam-rotation-starter uses. The PCA9685 and the speaker stay off it, on short leads; the servo supply "
         "goes straight to the PCA9685's terminal block.", "",
         "![Hole map of the carrier, component side, row 1 at the top and column A at the left. The XIAO straddles the channel in columns D and H from row 4 to row 12, USB-C toward row 1. The amp stands in column A rows 1 to 7, the PCA9685 lead's header in column J rows 6 to 10 and the mic in column J rows 12 to 17. Dashed lines are the ten underside links.](../web-programmer/carrier/hole-map.svg)", "",
         "## The board", "",
         "| | |", "|---|---|",
         "| Size | 38.1 × 50.8 mm (1.5 × 2.0 in), 1.6 mm FR4, plated through-holes |",
         "| Holes | 0.1 in (2.54 mm) grid, rows 1–17 |",
         "| Strips | Each row is two strips of five joined holes, **A–E** and **F–J**, either side of a 0.3 in centre channel. No power rails. |",
         "| Underside | The print is **mirrored**: A↔J, B↔I, C↔H, D↔G, E↔F, same row. To check your own board, push a wire into top A2, turn the board over and read the hole it comes out of. |", "",
         "## Placement", "",
         "The XIAO's pin rows are 15.24 mm apart (0.6 in), the same as columns D and H. Check this against your module before soldering.", "",
         "| Part | Where | Pins |", "|---|---|---|",
         "| XIAO ESP32 (S3 Sense, C3 or C6), component side up, USB-C toward row 1 | Rows 4–12 over columns D–H | D0–D5 at D5–D10, 5V H5, GND H6, 3V3 H7, D10 H8. **D6, D7, D8 and D9 stay out.** |",
         "| MAX98357A amp, standing on its header | Column A, rows 1–7 | Vin A1, GND A2, DIN A5, BCLK A6, LRC A7. SD and GAIN have no pin. *Pin order assumed.* |",
         "| INMP441 mic, standing on its header | Column J, rows 12–17 | SCK J12, WS J13, SD J14, L/R J15, VDD J16, GND J17. *Pin order assumed.* Where its sound port faces is open. |",
         "| Lead to the PCA9685, a 5-pin header | Column J, rows 6–10 | GND J6, VCC J7, /OE J8, SDA J9, SCL J10 |", "",
         "The amp's DIN, BCLK and LRC sit in the same strips as the XIAO's D0, D1 and D2, so they need no links; the GND, 3V3 and D10 pins of the lead sit in the XIAO's right-hand strips for the same reason.", "",
         "## Nets", "", "| Net | Holes |", "|---|---|"] + [f"| {n} | {h} |" for n, h in net_table()] + ["",
         "These are the nets in [`hardware/wires.csv`](../hardware/wires.csv) that the carrier holds. `python3 tools/carrier/carrier.py --check` computes the nets from the strips, pins and links and fails if they differ.", "",
         "## Links (underside)", "",
         f"Ten links of 30 AWG stranded wire, ends tinned, run on the underside. {len(crossings())} pairs cross; that is fine with insulated wire, but keep every link flat.", "",
         "| Link | Colour | From | To | Job | Cut (estimated) |", "|---|---|---|---|---|---|"]
    for l in sorted(LINKS, key=lambda l: length(route_mm(l))):
        o.append(f"| {l[0]} | {l[3]} | {l[1]} (printed {under(l[1])}) | {l[2]} (printed {under(l[2])}) | {l[5]} | {cut_mm(l)} mm |")
    o += ["", "Cut lengths are the routed length × 1.15 + 10 mm, rounded up to 5 mm. There is no 3D model, so they are estimates: trim each link to fit.", "",
          "## Where it goes", "", "**Pending.** The mechanical design decides where the carrier mounts, where the mic faces and how much height the standing amp and mic may take. "
          "The S3 Sense's camera board stacks on top of the XIAO and needs clearance above it.", "",
          "## Assumptions to confirm on the bench", "",
          "1. The amp breakout's pin order (assumed Vin, GND, SD, GAIN, DIN, BCLK, LRC).",
          "2. The mic breakout's pin order (assumed SCK, WS, SD, L/R, VDD, GND).",
          "3. That your perfboard's underside print is mirrored as described.",
          "4. That the XIAO's rows are 0.6 in apart on your module.",
          "5. The cut lengths."]
    return "\n".join(o) + "\n"


HTML_STYLE = """
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    :root { --bg:#0f1117; --card:#1a1f2e; --line:#2d3748; --text:#e2e8f0; --muted:#94a3b8; --head:#fff; --accent:#60a5fa; --warn:#fbbf24; --ok:#4ade80; }
    @media (prefers-color-scheme: light) { :root { --bg:#f8fafc; --card:#fff; --line:#cbd5e1; --text:#1e293b; --muted:#475569; --head:#0f172a; --accent:#1d4ed8; --warn:#92400e; --ok:#166534; } }
    body { font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif; background:var(--bg); color:var(--text); padding:1.5rem 1rem; line-height:1.5; }
    header, main { max-width:860px; margin:0 auto; }
    header { margin-bottom:1rem; } h1 { font-size:1.6rem; color:var(--head); } header p { color:var(--muted); margin-top:.35rem; font-size:.95rem; }
    a { color:var(--accent); } code { background:rgba(127,127,127,.18); padding:.05rem .3rem; border-radius:4px; font-size:.9em; }
    .banner { background:var(--card); border:1px solid var(--warn); border-radius:10px; padding:.9rem 1.1rem; margin-bottom:1rem; font-size:.92rem; }
    .banner strong { color:var(--warn); }
    nav.steps { display:flex; flex-wrap:wrap; gap:.35rem; margin-bottom:1rem; }
    nav.steps button { background:var(--card); color:var(--muted); border:1px solid var(--line); border-radius:999px; padding:.25rem .65rem; font-size:.78rem; cursor:pointer; }
    nav.steps button[aria-current="step"] { color:var(--head); border-color:var(--accent); background:rgba(96,165,250,.15); }
    .step { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:1.25rem; }
    .step h2 { font-size:1.25rem; color:var(--head); margin-bottom:.5rem; } .crumb { color:var(--muted); font-size:.8rem; }
    .step h3 { font-size:.8rem; text-transform:uppercase; letter-spacing:.05em; color:var(--muted); margin:1rem 0 .35rem; }
    .step ol, .step ul { margin-left:1.3rem; font-size:.95rem; } .step li { margin-bottom:.25rem; }
    img.pic { width:100%; height:auto; display:block; margin:.75rem 0; border-radius:8px; border:1px solid var(--line); background:#f6f8f8; }
    table { border-collapse:collapse; width:100%; font-size:.9rem; } th, td { text-align:left; padding:.3rem .6rem; border-bottom:1px solid var(--line); vertical-align:top; } th { color:var(--muted); width:7rem; }
    .check { border-left:3px solid var(--ok); padding-left:.8rem; list-style:none; margin-left:0 !important; }
    .pager { display:flex; justify-content:space-between; margin-top:1.25rem; gap:.5rem; }
    .pager button { background:var(--accent); color:#fff; border:0; border-radius:8px; padding:.5rem 1.1rem; font-size:.95rem; cursor:pointer; }
    .pager button:disabled { opacity:.35; cursor:default; } .pager button.ghost { background:transparent; color:var(--accent); border:1px solid var(--accent); }
    .site-nav, .site-pager { max-width:860px; }
"""


def html_page(steps):
    sec = []
    for s in steps:
        b = [f'<section class="step" id="step-{s["n"]}" hidden>',
             f'<p class="crumb">Step {s["n"]} of {len(steps)}</p><h2>{md_inline(s["title"])}</h2>']
        if s["view"]:
            b.append(f'<img class="pic" src="carrier/step-{s["n"]:02d}.svg" alt="{esc(s["title"])}. Hole map for this step." loading="lazy">')
        b.append("<h3>Do this</h3><ol>" + "".join(f"<li>{md_inline(t)}</li>" for t in s["do"]) + "</ol>")
        if s.get("table"):
            b.append("<h3>Nets that must connect</h3><table><tr><th>Net</th><td><b>Holes</b></td></tr>" +
                     "".join(f"<tr><th>{esc(n)}</th><td>{esc(h)}</td></tr>" for n, h in net_table()) + "</table>"
                     "<p>Then check that +3V3, +5V and GND are <strong>not</strong> connected to each other or to any other net, and D1 is not connected to D2.</p>")
        if s.get("leads"):
            b.append("<h3>The lead</h3><table><tr><th>Pin</th><td><b>Hole · goes to · colour</b></td></tr>" +
                     "".join(f"<tr><th>{esc(a)}</th><td>{h} · {esc(t)} · {c}</td></tr>" for a, h, t, c in LEADS) + "</table>")
        if s["facts"]:
            b.append("<h3>Details</h3><table>" + "".join(f"<tr><th>{esc(k)}</th><td>{md_inline(v)}</td></tr>" for k, v in s["facts"]) + "</table>")
        if s["check"]:
            b.append('<h3>Check</h3><ul class="check">' + "".join(f"<li>{md_inline(c)}</li>" for c in s["check"]) + "</ul>")
        b.append("</section>")
        sec.append("\n".join(b))
    titles = [s["title"] for s in steps]
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Build the carrier board · Coglet</title>
  <link rel="icon" href="data:,">
  <link rel="stylesheet" href="nav.css">
  <style>{HTML_STYLE}</style>
</head>
<body>
<!-- Generated by tools/carrier/carrier.py. Do not edit by hand. -->
<header>
  <h1>Build the carrier board</h1>
  <p>Solder the XIAO, amp and mic onto one perfboard, one link at a time, with a picture and a check for each step.
  The full hole map is <a href="carrier/hole-map.svg">here</a>.</p>
</header>
<main>
  <div class="banner"><strong>Design, not yet built.</strong> This layout has been checked by computer (the nets and the link routes)
  but nobody has assembled it. The amp's and mic's pin orders are assumed: check them against your boards' labels. The mechanical
  fit (where the carrier mounts, where the mic faces) is not designed yet. Prefer jumper leads? Use the
  <a href="wiring.html">wire-by-wire guide</a> instead.</div>
  <nav class="steps" id="nav" aria-label="Steps"></nav>
  <div id="steps">
{chr(10).join(sec)}
  </div>
  <div class="pager"><button class="ghost" id="prev">Back</button><button id="next">Next step</button></div>
</main>
<script>
const TITLES = {titles!r};
const secs = [...document.querySelectorAll('.step')], nav = document.getElementById('nav');
const btns = TITLES.map((t, i) => {{ const b = document.createElement('button'); b.textContent = (i + 1) + '. ' + t.replace(/\\*/g, '');
  b.onclick = () => show(i); nav.appendChild(b); return b; }});
let cur = 0;
function show(i, push = true) {{
  cur = Math.max(0, Math.min(secs.length - 1, i));
  secs.forEach((s, k) => {{ s.hidden = k !== cur; }});
  btns.forEach((b, k) => k === cur ? b.setAttribute('aria-current', 'step') : b.removeAttribute('aria-current'));
  document.getElementById('prev').disabled = cur === 0; document.getElementById('next').disabled = cur === secs.length - 1;
  if (push) history.replaceState(null, '', '#step-' + (cur + 1));
  window.scrollTo({{ top: 0 }});
}}
document.getElementById('prev').onclick = () => show(cur - 1); document.getElementById('next').onclick = () => show(cur + 1);
const m = /^#step-(\\d+)$/.exec(location.hash); show(m ? Number(m[1]) - 1 : 0, false);
</script>
<script src="site.js"></script>
<noscript><p>This page needs JavaScript for the step-by-step view. The same steps are in
<a href="https://github.com/ricklon/coglet/blob/main/docs/CARRIER-STEPS.md">CARRIER-STEPS.md</a>.</p></noscript>
</body>
</html>
"""


def outputs():
    steps = build_steps()
    files = {OUT / "hole-map.svg": hole_map_svg(),
             ROOT / "docs" / "CARRIER.md": carrier_markdown(),
             ROOT / "docs" / "CARRIER-STEPS.md": steps_markdown(steps),
             ROOT / "web-programmer" / "carrier.html": html_page(steps)}
    for s in steps:
        if s["view"]:
            files[OUT / f"step-{s['n']:02d}.svg"] = step_svg(s)
    return files


def main():
    problems = check()
    if problems:
        print("carrier check FAILED:\n  " + "\n  ".join(problems))
        sys.exit(1)
    files = outputs()
    if "--check" in sys.argv:
        stale = [str(p.relative_to(ROOT)) for p, t in files.items() if not p.exists() or p.read_text() != t]
        stale += [str(p.relative_to(ROOT)) for p in OUT.glob("step-*.svg") if p not in files]
        if stale:
            print("generated files are out of date; run python3 tools/carrier/carrier.py:\n  " + "\n  ".join(stale))
            sys.exit(1)
        print(f"carrier OK: {len(nets())} nets match hardware/wires.csv, {len(LINKS)} links clear of each other, "
              f"{len(crossings())} crossing pairs, {len(files)} generated files current")
        return
    OUT.mkdir(parents=True, exist_ok=True)
    for p in OUT.glob("step-*.svg"):
        p.unlink()
    for p, t in files.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(t)
    print(f"wrote {len(files)} files; {len(nets())} nets match wires.csv; crossings: {crossings()}")


if __name__ == "__main__":
    main()
