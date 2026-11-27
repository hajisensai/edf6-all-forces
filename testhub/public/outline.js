'use strict';
// 剧情大纲 (/api/outline): a group's outlines (several per group, `book`), each one's world lines drawn as a map (which
// line leads into which) and as timelines of steps. It says what happens; 关卡策划 (/story.html) says how each mission
// plays it, and every step lists the missions written for it.
// Anyone reads; the group's members edit in place (编辑 mode): typing changes the outline at once and goes into a draft
// in this browser, so a closed tab loses nothing; 保存 / Ctrl+S sends the whole outline with the version it was made
// from, and an outline someone saved meanwhile is never overwritten without asking. Every change can be undone
// (Ctrl+Z outside a text box, or the 撤销 buttons). Players' text only goes into text nodes (ui.js h()).

const $ = (id) => document.getElementById(id);
const EMPTY = { title: '', lanes: [] };
const TONES = { edf: '绿', hazard: '橙', violet: '紫' };
const STATUS = { planned: '构思中', writing: '制作中', testing: '待测试', done: '已完成' };
const params = new URLSearchParams(location.search);
let state = null;        // the last /api/outline answer
let data = structuredClone(EMPTY);   // the outline shown / being edited
let base = JSON.stringify(EMPTY);    // what the server has (to tell whether there are changes)
let version = 0;         // the server version the edits started from
let group = params.get('group');
let book = Number(params.get('book')) || null;   // the outline shown; 0 = a new one not saved yet
let editing = false;
let busy = false;
let missions = [];       // the group's missions (/api/story), in campaign order
let designs = [];        // the group's 设定集 entries (/api/design)
let openStep = null;     // the step whose form is open (editing)
let problem = null;      // {kind: 'conflict' | 'error', text}
let offer = null;        // a draft found for this outline: {data, version, at}
let typed = null;        // the field whose typing already took an undo snapshot
let drag = null;         // the step being dragged: {lane, step}
const past = [];
const future = [];
let firstLoad = true;

const uid = () => 'x' + Math.random().toString(36).slice(2, 10);
const laneOf = (id) => data.lanes.find((l) => l.id === id);
const dirty = () => JSON.stringify(data) !== base;
const draftKey = () => `edf6.outline.${group ?? ''}.${book || 0}`;
const when = (ms) => new Date(ms).toLocaleString('zh-CN', { hour12: false });
const savedSteps = () => new Set(JSON.parse(base).lanes.flatMap((l) => l.steps.map((s) => s.id)));

function error(text) {
  $('error').textContent = text;
  $('error').classList.toggle('hide', !text);
}

// ---- changes, undo, drafts ----

function snapshot() {
  past.push(JSON.stringify(data));
  if (past.length > 100) past.shift();
  future.length = 0;
}

function stash() {
  if (!state?.editable) return;
  if (dirty()) drafts.set(draftKey(), { data, version });
  else drafts.drop(draftKey());
}

// A change to the outline's shape: undoable, kept as a draft, drawn again.
function change(fn) {
  snapshot();
  fn();
  typed = null;
  stash();
  render();
}

// Typing into a field: one undo step per field until another field is typed in.
function edited(id, fn) {
  if (typed !== id) { snapshot(); typed = id; }
  fn();
  stash();
  status();
}

function undo() {
  if (!past.length) return toast('没有可以撤销的改动');
  future.push(JSON.stringify(data));
  data = JSON.parse(past.pop());
  typed = null;
  stash();
  render();
}

function redo() {
  if (!future.length) return toast('没有可以重做的改动');
  past.push(JSON.stringify(data));
  data = JSON.parse(future.pop());
  typed = null;
  stash();
  render();
}

// ---- rendering ----

// Columns of lines: a line that follows others sits one column right of the deepest of them.
function columns() {
  const depth = new Map();
  const d = (l, seen) => {
    if (depth.has(l.id)) return depth.get(l.id);
    if (seen.has(l.id)) return 0;
    seen.add(l.id);
    const parents = (l.after || []).map(laneOf).filter(Boolean);
    const v = parents.length ? 1 + Math.max(...parents.map((p) => d(p, seen))) : 0;
    depth.set(l.id, v);
    return v;
  };
  const out = [];
  for (const l of data.lanes) (out[d(l, new Set())] ||= []).push(l);
  return out.filter(Boolean);
}

function missionsOf(stepId) {
  return book ? missions.filter((m) => m.book === book && m.step === stepId) : [];
}

