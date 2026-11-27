-- The design list (/design.html, src/design.js) without touching existing data. Repeatable.
CREATE TABLE IF NOT EXISTS story_designs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  grp TEXT NOT NULL,
  kind TEXT NOT NULL,               -- src/design.js KINDS: character / vehicle / weapon / gear / map / monster / other
  name TEXT NOT NULL,
  status TEXT NOT NULL,             -- todo / drafting / making / done
  side TEXT NOT NULL DEFAULT '',    -- whose it is (EDF, 以太联军, 星导者……)
  look TEXT NOT NULL DEFAULT '',    -- how it looks
  body TEXT NOT NULL DEFAULT '',    -- what it is and does
  refs TEXT NOT NULL DEFAULT '',    -- references: stock look-alikes, model sources
  book INTEGER,                     -- the outline (of the same group) its steps are in
  steps TEXT NOT NULL DEFAULT '[]', -- JSON: outline step ids it appears in
  missions TEXT NOT NULL DEFAULT '[]', -- JSON: story_missions ids that use it
  author TEXT,
  updated_by TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  version INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS story_designs_grp ON story_designs (grp, id);
