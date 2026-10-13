"""EDF4.1's mission maps on EDF6 (plan P6): converted at install time from the player's own EDF4.1 into Mods/MAP under
4.1's names, so 4.1's mission scripts load them as they are. 1.4 GB of map art cannot ship with the tools: this part
needs EDF4.1 installed (pylib/gamedir.py EDF41); without it nothing is written and the reason is said.

Per map (MAPS):
  <MAP>.MAC   pylib/map_legacy.py (MAPB / MAPO in EDF6's layout), its .rmpa members by pylib/rmpa_legacy.py
  <MAP>.MAD   4.1's collision.hkt (Havok 2014) repacked; the plugin migrates it at load (src/legacymap.cpp)
  <MAP>.RAB   pylib/legacy_assets.py, map parts
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

import json
import os
import sys
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import bigmap  # noqa: E402
import ledger  # noqa: E402
import legacy_assets  # noqa: E402
import map_legacy  # noqa: E402
import rmpa_legacy  # noqa: E402
import rootcpk  # noqa: E402
from modfiles import load_json, sha256, sha256_file  # noqa: E402

OWNER = 'edf41maps'   # pylib/ledger.py OWNERS
VERSION = 1           # raise when a converter's output changes: installed maps are converted again
STAMP = '.edf41maps.json'   # Mods/: {map: {'version': VERSION, 'source': {4.1 file: [size, CRC]}}}
ARCHIVES = ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk', 'DX11.cpk')
NAVMESH = ('NAVMESH.HKT', 'NAVMESH.NVMD', 'NAVMESH_H.HKT', 'NAVMESH_H.NVMD')

# 4.1's mission maps but the SK_* ones (their heightfields, .shfb, EDF6 has no counterpart for: open issue): map ->
# (the EDF6 donor of weather and navmesh, the weathers 4.1's mission scripts load it with). The donor has every one.
MAPS: dict[str, tuple[str, tuple[str, ...]]] = {
    'IG_CAVE01': ('IG_CAVE501', ('FINE',)),
    'IG_HORNETCAVE01': ('IG_CAVE503', ('FINE',)),
    'NW_UNDERGROUND01': ('IG_CAVE503', ('FINE',)),          # EDF6 has no underground map: an enclosed one
    'IG_WRECKTOWN01': ('NW_HAIKYO601', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),   # ruins
    'NW_HILLYCITY_LIGHT': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),
    'NW_KASENJIKI01': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),
    'NW_KOUSOUBLD01': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),
    'NW_SINSUIKOUEN': ('NW_DANTI01', ('FINECLOUD', 'CLOUDY2', 'EVENING', 'FINE', 'SUNSET')),
    'NW_SOUKOGAI01': ('NW_DANTI01', ('CLOUDY2', 'EVENING', 'FINE')),
}


class Archives:
    """Every MAP file of a game's archives, read on demand."""

    def __init__(self, root: str) -> None:
        self.at: dict[str, rootcpk.Game] = {}
        for name in ARCHIVES:
            if os.path.isfile(os.path.join(root, name)):
                game = rootcpk.Game(root, name)
                for folder, file in game.cpk.index:
                    if folder.upper() == 'MAP':
                        self.at.setdefault(file.upper(), game)

    def read(self, name: str) -> bytes:
        return self.at[name.upper()].read('MAP', name.upper())

    def fingerprint(self, name: str) -> list[int]:
        """The archive's record of the file (size unpacked, CRC): a changed EDF4.1 file, without reading it."""
        entry = self.at[name.upper()].cpk.index[('MAP', name.upper())]
        return [entry['ExtractSize'], entry['CRC']]


def files_of(name: str) -> list[str]:
    """The Mods paths map `name` installs."""
    weathers = MAPS[name][1]
    return ([f'MAP/{name}.{ext}' for ext in ('MAC', 'MAD', 'RAB')]
            + [f'MAP/{name}_{w}.{ext}' for w in weathers for ext in ('MAE', 'RAB')]
            + [f'MAP/{name}.{n}' for n in NAVMESH])


