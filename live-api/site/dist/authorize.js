const code = location.hash.slice(1).toUpperCase();
const message = document.getElementById('message');
const identity = document.getElementById('identity');
const login = document.getElementById('login');
const actions = document.getElementById('actions');
let register = false;
let token = sessionStorage.getItem('lce-live-token') || '';
document.getElementById('code').textContent = code || '—';

async function api(method, path, body) {
  const response = await fetch(`/v1${path}`, { method, headers: { ...(body ? { 'content-type': 'application/json' } : {}), ...(token ? { authorization: `Bearer ${token}` } : {}) }, body: body ? JSON.stringify(body) : undefined });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || 'Ошибка запроса');
  return data;
}
async function showAccount() {
  if (!/^[A-F0-9]{12}$/.test(code)) { identity.textContent = 'Ссылка недействительна.'; return; }
  if (token) {
    try { const user = await api('GET', '/me'); identity.textContent = `Аккаунт: ${user.username}`; login.hidden = true; actions.hidden = false; return; }
    catch { token = ''; }
  }
  identity.textContent = 'Войди в свой аккаунт LCE Live.';
  login.hidden = false;
}
login.addEventListener('submit', async event => {
  event.preventDefault();
  try {
    if (register) await api('POST', '/accounts', { username: document.getElementById('username').value.trim(), password: document.getElementById('password').value });
    const result = await api('POST', '/sessions', { username: document.getElementById('username').value.trim(), password: document.getElementById('password').value });
    token = result.token;
    sessionStorage.setItem('lce-live-token', token);
    document.getElementById('password').value = '';
    message.textContent = '';
    await showAccount();
  } catch (error) { message.textContent = error.message; }
});
document.getElementById('auth-switch').addEventListener('click', () => {
  register = !register;
  document.getElementById('login-submit').textContent = register ? 'Создать аккаунт' : 'Войти';
  document.getElementById('auth-switch').textContent = register ? 'Уже есть аккаунт? Войти' : 'Нет аккаунта? Создайте!';
  document.getElementById('password').autocomplete = register ? 'new-password' : 'current-password';
  message.textContent = '';
});
for (const [id, allow] of [['allow', true], ['deny', false]]) document.getElementById(id).addEventListener('click', async () => {
  try {
    await api('POST', '/device/authorize', { user_code: code, allow });
    actions.hidden = true;
    identity.textContent = allow ? 'Доступ разрешён. Вернись в лаунчер.' : 'Доступ отклонён.';
  } catch (error) { message.textContent = error.message; }
});
if (!token && 'BroadcastChannel' in window) {
  const channel = new BroadcastChannel('lce-live-auth');
  const nonce = crypto.randomUUID();
  channel.onmessage = event => {
    if (event.data?.type === 'session' && event.data.nonce === nonce && !token) {
      token = event.data.token;
      channel.close();
      void showAccount();
    }
  };
  channel.postMessage({ type: 'request-session', nonce });
  setTimeout(() => { channel.close(); void showAccount(); }, 800);
} else void showAccount();
