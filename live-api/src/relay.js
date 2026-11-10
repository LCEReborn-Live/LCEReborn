import net from 'node:net';
import tls from 'node:tls';
import { randomBytes, createHash, timingSafeEqual } from 'node:crypto';

const HANDSHAKE_LIMIT = 1024;
const HANDSHAKE_MS = 8000;
const JOIN_MS = 10000;
const MAX_PLAYERS_PER_ROOM = 16;

const hash = value => createHash('sha256').update(value).digest();

function listen(server, port, host) {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, () => {
      server.removeListener('error', reject);
      resolve(server.address().port);
    });
  });
}

function close(server) {
  return new Promise(resolve => server.close(resolve));
}

function readHandshake(socket, callback) {
  let buffer = Buffer.alloc(0);
  const timer = setTimeout(() => socket.destroy(), HANDSHAKE_MS);
  function data(chunk) {
    buffer = Buffer.concat([buffer, chunk]);
    const newline = buffer.indexOf(10);
    if ((newline < 0 && buffer.length > HANDSHAKE_LIMIT) || newline > HANDSHAKE_LIMIT) return socket.destroy();
    if (newline < 0) return;
    clearTimeout(timer);
    socket.removeListener('data', data);
    try {
      const hello = JSON.parse(buffer.subarray(0, newline).toString('utf8'));
      const rest = buffer.subarray(newline + 1);
      callback(hello, rest);
    } catch { socket.destroy(); }
  }
  socket.on('data', data);
  socket.once('close', () => clearTimeout(timer));
}

export class Relay {
  constructor({ bind = '127.0.0.1', publicHost = '127.0.0.1', controlPort = 8081, firstPort = 30000, lastPort = 30099, authorizeJoin = () => false, onRoomState = () => {}, tlsOptions = null } = {}) {
    this.bind = bind;
    this.publicHost = publicHost;
    this.controlPort = controlPort;
    this.firstPort = firstPort;
    this.lastPort = lastPort;
    this.authorizeJoin = authorizeJoin;
    this.onRoomState = onRoomState;
    this.tlsOptions = tlsOptions;
    this.rooms = new Map();
    this.control = this.createServer(socket => this.handleControl(socket));
    this.control.on('tlsClientError', () => {});
    this.control.maxConnections = 128;
    this.ready = listen(this.control, controlPort, bind).then(port => { this.controlPort = port; });
  }

  createServer(handler) {
    return this.tlsOptions ? tls.createServer({ ...this.tlsOptions, handshakeTimeout: HANDSHAKE_MS }, handler) : net.createServer(handler);
  }

  async createRoom(ownerId) {
    await this.ready;
    for (const room of this.rooms.values()) {
      if (room.ownerId === ownerId) await this.removeRoom(room.id, ownerId);
    }
    const id = randomBytes(12).toString('hex');
    const secret = randomBytes(32).toString('hex');
    const room = { id, ownerId, secretHash: hash(secret), host: null, pending: new Map(), sockets: new Set() };
    room.game = this.createServer(socket => this.handleJoin(room, socket));
    room.game.on('tlsClientError', () => {});
    room.game.maxConnections = 1 + MAX_PLAYERS_PER_ROOM * 2;
    let lastError;
    for (let port = this.firstPort; port <= this.lastPort; port++) {
      try {
        room.port = await listen(room.game, port, this.bind);
        break;
      } catch (error) {
        lastError = error;
        if (error.code !== 'EADDRINUSE') throw error;
      }
    }
    if (!room.port) throw lastError || new Error('No relay ports available');
    this.rooms.set(id, room);
    return { id, secret, address: this.publicHost, port: room.port, control_port: this.controlPort, tls: Boolean(this.tlsOptions) };
  }

  connectedRoomFor(ownerId) {
    for (const room of this.rooms.values()) {
      if (room.ownerId === ownerId && room.host && !room.host.destroyed) {
        return { address: this.publicHost, port: room.port, ...(this.tlsOptions ? { tls: true } : {}) };
      }
    }
    return null;
  }

  handleControl(socket) {
    socket.setNoDelay(true);
    readHandshake(socket, (hello, rest) => {
      const room = hello && typeof hello.room === 'string' ? this.rooms.get(hello.room) : null;
      if (!room || typeof hello.secret !== 'string' || !/^[a-f0-9]{64}$/.test(hello.secret)
          || !timingSafeEqual(hash(hello.secret), room.secretHash)) return socket.destroy();
      if (hello.role === 'host') {
        if (room.host) room.host.destroy();
        room.host = socket;
        room.sockets.add(socket);
        this.onRoomState(room.ownerId);
        socket.once('close', () => {
          room.sockets.delete(socket);
          if (room.host === socket) {
            room.host = null;
            this.onRoomState(room.ownerId);
          }
        });
        socket.write('{"type":"ready"}\n');
      } else if (hello.role === 'data' && typeof hello.join === 'string') {
        const pending = room.pending.get(hello.join);
        if (!pending || room.host === null) return socket.destroy();
        room.pending.delete(hello.join);
        clearTimeout(pending.timer);
        const client = pending.socket;
        room.sockets.add(socket);
        socket.once('close', () => room.sockets.delete(socket));
        if (rest.length) client.write(rest);
        client.pipe(socket);
        socket.pipe(client);
        client.resume();
        socket.on('error', () => client.destroy());
        client.on('error', () => socket.destroy());
        client.once('close', () => socket.destroy());
        socket.once('close', () => client.destroy());
      } else socket.destroy();
    });
  }

  handleJoin(room, socket) {
    if (!room.host || room.host.destroyed) return socket.destroy();
    if (room.pending.size >= MAX_PLAYERS_PER_ROOM || room.sockets.size >= 1 + MAX_PLAYERS_PER_ROOM * 2) return socket.destroy();
    readHandshake(socket, (hello, rest) => {
      if (!this.authorizeJoin(room.ownerId, hello?.ticket)) return socket.destroy();
      socket.pause();
      if (rest.length) socket.unshift(rest);
      this.acceptJoin(room, socket);
    });
  }

  acceptJoin(room, socket) {
    socket.pause();
    const join = randomBytes(12).toString('hex');
    const timer = setTimeout(() => {
      room.pending.delete(join);
      socket.destroy();
    }, JOIN_MS);
    room.pending.set(join, { socket, timer });
    room.sockets.add(socket);
    socket.once('close', () => {
      room.sockets.delete(socket);
      clearTimeout(timer);
      room.pending.delete(join);
    });
    room.host.write(JSON.stringify({ type: 'join', id: join }) + '\n');
  }

  async removeRoom(id, ownerId) {
    const room = this.rooms.get(id);
    if (!room || room.ownerId !== ownerId) return false;
    this.rooms.delete(id);
    this.onRoomState(ownerId);
    for (const pending of room.pending.values()) clearTimeout(pending.timer);
    room.pending.clear();
    for (const socket of room.sockets) socket.destroy();
    await close(room.game);
    return true;
  }

  async shutdown() {
    await this.ready;
    await Promise.all([...this.rooms.values()].map(room => this.removeRoom(room.id, room.ownerId)));
    await close(this.control);
  }
}
