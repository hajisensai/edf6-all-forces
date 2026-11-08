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
const get = async (e, user = 'alice', group) => (await call(e, '/api/outline' + (group == null ? '' : '?group=' + group), undefined, user)).json();
const visit = (e, query = '') => worker.fetch(new Request('https://edf6.test/api/outline' + query), e);
const sample = () => ({ title: '轮回剧情大纲', lanes: [
  { id: 'l56', name: '5、6代任务线', tone: 'edf', note: '', after: [], steps: [{ id: 's1', title: '第一次轮回', tag: '轮回 1', body: '一块把传送门炸了' }] },
  { id: 'lmix', name: '融合线', tone: 'violet', note: '', after: ['l56', 'nope', 'lmix'], steps: [] },
] });

test('outline: a group shares its outlines; saves compare-and-set on the version', async () => {
  const e = env();
  const empty = await get(e);
  assert.equal(empty.outline, null);
  assert.equal(empty.version, 0);
  assert.deepEqual([empty.book, empty.books], [0, []]);
  const first = await (await call(e, '/api/outline/save', { outline: sample(), version: 0 })).json();
  assert.deepEqual([first.book, first.version], [1, 1]);
  const seen = await get(e, 'amy');
  assert.equal(seen.version, 1);
  assert.equal(seen.updated_by, 'alice');
  assert.deepEqual(seen.outline.lanes[1].after, ['l56']);   // unknown and self references dropped
  const edited = seen.outline;
  edited.lanes[0].steps.push({ id: 's2', title: '第二次轮回', tag: '', body: '' });
  assert.equal((await call(e, '/api/outline/save', { book: 1, outline: edited, version: 1 }, 'amy')).status, 200);
  // alice still edits version 1: refused, nothing overwritten
  assert.equal((await call(e, '/api/outline/save', { book: 1, outline: sample(), version: 1 })).status, 409);
  assert.equal((await get(e)).outline.lanes[0].steps.length, 2);
  assert.equal((await call(e, '/api/outline/save', { book: 1, outline: sample(), version: 0 })).status, 409);
  assert.equal((await call(e, '/api/outline/save', { book: 9, outline: sample(), version: 1 })).status, 409);   // no such outline
  assert.equal((await call(e, '/api/outline/save', { outline: sample(), version: 3 })).status, 400);   // a new one starts at 0
});

test('outline: everyone reads every written group, only members and developers write it', async () => {
  const e = env();
  await call(e, '/api/outline/save', { outline: sample(), version: 0 });
  const own = await get(e, 'bob');
  assert.equal(own.group, 'b');
  assert.equal(own.outline, null);
  assert.equal(own.editable, true);
  assert.deepEqual(own.groups, ['a', 'b']);   // its own empty group, plus the written one
  const other = await get(e, 'bob', 'a');
  assert.equal(other.outline.lanes.length, 2);
  assert.equal(other.editable, false);
  // a save always lands in the tester's own group, never the one it was reading
  assert.equal((await call(e, '/api/outline/save', { outline: { title: '', lanes: [] }, version: 0, group: 'a' }, 'bob')).status, 200);
  assert.equal((await get(e, 'alice')).version, 1);
  assert.equal((await get(e, 'dev', 'a')).editable, true);
  assert.deepEqual((await get(e, 'dev')).groups, ['', 'a']);   // its own (empty) group plus the written one: b is empty
  assert.equal((await get(e, 'dev', 'b')).group, 'b');   // an unlisted empty group still opens by name for a developer
  assert.equal((await get(e, 'alice', 'b')).group, 'a');   // but not for another group's tester
  assert.equal((await call(e, '/api/outline/save', { book: 1, outline: sample(), version: 1, group: 'b' }, 'dev')).status, 200);
  assert.equal((await get(e, 'bob')).version, 2);
});

