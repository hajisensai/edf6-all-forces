"""EDF4.1's player soldiers as extra outfits, installed into Mods from the copies converted ahead of time
(tools/make_edf41_outfits.py -> edf41port/outfits.json + edf41port/outfits/): no EDF4.1 is needed on the player's
machine. New file names only (OBJECT/EDF41_P_<CLASS>.SGO / .MRAB, MENUOBJECT/EDF41_P_<CLASS>.SGO): no stock file is
replaced, so a player without them (or without the plugin) sees and loads exactly what they did; the plugin
(src/outfit41.cpp) hands the game these paths only for the local player who picked the EDF4.1 outfit, and only when
the class's two SGOs are in Mods.

    build(root) -> (files, skipped)    {Mods path: bytes} to write; [(class, why)] left out
    install(root, files) -> paths      written as OWNER's (pylib/ledger.py); what it wrote before and not now released
    remove(root) -> (deleted, kept)

A file of the same name in Mods that the ledger does not record as ours (another mod's) is never written over: the
class needing it is left out, and said so.
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

OWNER = 'edf41outfit'   # pylib/ledger.py OWNERS
# The outfit popup's item that turns these outfits on, as the player reads it (src/hudtext.inc outfit41Item, its
# Simplified Chinese): what the installer names. tools/selftest.py edf41_outfit_item_label holds the two to one text.
ITEM_ZH = '绝望阴影再袭'
DATA = sys._MEIPASS if getattr(sys, 'frozen', False) else os.path.join(HERE, '..')  # type: ignore[attr-defined]
REGISTRY = os.path.join(DATA, 'edf41port', 'outfits.json')
FILES = os.path.join(DATA, 'edf41port', 'outfits')


def registry() -> dict:
    with open(REGISTRY, encoding='utf-8') as f:
        return json.load(f)


def _read_file(path: str) -> bytes:
    with open(path, 'rb') as f:
        return f.read()


def read(rel: str) -> bytes:
    """The bytes of Mods path `rel` as shipped (an archive rebuilt from its manifest and the shared members)."""
    return rabpool.unpack(rel, lambda s: _read_file(os.path.join(FILES, *s.split('/'))))


def _foreign(led: ledger.Ledger, rel: str) -> bool:
    """A file in Mods at `rel` that no tool of ours holds (another mod's)."""
    return os.path.exists(led.disk(rel)) and not led.owners(rel)


def build(root: str) -> tuple[dict[str, bytes], list[tuple[str, str]]]:
    """Every class's files (each checked against the registry's SHA-256), but those of a class one of whose names
    another mod already uses in Mods."""
    reg = registry()
    led = ledger.Ledger(root)
    files: dict[str, bytes] = {}
    skipped: list[tuple[str, str]] = []
    for c in reg['classes']:
        foreign = [rel for rel in c['files'] if _foreign(led, rel)]
        if foreign:
            skipped.append((c['class'], f"another mod's file in Mods: {', '.join(foreign)}"))
            continue
        for rel in c['files']:
            data = read(rel)
            if hashlib.sha256(data).hexdigest() != reg['files'][rel]:
                raise ValueError(f'{rel}: not the file the registry lists (a damaged install of the tools)')
            files[rel] = data
    return files, skipped


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` as OWNER's, but one it already holds with these bytes; what it wrote before and does not now is
    released."""
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
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)
