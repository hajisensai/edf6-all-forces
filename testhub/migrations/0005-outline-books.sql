-- Several outlines per group (/outline.html): story_outline (one row per group) moves into story_outlines as each
-- group's outline 1, and its scores' items gain the outline's number ("1/s1"). Repeatable: the moved rows leave
-- story_outline, so a second run moves nothing.
CREATE TABLE IF NOT EXISTS story_outline (
  grp TEXT PRIMARY KEY,
  data TEXT NOT NULL,
  version INTEGER NOT NULL,
  updated_by TEXT,
  updated_at INTEGER
);
CREATE TABLE IF NOT EXISTS story_outlines (
  grp TEXT NOT NULL,
  book INTEGER NOT NULL,            -- the outline's number within its group, from 1; never reused while the group has a higher one
  data TEXT NOT NULL,               -- the outline as JSON (src/outline.js normalize())
  version INTEGER NOT NULL,         -- bumped on every save; a save names the version it was edited from
  updated_by TEXT,
  updated_at INTEGER,
  PRIMARY KEY (grp, book)
);
CREATE TABLE IF NOT EXISTS outline_ratings (
  grp TEXT NOT NULL,
  item TEXT NOT NULL,
  author TEXT NOT NULL,
  score INTEGER NOT NULL CHECK (score BETWEEN 1 AND 5),
  body TEXT NOT NULL DEFAULT '',
  at INTEGER NOT NULL,
  PRIMARY KEY (grp, item, author)
);
UPDATE outline_ratings SET item = '1/' || item
  WHERE instr(item, '/') = 0 AND grp IN (SELECT grp FROM story_outline);
INSERT OR IGNORE INTO story_outlines (grp, book, data, version, updated_by, updated_at)
  SELECT grp, 1, data, version, updated_by, updated_at FROM story_outline;
DELETE FROM story_outline;
