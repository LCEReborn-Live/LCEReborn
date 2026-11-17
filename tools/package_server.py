#!/usr/bin/env python3
"""Package the existing dedicated-server build separately from the PC client.

    python tools/package_server.py --build build/pc

Windows produces a ZIP; Linux produces a tar.gz. Neither includes the launcher,
Qt, Node.js, game dumps, account files, configuration or saved worlds.
"""
import argparse
import io
import os
import sys
import tarfile
import zipfile

import package_pc as common


def server_files(build, system):
    program = 'LCERebornServer.exe' if system == 'windows' else 'LCERebornServer'
    binary = os.path.join(build, program)
    if not os.path.isfile(binary):
        raise common.PackageError('%s is missing. Build the LCERebornServer target first.' % binary)
    files = [(binary, program)]
    # PC headless hosting does not need interface artwork or Qt resources.
    for relative in ('tools/prepare_server.py', 'tools/prepare.py', 'tools/gamedata/rpl.py',
                     'tools/gamedata/shaders.py', 'tools/gamedata/recipe-v688.json'):
        source = os.path.join(common.ROOT, *relative.split('/'))
        if not os.path.isfile(source):
            raise common.PackageError('Missing server setup tool: ' + relative)
        files.append((source, relative))
    packs = os.path.join(common.ROOT, 'packs')
    for folder, _, names in os.walk(packs):
        for name in sorted(names):
            if name.lower().endswith('.pck'):
                source = os.path.join(folder, name)
                files.append((source, 'packs/' + os.path.relpath(source, packs).replace(os.sep, '/')))
    return files


def start_script(system):
    if system == 'windows':
        return 'start-server.cmd', ('@echo off\nsetlocal\ncd /d "%~dp0"\n'
                                    '"%~dp0LCERebornServer.exe" %*\nexit /b %errorlevel%\n')
    return 'start-server.sh', ('#!/bin/sh\nset -eu\n'
                               'here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                               'cd "$here"\nexec "$here/LCERebornServer" "$@"\n')


def readme(source, system, glibc=None):
    start = ('start-server.cmd' if system == 'windows' else './start-server.sh')
    python = 'python' if system == 'windows' else 'python3'
    requirements = (('Microsoft Visual C++ Redistributable 2015-2022 (x64) is required:\n%s\n' % common.REDIST_URL)
                    if system == 'windows' else
                    '64-bit x86 Linux, glibc %s or newer and libstdc++ are required.\n'
                    'No desktop session, display server, Qt or AppImage/FUSE is needed.\n' % glibc)
    return ('LCEReborn Live - Dedicated Server (%s x86-64)\n'
            '===========================================\n\n'
            '%s\n'
            'Game data\n---------\n'
            'Game data is not included. Copy the prepared assets folder from your\n'
            'launcher installation beside this README, or prepare it without a GUI:\n\n'
            '  %s tools/prepare_server.py --base "BASE_FOLDER" --update "UPDATE_688_FOLDER"\n\n'
            'The preparation tool needs Python 3 and extracted title folders with\n'
            'code/, content/ and meta/. It creates assets/ and refuses to replace\n'
            'an existing output folder. It does not generate Wii U shaders.\n\n'
            'Start and stop\n--------------\n'
            '  %s\n\n'
            'To use game data in another folder:\n'
            '  %s --content-dir "PATH_TO_ASSETS"\n\n'
            'The scripts select their own folder as the working directory. Saves,\n'
            'server.properties and ops.json live in saves/ by default; logs are in\n'
            'logs/. Override with --save-dir and --log-dir. These directories are\n'
            'not overwritten when you extract a newer server package here.\n\n'
            'The first start creates server.properties. Settings apply after a\n'
            'restart. The default game port is TCP 25570; change port= if needed.\n'
            'Clients can connect through Add Server using HOST:PORT. This package\n'
            'does not deploy or configure the LiveAPI or a relay node.\n\n'
            'Type help, save-all or stop in the server console. Ctrl+C saves and\n'
            'stops the server too. Back up saves/ before upgrading.\n\n'
            'Build and source\n----------------\n'
            'Server binary commit: %s%s\n%s/tree/%s\n'
            'Setup tools are included as Python source in tools/.\n'
            'See LICENSE.txt and third_party/ for source and dependency licenses.\n'
            % (system.capitalize(), requirements, python, start, start, source['commit'],
               ' (uncommitted test build)' if source['dirty'] else '', source['repository'], source['commit']))


def notices(source, game):
    parts = [common.rule('Dedicated-server third-party notices')]
    parts.extend(common.component_text(component) for component in game)
    parts.append('\nBuild source: %s/tree/%s\n' % (source['repository'], source['commit']))
    return '\n'.join(parts)


def license_entries(game):
    identifiers = {'GPL-3.0-only'}
    for component in game:
        identifiers.update(item for item in component['license'] if item != 'public domain')
    return [('third_party/licenses/' + identifier + '.txt',
             common.read(os.path.join(common.LICENSE_TEXTS, identifier + '.txt')))
            for identifier in sorted(identifiers)]


def write_archive(target, entries, system, date):
    if system == 'windows':
        common.write_zip(target, entries, date)
        return
    with tarfile.open(target, 'w:gz', compresslevel=9, format=tarfile.PAX_FORMAT) as archive:
        for name, content in entries:
            if isinstance(content, str):
                content = content.replace('\r\n', '\n').encode('utf-8')
            info = tarfile.TarInfo('LCERebornServer/' + name)
            info.size = len(content)
            info.mode = 0o755 if name in ('LCERebornServer', 'start-server.sh') else 0o644
            info.mtime = int(date.timestamp())
            archive.addfile(info, io.BytesIO(content))


def package(build, allow_dirty=False, output_dir=None, source=None, system=None):
    system = system or ('linux' if sys.platform.startswith('linux') else 'windows')
    if system not in ('windows', 'linux'):
        raise common.PackageError('Unsupported dedicated-server platform: ' + system)
    source = source or common.source_info(allow_dirty)
    files = server_files(build, system)
    game = common.game_components(build)
    glibc = common.glibc_requirement([files[0][0]]) if system == 'linux' else None
    entries = []
    for file, name in files:
        with open(file, 'rb') as handle:
            entries.append((name, handle.read()))
    entries.extend([start_script(system), ('README.txt', readme(source, system, glibc)),
                    ('LICENSE.txt', common.read(os.path.join(common.ROOT, 'LICENSE'))),
                    ('third_party/NOTICES.txt', notices(source, game))])
    entries.extend(license_entries(game))
    output_dir = output_dir or common.OUTPUT_DIR
    os.makedirs(output_dir, exist_ok=True)
    name = 'LCEReborn-server-%s-%s%s%s' % (system, source['commit'][:12],
            '-dirty' if source['dirty'] else '', '.tar.gz' if system == 'linux' else '.zip')
    target = os.path.join(output_dir, name)
    write_archive(target, entries, system, source['date'])
    print('Wrote %s (%d server files)' % (target, len(entries)))
    return [target], source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default=common.DEFAULT_BUILD)
    parser.add_argument('--output-dir', default=common.OUTPUT_DIR)
    parser.add_argument('--allow-dirty', action='store_true', help='make an unpublished local test package')
    args = parser.parse_args()
    try:
        package(os.path.abspath(args.build), args.allow_dirty, args.output_dir)
    except (common.PackageError, OSError) as error:
        print('error: %s' % error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
