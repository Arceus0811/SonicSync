import crypto from 'node:crypto';
import fs from 'node:fs';
import https from 'node:https';
import readline from 'node:readline/promises';
import { spawn } from 'node:child_process';

const args = new Map();
for (let i = 2; i < process.argv.length; i += 2) args.set(process.argv[i], process.argv[i + 1]);
const address = args.get('--listen-address');
const port = Number(args.get('--port') || 47931);
const pfxPath = args.get('--pfx');
const certificatePath = args.get('--certificate');
const executable = args.get('--media-session-exe');
if (!address || !pfxPath || !certificatePath || !executable || !fs.existsSync(pfxPath) || !fs.existsSync(certificatePath) || !fs.existsSync(executable) || !Number.isInteger(port) || port < 1024 || port > 65535) {
  console.error('Usage: node windows_remote_host.mjs --listen-address <explicit-IP> --port <1024-65535> --pfx <file> --certificate <DER-file> --media-session-exe <exe>');
  process.exit(2);
}

const pfxPassword = process.env.SONICSYNC_PFX_PASSWORD || '';
const pfx = fs.readFileSync(pfxPath);
const certificate = fs.readFileSync(certificatePath);
const certificateFingerprint = crypto.createHash('sha256').update(certificate).digest('hex');
const CODE_TTL_MS = 5 * 60_000;
const APPROVAL_TIMEOUT_MS = 60_000;
let code;
let pairingExpires;
function renewCode() {
  const previousCode = code;
  do { code = crypto.randomInt(0, 1_000_000).toString().padStart(6, '0'); } while (code === previousCode);
  pairingExpires = Date.now() + CODE_TTL_MS;
}
renewCode();
const readlineInterface = readline.createInterface({ input: process.stdin, output: process.stdout });
const tokens = new Map();
const SESSION_TTL_MS = 12 * 60 * 60_000;
let pairingFailures = 0;
let pairingLockedUntil = 0;
let pairingInProgress = false;
let stopping = false;

function send(response, status, body) {
  const bytes = Buffer.from(JSON.stringify(body));
  response.writeHead(status, { 'content-type': 'application/json; charset=utf-8', 'content-length': bytes.length, 'cache-control': 'no-store' });
  response.end(bytes);
}

async function bodyJson(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > 16_384) throw new Error('request_too_large');
    chunks.push(chunk);
  }
  let value;
  try { value = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch { throw new Error('invalid_message'); }
  if (!value || value.version !== 1 || typeof value.id !== 'string' || !/^[A-Za-z0-9_-]{1,64}$/.test(value.id) || !value.payload || typeof value.payload !== 'object' || Array.isArray(value.payload)) throw new Error('invalid_message');
  return value;
}

function mediaSession(action) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, [action], { windowsHide: true, stdio: ['ignore', 'pipe', 'ignore'] });
    let output = '';
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', data => { output += data; if (output.length > 32_768) child.kill(); });
    const timeout = setTimeout(() => child.kill(), 5_000);
    child.once('error', error => { clearTimeout(timeout); reject(error); });
    child.once('close', code => {
      clearTimeout(timeout);
      if (code !== 0) reject(new Error('media_session_unavailable'));
      else { try { resolve(JSON.parse(output)); } catch { reject(new Error('invalid_media_session_reply')); } }
    });
  });
}

function liveSession(token) {
  const session = tokens.get(token);
  if (session && session.expiresAt > Date.now()) return session;
  tokens.delete(token);
}

function acceptRequestId(session, id) {
  const ids = session.requestIds;
  if (ids.has(id)) return false;
  ids.add(id);
  if (ids.size > 2_048) ids.delete(ids.values().next().value);
  return true;
}

async function approvePairing(deviceName, response) {
  if (readlineInterface.closed || response.destroyed) return false;
  const controller = new AbortController();
  const cancel = () => controller.abort();
  const timeout = setTimeout(cancel, Math.min(APPROVAL_TIMEOUT_MS, pairingExpires - Date.now()));
  response.once('close', cancel);
  readlineInterface.once('close', cancel);
  try {
    const answer = await readlineInterface.question(`Approve pairing for "${deviceName}"? Type yes: `, { signal: controller.signal });
    return !response.destroyed && Date.now() < pairingExpires && answer.trim().toLowerCase() === 'yes';
  } catch (error) {
    if (!controller.signal.aborted) throw error;
    console.log('Pairing request closed or timed out.');
    return false;
  } finally {
    clearTimeout(timeout);
    response.off('close', cancel);
    readlineInterface.off('close', cancel);
  }
}

