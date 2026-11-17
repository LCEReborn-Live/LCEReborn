// First-run game data installer. Uses the Node runtime already shipped with Live.
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import https from 'node:https';
import http from 'node:http';
import crypto from 'node:crypto';
import * as zlib from 'node:zlib';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { Transform } from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { setTimeout as delay } from 'node:timers/promises';

const require = createRequire(import.meta.url);
const yauzl = require('./vendor/node_modules/yauzl');
const CRC_TABLE = Uint32Array.from({ length: 256 }, (_, index) => {
  let value = index;
  for (let bit = 0; bit < 8; ++bit) value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  return value >>> 0;
});
const crc32 = zlib.crc32 ?? ((buffer, previous = 0) => {
  let value = previous ^ 0xffffffff;
  for (const byte of buffer) value = CRC_TABLE[(value ^ byte) & 255] ^ (value >>> 8);
  return (value ^ 0xffffffff) >>> 0;
});
const MARGIN = 256 * 1024 * 1024;
const DRIVE_HOSTS = new Set(['drive.google.com', 'docs.google.com', 'drive.usercontent.google.com']);

export function validateSource(source) {
  if (!source || !Number.isSafeInteger(source.size) || source.size <= 0 || source.size > 64 * 1024 ** 3
      || !Number.isSafeInteger(source.unpackedBytes) || source.unpackedBytes <= 0
      || source.unpackedBytes > 128 * 1024 ** 3 || !/^[a-f0-9]{64}$/.test(source.sha256 ?? '')
      || !/^[a-fA-F0-9]{16}$/.test(source.titleId ?? '') || !Number.isInteger(source.version)) {
    throw new Error('The game download configuration is incomplete. Update the launcher or import local files.');
  }
  const url = new URL(source.url);
  if (url.protocol !== 'https:' || url.username || url.password) throw new Error('An HTTPS download URL is required.');
  return source;
}

function driveId(url) {
  if (!DRIVE_HOSTS.has(url.hostname)) return null;
  return url.pathname.match(/^\/file\/d\/([\w-]+)/)?.[1] ?? url.searchParams.get('id');
}

function allowedDriveHost(host) {
  return DRIVE_HOSTS.has(host) || host.endsWith('.googleusercontent.com');
}

function entity(text) {
  return text.replace(/&(?:amp|quot|apos|lt|gt|#\d+|#x[a-f\d]+);/gi, value => {
    const named = { '&amp;': '&', '&quot;': '"', '&apos;': "'", '&lt;': '<', '&gt;': '>' };
    if (named[value.toLowerCase()]) return named[value.toLowerCase()];
    const hex = value.toLowerCase().startsWith('&#x');
    const code = Number.parseInt(value.slice(hex ? 3 : 2, -1), hex ? 16 : 10);
    return code <= 0x10ffff ? String.fromCodePoint(code) : '';
  });
}

