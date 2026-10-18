"""EDF4.1's objects EDF6 lacks (enemies, NPCs the 4.1 missions name), installed into Mods under 4.1's own names from
the copies converted ahead of time (tools/make_edf41_objects.py -> edf41port/objects.json + edf41port/objects/): no
EDF4.1 is needed on the player's machine. Plan P4 (docs/edf5-weapons-plan.md).

A member of one of these archives that the player's EDF6 already ships byte for byte (the King's five Monster501
textures, the same in EDF6's OBJECT/MONSTER501.MRAB) is not shipped: its object's registry entry names where
('members_from': {archive: {sha256: [EDF6 archive, RAB path, member index, member name]}}, the manifest row's 7th
column, pylib/rabpool.py), and build reads it from the player's game, SHA-256 checked, as tools/edf5_dlc.py does. An
object one of whose members is not there as generated (EDF6 updated, an archive missing) is left out, and said why.

    build(root) -> (files, skipped)    {Mods path: bytes} to write; [(object, why)] left out
    install(root, files) -> paths      written as OWNER's (pylib/ledger.py); what it wrote before and not now released
    remove(root) -> (deleted, kept)

A file of the same name already in Mods that the ledger does not record as ours (another mod's) is never written
over: every object needing it is left out, and said so.
"""
from __future__ import annotations

import hashlib
import json
import os
import struct
import sys
from collections.abc import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import mdb  # noqa: E402
import rabpool  # noqa: E402
from modfiles import sha256, sha256_file  # noqa: E402

OWNER = 'edf41'   # pylib/ledger.py OWNERS
DEPENDENT_OWNERS = frozenset(('testrange', 'testrange_sub'))   # need() consumers, never a second writer
# The registry and the files: bundled next to the frozen installer, else the source tree's.
DATA = sys._MEIPASS if getattr(sys, 'frozen', False) else os.path.join(HERE, '..')  # type: ignore[attr-defined]
REGISTRY = os.path.join(DATA, 'edf41port', 'objects.json')


def bundled(rel: str) -> str:
    """Where the tools ship the file for Mods path `rel`: edf41port/objects, or where the registry's 'shared' says
    (a file another part already ships: EDF5's converted Blacker No.4.1 model, edf5port/assets)."""
    shared = registry().get('shared', {})
    if rel in shared:
        return os.path.join(DATA, *shared[rel].split('/'))
    return os.path.join(DATA, 'edf41port', 'objects', *rel.split('/'))


def _read_file(path: str) -> bytes:
    with open(path, 'rb') as f:
        return f.read()


def read(rel: str, source: Callable[[list], bytes] | None = None) -> bytes:
    """The bytes of Mods path `rel` as the tools ship it: a shared file whole, one of ours through pylib/rabpool.py
    (an archive rebuilt from its manifest and the shared members; a member the player's EDF6 ships through `source`,
    game_source(root): rabpool.Unsourced without one)."""
    reg = registry()
    if rel in reg.get('shared', {}):
        return _read_file(bundled(rel))
    base = os.path.join(DATA, 'edf41port', 'objects')
    return rabpool.unpack(rel, lambda s: _read_file(os.path.join(base, *s.split('/'))), source)


def stock_members(obj: dict) -> dict[str, dict[str, list]]:
    """{archive: {sha256: source}}: the members of the object's archives read from the player's EDF6."""
    return obj.get('members_from', {})


class _Stock:
    """The player's EDF6 as the source of the members not shipped (tools/edf5_dlc.py Edf6: its archives, read on
    demand); each RAB read and taken apart once."""

    def __init__(self, root: str) -> None:
        self.root = root
        self._e6 = None
        self.rabs: dict[tuple[str, str], list] = {}

    @property
    def e6(self):  # noqa: ANN201 - tools/edf5_dlc.py Edf6, opened on first use
        if self._e6 is None:
            import edf5_dlc
            self._e6 = edf5_dlc.Edf6(self.root)
        return self._e6

    def members(self, archive: str, path: str) -> list:
        key = (archive, path.upper())
        if key not in self.rabs:
            self.rabs[key] = mdb.rab_read(self.e6.read(archive, path)).files
        return self.rabs[key]

    def problems(self, sources: dict[str, list]) -> list[str]:
        """Why these members cannot be read from the player's EDF6 as generated: [] when they can."""
        out: list[str] = []
        for sha, (archive, path, index, name) in sorted(sources.items()):
            why = self.e6.missing(archive, path)
            if why is None:
                try:
                    have = self.members(archive, path)
                except (AssertionError, ValueError, IndexError, KeyError, struct.error, UnicodeDecodeError) as e:
                    why = f'EDF6 的 {path} 不再是可读的 RAB（{type(e).__name__}: {e}）'
                else:
                    if index >= len(have) or have[index].name != name or sha256(have[index].stored) != sha:
                        why = f'EDF6 的 {path} 里 {name} 不是生成时的那份（游戏更新过？）'
            if why and why not in out:
                out.append(why)
        return out

    def read(self, src: list) -> bytes:
        archive, path, index, name = src
        f = self.members(archive, path)[index]
        if f.name != name:
            raise ValueError(f'EDF6 {path}: member {index} is {f.name}, not {name}')
        return f.stored


