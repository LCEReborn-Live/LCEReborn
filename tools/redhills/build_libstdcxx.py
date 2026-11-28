"""Build the compiled part of libstdc++ with RedHills, for the Green Hills ABI build of the game.

The C++ library headers come from the two-byte-wchar_t runtime (tools/redhills/build_runtime16.py,
GCC 16.1.0 configured for newlib); its archive was compiled by GCC
with the Itanium class layout, so the hosted sources are rebuilt here with the same configuration,
following libstdc++-v3/src/c++98 and src/c++11 Makefile.am (per-directory -std, the COW-string ABI
variants, -fimplicit-templates where the Makefiles ask for it), the generic locale model and the
newlib OS sources configure selects for powerpc-eabi. The EDG runtime has no sized operator delete, so the C++14 and later
units are built without sized deallocation. RTTI support, operator new/delete and the
type_info class come from the EDG runtime (source/cafe/libsedg); of libsupc++ only the exception classes,
byte hashing, the new handler and the nothrow operator new are built; std::uncaught_exception, which libsupc++ reads from
the Itanium unwinder's globals, comes from tools/redhills/runtime (no exception is ever in flight). The string ABI default comes from c++config.h, as in GCC's build, so units such as locale-inst.cc
that select the COW ABI when nothing else did still do.

The sources are GCC's libstdc++-v3 at the 16.1.0 release with devkitPro's patch, which
tools/redhills/sources.py downloads and prepares when --src is not given.

    python tools/redhills/build_libstdcxx.py --redhills CLANG --runtime16 DIR [--src LIBSTDCXX_TREE]
                                              [--out DIR] [-j N]
"""
import argparse
import concurrent.futures
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sources  # noqa: E402
import toolchain  # noqa: E402

DEFAULT_RUNTIME16 = toolchain.default_runtime16()


def flags(runtime16):
    cxx_include = runtime16 / 'include/c++/16.1.0'
    return ['-target', 'powerpc-eabi', '-mcpu=750', '-O2', '-fshort-wchar', '-fsigned-char', '-frtti',
            '-fno-exceptions', '-fno-sized-deallocation', '-ffunction-sections', '-fdata-sections', '-w',
            '-nostdinc++', '-nostdlibinc',
            '-isystem', str(ROOT / 'tools/redhills/include'),
            '-isystem', str(cxx_include),
            '-isystem', str(cxx_include / 'powerpc-eabi'),
            '-isystem', str(cxx_include / 'backward'),
            '-isystem', str(runtime16 / 'powerpc-eabi/include')]

COW = ['-D_GLIBCXX_USE_CXX11_ABI=0']
IMPLICIT = ['-fimplicit-templates']
CHAR8 = ['-fchar8_t']
# libsupc++ builds with the compiler's default standard, C++20 for GCC 16.
SUPC = 'gnu++20'
# The ios::failure units' __cpp_rtti blocks build Itanium __class_type_info objects so a handler for the
# other string ABI's ios::failure can catch it; the Green Hills RTTI has no such classes and the game
# builds without exceptions, so those units are compiled without the feature macro and without
# <cxxabi.h>, whose classes derive from the Itanium type_info (their own classes keep their type_info).
NO_ITANIUM_RTTI = ['-U__cpp_rtti', '-D_CXXABI_H']
# Compiled as a system header so its init_priority below 101 (the reserved range the library uses to
# construct the standard streams before any other static object) is accepted.
SYSTEM = ['SYSTEM']

