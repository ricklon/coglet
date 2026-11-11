"""Flatten the pinned print layout into individually named reference meshes.

Preserves original vertices/triangles and composes the 3MF component and build
transforms. This is a print-layout reference, never an assembled robot model.
"""
import copy
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
CAD = ROOT / 'hardware/mechanical'
NS = 'http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
IDENTITY = '1 0 0 0 1 0 0 0 1 0 0 0'


def matrix(value):
    a = list(map(float, (value or IDENTITY).split()))
    if len(a) != 12:
        raise ValueError('Invalid 3MF transform')
    return [a[0:3] + [0], a[3:6] + [0], a[6:9] + [0], a[9:12] + [1]]


def compose(component, placement):
    # 3MF uses row vectors: local point * component * build placement.
    a, b = matrix(component), matrix(placement)
    c = [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
    return ' '.join(format(c[i][j], '.17g') for i in range(4) for j in range(3))


def main():
    inventory = json.loads((CAD / 'reference/inventory.json').read_text())
    source = CAD / 'original' / inventory['source']
    if hashlib.sha256(source.read_bytes()).hexdigest() != inventory['source_sha256']:
        raise ValueError('Source no longer matches inventory')
    ET.register_namespace('', NS)
    model = ET.Element(f'{{{NS}}}model', unit='millimeter')
    ET.SubElement(model, f'{{{NS}}}metadata', name='Title').text = 'Coglet C0_6_8 individual mesh references'
    ET.SubElement(model, f'{{{NS}}}metadata', name='Designer').text = 'Will Cogley / NMRobotics LTD'
    ET.SubElement(model, f'{{{NS}}}metadata', name='Description').text = 'CC BY-NC 4.0. Derived print layout: groups separated and named; geometry unchanged. Not an assembly.'
    resources = ET.SubElement(model, f'{{{NS}}}resources')
    build = ET.SubElement(model, f'{{{NS}}}build')
    cache = {}
    with ZipFile(source) as archive:
        original = ET.fromstring(archive.read('3D/3dmodel.model'))
        placements = {}
        for item in original.findall('{*}build/{*}item'):
            oid = item.get('objectid')
            if oid in placements:
                raise ValueError('Repeated build object needs instance handling')
            placements[oid] = item
        for index, part in enumerate(inventory['parts'], 1):
            path = part['model_path']
            if path not in cache:
                cache[path] = ET.fromstring(archive.read(path))
            original_object = next(o for o in cache[path].findall('{*}resources/{*}object')
                                   if o.get('id') == part['object_id'])
            mesh = original_object.find('{*}mesh')
            if len(mesh.findall('{*}triangles/{*}triangle')) != part['triangles']:
                raise ValueError('Inventory triangle mismatch')
            obj = ET.SubElement(resources, f'{{{NS}}}object', id=str(index), type='model',
                                name=f"{part['id']} {part['name']}")
            obj.append(copy.deepcopy(mesh))
            oid = part['id'].split('-O')[1].split('-P')[0]
            placement = placements[oid]
            ET.SubElement(build, f'{{{NS}}}item', objectid=str(index),
                          transform=compose(part['component_transform'], placement.get('transform')),
                          printable=placement.get('printable', '1'))
    output = CAD / 'reference/coglet-c0_6_8-individual.3mf'
    with ZipFile(output, 'w', compression=ZIP_DEFLATED) as archive:
        archive.writestr('[Content_Types].xml', '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
        archive.writestr('_rels/.rels', '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel" Target="/3D/3dmodel.model"/></Relationships>')
        archive.writestr('3D/3dmodel.model', ET.tostring(model, encoding='utf-8', xml_declaration=True))
    print(f'Wrote {output.name}: {len(inventory["parts"])} individually named meshes; print transforms composed.')


if __name__ == '__main__':
    main()
