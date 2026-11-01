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
const get = async (e, user = 'alice', group = '') => (await call(e, '/api/outline?group=' + group, undefined, user)).json();
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

test('outline: groups are isolated; developers pick the group', async () => {
  const e = env();
  await call(e, '/api/outline/save', { outline: sample(), version: 0 });
  assert.equal((await get(e, 'bob')).outline, null);
  assert.equal((await get(e, 'bob', 'a')).outline, null);   // a tester cannot look into another group
  assert.equal((await get(e, 'dev', 'a')).outline.lanes.length, 2);
  assert.equal((await call(e, '/api/outline/save', { outline: sample(), version: 0, group: 'b' }, 'dev')).status, 200);
  assert.equal((await get(e, 'bob')).version, 1);
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
  assert.equal((await worker.fetch(new Request('https://edf6.test/api/outline'), e)).status, 401);
});

test('outline migration is repeatable', () => {
  const e = env();
  const sql = readFileSync(new URL('../migrations/0003-story-outline.sql', import.meta.url), 'utf8');
  e.DB.raw.exec(sql);
  e.DB.raw.exec(sql);
});
