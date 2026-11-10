import { createServer } from 'node:http';
import { randomBytes, scrypt, createHash, timingSafeEqual } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { mkdirSync, readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { isIP } from 'node:net';
import { Relay } from './relay.js';

const SESSION_MS = 365 * 24 * 60 * 60 * 1000;
const PRESENCE_MS = 90 * 1000;
const LISTING_MS = 5 * 60 * 1000;
const MAX_BODY = 16 * 1024;
const STATIC_FILES = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/account', ['index.html', 'text/html; charset=utf-8']],
  ['/authorize', ['authorize.html', 'text/html; charset=utf-8']],
  ['/authorize.js', ['authorize.js', 'text/javascript; charset=utf-8']],
  ['/app.js', ['app.js', 'text/javascript; charset=utf-8']],
  ['/skinview3d.js', ['skinview3d.js', 'text/javascript; charset=utf-8']],
  ['/styles.css', ['styles.css', 'text/css; charset=utf-8']],
  ['/favicon.svg', ['favicon.svg', 'image/svg+xml']]
]);
const LOGIN_WINDOW_MS = 15 * 60 * 1000;
const MAX_LOGIN_FAILURES = 10;
const VISIBILITIES = new Set(['public', 'friends', 'friends_of_friends', 'invite_only']);
const LEADERBOARD_COLUMNS = new Map([['traveling', 4], ['mining', 7], ['farming', 6], ['kills', 7]]);

function passwordHash(password, salt) {
  return new Promise((resolve, reject) => {
    scrypt(password, salt, 64, (error, hash) => error ? reject(error) : resolve(hash));
  });
}

function fail(status, message) {
  const error = new Error(message);
  error.status = status;
  throw error;
}

function cleanName(value) {
  if (typeof value !== 'string' || !/^[a-zA-Z0-9_]{3,20}$/.test(value)) fail(400, 'Username must be 3–20 letters, numbers or underscores');
  return value;
}

function cleanPassword(value) {
  if (typeof value !== 'string' || value.length < 10 || Buffer.byteLength(value) > 256) fail(400, 'Password must be 10–256 bytes');
  return value;
}

function routeId(value) {
  const id = Number(value);
  if (!Number.isSafeInteger(id) || id < 1) fail(400, 'Invalid user ID');
  return id;
}

function visibility(value, fallback) {
  const selected = value === undefined ? fallback : value;
  if (!VISIBILITIES.has(selected)) fail(400, 'Invalid visibility');
  return selected;
}

function publicUser(user, online = false) {
  return { id: user.id, username: user.username, online };
}

function tokenHash(token) {
  return createHash('sha256').update(token).digest('hex');
}

function readBody(request) {
  return new Promise((done, reject) => {
    let size = 0;
    const chunks = [];
    let tooLarge = false;
    request.on('data', chunk => {
      size += chunk.length;
      if (size > MAX_BODY) tooLarge = true;
      else if (!tooLarge) chunks.push(chunk);
    });
    request.on('end', () => {
      if (tooLarge) return reject(Object.assign(new Error('Request body too large'), { status: 413 }));
      try {
        const body = chunks.length ? JSON.parse(Buffer.concat(chunks).toString('utf8')) : {};
        if (!body || typeof body !== 'object' || Array.isArray(body)) throw new Error('Invalid JSON object');
        done(body);
      }
      catch { reject(Object.assign(new Error('Invalid JSON'), { status: 400 })); }
    });
    request.on('error', reject);
  });
}

function readBinary(request, limit = 512 * 1024) {
  return new Promise((done, reject) => {
    let size = 0;
    let tooLarge = false;
    const chunks = [];
    request.on('data', chunk => {
      size += chunk.length;
      if (size > limit) tooLarge = true;
      else if (!tooLarge) chunks.push(chunk);
    });
    request.on('end', () => tooLarge
      ? reject(Object.assign(new Error('Image is too large'), { status: 413 }))
      : done(Buffer.concat(chunks)));
    request.on('error', reject);
  });
}

function validatePng(data, kind) {
  const expected = kind === 'skin' ? [64, 64] : [64, 32];
  if (data.length < 24 || data.length > 512 * 1024
      || !data.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))
      || data.toString('ascii', 12, 16) !== 'IHDR'
      || data.readUInt32BE(16) !== expected[0] || data.readUInt32BE(20) !== expected[1]) {
    fail(400, kind === 'skin' ? 'Skin must be a 64×64 PNG' : 'Cape must be a 64×32 PNG');
  }
}

