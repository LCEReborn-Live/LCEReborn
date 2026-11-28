"""Convert campfire block models from a local Java 1.14 client JAR."""

import argparse
import json
import math
from pathlib import Path
from zipfile import ZipFile

from import_java_composter import FACES


TEXTURES = {'block/campfire_log': 0, 'block/campfire_log_lit': 1, 'block/campfire_fire': 2}


def encode_model(model):
    elements = model['elements']
    if not 1 <= len(elements) <= 16:
        raise ValueError('Unsupported element count')
    encoded = bytearray(b'CBM3')
    encoded.append(len(elements))
    for element in elements:
        rotation = element.get('rotation', {})
        if rotation and (rotation['axis'] != 'y' or rotation['origin'] != [8, 8, 8]):
            raise ValueError('Unsupported rotation')
        low, high = list(element['from']), list(element['to'])
        if rotation.get('rescale', False):
            factor = 1 / math.cos(math.radians(rotation['angle']))
            for axis in (0, 2):
                low[axis] = max(0, 8 + (low[axis] - 8) * factor)
                high[axis] = min(16, 8 + (high[axis] - 8) * factor)
        angle = rotation.get('angle', 0)
        bounds = [round(coord * 2) for coord in low + high]
        if int(angle) != angle or not -90 <= angle <= 90 or not all(0 <= n <= 34 for n in bounds):
            raise ValueError('Unsupported model bounds')
        faces = element['faces']
        encoded.extend([angle + 90, *bounds, len(faces)])
        for side, face in faces.items():
            texture = TEXTURES[model['textures'][face['texture'][1:]]]
            cull = FACES.index(face['cullface']) + 1 if 'cullface' in face else 0
            rotation = face.get('rotation', 0)
            if rotation not in (0, 90, 180, 270):
                raise ValueError('Unsupported UV rotation')
            encoded.extend([FACES.index(side), texture, cull, rotation // 90, *face['uv']])
    return bytes(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jar', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with ZipFile(args.jar) as jar:
        for name in ('campfire', 'campfire_off'):
            model = json.loads(jar.read(f'assets/minecraft/models/block/{name}.json'))
            (args.output / f'{name}_model.bin').write_bytes(encode_model(model))


if __name__ == '__main__':
    main()
