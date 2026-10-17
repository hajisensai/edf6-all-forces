"""The friendly NPCs EDF4.1's story missions create, and where EDF6 gets each from (docs/feedback-2026-10-17-edf41-npc.md).

    spawns(bvm) -> list[Spawn]     every friendly creation call of a mission script (EDF5's form, as edf41port ships it)
    FRIENDS: Mods path of every SGO those calls name -> Friend (where the installed game reads it, and its class)

The story's scripts are EDF5's recompile of 4.1's (tools/make_edf41_missions.py), run by EDF6's BVM executor (dispatcher
EDF.dll 0x20FF70; RVAs of TimeDateStamp 0x678CCB46, static reading 2026-10-17). Its table-0 natives that create friends:
  1010 CreateFriend(point, sgo, level, bool)                           -> 0x219720
  1011 CreateFriendSquad(point, radius, leader sgo, member sgo, count, level, recruitable) -> 0x21A290
  1012 CreateFriendGroup(point, radius, sgo, count, level, bool)       -> 0x21A010 (members through 0x21BB40)
  1022 / 1023 a transporter formation and its cargo (five strings, a float) -> 0x21D900 / 0x21A9E0
1010..1012 resolve the point by name in the mission's RMPA (0x6F83B0: 0x2197F2, 0x21A38A, 0x21BC5D); a name it does not
find creates nothing (1011 returns -1 at 0x21A3E1). 1011 and 1012 then put each soldier on the ground with a map ray
(0x221DC0: from up to 20 m above the point down to y = -1000, layer 22); no ground under the point creates nothing
either (1011 at 0x21A41D, the members at 0x21BEB0). The same probe places the players (0x22B5E5), so a map the player
stands on answers it (inferred; not run).

The SGO each call names is loaded from the path as it is: the EDF6 archives' file when EDF6 has one there ('stock'),
the port's (edf41port/objects, tools/make_edf41_objects.py) otherwise ('ported'). EDF6 keeps EDF4.1's names for its own
soldiers (AiArmySoldier_AF ... AiPaleWing_LR), so a 4.1 mission's rangers, fencers and wing divers are EDF6's: EDF6's
models, colours and AI weapons under 4.1's names, the same class. EDF6's own missions spawn the same files, so a 4.1
look cannot go under these paths without changing EDF6's own missions too.
"""
from __future__ import annotations

from dataclasses import dataclass

import bvm41

NAT = bvm41.OP_NAT0
CREATE_FRIEND, CREATE_SQUAD, CREATE_GROUP, TRANSPORTER, TRANSPORTER2 = 1010, 1011, 1012, 1022, 1023
NATIVES: dict[int, str] = {CREATE_FRIEND: 'CreateFriend', CREATE_SQUAD: 'CreateFriendSquad',
                           CREATE_GROUP: 'CreateFriendGroup', TRANSPORTER: 'friend transporter',
                           TRANSPORTER2: 'friend transporter (cargo)'}
# the natives whose first argument is a point the executor looks up in the mission's RMPA (0x6F83B0)
AT_POINT = frozenset({CREATE_FRIEND, CREATE_SQUAD, CREATE_GROUP})
# the natives that put each soldier on the ground with a map ray (0x221DC0)
GROUND_PROBE = frozenset({CREATE_SQUAD, CREATE_GROUP})


@dataclass(frozen=True)
class Friend:
    source: str   # 'stock': EDF6's own file at this path; 'ported': edf41port/objects (tools/make_edf41_objects.py)
    cls: str      # xgs_scene_object_class of the file the game loads


def _soldiers(prefix: str, kinds: tuple[str, ...], cls: str) -> dict[str, Friend]:
    return {f'OBJECT/{prefix}_{k}{lead}.SGO': Friend('stock', cls) for k in kinds for lead in ('', '_LEADER')}


# Every friendly SGO the 89 story missions' creation calls name (2026-10-17: 34 files, 86 missions). 'stock' ones are
# EDF6's own (each also spawned by EDF6's own missions but AIPALEWING_TB, which has every key its stock siblings have:
# make_edf41_objects stale_stock);
# the ported ones are the 4.1 files EDF6 lacks, or holds stale (TRANSPORTER401_FORMATION, make_edf41_objects
# STOCK_OVERRIDES), and the 4.1 tank (a stand-in: EDF6's V505 AI tank under 4.1's name and look).
FRIENDS: dict[str, Friend] = {
    **_soldiers('AIARMYSOLDIER', ('AF', 'FL', 'RL', 'SG', 'SN'), 'AssultSoldier'),
    **_soldiers('AIHEAVYARMOR', ('SC', 'SMC', 'SP'), 'HeavyArmor'),
    **_soldiers('AIPALEWING', ('IZN', 'LANCE', 'LR', 'MS', 'TB'), 'PaleWing'),
    'OBJECT/AIARMYSOLDIER_OMG.SGO': Friend('ported', 'AssultSoldier'),
    'OBJECT/AIARMYSOLDIER_OMG_LEADER.SGO': Friend('ported', 'AssultSoldier'),
    'OBJECT/VEHICLE301_TANK_AI.SGO': Friend('ported', 'Vehicle505_Tank'),
    'OBJECT/VEHICLE402_ROCKET_AI.SGO': Friend('stock', 'Vehicle402_Rocket'),
    'OBJECT/VEHICLE403_TANK_AI.SGO': Friend('stock', 'Vehicle403_Tank'),
    'OBJECT/VEHICLE404_BIGTANK_AI.SGO': Friend('stock', 'Vehicle404_Tank'),
    'OBJECT/VEHICLE501_FORTRESSROBO.SGO': Friend('ported', 'Vehicle501_FortressRobo'),
    'OBJECT/VEHICLE501_FORTRESSROBO_AI.SGO': Friend('ported', 'Vehicle501_FortressRobo'),
    'OBJECT/TRANSPORTER401_FORMATION.SGO': Friend('ported', 'Transporter_Formation'),
}


@dataclass(frozen=True)
class Spawn:
    func: str                 # the script function
    native: int               # table-0 id (NATIVES)
    point: str | None         # the point name (AT_POINT natives, when the script passes a literal)
    sgos: tuple[str, ...]     # the SGOs it names, as Mods paths (OBJECT/AIARMYSOLDIER_AF.SGO)


def rel_of(app: str) -> str:
    """app:/Object/AiArmySoldier_AF.sgo -> OBJECT/AIARMYSOLDIER_AF.SGO (tools/make_edf41_objects.py rel_of)."""
    return app.split(':/', 1)[-1].replace('\\', '/').strip('/').upper()


def spawns(bvm: bytes) -> list[Spawn]:
    """Every friendly creation call of a mission script in EDF5's form (bvm41.parse), with its literal arguments."""
    prog = bvm41.parse(bvm)
    out = []
    for func, _i, x, args in bvm41.native_calls(prog, {(NAT, n) for n in NATIVES}):
        lits = [a.s for a in args]
        point = lits[0] if x.v in AT_POINT else None
        sgos = tuple(rel_of(s) for s in lits if s is not None and s.lower().endswith('.sgo'))
        out.append(Spawn(func, x.v, point, sgos))
    return out


def unknown(spawned: list[Spawn]) -> list[str]:
    """The SGOs `spawned` names that FRIENDS does not list: a friend nobody checked comes from where?"""
    return sorted({s for sp in spawned for s in sp.sgos} - set(FRIENDS))
