// The level plan page: the group's outline and its proposals (/api/plan). Everything players wrote is put on the
// page as text nodes (h() below): no string of theirs is ever parsed as markup.
'use strict';

const STATUS = { open: '待讨论', accepted: '采纳', done: '已做', rejected: '不做' };
const KIND = { new: '新关卡', edit: '修改原版' };
const $ = (id) => document.getElementById(id);
let state = null;
let pending = null;   // { verb: 'insert' | 'move', ...body, label }
let editing = null;   // the proposal id the form edits
let group = null;     // the group a developer looks at

// h('div', { class: 'x', onclick: f }, 'text', node, [more]): strings become text nodes.
function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? '' : String(v));
  }
  for (const kid of kids.flat(Infinity)) {
    if (kid == null || kid === false) continue;
    el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  }
  return el;
}

function when(ms) {
  return ms ? new Date(ms).toLocaleString('zh-CN', { hour12: false }) : '';
}

function code(m) {
  return m.key.replace(/^EDF[56]\//, '').replace(/^DLC\//, '');
}

function missionName(m) {
  return `[${m.series}] ${code(m)} ${m.sc}`;
}

function showError(text) {
  $('error').textContent = text || '';
  $('error').classList.toggle('hide', !text);
}

async function api(path, data) {
  const res = await fetch(path, data === undefined ? { cache: 'no-store' } : {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify(group == null ? data : { group, ...data }),
  });
  if (res.status === 401) { location.href = '/login.html'; throw new Error('请先登录'); }
  let out = {};
  try { out = await res.json(); } catch { /* not JSON */ }
  if (!res.ok) throw new Error(out.error || `HTTP ${res.status}`);
  return out;
}

async function act(path, data) {
  showError('');
  try {
    const out = await api(path, data);
    await load();
    return out;
  } catch (e) {
    showError('没有成功：' + e.message);
    throw e;
  }
}

// ------------------------------------------------------------ outline

function rowsHtml() {
  const missions = new Map(state.missions.map((m) => [m.key, m]));
  const proposals = new Map(state.proposals.map((p) => [p.id, p]));
  const counts = new Map();
  for (const p of state.proposals) if (p.kind === 'edit') counts.set(p.mission, (counts.get(p.mission) || 0) + 1);
  const needle = $('filter').value.trim().toLowerCase();
  const out = [];
  if (pending) out.push(slot(null));
  let seq = 0;
  let loops = 0;
  for (const r of state.outline) {
    let el;
    if (r.kind === 'loop') {
      loops += 1;
      el = h('li', { class: 'loop' },
        h('span', {}, `—— 第 ${loops} 条分隔：`, r.note, ' ——'),
        h('span', { class: 'acts' },
          button('改说明', () => {
            const note = prompt('分隔线的说明', r.note);
            if (note != null) act('/api/plan/loop-note', { id: r.id, note }).catch(() => {});
          }),
          moveButton(r, '分隔线「' + r.note + '」'), removeButton(r)));
    } else if (r.kind === 'stock') {
      const m = missions.get(r.mission);
      if (!m) continue;
      seq += 1;
      const text = `${seq} ${m.key} ${m.sc} ${m.ja}`.toLowerCase();
      if (needle && !text.includes(needle)) continue;
      const n = counts.get(m.key) || 0;
      el = h('li', { class: 'stock' },
        h('span', { class: 'seq' }, seq),
        h('span', { class: 'tag' }, m.series), h('b', {}, ' ', code(m), ' ', m.sc), ' ', h('span', { class: 'muted' }, m.ja),
        m.n ? h('span', { class: 'muted' }, ` · 游戏里第 ${m.n} 关`) : h('span', { class: 'bad' }, ' · 游戏里没有（', m.missing || '缺资源', '）'),
        h('span', { class: 'acts' },
          n ? button(`意见 ${n}`, () => showOnly(m.key)) : null,
          button('写意见', () => startProposal('edit', m.key)),
          moveButton(r, missionName(m))));
    } else {
      const p = proposals.get(r.proposal_id);
      seq += 1;
      const title = p ? p.title : '（意见已删除）';
      if (needle && !`${seq} ${title}`.toLowerCase().includes(needle)) continue;
      el = h('li', { class: 'new' },
        h('span', { class: 'seq' }, seq), h('span', { class: 'tag new' }, '新关卡'), h('b', {}, ' ', title),
        p ? h('span', { class: 'muted' }, ' · ', p.author) : null,
        h('span', { class: 'acts' }, p ? button('看意见', () => showOnly('#' + p.id)) : null,
          moveButton(r, '新关卡「' + title + '」'), removeButton(r)));
    }
    out.push(el);
    if (pending) out.push(slot(r.id));
  }
  return out;
}

function button(text, onclick) {
  return h('button', { class: 'btn ghost small', type: 'button', onclick }, text);
}

function moveButton(r, label) {
  return button('移动', () => setPending({ verb: 'move', id: r.id, label: `移动 ${label}：点一个「放到这里」` }));
}

function removeButton(r) {
  return button('删除', () => {
    if (confirm('从大纲里删掉这一行？（新关卡的意见本身不会删）')) act('/api/plan/remove', { id: r.id }).catch(() => {});
  });
}

function slot(after) {
  return h('li', { class: 'slot' }, h('button', {
    class: 'btn small', type: 'button',
    onclick: () => {
      const { verb, label: _, ...rest } = pending;
      pending = null;
      act(verb === 'move' ? '/api/plan/move' : '/api/plan/insert', { ...rest, after }).catch(() => renderOutline());
    },
  }, after == null ? '放到最前面' : '放到这里'));
}

function setPending(p) {
  pending = p;
  renderOutline();
}

function renderOutline() {
  $('pending').classList.toggle('hide', !pending);
  $('pendingText').textContent = pending ? pending.label : '';
  $('outline').replaceChildren(...rowsHtml());
  const placed = new Set(state.outline.filter((r) => r.kind === 'new').map((r) => r.proposal_id));
  const free = state.proposals.filter((p) => p.kind === 'new' && !placed.has(p.id));
  $('newPick').replaceChildren(...(free.length
    ? free.map((p) => h('option', { value: p.id }, `#${p.id} ${p.title}`))
    : [h('option', { value: '' }, '（没有还没放进大纲的新关卡）')]));
  $('addNew').disabled = !free.length;
}

// ------------------------------------------------------------ proposals

function showOnly(value) {
  $('show').value = value;
  renderProposals();
  $('proposals').scrollIntoView({ behavior: 'smooth' });
}

function renderShow() {
  const keep = $('show').value;
  const missions = new Map(state.missions.map((m) => [m.key, m]));
  const counts = new Map();
  for (const p of state.proposals) if (p.kind === 'edit') counts.set(p.mission, (counts.get(p.mission) || 0) + 1);
  const opts = [h('option', { value: '' }, `全部（${state.proposals.length}）`),
    h('option', { value: 'new' }, `新关卡（${state.proposals.filter((p) => p.kind === 'new').length}）`),
    h('option', { value: 'edit' }, `修改原版（${state.proposals.filter((p) => p.kind === 'edit').length}）`)];
  for (const [key, n] of counts) {
    const m = missions.get(key);
    if (m) opts.push(h('option', { value: key }, `${missionName(m)}（${n}）`));
  }
  if (keep.startsWith('#')) opts.push(h('option', { value: keep }, `意见 ${keep}`));
  $('show').replaceChildren(...opts);
  $('show').value = [...$('show').options].some((o) => o.value === keep) ? keep : '';
}

function renderProposals() {
  const missions = new Map(state.missions.map((m) => [m.key, m]));
  const show = $('show').value;
  const list = state.proposals.filter((p) => !show || (show === 'new' || show === 'edit' ? p.kind === show
    : show.startsWith('#') ? p.id === Number(show.slice(1)) : p.mission === show));
  const replies = new Map();
  for (const r of state.replies) (replies.get(r.proposal_id) || replies.set(r.proposal_id, []).get(r.proposal_id)).push(r);
  $('proposals').replaceChildren(...(list.length ? list.map((p) => proposalCard(p, missions.get(p.mission), replies.get(p.id) || []))
    : [document.createTextNode('还没有意见。')]));
}

function proposalCard(p, m, replies) {
  const mine = p.author === state.me.user || state.me.role === 'dev';
  const dev = state.me.role === 'dev';
  const status = dev
    ? h('select', { class: 'small', onchange: (e) => act('/api/plan/proposal-status', { id: p.id, status: e.target.value }).catch(() => {}) },
      Object.entries(STATUS).map(([k, v]) => h('option', { value: k, selected: k === p.status }, v)))
    : h('span', { class: `badge p-${p.status}` }, STATUS[p.status] || p.status);
  const input = h('textarea', { class: 'replyBox', maxlength: 4000, placeholder: '回复……' });
  return h('div', { class: 'report' },
    h('div', { class: 'row' },
      h('span', { class: `tag ${p.kind}` }, KIND[p.kind]), m ? h('span', { class: 'muted' }, missionName(m)) : null, status,
      h('span', { class: 'muted' }, `#${p.id} · ${p.author} · ${when(p.created_at)}`, p.updated_at !== p.created_at ? ` · 改于 ${when(p.updated_at)}` : '')),
    h('h3', {}, p.title),
    p.body ? h('pre', {}, p.body) : null,
    replies.map((r) => h('div', { class: 'reply' },
      h('span', { class: 'muted' }, `${r.author} · ${when(r.at)}`),
      (r.author === state.me.user || dev) ? button('删除', () => {
        if (confirm('删掉这条回复？')) act('/api/plan/reply-delete', { id: r.id }).catch(() => {});
      }) : null,
      h('pre', {}, r.body))),
    h('div', { class: 'row' }, input, button('回复', () => {
      if (input.value.trim()) act('/api/plan/reply', { proposal_id: p.id, body: input.value }).catch(() => {});
    })),
    mine ? h('div', { class: 'row' },
      button('编辑', () => startEdit(p)),
      button('删除意见', () => {
        if (confirm('删掉这条意见和它的回复？（放进大纲的新关卡也会一起拿掉）')) act('/api/plan/proposal-delete', { id: p.id }).catch(() => {});
      })) : null);
}

// ------------------------------------------------------------ the form

function kind() {
  return document.querySelector('input[name=kind]:checked').value;
}

function syncKind() {
  $('missionRow').classList.toggle('hide', kind() !== 'edit');
}

function renderMissionSelect() {
  const keep = $('mission').value;
  const groups = new Map();
  for (const m of state.missions) (groups.get(m.series) || groups.set(m.series, []).get(m.series)).push(m);
  $('mission').replaceChildren(...[...groups].map(([series, ms]) => h('optgroup', { label: series },
    ms.map((m) => h('option', { value: m.key }, `${code(m)} ${m.sc}${m.n ? '' : '（游戏里没有）'}`)))));
  if (keep) $('mission').value = keep;
}

function startProposal(k, mission) {
  stopEdit();
  document.querySelector(`input[name=kind][value=${k}]`).checked = true;
  if (mission) $('mission').value = mission;
  syncKind();
  $('form').scrollIntoView({ behavior: 'smooth' });
  $('ptitle').focus();
}

function startEdit(p) {
  editing = p.id;
  document.querySelector(`input[name=kind][value=${p.kind}]`).checked = true;
  for (const r of document.querySelectorAll('input[name=kind]')) r.disabled = true;
  if (p.mission) $('mission').value = p.mission;
  $('ptitle').value = p.title;
  $('pbody').value = p.body;
  $('formTitle').textContent = `2. 编辑意见 #${p.id}`;
  $('send').textContent = '保存';
  $('cancelEdit').classList.remove('hide');
  syncKind();
  $('form').scrollIntoView({ behavior: 'smooth' });
}

function stopEdit() {
  editing = null;
  for (const r of document.querySelectorAll('input[name=kind]')) r.disabled = false;
  $('formTitle').textContent = '2. 写意见';
  $('send').textContent = '提交';
  $('cancelEdit').classList.add('hide');
  $('form').reset();
  syncKind();
}

$('form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const data = { title: $('ptitle').value, body: $('pbody').value };
  if (editing) data.id = editing;
  else data.kind = kind();
  if (kind() === 'edit') data.mission = $('mission').value;
  $('send').disabled = true;
  try {
    const r = await act('/api/plan/proposal', data);
    $('sendState').textContent = `已保存 #${r.id}`;
    stopEdit();
  } catch (err) {
    $('sendState').textContent = '';
  } finally {
    $('send').disabled = false;
  }
});

