// Level plan: proposals (a new mission, or a change to a stock one) with replies, and each group's outline: the
// stock missions in the game's order (EDF6 then EDF5, src/missions.json), new missions placed between them and
// loop markers ("from here on, the next loop"). Everything is plain text, written by players.
//
// Poisoning: whatever players write is data. It is stored cleaned (clean(): no control, direction or zero-width
// characters, bounded length), shown only as text (public/plan.js builds DOM nodes, never markup), fenced as
// untrusted when the developer pulls it (tools/testhub.py plan), and every write is rate limited per account.
// A group sees only its own rows; developers see every group.
import { GROUP } from './auth.js';
import { HttpError, body } from './http.js';

export const KINDS = ['new', 'edit'];
export const PSTATUS = ['open', 'accepted', 'done', 'rejected'];
export const LIMIT = { title: 100, body: 10000, reply: 4000, note: 60, proposals: 3000, replies: 500, digest: 1 << 20 };
export const RATE = [[10 * 60e3, 60], [864e5, 600]];   // [window ms, writes] per account

// Invisible characters that hide, reorder or smuggle text: C0/C1 controls except \t \n, soft hyphen, Arabic
// letter mark, Mongolian vowel separator, zero-width and direction marks, line/paragraph separators, bidi
// embeddings/overrides/isolates, word joiner .. invisible operators, variation selectors, BOM, interlinear
// annotation, tag characters (ASCII smuggling: text a model reads and a screen does not show) and the
// supplementary variation selectors.
const INVISIBLE = /[\u0000-\u0008\u000B-\u001F\u007F-\u009F\u00AD\u061C\u180E\u200B-\u200F\u2028-\u202E\u2060-\u2069\uFE00-\uFE0F\uFEFF\uFFF9-\uFFFB\u{E0000}-\u{E007F}\u{E0100}-\u{E01EF}]/gu;

export function clean(value, max) {
  const text = String(value ?? '').normalize('NFC').replace(/\r\n?/g, '\n').replace(INVISIBLE, '')
    .replace(/\n{4,}/g, '\n\n\n').trim();
  return [...text].slice(0, max).join('').trim();
}

// The group a request acts on: an account's own; a developer names one (none = the ungrouped one).
export function scope(me, asked) {
  if (me.role !== 'dev') return me.group;
  const g = asked == null ? '' : String(asked);
  if (!GROUP.test(g)) throw new HttpError(400, 'bad group');
  return g;
}

// An id from a request: a whole number, else a 400 (not NaN bound into SQL).
export function int(value) {
  const n = typeof value === 'string' && value.trim() ? Number(value) : value;
  if (!Number.isSafeInteger(n)) throw new HttpError(400, 'bad id');
  return n;
}

// A sort key strictly between two neighbours (null = no neighbour on that side); null when the gap has worn out
// (the outline is renumbered first).
export function between(prev, next) {
  if (prev == null) return next == null ? 0 : next - 1;
  if (next == null) return prev + 1;
  const mid = (prev + next) / 2;
  return next - prev > 1e-6 && mid > prev && mid < next ? mid : null;
}

export class Plan {
  constructor(env, me, missions) {
    this.db = env.DB;
    this.me = me;
    this.dev = me.role === 'dev';
    this.missions = missions;
    this.keys = new Set(missions.map((m) => m.key));
  }

  // ------------------------------------------------------------ reads

  async state(grp, groups) {
    await this.seed(grp);
    const [outline, proposals, replies, digests] = await Promise.all([
      this.db.prepare('SELECT id, kind, mission, proposal_id, note, author, at FROM outline WHERE grp = ?1 ORDER BY sort, id').bind(grp).all(),
      this.db.prepare('SELECT * FROM proposals WHERE grp = ?1 ORDER BY id DESC').bind(grp).all(),
      this.db.prepare('SELECT r.* FROM proposal_replies r JOIN proposals p ON p.id = r.proposal_id WHERE p.grp = ?1 ORDER BY r.id').bind(grp).all(),
      this.db.prepare('SELECT mission FROM mission_digests').all(),
    ]);
    return {
      me: { user: this.me.user, role: this.me.role, group: this.me.group }, group: grp, groups: this.dev ? groups : [grp],
      missions: this.missions, outline: outline.results, proposals: proposals.results.map(({ grp: _, ...p }) => p),
      replies: replies.results, digests: digests.results.map((d) => d.mission),
    };
  }

