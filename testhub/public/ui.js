'use strict';
// Shared by the outline page and the 关卡策划 page: building the page (text goes into text nodes only, never parsed as
// HTML), calling the API, short notices with an action (撤销), drafts kept in this browser, small editing helpers.

function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (key.startsWith('on') && typeof value === 'function') el.addEventListener(key.slice(2), value);
    else if (value != null && value !== false) el.setAttribute(key, value === true ? '' : String(value));
  }
  for (const kid of kids.flat(Infinity)) if (kid != null && kid !== false) el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  return el;
}

// GET when `data` is undefined, else POST it as JSON. A failure throws an Error carrying the HTTP status (409: someone
// saved first; 404: it was deleted) and the server's message.
async function api(path, data) {
  const res = await fetch(path, data === undefined ? { cache: 'no-store' } : {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(data),
  });
  if (res.status === 401) { location.href = '/login.html'; throw new Error('请先登录'); }
  let out = {};
  try { out = await res.json(); } catch { /* an error page without JSON: the status says enough */ }
  if (!res.ok) throw Object.assign(new Error(out.error || `HTTP ${res.status}`), { status: res.status });
  return out;
}

function meText(me) {
  return !me ? '未登录（只能查看）' : me.user + (me.role === 'dev' ? '（开发者）' : me.group ? `（${me.group} 组）` : '');
}

// A notice at the bottom of the page, read out by screen readers; `action` = {label, fn} adds a button (撤销).
// It stays while hovered or focused, else goes after a few seconds.
function toast(text, action, kind = '') {
  let box = document.getElementById('toasts');
  if (!box) document.body.append(box = h('div', { id: 'toasts', class: 'toasts', role: 'status', 'aria-live': 'polite' }));
  const item = h('div', { class: 'toast ' + kind }, h('span', {}, text));
  if (action) item.append(h('button', { type: 'button', class: 'btn small', onclick: () => { item.remove(); action.fn(); } }, action.label));
  item.append(h('button', { type: 'button', class: 'toastX', 'aria-label': '关闭提示', onclick: () => item.remove() }, '×'));
  box.append(item);
  while (box.children.length > 3) box.firstChild.remove();
  let timer;
  const later = () => { clearTimeout(timer); timer = setTimeout(() => { if (!item.matches(':hover, :focus-within')) item.remove(); else later(); }, action ? 8000 : 4000); };
  later();
  return item;
}

// Drafts in localStorage (gone only when saved or discarded). `ok` is false where storage is blocked (private modes):
// the pages then ask before a tab with unsaved text closes.
const drafts = {
  ok: (() => { try { localStorage.setItem('edf6.probe', '1'); localStorage.removeItem('edf6.probe'); return true; } catch { return false; } })(),
  get(key) { try { return JSON.parse(localStorage.getItem(key) || 'null'); } catch { return null; } },
  set(key, value) { try { localStorage.setItem(key, JSON.stringify({ ...value, at: Date.now() })); } catch { /* full or blocked */ } },
  drop(key) { try { localStorage.removeItem(key); } catch { /* blocked */ } },
  keys(prefix) { try { return Object.keys(localStorage).filter((k) => k.startsWith(prefix)); } catch { return []; } },
};

function ago(at) {
  const s = Math.max(0, (Date.now() - at) / 1000);
  if (s < 60) return '刚刚';
  if (s < 3600) return `${Math.floor(s / 60)} 分钟前`;
  if (s < 86400) return `${Math.floor(s / 3600)} 小时前`;
  return new Date(at).toLocaleString('zh-CN', { month: 'numeric', day: 'numeric', hour: '2-digit', minute: '2-digit' });
}

// A textarea as tall as its text (CSSOM, which the page's style policy allows).
function grow(t) {
  t.style.height = 'auto';
  t.style.height = `${t.scrollHeight + 2}px`;
}

async function copyText(text) {
  try { await navigator.clipboard.writeText(text); return true; } catch { /* no clipboard permission: fall back */ }
  const t = h('textarea', { class: 'offscreen', readonly: true });
  t.value = text;
  document.body.append(t);
  t.select();
  let ok = false;
  try { ok = document.execCommand('copy'); } catch { /* unsupported */ }
  t.remove();
  return ok;
}

// The keystroke is meant for a text field (so page shortcuts such as Ctrl+Z leave it alone).
function typing(e) {
  const t = e.target;
  return t instanceof HTMLElement && (t.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName));
}

// Re-rendering replaces the nodes: keep the focused field (by id) and its caret across it.
function keepFocus(fn) {
  const a = document.activeElement;
  const id = a && a !== document.body ? a.id : '';
  const sel = id && 'selectionStart' in a ? [a.selectionStart, a.selectionEnd] : null;
  fn();
  const b = id && document.getElementById(id);
  if (b && b !== document.activeElement) {
    b.focus({ preventScroll: true });
    if (sel && b.setSelectionRange) try { b.setSelectionRange(...sel); } catch { /* not a text field */ }
  }
}
