-- 关卡策划 (/story.html): each mission's goal, enemies and hand-over to the next one, the outline step it plays out
-- (book + step of the same group's outline), and a version every edit is checked against (two people editing one
-- mission at once: the later save gets a 409). schema.sql already has the columns for a new database; run this once on
-- a database made before (ALTER TABLE has no IF NOT EXISTS: a second run fails with "duplicate column", nothing changes).
ALTER TABLE story_missions ADD COLUMN goal TEXT NOT NULL DEFAULT '';
ALTER TABLE story_missions ADD COLUMN enemies TEXT NOT NULL DEFAULT '';
ALTER TABLE story_missions ADD COLUMN handoff TEXT NOT NULL DEFAULT '';
ALTER TABLE story_missions ADD COLUMN book INTEGER;
ALTER TABLE story_missions ADD COLUMN step TEXT;
ALTER TABLE story_missions ADD COLUMN version INTEGER NOT NULL DEFAULT 1;
ALTER TABLE story_missions ADD COLUMN updated_by TEXT;
UPDATE story_missions SET updated_by = author WHERE updated_by IS NULL;
