"""Cafe import stubs for a Green Hills link.

The symbol -> RPL map comes from wut's export lists: cafe/import-only/*.def first (modules the game
imports from that wut does not link, such as the legacy snd_core and snd_user AX libraries the retail
image uses in place of sndcore2 and snduser2), then cafe/*.def, where wut's commented-out exports (the
GHS runtime hooks such as __ghsLock) are real exports and count too. When the retail symbol table is
present (evidence/symbols-update.tsv, not distributed), the retail image's own import sections take
precedence.

Each RPL becomes one relocatable ELF object holding a .fimport_<rpl> and/or .dimport_<rpl> section laid
out the way the retail image lays them out: a word count, a signature word, then one 8-byte slot per
symbol starting at offset 8, with the module name written over the leading slots.
"""
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RETAIL_SYMBOLS = ROOT / 'evidence/symbols-update.tsv'
WUT_CAFE = ROOT / 'third_party/wut/cafe'
RETAIL_IMPORT_LOW = 0xC0000000


def set_wut(root):
    """Read the export lists and asm labels of the wut checkout at root instead of third_party/wut."""
    global WUT_CAFE
    WUT_CAFE = Path(root) / 'cafe'
    LABEL_ROOTS[1] = Path(root) / 'include'


def retail_import_map():
    """{symbol: (rpl, is_data)} from the retail image's import sections; empty without the symbol table."""
    if not RETAIL_SYMBOLS.is_file():
        return {}
    sections = []
    symbols = []
    with open(RETAIL_SYMBOLS, encoding='utf-8') as stream:
        next(stream)
        for line in stream:
            value, size, kind, name = line.rstrip('\r\n').split('\t')
            address = int(value, 16)
            if address < RETAIL_IMPORT_LOW or not name:
                continue
            if kind == '3' and (name.startswith('.fimport_') or name.startswith('.dimport_')):
                sections.append((address, name))
            elif kind in ('1', '2'):
                symbols.append((address, name))
    sections.sort()
    result = {}
    for address, name in symbols:
        owner = None
        for start, section in sections:
            if start <= address:
                owner = section
            else:
                break
        if owner is None:
            continue
        result.setdefault(name, (owner[len('.fimport_'):], owner.startswith('.dimport_')))
    return result


def wut_import_map():
    """{symbol: (rpl, is_data)} from wut's export lists, commented-out exports included; the import-only
    lists win over the linked ones."""
    result = {}
    for path in sorted(WUT_CAFE.glob('import-only/*.def')) + sorted(WUT_CAFE.glob('*.def')):
        module, section = None, None
        for raw in path.read_text(encoding='utf-8').splitlines():
            line = raw.strip()
            if not line:
                continue
            if line.startswith(':NAME'):
                module = line.split()[1]
                continue
            if line.startswith(':'):
                section = line[1:]
                continue
            if line.startswith('//'):
                line = line[2:].strip()
            if not line or ' ' in line or module is None:
                continue
            if section in ('TEXT', 'TEXT_WRAP'):
                result.setdefault(line, (module, False))
            elif section in ('DATA', 'DATA_WRAP'):
                result.setdefault(line, (module, True))
    return result


def import_map():
    combined = wut_import_map()
    combined.update(retail_import_map())
    # Wii U Live uses the system HTTPS library, which modern wut does not link.
    for name in ('curl_global_init', 'curl_slist_append', 'curl_easy_init', 'curl_easy_setopt',
                 'curl_easy_perform', 'curl_easy_getinfo', 'curl_easy_cleanup', 'curl_slist_free_all'):
        combined.setdefault(name, ('nlibcurl', False))
    return combined


ASM_LABEL = re.compile(r'\b(\w+)\s*\([^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*\)\s*(?:const\s*)?asm\s*\(\s*"([^"]+)"\s*\)')
LABEL_ROOTS = [ROOT / 'source', ROOT / 'third_party/wut/include']


