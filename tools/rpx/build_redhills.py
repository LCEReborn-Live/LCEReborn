"""Build Minecraft.Client.rpx with RedHills, the Green Hills ABI clang, linked the way the retail image is.

    python tools/rpx/build_redhills.py [--redhills CLANG] [--devkitpro DIR | --libgcc FILE]
                                       [--wut DIR] [--runtime16 DIR] [--third-party DIR] [--sources DIR]
                                       [--out build/redhills] [--jobs N] [--install [DIR]]

The game objects keep the Green Hills class ABI the original was built with (layout, vtables, virtual
bases, RTTI), so they are compiled with RedHills (https://github.com/Jacobwasbeast/RedHills) using the
semantic flags of the original build, and the image is put together like the retail one: the Cafe crt0,
the reconstructed Cafe libsys port, the game's dlmalloc heap, the EDG C++ runtime, import stubs for every
RPL function and datum, and tools/rpx/elf2rpx.py. The C and C++ libraries are free ones built for this ABI:

1. game objects: every source unit except BootInit.cpp, which only hooks wut's crt for a devkitPPC
   build; dlmalloc is compiled as C, as the original was; the EDG runtime (source/cafe/libsedg) is an
   archive, so its operator new/delete and RTTI entry points link only where nothing else defines them
2. the third-party C libraries the image links statically (libpng 1.6.2, and zlib 1.2.5 as NEX bundles it,
   every external name prefixed quazal_), compiled with RedHills
3. libstdc++ and newlib's libm rebuilt with RedHills (tools/redhills/build_libstdcxx.py, build_libm.py):
   GCC code keeps condition values across calls, which stock Cemu mishandles
4. the crt: tools/rpx/cafe_crt0.ppc (the retail _start) assembled by RedHills, the host-syscall entry the
   libsys port calls, _main for ELF constructor lists, and newlib's system interface on the libsys port,
   dlmalloc and coreinit (tools/redhills/runtime)
5. ld.lld with tools/redhills/cafe.ld against the two-byte wchar_t newlib (tools/redhills/build_runtime16.py)
   and devkitPPC's libgcc; pass 1 finds the imported names, pass 2 links their stubs (tools/rpx/imports.py).
   The coreinit exports the image imports in place of C library code (memcpy, memset, memmove, exit,
   _Exit, environ) are taken from coreinit.
6. tools/redhills/crscan.py, which fails the build on code stock Cemu 2.6 mishandles, then
   tools/rpx/elf2rpx.py

Inputs:
  --redhills     the RedHills clang (default $REDHILLS)
  --devkitpro    a devkitPro install with devkitPPC, whose libgcc is linked (default $DEVKITPRO);
                 --libgcc names the archive directly
  --wut          wut's headers and cafe/*.def export lists (default third_party/wut)
  --runtime16    the two-byte wchar_t runtime (default build/runtime16)
  --third-party  the third-party sources, laid out as zlib/, libpng/, libpng-wiiu/ (the Wii U png.h,
                 pngwrite.c and pnglibconf.h), curl/include/, boost/ (holding boost/), stb/ (stb_image.h)
                 and rapidxml/ (rapidxml.hpp); without it the reconstruction tree's own copies are used
  --sources      the cache tools/redhills/sources.py prepares the newlib and libstdc++ sources in
The LLVM tools (ld.lld, llvm-ar, llvm-nm) are found by tools/redhills/toolchain.py.
--install copies the RPX into a Cemu update package's code/ folder.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'tools/redhills'))
import imports  # noqa: E402
import sources  # noqa: E402
import toolchain  # noqa: E402

SOURCE = ROOT / 'source'
RUNTIME = ROOT / 'tools/redhills/runtime'
LINKER_SCRIPT = ROOT / 'tools/redhills/cafe.ld'
CRT0 = HERE / 'cafe_crt0.ppc'
PACKAGE = ROOT / 'work/cemu-updates/Minecraft Wii U Edition Recon [Update] [0005000e101d9d00]/code'
GCC_VERSION = '16.1.0'
# Units that exist only to hook wut's crt for a devkitPPC build; the Cafe crt replaces them.
GCC_ONLY_UNITS = {'platform/wiiu/BootInit.cpp'}
# Units the original compiled as C; dlmalloc's malloc family has C names.
C_UNITS = {'vendor/dlmalloc.cpp'}
EDG_RUNTIME = re.compile(r'^cafe/libsedg/')
# Platform layers of other targets; the Wii U build compiles source/platform/wiiu only.
OTHER_PLATFORMS = re.compile(r'^platform/(?!wiiu/)[^/]+/')
# This parser has no platform dependencies and also serves the native Live client.
SHARED_PLATFORM_UNITS = {'platform/pc/PcJson.cpp'}
TARGET = ['-target', 'powerpc-eabi', '-mcpu=750', '-O2', '-ffunction-sections', '-fdata-sections',
          '-fno-delete-null-pointer-checks']
# Boost's configuration does not know the Cafe target and would build shared_ptr without threads; the
# original's reference counts are atomic (lwarx/stwcx.), so BOOST_SP_ENABLE_THREADS selects boost's PowerPC
# counter, which emits the same sequence.
SEMANTIC = ['-DNDEBUG', '-DNDEV=1', '-DCAFE=2', '-DPLATFORM=CAFE', '-DEPPC', '-DBOOST_SP_NO_SYNC',
            '-DBOOST_SP_ENABLE_THREADS', '-DBOOST_ENABLE_ASSERT_HANDLER', '-DBOOST_NO_EXCEPTIONS', '-DESPRESSO',
            '-D__WIIU__', '-D__WUT__', '-D__BASE__=__FILE_NAME__', '-DLCE_PLATFORM_WIIU=1']
PNG_SOURCES = ['png.c', 'pngerror.c', 'pngget.c', 'pngmem.c', 'pngpread.c', 'pngread.c', 'pngrio.c', 'pngrtran.c',
               'pngrutil.c', 'pngset.c', 'pngtrans.c', 'pngwio.c', 'pngwtran.c', 'pngwutil.c']
ZLIB_SOURCES = ['adler32.c', 'compress.c', 'crc32.c', 'deflate.c', 'infback.c', 'inffast.c', 'inflate.c',
                'inftrees.c', 'trees.c', 'uncompr.c', 'zutil.c']
# libsedg's __call_dtors() (C++ linkage) under the Green Hills name the crt0 registers with atexit.
CRT0_RENAMES = {'__call_dtors__Fv': '_Z12__call_dtorsv'}


class ThirdParty:
    """Where the third-party sources and headers are."""

    def __init__(self, zlib, libpng, libpng_wiiu, curl_include, boost, stb, rapidxml):
        self.zlib, self.libpng, self.libpng_wiiu = zlib, libpng, libpng_wiiu
        self.curl_include, self.boost, self.stb, self.rapidxml = curl_include, boost, stb, rapidxml

    @classmethod
    def from_directory(cls, root):
        return cls(root / 'zlib', root / 'libpng', root / 'libpng-wiiu', root / 'curl/include', root / 'boost',
                   root / 'stb', root / 'rapidxml')

    @classmethod
    def reconstruction_tree(cls):
        return cls(ROOT / 'evidence/upstream/zlib', ROOT / 'evidence/upstream/libpng',
                   ROOT / 'work/providers/libpng-wiiu', ROOT / 'third_party/curl-headers-7.38.0/include',
                   ROOT / 'scripts/syntax-shim', ROOT / 'evidence/upstream', ROOT / 'evidence/upstream/rapidxml')

    def missing(self):
        required = [self.zlib / 'zlib.h', self.libpng / 'png.c', self.libpng_wiiu / 'png.h',
                    self.libpng_wiiu / 'pngwrite.c', self.libpng_wiiu / 'pnglibconf.h',
                    self.curl_include / 'curl/curlbuild.h', self.boost / 'boost/shared_ptr.hpp',
                    self.stb / 'stb_image.h', self.rapidxml / 'rapidxml.hpp']
        return [path for path in required if not path.is_file()]


class Build:
    """The flags and tools of one build."""

    def __init__(self, clang, wut, runtime16, third_party):
        self.clang = clang
        self.third_party = third_party
        self.ar = toolchain.llvm_tool('llvm-ar', clang)
        self.nm = toolchain.llvm_tool('llvm-nm', clang)
        self.lld = toolchain.llvm_tool('ld.lld', clang)
        self.runtime16 = runtime16
        cxx_include = runtime16 / 'include/c++' / GCC_VERSION
        newlib_include = ['-isystem', str(runtime16 / 'powerpc-eabi/include')]
        game_includes = ['-I' + str(SOURCE), '-I' + str(wut / 'include'), '-I' + str(third_party.curl_include),
                         '-I' + str(third_party.stb), '-I' + str(third_party.zlib),
                         '-I' + str(third_party.libpng_wiiu), '-I' + str(third_party.libpng),
                         '-I' + str(third_party.rapidxml)]
        # The semantic flags of the original game build plus the wut toolchain macros.
        self.game_flags = TARGET + [
            '-std=gnu++11', '-fshort-wchar', '-fsigned-char', '-frtti', '-fno-exceptions', '-fno-threadsafe-statics',
            '-w', '-Wno-c++11-narrowing', '-nostdinc++', '-nostdlibinc'] + SEMANTIC + game_includes + [
            '-isystem', str(third_party.boost), '-isystem', str(ROOT / 'tools/redhills/include'),
            '-isystem', str(cxx_include), '-isystem', str(cxx_include / 'powerpc-eabi'),
            '-isystem', str(cxx_include / 'backward')] + newlib_include
        self.game_c_flags = TARGET + ['-x', 'c', '-std=gnu99', '-fshort-wchar', '-fsigned-char', '-w',
                                      '-nostdlibinc'] + SEMANTIC + game_includes + newlib_include
        self.runtime_flags = TARGET + ['-std=gnu99', '-fshort-wchar', '-fsigned-char', '-w',
                                       '-nostdlibinc'] + newlib_include
        self.provider_flags = TARGET + ['-std=gnu89', '-fsigned-char', '-w', '-DNDEBUG', '-DESPRESSO', '-D__WIIU__',
                                        '-D__WUT__', '-DHAVE_UNISTD_H', '-nostdlibinc'] + newlib_include


def run(command, **kwargs):
    return subprocess.run([str(part) for part in command], capture_output=True, text=True, errors='replace',
                          **kwargs)


def depfile_inputs(path):
    """The prerequisites a -MD depfile lists (make syntax: backslash-escaped spaces, line continuations)."""
    text = path.read_text(encoding='utf-8', errors='replace').replace('\\\n', ' ')
    _, _, rest = text.partition(': ')
    names, current, index = [], '', 0
    while index < len(rest):
        char = rest[index]
        if char == '\\' and index + 1 < len(rest) and rest[index + 1] == ' ':
            current += ' '
            index += 2
            continue
        if char.isspace():
            if current:
                names.append(current)
            current = ''
        else:
            current += char
        index += 1
    if current:
        names.append(current)
    return [Path(name) for name in names]


def flags_digest(flags):
    return hashlib.sha1(repr([str(flag) for flag in flags]).encode('utf-8')).hexdigest()


def up_to_date(obj, dep, source, compiler, digest):
    if not obj.is_file() or not dep.is_file():
        return False
    try:
        if obj.with_suffix('.flags').read_text(encoding='utf-8') != digest:
            return False
    except OSError:
        return False
    built = obj.stat().st_mtime
    for path in [source, compiler] + depfile_inputs(dep):
        try:
            if path.stat().st_mtime > built:
                return False
        except OSError:
            return False
    return True


def compile_units(clang, units, flags, jobs, label):
    """units: [(source, object)]. Returns the failures as [(source, first error line)]."""
    digest = flags_digest([clang] + flags)
    stale = [(src, obj) for src, obj in units
             if not up_to_date(obj, obj.with_suffix('.d'), src, Path(clang), digest)]
    failures = []

    def compile_one(unit):
        src, obj = unit
        obj.parent.mkdir(parents=True, exist_ok=True)
        result = run([clang] + flags + ['-MD', '-MF', obj.with_suffix('.d'), '-c', src, '-o', obj])
        if result.returncode:
            if obj.exists():
                obj.unlink()
            lines = [line for line in result.stderr.splitlines() if 'error' in line.lower()]
            return src, (lines or result.stderr.strip().splitlines() or ['exit %d' % result.returncode])[0]
        obj.with_suffix('.flags').write_text(digest, encoding='utf-8')
        return None

    print('%s: %d of %d units to compile' % (label, len(stale), len(units)), flush=True)
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for done, failure in enumerate(pool.map(compile_one, stale), 1):
            if failure:
                failures.append(failure)
            if done % 250 == 0:
                print('  %d/%d compiled, %d failed' % (done, len(stale), len(failures)), flush=True)
    for src, line in failures:
        print('FAIL %s: %s' % (src, line))
    return failures


def archive(build, path, objects):
    if path.exists():
        path.unlink()
    path.parent.mkdir(parents=True, exist_ok=True)
    response = path.with_suffix('.rsp')
    response.write_text(''.join('"%s"\n' % obj.as_posix() for obj in objects), encoding='utf-8')
    result = run([build.ar, 'rcs', path, '@%s' % response])
    if result.returncode:
        raise SystemExit('archive failed: %s\n%s' % (path, result.stderr))


def nm_names(build, paths, *options):
    names = set()
    for start in range(0, len(paths), 300):
        listing = run([build.nm] + list(options) + paths[start:start + 300]).stdout
        for line in listing.splitlines():
            parts = line.split()
            if parts and not line.endswith(':'):
                names.add(parts[-1])
    return names


def strong_undefined(build, elf):
    """Undefined names an image still references, less weak references (which resolve to 0)."""
    listing = run([build.nm, '--undefined-only', elf]).stdout
    return {parts[-1] for parts in (line.split() for line in listing.splitlines())
            if len(parts) == 2 and parts[0] == 'U'}


def write_if_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_text(encoding='utf-8') != text:
        path.write_text(text, encoding='utf-8')


def build_providers(build, out, jobs):
    """liblegacy_png.a and libquazal_zlib.a: libpng and NEX's private zlib, as the retail image links them."""
    third_party = build.third_party
    zlib, png = third_party.zlib, third_party.libpng
    includes = ['-I' + str(third_party.libpng_wiiu), '-I' + str(png), '-I' + str(zlib)]
    png_units = [(png / name, out / 'png' / (name[:-2] + '.o')) for name in PNG_SOURCES]
    png_units.append((third_party.libpng_wiiu / 'pngwrite.c', out / 'png/pngwrite.o'))
    plain_units = [(zlib / name, out / 'zlib' / (name[:-2] + '.o')) for name in ZLIB_SOURCES]
    if compile_units(build.clang, png_units + plain_units, build.provider_flags + includes, jobs, 'providers'):
        raise SystemExit('provider compile failed')
    archive(build, out / 'liblegacy_png.a', [obj for _, obj in png_units])
    names = sorted(name for name in nm_names(build, [obj for _, obj in plain_units], '--defined-only',
                                             '--extern-only')
                   if re.match(r'^[A-Za-z_]\w*$', name))
    prefix = out / 'quazal_zlib/quazal_prefix.h'
    write_if_changed(prefix, ''.join('#define %s quazal_%s\n' % (name, name) for name in names))
    quazal_units = [(zlib / name, out / 'quazal_zlib' / (name[:-2] + '.o')) for name in ZLIB_SOURCES]
    if compile_units(build.clang, quazal_units, build.provider_flags + includes + ['-include', str(prefix)], jobs,
                     'quazal zlib'):
        raise SystemExit('provider compile failed')
    archive(build, out / 'libquazal_zlib.a', [obj for _, obj in quazal_units])
    return [out / 'liblegacy_png.a', out / 'libquazal_zlib.a']


