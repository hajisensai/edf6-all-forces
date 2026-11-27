'use strict';
// 关卡策划: the group's missions in campaign order. Each one is what the player plays (goal, enemies, story, how it
// hands over to the next), the stock map it borrows, and the outline step it plays out (/outline.html says what
// happens; this page says how each mission plays it). Anyone reads; a group's members edit in place.
// Typing goes into a draft in this browser at once, so a closed tab loses nothing; 保存 / Ctrl+S sends it with the
// version it started from, and a mission someone saved meanwhile is never overwritten without asking.
// Moves, deletes and outline re-links by drag can be undone (Ctrl+Z, or 撤销 on the notice).

const $ = (id) => document.getElementById(id);
const STATUS = { planned: '构思中', writing: '制作中', testing: '待测试', done: '已完成' };
const TEXTS = ['goal', 'enemies', 'body', 'handoff'];
const LABEL = { title: '关卡名', goal: '作战目标', enemies: '敌人', body: '剧情与演出', handoff: '衔接下一关', status: '状态', link: '对应大纲步骤', mission: '借用地图' };
const HINT = {
  goal: '胜负条件：「守住基地 5 分钟」「击破全部运输船」',
  enemies: '哪些敌人、几波、从哪里来',
  body: '这一关发生什么：开场、无线电对话、中途事件、演出',
  handoff: '打完怎样接到下一关',
};
const params = new URLSearchParams(location.search);
let group = params.get('group');
let state = null;            // GET /api/story
let catalog = [];            // stock missions (/api/missions)
let byKey = new Map();
let books = [];              // the group's outlines: [{book, title, lanes: [{id, name, tone, steps: [{id, title, tag}]}]}]
let stepOf = new Map();      // `${book}/${step}` → {book, bookTitle, lane, step, n}
let shown = { text: '', status: '' };
let view = (() => { try { return localStorage.getItem('edf6.story.view') === 'lane' ? 'lane' : 'order'; } catch { return 'order'; } })();
let edit = null;             // the open mission: {id, version, after, base, cur, problem, el}
let viewing = null;          // a mission opened read-only (not editable, or a visitor)
let busy = false;
let dragId = null;
const undos = [];
const redos = [];

const fieldsOf = (r) => ({ title: r?.title ?? '', status: r?.status ?? 'planned', book: r?.book ?? null, step: r?.step ?? null,
  mission: r?.mission ?? null, goal: r?.goal ?? '', enemies: r?.enemies ?? '', body: r?.body ?? '', handoff: r?.handoff ?? '' });
const same = (a, b) => Object.keys(fieldsOf(null)).every((k) => String(a[k] ?? '') === String(b[k] ?? ''));
const draftKey = (id) => `edf6.story.${group ?? ''}.${id ?? 'new'}`;
const rowOf = (id) => state.rows.find((r) => r.id === id);
const indexOf = (id) => state.rows.findIndex((r) => r.id === id);

function error(text) {
  $('error').textContent = text;
  $('error').classList.toggle('hide', !text);
}

function mapLabel(key) {
  const m = byKey.get(key);
  return m ? `${m.series} ${m.key.split('/').slice(1).join('/')} ${m.sc}` : key;
}

function stepInfo(book, step) {
  if (book == null) return null;
  return stepOf.get(`${book}/${step}`) || { missing: true, book, step };
}

function stepText(info, short = false) {
  if (!info) return '';
  if (info.missing) return `大纲 ${info.book} 里已没有这一步`;
  const where = books.length > 1 && !short ? `${info.bookTitle} · ` : '';
  return `${where}${info.lane.name} · ${info.step.title}`;
}

// ---- loading ----

async function load() {
  state = await api('/api/story' + (group == null ? '' : '?group=' + encodeURIComponent(group)));
  group = state.group;
  $('me').textContent = meText(state.me);
  $('auth').textContent = state.me ? '退出' : '登录';
  $('auth').href = state.me ? '/logout' : '/login.html';
  $('add').classList.toggle('hide', !state.editable);
  $('hint').classList.toggle('hide', !state.editable);
  $('groupBar').classList.toggle('hide', groupButtons($('group'), state.groups, group, (g) => switchGroup(g)) < 2);
  if (edit && edit.id != null && !rowOf(edit.id)) {
    toast(`「${edit.cur.title || edit.base.title}」已被别人删除；你的改动还留在本机草稿里`, null, 'warn');
    edit = null;
  }
  render();
}

async function loadOutlines() {
  books = [];
  stepOf = new Map();
  if (group == null) return;
  const q = '/api/outline?group=' + encodeURIComponent(group);
  const first = await api(q);
  const list = await Promise.all(first.books.filter((b) => b.lanes > 0).map(async (b) =>
    b.book === first.book ? first : api(`${q}&book=${b.book}`)));
  for (const o of list) {
    if (!o.outline) continue;
    const book = { book: o.book, title: o.outline.title || `大纲 ${o.book}`, lanes: o.outline.lanes };
    books.push(book);
    let n = 0;
    for (const lane of book.lanes) for (const step of lane.steps) stepOf.set(`${o.book}/${step.id}`, { book: o.book, bookTitle: book.title, lane, step, n: ++n });
  }
}

