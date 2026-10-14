"""EDF5 Mission Pack 1's own content EDF6 lacks (two maps, the DLC fortress), installed into Mods under EDF5's names from
what tools/make_edf5_dlc.py pre-generated (edf5port/dlc/): no EDF5 is needed on the player's machine. Without it four
of the pack's missions cannot load: DM015 (OBJECT/E510_DLC_FORTRESS.SGO), DM018 / DM020 (MAP/IG_DESERT508.MAC),
DM019 (MAP/NG_DLC_EDFROOM03.MAC).

    build(root) -> (files, skipped)    files: {Mods path: bytes}, assembled when read; skipped: [(group, why)]
    install(root, files) -> paths      written as OWNER's (pylib/ledger.py); what it wrote before and not now released
    remove(root) -> (deleted, kept)
    unavailable(root) -> [(group, why)]
    offered(root) -> (map files, object files, {group: why})   for the campaign planner (make_edf5_campaign.plan)

The data (edf5port/dlc/, make_edf5_dlc.py writes it):
  registry.json       groups (a map by its name, the fortress by its root SGO: their Mods files, the missions they serve,
                      the EDF6 files they need), every file's final SHA-256 and where its bytes come from, the blobs
  <rel>.members.json  a RAB as pylib/rabpool.py stores one, each row [name, folder, flag, unk0, unk1, sha256, source]:
                      source null = the blob store, else [archive, RAB path, member index, member name] in the
                      player's EDF6 (the members EDF6 already ships are not committed), read and SHA-checked here
  blobs/<sha256>.<codec>.<NNN>   every other stored byte string (a whole file, a RAB member), once however many files
                      hold it, compressed (codec xz, or raw where that is not smaller), split under 50 MB per part
A file of kind 'edf6' is a whole file of the player's EDF6 (a donor map's weather and navmesh: tools/make_edf5_dlc.py).

A group is left out, with the reason, when a file of it must be written and one of its EDF6 sources is missing or holds
other bytes (EDF6 updated: SHA-256 checked), when an EDF6 file it needs is gone, or when a file of the same name is in
Mods and is not ours (another mod's: never written over). A file already in Mods as ours with its final bytes needs no
source. The one place that decides it: build writes exactly the rest, and the campaign plan offers only the missions
whose groups are not left out.
"""
from __future__ import annotations

import hashlib
import json
import lzma
import os
import struct
import sys
from collections.abc import Callable, Iterator, Mapping

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import mdb  # noqa: E402
import rootcpk  # noqa: E402
from modfiles import sha256  # noqa: E402

OWNER = 'edf5dlc'   # pylib/ledger.py OWNERS
# The data: bundled next to the frozen installer (tools/build_release.py), else the source tree's.
DATA = sys._MEIPASS if getattr(sys, 'frozen', False) else os.path.join(HERE, '..')  # type: ignore[attr-defined]
DLC = os.path.join(DATA, 'edf5port', 'dlc')
REGISTRY = 'registry.json'
MEMBERS = '.members.json'
BLOBS = 'blobs'
PART = 45 * 1024 * 1024   # a blob part's largest size: every committed file under 50 MB
ARCHIVES = ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk', 'DX11.cpk')


# ------------------------------------------------------------------------------------------- the stored data
def blob_name(sha: str, codec: str, part: int) -> str:
    return f'{BLOBS}/{sha}.{codec}.{part:03d}'


def pack_blob(data: bytes) -> tuple[dict, dict[str, bytes]]:
    """`data` as the store keeps it: ({'size', 'codec', 'parts'}, {stored path: bytes}). xz unless that is not smaller."""
    sha = sha256(data)
    packed = lzma.compress(data, preset=6)
    codec, body = ('xz', packed) if len(packed) < len(data) else ('raw', data)
    parts = [body[i:i + PART] for i in range(0, len(body), PART)] or [b'']
    return ({'size': len(data), 'codec': codec, 'parts': len(parts)},
            {blob_name(sha, codec, i): p for i, p in enumerate(parts)})


def unpack_blob(sha: str, entry: dict, read: Callable[[str], bytes]) -> bytes:
    """A blob's bytes from its stored parts; ValueError when they are not the bytes its name says."""
    body = b''.join(read(blob_name(sha, entry['codec'], i)) for i in range(entry['parts']))
    data = lzma.decompress(body) if entry['codec'] == 'xz' else body
    if len(data) != entry['size'] or sha256(data) != sha:
        raise ValueError(f'blob {sha}: the stored parts do not give its bytes (a damaged install of the tools)')
    return data


