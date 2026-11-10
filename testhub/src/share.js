// The public download of the share packages (/download): the one place on the site that needs no login.
// Two slots, each with its newest version: `main` (the main package, ~600 MB) and `models` (the model pack, which the
// main package names by version in models-required.json). A version holds one or more files: the model pack comes in
// several zips (...-1of2.zip, ...-2of2.zip: a QQ group takes at most 4 GB a file; models-required.json's model_packs
// lists them), all of which the player downloads and extracts into the same folder. Each file is kept the way fushi.moe
// keeps its recommended pack: cut into fixed-size parts, each with its SHA-256, listed in the manifest. The page
// (public/download.js, with fushi.moe's chunked-download.mjs / pack-download.mjs copied as they are) fetches every part
// over several Range connections, checks it, then writes it into the file the visitor chose. R2 keys:
//
//   share/<slot>/latest.json              the slot's newest manifest (written last: publishing is this one put)
//   share/<slot>/<version>/manifest.json  that version's manifest: {slot, version, notes, published_at, files: [...]}
//   share/<slot>/<version>/<k>/part-NNN   the parts of its file k (0, 1, ...: the order of `files`)
//
// Public paths: /share/<slot>/manifest.json (rolling, no-store), /share/<slot>/<version>/<k>/part-NNN (Range,
// immutable) and /share/<slot>/<version>/<k>/file (file k's parts as one stream, for download managers and browsers
// without the save dialog). A publish deletes every other version of the slot (R2 space): a download of the old one
// still running fails, and the page offers the new one on reload.
import { HttpError, body } from './http.js';

export const SLOTS = ['main', 'models'];
const SAFE = /^[A-Za-z0-9._-]{1,120}$/;
const PART = /^part-\d{3}$/;
const HEX64 = /^[0-9a-f]{64}$/;
const FILE = /^(0|[1-9]\d?)$/;                // a version's file index, as in the keys and paths
const MAX_FILES = 20;

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
  const seg = path.split('/').filter(Boolean);
  if (seg[0] !== 'share' || !SLOTS.includes(seg[1])) return null;
  const slot = seg[1];
  if (seg.length === 3 && seg[2] === 'manifest.json') {
    const obj = await env.FILES.get(`share/${slot}/latest.json`);
    if (!obj) throw new HttpError(404, 'nothing published yet');
    return new Response(method === 'HEAD' ? null : obj.body, {
      headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' },
    });
  }
  if (seg.length !== 5 || !safe(seg[2]) || !FILE.test(seg[3])) return null;
  const [, , version, k, name] = seg;
  if (PART.test(name)) return servePart(request, env, `share/${slot}/${version}/${k}/${name}`);
  if (name === 'file') return serveWhole(request, env, `share/${slot}/${version}`, Number(k));
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

// File k's parts in order as one download (no Range: a download manager restarts it, the page never uses it).
async function serveWhole(request, env, dir, k) {
  const obj = await env.FILES.get(`${dir}/manifest.json`);
  if (!obj) throw new HttpError(404, 'not found');
  const m = (JSON.parse(await new Response(obj.body).text()).files || [])[k];
  if (!m) throw new HttpError(404, 'not found');
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
      const part = await env.FILES.get(`${dir}/${k}/${p.name}`);
      if (!part) throw new Error('missing ' + p.name);
      await part.body.pipeTo(writable, { preventClose: true });
    }
    await writable.close();
  })().catch((e) => writable.abort(e).catch(() => {}));
  return new Response(readable, { headers });
}

// ---------------------------------------------------------------- developer writes (behind the login)

function slotOf(value) {
  if (!SLOTS.includes(value)) throw new HttpError(400, 'slot=' + SLOTS.join('|') + ' required');
  return value;
}

function partParams(url) {
  const q = url.searchParams;
  const slot = slotOf(q.get('slot'));
  const version = q.get('version') || '';
  const file = q.get('file') || '';
  const name = q.get('name') || '';
  const sha256 = (q.get('sha256') || '').toLowerCase();
  if (!safe(version) || !FILE.test(file) || Number(file) >= MAX_FILES || !PART.test(name) || !HEX64.test(sha256)) {
    throw new HttpError(400, `version=[A-Za-z0-9._-], file=0..${MAX_FILES - 1}, name=part-NNN and sha256= required`);
  }
  return { slot, version, file, name, sha256 };
}

// GET /api/share/part?(same query as the PUT): does R2 already hold this part with this hash? The client asks before
// sending the bytes, so a resumed upload over a slow link does not send the parts already there again.
export async function headSharePart(request, env, url) {
  const { slot, version, file, name, sha256 } = partParams(url);
  const key = `share/${slot}/${version}/${file}/${name}`;
  const old = await env.FILES.head(key);
  if (!old) return { name, existed: false };
  const had = old.checksums?.sha256 ? hex(old.checksums.sha256) : '';
  if (had !== sha256) throw new HttpError(409, `${key} already holds other bytes; publish under a new version`);
  return { name, existed: true, size: old.size };
}

