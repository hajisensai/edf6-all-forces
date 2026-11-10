// node --test test/share.test.mjs  (Node 22.5+: node:sqlite)
// The public download page's side of the worker (src/share.js): parts uploaded with their hash, the publish of a
// version with one or more files (the model pack's ...-1of2 / -2of2 zips), Range reads, the whole-file stream, pruning,
// and fushi.moe's page code (public/*.mjs) downloading through all of it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { d1 } from './d1.mjs';
import worker from '../src/worker.js';
import { parseRange } from '../src/share.js';
import { createMemorySink } from '../public/chunked-download.mjs';
import { downloadPack, preferredPartBaseUrl, validatePackManifest } from '../public/pack-download.mjs';

const hexOf = (buf) => [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');
const sha256 = async (bytes) => hexOf(await crypto.subtle.digest('SHA-256', bytes));

// R2 as share.js uses it: head / get (with range) / put (sha256 checked, If-None-Match) / list / delete (one or many).
function r2() {
  const objects = new Map();
  const meta = (key, v) => ({ key, size: v.bytes.length, httpEtag: '"' + v.sha + '"', checksums: { sha256: Buffer.from(v.sha, 'hex') } });
  return {
    objects,
    async head(key) { const v = objects.get(key); return v ? meta(key, v) : null; },
    async get(key, opts = {}) {
      const v = objects.get(key);
      if (!v) return null;
      const r = opts.range;
      const bytes = r ? v.bytes.slice(r.offset, r.offset + r.length) : v.bytes;
      return { ...meta(key, v), body: new Response(bytes).body };
    },
    async put(key, data, options = {}) {
      const bytes = new Uint8Array(typeof data === 'string' ? new TextEncoder().encode(data) : await new Response(data).arrayBuffer());
      if (options.onlyIf?.get('If-None-Match') === '*' && objects.has(key)) return null;
      const sha = await sha256(bytes);
      if (options.sha256 && options.sha256 !== sha) throw new Error('put: sha256 mismatch');
      objects.set(key, { bytes, sha });
      return meta(key, objects.get(key));
    },
    async list({ prefix }) {
      return { objects: [...objects.keys()].filter((k) => k.startsWith(prefix)).map((key) => ({ key })), truncated: false };
    },
    async delete(keys) { for (const k of [].concat(keys)) objects.delete(k); },
  };
}

const env = () => ({
  ACCOUNTS: 'alice:pw:tester,dev:pw:dev', SESSION_SECRET: 's', DB: d1(), FILES: r2(),
  ASSETS: { fetch: (req) => new Response('asset ' + new URL(req.url).pathname) },
});
const call = (e, path, { user, method = 'GET', data, raw, headers = {} } = {}) => worker.fetch(new Request('https://edf6.test' + path, {
  method,
  headers: {
    ...(user ? { authorization: 'Basic ' + btoa(user + ':pw') } : {}),
    ...(data ? { 'content-type': 'application/json' } : {}),
    ...(raw ? { 'content-length': String(raw.length) } : {}),
    ...headers,
  },
  body: data ? JSON.stringify(data) : raw,
  duplex: 'half',
}), e);

// What tools/testhub.py share does: cut each file, upload each part with its hash, then one publish for all of them.
// `zips`: one Uint8Array, or a list of [name, Uint8Array].
async function share(e, zips, version, partSize, name = 'EDF6-AllForces+CoopFix-test.zip', slot = 'main') {
  const files = [];
  for (const [k, [fname, zip]] of (zips instanceof Uint8Array ? [[name, zips]] : zips).entries()) {
    const parts = [];
    for (let n = 0, at = 0; at < zip.length; n++, at += partSize) {
      const data = zip.slice(at, at + partSize);
      const part = { name: `part-${String(n).padStart(3, '0')}`, offset: at, length: data.length, sha256: await sha256(data) };
      const res = await call(e, `/api/share/part?slot=${slot}&version=${version}&file=${k}&name=${part.name}&sha256=${part.sha256}`, { user: 'dev', method: 'PUT', raw: data });
      assert.equal(res.status, 200, await res.clone().text());
      parts.push(part);
    }
    files.push({ name: fname, size_bytes: zip.length, sha256: await sha256(zip), parts });
  }
  return call(e, '/api/share/publish', { user: 'dev', method: 'POST', data: { slot, version, notes: '说明', files } });
}

const zipOf = (n, seed = 31) => new Uint8Array(n).map((_, i) => (i * seed + (i >> 8)) & 255);

test('range parsing: the forms the page and download managers send', () => {
  assert.equal(parseRange(null, 100), null);
  assert.deepEqual(parseRange('bytes=0-9', 100), { offset: 0, length: 10 });
  assert.deepEqual(parseRange('bytes=90-', 100), { offset: 90, length: 10 });
  assert.deepEqual(parseRange('bytes=95-200', 100), { offset: 95, length: 5 });
  assert.deepEqual(parseRange('bytes=-10', 100), { offset: 90, length: 10 });
  for (const bad of ['bytes=100-', 'bytes=5-2', 'bytes=-0', 'bytes=-', 'items=0-1', 'bytes=0-1,4-5']) assert.equal(parseRange(bad, 100), 'bad', bad);
});

test('the page, its scripts, the manifest and the parts need no login; uploading does, and only for developers', async () => {
  const e = env();
  for (const p of ['/download', '/download.html', '/download.js', '/chunked-download.mjs', '/pack-download.mjs']) {
    assert.equal((await call(e, p)).status, 200, p);
  }
  assert.equal((await call(e, '/share/main/manifest.json')).status, 404);
  assert.equal((await call(e, '/share/other/manifest.json')).status, 404);
  assert.equal((await call(e, '/index.html')).status, 303);
  const data = zipOf(10);
  const q = `/api/share/part?slot=main&version=v1&file=0&name=part-000&sha256=${await sha256(data)}`;
  assert.equal((await call(e, q, { method: 'PUT', raw: data })).status, 401);
  assert.equal((await call(e, q, { user: 'alice', method: 'PUT', raw: data })).status, 403);
  assert.equal((await call(e, q, { user: 'dev', method: 'PUT', raw: data })).status, 200);
  assert.equal((await call(e, `/api/share/part?slot=main&version=../x&file=0&name=part-000&sha256=${await sha256(data)}`, { user: 'dev', method: 'PUT', raw: data })).status, 400);
  assert.equal((await call(e, `/api/share/part?slot=main&version=v1&file=0&name=manifest.json&sha256=${await sha256(data)}`, { user: 'dev', method: 'PUT', raw: data })).status, 400);
  for (const file of ['', '01', '-1', '20', '../0', 'x']) {
    assert.equal((await call(e, `/api/share/part?slot=main&version=v1&file=${file}&name=part-000&sha256=${await sha256(data)}`, { user: 'dev', method: 'PUT', raw: data })).status, 400, file);
  }
});

test('a part is accepted again only with the same bytes; the publish refuses gaps, missing parts and wrong totals', async () => {
  const e = env();
  const data = zipOf(1000);
  const sha = await sha256(data);
  const put = (bytes, h = sha, file = 0) => call(e, `/api/share/part?slot=main&version=v1&file=${file}&name=part-000&sha256=${h}`, { user: 'dev', method: 'PUT', raw: bytes });
  assert.equal((await put(data)).status, 200);
  assert.equal((await (await put(data)).json()).existed, true);
  const other = zipOf(1000, 7);
  assert.equal((await put(other, await sha256(other))).status, 409);
  // GET asks without sending: unknown part, held part, another hash, and developers only.
  const ask = (name, h = sha, user = 'dev') => call(e, `/api/share/part?slot=main&version=v1&file=0&name=${name}&sha256=${h}`, { user });
  assert.deepEqual(await (await ask('part-009')).json(), { name: 'part-009', existed: false });
  assert.deepEqual(await (await ask('part-000')).json(), { name: 'part-000', existed: true, size: 1000 });
  assert.equal((await ask('part-000', await sha256(other))).status, 409);
  assert.equal((await ask('part-000', sha, null)).status, 401);
  assert.equal((await ask('part-000', sha, 'alice')).status, 403);
  const publish = (parts, size = 1000, more = []) => call(e, '/api/share/publish', {
    user: 'dev', method: 'POST', data: { slot: 'main', version: 'v1', files: [{ name: 'a.zip', size_bytes: size, sha256: sha, parts }, ...more] },
  });
  const p0 = { name: 'part-000', offset: 0, length: 1000, sha256: sha };
  assert.equal((await publish([p0], 2000)).status, 400);
  assert.equal((await publish([p0, { ...p0, name: 'part-001', offset: 1000 }], 2000)).status, 409);   // not uploaded
  assert.equal((await publish([{ ...p0, offset: 5 }])).status, 400);
  assert.equal((await publish([{ ...p0, sha256: await sha256(other) }])).status, 409);
  // A second file is checked the same way, under its own index: file 1's part 0 is not file 0's.
  const f1 = { name: 'b.zip', size_bytes: 1000, sha256: sha, parts: [p0] };
  assert.equal((await publish([p0], 1000, [f1])).status, 409);
  assert.equal((await call(e, '/api/share/publish', { user: 'dev', method: 'POST', data: { slot: 'main', version: 'v1', files: [] } })).status, 400);
  assert.equal((await e.FILES.head('share/main/latest.json')), null);
  assert.equal((await put(data, sha, 1)).status, 200);
  assert.equal((await publish([p0], 1000, [{ ...f1, name: 'A.zip' }])).status, 400);   // the same name twice
  assert.equal((await publish([p0], 1000, [f1])).status, 200);
  assert.equal((await publish([p0])).status, 200);
});

test('published: manifest, Range reads of a part, and the whole file stream', async () => {
  const e = env();
  const zip = zipOf(25_000);
  const res = await share(e, zip, 'v1', 10_000);
  assert.equal(res.status, 200, await res.clone().text());
  const m = await (await call(e, '/share/main/manifest.json')).json();
  assert.equal(m.version, 'v1');
  assert.equal(m.notes, '说明');
  assert.equal(m.files.length, 1);
  assert.equal(m.files[0].parts.length, 3);
  assert.deepEqual(m.files[0].part_base_urls, ['/share/main/v1/0']);
  assert.equal(m.files[0].url, '/share/main/v1/0/file');

  const r = await call(e, '/share/main/v1/0/part-001', { headers: { range: 'bytes=100-199' } });
  assert.equal(r.status, 206);
  assert.equal(r.headers.get('content-range'), 'bytes 100-199/10000');
  assert.equal(r.headers.get('content-length'), '100');
  assert.deepEqual(new Uint8Array(await r.arrayBuffer()), zip.slice(10_100, 10_200));
  assert.equal((await call(e, '/share/main/v1/0/part-002', { headers: { range: 'bytes=5000-' } })).status, 416);
  const full = await call(e, '/share/main/v1/0/part-002');
  assert.equal(full.status, 200);
  assert.equal((await full.arrayBuffer()).byteLength, 5000);
  assert.equal((await call(e, '/share/main/v1/0/part-009')).status, 404);
  assert.equal((await call(e, '/share/main/v1/1/part-000')).status, 404);
  assert.equal((await call(e, '/share/main/v1/1/file')).status, 404);
  assert.equal((await call(e, '/share/main/v1/part-000')).status, 404);       // parts sit under their file's index
  assert.equal((await call(e, '/share/main/v1/manifest.json')).status, 404);   // only the rolling one is served

  const whole = await call(e, '/share/main/v1/0/file');
  assert.equal(whole.status, 200);
  assert.match(whole.headers.get('content-disposition'), /EDF6-AllForces%2BCoopFix-test\.zip/);
  assert.deepEqual(new Uint8Array(await whole.arrayBuffer()), zip);
});

test('a version of several files (the model pack\'s 1of2 / 2of2): one manifest lists them in order, each read on its own', async () => {
  const e = env();
  const one = zipOf(25_000);
  const two = zipOf(7_000, 13);
  const res = await share(e, [['models-1of2.zip', one], ['models-2of2.zip', two]], 'm1', 10_000, '', 'models');
  assert.equal(res.status, 200, await res.clone().text());
  const out = await res.json();
  assert.deepEqual(out.files.map((f) => [f.name, f.size_bytes, f.parts]), [['models-1of2.zip', 25_000, 3], ['models-2of2.zip', 7_000, 1]]);
  const m = await (await call(e, '/share/models/manifest.json')).json();
  assert.equal(m.version, 'm1');
  assert.deepEqual(m.files.map((f) => f.name), ['models-1of2.zip', 'models-2of2.zip']);
  assert.equal(m.files[1].sha256, await sha256(two));
  assert.deepEqual(m.files[1].part_base_urls, ['/share/models/m1/1']);
  const r = await call(e, '/share/models/m1/1/part-000', { headers: { range: 'bytes=10-19' } });
  assert.equal(r.status, 206);
  assert.deepEqual(new Uint8Array(await r.arrayBuffer()), two.slice(10, 20));
  const whole = await call(e, '/share/models/m1/1/file');
  assert.match(whole.headers.get('content-disposition'), /models-2of2\.zip/);
  assert.deepEqual(new Uint8Array(await whole.arrayBuffer()), two);
  assert.deepEqual(new Uint8Array(await (await call(e, '/share/models/m1/0/file')).arrayBuffer()), one);
});

test('a new publish deletes the other versions of its slot', async () => {
  const e = env();
  assert.equal((await share(e, zipOf(2000), 'm1', 1000, 'models.zip', 'models')).status, 200);
  for (const v of ['v1', 'v2', 'v3']) assert.equal((await share(e, zipOf(3000), v, 1000)).status, 200);
  const versions = new Set([...e.FILES.objects.keys()].map((k) => k.split('/').slice(1, 3).join('/')));
  assert.deepEqual([...versions].sort(), ['main/latest.json', 'main/v3', 'models/latest.json', 'models/m1']);
  assert.equal((await (await call(e, '/share/main/manifest.json')).json()).version, 'v3');
  assert.equal((await (await call(e, '/share/models/manifest.json')).json()).files[0].name, 'models.zip');
  const data = zipOf(10);
  assert.equal((await call(e, `/api/share/part?slot=other&version=x&file=0&name=part-000&sha256=${await sha256(data)}`, { user: 'dev', method: 'PUT', raw: data })).status, 400);
});

test('fushi.moe\'s page code downloads each file through the worker, part by part, checked', async () => {
  const e = env();
  const zips = [['p-1of2.zip', zipOf(3 * 1024 * 1024 + 123)], ['p-2of2.zip', zipOf(1024 * 1024 + 5, 11)]];
  assert.equal((await share(e, zips, 'v1', 1024 * 1024, '', 'models')).status, 200);
  const realFetch = globalThis.fetch;
  globalThis.fetch = (url, init) => worker.fetch(new Request(new URL(url, 'https://edf6.test'), init), e);
  try {
    // What public/download.js does: the version's manifest, then each file as fushi.moe's pack manifest.
    const raw = await (await fetch('/share/models/manifest.json')).json();
    for (const [k, file] of raw.files.entries()) {
      const manifest = validatePackManifest({ ...file, version: raw.version });
      const sink = createMemorySink(manifest.sizeBytes);
      const out = await downloadPack({ manifest, sink, baseUrl: preferredPartBaseUrl(manifest, 'https://edf6.test') });
      assert.equal(out.parts, [4, 2][k]);
      assert.equal(await sha256(sink.bytes()), manifest.sha256);
      assert.equal(await sha256(sink.bytes()), await sha256(zips[k][1]));
    }
  } finally {
    globalThis.fetch = realFetch;
  }
});
