// Story outline (/outline.html): each group keeps one outline of its story — mission lines (5/6, 3/4/4.1, a merged
// line…), each an ordered list of steps, and which lines a line follows (drawn as lines merging). Unlike the story
// list it is one document, saved whole: every save names the version it was edited from, so two people editing at
// once cannot silently overwrite each other (the later save gets a 409 and reloads).
// Players' text: cleaned like the plan (clean()), rendered as text only (public/outline.js), rate limited per account.
// Reading is open to everyone, visitors without an account included; writing stays with the group's members and developers.
// A group with nothing written yet is listed only to those who can write it.
// Every line and step can be scored (1..5) and reviewed by any account, once each (a new score replaces its old one);
// visitors see only each item's average and how many scored it, not the reviews or who saved the outline.
import { GROUP } from './auth.js';
import { Plan, canWrite, clean, pick, readable, scope } from './plan.js';
import { HttpError, body } from './http.js';

export const TONES = ['edf', 'hazard', 'violet'];
export const OUTLINE_LIMIT = { lanes: 12, steps: 100, title: 60, name: 40, note: 1000, stepTitle: 80, tag: 20, body: 4000, review: 1000 };
const ID = /^[A-Za-z0-9_-]{1,24}$/;

// The outline as stored: only known fields, cleaned and bounded; bad shapes are a 400, not silently dropped.
export function normalize(raw) {
  const L = OUTLINE_LIMIT;
  if (!raw || typeof raw !== 'object' || !Array.isArray(raw.lanes)) throw new HttpError(400, '大纲格式不对');
  if (raw.lanes.length > L.lanes) throw new HttpError(400, `任务线最多 ${L.lanes} 条`);
  const seen = new Set();
  const id = (v) => {
    const s = String(v ?? '');
    if (!ID.test(s) || seen.has(s)) throw new HttpError(400, '大纲里的编号重复或无效，请刷新后重试');
    seen.add(s);
    return s;
  };
  const lanes = raw.lanes.map((l) => {
    if (!l || typeof l !== 'object' || !Array.isArray(l.steps)) throw new HttpError(400, '大纲格式不对');
    if (l.steps.length > L.steps) throw new HttpError(400, `每条任务线最多 ${L.steps} 步`);
    return {
      id: id(l.id),
      name: clean(l.name, L.name) || '（未命名）',
      note: clean(l.note, L.note),
      tone: TONES.includes(l.tone) ? l.tone : 'edf',
      after: Array.isArray(l.after) ? l.after.map(String).slice(0, L.lanes) : [],
      steps: l.steps.map((s) => {
        if (!s || typeof s !== 'object') throw new HttpError(400, '大纲格式不对');
        return { id: id(s.id), title: clean(s.title, L.stepTitle) || '（未命名）', tag: clean(s.tag, L.tag), body: clean(s.body, L.body) };
      }),
    };
  });
  const laneIds = new Set(lanes.map((l) => l.id));
  for (const l of lanes) l.after = [...new Set(l.after)].filter((a) => a !== l.id && laneIds.has(a));
  return { title: clean(raw.title, L.title), lanes };
}

// Every lane's and step's id in a stored outline.
function items(outline) {
  return new Set(outline ? outline.lanes.flatMap((l) => [l.id, ...l.steps.map((st) => st.id)]) : []);
}

// The group's scores: per item { avg, n }; with an account also the reviews (who, score, text, when).
async function ratings(db, me, group, outline) {
  const live = items(outline);
  const rows = (await db.prepare('SELECT item, author, score, body, at FROM outline_ratings WHERE grp = ?1 ORDER BY at').bind(group).all())
    .results.filter((r) => live.has(r.item));
  const sums = {};
  for (const r of rows) {
    const x = (sums[r.item] ||= { sum: 0, n: 0 });
    x.sum += r.score;
    x.n += 1;
  }
  const scores = Object.fromEntries(Object.entries(sums).map(([k, x]) => [k, { avg: Math.round((x.sum / x.n) * 10) / 10, n: x.n }]));
  if (!me) return { scores };
  const reviews = {};
  for (const { item, ...r } of rows) (reviews[item] ||= []).push(r);
  return { scores, reviews };
}

