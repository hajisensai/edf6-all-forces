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

test('outline: a group shares one outline; saves compare-and-set on the version', async () => {
  const e = env();
  const empty = await get(e);
  assert.equal(empty.outline, null);
  assert.equal(empty.version, 0);
  assert.equal((await call(e, '/api/outline/save', { outline: sample(), version: 0 })).status, 200);
  const seen = await get(e, 'amy');
  assert.equal(seen.version, 1);
  assert.equal(seen.updated_by, 'alice');
  assert.deepEqual(seen.outline.lanes[1].after, ['l56']);   // unknown and self references dropped
  const edited = seen.outline;
  edited.lanes[0].steps.push({ id: 's2', title: '第二次轮回', tag: '', body: '' });
  assert.equal((await call(e, '/api/outline/save', { outline: edited, version: 1 }, 'amy')).status, 200);
  // alice still edits version 1: refused, nothing overwritten
  assert.equal((await call(e, '/api/outline/save', { outline: sample(), version: 1 })).status, 409);
  assert.equal((await get(e)).outline.lanes[0].steps.length, 2);
  assert.equal((await call(e, '/api/outline/save', { outline: sample(), version: 0 })).status, 409);
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
  assert.deepEqual((await get(e, 'dev')).groups, ['', 'a', 'b']);   // a developer sees every group, empty or not
  assert.equal((await call(e, '/api/outline/save', { outline: sample(), version: 1, group: 'b' }, 'dev')).status, 200);
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
    headers: { 'content-type': 'application/json' }, body: JSON.stringify({ outline: sample(), version: 1, group: 'a' }) }), e);
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
  assert.equal((await call(e, '/api/outline/save', { outline: edited, version: 1 })).status, 200);
  assert.equal(e.DB.raw.prepare("SELECT COUNT(*) AS n FROM outline_ratings WHERE item = 's1'").get().n, 0);
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
  assert.equal((await call(e, '/api/outline/save', { outline: sample(), version: 1 })).status, 429);
});

test('outline migrations are repeatable', () => {
  const e = env();
  for (const file of ['0003-story-outline.sql', '0004-outline-ratings.sql']) {
    const sql = readFileSync(new URL('../migrations/' + file, import.meta.url), 'utf8');
    e.DB.raw.exec(sql);
    e.DB.raw.exec(sql);
  }
});
