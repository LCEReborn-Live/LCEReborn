import fs from 'node:fs/promises';
import path from 'node:path';

const field = value => String(value ?? '').replace(/[\r\n\t]/g, ' ').slice(0, 96);
const validId = value => Number.isSafeInteger(Number(value)) && Number(value) > 0;
const catalog = 'lce-wiiu-v1';

// Only the launcher-owned process holds the bearer token. The game exchanges
// bounded snapshots and commands through its private per-session directory.
export class SocialBridge {
  constructor({ bridge, directory, now = Date.now }) {
    Object.assign(this, { bridge, directory, now });
    this.skins = new Map();
    this.worlds = [];
    this.rows = [];
    this.events = [];
    this.seen = null;
    this.sequence = now();
    this.closed = false;
    this.profileCache = new Map();
    this.achievementRetryAt = 0;
    this.gameAvatarRetryAt = 0;
  }

  async write(name, text) {
    if (this.closed) return;
    const file = path.join(this.directory, name);
    await fs.writeFile(file + '.tmp', text, { mode: 0o600 });
    await fs.rename(file + '.tmp', file);
  }

  async read(name) {
    try {
      const file = path.join(this.directory, name);
      if ((await fs.stat(file)).size > 65536) throw new Error('Oversized bridge file');
      return await fs.readFile(file, 'utf8');
    } catch (error) { if (error.code === 'ENOENT') return ''; throw error; }
  }

  async request(url, method = 'GET', body) {
    const response = await this.bridge.fetchImpl(new URL(url, this.bridge.apiUrl), {
      method, headers: { authorization: `Bearer ${this.bridge.token}`,
        ...(body ? { 'content-type': 'application/json' } : {}) },
      body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(8000)
    });
    if (!response.ok) throw Object.assign(new Error(`Live HTTP ${response.status}`), { status: response.status });
    return response.json();
  }

  errorText(error) {
    if (error.status === 401) return 'Sign in again in the launcher.';
    if (error.status === 429) return 'Too many requests. Please wait.';
    if (error.status === 404) return 'This request or world is no longer available.';
    if (error.status === 403) return 'You do not have permission to join.';
    return 'Live is unavailable. Retrying...';
  }

  async skin(user, signal) {
    const id = Number(user.id), version = Number(user.cosmetics_updated_at) || 0;
    const previous = this.skins.get(id);
    if (previous?.version === version && (version || this.now() - previous.checked < 60000)) return previous.revision;
    const file = `friend-${id}.skin.png`;
    try {
      const response = await this.bridge.fetchImpl(new URL(`/v1/users/${id}/skin?avatar=1`, this.bridge.apiUrl), {
        signal: signal ? AbortSignal.any([signal, AbortSignal.timeout(5000)]) : AbortSignal.timeout(5000)
      });
      if (response.status === 404) {
        await fs.unlink(path.join(this.directory, file)).catch(() => {});
        this.skins.set(id, { version, revision: '0', checked: this.now() });
        return '0';
      }
      if (!response.ok || !response.headers.get('content-type')?.startsWith('image/png')) return previous?.revision || '0';
      const chunks = []; let length = 0;
      for await (const chunk of response.body) {
        length += chunk.length;
        if (length > 512 * 1024) throw new Error('Oversized skin');
        chunks.push(chunk);
      }
      const png = Buffer.concat(chunks);
      if (png.length < 24 || png.toString('hex', 0, 8) !== '89504e470d0a1a0a'
          || png.readUInt32BE(16) !== 64 || ![32, 64].includes(png.readUInt32BE(20))) return previous?.revision || '0';
      const revision = String(version || this.now());
      await this.write(file, png);
      this.skins.set(id, { version, revision, checked: this.now() });
      return revision;
    } catch { return previous?.revision || '0'; }
  }

  async refresh() {
    if (this.refreshing || this.closed) return this.refreshing;
    this.refreshing = this.refreshNow().finally(() => { this.refreshing = null; });
    return this.refreshing;
  }