# (object name, source relative to libstdc++-v3, -std, extra flags)
C98 = 'src/c++98/'
C11 = 'src/c++11/'
GENERIC = 'config/locale/generic/'
UNITS = [
    ('bitmap_allocator', C98 + 'bitmap_allocator.cc', 'gnu++98', []),
    ('pool_allocator', C98 + 'pool_allocator.cc', 'gnu++98', []),
    ('mt_allocator', C98 + 'mt_allocator.cc', 'gnu++98', []),
    ('codecvt98', C98 + 'codecvt.cc', 'gnu++98', []),
    ('complex_io', C98 + 'complex_io.cc', 'gnu++98', []),
    ('globals_io', C98 + 'globals_io.cc', 'gnu++98', SYSTEM),
    ('hash_tr1', C98 + 'hash_tr1.cc', 'gnu++98', []),
    ('hashtable_tr1', C98 + 'hashtable_tr1.cc', 'gnu++98', []),
    ('ios_failure', C98 + 'ios_failure.cc', 'gnu++98', NO_ITANIUM_RTTI),
    ('ios_init', C98 + 'ios_init.cc', 'gnu++98', []),
    ('ios_locale', C98 + 'ios_locale.cc', 'gnu++98', []),
    ('list', C98 + 'list.cc', 'gnu++98', []),
    ('list-aux', C98 + 'list-aux.cc', 'gnu++98', []),
    ('list-aux-2', C98 + 'list-aux-2.cc', 'gnu++98', []),
    ('list_associated', C98 + 'list_associated.cc', 'gnu++98', []),
    ('list_associated-2', C98 + 'list_associated-2.cc', 'gnu++98', []),
    ('locale', C98 + 'locale.cc', 'gnu++98', []),
    ('locale_facets', C98 + 'locale_facets.cc', 'gnu++98', []),
    ('math_stubs_float', C98 + 'math_stubs_float.cc', 'gnu++98', []),
    ('math_stubs_long_double', C98 + 'math_stubs_long_double.cc', 'gnu++98', []),
    ('stdexcept', C98 + 'stdexcept.cc', 'gnu++98', []),
    ('tree', C98 + 'tree.cc', 'gnu++98', []),
    ('istream', C98 + 'istream.cc', 'gnu++98', []),
    ('istream-string', C98 + 'istream-string.cc', 'gnu++98', []),
    ('streambuf', C98 + 'streambuf.cc', 'gnu++98', []),
    ('valarray', C98 + 'valarray.cc', 'gnu++98', []),
    ('cow-istream-string', C98 + 'cow-istream-string.cc', 'gnu++98', []),
    ('allocator-inst', C98 + 'allocator-inst.cc', 'gnu++98', []),
    ('ext-inst', C98 + 'ext-inst.cc', 'gnu++98', []),
    ('misc-inst', C98 + 'misc-inst.cc', 'gnu++98', []),
    ('c++locale', GENERIC + 'c_locale.cc', 'gnu++98', IMPLICIT),
    ('codecvt_members', GENERIC + 'codecvt_members.cc', 'gnu++98', []),
    ('collate_members', GENERIC + 'collate_members.cc', 'gnu++98', []),
    ('messages_members', GENERIC + 'messages_members.cc', 'gnu++98', []),
    ('monetary_members', GENERIC + 'monetary_members.cc', 'gnu++98', []),
    ('numeric_members', GENERIC + 'numeric_members.cc', 'gnu++98', []),
    ('time_members', GENERIC + 'time_members.cc', 'gnu++98', []),
    ('collate_members_cow', GENERIC + 'collate_members.cc', 'gnu++98', COW + IMPLICIT),
    ('messages_members_cow', GENERIC + 'messages_members.cc', 'gnu++98', COW + IMPLICIT),
    ('monetary_members_cow', GENERIC + 'monetary_members.cc', 'gnu++98', COW + IMPLICIT),
    ('numeric_members_cow', GENERIC + 'numeric_members.cc', 'gnu++98', COW + IMPLICIT),
    ('limits', C11 + 'limits.cc', 'gnu++11', CHAR8),
    ('placeholders', C11 + 'placeholders.cc', 'gnu++11', []),
    ('assert_fail', C11 + 'assert_fail.cc', 'gnu++11', []),
    ('chrono', C11 + 'chrono.cc', 'gnu++11', []),
    ('codecvt', C11 + 'codecvt.cc', 'gnu++11', CHAR8),
    ('cow-stdexcept', C11 + 'cow-stdexcept.cc', 'gnu++11', []),
    ('cow-system_error', C11 + 'cow-system_error.cc', 'gnu++11', []),
    ('ctype', C11 + 'ctype.cc', 'gnu++11', []),
    ('functexcept', C11 + 'functexcept.cc', 'gnu++11', []),
    ('functional', C11 + 'functional.cc', 'gnu++11', []),
    ('hash_c++0x', C11 + 'hash_c++0x.cc', 'gnu++11', []),
    ('hashtable_c++0x', C11 + 'hashtable_c++0x.cc', 'gnu++11', IMPLICIT),
    ('ios', C11 + 'ios.cc', 'gnu++11', []),
    ('ios_errcat', C11 + 'ios_errcat.cc', 'gnu++11', []),
    ('locale_init', C11 + 'locale_init.cc', 'gnu++11', CHAR8),
    ('localename', C11 + 'localename.cc', 'gnu++11', CHAR8),
    ('random', C11 + 'random.cc', 'gnu++11', []),
    ('shared_ptr', C11 + 'shared_ptr.cc', 'gnu++11', []),
    ('snprintf_lite', C11 + 'snprintf_lite.cc', 'gnu++11', []),
    ('system_error', C11 + 'system_error.cc', 'gnu++11', []),
    ('cow-locale_init', C11 + 'cow-locale_init.cc', 'gnu++11', []),
    ('cow-shim_facets', C11 + 'cow-shim_facets.cc', 'gnu++11', []),
    ('cxx11-hash_tr1', C11 + 'cxx11-hash_tr1.cc', 'gnu++11', []),
    ('cxx11-ios_failure', C11 + 'cxx11-ios_failure.cc', 'gnu++11', NO_ITANIUM_RTTI),
    ('cxx11-shim_facets', C11 + 'cxx11-shim_facets.cc', 'gnu++11', []),
    ('cxx11-stdexcept', C11 + 'cxx11-stdexcept.cc', 'gnu++11', []),
    ('ctype_configure_char', 'config/os/newlib/ctype_configure_char.cc', 'gnu++11', []),
    ('ctype_members', GENERIC + 'ctype_members.cc', 'gnu++11', []),
    ('cow-sstream-inst', C11 + 'cow-sstream-inst.cc', 'gnu++11', []),
    ('cow-string-inst', C11 + 'cow-string-inst.cc', 'gnu++11', []),
    ('cow-string-io-inst', C11 + 'cow-string-io-inst.cc', 'gnu++11', []),
    ('cow-wstring-inst', C11 + 'cow-wstring-inst.cc', 'gnu++11', []),
    ('cow-wstring-io-inst', C11 + 'cow-wstring-io-inst.cc', 'gnu++11', []),
    ('cxx11-locale-inst', C11 + 'cxx11-locale-inst.cc', 'gnu++11', []),
    ('cxx11-wlocale-inst', C11 + 'cxx11-wlocale-inst.cc', 'gnu++11', []),
    ('sso_string', C11 + 'sso_string.cc', 'gnu++11', []),
    ('ext11-inst', C11 + 'ext11-inst.cc', 'gnu++11', []),
    ('ios-inst', C11 + 'ios-inst.cc', 'gnu++11', []),
    ('iostream-inst', C11 + 'iostream-inst.cc', 'gnu++11', []),
    ('istream-inst', C11 + 'istream-inst.cc', 'gnu++11', []),
    ('locale-inst', C11 + 'locale-inst.cc', 'gnu++11', []),
    ('ostream-inst', C11 + 'ostream-inst.cc', 'gnu++11', []),
    ('sstream-inst', C11 + 'sstream-inst.cc', 'gnu++11', []),
    ('streambuf-inst', C11 + 'streambuf-inst.cc', 'gnu++11', []),
    ('string-inst', C11 + 'string-inst.cc', 'gnu++11', []),
    ('string-io-inst', C11 + 'string-io-inst.cc', 'gnu++11', []),
    ('wlocale-inst', C11 + 'wlocale-inst.cc', 'gnu++11', []),
    ('wstring-inst', C11 + 'wstring-inst.cc', 'gnu++11', []),
    ('wstring-io-inst', C11 + 'wstring-io-inst.cc', 'gnu++11', []),
    ('eh_exception', 'libsupc++/eh_exception.cc', SUPC, []),
    ('hash_bytes', 'libsupc++/hash_bytes.cc', SUPC, []),
    ('bad_alloc', 'libsupc++/bad_alloc.cc', SUPC, []),
    ('bad_array_length', 'libsupc++/bad_array_length.cc', SUPC, []),
    ('bad_array_new', 'libsupc++/bad_array_new.cc', SUPC, []),
    ('new_handler', 'libsupc++/new_handler.cc', SUPC, []),
    ('new_opnt', 'libsupc++/new_opnt.cc', SUPC, []),
    ('new_opvnt', 'libsupc++/new_opvnt.cc', SUPC, []),
    ('uncaught_exception', str(ROOT / 'tools/redhills/runtime/uncaught_exception.cpp'), SUPC, []),
]


