"""Who owns the files the tools generate under <game>/Mods, so removing one tool's files never pulls a file
another one still uses (the jet guns are written by make_jets, make_sub and the test range alike; the test
range's jets fly in the models make_jets writes).

The ledger is Mods/.edf6vc_files.json: per file (its path under Mods, upper case) the owners that need it and
the SHA-256 of the bytes last written. An owner is a tool (OWNERS): the one that wrote the file, or one whose
own files refer to it (`need`). `release` drops an owner; a file nobody needs any more is deleted, unless it
no longer holds what was written (someone else changed it: kept, and reported).
Files from before the ledger: one that a tool needs gets the owner LEGACY besides (its writer is not known, so
releasing the need never deletes it); the tool that writes it takes it over when it writes it again (put) or
releases it as its own (release with writer=True), and a file the ledger does not know at all belongs to the
writer that releases it, as it did before the ledger.

Every write is atomic (pylib/modfiles.py atomic_write): a failed or interrupted write leaves the old file whole.
"""
from __future__ import annotations

import json
import os

from modfiles import atomic_write, sha256, sha256_file

MANIFEST = '.edf6vc_files.json'
OWNERS = ('jets', 'sub', 'testrange', 'testrange_sub', 'calls', 'katyusha', 'bigmap', 'artillery', 'chute', 'drill', 'stockstores', 'sidecar')
LEGACY = 'legacy'   # a file from before the ledger that a tool needs: its writer is not recorded


def key(rel: str) -> str:
    """The ledger's name for a path under Mods (Windows paths are case-insensitive)."""
    return rel.replace('\\', '/').strip('/').upper()


class Ledger:
    """The ownership ledger of one game directory. Every change is saved at once."""

    def __init__(self, game_root: str) -> None:
        self.mods = os.path.join(game_root, 'Mods')
        self.path = os.path.join(self.mods, MANIFEST)
        self.files: dict[str, dict] = {}
        if os.path.isfile(self.path):
            with open(self.path, encoding='utf-8') as f:
                self.files = json.load(f).get('files', {})

    def _save(self) -> None:
        data = json.dumps({'version': 1, 'files': self.files}, indent=1, sort_keys=True).encode('utf-8')
        atomic_write(self.path, data)

    def disk(self, rel: str) -> str:
        return os.path.join(self.mods, *key(rel).split('/'))

    def owners(self, rel: str) -> list[str]:
        return list(self.files.get(key(rel), {}).get('owners', []))

    def changed(self, rel: str) -> bool:
        """The file is not what was last written into it (someone else rewrote it since)."""
        entry = self.files.get(key(rel))
        return bool(entry) and sha256_file(self.disk(rel)) not in (None, entry.get('sha'))

    def put(self, owner: str, rel: str, data: bytes) -> str:
        """Writes `data` (atomically) and records `owner` as needing it; returns the path written."""
        assert owner in OWNERS, owner
        path = self.disk(rel)
        atomic_write(path, data)
        entry = self.files.setdefault(key(rel), {'owners': []})
        if LEGACY in entry['owners']:
            entry['owners'].remove(LEGACY)
        if owner not in entry['owners']:
            entry['owners'].append(owner)
        entry['sha'] = sha256(data)
        self._save()
        return path

    def need(self, owner: str, rel: str) -> None:
        """Records that `owner`'s own files refer to `rel`, which must exist (written by another owner)."""
        assert owner in OWNERS, owner
        path = self.disk(rel)
        if not os.path.isfile(path):
            raise FileNotFoundError(f'{key(rel)} is not installed')
        entry = self.files.setdefault(key(rel), {'owners': [LEGACY], 'sha': sha256_file(path)})
        if owner not in entry['owners']:
            entry['owners'].append(owner)
            self._save()

    def owned_by(self, owner: str) -> list[str]:
        return sorted(k for k, e in self.files.items() if owner in e.get('owners', []))

    def release(self, owner: str, rels: list[str] | None = None, writer: bool = False) -> tuple[list[str], list[str]]:
        """Drops `owner` from `rels` (default: every file it holds); `writer`: `owner` is the tool that writes
        them, so they are its own even where the ledger does not say so (from before it). Returns (deleted paths,
        paths kept although nobody needs them any more because someone else changed them)."""
        deleted: list[str] = []
        kept: list[str] = []
        for k in [key(r) for r in rels] if rels is not None else self.owned_by(owner):
            path = self.disk(k)
            entry = self.files.get(k)
            if entry is None:   # from before the ledger: its writer's alone, as it was
                if writer and os.path.isfile(path):
                    os.remove(path)
                    deleted.append(path)
                continue
            for gone in (owner, LEGACY) if writer else (owner,):
                if gone in entry['owners']:
                    entry['owners'].remove(gone)
            if entry['owners']:
                continue
            del self.files[k]
            if not os.path.isfile(path):
                continue
            if sha256_file(path) != entry.get('sha'):
                kept.append(path)
                continue
            os.remove(path)
            deleted.append(path)
        if self.files:
            self._save()
        elif os.path.isfile(self.path):
            os.remove(self.path)
        return deleted, kept
