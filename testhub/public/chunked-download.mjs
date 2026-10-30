/**
 * chunked-download.mjs — 零依赖、纯浏览器 ES module 的分片多源下载器。
 *
 * 把一个支持 HTTP Range 的大文件切成分片，从多个同域来源并发拉取，乱序写入 sink 拼成完整文件。
 * 三段式：probeSources（探测可用来源与文件大小）→ createBrowserSink / createMemorySink（落地端）
 * → downloadChunked（分片队列 + 每来源 worker + 全局并发闸 + 进度滑窗）。
 *
 * 不依赖任何 npm 包；只用 fetch / AbortController / performance.now / Blob / File System Access API。
 */

/**
 * @typedef {{ id: string, url: string, label?: string }} DownloadSource
 * @typedef {{ bytesDone: number, bytesTotal: number, bytesPerSecond: number, perSource: Record<string, number>, activeSources: string[], chunksDone: number, chunksTotal: number }} ChunkProgress
 * @typedef {{ write(position: number, data: Uint8Array): Promise<void>, close(): Promise<void>, abort?(): Promise<void>, bytes?(): Uint8Array, file?(): Promise<File> }} ChunkSink
 *   bytes() / file() 是校验用的取数口：内存 sink 有 bytes()，文件系统 sink 有 file()（close 之后才可用）
 * @typedef {{ index: number, start: number, end: number, lastFailedBy: string | null }} Chunk  start/end 都是含端点的字节下标
 * @typedef {{ id: string, url: string, bytes: number, consecutiveFailures: number, dropped: boolean, workers: number, succeed(n: number): void, fail(): void, reset(): void }} SourceState
 */

export const DEFAULT_CHUNK_BYTES = 8 * 1024 * 1024;
export const DEFAULT_PROBE_BYTES = 64 * 1024;
export const DEFAULT_PROBE_TIMEOUT_MS = 8000;
/** 分片看门狗：从发请求到收到首段字节的上限 */
export const DEFAULT_FIRST_BYTE_MS = 10000;
/** 分片看门狗：两段字节之间的上限 */
export const DEFAULT_STALL_MS = 8000;
/**
 * 所有来源都退出后，整轮重开的次数上限。
 *
 * 为什么必须有：maxSourceFailures 表达的是「这个镜像不行了，换别的镜像」——只有在
 * 真有别的镜像时才成立。推荐包每片只有一个来源（清单里的 GitHub 直链没有 CORS 头，
 * 页面里 fetch 不了，只能当 IDM/aria2 的入口），此时「丢弃唯一来源」等于「整包判死」。
 * 更糟的是 perSourceConcurrency 条 worker 共享同一个 consecutiveFailures 计数，且只有
 * 成功才清零：开局 4 条连接同时建连超时，一轮就把 3 次配额烧光，连一次重试都没有。
 * 实测 fushi.moe/pack 的一个 256 MiB 分片，开头 4 个 chunk 连续 CONNECT_TIMEOUT，
 * 而同一次里其余 26 个 chunk 的首字节中位数只有 213ms——是瞬时抖动，不是来源已死。
 */
export const DEFAULT_MAX_RETRIES = 3;
/** 第 n 次重开前的退避基数，实际等待 DEFAULT_RETRY_BACKOFF_MS * 2^n */
export const DEFAULT_RETRY_BACKOFF_MS = 1000;
/** bytesPerSecond 的滑窗宽度 */
const SPEED_WINDOW_MS = 3000;

/**
 * 可被 signal 提前唤醒的等待（唤醒即 resolve，取消由调用方自己判）。
 * @param {number} ms
 * @param {AbortSignal} [signal]
 * @returns {Promise<void>}
 */
function delay(ms, signal) {
  if (ms <= 0) return Promise.resolve();
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, ms);
    signal?.addEventListener('abort', () => {
      clearTimeout(timer);
      resolve();
    }, { once: true });
  });
}

// ---------------------------------------------------------------------------
// 通用小工具
// ---------------------------------------------------------------------------

/** @returns {number} 毫秒时钟（优先单调的 performance.now） */
function nowMs() {
  const perf = globalThis.performance;
  return perf && typeof perf.now === 'function' ? perf.now() : Date.now();
}

/**
 * @param {string} [message]
 * @returns {Error} name === 'AbortError' 的错误（与 DOMException AbortError 同名，方便调用方统一判断）
 */
export function createAbortError(message = 'The operation was aborted') {
  const err = new Error(message);
  err.name = 'AbortError';
  return err;
}

/**
 * @param {unknown} err
 * @returns {boolean}
 */
function isAbortError(err) {
  return typeof err === 'object' && err !== null && /** @type {{ name?: unknown }} */ (err).name === 'AbortError';
}

/**
 * 解析 `Content-Range: bytes a-b/total`。total 为 `*`、格式错误、b < a、total <= b 都返回 null。
 * @param {string | null | undefined} header
 * @returns {{ start: number, end: number, total: number } | null}
 */
