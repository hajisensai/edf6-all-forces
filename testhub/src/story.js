// The group's missions in campaign order (/story.html, 关卡策划): what each mission is (goal, enemies, story, how it hands
// over to the next), the stock mission whose map it borrows, and the outline step it plays out (book + step id of the
// same group's /outline.html). Independent of the stock planning outline (/plan.html) and the game's mission list.
// Any member of the group edits, moves and deletes any of its missions (as with the outline); when the group has an
// outline open to every account (src/outline.js), any account also adds, edits and moves them (deleting stays with the
// group). Every edit names the
// version it was made from, so two people editing one mission at once cannot overwrite each other (the later one gets a 409).
import { Plan, anonymous, between, canWrite, clean, int, pick, readable, scope } from './plan.js';
import { HttpError, body } from './http.js';
import { mainFirst, openGroup, openScope } from './outline.js';

export const STATUSES = ['planned', 'writing', 'testing', 'done'];
export const STORY_LIMIT = { missions: 1000, title: 100, body: 10000, goal: 2000, enemies: 2000, handoff: 2000 };
const STEP = /^[A-Za-z0-9_-]{1,24}$/;

// The outline step a mission plays out: both or neither (an outline number from 1, a step id as in src/outline.js).
// The step is not checked against the outline: an edited outline may drop it, and the page then says so.
function link(b) {
  if (b.book == null || b.book === '' || b.book === 0) return [null, null];
  const book = int(b.book);
  if (book < 1) throw new HttpError(400, 'bad outline number');
  const step = b.step == null || b.step === '' ? null : String(b.step);
  if (step != null && !STEP.test(step)) throw new HttpError(400, 'bad outline step');
  return [book, step];
}

