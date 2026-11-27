'use strict';
// 设定集 (/design.html): the characters, vehicles, weapons, equipment, maps and monsters a group's story needs, by kind.
// Each entry opens in place: read-only for those who cannot write it, else an editor whose typing is kept as a draft in
// this browser until saved (Ctrl+S). It links to the outline steps it appears in and the 关卡策划 missions that use it;
// those pages list it back. Saves name the version they were made from: a 409 offers 用我的覆盖 / 看对方的版本 / 复制.
// Text goes into text nodes only (ui.js h()).

const $ = (id) => document.getElementById(id);
const KIND = { character: '角色', vehicle: '载具', weapon: '武器', gear: '装备', map: '地图', monster: '怪物', other: '其他' };
const KIND_ICON = { character: '👤', vehicle: '🚙', weapon: '🔫', gear: '🎒', map: '🗺', monster: '👾', other: '📦' };
const STATUS = { todo: '待设计', drafting: '设计中', making: '制作中', done: '已完成' };
// The 关卡策划 status colours, reused: todo ~ planned, drafting ~ writing, making ~ testing.
const TINT = { todo: 'planned', drafting: 'writing', making: 'testing', done: 'done' };
const LABEL = { name: '名称', kind: '分类', status: '状态', side: '阵营 / 归属', look: '外观', body: '设定与玩法', refs: '参考', steps: '出现在大纲哪几步', missions: '用到它的关卡' };
const HINT = {
  look: '长什么样：体型、配色、材质、标志性的部件……',
  body: '它是什么、做什么：背景、能力、玩法、数值方向……',
  refs: '参考：原版相近的模型 / 武器、外部模型来源、图片链接……',
};
const TEXTS = ['look', 'body', 'refs'];

const params = new URLSearchParams(location.search);
let group = params.get('group');
let state = null;          // GET /api/design
let books = [];            // the group's outlines: [{book, title, lanes}]
let stepOf = new Map();    // `${book}/${step}` → {book, bookTitle, lane, step, n}
let missions = [];         // the group's 关卡策划 missions, in order: [{id, title, n}]
const shown = { text: '', kind: '', status: '' };
let edit = null;           // the open entry being edited: {id, version, base, cur, problem, el, refs}
let viewing = null;        // an entry opened read-only
let busy = false;

const fieldsOf = (r) => ({ name: r?.name ?? '', kind: r?.kind ?? 'character', status: r?.status ?? 'todo', side: r?.side ?? '',
  look: r?.look ?? '', body: r?.body ?? '', refs: r?.refs ?? '', book: r?.book ?? null, steps: [...(r?.steps ?? [])], missions: [...(r?.missions ?? [])] });
const same = (a, b) => JSON.stringify(fieldsOf(a)) === JSON.stringify(fieldsOf(b));
const draftKey = (id) => `edf6.design.${group ?? ''}.${id ?? 'new'}`;
const rowOf = (id) => state.rows.find((r) => r.id === id);

function error(text) {
  $('error').textContent = text;
  $('error').classList.toggle('hide', !text);
}

// ---- loading ----

async function load() {
  state = await api('/api/design' + (group == null ? '' : '?group=' + encodeURIComponent(group)));
  group = state.group;
  $('me').textContent = meText(state.me);
  $('auth').textContent = state.me ? '退出' : '登录';
  $('auth').href = state.me ? '/logout' : '/login.html';
  $('add').classList.toggle('hide', !state.editable);
  $('hint').classList.toggle('hide', !state.editable);
  $('who').textContent = state.editable && !state.member ? '这个组的大纲对所有账号开放，你也能新增和编辑（删除限本组成员）' : '同组成员都能编辑';
  $('groupBar').classList.toggle('hide', groupButtons($('group'), state.groups, group, switchGroup) < 2);
  if (edit && edit.id != null && !rowOf(edit.id)) {
    toast(`「${edit.cur.name || edit.base.name}」已被别人删除；你的改动还留在本机草稿里`, null, 'warn');
    edit = null;
  }
}

