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
BOARD_T = 1.6
PITCH = 2.54
# Four M2 corner holes 31.8 x 44.5 mm apart, as on the same board in cam-rotation-starter. Their screws and
# supports cover the corner holes A1, J1, A17 and J17, so nothing goes in those, and no link runs near them.
M2_HOLES = [(sx * 31.8 / 2, sy * 44.5 / 2) for sx in (-1, 1) for sy in (-1, 1)]
CORNER_HOLES = {"A1", "J1", "A17", "J17"}
CORNER_KEEP_MM = 3.5
# A link keeps this far (centre to centre) from every hole soldered after it is laid: the iron must not reach it.
CLEAR_MM = 1.6
PARALLEL_MM = 0.8  # links that don't cross keep this far apart
# The XIAO body, from Seeed's land pattern: 17.8 x 21 mm, pin rows 15.25 mm apart, pin pitch 2.54 mm.
XIAO_W, XIAO_L = 17.8, 21.0


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


def grid_mm(c, r):
    """A point on the underside in grid units: c is a column index (A=0 … E=4, F=7 … J=11), r a row."""
    return ((c - 5.5) * PITCH, (r - 9) * PITCH)


# ---------------------------------------------------------------- the pins fitted (pad -> hole)
# XIAO, component side up, USB-C toward row 1 at the board's edge: D0..D5 in column D (rows 1-6), 5V, GND, 3V3,
# D10 in column H (rows 1-4). Pins D6, D7, D8 and D9 are left out so their strips stay free.
PADS = {
    "XIAO.D0": "D1", "XIAO.D1": "D2", "XIAO.D2": "D3", "XIAO.D3": "D4", "XIAO.D4": "D5", "XIAO.D5": "D6",
    "XIAO.5V": "H1", "XIAO.GND": "H2", "XIAO.3V3": "H3", "XIAO.D10": "H4",
    # MAX98357A lying flat on a straight 7-pin header in column D, rows 17-11 (as built): Vin, GND, SD, GAIN, DIN,
    # BCLK, LRC. SD is fitted but connects to nothing (its strip stays empty); GAIN is tied to Vin (6 dB, as built).
    "MAX98357A.Vin": "D17", "MAX98357A.GND": "D16", "MAX98357A.SD": "D15", "MAX98357A.GAIN": "D14",
    "MAX98357A.DIN": "D13", "MAX98357A.BCLK": "D12", "MAX98357A.LRC": "D11",
    # INMP441 off the board on 75 mm wires (L/R tied to GND at the mic). Each wire lands straight in the strip it
    # needs: the signals in the XIAO's D1-D3 strips (as built), power on the row-8 (GND) and row-9 (3V3) rails.
    # Its power wires share B8 and B9 with the PCA9685's (two wire ends twisted into one hole, as built).
    "INMP441.SCK": "C2", "INMP441.WS": "C3", "INMP441.SD": "C4", "INMP441.GND": "B8", "INMP441.VDD": "B9",
    # PCA9685 off the board on 90 mm wires, landing the same way. SDA, SCL, VCC and GND as built; /OE is the design's
    # choice (any free hole in D10's strip).
    "PCA9685.SDA": "C5", "PCA9685.SCL": "B6", "PCA9685.GND": "B8", "PCA9685.VCC": "B9", "PCA9685./OE": "J4",
}
PAD_AT = {h: p for p, h in PADS.items()}
UNCONNECTED = {"MAX98357A.SD"}  # fitted, joined to nothing
# Off-board parts on wires: pad -> (wire colour, lead length mm). The wire ends are soldered in from the component
# side after the modules.
LEAD_WIRES = {
    "INMP441.SCK": ("white", 75), "INMP441.WS": ("blue", 75), "INMP441.SD": ("yellow", 75),
    "INMP441.VDD": ("red", 75), "INMP441.GND": ("black", 75),
    "PCA9685.SDA": ("green", 90), "PCA9685.SCL": ("yellow", 90), "PCA9685.VCC": ("red", 90),
    "PCA9685.GND": ("black", 90), "PCA9685./OE": ("brown", 90),
}
AS_BUILT = {"INMP441.SCK", "INMP441.WS", "INMP441.SD", "INMP441.GND", "INMP441.VDD", "PCA9685.SDA", "PCA9685.SCL",
            "PCA9685.GND", "PCA9685.VCC"}  # holes reported from the board

