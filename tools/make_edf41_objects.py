"""Developer tool: EDF4.1's objects (enemies, NPCs) that the 4.1 missions name and EDF6 lacks, in the form EDF6 loads
-> edf41port/objects/<Mods path> and edf41port/objects.json (committed; plan P4, docs/edf5-weapons-plan.md).

  python -B tools/make_edf41_objects.py [--edf41 DIR] [--edf6 DIR]

Runs once on a machine with both games; the install copies the files into Mods under 4.1's own names (the 4.1
missions' scripts name them so: app:/Object/AntHill301.sgo ...), so no EDF4.1 is needed on the player's machine.

Which: ROOTS, the objects of 4.1's missions whose class EDF6 still has (some EDF6 object SGO uses it), and those of
ALIASES, whose class EDF6 dropped for one that does the same job: the 4.1 object under that EDF6 class, with the SGO
keys and CAS players (cas_graft) the class looks up that 4.1's file lacks, taken from an EDF6 object of the class (plan
P4 tier 2), and those of STAND_INS, whose 4.1 file shares no key with what EDF6 does the job with: an EDF6 object
in their place, under 4.1's name (with a look of its own: a model the tools already ship, SHARED). The rest (Hector,
the UFOs, the motherships, UfoRobo, DragonBig) come later. Each root's files: every app:/ path it names, and every one those name (SGOs), that EDF6 lacks at that path
(EDF6's own copy, when it has one, is what the game loads; every archive of both games counted: Root.cpk, ChunkNN,
DX11). A path 4.1 itself does not have is not needed (4.1 runs without it: every *_LIGHT.MRAB its objects name, none
in any of its archives), nor one without an extension (a folder prefix: OBJECT/ARMYSOLDIER/APPEAL). How each comes
over:
  .SGO          4.1's file with its MAB blocks in EDF6's form (pylib/legacy_sgo.py; EDF6 reads 4.1's SGO format)
  .RAB / .MRAB  models and animations converted (pylib/legacy_assets.py), textures as they are
  .CAS          converted (pylib/cas_legacy.py through legacy_assets)
  .SHKT         4.1's Havok 2014 packfile as it is (EDF.dll keeps the old readers; real-device check in the plan)
  .ACB          a sound bank, a loose file in <game>/SOUND/PC; EDF6 ships 4.1's (TIKYUU4_*): checked, not copied
A root with a file none of these carry, or a converter refuses, is left out (listed under 'skipped' with why).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from dataclasses import dataclass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import cas_graft  # noqa: E402
import legacy_assets  # noqa: E402
import legacy_sgo  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

STEAM = r'D:\Steam\steamapps\common'
OUT = os.path.join(HERE, '..', 'edf41port', 'objects.json')
FILES = os.path.join(HERE, '..', 'edf41port', 'objects')
ARCHIVES = ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk', 'DX11.cpk')
# The 4.1 missions' objects EDF6 lacks whose class it has, with how many of 4.1's 98 missions name each
# (2026-10-10, the missions' BVM scripts scanned; jobs/4bf89026/tmp/bvm41/assets41.json).
ROOTS: dict[str, int] = {
    'ANTHILL301.SGO': 14,
    'DEIROI401.SGO': 9, 'DEIROI401S.SGO': 6, 'DEIROI401_2.SGO': 4, 'DEIROI401L.SGO': 4, 'DEIROI401_3.SGO': 2,
    'ALIENTRAILER401.SGO': 6, 'ALIENTRAILER401_L.SGO': 4, 'ALIENTRAILER401_FIXED.SGO': 3,
    'ALIENTRAILER401_FIXEDXL.SGO': 3, 'ALIENTRAILER401_XL.SGO': 1,
    'AIARMYSOLDIER_OMG.SGO': 5, 'AIARMYSOLDIER_OMG_LEADER.SGO': 5,
    'VEHICLE501_FORTRESSROBO.SGO': 1, 'VEHICLE501_FORTRESSROBO_AI.SGO': 1,
    'UFOCARRIER301.SGO': 15, 'UFOCARRIER401.SGO': 15,
    'VEHICLE301_TANK_AI.SGO': 13,
}


@dataclass(frozen=True)
class Alias:
    cls: str                  # the EDF6 class the 4.1 object goes under
    template: str             # an EDF6 object of that class (OBJECT/<name>): the keys and CAS players 4.1 lacks
    players: tuple[str, ...]  # CAS players (anmgroups) the class resolves that 4.1's CAS lacks (crash on spawn)


# 4.1's class -> EDF6's (jobs/4bf89026/tmp/alias41: the classes EDF.dll registers, the names each resolves).
ALIASES: dict[str, Alias] = {
    # UfoCarrier301 / 401 -> UfoCarrier508 (EDF5's carrier): the same job (hover, open the hatches, drop the enemies
    # the mission names); 508 also turns two rings 4.1's carriers do not have: its players in_ring / out_ring grafted
    # with clips that move nothing; keys UfoCarrier_InRingRotationSpeed, se_fly_name, se_list,
    # game_object_destroy_score_adjust from E508_CARRIER (its sounds are in tikyuu4_en_UFOcarrier301.acb, 4.1's bank).
    'UFOCARRIER301.SGO': Alias('UfoCarrier508', 'E508_CARRIER.SGO', ('in_ring', 'out_ring')),
    'UFOCARRIER401.SGO': Alias('UfoCarrier508', 'E508_CARRIER.SGO', ('in_ring', 'out_ring')),
}


def strings(v: object) -> list[str]:
    if isinstance(v, str):
        return [v]
    if isinstance(v, dict):
        return [s for x in v.values() for s in strings(x)]
    if isinstance(v, list):
        return [s for x in v for s in strings(x)]
    return []


def rel_of(app: str) -> str:
    """app:/Object/AntHill301.rab -> OBJECT/ANTHILL301.RAB (the Mods path, upper case)."""
    return app.split(':/', 1)[-1].replace('\\', '/').strip('/').upper()


def archives(root: str) -> dict[str, rootcpk.Game]:
    """{Mods-style path: the archive of the game at `root` holding it}, the first archive's copy winning (Root.cpk)."""
    out: dict[str, rootcpk.Game] = {}
    for name in ARCHIVES:
        if os.path.isfile(os.path.join(root, name)):
            game = rootcpk.Game(root, name)
            for d, n in game.cpk.index:
                out.setdefault(f'{d}/{n}'.upper(), game)
    return out


