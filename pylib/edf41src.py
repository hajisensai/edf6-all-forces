"""Where the tools read EDF4.1 from at install time: the player's own EDF4.1, or an EDF4.1 asset pack.

Two parts of the install convert EDF4.1's files into EDF6's Mods (tools/edf41_maps.py: the 16 mission maps,
tools/edf41_sounds.py: the weapon sounds, the voices and their subtitle table). A player without EDF4.1 can have them
from an asset pack instead: a folder holding exactly the EDF4.1 files those two read, as EDF4.1 lays them out, and a
manifest (MANIFEST) with each one's size and SHA-256. tools/export_edf41_assets.py makes it from an EDF4.1 install
(the share package carries it next to the installer, 2026-10-17 user: players without 4.1 get its maps and sounds too).

    <pack>/EDF41ASSETS.json            {'format': FORMAT, 'kind': KIND, 'files': {rel: {'size', 'sha256', 'source'}}}
    <pack>/MAP/<MAP>.MAC|MAD|RAB       the map files, unpacked from 4.1's Root.cpk / Chunk0x.cpk / DX11.cpk
    <pack>/SOUND/PC/...                4.1's loose sound files, as they are
    <pack>/mods/Plugins/subtitles*.txt the subtitle plugin's text, when the exporting 4.1 had it

`source` is what the converters fingerprint the file by in EDF4.1 itself (a MAP file: the archive's [unpacked size,
CRC]; a loose file: [size, mtime ns]): a map or sound group converted from EDF4.1 is current from the pack made of the
same files, and the other way round, so a change of source converts nothing again (the stamps stay as they are).

Which one (find): $EDF41_DIR (an EDF4.1), $EDF41_ASSETS (a pack), the player's EDF4.1 (gamedir.find_other), the pack
`edf41` next to the installer (the frozen exe's folder; the repository root when run from source). An EDF4.1 found is
used before a pack beside the installer: the player's own files are the ones their game has.

A pack's file is used only as the manifest records it: missing, another size (problems, cheap: before converting) or
another SHA-256 (verify / read, when converting) leaves out the map or sound group that needs it, said why; a file under
the pack's folders that the manifest does not list is a fault too (a half-replaced pack). Nothing is half written.
"""
from __future__ import annotations

import hashlib
import json
import os
import sys
from functools import lru_cache

import gamedir

MANIFEST = 'EDF41ASSETS.json'
KIND = 'edf41-assets'
FORMAT = 1
ENV = 'EDF41_ASSETS'
FOLDER = 'edf41'          # next to the installer
TOP = ('MAP', 'SOUND', 'mods')   # the folders a pack's files are in


def is_pack(path: str | None) -> bool:
    return bool(path) and os.path.isfile(os.path.join(path, MANIFEST))


def beside_installer() -> str:
    """`edf41` next to the installer: the frozen exe's folder in a release, the repository root otherwise (as
    pylib/obj_model.py model_roots does for models/)."""
    here = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else \
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    return os.path.normpath(os.path.join(here, FOLDER))


def _is_edf41(path: str | None) -> bool:
    _folder, exe, _var = gamedir.EDF41
    return bool(path) and all(os.path.isfile(os.path.join(path, n)) for n in (exe, 'Root.cpk'))


def find(near: str | None = None) -> str | None:
    """Where to read EDF4.1 from (the order in the module doc), None when neither is here."""
    env41 = os.environ.get(gamedir.EDF41[2], '')
    if _is_edf41(env41):
        return os.path.normpath(env41)
    env = os.environ.get(ENV, '')
    if is_pack(env):
        return os.path.normpath(env)
    real = gamedir.find_other(gamedir.EDF41, near=near)
    if real:
        return real
    beside = beside_installer()
    return beside if is_pack(beside) else None


def describe(root41: str | None) -> str:
    """The source in words, for the installer's messages."""
    if not root41:
        return f'没有找到 EDF4.1，也没有 EDF4.1 素材包（安装器旁的 {FOLDER} 文件夹）'
    return f'EDF4.1 素材包 {root41}' if is_pack(root41) else f'已安装的 EDF4.1 {root41}'


def key(rel: str) -> str:
    return rel.replace('\\', '/').upper()


