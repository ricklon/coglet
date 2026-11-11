"""Generate a SolidCraft diameter-screening coupon, not a spline coupler."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'hardware/mechanical/solidcraft'


def main():
    commands = []
    def add(command, **params):
        commands.append({'command': command, 'params': params})
    add('parameters.add', name='plate_thickness', expression='2 mm', unit='mm')
    add('solid.box', length=100, width=72, height='plate_thickness', body_name='DiameterCoupon', name='Coupon blank')
    holes = []
    for i in range(31):
        diameter = round(3 + i / 10, 1)
        x, y = 8 + 12 * (i % 8), 12 + 16 * (i // 8)
        parameter = f'diameter_{i:02}'
        add('parameters.add', name=parameter, expression=f'{diameter:.1f} mm', unit='mm',
            comment='Exploratory printed bore diameter; not a claimed servo dimension')
        add('solid.cylinder', diameter=parameter, height='plate_thickness + 2 mm', base=[x, y, -1],
            operation='cut', targets=['DiameterCoupon'], name=f'Bore {diameter:.1f} mm')
        holes.append({'diameter_mm': diameter, 'center_mm': [x, y], 'parameter': parameter})
    add('sketch.create', plane='XY', offset='plate_thickness - 0.4 mm', name='Engraved diameter labels')
    for hole in holes:
        x, y = hole['center_mm']
        add('sketch.text', text_expression=f"number({hole['parameter']}, 1)",
            at=[x, y-7], height=2.5, align='center')
    add('sketch.text', text='SHAFT DIA mm - V1', at=[5, 66], height=2.8)
    add('sketch.finish')
    add('solid.extrude', distance=0.5, operation='cut', targets=['DiameterCoupon'], name='Engrave labels')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'servo-diameter-coupon.json').write_text(json.dumps({'commands': commands}, indent=2)+'\n', encoding='utf-8')
    (OUT / 'servo-diameter-coupon-layout.json').write_text(json.dumps({
        'purpose': 'Unpowered diameter screening; smooth bores, no spline geometry',
        'plate_mm': [100, 72, 2], 'label_depth_mm': 0.4,
        'range_is_design_choice_not_servo_specification': True, 'holes': holes
    }, indent=2)+'\n', encoding='utf-8')
    print('Generated 31 diameter trials, 3.0–6.0 mm in 0.1 mm increments.')


if __name__ == '__main__':
    main()
