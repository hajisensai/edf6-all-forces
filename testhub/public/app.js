// The test page: renders /api/state and posts reports (multipart, with an upload progress bar).
'use strict';

const STATUS = { todo: '待测', verify: '已修，待验证', pass: '通过', fail: '还有问题', done: '已完成', closed: '关闭' };
const RESULT = { pass: '修好了', fail: '还有问题', new: '新问题', info: '补充信息' };
// The developer's handling of a report (worker.js REPORT_STATUSES).
const HANDLING = { open: '待处理', doing: '处理中', verify: '已修待确认', done: '已完成', closed: '不处理' };
const $ = (id) => document.getElementById(id);
let state = null;

function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}
function when(ms) {
  return ms ? new Date(ms).toLocaleString('zh-CN', { hour12: false }) : '';
}
function size(n) {
  return n >= 1 << 20 ? (n / (1 << 20)).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';
}
function dl(b) {
  return '/dl/' + encodeURIComponent(b.name);
}

function renderBuilds(builds) {
  const b = builds[0];
  $('build').innerHTML = b
    ? `<p class="row"><a class="btn" href="${dl(b)}">下载 ${esc(b.name)}</a><span class="muted">${size(b.size)} · ${esc(b.channel)} · ${when(b.at)}</span></p>
       ${b.notes ? `<pre>${esc(b.notes)}</pre>` : ''}<p class="muted hash">SHA-256 ${esc(b.sha256)}</p>`
    : '还没有上传测试版。';
  $('builds').innerHTML = builds.slice(1).map((x) =>
    `<tr><td><a href="${dl(x)}">${esc(x.name)}</a></td><td>${esc(x.channel)}</td><td>${when(x.at)}</td><td>${esc(x.notes.split('\n')[0])}</td></tr>`).join('');
  $('version').value = b ? b.version + (b.commit_sha ? '+' + b.commit_sha.slice(0, 7) : '') : '';
}

function handling(r) {
  const s = r.status || 'open';
  return `<span class="badge h-${esc(s)}">${HANDLING[s] || esc(s)}</span>`;
}

function reportHtml(r) {
  const files = r.files.map((f) => `<a href="/file/${f.id}">${esc(f.name)}</a><span class="muted">${size(f.size)}</span>`).join(' ');
  return `<div class="report" id="r${r.id}">
    <div class="row"><span class="badge r-${esc(r.result)}">${RESULT[r.result] || esc(r.result)}</span>${handling(r)}
      <span class="muted">#${r.id} · ${esc(r.author)} · ${when(r.at)}${r.version ? ' · ' + esc(r.version) : ''}${r.source === 'exe' ? ' · 安装器回传' : ''}</span></div>
    ${r.note ? `<pre>${esc(r.note)}</pre>` : ''}
    ${files ? `<div class="files">${files}</div>` : ''}
    ${r.reply ? `<div class="reply"><b>开发者回复</b> <span class="muted">${when(r.reply_at)}</span><pre>${esc(r.reply)}</pre></div>` : ''}
  </div>`;
}

function renderCases(cases, reports) {
  $('cases').innerHTML = cases.length ? cases.map((c) => {
    const mine = reports.filter((r) => r.case_id === c.id).slice(0, 3);
    return `<div class="case">
      <h3>#${c.id} ${esc(c.title)} <span class="badge s-${esc(c.status)}">${STATUS[c.status] || esc(c.status)}</span></h3>
      ${c.steps ? `<pre>${esc(c.steps)}</pre>` : ''}
      ${c.dev_note ? `<div class="note"><b>开发者说明</b>（${when(c.updated_at)}）<pre>${esc(c.dev_note)}</pre></div>` : ''}
      ${mine.length ? `<details><summary class="muted">最近 ${mine.length} 条反馈</summary>${mine.map(reportHtml).join('')}</details>` : ''}
    </div>`;
  }).join('') : '暂时没有测试项。';
  const open = cases.filter((c) => c.status !== 'closed');
  const keep = $('case').value;
  $('case').innerHTML = open.map((c) => `<option value="${c.id}">#${c.id} ${esc(c.title)}</option>`).join('') +
    '<option value="">其它 / 新问题</option>';
  if (keep !== '') $('case').value = keep;
}

