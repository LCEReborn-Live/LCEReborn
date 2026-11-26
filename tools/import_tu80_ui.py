"""Compare and adapt the supplied 1080p FUI movies to the existing 720p UI.

Works on copies of the user's archives. Bitmap bytes are preserved verbatim;
geometry, text metrics and pixel-valued actions are converted instead. Legacy
library exports are retained because PC controls still use them dynamically.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct

from import_common_update import read_archive, write_archive

REGIONS = [('timeline', 0, 28), ('action', 2, 132), ('shape', 3, 28),
           ('component', 4, 44), ('vertex', 5, 8), ('frame', 6, 72),
           ('event', 7, 72), ('name', 1, 64), ('reference', 8, 72),
           ('text', 9, 312), ('fontname', 13, 260), ('symbol', 10, 72),
           ('import', 14, 64), ('bitmap', 11, 32)]
TYPES = {1: 'shape', 2: 'timeline', 3: 'bitmap', 4: 'reference', 5: 'text'}
RETAINED_CONTRACTS = {
    'AvatarSelectMenu720.fui': 'PC avatar tabs require TabLeftOff, TabMiddleOff and TabRightOff',
    'TradingMenu720.fui': 'Current trading code requires RequiredLabel, Request1Label and Request2Label',
    'TradingMenuSplit720.fui': 'Current trading code requires RequiredLabel, Request1Label and Request2Label',
    'FullscreenProgress720.fui': 'Loading controls still use the original progress and text component hierarchy',
    'FullscreenProgressSplit720.fui': 'Loading controls still use the original progress and text component hierarchy',
}


def cstr(data):
    return bytes(data).split(b'\0', 1)[0].decode('utf-8')


def putstr(data, offset, value):
    encoded = value.encode('utf-8')
    if len(encoded) >= 64:
        raise ValueError('FUI name too long: ' + value)
    data[offset:offset + 64] = encoded.ljust(64, b'\0')


def mapped(name):
    return name.replace('1080', '720').replace('skinHD', 'skin').replace('skinOrbis', 'skinWiiU')


class Movie:
    def __init__(self, data):
        self.header = bytearray(data[:152])
        sig, size, length = struct.unpack_from('<III', data)
        if sig not in (0x46554900, 0x46554901) or length + 152 != len(data):
            raise ValueError('invalid FUI header')
        counts = struct.unpack_from('<15I', data, 76)
        self.regions = {}
        pos = 152
        for name, index, stride in REGIONS:
            end = pos + counts[index] * stride
            if end > len(data):
                raise ValueError('invalid FUI region: ' + name)
            self.regions[name] = [bytearray(data[p:p + stride]) for p in range(pos, end, stride)]
            pos = end
        self.images = bytes(data[pos:])
        if sig == 0x46554900:
            for event in self.regions['event']:
                event[3] = 0  # version 0's uint16 type becomes type + blend mode.
        struct.pack_into('<I', self.header, 0, 0x46554901)

    def encode(self):
        payload = b''.join(b''.join(self.regions[name]) for name, _, _ in REGIONS) + self.images
        header = bytearray(self.header)
        struct.pack_into('<II', header, 4, len(payload) + 152, len(payload))
        for name, index, _ in REGIONS:
            struct.pack_into('<I', header, 76 + index * 4, len(self.regions[name]))
        return bytes(header) + payload

    def exports(self):
        return {cstr(s[:64]): struct.unpack_from('<II', s, 64) for s in self.regions['symbol']}

    def normalize(self):
        factor = 2.0 / 3.0

        def scale(data, offset, count):
            values = struct.unpack_from('<' + 'f' * count, data, offset)
            struct.pack_into('<' + 'f' * count, data, offset, *(v * factor for v in values))

        putstr(self.header, 12, mapped(cstr(self.header[12:76])))
        scale(self.header, 136, 4)
        for data in self.regions['import']:
            putstr(data, 0, mapped(cstr(data)))
        for data in self.regions['timeline']:
            scale(data, 12, 4)
        for data in self.regions['shape']:
            scale(data, 12, 4)
        for data in self.regions['vertex']:
            scale(data, 0, 2)
        for data in self.regions['component']:
            scale(data, 12, 6)
        for data in self.regions['text']:
            scale(data, 4, 4)
            scale(data, 24, 1)
            scale(data, 36, 4)
        for data in self.regions['event']:
            scale(data, 28, 2)  # translations, including references to converted libraries
        for data in self.regions['bitmap']:
            bitmap_type = struct.unpack_from('<I', data, 4)[0]
            # Runtime renders the original image in 720p logical coordinates.
            struct.pack_into('<I', data, 4, bitmap_type | 0x80000000)
        for data in self.regions['action']:
            if data[0] in (11, 16, 21, 23, 29):
                value = cstr(data[68:132])
                try:
                    scaled = str(round(float(value) * factor))
                except ValueError:
                    raise ValueError('non-numeric pixel action: ' + value)
                putstr(data, 68, scaled)
        return self

    def append_legacy(self, legacy):
        """Retain missing exported symbols and their complete dependency graph."""
        old = copy.deepcopy(legacy)
        offsets = {name: len(values) for name, values in self.regions.items()}
        existing = self.exports()
        # Font IDs are local to each input movie, not indices in the font table.
        # In particular, TU80 swaps IDs 1 and 4 in skinGraphicsLabels. Appending
        # the old table unchanged makes runtime text select the wrong font.
        font_ids = {struct.unpack_from('<i', data)[0] for data in self.regions['fontname']}
        legacy_font_ids = {struct.unpack_from('<i', data)[0] for data in old.regions['fontname']}
        next_font_id = max(font_ids | legacy_font_ids | {0}) + 1
        font_remap = {}
        for font_id in sorted(font_ids & legacy_font_ids):
            font_remap[font_id] = next_font_id
            next_font_id += 1
        for data in old.regions['fontname']:
            font_id = struct.unpack_from('<i', data)[0]
            struct.pack_into('<i', data, 0, font_remap.get(font_id, font_id))
        for data in old.regions['text']:
            font_id = struct.unpack_from('<i', data, 20)[0]
            struct.pack_into('<i', data, 20, font_remap.get(font_id, font_id))
        # Keep class names for dispatch and place new exports before legacy ones.
        def add(data, pos, kind, fmt='I', sentinel=None):
            value = struct.unpack_from('<' + fmt, data, pos)[0]
            if sentinel is None or value != sentinel:
                struct.pack_into('<' + fmt, data, pos, value + offsets[kind])
        for data in old.regions['timeline']:
            add(data, 0, 'symbol', sentinel=0xffffffff)
            add(data, 4, 'frame', 'H')
            add(data, 8, 'action', 'H')
        for data in old.regions['shape']:
            add(data, 0, 'symbol', sentinel=0xffffffff)
            add(data, 4, 'component')
        for data in old.regions['component']:
            if struct.unpack_from('<I', data)[0] != 1:
                add(data, 8, 'bitmap')
            add(data, 36, 'vertex')
        for data in old.regions['frame']:
            add(data, 64, 'event')
        for data in old.regions['event']:
            if data[2] in TYPES:
                add(data, 6, TYPES[data[2]], 'H', 0xffff)
            add(data, 10, 'name', 'h', -1)
        for kind in ('reference', 'text', 'bitmap'):
            for data in old.regions[kind]:
                add(data, 0, 'symbol', sentinel=0xffffffff)
        for data in old.regions['symbol']:
            kind, index = struct.unpack_from('<II', data, 64)
            if kind in TYPES:
                struct.pack_into('<I', data, 68, index + offsets[TYPES[kind]])
        for data in old.regions['bitmap']:
            offset = struct.unpack_from('<I', data, 16)[0]
            struct.pack_into('<I', data, 16, offset + len(self.images))
        for name in self.regions:
            self.regions[name].extend(old.regions[name])
        self.images += old.images
        return sorted(set(legacy.exports()) - set(existing))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--current', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    baseline = read_archive(args.baseline.read_bytes())
    source = read_archive(args.source.read_bytes())
    current = read_archive(args.current.read_bytes())
    result = dict(current)
    report = []
    for name, data in source.items():
        if not name.endswith(b'.fui'):
            continue
        name = name.decode()
        target = mapped(name)
        record = dict(source=name, target=target)
        if target.encode() not in baseline:
            record.update(status='new-unconnected', reason='No existing screen implementation')
        elif target == 'skinWiiU.fui':
            record.update(status='platform-retained', reason='PC button prompts and controller layout')
        elif target in RETAINED_CONTRACTS:
            record.update(status='incompatible-retained', reason=RETAINED_CONTRACTS[target])
        else:
            old = Movie(baseline[target.encode()])
            new = Movie(data).normalize()
            if target.startswith('skin'):
                record['retained_exports'] = new.append_legacy(old)
            payload = new.encode()
            Movie(payload)  # Validate region boundaries after relocation.
            record.update(status='changed' if payload != old.encode() else 'same',
                          sha256=hashlib.sha256(payload).hexdigest())
            if record['status'] == 'changed':
                result[target.encode()] = payload
        report.append(record)
    # Reject screens with dependencies absent from the actual runtime libraries.
    exports = set()
    for name, data in result.items():
        if name.startswith(b'skin') and b'HD' not in name and name.endswith(b'.fui'):
            exports.update(Movie(data).exports())
    for record in report:
        if record['status'] != 'changed' or record['target'].startswith('skin'):
            continue
        key = record['target'].encode()
        refs = {cstr(r[4:68]) for r in Movie(result[key]).regions['reference']}
        missing = sorted(refs - exports)
        if missing:
            result[key] = current[key]
            record.update(status='incompatible-retained', missing_exports=missing)
    args.output.mkdir(parents=True, exist_ok=True)
    movie_dir = args.output / 'movies'
    movie_dir.mkdir(exist_ok=True)
    for record in report:
        key = record['target'].encode()
        if key in result:
            (movie_dir / record['target']).write_bytes(result[key])
    active_scenes = []
    for key, data in result.items():
        if key.endswith((b'720.fui', b'480.fui')):
            (movie_dir / key.decode()).write_bytes(data)
            active_scenes.append(key.decode())
    payload = write_archive(result)
    assert read_archive(payload) == result
    (args.output / 'MediaWiiU.arc').write_bytes(payload)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    (args.output / 'scenes.txt').write_text('\n'.join(r['target'] for r in report if r['status'] == 'changed' and not r['target'].startswith('skin')), encoding='ascii')
    (args.output / 'verify-scenes.txt').write_text('\n'.join(sorted(active_scenes)), encoding='ascii')
    old_exports = set()
    for key, data in baseline.items():
        if key.startswith(b'skin') and key.endswith(b'.fui'):
            old_exports.update(Movie(data).exports())
    known_missing = []
    for name in active_scenes:
        if name.encode() in baseline:
            refs = {cstr(r[4:68]) for r in Movie(baseline[name.encode()]).regions['reference']}
            known_missing.extend(name + ':' + ref for ref in sorted(refs - old_exports))
    (args.output / 'baseline-missing-exports.txt').write_text('\n'.join(known_missing), encoding='ascii')
    from collections import Counter
    print(dict(Counter(r['status'] for r in report)))


if __name__ == '__main__':
    main()