  // Every stock mission the group's outline lacks, after what it has (on a new group: the game's order).
  async seed(grp) {
    const have = await this.db.prepare("SELECT mission FROM outline WHERE grp = ?1 AND kind = 'stock'").bind(grp).all();
    const got = new Set(have.results.map((r) => r.mission));
    const missing = this.missions.filter((m) => !got.has(m.key));
    if (!missing.length) return;
    const top = await this.db.prepare('SELECT MAX(sort) AS s FROM outline WHERE grp = ?1').bind(grp).first();
    const base = top?.s ?? 0;
    const now = Date.now();
    await this.db.batch(missing.map((m, i) => this.db.prepare(
      "INSERT OR IGNORE INTO outline (grp, kind, mission, sort, at) VALUES (?1, 'stock', ?2, ?3, ?4)").bind(grp, m.key, base + i + 1, now)));
  }

  // ------------------------------------------------------------ proposals

  async saveProposal(grp, b) {
    const title = clean(b.title, LIMIT.title);
    const text = clean(b.body, LIMIT.body);
    if (!title) throw new HttpError(400, '标题不能为空');
    const now = Date.now();
    if (!b.id) {
      if (!KINDS.includes(b.kind)) throw new HttpError(400, 'kind must be new or edit');
      const mission = b.kind === 'edit' ? this.mission(b.mission) : null;
      await this.charge();
      const row = await this.db.prepare(   // the cap is checked in the insert itself
        `INSERT INTO proposals (grp, kind, mission, title, body, author, created_at, updated_at) SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?7
         WHERE (SELECT COUNT(*) FROM proposals WHERE grp = ?1) < ?8 RETURNING id`,
      ).bind(grp, b.kind, mission, title, text, this.me.user, now, LIMIT.proposals).first();
      if (!row) throw new HttpError(409, '这个组的意见已经太多了，请先清理');
      return { id: row.id };
    }
    const p = await this.ownProposal(grp, b.id);
    const mission = p.kind === 'edit' ? this.mission(b.mission ?? p.mission) : null;
    await this.charge();
    await this.db.prepare('UPDATE proposals SET title = ?2, body = ?3, mission = ?4, updated_at = ?5 WHERE id = ?1')
      .bind(p.id, title, text, mission, now).run();
    return { id: p.id };
  }

  async deleteProposal(grp, b) {
    const p = await this.ownProposal(grp, b.id);
    await this.charge();
    await this.db.batch([
      this.db.prepare('DELETE FROM proposal_replies WHERE proposal_id = ?1').bind(p.id),
      this.db.prepare("DELETE FROM outline WHERE kind = 'new' AND proposal_id = ?1").bind(p.id),
      this.db.prepare('DELETE FROM proposals WHERE id = ?1').bind(p.id),
    ]);
    return { ok: true };
  }

  async setStatus(grp, b) {
    if (!this.dev) throw new HttpError(403, 'developer only');
    if (!PSTATUS.includes(b.status)) throw new HttpError(400, 'status must be one of ' + PSTATUS.join('/'));
    const p = await this.proposal(grp, b.id);
    await this.db.prepare('UPDATE proposals SET status = ?2 WHERE id = ?1').bind(p.id, b.status).run();
    return { ok: true };
  }

  async reply(grp, b) {
    const p = await this.proposal(grp, b.proposal_id);
    const text = clean(b.body, LIMIT.reply);
    if (!text) throw new HttpError(400, '回复不能为空');
    await this.charge();
    const row = await this.db.prepare(
      `INSERT INTO proposal_replies (proposal_id, author, body, at) SELECT ?1, ?2, ?3, ?4
       WHERE (SELECT COUNT(*) FROM proposal_replies WHERE proposal_id = ?1) < ?5 RETURNING id`,
    ).bind(p.id, this.me.user, text, Date.now(), LIMIT.replies).first();
    if (!row) throw new HttpError(409, '这条意见的回复已经太多了');
    return { id: row.id };
  }