test('outline: visitors without an account read written groups only', async () => {
  const e = env();
  let v = await (await visit(e)).json();
  assert.deepEqual([v.me, v.group, v.groups, v.outline, v.editable], [null, null, [], null, false]);
  await call(e, '/api/outline/save', { outline: sample(), version: 0 });
  await call(e, '/api/outline/save', { outline: { title: '', lanes: [] }, version: 0 }, 'bob');   // saved, but empty
  v = await (await visit(e)).json();
  assert.deepEqual(v.groups, ['a']);
  assert.equal(v.group, 'a');
  assert.equal(v.outline.lanes.length, 2);
  assert.equal(v.editable, false);
  assert.equal((await (await visit(e, '?group=b')).json()).group, 'a');   // an empty group is not shown, even when asked for
  assert.equal((await (await visit(e, '?group=nobody')).json()).group, 'a');
  assert.equal(v.updated_by, null);   // who saved it, and when, only with an account
  assert.equal(v.updated_at, null);
  assert.equal((await get(e, 'bob', 'a')).updated_by, 'alice');
  assert.equal((await visit(e, '?group=%3Cx%3E')).status, 400);
  assert.deepEqual((await get(e, 'alice')).groups, ['a']);   // bob's empty group is hidden from other groups too
  // writing still needs an account; wrong credentials are refused, not treated as a visitor
  const save = await worker.fetch(new Request('https://edf6.test/api/outline/save', { method: 'POST',
    headers: { 'content-type': 'application/json' }, body: JSON.stringify({ book: 1, outline: sample(), version: 1, group: 'a' }) }), e);
  assert.equal(save.status, 401);
  const wrong = await worker.fetch(new Request('https://edf6.test/api/outline', { headers: { authorization: 'Basic ' + btoa('alice:nope') } }), e);
  assert.equal(wrong.status, 401);
  assert.equal((await worker.fetch(new Request('https://edf6.test/outline.html'), e)).status, 200);
  assert.equal((await worker.fetch(new Request('https://edf6.test/index.html'), e)).status, 303);   // the test pages stay behind the login
});

test('outline: any account scores and reviews lines and steps; visitors see only the average and count', async () => {
  const e = env();
  await call(e, '/api/outline/save', { outline: sample(), version: 0 });
  const rate = (user, data) => call(e, '/api/outline/rate', { group: 'a', ...data }, user);
  assert.equal((await rate('alice', { item: 's1', score: 5, body: '好' })).status, 200);
  assert.equal((await rate('bob', { item: 's1', score: 2, body: 'x\u202Ey' })).status, 200);   // another group may rate too
  const back = await (await rate('bob', { item: 'lmix', score: 4 })).json();
  assert.deepEqual(back.scores.lmix, { avg: 4, n: 1 });
  assert.equal((await rate('bob', { item: 's1', score: 3, body: '改主意了' })).status, 200);   // replaces bob's own
  for (const bad of [{ item: 's1', score: 0 }, { item: 's1', score: 6 }, { item: 's1', score: 2.5 }, { item: 's1', score: '4' }]) {
    assert.equal((await rate('bob', bad)).status, 400, JSON.stringify(bad));
  }
  assert.equal((await rate('bob', { item: 'nope', score: 3 })).status, 404);
  assert.equal((await call(e, '/api/outline/rate', { group: 'b', item: 's1', score: 3 }, 'bob')).status, 404);
  const seen = await get(e, 'amy');
  assert.deepEqual(seen.scores.s1, { avg: 4, n: 2 });
  assert.deepEqual(seen.reviews.s1.map((r) => [r.author, r.score, r.body]), [['alice', 5, '好'], ['bob', 3, '改主意了']]);
  const v = await (await visit(e)).json();
  assert.deepEqual(v.scores, { s1: { avg: 4, n: 2 }, lmix: { avg: 4, n: 1 } });
  assert.equal(v.reviews, undefined);
  const anon = await worker.fetch(new Request('https://edf6.test/api/outline/rate', { method: 'POST',
    headers: { 'content-type': 'application/json' }, body: JSON.stringify({ group: 'a', item: 's1', score: 5 }) }), e);
  assert.equal(anon.status, 401);
  assert.equal((await call(e, '/api/outline/unrate', { group: 'a', item: 's1' }, 'bob')).status, 200);
  assert.deepEqual((await get(e)).scores.s1, { avg: 5, n: 1 });
  // a step deleted from the outline takes its scores with it
  const edited = (await get(e)).outline;
  edited.lanes[0].steps = [];
  assert.equal((await call(e, '/api/outline/save', { book: 1, outline: edited, version: 1 })).status, 200);
  assert.equal(e.DB.raw.prepare("SELECT COUNT(*) AS n FROM outline_ratings WHERE item = '1/s1'").get().n, 0);
  assert.deepEqual((await get(e)).scores, { lmix: { avg: 4, n: 1 } });
});

