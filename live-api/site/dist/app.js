const $ = id => document.getElementById(id);
const authView = $('auth-view');
const accountView = $('account-view');
const authMessage = $('auth-message');
const accountMessage = $('account-message');
const localHost = ['localhost', '127.0.0.1'].includes(location.hostname);
const sameOriginApi = localHost || location.hostname === 'api.kerneldev.qzz.io';
const apiBase = sameOriginApi ? location.origin : 'https://api.kerneldev.qzz.io';
const tokenKey = 'lce-live-token';
let token = localStorage.getItem(tokenKey) || sessionStorage.getItem(tokenKey) || '';
if (token) localStorage.setItem(tokenKey, token);
sessionStorage.removeItem(tokenKey);
const isAccountPage = location.pathname === '/account';
if (isAccountPage) {
  document.body.classList.add('account-page');
  if (!token) location.replace('/#dashboard');
}
let authMode = 'login';
let presenceTimer = null;
let liveEvents = null;
let liveEventsReconnect = null;
let dashboardRefreshTimer = null;
let dashboardNeedsCosmetics = false;
let skinViewer = null;
if ('BroadcastChannel' in window) {
  const channel = new BroadcastChannel('lce-live-auth');
  channel.onmessage = event => {
    if (event.data?.type === 'request-session' && token && typeof event.data.nonce === 'string')
      channel.postMessage({ type: 'session', nonce: event.data.nonce, token });
  };
}

function setMessage(element, message, error = false) {
  element.textContent = message;
  element.classList.toggle('error', error);
}

function apiUrl(path) {
  if (!apiBase) throw new Error('Укажите адрес API');
  return new URL(`/v1${path}`, apiBase).href;
}

async function request(method, path, body) {
  const response = await fetch(apiUrl(path), {
    method,
    headers: { ...(body ? { 'content-type': 'application/json' } : {}), ...(token ? { authorization: `Bearer ${token}` } : {}) },
    body: body ? JSON.stringify(body) : undefined
  });
  let data;
  try { data = await response.json(); } catch { throw new Error(`API вернул ответ ${response.status}`); }
  if (!response.ok) {
    if (response.status === 401 && path !== '/sessions') signOut(false);
    throw new Error(data.error || `Ошибка ${response.status}`);
  }
  return data;
}

async function checkConnection() {
  try {
    const response = await fetch(apiUrl('/health'), { signal: AbortSignal.timeout(4000) });
    if (!response.ok) throw new Error();
    const health = await response.json();
    document.querySelector('.relay-panel').hidden = health.relay === 'disabled';
  } catch {
    document.querySelector('.relay-panel').hidden = true;
  }
}

function setAuthMode(mode) {
  authMode = mode;
  $('tab-login').classList.toggle('active', mode === 'login');
  $('tab-register').classList.toggle('active', mode === 'register');
  $('tab-login').setAttribute('aria-selected', mode === 'login');
  $('tab-register').setAttribute('aria-selected', mode === 'register');
  $('password').autocomplete = mode === 'login' ? 'current-password' : 'new-password';
  $('auth-submit').firstChild.textContent = mode === 'login' ? 'Войти ' : 'Создать аккаунт ';
  setMessage(authMessage, '');
}

function signOut(clearRemote = true) {
  const old = token;
  token = '';
  localStorage.removeItem('lce-live-token');
  if (presenceTimer) clearInterval(presenceTimer);
  presenceTimer = null;
  if (liveEvents) liveEvents.close();
  liveEvents = null;
  if (liveEventsReconnect) clearTimeout(liveEventsReconnect);
  liveEventsReconnect = null;
  if (dashboardRefreshTimer) clearTimeout(dashboardRefreshTimer);
  dashboardRefreshTimer = null;
  dashboardNeedsCosmetics = false;
  accountView.hidden = true;
  authView.hidden = false;
  if (clearRemote && old) {
    fetch(apiUrl('/sessions/current'), { method: 'DELETE', keepalive: true, headers: { authorization: `Bearer ${old}` } }).catch(() => {});
  }
  if (isAccountPage) location.replace('/#dashboard');
}

function startPresence() {
  if (presenceTimer) return;
  const update = () => { if (token) void request('POST', '/presence', { status: 'website' }).catch(() => {}); };
  update();
  presenceTimer = setInterval(update, 60 * 1000);
}

