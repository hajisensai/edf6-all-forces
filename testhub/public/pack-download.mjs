/**
 * 推荐包（约 9.5 GB）的网页端下载编排。
 *
 * 为什么不能直接丢给 downloadChunked 一次搞定：推荐包在服务端**不是一个文件**，
 * 而是 39 个 256 MiB 的物理分片（GitHub Release 单资产上限 2 GB，整包放不上去）。
 * downloadChunked 的模型是「一个文件 + 多个等价镜像，按 Range 切」，这里是
 * 「多个互不重叠的片段，按顺序拼」——两者不是一回事。本模块只做编排与校验，
 * 每个分片内部仍然交给 downloadChunked 去开多连接。
 *
 * 校验策略：**逐片校验，不做整包重读**。整包 9.5 GB 塞不进 crypto.subtle.digest
 * （它要一次性的 ArrayBuffer），而清单本来就给了每片 sha256，逐片校验的覆盖面是
 * 一样的。所以每片先落到内存 sink、算完 sha256 再按 offset 写进磁盘 sink：
 * 校验不过的片一个字节都不会落盘。峰值内存 = 一个分片（256 MiB）。
 *
 * 不做的事：跨刷新续传。文件句柄是用户在保存对话框里给的，页面一关就没了，
 * 无法在下次访问时接着写——需要断点续传就走 app 引导里的下载器
 * （fushi/lib/src/utils/misc/segmented_downloader.dart，进度绑 planVersion + sha256）。
 */

import { createMemorySink, downloadChunked, sha256Hex } from './chunked-download.mjs';

/** 清单的滚动地址（Worker 路由代理到 fushi-pack 最新 release），短 TTL，不 immutable。 */
export const PACK_MANIFEST_PATH = '/pack/manifest.json';

/** 拼好之后给用户的文件名；app 的备份导入按扩展名认。 */
export const PACK_FILENAME = 'fushi-recommended.fushi.zip';

/**
 * @typedef {{ name: string, offset: number, length: number, sha256: string }} PackPart
 * @typedef {{ version: string, sizeBytes: number, sha256: string, parts: PackPart[], partBaseUrls: string[], wholeUrl: string | null }} PackManifest
 */

/**
 * @param {unknown} value
 * @param {string} what
 * @returns {number}
 */
function requireSafeInt(value, what) {
  if (!Number.isSafeInteger(value) || /** @type {number} */ (value) < 0) {
    throw new TypeError(`pack manifest: ${what} must be an integer >= 0, got ${String(value)}`);
  }
  return /** @type {number} */ (value);
}

/**
 * @param {unknown} value
 * @param {string} what
 * @returns {string}
 */
function requireHex64(value, what) {
  if (typeof value !== 'string' || !/^[0-9a-f]{64}$/i.test(value)) {
    throw new TypeError(`pack manifest: ${what} must be a 64-char hex sha256, got ${String(value)}`);
  }
  return value.toLowerCase();
}

/**
 * 校验并归一化清单。故意严格：分片必须首尾相接且总长与 size_bytes 一致——
 * 一份对不上的清单会让下载「成功」地拼出一个坏文件，用户要到 app 导入时才发现。
 * @param {unknown} raw
 * @returns {PackManifest}
 */
export function validatePackManifest(raw) {
  if (!raw || typeof raw !== 'object') throw new TypeError('pack manifest: not an object');
  const obj = /** @type {Record<string, unknown>} */ (raw);

  const version = typeof obj.version === 'string' && obj.version.length > 0 ? obj.version : '';
  if (!version) throw new TypeError('pack manifest: version required');

  const sizeBytes = requireSafeInt(obj.size_bytes, 'size_bytes');
  const sha256 = requireHex64(obj.sha256, 'sha256');

  const rawParts = obj.parts;
  if (!Array.isArray(rawParts) || rawParts.length === 0) throw new TypeError('pack manifest: parts must be a non-empty array');

  /** @type {PackPart[]} */
  const parts = [];
  let cursor = 0;
  for (let i = 0; i < rawParts.length; i += 1) {
    const p = /** @type {Record<string, unknown>} */ (rawParts[i]);
    if (!p || typeof p !== 'object') throw new TypeError(`pack manifest: parts[${i}] is not an object`);
    const name = typeof p.name === 'string' && p.name.length > 0 ? p.name : '';
    if (!name) throw new TypeError(`pack manifest: parts[${i}].name required`);
    if (name.includes('/') || name.includes('\\')) throw new TypeError(`pack manifest: parts[${i}].name must be a bare file name`);
    const offset = requireSafeInt(p.offset, `parts[${i}].offset`);
    const length = requireSafeInt(p.length, `parts[${i}].length`);
    if (length === 0) throw new TypeError(`pack manifest: parts[${i}].length must be > 0`);
    if (offset !== cursor) {
      throw new TypeError(`pack manifest: parts[${i}].offset ${offset} leaves a gap or overlap (expected ${cursor})`);
    }
    parts.push({ name, offset, length, sha256: requireHex64(p.sha256, `parts[${i}].sha256`) });
    cursor += length;
  }
  if (cursor !== sizeBytes) {
    throw new TypeError(`pack manifest: parts cover ${cursor} bytes but size_bytes is ${sizeBytes}`);
  }

  const rawBases = Array.isArray(obj.part_base_urls) ? obj.part_base_urls : [];
  const partBaseUrls = rawBases.filter((u) => typeof u === 'string' && u.length > 0).map((u) => String(u).replace(/\/+$/, ''));
  if (partBaseUrls.length === 0) throw new TypeError('pack manifest: part_base_urls must list at least one base');

  const wholeUrl = typeof obj.url === 'string' && obj.url.length > 0 ? obj.url : null;

  return { version, sizeBytes, sha256, parts, partBaseUrls, wholeUrl };
}

