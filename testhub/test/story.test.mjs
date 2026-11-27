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
const rows = async (e, user = 'alice', group) => (await (await call(e, '/api/story' + (group == null ? '' : '?group=' + group), undefined, user)).json()).rows;

test('story: creation, edit and deletion are separate from the planning outline', async () => {
  const e = env();
  const created = await call(e, '/api/story/save', draft('开端', { mission: 'EDF41/M190' }));
  assert.equal(created.status, 200);
  const { id } = await created.json();
  assert.equal((await rows(e))[0].mission, 'EDF41/M190');
  assert.equal((await call(e, '/api/story/save', draft('突围', { id, version: 1, status: 'testing' }))).status, 200);
  assert.equal((await rows(e))[0].title, '突围');
  assert.equal((await rows(e))[0].status, 'testing');
  assert.equal(e.DB.raw.prepare('SELECT COUNT(*) AS n FROM outline').get().n, 0);
  assert.equal(e.DB.raw.prepare('SELECT COUNT(*) AS n FROM proposals').get().n, 0);
  assert.equal((await call(e, '/api/story/remove', { id })).status, 200);
  assert.deepEqual(await rows(e), []);
});

test('story: groups are isolated; any member of the group and developers edit or delete', async () => {
  const e = env();
  const { id } = await (await call(e, '/api/story/save', draft('本组', { group: 'b' }))).json();
  assert.equal((await rows(e)).length, 1);
  assert.deepEqual(await rows(e, 'bob'), []);
  const other = await (await call(e, '/api/story?group=a', undefined, 'bob')).json();   // readable, not writable
  assert.equal(other.rows[0].title, '本组');
  assert.equal(other.editable, false);
  assert.equal((await call(e, '/api/story/save', draft('篡改', { id, version: 1 }), 'bob')).status, 404);
  assert.equal((await call(e, '/api/story/move', { id, direction: 'up' }, 'bob')).status, 404);
  assert.equal((await call(e, '/api/story/remove', { id }, 'bob')).status, 404);
  assert.equal((await call(e, '/api/story/save', draft('队友修改', { id, version: 1 }), 'amy')).status, 200);
  assert.equal((await rows(e))[0].updated_by, 'amy');
  assert.equal((await call(e, '/api/story/save', draft('开发者修改', { id, version: 2, group: 'a' }), 'dev')).status, 200);
  assert.equal((await rows(e, 'dev', 'a'))[0].title, '开发者修改');
  assert.equal((await call(e, '/api/story/remove', { id }, 'amy')).status, 200);
  assert.deepEqual(await rows(e), []);
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

test('story: a mission jumps straight to any place in one move', async () => {
  const e = env();
  const ids = [];
  for (const title of ['一', '二', '三', '四', '五']) ids.push((await (await call(e, '/api/story/save', draft(title))).json()).id);
  const titles = async () => (await rows(e)).map((r) => r.title);
  assert.equal((await call(e, '/api/story/move', { id: ids[4], position: 1 }, 'amy')).status, 200);
  assert.deepEqual(await titles(), ['五', '一', '二', '三', '四']);
  await call(e, '/api/story/move', { id: ids[4], position: '4' });
  assert.deepEqual(await titles(), ['一', '二', '三', '五', '四']);
  await call(e, '/api/story/move', { id: ids[0], position: 99 });
  assert.deepEqual(await titles(), ['二', '三', '五', '四', '一']);
  await call(e, '/api/story/move', { id: ids[1], position: 0 });
  assert.deepEqual(await titles(), ['二', '三', '五', '四', '一']);
  assert.equal((await call(e, '/api/story/move', { id: ids[1], position: 'x' })).status, 400);
});

test('story: authentication, validation, clean text and write limits', async () => {
  const e = env();
  const visitor = (path, init) => worker.fetch(new Request('https://edf6.test' + path, init), e);
  assert.equal((await visitor('/api/story/save', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(draft('x')) })).status, 401);
  assert.deepEqual((await (await visitor('/api/story')).json()).groups, []);
  await call(e, '/api/story/save', draft('公开'));
  const seen = await (await visitor('/api/story')).json();
  assert.deepEqual([seen.me, seen.group, seen.editable, seen.rows[0].title, seen.rows[0].author, seen.rows[0].updated_by], [null, 'a', false, '公开', null, null]);
  assert.equal((await visitor('/story.html')).status, 200);
  for (const data of [null, [], draft(''), draft('x', { status: 'invalid' }), draft('x', { mission: 'EDF7/M1' }), draft('x', { id: 'oops' }),
    draft('x', { book: -1 }), draft('x', { book: 'one' }), draft('x', { book: 1, step: 'a b' }), draft('x', { after: 'x' })]) {
    assert.equal((await call(e, '/api/story/save', data)).status, 400);
  }
  const { id } = await (await call(e, '/api/story/save', draft('a\u202Eb <img src=x>', { body: 'x\u0007y' }))).json();
  const saved = (await rows(e)).at(-1);
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

test('story: a mission carries its goal, enemies, hand-over and outline step; edits are checked against its version', async () => {
  const e = env();
  const first = await (await call(e, '/api/story/save', draft('开端', { goal: '守住基地', enemies: '蚂蚁\n蜘蛛', handoff: '撤往港口', book: 3, step: 'w6s1' }))).json();
  assert.equal(first.version, 1);
  const [r] = await rows(e);
  assert.deepEqual([r.goal, r.enemies, r.handoff, r.book, r.step, r.version, r.updated_by], ['守住基地', '蚂蚁\n蜘蛛', '撤往港口', 3, 'w6s1', 1, 'alice']);
  // alice and amy both open version 1: the first save wins, the second is refused and changes nothing.
  assert.equal((await call(e, '/api/story/save', draft('alice 的', { id: first.id, version: 1 }))).status, 200);
  const late = await call(e, '/api/story/save', draft('amy 的', { id: first.id, version: 1 }), 'amy');
  assert.equal(late.status, 409);
  assert.match((await late.json()).error, /alice/);
  assert.equal((await rows(e))[0].title, 'alice 的');
  assert.equal((await call(e, '/api/story/save', draft('无版本', { id: first.id }))).status, 400);
  // No outline number: no step either (the link is both or neither).
  await call(e, '/api/story/save', draft('解绑', { id: first.id, version: 2, step: 'w6s1' }));
  assert.deepEqual([(await rows(e))[0].book, (await rows(e))[0].step], [null, null]);
});

test('story: a new mission goes right after the one named, renumbering a worn-out gap', async () => {
  const e = env();
  const ids = [];
  for (const title of ['一', '三']) ids.push((await (await call(e, '/api/story/save', draft(title))).json()).id);
  await call(e, '/api/story/save', draft('二', { after: ids[0] }));
  await call(e, '/api/story/save', draft('四', { after: ids[1] }));
  assert.deepEqual((await rows(e)).map((r) => r.title), ['一', '二', '三', '四']);
  e.DB.raw.prepare('UPDATE story_missions SET sort = ? WHERE id = ?').run(1 + 1e-9, ids[1]);
  e.DB.raw.prepare("UPDATE story_missions SET sort = 1 WHERE title = '一'").run();
  e.DB.raw.prepare("UPDATE story_missions SET sort = 1 + 5e-10 WHERE title = '二'").run();
  await call(e, '/api/story/save', draft('一点五', { after: ids[0] }));
  assert.deepEqual((await rows(e)).map((r) => r.title), ['一', '一点五', '二', '三', '四']);
  assert.equal((await call(e, '/api/story/save', draft('无处可插', { after: 9999 }))).status, 404);
});

test('story: migration 0008 adds the new fields to a database made before them', () => {
  const e = env();
  e.DB.raw.exec('DROP TABLE story_missions');
  e.DB.raw.exec(readFileSync(new URL('../migrations/0002-story-missions.sql', import.meta.url), 'utf8'));
  e.DB.raw.exec("INSERT INTO story_missions (grp, title, sort, author, created_at, updated_at) VALUES ('a', '旧关卡', 1, 'alice', 1, 1)");
  e.DB.raw.exec(readFileSync(new URL('../migrations/0008-story-mission-fields.sql', import.meta.url), 'utf8'));
  const r = e.DB.raw.prepare('SELECT * FROM story_missions').get();
  assert.deepEqual([r.title, r.goal, r.book, r.version, r.updated_by], ['旧关卡', '', null, 1, 'alice']);
});
