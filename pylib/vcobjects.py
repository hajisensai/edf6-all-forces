"""The plugin's generated objects, made on this machine from the player's own Root.cpk: the jets (src/jet.cpp,
src/playerjet.cpp, src/subcarrier.cpp) as V506 heli SGOs with their own models, their guns and charges, the
portal laser (src/carrierlaser.cpp) and script-placeable call-in vehicles. Shared by tools/make_jets.py,
tools/make_sub.py, tools/call_weapons.py and the test range (testrange/gen.py); builders only: what gets
written where, and who owns it, is pylib/ledger.py.
"""
from __future__ import annotations

import copy
import math
import os
import re
import struct
from dataclasses import dataclass, replace
from functools import lru_cache

import dsgo
import sgo
import xm_units
import xm_walker
from rootcpk import DEFAULT_GAME, Game  # noqa: F401  (re-exported: the tools take both from here)

@dataclass(frozen=True)
class Jet:
    mark: float        # mission_setup[1][0], the speed gain k: how EDF6VehicleCrew (src/jet.cpp) tells a jet
    durability: float
    weapons: tuple[str, ...]
    # Its own model (pylib/jet_models.py writes the archive, `file`, into Mods/OBJECT), or None: the bomber
    # (JET_MODEL / JET_ELEVON_MODEL). `body`: the mesh bone; `anchor`: the bone the V506 locators, weapons
    # and dead effect hang on (it replaces their names in place, so it is at most 4 characters: `body`).
    # The model's root bone is always JET_ROOT_BONE (see JET_MAB_BONES).
    model: tuple[str, str] | None = None
    file: str | None = None
    body: str = 'bomber501'
    anchor: str = 'mdl'
    rigid: tuple[tuple[float, float, float], tuple[float, float, float]] | None = None
    # The bone each weapon hangs on (vehicle_weapon_setting), in `weapons` order; empty: jet_weapon_bones (its guns and
    # stores on its model's hardpoints, HARDPOINT_FILES; the rest, or all on a model without them, on `anchor`).
    weapon_bones: tuple[str, ...] = ()
    # A player jet (src/playerjet.cpp): the player flies it. Its SGO keeps `vehicle_setup` beside
    # `mission_setup` (the Air Raider's call weapon brings it like a stock heli, tools/call_weapons.py), and
    # `camera` replaces game_object_camera_setting's offset (the stock heli's (0, 5.5, -11.5) is inside a jet).
    player: bool = False
    camera: tuple[float, float, float] | None = None
    # A parked one (testrange/gen.py BOARDABLE_PARKED): an NPC kind (its mark, model and arms) placed empty on the ground
    # for the player to fly, so built like a player jet where the player meets it: the whole model's box (a fuselage box
    # is walked through, wings and all: 「飞机缺少实体」, 2026-10-05) and a seat every class may take. Its NPC-flown
    # twin keeps the fuselage box (jet_sgo).
    parked: bool = False
    # A requested one (REQUEST_KINDS, the Air Raider's EDF6VC_CALL_FLY_* vehicle requests, tools/call_weapons.py): a parked
    # twin the stock request's transport drops empty at the flare, so its SGO keeps `vehicle_setup` beside `mission_setup`
    # as a player jet's does (the request's vehicle setup is the one the vehicle gets; the SGO is the stock heli's shape).
    requested: bool = False
    # The stock bomber model (pylib/jet_models.py STOCK_BOMBERS) a jet with no model file of its own is measured on for
    # its box and door when parked (the gunship's bomber401); None: its `file`, or the elevon bomber.
    box_model: str | None = None
    # The stock heli SGO it is made from (its body, rigid body, crash and weapons): every jet is a V506.
    stock: str = 'V506_HELI'
    # A Primer creature's sounds (CREATURE_SOUNDS), and no damage smoke (vehicle_damage_effect 0: it is no machine).
    creature: str | None = None
    # mission_setup[0]: the vehicle's tier, the two multipliers the game's vehicle requests scale a vehicle by (its
    # durability and its weapons' damage): JET_TIER unless set.
    tier: tuple[float, float] | None = None
    # Its own animation set (animation_model[1]) in place of the V506's (None: the V506's). Not needed for bones the
    # plugin poses: every bone's record world is drawn each frame (docs/sazabi-re.md §2).
    cas: str | None = None
    # Its seat camera (the MAB's eye and LookTarget locators, riding) given in its model's frame, in place of the
    # stock heli's rig fitted by seat_camera: a jet's camera must see over the plane, a mech's looks past its shoulder at
    # its back (place_seat_camera / check_seat_camera).
    seat_camera: tuple[tuple[float, float, float], tuple[float, float, float]] | None = None
    # MAB locators are measured from the render/model origin: native phase 1
    # subtracts the physics centre before SetModelWorld. InputHook sees the
    # temporary centre; a DoorLog relative to object position cannot distinguish
    # these phases. False remains an explicit centre-frame override for callers.
    locators_on_origin: bool = True


# Jets (src/jet.cpp, docs/jet-model-re.md): the V506 heli body (rigid body, HP, weapons, crash) with the
# BOMBER501 model, flown by the plugin. The 506 fires 0x2020 -> weapons 0 and 1, 0x2021 -> weapon 2.
# The guns are the 506's gatlings with a jet's reach (jet_guns): stock they fly 4 m a frame for 40 frames,
# 160 m, inside every role's gun pass (src/jet.cpp kKinds gunOpen 350-500 m, Fire takes the nearer of the
# two): a jet diving at 160 m/s had 0.3 s between their reach and its pull-out, and 7 of a drone's 130 gun
# chances fired on 2026-10-03 (the rest held, the nose not yet on the lead); the strike, interceptor and
# multirole jets fired none. Faster and longer lived they reach JET_GUN_REACH (a jet cannon's: ~960 m/s, the
# reach the jets' fire logic and the submarine carrier read off the weapon itself); damage and rate stay stock.
JET_GUN_FILES = {'EDF6VC_JET_GUN_L.SGO': 'V_506HELI_GATLING01_L.SGO', 'EDF6VC_JET_GUN_R.SGO': 'V_506HELI_GATLING01_R.SGO'}
JET_GUN_SPEED, JET_GUN_ALIVE = 16.0, 60.0   # m a frame, frames: 960 m/s, 960 m
JET_GUN_REACH = JET_GUN_SPEED * JET_GUN_ALIVE
_GUNS = tuple('app:/weapon/' + f.lower() for f in JET_GUN_FILES)
# The jets' tier (Jet.tier, mission_setup[0]): the two multipliers (durability, weapons' damage) the stock game puts
# over a vehicle's SGO values (vehicle_setup[0]: the N9 Eros 1.3 ... the Eros Sigma 25). The jets keep the stock
# 506's 1: their durability and weapons' damage below are base values the game scales as it scales any friend a
# script spawns (to the mission's difficulty: src/jet_spawn.cpp LevelVehicle), and a player jet's request by its level
# (tools/call_weapons.py request_tier). (2026-10-04 x25 and fixed Inferno values both stacked on that: far too strong.)
JET_TIER = (1.0, 1.0)
# The plugin's missiles (jet_guns): each the 506's homing missile (MissileBullet01) remade as a real class of missile,
# guided by the plugin (src/missile.cpp, docs/missile-re.md): the stock steering is off (CP[8], its homing delay, never
# comes) and CP[9] marks the round as the plugin's; it inherits the launcher's velocity vector (AmmoOwnerMove 1), no
# drop. Each is modelled on a real missile: its motor's burn, acceleration and top speed, the g it pulls and its
# navigation constant (CP[3], read by the plugin), its warhead, its seeker (lock range, cone, lock time), how it is
# fired and how many a carrier holds (JETS: one missile type a carrier: the 506 body has one missile trigger). Lock
# ranges are the real ones cut down to the game's world (+-2.4 km); the jets fire within them (src/jet_combat.cpp
# MissileReach). Sounds (docs/sound-re.md §5): the air raid's missile launch for its shot, and a rocket motor's loop
# for its flight, from the always loaded TIKYUUX_SE.ACB.
JET_MISSILE_STOCK = 'V_506HELI_MISSILE01.SGO'
JET_MISSILE_FIRE_SE = ('weapon_KUBAKU_missile_shot', 1.0, 80.0)            # cue, volume, metres heard at full
JET_MISSILE_FLIGHT_SE = ('weapon_KUBAKUBallisticMissle01_go', 0.6, 80.0)
MISSILE_NO_STOCK_HOMING, MISSILE_MARK = 1000000.0, 4242.0   # src/missile.cpp kNoStockHoming, kPluginMark
# What a store's round looks like in flight (the user, 2026-10-07: 「不同挂载要有不同模型，射出去的时候也应该是对应模型」):
# both templates fire WEAPON/bullet_missile.rab (the 506 missile at AmmoSize 2: 2.1 m; the 409 bomb at 8: an 8.4 x 2.5 m
# "missile"), so every store looked the same. Each weapon now names a stock projectile model (STORE_MODELS: Root.cpk
# WEAPON/<model>.rab, read where the stock rounds' are) and its real length: AmmoSize scales the model uniformly to that
# length (make_katyusha.py's way), AmmoHitSizeAdjust keeps the template's contact sphere (AmmoSize x AmmoHitSizeAdjust),
# and the weapon's preload list names that model in the template's place (_look). A real air-to-air missile's body is
# ~20 diameters long, every stock model's ~10, so each weapon takes the model nearest its real body and fin span at its
# real length, and the rounds one control cycles (a carrier's stores, and a stock vehicle's own weapon beside its stores:
# the helis' and the Naegling's fire bullet_missile) never share a model (tools/selftest.py store_looks). Measured on the
# models (mid-length median radius, largest radius), m at AmmoSize 1, then at the weapon's length against the real one:
#   bullet_rpg              1.015 long, body 0.06, fins 0.30   AIM-9X 3.02 m: 0.18 / 0.88 (real 0.127 / 0.45)
#   bullet_mediummissile01  4.013 long, body 0.36, fins 0.62   AIM-120, ESSM 3.66 m: 0.33 / 0.57 (real 0.18-0.25 / 0.45-0.6)
#   bullet_icbm01           1.250 long, body 0.12, fins 0.29   AIM-54 3.96 m: 0.37 / 0.91 (real 0.38 / 0.91);
#                                                              AGM-114 1.63 m: 0.15 / 0.37 (real 0.18 / 0.33);
#                                                              Mk 82 2.22 m: 0.21 / 0.51 (real 0.27 / 0.38), tail fins only
#   airtortoise_missile     1.271 long, body 0.16, fins 0.29   AGM-65 2.49 m: 0.31 / 0.57 (real 0.30 / 0.72), mid wings
#   bullet_missile          1.050 long, body 0.10, fins 0.31   the Sazabi's shield missile (2.1 m, as the 506's)
#   bullet_rocket           1.052 long, body 0.11, fins 0.44   Hydra 70 1.4 m (the stock rocket launchers' round)
# There is no stock bomb model: a Mods/WEAPON .rab of our own, or a round model read from Mods/OBJECT, is not proven to
# load (only Mods/OBJECT vehicle models are), so the Mk 82 flies the tail-finned icbm01 at its real length. bullet_rpg and
# bullet_mediummissile01 are in Root.cpk but no stock weapon fires them (their material is one stock rounds use).
STORE_MODELS: dict[str, float] = {'bullet_rpg': 1.015, 'bullet_mediummissile01': 4.013, 'bullet_icbm01': 1.250,
                                  'bullet_missile': 1.050, 'airtortoise_missile': 1.271, 'bullet_rocket': 1.052}
STORE_MODEL_SLACK = 0.002   # m: the model measured on the player's Root.cpk against STORE_MODELS (_look)
STORE_LENGTH_SLACK = 0.01   # m: AmmoSize (3 places) x the model's length against the weapon's real length


@dataclass(frozen=True)
class Look:
    """A round's model (a STORE_MODELS key) and the real weapon's length (m) it flies at."""
    model: str
    length: float

    @property
    def path(self) -> str:
        return f'app:/WEAPON/{self.model}.rab'

    @property
    def size(self) -> float:
        """AmmoSize: the model scaled uniformly to the weapon's length."""
        return round(self.length / STORE_MODELS[self.model], 3)


@dataclass(frozen=True)
class Missile:
    """One class of missile. Speeds in m/s and s (written as the game's m a frame and frames)."""
    model: str            # the real missile it follows
    look: Look            # its round's model, at the real missile's length
    burn: float           # s of motor
    top: float            # m/s
    accel: float          # m/s^2 while it burns (to its top speed)
    max_g: float          # g across its path at most
    nav: float            # the navigation constant
    life: float           # s it flies before it is gone
    damage: float
    blast: float          # m: its blast radius (its proximity fuse goes off within 0.6 of it)
    lock_range: float     # m
    lock_cone: float      # rad off the launcher's nose (a vertical launcher: wide)
    lock_time: float      # frames to lock on
    burst: float = 1.0    # rounds a trigger pull
    burst_gap: float = 10.0   # frames between them
    interval: float = 45.0    # frames from one shot (or salvo) to the next
    eject: float = 0.3    # m a frame it leaves the rail at, along the launcher's nose, over the launcher's velocity
    # rad: FireAccuracy, the half angle of the cone a round leaves its rail in (fire 0x691B02 -> 0x4E820: polar angle
    # uniform in [0, it]; src/launcher.cpp Cone). A rail sets the missile's line: none. The helicopter template's 0.3
    # (17 deg) is a stock pursuit missile's spray; kept, it sent a ground launcher's missiles into the ground at its
    # feet and its 12 m blast onto itself (the user, 2026-10-25: 「bmpt的飞弹怎么会往地里射？天启也会射到自己身上导弹」).
    spread: float = 0.0
    guided: bool = True   # False: an unguided rocket (no lock: src/missile.cpp only burns its motor, it flies straight)
    # The stock weapon (Root.cpk WEAPON) whose muzzle list (animation_model[2], mab_muzzles) it fires from; None: the
    # helicopter missile's own four pod muzzles. Offsets are from the holder's bone: a gun-launched missile on a tank's
    # gun bone with the pods' (1.05..1.76 m aside, 0.67 m down) leaves from beside the hull (the user, 2026-10-10:
    # 「这个炮射导弹是从坦克车头两边射出去的」).
    muzzle: str | None = None

    def params(self, rounds: int) -> dict[str, float]:
        return {'AmmoSpeed': self.eject, 'AmmoOwnerMove': 1.0, 'AmmoGravityFactor': 0.0, 'AmmoAlive': self.life * 60.0,
                'AmmoDamage': self.damage, 'AmmoExplosion': self.blast, 'AmmoCount': float(rounds),
                'FireBurstCount': self.burst, 'FireBurstInterval': self.burst_gap, 'FireInterval': self.interval,
                'FireAccuracy': self.spread, 'LockonRange': self.lock_range, 'LockonTime': self.lock_time,
                # A lock lives while its target stays in the cone (Lockon_AutoTimeOut 0: the hold timer restarts
                # there) and LockonHoldTime frames once it leaves: the nose on another, it locks that one
                # (docs/stores-re.md §7; stock: 600 frames whatever the nose does, no switching).
                'Lockon_AutoTimeOut': 0.0, 'LockonHoldTime': 20.0, 'LockonFailedTime': 0.0,
                **({} if self.guided else {'LockonType': 0.0, 'LockonRange': 0.0})}

    def motion(self) -> dict[int, object]:
        """Ammo_CustomParameter: [3] the plugin's guidance (burn frames, g, navigation constant), [4] acceleration
        (m a frame per frame), [6] top speed (m a frame), [8] / [9] the plugin's (see above)."""
        return {3: [self.burn * 60.0, self.max_g, self.nav], 4: self.accel / 3600.0, 6: self.top / 60.0,
                8: MISSILE_NO_STOCK_HOMING, 9: MISSILE_MARK}


@dataclass(frozen=True)
class Bomb:
    """A free-fall bomb (GrenadeBullet01, impact fuse: Ammo_CustomParameter[0] 0): released with the launcher's velocity
    (AmmoOwnerMove 1), falling at the world's gravity (AmmoGravityFactor 1); no drag in the game's model."""
    model: str
    look: Look            # its round's model, at the real bomb's length
    damage: float
    blast: float
    life: float = 30.0      # s: it bursts on impact long before
    interval: float = 8.0   # frames between bombs held down (a ripple)
    eject: float = 0.05     # m a frame, along the launcher's nose

    def params(self, rounds: int) -> dict[str, float]:
        return {'AmmoSpeed': self.eject, 'AmmoOwnerMove': 1.0, 'AmmoGravityFactor': 1.0, 'AmmoAlive': self.life * 60.0,
                'AmmoDamage': self.damage, 'AmmoExplosion': self.blast, 'AmmoCount': float(rounds),
                'FireBurstCount': 1.0, 'FireInterval': self.interval, 'LockonType': 0.0, 'LockonRange': 0.0}


ROUND_KINDS = ('ap', 'he', 'mg', 'heat', 'canister')
# War Thunder's gun rounds (the user, 2026-11-26: 「抄抄战雷」; docs/feature-projectile-look.md): a bright tracer. A
# RocketBullet01 round (the stock bursting tank and autocannon shells) trails white smoke, CP[2] frames a node (the stock
# 105 mm's 100; white whatever AmmoColor), behind a flame CP[1] frames long in AmmoColor (docs/missile-re.md,
# RocketBullet01): the flame made a short hot streak, its tracer; the smoke kept as the stock's. The smoke is what stays
# after the burst (the user, 2026-11-27: 「炮弹爆炸以后尾烟会秒消失」): a dead round is kept until both rings are empty
# (0x287E40: +0x10A0 and +0x1290 before 0x118A1B0) and its nodes fade on their own life, so with the smoke off (CP[2] 0,
# the emitter never made: the ctor's enable is CP[2] != 0) the only trail left was the 14-frame flame, gone a quarter
# second after the burst. A SolidBullet01 round's tracer is a 12-point streak of AmmoColor as long as its frame's travel
# (0x28B6A0): AP_TRACER over the stock's blue.
SHELL_TRACER_CP = {1: 14.0}
AP_TRACER = (3.6, 1.4, 0.5, 1.0)


def wt_tracer(r: dsgo.Node, where: str) -> None:
    """A RocketBullet01 round's flame made a tracer (SHELL_TRACER_CP), its smoke trail the stock's, in place; ValueError
    when `r` is not one of the stock four-entry RocketBullet01 rounds or has no smoke trail to keep (CP[2] 0)."""
    cp = r.get('Ammo_CustomParameter')
    if r.get('AmmoClass') != 'RocketBullet01' or not isinstance(cp, dsgo.Node) or len(cp.items) != 4:
        raise ValueError(f'{where} 不是四项 CP 的 RocketBullet01 弹')
    if not cp.items[2] > 0:
        raise ValueError(f'{where} 的 CP[2]（尾烟寿命）不是正数：{cp.items[2]!r}')
    for i, value in SHELL_TRACER_CP.items():
        cp.items[i] = value


# Real muzzle velocities (the user, 2026-11-26: 「真实初速」, the stock vehicles' own guns too: 「一块改」). The stock guns of
# the vehicles with a real prototype (docs/feedback-2026-10-19-vehicle-ammo.md) leave at the speed of the real round of
# their class; the setting's own weapons (lasers, the railgun, acid, napalm, the Titan, the Varius' EfsBullet gun, the
# DLC grenade cannons) keep theirs. Each round keeps its reach (AmmoSpeed x AmmoAlive: the AI's fire gates and the sights
# read it) and its gravity: it gets there sooner and flatter. {the stock WEAPON file: (m/s, the real round, its tracer:
# 'ap' AP_TRACER, 'he' wt_tracer, None the stock's)}. The stock files are written over in Mods/WEAPON (stock_gun_sgo,
# tools/make_stock_stores.py), so every one of these vehicles fires them, a mission's own and a call's; the stores of the
# same gun take the same speed (_shell_sgo). EDF6AutoTurret's flak guns are its own (autoturret/tools/build.py).
_TANK_HE = (1140.0, '120 mm M830 HEAT-MP-T (M256)', 'he')
_TANK_AP = (1575.0, '120 mm M829A1 APFSDS-T (M256)', 'ap')
_M134 = (853.0, '7.62 mm M80 ball (M240 / M134)', None)
MUZZLE_VELOCITY: dict[str, tuple[float, str, str | None]] = {
    **{f'V_505TANK_{g}.SGO': _TANK_HE for g in ('CANNON01', 'CANNON02', 'CANNON02L', 'CANNON03', 'CANNON03L',
                                                 'CANNON04', 'CANNON04L', 'DLC_CANNON04L')},
    'V_505_TANK_AI_CANNON01.SGO': _TANK_HE,
    'V_505TANK_CANNON01S.SGO': _TANK_AP, 'V_505TANK_CANNON02S.SGO': _TANK_AP,
    # The Grape (an M242 25 mm IFV): M792 HEI-T, M791 APDS-T.
    'V_401STRIKER_CANNON.SGO': (1100.0, '25 mm M792 HEI-T (M242)', 'he'),
    'V_401STRIKER_CANNON02.SGO': (1100.0, '25 mm M792 HEI-T (M242)', 'he'),
    'V_401STRIKER_CANNONS01.SGO': (1345.0, '25 mm M791 APDS-T (M242)', 'ap'),
    'V_401STRIKER_CANNONS02.SGO': (1345.0, '25 mm M791 APDS-T (M242)', 'ap'),
    'V_403TANK_MACHINEGUN.SGO': _M134,
    'V_409HELI_GATLING01.SGO': _M134,
    'V_506HELI_GATLING01_L.SGO': _M134, 'V_506HELI_GATLING01_R.SGO': _M134,
    'V_602HELI_GATLING01_L.SGO': _M134, 'V_602HELI_GATLING01_R.SGO': _M134,
    'V_503_BIKE_GUN_L.SGO': _M134, 'V_503_BIKE_GUN_R.SGO': _M134,
}


def real_muzzle(r: dsgo.Node, mps: float, where: str) -> None:
    """Weapon root `r` made to leave at `mps` (m/s), in place: AmmoSpeed that a frame, AmmoAlive the frames to its old
    reach (rounded up); ValueError without the two."""
    speed, alive = r.get('AmmoSpeed'), r.get('AmmoAlive')
    if not isinstance(speed, float) or not isinstance(alive, float) or speed <= 0.0:
        raise ValueError(f'{where} 缺少 AmmoSpeed / AmmoAlive')
    step = round(mps / 60.0, 3)
    r.set('AmmoSpeed', step)
    r.set('AmmoAlive', float(math.ceil(speed * alive / step - 1e-6)))


def stock_gun_sgo(game: Game, name: str) -> bytes:
    """Stock WEAPON file `name` (MUZZLE_VELOCITY) at its real muzzle velocity and with its War Thunder tracer."""
    mps, _, tracer = MUZZLE_VELOCITY[name]
    doc = dsgo.parse(game.read('WEAPON', name))
    r = doc.root
    real_muzzle(r, mps, name)
    if tracer == 'he':
        wt_tracer(r, name)
    elif tracer == 'ap':
        colour = r.get('AmmoColor')
        if not isinstance(colour, dsgo.Node) or len(colour.items) != 4:
            raise ValueError(f'{name} 的 AmmoColor 不是 4 个数')
        colour.items[:] = list(AP_TRACER)
    return dsgo.write(doc)


@dataclass(frozen=True)
class Shell:
    """A gun's round for a stock vehicle (the user, 2026-10-09: 「坦克应该有ap和he」「机炮车 AP/HE」「防空车高爆近炸」): a
    stock vehicle weapon of that kind made a store, its numbers the stock weapon's own (the game's own round of that class
    and tier: AP the penetrating high-velocity body, HE the bursting one), only AmmoCount (the load in the file name) and
    `extra` changed. `ammo_class` is the template's AmmoClass, checked on the player's Root.cpk (ValueError otherwise):
    the kind of round is the template's, not a guess."""
    template: str           # Root.cpk WEAPON file
    ammo_class: str         # its AmmoClass
    # 'ap' (no blast, penetrating), 'he' (a blast radius), 'mg' (a machine gun's ball: no blast), 'heat' (a shaped
    # charge: the bursting template made a fast, flat, contact-fused round that stops on what it hits, a small
    # blast, more damage on that one target) or 'canister' (a cloud of balls: the penetrating template firing
    # FireCount pellets in a wide cone, short reach). ROUND_KINDS; _shell_sgo checks the template and the result.
    kind: str
    extra: tuple[tuple[str, float], ...] = ()
    # A coaxial gun's muzzle: (the template's single muzzle where it is, where it goes) in the frame of the holder bone
    # it hangs on (make_stock_stores COAX: the gun mantlet's elevating bone, measured on each tank's model). None: the
    # template's muzzle as it is.
    muzzle: tuple[tuple[float, float, float], tuple[float, float, float]] | None = None
    # The gun's own model (Mods/WEAPON, coax_model): a coaxial machine gun seen in the mantlet, its muzzle on muzzle's
    # point. None: the template's model (v_null: none to see).
    model: str | None = None
    # Its round's colour (AmmoColor) and its muzzle flash's (MuzzleFlash_CustomParameter[8]); None: the template's.
    colour: tuple[float, float, float, float] | None = None
    flash: tuple[float, float, float, float] | None = None

    def params(self, rounds: int) -> dict[str, float]:
        return {'AmmoCount': float(rounds), **dict(self.extra)}


@dataclass(frozen=True)
class Store:
    """What a jet carries besides its guns (STORES): its weapon, its role for the plugin (src/stores.h StoreRole: air
    and ground missiles, bombs, rockets, a gun's rounds), and what one round adds to the jet: its mass and its drag (a
    share of the clean jet's parasitic drag; 0 for the stock vehicles' rounds, nothing weighs them). What it hangs on
    stays when its rounds are gone (test hub #39, the player: 「某些挂载投完以后应该只有挂架重量（如有的话）」): `per_pylon`
    rounds a pylon (its rail, rack or pod) holds, each such pylon `pylon_mass` kg and `pylon_drag` of the clean jet's
    drag, ceil(rounds / per_pylon) of them for a holder of `rounds` (src/stores.h StoreSpec, BurdenOf); per_pylon 0: no
    pylon (a gun's rounds, a ship's cells).
    `names`: the weapon's name.<lang> rows (the stock HUD shows the game language's, weapon +0x1B0); a language not
    listed (and the cockpit) shows `name`."""
    name: str               # shown in the cockpit (and the weapon's name.* rows)
    role: str               # 'air', 'ground', 'bomb', 'rocket', 'gun'
    mass: float             # kg a round
    drag: float             # a round's share of the clean jet's drag
    weapon: Missile | Bomb | Shell
    names: tuple[tuple[str, str], ...] = ()
    group: int = 0          # nonzero: the barrels of one selectable loadout (not separate R/LB choices)
    barrel: int = 0         # original native holder index this linked barrel replaces
    native_control: bool = False  # original native holder, selectable independently (two belts, one gun)
    # The vehicle's own fire key for this weapon (FIRE_KEYS; docs/multi-weapon-keys.md): 0 none (it rides the primary's
    # cycle, or is native); FIRE_SECONDARY the seat's otherwise unread second trigger (the stores of one vehicle on it are
    # one launcher's rounds, cycled by VehicleSecondarySwitchKey); 3..6 VehicleFire<n>Key, one weapon a key on a
    # vehicle. Every key fires at once with the primary and the others (src/payload.cpp OwnKey).
    key: int = 0
    # An automatic turret: a gunner seat's own turret (needs `seat`) that, with nobody in that seat and the vehicle's auto
    # turrets on (VehicleAutoTurretKey), turns onto an enemy in its reach and fires itself (src/seat_turret.cpp,
    # src/payload.cpp UnmannedTurretsFire). Only a turret that lays itself: a weapon on the driver's key fires along the
    # driver's aim, and pulling its trigger by itself whenever that aim happened onto an enemy or a lock was "an auto
    # turret" that never turned, firing its rounds off on its own (the player, 2026-11-21: 「载具的某些副炮塔能自动开火真的
    # 对劲吗，怎么感觉怪怪的」; docs/feedback-2026-11-21-vehicle-weapons.md).
    auto: bool = False
    # The crew seat that mans it (docs/multi-weapon-keys.md「独立座位」; the user, 2026-10-30: 「一些应该是独立的座位」): 0 the
    # seat its key is on (the driver's); n >= 1 its own gunner seat n (its holder on that seat: vehicle_weapon_setting
    # [bone, n]), fired by that seat's primary trigger (src/payload.cpp ControlOf: the V603 never reads it), aimed by its
    # rider; with nobody in it an auto turret aims and fires itself. Needs `turret` and no `key`.
    seat: int = 0
    # Its own turret, turned apart from the vehicle's (Turret: src/seat_turret.cpp poses its bones), else None: it fires
    # along its holder's bone, which the seat's turret turns.
    turret: Turret | None = None
    per_pylon: int = 0          # rounds one pylon (rail, rack, pod) holds; 0 none
    pylon_mass: float = 0.0     # kg a pylon, loaded or spent
    pylon_drag: float = 0.0     # a pylon's share of the clean jet's drag, loaded or spent

    def __post_init__(self) -> None:
        if self.per_pylon < 0 or self.pylon_mass < 0.0 or self.pylon_drag < 0.0:
            raise ValueError(f'{self.name}: a pylon weighs and drags nothing less than nothing')
        if not self.per_pylon and (self.pylon_mass or self.pylon_drag):
            raise ValueError(f'{self.name}: the pylon mass or drag needs its rounds a pylon (per_pylon)')
        if self.key not in (0, *FIRE_KEYS):
            raise ValueError(f'{self.name}: key {self.key} is none of 0, {FIRE_KEYS}')
        if self.auto and not self.seat:
            raise ValueError(f"{self.name}: an auto turret is a gunner seat's own turret (seat): on the driver's key "
                             "it only fires along the driver's aim")
        if (self.key or self.seat) and (self.group or self.native_control):
            raise ValueError(f'{self.name}: a weapon on its own key is no belt of the primary')
        if not 0 <= self.seat <= MOST_GUNNER_SEAT:
            raise ValueError(f'{self.name}: seat {self.seat} is none of 0..{MOST_GUNNER_SEAT}')
        if self.seat and (self.key or self.turret is None):
            raise ValueError(f'{self.name}: a gunner seat weapon has its turret and no key (the seat trigger fires it)')


@dataclass(frozen=True)
class Turret:
    """A store's own turret (Store.turret; src/seat_turret.cpp): two bones of its vehicle's model the plugin turns every
    frame, `yaw` about its upright and `pitch` (under it) its guns' lift about their own across; its muzzles ride `pitch`
    (its holder's bone). Matched by name in the model (the engine's bone names: at most 15 characters, distinct). Angles
    deg, from its bind pose (ahead, level); rates deg/s."""
    yaw: str
    pitch: str
    low: float = -5.0          # its guns' elevations
    high: float = 70.0
    arc: float = 180.0         # its traverse either side of ahead (180: all round)
    yaw_rate: float = 60.0
    pitch_rate: float = 45.0

    def __post_init__(self) -> None:
        for bone in (self.yaw, self.pitch):
            if not bone or len(bone) > 15:
                raise ValueError(f'turret bone {bone!r}: 1..15 characters')
        if self.yaw == self.pitch or not -90.0 <= self.low < self.high <= 90.0 or not 0.0 < self.arc <= 180.0 or                 not (self.yaw_rate > 0.0 and self.pitch_rate > 0.0):
            raise ValueError(f'turret {self.yaw}/{self.pitch}: bad limits')


# The vehicles' own fire keys (Store.key; src/stores.h StoreSpec::fireKey): the second trigger, then the four extra keys.
FIRE_SECONDARY = 2
FIRE_KEYS = (FIRE_SECONDARY, 3, 4, 5, 6)
MOST_GUNNER_SEAT = 7   # Store.seat: src/stores.h kMostGunnerSeat


def _local(en: str, ja: str, cn: str, sc: str, kr: str) -> tuple[tuple[str, str], ...]:
    return (('en', en), ('ja', ja), ('cn', cn), ('sc', sc), ('kr', kr))


