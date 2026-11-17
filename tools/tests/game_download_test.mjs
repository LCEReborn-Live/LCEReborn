import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import crypto from 'node:crypto';
import { deflateRawSync } from 'node:zlib';
import { createRequire } from 'node:module';
import { test } from 'node:test';
import { downloadArchive, extractArchive, driveConfirmation, safeEntryName, validateSource } from '../../live/game-files.js';

const require = createRequire(import.meta.url);
const crc32 = require('../../live/vendor/node_modules/yauzl/crc32.js');
const entries = [
  { name: 'game/code/main.rpx', data: Buffer.from('test game code') },
  { name: 'game/content/Common/data.bin', data: crypto.randomBytes(512 * 1024), method: 8 },
  { name: 'game/content/music/', data: Buffer.alloc(0) },
  { name: 'game/content/WiiU/', data: Buffer.alloc(0) },
  { name: 'game/meta/meta.xml', data: Buffer.from('<menu><title_id>00050000101D9D00</title_id><title_version>1</title_version></menu>') },
];

function makeZip(items, zip64 = false) {
  const local = [], central = [];
  let offset = 0;
  for (const entry of items) {
    const name = Buffer.from(entry.name), method = entry.method ?? 0;
    const compressed = method === 8 ? deflateRawSync(entry.data) : entry.data;
    const crc = entry.badCrc ? (crc32(entry.data) + 1) >>> 0 : crc32(entry.data);
    const header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034b50); header.writeUInt16LE(20, 4); header.writeUInt16LE(0x800, 6);
    header.writeUInt16LE(method, 8); header.writeUInt32LE(crc, 14);
    header.writeUInt32LE(compressed.length, 18); header.writeUInt32LE(entry.data.length, 22);
    header.writeUInt16LE(name.length, 26);
    const directory = Buffer.alloc(46);
    directory.writeUInt32LE(0x02014b50); directory.writeUInt16LE(0x0314, 4); directory.writeUInt16LE(20, 6);
    directory.writeUInt16LE(0x800, 8); directory.writeUInt16LE(method, 10); directory.writeUInt32LE(crc, 16);
    directory.writeUInt32LE(compressed.length, 20); directory.writeUInt32LE(entry.data.length, 24);
    directory.writeUInt16LE(name.length, 28); directory.writeUInt32LE(((entry.mode ?? 0x8000) << 16) >>> 0, 38);
    directory.writeUInt32LE(offset, 42);
    local.push(header, name, compressed); central.push(directory, name);
    offset += header.length + name.length + compressed.length;
  }
  const directory = Buffer.concat(central), end = Buffer.alloc(22), extra = [];
  end.writeUInt32LE(0x06054b50);
  if (zip64) {
    const record = Buffer.alloc(56), locator = Buffer.alloc(20);
    record.writeUInt32LE(0x06064b50); record.writeBigUInt64LE(44n, 4);
    record.writeUInt16LE(45, 12); record.writeUInt16LE(45, 14);
    record.writeBigUInt64LE(BigInt(items.length), 24); record.writeBigUInt64LE(BigInt(items.length), 32);
    record.writeBigUInt64LE(BigInt(directory.length), 40); record.writeBigUInt64LE(BigInt(offset), 48);
    locator.writeUInt32LE(0x07064b50); locator.writeBigUInt64LE(BigInt(offset + directory.length), 8);
    locator.writeUInt32LE(1, 16); extra.push(record, locator);
  }
  end.writeUInt16LE(zip64 ? 0xffff : items.length, 8); end.writeUInt16LE(zip64 ? 0xffff : items.length, 10);
  end.writeUInt32LE(zip64 ? 0xffffffff : directory.length, 12); end.writeUInt32LE(zip64 ? 0xffffffff : offset, 16);
  return Buffer.concat([...local, directory, ...extra, end]);
}

