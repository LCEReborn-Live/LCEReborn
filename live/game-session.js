import fs from 'node:fs/promises';
import path from 'node:path';
import readline from 'node:readline';
import { setTimeout as delay } from 'node:timers/promises';
import { GameBridge } from './game-bridge.js';
import { SocialBridge } from './social-bridge.js';

// One instance per launched game. The launcher sends credentials over stdin.
console.log = () => {};
const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
let bridge, directory, username, running = false, stopped = false, lastWorld = '';
let timer, leaderboardTimer, leaderboardBusy = false, leaderboardSubmissionBusy = false;
let leaderboardSubmitAt = 0, leaderboardRetryAt = 0;
let socialTimer, socialActionTimer, social;

const leaderboardBoards = ['traveling', 'mining', 'farming', 'kills'];
const leaderboardColumns = { traveling: 4, mining: 7, farming: 6, kills: 7 };
const leaderboardSubmitIntervalMs = 3 * 60 * 1000;
const submittedScores = new Map();

async function processLeaderboardRequest() {
  if (leaderboardBusy || stopped || !directory) return;
  leaderboardBusy = true;
  const requestPath = path.join(directory, 'leaderboard.request');
  let requestId = '';
  try {
    const request = (await fs.readFile(requestPath, 'utf8')).trim().split('\t');
    await fs.unlink(requestPath);
    const [id, boardText, filter, startText, countText] = request;
    requestId = id;
    const board = leaderboardBoards[Number(boardText)];
    const start = Number(startText), count = Number(countText);
    if (!/^\d+$/.test(id) || !board || !['overall', 'friends', 'mine'].includes(filter)
        || !Number.isInteger(start) || start < 1 || !Number.isInteger(count) || count < 1 || count > 50)
      throw new Error('Invalid leaderboard request');
    const url = new URL('/v1/leaderboards', bridge.apiUrl);
    url.search = new URLSearchParams({ board, filter, start: String(start), count: String(count) }).toString();
    const response = await bridge.fetchImpl(url, {
      headers: { authorization: `Bearer ${bridge.token}` }, signal: AbortSignal.timeout(10000)
    });
    const data = await response.json();
    if (!response.ok) throw new Error(`Leaderboard HTTP ${response.status}`);
    const columns = leaderboardColumns[board];
    const lines = [`${id}\t${data.total}\t${data.rows.length}`];
    for (const row of data.rows) {
      if (!Number.isSafeInteger(row.rank) || typeof row.username !== 'string'
          || !/^[A-Za-z0-9_]{3,20}$/.test(row.username) || !Array.isArray(row.values)
          || row.values.length !== columns || row.values.some(value => !Number.isSafeInteger(value) || value < 0))
        throw new Error('Invalid leaderboard response');
      lines.push([row.rank, row.username, row.user_id, ...row.values].join('\t'));
    }
    const temporary = path.join(directory, 'leaderboard.response.tmp');
    await fs.writeFile(temporary, lines.join('\n') + '\n', { encoding: 'utf8', mode: 0o600 });
    await fs.rename(temporary, path.join(directory, 'leaderboard.response'));
  } catch (error) {
    if (error.code !== 'ENOENT') {
      console.error('Live leaderboard:', error.message);
      if (/^\d+$/.test(requestId)) {
        const temporary = path.join(directory, 'leaderboard.response.tmp');
        await fs.writeFile(temporary, `${requestId}\t-1\t0\n`, { encoding: 'utf8', mode: 0o600 }).catch(() => {});
        await fs.rename(temporary, path.join(directory, 'leaderboard.response')).catch(() => {});
      }
    }
  } finally { leaderboardBusy = false; }
}

async function processLeaderboardSubmissions(flush = false) {
  const now = Date.now();
  if (leaderboardSubmissionBusy || !bridge || now < leaderboardRetryAt
      || (!flush && (stopped || now < leaderboardSubmitAt))) return;
  leaderboardSubmissionBusy = true;
  leaderboardSubmitAt = now + leaderboardSubmitIntervalMs;
  try {
    const files = await fs.readdir(directory);
    const pendingFiles = [];
    const scores = new Map();
    for (const file of files.filter(name => /^leaderboard-submit-\d+-\d+\.tsv$/.test(name))) {
      const fullPath = path.join(directory, file);
      let rows;
      try { rows = (await fs.readFile(fullPath, 'utf8')).trim().split('\n'); }
      catch (error) {
        if (error.code !== 'ENOENT') console.error('Live leaderboard:', error.message);
        continue;
      }
      const entries = rows.map(row => {
        const [boardIndex, ...fields] = row.split('\t');
        return { board: /^[0-3]$/.test(boardIndex) ? leaderboardBoards[Number(boardIndex)] : undefined,
          values: fields.map(Number) };
      });
      if (entries.some(({ board, values }) => !board || values.length !== leaderboardColumns[board]
          || values.some(value => !Number.isSafeInteger(value) || value < 0 || value > 0xffffffff))) {
        console.error('Live leaderboard: invalid score file', file);
        continue;
      }
      // The API keeps the maximum of each stat, so a single merge preserves every snapshot.
      for (const { board, values } of entries) {
        const previous = scores.get(board);
        scores.set(board, previous ? values.map((value, index) => Math.max(value, previous[index])) : values);
      }
      pendingFiles.push(fullPath);
    }

    for (const [board, values] of scores) {
      const previous = submittedScores.get(board);
      if (previous && values.every((value, index) => value <= previous[index])) continue;
      const merged = previous ? values.map((value, index) => Math.max(value, previous[index])) : values;
      const response = await bridge.fetchImpl(new URL(`/v1/leaderboards/${board}`, bridge.apiUrl), {
        method: 'POST', headers: { authorization: `Bearer ${bridge.token}`, 'content-type': 'application/json' },
        body: JSON.stringify({ values: merged }), signal: AbortSignal.timeout(10000)
      });
      if (!response.ok) {
        if (response.status === 429) {
          const retrySeconds = Number(response.headers.get('retry-after'));
          leaderboardRetryAt = Number.isFinite(retrySeconds) && retrySeconds > 0
            ? Date.now() + retrySeconds * 1000 : leaderboardSubmitAt;
        }
        throw new Error(`Leaderboard HTTP ${response.status}`);
      }
      submittedScores.set(board, merged);
    }
    for (const file of pendingFiles) await fs.unlink(file).catch(() => {});
  } catch (error) {
    if (error.code !== 'ENOENT') console.error('Live leaderboard submit:', error.message);
  } finally { leaderboardSubmissionBusy = false; }
}

