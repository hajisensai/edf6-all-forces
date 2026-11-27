'use strict';
// The external review page (/api/review, src/review.js): the published story outlines, one at a time in an order
// shuffled for each visitor, in Chinese, English or Japanese; anyone scores (1..5) and comments, no account needed.
// The outlines and the reviewer's own comment are people's text: they only ever go into text nodes (h() below), never markup.
// The browser keeps a random reviewer token (localStorage) so a reviewer can change or take back their own entry;
// the average shows only after the reviewer has scored that outline (so it does not sway the score); other reviewers'
// comments are never shown here (the server does not send them).

const $ = (id) => document.getElementById(id);
const LANGS = { zh: '中文', en: 'English', ja: '日本語' };
const HTML_LANG = { zh: 'zh-CN', en: 'en', ja: 'ja' };

const T = {
  zh: {
    h1: 'EDF 全军出击 · 剧情大纲评分',
    intro: '「EDF 全军出击」是一个把《地球防卫军 4.1 / 5 / 6》的剧情串在一起的玩家自制 MOD。下面是几份候选的剧情大纲，按随机顺序展示，不标作者。'
      + '请读完一份后打 1–5 分（5 = 很想玩到这个剧情），也可以写几句评论。不用注册；同一台设备可以随时改或删掉自己的评分。评论只有开发者看得到；打完分后能看到这份的平均分。',
    count: (i, n) => `第 ${i} / ${n} 份`, prev: '‹ 上一份', next: '下一份 ›', none: '现在没有待评分的大纲。',
    orig: '', tr: '',
    steps: (n) => `${n} 步`, merge: (names) => `${names} 汇入`, sep: ' ＋ ',
    formTitle: '你的评分', scores: ['1 很差', '2 一般偏下', '3 一般', '4 不错', '5 很棒'],
    nameLabel: '昵称（可空，只有开发者看得到）', bodyLabel: '评论（可空，只有开发者看得到）',
    submit: '提交', update: '更新我的评分', remove: '删掉我的评分', saved: '已保存，谢谢！', savedNext: '上一份已保存，谢谢！这是下一份。', allDone: '已保存。全部大纲都评过了，谢谢！', removed: '已删除',
    needOne: '请打分或写评论', othersTitle: '大家的评分', stats: (avg, n) => (avg == null ? `${n} 人参与` : `平均 ★ ${avg.toFixed(1)} · ${n} 人评分`),
    rated: '已评',
    loadFail: '暂时无法加载，请刷新重试。', busy: '提交太频繁了，请过一会儿再试', gone: '这份大纲已经下架了，请刷新页面',
  },
  en: {
    h1: 'EDF All Forces · Story Outline Review',
    intro: '"EDF All Forces" is a fan-made mod that ties together the stories of Earth Defense Force 4.1, 5 and 6. Below are several candidate story outlines, '
      + 'shown in a random order without their authors. After reading one, please score it from 1 to 5 (5 = I really want to play this story) and, if you like, leave a comment. '
      + 'No sign-up needed; on this device you can change or delete your own entry at any time. Comments are read only by the developers; once you have scored an outline you can see its average.',
    count: (i, n) => `Outline ${i} of ${n}`, prev: '‹ Previous', next: 'Next ›', none: 'There are no outlines to review right now.',
    orig: 'Original in Chinese', tr: 'English translation of the Chinese original.',
    steps: (n) => `${n} step${n === 1 ? '' : 's'}`, merge: (names) => `${names} merge here`, sep: ' + ',
    formTitle: 'Your score', scores: ['1 Poor', '2 Below average', '3 Average', '4 Good', '5 Excellent'],
    nameLabel: 'Nickname (optional, seen only by the developers)', bodyLabel: 'Comment (optional, seen only by the developers)',
    submit: 'Submit', update: 'Update my entry', remove: 'Delete my entry', saved: 'Saved, thank you!', savedNext: 'Saved, thank you! Here is the next one.', allDone: 'Saved. You have reviewed every outline, thank you!', removed: 'Deleted',
    needOne: 'Give a score or write a comment', othersTitle: 'Everyone\'s scores', stats: (avg, n) => (avg == null ? `${n} taking part` : `Average ★ ${avg.toFixed(1)} · ${n} score${n === 1 ? '' : 's'}`),
    rated: 'scored',
    loadFail: 'Could not load the page, please reload.', busy: 'Too many submissions, please try again later', gone: 'This outline is no longer shown, please reload the page',
  },
  ja: {
    h1: 'EDF 全軍出撃 · ストーリー構成案の評価',
    intro: '「EDF 全軍出撃」は『地球防衛軍4.1／5／6』のストーリーをつなぐファンメイドMODです。以下はストーリー構成案の候補で、ランダムな順番・作者名なしで表示しています。'
      + '一つ読み終えたら 1〜5 で評価してください（5 = このストーリーをぜひ遊びたい）。コメントも歓迎です。登録は不要で、この端末からならいつでも自分の評価を変更・削除できます。コメントは開発者だけが読みます。評価すると平均点が見られます。',
    count: (i, n) => `${n} 件中 ${i} 件目`, prev: '‹ 前へ', next: '次へ ›', none: '現在、評価できる構成案はありません。',
    orig: '原文は中国語', tr: '中国語の原文を日本語に翻訳したものです。',
    steps: (n) => `${n} ステップ`, merge: (names) => `${names} が合流`, sep: '＋',
    formTitle: 'あなたの評価', scores: ['1 よくない', '2 いまひとつ', '3 ふつう', '4 よい', '5 とてもよい'],
    nameLabel: 'ニックネーム（任意・開発者だけが見られます）', bodyLabel: 'コメント（任意・開発者だけが読みます）',
    submit: '送信', update: '評価を更新', remove: '自分の評価を削除', saved: '保存しました。ありがとうございます！', savedNext: '保存しました。ありがとうございます！次の大綱です。', allDone: '保存しました。すべての大綱を評価していただきました。ありがとうございます！', removed: '削除しました',
    needOne: '評価するかコメントを書いてください', othersTitle: 'みんなの評価', stats: (avg, n) => (avg == null ? `${n} 人が参加` : `平均 ★ ${avg.toFixed(1)}・${n} 人が評価`),
    rated: '評価済み',
    loadFail: '読み込めませんでした。再読み込みしてください。', busy: '送信が多すぎます。しばらくしてからお試しください', gone: 'この構成案は公開終了しました。ページを再読み込みしてください',
  },
};

