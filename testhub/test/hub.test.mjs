// node --test test/*.test.mjs  (Node 22.5+: node:sqlite)
// The test page's side of the worker: reports' handling status, case states, and builds sent in parts.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';
import { d1 } from './d1.mjs';
import worker, { REPORT_STATUSES } from '../src/worker.js';

// R2 as far as the worker uses it: put / get / delete and multipart (parts kept apart until complete()).
function r2() {
  const objects = new Map();
  const uploads = new Map();
  let next = 0;
  const handle = (key, uploadId) => ({
    uploadId,
    async uploadPart(partNumber, data) {
      const up = uploads.get(uploadId);
      if (!up || up.key !== key) throw new Error('no such upload');
      up.parts.set(partNumber, new Uint8Array(data));
      return { partNumber, etag: `etag-${uploadId}-${partNumber}` };
    },
    async complete(list) {
      const up = uploads.get(uploadId);
      if (!up || up.key !== key) throw new Error('no such upload');
      const chunks = list.map((p) => {
        if (p.etag !== `etag-${uploadId}-${p.partNumber}` || !up.parts.has(p.partNumber)) throw new Error('bad part');
        return up.parts.get(p.partNumber);
      });
      const all = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
      let at = 0;
      for (const c of chunks) { all.set(c, at); at += c.length; }
      uploads.delete(uploadId);
      objects.set(key, all);
      return { key, size: all.length };
    },
    async abort() { uploads.delete(uploadId); },
  });
  return {
    objects, uploads,
    async put(key, data) { objects.set(key, new Uint8Array(data instanceof ArrayBuffer ? data : await new Response(data).arrayBuffer())); },
    async get(key) {
      const v = objects.get(key);
      return v ? { body: v, size: v.length, httpMetadata: {} } : null;
    },
    async delete(key) { objects.delete(key); },
    async createMultipartUpload(key) {
      const id = 'u' + (++next);
      uploads.set(id, { key, parts: new Map() });
      return handle(key, id);
    },
    resumeMultipartUpload(key, id) { return handle(key, id); },
  };
}

const env = () => ({
  ACCOUNTS: 'alice:pw:tester,dev:pw:dev', SESSION_SECRET: 's', DB: d1(), FILES: r2(),
  ASSETS: { fetch: () => new Response('<p>page</p>', { headers: { 'content-type': 'text/html' } }) },
});
const call = (e, path, { user = 'dev', method = 'GET', data, raw, headers = {} } = {}) => worker.fetch(new Request('https://edf6.test' + path, {
  method,
  headers: { authorization: 'Basic ' + btoa(user + ':pw'), ...(data ? { 'content-type': 'application/json' } : {}), ...headers },
  body: data ? JSON.stringify(data) : raw,
}), e);
const addReport = async (e, note = '炸了') => {
  const form = new FormData();
  form.set('result', 'new');
  form.set('note', note);
  return (await (await call(e, '/api/report', { user: 'alice', method: 'POST', raw: form })).json()).id;
};
const sha256 = async (bytes) => [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))].map((b) => b.toString(16).padStart(2, '0')).join('');

test('reports: a new one is 待处理; developers set the handling status, testers cannot', async () => {
  const e = env();
  const id = await addReport(e);
  const st = async () => (await (await call(e, '/api/state', { user: 'alice' })).json()).reports.find((r) => r.id === id);
  assert.equal((await st()).status, 'open');
  assert.equal((await call(e, '/api/report_status', { user: 'alice', method: 'POST', data: { report_id: id, status: 'done' } })).status, 403);
  assert.equal((await call(e, '/api/report_status', { method: 'POST', data: { report_id: id, status: 'finished' } })).status, 400);
  assert.equal((await call(e, '/api/report_status', { method: 'POST', data: { report_id: 999, status: 'done' } })).status, 404);
  assert.equal((await call(e, '/api/report_status', { method: 'POST', data: { report_id: id, status: 'doing' } })).status, 200);
  assert.equal((await st()).status, 'doing');
  assert.ok((await st()).status_at > 0);
  for (const s of REPORT_STATUSES) {
    assert.equal((await call(e, '/api/report_status', { method: 'POST', data: { report_id: id, status: s } })).status, 200);
  }
});

test('reports: a reply can mark the report done in the same step; without a status it leaves it alone', async () => {
  const e = env();
  const id = await addReport(e);
  await call(e, '/api/reply', { method: 'POST', data: { report_id: id, text: '看一下' } });
  let r = (await (await call(e, '/api/state')).json()).reports[0];
  assert.equal(r.status, 'open');
  assert.equal(r.reply, '看一下');
  assert.equal((await call(e, '/api/reply', { method: 'POST', data: { report_id: id, text: '已修 abc1234', status: 'nope' } })).status, 400);
  await call(e, '/api/reply', { method: 'POST', data: { report_id: id, text: '已修 abc1234', status: 'done' } });
  r = (await (await call(e, '/api/state')).json()).reports[0];
  assert.equal(r.status, 'done');
  assert.equal(r.reply, '已修 abc1234');
});

