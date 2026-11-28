#!/usr/bin/env python3
"""Create standalone server packages from a verified successful client build.

Used by the manual server-release workflow to reuse existing compiled binaries.
The existing release and its tag are kept; only two server assets are added.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

import package_server
import package_pc as common


def portable_files(archive, system):
    selected = {}
    if system == 'windows':
        with zipfile.ZipFile(archive) as package:
            for name in package.namelist():
                if name == 'LCERebornServer.exe' or name == 'third_party/NOTICES.txt' or name.startswith(('packs/', 'assets/')):
                    if not name.endswith('/'):
                        selected[name] = package.read(name)
    else:
        with tarfile.open(archive) as package:
            for member in package:
                name = member.name.removeprefix('LCEReborn/')
                if name == 'LCERebornServer' or name == 'third_party/NOTICES.txt' or name.startswith(('packs/', 'assets/')):
                    if member.isfile():
                        selected[name] = package.extractfile(member).read()
    if any(name.startswith('/') or '..' in name.split('/') or '\\' in name for name in selected):
        raise common.PackageError('Unsafe source package paths')
    return selected


def package_portable(archive, system, source, version, output):
    files = portable_files(archive, system)
    binary_name = 'LCERebornServer.exe' if system == 'windows' else 'LCERebornServer'
    binary = files.get(binary_name)
    if not binary or 'third_party/NOTICES.txt' not in files:
        raise common.PackageError('The source package has no dedicated server or attribution information')
    original = files['third_party/NOTICES.txt'].decode('utf-8')
    if source['commit'] not in original:
        raise common.PackageError('The source package attribution does not match the successful build revision')
    if '\nQt, in ' not in original:
        raise common.PackageError('Unexpected source-package attribution format')
    engine_notices = original.split('\nQt, in ', 1)[0]
    identifiers = {'GPL-3.0-only'}
    for line in re.findall(r'^License: (.+)$', engine_notices, re.M):
        identifiers.update(item.strip() for item in line.split(',') if item.strip() != 'public domain')
    glibc = None
    if system == 'linux':
        versions = re.findall(rb'GLIBC_(\d+\.\d+(?:\.\d+)?)\x00', binary)
        if not versions:
            raise common.PackageError('The Linux server has no glibc version information')
        glibc = max((value.decode() for value in versions), key=lambda value: tuple(map(int, value.split('.'))))
    entries = [(binary_name, binary)]
    entries.extend((name, data) for name, data in sorted(files.items()) if name.startswith('assets/'))
    entries.extend((name, data) for name, data in sorted(files.items()) if name.startswith('packs/') and name.endswith('.pck'))
    for file, relative in package_server.setup_files(system):
        entries.append((relative, Path(file).read_bytes()))
    entries.extend([package_server.start_script(system), package_server.prepare_script(system), ('README.txt', package_server.readme(source, system, glibc)),
                    ('LICENSE.txt', Path(common.ROOT, 'LICENSE').read_bytes()),
                    ('third_party/NOTICES.txt', engine_notices + '\nServer source: ' + source['repository'] + '/tree/' + source['commit'] + '\n')])
    for identifier in sorted(identifiers):
        entries.append(('third_party/licenses/' + identifier + '.txt',
                        common.read(os.path.join(common.LICENSE_TEXTS, identifier + '.txt'))))
    suffix = 'Windows-' + version + '.zip' if system == 'windows' else 'Linux-' + version + '.tar.gz'
    target = Path(output, 'LCEReborn-Live-Server-' + suffix)
    target.parent.mkdir(parents=True, exist_ok=True)
    package_server.write_archive(str(target), entries, system, source['date'])
    print('Prepared', target.name, 'from server build', source['commit'])
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-id', required=True, type=int)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--input', default='build/server-source-artifacts')
    parser.add_argument('--output', default='build/server-release')
    args = parser.parse_args()
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:[-.][a-zA-Z0-9.]+)?', args.tag):
        parser.error('Expected an existing version tag, for example v1.2.3')
    repository = os.environ['GITHUB_REPOSITORY']
    headers = {'Authorization': 'Bearer ' + os.environ['GH_TOKEN'], 'User-Agent': 'LCEReborn-server-release',
               'Accept': 'application/vnd.github+json'}
    def get(route):
        request = urllib.request.Request('https://api.github.com/repos/' + repository + route, headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    run = get('/actions/runs/' + str(args.run_id))
    if run['conclusion'] != 'success' or run['head_repository']['full_name'].lower() != repository.lower():
        raise common.PackageError('A successful build from this repository is required')
    if run['path'] != '.github/workflows/windows-release.yml':
        raise common.PackageError('Expected the Windows/Linux client build workflow')
    get('/releases/tags/' + args.tag)  # Refuse to create a different release or tag.
    revision = run['head_sha']
    check = subprocess.run(['git', 'diff', '--quiet', revision, 'HEAD', '--', 'source', 'CMakeLists.txt',
                            '.gitmodules', 'tools/build/deps.json', 'tools/build/third_party', 'tools/gamedata'])
    if check.returncode != 0:
        raise common.PackageError('Game code/dependencies changed since this build; choose a newer successful run')
    timestamp = int(subprocess.check_output(['git', 'show', '-s', '--format=%ct', revision], text=True).strip())
    source = {'commit': revision, 'dirty': False, 'repository': 'https://github.com/' + repository,
              'date': datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc)}
    patterns = {'windows': 'LCEReborn-Live-Portable-*.zip', 'linux': 'LCEReborn-Live-Linux-Portable-*.tar.gz'}
    for system, pattern in patterns.items():
        matches = list(Path(args.input).rglob(pattern))
        if len(matches) != 1:
            raise common.PackageError('Expected one ' + system + ' portable source archive')
        package_portable(matches[0], system, source, args.tag.removeprefix('v'), args.output)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (common.PackageError, OSError) as error:
        print('error:', error, file=__import__('sys').stderr)
        raise SystemExit(1)