export function createApp(databasePath = resolve('data/live.db'), options = {}) {
  mkdirSync(dirname(databasePath), { recursive: true });
  const db = new DatabaseSync(databasePath);
  const webOrigins = options.webOrigins || (process.env.LIVE_WEB_ORIGINS || '').split(',').map(origin => origin.trim()).filter(Boolean);
  const trustProxy = options.trustProxy ?? (process.env.LIVE_TRUST_PROXY === '1');
  db.exec(`PRAGMA foreign_keys = ON;
    PRAGMA journal_mode = WAL;
    CREATE TABLE IF NOT EXISTS users (
      id INTEGER PRIMARY KEY, username TEXT NOT NULL COLLATE NOCASE UNIQUE,
      salt TEXT NOT NULL, password_hash TEXT NOT NULL, last_seen INTEGER NOT NULL DEFAULT 0,
      presence_status TEXT NOT NULL DEFAULT 'launcher', game_activity TEXT NOT NULL DEFAULT ''
    );
    CREATE TABLE IF NOT EXISTS user_cosmetics (
      user_id INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
      skin_png BLOB, cape_png BLOB, updated_at INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS sessions (
      token_hash TEXT PRIMARY KEY, user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      expires_at INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS friend_requests (
      sender_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      recipient_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      PRIMARY KEY(sender_id, recipient_id), CHECK(sender_id != recipient_id)
    );
    CREATE TABLE IF NOT EXISTS friendships (
      user_a INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      user_b INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      PRIMARY KEY(user_a, user_b), CHECK(user_a < user_b)
    );
    CREATE TABLE IF NOT EXISTS listings (
      owner_id INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
      name TEXT NOT NULL, address TEXT NOT NULL, port INTEGER NOT NULL,
      updated_at INTEGER NOT NULL, relay_room_id TEXT,
      visibility TEXT NOT NULL DEFAULT 'public',
      direct_ticketed INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS game_invites (
      owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      recipient_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      expires_at INTEGER NOT NULL,
      PRIMARY KEY(owner_id, recipient_id)
    );
    CREATE TABLE IF NOT EXISTS game_members (
      owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      PRIMARY KEY(owner_id, user_id)
    );
    CREATE TABLE IF NOT EXISTS join_tickets (
      token_hash TEXT PRIMARY KEY,
      owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      guest_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      expires_at INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS device_authorizations (
      device_hash TEXT PRIMARY KEY,
      user_code TEXT NOT NULL UNIQUE,
      status TEXT NOT NULL DEFAULT 'pending',
      user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
      expires_at INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS leaderboard_scores (
      user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      board TEXT NOT NULL,
      values_json TEXT NOT NULL,
      updated_at INTEGER NOT NULL,
      PRIMARY KEY(user_id, board)
    );`);
  if (!db.prepare('PRAGMA table_info(listings)').all().some(column => column.name === 'relay_room_id')) {
    db.exec('ALTER TABLE listings ADD COLUMN relay_room_id TEXT');
  }
  if (!db.prepare('PRAGMA table_info(listings)').all().some(column => column.name === 'visibility')) {
    db.exec("ALTER TABLE listings ADD COLUMN visibility TEXT NOT NULL DEFAULT 'public'");
  }
  if (!db.prepare('PRAGMA table_info(listings)').all().some(column => column.name === 'direct_ticketed')) {
    db.exec('ALTER TABLE listings ADD COLUMN direct_ticketed INTEGER NOT NULL DEFAULT 0');
  }

  const query = (sql, ...params) => db.prepare(sql).get(...params);
  const all = (sql, ...params) => db.prepare(sql).all(...params);
  const run = (sql, ...params) => db.prepare(sql).run(...params);
  for (const [column, definition] of [['presence_status', "TEXT NOT NULL DEFAULT 'launcher'"], ['game_activity', "TEXT NOT NULL DEFAULT ''"]]) {
    if (!db.prepare('PRAGMA table_info(users)').all().some(item => item.name === column))
      db.exec(`ALTER TABLE users ADD COLUMN ${column} ${definition}`);
  }
  const eventClients = new Map();
  const eventTickets = new Map();
  const writeEvent = (userId, kind = 'refresh') => {
    const clients = eventClients.get(userId);
    if (!clients) return;
    const message = `event: update\ndata: ${JSON.stringify({ kind })}\n\n`;
    for (const client of clients) {
      try { client.response.write(message); } catch { client.response.destroy(); }
    }
  };
  const notifyUsers = (userIds, kind = 'refresh') => {
    for (const userId of new Set(userIds)) writeEvent(Number(userId), kind);
  };
  const notifyAll = (kind = 'refresh') => {
    for (const userId of eventClients.keys()) writeEvent(userId, kind);
  };
  const friendIds = userId => all(`SELECT CASE WHEN user_a = ? THEN user_b ELSE user_a END AS id
      FROM friendships WHERE user_a = ? OR user_b = ?`, userId, userId, userId);
  const notifyFriendsAndSelf = (userId, kind = 'refresh') => {
    const friends = friendIds(userId);
    notifyUsers([userId, ...friends.map(friend => friend.id)], kind);
  };
  const presenceStates = new Map(all('SELECT id, last_seen, presence_status, game_activity FROM users')
    .map(user => [Number(user.id), `${user.last_seen >= Date.now() - PRESENCE_MS}:${user.presence_status}:${user.game_activity}`]));
  const updatePresenceState = (userId, lastSeen, status, activity) => {
    const current = lastSeen >= Date.now() - PRESENCE_MS;
    const previous = presenceStates.get(userId);
    const next = `${current}:${status || 'launcher'}:${activity || ''}`;
    presenceStates.set(userId, next);
    if (previous !== undefined && previous !== next) notifyFriendsAndSelf(userId);
  };
  const presenceExpiryTimer = setInterval(() => {
    const now = Date.now();
    for (const user of all('SELECT id, last_seen, presence_status, game_activity FROM users'))
      updatePresenceState(Number(user.id), user.last_seen, user.presence_status, user.game_activity);
  }, 10000);
  presenceExpiryTimer.unref?.();

  const listingStates = new Map();
  const listingSweep = () => {
    const now = Date.now();
    const next = new Map();
    for (const listing of all('SELECT owner_id, updated_at, relay_room_id FROM listings')) {
      const relayEndpoint = relay?.connectedRoomFor(Number(listing.owner_id)) || null;
      const available = listing.relay_room_id ? Boolean(relayEndpoint)
        : listing.updated_at > now - LISTING_MS;
      next.set(Number(listing.owner_id), available);
    }
    let changed = next.size !== listingStates.size;
    if (!changed) for (const [ownerId, available] of next) {
      if (listingStates.get(ownerId) !== available) { changed = true; break; }
    }
    listingStates.clear();
    for (const entry of next) listingStates.set(...entry);
    if (changed) notifyAll();
  };
  const listingExpiryTimer = setInterval(listingSweep, 5000);
  listingExpiryTimer.unref?.();

  function consumeJoinTicket(ownerId, ticket) {
    if (typeof ticket !== 'string' || !/^[a-f0-9]{64}$/.test(ticket)) return false;
    const row = query('SELECT guest_id FROM join_tickets WHERE token_hash = ? AND owner_id = ? AND expires_at > ?', tokenHash(ticket), ownerId, Date.now());
    if (!row) return false;
    run('DELETE FROM join_tickets WHERE token_hash = ?', tokenHash(ticket));
    const listing = query('SELECT visibility, updated_at, relay_room_id FROM listings WHERE owner_id = ?', ownerId);
    if (!listing || !canJoin(ownerId, row.guest_id, listing.visibility)) return false;
    const liveRelay = relay?.connectedRoomFor(ownerId);
    if (listing.updated_at <= Date.now() - LISTING_MS && !liveRelay) return false;
    return !listing.relay_room_id || Boolean(liveRelay);
  }
  const relay = options.relay ? new Relay({ ...options.relay, authorizeJoin: consumeJoinTicket,
    onRoomState: () => notifyAll() }) : null;
  const online = user => user.last_seen >= Date.now() - PRESENCE_MS;
  const presenceInfo = user => ({ online: online(user), presence: online(user) ? user.presence_status : 'offline',
    activity: online(user) ? user.game_activity : '' });
  const areFriends = (a, b) => Boolean(query('SELECT 1 FROM friendships WHERE user_a = ? AND user_b = ?', Math.min(a, b), Math.max(a, b)));
  function canJoin(ownerId, userId, mode) {
    if (ownerId === userId || mode === 'public') return true;
    if (query('SELECT 1 FROM game_invites WHERE owner_id = ? AND recipient_id = ? AND expires_at > ?', ownerId, userId, Date.now())) return true;
    if (mode === 'invite_only') return false;
    if (areFriends(ownerId, userId)) return true;
    if (mode !== 'friends_of_friends') return false;
    const members = all('SELECT user_id FROM game_members WHERE owner_id = ?', ownerId);
    return members.some(member => areFriends(member.user_id, userId));
  }
  const loginFailures = new Map();
  const deviceStarts = new Map();

  function requestAddress(request) {
    const remote = request.socket.remoteAddress || 'unknown';
    const localProxy = remote === '127.0.0.1' || remote === '::1' || remote === '::ffff:127.0.0.1';
    if (trustProxy && localProxy && typeof request.headers['x-forwarded-for'] === 'string') {
      const candidate = request.headers['x-forwarded-for'].split(',').at(-1).trim();
      if (isIP(candidate)) return candidate;
    }
    return remote;
  }

  function checkLoginLimit(address) {
    const entry = loginFailures.get(address);
    if (entry && entry.until > Date.now() && entry.count >= MAX_LOGIN_FAILURES) fail(429, 'Too many login attempts; try again later');
    if (entry && entry.until <= Date.now()) loginFailures.delete(address);
  }

  function recordLoginFailure(address) {
    const old = loginFailures.get(address);
    const now = Date.now();
    loginFailures.set(address, old && old.until > now
      ? { count: old.count + 1, until: old.until }
      : { count: 1, until: now + LOGIN_WINDOW_MS });
    if (loginFailures.size > 10000) {
      for (const [key, entry] of loginFailures) if (entry.until <= now) loginFailures.delete(key);
    }
  }

  function authenticate(request) {
    const match = /^Bearer ([a-f0-9]{64})$/.exec(request.headers.authorization || '');
    if (!match) fail(401, 'Bearer token required');
    const hash = tokenHash(match[1]);
    const now = Date.now();
    const user = query(`SELECT users.id, users.username, users.last_seen FROM sessions
      JOIN users ON users.id = sessions.user_id WHERE sessions.token_hash = ? AND sessions.expires_at > ?`, hash, now);
    if (!user) fail(401, 'Invalid or expired session');
    run('UPDATE sessions SET expires_at = ? WHERE token_hash = ?', now + SESSION_MS, hash);
    return user;
  }

  function respond(response, status, body) {
    response.writeHead(status, { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' });
    response.end(JSON.stringify(body));
  }

  const server = createServer(async (request, response) => {
    try {
      const url = new URL(request.url, 'http://localhost');
      const origin = request.headers.origin;
      if (origin && webOrigins.includes(origin)) {
        response.setHeader('access-control-allow-origin', origin);
        response.setHeader('vary', 'Origin');
        response.setHeader('access-control-allow-headers', 'Authorization, Content-Type');
        response.setHeader('access-control-allow-methods', 'GET, POST, PUT, DELETE, OPTIONS');
      }
      if (request.method === 'OPTIONS') return respond(response, origin && webOrigins.includes(origin) ? 200 : 403, { ok: Boolean(origin && webOrigins.includes(origin)) });
      if ((request.method === 'GET' || request.method === 'HEAD') && STATIC_FILES.has(url.pathname)) {
        const [file, contentType] = STATIC_FILES.get(url.pathname);
        const data = readFileSync(new URL(`../site/dist/${file}`, import.meta.url));
        response.writeHead(200, { 'content-type': contentType, 'cache-control': file === 'index.html' ? 'no-cache' : 'public, max-age=3600' });
        return response.end(request.method === 'HEAD' ? undefined : data);
      }
      const path = url.pathname.startsWith('/v1/') ? url.pathname.slice(3) : url.pathname;
      const route = `${request.method} ${path}`;
      if (route === 'GET /events') {
        const ticket = url.searchParams.get('ticket') || '';
        if (!/^[a-f0-9]{64}$/.test(ticket)) return respond(response, 401, { error: 'Event ticket required' });
        const ticketKey = tokenHash(ticket);
        const grant = eventTickets.get(ticketKey);
        eventTickets.delete(ticketKey);
        if (!grant || grant.expiresAt <= Date.now()) return respond(response, 401, { error: 'Event ticket expired' });
        const clients = eventClients.get(grant.userId);
        if (clients && clients.size >= 8) return respond(response, 429, { error: 'Too many live connections' });

        response.writeHead(200, {
          'content-type': 'text/event-stream; charset=utf-8',
          'cache-control': 'no-cache, no-transform',
          connection: 'keep-alive',
          'x-accel-buffering': 'no'
        });
        response.write('event: ready\ndata: {}\n\n');
        const client = { response, heartbeat: null };
        const connected = clients || new Set();
        connected.add(client);
        eventClients.set(grant.userId, connected);
        client.heartbeat = setInterval(() => {
          if (!response.destroyed && !response.writableEnded) response.write(': keep-alive\n\n');
        }, 20000);
        client.heartbeat.unref?.();
        response.on('close', () => {
          clearInterval(client.heartbeat);
          connected.delete(client);
          if (connected.size === 0) eventClients.delete(grant.userId);
        });
        return;
      }
      if (route === 'GET /health') return respond(response, 200, { ok: true, service: 'LCEReborn Live', relay: relay ? 'embedded' : 'disabled' });
      const cosmeticFile = /^\/users\/(\d+)\/(skin|cape)$/.exec(path);
      if (request.method === 'GET' && cosmeticFile) {
        const userId = routeId(cosmeticFile[1]);
        const column = cosmeticFile[2] === 'skin' ? 'skin_png' : 'cape_png';
        const item = query(`SELECT ${column} AS data FROM user_cosmetics WHERE user_id = ?`, userId);
        if (!item?.data) fail(404, 'Cosmetic not found');
        response.writeHead(200, {
          'content-type': 'image/png', 'cache-control': 'public, max-age=60',
          'access-control-allow-origin': '*', 'x-content-type-options': 'nosniff'
        });
        return response.end(item.data);
      }
      if (route === 'POST /accounts') {
        const body = await readBody(request);
        const username = cleanName(body.username);
        const password = cleanPassword(body.password);
        const salt = randomBytes(16).toString('hex');
        const hash = (await passwordHash(password, salt)).toString('hex');
        try {
          const result = run('INSERT INTO users (username, salt, password_hash) VALUES (?, ?, ?)', username, salt, hash);
          return respond(response, 201, { id: Number(result.lastInsertRowid), username });
        } catch (error) {
          if (error.code === 'ERR_SQLITE_ERROR' && error.message.includes('UNIQUE')) fail(409, 'Username already taken');
          throw error;
        }
      }
      if (route === 'POST /sessions') {
        const address = requestAddress(request);
        checkLoginLimit(address);
        const body = await readBody(request);
        const username = cleanName(body.username);
        const password = cleanPassword(body.password);
        const user = query('SELECT * FROM users WHERE username = ?', username);
        const actual = await passwordHash(password, user?.salt || '00000000000000000000000000000000');
        const expected = Buffer.from(user?.password_hash || '00'.repeat(64), 'hex');
        if (!user || !timingSafeEqual(actual, expected)) {
          recordLoginFailure(address);
          fail(401, 'Invalid credentials');
        }
        loginFailures.delete(address);
        const token = randomBytes(32).toString('hex');
        run('INSERT INTO sessions (token_hash, user_id, expires_at) VALUES (?, ?, ?)', tokenHash(token), user.id, Date.now() + SESSION_MS);
        const lastSeen = Date.now();
        run("UPDATE users SET last_seen = ?, presence_status = 'launcher', game_activity = '' WHERE id = ?", lastSeen, user.id);
        updatePresenceState(Number(user.id), lastSeen, 'launcher', '');
        return respond(response, 201, { token, expires_at: new Date(Date.now() + SESSION_MS).toISOString(), user: publicUser(user, true) });
      }
      if (route === 'POST /device/start') {
        const address = requestAddress(request);
        const now = Date.now();
        const previous = deviceStarts.get(address);
        if (previous && previous.until > now && previous.count >= 10) fail(429, 'Too many authorization requests');
        deviceStarts.set(address, previous && previous.until > now ? { count: previous.count + 1, until: previous.until } : { count: 1, until: now + 5 * 60 * 1000 });
        if (deviceStarts.size > 10000) for (const [key, entry] of deviceStarts) if (entry.until <= now) deviceStarts.delete(key);
        run('DELETE FROM device_authorizations WHERE expires_at <= ?', Date.now());
        const deviceCode = randomBytes(32).toString('hex');
        const userCode = randomBytes(6).toString('hex').toUpperCase();
        run('INSERT INTO device_authorizations (device_hash, user_code, expires_at) VALUES (?, ?, ?)', tokenHash(deviceCode), userCode, Date.now() + 5 * 60 * 1000);
        return respond(response, 201, { device_code: deviceCode, user_code: userCode, expires_in: 300, interval: 3 });
      }
      if (route === 'POST /device/token') {
        const body = await readBody(request);
        if (typeof body.device_code !== 'string' || !/^[a-f0-9]{64}$/.test(body.device_code)) fail(400, 'Invalid device code');
        const hash = tokenHash(body.device_code);
        const device = query('SELECT * FROM device_authorizations WHERE device_hash = ? AND expires_at > ?', hash, Date.now());
        if (!device) fail(410, 'Authorization expired');
        if (device.status === 'pending') return respond(response, 202, { status: 'pending' });
        if (device.status === 'denied') { run('DELETE FROM device_authorizations WHERE device_hash = ?', hash); fail(403, 'Authorization denied'); }
        const account = query('SELECT id, username FROM users WHERE id = ?', device.user_id);
        if (!account) fail(410, 'Authorization expired');
        const token = randomBytes(32).toString('hex');
        db.exec('BEGIN');
        try {
          run('DELETE FROM device_authorizations WHERE device_hash = ?', hash);
          run('INSERT INTO sessions (token_hash, user_id, expires_at) VALUES (?, ?, ?)', tokenHash(token), account.id, Date.now() + SESSION_MS);
          const lastSeen = Date.now();
          run("UPDATE users SET last_seen = ?, presence_status = 'launcher', game_activity = '' WHERE id = ?", lastSeen, account.id);
          db.exec('COMMIT');
          updatePresenceState(Number(account.id), lastSeen, 'launcher', '');
        } catch (error) { db.exec('ROLLBACK'); throw error; }
        return respond(response, 201, { token, user: publicUser(account, true) });
      }
      const user = authenticate(request);
      if (route === 'GET /leaderboards') {
        const board = url.searchParams.get('board') || '';
        const columns = LEADERBOARD_COLUMNS.get(board);
        if (!columns) fail(400, 'Invalid leaderboard board');
        const filter = url.searchParams.get('filter') || 'overall';
        if (!['overall', 'friends', 'mine'].includes(filter)) fail(400, 'Invalid leaderboard filter');
        const start = Number(url.searchParams.get('start') || '1');
        const count = Number(url.searchParams.get('count') || '15');
        if (!Number.isSafeInteger(start) || start < 1 || !Number.isSafeInteger(count) || count < 1 || count > 50)
          fail(400, 'Invalid leaderboard page');
        const friendClause = filter === 'overall' ? '' : filter === 'mine' ? 'AND u.id = ?'
          : 'AND (u.id = ? OR EXISTS (SELECT 1 FROM friendships f WHERE (f.user_a = ? AND f.user_b = u.id) OR (f.user_b = ? AND f.user_a = u.id)))';
        const params = filter === 'mine' ? [board, user.id] : filter === 'friends' ? [board, user.id, user.id, user.id] : [board];
        const scoreOrder = Array.from({ length: columns }, (_, index) =>
          `COALESCE(CAST(json_extract(s.values_json, '$[${index}]') AS INTEGER), 0)`).join(' + ');
        const total = Number(query(`SELECT COUNT(*) AS count FROM leaderboard_scores s JOIN users u ON u.id = s.user_id
          WHERE s.board = ? ${friendClause}`, ...params).count);
        const rows = all(`SELECT u.id, u.username, s.values_json FROM leaderboard_scores s JOIN users u ON u.id = s.user_id
          WHERE s.board = ? ${friendClause} ORDER BY (${scoreOrder}) DESC, u.username COLLATE NOCASE
          LIMIT ? OFFSET ?`, ...params, count, start - 1);
        return respond(response, 200, { board, filter, total, rows: rows.map((row, index) => ({
          rank: start + index, user_id: Number(row.id), username: row.username,
          values: JSON.parse(row.values_json).slice(0, columns)
        })) });
      }
      const leaderboardSubmit = /^POST \/leaderboards\/(traveling|mining|farming|kills)$/.exec(route);
      if (leaderboardSubmit) {
        const board = leaderboardSubmit[1];
        const columns = LEADERBOARD_COLUMNS.get(board);
        const body = await readBody(request);
        if (!Array.isArray(body.values) || body.values.length !== columns
            || body.values.some(value => !Number.isSafeInteger(value) || value < 0 || value > 0xffffffff))
          fail(400, `Provide ${columns} non-negative integer values`);
        const previous = query('SELECT values_json FROM leaderboard_scores WHERE user_id = ? AND board = ?', user.id, board);
        const merged = previous ? JSON.parse(previous.values_json) : Array(columns).fill(0);
        const values = body.values.map((value, index) => Math.max(merged[index] || 0, value));
        run(`INSERT INTO leaderboard_scores (user_id, board, values_json, updated_at) VALUES (?, ?, ?, ?)
          ON CONFLICT(user_id, board) DO UPDATE SET values_json = excluded.values_json, updated_at = excluded.updated_at`,
        user.id, board, JSON.stringify(values), Date.now());
        notifyFriendsAndSelf(user.id, 'leaderboards');
        return respond(response, 200, { ok: true });
      }
      if (route === 'POST /events/ticket') {
        const now = Date.now();
        for (const [key, ticket] of eventTickets) if (ticket.expiresAt <= now) eventTickets.delete(key);
        const pending = [...eventTickets.values()].filter(ticket => ticket.userId === user.id).length;
        if ((eventClients.get(user.id)?.size || 0) + pending >= 8) fail(429, 'Too many live connections');
        const ticket = randomBytes(32).toString('hex');
        eventTickets.set(tokenHash(ticket), { userId: user.id, expiresAt: now + 30000 });
        return respond(response, 201, { ticket, expires_in: 30 });
      }
      if (route === 'POST /device/authorize') {
        const body = await readBody(request);
        if (typeof body.user_code !== 'string' || !/^[A-F0-9]{12}$/.test(body.user_code) || typeof body.allow !== 'boolean') fail(400, 'Invalid authorization request');
        const result = run("UPDATE device_authorizations SET status = ?, user_id = ? WHERE user_code = ? AND status = 'pending' AND expires_at > ?", body.allow ? 'approved' : 'denied', user.id, body.user_code, Date.now());
        if (!result.changes) fail(404, 'Authorization request not found or expired');
        return respond(response, 200, { ok: true });
      }
      if (route === 'DELETE /sessions/current') {
        run('DELETE FROM sessions WHERE token_hash = ?', tokenHash(request.headers.authorization.slice(7)));
        if (!query('SELECT 1 FROM sessions WHERE user_id = ? AND expires_at > ?', user.id, Date.now())) {
          run('UPDATE users SET last_seen = 0 WHERE id = ?', user.id);
          updatePresenceState(Number(user.id), 0);
        }
        return respond(response, 200, { ok: true });
      }
      if (route === 'DELETE /sessions') {
        run('DELETE FROM sessions WHERE user_id = ?', user.id);
        run('UPDATE users SET last_seen = 0 WHERE id = ?', user.id);
        updatePresenceState(Number(user.id), 0);
        return respond(response, 200, { ok: true });
      }
      if (route === 'GET /me') return respond(response, 200, { ...publicUser(user, online(user)), ...presenceInfo(user) });
      if (route === 'GET /me/cosmetics') {
        const item = query('SELECT skin_png, cape_png, updated_at FROM user_cosmetics WHERE user_id = ?', user.id);
        return respond(response, 200, {
          user_id: user.id, skin: Boolean(item?.skin_png), cape: Boolean(item?.cape_png), updated_at: item?.updated_at || 0
        });
      }
      const cosmeticUpload = /^PUT \/me\/cosmetics\/(skin|cape)$/.exec(route);
      if (cosmeticUpload) {
        const kind = cosmeticUpload[1];
        const data = await readBinary(request);
        if (request.headers['content-type'] !== 'image/png') fail(415, 'Use a PNG image');
        validatePng(data, kind);
        const column = kind === 'skin' ? 'skin_png' : 'cape_png';
        run(`INSERT INTO user_cosmetics (user_id, ${column}, updated_at) VALUES (?, ?, ?)
          ON CONFLICT(user_id) DO UPDATE SET ${column} = excluded.${column}, updated_at = excluded.updated_at`, user.id, data, Date.now());
        notifyUsers([user.id], 'cosmetics');
        notifyUsers(friendIds(user.id).map(friend => friend.id));
        return respond(response, 200, { ok: true, kind });
      }
      const cosmeticDelete = /^DELETE \/me\/cosmetics\/(skin|cape)$/.exec(route);
      if (cosmeticDelete) {
        const column = cosmeticDelete[1] === 'skin' ? 'skin_png' : 'cape_png';
        run(`UPDATE user_cosmetics SET ${column} = NULL, updated_at = ? WHERE user_id = ?`, Date.now(), user.id);
        notifyUsers([user.id], 'cosmetics');
        notifyUsers(friendIds(user.id).map(friend => friend.id));
        return respond(response, 200, { ok: true });
      }
      if (route === 'PUT /me/password') {
        const body = await readBody(request);
        const currentPassword = cleanPassword(body.current_password);
        const newPassword = cleanPassword(body.new_password);
        const account = query('SELECT salt, password_hash FROM users WHERE id = ?', user.id);
        const actual = await passwordHash(currentPassword, account.salt);
        if (!timingSafeEqual(actual, Buffer.from(account.password_hash, 'hex'))) fail(401, 'Invalid current password');
        const salt = randomBytes(16).toString('hex');
        const hash = (await passwordHash(newPassword, salt)).toString('hex');
        db.exec('BEGIN');
        try {
          run('UPDATE users SET salt = ?, password_hash = ? WHERE id = ?', salt, hash, user.id);
          run('DELETE FROM sessions WHERE user_id = ?', user.id);
          run('UPDATE users SET last_seen = 0 WHERE id = ?', user.id);
          db.exec('COMMIT');
        } catch (error) { db.exec('ROLLBACK'); throw error; }
        updatePresenceState(Number(user.id), 0);
        return respond(response, 200, { ok: true, signed_out: true });
      }
      if (route === 'POST /presence') {
        const lastSeen = Date.now();
        const body = await readBody(request);
        const status = ['website', 'launcher', 'game', 'minigame'].includes(body.status) ? body.status : 'launcher';
        if (status === 'website' && user.presence_status !== 'website' && user.last_seen >= lastSeen - 20000)
          return respond(response, 200, { ok: true, preserved: true });
        const activities = new Set(['world', 'battle', 'tumble', 'glide', 'build']);
        const activity = typeof body.activity === 'string' && activities.has(body.activity) ? body.activity : '';
        run('UPDATE users SET last_seen = ?, presence_status = ?, game_activity = ? WHERE id = ?', lastSeen, status, activity, user.id);
        updatePresenceState(Number(user.id), lastSeen, status, activity);
        return respond(response, 200, { ok: true });
      }
      if (route === 'GET /friends') {
        const friends = all(`SELECT u.id, u.username, u.last_seen, u.presence_status, u.game_activity,
          c.updated_at AS cosmetics_updated_at FROM friendships f JOIN users u
          ON u.id = CASE WHEN f.user_a = ? THEN f.user_b ELSE f.user_a END
          LEFT JOIN user_cosmetics c ON c.user_id = u.id
          WHERE f.user_a = ? OR f.user_b = ? ORDER BY u.username`, user.id, user.id, user.id);
        return respond(response, 200, { friends: friends.map(friend => ({
          ...publicUser(friend, online(friend)), ...presenceInfo(friend), cosmetics_updated_at: friend.cosmetics_updated_at || 0
        })) });
      }
      if (route === 'GET /friends/requests') {
        const incoming = all(`SELECT u.id, u.username FROM friend_requests r JOIN users u ON u.id = r.sender_id
          WHERE r.recipient_id = ? ORDER BY u.username`, user.id);
        return respond(response, 200, { incoming });
      }
      if (route === 'GET /friends/requests/sent') {
        const sent = all(`SELECT u.id, u.username FROM friend_requests r JOIN users u ON u.id = r.recipient_id
          WHERE r.sender_id = ? ORDER BY u.username`, user.id);
        return respond(response, 200, { sent });
      }
      if (route === 'POST /friends/requests') {
        const body = await readBody(request);
        const target = query('SELECT id FROM users WHERE username = ?', cleanName(body.username));
        if (!target) fail(404, 'User not found');
        if (target.id === user.id) fail(400, 'Cannot add yourself');
        const a = Math.min(user.id, target.id), b = Math.max(user.id, target.id);
        if (query('SELECT 1 FROM friendships WHERE user_a = ? AND user_b = ?', a, b)) fail(409, 'Already friends');
        if (query('SELECT 1 FROM friend_requests WHERE sender_id = ? AND recipient_id = ?', target.id, user.id)) fail(409, 'Accept the incoming request');
        if (query('SELECT 1 FROM friend_requests WHERE sender_id = ? AND recipient_id = ?', user.id, target.id)) fail(409, 'Request already sent');
        run('INSERT INTO friend_requests VALUES (?, ?)', user.id, target.id);
        notifyUsers([user.id, target.id]);
        return respond(response, 201, { ok: true });
      }
      const accept = /^POST \/friends\/requests\/(\d+)\/accept$/.exec(route);
      if (accept) {
        const sender = routeId(accept[1]);
        if (!query('SELECT 1 FROM friend_requests WHERE sender_id = ? AND recipient_id = ?', sender, user.id)) fail(404, 'Request not found');
        db.exec('BEGIN');
        try {
          run('INSERT OR IGNORE INTO friendships VALUES (?, ?)', Math.min(sender, user.id), Math.max(sender, user.id));
          run('DELETE FROM friend_requests WHERE (sender_id = ? AND recipient_id = ?) OR (sender_id = ? AND recipient_id = ?)', sender, user.id, user.id, sender);
          db.exec('COMMIT');
        } catch (error) { db.exec('ROLLBACK'); throw error; }
        notifyUsers([user.id, sender]);
        return respond(response, 200, { ok: true });
      }
      const reject = /^DELETE \/friends\/requests\/(\d+)$/.exec(route);
      if (reject) {
        const sender = routeId(reject[1]);
        const result = run('DELETE FROM friend_requests WHERE sender_id = ? AND recipient_id = ?', sender, user.id);
        if (!result.changes) fail(404, 'Request not found');
        notifyUsers([user.id, sender]);
        return respond(response, 200, { ok: true });
      }
      const cancel = /^DELETE \/friends\/requests\/sent\/(\d+)$/.exec(route);
      if (cancel) {
        const recipient = routeId(cancel[1]);
        const result = run('DELETE FROM friend_requests WHERE sender_id = ? AND recipient_id = ?', user.id, recipient);
        if (!result.changes) fail(404, 'Request not found');
        notifyUsers([user.id, recipient]);
        return respond(response, 200, { ok: true });
      }
      const unfriend = /^DELETE \/friends\/(\d+)$/.exec(route);
      if (unfriend) {
        const other = routeId(unfriend[1]);
        const result = run('DELETE FROM friendships WHERE user_a = ? AND user_b = ?', Math.min(user.id, other), Math.max(user.id, other));
        if (!result.changes) fail(404, 'Friend not found');
        run('DELETE FROM game_invites WHERE (owner_id = ? AND recipient_id = ?) OR (owner_id = ? AND recipient_id = ?)', user.id, other, other, user.id);
        notifyUsers([user.id, other]);
        return respond(response, 200, { ok: true });
      }
      if (route === 'GET /servers') {
        const rows = all(`SELECT l.owner_id, l.name, l.address, l.port, l.updated_at, l.relay_room_id, l.visibility, l.direct_ticketed, u.username AS owner
          FROM listings l JOIN users u ON u.id = l.owner_id ORDER BY l.updated_at DESC`);
        const servers = rows.flatMap(row => {
          const { owner_id, relay_room_id, ...listing } = row;
          if (!canJoin(owner_id, user.id, row.visibility)) return [];
          const relayEndpoint = relay?.connectedRoomFor(owner_id) || null;
          if (relay_room_id && !relayEndpoint) return [];
          if (row.updated_at <= Date.now() - LISTING_MS && !relayEndpoint) return [];
          const isRelay = Boolean(relay_room_id);
          return [{ owner_id, ...listing, direct: isRelay ? null : { address: row.address, port: row.port }, relay: relayEndpoint }];
        });
        return respond(response, 200, { servers });
      }
      if (route === 'GET /game-invites') {
        const invitations = all(`SELECT i.owner_id, u.username AS owner, l.name, i.expires_at
          FROM game_invites i JOIN users u ON u.id = i.owner_id
          JOIN listings l ON l.owner_id = i.owner_id
          WHERE i.recipient_id = ? AND i.expires_at > ? ORDER BY i.expires_at DESC`, user.id, Date.now());
        return respond(response, 200, { invitations });
      }
      const dismissInvite = /^DELETE \/game-invites\/(\d+)$/.exec(route);
      if (dismissInvite) {
        const ownerId = routeId(dismissInvite[1]);
        const result = run('DELETE FROM game_invites WHERE owner_id = ? AND recipient_id = ?', ownerId, user.id);
        if (!result.changes) fail(404, 'Invitation not found');
        notifyUsers([ownerId, user.id]);
        return respond(response, 200, { ok: true });
      }
      const joinTicket = /^POST \/servers\/(\d+)\/join-ticket$/.exec(route);
      if (joinTicket) {
        const ownerId = routeId(joinTicket[1]);
        const listing = query('SELECT visibility, updated_at, relay_room_id FROM listings WHERE owner_id = ?', ownerId);
        if (!listing || !canJoin(ownerId, user.id, listing.visibility)) fail(404, 'Game not found');
        const liveRelay = relay?.connectedRoomFor(ownerId);
        if (listing.updated_at <= Date.now() - LISTING_MS && !liveRelay) fail(404, 'Game not available');
        if (listing.relay_room_id && !liveRelay) fail(404, 'Game not available');
        const ticket = randomBytes(32).toString('hex');
        const expiresAt = Date.now() + 30 * 1000;
        run('DELETE FROM join_tickets WHERE expires_at <= ?', Date.now());
        run('INSERT INTO join_tickets VALUES (?, ?, ?, ?)', tokenHash(ticket), ownerId, user.id, expiresAt);
        return respond(response, 201, { ticket, expires_at: new Date(expiresAt).toISOString() });
      }
      if (route === 'POST /join-tickets/consume') {
        const body = await readBody(request);
        if (!consumeJoinTicket(user.id, body.ticket)) fail(403, 'Invalid or expired join ticket');
        return respond(response, 200, { ok: true });
      }
      if (route === 'POST /servers/mine/invites') {
        const listing = query('SELECT 1 FROM listings WHERE owner_id = ?', user.id);
        if (!listing) fail(404, 'Host a game before inviting friends');
        const body = await readBody(request);
        const target = query('SELECT id FROM users WHERE username = ?', cleanName(body.username));
        if (!target || !areFriends(user.id, target.id)) fail(404, 'Friend not found');
        const expiresAt = Date.now() + 24 * 60 * 60 * 1000;
        run(`INSERT INTO game_invites VALUES (?, ?, ?) ON CONFLICT(owner_id, recipient_id)
          DO UPDATE SET expires_at = excluded.expires_at`, user.id, target.id, expiresAt);
        notifyUsers([user.id, target.id]);
        return respond(response, 201, { ok: true, expires_at: new Date(expiresAt).toISOString() });
      }
      if (route === 'PUT /servers/mine/members') {
        if (!query('SELECT 1 FROM listings WHERE owner_id = ?', user.id)) fail(404, 'Game not found');
        const body = await readBody(request);
        if (!Array.isArray(body.user_ids) || body.user_ids.length > 7 || body.user_ids.some(id => !Number.isSafeInteger(id) || id < 1 || id === user.id)
            || new Set(body.user_ids).size !== body.user_ids.length) fail(400, 'Provide up to seven distinct player IDs');
        for (const id of body.user_ids) if (!query('SELECT 1 FROM users WHERE id = ?', id)) fail(400, 'Unknown player ID');
        db.exec('BEGIN');
        try {
          run('DELETE FROM game_members WHERE owner_id = ?', user.id);
          run('INSERT INTO game_members VALUES (?, ?)', user.id, user.id);
          for (const id of body.user_ids) run('INSERT INTO game_members VALUES (?, ?)', user.id, id);
          db.exec('COMMIT');
        } catch (error) { db.exec('ROLLBACK'); throw error; }
        notifyAll();
        return respond(response, 200, { ok: true });
      }
      const cancelInvite = /^DELETE \/servers\/mine\/invites\/(\d+)$/.exec(route);
      if (cancelInvite) {
        const recipientId = routeId(cancelInvite[1]);
        const result = run('DELETE FROM game_invites WHERE owner_id = ? AND recipient_id = ?', user.id, recipientId);
        if (!result.changes) fail(404, 'Invitation not found');
        notifyUsers([user.id, recipientId]);
        return respond(response, 200, { ok: true });
      }
      if (route === 'PUT /servers/mine') {
        const body = await readBody(request);
        if (typeof body.name !== 'string' || body.name.length < 1 || body.name.length > 64) fail(400, 'Server name must be 1–64 characters');
        if (typeof body.address !== 'string' || !/^[a-zA-Z0-9.:[\]-]{1,255}$/.test(body.address)) fail(400, 'Invalid server address');
        if (!Number.isInteger(body.port) || body.port < 1 || body.port > 65535) fail(400, 'Invalid port');
        const mode = visibility(body.visibility, 'public');
        if (body.ticketed !== undefined && typeof body.ticketed !== 'boolean') fail(400, 'ticketed must be boolean');
        if (mode !== 'public' && body.ticketed !== true) fail(400, 'Private direct games require ticket verification');
        const isNew = !query('SELECT 1 FROM listings WHERE owner_id = ?', user.id);
        run(`INSERT INTO listings (owner_id, name, address, port, updated_at, relay_room_id, visibility, direct_ticketed) VALUES (?, ?, ?, ?, ?, NULL, ?, ?)
          ON CONFLICT(owner_id) DO UPDATE SET name = excluded.name, address = excluded.address,
          port = excluded.port, updated_at = excluded.updated_at, relay_room_id = NULL,
          visibility = excluded.visibility, direct_ticketed = excluded.direct_ticketed`, user.id, body.name, body.address, body.port, Date.now(), mode, body.ticketed ? 1 : 0);
        if (isNew) run('INSERT OR IGNORE INTO game_members VALUES (?, ?)', user.id, user.id);
        notifyAll();
        return respond(response, 200, { ok: true });
      }
      if (route === 'DELETE /servers/mine') {
        run('DELETE FROM listings WHERE owner_id = ?', user.id);
        run('DELETE FROM game_invites WHERE owner_id = ?', user.id);
        run('DELETE FROM game_members WHERE owner_id = ?', user.id);
        run('DELETE FROM join_tickets WHERE owner_id = ?', user.id);
        notifyAll();
        return respond(response, 200, { ok: true });
      }
      if (route === 'POST /relay/rooms') {
        if (!relay) fail(503, 'Relay is disabled');
        const body = await readBody(request);
        if (typeof body.name !== 'string' || body.name.length < 1 || body.name.length > 64) fail(400, 'Server name must be 1–64 characters');
        const mode = visibility(body.visibility, 'friends');
        const room = await relay.createRoom(user.id);
        run('DELETE FROM game_invites WHERE owner_id = ?', user.id);
        run('DELETE FROM game_members WHERE owner_id = ?', user.id);
        run('DELETE FROM join_tickets WHERE owner_id = ?', user.id);
        run('INSERT INTO game_members VALUES (?, ?)', user.id, user.id);
        run(`INSERT INTO listings (owner_id, name, address, port, updated_at, relay_room_id, visibility, direct_ticketed) VALUES (?, ?, ?, ?, ?, ?, ?, 0)
          ON CONFLICT(owner_id) DO UPDATE SET name = excluded.name, address = excluded.address,
          port = excluded.port, updated_at = excluded.updated_at, relay_room_id = excluded.relay_room_id,
          visibility = excluded.visibility, direct_ticketed = 0`, user.id, body.name, room.address, room.port, Date.now(), room.id, mode);
        notifyAll();
        return respond(response, 201, room);
      }
      const relayListing = /^PUT \/relay\/rooms\/([a-f0-9]{24})\/listing$/.exec(route);
      if (relayListing) {
        const room = relay?.rooms.get(relayListing[1]);
        if (!room || room.ownerId !== user.id) fail(404, 'Relay room not found');
        const body = await readBody(request);
        if (typeof body.name !== 'string' || body.name.length < 1 || body.name.length > 64) fail(400, 'Server name must be 1–64 characters');
        const mode = visibility(body.visibility, 'friends');
        run(`INSERT INTO listings (owner_id, name, address, port, updated_at, relay_room_id, visibility, direct_ticketed) VALUES (?, ?, ?, ?, ?, ?, ?, 0)
          ON CONFLICT(owner_id) DO UPDATE SET name = excluded.name, address = excluded.address,
          port = excluded.port, updated_at = excluded.updated_at, relay_room_id = excluded.relay_room_id,
          visibility = excluded.visibility, direct_ticketed = 0`,
          user.id, body.name, relay.publicHost, room.port, Date.now(), room.id, mode);
        notifyAll();
        return respond(response, 200, { ok: true });
      }
      const removeRoom = /^DELETE \/relay\/rooms\/([a-f0-9]{24})$/.exec(route);
      if (removeRoom) {
        const room = relay?.rooms.get(removeRoom[1]);
        if (!room || room.ownerId !== user.id || !await relay.removeRoom(removeRoom[1], user.id)) fail(404, 'Relay room not found');
        const deleted = run('DELETE FROM listings WHERE owner_id = ? AND relay_room_id = ?', user.id, room.id);
        if (deleted.changes) {
          run('DELETE FROM game_invites WHERE owner_id = ?', user.id);
          run('DELETE FROM game_members WHERE owner_id = ?', user.id);
          run('DELETE FROM join_tickets WHERE owner_id = ?', user.id);
        }
        notifyAll();
        return respond(response, 200, { ok: true });
      }
      fail(404, 'Route not found');
    } catch (error) {
      if (!response.writableEnded && !response.destroyed) respond(response, error.status || 500, { error: error.status ? error.message : 'Internal server error' });
      if (!error.status) console.error(error);
    }
  });
  server.relay = relay;
  server.on('close', () => {
    clearInterval(presenceExpiryTimer);
    clearInterval(listingExpiryTimer);
    for (const clients of eventClients.values()) for (const client of clients) {
      clearInterval(client.heartbeat);
      client.response.end();
    }
    eventClients.clear();
    eventTickets.clear();
    db.close();
    if (relay) void relay.shutdown();
  });
  return server;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const host = process.env.LIVE_HOST || '127.0.0.1';
  const port = Number(process.env.LIVE_PORT || 8080);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('LIVE_PORT must be 1–65535');
  const relayBind = process.env.LIVE_RELAY_BIND || host;
  const hasRelayTls = Boolean(process.env.LIVE_RELAY_TLS_CERT && process.env.LIVE_RELAY_TLS_KEY);
  if (process.env.LIVE_RELAY_ENABLED === '1' && !['127.0.0.1', '::1', 'localhost'].includes(relayBind)
      && !hasRelayTls && process.env.LIVE_ALLOW_INSECURE_RELAY !== '1') {
    throw new Error('Public relay requires LIVE_RELAY_TLS_CERT and LIVE_RELAY_TLS_KEY');
  }
  const app = createApp(resolve(process.env.LIVE_DB || 'data/live.db'), {
    relay: process.env.LIVE_RELAY_ENABLED === '1' ? {
      bind: relayBind,
      publicHost: process.env.LIVE_RELAY_PUBLIC_HOST || host,
      controlPort: Number(process.env.LIVE_RELAY_CONTROL_PORT || 8081),
      firstPort: Number(process.env.LIVE_RELAY_FIRST_PORT || 30000),
      lastPort: Number(process.env.LIVE_RELAY_LAST_PORT || 30099),
      tlsOptions: hasRelayTls
        ? { cert: readFileSync(process.env.LIVE_RELAY_TLS_CERT), key: readFileSync(process.env.LIVE_RELAY_TLS_KEY) }
        : null
    } : null
  });
  (app.relay ? app.relay.ready : Promise.resolve()).then(() => app.listen(port, host, () => {
    console.log(`LCEReborn Live listening on http://${host}:${port}`);
  })).catch(error => { console.error('Relay startup failed:', error); process.exitCode = 1; });
}
