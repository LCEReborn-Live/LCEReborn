"""Port a TU80 inventory menu and its split-screen variant onto the PC's 720p UI."""
import argparse
from pathlib import Path
import struct

from import_common_update import read_archive, write_archive
from import_tu80_ui import Movie, cstr


def single_shape_dimensions(library, timeline):
    """The shared panel symbols are single-frame, single-shape wrappers."""
    first, count = struct.unpack_from('<HH', library.regions['timeline'][timeline], 4)
    start, events = struct.unpack_from('<II', library.regions['frame'][first], 64)
    event = library.regions['event'][start]
    if count != 1 or events != 1 or event[2] != 1:
        raise ValueError('Unexpected shared panel structure')
    index, = struct.unpack_from('<H', event, 6)
    left, right, top, bottom = struct.unpack_from('<4f', library.regions['shape'][index], 12)
    xx, xy, yx, yy = struct.unpack_from('<4f', event, 12)
    if xy or yx:
        raise ValueError('Unexpected rotated panel')
    return (right - left) * xx, (bottom - top) * yy


def embedded_bitmap(movie, donor, index):
    bitmap = bytearray(donor.regions['bitmap'][index])
    offset, size = struct.unpack_from('<II', bitmap, 16)
    struct.pack_into('<I', bitmap, 0, 0xffffffff)  # private to this scene
    struct.pack_into('<I', bitmap, 16, len(movie.images))
    movie.images += donor.images[offset:offset + size]
    movie.regions['bitmap'].append(bitmap)
    return len(movie.regions['bitmap']) - 1


def port_movie(movie, donor_graphics, pc_graphics):
    movie.normalize()
    # The PC libraries still implement interactive controls. Bring in the TU80
    # images locally, including the shield silhouette, without changing a skin.
    donor = donor_graphics.exports()
    pc = pc_graphics.exports()
    references = []
    placements = {}
    for index, reference in enumerate(movie.regions['reference']):
        name = cstr(reference[4:68])
        if name in donor and donor[name][0] == 3:
            bitmap = embedded_bitmap(movie, donor_graphics, donor[name][1])
            placements[index] = (3, bitmap, 1., 1.)
            continue
        sx = sy = 1.
        if name in donor:
            width, height = single_shape_dimensions(donor_graphics, donor[name][1])
            pc_width, pc_height = single_shape_dimensions(pc_graphics, pc[name][1])
            sx, sy = width / pc_width, height / pc_height
        elif name in ('fourj.listClasses.FJ_SlotList', 'fourj.FJ_TouchControl'):
            # Both libraries use the same authored guide sizes for slots and
            # touch targets. Convert TU80's placement scale to logical pixels.
            sx = sy = 2. / 3.
        placements[index] = (4, len(references), sx, sy)
        references.append(reference)
    movie.regions['reference'] = references
    kept = []
    for frame in movie.regions['frame']:
        start, count = struct.unpack_from('<II', frame, 64)
        first = len(kept)
        for original in movie.regions['event'][start:start + count]:
            event = bytearray(original)
            name_index, = struct.unpack_from('<h', event, 10)
            if name_index >= 0 and cstr(movie.regions['name'][name_index]) in ('Effect28', 'Effect29'):
                continue  # TU80 effect types not implemented by the PC game
            if event[2] == 4:
                index, = struct.unpack_from('<H', event, 6)
                kind, target, sx, sy = placements[index]
                event[2] = kind
                struct.pack_into('<H', event, 6, target)
                xx, xy, yx, yy = struct.unpack_from('<4f', event, 12)
                struct.pack_into('<4f', event, 12, xx*sx, xy*sx, yx*sy, yy*sy)
            kept.append(event)
        struct.pack_into('<II', frame, 64, first, len(kept) - first)
    movie.regions['event'] = kept
    return movie.encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--current', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--menu', choices=('player', 'creative'), default='player')
    args = parser.parse_args()
    source = read_archive(args.source.read_bytes())
    current = read_archive(args.current.read_bytes())
    donor_graphics = Movie(source[b'skinHDGraphicsInGame.fui']).normalize()
    pc_graphics = Movie(current[b'skinGraphicsInGame.fui'])
    base = 'CreativeMenu' if args.menu == 'creative' else 'InventoryMenu'
    for name in (base, base + 'Split'):
        movie = Movie(source[(name + '1080.fui').encode()])
        current[(name + '720.fui').encode()] = port_movie(movie, donor_graphics, pc_graphics)
    args.output.write_bytes(write_archive(current))


if __name__ == '__main__':
    main()
