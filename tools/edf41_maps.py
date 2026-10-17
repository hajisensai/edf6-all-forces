"""EDF4.1's mission maps on EDF6 (plan P6): converted at install time from the player's own EDF4.1 into Mods/MAP under
4.1's names, so 4.1's mission scripts load them as they are. 1.4 GB of map art cannot ship with the tools: this part
needs EDF4.1 installed (pylib/gamedir.py EDF41) or an EDF4.1 asset pack (pylib/edf41src.py: the MAP files below,
unpacked, made by tools/export_edf41_assets.py); without either nothing is written and the reason is said.

Per map (MAPS):
  <MAP>.MAC   pylib/map_legacy.py (MAPB / MAPO in EDF6's layout), its .rmpa members by pylib/rmpa_legacy.py (the
              wire routes' links to other routes through the mapb's wire points), its .trb trees in EDF6's form
              (map_legacy.trb_from_legacy), and the textures its wires draw with as members, where EDF6 keeps them
              (with_wire_textures)
  <MAP>.MAD   4.1's collision.hkt (Havok 2014) repacked; the plugin migrates it at load (src/legacymap.cpp)
  <MAP>.RAB   pylib/legacy_assets.py, map parts

The SK_* maps (their ground is a heightfield, .shfb, that EDF6 has no loader for) differ in three of those files
(pylib/sk_map.py, docs/edf41-sk-terrain.md): the ground becomes an EDF6 terrain piece
  <PIECE>.FMB the field as a mesh (pylib/fmb_build.py: 4 m lattice, 20 x 20 cell leaves, the field's layers as EDF6
              terrain layers, normals from the heights), placed by the MAC as `<piece>.fmep` like EDF6's own pieces;
              the field's grass, flowers, trees, bushes and small objects as the piece's plants (pylib/fmex.py; their
              heights in the MAC's mapo)
  <MAP>.MAD   the ground's collision (the same lattice as 10 x 10 cell tiles) and the map's object bodies rebuilt from the
              RAB's models, written as EDF6's own Havok 2020 file (pylib/hkcms_build.py): nothing of the 4.1 collision
              stays, EDF6 loads it like any of its maps
  <MAP>.RAB   the field's colour array as <map>_color.dds, EDF6's flat normal map as <map>_normal.dds (the two textures
              EDF6's terrain looks up by the map's name)
  <MAP>_<WEATHER>.MAE / .RAB, <MAP>.NAVMESH(_H).HKT / .NVMD
              the donor EDF6 map's, read from the player's EDF6: EDF6 keeps a map's weather (lights, fog, sky, its
              env-map shapes and textures) in these, which 4.1 has elsewhere, and 4.1 has no navmesh. A donor's MAE
              names its own rmpa member and textures, all in the copied pair, so the pair loads as it does for the
              donor (real game, IG_CAVE01 on IG_CAVE501's, 2026-10-10). The sky and reflections are the donor's, and
              the navmesh does not match 4.1's ground: AI walking routes are the donor's (open issue).

    install(root, root41, say) -> (written, skipped)   map by map, each written as soon as it is converted
    remove(root) -> (deleted, kept)

A file of the same name in Mods that the ledger does not record as ours (another mod's) is never written over: that
map is left out and said so. A map installed by this VERSION from the same EDF4.1 files is not converted again.
"""
from __future__ import annotations

import functools
import json
import os
import struct
import sys
from typing import Callable, Iterator

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import bigmap  # noqa: E402
import edf41src  # noqa: E402
import ledger  # noqa: E402
import legacy_assets  # noqa: E402
import map_legacy  # noqa: E402
import mdb  # noqa: E402
import rmpa_legacy  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
import sk_map  # noqa: E402
from modfiles import load_json, sha256, sha256_file  # noqa: E402

