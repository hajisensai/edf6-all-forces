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
from dataclasses import dataclass
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import acb  # noqa: E402
import cas_graft  # noqa: E402
import dsgo  # noqa: E402
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
    refs = [tuple(e[:2]) for e in members['animation_model'][0]]
    for key in ('unit_model', 'small_barrier_model', 'large_barrier_model'):
        if key in members:
            refs += [tuple(e[:2]) for e in members[key][0]]
    return {k: members[k] for k in MATERIAL_KEYS if k in members}, refs


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
    names = [n for k in SE_KEYS for n in strings(members.get(k)) if n]
    if not names:
        return
    seset, banks = sound_cues(games)
    loaded = set(RESIDENT_BANKS) | {s.rsplit('/', 1)[-1].upper() for s in strings(members.get('game_sound'))}
    cues = set().union(*(banks.get(b, set()) for b in loaded))
    for n in names:
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
        return self._stock

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
        for entry in members['animation_model'][0]:
            rab_rel = rel_of(entry[0])
            if rab_rel in files:
                files[rab_rel] = reboned(files[rab_rel], a.bones)
    if a.mab:
        model = members['animation_model']
        model[2] = remabbed(model[2], a.mab)
    for locator, node in a.locator_nodes:
        members['animation_model'][2] = mab_legacy.mab_retarget(members['animation_model'][2], locator, node)
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
    files[rel] = sgo.write_depth_first(version, members)
    check_fracture(members, files, rel)
    graft_cas(games, a, files, cas, rel_of(t_am[1]) if t_am and isinstance(t_am[1], str) else '')
    check_materials(games, files, rel, {k: len(theirs[k]) for k in MATERIAL_KEYS if k in theirs}, a.inert_materials)
    check_sounds(games, files[rel], rel)


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
    return [games.read4(k) for k in sorted(games.in4) if k.startswith('MISSION/') and k.endswith('.BVM')]


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
        reach = max(reach, num(weapon.get('hector_weapon_range', 0.0)))
    set_path(out, 'AiSettings/gun_attack/range', reach)


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
    if a.players or a.variables:
        files[cas] = cas_graft.graft(files[cas], games.read6(template_cas), list(a.players), a.variables)
    for donor, players, variables in a.cas_donors:
        files[cas] = cas_graft.graft(files[cas], games.read6(donor), list(players), variables)
    if a.state_aliases:
        aliases: dict[str, list[tuple[str, str]]] = {}
        for player, new, old in a.state_aliases:
            aliases.setdefault(player, []).append((new, old))
        files[cas] = cas_graft.alias_states(files[cas], aliases)


def reboned(rab: bytes, ops: tuple[tuple[str, str, str], ...]) -> bytes:
    """A model RAB with `ops` (Alias.bones) applied to every MDB in it."""
    archive = mdb.rab_read(rab)
    for f in archive.files:
        if not f.name.lower().endswith('.mdb'):
            continue
        md = mdb.mdb_read(f.data)
        for op, a, b in ops:
            if op == 'rename':
                mdb.rename_bone(md, a, b)
            elif op == 'insert':
                mdb.insert_bone(md, a, b)
            else:
                raise ValueError(f'bone op {op}')
        f.stored = mdb.cmpl_compress(mdb.mdb_write(md)) if f.stored[:4] == b'CMPL' else mdb.mdb_write(md)
    return mdb.rab_write(archive)


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
    for root, uses in ROOTS.items():
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
            except ValueError as e:
                why.append(f'alias {ALIASES[root].cls}: {e}')
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
        filled = None
        if not why:
            try:   # the keys every stock SGO of the class has (class_keys): what an alias or patch left out is filled
                stock = games.stock_by_class()   # from the nearest one (only those keys), then all are required
                filled = fill_class_keys(stock, games.read6, rel, out)
                check_class_keys(stock, rel, out[rel])
                check_model_mab(stock, rel, out[rel])
                check_fracture(sgo.load(data=out[rel]), out, rel)
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
    data = {'source': 'EDF4.1 Root.cpk; tools/make_edf41_objects.py', 'objects': objects, 'skipped': skipped,
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