const store = {
  get(k) { try { return localStorage.getItem('edf6review.' + k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem('edf6review.' + k, v); } catch { /* private window: this visit only */ } },
};

let lang = pickLang();
let state = null;   // the last /api/review answer
let order = [];     // item ids, shuffled once per visitor
let at = 0;         // index into order
let busy = false;
let token = store.get('token') || null;
let memo = null;    // the token when localStorage is unavailable

function pickLang() {
  const asked = new URLSearchParams(location.search).get('lang') || store.get('lang');
  if (asked && LANGS[asked]) return asked;
  for (const l of navigator.languages || [navigator.language || '']) {
    const p = String(l).toLowerCase().slice(0, 2);
    if (LANGS[p]) return p;
  }
  return 'en';
}
const t = () => T[lang];

function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (key.startsWith('on')) el.addEventListener(key.slice(2), value);
    else if (value != null && value !== false) el.setAttribute(key, value === true ? '' : String(value));
  }
  for (const kid of kids.flat()) if (kid != null && kid !== '') el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  return el;
}

function myToken() {
  if (token) return token;
  const b = crypto.getRandomValues(new Uint8Array(16));
  token = [...b].map((x) => x.toString(16).padStart(2, '0')).join('');
  store.set('token', token);
  memo = token;
  return token;
}

async function api(path, payload) {
  const headers = {};
  if (token || memo) headers['x-review-token'] = token || memo;
  const res = await fetch(path, payload === undefined ? { cache: 'no-store', headers } : {
    method: 'POST', headers: { ...headers, 'content-type': 'application/json' }, body: JSON.stringify(payload),
  });
  const out = await res.json().catch(() => ({}));
  if (!res.ok) {
    const e = new Error(res.status === 429 ? t().busy : res.status === 404 ? t().gone : out.error || `HTTP ${res.status}`);
    e.status = res.status;
    throw e;
  }
  return out;
}

function error(text) {
  $('error').textContent = text || '';
  $('error').classList.toggle('hide', !text);
}

// The visitor's order: kept across visits; outlines published later go in at random places.
function shuffle(ids) {
  let saved = [];
  try { saved = JSON.parse(store.get('order') || '[]'); } catch { saved = []; }
  const out = (Array.isArray(saved) ? saved : []).filter((id) => ids.includes(id));
  for (const id of ids) if (!out.includes(id)) out.splice(Math.floor(Math.random() * (out.length + 1)), 0, id);
  store.set('order', JSON.stringify(out));
  return out;
}

const item = () => state.items.find((x) => x.id === order[at]);