export function parseContentRange(header) {
  if (!header) return null;
  const m = /^\s*bytes\s+(\d+)-(\d+)\/(\d+|\*)\s*$/i.exec(header);
  if (!m) return null;
  const start = Number(m[1]);
  const end = Number(m[2]);
  const total = m[3] === '*' ? Number.NaN : Number(m[3]);
  if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) || end < start) return null;
  if (!Number.isSafeInteger(total) || total <= end) return null;
  return { start, end, total };
}

/**
 * 不读 body 直接丢弃响应（非 206 时避免把整文件拉下来）。
 * @param {Response} res
 * @returns {void}
 */
function discardBody(res) {
  try {
    const p = res.body && typeof res.body.cancel === 'function' ? res.body.cancel() : undefined;
    if (p && typeof p.catch === 'function') p.catch(() => {});
  } catch {
    // body 已被消费或不可取消：无事可做
  }
}

/**
 * @param {AbortSignal} signal 已中止的 signal
 * @returns {unknown} 中止原因（老实现没有 reason 时补一个 AbortError）
 */
function abortReason(signal) {
  return signal.reason ?? createAbortError();
}

/**
 * 把外层 signal 的中止转发到本次请求自己的 controller。返回解除转发的函数。
 * @param {AbortSignal | undefined} outer
 * @param {AbortController} ctrl
 * @returns {() => void}
 */
function forwardAbort(outer, ctrl) {
  if (!outer) return () => {};
  const relay = () => ctrl.abort(abortReason(outer));
  if (outer.aborted) {
    relay();
    return () => {};
  }
  outer.addEventListener('abort', relay, { once: true });
  return () => outer.removeEventListener('abort', relay);
}

/**
 * 分片看门狗：arm(ms) 重新计时，到点就中止本次请求（reason 是 name==='TimeoutError' 的 Error）。
 * ms 为 undefined 表示不设限（probeSources 自己有 timeoutMs）。
 * @param {AbortController} ctrl
 * @param {string} url
 */
function createWatchdog(ctrl, url) {
  /** @type {ReturnType<typeof setTimeout> | undefined} */
  let timer;
  return {
    /**
     * @param {number | undefined} ms
     * @param {string} what 超时时写进错误信息的阶段名
     * @returns {void}
     */
    arm(ms, what) {
      clearTimeout(timer);
      timer = undefined;
      if (ms === undefined) return;
      timer = setTimeout(() => {
        const err = new Error(`range request to ${url}: no ${what} within ${ms}ms`);
        err.name = 'TimeoutError';
        ctrl.abort(err);
      }, ms);
    },
    /** @returns {void} */
    clear() {
      clearTimeout(timer);
    },
  };
}

/**
 * 流式读 body 到定长缓冲。ctrl 中止时取消 reader（让挂着的 read() 立刻返回）并抛中止原因；
 * body 比声明长直接失败（不会为了一个坏来源把超量数据吞进内存）。
 * @param {Response} res
 * @param {number} wanted
 * @param {string} url
 * @param {AbortController} ctrl
 * @param {(n: number) => void} onPiece 每收到一段字节调一次
 * @returns {Promise<Uint8Array>}
 */
async function readBodyInto(res, wanted, url, ctrl, onPiece) {
  if (ctrl.signal.aborted) {
    discardBody(res);
    throw abortReason(ctrl.signal);
  }
  if (!res.body || typeof res.body.getReader !== 'function') {
    throw new Error(`range request to ${url}: response has no readable body`);
  }
  const data = new Uint8Array(wanted);
  let received = 0;
  const reader = res.body.getReader();
  const onAbort = () => {
    reader.cancel(abortReason(ctrl.signal)).catch(() => {});
  };
  ctrl.signal.addEventListener('abort', onAbort, { once: true });
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (ctrl.signal.aborted) throw abortReason(ctrl.signal);
      if (done) break;
      if (received + value.byteLength > wanted) {
        throw new Error(`range request to ${url}: body longer than ${wanted} bytes`);
      }
      data.set(value, received);
      received += value.byteLength;
      onPiece(value.byteLength);
    }
  } catch (err) {
    reader.cancel(err).catch(() => {});
    throw err;
  } finally {
    ctrl.signal.removeEventListener('abort', onAbort);
  }
  if (received !== wanted) {
    throw new Error(`range request to ${url}: body ${received} bytes != ${wanted}`);
  }
  return data;
}

/**
 * 发一次 Range 请求并校验：必须 206、Content-Range 与请求一致、total 与预期一致、body 长度正确。
 * 任何一条不满足都 throw（调用方据此判定「该来源此次失败」）。
 * 每次请求有自己的 AbortController：外层 opts.signal 中止时联动中止；看门狗（firstByteMs：发请求到首段字节；
 * stallMs：相邻两段字节之间）到点也中止，同样算本次失败。
 * @param {typeof fetch} fetchImpl
 * @param {string} url
 * @param {number} start 含
 * @param {number} end 含
 * @param {{ expectedTotal?: number, allowClampedEnd?: boolean, signal?: AbortSignal, firstByteMs?: number, stallMs?: number, onBytes?: (n: number) => void }} opts
 *   allowClampedEnd：探测时文件可能比 probeBytes 还小，允许服务端把 end 夹到 total-1。
 *   onBytes：每收到一段字节调一次（进度滑窗用）。
 * @returns {Promise<{ data: Uint8Array, total: number }>}
 */
