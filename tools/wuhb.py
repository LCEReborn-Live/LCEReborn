"""Writes Wii U homebrew bundles (.wuhb), the format Aroma and Cemu run, as devkitPro's wuhbtool lays them out.

A bundle is a big-endian RomFS behind a "WUHB" header: code/root.rpx, meta/meta.ini with the names the menu shows,
the menu icon and boot screens as gzipped TGA images in meta/, and the title's content/ folder. Cemu gives the
bundle the title ID 0005000F followed by the CRC-32 of its author, long name and short name.
"""
import struct
import zlib

HEADER_SIZE = 0x50
FILE_PARTITION_OFFSET = 0x200
EMPTY = 0xFFFFFFFF


class WuhbError(Exception):
    """The bundle cannot be laid out."""


class File:
    def __init__(self, name, size, read):
        self.name = name
        self.size = size
        self.read = read
        self.entry_offset = 0
        self.data_offset = 0
        self.sibling = None
        self.parent = None


class Dir:
    def __init__(self, name):
        self.name = name
        self.children = []
        self.entry_offset = 0
        self.sibling = None
        self.parent = None
        self.first_dir = None
        self.first_file = None

    def add(self, node):
        node.parent = self
        self.children.append(node)
        return node

    def folder(self, name):
        for child in self.children:
            if isinstance(child, Dir) and child.name == name:
                return child
        return self.add(Dir(name))

    def add_path(self, path, size, read):
        parts = path.split("/")
        folder = self
        for part in parts[:-1]:
            folder = folder.folder(part)
        folder.add(File(parts[-1], size, read))

    def dirs(self):
        return [child for child in self.children if isinstance(child, Dir)]

    def files(self):
        return [child for child in self.children if isinstance(child, File)]


def file_bytes(name, data):
    return File(name, len(data), lambda: data)


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def table_count(entries):
    if entries < 3:
        return 3
    if entries < 19:
        return entries | 1
    count = entries
    while any(count % prime == 0 for prime in (2, 3, 5, 7, 11, 13, 17)):
        count += 1
    return count


def path_hash(parent, name):
    value = (parent ^ 123456789) & 0xFFFFFFFF
    for char in name:
        value = ((value >> 5) | (value << 27)) & 0xFFFFFFFF
        value ^= char - 32 if 97 <= char <= 122 else char
    return value


def entry_name(node):
    return node.name.encode("utf-8")


def layout(root):
    counts = {"dirs": 0, "files": 0, "dir_table": 0, "file_table": 0, "partition": 0}

    def place_dirs(folder, is_root):
        folder.entry_offset = counts["dir_table"]
        counts["dirs"] += 1
        counts["dir_table"] += 0x18 + (0 if is_root else align(len(entry_name(folder)), 4))
        for child in folder.dirs():
            place_dirs(child, False)

    def place_files(folder):
        for child in folder.dirs():
            place_files(child)
        for child in folder.files():
            counts["partition"] = align(counts["partition"], 0x10)
            child.data_offset = counts["partition"]
            counts["partition"] += child.size
            child.entry_offset = counts["file_table"]
            counts["files"] += 1
            counts["file_table"] += 0x20 + align(len(entry_name(child)), 4)

    def link(folder):
        previous_dir = previous_file = None
        for child in folder.children:
            if isinstance(child, Dir):
                if previous_dir is None:
                    folder.first_dir = child
                else:
                    previous_dir.sibling = child
                previous_dir = child
                link(child)
            else:
                if previous_file is None:
                    folder.first_file = child
                else:
                    previous_file.sibling = child
                previous_file = child

    place_dirs(root, True)
    # wuhbtool counts the root twice, leaving an unused root-sized entry at the end of the directory table.
    counts["dirs"] += 1
    counts["dir_table"] += 0x18
    place_files(root)
    link(root)
    return counts


def build_tables(root, counts):
    dir_buckets = [EMPTY] * table_count(counts["dirs"])
    file_buckets = [EMPTY] * table_count(counts["files"])
    dir_table = bytearray(counts["dir_table"])
    file_table = bytearray(counts["file_table"])

    def offset_of(node):
        return EMPTY if node is None else node.entry_offset

    def populate(node, is_root):
        name = b"" if is_root else entry_name(node)
        parent = node.entry_offset if is_root else node.parent.entry_offset
        value = path_hash(0 if is_root else parent, name)
        if isinstance(node, Dir):
            bucket = value % len(dir_buckets)
            struct.pack_into(">IIIIII", dir_table, node.entry_offset, parent, offset_of(node.sibling),
                             offset_of(node.first_dir), offset_of(node.first_file), dir_buckets[bucket], len(name))
            dir_table[node.entry_offset + 0x18:node.entry_offset + 0x18 + len(name)] = name
            dir_buckets[bucket] = node.entry_offset
            for child in node.children:
                populate(child, False)
        else:
            bucket = value % len(file_buckets)
            struct.pack_into(">IIQQII", file_table, node.entry_offset, parent, offset_of(node.sibling),
                             node.data_offset, node.size, file_buckets[bucket], len(name))
            file_table[node.entry_offset + 0x20:node.entry_offset + 0x20 + len(name)] = name
            file_buckets[bucket] = node.entry_offset

    populate(root, True)
    return (struct.pack(">%dI" % len(dir_buckets), *dir_buckets), bytes(dir_table),
            struct.pack(">%dI" % len(file_buckets), *file_buckets), bytes(file_table))


def write(path, root, progress=None):
    counts = layout(root)
    dir_hashes, dir_table, file_hashes, file_table = build_tables(root, counts)
    dir_hash_offset = align(counts["partition"] + FILE_PARTITION_OFFSET, 4)
    dir_table_offset = dir_hash_offset + len(dir_hashes)
    file_hash_offset = dir_table_offset + len(dir_table)
    file_table_offset = file_hash_offset + len(file_hashes)
    header = b"WUHB" + struct.pack(">IQQQQQQQQQ", HEADER_SIZE, dir_hash_offset, len(dir_hashes), dir_table_offset,
                                   len(dir_table), file_hash_offset, len(file_hashes), file_table_offset,
                                   len(file_table), FILE_PARTITION_OFFSET)

    files = []

    def collect(folder):
        for child in folder.dirs():
            collect(child)
        files.extend(folder.files())

    collect(root)
    files.sort(key=lambda node: node.data_offset)
    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(b"\0" * (FILE_PARTITION_OFFSET - len(header)))
        for index, node in enumerate(files):
            handle.write(b"\0" * (FILE_PARTITION_OFFSET + node.data_offset - handle.tell()))
            data = node.read()
            if len(data) != node.size:
                raise WuhbError("%s is %d bytes, not the %d expected" % (node.name, len(data), node.size))
            handle.write(data)
            if progress is not None:
                progress(index + 1, len(files))
        handle.write(b"\0" * (dir_hash_offset - handle.tell()))
        handle.write(dir_hashes)
        handle.write(dir_table)
        handle.write(file_hashes)
        handle.write(file_table)


def meta_ini(long_name, short_name, author):
    return ("[menu]\nlongname=%s\nshortname=%s\nauthor=%s\n" % (long_name, short_name, author)).encode("utf-8")


def title_id(long_name, short_name, author):
    return (0x0005000F << 32) | zlib.crc32((author + long_name + short_name).encode("utf-8"))
