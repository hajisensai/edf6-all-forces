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
A shared file installed over another mod's may carry `restore`: the SHA-256 of that pre-install original, kept under
Mods/.edf6vc_files_backup (defer_restore); the last release puts it back unless the file was changed since.
`writers` separately records each writer's committed hash. Releasing its need does not erase that provenance while
consumers remain; `need` never grants it. A writer can also remember a root's dependency closure, tied to that root's
committed hash, so an upgrade replaces consumer references instead of accumulating obsolete dependencies.
An in-progress dependency replacement retains `previous` paths before any root write; they are cleared only after
all consumer additions and releases succeed, allowing a restarted installer to finish the same reference transfer.

Every write is atomic (pylib/modfiles.py atomic_write): a failed or interrupted write leaves the old file whole.

More than one process writes it (the installer's flows, the in-game menu's background generator
tools/loadout_generator.py, which may outlive the game): every writer holds the generator's lock
(loadout_generator.generate_lock / wait_generate_lock) for its whole flow, and every save re-reads the file and merges
this object's own changes since it last read it into what is there now (merge_files): an object loaded before another
process saved never writes that process's entries back out.
"""
from __future__ import annotations

import json
import os

from modfiles import atomic_write, sha256, sha256_file

MANIFEST = '.edf6vc_files.json'
BACKUP = '.edf6vc_files_backup'
OWNERS = ('jets', 'sub', 'testrange', 'testrange_sub', 'calls', 'katyusha', 'bigmap', 'artillery', 'chute', 'drill', 'stockstores', 'sidecar', 'sedan', 'emc', 'ultimate', 'vulcan', 'bmpt', 'sazabi', 'dreadnought', 'proteus', 'optics', 'loadout', 'edf41', 'menu_preview', 'roomui', 'edf41maps', 'edf41sound', 'edf5dlc', 'edf41menubg', 'edf41outfit')
LEGACY = 'legacy'   # a file from before the ledger that a tool needs: its writer is not recorded


def _copy(value):
    """A deep copy of the ledger's JSON data (dicts, lists, strings, numbers): copy.deepcopy's memo and dispatch made
    the per-file saves of an install (one per put) its slowest step; the ledger holds no shared or cyclic parts."""
    if isinstance(value, dict):
        return {k: _copy(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_copy(v) for v in value]
    return value


def key(rel: str) -> str:
    """The ledger's name for a path under Mods (Windows paths are case-insensitive)."""
    return rel.replace('\\', '/').strip('/').upper()


def _merge_mapping(base: dict, mine: dict, disk: dict) -> dict:
    """Apply only this object's per-key edits, preserving another writer's metadata."""
    out = _copy(disk)
    for name in set(base) | set(mine):
        if base.get(name) == mine.get(name):
            continue
        if name in mine:
            out[name] = _copy(mine[name])
        else:
            out.pop(name, None)
    return out


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
    if (mine or {}).get('restore') != (base or {}).get('restore'):
        if mine and 'restore' in mine:
            entry['restore'] = mine['restore']
        else:
            entry.pop('restore', None)
    entry['owners'] = owners
    if sha is not None:
        entry['sha'] = sha
    for field in ('writers', 'dependencies'):
        merged = _merge_mapping((base or {}).get(field, {}), (mine or {}).get(field, {}),
                                (disk or {}).get(field, {}))
        if merged:
            entry[field] = merged
        else:
            entry.pop(field, None)
    return entry


