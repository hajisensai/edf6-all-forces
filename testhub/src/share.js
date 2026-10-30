// The public download of the share package (/download): the one place on the site that needs no login.
// The package (~600 MB) is kept the way fushi.moe keeps its recommended pack: cut into fixed-size parts, each with its
// SHA-256, listed in a manifest. The page (public/download.js, with fushi.moe's chunked-download.mjs / pack-download.mjs
// copied as they are) fetches every part over several Range connections, checks it, then writes it into the file the
// visitor chose. R2 keys:
//
//   share/latest.json              the manifest of the newest package (written last: publishing is this one put)
//   share/<version>/manifest.json  that version's manifest
//   share/<version>/part-NNN       its parts
//
// Public paths: /share/manifest.json (rolling, no-store), /share/<version>/part-NNN (Range, immutable) and
// /share/<version>/file (all parts as one stream, for download managers and browsers without the save dialog).
// A publish keeps the previous version, so a download started before it still finishes; older ones are deleted.
import { HttpError, body } from './http.js';

const SAFE = /^[A-Za-z0-9._-]{1,120}$/;
const PART = /^part-\d{3}$/;
const HEX64 = /^[0-9a-f]{64}$/;

function safe(seg) {
  return SAFE.test(seg) && seg !== '.' && seg !== '..';
}