# Component-side jumpers (as built): they make row 8 a ground rail and row 9 a 3.3 V rail across the whole board,
# for the off-board parts' power wires. Soldered before the XIAO, since H8 sits under the edge of its body.
# id, from hole, to hole, colour, what it does
JUMPERS = [
    ("T1", "I2", "H8", "colour not recorded", "Ground from the XIAO's GND strip (row 2) to row 8."),
    ("T2", "I3", "H9", "colour not recorded", "3.3 V from the XIAO's 3V3 strip (row 3) to row 9."),
    ("T3", "E8", "F8", "orange", "Joins row 8 across the channel: a ground rail on both halves."),
    ("T4", "E9", "F9", "orange", "Joins row 9 across the channel: a 3.3 V rail on both halves."),
]
PART_NAME = {"XIAO": "XIAO", "MAX98357A": "amp", "INMP441": "mic", "PCA9685": "PCA lead"}

# ---------------------------------------------------------------- the links (underside, 30 AWG stranded)
COLORS = {"red": "#d12a1f", "black": "#1b1b1b", "orange": "#e8801a", "yellow": "#e0b800", "green": "#1f9a3c",
          "blue": "#2f66d8", "violet": "#8a3fc0", "grey": "#808890", "white": "#ffffff", "brown": "#8a5a1c"}
# id, from hole, to hole, colour, corners of the route in grid units (see grid_mm), what it does.
# The list is the build order: the check requires every link to keep CLEAR_MM from each hole soldered after it.
LINKS = [
    ("L1", "E3", "E11", "green", [(4.65, 3), (4.65, 11)], "I2S word select: D2's strip to the amp's LRC."),
    ("L2", "E2", "E12", "yellow", [(5.0, 2), (5.0, 12)], "I2S bit clock: D1's strip to the amp's BCLK."),
    ("L3", "E1", "E13", "white", [(5.5, 1), (5.5, 13)], "Speaker data: D0's strip to the amp's DIN."),
    ("L4", "F1", "E17", "red", [(6.0, 1), (6.0, 17)], "+5 V from the XIAO's 5V strip to the amp's Vin."),
    ("L5", "F2", "E16", "black", [(6.35, 2), (6.35, 16)], "Ground from the XIAO's GND strip to the amp's GND."),
    ("L6", "C14", "C17", "red", [], "Ties the amp's GAIN to its Vin (+5 V): 6 dB. This is wire J2 in the wire list."),
]

# The amp lies flat on its header, its body over the pins and toward column J (as built). Approximate outline.
AMP_W, AMP_D = 17.8, 18.0  # along the rows, and from the pin row toward J


def amp_box():
    rows = [hrow(PADS[p]) for p in PADS if p.startswith("MAX98357A.")]
    vc = (mm_v(min(rows)) + mm_v(max(rows))) / 2
    u0 = mm_u("D") - 1.3
    return u0, u0 + AMP_D, vc - AMP_W / 2, vc + AMP_W / 2


def under_amp(h):
    u, v = hole_mm(h)
    u0, u1, v0, v1 = amp_box()
    return u0 < u < u1 and v0 < v < v1 and h not in PAD_AT


def route_mm(link):
    """The underside route of a link in board millimetres: hole, corners, hole."""
    _, a, b, _, via, _ = link
    return [hole_mm(a)] + [grid_mm(c, r) for c, r in via] + [hole_mm(b)]


XIAO_U = (mm_u("D") + mm_u("H")) / 2
XIAO_V = (mm_v(hrow(PADS["XIAO.D0"])) + mm_v(hrow(PADS["XIAO.D0"]) + 6)) / 2


def under_xiao(h):
    u, v = hole_mm(h)
    return abs(u - XIAO_U) < XIAO_W / 2 and abs(v - XIAO_V) < XIAO_L / 2


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
    for _, a, b, *_ in LINKS + JUMPERS:
        union(strip(a), strip(b))
    groups = {}
    for pad, h in PADS.items():
        groups.setdefault(find(strip(h)), set()).add(pad)
    return sorted((frozenset(g) for g in groups.values()), key=lambda g: sorted(g))