async function fetchRange(fetchImpl, url, start, end, opts) {
  const ctrl = new AbortController();
  const unlink = forwardAbort(opts.signal, ctrl);
  const watchdog = createWatchdog(ctrl, url);
  try {
    watchdog.arm(opts.firstByteMs, 'first byte');
    const res = await fetchImpl(url, {
      headers: { Range: `bytes=${start}-${end}` },
      signal: ctrl.signal,
      cache: 'no-store',
    });
    const range = validateRangeResponse(res, url, start, end, opts);
    const data = await readBodyInto(res, range.end - range.start + 1, url, ctrl, (n) => {
      watchdog.arm(opts.stallMs, 'progress');
      if (opts.onBytes) opts.onBytes(n);
    });
    return { data, total: range.total };
  } finally {
    watchdog.clear();
    unlink();
  }
}

/**
 * 校验响应头；不满足就丢弃 body 并 throw。
 * @param {Response} res
 * @param {string} url
 * @param {number} start
 * @param {number} end
 * @param {{ expectedTotal?: number, allowClampedEnd?: boolean }} opts
 * @returns {{ start: number, end: number, total: number }}
 */
function validateRangeResponse(res, url, start, end, opts) {
  if (res.status !== 206) {
    discardBody(res);
    throw new Error(`range request to ${url} got ${res.status}, expected 206`);
  }
  const range = parseContentRange(res.headers.get('content-range'));
  if (!range) {
    discardBody(res);
    throw new Error(`range request to ${url}: missing or invalid Content-Range`);
  }
  const clampedOk = opts.allowClampedEnd === true && range.end === range.total - 1 && range.end < end;
  if (range.start !== start || (range.end !== end && !clampedOk)) {
    discardBody(res);
    throw new Error(`range request to ${url}: Content-Range ${range.start}-${range.end} != requested ${start}-${end}`);
  }
  if (opts.expectedTotal !== undefined && range.total !== opts.expectedTotal) {
    discardBody(res);
    throw new Error(`range request to ${url}: total ${range.total} != expected ${opts.expectedTotal}`);
  }
  return range;
}

// ---------------------------------------------------------------------------
// probeSources
// ---------------------------------------------------------------------------

/**
 * 探测单个来源：`Range: bytes=0-(probeBytes-1)`，量耗时。任何失败/超时返回 null。
 * @param {typeof fetch} fetchImpl
 * @param {DownloadSource} source
 * @param {number} probeBytes
 * @param {number} timeoutMs
 * @param {number | undefined} expectedSize
 * @returns {Promise<(DownloadSource & { probeMs: number, total: number }) | null>}
 */
function probeOne(fetchImpl, source, probeBytes, timeoutMs, expectedSize, signal) {
  return new Promise((resolve) => {
    const ctrl = new AbortController();
    const t0 = nowMs();
    let settled = false;
    /** @type {ReturnType<typeof setTimeout>} */
    let timer;
    /**
     * @param {(DownloadSource & { probeMs: number, total: number }) | null} value
     * @returns {void}
     */
    const finish = (value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      if (signal) signal.removeEventListener('abort', onAbort);
      resolve(value);
    };
    /** @returns {void} */
    const onAbort = () => {
      ctrl.abort();
      finish(null);
    };
    timer = setTimeout(onAbort, timeoutMs);
    // 外部 signal（页面上的「取消」）必须能立刻掐断探测，否则取消之后这几个请求还在飞，
    // 调用方只能干等它们自己超时——页面上表现为「点了取消没反应」。
    if (signal) {
      if (signal.aborted) {
        onAbort();
        return;
      }
      signal.addEventListener('abort', onAbort);
    }
    fetchRange(fetchImpl, source.url, 0, Math.max(probeBytes, 1) - 1, {
      expectedTotal: expectedSize,
      allowClampedEnd: true,
      signal: ctrl.signal,
    }).then(
      ({ total }) => finish({ ...source, probeMs: nowMs() - t0, total }),
      () => finish(null),
    );
  });
}

/**
 * 没给 expectedSize 时，取「被最多来源报告的 total」；平票取最快来源报告的。
 * @param {{ total: number }[]} probes 已按 probeMs 升序
 * @returns {number}
 */
function majorityTotal(probes) {
  /** @type {Map<number, number>} */
  const votes = new Map();
  for (const p of probes) votes.set(p.total, (votes.get(p.total) ?? 0) + 1);
  let best = probes[0].total;
  for (const p of probes) {
    if ((votes.get(p.total) ?? 0) > (votes.get(best) ?? 0)) best = p.total;
  }
  return best;
}

/**
 * 探测每个来源：发 Range: bytes=0-N 小请求，量耗时，校验 206 + Content-Range 里的 total。
 * 返回可用来源（按耗时升序）及 total size。可用来源为空时 size=0、sources=[]。
 * expectedSize 给了则 total 不等的来源视为不可用（防两个来源指向不同文件）；没给则按多数票选 total，
 * 与之不等的来源同样剔除（调用方知道发布清单里的体积时务必传 expectedSize）。
 * @param {DownloadSource[]} sources
 * @param {{ fetch?: typeof fetch, probeBytes?: number, timeoutMs?: number, expectedSize?: number, signal?: AbortSignal }} [opts]
 * @returns {Promise<{ sources: (DownloadSource & { probeMs: number })[], size: number }>}
 */
