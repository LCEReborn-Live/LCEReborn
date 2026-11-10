import net from 'node:net';
import tls from 'node:tls';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

function connect(endpoint, timeoutMs = 3000, ca) {
  return new Promise((resolveSocket, reject) => {
    const socket = endpoint.tls ? tls.connect({ host: endpoint.address, port: endpoint.port, ...(ca ? { ca } : {}),
      ...(net.isIP(endpoint.address) ? {} : { servername: endpoint.address }) }) : net.connect({ host: endpoint.address, port: endpoint.port });
    const timer = setTimeout(() => socket.destroy(new Error('Connection timed out')), timeoutMs);
    socket.once(endpoint.tls ? 'secureConnect' : 'connect', () => {
      clearTimeout(timer);
      socket.removeListener('error', reject);
      resolveSocket(socket);
    });
    socket.once('error', error => { clearTimeout(timer); reject(error); });
  });
}

export async function startJoin({ apiUrl, token, owner, localPort = 25571, ca }) {
  if (!/^https?:\/\//.test(apiUrl)) throw new Error('LIVE_API_URL must be an HTTP(S) URL');
  if (!/^[a-f0-9]{64}$/.test(token)) throw new Error('LIVE_TOKEN must be a session token');
  if (typeof owner !== 'string' || !owner) throw new Error('LIVE_JOIN_OWNER is required');
  if (!Number.isInteger(localPort) || localPort < 0 || localPort > 65535) throw new Error('Invalid local port');
  async function candidates() {
    const response = await fetch(new URL('/v1/servers', apiUrl), {
      headers: { authorization: `Bearer ${token}` }, signal: AbortSignal.timeout(5000)
    });
    if (!response.ok) throw new Error(`Directory HTTP ${response.status}`);
    const listing = (await response.json()).servers.find(server => server.owner.toLowerCase() === owner.toLowerCase());
    if (!listing) throw new Error(`Server owned by ${owner} not found`);
    return { ownerId: listing.owner_id, endpoints: [
      ...(listing.direct ? [{ endpoint: listing.direct, ticketed: Boolean(listing.direct_ticketed), route: 'direct' }] : []),
      ...(listing.relay ? [{ endpoint: listing.relay, ticketed: true, route: 'relay' }] : [])
    ] };
  }
  async function ticket(ownerId) {
    const response = await fetch(new URL(`/v1/servers/${ownerId}/join-ticket`, apiUrl), {
      method: 'POST', headers: { authorization: `Bearer ${token}` }, signal: AbortSignal.timeout(5000)
    });
    if (!response.ok) throw new Error(`Join ticket HTTP ${response.status}`);
    return (await response.json()).ticket;
  }
  const server = net.createServer(async client => {
    client.pause();
    let upstream;
    try {
      const { ownerId, endpoints } = await candidates();
      let lastError;
      for (const candidate of endpoints) {
        try {
          const joinTicket = candidate.ticketed ? await ticket(ownerId) : null;
          upstream = await connect(candidate.endpoint, 3000, ca);
          if (joinTicket) upstream.write(JSON.stringify({ ticket: joinTicket }) + '\n');
          console.log(`Connected to ${owner} via ${candidate.route} route`);
          break;
        } catch (error) { lastError = error; }
      }
      if (!upstream) throw lastError || new Error('No connection candidates');
      client.pipe(upstream);
      upstream.pipe(client);
      client.resume();
      client.on('error', () => upstream.destroy());
      upstream.on('error', () => client.destroy());
      client.once('close', () => upstream.destroy());
      upstream.once('close', () => client.destroy());
    } catch (error) {
      console.error('Join failed:', error.message);
      client.destroy();
    }
  });
  await new Promise((resolveListen, reject) => {
    server.once('error', reject);
    server.listen(localPort, '127.0.0.1', () => {
      server.removeListener('error', reject);
      resolveListen();
    });
  });
  return server;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  startJoin({ apiUrl: process.env.LIVE_API_URL || 'http://127.0.0.1:8080', token: process.env.LIVE_TOKEN || '',
    owner: process.env.LIVE_JOIN_OWNER || '', localPort: Number(process.env.LIVE_JOIN_PORT || 25571) })
    .then(server => console.log(`Join bridge listening on 127.0.0.1:${server.address().port}`))
    .catch(error => { console.error(error.message); process.exitCode = 1; });
}
