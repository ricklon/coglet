"""Render parts on a selected print plate from the separated reference design."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CAD = ROOT / 'hardware/mechanical'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plate', type=int, choices=range(1, 6), default=1)
    args = parser.parse_args()
    cli = os.environ.get('SOLIDCRAFT_CLI') or shutil.which('solidcraft-cli')
    if not cli:
        cli = str(Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'SolidCraft/solidcraft-cli.exe')
    inventory = json.loads((CAD / 'reference/inventory.json').read_text())
    inspected = json.loads((CAD / 'reference/individual-inspection.json').read_text())
    names = [b['name'] for b in inspected['bodies']]
    out = CAD / 'reference/parts'
    out.mkdir(parents=True, exist_ok=True)
    lines = [f'# Plate {args.plate}: isolated part views', '',
             'Derived views of Will Cogley / NMRobotics LTD geometry, CC BY-NC 4.0.',
             'See [upstream provenance](../../UPSTREAM.md). IDs refer to the pinned C0_6_8 pack.',
             'Views are independently fitted, so image sizes cannot be compared as a scale.',
             'Upstream names are preserved. Functional identifications require confirmation.', '']
    for part in inventory['parts']:
        if part['plate'] != args.plate:
            continue
        name = f"{part['id']} {part['name']}"
        if name not in names:
            raise ValueError(f'Missing imported part: {name}')
        script = out / f"{part['id']}.json"
        script.write_text(json.dumps({'commands': [
            {'command': 'solid.remove', 'params': {'bodies': [n for n in names if n != name]}}
        ]}, indent=2) + '\n', encoding='utf-8')
        image = out / f"{part['id']}.png"
        subprocess.run([cli, 'snapshot', str(script), '--in',
                        str(CAD / 'solidcraft/coglet-c0_6_8-individual.sc'),
                        '--out', str(image), '--width', '800', '--height', '650',
                        '--view', 'iso', '--no-grid', '--no-axes'], check=True)
        lines += [f"## {part['id']} — {part['name']}", '',
                  'Mesh-local bounding box: ' + ' × '.join(str(v) for v in part['local_size_mm']) + ' mm.', '',
                  f"![{part['id']}]({image.name})", '']
        print(f"Rendered {part['id']}", flush=True)
    filename = 'STRUCTURAL-PARTS.md' if args.plate == 1 else f'PLATE-{args.plate}-PARTS.md'
    (out / filename).write_text('\n'.join(lines), encoding='utf-8')


if __name__ == '__main__':
    main()
