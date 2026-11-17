#!/usr/bin/env python3
"""Prepare dedicated-server assets from extracted game and Update 688 folders.

Only Python's standard library is needed. No launcher, Qt or shader generation.
"""
import argparse
import json
import os
import sys
import tempfile
import time

import prepare


def filesystem_path(value):
    value = os.path.abspath(value)
    if os.name == 'nt' and not value.startswith('\\\\?\\'):
        return '\\\\?\\UNC\\' + value[2:] if value.startswith('\\\\') else '\\\\?\\' + value
    return value


def within(child, parent):
    try:
        return os.path.commonpath([os.path.realpath(child), os.path.realpath(parent)]) == os.path.realpath(parent)
    except ValueError:  # Different Windows drives cannot contain each other.
        return False


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True, help='extracted base game folder')
    parser.add_argument('--update', required=True, help='extracted Update 688 folder')
    parser.add_argument('--output', default=prepare.ASSETS_DIR, help='new output assets folder')
    args = parser.parse_args(argv)
    args.base = filesystem_path(args.base)
    args.update = filesystem_path(args.update)
    output = filesystem_path(args.output)
    try:
        if os.path.lexists(output):
            raise prepare.PrepareError('The output folder already exists. Use it as --content-dir, or choose a new output folder.')
        for source in (args.base, args.update):
            source = os.path.realpath(source)
            if within(output, source):
                raise prepare.PrepareError('The output must be outside the input game folders.')
        recipe = {'update_version': 688}
        base_version = prepare.check_dump(args.base, 'base', recipe)
        update_version = prepare.check_dump(args.update, 'update', recipe)
        parent = os.path.dirname(output)
        os.makedirs(parent, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.lce-assets-', dir=parent) as temporary:
            # Cleanup is confined to the unique directory created for this import.
            if not within(temporary, parent):
                raise prepare.PrepareError('Invalid temporary output directory.')
            prepare.ASSETS_DIR = os.path.join(temporary, 'assets')
            assets = prepare.prepare_assets(args.base, args.update)
            manifest = {'prepared_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                        'prepared_by': 'LCEReborn dedicated server setup', 'title_id': prepare.TITLE_ID,
                        'base_version': base_version, 'update_version': update_version, 'assets': assets}
            with open(os.path.join(prepare.ASSETS_DIR, prepare.MANIFEST), 'w', encoding='utf-8') as handle:
                json.dump(manifest, handle, indent=2)
                handle.write('\n')
            if os.path.lexists(output):
                raise prepare.PrepareError('The output folder was created during import; it has not been replaced.')
            os.rename(prepare.ASSETS_DIR, output)
        print('Server assets ready: %s (%d files)' % (output, assets['files']))
        return 0
    except (prepare.PrepareError, OSError, ValueError) as error:
        print('error: %s' % error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