// The group's outlines (every step, for linking) and its missions.
async function loadLinks() {
  books = [];
  stepOf = new Map();
  missions = [];
  if (group == null) return;
  const q = '/api/outline?group=' + encodeURIComponent(group);
  const [first, story] = await Promise.all([api(q), api('/api/story?group=' + encodeURIComponent(group)).catch(() => null)]);
  const list = await Promise.all(first.books.filter((b) => b.lanes > 0).map((b) => (b.book === first.book ? first : api(`${q}&book=${b.book}`))));
  for (const o of list) {
    if (!o.outline) continue;
    const book = { book: o.book, title: o.outline.title || `大纲 ${o.book}`, lanes: o.outline.lanes };
    books.push(book);
    let n = 0;
    for (const lane of book.lanes) for (const step of lane.steps) stepOf.set(`${o.book}/${step.id}`, { book: o.book, bookTitle: book.title, lane, step, n: ++n });
  }
  if (story && story.group === group) missions = story.rows.map((r, i) => ({ id: r.id, title: r.title, n: i + 1 }));
}

async function switchGroup(g) {
  if (busy) return;
  group = g;
  edit = null;
  viewing = null;
  try { await load(); await loadLinks().catch(() => {}); render(); } catch (e) { error('加载失败：' + e.message); }
}

// ---- the list ----

function stepLabel(book, id) {
  const s = stepOf.get(`${book}/${id}`);
  return s ? `${s.lane.name} · ${s.step.title}` : `（大纲里已没有 ${id}）`;
}
function missionLabel(id) {
  const m = missions.find((x) => x.id === id);
  return m ? `第 ${m.n} 关 ${m.title}` : `（已删除的关卡 ${id}）`;
}

function matches(r) {
  if (shown.kind && r.kind !== shown.kind) return false;
  if (shown.status && r.status !== shown.status) return false;
  const t = shown.text.trim().toLowerCase();
  if (!t) return true;
  return [r.name, r.side, r.look, r.body, r.refs, KIND[r.kind], ...r.steps.map((s) => stepLabel(r.book, s))].join('\n').toLowerCase().includes(t);
}

function render() {
  keepFocus(() => {
    renderChips();
    const rows = state.rows.filter(matches);
    const all = state.rows.length;
    $('count').textContent = all ? (rows.length === all ? `${all} 条` : `显示 ${rows.length} / ${all} 条`) : '';
    $('sides').replaceChildren(...[...new Set(state.rows.map((r) => r.side).filter(Boolean))].map((s) => h('option', { value: s })));
    const kinds = shown.kind ? [shown.kind] : Object.keys(KIND);
    const sections = kinds.map((k) => {
      const mine = rows.filter((r) => r.kind === k);
      const adding = edit && edit.id == null && edit.cur.kind === k;
      if (!mine.length && !adding && (shown.text || shown.status || !shown.kind)) return null;
      return h('section', { class: 'dsKind', id: 'kind-' + k },
        h('div', { class: 'row dsKindHead' }, h('h3', {}, KIND_ICON[k], ' ', KIND[k]), h('span', { class: 'muted' }, mine.length),
          h('span', { class: 'grow' }),
          state.editable ? h('button', { type: 'button', class: 'btn ghost small', onclick: () => openNew({ kind: k }) }, '＋ ' + KIND[k]) : null),
        h('ul', { class: 'stList dsList' }, adding ? card(null) : null, mine.map(card)));
    }).filter(Boolean);
    if (!sections.length) {
      $('list').replaceChildren(h('p', { class: 'muted' }, all ? '没有符合条件的设定。' : state.editable ? '还没有设定：点「＋ 新增设定」写第一条。' : '这个组还没有设定。'));
    } else $('list').replaceChildren(...sections);
  });
}

