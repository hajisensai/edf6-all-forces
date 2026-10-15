"""Developer tool: EDF4.1's objects (enemies, NPCs) that the 4.1 missions name and EDF6 lacks, in the form EDF6 loads
-> edf41port/objects/<Mods path> and edf41port/objects.json (committed; plan P4, docs/edf5-weapons-plan.md).

  python -B tools/make_edf41_objects.py [--edf41 DIR] [--edf6 DIR]

Runs once on a machine with both games; the install copies the files into Mods under 4.1's own names (the 4.1
missions' scripts name them so: app:/Object/AntHill301.sgo ...), so no EDF4.1 is needed on the player's machine.

Which: ROOTS, the objects of 4.1's missions whose class EDF6 still has (some EDF6 object SGO uses it), and those of
ALIASES, whose class EDF6 dropped for one that does the same job: the 4.1 object under that EDF6 class, with the SGO
keys and CAS players (cas_graft) the class looks up that 4.1's file lacks, taken from an EDF6 object of the class (plan
P4 tier 2), and those of STAND_INS, whose 4.1 file shares no key with what EDF6 does the job with: an EDF6 object
in their place, under 4.1's name (with a look of its own: a model the tools already ship, SHARED). The rest (the other
Hector variants, the UFOs, the motherships, UfoRobo, DragonBig) come later. Each root's files: every app:/ path it names, and every one those name (SGOs), that EDF6 lacks at that path
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

DLC_ROOTS: the objects EDF4.1's two DLC mission packs spawn (mp01:/ mp02:/, read from <EDF4.1>/DLC through pylib/
edf41dlc.py), under OBJECT/EDF41MP01/ and OBJECT/EDF41MP02/; the mounted paths their SGOs name are rewritten to those
folders, and a story file EDF6 holds as another file (shadowed: DragonSmall401.mrab is EDF5's frog dragon in EDF6) moves
into the pack's folder with them.

STOCK_OVERRIDES: objects EDF6's archives hold, left over from 4.1 (EDF6's own missions never spawn them), whose class
EDF6 has since changed: the stock file lacks keys the class reads unchecked and crashes on spawn (Transporter401_
Formation: se_preset_list, EDF+0x5E9A90, M-P003 2026-10-13). Such a root is built like any other (4.1's SGO under an
EDF6 template of the class) and shipped under the same Mods path, so it replaces the stock file. build() checks every
SGO the 4.1 missions spawn (spawned_objects) that EDF6's archives serve: stale_stock refuses one that is not replaced.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import re
import shutil
import struct
import sys
from collections.abc import Callable
from dataclasses import dataclass, replace
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import acb  # noqa: E402
import cas_graft  # noqa: E402
import cas_pose  # noqa: E402
import dsgo  # noqa: E402
import edf41dlc  # noqa: E402
import legacy_assets  # noqa: E402
import legacy_sgo  # noqa: E402
import mab_legacy  # noqa: E402
import rootcpk  # noqa: E402
import mdb  # noqa: E402
import rabpool  # noqa: E402
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
    'UFO301.SGO': 18, 'UFO301ACE.SGO': 6, 'UFO401.SGO': 15,
    'TRANSPORTER401_FORMATION.SGO': 4,
}
# Stock SGOs the 4.1 missions name that no player reaches, with why (stale_stock would otherwise refuse them).
STOCK_EXEMPT: dict[str, str] = {
    'OBJECT/ROUTETEST.SGO': "class RouteTest is not registered in EDF.dll at all; only 4.1's developer test mission T_AIROUTE "
                            "spawns it (no story or mission-list entry)",
}
# Roots EDF6's archives also hold, as a stale 4.1 leftover its classes cannot load: root -> why the stock file is wrong.
STOCK_OVERRIDES: dict[str, str] = {
    'TRANSPORTER401_FORMATION.SGO': "no se_preset_list: Transporter_Formation's constructor passes it unchecked to the "
                                    "sound system (EDF+0x5E9A90 -> 0x5E4760 -> 0x7B3310 -> 0x240AD3, a null fault)",
}

# The DLC objects EDF4.1's two mission packs spawn (pylib/edf41dlc.py: <EDF4.1>/DLC/<hash>/dlc.cpk, mounted mp01: /
# mp02:), as roots under each pack's Mods folder (mp01:/Object/GiantAnt04.sgo -> OBJECT/EDF41MP01/GIANTANT04.SGO; the
# mission converter rewrites the scripts' names to the same paths), with how many of the pack's listed missions (its
# MISSION/MISSIONLIST.SGO: 26 + 23) name each. dlc_mission_roots scans the scripts at build time and build() refuses a
# table that is not what it finds (2026-10-14: the table below is that scan). Classes: GiantAnt (GIANTANT04*), GiantBee
# (GIANTBEEQUEEN02), Nephila (NEPHILA0*L), SpiderNet (*_FIT), DragonSmall (*_HS), AntHill (ANTHILL301UG*), BigNest
# (ANTHILL301LARGEMP02), Ufo10 (UFO401ACE: under UFO401's alias). The packs ship more objects than their missions name
# (pack 2 carries pack 1's AntHill301Large, GiantAnt04SB, SpiderNet20/40 ... and the reverse): those are not roots.
DLC_ROOTS: dict[str, int] = {
    'EDF41MP01/GIANTANT04.SGO': 8, 'EDF41MP01/GIANTANT04SB.SGO': 6, 'EDF41MP01/GIANTBEEQUEEN02.SGO': 2,
    'EDF41MP01/ANTHILL301UG.SGO': 1, 'EDF41MP01/NEPHILA02L.SGO': 1, 'EDF41MP01/SPIDERNET160_FIT.SGO': 2,
    'EDF41MP01/SPIDERNET80_FIT.SGO': 1, 'EDF41MP01/DRAGONSMALL401_HS.SGO': 2, 'EDF41MP01/REDSMALLDRAGON_HS.SGO': 1,
    'EDF41MP01/YERROWSMALLDRAGON_HS.SGO': 2,
    'EDF41MP02/ANTHILL301LARGEMP02.SGO': 4, 'EDF41MP02/ANTHILL301UGMP02.SGO': 1, 'EDF41MP02/GIANTANT04.SGO': 5,
    'EDF41MP02/GIANTANT04SBMP02.SGO': 2, 'EDF41MP02/GIANTBEEQUEEN02.SGO': 3, 'EDF41MP02/NEPHILA02L.SGO': 1,
    'EDF41MP02/NEPHILA03L.SGO': 1, 'EDF41MP02/UFO401ACE.SGO': 5,
    # 4.1's 401 mothership, the plugin's class (MOTHER401_DLC_ROOTS below)
    'EDF41MP02/UFOMOTHER401_DLC_M809.SGO': 1, 'EDF41MP02/UFOMOTHER401_DLC_MP503.SGO': 1,
}
# DLC objects the missions spawn that are not ported, with why: their missions stay unplayable (the mission plan skips
# a mission whose object is not registered), as the story's mothership missions do.
DLC_EXCLUDED: dict[str, str] = {}


@dataclass(frozen=True)
class Gun:
    """One weapon of a ported robot (EDF4.1's Hector): 4.1 hangs a separate weapon object on an arm bone; the class
    it goes under has no sub-objects, so the weapon's mesh becomes part of the body model on that bone and the
    class's own gun_control entry fires from a locator at the barrel's end (module docstring of Alias)."""
    node: str                 # the arm bone the weapon hangs on: its +Z is the way the weapon points and fires
    mdb: str                  # 4.1's weapon model, a member of the object's model RAB (Hector301cannon.mdb)
    muzzle: str               # the MAB locator gun_control's muzzle.shape names; added at the barrel's end
    entry: int = 0            # which gun_control entry of the template it copies (its timing, effects, bullet)
    fire: 'HectorWeapon | None' = None   # what it fires (4.1's weapon, in EDF6 terms): the entry's bullet is rebuilt


@dataclass(frozen=True)
class Alias:
    cls: str                  # the EDF6 class the 4.1 object goes under
    template: str             # an EDF6 object of that class (OBJECT/<name>): the keys and CAS players 4.1 lacks
    players: tuple[str, ...]  # CAS players (anmgroups) the class resolves that 4.1's CAS lacks (crash on spawn)
    override: tuple[tuple[str, object], ...] = ()   # keys set to these values whatever 4.1's and the template's are
    stock_cas: bool = False   # its CAS is a file EDF6's archives also hold (so the closure skips it): 4.1's own, converted, is
                              # shipped under the same path with the grafts below, replacing the stock copy
    own_cas: bool = False     # 4.1's object has no CAS: the template's, every clip hollowed (cas_graft.hollow), as
                              # OBJECT/<root>.CAS: the class's players without the template's motion on its bones
    bones: tuple[tuple[str, str, str], ...] = ()   # on every MDB of its model RABs, in order: ('rename', old, new)
                              # or ('insert', name, parent): the bones the class finds by name (mdb.rename_bone /
                              # insert_bone: a transform-only bone, the indices past it renumbered)
    mab: tuple[tuple[str, str], ...] = ()   # its MAB's bone names (the locators' nodes) changed in place to these
    locator_nodes: tuple[tuple[str, str], ...] = ()   # (locator, bone): the MAB records of that name moved onto that bone
    pad: tuple[str, ...] = ()  # list keys 4.1's is shorter in: completed with the template's entries past its end
    # --- a template written in EDF6's own DSGO (every EDF6_Berserker*: named sub-dictionaries 4.1's SGO has no form
    # for): the result is a DSGO too, the template's keys with 4.1's value for each key both have (alias_dsgo)
    variables: tuple[str, ...] = ()   # CAS variables the class sets unchecked, grafted from the template's CAS
    cas_donors: tuple[tuple[str, tuple[str, ...], tuple[str, ...]], ...] = ()   # (EDF6 CAS, players, variables)
                              # grafted after the template's own: what the template's CAS lacks (its sibling has)
    state_aliases: tuple[tuple[str, str, str], ...] = ()   # (player, new state, existing state): cas_graft.alias_states
    drop: tuple[str, ...] = ()    # template keys not copied (a weak point makes the robot 5x tougher; an optional
                                  # key the template sets but 4.1's object does not, e.g. Deiroi's level_adjust 0.7;
                                  # the SGO path too: the keys of a template's look that 4.1's object does not share)
    fracture: bool = False    # a voxel-destruction body: animation_model[3] built from the model's extent and the
                              # template's tuning (fracture_block); FRACTURE_CLASSES need it
    guns: tuple[Gun, ...] = ()    # the weapons: meshes merged into the model, muzzles added, gun_control rebuilt
    eye: tuple[str, tuple[float, float, float]] | None = None   # (bone, position): the locator 目の基準位置
    prune: bool = False       # ship only the files the result names (the closure holds 4.1's weapon objects)
    material_from: str = ''   # a 4.1 SGO (OBJECT/<this>) whose bare blobs are material_animation here: the object's own
                              # lights are code-driven in 4.1 and the template's blobs name materials its model lacks
    se_prefix: tuple[str, str] = ('', '')   # sound names starting with [0] are renamed to start with [1] (4.1's names that
                                            # SEPRESET.SGO lacks, EDF6's remake's for the same roles)
    inert_materials: str = ''   # why the kept material_animation binds no material of the model, on purpose (check_materials)
    material_pairs: tuple[tuple[str, tuple[tuple[int, int], ...]], ...] = ()   # (key, ((4.1 blob index, slot), ...)):
                              # 4.1's bare MTAB blobs where the class reads [blob, slot] pairs (material_pairs())
    rename_players: tuple[tuple[str, str], ...] = ()   # (4.1's player, the class's name for it): cas_graft.rename_group,
                              # before the grafts (the player keeps its clips: it animates as in 4.1)
    rename_states: tuple[tuple[str, str, str], ...] = ()   # (player, 4.1's state, the class's name): cas_graft.rename_state,
                              # in order, before the grafts and the state aliases
    add_locators: tuple['mab_legacy.Locator', ...] = ()   # MAB locators the class looks up unchecked that 4.1's MAB lacks
                              # (mab_legacy.mab_add_locators; SGO path)
    post: 'Callable[[dict], None] | None' = None   # last, on the SGO's members (SGO path): keys computed from the result
    take: tuple[str, ...] = ()   # template keys that replace 4.1's (SGO path): the look, when the class cannot drive 4.1's
                                 # model (CLASS_NAMES); with prune, the 4.1 files only that look needed are not shipped


@dataclass(frozen=True)
class HectorWeapon:
    """One kind of 4.1 Hector weapon (C cannon, G gatling, L launcher, S shield) as the EDF6 gun_control entry that
    fires it. The numbers are what EDF41.exe's weapon classes do at s = 1 (s = level / 10: jobs/4bf89026/tmp/
    behav_hector/NOTES.md and the reverse notes behind it); EDF6's bullet factories are the same ones (Plasma /
    Solid02 / Rocket01), the fields below land in the donor bullet's fields of the same name. What the weapon's own
    SGO holds (hector_weapon_range, bullet_custom_param) is read from that SGO at build time, not copied here."""
    cls: str                  # xgs_scene_object_class of the weapon's SGO (checked: the arm holds what the table says)
    sgo: str                  # the weapon's SGO, OBJECT/<this>
    mdb: str                  # its model in Hector.mrab
    bullet: str               # BULLET_DONORS key; 'none': a shield, it fires nothing
    speed: float = 0.0        # per frame
    alive: float = 1.0        # frames
    damage: float | None = None   # coefficient of the level (see hector_damage); None: bullet_custom_param[1]
    radius: tuple[float, float] = (0.0, 0.0)   # explosion radius at level 1 and at the cap (equal: a constant)
    spread: float = 0.0       # radians, the entry's fire.accuracy
    count: float = 1.0        # bullets per attack (fire.count)
    interval: float = 1.0     # frames between them (fire.interval)


# The stock EDF6 bullet each factory is copied from: (OBJECT/<file>, path of the bullet node). They are the bullets of
# EDF6's own robots (Berserker Middle's plasma spread and machine gun, the Grand Mother's rocket), so every key the
# factory reads is there with a value EDF6 itself uses.
BULLET_DONORS: dict[str, tuple[str, str]] = {
    'plasma': ('E602_BERSERKER_MIDDLE_SPREAD.SGO', 'gun_battery_settings/bullet'),
    'solid': ('E602_BERSERKER_MIDDLE_MG.SGO', 'gun_battery_settings/bullet'),
    'rocket': ('E614_GRANDMOTHER_ARMPARTS_M123_GENOCIDE.SGO', 'EDF6_GranMotherParts_GunBattery/gun_battery/bullet'),
}

# (generation, letter) -> the weapon. 301: plasma cannon (PlasmaBullet01, 0.8/frame, 1800 frames, 40s, blast 8 + 0.45s
# capped at 17, spread 0.1), gatling and launcher; 401: rocket cannon (RocketBullet01, 10/frame, 120 frames, 40s, blast
# 30), gatling (spread max(0.05, 0.1 - (s-1)/30), at s = 1 0.1) and launcher; the 401 shield fires nothing.
# Gatling: SolidBullet02 every 3 frames, 2.05/frame, 60 frames, 2.5s; its bullet_custom_param is the spread shape.
# Launcher: a salvo of RocketBullet01 (300 frames), one every max(4, 31 - floor(s)) frames = 30 at s = 1; its blast
# (30) and its speed (10) are the cannon rocket's: nothing in 4.1's weapon SGO says otherwise, a stated assumption.
HECTOR_WEAPONS: dict[tuple[str, str], HectorWeapon] = {
    ('301', 'C'): HectorWeapon('Hector301_Cannon', 'HECTOR301_CANNON.SGO', 'Hector301cannon.mdb', 'plasma',
                               0.8, 1800.0, 40.0, (8.0, 17.0), 0.1, 1.0, 1.0),
    ('301', 'G'): HectorWeapon('Hector301_Gatling', 'HECTOR301_GATLING.SGO', 'Hector301Gatling.mdb', 'solid',
                               2.05, 60.0, 2.5, (0.0, 0.0), 0.1, 100.0, 3.0),
    ('301', 'L'): HectorWeapon('Hector301_Launcher', 'HECTOR301_LAUNCHER.SGO', 'Hector301Launcher.mdb', 'rocket',
                               10.0, 300.0, None, (30.0, 30.0), 0.0, 6.0, 30.0),
    ('401', 'C'): HectorWeapon('Hector401_Cannon', 'HECTOR401_CANNON.SGO', 'Hector401Cannon.mdb', 'rocket',
                               10.0, 120.0, None, (30.0, 30.0), 0.0, 1.0, 1.0),
    ('401', 'G'): HectorWeapon('Hector401_Gatling', 'HECTOR401_GATLING.SGO', 'Hector401Gatling.mdb', 'solid',
                               2.05, 60.0, 2.5, (0.0, 0.0), 0.1, 100.0, 3.0),
    ('401', 'L'): HectorWeapon('Hector401_Launcher', 'HECTOR401_LAUNCHER.SGO', 'Hector401Launcher.mdb', 'rocket',
                               10.0, 300.0, None, (30.0, 30.0), 0.0, 6.0, 30.0),
    ('401', 'S'): HectorWeapon('Hector401_Shield', 'HECTOR401_SHIELD.SGO', 'Hector401shield.mdb', 'none'),
}

# The Hector variants 4.1's missions name (root SGO -> (generation, left arm, right arm, missions)). The letters are
# what the SGO's hector_weapon holds (arms_l then arms_r), not the file name: HECTOR401SG holds the gatling on the
# left and the shield on the right. alias_dsgo checks the table against the file.
HECTOR_VARIANTS: dict[str, tuple[str, str, str, int]] = {
    'HECTOR301CC.SGO': ('301', 'C', 'C', 14), 'HECTOR301CG.SGO': ('301', 'C', 'G', 10),
    'HECTOR301CL.SGO': ('301', 'C', 'L', 4), 'HECTOR301GC.SGO': ('301', 'G', 'C', 5),
    'HECTOR301GG.SGO': ('301', 'G', 'G', 6), 'HECTOR301GL.SGO': ('301', 'G', 'L', 8),
    'HECTOR301LC.SGO': ('301', 'L', 'C', 3), 'HECTOR301LG.SGO': ('301', 'L', 'G', 4),
    'HECTOR301LL.SGO': ('301', 'L', 'L', 7),
    'HECTOR401CC.SGO': ('401', 'C', 'C', 4), 'HECTOR401CG.SGO': ('401', 'C', 'G', 1),
    'HECTOR401CS.SGO': ('401', 'C', 'S', 4), 'HECTOR401GG.SGO': ('401', 'G', 'G', 4),
    'HECTOR401GL.SGO': ('401', 'G', 'L', 1), 'HECTOR401LC.SGO': ('401', 'L', 'C', 1),
    'HECTOR401LG.SGO': ('401', 'L', 'G', 4), 'HECTOR401LL.SGO': ('401', 'L', 'L', 2),
    'HECTOR401LS.SGO': ('401', 'L', 'S', 7), 'HECTOR401SC.SGO': ('401', 'S', 'C', 2),
    'HECTOR401SG.SGO': ('401', 'G', 'S', 6),
    # both arms shields (it fires nothing; 4.1's own): the story's M202 and DLC Mission Pack 1's M203 (app:/ in both)
    'HECTOR401SS.SGO': ('401', 'S', 'S', 2),
}

ROOTS.update({root: v[3] for root, v in HECTOR_VARIANTS.items()})

# What every Hector variant shares of 4.1's behaviour that Berserker's keys can say (see the Alias comment below).
HECTOR_COMMON: tuple[tuple[str, object], ...] = (
    # 4.1's `resource` names its weapon objects (HECTOR301CC spells the key `resoruce`, which the class never reads);
    # the guns are part of the body model here, so the list is the template's own: empty
    ('resource', []),
    ('WalkingEnemyBase/RandomAnimationTrack', []),
    ('WalkingEnemyBase/InterpolateNode', 'waist'),
    # 4.1: 6 units/s (velocity 1 x the 0.1 smoothing x 60), turn 0.03 rad/frame with 10% smoothing
    ('WalkingEnemyBase/MoveSpeed', [6.0, 0.1]),
    ('WalkingEnemyBase/TurnSpeed', [0.03, 0.03]),
    ('BerserkerSettings/WakeupNode', ['waist', [0.0, 0.0, 1.0], [0.0, 1.0, 0.0]]),
    ('BerserkerSettings/DownRagdollBodies',
     ['RagDollProxys.waist', 'RagDollProxys.chest', 'RagDollProxys.head_1', 'RagDollProxys.head_2',
      'RagDollProxys.thigh_l', 'RagDollProxys.thigh_r', 'RagDollProxys.shin_l1', 'RagDollProxys.shin_r1']),
    ('BerserkerSettings/NoSeparationBodies', ['RagDollProxys.head_2', 'RagDollProxys.head_3']),
    ('BerserkerSettings/DamageReactionAdjust/min', 0.05),
    ('BerserkerSettings/ExplosionDownBorder', 10.0),
    ('BasicAnimationCharacterSettings/DamageReaction',
     [{'border': 0.0, 'user_param': ['damage', ['damageA', 'damageB', 'damageC']]}]),
    # 4.1 has no melee (no grapple / stomp / melee string in EDF41.exe)
    ('AiSettings/attack/range', 0.0),
    # weak_point kept (the class's setup 0x34C580 dereferences a missing key or member: 0x34CEFC -> {null, -1} ->
    # 0x551E0, the 2026-10-13 M153 crash) but without effect: no hit is a weak or a non-weak one any more. Its body
    # ragdoll_eye is none of Hector's (0x6EA670 gives -1, which both readers, 0x34E06F and 0x34E6C0, take as no body).
    # what it plays on first sight of a target: 4.1's Hector wakes up (its CAS states: sleep, wakeup, stand, walk,
    # turn); the template's encount_appeal_* are not Hector's, resolve to -1 and fault when played (STATE_KEYS)
    ('BerserkerSettings/EncountAnimations', ['wakeup']),
    ('weak_point/noweak_damage_multiply', 1.0),
    ('weak_point/weak_damage_multiply', 1.0),
    # hector_ai_setting [720, 1440, 540, 720, 150, 20, 80]: the stop radius is drawn from 20..80 (approach distance);
    # a weapon is picked every 300 + rand(300) frames (gun_attack interval 300..600)
    ('AiSettings/move/approach/distance', {'min': 20.0, 'max': 80.0}),
    ('AiSettings/gun_attack/interval', {'min': 300.0, 'max': 600.0}),
    ('game_sound', ['app:/sound/adx/tikyuu4_en_Hector.acb', 'app:/sound/adx/tikyuu6_en_BerserkerL.acb']),
)


def hector_alias(generation: str, left: str, right: str) -> 'Alias':
    """The Alias of one Hector variant: the class and keys every variant shares, and its two guns from HECTOR_WEAPONS."""
    arms = ((left, 'arms_l', '砲台１', 0), (right, 'arms_r', '砲台２', 1))
    guns = tuple(Gun(node, HECTOR_WEAPONS[(generation, letter)].mdb, muzzle, entry,
                     HECTOR_WEAPONS[(generation, letter)]) for letter, node, muzzle, entry in arms)
    return Alias(
        'EDF6_Berserker_Large', 'E602_BERSERKER_LARGE.SGO', ('damage', 'random'),
        override=HECTOR_COMMON,
        variables=('v_move_angle', 'v_tilt_dir', 'v_tilt_lerp', 'v_damage_reaction_ratio', 'v_tilt_factor',
                   'v_tilt_lerp_factor'),
        cas_donors=(('OBJECT/E602_BERSERKER.CAS', ('additive_action',), ('v_move_velocity',)),),
        state_aliases=(('main', 'walk_battle_f', 'walk_f'), ('main', 'walk_battle_b', 'walk_b'),
                       ('main', 'walk_battle_l', 'walk_l'), ('main', 'walk_battle_r', 'walk_r')),
        guns=guns, eye=(f'Hector{generation}', (0.0, 29.0, 0.0)), prune=True)


def ufo301_bullet(cls: str, custom: list, hit: str | None) -> list:
    """UfoSmall515's bullet_object [class, [0, key/value pairs, custom]] with 4.1 UfoSmall301's shot (setup 0x281fe0 at
    level 10): speed 0.65 per frame, life 300 frames, size 0.33, tint (1.1, 0.051, 0.051, 1) multiplied into the
    bullet_custom_param colours; key 14 the hit sound."""
    kv: list = [0, 0.65, 1, 300, 8, 0.33, 10, 0.001, 11, 1.0, 12, [1.1, 0.051, 0.051, 1.0]]
    if hit:
        kv += [14, hit]
    return [cls, [0, kv, custom]]


def ufo301_battery() -> list:
    """4.1: each side fires its 3 muzzles together, 4 volleys, one every 3 frames. 515 has a slot per muzzle:
    [key, shots, 0, cooldown s, 0, 0, period frames, bullets per fire, per-shot spread rad], delay frames."""
    return [[n, ['bullet_object', 4, 0.0, 5.5, 0, 0, 3, 1, 0.0], 0]
            for n in ('muzzleL01', 'muzzleL02', 'muzzleL03', 'muzzleR01', 'muzzleR02', 'muzzleR03')]


# 515's config: [aim frames before firing, 1.0, [wander radius min, max], [wander height min, max]] (4.1's waypoint
# radius 10..130, height 10..30; jobs/4bf89026/tmp/behav_ufo301/NOTES.txt)
UFO301_CONFIG: list = [60, 1.0, [10.0, 130.0], [10.0, 30.0]]
_UFO301_BATTLE = '小型UFO戦闘モード'
UFO301_OVERRIDE: tuple[tuple[str, object], ...] = (
    ('battery_config', ufo301_battery()),
    ('bullet_object', ufo301_bullet('BeamBullet01', [0, [1.0] * 4, [1.0] * 4, 1.0, 1.0, 2.0, 16], '小型UFOビーム命中')),
    ('config', UFO301_CONFIG),
    ('ufo_se', ['小型UFO飛行音', '小型UFO発進音', '小型UFOダメージ', '小型UFO爆発１', '小型UFO爆発２', '小型UFO攻撃',
                _UFO301_BATTLE]))
# The ace: LaserBullet02 (one bullet per fire, so 515's level_adjust[4] = 0.2, which E515's ACE has to compensate its
# 16-way, is 1.0); ufo_se has the ace's hit sound as an eighth.
UFO301ACE_OVERRIDE: tuple[tuple[str, object], ...] = (
    ('battery_config', ufo301_battery()),
    ('bullet_object', ufo301_bullet('LaserBullet02', [1], None)),
    ('config', UFO301_CONFIG),
    ('level_adjust', [1.5, 0.038462, 4.5, 0.0, 1.0]),
    ('ufo_se', ['精鋭UFO飛行音', '精鋭UFO発進音', '精鋭UFOダメージ', '精鋭UFO爆発１', '精鋭UFO爆発２', '精鋭UFO攻撃',
                _UFO301_BATTLE, '精鋭UFOビーム命中']))


# UFO401 (4.1 Ufo10) on UfoSmall507: pattern 0 is 507's state that is 4.1's attack pass (fire after 1.0 s, leave after
# 5.0 s), bullet_pattern 1 its tint formula (1.25s, 0.5s, s, s) = 4.1's. Lists of [op, args...] are 507's op VM on
# the level L (0 (a*v-b)*c+b, 1 +a, 2 *a, 3 a/v, 4 min, 5 max); every formula checked against 4.1's at L = 5..280 by
# jobs/4bf89026/tmp/behav_ufo401/verify_recipe.py. bullet_class LaserBullet01 and bullet_custom_param stay E507's.
UFO401_OVERRIDE: tuple[tuple[str, object], ...] = (
    ('ai_attack_pattern', 0),
    ('bullet_pattern', 1),
    # speed v(L) = 0.00375 L + 0.2625; engage 200 m; next attack 600..1200 frames; radii 40 / 20; wobble 0.1..1.5
    ('ai_info', [[[2, 0.00375], [1, 0.2625]], 200.0, [600, 1200], [40.0, 20.0], [0.1, 1.5], [150, 210]]),
    ('bullet_info', [
        -1,                                                          # endless while firing
        [[2, 0.02], [1, 0.8], [3, 10.0], [1, 1.0], [5, 2.0]],       # frames between bolts: max(10 / (0.02 L + 0.8) + 1, 2)
        1,                                                           # one bolt per shot
        [-1, 0.0],                                                   # no fan
        0.4,                                                         # size
        1800.0,                                                      # speed: 30 per frame in 507's m/s
        [[2, 0.07142857], [5, 1.0]],                                 # damage max(1, L / 14)
        30,                                                          # lifetime, frames
        0.01,                                                        # hit_impulse_adjust
        0.5,                                                         # hit_stop
        [[2, 0.0008], [1, -0.0018], [2, 0.06], [1, 0.0018]],         # gun turn per frame
        0.0,                                                         # no auto-aim cone
        1,
        0.0]),                                                       # no aim error
)


# 4.1's shot of the Deiroi cannon in E509's bullet_object form (PlasmaBullet01 at 0.5/frame, life 1800, size 6.0, blast 10,
# colour (0.4, 0.4, 3.0, 1)): E509 has 4.0 / 7.0 / (0.8, 0.4, 2.0, 1).
DEIROI_SHOT: list = ['PlasmaBullet01', [0, [0, 0.5, 1, 1800, 8, 6.0, 2, 10.0, 12, [0.4, 0.4, 3.0, 1.0], 13, 0.5, 14,
                                            'ディロイプラズマキャノン着弾'], [0, 60]]]


# 4.1's class -> EDF6's (jobs/4bf89026/tmp/alias41: the classes EDF.dll registers, the names each resolves).
MATERIAL_KEYS = ('material_animation', 'unit_material_animation')
INERT_E515 = ("4.1's UFO301 has no material animation: E515's four entries (Metal, Translucent1) bind nothing on its "
              "model (the parse skips names it does not find, EDF+0x1104350), the slots only have to exist")
ALIASES: dict[str, Alias] = {
    # UfoCarrier301 / 401 -> UfoCarrier508 (EDF5's carrier): the same job (hover, open the hatches, drop the enemies
    # the mission names); 508 also turns two rings 4.1's carriers do not have: its players in_ring / out_ring grafted
    # with clips that move nothing; keys UfoCarrier_InRingRotationSpeed, se_fly_name, se_list,
    # game_object_destroy_score_adjust from E508_CARRIER (its sounds are in tikyuu4_en_UFOcarrier301.acb, 4.1's bank).
    'UFOCARRIER301.SGO': Alias('UfoCarrier508', 'E508_CARRIER.SGO', ('in_ring', 'out_ring'), material_pairs=(('material_animation', ((0, 0), (1, 2), (2, 2))),)),
    # 4.1's 401 animates its lights in code (flash 3.0 decaying 0.05/frame, death 20.0; 0x24d3c0) on ufosurface / ufoglass;
    # the template's blobs name E508's Metal / Weakpoint, which its model lacks: the 301's (light0_color + scroll_light0
    # on ufosurface, the same job) are its animation.
    'UFOCARRIER401.SGO': Alias('UfoCarrier508', 'E508_CARRIER.SGO', ('in_ring', 'out_ring'),
                               material_from='UFOCARRIER301.SGO',
                               # se_list names E508's cues (en_Carrier508_*, the bank of E508's game_sound = EDF6's
                               # UFOCARRIER301.ACB); 4.1's own names (E4キャリア*) are not in EDF6's SEPRESET.SGO and no
                               # name there plays a Carrier401 cue, so 508's bank joins 401's (M015: the cue's bank unloaded)
                               override=(('game_sound', ['app:/sound/adx/tikyuu4_en_UFOcarrier401.acb',
                                                         'app:/sound/adx/tikyuu4_en_UFOcarrier301.acb']),),
                               material_pairs=(('material_animation', ((0, 0), (1, 2), (2, 2))),)),
    # UFO301 (and its ace) -> UfoSmall515 (EDF5's imperial UFO): its ufo_* keys are UfoSmall301's own descendants, and
    # it finds no bone by name. It resolves player main (open / close / wakeup) unchecked, and 4.1's UFO has no CAS:
    # E515's, hollowed. material_animation, config, battery_config, bullet_object, level_adjust (its weapon and its
    # level scaling) come from E515. material_animation must not be empty: the constructor builds a slot per element
    # and enables slot 0 unchecked (EDF+0x534497 -> 0x6dc830: an empty list crashed at EDF+0x6dc844, M143 on the real
    # game, 2026-10-12); E515's entries name E515's materials, which 4.1's model lacks: looked up by name, skipped.
    'UFO301.SGO': Alias('UfoSmall515', 'E515_IMPERIALUFO.SGO', (), override=UFO301_OVERRIDE, own_cas=True,
                             inert_materials=INERT_E515),
    'UFO301ACE.SGO': Alias('UfoSmall515', 'E515_IMPERIALUFO_ACE.SGO', (), override=UFO301ACE_OVERRIDE, own_cas=True,
                                inert_materials=INERT_E515),
    # UFO401 -> UfoSmall507 (EDF5's gold UFO, its remake). 507 finds bones body (the aim's frame, never moved),
    # gun_side (yawed toward the target each frame) and gun (pitched) by name, and the MAB locator 銃口 on a bone:
    # missing, a null write at EDF+0x529067 (jobs/4bf89026/tmp/ufo401/NOTES.txt). 4.1's UFO401 keeps center (its
    # ragdoll RAGDOLL_UFO401_HAS_HKA.SHKT and its locators name it): a weightless body under it (center's frame),
    # gun_side under center and gun under gun_side, all three weightless (core stays core: 4.1's core mesh never turns
    # toward the target; 507 yaws gun_side and pitches gun, which move nothing), and the MAB locator 銃口 moved from
    # center onto gun: 507 fires along the locator's bone, so the aim shows in the shots (UFO401_OVERRIDE: 4.1's
    # Ufo10 numbers, jobs/4bf89026/tmp/behav_ufo401/RECIPE.txt).
    # Its 7 sounds completed with 507's eighth (battle mode); ai_* / aim_range_info / bullet_* / move_turn_info
    # come from E507.
    'UFO401.SGO': Alias('UfoSmall507', 'E507_GOLDUFO.SGO', (), override=UFO401_OVERRIDE,
                        bones=(('insert', 'body', 'center'), ('insert', 'gun_side', 'center'),
                               ('insert', 'gun', 'gun_side')),
                        locator_nodes=(('銃口', 'gun'),),
                        # 4.1's names (EDF4小型UFO*) are not in EDF6's SEPRESET.SGO: silent; the remake's, same roles
                        se_prefix=('EDF4小型UFO', '小型UFO507'),
                        pad=('se_table',)),
    # AntHill301 (4.1's ant nest, 14 missions) -> AntHill, the class 4.1's nest has under the same name. EDF6's class
    # breaks its body into voxels when it is hit: animation_model[3] holds the grid and the model's bounding box, and
    # the class keeps the object it builds from it in a member read unchecked on every hit (EDF+0x4EB8D7 loads this+
    # 0x848, 0x6CAF22 writes through it: a null one crashed M115 on the first damage, 2026-10-13). 4.1's SGO has no
    # such block. The grid and the tuning (the six min/max pairs the block's sub-list holds) are E517_ANTHILL's, the
    # box is Anthill301.mdb's own extent, and the debris list is empty: E517's chunks are metal (a mech nest), and an
    # empty list is the class's own early exit (this+0x5C0's member +0x310 counts them, tested before use) and what
    # E615_HORNETNEST_SMALL's 3-entry block leaves too. game_object_destroy_score_adjust (both stock nests have it)
    # comes from E517; its ant_hill_material_type 1 (mech: no dust), lighting and material_animation (the
    # mech-damage textures of E517's model) are not 4.1's dirt nest's: dropped, so a hit raises the dirt-dust effect
    # (type 0, the default; the class reads all three optionally, EDF+0x4E9EB2 / 0x4E9F4E / 0x4EA452). Ant
    # spawning is not in this object: 4.1's missions drive it from their scripts (AntHill301.sgo carries no
    # generator data, and neither class reads any).
    'ANTHILL301.SGO': Alias('AntHill', 'E517_ANTHILL.SGO', (),
                            drop=('ant_hill_material_type', 'lighting', 'material_animation'), fracture=True),
    # Hector (301, cannons on both arms) -> EDF6_Berserker_Large: a walking robot with guns (jobs/4bf89026/tmp/alias41
    # REPORT.md; jobs/4bf89026/tmp/hector41 NOTES.md lists every name the class looks up). The class's template is
    # EDF6's own DSGO, so the result is a DSGO (alias_dsgo). What the class resolves that 4.1's Hector lacks:
    #   CAS players damage and random (the constructor binds both by name and the damage reaction plays damageA..C;
    #   a missing player is a state-table index of -1 times 0x160) from the template's CAS, additive_action from
    #   E602_BERSERKER.CAS (the Large CAS has none); each with clips that move nothing. CAS variables v_move_angle, v_tilt_dir, v_tilt_lerp, v_damage_reaction_ratio (the setters
    #   write at index -1 of a missing one) from the template's CAS, v_move_velocity from E602_BERSERKER.CAS, and the
    #   tilt tunings v_tilt_factor, v_tilt_lerp_factor (a missing one reads 0: no lean).
    #   MAB locators 目の基準位置 (the eye height, read unchecked in the constructor) and one muzzle per gun
    #   (砲台１, 砲台２ on the arm bones, at the barrel's end, firing along the arm's +Z, which the stand and walk poses
    #   aim forward); the cannons' meshes become objects on bones gun_arms_l / gun_arms_r under the arm bones.
    #   The bone and ragdoll-body names the template's SGO gives in Berserker's terms: Hector's own.
    # What 4.1's Hector does that the class would otherwise do differently:
    #   weak_point is neutral (both multipliers 1.0): the template's noweak_damage_multiply 0.2 applies to every hit
    #   that is not on its body (ragdoll_eye, a body Hector's ragdoll has none of), so with it Hector takes 0.2x = 5x
    #   tougher. The key itself must stay: the setup (0x34C580) dereferences a missing one (0x34CEFC -> 0x551E0).
    #   The lean when hit: 4.1 hard-codes a spring in EDF41.exe with no cap (impulse 40 x damage / max HP, +0.06 x
    #   that to the position, pull 5%, damping 0.9), so a barrage or an explosion piles up and bends the robot over
    #   backwards. The class blends one additive reaction clip by clamp(damage / HP x scale, min, 1): DamageReaction
    #   keeps only its first entry (damageA..C; the second, at 0.3 of the HP, plays damage_large, which Hector has no
    #   clip for), DamageReactionAdjust.min 0.05 (the template's 0.01 lets a bullet's nick blend at 1%), and
    #   ExplosionDownBorder 10: an explosion of more than 10x the HP is the only one that knocks it down (the class
    #   would play wakeup_f / wakeup_b, which Hector has no state for). The clips are the grafted ones: they move nothing.
    #   AiSettings.attack.range 0: the melee attacks play states Hector has no animation for.
    #   WalkingEnemyBase.RandomAnimationTrack empty: it names CAS variables (vt_walk_battle_f ...) and sets each to a
    #   tcontrol's index unchecked (0x116DE30 writes at -1 of a missing variable); Hector's CAS has neither, and an
    #   empty list is the loop's own early exit (0x53D184). The states walk_battle_f / b / l / r, which the class asks
    #   for in a fight and a missing state refuses, are Hector's own walks, so it does not stand still while it moves.
    # Transporter401_Formation (MP003, MP004: 4 transporters carrying the mission's vehicle) stays 4.1's file under the
    # class it has in EDF6 too. EDF6's stock copy is a leftover that the class cannot load, found one crash at a time on
    # the real game (2026-10-13), each by the same cause: the class reads keys and names unchecked.
    #  - se_preset_list (constructor, EDF+0x5E9A90, null to the sound system): EDF6's V508_TRANSPORT_FORMATION's 4 names
    #    (flight, booster on / off, container release; cues of the resident bank TIKYUUX_SE).
    #  - boosts: read for each transporter by 0x5E4830 (0x5E4AE9, unchecked), and every entry is a MAB locator looked up
    #    in animation_model[2] (0x6BADD0 reads [model+0x280], EDF+0x6BAE06 rax=0). 4.1's model has no MAB (item 0)
    #    and no booster locators; EDF6's 6 entries name locators of V508's MAB. Empty: the loop's own exit (count 0).
    #  - CAS main states hover_start, hover_end (0x5E4A6A..0x5E4AB0 look the states up by name, required): 4.1's
    #    Transporter401.CAS has only fly in main. Both are fly (aliases: the transporter flies, which is all 4.1 shows),
    #    shipped as OBJECT/TRANSPORTER401.CAS over the stock copy (stock_cas).
    'TRANSPORTER401_FORMATION.SGO': Alias('Transporter_Formation', 'V508_TRANSPORT_FORMATION.SGO', (),
                                          override=(('boosts', []),), stock_cas=True,
                                          state_aliases=(('main', 'hover_start', 'fly'), ('main', 'hover_end', 'fly'))),
    **{root: hector_alias(g, l, r) for root, (g, l, r, _n) in HECTOR_VARIANTS.items()},
    # Deiroi (5 variants): EDF6's Deiroi is 4.1's class with every constant moved into the SGO, and its constructor reads
    # deiroi_battery_config / deiroi_moving_settings / bullet_object / charge_param unchecked (EDF+0x319566, 0x31981c): 4.1's
    # file has none of them. E509_DEIROI_S supplies them; the shot is 4.1's (DEIROI_SHOT). The 0.7 level_adjust is the S
    # variant's own, so only DEIROI401S keeps it (jobs/4bf89026/tmp/behav_rest/NOTES_direct.md section 2).
    **{root: Alias('Deiroi', 'E509_DEIROI_S.SGO', (), override=(('bullet_object', DEIROI_SHOT),),
                   # E509's 4 entries are loop, loop, once, hold (slots 0, 0, 1, 2; 0x31d540): 4.1's loop blob and its other
                   material_pairs=(('material_animation', ((0, 0), (0, 0), (1, 1), (1, 2))),),
                   drop=() if root == 'DEIROI401S.SGO' else ('game_object_level_adjust',))
       for root in ROOTS if root.startswith('DEIROI401')},
    # AlienTrailer (5 variants) -> E513_SHIELDBEARER, the same object (EDF5's remake): se_list from it (the class reads it
    # unchecked, EDF+0x30a4d7; 4.1 hard-codes the same six names), and the two unit material animations as [blob, slot]
    # pairs, three of them (EDF+0x6dc6b0 / 0x312e80).
    **{root: Alias('AlienTrailer', 'E513_SHIELDBEARER.SGO', (),
                   material_pairs=tuple((k, ((0, 0), (1, 2), (1, 2))) for k in MATERIAL_KEYS))
       for root in ROOTS if root.startswith('ALIENTRAILER401')},
}

# Files that are not roots (the closure of one) but need their class swapped: 4.1's Deiroi beam foot is class
# DeiroiFootParts_Beam, which EDF6 does not register (only _Battery, _Joint, _Toe); EDF6's Battery reads the shot,
# charge and firing keys 4.1's part does not have (E509's, with 4.1's numbers: 600 bolts of LaserBullet02, 20 per frame,
# 30 frames, the range min(L + 200, 400), charge 240 frames; the +1.0 damage term needs a plugin, NOTES_direct.md 2.4).
PART_ALIASES: dict[str, Alias] = {
    'DEIROIFOOTPARTS_BEAM.SGO': Alias(
        'DeiroiFootParts_Battery', 'E509_DEIROIFOOTPARTS_BEAM.SGO', (), override=(
            ('bullet_object', ['LaserBullet02', [0, [0, 20.0, 1, 30, 8, 1.2, 10, 0.001, 11, 0.9, 12, [1.0, 1.0, 4.0, 1.0]],
                                                   [0]]]),
            ('deiroi_battery_config', ['発射口', ['bullet_object', 600, 0.0, 1.0, 0, 'ディロイレーザー照射', 1],
                                       'charge_param', 240, 0]),
            ('deiroi_fire_config', [200.0, 1.0, 400.0, 0, [600.0, 0.02]]))),
}


# ------------------------------------------------------------------------------------------------ the four-legged fortress
# Alien4LegTank301 (4.1's walking fortress, M117 M133 M134 M152) -> MovingFortress, EDF5's remake of it (E510_FORTRESS:
# its large cannon's game_sound is 4.1's tikyuu4_en_4LegTank.acb; jobs/4bf89026/tmp/fortress41/NOTES.md, NOTES_e6.md).
# What both classes share, read in both binaries: the body takes damage only through its core body while the belly hatch is
# open (4.1 slot 10 0x1ca260, EDF6 slot 16 0x4c6650); the hatch opens ahead of an ObjectGenerator's drop and closes when
# the batch is out (4.1 0x1cbc20..0x1cbe70, EDF6 0x4caa20..0x4ca8f0, driven by generators[1]); movement is the CAS root
# motion of one whole clip at a time (no speed constant in either); the wander cycle is the same code (walk timer, then a
# stop of lerp(2400, 1200, t) + rand(lerp(4500, 2400, t)) frames, t = clamp((level - 30) / 100, 0, 1): 4.1 0x1cbf50 and
# E510's ai_battle_config[1], number for number); parts ride MAB locators and are switched on and off by group.
# What the port sets (the rest is 4.1's file, the class's other keys E510's):
#   generators: 4.1 has one generator, at 発生位置; the class needs two (gen[0] drives a generator hatch 4.1's fortress does
#     not have, gen[1] the core hatch). Both at 発生位置; the plugin (src/edf41fortress.cpp) routes the mission's drop to gen[1].
#   ai_battle_config: 4.1's first walk is 3600 + rand(3600) frames ([0] = [rand, base]); groups 0 (cannons) and 1 (gatling)
#     start as the fight starts (4.1 0x1cc5c0: enabled on entry; a threshold of 1.0 is met at full health) and group 2 (the
#     bits) at half health (4.1: HP / HPmax <= 0.5, once).
#   ai_move_limit: 4.1 turns back toward the map's centre 150 inside its bounds (0x1cc110) when facing > 60 deg off.
#   parts_setting: 4.1's [group, locator, SGO] as the class's [mode mask 3 (standing or not), 0 (stored, unread), group,
#     locator, SGO].
#   collision_patches, moving_collision_setting_sp, orthotic: E510's name its own locators; 4.1 has none: empty (the class
#     loops over them).
#   CAS: 4.1's player hatch is the class's core_hatch; 4.1's clips under the class's state names (walk -> walk_f, turn_l /
#     turn_r (a turn while walking) -> walk_fwd_l / walk_fwd_r, turn_ll / turn_rr (a turn on the spot) -> turn_l / turn_r,
#     down -> destroy); the states the class plays that 4.1 has no clip for alias 4.1's stand (ground, wakeup, walk_start,
#     walk_end: the class waits for them to end, stand ends in 20 frames) or walk (walk_b / walk_l / walk_r, a controller's
#     only); core_hatch open_state (held open while dropping) replays open, 4.1 holds the opened pose without a clip. The
#     players generator_hatch and circle (E510's ring), which the class plays unchecked, are grafted from E510 with clips that
#     move nothing, with v_circle_animation_speed.
#   se_table / blow_se: 4.1's fortress sounds ("４足歩行メカ*") have no name in EDF6's SEPRESET.SGO: E510's (the same roles in
#     EDF5's remake: core hit, core destroyed, explosion, running; footfall), from E510's bank, which game_sound adds.
FORTRESS_BANKS = ['app:/sound/adx/tikyuu4_en_4LegTank.acb', 'app:/sound/adx/tikyuu5_en_fortress.acb']
FORTRESS_PARTS_SETTING = [[3, 0, 0, 'キャノン右', 'app:/object/Alien4LegTank301_Cannon.sgo'],
                          [3, 0, 0, 'キャノン左', 'app:/object/Alien4LegTank301_Cannon.sgo'],
                          [3, 0, 1, 'ガトリング', 'app:/object/Alien4LegTank301_Gatling.sgo'],
                          *[[3, 0, 2, f'bit0{i}', 'app:/object/Alien4LegTank301_Bit.sgo'] for i in range(1, 6)]]
FORTRESS_OVERRIDE: tuple[tuple[str, object], ...] = (
    ('generators', [['発生位置'], ['発生位置']]),
    ('ai_battle_config', [[3600, 3600], [[2400, 1200], [4500, 2400]], [[0, 1.0], [1, 1.0], [2, 0.5]]]),
    ('ai_move_limit', [150.0, 150.0]),
    ('parts_setting', FORTRESS_PARTS_SETTING),
    ('collision_patches', []), ('moving_collision_setting_sp', []), ('orthotic', []),
    ('game_sound', FORTRESS_BANKS),
    # the plugin's mark (src/edf41fortress.cpp reads it through the class's own key lookup 0x528A0; the class never asks)
    ('edf41_fortress', 1),
)
FORTRESS_ALIAS = Alias(
    'MovingFortress', 'E510_FORTRESS.SGO', ('generator_hatch', 'circle'), override=FORTRESS_OVERRIDE,
    variables=('v_circle_animation_speed',),
    rename_players=(('hatch', 'core_hatch'),),
    rename_states=(('main', 'walk', 'walk_f'), ('main', 'turn_l', 'walk_fwd_l'), ('main', 'turn_r', 'walk_fwd_r'),
                   ('main', 'turn_ll', 'turn_l'), ('main', 'turn_rr', 'turn_r'), ('main', 'down', 'destroy')),
    state_aliases=(('main', 'ground', 'stand'), ('main', 'wakeup', 'stand'), ('main', 'walk_start', 'stand'),
                   ('main', 'walk_end', 'stand'), ('main', 'walk_b', 'walk_f'), ('main', 'walk_l', 'walk_f'),
                   ('main', 'walk_r', 'walk_f'), ('core_hatch', 'open_state', 'open')))
# debris (E510's rubble at the locator 瓦礫基準点, which 4.1's MAB has not) stays: the class reads it only on a script command
# (0x1000000d / 1) 4.1's missions never send, and every stock SGO of the class has it (class_keys).


@dataclass(frozen=True)
class FortressPart:
    """One kind of 4.1 fortress part as a MovingFortress_Battery (the class every E510 turret is). 4.1's parts follow their
    locator rigidly and fire on their own clock (NOTES.md 1.8); the Battery does the same with: battery_config [muzzle,
    fire row, charge effect, charge frames] (0x4d3cb0: a wait of rand(0..attack[0][1]) frames, the charge, then the fire
    row: [bullet key, frames of firing, a, damage, shot sound, loop sound, frames between shots, bullets per shot, spread];
    the damage the factory gets is `damage + 7 L / 100`, 0x4d3fd5), aim_config [min, max (x pitch, y yaw, in pi), turn
    speed], the muzzle and pivot locators AIM_TARGET / AIM / AIM_AXIS it looks up by name in its own MAB (0x4cf4e5..
    0x4cf51f, unchecked), and fort_parts_destroy_info, read unchecked when it dies (0x4d44e7), whose [5] names list-0
    locators of its MAB (0x6badd0)."""
    template: str                          # an EDF6 MovingFortress_Battery SGO: the keys 4.1's part lacks
    bullet: list                           # [factory, [type, [kv...], custom]]: the part's 'bullet_edf41'
    fire: list                             # battery_config[1] (its bullet key is 'bullet_edf41')
    charge: int                            # battery_config[3]
    wait: int                              # attack [[0, wait]]: rand(0..wait) frames before each charge
    aim: list                              # aim_config
    muzzle: tuple[str, tuple[float, float, float], tuple[float, float, float]]   # (bone, position, AIM pivot)
    destroy: list                          # fort_parts_destroy_info
    durability: float | None = None        # None: 4.1's
    hidden: str = ''                       # 4.1's part has no model (the cannon): a model of this name with no mesh, in
                                           # an archive of its own, carries the part (the class reads a model and a ragdoll)


FORTRESS_HIT_SE = '歩行要塞プラズマキャノン着弾'   # TIKYUUX_SE, resident
FORTRESS_PARTS: dict[str, FortressPart] = {
    # Cannon (2, group 0): 4.1's never aims and fires along its locator's +Z (the barrels gun_l / gun_r point), one huge
    # slow plasma ball every 181 + 600 frames (0x1cff60..0x1d00b0), damage 2000 whatever the level (0x1cfc79), not
    # destructible (no model, no ragdoll, no HP: +0x321 = 1). bullet_setting [PlasmaBullet01, size 30 (and blast 2 x 30),
    # speed 5, life 1800, colour, custom [1, 120]]. Here: no aim (aim_config all 0), the 780-frame period as the charge and a
    # wait of rand(0..1); the model is meshless and the HP out of reach.
    'ALIEN4LEGTANK301_CANNON.SGO': FortressPart(
        'E510_FORTRESS_PARTS_SMALLCANNON3.SGO',
        ['PlasmaBullet01', [0, [0, 5.0, 1, 1800, 8, 30.0, 2, 60.0, 12, [2.0, 2.0, 5.0, 1.0], 13, 0.0, 14, FORTRESS_HIT_SE],
                            [1, 120]]],
        ['bullet_edf41', 0, 0.0, 2000.0, '歩行要塞プラズマキャノン発射', 0, 15], 780, 1,
        [[0.0, 0.0, 0.0], [0.0, 0.0, 0.0], 0.0], ('Alien4LegTank301bit', (0.0, 0.0, 10.0), (0.0, 0.0, 0.0)),
        [180, 120, 2.0, 1.0, 2000.0, ['爆破A']], durability=1.0e8, hidden='Alien4LegTank301cannon.mdb'),
    # Gatling (group 1): BeamBullet01 every 16 frames for 600 frames (speed 3, life 180, blast 8, size 3, colour (1.5, 1,
    # 0.25)), after a 600-frame spin-up, then 600 frames of spin-down and 600 + rand(600) idle (0x1d1c10..0x1d1ff0): the
    # 1800 fixed frames between bursts are the charge and the rand(600) the wait. Damage 50 x s (0x1d0ff0; the host's
    # d + 0.07 L has another slope: the s = 1 value). HP 700 (4.1's). Its barrel 銃身 points down the model's -Z (rotated pi
    # about X), so AIM_TARGET sits there.
    'ALIEN4LEGTANK301_GATLING.SGO': FortressPart(
        'E510_FORTRESS_PARTS_SMALLCANNON1_AA.SGO',
        ['BeamBullet01', [0, [0, 3.0, 1, 180, 8, 3.0, 2, 8.0, 10, 0.001, 11, 0.5, 12, [1.5, 1.0, 0.25, 1.0], 13, 0.0, 14,
                              '歩行要塞ビーム小着弾'],
                          [0, [1.5, 1.0, 0.25, 1.0], [1.5, 1.0, 0.25, 1.0], 1.5, 3.0, 1.5, 30]]],
        ['bullet_edf41', 600, 0.0, 50.0, '歩行要塞ビーム小発射', 0, 16, 1, 0.0], 1800, 600,
        [[-1.0, -1.0, 0.0], [1.0, 1.0, 0.0], 0.01], ('aim', (0.0, 0.0, -17.35), (0.0, 0.0, 0.0)),
        [180, 120, 2.0, 1.0, 2000.0, ['爆破A']]),
    # Bit (5, group 2): a fixed laser turret on the waist (not a drone), LaserBullet02 every 16 frames for 300 frames, then
    # at once again (0x1cee70..0x1cf190): speed 15, life 30, size 1, colour (2, 0.3, 0.3); damage 2 + 4 (s - 1). Its aim
    # follows the target with inertia (an angular acceleration of 0.0005 (1 + 0.4 (s - 1)), damping ~1): a slow turn.
    'ALIEN4LEGTANK301_BIT.SGO': FortressPart(
        'E510_FORTRESS_PARTS_SMALLCANNON1_AA.SGO',
        ['LaserBullet02', [0, [0, 15.0, 1, 30, 8, 1.0, 10, 0.001, 11, 0.5, 12, [2.0, 0.3, 0.3, 1.0]], [1]]],
        ['bullet_edf41', 300, 0.0, 2.0, '歩行要塞ビーム小発射', 0, 16, 1, 0.0], 1, 1,
        [[-1.0, -1.0, 0.0], [1.0, 1.0, 0.0], 0.005], ('Alien4LegTank301bit', (0.0, 0.0, 3.45), (0.0, 0.0, 0.0)),
        [180, 120, 2.0, 1.0, 2000.0, ['爆破A']]),
}
# Template keys a part does not take: the template's pedestal sub-part and starting angle, its custom bullet tuning. (The
# other weapons' bullet_* / charge_effect_* stay: every stock Battery SGO carries the whole set, unused but for the one
# battery_config names; class_keys holds the port to the set.)
FORTRESS_PART_DROP = ('subparts', 'initial_local_angle', 'custom_bullet', 'bullet_custom_param')
ALIASES['ALIEN4LEGTANK301.SGO'] = FORTRESS_ALIAS
ROOTS['ALIEN4LEGTANK301.SGO'] = 4


# --------------------------------------------------------------------------------------------------------- the giant dragon
# DragonBig401 (4.1's giant dragon, M166) -> DragonSmall, the class EDF6 kept of the pair (jobs/4bf89026/tmp/dragon41/
# NOTES.md). 4.1's DragonBig and DragonSmall are sibling classes of one design: the same AI states (wander, circle 70 over
# the target, approach, climb away, a third of the landings stay on the ground), the same body states (take off on the MAB
# event 飛行開始, fly, glide on 滑空開始 with the timed wing beat, land, hit, blown off, get up, die), the same SGO keys and the
# same speed formulas (walk clamp(Base (1 + (s - 1) Up), 0.1, 1) x 60, fly the same within 0.2..2: 4.1 0x1efd50, EDF6
# 0x334660), and the insectbase_* keys mean the same (InsectBase 0x22b8ea / EDF6 0x4a6830). EDF6's DragonSmall IS 4.1's
# DragonSmall ported (0x334660 = 4.1 0x1fb910, 0x339180 = 0x1f7080). What differs, and how:
#   - the keys EDF6's class reads unchecked that DragonBig's file lacks (0x528A0, then a load through the node: EDF+0x32e53e,
#     0x32e556, 0x32e5df, 0x32e72e, 0x33925c, 0x339269): the blood colours 4.1 hard-codes for DragonBig ((0.5, 0.15, 0.5, 1),
#     0xab7e00), the death blood scale it uses (0.35 x 5 = 1.75, 0x1ed710), the ragdoll reaction scale that gives 4.1's
#     impulse (EDF6 min(dmg, 20) x 4 x scale against 4.1's dmg x 10: 2.5), and the small dragon's fireball and bite tunings,
#     which DragonBig has no use for: harmless values (the fireball is not fired: src/edf41dragon.cpp; the bite never closes:
#     DragonBig's MAB has none of the 捕食判定* events that open its hit box).
#   - the MAB locator 捕食判定 the constructor reads unchecked (EDF+0x32e84a) and 捕食固定位置 (used while holding a prey), on
#     head and on the jaw ago, where EDF6's small dragon has them.
#   - CAS states the class plays that DragonBig has no clip for: capture (its ground attack: EDF6 only ever enters the
#     capture state on the ground, 0x33599e) is DragonBig's ground_attack, the fire-breathing clip; glide_fly (the timed
#     wing beat over the glide, 0x33a454) is glide_wingbeat; fly_damage_large (blown off in the air) is blow_off; damage_3 /
#     fly_damage_3 (a third random hit clip) are damage_1 / fly_damage_1.
#   - se_table in the class's order (0 hurt roar, 1 hit effect, 2 death roar, 3 blood, 4 fireball shot, 5 fireball hit,
#     6..8 the bite): 4.1's names (ドラゴン大*, ドラゴン炎のブレス着弾) are not in EDF6's SEPRESET.SGO; the small dragon's of the
#     same roles, the flame hit EDF6's grey flame thrower's.
#   - the flame: 4.1 breathes FlameBullet02, one a frame, between the MAB events 火炎開始 and 火炎終了 of its three attack clips
#     (0x1ee7c0 / 0x1ee7e0, 0x1ed530); EDF6's class hard-codes FlameBullet01 fireballs (0x33ae30 -> 0x32dd60) and registers no
#     火炎* event. edf41_dragon tells the plugin each attack state's flame window (from DragonBig's MAB, read here) and the flame
#     (edf41_flame: the bullet in the form 0x329e80 reads, 4.1's numbers: speed 2 per frame, life 360 frames, size 4, hit stop
#     0.95, colour (10, 10, 10, 1) fading to (-5, -10, -10, 0), custom [0, 0.24, 0.997, 1, end colour], 0x1ed3e3..0x1ed474).
DRAGON_FLAME_STATES = (('capture', 'ground_attack'), ('fly_attack', 'fly_attack'), ('fly_glide_attack', 'fly_glide_attack'))
DRAGON_FLAME: list = ['FlameBullet02', [0, [0, 2.0, 1, 360, 8, 4.0, 11, 0.95, 12, [10.0, 10.0, 10.0, 1.0]],
                                       [0, 0.24, 0.997, 1, [-5.0, -10.0, -10.0, 0.0]]]]
DRAGON_OVERRIDE: tuple[tuple[str, object], ...] = (
    ('dragon_DeadBloodScale', 1.75),
    ('dragon_BloodColor', [0.5, 0.15, 0.5, 1.0]),
    ('dragon_DeadBloodColor', [0.5, 0.15, 0.5, 1.0]),
    ('dragon_RagdollReactionScale', 2.5),
    ('dragon_AttackBulletSetting', [4.0, 2.0, 1.0e6, 0.2, 0.0]),
    ('dragom_AttackCaptureSetting', [1.0, 0.0, 0.0, 1.0]),
    ('se_table', ['ドラゴン小ｖダメージ大', 'ドラゴン小ヒットエフェクト', 'ドラゴン小ｖ死亡大', '敵共通血しぶき大', 'ドラゴン火炎弾発射',
                  'グレイ火炎放射命中', 'ドラゴン小噛みつき', 'ドラゴン小噛まれダメージ', 'ドラゴン小噛まれ飛行中の風']),
    ('edf41_flame', DRAGON_FLAME),
    # 4.1's collision numbers; its sound (ドラゴン大衝突) has no SEPRESET name: the small dragon's
    ('ragdoll_contact', ['ドラゴン小衝突', 2000.0, 0.5, 10.0, 120]),
)
def dragon_flame_windows(mab: bytes) -> list:
    """edf41_dragon: [[CAS state, flame start, flame end], ...] for DRAGON_FLAME_STATES, the times of 火炎開始 / 火炎終了 on the
    clip's MAB track (the clip time the plugin compares with the main player's)."""
    events = mab_legacy.mab_events(mab)
    out = []
    for state, clip in DRAGON_FLAME_STATES:
        keys = dict(events.get(clip, []))
        if '火炎開始' not in keys or '火炎終了' not in keys:
            raise ValueError(f'MAB track {clip} has no 火炎開始 / 火炎終了: {events.get(clip)}')
        out.append([state, float(keys['火炎開始']), float(keys['火炎終了'])])
    return out


def dragon_marks(members: dict) -> None:
    """DragonBig's plugin key: its attack states' flame windows (dragon_flame_windows)."""
    members['edf41_dragon'] = dragon_flame_windows(members['animation_model'][2])


DRAGON_ALIAS = Alias(
    'DragonSmall', 'DRAGONSMALL401.SGO', (), override=DRAGON_OVERRIDE,
    state_aliases=(('main', 'capture', 'ground_attack'), ('main', 'glide_fly', 'glide_wingbeat'),
                   ('main', 'fly_damage_large', 'blow_off'), ('main', 'damage_3', 'damage_1'),
                   ('main', 'fly_damage_3', 'fly_damage_1')),
    add_locators=(mab_legacy.Locator('捕食判定', 'head', 0, (0.0, 0.0, 0.5, 1.0), 2.0),
                  mab_legacy.Locator('捕食固定位置', 'ago', 2, (0.0, -0.1, 0.8, 1.0), (0.1, 0.1, 0.5, 1.0),
                                     (0.7853982, 1.5707964, 0.7853982, 1.0))),
    post=dragon_marks)


ALIASES['DRAGONBIG401.SGO'] = DRAGON_ALIAS
ROOTS['DRAGONBIG401.SGO'] = 1

# The DLC roots whose 4.1 class needs an alias: the same classes as the story's objects, so the same alias. The
# underground nests (AntHill: the voxel block, as ANTHILL301; their model is the pack's own AnthillUG.rab), and the
# ace UFO401 (4.1's Ufo10 like UFO401, the same model ufo401.mrab with the same bones inserted; its own durability 300 and
# level_adjust 5 stay; its two ace keys become UfoSmall507's own keys of the same meaning: UFO401ACE_ALIAS below).
def nest_material_slots(members: dict) -> None:
    """The underground nest's material_animation as E517_ANTHILL's three entries (modes 0, 1, 2: check_materials holds
    a list to the template's count, as a class enabling slots by number reads past a shorter one): 4.1's single entry
    (its loop on Body02 / Hole02, mode 0) first, the same animation in the other two. E615_HORNETNEST_SMALL* have no
    material_animation at all (the class reads the key optionally); the UG nest's own is kept, so it has its 4.1 look."""
    ma = members['material_animation']
    if len(ma) != 1 or not isinstance(ma[0], list) or int(ma[0][1]) != 0:
        raise ValueError(f'the nest\'s material_animation is not one [blob, 0] entry: {len(ma)} entries')
    members['material_animation'] = [ma[0], [ma[0][0], 1], [ma[0][0], 2]]


# The underground nests: ANTHILL301's alias, with their own material animation (nest_material_slots). Two keys are set
# as 4.1 has them (NEST_41_KEYS below): the score multiplier E517's template would bring in (0.5) is 1.0, what 4.1 gives a
# nest without the key and EDF6 too (the base reader EDF+0x545DB7 defaults to 1.0); the kill counter is EDF6's name for
# the same tally.
NEST_41_KEYS: tuple[tuple[str, object], ...] = (
    ('game_object_destroy_score_adjust', 1.0),
    # 4.1's nests count into NestKillCount (a name in EDF41.exe's counter table, 0xA36D20); EDF6's table (EDF+0x893C40)
    # has no such name, so the kill would be counted nowhere: the same tally is AntHillKillCount there (0x894440)
    ('game_object_kill_counter', 'AntHillKillCount'),
)
UG_NEST_ALIAS = Alias('AntHill', 'E517_ANTHILL.SGO', (),
                      drop=('ant_hill_material_type', 'lighting', 'material_animation'), fracture=True,
                      override=NEST_41_KEYS, post=nest_material_slots)
ALIASES['EDF41MP01/ANTHILL301UG.SGO'] = UG_NEST_ALIAS
ALIASES['EDF41MP02/ANTHILL301UGMP02.SGO'] = UG_NEST_ALIAS

# ------------------------------------------------------------------------------------------- the DLC's insects and nests
# The DLC's ants, queen bee, Nephilas, webs and big ant nest stay 4.1's files on EDF6's classes of the same name. EDF6's
# classes are 4.1's ported (members moved, the same readers, formulas and constants: jobs notes edf41-handoff/tmp/insects/
# *_NOTES.md), so almost every key means what it meant. Checked and the same: GiantAnt's ant_AiSetting (4.1 0x2079d0 /
# EDF+0x409F40: type, then engage distance [1] + rand [2]), ant_MoveSpeedUp (the same clamp), ant_ex_DamageScale and the
# acid / bite numbers per level (4.1 0x205840 / EDF+0x40CD80: count min(L/5+1, 16), speed L/400+0.4, damage L/2 x scale,
# bite L/2 and L/20 x scale), ant_ex_CaptureAnimationParam, ant_ex_AlwaysPartsBreak (EDF+0x40A737); the queen bee's keys
# (4.1 0x210dee.. / EDF+0x41EBDE..: GiantBee moved by +0x810); the SpiderNet's keys including
# spider_net_ex_inner_radius_adjust (EDF+0x4E41C3, used 0x4E459E as 4.1 0x24031F); the nests' (AntHill 4.1 0x244260 /
# EDF+0x4E8E50, BigNest 0x246420 / EDF+0x4ED680: damage_animation entry 0 played once at spawn, 1..3 at 25 / 50 / 75 %
# lost, generators only fed by the script). game_object_ai_route_update_distance, which the ants and the queen lack,
# defaults to 5.0 in both (4.1 0x28D0F9, EDF+0x5464A5); the queen gets 160 from GIANTBEEQUEEN's keys (EDF6's own queen02
# has 160: in EDF6's route mode, which 4.1 lacks, 5 would stall it; the DLC missions never give it a route).
# What differs:
#  - Nephila02L's thread gravity. nephila_ex_attack_param's [2] (the thread's gravity factor) and [3] (the spread, rad)
#    are read together only when the list is longer than 2 (4.1 0x23D197, EDF+0x4DF5BD); else the setup's defaults stay:
#    3.0 and 0.1 in 4.1 (0x23A589, 0x23A3FB), 0.5 and 0.1 in EDF6 (EDF+0x4E280E, 0x4E26DF). 02L sets [4, 700]: its
#    threads would fall 6x slower under EDF6. 03L sets all four (0.5, 0.025): the same in both.
#  - The Nephilas' thread scatter: bullet_custom_param[0] (4.1 random_speed, read by position) never reaches EDF6's
#    thread bullet (src/edf41insects.cpp): the plugin key edf41_random_speed carries it.
#  - The queen bee's needle speed: L/400 + 0.4 in 4.1 (0x20E85A), + 0.5 in EDF6 (EDF+0x422958): the plugin key
#    edf41_needle_base carries 4.1's 0.4 (src/edf41insects.cpp).
#  - ragdoll_contact[1], the corpse impact that plays the thud: EDF6 measures it in other units (mass x v / 3600,
#    EDF+0x6EC450, against 4.1's 0x337FE0) and its own copies of these very objects (GIANTANT04, GIANTANT04SB,
#    GIANTBEEQUEEN02) hold 8000 / 12000 / 8000 for 4.1's 4000: theirs.
#  - The big ant nest's kill counter (NEST_41_KEYS).
# Not ported: EDF6's voxel body (animation_model[3], a cosmetic debris effect EDF6 adds to its own ants; the damage path
# skips it when absent, EDF+0x4A8DDF) and the queen's needle spread (4.1 0.035..0.14 rad, EDF6 0.007..0.14, EDF+0x424999).
DLC_NEEDLE_BASE = 0.4          # EDF41 0x20E85A: addss 0.4 (0xAB6550)
DLC_NEPHILA_GRAVITY = 3.0      # EDF41 0x23A589: [+0xCA0] = 3.0
DLC_NEPHILA_SPREAD = 0.1       # EDF41 0x23A3FB / EDF+0x4E26DF: [+0xBE0] / [+0x10B0] = 0.1
DLC_CONTACT: dict[str, float] = {'GiantAnt04': 8000.0, 'GiantAnt04SB': 12000.0, 'GiantBeeQueen02': 8000.0}


def dlc_contact(kind: str) -> Callable[[dict], None]:
    """ragdoll_contact[1] as EDF6's own copy of the object holds it (4.1's is 4000: another unit)."""
    def apply(members: dict) -> None:
        rc = members['ragdoll_contact']
        if not isinstance(rc, list) or len(rc) != 5 or sgo.plain(rc[1]) != 4000.0:
            raise ValueError(f'{kind}: ragdoll_contact is not 4.1\'s [SE, 4000, ...]: {rc!r}')
        members['ragdoll_contact'] = [rc[0], DLC_CONTACT[kind], *rc[2:]]
    return apply


def dlc_queen(members: dict) -> None:
    """GiantBeeQueen02: the queen (insectbase_Type 1), its needle speed base for the plugin, its corpse thud threshold."""
    if sgo.plain(members.get('insectbase_Type')) != 1:
        raise ValueError(f'GiantBeeQueen02 is not a queen (insectbase_Type {members.get("insectbase_Type")!r})')
    members['edf41_needle_base'] = [DLC_NEEDLE_BASE]
    dlc_contact('GiantBeeQueen02')(members)


def dlc_nephila(members: dict) -> None:
    """Nephila02L / 03L: nephila_ex_attack_param with 4.1's gravity and spread when it gives only speed and range, and
    bullet_custom_param[0] (4.1's random_speed) as the plugin key."""
    ap = [sgo.plain(v) for v in members['nephila_ex_attack_param']]
    if len(ap) == 2:
        members['nephila_ex_attack_param'] = [float(ap[0]), float(ap[1]), DLC_NEPHILA_GRAVITY, DLC_NEPHILA_SPREAD]
    elif len(ap) != 4:
        raise ValueError(f'nephila_ex_attack_param has {len(ap)} entries, not 2 or 4: {ap}')
    cp = members.get('bullet_custom_param')
    if not isinstance(cp, list) or len(cp) != 1:
        raise ValueError(f'bullet_custom_param is not one value (4.1\'s random_speed): {cp!r}')
    members['edf41_random_speed'] = [float(sgo.plain(cp[0]))]


def dlc_big_nest(members: dict) -> None:
    """AntHill301LargeMP02 (BigNest): the kill counter (NEST_41_KEYS); it has no score key, 1.0 in both games as for
    EDF6's own BigNest (HORNETNEST401)."""
    members.update(dict(NEST_41_KEYS[1:]))


DLC_INSECT_PATCHES: dict[str, Callable[[dict], None]] = {
    'EDF41MP01/GIANTANT04.SGO': dlc_contact('GiantAnt04'), 'EDF41MP02/GIANTANT04.SGO': dlc_contact('GiantAnt04'),
    'EDF41MP01/GIANTANT04SB.SGO': dlc_contact('GiantAnt04SB'), 'EDF41MP02/GIANTANT04SBMP02.SGO': dlc_contact('GiantAnt04SB'),
    'EDF41MP01/GIANTBEEQUEEN02.SGO': dlc_queen, 'EDF41MP02/GIANTBEEQUEEN02.SGO': dlc_queen,
    'EDF41MP01/NEPHILA02L.SGO': dlc_nephila, 'EDF41MP02/NEPHILA02L.SGO': dlc_nephila, 'EDF41MP02/NEPHILA03L.SGO': dlc_nephila,
    'EDF41MP02/ANTHILL301LARGEMP02.SGO': dlc_big_nest,
}

# The DLC ace UFO401 (Mission Pack 2's red saucer, UFO401ACE.SGO, class Ufo10 like UFO401) differs from UFO401 by two
# keys Ufo10 reads (EDF41.exe; jobs notes in the comments below):
#  ufo401_ex_ChangeColor [[material, param, [values]], ...] -> the constructor (0x285bc0) hands it to the model's
#    material-parameter setter 0x322940 (per entry: material name or 0 = every material, the parameter name, up to 4
#    floats, the rest from 0xab7760), the ace's red: diffuse (0.8, 0.05, 0.1, 1), fall_off_scale 1, fall_off_offset 0.
#    EDF6's UfoSmall507 constructor reads animation_model_param_change_table (EDF+0x528a68) in the same form into the
#    same kind of setter on its model (0x6c26a0; E507_GOLDUFO_HI's blue is [[0, 'diffuse', ...], [0, 'metallic', ...]]):
#    the list moves over as it is. ufo401.mdb's two materials (ufosurface, ufoglass) carry all three parameters.
#  ufo401_ex_param [[a, b], [c0, c1, c2], R, [n, g, m], k] -> 0x288870 (Ufo10's setup per level L) and 0x288140; without
#    the key (the story's UFO401) the defaults below. a + b ((0.1 L - 1) / 8 + 1): the flight speed (+0xf80), which 507's
#    ai_info[0] holds as a formula of L; R: the engage distance (+0xfc0; ai_info[1]); max(n / ((0.1 L - 1) g + 1) + 1, m):
#    frames between bolts (+0xfdc; bullet_info[1]); clamp(c1 L + c0, 1, c2): the attack pass's speed factor (+0xfd0),
#    the ace's equal to the default (checked: UFO401's recipe covers it); k: the knock a hit gives the body (+0x10b8 in
#    the damage handler 0x28736e: impulse ((damage - 5) / 2 + 5) k), which 507 reads as reaction_scale (EDF+0x528f77 into
#    +0x16b8, used the same way at 0x52e90c; its second value, 0.4, is 507's default and stays). So the ace flies at
#    0.001875 L + 0.63125 per frame (UFO401: 0.00375 L + 0.2625), engages from 600 m (200), fires down to every frame
#    (every 2nd) and takes 30% of the knock.
UFO401_EX_DEFAULT: list = [[0.0, 0.3], [1.0, 0.002, 5.0], 200.0, [10.0, 0.2, 2], 1.0]


def ufo401_ex_keys(ex: list) -> tuple[list, float, list, float]:
    """UfoSmall507's (ai_info[0] speed formula, ai_info[1] engage distance, bullet_info[1] bolt interval formula,
    reaction_scale) for 4.1 Ufo10's ufo401_ex_param `ex` (see above; 507's op VM: 1 +a, 2 *a, 3 a/v, 5 max). ValueError
    for a shape Ufo10 does not read or a part no 507 key carries."""
    if not (isinstance(ex, list) and len(ex) == 5 and [len(x) if isinstance(x, list) else -1 for x in ex] == [2, 3, -1, 3, -1]):
        raise ValueError(f'ufo401_ex_param is not [[a, b], [c0, c1, c2], R, [n, g, m], k]: {ex}')
    (a, b), clamp, engage, (n, g, m_), k = [[num(y) for y in x] if isinstance(x, list) else num(x) for x in ex]
    if any(abs(x - y) > 1e-6 for x, y in zip(clamp, UFO401_EX_DEFAULT[1])):
        raise ValueError(f'ufo401_ex_param[1] (the attack pass speed factor) {clamp} is not Ufo10\'s default '
                         f'{UFO401_EX_DEFAULT[1]}: UFO401\'s recipe is that default, no other 507 key holds it')
    r = lambda x: round(x, 9)   # float32 inputs: the formula's own decimals
    speed = [[2, r(b * 0.0125)], [1, r(a + b * 0.875)]]
    interval = [[2, r(g * 0.1)], [1, r(1.0 - g)], [3, r(n)], [1, 1.0], [5, r(m_)]]
    return speed, r(engage), interval, r(k)


def ufo401_recipe_is_default() -> None:
    """ValueError unless UFO401_OVERRIDE's speed, engage distance and bolt interval are ufo401_ex_keys of Ufo10's
    defaults (the story's UFO401 has no ufo401_ex_param): the two recipes cannot drift apart."""
    over = dict(UFO401_OVERRIDE)
    speed, engage, interval, k = ufo401_ex_keys(UFO401_EX_DEFAULT)
    if (speed, engage, interval, k) != (over['ai_info'][0], over['ai_info'][1], over['bullet_info'][1], 1.0):
        raise ValueError(f'UFO401_OVERRIDE is not Ufo10 at its defaults: {(speed, engage, interval, k)}')


def ufo401_ace(members: dict) -> None:
    """UFO401's alias, then the ace's two keys as UfoSmall507's (see above): ufo401_ex_ChangeColor as
    animation_model_param_change_table, ufo401_ex_param into ai_info / bullet_info / reaction_scale. Both are removed
    (507 reads neither). ValueError when either is missing or not in Ufo10's form."""
    ufo401_recipe_is_default()
    tint = members.pop('ufo401_ex_ChangeColor', None)
    if not (isinstance(tint, list) and tint and all(
            isinstance(e, list) and len(e) == 3 and (e[0] == 0 or isinstance(e[0], str)) and isinstance(e[1], str)
            and isinstance(e[2], list) and 1 <= len(e[2]) <= 4 for e in tint)):
        raise ValueError(f'ufo401_ex_ChangeColor is not [[0 or material, param, [1..4 floats]], ...]: {tint}')
    if 'ufo401_ex_param' not in members:
        raise ValueError('the ace has no ufo401_ex_param')
    speed, engage, interval, k = ufo401_ex_keys(members.pop('ufo401_ex_param'))
    members['animation_model_param_change_table'] = tint
    members['ai_info'] = [speed, engage, *copy.deepcopy(members['ai_info'][2:])]
    members['bullet_info'] = [members['bullet_info'][0], interval, *copy.deepcopy(members['bullet_info'][2:])]
    members['reaction_scale'] = k


UFO401ACE_ALIAS = replace(ALIASES['UFO401.SGO'], post=ufo401_ace)
ALIASES['EDF41MP02/UFO401ACE.SGO'] = UFO401ACE_ALIAS
# The DLC's three small dragons (DragonSmall401_HS, Red / YerrowSmallDragon_HS: M805, M993): 4.1's own small dragons
# (models SmallDragon401Green / RedSmallDragon / YerrowSmallDragon, their CAS, MAB and ragdolls, the random reactions,
# every tuning value) on EDF6's DragonSmall, which IS 4.1's DragonSmall ported (see the giant dragon above): every CAS
# state, CAS variable, MAB locator and event, and the wakeup_node bone the class looks up by name are in 4.1's files
# (CLASS_NAMES, check_class_names). One name is not: on a hit that knocks it about, the class hands the ragdoll bodies
# koshi and mune to 0x4A8570 (EDF+0x3395E6 / 0x339601 / 0x33962C), EDF5's frog dragon's, and dereferences a miss (the
# M805 crash at EDF+0x6E42EB, 2026-10-14); 4.1's small-dragon ragdolls have koshi and the torso bodies spine1 / spine2,
# no mune. 4.1's own DragonSmall, in the same reaction, drives {koshi, spine1, spine2} (EDF41.exe 0x1fafc0: 0x1fb06b /
# 0x1fb0af / 0x1fb0eb into 0x22d7b0). edf41_ragdoll_bodies carries 4.1's list and src/edf41smalldragon.cpp hands it to
# 0x4A8570 in place of the class's (only for an object with the key); check_class_names checks every name of it against
# the ragdoll shipped, and without the key refuses the object (mune). The EDF6 objects of the same names (EDF5's frog
# dragons) are the templates: only keys 4.1's file lacks come from them (game_object_destroy_score_adjust).
SMALL_DRAGON_BODIES: tuple[str, ...] = ('koshi', 'spine1', 'spine2')
SMALL_DRAGON_MOST_BODIES = 8       # src/edf41smalldragon.cpp kMostBodies: a longer list is refused there
SMALL_DRAGON_LONGEST_BODY = 31     # src/edf41smalldragon.cpp kLongestName (characters)
for _hs in ('DRAGONSMALL401_HS.SGO', 'REDSMALLDRAGON_HS.SGO', 'YERROWSMALLDRAGON_HS.SGO'):
    ALIASES['EDF41MP01/' + _hs] = Alias('DragonSmall', _hs, (),
                                        override=(('edf41_ragdoll_bodies', list(SMALL_DRAGON_BODIES)),))



def soldier_talk(members: dict) -> None:
    """AssultSoldier's game_object_talk_setting [sgo, a, b, c]: 4.1 (EDF41 0x28cbd0) c = the variant count, EDF6
    (EDF+0x545670) b = the variant count and c a bool; read as it is, the voice always plays variant 0."""
    t = members['game_object_talk_setting']
    members['game_object_talk_setting'] = [t[0], t[1], t[3], t[2], *t[4:]]


# Vehicle501_FortressRobo's keys EDF6 reads in another shape (EDF+0x6025c7: booster_setting is [[name, 0/1], ...] and,
# non-empty, needs booster_param_list; vehicle_riding_position has 7 items, EDF+0x62b430). The values are the ones EDF6's own
# object of the class (V515_RETROBALAM) holds, the 4 boosters 4.1 names kept (L1, R1 group 0; L2, R2 group 1).
V501_FIX: tuple[tuple[str, object], ...] = (
    ('booster_setting', [['ブースターL1', 0.0], ['ブースターR1', 0.0], ['ブースターL2', 1.0], ['ブースターR2', 1.0]]),
    ('booster_param_list', [[40.0, 40.0], [10.0, 10.0]]),
    ('se_damage_scaling', [0.5, 1.0, 2.0, 1.0]),
    ('vehicle_riding_position',
     [['搭乗口１', '操縦席１', ['カメラ１', 0.0, 0.0], '501_FORTRESSROBO_DRIVER', 15.0, 0.0, 3.0]]),
    ('ragdoll_contact', ['バラム衝撃', 2000.0, 0.5, 10.0, 120.0]),
    ('mission_setup', [[1.0, 1.0], [], []]),   # 4.1's [0, 0] third item is a weapon list in EDF6's reading
)


def set_keys(pairs: tuple[tuple[str, object], ...]) -> Callable[[dict], None]:
    def apply(members: dict) -> None:
        for k, v in pairs:
            members[k] = v
    return apply


# Roots kept as 4.1's own file (the class is EDF6's of the same name) with the keys whose shape or meaning differ fixed.
PATCHES: dict[str, Callable[[dict], None]] = {
    'AIARMYSOLDIER_OMG.SGO': soldier_talk, 'AIARMYSOLDIER_OMG_LEADER.SGO': soldier_talk,
    'VEHICLE501_FORTRESSROBO.SGO': set_keys(V501_FIX), 'VEHICLE501_FORTRESSROBO_AI.SGO': set_keys(V501_FIX),
}
PATCHES.update(DLC_INSECT_PATCHES)


def mtab_materials(blob: bytes) -> list[str]:
    """The material names an MTAB blob animates (EDF+0x1104350: int32 count at +0xc, entries of 12 bytes at +0x18's
    offset, each with a relative offset to the material's name, UTF-16, at +4)."""
    n, base = struct.unpack_from('<ii', blob, 0xc)[0], struct.unpack_from('<i', blob, 0x18)[0]
    names = []
    for i in range(n):
        at = base + 12 * i   # EDF+0x1104408..0x110440c: the offset at +4 is relative to the entry
        p = at + struct.unpack_from('<i', blob, at + 4)[0]
        q = p
        while blob[q:q + 2] != bytes(2):
            q += 2
        names.append(blob[p:q].decode('utf-16-le'))
    return names


def material_data(data: bytes) -> tuple[dict[str, list], list[tuple[str, str]]]:
    """({MATERIAL_KEYS key: its entries}, [(RAB app path, MDB name) of the models]) of an object SGO in either form. An
    entry is [blob, slot] in a legacy SGO (bytes, int) and (blob, slot) in a DSGO (animation_model/material_animation)."""
    if data[:4] == b'DSGO':
        model = dsgo.parse(data).root.get('animation_model')
        entries = [[c.items[0].data if isinstance(c.items[0], dsgo.Blob) else None, c.items[1]]
                   for c in model.get('material_animation').items]
        return {'material_animation': entries}, [(model.items[0].items[0], model.items[0].items[1])]
    members = sgo.read(data)[1]
    refs = [tuple(e[:2]) for e in model_entries((members.get('animation_model') or [[]])[0])]   # a SpiderNet has none
    for key in ('unit_model', 'small_barrier_model', 'large_barrier_model'):
        if key in members:
            refs += [tuple(e[:2]) for e in model_entries(members[key][0])]
    return {k: members[k] for k in MATERIAL_KEYS if k in members}, refs


def model_entries(models: object) -> list[list]:
    """animation_model[0] as its list of [RAB, MDB, (LODs)] entries: 4.1 writes either one entry ([RAB, MDB, ...]: the
    DLC's GiantAnt04, its LOD list third) or a list of them ([[RAB, MDB], [light RAB, light MDB]])."""
    if not isinstance(models, list) or not models:
        return []
    return [models] if isinstance(models[0], str) else [e for e in models if isinstance(e, list)]


def model_materials(games: 'Games', files: dict[str, bytes], refs: list[tuple[str, str]]) -> set[str]:
    """Every material name of the MDBs `refs` name (4.1's own, or EDF6's when it is EDF6's)."""
    out: set[str] = set()
    for rab, name in refs:
        rel = rel_of(rab)
        if rel in files:
            data = files[rel]
        elif rel in games.have6:
            data = games.read6(rel)
        else:
            continue   # a _light.rab 4.1 does not ship either
        for f in mdb.rab_read(data).files:
            if f.name.lower() == name.lower():
                md = mdb.mdb_read(f.data)
                out |= {md.name_of(x.name) for x in md.materials}
    return out


def check_materials(games: 'Games', files: dict[str, bytes], rel: str, need: dict[str, int], inert: str = '') -> None:
    """ValueError when a material animation of `rel` cannot work (like check_states):
    - an entry is not a [MTAB blob, slot] pair: a bare blob hands EDF+0x6dc050 a null data pointer and the per-frame
      update derefs it (EDF+0x1101102, the M112 crash of UFOCARRIER301);
    - the list has fewer entries than the class indexes (`need`: the template's count): the class enables slots by
      number unchecked (EDF+0x6dc830, mov byte [rdx], 1 with rdx the missing slot's null player, the M113 crash of
      Hector: 2 entries, the class uses 4);
    - an entry animates no material its model has, unless `inert` says why that is meant (its blob then binds
      nothing: EDF+0x1104350 skips the names it does not find, the slot only has to exist)."""
    found, refs = material_data(files[rel])
    mats = model_materials(games, files, refs)
    for key, entries in found.items():
        if len(entries) < need.get(key, 0):
            raise ValueError(f'{rel}: {key} has {len(entries)} entries, the class indexes {need[key]}')
        for i, e in enumerate(entries):
            blob, slot = e[0], e[1]
            if not (isinstance(blob, bytes) and blob[16:20] == b'MTAB' and isinstance(slot, (int, float))
                    and int(slot) in (0, 1, 2)):
                raise ValueError(f'{rel}: {key}[{i}] is not a [MTAB blob, slot] pair')
            if not inert and not any(n in mats for n in mtab_materials(blob)):
                raise ValueError(f'{rel}: {key}[{i}] animates {mtab_materials(blob)}, the model has {sorted(mats)}')


SE_KEYS = ('se_list', 'se_table', 'ufo_se', 'se_preset_list')   # object keys holding SEPRESET names (docs/sound-re.md section 2)
RESIDENT_BANKS = ('TIKYUUX_SE.ACB',)           # loaded for every mission (docs/sound-re.md section 1)
# Classes the plugin implements whose se_table no EDF6 code reads (EDF6's SEPRESET has none of their names): the plugin plays
# the cues 4.1's SEPRESET gives them by cue name, from a key the build adds and checks against the banks (mother401_cues).
PLUGIN_SOUND_CLASSES = {'UfoMother401': "4.1's Earth Eater: se_table names 4.1 presets (EE...), EDF6's SEPRESET has none; "
                                        "edf41_401_se carries their cues for src/edf41mother401.cpp"}


def sound_cues(games: 'Games') -> tuple[dict[str, str], dict[str, set[str]]]:
    """({SEPRESET name: its cue}, {bank file name: its cues}) of the EDF6 install (cached on `games`)."""
    if not hasattr(games, 'seset'):
        root = dsgo.parse(games.read6('SOUND/SEPRESET.SGO')).root
        games.seset = {n: root.items[i].items[0] for i, n in root.names.items()}
        folder = os.path.join(games.edf6_root, 'SOUND', 'PC')
        games.banks = {}
        for name in os.listdir(folder):
            if name.upper().endswith('.ACB'):
                with open(os.path.join(folder, name), 'rb') as f:
                    games.banks[name.upper()] = set(acb.cue_names(f.read()))
    return games.seset, games.banks


def check_sounds(games: 'Games', data: bytes, rel: str) -> None:
    """ValueError when a sound name of the object (SE_KEYS) cannot play: it is not in SEPRESET.SGO (an empty preset:
    silent), or its cue is in no bank the object lists in game_sound nor in a resident one (the player the cue names is
    null: the M015 crash of UFOCARRIER, the sound system derefs it at EDF+0x240AD0 via 0x7B3310 / 0x6ABF50)."""
    members = dsgo.to_py(dsgo.parse(data).root) if data[:4] == b'DSGO' else sgo.load(data=data)
    if str(members.get('xgs_scene_object_class')).split('_')[0] in PLUGIN_SOUND_CLASSES:
        return
    names = [n for k in SE_KEYS for n in strings(members.get(k)) if n]
    own = {e[0]: e[1] for e in members.get('edf41_se', []) if isinstance(e, list) and len(e) >= 2}
    if not names and not own:
        return
    seset, banks = sound_cues(games)
    loaded = set(RESIDENT_BANKS) | {s.rsplit('/', 1)[-1].upper() for s in strings(members.get('game_sound'))}
    cues = set().union(*(banks.get(b, set()) for b in loaded))
    # edf41_se: 4.1 SEPRESET names a plugin class plays itself, by the cue's name (robo_fix): each cue must be in a bank
    for n, cue in own.items():
        if cue not in cues:
            raise ValueError(f'{rel}: sound {n!r} (edf41_se) plays cue {cue}, which none of {sorted(loaded)} holds')
    for n in names:
        if n in own:
            continue
        if n not in seset:
            raise ValueError(f'{rel}: sound {n!r} is not in SEPRESET.SGO')
        if seset[n] not in cues:
            raise ValueError(f'{rel}: sound {n!r} plays cue {seset[n]}, which none of {sorted(loaded)} holds')


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


def is_ref(s: str) -> bool:
    """A string of a 4.1 SGO naming a file: app:/... or a DLC pack's mp01:/... / mp02:/..."""
    return s.lower().startswith('app:/') or edf41dlc.is_mounted(s)


def ref_rel(s: str) -> str:
    """The Mods path of a file reference: app:/ as rel_of, a DLC's mounted one under its pack's folder (edf41dlc)."""
    return edf41dlc.mods_rel(s) if edf41dlc.is_mounted(s) else rel_of(s)


def pack_folder(rel: str) -> str:
    """The DLC pack folder (EDF41MP01 ...) a Mods path lies in, '' for a story file."""
    parts = rel.split('/')
    folders = {f for _, f in edf41dlc.PACKS.values()}
    return parts[1] if len(parts) > 2 and parts[0] == 'OBJECT' and parts[1] in folders else ''


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
        self._stock: dict[str, dict[str, dict]] | None = None
        self._formats: dict[str, set[bool]] = {}   # class -> {is a DSGO} over its stock SGOs
        # EDF4.1's DLC packs: {mount: Dlc}, and every OBJECT/ file they ship by its Mods path (OBJECT/EDF41MP01/X.SGO)
        self.dlc = edf41dlc.find(edf41)
        self.in4dlc: dict[str, tuple[edf41dlc.Dlc, str]] = {
            rel: (d, inner) for d in self.dlc.values() for rel, inner in d.mods_rels().items()}
        # Story files a pack's object needs in 4.1's form where EDF6 holds another file at that path (shadowed): Mods
        # path in the pack's folder -> the story path it is read from (closure fills it)
        self.moved: dict[str, str] = {}

    def has4(self, rel: str) -> bool:
        """A file EDF4.1 has at Mods path `rel`: its own archives, a DLC pack's, or a shadowed story file moved."""
        return rel in self.in4 or rel in self.in4dlc or rel in self.moved

    def stock_by_class(self) -> dict[str, dict[str, dict]]:
        """EDF6's own top-level SGOs by xgs_scene_object_class: {class: {OBJECT/<name>: members}} (read once)."""
        if self._stock is None:
            self._stock = {}
            for rel in sorted(self.have6):
                if not (rel.startswith('OBJECT/') and rel.endswith('.SGO') and rel.count('/') == 1):
                    continue
                try:
                    members = sgo.load(data=self.read6(rel))
                except Exception:   # noqa: BLE001 - an SGO this reader does not parse is no class evidence
                    continue
                cls = members.get('xgs_scene_object_class')
                if isinstance(cls, str):
                    self._stock.setdefault(cls, {})[rel] = members
                    self._formats.setdefault(cls, set()).add(self.read6(rel)[:4] == b'DSGO')
        return self._stock

    def dsgo_only_classes(self) -> set[str]:
        """The classes whose every stock SGO is a DSGO (EDF6's own form): their code reads the object through the
        dictionary API (EDF+0xD7480), which a classic SGO does not feed."""
        self.stock_by_class()
        return {c for c, f in self._formats.items() if f == {True}}

    def sound(self, rel: str) -> bool:
        return os.path.isfile(os.path.join(self.edf6_root, 'SOUND', 'PC', rel.rsplit('/', 1)[-1]))

    def read4(self, rel: str) -> bytes:
        if rel in self.moved:
            rel = self.moved[rel]
        if rel in self.in4dlc:
            d, inner = self.in4dlc[rel]
            return d.read(inner)
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
        if not games.has4(rel):
            continue   # 4.1 runs without it
        need.append(rel)
        if rel.endswith('.SGO'):
            try:
                members = sgo.load(data=games.read4(rel))
            except Exception as e:  # noqa: BLE001 - a 4.1 file sgo does not read: refused, said why
                why.append(f'{rel}: {e!r}')
                continue
            moves = relocations(games, rel, members)
            todo += [moves.get(ref_rel(s), ref_rel(s)) for s in strings(members) if is_ref(s)]
    return need, why


def shadowed(games: Games, members: dict) -> list[str]:
    """The story files (app:/) the object `members` (a 4.1 SGO, sgo.load) loads its model from that EDF6 holds as
    another file: a model archive at a path EDF6 has whose copy lacks a model (MDB, or one of its LODs) the object
    names, and then the object's CAS when EDF6 has that path too (it animates the 4.1 model's skeleton). EDF6 reused
    some of 4.1's paths for EDF5's remakes: DragonSmall401.mrab holds EDF5's frog dragon, not 4.1's SmallDragon401Green
    that the DLC's DragonSmall401_HS names; the game would load EDF6's archive and find no such model."""
    am = members.get('animation_model')
    if not isinstance(am, list) or not am:
        return []
    out: list[str] = []
    for e in model_entries(am[0]):
        if not (isinstance(e, list) and len(e) > 1 and isinstance(e[0], str) and is_ref(e[0])):
            continue
        rel = ref_rel(e[0])
        if rel not in games.have6 or not games.has4(rel):
            continue
        names = [e[1]] + [lod[0] for lod in (e[2] if len(e) > 2 and isinstance(e[2], list) else [])]
        have = {f.name.lower() for f in mdb.rab_read(games.read6(rel)).files}
        if any(str(n).lower() not in have for n in names):
            out.append(rel)
    cas = am[1] if len(am) > 1 and isinstance(am[1], str) and is_ref(am[1]) else None
    if out and cas and ref_rel(cas) in games.have6 and games.has4(ref_rel(cas)):
        out.append(ref_rel(cas))
    return out


def relocations(games: Games, rel: str, members: dict) -> dict[str, str]:
    """{story path: the path in the pack's folder} for the shadowed files (shadowed()) of the SGO at `rel`, recorded in
    games.moved (read from the story path, shipped at the new one). A pack's object only: a story object's shadowed
    file has no folder of its own to go to (ValueError; none has one, 2026-10-14)."""
    files = shadowed(games, members)
    if not files:
        return {}
    folder = pack_folder(rel)
    if not folder:
        raise ValueError(f'{rel}: names {files}, which EDF6 holds as other files, and is no DLC object to move them for')
    out = {}
    for old in files:
        new = f'OBJECT/{folder}/{old.split("/", 1)[1]}'
        if new in games.in4dlc or games.moved.get(new, old) != old:
            raise ValueError(f'{rel}: {old} would move to {new}, which the pack already ships')
        games.moved[new] = old
        out[old] = new
    return out


def retarget(rel: str, data: bytes, moved: dict[str, str]) -> bytes:
    """A converted SGO with every DLC-mounted path it names as its Mods path's app:/ name (edf41dlc.rewrite), and every
    story path moved into its pack's folder (`moved`, Games.moved: new Mods path -> story path) as that path.
    Unchanged bytes when it names neither (the story objects' files stay byte for byte)."""
    if data[:4] != b'SGO\0':
        return data
    folder = pack_folder(rel)
    version, members = sgo.read(data)
    changed = [False]

    def one(p: str) -> str:
        if edf41dlc.is_mounted(p):
            return edf41dlc.app_path(p)
        if folder and p.lower().startswith('app:/object/'):
            new = f'OBJECT/{folder}/{rel_of(p).split("/", 1)[1]}'
            if moved.get(new) == rel_of(p):
                return f'app:/Object/{folder}/{p.split(":/", 1)[1].split("/", 1)[1]}'
        return p

    def walk(v: object) -> object:
        if isinstance(v, str):
            new = ';'.join(one(p) for p in v.split(';'))
            if new != v:
                changed[0] = True
            return new
        if isinstance(v, list):
            return [walk(x) for x in v]
        return v

    out = {k: walk(v) for k, v in members.items()}
    return sgo.write_depth_first(version, out) if changed[0] else data


def convert(games: Games, rel: str) -> bytes:
    data = games.read4(rel)
    ext = os.path.splitext(rel)[1]
    if ext == '.SGO':
        return retarget(rel, legacy_sgo.convert(data), games.moved)
    if ext in ('.RAB', '.MRAB', '.CAS'):
        return legacy_assets.convert(rel, data)
    if ext == '.SHKT':
        return data
    raise ValueError(f'no conversion for {ext} files')


@dataclass(frozen=True)
class StandIn:
    template: str             # the EDF6 object in its place (OBJECT/<name>, 4.1's SGO format)
    model: tuple[str, str]    # its animation_model[0]: (RAB, MDB)
    override: tuple[tuple[str, object], ...] = ()   # top-level keys set to 4.1's values (the template's differ)


# Files these need that the tools already ship elsewhere: Mods path -> path under the tools' data (not copied again).
SHARED: dict[str, str] = {'OBJECT/V505_TANKEDF4.MRAB': 'edf5port/assets/OBJECT/V505_TANKEDF4.MRAB'}

STAND_INS: dict[str, StandIn] = {
    # 4.1's AI tank (13 missions): class VehicleTank301, keys none of which EDF6's tank (Vehicle505_Tank) has. EDF6's
    # AI tank on EDF5's Blacker No.4.1 model (the same tank, EDF5's remake; P3 ships it converted, SHARED).
    'VEHICLE301_TANK_AI.SGO': StandIn('V505_TANK_AI.SGO', ('app:/Object/v505_tankedf4.mrab', 'v505_tankedf4.mdb'), (
        # 4.1 VehicleTank301's values for the keys EDF6's tank reads the same way (jobs/4bf89026/tmp/behav_rest/
        # NOTES_tank.md): the route-point radius (4.1 0x290200 tests distance^2 < 3^2), the water line. Its attack
        # (turn gain, reverse past 126 deg, turret 20 deg/s, aim gate 0.1 rad, the cannon) is EDF6's already.
        ('game_object_ai_route_update_distance', 3.0),
        ('car_base_submerge_height', 1.7))),
}


def stand_in(games: Games, s: StandIn) -> tuple[bytes, list[str]]:
    """(the SGO in its place, the files it needs EDF6 lacks: SHARED ones)."""
    version, members = sgo.read(games.read6('OBJECT/' + s.template))
    am = members['animation_model']
    am[0] = [s.model[0], s.model[1]]
    for k, v in s.override:
        if k not in members:
            raise ValueError(f'{s.template}: no key {k} to override')
        members[k] = v
    need = [r for r in (rel_of(s.model[0]),) if r not in games.have6]
    missing = [r for r in need if r not in SHARED]
    if missing:
        raise ValueError(f'{missing}: neither EDF6 nor the tools have it')
    return sgo.write_depth_first(version, members), need


# ---- UfoMother301, EDF4.1's mothership (docs/edf41-ufomother301.md) ------------------------------------------------------
# EDF6 has no class that does its job: the plugin registers UfoMother301 and its four part classes (src/edf41mother.cpp: the
# hull on UfoCarrier508's constructor, the parts on EventAnimationObject's, each under a cloned vtable). The hull SGO keeps
# 4.1's class name and its mp_* part lists; what the two EDF6 constructors read unchecked is added here (mother301_fix).
MOTHER301_ROOT = 'UFOMOTHER301.SGO'
# UfoCarrier508's constructor and its base read: CAS players in_ring / out_ring (grafted), the main player's states
# open_state / close_state (the mothership's CAS names them show / hide ... open / close: aliased), the keys of E508_CARRIER
# (setdefault), and the MAB locators 排出口 and ダメージエフェクト by name without asking whether they exist (EDF+0x4F60DD, 0x4F874D
# -> 0x6BADD0): 4.1's hull has neither (only ターゲット), so the carrier's two are added on the hull's waist bone.
MOTHER301_ALIAS = Alias('UfoMother301', 'E508_CARRIER.SGO', ('in_ring', 'out_ring'),
                         state_aliases=(('main', 'close_state', 'close'), ('main', 'open_state', 'open')),
                         # se_list names E508's cues (the bank of UFOCARRIER301.ACB): its bank joins the mothership's own
                         # The hull is 757 m across and flies 600 m up: past 1000 m from the camera the near pass clips it
                         # (docs/view-distance-re.md section 3; the 2026-10-13 real run showed an empty sky). EDF6's own far
                         # carrier E508_CARRIER_FAR.SGO differs from E508_CARRIER.SGO in exactly these two keys.
                         override=(('game_sound', ['app:/sound/adx/tikyuu4_en_MotherShip.acb',
                                                   'app:/sound/adx/tikyuu4_en_UFOcarrier301.acb']),
                                   ('UfoCarryer_EnableFarRender', 1), ('spawn_effect_setting', [0.0, 80.0, 80.0])),
                         inert_materials="the 4.1 mothership has no material animation (its lights are code-driven): E508's "
                                         "entries (Metal, Weakpoint) bind nothing on its model, the slots only have to exist (prototype)")
MOTHER301_LOCATORS = (
    mab_legacy.Locator('排出口', 'waist', 2, (0.0, -40.0, 0.0, 1.0), (5.0, 5.0, 5.0, 1.0), (0.0, 0.0, 3.1415927, 1.0)),
    mab_legacy.Locator('ダメージエフェクト', 'waist', 4, (0.0, -40.0, 0.0, 1.0), 80.0, (0.0, -1.2, 0.0, 1.0)),
)


def se41_table(games: 'Games', prefixes: tuple[str, ...]) -> list[list]:
    """4.1's SEPRESET entries whose name starts with one of `prefixes`, as the plugin's SGO key edf41_se wants them:
    [[name, cue, volume, pitch, a, b, c], ...] (src/edf41classes.h PlaySe: EDF6's SEPRESET has none of 4.1's names; the cues
    are in the tikyuu4_* banks EDF6 ships)."""
    data = games.read4('SOUND/SEPRESET.SGO')
    try:
        table = sgo.load(data=data)
    except Exception:   # noqa: BLE001 - 4.1's classic SGO through the converter
        table = sgo.load(data=legacy_sgo.convert(data))
    out = []
    for name, v in sorted(table.items()):
        if name.startswith(prefixes) and isinstance(v, list) and len(v) >= 6 and isinstance(v[0], str):
            out.append([name, v[0], *(float(x) for x in v[1:6])])
    if not out:
        raise ValueError(f'4.1 SEPRESET has no entry starting with {prefixes}')
    return out


def mother301_fix(games: 'Games', files: dict[str, bytes]) -> None:
    """The hull SGO after the alias: the carrier's two MAB locators added, the model's size recorded for the plugin's log,
    only the files the SGO names kept, the part classes' CAS and model fixes (mother_parts_fix)."""
    rel = 'OBJECT/' + MOTHER301_ROOT
    version, members = sgo.read(files[rel])
    # mp_* (the parts: [sgo path, euler degrees, position]) and resource stay: the plugin makes the parts from them (phase 2 step 1)
    # and the six part SGOs, their models, CASs and ragdolls come over with the hull.
    am = members['animation_model']
    am[2] = mab_legacy.mab_add_locators(mab_block(am[2]), list(MOTHER301_LOCATORS))
    # What the plugin logs back at creation to show the game was given this model: [mdb name, half extents x y z, bones]
    # (src/edf41mother.cpp; read from the file, so a different model in the archive changes it).
    rab_rel = rel_of(am[0][0][0])
    body = next(f for f in mdb.rab_read(files[rab_rel]).files if f.name.lower() == am[0][0][1].lower())
    md = mdb.mdb_read(body.data)
    ext = [max(abs(x) for x in mdb.rigid_extent(md, ax)) for ax in range(3)]
    members['edf41_mother_model'] = [am[0][0][1], *ext, len(md.bones)]
    members['edf41_se'] = se41_table(games, ('母船UFO',))   # the hull's table: its parts play from it too
    files[rel] = sgo.write_depth_first(version, members)
    prune(files, rel)
    mother_parts_fix(files)


# The part classes (UfoMother301_Hatch / _Panel / _Cannon / _BigCannon) are built by the plugin on EventAnimationObject
# (src/edf41mother.cpp): its constructor asks the CAS player "main" for the state "default". 4.1's part CASs have no such state:
# it is an alias of the state the part rests in (the cannon's CAS has only "appear"; the others rest in "close_state").
MOTHER_PART_REST = {'UFOMOTHER301_CANNON.CAS': 'appear'}


def mother_parts_fix(files: dict[str, bytes]) -> None:
    for rel in [r for r in files if r.startswith('OBJECT/UFOMOTHER301_') and r.endswith('.CAS')]:
        rest = MOTHER_PART_REST.get(rel.split('/', 1)[1], 'close_state')
        files[rel] = cas_graft.alias_states(files[rel], {'main': [('default', rest)]})
    # The panels have no CAS in 4.1 (animation_model[1] is the int 0); EventAnimationObject's constructor asks for the player "main",
    # so they get the hatch's (its clips name bones the panel's model lacks: nothing binds, the state only has to exist).
    for rel in [r for r in files if r.startswith('OBJECT/UFOMOTHER301_PANEL') and r.endswith('.SGO')]:
        version, members = sgo.read(files[rel])
        am = members['animation_model']
        if isinstance(am[1], int):
            am[1] = 'app:/object/UfoMother301_Hatch.cas'
            files[rel] = sgo.write_depth_first(version, members)



# ---- UfoMother401, EDF4.1's Earth Eater (docs/edf41-ufomother401.md) -------------------------------------------------------
# A hull with no model of its own that lays out up to 361 hexagonal panels (19 x 19, panel_layout_map*) by its action_list; the
# panels (UfoMother401_Panel01 / _WeakPanel / _Core / _BigCannon01 / _Generator / _FinalCannon01 / _FinalGenerator) and the gun
# batteries hung on Panel01's 砲台 locators (_GunBattery_SmallLaser / _Sniper / _PlasmaCannon / _RapidCannon) are real objects. The
# plugin registers all twelve classes (src/edf41mother401.cpp), every one built on EventAnimationObject (a model, a CAS player
# "main", a ragdoll, durability). Every variant SGO keeps 4.1's class and keys; what EventAnimationObject's constructor reads
# unchecked and what the plugin reads from the model are added (mother401_fix). The variants by mission (4.1's BVM scripts,
# jobs/14618f05/tmp/re401/notes_D_missions.md): the base file M018; 401M009 M009, M012 M012, M019 M019, M201 M201, M412 M412,
# M550 M516, M551 M312, M552 M145, M553 M142, M554 M518, M555 M149, M556 M132, M557 M308, M602 M602; DLC pack 2's two M809, MP503.
MOTHER401_ROOTS: dict[str, int] = {
    'UFOMOTHER401.SGO': 1, 'UFOMOTHER401M009.SGO': 1, 'UFOMOTHER401M012.SGO': 1, 'UFOMOTHER401M019.SGO': 1,
    'UFOMOTHER401M201.SGO': 1, 'UFOMOTHER401M412.SGO': 1, 'UFOMOTHER401M550.SGO': 1, 'UFOMOTHER401M551.SGO': 1,
    'UFOMOTHER401M552.SGO': 1, 'UFOMOTHER401M553.SGO': 1, 'UFOMOTHER401M554.SGO': 1, 'UFOMOTHER401M555.SGO': 1,
    'UFOMOTHER401M556.SGO': 1, 'UFOMOTHER401M557.SGO': 1, 'UFOMOTHER401M602.SGO': 1,
}
MOTHER401_DLC_ROOTS: dict[str, int] = {'EDF41MP02/UFOMOTHER401_DLC_M809.SGO': 1, 'EDF41MP02/UFOMOTHER401_DLC_MP503.SGO': 1}
MOTHER401_HULL = 'UfoMother401'
# The hull has no model, collision or durability of its own in 4.1; EventAnimationObject's constructor reads animation_model and
# ragdoll unchecked (EDF+0x5B60C9 hands the model loader whatever the key gave, 0x5B61ED throws on a missing ragdoll). It gets the
# model and ragdoll of a part of its own set, the smallest the variant has (the small laser battery, 10 x 30 m, else a panel);
# the plugin builds the hull far under its spawn point and never moves that model or ragdoll again (its Update skips
# EventAnimationObject's own, src/edf41mother401.cpp).
MOTHER401_HULL_DONORS = ('UfoMother401_GunBattery_SmallLaser', 'UfoMother401_WeakPanel', 'UfoMother401_Panel01')
# 4.1's panels and batteries mostly have no CAS (animation_model[1] is 0); EventAnimationObject's constructor asks the player main
# for the state default. They get the flat panel's CAS with every clip emptied (cas_graft.hollow): the players and states exist,
# nothing moves. The two CASs 4.1 has (cannon panel, flat panel: open / close / open_state / close_state) rest in close_state.
MOTHER401_FLAT_CAS = 'OBJECT/MOTHERSHIP401_FLATPANEL.CAS'
MOTHER401_CASES = (MOTHER401_FLAT_CAS, 'OBJECT/MOTHERSHIP401_CANNONPANEL.CAS')
MOTHER401_STILL_CAS = 'OBJECT/UFOMOTHER401_STILL.CAS'
MOTHER401_STILL_APP = 'app:/Object/UfoMother401_Still.cas'
# Points the plugin needs on a part's model, from 4.1's MAB locators (node bind matrix x the locator's position; the panels are
# rigid, so the rest pose is the pose): the battery mounts (Panel01's 砲台, each one), the muzzle (発射口: BigCannon01,
# FinalCannon01, the batteries), the spawn point of a generator (出現位置 / 排出位置), the lock-on point (ターゲット: the MAB's
# list 1, the list EDF6's lock-point component 0x6C74E0 hands to AddLockTarget 0x547A50; EventAnimationObject has no such
# component, so the plugin adds the point itself), and the weak body's bone (weakpoint).
MOTHER401_POINTS = (('edf41_401_mounts', ('砲台',), True), ('edf41_401_muzzle', ('発射口',), False),
                    ('edf41_401_spawn', ('出現位置', '排出位置'), False), ('edf41_401_lock', ('ターゲット',), False))


# The charge glows: 4.1 hangs a Particle3_k emitter (ParticleObject 0x1c2d80) on the muzzle of BigCannon01 (0x262684),
# FinalCannon01 and FinalGenerator (jobs/14618f05/tmp/re401/notes_A 1.5, notes_B 1.3 / 3.1) and switches it on while charging /
# firing, coloured per state. EDF6 has the same emitter behind gunbattery::ChargeEffect_StdParticle, whose SGO form E510's
# batteries carry (charge_effect_std: [[], ['ChargeEffect_StdParticle', [[key, value, ...], [se]]]]; the keys are EDF6's
# parser table 0x303710: 3 / 4 ints, 8 [0: the flag byte +0x50], 11 [1: +0xb8, 2: +0xbc], 12 [1: +0xc4, 2: +0xc8, 3: +0xcc],
# 13 [1: colour +0xe0, 3: colour +0x100], 15 [0: texture]); 4.1's block writes the same offsets: ints 4 / 4 / 2, +0x50 = 1,
# +0xb8 = 0, +0xc4 / +0xc8 / +0xcc = 80 / 100 / -25, colours +0xe0 and (0, 0, 0, 1) at +0x100. The colour of each glow the
# class switches on (one ChargeEffect per colour); the se is EDF6's (empty: the plugin plays 4.1's charge sounds itself).
MOTHER401_GLOWS = {
    'UfoMother401_BigCannon01': ((1.0, 1.0, 3.0, 1.0),),                               # charge and fire
    'UfoMother401_FinalCannon01': ((10.0, 1.0, 5.0, 1.0), (1.0, 1.0, 5.0, 1.0), (2.0, 2.0, 10.0, 1.0)),   # genocide charge,
    'UfoMother401_FinalGenerator': ((1.0, 10.0, 5.0, 1.0),),                           # lightning charge, lightning fire; summon
}


def mother401_glow(colour: tuple) -> list:
    """One EDF6 ChargeEffect_StdParticle node of 4.1's muzzle emitter in `colour` (MOTHER401_GLOWS)."""
    keys = [2, 4, 3, 4, 4, 2, 8, [0, 1], 11, [1, 0.0, 2, 0.0], 12, [1, 80.0, 2, 100.0, 3, -25.0],
            13, [1, [float(c) for c in colour], 3, [0.0, 0.0, 0.0, 1.0]], 15, [0, 'Particle3_k.dds']]
    return [[], ['ChargeEffect_StdParticle', [keys, ['']]]]


def mother401_point(md: 'mdb.Mdb', world: list, node: str, a: tuple) -> list[float]:
    m = world[mdb.bone_named(md, node)]
    return [round(a[0] * m[0 + j] + a[1] * m[4 + j] + a[2] * m[8 + j] + m[12 + j], 4) for j in range(3)]


def mother401_part(files: dict[str, bytes], rel: str) -> None:
    """One part SGO of the 401 set (class UfoMother401_*): the still CAS where 4.1 has none, the plugin's points added."""
    version, members = sgo.read(files[rel])
    am = members['animation_model']
    if isinstance(am[1], int):
        am[1] = MOTHER401_STILL_APP
    entry = model_entries(am[0])[0]
    by_name = {f.name.lower(): f for f in mdb.rab_read(files[rel_of(entry[0])]).files}
    md = mdb.mdb_read(by_name[entry[1].lower()].data)
    world = mdb.bind_world(md)
    locs = [loc for _i, loc in mab_legacy.mab_locators(mab_block(am[2]))] if isinstance(am[2], bytes) else []
    for key, names, many in MOTHER401_POINTS:
        found = [mother401_point(md, world, loc.node, loc.a) for loc in locs if loc.name in names]
        if found:
            members[key] = found if many else found[0]
    weak = members.get('weakpoint')
    if isinstance(weak, str):
        m = world[mdb.bone_named(md, weak)]
        members['edf41_401_weak'] = [round(m[12 + j], 4) for j in range(3)]
    glows = MOTHER401_GLOWS.get(str(members.get('xgs_scene_object_class')))
    if glows:
        members['edf41_401_charge'] = [mother401_glow(c) for c in glows]
    files[rel] = sgo.write_depth_first(version, members)


def mother401_cues(games: 'Games', files: dict[str, bytes], rel: str) -> None:
    """edf41_401_se of a 401 SGO: per se_table entry, 4.1's preset for that name ([cue, volume, pitch, near, far, priority],
    4.1's SOUND/SEPRESET.SGO) or 0 where se_table has 0. ValueError when a name is no 4.1 preset or its cue is in neither the
    banks the SGO lists in game_sound nor a resident one."""
    version, members = sgo.read(files[rel])
    table = members.get('se_table')
    if not isinstance(table, list):
        return
    if not hasattr(games, 'seset41'):
        games.seset41 = sgo.load(data=games.read4('SOUND/SEPRESET.SGO'))
    _seset, banks = sound_cues(games)
    loaded = set(RESIDENT_BANKS) | {s.rsplit('/', 1)[-1].upper() for s in strings(members.get('game_sound'))}
    cues = set().union(*(banks.get(b, set()) for b in loaded))
    out: list = []
    for name in table:
        if not isinstance(name, str) or not name:
            out.append(0)
            continue
        preset = games.seset41.get(name)
        if not isinstance(preset, list) or not preset or preset[0] not in cues:
            raise ValueError(f'{rel}: se_table {name!r} is {preset!r} in 4.1, no cue of {sorted(loaded)}')
        out.append([preset[0], *[float(x) for x in preset[1:]]])
    members['edf41_401_se'] = out
    files[rel] = sgo.write_depth_first(version, members)


def mother401_fix(games: 'Games', files: dict[str, bytes]) -> None:
    """The converted files of one 401 variant: the hull SGO with a part's model and ragdoll (MOTHER401_HULL_DONORS), every
    part SGO through mother401_part, every SGO's sounds as cues (mother401_cues), the CASs with a default state, the still CAS
    added. Idempotent (a second variant gets the files the first fixed) and the same bytes for a shared file whichever variant
    fixes it."""
    if MOTHER401_FLAT_CAS not in files:
        raise ValueError(f'{MOTHER401_FLAT_CAS} is not in the closure: no CAS to base the still one on')
    for rel in MOTHER401_CASES:
        if rel in files and 'default' not in cas_graft.states(files[rel]).get('main', set()):
            files[rel] = cas_graft.alias_states(files[rel], {'main': [('default', 'close_state')]})
    files[MOTHER401_STILL_CAS] = cas_graft.hollow(files[MOTHER401_FLAT_CAS])
    hulls, parts = [], {}
    for rel in sorted(r for r in files if r.endswith('.SGO')):
        cls = str(sgo.load(data=files[rel]).get('xgs_scene_object_class'))
        if cls == MOTHER401_HULL:
            hulls.append(rel)
        elif cls.startswith(MOTHER401_HULL + '_'):
            mother401_part(files, rel)
            parts.setdefault(cls, rel)
        if cls.startswith(MOTHER401_HULL):
            mother401_cues(games, files, rel)
    if len(hulls) != 1:
        raise ValueError(f'the closure holds {len(hulls)} UfoMother401 hulls: {hulls}')
    donor = next((parts[c] for c in MOTHER401_HULL_DONORS if c in parts), None)
    if donor is None:
        raise ValueError(f'the closure holds none of {MOTHER401_HULL_DONORS}: no model for the hull')
    part = sgo.read(files[donor])[1]
    version, members = sgo.read(files[hulls[0]])
    members['animation_model'] = copy.deepcopy(part['animation_model'])
    members['ragdoll'] = copy.deepcopy(part['ragdoll'])
    files[hulls[0]] = sgo.write_depth_first(version, members)
    prune(files, hulls[0])


# Native classes the plugin implements (src/edf41mother.cpp ...): their root's alias, then this fix of the converted files.
ROOTS[MOTHER301_ROOT] = 6   # M016 M110 M123 M130 M210 M603 (DLC pack 1's M993 names the same story file)
ALIASES[MOTHER301_ROOT] = MOTHER301_ALIAS
NATIVE_FIXES: dict[str, Callable[['Games', dict[str, bytes]], None]] = {MOTHER301_ROOT: mother301_fix}
# The 401 has no stock template (no alias): its fix runs on 4.1's converted files as they are.
ROOTS.update(MOTHER401_ROOTS)
NATIVE_FIXES.update({root: mother401_fix for root in (*MOTHER401_ROOTS, *MOTHER401_DLC_ROOTS)})


# ---- UfoRobo, EDF4.1's transforming ship / walking robot (docs/edf41-uforobo.md) -----------------------------------------
# EDF6 has no class that flies and walks as one object (the script keeps one handle across the transformation: M007 registers
# its durability events on it after ObjectAction 0). The plugin registers UfoRobo itself (src/edf41robo.cpp: the object on
# EventAnimationObject's constructor, which reads only animation_model and ragdoll, under a cloned vtable) and runs 4.1's state
# machines. No alias: the SGO keeps 4.1's class name and every 4.1 key the plugin reads (robo_*_muzzle_setting,
# robo_attack_table, se_table, the bullet classes and custom parameters, ik_control_info, game_object_durability). robo_fix adds
# what EventAnimationObject and the plugin need (below).
ROBO_ROOT = 'UFOROBO401.SGO'
ROBO_ROOT_BONE = 'globalSRT'
# The MAB events the plugin raises itself, as it plays the clips (EDF6's EventAnimationObject registers no handler for 4.1's
# events): kind numbers of edf41_robo_events (src/edf41robo.cpp EventKind). 4.1 registers 攻撃判定 / 砲撃開始 / 砲撃終了 /
# ジェノサイド砲発射 (EDF41.exe 0x2732B0 -> 0x277790 / 0x277C10 / 0x277D90 / 0x277DB0); se / se_stop are the engine's own.
ROBO_EVENTS = {'se': 0, 'se_stop': 1, '攻撃判定': 2, '砲撃開始': 3, '砲撃終了': 4, 'ジェノサイド砲発射': 5}
# SEPRESET names 4.1's class code plays by name (the guns' charge / shot / hit cues, the flying buster charge: EDF41.exe
# 0x27B0A0 / 0x2744B0 / 0x278480), besides se_table, ragdoll_contact and the MAB's se events.
ROBO_CLASS_SE = ('ロボ小ビームチャージ', 'ロボ小ビーム発射', 'ロボビーム着弾小', 'ロボ飛行形態中型キャノンチャージ', 'ロボ肩ビーム発射',
                 'ロボビーム着弾中', 'ロボバスター砲発射', 'ロボビーム着弾大', 'ロボ飛行形態バスター砲チャージ')
# Names the plugin looks up in the files and uses (check_class_names holds the object to them): the CAS states its state
# machines play (and default, which EventAnimationObject's constructor plays), the muzzle / attack range / foot locators.
ROBO_STATES = ('default', 'ship', 'transform', 'stand', 'fall', 'fall_land', 'walk_f_nl', 'walk_f_lr', 'walk_f_rl',
               'walk_f_ln', 'walk_f_rn', 'walk_b_nl', 'walk_b_lr', 'walk_b_rl', 'walk_b_ln', 'walk_b_rn', 'side_step_l',
               'side_step_r', 'turn_l45', 'turn_r45', 'turn_l90', 'turn_r90', 'turn_l180', 'turn_r180', 'attack_chop_l',
               'attack_chop_r', 'attack_stomp_l', 'attack_stomp_r', 'attack_fire360', 'attack_fire_cross',
               'attack_fire_genocide', 'attack_fire_straight', 'dead')
ROBO_LOCATORS = ('muzzle_small_l', 'muzzle_small_r', 'muzzle_middle_l', 'muzzle_middle_r', 'muzzle_big_l', 'muzzle_big_r',
                 '右手攻撃範囲', '右手攻撃範囲2', '右手攻撃範囲3', '左手攻撃範囲', '左手攻撃範囲2', '左手攻撃範囲3', '右足歩行衝撃',
                 '左足歩行衝撃', 'ロックオン')
# The plugin's own limits (src/edf41robo.cpp): longer tables are refused there, so they are refused here.
ROBO_MOST_EVENTS = 24        # kMostEvents: per clip
ROBO_SE_PREFIXES = ('ロボ',)   # 4.1's SEPRESET entries copied into edf41_se (se41_table): every name the robot plays
ROBO_MOST_MOTION_KEYS = 512  # kMostMotionKeys



def robo_events(cas: bytes, mab: bytes) -> list:
    """edf41_robo_events: [[CAS state, clip number in the state, [[time, kind, text, a, b, c], ...]], ...] for every state of
    the CAS's main player whose clips have MAB events: the clips a state plays (its play data command's tcontrol, in order;
    attack_fire_genocide plays attack_fire_genocide_charge then attack_fire_genocide), each with its track's keys (ROBO_EVENTS
    kinds; time in clip units, the CAS player's +0xE8). se: text the 4.1 SEPRESET name; 攻撃判定: text the locator, a damage
    (default 10), b scale (1), c quake (0.2: EDF41.exe 0x277790); 砲撃開始 / ジェノサイド砲発射: a charge_frame."""
    clips = cas_graft.state_clips(cas)['main']
    tracks = mab_legacy.mab_event_params(mab)
    out = []
    for state, names in sorted(clips.items()):
        for n, clip in enumerate(names):
            keys = []
            for key, time, p in tracks.get(clip, []):
                if key not in ROBO_EVENTS:
                    raise ValueError(f'MAB track {clip}: event {key} is not one the plugin raises')
                kind = ROBO_EVENTS[key]
                text = p.get('name', p.get('collision', ''))
                a = float(p.get('damage', 10.0) if kind == 2 else p.get('charge_frame', 0) if kind in (3, 5) else
                          p.get('channel', 0))
                b = float(p.get('scale', 1.0))
                c = float(p.get('quake', 0.2))
                keys.append([float(time), kind, str(text), a, b, c])
            if keys:
                if len(keys) > ROBO_MOST_EVENTS:
                    raise ValueError(f'MAB track {clip}: {len(keys)} events, the plugin takes {ROBO_MOST_EVENTS}')
                out.append([state, n, keys])
    return out


def robo_motion(cas: bytes) -> tuple[list, set[str]]:
    """(edf41_robo_motion, the clips it covers): [[CAS state, clip length, [x, z, yaw, ...] per key], ...] for the states whose
    one clip moves the root bone (ROBO_ROOT_BONE: the walks, side steps and turns), relative to the clip's start (cas_pose
    root_motion). 4.1 moves the object by the clip's root motion (EDF41.exe 0x276DFA reads the player's root motion; the
    robot's horizontal speed is that motion x 60, turned by the yaw it had when the step began, 0x324); the plugin does the
    same from these keys and the bone's own tracks are dropped (cas_pose.strip_bone) so the model does not move twice."""
    pose = cas_pose.CasPose(cas)
    by_name = {c.name: c for c in pose.clips}
    out, moved = [], set()
    for state, names in sorted(cas_graft.state_clips(cas)['main'].items()):
        motions = [(n, cas_pose.root_motion(pose, by_name[n], ROBO_ROOT_BONE)) for n in names]
        motions = [(n, m) for n, m in motions if m]
        if not motions:
            continue
        if len(names) != 1:
            raise ValueError(f'CAS state {state} moves the root over {len(names)} clips: the plugin follows one')
        name, keys = motions[0]
        if len(keys) > ROBO_MOST_MOTION_KEYS:
            raise ValueError(f'clip {name}: {len(keys)} root motion keys, the plugin takes {ROBO_MOST_MOTION_KEYS}')
        duration, _step, _frames = cas_pose.clip_info(pose, by_name[name])
        out.append([state, float(duration), [round(v, 4) for k in keys for v in k]])
        moved.add(name)
    return out, moved


def robo_fix(games: Games, files: dict[str, bytes]) -> None:
    """UfoRobo401's files for the plugin's class (src/edf41robo.cpp):
    - CAS: main gets `default` (EventAnimationObject's constructor plays it; 4.1's class starts in ship) as an alias of
      ship; the root bone's tracks of the moving clips dropped (robo_motion);
    - edf41_robo_events (robo_events), edf41_robo_motion (robo_motion); material_animation dropped (below);
    - edf41_se: [[4.1 SEPRESET name, cue, volume, pitch, inner, reference, cull], ...] for every name the object plays (se_table, ragdoll_contact,
      the MAB's se events, ROBO_CLASS_SE; se41_table's form, all of 4.1's ロボ* entries): EDF6's SEPRESET has none of 4.1's robot names (EDF6 ships the bank,
      TIKYUU4_EN_UFOROBO.ACB, with every en_Robo_* cue, but no preset naming them), so the plugin plays the cues by name
      with 4.1's volume and pitch; check_sounds holds each cue to the banks the object loads."""
    rel = 'OBJECT/' + ROBO_ROOT
    version, members = sgo.read(files[rel])
    am = members['animation_model']
    cas_rel = rel_of(am[1])
    mab = mab_block(am[2])
    cas = files[cas_rel]
    # material_animation: 4.1's class drives it itself (EDF41.exe 0x2713F0 / 0x273FB2, the hull's lights) as a bare MTAB blob,
    # the form EDF6's material code faults on (check_materials); EventAnimationObject's constructor does not read the key
    # and the plugin does not drive the lights: dropped.
    members.pop('material_animation', None)
    members['edf41_robo_events'] = robo_events(cas, mab)
    members['edf41_robo_motion'], moved = robo_motion(cas)
    cas = cas_pose.strip_bone(cas, moved, ROBO_ROOT_BONE)
    files[cas_rel] = cas_graft.alias_states(cas, {'main': [('default', 'ship')]})
    table = se41_table(games, ROBO_SE_PREFIXES)
    names = list(strings(members.get('se_table'))) + [members['ragdoll_contact'][0]] + list(ROBO_CLASS_SE)
    names += [e[2] for _s, _n, keys in members['edf41_robo_events'] for e in keys if e[1] == ROBO_EVENTS['se']]
    missing = sorted(set(names) - {e[0] for e in table})
    if missing:
        raise ValueError(f'{rel}: sounds {missing} are not in EDF4.1\'s SEPRESET.SGO')
    # only what the robot plays: 4.1 also has ロボ３６０停止 (en_Robo_fire360_shotStop), which no MAB event or code plays and
    # EDF6's bank lacks (check_sounds holds every edf41_se cue to the loaded banks)
    members['edf41_se'] = [e for e in table if e[0] in set(names)]
    files[rel] = sgo.write_depth_first(version, members)


ROOTS[ROBO_ROOT] = 2   # M007 M411 (DLC pack 1's M415 / M416 and pack 2's M010 name the same story file)
NATIVE_FIXES[ROBO_ROOT] = robo_fix   # no alias: build() runs it on the converted closure before the checks

def alias(games: Games, root: str, files: dict[str, bytes], a: Alias) -> None:
    """In `files` (the converted closure of OBJECT/`root`): its SGO under class a.cls with a.template's keys it
    lacks, its CAS with a.template's players it lacks (when the CAS is one of the files; else EDF6's own is used).
    A template in EDF6's DSGO form goes through alias_dsgo."""
    rel = 'OBJECT/' + root
    t_data = games.read6('OBJECT/' + a.template)
    if t_data[:4] == b'DSGO':
        alias_dsgo(games, root, files, a, t_data)
        return
    version, members = sgo.read(files[rel])
    t_version, theirs = sgo.read(t_data)
    if t_version != version:
        raise ValueError(f'{a.template}: SGO {t_version:#x}, not {version:#x} as {root}')
    members['xgs_scene_object_class'] = a.cls
    for k, v in theirs.items():
        if k not in a.drop:
            members.setdefault(k, v)
    for k in a.take:
        members[k] = theirs[k]
    if a.fracture:
        members['animation_model'] = with_fracture(files, members['animation_model'], theirs['animation_model'], root)
    for k, v in a.override:
        members[k] = v
    if a.material_from:
        members['material_animation'] = sgo.read(legacy_sgo.convert(games.read4('OBJECT/' + a.material_from)))[1][
            'material_animation']
    for k, layout in a.material_pairs:
        members[k] = material_pairs(members[k], layout)
    if a.se_prefix[0]:
        for k in SE_KEYS:
            if k in members:
                members[k] = [a.se_prefix[1] + n[len(a.se_prefix[0]):] if n.startswith(a.se_prefix[0]) else n
                              for n in members[k]]
    for k in a.pad:
        mine, full = members.get(k), theirs.get(k)
        if isinstance(mine, list) and isinstance(full, list) and len(mine) < len(full):
            members[k] = mine + full[len(mine):]
    if a.bones:
        for entry in model_entries(members['animation_model'][0]):
            rab_rel = rel_of(entry[0])
            if rab_rel in files:
                files[rab_rel] = reboned(files[rab_rel], a.bones)
    if a.mab:
        model = members['animation_model']
        model[2] = remabbed(model[2], a.mab)
    for locator, node in a.locator_nodes:
        members['animation_model'][2] = mab_legacy.mab_retarget(members['animation_model'][2], locator, node)
    if a.add_locators:
        members['animation_model'][2] = mab_legacy.mab_add_locators(members['animation_model'][2], list(a.add_locators))
    am = members.get('animation_model')
    cas = rel_of(am[1]) if isinstance(am, list) and len(am) > 1 and isinstance(am[1], str) else None
    t_am = theirs.get('animation_model')   # a part (animation_model_d) has none: no CAS to graft
    if a.stock_cas and cas and cas not in files:
        files[cas] = convert(games, cas) if cas in games.in4 else games.read6(cas)
    if a.own_cas:
        if cas:
            raise ValueError(f'{root} has a CAS of its own: own_cas is for an object without one')
        cas = 'OBJECT/' + root.rsplit('.', 1)[0] + '.CAS'
        files[cas] = cas_graft.hollow(games.read6(rel_of(t_am[1])))
        members['animation_model'] = [am[0], 'app:/' + cas.lower(), *am[2:]]
    if a.post:
        a.post(members)
    files[rel] = sgo.write_depth_first(version, members)
    if a.prune:
        prune(files, rel)
    check_fracture(members, files, rel)
    graft_cas(games, a, files, cas, rel_of(t_am[1]) if t_am and isinstance(t_am[1], str) else '')
    check_materials(games, files, rel, {k: len(theirs[k]) for k in MATERIAL_KEYS if k in theirs}, a.inert_materials)
    check_sounds(games, files[rel], rel)


def hidden_model(rab: bytes, donor_mdb: str, name: str) -> bytes:
    """A model archive holding one model `name`: `donor_mdb` of `rab` (its bones, so the donor's ragdoll fits) with no mesh,
    material or texture: a part the class needs a model for where 4.1's has none (FortressPart.hidden)."""
    archive = mdb.rab_read(rab)
    f = next(x for x in archive.files if x.name.lower() == donor_mdb.lower())
    md = mdb.mdb_read(f.data)
    for ob in md.objects:
        ob.meshes = []
    md.materials, md.textures, md.buffer_order = [], [], None
    raw = mdb.mdb_write(md)
    one = mdb.RabFile(name, f.folder, f.flag, mdb.cmpl_compress(raw) if f.stored[:4] == b'CMPL' else raw)
    return mdb.rab_write(mdb.Rab(archive.version, archive.folders, [one]))


def fortress_part(games: 'Games', files: dict[str, bytes], root: str, p: FortressPart) -> None:
    """files['OBJECT/' + root] (4.1's fortress part, converted) as a MovingFortress_Battery doing what 4.1's part does
    (FortressPart): p.template's keys but the other weapons', 4.1's model (or a meshless one, p.hidden), ragdoll, HP and
    material animation, its MAB with the locators the class looks up, the weapon, aim and timing of p."""
    rel = 'OBJECT/' + root
    version, mine = sgo.read(files[rel])
    t_version, theirs = sgo.read(games.read6('OBJECT/' + p.template))
    if t_version != version:
        raise ValueError(f'{p.template}: SGO {t_version:#x}, not {version:#x} as {root}')
    out = {k: v for k, v in theirs.items() if k not in FORTRESS_PART_DROP}
    for k, v in mine.items():
        if k.startswith('game_object_') or k in ('animation_model', 'ragdoll'):
            out[k] = v
    out['xgs_scene_object_class'] = theirs['xgs_scene_object_class']
    t_am = theirs['animation_model']
    if p.hidden:
        donor = sgo.read(files['OBJECT/ALIEN4LEGTANK301_BIT.SGO'])[1]   # the bit: the smallest model and ragdoll of the set
        body_rab = rel_of(donor['animation_model'][0][0][0])
        own = 'OBJECT/' + root.rsplit('.', 1)[0] + '.MRAB'
        files[own] = hidden_model(files[body_rab], donor['animation_model'][0][0][1], p.hidden)
        mab = donor['animation_model'][2]
        out['animation_model'] = [[['app:/Object/' + own.rsplit('/', 1)[-1].lower(), p.hidden]], 0, mab,
                                  [list(t_am[3][0]), [-1.0, -1.0, -1.0], [1.0, 1.0, 1.0]]]
        out['ragdoll'] = donor['ragdoll']
        out['material_animation'] = theirs['material_animation']
    else:
        am = mine['animation_model']
        entry = am[0][0]
        body = mdb.mdb_read(next(f for f in mdb.rab_read(files[rel_of(entry[0])]).files
                                 if f.name.lower() == entry[1].lower()).data)
        box = [mdb.rigid_extent(body, ax) for ax in range(3)]
        out['animation_model'] = [am[0], 0, am[2], [list(t_am[3][0]), [float(lo) for lo, _ in box],
                                                    [float(hi) for _, hi in box]]]
        out['material_animation'] = material_pairs(mine['material_animation'], ((0, 0), (1, 0), (1, 1)))
    bone, at, pivot = p.muzzle
    out['animation_model'][2] = mab_legacy.mab_add_locators(out['animation_model'][2], [
        mab_legacy.Locator('AIM_TARGET', bone, 0, (*at, 1.0), 5.0),
        mab_legacy.Locator('AIM', bone, 0, (*pivot, 1.0), 5.0),
        mab_legacy.Locator('AIM_AXIS', bone, 2, (*pivot, 1.0), (5.0, 5.0, 5.0, 1.0), (0.0, 0.0, 0.0, 1.0)),
        mab_legacy.Locator('爆破A', bone, 0, (*pivot, 1.0), 3.0)])
    if p.durability is not None:
        out['game_object_durability'] = p.durability
    out['game_sound'] = FORTRESS_BANKS
    out['bullet_edf41'] = p.bullet
    out['battery_config'] = ['AIM_TARGET', p.fire, 'charge_effect_std', p.charge]
    out['attack'] = [[0, p.wait]]
    out['attack_target_range'] = [0, 100000]
    out['aim_config'] = p.aim
    out['fort_parts_destroy_info'] = p.destroy
    files[rel] = sgo.write_depth_first(version, dict(sorted(out.items())))
    stock = games.stock_by_class()
    check_class_keys(stock, rel, files[rel])
    check_materials(games, files, rel, {'material_animation': len(theirs['material_animation'])},
                    "the meshless cannon binds no material: the template's entries only have to exist" if p.hidden else '')
    check_sounds(games, files[rel], rel)
    check_battery(files, rel)


def check_battery(files: dict[str, bytes], rel: str) -> None:
    """ValueError when a MovingFortress_Battery SGO cannot fire or die: every name the class reads unchecked must be there:
    the bullet key battery_config[1][0] names (0x4d403e), the charge effect key [2], the locators AIM_TARGET / AIM /
    AIM_AXIS (0x4cf4e5..), the muzzle [0] and each fort_parts_destroy_info[5] locator (0x6badd0, list 0 of its MAB)."""
    members = sgo.read(files[rel])[1]
    cfg = members.get('battery_config')
    if not isinstance(cfg, list) or len(cfg) != 4:
        raise ValueError(f'{rel}: battery_config is not [muzzle, fire row, charge effect, charge frames]')
    for key in (cfg[1][0], cfg[2], 'attack', 'aim_config', 'ragdoll', 'se_table', 'fort_parts_destroy_info'):
        if key not in members:
            raise ValueError(f'{rel}: no key {key}, which the class reads unchecked')
    names = {loc.name for lst, loc in mab_legacy.mab_locators(members['animation_model'][2]) if lst == 0}
    want = {'AIM_TARGET', 'AIM', 'AIM_AXIS', cfg[0], *members['fort_parts_destroy_info'][5]}
    if want - names:
        raise ValueError(f'{rel}: its MAB has no locator {sorted(want - names)}')


def material_pairs(blobs: list, layout: tuple[tuple[int, int], ...]) -> list:
    """[blob, ...] (4.1's bare MTAB blobs, bytes as sgo.read gives them) -> [[blob, slot], ...], the form the classes'
    reader takes (EDF+0x6dc6b0 / Deiroi 0x31d540: a sub-array per entry, blob first, int slot at +0x14; a bare blob gave
    it a null data pointer, which the per-frame update derefs, EDF+0x1101102). `layout` lists the entries the class
    indexes, each (index of 4.1's blob, slot): 4.1 passes slot 0 with the blob whose first dword is 1 (a loop) and 2 with
    the others (UfoCarrier301 0x24bb10, Deiroi 0x1de02f / 0x1de06c); the classes index more entries than 4.1 has, so
    some blobs serve twice. Already pairs: kept."""
    if not all(isinstance(b, bytes) for b in blobs):
        return blobs
    return [[blobs[i], slot] for i, slot in layout]


def dsgo_value(v: object) -> dsgo.Value:
    """A value of 4.1's SGO (sgo.read) or a plain Python one as a DSGO value: numbers become doubles, lists and dicts
    Nodes (a dict's keys its names), raw blocks Blobs (kind 2, as every MAB, MTAB and ragdoll block of EDF6's own
    objects is; a MAB still in 4.1's form is converted)."""
    if isinstance(v, bool):
        raise ValueError('no booleans in an SGO')
    if isinstance(v, bytes):
        if v[:4] == b'MAB\0' and mab_legacy.mab_layout(v).flag == mab_legacy.LEGACY:
            v = mab_legacy.mab_from_edf5(v)
        return dsgo.Blob(v, 2)
    if isinstance(v, list):
        return dsgo.Node([dsgo_value(x) for x in v])
    if isinstance(v, dict):
        node = dsgo.Node([])
        for k, x in v.items():
            node.set(k, dsgo_value(x))
        return node
    if isinstance(v, int):
        return float(v)
    if isinstance(v, sgo.Float):
        return float(v.value)
    if isinstance(v, (float, str)):
        return v
    raise ValueError(f'{type(v).__name__} in an SGO')


def node_path(node: dsgo.Node, path: str) -> dsgo.Value:
    """The value at `path` ('A/B/0': a name, or an index into a list) of a DSGO container."""
    for seg in path.split('/'):
        if not isinstance(node, dsgo.Node):
            raise KeyError(path)
        node = node.items[int(seg)] if seg.isdigit() else node.get(seg)
    return node


def set_path(node: dsgo.Node, path: str, value: dsgo.Value) -> None:
    """`value` at `path` (node_path's form); a name that is not there yet is added to its container."""
    head, _, last = path.rpartition('/')
    parent = node_path(node, head) if head else node
    if not isinstance(parent, dsgo.Node):
        raise KeyError(path)
    if last.isdigit():
        parent.items[int(last)] = value
    else:
        parent.set(last, value)


# SGO keys whose strings the class resolves to CAS states once, at construction, and later plays by that index alone:
# a name the CAS lacks resolves to -1 and the play faults (BerserkerSettings.EncountAnimations: 0x33D4AD stores it at
# +0x18D4, 0x33F861 plays it on first sight with no name to fall back on; the 2026-10-13 M153 crash).
STATE_KEYS = ('BerserkerSettings/EncountAnimations',)


@dataclass(frozen=True)
class ClassNames:
    """Names a class looks up in the object's files and uses unchecked: a miss is a crash, not a fallback."""
    states: tuple[str, ...] = ()       # CAS states (any player)
    locators: tuple[str, ...] = ()     # MAB locators (animation_model[2])
    bodies: tuple[str, ...] = ()       # ragdoll bodies (RagDollProxys.<name> in the ragdoll's SHKT)
    # (MAB event, states): states needed only by an object whose MAB raises that event (they are played after it)
    when_event: tuple[tuple[str, tuple[str, ...]], ...] = ()
    bodies_key: str = ''               # an SGO key, a list of names the plugin hands the class in place of `bodies`: with
                                       # it, those are the ragdoll bodies needed (src/edf41smalldragon.cpp)
    variables: tuple[str, ...] = ()    # CAS variables
    bone_keys: tuple[str, ...] = ()    # SGO keys naming a bone of the model (animation_model[0]'s first MDB)


# What check_class_names holds every object of the class to. DragonSmall (EDF.dll 0x32c000..0x33c000, the names its code
# passes by address; a sweep of every UTF-16 string the range loads, 2026-10-14):
#  - ragdoll bodies koshi and mune: on a hit that knocks it about, 0x3395E6 / 0x339601 hand both to 0x4A8570, which looks
#    each up in the ragdoll's body table (0x6E4160, a hash map, this+0xB10) and dereferences the miss: the end sentinel's
#    null value, EDF+0x6E42EB `mov rbx,[rax+8]` (DLC M805 2026-10-14: 4.1's SmallDragon401 ragdoll has koshi but spine1 /
#    spine2 where EDF5's frog dragon, which EDF6's class was written for, has mune). An object with edf41_ragdoll_bodies:
#    those names instead, which src/edf41smalldragon.cpp hands 0x4A8570 (the class's other ragdoll call, 0x339DD6 ->
#    0x4A8620, passes no names);
#  - the CAS states it plays by name (every one EDF6's own six dragons' CAS has); capture_* only after a bite closed, which
#    needs the MAB event 捕食判定開始 (DragonBig401 has none: it never bites);
#  - the MAB locators 捕食判定 (read in the constructor, EDF+0x32E84A) and 捕食固定位置 (where the prey is held), and 発射口
#    (the muzzle, 0x32E78B: its handle at +0x1798 is what the fireball and src/edf41dragon.cpp's flame read);
#  - the CAS variables v_glide_angle / v_glide_tilt (0x333A92 / 0x333AAA, through 0x1167170);
#  - wakeup_node, a bone of the model (0x32E679: 0x11001F0 on the skeleton at +0x6D0; the result, at +0x1598, is read
#    unchecked by the wake-up at 0x3396B7);
#  - not held to: RandomReactionTable's states, resolved in the CAS player random_reaction (0x32E4A0 -> 0x6EF260), whose
#    miss plays nothing (4.1's own DragonBig names them all over an empty random_reaction player, as in 4.1).
# The range's other strings are SGO keys, MAB event names and their parameters' keys, or another class's (ダメージ判定 at
# 0x32DA55 is DeiroiFootParts_Toe's).
CLASS_NAMES: dict[str, ClassNames] = {
    # The plugin's own class (src/edf41robo.cpp): what it looks up by name (ROBO_STATES / ROBO_LOCATORS above).
    'UfoRobo': ClassNames(states=ROBO_STATES, locators=ROBO_LOCATORS),
    'DragonSmall': ClassNames(
        states=('capture', 'damage_1', 'damage_2', 'damage_3', 'dead', 'down', 'fly', 'fly_attack', 'fly_begin',
                'fly_damage_1', 'fly_damage_2', 'fly_damage_3', 'fly_damage_large', 'glide', 'glide_end', 'glide_fly',
                'glide_start', 'ground_attack', 'landing', 'wakeup'),
        locators=('捕食判定', '捕食固定位置', '発射口'),
        bodies=('koshi', 'mune'),
        bodies_key='edf41_ragdoll_bodies',
        variables=('v_glide_angle', 'v_glide_tilt'),
        bone_keys=('wakeup_node',),
        when_event=(('捕食判定開始', ('capture_bite', 'capture_impact', 'capture_swing', 'capture_throw',
                                     'capture_throw_fly', 'capture_throw_over')),)),
}


def ragdoll_body(shkt: bytes, name: str) -> bool:
    """The ragdoll packfile `shkt` has a body RagDollProxys.<name> (the name whole: a 2014 packfile follows it with a
    tag byte, EDF6's own with a NUL)."""
    return re.search(rb'RagDollProxys\.' + re.escape(name.encode('ascii')) + rb'(?![A-Za-z0-9_])', shkt) is not None


def class_bodies(spec: ClassNames, members: dict, rel: str) -> tuple[str, ...]:
    """The ragdoll bodies the class looks up for this object: spec.bodies, or the names of its spec.bodies_key, which the
    plugin hands the class in their place (ValueError when the key is not a list of names the plugin takes)."""
    if not spec.bodies_key or spec.bodies_key not in members:
        return spec.bodies
    names = members[spec.bodies_key]
    if not (isinstance(names, list) and 1 <= len(names) <= SMALL_DRAGON_MOST_BODIES
            and all(isinstance(n, str) and 1 <= len(n) <= SMALL_DRAGON_LONGEST_BODY for n in names)):
        raise ValueError(f'{rel}: {spec.bodies_key} is not 1..{SMALL_DRAGON_MOST_BODIES} names of at most '
                         f'{SMALL_DRAGON_LONGEST_BODY} characters: {names!r}')
    return tuple(names)


def model_bones(file: Callable[[object], bytes | None], am: object) -> set[str]:
    """The bone names of the first MDB animation_model[0] names (its archive through `file`); empty when it is not there."""
    entries = model_entries(am[0]) if isinstance(am, list) and am else []
    rab = file(entries[0][0]) if entries else None
    for f in mdb.rab_read(rab).files if rab else []:
        if f.name.lower() == str(entries[0][1]).lower():
            md = mdb.mdb_read(f.data)
            return {md.name_of(b.name) for b in md.bones}
    return set()


def check_class_names(games: 'Games', files: dict[str, bytes], rel: str) -> None:
    """ValueError when the object at `rel` (in `files`; the files it names from there, else EDF6's) lacks a name
    CLASS_NAMES says its class looks up: a CAS state or variable, a MAB locator, a ragdoll body (those of the class, or
    of the plugin's key), the model bone a key names."""
    members = sgo.load(data=files[rel])
    spec = CLASS_NAMES.get(str(members.get('xgs_scene_object_class')))
    if spec is None:
        return

    def file(ref: object) -> bytes | None:
        r = rel_of(ref) if isinstance(ref, str) and ref.lower().startswith('app:/') else None
        return None if r is None else files[r] if r in files else games.read6(r) if r in games.have6 else None

    am = members.get('animation_model')
    cas = file(am[1]) if isinstance(am, list) and len(am) > 1 else None
    mab = am[2] if isinstance(am, list) and len(am) > 2 else None
    mab = bytes.fromhex(mab[4:]) if isinstance(mab, str) and mab.startswith('raw:') else mab
    mab = mab if isinstance(mab, bytes) and mab[:4] == b'MAB\0' else None
    states = set().union(*cas_graft.states(cas).values()) if cas else set()
    events = {k for track in (mab_legacy.mab_events(mab) if mab else {}).values() for k, _t in track}
    want = list(spec.states) + [st for ev, sts in spec.when_event if ev in events for st in sts]
    missing = [f'state {n}' for n in want if n not in states]
    locators = {loc.name for _l, loc in mab_legacy.mab_locators(mab)} if mab else set()
    missing += [f'locator {n}' for n in spec.locators if n not in locators]
    rd = members.get('ragdoll')
    shkt = file(rd[0]) if isinstance(rd, list) and rd else None
    missing += [f'ragdoll body {n}' for n in class_bodies(spec, members, rel)
                if not shkt or not ragdoll_body(shkt, n)]
    variables = set(cas_graft.variable_names(cas)) if cas else set()
    missing += [f'CAS variable {n}' for n in spec.variables if n not in variables]
    bones = model_bones(file, am)
    missing += [f'bone {members.get(k)!r} ({k})' for k in spec.bone_keys if members.get(k) not in bones]
    if missing:
        raise ValueError(f'{rel}: class {members["xgs_scene_object_class"]} looks up {missing}, which its files lack')


def check_states(out: dsgo.Node, cas: bytes | None, rel: str) -> None:
    """ValueError when a STATE_KEYS entry of `out` names a state no player of `cas` has."""
    have = set().union(*cas_graft.states(cas).values()) if cas else set()
    for key in STATE_KEYS:
        try:
            names = node_path(out, key)
        except KeyError:
            continue
        values = names.items if isinstance(names, dsgo.Node) else []
        missing = [n for n in values if isinstance(n, str) and n not in have]
        if missing:
            raise ValueError(f'{rel}: {key} names states its CAS has not: {missing}')


# Classes whose body is a voxel-destruction object (VoxelBody, this+0x5C0 of an AntHill): animation_model[3] builds it
# (EDF+0x6BDD1B allocates it only when that entry is a list; 0x6B8843 starts the member null) and the damage path
# dereferences it unchecked (EDF+0x4EB8D7 -> 0x6CAF22; the 2026-10-13 M115 crash on a 4.1 nest without the entry).
FRACTURE_CLASSES = ('AntHill',)
# The sub-list of the block: six (min, max) pairs of tuning and the debris models' names. The class reads all seven
# entries once the block has a sub-list (EDF+0x6BE2B7..0x6BEBBE, no per-entry length test).
FRACTURE_TUNING = 6


def fracture_block(rab_bytes: bytes, mdb_name: str, template: list) -> list:
    """animation_model[3] for an object whose model is `mdb_name` in `rab_bytes`: `template`'s grid and tuning around
    the model's own bounding box, no debris. `template` is another AntHill's block (E517_ANTHILL's)."""
    grid, _lo, _hi, tuning = template[:4]
    if len(grid) != 3 or len(tuning) != FRACTURE_TUNING + 1:
        raise ValueError(f'the template block is not [grid, min, max, [6 pairs, debris]]: {template!r}')
    by_name = {f.name.lower(): f for f in mdb.rab_read(rab_bytes).files}
    body = mdb.mdb_read(by_name[mdb_name.lower()].data)
    box = [mdb.rigid_extent(body, ax) for ax in range(3)]
    return [list(grid), [float(lo) for lo, _ in box], [float(hi) for _, hi in box],
            [list(p) for p in tuning[:FRACTURE_TUNING]] + [[]]]


def with_fracture(files: dict[str, bytes], model: list, template_model: list, root: str) -> list:
    """`model` (animation_model of 4.1's object: [models, cas, mab]) with the voxel block as its fourth entry."""
    if len(model) != 3:
        raise ValueError(f'{root}: animation_model has {len(model)} entries, not 4.1\'s 3: the block is added, not replaced')
    if len(template_model) < 4 or not isinstance(template_model[3], list):
        raise ValueError(f'{root}: the template has no voxel block (animation_model[3])')
    entry = model[0]
    block = fracture_block(files[rel_of(entry[0])], entry[1], template_model[3])
    return [*model, block]


def check_fracture(members: dict, files: dict[str, bytes], rel: str) -> None:
    """ValueError when `members` (an SGO's top level) is of a FRACTURE_CLASSES class and its animation_model[3] is
    not a block the class can build its body from: a list of [grid (3 positive ints), min (3), max (3, above min)]
    with, when it has a sub-list, six pairs and a debris list whose every name is a model of the object's archive."""
    if members.get('xgs_scene_object_class') not in FRACTURE_CLASSES:
        return
    am = members.get('animation_model')
    block = am[3] if isinstance(am, list) and len(am) > 3 else None
    if not isinstance(block, list) or len(block) < 3:
        raise ValueError(f'{rel}: no voxel block in animation_model[3]: the first hit dereferences a null body')
    grid, lo, hi = block[:3]
    ok = (isinstance(grid, list) and len(grid) == 3 and all(isinstance(g, (int, float)) and g >= 1 for g in grid)
          and isinstance(lo, list) and isinstance(hi, list) and len(lo) == len(hi) == 3
          and all(isinstance(v, (int, float)) for v in lo + hi) and all(a < b for a, b in zip(lo, hi)))
    if not ok:
        raise ValueError(f'{rel}: the voxel block\'s grid / box is malformed: {block[:3]!r}')
    if len(block) == 3:
        return
    tuning = block[3]
    pairs = tuning[:FRACTURE_TUNING] if isinstance(tuning, list) else []
    if (len(tuning) != FRACTURE_TUNING + 1 or not isinstance(tuning[-1], list)
            or any(not isinstance(p, list) or len(p) != 2 for p in pairs)):
        raise ValueError(f'{rel}: the voxel block\'s tuning is not six pairs and a debris list: {tuning!r}')
    names = {f.name.lower() for f in mdb.rab_read(files[rel_of(am[0][0])]).files}
    gone = [n for n in tuning[-1] if str(n).lower() not in names]
    if gone:
        raise ValueError(f'{rel}: the voxel block names debris models its archive has not: {gone}')


def mission_scripts(games: 'Games', edf6: bool) -> list[bytes]:
    """The mission scripts of EDF4.1 (every MISSION/<id>/*.BVM), or of EDF6's own missions (MISSION/EDF6 and EDF6_DLC:
    *.AC text, *.JSON), as bytes."""
    if edf6:
        want = {'EDF6', 'EDF6_DLC'}
        return [games.read6(k) for k in sorted(games.have6)
                if k.split('/')[0] == 'MISSION' and k.count('/') >= 2 and k.split('/')[1] in want
                and k.endswith(('.AC', '.JSON'))]
    return ([games.read4(k) for k in sorted(games.in4) if k.startswith('MISSION/') and k.endswith('.BVM')]
            + [s for _mount, _mid, s in dlc_mission_scripts(games)])


def dlc_mission_scripts(games: 'Games') -> list[tuple[str, str, bytes]]:
    """(mount, mission id, script) of every BVM script of the missions each installed DLC pack lists in its
    MISSION/MISSIONLIST.SGO (the pack's folders of missions it does not list are not played)."""
    out = []
    for mount, d in sorted(games.dlc.items()):
        listed = sgo.load(data=d.read('MISSION/MISSIONLIST.SGO'))
        ids = sorted({edf41dlc.MOUNT.match(s).group(2).split('/')[-1].upper() for s in strings(listed)
                      if edf41dlc.is_mounted(s) and edf41dlc.MOUNT.match(s).group(2).upper().startswith('MISSION/')})
        for mid in ids:
            for inner in sorted(k for k in d.files if k.startswith(f'MISSION/{mid}/') and k.endswith('.BVM')):
                out.append((mount, mid, d.read(inner)))
    return out


def script_dlc_sgos(script: bytes) -> set[str]:
    """The DLC objects (mp01:/...sgo, mp02:/...sgo) a BVM script names, as Mods paths (edf41dlc.mods_rel)."""
    texts = [m.group().decode('utf-16le') for m in re.finditer(rb'(?:[\x20-\x7e]\x00){4,}', script)]
    return {edf41dlc.mods_rel(p.strip()) for t in texts for p in t.split(';')
            if edf41dlc.is_mounted(p.strip()) and p.strip().lower().endswith('.sgo')}


def dlc_mission_roots(games: 'Games') -> dict[str, int]:
    """{root (EDF41MP01/GIANTANT04.SGO, the Mods path under OBJECT/): how many listed DLC missions name it}."""
    count: dict[str, set[tuple[str, str]]] = {}
    for mount, mid, script in dlc_mission_scripts(games):
        for rel in script_dlc_sgos(script):
            count.setdefault(rel.split('/', 1)[1], set()).add((mount, mid))
    return {root: len(ms) for root, ms in sorted(count.items())}


def script_sgos(script: bytes) -> set[str]:
    """The app:/...sgo paths a mission script names (Mods style): BVM strings are UTF-16, AC / JSON text is UTF-8."""
    texts = [script.decode('utf-8', 'ignore')]
    texts += [m.group().decode('utf-16le') for m in re.finditer(rb'(?:[\x20-\x7e]\x00){4,}', script)]
    return {rel_of(p) for t in texts for p in re.findall(r"(?i)app:/[^\"\s;,'()]+?\.sgo", t)}


def sgo_closure(roots: set[str], read: Callable[[str], bytes | None]) -> set[str]:
    """`roots` and every SGO they name, recursively, through the SGO that `read` gives (None: no such file)."""
    seen: set[str] = set()
    todo = sorted(roots)
    while todo:
        rel = todo.pop()
        if rel in seen:
            continue
        seen.add(rel)
        data = read(rel)
        if data is None:
            continue
        try:
            members = sgo.load(data=data)
        except Exception:   # noqa: BLE001 - a file this reader does not parse names nothing we can follow
            continue
        todo += [rel_of(s) for s in strings(members) if s.lower().startswith('app:/') and s.lower().endswith('.sgo')]
    return seen


def spawned_objects(games: 'Games') -> set[str]:
    """Every SGO the EDF4.1 missions' scripts spawn, as the game loads it: a path EDF6's archives serve is followed
    through EDF6's file (its children are what loads), any other through 4.1's."""
    def read(rel: str) -> bytes | None:
        return games.read6(rel) if rel in games.have6 else games.read4(rel) if rel in games.in4 else None
    return sgo_closure({r for s in mission_scripts(games, False) for r in script_sgos(s)}, read)


def stale_stock(stock: dict[str, dict[str, dict]], spawned: set[str], overridden: set[str],
                proven: set[str]) -> list[str]:
    """The stock EDF6 SGOs (`stock`: stock_by_class) among `spawned` that this port neither replaces (`overridden`) nor
    can show to load, as 'rel: lacks [keys]' / 'rel: no sibling to test': the class reads its keys by name and several
    dereference a miss unchecked, so a file lacking a key every OTHER stock SGO of its class has is a leftover its
    class cannot load (Transporter401_Formation, no se_preset_list: EDF6's own missions never spawn it, its sibling
    has the key). `proven` (SGOs EDF6's own missions spawn, directly or through the files they name) are loaded by
    the game, so they pass: that is the only evidence for a class with a single member."""
    out: list[str] = []
    for cls, members in sorted(stock.items()):
        for rel, mine in sorted(members.items()):
            if rel not in spawned or rel in overridden or rel in proven:
                continue
            others = [set(m) for r, m in members.items() if r != rel]
            if not others:
                out.append(f'{rel}: class {cls} has no other stock SGO to test against and no EDF6 mission spawns it')
                continue
            lacks = sorted(set.intersection(*others) - set(mine))
            if lacks:
                out.append(f'{rel}: class {cls} lacks {lacks}, which every other stock SGO of the class has')
    return out


def proven_stock(games: 'Games') -> set[str]:
    """The SGOs EDF6's own missions spawn (EDF6 and EDF6_DLC scripts), with the SGOs those name: loaded by the game."""
    return sgo_closure({r for s in mission_scripts(games, True) for r in script_sgos(s)},
                       lambda rel: games.read6(rel) if rel in games.have6 else None)


def as_dsgo(data: bytes) -> bytes:
    """The classic SGO `data` as a DSGO with the same values (numbers become doubles, raw blocks Blobs: dsgo_value)."""
    if data[:4] == b'DSGO':
        return data
    _version, members = sgo.read(data)
    root = dsgo.Node([])
    for k, v in members.items():
        root.set(k, dsgo_value(v))
    return dsgo.compact(dsgo.Document(root, []))


def check_class_format(dsgo_classes: set[str], rel: str, data: bytes) -> None:
    """ValueError when the SGO `data` is a classic SGO of a class whose every stock SGO is a DSGO: the class reads its
    object through the dictionary API (EDF+0xD7480 over the table at object+0xA8), which a classic SGO leaves empty;
    a constructor that does not guard it (Vehicle501_FortressRobo slot 6, 0x609E98: ai_attack_interval, then
    ai_attack_setting) faults on the null default table (MP004 2026-10-13, EDF+0xD74D9)."""
    cls = str(sgo.load(data=data).get('xgs_scene_object_class'))
    if data[:4] != b'DSGO' and cls in dsgo_classes:
        raise ValueError(f'{rel}: a classic SGO of class {cls}, whose every stock SGO is a DSGO')


def class_keys(stock: dict[str, dict[str, dict]], cls: str) -> set[str]:
    """The top-level keys every stock EDF6 SGO of class `cls` has. The classes read their keys by name and several
    dereference a miss unchecked (named lookup -1, then a load through it: Hector's weak_point, EDF+0x551E0; the
    AlienTrailer's se_list, EDF+0x30A4D7; the AntHill's voxel block, 0x4EB8D7): a key all of them have is a key the
    class may count on."""
    sets = [set(m) for m in stock.get(cls, {}).values()]
    return set.intersection(*sets) if sets else set()


def missing_class_keys(stock: dict[str, dict[str, dict]], members: dict) -> list[str]:
    return sorted(class_keys(stock, str(members.get('xgs_scene_object_class'))) - set(members))


def fill_class_keys(stock: dict[str, dict[str, dict]], read: Callable[[str], bytes], rel: str,
                    files: dict[str, bytes]) -> dict | None:
    """The keys of `files[rel]` (a 4.1 SGO under an EDF6 class, converted) that every stock SGO of its class has and
    it lacks, taken from the stock SGO of that class whose game_object_durability is nearest its own (ties: by
    name): {'template', 'keys'} for what was filled, None when nothing was missing. Only those keys: what 4.1's
    object has stays, and what the class does with the rest is what check_class_keys then finds already settled.
    `read` gives a stock SGO's bytes. A DSGO result (an alias's) is not filled: the alias carries its own keys, and
    check_class_keys still tests it. When the template is a DSGO (the classes EDF6 wrote in its own form: a key may
    be a named dictionary, which 4.1's SGO has no form for) the object becomes a DSGO with the same values (numbers
    as doubles, dsgo_value) and the template's keys added."""
    data = files[rel]
    if data[:4] == b'DSGO':
        return None
    version, members = sgo.read(data)
    need = missing_class_keys(stock, members)
    if not need:
        return None
    dur = sgo.plain(members.get('game_object_durability'))   # a Float (4 stored bytes) to a number
    near = lambda item: (abs(item[1].get('game_object_durability', 0.0) - dur) if isinstance(dur, (int, float)) else 0.0, item[0])
    name, theirs = min(stock[members['xgs_scene_object_class']].items(), key=near)
    t_data = read(name)
    if t_data[:4] == b'DSGO':
        root = dsgo.Node([])
        for k, v in members.items():
            root.set(k, dsgo_value(v))
        donor = dsgo.parse(t_data).root
        for k in need:
            root.set(k, copy.deepcopy(donor.get(k)))
        files[rel] = dsgo.compact(dsgo.Document(root, []))
        return {'template': name, 'keys': need, 'as': 'DSGO'}
    t_version, full = sgo.read(t_data)
    if t_version != version:
        raise ValueError(f'{rel}: {name} is SGO {t_version:#x}, not {version:#x}')
    for k in need:
        members[k] = full[k]
    files[rel] = sgo.write_depth_first(version, members)
    return {'template': name, 'keys': need}


def check_class_keys(stock: dict[str, dict[str, dict]], rel: str, data: bytes) -> None:
    """ValueError when the SGO `data` lacks a key every stock SGO of its class has."""
    need = missing_class_keys(stock, sgo.load(data=data))
    if need:
        raise ValueError(f'{rel}: lacks {need}, which every EDF6 SGO of its class has')


# Objects whose animation_model has no MAB block (item 2 is 0) while every stock SGO of their class has one, with why that
# works: the class looks MAB locators up by name through a pointer a missing MAB leaves null (EDF+0x6BAE06), and the
# object names none.
MAB_LESS: dict[str, str] = {
    'TRANSPORTER401_FORMATION.SGO': 'boosts is empty: no booster locator is looked up (0x5E4830 -> 0x6BADD0)',
}


def has_mab(members: dict) -> bool:
    """True when animation_model[2] is a MAB block (a 'raw:' hex string in an SGO read, or bytes)."""
    am = members.get('animation_model')
    blk = am[2] if isinstance(am, list) and len(am) > 2 else None
    return (isinstance(blk, str) and blk.startswith('raw:4d414200')) or (isinstance(blk, bytes) and blk[:4] == b'MAB\0')


def check_model_mab(stock: dict[str, dict[str, dict]], rel: str, data: bytes) -> None:
    """ValueError when the SGO `data` has no MAB block in animation_model[2] and every other stock SGO of its class has
    one, unless MAB_LESS says why it works (Transporter401_Formation: its boosts named locators in a MAB it did not
    have, the null read at EDF+0x6BAE06, MP003 2026-10-13). DSGO results and classes without a stock sibling are
    not compared."""
    if data[:4] == b'DSGO':
        return
    members = sgo.load(data=data)
    siblings = [m for r, m in stock.get(str(members.get('xgs_scene_object_class')), {}).items() if r != rel]
    if siblings and all(has_mab(m) for m in siblings) and not has_mab(members) and rel.split('/')[-1] not in MAB_LESS:
        raise ValueError(f'{rel}: animation_model has no MAB block, which every other stock SGO of its class has '
                         '(the class looks locators up in it unchecked)')


def remove_key(node: dsgo.Node, key: str) -> None:
    """The member `key` of a DSGO dictionary taken out (the others keep their names)."""
    at = next((i for i, n in node.names.items() if n == key), None)
    if at is None:
        return
    del node.items[at]
    node.names = {(i - 1 if i > at else i): n for i, n in node.names.items() if i != at}


def merged_model(games: 'Games', a: Alias, files: dict[str, bytes], rab_rel: str, mdb_name: str,
                 template_rab: str, fragments: list[str]) -> tuple[bytes, mdb.Mdb, list[float], tuple[list[float], list[float]]]:
    """(the object's own model archive, its body model, each gun's barrel length, the body's (min, max) corner).

    The archive is 4.1's (files[rab_rel], converted) with its model `mdb_name` holding every gun's mesh
    (mdb.graft_rigid: a rigid object on a bone under the arm bone) and no other model, plus the debris models the
    template's damage map names (`fragments`) with the textures they use, taken from the template's own archive: the
    class loads those from the object's archive by name, and Hector's has none. An archive of its own because the
    next Hector variant carries other guns."""
    rab = mdb.rab_read(files[rab_rel])
    by_name = {f.name.lower(): f for f in rab.files}
    body_file = by_name[mdb_name.lower()]
    body = mdb.mdb_read(body_file.data)
    corner = ([mdb.rigid_extent(body, ax)[0] for ax in range(3)], [mdb.rigid_extent(body, ax)[1] for ax in range(3)])
    barrel = []
    for gun in a.guns:
        weapon = mdb.mdb_read(by_name[gun.mdb.lower()].data)
        barrel.append(mdb.rigid_extent(weapon, 2)[1])
        skinned = any(e.name.lower() == 'blendindices' for ob in weapon.objects for me in ob.meshes for e in me.elems)
        if skinned:    # 4.1's Hector401 weapons: skin bones (gun_arms / gun_top ...) under the arm bone
            mdb.graft_skinned(body, weapon, gun.node, 'gun_' + gun.node)
        else:
            mdb.graft_rigid(body, weapon, gun.node, 'gun_' + gun.node)
    raw = mdb.mdb_write(body)
    body_file.stored = mdb.cmpl_compress(raw) if body_file.stored[:4] == b'CMPL' else raw
    rab.files = [f for f in rab.files if not f.name.lower().endswith('.mdb') or f is body_file]
    donor = mdb.rab_read(games.read6(template_rab))
    have = {f.name.lower() for f in rab.files}
    members = {f.name.lower(): f for f in donor.files}
    for name in fragments:
        frag = members[name.lower()]
        wanted = [frag]
        for t in mdb.mdb_read(frag.data).textures:
            stem, ext = os.path.splitext(t.filename.lower())
            # the texture's own member, its low-resolution one (stem.lod.ext) and its HD one: whichever the archive holds
            wanted += [f for n, f in members.items() if n in (stem + ext, stem + '.lod' + ext)]
        for f in wanted:
            if f.name.lower() not in have:
                have.add(f.name.lower())
                mdb.insert_member(rab, f)
    return mdb.rab_write(rab), body, barrel, corner


def missing_textures(rab_bytes: bytes, md: mdb.Mdb) -> list[str]:
    """The textures of `md` that its archive holds under no name the loader would bind (a texture member must be
    there by its filename or as stem.lod.ext, in the TEXTURE folder, before the model: mdb.insert_member's docstring;
    the HD members are optional)."""
    rab = mdb.rab_read(rab_bytes)
    names = {f.name.lower() for f in rab.files if f.folder == 0}
    out = []
    for t in md.textures:
        stem, ext = os.path.splitext(t.filename.lower())
        if stem + ext not in names and stem + '.lod' + ext not in names:
            out.append(t.filename)
    return out


def alias_dsgo(games: 'Games', root: str, files: dict[str, bytes], a: Alias, t_data: bytes) -> None:
    """alias() for a template in EDF6's DSGO form: `files` gets the object's DSGO (the template's keys, 4.1's value
    for each key both have, then a's drops and overrides), its CAS grafted, its MAB with the locators the class looks up,
    and a model archive of its own with the guns' meshes. See Alias for the fields; the 4.1 object's own keys the
    template has no use for (hector_*, resoruce, material_animation) do not come over: `material_animation` becomes
    the template's animation_model[4], the per-material table of the same blocks."""
    rel = 'OBJECT/' + root
    unsupported = [n for n, v in (('own_cas', a.own_cas), ('bones', a.bones), ('mab', a.mab), ('pad', a.pad)) if v]
    if unsupported:
        raise ValueError(f'{unsupported}: alias fields of the SGO path; the DSGO path does not apply them')
    _version, mine = sgo.read(files[rel])
    doc = dsgo.parse(t_data)
    out = doc.root
    t_am = out.get('animation_model')
    assert isinstance(t_am, dsgo.Node)
    keys = set(out.names.values())
    guns_of_template = copy.deepcopy(out.get('gun_control'))
    for k, v in mine.items():
        if k in keys:
            out.set(k, dsgo_value(v))
    for k in a.drop:
        remove_key(out, k)
    out.set('xgs_scene_object_class', a.cls)
    for path, value in a.override:
        set_path(out, path, dsgo_value(value))

    am = mine['animation_model']
    mrab, mdb_name = am[0][0][0], am[0][0][1]
    template_rab = rel_of(t_am.items[0].items[0])
    fragments = list(node_path(t_am, '3/3/6').items)
    rab_rel = 'OBJECT/' + root.rsplit('.', 1)[0] + '.MRAB'
    rab_bytes, body, barrel, (lo, hi) = merged_model(games, a, files, rel_of(mrab), mdb_name, template_rab, fragments)
    files[rab_rel] = rab_bytes

    locators = []
    if a.eye:
        locators.append(mab_legacy.Locator('目の基準位置', a.eye[0], 0, (*a.eye[1], 1.0), 1.0))
    for gun, z in zip(a.guns, barrel):
        locators.append(mab_legacy.Locator(gun.muzzle, gun.node, 2, (0.0, 0.0, z, 1.0), (0.1, 0.1, 5.0, 1.0),
                                           (0.0, 0.0, 0.0, 1.0)))
    mab = mab_legacy.mab_add_locators(mab_block(am[2]), locators)

    model = copy.deepcopy(t_am)
    model.items[0].items[0] = 'app:/Object/' + rab_rel.rsplit('/', 1)[-1].lower()
    model.items[0].items[1] = mdb_name
    model.items[1] = am[1]
    model.items[2] = dsgo.Blob(mab, 2)
    grid = model.items[3]
    grid.items[0] = dsgo.Node([24.0, 48.0, 12.0])
    grid.items[1] = dsgo.Node([lo[0] - 0.25, lo[1] - 1.0, lo[2] - 0.25])
    grid.items[2] = dsgo.Node([hi[0] + 0.25, hi[1] + 0.25, hi[2] + 0.25])
    blob = mine['material_animation'][0]
    # Berserker's base class enables material animation slots by number, unchecked (EDF+0x6dc830): 0 and 2 at spawn and on
    # a hit (0x348d1d, 0x33ef81 -> 0x6c0200), 1 and 3 elsewhere (0x34b2de, 0x37acde). A list shorter than the template's
    # (4) hands it the memory past its end: a null player (M113) or whatever text follows (M131, "NT02"). 4.1 has one
    # animation, its glow: it serves every slot, with the template's mode for each.
    model.items[4] = dsgo.Node([dsgo.Node([dsgo.Blob(blob, 2), c.items[1]]) for c in t_am.get('material_animation').items])
    out.set('animation_model', model)

    if a.guns:
        entries = []
        for gun in a.guns:
            entry = copy.deepcopy(guns_of_template.items[gun.entry])
            set_path(entry, 'gun_battery/muzzle/shape', gun.muzzle)
            entries.append(entry)
        out.set('gun_control', dsgo.Node(entries))
        if all(g.fire for g in a.guns):
            hector_fire(games, files, mine, out, a)

    cas = rel_of(am[1])
    template_cas = rel_of(t_am.items[1])
    graft_cas(games, a, files, cas, template_cas)
    check_states(out, files.get(cas), rel)
    files[rel] = dsgo.compact(dsgo.Document(out, doc.pool))
    check_materials(games, files, rel, {'material_animation': len(t_am.get('material_animation').items)})
    check_sounds(games, files[rel], rel)
    if a.prune:
        prune(files, rel)


def num(v: object) -> float:
    """A number of 4.1's SGO (int, float or sgo.Float) as a float."""
    return float(v.value) if isinstance(v, sgo.Float) else float(v)


def level_product(coef: float) -> dsgo.Blob:
    """The expression `coef * V11` (EDF6's damage: Berserker's bullets are `2.0 * V11`, 28 bytes: push constant,
    push variable 11, multiply, end)."""
    return dsgo.Blob(struct.pack('<IdIIII', 1, coef, 3, 11, 7, 0), 4)


def level_lerp(lo: float, hi: float, full_at: float) -> dsgo.Blob:
    """lerp(lo, hi, min(V11 / full_at, 1)): the function 0x80000006 EDF6's bullets use for lerp(a, b, t) (colour,
    blast radius: constant, constant, variable, call), its t made from V11 with the multiply (7) and min (8) the
    level clamp `V11 1 max 29 min` uses."""
    body = struct.pack('<IdIdIdIII', 1, lo, 1, hi, 1, 1.0 / full_at, 3, 11, 7)
    body += struct.pack('<IdIIII', 1, 1.0, 8, 4, 0x80000006, 0)
    return dsgo.Blob(body, 4)


def hector_bullet(games: 'Games', w: HectorWeapon, damage: float, custom: list[float], hit_se: str) -> dsgo.Node:
    """The bullet node of one Hector gun: the donor bullet (BULLET_DONORS) with 4.1's speed, lifetime, damage and
    blast. `custom` is the weapon SGO's bullet_custom_param. hit_se is the template's own: the donors' cue names are
    in sound banks the Hector does not load."""
    donor_file, path = BULLET_DONORS['solid' if w.bullet == 'none' else w.bullet]
    bullet = copy.deepcopy(node_path(dsgo.parse(games.read6('OBJECT/' + donor_file)).root, path))
    bullet.set('speed', w.speed or 1.0)
    bullet.set('alive_time', w.alive)
    bullet.set('hit_se', hit_se)
    dmg = level_product(damage)
    lo, hi = w.radius
    if w.bullet == 'rocket':
        bullet.set('_base_damage', dmg)
        bullet.get('damage').items[0] = dmg      # [1] and [2] are the donor's caps on it
        bullet.set('explosion_radius', lo)
        bullet.set('size', lo)
        params = bullet.get('custom_parameter')
        params.items[3] = 0.0                    # 4.1's fourth rocket parameter is 0 (the donor's 0.0005 accelerates)
    else:
        bullet.set('damage', dmg)
        if w.bullet == 'plasma':
            bullet.set('explosion_radius', level_lerp(lo, hi, (hi - lo) / 0.45))
            bullet.set('gravity_factor', 0.0)    # 4.1's plasma flies straight
        else:
            bullet.set('explosion_radius', 0.0)
            if w.bullet == 'solid':
                bullet.set('custom_parameter', dsgo.Node([float(x) for x in custom]))
    return bullet


def hector_fire(games: 'Games', files: dict[str, bytes], mine: dict, out: dsgo.Node, a: Alias) -> None:
    """gun_control's entries made to fire what 4.1's arms hold, and the AI's gun range their longest: each entry
    gets the bullet of its HectorWeapon, fire.count / interval / accuracy, and a one-frame charge (4.1 has none).
    The weapon SGOs (in `files`: the closure holds them until prune) are checked against the table and give
    hector_weapon_range and bullet_custom_param. Damage: 4.1's coefficient times (this object's HP / 4.1's HP)
    -- the same HP, durability is a key both have and 4.1's wins -- so the ratio of a hit to the HP is 4.1's;
    that V11 here is the s of 4.1 (level / 10) is an assumption."""
    arms = mine['hector_weapon']
    entries = out.get('gun_control').items
    hit_se = entries[0].get('gun_battery').get('bullet').get('hit_se')
    hp_ratio = float(out.get('game_object_durability')) / num(mine['game_object_durability'])
    reach = 0.0
    ranges: list[float] = []
    for gun, arm, entry in zip(a.guns, arms, entries):
        w = gun.fire
        if rel_of(arm[3]) != 'OBJECT/' + w.sgo:
            raise ValueError(f'{arm[0]} holds {arm[3]}, the table says {w.sgo}')
        weapon = sgo.load(data=files['OBJECT/' + w.sgo])
        if weapon['xgs_scene_object_class'] != w.cls:
            raise ValueError(f'{w.sgo}: class {weapon["xgs_scene_object_class"]}, not {w.cls}')
        custom = [num(x) for x in weapon.get('bullet_custom_param', [])]
        if w.bullet == 'none':
            coef = 0.0       # a shield: its entry is kept (the class needs a muzzle per entry) and hurts nothing
        else:
            coef = w.damage if w.damage is not None else custom[1]
        battery = entry.get('gun_battery')
        battery.set('bullet', hector_bullet(games, w, coef * hp_ratio, custom, hit_se))
        set_path(battery, 'charge/time', 1.0)
        fire = battery.get('fire')
        fire.set('count', w.count)
        fire.set('interval', w.interval)
        if w.spread:
            fire.set('accuracy', w.spread)
        ranges.append(num(weapon.get('hector_weapon_range', 0.0)))
        reach = max(reach, ranges[-1])
    set_path(out, 'AiSettings/gun_attack/range', reach)
    # What Berserker's keys cannot say, for the plugin (src/edf41ai.cpp reads this key through the game's SGO accessors;
    # the class never looks it up): each arm's own range (4.1 starts only the arms that reach the target), whether the
    # two arms hold the same weapon class (4.1 then always fires both), and 4.1's hector_ai_setting (back-off cycle).
    ai = [num(x) for x in mine['hector_ai_setting']]
    if len(ai) != 7:
        raise ValueError(f'hector_ai_setting has {len(ai)} numbers, not 7')
    out.set('edf41_hector', dsgo_value({'ranges': ranges, 'same_weapon': float(a.guns[0].fire.cls == a.guns[1].fire.cls),
                                        'ai': ai}))


def mab_block(block: object) -> bytes:
    """4.1's animation_model[2], a MAB block, in EDF6's form."""
    if not isinstance(block, bytes) or block[:4] != b'MAB\0':
        raise ValueError('no MAB block in animation_model[2]')
    return block


def prune(files: dict[str, bytes], root_rel: str) -> None:
    """`files` left with only the files `root_rel` names, directly or through the SGOs it names (a closure taken over
    4.1's SGO holds objects the port does not use: Hector's weapon objects, their ragdolls)."""
    keep: set[str] = set()
    todo = [root_rel]
    while todo:
        rel = todo.pop()
        if rel in keep or rel not in files:
            continue
        keep.add(rel)
        if rel.endswith('.SGO'):
            todo += [rel_of(s) for s in strings(sgo.load(data=files[rel])) if s.lower().startswith('app:/')]
    for rel in [r for r in files if r not in keep]:
        del files[rel]


def graft_cas(games: 'Games', a: Alias, files: dict[str, bytes], cas: str | None, template_cas: str) -> None:
    """files[cas] with the players and variables of a's template CAS (a.players, a.variables) and of a's other donors
    (a.cas_donors) that it lacks (cas_graft.graft)."""
    if not cas or cas not in files:
        return
    for old, new in a.rename_players:
        files[cas] = cas_graft.rename_group(files[cas], old, new)
    for player, old, new in a.rename_states:
        files[cas] = cas_graft.rename_state(files[cas], player, old, new)
    if a.players or a.variables:
        files[cas] = cas_graft.graft(files[cas], games.read6(template_cas), list(a.players), a.variables)
    for donor, players, variables in a.cas_donors:
        files[cas] = cas_graft.graft(files[cas], games.read6(donor), list(players), variables)
    if a.state_aliases:
        aliases: dict[str, list[tuple[str, str]]] = {}
        for player, new, old in a.state_aliases:
            aliases.setdefault(player, []).append((new, old))
        files[cas] = cas_graft.alias_states(files[cas], aliases)


def has_bone(md: mdb.Mdb, name: str) -> bool:
    return any(md.name_of(b.name) == name for b in md.bones)


def reboned(rab: bytes, ops: tuple[tuple[str, str, str], ...]) -> bytes:
    """A model RAB with `ops` (Alias.bones) applied to every MDB in it. An op whose result the MDB already shows (a
    model another object's alias already reboned: UFO401 and the DLC's UFO401ACE share ufo401.mrab, and the closure
    hands the second the first's result) is not applied again; the archive is returned as it is when none applies."""
    archive = mdb.rab_read(rab)
    changed = False
    for f in archive.files:
        if not f.name.lower().endswith('.mdb'):
            continue
        md = mdb.mdb_read(f.data)
        done = 0
        for op, a, b in ops:
            if op == 'rename' and not has_bone(md, a) and has_bone(md, b):
                continue
            if op == 'insert' and has_bone(md, a):
                if md.name_of(md.bones[md.bones[mdb.bone_named(md, a)].parent].name) != b:
                    raise ValueError(f'{f.name}: bone {a} is there, but not under {b}')
                continue
            if op == 'rename':
                mdb.rename_bone(md, a, b)
            elif op == 'insert':
                mdb.insert_bone(md, a, b)
            else:
                raise ValueError(f'bone op {op}')
            done += 1
        if done:
            changed = True
            f.stored = mdb.cmpl_compress(mdb.mdb_write(md)) if f.stored[:4] == b'CMPL' else mdb.mdb_write(md)
    return mdb.rab_write(archive) if changed else rab


def remabbed(mab: object, names: tuple[tuple[str, str], ...]) -> bytes:
    """An SGO's MAB block with each bone name (a NUL-terminated UTF-16 string) changed in place."""
    if not isinstance(mab, bytes):
        raise ValueError('no MAB block in animation_model[2]')
    for old, new in names:
        at = mab.find(old.encode('utf-16le') + b'\0\0')
        if at < 0 or at % 2:
            raise ValueError(f'MAB has no bone name {old}')
        mab = sgo.replace_utf16(mab, at, old, new)
    return mab


def dlc_roots(games: Games) -> dict[str, int]:
    """DLC_ROOTS, once the DLC missions' scripts are shown to name exactly those (and DLC_EXCLUDED's) objects that many
    times (dlc_mission_roots): ValueError when a pack is not installed or the table is not what the scan finds."""
    missing = sorted(set(edf41dlc.PACKS) - set(games.dlc))
    if missing:
        raise ValueError(f'EDF4.1 DLC packs {missing} are not installed (<EDF4.1>/DLC/<hash>/dlc.cpk): their objects '
                         'would drop out of the registry')
    found = dlc_mission_roots(games)
    if set(found) - set(DLC_EXCLUDED) != set(DLC_ROOTS) or any(found[r] != n for r, n in DLC_ROOTS.items()):
        raise ValueError(f'DLC_ROOTS is not what the DLC missions name: {found}')
    return dict(DLC_ROOTS)


def build(edf41: str, edf6: str, only: tuple[str, ...] = ()) -> tuple[dict, dict[str, bytes]]:
    """The whole conversion; `only` (SGO names like 'HECTOR301CC.SGO') limits it to those roots."""
    games = Games(edf41, edf6)
    objects, skipped, files = [], [], {}
    shared: dict[str, str] = {}
    for root, s in STAND_INS.items():
        if only and root not in only:
            continue
        rel = 'OBJECT/' + root
        data, need = stand_in(games, s)
        files[rel] = data
        for r in need:
            shared[r] = SHARED[r]
        objects.append({'object': rel, 'class': sgo.load(data=data).get('xgs_scene_object_class'),
                        'missions': ROOTS.get(root, 0), 'files': [rel] + need, 'stand_in': s.template})
    roots = dict(ROOTS)
    if not only or set(only) & (set(DLC_ROOTS) | set(DLC_EXCLUDED)):
        roots.update(dlc_roots(games))
        skipped += [{'object': 'OBJECT/' + r, 'reason': why} for r, why in DLC_EXCLUDED.items()
                    if not only or r in only]
    for root, uses in roots.items():
        rel = 'OBJECT/' + root
        if root in STAND_INS or (only and root not in only):
            continue
        if rel in games.have6 and root not in STOCK_OVERRIDES:
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
                if root in NATIVE_FIXES:
                    NATIVE_FIXES[root](games, out)
            except ValueError as e:
                why.append(f'alias {ALIASES[root].cls}: {e}')
        if not why and root in NATIVE_FIXES and root not in ALIASES:   # a native class without an alias (UfoRobo, the 401)
            try:
                NATIVE_FIXES[root](games, out)
            except ValueError as e:
                why.append(f'native class: {e}')
        if not why and root in PATCHES:
            version, members = sgo.read(out[rel])
            PATCHES[root](members)
            out[rel] = sgo.write_depth_first(version, members)
        if not why and root not in ALIASES:
            try:
                check_materials(games, out, rel, {})
                check_sounds(games, out[rel], rel)
            except ValueError as e:
                why.append(str(e))
        for part in [r for r in out if r not in files and r.split('/', 1)[1] in PART_ALIASES]:
            try:
                alias(games, part.split('/', 1)[1], out, PART_ALIASES[part.split('/', 1)[1]])
            except ValueError as e:
                why.append(f'alias {part}: {e}')
        for part in [r for r in out if r.split('/', 1)[1] in FORTRESS_PARTS] if not why else []:
            try:
                fortress_part(games, out, part.split('/', 1)[1], FORTRESS_PARTS[part.split('/', 1)[1]])
            except ValueError as e:
                why.append(f'fortress part {part}: {e}')
        filled = None
        if not why:
            try:   # the keys every stock SGO of the class has (class_keys): what an alias or patch left out is filled
                stock = games.stock_by_class()   # from the nearest one (only those keys), then all are required
                filled = fill_class_keys(stock, games.read6, rel, out)
                if out[rel][:4] != b'DSGO' and str(sgo.load(data=out[rel]).get('xgs_scene_object_class')) in games.dsgo_only_classes():
                    out[rel] = as_dsgo(out[rel])
                check_class_format(games.dsgo_only_classes(), rel, out[rel])
                check_class_keys(stock, rel, out[rel])
                check_model_mab(stock, rel, out[rel])
                check_fracture(sgo.load(data=out[rel]), out, rel)
                check_class_names(games, out, rel)
            except ValueError as e:
                why.append(str(e))
        if why:
            skipped.append({'object': rel, 'reason': '; '.join(why)})
            continue
        clash = [r for r in out if r in files and files[r] != out[r]]
        if clash:   # two objects needing different bytes for one file (a CAS grafted for one class only)
            skipped.append({'object': rel, 'reason': 'files another object needs differently: ' + ', '.join(clash)})
            continue
        files.update(out)
        cls = sgo.load(data=out[rel]).get('xgs_scene_object_class')
        objects.append({'object': rel, 'class': cls, 'missions': uses, 'files': list(out),
                        **({'class_keys_from': filled} if filled else {}),
                        **({'replaces_stock': STOCK_OVERRIDES[root]} if root in STOCK_OVERRIDES else {})})
    if not only:   # every stock SGO the 4.1 missions spawn is one EDF6 loads, or replaced above
        stale = stale_stock(games.stock_by_class(), spawned_objects(games), {o['object'] for o in objects},
                            proven_stock(games))
        stale = [s for s in stale if s.split(':')[0] not in STOCK_EXEMPT]
        if stale:
            raise ValueError('EDF6 archives serve SGOs the 4.1 missions spawn that its classes cannot load '
                             '(add the root to ROOTS / ALIASES with STOCK_OVERRIDES):\n  ' + '\n  '.join(stale))
    hashes = {r: hashlib.sha256(b).hexdigest() for r, b in files.items()}
    for r, path in shared.items():
        with open(os.path.join(HERE, '..', *path.split('/')), 'rb') as f:
            hashes[r] = hashlib.sha256(f.read()).hexdigest()
    data = {'source': 'EDF4.1 Root.cpk and its DLC packs; tools/make_edf41_objects.py', 'objects': objects, 'skipped': skipped,
            'files': dict(sorted(hashes.items())), 'shared': shared}
    return data, files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--edf41', default=os.path.join(STEAM, 'Earth Defense Force 4.1'))
    ap.add_argument('--edf6', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 6'))
    ap.add_argument('--out', help='write objects/ and objects.json under this folder instead of edf41port/')
    ap.add_argument('--only', action='append', default=[], metavar='SGO',
                    help='build only this root (HECTOR301CC.SGO; repeatable)')
    a = ap.parse_args()
    data, files = build(a.edf41, a.edf6, tuple(r.upper() for r in a.only))
    out_json, out_files = (os.path.join(a.out, 'objects.json'), os.path.join(a.out, 'objects')) if a.out else (OUT, FILES)
    shutil.rmtree(out_files, ignore_errors=True)
    for rel, b in rabpool.pack(files).items():   # archives as manifests over shared members (pylib/rabpool.py)
        path = os.path.join(out_files, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(b)
    os.makedirs(os.path.dirname(out_json), exist_ok=True)
    with open(out_json, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write('\n')
    size = sum(len(b) for b in files.values())
    print(f'{out_json}: {len(data["objects"])} objects, {len(files)} files ({size / 1e6:.1f} MB), {len(data["skipped"])} skipped')
    for s in data['skipped']:
        print('  skipped', s['object'], s['reason'][:200])
    return 0


if __name__ == '__main__':
    sys.exit(main())