  async deleteReply(grp, b) {
    const r = await this.db.prepare(
      'SELECT r.id, r.author FROM proposal_replies r JOIN proposals p ON p.id = r.proposal_id WHERE r.id = ?1 AND p.grp = ?2',
    ).bind(int(b.id), grp).first();
    if (!r) throw new HttpError(404, 'no such reply');
    if (r.author !== this.me.user && !this.dev) throw new HttpError(403, '只能删自己的回复');
    await this.charge();
    await this.db.prepare('DELETE FROM proposal_replies WHERE id = ?1').bind(r.id).run();
    return { ok: true };
  }

  // ------------------------------------------------------------ outline

  async insert(grp, b) {
    await this.seed(grp);
    let proposal = null;
    let note = '';
    if (b.kind === 'new') {
      const p = await this.proposal(grp, b.proposal_id);
      if (p.kind !== 'new') throw new HttpError(400, '只有「新关卡」意见能放进大纲');
      proposal = p.id;
    } else if (b.kind === 'loop') {
      note = clean(b.note, LIMIT.note) || '下一个轮回';
    } else {
      throw new HttpError(400, 'kind must be new or loop');
    }
    await this.charge();
    const sort = await this.place(grp, b.after, null);
    try {
      const row = await this.db.prepare(
        'INSERT INTO outline (grp, kind, proposal_id, note, sort, author, at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) RETURNING id',
      ).bind(grp, b.kind, proposal, note, sort, this.me.user, Date.now()).first();
      return { id: row.id };
    } catch (e) {
      if (/UNIQUE/i.test(String(e && e.message))) throw new HttpError(409, '这个新关卡已经在大纲里了');
      throw e;
    }
  }

  async move(grp, b) {
    const row = await this.row(grp, b.id);
    if (b.after != null && int(b.after) === row.id) return { ok: true };
    await this.charge();
    const sort = await this.place(grp, b.after, row.id);
    await this.db.prepare('UPDATE outline SET sort = ?2 WHERE id = ?1').bind(row.id, sort).run();
    return { ok: true };
  }

  async renameLoop(grp, b) {
    const row = await this.row(grp, b.id);
    if (row.kind !== 'loop') throw new HttpError(400, '只有轮回分隔线能改说明');
    await this.charge();
    await this.db.prepare('UPDATE outline SET note = ?2 WHERE id = ?1').bind(row.id, clean(b.note, LIMIT.note) || '下一个轮回').run();
    return { ok: true };
  }

  async remove(grp, b) {
    const row = await this.row(grp, b.id);
    if (row.kind === 'stock') throw new HttpError(400, '原版关卡不能删除，只能移动');
    await this.charge();
    await this.db.prepare('DELETE FROM outline WHERE id = ?1').bind(row.id).run();
    return { ok: true };
  }

  // The sort key right after row `after` (null: the top), skipping the row being moved.
  async place(grp, after, moving) {
    for (let pass = 0; pass < 2; pass++) {
      let prev = null;
      if (after != null) {
        const a = await this.db.prepare('SELECT sort FROM outline WHERE id = ?1 AND grp = ?2').bind(int(after), grp).first();
        if (!a) throw new HttpError(404, '要放的位置不存在了（大纲可能刚被别人改过），请刷新');
        prev = a.sort;
      }
      const next = prev == null
        ? await this.db.prepare('SELECT sort FROM outline WHERE grp = ?1 AND id != ?2 ORDER BY sort LIMIT 1').bind(grp, moving ?? -1).first()
        : await this.db.prepare('SELECT sort FROM outline WHERE grp = ?1 AND id != ?2 AND sort > ?3 ORDER BY sort LIMIT 1').bind(grp, moving ?? -1, prev).first();
      const s = between(prev, next ? next.sort : null);
      if (s !== null) return s;
      await this.renumber(grp);
    }
    throw new HttpError(500, 'outline order exhausted');
  }

  async renumber(grp) {
    const rows = await this.db.prepare('SELECT id FROM outline WHERE grp = ?1 ORDER BY sort, id').bind(grp).all();
    await this.db.batch(rows.results.map((r, i) => this.db.prepare('UPDATE outline SET sort = ?2 WHERE id = ?1').bind(r.id, i)));
  }