function status() {
  const canEdit = !!state?.editable;
  const d = dirty();
  $('save').classList.toggle('hide', !editing && !d);
  $('save').disabled = !d || busy;
  $('undo').disabled = !past.length;
  $('redo').disabled = !future.length;
  $('saved').classList.toggle('bad', d);
  $('saved').textContent = busy ? '保存中…' : d ? (drafts.ok ? '有改动还没保存 · 草稿已存在本机 · Ctrl+S 保存' : '有改动还没保存 · Ctrl+S 保存')
    : state?.updated_at ? (state.author ? `创建者：${state.author} · ` : '') + `最后由 ${state.updated_by} 于 ${when(state.updated_at)} 保存` : '';
  $('edit').classList.toggle('hide', !canEdit);
}

function render() {
  keepFocus(() => {
    const canEdit = !!state?.editable;
    if (!canEdit) editing = false;
    $('title').textContent = data.title || (book ? `大纲 ${book}` : '新大纲');
    $('title').classList.toggle('hide', editing);
    $('titleInput').classList.toggle('hide', !editing);
    if (document.activeElement !== $('titleInput')) $('titleInput').value = data.title || '';
    for (const id of ['addLane', 'undo', 'redo']) $(id).classList.toggle('hide', !editing);
    $('removeBook').classList.toggle('hide', !editing || !book || !state?.member);
    $('badges').replaceChildren(...fill(state?.main ? h('span', { class: 'badge olMain' }, '主大纲') : null,
      state?.open ? h('span', { class: 'badge', title: '有账号的人都能编辑，不限组' }, '所有账号可编辑') : null));
    $('who').textContent = state?.open ? '登录的账号都能改' : '同组成员都能改';
    $('removeBook').disabled = busy;
    $('newBook').disabled = busy;
    $('edit').textContent = editing ? '完成编辑' : '编辑';
    $('edit').setAttribute('aria-pressed', String(editing));
    $('edit').disabled = busy;
    $('hint').classList.toggle('hide', !editing);
    status();
    renderOffer();
    renderProblem();
    renderMap();
    if (!data.lanes.length) {
      $('board').replaceChildren(h('div', { class: 'stEmpty' }, h('p', { class: 'muted' }, group == null ? '还没有哪个组写了剧情大纲。'
        : !canEdit ? '这个组还没有剧情大纲。'
          : editing ? '先在上面填大纲名称，再点「＋ 新世界线」开始。' : '这份大纲还是空的。'),
      canEdit && !editing ? h('button', { type: 'button', class: 'btn', onclick: () => { editing = true; render(); } }, '开始编辑') : null,
      editing ? h('button', { type: 'button', class: 'btn', onclick: addLane }, '＋ 新世界线') : null));
      return;
    }
    const parts = [];
    columns().forEach((col, i) => {
      if (i) { const m = mergeBand(col); if (m) parts.push(m); }
      parts.push(h('div', { class: 'olRow' }, col.map(renderLane)));
    });
    $('board').replaceChildren(h('div', { class: 'olBoard' + (editing ? ' editing' : '') }, parts));
  });
}

// replaceChildren() takes nodes only: drop the empty parts, flatten the lists.
const fill = (...kids) => kids.flat(Infinity).filter((k) => k != null && k !== false);

function renderOffer() {
  const box = $('draftBar');
  if (!offer || !state?.editable) { box.classList.add('hide'); box.replaceChildren(); return; }
  box.classList.remove('hide');
  box.replaceChildren(...fill(h('span', {}, `本机有这份大纲没保存的改动（${ago(offer.at)}）。`),
    offer.version !== version ? h('span', { class: 'bad' }, '之后别人又保存过它，恢复后保存时会先问你怎么处理。') : null,
    h('button', { type: 'button', class: 'btn small', onclick: () => {
      snapshot();
      data = offer.data;
      version = offer.version;
      offer = null;
      editing = true;
      stash();
      render();
      toast('已恢复草稿，继续编辑');
    } }, '恢复并继续编辑'),
    h('button', { type: 'button', class: 'btn ghost small', onclick: () => { drafts.drop(draftKey()); offer = null; render(); toast('已丢弃草稿'); } }, '丢弃')));
}

function renderProblem() {
  const box = $('problem');
  box.classList.toggle('hide', !problem);
  if (!problem) { box.replaceChildren(); return; }
  box.className = 'stProblem ' + problem.kind;
  const btn = (text, fn, main = false) => h('button', { type: 'button', class: main ? 'btn small' : 'btn ghost small', onclick: fn }, text);
  box.replaceChildren(...fill(h('span', {}, problem.text),
    problem.kind === 'conflict' ? [btn('用我的覆盖', overwrite, true), btn('看最新版（丢掉我的）', discardMine), btn('复制我的为文字', copyOutline)] : btn('重试', save, true),
    h('button', { type: 'button', class: 'toastX', 'aria-label': '关闭', onclick: () => { problem = null; renderProblem(); } }, '×')));
}