def pack(groups: dict[str, dict], made: Mapping[str, bytes], whole: Mapping[str, dict],
         sources: Mapping[str, list], notes: list[str] | None = None) -> tuple[dict[str, bytes], dict[str, int]]:
    """The data to commit ({path under DLC: bytes}, {'shared', 'own'}: RAB member bytes read from EDF6 / stored) from
    `made` (Mods path -> the final bytes this data holds), `whole` (Mods path -> its 'edf6' entry) and `sources` (a
    member's SHA-256 -> [archive, RAB path, index, name] of the same stored bytes in EDF6). RAB members with a source
    are not stored. ValueError for an archive mdb.rab_write would not lay out again byte for byte."""
    files: dict[str, dict] = dict(whole)
    blobs: dict[str, dict] = {}
    stored: dict[str, bytes] = {}
    stats = {'shared': 0, 'own': 0}

    def keep(data: bytes) -> str:
        sha = sha256(data)
        if sha not in blobs:
            blobs[sha], parts = pack_blob(data)
            stored.update(parts)
        return sha

    for rel, data in made.items():
        if not rel.upper().endswith('.RAB'):
            files[rel] = {'kind': 'blob', 'sha': keep(data), 'size': len(data)}
            continue
        rab = mdb.rab_read(data)
        if mdb.rab_write(rab) != data:
            raise ValueError(f'{rel}: mdb.rab_write does not rebuild it byte for byte')
        rows = []
        for f in rab.files:
            sha = sha256(f.stored)
            src = sources.get(sha)
            stats['shared' if src else 'own'] += len(f.stored)
            if not src:
                keep(f.stored)
            rows.append([f.name, f.folder, f.flag, f.unk[0], f.unk[1], sha, list(src) if src else None])
        m = {'version': rab.version, 'folders': rab.folders, 'files': rows}
        stored[rel + MEMBERS] = (json.dumps(m, ensure_ascii=False, indent=0) + '\n').encode('utf-8')
        files[rel] = {'kind': 'rab', 'sha': sha256(data), 'size': len(data)}
    missing = sorted(rel for g in groups.values() for rel in g['files'] if rel not in files)
    if missing:
        raise ValueError(f'group files with no data: {missing}')
    reg = {'version': 1, 'groups': groups, 'files': dict(sorted(files.items())), 'blobs': dict(sorted(blobs.items())),
           'notes': list(notes or [])}
    stored[REGISTRY] = (json.dumps(reg, ensure_ascii=False, indent=1) + '\n').encode('utf-8')
    return stored, stats


def _read_data(rel: str) -> bytes:
    with open(os.path.join(DLC, *rel.split('/')), 'rb') as f:
        return f.read()


def registry() -> dict:
    """The registry; {} when the data is not there (not generated: nothing is offered)."""
    path = os.path.join(DLC, REGISTRY)
    if not os.path.isfile(path):
        return {}
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def manifest(rel: str) -> dict:
    """RAB `rel`'s member manifest (rabpool's layout, with a source column)."""
    return json.loads(_read_data(rel + MEMBERS).decode('utf-8'))


# ------------------------------------------------------------------------------------------- the player's EDF6
class Edf6:
    """The player's EDF6 archives, read on demand (nothing written). An archive that is missing or unreadable is
    recorded with why: whatever needs it is unavailable, said so."""

    def __init__(self, root: str) -> None:
        self.games: dict[str, rootcpk.Game] = {}
        self.broken: dict[str, str] = {}
        self.where: dict[str, str] = {}   # Mods-style path (upper case) -> the first archive holding it
        self.keys: dict[str, dict[str, tuple[str, str]]] = {}   # archive -> {upper-case path: its index key}
        for name in ARCHIVES:
            path = os.path.join(root, name)
            if not os.path.isfile(path):
                self.broken[name] = f'没有 {name}'
                continue
            try:
                game = rootcpk.Game(root, name)
            except (OSError, ValueError, struct.error) as e:
                self.broken[name] = f'{name} 读不了（{type(e).__name__}: {e}）'
                continue
            self.games[name] = game
            self.keys[name] = {f'{d}/{n}'.upper(): (d, n) for d, n in game.cpk.index}
            for rel in self.keys[name]:
                self.where.setdefault(rel, name)

    def missing(self, archive: str, rel: str) -> str | None:
        """Why `rel` cannot be read from `archive`, None when it can."""
        if archive in self.broken:
            return self.broken[archive]
        if archive not in self.games:
            return f'没有 {archive}'
        if rel.upper() not in self.keys[archive]:
            return f'EDF6 的 {archive} 里没有 {rel}'
        return None

    def read(self, archive: str, rel: str) -> bytes:
        return self.games[archive].read(*self.keys[archive][rel.upper()])

    def stamp(self, archive: str) -> tuple[str, int, int]:
        path = self.games[archive].cpk.path
        st = os.stat(path)
        return path, st.st_mtime_ns, st.st_size


# What was read and hashed in this process, by the archive file's identity: the plan and the install of one installer
# run read each source once (the archives are hundreds of MB).
_FILE_SHA: dict[tuple, str] = {}
_MEMBER_SHA: dict[tuple, list[tuple[str, str]]] = {}


