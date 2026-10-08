// Accounts come from one secret, ACCOUNTS = "user:password:role[:group],..." (role tester or dev). The group decides
// whose level proposals and outline an account sees (src/plan.js); no group = the shared ungrouped one.
// The browser keeps a signed cookie user.expiry.hmac; scripts and the installer send HTTP Basic.

export const GROUP = /^[\p{L}\p{N}_-]{0,32}$/u;

export function parseAccounts(text) {
  const out = new Map();
  for (const part of String(text).split(',')) {
    const [user, pass, role, group = ''] = part.trim().split(':');
    if (!user || !pass) continue;
    // A mistyped group must not land the account in the shared ungrouped one (it would see that group's text):
    // the account is left out until the secret is fixed.
    if (!GROUP.test(group)) { console.error(`ACCOUNTS: ${user} has a bad group name, account disabled`); continue; }
    out.set(user, { user, pass, role: role === 'dev' ? 'dev' : 'tester', group });
  }
  return out;
}

function who(a) {
  return { user: a.user, role: a.role, group: a.group };
}

function same(a, b) {
  const x = new TextEncoder().encode(a);
  const y = new TextEncoder().encode(b);
  let diff = x.length ^ y.length;
  for (let i = 0; i < Math.max(x.length, y.length); i++) diff |= (x[i] || 0) ^ (y[i] || 0);
  return diff === 0;
}

export function checkLogin(accounts, user, pass) {
  const a = accounts.get(user);
  return a && same(a.pass, pass) ? who(a) : null;
}

export function basicUser(header) {
  const m = /^Basic\s+(.+)$/i.exec(header || '');
  if (!m) return null;
  let text;
  try { text = new TextDecoder().decode(Uint8Array.from(atob(m[1]), (c) => c.charCodeAt(0))); } catch { return null; }
  const i = text.indexOf(':');
  return i < 0 ? null : { user: text.slice(0, i), pass: text.slice(i + 1) };
}

async function hmac(secret, text) {
  const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const sig = await crypto.subtle.sign('HMAC', key, new TextEncoder().encode(text));
  return btoa(String.fromCharCode(...new Uint8Array(sig))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

// The password is part of the signing key, so changing it logs that account out everywhere.
export async function makeSession(account, expires, secret) {
  const body = `${encodeURIComponent(account.user)}.${expires}`;
  return `${body}.${await hmac(secret + '|' + account.pass, body)}`;
}

// The cookie's user, if the signature holds, it has not expired and the account still exists (a removed or
// renamed account logs out everywhere).
export async function readSession(cookieHeader, secret, accounts, now = Date.now()) {
  const m = /(?:^|;\s*)s=([^;]+)/.exec(cookieHeader || '');
  if (!m) return null;
  const [user, expires, sig] = m[1].split('.');
  if (!user || !expires || !sig || Number(expires) < now) return null;
  const a = accounts.get(decodeURIComponent(user));
  if (!a || !same(sig, await hmac(secret + '|' + a.pass, `${user}.${expires}`))) return null;
  return who(a);
}
