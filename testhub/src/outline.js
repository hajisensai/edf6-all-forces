// Story outline (/outline.html): each group keeps one outline of its story — mission lines (5/6, 3/4/4.1, a merged
// line…), each an ordered list of steps, and which lines a line follows (drawn as lines merging). Unlike the story
// list it is one document, saved whole: every save names the version it was edited from, so two people editing at
// once cannot silently overwrite each other (the later save gets a 409 and reloads).
// Players' text: cleaned like the plan (clean()), rendered as text only (public/outline.js), rate limited per account.
import { Plan, clean, scope } from './plan.js';
import { HttpError, body } from './http.js';

export const TONES = ['edf', 'hazard', 'violet'];
export const OUTLINE_LIMIT = { lanes: 12, steps: 100, title: 60, name: 40, note: 1000, stepTitle: 80, tag: 20, body: 4000 };
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

export async function outlineRoute(request, env, me, missions, groups, path, url) {
  const db = env.DB;
  if (path === '/api/outline' && request.method === 'GET') {
    const group = scope(me, url.searchParams.get('group'));
    const row = await db.prepare('SELECT * FROM story_outline WHERE grp = ?1').bind(group).first();
    return {
      me: { user: me.user, role: me.role, group: me.group }, group, groups: me.role === 'dev' ? groups : [group],
      outline: row ? JSON.parse(row.data) : null, version: row ? row.version : 0,
      updated_by: row?.updated_by ?? null, updated_at: row?.updated_at ?? null,
    };
  }
  if (path !== '/api/outline/save' || request.method !== 'POST') return null;
  const b = await body(request);
  if (!b || typeof b !== 'object' || Array.isArray(b)) throw new HttpError(400, 'JSON object expected');
  const group = scope(me, b.group);
  const outline = normalize(b.outline);
  const version = Number.isSafeInteger(b.version) && b.version >= 0 ? b.version : null;
  if (version == null) throw new HttpError(400, '缺少版本号，请刷新后重试');
  await new Plan(env, me, missions).charge();
  const now = Date.now();
  const data = JSON.stringify(outline);
  // Compare-and-set on the version the editor loaded: 0 = nobody has saved this group's outline yet.
  const saved = version === 0
    ? await db.prepare(`INSERT INTO story_outline (grp, data, version, updated_by, updated_at) VALUES (?1, ?2, 1, ?3, ?4)
        ON CONFLICT(grp) DO NOTHING RETURNING version`).bind(group, data, me.user, now).first()
    : await db.prepare(`UPDATE story_outline SET data = ?2, version = version + 1, updated_by = ?3, updated_at = ?4
        WHERE grp = ?1 AND version = ?5 RETURNING version`).bind(group, data, me.user, now, version).first();
  if (!saved) throw new HttpError(409, '别人刚保存过这份大纲，你的改动没有存上：刷新看看最新版再改');
  return { version: saved.version, outline };
}
