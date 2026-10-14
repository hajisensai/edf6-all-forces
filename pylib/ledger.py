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

More than one process writes it (the installer's flows, the in-game menu's background generator
tools/loadout_generator.py, which may outlive the game): every writer holds the generator's lock
(loadout_generator.generate_lock / wait_generate_lock) for its whole flow, and every save re-reads the file and merges
this object's own changes since it last read it into what is there now (merge_files): an object loaded before another
process saved never writes that process's entries back out.
"""
from __future__ import annotations

import copy
import json
import os

from modfiles import atomic_write, sha256, sha256_file

MANIFEST = '.edf6vc_files.json'
OWNERS = ('jets', 'sub', 'testrange', 'testrange_sub', 'calls', 'katyusha', 'bigmap', 'artillery', 'chute', 'drill', 'stockstores', 'sidecar', 'emc', 'sazabi', 'proteus', 'optics', 'loadout', 'edf41', 'menu_preview', 'roomui', 'edf41maps', 'edf41sound', 'edf5dlc')
LEGACY = 'legacy'   # a file from before the ledger that a tool needs: its writer is not recorded


def key(rel: str) -> str:
    """The ledger's name for a path under Mods (Windows paths are case-insensitive)."""
    return rel.replace('\\', '/').strip('/').upper()


def _merge_entry(base: dict | None, mine: dict | None, disk: dict | None) -> dict | None:
    """One file's entry: what this object changed since `base` (owners added / removed, the sha rewritten) applied to
    what is on disk now; owners another process added meanwhile are kept."""
    base_owners = list((base or {}).get('owners', []))
    mine_owners = list((mine or {}).get('owners', []))
    added = [o for o in mine_owners if o not in base_owners]
    removed = {o for o in base_owners if o not in mine_owners}
    owners = [o for o in (disk or {}).get('owners', []) if o not in removed]
    owners += [o for o in added if o not in owners]
    if not owners:
        return None
    sha = (mine or {}).get('sha') if mine is not None and (mine or {}).get('sha') != (base or {}).get('sha') else \
        (disk or {}).get('sha', (mine or {}).get('sha'))
    entry = dict(disk or mine or {})
    entry['owners'] = owners
    if sha is not None:
        entry['sha'] = sha
    return entry


def merge_files(base: dict[str, dict], mine: dict[str, dict], disk: dict[str, dict]) -> dict[str, dict]:
    """The ledger to write: `disk` (read just now) with every entry this object changed since `base` (what it last read)
    merged in (_merge_entry); entries it did not touch are disk's as they are."""
    out = copy.deepcopy(disk)
    for k in set(base) | set(mine):
        if base.get(k) == mine.get(k):
            continue
        merged = _merge_entry(base.get(k), mine.get(k), disk.get(k))
        if merged is None:
            out.pop(k, None)
        else:
            out[k] = merged
    return out


class Ledger:
    """The ownership ledger of one game directory. Every change is saved at once, merged into the file as it is then."""

    def __init__(self, game_root: str) -> None:
        self.mods = os.path.join(game_root, 'Mods')
        self.path = os.path.join(self.mods, MANIFEST)
        self.files: dict[str, dict] = self._read()
        self._base: dict[str, dict] = copy.deepcopy(self.files)

    def _read(self) -> dict[str, dict]:
        if not os.path.isfile(self.path):
            return {}
        with open(self.path, encoding='utf-8') as f:
            return json.load(f).get('files', {})

    def _refresh(self) -> None:
        """Before a change: what is on disk now, with this object's unsaved changes (if any) merged on top."""
        disk = self._read()
        self.files = merge_files(self._base, self.files, disk)
        self._base = disk

    def _save(self) -> None:
        """Re-read, merge this object's changes into it, write (atomically) or remove the file when nothing is left."""
        merged = merge_files(self._base, self.files, self._read())
        if merged:
            data = json.dumps({'version': 1, 'files': merged}, indent=1, sort_keys=True).encode('utf-8')
            atomic_write(self.path, data)
        elif os.path.isfile(self.path):
            os.remove(self.path)
        self.files = merged
        self._base = copy.deepcopy(merged)

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
        self._refresh()
        entry = self.files.setdefault(key(rel), {'owners': []})
        if LEGACY in entry['owners']:
            entry['owners'].remove(LEGACY)
        if owner not in entry['owners']:
            entry['owners'].append(owner)
        entry['sha'] = sha256(data)
        self._save()
        return path

    def put_new(self, owner: str, rel: str, data: bytes) -> str | None:
        """Writes `data` only when nothing is at `rel` yet: into a temporary file beside it, then renamed onto the free
        name (os.rename never replaces on Windows), so a running game that opens the name sees no file or the whole one,
        and no file already there is ever overwritten. Records `owner` as with put. Returns the path written, None when
        a file was already there (left as it was)."""
        assert owner in OWNERS, owner
        path = self.disk(rel)
        if os.path.exists(path):
            return None
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = f'{path}.tmp-{os.getpid()}'
        try:
            with open(tmp, 'wb') as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
            os.rename(tmp, path)
        except FileExistsError:   # made meanwhile by someone else: theirs stays
            os.remove(tmp)
            return None
        except BaseException:
            if os.path.exists(tmp):
                os.remove(tmp)
            raise
        self._refresh()
        entry = self.files.setdefault(key(rel), {'owners': []})
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
        self._refresh()
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
        self._refresh()
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
        self._save()
        return deleted, kept