def compile_unit(clang, src_root, out, runtime16, unit):
    name, source, std, extra = unit
    obj = out / 'obj' / (name + '.o')
    path = src_root / source
    if extra == SYSTEM:
        wrapper = out / 'wrap' / (name + '.cc')
        wrapper.parent.mkdir(parents=True, exist_ok=True)
        wrapper.write_text('#include <%s>\n' % path.name, encoding='utf-8')
        extra, path = ['-isystem', str(path.parent)], wrapper
    command = [str(clang), '-std=' + std] + flags(runtime16) + extra + [
        '-I' + str((src_root / source).parent), '-c', str(path), '-o', str(obj)]
    proc = subprocess.run(command, capture_output=True, text=True, errors='replace')
    return name, proc.returncode, proc.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--redhills', type=Path, default=os.environ.get('REDHILLS'),
                        help='the RedHills clang executable (default $REDHILLS)')
    parser.add_argument('--runtime16', type=Path, default=DEFAULT_RUNTIME16,
                        help='the two-byte wchar_t runtime (tools/redhills/build_runtime16.py)')
    parser.add_argument('--src', type=Path, help='a prepared libstdc++-v3 tree (default: tools/redhills/sources.py)')
    parser.add_argument('--sources', type=Path, default=sources.DEFAULT_CACHE,
                        help='the download and extraction cache of tools/redhills/sources.py')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/redhills/libstdc++')
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()
    if not args.redhills or not args.redhills.is_file():
        sys.exit('RedHills clang not found: pass --redhills or set REDHILLS')
    if not (args.runtime16 / 'include/c++/16.1.0').is_dir():
        sys.exit('two-byte wchar_t runtime not found at %s (tools/redhills/build_runtime16.py)' % args.runtime16)
    src = args.src or sources.libstdcxx_tree(args.sources)
    if not (src / 'src/c++11/string-inst.cc').is_file():
        sys.exit('libstdc++-v3 sources not found at %s' % src)
    (args.out / 'obj').mkdir(parents=True, exist_ok=True)
    failures = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for name, rc, err in pool.map(lambda u: compile_unit(args.redhills, src, args.out, args.runtime16, u),
                                      UNITS):
            if rc:
                failures.append((name, err))
    for name, err in failures:
        first = next((l for l in err.splitlines() if 'error' in l), err.strip()[:200])
        print('FAIL %-24s %s' % (name, first))
    archive = args.out / 'libstdc++.a'
    if archive.exists():
        archive.unlink()
    objects = sorted(str(p) for p in (args.out / 'obj').glob('*.o'))
    subprocess.run([str(toolchain.llvm_tool('llvm-ar', args.redhills)), 'rcs', str(archive)] + objects, check=True)
    print('%d of %d units compiled -> %s' % (len(UNITS) - len(failures), len(UNITS), archive))
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