def crt0_source():
    """tools/rpx/cafe_crt0.ppc in the GNU assembler syntax RedHills reads: %hiadj/%lo operators become
    @ha/@l, '$' becomes '.', registers in instructions are %rN (sp is %r1), and the crt0's Green Hills
    runtime names are the RedHills ones."""
    text = CRT0.read_text(encoding='utf-8')
    text = re.sub(r'%hiadj\(([^()]+)\)', r'\1@ha', text)
    text = re.sub(r'%lo\(([^()]+)\)', r'\1@l', text)
    text = text.replace('$-_start', '.-_start')
    lines = []
    for line in text.split('\n'):
        if re.match(r'\t[a-z]', line):
            line = re.sub(r'\bsp\b', '%r1', line)
            line = re.sub(r'(?<![%\w])r(\d+)\b', r'%r\1', line)
        lines.append(line)
    text = '\n'.join(lines)
    text = text.replace('.section ".bss","awb"', '.section ".bss","aw",@nobits')
    for old, new in CRT0_RENAMES.items():
        text = re.sub(r'\b%s\b' % re.escape(old), new, text)
    return text


def build_runtime(build, out, jobs):
    """The crt objects: cafe_crt0, the host-syscall entry, _main and newlib's system interface."""
    crt0 = out / 'cafe_crt0.S'
    write_if_changed(crt0, crt0_source())
    units = [(crt0, out / 'cafe_crt0.o'), (RUNTIME / 'ghs_syscall.S', out / 'ghs_syscall.o'),
             (RUNTIME / 'cafe_start.c', out / 'cafe_start.o'), (RUNTIME / 'cafe_newlib.c', out / 'cafe_newlib.o')]
    if compile_units(build.clang, units, build.runtime_flags, jobs, 'runtime'):
        raise SystemExit('runtime compile failed')
    return [obj for _, obj in units]