const WRITES = new Set(['/api/outline/save', '/api/outline/rate', '/api/outline/unrate']);

export async function outlineRoute(request, env, me, missions, groups, path, url) {
  const db = env.DB;
  if (path === '/api/outline' && request.method === 'GET') {
    // me = null: a visitor without an account.
    const shown = await readable(db, me, groups, "SELECT grp FROM story_outline WHERE json_array_length(data, '$.lanes') > 0");
    const group = pick(me, url.searchParams.get('group'), shown);
    const row = group == null ? null : await db.prepare('SELECT * FROM story_outline WHERE grp = ?1').bind(group).first();
    const outline = row ? JSON.parse(row.data) : null;
    return {
      me: me && { user: me.user, role: me.role, group: me.group }, group, groups: shown, editable: group != null && canWrite(me, group),
      outline, version: row ? row.version : 0,
      updated_by: me ? row?.updated_by ?? null : null, updated_at: me ? row?.updated_at ?? null : null,
      ...(group == null ? { scores: {} } : await ratings(db, me, group, outline)),
    };
  }
  if (!WRITES.has(path) || request.method !== 'POST') return null;
  const b = await body(request);
  if (!b || typeof b !== 'object' || Array.isArray(b)) throw new HttpError(400, 'JSON object expected');
  const plan = new Plan(env, me, missions);
  if (path !== '/api/outline/save') {
    // Any account rates any group's outline: the item must be in what that group has saved.
    const group = b.group == null ? '' : String(b.group);
    if (!GROUP.test(group)) throw new HttpError(400, 'bad group');
    const row = await db.prepare('SELECT data FROM story_outline WHERE grp = ?1').bind(group).first();
    const outline = row && JSON.parse(row.data);
    const item = String(b.item ?? '');
    if (!items(outline).has(item)) throw new HttpError(404, '大纲里没有这一项（可能刚被改过或还没保存），请刷新');
    if (path === '/api/outline/rate') {
      const score = b.score;
      if (!Number.isInteger(score) || score < 1 || score > 5) throw new HttpError(400, '评分是 1 到 5 的整数');
      await plan.charge();
      await db.prepare(`INSERT INTO outline_ratings (grp, item, author, score, body, at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)
        ON CONFLICT(grp, item, author) DO UPDATE SET score = ?4, body = ?5, at = ?6`)
        .bind(group, item, me.user, score, clean(b.body, OUTLINE_LIMIT.review), Date.now()).run();
    } else {
      await plan.charge();
      await db.prepare('DELETE FROM outline_ratings WHERE grp = ?1 AND item = ?2 AND author = ?3').bind(group, item, me.user).run();
    }
    return ratings(db, me, group, outline);
  }
  const group = scope(me, b.group);
  const outline = normalize(b.outline);
  const version = Number.isSafeInteger(b.version) && b.version >= 0 ? b.version : null;
  if (version == null) throw new HttpError(400, '缺少版本号，请刷新后重试');
  await plan.charge();
  const now = Date.now();
  const data = JSON.stringify(outline);
  // Compare-and-set on the version the editor loaded: 0 = nobody has saved this group's outline yet.
  const saved = version === 0
    ? await db.prepare(`INSERT INTO story_outline (grp, data, version, updated_by, updated_at) VALUES (?1, ?2, 1, ?3, ?4)
        ON CONFLICT(grp) DO NOTHING RETURNING version`).bind(group, data, me.user, now).first()
    : await db.prepare(`UPDATE story_outline SET data = ?2, version = version + 1, updated_by = ?3, updated_at = ?4
        WHERE grp = ?1 AND version = ?5 RETURNING version`).bind(group, data, me.user, now, version).first();
  if (!saved) throw new HttpError(409, '别人刚保存过这份大纲，你的改动没有存上：刷新看看最新版再改');
  // A deleted line or step takes its scores with it.
  await db.prepare('DELETE FROM outline_ratings WHERE grp = ?1 AND item NOT IN (SELECT value FROM json_each(?2))')
    .bind(group, JSON.stringify([...items(outline)])).run();
  return { version: saved.version, outline };
}
