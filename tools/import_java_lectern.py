"""Convert the lectern's geometry from a local Java client JAR."""

import argparse
import json
from pathlib import Path
from zipfile import ZipFile

from import_java_composter import FACES

TEXTURES = {'bottom': 0, 'base': 1, 'front': 2, 'sides': 3, 'top': 4}


def encode_model(model):
    elements = model['elements']
    encoded = bytearray(b'CBM4')
    encoded.append(len(elements))
    for element in elements:
        rotation = element.get('rotation', {})
        if rotation and (rotation['axis'] != 'x' or rotation['origin'] != [8, 8, 8]):
            raise ValueError('Unsupported element rotation')
        bounds = [round(coordinate * 2) for coordinate in element['from'] + element['to']]
        if any(not 0 <= coordinate <= 34 for coordinate in bounds):
            raise ValueError('Invalid element bounds')
        angle = round(rotation.get('angle', 0) * 2)
        faces = element['faces']
        encoded.extend([90, angle + 90, *bounds, len(faces)])
        for side, face in faces.items():
            texture = TEXTURES[face['texture'][1:]]
            cull = FACES.index(face['cullface']) + 1 if 'cullface' in face else 0
            encoded.extend([FACES.index(side), texture, cull, face.get('rotation', 0) // 90,
                            *map(round, face['uv'])])
    return bytes(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jar', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with ZipFile(args.jar) as jar:
        model = json.loads(jar.read('assets/minecraft/models/block/lectern.json'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(encode_model(model))


if __name__ == '__main__':
    main()
