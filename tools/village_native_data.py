"""Build-time NBT reader for the native village converter, never used by the game."""
import gzip
import io
import struct


def read(path):
    data = path.read_bytes()
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    stream = io.BytesIO(data)

    def number(fmt):
        return struct.unpack('>' + fmt, stream.read(struct.calcsize('>' + fmt)))[0]

    def string():
        return stream.read(number('H')).decode('utf-8')

    def tag(kind):
        if kind in range(1, 7):
            value = number({1: 'b', 2: 'h', 3: 'i', 4: 'q', 5: 'f', 6: 'd'}[kind])
        elif kind == 8:
            value = string()
        elif kind == 9:
            child, count = number('B'), number('i')
            value = [tag(child) for _ in range(count)]
        elif kind == 10:
            value = {}
            while (child := number('B')):
                name = string()
                value[name] = tag(child)
        elif kind in (7, 11, 12):
            value = [number({7: 'b', 11: 'i', 12: 'q'}[kind]) for _ in range(number('i'))]
        else:
            raise ValueError(f'Unknown NBT type {kind}')
        return kind, value

    kind = number('B')
    string()
    result = tag(kind)
    if stream.read():
        raise ValueError('Trailing NBT bytes')
    return result


def plain(tag):
    kind, value = tag
    if kind == 10:
        return {k: plain(v) for k, v in value.items()}
    if kind == 9:
        return [plain(v) for v in value]
    return value
