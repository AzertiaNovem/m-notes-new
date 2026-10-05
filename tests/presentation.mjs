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

    const concept = '先配方识别二次函数顶点，再判断开口方向与最小值。';
    const conceptA = (await request('/api/v1/notes', {token: owner.token, method: 'POST', body: {title: '二次函数复习甲', body_md: concept, subject: '数学', ...audit}})).node;
    const conceptB = await request('/api/v1/notes', {token: owner.token, method: 'POST', body: {title: '二次函数复习乙', body_md: concept, subject: '数学', ...audit}});
    const beforeColor = conceptB.related.find((hit) => hit.entity.kind === 'note' && hit.entity.id === conceptA.id);
    assert.ok(beforeColor?.linked, 'the shared concept should have an automatic association');
    for (const color of ['red', 'green', 'blue', 'yellow', '红', '绿', '蓝', '黄']) {
      const updated = await request(`/api/v1/notes/${conceptB.node.id}`, {token: owner.token, method: 'PATCH', body: {title: `==${color}:二次函数复习乙==`, body_md: `==${color}:${concept}==`, ...audit}});
      const afterColor = updated.related.find((hit) => hit.entity.kind === 'note' && hit.entity.id === conceptA.id);
      assert.equal(afterColor?.score, beforeColor.score, `color ${color} must not alter similarity`);
      assert.equal(afterColor?.linked, true);
      const storedColor = await request(`/api/v1/notes/${conceptB.node.id}`, {token: reader.token});
      assert.equal(storedColor.body_md, `==${color}:${concept}==`, 'display and export keep the source markup');
    }

    const longBody = '先配方识别二次函数顶点，再判断开口方向与最小值。'.repeat(150);
    const longNote = (await request('/api/v1/notes', {token: owner.token, method: 'POST', body: {title: '长笔记切片', body_md: longBody, subject: '数学', ...audit}})).node;
    const searchLong = async () => {
      const result = await request('/api/v1/search', {token: reader.token, method: 'POST', body: {query: '配方 顶点 开口方向', mode: 'rag', target: 'notes', limit: 100}});
      return result.hits.filter((hit) => hit.entity.kind === 'note' && hit.entity.id === longNote.id)
        .map(({chunk_id, text, score}) => ({chunk_id, text, score})).sort((a, b) => a.chunk_id - b.chunk_id);
    };
    const beforeChunks = await searchLong();
    assert.ok(beforeChunks.length > 1, 'the regression must exercise a marker that spans multiple chunks');
    await request(`/api/v1/notes/${longNote.id}`, {token: owner.token, method: 'PATCH', body: {body_md: `==blue:${longBody}==`, ...audit}});
    assert.deepEqual(await searchLong(), beforeChunks, 'valid color markup must be removed before RAG chunking');

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
