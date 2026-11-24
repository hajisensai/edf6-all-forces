"""Model archives that are always loaded together, merged into one archive with one copy of each texture (mem-2).

The engine builds a texture map per archive and a model binds its material textures by filename in its own archive's
map only (docs/mdb-format.md §1: EDF.dll 0x110b03b hands the loader the archive's own TEXTURE map, 0x110f640 looks the
filename up there, a miss is a null texture; there is no lookup in another archive's map). So two models can share a
texture only by living in the same archive: one archive with several models and one copy of the textures, each SGO
naming its own model in it (as the stock BOMBER501.MRAB: bomber501.mdb and bomber501_2.mdb, or MOB.MRAB's five people).

SHARED lists the merges: each target's sources are what a generator builds (and what its checks measure), and `apply`
is the generator's last step, so what is written has the target only:

  * every TEXTURE / HD-TEXTURE pair of the same content once, under the first source's name (a name another source
    uses for other content gets the source's stem in front); a model whose texture is renamed has its filename
    rewritten;
  * every MODEL member of every source (their names must not collide);
  * members in folder-table order, TEXTURE -> MODEL -> HD-TEXTURE (mdb.insert_member: the load-order contract);
  * every SGO in the output that drew from a source draws from the target (its model and LOD table as they were).

Only sources one generator builds together are merged (one ledger owner, pylib/ledger.py, one build cache group): the
jets' four (tools/make_jets.py) and the C-130's two (tools/make_c130.py). Another writer of an SGO on one of these
models (testrange/gen.py) asks `installed` which archive is there and `redirect`s its SGO to it.

  python pylib/texshare.py [Mods/OBJECT dir] [A,B,...]...   duplicate texture bytes across the given archives
"""
from __future__ import annotations

import collections
import hashlib
import os
import sys
from typing import Callable, Iterable

from mdb import Rab, RabFile, _raw_size, cmpl_compress, folder_order_ok, mdb_read, mdb_write, rab_read, rab_write

FOLDERS = ['TEXTURE', 'MODEL', 'HD-TEXTURE']
TEXTURE, MODEL, HD = range(3)

# target archive -> the archives merged into it, in order (the first one's texture names win)
SHARED: dict[str, tuple[str, ...]] = {
    # tools/make_jets.py: the elevon bomber (strike / fighter jets), the interceptor's and the multirole's bombers and
    # the transport heli; all preloaded every mission (src/jet_spawn.cpp PreloadJets), sharing the 410's
    # helicopter_body_* and the F-16 cockpit (cockpits.graft_files)
    'EDF6VC_AIRCRAFT.MRAB': ('EDF6VC_JET.MRAB', 'EDF6VC_INTERCEPTOR.MRAB', 'EDF6VC_MULTIROLE.MRAB',
                             'EDF6VC_HELI_TRANSPORT.MRAB'),
    # tools/make_c130.py: the AC-130 and the paratroop plane, one airframe's textures twice
    'EDF6VC_C130.MRAB': ('EDF6VC_C130_GUNSHIP.MRAB', 'EDF6VC_C130_TRANSPORT.MRAB'),
}
SHARED_IN: dict[str, str] = {src: target for target, srcs in SHARED.items() for src in srcs}


class TexShareError(ValueError):
    pass


def _req(ok: bool, what: str) -> None:
    if not ok:
        raise TexShareError(what)


def tex_key(name: str) -> str:
    """The name a TEXTURE member is mapped under (`x.lod.dds` -> `x.dds`, EDF.dll 0x110b182), an HD member's own."""
    return name.replace('.lod.', '.', 1)


def _digest(stored: bytes) -> str:
    return hashlib.sha1(stored).hexdigest()


def _textures(rab: Rab) -> dict[str, list[RabFile]]:
    """Texture map name (lowercase) -> its members (the TEXTURE lod, the HD-TEXTURE one), archive order."""
    out: dict[str, list[RabFile]] = collections.defaultdict(list)
    for f in rab.files:
        if f.folder in (TEXTURE, HD):
            out[tex_key(f.name).lower()].append(f)
    return out


def _content(members: list[RabFile]) -> tuple:
    return tuple(sorted((f.folder, _digest(f.stored)) for f in members))


