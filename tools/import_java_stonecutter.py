"""Prepare the stonecutter from a local Java client and optional PS4 update."""

import argparse
import json
from io import BytesIO
from pathlib import Path
from zipfile import ZipFile

from PIL import Image

from import_java_grindstone import encode_model


ROLES = ('bottom', 'top', 'side', 'saw')
ATLAS_ROW = 39


def load_tile(data):
    with Image.open(BytesIO(data)) as source:
        if source.width == 0 or source.height % source.width != 0:
            raise ValueError('Expected a square texture or a vertical strip of square frames')
        return source.convert('RGBA').crop((0, 0, source.width, source.width))


def add_atlas_row(path, tiles):
    with Image.open(path) as source:
        cell = source.width // 16
        if not cell or source.height < 38 * cell:
            raise ValueError(f'Expected an extended terrain atlas: {path}')
        atlas = Image.new('RGBA', (source.width, max(source.height, (ATLAS_ROW + 1) * cell)))
        atlas.paste(source.convert('RGBA'))
    for column, tile in enumerate(tiles):
        atlas.paste(tile.resize((cell, cell), Image.Resampling.NEAREST), (column * cell, ATLAS_ROW * cell))
    return atlas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jar', type=Path, required=True)
    parser.add_argument('--terrain-dir', type=Path, required=True)
    parser.add_argument('--title-update', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()

    root = 'assets/minecraft'
    with ZipFile(args.jar) as jar:
        model = json.loads(jar.read(f'{root}/models/block/stonecutter.json'))
        encoded = encode_model(model, ROLES)
        tiles = [
            load_tile(jar.read(f'{root}/textures/{model["textures"][role].removeprefix("minecraft:")}.png'))
            for role in ROLES
        ]
    if args.title_update:
        saw = args.title_update / 'textures/blocks/stonecutter2_saw.png'
        if saw.is_file():
            tiles[ROLES.index('saw')] = load_tile(saw.read_bytes())

    atlases = {}
    for name in ('terrain.png', 'terrainMipMapLevel2.png', 'terrainMipMapLevel3.png'):
        source = args.terrain_dir / name
        if name == 'terrain.png' or source.is_file():
            atlases[name] = add_atlas_row(source, tiles)
    args.output.mkdir(parents=True, exist_ok=True)
    for name, atlas in atlases.items():
        atlas.save(args.output / name)
    (args.output / 'stonecutter_model.bin').write_bytes(encoded)


if __name__ == '__main__':
    main()