async function switchGroup(g) {
  if (busy) return;
  group = g;
  edit = null;
  viewing = null;
  undos.length = redos.length = 0;
  try { await Promise.all([load(), loadOutlines().catch(() => {})]); render(); } catch (e) { error('加载失败：' + e.message); }
}

// ---- the list ----

function matches(r) {
  if (shown.status && r.status !== shown.status) return false;
  if (!shown.text) return true;
  const hay = [r.title, r.goal, r.enemies, r.body, r.handoff, r.mission && mapLabel(r.mission), stepText(stepInfo(r.book, r.step))].join('\n').toLowerCase();
  return shown.text.split(/\s+/).every((w) => hay.includes(w));
}

function render() {
  if (!state) return;
  keepFocus(() => {
    renderStats();
    renderDrafts();
    const rows = state.rows.filter(matches);
    const all = state.rows.length;
    $('count').textContent = all ? `共 ${all} 关` + (rows.length !== all ? `，筛出 ${rows.length} 关` : '') : '';
    for (const b of $('views').children) b.setAttribute('aria-pressed', String(b.dataset.view === view));
    const list = view === 'lane' && books.length ? laneView(rows) : orderView(rows);
    if (!all && !edit) {
      list.replaceChildren(h('div', { class: 'stEmpty' },
        h('p', {}, state.group == null ? '还没有哪个组写了关卡。'
          : state.editable ? '还没有关卡。从第一关开始写：' : '这个组还没有关卡。'),
        state.editable ? h('button', { type: 'button', class: 'btn', onclick: () => openNew({}) }, '＋ 写第一关') : null));
    } else if (!rows.length && !edit) {
      list.replaceChildren(h('p', { class: 'stEmpty muted' }, '没有符合条件的关卡。', h('button', { type: 'button', class: 'btn ghost small', onclick: clearFilter }, '清除筛选')));
    }
    $('story').replaceChildren(list);
  });
}

function renderStats() {
  const counts = Object.fromEntries(Object.keys(STATUS).map((s) => [s, 0]));
  for (const r of state.rows) counts[r.status] = (counts[r.status] || 0) + 1;
  const all = state.rows.length;
  const chip = (value, text, n) => h('button', { type: 'button', class: 'chip stat' + (value ? ' st-' + value : ''), 'aria-pressed': String(shown.status === value),
    onclick: () => { shown.status = shown.status === value ? '' : value; render(); } }, text, h('b', {}, n));
  $('stats').replaceChildren(chip('', '全部', all), ...Object.entries(STATUS).map(([s, t]) => chip(s, t, counts[s])));
  $('stats').classList.toggle('hide', !all);
  // One bar, a segment per status (left to right: done → planned), so how far along the campaign is reads at a glance.
  const bar = $('bar');
  bar.replaceChildren(...['done', 'testing', 'writing', 'planned'].filter((s) => counts[s]).map((s) => {
    const seg = h('div', { class: 'st-' + s, title: `${STATUS[s]} ${counts[s]} 关` });
    seg.style.width = `${(100 * counts[s]) / all}%`;
    return seg;
  }));
  bar.parentElement.classList.toggle('hide', !all);
  bar.parentElement.setAttribute('aria-label', all ? `完成 ${counts.done} / ${all} 关` : '');
}

// Drafts not yet saved: listed at the top so they are found again (their missions also carry a 草稿 tag).
function renderDrafts() {
  const items = [];
  for (const key of drafts.keys(`edf6.story.${group ?? ''}.`)) {
    const d = drafts.get(key);
    if (!d?.cur) continue;
    const idText = key.slice(`edf6.story.${group ?? ''}.`.length);
    const id = idText === 'new' ? null : Number(idText);
    if (edit && (edit.id ?? null) === id) continue;
    const row = id == null ? null : rowOf(id);
    if (row && same(d.cur, fieldsOf(row)) && d.version === row.version) { drafts.drop(key); continue; }
    const title = d.cur.title || row?.title || '（未命名）';
    const open = () => (id == null || !row ? openNew({ restore: key }) : openMission(id, { focus: true }));
    items.push(h('li', {},
      h('button', { type: 'button', class: 'linkish', onclick: open },
        id == null ? `新关卡「${title}」` : row ? `第 ${indexOf(id) + 1} 关「${title}」` : `「${title}」（这一关已被删除，可另存为新关卡）`),
      h('span', { class: 'muted' }, ` · ${ago(d.at)}`),
      h('button', { type: 'button', class: 'btn ghost small', onclick: () => { drafts.drop(key); render(); toast(`已丢弃「${title}」的草稿`); } }, '丢弃')));
  }
  $('draftBar').replaceChildren(...(items.length && state.editable
    ? [h('strong', {}, `本机有 ${items.length} 份没保存的改动：`), h('ul', {}, items)] : []));
  $('draftBar').classList.toggle('hide', !items.length || !state.editable);
}

function orderView(rows) {
  const list = h('ol', { class: 'stList', 'aria-label': '关卡顺序' });
  for (const r of rows) {
    list.append(card(r));
    if (edit && edit.id == null && edit.after === r.id) list.append(card(null));
  }
  if (edit && edit.id == null && (edit.after == null || !rows.some((r) => r.id === edit.after))) list.append(card(null));
  return list;
}

