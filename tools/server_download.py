"""Streaming, verified ZIP downloads for headless server setup (stdlib only)."""
import hashlib
import http.client
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

MARGIN = 256 * 1024 * 1024
DRIVE_HOSTS = {'drive.google.com', 'drive.usercontent.google.com', 'docs.google.com'}


class DownloadError(Exception):
    pass


def filesystem_path(value):
    value = os.path.abspath(value)
    if os.name == 'nt' and not value.startswith('\\\\?\\'):
        return '\\\\?\\UNC\\' + value[2:] if value.startswith('\\\\') else '\\\\?\\' + value
    return value


def within(child, parent):
    try:
        return os.path.commonpath([os.path.realpath(child), os.path.realpath(parent)]) == os.path.realpath(parent)
    except ValueError:
        return False


class Confirmation(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.action = None
        self.fields = {}
        self.active = False

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'form' and attrs.get('id') == 'download-form':
            self.active = True
            self.action = attrs.get('action')
        if self.active and tag == 'input' and attrs.get('name') and attrs.get('value') is not None:
            self.fields[attrs['name']] = attrs['value']

    def handle_endtag(self, tag):
        if tag == 'form':
            self.active = False


def confirmation_url(text, file_id):
    form = Confirmation()
    form.feed(text)
    action = urllib.parse.urlparse(urllib.parse.urljoin('https://drive.google.com', form.action or ''))
    if not form.action or action.scheme != 'https' or action.hostname not in DRIVE_HOSTS \
            or action.path not in ('/download', '/uc') or action.username or action.password \
            or form.fields.get('id') != file_id or form.fields.get('export') != 'download' or not form.fields.get('confirm'):
        raise DownloadError('Google Drive did not provide the archive. Check sharing access or try again later.')
    query = {key: value for key, value in form.fields.items() if key in ('id', 'export', 'confirm', 'uuid', 'resourcekey')}
    return urllib.parse.urlunparse(action._replace(query=urllib.parse.urlencode(query)))


class SecureRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        if urllib.parse.urlparse(newurl).scheme != 'https':
            raise DownloadError('Refusing an insecure download redirect.')
        return super().redirect_request(request, fp, code, message, headers, newurl)


def open_download(source, offset=0, allow_test_http=False):
    url = source['url']
    parsed = urllib.parse.urlparse(url)
    file_id = None
    if parsed.hostname in DRIVE_HOSTS:
        found = re.match(r'/file/d/([\w-]+)', parsed.path)
        fields = urllib.parse.parse_qs(parsed.query)
        file_id = found.group(1) if found else fields.get('id', [None])[0]
        if not file_id:
            raise DownloadError('Invalid Google Drive file link.')
        query = {'export': 'download', 'id': file_id, 'confirm': 't'}
        if fields.get('resourcekey'):
            query['resourcekey'] = fields['resourcekey'][0]
        url = 'https://drive.usercontent.google.com/download?' + urllib.parse.urlencode(query)
    if parsed.scheme != 'https' and not (allow_test_http and parsed.scheme == 'http' and parsed.hostname == '127.0.0.1'):
        raise DownloadError('An HTTPS download URL is required.')
    opener = urllib.request.build_opener(SecureRedirect)
    for _ in range(5):
        headers = {'User-Agent': 'LCEReborn-Server-Setup/1', 'Accept-Encoding': 'identity'}
        if offset:
            headers['Range'] = 'bytes=%d-' % offset
        response = opener.open(urllib.request.Request(url, headers=headers), timeout=60)
        if 'text/html' not in response.headers.get('Content-Type', '').lower():
            return response
        with response:
            body = response.read(256 * 1024 + 1)
        if len(body) > 256 * 1024 or not file_id:
            raise DownloadError('The server returned a web page instead of a ZIP archive.')
        url = confirmation_url(body.decode('utf-8', 'replace'), file_id)
    raise DownloadError('Too many download confirmations. Try again later.')


def validate_source(source):
    if not re.fullmatch(r'[a-f0-9]{64}', source.get('sha256', '')) \
            or not isinstance(source.get('size'), int) or not 0 < source['size'] <= 64 * 1024 ** 3 \
            or not isinstance(source.get('unpackedBytes'), int) or not 0 < source['unpackedBytes'] <= 128 * 1024 ** 3:
        raise DownloadError('Invalid game download manifest.')


def enough_space(folder, required):
    if shutil.disk_usage(folder).free < required + MARGIN:
        raise DownloadError('Not enough free disk space: %.1f GiB is required.' % ((required + MARGIN) / 1024 ** 3))


def sha256(file, progress=None):
    digest = hashlib.sha256()
    done = 0
    total = os.path.getsize(file)
    if progress:
        progress('Verifying SHA-256', 0, total)
    with open(file, 'rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
            done += len(block)
            if progress:
                progress('Verifying SHA-256', done, total)
    return digest.hexdigest()


def download_archive(source, folder, progress=None, allow_test_http=False):
    validate_source(source)
    folder = Path(filesystem_path(folder))
    folder.mkdir(parents=True, exist_ok=True)
    archive, partial = folder / 'archive.zip', folder / 'archive.zip.part'
    if archive.is_file():
        if archive.stat().st_size == source['size'] and sha256(archive, progress) == source['sha256']:
            return archive
        archive.unlink()
    for attempt in range(3):
        offset = partial.stat().st_size if partial.exists() else 0
        if offset > source['size']:
            partial.unlink()
            offset = 0
        try:
            if offset < source['size']:
                enough_space(folder, source['size'] - offset)
                with open_download(source, offset, allow_test_http) as response:
                    if response.status == 200:
                        offset = 0
                    elif response.status == 206:
                        found = re.fullmatch(r'bytes (\d+)-(\d+)/(\d+)', response.headers.get('Content-Range', ''))
                        if not found or tuple(map(int, found.groups())) != (offset, source['size'] - 1, source['size']):
                            raise DownloadError('The server returned an invalid resume range.')
                    else:
                        raise DownloadError('Download server returned HTTP %s.' % response.status)
                    length = response.headers.get('Content-Length')
                    if length is not None and int(length) != source['size'] - offset:
                        raise DownloadError('The archive size changed. Update the server package.')
                    enough_space(folder, source['size'] - offset)
                    with partial.open('ab' if offset else 'wb') as output:
                        while True:
                            block = response.read(1024 * 1024)
                            if not block:
                                break
                            if offset + len(block) > source['size']:
                                raise DownloadError('The download exceeded its expected size.')
                            output.write(block)
                            offset += len(block)
                            if progress:
                                progress('Downloading', offset, source['size'])
                if offset != source['size']:
                    raise OSError('Incomplete download')
            if sha256(partial, progress) != source['sha256']:
                partial.unlink()
                raise DownloadError('Archive SHA-256 verification failed. Retry the download.')
            partial.replace(archive)
            return archive
        except DownloadError:
            raise
        except (OSError, urllib.error.URLError, http.client.HTTPException) as error:
            if attempt == 2:
                raise DownloadError('Download interrupted. Run prepare-server again to resume: %s' % error) from error
            time.sleep(attempt + 1)


def safe_name(value):
    if not value or len(value) > 1000 or value.startswith('/') or re.search(r'[\\:\x00-\x1f\x7f]', value):
        raise DownloadError('Unsafe ZIP file name.')
    parts = value.rstrip('/').split('/')
    for part in parts:
        if part in ('', '.', '..') or part.endswith(('.', ' ')) or re.match(r'^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)', part, re.I):
            raise DownloadError('Unsafe ZIP file name.')
    return '/'.join(parts)


def unpack_archive(archive, folder, source, check_dump, progress=None):
    folder = Path(filesystem_path(folder))
    ready, marker = folder / 'unpacked', folder / 'ready.json'
    try:
        record = json.loads(marker.read_text(encoding='utf-8'))
        relative = safe_name(record['root']) if record['root'] else ''
        root = ready / relative
        if record['sha256'] == source['sha256']:
            check_dump(str(root))
            return str(root)
    except (OSError, ValueError, KeyError, DownloadError):
        pass
    with zipfile.ZipFile(archive) as compressed:
        entries = compressed.infolist()
        names, total, metadata = set(), 0, []
        if len(entries) > 100000:
            raise DownloadError('Too many files in ZIP archive.')
        for entry in entries:
            name = safe_name(entry.filename)
            mode = stat.S_IFMT(entry.external_attr >> 16)
            if name.casefold() in names or mode not in (0, stat.S_IFREG, stat.S_IFDIR) \
                    or entry.flag_bits & 1 or entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                raise DownloadError('ZIP contains duplicate paths, links, encryption or an unsupported compression method.')
            names.add(name.casefold())
            total += entry.file_size
            if total > source['unpackedBytes']:
                raise DownloadError('ZIP exceeds its expected unpacked size.')
            if name == 'meta/meta.xml' or name.endswith('/meta/meta.xml'):
                metadata.append(name[:-len('meta/meta.xml')].rstrip('/'))
        if total != source['unpackedBytes'] or len(metadata) != 1:
            raise DownloadError('The game archive is incomplete or contains multiple dumps.')
        enough_space(folder, total * 2)
        with tempfile.TemporaryDirectory(prefix='unpacking-', dir=folder) as temporary:
            if not within(temporary, folder):
                raise DownloadError('Invalid extraction directory.')
            done = 0
            for entry in entries:
                destination = Path(temporary, safe_name(entry.filename))
                if entry.is_dir():
                    destination.mkdir(parents=True, exist_ok=True)
                    continue
                destination.parent.mkdir(parents=True, exist_ok=True)
                count = 0
                with compressed.open(entry) as input_file, destination.open('xb') as output_file:
                    while True:
                        block = input_file.read(1024 * 1024)
                        if not block:
                            break
                        count += len(block)
                        if count > entry.file_size:
                            raise DownloadError('Invalid unpacked file size.')
                        output_file.write(block)
                        done += len(block)
                        if progress:
                            progress('Unpacking', done, total)
                if count != entry.file_size:
                    raise DownloadError('Incomplete unpacked file.')
            check_dump(str(Path(temporary, metadata[0])))
            if ready.exists():
                if not within(ready, folder):
                    raise DownloadError('Invalid cached extraction path.')
                shutil.rmtree(ready)
            # Move the checked output out of the temporary directory before cleanup.
            os.rename(temporary, ready)
            Path(temporary).mkdir()
        marker.write_text(json.dumps({'sha256': source['sha256'], 'root': metadata[0]}), encoding='utf-8')
        return str(ready / metadata[0])