async function replaceLiveFile(temporary, destination) {
  for (let attempt = 0; ; attempt++) {
    try {
      await fs.rename(temporary, destination);
      return;
    } catch (error) {
      if (process.platform !== 'win32' || !['EACCES', 'EPERM', 'EBUSY'].includes(error.code) || attempt === 4)
        throw error;
      await delay(25 * 2 ** attempt);
    }
  }
}

async function tick() {
  if (running || stopped) return;
  running = true;
  try {
    let world;
    try { world = (await fs.readFile(path.join(directory, 'world.txt'), 'utf8')).trim(); }
    catch (error) { if (error.code !== 'ENOENT') throw error; world = '0'; }
    if (/^0$/.test(world) && lastWorld !== '0') {
      await bridge.setWorld(null); lastWorld = '0';
    } else {
      const match = /^1\t(\d{1,5})\t([01])(?:\t([01])\t(\d{1,2})\t(\d{1,2})\t(\d{1,2}))?$/.exec(world);
      if (match && Number(match[1]) > 0 && Number(match[1]) <= 65535 && world !== lastWorld) {
        const matchmaking = match[3] === '1' && Number(match[4]) >= 1 && Number(match[4]) <= 4
          ? { enabled: true, mini_game_id: Number(match[4]), filters: [Number(match[5]), Number(match[6])]
            .map((value, id) => ({ id, value })).filter(filter => filter.value > 0) }
          : {};
        await bridge.setWorld({ name: `${username}'s world`.slice(0, 64), port: Number(match[1]),
          private: match[2] === '1', matchmaking });
        lastWorld = world;
      }
    }
    const servers = await bridge.refresh();
    await social?.updateWorlds(servers);
    const lines = servers.filter(s => s.owner.toLowerCase() !== username.toLowerCase())
      .map(s => {
        const match = s.matchmaking?.enabled ? s.matchmaking : {};
        const filters = [0, 0];
        if (Array.isArray(match.filters)) for (const filter of match.filters)
          if (Number.isInteger(filter?.id) && filter.id >= 0 && filter.id < 2 && Number.isInteger(filter.value)) filters[filter.id] = filter.value;
        const miniGame = Number.isInteger(match.mini_game_id) ? match.mini_game_id : 0;
        return `${s.port}\t${String(s.name || s.owner).replace(/[\r\n\t]/g, ' ').slice(0, 96)}\t${miniGame}\t${match.enabled ? 1 : 0}\t${filters[0]}\t${filters[1]}`;
      }).join('\n');
    const temporary = path.join(directory, 'servers.tmp');
    await fs.writeFile(temporary, lines, { encoding: 'utf8', mode: 0o600 });
    await replaceLiveFile(temporary, path.join(directory, 'servers.txt'));
    const directoryTemp = path.join(directory, 'directory.tmp');
    await fs.writeFile(directoryTemp, JSON.stringify(servers.map(({ owner, port }) => ({ owner, port }))), { encoding: 'utf8', mode: 0o600 });
    await replaceLiveFile(directoryTemp, path.join(directory, 'directory.json'));
  } catch (error) { console.error('Live:', error.message); }
  finally { running = false; }
}

input.once('line', line => {
  try {
    if (line.length > 8192) throw new Error('Invalid initialization');
    const init = JSON.parse(line);
    if (typeof init.directory !== 'string' || typeof init.username !== 'string' || !init.username) throw new Error('Invalid initialization');
    directory = init.directory;
    username = init.username;
    bridge = new GameBridge({ apiUrl: init.apiUrl, token: init.token });
    leaderboardSubmitAt = Date.now() + leaderboardSubmitIntervalMs;
    void tick();
    timer = setInterval(() => void tick(), 2000);
    social = new SocialBridge({ bridge, directory });
    void social.refresh();
    socialTimer = setInterval(() => {
      void social.refresh();
      void social.syncAchievements();
      void social.syncGameAvatar();
    }, 2000);
    socialActionTimer = setInterval(() => {
      void social.processAction().catch(error => console.error('Live social action:', error.message));
      void social.refreshProfile().catch(error => console.error('Live social profile:', error.message));
    }, 200);
    leaderboardTimer = setInterval(() => {
      void processLeaderboardRequest();
      void processLeaderboardSubmissions();
    }, 150);
  } catch { console.error('Live: invalid initialization'); process.exitCode = 1; input.close(); }
});
input.on('close', () => {
  stopped = true;
  clearInterval(timer);
  clearInterval(leaderboardTimer);
  clearInterval(socialTimer);
  clearInterval(socialActionTimer);
  void (async () => {
    while (running) await new Promise(resolve => setTimeout(resolve, 50));
    while (leaderboardBusy || leaderboardSubmissionBusy) await new Promise(resolve => setTimeout(resolve, 50));
    await processLeaderboardSubmissions(true);
    await social?.close();
    await bridge?.close();
  })();
});