def connected_nets():
    return [g for g in nets() if len(g) > 1]


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

    with open(ROOT / "hardware" / "wires.csv", encoding="utf-8") as f:
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
    got, want = connected_nets(), expected_nets()
    for g in nets():  # a fitted pin that should connect to nothing must be alone in its strip
        if len(g) > 1 and g & UNCONNECTED:
            problems.append("an unconnected pin is joined to something: " + ", ".join(sorted(g)))
    for g in nets():
        if len(g) == 1 and not g <= UNCONNECTED:
            problems.append("a pin connects to nothing: " + ", ".join(g))
    if got != want:
        for g in got:
            if g not in want:
                problems.append("carrier net not in wires.csv: " + ", ".join(sorted(g)))
        for g in want:
            if g not in got:
                problems.append("wires.csv net missing on the carrier: " + ", ".join(sorted(g)))
    # one pin per hole; only off-board wire ends may share a hole (twisted together)
    by_hole = {}
    for p, h in PADS.items():
        by_hole.setdefault(h, []).append(p)
    for h, ps in sorted(by_hole.items()):
        if len(ps) > 1 and not all(p in LEAD_WIRES for p in ps):
            problems.append(f"{h} holds more than one pin: {', '.join(ps)}")
    # every link end is a free hole, and no hole is used twice
    seen = {}
    for lid, a, b, *_ in LINKS + JUMPERS:
        for h in (a, b):
            if h in PAD_AT:
                problems.append(f"{lid} ends in {h}, which holds the pin {PAD_AT[h]}")
            if h in seen:
                problems.append(f"{lid} and {seen[h]} both end in {h}")
            seen[h] = lid
    # the corner holes stay empty: the mounting screws and supports cover them
    for h in sorted(CORNER_HOLES & (set(PAD_AT) | set(seen))):
        problems.append(f"{h} is a corner hole under a mounting screw, but holds {PAD_AT.get(h) or seen[h]}")
    # links keep clear of each other: they may cross (insulated wire) but never run closer than PARALLEL_MM
    routes = {l[0]: route_mm(l) for l in LINKS}
    ids = list(routes)
    for i, x in enumerate(ids):
        for y in ids[i + 1:]:
            for p, q in zip(routes[x], routes[x][1:]):
                for r, s in zip(routes[y], routes[y][1:]):
                    d, crossed = seg_dist(p, q, r, s)
                    if not crossed and d < PARALLEL_MM:
                        problems.append(f"{x} and {y} run within {d:.2f} mm of each other")
    # every hole soldered after a link is laid (all the module pins, and the ends of later links) keeps CLEAR_MM
    # from it, so the iron never meets the wire's insulation
    for i, link in enumerate(LINKS):
        lid, pts = link[0], routes[link[0]]
        later = {h: f"pin {p}" for p, h in PADS.items()}
        for j in JUMPERS:  # soldered after the links
            later[j[1]] = later[j[2]] = f"{j[0]}'s end"
        for l in LINKS[i + 1:]:
            later[l[1]] = later[l[2]] = f"{l[0]}'s end"
        for h, what in later.items():
            if h in (link[1], link[2]):
                continue
            d = min(seg_dist(hole_mm(h), hole_mm(h), p, q)[0] for p, q in zip(pts, pts[1:]))
            if d < CLEAR_MM:
                problems.append(f"{lid} runs {d:.2f} mm from {h} ({what}), which is soldered later")
        for m in M2_HOLES:
            d = min(seg_dist(m, m, p, q)[0] for p, q in zip(pts, pts[1:]))
            if d < CORNER_KEEP_MM:
                problems.append(f"{lid} runs {d:.2f} mm from a corner mounting hole")
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
    # corner mounting holes (M2): their screw heads and supports cover A1, J1, A17 and J17
    for mu, mv in M2_HOLES:
        mx, my = view.x(mu), view.y(mv)
        o.append(f'<circle cx="{mx:.1f}" cy="{my:.1f}" r="{1.9 * S:.1f}" fill="none" stroke="#9aa6a0" stroke-width="1" stroke-dasharray="3 3"/>')
        o.append(f'<circle cx="{mx:.1f}" cy="{my:.1f}" r="{1.1 * S:.1f}" fill="#ffffff" stroke="#9aa6a0" stroke-width="1.4"/>')
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
    for pad, h in sorted(PADS.items(), key=lambda ph: ph[0] not in LEAD_WIRES):  # wire rings first, so pin labels sit on top
        x, y = view.hole(h)
        hot = pad in highlight
        if pad in LEAD_WIRES:  # a wire end from an off-board part: a ring in the wire's colour
            col = COLORS.get(LEAD_WIRES[pad][0], "#a16207")
            o.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{5.5 if hot else 4.5}" fill="#ffffff" stroke="#55606a" stroke-width="{2.6 if hot else 1.6}"/>')
            o.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{3.4 if hot else 2.6}" fill="{col}"/>')
            continue
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
    if color in ("#1b1b1b", "#ffffff") and width > 3:  # black and white wires need an outline to read on the board
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
    # XIAO: body 17.8 x 21 mm centred on its pins (Seeed's land pattern); USB-C at the row-1 end
    u0, u1 = XIAO_U - XIAO_W / 2, XIAO_U + XIAO_W / 2
    v0, v1 = XIAO_V - XIAO_L / 2, XIAO_V + XIAO_L / 2
    xa, xb = sorted((view.x(u0), view.x(u1)))
    o.append(f'<rect x="{xa:.1f}" y="{view.y(v0):.1f}" width="{xb - xa:.1f}" height="{(v1 - v0) * S:.1f}" rx="4" fill="#cfe0ef" fill-opacity="0.55" stroke="#4f7da8" stroke-width="1.6" stroke-dasharray="5 3"/>')
    cx = (xa + xb) / 2
    o.append(f'<rect x="{cx - 36:.1f}" y="{view.y(v0) - 8:.1f}" width="72" height="20" rx="4" fill="#b8c6d3" stroke="#4f7da8"/>')
    o.append(f'<text x="{cx:.1f}" y="{view.y(v0) + 6:.1f}" text-anchor="middle" font-size="10" fill="#2f5a82" {FONT}>USB-C</text>')
    o.append(f'<text x="{cx:.1f}" y="{view.y(XIAO_V) + 4:.1f}" text-anchor="middle" font-size="13" font-weight="700" fill="#2f5a82" {FONT}>XIAO</text>')
    # the amp, lying flat on its header: approximate outline over the pins and toward column J
    au0, au1, av0, av1 = amp_box()
    xa, xb = sorted((view.x(au0), view.x(au1)))
    o.append(f'<rect x="{xa:.1f}" y="{view.y(av0):.1f}" width="{xb - xa:.1f}" height="{(av1 - av0) * S:.1f}" rx="4" fill="#c9a6e0" fill-opacity="0.35" stroke="#7d4fa8" stroke-width="1.6" stroke-dasharray="5 3"/>')
    o.append(f'<text x="{(xa + xb) / 2 + 12:.1f}" y="{view.y((av0 + av1) / 2) + 4:.1f}" text-anchor="middle" font-size="12" font-weight="700" fill="#5b3a80" {HALO} {FONT}>amp (flat)</text>')
    return o