# --- the coaxial machine guns (the user, 2026-10-11: 「应该有同轴机枪的坦克增加同轴机枪」, then 「这个坦克的同轴机枪做的
# 什么玩意，找现实的模型来做」) ---
# A real tank's coax is a 7.62 mm general-purpose machine gun inside the gun mantlet beside the main gun (M240C on the
# M1 Abrams, the LAV-25 and the Bradley; MG3 on the Leopard 2; PKT on the T-72; the Type 74 on the Type 10), locked
# parallel to the main gun so it elevates and traverses with it. Outside, all that shows is the end of its barrel
# and its flash hider standing out of a small port collar on the mantlet's face: the receiver, the feed and the
# ammunition box are inside the turret. The 2026-10-11 model was none of that (docs/coax-feedback-2026-10-11.md): the
# Begaruta mech's whole hand gun shrunk to a quarter, stock, grip and 0.3 m magazine and all, stuck outside the turret,
# its bone's own 1.3 scale left in so it stood 1.25 m long and 30% further out than the muzzle point it was put on.
@dataclass(frozen=True)
class CoaxGun:
    """A real coaxial machine gun: what it fires (7.62 x 51 mm) and the part of it outside the mantlet (m)."""
    name: str           # the real gun
    interval: float     # frames from one round to the next (60 a second): its cyclic rate
    speed: float        # m/s at the muzzle (M80 ball)
    reach: float        # m: its rounds live this far (the gun's effective reach as a vehicle coax)
    barrel_r: float     # the barrel's outer radius where it leaves the port
    bare: float         # bare barrel from the port collar's face to the flash hider
    hider_r: float      # the flash hider's outer radius
    hider: float        # its length

    @property
    def rpm(self) -> float:
        return 3600.0 / self.interval

    @property
    def outside(self) -> float:
        """m of gun outside the port collar: bare barrel and flash hider."""
        return self.bare + self.hider


# The M240C (FN MAG; the coax of the M1 Abrams, the LAV-25, the Bradley): 650-950 rounds a minute cyclic, here 720
# (a round every 5 frames), 853 m/s with M80 ball, 1100 m effective on a vehicle mount (the M62 tracer burns ~900 m);
# a 0.63 m barrel, 24 mm across at the muzzle, a 0.1 m flash hider about 30 mm across.
M240C = CoaxGun('M240C', interval=5.0, speed=853.0, reach=1100.0, barrel_r=0.012, bare=0.14, hider_r=0.015,
                hider=0.10)
# The Type 74 vehicle machine gun (the coax of Japan's Type 74 / 90 / 10 tanks): about 1000 a minute, here 900
# (a round every 4 frames), 7.62 x 51 mm at about 855 m/s, a barrel and flash hider of the M240's size.
TYPE74 = CoaxGun('Type 74', interval=4.0, speed=855.0, reach=1100.0, barrel_r=0.012, bare=0.12, hider_r=0.016,
                 hider=0.09)


@dataclass(frozen=True)
class CoaxMount:
    """A coax on one stock tank: its gun, the centre of its port on the face of the elevating part of the mantlet
    (the holder bone's frame: +x the vehicle's left, so a port right of the gun is at -x; +z along the bore; measured
    on each model by casting rays at the mantlet from ahead, docs/coax-feedback-2026-10-11.md §2), the port collar's
    radius and how far it stands proud of the face and reaches back into it (the faces slope), and its belt."""
    gun: CoaxGun
    port: tuple[float, float, float]
    collar_r: float
    collar_out: float
    collar_in: float
    belt: int           # rounds in the ready belt (AmmoCount; then ReloadTime COAX_BELT_CHANGE to change it)

    @property
    def muzzle(self) -> tuple[float, float, float]:
        """The flash hider's front face: where the rounds come out (the muzzle locator)."""
        x, y, z = self.port
        return (x, y, round(z + self.collar_out + self.gun.outside, 4))


COAX_BELT_CHANGE = 600.0   # frames (10 s) for the loader to change the belt
# The rounds: red-orange tracer (NATO M62 / M276 burn red); every round in the game's file is a tracer, the plugin
# dims four in five to ball (src/payload.cpp CoaxBelt: a coax belt is linked four ball to one tracer). Keep these two
# colours and COAX_TRACER_EVERY equal to src/payload.cpp's kCoaxTracer / kCoaxBall / kCoaxTracerEvery (tested).
COAX_TRACER = (3.0, 1.0, 0.35, 1.0)
COAX_BALL = (0.3, 0.28, 0.26, 0.1)
COAX_TRACER_EVERY = 5
COAX_FLASH = (2.4, 1.7, 0.9, 1.0)   # a rifle-calibre muzzle flash: orange (the template's is green)
COAX_SIZE = 0.15                    # AmmoSize: a thin streak (the template's 0.3 ball); contact radius still 0.15:
COAX_HIT = 1.0                      # AmmoHitSizeAdjust (0.3 x 0.5 before)
COAX_SPREAD = 0.006                 # FireAccuracy: a mounted gun's tight burst (the template's 0.025 a gunner's)
COAX_MOUNTS: dict[str, CoaxMount] = {
    # Blacker 505 (the M1 Abrams' class; its five requests share the v505_tank skeleton): the elevating 'cannon'
    # bone's mantlet is 0.55 m wide (|x| <= 0.275; beyond it the turret cheeks, which do not elevate), its face a
    # flat nose 0.64 m ahead between y -0.10 and -0.02, the barrel's root collar inside |x| 0.17: the port right of
    # the bore 0.22 m on that nose. (A real M1's gun shield is wider and its coax sits further out; on this model
    # anything past 0.275 m would be on the turret and would not lay with the gun.)
    '505': CoaxMount(M240C, (-0.22, -0.06, 0.64), collar_r=0.035, collar_out=0.03, collar_in=0.04, belt=3000),
    # Varius 601 (a Type 10 class tank): its 'cannon' bone carries a coax housing modelled right of the gun, its front
    # face 1.80-1.84 m ahead over x -0.30..-0.22, y 0.00..0.16: the port in its middle.
    '601': CoaxMount(TYPE74, (-0.26, 0.08, 1.84), collar_r=0.03, collar_out=0.03, collar_in=0.05, belt=3000),
    # Grape 401 (an LAV-25 / Bradley class IFV): its 'Barrel' bone is the elevating autocannon itself (its root sleeve
    # 0.1 m round, 0.35 m ahead); the turret face right of it 0.18-0.24 m ahead (it slopes): the port 0.18 m right of
    # the bore, the collar riding on the gun as the mount's shield does.
    '401': CoaxMount(M240C, (-0.18, 0.0, 0.21), collar_r=0.03, collar_out=0.04, collar_in=0.06, belt=2200),
}
COAX_GUN_STOCK = 'V_504_H_MACHINEGUN.RAB'   # the model's template: only its gunmetal material and texture are used
COAX_GUN_NODE = 'v_Null'   # the template's muzzle node (V_403TANK_MACHINEGUN's MAB): the model's gun bone named so
COAX_GUN_TINTS = {'collar': (58, 62, 62), 'barrel': (30, 31, 33), 'hider': (24, 24, 26), 'bore': (10, 10, 12)}


def coax_model_name(tank: str) -> str:
    return f'EDF6VC_COAX_GUN{tank}.RAB'


def _coax_store(tank: str, mount: CoaxMount) -> 'Store':
    gun = mount.gun
    step = gun.speed / 60.0   # m a frame
    extra = (('AmmoSpeed', round(step, 3)), ('AmmoAlive', float(math.ceil(gun.reach / step))),
             ('FireInterval', gun.interval), ('ReloadTime', COAX_BELT_CHANGE), ('FireAccuracy', COAX_SPREAD),
             ('AmmoSize', COAX_SIZE), ('AmmoHitSizeAdjust', COAX_HIT))
    shell = Shell('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg', extra, ((0.0, 0.2, 2.47), mount.muzzle),
                  coax_model_name(tank), COAX_TRACER, COAX_FLASH)
    g = gun.name
    return Store(f'{g} coax', 'coax', 0.0, 0.0, shell,
                 _local(f'Coaxial MG ({g})', f'同軸機銃（{g}）', f'同軸機槍（{g}）', f'同轴机枪（{g}）', f'동축 기관총（{g}）'))

STORES: dict[str, Store] = {
    # Short-range air-to-air, infrared, high off-boresight (AIM-9X): quick to lock, very agile, light warhead. 3.02 m.
    # Its rail (LAU-7 class, ~35 kg) one a pylon.
    'AAM_S': Store('AIM-9X', 'air', 85.0, 0.004, Missile('AIM-9X', Look('bullet_rpg', 3.02), burn=5.0, top=850.0, accel=300.0,
                   max_g=50.0, nav=4.0, life=12.0, damage=500.0, blast=10.0, lock_range=1500.0, lock_cone=0.6, lock_time=10.0),
                   per_pylon=1, pylon_mass=35.0, pylon_drag=0.002),
    # Medium-range air-to-air, active radar (AIM-120): a long burn, fast. 3.66 m.
    'AAM_M': Store('AIM-120', 'air', 152.0, 0.007, Missile('AIM-120', Look('bullet_mediummissile01', 3.66), burn=8.0, top=1200.0,
                   accel=250.0, max_g=40.0, nav=4.0, life=16.0, damage=600.0, blast=12.0, lock_range=2500.0, lock_cone=0.35,
                   lock_time=30.0), per_pylon=1, pylon_mass=45.0, pylon_drag=0.003),   # its LAU-127 rail, one a pylon
    # Long-range air-to-air (AIM-54): a very long burn, very fast, a big warhead, not agile. 3.96 m.
    'AAM_L': Store('AIM-54', 'air', 450.0, 0.018, Missile('AIM-54', Look('bullet_icbm01', 3.96), burn=20.0, top=1500.0, accel=150.0,
                   max_g=25.0, nav=3.0, life=30.0, damage=700.0, blast=15.0, lock_range=3000.0, lock_cone=0.3, lock_time=45.0,
                   interval=90.0), per_pylon=1, pylon_mass=90.0, pylon_drag=0.007),   # its LAU-93 rail and pallet
    # Air-to-ground (AGM-65 Maverick): subsonic, a heavy warhead, a slow lock. 2.49 m.
    'AGM': Store('AGM-65', 'ground', 300.0, 0.014, Missile('AGM-65', Look('airtortoise_missile', 2.49), burn=3.5, top=320.0, accel=120.0,
                 max_g=15.0, nav=3.0, life=15.0, damage=1500.0, blast=18.0, lock_range=1800.0, lock_cone=0.3, lock_time=40.0,
                 interval=60.0), per_pylon=1, pylon_mass=60.0, pylon_drag=0.006),   # its LAU-117 rail, one a pylon
    # Light air-to-ground (AGM-114 Hellfire): what a drone carries, four to a launcher (M299, ~66 kg). 1.63 m.
    'AGM_L': Store('AGM-114', 'ground', 50.0, 0.002, Missile('AGM-114', Look('bullet_icbm01', 1.63), burn=3.0, top=425.0,
                   accel=180.0, max_g=20.0, nav=3.0, life=12.0, damage=800.0, blast=10.0, lock_range=1200.0, lock_cone=0.3,
                   lock_time=30.0), per_pylon=4, pylon_mass=66.0, pylon_drag=0.008),
    # Ship-launched air defence from a vertical launcher (RIM-162 ESSM): the bay need not face the target (a wide cone),
    # very fast, very agile, fired two at a target (the submarine carrier, src/subcarrier.cpp: no mass that matters). 3.66 m.
    'ESSM': Store('RIM-162 ESSM', 'air', 0.0, 0.0, Missile('RIM-162 ESSM', Look('bullet_mediummissile01', 3.66), burn=4.0,
                  top=1300.0, accel=400.0, max_g=50.0, nav=4.0, life=15.0, damage=600.0, blast=15.0, lock_range=3000.0,
                  lock_cone=1.2, lock_time=20.0, burst=2.0, interval=120.0, eject=0.5)),
    # General-purpose 500 lb free-fall bomb (Mk 82): a heavier warhead than the Maverick's, a wider blast. 2.22 m. One a
    # pylon on its bomb rack (~50 kg).
    'MK82': Store('Mk 82', 'bomb', 230.0, 0.011, Bomb('Mk 82', Look('bullet_icbm01', 2.22), damage=1500.0, blast=25.0),
                  per_pylon=1, pylon_mass=50.0, pylon_drag=0.004),
    # Unguided 70 mm rockets (Hydra 70) from a pod of 19: a short burn to about Mach 2, a light warhead; a trigger pull
    # ripples 4. Strafing runs fire them before the guns (src/jet_combat.cpp kRocket*); the player fires them along
    # the nose. About 1.4 m with its M151 warhead (the Mk 66 motor 1.06 m). The pod (19 tubes, ~40 kg) stays when it is
    # empty, and with it nearly all of its drag (a full pod's 0.0285: a round's 0.0002, the pod's 0.0247).
    'RKT': Store('Hydra 70', 'rocket', 32.0, 0.0002, Missile('Hydra 70', Look('bullet_rocket', 1.4), burn=1.1, top=740.0,
                 accel=650.0, max_g=1.0, nav=1.0, life=6.0, damage=250.0, blast=6.0, lock_range=0.0, lock_cone=0.3, lock_time=0.0,
                 burst=4.0, burst_gap=4.0, interval=20.0, eject=0.6, spread=0.006, guided=False),
                 per_pylon=19, pylon_mass=40.0, pylon_drag=0.0247),
    # --- the stock vehicles' rounds (tools/make_stock_stores.py LOADOUTS; docs/feedback-2026-10-09-loadcamp.md) ---
    # A main battle tank's armour-piercing round: the Blacker A's smooth-bore round (V_505TANK_CANNON01S:
    # SolidBullet01Rail, a tenth of gravity, penetrating, no blast, 350): the stock A series; at the M829A1's 1575 m/s
    # (MUZZLE_VELOCITY: the stock's 18 m a frame was 1080).
    'AP': Store('APFSDS', 'gun', 0.0, 0.0, Shell('V_505TANK_CANNON01S.SGO', 'SolidBullet01Rail', 'ap', colour=AP_TRACER),
                _local('APFSDS', 'APFSDS（徹甲弾）', 'APFSDS（穿甲彈）', 'APFSDS（穿甲弹）', 'APFSDS（철갑탄）')),
    # Its high-explosive round: the Blacker E's round (V_505TANK_CANNON01: RocketBullet01, no motor (CP[0] 1), full
    # gravity, an 8 m blast, 350): the stock E series; at the M830's 1140 m/s (MUZZLE_VELOCITY: the stock's 3 m a frame
    # was 180).
    'HE': Store('HE', 'gun', 0.0, 0.0, Shell('V_505TANK_CANNON01.SGO', 'RocketBullet01', 'he'),
                _local('HE', 'HE（榴弾）', 'HE（高爆彈）', 'HE（高爆弹）', 'HE（고폭탄）')),
    # --- rounds the tanks' real prototypes fire and the stock game lacks (the user, 2026-10-19: 「坦克好像还有串联破甲弹
    # 或者破甲弹之类的弹种。你看看各种载具缺什么都补一下」; only a round the vehicle's prototype really carries:
    # docs/feedback-2026-10-19-vehicle-ammo.md). Both from the Blacker's own gun files (their muzzle (0, 0, 2.8) is the
    # 505's and the 601's main gun's), so they leave the barrel the stock rounds leave.
    # HEAT-MP: the multi-purpose shaped-charge round of a 120 mm smooth-bore (the Abrams' M830 / M830A1, the Type 10's
    # JM12A1). The Blacker E's bursting round (RocketBullet01) made one: a shaped charge's jet stops on the one thing
    # it hits (no penetration through a line of enemies as the APFSDS), so it puts more on that target (450 to the
    # stock rounds' 350) with a small fragmentation burst (3 m to the howitzer's 8-18); it leaves at the M830's
    # 1140 m/s (19 m a frame; the M829A1 APFSDS' 1575), flat (gravity 0.15 against the howitzer's 1), reaching as far
    # as before (96 frames: 1824 m); a contact sphere of 0.8 m (the HE's 1.6); orange-red.
    'HEAT': Store('HEAT-MP', 'gun', 0.0, 0.0,
                  Shell('V_505TANK_CANNON01.SGO', 'RocketBullet01', 'heat',
                        (('AmmoSpeed', 19.0), ('AmmoGravityFactor', 0.15), ('AmmoAlive', 96.0), ('AmmoDamage', 450.0),
                         ('AmmoExplosion', 3.0), ('AmmoIsPenetration', 0.0), ('AmmoHitSizeAdjust', 0.5)),
                        colour=(3.2, 1.3, 0.5, 1.0)),
                  _local('HEAT-MP', 'HEAT-MP（多目的対戦車榴弾）', 'HEAT-MP（多用途破甲彈）', 'HEAT-MP（多用途破甲弹）',
                         'HEAT-MP（다목적 대전차고폭탄）')),
    # Canister (the Abrams' M1028: about 1100 tungsten balls, against infantry in the open at up to ~500 m): the
    # Blacker A's penetrating round made a cloud: 12 pellets a shot in a 0.07 cone, 40 each (480 on a big target at
    # close range), the M1028's 1410 m/s, gone after 22 frames (~520 m), penetrating soft targets as the template does;
    # no blast.
    # Only the Blacker's: the Type 10 (the 601's prototype) has no canister round.
    'CANISTER': Store('Canister', 'gun', 0.0, 0.0,
                      Shell('V_505TANK_CANNON01S.SGO', 'SolidBullet01Rail', 'canister',
                            (('FireCount', 12.0), ('FireAccuracy', 0.07), ('AmmoSpeed', 23.5), ('AmmoAlive', 22.0),
                             ('AmmoDamage', 40.0), ('AmmoGravityFactor', 0.5), ('AmmoSize', 0.5),
                             ('AmmoHitSizeAdjust', 0.6)),
                            colour=(2.6, 2.2, 1.4, 1.0)),
                      _local('Canister (M1028)', 'キャニスター弾（M1028）', '霰彈（M1028）', '霰弹（M1028）',
                             '캐니스터탄（M1028）')),
    # A gun-launched anti-tank missile (LAHAT class: 105/120 mm, 0.975 m, 13 kg, semi-active laser, subsonic, about
    # 8 km): from the tank's own barrel, kicked out at 120 m/s, its motor to 300 m/s. Its warhead a tandem HEAT smaller
    # than the Hellfire's (800 here): 600, a 6 m blast; its lock as the Hellfire's. 0.975 m of the tail-finned icbm01:
    # body 0.09, fins 0.23 (real 0.105).
    'GLM': Store('LAHAT', 'ground', 0.0, 0.0, Missile('LAHAT', Look('bullet_icbm01', 0.975), burn=2.5, top=300.0, accel=180.0,
                 max_g=15.0, nav=3.0, life=15.0, damage=600.0, blast=6.0, lock_range=1200.0, lock_cone=0.3, lock_time=30.0,
                 interval=90.0, eject=2.0, muzzle='V_505TANK_CANNON01.SGO'),
                 _local('LAHAT (gun-launched ATGM)', 'LAHAT（砲発射ミサイル）', 'LAHAT（炮射導彈）', 'LAHAT（炮射导弹）',
                        'LAHAT（포발사 미사일）')),
    # An infantry fighting vehicle's autocannon belts (the Grape): its AP the Grape's own smooth-bore round
    # (V_401STRIKER_CANNONS01: SolidBullet01, penetrating, 40), its HE the Grape's own bursting round
    # (V_401STRIKER_CANNON: RocketBullet01, a 5 m blast, 32); at the M242's M791 / M792 speeds (MUZZLE_VELOCITY).
    'AC_AP': Store('AP belt', 'gun', 0.0, 0.0, Shell('V_401STRIKER_CANNONS01.SGO', 'SolidBullet01', 'ap', colour=AP_TRACER),
                   _local('AP belt', 'AP弾帯（徹甲）', 'AP彈鏈（穿甲）', 'AP弹链（穿甲）', 'AP 탄띠（철갑）')),
    'AC_HE': Store('HE belt', 'gun', 0.0, 0.0, Shell('V_401STRIKER_CANNON.SGO', 'RocketBullet01', 'he'),
                   _local('HE belt', 'HE弾帯（榴弾）', 'HE彈鏈（高爆）', 'HE弹链（高爆）', 'HE 탄띠（고폭）')),
    # The flak's high-explosive proximity round: the stock Volus' anti-aircraft explosive gun round
    # (V603_FLAK_GLGUN01_DLC_L: GrenadeBullet01_MapNoDamage, 300 m/s, a 4 m blast, 9). Its contact sphere is 4 m
    # (AmmoSize 4 x AmmoHitSizeAdjust 1, docs/carrier-laser-re.md §3): passing within 4 m of an aircraft sets it off, a
    # proximity fuse in effect; it does not harm the map.
    'FLAK_AP': Store('AP', 'gun', 0.0, 0.0, Shell('V603_FLAK_GUNH01_DLC_L.SGO', 'SolidBullet01', 'ap'),
                     _local('AP belt', '徹甲弾帯', '穿甲彈鏈', '穿甲弹链', '철갑 탄띠'), group=1, barrel=0),
    'FLAK_AP_R': Store('AP', 'gun', 0.0, 0.0, Shell('V603_FLAK_GUNH01_DLC_R.SGO', 'SolidBullet01', 'ap'),
                     _local('AP belt', '徹甲弾帯', '穿甲彈鏈', '穿甲弹链', '철갑 탄띠'), group=1, barrel=1),
    'FLAK_HE': Store('HE-PROX', 'gun', 0.0, 0.0, Shell('V603_FLAK_GLGUN01_DLC_L.SGO', 'GrenadeBullet01_MapNoDamage', 'he'),
                     _local('HE proximity', '近接信管榴弾', '近炸引信高爆彈', '近炸引信高爆弹', '근접신관 고폭탄'), group=2, barrel=0),
    'FLAK_HE_R': Store('HE-PROX', 'gun', 0.0, 0.0, Shell('V603_FLAK_GLGUN01_DLC_R.SGO', 'GrenadeBullet01_MapNoDamage', 'he'),
                     _local('HE proximity', '近接信管榴弾', '近炸引信高爆彈', '近炸引信高爆弹', '근접신관 고폭탄'), group=2, barrel=1),
    # Two native 603 holders represent alternate ammunition for the same M61 barrel cluster.
    'VULCAN_HE': Store('20 mm M61 HE', 'gun', 0.0, 0.0,
                       Shell('V603_FLAK_GLGUN01_DLC_L.SGO', 'GrenadeBullet01_MapNoDamage', 'he'),
                       _local('20 mm M61 HE', '20mm M61 榴弾', '20mm M61 高爆彈', '20mm M61 高爆弹', '20mm M61 고폭탄'), native_control=True),
    'VULCAN_AP': Store('20 mm M61 AP', 'gun', 0.0, 0.0,
                       Shell('V603_FLAK_GUNH01_DLC_L.SGO', 'SolidBullet01', 'ap'),
                       _local('20 mm M61 AP', '20mm M61 徹甲弾', '20mm M61 穿甲彈', '20mm M61 穿甲弹', '20mm M61 철갑탄'), native_control=True),
    # The quad Vulcan's AP belt (tools/make_tracked_combat.py QUADVULCAN_AP_BELT): its HE is the native pair, this the
    # second loadout on the same four guns (group 4, its two columns the native holders' barrels).
    # The 2S6's 9M311 (tools/make_tracked_combat.py, models/wt_tunguska): a SACLOS anti-air missile, here locking air
    # targets, on the V603's otherwise unread secondary as the BMPT's Ataka (key FIRE_SECONDARY). 2.56 m.
    'TUNGUSKA_SAM': Store('9M311', 'air', 0.0, 0.0,
                          Missile('9M311', Look('bullet_rpg', 2.56), burn=3.0, top=900.0, accel=350.0, max_g=35.0,
                                  nav=4.0, life=14.0, damage=500.0, blast=9.0, lock_range=2500.0, lock_cone=0.5,
                                  lock_time=15.0),
                          _local('9M311', '9M311', '9M311', '9M311', '9M311'), key=FIRE_SECONDARY),
    # The Apocalypse's anti-air missiles (its pods beside the turret, fixed to it: Red Alert 2's own were anti-air): on the
    # V603's otherwise unread secondary as the BMPT's Ataka, fired by the driver on his own lock (no auto turret: the pods
    # do not lay themselves, Store.auto).
    'APOC_SAM': Store('Apocalypse SAM', 'air', 0.0, 0.0,
                      Missile('Apocalypse SAM', Look('bullet_mediummissile01', 3.0), burn=4.0, top=1000.0, accel=320.0,
                              max_g=35.0, nav=4.0, life=15.0, damage=700.0, blast=12.0, lock_range=2600.0,
                              lock_cone=0.6, lock_time=15.0),
                      _local('Anti-air missile', '対空ミサイル', '防空飛彈', '防空导弹', '대공 미사일'), key=FIRE_SECONDARY),
    # The Patriot's PAC-3 (tools/make_patriot.py, models/emp_patriot): a hit-to-kill anti-air missile from the pack's
    # four tubes, on the V603's otherwise unread secondary as the BMPT's Ataka.
    'PATRIOT_SAM': Store('PAC-3', 'air', 0.0, 0.0,
                         Missile('PAC-3', Look('bullet_mediummissile01', 5.2), burn=4.0, top=1400.0, accel=450.0,
                                 max_g=50.0, nav=4.5, life=16.0, damage=2500.0, blast=14.0, lock_range=3500.0,
                                 lock_cone=0.6, lock_time=20.0),
                         _local('PAC-3', 'PAC-3', 'PAC-3', 'PAC-3', 'PAC-3'), key=FIRE_SECONDARY),
    # The PzH 2000's roof MG3 (the tester, 2026-10-28, #29: 「Pzh2000自行榴弹炮可以加个机枪或机枪位」; models/wt_pzh2000's
    # MG on the loader's ring, tools/prep_wt_tracked.py PZH2000 stores): a 7.62 x 51 mm general-purpose machine gun on
    # the V603's otherwise unread secondary (key FIRE_SECONDARY, as the 2S6's 9M311), fired while the howitzer keeps
    # its load. The stock Epsilon gunner's machine gun round (as the coax, COAX_*) with the MG3's numbers: about 1200 a
    # minute (a round every 3 frames), 820 m/s (13.7 m a frame), its rounds alive 1100 m; a tight mounted cone; a belt
    # of 1000 changed in 10 s. On the turret, it turns with it and fires level along it (no elevation of its own).
    'PZH2000_MG': Store('MG3', 'gun', 0.0, 0.0,
                        Shell('V_403TANK_MACHINEGUN.SGO', 'SolidBullet01', 'mg',
                              (('AmmoSpeed', 13.7), ('AmmoAlive', 81.0), ('FireInterval', 3.0),
                               ('ReloadTime', COAX_BELT_CHANGE), ('FireAccuracy', COAX_SPREAD), ('AmmoSize', COAX_SIZE),
                               ('AmmoHitSizeAdjust', COAX_HIT))),
                        _local('MG3 7.62 mm', 'MG3 7.62mm 機関銃', 'MG3 7.62mm 機槍', 'MG3 7.62mm 机枪', 'MG3 7.62mm 기관총'),
                        key=FIRE_SECONDARY),
    'QVULCAN_AP': Store('Quad M61 AP', 'gun', 0.0, 0.0, Shell('V603_FLAK_GUNH01_DLC_L.SGO', 'SolidBullet01', 'ap'),
                        _local('Quad M61 AP', '4連装 M61 徹甲弾', '四聯 M61 穿甲彈', '四联 M61 穿甲弹', '4연장 M61 철갑탄'),
                        group=4, barrel=0),
    'QVULCAN_AP_R': Store('Quad M61 AP', 'gun', 0.0, 0.0, Shell('V603_FLAK_GUNH01_DLC_R.SGO', 'SolidBullet01', 'ap'),
                          _local('Quad M61 AP', '4連装 M61 徹甲弾', '四聯 M61 穿甲彈', '四联 M61 穿甲弹', '4연장 M61 철갑탄'),
                          group=4, barrel=1),
    'BMPT_ATGM': Store('9M120 Ataka', 'ground', 0.0, 0.0,
                       Missile('9M120 Ataka', Look('bullet_icbm01', 1.83), burn=12.0, top=550.0, accel=180.0,
                               max_g=20.0, nav=3.0, life=12.0, damage=1200.0, blast=6.0,
                               lock_range=1800.0, lock_cone=0.3, lock_time=30.0),
                       _local('9M120 Ataka', '9M120 アターカ', '9M120 阿塔卡', '9M120 阿塔卡', '9M120 아타카'),
                       key=FIRE_SECONDARY),
    # The BMPT's second belt: its 2A42s are dual-feed guns, HE-FRAG-T (the native belt, tools/make_tracked_combat GUNS)
    # and the 3UBR8 APDS-T the gunner switches to against armour. Two barrels, one choice (group 3, as the flak's);
    # its numbers are make_tracked_combat's (the native gun's own file and muzzles, BMPT_AP_BELT), this template only
    # names the round's family.
    'BMPT_AP': Store('30 mm APDS', 'gun', 0.0, 0.0, Shell('V603_FLAK_GUNH01_DLC_L.SGO', 'SolidBullet01', 'ap'),
                     _local('30 mm APDS (3UBR8)', '30mm APDS（3UBR8）', '30mm 脫殼穿甲彈（3UBR8）', '30mm 脱壳穿甲弹（3UBR8）',
                            '30mm 분리철갑탄（3UBR8）'), group=3, barrel=0),
    'BMPT_AP_R': Store('30 mm APDS', 'gun', 0.0, 0.0, Shell('V603_FLAK_GUNH01_DLC_R.SGO', 'SolidBullet01', 'ap'),
                       _local('30 mm APDS (3UBR8)', '30mm APDS（3UBR8）', '30mm 脫殼穿甲彈（3UBR8）', '30mm 脱壳穿甲弹（3UBR8）',
                              '30mm 분리철갑탄（3UBR8）'), group=3, barrel=1),
    # The Ataka's thermobaric round (9M120F: the same tube and flight, a thermobaric warhead against infantry and
    # cover): half the tandem warhead's damage on one target, more than twice its blast. Its own look (the rounds one
    # control cycles never look alike: tools/selftest.py store_looks).
    'BMPT_ATGMF': Store('9M120F Ataka', 'ground', 0.0, 0.0,
                        Missile('9M120F Ataka', Look('airtortoise_missile', 1.83), burn=12.0, top=550.0, accel=180.0,
                                max_g=20.0, nav=3.0, life=12.0, damage=600.0, blast=14.0,
                                lock_range=1800.0, lock_cone=0.3, lock_time=30.0),
                        _local('9M120F Ataka (thermobaric)', '9M120F アターカ（サーモバリック）', '9M120F 阿塔卡（溫壓）',
                               '9M120F 阿塔卡（温压）', '9M120F 아타카（열압력）'),
                        key=FIRE_SECONDARY),
    # A tank's / an IFV's coaxial machine gun (COAX_MOUNTS above): fired on its own trigger (src/payload.cpp: the
    # secondary trigger, the main gun staying loaded). The stock Epsilon gunner's machine gun (V_403TANK_MACHINEGUN:
    # SolidBullet01, 6 a round: the lightest round of any stock vehicle gun, a rifle round's; its own tank machine gun
    # sound) with the real gun's rate, muzzle velocity and reach, a red-orange tracer, an orange flash, a tight cone,
    # its ready belt changed in 10 s; the muzzle on the flash hider of its own model (coax_model). Its holder is on the
    # elevating bone (no recoil travel with the main gun); recoilless on the hull (make_stock_stores._params).
    **{f'MG{tank}': _coax_store(tank, mount) for tank, mount in COAX_MOUNTS.items()},
}
JET_MISSILE_STOCK, JET_BOMB_STOCK = JET_MISSILE_STOCK, 'V_409HELI_BOMB01.SGO'


@dataclass(frozen=True)
class Plume:
    """How a missile store's round looks in flight (the user, 2026-11-26: 「所有导弹、炮弹之类都换得了。抄抄战雷、皇牌空战之类的
    游戏」; docs/feature-projectile-look.md): Ace Combat's rail drop, white-hot motor and long white trail, on the stock
    class's own fields (docs/missile-re.md, MissileBullet01):
      - `ignite`: frames an ejected round falls clear before its motor lights (CP[7][0]; CP[7][1] 1: the launcher's
        velocity kept while it drops); 0 for a rail or tube launch, lit at once (PLUMES). The drop itself is src/missile.cpp kDrop (AmmoGravityFactor stays 0: its readers take it for the
        whole flight); the motor's flame and trail emitters open at ignition (0x26AA02).
      - `trail`: frames a trail node lives (CP[2], a single int: each node 0.9..1.1 of it, 0x268D31; the trail stays
        where it was laid, it does not drift).
      - `colour`: AmmoColor, both the trail's start colour (rgb as given, fading to alpha 0) and the motor's flame
        ((r*4, g*2, b, a), 0x268AEB): over 1 a brighter, whiter trail and a white-hot flame. The trail's and the flame's
        widths are AmmoSize's (the round's model at its real length, Look), not ours to set."""
    ignite: float
    trail: float
    colour: tuple[float, float, float, float]

    def __post_init__(self) -> None:
        if not (0.0 <= self.ignite <= PLUME_IGNITE_MOST and self.trail > 0.0 and len(self.colour) == 4):
            raise ValueError(f'bad plume {self}')