export async function probeSources(sources, opts = {}) {
  const fetchImpl = opts.fetch ?? globalThis.fetch;
  if (typeof fetchImpl !== 'function') throw new TypeError('probeSources: no fetch implementation');
  const probeBytes = opts.probeBytes ?? DEFAULT_PROBE_BYTES;
  const timeoutMs = opts.timeoutMs ?? DEFAULT_PROBE_TIMEOUT_MS;
  const results = await Promise.all(
    sources.map((s) => probeOne(fetchImpl, s, probeBytes, timeoutMs, opts.expectedSize, opts.signal)),
  );
  const ok = results
    .filter(/** @returns {r is NonNullable<typeof r>} */ (r) => r !== null)
    .sort((a, b) => a.probeMs - b.probeMs);
  if (ok.length === 0) return { sources: [], size: 0 };
  const size = opts.expectedSize ?? majorityTotal(ok);
  const usable = ok.filter((r) => r.total === size).map(({ total: _total, ...rest }) => rest);
  return { sources: usable, size };
}

// ---------------------------------------------------------------------------
// downloadChunked 的三个内部构件：分片队列 / 来源状态 / 进度 + 并发闸
// ---------------------------------------------------------------------------

/**
 * 分片队列：持有 [0,size) 的分片表和待取列表。失败分片回到队首并记住上次失败的来源，
 * 让别的来源优先接手；等待者通过 waitForChange 收敛在真实事件（放回/完成/来源退出/中止）上。
 * @param {number} size
 * @param {number} chunkBytes
 */
function createChunkQueue(size, chunkBytes) {
  /** @type {Chunk[]} */
  const chunks = [];
  for (let start = 0, index = 0; start < size; start += chunkBytes, index += 1) {
    chunks.push({ index, start, end: Math.min(start + chunkBytes, size) - 1, lastFailedBy: null });
  }
  /** @type {Chunk[]} */
  const pending = chunks.slice();
  let done = 0;
  /** @type {(() => void)[]} */
  let waiters = [];

  /** @returns {void} */
  function notify() {
    const batch = waiters;
    waiters = [];
    for (const wake of batch) wake();
  }

  return {
    total: chunks.length,
    get done() {
      return done;
    },
    get isComplete() {
      return done === chunks.length;
    },
    /**
     * 取下一个待下载分片。allowOwnFailed=false 时跳过本来源刚失败的分片（留给别的来源）。
     * @param {string} sourceId
     * @param {boolean} allowOwnFailed
     * @returns {Chunk | null}
     */
    take(sourceId, allowOwnFailed) {
      const i = allowOwnFailed ? (pending.length > 0 ? 0 : -1) : pending.findIndex((c) => c.lastFailedBy !== sourceId);
      if (i < 0) return null;
      return pending.splice(i, 1)[0];
    },
    /**
     * 分片失败放回队首。
     * @param {Chunk} chunk
     * @param {string} sourceId
     * @returns {void}
     */
    release(chunk, sourceId) {
      chunk.lastFailedBy = sourceId;
      pending.unshift(chunk);
      notify();
    },
    /** @returns {void} */
    complete() {
      done += 1;
      notify();
    },
    notify,
    /** @returns {Promise<void>} */
    waitForChange() {
      return new Promise((resolve) => {
        waiters.push(resolve);
      });
    },
  };
}

/**
 * 单个来源的运行时状态：累计字节、连续失败计数、是否已退出、存活 worker 数。
 * @param {DownloadSource} source
 * @param {number} maxFailures
 * @param {(id: string) => void} onDrop
 * @returns {SourceState}
 */
function createSourceState(source, maxFailures, onDrop) {
  /** @type {SourceState} */
  const state = {
    id: source.id,
    url: source.url,
    bytes: 0,
    consecutiveFailures: 0,
    dropped: false,
    workers: 0,
    succeed(n) {
      state.bytes += n;
      state.consecutiveFailures = 0;
    },
    fail() {
      state.consecutiveFailures += 1;
      if (!state.dropped && state.consecutiveFailures >= maxFailures) {
        state.dropped = true;
        onDrop(state.id);
      }
    },
    // 退避后整轮重开时把来源放回可用：队列里已完成的分片不会重下，所以这不是「从头再来」。
    reset() {
      state.consecutiveFailures = 0;
      state.dropped = false;
    },
  };
  return state;
}

/**
 * 进度快照 + 最近 SPEED_WINDOW_MS 的滑窗速率。
 * 速率样本按「收到的字节段」记（sample），分片内也在动；bytesDone / chunksDone 只在分片落位后记（record）。
 * 分片中途失败时已收到的字节留在速率窗里（那确实是吞吐），不进 bytesDone。
 * @param {number} size
 * @param {number} chunksTotal
 * @param {SourceState[]} states
 */