def _file_sha(e6: Edf6, archive: str, rel: str) -> str:
    key = (*e6.stamp(archive), rel.upper())
    if key not in _FILE_SHA:
        _FILE_SHA[key] = sha256(e6.read(archive, rel))
    return _FILE_SHA[key]


def _member_shas(e6: Edf6, archive: str, rel: str) -> list[tuple[str, str]]:
    """[(name, sha256 of the stored bytes)] of RAB `rel`'s members, in order."""
    key = (*e6.stamp(archive), rel.upper())
    if key not in _MEMBER_SHA:
        rab = mdb.rab_read(e6.read(archive, rel))
        _MEMBER_SHA[key] = [(f.name, sha256(f.stored)) for f in rab.files]
    return _MEMBER_SHA[key]


def _installed_sha(path: str) -> str | None:
    """SHA-256 of a file in Mods (cached by its stat: the installed maps are hundreds of MB)."""
    if not os.path.isfile(path):
        return None
    st = os.stat(path)
    key = (os.path.normcase(os.path.abspath(path)), st.st_mtime_ns, st.st_size)
    if key not in _FILE_SHA:
        with open(path, 'rb') as f:
            _FILE_SHA[key] = sha256(f.read())
    return _FILE_SHA[key]


def source_problems(e6: Edf6, rel: str, entry: dict) -> list[str]:
    """Why file `rel` (registry entry) cannot be put together from the player's EDF6 and the store: [] when it can."""
    out: list[str] = []
    if entry['kind'] == 'edf6':
        why = e6.missing(entry['archive'], entry['path'])
        if why is None and _file_sha(e6, entry['archive'], entry['path']) != entry['sha']:
            why = f"EDF6 的 {entry['path']} 不是生成时的那份（游戏更新过？）"
        return [why] if why else []
    if entry['kind'] != 'rab':
        return out
    rows = manifest(rel)['files']
    for src in sorted({tuple(row[6][:2]) for row in rows if row[6]}):
        why = e6.missing(*src)
        if why is None:
            try:
                have = _member_shas(e6, *src)
            except (AssertionError, ValueError, IndexError, struct.error, UnicodeDecodeError) as e:
                why = f'EDF6 的 {src[1]} 不再是可读的 RAB（{type(e).__name__}: {e}）'
        if why:
            out.append(why)
            continue
        for row in rows:
            if not row[6] or tuple(row[6][:2]) != src:
                continue
            index, name = row[6][2], row[6][3]
            if index >= len(have) or have[index] != (name, row[5]):
                out.append(f'EDF6 的 {src[1]} 里 {name} 不是生成时的那份（游戏更新过？）')
                break
    return out


# ------------------------------------------------------------------------------------------- deciding
def _not_ours(led: ledger.Ledger, rel: str, sha: str) -> bool:
    """A file in Mods this install must not write over: one no tool of ours wrote (no owner in the ledger), one another
    of our tools holds with other bytes, or ours edited by someone since our recorded write (as tools/edf41_maps.py)."""
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    if not owners:
        return True
    if any(o != OWNER for o in owners):
        return _installed_sha(led.disk(rel)) != sha
    return _installed_sha(led.disk(rel)) != led.files[ledger.key(rel)].get('sha')


def _in_place(led: ledger.Ledger, rel: str, sha: str) -> bool:
    """Ours already, with its final bytes: nothing to write, no source needed."""
    return OWNER in led.owners(rel) and _installed_sha(led.disk(rel)) == sha


def unavailable(root: str) -> list[tuple[str, str]]:
    """The groups build leaves out, [(group, why)]: map groups by map name ('IG_DESERT508'), object groups by their
    root SGO ('OBJECT/E510_DLC_FORTRESS.SGO'), the keys make_edf5_campaign's skip texts use."""
    reg = registry()
    if not reg:
        return []
    led = ledger.Ledger(root)
    e6 = Edf6(root)
    out: list[tuple[str, str]] = []
    for name, group in reg['groups'].items():
        why: list[str] = []
        foreign = [rel for rel in group['files'] if _not_ours(led, rel, reg['files'][rel]['sha'])]
        if foreign:
            why.append(f"Mods 里已有别的 MOD 的文件：{', '.join(foreign)}")
        gone = [rel for rel in group['needs'] if rel.upper() not in e6.where]
        if gone:
            why.append(f"EDF6 里没有 {', '.join(gone)}")
        for rel in group['files']:
            if rel in foreign or _in_place(led, rel, reg['files'][rel]['sha']):
                continue
            why += source_problems(e6, rel, reg['files'][rel])
        if why:
            out.append((name, '；'.join(dict.fromkeys(why))))
    return out