def game_units(out):
    game, c_units, edg = [], [], []
    for src in sorted(SOURCE.rglob('*.cpp')):
        rel = src.relative_to(SOURCE).as_posix()
        if rel in GCC_ONLY_UNITS or (OTHER_PLATFORMS.match(rel) and rel not in SHARED_PLATFORM_UNITS):
            continue
        unit = (src, out / 'obj' / Path(rel).with_suffix('.o'))
        if rel in C_UNITS:
            c_units.append(unit)
        elif EDG_RUNTIME.match(rel):
            edg.append(unit)
        else:
            game.append(unit)
    return game, c_units, edg


def libgcc_path(args):
    if args.libgcc:
        path = args.libgcc
    else:
        if not args.devkitpro or not args.devkitpro.is_dir():
            raise SystemExit('devkitPro not found: pass --devkitpro, set DEVKITPRO, or pass --libgcc')
        path = args.devkitpro / 'devkitPPC/lib/gcc/powerpc-eabi' / GCC_VERSION / 'libgcc.a'
    if not path.is_file():
        raise SystemExit('libgcc.a missing: install devkitPPC with GCC %s (%s)' % (GCC_VERSION, path))
    return path


def coreinit_c_runtime(build, libraries):
    """Names the image imports from an RPL although newlib defines them too (memcpy, exit, environ, ...)."""
    defined = nm_names(build, [str(path) for path in libraries], '--defined-only', '--extern-only')
    return sorted(name for name in imports.import_map() if name in defined)