def hole_map_svg():
    v = View(False)
    body = footprints(v) + jumper_lines(v) + pad_dots(v)
    for l in LINKS:  # links are on the other side: drawn faint and dashed
        pts = " ".join(f"{v.pt(p)[0]:.1f},{v.pt(p)[1]:.1f}" for p in route_mm(l))
        body.append(f'<polyline points="{pts}" fill="none" stroke="{COLORS[l[3]]}" stroke-width="2" stroke-dasharray="4 3" opacity="0.55"/>')
    return board_svg(v, "Coglet carrier: hole map", "Dots: pins. Rings: wire ends. Solid: jumpers. Dashed: links below.", body)


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
    body = footprints(v) + jumper_lines(v, hot=kind == "jumpers") + pad_dots(v, highlight=hot)
    rings = [PADS[p] for p in hot] + ([h for j in JUMPERS for h in (j[1], j[2])] if kind == "jumpers" else [])
    for h in rings:
        x, y = v.hole(h)
        body.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="8" fill="none" stroke="{RING}" stroke-width="2.4"/>')
    return board_svg(v, f"Step {step['n']}: {step['title']}", step.get("sub", "Component side."), body)


def jumper_lines(view, hot=False):
    """The component-side jumpers, drawn solid (the underside links are dashed)."""
    o = []
    for _, a, b, c, _ in JUMPERS:
        (x1, y1), (x2, y2) = view.hole(a), view.hole(b)
        col = COLORS.get(c, "#808890")
        w = 4.5 if hot else 2.6
        o.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#55606a" stroke-width="{w + 1.4}" stroke-linecap="round"/>')
        o.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{col}" stroke-width="{w}" stroke-linecap="round"/>')
    return o


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

    def pinlist(prefix):
        return ", ".join(f"{p.split('.')[1]} {PADS[p]}" for p in PADS if p.startswith(prefix))

    add(kind="bench", title="Prepare the modules", sub="Component side. Dots are the pins you fit; rings are where wires land later.",
        pads=[p for p in PADS if p not in LEAD_WIRES], view="top",
        do=["**XIAO:** fit header pins to left row D0–D5 and right row 5V, GND, 3V3, D10 only. Leave **D6, D7, D8 and D9 out**, so their strips stay free. Fit the pins to the module with the perfboard as the jig, so they come out square, and solder them to the module only.",
            "**MAX98357A amp:** a straight 7-pin header, all seven kept: Vin, GND, SD, GAIN, DIN, BCLK, LRC. It lies flat on the carrier.",
            "**INMP441 mic:** solder its five 75 mm wires to the mic (white SCK, blue WS, yellow SD, red VDD, black GND) and tie **L/R to GND** on the mic board. That is wire J1 in the [wiring guide](wiring.html).",
            "**PCA9685:** solder its five 90 mm wires to the breakout (green SDA, yellow SCL, red VCC, black GND, brown /OE) at one end's header. Its other end has the same header; solder the /OE pull-up (about 10 kΩ, /OE to VCC) there, so it keeps clear of the wires. Remove the breakout's /OE pull-down if it has one: [wiring guide](wiring.html), stage 1.",
            "Keep the four corner holes (A1, J1, A17, J17) empty: the mounting screws and their supports cover them."],
        facts=[("Board", "ElectroCookie mini solderable breadboard, 38.1 × 50.8 mm, 17 rows"),
               ("Check", "Hold each header over the hole map: every fitted pin lands on a dotted hole.")],
        check=["Nothing is soldered to the carrier yet.", "Meter between L/R and GND on the mic: continuity."])
    for l in LINKS:  # in build order: each link stays clear of every hole soldered after it
        lid, a, b, col, via, job = l
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
        trim = [h for h in (a, b) if under_xiao(h) or under_amp(h)]
        over = " and ".join(dict.fromkeys(("the XIAO" if under_xiao(h) else "the amp") for h in trim))
        add(kind="link", link=lid, title=f"{lid}, {col}, {under(a)} → {under(b)}", view="under",
            do=["Turn the board over (underside up).",
                f"Start at hole printed **{under(a)}** (top {a}): push the tinned end in and solder it.",
                "Lay the wire flat along the drawn path; pinch the corners and hold them with Kapton tape.",
                f"End at hole printed **{under(b)}** (top {b}): push it in and solder it."]
               + ([f"Turn the board back over and trim the end{'s' if len(trim) > 1 else ''} in {' and '.join(trim)} flush: "
                   f"{over} sits over " + ("them." if len(trim) > 1 else "it.")] if trim else [])
               + ["Check, as below."],
            facts=[("Wire", f"{lid}, {col}, 30 AWG stranded"),
                   ("Cut", f"{cut_mm(l)} mm, estimated from the drawn route: trim to fit"),
                   ("From → to", f"{a} → {b} (printed {under(a)} → {under(b)} on the underside)"),
                   ("Side", "underside (link)"),
                   ("What it does", job)],
            check=[f"{a} connects to {b}." + (f" Both strips will join {joins}." if (pa or pb) else ""),
                   "It must not connect to: " + (", ".join(dict.fromkeys(near)) if near else "any other hole.")])
    add(kind="jumpers", title="Power rails: four jumpers", view="top",
        sub="Component side. The four jumpers make row 8 ground and row 9 3.3 V.",
        do=["These go on the **component side**, before the XIAO: H8 sits under the edge of its body.",
            "**" + "; ".join(f"{j[0]}: {j[1]} → {j[2]}" for j in JUMPERS) + "**.",
            "T1 and T2 bring ground and 3.3 V down the right half to rows 8 and 9; T3 and T4 carry both rails across the channel. "
            "The mic's and the PCA9685's power wires land on these rails later.",
            "Keep the jumpers low and clear of the holes around them."],
        facts=[(j[0], f"{j[1]} → {j[2]}, {j[3]}: {j[4]}") for j in JUMPERS],
        check=["Row 8 (any hole, A–J) connects to H2's strip (GND); row 9 connects to H3's strip (3V3).",
               "Row 9 must **not** connect to row 1 (5V) or to row 8."])
    xiao = [p for p in PADS if p.startswith("XIAO.")]
    add(kind="part", title="Solder the XIAO", sub="Component side. The XIAO's pins go in the circled holes.", pads=xiao, view="top",
        do=["Component side up, **USB-C toward row 1** (the top edge), so the port sits at the board's edge. Left pin row in column D (D0 at D1 … D5 at D6), right row in column H (5V at H1, GND H2, 3V3 H3, D10 at H4).",
            "Solder each pin from the underside. Check the pins you pulled (D6–D9) are not in the board.",
            "S3 Sense: the camera board stacks on top of the XIAO, so allow height above it."],
        facts=[("Pins", pinlist("XIAO.")),
               ("USB-C", "Seeed's land pattern puts pin 1 2.9 mm from the body's USB end, so the body ends about 2.2 mm inside the board's row-1 edge. The cable plugs in from that edge.")],
        check=["Each XIAO pin hole reads the same net as its link end (see the continuity check below)."])
    amp = [p for p in PADS if p.startswith("MAX98357A.")]
    add(kind="part", title="Solder the amp, flat", sub="Component side. The amp's pins go in column D, rows 11–17.", pads=amp, view="top",
        do=["Lay the MAX98357A flat on its straight header, pins in column D: **Vin D17, GND D16, SD D15, GAIN D14, DIN D13, BCLK D12, LRC D11**. Its board lies over the pins and toward column J, the speaker terminal at the far edge.",
            "Solder from the underside. SD's strip (row 15, A–E) holds nothing else: SD stays unconnected. GAIN's strip is tied to Vin by L6, which sets 6 dB.",
            "The speaker's two wires go to the amp's own terminal later."],
        facts=[("Pins", pinlist("MAX98357A."))],
        check=["D17 connects to H1 (+5 V), D16 to H2 (GND) and D14 to D17 (GAIN to Vin).",
               "D13, D12 and D11 connect to D1, D2 and D3 (the XIAO's D0, D1 and D2) through L3, L2 and L1.",
               "D15 (SD) connects to nothing."])

    def lead_rows(prefix):
        return ", ".join(f"{p.split('.')[1]} ({LEAD_WIRES[p][0]}) {PADS[p]}" for p in PADS if p.startswith(prefix))

    mic = [p for p in PADS if p.startswith("INMP441.")]
    add(kind="part", title="Solder the mic's wires", sub="Component side. The mic's five wires land in the circled holes.", pads=mic, view="top",
        do=["The mic sits off the board, on its 75 mm wires, where the head has a sound opening. Each wire lands in the strip of the XIAO pin it needs, so it needs no link.",
            "Push each wire in from the component side and solder it underneath: **" + lead_rows("INMP441.") + "**.",
            "Red VDD goes on the row-9 rail (3.3 V) and black GND on the row-8 rail. Never on row 1, which is 5 V.",
            "B8 and B9 each take two wires, the mic's and the PCA9685's: twist the two tinned ends together, push them in and solder them as one."],
        facts=[("Wires", lead_rows("INMP441."))],
        check=["C2 connects to D2 (D1, bit clock), C3 to D3 (D2, word select), C4 to D4 (D3, mic data).",
               f"{PADS['INMP441.VDD']} connects to H3 (3V3) and {PADS['INMP441.GND']} to H2 (GND)."])
    pca = [p for p in PADS if p.startswith("PCA9685.")]
    add(kind="part", title="Solder the PCA9685's wires", sub="Component side. The PCA9685's five wires land in the circled holes.", pads=pca, view="top",
        do=["The PCA9685 sits off the board, on its 90 mm wires. Each wire lands in the strip of the XIAO pin it needs, so it needs no link.",
            "Push each wire in from the component side and solder it underneath: **" + lead_rows("PCA9685.") + "**.",
            "Red VCC is the logic supply: the row-9 rail (3.3 V), not 5 V. Black GND goes on the row-8 rail. The servo supply never comes to the carrier.",
            "The /OE pull-up (about 10 kΩ to VCC) sits on the PCA9685 board, across /OE and VCC on the header at the end away from the wires."],
        facts=[("Wires", lead_rows("PCA9685."))],
        check=["C5 connects to D5 (SDA, D4) and B6 to D6 (SCL, D5).",
               f"{PADS['PCA9685.GND']} connects to H2 (GND), {PADS['PCA9685.VCC']} to H3 (3V3) and {PADS['PCA9685./OE']} to H4 (D10)."])
    add(kind="check", title="Continuity check", view="top", pads=list(PADS),
        sub="Component side. Probe the holes in each row of the table.",
        do=["Meter on continuity: connected beeps. Probe from the component side."],
        facts=[], check=[], table=True)
    add(kind="leads", title="Off the board", view=None,
        do=["Speaker: its two wires to the amp's own screw terminal (+ and −).",
            "Servo power stays off the carrier: the supply's + goes through the switch to the PCA9685's V+ terminal, and its − to the PCA9685's GND terminal, as in stage 8 of the [wiring guide](wiring.html).",
            "Leave the switch off and every servo unplugged."],
        facts=[], check=["Then run the bench checks on the [Check](console.html) page."], leads=True)
    return steps