class Games:
    def __init__(self, edf41: str, edf6: str) -> None:
        self.edf6_root = edf6
        self.in4 = archives(edf41)
        self.in6 = archives(edf6)
        self.have6 = set(self.in6)

    def sound(self, rel: str) -> bool:
        return os.path.isfile(os.path.join(self.edf6_root, 'SOUND', 'PC', rel.rsplit('/', 1)[-1]))

    def read4(self, rel: str) -> bytes:
        return self.in4[rel].read(*rel.rsplit('/', 1))

    def read6(self, rel: str) -> bytes:
        return self.in6[rel].read(*rel.rsplit('/', 1))


def closure(games: Games, root: str) -> tuple[list[str], list[str]]:
    """(the files `root` needs that EDF6 lacks, in walk order; why it cannot come over, [] when it can)."""
    need: list[str] = []
    why: list[str] = []
    todo, seen = [root], set()
    while todo:
        rel = todo.pop(0)
        if rel in seen:
            continue
        seen.add(rel)
        if not os.path.splitext(rel)[1]:
            continue   # a folder prefix
        if rel.endswith('.ACB'):
            if not games.sound(rel):
                why.append(f'{rel}: EDF6 has no such sound bank')
            continue
        if rel in games.have6 and rel != root:
            continue
        if rel not in games.in4:
            continue   # 4.1 runs without it
        need.append(rel)
        if rel.endswith('.SGO'):
            try:
                members = sgo.load(data=games.read4(rel))
            except Exception as e:  # noqa: BLE001 - a 4.1 file sgo does not read: refused, said why
                why.append(f'{rel}: {e!r}')
                continue
            todo += [rel_of(s) for s in strings(members) if s.lower().startswith('app:/')]
    return need, why


def convert(games: Games, rel: str) -> bytes:
    data = games.read4(rel)
    ext = os.path.splitext(rel)[1]
    if ext == '.SGO':
        return legacy_sgo.convert(data)
    if ext in ('.RAB', '.MRAB', '.CAS'):
        return legacy_assets.convert(rel, data)
    if ext == '.SHKT':
        return data
    raise ValueError(f'no conversion for {ext} files')


@dataclass(frozen=True)
class StandIn:
    template: str             # the EDF6 object in its place (OBJECT/<name>, 4.1's SGO format)
    model: tuple[str, str]    # its animation_model[0]: (RAB, MDB)


# Files these need that the tools already ship elsewhere: Mods path -> path under the tools' data (not copied again).
SHARED: dict[str, str] = {'OBJECT/V505_TANKEDF4.MRAB': 'edf5port/assets/OBJECT/V505_TANKEDF4.MRAB'}

STAND_INS: dict[str, StandIn] = {
    # 4.1's AI tank (13 missions): class VehicleTank301, keys none of which EDF6's tank (Vehicle505_Tank) has. EDF6's
    # AI tank on EDF5's Blacker No.4.1 model (the same tank, EDF5's remake; P3 ships it converted, SHARED).
    'VEHICLE301_TANK_AI.SGO': StandIn('V505_TANK_AI.SGO', ('app:/Object/v505_tankedf4.mrab', 'v505_tankedf4.mdb')),
}


