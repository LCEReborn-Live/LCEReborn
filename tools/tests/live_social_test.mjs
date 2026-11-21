import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { SocialBridge } from '../../live/social-bridge.js';

async function setup(t) {
  const directory = await fs.mkdtemp(path.join(tmpdir(), 'live-social-'));
  const calls = [];
  const state = { friends: [], incoming: [], sent: [], invitations: [], status: 200, worlds: [] };
  const bridge = { apiUrl: 'https://example.test', token: 'a'.repeat(64),
    refresh: async () => state.worlds,
    fetchImpl: async (url, options = {}) => {
      calls.push({ url: url.pathname, ...options });
      if (url.pathname.endsWith('/skin')) return new Response('', { status: 404 });
      if (state.status !== 200) return new Response('{}', { status: state.status });
      let data = {};
      if (url.pathname === '/v1/friends') data = { friends: state.friends };
      if (url.pathname === '/v1/friends/requests') data = { incoming: state.incoming };
      if (url.pathname === '/v1/friends/requests/sent') data = { sent: state.sent };
      if (url.pathname === '/v1/game-invites') data = { invitations: state.invitations };
      if (/\/users\/\d+\/achievements$/.test(url.pathname)) data = {
        catalog: 'lce-wiiu-v1', unlocked: [{ id: 0 }, { id: 96 }]
      };
      return Response.json(data);
    }
  };
  const social = new SocialBridge({ bridge, directory });
  t.after(async () => { await social.close(); await fs.rm(directory, { recursive: true, force: true }); });
  return { social, state, calls, bridge,
    read: name => fs.readFile(path.join(directory, name), 'utf8'),
    write: (name, text) => fs.writeFile(path.join(directory, name), text) };
}

test('friends preserve identity, reflect joinability, and notify only new requests/invites', async t => {
  const { social, state, calls, read } = await setup(t);
  state.friends = [{ id: 4, username: 'FriendOne', online: true, presence: 'minigame', activity: 'tumble', platform: 'wiiu' }];
  state.incoming = [{ id: 5, username: 'AlreadyPending' }];
  await social.refresh();
  assert.match(await read('social.tsv'), /^F\t4\tFriendOne\t1\tminigame\ttumble\t0\t0\t\twiiu\n/);
  assert.equal(await read('social-events.tsv'), '', 'Login does not replay old notifications');
  await social.updateWorlds([{ ownerId: '4', owner: 'FriendOne', port: 1234 }]);
  assert.match(await read('social.tsv'), /tumble\t0\t1/);
  state.incoming.push({ id: 6, username: 'NewRequest' });
  state.invitations.push({ owner_id: 4, owner: 'FriendOne', name: 'World', expires_at: 123 });
  await social.refresh();
  const events = await read('social-events.tsv');
  assert.equal(events.split('\n').length, 2);
  assert.match(events, /\tR\t6\tNewRequest\t/);
  assert.match(events, /\tI\t4\tFriendOne\t/);
  await social.refresh();
  assert.equal(await read('social-events.tsv'), events, 'Polls must not replay toasts');
  const snapshot = await read('social.tsv');
  state.status = 401;
  await social.refresh();
  assert.equal(await read('social.tsv'), snapshot, 'Connection errors preserve last known friends');
  assert.match(await read('social-status.tsv'), /error.*Sign in again/);
  for (const call of calls.filter(call => !call.url.endsWith('/skin')))
    assert.equal(call.headers.authorization, 'Bearer ' + 'a'.repeat(64));
  assert.ok(!snapshot.includes('a'.repeat(64)), 'Snapshot must not contain the bearer token');
});

test('actions use exact authenticated routes, acknowledge failures and authorize joins by owner ID', async t => {
  const { social, state, calls, read, write } = await setup(t);
  for (const [op, method, url] of [
    ['A', 'POST', '/v1/friends/requests/7/accept'],
    ['D', 'DELETE', '/v1/friends/requests/7'],
    ['C', 'DELETE', '/v1/friends/requests/sent/7'],
    ['X', 'DELETE', '/v1/game-invites/7']
  ]) {
    await write('social-action.tsv', `123\t${op}\t7`);
    await Promise.all([social.processAction(), social.processAction()]);
    assert.equal(calls.filter(call => call.method === method && call.url === url).length, 1);
    assert.match(await read('social-result.tsv'), /^123\tok\t/);
  }
  await write('social-action.tsv', '124\tJ\t7');
  await social.processAction();
  assert.match(await read('social-result.tsv'), /^124\terror\t/);
  state.worlds = [{ ownerId: '7', owner: 'HostOne', port: 32567 }];
  await write('social-action.tsv', '125\tJ\t7');
  await social.processAction();
  assert.equal(await read('join.txt'), '32567');
  state.status = 429;
  await write('social-action.tsv', '126\tA\t7');
  await social.processAction();
  assert.match(await read('social-result.tsv'), /^126\terror\tToo many requests/);
  const before = calls.length;
  await write('social-action.tsv', '127\tA\t../../secret');
  await social.processAction();
  assert.equal(calls.length, before);
});

test('achievement upload deduplicates snapshots and selected profiles are cached', async t => {
  const { social, calls, read, write } = await setup(t);
  await write('achievements.txt', '0,1,96\n');
  await social.syncAchievements();
  await social.syncAchievements();
  const uploads = calls.filter(call => call.method === 'PUT');
  assert.equal(uploads.length, 1);
  assert.deepEqual(JSON.parse(uploads[0].body), { catalog: 'lce-wiiu-v1', unlocked: [0, 1, 96] });
  await write('social-profile.txt', '7');
  await social.refreshProfile();
  await social.refreshProfile();
  assert.equal(await read('social-profile.tsv'), '7\tok\t0,96');
  assert.equal(calls.filter(call => call.url === '/v1/users/7/achievements').length, 1);
});
