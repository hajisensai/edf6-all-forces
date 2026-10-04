"""The plugin's generated objects, made on this machine from the player's own Root.cpk: the jets (src/jet.cpp,
src/playerjet.cpp, src/subcarrier.cpp) as V506 heli SGOs with their own models, their guns and charges, the
portal laser (src/carrierlaser.cpp) and script-placeable call-in vehicles. Shared by tools/make_jets.py,
tools/make_sub.py, tools/call_weapons.py and the test range (testrange/gen.py); builders only: what gets
written where, and who owns it, is pylib/ledger.py.
"""
from __future__ import annotations

import os
import re
import struct
from dataclasses import dataclass

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
    # The stock heli SGO it is made from (its body, rigid body, crash and weapons): every jet is a V506.
    stock: str = 'V506_HELI'
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
# kg: a jet's mass without stores, by its mark (src/stores.inc kJetMasses: what its stores' mass is weighed against).
JET_MASSES = {7001.0: 22000.0, 7002.0: 16000.0, 7003.0: 20000.0, 7004.0: 18000.0, 7006.0: 2200.0, 7020.0: 16000.0,
              7201.0: 16000.0, 7202.0: 22000.0}
STORE_FILES = tuple(sorted({w.split('/')[-1].upper() for w in (*_FIGHTER, *_INTERCEPTOR, *_MULTIROLE, *_STRIKE, *_DRONE, *_SHIP)
                            if store_of(w)}))
JET_WEAPON_FILES = (*JET_GUN_FILES, *JET_BLAST_FILES, *STORE_FILES)
# A derived weapon's stock file: in a vehicle's weapon list it takes the stock one's per-weapon parameters.
_STOCK_OF = {'app:/weapon/' + d.lower(): 'app:/weapon/' + st.lower()
             for d, st in (*JET_GUN_FILES.items(),
                           *((f, JET_BOMB_STOCK if STORES[store_of(_weapon(f))[0]].role == 'bomb' else JET_MISSILE_STOCK)
                             for f in STORE_FILES))}
# The 506's sound table rows of its rotor (start-up and the main loop): a jet has no rotor to hear. A name SEPRESET.SGO
# does not hold makes the game's preset empty (0x7B16F0 returns false, its cue none) and playing it does nothing
# (0x7B4510); the plugin plays the engine instead (src/jetsound.cpp).
PLAYER_SEAT_POSE, PLAYER_SEAT_CLASSES = '505_TANK_DRIVER', 15   # jet_sgo: a player jet's seat
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
            rigid: list[list[float]] | None = None) -> bytes:
    """`model`: the model archive and file (default JET_MODEL, the stock bomber); `body`: its mesh bone, which
    the root and the ragdoll drive; `rigid`: the collision box [centre, half extents] (default JET_RIGID_BODY).
    A jet with its own model (Jet.model) always flies it: these three come from the Jet then."""
    jet = JETS[name]
    root = anchor = JET_ROOT_BONE   # root: see JET_MAB_ROOT
    if jet.model is not None:
        model, body, rigid = list(jet.model), jet.body, [list(x) for x in jet.rigid] if jet.rigid else None
        anchor = jet.anchor
    import jet_models
    if jet.player:
        # The player sees the whole plane: its box is the model's (wings, nose and tail), measured, not a fuselage
        # box (an NPC jet's is the fuselage: a formation's wings would catch on each other).
        rigid = jet_models.model_box(game, jet.file)
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
    if jet.player:
        import copy
        m['vehicle_setup'] = copy.deepcopy(setup)
        # Every class flies it (the user, 2026-10-05: Wing Divers and Fencers too): the seat's class mask (R 1, WD 2,
        # F 4, AR 8; the 506's 9) to 15, and a driver's pose every class has (the stock gives 15 only to seats like
        # the tanks' drivers; 506_HELI_DRIVER only ever comes with 9).
        seat = m['vehicle_riding_position'][0]
        seat[3], seat[4] = PLAYER_SEAT_POSE, PLAYER_SEAT_CLASSES
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
    box = on_origin(JET_RIGID_BODY if rigid is None else rigid)
    m['heli_rigid_body'] = [box[0], box[1], rb[2]]
    rag = m['ragdoll']
    m['ragdoll'] = [rag[0], _jet_ragdoll(rag[1], body)]
    se = m.get('heli_se_table')
    if not isinstance(se, list) or len(se) <= max(JET_ROTOR_SE_ROWS):
        raise ValueError('V506_HELI 的 heli_se_table 不是预期的样子')
    for i in JET_ROTOR_SE_ROWS:
        se[i] = JET_SILENT_SE
    return sgo.write(version, m)


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