def merge(archives: list[tuple[str, bytes]]) -> bytes:
    """`archives` ([(file name, bytes)], each with the stock folders TEXTURE, MODEL, HD-TEXTURE) as one archive."""
    _req(len(archives) >= 2, 'nothing to merge')
    seen: dict[tuple, str] = {}             # texture content -> its name in the target
    taken: dict[str, tuple] = {}            # name (lowercase) in the target -> its content
    tex: list[RabFile] = []
    models: list[RabFile] = []
    hd: list[RabFile] = []
    version = None
    for file, data in archives:
        rab = rab_read(data)
        _req(rab.folders == FOLDERS, f'{file}: folders {rab.folders}, not {FOLDERS}')
        _req(folder_order_ok(rab), f'{file}: members not in folder order')
        version = rab.version if version is None else version
        _req(rab.version == version, f'{file}: archive version {rab.version} != {version}')
        stem = file.rsplit('.', 1)[0].lower()
        rename: dict[str, str] = {}         # this source's texture map name (lowercase) -> its name in the target
        for key, members in _textures(rab).items():
            content = _content(members)
            if content in seen:
                rename[key] = seen[content]
                continue
            name, prefix = tex_key(members[0].name), ''
            if name.lower() in taken:
                prefix = f'{stem}_'
                name = prefix + name
                _req(name.lower() not in taken, f'{file}: {name} taken twice')
            seen[content] = name
            taken[name.lower()] = content
            rename[key] = name
            for f in members:
                (tex if f.folder == TEXTURE else hd).append(RabFile(prefix + f.name, f.folder, f.flag, f.stored, f.unk))
        own = set(rename)
        for f in rab.files:
            if f.folder != MODEL:
                continue
            _req(all(f.name.lower() != m.name.lower() for m in models), f'{file}: model {f.name} is in another source')
            md = mdb_read(f.data)
            changed = False
            for t in md.textures:
                key = t.filename.lower()
                if key not in own:
                    # bound to nothing in its own archive: it must not start binding another source's texture
                    _req(key not in taken, f'{file} {f.name}: {t.filename} unbound at home, another source has it')
                    continue
                if rename[key] != t.filename:
                    t.filename = rename[key]
                    changed = True
            stored = f.stored
            if changed:
                raw = mdb_write(md)
                stored = cmpl_compress(raw) if f.stored[:4] == b'CMPL' else raw
            models.append(RabFile(f.name, MODEL, f.flag, stored, f.unk))
    out = Rab(version, list(FOLDERS), tex + models + hd)
    _req(folder_order_ok(out), 'merged members out of folder order')
    return rab_write(out)


def _object_path(file: str) -> str:
    return f'app:/object/{file.lower()}'


def redirect(data: bytes, moved: dict[str, str]) -> bytes:
    """An SGO's bytes with its animation_model archive (one of `moved`'s keys: file names under OBJECT) its target's;
    anything else (its model, its LOD table) as it was. Unchanged bytes when it draws from none of them."""
    import model_lod
    if data[:4] not in (b'DSGO', b'SGO\0'):
        return data
    doc = model_lod._Sgo(data)
    ref = doc.model()
    if ref is None:
        return data
    file = ref[0].split('/')[-1].upper()
    if file not in moved:
        return data
    return doc.with_archive(_object_path(moved[file]))


def apply(files: dict[str, bytes], target: str) -> dict[str, bytes]:
    """`files` (a generator's output, Mods path -> bytes, holding every source of SHARED[target]) with them merged
    into OBJECT/<target> and every SGO in it redirected there. Checked (problems) before it returns."""
    srcs = SHARED[target]
    rels = {s: f'OBJECT/{s}' for s in srcs}
    missing = [r for r in rels.values() if r not in files]
    _req(not missing, f'{target}: sources not built: {missing}')
    _req(f'OBJECT/{target}' not in files, f'{target} already in the output')
    out = {rel: data for rel, data in files.items() if rel not in rels.values()}
    out[f'OBJECT/{target}'] = merge([(s, files[rels[s]]) for s in srcs])
    moved = {s: target for s in srcs}
    for rel, data in out.items():
        if rel.upper().startswith('OBJECT/') and rel.upper().endswith('.SGO'):
            out[rel] = redirect(data, moved)
    bad = problems(out)
    _req(not bad, '; '.join(bad))
    return out


def names(rels: Iterable[str]) -> list[str]:
    """A generator's output paths (its sources') as written: each SHARED source's path its target's, once."""
    out: list[str] = []
    for rel in rels:
        head, _, file = rel.rpartition('/')
        if head.upper() == 'OBJECT' and file.upper() in SHARED_IN:
            rel = f'{head}/{SHARED_IN[file.upper()]}'
        if rel not in out:
            out.append(rel)
    return out


def installed(file: str, exists: Callable[[str], bool]) -> str:
    """The archive (file name under OBJECT) a model built as `file` is in on disk: its SHARED target when that
    `exists`, else `file`."""
    target = SHARED_IN.get(file.upper())
    return target if target is not None and exists(target) else file


def moved(exists: Callable[[str], bool]) -> dict[str, str]:
    """Every SHARED source -> its target, for the targets that `exists` (file names under OBJECT)."""
    return {s: t for s, t in SHARED_IN.items() if exists(t)}