// The map: one box per world line, arrows from the lines it follows; each box counts its steps and missions and has a
// dot per step (filled once the step has a mission). A click (or Enter) goes to the line.
function renderMap() {
  const wrap = $('mapBox');
  wrap.classList.toggle('hide', !data.lanes.length);
  if (!data.lanes.length) return;
  const NS = 'http://www.w3.org/2000/svg';
  const s = (tag, attrs, ...kids) => {
    const el = document.createElementNS(NS, tag);
    for (const [k, v] of Object.entries(attrs)) if (v != null) el.setAttribute(k, String(v));
    for (const kid of kids) if (kid != null) el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
    return el;
  };
  const cols = columns();
  const W = 210, H = 74, GX = 64, GY = 14, P = 6;
  const tall = Math.max(...cols.map((c) => c.length)) * (H + GY) - GY;
  const at = new Map();
  cols.forEach((col, c) => {
    const top = P + (tall - (col.length * (H + GY) - GY)) / 2;
    col.forEach((l, i) => at.set(l.id, { x: P + c * (W + GX), y: top + i * (H + GY) }));
  });
  const width = cols.length * (W + GX) - GX + 2 * P;
  const height = tall + 2 * P;
  const svg = s('svg', { viewBox: `0 0 ${width} ${height}`, width, height, class: 'olMap', role: 'group', 'aria-label': '世界线总图' },
    s('defs', {}, s('marker', { id: 'arrow', viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse' },
      s('path', { d: 'M0 0 L10 5 L0 10 z', class: 'olArrow' }))));
  for (const l of data.lanes) {
    const b = at.get(l.id);
    for (const pid of l.after || []) {
      const a = at.get(pid);
      if (!a) continue;
      const x1 = a.x + W, y1 = a.y + H / 2, x2 = b.x - 2, y2 = b.y + H / 2, mx = (x1 + x2) / 2;
      svg.append(s('path', { d: `M${x1} ${y1} C ${mx} ${y1}, ${mx} ${y2}, ${x2} ${y2}`, class: 'olEdge', 'marker-end': 'url(#arrow)' }));
    }
  }
  for (const l of data.lanes) {
    const { x, y } = at.get(l.id);
    const ms = l.steps.flatMap((st) => missionsOf(st.id));
    const done = ms.filter((m) => m.status === 'done').length;
    const name = [...l.name].length > 12 ? [...l.name].slice(0, 11).join('') + '…' : l.name;
    const info = `${l.steps.length} 步 · ${ms.length} 关` + (ms.length ? ` · 完成 ${done}` : '');
    const g = s('g', { class: 'olNode', 'data-tone': l.tone || 'edf', tabindex: 0, role: 'link', 'aria-label': `${l.name}：${info}，跳到这条线` },
      s('title', {}, `${l.name}\n${info}`),
      s('rect', { x, y, width: W, height: H, rx: 9 }),
      s('rect', { x, y, width: 5, height: H, rx: 2, class: 'olNodeBar' }),
      s('text', { x: x + 16, y: y + 24, class: 'olNodeName' }, name),
      s('text', { x: x + 16, y: y + 44, class: 'olNodeInfo' }, info));
    l.steps.slice(0, 16).forEach((st, k) => {
      const has = missionsOf(st.id).length > 0;
      g.append(s('circle', { cx: x + 20 + k * 11.5, cy: y + H - 14, r: 4, class: has ? 'olPip on' : 'olPip' }, s('title', {}, `${k + 1}. ${st.title}${has ? '（有关卡）' : ''}`)));
    });
    const go = () => {
      const el = $(`lane-${l.id}`);
      if (!el) return;
      el.scrollIntoView({ behavior: 'smooth', block: 'start' });
      flash(el);
    };
    g.addEventListener('click', go);
    g.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); go(); } });
    svg.append(g);
  }
  const done = missions.filter((m) => m.book === book).length;
  const stepsAll = data.lanes.reduce((n, l) => n + l.steps.length, 0);
  const covered = data.lanes.reduce((n, l) => n + l.steps.filter((st) => missionsOf(st.id).length).length, 0);
  $('mapInfo').textContent = `${data.lanes.length} 条世界线 · ${stepsAll} 步 · ${done} 个关卡` + (stepsAll ? ` · ${covered} / ${stepsAll} 步已有关卡` : '');
  $('map').replaceChildren(svg);
}

function flash(el) {
  el.classList.remove('flash');
  void el.offsetWidth;
  el.classList.add('flash');
}