test('outline: validation, clean text, limits and rate', async () => {
  const e = env();
  for (const bad of [null, {}, { lanes: 'x' }, { lanes: [{ id: 'a', steps: 'x' }] }, { lanes: [{ id: 'bad id', steps: [] }] },
    { lanes: [{ id: 'a', steps: [{ id: 'a' }] }] }, { lanes: Array.from({ length: 13 }, (_, i) => ({ id: 'l' + i, steps: [] })) }]) {
    assert.equal((await call(e, '/api/outline/save', { outline: bad, version: 0 })).status, 400, JSON.stringify(bad));
  }
  assert.equal((await call(e, '/api/outline/save', { outline: sample() })).status, 400);
  const dirty = sample();
  dirty.lanes[0].name = 'a\u202Eb <img src=x>';
  dirty.lanes[0].tone = 'red';
  dirty.lanes[0].steps[0].body = 'x\u0007y\u200Bz';
  dirty.lanes[0].extra = 'dropped';
  assert.equal((await call(e, '/api/outline/save', { outline: dirty, version: 0 })).status, 200);
  const lane = (await get(e)).outline.lanes[0];
  assert.equal(lane.name, 'ab <img src=x>');
  assert.equal(lane.tone, 'edf');
  assert.equal(lane.steps[0].body, 'xyz');
  assert.equal(lane.extra, undefined);
  const [[, perWindow]] = RATE;
  e.DB.raw.prepare('DELETE FROM write_log').run();
  for (let i = 0; i < perWindow; i++) e.DB.raw.prepare('INSERT INTO write_log (author, at) VALUES (?, ?)').run('alice', Date.now());
  assert.equal((await call(e, '/api/outline/save', { book: 1, outline: sample(), version: 1 })).status, 429);
});

test('migration 0006 adds the outline author, taken from who saved it last', () => {
  const e = env();
  const db = e.DB.raw;
  db.exec('DROP TABLE story_outlines');
  db.exec(readFileSync(new URL('../migrations/0005-outline-books.sql', import.meta.url), 'utf8'));
  db.prepare("INSERT INTO story_outlines (grp, book, data, version, updated_by, updated_at) VALUES ('a', 1, '{}', 1, 'amy', 1)").run();
  db.exec(readFileSync(new URL('../migrations/0006-outline-author.sql', import.meta.url), 'utf8'));
  assert.equal(db.prepare('SELECT author FROM story_outlines').get().author, 'amy');
});

test('outline migrations are repeatable', () => {
  const e = env();
  for (const file of ['0003-story-outline.sql', '0004-outline-ratings.sql', '0005-outline-books.sql']) {
    const sql = readFileSync(new URL('../migrations/' + file, import.meta.url), 'utf8');
    e.DB.raw.exec(sql);
    e.DB.raw.exec(sql);
  }
});

test('outline migration 0005 moves each group\'s outline and its scores into outline 1', async () => {
  const e = env();
  const db = e.DB.raw;
  db.exec('DROP TABLE story_outlines');
  db.prepare('INSERT INTO story_outline (grp, data, version, updated_by, updated_at) VALUES (?, ?, 4, ?, 1)').run('a', JSON.stringify(sample()), 'alice');
  db.prepare("INSERT INTO outline_ratings (grp, item, author, score, body, at) VALUES ('a', 's1', 'bob', 4, '', 1)").run();
  const sql = readFileSync(new URL('../migrations/0005-outline-books.sql', import.meta.url), 'utf8');
  db.exec(sql);
  db.exec(sql);
  db.exec(readFileSync(new URL('../migrations/0006-outline-author.sql', import.meta.url), 'utf8'));
  const seen = await get(e, 'bob', 'a');
  assert.deepEqual([seen.book, seen.version, seen.outline.title, seen.updated_by, seen.author], [1, 4, '轮回剧情大纲', 'alice', 'alice']);
  assert.deepEqual(seen.scores.s1, { avg: 4, n: 1 });
  assert.equal(db.prepare('SELECT COUNT(*) AS n FROM story_outline').get().n, 0);
});