LEADS = [(f"{p.split('.')[0].replace('INMP441', 'mic')} {p.split('.')[1]}", PADS[p],
          f"{LEAD_WIRES[p][1]} mm" + ("" if p in AS_BUILT else ", hole chosen by the design"), LEAD_WIRES[p][0])
         for p in PADS if p in LEAD_WIRES]


def net_name(g):
    names = {"XIAO.3V3": "+3V3", "XIAO.5V": "+5V", "XIAO.GND": "GND"}
    for p in sorted(g):
        if p in names:
            return names[p]
    return next(p for p in sorted(g) if p.startswith("XIAO.")).split(".")[1]


def net_table():
    rows = []
    for g in connected_nets():
        name = net_name(g)
        holes = ", ".join(f"{PADS[p]} ({p.replace('MAX98357A', 'amp').replace('INMP441', 'mic').replace('PCA9685', 'PCA lead')})" for p in sorted(g, key=lambda p: (hcol(PADS[p]), hrow(PADS[p]), p)))
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
         "> **Design, matched to a prototype in progress.** The XIAO's and amp's positions and the mic's and PCA9685's signal "
         "wires follow a board being built; the underside links and the power wire holes are the design's. The layout is checked by "
         "computer (the nets, the link routes) but not on a bench. This page is generated by "
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
            o += ["| Wire | Hole | Lead | Colour |", "|---|---|---|---|"] + [f"| {a} | {h} | {t} | {c} |" for a, h, t, c in LEADS] + [""]
        o += [f"- **{k}:** {v}" for k, v in s["facts"]]
        o += [f"- **Check:** {c}" for c in s["check"]]
        o += [""]
    return "\n".join(o) + "\n"