function hex(buf) {
  return [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// ---------------------------------------------------------------- public reads

export async function shareRoute(request, env, path) {
  const method = request.method;
  if (method !== 'GET' && method !== 'HEAD') return null;
  if (path === '/share/manifest.json') {
    const obj = await env.FILES.get('share/latest.json');
    if (!obj) throw new HttpError(404, 'nothing published yet');
    return new Response(method === 'HEAD' ? null : obj.body, {
      headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' },
    });
  }
  const seg = path.split('/').filter(Boolean);
  if (seg.length !== 3 || seg[0] !== 'share' || !safe(seg[1])) return null;
  const [, version, name] = seg;
  if (PART.test(name)) return servePart(request, env, `share/${version}/${name}`);
  if (name === 'file') return serveWhole(request, env, version);
  return null;
}

// One part, whole or one byte range: the page asks for `bytes=a-b` and insists on 206 with a matching Content-Range.
async function servePart(request, env, key) {
  const head = await env.FILES.head(key);
  if (!head) throw new HttpError(404, 'not found');
  const size = head.size;
  const headers = {
    'content-type': 'application/octet-stream',
    'accept-ranges': 'bytes',
    'cache-control': 'public, max-age=31536000, immutable',
    etag: head.httpEtag,
  };
  const range = parseRange(request.headers.get('range'), size);
  if (range === 'bad') {
    return new Response(null, { status: 416, headers: { ...headers, 'content-range': `bytes */${size}` } });
  }
  const { offset, length } = range || { offset: 0, length: size };
  const obj = request.method === 'HEAD' ? null : await env.FILES.get(key, range ? { range: { offset, length } } : {});
  if (request.method !== 'HEAD' && !obj) throw new HttpError(404, 'not found');
  headers['content-length'] = String(length);
  if (range) headers['content-range'] = `bytes ${offset}-${offset + length - 1}/${size}`;
  return new Response(obj ? obj.body : null, { status: range ? 206 : 200, headers });
}

// `bytes=a-b`, `bytes=a-`, `bytes=-n` (one range); null = no Range header (the whole part); 'bad' = unsatisfiable.
export function parseRange(header, size) {
  if (!header) return null;
  const m = /^bytes=(\d*)-(\d*)$/.exec(header.trim());
  if (!m || (m[1] === '' && m[2] === '')) return 'bad';
  let start;
  let end;
  if (m[1] === '') {
    const n = Number(m[2]);
    if (n === 0) return 'bad';
    start = Math.max(0, size - n);
    end = size - 1;
  } else {
    start = Number(m[1]);
    end = m[2] === '' ? size - 1 : Math.min(Number(m[2]), size - 1);
  }
  if (start >= size || end < start) return 'bad';
  return { offset: start, length: end - start + 1 };
}

// All parts in order as one download (no Range: a download manager restarts it, the page never uses it).
async function serveWhole(request, env, version) {
  const obj = await env.FILES.get(`share/${version}/manifest.json`);
  if (!obj) throw new HttpError(404, 'not found');
  const m = JSON.parse(await new Response(obj.body).text());
  const headers = {
    'content-type': 'application/zip',
    'content-length': String(m.size_bytes),
    'content-disposition': `attachment; filename*=UTF-8''${encodeURIComponent(m.name)}`,
    'cache-control': 'no-store',
  };
  if (request.method === 'HEAD') return new Response(null, { headers });
  // FixedLengthStream (Workers) keeps the Content-Length on the streamed response, so the browser shows progress.
  const { readable, writable } = typeof FixedLengthStream === 'function' ? new FixedLengthStream(m.size_bytes) : new TransformStream();
  (async () => {
    for (const p of m.parts) {
      const part = await env.FILES.get(`share/${version}/${p.name}`);
      if (!part) throw new Error('missing ' + p.name);
      await part.body.pipeTo(writable, { preventClose: true });
    }
    await writable.close();
  })().catch((e) => writable.abort(e).catch(() => {}));
  return new Response(readable, { headers });
}

// ---------------------------------------------------------------- developer writes (behind the login)

// PUT /api/share/part?version=&name=part-NNN&sha256=: one part, streamed into R2, which checks it against the SHA-256.
// A part already there with the same hash answers ok, so a publish cut off half way is simply run again.
export async function putSharePart(request, env, url) {
  const q = url.searchParams;
  const version = q.get('version') || '';
  const name = q.get('name') || '';
  const sha256 = (q.get('sha256') || '').toLowerCase();
  if (!safe(version) || !PART.test(name) || !HEX64.test(sha256)) {
    throw new HttpError(400, 'version=[A-Za-z0-9._-], name=part-NNN and sha256= required');
  }
  const length = Number(request.headers.get('content-length') || 0);
  if (!length) throw new HttpError(400, 'content-length required');
  const key = `share/${version}/${name}`;
  const old = await env.FILES.head(key);
  if (old) {
    const had = old.checksums?.sha256 ? hex(old.checksums.sha256) : '';
    if (had === sha256 && old.size === length) { await request.body?.cancel(); return { name, size: old.size, existed: true }; }
    throw new HttpError(409, `${key} already holds other bytes; publish under a new version`);
  }
  const obj = await env.FILES.put(key, request.body, {
    sha256, httpMetadata: { contentType: 'application/octet-stream' }, onlyIf: new Headers({ 'If-None-Match': '*' }),
  });
  if (!obj) throw new HttpError(409, `${key} was written by another upload`);
  if (obj.size !== length) {
    await env.FILES.delete(key);
    throw new HttpError(400, `${key}: got ${obj.size} bytes, not ${length}: discarded`);
  }
  return { name, size: obj.size };
}

// POST /api/share/publish {version, name, size_bytes, sha256, notes, parts: [{name, offset, length, sha256}]}:
// checks the parts end to end and against what R2 holds, then switches share/latest.json to it.
export async function publishShare(request, env) {
  const m = await body(request);
  const version = String(m.version || '');
  if (!safe(version)) throw new HttpError(400, 'version=[A-Za-z0-9._-] required');
  const name = String(m.name || '').split(/[\\/]/).pop().slice(0, 200);
  if (!/\.zip$/i.test(name)) throw new HttpError(400, 'name=<file>.zip required');
  const sizeBytes = Number(m.size_bytes);
  const sha256 = String(m.sha256 || '');
  if (!Number.isSafeInteger(sizeBytes) || sizeBytes <= 0 || !HEX64.test(sha256)) throw new HttpError(400, 'size_bytes and sha256 required');
  if (!Array.isArray(m.parts) || !m.parts.length || m.parts.length > 999) throw new HttpError(400, 'parts required');
  const parts = [];
  let at = 0;
  for (const [i, p] of m.parts.entries()) {
    const part = { name: String(p.name), offset: Number(p.offset), length: Number(p.length), sha256: String(p.sha256) };
    if (part.name !== `part-${String(i).padStart(3, '0')}` || part.offset !== at || !(part.length > 0) || !HEX64.test(part.sha256)) {
      throw new HttpError(400, `parts[${i}] out of order, not contiguous or without sha256`);
    }
    const held = await env.FILES.head(`share/${version}/${part.name}`);
    if (!held || held.size !== part.length) throw new HttpError(409, `share/${version}/${part.name} missing or not ${part.length} bytes`);
    if (held.checksums?.sha256 && hex(held.checksums.sha256) !== part.sha256) throw new HttpError(409, `${part.name}: sha256 differs from the upload`);
    parts.push(part);
    at += part.length;
  }
  if (at !== sizeBytes) throw new HttpError(400, `parts cover ${at} bytes, not ${sizeBytes}`);

  const manifest = {
    version, name, size_bytes: sizeBytes, sha256, parts,
    part_base_urls: [`/share/${version}`],
    url: `/share/${version}/file`,
    notes: String(m.notes || '').slice(0, 20000),
    published_at: Date.now(),
  };
  const text = JSON.stringify(manifest);
  const prev = await env.FILES.get('share/latest.json');
  const prevVersion = prev ? JSON.parse(await new Response(prev.body).text()).version : '';
  await env.FILES.put(`share/${version}/manifest.json`, text, { httpMetadata: { contentType: 'application/json' } });
  await env.FILES.put('share/latest.json', text, { httpMetadata: { contentType: 'application/json' } });
  const removed = await pruneShare(env, new Set([version, prevVersion]));
  return { version, name, size_bytes: sizeBytes, parts: parts.length, removed };
}

// Every version but the kept ones (the new one and the one it replaced) leaves R2, uploads left half way included.
async function pruneShare(env, keep) {
  const removed = [];
  let cursor;
  const keys = [];
  do {
    const page = await env.FILES.list({ prefix: 'share/', cursor });
    for (const o of page.objects) {
      const v = o.key.split('/')[1];
      if (o.key !== 'share/latest.json' && !keep.has(v)) {
        keys.push(o.key);
        if (!removed.includes(v)) removed.push(v);
      }
    }
    cursor = page.truncated ? page.cursor : undefined;
  } while (cursor);
  for (let i = 0; i < keys.length; i += 1000) await env.FILES.delete(keys.slice(i, i + 1000));
  return removed;
}
