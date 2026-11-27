-- External review of the story outlines (/rate.html) without touching existing data. Repeatable.
CREATE TABLE IF NOT EXISTS review_items (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  grp TEXT NOT NULL,
  book INTEGER NOT NULL,
  version INTEGER NOT NULL,         -- the outline's version the snapshot (and its translations) were taken from
  data TEXT NOT NULL,               -- {"zh": outline, "en": outline, "ja": outline}
  active INTEGER NOT NULL DEFAULT 1,  -- shown on /rate.html now
  published_at INTEGER NOT NULL,
  UNIQUE (grp, book, version)
);
CREATE TABLE IF NOT EXISTS review_scores (
  item INTEGER NOT NULL,            -- review_items.id
  rater TEXT NOT NULL,              -- SHA-256 of the random token the reviewer's browser keeps
  score INTEGER CHECK (score IS NULL OR score BETWEEN 1 AND 5),   -- null = a comment without a score
  name TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  lang TEXT NOT NULL,               -- the language the reviewer read it in
  at INTEGER NOT NULL,
  PRIMARY KEY (item, rater)
);
-- One row per visitor write to the review, for the per-IP rate limit (a keyed hash, not the address); rows older than two days are dropped.
CREATE TABLE IF NOT EXISTS review_log (
  who TEXT NOT NULL,
  at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS review_log_who ON review_log (who, at);
