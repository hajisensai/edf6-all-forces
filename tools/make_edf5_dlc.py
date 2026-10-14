"""Developer tool: EDF5 Mission Pack 1's content EDF6 lacks, pre-generated into edf5port/dlc/ for tools/edf5_dlc.py (the
installer installs it; players need no EDF5). Needs EDF5 and EDF6 installed; reads their .cpk files only.

    python -B tools/make_edf5_dlc.py [--edf5 DIR] [--edf6 DIR]            writes edf5port/dlc/
    python -B tools/make_edf5_dlc.py --verify [--edf5 DIR] [--edf6 DIR]   the data put together as the installer does

What four of the pack's missions name and EDF6 does not have (make_edf5_campaign.plan skipped them):
  DM015  OBJECT/E510_DLC_FORTRESS.SGO and its ten parts E510_DLC_FORT_PARTS_*.SGO
  DM018, DM020  MAP/IG_DESERT508.MAC (weathers evening, sunset)
  DM019  MAP/NG_DLC_EDFROOM03.MAC (weather fine)

EDF5's map files do not load in EDF6 as they are (measured 2026-10-14, every one of EDF5's 255 MAP files that EDF6 also
ships differs, and these formats moved):
  .MAC   map.mapb / map.mapo laid out anew, map.rmpa another RMPA form: pylib/map_legacy.py + pylib/rmpa_legacy.py, as
         tools/edf41_maps.py converts 4.1's (docs/edf41-map-mac-format.md covers EDF5 too)
  .RAB   models MDB 0x14 (EDF5) -> 0x20 (EDF6): pylib/legacy_assets.py (map_part), textures as they are
  .NAVMESH(_H).NVMD  EDF5 version 0x120, EDF6 0x130; EDF.dll 0xEF335 refuses any other than 0x130 (the .HKT pair: Havok
         2015, every EDF6 one 2020). Taken from the donor EDF6 map instead (DONORS), as edf41_maps does for 4.1's maps:
         the AI's walking routes are the donor's, not this map's (open issue, as for 4.1)
  weather  EDF6 keeps a map's weather (lights, fog, sky) in <map>_<weather>.MAE beside the weather .RAB; EDF5 has no .MAE.
         The donor's .MAE / .RAB pair, whole: the map EDF5 built this one from (its MAC carries the donor's .rmpa:
         ig_base502.rmpa, ig_edfroom02.rmpa), whose EDF6 copy is the developers' own conversion of that weather
  .MAD   collision.hkt is Havok 2015 hkpPhysicsData (EDF6's: 2020 hknpPhysicsSceneData); the plugin migrates hkp
         physics at load (src/legacymap.cpp). Kept, re-packed as edf41_maps does (not proven in game for 2015 files)
  .FMB   version 0x110 in both games (the loader's check, EDF.dll 0x10E8B0): as it is
The fortress SGOs are classic SGOs in both games (so is all of EDF6's MovingFortress family: no DSGO form needed). Each is
EDF6's own counterpart (E510_FORTRESS*.SGO) with what the DLC changed against EDF5's counterpart (rebase): the fortress
its part list, the parts their durability and drop rate. What EDF6 changed (MAB blocks 0x03 -> 0x83, new effect keys,
ragdoll_disable removed) comes with EDF6's file. Their sound banks (TIKYUU5_EN_FORTRESS.ACB, TIKYUU4_EN_4LEGTANK.ACB) are
EDF6's own (SOUND/PC): nothing to map.

Output (tools/edf5_dlc.py reads it): registry.json, a .members.json per RAB, blobs/. A RAB member EDF6 already ships (the
same stored bytes, SHA-256) is not stored: its row names where it is in EDF6. Every committed file is under 50 MB.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, HERE)
import bigmap  # noqa: E402
import edf5_dlc  # noqa: E402
import fmb  # noqa: E402
import legacy_assets  # noqa: E402
import mab_legacy  # noqa: E402
import make_edf5_campaign  # noqa: E402
import make_edf41_objects as m41  # noqa: E402
import map_legacy  # noqa: E402
import mdb  # noqa: E402
import rmpa_legacy  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
from modfiles import sha256  # noqa: E402

EDF5 = r'D:\Steam\steamapps\common\EARTH DEFENSE FORCE 5'
EDF6 = r'D:\Steam\steamapps\common\EARTH DEFENSE FORCE 6'
OUT = os.path.join(ROOT, 'edf5port', 'dlc')
EDF5_ARCHIVES = ('Root.cpk', 'Chunk01.cpk', 'DX11.cpk')
NAVMESH = ('NAVMESH.HKT', 'NAVMESH.NVMD', 'NAVMESH_H.HKT', 'NAVMESH_H.NVMD')
WEATHERS = ('FINE', 'FINECLOUD', 'CLOUDY', 'CLOUDY2', 'EVENING', 'SUNSET', 'NOFOG')
# map -> (the EDF6 map whose weather and navmesh it takes, the weathers its missions load it with: checked against the
# scripts, weathers_of)
DONORS: dict[str, tuple[str, tuple[str, ...]]] = {
    'IG_DESERT508': ('IG_BASE502', ('EVENING', 'SUNSET')),
    'NG_DLC_EDFROOM03': ('IG_EDFROOM02', ('FINE',)),
}
FORTRESS = 'OBJECT/E510_DLC_FORTRESS.SGO'
MISSING = object()


class Archives:
    """A game's archives: every file by its Mods-style path (upper case), the first archive's copy winning."""

    def __init__(self, root: str, names: tuple[str, ...]) -> None:
        self.at: dict[str, tuple[str, rootcpk.Game, tuple[str, str]]] = {}
        for name in names:
            if os.path.isfile(os.path.join(root, name)):
                game = rootcpk.Game(root, name)
                for d, n in game.cpk.index:
                    self.at.setdefault(f'{d}/{n}'.upper(), (name, game, (d, n)))

    def read(self, rel: str) -> bytes:
        _, game, key = self.at[rel.upper()]
        return game.read(*key)

    def archive(self, rel: str) -> str:
        return self.at[rel.upper()][0]


# ------------------------------------------------------------------------------------------- the maps
def weathers_of(e6: Archives) -> dict[str, set[str]]:
    """{map: the weathers EDF5's DLC scripts (EDF6's MISSION/EDF5_OLD_SCRIPT copies) load it with}."""
    text = json.load(open(make_edf5_campaign.TEXT, encoding='utf-8'))
    out: dict[str, set[str]] = {m: set() for m in DONORS}
    for m in text['dlc1']:
        bvm = e6.read(f'MISSION/{make_edf5_campaign.SCRIPTS}/{m["path"]}/MISSION.BVM')
        maps = {r.rsplit('/', 1)[-1].upper().removesuffix('.MAC') for r in make_edf5_campaign.asset_refs(bvm)
                if r.upper().startswith('MAP/')}
        words = {s.upper() for s in make_edf5_campaign.utf16_strings(bvm)} & set(WEATHERS)
        for name in maps & set(out):
            out[name] |= words
    return out


def convert_map(name: str, e5: Archives, e6: Archives) -> tuple[dict[str, bytes], dict[str, dict], list[str]]:
    """Map `name`: ({Mods path: final bytes} of the files made from EDF5's, {Mods path: edf6 entry} of those taken whole
    from the donor, notes)."""
    donor, weathers = DONORS[name]
    notes: list[str] = []
    mac = bigmap.Marc.parse(map_legacy.mac_from_legacy(e5.read(f'MAP/{name}.MAC')))
    mac.files = [(n, rmpa_legacy.rmpa_from_legacy(d) if n.lower().endswith('.rmpa') else d) for n, d in mac.files]
    made = {f'MAP/{name}.MAC': mac.build()}
    problems = bigmap.verify(made[f'MAP/{name}.MAC'])
    if problems:
        raise ValueError(f'{name}.MAC converted fails bigmap.verify: {problems}')
    made[f'MAP/{name}.RAB'] = legacy_assets.convert(f'MAP/{name}.RAB', e5.read(f'MAP/{name}.RAB'), map_part=True)
    mad = e5.read(f'MAP/{name}.MAD')
    version, members = map_legacy.parse_marc_lenient(mad)
    made[f'MAP/{name}.MAD'] = bigmap.Marc(version, list(members)).build()
    notes.append(f'{name}.MAD ' + ('EDF5 bytes as they are' if made[f'MAP/{name}.MAD'] == mad else 're-packed'))
    for rel in sorted(r for r in e5.at if r.startswith('MAP/') and r.endswith('.FMB')
                      and (r[4:-4] == name or r[4:-4].startswith(name + '_'))):   # its ground and far pieces
        data = e5.read(rel)
        fmb.parse(data)   # the reader written from EDF6's loader: a file it cannot read is refused here
        made[rel] = data
    whole: dict[str, dict] = {}
    for w in weathers:
        for ext in ('MAE', 'RAB'):
            whole[f'MAP/{name}_{w}.{ext}'] = _edf6_entry(e6, f'MAP/{donor}_{w}.{ext}')
    for n in NAVMESH:
        whole[f'MAP/{name}.{n}'] = _edf6_entry(e6, f'MAP/{donor}.{n}')
    return made, whole, notes


def _edf6_entry(e6: Archives, rel: str) -> dict:
    return {'kind': 'edf6', 'archive': e6.archive(rel), 'path': rel, 'sha': sha256(e6.read(rel))}


def donor_evidence(name: str, e5: Archives) -> str:
    """How much of the map's own EDF5 weather textures are its donor's EDF5 ones (the same stored bytes)."""
    donor = DONORS[name][0]
    mine, theirs = set(), set()
    for rel in e5.at:
        if rel.startswith(f'MAP/{name}_') and rel.endswith('.RAB'):
            mine |= {sha256(f.stored) for f in mdb.rab_read(e5.read(rel)).files}
        if rel.startswith(f'MAP/{donor}_') and rel.endswith('.RAB'):
            theirs |= {sha256(f.stored) for f in mdb.rab_read(e5.read(rel)).files}
    rmpa = [n for n, _ in map_legacy.parse_marc_lenient(e5.read(f'MAP/{name}.MAC'))[1] if n.endswith('.rmpa')]
    return (f'{name}: {len(mine & theirs)} of its {len(mine)} EDF5 weather textures are {donor}\'s; MAC members '
            f'{rmpa}')


# ------------------------------------------------------------------------------------------- the fortress
def stock_name(rel: str) -> str:
    """The EDF5 / EDF6 stock counterpart of a DLC fortress SGO."""
    return rel.replace('E510_DLC_FORT_PARTS_', 'E510_FORTRESS_PARTS_').replace('E510_DLC_FORTRESS', 'E510_FORTRESS')


def rebase(dlc: bytes, e5: bytes, e6: bytes) -> tuple[bytes, list[str], list[str]]:
    """EDF6's SGO `e6` with every key the DLC SGO `dlc` changed against EDF5's `e5` (taken, or taken out): (bytes, the
    keys taken from the DLC, those of them EDF6 had changed too)."""
    v_d, m_d = sgo.read(dlc)
    v_5, m_5 = sgo.read(e5)
    v_6, m_6 = sgo.read(e6)
    if not v_d == v_5 == v_6:
        raise ValueError(f'SGO versions differ: {v_d:#x} {v_5:#x} {v_6:#x}')
    out = dict(m_6)
    taken, both = [], []
    for k in list(m_5) + [k for k in m_d if k not in m_5]:
        if m_d.get(k, MISSING) == m_5.get(k, MISSING):
            continue
        taken.append(k)
        if m_6.get(k, MISSING) != m_5.get(k, MISSING) and m_6.get(k, MISSING) != m_d.get(k, MISSING):
            both.append(k)
        if k in m_d:
            out[k] = m_d[k]
        else:
            out.pop(k, None)
    return sgo.write_depth_first(v_6, out), taken, both


class E6Games:
    """What make_edf41_objects.check_sounds reads of a game (its Games object): EDF6's files and its root."""

    def __init__(self, e6: Archives, root: str) -> None:
        self.e6, self.edf6_root = e6, root

    def read6(self, rel: str) -> bytes:
        return self.e6.read(rel)