// PUT /api/share/part?slot=&version=&file=k&name=part-NNN&sha256=: one part of the version's file k, streamed into R2,
// which checks it against the SHA-256. A part already there with the same hash answers ok, so a publish cut off half
// way is simply run again.
export async function putSharePart(request, env, url) {
  const { slot, version, file, name, sha256 } = partParams(url);
  const length = Number(request.headers.get('content-length') || 0);
  if (!length) throw new HttpError(400, 'content-length required');
  const key = `share/${slot}/${version}/${file}/${name}`;
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

// POST /api/share/publish {slot, version, notes, files: [{name, size_bytes, sha256, parts: [{name, offset, length, sha256}]}]}:
// checks each file's parts end to end and against what R2 holds (files[k] under <version>/<k>/), then switches the
// slot's latest.json to the version, all its files at once.
export async function publishShare(request, env) {
  const m = await body(request);
  const slot = slotOf(m.slot);
  const dir = `share/${slot}`;
  const version = String(m.version || '');
  if (!safe(version)) throw new HttpError(400, 'version=[A-Za-z0-9._-] required');
  if (!Array.isArray(m.files) || !m.files.length || m.files.length > MAX_FILES) {
    throw new HttpError(400, `files (1..${MAX_FILES}) required`);
  }
  const files = [];
  const names = new Set();
  for (const [k, f] of m.files.entries()) {
    const file = await checkFile(env, `${dir}/${version}/${k}`, f, `files[${k}]`);
    if (names.has(file.name.toLowerCase())) throw new HttpError(400, `files[${k}]: ${file.name} listed twice`);
    names.add(file.name.toLowerCase());
    files.push({ ...file, part_base_urls: [`/${dir}/${version}/${k}`], url: `/${dir}/${version}/${k}/file` });
  }

  const manifest = { slot, version, notes: String(m.notes || '').slice(0, 20000), published_at: Date.now(), files };
  const text = JSON.stringify(manifest);
  await env.FILES.put(`${dir}/${version}/manifest.json`, text, { httpMetadata: { contentType: 'application/json' } });
  await env.FILES.put(`${dir}/latest.json`, text, { httpMetadata: { contentType: 'application/json' } });
  const removed = await pruneShare(env, dir, new Set([version]));
  return {
    slot, version, removed,
    files: files.map((f) => ({ name: f.name, size_bytes: f.size_bytes, sha256: f.sha256, parts: f.parts.length })),
  };
}

// One file of a publish: its name, size, whole SHA-256 (the uploader's: the Worker's CPU budget cannot hash GBs) and
// its parts, contiguous from 0 to size_bytes, each held in R2 under `prefix` with that length and hash.
async function checkFile(env, prefix, f, what) {
  if (!f || typeof f !== 'object') throw new HttpError(400, `${what}: not an object`);
  const name = String(f.name || '').split(/[\\/]/).pop().slice(0, 200);
  if (!/\.zip$/i.test(name)) throw new HttpError(400, `${what}: name=<file>.zip required`);
  const sizeBytes = Number(f.size_bytes);
  const sha256 = String(f.sha256 || '');
  if (!Number.isSafeInteger(sizeBytes) || sizeBytes <= 0 || !HEX64.test(sha256)) {
    throw new HttpError(400, `${what}: size_bytes and sha256 required`);
  }
  if (!Array.isArray(f.parts) || !f.parts.length || f.parts.length > 999) throw new HttpError(400, `${what}: parts required`);
  const parts = [];
  let at = 0;
  for (const [i, p] of f.parts.entries()) {
    const part = { name: String(p.name), offset: Number(p.offset), length: Number(p.length), sha256: String(p.sha256) };
    if (part.name !== `part-${String(i).padStart(3, '0')}` || part.offset !== at || !(part.length > 0) || !HEX64.test(part.sha256)) {
      throw new HttpError(400, `${what}.parts[${i}] out of order, not contiguous or without sha256`);
    }
    const held = await env.FILES.head(`${prefix}/${part.name}`);
    if (!held || held.size !== part.length) throw new HttpError(409, `${prefix}/${part.name} missing or not ${part.length} bytes`);
    if (held.checksums?.sha256 && hex(held.checksums.sha256) !== part.sha256) {
      throw new HttpError(409, `${prefix}/${part.name}: sha256 differs from the upload`);
    }
    parts.push(part);
    at += part.length;
  }
  if (at !== sizeBytes) throw new HttpError(400, `${what}: parts cover ${at} bytes, not ${sizeBytes}`);
  return { name, size_bytes: sizeBytes, sha256, parts };
}

// Every version of the slot but the kept one (the new one) leaves R2, uploads left half way included.
async function pruneShare(env, dir, keep) {
  const removed = [];
  let cursor;
  const keys = [];
  do {
    const page = await env.FILES.list({ prefix: dir + '/', cursor });
    for (const o of page.objects) {
      const v = o.key.slice(dir.length + 1).split('/')[0];
      if (o.key !== `${dir}/latest.json` && !keep.has(v)) {
        keys.push(o.key);
        if (!removed.includes(v)) removed.push(v);
      }
    }
    cursor = page.truncated ? page.cursor : undefined;
  } while (cursor);
  for (let i = 0; i < keys.length; i += 1000) await env.FILES.delete(keys.slice(i, i + 1000));
  return removed;
}
