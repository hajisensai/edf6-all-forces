// External review of the story outlines (/rate.html): people with no account score (1..5) and comment on outlines
// shown to them in a random order, in Chinese, English or Japanese.
// What they read is a snapshot: a developer publishes chosen outlines at a saved version together with their
// English and Japanese translations (POST /api/review/publish; tools/testhub.py review-publish), so everyone scores the
// same text and a later edit to the outline does not change what was scored. Publishing again replaces the shown set;
// an outline published at a new version is a new item (its old scores stay with the old version in the results).
// Shown blind: no group, author or editor. A reviewer is a random token their browser keeps (only its SHA-256 is
// stored): one score + comment per reviewer and outline, which they can change or take back. The average and the
// other reviewers' comments show to a reviewer only after they have scored that outline themselves.
// Visitors' writes are rate limited per IP (only a keyed hash of it is stored); the text is cleaned like the plan's
// and the page puts it into text nodes only.
import { clean } from './plan.js';
import { normalize, OUTLINE_LIMIT } from './outline.js';
import { GROUP } from './auth.js';
import { HttpError, body } from './http.js';

export const LANGS = ['zh', 'en', 'ja'];
export const REVIEW_LIMIT = { name: 30, body: 2000, items: 50, perItem: 5000 };
export const REVIEW_RATE = [[10 * 60e3, 30], [864e5, 200]];   // [window ms, writes] per IP
const TOKEN = /^[0-9a-f]{32}$/;