def problems(files: dict[str, bytes]) -> list[str]:
    """In `files`: no SGO draws from a SHARED source when its target is there; every SHARED target is in folder order,
    holds each model an SGO in `files` names in it, every texture its models name, and no texture twice."""
    import model_lod
    bad: list[str] = []
    up = {rel.upper(): rel for rel in files}
    for target in SHARED:
        rel = up.get(f'OBJECT/{target}')
        if rel is None:
            continue
        rab = rab_read(files[rel])
        if rab.folders != FOLDERS or not folder_order_ok(rab):
            bad.append(f'{target}: folders {rab.folders} / members not in TEXTURE, MODEL, HD-TEXTURE order')
        maps = {tex_key(f.name).lower() for f in rab.files if f.folder == TEXTURE}
        contents = collections.Counter(_content(m) for m in _textures(rab).values())
        bad += [f'{target}: a texture {n} times' for n in contents.values() if n > 1]
        for f in rab.files:
            if f.folder == MODEL:
                lost = sorted({t.filename for t in mdb_read(f.data).textures if t.filename.lower() not in maps})
                if lost:
                    bad.append(f'{target} {f.name}: textures not in the archive: {lost}')
        for src in SHARED[target]:
            if f'OBJECT/{src}' in up:
                bad.append(f'{target}: its source {src} written too')
    for rel in sorted(files):
        data = files[rel]
        if not rel.upper().endswith('.SGO') or data[:4] not in (b'DSGO', b'SGO\0'):
            continue
        ref = model_lod._Sgo(data).model()
        if ref is None:
            continue
        file = ref[0].split('/')[-1].upper()
        if file in SHARED_IN and f'OBJECT/{SHARED_IN[file]}' in up:
            bad.append(f'{rel}: draws from {file}, merged into {SHARED_IN[file]}')
        if file in SHARED and f'OBJECT/{file}' in up:
            members = {f.name.lower() for f in rab_read(files[up[f'OBJECT/{file}']]).files if f.folder == MODEL}
            table = model_lod._Sgo(data).table() or []
            lost = [n for n in [ref[1], *(n for n, _d in table)] if n.lower() not in members]
            if lost:
                bad.append(f'{rel}: {file} has no {lost}')
    return bad


# ------------------------------------------------------------------------------------------------- report


def duplicates(archives: dict[str, bytes]) -> tuple[int, int, list[tuple[int, int, tuple[str, ...]]]]:
    """Texture members (TEXTURE / HD-TEXTURE) of `archives` (name -> bytes) of the same stored bytes: (texture bytes
    in all, bytes a single copy each would save, [(bytes saved, textures, the archives sharing them)] largest first).
    Sizes are uncompressed (CMPL raw size)."""
    by: dict[str, list[tuple[str, int]]] = collections.defaultdict(list)
    total = 0
    for name, data in archives.items():
        rab = rab_read(data)
        for f in rab.files:
            if rab.folders[f.folder] in ('TEXTURE', 'HD-TEXTURE'):
                size = _raw_size(f.stored)
                by[_digest(f.stored)].append((name, size))
                total += size
    groups: dict[tuple[str, ...], list[int]] = collections.defaultdict(lambda: [0, 0])
    dup = 0
    for hits in by.values():
        if len(hits) > 1:
            extra = sum(s for _n, s in hits[1:])
            dup += extra
            g = groups[tuple(sorted({n for n, _s in hits}))]
            g[0] += extra
            g[1] += 1
    return total, dup, sorted(((b, n, k) for k, (b, n) in groups.items()), reverse=True)


def merged_size(archives: dict[str, bytes]) -> tuple[int, int]:
    """(bytes of `archives` apart, bytes merged with one copy of each texture), members uncompressed."""
    apart = 0
    seen: dict[tuple, int] = {}
    for data in archives.values():
        rab = rab_read(data)
        for f in rab.files:
            size = _raw_size(f.stored)
            apart += size
            key = (TEXTURE if f.folder != MODEL else MODEL, _digest(f.stored), f.name.lower() if f.folder == MODEL else '')
            seen[key] = size
    return apart, sum(seen.values())


def main(argv: list[str]) -> int:
    folder = argv[0] if argv else os.path.join(os.environ.get('EDF6_GAME', r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'),
                                               'Mods', 'OBJECT')
    sets = [s.split(',') for s in argv[1:]] or [[s for s in SHARED_IN]] + [list(s) for s in SHARED.values()]

    def read(name: str) -> bytes | None:
        p = os.path.join(folder, name)
        return open(p, 'rb').read() if os.path.isfile(p) else None
    for names_ in sets:
        got = {n: read(n) for n in names_}
        missing = [n for n, d in got.items() if d is None]
        have = {n: d for n, d in got.items() if d is not None}
        if missing:
            print(f'not in {folder}: {" ".join(missing)}')
        if len(have) < 2:
            continue
        total, dup, groups = duplicates(have)
        apart, merged = merged_size(have)
        print(f'{",".join(have)}: textures {total / 1e6:.1f} MB, duplicate {dup / 1e6:.1f} MB; '
              f'archives {apart / 1e6:.1f} MB apart, {merged / 1e6:.1f} MB merged')
        for b, n, k in groups:
            print(f'  {b / 1e6:7.2f} MB {n:4d} textures  {" ".join(k)}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
