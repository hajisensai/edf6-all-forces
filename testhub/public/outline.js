'use strict';
// The story outline page (/api/outline): the group's mission lines drawn as timelines, edited in place and saved
// whole. Players' text only ever goes into text nodes (h() below), never markup.

const $ = (id) => document.getElementById(id);
const EMPTY = { title: '', lanes: [] };
let state = null;      // the last /api/outline answer
let data = EMPTY;      // the outline being shown / edited
let group = null;
let editing = false;
let dirty = false;
let busy = false;
let current = null;    // what an open dialog edits

function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (key.startsWith('on')) el.addEventListener(key.slice(2), value);
    else if (value != null && value !== false) el.setAttribute(key, value === true ? '' : String(value));
  }
  for (const kid of kids.flat()) if (kid != null && kid !== '') el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  return el;
}

async function api(path, payload) {
  const res = await fetch(path, payload === undefined ? { cache: 'no-store' } : {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ ...payload, group }),
  });
  if (res.status === 401) { location.href = '/login.html'; throw new Error('请先登录'); }
  const out = await res.json();
  if (!res.ok) { const e = new Error(out.error || `HTTP ${res.status}`); e.status = res.status; throw e; }
  return out;
}

function error(text) {
  $('error').textContent = text;
  $('error').classList.toggle('hide', !text);
}

const uid = () => 'x' + Math.random().toString(36).slice(2, 10);
const lane = (id) => data.lanes.find((l) => l.id === id);
const swap = (arr, i, d) => { const j = i + d; if (j >= 0 && j < arr.length) [arr[i], arr[j]] = [arr[j], arr[i]]; };
const when = (ms) => new Date(ms).toLocaleString('zh-CN', { hour12: false });

function changed() { dirty = true; render(); }

// Rows of lanes: a lane that follows other lanes sits one row below the deepest of them.
function rows() {
  const depth = new Map();
  const d = (l, seen) => {
    if (depth.has(l.id)) return depth.get(l.id);
    if (seen.has(l.id)) return 0;
    seen.add(l.id);
    const parents = (l.after || []).map(lane).filter(Boolean);
    const v = parents.length ? 1 + Math.max(...parents.map((p) => d(p, seen))) : 0;
    depth.set(l.id, v);
    return v;
  };
  const out = [];
  for (const l of data.lanes) (out[d(l, new Set())] ||= []).push(l);
  return out.filter(Boolean);
}

function mergeBand(row) {
  const from = [...new Set(row.flatMap((l) => l.after || []))].map(lane).filter(Boolean);
  if (!from.length) return null;
  const NS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', '0 0 520 40');
  svg.setAttribute('aria-hidden', 'true');
  from.forEach((_, i) => {
    const x = from.length === 1 ? 260 : 40 + (440 * i) / (from.length - 1);
    const p = document.createElementNS(NS, 'path');
    p.setAttribute('d', `M${x} 0 C ${x} 22, 260 14, 260 32`);
    svg.append(p);
  });
  const tip = document.createElementNS(NS, 'polygon');
  tip.setAttribute('points', '253,30 267,30 260,40');
  svg.append(tip);
  return h('div', { class: 'olMerge' }, svg, h('span', {}, from.map((l) => l.name).join(' ＋ ') + ' 汇入'));
}

function newStep() { return { id: uid(), tag: '', title: '新的一步', body: '' }; }