PLUME_IGNITE_MOST = 20.0   # frames: src/missile.cpp neither guides nor fuses-by-proximity it before ignition
# When the motor lights is the real launcher's (the user, 2026-11-26: 「导弹不一定全是下坠的吧……至少应该和现实一样吧，有些可能
#直接射出去」): a rail launcher (LAU-7 / LAU-127 / LAU-117 / M299, a rocket pod, a tube) lights the motor on the rail and
# the round flies straight off it; a hot-launch cell or canister (Mk 41 for the ESSM, the PAC-3's, the 9M311's tube)
# lights in it; only an ejector launcher (the AIM-54's LAU-93 under an F-14) kicks the round clear to fall before it
# lights: AAM_L alone drops. The stock helicopter missile's: ignite 0, trail 300, (1, 1, 1, 0.5).
_HOT = (1.5, 1.4, 1.3, 0.85)       # flame (6.0, 2.8, 1.3): white-hot; trail a bright white
_SAM = (1.6, 1.5, 1.4, 0.9)
_ATGM = (1.2, 1.1, 1.0, 0.6)       # flame (4.8, 2.2, 1.0), a thinner grey trail
_ROCKET = (1.25, 1.15, 1.0, 0.7)
PLUMES: dict[str, Plume] = {
    'AAM_S': Plume(0.0, 420.0, _HOT), 'AAM_M': Plume(0.0, 450.0, _HOT), 'AAM_L': Plume(14.0, 480.0, _HOT),
    'AGM': Plume(0.0, 300.0, _HOT), 'AGM_L': Plume(0.0, 240.0, _HOT),
    'ESSM': Plume(0.0, 420.0, _SAM), 'TUNGUSKA_SAM': Plume(0.0, 360.0, _SAM), 'APOC_SAM': Plume(0.0, 420.0, _SAM),
    'PATRIOT_SAM': Plume(0.0, 480.0, _SAM),
    'GLM': Plume(0.0, 180.0, _ATGM), 'BMPT_ATGM': Plume(0.0, 180.0, _ATGM), 'BMPT_ATGMF': Plume(0.0, 180.0, _ATGM),
    'RKT': Plume(0.0, 150.0, _ROCKET),
    'SAZABI_MISSILE': Plume(0.0, 240.0, _HOT),   # SAZABI_MISSILE below: from the shield's launcher, lit
}


def plume_of(store: Store) -> Plume | None:
    """The store's Plume (PLUMES by its STORES key, or the Sazabi's), None: the template's look (the salvo's own)."""
    kind = next((k for k, s in STORES.items() if s is store), None)
    if kind is None and store is globals().get('SAZABI_MISSILE'):
        kind = 'SAZABI_MISSILE'
    return PLUMES.get(kind) if kind else None
