"""Import the Java grindstone model and textures from a local client JAR."""

import argparse
import json
from io import BytesIO
from pathlib import Path
from zipfile import ZipFile

from PIL import Image


FACES = ('down', 'up', 'north', 'south', 'west', 'east')
TEXTURES = ('pivot', 'side', 'round', 'leg')
ATLAS_ROW = 38
ATLAS_COLUMN = 12


def encode_model(model, texture_roles=TEXTURES):
    elements = model['elements']
    if not 1 <= len(elements) <= 16:
        raise ValueError('Unsupported grindstone model part count')
    encoded = bytearray(b'GSJ1')
    encoded.append(len(elements))
    for element in elements:
        if 'rotation' in element:
            raise ValueError('Rotated grindstone model parts are unsupported')
        bounds = element['from'] + element['to']
        faces = element['faces']
        values = bounds + [len(faces)]
        for face, entry in faces.items():
            if entry.get('rotation', 0) or entry.get('cullface', face) != face:
                raise ValueError(f'Unsupported face configuration on {face}')
            i = FACES.index(face)
            if 'cullface' in entry:
                i |= 0x80
            values += [i, texture_roles.index(entry['texture'].removeprefix('#')), *entry['uv']]
        encoded.extend(values)
    return encoded


def install_atlas(source, target, textures):
    atlas = Image.open(source).convert('RGBA')
    cell = atlas.width // 16
    if cell == 0 or atlas.height < (ATLAS_ROW + 1) * cell:
        raise ValueError(f'Terrain atlas lacks the TU80 rows: {source}')
    for i, image in enumerate(textures):
        tile = image.resize((cell, cell), Image.Resampling.NEAREST)
        atlas.paste(tile, ((ATLAS_COLUMN + i) * cell, ATLAS_ROW * cell))
    atlas.save(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jar', type=Path, required=True)
    parser.add_argument('--terrain-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with ZipFile(args.jar) as jar:
        root = 'assets/minecraft/'
        model = json.loads(jar.read(root + 'models/block/grindstone.json'))
        textures = []
        for role in TEXTURES:
            name = model['textures'][role].removeprefix('minecraft:')
            textures.append(Image.open(BytesIO(jar.read(root + f'textures/{name}.png'))).convert('RGBA'))

    encoded = encode_model(model)
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('terrain.png', 'terrainMipMapLevel2.png', 'terrainMipMapLevel3.png'):
        source = args.terrain_dir / name
        if name == 'terrain.png' or source.is_file():
            install_atlas(source, args.output / name, textures)
    (args.output / 'grindstone_model.bin').write_bytes(encoded)


if __name__ == '__main__':
    main()
