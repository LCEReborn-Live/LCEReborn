"""Turn a Cafe ELF (link_ghs.py output, or any final link that keeps its relocations) into an RPX laid out like the retail image.

    python tools/rpx/elf2rpx.py minecraft.elf minecraft.rpx [--keep-local-branches]

Section order follows the retail Minecraft.Client.rpx: .syscall, .text, .fexports, .rodata, .data,
.module_id, .bss, .thrdata, .thrbss, the .rela sections, the import sections, .symtab, .strtab,
.shstrtab, RPL_CRCS, RPL_FILEINFO. Code lives from 0x02000000, data from 0x10000000, and the loader
region from 0xC0000000 holds .fexports, the symbol and string tables, then the import sections.
Debug and Green Hills bookkeeping sections are dropped. Branch relocations whose target lies in code
are already resolved and are dropped, as the retail image does; branches into import sections keep
their R_PPC_REL24 so the loader can point them at the imported function.
"""
import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

CODE_BASE, DATA_BASE, LOAD_BASE = 0x02000000, 0x10000000, 0xC0000000
SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS = 0, 1, 2, 3, 4, 8
SHT_RPL_EXPORTS, SHT_RPL_IMPORTS, SHT_RPL_CRCS, SHT_RPL_FILEINFO = 0x80000001, 0x80000002, 0x80000003, 0x80000004
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR, SHF_TLS = 0x1, 0x2, 0x4, 0x400
SHF_RPL_TLS, SHF_RPL_ZLIB = 0x04000000, 0x08000000
R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA = 1, 4, 5, 6
R_PPC_REL24, R_PPC_REL14, R_PPC_REL32 = 10, 11, 26
R_PPC_DTPMOD32, R_PPC_DTPREL32 = 68, 78
R_PPC_GHS_REL16_HI, R_PPC_GHS_REL16_LO = 252, 253
SUPPORTED = {0, R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA, R_PPC_REL24, R_PPC_REL14,
             R_PPC_DTPMOD32, R_PPC_DTPREL32, R_PPC_REL32}
PC_RELATIVE = {R_PPC_REL24, R_PPC_REL14}
CODE_SECTIONS = ['.syscall', '.text']
DATA_SECTIONS = ['.rodata', '.data', '.module_id', '.bss', '.thrdata', '.thrbss']
TLS_SECTIONS = ('.thrdata', '.thrbss')
EXPORTS = ['__preinit_user']
DEFLATE_MIN = 0x18
FILEINFO_SIZE_TO_STRINGS = 0x60
# RPL_FILEINFO values the retail image carries.
VERSION, TEXT_ALIGN, DATA_ALIGN, LOAD_ALIGN = 0xCAFE0402, 32, 0x100, 32
STACK_SIZE, HEAP_SIZE, MIN_VERSION, COMPRESSION_LEVEL = 0x10000, 0x8000, 0x5078, -1
CAFE_SDK_VERSION, CAFE_SDK_REVISION, TLS_ALIGN_SHIFT = 0x5335, 0x10D4B, 5
RPL_IS_RPX, RPL_HAS_TLS = 0x2, 0x8
RETAIL_TAGS = b'BUILD_TYPE\0NDEBUG\0\0'


def align(value, to):
    return (value + to - 1) & ~(to - 1) if to > 1 else value


class Section:
    def __init__(self, name, kind, flags, addr, data, link=0, info=0, addralign=1, entsize=0, size=None):
        self.name, self.kind, self.flags, self.addr = name, kind, flags, addr
        self.data = bytearray(data)
        self.size = len(self.data) if size is None else size
        self.link, self.info, self.addralign, self.entsize = link, info, addralign, entsize
        self.offset = 0
        self.name_offset = 0


def read_elf(path):
    raw = Path(path).read_bytes()
    if raw[:4] != b'\x7fELF' or raw[4] != 1 or raw[5] != 2:
        raise SystemExit('%s is not a big-endian ELF32 file' % path)
    entry = struct.unpack('>I', raw[24:28])[0]
    shoff = struct.unpack('>I', raw[32:36])[0]
    shentsize, shnum, shstrndx = struct.unpack('>HHH', raw[46:52])
    sections = []
    for index in range(shnum):
        start = shoff + index * shentsize
        name_off, kind, flags, addr, offset, size, link, info, addralign, entsize = struct.unpack(
            '>IIIIIIIIII', raw[start:start + 40])
        data = b'' if kind in (SHT_NOBITS, SHT_NULL) else raw[offset:offset + size]
        section = Section(name_off, kind, flags, addr, data, link, info, addralign, entsize, size)
        sections.append(section)
    strings = bytes(sections[shstrndx].data)
    for section in sections:
        section.name = strings[section.name:strings.index(b'\0', section.name)].decode()
    return entry, sections


