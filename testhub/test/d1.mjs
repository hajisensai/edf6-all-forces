// A D1 stand-in over node:sqlite for the tests: prepare(sql).bind(...).first()/all()/run(), batch() in one
// transaction, the real schema.sql loaded.
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';

export function d1() {
  const db = new DatabaseSync(':memory:');
  db.exec(readFileSync(new URL('../schema.sql', import.meta.url), 'utf8'));
  const plain = (row) => (row ? { ...row } : null);
  const stmt = (sql, args = []) => ({
    bind: (...a) => stmt(sql, a),
    first: async () => plain(db.prepare(sql).get(...args)),
    all: async () => ({ results: db.prepare(sql).all(...args).map(plain) }),
    run: async () => ({ meta: { changes: Number(db.prepare(sql).run(...args).changes) } }),
    exec: () => db.prepare(sql).run(...args),
  });
  return {
    raw: db,
    prepare: (sql) => stmt(sql),
    batch: async (list) => {
      db.exec('BEGIN');
      try { for (const s of list) s.exec(); db.exec('COMMIT'); } catch (e) { db.exec('ROLLBACK'); throw e; }
      return [];
    },
  };
}
