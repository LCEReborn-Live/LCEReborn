import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import package_server
import prepare_server
import release_server_artifacts

SOURCE = {'commit': '1' * 40, 'dirty': True, 'repository': 'https://github.com/LCEReborn-Live/LCEReborn',
          'date': datetime.datetime(2026, 11, 16, tzinfo=datetime.timezone.utc)}


class ServerPackageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='lce-server-package-')
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / 'build'
        self.build.mkdir()
        for name in ('LCERebornServer.exe', 'LCERebornServer'):
            (self.build / name).write_bytes(b'server fixture')
        for name in ('LCERebornLauncher.exe', 'Qt6Core.dll', 'server.properties', 'launcher.ini'):
            (self.build / name).write_bytes(b'MUST NOT SHIP')
        for name in ('assets', 'saves', 'logs', 'live'):
            (self.build / name).mkdir()
            (self.build / name / 'private.json').write_text('MUST NOT SHIP')

    def tearDown(self):
        if self.root.name.startswith('lce-server-package-') and self.root.parent == Path(tempfile.gettempdir()).resolve():
            self.temporary.name = prepare_server.filesystem_path(self.temporary.name)
            self.temporary.cleanup()

    def package(self, system):
        with patch.object(package_server.common, 'game_components', return_value=[]), \
             patch.object(package_server.common, 'glibc_requirement', return_value='2.35'):
            outputs, _ = package_server.package(str(self.build), output_dir=str(self.root), source=SOURCE, system=system)
        return Path(outputs[0])

    def assert_contents(self, names):
        self.assertIn('tools/prepare_server.py', names)
        self.assertIn('tools/prepare.py', names)
        self.assertIn('tools/gamedata/shaders.py', names)
        self.assertIn('LICENSE.txt', names)
        self.assertIn('third_party/NOTICES.txt', names)
        for name in names:
            self.assertNotIn('Qt6', name)
            self.assertNotIn('Launcher', name)
            self.assertFalse(name.startswith(('assets/', 'saves/', 'logs/', 'live/')))
            self.assertNotIn(name, ('server.properties', 'launcher.ini'))

    def test_windows_package_excludes_client_and_user_data(self):
        with zipfile.ZipFile(self.package('windows')) as archive:
            names = set(archive.namelist())
            self.assert_contents(names)
            self.assertIn('LCERebornServer.exe', names)
            self.assertIn('start-server.cmd', names)
            self.assertEqual(archive.read('LCERebornServer.exe'), b'server fixture')
            self.assertIn(b'cd /d "%~dp0"', archive.read('start-server.cmd'))
            self.assertIn(b'Visual C++', archive.read('README.txt'))

    def test_linux_archive_has_correct_root_and_executable_modes(self):
        with tarfile.open(self.package('linux')) as archive:
            members = archive.getmembers()
            self.assertTrue(all(member.name.startswith('LCERebornServer/') for member in members))
            self.assert_contents({member.name.removeprefix('LCERebornServer/') for member in members})
            self.assertEqual(archive.getmember('LCERebornServer/LCERebornServer').mode, 0o755)
            self.assertEqual(archive.getmember('LCERebornServer/start-server.sh').mode, 0o755)
            self.assertEqual(archive.getmember('LCERebornServer/tools/prepare_server.py').mode, 0o644)
            self.assertIn(b'glibc 2.35', archive.extractfile('LCERebornServer/README.txt').read())

    def test_missing_binary_and_unknown_platform_fail(self):
        with self.assertRaises(package_server.common.PackageError):
            package_server.server_files(str(self.root), 'windows')
        with self.assertRaises(package_server.common.PackageError):
            package_server.package(str(self.build), source=SOURCE, system='unknown')

    def test_repack_existing_windows_build_preserves_binary_and_omits_client(self):
        portable = self.root / 'portable.zip'
        notice = 'Engine\nLicense: Zlib\nCopyright test\n\nQt, in Launcher\nQt details\nSource: ' + SOURCE['commit']
        with zipfile.ZipFile(portable, 'w') as archive:
            archive.writestr('LCERebornServer.exe', b'existing compiled server')
            archive.writestr('third_party/NOTICES.txt', notice)
            archive.writestr('Qt6Core.dll', b'omit')
            archive.writestr('live/node.exe', b'omit')
            archive.writestr('packs/default/TexturePack.pck', b'pack')
        target = release_server_artifacts.package_portable(portable, 'windows', SOURCE, '1.2.3', self.root)
        with zipfile.ZipFile(target) as archive:
            self.assertEqual(archive.read('LCERebornServer.exe'), b'existing compiled server')
            self.assertNotIn('Qt6Core.dll', archive.namelist())
            self.assertNotIn('live/node.exe', archive.namelist())
            self.assertNotIn(b'Qt details', archive.read('third_party/NOTICES.txt'))

    def test_repack_existing_linux_build_sets_executable_permissions(self):
        import io
        portable = self.root / 'portable.tar.gz'
        notice = ('Engine\nLicense: Zlib\n\nQt, in Launcher\nSource: ' + SOURCE['commit']).encode()
        binary = b'\x7fELF existing server GLIBC_2.35\0'
        with tarfile.open(portable, 'w:gz') as archive:
            for name, data in [('LCEReborn/LCERebornServer', binary), ('LCEReborn/third_party/NOTICES.txt', notice)]:
                member = tarfile.TarInfo(name); member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        target = release_server_artifacts.package_portable(portable, 'linux', SOURCE, '1.2.3', self.root)
        with tarfile.open(target) as archive:
            self.assertEqual(archive.extractfile('LCERebornServer/LCERebornServer').read(), binary)
            self.assertEqual(archive.getmember('LCERebornServer/start-server.sh').mode, 0o755)

    def make_dump(self, name, version):
        root = self.root / name
        for folder in ('code', 'meta', 'content/Common', 'content/music', 'content/WiiU'):
            (root / folder).mkdir(parents=True, exist_ok=True)
        (root / 'meta/meta.xml').write_text('<menu><title_id>00050000101D9D00</title_id>'
                                         '<title_version>%d</title_version></menu>' % version)
        return root

    def test_packaged_headless_setup_merges_updates_and_preserves_existing_output(self):
        package = self.package('windows')
        install = self.root / 'install'
        with zipfile.ZipFile(package) as archive:
            self.assertTrue(all(not name.startswith('/') and '..' not in Path(name).parts for name in archive.namelist()))
            archive.extractall(install)
        base = self.make_dump('base', 1)
        update = self.make_dump('update', 688)
        (base / 'content/Common/shared').write_text('base')
        (update / 'content/Common/shared').write_text('update')
        (base / 'content/Common/base-only').write_text('keep')
        output = self.root / 'prepared'
        command = [sys.executable, str(install / 'tools/prepare_server.py'), '--base', str(base),
                   '--update', str(update), '--output', str(output)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((output / 'Common/shared').read_text(), 'update')
        self.assertEqual((output / 'Common/base-only').read_text(), 'keep')
        self.assertEqual(json.loads((output / '.prepared.json').read_text())['update_version'], 688)
        self.assertFalse((install / 'source').exists(), 'Setup must not generate Wii U shader sources')
        again = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(again.returncode, 0)
        self.assertEqual((output / 'Common/base-only').read_text(), 'keep')

    def test_headless_setup_rejects_wrong_update_before_creating_output(self):
        base, update = self.make_dump('base', 1), self.make_dump('wrong-update', 1)
        output = self.root / 'rejected'
        result = subprocess.run([sys.executable, str(TOOLS / 'prepare_server.py'), '--base', str(base),
                                 '--update', str(update), '--output', str(output)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())

    def test_headless_setup_accepts_long_windows_input_paths(self):
        deep = self.root / ('a' * 90) / ('b' * 90)
        base = self.make_dump(prepare_server.filesystem_path(deep / 'base'), 1)
        update = self.make_dump(prepare_server.filesystem_path(deep / 'update'), 688)
        (base / 'content/Common/base-file').write_text('long path data')
        output = self.root / 'long-path-output'
        # Pass ordinary paths to the command line; the tool must normalize them.
        base_arg = str(deep / 'base')
        update_arg = str(deep / 'update')
        result = subprocess.run([sys.executable, str(TOOLS / 'prepare_server.py'), '--base', base_arg,
                                 '--update', update_arg, '--output', str(output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((output / 'Common/base-file').read_text(), 'long path data')


if __name__ == '__main__':
    unittest.main()