def write_linker_script(path, import_sections):
    lines = ''.join('\t%s ALIGN(16) : {\n\t\tKEEP(*(%s))\n\t}\n\n' % (name, name) for name in import_sections)
    text = LINKER_SCRIPT.read_text(encoding='utf-8').replace('\t/* IMPORT_SECTIONS */\n', lines)
    write_if_changed(path, text)


def import_sections(stubs):
    import elf2rpx
    names = []
    for stub in stubs:
        _, sections = elf2rpx.read_elf(stub)
        names += [section.name for section in sections
                  if section.name.startswith(('.fimport_', '.dimport_')) and section.size]
    return names


def run_lld(build, inputs, script, elf, log, allow_undefined):
    response = elf.with_suffix('.rsp')
    response.write_text(''.join('"%s"\n' % Path(path).as_posix() for path in inputs), encoding='utf-8')
    command = [build.lld, '-T', script, '-static', '--gc-sections', '--emit-relocs',
               '--no-demangle', '-u', '__preinit_user', '-Map', elf.with_suffix('.map'), '-o', elf,
               '@%s' % response]
    if allow_undefined:
        command.append('--unresolved-symbols=ignore-all')
    else:
        command.append('--error-limit=0')
    result = run(command)
    log.write_text(' '.join(str(part) for part in command) + '\n\n' + result.stdout + result.stderr,
                   encoding='utf-8')
    return result