// By world line: outline → lane → step, each step with its missions (still numbered in campaign order) and room for
// more; steps with no mission yet show as gaps to fill. Dropping a mission on a step links it there.
function laneView(rows) {
  const out = h('div', { class: 'stLanes' });
  const filtering = !!(shown.text || shown.status);
  const pending = edit && edit.id == null ? edit : null;
  const placed = new Set();
  for (const book of books) {
    if (books.length > 1) out.append(h('h2', { class: 'stBook' }, book.title));
    for (const lane of book.lanes) {
      const box = h('section', { class: 'stLane', 'data-tone': lane.tone || 'edf', 'aria-label': lane.name }, h('h3', { class: 'stLaneName' }, lane.name));
      let any = false;
      for (const step of lane.steps) {
        const mine = rows.filter((r) => r.book === book.book && r.step === step.id);
        const draftHere = pending && pending.cur.book === book.book && pending.cur.step === step.id;
        if (filtering && !mine.length && !draftHere) continue;
        any = true;
        mine.forEach((r) => placed.add(r.id));
        box.append(stepZone(book.book, step, mine, draftHere));
      }
      if (any || !filtering) out.append(box);
    }
  }
  const rest = rows.filter((r) => !placed.has(r.id));
  const missing = rest.filter((r) => r.book != null);
  const loose = rest.filter((r) => r.book == null);
  if (missing.length) out.append(h('section', { class: 'stLane', 'data-tone': 'hazard' }, h('h3', { class: 'stLaneName' }, '大纲里已经没有的步骤'),
    h('p', { class: 'muted' }, '这些关卡关联的步骤已从大纲删掉或换了编号，打开关卡重新选一个步骤。'), h('ol', { class: 'stList' }, missing.map((r) => card(r)))));
  const pendingLoose = pending && pending.cur.book == null;
  if (loose.length || pendingLoose || !filtering) {
    out.append(h('section', { class: 'stLane', 'data-tone': 'none' }, h('h3', { class: 'stLaneName' }, '未关联大纲'),
      stepZone(null, null, loose, pendingLoose)));
  }
  return out;
}

function stepZone(book, step, mine, draftHere) {
  const list = h('ol', { class: 'stList' }, mine.map((r) => card(r)), draftHere ? card(null) : null);
  const zone = h('div', { class: 'stStep' + (mine.length || draftHere ? '' : ' empty') },
    step ? h('div', { class: 'stStepHead' }, h('span', { class: 'stStepN' }, stepOf.get(`${book}/${step.id}`)?.n ?? ''), h('b', {}, step.title),
      step.tag ? h('span', { class: 'olTag' }, step.tag) : null, h('span', { class: 'muted' }, mine.length ? `${mine.length} 关` : '还没有关卡'),
      h('a', { class: 'small', href: `/outline.html?group=${encodeURIComponent(group ?? '')}&book=${book}#step-${step.id}` }, '看大纲')) : null,
    list,
    state.editable && step ? h('button', { type: 'button', class: 'btn ghost small stAddHere', disabled: busy, onclick: () => openNew({ book, step: step.id }) }, '＋ 写这一步的关卡') : null);
  if (state.editable) {
    zone.addEventListener('dragover', (e) => { if (dragId != null) { e.preventDefault(); zone.classList.add('dropInto'); } });
    zone.addEventListener('dragleave', (e) => { if (!zone.contains(e.relatedTarget)) zone.classList.remove('dropInto'); });
    zone.addEventListener('drop', (e) => {
      if (dragId == null || e.defaultPrevented) return;
      e.preventDefault();
      const id = dragId;
      endDrag();
      relink(id, book, step?.id ?? null);
    });
  }
  return zone;
}