// Rows of lanes: a lane that follows other lanes sits one row below the deepest of them (as on /outline.html).
function rows(o) {
  const lane = (id) => o.lanes.find((l) => l.id === id);
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
  for (const l of o.lanes) (out[d(l, new Set())] ||= []).push(l);
  return out.filter(Boolean).map((row) => ({ row, from: [...new Set(row.flatMap((l) => l.after || []))].map(lane).filter(Boolean) }));
}

function mergeBand(from) {
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
  return h('div', { class: 'olMerge' }, svg, h('span', {}, t().merge(from.map((l) => l.name).join(t().sep))));
}

function renderLane(l) {
  return h('div', { class: 'olLane', 'data-tone': l.tone || 'edf' },
    h('div', { class: 'row' }, h('h3', { class: 'olName' }, l.name), h('span', { class: 'muted' }, t().steps(l.steps.length))),
    l.note ? h('p', { class: 'muted olNote' }, l.note) : null,
    h('ol', { class: 'olSteps' }, l.steps.map((s, i) => h('li', { class: 'olStep', 'data-tone': s.tone || null },
      h('span', { class: 'olDot' }, i + 1),
      h('div', { class: 'olCard' }, h('h3', {}, s.title, s.tag ? h('span', { class: 'olTag' }, s.tag) : null), s.body ? h('p', {}, s.body) : null)))));
}

function renderChrome() {
  document.documentElement.lang = HTML_LANG[lang];
  document.title = t().h1;
  $('h1').textContent = t().h1;
  $('intro').textContent = t().intro;
  $('langs').replaceChildren(...Object.entries(LANGS).map(([k, name]) => h('button', {
    type: 'button', class: 'chip', lang: HTML_LANG[k], 'aria-pressed': k === lang ? 'true' : 'false',
    onclick: () => { if (k === lang) return; lang = k; store.set('lang', k); render(); },
  }, name)));
  $('prev').textContent = t().prev;
  $('next').textContent = t().next;
  $('formTitle').textContent = t().formTitle;
  $('nameLabel').textContent = t().nameLabel;
  $('bodyLabel').textContent = t().bodyLabel;
  $('remove').textContent = t().remove;
  $('othersTitle').textContent = t().othersTitle;
}

function render() {
  renderChrome();
  const n = order.length;
  $('nav').classList.toggle('hide', !n);
  for (const id of ['paper', 'formBox']) $(id).classList.toggle('hide', !n);
  $('none').textContent = t().none;
  $('none').classList.toggle('hide', !!n || !state);
  if (!n) { $('others').classList.add('hide'); return; }
  const it = item();
  const mine = state.mine[it.id];
  $('count').textContent = t().count(at + 1, n);
  $('picks').replaceChildren(...order.map((id, i) => h('button', {
    type: 'button', class: 'btn small' + (i === at ? '' : ' ghost'), 'aria-current': i === at ? 'true' : null,
    title: state.mine[id] ? t().rated : null, onclick: () => go(i),
  }, String(i + 1) + (state.mine[id] ? ' ✓' : ''))));
  $('prev').disabled = at === 0;
  $('next').disabled = at === n - 1;
  const o = it[lang] || it.zh;
  $('title').textContent = o.title;
  $('tnote').textContent = lang === 'zh' ? '' : t().tr;
  $('tnote').classList.toggle('hide', lang === 'zh');
  const parts = [];
  rows(o).forEach(({ row, from }, i) => {
    if (i && from.length) parts.push(mergeBand(from));
    parts.push(h('div', { class: 'olRow' }, row.map(renderLane)));
  });
  $('board').replaceChildren(h('div', { class: 'olBoard' }, parts));
  renderForm(it, mine);
  renderOthers(it, mine);
}

let formFor = null;   // the item the form's fields were filled for (switching outlines refills them, a language switch does not)
function renderForm(it, mine) {
  const picked = Number(document.querySelector('input[name="score"]:checked')?.value) || null;
  const fresh = formFor !== it.id;
  const score = fresh ? mine?.score ?? null : picked;
  $('score').replaceChildren(...t().scores.map((label, i) => h('label', {}, h('input', { type: 'radio', name: 'score', value: i + 1, checked: score === i + 1 }), label)));
  if (fresh) {
    $('name').value = mine ? mine.name : store.get('name') || '';
    $('body').value = mine ? mine.body : '';
    $('formError').textContent = '';
    $('savedNote').textContent = '';
    formFor = it.id;
  }
  $('submit').textContent = mine ? t().update : t().submit;
  $('remove').classList.toggle('hide', !mine);
  $('submit').disabled = busy;
  $('remove').disabled = busy;
}

