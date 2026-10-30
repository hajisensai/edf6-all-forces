// The public download page (/download): the newest package of each slot (src/share.js: main, models), fetched part by
// part the way fushi.moe fetches its recommended pack (pack-download.mjs and chunked-download.mjs are fushi.moe's,
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

function pack(section) {
  const $ = (cls) => section.querySelector('.' + cls);
  const slot = section.dataset.slot;
  let manifest = null;
  let raw = null;
  let controller = null;
  const status = (text) => { $('status').textContent = text; };

  async function load() {
    try {
      const res = await fetch(`/share/${slot}/manifest.json`, { cache: 'no-store' });
      if (!res.ok) throw new Error(res.status === 404 ? '还没有发布' : `HTTP ${res.status}`);
      raw = await res.json();
      manifest = validatePackManifest(raw);
    } catch (e) {
      $('info').textContent = '取不到下载清单：' + (e.message || e);
      return;
    }
    const when = new Date(raw.published_at).toLocaleString('zh-CN', { hour12: false });
    const name = document.createElement('b');
    name.textContent = raw.name;
    $('info').replaceChildren(name, document.createElement('br'),
      `${mb(manifest.sizeBytes)} · ${manifest.parts.length} 片 · 发布于 ${when}`,
      document.createElement('br'), 'SHA-256 ', Object.assign(document.createElement('code'), { textContent: manifest.sha256 }));
    if (raw.notes) {
      $('notes').textContent = raw.notes;
      $('notesBox').hidden = false;
    }
    $('plain').href = manifest.wholeUrl;
    $('plain').hidden = false;
    $('go').disabled = false;
  }

  async function start() {
    if (!manifest || controller) return;
    if (typeof window.showSaveFilePicker !== 'function' && manifest.sizeBytes > MEMORY_MAX) {
      status('这个浏览器不能直接写盘，放不下这么大的文件：请用 Chrome / Edge，或点「普通下载」。');
      return;
    }
    let sink;
    try {
      // First, before any other await: the save dialog only opens inside the click.
      sink = await createBrowserSink({ filename: raw.name, size: manifest.sizeBytes });
    } catch (e) {
      if (e && e.name === 'AbortError') return;
      status('这个浏览器放不下整个文件（' + (e.message || e) + '），请用「普通下载」。');
      return;
    }
    controller = new AbortController();
    $('go').disabled = true;
    $('cancel').hidden = false;
    $('progress').hidden = false;
    $('fill').style.width = '0';
    status('开始下载……');
    try {
      await downloadPack({
        manifest,
        sink,
        baseUrl: preferredPartBaseUrl(manifest, location.origin),
        signal: controller.signal,
        onProgress: (p) => {
          $('fill').style.width = (p.bytesDone / p.bytesTotal * 100).toFixed(1) + '%';
          status(`${mb(p.bytesDone)} / ${mb(p.bytesTotal)} · 第 ${Math.min(p.partsDone + 1, p.partsTotal)}/${p.partsTotal} 片 ${speed(p.bytesPerSecond)}`);
        },
      });
      $('fill').style.width = '100%';
      status(sink.kind === 'filesystem' ? '完成，每片都已校验。' : '完成，每片都已校验，浏览器正在保存。');
    } catch (e) {
      status(e && e.name === 'AbortError' ? '已取消。' : '下载失败：' + (e.message || e) + '。可以重试，或用「普通下载」。');
    } finally {
      controller = null;
      $('go').disabled = false;
      $('cancel').hidden = true;
    }
  }

  $('go').addEventListener('click', start);
  $('cancel').addEventListener('click', () => controller?.abort());
  load();
}

document.querySelectorAll('section.pack').forEach(pack);
