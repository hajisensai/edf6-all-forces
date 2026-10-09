// EDF 全军出击 测试站 (edf6.fushi.moe): the tester and the developer share one page.
//  - builds: installer zips mirrored into R2 (CI after each nightly, or tools/testhub.py publish for a branch build);
//  - cases: what to test, how, and its state; reports: the tester's results with logs / videos attached;
//  - the developer's replies sit under each report.
// Everything is behind a login (ACCOUNTS secret); scripts and the installer use HTTP Basic instead of the cookie.
import { parseAccounts, checkLogin, makeSession, readSession, basicUser } from './auth.js';
import { HttpError, json, body } from './http.js';
import { planRoute } from './plan.js';
import MISSIONS from './missions.json' with { type: 'json' };

const MAX_UPLOAD = 95 * 1024 * 1024;        // the Workers request limit is 100 MB
const RESULTS = ['pass', 'fail', 'new', 'info'];
const STATUSES = ['todo', 'verify', 'pass', 'fail', 'closed'];
const PUBLIC_ASSETS = new Set(['/login.html', '/login.js', '/style.css']);
const SESSION_DAYS = 60;

export default {
  async fetch(request, env) {
    try {
      return secured(await route(request, env));
    } catch (e) {
      if (e instanceof HttpError) return secured(json({ error: e.message }, e.status));
      console.error(e && e.stack || e);
      return secured(json({ error: 'internal error' }, 500));
    }
  },
};

// Every response: no framing, no sniffing, no referrer leaving the site, and for pages only the site's own script and style (no
// inline script at all, so text a player wrote can never run even if some page forgot to treat it as text).
const SECURITY = {
  'content-security-policy': "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self'; connect-src 'self'; "
    + "form-action 'self'; base-uri 'none'; frame-ancestors 'none'",
  'x-content-type-options': 'nosniff',
  // same-origin, not no-referrer: under no-referrer the browser sends `Origin: null` with its own form posts (the
  // login), which the cross-site check below would refuse.
  'referrer-policy': 'same-origin',
  'x-frame-options': 'DENY',
};

function secured(response) {
  const out = new Response(response.body, response);
  for (const [k, v] of Object.entries(SECURITY)) out.headers.set(k, v);
  return out;
}

async function who(request, env, accounts) {
  const basic = basicUser(request.headers.get('authorization'));
  if (basic) return checkLogin(accounts, basic.user, basic.pass);
  return readSession(request.headers.get('cookie'), env.SESSION_SECRET, accounts);
}

async function route(request, env) {
  if (!env.ACCOUNTS || !env.SESSION_SECRET) return json({ error: 'server not configured' }, 500);   // fail closed
  const accounts = parseAccounts(env.ACCOUNTS);
  const url = new URL(request.url);
  const path = url.pathname;
  const method = request.method;

  // Writes come from this site's own pages or from scripts (no Origin): a page elsewhere cannot post here.
  const origin = request.headers.get('origin');
  if (method !== 'GET' && method !== 'HEAD' && origin && origin !== url.origin) throw new HttpError(403, 'cross-site request');

  if (path === '/login' && method === 'POST') return login(request, env, accounts);
  if (path === '/logout') return redirect('/login.html', { 'set-cookie': 's=; Path=/; Max-Age=0; HttpOnly; Secure; SameSite=Lax' });
  if (PUBLIC_ASSETS.has(path)) return env.ASSETS.fetch(request);

  const me = await who(request, env, accounts);
  if (!me) {
    if (path === '/' || path.endsWith('.html')) return redirect('/login.html');
    return json({ error: 'login required' }, 401, { 'www-authenticate': 'Basic realm="edf6-testhub"' });
  }
  const dev = me.role === 'dev';
  const need = (ok) => { if (!ok) throw new HttpError(403, 'developer only'); };

  if (path === '/api/state' && method === 'GET') return json(await state(env, me));
  if (path === '/api/latest' && method === 'GET') return json(await latest(env, url.searchParams.get('channel')));
  if (path.startsWith('/dl/') && method === 'GET') return download(env, 'builds/' + decodeURIComponent(path.slice(4)));
  if (path.startsWith('/file/') && method === 'GET') return attachment(env, Number(path.slice(6)));
  if (path === '/api/report' && method === 'POST') return json(await report(request, env, me));
  if (path === '/api/reply' && method === 'POST') { need(dev); return json(await reply(request, env)); }
  if (path === '/api/case' && method === 'POST') { need(dev); return json(await saveCase(request, env)); }
  if (path === '/api/build' && method === 'PUT') { need(dev); return json(await putBuild(request, env, url)); }
  if (path === '/api/reports' && method === 'GET') { need(dev); return json(await reportsSince(env, Number(url.searchParams.get('since') || 0))); }
  if (path === '/api/plan' || path.startsWith('/api/plan/')) {
    const out = await planRoute(request, env, me, MISSIONS, groupsOf(accounts), path, url);
    if (out) return json(out);
  }
  if (method === 'GET') return env.ASSETS.fetch(path === '/' ? new Request(new URL('/index.html', url), request) : request);
  throw new HttpError(404, 'not found');
}