def offered(root: str) -> tuple[set[str], set[tuple[str, str]], dict[str, str]]:
    """For make_edf5_campaign.plan: (the map names 'X.MAC' and the (folder, name) files the install will write, and
    {group: why} for those it leaves out)."""
    reg = registry()
    why = dict(unavailable(root))
    maps = {f'{g}.MAC' for g, group in reg.get('groups', {}).items() if group['kind'] == 'map' and g not in why}
    names = {tuple(rel.upper().split('/', 1)) for g, group in reg.get('groups', {}).items()
             if group['kind'] == 'object' and g not in why for rel in group['files']}
    return maps, names, why   # type: ignore[return-value]


# ------------------------------------------------------------------------------------------- putting together
def assemble(rel: str, entry: dict, e6: Edf6, blobs: Mapping[str, dict]) -> bytes:
    """File `rel`'s final bytes; ValueError when they are not the ones the registry names."""
    kind = entry['kind']
    if kind == 'edf6':
        data = e6.read(entry['archive'], entry['path'])
    elif kind == 'blob':
        data = unpack_blob(entry['sha'], blobs[entry['sha']], _read_data)
    elif kind == 'rab':
        m = manifest(rel)
        stored: dict[int, bytes] = {}
        by_src: dict[tuple[str, str], list[int]] = {}
        for i, row in enumerate(m['files']):
            if row[6]:
                by_src.setdefault((row[6][0], row[6][1]), []).append(i)
            else:
                stored[i] = unpack_blob(row[5], blobs[row[5]], _read_data)
        for (archive, path), rows in by_src.items():   # each EDF6 archive read once, dropped after
            src = mdb.rab_read(e6.read(archive, path)).files
            for i in rows:
                f = src[m['files'][i][6][2]]
                if f.name != m['files'][i][6][3] or sha256(f.stored) != m['files'][i][5]:
                    raise ValueError(f'{rel}: EDF6 {path} member {f.name} is not the one recorded')
                stored[i] = f.stored
        data = mdb.rab_write(mdb.Rab(m['version'], m['folders'],
                                     [mdb.RabFile(r[0], r[1], r[2], stored[i], (r[3], r[4]))
                                      for i, r in enumerate(m['files'])]))
    else:
        raise ValueError(f'{rel}: unknown kind {kind}')
    if hashlib.sha256(data).hexdigest() != entry['sha']:
        raise ValueError(f'{rel}: not the file the registry lists (a damaged install of the tools)')
    return data


class Files(Mapping[str, bytes]):
    """{Mods path: bytes} of the groups build keeps, each file put together when read (hundreds of MB each: never all
    in memory at once). sha(rel) is its final SHA-256 without reading it."""

    def __init__(self, root: str, rels: list[str], reg: dict) -> None:
        self.root, self.rels, self.reg = root, rels, reg
        self._e6: Edf6 | None = None

    def __getitem__(self, rel: str) -> bytes:
        if rel not in self.rels:
            raise KeyError(rel)
        if self._e6 is None:
            self._e6 = Edf6(self.root)
        return assemble(rel, self.reg['files'][rel], self._e6, self.reg['blobs'])

    def __iter__(self) -> Iterator[str]:
        return iter(self.rels)

    def __len__(self) -> int:
        return len(self.rels)

    def sha(self, rel: str) -> str:
        return self.reg['files'][rel]['sha']


def build(root: str) -> tuple[Files, list[tuple[str, str]]]:
    """The files of every group but those unavailable() names."""
    reg = registry()
    skipped = unavailable(root)
    left_out = {g for g, _ in skipped}
    rels = [rel for g, group in reg.get('groups', {}).items() if g not in left_out for rel in group['files']]
    return Files(root, list(dict.fromkeys(rels)), reg), skipped


def install(root: str, files: Files) -> list[str]:
    """Writes `files` as OWNER's, one at a time, but a file it already holds with these bytes; what it wrote before and
    does not now is released."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = []
    for rel in files:
        if ledger.key(rel) in before and _installed_sha(led.disk(rel)) == files.sha(rel):
            continue
        paths.append(led.put(OWNER, rel, files[rel]))
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def status(root: str) -> list[str]:
    """Lines for the installer's check (reading only): each group installed, or why it is not."""
    reg = registry()
    if not reg:
        return ['EDF5 任务包１的地图与要塞：安装包里没有这部分数据']
    led = ledger.Ledger(root)
    why = dict(unavailable(root))
    out = []
    for g, group in reg['groups'].items():
        ok = all(_in_place(led, rel, reg['files'][rel]['sha']) for rel in group['files'])
        missions = '、'.join(group['missions'])
        out.append(f'EDF5 任务包１ {g}（{missions}）：' + ('已安装' if ok else f'未安装：{why[g]}' if g in why
                                                        else '未安装（选 1 安装）'))
    return out
