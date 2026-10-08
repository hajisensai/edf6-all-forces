// node --test test/*.test.mjs  (Node 22.5+: node:sqlite)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { d1 } from './d1.mjs';
import { parseAccounts } from '../src/auth.js';
import { HttpError } from '../src/http.js';
import { Plan, RATE, between, clean, scope } from '../src/plan.js';
import worker from '../src/worker.js';

const MISSIONS = [
  { key: 'EDF6/M001', series: 'EDF6', n: 1, sc: '一', ja: 'いち' },
  { key: 'EDF6/M002', series: 'EDF6', n: 2, sc: '二', ja: 'に' },
  { key: 'EDF5/M001', series: 'EDF5', n: 3, sc: '三', ja: 'さん' },
];
const alice = { user: 'alice', role: 'tester', group: 'a' };
const amy = { user: 'amy', role: 'tester', group: 'a' };
const bob = { user: 'bob', role: 'tester', group: 'b' };
const dev = { user: 'dev', role: 'dev', group: '' };

const env = () => ({ DB: d1() });
const order = async (e, grp) => (await e.DB.prepare('SELECT * FROM outline WHERE grp = ?1 ORDER BY sort, id').bind(grp).all()).results
  .map((r) => (r.kind === 'stock' ? r.mission : r.kind === 'loop' ? 'loop:' + r.note : 'new:' + r.proposal_id));
const status = async (p) => { try { await p; return 200; } catch (e) { if (e instanceof HttpError) return e.status; throw e; } };

test('clean drops invisible and control characters, keeps lines, bounds length', () => {
  assert.equal(clean('a\u202Eb\u2066c\u200Bd\u0007e\uFEFF', 100), 'abcde');
  assert.equal(clean(' x\r\ny\tz\n\n\n\n\nw ', 100), 'x\ny\tz\n\n\nw');
  assert.equal(clean('😀😀😀', 2), '😀😀');
  assert.equal(clean(null, 10), '');
  assert.equal(clean('<img src=x onerror=alert(1)>', 100), '<img src=x onerror=alert(1)>');   // text, shown as text
});

test('between: midpoints, ends, worn-out gaps', () => {
  assert.equal(between(null, null), 0);
  assert.equal(between(null, 5), 4);
  assert.equal(between(5, null), 6);
  assert.equal(between(1, 2), 1.5);
  assert.equal(between(1, 1 + 1e-9), null);
});

test('scope: players get their own group whatever they ask; developers pick a valid one', () => {
  assert.equal(scope(alice, 'b'), 'a');
  assert.equal(scope(dev, 'b'), 'b');
  assert.equal(scope(dev, null), '');
  assert.throws(() => scope(dev, '<x>'), HttpError);
});

test('accounts carry a group; a malformed group falls back to none', () => {
  const a = parseAccounts('u1:p:tester:红队, u2:p:dev, u3:p:tester:bad group!');
  assert.equal(a.get('u1').group, '红队');
  assert.equal(a.get('u2').group, '');
  assert.equal(a.get('u3').group, '');
});

test('outline: seeded in the game order once, loops and new missions placed and moved', async () => {
  const e = env();
  const p = new Plan(e, alice, MISSIONS);
  await p.seed('a');
  await p.seed('a');
  assert.deepEqual(await order(e, 'a'), ['EDF6/M001', 'EDF6/M002', 'EDF5/M001']);
  const rows = (await p.state('a', [])).outline;
  await p.insert('a', { kind: 'loop', note: '第 2 轮回', after: rows[0].id });
  await p.insert('a', { kind: 'loop', note: '开头', after: null });
  const { id } = await p.saveProposal('a', { kind: 'new', title: '地底蚂蚁巢', body: '全是蚂蚁' });
  await p.insert('a', { kind: 'new', proposal_id: id, after: rows[2].id });
  assert.equal(await status(p.insert('a', { kind: 'new', proposal_id: id, after: null })), 409);
  assert.deepEqual(await order(e, 'a'), ['loop:开头', 'EDF6/M001', 'loop:第 2 轮回', 'EDF6/M002', 'EDF5/M001', 'new:' + id]);
  await p.move('a', { id: rows[2].id, after: null });
  assert.deepEqual(await order(e, 'a'), ['EDF5/M001', 'loop:开头', 'EDF6/M001', 'loop:第 2 轮回', 'EDF6/M002', 'new:' + id]);
  assert.equal(await status(p.remove('a', { id: rows[0].id })), 400);   // stock rows stay
  await p.deleteProposal('a', { id });
  assert.ok(!(await order(e, 'a')).includes('new:' + id));
});

