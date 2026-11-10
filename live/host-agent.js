import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import net from 'node:net';
import { openTcpPort } from './nat.js';
import { runAgent } from './relay-agent.js';

export async function startHost({ apiUrl, token, name, visibility = 'friends', gamePort = 25570, proxyPort = 25572, openPort = openTcpPort }) {
  if (!/^https?:\/\//.test(apiUrl)) throw new Error('LIVE_API_URL must be an HTTP(S) URL');
  if (!/^[a-f0-9]{64}$/.test(token)) throw new Error('LIVE_TOKEN must be a session token');
  if (typeof name !== 'string' || !name.trim() || name.length > 64) throw new Error('Invalid server name');
  if (!['public', 'friends', 'friends_of_friends', 'invite_only'].includes(visibility)) throw new Error('Invalid visibility');
  if (!Number.isInteger(gamePort) || gamePort < 1 || gamePort > 65535) throw new Error('Invalid game port');
  if (!Number.isInteger(proxyPort) || proxyPort < 1 || proxyPort > 65535 || proxyPort === gamePort) throw new Error('Invalid proxy port');
  const endpoint = new URL(apiUrl);
  const request = async (method, path, body) => {
    const response = await fetch(new URL(`/v1${path}`, endpoint), {
      method, headers: { authorization: `Bearer ${token}`, ...(body ? { 'content-type': 'application/json' } : {}) },
      body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(5000)
    });
    const data = await response.json();
    if (!response.ok) throw new Error(`${method} ${path}: ${data.error || response.status}`);
    return data;
  };
  const proxySockets = new Set();
  const proxy = net.createServer(client => {
    if (proxySockets.size >= 32) return client.destroy();
    proxySockets.add(client);
    client.once('close', () => proxySockets.delete(client));
    let header = Buffer.alloc(0);
    const timer = setTimeout(() => client.destroy(), 8000);
    function onData(chunk) {
      header = Buffer.concat([header, chunk]);
      const newline = header.indexOf(10);
      if ((newline < 0 && header.length > 1024) || newline > 1024) return client.destroy();
      if (newline < 0) return;
      clearTimeout(timer);
      client.removeListener('data', onData);
      client.pause();
      let ticket;
      try { ticket = JSON.parse(header.subarray(0, newline).toString('utf8')).ticket; }
      catch { return client.destroy(); }
      if (header.length > newline + 1) client.unshift(header.subarray(newline + 1));
      void (async () => {
        try {
          await request('POST', '/join-tickets/consume', { ticket });
          const game = net.connect({ host: '127.0.0.1', port: gamePort });
          await new Promise((done, reject) => {
            game.once('connect', done);
            game.once('error', reject);
          });
          proxySockets.add(game);
          game.once('close', () => proxySockets.delete(game));
          client.pipe(game);
          game.pipe(client);
          client.resume();
          client.on('error', () => game.destroy());
          game.on('error', () => client.destroy());
          client.once('close', () => game.destroy());
          game.once('close', () => client.destroy());
        } catch { client.destroy(); }
      })();
    }
    client.on('data', onData);
    client.once('close', () => clearTimeout(timer));
  });
  await new Promise((done, reject) => {
    proxy.once('error', reject);
    proxy.listen(proxyPort, '0.0.0.0', () => { proxy.removeListener('error', reject); done(); });
  });
  let room;
  try { room = await request('POST', '/relay/rooms', { name, visibility }); }
  catch (error) { proxy.close(); throw error; }
  let stopped = false;
  let control = null;
  let mapping = null;
  let mappingBusy = false;
  const timers = [];

  async function connectLoop() {
    while (!stopped) {
      try {
        control = await runAgent({ room: room.id, secret: room.secret, relayHost: room.address,
          controlPort: room.control_port, secure: Boolean(room.tls), localPort: gamePort });
        if (stopped) { control.destroy(); break; }
        if (!control.destroyed) await new Promise(resolve => control.once('close', resolve));
      } catch (error) { if (!stopped) console.error('Relay connection:', error.message); }
      control = null;
      if (!stopped) await new Promise(resolve => setTimeout(resolve, 2000));
    }
  }

  async function publishDirect() {
    if (mapping) await request('PUT', '/servers/mine', { name, address: mapping.address, port: mapping.port, visibility, ticketed: true });
  }

  async function tryMapping() {
    if (mapping || mappingBusy || stopped) return;
    mappingBusy = true;
    try {
      mapping = await openPort(proxyPort);
      if (stopped) { await mapping.remove().catch(() => {}); mapping = null; return; }
      await publishDirect();
      console.log(`Direct TCP available at ${mapping.address}:${mapping.port}`);
    } catch (error) {
      if (mapping) await mapping.remove().catch(() => {});
      mapping = null;
      console.log(`Direct TCP unavailable: ${error.message}; relay remains available`);
    } finally { mappingBusy = false; }
  }

  void connectLoop();
  await tryMapping();
  timers.push(setInterval(() => void tryMapping(), 5 * 60 * 1000));
  timers.push(setInterval(() => void publishDirect().catch(error => console.error('Directory update:', error.message)), 60 * 1000));
  timers.push(setInterval(() => {
    if (mapping && mapping.leaseSeconds) void mapping.renew().catch(async error => {
      console.error('Port mapping renewal:', error.message);
      mapping = null;
      await request('PUT', `/relay/rooms/${room.id}/listing`, { name, visibility }).catch(() => {});
    });
  }, 30 * 60 * 1000));

  return {
    room,
    get direct() { return mapping ? { address: mapping.address, port: mapping.port } : null; },
    async stop() {
      if (stopped) return;
      stopped = true;
      for (const timer of timers) clearInterval(timer);
      if (control) control.destroy();
      for (const socket of proxySockets) socket.destroy();
      await new Promise(done => proxy.close(done));
      if (mapping) await mapping.remove().catch(() => {});
      await request('DELETE', `/relay/rooms/${room.id}`).catch(() => {});
      await request('DELETE', '/servers/mine').catch(() => {});
    }
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const token = process.env.LIVE_TOKEN || '';
  const apiUrl = process.env.LIVE_API_URL || 'http://127.0.0.1:8080';
  const name = process.env.LIVE_SERVER_NAME || 'LCEReborn World';
  const visibility = process.env.LIVE_VISIBILITY || 'friends';
  const gamePort = Number(process.env.LIVE_GAME_PORT || 25570);
  const proxyPort = Number(process.env.LIVE_HOST_PROXY_PORT || 25572);
  startHost({ apiUrl, token, name, visibility, gamePort, proxyPort }).then(host => {
    const stop = () => void host.stop().finally(() => process.exit(0));
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
    console.log('Host agent running. Press Ctrl+C to stop.');
  }).catch(error => { console.error(error.message); process.exitCode = 1; });
}
