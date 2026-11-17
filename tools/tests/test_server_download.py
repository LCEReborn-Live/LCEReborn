import contextlib
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
import zipfile

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import prepare
import prepare_server
import server_download as download


class ServerDownloadTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='lce-server-download-')
        self.root = Path(download.filesystem_path(self.temporary.name))
        self.data = b'ZIP-download-fixture-' * 100000
        self.requests = []

    def tearDown(self):
        self.temporary.name = str(self.root)
        self.temporary.cleanup()

    def source(self, data=None, url='https://example.invalid/archive.zip', unpacked=1):
        data = self.data if data is None else data
        return {'url': url, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                'unpackedBytes': unpacked, 'titleId': prepare.TITLE_ID, 'version': 1}

    @contextlib.contextmanager
    def server(self, mode='range'):
        owner = self
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                header = self.headers.get('Range')
                owner.requests.append(header)
                offset = int(header.split('=')[1].split('-')[0]) if header else 0
                if mode == 'html':
                    self.send_response(200)
                    self.send_header('Content-Type', 'text/html')
                    self.end_headers()
                    self.wfile.write(b'<html>Download quota exceeded</html>')
                    return
                if mode == 'ignore-range':
                    offset = 0
                self.send_response(206 if offset else 200)
                self.send_header('Content-Type', 'application/zip')
                if offset:
                    start = offset + 1 if mode == 'bad-range' else offset
                    self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, len(owner.data) - 1, len(owner.data)))
                self.send_header('Content-Length', str(len(owner.data) - offset))
                self.end_headers()
                if mode == 'interrupt' and len(owner.requests) == 1:
                    self.wfile.write(owner.data[:1024 * 1024])
                    self.close_connection = True
                    return
                try:
                    self.wfile.write(owner.data[offset:])
                except (BrokenPipeError, ConnectionResetError):
                    pass  # Expected when the downloader rejects headers or is cancelled.
        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            yield 'http://127.0.0.1:%d/archive.zip' % server.server_port
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=3)

    def test_resume_and_ignored_range_both_produce_exact_archive(self):
        for mode in ('range', 'ignore-range'):
            with self.subTest(mode=mode), self.server(mode) as url:
                folder = self.root / mode
                folder.mkdir()
                (folder / 'archive.zip.part').write_bytes(self.data[:1234])
                result = download.download_archive(self.source(url=url), folder, allow_test_http=True)
                self.assertEqual(result.read_bytes(), self.data)
                self.assertFalse((folder / 'archive.zip.part').exists())
                self.assertEqual(self.requests[-1], 'bytes=1234-')

    def test_interrupted_transfer_resumes_and_valid_cache_avoids_network(self):
        with self.server('interrupt') as url, patch.object(download.time, 'sleep'):
            source = self.source(url=url)
            result = download.download_archive(source, self.root, allow_test_http=True)
        self.assertEqual(result.read_bytes(), self.data)
        self.assertEqual(self.requests, [None, 'bytes=1048576-'])
        with patch.object(download, 'open_download', side_effect=AssertionError('Unexpected network access')):
            self.assertEqual(download.download_archive(source, self.root), result)

    def test_bad_range_html_and_bad_checksum_never_become_ready(self):
        for mode in ('bad-range', 'html', 'checksum'):
            with self.subTest(mode=mode), self.server(mode) as url:
                folder = self.root / mode
                folder.mkdir()
                source = self.source(url=url)
                if mode == 'bad-range':
                    (folder / 'archive.zip.part').write_bytes(self.data[:42])
                if mode == 'checksum':
                    source['sha256'] = '0' * 64
                with self.assertRaises(download.DownloadError):
                    download.download_archive(source, folder, allow_test_http=True)
                self.assertFalse((folder / 'archive.zip').exists())

    def test_cancellation_preserves_partial_archive_for_next_run(self):
        def cancel(*args):
            raise KeyboardInterrupt()
        with self.server() as url:
            source = self.source(url=url)
            with self.assertRaises(KeyboardInterrupt):
                download.download_archive(source, self.root, cancel, allow_test_http=True)
            self.assertGreater((self.root / 'archive.zip.part').stat().st_size, 0)
            result = download.download_archive(source, self.root, allow_test_http=True)
            self.assertEqual(result.read_bytes(), self.data)

    def test_google_confirmation_only_accepts_expected_download(self):
        form = ('<form id="download-form" action="https://drive.usercontent.google.com/download">'
                '<input name="id" value="file-id"><input name="export" value="download">'
                '<input name="confirm" value="t"><input name="uuid" value="token"></form>')
        result = download.confirmation_url(form, 'file-id')
        self.assertIn('confirm=t', result)
        for invalid in (form.replace('drive.usercontent.google.com', 'example.com'),
                        form.replace('https:', 'http:'), form.replace('file-id', 'other-id')):
            with self.assertRaises(download.DownloadError):
                download.confirmation_url(invalid, 'file-id')
        with self.assertRaises(download.DownloadError):
            download.open_download(self.source(url='http://example.com/a.zip'))

    def zip_fixture(self, entries):
        archive = self.root / 'fixture.zip'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, allowZip64=True) as output:
            for name, content in entries:
                output.writestr(name, content)
        total = sum(len(content) for _, content in entries)
        return archive, self.source(archive.read_bytes(), unpacked=total)

    def test_extraction_checks_crc_and_preserves_existing_cached_data_on_failure(self):
        entries = [('dump/meta/meta.xml', b'meta'), ('dump/content/test', b'content')]
        archive, source = self.zip_fixture(entries)
        folder = self.root / 'cache'
        folder.mkdir()
        root = Path(download.unpack_archive(archive, folder, source, lambda root: None))
        self.assertEqual((root / 'content/test').read_bytes(), b'content')
        # A failed replacement must leave the last complete extraction intact.
        (folder / 'ready.json').unlink()
        def reject(root):
            raise download.DownloadError('wrong dump metadata')
        with self.assertRaises(download.DownloadError):
            download.unpack_archive(archive, folder, source, reject)
        self.assertEqual((root / 'content/test').read_bytes(), b'content')
        self.assertFalse(list(folder.glob('unpacking-*')))
        archive, source = self.zip_fixture([('dump/meta/meta.xml', b'meta'), ('dump/file', b'CRC-check')])
        with zipfile.ZipFile(archive) as zipped:
            info = zipped.getinfo('dump/file')
            position = info.header_offset + 30 + len(info.filename.encode()) + len(info.extra)
        raw = bytearray(archive.read_bytes())
        raw[position] ^= 0x10
        archive.write_bytes(raw)
        with self.assertRaises((zipfile.BadZipFile, OSError)):
            download.unpack_archive(archive, folder, source, lambda root: None)

    def test_zip_rejects_traversal_links_case_collisions_and_size_mismatch(self):
        link = zipfile.ZipInfo('dump/link')
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for extra in [('../outside', b'x'), ('C:/outside', b'x'), ('dump/CON.txt', b'x'),
                      ('dump/../../outside', b'x'), ('dump/Meta/meta.xml', b'x'), (link, b'../outside')]:
            with self.subTest(extra=str(extra[0])):
                archive, source = self.zip_fixture([('dump/meta/meta.xml', b'meta'), extra])
                with self.assertRaises(download.DownloadError):
                    download.unpack_archive(archive, self.root, source, lambda root: None)
        archive, source = self.zip_fixture([('dump/meta/meta.xml', b'meta')])
        source['unpackedBytes'] += 1
        with self.assertRaises(download.DownloadError):
            download.unpack_archive(archive, self.root, source, lambda root: None)
        self.assertFalse((self.root / 'unpacked').exists())

    def test_extracted_paths_longer_than_windows_max_path(self):
        relative = 'dump/content/' + '/'.join(['long-name-' * 5] * 6) + '/asset'
        archive, source = self.zip_fixture([('dump/meta/meta.xml', b'meta'), (relative, b'deep asset')])
        download.unpack_archive(archive, self.root, source, lambda root: None)
        self.assertEqual((self.root / 'unpacked' / relative).read_bytes(), b'deep asset')

    def test_auto_setup_from_verified_cache_merges_update_and_is_idempotent(self):
        cache = self.root / 'downloads'
        manifest = {'schema': 1}
        for key, version in [('base', 1), ('update', 688)]:
            entries = [('dump/code/', b''), ('dump/content/Common/Media/MediaWiiU.arc', key.encode()),
                       ('dump/content/music/file', b'music'), ('dump/content/WiiU/file', b'wiiu'),
                       ('dump/meta/meta.xml', ('<menu><title_id>%s</title_id><title_version>%d</title_version></menu>'
                                              % (prepare.TITLE_ID, version)).encode())]
            archive, source = self.zip_fixture(entries)
            source['version'] = version
            folder = cache / source['sha256']
            folder.mkdir(parents=True)
            (folder / 'archive.zip').write_bytes(archive.read_bytes())
            manifest[key] = source
        manifest_path = self.root / 'manifest.json'
        manifest_path.write_text(json.dumps(manifest))
        output = self.root / 'assets'
        args = ['--manifest', str(manifest_path), '--cache', str(cache), '--output', str(output)]
        with patch.object(download, 'open_download', side_effect=AssertionError('Unexpected network access')):
            self.assertEqual(prepare_server.main(args), 0)
            self.assertEqual((output / 'Common/Media/MediaWiiU.arc').read_bytes(), b'update')
            (output / 'user-file').write_text('preserve')
            self.assertEqual(prepare_server.main(args), 0)
            self.assertEqual((output / 'user-file').read_text(), 'preserve')
        self.assertFalse((cache / 'prepare.lock').exists())

    def test_concurrent_setup_is_rejected_without_killing_lock_owner(self):
        with prepare_server.setup_lock(str(self.root)):
            with self.assertRaises(download.DownloadError), prepare_server.setup_lock(str(self.root)):
                self.fail('A concurrent setup acquired the lock')
            self.assertTrue(prepare_server.process_alive(os.getpid()))
        self.assertFalse((self.root / 'prepare.lock').exists())


if __name__ == '__main__':
    unittest.main()
