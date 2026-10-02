import test from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {spawn} from 'node:child_process';
import {mkdtemp, rm} from 'node:fs/promises';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {setTimeout as sleep} from 'node:timers/promises';

test('password rotation prevents an already-verifying old login from creating a fresh session', async()=>{
  const dir=await mkdtemp(join(tmpdir(),'mistakebook-credential-race-'));
  const dbpath=join(dir,'test.db');
  const port=21000+Math.floor(Math.random()*500);
  const base=`http://127.0.0.1:${port}`;
  let output='', db;
  const server=spawn(resolve('backend/build/mistakebook'),[],{env:{...process.env,DATABASE_PATH:dbpath,PORT:String(port),HOST:'127.0.0.1',PUBLIC_URL:base,WEB_ROOT:join(dir,'no-web')},stdio:['ignore','pipe','pipe']});
  server.stdout.on('data',b=>output+=b);server.stderr.on('data',b=>output+=b);
  async function send(path,body){return fetch(`${base}${path}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});}
  try {
    let ready=false;
    for(let i=0;i<100;i++){try{if((await fetch(`${base}/health`)).ok){ready=true;break;}}catch{}await sleep(100);}
    assert.ok(ready,output);
    const oldPassword='Old-password-testing', newPassword='New-password-testing';
    const registration=await send('/api/v1/auth/register',{username:'rotation_test',password:oldPassword});assert.equal(registration.status,200);
    const owner=(await registration.json()).user;
    const donor=await send('/api/v1/auth/register',{username:'new_password_hash',password:newPassword});assert.equal(donor.status,200);
    // Warm the first-login dummy hash before controlling the transaction ordering.
    assert.equal((await send('/api/v1/auth/login',{username:'rotation_test',password:oldPassword})).status,200);
    db=new DatabaseSync(dbpath);db.exec('PRAGMA busy_timeout=10000; BEGIN IMMEDIATE');
    // WAL allows the server to read and verify the previous password while this
    // test holds the write lock, just as an in-flight password update would.
    const pendingLogin=send('/api/v1/auth/login',{username:'rotation_test',password:oldPassword});
    await sleep(400);
    db.prepare("UPDATE users SET password_hash=(SELECT password_hash FROM users WHERE username='new_password_hash') WHERE id=?").run(owner.id);
    db.prepare('DELETE FROM sessions WHERE user_id=?').run(owner.id);
    db.exec('COMMIT');
    const response=await pendingLogin;
    assert.equal(response.status,401,await response.text());
    assert.equal((await send('/api/v1/auth/login',{username:'rotation_test',password:newPassword})).status,200);
    assert.equal((await send('/api/v1/auth/login',{username:'rotation_test',password:oldPassword})).status,401);
  } finally {
    if(db){try{db.exec('ROLLBACK');}catch{}db.close();}
    if(server.exitCode===null){server.kill('SIGTERM');await new Promise(resolve=>server.once('exit',resolve));}
    await rm(dir,{recursive:true,force:true});
  }
});
