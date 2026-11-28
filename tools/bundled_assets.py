"""Record and verify the prepared game content shipped in release packages."""
import argparse
import hashlib
import json
from pathlib import Path

MANIFEST = '.bundle-manifest.json'
REQUIRED = ('Common/Media/MediaWiiU.arc', 'WiiU', 'music')


def inventory(root):
    root = Path(root)
    for name in REQUIRED:
        if not (root / name).exists():
            raise ValueError('Missing game content: ' + name)
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('Unexpected asset symlink: ' + str(path))
        if not path.is_file() or path.name == MANIFEST:
            continue
        digest = hashlib.sha256()
        with path.open('rb') as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b''):
                digest.update(chunk)
        result[path.relative_to(root).as_posix()] = {
            'bytes': path.stat().st_size, 'sha256': digest.hexdigest()}
    return result


def write_manifest(root):
    root = Path(root)
    files = inventory(root)
    (root / MANIFEST).write_text(json.dumps({'version': 1, 'files': files}, indent=2) + '\n',
                               encoding='utf-8', newline='\n')
    return files


def verify(root):
    root = Path(root)
    manifest = json.loads((root / MANIFEST).read_text(encoding='utf-8'))
    if manifest.get('version') != 1 or not isinstance(manifest.get('files'), dict):
        raise ValueError('Unsupported bundled asset manifest')
    actual = inventory(root)
    expected = manifest['files']
    if actual != expected:
        changed = sorted(name for name in actual.keys() | expected.keys()
                         if actual.get(name) != expected.get(name))
        raise ValueError('Asset checksum or file list mismatch: ' + ', '.join(changed[:8]))
    return [(str(root / name), 'assets/' + name) for name in sorted(actual)] + [
        (str(root / MANIFEST), 'assets/' + MANIFEST)]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=Path(__file__).resolve().parents[1] / 'assets')
    parser.add_argument('--write-manifest', action='store_true')
    args = parser.parse_args()
    if args.write_manifest:
        write_manifest(args.assets)
    files = verify(args.assets)
    print('Verified %d bundled game files' % len(files))