function renderLane(l) {
  const li = data.lanes.indexOf(l);
  const small = (text, fn, disabled = false) => h('button', { type: 'button', class: 'btn ghost small', onclick: fn, disabled: disabled || busy }, text);
  const steps = l.steps.map((s, i) => h('li', { class: 'olStep' },
    h('span', { class: 'olDot' }, i + 1),
    h('div', { class: 'olCard', tabindex: editing ? 0 : null, role: editing ? 'button' : null,
      onclick: () => editing && openStep(l, s),
      onkeydown: (e) => { if (editing && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); openStep(l, s); } } },
      h('h3', {}, s.title, s.tag ? h('span', { class: 'olTag' }, s.tag) : null),
      s.body ? h('p', {}, s.body) : null),
    editing ? h('div', { class: 'row olTools' },
      small('上移', () => { swap(l.steps, i, -1); changed(); }, i === 0),
      small('下移', () => { swap(l.steps, i, 1); changed(); }, i === l.steps.length - 1),
      small('在下面插一步', () => { const n = newStep(); l.steps.splice(i + 1, 0, n); changed(); openStep(l, n); })) : null));
  return h('div', { class: 'olLane', 'data-tone': l.tone || 'edf' },
    h('div', { class: 'row' }, h('h3', { class: 'olName' }, l.name), h('span', { class: 'muted' }, `${l.steps.length} 步`)),
    l.note ? h('p', { class: 'muted olNote' }, l.note) : null,
    editing ? h('div', { class: 'row olTools' },
      small('改名称 / 说明 / 汇合', () => openLane(l)),
      small('往前', () => { swap(data.lanes, li, -1); changed(); }, li === 0),
      small('往后', () => { swap(data.lanes, li, 1); changed(); }, li === data.lanes.length - 1)) : null,
    h('ol', { class: 'olSteps' }, steps),
    editing ? small('＋ 在最后加一步', () => { const n = newStep(); l.steps.push(n); changed(); openStep(l, n); }) : null);
}

function render() {
  $('title').textContent = data.title || '剧情大纲';
  $('edit').textContent = editing ? '完成编辑' : '编辑';
  $('edit').disabled = busy;
  $('addLane').classList.toggle('hide', !editing);
  $('save').classList.toggle('hide', !editing && !dirty);
  $('save').disabled = !dirty || busy;
  $('saved').textContent = dirty ? '有改动还没保存' : state?.updated_at ? `最后由 ${state.updated_by} 于 ${when(state.updated_at)} 保存` : '';
  $('saved').classList.toggle('bad', dirty);
  if (!data.lanes.length) {
    $('board').replaceChildren(h('p', { class: 'muted' }, editing ? '点上面的「＋ 新任务线」开始，比如「5、6代任务线」。' : '这个组还没有剧情大纲。点「编辑」，再点「＋ 新任务线」开始写。'));
    return;
  }
  const parts = [];
  rows().forEach((row, i) => {
    if (i) { const m = mergeBand(row); if (m) parts.push(m); }
    parts.push(h('div', { class: 'olRow' }, row.map(renderLane)));
  });
  $('board').replaceChildren(h('div', { class: 'olBoard' + (editing ? ' editing' : '') }, parts));
}

function openStep(l, s) {
  current = { l, s };
  $('fTitle').value = s.title;
  $('fTag').value = s.tag || '';
  $('fBody').value = s.body || '';
  $('stepDlg').showModal();
  $('fTitle').focus();
}
$('stepForm').addEventListener('submit', () => {
  const { s } = current;
  s.title = $('fTitle').value.trim() || '（未命名）';
  s.tag = $('fTag').value.trim();
  s.body = $('fBody').value.replace(/\s+$/, '');
  changed();
});
$('fCancel').addEventListener('click', () => $('stepDlg').close());
// Delete asks twice on the button itself (no confirm(): it is easy to click through).
function armed(btn, text) {
  if (btn.dataset.armed === '1') return true;
  btn.dataset.armed = '1';
  btn.textContent = text;
  return false;
}
$('fDelete').addEventListener('click', () => {
  const { l, s } = current;
  if (!armed($('fDelete'), '再点一次确认删除')) return;
  l.steps.splice(l.steps.indexOf(s), 1);
  $('stepDlg').close();
  changed();
});
$('stepDlg').addEventListener('close', () => { $('fDelete').dataset.armed = ''; $('fDelete').textContent = '删除这一步'; });

