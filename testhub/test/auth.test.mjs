// node --test test/*.test.mjs  (Node 20+: WebCrypto, atob, btoa are globals)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseAccounts, checkLogin, makeSession, readSession, basicUser } from '../src/auth.js';

const accounts = parseAccounts('alice:pw1:tester, bob:pw2:dev, broken');

test('accounts and roles', () => {
  assert.deepEqual(checkLogin(accounts, 'alice', 'pw1'), { user: 'alice', role: 'tester', group: '' });
  assert.deepEqual(checkLogin(accounts, 'bob', 'pw2'), { user: 'bob', role: 'dev', group: '' });
  assert.equal(checkLogin(accounts, 'alice', 'pw2'), null);
  assert.equal(checkLogin(accounts, 'broken', ''), null);
});

test('basic header', () => {
  assert.deepEqual(basicUser('Basic ' + btoa('alice:p:w')), { user: 'alice', pass: 'p:w' });
  assert.equal(basicUser('Bearer x'), null);
  assert.equal(basicUser('Basic !!!'), null);
});

test('session cookie: valid, expired, forged, password changed', async () => {
  const now = Date.now();
  const s = await makeSession(accounts.get('alice'), now + 1000, 'k');
  assert.deepEqual(await readSession(`x=1; s=${s}`, 'k', accounts, now), { user: 'alice', role: 'tester', group: '' });
  assert.equal(await readSession(`s=${s}`, 'k', accounts, now + 2000), null);
  assert.equal(await readSession(`s=${s}`, 'other', accounts, now), null);
  assert.equal(await readSession(`s=${s.replace('alice', 'bob')}`, 'k', accounts, now), null);
  assert.equal(await readSession(`s=${s}`, 'k', parseAccounts('alice:new:tester'), now), null);
});