function groupsOf(accounts) {
  return [...new Set([...accounts.values()].map((a) => a.group))].sort();
}

function redirect(to, headers = {}) {
  return new Response(null, { status: 303, headers: { location: to, ...headers } });
}

async function login(request, env, accounts) {
  const form = await request.formData();
  const me = checkLogin(accounts, String(form.get('user') || ''), String(form.get('pass') || ''));
  if (!me) return redirect('/login.html?bad=1');
  const token = await makeSession(accounts.get(me.user), Date.now() + SESSION_DAYS * 864e5, env.SESSION_SECRET);
  return redirect('/', { 'set-cookie': `s=${token}; Path=/; Max-Age=${SESSION_DAYS * 86400}; HttpOnly; Secure; SameSite=Lax` });
}

// ---------------------------------------------------------------- reads

async function state(env, me) {
  const [builds, cases, reports, files] = await Promise.all([
    env.DB.prepare('SELECT * FROM builds ORDER BY at DESC LIMIT 20').all(),
    env.DB.prepare('SELECT * FROM cases ORDER BY sort, id').all(),
    env.DB.prepare('SELECT * FROM reports ORDER BY id DESC LIMIT 200').all(),
    env.DB.prepare('SELECT id, report_id, name, size FROM files WHERE report_id >= (SELECT COALESCE(MIN(id),0) FROM (SELECT id FROM reports ORDER BY id DESC LIMIT 200))').all(),
  ]);
  return { me: { user: me.user, role: me.role, group: me.group }, builds: builds.results, cases: cases.results, reports: attach(reports.results, files.results) };
}

function attach(reports, files) {
  const by = new Map(reports.map((r) => [r.id, { ...r, files: [] }]));
  for (const f of files) by.get(f.report_id)?.files.push({ id: f.id, name: f.name, size: f.size });
  return [...by.values()];
}

async function reportsSince(env, since) {
  const reports = await env.DB.prepare('SELECT * FROM reports WHERE id > ?1 ORDER BY id').bind(since).all();
  const files = await env.DB.prepare('SELECT id, report_id, name, size FROM files WHERE report_id > ?1').bind(since).all();
  return { reports: attach(reports.results, files.results) };
}

async function latest(env, channel) {
  const row = channel
    ? await env.DB.prepare('SELECT * FROM builds WHERE channel = ?1 ORDER BY at DESC LIMIT 1').bind(channel).first()
    : await env.DB.prepare('SELECT * FROM builds ORDER BY at DESC LIMIT 1').first();
  if (!row) throw new HttpError(404, 'no build yet');
  return row;
}

async function download(env, key) {
  const obj = await env.FILES.get(key);
  if (!obj) throw new HttpError(404, 'not found');
  const name = key.split('/').pop();
  return new Response(obj.body, {
    headers: {
      'content-type': obj.httpMetadata?.contentType || 'application/octet-stream',
      'content-length': String(obj.size),
      'content-disposition': `attachment; filename*=UTF-8''${encodeURIComponent(name)}`,
      'cache-control': 'private, no-store',
    },
  });
}

async function attachment(env, id) {
  const row = await env.DB.prepare('SELECT r2_key FROM files WHERE id = ?1').bind(id).first();
  if (!row) throw new HttpError(404, 'not found');
  return download(env, row.r2_key);
}

// ---------------------------------------------------------------- writes

function cleanName(name) {
  const base = String(name || 'file').split(/[\\/]/).pop().replace(/[^\p{L}\p{N}._ -]/gu, '_').slice(0, 120);
  return base || 'file';
}