test('outline: inserting again and again at one spot renumbers instead of losing the order', async () => {
  const e = env();
  const p = new Plan(e, dev, MISSIONS);
  const first = (await p.state('', [])).outline[0].id;
  for (let i = 0; i < 40; i++) await p.insert('', { kind: 'loop', note: String(i), after: first });
  const got = await order(e, '');
  assert.deepEqual(got.slice(0, 2), ['EDF6/M001', 'loop:39']);
  assert.deepEqual(got.slice(-3), ['loop:0', 'EDF6/M002', 'EDF5/M001']);
});

test('groups: another group sees nothing and can touch nothing; only authors and developers edit', async () => {
  const e = env();
  const { id } = await new Plan(e, alice, MISSIONS).saveProposal('a', { kind: 'edit', mission: 'EDF6/M002', title: '改一下' });
  const b = new Plan(e, bob, MISSIONS);
  assert.equal((await b.state(scope(bob, 'a'), [])).proposals.length, 0);
  assert.equal(await status(b.reply(scope(bob, 'a'), { proposal_id: id, body: 'hi' })), 404);
  assert.equal(await status(b.deleteProposal(scope(bob, 'a'), { id })), 404);
  const m = new Plan(e, amy, MISSIONS);
  assert.equal(await status(m.saveProposal('a', { id, title: '我改了' })), 403);
  await m.reply('a', { proposal_id: id, body: '同意' });
  assert.equal(await status(m.setStatus('a', { id, status: 'done' })), 403);
  await new Plan(e, dev, MISSIONS).setStatus('a', { id, status: 'accepted' });
  const st = await new Plan(e, dev, MISSIONS).state('a', []);
  assert.equal(st.proposals[0].status, 'accepted');
  assert.equal(st.replies[0].body, '同意');
  assert.equal(await status(m.saveProposal('a', { kind: 'edit', mission: 'EDF7/M001', title: 'x' })), 400);
  assert.equal(await status(m.saveProposal('a', { kind: 'new', title: ' \u200B ' })), 400);
});

test('every write is rate limited per account', async () => {
  const saved = RATE[0];
  RATE[0] = [60e3, 3];
  try {
    const p = new Plan(env(), alice, MISSIONS);
    for (let i = 0; i < 3; i++) await p.saveProposal('a', { kind: 'new', title: 't' + i });
    assert.equal(await status(p.saveProposal('a', { kind: 'new', title: 'one too many' })), 429);
  } finally {
    RATE[0] = saved;
  }
});

// ------------------------------------------------------------ through the worker

const wenv = () => ({
  ACCOUNTS: 'alice:pw:tester:a,bob:pw:tester:b,dev:pw:dev', SESSION_SECRET: 's', DB: d1(),
  ASSETS: { fetch: () => new Response('<p>page</p>', { headers: { 'content-type': 'text/html' } }) },
});
const call = (e, path, { user = 'alice', method = 'GET', data, headers = {} } = {}) => worker.fetch(new Request('https://edf6.test' + path, {
  method,
  headers: { authorization: 'Basic ' + btoa(user + ':pw'), ...(data ? { 'content-type': 'application/json' } : {}), ...headers },
  body: data ? JSON.stringify(data) : undefined,
}), e);

