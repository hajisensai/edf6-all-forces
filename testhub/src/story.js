// Custom story missions are group-owned data, independent of the stock planning outline.
import { Plan, between, clean, int, scope } from './plan.js';
import { HttpError, body } from './http.js';

const STATUSES = ['planned', 'writing', 'testing', 'done'];
const MAX_MISSIONS = 1000;

export async function storyRoute(request, env, me, missions, groups, path, url) {
  const plan = new Plan(env, me, missions);
  const db = env.DB;
  if (path === '/api/story' && request.method === 'GET') {
    const group = scope(me, url.searchParams.get('group'));
    const rows = await db.prepare('SELECT * FROM story_missions WHERE grp = ?1 ORDER BY sort, id').bind(group).all();
    return { me, group, groups: plan.dev ? groups : [group], rows: rows.results.map(({ grp, ...row }) => row) };
  }
  if (request.method !== 'POST' || !['/api/story/save', '/api/story/move', '/api/story/remove'].includes(path)) return null;
  const b = await body(request);
  if (!b || typeof b !== 'object' || Array.isArray(b)) throw new HttpError(400, 'JSON object expected');
  const group = scope(me, b.group);
  let row = null;
  if (b.id != null) {
    row = await db.prepare('SELECT * FROM story_missions WHERE id = ?1 AND grp = ?2').bind(int(b.id), group).first();
    if (!row) throw new HttpError(404, '这条剧情关卡不存在，请刷新');
  }
  if (path === '/api/story/save') {
    if (row && row.author !== me.user && !plan.dev) throw new HttpError(403, '只能编辑自己创建的关卡');
    const title = clean(b.title, 100);
    const text = clean(b.body, 10000);
    if (!title) throw new HttpError(400, '关卡名不能为空');
    if (!STATUSES.includes(b.status)) throw new HttpError(400, '关卡状态无效');
    const mission = b.mission ? plan.mission(b.mission) : null;
    await plan.charge();
    const now = Date.now();
    if (row) {
      await db.prepare('UPDATE story_missions SET title = ?2, body = ?3, mission = ?4, status = ?5, updated_at = ?6 WHERE id = ?1')
        .bind(row.id, title, text, mission, b.status, now).run();
      return { id: row.id };
    }
    const added = await db.prepare(
      `INSERT INTO story_missions (grp, title, body, mission, status, sort, author, created_at, updated_at)
       SELECT ?1, ?2, ?3, ?4, ?5, COALESCE((SELECT MAX(sort) FROM story_missions WHERE grp = ?1), 0) + 1, ?6, ?7, ?7
       WHERE (SELECT COUNT(*) FROM story_missions WHERE grp = ?1) < ?8 RETURNING id`,
    ).bind(group, title, text, mission, b.status, me.user, now, MAX_MISSIONS).first();
    if (!added) throw new HttpError(409, '这个组的剧情关卡已达上限');
    return { id: added.id };
  }
  if (!row) throw new HttpError(400, '需要关卡编号');
  if (path === '/api/story/remove') {
    if (row.author !== me.user && !plan.dev) throw new HttpError(403, '只能删除自己创建的关卡');
    await plan.charge();
    await db.prepare('DELETE FROM story_missions WHERE id = ?1').bind(row.id).run();
    return { ok: true };
  }
  if (!['up', 'down'].includes(b.direction)) throw new HttpError(400, '移动方向无效');
  await plan.charge();
  // Move to a gap rather than swapping two rows: concurrent moves never leave a half-swapped pair.
  for (let pass = 0; pass < 2; pass++) {
    const rows = (await db.prepare('SELECT id, sort FROM story_missions WHERE grp = ?1 ORDER BY sort, id').bind(group).all()).results;
    const index = rows.findIndex((r) => r.id === row.id);
    if (index < 0) throw new HttpError(404, '这条剧情关卡不存在，请刷新');
    const nextIndex = index + (b.direction === 'up' ? -1 : 1);
    if (nextIndex < 0 || nextIndex >= rows.length) return { ok: true };
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