function mergeBand(col) {
  const from = [...new Set(col.flatMap((l) => l.after || []))].map(laneOf).filter(Boolean);
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

const small = (text, fn, opts = {}) => h('button', { type: 'button', class: 'btn ghost small' + (opts.danger ? ' danger' : ''), onclick: fn,
  disabled: opts.disabled || busy, title: opts.title || null, 'aria-label': opts.label || null }, text);

function renderLane(l) {
  const li = data.lanes.indexOf(l);
  const ms = l.steps.flatMap((st) => missionsOf(st.id));
  const head = editing ? laneEditor(l, li) : [
    h('div', { class: 'row' }, h('h3', { class: 'olName' }, l.name), h('span', { class: 'muted' }, `${l.steps.length} 步 · ${ms.length} 关`)),
    l.note ? h('p', { class: 'muted olNote' }, l.note) : null,
    (l.after || []).length ? h('p', { class: 'muted small' }, '接在 ', l.after.map((id) => laneOf(id)?.name).filter(Boolean).join('、'), ' 之后') : null];
  const list = h('ol', { class: 'olSteps' }, l.steps.map((st, i) => renderStep(l, st, i)));
  if (editing) {
    // Dropping below the last step (or into an empty line) puts the step at the end.
    list.addEventListener('dragover', (e) => { if (drag && e.target === list) { e.preventDefault(); list.classList.add('dropInto'); } });
    list.addEventListener('dragleave', () => list.classList.remove('dropInto'));
    list.addEventListener('drop', (e) => {
      if (!drag || e.target !== list) return;
      e.preventDefault();
      const d = drag;
      endDrag();
      moveStep(d.lane, d.step, l, l.steps.length - (d.lane === l ? 1 : 0));
    });
  }
  return h('section', { class: 'olLane', id: `lane-${l.id}`, 'data-tone': l.tone || 'edf', 'aria-label': l.name }, head, list,
    editing ? small('＋ 在最后加一步', () => addStep(l, l.steps.length)) : null);
}

function laneEditor(l, li) {
  const name = h('input', { type: 'text', id: `ln-${l.id}`, class: 'olNameInput', maxlength: 40, value: l.name, 'aria-label': '世界线名称', placeholder: '世界线名称' });
  name.addEventListener('input', () => edited(name.id, () => { l.name = name.value; }));
  name.addEventListener('change', () => { if (!l.name.trim()) { l.name = '（未命名）'; name.value = l.name; stash(); } renderMap(); });
  const note = h('textarea', { id: `lnote-${l.id}`, maxlength: 1000, rows: 2, placeholder: '这条线讲什么（可空）', 'aria-label': '说明' });
  note.value = l.note || '';
  note.addEventListener('input', () => { edited(note.id, () => { l.note = note.value; }); grow(note); });
  const tone = h('div', { class: 'seg', role: 'radiogroup', 'aria-label': '颜色' }, Object.entries(TONES).map(([v, t]) =>
    h('button', { type: 'button', role: 'radio', class: 'tone-' + v, 'aria-checked': String((l.tone || 'edf') === v), onclick: () => change(() => { l.tone = v; }) }, t)));
  const others = data.lanes.filter((o) => o !== l);
  const after = h('div', { class: 'choices' }, others.length ? others.map((o) => {
    const box = h('input', { type: 'checkbox', checked: (l.after || []).includes(o.id) });
    box.addEventListener('change', () => change(() => { l.after = box.checked ? [...new Set([...(l.after || []), o.id])] : (l.after || []).filter((x) => x !== o.id); }));
    return h('label', {}, box, o.name);
  }) : h('span', { class: 'muted' }, '（还没有别的世界线）'));
  const more = h('details', { class: 'olMore', id: `lmore-${l.id}` }, h('summary', {}, '说明、颜色、接在哪些线后面'),
    h('div', { class: 'stField' }, note), h('div', { class: 'stField' }, h('span', { class: 'lbl' }, '颜色'), tone),
    h('div', { class: 'stField' }, h('span', { class: 'lbl' }, '接在哪些线后面（几条线汇合就都勾上）'), after));
  if (!l.name || l.name === '新世界线') more.open = true;
  queueMicrotask(() => grow(note));
  return [h('div', { class: 'row olLaneHead' }, name,
    small('←', () => change(() => swap(data.lanes, li, -1)), { disabled: li === 0, label: '往前移这条线', title: '往前移' }),
    small('→', () => change(() => swap(data.lanes, li, 1)), { disabled: li === data.lanes.length - 1, label: '往后移这条线', title: '往后移' }),
    small('删除', () => removeLane(l), { danger: true, label: `删除世界线「${l.name}」` })), more];
}

function swap(arr, i, d) {
  const j = i + d;
  if (j >= 0 && j < arr.length) [arr[i], arr[j]] = [arr[j], arr[i]];
}

function renderStep(l, st, i) {
  const ms = missionsOf(st.id);
  const linkable = book && savedSteps().has(st.id);
  const open = editing && openStep === st.id;
  const item = h('li', { class: 'olStep' + (open ? ' open' : ''), id: `step-${st.id}`, 'data-tone': st.tone || null }, h('span', { class: 'olDot' }, i + 1));
  if (open) item.append(stepEditor(l, st, i));
  else {
    const cardAttrs = editing ? { class: 'olCard', tabindex: 0, role: 'button', 'aria-label': `编辑第 ${i + 1} 步「${st.title}」`,
      onclick: (e) => { if (!e.target.closest('a')) openForm(st.id); },
      onkeydown: (e) => stepKeys(e, l, st, i) } : { class: 'olCard' };
    item.append(h('div', cardAttrs,
      h('h3', {}, editing ? h('span', { class: 'olGrip', draggable: 'true', title: '拖动到别的位置或别的线（或选中后按 Alt+方向键）', 'aria-hidden': 'true' }, '⠿') : null,
        st.title, st.tag ? h('span', { class: 'olTag' }, st.tag) : null),
      st.body ? h('p', {}, st.body) : null));
  }
  // The missions written for this step, and (for its group) a way to write one more.
  if (ms.length || (state.editable && linkable)) {
    item.append(h('ul', { class: 'olMissions', 'aria-label': `第 ${i + 1} 步的关卡` },
      ms.map((m) => h('li', {}, h('a', { href: `/story.html?group=${encodeURIComponent(group ?? '')}&id=${m.id}` },
        h('span', { class: 'olMNo' }, `第 ${m.n} 关`), m.title), h('span', { class: 'badge st-' + m.status }, STATUS[m.status] || m.status))),
      state.editable && linkable ? h('li', {}, h('a', { class: 'olWrite', href: `/story.html?group=${encodeURIComponent(group ?? '')}&new=1&book=${book}&step=${encodeURIComponent(st.id)}` }, '＋ 写关卡')) : null));
  } else if (editing && !linkable) {
    item.append(h('p', { class: 'muted small olMissions' }, '保存大纲后就能给这一步写关卡'));
  }
  // The 设定集 entries that appear in this step.
  const ds = book ? designs.filter((d) => d.book === book && d.steps.includes(st.id)) : [];
  if (ds.length || (state.editable && linkable)) {
    const g = encodeURIComponent(group ?? '');
    item.append(h('p', { class: 'olDesigns small', 'aria-label': `第 ${i + 1} 步的设定` }, ds.length ? h('span', { class: 'muted' }, '设定：') : null,
      ds.map((d) => h('a', { class: 'tag', href: `/design.html?group=${g}&id=${d.id}` }, d.name)),
      state.editable && linkable ? h('a', { class: 'olWrite', href: `/design.html?group=${g}&new=1&book=${book}&step=${encodeURIComponent(st.id)}` }, '＋ 设定') : null));
  }
  if (editing && !open) {
    const grip = item.querySelector('.olGrip');
    grip.addEventListener('dragstart', (e) => {
      drag = { lane: l, step: st };
      e.dataTransfer.effectAllowed = 'move';
      e.dataTransfer.setData('text/plain', st.id);
      e.dataTransfer.setDragImage(item, 16, 16);
      item.classList.add('dragging');
      document.body.classList.add('dragActive');
    });
    grip.addEventListener('dragend', endDrag);
    item.addEventListener('dragover', (e) => {
      if (!drag || drag.step === st) return;
      e.preventDefault();
      const above = e.clientY < item.getBoundingClientRect().top + item.offsetHeight / 2;
      item.classList.toggle('dropBefore', above);
      item.classList.toggle('dropAfter', !above);
    });
    item.addEventListener('dragleave', () => item.classList.remove('dropBefore', 'dropAfter'));
    item.addEventListener('drop', (e) => {
      if (!drag || drag.step === st) return;
      e.preventDefault();
      const above = item.classList.contains('dropBefore');
      const d = drag;
      endDrag();
      let to = i + (above ? 0 : 1);
      if (d.lane === l && d.lane.steps.indexOf(d.step) < to) to -= 1;
      moveStep(d.lane, d.step, l, to);
    });
  }
  return item;
}

function endDrag() {
  drag = null;
  document.body.classList.remove('dragActive');
  for (const el of document.querySelectorAll('.dragging, .dropBefore, .dropAfter, .dropInto')) el.classList.remove('dragging', 'dropBefore', 'dropAfter', 'dropInto');
}

// Keys on a step (editing): Enter opens it, Alt+↑/↓ moves it in its line, Alt+←/→ to the same place in the next line,
// Delete removes it (undoable).
function stepKeys(e, l, st, i) {
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); openForm(st.id); return; }
  if (e.key === 'Delete') { e.preventDefault(); removeStep(l, st); return; }
  if (!e.altKey) return;
  const li = data.lanes.indexOf(l);
  const move = { ArrowUp: [l, i - 1], ArrowDown: [l, i + 1], ArrowLeft: [data.lanes[li - 1], i], ArrowRight: [data.lanes[li + 1], i] }[e.key];
  if (!move) return;
  e.preventDefault();
  const [to, at] = move;
  if (!to || at < 0 || (to === l && at >= l.steps.length)) return;
  moveStep(l, st, to, Math.min(at, to.steps.length - (to === l ? 1 : 0)));
  $(`step-${st.id}`)?.querySelector('.olCard')?.focus();
}