test('worker: pages and replies carry the security headers', async () => {
  const e = wenv();
  for (const r of [await call(e, '/plan.html'), await call(e, '/api/plan'), await call(e, '/nope', { method: 'POST' })]) {
    assert.match(r.headers.get('content-security-policy'), /script-src 'self'/);
    assert.doesNotMatch(r.headers.get('content-security-policy'), /unsafe-inline/);
    assert.equal(r.headers.get('x-content-type-options'), 'nosniff');
  }
});

test('worker: cross-site writes, non-JSON bodies and unknown actions are refused', async () => {
  const e = wenv();
  const data = { kind: 'new', title: 'x' };
  assert.equal((await call(e, '/api/plan/proposal', { method: 'POST', data, headers: { origin: 'https://evil.example' } })).status, 403);
  assert.equal((await call(e, '/api/plan/proposal', { method: 'POST', data, headers: { origin: 'https://edf6.test' } })).status, 200);
  const form = await worker.fetch(new Request('https://edf6.test/api/plan/proposal', {
    method: 'POST', body: JSON.stringify(data), headers: { authorization: 'Basic ' + btoa('alice:pw'), 'content-type': 'text/plain' },
  }), e);
  assert.equal(form.status, 415);
  assert.equal((await call(e, '/api/plan/constructor', { method: 'POST', data: {} })).status, 404);
  assert.equal((await call(e, '/api/plan/__proto__', { method: 'POST', data: {} })).status, 404);
});

test('worker: a player asking for another group gets their own; the developer sees every group', async () => {
  const e = wenv();
  await call(e, '/api/plan/proposal', { method: 'POST', data: { kind: 'new', title: 'a 组的' } });
  const bob = await (await call(e, '/api/plan?group=a', { user: 'bob' })).json();
  assert.equal(bob.group, 'b');
  assert.equal(bob.proposals.length, 0);
  assert.deepEqual(bob.groups, ['b']);
  const devView = await (await call(e, '/api/plan?group=a', { user: 'dev' })).json();
  assert.equal(devView.proposals[0].title, 'a 组的');
  assert.deepEqual(devView.groups, ['', 'a', 'b']);
  assert.ok(devView.missions.length > 280);   // EDF6 and EDF5
});

test('worker: script digests are uploaded by developers and read by every account', async () => {
  const e = wenv();
  const put = (user, text) => worker.fetch(new Request('https://edf6.test/api/plan/digest?mission=EDF6%2FM001', {
    method: 'PUT', body: text, headers: { authorization: 'Basic ' + btoa(user + ':pw'), 'content-type': 'text/plain' },
  }), e);
  assert.equal((await call(e, '/api/plan/digest?mission=EDF6%2FM001')).status, 404);
  assert.equal((await put('alice', 'x')).status, 403);
  assert.equal((await put('dev', '■ [0] 開始\n⊘ old')).status, 200);
  const d = await (await call(e, '/api/plan/digest?mission=EDF6%2FM001', { user: 'bob' })).json();
  assert.equal(d.body, '■ [0] 開始\n⊘ old');
  assert.deepEqual((await (await call(e, '/api/plan')).json()).digests, ['EDF6/M001']);
  assert.equal((await call(e, '/api/plan/digest?mission=EDF7%2FM001')).status, 400);
});

// Hidden characters in the site's own source would defeat reading it for exactly the tricks the plan strips from
// players' text (escape them as \uXXXX instead).
test('source holds no invisible or direction characters', async () => {
  const { readdirSync, readFileSync } = await import('node:fs');
  const hidden = /[\u200B-\u200F\u202A-\u202E\u2060-\u2069\uFEFF\uFFF9-\uFFFB]/u;
  for (const dir of ['src', 'public', 'test']) {
    for (const name of readdirSync(new URL(`../${dir}/`, import.meta.url))) {
      const text = readFileSync(new URL(`../${dir}/${name}`, import.meta.url), 'utf8');
      assert.ok(!hidden.test(text), `${dir}/${name} holds an invisible character`);
    }
  }
});