function createProgressTracker(size, chunksTotal, states) {
  const startedAt = nowMs();
  /** @type {{ t: number, bytes: number }[]} */
  const samples = [];
  let bytesDone = 0;
  let chunksDone = 0;

  /** @returns {number} */
  function bytesPerSecond() {
    const t = nowMs();
    const windowStart = Math.max(startedAt, t - SPEED_WINDOW_MS);
    while (samples.length > 0 && samples[0].t < windowStart) samples.shift();
    let sum = 0;
    for (const s of samples) sum += s.bytes;
    return (sum * 1000) / Math.max(t - windowStart, 1);
  }

  return {
    get bytesDone() {
      return bytesDone;
    },
    /**
     * 收到一段字节：只进速率滑窗。
     * @param {number} n
     * @returns {void}
     */
    sample(n) {
      samples.push({ t: nowMs(), bytes: n });
    },
    /**
     * 一个分片落位。
     * @param {number} n
     * @returns {void}
     */
    record(n) {
      bytesDone += n;
      chunksDone += 1;
    },
    /** @returns {Record<string, number>} */
    perSource() {
      return Object.fromEntries(states.map((s) => [s.id, s.bytes]));
    },
    /** @returns {ChunkProgress} */
    snapshot() {
      return {
        bytesDone,
        bytesTotal: size,
        bytesPerSecond: bytesPerSecond(),
        perSource: this.perSource(),
        activeSources: states.filter((s) => !s.dropped && s.workers > 0).map((s) => s.id),
        chunksDone,
        chunksTotal,
      };
    },
  };
}

/**
 * 计数信号量：全局在飞请求数上限。release 时若有等待者直接把槽位转交，不会超卖。
 * @param {number} max
 */
function createSemaphore(max) {
  let active = 0;
  /** @type {(() => void)[]} */
  const waiters = [];
  return {
    /** @returns {Promise<void>} */
    acquire() {
      if (active < max) {
        active += 1;
        return Promise.resolve();
      }
      return new Promise((resolve) => {
        waiters.push(resolve);
      });
    },
    /** @returns {void} */
    release() {
      const next = waiters.shift();
      if (next) next();
      else active -= 1;
    },
  };
}

// ---------------------------------------------------------------------------
// worker
// ---------------------------------------------------------------------------

/**
 * @typedef {object} JobContext
 * @property {typeof fetch} fetchImpl
 * @property {ChunkSink} sink
 * @property {number} size
 * @property {ReturnType<typeof createChunkQueue>} queue
 * @property {ReturnType<typeof createSemaphore>} gate
 * @property {ReturnType<typeof createProgressTracker>} progress
 * @property {SourceState[]} states
 * @property {AbortSignal} signal 内部信号：外部 signal 中止或致命错误时触发，传给每个 fetch
 * @property {boolean} aborted
 * @property {number} firstByteMs
 * @property {number} stallMs
 * @property {((p: ChunkProgress) => void) | undefined} onProgress
 */

/**
 * 拉一个分片并落位。来源侧失败（非 206 / Content-Range 不对 / 长度不对 / 网络异常 / 看门狗超时）在这里吃掉并
 * 记到来源状态；sink.write 失败或 onProgress 抛错是致命错误，原样向上抛。
 * @param {JobContext} ctx
 * @param {SourceState} state
 * @param {Chunk} chunk
 * @returns {Promise<void>}
 */
async function transferChunk(ctx, state, chunk) {
  /** @type {Uint8Array} */
  let data;
  try {
    ({ data } = await fetchRange(ctx.fetchImpl, state.url, chunk.start, chunk.end, {
      expectedTotal: ctx.size,
      signal: ctx.signal,
      firstByteMs: ctx.firstByteMs,
      stallMs: ctx.stallMs,
      onBytes: (n) => ctx.progress.sample(n),
    }));
  } catch (err) {
    if (ctx.aborted) return;
    ctx.queue.release(chunk, state.id);
    state.fail();
    return;
  }
  if (ctx.aborted) return;
  await ctx.sink.write(chunk.start, data);
  if (ctx.aborted) return;
  state.succeed(data.byteLength);
  ctx.progress.record(data.byteLength);
  ctx.queue.complete();
  if (ctx.onProgress) ctx.onProgress(ctx.progress.snapshot());
}

/**
 * 是否还有别的来源活着（用于决定本来源能不能重试自己刚失败的分片）。
 * @param {SourceState[]} states
 * @param {SourceState} self
 * @returns {boolean}
 */
function othersAlive(states, self) {
  return states.some((s) => s !== self && !s.dropped && s.workers > 0);
}

/**
 * 单条 worker：先拿全局并发槽，再从队列取分片；没分片就还槽等事件。
 * 来源退出 / 中止 / 全部完成时退出。
 * @param {JobContext} ctx
 * @param {SourceState} state
 * @returns {Promise<void>}
 */
async function runWorker(ctx, state) {
  state.workers += 1;
  try {
    while (!ctx.aborted && !state.dropped && !ctx.queue.isComplete) {
      await ctx.gate.acquire();
      const chunk = ctx.aborted || state.dropped ? null : ctx.queue.take(state.id, !othersAlive(ctx.states, state));
      if (!chunk) {
        ctx.gate.release();
        if (ctx.aborted || state.dropped || ctx.queue.isComplete) break;
        await ctx.queue.waitForChange();
        continue;
      }
      try {
        await transferChunk(ctx, state, chunk);
      } finally {
        ctx.gate.release();
      }
    }
  } finally {
    state.workers -= 1;
    ctx.queue.notify();
  }
}

