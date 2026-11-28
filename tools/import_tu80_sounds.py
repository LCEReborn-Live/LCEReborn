"""Build the PC sound add-on from selected events in a local PS4 TU80 bank."""

import argparse
import re
import struct
from pathlib import Path


WORKSTATIONS = (
    'block/barrel/', 'block/bell/', 'block/blastfurnace/', 'block/campfire/', 'block/composter/', 'block/grindstone/', 'block/lantern/',
    'block/scaffolding/', 'block/smoker/', 'item/crossbow/', 'mob/panda/', 'mob/cat/', 'mob/ocelot/',
    'UI/cartography_table/', 'UI/stonecutter/', 'mob/wanderingtrader/', 'mob/pillager/', 'mob/ravager/', 'raid/',
)
EVENT_ROOT = 'Minecraft/'


def ps4_bank(data):
    if not data.startswith(b'KNAB\x08\x00\x00\x00'):
        raise ValueError('Expected a little-endian PS4 Miles v8 sound bank')

    def word(offset):
        return struct.unpack_from('<I', data, offset)[0]

    def string(offset):
        return data[offset:data.index(b'\0', offset)].decode('ascii')

    def table(offset, count):
        start = word(offset)
        return {
            string(word(entry)): word(entry + 4)
            for entry in range(start, start + word(count) * 8, 8)
        }

    events = {name: string(body) for name, body in table(0x18, 0x40).items()}
    sounds = table(0x30, 0x4c)

    def read_sound(name):
        record = sounds[name]
        fields = list(struct.unpack_from('<15I', data, record))
        descriptor = string(record + fields[1])
        match = re.fullmatch(r'\*(\d+)\*(\d+)\.binka', descriptor)
        if fields[14] or not match:
            raise ValueError(f'Unsupported sound record: {name}')
        size, offset = map(int, match.groups())
        payload = data[offset:offset + size]
        if size != fields[6] or len(payload) != size or not payload.startswith(b'1FCB'):
            raise ValueError(f'Invalid Bink sample: {name}')
        return fields, payload

    return events, read_sound


def make_addon(events, read_sound):
    samples = {}
    for name, script in events.items():
        fields = script.split(';')
        if fields[:3] != ['9', '4', '1'] or len(fields) != 27 or fields[-1]:
            raise ValueError(f'Expected a single Miles play step: {name}')
        choices = fields[3].strip(':').split(':')
        if len(choices) % 2 or not all(weight.isdecimal() for weight in choices[1::2]):
            raise ValueError(f'Invalid sample choices: {name}')
        for key in choices[::2]:
            if key not in samples:
                samples[key] = read_sound(key)

    event_table = 56
    sample_table = event_table + len(events) * 8
    output = bytearray(sample_table + len(samples) * 8)

    def append(data):
        offset = len(output)
        output.extend(data)
        return offset

    def string(value):
        return append(value.encode('ascii') + b'\0')

    for index, (name, event) in enumerate(events.items()):
        struct.pack_into('>II', output, event_table + index * 8, string(name), string(event))

    records = []
    for index, (name, (fields, payload)) in enumerate(samples.items()):
        name_offset = string(name)
        append(bytes(-len(output) % 4))
        record = append(bytes(60))
        descriptor = append(bytes(32))
        fields[0] = name_offset
        fields[1] = descriptor - record
        records.append((record, descriptor, fields, payload))
        struct.pack_into('>II', output, sample_table + index * 8, name_offset, record)

    append(bytes(-len(output) % 4))
    data_start = len(output)
    for record, descriptor, fields, payload in records:
        offset = append(payload)
        fields[2] = offset
        extension = 'wav' if payload.startswith(b'RIFF') else 'binka'
        text = f'*{len(payload)}*{offset}.{extension}'.encode('ascii')
        output[descriptor:descriptor + len(text)] = text
        struct.pack_into('>15I', output, record, *fields)

    struct.pack_into('>14I', output, 0, 0x42414e4b, 8, data_start, 0, 0, event_table,
                     0, 0, sample_table, 0, len(events), 0, 0, len(samples))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='PS4 Sound/Minecraft.msscmp')
    parser.add_argument('--output', type=Path, help='PC TU80.msscmp output')
    parser.add_argument('--prefix', action='append', default=[], help='Extra event prefix, e.g. block/composter/')
    parser.add_argument('--list', action='store_true', help='List matching events without writing a bank')
    args = parser.parse_args()
    if not args.list and not args.output:
        parser.error('--output is required unless --list is used')

    events, read_sound = ps4_bank(args.source.read_bytes())
    prefixes = tuple(EVENT_ROOT + prefix for prefix in (*WORKSTATIONS, *args.prefix))
    selected = {name: body for name, body in events.items() if name.startswith(prefixes)}
    for prefix in prefixes:
        if not any(name.startswith(prefix) for name in selected):
            parser.error(f'No sound events found for {prefix}')

    if not args.list:
        bank = make_addon(selected, read_sound)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(bank)
    for name in selected:
        print(name)


if __name__ == '__main__':
    main()