// One mission: a summary row that opens it (an editor for the group's members, the full text for everyone else).
function card(r) {
  const isNew = r == null;
  const id = r?.id ?? null;
  const open = isNew || edit?.id === id || viewing === id;
  const n = isNew ? null : indexOf(id) + 1;
  const d = !isNew && state.editable ? drafts.get(draftKey(id)) : null;
  const dirty = d && (d.version !== r.version || !same(d.cur, fieldsOf(r)));
  const info = isNew ? stepInfo(edit.cur.book, edit.cur.step) : stepInfo(r.book, r.step);
  const li = h('li', { class: 'stCard' + (open ? ' open' : ''), id: isNew ? 'm-new' : `m-${id}`, 'data-status': isNew ? 'planned' : r.status });
  const toggle = h('button', { type: 'button', class: 'stToggle', 'aria-expanded': String(open), 'aria-controls': isNew ? 'ed-new' : `ed-${id}`,
    onclick: () => (isNew ? closeEditor() : open ? closeEditor() : openMission(id, { focus: true })),
    onkeydown: (e) => {
      if (!e.altKey || isNew || !state.editable || !['ArrowUp', 'ArrowDown'].includes(e.key)) return;
      e.preventDefault();
      const to = n + (e.key === 'ArrowUp' ? -1 : 1);
      if (to >= 1 && to <= state.rows.length) moveTo(id, to, { refocus: true });
    } },
  h('span', { class: 'stTitle' }, isNew ? (edit.cur.title || '新关卡（未保存）') : r.title),
  h('span', { class: 'stMeta' },
    h('span', { class: 'badge st-' + (isNew ? 'planned' : r.status) }, isNew ? '未保存' : STATUS[r.status] || r.status),
    info ? h('span', { class: 'stLink' + (info.missing ? ' bad' : '') }, stepText(info, view === 'lane')) : null,
    r?.mission ? h('span', { class: 'stMap' }, '🗺 ', mapLabel(r.mission)) : null,
    dirty ? h('span', { class: 'tag edit' }, '草稿') : null));
  const grip = state.editable && !isNew && !open
    ? h('span', { class: 'stGrip', draggable: 'true', title: '拖动调整顺序（或选中后按 Alt+↑/↓）', 'aria-hidden': 'true' }, '⠿') : h('span', { class: 'stGrip off', 'aria-hidden': 'true' });
  li.append(h('div', { class: 'stHead' }, grip, h('span', { class: 'stNo', 'aria-label': isNew ? '新关卡' : `第 ${n} 关` }, isNew ? '新' : n), toggle));
  if (!open && r?.goal) li.append(h('p', { class: 'stSum' }, r.goal));
  if (open) li.append(isNew || edit?.id === id ? edit.el : reader(r));
  if (grip.draggable) {
    grip.addEventListener('dragstart', (e) => {
      dragId = id;
      e.dataTransfer.effectAllowed = 'move';
      e.dataTransfer.setData('text/plain', String(id));
      e.dataTransfer.setDragImage(li, 24, 20);
      li.classList.add('dragging');
      document.body.classList.add('dragActive');
    });
    grip.addEventListener('dragend', endDrag);
  }
  if (state.editable && !isNew) {
    li.addEventListener('dragover', (e) => {
      if (dragId == null || dragId === id) return;
      e.preventDefault();
      const above = e.clientY < li.getBoundingClientRect().top + li.offsetHeight / 2;
      li.classList.toggle('dropBefore', above);
      li.classList.toggle('dropAfter', !above);
    });
    li.addEventListener('dragleave', () => li.classList.remove('dropBefore', 'dropAfter'));
    li.addEventListener('drop', (e) => {
      if (dragId == null || dragId === id) return;
      e.preventDefault();
      const above = li.classList.contains('dropBefore');
      const from = dragId;
      endDrag();
      dropNear(from, r, above);
    });
  }
  return li;
}

function endDrag() {
  dragId = null;
  document.body.classList.remove('dragActive');
  for (const el of document.querySelectorAll('.dragging, .dropBefore, .dropAfter, .dropInto')) el.classList.remove('dragging', 'dropBefore', 'dropAfter', 'dropInto');
}

// Dropped before or after another mission: take that place in the campaign; in the world-line view also that step.
async function dropNear(id, target, above) {
  const from = indexOf(id);
  const at = indexOf(target.id);
  if (from < 0 || at < 0) return;
  const final = above ? (from < at ? at - 1 : at) : (from < at ? at : at + 1);
  const row = rowOf(id);
  const relinked = view === 'lane' && (row.book !== target.book || row.step !== target.step);
  if (relinked) await relink(id, target.book, target.step, { quiet: final !== from });
  if (final !== from) await moveTo(id, final + 1);
}

function reader(r) {
  const info = stepInfo(r.book, r.step);
  const part = (k, v) => (v ? h('div', { class: 'stPart' }, h('h4', {}, LABEL[k]), h('p', {}, v)) : null);
  return h('div', { class: 'stRead', id: `ed-${r.id}` },
    part('goal', r.goal), part('enemies', r.enemies), part('body', r.body), part('handoff', r.handoff),
    !r.goal && !r.enemies && !r.body && !r.handoff ? h('p', { class: 'muted' }, '还没有写内容。') : null,
    h('p', { class: 'muted' }, info ? [LABEL.link, '：', stepText(info)] : null, info && r.mission ? ' · ' : null, r.mission ? [LABEL.mission, '：', mapLabel(r.mission)] : null),
    r.updated_by ? h('p', { class: 'muted' }, `最后修改：${r.updated_by}`) : null);
}

// ---- the editor ----

function openMission(id, { focus = false } = {}) {
  const r = rowOf(id);
  if (!r) return;
  if (!state.editable) {
    viewing = viewing === id ? null : id;
    render();
    setUrlId(viewing);
    return;
  }
  if (edit?.id === id) return;
  const base = fieldsOf(r);
  const d = drafts.get(draftKey(id));
  edit = { id, version: r.version, after: null, base, cur: d?.cur ? { ...base, ...d.cur } : { ...base }, problem: null };
  if (d?.cur && d.version !== r.version && !same(d.cur, base)) edit.problem = { kind: 'stale', text: `你上次在这一关写的草稿（${ago(d.at)}）之后，${r.updated_by || '别人'}又保存过它。` };
  else if (d?.cur && !same(d.cur, base)) edit.problem = { kind: 'restored', text: `已恢复你上次没保存的改动（${ago(d.at)}）。` };
  if (d?.cur && same(d.cur, base)) drafts.drop(draftKey(id));
  build(focus);
}

