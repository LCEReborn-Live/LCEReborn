import datetime
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bundled_assets
import package_pc
import package_server


class BundledAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / 'assets'
        for name in ('Common/Media/MediaWiiU.arc', 'Common/Media/MediaOrbis.arc', 'music/song.binka',
                     'WiiU/Sound/Minecraft.msscmp', 'custom-villages/outpost/watchtower.nbt'):
            path = self.assets / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'asset\x00\r\n\xff')
        bundled_assets.write_manifest(self.assets)

    def test_final_client_archives_preserve_verified_asset_bytes(self):
        (self.root / 'LICENSE').write_text('license fixture')
        files = bundled_assets.verify(self.assets)
        source = {'commit': '1' * 40, 'dirty': False, 'repository': 'https://example.test/repo',
                  'date': datetime.datetime(2026, 11, 28, tzinfo=datetime.timezone.utc)}
        for linux in (False, True):
            with self.subTest(linux=linux), \
                 patch.object(package_pc, 'ROOT', str(self.root)), \
                 patch.object(package_pc, 'OUTPUT_DIR', str(self.root)), \
                 patch.object(package_pc, 'LINUX', linux), \
                 patch.object(package_pc, 'SYSTEM', 'linux' if linux else 'pc'), \
                 patch.object(package_pc, 'source_info', return_value=source), \
                 patch.object(package_pc, 'program_files', return_value=(files, [])), \
                 patch.object(package_pc, 'qt_notices', return_value={}), \
                 patch.object(package_pc, 'game_components', return_value=[]), \
                 patch.object(package_pc, 'launcher_components', return_value=[]), \
                 patch.object(package_pc, 'glibc_requirement', return_value='2.35'), \
                 patch.object(package_pc, 'readme_text', return_value='readme fixture'), \
                 patch.object(package_pc, 'notices_text', return_value='notices fixture'), \
                 patch.object(package_pc, 'license_files', return_value=[]):
                outputs, _ = package_pc.package(str(self.root / 'build'), False, appimage=False)
            if linux:
                with tarfile.open(outputs[0]) as archive:
                    for path, name in files:
                        self.assertEqual(archive.extractfile(package_pc.TOP + '/' + name).read(), Path(path).read_bytes())
            else:
                with zipfile.ZipFile(outputs[0]) as archive:
                    for path, name in files:
                        self.assertEqual(archive.read(name), Path(path).read_bytes())

    def test_unverified_game_archive_is_still_rejected(self):
        for name in ('assets/unlisted.arc', 'outside/MediaOrbis.arc'):
            with self.subTest(name=name), self.assertRaises(package_pc.PackageError):
                package_pc.check_names([(name, b'archive')], {'assets/Common/Media/MediaOrbis.arc'})

    def test_all_assets_are_in_client_and_server_packages(self):
        build = self.root / 'build'
        build.mkdir()
        for name in set(package_pc.PROGRAMS) | {'LCERebornServer.exe', 'LCERebornServer'}:
            (build / name).write_bytes(b'binary')
        runtime = build / 'live-runtime' / ('node' if package_pc.LINUX else 'node.exe')
        runtime.parent.mkdir()
        runtime.write_bytes(b'node')
        stale_pack = build / 'packs/1.14 Texture Pack/TexturePack.pck'
        stale_pack.parent.mkdir(parents=True)
        stale_pack.write_bytes(b'obsolete')
        with patch.object(package_pc, 'ROOT', str(self.root)), \
             patch.object(package_pc, 'windows_qt_files', return_value=[]), \
             patch.object(package_pc, 'linux_qt_files', return_value=[]), \
             patch.object(package_server, 'setup_files', return_value=[]):
            client, _ = package_pc.program_files(str(build))
            server = package_server.server_files(str(build), 'windows')
        for files in (client, server):
            names = {name for _, name in files}
            self.assertIn('assets/custom-villages/outpost/watchtower.nbt', names)
            self.assertIn('assets/Common/Media/MediaWiiU.arc', names)
            self.assertIn('assets/.bundle-manifest.json', names)
            self.assertFalse(any('1.14 Texture Pack' in name for name in names))
        self.assertEqual({name for _, name in client if name.startswith('assets/')},
                         {name for _, name in server if name.startswith('assets/')})

    def test_corrupt_or_missing_asset_rejects_package(self):
        path = self.assets / 'music/song.binka'
        path.write_bytes(b'corrupted')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            bundled_assets.verify(self.assets)
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'checksum'):
            bundled_assets.verify(self.assets)

    def test_unlisted_file_rejects_package(self):
        (self.assets / 'private.json').write_text('not part of the bundle')
        with self.assertRaisesRegex(ValueError, 'private.json'):
            bundled_assets.verify(self.assets)

    def test_missing_bundle_rejects_package(self):
        with patch.object(package_pc, 'ROOT', str(self.root / 'missing')):
            with self.assertRaisesRegex(package_pc.PackageError, 'missing or damaged'):
                package_pc.bundled_game_files()


if __name__ == '__main__':
    unittest.main()