test('cases: done (已完成) is a state of its own', async () => {
  const e = env();
  const { id } = await (await call(e, '/api/case', { method: 'POST', data: { title: 't' } })).json();
  assert.equal((await call(e, '/api/case', { method: 'POST', data: { id, status: 'done' } })).status, 200);
  assert.equal((await (await call(e, '/api/state')).json()).cases[0].status, 'done');
  assert.equal((await call(e, '/api/case', { method: 'POST', data: { id, status: 'finished' } })).status, 400);
});

test('builds in parts: start, parts, finish record the uploader\'s size and hash; the download is the whole zip', async () => {
  const e = env();
  const zip = new Uint8Array(23 * 1024).map((_, i) => (i * 31) & 255);
  const sha = await sha256(zip);
  const name = 'EDF6VehicleCrew-0.8.0-build.abc1234.zip';
  assert.equal((await call(e, `/api/build/start?name=${name}`, { user: 'alice', method: 'POST', data: {} })).status, 403);
  const { upload_id: up } = await (await call(e, `/api/build/start?name=${name}`, { method: 'POST', data: {} })).json();
  const parts = [];
  for (let n = 1, at = 0; at < zip.length; n++, at += 10 * 1024) {
    const res = await call(e, `/api/build/part?name=${name}&upload_id=${up}&part=${n}`, { method: 'PUT', raw: zip.slice(at, at + 10 * 1024) });
    assert.equal(res.status, 200);
    parts.push(await res.json());
  }
  assert.equal(parts.length, 3);
  const fin = await call(e, `/api/build/finish?name=${name}&version=0.8.0-build.abc1234&channel=nightly&commit=abc1234&upload_id=${up}`, {
    method: 'POST', data: { parts, size: zip.length, sha256: sha }, headers: { 'x-notes': encodeURIComponent('main abc1234\n说明') },
  });
  assert.equal(fin.status, 200);
  assert.deepEqual(await fin.json(), { name, sha256: sha, size: zip.length });
  const latest = await (await call(e, '/api/latest?channel=nightly', { user: 'alice' })).json();
  assert.equal(latest.sha256, sha);
  assert.equal(latest.notes, 'main abc1234\n说明');
  const dl = await call(e, '/dl/' + name, { user: 'alice' });
  assert.equal(await sha256(new Uint8Array(await dl.arrayBuffer())), sha);
});

test('builds in parts: a size that does not add up is discarded; bad requests are refused', async () => {
  const e = env();
  const name = 'x.zip';
  const { upload_id: up } = await (await call(e, `/api/build/start?name=${name}`, { method: 'POST', data: {} })).json();
  const part = await (await call(e, `/api/build/part?name=${name}&upload_id=${up}&part=1`, { method: 'PUT', raw: new Uint8Array(100) })).json();
  const fin = await call(e, `/api/build/finish?name=${name}&version=v&upload_id=${up}`, {
    method: 'POST', data: { parts: [part], size: 101, sha256: 'a'.repeat(64) },
  });
  assert.equal(fin.status, 400);
  assert.equal(e.FILES.objects.has('builds/x.zip'), false);
  assert.equal((await (await call(e, '/api/state')).json()).builds.length, 0);
  assert.equal((await call(e, `/api/build/start?name=x.exe`, { method: 'POST', data: {} })).status, 400);
  assert.equal((await call(e, `/api/build/part?name=${name}&upload_id=${up}&part=0`, { method: 'PUT', raw: new Uint8Array(1) })).status, 400);
  assert.equal((await call(e, `/api/build/part?name=${name}&part=1`, { method: 'PUT', raw: new Uint8Array(1) })).status, 400);
  const bad = await call(e, `/api/build/finish?name=${name}&version=v&upload_id=${up}`, { method: 'POST', data: { parts: [part], size: 100, sha256: 'xyz' } });
  assert.equal(bad.status, 400);
  const { upload_id: up2 } = await (await call(e, `/api/build/start?name=${name}`, { method: 'POST', data: {} })).json();
  assert.equal((await call(e, `/api/build/abort?name=${name}&upload_id=${up2}`, { method: 'POST', data: {} })).status, 200);
  assert.equal(e.FILES.uploads.has(up2), false);
});

test('migration 0001 adds the handling status to a database made before it, keeping the reports', () => {
  const db = new DatabaseSync(':memory:');
  const schema = readFileSync(new URL('../schema.sql', import.meta.url), 'utf8');
  const before = schema.replace(/,\n\s*status TEXT NOT NULL DEFAULT 'open'[^\n]*\n\s*status_at INTEGER\n/, '\n');
  assert.notEqual(before, schema);
  db.exec(before);
  db.prepare("INSERT INTO reports (author, result, note, at) VALUES ('t', 'fail', 'old', 1)").run();
  db.exec(readFileSync(new URL('../migrations/0001-report-status.sql', import.meta.url), 'utf8'));
  const r = db.prepare('SELECT * FROM reports').get();
  assert.equal(r.note, 'old');
  assert.equal(r.status, 'open');
  assert.equal(r.status_at, null);
  const cols = (sql) => sql.prepare("SELECT name FROM pragma_table_info('reports') ORDER BY name").all().map((c) => c.name);
  const fresh = new DatabaseSync(':memory:');
  fresh.exec(schema);
  assert.deepEqual(cols(db), cols(fresh));
});