def asm_label_targets():
    """{declared name: [asm label target, ...]} for every GCC asm label in the source tree and wut's headers.

    Green Hills defines asm(x) away, so a declaration carrying a label binds to the name Green Hills mangles
    for it instead of the label; this table recovers what the declaration meant to import."""
    table = {}
    for root in LABEL_ROOTS:
        for path in root.rglob('*'):
            if path.suffix not in ('.h', '.hpp', '.cpp') or not path.is_file():
                continue
            text = path.read_text(encoding='utf-8', errors='replace')
            if 'asm' not in text:
                continue
            for name, target in ASM_LABEL.findall(text):
                table.setdefault(name, set()).add(target)
    return {name: sorted(targets) for name, targets in table.items()}


def qualifier_end(name, start):
    """Index just past a Green Hills qualifier (Q<n>_<len><id>... or <len><id>) beginning at start."""
    if name.startswith('Q', start) and start + 2 < len(name) and name[start + 1].isdigit() and name[start + 2] == '_':
        index = start + 3
        for _ in range(int(name[start + 1])):
            digits = re.match(r'\d+', name[index:])
            if not digits:
                return -1
            index += len(digits.group()) + int(digits.group())
        return index
    digits = re.match(r'\d+', name[start:])
    if digits:
        return start + len(digits.group()) + int(digits.group())
    return start


def function_split(name):
    """(prefix through 'F', parameter text) for a Green Hills mangled function name, or (None, None)."""
    separator = name.find('__', 1)
    while separator > 0:
        index = qualifier_end(name, separator + 2)
        if index > 0:
            while index < len(name) and name[index] in 'CS':
                index += 1
            if index < len(name) and name[index] == 'F':
                return name[:index + 1], name[index + 1:]
        separator = name.find('__', separator + 1)
    return None, None


def mangled_prefix(name):
    """'GetUuid__Q2_2nn3actF' for a Green Hills mangled function name, None for an unmangled one."""
    return function_split(name)[0]


def resolve_aliases(undefined, mapping):
    """{referenced name: export name} for undefined names that are a mislabelled import.

    An extern "C" wrapper declared with asm("export") resolves to its label. A C++ declaration whose
    parameter spelling differs from the export (int16_t* for wchar_t*, char* for ACTUuid*) resolves to the
    one import with the same qualified name and the same parameter count; anything ambiguous stays
    unresolved."""
    labels = asm_label_targets()
    by_prefix = {}
    for export in mapping:
        prefix = mangled_prefix(export)
        if prefix:
            by_prefix.setdefault(prefix, []).append(export)
    aliases = {}
    for name in sorted(undefined):
        if name in mapping:
            continue
        targets = [target for target in labels.get(name, ()) if target in mapping]
        if len(targets) == 1:
            aliases[name] = targets[0]
            continue
        prefix = mangled_prefix(name)
        if not prefix:
            continue
        candidates = [export for export in by_prefix.get(prefix, ())
                      if parameter_count(export) == parameter_count(name)]
        if len(candidates) == 1:
            aliases[name] = candidates[0]
    return aliases


def parameter_count(name):
    """Parameters in a Green Hills mangled function name; repeats (N21, T1) count."""
    text = function_split(name)[1]
    if text is None:
        return -1
    count, index = 0, 0
    while index < len(text):
        char = text[index]
        if char in 'CVS':
            index += 1
            continue
        if char in 'PR':
            index += 1
            continue
        if char == 'A':
            index = text.index('_', index) + 1
            continue
        if char == 'N':
            repeat = int(text[index + 1])
            index += 3
            count += repeat
            continue
        if char == 'T':
            index += 2
            count += 1
            continue
        if char == 'Q':
            parts = int(text[index + 1])
            index += 3
            for _ in range(parts):
                digits = re.match(r'\d+', text[index:]).group()
                index += len(digits) + int(digits)
            count += 1
            continue
        if char.isdigit():
            digits = re.match(r'\d+', text[index:]).group()
            index += len(digits) + int(digits)
            count += 1
            continue
        if char == 'U':
            index += 1
            continue
        if char == 'v' and count == 0:
            return 0
        index += 1
        count += 1
    return count


def section_bytes(module, count):
    name = module.encode() + b'\0'
    name_size = (len(name) + 7) & ~7
    size = 8 + max(8 * count, name_size)
    blob = bytearray(size)
    struct.pack_into('>II', blob, 0, count, zlib.crc32(module.encode()))
    blob[8:8 + len(name)] = name
    return bytes(blob)


