#!/usr/bin/env python3
"""Check that web-programmer/wiring.html and hardware/wires.csv list the same wires.

The wiring page draws every step from its WIRES table; hardware/wires.csv is the same
list for people who want a plain file. Run: python3 scripts/check_wiring.py
"""
import csv, json, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
html = (ROOT / "web-programmer" / "wiring.html").read_text()
js = html[html.index("const SHARED"):html.index("const WIRE_NAMES")]
prog = "const C={};" + js + "\nconsole.log(JSON.stringify(WIRES.map(w=>[w.id,w.step,w.from,w.to,w.color])))"
page = [tuple(w) for w in json.loads(subprocess.check_output(["node", "-e", prog]))]
names = {"X": "XIAO", "M": "INMP441", "A": "MAX98357A", "P": "PCA9685", "K": "Speaker", "S": "Servo supply"}
nm = lambda r: names[r[0]] + " " + r[2:].replace("GND2", "GND")
page = [(i, str(s), nm(f), nm(t), c) for i, s, f, t, c in page]
with open(ROOT / "hardware" / "wires.csv") as f:
    rows = [(r["id"], r["stage"], r["from"], r["to"], r["suggested_colour"]) for r in csv.DictReader(f)]
if page != rows:
    for a, b in zip(page, rows):
        if a != b:
            print("page:", a, "\ncsv: ", b)
    sys.exit("wiring.html and hardware/wires.csv differ")
print(f"{len(rows)} wires match")
