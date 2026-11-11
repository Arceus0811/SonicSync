import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import https from 'node:https';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const [pfxPath, certificatePath, mediaExecutable] = process.argv.slice(2);
for (const file of [pfxPath, certificatePath, mediaExecutable]) assert.ok(file && fs.existsSync(file), `Missing smoke-test input: ${file}`);

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const password = process.env.SONICSYNC_PFX_PASSWORD || '';
const certDer = fs.readFileSync(certificatePath);
const expectedPin = crypto.createHash('sha256').update(certDer).digest('hex');
const portServer = net.createServer();
await new Promise(resolve => portServer.listen(0, '127.0.0.1', resolve));
const port = portServer.address().port;
await new Promise(resolve => portServer.close(resolve));

const host = spawn(process.execPath, [path.join(root, 'tools/windows_remote_host.mjs'), '--listen-address', '127.0.0.1', '--port', String(port),
  '--pfx', pfxPath, '--certificate', certificatePath, '--media-session-exe', mediaExecutable], {
  env: { ...process.env, SONICSYNC_PFX_PASSWORD: password }, windowsHide: true,
  stdio: ['pipe', 'pipe', 'pipe'],
});
let output = '';
host.stdout.setEncoding('utf8').on('data', chunk => { output += chunk; });
host.stderr.setEncoding('utf8').on('data', chunk => { output += chunk; });

function startRequest(method, route, body, authorization, holdBody = false) {
  let req;
  const response = new Promise((resolve, reject) => {
    req = https.request({ hostname: '127.0.0.1', port, path: route, method, rejectUnauthorized: false,
      headers: { ...(body === undefined ? {} : { 'content-type': 'application/json', 'content-length': Buffer.byteLength(body) }),
        ...(holdBody ? { expect: '100-continue' } : {}),
        ...(authorization ? { authorization } : {}) } }, response => {
      const pin = crypto.createHash('sha256').update(response.socket.getPeerCertificate().raw).digest('hex');
      if (pin !== expectedPin) return reject(new Error('TLS peer fingerprint mismatch'));
      const data = [];
      response.on('data', chunk => data.push(chunk));
      response.on('end', () => resolve({ status: response.statusCode, body: Buffer.concat(data).toString('utf8') }));
    });
    req.setTimeout(3000, () => req.destroy(new Error('request timeout')));
    req.on('error', reject);
  });
  const sendBody = () => req.end(body);
  // The interim HTTP response confirms the host has received the headers before another request runs.
  const headersReceived = holdBody ? new Promise((resolve, reject) => {
    req.once('continue', resolve);
    req.once('error', reject);
    req.flushHeaders();
  }) : undefined;
  if (!holdBody) sendBody();
  return { response, headersReceived, sendBody, cancel: () => req.destroy(new Error('device disconnected')) };
}

function request(method, route, body, authorization) {
  return startRequest(method, route, body, authorization).response;
}

async function waitFor(text) {
  const deadline = Date.now() + 3_000;
  while (!output.includes(text) && Date.now() < deadline) {
    if (host.exitCode !== null) throw new Error(`Remote host exited early: ${output}`);
    await new Promise(resolve => setTimeout(resolve, 25));
  }
  assert.ok(output.includes(text), `Timed out waiting for host prompt: ${text}`);
}