SALVO_ROUNDS = 256
SALVO_CHILDREN = 4
# The salvo's rhythm (src/salvo_volley.h kVolley / kVolleyGap, src/lockon.cpp ShotHook): the plugin fires SALVO_VOLLEY
# rounds a shot, round n off rail n % 4 (all four rails, twice), and the burst waits FireBurstInterval =
# SALVO_VOLLEY_GAP frames between volleys: 32 volleys, the last 93 frames (1.55 s) after the first. One lock starts it;
# the rounds go over every enemy locked, one each first, the rest evenly.
SALVO_VOLLEY = 8
SALVO_VOLLEY_GAP = 3
SALVO_MARK = 7070.0
SALVO_JET = 'edf6tr_jet_salvo_interceptor_mission'
SALVO_MODEL = 'EDF6VC_SALVO_INTERCEPTOR.MRAB'
SALVO_OBJECT = 'EDF6VC_JET_SALVO_INTERCEPTOR.SGO'
# The Arsenal Bird's two modes (pylib/salvo_model.py, the model pack's arsenal_bird folder): the missile mode is the salvo
# (SALVO_JET, in SALVO_MODEL); the drone mode the carrier ARSENAL_CARRIER_JET in the same model (src/jet_internal.h
# Body::arsenalCarrier: the carrier kind's arms, durability and AI under a mark of its own), launching M101s (M101_JET,
# Body::m101Drone: the gun drone's arms, durability and AI in the M101's model). Both only with the model folder.
ARSENAL_CARRIER_MARK = 7071.0
ARSENAL_CARRIER_JET = 'edf6tr_jet_arsenal_carrier_mission'
ARSENAL_CARRIER_OBJECT = 'EDF6VC_JET_ARSENAL_CARRIER.SGO'
M101_MARK = 7072.0
M101_JET = 'edf6tr_jet_m101'
M101_MODEL = 'EDF6VC_M101.MRAB'
M101_OBJECT = 'EDF6VC_JET_M101.SGO'
# The salvo's launch, after Ace Combat's multi-missile volleys (the user, 2026-11-26: 「参照别的游戏做导弹发射效果…类似王牌空战」):
# the rounds leave the four rails in volleys (SALVO_VOLLEY every SALVO_VOLLEY_GAP frames), drop clear of the launcher for a beat, light, fan out, then
# turn onto their locks off the nose; each splits into four bright children. The carrier is MissileBullet02
# (docs/missile-re.md §MissileBullet02): its CP[7] [frames, factor] the drop before ignition (the launcher's velocity
# times factor each frame plus AmmoGravityFactor's gravity; own speed AmmoSpeed and no trail), then the motor (own
# speed + CP[4] a frame to CP[6], the inherited velocity x0.9 a frame and no gravity).
# Then after Ace Combat 7's Arsenal Bird (the user, 2026-11-26: 「天顶截击机 要 飞行要塞武库鸟 那种…发射的导弹感觉不对劲，发光也
# 不对劲」; its rhythm only, none of its assets): a cold launch -- the rounds are let go dark, drift down clear of the jet
# for 0.4 s with no flame, no smoke and no glow, then the motor lights bright and lays a long white trail.
SALVO_LOCK_CONE = 1.40          # rad, LockonAngle both halves: about 80 deg off the nose (front hemisphere only, 0x22DF30)
SALVO_EJECT = 0.15              # m a frame off the rail (AmmoSpeed): let go, not pushed (9 m/s: it hangs by the jet)
SALVO_DROP = (24.0, 1.0)        # CP[7]: 0.4 s dark before the motor lights, the launcher's speed kept
SALVO_DROP_GRAVITY = 4.0        # AmmoGravityFactor, felt only before ignition: ~4.7 m of drop, 22 m/s down when it lights
SALVO_FAN = 0.3                 # rad, FireAccuracy: the rounds leave in a 17 deg cone (polar angle uniform in [0, it])
SALVO_ACCEL = 0.27              # m a frame per frame (CP[4]): own speed SALVO_CHILD_SPEED at the split
SALVO_TOP = 12.0                # m a frame (CP[6])
SALVO_TURN = 0.1                # rad a frame (CP[5], the helicopter template's own): the PN's limit |own| x it a frame
SALVO_HOMING_DELAY = 8.0        # frames after ignition before it steers (CP[8]): the fan opens first
SALVO_SPLIT_AFTER = 40.0        # frames after ignition it splits (CP[12] = [1, it]: by time, the target alive or not)
SALVO_SPLIT_CONE = 0.45         # rad, CP[13][2]: the children's cone off the carrier's heading
SALVO_CHILD_SPEED = 11.0        # m a frame the children leave at (CP[15][5]): the carrier's own speed at the split
SALVO_CHILD_ACCEL = 0.15        # their CP[4]
SALVO_CHILD_TURN = 0.15         # their CP[5] (the plugin's PN: lateral acceleration at most |own| x it a frame)
SALVO_CHILD_TOP = 14.0          # their CP[6]
SALVO_CHILD_HOMING_DELAY = 6.0  # their CP[8]: a second, smaller fan before they turn
SALVO_CHILD_SPREAD = (0.9, 1.0)  # their CP[10]: top speed x U[0.9, 1], turn x U[1, 1] (MissileBullet01 ctor 0x267910)
# AmmoColor: the smoke trail (噴射煙_01.dds) takes it as is, the head flare (Particle3_k.dds) as (r x4, g x2, b)
# (ctor 0x26BEA4). Just over 1 like the other missiles' _HOT: an orange-white flame (5.0, 2.4, 1.15) and a thick white
# smoke, not the old blue HDR trail (3, 3.2, 4), whose 300 frames of glowing smoke sat over the launch like a light.
SALVO_COLOUR = (1.25, 1.2, 1.15, 0.8)
SALVO_CHILD_COLOUR = (1.15, 1.1, 1.05, 0.55)    # CP[15][12]: the children's AmmoColor, a thinner trail
# The smoke trails' life (CP[2], frames, each node x0.9..1.1): long white smoke hanging in the sky after the rain.
SALVO_TRAIL = 360.0
SALVO_CHILD_TRAIL = 240.0
# The rounds' own glow (docs/round-glow.md, drawn by the plugin while each is alive with its motor lit): (r, g, b,
# intensity, size m, back m). Small and orange-white on the flame, set back along the nose so it hugs the motor
# instead of lighting the whole round; the round's model untouched.
SALVO_GLOW = (1.0, 0.75, 0.45, 5.0, 1.0, 0.5)
SALVO_CHILD_GLOW = (1.0, 0.8, 0.5, 3.5, 0.6, 0.3)
STORES['SALVO'] = Store(
    'Salvo cluster missile', 'air', 16.0, 0.002,
    replace(STORES['AAM_L'].weapon, model='Salvo missile', look=Look('bullet_icbm01', 1.6),
            damage=180.0, blast=4.0, lock_range=4200.0, lock_cone=SALVO_LOCK_CONE, lock_time=1.0,
            accel=SALVO_ACCEL * 3600.0, top=SALVO_TOP * 60.0, eject=SALVO_EJECT, spread=SALVO_FAN,
            burst=float(SALVO_ROUNDS), burst_gap=float(SALVO_VOLLEY_GAP), interval=float(SALVO_ROUNDS)),
    _local('Salvo missile', '斉射ミサイル', '齊射飛彈', '齐射导弹', '일제 사격 미사일'),
    per_pylon=SALVO_ROUNDS // 4, pylon_mass=110.0, pylon_drag=0.015)

def xm_store(unit: xm_units.Unit, arm: xm_units.Arm) -> str:
    """The STORES kind of an X-Morph unit's arm (xm_units.Arm): <UNIT KEY>_<ARM KEY>."""
    return f'{unit.key}_{arm.key}'.upper()


def _xm_arm(arm: xm_units.Arm) -> Store:
    names = _local(*arm.names)
    if arm.missile:
        m = dict(arm.missile)
        weapon: Missile | Shell = Missile(**{**m, 'look': Look(*m['look'])})
    else:
        weapon = Shell(*arm.shell, extra=tuple(arm.numbers.items()))
    turret = Turret(*xm_units.turret_bones(arm.key), **arm.turret) if arm.turret else None
    return Store(arm.names[0], arm.role, 0.0, 0.0, weapon, names, key=arm.fire, auto=arm.auto, seat=arm.seat, turret=turret)


# The X-Morph units' other weapons (pylib/xm_units.py Arm): each on its own fire key of its unit (Arm.key).
STORES.update({xm_store(u, a): _xm_arm(a) for u in xm_units.v603() for a in u.arms})


@dataclass(frozen=True)
class Mounting:
    """The control a stock vehicle's independent weapon is on (STOCK_MOUNTS): its own fire key (Store.key). No auto turret:
    a stock vehicle's mount has no turret of its own to lay (Store.auto)."""
    key: int

    def __post_init__(self):
        if self.key not in FIRE_KEYS:
            raise ValueError(f'a mounting needs a fire key of FIRE_KEYS, not {self.key}')


# Each independent weapon of the stock vehicles the plugin arms (the user, 2026-10-30: every weapon that is a mount of its
# own, not another round of the same gun, on a fire key of its own, a switchable auto turret, or a gunner seat; table
# driven, never per vehicle in code): vehicle category (tools/make_stock_stores.py CATEGORIES) -> store kind -> its
# control. A kind left out rides the primary's cycle (a gun's other rounds, a launcher's other rounds: the 402's and the
# helicopters' pylons). The stock models carry no turret bones of the plugin's: none is a gunner seat (Store.seat), and
# only the V603 reads its second trigger nowhere (key 2). A mounted kind is a kind of its own (mounted()), so the vehicles
# that share the kind elsewhere keep its control there.
STOCK_MOUNTS: dict[str, dict[str, Mounting]] = {
    # The Grape's launcher beside its autocannon, and its coaxial gun: the Car's stock input reads the second trigger
    # (seat +0x2E0) as its throttle (0x65A548), where the tanks' coax is (src/payload.cpp FreeSecondary), so a key of its
    # own (pad RB: VehicleFire4Button; the Car leaves RB unread, docs/stock-payload-re.md §2.1; the player, 2026-11-08:
    # 「you cannot move forward unless you keep firing the coaxial machine gun」).
    'ifv': {'AGM_L': Mounting(3), 'MG401': Mounting(4)},
    'railgun': {'AGM_L': Mounting(3)},          # the 403's missiles beside its rail gun
    'superheavy': {'AGM_L': Mounting(3)},       # the 404's missiles beside the driver's front gatling
    'flak': {'AAM_S': Mounting(FIRE_SECONDARY),   # the Kepler's SAM launcher: one launcher's two rounds on the
             'AAM_M': Mounting(FIRE_SECONDARY)},  # second trigger (T switches them)
    'bike': {'RKT': Mounting(3)},               # the bike's rocket pod beside its guns
}


def mounted(kind: str, how: Mounting) -> str:
    """The store kind `kind` as a vehicle mounts it on `how` (STOCK_MOUNTS): <KIND>_K<key>."""
    return f'{kind}_K{how.key}'


STORES.update({mounted(k, how): replace(STORES[k], key=how.key)
               for mounts in STOCK_MOUNTS.values() for k, how in mounts.items()})


def _xm_jet_store(arm: xm_units.Arm) -> Store:
    like, over = arm.store
    s = STORES[like]
    return replace(s, name=arm.names[0], weapon=replace(s.weapon, **over), names=_local(*arm.names))


# The X-Morph jets' stores (xm_units Unit.air 'stores'): each made like one of the jets' stores (Arm.store: its mass, drag
# and look), the game's damage, blast and cadence over it.
STORES.update({xm_store(u, a): _xm_jet_store(a) for u in xm_units.ported('jet') for a in u.air['stores']})



# The fuel tank's place in a jet's weapon list and holders: fourth (index 3, or last with fewer), among the four the
# 506 builds without the plugin's loop patch (src/stores.cpp), so it is always built; the stores past it are the
# plugin's alone.
FUEL_AT = 3


def with_fuel(weapons: list, fuel) -> list:
    at = min(FUEL_AT, len(weapons))
    return list(weapons[:at]) + [fuel] + list(weapons[at:])


def tier_of(name: str) -> tuple[float, float]:
    """A jet's tier (Jet.tier, else JET_TIER): mission_setup[0], and its vehicle request's multipliers."""
    return JETS[name].tier or JET_TIER


def store_file(kind: str, rounds: int) -> str:
    """A store's weapon SGO for a load of `rounds` (AmmoCount is the weapon's, so each load is its own file)."""
    return f'EDF6VC_{kind}_{rounds}.SGO'


def _weapon(name: str) -> str:
    return 'app:/weapon/' + name.lower()


_STORE_FILE = re.compile(r'app:/weapon/edf6vc_([a-z0-9_]+?)_(\d+)\.sgo$')


def store_of(weapon: str) -> tuple[str, int] | None:
    """(kind, rounds) of a store weapon path (store_file), else None."""
    m = _STORE_FILE.match(weapon.lower())
    if not m or m.group(1).upper() not in STORES:
        return None
    return m.group(1).upper(), int(m.group(2))


def _load(*stores: tuple[str, int]) -> tuple[str, ...]:
    """The guns, then the stores in the order the cockpit cycles them."""
    for kind, _ in stores:
        if kind not in STORES:
            raise ValueError(kind)
    return _GUNS + tuple(_weapon(store_file(kind, n)) for kind, n in stores)


# Real loads (one jet type each, src/jet_internal.h roles): an air-superiority fighter's 4 AIM-120 + 2 AIM-9X; an
# interceptor's AIM-54 / AIM-120 / AIM-9X; a multirole fighter swinging both ways; a strike fighter's Mavericks and
# Mk 82s with two AIM-9X for itself; a drone's Hellfires; the carriers fire nothing (their drones do), they keep a
# fighter's load so every jet has the 506's four holders at least (src/stores.cpp: without the plugin the game builds
# just four).
_FIGHTER = _load(('AAM_M', 4), ('AAM_S', 2))
_INTERCEPTOR = _load(('AAM_L', 4), ('AAM_M', 2), ('AAM_S', 2))
_SALVO = _load(('SALVO', SALVO_ROUNDS))
_MULTIROLE = _load(('AAM_M', 2), ('AAM_S', 2), ('AGM', 2), ('RKT', 19), ('MK82', 4))
_STRIKE = _load(('AGM', 6), ('RKT', 38), ('MK82', 6), ('AAM_S', 2))
_DRONE = _load(('AGM_L', 4))
_SHIP = _load(('ESSM', 32))
# The X-Morph jets' (XM_JETS): their gun pair (the jets' gatling with the game's numbers, XM_GUN_FILES; a unit with no gun
# the jets' own guns), then their stores.
XM_GUN_FILES: dict[str, tuple[str, str]] = {f'EDF6VC_{u.key.upper()}_GUN_{s}.SGO': (stock, u.key)
                                            for u in xm_units.ported('jet') if u.air['gun']
                                            for s, stock in zip('LR', JET_GUN_FILES.values())}
XM_LOADS: dict[str, tuple[str, ...]] = {
    u.key: (tuple(_weapon(f) for f, (_s, k) in XM_GUN_FILES.items() if k == u.key) or _GUNS)
    + tuple(_weapon(store_file(xm_store(u, a), a.rounds)) for a in u.air['stores'])
    for u in xm_units.ported('jet')}
HOMING_WEAPONS = tuple(w for jet_weapons in (_FIGHTER, _INTERCEPTOR, _SALVO, _MULTIROLE, _STRIKE, _DRONE, _SHIP)
                       for w in jet_weapons if store_of(w) and STORES[store_of(w)[0]].role in ('air', 'ground'))
_ARMS = _FIGHTER
# The blast drones' charge (src/jet.cpp Detonate: weapon 2, fired by 0x2021 once next to the enemy): the
# 409's unguided bomb (GrenadeBullet01) made a point charge (docs/decoy-blast-re.md 1.4): CP#0 = 1 bursts
# when its life runs out (0x26543E), CP#3 = 0 no bounce, CP#5 = 0 no random life; it barely moves, lives
# JET_BLAST_ALIVE frames, so it goes off where the drone is. One round, one shot. (damage, radius m).
JET_BLAST_STOCK = 'V_409HELI_BOMB01.SGO'
JET_BLAST_FILES: dict[str, tuple[float, float]] = {
    'EDF6VC_BLAST_CHARGE.SGO': (1200.0, 15.0),   # the blast drone: fast, many
    'EDF6VC_DOLL_CHARGE.SGO': (3000.0, 25.0),    # the doll drone: slow, draws the enemy in first
}
JET_BLAST_ALIVE = 2.0
_BLAST = tuple('app:/weapon/' + f.lower() for f in JET_BLAST_FILES)
# The support calls' aircraft (the support deployment's: tools/calls.py EDF6VC_CALL_STRIKE .. MULTIROLE_F and their sorties;
# src/jet_internal.h kLaunchRows, Body::supStrike .. supMultirole): each its kind's durability and arms (JETS
# 'edf6tr_jet_<role>_mission') in the airframe of that kind the model pack has, a Dreadnought fighter's model
# (pylib/dreadnought_model.py; the player 及开, 2026-11-07: 「飞机的模型换成对应的模型」, docs/feature-support-jet-models.md):
# the strike jet the Dreadnought bomber (ground attack), the fighter its fighter, the interceptor its interceptor, the
# multirole its defender. Each under a mark of its own (a mark names one body's model: its masses, stores, boarding),
# crewed as the kinds are (a Dreadnought fighter of the hangar's is crewless); built by tools/make_dreadnought.py with the
# model pack's folder, or not at all (that support is then not deployed: src/jet_spawn.cpp SupportAircraftReady).
# role -> (JETS name, mark, Dreadnought model, the kind it is: its JETS name and mark).
SUPPORT_JETS: dict[str, tuple[str, float, str, str, float]] = {
    'strike': ('edf6tr_dn_support_strike', 7061.0, 'bomber', 'edf6tr_jet_strike_mission', 7001.0),
    'fighter': ('edf6tr_dn_support_fighter', 7062.0, 'fighter', 'edf6tr_jet_fighter_mission', 7002.0),
    'interceptor': ('edf6tr_dn_support_interceptor', 7063.0, 'interceptor', 'edf6tr_jet_interceptor_mission', 7003.0),
    'multirole': ('edf6tr_dn_support_multirole', 7064.0, 'defender', 'edf6tr_jet_multirole_mission', 7004.0),
}


def support_file(role: str) -> str:
    """A support aircraft's SGO (SUPPORT_JETS; tools/make_dreadnought.py writes it, src/jet_internal.h kBodies names it)."""
    return f'EDF6VC_DN_SUPPORT_{role.upper()}.SGO'


# kg: a jet's mass without stores, by its mark (src/stores.inc kJetMasses: what its stores' mass is weighed against, and
# the mass a player-flown aircraft rams with, src/playerjet.cpp RamDamage; src/support_loadout.h payloadKg 40 % of it).
# Every jet mark has one (tools/selftest.py jet_masses_cover_every_jet). Each its real prototype's (the user, 2026-11-26:
# 「重量全得重新做，全都还原一下」, by the real types; table: docs/feature-support-jet-models.md §8), all in one measure:
# empty weight + full internal fuel, no external stores (they are weighed on top of it: Burden), crew left out; fuel
# given as a volume at 0.8 kg/L; rounded to 100 kg. A role's prototype (strike, fighter, interceptor, multirole, the
# gunship / transport) whatever stock model it wears; a body with none (sci-fi, X-Morph, the drones) the nearest real
# aircraft its size, unscaled. Figures: Wikipedia's specifications sections unless said.
JET_MASSES = {
    7001.0: 21600.0,    # strike: F-15E, 15,694 kg empty + 5,858 kg internal fuel
    7002.0: 19300.0,    # fighter: F-15C, 13,154 + 6,103
    7003.0: 27200.0,    # interceptor (AIM-54s): F-14D, 19,838 + 7,348
    7004.0: 21200.0,    # multirole: F/A-18E, 14,552 + 6,668
    # the carriers (V508 x 1.6, 59.4 x 17.0 x 76.8 m): the C-5M its size (67.9 x 19.8 x 75.3 m), 172,365 + 154,880
    7005.0: 327200.0, 7009.0: 327200.0, 7010.0: 327200.0,
    # the drones (pd607 x 3, 3.5 x 2.3 x 5.7 m; the blast / doll drones the same airframe): the BQM-167A its size
    # (3.4 x 1.2 x 6.1 m), 313 kg empty + ~435 L of fuel (Kratos' sheet; not a confirmed figure)
    7006.0: 700.0, 7007.0: 700.0, 7008.0: 700.0,
    # the gunship (tools/make_jets.py GUNSHIP_MARK): the C-130J airframe, 34,274 + 6,700 US gal (25,360 L); the AC-130J's
    # guns and mission kit have no published weight (its stores weigh on top)
    7011.0: 54600.0,
    7020.0: 25800.0,    # the enemy fighter: Su-27SK, 16,380 + 9,400
    # the Primers' fighter (19.5 m across, its fuselage 5.9 x 3.2 x 10.9 m: an F-16's box): F-16C Block 50, 8,573 + 3,200
    7030.0: 11800.0,
    7201.0: 19300.0, 7202.0: 21600.0,   # the player's fighter and strike jet: their kinds' (7002, 7001)
    7401.0: 71200.0,   # the Sazabi (not an aircraft): its canonical full weight (30.5 t empty)
    # the Dreadnought's hangar fighters: the same models as the support's (SUPPORT_JETS), so their roles' prototypes:
    # the fighter F-15C, the defender F/A-18E, the bomber F-15E, the interceptor F-14D; the Dreadnought (a ship, not an
    # aircraft): a supercarrier's displacement (~100 kt)
    7041.0: 19300.0, 7042.0: 21200.0, 7043.0: 21600.0, 7044.0: 27200.0, 7601.0: 1.0e8}
# ...the support aircraft weigh what their kinds do (SUPPORT_JETS: the same arms, the same payload; src/support_loadout.h
# kVehicleBodies payloadKg)
JET_MASSES.update({mark: JET_MASSES[kind_mark] for _n, mark, _m, _k, kind_mark in SUPPORT_JETS.values()})
# ...the Arsenal Bird, both modes (106.6 x 14.9 x 22.8 m, salvo_model): the Stratolaunch its size (117 m across, 73 m
# long, 15 m high). Wikipedia gives no fuel figure: its 589,670 kg maximum takeoff weight less its 250,000 kg external
# payload, 339,670 kg, is the empty aircraft (226,796 kg) with the fuel it takes off with
JET_MASSES[SALVO_MARK] = JET_MASSES[ARSENAL_CARRIER_MARK] = 339700.0
# ...and its M101 drone (3.2 x 1.5 x 5.7 m, scaled to the gun drone's length: tools/prep_arsenal_bird.py): the drones'
JET_MASSES[M101_MARK] = JET_MASSES[7006.0]
# ...and the support's C-130s (C130_JETS): the AC-130 the gunship's (C130_GUNSHIP_MARK), the paratroop plane the same
# airframe's (C130_TRANSPORT_MARK): the C-130J's
C130_GUNSHIP_MARK = 7065.0
C130_TRANSPORT_MARK = 7066.0
JET_MASSES[C130_GUNSHIP_MARK] = JET_MASSES[C130_TRANSPORT_MARK] = JET_MASSES[7011.0]
# ...and the support's air carriers in the model pack's Helicarrier (HELICARRIER_JETS, 46.0 x 16.4 x 80.0 m: the C-5M
# its size too): the carriers' mass (the carrier kind's mark -> the Helicarrier's)
HELICARRIER_MARKS = {7005.0: 7067.0, 7009.0: 7068.0, 7010.0: 7069.0}
JET_MASSES.update({mark: JET_MASSES[kind] for kind, mark in HELICARRIER_MARKS.items()})
# ...and the X-Morph jets' (pylib/xm_units.py Unit.air 'mass': small airframes, a light jet's to a medium bomber's)
JET_MASSES.update({u.air['mark']: u.air['mass'] for u in xm_units.ported('jet')})
# The Primer creatures (src/primer.cpp, docs/primer-plan.md): enemies. Their guns are the 506 gatling made a glowing
# round (name -> damage, frames between rounds, m a frame, frames of life, round size, blast radius, colour, spread,
# the share of the world's gravity it falls at). Damage is per round against the player (the stock gatling's is 10,
# 20 a second). src/primer.cpp's reaches and the stinger's lob (kStingSpeed, kGravityLob) are these numbers.
PRIMER_GUN_STOCK = 'V_506HELI_GATLING01_L.SGO'
PRIMER_GUN_FILES: dict[str, tuple[float, float, float, float, float, float, tuple[float, float, float, float], float, float]] = {
    # the dragonfly's needles: fast thin violet rounds, 15 a second, 360 m
    'EDF6VC_PRIMER_NEEDLE.SGO': (8.0, 4.0, 12.0, 30.0, 0.4, 0.0, (2.2, 0.6, 3.4, 1.0), 0.01, 0.0),
    # a centipede head's spit: a slow green glob with a small blast, about one a second, 420 m
    'EDF6VC_PRIMER_SPIT.SGO': (30.0, 45.0, 3.5, 120.0, 1.2, 4.0, (0.9, 2.4, 0.5, 1.0), 0.03, 0.0),
    # a middle segment's barbs: thin amber darts from its back, 3 a second, 320 m
    'EDF6VC_PRIMER_BARB.SGO': (6.0, 20.0, 8.0, 40.0, 0.35, 0.0, (2.4, 1.6, 0.4, 1.0), 0.03, 0.0),
    # the tail's stinger: a heavy round lobbed on a high arc (90 m/s, falling at the world's 14.7 m/s^2: up to
    # 550 m, 12 s up and down), a blast where it comes down, one every 2 s
    'EDF6VC_PRIMER_STING.SGO': (60.0, 120.0, 1.5, 720.0, 1.0, 6.0, (2.6, 0.8, 0.3, 1.0), 0.02, 1.0),
}
_PRIMER_NEEDLE, _PRIMER_SPIT, _PRIMER_BARB, _PRIMER_STING = ('app:/weapon/' + f.lower() for f in PRIMER_GUN_FILES)
# What a creature sounds like (a Jet's `creature`): the sound bank its cues are in (added to game_sound), the 506's
# heli_se_table rows (index -> SEPRESET cue: 3 hit, 4 crash, 6-8 the explosions: which of those its death plays is not
# known, so all three; 5, the rotor crash, is JET_HELI_SE_ROWS' silence as for every jet), and ragdoll_contact's cue.
CREATURE_SOUNDS: dict[str, tuple[str, dict[int, str], str]] = {
    # the giant ant's (GIANTANT01.SGO ant_DamageEffectSe / ant_BloodSe / ant_DeadSe)
    'centipede': ('app:/sound/adx/tikyuu4_en_GiantAnt.acb',
                  {3: '巨大蟻ヒットエフェクト', 4: '巨大蟻衝突', 6: '敵共通血しぶき大', 7: '敵共通血しぶき小',
                   8: '巨大蟻死亡'}, '巨大蟻衝突'),
    # the giant bee's (GIANTBEE01.SGO bee_DamageEffectSe / bee_DamageSe / bee_DeadBloodSe / bee_DeadSe)
    'dragonfly': ('app:/sound/adx/tikyuu4_en_GiantBee.acb',
                  {3: '蜂ヒットエフェクト', 4: '蜂ダメージ', 6: '敵共通血しぶき小', 7: '敵共通血しぶき小',
                   8: '蜂死亡'}, '蜂ダメージ'),
}
STORE_FILES = tuple(sorted({w.split('/')[-1].upper() for w in (*_FIGHTER, *_INTERCEPTOR, *_SALVO, *_MULTIROLE, *_STRIKE, *_DRONE, *_SHIP,
                                                                 *(w for load in XM_LOADS.values() for w in load))
                            if store_of(w)}))
JET_WEAPON_FILES = (*JET_GUN_FILES, *JET_BLAST_FILES, *STORE_FILES, *PRIMER_GUN_FILES, *XM_GUN_FILES)
# A derived weapon's stock file: in a vehicle's weapon list it takes the stock one's per-weapon parameters.
_STOCK_OF = {'app:/weapon/' + d.lower(): 'app:/weapon/' + st.lower()
             for d, st in (*JET_GUN_FILES.items(), *((f, PRIMER_GUN_STOCK) for f in PRIMER_GUN_FILES),
                           *((f, stock) for f, (stock, _k) in XM_GUN_FILES.items()),
                           *((f, JET_BOMB_STOCK if STORES[store_of(_weapon(f))[0]].role == 'bomb' else JET_MISSILE_STOCK)
                             for f in STORE_FILES))}
# jet_sgo: the seat of an aircraft the player boards. vehicle_riding_position[i][3] is not a pose: it is the seat's
# name key in the text table (0x7A3500; 505_TANK_DRIVER and 506_HELI_DRIVER both read "Pilot Seat"); the rider's pose
# is the seat locator's own MAB parameters (Animation / DispCharacter, 0x62B62D), the 506's: rider hidden
# (docs/all-classes-ride-re.md). Every class also takes every stock seat at run time (src/allclass.cpp).
PLAYER_SEAT_POSE, PLAYER_SEAT_CLASSES = '505_TANK_DRIVER', 15
# The marks of the plugin's aircraft the player boards (src/playerjet_kinds.h kBoardable, its bodies' marks in
# src/jet_internal.h kBodies; tools/selftest.py boardable_seats_take_every_class holds them together): every SGO of one
# takes every class into its seat, the NPC body's too (the user, 2026-10-07: the carrier an NPC called took Rangers and Air
# Raiders only), whatever brings it: the NPC jet, its parked and requested twins, the airstrike bombers and the gunship
# made from the strike jet (7001), and the Dreadnought's fighters (7041-7044). The enemy's, the Primer creatures' and the
# submarine carrier keep the 506's seat.
BOARDABLE_MARKS = frozenset({7001.0, 7002.0, 7003.0, 7004.0, 7005.0, 7006.0, 7007.0, 7008.0, 7009.0, 7010.0, 7011.0,
                             7041.0, 7042.0, 7043.0, 7044.0, *(u.air['mark'] for u in xm_units.ported('jet')),
                             *(mark for _n, mark, *_r in SUPPORT_JETS.values()), C130_GUNSHIP_MARK,
                             C130_TRANSPORT_MARK, *HELICARRIER_MARKS.values()})


@dataclass(frozen=True)
class GroundVehicle:
    """A ground vehicle the player requests (tools/calls.py Call.ground), built by its own tool: its stock vehicle SGO
    (the class and everything it does not change), the stock request it is requested like (the transport and the
    vehicle setup's layout: tools/call_weapons.py vehicle_sgo; its family's levels set the request's multipliers),
    the weapons its setup carries, and its base durability."""
    sgo: str              # Mods/OBJECT file, without extension
    stock: str            # Root.cpk OBJECT SGO it is made from, without extension
    request: str          # the stock request weapon it is requested like (its WEAPONTABLE row is the template)
    family: tuple[str, ...]   # that request and its stronger versions: (level, multipliers) for request_tier
    weapons: tuple[str, ...]
    durability: float
    tool: str             # the tool that writes it (tools/<tool>.py)
    setup: tuple[float, ...] | None = None  # native vehicle params when its request template is another vehicle class
    # What each of `weapons` fires from ('left' / 'right': a gun barrel, the holder on that side's moving barrel bone;
    # 'missiles': the launcher), in order; empty: the generator's own layout (tools/make_tracked_combat.py slot).
    slots: tuple[str, ...] = ()
    # The left / right barrels fire in turn, not together (src/twin_cadence.h): each 'left' weapon is paired with the
    # 'right' one of the same belt (the k-th left with the k-th right in `weapons`); tools/gen_stores.py kTwinBarrels.
    alternate: bool = False
    # The setup entry holding the weapon list where it is not the last (the walkers' requests, the Begaruta's and the
    # Proteus's: [multipliers, params, params, [seat params], weapons, [x]]); there one entry a holder of the host's,
    # '' in `weapons` an empty one ([0.0]). None: the last entry (tools/call_weapons.py vehicle_sgo).
    weapons_at: int | None = None
    # The OBJECT/<model>.MRAB the vehicle draws from when it is another vehicle's of the same tool (the Andryusha's is
    # the Katyusha's: one archive, two SGOs); '' its own, OBJECT/<sgo>.MRAB.
    model: str = ''


def vehicle_fire_keys(vehicle: GroundVehicle) -> dict[int, list[str]]:
    """A vehicle's own fire keys (Store.key): each key its store kinds, in its weapons' order (a kind once: its
    left / right files are one weapon). ValueError for an extra key (3..6) with two weapons on it: only the second trigger
    has a cycle (one launcher's rounds, VehicleSecondarySwitchKey)."""
    out: dict[int, list[str]] = {}
    for w in vehicle.weapons:
        store = store_of(w)
        if store and STORES[store[0]].key and store[0] not in out.get(STORES[store[0]].key, []):
            out.setdefault(STORES[store[0]].key, []).append(store[0])
    for key, kinds in out.items():
        if key != FIRE_SECONDARY and len(kinds) > 1:
            raise ValueError(f'{vehicle.sgo}: fire key {key} has {kinds}: one weapon an extra key')
    return dict(sorted(out.items()))


# A ground vehicle's own camera (game_object_camera_setting, docs/camera-re.md): [0] the point it looks at, [1] the
# eye, both points in the vehicle's frame (metres; x left-right, y up, z forward, from the vehicle's origin): the eye is
# a position of its own, not an offset from [0]. The stock ground vehicles look level from 4 m (Naegling (0, 4, 0) /
# (0, 4, -10.5), Kepler (0, 4, 0) / (0, 4, -15.5)); the artillery's (the user, 2026-10-05: "the view is too low")
# looks down past its front onto the ground ahead: ARTILLERY_VIEW_DOWN deg down, the line of sight reaching the ground
# ARTILLERY_VIEW_GROUND m ahead of the origin. The plugin's high view (src/highcam.cpp) is the second mode over these.
ARTILLERY_VIEW_DOWN = (8.0, 20.0)
ARTILLERY_VIEW_GROUND = (15.0, 45.0)


def twin_barrels() -> list[tuple[str, int]]:
    """The weapon files whose barrels fire in turn (GroundVehicle.alternate) with their pair number (from 1, one per
    belt): a vehicle's k-th 'left' slot paired with its k-th 'right' (src/twin_cadence.h, tools/gen_stores.py)."""
    out: list[tuple[str, int]] = []
    pair = 0
    for vehicle in GROUND_VEHICLES.values():
        if not vehicle.alternate:
            continue
        if len(vehicle.slots) != len(vehicle.weapons):
            raise ValueError(f'{vehicle.sgo}: alternate barrels need a slot for every weapon')
        sides = {side: [w.rsplit('/', 1)[1].upper() for w, s in zip(vehicle.weapons, vehicle.slots) if s == side]
                 for side in ('left', 'right')}
        if not sides['left'] or len(sides['left']) != len(sides['right']):
            raise ValueError(f'{vehicle.sgo}: alternate barrels need a right for every left')
        for left, right in zip(sides['left'], sides['right']):
            pair += 1
            out += [(left, pair), (right, pair)]
    if len({name for name, _ in out}) != len(out):
        raise ValueError('a weapon file is in two alternating pairs')
    return out


def camera_view(camera: tuple[list[float], list[float]]) -> dict[str, float]:
    """What a game_object_camera_setting ([0] look-at, [1] eye, in the vehicle's frame) shows: the line of sight's
    pitch down (deg), where it meets the vehicle's ground plane (y 0; m ahead of the origin), the eye's height and its
    distance from the look-at point."""
    look, eye = camera
    assert look[0] == eye[0] == 0.0 and look[2] > eye[2], camera
    run, drop = look[2] - eye[2], eye[1] - look[1]
    ground = eye[2] + eye[1] * run / drop if drop > 0.0 else math.inf
    return {'down': math.degrees(math.atan2(drop, run)), 'ground': ground, 'height': eye[1], 'arm': math.hypot(run, drop)}


def check_artillery_camera(camera: tuple[list[float], list[float]]) -> dict[str, float]:
    """The artillery's raised camera looks down onto the ground ahead (ARTILLERY_VIEW_DOWN, ARTILLERY_VIEW_GROUND),
    from over the stock 4 m. Its numbers."""
    out = camera_view(camera)
    assert ARTILLERY_VIEW_DOWN[0] <= out['down'] <= ARTILLERY_VIEW_DOWN[1], f'the view is not tilted onto the ground ahead: {out}'
    assert ARTILLERY_VIEW_GROUND[0] <= out['ground'] <= ARTILLERY_VIEW_GROUND[1], f'the view meets the ground too near / far: {out}'
    assert out['height'] > 4.0, f'the eye is no higher than the stock: {out}'
    return out


# The Sazabi (JETS[SAZABI_JET]): its file names are pylib/sazabi_model.py's (checked by tools/make_sazabi.py check).
SAZABI_JET = 'edf6tr_sazabi_mission'
SAZABI_MARK = 7401.0
SAZABI_DURABILITY = 9000.0
_SAZABI_ARC, _SAZABI_MDB = 'EDF6VC_SAZABI.MRAB', 'edf6vc_sazabi.mdb'
# Its arms (the rifle, the shield missiles, the rifle again: see sazabi_weapons), on its muzzle bones.
SAZABI_WEAPONS = ('app:/weapon/edf6vc_sz_rifle.sgo', 'app:/weapon/edf6vc_sz_missile.sgo', 'app:/weapon/edf6vc_sz_rifle.sgo')
SAZABI_WEAPON_BONES = ('sz_muzzle', 'sz_missile', 'sz_muzzle')
SAZABI_CAMERA = (0.0, 26.0, -48.0)                 # over the 25.6 m mech's shoulder, far enough back to see it whole
# Riding (the MAB's eye and LookTarget, model frame: x left, y up, z forward): behind and over its right shoulder (the
# funnel packs reach 25.5 m up and the tubes 15 m back), far enough back that the whole mech is in view, feet and all (the
# simulated riding view, tools/sazabi_pose_view.py cam, 2026-10-07), the ground ahead above it.
SAZABI_SEAT_CAMERA = ((-8.0, 28.0, -52.0), (0.0, 13.0, 10.0))

# The Dreadnought (JETS[DREADNOUGHT_JET], src/dreadnought.cpp, docs/dreadnought-plan.md): the Dreadnought game's Jupiter
# Arms heavy dreadnought, 500 m bow to stern, the player pilots; its archives are pylib/dreadnought_model.py's (built
# from the model folder by tools/make_dreadnought.py, which checks these names). Its bow gun on its prow's muzzle bone,
# its missiles in their cells amidships (dreadnought_model.SHIP_MUZZLES). Marks 7601-7699 are its own (src/body506.cpp).
DREADNOUGHT_JET = 'edf6tr_dreadnought_mission'
DREADNOUGHT_MARK = 7601.0
DREADNOUGHT_DURABILITY = 60000.0
_DREADNOUGHT_ARC, _DREADNOUGHT_MDB = 'EDF6VC_DN_DREADNOUGHT.MRAB', 'edf6vc_dn_dreadnought.mdb'
DREADNOUGHT_WEAPON_BONES = ('dn_muzzle', 'dn_muzzle', 'dn_missile')
# Its turrets (the user, 2026-10-21: 「船的炮塔是可以上人的」): a gunner seat each after the pilot's (seat 1 + n: turret n,
# its bones dn_turret_<n> / _gun / _muzzle, tools/prep_dreadnought.py), the plugin turning it and firing from its muzzle.
DREADNOUGHT_TURRETS = 8
# Each turret's gun (tools/prep_dreadnought.py: turret_0 / 1 the SecMid, 2-5 the SecShort, 6 / 7 the SecLong), fired as the
# fixed emplacement made of it fires (the user, 2026-10-25: 「无畏舰的炮塔攻击方式太抽象了，换成做好的固定炮台的那种吧」;
# pylib/emplacement_model.py KINDS, tools/make_tracked_combat.py GUNS): its round (tools/make_dreadnought.py TURRET_ROUNDS),
# its two barrels in turn; the barrels' half spacing in m (across its guns, either side of its muzzle bone), measured on the
# model's guns (tools/make_dreadnought.py check_barrels). src/dreadnought_turrets.inc kGuns / kGunOf hold the same.
DREADNOUGHT_TURRET_GUNS = ('emp_mid',) * 2 + ('emp_short',) * 4 + ('emp_long',) * 2
DREADNOUGHT_BARRELS = {'emp_mid': 2.46, 'emp_short': 4.07, 'emp_long': 5.7}
# Its collision box's centre over its keel (heli_rigid_body[0][1]: the model's whole box, the model standing on its keel):
# the vehicle's position is that centre (src/dreadnought.cpp kKeel; tools/make_dreadnought.py check holds the SGO to it).
DREADNOUGHT_KEEL = 55.25
DREADNOUGHT_CAMERA = (0.0, 160.0, -640.0)            # behind and over a 500 m ship, all of it in view
# Riding: over its stern, looking along its deck at the horizon ahead (model frame: x left, y up, z forward).
DREADNOUGHT_SEAT_CAMERA = ((0.0, 190.0, -560.0), (0.0, 80.0, 200.0))
# Its hangar's fighters (src/dreadnought_hangar.cpp; src/jet_internal.h kBodies Body::dn*): NPC jets in the Dreadnought
# game's fighters' models, crewless (its hangar launches them), boardable (src/playerjet_kinds.h kBoardable).
# kind -> (JETS name, mark, durability, arms); each one's SGO is EDF6VC_DN_<KIND>.SGO, its model EDF6VC_DN_<KIND>.MRAB.
DREADNOUGHT_FIGHTERS: dict[str, tuple[str, float, float, tuple[str, ...]]] = {
    'fighter': ('edf6tr_dn_fighter_mission', 7041.0, 1100.0, _FIGHTER),
    'defender': ('edf6tr_dn_defender_mission', 7042.0, 1300.0, _FIGHTER),
    'bomber': ('edf6tr_dn_bomber_mission', 7043.0, 1600.0, _STRIKE),
    'interceptor': ('edf6tr_dn_interceptor_mission', 7044.0, 900.0, _INTERCEPTOR),
}


def dn_file(kind: str, ext: str) -> str:
    """A Dreadnought model's file: its archive (MRAB, pylib/dreadnought_model.py archive_name) or SGO."""
    return f'EDF6VC_DN_{kind.upper()}.{ext}'


# The Katyusha (tools/make_katyusha.py): a rocket truck on the Naegling's class (Vehicle402_Rocket: its turret, its
# wheels), the V607 truck under the Naegling's rack; its rockets are lobbed on the high arc and the EDF6AutoTurret
# plugin aims them (LockonTargetType kMarkKatyusha); EDF6VehicleCrew shows its rider where they land.
KATYUSHA_ROCKETS = 'EDF6VC_KATYUSHA_ROCKETS.SGO'
# The Andryusha (test site #37, docs/andryusha-re.md; tools/make_katyusha.py builds it with the Katyusha): the same truck
# and model, fewer rockets, each an incendiary cluster carrier src/andryusha.cpp splits (ANDRYUSHA_BOMBLET_FILES).
ANDRYUSHA_ROCKETS = 'EDF6VC_ANDRYUSHA_ROCKETS.SGO'
GROUND_VEHICLES: dict[str, GroundVehicle] = {
    'sedan': GroundVehicle('EDF6VC_SEDAN', 'V512_KEITRUCK', 'EWEAPON401',
                           ('EWEAPON401',), (), 300., 'make_sedan', (.8, 30., .0075, .7, .075, .3, .6)),
    # The aura trucks (tools/make_aura_vehicles.py, docs/feature-aura-vehicles.md): the stock kei truck, unarmed, its
    # request the sedan's (the V512's handling).
    **{key: GroundVehicle(f'EDF6VC_{key.upper()}', 'V512_KEITRUCK', 'EWEAPON401', ('EWEAPON401',), (), 1000.,
                          'make_aura_vehicles', (.8, 30., .0075, .7, .075, .3, .6))
       for key in ('aura_cmd', 'aura_rep', 'aura_amo')},
    'katyusha': GroundVehicle('EDF6VC_KATYUSHA', 'VEHICLE402_ROCKET', 'EWEAPON401',
                              ('EWEAPON401', 'EWEAPON405', 'EWEAPON409', 'EWEAPON412', 'EWEAPON417'),
                              ('app:/weapon/' + KATYUSHA_ROCKETS.lower(),), 350.0, 'make_katyusha'),
    'andryusha': GroundVehicle('EDF6VC_ANDRYUSHA', 'VEHICLE402_ROCKET', 'EWEAPON401',
                               ('EWEAPON401', 'EWEAPON405', 'EWEAPON409', 'EWEAPON412', 'EWEAPON417'),
                               ('app:/weapon/' + ANDRYUSHA_ROCKETS.lower(),), 350.0, 'make_katyusha',
                               model='EDF6VC_KATYUSHA'),
    # The self-propelled artillery (tools/make_artillery.py): the Kepler's class (Vehicle603_Flak: a turret, twin guns),
    # the user's twin-gun tank model (an E551 hull, a twin-barrel turret; pylib/artillery_model.py); two large shells a
    # salvo, lobbed, aimed by EDF6AutoTurret.
    'artillery': GroundVehicle('EDF6VC_ARTILLERY', 'V603_FLAK', 'AWEAPON346',
                               ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                               ('app:/weapon/edf6vc_howitzer_l.sgo', 'app:/weapon/edf6vc_howitzer_r.sgo'), 600.0,
                               'make_artillery'),
    'vulcan': GroundVehicle('EDF6VC_VULCAN', 'V603_FLAK', 'AWEAPON346',
                            ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                            ('app:/weapon/edf6vc_vulcan_he_2000.sgo', 'app:/weapon/edf6vc_vulcan_ap_2000.sgo'),
                            800.0, 'make_vulcan'),
    'bmpt': GroundVehicle('EDF6VC_BMPT', 'V603_FLAK', 'AWEAPON346',
                          ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                          ('app:/weapon/edf6vc_bmpt_l.sgo', 'app:/weapon/edf6vc_bmpt_r.sgo',
                           'app:/weapon/edf6vc_bmpt_atgm_4.sgo',
                           # its dual-feed guns' APDS belt (one choice for both barrels) and the Ataka's thermobaric
                           # round (2026-10-19; tools/make_tracked_combat.py BMPT_AP_BELT, STORES BMPT_AP / BMPT_ATGMF)
                           'app:/weapon/edf6vc_bmpt_ap_150.sgo', 'app:/weapon/edf6vc_bmpt_ap_r_150.sgo',
                           'app:/weapon/edf6vc_bmpt_atgmf_4.sgo'), 1600.0, 'make_bmpt',
                          # the native guns' sides, the missile tubes, the second belt's sides on the same moving
                          # barrels, the thermobaric missile on the same tubes; the 2A42 pair fires in turn (2026-10-21)
                          slots=('left', 'right', 'missiles', 'left', 'right', 'missiles'), alternate=True),
    # The quad Vulcan (tools/make_quadvulcan.py, pylib/quadvulcan_model.py; the user, 2026-10-24: 「火神炮做一个四管的」
    # 「我说的四管是4联装转轮机枪」): the M163 upgraded to four M61s on its one cradle. Its HE is the native pair (each
    # holder a column of two guns), its AP the second loadout on the same columns; both columns fire every frame.
    'quadvulcan': GroundVehicle('EDF6VC_QUADVULCAN', 'V603_FLAK', 'AWEAPON346',
                                ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                                ('app:/weapon/edf6vc_quadvulcan_l.sgo', 'app:/weapon/edf6vc_quadvulcan_r.sgo',
                                 'app:/weapon/edf6vc_qvulcan_ap_1000.sgo', 'app:/weapon/edf6vc_qvulcan_ap_r_1000.sgo'),
                                1100.0, 'make_quadvulcan', slots=('left', 'right', 'left', 'right')),
    # The fixed emplacements (tools/make_emp_*.py, pylib/emplacement_model.py; the user, 2026-10-24: 「再做一类在地图上
    # 固定的炮台。这个炮台使用无畏舰上面那几个」): the Dreadnought's secondary turrets, each on a base, the Kepler's class
    # (its turret and guns laid natively) that the plugin never lets drive (src/emplacement.cpp). Requested like the
    # Kepler; each twin gun's barrels fire in turn.
    **{kind: GroundVehicle(f'EDF6VC_{kind.upper()}', 'V603_FLAK', 'AWEAPON346',
                           ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                           (f'app:/weapon/edf6vc_{kind}_l.sgo', f'app:/weapon/edf6vc_{kind}_r.sgo'), durability,
                           f'make_{kind}', slots=('left', 'right'), alternate=True)
       for kind, durability in (('emp_mid', 3000.0), ('emp_short', 3600.0), ('emp_long', 3300.0),
                                ('ciws_phalanx', 1800.0), ('flak88', 1500.0), ('laser', 1600.0),
                                ('railgun', 2400.0))},
    # The Patriot (tools/make_patriot.py, models/emp_patriot): PAC-2 salvoes from the native pair, in turn, and the
    # PAC-3 from the same tubes (the BMPT's missile holder on the turret).
    'patriot': GroundVehicle('EDF6VC_PATRIOT', 'V603_FLAK', 'AWEAPON346',
                             ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                             ('app:/weapon/edf6vc_patriot_l.sgo', 'app:/weapon/edf6vc_patriot_r.sgo',
                              'app:/weapon/edf6vc_patriot_sam_8.sgo'), 2000.0, 'make_patriot',
                             slots=('left', 'right', 'missiles'), alternate=True),
    # The GDF-005: its two guns together (not in turn).
    'gdf005': GroundVehicle('EDF6VC_GDF005', 'V603_FLAK', 'AWEAPON346',
                            ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                            ('app:/weapon/edf6vc_gdf005_l.sgo', 'app:/weapon/edf6vc_gdf005_r.sgo'), 1400.0,
                            'make_gdf005', slots=('left', 'right')),
    # The 2S6 Tunguska (tools/make_tunguska.py; its War Thunder model, models/wt_tunguska): two twin 30 mm guns (the
    # native pair, firing together) and the 9M311 missiles beside them (the BMPT's missile holder on the turret).
    'tunguska': GroundVehicle('EDF6VC_TUNGUSKA', 'V603_FLAK', 'AWEAPON346',
                              ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                              ('app:/weapon/edf6vc_tunguska_l.sgo', 'app:/weapon/edf6vc_tunguska_r.sgo',
                               'app:/weapon/edf6vc_tunguska_sam_4.sgo'), 1300.0, 'make_tunguska',
                              slots=('left', 'right', 'missiles')),
    # The PzH 2000 and the Sturmtiger (tools/make_pzh2000.py / make_sturmtiger.py; their War Thunder models): one gun
    # each, both native holders on it, in turn (tools/make_tracked_combat.py GUNS); the PzH 2000's roof MG3 on the
    # secondary (STORES PZH2000_MG), from the model's MG (muzzle slot 'mg').
    'pzh2000': GroundVehicle('EDF6VC_PZH2000', 'V603_FLAK', 'AWEAPON346',
                             ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                             ('app:/weapon/edf6vc_pzh2000_l.sgo', 'app:/weapon/edf6vc_pzh2000_r.sgo',
                              'app:/weapon/edf6vc_pzh2000_mg_1000.sgo'), 1500.0, 'make_pzh2000',
                             slots=('left', 'right', 'mg'), alternate=True),
    'sturmtiger': GroundVehicle('EDF6VC_STURMTIGER', 'V603_FLAK', 'AWEAPON346',
                                ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                                ('app:/weapon/edf6vc_sturmtiger_l.sgo', 'app:/weapon/edf6vc_sturmtiger_r.sgo'), 2200.0,
                                'make_sturmtiger', slots=('left', 'right'), alternate=True),
    # The Apocalypse (tools/make_apocalypse.py; ska-ara's Red Alert 2 model, models/wt_apocalypse): twin heavy guns firing
    # in turn (the native pair), the anti-air missiles of its pods (the BMPT's missile holder on the turret).
    'apocalypse': GroundVehicle('EDF6VC_APOCALYPSE', 'V603_FLAK', 'AWEAPON346',
                                ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                                ('app:/weapon/edf6vc_apocalypse_l.sgo', 'app:/weapon/edf6vc_apocalypse_r.sgo',
                                 'app:/weapon/edf6vc_apoc_sam_4.sgo'), 4500.0, 'make_apocalypse',
                                slots=('left', 'right', 'missiles'), alternate=True),
    # The TOS-1 (tools/make_tos1.py; models/wt_tos1): both native holders on its launcher pack, salvoes in turn.
    'tos1': GroundVehicle('EDF6VC_TOS1', 'V603_FLAK', 'AWEAPON346',
                          ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                          ('app:/weapon/edf6vc_tos1_l.sgo', 'app:/weapon/edf6vc_tos1_r.sgo'), 1600.0, 'make_tos1',
                          slots=('left', 'right'), alternate=True),
    # The drill tank (tools/make_drill.py, src/drill.cpp): the Blacker's class (Vehicle505_Tank, tracks, one weapon
    # holder) in EDF: Iron Rain's drill tank, requested like the Blacker E series; its one weapon fires nothing (the
    # drill is the plugin's: it spins it and bites with DRILL_CHARGE_FILE).
    'drill': GroundVehicle('EDF6VC_DRILL', 'V505_TANK', 'EWEAPON418',
                           ('EWEAPON418', 'EWEAPON421', 'EWEAPON425', 'EWEAPON428', 'EWEAPON433'),
                           ('app:/weapon/edf6vc_drill_bit.sgo',), 1400.0, 'make_drill'),
    # The sidecar motorcycle (tools/make_sidecar.py, src/sidecar.cpp): the Freed bike's class (Vehicle503_Bike) and
    # weapons (its rider's two machine guns, its fuel tank: the stock ones), a sidecar (a boat-shaped tub, its wheel
    # under a mudguard) built in the bike's own material (pylib/sidecar_model.py), requested like the Freed bikes (the Ranger's vehicle slot). Its gunner is
    # no seat's: the plugin holds a soldier standing in the tub, firing their own weapons.
    'sidecar': GroundVehicle('EDF6VC_SIDECAR', 'V503_BIKE', 'AWEAPON338',
                             ('AWEAPON338', 'AWEAPON339', 'AWEAPON341', 'AWEAPON343', 'AWEAPON345'),
                             ('app:/weapon/v_503_bike_gun_l.sgo', 'app:/weapon/v_503_bike_gun_r.sgo',
                              'app:/weapon/v_fuel01.sgo'), 300.0, 'make_sidecar'),
}
# The X-Morph: Defense units on the V603 (pylib/xm_units.py; one generator for them all, tools/make_xmorph.py): each its
# `like`'s native gun pair (the two V603 holders, in the `like`'s cadence) under its own weapon files, its own
# durability; the `like`'s stores (missiles, a second belt: their own store rows and muzzles) are not inherited.
# Its other weapons (xm_units.Arm: the game's missile pods, machine-gun turrets, EMP flak) after the pair: each a store
# (xm_store) on its own fire key (Arm.key: the second trigger, then the extra keys), fired from its own muzzles (slot
# xm_<key>), together with the pair and the other arms.
for _xm in xm_units.v603():
    _like = GROUND_VEHICLES[_xm.like]
    assert _like.stock == 'V603_FLAK' and _like.slots[:2] == ('left', 'right'), (_xm.key, _xm.like)
    GROUND_VEHICLES[_xm.key] = replace(
        _like, sgo=f'EDF6VC_{_xm.key.upper()}', durability=_xm.durability, tool='make_xmorph',
        slots=('left', 'right', *(f'xm_{a.key}' for a in _xm.arms)),
        alternate=_like.alternate if _xm.alternate is None else _xm.alternate,
        weapons=(f'app:/weapon/edf6vc_{_xm.key}_l.sgo', f'app:/weapon/edf6vc_{_xm.key}_r.sgo',
                 *(_weapon(store_file(xm_store(_xm, a), a.rounds)) for a in _xm.arms)))
# The X-Morph cars (pylib/xm_units.py car: pylib/car_model.py on the V512): the sedan's unarmed request (its `like`),
# under their own SGO and durability.
for _xm in xm_units.v512():
    _like = GROUND_VEHICLES[_xm.like]
    assert _like.stock == 'V512_KEITRUCK' and not _like.slots, (_xm.key, _xm.like)
    GROUND_VEHICLES[_xm.key] = replace(_like, sgo=f'EDF6VC_{_xm.key.upper()}', durability=_xm.durability,
                                       tool='make_xmorph')

# The X-Morph walkers (pylib/xm_walker.py; make_xmorph's builder): the host's class and request, the unit's weapons on
# the host's holders.
for _xm in xm_units.ported('mech'):
    _walker = xm_walker.WALKERS[_xm.key]
    GROUND_VEHICLES[_xm.key] = GroundVehicle(
        f'EDF6VC_{_xm.key.upper()}', _walker.stock, _walker.family[0], _walker.family, xm_walker.weapons(_xm.key),
        _xm.durability, 'make_xmorph', weapons_at=_walker.weapons_at)
# The walkers with what the test range calls each (testrange/gen.py: edf6tr_<kind>_mission, as TRACKED).
WALKERS: dict[str, str] = {_xm.key: xm_units.label(_xm) for _xm in xm_units.ported('mech')}

for _ground in GROUND_VEHICLES.values():
    vehicle_fire_keys(_ground)   # an extra fire key never holds two weapons
# The ground vehicles tools/make_tracked_combat.py builds on the V603 (each its make_<kind> tool, an X-Morph unit
# make_xmorph's builder; each its own ledger owner and build group): the War Thunder M163 / BMPT, the quad Vulcan, the
# fixed emplacements, the X-Morph units; with what the test range calls each (testrange/gen.py: edf6tr_<kind>_mission).
TRACKED: dict[str, str] = {
    'vulcan': 'M163 火神炮车', 'bmpt': 'BMPT 火力支援车', 'quadvulcan': '四联装火神防空车',
    'emp_mid': '无畏舰副炮炮台·中型', 'emp_short': '无畏舰副炮炮台·短管', 'emp_long': '无畏舰副炮炮台·长管',
    'tunguska': '2S6 通古斯卡防空车', 'pzh2000': 'PzH 2000 自行榴弹炮', 'sturmtiger': '突击虎',
    'ciws_phalanx': '密集阵近防炮', 'flak88': '88 毫米高射炮', 'apocalypse': '天启坦克', 'gdf005': 'GDF-005 双联高炮',
    'laser': '激光防空炮台', 'railgun': '电磁炮炮台', 'tos1': 'TOS-1 喷火兵火箭炮', 'patriot': '爱国者防空导弹',
    **{u.key: xm_units.label(u) for u in xm_units.v603()},
}
# The drill tank's bite (src/jet_bay.cpp kDrillChargeFile, DrillCharge): an impact charge (tools/make_jets.py
# impact_charge) with a blast of DRILL_CHARGE_RADIUS m (3 m or more: the stock path that lets a blast break buildings,
# docs/drill-re.md §3), DRILL_CHARGE_SPEED m a frame for DRILL_CHARGE_LIFE frames: from the drill's base it reaches
# what the drill touches (an enemy's lock point within ~6 m) and bursts there; meeting nothing it is gone without a burst.
DRILL_CHARGE_FILE = 'EDF6VC_DRILL_CHARGE.SGO'
DRILL_CHARGE_RADIUS, DRILL_CHARGE_SPEED, DRILL_CHARGE_LIFE = 4.0, 2.5, 4
# The 506's heli_se_table rows a jet must not play (the user, 2026-11-26: 「喷气机的音效怎么用的螺旋桨的…把原版的砍掉吧」),
# each with the SEPRESET name V506_HELI.SGO holds there (jet_sgo checks them). Its 16 rows (docs/sound-re.md §3):
# 0 ヘリ起動アイドリング, 1 ヘリローター, 2 ヘリ着陸, 3 ヘリダメージ, 4 ヘリ衝突, 5 ヘリローター衝突, 6-8 ヘリ爆発小/極小/大,
# 9 ヘリ搭乗, 10 ヘリ降車, 11-13 ビークル水中移動/水中移動停止/水中落ちる, 14-15 ビークル燃料切れ警告/燃料切れ.
# Silenced: the rotor's (0 start-up and idle, 1 its loop, 5 the rotor striking something) and what only a helicopter
# does (2 landing, 9 boarding, 10 leaving). Kept: what any vehicle makes (3 hit, 4 crash, 6-8 explosions; 11-15 are
# the shared ビークル cues: water and fuel). A name SEPRESET.SGO does not hold makes the game's preset empty (0x7B16F0
# returns false, its cue none) and playing it does nothing (0x7B4510); the plugin plays the engine (src/jetsound.cpp).
JET_SILENT_SE = 'EDF6VC_SILENT'
JET_HELI_SE: dict[int, str] = {0: 'ヘリ起動アイドリング', 1: 'ヘリローター', 2: 'ヘリ着陸', 5: 'ヘリローター衝突', 9: 'ヘリ搭乗',
                               10: 'ヘリ降車'}
JET_HELI_SE_ROWS = tuple(JET_HELI_SE)
# Model sizes and boxes: pylib/jet_models.py (bind-pose vertices after scaling).
JETS: dict[str, Jet] = {
    'edf6tr_jet_strike_mission': Jet(7001.0, 1500.0, _STRIKE),
    'edf6tr_jet_fighter_mission': Jet(7002.0, 1000.0, _FIGHTER),
    # bomber501_2 (dark paint) with elevons, x 0.65: 16 m across
    'edf6tr_jet_interceptor_mission': Jet(7003.0, 900.0, _INTERCEPTOR, ('app:/object/edf6vc_interceptor.mrab', 'bomber501_2.mdb'),
                                          'EDF6VC_INTERCEPTOR.MRAB', 'bomber501'),
    SALVO_JET: Jet(SALVO_MARK, 1250.0, _SALVO, (f'app:/object/{SALVO_MODEL.lower()}', 'edf6vc_salvo_interceptor.mdb'),
                   SALVO_MODEL, 'body', 'body'),
    # the Arsenal Bird's drone mode: the carrier kind's durability and arms (its drones do the fighting) in the same model
    ARSENAL_CARRIER_JET: Jet(ARSENAL_CARRIER_MARK, 8000.0, _ARMS,
                             (f'app:/object/{SALVO_MODEL.lower()}', 'edf6vc_salvo_interceptor.mdb'), SALVO_MODEL, 'body',
                             'body'),
    # ...and the M101 it launches: the gun drone's durability and arms (edf6tr_jet_drone) in the M101's model
    M101_JET: Jet(M101_MARK, 300.0, _DRONE, (f'app:/object/{M101_MODEL.lower()}', 'edf6vc_m101.mdb'), M101_MODEL, 'body',
                  'body'),
    # The enemy fighter (src/jet_internal.h kBodies Body::enemyFighter): the interceptor's model, a fighter's arms; the plugin
    # puts it on the enemy team on first sight, so it fights the player and their jets.
    'edf6tr_jet_enemy_fighter_mission': Jet(7020.0, 900.0, _ARMS, ('app:/object/edf6vc_interceptor.mrab', 'bomber501_2.mdb'),
                                            'EDF6VC_INTERCEPTOR.MRAB', 'bomber501'),
    # The Primers' fighter (pylib/primer_fighter_model.py: a pod of their new ship, two of its hatch petals for flapping
    # wings, 19.5 m across): the enemy's (src/jet_internal.h Body::primerFighter, Role::primer); its box the fuselage.
    'edf6tr_jet_primer_fighter_mission': Jet(7030.0, 700.0, _load(('AAM_S', 2)),
                                             ('app:/object/edf6vc_primer_fighter.mrab', 'edf6vc_primer_fighter.mdb'),
                                             'EDF6VC_PRIMER_FIGHTER.MRAB', 'body', 'body', ((0.0, 1.619, 0.0), (2.97, 1.62, 5.43))),
    # bomber401 x 0.5: 26 m across
    'edf6tr_jet_multirole_mission': Jet(7004.0, 1300.0, _MULTIROLE, ('app:/object/edf6vc_multirole.mrab', 'bomber401.mdb'),
                                        'EDF6VC_MULTIROLE.MRAB', 'bomber401'),
    # the EDF transport x 1.6: 59 x 77 m; it never fires (its drones do)
    'edf6tr_jet_carrier_mission': Jet(7005.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                      'EDF6VC_CARRIER.MRAB', 'body'),
    # the same carrier sending blast / doll drones (src/jet.cpp kCarrierMarks)
    'edf6tr_jet_blast_carrier_mission': Jet(7009.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                            'EDF6VC_CARRIER.MRAB', 'body'),
    'edf6tr_jet_doll_carrier_mission': Jet(7010.0, 8000.0, _ARMS, ('app:/object/edf6vc_carrier.mrab', 'v508_transport.mdb'),
                                           'EDF6VC_CARRIER.MRAB', 'body'),
    # the airstrike drone x 3: 5.7 m long; only carriers launch it (tools/make_jets.py EDF6VC_JET_DRONE.SGO)
    'edf6tr_jet_drone': Jet(7006.0, 300.0, _DRONE, ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                            'EDF6VC_DRONE.MRAB', 'body', 'body'),
    # Blast and doll drones (src/jet.cpp Role::blast / doll): the drone with a charge for its missile; only
    # the blast and doll carriers launch them (their guns never fire).
    'edf6tr_jet_blast': Jet(7007.0, 250.0, _GUNS + (_BLAST[0],), ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                            'EDF6VC_DRONE.MRAB', 'body', 'body'),
    'edf6tr_jet_doll': Jet(7008.0, 800.0, _GUNS + (_BLAST[1],), ('app:/object/edf6vc_drone.mrab', 'pd607_Drone_airstrike.mdb'),
                           'EDF6VC_DRONE.MRAB', 'body', 'body'),
    # The Primer creatures (src/primer.cpp, docs/primer-plan.md), enemies a mission places, in models of their own:
    # the centipede, one segment a creature (pylib/centipede_model.py: 3 m of plate, in the giant pill bug's archive
    # and chitin; its box the plate's middle 2.2 m, so that linked ones 3 m apart do not touch), its weapons by its
    # place in a chain: [0, 1] the spit on its head (veh+0x2020), [2] the stinger on its tail (+0x2021), [3] the barbs
    # on its back (their own trigger); and the dragonfly (pylib/dragonfly_model.py: 14 m long, 13.5 m across the
    # wings, in the gold drone's; its box the body, not the wings), three needle guns (src/stores.cpp: four holders
    # at least; primer.cpp fires the gun pair only).
    'edf6tr_centipede_mission': Jet(7012.0, 150.0, (_PRIMER_SPIT, _PRIMER_SPIT, _PRIMER_STING, _PRIMER_BARB),
                                    ('app:/object/edf6vc_centipede.mrab', 'e514_dango.mdb'), 'EDF6VC_CENTIPEDE.MRAB',
                                    'body', 'body', rigid=((0.0, 0.2, 0.0), (1.2, 0.6, 1.1)),
                                    weapon_bones=('head', 'head', 'sting', 'gun'), creature='centipede'),
    'edf6tr_dragonfly_mission': Jet(7013.0, 600.0, (_PRIMER_NEEDLE,) * 3,
                                    ('app:/object/edf6vc_dragonfly.mrab', 'e507_goldufo.mdb'), 'EDF6VC_DRAGONFLY.MRAB',
                                    'body', 'body', rigid=((0.0, -0.134, -1.32), (1.529, 1.454, 7.04)), creature='dragonfly'),
    # the submarine carrier (src/subcarrier.cpp, tools/make_sub.py, docs/subcarrier-re.md): the mission
    # object EV603_MARINE's model at its own size, 1664 m long; the box is the 30 m of hull under its main
    # deck (y 163.08..193.08 over the origin; the tower above is not solid). Not the whole hull: afloat its
    # keel is 340 m down and EDF's seas are some 30 m deep (M082, 2026-10-04): a hull box stuck in the seabed,
    # was pushed 215 m off its point and fought the ground every frame. Guns on its forward turrets' (left)
    # barrels, the missile on its missile bay.
    'edf6tr_sub_carrier_mission': Jet(7101.0, 30000.0, _SHIP, ('app:/object/edf6vc_sub.mrab', 'ev603_marine.mdb'),
                                      'EDF6VC_SUB.MRAB', 'body', 'body', rigid=((0.0, 178.08, -7.58), (121.0, 15.0, 832.0)),
                                      weapon_bones=('gunA_tilt_l', 'gunB_tilt_l', 'missle_l')),
    # Player jets (src/playerjet.cpp kKinds): the fighter in the interceptor's dark bomber501_2 (16 m across),
    # the strike jet in the elevon bomber (25 m across); empty until the player boards them.
    'edf6tr_pjet_fighter_mission': Jet(7201.0, 1400.0, _ARMS, ('app:/object/edf6vc_interceptor.mrab', 'bomber501_2.mdb'),
                                       'EDF6VC_INTERCEPTOR.MRAB', 'bomber501', player=True, camera=(0.0, 6.0, -24.0)),
    'edf6tr_pjet_strike_mission': Jet(7202.0, 2200.0, _STRIKE, player=True, camera=(0.0, 8.0, -32.0)),
    # The Sazabi (src/sazabi.cpp, docs/gundam-plan.md): a 25.6 m mobile suit the player pilots, on the V506 body like
    # the jets (the plugin walks and flies it and poses its bones); its own model (pylib/sazabi_model.py,
    # tools/make_sazabi.py) with the V506's own CAS, its weapons on its rifle bone. Marks 7401-7499 are its own (src/body506.cpp kMarks).
    SAZABI_JET: Jet(SAZABI_MARK, SAZABI_DURABILITY, SAZABI_WEAPONS, (f'app:/object/{_SAZABI_ARC.lower()}', _SAZABI_MDB),
                    _SAZABI_ARC, 'body', 'body', weapon_bones=SAZABI_WEAPON_BONES, player=True,
                    camera=SAZABI_CAMERA, seat_camera=SAZABI_SEAT_CAMERA, locators_on_origin=True),
    # The Dreadnought (src/dreadnought.cpp): a 500 m ship the player pilots, on the V506 body, flown by the plugin.
    DREADNOUGHT_JET: Jet(DREADNOUGHT_MARK, DREADNOUGHT_DURABILITY, _SHIP,
                         (f'app:/object/{_DREADNOUGHT_ARC.lower()}', _DREADNOUGHT_MDB), _DREADNOUGHT_ARC, 'body', 'body',
                         weapon_bones=DREADNOUGHT_WEAPON_BONES, player=True, camera=DREADNOUGHT_CAMERA,
                         seat_camera=DREADNOUGHT_SEAT_CAMERA, locators_on_origin=True),
    # Its fighters: NPC jets (the hangar launches them crewless) the player boards.
    **{name: Jet(mark, hp, arms, (f'app:/object/{dn_file(k, "MRAB").lower()}', f'edf6vc_dn_{k}.mdb'), dn_file(k, 'MRAB'),
                 'body', 'body')
       for k, (name, mark, hp, arms) in DREADNOUGHT_FIGHTERS.items()},
}
# The support aircraft (SUPPORT_JETS): their kinds' arms and durability in their Dreadnought models.
JETS.update({name: Jet(mark, JETS[kind].durability, JETS[kind].weapons,
                       (f'app:/object/{dn_file(model, "MRAB").lower()}', f'edf6vc_dn_{model}.mdb'), dn_file(model, 'MRAB'),
                       'body', 'body')
             for name, mark, model, kind, kind_mark in SUPPORT_JETS.values()})
assert all(JETS[kind].mark == kind_mark for _n, _m, _d, kind, kind_mark in SUPPORT_JETS.values())
# The NPC kinds the test range parks for the player (testrange/gen.py BOARDABLE_PARKED): each one's parked twin
# (Jet.parked), named after it: edf6tr_jet_<kind>_parked_mission. Same mark, model and arms: the plugin tells them
# apart from nothing (src/playerjet_kinds.h kBoardable goes by the mark).
PARKED_KINDS = ('edf6tr_jet_fighter_mission', 'edf6tr_jet_interceptor_mission', 'edf6tr_jet_strike_mission',
                'edf6tr_jet_multirole_mission', 'edf6tr_jet_carrier_mission', 'edf6tr_jet_blast_carrier_mission',
                'edf6tr_jet_doll_carrier_mission')


def parked_name(kind: str) -> str:
    """The parked twin's SGO name of the NPC jet `kind` (PARKED_KINDS)."""
    return kind.removesuffix('_mission') + '_parked_mission'


JETS.update({parked_name(k): replace(JETS[k], parked=True) for k in PARKED_KINDS})
# The gunship (src/jet_internal.h Body::gunship, mark 7011) as a JETS entry, for its requested twin only: the NPC gunship
# is tools/make_jets.py's (the strike jet in BOMBER401's model with GUNSHIP_MARK and a gunner seat). Its arms and
# durability the strike jet's (as tools/make_jets.py's), its model the stock bomber401 (52 m across, no gear: it stands
# on its belly), measured on that model when parked (box_model). It wears that model under a name of its own,
# GUNSHIP_MODEL_FILE (tools/make_jets.py writes it: pylib/jet_models.py STOCK_COPIES, the stock geometry skinned with its
# nozzle bones), so its seats' cockpits can be grafted into it; the NPC gunship (tools/make_jets.py GUNSHIP_FILE) too.
GUNSHIP_JET = 'edf6tr_jet_gunship'   # no _mission: the range never places it (testrange/gen.py)
GUNSHIP_MODEL_FILE = 'EDF6VC_GUNSHIP.MRAB'
GUNSHIP_MODEL = ('app:/object/' + GUNSHIP_MODEL_FILE.lower(), 'bomber401.mdb')
JETS[GUNSHIP_JET] = replace(JETS['edf6tr_jet_strike_mission'], mark=7011.0, model=GUNSHIP_MODEL,
                            body='bomber401', box_model='bomber401')
# The support's gunship and paratroop plane in the model pack's C-130s (pylib/c130_model.py, FlightGear's C-130 Hercules;
# the maintainer, 2026-11-10: 「没有模型来源的支援机型 直接去网站上拿呗」, docs/feature-support-jet-models.md §6): the AC-130
# (src/jet_internal.h Body::c130Gunship, C130_GUNSHIP_MARK: a mark of its own, so the plugin fires its rounds off this
# airframe's box, src/gunmuzzle.h kC130Gunship, and not the bomber401 requested twin's) and the C-130 (Body::transportPlane,
# C130_TRANSPORT_MARK: flown as a strike jet, its kind, but boarded as itself, not as the strike jet whose mark it had while
# it was the bomber401). Each the arms and durability of what it replaces (the gunship's, the strike jet's); built by
# tools/make_c130.py with the model pack's c130 folder, or not at all (that support is then not deployed).
# name -> (archive, model)
C130_JETS: dict[str, tuple[str, str]] = {'edf6tr_c130_gunship': ('EDF6VC_C130_GUNSHIP.MRAB', 'edf6vc_c130_gunship.mdb'),
                                         'edf6tr_c130_transport': ('EDF6VC_C130_TRANSPORT.MRAB', 'edf6vc_c130_transport.mdb')}
C130_GUNSHIP_JET, C130_TRANSPORT_JET = C130_JETS
JETS[C130_GUNSHIP_JET] = Jet(C130_GUNSHIP_MARK, JETS[GUNSHIP_JET].durability, JETS[GUNSHIP_JET].weapons,
                             ('app:/object/' + C130_JETS[C130_GUNSHIP_JET][0].lower(), C130_JETS[C130_GUNSHIP_JET][1]),
                             C130_JETS[C130_GUNSHIP_JET][0], 'body', 'body')
JETS[C130_TRANSPORT_JET] = Jet(C130_TRANSPORT_MARK, JETS['edf6tr_jet_strike_mission'].durability, JETS['edf6tr_jet_strike_mission'].weapons,
                               ('app:/object/' + C130_JETS[C130_TRANSPORT_JET][0].lower(), C130_JETS[C130_TRANSPORT_JET][1]),
                               C130_JETS[C130_TRANSPORT_JET][0], 'body', 'body')
# The support's air carrier and its blast / doll drone carriers in the model pack's Helicarrier (pylib/helicarrier_model.py,
# the Sketchfab "Helicarrier", CC BY 4.0; docs/feature-support-jet-models.md §6): src/jet_internal.h Body::supCarrier /
# supBlastCarrier / supDollCarrier under marks of their own (HELICARRIER_MARKS: the plugin picks the fans' reach over the
# ground by them, src/nacelle_reach.h), each the arms and durability of the carrier kind it replaces (its weapons on
# `body`: the model has no hardpoints; a carrier fires none, its drones do). One model for the three (as the V508 one);
# built by tools/make_helicarrier.py with the model pack's helicarrier folder, or not at all (those supports are then not
# deployed). The V508 carriers (7005, 7009, 7010) stay the requested / parked twins'.
# name -> the carrier kind it replaces
HELICARRIER_JETS: dict[str, str] = {'edf6tr_helicarrier': 'edf6tr_jet_carrier_mission',
                                    'edf6tr_helicarrier_blast': 'edf6tr_jet_blast_carrier_mission',
                                    'edf6tr_helicarrier_doll': 'edf6tr_jet_doll_carrier_mission'}
HELICARRIER_MODEL = ('app:/object/edf6vc_helicarrier.mrab', 'edf6vc_helicarrier.mdb')
for _name, _kind in HELICARRIER_JETS.items():
    JETS[_name] = Jet(HELICARRIER_MARKS[JETS[_kind].mark], JETS[_kind].durability, JETS[_kind].weapons, HELICARRIER_MODEL,
                      'EDF6VC_HELICARRIER.MRAB', 'body', 'body')
# The NPC kinds the Air Raider requests as empty aircraft to fly (tools/calls.py EDF6VC_CALL_FLY_*, the user, 2026-10-06:
# 「补上空袭的召唤飞机，空母载具」): every kind of src/playerjet_kinds.h kBoardable the player jets' requests do not already
# bring, each one's requested twin (parked: the whole plane's box, every class; requested: vehicle_setup). Left out on
# purpose (tools/selftest.py every_boardable_aircraft_requested): the strike jet (the player strike jet's request brings
# the same airframe, model and stores), the bomber takeover bodies (the airstrike's stock bombers, not ours to bring),
# the blast and doll drones (their one weapon is the charge that destroys them: the thrown drones bring that charge).
REQUEST_KINDS = ('edf6tr_jet_interceptor_mission', 'edf6tr_jet_fighter_mission', 'edf6tr_jet_multirole_mission', GUNSHIP_JET,
                 'edf6tr_jet_drone', 'edf6tr_jet_carrier_mission', 'edf6tr_jet_blast_carrier_mission',
                 'edf6tr_jet_doll_carrier_mission')


def request_name(kind: str) -> str:
    """The requested twin's SGO name of the NPC jet `kind` (REQUEST_KINDS)."""
    return kind.removesuffix('_mission') + '_request_mission'


JETS.update({request_name(k): replace(JETS[k], parked=True, requested=True) for k in REQUEST_KINDS})
# The X-Morph jets (pylib/xm_units.py family jet, docs/xmorph-port.md 「固定翼」): NPC kinds the plugin flies (src/jet_internal.h
# kBodies, their marks) in their own models (pylib/xm_air_model.py: EDF6VC_XM_<KEY>.MRAB and its CAS, the model pack's
# folder), each with its requested twin its request brings (tools/calls.py: Call.jet). Their gun pair on its holders
# xm_gun_l / xm_gun_r, each store on xm_<arm> (pylib/xm_jet.py holders: the game's muzzles). Built by tools/make_xmorph.py
# (the model folder's), so not in REQUEST_KINDS (tools/make_jets.py's).
XM_JETS: dict[str, str] = {xm_units.jet_name(u.key): u.key for u in xm_units.ported('jet')}
for _name, _key in XM_JETS.items():
    _u = xm_units.UNITS[_key]
    JETS[_name] = Jet(_u.air['mark'], _u.durability, XM_LOADS[_key],
                      (f'app:/object/edf6vc_{_key}.mrab', f'edf6vc_{_key}.mdb'), f'EDF6VC_{_key.upper()}.MRAB', 'body', 'body',
                      weapon_bones=('xm_gun_l', 'xm_gun_r', *(f'xm_{a.key}' for a in _u.air['stores'])),
                      cas=f'app:/object/edf6vc_{_key}.cas')
    JETS[request_name(_name)] = replace(JETS[_name], parked=True, requested=True)
    assert request_name(_name) == xm_units.jet_request(_key), _name
JET_MODEL = ['app:/object/bomber501.mrab', 'bomber501.mdb']
# The bomber with elevon bones (tools/make_jets.py writes it): the jets use it when it is installed.
JET_ELEVON_FILE = 'EDF6VC_JET.MRAB'
JET_ELEVON_MODEL = ['app:/object/edf6vc_jet.mrab', 'bomber501.mdb']
JET_ROOT_BONE = 'mdl'
# The V506 MAB block's locator parent names (UTF-16, block offsets), shortened in place to JET_ROOT_BONE:
# the bomber has only `mdl` and `bomber501` (docs/jet-model-re.md §1, §3.3).
JET_MAB_BONES = ((0x360, 'body'), (0x372, 'rotor'), (0x37E, 'tailRotor'))
# The fourth parent name, the root, stays: (0x36A, 'mdl') has no room for a longer name, so every jet model's
# root bone is JET_ROOT_BONE (pylib/jet_models.py renames the drone's `pd607_Drone_airstrike`). A model
# without it leaves the riding-position locators (vehicle_riding_position) without a parent: the vehicle
# init (0x62B430, from 0x629450) then reads a null locator (EDF+0x62B619) and CreateObject comes back with a
# half-made vehicle. (The crash at EDF+0x5F866C first put down to this is the dead effect's: see _dead_effect.)
JET_MAB_ROOT = (0x36A, JET_ROOT_BONE)
# Fuselage only (half extents; the 25 m wingspan left out so low passes do not scrape), centre as the model.
JET_RIGID_BODY = [[0.0, 0.34, 2.6], [2.0, 1.6, 13.0]]


def on_origin(box) -> list[list[float]]:
    """A collision box [centre, half extents] cut off at the body's origin: what is under it goes. The game puts a
    vehicle's origin on the ground where it spawns it (the stock 506's box is 0 - 2.9 m over it); a box reaching
    under it starts in the terrain, and the body falls through the terrain's mesh: the player jets' boxes (measured
    off their models, 0.83 m / 1.29 m under the origin) sank at their spawn (2026-10-04, the test range)."""
    (cx, cy, cz), (hx, hy, hz) = box
    bottom, top = cy - hy, cy + hy
    if bottom >= 0.0:
        return [list(box[0]), list(box[1])]
    return [[cx, top / 2.0, cz], [hx, top / 2.0, hz]]


# The boarding point (docs/player-jet-re.md §12). A seat's door is the MAB locator its vehicle_riding_position entry
# names first ([0], seat +0x1E0); CanRideSeat (0x6346D0) lets a human board when their position is within the locator's
# radius (record +0x10) plus DOOR_SLACK (the float at EDF+0x1C36990) of it (docs/rescue-re.md). The V506's door
# (「搭乗口１」) is (2.15, 0, 1.8) on `mdl`, radius 1.8: at the ground, 0.65 m inside the stock heli's box side (2.8 m) --
# a human against the hull is in reach. On a jet the same point is under the middle of the plane: the parked carrier's
# was 4.9 m in from its fuselage box's side at the ground (no prompt anywhere round it: 「空母缺少登机口」, 2026-10-05),
# a player jet's 10 m in from its whole-model box. move_door puts it where the stock heli has its own: on the ground,
# DOOR_OUT m outside the box's right side (+x, the V506's side), at the stock door's z (within the box's length).
# Native 652630 subtracts the rigid centre, then 62E7D0 -> 1100B90 sets mdl's world
# to that model origin. A door on the box bottom therefore has local y=cy-hy,
# not -hy. The old centre assumption buried the carrier's door by 8.516 m; the
# native callback/model/locator regression executes the full coordinate chain.
# The door is DOOR_OUT outside the accessible right side, at the stock z within
# its length. Its radius makes it reachable from DOOR_STEP across the ground, from the human's
# feet or HUMAN_HEIGHT over them (which of the two its position is, is not settled). The stock radius is never cut.
# With a mesh-derived compound, a high wing's full-span box no longer blocks a person under it. walkup_box moves
# that doorway beside the fuselage only after checking the entire approach against the actual collision cells.
DOOR_SLACK = 0.5      # EDF.dll 0x1C36990
DOOR_OUT = 0.6        # m: outside the box's side, where a human standing against it is
DOOR_STEP = 1.0       # m: across the ground from the door point, still in reach
HUMAN_HEIGHT = 1.0    # m: a human's position is at its feet or up to this over them
DOOR_MARGIN = 0.05    # m: of reach to spare
DOOR_HEADROOM = 2.5   # a standing armoured soldier, including clearance above the head
DOOR_HALF_WIDTH = 0.5 # walking corridor half width (not the locator's interaction radius)


@lru_cache(maxsize=32)
def walkup_box(game: Game, file: str | None, box: tuple) -> tuple:
    """Use the fuselage side if a person can actually walk under the outer wing.

    The flight collision's full span is not an entrance: the carrier's wingtip
    put its door 23 m from the hull. Keep the full box's frame/ground height, but
    measure access against the SAME conservative convex cells Havok receives.
    Low wings, nacelles or landing pods across the approach retain the outside
    door. No collision is removed to make this path pass.
    """
    import aircraft_collision as ac
    import jet_models
    if file not in jet_models.MODELS:
        return box
    hull = jet_models.fuselage_box(game, file)
    if hull[1][0] >= box[1][0]:
        return box
    x = box[0][0] + hull[1][0] + DOOR_OUT
    z = box[0][2] + min(1.8, box[1][2])
    floor = box[0][1] - box[1][1]
    cells = ac.mesh_cells(ac.triangles(jet_models._model_of(game, file)))
    for lo, hi in cells:
        if (hi[0] >= x - DOOR_HALF_WIDTH and lo[0] <= box[0][0] + box[1][0] + DOOR_OUT + DOOR_STEP
                and hi[2] >= z - DOOR_HALF_WIDTH and lo[2] <= z + DOOR_HALF_WIDTH
                and lo[1] < floor + DOOR_HEADROOM and hi[1] > floor):
            return box
    return box[0], (hull[1][0], box[1][1], box[1][2])


class DoorError(Exception):
    """A jet's boarding point is not where move_door puts it (check_door)."""


# The jets' hardpoints (pylib/jet_hardpoints.py; the user, 2026-10-19: 「在地面停着的飞机射导弹，会在地底生成」): the model
# files that carry the bones HP_GUN / HP_STORE (jet_models makes them), and which of a jet's weapons hangs on which.
# Without them every weapon hung on `mdl`, the model's origin (the wheels' contact): the stock 506 muzzles, all under
# their holder bone, then stood 0.24-0.67 m in the ground under a parked jet. Every other weapon (the blast drones'
# charges, the Primers' guns) and the fuel tank stay on the anchor.
HARDPOINT_FILES = frozenset({JET_ELEVON_FILE, 'EDF6VC_INTERCEPTOR.MRAB', 'EDF6VC_MULTIROLE.MRAB', 'EDF6VC_DRONE.MRAB',
                             'EDF6VC_CARRIER.MRAB', GUNSHIP_MODEL_FILE, SALVO_MODEL, M101_MODEL})


def model_file(model) -> str:  # noqa: ANN001 - [archive path, member]
    """The archive file name (upper case) of a vehicle SGO's animation_model[0] ([path, member])."""
    return str(model[0]).replace('\\', '/').rsplit('/', 1)[-1].upper()


def hardpoint_of(weapon: str) -> str | None:
    """The hardpoint bone weapon path `weapon` hangs on (jet_hardpoints.HP_GUN / HP_STORE), or None (the anchor)."""
    from jet_hardpoints import HP_GUN, HP_STORE
    if weapon.lower() in _GUNS:
        return HP_GUN
    kind = store_of(weapon)
    if kind and STORES[kind[0]].role in ('air', 'ground', 'bomb', 'rocket') and isinstance(STORES[kind[0]].weapon, (Missile, Bomb)):
        return HP_STORE
    return None


def _muzzle_stock(weapon: str) -> str:
    """The stock weapon whose muzzle list weapon path `weapon` (one hardpoint_of hangs) fires from."""
    name = weapon.rsplit('/', 1)[-1].upper()
    if name in JET_GUN_FILES:
        return JET_GUN_FILES[name]
    w = STORES[store_of(weapon)[0]].weapon
    if isinstance(w, Bomb):
        return JET_BOMB_STOCK
    return w.muzzle or JET_MISSILE_STOCK


@lru_cache(maxsize=None)
def _stock_muzzles(game: Game, stock: str) -> tuple[tuple[float, float, float], ...]:
    block = dsgo.parse(game.read('WEAPON', stock)).root.get('animation_model').items[2].data
    return tuple(tuple(round(v, 6) for v in struct.unpack_from('<3f', block, at)) for _n, _node, at in mab_muzzles(block))


def hardpoint_muzzles(game: Game) -> dict[str, tuple[tuple[float, float, float], ...]]:
    """{bone: every muzzle (from its holder bone) any weapon hung on it may fire from}: the guns' (JET_GUN_FILES) on
    HP_GUN; on HP_STORE every jet store's (STORE_FILES: a carrier's own load or any a loadout puts on its pylons,
    tools/support_loadout.py) and both templates' (the missile pods, the bomb racks). Read off the stock weapons they fire
    from (_muzzle_stock): what jet_hardpoints places, the same on every model."""
    from jet_hardpoints import HP_GUN, HP_STORE
    out: dict[str, set[tuple[float, float, float]]] = {HP_GUN: set(), HP_STORE: set()}
    for stock in JET_GUN_FILES.values():
        out[HP_GUN].update(_stock_muzzles(game, stock))
    stocks = {JET_MISSILE_STOCK, JET_BOMB_STOCK} | {_muzzle_stock(_weapon(f)) for f in STORE_FILES if hardpoint_of(_weapon(f))}
    for stock in sorted(stocks):
        out[HP_STORE].update(_stock_muzzles(game, stock))
    return {k: tuple(sorted(v)) for k, v in out.items()}


def jet_weapon_bones(jet: 'Jet', model, anchor: str) -> list[str]:  # noqa: ANN001 - [archive path, member]
    """The bone each of `jet`'s weapons hangs on (vehicle_weapon_setting, without the fuel tank) when it wears `model`:
    Jet.weapon_bones; on a model with hardpoints (HARDPOINT_FILES) its guns and stores on theirs; else `anchor`."""
    if jet.weapon_bones:
        return list(jet.weapon_bones)
    if model_file(model) in HARDPOINT_FILES:
        return [hardpoint_of(w) or anchor for w in jet.weapons]
    return [anchor] * len(jet.weapons)


def mab_locator(mab: bytes, name: str) -> tuple[int, int]:
    """(offset of the local vec4, offset of the radius) of locator `name` in the MAB block `mab` (an SGO's
    animation_model[2]). The block: b'MAB\\0'; u32 at 0x14 the record table, its records from +0x20 to the u32 at 0x18,
    0x20 bytes each (i32 +0 its name, +4 its parent bone's name: UTF-16, from the record; +0xC its vec4, from the record;
    float +0x10 its radius); the vec4s between the u32s at 0x1C and 0x20 (EDF.dll 0x6BADD0, 0x6BB420). ValueError
    unless exactly one record is named so and its vec4 is a point (w 1) in that area."""
    if mab[:4] != b'MAB\0':
        raise ValueError('不是 MAB 块')
    table, end, vecs, strings = struct.unpack_from('<4I', mab, 0x14)
    want = name.encode('utf-16le') + b'\0\0'
    hits = [r for r in range(table + 0x20, end, 0x20)
            if mab[r + struct.unpack_from('<i', mab, r)[0]:][:len(want)] == want]
    if len(hits) != 1:
        raise ValueError(f'MAB 里叫 {name!r} 的定位点有 {len(hits)} 个')
    r = hits[0]
    vec = r + struct.unpack_from('<i', mab, r + 0xC)[0]
    if not (vecs <= vec and vec + 16 <= strings and struct.unpack_from('<f', mab, vec + 12)[0] == 1.0):
        raise ValueError(f'定位点 {name!r} 的坐标不在 MAB 的坐标区')
    return vec, r + 0x10


def mab_muzzles(mab: bytes) -> list[tuple[str, str, int]]:
    """A weapon's muzzles in the MAB block `mab` (its SGO's animation_model[2]), in order: (name, the weapon-model node
    it hangs on, offset of its local position vec4). The weapon's constructor walks them (EDF.dll 0x68B62F..0x68BC97,
    the block at weapon+0x11B0): the list's header at the i32 at 0x14, u16 +2 the count, i32 +4 the first record from
    the header, records 0x20 apart: i32 +0 the name and +4 the node (UTF-16, from the record; a node the weapon model
    lacks: 'muzzle node not found'), +0xC the position vec4 (from the record); the grip list's header follows at
    +0x10. ValueError unless every position is a point (w 1) between the u32s at 0x1C and 0x20 and no two share one."""
    if mab[:4] != b'MAB\0':
        raise ValueError('不是 MAB 块')
    head = struct.unpack_from('<i', mab, 0x14)[0]
    vecs, strings = struct.unpack_from('<2I', mab, 0x1C)
    count = struct.unpack_from('<H', mab, head + 2)[0]
    first = head + struct.unpack_from('<i', mab, head + 4)[0]

    def text(at: int) -> str:
        end = at
        while mab[end:end + 2] != b'\0\0':
            end += 2
        return mab[at:end].decode('utf-16le')
    out = []
    for r in range(first, first + 0x20 * count, 0x20):
        name, node, _kind, vec = struct.unpack_from('<4i', mab, r)
        at = r + vec
        if not (vecs <= at and at + 16 <= strings and struct.unpack_from('<f', mab, at + 12)[0] == 1.0):
            raise ValueError(f'炮口 {text(r + name)!r} 的坐标不在 MAB 的坐标区')
        out.append((text(r + name), text(r + node), at))
    if len({at for _n, _p, at in out}) != len(out):
        raise ValueError('MAB 里有炮口共用一个坐标')
    return out


def door_point(box, stock: tuple[float, float, float], radius: float, mdl=None) -> tuple[list[float], float]:
    """(local position on `mdl`, radius) of the boarding point of a jet with collision box `box` ([centre, half extents]):
    the box's bottom (the ground), DOOR_OUT outside its right side, within its length; `stock`, `radius`: the V506
    door's (see DOOR_OUT). `mdl`: where the locators are measured from in the model's frame (mdl_at; None: the box's
    centre, every jet's)."""
    (cx, cy, cz), (hx, hy, hz) = box
    z = min(max(stock[2], -hz), hz)   # the stock door's place along the box, from its centre
    need = (HUMAN_HEIGHT * HUMAN_HEIGHT + DOOR_STEP * DOOR_STEP) ** 0.5 - DOOR_SLACK + DOOR_MARGIN
    if mdl is None:   # `mdl` is the box's centre: the box spans -half..half round it
        return [round(hx + DOOR_OUT, 3), round(-hy, 3), round(z, 3)], round(max(radius, need), 3)
    at = (cx + hx + DOOR_OUT - mdl[0], cy - hy - mdl[1], cz + z - mdl[2])
    return [round(v, 3) for v in at], round(max(radius, need), 3)


def mdl_at(jet: 'Jet') -> tuple[float, float, float] | None:
    """Where `jet`'s `mdl` (its MAB locators' frame) is in its model's frame: its origin (Jet.locators_on_origin), or
    None: the collision box's centre, as door_point / place_seat_camera and the checks take it by default."""
    return (0.0, 0.0, 0.0) if jet.locators_on_origin else None


def door_name(m: dict) -> str:
    """The door locator's name of seat 0 (vehicle_riding_position[0][0])."""
    seats = m.get('vehicle_riding_position')
    if not (isinstance(seats, list) and seats and isinstance(seats[0], list) and isinstance(seats[0][0], str)):
        raise ValueError('没有 vehicle_riding_position')
    return seats[0][0]


def move_door(m: dict, box, mdl=None) -> None:
    """`m` (a jet SGO's values) with its door (seat 0's: every seat of a jet shares it, make_jets.with_gunner_seat)
    moved to door_point."""
    mab = bytearray(m['animation_model'][2])
    vec, rad = mab_locator(bytes(mab), door_name(m))
    stock = struct.unpack_from('<3f', mab, vec)
    at, radius = door_point(box, stock, struct.unpack_from('<f', mab, rad)[0], mdl)
    struct.pack_into('<3f', mab, vec, *at)
    struct.pack_into('<f', mab, rad, radius)
    m['animation_model'][2] = bytes(mab)


def check_door(data: bytes, mdl=None, access_box=None) -> None:
    """Re-read a jet SGO and raise DoorError unless its door (on `mdl`, the collision box's centre) is at its collision
    box's (heli_rigid_body) bottom, the ground, outside its right side by DOOR_OUT, within its length, and a human standing
    DOOR_STEP m from it on that ground (its position at its feet or HUMAN_HEIGHT over them) is in reach.
    access_box is walkup_box's proven clear footprint; it retains the rigid body's centre and ground height."""
    _, m = sgo.read(data)
    mab = m['animation_model'][2]
    name = door_name(m)
    vec, rad = mab_locator(mab, name)
    x, y, z = struct.unpack_from('<3f', mab, vec)
    reach = struct.unpack_from('<f', mab, rad)[0] + DOOR_SLACK
    box = access_box or [[float(_value(v)) for v in row] for row in m['heli_rigid_body'][:2]]
    (cx, cy, cz), (hx, hy, hz) = box
    if mdl is not None:   # from `mdl` to the box's centre
        x, y, z = x + mdl[0] - cx, y + mdl[1] - cy, z + mdl[2] - cz
    if abs(y + hy) > 2e-3 or x < hx + DOOR_OUT - 1e-3 or not -hz - 1e-3 <= z <= hz + 1e-3:
        raise DoorError(f'上车点 ({x:.2f},{y:.2f},{z:.2f}) 不在碰撞箱（中心起半尺寸 {hx:.2f},{hy:.2f},{hz:.2f}）右侧外 {DOOR_OUT} m 的地面上')
    worst = max(((DOOR_STEP ** 2 + feet ** 2) ** 0.5 for feet in (0.0, HUMAN_HEIGHT)))
    if worst > reach:
        raise DoorError(f'站在上车点旁 {DOOR_STEP} m 的地面上够不着（要 {worst:.2f} m，能 {reach:.2f} m）')


# The seat camera (docs/camera-re.md §3b, docs/player-jet-re.md §14). Riding, the game's camera is seat 0's MAB camera
# locator (the eye, vehicle_riding_position[0][2][0], `カメラ１`) looking at its LookTarget
# (`カメラ１注目`); both hang on `mdl`, the model origin after native phase 1.
# The stock rig (eye (0,5.4,-14.45), look (0,2.75,1.1)) is inside the 77 m carrier.
# Keep an outside stock eye, otherwise scale the original rig by model length.
# fit_camera/check_camera use the same explicit locator frame as the door.
CAMERA_LOOK_KEY = 'LookTarget'
Bounds = tuple[tuple[float, float, float], tuple[float, float, float]]   # (min xyz, max xyz) in the model's frame


class CameraError(Exception):
    """A jet's seat camera sees its own model from inside, or over nothing (check_camera)."""


def _mab_record(mab: bytes, name: str) -> int:
    """The offset of the MAB locator record named `name` (see mab_locator)."""
    vec, _rad = mab_locator(mab, name)
    table, end = struct.unpack_from('<2I', mab, 0x14)
    for r in range(table + 0x20, end, 0x20):
        if r + struct.unpack_from('<i', mab, r + 0xC)[0] == vec:
            return r
    raise ValueError(f'定位点 {name!r} 没有记录')


def camera_names(m: dict) -> tuple[str, str]:
    """(eye, look) locator names of seat 0's camera: the seat's camera entry and the LookTarget of that locator's SGO
    (record +0x1C, relative to the record)."""
    seat = m['vehicle_riding_position'][0]
    eye = seat[2][0] if isinstance(seat[2], list) and seat[2] and isinstance(seat[2][0], str) else None
    if eye is None:
        raise ValueError('座位 0 没有镜头定位点')
    mab = m['animation_model'][2]
    r = _mab_record(mab, eye)
    at = struct.unpack_from('<i', mab, r + 0x1C)[0]
    look = sgo.read(mab[r + at:])[1].get(CAMERA_LOOK_KEY) if at else None
    if not isinstance(look, str):
        raise ValueError(f'镜头定位点 {eye!r} 没有 {CAMERA_LOOK_KEY}')
    return eye, look


def _inside(p: list[float] | tuple[float, ...], bounds: Bounds) -> bool:
    lo, hi = bounds
    return all(lo[i] <= p[i] <= hi[i] for i in range(3))


def sight_problem(eye, look, bounds: Bounds) -> str | None:
    """Why a camera at `eye` looking at `look` (model frame) does not see the plane from outside: its eye in the model's
    bounds, or behind it with the line of sight under the model's top at its tail. None when it does."""
    lo, hi = bounds
    if _inside(eye, bounds):
        return f'眼睛 {tuple(round(x, 2) for x in eye)} 在模型包围盒 {lo}..{hi} 里'
    if eye[2] < lo[2] and look[2] > eye[2]:
        y = eye[1] + (look[1] - eye[1]) * (lo[2] - eye[2]) / (look[2] - eye[2])
        if y < hi[1]:
            return f'视线在机尾 z={lo[2]:.2f} 处高 {y:.2f}，低于机顶 {hi[1]:.2f}'
    return None


def seat_camera(eye, look, centre, bounds: Bounds, heli: Bounds, heli_centre) -> tuple[list[float], list[float]] | None:
    """New (eye, look) locator offsets on `mdl` for a jet whose box centre is `centre` and model `bounds` (its frame), from
    the stock ones (`eye`, `look`; `heli`, `heli_centre`: the V506 model's bounds and box centre); None to keep them."""
    if not _inside([centre[i] + eye[i] for i in range(3)], bounds):
        return None
    s = (bounds[1][2] - bounds[0][2]) / (heli[1][2] - heli[0][2])
    return ([round(s * (heli_centre[i] + eye[i]) - centre[i], 3) for i in range(3)],
            [round(s * (heli_centre[i] + look[i]) - centre[i], 3) for i in range(3)])


def fit_camera(m: dict, box, bounds: Bounds, heli: Bounds, heli_centre, mdl=None) -> None:
    """`m` (a jet SGO's values, collision box `box` [centre, half extents], model `bounds`) with its seat camera moved by
    seat_camera."""
    mab = bytearray(m['animation_model'][2])
    eye_name, look_name = camera_names(m)
    (eye_at, _), (look_at, _) = mab_locator(bytes(mab), eye_name), mab_locator(bytes(mab), look_name)
    eye, look = struct.unpack_from('<3f', mab, eye_at), struct.unpack_from('<3f', mab, look_at)
    # mdl is a render/model-space locator, after 652630 subtracts the rigid
    # centre. The stock helicopter's mdl uses the same origin convention.
    origin = box[0] if mdl is None else mdl
    stock_origin = heli_centre if mdl is None else (0.0, 0.0, 0.0)
    fit = seat_camera(eye, look, origin, bounds, heli, stock_origin)
    if fit is None:
        return
    struct.pack_into('<3f', mab, eye_at, *fit[0])
    struct.pack_into('<3f', mab, look_at, *fit[1])
    m['animation_model'][2] = bytes(mab)


def check_camera(data: bytes, bounds: Bounds, mdl=None) -> tuple[list[float], list[float]]:
    """Re-read a jet SGO and raise CameraError unless its seat camera (on `mdl`, at its collision box's centre) sees its
    model (`bounds`, model frame) from outside (sight_problem). Returns (eye, look) in the model's frame."""
    _, m = sgo.read(data)
    mab = m['animation_model'][2]
    centre = [float(_value(v)) for v in m['heli_rigid_body'][0]] if mdl is None else list(mdl)
    eye_name, look_name = camera_names(m)
    pts = []
    for name in (eye_name, look_name):
        vec, _ = mab_locator(mab, name)
        pts.append([round(centre[i] + struct.unpack_from('<3f', mab, vec)[i], 3) for i in range(3)])
    why = sight_problem(pts[0], pts[1], bounds)
    if why:
        raise CameraError(why)
    return pts[0], pts[1]


def place_seat_camera(m: dict, box, camera: tuple[tuple[float, float, float], tuple[float, float, float]],
                      mdl=None) -> None:
    """`m` with its seat camera's eye and look locators at `camera` (model frame): offsets on `mdl`, at `mdl` (mdl_at;
    None: the collision box's centre `box[0]`, see CAMERA_LOOK_KEY)."""
    origin = box[0] if mdl is None else mdl
    mab = bytearray(m['animation_model'][2])
    for name, at in zip(camera_names(m), camera):
        vec, _ = mab_locator(bytes(mab), name)
        struct.pack_into('<3f', mab, vec, *(round(at[i] - float(origin[i]), 3) for i in range(3)))
    m['animation_model'][2] = bytes(mab)


def check_seat_camera(data: bytes, bounds: Bounds, camera: tuple[tuple[float, float, float], tuple[float, float, float]],
                      mdl=None) -> None:
    """Re-read a mech's SGO and raise CameraError unless its seat camera is `camera` (model frame), its eye outside the
    model's `bounds` and its look point inside them (it looks at the mech). `mdl`: where its locators are measured
    from (mdl_at; None: the box's centre)."""
    _, m = sgo.read(data)
    mab = m['animation_model'][2]
    centre = [float(_value(v)) for v in m['heli_rigid_body'][0]] if mdl is None else list(mdl)
    got = []
    for name in camera_names(m):
        vec, _ = mab_locator(mab, name)
        got.append([round(centre[i] + struct.unpack_from('<3f', mab, vec)[i], 3) for i in range(3)])
    if any(abs(got[k][i] - camera[k][i]) > 2e-3 for k in range(2) for i in range(3)):
        raise CameraError(f'座位镜头 {got} 不是 {camera}')
    if _inside(got[0], bounds) or not _inside(got[1], bounds):
        raise CameraError(f'座位镜头眼睛 {got[0]} 应在模型 {bounds} 外、注视点 {got[1]} 应在模型内')


def _value(v) -> float:
    """An SGO number node's value (sgo.Float keeps its bytes)."""
    return v.value if isinstance(v, sgo.Float) else float(v)


def _rebone(v, names: set[str], to: str = JET_ROOT_BONE):
    """`v` with every string in `names` replaced by `to` (deep)."""
    if isinstance(v, list):
        return [_rebone(c, names, to) for c in v]
    return to if isinstance(v, str) and v in names else v


JET_BODY_BONE = 'bomber501'
# The V506 ragdoll's rest pose (RAGDOLL_V506_HELI.SHKT: each body's position less the body proxy's, every orientation
# the identity), which its constraints hold; tools/selftest.py checks it against the game's file.
V506_PROXY_LAYOUT: dict[str, tuple[float, float, float]] = {
    'RagDollProxys.body': (0.0, 0.0, 0.0),
    'RagDollProxys.rearWheelSus': (-0.0005, -1.347, -7.2468),
    'RagDollProxys.rotor': (0.0, 1.5602, 0.0),
    'RagDollProxys.bend_roterA': (0.0, 1.5602, 0.5158),
    'RagDollProxys.bend_roterB': (-0.5158, 1.5602, 0.0),
    'RagDollProxys.bend_roterC': (0.0, 1.5602, -0.5158),
    'RagDollProxys.bend_roterD': (0.5158, 1.5602, 0.0),
    'RagDollProxys.tailRotor': (0.4356, 1.6471, -7.15),
}


def _dead_effect(effects: list) -> list:
    """The V506's dead effect with no part thrown off. Its first step (BrakePartFunc, vtable 0x17D88F0) throws
    [['rotor', 6]] off and its third (ExplosionAddSelectActivePartFunc) [['tailRotor', 2]]: each name is looked up
    in the ragdoll (0x6EA4B0), which knows only the bones animation_from_ragdoll binds (a jet's: its body, see
    _jet_ragdoll); any other name is -1 there, and BrakePartFunc's step 0x5F85A0 reads that record unchecked
    (EDF+0x5F866C: a player jet crashing into the ground, 2026-10-04). A jet has no rotor to throw: both lists
    empty (an empty list is the stock second step's own form; the steps then have nothing to do)."""
    for step, stock in ((0, 'rotor'), (2, 'tailRotor')):
        parts = effects[step][2]
        if not (isinstance(parts, list) and len(parts) == 1 and isinstance(parts[0], list)):
            raise ValueError(f'V506_HELI 的 vehicle_dead_effect[{step}] 不是预期的样子（{stock}）')
        effects[step][2] = []
    return effects


def _jet_ragdoll(blob: bytes, body: str = JET_BODY_BONE, centre: list[float] | tuple[float, ...] | None = None) -> bytes:
    """The ragdoll's embedded binding SGO with every model-side bone one the bomber has.
    RagdollController::BindDependency (0x6E6A50): each animation_from_ragdoll entry looks its model bone
    up (0x6E7B98); found, the proxy's record gets the bone (+0x60, first entry wins) and the bone gets the
    proxy (+8, last entry wins). Every proxy must end up with a bone: the loop at 0x6E8280 reads each
    record's +0x60 unchecked (crashed 2026-10-03 with the V506 bone names, then with only the body proxy
    bound). So every proxy is bound to the fuselage bone, the body proxy last so it is what drives it
    (the rotor proxies spin); ragdoll_from_animation has them all follow it.
    `centre`: the collision box's centre (heli_rigid_body[0], the vehicle's position; see seat_camera), in the body's
    frame. The body bone is drawn at the model's origin, the centre less that; with the V506's zero offsets the proxies (a
    13.6 m heli's hull and rotors, y -1.7..+1.95 round their origin) sat at the model's origin: under the carrier's belly
    (3.49 m over it), a heli-sized body hanging under the plane (the user, 2026-10-06: 「碰撞体积也是」在空母底下). Every
    proxy goes on `centre` (proxy = bone o (centre)), every bone it drives back off it (bone = proxy o (-centre)), so
    the two bindings stay each other's inverse and the proxies keep their places relative to each other. None: the
    V506's offsets as they are (a model jet_models does not measure: the Primer creatures, the submarine carrier).
    With `centre`, each proxy goes where the ragdoll's own rest pose has it relative to the body proxy
    (V506_PROXY_LAYOUT), upright: the SHKT's hinges and slider hold exactly that pose. The V506 gets it from bones of
    its own; one fuselage bone with the same offset for all put the eight proxies on one point, each still turned by its
    V506 binding, so the moment a destroyed aircraft's ragdoll went dynamic its constraints threw the parts apart: the
    wreck left the ground within 6 frames (2026-10-12 probe, docs/feedback-2026-10-12-ragdoll-twitch.md; the user:
    「飞机炸的时候，尸体布娃娃会抽搐」). In the rest pose it lies still until the dead effect's steps."""
    version, inner = sgo.read(blob)
    up = None if centre is None else [float(c) for c in centre]

    def offset(e: list, sign: float) -> list:
        if up is None:
            return e[1:]
        proxy = e[0][1] if sign > 0 else e[0][0]
        if proxy not in V506_PROXY_LAYOUT:
            raise ValueError(f'{proxy} is no proxy of the V506 ragdoll (V506_PROXY_LAYOUT)')
        rest = V506_PROXY_LAYOUT[proxy]
        return [[sign * (up[k] + rest[k]) for k in range(3)], [0.0, 0.0, 0.0, 1.0]] + e[3:]
    inner['ragdoll_from_animation'] = [[[body, e[0][1]]] + offset(e, 1.0) for e in inner['ragdoll_from_animation']]
    drive: dict[str, list] = {}
    for e in inner['animation_from_ragdoll']:
        drive.setdefault(e[0][0], [[e[0][0], body]] + offset(e, -1.0))   # globalSRT: a second body entry, dropped
    body = drive.pop('RagDollProxys.body')
    inner['animation_from_ragdoll'] = list(drive.values()) + [body]
    return sgo.write(version, inner)


# The SGO member that marks a body built on its ragdoll's mesh compound (src/body506.cpp kAirframeMember).
AIRFRAME_MEMBER = 'edf6vc_airframe'


def jet_own_cas(jet: Jet) -> str | None:
    """The CAS a jet flies on when it is not its model's (V506_HELI.CAS): its own (Jet.cas) or its model's retarget
    (pylib/jet_models.py ANIMATIONS); both retargets of the 506's."""
    import jet_models
    return jet.cas or ('app:/object/' + jet_models.ANIMATIONS[jet.file].lower() if jet.file in jet_models.ANIMATIONS else None)


def jet_sgo(game: Game, name: str, model: list[str] | None = None, body: str = JET_BODY_BONE,
            rigid: list[list[float]] | None = None, airborne: bool = False) -> bytes:
    """`model`: the model archive and file (default JET_MODEL, the stock bomber); `body`: its mesh bone, which
    the root and the ragdoll drive; `rigid`: the collision box [centre, half extents] (default JET_RIGID_BODY).
    A jet with its own model (Jet.model) always flies it: these three come from the Jet then. `airborne`: only ever
    made in the air (a stock bomber an airstrike's jet takes over, tools/make_jets.py BOMBERS): its box is its model's
    as it is, not cut at its origin (on_origin: a vehicle made on the ground stands on its origin)."""
    jet = JETS[name]
    root = anchor = JET_ROOT_BONE   # root: see JET_MAB_ROOT
    if jet.model is not None:
        model, body, rigid = list(jet.model), jet.body, [list(x) for x in jet.rigid] if jet.rigid else None
        anchor = jet.anchor
    import jet_models
    import aircraft_collision
    airframe = aircraft_collision.model_key(jet, model)
    if jet.player or jet.parked:
        # The player sees the whole plane: its box is the model's (wings, nose and tail), measured, not a fuselage
        # box (an NPC jet's is the fuselage: a formation's wings would catch on each other, low passes scrape; a
        # carrier's drones leave and dock under its middle). A parked one is the player's to walk up to.
        rigid = jet_models.model_box(game, jet.file or jet.box_model)
    elif jet.file in jet_models.MODELS:
        rigid = jet_models.fuselage_box(game, jet.file)   # off its model as made (grounded): never under its origin
    elif jet.model is None and rigid is None and model == JET_ELEVON_MODEL:
        rigid = jet_models.fuselage_box(game, None)
    if airframe is not False:
        # A shared shape/coordinate frame for NPC, parked and requested copies.
        # body506 replaces this broad box with the mesh-derived ragdoll compound;
        # keeping the full-model centre also keeps locators/cameras consistent.
        rigid = jet_models.model_box(game, airframe)
    version, m = sgo.read(game.read('OBJECT', jet.stock + '.SGO'))
    at, want = JET_MAB_ROOT
    if m['animation_model'][2][at:at + 2 * len(want) + 2] != want.encode('utf-16le') + b'\0\0':
        raise ValueError(f'V506 MAB 的根骨骼名不在 {at:#x}')
    if 'vehicle_setup' not in m or 'mission_setup' in m:
        raise ValueError('V506_HELI 没有 vehicle_setup')
    setup = m.pop('vehicle_setup')
    setup[1][0] = jet.mark
    setup[0] = [float(x) for x in tier_of(name)]
    stock = {w[0]: w for w in setup[3]}   # each weapon keeps its stock per-weapon parameters (a derived one its stock's)
    setup[3] = with_fuel([[w, stock[_STOCK_OF[w]][1]] if _STOCK_OF.get(w) in stock else stock.get(w, [w, [0.0001, 0.1]])
                          for w in jet.weapons], stock['app:/weapon/v_fuel01.sgo'])
    m['mission_setup'] = setup
    if jet.player or jet.mark in BOARDABLE_MARKS:
        # Every class flies it (the user, 2026-10-05: Wing Divers and Fencers too): the seat's class mask (R 1, WD 2,
        # F 4, AR 8; the 506's 9) to 15, and the seat's name key 505_TANK_DRIVER (the same "Pilot Seat" text as
        # 506_HELI_DRIVER: [3] names the seat, it is no pose; PLAYER_SEAT_POSE). An NPC body's pilot is the stock RideAi's dummy
        # rider (online_authority.cpp SeatNpcRider), the same one a requested twin gets in this seat when the player
        # leaves it in the air (playerjet_board.inc). No soldier comes in by the mask: crew.cpp Crew seats that dummy
        # too, npcai.cpp BoardSquad fills seats from 1 on.
        seat = m['vehicle_riding_position'][0]
        seat[3], seat[4] = PLAYER_SEAT_POSE, PLAYER_SEAT_CLASSES
    if jet.player or jet.requested:
        import copy
        m['vehicle_setup'] = copy.deepcopy(setup)
    if jet.player:
        cam = m['game_object_camera_setting']
        if jet.camera is not None:
            m['game_object_camera_setting'] = [cam[0], [float(x) for x in jet.camera]]
    m['game_object_durability'] = jet.durability
    model_ref = model
    model = m['animation_model']
    mab = model[2]
    for at, old in JET_MAB_BONES:
        mab = sgo.replace_utf16(mab, at, old, anchor)
    cas = jet_own_cas(jet) or model[1]
    m['animation_model'] = [list(JET_MODEL if model_ref is None else model_ref), cas, mab]
    m['animation_model_bone_mapping'] = [root, body]
    bones = {'body', 'rotor', 'tailRotor'}
    m['vehicle_weapon_setting'] = with_fuel([[b, 0] for b in jet_weapon_bones(jet, m['animation_model'][0], anchor)], [anchor, -1])
    # Without the plugin the 506 builds weapons for its first four holders only (src/stores.cpp): fewer would read an
    # entry that is not there; the plugin builds them all, one an entry, so the two lists must agree. The fuel tank is
    # among the four (with_fuel): built either way.
    if len(m['vehicle_weapon_setting']) < 4 or len(m['vehicle_weapon_setting']) != len(setup[3]):
        raise ValueError(f'{name}: {len(setup[3])} weapons for {len(m["vehicle_weapon_setting"])} holders (at least 4)')
    m['vehicle_dead_effect'] = _dead_effect(_rebone(m['vehicle_dead_effect'], bones, anchor))
    if airframe in aircraft_collision.PACK_FILES:
        # Its body is its ragdoll's mesh compound (src/body506.cpp MakeAirframe), told by this member: the game keeps
        # no shape userData of a compact SHKT (aircraft_collision.MAGIC read 0 at shape +0x28 in every vehicle, range
        # 2026-10-21: the Dreadnought flew as its whole box, its deck 12 m over its hull).
        m[AIRFRAME_MEMBER] = 1
    m['roter_contact_damage_scale'] = 0.0
    m['heli_contact_damage_scale'] = 0.0005
    rb = m['heli_rigid_body']
    box = JET_RIGID_BODY if rigid is None else rigid
    box = [list(box[0]), list(box[1])] if airborne else on_origin(box)
    m['heli_rigid_body'] = [box[0], box[1], rb[2]]
    door = _moves_door(jet)
    bounds = heli = None
    access_box = box
    if door:
        access_box = walkup_box(game, jet.file or jet.box_model, tuple(tuple(r) for r in box))
        move_door(m, access_box, mdl_at(jet))
        # The models jet_models measures (the ones the player boards): the seat camera and the ragdoll on the box's
        # centre, where `mdl` is (seat_camera, _jet_ragdoll).
        bounds = jet_models.model_bounds(game, jet.file or jet.box_model)
        heli = jet_models.heli_bounds(game)
        if jet.seat_camera is None:
            fit_camera(m, box, bounds, heli, [_value(v) for v in rb[0]], mdl_at(jet))
        else:
            place_seat_camera(m, box, jet.seat_camera, mdl_at(jet))
    rag = m['ragdoll']
    m['ragdoll'] = [rag[0] if airframe is False else 'app:/object/' + aircraft_collision.file_of(airframe).lower(),
                    _jet_ragdoll(rag[1], body, box[0] if door or airframe is not False else None)]
    se = m.get('heli_se_table')
    if not isinstance(se, list) or len(se) <= max(JET_HELI_SE_ROWS) or any(
            sgo.plain(se[i]) != cue for i, cue in JET_HELI_SE.items()):
        raise ValueError('V506_HELI 的 heli_se_table 不是预期的样子')
    for i in JET_HELI_SE_ROWS:
        se[i] = JET_SILENT_SE
    if jet.creature:
        bank, cues, contact = CREATURE_SOUNDS[jet.creature]
        if bank not in m['game_sound']:
            m['game_sound'] = list(m['game_sound']) + [bank]
        for i, cue in cues.items():
            se[i] = cue
        m['ragdoll_contact'][0] = contact
        m['vehicle_damage_effect'] = [0.0]   # the 506's damage smoke: none (a scale of 1.0 when the key is missing)
    # A unified model file (pylib/menu_model.py, docs/unified-model-re.md): the room / HQ 3D preview reads this very file.
    # Every jet's CAS is the 506's or a retarget of it (pylib/jet_models.py animation, pylib/xm_air_model.py): `Roter` /
    # `Main`, not the menu's case-sensitive `main` (-1 index: test site #50), so its menu CAS is always
    # tools/menu_preview.py's copy with that one anmgroup renamed (menu_preview.jet_cas_sources lists them).
    import menu_model
    cas = m['animation_model'][1].lower()
    if cas != menu_model.JET_CAS and cas != (jet_own_cas(jet) or '').lower():
        raise ValueError(f'{name}: CAS {cas} 既不是 506 的，也不是该机自己的重定向 CAS')
    menu_cas = menu_model.menu_cas_path(cas)
    _, stock_menu = sgo.read(game.read(menu_model.MENU_FOLDER, menu_model.VEHICLE_STOCK))
    m = menu_model.add_sgo(m, menu_model.vehicle_menu_keys(stock_menu, m['animation_model'], menu_cas))
    out = sgo.write(version, m)
    if door:
        check_door(out, mdl_at(jet), access_box)
        if jet.seat_camera is None:
            check_camera(out, bounds, mdl_at(jet))
        else:
            check_seat_camera(out, bounds, jet.seat_camera, mdl_at(jet))
    return out


def _moves_door(jet: Jet) -> bool:
    """Whether jet_sgo puts `jet`'s door beside its box (move_door): every model pylib/jet_models.py makes and the
    bombers; not a model another builder makes (the Primers' fighter, the submarine carrier: not boarded on the
    ground), whose door stays the V506's."""
    import jet_models
    # A requested body measured on a stock bomber model (box_model: the gunship) is boarded on the ground too.
    # A player's vehicle with a model another builder makes (the Sazabi) is boarded on the ground too.
    # An X-Morph jet's requested twin is dropped on the ground for the player to board.
    return (jet.file is None or jet.file in jet_models.MODELS or jet.file in jet_models.XM_FILES or jet.box_model is not None
            or jet.player)


def as_mission_sgo(data: bytes) -> bytes:
    """A call-in vehicle SGO turned into a script-placeable one: its `vehicle_setup` name becomes
    `mission_setup` (same length, same value layout) and the name table is re-sorted. Little-endian SGO:
    header {count, data offset, name count, name table offset} at 8, names {string offset from the entry,
    member index}. DSGO: see _sort_dsgo_names."""
    old, new = 'vehicle_setup'.encode('utf-16le') + b'\0\0', 'mission_setup'.encode('utf-16le') + b'\0\0'
    if data[:4] not in (b'SGO\0', b'DSGO'):
        raise ValueError('不是小端 SGO / DSGO')
    if data.count(old) != 1 or new in data:
        raise ValueError('vehicle_setup 不唯一或已有 mission_setup')
    buf = bytearray(data.replace(old, new))
    if data[:4] == b'DSGO':
        _sort_dsgo_names(buf)
        return bytes(buf)
    _, _, name_count, name_off = struct.unpack_from('<4I', buf, 8)
    entries = []
    for i in range(name_count):
        p = name_off + i * 8
        rel, idx = struct.unpack_from('<iI', buf, p)
        entries.append((_utf16_at(buf, p + rel), p + rel, idx))
    for i, (_, at, idx) in enumerate(sorted(entries)):
        p = name_off + i * 8
        struct.pack_into('<iI', buf, p, at - p, idx)
    return _without_ai_obstacle(bytes(buf))


def _without_ai_obstacle(data: bytes) -> bytes:
    """`data` without its `ai_obstacle` member. A script-placed vehicle is AI-driven, and only then does the
    game (EDF+62C990, under EDF+6747F2's flag test) look each ai_obstacle name up in the vehicle's collision
    bodies (+0xE40) and read the result without a null check. The call-in Grape 401 lists one the placed
    vehicle does not have (2026-10-04: EXCEPTION at EDF+62CB9C, rbx=5 entries, r15=4); the stock game never
    places a 401 from a script, so it never hit it. Without the member the loop is skipped (as for the 502,
    which has none). SGOs without it come back unchanged."""
    version, m = sgo.read(data)
    if 'ai_obstacle' not in m:
        return data
    del m['ai_obstacle']
    return sgo.write(version, m)


def _sort_dsgo_names(buf: bytearray) -> None:
    """Re-sort the top-level dictionary's name table of a DSGO (see pylib/dsgo.py): node 0 at the node
    table is that dictionary, {name table offset, name count, ...} at node + value; names are {string
    offset from the entry, member position}, kept sorted like the stock files."""
    table = struct.unpack_from('<I', buf, 4)[0]
    raw, typ = struct.unpack_from('<QI', buf, table)
    if typ != 3:
        raise ValueError('DSGO 顶层不是字典')
    d = table + raw
    name_off, name_count = struct.unpack_from('<II', buf, d)
    entries = []
    for k in range(name_count):
        e = d + name_off + k * 8
        so, member = struct.unpack_from('<II', buf, e)
        entries.append((_utf16_at(buf, e + so), e + so, member))
    for k, (_, at, member) in enumerate(sorted(entries)):
        e = d + name_off + k * 8
        struct.pack_into('<II', buf, e, at - e, member)


def _utf16_at(buf: bytes, off: int) -> str:
    end = off
    while buf[end:end + 2] != b'\0\0':
        end += 2
    return buf[off:end].decode('utf-16le')


def object_dir(game_root: str) -> str:
    return os.path.join(game_root, 'Mods', 'OBJECT')


def weapon_dir(game_root: str) -> str:
    return os.path.join(game_root, 'Mods', 'WEAPON')



def jet_guns(game: Game) -> dict[str, bytes]:
    """The jets' guns (JET_GUN_FILES): the stock gatling with JET_GUN_SPEED and JET_GUN_ALIVE; the
    blast drones' charges (JET_BLAST_FILES); the stores (STORE_FILES: missiles and bombs, one file a load)."""
    out = {}
    for name, stock in JET_GUN_FILES.items():
        doc = dsgo.parse(game.read('WEAPON', stock))
        r = doc.root
        if r.get('AmmoClass') != 'SolidBullet01' or r.get('AmmoSpeed') * r.get('AmmoAlive') >= JET_GUN_REACH:
            raise ValueError(f'{stock} 不是预期的直升机机炮')
        r.set('AmmoSpeed', JET_GUN_SPEED)
        r.set('AmmoAlive', JET_GUN_ALIVE)
        out[name] = dsgo.write(doc)
    for name, (stock, key) in XM_GUN_FILES.items():
        # an X-Morph jet's guns (XM_GUN_FILES): the jets' gun (its reach) with the game's numbers (Unit.air 'gun')
        unit = xm_units.UNITS[key]
        doc = dsgo.parse(out[next(f for f, s in JET_GUN_FILES.items() if s == stock)])
        r = doc.root
        for k, v in unit.air['gun'].items():
            if k not in r.names.values():
                raise ValueError(f'{stock}: no {k}')
            r.set(k, float(v))
        _named(r, Store(unit.gun[0], 'gun', 0.0, 0.0, Shell(stock, 'SolidBullet01', 'mg'),
                        _local(unit.gun[0], unit.gun[2], unit.gun[3], unit.gun[1], unit.gun[0])))
        out[name] = dsgo.write(doc)
    for name, (damage, gap, speed, alive, size, blast, colour, spread, gravity) in PRIMER_GUN_FILES.items():
        doc = dsgo.parse(game.read('WEAPON', PRIMER_GUN_STOCK))
        r = doc.root
        if r.get('AmmoClass') != 'SolidBullet01' or len(r.get('AmmoColor').items) != 4:
            raise ValueError(f'{PRIMER_GUN_STOCK} 不是预期的直升机机炮')
        flash = r.get('MuzzleFlash_CustomParameter')
        for key, value in (('AmmoCount', 99999.0), ('AmmoDamage', damage), ('FireInterval', gap), ('AmmoSpeed', speed),
                           ('AmmoAlive', alive), ('AmmoSize', size), ('AmmoExplosion', blast), ('FireAccuracy', spread),
                           ('AmmoGravityFactor', gravity)):
            r.set(key, value)
        r.get('AmmoColor').items[:] = list(colour)
        if len(flash.items) == 10 and isinstance(flash.items[8], dsgo.Node) and len(flash.items[8].items) == 4:
            flash.items[8].items[:] = list(colour)   # the muzzle flash in the round's colour
        out[name] = dsgo.write(doc)
    for name, (damage, radius) in JET_BLAST_FILES.items():
        doc = dsgo.parse(game.read('WEAPON', JET_BLAST_STOCK))
        r = doc.root
        cp = r.get('Ammo_CustomParameter')
        if r.get('AmmoClass') != 'GrenadeBullet01' or len(cp.items) != 6:
            raise ValueError(f'{JET_BLAST_STOCK} 不是预期的直升机炸弹')
        cp.items[0], cp.items[3], cp.items[5] = 1.0, 0.0, 0.0
        for key, value in (('AmmoCount', 1.0), ('FireCount', 1.0), ('FireBurstCount', 1.0), ('FireInterval', 1.0),
                           ('AmmoSpeed', 0.01), ('AmmoGravityFactor', 0.0), ('AmmoAlive', JET_BLAST_ALIVE),
                           ('AmmoDamage', damage), ('AmmoExplosion', radius)):
            r.set(key, value)
        out[name] = dsgo.write(doc)
    for name in STORE_FILES:
        out[name] = store_sgo(game, name)
    return out


def store_sgo(game: Game, name: str) -> bytes:
    """The store weapon file `name` (store_file: EDF6VC_<KIND>_<rounds>.SGO) built on this Root.cpk: a missile, a bomb
    or a stock vehicle's round (Shell)."""
    got = store_of(_weapon(name))
    if got is None:
        raise ValueError(f'{name} 不是挂载武器文件')
    kind, rounds = got
    store = STORES[kind]
    if isinstance(store.weapon, Bomb):
        return _bomb_sgo(game, store, rounds)
    if isinstance(store.weapon, Shell):
        return _shell_sgo(game, store, rounds)
    return _missile_sgo(game, store, rounds)


def _named(r, name: str | Store) -> None:
    """Every name.* row of a weapon: the store's name (the stock HUD shows the weapon's, weapon +0x1B0), in the row's
    language where the store has one (Store.names)."""
    local = dict(name.names) if isinstance(name, Store) else {}
    plain = name.name if isinstance(name, Store) else name
    for key in list(r.names.values()):
        if key.startswith('name.'):
            r.set(key, local.get(key[5:].lower(), plain))


def _check_round(shell: Shell, r: dsgo.Node, made: bool) -> None:
    """Shell `shell`'s round is of its kind: its template's family (`made` False: the game's own round of that family,
    not a guess) and, once its `extra` is in (`made` True), what the kind means (ValueError otherwise)."""
    blast, pierce, count = r.get('AmmoExplosion'), r.get('AmmoIsPenetration'), r.get('FireCount')
    where = f'{shell.template}{"（改后）" if made else ""}'
    if shell.kind not in ROUND_KINDS:
        raise ValueError(f'{shell.template}: 未知弹种 {shell.kind}')
    if shell.kind in ('mg', 'canister') and blast:
        raise ValueError(f'{where}: {shell.kind} 弹不应有爆炸半径')
    if shell.kind == 'ap' and (blast or pierce != 1.0):
        raise ValueError(f'{where}: ap 弹应穿透且无爆炸')
    if shell.kind in ('he', 'heat') and not blast:
        raise ValueError(f'{where}: {shell.kind} 弹应有爆炸半径')
    if not made:
        return
    if shell.kind == 'heat' and (pierce != 0.0 or count != 1.0):
        raise ValueError(f'{where}: 破甲弹一发一弹、不穿透（聚能射流止于所中之物）')
    if shell.kind == 'canister' and not (isinstance(count, float) and count > 1.0):
        raise ValueError(f'{where}: 霰弹一发应有多枚弹丸（FireCount > 1）')


def _shell_sgo(game: Game, store: Store, rounds: int) -> bytes:
    """A stock vehicle's round (Shell): its template as it is, but its load and `extra`, and its name."""
    shell = store.weapon
    doc = dsgo.parse(game.read('WEAPON', shell.template))
    r = doc.root
    if r.get('AmmoClass') != shell.ammo_class:
        raise ValueError(f'{shell.template} 的弹类是 {r.get("AmmoClass")}，不是 {shell.ammo_class}')
    _check_round(shell, r, made=False)
    for key, value in shell.params(rounds).items():
        if r.get(key) is None:
            raise ValueError(f'{shell.template} 缺少 {key}')
        r.set(key, value)
    _check_round(shell, r, made=True)
    if shell.template in MUZZLE_VELOCITY and 'AmmoSpeed' not in dict(shell.extra):   # the stock gun's real speed
        real_muzzle(r, MUZZLE_VELOCITY[shell.template][0], shell.template)
    if shell.ammo_class == 'RocketBullet01':
        wt_tracer(r, shell.template)
    if shell.colour is not None:
        colour = r.get('AmmoColor')
        if not isinstance(colour, dsgo.Node) or len(colour.items) != 4:
            raise ValueError(f'{shell.template} 的 AmmoColor 不是 4 个数')
        colour.items[:] = [float(c) for c in shell.colour]
    if shell.flash is not None:
        flash = r.get('MuzzleFlash_CustomParameter')
        if not (isinstance(flash, dsgo.Node) and len(flash.items) == 10 and isinstance(flash.items[8], dsgo.Node)
                and len(flash.items[8].items) == 4):
            raise ValueError(f'{shell.template} 的枪口焰参数不是预期的 10 项')
        flash.items[8].items[:] = [float(c) for c in shell.flash]
    if shell.muzzle is not None:
        _move_muzzle(r, shell.template, *shell.muzzle)
    if shell.model is not None:   # its own gun to see (coax_model), its muzzle node COAX_GUN_NODE as the template's
        ref = r.get('animation_model').items[0]
        ref.items[0] = 'app:/Weapon/' + shell.model.lower()
        ref.items[1] = shell.model.rsplit('.', 1)[0].lower() + '.mdb'
    _named(r, store)
    return dsgo.write(doc)


def _move_muzzle(r: dsgo.Node, template: str, was: tuple[float, float, float], at: tuple[float, float, float]) -> None:
    """Move weapon root `r`'s single muzzle (animation_model[2], mab_muzzles) from `was` to `at`: ValueError unless the
    template has exactly one muzzle, it is at `was`, and no other locator of the block shares its vec4."""
    model = r.get('animation_model')
    block = bytearray(model.items[2].data)
    muzzles = mab_muzzles(bytes(block))
    if len(muzzles) != 1:
        raise ValueError(f'{template} 不是单炮口的武器')
    vec = muzzles[0][2]
    got = struct.unpack_from('<3f', block, vec)
    if any(abs(g - w) > 1e-3 for g, w in zip(got, was)):
        raise ValueError(f'{template} 的炮口在 {got}，不是 {was}')
    # Every list's records (0x20 each, from the lists' headers to the u32 at 0x18; pylib/mab.py) name their three vec4s
    # at +0xC / +0x10 / +0x14, each vec4 stored once: the muzzle's must be named by its own record alone.
    heads, end = struct.unpack_from('<2I', block, 0x14)
    first = heads + 8 * struct.unpack_from('<H', block, 0x0C)[0]
    users = [(rec, off) for rec in range(first, end, 0x20) for off in (0xC, 0x10, 0x14)
             if rec + struct.unpack_from('<i', block, rec + off)[0] == vec]
    if len(users) != 1:
        raise ValueError(f'{template} 的炮口坐标被 {len(users)} 处共用')
    struct.pack_into('<3f', block, vec, *at)
    model.items[2] = dsgo.Blob(bytes(block), model.items[2].kind)


def _coax_mount_of(name: str) -> CoaxMount:
    for tank, mount in COAX_MOUNTS.items():
        if coax_model_name(tank).lower() == name.lower():
            return mount
    raise ValueError(f'{name} 不是同轴机枪模型')


def coax_parts(mount: CoaxMount, segs: int = 14) -> dict[str, list]:
    """The coax as it shows outside the mantlet, in the holder bone's frame (CoaxMount.port), as procmesh Parts by
    COAX_GUN_TINTS: the port collar (from collar_in behind the face to collar_out proud of it, the barrel's hole in its
    face), the bare barrel out of the hole, the flash hider (a short taper up from the barrel, its face with the bore's
    dark mouth), the bore. Every surface has its own vertices at its edges (crisp caps: the smooth normals of procmesh
    would round a shared rim)."""
    from procmesh import Part
    gun = mount.gun
    x, y, z = mount.port
    skin = [(1, 1.0)]
    parts = {k: Part(0, tint) for k, tint in COAX_GUN_TINTS.items()}

    def ring(part, zz: float, r: float, k: int) -> list[int]:
        return [part.add((x + r * math.cos(2 * math.pi * s / segs), y + r * math.sin(2 * math.pi * s / segs), zz),
                         skin, (s / segs, k)) for s in range(segs)]

    def side(part, z0: float, r0: float, z1: float, r1: float, inward: bool = False) -> None:
        """The surface between two rings (z0, r0) -> (z1, r1), facing out (or in: a hole's wall)."""
        a, b = ring(part, z0, r0, 0), ring(part, z1, r1, 1)
        for s in range(segs):
            t = (s + 1) % segs
            quad = ((a[s], b[s], b[t]), (a[s], b[t], a[t]))
            for tri in quad:
                part.tris.append(tri[::-1] if inward else tri)

    def annulus(part, zz: float, r_out: float, r_in: float, forward: bool = True) -> None:
        """A flat ring at zz between r_in and r_out (r_in 0: a disc), facing +z (or -z)."""
        o = ring(part, zz, r_out, 0)
        if r_in <= 0.0:
            c = part.add((x, y, zz), skin, (0.5, 0.5))
            for s in range(segs):
                tri = (c, o[s], o[(s + 1) % segs])
                part.tris.append(tri if not forward else tri[::-1])
            return
        i = ring(part, zz, r_in, 1)
        for s in range(segs):
            t = (s + 1) % segs
            for tri in ((o[s], i[s], i[t]), (o[s], i[t], o[t])):
                part.tris.append(tri if not forward else tri[::-1])

    hole = gun.barrel_r + 0.006                 # the port's hole round the barrel
    back, face = z - mount.collar_in, z + mount.collar_out
    c = parts['collar']
    side(c, back, mount.collar_r, face, mount.collar_r)
    annulus(c, face, mount.collar_r, hole)
    annulus(c, back, mount.collar_r, 0.0, forward=False)
    side(parts['bore'], face, hole, face - 0.03, hole, inward=True)   # the hole's dark wall
    annulus(parts['bore'], face - 0.03, hole, 0.0)                    # and its bottom
    hider0 = face + gun.bare
    b = parts['barrel']
    side(b, face - 0.03, gun.barrel_r, hider0, gun.barrel_r)
    h = parts['hider']
    taper = min(0.015, gun.hider / 4)
    side(h, hider0, gun.barrel_r, hider0 + taper, gun.hider_r)
    muzzle = hider0 + gun.hider
    side(h, hider0 + taper, gun.hider_r, muzzle, gun.hider_r)
    bore_r = 0.0055                             # the flash hider's mouth (7.62 mm bore, its cone opening out)
    annulus(h, muzzle, gun.hider_r, bore_r)
    side(parts['bore'], muzzle, bore_r, muzzle - 0.04, bore_r * 0.7, inward=True)
    annulus(parts['bore'], muzzle - 0.04, bore_r * 0.7, 0.0)
    return parts


def coax_model(game: Game, name: str) -> bytes:
    """The coaxial gun's model `name` (coax_model_name, its COAX_MOUNTS mount): coax_parts made a rigid model in the
    stock COAX_GUN_STOCK archive in place of its gun (its gunmetal material, each part's UVs on the even patch of its
    texture nearest the part's tint, procmesh.uv_boxes), its gun bone named COAX_GUN_NODE (the muzzle locator's node)
    with no scale (the stock bone's 1.3: the 2026-10-11 model stood 30% out of place), its bounds the parts', its model
    member named after `name` (each archive its own; the textures are the stock gun's, by the archive's texture map)."""
    import mdb
    import procmesh
    import numpy as np
    mount = _coax_mount_of(name)
    rab = mdb.rab_read(game.read('WEAPON', COAX_GUN_STOCK))
    member = next(f for f in rab.files if f.name.lower().endswith('.mdb'))
    md = mdb.mdb_read(member.data)
    if len(md.objects) != 1 or len(md.objects[0].meshes) != 1 or len(md.bones) != 2 or md.bones[1].kind != 1:
        raise ValueError(f'{COAX_GUN_STOCK} 不是预期的单网格机枪模型')
    tmpl = md.objects[0].meshes[0]
    if tmpl.flags[1] or {e.name.lower() for e in tmpl.elems} != {'position', 'normal', 'binormal', 'tangent', 'texcoord'}:
        raise ValueError(f'{COAX_GUN_STOCK} 的网格不是预期的刚体顶点格式')
    albedos = procmesh.albedos(rab, md)
    meshes, every = [], []
    for k, part in enumerate(p for p in coax_parts(mount).values() if p.pos):
        boxes = procmesh.uv_boxes(md, albedos, part.tint)
        vdata, idx = procmesh.pack(part, tmpl, boxes, 1.0)
        meshes.append(mdb.Mesh(tmpl.flags, tmpl.material, tmpl.unk08, tmpl.vsize, tmpl.elems, k, vdata, idx))
        every.extend(part.pos)
    pts = np.array(every)
    lo, hi = pts.min(axis=0), pts.max(axis=0)
    gun = md.bones[1]
    gun.local = mdb.ident()
    gun.inv_bind = mdb.ident()
    gun.half = [float(v) for v in (hi - lo) / 2] + [1.0]
    gun.centre = [float(v) for v in (hi + lo) / 2] + [1.0]
    names = list(md.names)
    names[gun.name] = COAX_GUN_NODE
    md = replace(md, names=names, objects=[replace(md.objects[0], meshes=meshes)])
    data = mdb.mdb_write(md)
    if mdb.mdb_write(mdb.mdb_read(data)) != data:
        raise ValueError(f'{name}: 模型写回不一致')
    member.stored = mdb.cmpl_compress(data)
    member.name = name.rsplit('.', 1)[0].lower() + '.mdb'
    return mdb.rab_write(rab)


# A round's glow declaration (src/gpufx/gpufx_glow.h, docs/round-glow.md): one more Ammo_CustomParameter entry
# [GLOW_MARK, r, g, b, intensity, size(, back)] at the first index the class's constructor never reads (GLOW_FIRST_FREE: the
# stock never looks there). MissileBullet01's CP[12] is read only when present (a bool, 0 when absent): padded with 0.
GLOW_MARK = 7146.0
GLOW_FIRST_FREE = {'MissileBullet01': 13, 'MissileBullet02': 16}
GLOW_PAD = {'MissileBullet01': {12: 0.0}}

def declare_glow(cp: dsgo.Node, ammo_class: str, glow: tuple[float, ...]) -> None:
    """Appends the glow declaration `glow` (r, g, b, intensity, size, optionally back: m behind the position along the
    nose) to the Ammo_CustomParameter `cp` of a round of `ammo_class`, padding it up to the class's first free index."""
    free = GLOW_FIRST_FREE.get(ammo_class)
    if free is None:
        raise ValueError(f'{ammo_class} 的 Ammo_CustomParameter 读法未核实，不能声明发光')
    if len(glow) not in (5, 6):
        raise ValueError(f'发光声明应为 5 或 6 个数: {glow}')
    r, g, b, intensity, size = glow[:5]
    back = glow[5] if len(glow) == 6 else 0.0
    peak = max(r, g, b)
    if (min(r, g, b) < 0.0 or not 0.0 < peak <= 1.0 or not 0.0 < intensity <= 64.0 or not 0.0 < size <= 50.0
            or not 0.0 <= back <= 50.0):
        raise ValueError(f'发光声明越界: {glow}')
    if len(cp.items) > free:
        raise ValueError(f'{ammo_class} 的 Ammo_CustomParameter 已有 {len(cp.items)} 项，超过首个空闲位 {free}')
    pad = GLOW_PAD.get(ammo_class, {})
    while len(cp.items) < free:
        if len(cp.items) not in pad:
            raise ValueError(f'{ammo_class} 的 Ammo_CustomParameter 只有 {len(cp.items)} 项，缺原版读的参数')
        cp.items.append(pad[len(cp.items)])
    cp.items.append(dsgo.Node([GLOW_MARK, *(float(v) for v in glow)]))

def _missile_sgo(game: Game, store: Store, rounds: int) -> bytes:
    missile = store.weapon
    doc = dsgo.parse(game.read('WEAPON', JET_MISSILE_STOCK))
    r = doc.root
    fire, cp, cone = r.get('FireSe'), r.get('Ammo_CustomParameter'), r.get('LockonAngle')
    if (r.get('AmmoClass') != 'MissileBullet01' or len(fire.items) != 6 or len(cp.items) != 12
            or len(cp.items[11].items) != 6 or len(cp.items[3].items) != 3 or len(cone.items) != 2):
        raise ValueError(f'{JET_MISSILE_STOCK} 不是预期的直升机导弹')
    for node, (cue, volume, reach) in ((fire, JET_MISSILE_FIRE_SE), (cp.items[11], JET_MISSILE_FLIGHT_SE)):
        node.items[1], node.items[2], node.items[5] = cue, volume, reach
    for key, value in missile.params(rounds).items():
        if r.get(key) is None:
            raise ValueError(f'{JET_MISSILE_STOCK} 缺少 {key}')
        r.set(key, value)
    for i, value in missile.motion().items():
        if i == 3:
            cp.items[3].items[0], cp.items[3].items[1], cp.items[3].items[2] = value
        else:
            cp.items[i] = value
    cone.items[0] = cone.items[1] = missile.lock_cone
    plume = plume_of(store)
    if plume is not None:
        if len(cp.items[7].items) != 2 or not isinstance(cp.items[2], float):
            raise ValueError(f'{JET_MISSILE_STOCK} 的 CP[2] / CP[7] 不是预期的尾烟寿命 / [点火延迟, 衰减]')
        cp.items[2] = plume.trail
        cp.items[7].items[:] = [plume.ignite, 1.0]
        colour = r.get('AmmoColor')
        if not isinstance(colour, dsgo.Node) or len(colour.items) != 4:
            raise ValueError(f'{JET_MISSILE_STOCK} 的 AmmoColor 不是 4 个数')
        colour.items[:] = [float(c) for c in plume.colour]
    if missile.muzzle is not None:
        _fire_from(game, r, missile.muzzle)
    if store is STORES['SALVO']:
        r.set('LockonHoldTime', 6.0)
        # Keep the vehicle holder and its four animated launch points. Only the projectile class uses
        # the stock split-missile contract, including inherited lock points on the guided submunitions.
        source = dsgo.parse(game.read('WEAPON', 'V_402ROCKET_ROCKETCANNON02.SGO')).root
        split = copy.deepcopy(source.get('Ammo_CustomParameter').items[12:])
        if (source.get('AmmoClass') != 'MissileBullet02' or len(split) != 4
                or len(split[3].items) != 19 or split[3].items[4] != 'MissileBullet01'):
            raise ValueError('unexpected split missile template')
        child = split[3].items[13]
        if (len(split[3].items[12].items) != 4 or len(child.items) != 12 or len(child.items[10].items) != 2
                or len(cp.items[7].items) != 2 or not isinstance(cp.items[2], float)
                or not isinstance(child.items[2], float)):
            raise ValueError('unexpected split missile template')
        split[0].items[:] = [1.0, SALVO_SPLIT_AFTER]   # Split after launch even when the target has already died.
        split[1].items[:] = [0.0, 0.0, SALVO_SPLIT_CONE]
        split[3].items[2] = float(SALVO_CHILDREN)
        split[3].items[5] = SALVO_CHILD_SPEED
        split[3].items[12].items[:] = list(SALVO_CHILD_COLOUR)
        child.items[4], child.items[5], child.items[6] = SALVO_CHILD_ACCEL, SALVO_CHILD_TURN, SALVO_CHILD_TOP
        child.items[8] = SALVO_CHILD_HOMING_DELAY
        child.items[10].items[:] = list(SALVO_CHILD_SPREAD)
        child.items[2] = SALVO_CHILD_TRAIL
        declare_glow(child, split[3].items[4], SALVO_CHILD_GLOW)
        r.set('AmmoClass', 'MissileBullet02')
        r.set('AmmoGravityFactor', SALVO_DROP_GRAVITY)
        r.get('AmmoColor').items[:] = list(SALVO_COLOUR)
        cp.items[7].items[:] = list(SALVO_DROP)
        cp.items[2] = SALVO_TRAIL
        # Native guidance for the carrier (src/guidance.cpp's PN), not MissileBullet01's plugin mode.
        cp.items[5] = SALVO_TURN
        cp.items[8], cp.items[9] = SALVO_HOMING_DELAY, 0.0
        cp.items.extend(split)
        declare_glow(cp, 'MissileBullet02', SALVO_GLOW)
        flash_host = dsgo.parse(game.read('WEAPON', JET_GUN_FILES['EDF6VC_JET_GUN_L.SGO'])).root
        flash = copy.deepcopy(flash_host.get('MuzzleFlash_CustomParameter'))
        if flash_host.get('MuzzleFlash') != 'MuzzleFlash02' or len(flash.items) != 10:
            raise ValueError('unexpected launcher flash template')
        flash.items[2] = 0.72
        flash.items[8].items[:] = [3.0, 1.65, 0.45, 1.0]
        flash.items[9] = 0.0
        r.set('MuzzleFlash', 'MuzzleFlash02')
        r.set('MuzzleFlash_CustomParameter', flash)
    _look(game, r, missile.look, JET_MISSILE_STOCK)
    if store is STORES['SALVO']:
        r.get('resource').items.append(cp.items[15].items[14])
    _named(r, store)
    return dsgo.write(doc)


def _fire_from(game: Game, r: dsgo.Node, stock: str) -> None:
    """Give weapon root `r` the muzzle list (animation_model[2]) of stock weapon `stock`: one muzzle, else ValueError."""
    theirs = dsgo.parse(game.read('WEAPON', stock)).root.get('animation_model')
    mine = r.get('animation_model')
    if len(mab_muzzles(theirs.items[2].data)) != 1 or theirs.items[0] != mine.items[0]:
        raise ValueError(f'{stock} 不是单炮口的原版炮')
    mine.items[2] = copy.deepcopy(theirs.items[2])


def _bomb_sgo(game: Game, store: Store, rounds: int) -> bytes:
    """The stock unguided bomb (GrenadeBullet01) with an impact fuse (CP[0] 0) and no bounce (CP[3] 0); docs/stores-re.md §5."""
    bomb = store.weapon
    doc = dsgo.parse(game.read('WEAPON', JET_BOMB_STOCK))
    r = doc.root
    cp = r.get('Ammo_CustomParameter')
    if r.get('AmmoClass') != 'GrenadeBullet01' or len(cp.items) != 6:
        raise ValueError(f'{JET_BOMB_STOCK} 不是预期的直升机炸弹')
    cp.items[0], cp.items[3] = 0.0, 0.0   # no CP[6] in the stock list: no random life
    for key, value in bomb.params(rounds).items():
        if r.get(key) is None:
            raise ValueError(f'{JET_BOMB_STOCK} 缺少 {key}')
        r.set(key, value)
    _look(game, r, bomb.look, JET_BOMB_STOCK)
    _named(r, store)
    return dsgo.write(doc)


def model_length(raw: bytes) -> float:
    """The length (m, along z: a round's model points down +z) of the model in the archive `raw` at AmmoSize 1."""
    from graft_pure import mesh_positions
    from mdb import mdb_read, rab_read
    md = mdb_read(next(f for f in rab_read(raw).files if f.name.lower().endswith('.mdb')).data)
    zs = [p[2] for o in md.objects for me in o.meshes for p in mesh_positions(me)]
    return max(zs) - min(zs)


def _look(game: Game, r: dsgo.Node, look: Look, stock: str) -> None:
    """A store weapon's round as `look` (see STORE_MODELS), in place: AmmoModel the model, AmmoSize its real length,
    AmmoHitSizeAdjust keeping the template's contact sphere (AmmoSize x AmmoHitSizeAdjust: what it hits with stays as it
    was), the preload list (`resource`) that model in place of the template's one. The model is measured on this
    Root.cpk: one of another length would fly at another length (ValueError)."""
    length = model_length(game.read('WEAPON', f'{look.model}.rab'))   # KeyError: not there
    if abs(length - STORE_MODELS[look.model]) > STORE_MODEL_SLACK:
        raise ValueError(f'{look.model}.rab 长 {length:.3f} m，不是 STORE_MODELS 的 {STORE_MODELS[look.model]} m')
    res = r.get('resource')
    model = r.get('AmmoModel')
    if not isinstance(res, dsgo.Node) or [str(x).lower() for x in res.items] != [str(model).lower()]:
        raise ValueError(f'{stock} 的 resource 不是只预载它的弹体模型 {model}')
    contact = r.get('AmmoSize') * r.get('AmmoHitSizeAdjust')
    r.set('AmmoModel', look.path)
    r.set('AmmoSize', look.size)
    r.set('AmmoHitSizeAdjust', contact / look.size)
    res.items[:] = [look.path]


# The teleportation ships' portal laser (src/carrierlaser.cpp): two DemoIndirectFire objects (the class of
# the missions' DEMOSATELLITELASER*: an IndirectFireControl at +0x170 that fires indirect_fire_param's
# rounds at its own position, docs/carrier-laser-re.md), made from DEMOSATELLITELASER18.SGO. The plugin
# fires them from the ship's hatch (IFC +0x2F9 / +0x300) at its target and sets their damage itself.
# indirect_fire_param (index: meaning, from the IFC's parser 0x2B5F40): 2 rounds, 3 frames between rounds,
# 4 bullet class, 5 speed (m a frame), 6 gravity factor, 7 beam size, 8 hit size factor, 9 blast radius
# (AmmoExplosion; 0: none), 10 life (frames), 11 penetrates, 12 colour, 13 the bullet class's custom parameter,
# 14 a model of its own (0: none), 15 frames before the first round, 16 fire sound looped, 17 fire sound, 18 hit sound.
PORTAL_LASER_STOCK = 'DEMOSATELLITELASER18.SGO'
# name -> (rounds, gap, size, life, colour, fire sound once)
PORTAL_LASER_FILES: dict[str, tuple[int, int, float, int, tuple[float, float, float, float], bool]] = {
    # The aim light: a thin red beam, a round every frame living 6 (so it follows the aim), 12.5 s of rounds at
    # most (the charge is 12 s, src/carrierlaser.cpp kChargeMs; the plugin ends it sooner), no damage (the
    # plugin sets 0).
    'EDF6VC_PORTAL_SIGHT.SGO': (750, 0, 1.5, 6, (3.0, 0.15, 0.1, 1.0), True),
    # The main shot: one wide violet beam living 45 frames (0.75 s); its damage is CarrierLaserDamage.
    'EDF6VC_PORTAL_LASER.SGO': (1, 0, 8.0, 45, (2.5, 0.4, 3.0, 1.0), False),
}


def portal_lasers(game: Game) -> dict[str, bytes]:
    """The portal laser's two DemoIndirectFire SGOs (PORTAL_LASER_FILES)."""
    out = {}
    for name, (rounds, gap, size, life, colour, once) in PORTAL_LASER_FILES.items():
        version, m = sgo.read(game.read('OBJECT', PORTAL_LASER_STOCK))
        p = m['indirect_fire_param']
        if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
                or p[4] != 'LaserBullet02'):
            raise ValueError(f'{PORTAL_LASER_STOCK} 不是预期的卫星激光')
        p[2], p[3], p[7], p[9], p[10] = rounds, gap, size, 0.0, life
        p[12] = list(colour)
        p[14], p[15], p[16] = 0, 0, 0
        if isinstance(p[17], list) and p[17]:
            p[17][0] = 1.0 if once else 0   # 1: the fire sound once for all rounds (the player's satellite)
        m['indirect_fire_damage'] = 0.0     # the plugin sets the damage (IFC +0xDC)
        out[name] = sgo.write(version, m)
    return out


