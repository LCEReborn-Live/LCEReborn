import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bundled_assets
import package
import wuhb


class WiiUPackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / 'assets'
        for name in ('Common/Media/MediaWiiU.arc', 'Common/Media/MediaOrbis.arc',
                     'music/song.binka', 'WiiU/Sound/Minecraft.msscmp',
                     'custom-villages/pillager_outpost/watchtower.nbt', '.prepared.json'):
            path = self.assets / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'prepared-tu80-' + name.encode())
        bundled_assets.write_manifest(self.assets)
        self.rpx = self.root / 'game.rpx'
        self.rpx.write_bytes(b'rpx fixture')

    def test_complete_wuhb_uses_prepared_ps4_resources_and_templates(self):
        with patch.object(package.prepare, 'ASSETS_DIR', str(self.assets)):
            files = package.prepared_content()
        self.assertNotIn('.prepared.json', files)
        self.assertNotIn('.bundle-manifest.json', files)
        bundle = package.build_bundle(str(self.rpx), None, files)
        output = self.root / 'game.wuhb'
        wuhb.write(str(output), bundle)
        with output.open('rb') as handle:
            self.assertEqual(handle.read(4), b'WUHB')
            def check(folder, prefix=''):
                for child in folder.children:
                    path = prefix + child.name
                    if isinstance(child, wuhb.Dir):
                        check(child, path + '/')
                    elif path.startswith('content/') and path[8:] in files:
                        handle.seek(wuhb.FILE_PARTITION_OFFSET + child.data_offset)
                        self.assertEqual(handle.read(child.size), Path(files[path[8:]]).read_bytes())
            check(bundle)
        self.assertIn('Common/Media/MediaOrbis.arc', files)
        self.assertIn('custom-villages/pillager_outpost/watchtower.nbt', files)

    def test_corrupted_prepared_content_fails_before_packaging(self):
        (self.assets / 'Common/Media/MediaWiiU.arc').write_bytes(b'wrong')
        with patch.object(package.prepare, 'ASSETS_DIR', str(self.assets)):
            with self.assertRaisesRegex(package.PackageError, 'missing or damaged'):
                package.prepared_content()


if __name__ == '__main__':
    unittest.main()
