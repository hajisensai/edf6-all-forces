"""EDF4.1's community Simplified Chinese patch (地球防卫军4.1简中汉化补丁整合包) as the source of the 4.1 port's Chinese
texts: the developer tools (tools/make_edf41_missions.py, tools/make_edf41_weapons.py) read it from a patched 4.1's
mods folder (<EDF4.1>/mods, or --zh DIR: the patch's own mods folder) and the outputs record which files they used.

The patch is a mods folder 4.1's mod loader reads over the game's files. What the tools take from it:
  mission/MISSIONTEXT.SGO, <app id>/mission/MISSIONTEXT.SGO   the story's and the two DLC packs' mission texts: 4.1's
      rows [JA title, JA brief, EN title, EN brief, ...] with the EN pair in Chinese, the JA pair 4.1's own (checked
      row by row, so the rows are the missions 4.1 lists in that order: a patch for another version is refused)
  weapon/<file>.SGO   a weapon with its 'name' [JA, EN, JA] the EN in Chinese (its JA checked against 4.1's file; the
      patch's weapon/_WEAPONTEXT.SGO is not used: it puts Chinese in the JA columns as well, so nothing ties its rows
      to 4.1's but their order)
The patch is Simplified Chinese (EDF6's SC); EDF6's CN is Traditional, made from it with OpenCC (s2tw) and the forms
EDF6's own Traditional texts use where s2tw writes another (HOUSE: 鎗 for 槍, 砲 for 炮 ...). Without OpenCC
(pip install opencc-python-reimplemented) traditional() gives None and the tools keep CN as before.
"""
from __future__ import annotations

import functools
import hashlib
import os
import re

import sgo

PATCH = '4.1 简中汉化补丁 (EDF4.1 Simplified Chinese community patch)'
CJK = re.compile(r'[㐀-鿿]')
# s2tw's output -> EDF6's own Traditional form: found by putting EDF6's SC weapon texts through s2tw and comparing them
# with its CN ones (槍 never appears in EDF6's CN, 鎗 960 times; 炮 once, 砲 372 times; ...).
HOUSE: tuple[tuple[str, str], ...] = (
    ('槍', '鎗'), ('炮', '砲'), ('錘', '鎚'), ('粘', '黏'), ('臺', '台'), ('巖', '岩'), ('·', '‧'),
    ('回覆', '回復'), ('激光', '雷射'), ('計劃', '計畫'), ('鼠標', '滑鼠'), ('“', '「'), ('”', '」'),
)


def default_dir(edf41: str) -> str:
    """Where a patched 4.1 has the patch: its mods folder."""
    return os.path.join(edf41, 'mods')


def locate(zh: str, *parts: str) -> str:
    """The file at zh/parts..., matched without regard to case (the patch writes 411380/mission/MissionText.SGO beside
    410780/mission/MISSIONTEXT.SGO); SystemExit naming it when the patch has none."""
    at = zh
    for part in parts:
        try:
            names = os.listdir(at)
        except OSError:
            names = []
        hit = next((n for n in names if n.upper() == part.upper()), None)
        if hit is None:
            raise SystemExit(f'the Chinese patch has no {"/".join(parts)} in {zh} (pass --zh <the patch\'s mods folder>)')
        at = os.path.join(at, hit)
    return at


def sha(path: str) -> str:
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def note(zh: str, path: str) -> str:
    """How a source records a patch file: its path under the mods folder and its sha256 (no machine's folder)."""
    rel = os.path.relpath(path, zh).replace(os.sep, '/')
    return f'mods/{rel} (sha256 {sha(path)[:16]})'


@functools.lru_cache(maxsize=1)
def _opencc() -> object | None:
    try:
        import opencc
    except ImportError:
        return None
    return opencc.OpenCC('s2tw')


def available() -> bool:
    """Whether traditional() can convert (OpenCC is installed)."""
    return _opencc() is not None


def traditional(text: str) -> str | None:
    """Simplified Chinese as EDF6's Traditional (OpenCC s2tw, then HOUSE); None without OpenCC."""
    cc = _opencc()
    if cc is None:
        return None
    out = cc.convert(text)
    for a, b in HOUSE:
        out = out.replace(a, b)
    return out


def merge_rows(orig: list, patch: list, where: str) -> list[tuple[str, str]]:
    """(title, brief) in Chinese per row of a 4.1 MISSIONTEXT table `orig` from the patch's `patch`: the same rows,
    checked by their JA pair (columns 0, 1) row by row; every Chinese title must be Chinese. SystemExit on a mismatch."""
    if len(orig) != len(patch):
        raise SystemExit(f'{where}: 4.1 has {len(orig)} mission texts, the Chinese patch {len(patch)}')
    out = []
    for i, (o, p) in enumerate(zip(orig, patch)):
        if len(p) < 4 or list(o[:2]) != list(p[:2]):
            raise SystemExit(f'{where}: row {i} is not 4.1\'s ({o[0]!r} / patch {p[0]!r}): a patch of another version')
        title, brief = p[2].strip(), p[3]
        if not CJK.search(title):
            raise SystemExit(f'{where}: row {i} ({o[0]!r}) has no Chinese title in the patch: {title!r}')
        out.append((title, brief))
    return out


def mission_texts(zh: str, orig: list, app_id: int | None = None) -> tuple[list[tuple[str, str]], str]:
    """The patch's mission texts for 4.1's MISSIONTEXT table `orig` (the story's, or DLC pack `app_id`'s), and the
    note of the file they came from."""
    parts = ((str(app_id),) if app_id else ()) + ('mission', 'MISSIONTEXT.SGO')
    path = locate(zh, *parts)
    with open(path, 'rb') as f:
        patch = sgo.plain(sgo.read(f.read())[1])['table']
    return merge_rows(orig, patch, '/'.join(parts)), note(zh, path)


class WeaponNames:
    """The patch's Chinese weapon names: weapon/<file>.SGO's 'name'[1], for a file whose 'name'[0] is 4.1's (the patch
    rewrites one decoy, aDecoyShooter02, into another: its name is not taken)."""

    def __init__(self, zh: str) -> None:
        self.zh = zh
        self.folder = locate(zh, 'weapon')
        self.files = {n.upper(): n for n in os.listdir(self.folder) if n.upper().endswith('.SGO')}
        self.used: dict[str, str] = {}   # file (upper) -> sha256 of the patch's
        self.refused: list[str] = []

    def sc(self, file: str, ja: str) -> str | None:
        """The Simplified Chinese name of 4.1's weapon/<file> (whose own 'name'[0] is `ja`), None if the patch has
        none for it (the patch leaves the model-code names, AF14 ..., untouched: no file)."""
        n = self.files.get(file.upper())
        if n is None:
            return None
        path = os.path.join(self.folder, n)
        with open(path, 'rb') as f:
            data = f.read()
        name = sgo.plain(sgo.read(data)[1]).get('name')
        if not isinstance(name, list) or len(name) < 2 or name[0] != ja or not CJK.search(str(name[1])):
            self.refused.append(file)
            return None
        self.used[file.upper()] = hashlib.sha256(data).hexdigest()
        return str(name[1]).strip()

    def note(self) -> str:
        """The weapon files the names came from, as one hash over (file, sha256) sorted."""
        h = hashlib.sha256(''.join(f'{k}:{v}\n' for k, v in sorted(self.used.items())).encode()).hexdigest()
        return f"mods/weapon/*.SGO 'name'[1] ({len(self.used)} files, sha256 of the list {h[:16]})"