function moveStep(from, st, to, index) {
  const at = from.steps.indexOf(st);
  if (at < 0 || (from === to && at === index)) return;
  change(() => {
    from.steps.splice(at, 1);
    to.steps.splice(Math.max(0, Math.min(index, to.steps.length)), 0, st);
  });
  flash($(`step-${st.id}`));
  if (from !== to) toast(`「${st.title}」移到了「${to.name}」第 ${to.steps.indexOf(st) + 1} 步`, { label: '撤销', fn: undo });
}

function stepEditor(l, st, i) {
  const field = (key, input, label) => h('div', { class: 'stField' }, h('label', { for: input.id }, label), input);
  const title = h('input', { type: 'text', id: `st-title-${st.id}`, maxlength: 80, value: st.title, autocomplete: 'off' });
  title.addEventListener('input', () => edited(title.id, () => { st.title = title.value; }));
  const tag = h('input', { type: 'text', id: `st-tag-${st.id}`, maxlength: 20, value: st.tag || '', placeholder: '比如「关卡」「剧情」「DLC」', autocomplete: 'off' });
  tag.addEventListener('input', () => edited(tag.id, () => { st.tag = tag.value; }));
  const body = h('textarea', { id: `st-body-${st.id}`, maxlength: 4000, rows: 4, placeholder: '这一步发生什么' });
  body.value = st.body || '';
  body.addEventListener('input', () => { edited(body.id, () => { st.body = body.value; }); grow(body); });
  queueMicrotask(() => grow(body));
  const tone = h('div', { class: 'seg', role: 'radiogroup', 'aria-label': '颜色' },
    [['', '跟随线'], ...Object.entries(TONES)].map(([v, t]) => h('button', { type: 'button', role: 'radio', class: v ? 'tone-' + v : '', 'aria-checked': String((st.tone || '') === v),
      onclick: () => change(() => { if (v) st.tone = v; else delete st.tone; }) }, t)));
  const li = data.lanes.indexOf(l);
  const where = h('select', { id: `st-lane-${st.id}`, 'aria-label': '移到哪条线' },
    data.lanes.map((o) => h('option', { value: o.id, selected: o === l }, o.name)));
  where.addEventListener('change', () => { const to = laneOf(where.value); if (to && to !== l) moveStep(l, st, to, to.steps.length); });
  const close = () => {
    if (!st.title.trim()) { st.title = '（未命名）'; stash(); }
    openStep = null;
    typed = null;
    render();
    $(`step-${st.id}`)?.querySelector('.olCard')?.focus();
  };
  return h('div', { class: 'olForm', onkeydown: (e) => { if (e.key === 'Escape') { e.preventDefault(); close(); } } },
    h('div', { class: 'olFormGrid' }, field('title', title, '标题'), field('tag', tag, '标签')),
    field('body', body, '内容'),
    h('div', { class: 'stField' }, h('span', { class: 'lbl' }, '颜色（区分同一条线里不同来源的步骤）'), tone),
    h('div', { class: 'row olFormBar' }, h('button', { type: 'button', class: 'btn small', onclick: close, title: 'Esc' }, '完成'),
      small('↑', () => moveStep(l, st, l, i - 1), { disabled: i === 0, label: '上移', title: '上移（Alt+↑）' }),
      small('↓', () => moveStep(l, st, l, i + 1), { disabled: i === l.steps.length - 1, label: '下移', title: '下移（Alt+↓）' }),
      h('span', { class: 'moveTo' }, '移到', where), li >= 0 ? small('＋ 在下面插一步', () => addStep(l, i + 1)) : null,
      h('span', { class: 'grow' }), small('删除这一步', () => removeStep(l, st), { danger: true })));
}

