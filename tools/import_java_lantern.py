"""Convert the two lantern models from a local Java client JAR."""

import argparse
import json
from pathlib import Path
from zipfile import ZipFile

from import_java_composter import FACES


def encode_model(model):
    elements = model['elements']
    if not 1 <= len(elements) <= 16:
        raise ValueError('Unsupported element count')
    encoded = bytearray(b'CBM2')
    encoded.append(len(elements))
    for element in elements:
        rotation = element.get('rotation', {})
        if rotation and (rotation['axis'] != 'y' or rotation['origin'] != [8, 8, 8]
                         or rotation.get('rescale', False)):
            raise ValueError('Unsupported rotation')
        angle = rotation.get('angle', 0)
        bounds = [coordinate * 2 for coordinate in element['from'] + element['to']]
        if not -90 <= angle <= 90 or int(angle) != angle or any(int(n) != n or not 0 <= n <= 32 for n in bounds):
            raise ValueError('Model exceeds the binary format limits')
        faces = element['faces']
        encoded.extend([int(angle) + 90, *map(int, bounds), len(faces)])
        for side, face in faces.items():
            if face.get('rotation', 0) or model['textures'][face['texture'][1:]] != 'block/lantern':
                raise ValueError('Unsupported texture mapping')
            cull = FACES.index(face['cullface']) + 1 if 'cullface' in face else 0
            encoded.extend([FACES.index(side), 0, cull, *face['uv']])
    return bytes(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jar', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with ZipFile(args.jar) as jar:
        models = {
            name: encode_model(json.loads(jar.read(f'assets/minecraft/models/block/{name}.json')))
            for name in ('lantern', 'hanging_lantern')
        }
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in models.items():
        (args.output / f'{name}_model.bin').write_bytes(data)


if __name__ == '__main__':
    main()
