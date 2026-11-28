"""Build the loom panel from the shared TU80 container controls."""

import argparse
from pathlib import Path
import struct

from import_common_update import read_archive, write_archive
from import_tu80_ui import Movie, cstr, putstr


NAMES = {
    'Ingredient': 'Inputs',
    'Fuel': 'Patterns',
    'Result': 'Output',
    'Furnace_text': 'Title',
    'Ingredient_Label': 'InputsLabel',
    'Fuel_Label': 'PatternsLabel',
}


def build_layout(data, target, split):
    movie = Movie(data)
    putstr(movie.header, 12, target.removesuffix('.fui') + '.swf')
    regions = movie.regions
    document = next(row for row in regions['symbol'] if cstr(row[:64]) == 'fourj.documents.FurnaceMenu')
    putstr(document, 0, 'fourj.documents.LoomMenu')
    panel = regions['timeline'][movie.exports()['Panel'][1]]
    panel_frame, frame_count = struct.unpack_from('<HH', panel, 4)
    if frame_count != 1:
        raise ValueError('Expected a single panel frame')
    frame = regions['frame'][panel_frame]
    start, count = struct.unpack_from('<II', frame, 64)

    positions = {
        'Ingredient': (15, 47) if split else (25, 67),
        'Ingredient_Label': (15, 28) if split else (25, 43),
        'Fuel': (15, 100) if split else (25, 129),
        'Fuel_Label': (15, 82) if split else (25, 108),
        'Result': (211, 44) if split else (336, 60),
    }
    pitch = 26 if split else 42
    scale = pitch / 50
    events = []
    found = set()
    for event in regions['event'][start:start + count]:
        name_id, = struct.unpack_from('<h', event, 10)
        name = cstr(regions['name'][name_id]) if name_id >= 0 else ''
        if name in ('FurnaceFire', 'FurnaceArrow'):
            continue
        if name in positions:
            struct.pack_into('<2f', event, 28, *positions[name])
        if name == 'Ingredient':
            struct.pack_into('<4f', event, 12, 3 * scale, 0, 0, scale)
        if name == 'Fuel':
            struct.pack_into('<4f', event, 12, 8 * scale, 0, 0, scale)
        if name in NAMES:
            found.add(name)
        events.append(event)
    if found != set(NAMES):
        raise ValueError(f'Missing controls: {set(NAMES) - found}')
    struct.pack_into('<II', frame, 64, len(regions['event']), len(events))
    regions['event'].extend(events)

    for name in regions['name']:
        old = cstr(name)
        if old in NAMES:
            putstr(name, 0, NAMES[old])
    for action in regions['action']:
        old = cstr(action[4:68])
        if old in NAMES:
            putstr(action, 4, NAMES[old])
        if old in ('Ingredient', 'Fuel') and action[0] == 16:
            putstr(action, 68, str(pitch))
        if old in ('Fuel_Label', 'Ingredient_Label') and action[0] == 5:
            putstr(action, 68, '1')
    return movie.encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    archive = read_archive(args.archive.read_bytes())
    for split in (False, True):
        for resolution in ('720', '480'):
            suffix = ('Split' if split else '') + resolution + '.fui'
            name = 'LoomMenu' + suffix
            base = 'FurnaceMenu' + suffix
            source = archive.get(base.encode())
            if source is None:
                source = archive[('FurnaceMenu' + suffix.replace('480', '720')).encode()]
            archive[name.encode()] = build_layout(source, name, split)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(write_archive(archive))


if __name__ == '__main__':
    main()
