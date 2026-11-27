import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import worker from '../src/worker.js';
import { d1 } from './d1.mjs';

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
const entry = (name, extra = {}) => ({ name, kind: 'weapon', status: 'todo', side: '以太联军', look: '黄铜枪身', body: '打出光阵', refs: '', ...extra });
const list = async (e, user = 'alice', group) => (await (await call(e, '/api/design' + (group == null ? '' : '?group=' + group), undefined, user)).json());
const outline = { title: '平行世界', lanes: [{ id: 'scp', name: '以太世界线', steps: [{ id: 'scps1', title: '各国抵抗' }, { id: 'scps2', title: '以太联军' }] }] };

test('design: a group writes its entries with links; saves compare-and-set on the version', async () => {
  const e = env();
  const out = await (await call(e, '/api/design/save', entry('刻印步枪', { book: 1, steps: ['scps1', 'scps1', 'scps2'], missions: [3] }))).json();
  assert.deepEqual([out.id, out.version], [1, 1]);
  let rows = (await list(e)).rows;
  assert.deepEqual([rows[0].name, rows[0].book, rows[0].steps, rows[0].missions, rows[0].author], ['刻印步枪', 1, ['scps1', 'scps2'], [3], 'alice']);
  assert.equal((await call(e, '/api/design/save', entry('刻印步枪 Mk2', { id: 1, version: 1 }), 'amy')).status, 200);
  assert.equal((await call(e, '/api/design/save', entry('我的版本', { id: 1, version: 1 }))).status, 409);
  rows = (await list(e)).rows;
  assert.deepEqual([rows[0].name, rows[0].version, rows[0].updated_by, rows[0].steps], ['刻印步枪 Mk2', 2, 'amy', []]);
  for (const bad of [{ kind: 'spell' }, { status: 'x' }, { name: '  ' }, { book: 1, steps: ['bad id!'] }, { missions: [0] }]) {
    assert.equal((await call(e, '/api/design/save', entry('x', bad))).status, 400, JSON.stringify(bad));
  }
  // another group reads but cannot touch it; visitors read without names
  assert.equal((await list(e, 'bob', 'a')).editable, false);
  assert.equal((await call(e, '/api/design/save', entry('乱改', { id: 1, version: 2, group: 'a' }), 'bob')).status, 404);   // lands in b
  assert.equal((await call(e, '/api/design/remove', { id: 1, group: 'a' }, 'bob')).status, 403);
  const v = await (await worker.fetch(new Request('https://edf6.test/api/design?group=a'), e)).json();
  assert.deepEqual([v.rows.length, v.rows[0].author, v.rows[0].updated_by, v.editable], [1, null, null, false]);
  assert.equal((await call(e, '/api/design/remove', { id: 1 }, 'amy')).status, 200);
  assert.equal((await list(e)).rows.length, 0);
});

test('design + story: an outline open to every account opens the group\'s entries and missions too, except deleting', async () => {
  const e = env();
  await call(e, '/api/outline/save', { outline, version: 0 });
  await call(e, '/api/design/save', entry('以太炮'));
  await call(e, '/api/story/save', { title: '以太炮击', status: 'planned', body: '' });
  assert.equal((await call(e, '/api/design/save', entry('乱写', { group: 'a' }), 'bob')).status, 200);   // not open yet: lands in b
  assert.equal((await list(e, 'bob', 'b')).rows.length, 1);
  assert.equal((await call(e, '/api/outline/settings', { group: 'a', book: 1, open: true, main: true }, 'dev')).status, 200);
  const seen = await list(e, 'bob');
  assert.deepEqual([seen.group, seen.editable, seen.member], ['a', true, false]);   // the main outline's group first
  assert.equal((await call(e, '/api/design/save', entry('刻印军刀', { group: 'a', kind: 'weapon' }), 'bob')).status, 200);
  assert.equal((await call(e, '/api/design/save', entry('以太炮 改', { group: 'a', id: 1, version: 1 }), 'bob')).status, 200);
  assert.deepEqual((await list(e, 'alice')).rows.map((r) => r.name), ['以太炮 改', '刻印军刀']);
  assert.equal((await call(e, '/api/design/remove', { group: 'a', id: 1 }, 'bob')).status, 403);
  assert.equal((await (await call(e, '/api/story', undefined, 'bob')).json()).group, 'a');   // 关卡策划 opens the main outline's group too
  const story = await (await call(e, '/api/story?group=a', undefined, 'bob')).json();
  assert.deepEqual([story.editable, story.member], [true, false]);
  assert.equal((await call(e, '/api/story/save', { group: 'a', id: story.rows[0].id, version: 1, title: '以太炮击（改）', status: 'writing', body: '' }, 'bob')).status, 200);
  assert.equal((await call(e, '/api/story/save', { group: 'a', title: '新的一关', status: 'planned', body: '' }, 'bob')).status, 200);
  assert.deepEqual((await (await call(e, '/api/story?group=a')).json()).rows.map((r) => r.title), ['以太炮击（改）', '新的一关']);
  assert.equal((await call(e, '/api/story/remove', { group: 'a', id: story.rows[0].id }, 'bob')).status, 403);
  assert.equal((await call(e, '/api/story/remove', { group: 'a', id: story.rows[0].id })).status, 200);
});

test('design: migration 0010 is repeatable; the page is text only and public', async () => {
  const e = env();
  e.DB.raw.exec('DROP TABLE story_designs');
  const sql = readFileSync(new URL('../migrations/0010-story-designs.sql', import.meta.url), 'utf8');
  e.DB.raw.exec(sql);
  e.DB.raw.exec(sql);
  assert.equal((await call(e, '/api/design/save', entry('银之人', { kind: 'monster' }))).status, 200);
  assert.doesNotMatch(readFileSync(new URL('../public/design.js', import.meta.url), 'utf8'), /innerHTML|outerHTML|insertAdjacentHTML|document\.write/);
  const html = readFileSync(new URL('../public/design.html', import.meta.url), 'utf8');
  assert.doesNotMatch(html, /\son[a-z]+=|style=|<script>(?!<)/);
  assert.match(html, /<script src="\/ui\.js"><\/script>/);
  for (const p of ['/design.html', '/design.js']) assert.equal(await (await worker.fetch(new Request('https://edf6.test' + p), e)).text(), 'asset');
});