function openForm(id) {
  openStep = id;
  typed = null;
  render();
  $(`st-title-${id}`)?.focus();
}

function addStep(l, at) {
  const st = { id: uid(), tag: '', title: '新的一步', body: '' };
  change(() => l.steps.splice(at, 0, st));
  openForm(st.id);
  $(`st-title-${st.id}`)?.select();
}

function addLane() {
  const l = { id: uid(), name: '新世界线', note: '', tone: 'edf', after: [], steps: [] };
  change(() => data.lanes.push(l));
  const input = $(`ln-${l.id}`);
  input?.scrollIntoView({ block: 'center', behavior: 'smooth' });
  input?.focus();
  input?.select();
}

function removeStep(l, st) {
  const n = missionsOf(st.id).length;
  change(() => { l.steps.splice(l.steps.indexOf(st), 1); if (openStep === st.id) openStep = null; });
  toast(`已删除「${st.title}」` + (n ? `；有 ${n} 个关卡关联着这一步，保存后它们会显示「大纲里已没有这一步」` : ''), { label: '撤销', fn: undo }, n ? 'warn' : '');
}

function removeLane(l) {
  const n = l.steps.reduce((k, st) => k + missionsOf(st.id).length, 0);
  change(() => {
    data.lanes.splice(data.lanes.indexOf(l), 1);
    for (const o of data.lanes) o.after = (o.after || []).filter((id) => id !== l.id);
  });
  toast(`已删除世界线「${l.name}」和它的 ${l.steps.length} 步` + (n ? `（${n} 个关卡关联着它们）` : ''), { label: '撤销', fn: undo }, n ? 'warn' : '');
}

