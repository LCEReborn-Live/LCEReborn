"""Add a stonecutter layout using the prepared TU80 UI's shared controls."""

import argparse
from pathlib import Path
import struct

from import_common_update import read_archive, write_archive
from import_tu80_ui import Movie, cstr, putstr


NAMES = {
    'Ingredient': 'Material',
    'Fuel': 'Recipes',
    'Result': 'Output',
    'Furnace_text': 'Title',
    'Ingredient_Label': 'MaterialLabel',
    'Fuel_Label': 'RecipesLabel',
}


def build_layout(data, target, split):
    movie = Movie(data)
    putstr(movie.header, 12, target.removesuffix('.fui') + '.swf')
    regions = movie.regions
    symbol = next(row for row in regions['symbol'] if cstr(row[:64]) == 'fourj.documents.FurnaceMenu')
    putstr(symbol, 0, 'fourj.documents.StonecutterMenu')

    # The MainPanel contains the workstation controls and the shared inventory.
    panel_index = movie.exports()['Panel'][1]
    panel_frame, frame_count = struct.unpack_from('<HH', regions['timeline'][panel_index], 4)
    if frame_count != 1:
        raise ValueError('Expected a single workstation panel frame')
    frame = regions['frame'][panel_frame]
    start, count = struct.unpack_from('<II', frame, 64)
    positions = {
        'Ingredient': (15, 36) if split else (25, 56),
        'Ingredient_Label': (55, 42) if split else (78, 65),
        'Fuel': (84, 94) if split else (135, 122),
        'Fuel_Label': (15, 96) if split else (25, 131),
        'Result': (207, 31) if split else (339, 44),
    }
    panel_events = []
    found = set()
    for event in regions['event'][start:start + count]:
        name_id, = struct.unpack_from('<h', event, 10)
        name = cstr(regions['name'][name_id]) if name_id >= 0 else ''
        if name in ('FurnaceFire', 'FurnaceArrow'):
            continue
        if name in positions:
            x, y = positions[name]
            struct.pack_into('<2f', event, 28, x, y)
            if name == 'Fuel':
                scale = (26 if split else 42) / 50
                struct.pack_into('<4f', event, 12, 5 * scale, 0, 0, scale)
        if name in NAMES:
            found.add(name)
        panel_events.append(event)
    if found != set(NAMES):
        raise ValueError(f'Missing workstation controls: {set(NAMES) - found}')
    struct.pack_into('<II', frame, 64, len(regions['event']), len(panel_events))
    regions['event'].extend(panel_events)

    for name in regions['name']:
        old = cstr(name)
        if old in NAMES:
            putstr(name, 0, NAMES[old])
    for action in regions['action']:
        old = cstr(action[4:68])
        if old in NAMES:
            putstr(action, 4, NAMES[old])
        if old == 'Fuel' and action[0] == 16:
            putstr(action, 68, str(26 if split else 42))
        if old in ('Fuel_Label', 'Ingredient_Label') and action[0] == 5:
            putstr(action, 68, '1')  # Left-align labels beside their controls.
    return movie.encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    archive = read_archive(args.archive.read_bytes())
    for split in (False, True):
        for resolution in ('720', '480'):
            suffix = ('Split' if split else '') + resolution + '.fui'
            source = ('FurnaceMenu' + suffix).encode()
            if source not in archive:
                continue
            name = 'StonecutterMenu' + suffix
            archive[name.encode()] = build_layout(archive[source], name, split)
    if b'StonecutterMenu720.fui' not in archive or b'StonecutterMenuSplit720.fui' not in archive:
        raise ValueError('Expected the prepared TU80 full-screen and split-screen controls')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(write_archive(archive))


if __name__ == '__main__':
    main()
