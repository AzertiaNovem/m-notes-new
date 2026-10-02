import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {createServer} from 'node:net';
import {request as httpRequest} from 'node:http';
import {setTimeout as sleep} from 'node:timers/promises';

async function start(t, trusted = '') {
  const directory = await mkdtemp(join(tmpdir(), 'mistakebook-proxy-test-'));
  const socket = createServer();
  await new Promise(resolve => socket.listen(0, '127.0.0.1', resolve));
  const port = socket.address().port;
  await new Promise(resolve => socket.close(resolve));
  const base = `http://127.0.0.1:${port}`;
  let output = '';
  const server = spawn(resolve(process.env.BINARY_PATH ?? 'backend/build/mistakebook'), [], {
    cwd: directory,
    env: {...process.env, DATABASE_PATH: join(directory, 'proxy.db'), HOST: '127.0.0.1',
      PORT: String(port), PUBLIC_URL: base, CORS_ORIGINS: base,
      WEB_ROOT: join(directory, 'no-web'), TRUSTED_PROXY_IPS: trusted},
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  server.stdout.on('data', chunk => output += chunk);
  server.stderr.on('data', chunk => output += chunk);
  let launchError;
  server.on('error', error => launchError = error);
  t.after(async () => {
    if (server.pid && server.exitCode === null && server.signalCode === null) {
      const exited = new Promise(resolve => server.once('exit', resolve));
      server.kill('SIGTERM');
      await exited;
    }
    await rm(directory, {recursive: true, force: true});
  });
  for (let i = 0; i < 100; i++) {
    if (launchError) throw launchError;
    if (server.exitCode !== null) throw new Error(output);
    try {
      const response = await fetch(base + '/health');
      await response.text();
      if (response.ok) return base;
    } catch {}
    await sleep(50);
  }
  throw new Error(`Test server failed to start: ${output}`);
}

async function request(base, ip, path = '/api/v1/auth/register', extra = {}) {
  const response = await fetch(base + path, {
    method: path === '/authorize' ? 'GET' : 'POST',
    headers: {'Content-Type': 'application/json',
      ...(ip === undefined ? {} : {'X-Real-IP': ip}), ...extra},
    ...(path === '/authorize' ? {} : {body: '{}'}),
  });
  await response.text();
  return response.status;
}

test('forwarded IP headers are ignored unless the immediate peer is explicitly trusted', async t => {
  for (const trusted of ['', '127.0.0.2']) {
    const base = await start(t, trusted);
    for (let i = 0; i < 10; i++) {
      assert.equal(await request(base, `192.0.2.${i + 1}`, undefined,
        {'X-Forwarded-For': `198.51.100.${i + 1}`}), 422);
    }
    assert.equal(await request(base, '192.0.2.50'), 429);
  }
});

test('trusted proxy rate limits are isolated by validated IPv4 and normalized IPv6', async t => {
  const base = await start(t, '127.0.0.1,::1');
  for (let i = 0; i < 10; i++) assert.equal(await request(base, '192.0.2.10'), 422);
  assert.equal(await request(base, '192.0.2.10'), 429);
  assert.equal(await request(base, '192.0.2.11'), 422);
  for (let i = 0; i < 10; i++) {
    assert.equal(await request(base, '2001:0db8:0000:0000:0000:0000:0000:0001'), 422);
  }
  assert.equal(await request(base, '2001:db8::1'), 429);
  assert.equal(await request(base, '2001:db8::2'), 422);
});

test('missing, malformed, and chained proxy IPs fall back to the immediate peer', async t => {
  const base = await start(t, '127.0.0.1,::1');
  const invalid = [undefined, '', 'localhost', '192.0.2.1:1234', '192.0.2.1,192.0.2.2',
    '2001:db8::1%eth0', '[2001:db8::1]', '999.1.1.1', '192.0.2.-1', 'not-an-ip'];
  for (const ip of invalid) {
    assert.equal(await request(base, ip, undefined, {'X-Forwarded-For': '198.51.100.1'}), 422);
  }
  assert.equal(await request(base, '192.0.2.1, 192.0.2.2'), 429);
  assert.equal(await request(base, undefined), 429);
  const duplicateStatus = await new Promise((resolve, reject) => {
    const req = httpRequest(base + '/api/v1/auth/register', {
      method: 'POST', headers: {'Content-Type': 'application/json',
        'X-Real-IP': ['192.0.2.123', '192.0.2.124']},
    }, response => {
      response.resume();
      response.on('end', () => resolve(response.statusCode));
    });
    req.on('error', reject);
    req.end('{}');
  });
  assert.equal(duplicateStatus, 429);
  assert.equal(await request(base, '192.0.2.99'), 422);
});

test('login and OAuth authorization rate limits use the trusted client IP too', async t => {
  const base = await start(t, '127.0.0.1,::1');
  for (const path of ['/api/v1/auth/login', '/authorize']) {
    const expected = path === '/authorize' ? 400 : 422;
    for (let i = 0; i < 30; i++) assert.equal(await request(base, '192.0.2.10', path), expected);
    assert.equal(await request(base, '192.0.2.10', path), 429);
    assert.equal(await request(base, '192.0.2.11', path), expected);
  }
});
