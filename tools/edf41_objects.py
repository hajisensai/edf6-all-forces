"""EDF4.1's objects EDF6 lacks (enemies, NPCs the 4.1 missions name), installed into Mods under 4.1's own names from
the copies converted ahead of time (tools/make_edf41_objects.py -> edf41port/objects.json + edf41port/objects/): no
EDF4.1 is needed on the player's machine. Plan P4 (docs/edf5-weapons-plan.md).

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
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import rabpool  # noqa: E402
from modfiles import sha256, sha256_file  # noqa: E402

OWNER = 'edf41'   # pylib/ledger.py OWNERS
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


def read(rel: str) -> bytes:
    """The bytes of Mods path `rel` as the tools ship it: a shared file whole, one of ours through pylib/rabpool.py
    (an archive rebuilt from its manifest and the shared members)."""
    reg = registry()
    if rel in reg.get('shared', {}):
        return _read_file(bundled(rel))
    base = os.path.join(DEV_DIR, 'objects') if rel in reg.get('dev', ()) else os.path.join(DATA, 'edf41port', 'objects')
    return rabpool.unpack(rel, lambda s: _read_file(os.path.join(base, *s.split('/'))))


# ---- development only: the UfoMother301 prototype (docs/edf41-ufomother301.md) --------------------------------------------
# `python tools/make_edf41_objects.py --mother-dev` writes edf41port_dev/ (git-ignored, not in the installer bundle, which
# names the edf41port files one by one: tools/build_release.py). An install from the source tree with EDF41_MOTHER_DEV=1 in
# its environment adds those objects to the registry (so the campaign lets M016 through, make_edf5_campaign.plan41) and sets
# the plugin's ini Edf41MotherDev=1; without the variable, in the frozen installer, or without the folder, nothing changes:
# the registry is the committed one and the ini key is 0.
DEV_DIR = os.path.join(HERE, '..', 'edf41port_dev')
DEV_ENV = 'EDF41_MOTHER_DEV'
DEV_INI = os.path.join('Mods', 'Plugins', 'EDF6VehicleCrew.ini')
DEV_INI_KEY = 'Edf41MotherDev'


def dev_enabled() -> bool:
    """Whether this run adds the development objects: from the source tree only, the variable set to 1, the folder there."""
    return (not getattr(sys, 'frozen', False) and os.environ.get(DEV_ENV) == '1'
            and os.path.isfile(os.path.join(DEV_DIR, 'objects.json')))


def _dev_registry() -> dict:
    with open(os.path.join(DEV_DIR, 'objects.json'), encoding='utf-8') as f:
        return json.load(f)


def registry() -> dict:
    with open(REGISTRY, encoding='utf-8') as f:
        reg = json.load(f)
    if dev_enabled():
        dev = _dev_registry()
        reg['objects'] = reg['objects'] + dev['objects']
        reg['files'] = {**reg['files'], **dev['files']}
        reg['dev'] = sorted(dev['files'])
    return reg


def set_dev_flag(root: str, on: bool) -> str | None:
    """The plugin's ini Edf41MotherDev set to 1 / 0 (the file is the game's Mods/Plugins one; unrelated lines kept).
    None when there is no ini yet, or the key is not there and `on` is false (nothing to turn off)."""
    import make_edf5_campaign
    import modfiles
    path = os.path.join(root, DEV_INI)
    if not os.path.isfile(path):
        return None
    with open(path, encoding='utf-8-sig') as f:
        text = f.read()
    lines, key, end = make_edf5_campaign.key_setting(text, DEV_INI_KEY)
    if key is None and not on:
        return None
    line = f'{DEV_INI_KEY}={1 if on else 0}'
    if key is None:
        lines.insert(end, line)
    elif lines[key] == line:
        return None
    else:
        lines[key] = line
    modfiles.atomic_write(path, (chr(10).join(lines) + chr(10)).encode('utf-8'))
    return path


def _not_ours(led: ledger.Ledger, rel: str, sha: str) -> bool:
    """A file in Mods this install must not write over: one no tool of ours wrote (no owner in the ledger), or one
    another of our tools holds with other bytes."""
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    others = [o for o in owners if o != OWNER]
    return not owners or (bool(others) and sha256_file(led.disk(rel)) != sha)


def build(root: str) -> tuple[dict[str, bytes], list[tuple[str, str]]]:
    """The files of every registered object (each checked against the registry's SHA-256), but those of an object
    one of whose files is in Mods and is not ours: another mod's (the ledger knows no owner of it), or another of our
    tools' with other bytes. One of our tools' with these very bytes (the Blacker No.4.1 model the EDF5 weapons install
    too) is shared: install records edf41 as needing it as well."""
    reg = registry()
    led = ledger.Ledger(root)
    files: dict[str, bytes] = {}
    skipped: list[tuple[str, str]] = []
    for obj in reg['objects']:
        foreign = [rel for rel in obj['files'] if _not_ours(led, rel, reg['files'][rel])]
        if foreign:
            skipped.append((obj['object'], f"another mod's file in Mods: {', '.join(foreign)}"))
            continue
        for rel in obj['files']:
            if rel not in files:
                data = read(rel)
                if hashlib.sha256(data).hexdigest() != reg['files'][rel]:
                    raise ValueError(f'{rel}: not the file the registry lists (a damaged install of the tools)')
                files[rel] = data
    return files, skipped


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` as OWNER's, but a file it already holds with these bytes (a second install writes nothing it
    need not: tools/test_installer_incremental.py); what it wrote before and does not now is released."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = []
    for rel, data in files.items():
        if ledger.key(rel) in before and sha256_file(led.disk(rel)) == sha256(data):
            continue
        paths.append(led.put(OWNER, rel, data))
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    set_dev_flag(root, False)
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)