OWNER = 'edf41maps'   # pylib/ledger.py OWNERS
VERSION = 3           # raise when a converter's output changes: installed maps are converted again
# 2: wire textures in the MAC, cross-route wire links kept, SK grass, SK leaf shader records (docs/feedback-2026-10-17-edf41-maps.md)
# 3: .trb in EDF6's form, SK trees / bushes / small objects as plant groups
STAMP = '.edf41maps.json'   # Mods/: {map: {'version': VERSION, 'source': {4.1 file: [size, CRC]}}}
ARCHIVES = ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk', 'DX11.cpk')
NAVMESH = ('NAVMESH.HKT', 'NAVMESH.NVMD', 'NAVMESH_H.HKT', 'NAVMESH_H.NVMD')

# 4.1's mission maps: map -> (the EDF6 donor of weather and navmesh, the weathers 4.1's mission scripts load it with:
# the story's and its two DLC mission packs', selftest edf41_maps_cover_the_scripts).
# The donor has every one. The SK_* maps are built from the field (SK_MAPS); the weathers are the ones the scripts name
# (selftest edf41_maps_cover_the_scripts).
MAPS: dict[str, tuple[str, tuple[str, ...]]] = {
    'IG_CAVE01': ('IG_CAVE501', ('FINE',)),
    'IG_HORNETCAVE01': ('IG_CAVE503', ('FINE',)),
    'NW_UNDERGROUND01': ('IG_CAVE503', ('FINE',)),          # EDF6 has no underground map: an enclosed one
    'IG_WRECKTOWN01': ('NW_HAIKYO601', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),   # ruins
    'NW_HILLYCITY_LIGHT': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),
    'NW_KASENJIKI01': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),
    'NW_KOUSOUBLD01': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),
    'NW_SINSUIKOUEN': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),
    'NW_SOUKOGAI01': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY2', 'EVENING', 'FINE')),
    'SK_HEIGEN02': ('IG_HEIGEN507', ('CLOUDY', 'CLOUDY2', 'SUNSET')),                        # a plain
    'SK_VALLEY01': ('IG_TERRACEMOUNTAIN', ('CLOUDY', 'FINE', 'FINECLOUD')),
    'SK_KAIGAN01': ('IG_KAIGAN502', ('CLOUDY', 'CLOUDY2', 'FINE', 'SUNSET')),                           # a coast
    'SK_SANGAKU01': ('IG_SANGAKU506', ('CLOUDY2', 'EVENING', 'FINE', 'FINECLOUD', 'SUNSET')),
    'SK_SANGAKU02': ('IG_SANGAKU506', ('CLOUDY', 'CLOUDY2', 'FINECLOUD', 'SUNSET')),
    'SK_STEPMOUNTAIN01': ('IG_TERRACEMOUNTAIN', ('CLOUDY2', 'EVENING', 'FINE', 'FINECLOUD', 'SUNSET')),
    'SK_CRATER01': ('IG_CRATER601', ('CLOUDY2', 'SUNSET')),
}
SK_MAPS = frozenset(n for n in MAPS if n.startswith('SK_'))


class Archives:
    """Every MAP file of a game's archives, read on demand. For an EDF4.1 asset pack (pylib/edf41src.py), its MAP files
    as the manifest records them, fingerprinted by what the manifest says EDF4.1's archive records: the same stamp."""

    def __init__(self, root: str) -> None:
        self.at: dict[str, rootcpk.Game] = {}
        self.pack: edf41src.Pack | None = None
        self.why = ''
        if edf41src.is_pack(root):
            try:
                self.pack = edf41src.pack(root)
            except ValueError as e:
                self.why = str(e)
            return
        for name in ARCHIVES:
            if os.path.isfile(os.path.join(root, name)):
                game = rootcpk.Game(root, name)
                for folder, file in game.cpk.index:
                    if folder.upper() == 'MAP':
                        self.at.setdefault(file.upper(), game)

    def read(self, name: str) -> bytes:
        if self.pack is not None or self.why:
            return self._pack().read(f'MAP/{name}')
        return self.at[name.upper()].read('MAP', name.upper())

    def _pack(self) -> edf41src.Pack:
        if self.pack is None:
            raise ValueError(self.why)
        return self.pack

    def fingerprint(self, name: str) -> list[int]:
        """The archive's record of the file (size unpacked, CRC): a changed EDF4.1 file, without reading it. A pack's:
        the manifest's record of it, the file there of the size it says (else ValueError)."""
        if self.pack is not None or self.why:
            why = edf41src.problems(self._pack().root, [f'MAP/{name}'])
            if why:
                raise ValueError(why[0])
            return self._pack().source(f'MAP/{name}')
        entry = self.at[name.upper()].cpk.index[('MAP', name.upper())]
        return [entry['ExtractSize'], entry['CRC']]


