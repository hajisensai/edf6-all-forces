-- EDF 全军出击 测试站 (testhub/): builds mirrored into R2, test cases, testers' reports and the files they attach.
CREATE TABLE IF NOT EXISTS builds (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,          -- the zip's file name, also its R2 key under builds/
  version TEXT NOT NULL,
  channel TEXT NOT NULL,              -- nightly (main) / test (a branch build sent for one fix)
  commit_sha TEXT NOT NULL DEFAULT '',
  notes TEXT NOT NULL DEFAULT '',
  size INTEGER NOT NULL,
  sha256 TEXT NOT NULL,
  at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS cases (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  title TEXT NOT NULL,
  steps TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'todo', -- todo / verify / pass / fail / closed
  dev_note TEXT NOT NULL DEFAULT '',
  sort INTEGER NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS reports (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  case_id INTEGER,                    -- null: a problem of its own
  author TEXT NOT NULL,
  result TEXT NOT NULL,               -- pass / fail / new / info
  note TEXT NOT NULL DEFAULT '',
  version TEXT NOT NULL DEFAULT '',
  source TEXT NOT NULL DEFAULT 'web', -- web / exe
  at INTEGER NOT NULL,
  reply TEXT NOT NULL DEFAULT '',
  reply_at INTEGER
);
CREATE TABLE IF NOT EXISTS files (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  report_id INTEGER NOT NULL,
  name TEXT NOT NULL,
  size INTEGER NOT NULL,
  r2_key TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS files_report ON files(report_id);

-- Level plan (src/plan.js): proposals for new missions and for changing stock ones, their replies, and each group's
-- outline (the stock missions in order, new missions placed between them, loop markers). Rows belong to the
-- author's group (ACCOUNTS); only that group and developers see them. Text is stored cleaned (no control or
-- direction characters) and always shown as text, never as markup.
CREATE TABLE IF NOT EXISTS proposals (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  grp TEXT NOT NULL,
  kind TEXT NOT NULL,                 -- new / edit
  mission TEXT,                       -- edit: the stock mission's key (src/missions.json); new: null
  title TEXT NOT NULL,
  body TEXT NOT NULL DEFAULT '',
  author TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'open', -- open / accepted / done / rejected (developers set it)
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS proposals_grp ON proposals(grp, id);
CREATE TABLE IF NOT EXISTS proposal_replies (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  proposal_id INTEGER NOT NULL,
  author TEXT NOT NULL,
  body TEXT NOT NULL,
  at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS proposal_replies_p ON proposal_replies(proposal_id, id);
CREATE TABLE IF NOT EXISTS outline (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  grp TEXT NOT NULL,
  kind TEXT NOT NULL,                 -- stock / new / loop
  mission TEXT,                       -- stock: its key
  proposal_id INTEGER,                -- new: the proposal it places
  note TEXT NOT NULL DEFAULT '',      -- loop: its label
  sort REAL NOT NULL,                 -- order within the group; an insert takes the midpoint of its neighbours
  author TEXT NOT NULL DEFAULT '',
  at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS outline_grp ON outline(grp, sort);
CREATE UNIQUE INDEX IF NOT EXISTS outline_stock ON outline(grp, mission) WHERE kind = 'stock';
CREATE UNIQUE INDEX IF NOT EXISTS outline_new ON outline(grp, proposal_id) WHERE kind = 'new';
-- One row per write to the plan, for the per-account rate limit; rows older than two days are dropped.
CREATE TABLE IF NOT EXISTS write_log (
  author TEXT NOT NULL,
  at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS write_log_a ON write_log(author, at);
