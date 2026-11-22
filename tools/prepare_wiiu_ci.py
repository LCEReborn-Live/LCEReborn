#!/usr/bin/env python3
"""Prepare Wii U build inputs and optionally the verified game dumps."""

import argparse
import json
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
from zipfile import ZipFile

import prepare
from server_download import download_archive, unpack_archive


def prepare_dumps(downloads, recipe, target):
    target.mkdir(parents=True, exist_ok=True)
    dumps = {}
    for role in ('update', 'base'):
        source = downloads[role]
        folder = target / role
        print(f'Downloading the pinned Wii U {role} archive', flush=True)
        archive = download_archive(source, folder)
        print(f'Unpacking the verified Wii U {role} archive', flush=True)
        dumps[role] = unpack_archive(archive, folder, source, lambda path: prepare.check_dump(path, role, recipe))
        archive.unlink()
    (target / 'dumps.json').write_text(json.dumps(dumps), encoding='utf-8')
    return Path(dumps['update'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dumps-dir', type=Path, help='keep both decrypted dumps here for WUHB packaging')
    args = parser.parse_args()
    manifest = Path(prepare.ROOT) / 'resources/pc/game-downloads.json'
    downloads = json.loads(manifest.read_text(encoding='utf-8'))
    recipe = prepare.shaders.load_recipe()
    if args.dumps_dir is not None:
        update_dump = prepare_dumps(downloads, recipe, args.dumps_dir.resolve())
        print('Generating Wii U build inputs', flush=True)
        prepare.prepare_shaders(str(update_dump), recipe)
        return

    executable_path = recipe['executable']['path']
    with TemporaryDirectory(prefix='lcereborn-wiiu-') as temporary:
        folder = Path(temporary)
        print('Downloading the pinned Wii U update archive', flush=True)
        archive = download_archive(downloads['update'], folder)
        with ZipFile(archive) as source:
            matches = [entry for entry in source.infolist()
                       if entry.filename == executable_path or entry.filename.endswith('/' + executable_path)]
            if len(matches) != 1 or matches[0].file_size != recipe['executable']['size']:
                raise SystemExit('Update archive does not contain the expected executable')
            executable = folder / 'Minecraft.Client.rpx'
            with source.open(matches[0]) as contents, executable.open('wb') as output:
                shutil.copyfileobj(contents, output)
        print('Generating Wii U build inputs', flush=True)
        prepare.shaders.generate(executable, prepare.SHADER_DIR)


if __name__ == '__main__':
    main()