function scheduleRealtimeRefresh(refreshCosmetics = false) {
  dashboardNeedsCosmetics ||= refreshCosmetics;
  if (dashboardRefreshTimer) clearTimeout(dashboardRefreshTimer);
  dashboardRefreshTimer = setTimeout(() => {
    const includeCosmetics = dashboardNeedsCosmetics;
    dashboardRefreshTimer = null;
    dashboardNeedsCosmetics = false;
    void loadDashboard(includeCosmetics);
  }, 250);
}

async function connectLiveEvents() {
  if (!token || liveEvents || liveEventsReconnect) return;
  const session = token;
  try {
    const { ticket } = await request('POST', '/events/ticket');
    if (!token || token !== session) return;
    const url = new URL(apiUrl('/events'));
    url.searchParams.set('ticket', ticket);
    const stream = new EventSource(url.href);
    liveEvents = stream;
    stream.addEventListener('update', event => {
      let refreshCosmetics = false;
      try { refreshCosmetics = JSON.parse(event.data).kind === 'cosmetics'; } catch {}
      scheduleRealtimeRefresh(refreshCosmetics);
    });
    stream.onerror = () => {
      stream.close();
      if (liveEvents === stream) liveEvents = null;
      if (token && !liveEventsReconnect) {
        liveEventsReconnect = setTimeout(() => {
          liveEventsReconnect = null;
          void connectLiveEvents();
        }, 3000);
      }
    };
  } catch {
    if (token === session && !liveEventsReconnect) {
      liveEventsReconnect = setTimeout(() => {
        liveEventsReconnect = null;
        void connectLiveEvents();
      }, 5000);
    }
  }
}

function empty(container, text) {
  const item = document.createElement('div');
  item.className = 'empty';
  item.textContent = text;
  container.replaceChildren(item);
}

function row(title, subtitle, actions = [], avatarUrl = null) {
  const item = document.createElement('div');
  item.className = 'item';
  const words = document.createElement('div');
  words.className = 'item-words';
  const strong = document.createElement('strong');
  strong.textContent = title;
  const small = document.createElement('small');
  small.textContent = subtitle;
  words.append(strong, small);
  const buttons = document.createElement('div');
  buttons.className = 'item-actions';
  for (const action of actions) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = action.label;
    button.addEventListener('click', action.run);
    buttons.append(button);
  }
  if (avatarUrl) {
    const avatar = new Image();
    avatar.className = 'item-avatar';
    avatar.alt = '';
    avatar.hidden = true;
    renderSkinFace(avatarUrl, avatar);
    item.append(avatar);
  }
  item.append(words, buttons);
  return item;
}

function friendPresence(friend) {
  if (!friend.online) return 'Не в сети';
  const activities = { battle: 'Battle', tumble: 'Tumble', glide: 'Glide', build: 'Build' };
  if (friend.presence === 'minigame' && activities[friend.activity]) return `В сети | В игре: ${activities[friend.activity]}`;
  if (friend.presence === 'game') return 'В сети | В игре';
  if (friend.presence === 'website') return 'В сети | На сайте';
  return 'В сети | В лаунчере';
}

function skinUrl(userId, kind, version = 0) {
  const url = new URL(`/v1/users/${userId}/${kind}`, apiBase);
  if (version) url.searchParams.set('v', String(version));
  return url.href;
}

function renderSkinFace(url, imageElement) {
  const source = new Image();
  source.crossOrigin = 'anonymous';
  source.onload = () => {
    const face = document.createElement('canvas');
    face.width = 8;
    face.height = 8;
    const context = face.getContext('2d');
    context.imageSmoothingEnabled = false;
    context.drawImage(source, 8, 8, 8, 8, 0, 0, 8, 8);
    context.drawImage(source, 40, 8, 8, 8, 0, 0, 8, 8);
    imageElement.src = face.toDataURL('image/png');
    imageElement.hidden = false;
  };
  source.src = url;
}

