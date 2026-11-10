import fs from 'node:fs/promises';
import path from 'node:path';
import readline from 'node:readline';
import { GameBridge } from './game-bridge.js';

// One instance per launched game. The launcher sends credentials over stdin.
console.log = () => {};
const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
let bridge, directory, username, running = false, stopped = false, lastWorld = '';
let timer, leaderboardTimer, leaderboardBusy = false, leaderboardSubmissionBusy = false;
let leaderboardRetryAt = 0;

const leaderboardBoards = ['traveling', 'mining', 'farming', 'kills'];

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
    const columns = { traveling: 4, mining: 7, farming: 6, kills: 7 }[board];
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

async function processLeaderboardSubmissions() {
  if (leaderboardSubmissionBusy || stopped || !directory || Date.now() < leaderboardRetryAt) return;
  leaderboardSubmissionBusy = true;
  try {
  let files;
  try { files = await fs.readdir(directory); }
  catch { return; }
  for (const file of files.filter(name => /^leaderboard-submit-\d+-\d+\.tsv$/.test(name)).sort()) {
    const fullPath = path.join(directory, file);
    let rows;
    try { rows = (await fs.readFile(fullPath, 'utf8')).trim().split('\n'); }
    catch (error) { if (error.code !== 'ENOENT') console.error('Live leaderboard:', error.message); continue; }
    let complete = true;
    for (const row of rows) {
      const [boardText, ...valuesText] = row.split('\t');
      const board = leaderboardBoards[Number(boardText)];
      const values = valuesText.map(Number);
      const columns = { traveling: 4, mining: 7, farming: 6, kills: 7 }[board];
      if (!board || values.length !== columns || values.some(value => !Number.isSafeInteger(value) || value < 0)) {
        complete = false;
        break;
      }
      try {
        const response = await bridge.fetchImpl(new URL(`/v1/leaderboards/${board}`, bridge.apiUrl), {
          method: 'POST', headers: { authorization: `Bearer ${bridge.token}`, 'content-type': 'application/json' },
          body: JSON.stringify({ values }), signal: AbortSignal.timeout(10000)
        });
        if (!response.ok) throw new Error(`Leaderboard HTTP ${response.status}`);
      } catch (error) {
        console.error('Live leaderboard submit:', error.message);
        complete = false;
        break;
      }
    }
    if (complete) await fs.unlink(fullPath).catch(() => {});
    else { leaderboardRetryAt = Date.now() + 5000; break; }
  }
  } finally { leaderboardSubmissionBusy = false; }
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
      const match = /^1\t(\d{1,5})\t([01])$/.exec(world);
      if (match && Number(match[1]) > 0 && Number(match[1]) <= 65535 && world !== lastWorld) {
        await bridge.setWorld({ name: `${username}'s world`.slice(0, 64), port: Number(match[1]), private: match[2] === '1' });
        lastWorld = world;
      }
    }
    const servers = await bridge.refresh();
    const lines = servers.filter(s => s.owner.toLowerCase() !== username.toLowerCase())
      .map(s => `${s.port}\t${String(s.name || s.owner).replace(/[\r\n\t]/g, ' ').slice(0, 96)}`).join('\n');
    const temporary = path.join(directory, 'servers.tmp');
    await fs.writeFile(temporary, lines, { encoding: 'utf8', mode: 0o600 });
    await fs.rename(temporary, path.join(directory, 'servers.txt'));
    const directoryTemp = path.join(directory, 'directory.tmp');
    await fs.writeFile(directoryTemp, JSON.stringify(servers.map(({ owner, port }) => ({ owner, port }))), { encoding: 'utf8', mode: 0o600 });
    await fs.rename(directoryTemp, path.join(directory, 'directory.json'));
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
    void tick();
    timer = setInterval(() => void tick(), 2000);
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
  void (async () => {
    while (running) await new Promise(resolve => setTimeout(resolve, 50));
    while (leaderboardBusy || leaderboardSubmissionBusy) await new Promise(resolve => setTimeout(resolve, 50));
    await bridge?.close();
  })();
});