  async refreshNow() {
    try {
      const data = await Promise.all(['/v1/friends', '/v1/friends/requests',
        '/v1/friends/requests/sent', '/v1/game-invites'].map(url => this.request(url)));
      const arrays = [data[0].friends, data[1].incoming, data[2].sent, data[3].invitations];
      if (!arrays.every(Array.isArray)) throw new Error('Invalid social response');
      const kinds = ['F', 'R', 'S', 'I'], rows = [], active = new Set();
      for (let group = 0; group < arrays.length; group++) for (const raw of arrays[group].slice(0, 50)) {
        const user = group === 3 ? { ...raw, id: raw.owner_id, username: raw.owner, online: true } : raw;
        if (!validId(user.id) || !/^[A-Za-z0-9_]{3,20}$/.test(user.username)) continue;
        const id = Number(user.id);
        active.add(id);
        rows.push({ ...user, kind: kinds[group], id, revision: this.skins.get(id)?.revision || '0' });
      }
      // A slow avatar server cannot stall social polling for every friend.
      // Deduplicate users and cap both concurrency and total download time.
      const users = [...new Map(rows.map(row => [row.id, row])).values()];
      const avatarDeadline = AbortSignal.timeout(7000);
      let next = 0;
      await Promise.all(Array.from({ length: Math.min(4, users.length) }, async () => {
        while (next < users.length && !avatarDeadline.aborted) await this.skin(users[next++], avatarDeadline);
      }));
      for (const row of rows) row.revision = this.skins.get(row.id)?.revision || '0';
      this.rows = rows;
      for (const id of this.skins.keys()) if (!active.has(id)) {
        await fs.unlink(path.join(this.directory, `friend-${id}.skin.png`)).catch(() => {});
        this.skins.delete(id);
      }
      const seen = new Set();
      for (const row of rows.filter(row => row.kind === 'R' || row.kind === 'I')) {
        const key = `${row.kind}:${row.id}:${row.expires_at || ''}`;
        seen.add(key);
        if (this.seen && !this.seen.has(key)) this.events.push([++this.sequence, row.kind,
          row.id, row.username, row.revision].join('\t'));
      }
      this.seen = seen;
      this.events = this.events.slice(-32);
      await this.write('social-events.tsv', this.events.join('\n'));
      await this.publish();
      await this.write('social-status.tsv', `ok\t${this.now()}\t`);
    } catch (error) {
      await this.write('social-status.tsv', `error\t${this.now()}\t${this.errorText(error)}`);
    }
  }

  async publish() {
    const text = this.rows.map(row => [row.kind, row.id, row.username, row.online ? 1 : 0,
      field(row.presence), field(row.activity), row.revision,
      this.worlds.some(world => Number(world.ownerId) === row.id) ? 1 : 0, field(row.name), field(row.platform)].join('\t')).join('\n');
    // Serialize writes: world discovery and social polling may complete together.
    this.publishing = (this.publishing || Promise.resolve()).catch(() => {}).then(() => this.write('social.tsv', text));
    await this.publishing;
  }

  async updateWorlds(worlds) { this.worlds = worlds; if (this.seen) await this.publish(); }

  async processAction() {
    if (this.actionBusy || this.closed) return;
    this.actionBusy = true;
    try {
      const raw = await this.read('social-action.tsv');
      if (!raw) return;
      await fs.unlink(path.join(this.directory, 'social-action.tsv'));
      const match = /^(\d{1,18})\t([ACDJXNV])\t(.{1,64})$/.exec(raw.trim());
      if (!match) return;
      const [, requestId, operation, target] = match;
      if (operation === 'N' && !/^[A-Za-z0-9_]{3,20}$/.test(target)) return;
      if (operation !== 'N' && !/^[1-9]\d{0,9}$/.test(target)) return;
      try {
        if (operation === 'J') {
          const worlds = await this.bridge.refresh();
          const world = worlds.find(world => Number(world.ownerId) === Number(target));
          if (!world || !Number.isInteger(world.port) || world.port < 1 || world.port > 65535)
            throw Object.assign(new Error('World unavailable'), { status: 404 });
          await this.write('join.txt', String(world.port));
        } else {
          const friend = operation === 'V' ? this.rows.find(row => row.kind === 'F' && row.id === Number(target)) : null;
          if (operation === 'V' && !friend) throw Object.assign(new Error('Friend not found'), { status: 404 });
          const url = operation === 'N' ? '/v1/friends/requests'
            : operation === 'A' ? `/v1/friends/requests/${target}/accept`
            : operation === 'D' ? `/v1/friends/requests/${target}`
            : operation === 'C' ? `/v1/friends/requests/sent/${target}`
            : operation === 'V' ? '/v1/servers/mine/invites' : `/v1/game-invites/${target}`;
          await this.request(url, operation === 'A' || operation === 'N' || operation === 'V' ? 'POST' : 'DELETE',
            operation === 'N' ? { username: target } : operation === 'V' ? { username: friend.username } : undefined);
          await this.refresh();
        }
        await this.write('social-result.tsv', `${requestId}\tok\t${operation === 'J' ? 'Joining...' : operation === 'V' ? 'Invitation sent' : 'Done'}`);
      } catch (error) {
        await this.write('social-result.tsv', `${requestId}\terror\t${this.errorText(error)}`);
      }
    } finally { this.actionBusy = false; }
  }