try {
  const deadline = Date.now() + 8_000;
  while (!output.includes('One-time pairing code:') && Date.now() < deadline) {
    if (host.exitCode !== null) throw new Error(`Remote host exited early: ${output}`);
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  assert.match(output, /One-time pairing code: [0-9]{6}/, `Host did not start: ${output}`);
  const shownCode = output.match(/One-time pairing code: ([0-9]{6})/)[1];
  const wrongCode = ((Number(shownCode) + 1) % 1_000_000).toString().padStart(6, '0');
  assert.equal((await request('GET', '/v1/health')).status, 200, 'TLS health request');
  assert.equal((await request('GET', '/v1/state')).status, 401, 'state must require authentication');
  assert.equal((await request('POST', '/v1/control', '', `Bearer ${'a'.repeat(64)}`)).status, 401, 'invalid bearer token must be rejected');
  const rejectedPair = await request('POST', '/v1/pair', JSON.stringify({ version: 1, type: 'pair.request', id: 'smoke', payload: { code: wrongCode, deviceName: 'test' } }));
  assert.equal(rejectedPair.status, 401, 'a rejected pairing code must not approve or create a session');
  assert.equal((await request('POST', '/v1/pair', '{')).status, 400, 'malformed JSON is a client error');
  const pairBody = deviceName => JSON.stringify({ version: 1, type: 'pair.request', id: 'smoke_concurrent', payload: { code: shownCode, deviceName } });
  const delayedPair = startRequest('POST', '/v1/pair', pairBody('delayed device'), undefined, true);
  await delayedPair.headersReceived;
  const abandonedPair = startRequest('POST', '/v1/pair', pairBody('departing device'));
  const abandonedResponse = assert.rejects(abandonedPair.response, /device disconnected/);
  await waitFor('Approve pairing for "departing device"?');
  delayedPair.sendBody();
  assert.equal((await delayedPair.response).status, 409, 'overlapping requests cannot open competing approval prompts');
  abandonedPair.cancel();
  await abandonedResponse;
  await waitFor('Pairing request closed or timed out.');
  const pairResponsePromise = request('POST', '/v1/pair', JSON.stringify({ version: 1, type: 'pair.request', id: 'smoke_pair', payload: { code: shownCode, deviceName: 'SonicSync test emulator' } }));
  await waitFor('Approve pairing for "SonicSync test emulator"?');
  host.stdin.write('yes\n');
  const pairResponse = await pairResponsePromise;
  assert.equal(pairResponse.status, 200, 'locally approved device must pair');
  const token = JSON.parse(pairResponse.body).payload.token;
  assert.match(token, /^[0-9a-f]{64}$/, 'pairing returns a random session token');
  const reusedCode = await request('POST', '/v1/pair', JSON.stringify({ version: 1, type: 'pair.request', id: 'smoke_pair_replay', payload: { code: shownCode, deviceName: 'replay test' } }));
  assert.equal(reusedCode.status, 401, 'approved one-time pairing code cannot be replayed');
  const state = await request('GET', '/v1/state', undefined, `Bearer ${token}`);
  assert.equal(state.status, 200, 'paired device can read actual Windows media-session status');
  assert.equal(JSON.parse(state.body).type, 'media.state');
  const invalidControl = JSON.stringify({ version: 1, type: 'media.control', id: 'bad_action', payload: { action: 'shutdown' } });
  const unsupported = await request('POST', '/v1/control', invalidControl, `Bearer ${token}`);
  assert.equal(unsupported.status, 400, 'unknown controls must be rejected');
  const replayedControl = await request('POST', '/v1/control', invalidControl, `Bearer ${token}`);
  assert.equal(replayedControl.status, 409, 'a request ID cannot replay an authenticated control message');
  const delayedControl = startRequest('POST', '/v1/control', invalidControl, `Bearer ${token}`, true);
  await delayedControl.headersReceived;
  const disconnected = await request('POST', '/v1/disconnect', JSON.stringify({ version: 1, type: 'device.disconnect', id: 'smoke_disconnect', payload: {} }), `Bearer ${token}`);
  assert.equal(disconnected.status, 200, 'paired device can revoke its session');
  delayedControl.sendBody();
  assert.equal((await delayedControl.response).status, 401, 'revocation takes effect while an earlier request body is still arriving');
  assert.equal((await request('GET', '/v1/state', undefined, `Bearer ${token}`)).status, 401, 'revoked token cannot be reused');
  console.log('Remote host smoke checks passed: pinned TLS, authorization, overlapping and abandoned pairing, approved single-use pairing, media state, command replay, and in-flight revocation.');
} finally {
  host.kill('SIGTERM');
  await Promise.race([new Promise(resolve => host.once('exit', resolve)), new Promise(resolve => setTimeout(resolve, 2000))]);
  if (host.exitCode === null) host.kill();
}
