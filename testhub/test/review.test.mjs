import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import worker from '../src/worker.js';
import { d1 } from './d1.mjs';
import { REVIEW_RATE } from '../src/review.js';

function env() {
  return { ACCOUNTS: 'alice:pw:tester:a,dev:pw:dev', SESSION_SECRET: 'test', DB: d1(), ASSETS: { fetch: () => new Response('asset') } };
}
function call(e, path, data, { user, token, ip = '1.2.3.4', origin } = {}) {
  const headers = { 'content-type': 'application/json', 'cf-connecting-ip': ip };
  if (user) headers.authorization = 'Basic ' + btoa(user + ':pw');
  if (token) headers['x-review-token'] = token;
  if (origin) headers.origin = origin;
  return worker.fetch(new Request('https://edf6.test' + path, {
    method: data === undefined ? 'GET' : 'POST', headers, body: data === undefined ? undefined : JSON.stringify(data),
  }), e);
}
const TA = 'a'.repeat(32);
const TB = 'b'.repeat(32);
const zh = () => ({ title: '【待补充】轮回大纲', lanes: [
  { id: 'l1', name: '5、6代', tone: 'edf', note: '', after: [], steps: [{ id: 's1', title: '第一次轮回', tag: '轮回 1', body: '炸掉传送门' }] },
  { id: 'l2', name: '融合线', tone: 'violet', note: '', after: ['l1'], steps: [{ id: 's2', title: '汇合', tag: '', body: '', tone: 'hazard' }] },
] });
const tr = (word) => ({ title: word, lanes: [
  { id: 'l1', name: word + ' line', tone: 'edf', note: '', after: [], steps: [{ id: 's1', title: word + ' 1', tag: '', body: word }] },
  { id: 'l2', name: word + ' merge', tone: 'violet', note: '', after: ['l1'], steps: [{ id: 's2', title: word + ' 2', tag: '', body: '', tone: 'hazard' }] },
] });

async function setup() {
  const e = env();
  const saved = await (await call(e, '/api/outline/save', { outline: zh(), version: 0 }, { user: 'alice' })).json();
  assert.equal(saved.version, 1);
  const pub = await call(e, '/api/review/publish', { items: [{ group: 'a', book: 1, version: 1, zh_title: '轮回大纲', en: tr('Loop'), ja: tr('ループ') }] }, { user: 'dev' });
  assert.equal(pub.status, 200);
  const { shown } = await pub.json();
  return { e, id: shown[0].id };
}

test('review: a developer publishes a snapshot with translations; visitors read all three languages, blind', async () => {
  const { e, id } = await setup();
  const r = await call(e, '/api/review');
  assert.equal(r.status, 200);
  const out = await r.json();
  assert.equal(out.items.length, 1);
  const it = out.items[0];
  assert.equal(it.id, id);
  assert.deepEqual(Object.keys(it).sort(), ['en', 'id', 'ja', 'zh']);   // no group, book, author
  assert.equal(it.zh.title, '轮回大纲');
  assert.equal(it.en.lanes[0].steps[0].body, 'Loop');
  assert.equal(it.ja.title, 'ループ');
  // A later edit of the outline does not change what is shown.
  const edited = zh();
  edited.lanes[0].steps[0].body = '改过了';
  assert.equal((await call(e, '/api/outline/save', { book: 1, outline: edited, version: 1 }, { user: 'alice' })).status, 200);
  assert.equal((await (await call(e, '/api/review')).json()).items[0].zh.lanes[0].steps[0].body, '炸掉传送门');
});

test('review: publishing checks the version and that translations have the same lines and steps', async () => {
  const { e } = await setup();
  const pub = (items, user = 'dev') => call(e, '/api/review/publish', { items }, { user });
  assert.equal((await pub([{ group: 'a', book: 1, version: 1, en: tr('x'), ja: tr('y') }], 'alice')).status, 403);
  assert.equal((await call(e, '/api/review/publish', { items: [] })).status, 403);   // visitor
  assert.equal((await pub([{ group: 'a', book: 1, version: 7, en: tr('x'), ja: tr('y') }])).status, 409);
  assert.equal((await pub([{ group: 'a', book: 9, version: 1, en: tr('x'), ja: tr('y') }])).status, 404);
  const bad = tr('x');
  bad.lanes[1].steps.push({ id: 's3', title: 'extra', tag: '', body: '' });
  assert.equal((await pub([{ group: 'a', book: 1, version: 1, en: bad, ja: tr('y') }])).status, 400);
  const moved = tr('x');
  moved.lanes[1].after = [];
  assert.equal((await pub([{ group: 'a', book: 1, version: 1, en: tr('x'), ja: moved }])).status, 400);
  // An empty list takes everything down (results keep it).
  assert.equal((await pub([])).status, 200);
  assert.equal((await (await call(e, '/api/review')).json()).items.length, 0);
  const res = await (await call(e, '/api/review/results', undefined, { user: 'dev' })).json();
  assert.equal(res.items.length, 1);
  assert.equal(res.items[0].active, false);
  assert.equal((await call(e, '/api/review/results')).status, 403);
  assert.equal((await call(e, '/api/review/source', undefined, { user: 'alice' })).status, 403);
  const src = await (await call(e, '/api/review/source', undefined, { user: 'dev' })).json();
  assert.deepEqual(src.outlines.map((x) => [x.group, x.book, x.version]), [['a', 1, 1]]);
});