function openNew({ book = null, step = null, restore = null, after } = {}) {
  if (!state.editable) return;
  const d = drafts.get(restore || draftKey(null));
  const base = { ...fieldsOf(null), book, step };
  if (after === undefined) {
    // Right after the last mission of the same step, else at the end.
    const peers = book != null ? state.rows.filter((r) => r.book === book && r.step === step) : [];
    after = (peers.at(-1) || state.rows.at(-1))?.id ?? null;
  }
  edit = { id: null, version: null, after, base, cur: d?.cur ? { ...base, ...d.cur } : { ...base }, problem: null };
  if (d?.cur) {
    if (d.after !== undefined && (d.after == null || rowOf(d.after))) edit.after = d.after;
    edit.problem = { kind: 'restored', text: `已恢复你上次没保存的新关卡（${ago(d.at)}）。` };
    if (restore && restore !== draftKey(null)) { drafts.drop(restore); stash(); }
  }
  build(true);
}

function closeEditor() {
  edit = null;
  viewing = null;
  setUrlId(null);
  render();
}

function setUrlId(id) {
  const url = new URL(location.href);
  for (const k of ['new', 'book', 'step']) url.searchParams.delete(k);
  if (id == null) url.searchParams.delete('id'); else url.searchParams.set('id', id);
  if (url.href !== location.href) history.replaceState(null, '', url);
}

function stash() {
  if (!edit) return;
  const key = draftKey(edit.id);
  if (edit.id != null && same(edit.cur, edit.base)) drafts.drop(key);
  else drafts.set(key, { cur: edit.cur, version: edit.version, after: edit.after });
}

