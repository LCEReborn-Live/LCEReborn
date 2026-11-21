"""The C and C++ runtime sources the RedHills build compiles, fetched from their releases and pinned.

The game links newlib and libstdc++ as devkitPro configures them for powerpc-eabi: newlib 4.6.0.20260123
and GCC 16.1.0, each with devkitPro's patch from devkitPro/buildscripts. Every download is checked
against its SHA-256 before use. Each tree is extracted and patched once into the cache directory; a
marker file records a finished tree, so an interrupted run starts that tree again.

    python tools/redhills/sources.py [--cache DIR] [newlib] [gcc] [libstdc++]

prints the prepared trees (all three by default).
"""
import argparse
import hashlib
from http.client import HTTPException
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CACHE = ROOT / 'build/redhills-sources'
BUILDSCRIPTS = 'https://raw.githubusercontent.com/devkitPro/buildscripts/9b57a022648cbd971c2df3652671cc9deaa2482a'

ARCHIVES = {
    'newlib': ('https://sourceware.org/pub/newlib/newlib-4.6.0.20260123.tar.gz',
               '6ff27e3bf022666f43f7802255be680eeff722ac181b1725d21e2e8318604ee3'),
    'gcc': ('https://gcc.gnu.org/pub/gcc/releases/gcc-16.1.0/gcc-16.1.0.tar.xz',
            '50efb4d94c3397aff3b0d61a5abd748b4dd31d9d3f2ab7be05b171d36a510f79'),
}
MIRRORS = {
    'gcc-16.1.0.tar.xz': (
        'https://ftp.gnu.org/gnu/gcc/gcc-16.1.0/gcc-16.1.0.tar.xz',
        'https://ftpmirror.gnu.org/gcc/gcc-16.1.0/gcc-16.1.0.tar.xz',
    ),
}
PATCHES = {
    'newlib': (BUILDSCRIPTS + '/patches/newlib-4.6.0.20260123-4.patch',
               '6d5aba4356553c727765c98e7553d3784f31d361c856e220ef27cc26353fb88f'),
    'gcc': (BUILDSCRIPTS + '/patches/gcc-16.1.0-1.patch',
            '7b39aeadf373327bf6671e1bc3596c864eeb38a92a0e3dd553ddd7f385869981'),
}
TREES = {'newlib': 'newlib-4.6.0.20260123', 'gcc': 'gcc-16.1.0'}
MARKER = '.prepared'


def download(url, sha256, cache):
    """The bytes at url, cached under cache/downloads and checked against sha256."""
    path = cache / 'downloads' / url.rsplit('/', 1)[-1]
    if path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == sha256:
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    for source in (url,) + MIRRORS.get(path.name, ()):
        print('downloading %s' % source, flush=True)
        request = urllib.request.Request(source, headers={'User-Agent': 'mcwiiu-reconstructed-build'})
        try:
            with urllib.request.urlopen(request, timeout=45) as response:
                data = response.read()
        except (OSError, HTTPException) as error:
            print('%s: %s' % (source, error), file=sys.stderr, flush=True)
        else:
            break
    else:
        raise SystemExit('could not download %s from any configured source' % path.name)
    actual = hashlib.sha256(data).hexdigest()
    if actual != sha256:
        raise SystemExit('%s: SHA-256 %s, expected %s' % (source, actual, sha256))
    partial = path.with_name(path.name + '.partial')
    partial.write_bytes(data)
    partial.replace(path)
    return path


def apply_patch(tree, patch):
    tool = shutil.which('patch')
    if tool is None:
        raise SystemExit('patch is not on PATH (Git for Windows ships one in usr/bin)')
    result = subprocess.run([tool, '-p1', '-s', '-i', str(patch)], cwd=tree, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit('patching %s failed:\n%s%s' % (tree, result.stdout, result.stderr))


def prepare(name, cache=DEFAULT_CACHE, members=None):
    """The extracted and patched tree for 'newlib' or 'gcc'. members, a path prefix inside the archive's
    top directory (such as 'libstdc++-v3/'), extracts only that part; the full tree replaces it later
    when it is asked for."""
    cache = Path(cache)
    tree = cache / TREES[name]
    marker = tree / MARKER
    scope = members or ''
    if marker.is_file() and marker.read_text(encoding='utf-8') in ('', scope):
        return tree
    archive = download(*ARCHIVES[name], cache)
    patch = download(*PATCHES[name], cache)
    if tree.exists():
        shutil.rmtree(tree)
    prefix = TREES[name] + '/' + scope
    print('extracting %s%s' % (TREES[name], ' (%s)' % scope if scope else ''), flush=True)
    with tarfile.open(archive) as stream:
        selected = [member for member in stream.getmembers() if member.name.startswith(prefix)]
        if hasattr(tarfile, 'tar_filter'):
            stream.extractall(cache, members=selected, filter='tar')
        else:
            stream.extractall(cache, members=selected)
    if not scope:
        apply_patch(tree, patch)
    else:
        apply_patch_subset(tree, patch, scope)
    marker.write_text(scope, encoding='utf-8')
    return tree


def apply_patch_subset(tree, patch, scope):
    """Apply only the hunks of patch for files under scope, so a partial tree takes its part of the patch."""
    kept, keep = [], False
    for line in patch.read_text(encoding='utf-8', errors='surrogateescape').splitlines(keepends=True):
        if line.startswith('diff '):
            keep = ' a/%s' % scope in line
        if keep:
            kept.append(line)
    if kept:
        subset = tree / '.subset.patch'
        subset.write_text(''.join(kept), encoding='utf-8', errors='surrogateescape')
        apply_patch(tree, subset)
        subset.unlink()


def newlib_tree(cache=DEFAULT_CACHE):
    return prepare('newlib', cache)


def gcc_tree(cache=DEFAULT_CACHE):
    return prepare('gcc', cache)


def libstdcxx_tree(cache=DEFAULT_CACHE):
    """libstdc++-v3 alone, unless the full GCC tree is already prepared."""
    full = Path(cache) / TREES['gcc'] / MARKER
    if full.is_file() and full.read_text(encoding='utf-8') == '':
        return Path(cache) / TREES['gcc'] / 'libstdc++-v3'
    return prepare('gcc', cache, 'libstdc++-v3/') / 'libstdc++-v3'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--cache', type=Path, default=DEFAULT_CACHE)
    parser.add_argument('trees', nargs='*', choices=['newlib', 'gcc', 'libstdc++'], default=[])
    args = parser.parse_args()
    for name in args.trees or ['newlib', 'gcc', 'libstdc++']:
        tree = {'newlib': newlib_tree, 'gcc': gcc_tree, 'libstdc++': libstdcxx_tree}[name](args.cache)
        print(tree)


if __name__ == '__main__':
    main()