def link(build, objects, archives, out):
    """Pass 1 links with undefined symbols allowed to learn the imported names; pass 2 links their stubs."""
    preferred = coreinit_c_runtime(build, [path for path in archives if path.name in ('libc.a', 'libm.a')])
    script = out / 'cafe.ld'
    write_linker_script(script, [])
    seed, _, _, _ = imports.generate(set(preferred), out / 'imports-seed')
    probe = out / 'pass1.elf'
    result = run_lld(build, objects + seed + archives, script, probe, out / 'link-pass1.log', True)
    if result.returncode or not probe.is_file():
        print('pass 1 failed; see %s' % (out / 'link-pass1.log'))
        print(result.stderr, file=sys.stderr, end='')
        return None
    undefined = strong_undefined(build, probe) | set(preferred)
    stubs, chosen, unresolved, aliases = imports.generate(undefined, out / 'imports')
    (out / 'renames.json').write_text(json.dumps(aliases, indent=1, sort_keys=True), encoding='utf-8')
    (out / 'imports.json').write_text(json.dumps(chosen, indent=1, sort_keys=True), encoding='utf-8')
    write_linker_script(script, import_sections(stubs))
    elf = out / 'minecraft.elf'
    result = run_lld(build, objects + stubs + archives, script, elf, out / 'link-pass2.log', False)
    missing = sorted(set(re.findall(r'undefined symbol: (\S+)', result.stderr)) | set(unresolved))
    (out / 'unresolved.txt').write_text(''.join(name + '\n' for name in missing), encoding='utf-8')
    if result.returncode or not elf.is_file():
        print('pass 2 failed: %d unresolved (%s); see %s'
              % (len(missing), out / 'unresolved.txt', out / 'link-pass2.log'))
        if missing:
            print('Unresolved symbols:\n' + '\n'.join(missing), file=sys.stderr)
        print(result.stderr, file=sys.stderr, end='')
        return None
    print('imports %d from %d RPLs' % (len(chosen), len(set(chosen.values()))))
    return elf