function build(focus) {
  const e = edit;
  const isNew = e.id == null;
  const field = (k, input) => h('div', { class: 'stField' }, h('label', { for: 'f-' + k }, LABEL[k]), input);
  const set = (k, v) => { e.cur[k] = v; stash(); refresh(); };
  const title = h('input', { type: 'text', id: 'f-title', maxlength: state.limit.title, required: true, placeholder: '比如「港湾防卫战」', autocomplete: 'off' });
  title.value = e.cur.title;
  title.addEventListener('input', () => { set('title', title.value); const t = e.el.closest('.stCard')?.querySelector('.stTitle'); if (t) t.textContent = title.value || '新关卡（未保存）'; });
  const area = (k, rows) => {
    const t = h('textarea', { id: 'f-' + k, maxlength: state.limit[k], rows, placeholder: HINT[k] });
    t.value = e.cur[k];
    t.addEventListener('input', () => { set(k, t.value); grow(t); });
    return t;
  };
  const texts = Object.fromEntries(TEXTS.map((k) => [k, area(k, k === 'body' ? 8 : 3)]));
  // Status: four buttons, one pressed (a radio group the arrow keys walk).
  const status = h('div', { class: 'seg', role: 'radiogroup', 'aria-labelledby': 'l-status' });
  const paint = () => {
    for (const b of status.children) {
      const on = b.dataset.v === e.cur.status;
      b.setAttribute('aria-checked', String(on));
      b.tabIndex = on ? 0 : -1;
    }
  };
  for (const [v, t] of Object.entries(STATUS)) {
    status.append(h('button', { type: 'button', role: 'radio', class: 'st-' + v, 'data-v': v, onclick: () => { set('status', v); paint(); },
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
  paint();
  // Outline step: every step of every outline of the group, grouped by world line.
  const link = h('select', { id: 'f-link' }, h('option', { value: '' }, books.length ? '不关联' : '这个组还没有大纲'));
  for (const book of books) {
    for (const lane of book.lanes) {
      link.append(h('optgroup', { label: (books.length > 1 ? book.title + ' · ' : '') + lane.name },
        lane.steps.map((s) => h('option', { value: `${book.book}/${s.id}` }, `${stepOf.get(`${book.book}/${s.id}`).n}. ${s.title}${s.tag ? `（${s.tag}）` : ''}`))));
    }
  }
  const cur = e.cur.book == null ? '' : `${e.cur.book}/${e.cur.step}`;
  if (cur && !stepOf.has(cur)) link.append(h('option', { value: cur }, `（大纲 ${e.cur.book} 里已没有的步骤 ${e.cur.step}）`));
  link.value = cur;
  const outlineLink = h('a', { class: 'small', target: '_blank', rel: 'noopener' }, '在大纲里看这一步');
  const paintLink = () => {
    const ok = e.cur.book != null && stepOf.has(`${e.cur.book}/${e.cur.step}`);
    outlineLink.classList.toggle('hide', !ok);
    if (ok) outlineLink.href = `/outline.html?group=${encodeURIComponent(group ?? '')}&book=${e.cur.book}#step-${e.cur.step}`;
  };
  paintLink();
  link.addEventListener('change', () => {
    const [b, s] = link.value ? [Number(link.value.split('/')[0]), link.value.split('/').slice(1).join('/')] : [null, null];
    e.cur.book = b;
    e.cur.step = s;
    stash();
    refresh();
    paintLink();
  });
  // Map: a text box that searches the 400-odd stock missions (the browser's own suggestion list), narrowed by series.
  const series = h('select', { id: 'f-series', 'aria-label': '只搜这一作的地图' }, h('option', { value: '' }, '全部系列'),
    [...new Set(catalog.map((m) => m.series))].map((s) => h('option', { value: s }, s)));
  const map = h('input', { type: 'search', id: 'f-mission', list: 'maps', placeholder: '输入任务名或编号搜索', autocomplete: 'off' });
  const mapNote = h('span', { class: 'muted small', id: 'f-mission-note', 'aria-live': 'polite' });
  const fillMaps = () => $('maps').replaceChildren(...catalog.filter((m) => !series.value || m.series === series.value).map((m) => h('option', { value: mapLabel(m.key) })));
  map.value = e.cur.mission ? mapLabel(e.cur.mission) : '';
  if (e.cur.mission) series.value = byKey.get(e.cur.mission)?.series || '';
  fillMaps();
  series.addEventListener('change', fillMaps);
  const pickMap = (final) => {
    const v = map.value.trim();
    const m = v ? catalog.find((x) => mapLabel(x.key) === v) : null;
    if (!v) { set('mission', null); mapNote.textContent = ''; } else if (m) { set('mission', m.key); mapNote.textContent = ''; } else if (final) mapNote.textContent = '没有找到这张地图，请从下拉建议里选一个';
  };
  map.addEventListener('input', () => pickMap(false));
  map.addEventListener('change', () => pickMap(true));
  const problem = h('div', { class: 'stProblem hide', role: 'alert' });
  const saveBtn = h('button', { type: 'button', class: 'btn', id: 'f-save', onclick: () => save() }, isNew ? '保存新关卡' : '保存');
  const stateText = h('span', { class: 'stState muted', id: 'f-state', 'aria-live': 'polite' });
  const n = isNew ? null : indexOf(e.id) + 1;
  const total = state.rows.length;
  const target = h('input', { type: 'number', class: 'stPos', id: 'f-pos', min: 1, max: total, placeholder: n ?? '', 'aria-label': '移到第几关',
    onkeydown: (ev) => { if (ev.key === 'Enter') { ev.preventDefault(); goPos(); } } });
  const goPos = () => { const to = Number(target.value); if (Number.isInteger(to) && to >= 1 && to <= total && to !== n) moveTo(e.id, to, { refocus: true }); };
  const tools = isNew ? [h('button', { type: 'button', class: 'btn ghost', onclick: discardNew }, '不要这一关')] : [
    h('button', { type: 'button', class: 'btn ghost small', disabled: n === 1, onclick: () => moveTo(e.id, n - 1, { refocus: true }), title: 'Alt+↑' }, '↑ 上移'),
    h('button', { type: 'button', class: 'btn ghost small', disabled: n === total, onclick: () => moveTo(e.id, n + 1, { refocus: true }), title: 'Alt+↓' }, '↓ 下移'),
    h('span', { class: 'moveTo' }, '移到第', target, '关', h('button', { type: 'button', class: 'btn ghost small', disabled: total < 2, onclick: goPos }, '移动')),
    h('button', { type: 'button', class: 'btn ghost small', onclick: () => openNew({ book: e.cur.book, step: e.cur.step, after: e.id }) }, '＋ 在后面插一关'),
    h('button', { type: 'button', class: 'btn ghost small danger', onclick: () => remove(e.id) }, '删除')];
  e.el = h('div', { class: 'stEditor', id: isNew ? 'ed-new' : `ed-${e.id}`,
    onkeydown: (ev) => { if (ev.key === 'Escape' && !ev.target.closest('select')) { ev.preventDefault(); const id = e.id; closeEditor(); if (id != null) $(`m-${id}`)?.querySelector('.stToggle')?.focus(); } } },
  problem,
  h('div', { class: 'stGrid' },
    h('div', { class: 'stMain' }, field('title', title), field('goal', texts.goal), field('enemies', texts.enemies), field('body', texts.body), field('handoff', texts.handoff)),
    h('div', { class: 'stSide' },
      h('div', { class: 'stField' }, h('span', { class: 'lbl', id: 'l-status' }, LABEL.status), status),
      h('div', { class: 'stField' }, h('label', { for: 'f-link' }, LABEL.link), link, outlineLink),
      h('div', { class: 'stField' }, h('label', { for: 'f-mission' }, LABEL.mission), h('div', { class: 'row tight' }, series, map), mapNote),
      !isNew && edit.id != null && rowOf(e.id)?.updated_by ? h('p', { class: 'muted small' }, `最后修改：${rowOf(e.id).updated_by}`) : null)),
  h('div', { class: 'stBar' }, saveBtn, stateText, h('button', { type: 'button', class: 'btn ghost small', onclick: closeEditor, title: 'Esc' }, '收起'),
    h('span', { class: 'grow' }), tools));
  e.refs = { problem, saveBtn, stateText };
  render();
  for (const t of Object.values(texts)) grow(t);
  showProblem();
  refresh();
  setUrlId(e.id);
  const li = e.el.closest('.stCard');
  if (li) li.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  if (focus) title.focus({ preventScroll: true });
}

function refresh() {
  if (!edit?.refs) return;
  const dirty = edit.id == null || !same(edit.cur, edit.base) || edit.problem?.kind === 'stale';
  edit.refs.saveBtn.disabled = busy || !dirty;
  edit.refs.stateText.textContent = busy ? '保存中…' : edit.id == null ? '还没保存 · 草稿已存在本机' : !same(edit.cur, edit.base)
    ? (drafts.ok ? '有改动没保存 · 草稿已存在本机 · Ctrl+S 保存' : '有改动没保存 · Ctrl+S 保存') : '已保存';
}

function showProblem() {
  const box = edit?.refs?.problem;
  if (!box) return;
  const p = edit.problem;
  box.classList.toggle('hide', !p);
  box.className = 'stProblem' + (p ? ' ' + p.kind : ' hide');
  if (!p) { box.replaceChildren(); return; }
  const btn = (text, fn, main = false) => h('button', { type: 'button', class: main ? 'btn small' : 'btn ghost small', onclick: fn }, text);
  const parts = [h('span', {}, p.text)];
  if (p.kind === 'restored') parts.push(btn('撤回到已保存的版本', discardMine));
  if (p.kind === 'stale' || p.kind === 'conflict') {
    parts.push(h('span', {}, ' 怎么处理？'), btn('用我的覆盖', overwrite, true), btn('看对方的版本（丢掉我的）', discardMine),
      btn('复制我的内容', async () => toast((await copyText(asText(edit.cur))) ? '已复制你的内容，可以粘贴到别处' : '复制失败，请手动选中复制', null)));
  }
  if (p.kind === 'gone') parts.push(btn('另存为新关卡', () => { edit.id = null; edit.version = null; edit.problem = null; save(); }, true), btn('复制我的内容', async () => toast((await copyText(asText(edit.cur))) ? '已复制' : '复制失败')));
  if (p.kind === 'error') parts.push(btn('重试', () => save(), true));
  parts.push(h('button', { type: 'button', class: 'toastX', 'aria-label': '关闭', onclick: () => { edit.problem = null; showProblem(); refresh(); } }, '×'));
  box.replaceChildren(...parts);
}

function asText(c) {
  const info = stepInfo(c.book, c.step);
  return [`# ${c.title}`, `状态：${STATUS[c.status]}`, info ? `${LABEL.link}：${stepText(info)}` : '', c.mission ? `${LABEL.mission}：${mapLabel(c.mission)}` : '',
    ...TEXTS.filter((k) => c[k]).map((k) => `\n## ${LABEL[k]}\n${c[k]}`)].filter(Boolean).join('\n');
}

async function save() {
  if (!edit || busy) return;
  const e = edit;
  if (!e.cur.title.trim()) {
    e.problem = { kind: 'error', text: '关卡名不能为空。' };
    showProblem();
    $('f-title').focus();
    return;
  }
  busy = true;
  refresh();
  try {
    const out = await api('/api/story/save', { group, id: e.id, version: e.version, after: e.id == null ? e.after : undefined, ...e.cur });
    drafts.drop(draftKey(e.id));
    const wasNew = e.id == null;
    e.id = out.id;
    e.version = out.version;
    e.base = { ...e.cur };
    e.problem = null;
    busy = false;
    await load();
    if (edit === e) build(false);
    toast(wasNew ? `已新增第 ${indexOf(out.id) + 1} 关「${e.cur.title}」` : `已保存「${e.cur.title}」`);
  } catch (err) {
    busy = false;
    if (edit !== e) return;
    e.problem = err.status === 409 && e.id != null ? { kind: 'conflict', text: err.message + '。' }
      : err.status === 404 && e.id != null ? { kind: 'gone', text: '这一关已经被别人删除了，你的改动还在。' }
        : err.status === 404 ? { kind: 'error', text: '要插在它后面的那一关刚被删掉了；点「重试」会把新关卡放到最后。' }
          : { kind: 'error', text: '保存失败：' + err.message };
    if (err.status === 404 && e.id == null) { e.after = null; stash(); }
    showProblem();
    refresh();
  }
}

// Mine wins: take the newest version number and save again (what the other person wrote is replaced).
async function overwrite() {
  const e = edit;
  await load();
  const r = rowOf(e.id);
  if (!r) { e.problem = { kind: 'gone', text: '这一关已经被别人删除了，你的改动还在。' }; showProblem(); return; }
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
  if (!r) return closeEditor();
  edit = { id: e.id, version: r.version, after: null, base: fieldsOf(r), cur: fieldsOf(r), problem: null };
  build(false);
}

function discardNew() {
  const e = edit;
  const empty = !e.cur.title && TEXTS.every((k) => !e.cur[k]);
  drafts.drop(draftKey(null));
  closeEditor();
  if (!empty) toast('已丢弃新关卡', { label: '撤销', fn: () => { drafts.set(draftKey(null), { cur: e.cur, after: e.after }); openNew({ after: e.after }); } });
}

// ---- moves, deletes, re-links (all undoable) ----

async function run(fn) {
  if (busy) return false;
  busy = true;
  error('');
  document.body.classList.add('busy');
  try { await fn(); return true; } catch (e) { error('操作失败：' + e.message); return false; } finally {
    busy = false;
    document.body.classList.remove('busy');
    try { await load(); } catch (e) { error('刷新失败：' + e.message); }
  }
}

function record(text, undo, redo) {
  undos.push({ text, undo, redo });
  if (undos.length > 50) undos.shift();
  redos.length = 0;
  toast(text, { label: '撤销', fn: undoLast });
}

async function undoLast() {
  const op = undos.pop();
  if (!op) return toast('没有可以撤销的操作');
  if (await run(op.undo)) { redos.push(op); toast('已撤销：' + op.text, { label: '重做', fn: redoLast }); } else undos.push(op);
}

async function redoLast() {
  const op = redos.pop();
  if (!op) return toast('没有可以重做的操作');
  if (await run(op.redo)) { undos.push(op); toast('已重做：' + op.text, { label: '撤销', fn: undoLast }); } else redos.push(op);
}

async function moveTo(id, to, { refocus = false } = {}) {
  const from = indexOf(id) + 1;
  if (!from || to === from) return;
  const title = rowOf(id).title;
  const keepEditor = edit?.id === id;
  if (await run(() => api('/api/story/move', { group, id, position: to }))) {
    record(`「${title}」从第 ${from} 关移到第 ${to} 关`, () => api('/api/story/move', { group, id, position: from }), () => api('/api/story/move', { group, id, position: to }));
  }
  if (keepEditor && edit?.id === id) build(false);
  if (refocus) (keepEditor ? $('f-pos') : $(`m-${id}`)?.querySelector('.stToggle'))?.focus({ preventScroll: false });
}

async function remove(id) {
  const r = rowOf(id);
  if (!r) return;
  const at = indexOf(id);
  const ref = { id };
  const keep = edit?.id === id ? { ...edit.cur } : fieldsOf(r);
  if (edit?.id === id) { drafts.drop(draftKey(id)); edit = null; setUrlId(null); }
  const restore = async () => {
    const prev = state.rows[at - 1]?.id;
    const out = await api('/api/story/save', { group, ...keep, after: prev });
    ref.id = out.id;
    if (prev == null && state.rows.length) await api('/api/story/move', { group, id: out.id, position: 1 });
  };
  if (await run(() => api('/api/story/remove', { group, id }))) record(`已删除第 ${at + 1} 关「${keep.title}」`, restore, () => api('/api/story/remove', { group, id: ref.id }));
}

async function relink(id, book, step, { quiet = false } = {}) {
  const r = rowOf(id);
  if (!r || (r.book === book && r.step === step)) return;
  const old = [r.book, r.step];
  const to = (b, s) => async () => { const now = rowOf(id); await api('/api/story/save', { group, id, version: now.version, ...fieldsOf(now), book: b, step: s }); };
  if (await run(to(book, step)) && !quiet) {
    const where = book == null ? '取消了大纲关联' : `关联到「${stepText(stepInfo(book, step), true)}」`;
    record(`「${r.title}」${where}`, to(...old), to(book, step));
  }
}

// ---- page wiring ----

function clearFilter() {
  shown = { text: '', status: '' };
  $('filter').value = '';
  render();
}

$('add').addEventListener('click', () => openNew({ after: state.rows.at(-1)?.id ?? null }));
$('filter').addEventListener('input', () => { shown.text = $('filter').value.trim().toLowerCase(); render(); });
for (const b of $('views').children) {
  b.addEventListener('click', () => {
    view = b.dataset.view;
    try { localStorage.setItem('edf6.story.view', view); } catch { /* blocked */ }
    render();
  });
}
document.addEventListener('keydown', (e) => {
  const mod = e.ctrlKey || e.metaKey;
  if (mod && e.key.toLowerCase() === 's') { e.preventDefault(); if (edit) save(); return; }
  if (typing(e)) return;
  if (mod && !e.shiftKey && e.key.toLowerCase() === 'z') { e.preventDefault(); undoLast(); }
  else if (mod && (e.key.toLowerCase() === 'y' || (e.shiftKey && e.key.toLowerCase() === 'z'))) { e.preventDefault(); redoLast(); }
  else if (e.key === '/' && !mod) { e.preventDefault(); $('filter').focus(); }
});
window.addEventListener('beforeunload', (e) => {
  if (edit && !drafts.ok && (edit.id == null || !same(edit.cur, edit.base))) e.preventDefault();
});

async function init() {
  const [missions] = await Promise.all([api('/api/missions'), load()]);
  catalog = missions.missions;
  byKey = new Map(catalog.map((m) => [m.key, m]));
  try { await loadOutlines(); } catch (e) { error('大纲加载失败（关卡照常可写）：' + e.message); }
  render();
  // Deep links: ?id= opens a mission; ?new=1&book=&step= (from the outline's 写关卡) starts one on that step.
  if (params.get('new') && state.editable) {
    const book = Number(params.get('book')) || null;
    openNew({ book, step: book ? params.get('step') : null });
  } else if (params.get('id')) {
    const id = Number(params.get('id'));
    if (rowOf(id)) openMission(id, { focus: false });
    $(`m-${id}`)?.scrollIntoView({ block: 'start' });
  }
}
init().catch((e) => { error('加载失败：' + e.message); $('story').textContent = '暂时无法加载关卡，请刷新重试。'; });