async function sha256(text) {
  const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// The reviewer's id from their token (absent = a reader who has not reviewed anything).
async function rater(token, required) {
  if (token == null || token === '') {
    if (required) throw new HttpError(400, 'missing reviewer token');
    return null;
  }
  if (typeof token !== 'string' || !TOKEN.test(token)) throw new HttpError(400, 'bad reviewer token');
  return sha256('review|' + token);
}

// Every lane and step id of an outline, in order: a translation must have exactly these.
const shape = (o) => JSON.stringify(o.lanes.map((l) => [l.id, l.tone, l.after, l.steps.map((s) => [s.id, s.tone ?? ''])]));

async function charge(db, env, request) {
  const ip = request.headers.get('cf-connecting-ip') || 'unknown';
  const who = await sha256('ip|' + env.SESSION_SECRET + '|' + ip);
  const now = Date.now();
  const [[w1, m1], [w2, m2]] = REVIEW_RATE;
  const r = await db.prepare(
    `INSERT INTO review_log (who, at) SELECT ?1, ?2
     WHERE (SELECT COUNT(*) FROM review_log WHERE who = ?1 AND at > ?3) < ?4
       AND (SELECT COUNT(*) FROM review_log WHERE who = ?1 AND at > ?5) < ?6`,
  ).bind(who, now, now - w1, m1, now - w2, m2).run();
  if (!r.meta.changes) throw new HttpError(429, 'too many requests, try again later');
  await db.prepare('DELETE FROM review_log WHERE at < ?1').bind(now - 2 * 864e5).run();
}

// The shown set for one reviewer: each item's three texts; their own entry; and, for the items they scored,
// the average and everyone's comments (score, name, text, language, when — never who).
async function shown(db, me) {
  const items = (await db.prepare('SELECT id, data FROM review_items WHERE active = 1 ORDER BY id').all()).results
    .map((r) => ({ id: r.id, ...JSON.parse(r.data) }));
  const mine = {};
  if (me) {
    for (const r of (await db.prepare(`SELECT s.item, s.score, s.name, s.body, s.at FROM review_scores s JOIN review_items i ON i.id = s.item
      WHERE i.active = 1 AND s.rater = ?1`).bind(me).all()).results) mine[r.item] = { score: r.score, name: r.name, body: r.body, at: r.at };
  }
  const open = Object.keys(mine).map(Number);
  const stats = {};
  const comments = {};
  if (open.length) {
    const list = JSON.stringify(open);
    for (const r of (await db.prepare(`SELECT item, COUNT(score) AS n, AVG(score) AS avg FROM review_scores
      WHERE item IN (SELECT value FROM json_each(?1)) GROUP BY item`).bind(list).all()).results) {
      stats[r.item] = { n: r.n, avg: r.avg == null ? null : Math.round(r.avg * 10) / 10 };
    }
    for (const r of (await db.prepare(`SELECT item, score, name, body, lang, at, rater = ?2 AS own FROM review_scores
      WHERE item IN (SELECT value FROM json_each(?1)) AND body != '' ORDER BY at DESC`).bind(list, me).all()).results) {
      (comments[r.item] ||= []).push({ score: r.score, name: r.name, body: r.body, lang: r.lang, at: r.at, own: !!r.own });
    }
  }
  return { langs: LANGS, items, mine, stats, comments };
}

// The developer's view: every item ever published (shown or not), its scores per language and all comments.
async function results(db) {
  const items = (await db.prepare('SELECT id, grp, book, version, data, active, published_at FROM review_items ORDER BY id').all()).results;
  const scores = (await db.prepare('SELECT item, score, name, body, lang, at, substr(rater, 1, 8) AS rater FROM review_scores ORDER BY at').all()).results;
  return {
    items: items.map((r) => {
      const mine = scores.filter((s) => s.item === r.id);
      const scored = mine.filter((s) => s.score != null);
      const avg = (list) => (list.length ? Math.round((list.reduce((a, s) => a + s.score, 0) / list.length) * 100) / 100 : null);
      return {
        id: r.id, group: r.grp, book: r.book, version: r.version, active: !!r.active, published_at: r.published_at,
        title: JSON.parse(r.data).zh.title, n: scored.length, avg: avg(scored),
        by_lang: Object.fromEntries(LANGS.map((l) => { const x = scored.filter((s) => s.lang === l); return [l, { n: x.length, avg: avg(x) }]; })),
        entries: mine.map(({ item, ...s }) => s),
      };
    }),
  };
}

// The current saved outlines of every group (what a developer translates before publishing).
async function source(db) {
  return { outlines: (await db.prepare(`SELECT grp, book, version, data FROM story_outlines WHERE json_array_length(data, '$.lanes') > 0
    ORDER BY grp, book`).all()).results.map((r) => ({ group: r.grp, book: r.book, version: r.version, outline: JSON.parse(r.data) })) };
}

// Replace the shown set. Each item names a saved outline at the version it was translated from; the Chinese text is
// taken from that saved outline (zh_title may replace its title), en / ja must have the same lines and steps.
async function publish(db, b) {
  if (!Array.isArray(b.items) || b.items.length > REVIEW_LIMIT.items) throw new HttpError(400, `items: a list of at most ${REVIEW_LIMIT.items}`);
  const now = Date.now();
  const keep = [];
  const writes = [];
  for (const it of b.items) {
    if (!it || typeof it !== 'object') throw new HttpError(400, 'bad item');
    const group = String(it.group ?? '');
    if (!GROUP.test(group) || !Number.isSafeInteger(it.book) || !Number.isSafeInteger(it.version)) throw new HttpError(400, 'bad item');
    const row = await db.prepare('SELECT version, data FROM story_outlines WHERE grp = ?1 AND book = ?2').bind(group, it.book).first();
    if (!row) throw new HttpError(404, `no outline ${group || '(ungrouped)'}/${it.book}`);
    if (row.version !== it.version) throw new HttpError(409, `outline ${group || '(ungrouped)'}/${it.book} is at version ${row.version}, the translation is of ${it.version}`);
    const zh = JSON.parse(row.data);
    if (it.zh_title != null) zh.title = clean(it.zh_title, OUTLINE_LIMIT.title);
    const data = { zh };
    for (const lang of ['en', 'ja']) {
      const t = normalize(it[lang]);
      if (shape(t) !== shape(zh)) throw new HttpError(400, `${lang} of ${group || '(ungrouped)'}/${it.book}: lines and steps differ from the outline`);
      data[lang] = t;
    }
    writes.push(db.prepare(`INSERT INTO review_items (grp, book, version, data, active, published_at) VALUES (?1, ?2, ?3, ?4, 1, ?5)
      ON CONFLICT(grp, book, version) DO UPDATE SET data = ?4, active = 1`).bind(group, it.book, it.version, JSON.stringify(data), now));
    keep.push([group, it.book, it.version]);
  }
  writes.push(db.prepare(`UPDATE review_items SET active = 0 WHERE NOT EXISTS (SELECT 1 FROM json_each(?1) k
    WHERE json_extract(k.value, '$[0]') = grp AND json_extract(k.value, '$[1]') = book AND json_extract(k.value, '$[2]') = version)`)
    .bind(JSON.stringify(keep)));
  await db.batch(writes);
  return { shown: (await db.prepare('SELECT id, grp AS "group", book, version FROM review_items WHERE active = 1 ORDER BY id').all()).results };
}

// GET /api/review, POST /api/review/rate|unrate: anyone. me = the account, if any (developers get the rest).
export async function reviewRoute(request, env, me, path) {
  const db = env.DB;
  const dev = me?.role === 'dev';
  if (path === '/api/review' && request.method === 'GET') return shown(db, await rater(request.headers.get('x-review-token'), false));
  if (path === '/api/review/results' && request.method === 'GET') { if (!dev) throw new HttpError(403, 'developer only'); return results(db); }
  if (path === '/api/review/source' && request.method === 'GET') { if (!dev) throw new HttpError(403, 'developer only'); return source(db); }
  if (request.method !== 'POST') return null;
  if (path === '/api/review/publish') {
    if (!dev) throw new HttpError(403, 'developer only');
    const b = await body(request);
    return publish(db, b || {});
  }
  if (path !== '/api/review/rate' && path !== '/api/review/unrate') return null;
  const b = await body(request);
  if (!b || typeof b !== 'object' || Array.isArray(b)) throw new HttpError(400, 'JSON object expected');
  const who = await rater(b.token, true);
  const item = await db.prepare('SELECT id FROM review_items WHERE id = ?1 AND active = 1').bind(Number.isSafeInteger(b.item) ? b.item : -1).first();
  if (!item) throw new HttpError(404, 'this outline is no longer shown, reload the page');
  await charge(db, env, request);
  if (path === '/api/review/unrate') {
    await db.prepare('DELETE FROM review_scores WHERE item = ?1 AND rater = ?2').bind(item.id, who).run();
    return shown(db, who);
  }
  const score = b.score == null ? null : b.score;
  if (score !== null && (!Number.isInteger(score) || score < 1 || score > 5)) throw new HttpError(400, 'score: 1 to 5');
  const text = clean(b.body, REVIEW_LIMIT.body);
  if (score === null && !text) throw new HttpError(400, 'give a score or a comment');
  const lang = LANGS.includes(b.lang) ? b.lang : 'zh';
  const saved = await db.prepare(`INSERT INTO review_scores (item, rater, score, name, body, lang, at)
    SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?7
    WHERE EXISTS (SELECT 1 FROM review_scores WHERE item = ?1 AND rater = ?2) OR (SELECT COUNT(*) FROM review_scores WHERE item = ?1) < ?8
    ON CONFLICT(item, rater) DO UPDATE SET score = ?3, name = ?4, body = ?5, lang = ?6, at = ?7`)
    .bind(item.id, who, score, clean(b.name, REVIEW_LIMIT.name), text, lang, Date.now(), REVIEW_LIMIT.perItem).run();
  if (!saved.meta.changes) throw new HttpError(409, 'this outline has had too many reviews');
  return shown(db, who);
}