export function driveConfirmation(html, expectedId) {
  const form = html.match(/<form\b[^>]*\bid=["']download-form["'][^>]*>[\s\S]*?<\/form>/i)?.[0];
  const action = form?.match(/\baction=["']([^"']+)["']/i)?.[1];
  if (!action) throw new Error('Google Drive did not provide the archive. Check link sharing or try again later.');
  const url = new URL(entity(action), 'https://drive.google.com');
  if (url.protocol !== 'https:' || !DRIVE_HOSTS.has(url.hostname)
      || !['/download', '/uc'].includes(url.pathname) || url.username || url.password) {
    throw new Error('Google Drive returned an unexpected download address.');
  }
  const fields = new Map();
  for (const input of form.matchAll(/<input\b[^>]*>/gi)) {
    const name = input[0].match(/\bname=["']([^"']+)["']/i)?.[1];
    const value = input[0].match(/\bvalue=["']([^"']*)["']/i)?.[1];
    if (name && value !== undefined) fields.set(entity(name), entity(value));
  }
  if (fields.get('id') !== expectedId || fields.get('export') !== 'download' || !fields.get('confirm')) {
    throw new Error('Google Drive returned an invalid download confirmation.');
  }
  for (const key of ['id', 'export', 'confirm', 'uuid', 'resourcekey']) {
    if (fields.has(key)) url.searchParams.set(key, fields.get(key));
  }
  return url;
}

function request(url, headers, signal, allowHttp) {
  if (url.protocol !== 'https:' && !(allowHttp && url.protocol === 'http:' && url.hostname === '127.0.0.1')) {
    throw new Error('Refusing an insecure download redirect.');
  }
  return new Promise((resolve, reject) => {
    const client = url.protocol === 'https:' ? https : http;
    const req = client.get(url, { headers, signal }, response => {
      // A cancellation can arrive while disk-space checks precede pipeline().
      response.on('error', () => {});
      resolve(response);
    });
    req.setTimeout(60000, () => req.destroy(new Error('The download timed out. Try again to resume.')));
    req.on('error', reject);
  });
}

async function boundedBody(response) {
  const blocks = [];
  let size = 0;
  for await (const block of response) {
    size += block.length;
    if (size > 256 * 1024) {
      response.destroy();
      throw new Error('The download returned a web page instead of a ZIP archive.');
    }
    blocks.push(block);
  }
  return Buffer.concat(blocks).toString('utf8');
}

async function openDownload(source, headers, options) {
  let url = new URL(source.url);
  const id = driveId(url);
  if (id) {
    const resourceKey = url.searchParams.get('resourcekey');
    url = new URL('https://drive.google.com/uc');
    url.searchParams.set('export', 'download');
    url.searchParams.set('id', id);
    if (resourceKey) url.searchParams.set('resourcekey', resourceKey);
  }
  for (let redirects = 0; redirects < 10; ++redirects) {
    options.signal?.throwIfAborted();
    if (id && !allowedDriveHost(url.hostname)) throw new Error('Unexpected Google Drive redirect.');
    const response = await request(url, { 'User-Agent': 'LCEReborn-Live-Installer/1', 'Accept-Encoding': 'identity',
      ...headers }, options.signal, options.allowHttp);
    if ([301, 302, 303, 307, 308].includes(response.statusCode)) {
      response.resume();
      if (!response.headers.location) throw new Error('The download redirect has no address.');
      url = new URL(response.headers.location, url);
      continue;
    }
    if ((response.headers['content-type'] ?? '').toLowerCase().includes('text/html')) {
      const body = await boundedBody(response);
      if (!id) throw new Error('The download returned a web page instead of a ZIP archive.');
      url = driveConfirmation(body, id);
      continue;
    }
    return response;
  }
  throw new Error('Too many download redirects. Try again later or import local files.');
}

async function hasFile(file) {
  try { return (await fsp.stat(file)).isFile(); } catch (error) {
    if (error.code === 'ENOENT') return false;
    throw error;
  }
}

async function freeSpace(folder, required) {
  const stats = await fsp.statfs(folder, { bigint: true });
  if (stats.bavail * stats.bsize < BigInt(required + MARGIN)) {
    throw new Error(`Not enough free disk space. At least ${((required + MARGIN) / 1024 ** 3).toFixed(1)} GiB is needed.`);
  }
}

async function hashFile(file, options) {
  const digest = crypto.createHash('sha256');
  let bytes = 0;
  for await (const block of fs.createReadStream(file, { signal: options.signal })) {
    digest.update(block);
    bytes += block.length;
    options.progress?.('verify', bytes);
  }
  return digest.digest('hex');
}

export async function downloadArchive(source, folder, options = {}) {
  await fsp.mkdir(folder, { recursive: true, mode: 0o700 });
  const archive = path.join(folder, 'archive.zip');
  const partial = archive + '.part';
  if (await hasFile(archive)) {
    if ((await fsp.stat(archive)).size === source.size && await hashFile(archive, options) === source.sha256) return archive;
    await fsp.unlink(archive);
  }
  for (let attempt = 0; attempt < 3; ++attempt) {
    options.signal?.throwIfAborted();
    let offset = await hasFile(partial) ? (await fsp.stat(partial)).size : 0;
    if (offset > source.size) { await fsp.unlink(partial); offset = 0; }
    try {
      if (offset < source.size) {
        await freeSpace(folder, source.size - offset);
        const response = await openDownload(source, offset ? { Range: `bytes=${offset}-` } : {}, options);
        if (response.statusCode === 200) offset = 0;
        else if (response.statusCode === 206) {
          const range = /^bytes (\d+)-(\d+)\/(\d+)$/.exec(response.headers['content-range'] ?? '');
          if (!range || Number(range[1]) !== offset || Number(range[2]) !== source.size - 1
              || Number(range[3]) !== source.size) {
            response.destroy();
            throw new Error('The server returned an invalid resume range.');
          }
        } else {
          response.destroy();
          throw new Error(`The download server returned HTTP ${response.statusCode}. Try again later.`);
        }
        const length = response.headers['content-length'];
        if (length !== undefined && Number(length) !== source.size - offset) {
          response.destroy();
          throw new Error('The archive size has changed. Update the launcher or import local files.');
        }
        await freeSpace(folder, source.size - offset);
        const limiter = new Transform({ transform(block, encoding, callback) {
          offset += block.length;
          if (offset > source.size) return callback(new Error('The download exceeded the expected size.'));
          options.progress?.('download', offset);
          callback(null, block);
        } });
        await pipeline(response, limiter, fs.createWriteStream(partial, { flags: offset ? 'a' : 'w', mode: 0o600 }),
          { signal: options.signal });
        if (offset !== source.size) throw new Error('The download was interrupted. Try again to resume.');
      }
      if (await hashFile(partial, options) !== source.sha256) {
        await fsp.unlink(partial);
        throw new Error('The archive failed its SHA-256 check. The incomplete download was discarded; try again.');
      }
      await fsp.rename(partial, archive);
      return archive;
    } catch (error) {
      options.signal?.throwIfAborted();
      if (attempt === 2 || !/ECONNRESET|ETIMEDOUT|timed out|interrupted|premature close/i.test(error.code + ' ' + error.message)) throw error;
      options.info?.('Connection interrupted. Resuming the download...');
      await delay(1000 * (attempt + 1), undefined, { signal: options.signal });
    }
  }
}

export function safeEntryName(name) {
  if (!name || name.length > 1000 || /[\\:\x00-\x1f\x7f]/.test(name) || name.startsWith('/')) {
    throw new Error('The ZIP contains an unsafe file name.');
  }
  const parts = name.replace(/\/$/, '').split('/');
  for (const part of parts) {
    if (!part || part === '.' || part === '..' || /[. ]$/.test(part)
        || /^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part)) {
      throw new Error('The ZIP contains an unsafe file name.');
    }
  }
  return parts.join('/');
}

async function dumpMetadata(root, source) {
  const metaPath = path.join(root, 'meta', 'meta.xml');
  if ((await fsp.stat(metaPath)).size > 1024 * 1024) throw new Error('Invalid game metadata size.');
  const text = await fsp.readFile(metaPath, 'utf8');
  const title = text.match(/<title_id\b[^>]*>\s*([a-fA-F0-9]{16})\s*</)?.[1];
  const version = Number(text.match(/<title_version\b[^>]*>\s*(\d+)\s*</)?.[1]);
  if (title?.toLowerCase() !== source.titleId.toLowerCase() || version !== source.version) {
    throw new Error('The archive is not the expected Minecraft: Wii U Edition game or update version.');
  }
  for (const name of ['code', 'content', 'meta']) {
    if (!(await fsp.lstat(path.join(root, name))).isDirectory()) throw new Error('The game archive is incomplete.');
  }
  if (source.version !== 688) {
    for (const name of ['Common', 'music', 'WiiU']) {
      if (!(await fsp.lstat(path.join(root, 'content', name))).isDirectory()) {
        throw new Error('The base game archive is incomplete.');
      }
    }
  }
}

export async function extractArchive(archive, folder, source, options = {}) {
  const ready = path.join(folder, 'unpacked');
  const marker = path.join(folder, 'ready.json');
  try {
    const record = JSON.parse(await fsp.readFile(marker, 'utf8'));
    if (record.sha256 === source.sha256) {
      const relative = record.root ? safeEntryName(record.root) : '';
      const root = path.join(ready, relative);
      await dumpMetadata(root, source);
      return root;
    }
  } catch { /* A missing or interrupted extraction is rebuilt from the verified archive. */ }
  const zip = await yauzl.openPromise(archive, { lazyEntries: true, autoClose: false, strictFileNames: true });
  let temp;
  try {
    const entries = [];
    const names = new Set();
    let unpacked = 0;
    let metadata;
    for await (const entry of zip.eachEntry()) {
      options.signal?.throwIfAborted();
      const relative = safeEntryName(entry.fileName);
      const key = relative.toLowerCase();
      const mode = (entry.externalFileAttributes >>> 16) & 0xf000;
      if (names.has(key) || (mode !== 0 && mode !== 0x8000 && mode !== 0x4000)
          || (entry.generalPurposeBitFlag & 1) || ![0, 8].includes(entry.compressionMethod)) {
        throw new Error('The ZIP contains duplicate paths, links, encryption or an unsupported compression method.');
      }
      names.add(key);
      unpacked += entry.uncompressedSize;
      if (!Number.isSafeInteger(unpacked) || unpacked > source.unpackedBytes || entries.length >= 100000) {
        throw new Error('The ZIP exceeds its expected unpacked size.');
      }
      if (relative === 'meta/meta.xml' || relative.endsWith('/meta/meta.xml')) {
        if (metadata !== undefined) throw new Error('The ZIP contains more than one game dump.');
        metadata = relative.slice(0, -'meta/meta.xml'.length).replace(/\/$/, '');
      }
      entries.push(entry);
    }
    if (unpacked !== source.unpackedBytes || metadata === undefined) throw new Error('The game archive is incomplete.');
    await freeSpace(folder, unpacked * 2);
    temp = await fsp.mkdtemp(path.join(folder, 'unpacking-'));
    let done = 0;
    for (const entry of entries) {
      options.signal?.throwIfAborted();
      const target = path.join(temp, safeEntryName(entry.fileName));
      if (entry.fileName.endsWith('/')) { await fsp.mkdir(target, { recursive: true }); continue; }
      await fsp.mkdir(path.dirname(target), { recursive: true });
      let checksum = 0;
      let count = 0;
      const check = new Transform({ transform(block, encoding, callback) {
        count += block.length;
        if (count > entry.uncompressedSize) return callback(new Error('Invalid unpacked file size.'));
        checksum = crc32(block, checksum) >>> 0;
        done += block.length;
        options.progress?.('extract', done, unpacked);
        callback(null, block);
      } });
      await pipeline(await zip.openReadStreamPromise(entry), check, fs.createWriteStream(target, { flags: 'wx', mode: 0o600 }),
        { signal: options.signal });
      if (count !== entry.uncompressedSize || checksum !== entry.crc32) throw new Error('A file in the ZIP failed its CRC check.');
    }
    await dumpMetadata(path.join(temp, metadata), source);
    await fsp.rm(ready, { recursive: true, force: true });
    await fsp.rename(temp, ready);
    temp = undefined;
    await fsp.writeFile(marker, JSON.stringify({ sha256: source.sha256, root: metadata }));
    return path.join(ready, metadata);
  } finally {
    zip.close();
    if (temp) await fsp.rm(temp, { recursive: true, force: true });
  }
}

async function acquireLock(folder) {
  const file = path.join(folder, 'download.lock');
  for (let attempt = 0; attempt < 2; ++attempt) {
    try {
      const handle = await fsp.open(file, 'wx', 0o600);
      try { await handle.writeFile(JSON.stringify({ pid: process.pid })); }
      catch (error) { await handle.close(); await fsp.unlink(file); throw error; }
      return async () => { await handle.close(); await fsp.unlink(file); };
    } catch (error) {
      if (error.code !== 'EEXIST') throw error;
      let pid;
      try { pid = JSON.parse(await fsp.readFile(file, 'utf8')).pid; } catch { /* Do not take an incomplete live lock. */ }
      if (!Number.isInteger(pid) || pid <= 0) throw new Error('Another game download may already be running.');
      try { process.kill(pid, 0); throw new Error('Another game download is already running.'); }
      catch (check) { if (check.code !== 'ESRCH') throw check; }
      await fsp.unlink(file);
    }
  }
  throw new Error('The game download is already running.');
}

async function main() {
  const args = new Map();
  for (let i = 2; i < process.argv.length; i += 2) args.set(process.argv[i], process.argv[i + 1]);
  const manifest = JSON.parse(await fsp.readFile(args.get('--manifest'), 'utf8'));
  if (manifest.schema !== 1 || !['base', 'update'].includes(args.get('--key'))) throw new Error('Invalid game download configuration.');
  const source = validateSource(manifest[args.get('--key')]);
  const folder = path.resolve(args.get('--cache'), source.sha256);
  await fsp.mkdir(folder, { recursive: true, mode: 0o700 });
  const release = await acquireLock(folder);
  const controller = new AbortController();
  const emit = value => process.stdout.write(JSON.stringify(value) + '\n');
  let previous = 0;
  let previousPhase;
  const options = {
    signal: controller.signal,
    info: message => emit({ type: 'info', message }),
    progress(phase, done, total = source.size) {
      const now = Date.now();
      if (phase !== previousPhase || now - previous >= 150 || done === total) {
        emit({ type: 'progress', phase, done, total });
        previous = now;
        previousPhase = phase;
      }
    },
  };
  process.stdin.setEncoding('utf8');
  let input = '';
  process.stdin.on('data', block => {
    input = (input + block).slice(-128);
    if (input.includes('cancel\n')) controller.abort();
  });
  const cancel = () => controller.abort();
  process.on('SIGTERM', cancel);
  process.on('SIGINT', cancel);
  try {
    const archive = await downloadArchive(source, folder, options);
    const root = await extractArchive(archive, folder, source, options);
    emit({ type: 'ready', path: root });
  } finally {
    await release();
    process.stdin.pause();
    process.stdin.unref?.();
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => {
    process.stdout.write(JSON.stringify({ type: 'error', message: error.name === 'AbortError' ? 'Cancelled.' : error.message }) + '\n');
    process.stdin.pause();
    process.stdin.unref?.();
    process.exitCode = 1;
  });
}