function openLane(l) {
  current = { l };
  const x = l || { name: '', note: '', tone: 'edf', after: [] };
  $('laneDlgTitle').textContent = l ? '编辑任务线' : '新任务线';
  $('lName').value = x.name;
  $('lNote').value = x.note || '';
  $('lTone').value = x.tone || 'edf';
  $('lAfter').replaceChildren(...data.lanes.filter((o) => o !== l).map((o) =>
    h('label', {}, h('input', { type: 'checkbox', value: o.id, checked: (x.after || []).includes(o.id) }), o.name)));
  if (!$('lAfter').children.length) $('lAfter').append(h('span', { class: 'muted' }, '（还没有别的任务线）'));
  $('lDelete').classList.toggle('hide', !l);
  $('laneDlg').showModal();
  $('lName').focus();
}
$('laneForm').addEventListener('submit', () => {
  let { l } = current;
  if (!l) { l = { id: uid(), steps: [] }; data.lanes.push(l); }
  l.name = $('lName').value.trim() || '（未命名）';
  l.note = $('lNote').value.trim();
  l.tone = $('lTone').value;
  l.after = [...$('lAfter').querySelectorAll('input:checked')].map((c) => c.value);
  changed();
});
$('lCancel').addEventListener('click', () => $('laneDlg').close());
$('lDelete').addEventListener('click', () => {
  const { l } = current;
  if (!armed($('lDelete'), `再点一次删除「${l.name}」和它的 ${l.steps.length} 步`)) return;
  data.lanes.splice(data.lanes.indexOf(l), 1);
  for (const o of data.lanes) o.after = (o.after || []).filter((id) => id !== l.id);
  $('laneDlg').close();
  changed();
});
$('laneDlg').addEventListener('close', () => { $('lDelete').dataset.armed = ''; $('lDelete').textContent = '删除整条线'; });

function asText() {
  return rows().flat().map((l) => [
    `【${l.name}】` + ((l.after || []).length ? `（接在 ${l.after.map((id) => lane(id)?.name).filter(Boolean).join('、')} 之后）` : ''),
    l.note || null,
    ...l.steps.map((s, i) => `${i + 1}. ${s.title}${s.tag ? `［${s.tag}］` : ''}` + (s.body ? '\n   ' + s.body.replace(/\n/g, '\n   ') : '')),
  ].filter((x) => x != null).join('\n')).join('\n\n');
}
$('copy').addEventListener('click', async () => {
  const text = asText();
  try { await navigator.clipboard.writeText(text); $('saved').textContent = '已复制到剪贴板'; }
  catch { $('copyBox').value = text; $('copyBox').classList.remove('hide'); $('copyBox').select(); $('saved').textContent = '自动复制不可用：下面的文字已选中，按 Ctrl+C 复制'; }
});

$('edit').addEventListener('click', () => { editing = !editing; render(); });
$('addLane').addEventListener('click', () => openLane(null));
$('save').addEventListener('click', async () => {
  if (busy || !dirty) return;
  busy = true;
  error('');
  render();
  try {
    await api('/api/outline/save', { outline: data, version: state.version });
    dirty = false;
    await load();
  } catch (e) {
    error(e.status === 409 ? e.message + '（可以先「复制为文字」留住你的改动）' : '保存失败：' + e.message);
  } finally { busy = false; render(); }
});
window.addEventListener('beforeunload', (e) => { if (dirty) e.preventDefault(); });

async function load() {
  state = await api('/api/outline' + (group == null ? '' : '?group=' + encodeURIComponent(group)));
  group = state.group;
  data = state.outline ? structuredClone(state.outline) : structuredClone(EMPTY);
  $('me').textContent = state.me.user + (state.me.role === 'dev' ? '（开发者）' : state.me.group ? `（${state.me.group} 组）` : '');
  $('groupBar').classList.toggle('hide', state.me.role !== 'dev');
  $('group').replaceChildren(...state.groups.map((g) => h('option', { value: g, selected: g === group }, g || '（未分组）')));
  render();
}
let leaveArmed = false;   // switching group with unsaved edits: the second switch discards them
$('group').addEventListener('change', () => {
  if (dirty && !leaveArmed) {
    leaveArmed = true;
    error('当前组的大纲有改动还没保存：先保存，或再切换一次放弃改动');
    $('group').value = group;
    return;
  }
  leaveArmed = false;
  dirty = false;
  group = $('group').value;
  error('');
  load().catch((e) => error(e.message));
});
load().catch((e) => { error('加载失败：' + e.message); $('board').textContent = '暂时无法加载大纲，请刷新重试。'; });
