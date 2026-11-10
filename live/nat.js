import dgram from 'node:dgram';
import { networkInterfaces } from 'node:os';
import { isIP } from 'node:net';

const SEARCH = Buffer.from('M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\nMX: 2\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n');
const SERVICE = /urn:schemas-upnp-org:service:(?:WANIPConnection|WANPPPConnection):[12]/;

export function isPublicIPv4(address) {
  if (isIP(address) !== 4) return false;
  const [a, b] = address.split('.').map(Number);
  return !(a === 0 || a === 10 || a === 127 || a >= 224 || (a === 100 && b >= 64 && b <= 127)
    || (a === 169 && b === 254) || (a === 172 && b >= 16 && b <= 31)
    || (a === 192 && b === 168) || (a === 198 && (b === 18 || b === 19)));
}

function isLocalGateway(address) {
  if (isIP(address) !== 4) return false;
  const [a, b] = address.split('.').map(Number);
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168);
}

export async function discoverUpnp(timeoutMs = 2500) {
  const addresses = Object.values(networkInterfaces()).flat()
    .filter(item => item && item.family === 'IPv4' && !item.internal && isLocalGateway(item.address))
    .map(item => item.address);
  if (!addresses.length) throw new Error('No private IPv4 network interface found');
  const sockets = [];
  return new Promise((resolve, reject) => {
    let settled = false;
    const finish = (error, value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      for (const socket of sockets) socket.close();
      if (error) reject(error); else resolve(value);
    };
    const timer = setTimeout(() => finish(new Error('No UPnP gateway found')), timeoutMs);
    for (const localAddress of addresses) {
      const socket = dgram.createSocket('udp4');
      sockets.push(socket);
      socket.on('error', () => {});
      socket.on('message', packet => {
        const location = /^location:\s*(\S+)/im.exec(packet.toString('utf8'))?.[1];
        if (!location) return;
        try {
          const url = new URL(location);
          if (url.protocol === 'http:' && isLocalGateway(url.hostname)) finish(null, { location, localAddress });
        } catch { /* ignore malformed discovery response */ }
      });
      socket.bind(0, localAddress, () => {
        try { socket.setMulticastInterface(localAddress); } catch { /* interface may reject multicast setting */ }
        socket.send(SEARCH, 1900, '239.255.255.250');
      });
    }
  });
}

function xmlValue(xml, tag) {
  const match = new RegExp(`<${tag}>([\\s\\S]*?)<\\/${tag}>`, 'i').exec(xml);
  return match?.[1]?.trim() || null;
}

export function parseDescription(xml, location) {
  if (typeof xml !== 'string' || xml.length > 128 * 1024) throw new Error('Invalid gateway description');
  const services = xml.match(/<service>[\s\S]*?<\/service>/gi) || [];
  const service = services.map(block => ({
    type: xmlValue(block, 'serviceType'),
    control: xmlValue(block, 'controlURL')
  })).find(item => item.type && SERVICE.test(item.type) && item.control);
  if (!service) throw new Error('Gateway has no WAN IP/PPP connection service');
  const controlUrl = new URL(service.control.replaceAll('&amp;', '&'), location);
  const locationUrl = new URL(location);
  if (controlUrl.protocol !== 'http:' || controlUrl.hostname !== locationUrl.hostname) throw new Error('Unsafe UPnP control URL');
  return { controlUrl: controlUrl.href, serviceType: service.type };
}

async function limitedText(response) {
  const text = await response.text();
  if (text.length > 128 * 1024) throw new Error('Gateway response too large');
  return text;
}

export async function getGatewayService(discovery) {
  const url = new URL(discovery.location);
  if (url.protocol !== 'http:' || !isLocalGateway(url.hostname)) throw new Error('Unsafe UPnP location');
  const response = await fetch(url, { signal: AbortSignal.timeout(3000), redirect: 'error' });
  if (!response.ok) throw new Error(`Gateway description HTTP ${response.status}`);
  return parseDescription(await limitedText(response), discovery.location);
}

async function soap(service, action, parameters, request = fetch) {
  const args = Object.entries(parameters).map(([key, value]) => `<${key}>${value}</${key}>`).join('');
  const body = `<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:${action} xmlns:u="${service.serviceType}">${args}</u:${action}></s:Body></s:Envelope>`;
  const response = await request(service.controlUrl, {
    method: 'POST', redirect: 'error', signal: AbortSignal.timeout(3000),
    headers: { 'content-type': 'text/xml; charset="utf-8"', SOAPAction: `"${service.serviceType}#${action}"` }, body
  });
  const text = await limitedText(response);
  if (!response.ok) throw new Error(`UPnP ${action} failed: ${xmlValue(text, 'errorCode') || response.status}`);
  return text;
}

export async function mapTcpPort(service, localAddress, port, request = fetch) {
  if (!isLocalGateway(localAddress)) throw new Error('Local address must be private IPv4');
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid game port');
  const args = {
    NewRemoteHost: '', NewExternalPort: port, NewProtocol: 'TCP', NewInternalPort: port,
    NewInternalClient: localAddress, NewEnabled: 1, NewPortMappingDescription: 'LCEReborn Live', NewLeaseDuration: 3600
  };
  let leaseSeconds = 3600;
  async function add() {
    try { await soap(service, 'AddPortMapping', args, request); }
    catch (error) {
      if (!String(error.message).endsWith(': 725')) throw error;
      args.NewLeaseDuration = 0;
      leaseSeconds = 0;
      await soap(service, 'AddPortMapping', args, request);
    }
  }
  await add();
  const remove = () => soap(service, 'DeletePortMapping', {
    NewRemoteHost: '', NewExternalPort: port, NewProtocol: 'TCP'
  }, request);
  let address;
  try {
    const response = await soap(service, 'GetExternalIPAddress', {}, request);
    address = xmlValue(response, 'NewExternalIPAddress');
  } catch (error) {
    await remove().catch(() => {});
    throw error;
  }
  if (!isPublicIPv4(address)) {
    await remove().catch(() => {});
    throw new Error('Gateway has no public IPv4 address (possible carrier NAT)');
  }
  return { address, port, leaseSeconds, renew: add, remove };
}

export async function openTcpPort(port) {
  const discovery = await discoverUpnp();
  const service = await getGatewayService(discovery);
  return mapTcpPort(service, discovery.localAddress, port);
}
