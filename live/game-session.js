import fs from 'node:fs/promises';
import path from 'node:path';
import readline from 'node:readline';
import { GameBridge } from './game-bridge.js';

// One instance per launched game. The launcher sends credentials over stdin.
console.log = () => {};
const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
let bridge, directory, username, running = false, stopped = false, lastWorld = '';
let timer;

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
  } catch { console.error('Live: invalid initialization'); process.exitCode = 1; input.close(); }
});
input.on('close', () => {
  stopped = true;
  clearInterval(timer);
  void (async () => {
    while (running) await new Promise(resolve => setTimeout(resolve, 50));
    await bridge?.close();
  })();
});