function renderChips() {
  const byKind = Object.fromEntries(Object.keys(KIND).map((k) => [k, 0]));
  const byStatus = Object.fromEntries(Object.keys(STATUS).map((s) => [s, 0]));
  for (const r of state.rows) { byKind[r.kind] += 1; byStatus[r.status] += 1; }
  const chip = (key, value, text, n, cls = '') => h('button', { type: 'button', class: 'chip stat ' + cls, 'aria-pressed': String(shown[key] === value),
    onclick: () => { shown[key] = shown[key] === value ? '' : value; render(); } }, text, h('b', {}, n));
  $('kinds').replaceChildren(chip('kind', '', '全部', state.rows.length), ...Object.entries(KIND).map(([k, t]) => chip('kind', k, `${KIND_ICON[k]} ${t}`, byKind[k])));
  $('stats').replaceChildren(...Object.entries(STATUS).map(([s, t]) => chip('status', s, t, byStatus[s], 'st-' + TINT[s])));
  $('stats').classList.toggle('hide', !state.rows.length);
}

function card(r) {
  const isNew = r == null;
  const id = r?.id ?? null;
  const open = isNew || edit?.id === id || viewing === id;
  const d = !isNew && state.editable ? drafts.get(draftKey(id)) : null;
  const dirty = d && (d.version !== r.version || !same(d.cur, r));
  const status = isNew ? edit.cur.status : r.status;
  const li = h('li', { class: 'stCard dsCard' + (open ? ' open' : ''), id: isNew ? 'd-new' : `d-${id}`, 'data-status': TINT[status] });
  const steps = isNew ? [] : r.steps;
  li.append(h('div', { class: 'stHead' },
    h('button', { type: 'button', class: 'stToggle', 'aria-expanded': String(open), onclick: () => (open ? closeEditor() : openEntry(id, { focus: true })) },
      h('span', { class: 'stTitle' }, isNew ? (edit.cur.name || `新${KIND[edit.cur.kind]}（未保存）`) : r.name),
      h('span', { class: 'stMeta' },
        h('span', { class: 'badge st-' + TINT[status] }, isNew ? '未保存' : STATUS[status]),
        !isNew && r.side ? h('span', {}, r.side) : null,
        steps.length ? h('span', { title: steps.map((s) => stepLabel(r.book, s)).join('\n') }, `大纲 ${steps.length} 步`) : null,
        !isNew && r.missions.length ? h('span', {}, `${r.missions.length} 关`) : null,
        dirty ? h('span', { class: 'tag edit' }, '草稿') : null))));
  if (!open && r && (r.body || r.look)) li.append(h('p', { class: 'stSum dsSum' }, r.body || r.look));
  if (open) li.append(isNew || edit?.id === id ? edit.el : reader(r));
  return li;
}

// Read-only: what those who cannot write the entry see when they open it.
function reader(r) {
  const part = (k, text) => (text ? h('div', { class: 'stPart' }, h('h4', {}, LABEL[k]), h('p', {}, text)) : null);
  const stepLinks = r.steps.map((s) => h('li', {}, h('a', { href: `/outline.html?group=${encodeURIComponent(group ?? '')}&book=${r.book}#step-${s}` }, stepLabel(r.book, s))));
  const missionLinks = r.missions.map((m) => h('li', {}, h('a', { href: `/story.html?group=${encodeURIComponent(group ?? '')}&id=${m}` }, missionLabel(m))));
  return h('div', { class: 'stEditor stRead' },
    part('side', r.side), part('look', r.look), part('body', r.body), part('refs', r.refs),
    stepLinks.length ? h('div', { class: 'stPart' }, h('h4', {}, LABEL.steps), h('ul', { class: 'dsLinks' }, stepLinks)) : null,
    missionLinks.length ? h('div', { class: 'stPart' }, h('h4', {}, LABEL.missions), h('ul', { class: 'dsLinks' }, missionLinks)) : null,
    r.updated_by ? h('p', { class: 'muted small' }, `最后由 ${r.updated_by} 于 ${ago(r.updated_at)} 修改`) : null);
}

// ---- editing ----

function openEntry(id, { focus = false } = {}) {
  if (edit && edit.id === id) return;
  closeEditor(false);
  const r = rowOf(id);
  if (!r) return;
  if (!state.editable) { viewing = id; render(); setUrlId(id); return; }
  const d = drafts.get(draftKey(id));
  const restored = d && (d.version !== r.version || !same(d.cur, r)) ? d : null;
  edit = { id, version: restored ? d.version : r.version, base: fieldsOf(r), cur: fieldsOf(restored ? d.cur : r), problem: null };
  if (restored) edit.problem = d.version !== r.version
    ? { kind: 'stale', text: `恢复了你没保存的草稿；但这条设定在那之后被 ${r.updated_by || '别人'} 改过了。` }
    : { kind: 'restored', text: `恢复了你 ${ago(d.at)} 没保存的草稿。` };
  build(focus);
  render();
  setUrlId(id);
}