test('review: visitors score and comment once each, change and remove it; others show only after scoring', async () => {
  const { e, id } = await setup();
  const rate = (token, data, opts = {}) => call(e, '/api/review/rate', { token, item: id, ...data }, opts);
  assert.equal((await rate(TA, { score: 6 })).status, 400);
  assert.equal((await rate(TA, { score: null, body: '  ' })).status, 400);
  assert.equal((await rate('nope', { score: 3 })).status, 400);
  assert.equal((await call(e, '/api/review/rate', { item: id, score: 3 })).status, 400);   // no token
  assert.equal((await call(e, '/api/review/rate', { token: TA, item: id + 1, score: 3 })).status, 404);
  assert.equal((await rate(TA, { score: 4, name: 'Ann' + String.fromCharCode(0x202e), body: 'nice' + String.fromCharCode(0x200b) + ' one', lang: 'en' })).status, 200);
  // B has not scored: sees the outline but neither the average nor A's comment.
  const b0 = await (await call(e, '/api/review', undefined, { token: TB })).json();
  assert.deepEqual([b0.mine, b0.stats, b0.comments], [{}, {}, {}]);
  const b1 = await (await rate(TB, { score: 2, body: '', lang: 'ja' })).json();
  assert.deepEqual(b1.mine[id].score, 2);
  assert.deepEqual(b1.stats[id], { n: 2, avg: 3 });
  assert.equal(b1.comments[id].length, 1);   // B wrote no text
  assert.deepEqual([b1.comments[id][0].name, b1.comments[id][0].body, b1.comments[id][0].own, b1.comments[id][0].lang], ['Ann', 'nice one', false, 'en']);
  assert.ok(!('rater' in b1.comments[id][0]));
  // A changes theirs (a comment without a score is allowed), then B removes theirs.
  const a1 = await (await rate(TA, { score: null, body: 'changed my mind' })).json();
  assert.deepEqual(a1.stats[id], { n: 1, avg: 2 });
  assert.equal(a1.comments[id][0].own, true);
  const b2 = await (await call(e, '/api/review/unrate', { token: TB, item: id })).json();
  assert.deepEqual([b2.mine, b2.stats], [{}, {}]);
  const res = await (await call(e, '/api/review/results', undefined, { user: 'dev' })).json();
  assert.equal(res.items[0].entries.length, 1);
  assert.equal(res.items[0].entries[0].body, 'changed my mind');
  assert.equal(res.items[0].entries[0].rater.length, 8);
  // The token itself is never stored.
  assert.equal(e.DB.raw.prepare('SELECT COUNT(*) AS n FROM review_scores WHERE rater = ?').get(TA).n, 0);
});

test('review: visitor writes are rate limited per IP and refused cross-site', async () => {
  const { e, id } = await setup();
  assert.equal((await call(e, '/api/review/rate', { token: TA, item: id, score: 3 }, { origin: 'https://evil.test' })).status, 403);
  const [[, perWindow]] = REVIEW_RATE;
  for (let i = 0; i < perWindow; i++) assert.equal((await call(e, '/api/review/rate', { token: TA, item: id, score: 1 + (i % 5) })).status, 200);
  assert.equal((await call(e, '/api/review/rate', { token: TA, item: id, score: 3 })).status, 429);
  assert.equal((await call(e, '/api/review/rate', { token: TA, item: id, score: 3 }, { ip: '5.6.7.8' })).status, 200);
  assert.equal(e.DB.raw.prepare("SELECT COUNT(*) AS n FROM review_log WHERE who LIKE '%1.2.3.4%'").get().n, 0);
});

test('review: the page and its script are public; the page puts text in as text only', async () => {
  const e = env();
  for (const p of ['/rate.html', '/rate.js']) assert.equal(await (await worker.fetch(new Request('https://edf6.test' + p), e)).text(), 'asset');
  const js = readFileSync(new URL('../public/rate.js', import.meta.url), 'utf8');
  assert.doesNotMatch(js, /innerHTML|outerHTML|insertAdjacentHTML|document\.write/);
  const html = readFileSync(new URL('../public/rate.html', import.meta.url), 'utf8');
  assert.doesNotMatch(html, /<script>(?!<\/script>)|\son[a-z]+=/);
});