def carrier_markdown():
    o = ["# Carrier board", "",
         "> **Design, matched to a prototype in progress.** The XIAO's and amp's positions and the mic's and PCA9685's signal wires "
         "follow a board being built; the underside links and the power wire holes are the design's. See [the step-by-step guide](CARRIER-STEPS.md) "
         "for the order of work. Generated by [`tools/carrier/carrier.py`](../tools/carrier/carrier.py).", "",
         "The XIAO and the MAX98357A amp go on an **ElectroCookie mini solderable breadboard** (1/4 size, 38.1 × 50.8 mm), "
         "the board the N20 drive in cam-rotation-starter uses. The INMP441 mic (75 mm wires) and the PCA9685 (90 mm wires) sit off it, "
         "where the robot needs them, and their wires land straight in the carrier's holes. The speaker wires to the amp's terminal; the servo "
         "supply goes straight to the PCA9685's terminal block.", "",
         f"![Hole map of the carrier, component side, row 1 at the top and column A at the left. The XIAO straddles the channel in columns D and H, its pins in rows 1 to 7 and its USB-C port at the row-1 edge. The amp lies flat with its pins in column D rows 11 to 17 and its body toward column J. Jumpers I2 to H8 and I3 to H9, and E8 to F8 and E9 to F9, make row 8 a ground rail and row 9 a 3.3 V rail. The mic's signal wires land in C2, C3 and C4 and the PCA9685's in C5, B6 and J4; both parts' power wires share B8 (ground) and B9 (3.3 V). The four corner holes are left empty for the mounting screws. Dashed lines are the {len(LINKS)} underside links.](../web-programmer/carrier/hole-map.svg)", "",
         "## The board", "",
         "| | |", "|---|---|",
         f"| Size | {BOARD_W} × {BOARD_H} mm (1.5 × 2.0 in), {BOARD_T} mm FR4, plated through-holes |",
         "| Holes | 0.1 in (2.54 mm) grid, rows 1–17 |",
         "| Strips | Each row is two strips of five joined holes, **A–E** and **F–J**, either side of a 0.3 in centre channel. No power rails. |",
         "| Underside | The print is **mirrored**: A↔J, B↔I, C↔H, D↔G, E↔F, same row. To check your own board, push a wire into top A2, turn the board over and read the hole it comes out of. |",
         "| Mounting | Four Ø2.2 mm (M2) corner holes, 31.8 × 44.5 mm apart (as measured on this board in cam-rotation-starter). Their screws and supports cover A1, J1, A17 and J17, so those holes stay empty and no link runs within 3.5 mm of a corner hole. The two M3 holes in the channel aren't used: the row-1 one is under the XIAO and the row-17 one under the amp. |", "",
         "## Placement", "",
         "Seeed's land pattern puts the XIAO's pin rows 15.25 mm apart (0.6 in), the same as columns D and H, with its body 17.8 × 21 mm centred on the pins. "
         "Its pins sit in rows 1–7, so its USB-C end is at the board's row-1 edge: pin 1 is 2.9 mm from the body's end, which leaves the body about 2.2 mm inside the board edge. "
         "USB is the robot's 5 V input and its console, so the cable stays plugged in and has to come straight in from that edge.", "",
         "| Part | Where | Pins |", "|---|---|---|",
         "| XIAO ESP32 (S3 Sense, C3 or C6), component side up, USB-C at the row-1 edge | Rows 1–7 over columns D–H | D0–D5 at D1–D6, 5V H1, GND H2, 3V3 H3, D10 H4. **D6, D7, D8 and D9 stay out.** |",
         "| MAX98357A amp, flat on a straight header, body toward column J | Column D, rows 11–17 | Vin D17, GND D16, SD D15, GAIN D14, DIN D13, BCLK D12, LRC D11. SD connects to nothing; GAIN is tied to Vin (6 dB). |",
         "| INMP441 mic, off the board on 75 mm wires, L/R tied to GND on the mic | Wire ends | " + ", ".join(f"{p.split('.')[1]} {PADS[p]}" for p in PADS if p.startswith("INMP441.")) + " |",
         "| PCA9685, off the board on 90 mm wires | Wire ends | " + ", ".join(f"{p.split('.')[1]} {PADS[p]}" for p in PADS if p.startswith("PCA9685.")) + " |", "",
         "Every mic and PCA9685 wire lands in a strip that already carries its net, so none of them needs a link: the signals in the XIAO's own strips, "
         "the power wires on two rails that four component-side jumpers make out of rows 8 and 9.", "",
         "| Jumper | From | To | Colour | Job |", "|---|---|---|---|---|"] + [f"| {j[0]} | {j[1]} | {j[2]} | {j[3]} | {j[4]} |" for j in JUMPERS] + ["",
         "The amp's pins sit in rows 11–17, away from the XIAO's, so L1–L5 carry its three I2S lines, 5 V and ground through the centre channel, and L6 ties GAIN to Vin. "
         "Holes under the amp's body (roughly columns E–I, rows 10–17) can't take a wire from the top.", "",
         "| Wire | Hole | Lead | Colour |", "|---|---|---|---|"] + [f"| {a} | {h} | {t} | {c} |" for a, h, t, c in LEADS] + ["",
         "## Nets", "", "| Net | Holes |", "|---|---|"] + [f"| {n} | {h} |" for n, h in net_table()] + ["",
         "These are the nets in [`hardware/wires.csv`](../hardware/wires.csv) that the carrier holds. `python3 tools/carrier/carrier.py --check` computes the nets from the strips, pins and links and fails if they differ.", "",
         "## Links (underside)", "",
         f"{len(LINKS)} links of 30 AWG stranded wire, ends tinned, run on the underside, laid in the order below. {len(crossings())} pairs cross; that is fine with insulated wire, but keep every link flat.", "",
         f"The order matters: each link keeps at least {CLEAR_MM} mm (centre to centre) from every hole soldered after it, the module pins included, so the iron never meets a wire's insulation. "
         f"Links that don't cross keep {PARALLEL_MM} mm apart. `--check` enforces both. Link ends under the XIAO or the amp are trimmed flush on the component side.", "",
         "| Link | Colour | From | To | Job | Cut (estimated) |", "|---|---|---|---|---|---|"]
    for l in LINKS:
        o.append(f"| {l[0]} | {l[3]} | {l[1]} (printed {under(l[1])}) | {l[2]} (printed {under(l[2])}) | {l[5]} | {cut_mm(l)} mm |")
    o += ["", "Cut lengths are the routed length × 1.15 + 10 mm, rounded up to 5 mm. They are estimates: trim each link to fit.", "",
          "## Where it goes", "", "**Pending.** The mechanical design decides where the carrier mounts, where the mic and the PCA9685 sit on their wires, "
          "and the height above the flat amp and the XIAO. "
          "The row-1 edge, with the USB-C port, goes to an opening in the robot's outer wall. "
          "The XIAO ESP32-C3 has no antenna of its own: its external antenna plugs into the connector by its buttons and needs room. "
          "The S3 Sense's camera board stacks on top of the XIAO and needs clearance above it. "
          "[`carrier_cad.py`](../tools/carrier/carrier_cad.py) turns this layout into datums for the 3D model.", "",
          "## Still to confirm", "",
          "1. The holes the mic's and PCA9685's red, black and brown wires land in (the design chose " +
          ", ".join(f"{PADS[p]} for {p.replace('INMP441', 'mic').replace('.', ' ')}" for p in PADS if p in LEAD_WIRES and p not in AS_BUILT) + ").",
          "2. The amp's outline: its body is drawn about 17.8 × 18 mm over columns D–I.",
          "3. The colours of jumpers T1 and T2.",
          "4. That your perfboard's underside print is mirrored as described, and its corner holes sit as described.",
          "5. How far the XIAO's USB-C port overhangs its body, and the cut lengths."]
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
            b.append("<h3>Wires that land on the carrier</h3><table><tr><th>Wire</th><td><b>Hole · lead · colour</b></td></tr>" +
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
  <p>Solder the XIAO and amp onto one perfboard, one link at a time, then land the mic's and PCA9685's wires, with a picture and a check for each step.
  The full hole map is <a href="carrier/hole-map.svg">here</a>.</p>
</header>
<main>
  <div class="banner"><strong>Design, matched to a prototype in progress.</strong> The XIAO's and amp's positions and the mic's and
  PCA9685's signal wires follow a board being built; the underside links and the power wire holes are the design's. It has been
  checked by computer (the nets and the link routes), not on a bench. The mechanical fit (where the carrier mounts, where the mic
  sits) is not designed yet. Prefer jumper leads? Use the <a href="wiring.html">wire-by-wire guide</a> instead.</div>
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
        stale = [str(p.relative_to(ROOT)) for p, t in files.items() if not p.exists() or p.read_text(encoding="utf-8") != t]
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
        p.write_text(t, encoding="utf-8")
    print(f"wrote {len(files)} files; {len(nets())} nets match wires.csv; crossings: {crossings()}")


if __name__ == "__main__":
    main()