function openNew({ kind = 'character', book = null, step = null, mission = null } = {}) {
  closeEditor(false);
  const d = drafts.get(draftKey(null));
  const cur = d?.cur ? fieldsOf(d.cur) : fieldsOf({ kind, book: book ?? books[0]?.book ?? null });
  if (!d?.cur) {
    cur.kind = kind;
    if (book && step) { cur.book = book; cur.steps = [step]; }
    if (mission) cur.missions = [mission];
  }
  edit = { id: null, version: null, base: fieldsOf(null), cur, problem: d?.cur ? { kind: 'restored', text: `恢复了你 ${ago(d.at)} 没写完的新设定。` } : null };
  build(true);
  shown.kind = shown.kind && shown.kind !== cur.kind ? '' : shown.kind;
  render();
  $('d-new')?.scrollIntoView({ block: 'nearest' });
  $('f-name')?.focus();
}

function closeEditor(rerender = true) {
  edit = null;
  viewing = null;
  setUrlId(null);
  if (rerender) render();
}

function setUrlId(id) {
  const url = new URL(location.href);
  for (const k of ['new', 'kind', 'book', 'step', 'mission']) url.searchParams.delete(k);
  if (id == null) url.searchParams.delete('id'); else url.searchParams.set('id', id);
  if (url.href !== location.href) history.replaceState(null, '', url);
}

// Every keystroke: keep the draft (dropped again once it matches what is saved).
function stash() {
  const e = edit;
  if (!e) return;
  if (e.id != null && same(e.cur, e.base) && e.version === rowOf(e.id)?.version) drafts.drop(draftKey(e.id));
  else drafts.set(draftKey(e.id), { version: e.version, cur: e.cur });
}