def symbols_of(data):
    return [list(struct.unpack('>IIIBBH', data[offset:offset + 16])) for offset in range(0, len(data), 16)]


def pack_symbols(rows):
    return b''.join(struct.pack('>IIIBBH', *row) for row in rows)


def string_at(table, offset):
    return bytes(table[offset:table.index(b'\0', offset)]).decode(errors='replace')


def build_exports(symbols, strtab, names):
    """RPL_EXPORTS body: count, signature, {value, name offset} per export, then the names.

    Returns (bytes, [(entry offset, symbol index)]) so the caller can emit R_PPC_ADDR32 for each value."""
    found = []
    for wanted in names:
        for index, row in enumerate(symbols):
            if row[5] and string_at(strtab, row[0]) == wanted:
                found.append((wanted, row[1], index))
                break
    head = struct.pack('>II', len(found), zlib.crc32(b''.join(name.encode() for name, _, _ in found)))
    entries, strings, relocs = bytearray(), bytearray(), []
    base = 8 + 8 * len(found)
    for position, (name, value, index) in enumerate(found):
        entries += struct.pack('>II', value, base + len(strings))
        strings += name.encode() + b'\0'
        relocs.append((8 + 8 * position, index, value))
    return head + bytes(entries) + bytes(strings), relocs