def convert(name: str, e41: Archives, e6: Archives) -> dict[str, bytes]:
    """Map `name`'s files (rel under Mods -> bytes)."""
    donor, weathers = MAPS[name]
    mac = bigmap.Marc.parse(map_legacy.mac_from_legacy(e41.read(f'{name}.MAC')))
    mac.files = [(n, rmpa_legacy.rmpa_from_legacy(d) if n.lower().endswith('.rmpa') else d) for n, d in mac.files]
    version, members = map_legacy.parse_marc_lenient(e41.read(f'{name}.MAD'))
    out = {f'MAP/{name}.MAC': mac.build(),
           f'MAP/{name}.MAD': bigmap.Marc(version, list(members)).build(),
           f'MAP/{name}.RAB': legacy_assets.convert(f'MAP/{name}.RAB', e41.read(f'{name}.RAB'), map_part=True)}
    for w in weathers:
        for ext in ('MAE', 'RAB'):
            out[f'MAP/{name}_{w}.{ext}'] = e6.read(f'{donor}_{w}.{ext}')
    for n in NAVMESH:
        out[f'MAP/{name}.{n}'] = e6.read(f'{donor}.{n}')
    return out


def _source(name: str, e41: Archives) -> dict[str, list[int]]:
    return {f: e41.fingerprint(f) for f in (f'{name}.MAC', f'{name}.MAD', f'{name}.RAB')}


def _foreign(led: ledger.Ledger, rel: str) -> bool:
    """A file in Mods this install must not write over: one no tool of ours wrote, or another of ours'."""
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    return not owners or any(o != OWNER for o in owners)


def _current(led: ledger.Ledger, stamp: dict, name: str, source: dict[str, list[int]]) -> bool:
    """Map `name` is installed by this VERSION from these 4.1 files, every file still as written."""
    entry = stamp.get(name, {})
    if entry.get('version') != VERSION or entry.get('source') != source:
        return False
    mine = set(led.owned_by(OWNER))
    return all(ledger.key(rel) in mine and os.path.exists(led.disk(rel)) for rel in files_of(name))


NO_EDF41 = '没有找到 EDF4.1（需要安装 EDF4.1 才能转换它的地图）'


def unavailable(root: str, root41: str | None) -> list[tuple[str, str]]:
    """The maps install leaves out before converting anything, [(map, why)]: all of them without EDF4.1, else those
    with a file in Mods that is not ours. Conversion itself can still fail (install's skipped list). The one place
    that decides it: install skips exactly these, and the campaign plan offers only missions whose maps are not here."""
    if not root41:
        return [(name, NO_EDF41) for name in MAPS]
    led = ledger.Ledger(root)
    out: list[tuple[str, str]] = []
    for name in MAPS:
        foreign = [rel for rel in files_of(name) if _foreign(led, rel)]
        if foreign:
            out.append((name, f"Mods 里已有别的 MOD 的文件：{', '.join(foreign)}"))
    return out


def install(root: str, root41: str | None, say: Callable[[str], None] = print) -> tuple[list[str], list[tuple[str, str]]]:
    """Every map of MAPS converted and written as OWNER's; (paths written, [(map, why not)])."""
    if not root41:
        return [], unavailable(root, root41)
    e41, e6 = Archives(root41), Archives(root)
    led = ledger.Ledger(root)
    stamp_path = os.path.join(root, 'Mods', STAMP)
    stamp = load_json(stamp_path, {})
    written: list[str] = []
    skipped = unavailable(root, root41)
    left_out = {name for name, _ in skipped}
    wanted: set[str] = set()
    for name in MAPS:
        if name in left_out:
            continue
        source = _source(name, e41)
        wanted.update(ledger.key(rel) for rel in files_of(name))
        if _current(led, stamp, name, source):
            continue
        say(f'  转换 4.1 地图 {name}（借用 EDF6 {MAPS[name][0]} 的天气与寻路网格）……')
        try:
            files = convert(name, e41, e6)
        except (ValueError, KeyError) as e:
            skipped.append((name, f'转换失败：{e}'))
            wanted.difference_update(ledger.key(rel) for rel in files_of(name))
            continue
        for rel, data in files.items():
            if ledger.key(rel) in set(led.owned_by(OWNER)) and sha256_file(led.disk(rel)) == sha256(data):
                continue
            written.append(led.put(OWNER, rel, data))
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
