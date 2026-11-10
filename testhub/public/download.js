// The public download page (/download): the newest version of each slot (src/share.js: main, models) with every one
// of its files (the model pack comes in several zips, all needed, extracted into the same folder), each fetched part
// by part the way fushi.moe fetches its recommended pack (pack-download.mjs and chunked-download.mjs are fushi.moe's,
// copied unchanged): several Range connections per part, each part checked against its SHA-256 before it is written
// into the visitor's file.
import { createBrowserSink } from './chunked-download.mjs';
import { downloadPack, preferredPartBaseUrl, validatePackManifest } from './pack-download.mjs';

// Without the save dialog (Firefox, Safari) the file is put together in memory first: only up to this size.
const MEMORY_MAX = 1536 * 1048576;

function mb(n) {
  return n >= 1073741824 ? (n / 1073741824).toFixed(2) + ' GB' : (n / 1048576).toFixed(1) + ' MB';
}

function speed(bps) {
  if (!bps) return '';
  return bps >= 1048576 ? (bps / 1048576).toFixed(1) + ' MB/s' : Math.max(1, Math.round(bps / 1024)) + ' KB/s';
}

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

// One file of the version: its line, its buttons, its progress.
function fileRow(raw, file, k, count) {
  const manifest = validatePackManifest({ ...file, version: raw.version });
  const row = el('div', 'file gap-top');
  const title = el('b', '', count > 1 ? `第 ${k + 1}/${count} 部分 / Part ${k + 1} of ${count}：` : '');
  title.append(el('span', '', file.name));
  const info = el('div', 'muted');
  info.append(title, el('br'), `${mb(manifest.sizeBytes)} · ${manifest.parts.length} 块 / chunks`, el('br'), 'SHA-256 ',
    el('code', '', manifest.sha256));
  const go = el('button', 'btn', count > 1 ? `下载第 ${k + 1} 部分 / Download part ${k + 1}` : '下载 / Download');
  const cancel = el('button', 'btn ghost', '取消 / Cancel');
  cancel.hidden = true;
  const plain = el('a', 'btn ghost', '普通下载 / Plain download');
  plain.href = manifest.wholeUrl;
  const buttons = el('div', 'row gap-top');
  buttons.append(go, cancel, plain);
  const progress = el('div', 'progress');
  progress.hidden = true;
  const fill = el('div', 'fill');
  progress.append(fill);
  const statusLine = el('div', 'status muted');
  row.append(info, buttons, progress, statusLine);
  const status = (text) => { statusLine.textContent = text; };
  let controller = null;

  async function start() {
    if (controller) return;
    if (typeof window.showSaveFilePicker !== 'function' && manifest.sizeBytes > MEMORY_MAX) {
      status('这个浏览器不能直接写盘，放不下这么大的文件：请用 Chrome / Edge，或点「普通下载」。'
        + ' / This browser cannot hold a file this large: use Chrome / Edge, or "Plain download".');
      return;
    }
    let sink;
    try {
      // First, before any other await: the save dialog only opens inside the click.
      sink = await createBrowserSink({ filename: file.name, size: manifest.sizeBytes });
    } catch (e) {
      if (e && e.name === 'AbortError') return;
      status('这个浏览器放不下整个文件（' + (e.message || e) + '），请用「普通下载」。 / Use "Plain download".');
      return;
    }
    controller = new AbortController();
    go.disabled = true;
    cancel.hidden = false;
    progress.hidden = false;
    fill.style.width = '0';
    status('开始下载…… / Starting...');
    try {
      await downloadPack({
        manifest,
        sink,
        baseUrl: preferredPartBaseUrl(manifest, location.origin),
        signal: controller.signal,
        onProgress: (p) => {
          fill.style.width = (p.bytesDone / p.bytesTotal * 100).toFixed(1) + '%';
          status(`${mb(p.bytesDone)} / ${mb(p.bytesTotal)} · ${Math.min(p.partsDone + 1, p.partsTotal)}/${p.partsTotal} ${speed(p.bytesPerSecond)}`);
        },
      });
      fill.style.width = '100%';
      status(sink.kind === 'filesystem' ? '完成，每块都已校验。 / Done, every chunk checked.'
        : '完成，每块都已校验，浏览器正在保存。 / Done, every chunk checked; the browser is saving it.');
    } catch (e) {
      status(e && e.name === 'AbortError' ? '已取消。 / Cancelled.'
        : '下载失败：' + (e.message || e) + '。可以重试，或用「普通下载」。 / Failed: retry, or use "Plain download".');
    } finally {
      controller = null;
      go.disabled = false;
      cancel.hidden = true;
    }
  }

  go.addEventListener('click', start);
  cancel.addEventListener('click', () => controller?.abort());
  return row;
}

async function pack(section) {
  const $ = (cls) => section.querySelector('.' + cls);
  const slot = section.dataset.slot;
  let raw;
  let rows;
  try {
    const res = await fetch(`/share/${slot}/manifest.json`, { cache: 'no-store' });
    if (!res.ok) throw new Error(res.status === 404 ? '还没有发布 / nothing published yet' : `HTTP ${res.status}`);
    raw = await res.json();
    if (!Array.isArray(raw.files) || !raw.files.length) throw new TypeError('manifest: files required');
    rows = raw.files.map((f, k) => fileRow(raw, f, k, raw.files.length));
  } catch (e) {
    $('info').textContent = '取不到下载清单 / no download list: ' + (e.message || e);
    return;
  }
  const when = new Date(raw.published_at).toLocaleString('zh-CN', { hour12: false });
  $('info').replaceChildren('版本 / version ', el('b', '', raw.version), ` · 发布于 / published ${when}`);
  if (raw.files.length > 1) {
    const n = raw.files.length;
    $('multi').replaceChildren(el('b', '', `共 ${n} 个文件，${n} 个都要下载，并解压到同一个文件夹。`), el('br'),
      el('b', '', `${n} files: download all ${n} and extract them into the same folder.`));
    $('multi').hidden = false;
  }
  $('files').replaceChildren(...rows);
  if (raw.notes) {
    $('notes').textContent = raw.notes;
    $('notesBox').hidden = false;
  }
}

document.querySelectorAll('section.pack').forEach(pack);