function build(focus) {
  const e = edit;
  const isNew = e.id == null;
  const L = state.limit;
  const field = (k, input, wide = false) => h('div', { class: 'stField' + (wide ? ' dsWide' : '') }, h('label', { for: 'f-' + k }, LABEL[k]), input);
  const set = (k, v) => { e.cur[k] = v; stash(); refresh(); };
  const name = h('input', { type: 'text', id: 'f-name', maxlength: L.name, required: true, placeholder: '比如「刻印步枪」', autocomplete: 'off' });
  name.value = e.cur.name;
  name.addEventListener('input', () => {
    set('name', name.value);
    const t = e.el.closest('.stCard')?.querySelector('.stTitle');
    if (t) t.textContent = name.value || (isNew ? `新${KIND[e.cur.kind]}（未保存）` : '');
  });
  const kind = h('select', { id: 'f-kind' }, Object.entries(KIND).map(([k, t]) => h('option', { value: k }, `${KIND_ICON[k]} ${t}`)));
  kind.value = e.cur.kind;
  kind.addEventListener('change', () => { set('kind', kind.value); render(); });
  const side = h('input', { type: 'text', id: 'f-side', maxlength: L.side, list: 'sides', placeholder: 'EDF、以太联军、星导者……', autocomplete: 'off' });
  side.value = e.cur.side;
  side.addEventListener('input', () => set('side', side.value));
  // Status: four buttons, one pressed (a radio group the arrow keys walk).
  const status = h('div', { class: 'seg', role: 'radiogroup', 'aria-label': LABEL.status });
  const paint = () => {
    for (const b of status.children) {
      const on = b.dataset.v === e.cur.status;
      b.setAttribute('aria-checked', String(on));
      b.tabIndex = on ? 0 : -1;
    }
    e.el?.closest('.stCard')?.setAttribute('data-status', TINT[e.cur.status]);
  };
  for (const [v, t] of Object.entries(STATUS)) {
    status.append(h('button', { type: 'button', role: 'radio', class: 'st-' + TINT[v], 'data-v': v, onclick: () => { set('status', v); paint(); },
      onkeydown: (ev) => {
        const keys = Object.keys(STATUS);
        const d = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[ev.key];
        if (!d) return;
        ev.preventDefault();
        const next = keys[(keys.indexOf(e.cur.status) + d + keys.length) % keys.length];
        set('status', next);
        paint();
        status.querySelector(`[data-v="${next}"]`).focus();
      } }, t));
  }
  const area = (k, rows) => {
    const t = h('textarea', { id: 'f-' + k, maxlength: L[k], rows, placeholder: HINT[k] });
    t.value = e.cur[k];
    t.addEventListener('input', () => { set(k, t.value); grow(t); });
    return t;
  };
  const texts = Object.fromEntries(TEXTS.map((k) => [k, area(k, k === 'body' ? 6 : 3)]));
  // Outline steps: one outline, any of its steps (checkboxes by world line).
  const stepBox = h('div', { class: 'dsPick', id: 'f-steps' });
  const bookSel = h('select', { id: 'f-book', 'aria-label': '哪份大纲' }, books.length ? null : h('option', { value: '' }, '这个组还没有大纲'),
    books.map((b) => h('option', { value: b.book }, b.title)));
  if (e.cur.book != null && !books.some((b) => b.book === e.cur.book)) bookSel.append(h('option', { value: e.cur.book }, `大纲 ${e.cur.book}（已不在）`));
  bookSel.value = e.cur.book ?? books[0]?.book ?? '';
  const paintSteps = () => {
    const book = books.find((b) => b.book === Number(bookSel.value));
    const lost = e.cur.steps.filter((s) => !stepOf.has(`${e.cur.book}/${s}`));
    stepBox.replaceChildren(...(book ? book.lanes : []).filter((l) => l.steps.length).map((l) => h('fieldset', { class: 'dsLane' }, h('legend', {}, l.name),
      l.steps.map((s) => {
        const box = h('input', { type: 'checkbox', value: s.id });
        box.checked = e.cur.book === book.book && e.cur.steps.includes(s.id);
        box.addEventListener('change', () => {
          e.cur.book = book.book;
          e.cur.steps = box.checked ? [...e.cur.steps, s.id] : e.cur.steps.filter((x) => x !== s.id);
          stash();
          refresh();
          count();
        });
        return h('label', { class: 'dsCheck' }, box, ` ${stepOf.get(`${book.book}/${s.id}`)?.n}. ${s.title}`);
      }))), lost.length ? h('p', { class: 'muted small' }, `另有 ${lost.length} 步已不在大纲里（保存时保留）`) : null);
  };
  bookSel.addEventListener('change', () => {
    if (e.cur.steps.length && Number(bookSel.value) !== e.cur.book) { e.cur.steps = []; stash(); refresh(); }
    e.cur.book = Number(bookSel.value) || null;
    paintSteps();
    count();
  });
  paintSteps();
  const stepsSum = h('summary', {});
  const missionSum = h('summary', {});
  const missionBox = h('div', { class: 'dsPick', id: 'f-missions' }, missions.length ? missions.map((m) => {
    const box = h('input', { type: 'checkbox', value: m.id });
    box.checked = e.cur.missions.includes(m.id);
    box.addEventListener('change', () => {
      e.cur.missions = box.checked ? [...e.cur.missions, m.id] : e.cur.missions.filter((x) => x !== m.id);
      stash();
      refresh();
      count();
    });
    return h('label', { class: 'dsCheck' }, box, ` 第 ${m.n} 关 ${m.title}`);
  }) : h('p', { class: 'muted small' }, '这个组在关卡策划里还没有关卡。'));
  const count = () => {
    stepsSum.textContent = `${LABEL.steps}（已选 ${e.cur.steps.length}）`;
    missionSum.textContent = `${LABEL.missions}（已选 ${e.cur.missions.length}）`;
  };
  count();
  const problem = h('div', { class: 'stProblem hide', role: 'alert' });
  const state_ = h('span', { class: 'stState muted', id: 'f-state', role: 'status' });
  const saveBtn = h('button', { type: 'button', class: 'btn', id: 'f-save', onclick: () => save() }, '保存');
  const del = !isNew && state.member ? h('button', { type: 'button', class: 'btn ghost small danger', onclick: () => remove(e.id) }, '删除') : null;
  e.refs = { problem, state: state_, save: saveBtn };
  e.el = h('div', { class: 'stEditor' }, problem,
    h('div', { class: 'stGrid' },
      h('div', {}, field('name', name), field('look', texts.look), field('body', texts.body), field('refs', texts.refs)),
      h('div', {}, field('kind', kind), h('div', { class: 'stField' }, h('span', { class: 'lbl' }, LABEL.status), status), field('side', side),
        h('div', { class: 'stField' }, h('details', { open: e.cur.steps.length > 0 || isNew ? true : null }, stepsSum, bookSel, stepBox)),
        h('div', { class: 'stField' }, h('details', { open: e.cur.missions.length > 0 ? true : null }, missionSum, missionBox)))),
    h('div', { class: 'stBar' }, saveBtn, h('button', { type: 'button', class: 'btn ghost', onclick: () => (isNew ? discardNew() : closeEditor()) }, isNew ? '放弃' : '收起'),
      state_, h('span', { class: 'grow' }), del));
  paint();
  showProblem();
  refresh();
  requestAnimationFrame(() => { for (const t of Object.values(texts)) grow(t); if (focus) name.focus({ preventScroll: true }); });
}