# The EMC's charged beam (src/emc.cpp, docs/emc-re.md; the user 2026-10-06: "蓄力 → 一道粗光束持续 2–3 秒 ... 贯穿，沿途建筑
# ... 一起摧毁 ... 几百米范围的爆炸"): the EMC (V510_MASER, Vehicle510_Maser, the Air Raider's EMC / EMCS / EMCX requests)
# fires its stock weapon EMC_STOCK_WEAPON as a 1000-round burst (FireBurstCount), a round a frame for 16.7 s. With the
# plugin's EmcBeam on, the trigger charges instead and the burst's whole damage (AmmoDamage x FireBurstCount, times the
# request's damage factor the game puts on the weapon) goes out as one beam. Four DemoIndirectFire SGOs (the IFC as the
# portal laser's above; the plugin owns, aims and deletes them, docs/carrier-laser-re.md §2-3):
#   EMC_BEAM_FILE   the beam: the missions' satellite laser (LaserBullet02, penetrating: it passes through every enemy on
#                   its line) remade thick in the maser's blue, a round a frame for up to EMC_BEAM_ROUNDS frames (the
#                   plugin ends it at EmcBeamSec), each EMC_BEAM_SPEED m a frame for EMC_BEAM_LIFE frames: EMC_BEAM_RANGE,
#                   the stock round's reach (AmmoSpeed x AmmoAlive, checked against the stock SGO). Its shot sound once.
#   EMC_SIGHT_FILE  the charge's glow: the same beam thin and dim, silent, no damage (the plugin thickens it as it charges).
#   EMC_BREAK_FILE  a break charge (tools/make_jets.py impact_charge's recipe, as the drill's): a EMC_BREAK_RADIUS m blast,
#                   3 m or more so it takes its damage off the buildings it meets (docs/drill-re.md §3), fired at each
#                   building on the beam's line; a short flight, so it bursts only on what it meets there.
#   EMC_BLAST_FILE  the blast at the beam's end: the same, its radius EMC_BLAST_RADIUS (the plugin writes EmcBlastRadius over it).
# The charges are thin and beam-coloured (they fly a few metres inside the beam); the SGOs' damage is 0 (the plugin's).
EMC_STOCK_WEAPON = 'V_510_MASER_THUNDER01.SGO'
EMC_BEAM_FILE = 'EDF6VC_EMC_BEAM.SGO'
EMC_SIGHT_FILE = 'EDF6VC_EMC_SIGHT.SGO'
EMC_BREAK_FILE = 'EDF6VC_EMC_BREAK.SGO'
EMC_BLAST_FILE = 'EDF6VC_EMC_BLAST.SGO'
EMC_FILES = (EMC_BEAM_FILE, EMC_SIGHT_FILE, EMC_BREAK_FILE, EMC_BLAST_FILE)
EMC_BEAM_RANGE = 600.0                   # m: the stock round's reach (8 m a frame x 75 frames)
EMC_BEAM_SPEED, EMC_BEAM_LIFE = 100.0, 6
EMC_BEAM_ROUNDS, EMC_SIGHT_ROUNDS = 330, 630   # 5.5 s / 10.5 s of rounds: past EmcBeamSec's / EmcChargeSec's top (5 / 10 s)
EMC_BEAM_SIZE, EMC_SIGHT_SIZE = 12.0, 0.6
EMC_BEAM_COLOUR = (0.3, 0.6, 3.0, 1.0)   # the stock maser's (0.14, 0.3, 2.5) blue, brighter
EMC_SIGHT_COLOUR = (0.14, 0.3, 2.5, 1.0)
EMC_BEAM_SHOT_PITCH = 0.8                # the satellite's shot a little lower
EMC_BREAK_RADIUS, EMC_BREAK_SPEED, EMC_BREAK_LIFE = 12.0, 2.5, 4
EMC_BLAST_RADIUS, EMC_BLAST_SPEED, EMC_BLAST_LIFE = 300.0, 3.0, 4
EMC_CHARGE_SIZE, EMC_CHARGE_HIT = 0.5, 2.0   # the charges' round: thin (hidden in the beam), a 1 m hit radius