def build_library(build, args, name, script, extra):
    """libstdc++.a or libm.a under <out>/<name>, rebuilt when the compiler or its build script is newer than it."""
    library = args.out / name / (name + '.a')
    builder = ROOT / 'tools/redhills' / script
    if library.is_file() and library.stat().st_mtime >= max(args.redhills.stat().st_mtime,
                                                            builder.stat().st_mtime):
        return library
    result = subprocess.run([sys.executable, str(ROOT / 'tools/redhills' / script), '--redhills', str(args.redhills),
                             '--runtime16', str(build.runtime16), '--sources', str(args.sources),
                             '--out', str(library.parent), '-j', str(args.jobs)] + extra)
    if result.returncode or not library.is_file():
        raise SystemExit('%s build failed' % name)
    return library


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--redhills', type=Path, default=os.environ.get('REDHILLS'),
                        help='the RedHills clang executable (default $REDHILLS)')
    parser.add_argument('--devkitpro', type=Path, default=os.environ.get('DEVKITPRO'),
                        help='the devkitPro root with devkitPPC, for libgcc (default $DEVKITPRO)')
    parser.add_argument('--libgcc', type=Path, help="devkitPPC's libgcc.a, in place of --devkitpro")
    parser.add_argument('--wut', type=Path, default=ROOT / 'third_party/wut')
    parser.add_argument('--runtime16', type=Path, default=toolchain.default_runtime16())
    parser.add_argument('--third-party', type=Path, help='the third-party sources (see the description)')
    parser.add_argument('--sources', type=Path, default=sources.DEFAULT_CACHE)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/redhills')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--install', type=Path, nargs='?', const=PACKAGE,
                        help="copy the RPX into this Cemu update package's code/ folder")
    parser.add_argument('--keep-going', action='store_true',
                        help='diagnostics only: link the units that compiled and stop before the RPX')
    args = parser.parse_args()
    if not args.redhills or not args.redhills.is_file():
        sys.exit('RedHills clang not found: pass --redhills or set REDHILLS')
    if not (args.runtime16 / 'powerpc-eabi/lib/libc.a').is_file():
        sys.exit('two-byte wchar_t runtime missing at %s: build it with tools/redhills/build_runtime16.py'
                 % args.runtime16)
    if not (args.wut / 'include/wut.h').is_file():
        sys.exit('wut not found at %s: pass --wut' % args.wut)
    third_party = ThirdParty.from_directory(args.third_party) if args.third_party else ThirdParty.reconstruction_tree()
    missing = third_party.missing()
    if missing:
        sys.exit('third-party sources missing: %s' % ', '.join(str(path) for path in missing))
    imports.set_wut(args.wut)
    libgcc = libgcc_path(args)
    build = Build(args.redhills, args.wut, args.runtime16, third_party)
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    game, c_units, edg = game_units(out)
    failed = compile_units(args.redhills, game + edg, build.game_flags, args.jobs, 'game')
    failed += compile_units(args.redhills, c_units, build.game_c_flags, args.jobs, 'game C')
    if failed and not args.keep_going:
        return 1
    failed_sources = {src for src, _ in failed}
    game = [unit for unit in game if unit[0] not in failed_sources]
    edg = [unit for unit in edg if unit[0] not in failed_sources]
    c_units = [unit for unit in c_units if unit[0] not in failed_sources]
    libsedg = out / 'libsedg.a'
    archive(build, libsedg, [obj for _, obj in edg])
    providers = build_providers(build, out / 'providers', args.jobs)
    runtime = build_runtime(build, out / 'runtime', args.jobs)
    libstdcxx = build_library(build, args, 'libstdc++', 'build_libstdcxx.py', [])
    libm = build_library(build, args, 'libm', 'build_libm.py', [])

    objects = runtime + [obj for _, obj in game + c_units]
    archives = providers + [libstdcxx, libsedg, libm, args.runtime16 / 'powerpc-eabi/lib/libc.a', libgcc]
    elf = link(build, objects, archives, out)
    if elf is None or failed:
        return 1
    scan = subprocess.run([sys.executable, str(ROOT / 'tools/redhills/crscan.py'), str(elf), '--show', '20'])
    if scan.returncode:
        print('the image has code stock Cemu mishandles (tools/redhills/crscan.py); the compiler needs fixing')
        return 1
    rpx = out / 'Minecraft.Client.rpx'
    result = subprocess.run([sys.executable, str(HERE / 'elf2rpx.py'), str(elf), str(rpx),
                             '--renames', str(out / 'renames.json')])
    if result.returncode or not rpx.is_file():
        print('elf2rpx failed')
        return 1
    if args.install:
        shutil.copyfile(rpx, args.install / 'Minecraft.Client.rpx')
        print('installed', args.install / 'Minecraft.Client.rpx')
    print(rpx)
    return 0


if __name__ == '__main__':
    sys.exit(main())