const zip = makeZip(entries);
const source = { url: 'https://example.invalid/archive.zip', size: zip.length,
  sha256: crypto.createHash('sha256').update(zip).digest('hex'),
  unpackedBytes: entries.reduce((sum, item) => sum + item.data.length, 0), titleId: '00050000101D9D00', version: 1 };
const temp = await fs.mkdtemp(path.join(os.tmpdir(), 'lce-download-test-'));
let calls = [];
const server = http.createServer((req, res) => {
  calls.push(req.headers.range ?? 'full');
  if (req.url === '/html') { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<html>Sign in</html>'); return; }
  let start = req.headers.range ? Number(req.headers.range.match(/\d+/)[0]) : 0;
  if (req.url === '/ignore') start = 0;
  const headers = { 'Content-Type': 'application/zip', 'Content-Length': zip.length - start };
  if (start) headers['Content-Range'] = `bytes ${start}-${zip.length - 1}/${zip.length + (req.url === '/bad-range' ? 1 : 0)}`;
  res.writeHead(start ? 206 : 200, headers);
  let offset = start;
  const timer = setInterval(() => {
    if (offset >= zip.length) { clearInterval(timer); res.end(); return; }
    const next = Math.min(offset + 16384, zip.length);
    res.write(zip.subarray(offset, next)); offset = next;
  }, 1);
  res.on('close', () => clearInterval(timer));
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const address = `http://127.0.0.1:${server.address().port}`;

test('Drive confirmation accepts the real form and rejects different IDs and hosts', () => {
  const form = '<form id="download-form" action="https://drive.usercontent.google.com/download?x=1&amp;y=2" method="get">'
    + '<input name="id" value="example"><input name="export" value="download"><input name="confirm" value="t"></form>';
  const url = driveConfirmation(form, 'example');
  assert.equal(url.searchParams.get('confirm'), 't'); assert.equal(url.searchParams.get('y'), '2');
  assert.throws(() => driveConfirmation(form, 'another'));
  assert.throws(() => driveConfirmation(form.replace('drive.usercontent.google.com', 'evil.invalid'), 'example'));
  assert.throws(() => driveConfirmation('<html>Sign in</html>', 'example'));
});

test('configuration and paths reject incomplete hashes, traversal, ADS, device names and ambiguous paths', () => {
  assert.equal(validateSource(source), source);
  for (const bad of ['', '00', 'f'.repeat(63)]) assert.throws(() => validateSource({ ...source, sha256: bad }));
  for (const bad of ['../outside', '/absolute', 'x/../outside', 'x\\outside', 'x:stream', 'CON.txt', 'x/NUL', 'x//y', 'x/./y', 'x/file.']) {
    assert.throws(() => safeEntryName(bad), bad);
  }
  assert.equal(safeEntryName('game/content/texture.png'), 'game/content/texture.png');
});

test('HTTP resume appends only the requested range and verifies SHA-256', async () => {
  const folder = path.join(temp, 'resume'); await fs.mkdir(folder);
  await fs.writeFile(path.join(folder, 'archive.zip.part'), zip.subarray(0, 40000));
  calls = [];
  const result = await downloadArchive({ ...source, url: address + '/zip' }, folder, { allowHttp: true });
  assert.deepEqual(await fs.readFile(result), zip); assert.equal(calls[0], 'bytes=40000-');
});

test('a server ignoring Range causes a clean restart rather than corrupt concatenation', async () => {
  const folder = path.join(temp, 'ignore'); await fs.mkdir(folder);
  await fs.writeFile(path.join(folder, 'archive.zip.part'), zip.subarray(0, 12345));
  const result = await downloadArchive({ ...source, url: address + '/ignore' }, folder, { allowHttp: true });
  assert.deepEqual(await fs.readFile(result), zip);
});

test('invalid ranges and HTML are rejected without accepting a bogus archive', async () => {
  const folder = path.join(temp, 'bad-range'); await fs.mkdir(folder);
  await fs.writeFile(path.join(folder, 'archive.zip.part'), zip.subarray(0, 12345));
  await assert.rejects(downloadArchive({ ...source, url: address + '/bad-range' }, folder, { allowHttp: true }), /resume range/);
  assert.equal((await fs.stat(path.join(folder, 'archive.zip.part'))).size, 12345);
  await assert.rejects(downloadArchive({ ...source, url: address + '/html' }, path.join(temp, 'html'), { allowHttp: true }), /web page/);
});

test('cancel preserves partial data, and retry resumes to a verified archive', async () => {
  const folder = path.join(temp, 'cancel'), controller = new AbortController();
  await fs.mkdir(folder);
  await fs.writeFile(path.join(folder, 'archive.zip.part'), zip.subarray(0, 16000));
  await assert.rejects(downloadArchive({ ...source, url: address + '/zip' }, folder, {
    allowHttp: true, signal: controller.signal,
    progress(phase, done) { if (phase === 'download' && done > 80000) controller.abort(); },
  }), { name: 'AbortError' });
  const size = (await fs.stat(path.join(folder, 'archive.zip.part'))).size;
  assert.ok(size >= 16000 && size < zip.length);
  await downloadArchive({ ...source, url: address + '/zip' }, folder, { allowHttp: true });
});

test('a checksum mismatch discards bad bytes and never extracts them', async () => {
  const folder = path.join(temp, 'bad-hash');
  await assert.rejects(downloadArchive({ ...source, sha256: '0'.repeat(64), url: address + '/zip' }, folder,
    { allowHttp: true }), /SHA-256/);
  await assert.rejects(fs.stat(path.join(folder, 'archive.zip')));
  await assert.rejects(fs.stat(path.join(folder, 'archive.zip.part')));
});

test('stored, deflated and ZIP64 archives extract with CRC and metadata verification', async () => {
  for (const zip64 of [false, true]) {
    const folder = path.join(temp, 'extract-' + zip64); await fs.mkdir(folder);
    const file = path.join(folder, 'test.zip'); await fs.writeFile(file, makeZip(entries, zip64));
    const root = await extractArchive(file, folder, source);
    assert.deepEqual(await fs.readFile(path.join(root, 'content/Common/data.bin')), entries[1].data);
    assert.equal(await extractArchive(file, folder, source), root);
  }
});

test('bad CRC, symlinks, duplicate paths, traversal, wrong version and oversized archives are rejected', async () => {
  const cases = [
    [...entries, { name: '../escape', data: Buffer.from('escape') }],
    entries.map((entry, i) => i === 1 ? { ...entry, badCrc: true } : entry),
    [...entries, { name: 'game/symlink', data: Buffer.from('../escape'), mode: 0xa000 }],
    [...entries, { name: 'game/CODE/main.rpx', data: Buffer.from('duplicate') }],
  ];
  for (const [index, items] of cases.entries()) {
    const folder = path.join(temp, 'reject-' + index); await fs.mkdir(folder);
    const file = path.join(folder, 'test.zip'); await fs.writeFile(file, makeZip(items));
    await assert.rejects(extractArchive(file, folder, { ...source, unpackedBytes: items.reduce((sum, item) => sum + item.data.length, 0) }));
    await assert.rejects(fs.stat(path.join(folder, 'ready.json')));
    assert.equal((await fs.readdir(folder)).filter(name => name.startsWith('unpacking-')).length, 0);
  }
  const folder = path.join(temp, 'metadata'); await fs.mkdir(folder);
  const file = path.join(folder, 'test.zip'); await fs.writeFile(file, zip);
  await assert.rejects(extractArchive(file, folder, { ...source, version: 688 }), /expected Minecraft/);
  await assert.rejects(extractArchive(file, folder, { ...source, unpackedBytes: 1 }), /expected unpacked/);
});

test('cleanup', async () => {
  await new Promise(resolve => server.close(resolve));
  // Only the mkdtemp-owned test directory is ever removed.
  await fs.rm(temp, { recursive: true, force: true });
});