def stock_by_class(e6: Archives) -> tuple[dict[str, dict[str, dict]], set[str]]:
    """EDF6's own top-level OBJECT SGOs by class, and the classes all of whose SGOs are DSGOs (make_edf41_objects.Games
    stock_by_class / dsgo_only_classes, without an EDF4.1)."""
    stock: dict[str, dict[str, dict]] = {}
    formats: dict[str, set[bool]] = {}
    for rel in sorted(e6.at):
        if not (rel.startswith('OBJECT/') and rel.endswith('.SGO') and rel.count('/') == 1):
            continue
        data = e6.read(rel)
        try:
            members = sgo.load(data=data)
        except Exception:   # noqa: BLE001 - an SGO this reader does not parse is no class evidence (as m41's)
            continue
        cls = members.get('xgs_scene_object_class') if isinstance(members, dict) else None
        if isinstance(cls, str):
            stock.setdefault(cls, {})[rel] = members
            formats.setdefault(cls, set()).add(data[:4] == b'DSGO')
    return stock, {c for c, f in formats.items() if f == {True}}


def fortress(e5: Archives, e6: Archives, root6: str) -> tuple[dict[str, bytes], list[str], list[str]]:
    """The DLC fortress and its parts: ({Mods path: bytes}, the EDF6 files they name, notes)."""
    rels = sorted(r for r in e5.at if r.startswith('OBJECT/E510_DLC_FORT') and r.endswith('.SGO'))
    if FORTRESS not in rels:
        raise ValueError(f'EDF5 has no {FORTRESS}')
    made: dict[str, bytes] = {}
    notes: list[str] = []
    for rel in rels:
        data, taken, both = rebase(e5.read(rel), e5.read(stock_name(rel)), e6.read(stock_name(rel)))
        made[rel] = data
        notes.append(f'{rel}: EDF6 {stock_name(rel)} + the DLC\'s {taken}' + (f' (EDF6 changed {both} too: the DLC\'s '
                                                                               'value kept)' if both else ''))
    return made, check_objects(made, e6, root6), notes