  async refreshProfile() {
    if (this.profileBusy || this.closed) return;
    this.profileBusy = true;
    try {
      const id = (await this.read('social-profile.txt')).trim();
      if (!/^[1-9]\d{0,9}$/.test(id)) return;
      const cached = this.profileCache.get(id);
      if (cached && this.now() - cached.at < 30000) {
        if (this.shownProfile !== id) { await this.write('social-profile.tsv', cached.text); this.shownProfile = id; }
        return;
      }
      let text;
      try {
        const data = await this.request(`/v1/users/${id}/achievements`);
        if (data.catalog !== catalog || !Array.isArray(data.unlocked) || data.unlocked.length > 93) throw new Error('Invalid achievements');
        const ids = data.unlocked.map(item => item.id);
        if (ids.some(value => !Number.isInteger(value) || value < 0 || value > 96)) throw new Error('Invalid achievement ID');
        text = `${id}\tok\t${ids.join(',')}`;
      } catch { text = `${id}\terror\t`; }
      this.profileCache.set(id, { at: this.now(), text });
      if (this.profileCache.size > 100) this.profileCache.delete(this.profileCache.keys().next().value);
      await this.write('social-profile.tsv', text);
      this.shownProfile = id;
    } finally { this.profileBusy = false; }
  }

  async syncAchievements() {
    if (this.syncBusy || this.closed || this.now() < this.achievementRetryAt) return;
    this.syncBusy = true;
    try {
      const text = await this.read('achievements.txt');
      const local = text ? text.trim().split(',').filter(Boolean).map(Number) : [];
      if (local.some(id => !Number.isInteger(id) || id < 0 || id > 96)) return;

      const remote = await this.request('/v1/me/achievements');
      if (remote.catalog !== catalog || !Array.isArray(remote.unlocked) || remote.unlocked.length > 93) return;
      const remoteIds = remote.unlocked.map(item => item.id);
      if (remoteIds.some(id => !Number.isInteger(id) || id < 0 || id > 96)) return;

      const unlockedSet = new Set([...local, ...remoteIds]);
      const unlocked = [...unlockedSet].sort((a, b) => a - b);
      const cloudText = unlocked.join(',') + (unlocked.length ? '\n' : '');
      await this.write('achievements-cloud.txt', cloudText);

      if (JSON.stringify(unlocked) !== JSON.stringify(remoteIds.sort((a, b) => a - b))) {
        await this.request('/v1/me/achievements', 'PUT', { catalog, unlocked });
      }
      this.sentAchievements = local.join(',');
    } catch { this.achievementRetryAt = this.now() + 60000; }
    finally { this.syncBusy = false; }
  }

  async syncGameAvatar() {
    if (this.gameAvatarBusy || this.closed || this.now() < this.gameAvatarRetryAt) return;
    this.gameAvatarBusy = true;
    try {
      const pixels = await this.read('game-avatar.hex');
      if (pixels === this.sentGameAvatar || !/^[a-f0-9]{512}$/.test(pixels)) return;
      await this.request('/v1/me/avatar', 'POST', { pixels });
      this.sentGameAvatar = pixels;
    } catch {
      this.gameAvatarRetryAt = this.now() + 60000;
    } finally {
      this.gameAvatarBusy = false;
    }
  }

  async close() {
    this.closed = true;
    await this.refreshing;
    await this.publishing;
  }
}