const server = https.createServer({ pfx, passphrase: pfxPassword, minVersion: 'TLSv1.2' }, async (request, response) => {
  try {
    if (request.url === '/v1/health' && request.method === 'GET') return send(response, 200, { version: 1, service: 'SonicSync Windows remote', pairing: true });
    if (request.url === '/v1/pair' && request.method === 'POST') {
      const message = await bodyJson(request);
      if (message.type !== 'pair.request' || typeof message.payload.code !== 'string' || !/^[0-9]{6}$/.test(message.payload.code)) return send(response, 400, { error: 'invalid_message' });
      if (pairingInProgress) return send(response, 409, { error: 'pairing_in_progress' });
      if (Date.now() < pairingLockedUntil) return send(response, 429, { error: 'pairing_rate_limited' });
      if (Date.now() >= pairingExpires) {
        renewCode();
        console.log(`Pairing code expired. New one-time pairing code: ${code}`);
        return send(response, 410, { error: 'pairing_expired' });
      }
      if (!crypto.timingSafeEqual(Buffer.from(message.payload.code), Buffer.from(code))) {
        pairingFailures++;
        if (pairingFailures >= 5) { pairingLockedUntil = Date.now() + 60_000; pairingFailures = 0; }
        return send(response, 401, { error: 'pairing_rejected' });
      }
      const deviceName = String(message.payload.deviceName || 'Android device').slice(0, 80).replace(/[\x00-\x1f\x7f-\x9f\u202a-\u202e\u2066-\u2069]/g, '');
      pairingInProgress = true;
      try {
        const approved = await approvePairing(deviceName, response);
        if (response.destroyed) return;
        if (!approved) return send(response, 403, { error: 'pairing_not_approved' });
        const token = crypto.randomBytes(32).toString('hex');
        tokens.set(token, { requestIds: new Set(), expiresAt: Date.now() + SESSION_TTL_MS });
        pairingFailures = 0;
        renewCode();
        console.log(`Paired ${deviceName}. One-time code for another device: ${code}`);
        return send(response, 200, { version: 1, type: 'pair.approved', id: message.id, payload: { token, hostName: process.env.COMPUTERNAME || 'Windows PC', capabilities: ['media.state', 'media.play', 'media.pause', 'media.next', 'media.previous'] } });
      } finally {
        pairingInProgress = false;
      }
    }
    if (request.url?.startsWith('/v1/') && request.url !== '/v1/health') {
      const token = /^Bearer ([0-9a-f]{64})$/.exec(request.headers.authorization || '')?.[1];
      if (!liveSession(token)) return send(response, 401, { error: 'unauthorized' });
      if (request.url === '/v1/state' && request.method === 'GET') return send(response, 200, {
        version: 1, type: 'media.state', id: crypto.randomBytes(8).toString('hex'), payload: await mediaSession('--state'),
      });
      if ((request.url === '/v1/control' || request.url === '/v1/disconnect') && request.method === 'POST') {
        const message = await bodyJson(request);
        const session = liveSession(token);
        if (!session) return send(response, 401, { error: 'unauthorized' });
        if (request.url === '/v1/disconnect') {
          if (message.type !== 'device.disconnect') return send(response, 400, { error: 'invalid_message' });
          if (!acceptRequestId(session, message.id)) return send(response, 409, { error: 'replayed_request' });
          tokens.delete(token);
          return send(response, 200, { version: 1, type: 'device.disconnected', id: message.id, payload: { ok: true } });
        }
        if (!acceptRequestId(session, message.id)) return send(response, 409, { error: 'replayed_request' });
        const action = message.payload.action;
        if (message.type !== 'media.control' || !['play', 'pause', 'toggle', 'next', 'previous'].includes(action)) return send(response, 400, { error: 'unsupported_request' });
        const operation = `--${action}`;
        return send(response, 200, { version: 1, type: 'media.control.result', id: message.id, payload: await mediaSession(operation) });
      }
    }
    return send(response, 404, { error: 'not_found' });
  } catch (error) {
    const status = error.message === 'request_too_large' ? 413 : error.message === 'invalid_message' ? 400 : 503;
    if (!response.destroyed) send(response, status, { error: status === 503 ? 'media_session_unavailable' : error.message });
  }
});

server.on('tlsClientError', () => {});
server.listen(port, address, () => {
  console.log(`SonicSync remote endpoint enabled at https://${address}:${port}`);
  console.log(`TLS certificate SHA-256 fingerprint: ${certificateFingerprint}`);
  console.log(`One-time pairing code: ${code}`);
  console.log('No firewall rule was created. Press Ctrl+C to disable and revoke all paired-device tokens.');
});
function stop() {
  if (stopping) return;
  stopping = true;
  tokens.clear();
  readlineInterface.close();
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 2_000).unref();
}
process.on('SIGINT', stop);
process.on('SIGTERM', stop);
readlineInterface.once('close', stop);