def check_battery(data: bytes, rel: str) -> None:
    """ValueError when a MovingFortress_Battery SGO lacks a name the class reads unchecked: make_edf41_objects.check_battery's
    keys and locators (the bullet key battery_config[1][0], the charge effect [2], AIM_TARGET / AIM / AIM_AXIS, the muzzle
    [0], each fort_parts_destroy_info[5] locator), the locators looked up in every list of the MAB. That function reads
    list 0 only, which fits the 4.1 parts it builds (it adds its locators there) but not EDF6's own 3-list MABs: EDF6's
    E510_FORTRESS_PARTS_SMALLCANNON*.SGO keep their destroy locators (爆破A ...) in list 2, and it refuses those too."""
    members = sgo.read(data)[1]
    cfg = members.get('battery_config')
    if not isinstance(cfg, list) or len(cfg) != 4:
        raise ValueError(f'{rel}: battery_config is not [muzzle, fire row, charge effect, charge frames]')
    for key in (cfg[1][0], cfg[2], 'attack', 'aim_config', 'ragdoll', 'se_table', 'fort_parts_destroy_info'):
        if key not in members:
            raise ValueError(f'{rel}: no key {key}, which the class reads unchecked')
    names = {loc.name for _lst, loc in mab_legacy.mab_locators(members['animation_model'][2])}
    want = {'AIM_TARGET', 'AIM', 'AIM_AXIS', cfg[0], *members['fort_parts_destroy_info'][5]}
    if want - names:
        raise ValueError(f'{rel}: its MAB has no locator {sorted(want - names)}')


