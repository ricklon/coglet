"""Generate SolidCraft carrier datums from the electrical layout's single source.

A board envelope and colored pad markers, not a fabricated PCB or module model.
"""
import json
from pathlib import Path
import carrier

ROOT = Path(__file__).resolve().parents[2]
CAD = ROOT / 'hardware/mechanical'
COLORS = {'XIAO': '#3478cc', 'MAX98357A': '#ec9d36', 'INMP441': '#ad60ce', 'PCA9685': '#d64f4f'}


def main():
    commands, pads = [], []
    def add(command, **params):
        commands.append({'command': command, 'params': params})
    for name, value in [('board_width', carrier.BOARD_W), ('board_length', carrier.BOARD_H),
                        ('board_thickness', carrier.BOARD_T)]:
        add('parameters.add', name=name, expression=f'{value} mm', unit='mm',
            comment='Nominal dimension from tools/carrier/carrier.py; verify actual board')
    add('solid.box', length='board_width', width='board_length', height='board_thickness',
        corner=['-board_width / 2', '-board_length / 2', 0], body_name='CarrierBoardEnvelope',
        name='Nominal board envelope')
    add('appearance.assign', bodies=['CarrierBoardEnvelope'], color='#3f8b65')
    groups = {}
    for pad, hole in carrier.PADS.items():
        group = pad.split('.')[0]
        x, source_y = carrier.hole_mm(hole)
        y = -source_y  # row 1 at +Y; USB side; conventional top view
        name = f'{hole} {pad} DATUM'
        add('solid.cylinder', diameter=1, height=0.2, base=[x, y, 'board_thickness'],
            operation='new', body_name=name, name=f'Pad location {hole}')
        groups.setdefault(group, []).append(name)
        pads.append({'pad': pad, 'hole': hole, 'x_mm': x, 'y_mm': y,
                     'z_mm': carrier.BOARD_T, 'marker_only': True})
    for group, names in groups.items():
        add('appearance.assign', bodies=names, color=COLORS[group])
    # No holes, mounting bosses or unmeasured module envelopes are invented.
    out = CAD / 'solidcraft'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'carrier-datums.json').write_text(json.dumps({'commands': commands}, indent=2)+'\n', encoding='utf-8')
    (CAD / 'reference/carrier-datums.json').write_text(json.dumps({
        'source': 'tools/carrier/carrier.py', 'units': 'mm',
        'origin': 'Board underside center; +Z toward components; +Y toward row 1 / USB',
        'board_mm': [carrier.BOARD_W, carrier.BOARD_H, carrier.BOARD_T],
        'markers': '1 mm diameter by 0.2 mm tall visualization only; not drilled holes or header dimensions',
        'pads': pads, 'colors': COLORS,
        'missing': ['module dimensions and heights', 'underside solder/link height',
                    'USB plug/cable envelope', 'camera stack', 'microphone port direction',
                    'mounting geometry', 'assembled placement']
    }, indent=2)+'\n', encoding='utf-8')
    print(f'Generated carrier envelope and {len(pads)} pad datums from electrical source.')


if __name__ == '__main__':
    main()