def _emc_beam(game: Game, rounds: int, size: float, colour: tuple[float, ...], shot_volume: float) -> bytes:
    """The satellite laser (PORTAL_LASER_STOCK) made one of the EMC's beams (see EMC_BEAM_FILE)."""
    version, m = sgo.read(game.read('OBJECT', PORTAL_LASER_STOCK))
    p = m['indirect_fire_param']
    if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
            or p[4] != 'LaserBullet02' or p[11] != 1 or not isinstance(p[17], list) or len(p[17]) != 6
            or not isinstance(p[18], list) or len(p[18]) != 6):
        raise ValueError(f'{PORTAL_LASER_STOCK} 不是预期的卫星激光')
    p[0] = [0.0, 0.0]
    p[2], p[3], p[5], p[7], p[9], p[10] = rounds, 0, EMC_BEAM_SPEED, size, 0.0, EMC_BEAM_LIFE
    p[12] = list(colour)
    p[14], p[15], p[16] = 0, 0, 0
    p[17][0] = 1.0                      # its shot sound once for all its rounds
    p[17][2] = shot_volume
    p[17][3] = EMC_BEAM_SHOT_PITCH
    p[18][2] = 0.0                      # no hit sound: a round a frame would play it 60 times a second
    m['indirect_fire_damage'] = 0.0     # the plugin sets the damage (IFC +0xDC)
    return sgo.write(version, m)