function refresh() {
  const e = edit;
  if (!e?.refs) return;
  const dirty = e.id == null || !same(e.cur, e.base);
  e.refs.save.disabled = busy || !dirty;
  e.refs.state.textContent = busy ? '保存中…' : !dirty ? '已保存' : drafts.ok ? '有改动还没保存 · 草稿已存在本机 · Ctrl+S 保存' : '有改动还没保存 · Ctrl+S 保存';
  e.refs.state.classList.toggle('bad', dirty && !busy);
}

function showProblem() {
  const box = edit?.refs?.problem;
  if (!box) return;
  const p = edit.problem;
  box.className = 'stProblem' + (p ? ' ' + p.kind : ' hide');
  if (!p) { box.replaceChildren(); return; }
  const btn = (text, fn, main = false) => h('button', { type: 'button', class: main ? 'btn small' : 'btn ghost small', onclick: fn }, text);
  const copy = async () => toast((await copyText(asText(edit.cur))) ? '已复制你的内容，可以粘贴到别处' : '复制失败，请手动选中复制');
  const parts = [h('span', {}, p.text)];
  if (p.kind === 'restored') parts.push(btn('撤回到已保存的版本', discardMine));
  if (p.kind === 'stale' || p.kind === 'conflict') parts.push(h('span', {}, ' 怎么处理？'), btn('用我的覆盖', overwrite, true), btn('看对方的版本（丢掉我的）', discardMine), btn('复制我的内容', copy));
  if (p.kind === 'gone') parts.push(btn('另存为新设定', () => { drafts.drop(draftKey(edit.id)); edit.id = null; edit.version = null; edit.problem = null; save(); }, true), btn('复制我的内容', copy));
  if (p.kind === 'error') parts.push(btn('重试', () => save(), true));
  parts.push(h('button', { type: 'button', class: 'toastX', 'aria-label': '关闭', onclick: () => { edit.problem = null; showProblem(); } }, '×'));
  box.replaceChildren(...parts);
}

function asText(c) {
  return [`【${KIND[c.kind]}】${c.name}${c.side ? `（${c.side}）` : ''}`, c.look && `外观：${c.look}`, c.body && `设定：${c.body}`, c.refs && `参考：${c.refs}`]
    .filter(Boolean).join('\n\n');
}