def stand_in(games: Games, s: StandIn) -> tuple[bytes, list[str]]:
    """(the SGO in its place, the files it needs EDF6 lacks: SHARED ones)."""
    version, members = sgo.read(games.read6('OBJECT/' + s.template))
    am = members['animation_model']
    am[0] = [s.model[0], s.model[1]]
    need = [r for r in (rel_of(s.model[0]),) if r not in games.have6]
    missing = [r for r in need if r not in SHARED]
    if missing:
        raise ValueError(f'{missing}: neither EDF6 nor the tools have it')
    return sgo.write_depth_first(version, members), need


def alias(games: Games, root: str, files: dict[str, bytes], a: Alias) -> None:
    """In `files` (the converted closure of OBJECT/`root`): its SGO under class a.cls with a.template's keys it
    lacks, its CAS with a.template's players it lacks (when the CAS is one of the files; else EDF6's own is used)."""
    rel = 'OBJECT/' + root
    version, members = sgo.read(files[rel])
    t_version, theirs = sgo.read(games.read6('OBJECT/' + a.template))
    if t_version != version:
        raise ValueError(f'{a.template}: SGO {t_version:#x}, not {version:#x} as {root}')
    members['xgs_scene_object_class'] = a.cls
    for k, v in theirs.items():
        members.setdefault(k, v)
    files[rel] = sgo.write_depth_first(version, members)
    am = members.get('animation_model')
    cas = rel_of(am[1]) if isinstance(am, list) and len(am) > 1 and isinstance(am[1], str) else None
    if a.players and cas in files:
        t_am = theirs['animation_model']
        files[cas] = cas_graft.graft(files[cas], games.read6(rel_of(t_am[1])), list(a.players))


def build(edf41: str, edf6: str) -> tuple[dict, dict[str, bytes]]:
    games = Games(edf41, edf6)
    objects, skipped, files = [], [], {}
    shared: dict[str, str] = {}
    for root, s in STAND_INS.items():
        rel = 'OBJECT/' + root
        data, need = stand_in(games, s)
        files[rel] = data
        for r in need:
            shared[r] = SHARED[r]
        objects.append({'object': rel, 'class': sgo.load(data=data).get('xgs_scene_object_class'),
                        'missions': ROOTS.get(root, 0), 'files': [rel] + need, 'stand_in': s.template})
    for root, uses in ROOTS.items():
        rel = 'OBJECT/' + root
        if root in STAND_INS:
            continue
        if rel in games.have6:
            skipped.append({'object': rel, 'reason': 'EDF6 has it'})
            continue
        need, why = closure(games, rel)
        out: dict[str, bytes] = {}
        for r in need:
            if why:
                break
            try:
                out[r] = files[r] if r in files else convert(games, r)
            except ValueError as e:
                why.append(f'{r}: {e}')
        if not why and root in ALIASES:
            try:
                alias(games, root, out, ALIASES[root])
            except ValueError as e:
                why.append(f'alias {ALIASES[root].cls}: {e}')
        if why:
            skipped.append({'object': rel, 'reason': '; '.join(why)})
            continue
        clash = [r for r in out if r in files and files[r] != out[r]]
        if clash:   # two objects needing different bytes for one file (a CAS grafted for one class only)
            skipped.append({'object': rel, 'reason': 'files another object needs differently: ' + ', '.join(clash)})
            continue
        files.update(out)
        cls = sgo.load(data=out[rel]).get('xgs_scene_object_class')
        objects.append({'object': rel, 'class': cls, 'missions': uses, 'files': need})
    hashes = {r: hashlib.sha256(b).hexdigest() for r, b in files.items()}
    for r, path in shared.items():
        with open(os.path.join(HERE, '..', *path.split('/')), 'rb') as f:
            hashes[r] = hashlib.sha256(f.read()).hexdigest()
    data = {'source': 'EDF4.1 Root.cpk; tools/make_edf41_objects.py', 'objects': objects, 'skipped': skipped,
            'files': dict(sorted(hashes.items())), 'shared': shared}
    return data, files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--edf41', default=os.path.join(STEAM, 'Earth Defense Force 4.1'))
    ap.add_argument('--edf6', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 6'))
    a = ap.parse_args()
    data, files = build(a.edf41, a.edf6)
    shutil.rmtree(FILES, ignore_errors=True)
    for rel, b in files.items():
        path = os.path.join(FILES, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(b)
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write('\n')
    size = sum(len(b) for b in files.values())
    print(f'{OUT}: {len(data["objects"])} objects, {len(files)} files ({size / 1e6:.1f} MB), {len(data["skipped"])} skipped')
    for s in data['skipped']:
        print('  skipped', s['object'], s['reason'][:200])
    return 0


if __name__ == '__main__':
    sys.exit(main())