def _emc_charge(game: Game, radius: float, speed: float, life: int) -> bytes:
    """An impact charge (tools/make_jets.py impact_charge's recipe, here so pylib needs no tool) for the EMC: a `radius` m
    blast, `speed` m a frame for `life` frames, thin and in the beam's colour."""
    version, m = sgo.read(game.read('OBJECT', 'DEMOGUNSHIPFIRESOLID.SGO'))
    p = m.get('indirect_fire_param')
    if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
            or p[4] != 'SolidBullet01' or 'indirect_fire_damage' not in m):
        raise ValueError('DEMOGUNSHIPFIRESOLID.SGO 不是预期的炮舰炮弹')
    for i, value in ((2, 1), (3, 0), (5, speed), (6, 0), (9, radius), (10, life), (11, 0), (15, 0)):
        p[i] = int(value) if isinstance(p[i], int) else float(value)   # each keeps its node type
    p[0] = [0.0, 0.0]
    p[7], p[8] = EMC_CHARGE_SIZE, EMC_CHARGE_HIT
    p[12] = list(EMC_BEAM_COLOUR)
    m['indirect_fire_damage'] = 0.0
    return sgo.write(version, m)


def emc_rounds(game: Game) -> dict[str, bytes]:
    """The EMC's four DemoIndirectFire SGOs (EMC_FILES), {Mods/OBJECT file: bytes}."""
    return {EMC_BEAM_FILE: _emc_beam(game, EMC_BEAM_ROUNDS, EMC_BEAM_SIZE, EMC_BEAM_COLOUR, 1.0),
            EMC_SIGHT_FILE: _emc_beam(game, EMC_SIGHT_ROUNDS, EMC_SIGHT_SIZE, EMC_SIGHT_COLOUR, 0.0),
            EMC_BREAK_FILE: _emc_charge(game, EMC_BREAK_RADIUS, EMC_BREAK_SPEED, EMC_BREAK_LIFE),
            EMC_BLAST_FILE: _emc_charge(game, EMC_BLAST_RADIUS, EMC_BLAST_SPEED, EMC_BLAST_LIFE)}


