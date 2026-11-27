-- The main outline (listed first and opened by default) and outlines any account may edit (src/outline.js).
-- Not repeatable: ALTER TABLE ADD COLUMN fails the second time.
ALTER TABLE story_outlines ADD COLUMN open INTEGER NOT NULL DEFAULT 0;
ALTER TABLE story_outlines ADD COLUMN main INTEGER NOT NULL DEFAULT 0;