def with_banks(data: bytes, more: list[str]) -> bytes:
    """The classic SGO `data` with `more` sound banks in its game_sound (for check_sounds only: the banks its spawner
    has loaded)."""
    if not more:
        return data
    version, members = sgo.read(data)
    members['game_sound'] = list(dict.fromkeys([*m41.strings(members.get('game_sound')), *more]))
    return sgo.write_depth_first(version, members)


def check_objects(made: dict[str, bytes], e6: Archives, root6: str) -> list[str]:
    """ValueError unless the SGOs `made` pass make_edf41_objects' class checks against EDF6's own (format, keys every
    stock SGO of the class has, MAB block, sounds, a battery's names) and every file they name is EDF6's or theirs;
    the EDF6 files they name."""
    stock, dsgo_only = stock_by_class(e6)
    games = E6Games(e6, root6)
    needs: set[str] = set()
    # A part plays its sounds with its fortress's banks loaded too (the parts list game_sound tikyuu4_en_4LegTank only,
    # their damage cues are in the fortress's tikyuu5_en_fortress, as EDF6's own E510_FORTRESS_PARTS_*.SGO)
    banks: dict[str, list[str]] = {}
    for rel, data in made.items():
        members = sgo.load(data=data)
        for s in m41.strings(members.get('parts_setting')):
            if m41.is_ref(s):
                banks.setdefault(m41.ref_rel(s), []).extend(m41.strings(members.get('game_sound')))
    for rel, data in made.items():
        m41.check_class_format(dsgo_only, rel, data)
        m41.check_class_keys(stock, rel, data)
        m41.check_model_mab(stock, rel, data)
        m41.check_sounds(games, with_banks(data, banks.get(rel, [])), rel)
        if sgo.load(data=data).get('xgs_scene_object_class') == 'MovingFortress_Battery':
            check_battery(made[rel], rel)
        for s in m41.strings(sgo.load(data=data)):
            if not m41.is_ref(s):
                continue
            ref = m41.ref_rel(s)
            if ref in made or not os.path.splitext(ref)[1]:
                continue
            if ref.endswith('.ACB'):
                if not os.path.isfile(os.path.join(root6, 'SOUND', 'PC', ref.rsplit('/', 1)[-1])):
                    raise ValueError(f'{rel}: names {ref}, which EDF6 has not')
                continue
            if ref not in e6.at:
                raise ValueError(f'{rel}: names {ref}, which EDF6 has not')
            needs.add(ref)
    return sorted(needs)