def merge_files(base: dict[str, dict], mine: dict[str, dict], disk: dict[str, dict]) -> dict[str, dict]:
    """The ledger to write: `disk` (read just now) with every entry this object changed since `base` (what it last read)
    merged in (_merge_entry); entries it did not touch are disk's as they are."""
    out = _copy(disk)
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
        self._base: dict[str, dict] = _copy(self.files)

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
            # One line: json's C encoder runs only without indent, and an install saves this once per file it puts.
            data = json.dumps({'version': 1, 'files': merged}, sort_keys=True).encode('utf-8')
            atomic_write(self.path, data)
        elif os.path.isfile(self.path):
            os.remove(self.path)
        self.files = merged
        self._base = _copy(merged)

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
        entry.setdefault('writers', {})[owner] = entry['sha']
        self._save()
        return path

    def record(self, owner: str, rel: str, sha: str) -> None:
        """Records a writer after its own transaction committed the file, without writing it a second time.
        The caller's committed hash must still match; a legacy dependency becomes a known writer's file."""
        assert owner in OWNERS, owner
        if sha256_file(self.disk(rel)) != sha:
            raise ValueError(f'{key(rel)}: not the committed file')
        self._refresh()
        entry = self.files.setdefault(key(rel), {'owners': []})
        entry['owners'] = [o for o in entry['owners'] if o != LEGACY]
        if owner not in entry['owners']:
            entry['owners'].append(owner)
        entry['sha'] = sha
        entry.setdefault('writers', {})[owner] = sha
        self._save()

    def written_by(self, owner: str, rel: str) -> bool:
        """Exact committed writer proof, including after that writer's owner was released."""
        entry = self.files.get(key(rel), {})
        digest = entry.get('writers', {}).get(owner)
        return digest is not None and digest == entry.get('sha') and sha256_file(self.disk(rel)) == digest

    def dependency_closure(self, owner: str, rel: str) -> set[str] | None:
        """The last committed root's schema; a user edit conservatively retains its previous dependencies."""
        entry = self.files.get(key(rel), {})
        closure = entry.get('dependencies', {}).get(owner)
        if not isinstance(closure, dict) or closure.get('sha') != entry.get('sha'):
            return None
        return {key(path) for path in closure['files']}

    def remember_dependencies(self, owner: str, rel: str, files: list[str]) -> None:
        """Record a writer's complete root closure; a consumer cannot create writer provenance."""
        self._refresh()
        if not self.written_by(owner, rel):
            raise ValueError(f'{key(rel)}: not {owner}\'s committed root')
        entry = self.files[key(rel)]
        closure = {'sha': entry['sha'], 'files': sorted({key(path) for path in files})}
        previous = entry.get('dependencies', {}).get(owner, {}).get('previous')
        if previous:
            closure['previous'] = previous
        if entry.get('dependencies', {}).get(owner) != closure:
            entry.setdefault('dependencies', {})[owner] = closure
            self._save()

    def pending_dependencies(self, owner: str, rel: str) -> set[str]:
        """Previous consumer references retained until their replacement has fully committed."""
        entry = self.files.get(key(rel), {})
        return set(entry.get('dependencies', {}).get(owner, {}).get('previous', ()))

    def stage_dependencies(self, owner: str, rel: str, files: list[str]) -> None:
        """Checkpoint the old graph before any root bytes change, so interruption cannot lose references."""
        self._refresh()
        if not self.written_by(owner, rel):
            raise ValueError(f'{key(rel)}: not {owner}\'s committed root')
        entry = self.files[key(rel)]
        closure = entry.setdefault('dependencies', {}).setdefault(owner, {'sha': entry['sha'], 'files': []})
        closure['previous'] = sorted(set(closure.get('previous', ())) | {key(path) for path in files})
        self._save()

    def complete_dependencies(self, owner: str, rel: str) -> None:
        """Forget the old graph only after every consumer need/release succeeded."""
        self._refresh()
        closure = self.files.get(key(rel), {}).get('dependencies', {}).get(owner, {})
        if closure.pop('previous', None) is not None:
            self._save()

    def defer_restore(self, rel: str, data: bytes) -> None:
        """Keeps the pre-install bytes until the last consumer releases a shared file.
        Its writer may remove its own transaction backups before the remaining consumers are uninstalled.
        Bytes equal to what our tools registered for it are not a pre-install original (a writer that backed up a file
        another of our tools had already written): nothing is kept, and the last release deletes the file."""
        self._refresh()
        entry = self.files[key(rel)]
        if 'restore' in entry or sha256(data) == entry.get('sha'):
            return
        atomic_write(self._backup(key(rel)), data)
        entry['restore'] = sha256(data)
        self._save()

    def restores(self, rel: str) -> bool:
        """The ledger keeps the file's pre-install original: its last release puts that back (writers leave the file
        to it rather than deleting it themselves)."""
        return 'restore' in self.files.get(key(rel), {})

    def ours(self, rel: str) -> bool:
        """The file on disk is what one of our tools registered for it (a known owner, not only LEGACY, and the
        registered bytes): not a pre-install original to back up."""
        entry = self.files.get(key(rel))
        return bool(entry) and bool(set(entry.get('owners', [])) - {LEGACY}) and \
            sha256_file(self.disk(rel)) == entry.get('sha')

    def _backup(self, k: str) -> str:
        return os.path.join(self.mods, BACKUP, *k.split('/'))

    def _drop_backup(self, k: str) -> None:
        """Removes a kept original and the directories left empty under BACKUP (BACKUP itself too)."""
        path = self._backup(k)
        if os.path.isfile(path):
            os.remove(path)
        root = os.path.join(self.mods, BACKUP)
        d = os.path.dirname(path)
        while os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
            if os.path.normcase(d) == os.path.normcase(root):
                break
            d = os.path.dirname(d)

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
        entry.setdefault('writers', {})[owner] = entry['sha']
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
            restore = entry.get('restore')
            if not os.path.isfile(path):
                if restore:
                    self._drop_backup(k)   # removed by someone else since: nothing of ours left to put the original over
                continue
            if sha256_file(path) != entry.get('sha'):
                kept.append(path)   # someone else changed it since: theirs now, the original is not put over it
                if restore:
                    self._drop_backup(k)
                continue
            if restore:
                original = None
                if os.path.isfile(self._backup(k)):
                    with open(self._backup(k), 'rb') as f:
                        original = f.read()
                if original is None or sha256(original) != restore:
                    # Checked before anything is written: the loop goes on and the ledger is saved; our file stays
                    # rather than leaving neither it nor the original.
                    print(f'WARNING: {k}: the kept original ({self._backup(k)}) is '
                          f'{"missing" if original is None else "not what was kept"}; left the installed file as it is')
                    kept.append(path)
                    if original is None:
                        self._drop_backup(k)   # its directories, left empty
                    continue
                atomic_write(path, original)
                self._drop_backup(k)
                kept.append(path)
                continue
            os.remove(path)
            deleted.append(path)
        self._save()
        return deleted, kept