async function showCosmetics(user, cosmetics) {
  const imageUrl = skinUrl(user.id, 'skin', cosmetics.updated_at);
  const profileAvatar = $('profile-avatar');
  profileAvatar.hidden = true;
  if (cosmetics.skin) renderSkinFace(imageUrl, profileAvatar);
  const emptyPreview = $('skin-preview-empty');
  const previewWrap = $('skin-preview').parentElement;
  if (!window.skinview3d) {
    setMessage($('cosmetics-message'), 'Не удалось загрузить 3D предпросмотр', true);
    return;
  }
  try {
    if (!skinViewer) {
      skinViewer = new skinview3d.SkinViewer({ canvas: $('skin-preview'), width: 260, height: 270 });
      skinViewer.autoRotate = true;
      skinViewer.animation = new skinview3d.WalkingAnimation();
    }
    if (cosmetics.skin) {
      await skinViewer.loadSkin(imageUrl);
      previewWrap.classList.add('has-skin');
      emptyPreview.hidden = true;
    } else {
      await skinViewer.loadSkin(null);
      previewWrap.classList.remove('has-skin');
      emptyPreview.hidden = false;
    }
    await skinViewer.loadCape(cosmetics.cape ? skinUrl(user.id, 'cape', cosmetics.updated_at) : null);
  } catch {
    setMessage($('cosmetics-message'), '3D предпросмотр недоступен в этом браузере', true);
  }
}

