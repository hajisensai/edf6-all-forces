-- Scores and reviews on the story outline's lines and steps (/outline.html) without touching existing data.
CREATE TABLE IF NOT EXISTS outline_ratings (
  grp TEXT NOT NULL,
  item TEXT NOT NULL,               -- a lane's or a step's id in that group's outline
  author TEXT NOT NULL,
  score INTEGER NOT NULL CHECK (score BETWEEN 1 AND 5),
  body TEXT NOT NULL DEFAULT '',
  at INTEGER NOT NULL,
  PRIMARY KEY (grp, item, author)
);