function asText() {
  return [data.title ? `# ${data.title}` : null, ...columns().flat().map((l) => [
    `【${l.name}】` + ((l.after || []).length ? `（接在 ${l.after.map((id) => laneOf(id)?.name).filter(Boolean).join('、')} 之后）` : ''),
    l.note || null,
    ...l.steps.map((st, i) => `${i + 1}. ${st.title}${st.tag ? `［${st.tag}］` : ''}` + (st.body ? '\n   ' + st.body.replace(/\n/g, '\n   ') : '')),
  ].filter((x) => x != null).join('\n'))].filter(Boolean).join('\n\n');
}

async function copyOutline() {
  toast((await copyText(asText())) ? '已复制为文字' : '复制失败：浏览器不让访问剪贴板');
}

// ---- saving ----

async function save() {
  if (busy || !dirty() || !state?.editable) return;
  busy = true;
  problem = null;
  error('');
  status();
  const key = draftKey();
  try {
    const out = await api('/api/outline/save', { group, book: book || 0, outline: data, version: book ? version : 0 });
    drafts.drop(key);
    book = out.book;
    busy = false;
    await load();
    toast('大纲已保存');
  } catch (e) {
    busy = false;
    problem = e.status === 409 && book ? { kind: 'conflict', text: '别人刚保存过这份大纲，你的改动还没存上。' } : { kind: 'error', text: '保存失败：' + e.message };
    render();
  }
}

// Mine wins: take the newest version number and save again (the other person's version is replaced).
async function overwrite() {
  const fresh = await api(`/api/outline?group=${encodeURIComponent(group ?? '')}&book=${book}`);
  if (fresh.book !== book) { problem = { kind: 'error', text: '这份大纲已经被删掉了；可以「复制我的为文字」后新建一份。' }; renderProblem(); return; }
  version = fresh.version;
  problem = null;
  await save();
}

async function discardMine() {
  drafts.drop(draftKey());
  problem = null;
  past.length = future.length = 0;
  await load();
}

// ---- loading, switching ----

async function load() {
  const q = new URLSearchParams();
  if (group != null) q.set('group', group);
  if (book) q.set('book', book);
  const [answer, story, design] = await Promise.all([api('/api/outline' + (q.size ? '?' + q : '')),
    api('/api/story' + (group == null ? '' : '?group=' + encodeURIComponent(group))).catch(() => null),
    api('/api/design' + (group == null ? '' : '?group=' + encodeURIComponent(group))).catch(() => null)]);
  state = answer;
  designs = design && design.group === state.group ? design.rows : [];
  group = state.group;
  if (book !== 0 || !state.member) book = state.book || null;
  if (book === null && state.member && !state.books.length) book = 0;   // nothing yet: the first save makes outline 1
  data = book && state.outline ? structuredClone(state.outline) : structuredClone(EMPTY);
  base = JSON.stringify(data);
  version = book ? state.version : 0;
  missions = story && story.group === group ? story.rows.map((r, i) => ({ ...r, n: i + 1 })) : [];
  past.length = future.length = 0;
  typed = null;
  const d = state.editable ? drafts.get(draftKey()) : null;
  offer = d?.data && JSON.stringify(d.data) !== base ? d : null;
  if (d && !offer) drafts.drop(draftKey());
  $('me').textContent = meText(state.me);
  $('auth').textContent = state.me ? '退出' : '登录';
  $('auth').href = state.me ? '/logout' : '/login.html';
  $('groupBar').classList.toggle('hide', groupButtons($('group'), state.groups, group, switchGroup) < 2);
  renderBooks();
  render();
  if (firstLoad) {
    firstLoad = false;
    const target = location.hash.startsWith('#step-') || location.hash.startsWith('#lane-') ? document.getElementById(decodeURIComponent(location.hash.slice(1))) : null;
    if (target) { target.scrollIntoView({ block: 'center' }); flash(target); }
  }
}

