import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {setTimeout as sleep} from 'node:timers/promises';

test('formula titles and colored Markdown survive MCP, note versions, export and print with tenant permissions', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'mistakebook-presentation-'));
  const port = 21000 + Math.floor(Math.random() * 1000);
  const base = `http://127.0.0.1:${port}`;
  const server = spawn(resolve('backend/build/mistakebook'), [], {
    cwd: directory,
    env: {...process.env, DATABASE_PATH: join(directory, 'test.db'), HOST: '127.0.0.1', PORT: String(port), PUBLIC_URL: base, WEB_ROOT: join(directory, 'no-web')},
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let output = '';
  server.stdout.on('data', (data) => output += data);
  server.stderr.on('data', (data) => output += data);
  async function request(path, {token, method = 'GET', body, status = 200} = {}) {
    const response = await fetch(base + path, {method, headers: {...(token ? {Authorization: `Bearer ${token}`} : {}), ...(body ? {'Content-Type': 'application/json'} : {})}, body: body ? JSON.stringify(body) : undefined});
    const data = await response.json();
    assert.equal(response.status, status, JSON.stringify(data));
    return data;
  }
  const audit = {editor_tool: 'presentation-test', change_summary: '验证上游标题与染色语法'};
  const password = 'Presentation-test-2026';
  const title = '数列 a_{n+1} 与 $\\frac{a_{n+1}}{a_n}$';
  const stem = '已知 ==blue:$a_1=1$==，请说明 ==重点==。';
  const approach = '注意 ==red:下标==，再应用递推关系。';
  const answer = '结论为 ==绿:$a_{n+1}>a_n$==。';
  try {
    for (let attempt = 0; attempt < 100; attempt++) {
      try { if ((await fetch(base + '/health')).ok) break; } catch {}
      if (attempt === 99) throw new Error(`Server failed: ${output}`);
      await sleep(50);
    }
    const signup = (username) => request('/api/v1/auth/register', {method: 'POST', body: {username, password}});
    const owner = await signup('presentation_owner');
    const other = await signup('presentation_other');
    const created = await request('/mcp', {token: owner.token, method: 'POST', body: {
      jsonrpc: '2.0', id: 1, method: 'tools/call', params: {name: 'upsert_problem', arguments: {title, stem_md: stem, subject: '数学', tags: ['数列'], approach_md: approach, answer_md: answer, ...audit}},
    }});
    assert.equal(created.result.isError, false);
    const problem = JSON.parse(created.result.content[0].text);
    assert.equal(problem.title, title);
    assert.equal(problem.stem_md, stem);
    assert.equal(problem.solution.approach_md, approach);
    assert.equal(problem.solution.answer_md, answer);

    const first = await request('/api/v1/notes', {token: owner.token, method: 'POST', body: {title, body_md: stem, subject: '数学', ...audit}});
    const note = first.node;
    await request(`/api/v1/notes/${note.id}`, {token: owner.token, method: 'PATCH', body: {body_md: answer, ...audit}});
    const versions = await request(`/api/v1/notes/${note.id}/versions`, {token: owner.token});
    const original = await request(`/api/v1/notes/${note.id}/versions/${versions.items.at(-1).id}`, {token: owner.token});
    assert.equal(original.title, title);
    assert.equal(original.body_md, stem);

    await request('/api/v1/subaccounts', {token: owner.token, method: 'POST', body: {username: 'presentation_reader', password}});
    const reader = await request('/api/v1/auth/login', {method: 'POST', body: {username: 'presentation_reader', password}});
    const visible = await request(`/api/v1/problems/${problem.id}`, {token: reader.token});
    assert.equal(visible.title, title);
    await request(`/api/v1/problems/${problem.id}`, {token: reader.token, method: 'PATCH', body: {stem_md: '==red:越权==', ...audit}, status: 403});
    await request(`/api/v1/problems/${problem.id}`, {token: other.token, status: 404});
    await request(`/api/v1/notes/${note.id}/versions/${original.id}`, {token: other.token, status: 404});

    const book = await request('/api/v1/print/books', {token: owner.token, method: 'POST', body: {subjects: ['数学'], tag_ids: []}});
    await request(`/api/v1/print/books/${book.id}/apply`, {token: owner.token, method: 'POST'});
    const document = await request(`/api/v1/print/books/${book.id}/document`, {token: reader.token});
    const printed = document.pages.flatMap((page) => page.problems).find((item) => item.id === problem.id);
    assert.equal(printed.title, title);
    assert.equal(printed.stem_md, stem);
    assert.equal(printed.solution.answer_md, answer);
    const exported = await request('/api/v1/export', {token: reader.token});
    assert.equal(exported.data.problems[0].title, title);
    assert.equal(exported.data.problems[0].stem_md, stem);
    assert.ok(exported.data.note_versions.some((version) => version.body_md === stem));

    const prompt = await request('/mcp', {token: owner.token, method: 'POST', body: {jsonrpc: '2.0', id: 2, method: 'prompts/get', params: {name: 'upload_mistake'}}});
    assert.match(prompt.result.messages[0].content.text, /==red:易错==/);
    assert.match(prompt.result.messages[0].content.text, /a_\{n\+1\}/);
  } finally {
    if (server.exitCode === null) {
      server.kill('SIGTERM');
      await new Promise((done) => server.once('exit', done));
    }
    await rm(directory, {recursive: true, force: true});
  }
});
