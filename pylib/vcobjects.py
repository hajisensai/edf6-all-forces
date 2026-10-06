"""The plugin's generated objects, made on this machine from the player's own Root.cpk: the jets (src/jet.cpp,
src/playerjet.cpp, src/subcarrier.cpp) as V506 heli SGOs with their own models, their guns and charges, the
portal laser (src/carrierlaser.cpp) and script-placeable call-in vehicles. Shared by tools/make_jets.py,
tools/make_sub.py, tools/call_weapons.py and the test range (testrange/gen.py); builders only: what gets
written where, and who owns it, is pylib/ledger.py.
"""
from __future__ import annotations

import math
import os
import re
import struct
from dataclasses import dataclass, replace

import dsgo
import sgo
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
    # The bone each weapon hangs on (vehicle_weapon_setting), in `weapons` order; empty: all on `anchor`.
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


@dataclass(frozen=True)
class Missile:
    """One class of missile. Speeds in m/s and s (written as the game's m a frame and frames)."""
    model: str            # the real missile it follows
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
    guided: bool = True   # False: an unguided rocket (no lock: src/missile.cpp only burns its motor, it flies straight)

    def params(self, rounds: int) -> dict[str, float]:
        return {'AmmoSpeed': self.eject, 'AmmoOwnerMove': 1.0, 'AmmoGravityFactor': 0.0, 'AmmoAlive': self.life * 60.0,
                'AmmoDamage': self.damage, 'AmmoExplosion': self.blast, 'AmmoCount': float(rounds),
                'FireBurstCount': self.burst, 'FireBurstInterval': self.burst_gap, 'FireInterval': self.interval,
                'LockonRange': self.lock_range, 'LockonTime': self.lock_time,
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
    damage: float
    blast: float
    life: float = 30.0      # s: it bursts on impact long before
    interval: float = 8.0   # frames between bombs held down (a ripple)
    eject: float = 0.05     # m a frame, along the launcher's nose

    def params(self, rounds: int) -> dict[str, float]:
        return {'AmmoSpeed': self.eject, 'AmmoOwnerMove': 1.0, 'AmmoGravityFactor': 1.0, 'AmmoAlive': self.life * 60.0,
                'AmmoDamage': self.damage, 'AmmoExplosion': self.blast, 'AmmoCount': float(rounds),
                'FireBurstCount': 1.0, 'FireInterval': self.interval, 'LockonType': 0.0, 'LockonRange': 0.0}


@dataclass(frozen=True)
class Store:
    """What a jet carries besides its guns (STORES): its weapon, its role for the plugin (src/stores.h StoreRole: air
    and ground missiles, bombs), and what one round adds to the jet: its mass and its drag (a share of the clean jet's
    parasitic drag, with its pylon)."""
    name: str               # shown in the cockpit (and the weapon's name.* rows)
    role: str               # 'air', 'ground', 'bomb', 'rocket'
    mass: float             # kg a round
    drag: float             # a round's share of the clean jet's drag
    weapon: Missile | Bomb


STORES: dict[str, Store] = {
    # Short-range air-to-air, infrared, high off-boresight (AIM-9X): quick to lock, very agile, light warhead.
    'AAM_S': Store('AIM-9X', 'air', 85.0, 0.006, Missile('AIM-9X', burn=5.0, top=850.0, accel=300.0, max_g=50.0, nav=4.0,
                   life=12.0, damage=500.0, blast=10.0, lock_range=1500.0, lock_cone=0.6, lock_time=10.0)),
    # Medium-range air-to-air, active radar (AIM-120): a long burn, fast.
    'AAM_M': Store('AIM-120', 'air', 152.0, 0.010, Missile('AIM-120', burn=8.0, top=1200.0, accel=250.0, max_g=40.0, nav=4.0,
                   life=16.0, damage=600.0, blast=12.0, lock_range=2500.0, lock_cone=0.35, lock_time=30.0)),
    # Long-range air-to-air (AIM-54): a very long burn, very fast, a big warhead, not agile.
    'AAM_L': Store('AIM-54', 'air', 450.0, 0.025, Missile('AIM-54', burn=20.0, top=1500.0, accel=150.0, max_g=25.0, nav=3.0,
                   life=30.0, damage=700.0, blast=15.0, lock_range=3000.0, lock_cone=0.3, lock_time=45.0, interval=90.0)),
    # Air-to-ground (AGM-65 Maverick): subsonic, a heavy warhead, a slow lock.
    'AGM': Store('AGM-65', 'ground', 300.0, 0.020, Missile('AGM-65', burn=3.5, top=320.0, accel=120.0, max_g=15.0, nav=3.0,
                 life=15.0, damage=1500.0, blast=18.0, lock_range=1800.0, lock_cone=0.3, lock_time=40.0, interval=60.0)),
    # Light air-to-ground (AGM-114 Hellfire): what a drone carries.
    'AGM_L': Store('AGM-114', 'ground', 50.0, 0.004, Missile('AGM-114', burn=3.0, top=425.0, accel=180.0, max_g=20.0, nav=3.0,
                   life=12.0, damage=800.0, blast=10.0, lock_range=1200.0, lock_cone=0.3, lock_time=30.0)),
    # Ship-launched air defence from a vertical launcher (RIM-162 ESSM): the bay need not face the target (a wide cone),
    # very fast, very agile, fired two at a target (the submarine carrier, src/subcarrier.cpp: no mass that matters).
    'ESSM': Store('RIM-162 ESSM', 'air', 0.0, 0.0, Missile('RIM-162 ESSM', burn=4.0, top=1300.0, accel=400.0, max_g=50.0,
                  nav=4.0, life=15.0, damage=600.0, blast=15.0, lock_range=3000.0, lock_cone=1.2, lock_time=20.0, burst=2.0,
                  interval=120.0, eject=0.5)),
    # General-purpose 500 lb free-fall bomb (Mk 82): a heavier warhead than the Maverick's, a wider blast.
    'MK82': Store('Mk 82', 'bomb', 230.0, 0.015, Bomb('Mk 82', damage=1500.0, blast=25.0)),
    # Unguided 70 mm rockets (Hydra 70) from a pod of 19: a short burn to about Mach 2, a light warhead; a trigger pull
    # ripples 4. Strafing runs fire them before the guns (src/jet_combat.cpp kRocket*); the player fires them along
    # the nose. A round's mass and drag carry its pod's share.
    'RKT': Store('Hydra 70', 'rocket', 32.0, 0.0015, Missile('Hydra 70', burn=1.1, top=740.0, accel=650.0, max_g=1.0, nav=1.0,
                 life=6.0, damage=250.0, blast=6.0, lock_range=0.0, lock_cone=0.3, lock_time=0.0, burst=4.0, burst_gap=4.0,
                 interval=20.0, eject=0.6, guided=False)),
}
JET_MISSILE_STOCK, JET_BOMB_STOCK = JET_MISSILE_STOCK, 'V_409HELI_BOMB01.SGO'


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
_MULTIROLE = _load(('AAM_M', 2), ('AAM_S', 2), ('AGM', 2), ('RKT', 19), ('MK82', 4))
_STRIKE = _load(('AGM', 6), ('RKT', 38), ('MK82', 6), ('AAM_S', 2))
_DRONE = _load(('AGM_L', 4))
_SHIP = _load(('ESSM', 32))
HOMING_WEAPONS = tuple(w for jet_weapons in (_FIGHTER, _INTERCEPTOR, _MULTIROLE, _STRIKE, _DRONE, _SHIP)
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
# kg: a jet's mass without stores, by its mark (src/stores.inc kJetMasses: what its stores' mass is weighed against, and
# the mass a player-flown aircraft rams with, src/playerjet.cpp RamDamage). Every jet mark has one (tools/selftest.py
# jet_masses_cover_every_jet): the carriers are the EDF transport x 1.6 (a C-17's class), the Primers' fighter a light
# fighter's, the blast / doll drones the drone's, the gunship (tools/make_jets.py GUNSHIP_MARK) an AC-130's.
JET_MASSES = {7001.0: 22000.0, 7002.0: 16000.0, 7003.0: 20000.0, 7004.0: 18000.0, 7005.0: 120000.0, 7006.0: 2200.0,
              7007.0: 2200.0, 7008.0: 2200.0, 7009.0: 120000.0, 7010.0: 120000.0, 7011.0: 70000.0, 7020.0: 16000.0,
              7030.0: 12000.0, 7201.0: 16000.0, 7202.0: 22000.0}
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
# heli_se_table rows (index -> SEPRESET cue: 3 hit, 4 crash, 5 rotor crash, 6-8 the explosions: which of those its
# death plays is not known, so all three), and ragdoll_contact's cue. A cue the banks do not hold is silence.
CREATURE_SOUNDS: dict[str, tuple[str, dict[int, str], str]] = {
    # the giant ant's (GIANTANT01.SGO ant_DamageEffectSe / ant_BloodSe / ant_DeadSe)
    'centipede': ('app:/sound/adx/tikyuu4_en_GiantAnt.acb',
                  {3: '巨大蟻ヒットエフェクト', 4: '巨大蟻衝突', 5: 'EDF6VC_SILENT', 6: '敵共通血しぶき大', 7: '敵共通血しぶき小',
                   8: '巨大蟻死亡'}, '巨大蟻衝突'),
    # the giant bee's (GIANTBEE01.SGO bee_DamageEffectSe / bee_DamageSe / bee_DeadBloodSe / bee_DeadSe)
    'dragonfly': ('app:/sound/adx/tikyuu4_en_GiantBee.acb',
                  {3: '蜂ヒットエフェクト', 4: '蜂ダメージ', 5: 'EDF6VC_SILENT', 6: '敵共通血しぶき小', 7: '敵共通血しぶき小',
                   8: '蜂死亡'}, '蜂ダメージ'),
}
# kg: a jet's mass without stores, by its mark (src/stores.inc kJetMasses: what its stores' mass is weighed against).
JET_MASSES = {7001.0: 22000.0, 7002.0: 16000.0, 7003.0: 20000.0, 7004.0: 18000.0, 7006.0: 2200.0, 7020.0: 16000.0,
              7201.0: 16000.0, 7202.0: 22000.0}
STORE_FILES = tuple(sorted({w.split('/')[-1].upper() for w in (*_FIGHTER, *_INTERCEPTOR, *_MULTIROLE, *_STRIKE, *_DRONE, *_SHIP)
                            if store_of(w)}))
JET_WEAPON_FILES = (*JET_GUN_FILES, *JET_BLAST_FILES, *STORE_FILES, *PRIMER_GUN_FILES)
# A derived weapon's stock file: in a vehicle's weapon list it takes the stock one's per-weapon parameters.
_STOCK_OF = {'app:/weapon/' + d.lower(): 'app:/weapon/' + st.lower()
             for d, st in (*JET_GUN_FILES.items(), *((f, PRIMER_GUN_STOCK) for f in PRIMER_GUN_FILES),
                           *((f, JET_BOMB_STOCK if STORES[store_of(_weapon(f))[0]].role == 'bomb' else JET_MISSILE_STOCK)
                             for f in STORE_FILES))}
# The 506's sound table rows of its rotor (start-up and the main loop): a jet has no rotor to hear. A name SEPRESET.SGO
# does not hold makes the game's preset empty (0x7B16F0 returns false, its cue none) and playing it does nothing
# (0x7B4510); the plugin plays the engine instead (src/jetsound.cpp).
PLAYER_SEAT_POSE, PLAYER_SEAT_CLASSES = '505_TANK_DRIVER', 15   # jet_sgo: a player jet's seat


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


# A ground vehicle's own camera (game_object_camera_setting, docs/camera-re.md): [0] the point it looks at, [1] the
# eye, both points in the vehicle's frame (metres; x left-right, y up, z forward, from the vehicle's origin): the eye is
# a position of its own, not an offset from [0]. The stock ground vehicles look level from 4 m (Naegling (0, 4, 0) /
# (0, 4, -10.5), Kepler (0, 4, 0) / (0, 4, -15.5)); the artillery's (the user, 2026-10-05: "the view is too low")
# looks down past its front onto the ground ahead: ARTILLERY_VIEW_DOWN deg down, the line of sight reaching the ground
# ARTILLERY_VIEW_GROUND m ahead of the origin. The plugin's high view (src/highcam.cpp) is the second mode over these.
ARTILLERY_VIEW_DOWN = (8.0, 20.0)
ARTILLERY_VIEW_GROUND = (15.0, 45.0)


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


# The Katyusha (tools/make_katyusha.py): a rocket truck on the Naegling's class (Vehicle402_Rocket: its turret, its
# wheels), the V607 truck under the Naegling's rack; its rockets are lobbed on the high arc and the EDF6AutoTurret
# plugin aims them (LockonTargetType kMarkLofted); EDF6VehicleCrew shows its rider where they land.
KATYUSHA_ROCKETS = 'EDF6VC_KATYUSHA_ROCKETS.SGO'
GROUND_VEHICLES: dict[str, GroundVehicle] = {
    'katyusha': GroundVehicle('EDF6VC_KATYUSHA', 'VEHICLE402_ROCKET', 'EWEAPON401',
                              ('EWEAPON401', 'EWEAPON405', 'EWEAPON409', 'EWEAPON412', 'EWEAPON417'),
                              ('app:/weapon/' + KATYUSHA_ROCKETS.lower(),), 350.0, 'make_katyusha'),
    # The self-propelled artillery (tools/make_artillery.py): the Kepler's class (Vehicle603_Flak: a turret, twin guns),
    # the user's twin-gun tank model (an E551 hull, a twin-barrel turret; pylib/artillery_model.py); two large shells a
    # salvo, lobbed, aimed by EDF6AutoTurret.
    'artillery': GroundVehicle('EDF6VC_ARTILLERY', 'V603_FLAK', 'AWEAPON346',
                               ('AWEAPON346', 'AWEAPON349', 'AWEAPON352', 'AWEAPON359', 'AWEAPON361'),
                               ('app:/weapon/edf6vc_howitzer_l.sgo', 'app:/weapon/edf6vc_howitzer_r.sgo'), 600.0,
                               'make_artillery'),
    # The drill tank (tools/make_drill.py, src/drill.cpp): the Blacker's class (Vehicle505_Tank, tracks, one weapon
    # holder) in EDF: Iron Rain's drill tank, requested like the Blacker E series; its one weapon fires nothing (the
    # drill is the plugin's: it spins it and bites with DRILL_CHARGE_FILE).
    'drill': GroundVehicle('EDF6VC_DRILL', 'V505_TANK', 'EWEAPON418',
                           ('EWEAPON418', 'EWEAPON421', 'EWEAPON425', 'EWEAPON428', 'EWEAPON433'),
                           ('app:/weapon/edf6vc_drill_bit.sgo',), 1400.0, 'make_drill'),
    # The sidecar motorcycle (tools/make_sidecar.py, src/sidecar.cpp): the Freed bike's class (Vehicle503_Bike) and
    # weapons (its rider's two machine guns, its fuel tank: the stock ones), a sidecar platform and wheel built from
    # stock parts (pylib/sidecar_model.py), requested like the Freed bikes (the Ranger's vehicle slot). Its gunner is
    # no seat's: the plugin holds a soldier on the platform, firing their own weapons.
    'sidecar': GroundVehicle('EDF6VC_SIDECAR', 'V503_BIKE', 'AWEAPON338',
                             ('AWEAPON338', 'AWEAPON339', 'AWEAPON341', 'AWEAPON343', 'AWEAPON345'),
                             ('app:/weapon/v_503_bike_gun_l.sgo', 'app:/weapon/v_503_bike_gun_r.sgo',
                              'app:/weapon/v_fuel01.sgo'), 300.0, 'make_sidecar'),
}
# The drill tank's bite (src/jet_bay.cpp kDrillChargeFile, DrillCharge): an impact charge (tools/make_jets.py
# impact_charge) with a blast of DRILL_CHARGE_RADIUS m (3 m or more: the stock path that lets a blast break buildings,
# docs/drill-re.md §3), DRILL_CHARGE_SPEED m a frame for DRILL_CHARGE_LIFE frames: from the drill's base it reaches
# what the drill touches (an enemy's lock point within ~6 m) and bursts there; meeting nothing it is gone without a burst.
DRILL_CHARGE_FILE = 'EDF6VC_DRILL_CHARGE.SGO'
DRILL_CHARGE_RADIUS, DRILL_CHARGE_SPEED, DRILL_CHARGE_LIFE = 4.0, 2.5, 4
JET_SILENT_SE = 'EDF6VC_SILENT'
JET_ROTOR_SE_ROWS = (0, 1)
# Model sizes and boxes: pylib/jet_models.py (bind-pose vertices after scaling).
JETS: dict[str, Jet] = {
    'edf6tr_jet_strike_mission': Jet(7001.0, 1500.0, _STRIKE),
    'edf6tr_jet_fighter_mission': Jet(7002.0, 1000.0, _FIGHTER),
    # bomber501_2 (dark paint) with elevons, x 0.65: 16 m across
    'edf6tr_jet_interceptor_mission': Jet(7003.0, 900.0, _INTERCEPTOR, ('app:/object/edf6vc_interceptor.mrab', 'bomber501_2.mdb'),
                                          'EDF6VC_INTERCEPTOR.MRAB', 'bomber501'),
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
}
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
# on its belly), measured on that model when parked (box_model).
GUNSHIP_JET = 'edf6tr_jet_gunship'   # no _mission: the range never places it (testrange/gen.py)
JETS[GUNSHIP_JET] = replace(JETS['edf6tr_jet_strike_mission'], mark=7011.0, model=('app:/object/bomber401.mrab', 'bomber401.mdb'),
                            body='bomber401', box_model='bomber401')
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
# Every model's `mdl` and mesh bone are bound at its origin (pylib/jet_models.py lift_mdb: the grounding lifts what
# hangs on the mesh bone, not it), the box frame's origin, so the door (y 0 on `mdl`) is as high over the box's bottom
# as the origin is: on the ground for a grounded jet (its box's bottom is the origin), door_height over it for a stock
# bomber's box that reaches under its origin (on the ground once it has landed). Its radius makes it reachable from
# DOOR_STEP m across the ground, from the human's feet or HUMAN_HEIGHT over them (which of the two its position is, is
# not settled). The stock radius is never cut.
DOOR_SLACK = 0.5      # EDF.dll 0x1C36990
DOOR_OUT = 0.6        # m: outside the box's side, where a human standing against it is
DOOR_STEP = 1.0       # m: across the ground from the door point, still in reach
HUMAN_HEIGHT = 1.0    # m: a human's position is at its feet or up to this over them
DOOR_MARGIN = 0.05    # m: of reach to spare


class DoorError(Exception):
    """A jet's boarding point is not where move_door puts it (check_door)."""


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


def door_height(box) -> float:
    """How far a jet's door (y 0 on `mdl`, the box frame's origin) is over the bottom of its collision box `box`
    ([centre, half extents]): where it stands on the ground. 0 for a box on its origin (on_origin)."""
    (_cx, cy, _cz), (_hx, hy, _hz) = box
    return max(0.0, hy - cy)


def door_point(box, stock: tuple[float, float, float], radius: float) -> tuple[list[float], float]:
    """(local position on `mdl`, radius) of the boarding point of a jet with collision box `box` ([centre, half extents]);
    `stock`, `radius`: the V506 door's (see DOOR_OUT)."""
    (cx, _cy, cz), (hx, _hy, hz) = box
    z = min(max(stock[2], cz - hz), cz + hz)
    rise = max(door_height(box), HUMAN_HEIGHT)   # the most the door can be over (or under) the human's position
    need = (rise * rise + DOOR_STEP * DOOR_STEP) ** 0.5 - DOOR_SLACK + DOOR_MARGIN
    return [round(cx + hx + DOOR_OUT, 3), 0.0, round(z, 3)], round(max(radius, need), 3)


def door_name(m: dict) -> str:
    """The door locator's name of seat 0 (vehicle_riding_position[0][0])."""
    seats = m.get('vehicle_riding_position')
    if not (isinstance(seats, list) and seats and isinstance(seats[0], list) and isinstance(seats[0][0], str)):
        raise ValueError('没有 vehicle_riding_position')
    return seats[0][0]


def move_door(m: dict, box) -> None:
    """`m` (a jet SGO's values) with its door (seat 0's: every seat of a jet shares it, make_jets.with_gunner_seat)
    moved to door_point."""
    mab = bytearray(m['animation_model'][2])
    vec, rad = mab_locator(bytes(mab), door_name(m))
    stock = struct.unpack_from('<3f', mab, vec)
    at, radius = door_point(box, stock, struct.unpack_from('<f', mab, rad)[0])
    struct.pack_into('<3f', mab, vec, *at)
    struct.pack_into('<f', mab, rad, radius)
    m['animation_model'][2] = bytes(mab)


def check_door(data: bytes) -> None:
    """Re-read a jet SGO and raise DoorError unless its door is at its origin's height (y 0 on `mdl`), outside its
    collision box (heli_rigid_body) across its right side by DOOR_OUT, within its length, and a human standing DOOR_STEP m
    from it on the ground under its box (its position at its feet or HUMAN_HEIGHT over them) is in reach."""
    _, m = sgo.read(data)
    mab = m['animation_model'][2]
    name = door_name(m)
    vec, rad = mab_locator(mab, name)
    x, y, z = struct.unpack_from('<3f', mab, vec)
    reach = struct.unpack_from('<f', mab, rad)[0] + DOOR_SLACK
    box = [[float(_value(v)) for v in row] for row in m['heli_rigid_body'][:2]]
    (cx, cy, cz), (hx, hy, hz) = box
    if abs(y) > 1e-4 or x < cx + hx + DOOR_OUT - 1e-3 or not cz - hz - 1e-3 <= z <= cz + hz + 1e-3:
        raise DoorError(f'上车点 ({x:.2f},{y:.2f},{z:.2f}) 不在碰撞箱右侧外 {DOOR_OUT} m 的地面上')
    door = door_height(box)
    worst = max(((DOOR_STEP ** 2 + (door - feet) ** 2) ** 0.5 for feet in (0.0, HUMAN_HEIGHT)))
    if worst > reach:
        raise DoorError(f'站在上车点旁 {DOOR_STEP} m 的地面上够不着（要 {worst:.2f} m，能 {reach:.2f} m）')


def _value(v) -> float:
    """An SGO number node's value (sgo.Float keeps its bytes)."""
    return v.value if isinstance(v, sgo.Float) else float(v)


def _rebone(v, names: set[str], to: str = JET_ROOT_BONE):
    """`v` with every string in `names` replaced by `to` (deep)."""
    if isinstance(v, list):
        return [_rebone(c, names, to) for c in v]
    return to if isinstance(v, str) and v in names else v


JET_BODY_BONE = 'bomber501'


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


def _jet_ragdoll(blob: bytes, body: str = JET_BODY_BONE) -> bytes:
    """The ragdoll's embedded binding SGO with every model-side bone one the bomber has.
    RagdollController::BindDependency (0x6E6A50): each animation_from_ragdoll entry looks its model bone
    up (0x6E7B98); found, the proxy's record gets the bone (+0x60, first entry wins) and the bone gets the
    proxy (+8, last entry wins). Every proxy must end up with a bone: the loop at 0x6E8280 reads each
    record's +0x60 unchecked (crashed 2026-10-03 with the V506 bone names, then with only the body proxy
    bound). So every proxy is bound to the fuselage bone, the body proxy last so it is what drives it
    (the rotor proxies spin); ragdoll_from_animation has them all follow it."""
    version, inner = sgo.read(blob)
    inner['ragdoll_from_animation'] = [[[body, e[0][1]]] + e[1:] for e in inner['ragdoll_from_animation']]
    drive: dict[str, list] = {}
    for e in inner['animation_from_ragdoll']:
        drive.setdefault(e[0][0], [[e[0][0], body]] + e[1:])   # globalSRT: a second body entry, dropped
    body = drive.pop('RagDollProxys.body')
    inner['animation_from_ragdoll'] = list(drive.values()) + [body]
    return sgo.write(version, inner)


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
    if jet.player or jet.parked:
        # The player sees the whole plane: its box is the model's (wings, nose and tail), measured, not a fuselage
        # box (an NPC jet's is the fuselage: a formation's wings would catch on each other, low passes scrape; a
        # carrier's drones leave and dock under its middle). A parked one is the player's to walk up to.
        rigid = jet_models.model_box(game, jet.file or jet.box_model)
    elif jet.file in jet_models.MODELS:
        rigid = jet_models.fuselage_box(game, jet.file)   # off its model as made (grounded): never under its origin
    elif jet.model is None and rigid is None and model == JET_ELEVON_MODEL:
        rigid = jet_models.fuselage_box(game, None)
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
    if jet.player or jet.parked:
        # Every class flies it (the user, 2026-10-05: Wing Divers and Fencers too): the seat's class mask (R 1, WD 2,
        # F 4, AR 8; the 506's 9) to 15, and a driver's pose every class has (the stock gives 15 only to seats like
        # the tanks' drivers; 506_HELI_DRIVER only ever comes with 9).
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
    m['animation_model'] = [list(JET_MODEL if model_ref is None else model_ref), model[1], mab]
    m['animation_model_bone_mapping'] = [root, body]
    bones = {'body', 'rotor', 'tailRotor'}
    m['vehicle_weapon_setting'] = with_fuel([[b, 0] for b in (jet.weapon_bones or (anchor,) * len(jet.weapons))], [anchor, -1])
    # Without the plugin the 506 builds weapons for its first four holders only (src/stores.cpp): fewer would read an
    # entry that is not there; the plugin builds them all, one an entry, so the two lists must agree. The fuel tank is
    # among the four (with_fuel): built either way.
    if len(m['vehicle_weapon_setting']) < 4 or len(m['vehicle_weapon_setting']) != len(setup[3]):
        raise ValueError(f'{name}: {len(setup[3])} weapons for {len(m["vehicle_weapon_setting"])} holders (at least 4)')
    m['vehicle_dead_effect'] = _dead_effect(_rebone(m['vehicle_dead_effect'], bones, anchor))
    m['roter_contact_damage_scale'] = 0.0
    m['heli_contact_damage_scale'] = 0.0005
    rb = m['heli_rigid_body']
    box = JET_RIGID_BODY if rigid is None else rigid
    box = [list(box[0]), list(box[1])] if airborne else on_origin(box)
    m['heli_rigid_body'] = [box[0], box[1], rb[2]]
    door = _moves_door(jet)
    if door:
        move_door(m, box)
    rag = m['ragdoll']
    m['ragdoll'] = [rag[0], _jet_ragdoll(rag[1], body)]
    se = m.get('heli_se_table')
    if not isinstance(se, list) or len(se) <= max(JET_ROTOR_SE_ROWS):
        raise ValueError('V506_HELI 的 heli_se_table 不是预期的样子')
    for i in JET_ROTOR_SE_ROWS:
        se[i] = JET_SILENT_SE
    if jet.creature:
        bank, cues, contact = CREATURE_SOUNDS[jet.creature]
        if bank not in m['game_sound']:
            m['game_sound'] = list(m['game_sound']) + [bank]
        for i, cue in cues.items():
            se[i] = cue
        m['ragdoll_contact'][0] = contact
        m['vehicle_damage_effect'] = [0.0]   # the 506's damage smoke: none (a scale of 1.0 when the key is missing)
    out = sgo.write(version, m)
    if door:
        check_door(out)
    return out


def _moves_door(jet: Jet) -> bool:
    """Whether jet_sgo puts `jet`'s door beside its box (move_door): every model pylib/jet_models.py makes and the
    bombers; not a model another builder makes (the Primers' fighter, the submarine carrier: not boarded on the
    ground), whose door stays the V506's."""
    import jet_models
    # A requested body measured on a stock bomber model (box_model: the gunship) is boarded on the ground too.
    return jet.file is None or jet.file in jet_models.MODELS or jet.box_model is not None


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
        kind, rounds = store_of(_weapon(name))
        store = STORES[kind]
        if isinstance(store.weapon, Bomb):
            out[name] = _bomb_sgo(game, store, rounds)
        else:
            out[name] = _missile_sgo(game, store, rounds)
    return out


def _named(r, name: str) -> None:
    """Every name.* row of a weapon: the store's name (the stock HUD shows the weapon's, weapon +0x1B0)."""
    for key in list(r.names.values()):
        if key.startswith('name.'):
            r.set(key, name)


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
    _named(r, store.name)
    return dsgo.write(doc)


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
    _named(r, store.name)
    return dsgo.write(doc)


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
