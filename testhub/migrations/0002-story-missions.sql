-- Add the separate MOD story list without changing existing plans or proposals.
CREATE TABLE IF NOT EXISTS story_missions (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  grp TEXT NOT NULL,
  title TEXT NOT NULL,
  body TEXT NOT NULL DEFAULT '',
  mission TEXT,
  status TEXT NOT NULL DEFAULT 'planned',
  sort REAL NOT NULL,
  author TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS story_missions_grp ON story_missions(grp, sort, id);