export async function storyRoute(request, env, me, missions, groups, path, url) {
  const plan = new Plan(env, me, missions);
  const db = env.DB;
  const L = STORY_LIMIT;
  if (path === '/api/story' && request.method === 'GET') {
    // Readable by anyone (me = null: a visitor, who does not see who wrote or last changed a mission).
    const [shown, main] = await mainFirst(db, await readable(db, me, groups, 'SELECT DISTINCT grp FROM story_missions'));
    const group = pick(me, url.searchParams.get('group') ?? main, shown, groups);
    const rows = group == null ? { results: [] } : await db.prepare('SELECT * FROM story_missions WHERE grp = ?1 ORDER BY sort, id').bind(group).all();
    const member = group != null && canWrite(me, group);
    const editable = member || (me != null && group != null && await openGroup(db, group));
    return { me, group, groups: shown, editable, member, limit: L,
      rows: anonymous(me, rows.results.map(({ grp, ...row }) => row), ['author', 'updated_by']) };
  }
  if (request.method !== 'POST' || !['/api/story/save', '/api/story/move', '/api/story/remove'].includes(path)) return null;
  const b = await body(request);
  if (!b || typeof b !== 'object' || Array.isArray(b)) throw new HttpError(400, 'JSON object expected');
  if (path === '/api/story/remove' && me.role !== 'dev' && b.group != null && String(b.group) !== me.group) {
    throw new HttpError(403, '只有本组成员能删除关卡');
  }
  const group = path === '/api/story/remove' ? scope(me, b.group) : await openScope(db, me, b);
  let row = null;
  if (b.id != null) {
    row = await db.prepare('SELECT * FROM story_missions WHERE id = ?1 AND grp = ?2').bind(int(b.id), group).first();
    if (!row) throw new HttpError(404, '这一关已经被删掉了，请刷新');
  }
  if (path === '/api/story/save') {
    const title = clean(b.title, L.title);
    if (!title) throw new HttpError(400, '关卡名不能为空');
    if (!STATUSES.includes(b.status)) throw new HttpError(400, '关卡状态无效');
    const mission = b.mission ? plan.mission(b.mission) : null;
    const [book, step] = link(b);
    const fields = [title, clean(b.body, L.body), clean(b.goal, L.goal), clean(b.enemies, L.enemies), clean(b.handoff, L.handoff), mission, b.status, book, step];
    const version = row ? int(b.version) : null;
    await plan.charge();
    const now = Date.now();
    if (row) {
      // Compare-and-set on the version the editor started from.
      const saved = await db.prepare(`UPDATE story_missions SET title = ?3, body = ?4, goal = ?5, enemies = ?6, handoff = ?7, mission = ?8,
        status = ?9, book = ?10, step = ?11, updated_at = ?12, updated_by = ?13, version = version + 1
        WHERE id = ?1 AND version = ?2 RETURNING version`).bind(row.id, version, ...fields, now, me.user).first();
      if (!saved) {
        const cur = await db.prepare('SELECT updated_by, updated_at FROM story_missions WHERE id = ?1').bind(row.id).first();
        throw new HttpError(409, `这一关刚被 ${cur?.updated_by || '别人'} 改过，你的改动没有存上`);
      }
      return { id: row.id, version: saved.version };
    }
    // At the end, or right after mission `after` (a mission written between two others).
    let sort = null;
    if (b.after != null) {
      const prev = await db.prepare('SELECT sort FROM story_missions WHERE id = ?1 AND grp = ?2').bind(int(b.after), group).first();
      if (!prev) throw new HttpError(404, '要插在后面的那一关已经被删掉了，请刷新');
      const next = await db.prepare('SELECT sort FROM story_missions WHERE grp = ?1 AND sort > ?2 ORDER BY sort LIMIT 1').bind(group, prev.sort).first();
      sort = between(prev.sort, next ? next.sort : null);
      if (sort == null) {   // the gap has worn out: renumber, then the gap after it is 1 wide
        const all = (await db.prepare('SELECT id FROM story_missions WHERE grp = ?1 ORDER BY sort, id').bind(group).all()).results;
        await db.batch(all.map((r, i) => db.prepare('UPDATE story_missions SET sort = ?2 WHERE id = ?1').bind(r.id, i)));
        sort = all.findIndex((r) => r.id === int(b.after)) + 0.5;
      }
    }
    const added = await db.prepare(
      `INSERT INTO story_missions (grp, title, body, goal, enemies, handoff, mission, status, book, step, sort, author, updated_by, created_at, updated_at, version)
       SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, COALESCE(?11, (SELECT MAX(sort) FROM story_missions WHERE grp = ?1) + 1, 0), ?12, ?12, ?13, ?13, 1
       WHERE (SELECT COUNT(*) FROM story_missions WHERE grp = ?1) < ?14 RETURNING id, version`,
    ).bind(group, ...fields, sort, me.user, now, L.missions).first();
    if (!added) throw new HttpError(409, '这个组的关卡已达上限');
    return { id: added.id, version: added.version };
  }
  if (!row) throw new HttpError(400, '需要关卡编号');
  if (path === '/api/story/remove') {
    await plan.charge();
    await db.prepare('DELETE FROM story_missions WHERE id = ?1').bind(row.id).run();
    return { ok: true };
  }
  // Either one step (direction) or straight to a 1-based place in the list (position; past either end = that end).
  const position = b.position == null ? null : int(b.position);
  if (position == null && !['up', 'down'].includes(b.direction)) throw new HttpError(400, '移动方向无效');
  await plan.charge();
  // Move to a gap rather than swapping two rows: concurrent moves never leave a half-swapped pair.
  for (let pass = 0; pass < 2; pass++) {
    const rows = (await db.prepare('SELECT id, sort FROM story_missions WHERE grp = ?1 ORDER BY sort, id').bind(group).all()).results;
    const index = rows.findIndex((r) => r.id === row.id);
    if (index < 0) throw new HttpError(404, '这条剧情关卡不存在，请刷新');
    const nextIndex = position != null ? Math.min(Math.max(position, 1), rows.length) - 1 : index + (b.direction === 'up' ? -1 : 1);
    if (nextIndex < 0 || nextIndex >= rows.length || nextIndex === index) return { ok: true };
    const rest = rows.filter((r) => r.id !== row.id);
    const sort = between(rest[nextIndex - 1]?.sort ?? null, rest[nextIndex]?.sort ?? null);
    if (sort !== null) {
      await db.prepare('UPDATE story_missions SET sort = ?2 WHERE id = ?1').bind(row.id, sort).run();
      return { ok: true };
    }
    await db.batch(rows.map((r, i) => db.prepare('UPDATE story_missions SET sort = ?2 WHERE id = ?1').bind(r.id, i)));
  }
  throw new HttpError(409, '顺序正在变动，请刷新后重试');
}