// ---------------------------------------------------------------------------
// downloadChunked
// ---------------------------------------------------------------------------

/**
 * @param {number} v
 * @param {string} name
 * @param {number} min
 * @returns {number}
 */
function requireInt(v, name, min) {
  if (!Number.isSafeInteger(v) || v < min) throw new TypeError(`downloadChunked: ${name} must be an integer >= ${min}, got ${v}`);
  return v;
}

/**
 * @param {ChunkSink} sink
 * @returns {Promise<void>}
 */
async function abortSink(sink) {
  if (typeof sink.abort !== 'function') return;
  try {
    await sink.abort();
  } catch {
    // 中止路径上 sink 自己的清理失败不应盖过原始错误
  }
}

/**
 * 分片并发下载。
 *
 * 把 [0,size) 切成 chunkBytes 的分片（最后一片短）；每个来源跑 perSourceConcurrency 条 worker（默认 2），
 * 全局并发不超过 maxTotalConcurrency（默认 4）；worker 从共享队列取下一个未完成分片，
 * 必须 206 且 Content-Range 与请求一致、total 等于 size、长度正确，否则视为失败：分片放回队列
 * （优先让别的来源拿）、该来源连续失败 +1，达到 maxSourceFailures（默认 3）则退出（droppedSources）；
 * 成功一次清零。所有来源都退出而分片没拿完 → 退避 retryBackoffMs * 2^n 后把所有来源放回可用、整轮重开，
 * 最多 maxRetries 次（默认 3）；队列是共享的，已落位的分片不会重下。重开次数用尽仍没拿完
 * → reject Error('all sources failed')。maxRetries: 0 即旧行为（来源退完立刻失败）。
 * 分片 body 流式读取并带看门狗：发请求到首段字节超过 firstByteMs（默认 10000）、或相邻两段字节之间超过 stallMs
 * （默认 8000）就中止这次请求，按普通失败处理（放回队列、连续失败 +1）；卡住不是来源已死的强证据，
 * 但连续卡住同样会达到 maxSourceFailures。
 * 数据经 sink.write(offset, data) 乱序落位，全部写完后 sink.close()。
 * signal 中止 → 停止所有 worker（正在流式读取的请求立刻中止）、sink.abort?.()、reject name==='AbortError'。
 * sink.write / onProgress 抛错属于致命错误：中止全部、sink.abort?.()、原错误 reject。
 * onProgress 在每个分片完成时调，bytesPerSecond 用最近 ~3 秒滑窗（样本按收到的字节段计，分片内也在动）。
 *
 * @param {{ sources: DownloadSource[], size: number, sink: ChunkSink, chunkBytes?: number, perSourceConcurrency?: number, maxTotalConcurrency?: number, maxSourceFailures?: number, maxRetries?: number, retryBackoffMs?: number, firstByteMs?: number, stallMs?: number, fetch?: typeof fetch, signal?: AbortSignal, onProgress?: (p: ChunkProgress) => void }} job
 * @returns {Promise<{ bytes: number, perSource: Record<string, number>, droppedSources: string[] }>}
 */