async function uploadCosmetic(kind, file) {
  const message = $('cosmetics-message');
  const maxBytes = 512 * 1024;
  const expected = kind === 'skin' ? [64, 64] : [64, 32];
  if (!file || file.type !== 'image/png' || file.size > maxBytes) {
    setMessage(message, 'Нужен PNG-файл размером до 512 КБ', true);
    return;
  }
  const image = new Image();
  const localUrl = URL.createObjectURL(file);
  try {
    await new Promise((resolve, reject) => {
      image.onload = resolve;
      image.onerror = reject;
      image.src = localUrl;
    });
    if (image.naturalWidth !== expected[0] || image.naturalHeight !== expected[1]) {
      throw new Error(kind === 'skin' ? 'Размер скина должен быть 64×64 пикселя' : 'Размер плаща должен быть 64×32 пикселя');
    }
    setMessage(message, 'Сохраняю…');
    const response = await fetch(apiUrl(`/me/cosmetics/${kind}`), {
      method: 'PUT', headers: { authorization: `Bearer ${token}`, 'content-type': 'image/png' }, body: file
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || `Ошибка ${response.status}`);
    const [me, cosmetics] = await Promise.all([request('GET', '/me'), request('GET', '/me/cosmetics')]);
    await showCosmetics(me, cosmetics);
    setMessage(message, kind === 'skin' ? 'Скин сохранён' : 'Плащ сохранён');
  } catch (error) {
    setMessage(message, error.message || 'Не удалось загрузить PNG', true);
  } finally {
    URL.revokeObjectURL(localUrl);
  }
}

async function action(method, path, body, success) {
  try {
    await request(method, path, body);
    setMessage(accountMessage, success);
    await loadDashboard(false);
  } catch (error) { setMessage(accountMessage, error.message, true); }
}

async function loadDashboard(refreshCosmetics = true) {
  if (!token) return;
  try {
    const [me, friends, incoming, sent, catalog, cosmetics] = await Promise.all([
      request('GET', '/me'), request('GET', '/friends'), request('GET', '/friends/requests'),
      request('GET', '/friends/requests/sent'), request('GET', '/servers'),
      refreshCosmetics ? request('GET', '/me/cosmetics') : Promise.resolve(null)
    ]);
    if (!isAccountPage) { location.assign('/account'); return; }
    $('account-name').textContent = me.username;
    if (refreshCosmetics) await showCosmetics(me, cosmetics);
    authView.hidden = true;
    accountView.hidden = false;
    startPresence();
    void connectLiveEvents();
    $('friend-count').textContent = friends.friends.length;
    $('server-count').textContent = catalog.servers.length;
    const friendList = $('friends-list');
    friendList.replaceChildren(...friends.friends.map(friend => row(friend.username, `${friend.online ? '● ' : ''}${friendPresence(friend)}`, [
      { label: 'Удалить', run: () => action('DELETE', `/friends/${friend.id}`, null, 'Друг удалён') }
    ], skinUrl(friend.id, 'skin', friend.cosmetics_updated_at || 0))));
    if (!friends.friends.length) empty(friendList, 'Пока нет друзей. Добавь первого по имени.');
    const requestList = $('requests-list');
    requestList.replaceChildren(
      ...incoming.incoming.map(person => row(person.username, 'Хочет добавить тебя', [
        { label: 'Принять', run: () => action('POST', `/friends/requests/${person.id}/accept`, null, 'Заявка принята') },
        { label: 'Отклонить', run: () => action('DELETE', `/friends/requests/${person.id}`, null, 'Заявка отклонена') }
      ])),
      ...sent.sent.map(person => row(person.username, 'Заявка отправлена', [
        { label: 'Отменить', run: () => action('DELETE', `/friends/requests/sent/${person.id}`, null, 'Заявка отменена') }
      ]))
    );
    if (!incoming.incoming.length && !sent.sent.length) empty(requestList, 'Новых заявок нет.');
    const serverList = $('servers-list');
    serverList.replaceChildren(...catalog.servers.map(server => {
      const direct = server.direct ? `${server.direct.address}:${server.direct.port}` : null;
      const relay = server.relay ? `${server.relay.address}:${server.relay.port}` : null;
      return row(server.name, `${server.owner} · ${direct ? `Прямой: ${direct}` : ''}${direct && relay ? ' · ' : ''}${relay ? `Relay: ${relay}` : ''}`,
        [{ label: 'Копировать адрес', run: async () => {
          try { await navigator.clipboard.writeText(direct || relay || ''); setMessage(accountMessage, 'Адрес скопирован'); }
          catch { setMessage(accountMessage, 'Не удалось скопировать адрес', true); }
        } }]);
    }));
    if (!catalog.servers.length) empty(serverList, 'Серверов пока нет.');
  } catch (error) { setMessage(accountMessage, error.message, true); }
}

$('tab-login').addEventListener('click', () => setAuthMode('login'));
$('tab-register').addEventListener('click', () => setAuthMode('register'));
$('auth-form').addEventListener('submit', async event => {
  event.preventDefault();
  const username = $('username').value.trim();
  const password = $('password').value;
  setMessage(authMessage, 'Подключаемся…');
  try {
    if (authMode === 'register') await request('POST', '/accounts', { username, password });
    const session = await request('POST', '/sessions', { username, password });
    token = session.token;
    localStorage.setItem('lce-live-token', token);
    $('password').value = '';
    setMessage(authMessage, '');
    await loadDashboard();
  } catch (error) { setMessage(authMessage, error.message, true); }
});
$('logout').addEventListener('click', () => signOut());
$('refresh-data').addEventListener('click', () => void loadDashboard());
$('skin-file').addEventListener('change', event => {
  const file = event.target.files?.[0];
  if (file) void uploadCosmetic('skin', file);
  event.target.value = '';
});
$('cape-file').addEventListener('change', event => {
  const file = event.target.files?.[0];
  if (file) void uploadCosmetic('cape', file);
  event.target.value = '';
});
async function removeCosmetic(kind) {
  try {
    await request('DELETE', `/me/cosmetics/${kind}`);
    const [me, cosmetics] = await Promise.all([request('GET', '/me'), request('GET', '/me/cosmetics')]);
    await showCosmetics(me, cosmetics);
    setMessage($('cosmetics-message'), kind === 'skin' ? 'Скин удалён' : 'Плащ удалён');
  } catch (error) { setMessage($('cosmetics-message'), error.message, true); }
}
$('skin-remove').addEventListener('click', () => void removeCosmetic('skin'));
$('cape-remove').addEventListener('click', () => void removeCosmetic('cape'));
$('friend-form').addEventListener('submit', event => {
  event.preventDefault();
  const username = $('friend-name').value.trim();
  $('friend-name').value = '';
  void action('POST', '/friends/requests', { username }, 'Заявка отправлена');
});
$('server-form').addEventListener('submit', event => {
  event.preventDefault();
  void action('PUT', '/servers/mine', {
    name: $('server-name').value.trim(), address: $('server-address').value.trim(), port: Number($('server-port').value)
  }, 'Сервер опубликован');
});
$('relay-form').addEventListener('submit', async event => {
  event.preventDefault();
  try {
    const room = await request('POST', '/relay/rooms', { name: $('relay-name').value.trim() });
    const tlsSetup = room.tls ? '$env:LIVE_RELAY_TLS = "1"\n' : '';
    $('relay-result').textContent = `Комната: ${room.id}\nАдрес игроков: ${room.address}:${room.port}\nКоманды на машине хоста (PowerShell):\n${tlsSetup}node src/relay-agent.js ${room.id} ${room.secret} ${room.address} ${room.control_port} 25570\n\nСохраните секрет сейчас: повторно API его не покажет.`;
    $('relay-result').hidden = false;
    setMessage(accountMessage, 'Relay комната создана');
    await loadDashboard();
  } catch (error) { setMessage(accountMessage, error.message, true); }
});

void checkConnection();
if (token) void loadDashboard();