async function save() {
  if (!edit || busy) return;
  const e = edit;
  if (!e.cur.name.trim()) {
    e.problem = { kind: 'error', text: '名称不能为空。' };
    showProblem();
    $('f-name')?.focus();
    return;
  }
  busy = true;
  refresh();
  try {
    const out = await api('/api/design/save', { group, id: e.id, version: e.version, ...e.cur });
    drafts.drop(draftKey(e.id));
    const wasNew = e.id == null;
    e.id = out.id;
    e.version = out.version;
    e.base = fieldsOf(e.cur);
    e.problem = null;
    busy = false;
    await load();
    if (edit === e) { build(false); render(); setUrlId(e.id); }
    toast(wasNew ? `已新增${KIND[e.cur.kind]}「${e.cur.name}」` : `已保存「${e.cur.name}」`);
  } catch (err) {
    busy = false;
    if (edit !== e) return;
    e.problem = err.status === 409 && e.id != null ? { kind: 'conflict', text: err.message + '。' }
      : err.status === 404 && e.id != null ? { kind: 'gone', text: '这条设定已经被别人删除了，你的改动还在。' }
        : { kind: 'error', text: '保存失败：' + err.message };
    showProblem();
    refresh();
  }
}

// Mine wins: take the newest version number and save again.
async function overwrite() {
  const e = edit;
  await load();
  const r = rowOf(e.id);
  if (!r) { e.problem = { kind: 'gone', text: '这条设定已经被别人删除了，你的改动还在。' }; showProblem(); return; }
  e.version = r.version;
  e.base = fieldsOf(r);
  e.problem = null;
  await save();
}

async function discardMine() {
  const e = edit;
  drafts.drop(draftKey(e.id));
  await load();
  const r = rowOf(e.id);
  if (!r) { closeEditor(); return; }
  edit = { id: r.id, version: r.version, base: fieldsOf(r), cur: fieldsOf(r), problem: null };
  build(false);
  render();
}

function discardNew() {
  drafts.drop(draftKey(null));
  closeEditor();
}

async function remove(id) {
  const r = rowOf(id);
  if (!r || busy) return;
  busy = true;
  try {
    await api('/api/design/remove', { group, id });
    drafts.drop(draftKey(id));
    if (edit?.id === id) edit = null;
    await load();
    render();
    const again = fieldsOf(r);
    toast(`已删除「${r.name}」`, { label: '撤销', fn: async () => {
      try { await api('/api/design/save', { group, ...again }); await load(); render(); toast(`已恢复「${r.name}」`); } catch (err) { error('恢复失败：' + err.message); }
    } });
  } catch (err) {
    error('删除失败：' + err.message);
  } finally {
    busy = false;
  }
}

// ---- page ----

$('add').addEventListener('click', () => openNew({ kind: shown.kind || 'character' }));
$('filter').addEventListener('input', () => { shown.text = $('filter').value; render(); });
document.addEventListener('keydown', (e) => {
  const mod = e.ctrlKey || e.metaKey;
  if (mod && e.key.toLowerCase() === 's') { e.preventDefault(); if (edit) save(); return; }
  if (e.key === 'Escape' && (edit || viewing != null)) {
    if (e.target === $('filter')) return;
    e.preventDefault();
    const id = edit?.id ?? viewing;
    closeEditor();
    $(`d-${id}`)?.querySelector('.stToggle')?.focus();
    return;
  }
  if (typing(e)) return;
  if (e.key === '/' && !mod) { e.preventDefault(); $('filter').focus(); }
});
window.addEventListener('beforeunload', (e) => {
  if (edit && !drafts.ok && (edit.id == null || !same(edit.cur, edit.base))) e.preventDefault();
});

async function init() {
  await load();
  try { await loadLinks(); } catch (e) { error('大纲 / 关卡加载失败（设定照常可写）：' + e.message); }
  render();
  // Deep links: ?id= opens an entry; ?new=1&kind=&book=&step=&mission= starts one (from the outline or a mission).
  if (params.get('new') && state.editable) {
    const book = Number(params.get('book')) || null;
    openNew({ kind: KIND[params.get('kind')] ? params.get('kind') : 'character', book, step: book ? params.get('step') : null, mission: Number(params.get('mission')) || null });
  } else if (params.get('id')) {
    const id = Number(params.get('id'));
    if (rowOf(id)) openEntry(id);
    $(`d-${id}`)?.scrollIntoView({ block: 'start' });
  }
}
init().catch((e) => { error('加载失败：' + e.message); $('list').textContent = '暂时无法加载设定，请刷新重试。'; });
