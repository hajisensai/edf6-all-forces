-- Who started each story outline (/outline.html shows it beside the outline's name, to accounts only).
-- schema.sql already has the column for a new database; run this once on a database made before (ALTER TABLE has
-- no IF NOT EXISTS: a second run fails with "duplicate column", nothing changes). Until someone says otherwise, an
-- outline's author is whoever saved it last.
ALTER TABLE story_outlines ADD COLUMN author TEXT;
UPDATE story_outlines SET author = updated_by WHERE author IS NULL;
