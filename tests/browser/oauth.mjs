// Optional real-browser regression. Set PLAYWRIGHT_MODULE to an existing
// Playwright installation and BROWSER_EXECUTABLE if using system Chromium.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createServer as httpServer} from 'node:http';
import {createServer as netServer} from 'node:net';
import {randomBytes, createHash} from 'node:crypto';
import {mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {setTimeout as sleep} from 'node:timers/promises';
import {createRequire} from 'node:module';

const {chromium} = createRequire(import.meta.url)(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const directory = await mkdtemp(join(tmpdir(), 'mistakebook-oauth-browser-'));
async function freePort() {
  const server = netServer();
  await new Promise((done) => server.listen(0, '127.0.0.1', done));
  const port = server.address().port;
  await new Promise((done) => server.close(done));
  return port;
}
const apiPort = await freePort(), callbackPort = await freePort();
const base = `http://127.0.0.1:${apiPort}`;
const loopback = `http://127.0.0.1:${callbackPort}/oauth/callback?existing=1`;
const hosted = 'https://oauth-client.example/callback?existing=1';
const callbacks = [];
const receiver = httpServer((req, res) => {
  callbacks.push(new URL(req.url, loopback));
  res.writeHead(200, {'Content-Type': 'text/plain; charset=utf-8'});
  res.end('OAuth callback received');
});
await new Promise((done) => receiver.listen(callbackPort, '127.0.0.1', done));
const server = spawn(resolve(root, 'backend/build/mistakebook'), [], {
  cwd: directory,
  env: {...process.env, HOST: '127.0.0.1', PORT: String(apiPort), PUBLIC_URL: base, CORS_ORIGINS: base, DATABASE_PATH: join(directory, 'test.db'), WEB_ROOT: join(directory, 'no-web')},
  stdio: ['ignore', 'pipe', 'pipe'],
});
let output = '', browser;
server.stdout.on('data', (data) => output += data);
server.stderr.on('data', (data) => output += data);
async function json(path, body) {
  const response = await fetch(base + path, {method: body ? 'POST' : 'GET', headers: body ? {'Content-Type': 'application/json'} : {}, body: body ? JSON.stringify(body) : undefined});
  const data = await response.json();
  assert.ok(response.ok, JSON.stringify(data));
  return data;
}
try {
  for (let n = 0; n < 100; n++) {
    try { if ((await fetch(base + '/health')).ok) break; } catch {}
    if (server.exitCode !== null) throw new Error(output);
    await sleep(50);
  }
  const password = 'Browser-oauth-2026';
  await json('/api/v1/auth/register', {username: 'oauth_browser', password});
  const client = await json('/register', {client_name: 'Browser regression', redirect_uris: [loopback, hosted], token_endpoint_auth_method: 'none'});
  browser = await chromium.launch({headless: true, ...(process.env.BROWSER_EXECUTABLE ? {executablePath: process.env.BROWSER_EXECUTABLE} : {})});

  for (const flow of [{callback: loopback}, {callback: hosted}, {callback: loopback, wrongPassword: true}, {callback: hosted, cancel: true}]) {
    console.log('Checking callback:', new URL(flow.callback).host, flow.wrongPassword ? 'password retry' : flow.cancel ? 'cancel' : 'approve');
    const verifier = randomBytes(48).toString('base64url');
    const challenge = createHash('sha256').update(verifier).digest('base64url');
    const state = randomBytes(16).toString('hex');
    const context = await browser.newContext();
    const page = await context.newPage();
    const violations = [];
    page.on('console', (message) => {if (message.type() === 'error' && message.text().includes('form-action')) violations.push(message.text());});
    await page.route('https://oauth-client.example/callback?**', async (route) => {
      callbacks.push(new URL(route.request().url()));
      await route.fulfill({status: 200, contentType: 'text/plain', body: 'OAuth callback received'});
    });
    await page.goto(base + '/authorize?' + new URLSearchParams({client_id: client.client_id, redirect_uri: flow.callback, response_type: 'code', code_challenge: challenge, code_challenge_method: 'S256', resource: base + '/mcp', scope: 'mcp:tools', state}));
    await page.locator('#username').fill('oauth_browser');
    if (flow.wrongPassword) {
      await page.locator('#password').fill('wrong-password');
      await page.getByRole('button', {name: '登录并授权'}).click();
      await page.getByRole('alert').filter({hasText: '用户名或密码不正确'}).waitFor();
      await page.locator('#username').fill('oauth_browser');
    }
    await page.locator('#password').fill(password);
    await page.getByRole('button', {name: flow.cancel ? '取消' : '登录并授权', exact: true}).click();
    try {
      await page.waitForURL((url) => url.origin === new URL(flow.callback).origin && url.pathname === new URL(flow.callback).pathname);
    } catch (error) {
      console.log('Navigation diagnostics:', {pathname:new URL(page.url()).pathname, violations:violations.length, callbackCount:callbacks.length, body:(await page.locator('body').innerText()).slice(0,150)});
      throw error;
    }
    const returned = callbacks.find((entry) => entry.searchParams.get('state') === state);
    assert.ok(returned, 'the real browser must reach the callback');
    assert.equal(returned.searchParams.get('existing'), '1');
    assert.equal(returned.searchParams.get('iss'), base);
    assert.deepEqual(violations, [], 'the callback redirect must not violate CSP');
    if (flow.cancel) {
      assert.equal(returned.searchParams.get('error'), 'access_denied');
      assert.equal(returned.searchParams.get('code'), null);
    } else {
      const response = await fetch(base + '/token', {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: new URLSearchParams({grant_type: 'authorization_code', client_id: client.client_id, redirect_uri: flow.callback, code: returned.searchParams.get('code'), code_verifier: verifier, resource: base + '/mcp'})});
      assert.equal(response.status, 200);
      const tokens = await response.json();
      const rpc = await fetch(base + '/mcp', {method: 'POST', headers: {Authorization: 'Bearer ' + tokens.access_token, 'Content-Type': 'application/json'}, body: JSON.stringify({jsonrpc: '2.0', id: 1, method: 'tools/list'})});
      assert.equal(rpc.status, 200);
      assert.equal((await rpc.json()).result.tools.length, 20);
    }
    await context.close();
  }
  console.log('PASS: real Chromium loopback and HTTPS callbacks, password retry, cancellation, PKCE exchange and MCP tools.');
} finally {
  await browser?.close();
  await new Promise((done) => receiver.close(done));
  if (server.exitCode === null) {const ended = new Promise((done) => server.once('exit', done)); server.kill('SIGTERM'); await ended;}
  await rm(directory, {recursive: true, force: true});
}