// The group's outlines as buttons, the shown one pressed; the address keeps ?book= so a link opens the same one.
function renderBooks() {
  const list = state.books.map((b) => ({ book: b.book, label: (b.main ? '★ ' : '') + (b.title || `大纲 ${b.book}`) + (b.lanes ? '' : '（空）') }));
  if (book === 0 && state.member) list.push({ book: 0, label: '新大纲（还没保存）' });
  $('books').replaceChildren(...list.map((b) => h('button', { type: 'button', class: 'chip', 'aria-pressed': String(b.book === book),
    onclick: () => { if (b.book !== book) leave(() => { book = b.book; return b.book ? load() : null; }); } }, b.label)));
  $('newBook').classList.toggle('hide', !state.member || book === 0);
  $('bookBar').classList.toggle('hide', list.length < 2 && !state.member);
  const url = new URL(location.href);
  if (book) url.searchParams.set('book', book); else url.searchParams.delete('book');
  if (url.href !== location.href) history.replaceState(null, '', url);
}

// Switching away keeps unsaved edits as a draft (offered again on coming back). Only where this browser cannot keep
// drafts does a switch with unsaved edits need a second click.
let leaveArmed = false;
function leave(go) {
  if (dirty() && !drafts.ok && !leaveArmed) {
    leaveArmed = true;
    error('当前大纲有改动还没保存（这个浏览器存不了草稿）：先保存，或再点一次放弃改动');
    return;
  }
  if (dirty()) toast('没保存的改动已留作本机草稿，回到这份大纲时可以恢复');
  leaveArmed = false;
  error('');
  problem = null;
  openStep = null;
  const next = go();
  if (next) next.catch((e) => error(e.message));
}
function switchGroup(g) { leave(() => { group = g; book = null; return load(); }); }

$('newBook').addEventListener('click', () => leave(() => {
  book = 0;
  data = structuredClone(EMPTY);
  base = JSON.stringify(EMPTY);
  version = 0;
  state.updated_at = null;
  state.author = null;
  past.length = future.length = 0;
  const d = drafts.get(draftKey());
  offer = d?.data && JSON.stringify(d.data) !== base ? d : null;
  editing = true;
  renderBooks();
  render();
  $('titleInput').focus();
}));
$('titleInput').addEventListener('input', () => edited('titleInput', () => { data.title = $('titleInput').value; }));
$('edit').addEventListener('click', () => { editing = !editing; openStep = null; render(); });
$('addLane').addEventListener('click', addLane);
$('undo').addEventListener('click', undo);
$('redo').addEventListener('click', redo);
$('save').addEventListener('click', save);
$('copy').addEventListener('click', copyOutline);
$('removeBook').addEventListener('click', async () => {
  const btn = $('removeBook');
  if (btn.dataset.armed !== '1') { btn.dataset.armed = '1'; btn.textContent = `再点一次删除「${data.title || `大纲 ${book}`}」`; return; }
  busy = true;
  error('');
  render();
  try {
    await api('/api/outline/remove', { group, book, version });
    drafts.drop(draftKey());
    book = null;
    editing = false;
    await load();
    toast('已删除这份大纲');
  } catch (e) { error('删除失败：' + e.message); } finally { busy = false; btn.dataset.armed = ''; btn.textContent = '删除这份大纲'; render(); }
});
document.addEventListener('keydown', (e) => {
  const mod = e.ctrlKey || e.metaKey;
  if (mod && e.key.toLowerCase() === 's') { e.preventDefault(); save(); return; }
  if (!editing) return;
  const t = e.target;
  if (t instanceof HTMLElement && (t.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName))) return;
  if (mod && !e.shiftKey && e.key.toLowerCase() === 'z') { e.preventDefault(); undo(); }
  else if (mod && (e.key.toLowerCase() === 'y' || (e.shiftKey && e.key.toLowerCase() === 'z'))) { e.preventDefault(); redo(); }
});
window.addEventListener('beforeunload', (e) => { if (dirty() && !drafts.ok) e.preventDefault(); });
load().catch((e) => { error('加载失败：' + e.message); $('board').textContent = '暂时无法加载大纲，请刷新重试。'; });