def files_of(name: str) -> list[str]:
    """The Mods paths map `name` installs."""
    weathers = MAPS[name][1]
    return ([f'MAP/{name}.{ext}' for ext in ('MAC', 'MAD', 'RAB')]
            + ([f'MAP/{sk_map.fmb_file(name)}'] if name in SK_MAPS else [])
            + [f'MAP/{name}_{w}.{ext}' for w in weathers for ext in ('MAE', 'RAB')]
            + [f'MAP/{name}.{n}' for n in NAVMESH])


def with_wire_textures(mac: bigmap.Marc, rab: bytes) -> None:
    """Adds to the MAC, as plain DDS members before map.mapo, the textures its wire types draw with (each .erb
    member's `strings`, + '.dds', taken from the map's RAB) when the map has wires (pairs in the mapb's 0x7C block).
    That is where EDF6 keeps them: the 24 EDF6 maps with wire pairs all hold wire_test.dds in the MAC (and in the
    RAB), the 26 without pairs never; EDF5's NW_DANTI01 / NW_HENDEN had it only in the RAB and EDF6's re-export added
    it. 4.1's maps have it only in the RAB (NW_UNDERGROUND01's elec_wire_ugc draws with nw_wire_black). EDF.dll's
    cable_init (0x15B3F0) drops a wire type whose texture its lookup (0x18DD30) does not find (0x15B865); that lookup
    goes through the same container (the MAC object + 0x60) as grass_init's grass.merge.dds, which EDF6 keeps only in
    the RAB (as the terrain's <map>_color.dds and 233 of EDF6's own tree textures), so the RAB copy is found as well:
    this only follows EDF6's layout, it is not why wires would be missing (docs/feedback-2026-10-17-edf41-maps.md 3b)."""
    if not map_legacy.cable_graph(mac.get('map.mapb'))[1]:
        return
    want: list[str] = []
    for n, d in mac.files:
        if n.lower().endswith('.erb'):
            for t in sgo.plain(sgo.read(d)[1]).get('strings', []):
                if f'{t}.dds'.lower() not in want:
                    want.append(f'{t}.dds'.lower())
    have = {n.lower() for n, _ in mac.files}
    textures = {f.name.lower(): f for f in mdb.rab_read(rab).files}
    add = []
    for t in want:
        if t in have:
            continue
        if t not in textures:
            raise ValueError(f'wire texture {t} is in neither the MAC nor the RAB')
        add.append((t, textures[t].data))
    at = [n for n, _ in mac.files].index('map.mapo') if 'map.mapo' in have else len(mac.files)
    mac.files[at:at] = add


