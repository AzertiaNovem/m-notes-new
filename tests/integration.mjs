import test, {before, after} from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp, rm, readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {setTimeout as sleep} from 'node:timers/promises';
let server, directory, output = '', base;
const unique = `u${Date.now().toString(36)}`;
const password = 'Testing-native-2026';
let alice, bob, child, problem, note;
async function request(path, {token, method='GET', body, status=200}={}) {
  const response = await fetch(`${base}${path}`, {method, headers: {...(token ? {Authorization:`Bearer ${token}`} : {}), ...(body ? {'Content-Type':'application/json'} : {})}, body: body ? JSON.stringify(body) : undefined});
  const data = await response.json();
  assert.equal(response.status, status, `${method} ${path}: ${JSON.stringify(data)}`);
  return data;
}
async function signup(name) {return request('/api/v1/auth/register', {method:'POST',body:{username:name,password}});}
const audit={editor_tool:'test',change_summary:'验证完整流程'};
const payload={title:'抛物线顶点',stem_md:'已知 $f(x)=x^2-4x+3$，求顶点。',subject:'数学',tags:['二次函数'],approach_md:'通过配方识别顶点形式，注意常数项符号。',answer_md:'$f(x)=(x-2)^2-1$，顶点为 $(2,-1)$。',diagrams:[{svg:'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20"><path d="M0 0L20 20"/></svg>',caption:'示意'}],...audit};
before(async()=>{
 directory=await mkdtemp(join(tmpdir(),'mistakebook-native-test-'));
 const port=19000+Math.floor(Math.random()*1000);base=`http://127.0.0.1:${port}`;
 server=spawn(resolve('backend/build/mistakebook'),[],{env:{...process.env,DATABASE_PATH:join(directory,'test.db'),PORT:String(port),HOST:'127.0.0.1',WEB_ROOT:join(directory,'no-web')},stdio:['ignore','pipe','pipe']});
 server.stdout.on('data',chunk=>output+=chunk);server.stderr.on('data',chunk=>output+=chunk);
 server.on('error',error=>output+=error);
 for(let i=0;i<100;i++){try{const r=await fetch(`${base}/health`);if(r.ok)return;}catch{}await sleep(100);}
 throw new Error(`Server failed: ${output}`);
});
after(async()=>{if(server){server.kill('SIGTERM');await new Promise(resolve=>server.once('exit',resolve));}if(directory)await rm(directory,{recursive:true,force:true});});
test('registration creates independent owner accounts and rejects role escalation', async()=>{
 alice=await signup(`${unique}alice`);bob=await signup(`${unique}bob`);
 assert.equal(alice.user.role,'user');assert.equal(alice.user.owner_id,null);
 await request('/api/v1/auth/register',{method:'POST',body:{username:`${unique}evil`,password,role:'readonly',owner_id:alice.user.id},status:422});
 await request('/api/v1/auth/register',{method:'POST',body:{username:`${unique}ALICE`,password},status:409});
 await request('/api/v1/auth/login',{method:'POST',body:{username:alice.user.username,password:'wrong-password'},status:401});
 await request('/api/v1/problems',{status:401});
});
test('problem CRUD preserves math, diagrams, audit, and enforces tenant isolation',async()=>{
 problem=await request('/api/v1/problems',{method:'POST',token:alice.token,body:payload});
 assert.equal(problem.solution.answer_md,payload.answer_md);assert.equal(problem.diagrams.length,1);
 const mine=await request('/api/v1/problems',{token:alice.token});assert.equal(mine.total,1);
 const other=await request('/api/v1/problems',{token:bob.token});assert.equal(other.total,0);
 await request(`/api/v1/problems/${problem.id}`,{token:bob.token,status:404});
 await request(`/api/v1/problems/${problem.id}`,{token:bob.token,method:'PATCH',body:{title:'越权',...audit},status:404});
 await request(`/api/v1/problems/${problem.id}`,{token:bob.token,method:'DELETE',status:404});
 const log=await request(`/api/v1/problems/${problem.id}/logs`,{token:alice.token});assert.ok(log.items.length);
 const search=await request('/api/v1/search',{method:'POST',token:alice.token,body:{query:'抛物线',mode:'keyword'}});assert.ok(search.hits.length);
 const hidden=await request('/api/v1/search',{method:'POST',token:bob.token,body:{query:'抛物线',mode:'hybrid'}});assert.equal(hidden.hits.length,0);
});
test('notes, versions, graph, and foreign references remain isolated',async()=>{
 const created=await request('/api/v1/notes',{token:alice.token,method:'POST',body:{title:'函数笔记',body_md:'初始内容 $x^2$',subject:'数学',...audit}});note=created.node??created;
 await request(`/api/v1/notes/${note.id}`,{token:bob.token,status:404});
 await request('/api/v1/notes',{token:bob.token,method:'POST',body:{title:'越权树',parent_id:note.id,...audit},status:404});
 await request(`/api/v1/notes/${note.id}`,{token:alice.token,method:'PATCH',body:{body_md:'修改后的内容 $x^2+1$',...audit}});
 const versions=await request(`/api/v1/notes/${note.id}/versions`,{token:alice.token});assert.equal(versions.items.length,2);
 const diff=await request(`/api/v1/notes/${note.id}/versions/${versions.items[0].id}/diff?against=previous`,{token:alice.token});assert.ok(diff.hunks.some(h=>h.kind!=='equal'));
 await request('/api/v1/notes/links',{token:alice.token,method:'POST',body:{from:{kind:'note',id:note.id},to:{kind:'problem',id:problem.id},label:'知识点'}});
 const graph=await request('/api/v1/notes/graph',{token:alice.token});assert.ok(graph.nodes.length>=2);assert.ok(graph.edges.length);
 const empty=await request('/api/v1/notes/graph',{token:bob.token});assert.equal(empty.nodes.length,0);
 await request(`/api/v1/notes/${note.id}/move`,{token:alice.token,method:'POST',body:{parent_id:note.id,...audit},status:400});
});
test('five subaccount limit survives concurrent requests',async()=>{
 const attempts=await Promise.all(Array.from({length:8},(_,i)=>fetch(`${base}/api/v1/subaccounts`,{method:'POST',headers:{Authorization:`Bearer ${alice.token}`,'Content-Type':'application/json'},body:JSON.stringify({username:`${unique}child${i}`,password})}).then(async r=>({status:r.status,body:await r.json()}))));
 assert.equal(attempts.filter(r=>r.status===200).length,5,JSON.stringify(attempts));
 assert.equal(attempts.filter(r=>r.status===409).length,3);
 const list=await request('/api/v1/subaccounts',{token:alice.token});assert.equal(list.items.length,5);assert.equal(list.limit,5);
 child=await request('/api/v1/auth/login',{method:'POST',body:{username:list.items[0].username,password}});
 assert.equal(child.user.role,'readonly');assert.equal(child.user.owner_id,alice.user.id);
 await request(`/api/v1/subaccounts/${child.user.id}`,{token:bob.token,method:'DELETE',status:404});
});
test('readonly is enforced for every write surface including MCP',async()=>{
 const visible=await request(`/api/v1/problems/${problem.id}`,{token:child.token});assert.equal(visible.id,problem.id);
 const attempts=[['POST','/api/v1/problems',payload],['PATCH',`/api/v1/problems/${problem.id}`,{title:'bad',...audit}],['DELETE',`/api/v1/problems/${problem.id}`],['POST','/api/v1/notes',{title:'bad',...audit}],['DELETE',`/api/v1/notes/${note.id}`],['POST','/api/v1/tags/merge',{source_tag_id:1,target_tag_id:2}],['POST','/api/v1/subjects',{name:'bad'}],['POST','/api/v1/print/books',{title:'bad'}],['POST','/api/v1/subaccounts',{username:'bad',password}]];
 for(const [method,path,body] of attempts)await request(path,{method,body,token:child.token,status:403});
 const tools=await request('/mcp',{token:child.token,method:'POST',body:{jsonrpc:'2.0',id:1,method:'tools/list'}});assert.ok(!tools.result.tools.some(t=>t.name==='upsert_problem'));
 await request('/mcp',{token:child.token,method:'POST',body:{jsonrpc:'2.0',id:2,method:'tools/call',params:{name:'upsert_problem',arguments:payload}},status:403});
 const search=await request('/api/v1/search',{token:child.token,method:'POST',body:{query:'抛物线',mode:'keyword'}});assert.ok(search.hits.length);
 await request('/api/v1/subaccounts',{token:child.token,status:403});
});
test('JSON export is a complete scoped snapshot without credentials',async()=>{
 const exported=await request('/api/v1/export',{token:alice.token});assert.equal(exported.format,'mistakebook');assert.equal(exported.data.problems.length,1);assert.equal(exported.data.notes.length,1);assert.equal(exported.data.note_versions.length,2);assert.ok(exported.data.tags.length);assert.equal(exported.subaccounts.length,5);
 const data=JSON.stringify(exported);for(const secret of ['password_hash','token_hash',alice.token,password,bob.user.username])assert.ok(!data.includes(secret),secret);
 const childExport=await request('/api/v1/export',{token:child.token});assert.deepEqual(childExport.data,exported.data);
 const empty=await request('/api/v1/export',{token:bob.token});assert.equal(empty.data.problems.length,0);
});
test('MCP ingestion shares the REST data layer',async()=>{
 const result=await request('/mcp',{token:alice.token,method:'POST',body:{jsonrpc:'2.0',id:3,method:'tools/call',params:{name:'upsert_problem',arguments:{...payload,title:'MCP 新题'}}}});
 assert.equal(result.result.isError,false);const created=JSON.parse(result.result.content[0].text);assert.equal(created.title,'MCP 新题');
 const listed=await request('/api/v1/problems',{token:alice.token});assert.equal(listed.total,2);
 await request(`/api/v1/problems/${created.id}`,{token:alice.token,method:'DELETE'});
});
test('printing persists scoped snapshots and serves real browser document data',async()=>{
 const book=await request('/api/v1/print/books',{token:alice.token,method:'POST',body:{title:'数学打印',subjects:['数学'],tag_ids:[]}});
 await request(`/api/v1/print/books/${book.id}`,{token:bob.token,status:404});
 const plan=await request(`/api/v1/print/books/${book.id}/plan`,{token:alice.token});assert.ok(plan.first_generation);
 await request(`/api/v1/print/books/${book.id}/apply`,{token:child.token,method:'POST',status:403});
 const applied=await request(`/api/v1/print/books/${book.id}/apply`,{token:alice.token,method:'POST'});assert.ok(applied);
 const document=await request(`/api/v1/print/books/${book.id}/document`,{token:child.token});assert.ok(JSON.stringify(document).includes('抛物线'));
 const clean=await request(`/api/v1/print/books/${book.id}/plan`,{token:alice.token});assert.equal(clean.cost,0);
 await request(`/api/v1/problems/${problem.id}`,{token:alice.token,method:'PATCH',body:{title:'修改后的抛物线',...audit}});
 const dirty=await request(`/api/v1/print/books/${book.id}/plan`,{token:alice.token});assert.ok(dirty.cost>0);
});
test('changing password and deleting subaccounts invalidate old sessions',async()=>{
 const changed=await request('/api/v1/auth/password',{token:child.token,method:'POST',body:{current_password:password,new_password:'New-native-password'}});
 await request('/api/v1/auth/me',{token:child.token,status:401});
 await request('/api/v1/auth/me',{token:changed.token});
 await request(`/api/v1/subaccounts/${child.user.id}`,{token:alice.token,method:'DELETE'});
 await request('/api/v1/auth/me',{token:changed.token,status:401});
 const next=await request('/api/v1/subaccounts',{token:alice.token,method:'POST',body:{username:`${unique}replacement`,password}});assert.equal(next.role,'readonly');
});
test('malformed payloads and untrusted origins fail without changing data',async()=>{
 const r=await fetch(`${base}/api/v1/problems`,{method:'POST',headers:{Authorization:`Bearer ${alice.token}`,'Content-Type':'application/json'},body:'{bad'});assert.equal(r.status,400);
 const origin=await fetch(`${base}/api/v1/auth/me`,{headers:{Authorization:`Bearer ${alice.token}`,Origin:'https://attacker.invalid'}});assert.equal(origin.status,403);
});
