import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import worker from '../src/worker.js';
import { d1 } from './d1.mjs';
import { RATE } from '../src/plan.js';

function env() {
  return { ACCOUNTS: 'alice:pw:tester:a,amy:pw:tester:a,bob:pw:tester:b,dev:pw:dev',
    SESSION_SECRET: 'test', DB: d1(), ASSETS: { fetch: () => new Response('asset') } };
}
function call(e, path, data, user = 'alice') {
  return worker.fetch(new Request('https://edf6.test' + path, {
    method: data === undefined ? 'GET' : 'POST',
    headers: { authorization: 'Basic ' + btoa(user + ':pw'), 'content-type': 'application/json' },
    body: data === undefined ? undefined : JSON.stringify(data),
  }), e);
}
const draft = (title, extra = {}) => ({ title, body: '剧情\n下一段', status: 'planned', ...extra });
const rows = async (e, user = 'alice', group = '') => (await (await call(e, '/api/story?group=' + group, undefined, user)).json()).rows;

test('story: creation, edit and deletion are separate from the planning outline', async () => {
  const e = env();
  const created = await call(e, '/api/story/save', draft('开端', { mission: 'EDF41/M190' }));
  assert.equal(created.status, 200);
  const { id } = await created.json();
  assert.equal((await rows(e))[0].mission, 'EDF41/M190');
  assert.equal((await call(e, '/api/story/save', draft('突围', { id, status: 'testing' }))).status, 200);
  assert.equal((await rows(e))[0].title, '突围');
  assert.equal((await rows(e))[0].status, 'testing');
  assert.equal(e.DB.raw.prepare('SELECT COUNT(*) AS n FROM outline').get().n, 0);
  assert.equal(e.DB.raw.prepare('SELECT COUNT(*) AS n FROM proposals').get().n, 0);
  assert.equal((await call(e, '/api/story/remove', { id })).status, 200);
  assert.deepEqual(await rows(e), []);
});

test('story: groups are isolated; only authors and developers edit or delete', async () => {
  const e = env();
  const { id } = await (await call(e, '/api/story/save', draft('本组', { group: 'b' }))).json();
  assert.equal((await rows(e)).length, 1);
  assert.deepEqual(await rows(e, 'bob', 'a'), []);
  assert.equal((await call(e, '/api/story/save', draft('篡改', { id }), 'bob')).status, 404);
  assert.equal((await call(e, '/api/story/move', { id, direction: 'up' }, 'bob')).status, 404);
  assert.equal((await call(e, '/api/story/remove', { id }, 'amy')).status, 403);
  assert.equal((await call(e, '/api/story/save', draft('篡改', { id }), 'amy')).status, 403);
  assert.equal((await call(e, '/api/story/save', draft('开发者修改', { id, group: 'a' }), 'dev')).status, 200);
  assert.equal((await rows(e, 'dev', 'a'))[0].title, '开发者修改');
});

test('story: teammates can move missions; boundaries and worn gaps preserve every mission', async () => {
  const e = env();
  const ids = [];
  for (const title of ['一', '二', '三']) ids.push((await (await call(e, '/api/story/save', draft(title))).json()).id);
  await call(e, '/api/story/move', { id: ids[0], direction: 'up' }, 'amy');
  assert.deepEqual((await rows(e)).map((r) => r.title), ['一', '二', '三']);
  await call(e, '/api/story/move', { id: ids[0], direction: 'down' }, 'amy');
  assert.deepEqual((await rows(e)).map((r) => r.title), ['二', '一', '三']);
  e.DB.raw.prepare('UPDATE story_missions SET sort = ? WHERE id = ?').run(1 + 1e-9, ids[0]);
  e.DB.raw.prepare('UPDATE story_missions SET sort = ? WHERE id = ?').run(1, ids[1]);
  e.DB.raw.prepare('UPDATE story_missions SET sort = ? WHERE id = ?').run(2, ids[2]);
  await call(e, '/api/story/move', { id: ids[2], direction: 'up' });
  assert.deepEqual((await rows(e)).map((r) => r.title), ['二', '三', '一']);
});

test('story: authentication, validation, clean text and write limits', async () => {
  const e = env();
  assert.equal((await worker.fetch(new Request('https://edf6.test/api/story'), e)).status, 401);
  for (const data of [null, [], draft(''), draft('x', { status: 'invalid' }), draft('x', { mission: 'EDF7/M1' }), draft('x', { id: 'oops' })]) {
    assert.equal((await call(e, '/api/story/save', data)).status, 400);
  }
  const { id } = await (await call(e, '/api/story/save', draft('a\u202Eb <img src=x>', { body: 'x\u0007y' }))).json();
  const [saved] = await rows(e);
  assert.equal(saved.title, 'ab <img src=x>');
  assert.equal(saved.body, 'xy');
  assert.equal((await call(e, '/api/story/move', { id, direction: 'sideways' })).status, 400);
  assert.equal((await call(e, '/api/story/remove', {})).status, 400);
  const prior = RATE[0];
  RATE[0] = [60e3, 1];
  try { assert.equal((await call(e, '/api/story/save', draft('频率'))).status, 429); }
  finally { RATE[0] = prior; }
});

test('story migration is repeatable and preserves existing plans', () => {
  const e = env();
  e.DB.raw.exec('DROP TABLE story_missions'); // stand in for the pre-feature database
  e.DB.raw.exec("INSERT INTO outline (grp, kind, note, sort, at) VALUES ('a', 'loop', '已有轮回', 1, 1)");
  const sql = readFileSync(new URL('../migrations/0002-story-missions.sql', import.meta.url), 'utf8');
  e.DB.raw.exec(sql);
  e.DB.raw.exec(sql);
  assert.equal(e.DB.raw.prepare('SELECT note FROM outline').get().note, '已有轮回');
});

test('story limit is per group and refuses an extra entry without removing existing ones', async () => {
  const e = env();
  e.DB.raw.exec(`WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x + 1 FROM n WHERE x < 1000)
    INSERT INTO story_missions (grp, title, sort, author, created_at, updated_at)
    SELECT 'a', '关卡', x, 'alice', 1, 1 FROM n`);
  assert.equal((await call(e, '/api/story/save', draft('超出上限'))).status, 409);
  assert.equal((await rows(e)).length, 1000);
  assert.equal((await call(e, '/api/story/save', draft('另一组'), 'bob')).status, 200);
});