def convert(name: str, e41: Archives, e6: Archives) -> dict[str, bytes]:
    """Map `name`'s files (rel under Mods -> bytes)."""
    donor, weathers = MAPS[name]
    mac = bigmap.Marc.parse(map_legacy.mac_from_legacy(e41.read(f'{name}.MAC')))
    cables = map_legacy.cable_graph(mac.get('map.mapb'))
    mac.files = [(n, rmpa_legacy.rmpa_from_legacy(d, cables) if n.lower().endswith('.rmpa')
                  else map_legacy.trb_from_legacy(d) if n.lower().endswith('.trb') else d)
                 for n, d in mac.files]
    rab = legacy_assets.convert(f'MAP/{name}.RAB', e41.read(f'{name}.RAB'), map_part=True)
    with_wire_textures(mac, rab)
    fmb_bytes = None
    if name in SK_MAPS:
        terrain = sk_map.terrain(mac, rab, e6, name)
        mac, rab, fmb_bytes = terrain.mac, terrain.rab, terrain.fmb
        hkt41 = dict(map_legacy.parse_marc_lenient(e41.read(f'{name}.MAD'))[1])['collision.hkt']
        mad, _missing = sk_map.collision(terrain, hkt41, e6)
    else:
        version, members = map_legacy.parse_marc_lenient(e41.read(f'{name}.MAD'))
        mad = bigmap.Marc(version, list(members)).build()
    out: dict[str, bytes] = {f'MAP/{name}.MAC': mac.build(), f'MAP/{name}.MAD': mad, f'MAP/{name}.RAB': rab}
    if fmb_bytes is not None:
        out[f'MAP/{sk_map.fmb_file(name)}'] = fmb_bytes
    for w in weathers:
        for ext in ('MAE', 'RAB'):
            out[f'MAP/{name}_{w}.{ext}'] = e6.read(f'{donor}_{w}.{ext}')
    for n in NAVMESH:
        out[f'MAP/{name}.{n}'] = e6.read(f'{donor}.{n}')
    return out


def _source(name: str, e41: Archives) -> dict[str, list[int]]:
    return {rel.split('/', 1)[1]: e41.fingerprint(rel.split('/', 1)[1]) for rel in sources(name)}


def _foreign(led: ledger.Ledger, rel: str) -> bool:
    """A file no tool of ours wrote, another tool's, or edited since our recorded write: preserve its bytes."""
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    return not owners or any(o != OWNER for o in owners) or led.changed(rel)


def _current(led: ledger.Ledger, stamp: dict, name: str, source: dict[str, list[int]]) -> bool:
    """Map `name` is installed by this VERSION from these 4.1 files, every file still as written."""
    entry = stamp.get(name, {})
    if entry.get('version') != VERSION or entry.get('source') != source:
        return False
    mine = set(led.owned_by(OWNER))
    return all(ledger.key(rel) in mine and os.path.exists(led.disk(rel)) for rel in files_of(name))


NO_EDF41 = '没有找到 EDF4.1 也没有 EDF4.1 素材包（需要其中之一才能转换它的地图）'


def sources(name: str) -> list[str]:
    """The EDF4.1 files map `name` is converted from (an asset pack's paths: tools/export_edf41_assets.py)."""
    return [f'MAP/{name}.{ext}' for ext in ('MAC', 'MAD', 'RAB')]


def unavailable(root: str, root41: str | None) -> list[tuple[str, str]]:
    """The maps install leaves out before converting anything, [(map, why)]: all of them without EDF4.1, else those
    with a file in Mods that is not ours, and from an asset pack those whose files it lacks (or has of another size).
    Conversion itself can still fail (install's skipped list). The one place that decides it: install skips exactly
    these, and the campaign plan offers only missions whose maps are not here."""
    if not root41:
        return [(name, NO_EDF41) for name in MAPS]
    led = ledger.Ledger(root)
    out: list[tuple[str, str]] = []
    for name in MAPS:
        foreign = [rel for rel in files_of(name) if _foreign(led, rel)]
        if foreign:
            out.append((name, f"Mods 里已有别的 MOD 的文件：{', '.join(foreign)}"))
            continue
        bad = edf41src.problems(root41, sources(name))
        if bad:
            out.append((name, '；'.join(bad)))
    return out


CONVERT_ERRORS = (ValueError, KeyError, IndexError, AssertionError, NotImplementedError, struct.error)
# A conversion process's budget (model_workers.workers_for): the largest map (NW_KASENJIKI01) peaks near 1.1 GB in its
# process, and its ~420 MB of files are held once more here while they are written.
WORKER_MEMORY = 2 * 1024**3
MAX_WORKERS = 4


@functools.lru_cache(maxsize=4)
def _archives(root: str) -> Archives:
    """One Archives per game per process: a conversion process converts several maps."""
    return Archives(root)