async function report(request, env, me) {
  const length = Number(request.headers.get('content-length') || 0);
  if (length > MAX_UPLOAD) throw new HttpError(413, `附件总共不能超过 ${MAX_UPLOAD >> 20} MB`);
  const form = await request.formData();
  const result = String(form.get('result') || '');
  if (!RESULTS.includes(result)) throw new HttpError(400, 'result must be one of ' + RESULTS.join('/'));
  const caseId = Number(form.get('case_id')) || null;
  const note = String(form.get('note') || '').slice(0, 20000);
  const version = String(form.get('version') || '').slice(0, 80);
  const source = form.get('source') === 'exe' ? 'exe' : 'web';
  const uploads = form.getAll('file').filter((f) => typeof f === 'object' && f.size > 0);
  const total = uploads.reduce((n, f) => n + f.size, 0);
  if (total > MAX_UPLOAD) throw new HttpError(413, `附件总共不能超过 ${MAX_UPLOAD >> 20} MB`);
  if (!note.trim() && !uploads.length) throw new HttpError(400, '说明和附件至少要有一样');

  const row = await env.DB.prepare(
    'INSERT INTO reports (case_id, author, result, note, version, source, at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) RETURNING id',
  ).bind(caseId, me.user, result, note, version, source, Date.now()).first();
  const files = [];
  for (const f of uploads) {
    const name = cleanName(f.name);
    const key = `reports/${row.id}/${crypto.randomUUID().slice(0, 8)}-${name}`;
    await env.FILES.put(key, f.stream(), { httpMetadata: { contentType: f.type || 'application/octet-stream' } });
    files.push(env.DB.prepare('INSERT INTO files (report_id, name, size, r2_key) VALUES (?1, ?2, ?3, ?4)').bind(row.id, name, f.size, key));
  }
  if (files.length) await env.DB.batch(files);
  if (caseId && result === 'fail') {
    await env.DB.prepare("UPDATE cases SET status = 'fail', updated_at = ?2 WHERE id = ?1").bind(caseId, Date.now()).run();
  }
  return { id: row.id, files: files.length };
}

async function reply(request, env) {
  const { report_id: id, text } = await body(request);
  const r = await env.DB.prepare('UPDATE reports SET reply = ?2, reply_at = ?3 WHERE id = ?1').bind(Number(id), String(text || ''), Date.now()).run();
  if (!r.meta.changes) throw new HttpError(404, 'no such report');
  return { ok: true };
}

async function saveCase(request, env) {
  const c = await body(request);
  if (c.status !== undefined && !STATUSES.includes(c.status)) throw new HttpError(400, 'status must be one of ' + STATUSES.join('/'));
  const now = Date.now();
  if (!c.id) {
    if (!c.title) throw new HttpError(400, 'title required');
    const row = await env.DB.prepare(
      'INSERT INTO cases (title, steps, status, dev_note, sort, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6) RETURNING id',
    ).bind(c.title, c.steps || '', c.status || 'todo', c.dev_note || '', Number(c.sort) || 0, now).first();
    return { id: row.id };
  }
  const old = await env.DB.prepare('SELECT * FROM cases WHERE id = ?1').bind(Number(c.id)).first();
  if (!old) throw new HttpError(404, 'no such case');
  const v = { ...old, ...c };
  await env.DB.prepare('UPDATE cases SET title = ?2, steps = ?3, status = ?4, dev_note = ?5, sort = ?6, updated_at = ?7 WHERE id = ?1')
    .bind(old.id, v.title, v.steps, v.status, v.dev_note, Number(v.sort) || 0, now).run();
  return { id: old.id };
}

async function putBuild(request, env, url) {
  const q = url.searchParams;
  const name = cleanName(q.get('name'));
  const version = q.get('version') || '';
  const channel = q.get('channel') || 'nightly';
  if (!/\.zip$/i.test(name) || !version) throw new HttpError(400, 'name=<file>.zip and version= required');
  const notes = decodeURIComponent(request.headers.get('x-notes') || '').slice(0, 20000);
  const data = await request.arrayBuffer();
  if (!data.byteLength) throw new HttpError(400, 'empty body');
  const sha = [...new Uint8Array(await crypto.subtle.digest('SHA-256', data))].map((b) => b.toString(16).padStart(2, '0')).join('');
  await env.FILES.put('builds/' + name, data, { httpMetadata: { contentType: 'application/zip' } });
  await env.DB.prepare(
    `INSERT INTO builds (name, version, channel, commit_sha, notes, size, sha256, at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
     ON CONFLICT(name) DO UPDATE SET version = ?2, channel = ?3, commit_sha = ?4, notes = ?5, size = ?6, sha256 = ?7, at = ?8`,
  ).bind(name, version, channel, q.get('commit') || '', notes, data.byteLength, sha, Date.now()).run();
  await pruneBuilds(env, Number(env.KEEP_BUILDS) || 15);
  return { name, sha256: sha, size: data.byteLength };
}

async function pruneBuilds(env, keep) {
  const old = await env.DB.prepare('SELECT id, name FROM builds ORDER BY at DESC LIMIT -1 OFFSET ?1').bind(keep).all();
  for (const b of old.results) {
    await env.FILES.delete('builds/' + b.name);
    await env.DB.prepare('DELETE FROM builds WHERE id = ?1').bind(b.id).run();
  }
}
