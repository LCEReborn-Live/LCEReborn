import net from 'node:net';
import tls from 'node:tls';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

function connect(host, port, secure = false, ca) {
  return new Promise((resolveSocket, reject) => {
    const socket = secure ? tls.connect({ host, port, ...(ca ? { ca } : {}), ...(net.isIP(host) ? {} : { servername: host }) }) : net.connect({ host, port });
    socket.once(secure ? 'secureConnect' : 'connect', () => { socket.removeListener('error', reject); resolveSocket(socket); });
    socket.once('error', reject);
  });
}

export async function runAgent({ room, secret, relayHost, controlPort, secure = false, ca, localHost = '127.0.0.1', localPort = 25570 }) {
  if (!/^[a-f0-9]{24}$/.test(room) || !/^[a-f0-9]{64}$/.test(secret)) throw new Error('Invalid room credentials');
  const control = await connect(relayHost, controlPort, secure, ca);
  control.setKeepAlive(true, 30000);
  control.on('error', error => console.error('Relay control error:', error.message));
  control.setEncoding('utf8');
  control.write(JSON.stringify({ role: 'host', room, secret }) + '\n');
  let readyResolve;
  let readyReject;
  const ready = new Promise((resolve, reject) => { readyResolve = resolve; readyReject = reject; });
  control.once('close', () => readyReject(new Error('Relay control connection closed')));
  let buffer = '';
  control.on('data', chunk => {
    buffer += chunk;
    if (buffer.length > 4096) return control.destroy();
    let newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, newline);
      buffer = buffer.slice(newline + 1);
      let message;
      try { message = JSON.parse(line); } catch { return control.destroy(); }
      if (message.type === 'ready') {
        console.log('Relay agent connected');
        readyResolve();
      }
      if (message.type === 'join' && /^[a-f0-9]{24}$/.test(message.id)) {
        void (async () => {
          let local;
          let tunnel;
          try {
            local = await connect(localHost, localPort);
            local.pause();
            tunnel = await connect(relayHost, controlPort, secure, ca);
            tunnel.write(JSON.stringify({ role: 'data', room, secret, join: message.id }) + '\n');
            local.pipe(tunnel);
            tunnel.pipe(local);
            local.resume();
            local.on('error', () => tunnel.destroy());
            tunnel.on('error', () => local.destroy());
            local.once('close', () => tunnel.destroy());
            tunnel.once('close', () => local.destroy());
          } catch (error) {
            if (local) local.destroy();
            if (tunnel) tunnel.destroy();
            console.error('Relay join failed:', error.message);
          }
        })();
      }
    }
  });
  await ready;
  return control;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const [room, secret, relayHost, controlPort, localPort = '25570'] = process.argv.slice(2);
  if (!room || !secret || !relayHost || !controlPort) {
    console.error('Usage: node src/relay-agent.js ROOM SECRET RELAY_HOST CONTROL_PORT [LOCAL_GAME_PORT]');
    process.exitCode = 2;
  } else {
    const settings = { room, secret, relayHost, controlPort: Number(controlPort), localPort: Number(localPort), secure: process.env.LIVE_RELAY_TLS === '1' };
    void (async () => {
      for (;;) {
        try {
          const control = await runAgent(settings);
          if (!control.destroyed) await new Promise(resolve => control.once('close', resolve));
        } catch (error) { console.error('Relay agent connection failed:', error.message); }
        await new Promise(resolve => setTimeout(resolve, 2000));
      }
    })();
  }
}