// One row per report: what the tester said and what became of it (the 处理状态 column). A developer changes the
// status right in the table; everyone else reads it.
function reportTable(reports, dev) {
  if (!reports.length) return '';
  const rows = reports.map((r) => {
    const s = r.status || 'open';
    const cell = dev
      ? `<select class="small" data-report="${r.id}">${Object.entries(HANDLING).map(([k, v]) =>
        `<option value="${k}"${k === s ? ' selected' : ''}>${v}</option>`).join('')}</select>`
      : handling(r);
    const note = (r.note || '').split('\n')[0];
    return `<tr class="${s === 'done' || s === 'closed' ? 'finished' : ''}"><td><a href="#r${r.id}">#${r.id}</a></td>
      <td><span class="badge r-${esc(r.result)}">${RESULT[r.result] || esc(r.result)}</span></td><td>${cell}</td>
      <td class="noteCell">${esc(note.length > 60 ? note.slice(0, 60) + '…' : note) || '<span class="muted">（只有附件）</span>'}</td>
      <td class="muted">${esc(r.author)}</td><td class="muted">${when(r.at)}</td><td>${r.reply ? '✓' : ''}</td></tr>`;
  }).join('');
  const done = reports.filter((r) => r.status === 'done').length;
  return `<p class="muted">共 ${reports.length} 条，已完成 ${done} 条。</p>
    <div class="scroll"><table class="builds reportsTable"><thead><tr><th>#</th><th>结果</th><th>处理状态</th><th>说明</th><th>谁</th><th>时间</th><th>回复</th></tr></thead>
    <tbody>${rows}</tbody></table></div>`;
}

async function setHandling(id, status, select) {
  select.disabled = true;
  try {
    const res = await fetch('/api/report_status', {
      method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ report_id: id, status }),
    });
    if (!res.ok) throw new Error((await res.json().catch(() => ({}))).error || `HTTP ${res.status}`);
    await load();
  } catch (err) {
    alert('没改成：' + err.message);
    select.disabled = false;
  }
}

$('reportTable').addEventListener('change', (e) => {
  const sel = e.target.closest('select[data-report]');
  if (sel) setHandling(Number(sel.dataset.report), sel.value, sel);
});

async function load() {
  const res = await fetch('/api/state', { cache: 'no-store' });
  if (res.status === 401) { location.href = '/login.html'; return; }
  state = await res.json();
  $('me').textContent = state.me.user + (state.me.role === 'dev' ? '（开发者）' : '');
  renderBuilds(state.builds);
  renderCases(state.cases, state.reports);
  $('reportTable').innerHTML = reportTable(state.reports, state.me.role === 'dev');
  $('reports').innerHTML = state.reports.length ? state.reports.map(reportHtml).join('') : '还没有反馈。';
}

// fetch() has no upload progress; XMLHttpRequest does (videos can be tens of MB).
function send(form) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/report');
    xhr.upload.onprogress = (e) => { if (e.lengthComputable) $('progress').value = e.loaded / e.total; };
    xhr.onload = () => {
      let body = {};
      try { body = JSON.parse(xhr.responseText); } catch { /* not JSON */ }
      xhr.status === 200 ? resolve(body) : reject(new Error(body.error || `HTTP ${xhr.status}`));
    };
    xhr.onerror = () => reject(new Error('网络错误，请重试'));
    xhr.send(new FormData(form));
  });
}

$('form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const form = e.target;
  const total = [...$('file').files].reduce((n, f) => n + f.size, 0);
  if (total > 95 * (1 << 20)) { $('sendState').textContent = `附件共 ${size(total)}，超过 95 MB，请剪短录屏或分几次提交。`; return; }
  $('send').disabled = true;
  $('progress').classList.remove('hide');
  $('progress').value = 0;
  $('sendState').textContent = '上传中……';
  try {
    const r = await send(form);
    $('sendState').textContent = `已提交 #${r.id}，谢谢！`;
    form.reset();
    await load();
  } catch (err) {
    $('sendState').textContent = '提交失败：' + err.message;
  } finally {
    $('send').disabled = false;
    $('progress').classList.add('hide');
  }
});

load().catch((e) => { $('cases').textContent = '加载失败：' + e.message; });
