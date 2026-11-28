"""Convert the composter and its fill models from a local Java client JAR."""

import argparse
import json
from pathlib import Path
from zipfile import ZipFile


FACES = ('down', 'up', 'north', 'south', 'west', 'east')
TEXTURES = ('composter_bottom', 'composter_side', 'composter_top', 'composter_compost', 'composter_ready')


def default_uv(side, low, high):
    x0, y0, z0 = low
    x1, y1, z1 = high
    return (
        (x0, 16 - z1, x1, 16 - z0),
        (x0, z0, x1, z1),
        (16 - x1, 16 - y1, 16 - x0, 16 - y0),
        (x0, 16 - y1, x1, 16 - y0),
        (z0, 16 - y1, z1, 16 - y0),
        (16 - z1, 16 - y1, 16 - z0, 16 - y0),
    )[side]


def encode_element(element, textures):
    if 'rotation' in element:
        raise ValueError('Rotated composter model parts are unsupported')
    low, high = element['from'], element['to']
    faces = element['faces']
    data = bytearray([*low, *high, len(faces)])
    for direction, face in faces.items():
        if face.get('rotation', 0):
            raise ValueError(f'Rotated texture on {direction}')
        side = FACES.index(direction)
        cull = FACES.index(face['cullface']) + 1 if 'cullface' in face else 0
        texture = TEXTURES.index(textures[face['texture'].removeprefix('#')].split('/')[-1])
        data.extend([side, texture, cull, *face.get('uv', default_uv(side, low, high))])
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jar', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with ZipFile(args.jar) as jar:
        def load(name):
            return json.loads(jar.read(f'assets/minecraft/models/block/{name}.json'))

        model = load('composter')
        if len(model['elements']) != 5:
            raise ValueError('Unexpected composter base model')
        parts = [encode_element(part, model['textures']) for part in model['elements']]
        for contents in [f'composter_contents{i}' for i in range(1, 8)] + ['composter_contents_ready']:
            model = load(contents)
            if len(model['elements']) != 1:
                raise ValueError(f'Unexpected fill model: {contents}')
            parts.append(encode_element(model['elements'][0], model['textures']))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b'CBM1' + bytes([len(parts)]) + b''.join(parts))


if __name__ == '__main__':
    main()
