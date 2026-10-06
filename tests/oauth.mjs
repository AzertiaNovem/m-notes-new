import test, {before, after} from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createHash, randomBytes} from 'node:crypto';
import {mkdtemp, rm, readFile, readdir} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {createServer} from 'node:net';
import {setTimeout as sleep} from 'node:timers/promises';

let server, directory, base, owner, readonly, client, otherClient, access, pendingCode, output='';
const password='OAuth-test-2026-secret';
const callback='https://client.example/callback?existing=1';
const headerCallback='https://CALLBACK.EXAMPLE?existing=1;script-src=*';
const verifier=randomBytes(48).toString('base64url');
const challenge=createHash('sha256').update(verifier).digest('base64url');
async function json(path,{method='GET',body,token,status=200}={}) {
  const response=await fetch(base+path,{method,headers:{...(body?{'Content-Type':'application/json'}:{}),...(token?{Authorization:`Bearer ${token}`}:{})},body:body?JSON.stringify(body):undefined,redirect:'manual'});
  const data=await response.json();assert.equal(response.status,status,`${path}: ${JSON.stringify(data)}`);return data;
}
async function form(path,body,{cookie,status=200,json=true}={}) {
  const response=await fetch(base+path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded',...(cookie?{Cookie:cookie}:{})},body:new URLSearchParams(body),redirect:'manual'});
  const text=await response.text();assert.equal(response.status,status,`${path}: ${text}`);return json?JSON.parse(text):{response,text};
}
function authParams(overrides={}) {return {client_id:client.client_id,redirect_uri:callback,response_type:'code',code_challenge:challenge,code_challenge_method:'S256',resource:base+'/mcp',scope:'mcp:tools',state:'opaque state & = /?',...overrides};}
async function authForm(overrides={}) {
  const params=authParams(overrides);const response=await fetch(base+'/authorize?'+new URLSearchParams(params),{redirect:'manual'});const html=await response.text();assert.equal(response.status,200,html);
  const request_id=html.match(/name='request_id' value='([^']+)'/)?.[1];assert.ok(request_id);const cookie=response.headers.get('set-cookie').split(';')[0];assert.ok(response.headers.get('set-cookie').includes('HttpOnly'));assert.ok(response.headers.get('content-security-policy').includes("frame-ancestors 'none'"));
  assert.equal(response.headers.get('referrer-policy'),'same-origin','the form must send a verifiable Origin without leaking its URL across origins');
  const formAction=response.headers.get('content-security-policy').split(';').map(x=>x.trim()).find(x=>x.startsWith('form-action '));
  assert.equal(formAction,`form-action 'self' ${new URL(params.redirect_uri).origin}`,'the browser must be allowed to follow the registered callback after a form POST');
  return {request_id,cookie,params,html,policy:response.headers.get('content-security-policy')};
}
async function authorize(account=owner,overrides={}) {
  const page=await authForm(overrides);const {response}=await form('/authorize',{request_id:page.request_id,username:account.user?.username??account.username,password,approve:'yes'},{cookie:page.cookie,status:303,json:false});
  const location=new URL(response.headers.get('location'));assert.equal(location.origin,'https://client.example');assert.equal(location.searchParams.get('state'),page.params.state);assert.equal(location.searchParams.get('iss'),base);assert.equal(location.searchParams.get('existing'),'1');return {code:location.searchParams.get('code'),page};
}
async function exchange(code,overrides={},status=200) {return form('/token',{grant_type:'authorization_code',client_id:client.client_id,redirect_uri:callback,code,code_verifier:verifier,resource:base+'/mcp',...overrides},{status});}
async function rpc(token,name='list_taxonomy',args={}) {return json('/mcp',{method:'POST',token,body:{jsonrpc:'2.0',id:1,method:'tools/call',params:{name,arguments:args}}});}
async function freePort(){const socket=createServer();await new Promise(resolve=>socket.listen(0,'127.0.0.1',resolve));const port=socket.address().port;await new Promise(resolve=>socket.close(resolve));return port;}
before(async()=>{
  directory=await mkdtemp(join(tmpdir(),'mistakebook-oauth-test-'));const port=await freePort();base=`http://127.0.0.1:${port}`;
  server=spawn(resolve(process.env.BINARY_PATH??'backend/build/mistakebook'),[],{env:{...process.env,DATABASE_PATH:join(directory,'oauth.db'),PORT:String(port),HOST:'127.0.0.1',PUBLIC_URL:base,CORS_ORIGINS:base,WEB_ROOT:join(directory,'no-web')},stdio:['ignore','pipe','pipe']});server.stdout.on('data',chunk=>output+=chunk);server.stderr.on('data',chunk=>output+=chunk);
  for(let i=0;i<100;i++){try{if((await fetch(base+'/health')).ok)break;}catch{}if(server.exitCode!==null)throw new Error(output);await sleep(50);}
  owner=await json('/api/v1/auth/register',{method:'POST',body:{username:'oauth_owner',password}});
  readonly=await json('/api/v1/subaccounts',{method:'POST',token:owner.token,body:{username:'oauth_reader',password}});
});
after(async()=>{if(server&&server.exitCode===null){const ended=new Promise(resolve=>server.once('exit',resolve));server.kill('SIGTERM');await ended;}if(directory)await rm(directory,{recursive:true,force:true});});
test('authorization and protected-resource discovery advertise S256 and audience',async()=>{
  const metadata=await json('/.well-known/oauth-authorization-server');assert.deepEqual(metadata.code_challenge_methods_supported,['S256']);assert.deepEqual(metadata.token_endpoint_auth_methods_supported,['none']);assert.equal(metadata.issuer,base);
  for(const path of ['/.well-known/oauth-protected-resource','/.well-known/oauth-protected-resource/mcp']){const resource=await json(path);assert.equal(resource.resource,base+'/mcp');assert.deepEqual(resource.authorization_servers,[base]);}
  const response=await fetch(base+'/mcp',{method:'POST',headers:{'Content-Type':'application/json'},body:'{"jsonrpc":"2.0","id":1,"method":"ping"}'});assert.equal(response.status,401);assert.ok(response.headers.get('www-authenticate').includes('resource_metadata='));
});
test('dynamic registration restricts redirects and public client authentication',async()=>{
  for(const redirect of ['http://evil.example/cb','https://client.example/cb#fragment','https://user@client.example/cb','javascript:alert(1)','https://*.example/cb','https://%31%32%37.0.0.1/cb','https://client.example/\r\nInjected'])await json('/register',{method:'POST',body:{redirect_uris:[redirect]},status:400});
  await json('/register',{method:'POST',body:{redirect_uris:[callback],token_endpoint_auth_method:'client_secret_basic'},status:400});
  client=await json('/register',{method:'POST',body:{client_name:'Test <script>alert(1)</script>',redirect_uris:[callback,headerCallback,'http://127.0.0.1:8888/callback'],token_endpoint_auth_method:'none'},status:201});assert.equal(client.client_secret,undefined);
  otherClient=await json('/register',{method:'POST',body:{redirect_uris:[callback]},status:201});
});
test('authorization validates exact redirects, resource, challenge, and scope',async()=>{
  for(const override of [{redirect_uri:callback+'/wrong'},{resource:'https://another.example/mcp'},{code_challenge_method:'plain'},{code_challenge:'short'},{scope:'admin'}])await json('/authorize?'+new URLSearchParams(authParams(override)),{status:400});
  await json('/authorize?'+new URLSearchParams(authParams({client_id:'missing'})),{status:401});
});
test('consent form escapes client names and binds approval to browser nonce',async()=>{
  const page=await authForm({redirect_uri:headerCallback});assert.ok(page.html.includes('&lt;script&gt;'));assert.ok(!page.html.includes('Test <script>'));
  assert.ok(!page.policy.includes('existing=') && !page.policy.includes('script-src='));
  assert.ok(!page.policy.includes('https://client.example'),'other registered callbacks are not allowed by this form policy');
  await form('/authorize',{request_id:page.request_id,username:owner.user.username,password,approve:'yes'},{status:403});
  await form('/authorize',{request_id:page.request_id,username:owner.user.username,password:'wrong-password',approve:'yes'},{cookie:page.cookie,status:401,json:false});
  const {response}=await form('/authorize',{request_id:page.request_id,approve:'no'},{cookie:page.cookie,status:303,json:false});assert.equal(new URL(response.headers.get('location')).searchParams.get('error'),'access_denied');
  await form('/authorize',{request_id:page.request_id,username:owner.user.username,password,approve:'yes'},{cookie:page.cookie,status:403});
});
test('PKCE exchange binds client, exact redirect and audience',async()=>{
  const {code,page}=await authorize();pendingCode=code;
  await form('/authorize',{request_id:page.request_id,username:owner.user.username,password,approve:'yes'},{cookie:page.cookie,status:403});
  await exchange(code,{code_verifier:'B'.repeat(43)},400);await exchange(code,{client_id:otherClient.client_id},400);await exchange(code,{redirect_uri:callback+'/wrong'},400);await exchange(code,{resource:'https://other.example/mcp'},400);
  access=await exchange(code);assert.equal(access.token_type,'Bearer');assert.equal(access.expires_in,3600);assert.ok(access.refresh_token);assert.equal((await rpc(access.access_token)).result.isError,false);
  await json('/api/v1/auth/me',{token:access.access_token,status:401});
});
test('authorization code reuse is rejected and revokes issued tokens',async()=>{
  await exchange(pendingCode,{},400);await json('/mcp',{method:'POST',token:access.access_token,body:{jsonrpc:'2.0',id:1,method:'ping'},status:401});
});
test('refresh tokens rotate and replay revokes the full token family',async()=>{
  const {code}=await authorize();const original=await exchange(code);
  await form('/token',{grant_type:'refresh_token',client_id:otherClient.client_id,refresh_token:original.refresh_token,resource:base+'/mcp'},{status:400});
  const rotated=await form('/token',{grant_type:'refresh_token',client_id:client.client_id,refresh_token:original.refresh_token,resource:base+'/mcp'});assert.notEqual(rotated.refresh_token,original.refresh_token);assert.equal((await rpc(rotated.access_token)).result.isError,false);
  await json('/mcp',{method:'POST',token:original.access_token,body:{jsonrpc:'2.0',id:1,method:'ping'},status:401});
  await form('/token',{grant_type:'refresh_token',client_id:client.client_id,refresh_token:original.refresh_token,resource:base+'/mcp'},{status:400});
  await json('/mcp',{method:'POST',token:rotated.access_token,body:{jsonrpc:'2.0',id:1,method:'ping'},status:401});
});
test('readonly OAuth grants preserve permissions and child deletion revokes access',async()=>{
  const {code}=await authorize(readonly);const readToken=await exchange(code);assert.equal((await rpc(readToken.access_token)).result.isError,false);
  await json('/mcp',{method:'POST',token:readToken.access_token,body:{jsonrpc:'2.0',id:1,method:'tools/call',params:{name:'upsert_note_node',arguments:{title:'Forbidden',body_md:'No',editor_tool:'test',change_summary:'forbidden'}}},status:403});
  await json('/api/v1/subaccounts/'+readonly.id,{method:'DELETE',token:owner.token});
  await json('/mcp',{method:'POST',token:readToken.access_token,body:{jsonrpc:'2.0',id:1,method:'ping'},status:401});
});
test('revocation is client bound and invalidates access plus refresh',async()=>{
  const {code}=await authorize();const tokens=await exchange(code);
  await form('/revoke',{client_id:otherClient.client_id,token:tokens.access_token});assert.equal((await rpc(tokens.access_token)).result.isError,false);
  await form('/revoke',{client_id:client.client_id,token:tokens.refresh_token});await json('/mcp',{method:'POST',token:tokens.access_token,body:{jsonrpc:'2.0',id:1,method:'ping'},status:401});
  await form('/token',{grant_type:'refresh_token',client_id:client.client_id,refresh_token:tokens.refresh_token,resource:base+'/mcp'},{status:400});
});
test('concurrent code and refresh redemption each have a single winner',async()=>{
  const {code}=await authorize();const body={grant_type:'authorization_code',client_id:client.client_id,redirect_uri:callback,code,code_verifier:verifier,resource:base+'/mcp'};
  const send=payload=>fetch(base+'/token',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(payload)});
  const codeResponses=await Promise.all([send(body),send(body)]);assert.deepEqual(codeResponses.map(x=>x.status).sort(),[200,400]);
  const tokens=await exchange((await authorize()).code);const refresh={grant_type:'refresh_token',client_id:client.client_id,refresh_token:tokens.refresh_token,resource:base+'/mcp'};
  const refreshResponses=await Promise.all([send(refresh),send(refresh)]);assert.deepEqual(refreshResponses.map(x=>x.status).sort(),[200,400]);
});
test('database and WAL persist only hashes of authorization credentials',async()=>{
  const {code,page}=await authorize();const tokens=await exchange(code);const files=await readdir(directory);
  const stored=Buffer.concat(await Promise.all(files.map(file=>readFile(join(directory,file))))).toString('latin1');
  for(const secret of [code,page.request_id,page.cookie.split('=')[1],tokens.access_token,tokens.refresh_token])assert.equal(stored.includes(secret),false,'credential must not appear in persisted database pages');
});
test('password rotation revokes active OAuth tokens and pending codes',async()=>{
  const active=await exchange((await authorize()).code);const pending=await authorize();
  owner=await json('/api/v1/auth/password',{method:'POST',token:owner.token,body:{current_password:password,new_password:'changed-password-2026'}});
  await json('/mcp',{method:'POST',token:active.access_token,body:{jsonrpc:'2.0',id:1,method:'ping'},status:401});await exchange(pending.code,{},400);
});
