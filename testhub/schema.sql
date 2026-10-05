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