def emc_stock(game: Game) -> dict[str, float]:
    """The stock EMC weapon's numbers the beam rests on (EMC_STOCK_WEAPON): AmmoDamage, AmmoCount, FireBurstCount,
    AmmoSpeed, AmmoAlive, and its reach (speed x life)."""
    w = sgo.load(data=game.read('WEAPON', EMC_STOCK_WEAPON))
    if not isinstance(w, dict) or w.get('xgs_scene_object_class') != 'Weapon_VehicleMaser':
        raise ValueError(f'{EMC_STOCK_WEAPON} 不是原版 EMC 的武器（Weapon_VehicleMaser）')
    out = {k: float(w[k]) for k in ('AmmoDamage', 'AmmoCount', 'FireBurstCount', 'AmmoSpeed', 'AmmoAlive')}
    out['reach'] = out['AmmoSpeed'] * out['AmmoAlive']
    return out


def check_emc(files: dict[str, bytes], game: Game | None = None) -> None:
    """The EMC's SGOs as src/emc.cpp fires them: the beam and the sight LaserBullet02, penetrating, no blast, a round a frame
    reaching EMC_BEAM_RANGE, enough rounds for the longest beam / charge, the sight silent; the charges SolidBullet01, one
    round, not penetrating, the break charge's blast 3 m or more (it breaks buildings), every damage 0 (the plugin's).
    With the game: EMC_BEAM_RANGE is the stock weapon's reach."""
    for name in (EMC_BEAM_FILE, EMC_SIGHT_FILE):
        m = sgo.load(data=files[name])
        p = m['indirect_fire_param']
        assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == 'LaserBullet02' and p[11] == 1, name
        assert p[3] == 0 and p[9] == 0.0 and p[15] == 0 and m['indirect_fire_damage'] == 0.0, (name, p)
        assert abs(p[5] * p[10] - EMC_BEAM_RANGE) < 1e-3, (name, p[5], p[10])
        assert p[17][0] == 1.0 and p[18][2] == 0.0, name
    beam, sight = (sgo.load(data=files[n])['indirect_fire_param'] for n in (EMC_BEAM_FILE, EMC_SIGHT_FILE))
    assert beam[2] >= 5.0 * 60 and sight[2] >= 10.0 * 60 and sight[17][2] == 0.0 and beam[17][2] > 0.0
    assert beam[7] > sight[7]
    for name, radius in ((EMC_BREAK_FILE, EMC_BREAK_RADIUS), (EMC_BLAST_FILE, EMC_BLAST_RADIUS)):
        m = sgo.load(data=files[name])
        p = m['indirect_fire_param']
        assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == 'SolidBullet01', name
        assert p[2] == 1 and p[11] == 0 and p[9] == radius >= 3.0 and m['indirect_fire_damage'] == 0.0, (name, p)
    if game is not None:
        stock = emc_stock(game)
        assert abs(stock['reach'] - EMC_BEAM_RANGE) < 1e-3, stock


# ------------------------------------------------------------------------------------------ the ultimate strike
# The ultimate strike's rounds (src/ultimate.cpp, src/ultimate_plan.h, docs/plan-2026-10-19-ultimate.md; the user,
# 2026-10-19: 「特效拉满 昏天暗地」): DemoIndirectFire SGOs the plugin owns, aims and grows as the EMC's (jet_bay.cpp kEmcFiles,
# after the Sazabi's and the Proteus's), the satellite laser (PORTAL_LASER_STOCK) remade for each part of the show, and the
# EMC's impact charge for the blasts. Every damage 0 in the SGO: the plugin writes it (IFC +0xDC) from the weapon's.
#   ULT_SIGHT_FILE  the charge's aim beams: thin crimson, a round a frame for the whole charge, silent (the plugin grows them)
#   ULT_BOLT_FILE   a lightning bolt: a blue-white flash ULT_BOLT_ROUNDS frames long, its crack once
#   ULT_SPARK_FILE  the burst where a bolt lands and along a shock wave's front (ULT_SPARK_RADIUS m: src/ultimate_plan.h kSparkRadius)
#   ULT_BEAM_FILE   the main beam: the satellite's thickest, violet, a round a frame for the whole cannon phase, a deep shot
#   ULT_CORE_FILE   its white-hot core, silent
#   ULT_BLAST_FILE  a ring's blast (ULT_BLAST_RADIUS m: src/ultimate_plan.h kRingBlast)
#   ULT_GATHER_FILE the gather's pulse: a short gold-white laser, exactly ULT_GATHER_REACH m (src/ultimate_plan.h
#                   kGatherReach: fired from a point that far short of the beam's start, it ends on it), silent
ULT_SIGHT_FILE = 'EDF6VC_ULT_SIGHT.SGO'
ULT_BOLT_FILE = 'EDF6VC_ULT_BOLT.SGO'
ULT_SPARK_FILE = 'EDF6VC_ULT_SPARK.SGO'
ULT_BEAM_FILE = 'EDF6VC_ULT_BEAM.SGO'
ULT_CORE_FILE = 'EDF6VC_ULT_CORE.SGO'
ULT_BLAST_FILE = 'EDF6VC_ULT_BLAST.SGO'
ULT_GATHER_FILE = 'EDF6VC_ULT_GATHER.SGO'
ULT_FILES = (ULT_SIGHT_FILE, ULT_BOLT_FILE, ULT_SPARK_FILE, ULT_BEAM_FILE, ULT_CORE_FILE, ULT_BLAST_FILE,
             ULT_GATHER_FILE)   # EmcRound's order
ULT_BOLT_ROUNDS = 8                     # src/ultimate_plan.h kBoltFrames
ULT_SPARK_RADIUS, ULT_BLAST_RADIUS = 40.0, 55.0
# (rounds, size, colour, speed m a frame, life frames, shot volume, shot pitch): reach = speed x life, past the farthest
# start: the aims' ring (900 m up, 520 m out) to the area's far edge, a bolt's 700 m (plus its lean), the beam's 1300 m.
ULT_BEAMS: dict[str, tuple[int, float, tuple[float, ...], float, int, float, float]] = {
    ULT_SIGHT_FILE: (760, 0.8, (3.0, 0.2, 0.45, 1.0), 140.0, 10, 0.0, 1.0),
    ULT_BOLT_FILE: (ULT_BOLT_ROUNDS, 2.5, (1.8, 2.4, 3.0, 1.0), 160.0, 6, 0.7, 1.7),
    ULT_BEAM_FILE: (500, 48.0, (2.8, 0.45, 3.0, 1.0), 150.0, 10, 1.0, 0.6),
    ULT_CORE_FILE: (500, 18.0, (3.0, 2.6, 3.0, 1.0), 150.0, 10, 0.0, 1.0),
    ULT_GATHER_FILE: (200, 0.6, (3.0, 2.4, 1.4, 1.0), 12.0, 10, 0.0, 1.0),
}
ULT_GATHER_REACH = 120.0
ULT_REACH = {ULT_SIGHT_FILE: 1300.0, ULT_BOLT_FILE: 760.0, ULT_BEAM_FILE: 1300.0, ULT_CORE_FILE: 1300.0,
             ULT_GATHER_FILE: ULT_GATHER_REACH}
ULT_CHARGE_COLOUR = (2.2, 0.6, 3.0, 1.0)


def _ult_beam(game: Game, rounds: int, size: float, colour: tuple[float, ...], speed: float, life: int, volume: float,
              pitch: float) -> bytes:
    """The satellite laser (PORTAL_LASER_STOCK) made one of the ultimate's beams (see ULT_BEAMS)."""
    version, m = sgo.read(game.read('OBJECT', PORTAL_LASER_STOCK))
    p = m['indirect_fire_param']
    if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
            or p[4] != 'LaserBullet02' or p[11] != 1 or not isinstance(p[17], list) or len(p[17]) != 6
            or not isinstance(p[18], list) or len(p[18]) != 6):
        raise ValueError(f'{PORTAL_LASER_STOCK} 不是预期的卫星激光')
    p[0] = [0.0, 0.0]
    p[2], p[3], p[5], p[7], p[9], p[10] = rounds, 0, speed, size, 0.0, life
    p[12] = list(colour)
    p[14], p[15], p[16] = 0, 0, 0
    p[17][0] = 1.0                      # its shot sound once for all its rounds
    p[17][2] = volume
    p[17][3] = pitch
    p[18][2] = 0.0                      # no hit sound: a round a frame would play it 60 times a second
    m['indirect_fire_damage'] = 0.0     # the plugin sets the damage (IFC +0xDC)
    return sgo.write(version, m)


def ult_rounds(game: Game) -> dict[str, bytes]:
    """The ultimate's DemoIndirectFire SGOs (ULT_FILES), {Mods/OBJECT file: bytes}."""
    out = {name: _ult_beam(game, *spec) for name, spec in ULT_BEAMS.items()}
    for name, radius in ((ULT_SPARK_FILE, ULT_SPARK_RADIUS), (ULT_BLAST_FILE, ULT_BLAST_RADIUS)):
        version, m = sgo.read(_emc_charge(game, radius, EMC_BLAST_SPEED, EMC_BLAST_LIFE))
        m['indirect_fire_param'][12] = list(ULT_CHARGE_COLOUR)
        out[name] = sgo.write(version, m)
    return {name: out[name] for name in ULT_FILES}


def check_ult(files: dict[str, bytes]) -> None:
    """The ultimate's SGOs as src/ultimate.cpp fires them: the beams LaserBullet02, penetrating, no blast, a round a frame,
    reaching past their farthest start, enough rounds for their phase; the charges SolidBullet01, one round, their blast
    radius ULT_*_RADIUS; every damage 0 (the plugin's)."""
    assert list(files) == list(ULT_FILES), list(files)
    for name, (rounds, size, _, speed, life, volume, _) in ULT_BEAMS.items():
        m = sgo.load(data=files[name])
        p = m['indirect_fire_param']
        assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == 'LaserBullet02' and p[11] == 1, name
        assert p[2] == rounds and p[3] == 0 and p[9] == 0.0 and p[15] == 0 and m['indirect_fire_damage'] == 0.0, (name, p)
        assert p[5] * p[10] >= ULT_REACH[name], (name, p[5], p[10])
        assert name != ULT_GATHER_FILE or p[5] * p[10] == ULT_GATHER_REACH, (name, p[5], p[10])   # never past the beam's start
        assert p[17][0] == 1.0 and p[18][2] == 0.0 and p[17][2] == volume, name
    assert ULT_BEAMS[ULT_SIGHT_FILE][0] >= 12 * 60 and ULT_BEAMS[ULT_BEAM_FILE][0] >= 8 * 60, 'rounds for the whole phase'
    for name, radius in ((ULT_SPARK_FILE, ULT_SPARK_RADIUS), (ULT_BLAST_FILE, ULT_BLAST_RADIUS)):
        m = sgo.load(data=files[name])
        p = m['indirect_fire_param']
        assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == 'SolidBullet01', name
        assert p[2] == 1 and p[11] == 0 and p[9] == radius and m['indirect_fire_damage'] == 0.0, (name, p)


# ------------------------------------------------------------------------------------------ the long-range barrages
# The long-range barrages' rounds (src/barrage.cpp, src/barrage_plan.h, docs/plan-2026-10-25-strikes.md §1.1; written by
# tools/make_barrage.py), fired through the EMC's table (src/jet_bay.cpp kEmcFiles: EmcRound::barrage, barrageBurst):
#   BARRAGE_ROUND_FILE  one round: the missions' indirect fire rocket (BARRAGE_STOCK: DemoIndirectFire, a RocketBullet01 with
#                       its smoke trail) with no gravity and a long life (the plugin flies it along its curve and writes its
#                       size, blast and mark into the IFC before it fires), no wait, damage 0 (the plugin's)
#   BARRAGE_BURST_FILE  a cluster's flash where its bomblets leave: the EMC's impact charge, BARRAGE_BURST_RADIUS m, no damage
BARRAGE_STOCK = 'DEMOINDIRECTFIREE50.SGO'
BARRAGE_ROUND_FILE = 'EDF6VC_BARRAGE_ROUND.SGO'
BARRAGE_BURST_FILE = 'EDF6VC_BARRAGE_BURST.SGO'
BARRAGE_MISSILE_FILE = 'EDF6VC_BARRAGE_MISSILE.SGO'
BARRAGE_MISSILE_STOCK = 'EWEAPON097.SGO'  # N6 AT, its firing unit uses the same missile body as Tempest ATS
BARRAGE_MISSILE_MODEL = 'app:/weapon/bullet_icbm01.rab'
BARRAGE_FILES = (BARRAGE_ROUND_FILE, BARRAGE_BURST_FILE, BARRAGE_MISSILE_FILE)   # EmcRound's order
BARRAGE_SPEED = 5.0          # m a frame: its first frame's flight, before the plugin flies it
BARRAGE_LIFE = 45 * 60       # frames: past the longest flight (the ballistic missile's 22 s, the creeping barrage's shells)
BARRAGE_SIZE, BARRAGE_BLAST = 6.0, 12.0   # the SGO's own (the plugin writes each round's)
BARRAGE_BURST_RADIUS = 20.0
BARRAGE_BURST_COLOUR = (3.0, 1.6, 0.6, 1.0)


# ------------------------------------------------------------------------------------------ the Andryusha's bomblets
# A bomblet of the Andryusha's (src/andryusha.cpp splits a carrier into andryusha.h kBomblets of them; src/jet_bay.cpp
# AndryushaBomblet fires each through the EMC's table: EmcRound::andryushaBlast, andryushaFire; tools/make_katyusha.py
# writes them) is two rounds on one arc, each a DemoIndirectFire of one round fired ballistically by the plugin (the IFC
# solves the arc onto its aim; both at ANDRYUSHA_BOMBLET_SPEED and gravity 1, so they fly together):
#   ANDRYUSHA_BLAST_FILE  its burst: a GrenadeBullet01 (impact fuse, a short smoke trail), ANDRYUSHA_BOMBLET_BLAST m
#   ANDRYUSHA_FIRE_FILE   its fire: a NapalmBullet01 with the stock napalm pod's ground fire (ANDRYUSHA_NAPALM_STOCK's
#                         Ammo_CustomParameter: FlameBullet02 burning on where it lands), no blast of its own
# Both silent where they leave (a salvo is 128 bomblets); the burst keeps the stock blast's hit sound, quieter. The damage
# is the plugin's (a share of the carrier's, andryusha.h).
ANDRYUSHA_BLAST_FILE = 'EDF6VC_ANDRYUSHA_BLAST.SGO'
ANDRYUSHA_FIRE_FILE = 'EDF6VC_ANDRYUSHA_FIRE.SGO'
ANDRYUSHA_BOMBLET_FILES = (ANDRYUSHA_BLAST_FILE, ANDRYUSHA_FIRE_FILE)   # EmcRound's order (after the barrages')
ANDRYUSHA_NAPALM_STOCK = 'V_506HELI_UNDER_NAPALM01.SGO'   # the N9 Eros's napalm drop pod (WEAPON, a NapalmBullet01)
ANDRYUSHA_BOMBLET_SPEED = 0.5      # m a frame (30 m/s): reaches the impact ring (26 m) and the airburst's disc
ANDRYUSHA_BOMBLET_LIFE = 600       # frames
ANDRYUSHA_BOMBLET_BLAST = 6.0
ANDRYUSHA_BLAST_CUSTOM = [0.0, -0.004, 1.0, 0.0, 0.05, 30.0]   # GrenadeBullet01: impact, no bounce, a half-second trail
ANDRYUSHA_HIT_VOLUME = 0.4


def andryusha_bomblets(game: Game) -> dict[str, bytes]:
    """The Andryusha's bomblet SGOs (ANDRYUSHA_BOMBLET_FILES), {Mods/OBJECT file: bytes}."""
    pod = dsgo.to_py(dsgo.parse(game.read('WEAPON', ANDRYUSHA_NAPALM_STOCK)).root)
    cp = pod.get('Ammo_CustomParameter') if isinstance(pod, dict) else None
    if (not isinstance(pod, dict) or pod.get('AmmoClass') != 'NapalmBullet01' or not isinstance(cp, list) or len(cp) < 5
            or not isinstance(cp[4], list) or len(cp[4]) < 5 or cp[4][4] != 'FlameBullet02'):
        raise ValueError(f'{ANDRYUSHA_NAPALM_STOCK} 不是预期的凝固汽油弹（NapalmBullet01 / FlameBullet02）')
    out = {}
    for name, cls, blast, custom, colour in (
            (ANDRYUSHA_BLAST_FILE, 'GrenadeBullet01', ANDRYUSHA_BOMBLET_BLAST, ANDRYUSHA_BLAST_CUSTOM, None),
            (ANDRYUSHA_FIRE_FILE, 'NapalmBullet01', 0.0, cp, pod.get('AmmoColor'))):
        version, m = sgo.read(game.read('OBJECT', BARRAGE_STOCK))
        p = m.get('indirect_fire_param')
        if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
                or not isinstance(p[17], list) or len(p[17]) != 6 or not isinstance(p[18], list) or len(p[18]) != 6):
            raise ValueError(f'{BARRAGE_STOCK} 不是预期的任务火箭弹（DemoIndirectFire）')
        for i, value in ((2, 1), (3, 0), (5, ANDRYUSHA_BOMBLET_SPEED), (6, 1.0), (7, 1.0), (8, 1.0), (9, blast),
                         (10, ANDRYUSHA_BOMBLET_LIFE), (11, 0), (15, 0)):
            p[i] = int(value) if isinstance(p[i], int) else float(value)   # each keeps its node type
        p[0] = [0.0, 0.0]
        p[4] = cls
        p[13] = copy.deepcopy(custom)
        if colour is not None:
            p[12] = [float(c) for c in colour]
        p[14], p[16] = 0, 0
        p[17][2] = 0.0
        p[18][2] = ANDRYUSHA_HIT_VOLUME if blast > 0.0 else 0.0
        m['indirect_fire_damage'] = 0.0
        out[name] = sgo.write(version, m)
    return out


def check_andryusha_bomblets(files: dict[str, bytes]) -> None:
    """The bomblets as src/jet_bay.cpp AndryushaBomblet fires them: one round each, ballistic at one speed and gravity
    (the two fly one arc), no wait, damage 0 (the plugin's); the burst a GrenadeBullet01 with a blast and an impact fuse,
    the fire a NapalmBullet01 whose custom parameter spawns FlameBullet02 where it lands."""
    assert list(files) == list(ANDRYUSHA_BOMBLET_FILES), list(files)
    ps = []
    for name, cls in zip(ANDRYUSHA_BOMBLET_FILES, ('GrenadeBullet01', 'NapalmBullet01')):
        m = sgo.load(data=files[name])
        p = m['indirect_fire_param']
        assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == cls, (name, p[4])
        assert p[2] == 1 and p[15] == 0 and p[11] == 0 and m['indirect_fire_damage'] == 0.0 and p[17][2] == 0.0, (name, p)
        ps.append(p)
    blast, fire = ps
    assert blast[5] == fire[5] == ANDRYUSHA_BOMBLET_SPEED and blast[6] == fire[6] == 1.0, (blast[5:7], fire[5:7])
    assert blast[9] == ANDRYUSHA_BOMBLET_BLAST and fire[9] == 0.0 and blast[13][0] == 0.0, (blast[9], fire[9], blast[13])
    assert fire[13][4][4] == 'FlameBullet02', fire[13]
    # The ring an impact throws them to is within their reach (src/andryusha.h kImpactRingMax 26 m: 30 m/s reaches ~60 m).
    assert (ANDRYUSHA_BOMBLET_SPEED * 60.0) ** 2 / 14.7 > 2 * 26.0


def barrage_rounds(game: Game) -> dict[str, bytes]:
    """The barrages' DemoIndirectFire SGOs (BARRAGE_FILES), {Mods/OBJECT file: bytes}."""
    version, m = sgo.read(game.read('OBJECT', BARRAGE_STOCK))
    p = m.get('indirect_fire_param')
    if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
            or p[4] != 'RocketBullet01' or 'indirect_fire_damage' not in m):
        raise ValueError(f'{BARRAGE_STOCK} 不是预期的任务火箭弹（DemoIndirectFire / RocketBullet01）')
    for i, value in ((2, 1), (3, 0), (5, BARRAGE_SPEED), (6, 0), (7, BARRAGE_SIZE), (9, BARRAGE_BLAST), (10, BARRAGE_LIFE),
                     (11, 0), (15, 0)):
        p[i] = int(value) if isinstance(p[i], int) else float(value)   # each keeps its node type
    p[0] = [0.0, 0.0]
    m['indirect_fire_damage'] = 0.0
    out = {BARRAGE_ROUND_FILE: sgo.write(version, m)}
    stock_missile = sgo.load(data=game.read('WEAPON', BARRAGE_MISSILE_STOCK))
    missile = stock_missile['Ammo_CustomParameter'][4]
    if len(missile) != 19 or missile[4] != 'MissileBullet01' or missile[14].lower() != BARRAGE_MISSILE_MODEL:
        raise ValueError(f'{BARRAGE_MISSILE_STOCK} 的导弹发射单元与预期不符')
    # One straight launch from the IFC. The plugin steers every later frame; keep the model and motor effects.
    for i, value in ((2, 1), (3, 0), (5, BARRAGE_SPEED), (6, 0), (7, BARRAGE_SIZE), (9, BARRAGE_BLAST),
                     (10, BARRAGE_LIFE), (11, 0), (15, 0)):
        missile[i] = int(value) if isinstance(missile[i], int) else float(value)
    missile[0] = [0.0, 0.0]
    missile[13][4:7] = [0.0, 0.0, 100.0]  # motor acceleration/turn off, native clamp wider than the planned curve
    missile[13][8] = 1000000.0   # no native homing or lock acquisition
    m['indirect_fire_param'] = missile
    out[BARRAGE_MISSILE_FILE] = sgo.write(version, m)
    version, m = sgo.read(_emc_charge(game, BARRAGE_BURST_RADIUS, EMC_BLAST_SPEED, EMC_BLAST_LIFE))
    m['indirect_fire_param'][12] = list(BARRAGE_BURST_COLOUR)
    out[BARRAGE_BURST_FILE] = sgo.write(version, m)
    return {name: out[name] for name in BARRAGE_FILES}


def check_barrage(files: dict[str, bytes]) -> None:
    """The barrages' SGOs as src/barrage.cpp fires them: the round a RocketBullet01, one round, no gravity, no wait, not
    penetrating, living past the longest flight; the flash a SolidBullet01 charge of BARRAGE_BURST_RADIUS m; damage 0."""
    assert list(files) == list(BARRAGE_FILES), list(files)
    m = sgo.load(data=files[BARRAGE_ROUND_FILE])
    p = m['indirect_fire_param']
    assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == 'RocketBullet01', p
    assert p[2] == 1 and p[3] == 0 and p[6] == 0 and p[11] == 0 and p[15] == 0 and m['indirect_fire_damage'] == 0.0, p
    assert p[10] >= 30 * 60 and p[5] == BARRAGE_SPEED, p
    m = sgo.load(data=files[BARRAGE_MISSILE_FILE])
    p = m['indirect_fire_param']
    assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and m['indirect_fire_damage'] == 0.0, m
    assert p[4] == 'MissileBullet01' and p[14].lower() == BARRAGE_MISSILE_MODEL, p
    assert p[2] == 1 and p[3] == 0 and p[6] == 0 and p[10] == BARRAGE_LIFE and p[15] == 0, p
    m = sgo.load(data=files[BARRAGE_BURST_FILE])
    p = m['indirect_fire_param']
    assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == 'SolidBullet01', p
    assert p[2] == 1 and p[9] == BARRAGE_BURST_RADIUS and m['indirect_fire_damage'] == 0.0, p


# ------------------------------------------------------------------------------------------ the Sazabi's arms
# The Sazabi (JETS[SAZABI_JET], src/sazabi.cpp, tools/make_sazabi.py writes these). Its holders (weapon_bones):
#   0 the beam shot rifle on sz_muzzle: the 506's own laser (a null model: nothing hangs on the muzzle) made a thick
#     piercing pink beam, 900 m (30 m a frame x 30 frames), two shots a second;
#   1 the shield missiles on sz_missile: a salvo of three from the shield, the plugin's proportional guidance;
#   2 the rifle again (never fired: the 506 builds a holder for every vehicle_weapon_setting row, four at least with
#     the fuel tank, src/stores.cpp).
# The plugin pulls each one's trigger itself (src/sazabi.cpp: weapon +0x139), the stock fire bytes cleared.
# Its beams the plugin fires as rounds of its own (src/jet_bay.cpp SazabiFire, the EMC's way: DemoIndirectFire objects
# it owns, aims and grows, docs/emc-re.md), the satellite laser in the Sazabi's pink:
#   SAZABI_MEGA_FILE    the chest's diffuse mega particle cannon: a fan of these, a round a frame for SAZABI_MEGA_ROUNDS
#   SAZABI_CHARGE_FILE  its charge's glow at the chest (thin; the plugin thickens it as it charges), silent
#   SAZABI_FUNNEL_FILE  a funnel's short burst
SAZABI_RIFLE_STOCK = 'V_506HELI_LASERCANNON01_L.SGO'
SAZABI_RIFLE_FILE = 'EDF6VC_SZ_RIFLE.SGO'
SAZABI_MISSILE_FILE = 'EDF6VC_SZ_MISSILE.SGO'
SAZABI_PINK = (2.6, 0.55, 1.9, 0.6)
# AmmoSize: the stock heli laser's is 0.9; 5 lit the ground pink tens of metres round the shot (its light grows with it)
SAZABI_RIFLE: dict[str, float] = {'AmmoSize': 2.5, 'AmmoSpeed': 30.0, 'AmmoAlive': 30.0, 'AmmoDamage': 1800.0,
                                  'FireInterval': 30.0, 'AmmoCount': 9999.0, 'AmmoIsPenetration': 1.0, 'FireRecoil': 0.0}
SAZABI_RIFLE_FIRE_SE = ('weapon_Fencer_CA_blasterCannon01', 0.9, 60.0)   # cue, volume, metres heard at full
SAZABI_RIFLE_HIT_SE = ('common_damages_impactParticle_S', 0.8, 50.0)
SAZABI_MISSILE_ROUNDS = 12
SAZABI_MISSILE = Store('Shield Missile', 'ground', 0.0, 0.0,
                       Missile('MSN-04 shield missile', Look('bullet_missile', 2.1), burn=1.5, top=220.0, accel=160.0,
                               max_g=30.0, nav=4.0, life=6.0, damage=900.0, blast=10.0, lock_range=700.0, lock_cone=0.7,
                               lock_time=20.0, burst=3.0, burst_gap=6.0, interval=150.0))
SAZABI_MEGA_FILE = 'EDF6VC_SZ_MEGA.SGO'
SAZABI_CHARGE_FILE = 'EDF6VC_SZ_CHARGE.SGO'
SAZABI_FUNNEL_FILE = 'EDF6VC_SZ_FUNNEL.SGO'
SAZABI_ROUND_FILES = (SAZABI_MEGA_FILE, SAZABI_CHARGE_FILE, SAZABI_FUNNEL_FILE)   # src/jet_bay.cpp kEmcFiles' order (after the EMC's)
SAZABI_MEGA_ROUNDS, SAZABI_CHARGE_ROUNDS, SAZABI_FUNNEL_ROUNDS = 90, 120, 6
SAZABI_MEGA_SIZE, SAZABI_CHARGE_SIZE, SAZABI_FUNNEL_SIZE = 8.0, 0.6, 0.9
SAZABI_BEAM_COLOUR = (3.0, 0.6, 2.2, 1.0)


def sazabi_weapons(game: Game) -> dict[str, bytes]:
    """The Sazabi's holders' weapons, {Mods/WEAPON file: bytes}: the beam rifle and the shield missiles."""
    doc = dsgo.parse(game.read('WEAPON', SAZABI_RIFLE_STOCK))
    r = doc.root
    flash = r.get('MuzzleFlash_CustomParameter')
    cp = r.get('Ammo_CustomParameter')
    if (r.get('AmmoClass') != 'LaserBullet01' or len(r.get('AmmoColor').items) != 4 or len(flash.items) != 4
            or len(cp.items) != 11 or len(r.get('FireSe').items) != 6):
        raise ValueError(f'{SAZABI_RIFLE_STOCK} 不是预期的直升机激光炮')
    for key, value in SAZABI_RIFLE.items():
        r.set(key, value)
    r.get('AmmoColor').items[:] = list(SAZABI_PINK)
    cp.items[0].items[:] = [SAZABI_PINK[0], SAZABI_PINK[1], SAZABI_PINK[2], 1.0]   # the beam's core and glow colours
    cp.items[1].items[:] = [SAZABI_PINK[0] * 1.5, SAZABI_PINK[1] * 1.5, SAZABI_PINK[2] * 1.5, 1.0]
    flash.items[1] = 2.0                                     # the muzzle flash: as big as the Nix cannon's
    flash.items[2].items[:] = list(SAZABI_PINK)
    flash.items[3] = 4.0                                     # its glow: twice the stock laser's 2.0 (40 lit the ground pink far round it)
    fire = r.get('FireSe')
    fire.items[1], fire.items[2], fire.items[5] = SAZABI_RIFLE_FIRE_SE
    r.set('AmmoHitSe', _node_se(SAZABI_RIFLE_HIT_SE))
    _named(r, 'Beam Shot Rifle')
    return {SAZABI_RIFLE_FILE: dsgo.write(doc), SAZABI_MISSILE_FILE: _missile_sgo(game, SAZABI_MISSILE, SAZABI_MISSILE_ROUNDS)}


def _node_se(se: tuple[str, float, float]) -> object:
    """A sound entry [0, cue, volume, 1, 1, reach] as dsgo writes it."""
    cue, volume, reach = se
    return dsgo.Node([0.0, cue, volume, 1.0, 1.0, reach])


def sazabi_rounds(game: Game) -> dict[str, bytes]:
    """The Sazabi's beams the plugin fires (SAZABI_ROUND_FILES), {Mods/OBJECT file: bytes}."""
    return {SAZABI_MEGA_FILE: _emc_beam(game, SAZABI_MEGA_ROUNDS, SAZABI_MEGA_SIZE, SAZABI_BEAM_COLOUR, 1.0),
            SAZABI_CHARGE_FILE: _emc_beam(game, SAZABI_CHARGE_ROUNDS, SAZABI_CHARGE_SIZE, SAZABI_BEAM_COLOUR, 0.0),
            SAZABI_FUNNEL_FILE: _emc_beam(game, SAZABI_FUNNEL_ROUNDS, SAZABI_FUNNEL_SIZE, SAZABI_BEAM_COLOUR, 0.7)}
