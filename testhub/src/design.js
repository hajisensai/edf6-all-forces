// The group's design list (/design.html, 设定集): the characters, vehicles, weapons, equipment, maps and monsters its
// story needs, each with how it looks, what it is and does, and references; linked to the outline steps it appears in
// (book + step ids of the same group's /outline.html) and the 关卡策划 missions that use it (story_missions ids).
// Reading is open to everyone (visitors do not see who wrote or changed an entry). Writing: the group's members and
// developers; when the group has an outline open to every account (src/outline.js `open`), any account adds and edits
// its entries too, though deleting stays with the group. Every edit names the version it was made from (409 when stale).
import { Plan, anonymous, canWrite, clean, int, pick, readable, scope } from './plan.js';
import { HttpError, body } from './http.js';
import { mainFirst, openGroup, openScope } from './outline.js';

export const KINDS = ['character', 'vehicle', 'weapon', 'gear', 'map', 'monster', 'other'];
export const DESIGN_STATUSES = ['todo', 'drafting', 'making', 'done'];
export const DESIGN_LIMIT = { entries: 1000, name: 60, side: 30, look: 4000, body: 6000, refs: 2000, steps: 30, missions: 50 };
const STEP = /^[A-Za-z0-9_-]{1,24}$/;
const FILLED = 'SELECT DISTINCT grp FROM story_designs';


// The outline steps an entry appears in: one outline (book from 1) and up to `steps` step ids of it; none = no book.
function links(b) {
  const L = DESIGN_LIMIT;
  const book = b.book == null || b.book === '' || b.book === 0 ? null : int(b.book);
  if (book != null && book < 1) throw new HttpError(400, 'bad outline number');
  const steps = Array.isArray(b.steps) ? [...new Set(b.steps.map(String))] : [];
  if (steps.length > L.steps || steps.some((s) => !STEP.test(s))) throw new HttpError(400, '大纲步骤无效');
  const missions = Array.isArray(b.missions) ? [...new Set(b.missions.map((m) => int(m)))] : [];
  if (missions.length > L.missions || missions.some((m) => !(m >= 1))) throw new HttpError(400, '关卡编号无效');
  return [book, JSON.stringify(book ? steps : []), JSON.stringify(missions)];
}

function shape({ grp, steps, missions, ...row }) {
  return { ...row, steps: JSON.parse(steps), missions: JSON.parse(missions) };
}

export async function designRoute(request, env, me, missions, groups, path, url) {
  const db = env.DB;
  const L = DESIGN_LIMIT;
  if (path === '/api/design' && request.method === 'GET') {
    // The groups with entries, plus those with an outline (so a group starting its list is listed to everyone who may write it).
    const [shown, main] = await mainFirst(db, await readable(db, me, groups, `${FILLED} UNION SELECT grp FROM story_outlines WHERE json_array_length(data, '$.lanes') > 0`));
    const group = pick(me, url.searchParams.get('group') ?? main, shown, groups);
    const member = group != null && canWrite(me, group);
    const editable = member || (me != null && group != null && await openGroup(db, group));
    const rows = group == null ? { results: [] } : await db.prepare('SELECT * FROM story_designs WHERE grp = ?1 ORDER BY id').bind(group).all();
    return { me, group, groups: shown, editable, member, limit: L,
      rows: anonymous(me, rows.results.map(shape), ['author', 'updated_by']) };
  }
  if (request.method !== 'POST' || !['/api/design/save', '/api/design/remove'].includes(path)) return null;
  const b = await body(request);
  if (!b || typeof b !== 'object' || Array.isArray(b)) throw new HttpError(400, 'JSON object expected');
  const plan = new Plan(env, me, missions);
  if (path === '/api/design/remove' && me.role !== 'dev' && b.group != null && String(b.group) !== me.group) {
    throw new HttpError(403, '只有本组成员能删除设定');
  }
  const group = path === '/api/design/save' ? await openScope(db, me, b) : scope(me, b.group);
  let row = null;
  if (b.id != null) {
    row = await db.prepare('SELECT * FROM story_designs WHERE id = ?1 AND grp = ?2').bind(int(b.id), group).first();
    if (!row) throw new HttpError(404, '这条设定已经被删掉了，请刷新');
  }
  if (path === '/api/design/remove') {
    if (!row) throw new HttpError(400, '需要设定编号');
    await plan.charge();
    await db.prepare('DELETE FROM story_designs WHERE id = ?1').bind(row.id).run();
    return { ok: true };
  }
  const name = clean(b.name, L.name);
  if (!name) throw new HttpError(400, '名称不能为空');
  if (!KINDS.includes(b.kind)) throw new HttpError(400, '分类无效');
  if (!DESIGN_STATUSES.includes(b.status)) throw new HttpError(400, '状态无效');
  const fields = [b.kind, name, b.status, clean(b.side, L.side), clean(b.look, L.look), clean(b.body, L.body), clean(b.refs, L.refs), ...links(b)];
  await plan.charge();
  const now = Date.now();
  if (row) {
    const saved = await db.prepare(`UPDATE story_designs SET kind = ?3, name = ?4, status = ?5, side = ?6, look = ?7, body = ?8, refs = ?9,
      book = ?10, steps = ?11, missions = ?12, updated_at = ?13, updated_by = ?14, version = version + 1
      WHERE id = ?1 AND version = ?2 RETURNING version`).bind(row.id, int(b.version), ...fields, now, me.user).first();
    if (!saved) {
      const cur = await db.prepare('SELECT updated_by FROM story_designs WHERE id = ?1').bind(row.id).first();
      throw new HttpError(409, `这条设定刚被 ${cur?.updated_by || '别人'} 改过，你的改动没有存上`);
    }
    return { id: row.id, version: saved.version };
  }
  const added = await db.prepare(`INSERT INTO story_designs (grp, kind, name, status, side, look, body, refs, book, steps, missions,
      author, updated_by, created_at, updated_at, version)
    SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?12, ?13, ?13, 1
    WHERE (SELECT COUNT(*) FROM story_designs WHERE grp = ?1) < ?14 RETURNING id, version`)
    .bind(group, ...fields, me.user, now, L.entries).first();
  if (!added) throw new HttpError(409, '这个组的设定已达上限');
  return { id: added.id, version: added.version };
}