function renderOthers(it, mine) {
  $('others').classList.toggle('hide', !mine);
  if (!mine) return;
  const s = state.stats[it.id] || { n: 0, avg: null };
  $('stats').textContent = t().stats(s.avg, s.n);
}

function go(i) {
  if (i < 0 || i >= order.length) return;
  at = i;
  render();
  $('paper').scrollIntoView({ behavior: 'smooth', block: 'start' });
}

async function send(path, payload) {
  busy = true;
  render();
  try {
    state = await api(path, payload);
    error('');
    return true;
  } catch (e) {
    $('formError').textContent = e.message;
    return false;
  } finally {
    busy = false;
    render();
  }
}

$('form').addEventListener('submit', async (e) => {
  e.preventDefault();
  if (busy) return;
  const score = Number(document.querySelector('input[name="score"]:checked')?.value) || null;
  const text = $('body').value.trim();
  if (!score && !text) { $('formError').textContent = t().needOne; return; }
  const name = $('name').value.trim();
  store.set('name', name);
  $('formError').textContent = '';
  const id = order[at];
  if (await send('/api/review/rate', { token: myToken(), item: id, score, name, body: text, lang })) {
    // On to the next outline this visitor has not reviewed yet (after this one, wrapping round); none left: stay.
    const next = [...order.slice(at + 1), ...order.slice(0, at)].find((x) => !state.mine[x]);
    formFor = null;
    if (next != null && order.includes(id)) {
      go(order.indexOf(next));
      $('savedNote').textContent = t().savedNext;
    } else {
      render();
      $('savedNote').textContent = next == null ? t().allDone : t().saved;
      $('others').scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    }
  }
});
$('remove').addEventListener('click', async () => {
  if (busy || !token && !memo) return;
  if (await send('/api/review/unrate', { token: token || memo, item: order[at] })) {
    formFor = null;
    render();
    $('savedNote').textContent = t().removed;
  }
});
$('prev').addEventListener('click', () => go(at - 1));
$('next').addEventListener('click', () => go(at + 1));

// Developers (logged in): every published outline's scores and all comments, newest first.
let isDev = false;   // from the first load: a score's reply does not repeat it
async function loadResults() {
  if (!isDev) return;
  $('results').classList.remove('hide');
  $('resultsNote').textContent = '加载中……';
  let out;
  try { out = await api('/api/review/results'); } catch (e) { $('resultsNote').textContent = '加载失败：' + e.message; return; }
  const num = (v) => (v == null ? '-' : v.toFixed(2));
  const total = out.items.reduce((a, it) => a + it.entries.length, 0);
  $('resultsNote').textContent = `共 ${out.items.length} 份大纲、${total} 条评分 / 评论。访客文字只作展示。`;
  $('resultsList').replaceChildren(...out.items.slice().sort((a, b) => b.active - a.active || b.id - a.id).map((it) => h('div', { class: 'report' },
    h('h3', { style: 'margin:0' }, it.title),
    h('div', { class: 'muted' }, `${it.group || '（未分组）'} · 第 ${it.book} 份 · 版本 ${it.version} · ${it.active ? '展示中' : '已下架'}`),
    h('div', {}, h('b', {}, `平均 ★ ${num(it.avg)}（${it.n} 人评分）`), '　',
      h('span', { class: 'muted' }, Object.entries(it.by_lang).map(([k, v]) => `${LANGS[k]} ${num(v.avg)}（${v.n}）`).join(' · '))),
    h('div', { class: 'muted' }, '分布：' + [5, 4, 3, 2, 1].map((s) => `${s}★ ${it.entries.filter((e) => e.score === s).length}`).join(' · ')
      + ` · 只评论 ${it.entries.filter((e) => e.score == null).length}`),
    it.entries.length ? it.entries.slice().reverse().map((e) => h('div', { class: 'reply' },
      h('div', { class: 'muted' }, `${e.score ? '★ ' + e.score : '未打分'} · ${e.name || '匿名'} · ${LANGS[e.lang] || e.lang} · 访客 ${e.rater} · `
        + new Date(e.at).toLocaleString('zh-CN', { hour12: false })),
      e.body ? h('p', { class: 'reviewBody' }, e.body) : null)) : h('p', { class: 'muted' }, '还没有人评。'))));
}
$('reload').addEventListener('click', () => loadResults());

async function load() {
  state = await api('/api/review');
  isDev = !!state.dev;
  loadResults();
  order = shuffle(state.items.map((x) => x.id));
  const first = order.findIndex((id) => !state.mine[id]);
  at = first < 0 ? 0 : first;
  render();
}

renderChrome();
load().catch((e) => { renderChrome(); error(t().loadFail + ' (' + e.message + ')'); });
