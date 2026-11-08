'use strict';
// Anyone can read the list (no account needed); the editing controls show only on a group the viewer can write.

const $ = (id) => document.getElementById(id);
const STATUS = { planned: '构思中', writing: '制作中', testing: '待测试', done: '已完成' };
let state;
let catalog = [];
let group = new URLSearchParams(location.search).get('group');
let editing = null;
let busy = false;
let deleting = null;

function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (key.startsWith('on')) el.addEventListener(key.slice(2), value);
    else if (value != null && value !== false) el.setAttribute(key, value === true ? '' : String(value));
  }
  for (const kid of kids.flat()) if (kid != null) el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  return el;
}

async function api(path, data) {
  const res = await fetch(path, data === undefined ? { cache: 'no-store' } : {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ ...data, group }),
  });
  if (res.status === 401) { location.href = '/login.html'; throw new Error('请先登录'); }
  const out = await res.json();
  if (!res.ok) throw new Error(out.error || `HTTP ${res.status}`);
  return out;
}

function error(text) {
  $('error').textContent = text;
  $('error').classList.toggle('hide', !text);
}

function missionName(key) {
  const m = catalog.find((m) => m.key === key);
  return m ? `[${m.series}] ${m.key.split('/').slice(1).join('/')} ${m.sc}` : key;
}

function render() {
  const needle = $('filter').value.trim().toLowerCase();
  const status = $('statusFilter').value;
  const rows = state.rows.map((row, index) => ({ ...row, index })).filter((r) =>
    (!status || r.status === status) && (!needle || `${r.title} ${r.body} ${missionName(r.mission) || ''}`.toLowerCase().includes(needle)));
  $('count').textContent = `共 ${state.rows.length} 关` + (rows.length !== state.rows.length ? `，显示 ${rows.length} 关` : '，按剧情顺序排列');
  const button = (text, fn, disabled = false) => h('button', { type: 'button', class: 'btn ghost small', onclick: fn, disabled: disabled || busy }, text);
  const last = state.rows.length;
  const moveTo = (r, position) => change('/api/story/move', { id: r.id, position });
  $('story').replaceChildren(...(rows.length ? rows.map((r) => {
    const own = state.editable && (state.me.role === 'dev' || state.me.user === r.author);
    // Jump straight to a place: one click to the top / bottom, or type the number of the place.
    const target = h('input', { type: 'number', class: 'small', min: 1, max: last, placeholder: '第几关', 'aria-label': `把「${r.title}」移到第几关`, disabled: busy || last < 2,
      onkeydown: (e) => { if (e.key === 'Enter') go(); } });
    const go = () => { const n = Number(target.value); if (Number.isInteger(n) && n >= 1 && n !== r.index + 1) moveTo(r, n); };
    const item = h('article', { class: 'storyMission' },
      h('div', { class: 'row' }, h('span', { class: 'tag' }, `第 ${r.index + 1} 关`), h('h3', {}, r.title), h('span', { class: 'badge' }, STATUS[r.status] || r.status)),
      r.body ? h('p', { class: 'storyBody' }, r.body) : h('p', { class: 'muted' }, '还没有填写剧情内容。'),
      r.mission ? h('p', { class: 'muted' }, '参考地图：', missionName(r.mission)) : null,
      h('div', { class: 'row' }, r.author ? h('span', { class: 'muted' }, `创建者：${r.author}`) : null,
        state.editable ? [
          button('移到第一关', () => moveTo(r, 1), r.index === 0),
          button('上移', () => change('/api/story/move', { id: r.id, direction: 'up' }), r.index === 0),
          button('下移', () => change('/api/story/move', { id: r.id, direction: 'down' }), r.index === last - 1),
          button('移到最后', () => moveTo(r, last), r.index === last - 1),
          h('span', { class: 'moveTo' }, '移到第', target, '关', button('移动', go, last < 2))] : null));
    if (own) item.lastChild.append(button('编辑', () => openEditor(r)), button('删除', () => {
      deleting = r.id;
      $('deleteText').textContent = `确定删除「${r.title}」？`;
      $('deleteDialog').showModal();
    }));
    return item;
  }) : [h('p', { class: 'muted' }, state.rows.length ? '没有符合条件的关卡。换个关键词或状态试试。'
    : state.group == null ? '还没有哪个组写了 MOD 剧情关卡。'
      : state.editable ? '还没有 MOD 剧情关卡。点击「新增剧情关卡」，从第一关开始编排。' : '这个组还没有 MOD 剧情关卡。')]));
}