export async function downloadChunked(job) {
  const { sources, sink, signal, onProgress } = job;
  if (!Array.isArray(sources)) throw new TypeError('downloadChunked: sources must be an array');
  if (!sink || typeof sink.write !== 'function' || typeof sink.close !== 'function') {
    throw new TypeError('downloadChunked: sink must have write() and close()');
  }
  const size = requireInt(job.size, 'size', 0);
  const chunkBytes = requireInt(job.chunkBytes ?? DEFAULT_CHUNK_BYTES, 'chunkBytes', 1);
  const perSourceConcurrency = requireInt(job.perSourceConcurrency ?? 2, 'perSourceConcurrency', 1);
  const maxTotalConcurrency = requireInt(job.maxTotalConcurrency ?? 4, 'maxTotalConcurrency', 1);
  const maxSourceFailures = requireInt(job.maxSourceFailures ?? 3, 'maxSourceFailures', 1);
  const maxRetries = requireInt(job.maxRetries ?? DEFAULT_MAX_RETRIES, 'maxRetries', 0);
  const retryBackoffMs = requireInt(job.retryBackoffMs ?? DEFAULT_RETRY_BACKOFF_MS, 'retryBackoffMs', 0);
  const firstByteMs = requireInt(job.firstByteMs ?? DEFAULT_FIRST_BYTE_MS, 'firstByteMs', 1);
  const stallMs = requireInt(job.stallMs ?? DEFAULT_STALL_MS, 'stallMs', 1);
  const fetchImpl = job.fetch ?? globalThis.fetch;
  if (typeof fetchImpl !== 'function') throw new TypeError('downloadChunked: no fetch implementation');

  if (signal?.aborted) {
    await abortSink(sink);
    throw createAbortError();
  }

  /** @type {string[]} */
  const droppedSources = [];
  // 每轮重开换一个新的：上一轮结束时它可能已经 abort 过（看门狗、来源退出），
  // 复用会让新一轮的请求一发出去就被中止。
  let ctrl = new AbortController();
  const queue = createChunkQueue(size, chunkBytes);
  const states = sources.map((s) => createSourceState(s, maxSourceFailures, (id) => {
    if (!droppedSources.includes(id)) droppedSources.push(id);
    queue.notify();
  }));
  /** @type {JobContext} */
  const ctx = {
    fetchImpl,
    sink,
    size,
    queue,
    gate: createSemaphore(maxTotalConcurrency),
    progress: createProgressTracker(size, queue.total, states),
    states,
    signal: ctrl.signal,
    aborted: false,
    firstByteMs,
    stallMs,
    onProgress,
  };

  /** @returns {void} */
  function stopEverything() {
    if (ctx.aborted) return;
    ctx.aborted = true;
    ctrl.abort();
    queue.notify();
  }

  /** @type {(err: Error) => void} */
  let failFast = () => {};
  /** @type {Promise<never>} */
  const abortedPromise = new Promise((_, reject) => {
    failFast = reject;
  });
  abortedPromise.catch(() => {}); // 竞速已被别的分支赢走时不留下 unhandled rejection

  /** @returns {void} */
  const onSignalAbort = () => {
    stopEverything();
    failFast(createAbortError());
  };
  signal?.addEventListener('abort', onSignalAbort, { once: true });

  try {
    for (let attempt = 0; ; attempt += 1) {
      const workers = states.flatMap((state) => Array.from({ length: perSourceConcurrency }, () => runWorker(ctx, state)));
      await Promise.race([Promise.all(workers), abortedPromise]);
      if (queue.isComplete || attempt >= maxRetries) break;
      // 退避期间外部取消要立刻生效，所以和 abortedPromise 竞速。
      await Promise.race([delay(retryBackoffMs * 2 ** attempt, signal), abortedPromise]);
      ctrl = new AbortController();
      ctx.signal = ctrl.signal;
      for (const state of states) state.reset();
      queue.notify();
    }
  } catch (err) {
    stopEverything();
    await abortSink(sink);
    throw err;
  } finally {
    signal?.removeEventListener('abort', onSignalAbort);
  }

  if (!queue.isComplete) {
    stopEverything();
    await abortSink(sink);
    throw new Error('all sources failed');
  }
  await sink.close();
  return { bytes: ctx.progress.bytesDone, perSource: ctx.progress.perSource(), droppedSources };
}

// ---------------------------------------------------------------------------
// sinks
// ---------------------------------------------------------------------------

/**
 * 内存 sink 的共同实现；onClose 用于浏览器分支在 close 时触发保存。
 * @param {number} size
 * @param {((bytes: Uint8Array) => Promise<void>) | undefined} onClose
 * @returns {ChunkSink & { kind: 'memory', closed: boolean, aborted: boolean, bytes(): Uint8Array }}
 */
function buildMemorySink(size, onClose) {
  if (!Number.isSafeInteger(size) || size < 0) throw new TypeError(`memory sink: size must be an integer >= 0, got ${size}`);
  const buffer = new Uint8Array(size); // 分配失败抛 RangeError，留给调用方
  const sink = {
    kind: /** @type {const} */ ('memory'),
    closed: false,
    aborted: false,
    /**
     * @param {number} position
     * @param {Uint8Array} data
     * @returns {Promise<void>}
     */
    async write(position, data) {
      if (sink.closed || sink.aborted) throw new Error('memory sink: write after close/abort');
      if (!(data instanceof Uint8Array)) throw new TypeError('memory sink: data must be a Uint8Array');
      if (!Number.isSafeInteger(position) || position < 0 || position + data.byteLength > size) {
        throw new RangeError(`memory sink: write [${position}, ${position + data.byteLength}) out of [0, ${size})`);
      }
      buffer.set(data, position);
    },
    /** @returns {Promise<void>} */
    async close() {
      if (sink.aborted) throw new Error('memory sink: close after abort');
      if (sink.closed) return;
      sink.closed = true;
      if (onClose) await onClose(buffer);
    },
    /** @returns {Promise<void>} */
    async abort() {
      sink.aborted = true;
    },
    /** @returns {Uint8Array} */
    bytes() {
      return buffer;
    },
  };
  return sink;
}

/**
 * 纯内存 sink，给测试和小文件用。size 太大分配失败时同步抛 RangeError。
 * @param {number} size
 * @returns {ChunkSink & { kind: 'memory', closed: boolean, aborted: boolean, bytes(): Uint8Array }}
 */
export function createMemorySink(size) {
  return buildMemorySink(size, undefined);
}

/**
 * 用隐藏的 <a download> 触发浏览器保存。Object URL 延迟 60 秒再回收：立即 revoke 会让部分浏览器的下载失败。
 * @param {Blob} blob
 * @param {string} filename
 * @returns {Promise<void>}
 */
export async function saveBlobViaAnchor(blob, filename) {
  const doc = /** @type {Document | undefined} */ (globalThis.document);
  if (!doc || typeof URL.createObjectURL !== 'function') {
    throw new Error('saveBlobViaAnchor: no DOM available to trigger a download');
  }
  const url = URL.createObjectURL(blob);
  try {
    const a = doc.createElement('a');
    a.href = url;
    a.download = filename;
    a.rel = 'noopener';
    a.style.display = 'none';
    doc.body.appendChild(a);
    a.click();
    a.remove();
  } finally {
    setTimeout(() => URL.revokeObjectURL(url), 60_000);
  }
}