// ------------------------------------------------------------ wiring

for (const r of document.querySelectorAll('input[name=kind]')) r.addEventListener('change', syncKind);
$('cancelEdit').addEventListener('click', stopEdit);
$('cancel').addEventListener('click', () => setPending(null));
$('filter').addEventListener('input', () => renderOutline());
$('show').addEventListener('change', renderProposals);
$('addLoop').addEventListener('click', () => {
  const note = prompt('分隔线的说明（例：第 2 轮回）', '下一个轮回');
  if (note != null) setPending({ verb: 'insert', kind: 'loop', note, label: '插入轮回分隔线：点一个「放到这里」' });
});
$('addNew').addEventListener('click', () => {
  const id = Number($('newPick').value);
  if (id) setPending({ verb: 'insert', kind: 'new', proposal_id: id, label: '放入新关卡：点一个「放到这里」' });
});
$('group').addEventListener('change', () => { group = $('group').value; pending = null; load().catch((e) => showError(e.message)); });

async function load() {
  state = await api('/api/plan' + (group == null ? '' : '?group=' + encodeURIComponent(group)));
  $('me').textContent = state.me.user + (state.me.role === 'dev' ? '（开发者）' : state.me.group ? `（${state.me.group} 组）` : '');
  if (state.me.role === 'dev') {
    if (group == null) group = state.group;
    $('groupBar').classList.remove('hide');
    $('group').replaceChildren(...state.groups.map((g) => h('option', { value: g, selected: g === state.group }, g || '（未分组）')));
  }
  renderMissionSelect();
  renderOutline();
  renderShow();
  renderProposals();
  syncKind();
}

load().catch((e) => showError('加载失败：' + e.message));