# ------------------------------------------------------------------------------------------- de-duplicating
def edf6_members(e6: Archives, want: dict[int, set[str]]) -> dict[str, list]:
    """{sha: [[archive, RAB path, index, name], ...]} of the members of EDF6's MAP RABs whose stored bytes have one of the
    SHA-256s in `want` ({size: shas}: only members of those sizes are hashed)."""
    out: dict[str, list] = {}
    for rel in sorted(r for r in e6.at if r.startswith('MAP/') and r.endswith('.RAB')):
        for i, f in enumerate(mdb.rab_read(e6.read(rel)).files):
            if len(f.stored) in want:
                sha = sha256(f.stored)
                if sha in want[len(f.stored)]:
                    out.setdefault(sha, []).append([e6.archive(rel), rel, i, f.name])
    return out


def pick_sources(needed: list[str], found: dict[str, list]) -> dict[str, list]:
    """One source per member, the RABs covering the most of `needed` first (fewer EDF6 archives read at install)."""
    cover: dict[tuple[str, str], int] = {}
    for sha in set(needed):
        for src in found.get(sha, []):
            cover[(src[0], src[1])] = cover.get((src[0], src[1]), 0) + 1
    return {sha: max(found[sha], key=lambda s: (cover[(s[0], s[1])], s[1])) for sha in set(needed) if sha in found}