  // ------------------------------------------------------------ script digests (shared by every group)

  async digest(key) {
    const d = await this.db.prepare('SELECT mission, body, at FROM mission_digests WHERE mission = ?1').bind(this.mission(key)).first();
    if (!d) throw new HttpError(404, '这一关的脚本还没有上传');
    return d;
  }

  async putDigest(key, text) {
    if (!this.dev) throw new HttpError(403, 'developer only');
    const mission = this.mission(key);
    if (!text || text.length > LIMIT.digest) throw new HttpError(400, `digest must be 1..${LIMIT.digest} characters`);
    await this.db.prepare(
      'INSERT INTO mission_digests (mission, body, at) VALUES (?1, ?2, ?3) ON CONFLICT(mission) DO UPDATE SET body = ?2, at = ?3',
    ).bind(mission, text, Date.now()).run();
    return { mission, size: text.length };
  }

  // ------------------------------------------------------------ helpers

  mission(key) {
    if (!this.keys.has(key)) throw new HttpError(400, '没有这一关');
    return key;
  }

  async proposal(grp, id) {
    const p = await this.db.prepare('SELECT * FROM proposals WHERE id = ?1 AND grp = ?2').bind(int(id), grp).first();
    if (!p) throw new HttpError(404, 'no such proposal');
    return p;
  }

  async ownProposal(grp, id) {
    const p = await this.proposal(grp, id);
    if (p.author !== this.me.user && !this.dev) throw new HttpError(403, '只能改自己写的意见');
    return p;
  }

  async row(grp, id) {
    const r = await this.db.prepare('SELECT * FROM outline WHERE id = ?1 AND grp = ?2').bind(int(id), grp).first();
    if (!r) throw new HttpError(404, '大纲里没有这一行（可能刚被别人改过），请刷新');
    return r;
  }

  // Counts a write against the account's rate limits, or refuses it.
  // The count and the write are one statement (D1 runs statements one at a time), so concurrent requests cannot
  // all pass the check before any of them is counted.
  async charge() {
    const now = Date.now();
    const [[w1, m1], [w2, m2]] = RATE;
    const r = await this.db.prepare(
      `INSERT INTO write_log (author, at) SELECT ?1, ?2
       WHERE (SELECT COUNT(*) FROM write_log WHERE author = ?1 AND at > ?3) < ?4
         AND (SELECT COUNT(*) FROM write_log WHERE author = ?1 AND at > ?5) < ?6`,
    ).bind(this.me.user, now, now - w1, m1, now - w2, m2).run();
    if (!r.meta.changes) throw new HttpError(429, '操作太频繁了，请过一会儿再试');
    await this.db.prepare('DELETE FROM write_log WHERE author = ?1 AND at < ?2').bind(this.me.user, now - 2 * 864e5).run();
  }
}

// POST /api/plan/<action>: the action's name is the method; the body names the group (developers) and the rest.
const ACTIONS = {
  proposal: 'saveProposal', 'proposal-delete': 'deleteProposal', 'proposal-status': 'setStatus',
  reply: 'reply', 'reply-delete': 'deleteReply',
  insert: 'insert', move: 'move', 'loop-note': 'renameLoop', remove: 'remove',
};

export async function planRoute(request, env, me, missions, groups, path, url) {
  const plan = new Plan(env, me, missions);
  if (path === '/api/plan' && request.method === 'GET') return plan.state(scope(me, url.searchParams.get('group')), groups);
  if (path === '/api/plan/digest' && request.method === 'GET') return plan.digest(url.searchParams.get('mission'));
  if (path === '/api/plan/digest' && request.method === 'PUT') {
    if (me.role !== 'dev') throw new HttpError(403, 'developer only');   // before reading the body
    return plan.putDigest(url.searchParams.get('mission'), await request.text());
  }
  const name = path.slice('/api/plan/'.length);
  const action = path.startsWith('/api/plan/') && request.method === 'POST' && Object.hasOwn(ACTIONS, name) ? ACTIONS[name] : null;
  if (!action) return null;
  const b = await body(request);
  if (!b || typeof b !== 'object') throw new HttpError(400, 'JSON object expected');
  return plan[action](scope(me, b.group), b);
}