/**
 * File System Access 流式落盘：createWritable → truncate(size) → write({type:'write', position, data})。
 * WritableStream 内部串行排队，乱序 position 写入是安全的。
 * @param {{ createWritable(): Promise<any>, getFile(): Promise<File> }} handle FileSystemFileHandle
 * @param {number} size
 * @returns {Promise<ChunkSink & { kind: 'filesystem', file(): Promise<File> }>}
 */
async function createFileSystemSink(handle, size) {
  const writable = await handle.createWritable();
  await writable.truncate(size);
  let finished = false;
  let closed = false;
  return {
    kind: 'filesystem',
    /**
     * @param {number} position
     * @param {Uint8Array} data
     * @returns {Promise<void>}
     */
    write(position, data) {
      return writable.write({ type: 'write', position, data });
    },
    /** @returns {Promise<void>} */
    async close() {
      if (finished) return;
      finished = true;
      await writable.close();
      closed = true;
    },
    /** @returns {Promise<void>} */
    async abort() {
      if (finished) return;
      finished = true;
      await writable.abort();
    },
    /**
     * close 成功之后才有完整文件可读（写盘走 WritableStream，close 前内容未落定）。
     * @returns {Promise<File>}
     */
    async file() {
      if (!closed) throw new Error('filesystem sink: file() is only available after close()');
      return handle.getFile();
    },
  };
}

/**
 * 浏览器保存端：有 showSaveFilePicker（Chromium 桌面）就流式写盘；否则（或 forceMemory）内存 Uint8Array(size)
 * （分配失败 RangeError 时 reject）+ close 时 Blob → <a download>。
 * 用户在文件选择器里取消 → reject AbortError；选择器因其它原因失败（无 user activation、非安全上下文）→ 回落内存分支。
 * `save` 可注入（默认 saveBlobViaAnchor），方便无 DOM 环境测试内存分支。
 * @param {{ filename: string, size: number, forceMemory?: boolean, save?: (blob: Blob, filename: string) => Promise<void> }} opts
 * @returns {Promise<ChunkSink & { kind: 'filesystem' | 'memory' }>}
 */
export async function createBrowserSink(opts) {
  const { filename, size, forceMemory = false, save = saveBlobViaAnchor } = opts;
  if (typeof filename !== 'string' || filename.length === 0) throw new TypeError('createBrowserSink: filename required');
  const g = /** @type {any} */ (globalThis);
  if (!forceMemory && typeof g.showSaveFilePicker === 'function') {
    /** @type {any} */
    let handle = null;
    try {
      handle = await g.showSaveFilePicker({ suggestedName: filename });
    } catch (err) {
      if (isAbortError(err)) throw err;
      handle = null;
    }
    if (handle) return createFileSystemSink(handle, size);
  }
  return buildMemorySink(size, (bytes) => save(new Blob([bytes]), filename));
}

// ---------------------------------------------------------------------------
// 校验
// ---------------------------------------------------------------------------

/**
 * SHA-256 小写 hex。走 crypto.subtle（需要安全上下文）；Blob 先整体读进内存再算——SubtleCrypto 没有增量接口。
 * @param {ArrayBuffer | ArrayBufferView | Blob} data
 * @returns {Promise<string>}
 */
export async function sha256Hex(data) {
  const subtle = globalThis.crypto?.subtle;
  if (!subtle || typeof subtle.digest !== 'function') {
    throw new Error('sha256Hex: crypto.subtle is unavailable (needs a secure context)');
  }
  const isBlob = typeof Blob !== 'undefined' && data instanceof Blob;
  const buf = isBlob ? await data.arrayBuffer() : data;
  if (!(buf instanceof ArrayBuffer) && !ArrayBuffer.isView(buf)) {
    throw new TypeError('sha256Hex: data must be an ArrayBuffer, ArrayBufferView or Blob');
  }
  const digest = new Uint8Array(await subtle.digest('SHA-256', buf));
  let hex = '';
  for (const b of digest) hex += b.toString(16).padStart(2, '0');
  return hex;
}

/**
 * 校验 sink 里落地的完整文件：内存 sink 走 bytes()，文件系统 sink 走 file()（须在 close 之后）。
 * expectedHex 比较不区分大小写。sink 两种取数口都没有 → throw TypeError。
 * @param {ChunkSink} sink
 * @param {string} expectedHex
 * @returns {Promise<{ ok: boolean, actual: string }>}
 */
export async function verifySink(sink, expectedHex) {
  if (typeof expectedHex !== 'string') throw new TypeError('verifySink: expectedHex must be a string');
  /** @type {Uint8Array | File | null} */
  let data = null;
  if (typeof sink.bytes === 'function') data = sink.bytes();
  else if (typeof sink.file === 'function') data = await sink.file();
  if (data === null) throw new TypeError('verifySink: sink has neither bytes() nor file()');
  const actual = await sha256Hex(data);
  return { ok: actual === expectedHex.trim().toLowerCase(), actual };
}