# ------------------------------------------------------------------------------------------- writing
def generate(edf5: str, edf6: str, out: str = OUT) -> dict:
    e5 = Archives(edf5, EDF5_ARCHIVES)
    e6 = Archives(edf6, edf5_dlc.ARCHIVES)
    used = weathers_of(e6)
    for name, (donor, weathers) in DONORS.items():
        if not used[name] <= set(weathers):
            raise ValueError(f'{name}: the scripts load weathers {sorted(used[name])}, DONORS gives {weathers}')
        lacking = [w for w in weathers if f'MAP/{donor}_{w}.MAE' not in e6.at]
        if lacking:
            raise ValueError(f'{name}: the donor {donor} has no weather {lacking} in EDF6')
    groups: dict[str, dict] = {}
    files: dict[str, dict] = {}
    made: dict[str, bytes] = {}
    notes: list[str] = []
    for name in DONORS:
        print(f'converting {name} ...', flush=True)
        mine, whole, n = convert_map(name, e5, e6)
        notes += n + [donor_evidence(name, e5)]
        made.update(mine)
        files.update(whole)
        groups[name] = {'kind': 'map', 'missions': [], 'files': sorted([*mine, *whole]), 'needs': []}
    fort, needs, n = fortress(e5, e6, edf6)
    notes += n
    made.update(fort)
    groups[FORTRESS] = {'kind': 'object', 'missions': [], 'files': sorted(fort), 'needs': needs}
    text = json.load(open(make_edf5_campaign.TEXT, encoding='utf-8'))
    for m in text['dlc1']:
        bvm = e6.read(f'MISSION/{make_edf5_campaign.SCRIPTS}/{m["path"]}/MISSION.BVM')
        refs = {r.replace('\\', '/').upper() for r in make_edf5_campaign.asset_refs(bvm)}
        for g in groups:
            if (f'MAP/{g}.MAC' if groups[g]['kind'] == 'map' else g) in refs:
                groups[g]['missions'].append(m['path'])
    rabs = {rel: mdb.rab_read(data) for rel, data in made.items() if rel.endswith('.RAB')}
    want: dict[int, set[str]] = {}
    for rab in rabs.values():
        for f in rab.files:
            want.setdefault(len(f.stored), set()).add(sha256(f.stored))
    print('looking for the members in EDF6 ...', flush=True)
    found = edf6_members(e6, want)
    sources = pick_sources([sha256(f.stored) for r in rabs.values() for f in r.files], found)
    stored, stats = edf5_dlc.pack(groups, made, files, sources, notes)
    reg = json.loads(stored[edf5_dlc.REGISTRY].decode('utf-8'))
    if os.path.isdir(out):
        shutil.rmtree(out)
    for rel, data in stored.items():
        path = os.path.join(out, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
    sizes = sorted(((len(d), rel) for rel, d in stored.items()), reverse=True)
    print(f"RAB members: {stats['shared'] / 2**20:.1f} MB read from EDF6 at install, "
          f"{stats['own'] / 2**20:.1f} MB EDF5-only")
    print(f'committed: {len(stored)} files, {sum(s for s, _ in sizes) / 2**20:.1f} MB; largest {sizes[0][1]} '
          f'{sizes[0][0] / 2**20:.1f} MB')
    if sizes[0][0] >= 50 * 2**20:
        raise ValueError(f'{sizes[0][1]} is 50 MB or more')
    for line in notes:
        print(' ', line)
    return {'made': {rel: sha256(d) for rel, d in made.items()}, 'registry': reg}


def verify(edf5: str, edf6: str) -> int:
    """The committed data put together the way the installer does it (tools/edf5_dlc.py build, the player's EDF6 at
    `edf6`, no EDF5) against the conversion run again from EDF5: every file the same bytes, and what each is of EDF5's
    own file (the same bytes, converted, or an EDF6 donor's). The fortress SGOs through the class checks again. Reads
    the games only; nothing written."""
    e5 = Archives(edf5, EDF5_ARCHIVES)
    e6 = Archives(edf6, edf5_dlc.ARCHIVES)
    files, skipped = edf5_dlc.build(edf6)
    bad = 0
    for g, why in skipped:
        print('LEFT OUT', g, why)
        bad += 1
    expect: dict[str, str] = {}
    for name in DONORS:
        mine, whole, _ = convert_map(name, e5, e6)
        expect.update({rel: sha256(d) for rel, d in mine.items()})
        expect.update({rel: w['sha'] for rel, w in whole.items()})
    fort, _, _ = fortress(e5, e6, edf6)
    expect.update({rel: sha256(d) for rel, d in fort.items()})
    sgos: dict[str, bytes] = {}
    for rel in files:
        data = files[rel]   # edf5_dlc.assemble: refused unless it is the registry's SHA-256
        entry = files.reg['files'][rel]
        same = sha256(data) == expect.get(rel)
        if entry['kind'] == 'edf6':
            what = f"EDF6 donor {entry['path']}"
        elif rel.upper() in e5.at:
            what = 'EDF5 bytes as they are' if data == e5.read(rel) else 'converted from EDF5'
        else:
            what = 'no EDF5 file'
        print(f"{'ok ' if same else 'BAD'} {rel} {len(data)} bytes, {what}")
        bad += not same
        if rel.endswith('.SGO'):
            sgos[rel] = data
    check_objects(sgos, e6, edf6)
    print(f'fortress SGOs pass the class checks ({len(sgos)})')
    missing = sorted(set(expect) - set(files))
    for rel in missing:
        print('NOT BUILT', rel)
    return 1 if bad or missing else 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--edf5', default=EDF5)
    ap.add_argument('--edf6', default=EDF6)
    ap.add_argument('--verify', action='store_true', help='check the committed data instead of writing it')
    a = ap.parse_args(argv)
    if a.verify:
        return verify(a.edf5, a.edf6)
    generate(a.edf5, a.edf6)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
