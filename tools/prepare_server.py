#!/usr/bin/env python3
"""Download game files and prepare dedicated-server assets automatically.

Use --base and --update to import extracted folders instead of downloading.
Only Python's standard library is needed. No launcher, Qt or shader generation.
"""
import argparse
from contextlib import contextmanager
import json
import os
import sys
import tempfile
import time
import zipfile

import prepare
from server_download import (DownloadError, download_archive, unpack_archive, filesystem_path,
                             within, validate_source, enough_space)


def assets_ready(output):
    if not os.path.isfile(os.path.join(output, 'Common', 'Media', 'MediaWiiU.arc')):
        return False
    return all(os.path.isdir(os.path.join(output, name)) for name in ('music', 'WiiU'))


def process_alive(pid):
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            return ctypes.get_last_error() != 87
        try:
            code = wintypes.DWORD()
            return not kernel.GetExitCodeProcess(handle, ctypes.byref(code)) or code.value == 259
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


@contextmanager
def setup_lock(cache):
    os.makedirs(cache, exist_ok=True)
    lock = os.path.join(cache, 'prepare.lock')
    for attempt in range(2):
        try:
            descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            break
        except FileExistsError:
            try:
                with open(lock, encoding='utf-8') as handle:
                    pid = json.load(handle)['pid']
            except (OSError, ValueError, KeyError):
                raise DownloadError('Another server preparation may already be running.')
            if not isinstance(pid, int) or pid <= 0 or process_alive(pid) or attempt:
                raise DownloadError('Another server preparation is already running.')
            os.unlink(lock)
    try:
        with os.fdopen(descriptor, 'w') as handle:
            json.dump({'pid': os.getpid()}, handle)
        yield
    finally:
        os.unlink(lock)


def auto_dumps(manifest_path, cache):
    with open(manifest_path, encoding='utf-8') as handle:
        manifest = json.load(handle)
    if manifest.get('schema') != 1:
        raise DownloadError('Unsupported game download manifest.')
    roots = []
    for key, label in (('base', 'Base game'), ('update', 'Update 688')):
        source = manifest[key]
        validate_source(source)
        if source.get('titleId', '').upper() != prepare.TITLE_ID or (key == 'update' and source.get('version') != 688):
            raise DownloadError('The download manifest is not for this game version.')
        folder = os.path.join(cache, source['sha256'])
        previous = [None, 0]
        def progress(phase, done, total):
            now = time.monotonic()
            if phase != previous[0] or now - previous[1] >= 1 or done == total:
                print('%s - %s: %.2f / %.2f GB' % (label, phase, done / 1e9, total / 1e9), flush=True)
                previous[:] = [phase, now]
        def validate(root):
            try:
                version = prepare.check_dump(root, key, {'update_version': 688})
            except prepare.PrepareError as error:
                raise DownloadError(str(error)) from error
            if version != source['version']:
                raise DownloadError('Unexpected game dump version.')
        print('Preparing %s...' % label, flush=True)
        archive = download_archive(source, folder, progress)
        roots.append(unpack_archive(archive, folder, source, validate, progress))
    return roots


def import_dumps(base, update, output):
    for source in (base, update):
        if within(output, source):
            raise prepare.PrepareError('The output must be outside the input game folders.')
    recipe = {'update_version': 688}
    base_version = prepare.check_dump(base, 'base', recipe)
    update_version = prepare.check_dump(update, 'update', recipe)
    parent = os.path.dirname(output)
    os.makedirs(parent, exist_ok=True)
    merged = prepare.content_files(base)
    merged.update(prepare.content_files(update))
    enough_space(parent, sum(os.path.getsize(file) for file in merged.values()))
    original = prepare.ASSETS_DIR
    try:
        with tempfile.TemporaryDirectory(prefix='.lce-assets-', dir=parent) as temporary:
            if not within(temporary, parent):
                raise prepare.PrepareError('Invalid temporary output directory.')
            prepare.ASSETS_DIR = os.path.join(temporary, 'assets')
            assets = prepare.prepare_assets(base, update)
            manifest = {'prepared_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                        'prepared_by': 'LCEReborn dedicated server setup', 'title_id': prepare.TITLE_ID,
                        'base_version': base_version, 'update_version': update_version, 'assets': assets}
            with open(os.path.join(prepare.ASSETS_DIR, prepare.MANIFEST), 'w', encoding='utf-8') as handle:
                json.dump(manifest, handle, indent=2)
                handle.write('\n')
            if os.path.lexists(output):
                raise prepare.PrepareError('The output folder was created during import; it has not been replaced.')
            os.rename(prepare.ASSETS_DIR, output)
    finally:
        prepare.ASSETS_DIR = original
    print('Server assets ready: %s (%d files)' % (output, assets['files']))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', help='optional extracted base game folder')
    parser.add_argument('--update', help='optional extracted Update 688 folder')
    parser.add_argument('--output', default=prepare.ASSETS_DIR, help='new output assets folder')
    parser.add_argument('--manifest', default=os.path.join(os.path.dirname(__file__), 'game-downloads.json'))
    parser.add_argument('--cache', default=os.path.join(prepare.ROOT, 'downloads', 'game-files'))
    args = parser.parse_args(argv)
    if bool(args.base) != bool(args.update):
        parser.error('Provide both --base and --update, or neither for automatic downloads.')
    output = filesystem_path(args.output)
    try:
        if not args.base and assets_ready(output):
            print('Server game files are already prepared: ' + output)
            return 0
        if os.path.lexists(output):
            raise prepare.PrepareError('The output folder already exists. Use it as --content-dir, or choose a new output folder.')
        if args.base:
            import_dumps(filesystem_path(args.base), filesystem_path(args.update), output)
        else:
            cache = filesystem_path(args.cache)
            with setup_lock(cache):
                base, update = auto_dumps(args.manifest, cache)
                import_dumps(base, update, output)
        return 0
    except KeyboardInterrupt:
        print('\nPreparation cancelled. Run prepare-server again to resume.', file=sys.stderr)
        return 130
    except (prepare.PrepareError, DownloadError, zipfile.BadZipFile, OSError, ValueError, KeyError) as error:
        print('error: %s' % error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