def convert(elf_path, rpx_path, keep_local_branches=False, filename='Minecraft.Client.rpx', renames=None):
    """renames: {symbol name: export name} for import slots the objects reference under another name."""
    entry, sections = read_elf(elf_path)
    by_name = {section.name: index for index, section in enumerate(sections)}
    symtab_index = next(index for index, section in enumerate(sections) if section.kind == SHT_SYMTAB)
    symtab = sections[symtab_index]
    strtab = bytearray(sections[symtab.link].data)
    symbols = symbols_of(symtab.data)
    if renames:
        for row in symbols:
            label = string_at(strtab, row[0]) if row[0] else ''
            if label in renames:
                row[0] = len(strtab)
                strtab.extend(renames[label].encode() + b'\0')
    strtab = bytes(strtab)

    code = [by_name[name] for name in CODE_SECTIONS if name in by_name and sections[by_name[name]].size]
    data = [by_name[name] for name in DATA_SECTIONS if name in by_name and sections[by_name[name]].size]
    imports = sorted((index for index, section in enumerate(sections)
                      if section.name.startswith('.fimport_') or section.name.startswith('.dimport_')),
                     key=lambda index: sections[index].addr)
    placed = set(code + data + imports)
    stray = [section.name for index, section in enumerate(sections)
             if section.flags & SHF_ALLOC and section.size and index not in placed]
    if stray:
        raise SystemExit('allocated sections with no place in the RPX layout: %s' % ', '.join(stray))

    code_ranges = [(sections[index].addr, sections[index].addr + sections[index].size) for index in code]

    def in_code(address):
        return any(low <= address < high for low, high in code_ranges)

    stats = {'kept': 0, 'dropped_code_branch': 0}
    unsupported = {}
    relas = {}
    for section in sections:
        if section.kind != SHT_RELA or section.info not in placed or section.info in imports:
            continue
        rows = relas.setdefault(section.info, [])
        for offset in range(0, len(section.data), 12):
            where, info, addend = struct.unpack('>IIi', section.data[offset:offset + 12])
            kind, symbol = info & 0xFF, info >> 8
            if kind not in SUPPORTED:
                unsupported[kind] = unsupported.get(kind, 0) + 1
                continue
            if kind in PC_RELATIVE and not keep_local_branches and in_code(symbols[symbol][1] + addend):
                stats['dropped_code_branch'] += 1
                continue
            if kind == R_PPC_REL32:
                rows.append((where, (symbol << 8) | R_PPC_GHS_REL16_HI, addend))
                rows.append((where + 2, (symbol << 8) | R_PPC_GHS_REL16_LO, addend + 2))
                stats['kept'] += 2
                continue
            rows.append((where, info, addend))
            stats['kept'] += 1
    if unsupported:
        raise SystemExit('unsupported relocation types %s' % unsupported)

    out = [Section('', SHT_NULL, 0, 0, b'')]
    index_map = {}

    def add_copy(index, kind=None, flags=None):
        section = sections[index]
        if flags is None:
            flags = section.flags & (SHF_WRITE | SHF_ALLOC | SHF_EXECINSTR)
            if DATA_BASE <= section.addr < LOAD_BASE:
                # Everything in the data region is writable in an RPL (retail .rodata and .module_id are
                # 0x08000003); the loader maps allocated read-only sections into the loader-info region.
                flags |= SHF_WRITE
            if section.flags & SHF_TLS or section.name in TLS_SECTIONS:
                flags |= SHF_RPL_TLS
        copy = Section(section.name, kind or section.kind, flags, section.addr, section.data,
                       addralign=max(section.addralign, 1), size=section.size)
        index_map[index] = len(out)
        out.append(copy)

    for index in code:
        add_copy(index)
    export_bytes, export_relocs = build_exports(symbols, strtab, EXPORTS)
    fexports = Section('.fexports', SHT_RPL_EXPORTS, SHF_ALLOC | SHF_EXECINSTR, LOAD_BASE, export_bytes,
                       addralign=32)
    fexports_index = len(out)
    out.append(fexports)
    for index in data:
        add_copy(index)
    rela_sections = []
    for index in code + data:
        rows = relas.get(index)
        if rows:
            rela = Section('.rela' + sections[index].name, SHT_RELA, 0, 0, b'',
                           info=index_map[index], addralign=4, entsize=12)
            rela.rows = sorted(rows)
            rela_sections.append(rela)
            out.append(rela)
    if export_relocs:
        rela = Section('.rela.fexports', SHT_RELA, 0, 0, b'', info=fexports_index, addralign=12, entsize=12)
        rela.rows = [(LOAD_BASE + offset, (index << 8) | R_PPC_ADDR32, 0) for offset, index, _ in export_relocs]
        rela_sections.append(rela)
        out.append(rela)
    for index in imports:
        flags = SHF_ALLOC | (SHF_EXECINSTR if sections[index].name.startswith('.fimport_') else 0)
        add_copy(index, SHT_RPL_IMPORTS, flags)
    # Symbols: one section symbol per loaded section, then the functions and TLS objects, then
    # every global, weak and undefined symbol. Local data names and Green Hills' labels (defined untyped
    # symbols such as line and end-of-function markers, global or not, and link-once section symbols) are
    # dropped; relocations against them move to their section's symbol, the way the retail image relocates
    # against $TEXT/$DATA.
    section_symbol = {}
    new_rows = [[0, 0, 0, 0, 0, 0]]
    for position, section in enumerate(out):
        if position and section.flags & SHF_ALLOC:
            section_symbol[position] = len(new_rows)
            new_rows.append([0, 0, 0, 3, 0, position])
    kept_locals, kept_globals = [], []
    for old, row in enumerate(symbols):
        if not old:
            continue
        bind, kind, shndx = row[3] >> 4, row[3] & 0xF, row[5]
        if 0 < shndx < 0xFF00 and shndx not in index_map:
            continue
        if bind == 0 and kind not in (2, 6):
            continue
        if shndx and kind == 0:
            continue
        (kept_globals if bind else kept_locals).append(old)
    names = bytearray(b'\0')
    remap = {}
    for old in kept_locals + kept_globals:
        row = list(symbols[old])
        label = string_at(strtab, row[0]) if row[0] else ''
        row[0] = len(names) if label else 0
        if label:
            names.extend(label.encode() + b'\0')
        if row[5] in index_map:
            row[5] = index_map[row[5]]
        remap[old] = len(new_rows)
        new_rows.append(row)
    first_global = len(new_rows) - len(kept_globals)
    for rela in rela_sections:
        fixed = []
        for where, info, addend in rela.rows:
            old, kind = info >> 8, info & 0xFF
            if old in remap:
                fixed.append((where, (remap[old] << 8) | kind, addend))
                continue
            value, shndx = symbols[old][1], symbols[old][5]
            target = index_map.get(shndx)
            if target not in section_symbol:
                raise SystemExit('relocation at 0x%08x against a symbol in no loaded section' % where)
            fixed.append((where, (section_symbol[target] << 8) | kind, value + addend - out[target].addr))
        rela.data = bytearray(b''.join(struct.pack('>IIi', *row) for row in fixed))
        rela.size = len(rela.data)
        rela.rows = None
    strtab = bytes(names)

    new_symtab = len(out)
    out.append(Section('.symtab', SHT_SYMTAB, SHF_ALLOC, 0, b'', link=new_symtab + 1, info=first_global,
                       addralign=4, entsize=16))
    out.append(Section('.strtab', SHT_STRTAB, SHF_ALLOC, 0, strtab, addralign=1))
    out.append(Section('.shstrtab', SHT_STRTAB, SHF_ALLOC, 0, b'', addralign=1))
    for rela in rela_sections:
        rela.link = new_symtab

    names = bytearray(b'\0')
    for section in out[1:]:
        section.name_offset = len(names)
        names.extend(section.name.encode() + b'\0')
    out[new_symtab + 2].data = names
    out[new_symtab + 2].size = len(names)

    # Loader region placement; import sections move, so their symbols move with them.
    symbol_rows = new_rows
    load = LOAD_BASE + align(fexports.size, 32)
    out[new_symtab].size = len(symbol_rows) * 16
    for section in out[new_symtab:new_symtab + 3]:
        load = align(load, max(section.addralign, 4))
        section.addr = load
        load += section.size
    moved = {}
    for index in imports:
        section = out[index_map[index]]
        load = align(load, 64)
        moved[index_map[index]] = (section.addr, load)
        section.addr = load
        load += section.size
    for position, row in enumerate(symbol_rows):
        if row[3] == 3 and position in section_symbol.values():
            row[1] = out[row[5]].addr
        elif row[5] in moved:
            old, new = moved[row[5]]
            row[1] = row[1] - old + new
    out[new_symtab].data = bytearray(pack_symbols(symbol_rows))
    out[new_symtab].size = len(out[new_symtab].data)

    # The console's loader places each section at its segment's base plus its offset from the segment's
    # lowest address and rejects any that land off their alignment; Cemu does not check.
    misaligned = []
    for section in out:
        if not section.flags & SHF_ALLOC:
            continue
        limit = TEXT_ALIGN if section.addr < DATA_BASE else DATA_ALIGN if section.addr < LOAD_BASE else LOAD_ALIGN
        alignment = max(section.addralign, 1)
        if section.addr % alignment or alignment > limit:
            misaligned.append('%s at 0x%08x (align 0x%x)' % (section.name, section.addr, alignment))
    if misaligned:
        raise SystemExit('sections the Cafe loader rejects as misaligned: %s' % ', '.join(misaligned))

    tls = any(section.flags & SHF_RPL_TLS for section in out)
    # The console's loader writes a 16-byte trampoline after the code for each R_PPC_REL24 that cannot
    # reach its target, such as a branch into a system library, and fails the load once the room up to
    # textSize runs out; the retail image reserves one per such relocation. Cemu makes its own.
    branches = sum(1 for rela in rela_sections for offset in range(0, len(rela.data), 12)
                   if rela.data[offset + 7] == R_PPC_REL24)
    text_end = max(s.addr + s.size for s in out if CODE_BASE <= s.addr < DATA_BASE)
    text_end = align(text_end, 16) + 16 * branches
    data_end = max((s.addr + s.size for s in out if DATA_BASE <= s.addr < LOAD_BASE), default=DATA_BASE)
    load_end = max(s.addr + s.size for s in out if s.addr >= LOAD_BASE)
    temp = sum(len(s.data) + 128 for s in out[1:] if s.addr == 0)
    sda = sda2 = 0
    for row in symbol_rows:
        label = string_at(strtab, row[0]) if row[0] < len(strtab) else ''
        if label == '_SDA_BASE_':
            sda = row[1]
        elif label == '_SDA2_BASE_':
            sda2 = row[1]
    name_bytes = filename.encode() + b'\0'
    fileinfo = struct.pack('>IIIIIIIIIIIIIIIIIiIIIIHHI', VERSION, align(text_end - CODE_BASE, TEXT_ALIGN), TEXT_ALIGN,
                           align(data_end - DATA_BASE, DATA_ALIGN), DATA_ALIGN, align(load_end - LOAD_BASE, LOAD_ALIGN),
                           LOAD_ALIGN, temp, 0, sda, sda2, STACK_SIZE, FILEINFO_SIZE_TO_STRINGS,
                           RPL_IS_RPX | (RPL_HAS_TLS if tls else 0), HEAP_SIZE,
                           FILEINFO_SIZE_TO_STRINGS + len(name_bytes), MIN_VERSION, COMPRESSION_LEVEL, 0, 0,
                           CAFE_SDK_VERSION, CAFE_SDK_REVISION, 0, TLS_ALIGN_SHIFT if tls else 0, 0)
    fileinfo += name_bytes + RETAIL_TAGS
    crcs = [zlib.crc32(bytes(section.data)) if section.data else 0 for section in out]
    crcs += [0, zlib.crc32(fileinfo)]
    out.append(Section('', SHT_RPL_CRCS, 0, 0, b''.join(struct.pack('>I', crc) for crc in crcs),
                       addralign=4, entsize=4))
    out.append(Section('', SHT_RPL_FILEINFO, 0, 0, fileinfo, addralign=4))

    for section in out:
        if section.kind in (SHT_NULL, SHT_NOBITS, SHT_RPL_CRCS, SHT_RPL_FILEINFO) or len(section.data) < DEFLATE_MIN:
            continue
        deflated = struct.pack('>I', len(section.data)) + zlib.compress(bytes(section.data), 6)
        if len(deflated) >= len(section.data):
            continue
        section.data = bytearray(deflated)
        section.flags |= SHF_RPL_ZLIB

    shoff = 64
    offset = align(shoff + len(out) * 40, 64)
    has_data = [s for s in out if s.data and s.kind not in (SHT_NULL, SHT_NOBITS)]
    order = [s for s in has_data if s.kind == SHT_RPL_CRCS] + [s for s in has_data if s.kind == SHT_RPL_FILEINFO]
    order += [s for s in has_data if s not in order and s.kind != SHT_RPL_IMPORTS and
              s.flags & SHF_ALLOC and s.flags & SHF_WRITE and not s.flags & SHF_EXECINSTR]
    order += [s for s in has_data if s not in order and s.flags & SHF_ALLOC and
              (s.kind in (SHT_RPL_EXPORTS, SHT_RPL_IMPORTS) or not s.flags & (SHF_EXECINSTR | SHF_WRITE))]
    order += [s for s in has_data if s not in order and s.flags & SHF_EXECINSTR]
    order += [s for s in has_data if s not in order]
    for section in order:
        section.offset = offset
        offset += len(section.data)

    blob = bytearray(offset)
    header = b'\x7fELF' + bytes([1, 2, 1, 0xCA, 0xFE]) + bytes(7)
    header += struct.pack('>HHIIIIIHHHHHH', 0xFE01, 20, 1, entry, 0, shoff, 0, 52, 0, 0, 40, len(out),
                          new_symtab + 2)
    blob[:len(header)] = header
    for position, section in enumerate(out):
        size = section.size if section.kind == SHT_NOBITS else len(section.data)
        struct.pack_into('>IIIIIIIIII', blob, shoff + position * 40, section.name_offset, section.kind,
                         section.flags, section.addr, section.offset, size, section.link, section.info,
                         section.addralign, section.entsize)
        if section.data:
            blob[section.offset:section.offset + len(section.data)] = section.data
    Path(rpx_path).write_bytes(bytes(blob))
    return stats


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('elf')
    parser.add_argument('rpx')
    parser.add_argument('--keep-local-branches', action='store_true')
    parser.add_argument('--renames', type=Path, help='JSON {symbol: export} written by link.py')
    args = parser.parse_args()
    renames = json.loads(args.renames.read_text(encoding='utf-8')) if args.renames else None
    stats = convert(args.elf, args.rpx, args.keep_local_branches, renames=renames)
    print('relocations kept %(kept)d, branches into code dropped %(dropped_code_branch)d' % stats)


if __name__ == '__main__':
    sys.exit(main())