test('outline: a group keeps several outlines, each saved, scored and deleted on its own', async () => {
  const e = env();
  const save = (data, user = 'alice') => call(e, '/api/outline/save', data, user);
  assert.equal((await save({ outline: sample(), version: 0 })).status, 200);
  const other = { title: '星导者复仇线', lanes: [{ id: 's1', name: '6代', steps: [] }] };   // same id as outline 1's step
  const second = await (await save({ outline: other, version: 0 }, 'amy')).json();
  assert.deepEqual([second.book, second.version], [2, 1]);
  assert.equal((await save({ outline: { title: '空的', lanes: [] }, version: 0 })).status, 200);   // outline 3, empty
  const list = await get(e, 'amy');
  assert.equal(list.book, 1);
  assert.deepEqual(list.books, [{ book: 1, title: '轮回剧情大纲', lanes: 2, author: 'alice' }, { book: 2, title: '星导者复仇线', lanes: 1, author: 'amy' },
    { book: 3, title: '空的', lanes: 0, author: 'alice' }]);
  assert.equal(list.author, 'alice');   // who started it, not who saved it last
  assert.deepEqual((await (await visit(e, '?group=a')).json()).books.map((b) => b.author), [null, null]);   // visitors see no authors
  assert.equal((await (await visit(e, '?group=a')).json()).author, null);
  const two = await (await call(e, '/api/outline?book=2', undefined, 'amy')).json();
  assert.deepEqual([two.book, two.outline.title, two.version], [2, '星导者复仇线', 1]);
  // other groups and visitors do not see an empty outline
  assert.deepEqual((await get(e, 'bob', 'a')).books.map((b) => b.book), [1, 2]);
  assert.deepEqual((await (await visit(e, '?group=a&book=3')).json()).book, 1);
  assert.equal((await visit(e, '?book=x')).status, 400);
  // scores stay with their own outline even when ids repeat
  const rate = (data) => call(e, '/api/outline/rate', { group: 'a', ...data }, 'bob');
  assert.equal((await rate({ book: 1, item: 's1', score: 5 })).status, 200);
  assert.equal((await rate({ book: 2, item: 's1', score: 1 })).status, 200);
  assert.equal((await rate({ book: 2, item: 'l56', score: 1 })).status, 404);   // a lane of outline 1, not 2
  assert.deepEqual((await get(e, 'amy')).scores.s1, { avg: 5, n: 1 });
  assert.deepEqual(two.scores, {});
  assert.deepEqual((await (await call(e, '/api/outline?book=2', undefined, 'amy')).json()).scores.s1, { avg: 1, n: 1 });
  // saving outline 2 keeps outline 1's scores
  assert.equal((await save({ book: 2, outline: { ...other, lanes: [] }, version: 1 })).status, 200);
  assert.deepEqual((await get(e, 'amy')).scores.s1, { avg: 5, n: 1 });
  // delete: members only, compare-and-set, takes its scores along
  const remove = (data, user = 'alice') => call(e, '/api/outline/remove', data, user);
  assert.equal((await remove({ book: 1, version: 1, group: 'a' }, 'bob')).status, 409);   // lands in bob's own group b, which has no outline 1
  assert.equal((await remove({ book: 1, version: 7 })).status, 409);
  assert.equal((await remove({ version: 1 })).status, 400);
  assert.equal((await remove({ book: 1, version: 1 })).status, 200);
  assert.equal(e.DB.raw.prepare("SELECT COUNT(*) AS n FROM outline_ratings WHERE item LIKE '1/%'").get().n, 0);
  assert.deepEqual((await get(e, 'amy')).books.map((b) => b.book), [2, 3]);
  // the limit
  for (let i = 0; i < 18; i++) await save({ outline: { title: '', lanes: [] }, version: 0 }, i % 2 ? 'alice' : 'amy');
  assert.equal((await save({ outline: { title: '', lanes: [] }, version: 0 })).status, 400);
});
