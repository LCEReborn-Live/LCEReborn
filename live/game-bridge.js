import { startHost } from './host-agent.js';
import { startJoin } from './join-agent.js';

// The game-facing controller owns all transient rooms and loopback listeners.
// Credentials stay in memory and are never included in snapshots.
export class GameBridge {
  constructor({ apiUrl, token, hostFactory = startHost, joinFactory = startJoin, fetchImpl = fetch }) {
    if (!/^https?:\/\//.test(apiUrl) || !/^[a-f0-9]{64}$/.test(token)) throw new Error('Invalid Live session');
    Object.assign(this, { apiUrl, token, hostFactory, joinFactory, fetchImpl });
    this.host = null;
    this.joins = new Map();
    this.queue = Promise.resolve();
    this.closed = false;
  }

  async setWorld(world) {
    const task = this.queue.then(async () => {
      if (this.closed) throw new Error('Bridge is closed');
      if (this.host) { await this.host.stop(); this.host = null; }
      if (world) this.host = await this.hostFactory({ apiUrl: this.apiUrl, token: this.token,
        name: world.name, visibility: world.private ? 'invite_only' : 'friends', gamePort: world.port });
    });
    this.queue = task.catch(() => {});
    return task;
  }

  async refresh() {
    const task = this.queue.then(() => this.refreshNow());
    this.queue = task.catch(() => {});
    return task;
  }

  async refreshNow() {
    if (this.closed) throw new Error('Bridge is closed');
    const response = await this.fetchImpl(new URL('/v1/servers', this.apiUrl), {
      headers: { authorization: `Bearer ${this.token}` }, signal: AbortSignal.timeout(5000)
    });
    if (!response.ok) throw new Error(`Directory HTTP ${response.status}`);
    const { servers } = await response.json();
    if (!Array.isArray(servers)) throw new Error('Invalid directory response');
    const desired = new Map(servers.filter(s => typeof s.owner === 'string' && (s.direct || s.relay))
      .map(s => [String(s.owner_id), s]));
    for (const [id, join] of this.joins) {
      if (!desired.has(id) || desired.get(id).owner !== join.owner) {
        join.server.close();
        this.joins.delete(id);
      }
    }
    for (const [id, entry] of desired) {
      if (!this.joins.has(id)) {
        const server = await this.joinFactory({ apiUrl: this.apiUrl, token: this.token, owner: entry.owner, localPort: 0 });
        if (this.closed) { server.close(); throw new Error('Bridge is closed'); }
        this.joins.set(id, { owner: entry.owner, server });
      }
    }
    return [...desired].map(([id, entry]) => ({ ownerId: id, owner: entry.owner,
      name: entry.name, address: '127.0.0.1', port: this.joins.get(id).server.address().port }));
  }

  async close() {
    this.closed = true;
    await this.queue;
    if (this.host) { await this.host.stop(); this.host = null; }
    for (const join of this.joins.values()) join.server.close();
    this.joins.clear();
  }
}
