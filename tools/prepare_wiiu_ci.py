#!/usr/bin/env python3
"""Prepare Wii U build inputs from the checksum-pinned Update 688 archive."""

import json
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
from zipfile import ZipFile

import prepare
from server_download import download_archive


def main():
    manifest = Path(prepare.ROOT) / 'resources/pc/game-downloads.json'
    update = json.loads(manifest.read_text(encoding='utf-8'))['update']
    recipe = prepare.shaders.load_recipe()
    executable_path = recipe['executable']['path']
    with TemporaryDirectory(prefix='lcereborn-wiiu-') as temporary:
        folder = Path(temporary)
        print('Downloading the pinned Wii U update archive', flush=True)
        archive = download_archive(update, folder)
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