async function load() {
  state = await api('/api/story' + (group == null ? '' : '?group=' + encodeURIComponent(group)));
  group = state.group;
  const me = state.me;
  $('me').textContent = !me ? '未登录（只能查看）' : me.user + (me.role === 'dev' ? '（开发者）' : me.group ? `（${me.group} 组）` : '');
  $('auth').textContent = me ? '退出' : '登录';
  $('auth').href = me ? '/logout' : '/login.html';
  $('add').classList.toggle('hide', !state.editable);
  $('groupBar').classList.toggle('hide', groupButtons($('group'), state.groups, group, (g) => {
    group = g;
    load().catch((e) => error(e.message));
  }) < 2);
  render();
}

async function change(path, data) {
  if (busy) return;
  busy = true;
  error('');
  $('group').disabled = true;
  $('add').disabled = true;
  render();
  try { await api(path, data); await load(); }
  catch (e) { error('操作失败：' + e.message); }
  finally { busy = false; $('group').disabled = false; $('add').disabled = false; render(); }
}

function openEditor(row = null) {
  editing = row?.id ?? null;
  $('form').reset();
  $('editorTitle').textContent = row ? '编辑剧情关卡' : '新增剧情关卡';
  $('title').value = row?.title || '';
  $('body').value = row?.body || '';
  $('mission').value = row?.mission || '';
  $('status').value = row?.status || 'planned';
  $('formError').textContent = '';
  $('editor').showModal();
  $('title').focus();
}

$('form').addEventListener('submit', async (e) => {
  e.preventDefault();
  if (busy) return;
  busy = true;
  $('save').disabled = true;
  $('cancel').disabled = true;
  $('formError').textContent = '';
  try {
    await api('/api/story/save', { id: editing, title: $('title').value, body: $('body').value, mission: $('mission').value, status: $('status').value });
    $('editor').close();
    error('');
    await load();
  } catch (err) {
    if ($('editor').open) $('formError').textContent = err.message;
    else error('保存已完成，刷新列表失败：' + err.message);
  } finally { busy = false; $('save').disabled = false; $('cancel').disabled = false; if (state) render(); }
});
$('editor').addEventListener('cancel', (e) => { if (busy) e.preventDefault(); });
$('cancel').addEventListener('click', () => $('editor').close());
$('deleteCancel').addEventListener('click', () => $('deleteDialog').close());
$('deleteConfirm').addEventListener('click', () => {
  const id = deleting;
  $('deleteDialog').close();
  if (id != null) change('/api/story/remove', { id });
});
$('deleteDialog').addEventListener('close', () => { deleting = null; });
$('add').addEventListener('click', () => openEditor());
$('filter').addEventListener('input', () => { if (state) render(); });
$('statusFilter').addEventListener('change', () => { if (state) render(); });
$('add').disabled = true;
async function init() {
  catalog = (await api('/api/missions')).missions;
  const groups = new Map();
  for (const m of catalog) {
    if (!groups.has(m.series)) groups.set(m.series, []);
    groups.get(m.series).push(m);
  }
  $('mission').replaceChildren(h('option', { value: '' }, '不指定参考地图'),
    ...[...groups].map(([series, ms]) => h('optgroup', { label: series }, ms.map((m) => h('option', { value: m.key }, missionName(m.key))))));
  await load();
  $('add').disabled = false;
}
init().catch((e) => { error('加载失败：' + e.message); $('story').textContent = '暂时无法加载剧情列表，请刷新重试。'; });