def write_stub_object(path, module, functions, data):
    """Write a big-endian ELF32 PowerPC relocatable object with this module's import sections."""
    sections = []  # (name, type, flags, bytes, symbols[(name, offset, is_data)])
    if functions:
        sections.append(('.fimport_' + module, 1, 0x6, section_bytes(module, len(functions)),
                         [(name, 8 + 8 * index, False) for index, name in enumerate(functions)]))
    if data:
        sections.append(('.dimport_' + module, 1, 0x2, section_bytes(module, len(data)),
                         [(name, 8 + 8 * index, True) for index, name in enumerate(data)]))
    shstrtab = bytearray(b'\0')
    strtab = bytearray(b'\0')

    def add(table, text):
        offset = len(table)
        table.extend(text.encode() + b'\0')
        return offset

    symtab = bytearray(16)
    headers = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    payloads = [b'']
    for index, (name, kind, flags, blob, _) in enumerate(sections, start=1):
        headers.append((add(shstrtab, name), kind, flags, 0, None, len(blob), 0, 0, 8, 0))
        payloads.append(blob)
        symtab += struct.pack('>IIIBBH', 0, 0, 0, 3, 0, index)
    locals_end = len(symtab) // 16
    for index, (_, _, _, _, symbols) in enumerate(sections, start=1):
        for name, offset, is_data in symbols:
            info = (1 << 4) | (1 if is_data else 2)
            symtab += struct.pack('>IIIBBH', add(strtab, name), offset, 8, info, 0, index)
    symtab_index = len(headers)
    headers.append((add(shstrtab, '.symtab'), 2, 0, 0, None, len(symtab), symtab_index + 1, locals_end, 4, 16))
    payloads.append(bytes(symtab))
    headers.append((add(shstrtab, '.strtab'), 3, 0, 0, None, len(strtab), 0, 0, 1, 0))
    payloads.append(bytes(strtab))
    shstrndx = len(headers)
    headers.append((add(shstrtab, '.shstrtab'), 3, 0, 0, None, 0, 0, 0, 1, 0))
    payloads.append(bytes(shstrtab))
    headers[-1] = headers[-1][:5] + (len(shstrtab),) + headers[-1][6:]
    offset = 52
    out = bytearray()
    placed = []
    body = bytearray()
    for header, payload in zip(headers, payloads):
        if header[1] == 0:
            placed.append(header[:4] + (0,) + header[5:])
            continue
        while (offset + len(body)) % 8:
            body.append(0)
        placed.append(header[:4] + (offset + len(body),) + header[5:])
        body += payload
    shoff = offset + len(body)
    while shoff % 4:
        body.append(0)
        shoff += 1
    ident = b'\x7fELF' + bytes([1, 2, 1, 0]) + bytes(8)
    out += ident + struct.pack('>HHIIIIIHHHHHH', 1, 20, 1, 0, 0, shoff, 0, 52, 0, 0, 40, len(placed), shstrndx)
    out += body
    for header in placed:
        out += struct.pack('>IIIIIIIIII', *header)
    path.write_bytes(bytes(out))


def generate(undefined, out_dir):
    """Write one stub object per RPL for the undefined symbols that are imports.

    Returns (object paths, {symbol: rpl}, unresolved symbols, {referenced name: export name}).
    An aliased name gets its own slot under the name the objects reference; the RPX converter renames it
    to the export."""
    mapping = import_map()
    aliases = resolve_aliases(undefined, mapping)
    by_module = {}
    unresolved = []
    for name in sorted(undefined):
        key = aliases.get(name, name)
        if key not in mapping:
            unresolved.append(name)
            continue
        module, is_data = mapping[key]
        by_module.setdefault(module, ([], []))[1 if is_data else 0].append(name)
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for module, (functions, data) in sorted(by_module.items()):
        path = out_dir / ('import_%s.o' % module)
        write_stub_object(path, module, functions, data)
        paths.append(path)
    chosen = {name: mapping[aliases.get(name, name)][0] for name in undefined
              if aliases.get(name, name) in mapping}
    return paths, chosen, unresolved, aliases