def game_source(root: str) -> Callable[[list], bytes]:
    """The source read() takes for the game at `root`."""
    return _Stock(root).read


def registry() -> dict:
    with open(REGISTRY, encoding='utf-8') as f:
        return json.load(f)


def _not_ours(led: ledger.Ledger, rel: str, sha: str) -> bool:
    """A file in Mods this install must not write over: one no tool of ours wrote (no owner in the ledger), or one
    another of our tools holds with other bytes."""
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    others = [o for o in owners if o != OWNER]
    if not owners:
        import call_weapons
        # Older installers recorded ported models only in the weapon transaction's manifest, not the ledger.
        # Planning must recognize them before the weapon install migrates their ownership later in this run.
        return call_weapons.owned_assets(os.path.dirname(led.mods)).get(ledger.key(rel)) != sha
    if led.changed(rel):
        return sha256_file(led.disk(rel)) != sha
    if OWNER in owners and others and set(others) <= DEPENDENT_OWNERS:
        # The primary writer may upgrade its own unedited object while the range
        # still references it. A dependency is not an independent competing writer.
        return False
    return bool(others) and sha256_file(led.disk(rel)) != sha


def entries(reg: dict) -> list[dict]:
    """Every object the registry installs files for: the ported ones, then the stock SGOs whose missing files it ships
    ('stock_completions': EDF6's Monster501_King without its model archive; the object path is EDF6's own file)."""
    return reg['objects'] + reg.get('stock_completions', [])


def unavailable(root: str) -> list[tuple[str, str]]:
    """The objects build leaves out, [(object, why)]: those one of whose files is in Mods and is not ours (another
    mod's: the ledger knows no owner of it, or another of our tools' with other bytes). The one place that decides it:
    build writes exactly the rest, and the campaign plan (tools/make_edf5_campaign.py plan41) offers only missions
    whose objects are not here."""
    reg = registry()
    led = ledger.Ledger(root)
    stock: _Stock | None = None
    out: list[tuple[str, str]] = []
    for obj in entries(reg):
        why: list[str] = []
        foreign = [rel for rel in obj['files'] if _not_ours(led, rel, reg['files'][rel])]
        if foreign:
            why.append(f"another mod's file in Mods: {', '.join(foreign)}")
        sourced = {sha: src for srcs in stock_members(obj).values() for sha, src in srcs.items()}
        if sourced:
            stock = stock or _Stock(root)
            gone = stock.problems(sourced)
            if gone:
                why.append('它的模型档案有几个成员不随包附带，要从你的 EDF6 里取：' + '；'.join(gone))
        if why:
            out.append((obj['object'], '; '.join(why)))
    return out


def build(root: str, *, material_compat: bool = False) -> tuple[dict[str, bytes], list[tuple[str, str]]]:
    """The files of every registered object (each checked against the registry's SHA-256) but those of the objects
    unavailable() names. One of our tools' files with these very bytes (the Blacker No.4.1 model the EDF5 weapons
    install too) is shared: install records edf41 as needing it as well."""
    reg = registry()
    skipped = unavailable(root)
    left_out = {obj for obj, _ in skipped}
    source = game_source(root)
    files: dict[str, bytes] = {}
    for obj in entries(reg):
        if obj['object'] in left_out:
            continue
        for rel in obj['files']:
            if rel not in files:
                data = read(rel, source) if rel in stock_members(obj) else read(rel)
                if hashlib.sha256(data).hexdigest() != reg['files'][rel]:
                    raise ValueError(f'{rel}: not the file the registry lists (a damaged install of the tools)')
                files[rel] = data
    if material_compat:
        import edf41_materials
        # A shared archive also belongs to an EDF5 weapon: never alter its shader names.
        own = {rel: data for rel, data in files.items() if rel not in reg.get('shared', {})}
        files.update(edf41_materials.apply(own, root))
    return files, skipped


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` as OWNER's, but a file it already holds with these bytes (a second install writes nothing it
    need not: tools/test_installer_incremental.py); what it wrote before and does not now is released."""
    led = ledger.Ledger(root)
    import call_weapons
    for rel, sha in call_weapons.owned_assets(root).items():
        if rel in files:
            led.record(call_weapons.OWNER, rel, sha)
    before = set(led.owned_by(OWNER))
    dependencies: list[tuple[set[str], list[str]]] = []
    for obj in entries(registry()):
        held = set(led.owners(obj['object'])) & DEPENDENT_OWNERS
        if not held or OWNER not in led.owners(obj['object']) or led.changed(obj['object']):
            continue
        required = [rel for rel in obj['files'] if rel in files]
        if obj['object'] not in files:
            continue
        if any(rel.startswith('SHADER/') for rel in files):
            import edf41materials
            private = {rel: files[rel] for rel in required}
            required += [rel for name in edf41materials.needed_names(private)
                         if (rel := edf41materials.shader_path(name)) in files]
        dependencies.append((held, required))
    paths = []
    for rel, data in files.items():
        if ledger.key(rel) in before and sha256_file(led.disk(rel)) == sha256(data):
            continue
        paths.append(led.put(OWNER, rel, data))
    for held, required in dependencies:
        for dependent in sorted(held):
            for rel in required:
                led.need(dependent, rel)
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)