class Pack:
    """A pack's manifest. ValueError when it is not one this version reads."""

    def __init__(self, root: str) -> None:
        self.root = root
        try:
            with open(os.path.join(root, MANIFEST), encoding='utf-8') as f:
                doc = json.load(f)
        except (OSError, ValueError) as e:
            raise ValueError(f'素材包清单 {MANIFEST} 读不了：{e}') from e
        if not isinstance(doc, dict) or doc.get('kind') != KIND or doc.get('format') != FORMAT \
                or not isinstance(doc.get('files'), dict):
            raise ValueError(f'素材包清单 {MANIFEST} 不是这个版本认识的格式（kind {KIND}，format {FORMAT}）')
        self.files: dict[str, dict] = {key(rel): e for rel, e in doc['files'].items()}
        self.names: dict[str, str] = {key(rel): rel for rel in doc['files']}

    def path(self, rel: str) -> str:
        return os.path.join(self.root, *self.names.get(key(rel), rel).split('/'))

    def problem(self, rel: str) -> str | None:
        """Why the file `rel` cannot be used, by its size (cheap); None when it can, as far as that tells."""
        entry = self.files.get(key(rel))
        if entry is None:
            return f'素材包清单里没有 {rel}'
        path = self.path(rel)
        if not os.path.isfile(path):
            return f'素材包缺少 {rel}'
        if os.path.getsize(path) != entry['size']:
            return f'素材包里的 {rel} 大小和清单不符（被改过或没复制完）'
        return None

    def verify(self, rel: str) -> str | None:
        """problem(), then the SHA-256 (read in pieces: the voice banks are 800 MB)."""
        why = self.problem(rel)
        if why:
            return why
        h = hashlib.sha256()
        with open(self.path(rel), 'rb') as f:
            for block in iter(lambda: f.read(1 << 22), b''):
                h.update(block)
        return None if h.hexdigest() == self.files[key(rel)]['sha256'] else f'素材包里的 {rel} 内容和清单不符（被改过）'

    def read(self, rel: str) -> bytes:
        """The file's bytes as the manifest records them: KeyError when it does not list it, ValueError otherwise."""
        if key(rel) not in self.files:
            raise KeyError(rel)
        why = self.problem(rel)
        if why:
            raise ValueError(why)
        with open(self.path(rel), 'rb') as f:
            data = f.read()
        if hashlib.sha256(data).hexdigest() != self.files[key(rel)]['sha256']:
            raise ValueError(f'素材包里的 {rel} 内容和清单不符（被改过）')
        return data

    def source(self, rel: str) -> list[int]:
        """The file's fingerprint in EDF4.1 (the module doc). KeyError when the manifest does not list it."""
        return list(self.files[key(rel)]['source'])

    def unlisted(self) -> list[str]:
        """Files under the pack's folders the manifest does not list."""
        out = []
        for top in TOP:
            for d, _dirs, names in os.walk(os.path.join(self.root, top)):
                for n in names:
                    rel = os.path.relpath(os.path.join(d, n), self.root).replace(os.sep, '/')
                    if key(rel) not in self.files:
                        out.append(rel)
        return sorted(out)


@lru_cache(maxsize=8)
def _load(root: str, stamp: tuple[int, int]) -> Pack:
    return Pack(root)


def pack(root: str) -> Pack:
    """The pack at `root` (read again when its manifest changes). ValueError as Pack."""
    try:
        st = os.stat(os.path.join(root, MANIFEST))
    except OSError as e:
        raise ValueError(f'素材包清单 {MANIFEST} 读不了：{e}') from e
    return _load(os.path.normpath(root), (st.st_size, st.st_mtime_ns))


def problems(root41: str | None, rels: list[str], optional: bool = False) -> list[str]:
    """Why the files `rels` of the source cannot be used ([] for an EDF4.1: its own install says, as before). With
    `optional`, a file neither listed nor on disk is no fault (the subtitle text: the cue names stand in)."""
    if not is_pack(root41):
        return []
    try:
        p = pack(root41)
    except ValueError as e:
        return [str(e)]
    out = []
    for rel in rels:
        if optional and key(rel) not in p.files and not os.path.exists(p.path(rel)):
            continue
        why = p.problem(rel)
        if why:
            out.append(why)
    return out


def check(root41: str, full: bool = False) -> list[str]:
    """Every fault of the pack at `root41`: unreadable manifest, files missing or of another size (another SHA-256
    too with `full`), files it does not list."""
    try:
        p = pack(root41)
    except ValueError as e:
        return [str(e)]
    out = [why for rel in p.names.values() for why in [p.verify(rel) if full else p.problem(rel)] if why]
    return out + [f'素材包里有清单没列的文件 {rel}' for rel in p.unlisted()]