/**
 * 选取分片来源：优先同源的那个 base（fushi.moe 的 /pack Worker 路由）。
 * GitHub release 直链虽然也在清单里，但那是给 IDM / aria2 用的——页面里的 fetch
 * 打它要看 CORS 脸色，同源路由没这个问题，Worker 也已经把 Range 透传了。
 * @param {PackManifest} manifest
 * @param {string} origin 形如 https://fushi.moe
 * @returns {string}
 */
export function preferredPartBaseUrl(manifest, origin) {
  const sameOrigin = manifest.partBaseUrls.find((u) => u.startsWith(origin + '/') || u.startsWith('/'));
  return sameOrigin ?? manifest.partBaseUrls[0];
}

/**
 * @param {string} base
 * @param {PackPart} part
 * @returns {string}
 */
export function packPartUrl(base, part) {
  return base.replace(/\/+$/, '') + '/' + encodeURIComponent(part.name);
}

/**
 * 拉清单。
 * @param {{ url?: string, fetch?: typeof fetch, signal?: AbortSignal }} [opts]
 * @returns {Promise<PackManifest>}
 */
export async function fetchPackManifest(opts = {}) {
  const fetchImpl = opts.fetch ?? globalThis.fetch;
  const url = opts.url ?? PACK_MANIFEST_PATH;
  const res = await fetchImpl(url, { signal: opts.signal, cache: 'no-store' });
  if (!res.ok) throw new Error(`pack manifest: HTTP ${res.status} for ${url}`);
  return validatePackManifest(await res.json());
}

/**
 * @typedef {{ partsDone: number, partsTotal: number, partName: string, bytesDone: number, bytesTotal: number, bytesPerSecond: number }} PackProgress
 */

/**
 * 逐片下载并拼进 sink。成功时 close(sink)，失败/取消时 abort(sink)——
 * 拼了一半的 9.5 GB 文件留在磁盘上比没有更糟（用户会以为它能用）。
 *
 * @param {{
 *   manifest: PackManifest,
 *   sink: { write(position: number, data: Uint8Array): Promise<void>, close(): Promise<void>, abort?(): Promise<void> },
 *   baseUrl: string,
 *   signal?: AbortSignal,
 *   onProgress?: (p: PackProgress) => void,
 *   deps?: { downloadChunked?: typeof downloadChunked, createMemorySink?: typeof createMemorySink, sha256Hex?: typeof sha256Hex },
 * }} job
 * @returns {Promise<{ bytes: number, parts: number }>}
 */
export async function downloadPack(job) {
  const { manifest, sink, baseUrl, signal, onProgress } = job;
  if (!manifest || !Array.isArray(manifest.parts)) throw new TypeError('downloadPack: manifest required');
  if (!sink || typeof sink.write !== 'function' || typeof sink.close !== 'function') {
    throw new TypeError('downloadPack: sink must have write() and close()');
  }
  const deps = job.deps ?? {};
  const runChunked = deps.downloadChunked ?? downloadChunked;
  const makeMemorySink = deps.createMemorySink ?? createMemorySink;
  const hash = deps.sha256Hex ?? sha256Hex;

  let bytesDone = 0;
  try {
    for (let i = 0; i < manifest.parts.length; i += 1) {
      if (signal?.aborted) throw signal.reason ?? new Error('aborted');
      const part = manifest.parts[i];
      const partSink = makeMemorySink(part.length);
      const baseBytes = bytesDone;

      await runChunked({
        sources: [{ id: 'pack', url: packPartUrl(baseUrl, part), label: part.name }],
        size: part.length,
        sink: partSink,
        // 单来源：4 条连接全给它，与 app 包下载同一套调参（同域 6 连接上限内留 2 条给页面）。
        perSourceConcurrency: 4,
        maxTotalConcurrency: 4,
        signal,
        onProgress: (p) => {
          onProgress?.({
            partsDone: i,
            partsTotal: manifest.parts.length,
            partName: part.name,
            bytesDone: baseBytes + p.bytesDone,
            bytesTotal: manifest.sizeBytes,
            bytesPerSecond: p.bytesPerSecond,
          });
        },
      });

      const bytes = partSink.bytes();
      const actual = await hash(bytes);
      if (actual !== part.sha256) {
        throw new Error(`分片 ${part.name} 的 SHA-256 不匹配（期望 ${part.sha256}，实得 ${actual}）`);
      }
      await sink.write(part.offset, bytes);
      bytesDone = baseBytes + part.length;
      onProgress?.({
        partsDone: i + 1,
        partsTotal: manifest.parts.length,
        partName: part.name,
        bytesDone,
        bytesTotal: manifest.sizeBytes,
        bytesPerSecond: 0,
      });
    }
  } catch (err) {
    await sink.abort?.().catch(() => {});
    throw err;
  }

  await sink.close();
  return { bytes: bytesDone, parts: manifest.parts.length };
}