def convert_job(name: str, root41: str, root: str) -> tuple[bool, dict[str, bytes] | str]:
    """convert(name) in a conversion process (pylib/model_workers.py): (True, files), or (False, why) for the errors
    install reports as a skipped map; any other error is raised in the installer as it would be there."""
    try:
        return True, convert(name, _archives(root41), _archives(root))
    except CONVERT_ERRORS as e:
        return False, f'转换失败：{type(e).__name__}: {e}'


def workers() -> int:
    """How many maps convert at once: half the CPUs, at most MAX_WORKERS, WORKER_MEMORY of free memory each (one on a
    machine with little free; EDF6VC_MAP_WORKERS overrides)."""
    import model_workers
    forced = os.environ.get('EDF6VC_MAP_WORKERS', '')
    if forced.isdigit() and int(forced) > 0:
        return int(forced)
    return model_workers.workers_for(os.cpu_count() or 1, model_workers.available_memory(), MAX_WORKERS, WORKER_MEMORY)


def install(root: str, root41: str | None, say: Callable[[str], None] = print) -> tuple[list[str], list[tuple[str, str]]]:
    """Every map of MAPS converted and written as OWNER's; (paths written, [(map, why not)]). With more than one map to
    convert and workers() > 1 they convert in processes of their own, several at once (convert_job; nothing patched
    into this module reaches them); each is written, in MAPS order, as soon as it and the maps before it are
    converted: the files, the ledger, the stamp and the messages are those of converting them here one by one."""
    if not root41:
        return [], unavailable(root, root41)
    import model_workers
    e41 = Archives(root41)
    led = ledger.Ledger(root)
    stamp_path = os.path.join(root, 'Mods', STAMP)
    stamp = load_json(stamp_path, {})
    written: list[str] = []
    skipped = unavailable(root, root41)
    left_out = {name for name, _ in skipped}
    wanted: set[str] = set()
    plan: list[tuple[str, dict[str, list[int]] | None, str]] = []   # (map, its source, why it is not converted)
    for name in MAPS:
        if name in left_out:
            continue
        try:
            source = _source(name, e41)
        except (ValueError, KeyError) as e:
            plan.append((name, None, f'源地图不可用：{type(e).__name__}: {e}'))
            continue
        plan.append((name, source, ''))
    todo = [name for name, source, _ in plan if source is not None and not _current(led, stamp, name, source)]
    count = workers() if len(todo) > 1 else 1
    if count > 1:
        jobs = [(convert_job, (name, root41, root), f'地图 {name}') for name in todo]
        results = model_workers.ordered_jobs(jobs, count)
    else:   # here, one by one (what the processes do, without starting one)
        e6 = Archives(root)

        def here() -> Iterator[tuple[bool, dict[str, bytes] | str]]:
            for name in todo:
                try:
                    yield True, convert(name, e41, e6)
                except CONVERT_ERRORS as e:
                    yield False, f'转换失败：{type(e).__name__}: {e}'
        results = here()
    for name, source, why in plan:
        if source is None:
            skipped.append((name, why))
            continue
        wanted.update(ledger.key(rel) for rel in files_of(name))
        if name not in todo:
            continue
        say(f'  转换 4.1 地图 {name}（借用 EDF6 {MAPS[name][0]} 的天气与寻路网格）……')
        ok, files = next(results)
        if not ok:
            skipped.append((name, files))
            wanted.difference_update(ledger.key(rel) for rel in files_of(name))
            continue
        for rel, data in files.items():
            if ledger.key(rel) in set(led.owned_by(OWNER)) and sha256_file(led.disk(rel)) == sha256(data):
                continue
            written.append(led.put(OWNER, rel, data))
        del files
        stamp[name] = {'version': VERSION, 'source': source}
        with open(stamp_path, 'w', encoding='utf-8') as f:
            json.dump(stamp, f, indent=1)
    led.release(OWNER, sorted(set(led.owned_by(OWNER)) - wanted))
    return written, skipped


def remove(root: str) -> tuple[list[str], list[str]]:
    stamp = os.path.join(root, 'Mods', STAMP)
    if os.path.exists(stamp):
        os.remove(stamp)
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)
