-- 2026-10-12: the developer's handling status of each report (a column on the page: 待处理 / 处理中 / 已修待确认 /
-- 已完成 / 不处理). schema.sql already has these columns for a new database; run this once on a database made
-- before (ALTER TABLE has no IF NOT EXISTS: a second run fails with "duplicate column", nothing changes).
ALTER TABLE reports ADD COLUMN status TEXT NOT NULL DEFAULT 'open';
ALTER TABLE reports ADD COLUMN status_at INTEGER;
