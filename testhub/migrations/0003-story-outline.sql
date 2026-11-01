-- Add each group's story outline (/outline.html) without touching existing data.
CREATE TABLE IF NOT EXISTS story_outline (
  grp TEXT PRIMARY KEY,
  data TEXT NOT NULL,               -- the outline as JSON (src/outline.js normalize())
  version INTEGER NOT NULL,         -- bumped on every save; a save names the version it was edited from
  updated_by TEXT,
  updated_at INTEGER
);
