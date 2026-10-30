"""The Air Raider's call weapons and the map's supports: the one table both sides are made from. tools/call_weapons.py
writes the weapon rows and SGOs from it; tools/gen_calls.py writes src/calls.inc (the plugin's kCalls, kCallLabels,
kCallRows, kMovedRows, kThrows and kStrikes, src/airstrike.cpp) from it; tools/selftest.py checks that the generated file
is current, that the hand-kept copies elsewhere agree, and that the order rules below hold (CI runs both).

The supports (Call.on_map: the flown calls, the barrages and the ultimate) are called from the M map's support bar for pt
since 2026-10-26 (the user: 「所有插件支援改成m里面消耗pt召唤，空袭兵召唤的支援武器删掉」): src/support_dispatch.cpp's
catalog, at Call.pt. They are no weapons any more: their rows are placeholders (RETIRED_PREFIX). The vehicle requests,
thrown drones and guns stay weapons.

Row order: saves refer to weapons by their row in the shared weapon table, so a row, once installed, never
moves. tools/call_weapons.py keeps every installed row where it is (by its id) and appends the ones a table
lacks at its end, in CALLS order. A new call is only ever appended to CALLS, never inserted, and none is ever
removed (RELEASED: each order a release installed must stay a prefix of CALLS); a support moved to the map keeps its
row as a placeholder.
"""
from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import xm_units  # noqa: E402

ID_PREFIX = 'EDF6VC_CALL_'
# A placeholder row: its template's stock row under this id, so the row keeps its index (no save loses or gains a
# weapon) and the plugin's GrantCalls (ID_PREFIX only) leaves it be. Two make them: an uninstall (tools/call_weapons.py
# retire: every row of ours), and every install since 2026-10-26 for the supports moved to the map (Call.on_map,
# call_weapons.stack), whose placeholders the plugin un-owns at every save load (src/calls.inc kMovedRows).
RETIRED_PREFIX = 'EDF6VC_RETIRED_'


@dataclass(frozen=True)
class Call:
    id: str
    mark: float          # a call: its id in the plugin (once its weapon's AmmoHitSizeAdjust); a vehicle request: the jet's mark
    kind: str            # key into KINDS
    follow: bool         # escorts the caller instead of holding the marked point
    count: int           # what it brings; also the stock bomber fallback's plane count (Ammo_CustomParameter[2][1]);
                         # a thrown one's: the bombs a magazine holds (AmmoCount[0])
    reload: float        # ReloadTime[0], the base of the star curve (a thrown one's: frames, 60 a second)
    level: float         # WEAPONTABLE column 4, same units as docs/weapons.csv level_raw
    brings: str          # 'jets' / 'helis' (the support deployment), 'sub' (SubLaunch), 'vehicle' (a vehicle request),
                         # 'throw' (a Robot Bomb whose bomb releases a drone where it lands: JetLaunchThrown),
                         # 'ultimate' (a satellite call whose fire becomes the plugin's show: src/ultimate.cpp)
    log: str = ''        # its name in the plugin's log
    fuel_sec: int = 0    # the plugin's fuel limit for what it brings (0: none)
    role: str = ''       # brings 'jets': the JetRole (src/crew.h)
    body: str = ''       # brings 'helis': the HeliBody (src/crew.h)
    # A vehicle request (template eWeapon394): the OBJECT SGO it brings (tools/make_jets.py, no extension) and
    # the pylib/vcobjects.py JETS entry it is made like; `mark` is then that jet's mark (its speed gain k).
    vehicle: str = ''
    jet: str = ''
    # ...or the pylib/vcobjects.py GROUND_VEHICLES entry it brings (a vehicle request of a ground vehicle; `mark` 0).
    ground: str = ''
    # A vehicle request the game lets the player use underground (its SGO's use_underground, the native weapon +0x228
    # that EDF.dll 0x690DB0 reads with the scene's underground flag). Underground only the drill tank comes (the user,
    # 2026-10-14: 「地底也能召唤飞机……应该只能召唤地底的钻地战车这种」): every other request is written 0, whatever its
    # template had (the Freed bike's, the sidecar's template, is 1).
    underground: bool = False
    # brings 'throw': the drone its bomb releases (src/crew.h ThrownDrone); `mark` is then throw_mark(code).
    drone: str = ''

    # ...or (brings 'gun') a hand weapon made from this stock weapon row (its SGO, row and texts): the boarding gun,
    # a KFF 50 LS (laser sight, scope; a debugging tool: instant rounds, no spread, 999 rounds, call_weapons.GUN_CURVES)
    # whose rounds put the player into the vehicle they hit (src/boarding.cpp);
    # `mark` is then its bullets' tag (call_weapons.gun_sgo), `reload` 0 (the template's own).
    gun: str = ''
    # ...or (brings 'gravity') the gravity gun (src/gravitygun.cpp on src/gravity.h): this stock weapon row (its SGO, row and
    # texts) under its own name, `mark` its AmmoHitSizeAdjust (throw_mark: the plugin tells it by the float's bits).
    weapon: str = ''
    # ...or (brings 'barrage') a long-range barrage (src/barrage.cpp, src/barrage_plan.h; docs/plan-2026-10-25-strikes.md):
    # on the map since 2026-10-26 (on_map; its row the bomber call TEMPLATE's placeholder); `barrage` its kind
    # (BARRAGE_KINDS), `damage` each round's (the plugin's, src/calls.inc kStrikes). The ultimate's `damage` is its show's.
    barrage: str = ''
    damage: float = 0.0
    # ...or (brings 'jets' / 'helis') a sortie (docs/plan-2026-10-25-strikes.md §1.2): its aircraft make one pass of
    # SORTIE_PASSES[sortie] at the mark and leave the map, no guard / follow version; `then` another flown call's id the
    # caller's machine calls onto the same mark `then_after` s after this one went out (a strike in two waves).
    sortie: str = ''
    then: str = ''
    then_after: int = 0

    @property
    def flown(self) -> bool:
        """The plugin launches what it brings at the call (kCalls); a vehicle request is the game's own, a gun is no call."""
        return self.brings in ('jets', 'helis', 'sub')

    @property
    def modal(self) -> bool:
        """It comes in a guard and a follow version, and its name says which (a sortie does not: one pass and gone)."""
        return self.brings in ('jets', 'helis') and not self.sortie

    @property
    def on_map(self) -> bool:
        """A support called from the M map's support bar for pt since 2026-10-26 (src/support_dispatch.cpp's catalog: the
        flown calls, then the strikes, STRIKES), no weapon: its weapon table row is a placeholder (RETIRED_PREFIX)."""
        return self.brings in ('jets', 'helis', 'sub', 'barrage', 'ultimate')

    @property
    def pt(self) -> int:
        """What the support costs on the map: what its call weapon charged at the 0 stars the plugin granted it with. A
        flown call's weapon kept KM6 eWeapon051's ReloadTime star curve [base, 21, 1, 10, 1.0, 0.3, 0], which falls to
        twice its base at 0 stars (call_weapons.star_value; the stock KM6 asks 2040 for its 1020); a barrage's and the
        ultimate's curves were made flat (the same at every star count), so they cost their base."""
        if not self.on_map:
            raise ValueError(f'{self.id} is a weapon, not a support on the map: it has no pt')
        return round(2 * self.reload) if self.flown else round(self.reload)


# A thrown drone's marker (brings 'throw'): its weapon's AmmoHitSizeAdjust (weapon +0x8C4, which the plugin
# compares as the float's bits) is the float whose bits are THROW_MARK_BASE | code, about 1.0004: the stock
# Patroller's bomb hits as it does, so without the plugin the weapon is a plain Patroller, and every stock weapon
# SGO near 1.0 there is exactly 1.0 (0x3F800000, 1590 of them; 2026-10-05 scan), so no stock weapon is taken for
# ours. The SGO holds a double the game converts to a float (0x68D8FB): the value is that float exactly.
THROW_MARK_BASE = 0x3F800000


def throw_mark(code: int) -> float:
    assert 0 < code < 0x1000, code
    return struct.unpack('<f', struct.pack('<I', THROW_MARK_BASE | code))[0]


def mark_bits(call: 'Call') -> int:
    """The float32 bits of a call's mark (a thrown drone's: what src/airstrike.cpp kThrows compares)."""
    return struct.unpack('<I', struct.pack('<f', call.mark))[0]


# The sorties' passes, in src/crew.h SortiePass's order after its `none` (1 + the index here).
SORTIE_PASSES = ('gunRun', 'missilePass', 'carpet', 'popUp', 'missileRain', 'gunshipLap', 'heliStrafe', 'heliHunter',
                 'heliCircle')
# The barrage kinds, in src/barrage_plan.h Kind's order (kKindNames).
BARRAGE_KINDS = ('katyusha', 'guided', 'thermobaric', 'cluster', 'creeping', 'mrsi', 'heavy', 'cruise', 'ballistic')


CALLS: tuple[Call, ...] = (
    Call('EDF6VC_CALL_INTERCEPTOR', 7101, 'interceptor', False, 2, 900, 0.3, 'jets', 'interceptors (guard)', 240, 'interceptor'),
    Call('EDF6VC_CALL_INTERCEPTOR_F', 7102, 'interceptor', True, 2, 1035, 0.5, 'jets', 'interceptors (follow)', 240, 'interceptor'),
    Call('EDF6VC_CALL_STRIKE', 7103, 'strike', False, 3, 1500, 0.5, 'jets', 'strike jets (guard)', 240, 'strike'),
    Call('EDF6VC_CALL_STRIKE_F', 7104, 'strike', True, 3, 1725, 0.7, 'jets', 'strike jets (follow)', 240, 'strike'),
    Call('EDF6VC_CALL_MULTIROLE', 7105, 'multirole', False, 3, 1800, 0.8, 'jets', 'multirole jets (guard)', 300, 'multirole'),
    Call('EDF6VC_CALL_MULTIROLE_F', 7106, 'multirole', True, 3, 2070, 1.0, 'jets', 'multirole jets (follow)', 300, 'multirole'),
    Call('EDF6VC_CALL_FIGHTER', 7107, 'fighter', False, 4, 2000, 1.0, 'jets', 'fighters (guard)', 300, 'fighter'),
    Call('EDF6VC_CALL_FIGHTER_F', 7108, 'fighter', True, 4, 2300, 1.2, 'jets', 'fighters (follow)', 300, 'fighter'),
    Call('EDF6VC_CALL_CARRIER', 7109, 'carrier', False, 1, 3000, 1.8, 'jets', 'carrier (guard)', 600, 'carrier'),
    Call('EDF6VC_CALL_CARRIER_F', 7110, 'carrier', True, 1, 3450, 2.0, 'jets', 'carrier (follow)', 600, 'carrier'),
    Call('EDF6VC_CALL_HELI', 7111, 'heli', False, 2, 1600, 0.4, 'helis', 'Brute helis (guard)', 360, body='brute410'),
    Call('EDF6VC_CALL_HELI_F', 7112, 'heli', True, 2, 1800, 0.6, 'helis', 'Eros helis (follow)', 360, body='eros506'),
    # Appended 2026-10-04: carriers whose drones blow themselves up next to the enemy.
    Call('EDF6VC_CALL_BLAST_CARRIER', 7113, 'blast_carrier', False, 1, 3300, 2.0, 'jets', 'blast drone carrier (guard)', 600,
         'blastCarrier'),
    Call('EDF6VC_CALL_BLAST_CARRIER_F', 7114, 'blast_carrier', True, 1, 3800, 2.2, 'jets', 'blast drone carrier (follow)', 600,
         'blastCarrier'),
    Call('EDF6VC_CALL_DOLL_CARRIER', 7115, 'doll_carrier', False, 1, 3600, 2.2, 'jets', 'doll drone carrier (guard)', 600,
         'dollCarrier'),
    Call('EDF6VC_CALL_DOLL_CARRIER_F', 7116, 'doll_carrier', True, 1, 4100, 2.4, 'jets', 'doll drone carrier (follow)', 600,
         'dollCarrier'),
    # Appended 2026-10-04: the submarine carrier (src/subcarrier.cpp): one, it stays the mission following the
    # player (it surfaces kSubAhead past the marker, its 1664 m hull clear of the caller; three at most).
    Call('EDF6VC_CALL_SUB', 7117, 'sub', True, 1, 7200, 3.0, 'sub', 'submarine carrier'),
    # Appended 2026-10-04: the jets the player flies (src/playerjet.cpp kKinds), vehicle requests.
    Call('EDF6VC_CALL_PJET_FIGHTER', 7201, 'pjet_fighter', False, 0, 6000, 1.0, 'vehicle',
         vehicle='EDF6VC_PJET_FIGHTER', jet='edf6tr_pjet_fighter_mission'),
    Call('EDF6VC_CALL_PJET_STRIKE', 7202, 'pjet_strike', False, 0, 6500, 0.8, 'vehicle',
         vehicle='EDF6VC_PJET_STRIKE', jet='edf6tr_pjet_strike_mission'),
    # Appended 2026-10-04: the gunship (src/jet.cpp GunshipFire), a bomber401 circling and shelling. 0.7.0 had
    # put these two before the player jets', which moved those rows (WITHDRAWN).
    Call('EDF6VC_CALL_GUNSHIP', 7118, 'gunship', False, 1, 2600, 1.2, 'jets', 'gunship (guard)', 600, 'gunship'),
    Call('EDF6VC_CALL_GUNSHIP_F', 7119, 'gunship', True, 1, 3000, 1.4, 'jets', 'gunship (follow)', 600, 'gunship'),
    # Appended 2026-10-05: the Katyusha rocket truck (tools/make_katyusha.py), requested like the Naegling.
    Call('EDF6VC_CALL_KATYUSHA', 0, 'katyusha', False, 0, 7500, 1.0, 'vehicle', vehicle='EDF6VC_KATYUSHA',
         ground='katyusha'),
    # Appended 2026-10-05: the self-propelled artillery (tools/make_artillery.py), requested like the Kepler.
    Call('EDF6VC_CALL_ARTILLERY', 0, 'artillery', False, 0, 8000, 1.2, 'vehicle', vehicle='EDF6VC_ARTILLERY',
         ground='artillery'),
    # Appended 2026-10-05: the drill tank (tools/make_drill.py, src/drill.cpp), requested like the Blacker.
    Call('EDF6VC_CALL_DRILL', 0, 'drill', False, 0, 7000, 1.0, 'vehicle', vehicle='EDF6VC_DRILL', ground='drill',
         underground=True),
    # Appended 2026-10-05: Robot Bombs (clones of the stock Patroller, eWeapon217) whose bomb releases one of the
    # plugin's drones where it lands (src/airstrike.cpp kThrows, src/jet_spawn.cpp JetLaunchThrown).
    Call('EDF6VC_CALL_THROW_BLAST', throw_mark(0xD61), 'throw_blast', False, 6, 900, 1.2, 'throw', 'thrown blast drone', 90,
         drone='blast'),
    Call('EDF6VC_CALL_THROW_DOLL', throw_mark(0xD62), 'throw_doll', False, 3, 1200, 1.6, 'throw', 'thrown doll drone', 120,
         drone='doll'),
    Call('EDF6VC_CALL_THROW_DRONE', throw_mark(0xD63), 'throw_drone', False, 2, 1800, 2.0, 'throw', 'thrown gun drone', 180,
         drone='drone'),
    # Appended 2026-10-06 (the user: 「补上空袭的召唤飞机，空母载具」): the plugin's other aircraft the player flies
    # (src/playerjet_kinds.h kBoardable), requested empty like the player jets: each one's requested twin
    # (pylib/vcobjects.py REQUEST_KINDS, tools/make_jets.py EDF6VC_FLY_*.SGO), `mark` its NPC kind's (the plugin tells
    # the kind by it). The strike jet, the bomber takeovers and the blast / doll drones are left out (REQUEST_KINDS says why).
    Call('EDF6VC_CALL_FLY_INTERCEPTOR', 7003, 'fly_interceptor', False, 0, 6500, 1.2, 'vehicle',
         vehicle='EDF6VC_FLY_INTERCEPTOR', jet='edf6tr_jet_interceptor_request_mission'),
    Call('EDF6VC_CALL_FLY_FIGHTER', 7002, 'fly_fighter', False, 0, 5500, 0.9, 'vehicle',
         vehicle='EDF6VC_FLY_FIGHTER', jet='edf6tr_jet_fighter_request_mission'),
    Call('EDF6VC_CALL_FLY_MULTIROLE', 7004, 'fly_multirole', False, 0, 8000, 1.5, 'vehicle',
         vehicle='EDF6VC_FLY_MULTIROLE', jet='edf6tr_jet_multirole_request_mission'),
    Call('EDF6VC_CALL_FLY_GUNSHIP', 7011, 'fly_gunship', False, 0, 16000, 2.2, 'vehicle',
         vehicle='EDF6VC_FLY_GUNSHIP', jet='edf6tr_jet_gunship_request_mission'),
    Call('EDF6VC_CALL_FLY_DRONE', 7006, 'fly_drone', False, 0, 3500, 0.3, 'vehicle',
         vehicle='EDF6VC_FLY_DRONE', jet='edf6tr_jet_drone_request_mission'),
    Call('EDF6VC_CALL_FLY_CARRIER', 7005, 'fly_carrier', False, 0, 24000, 2.6, 'vehicle',
         vehicle='EDF6VC_FLY_CARRIER', jet='edf6tr_jet_carrier_request_mission'),
    Call('EDF6VC_CALL_FLY_BLAST_CARRIER', 7009, 'fly_blast_carrier', False, 0, 26000, 2.8, 'vehicle',
         vehicle='EDF6VC_FLY_BLAST_CARRIER', jet='edf6tr_jet_blast_carrier_request_mission'),
    Call('EDF6VC_CALL_FLY_DOLL_CARRIER', 7010, 'fly_doll_carrier', False, 0, 28000, 3.0, 'vehicle',
         vehicle='EDF6VC_FLY_DOLL_CARRIER', jet='edf6tr_jet_doll_carrier_request_mission'),
    # Appended 2026-10-06: the sidecar motorcycle (tools/make_sidecar.py, src/sidecar.cpp), requested like the Freed bike.
    Call('EDF6VC_CALL_SIDECAR', 0, 'sidecar', False, 0, 4000, 1.0, 'vehicle', vehicle='EDF6VC_SIDECAR', ground='sidecar'),
    # Appended 2026-10-05: the boarding gun (src/boarding.cpp), a Ranger sniper rifle; no call, a row like the calls'.
    Call('EDF6VC_CALL_BOARDING_GUN', 7301, 'boarding_gun', False, 0, 0, 0.26, 'gun', gun='aWeapon081'),
    # Appended 2026-10-06 (the user: 「增加救护直升机，射的子弹射到队友会回血，自瞄也是锁队友」): Brute 410s whose door guns
    # heal (tools/make_jets.py MEDIC_HELI_FILE); their gunners aim at hurt friends (src/heli.cpp Medic).
    Call('EDF6VC_CALL_MEDIC_HELI', 7120, 'medic_heli', False, 2, 1800, 0.5, 'helis', 'medic helis (guard)', 360, body='medic410'),
    Call('EDF6VC_CALL_MEDIC_HELI_F', 7121, 'medic_heli', True, 2, 2000, 0.7, 'helis', 'medic helis (follow)', 360, body='medic410'),
    # Appended 2026-10-07 (the user: 「增加高达」): the Sazabi (src/sazabi.cpp, tools/make_sazabi.py), a mobile suit the
    # player pilots, requested empty like the player jets; `mark` its body's (pylib/vcobjects.py SAZABI_MARK).
    Call('EDF6VC_CALL_SAZABI', 7401, 'sazabi', False, 0, 15000, 2.6, 'vehicle', vehicle='EDF6VC_SAZABI',
         jet='edf6tr_sazabi_mission'),
    # Append-only: retain each class's native weapon model, handling and equipment category.
    Call('EDF6VC_CALL_BOARDING_GUN_WING', 7302, 'boarding_gun', False, 0, 0, 0.26, 'gun', gun='pWeapon127'),
    Call('EDF6VC_CALL_BOARDING_GUN_RAIDER', 7303, 'boarding_gun', False, 0, 0, 0.26, 'gun', gun='eWeapon120'),
    Call('EDF6VC_CALL_BOARDING_GUN_FENCER', 7304, 'boarding_gun', False, 0, 0, 0.26, 'gun', gun='hCannon01'),
    Call('EDF6VC_CALL_SEDAN', 0, 'sedan', False, 0, 1500, .1, 'vehicle', vehicle='EDF6VC_SEDAN', ground='sedan'),
    # Appended 2026-10-18: authored M163 / BMPT models, native tracked vehicle contracts.
    Call('EDF6VC_CALL_VULCAN', 0, 'vulcan', False, 0, 6500, 1.0, 'vehicle', vehicle='EDF6VC_VULCAN', ground='vulcan'),
    Call('EDF6VC_CALL_BMPT', 0, 'bmpt', False, 0, 7500, 1.2, 'vehicle', vehicle='EDF6VC_BMPT', ground='bmpt'),
    # Appended 2026-10-19: the ultimate strike (src/ultimate.cpp, docs/plan-2026-10-19-ultimate.md; the user: 「特效拉满
    # 昏天暗地」「清一片区域的终极技能 pt要求很高」): a stock satellite call (call_weapons.ULTIMATE_TEMPLATE: the laser
    # marker, its countdown and voice) whose fire the plugin turns into its show. 15000 pt: the stock satellites ask 2400-5400.
    # `damage` the show's (src/ultimate.cpp shares it out over its bolts, blasts and beam): at the 0 stars the stock curve
    # would scale it by 0.2 (the 2026-10-20 smoke test's 2400, docs/plan-2026-10-19-ultimate.md).
    Call('EDF6VC_CALL_ULTIMATE', 7501, 'ultimate', False, 0, 15000, 2.4, 'ultimate', 'ultimate strike', damage=12000.0),
    # Appended 2026-10-20 (the user: 「看看这里面的重力枪，咱们也做一个……根据速度和质量造成伤害」): the gravity gun, the Air Raider's
    # Laser Guide Kit (eLockonMarkerL01: its beam held while the trigger is, no round of its own) that grabs, carries and
    # throws what its beam is on (src/gravitygun.cpp, src/gravity.h).
    Call('EDF6VC_CALL_GRAVITY_GUN', throw_mark(0xE01), 'gravity_gun', False, 0, 0, 0.5, 'gravity', weapon='eLockonMarkerL01'),
    # Appended 2026-10-21 (the user: 「无畏舰拿出来放到edf里面……可以放飞船……玩家也可以操纵无畏舰」): the Dreadnought
    # (src/dreadnought.cpp, tools/make_dreadnought.py), a 500 m ship the player pilots, its hangar's fighters the player
    # boards; requested empty like the Sazabi; `mark` its body's (pylib/vcobjects.py DREADNOUGHT_MARK).
    Call('EDF6VC_CALL_DREADNOUGHT', 7601, 'dreadnought', False, 0, 30000, 3.2, 'vehicle', vehicle='EDF6VC_DREADNOUGHT',
         jet='edf6tr_dreadnought_mission'),
    # Appended 2026-10-24: the quad Vulcan, the M163 with four M61s (tools/make_quadvulcan.py, pylib/quadvulcan_model.py), its upgrade (the user, 2026-10-24: 「火神炮做一个四管的」「我说的四管是4联装转轮机枪」).
    Call('EDF6VC_CALL_QUADVULCAN', 0, 'quadvulcan', False, 0, 10000, 1.4, 'vehicle', vehicle='EDF6VC_QUADVULCAN', ground='quadvulcan'),
    # Appended 2026-10-24: the Dreadnought's SecMid turret as a fixed emplacement (tools/make_emp_mid.py, pylib/emplacement_model.py; the user, 2026-10-24: 「再做一类在地图上固定的炮台。这个炮台使用无畏舰上面那几个」).
    Call('EDF6VC_CALL_EMP_MID', 0, 'emp_mid', False, 0, 9000, 1.4, 'vehicle', vehicle='EDF6VC_EMP_MID', ground='emp_mid'),
    # Appended 2026-10-24: the Dreadnought's SecShort turret as a fixed emplacement (tools/make_emp_short.py).
    Call('EDF6VC_CALL_EMP_SHORT', 0, 'emp_short', False, 0, 12000, 1.8, 'vehicle', vehicle='EDF6VC_EMP_SHORT', ground='emp_short'),
    # Appended 2026-10-24: the Dreadnought's SecLong turret as a fixed emplacement (tools/make_emp_long.py).
    Call('EDF6VC_CALL_EMP_LONG', 0, 'emp_long', False, 0, 12000, 1.8, 'vehicle', vehicle='EDF6VC_EMP_LONG', ground='emp_long'),
    # Appended 2026-10-24 (the user: 「各种现实的炮台、近防炮……做一堆出名、独特的载具」): the 2S6 Tunguska, guns and
    # missiles in one mount (tools/make_tunguska.py; its War Thunder model, models/wt_tunguska).
    Call('EDF6VC_CALL_TUNGUSKA', 0, 'tunguska', False, 0, 9500, 1.4, 'vehicle', vehicle='EDF6VC_TUNGUSKA', ground='tunguska'),
    # Appended 2026-10-24 (the user: 「重复定位的就不要加了，除非是升级版（指的是定位相近但是数值高）」): the PzH 2000,
    # the self-propelled howitzer's upgrade (tools/make_pzh2000.py; its War Thunder model, models/wt_pzh2000).
    Call('EDF6VC_CALL_PZH2000', 0, 'pzh2000', False, 0, 11000, 1.6, 'vehicle', vehicle='EDF6VC_PZH2000', ground='pzh2000'),
    # Appended 2026-10-24 (the user: 「做一堆出名、独特的载具」): the Sturmtiger, a 380 mm rocket mortar for breaking
    # strongpoints (tools/make_sturmtiger.py; its War Thunder model, models/wt_sturmtiger).
    Call('EDF6VC_CALL_STURMTIGER', 0, 'sturmtiger', False, 0, 10000, 1.5, 'vehicle', vehicle='EDF6VC_STURMTIGER', ground='sturmtiger'),
    # Appended 2026-10-24 (the user: 「升级版（定位相近但是数值高）」): the TOS-1, the Katyusha's upgrade, thermobaric rockets in
    # salvoes (tools/make_tos1.py; lm9241221's Sketchfab model, models/wt_tos1).
    Call('EDF6VC_CALL_TOS1', 0, 'tos1', False, 0, 12000, 1.7, 'vehicle', vehicle='EDF6VC_TOS1', ground='tos1'),
    # Appended 2026-10-24 (the user: 「再做个冷战那种天启」): Red Alert 2's Apocalypse tank, a superheavy twin-gun tank with
    # anti-air missile pods (tools/make_apocalypse.py; ska-ara's Sketchfab model, models/wt_apocalypse).
    Call('EDF6VC_CALL_APOCALYPSE', 0, 'apocalypse', False, 0, 16000, 2.2, 'vehicle', vehicle='EDF6VC_APOCALYPSE', ground='apocalypse'),
    # Appended 2026-10-24 (the user: 「还有各种现实的炮台、近防炮」): the Phalanx CIWS, a fixed emplacement (tools/make_ciws_phalanx.py,
    # its Sketchfab model in models/emp_phalanx; its six barrels spin as the M163's).
    Call('EDF6VC_CALL_CIWS_PHALANX', 0, 'ciws_phalanx', False, 0, 8000, 1.3, 'vehicle', vehicle='EDF6VC_CIWS_PHALANX', ground='ciws_phalanx'),
    # Appended 2026-10-24 (the user: 「还有各种现实的炮台」): the 8.8 cm Flak 18/36, a fixed emplacement (tools/make_flak88.py,
    # its Sketchfab model in models/emp_flak88).
    Call('EDF6VC_CALL_FLAK88', 0, 'flak88', False, 0, 7000, 1.2, 'vehicle', vehicle='EDF6VC_FLAK88', ground='flak88'),
    # Appended 2026-10-24 (the user: 「还有各种现实的炮台」): the Oerlikon GDF-005 twin 35 mm, a fixed emplacement with air-burst
    # rounds, in place of the Millennium gun no usable model of which exists (tools/make_gdf005.py; models/emp_gdf005).
    Call('EDF6VC_CALL_GDF005', 0, 'gdf005', False, 0, 7500, 1.2, 'vehicle', vehicle='EDF6VC_GDF005', ground='gdf005'),
    # Appended 2026-10-24 (the user: 「还有各种现实的炮台」; Iron Beam / LaWS have no usable model): a laser air-defence turret,
    # a fixed emplacement (tools/make_laser.py; Planetrix23's Sketchfab model in models/emp_laser).
    Call('EDF6VC_CALL_LASER', 0, 'laser', False, 0, 9000, 1.4, 'vehicle', vehicle='EDF6VC_LASER', ground='laser'),
    # Appended 2026-10-24 (the user: 「做一堆出名、独特的载具/固定炮台」): a railgun turret, a fixed emplacement firing a near-instant
    # slug through everything in its path (tools/make_railgun.py; Obi-Wanya's Sketchfab model in models/emp_railgun).
    Call('EDF6VC_CALL_RAILGUN', 0, 'railgun', False, 0, 14000, 2.0, 'vehicle', vehicle='EDF6VC_RAILGUN', ground='railgun'),
    # Appended 2026-10-24 (the user: 「还有各种现实的炮台」): the MIM-104 Patriot's M901 launcher, a fixed SAM emplacement
    # (tools/make_patriot.py; MirzaArrafiERV_45's Sketchfab model in models/emp_patriot).
    Call('EDF6VC_CALL_PATRIOT', 0, 'patriot', False, 0, 15000, 2.0, 'vehicle', vehicle='EDF6VC_PATRIOT', ground='patriot'),
    # Appended 2026-10-25 (the user's strike list: 「攻击都做，实现方案可以随你」): the long-range barrages, rounds out of the
    # distance onto the marked point (src/barrage.cpp, docs/plan-2026-10-25-strikes.md §1.1); `damage` each round's.
    Call('EDF6VC_CALL_BARRAGE_ROCKETS', 7701, 'barrage_katyusha', False, 0, 2000, 1.0, 'barrage', 'rocket salvo',
         barrage='katyusha', damage=500.0),
    Call('EDF6VC_CALL_BARRAGE_GUIDED', 7702, 'barrage_guided', False, 0, 2500, 1.2, 'barrage', 'precision rockets',
         barrage='guided', damage=2500.0),
    Call('EDF6VC_CALL_BARRAGE_THERMO', 7703, 'barrage_thermobaric', False, 0, 3500, 1.5, 'barrage', 'thermobaric salvo',
         barrage='thermobaric', damage=1500.0),
    Call('EDF6VC_CALL_BARRAGE_CLUSTER', 7704, 'barrage_cluster', False, 0, 3500, 1.6, 'barrage', 'cluster rockets',
         barrage='cluster', damage=800.0),
    Call('EDF6VC_CALL_BARRAGE_CREEPING', 7705, 'barrage_creeping', False, 0, 3500, 1.4, 'barrage', 'creeping barrage',
         barrage='creeping', damage=700.0),
    Call('EDF6VC_CALL_BARRAGE_MRSI', 7706, 'barrage_mrsi', False, 0, 2000, 1.1, 'barrage', 'time-on-target fire',
         barrage='mrsi', damage=1500.0),
    Call('EDF6VC_CALL_BARRAGE_HEAVY', 7707, 'barrage_heavy', False, 0, 2500, 1.3, 'barrage', 'heavy mortar',
         barrage='heavy', damage=8000.0),
    Call('EDF6VC_CALL_BARRAGE_CRUISE', 7708, 'barrage_cruise', False, 0, 3000, 1.6, 'barrage', 'cruise missile',
         barrage='cruise', damage=6000.0),
    Call('EDF6VC_CALL_BARRAGE_BALLISTIC', 7709, 'barrage_ballistic', False, 0, 6000, 2.2, 'barrage', 'ballistic missile',
         barrage='ballistic', damage=15000.0),
    # Appended 2026-10-25 (the same list): the sorties, aircraft that make one pass at the mark and leave the map
    # (docs/plan-2026-10-25-strikes.md §1.2); `count` the aircraft, `fuel_sec` the latest they leave anyway.
    Call('EDF6VC_CALL_SORTIE_HELI_STRAFE', 7801, 'sortie_heli_strafe', False, 2, 1500, 0.8, 'helis', 'heli strafing pass', 150,
         body='eros506', sortie='heliStrafe'),
    Call('EDF6VC_CALL_SORTIE_HELI_HUNTER', 7802, 'sortie_heli_hunter', False, 1, 1600, 0.9, 'helis', 'heli missile hunter', 150,
         body='eros506', sortie='heliHunter'),
    Call('EDF6VC_CALL_SORTIE_HELI_CIRCLE', 7803, 'sortie_heli_circle', False, 1, 1400, 0.7, 'helis', 'heli death circle', 150,
         body='brute410', sortie='heliCircle'),
    Call('EDF6VC_CALL_SORTIE_DIVE', 7804, 'sortie_dive', False, 2, 1800, 0.9, 'jets', 'diving strafing run', 150, 'strike',
         sortie='gunRun'),
    Call('EDF6VC_CALL_SORTIE_MISSILE_PASS', 7805, 'sortie_missile_pass', False, 2, 2200, 1.1, 'jets', 'missile pass', 150,
         'multirole', sortie='missilePass'),
    Call('EDF6VC_CALL_SORTIE_CARPET', 7806, 'sortie_carpet', False, 3, 2600, 1.3, 'jets', 'carpet bombing', 150, 'strike',
         sortie='carpet'),
    Call('EDF6VC_CALL_SORTIE_POPUP', 7807, 'sortie_popup', False, 1, 1500, 0.8, 'jets', 'hit and run', 150, 'strike',
         sortie='popUp'),
    Call('EDF6VC_CALL_SORTIE_MISSILE_RAIN', 7808, 'sortie_missile_rain', False, 1, 2400, 1.3, 'jets', 'missile rain', 150,
         'interceptor', sortie='missileRain'),
    Call('EDF6VC_CALL_SORTIE_HAMMER', 7809, 'sortie_hammer', False, 2, 3200, 1.5, 'helis', 'hammer and anvil', 150,
         body='eros506', sortie='heliStrafe', then='EDF6VC_CALL_SORTIE_POPUP', then_after=25),
    Call('EDF6VC_CALL_SORTIE_WOLFPACK', 7810, 'sortie_wolfpack', False, 4, 2400, 1.2, 'jets', 'wolf pack', 150, 'fighter',
         sortie='gunRun'),
    Call('EDF6VC_CALL_SORTIE_GUIDED', 7811, 'sortie_guided', False, 1, 3600, 1.7, 'jets', 'guided strike', 180, 'gunship',
         sortie='gunshipLap', then='EDF6VC_CALL_SORTIE_DIVE', then_after=25),
)


def _with_xmorph(hand: tuple[Call, ...]) -> tuple[Call, ...]:
    """`hand` with the X-Morph units' requests (pylib/xm_units.py ported: one row a unit, a ground vehicle request as the
    War Thunder vehicles' are, its texts KINDS[key]), each right after the call its unit names (Unit.after): the row
    order rule above holds for them as for every row (tools/selftest.py checks RELEASED against it). The rows are the
    catalog's on every machine; whether a unit's model folder is here only decides whether its row is live
    (tools/call_weapons.py undeployed)."""
    out = list(hand)
    units = xm_units.v603()
    if len({u.after for u in units}) != len(units):
        raise ValueError('two X-Morph units name the same call to follow (pylib/xm_units.py Unit.after)')
    for u in units:
        at = [c.id for c in out].index(u.after) + 1
        out.insert(at, Call(f'{ID_PREFIX}{u.key.upper()}', 0, u.key, False, 0, u.reload, u.level, 'vehicle',
                            vehicle=f'EDF6VC_{u.key.upper()}', ground=u.key))
    return tuple(out)


CALLS = _with_xmorph(CALLS)
IDS: tuple[str, ...] = tuple(c.id for c in CALLS)
FLOWN: tuple[Call, ...] = tuple(c for c in CALLS if c.flown)   # the plugin's kCalls, in this order
# The strikes on the map's support bar (the plugin's kStrikes): the barrages in CALLS order, then the ultimate.
STRIKES: tuple[Call, ...] = (tuple(c for c in CALLS if c.brings == 'barrage')
                             + tuple(c for c in CALLS if c.brings == 'ultimate'))

# Every order of CALLS a commit on main installed (git log -p tools/call_weapons.py), frozen: each must stay a
# prefix of CALLS (tools/selftest.py). A release that adds calls adds its own order here.
RELEASED: dict[str, tuple[str, ...]] = {
    '94808aa (the first 12 calls)': IDS[:12],
    'a1c8dbb (blast and doll drone carriers)': IDS[:16],
    '2ce755c (submarine carrier)': IDS[:17],
    '5d1a3ed / 9233829 (player jets)': IDS[:19],
    '0.7.1 (the gunship after the player jets)': IDS[:21],
    'Katyusha (2026-10-05)': IDS[:22],
    'artillery (2026-10-05)': IDS[:23],
    'drill tank (2026-10-05)': IDS[:24],
    'thrown drones (2026-10-05)': IDS[:27],
    'aircraft to fly and air carriers (2026-10-06)': IDS[:35],
    'sidecar motorcycle (2026-10-06)': IDS[:36],
    'boarding gun (integrated 2026-10-06)': IDS[:37],
    'medic helis (2026-10-06)': IDS[:39],
    'Sazabi (2026-10-07)': IDS[:40],
    'all-class boarding rifles (2026-10-08)': IDS[:43],
    'civilian sedan (2026-10-16)': IDS[:44],
    'M163 Vulcan and BMPT (2026-10-19)': IDS[:46],
    'ultimate strike (2026-10-19)': IDS[:47],
    'gravity gun (2026-10-20)': IDS[:48],
    'Dreadnought (2026-10-21)': IDS[:49],
    'new vehicles and emplacements (2026-10-24)': IDS[:63],
    'Patriot (2026-10-24)': IDS[:64],
    'long-range barrages (2026-10-25)': IDS[:73],
    'sorties (2026-10-25)': IDS[:84],
    'X-Morph battle tank (2026-10-27)': IDS[:85],
    'X-Morph tracked units and forts (2026-10-27)': IDS[:93],
    'X-Morph JAGUAR Type S (2026-10-30)': IDS[:94],
}
# Orders that broke the rule and shipped: 063bf99 (0.7.0) inserted the gunship's rows before the player jets'.
# An install of it holds all of its ids, only in another order: tools/call_weapons.py keeps every installed row
# where it is (by id), so it upgrades in place.
WITHDRAWN: dict[str, tuple[str, ...]] = {
    '063bf99 (0.7.0)': IDS[:17] + ('EDF6VC_CALL_GUNSHIP', 'EDF6VC_CALL_GUNSHIP_F',
                                  'EDF6VC_CALL_PJET_FIGHTER', 'EDF6VC_CALL_PJET_STRIKE'),
}


def retired_id(call_id: str) -> str:
    return RETIRED_PREFIX + call_id[len(ID_PREFIX):]


def slot_of(row_id: str) -> str | None:
    """The call a weapon table row belongs to (its own row, or the placeholder an uninstall left), else None."""
    upper = row_id.upper()
    for c in CALLS:
        if upper in (c.id.upper(), retired_id(c.id).upper()):
            return c.id
    return None


# Per kind: name and what it does, per language (KR reuses EN).
KINDS: dict[str, dict[str, tuple[str, str]]] = {
    'gravity_gun': {
        'SC': ('重力枪', '按住扳机：光束照到的东西被抓起，悬在准星前方随视线移动——敌人、尸体、车辆、集装箱、碎片都行（60 吨以内；'
                      '越重拖得越慢，太重的抓不动）。松开扳机：把它朝准星方向甩出去，越轻飞得越快，越重飞得越慢但更有力。'
                      '被甩出的东西撞到地面、墙壁或敌人时，按自身质量和撞击速度造成伤害（½·质量·速度²），被抓的敌人自己摔下也受伤；'
                      '飞过敌人身边会撞伤它。只伤敌人，不伤友军。射程 150 米。'),
        'CN': ('重力槍', '按住扳機：光束照到的東西被抓起，懸在準星前方隨視線移動——敵人、屍體、車輛、貨櫃、碎片都行（60 噸以內；'
                      '越重拖得越慢，太重的抓不動）。放開扳機：把它朝準星方向甩出去，越輕飛得越快，越重飛得越慢但更有力。'
                      '被甩出的東西撞到地面、牆壁或敵人時，按自身質量和撞擊速度造成傷害（½·質量·速度²），被抓的敵人自己摔下也受傷；'
                      '飛過敵人身邊會撞傷它。只傷敵人，不傷友軍。射程 150 公尺。'),
        'JA': ('グラビティガン', '引き金を引き続けると、ビームが当たったもの（敵・死体・ビークル・コンテナ・破片。60 t まで、重いほどゆっくり、'
                            'それ以上は動かない）をつかみ、照準の先に浮かべて視線に合わせて動かす。離すと照準の方向へ投げる。軽いほど速く、'
                            '重いほど遅いが強い。投げたものが地面・壁・敵にぶつかると、その質量と衝突速度に応じたダメージ（½·質量·速度²）を与え、'
                            'つかまれた敵は自分が叩きつけられてもダメージを受ける。そばを通過した敵にも当たる。味方には当たらない。射程 150 m。'),
        'EN': ('Gravity Gun', 'Hold the trigger: what the beam is on is picked up and held in front of the sight, following your '
                              'aim (enemies, corpses, vehicles, containers, debris; up to 60 t, the heavier the slower, heavier '
                              'than that does not move). Release: it is thrown where the sight points, the lighter the faster, '
                              'the heavier the slower but harder. What is thrown deals damage by its own mass and impact speed '
                              '(1/2 mass speed^2) when it hits the ground, a wall or an enemy (a thrown enemy is hurt by its own '
                              'fall too), and hits the enemies it flies past. Enemies only, never friends. Range 150 m.'),
    },
    'vulcan': {
        'SC': ('M163 火神防空车', '请求一辆 M163 火神防空车：M113 底盘和六管 20mm M61 机炮。高爆与穿甲弹在载具挂载菜单或 R / 手柄 LB 间切换，一次只发射选中的弹种。外观为 KojfDiscord 分享的战雷（War Thunder）模型（CC BY 4.0）。'),
        'CN': ('M163 火神防空車', '請求一輛 M163 火神防空車：M113 底盤和六管 20mm M61 機砲。高爆與穿甲彈在載具掛載選單或 R / 手把 LB 間切換，一次只發射選中的彈種。外觀為 KojfDiscord 分享的戰雷（War Thunder）模型（CC BY 4.0）。'),
        'JA': ('M163 バルカン対空車両', 'M113 車体に 20mm 六砲身 M61 機関砲を搭載する M163 を要請する。搭載メニューまたは R / LB で榴弾・徹甲弾を切り替える。外観は KojfDiscord が公開した War Thunder のモデル（CC BY 4.0）。'),
        'EN': ('M163 Vulcan', 'Requests an M163 with an M113 hull and six-barrel 20 mm M61 cannon. Select HE or AP in the payload menu or with R / LB; only the selected belt fires. Model: the War Thunder model shared by KojfDiscord (CC BY 4.0).'),
    },
    'bmpt': {
        'SC': ('BMPT 坦克支援车', '请求一辆 BMPT：双 30mm 双路供弹机炮（高爆 / APDS 穿甲弹链，左右炮管轮流射击）为主攻击，9M120 阿塔卡导弹（串联破甲 / 9M120F 温压）为副攻击，可同时开火，导弹需要锁定。R / 手柄 LB 切换机炮弹链，T / 手柄十字键右切换导弹，各管各的；载具挂载菜单两路都可选。外观为 KojfDiscord 分享的战雷（War Thunder）模型（CC BY 4.0）。'),
        'CN': ('BMPT 戰車支援車', '請求一輛 BMPT：雙 30mm 雙路供彈機砲（高爆 / APDS 穿甲彈鏈，左右砲管輪流射擊）為主攻擊，9M120 阿塔卡飛彈（串聯破甲 / 9M120F 溫壓）為副攻擊，可同時開火，飛彈需要鎖定。R / 手把 LB 切換機砲彈鏈，T / 手把十字鍵右切換飛彈，各管各的；載具掛載選單兩路皆可選。外觀為 KojfDiscord 分享的戰雷（War Thunder）模型（CC BY 4.0）。'),
        'JA': ('BMPT 戦車支援戦闘車', '30mm 連装機関砲（二系統給弾：榴弾 / APDS、左右交互射撃）を主攻撃、9M120 アターカ（タンデム HEAT / 9M120F サーモバリック）を副攻撃で同時に撃てる BMPT を要請する。ミサイルはロックオンが必要。R / LB で機関砲の弾帯、T / 十字キー右でミサイルを切り替える（それぞれ独立）。搭載メニューでは両方を選べる。外観は KojfDiscord が公開した War Thunder のモデル（CC BY 4.0）。'),
        'EN': ('BMPT Tank Support Vehicle', 'Requests a BMPT: twin dual-feed 30 mm autocannons (HE or APDS belt, the barrels firing in turn) on the primary attack and 9M120 Ataka missiles (tandem HEAT or 9M120F thermobaric) on the secondary attack, both at once. Missiles require lock-on. R / LB switches the cannon belt, T / D-pad right the missile, each its own; the payload menu sets either. Model: the War Thunder model shared by KojfDiscord (CC BY 4.0).'),
    },
    'quadvulcan': {
        'SC': ('四联装火神防空车', '请求一辆四联装火神防空车：M163 的升级版，同一摇架上并排四门六管 20mm M61，四门一起射击，弹量与射速都是 M163 的数倍。高爆与穿甲弹在载具挂载菜单或 R / 手柄 LB 间切换，一次只发射选中的弹种。车体为 KojfDiscord 分享的战雷（War Thunder）M163 模型（CC BY 4.0）。'),
        'CN': ('四聯裝火神防空車', '請求一輛四聯裝火神防空車：M163 的升級版，同一搖架上並排四門六管 20mm M61，四門一起射擊，彈量與射速都是 M163 的數倍。高爆與穿甲彈在載具掛載選單或 R / 手把 LB 間切換，一次只發射選中的彈種。車體為 KojfDiscord 分享的戰雷（War Thunder）M163 模型（CC BY 4.0）。'),
        'JA': ('4連装バルカン対空車両', 'M163 の強化型。一つの砲架に 20mm 六砲身 M61 を 4 門並べ、4 門同時に撃つ。弾数と発射速度は M163 の数倍。搭載メニューまたは R / LB で榴弾・徹甲弾を切り替える。車体は KojfDiscord が公開した War Thunder の M163 モデル（CC BY 4.0）。'),
        'EN': ('Quad Vulcan', 'Requests the M163 upgraded with four six-barrel 20 mm M61 cannon side by side on its one cradle, all four firing together: several times the M163 rounds and rate of fire. Select HE or AP in the payload menu or with R / LB; only the selected belt fires. Hull: the War Thunder M163 model shared by KojfDiscord (CC BY 4.0).'),
    },
    'emp_mid': {
        'SC': ('无畏舰副炮炮台·中型', '请求一座中型双联速射炮台：两门炮轮流射击，射速快、爆炸范围中等，适合对付成群的敌人。外形为游戏《Dreadnought》无畏舰的副炮，装在加固底座上。固定炮台，落地后不能移动，只能转动炮塔射击；可以让 NPC 上去操作。'),
        'CN': ('無畏艦副砲砲台·中型', '請求一座中型雙聯速射砲台：兩門砲輪流射擊，射速快、爆炸範圍中等，適合對付成群的敵人。外形為遊戲《Dreadnought》無畏艦的副砲，裝在加固底座上。固定砲台，落地後不能移動，只能轉動砲塔射擊；可以讓 NPC 上去操作。'),
        'JA': ('弩級艦副砲砲台・中型', '中型連装速射砲台を要請する。2 門が交互に撃ち、連射が速く爆発範囲は中程度。群れる敵向け。外観はゲーム『Dreadnought』の弩級艦の副砲を補強台座に載せたもの。固定砲台：着地後は移動できず、砲塔を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('Dreadnought Turret: SecMid', "Requests a medium twin quick-firing gun emplacement: its two guns fire in turn, fast, with a medium blast; for swarms. Its look is a secondary turret of the Dreadnought game's ships on a fortified base. A fixed emplacement: once down it cannot move, only turn and fire; NPCs can man it."),
    },
    'emp_short': {
        'SC': ('无畏舰副炮炮台·短管', '请求一座短管双联重炮炮台：射速慢，单发威力和爆炸范围都很大，弹道下坠明显。外形为游戏《Dreadnought》无畏舰的副炮，装在加固底座上。固定炮台，落地后不能移动，只能转动炮塔射击；可以让 NPC 上去操作。'),
        'CN': ('無畏艦副砲砲台·短管', '請求一座短管雙聯重砲砲台：射速慢，單發威力和爆炸範圍都很大，彈道下墜明顯。外形為遊戲《Dreadnought》無畏艦的副砲，裝在加固底座上。固定砲台，落地後不能移動，只能轉動砲塔射擊；可以讓 NPC 上去操作。'),
        'JA': ('弩級艦副砲砲台・短砲身', '短砲身の連装重砲台を要請する。発射は遅いが一発の威力と爆発範囲が大きい。弾道は大きく落ちる。外観はゲーム『Dreadnought』の弩級艦の副砲を補強台座に載せたもの。固定砲台：着地後は移動できず、砲塔を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('Dreadnought Turret: SecShort', "Requests a short-barrelled twin heavy gun emplacement: slow, each shell very hard-hitting with a wide blast, on a dropping arc. Its look is a secondary turret of the Dreadnought game's ships on a fortified base. A fixed emplacement: once down it cannot move, only turn and fire; NPCs can man it."),
    },
    'emp_long': {
        'SC': ('无畏舰副炮炮台·长管', '请求一座长管双联远程炮炮台：炮弹快、弹道平直、射程远，适合远距离精确打击。外形为游戏《Dreadnought》无畏舰的副炮，装在加固底座上。固定炮台，落地后不能移动，只能转动炮塔射击；可以让 NPC 上去操作。'),
        'CN': ('無畏艦副砲砲台·長管', '請求一座長管雙聯遠程砲砲台：砲彈快、彈道平直、射程遠，適合遠距離精確打擊。外形為遊戲《Dreadnought》無畏艦的副砲，裝在加固底座上。固定砲台，落地後不能移動，只能轉動砲塔射擊；可以讓 NPC 上去操作。'),
        'JA': ('弩級艦副砲砲台・長砲身', '長砲身の連装遠距離砲台を要請する。弾が速く弾道が平らで射程が長い。遠距離の精密射撃向け。外観はゲーム『Dreadnought』の弩級艦の副砲を補強台座に載せたもの。固定砲台：着地後は移動できず、砲塔を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('Dreadnought Turret: SecLong', "Requests a long-barrelled twin long-range gun emplacement: fast flat shells that reach far; for precise fire at range. Its look is a secondary turret of the Dreadnought game's ships on a fortified base. A fixed emplacement: once down it cannot move, only turn and fire; NPCs can man it."),
    },
    'tunguska': {
        'SC': ('2S6 通古斯卡防空车', '请求一辆 2S6 通古斯卡：两门双管 30mm 2A38M 机炮为主攻击（两门同时射击），9M311 防空导弹为副攻击（需要锁定空中目标），两者可以同时开火；T / 手柄十字键右切换导弹。外观为 KojfDiscord 分享的战雷（War Thunder）模型（CC BY 4.0）。'),
        'CN': ('2S6 通古斯卡防空車', '請求一輛 2S6 通古斯卡：兩門雙管 30mm 2A38M 機砲為主攻擊（兩門同時射擊），9M311 防空飛彈為副攻擊（需要鎖定空中目標），兩者可以同時開火；T / 手把十字鍵右切換飛彈。外觀為 KojfDiscord 分享的戰雷（War Thunder）模型（CC BY 4.0）。'),
        'JA': ('2S6 ツングースカ', '2 門の連装 30mm 2A38M 機関砲（同時射撃）を主攻撃、9M311 対空ミサイル（空中目標へのロックオンが必要）を副攻撃で同時に撃てる 2S6 を要請する。T / 十字キー右でミサイルを切り替える。外観は KojfDiscord が公開した War Thunder のモデル（CC BY 4.0）。'),
        'EN': ('2S6 Tunguska', 'Requests a 2S6 Tunguska: two twin-barrel 30 mm 2A38M cannon (firing together) on the primary attack and 9M311 anti-air missiles (lock on to air targets) on the secondary, both at once; T / D-pad right switches the missile. Model: the War Thunder model shared by KojfDiscord (CC BY 4.0).'),
    },
    'pzh2000': {
        'SC': ('PzH 2000 自行榴弹炮', '请求一辆 PzH 2000：自行榴弹炮的升级版，炮塔可以转动，155mm 长身管榴弹炮，射速、射程和单发威力都更高。外观为 KojfDiscord 分享的战雷（War Thunder）模型（CC BY 4.0）。'),
        'CN': ('PzH 2000 自走榴彈砲', '請求一輛 PzH 2000：自走榴彈砲的升級版，砲塔可以轉動，155mm 長身管榴彈砲，射速、射程和單發威力都更高。外觀為 KojfDiscord 分享的戰雷（War Thunder）模型（CC BY 4.0）。'),
        'JA': ('PzH 2000 自走榴弾砲', '自走榴弾砲の強化型 PzH 2000 を要請する。砲塔が旋回し、155mm 長砲身榴弾砲は発射速度・射程・一発の威力がいずれも上。外観は KojfDiscord が公開した War Thunder のモデル（CC BY 4.0）。'),
        'EN': ('PzH 2000', 'Requests a PzH 2000, the self-propelled howitzer upgraded: its turret turns, and its long 155 mm gun fires faster, further and harder. Model: the War Thunder model shared by KojfDiscord (CC BY 4.0).'),
    },
    'sturmtiger': {
        'SC': ('突击虎', '请求一辆突击虎：虎式底盘上的 380mm 火箭臼炮，没有炮塔，炮只能左右各转 10 度，转向靠车身。装填很慢，一发的爆炸范围极大，用来拔除据点。外观为 KojfDiscord 分享的战雷（War Thunder）模型（CC BY 4.0）。'),
        'CN': ('突擊虎', '請求一輛突擊虎：虎式底盤上的 380mm 火箭臼砲，沒有砲塔，砲只能左右各轉 10 度，轉向靠車身。裝填很慢，一發的爆炸範圍極大，用來拔除據點。外觀為 KojfDiscord 分享的戰雷（War Thunder）模型（CC BY 4.0）。'),
        'JA': ('シュトゥルムティーガー', 'ティーガーの車体に 380mm ロケット臼砲を載せたシュトゥルムティーガーを要請する。砲塔はなく砲は左右 10 度ずつしか向かない。装填は遅いが一発の爆発範囲は非常に大きい。拠点破壊用。外観は KojfDiscord が公開した War Thunder のモデル（CC BY 4.0）。'),
        'EN': ('Sturmtiger', 'Requests a Sturmtiger: a 380 mm rocket mortar on a Tiger hull, no turret (the mortar turns 10 degrees either way; the hull does the rest). Slow to reload, its every round a huge blast, for breaking strongpoints. Model: the War Thunder model shared by KojfDiscord (CC BY 4.0).'),
    },
    'tos1': {
        'SC': ('TOS-1 喷火兵火箭炮', '请求一辆 TOS-1：喀秋莎的升级版，T-72 底盘上的温压火箭发射箱，炮塔可转，一次齐射十几发，每发爆炸范围很大，装填很慢。外观为 lm9241221 在 Sketchfab 分享的低模（CC BY 4.0）。'),
        'CN': ('TOS-1 噴火兵火箭砲', '請求一輛 TOS-1：喀秋莎的升級版，T-72 底盤上的溫壓火箭發射箱，砲塔可轉，一次齊射十幾發，每發爆炸範圍很大，裝填很慢。外觀為 lm9241221 在 Sketchfab 分享的低模（CC BY 4.0）。'),
        'JA': ('TOS-1 ブラチーノ', 'カチューシャの強化型 TOS-1 を要請する。T-72 車体にサーモバリック・ロケットの発射機を載せ、旋回でき、一度に十数発を斉射する。一発ごとの爆発範囲は大きいが装填は遅い。外観は lm9241221 が Sketchfab で公開したローポリモデル（CC BY 4.0）。'),
        'EN': ('TOS-1 Buratino', "Requests a TOS-1, the Katyusha upgraded: a thermobaric rocket launcher on a T-72 hull, its launcher turning, a dozen rockets a salvo, each bursting wide, slow to reload. Model: lm9241221's low-poly model on Sketchfab (CC BY 4.0)."),
    },
    'apocalypse': {
        'SC': ('天启坦克', '请求一辆《红色警戒 2》的天启坦克：超重型坦克，双管重炮轮流射击为主攻击，炮塔两侧导弹巢的防空导弹为副攻击（需要锁定空中目标），两者可以同时开火；装甲极厚。T / 手柄十字键右切换导弹。外观为 ska-ara 在 Sketchfab 分享的模型（CC BY 4.0）。'),
        'CN': ('天啟坦克', '請求一輛《紅色警戒 2》的天啟坦克：超重型坦克，雙管重砲輪流射擊為主攻擊，砲塔兩側飛彈巢的防空飛彈為副攻擊（需要鎖定空中目標），兩者可以同時開火；裝甲極厚。T / 手把十字鍵右切換飛彈。外觀為 ska-ara 在 Sketchfab 分享的模型（CC BY 4.0）。'),
        'JA': ('アポカリプス戦車', '『レッドアラート 2』のアポカリプス戦車を要請する。超重戦車で、連装重砲（交互射撃）を主攻撃、砲塔両脇のミサイルポッドの対空ミサイル（空中目標へのロックオンが必要）を副攻撃で同時に撃てる。装甲は非常に厚い。T / 十字キー右でミサイルを切り替える。外観は ska-ara が Sketchfab で公開したモデル（CC BY 4.0）。'),
        'EN': ('Apocalypse Tank', "Requests Red Alert 2's Apocalypse tank: a superheavy tank, its twin heavy guns firing in turn on the primary attack and the anti-air missiles of the pods beside its turret (lock on to air targets) on the secondary, both at once; very heavy armour. T / D-pad right switches the missile. Model: shared on Sketchfab by ska-ara (CC BY 4.0)."),
    },
    'ciws_phalanx': {
        'SC': ('密集阵近防炮', '请求一座密集阵近防炮台：M61A1 六管 20mm 机炮，射速极高，专打空中目标和成群的小型敌人，炮管旋转。外观为 Lemper_Studio 在 Sketchfab 分享的模型（CC BY 4.0）。固定炮台，落地后不能移动，只能转动炮塔射击；可以让 NPC 上去操作。'),
        'CN': ('密集陣近防砲', '請求一座密集陣近防砲台：M61A1 六管 20mm 機砲，射速極高，專打空中目標和成群的小型敵人，砲管旋轉。外觀為 Lemper_Studio 在 Sketchfab 分享的模型（CC BY 4.0）。固定砲台，落地後不能移動，只能轉動砲塔射擊；可以讓 NPC 上去操作。'),
        'JA': ('ファランクス CIWS', 'M61A1 六砲身 20mm 機関砲のファランクス砲台を要請する。発射速度が非常に高く、空中目標と群れる小型の敵向け。砲身が回転する。外観は Lemper_Studio が Sketchfab で公開したモデル（CC BY 4.0）。固定砲台：着地後は移動できず、砲塔を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('Phalanx CIWS', 'Requests a Phalanx CIWS emplacement: a six-barrel 20 mm M61A1 at a very high rate, for air targets and swarms; its barrels spin. Model: shared on Sketchfab by Lemper_Studio (CC BY 4.0). A fixed emplacement: once down it cannot move, only turn and fire; NPCs can man it.'),
    },
    'flak88': {
        'SC': ('88 毫米高射炮', '请求一座 88 毫米高射炮（Flak 18/36）：二战名炮，对空对地两用，高爆弹爆炸范围大，射速中等。外观为 bear17 在 Sketchfab 分享的模型（CC BY 4.0）。固定炮台，落地后不能移动，只能转动炮塔射击；可以让 NPC 上去操作。'),
        'CN': ('88 毫米高射砲', '請求一座 88 毫米高射砲（Flak 18/36）：二戰名砲，對空對地兩用，高爆彈爆炸範圍大，射速中等。外觀為 bear17 在 Sketchfab 分享的模型（CC BY 4.0）。固定砲台，落地後不能移動，只能轉動砲塔射擊；可以讓 NPC 上去操作。'),
        'JA': ('8.8cm 高射砲', '8.8cm 高射砲（Flak 18/36）を要請する。対空・対地両用の第二次大戦の名砲で、榴弾の爆発範囲が大きく発射速度は中程度。外観は bear17 が Sketchfab で公開したモデル（CC BY 4.0）。固定砲台：着地後は移動できず、砲塔を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('8.8 cm Flak', 'Requests an 8.8 cm Flak 18/36: the famous WWII dual-purpose gun, its HE shell bursting wide, at a moderate rate. Model: shared on Sketchfab by bear17 (CC BY 4.0). A fixed emplacement: once down it cannot move, only turn and fire; NPCs can man it.'),
    },
    'gdf005': {
        'SC': ('GDF-005 双联高炮', '请求一座厄利孔 GDF-005 双联 35mm 高炮：两门炮同时射击，可编程空爆弹在目标附近炸开，专打空中目标。外观为 KojfDiscord 在 Sketchfab 分享的模型（CC BY 4.0）。固定炮台，落地后不能移动，只能转动炮塔射击；可以让 NPC 上去操作。'),
        'CN': ('GDF-005 雙聯高砲', '請求一座厄利孔 GDF-005 雙聯 35mm 高砲：兩門砲同時射擊，可程式化空爆彈在目標附近炸開，專打空中目標。外觀為 KojfDiscord 在 Sketchfab 分享的模型（CC BY 4.0）。固定砲台，落地後不能移動，只能轉動砲塔射擊；可以讓 NPC 上去操作。'),
        'JA': ('GDF-005 連装高射機関砲', 'エリコン GDF-005 連装 35mm 高射機関砲を要請する。2 門同時に撃ち、プログラム式空中炸裂弾が目標の近くで炸裂する。空中目標向け。外観は KojfDiscord が Sketchfab で公開したモデル（CC BY 4.0）。固定砲台：着地後は移動できず、砲塔を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('GDF-005 Twin 35 mm', 'Requests an Oerlikon GDF-005 twin 35 mm emplacement: both guns firing together, programmable air-burst rounds bursting near the target; for air targets. Model: shared on Sketchfab by KojfDiscord (CC BY 4.0). A fixed emplacement: once down it cannot move, only turn and fire; NPCs can man it.'),
    },
    'laser': {
        'SC': ('激光防空炮台', '请求一座激光防空炮台：激光几乎瞬间到达、不受重力影响、可以穿透目标，两管轮流照射，专打空中目标。现实中的铁束、LaWS 没有可用模型，外观为 Planetrix23 在 Sketchfab 分享的反无人机炮塔模型（CC BY 4.0）。固定炮台，落地后不能移动，只能转动炮塔射击；可以让 NPC 上去操作。'),
        'CN': ('雷射防空砲台', '請求一座雷射防空砲台：雷射幾乎瞬間到達、不受重力影響、可以穿透目標，兩管輪流照射，專打空中目標。現實中的鐵束、LaWS 沒有可用模型，外觀為 Planetrix23 在 Sketchfab 分享的反無人機砲塔模型（CC BY 4.0）。固定砲台，落地後不能移動，只能轉動砲塔射擊；可以讓 NPC 上去操作。'),
        'JA': ('レーザー防空砲台', 'レーザー防空砲台を要請する。レーザーはほぼ瞬時に届き重力の影響を受けず目標を貫通する。2 本が交互に照射する空中目標向け。実在のアイアンビーム / LaWS には使えるモデルがなく、外観は Planetrix23 が Sketchfab で公開した対ドローン砲塔のモデル（CC BY 4.0）。固定砲台：着地後は移動できず、砲塔を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('Laser Air-Defence Turret', 'Requests a laser air-defence emplacement: its beam arrives almost at once, never drops and goes through its target, the two projectors firing in turn; for air targets. Iron Beam and LaWS have no usable model: this is the anti-drone turret Planetrix23 shared on Sketchfab (CC BY 4.0). A fixed emplacement: once down it cannot move, only turn and fire; NPCs can man it.'),
    },
    'railgun': {
        'SC': ('电磁炮炮台', '请求一座电磁炮炮台：弹丸几乎瞬间到达、弹道笔直，贯穿路径上的所有目标，单发威力极大，装填慢。外观为 Obi-Wanya 在 Sketchfab 分享的模型（CC BY 4.0）。固定炮台，落地后不能移动，只能转动炮塔射击；可以让 NPC 上去操作。'),
        'CN': ('電磁砲砲台', '請求一座電磁砲砲台：彈丸幾乎瞬間到達、彈道筆直，貫穿路徑上的所有目標，單發威力極大，裝填慢。外觀為 Obi-Wanya 在 Sketchfab 分享的模型（CC BY 4.0）。固定砲台，落地後不能移動，只能轉動砲塔射擊；可以讓 NPC 上去操作。'),
        'JA': ('レールガン砲台', 'レールガン砲台を要請する。弾体はほぼ瞬時に届き弾道はまっすぐで、射線上のすべてを貫通する。一発の威力は非常に大きく装填は遅い。外観は Obi-Wanya が Sketchfab で公開したモデル（CC BY 4.0）。固定砲台：着地後は移動できず、砲塔を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('Railgun Turret', 'Requests a railgun emplacement: its slug arrives almost at once on a dead-straight path and goes through everything on it, each shot very hard-hitting, slow to reload. Model: shared on Sketchfab by Obi-Wanya (CC BY 4.0). A fixed emplacement: once down it cannot move, only turn and fire; NPCs can man it.'),
    },
    'patriot': {
        'SC': ('爱国者防空导弹', '请求一座 MIM-104 爱国者发射装置：主攻击为 PAC-2 导弹齐射（两个挂点轮流，每次两发），副攻击为 PAC-3 防空导弹（需要锁定空中目标），两者可以同时开火；T / 手柄十字键右切换导弹。射程远、威力大、装填慢，专打空中目标。外观为 MirzaArrafiERV_45 在 Sketchfab 分享的模型（CC BY 4.0）。固定炮台，落地后不能移动，只能转动发射架射击；可以让 NPC 上去操作。'),
        'CN': ('愛國者防空飛彈', '請求一座 MIM-104 愛國者發射裝置：主攻擊為 PAC-2 飛彈齊射（兩個掛點輪流，每次兩發），副攻擊為 PAC-3 防空飛彈（需要鎖定空中目標），兩者可以同時開火；T / 手把十字鍵右切換飛彈。射程遠、威力大、裝填慢，專打空中目標。外觀為 MirzaArrafiERV_45 在 Sketchfab 分享的模型（CC BY 4.0）。固定砲台，落地後不能移動，只能轉動發射架射擊；可以讓 NPC 上去操作。'),
        'JA': ('パトリオット対空ミサイル', 'MIM-104 パトリオットの発射機を要請する。主攻撃は PAC-2 ミサイルの斉射（2 つの発射口が交互に 2 発ずつ）、副攻撃は PAC-3 対空ミサイル（空中目標へのロックオンが必要）で、同時に撃てる。T / 十字キー右でミサイルを切り替える。射程が長く威力が大きいが装填は遅い、空中目標向け。外観は MirzaArrafiERV_45 が Sketchfab で公開したモデル（CC BY 4.0）。固定砲台：着地後は移動できず、発射機を旋回させて撃つ。NPC も操作できる。'),
        'EN': ('MIM-104 Patriot', "Requests a MIM-104 Patriot launcher: PAC-2 salvoes on the primary attack (its two holders in turn, two a time) and PAC-3 anti-air missiles (lock on to air targets) on the secondary, both at once; T / D-pad right switches the missile. Long-ranged, hard-hitting, slow to reload; for air targets. Model: MirzaArrafiERV_45's model on Sketchfab (CC BY 4.0). A fixed emplacement: once down it cannot move, only turn its launcher and fire; NPCs can man it."),
    },
    'sedan': {
        'SC': ('民用轿车', '请求一辆没有武器的民用轿车，单驾驶席。按通常的上车键驾驶，V 切换第一人称，按住鼠标中键或手柄 L3 转动视角自由观察。'),
        'CN': ('民用轎車', '請求一輛沒有武器的民用轎車，單駕駛席。按通常的上車鍵駕駛，V 切換第一人稱，按住滑鼠中鍵或手柄 L3 轉動視角自由觀察。'),
        'JA': ('民間セダン', '非武装のセダンを要請する。運転席は一つ。通常の搭乗キーで運転、V で一人称切替。マウス中ボタンまたは L3 を押しながら見回す。'),
        'EN': ('Civilian Sedan', 'Requests an unarmed sedan with one driver seat. Board normally, press V for first person, hold middle mouse or L3 and look to observe.'),
    },
    'fly_interceptor': {
        'SC': ('截击机（自驾）', '请求一架由你自己驾驶的截击机，空着送到信号弹处：机炮和 4 远距、2 中距、2 近距空空导'
                                 '弹，飞得最快最高。操作同玩家战斗机。'),
        'CN': ('截擊機（自駕）', '請求一架由你自己駕駛的截擊機，空著送到信號彈處：機砲和 4 遠距、2 中距、2 近距空空飛'
                                 '彈，飛得最快最高。操作同玩家戰鬥機。'),
        'JA': ('迎撃機（自操縦）', '自分で操縦する迎撃機を信号弾の位置へ要請する。機関砲と長距離 4・中距離 2・短距離 '
                                   '2 発の空対空ミサイル、最も速く高く飛ぶ。操作は戦闘機（自操縦）と同じ。'),
        'EN': ('Interceptor (Fly It)', 'Requests an interceptor you fly yourself, delivered empty to the flare: guns '
                                       'and 4 long, 2 medium and 2 short-range air-to-air missiles; the fastest and '
                                       'highest flier. Flown like the player fighter.'),
    },
    'fly_fighter': {
        'SC': ('制空战斗机（自驾）', '请求一架由你自己驾驶的制空战斗机（呼叫来的那种带舵面的大翼展机体），空着送到信'
                                     '号弹处：机炮和 4 中距、2 近距空空导弹。操作同玩家战斗机。'),
        'CN': ('制空戰鬥機（自駕）', '請求一架由你自己駕駛的制空戰鬥機（呼叫來的那種帶舵面的大翼展機體），空著送到信'
                                     '號彈處：機砲和 4 中距、2 近距空空飛彈。操作同玩家戰鬥機。'),
        'JA': ('制空戦闘機（自操縦）', '自分で操縦する制空戦闘機（要請で来る舵面付きの大きな翼の機体）を信号弾の位置'
                                       'へ要請する。機関砲と中距離 4・短距離 2 発の空対空ミサイル。操作は戦闘機（自操'
                                       '縦）と同じ。'),
        'EN': ('Air Superiority Fighter (Fly It)', 'Requests an air superiority fighter you fly yourself (the '
                                                   'wide-winged airframe the fighter call brings), delivered empty '
                                                   'to the flare: guns and 4 medium and 2 short-range air-to-air '
                                                   'missiles. Flown like the player fighter.'),
    },
    'fly_multirole': {
        'SC': ('多用途机（自驾）', '请求一架由你自己驾驶的多用途战斗机，空着送到信号弹处：机炮，空空导弹、空地导弹、'
                                   '火箭弹和 Mk 82 炸弹都带。操作同玩家战斗机。'),
        'CN': ('多用途機（自駕）', '請求一架由你自己駕駛的多用途戰鬥機，空著送到信號彈處：機砲，空空飛彈、空地飛彈、'
                                   '火箭彈和 Mk 82 炸彈都帶。操作同玩家戰鬥機。'),
        'JA': ('マルチロール機（自操縦）', '自分で操縦するマルチロール機を信号弾の位置へ要請する。機関砲に空対空・空'
                                           '対地ミサイル、ロケット弾、Mk 82 爆弾をすべて積む。操作は戦闘機（自操縦）'
                                           'と同じ。'),
        'EN': ('Multirole Fighter (Fly It)', 'Requests a multirole fighter you fly yourself, delivered empty to the '
                                             'flare: guns, air-to-air and air-to-ground missiles, rockets and Mk 82 '
                                             'bombs. Flown like the player fighter.'),
    },
    'fly_gunship': {
        'SC': ('炮舰机（自驾）', '请求一架由你自己驾驶的炮舰机，空着送到信号弹处：重而慢，机炮和攻击机的挂载，另有不'
                                 '限量的炮击（打向屏幕中心所看的地面点，按住锁定键绕那一点盘旋）；有驾驶座和侧炮手座'
                                 '。翼展 52 米，没有起落架，停着时机腹着地。操作同玩家战斗机。'),
        'CN': ('砲艦機（自駕）', '請求一架由你自己駕駛的砲艦機，空著送到信號彈處：重而慢，機砲和攻擊機的掛載，另有不'
                                 '限量的砲擊（打向畫面中心所看的地面點，按住鎖定鍵繞那一點盤旋）；有駕駛座和側砲手座'
                                 '。翼展 52 米，沒有起落架，停著時機腹著地。操作同玩家戰鬥機。'),
        'JA': ('ガンシップ（自操縦）', '自分で操縦するガンシップを信号弾の位置へ要請する。重く遅い。機関砲と攻撃機の'
                                       '兵装に加え、画面中央の地点への無制限の砲撃（ロックオン切替を押し続けるとその'
                                       '地点を旋回する）。操縦席と側面砲手席がある。翼幅 52m、着陸脚はなく、駐機中は'
                                       '胴体着地。操作は戦闘機（自操縦）と同じ。'),
        'EN': ('Fixed-wing Gunship (Fly It)', 'Requests a fixed-wing gunship you fly yourself, delivered empty to '
                                              "the flare: heavy and slow, guns and the strike jet's stores, plus "
                                              "unlimited shelling at the ground point the screen's centre looks at "
                                              '(hold the target key to circle that point); a pilot and a side gunner '
                                              'seat. 52 m across, no landing gear: parked on its belly. Flown like '
                                              'the player fighter.'),
    },
    'fly_drone': {
        'SC': ('无人机（自驾）', '请求一架由你自己驾驶的小型固定翼无人机（空母放出的那种），空着送到信号弹处：机炮和 '
                                 '4 枚轻型空地导弹，又小又灵活，但很脆。操作同玩家战斗机。'),
        'CN': ('無人機（自駕）', '請求一架由你自己駕駛的小型固定翼無人機（空母放出的那種），空著送到信號彈處：機砲和 '
                                 '4 枚輕型空地飛彈，又小又靈活，但很脆。操作同玩家戰鬥機。'),
        'JA': ('ドローン（自操縦）', '自分で操縦する小型の固定翼ドローン（母艦が放つもの）を信号弾の位置へ要請する。'
                                     '機関砲と軽対地ミサイル 4 発、小さく身軽だが脆い。操作は戦闘機（自操縦）と同じ。'),
        'EN': ('Drone (Fly It)', 'Requests a small fixed-wing drone you fly yourself (the kind the carriers launch), '
                                 'delivered empty to the flare: guns and 4 light air-to-ground missiles; small and '
                                 'nimble, but fragile. Flown like the player fighter.'),
    },
    'fly_carrier': {
        'SC': ('无人机母舰（自驾）', '请求一艘由你自己驾驶的无人机母舰，空着送到信号弹处：机炮和空空导弹，副射击向屏'
                                     '幕中心所看的地点放出无人机，按锁定键召回。旋翼悬停：左摇杆或 W / S / A / D 前后'
                                     '左右平移，上升键上升，按下左摇杆或减速键下降，右摇杆或鼠标转机头。机体有 59 × 7'
                                     '7 米，信号弹要扔在够大的空地上。'),
        'CN': ('無人機母艦（自駕）', '請求一艘由你自己駕駛的無人機母艦，空著送到信號彈處：機砲和空空飛彈，副射擊向畫'
                                     '面中心所看的地點放出無人機，按鎖定鍵召回。旋翼懸停：左搖桿或 W / S / A / D 前後'
                                     '左右平移，上升鍵上升，按下左搖桿或減速鍵下降，右搖桿或滑鼠轉機頭。機體有 59 × 7'
                                     '7 米，信號彈要扔在夠大的空地上。'),
        'JA': ('無人機母艦（自操縦）', '自分で操縦する無人機母艦を信号弾の位置へ要請する。機関砲と空対空ミサイル、サ'
                                       'ブ射撃で画面中央の地点へ無人機を放ち、ロックオン切替で呼び戻す。回転翼で滞空'
                                       'する。左スティックか W / S / A / D で前後左右に移動、上昇で上昇、左スティック'
                                       '押し込みか減速で降下、右スティックかマウスで機首を回す。機体は 59 × 77m ある'
                                       'ので、信号弾は十分に広い空き地へ投げること。'),
        'EN': ('Drone Carrier (Fly It)', 'Requests a drone carrier you fly yourself, delivered empty to the flare: '
                                         'guns and air-to-air missiles; the secondary fire sends a drone to the '
                                         "point the screen's centre looks at, the target key calls them back. It "
                                         'hovers on its rotors: the left stick or W / S / A / D moves it, ascend '
                                         'climbs, L3 or the brake key descends, the right stick or mouse turns its '
                                         'nose. It is 59 x 77 m: throw the flare onto open ground that size.'),
    },
    'fly_blast_carrier': {
        'SC': ('自爆无人机母舰（自驾）', '请求一艘由你自己驾驶的自爆无人机母舰，空着送到信号弹处：同无人机母舰，放出'
                                         '的是冲到敌人身边自爆的近炸无人机。旋翼悬停：左摇杆或 W / S / A / D 前后左右'
                                         '平移，上升键上升，按下左摇杆或减速键下降，右摇杆或鼠标转机头。机体有 59 × 7'
                                         '7 米，信号弹要扔在够大的空地上。'),
        'CN': ('自爆無人機母艦（自駕）', '請求一艘由你自己駕駛的自爆無人機母艦，空著送到信號彈處：同無人機母艦，放出'
                                         '的是衝到敵人身邊自爆的近炸無人機。旋翼懸停：左搖桿或 W / S / A / D 前後左右'
                                         '平移，上升鍵上升，按下左搖桿或減速鍵下降，右搖桿或滑鼠轉機頭。機體有 59 × 7'
                                         '7 米，信號彈要扔在夠大的空地上。'),
        'JA': ('自爆ドローン母艦（自操縦）', '自分で操縦する自爆ドローン母艦を信号弾の位置へ要請する。無人機母艦と同'
                                             'じだが、放つのは敵に突っ込んで近接起爆するドローン。回転翼で滞空する。'
                                             '左スティックか W / S / A / D で前後左右に移動、上昇で上昇、左スティック'
                                             '押し込みか減速で降下、右スティックかマウスで機首を回す。機体は 59 × 77m'
                                             ' あるので、信号弾は十分に広い空き地へ投げること。'),
        'EN': ('Blast Drone Carrier (Fly It)', 'Requests a blast drone carrier you fly yourself, delivered empty to '
                                               'the flare: as the drone carrier, its drones dive at the enemy and '
                                               'blow up next to it. It hovers on its rotors: the left stick or W / S '
                                               '/ A / D moves it, ascend climbs, L3 or the brake key descends, the '
                                               'right stick or mouse turns its nose. It is 59 x 77 m: throw the '
                                               'flare onto open ground that size.'),
    },
    'fly_doll_carrier': {
        'SC': ('人偶无人机母舰（自驾）', '请求一艘由你自己驾驶的人偶无人机母舰，空着送到信号弹处：同无人机母舰，放出'
                                         '挂着唱歌跳舞人偶的无人机，慢慢飞到敌人中间吸引火力后自爆。旋翼悬停：左摇杆'
                                         '或 W / S / A / D 前后左右平移，上升键上升，按下左摇杆或减速键下降，右摇杆或'
                                         '鼠标转机头。机体有 59 × 77 米，信号弹要扔在够大的空地上。'),
        'CN': ('人偶無人機母艦（自駕）', '請求一艘由你自己駕駛的人偶無人機母艦，空著送到信號彈處：同無人機母艦，放出'
                                         '掛著唱歌跳舞人偶的無人機，慢慢飛到敵人中間吸引火力後自爆。旋翼懸停：左搖桿'
                                         '或 W / S / A / D 前後左右平移，上升鍵上升，按下左搖桿或減速鍵下降，右搖桿或'
                                         '滑鼠轉機頭。機體有 59 × 77 米，信號彈要扔在夠大的空地上。'),
        'JA': ('人形ドローン母艦（自操縦）', '自分で操縦する人形ドローン母艦を信号弾の位置へ要請する。無人機母艦と同'
                                             'じだが、歌って踊る人形を吊るしたドローンを放ち、敵の中で注意を引いてか'
                                             'ら自爆させる。回転翼で滞空する。左スティックか W / S / A / D で前後左右'
                                             'に移動、上昇で上昇、左スティック押し込みか減速で降下、右スティックかマ'
                                             'ウスで機首を回す。機体は 59 × 77m あるので、信号弾は十分に広い空き地へ'
                                             '投げること。'),
        'EN': ('Doll Drone Carrier (Fly It)', 'Requests a doll drone carrier you fly yourself, delivered empty to '
                                              'the flare: as the drone carrier, its drones carry a singing, dancing '
                                              'doll into the enemy, draw their fire and blow up. It hovers on its '
                                              'rotors: the left stick or W / S / A / D moves it, ascend climbs, L3 '
                                              'or the brake key descends, the right stick or mouse turns its nose. '
                                              'It is 59 x 77 m: throw the flare onto open ground that size.'),
    },
    'throw_blast': {
        'SC': ('投掷式自爆无人机', '投出一枚机械化炸弹，落地后变成一架旋翼自爆无人机：在落点上空低悬，飞向落点 300 米内的敌人，'
                        '贴近后引爆（伤害 1200，半径 15 米，不伤友军）；90 秒内没等到敌人就在原地引爆。'),
        'CN': ('投擲式自爆無人機', '投出一枚機械化炸彈，落地後變成一架旋翼自爆無人機：在落點上空低懸，飛向落點 300 米內的敵人，'
                        '貼近後引爆（傷害 1200，半徑 15 米，不傷友軍）；90 秒內沒等到敵人就在原地引爆。'),
        'JA': ('投擲式自爆ドローン', 'ロボットボムを投げ、着地すると回転翼の自爆ドローンになる。着地点の上空に低く滞空し、着地点から'
                          '300m 以内の敵へ飛んで近接起爆する（ダメージ 1200、半径 15m、味方には当たらない）。90 秒以内に敵が'
                          '来なければその場で起爆する。'),
        'EN': ('Thrown Blast Drone', 'Throws a robot bomb that becomes a rotor blast drone where it lands: it hovers low over '
                                     'the spot, flies at enemies within 300 m of it and blows up next to them (1200 damage, '
                                     '15 m radius, no harm to friends). With no enemy in 90 seconds it blows up where it is.'),
    },
    'throw_doll': {
        'SC': ('投掷式人偶无人机', '投出一枚机械化炸弹，落地后变成一架挂着唱歌跳舞人偶的旋翼无人机：慢慢飞到落点 300 米内的敌人中间'
                        '吸引火力，然后自爆（伤害 3000，半径 25 米）。人偶是 DLC 内容，没有时无人机照样自爆。'),
        'CN': ('投擲式人偶無人機', '投出一枚機械化炸彈，落地後變成一架掛著唱歌跳舞人偶的旋翼無人機：慢慢飛到落點 300 米內的敵人中間'
                        '吸引火力，然後自爆（傷害 3000，半徑 25 米）。人偶是 DLC 內容，沒有時無人機照樣自爆。'),
        'JA': ('投擲式人形ドローン', 'ロボットボムを投げ、着地すると歌って踊る人形を吊るした回転翼ドローンになる。着地点から 300m 以内の'
                          '敵の中へゆっくり進んで注意を引き、自爆する（ダメージ 3000、半径 25m）。人形は DLC の内容で、'
                          'ない場合もドローンは自爆する。'),
        'EN': ('Thrown Doll Drone', 'Throws a robot bomb that becomes a rotor drone carrying a singing, dancing doll: it '
                                    'drifts into the enemies within 300 m of where it landed, draws their fire and blows up '
                                    '(3000 damage, 25 m radius). The doll is DLC; without it the drone still blows up.'),
    },
    'throw_drone': {
        'SC': ('投掷式无人机', '投出一枚机械化炸弹，落地后变成一架小型固定翼无人机：机炮和轻型对地导弹，在落点上空盘旋，攻击落点'
                      ' 300 米内的敌人；弹药或燃料（180 秒）用完后飞走。'),
        'CN': ('投擲式無人機', '投出一枚機械化炸彈，落地後變成一架小型固定翼無人機：機砲和輕型對地飛彈，在落點上空盤旋，攻擊落點'
                      ' 300 米內的敵人；彈藥或燃料（180 秒）用完後飛走。'),
        'JA': ('投擲式ドローン', 'ロボットボムを投げ、着地すると小型の固定翼ドローンになる。機関砲と軽対地ミサイルを持ち、着地点の上空を'
                        '旋回して 300m 以内の敵を攻撃する。弾薬か燃料（180 秒）が尽きると飛び去る。'),
        'EN': ('Thrown Gun Drone', 'Throws a robot bomb that becomes a small fixed-wing drone with guns and light air-to-ground '
                                   'missiles: it circles over where it landed and attacks enemies within 300 m of it, '
                                   'leaving once out of ammo or fuel (180 seconds).'),
    },
    'sidecar': {
        'SC': ('边三轮摩托', '请求一辆边三轮摩托：驾驶员开车并用车上的两挺机枪射击，边车上的人用自己的武器开火。'
                      '靠近边车按上车键坐进边车（离驾驶座更近则开车）；在边车里再按上车键或跳跃离开。'
                      '你开车时，附近的 NPC 队友会跳进边车替你射击；你坐边车、没人开车时，推左摇杆让车朝你看的方向开。'),
        'CN': ('邊三輪摩托', '請求一輛邊三輪摩托：駕駛員開車並用車上的兩挺機槍射擊，邊車上的人用自己的武器開火。'
                      '靠近邊車按上車鍵坐進邊車（離駕駛座更近則開車）；在邊車裡再按上車鍵或跳躍離開。'
                      '你開車時，附近的 NPC 隊友會跳進邊車替你射擊；你坐邊車、沒人開車時，推左搖桿讓車朝你看的方向開。'),
        'JA': ('サイドカー', 'サイドカー付きバイクを要請する。運転手は運転と車体の機関銃 2 挺、サイドカーの隊員は自分の武器で射撃する。'
                        'サイドカーに近づいて搭乗ボタンで乗り込む（運転席の方が近ければ運転する）。サイドカーでもう一度搭乗ボタンか'
                        'ジャンプで降りる。運転中は近くの NPC 隊員がサイドカーに乗って射撃し、サイドカーに乗って誰も運転していない'
                        'ときは左スティックを倒すと見ている方向へ走る。'),
        'EN': ('Sidecar Motorcycle', 'Requests a motorcycle with a sidecar: the rider drives and fires its two machine '
                                     'guns, whoever is in the sidecar fires their own weapons. Walk up to the sidecar '
                                     'and press board to get in (nearer the saddle, you drive); board again or jump to '
                                     'get out. While you drive, a nearby NPC squadmate hops into the sidecar and shoots '
                                     'for you; in the sidecar with nobody driving, push the left stick and the bike '
                                     'drives the way you look.'),
    },
    'drill': {
        'SC': ('钻头战车', '请求一辆钻头战车：车头装着巨大的钻头，按住射击键钻头加速旋转，转速越高，对接触到的敌人伤害越大、'
                      '钻开建筑和岩石越快。近战，不发射炮弹。'),
        'CN': ('鑽頭戰車', '請求一輛鑽頭戰車：車頭裝著巨大的鑽頭，按住射擊鍵鑽頭加速旋轉，轉速越高，對接觸到的敵人傷害越大、'
                      '鑽開建築和岩石越快。近戰，不發射砲彈。'),
        'JA': ('ドリル戦車', 'ドリル戦車を要請する。車体前方の巨大ドリルは射撃ボタンを押し続けると回転が上がり、回転数が高いほど'
                        '触れた敵へのダメージと建物・岩を掘り崩す速さが増す。近接武器で、砲弾は撃たない。'),
        'EN': ('Drill Tank', 'Requests a drill tank: a huge drill on its nose that spins up while the fire button is held. '
                             'The faster it spins, the harder it hits the enemies it touches and the faster it bores '
                             'through buildings and rock. Melee: it fires no shells.'),
    },
    'boarding_gun': {
        'SC': ('登车狙击枪', '调试用。使用本职业武器模型、瞄具和射击操作的狙击枪：子弹瞬间到达（1500 米约 0.03 秒）、'
                        '射程无限（90 公里，超过任何地图与视距）、没有散布和后坐力，'
                        '弹匣 999 发、1 帧换弹、每秒 10 发。子弹打中己方载具时不造成伤害，而是让你立刻坐进那台载具'
                        '（优先驾驶座；NPC 驾驶的载具，NPC 挪到副座或下车）。对敌人照常造成伤害。'),
        'CN': ('登車狙擊槍', '除錯用。使用本職業武器模型、瞄具和射擊操作的狙擊槍：子彈瞬間到達（1500 公尺約 0.03 秒）、'
                        '射程無限（90 公里，超過任何地圖與視距）、沒有散布和後座力，'
                        '彈匣 999 發、1 幀換彈、每秒 10 發。子彈打中己方載具時不造成傷害，而是讓你立刻坐進那台載具'
                        '（優先駕駛座；NPC 駕駛的載具，NPC 挪到副座或下車）。對敵人照常造成傷害。'),
        'JA': ('搭乗狙撃銃', 'デバッグ用。兵科固有のモデル・スコープ・射撃操作を使う狙撃銃。弾は一瞬で届き（1500 m を約 0.03 秒）、射程は無制限（90 km、'
                        'どのマップ・視界よりも遠い）、'
                        '弾のばらつきと反動はなく、装弾数 999・リロード 1 フレーム・毎秒 10 発。味方のビークルに命中すると、'
                        'ダメージを与えずにそのビークルへ即座に搭乗する（運転席を優先。NPC が運転中なら NPC は副座へ移るか降車する）。'
                        '敵には通常どおりダメージを与える。'),
        'EN': ('Boarding Rifle', 'A debugging tool: a sniper rifle retaining the class-specific model, scope and firing controls, whose rounds '
                                 'arrive at once (1500 m in about 0.03 s) and reach any distance (90 km, past any map '
                                 'and view), with no spread and no recoil, 999 rounds, a '
                                 'one-frame reload and 10 shots a second. A round that hits a friendly vehicle does it '
                                 'no harm and puts you in it at once (the driver seat first; an NPC driver moves to a '
                                 'gunner seat or gets off). It hurts enemies as usual.'),
    },
    'artillery': {
        'SC': ('自行榴弹炮', '请求一辆自行榴弹炮：E551 的车体上一座双管炮塔，自动瞄准地面目标，每次曲射两发大口径高爆弹。装填较慢。'),
        'CN': ('自行榴彈砲', '請求一輛自行榴彈砲：E551 的車體上一座雙管砲塔，自動瞄準地面目標，每次曲射兩發大口徑高爆彈。裝填較慢。'),
        'JA': ('自走榴弾砲', '自走榴弾砲を要請する。E551 の車体に連装砲塔、地上の目標を自動で狙い、大口径の榴弾を 2 発ずつ曲射する。'
                       '装填は遅い。'),
        'EN': ('Self-Propelled Howitzer', "Requests a self-propelled howitzer: a twin-gun turret on an E551 hull, "
                                          'aiming at ground targets by itself and lobbing two heavy-calibre shells a '
                                          'salvo. Slow to reload.'),
    },
    'katyusha': {
        'SC': ('喀秋莎火箭炮车', '请求一辆喀秋莎火箭炮车：卡车车斗上的多管火箭发射架，自动瞄准地面目标，曲射齐射 40 发火箭弹覆盖一片区域。'
                          '装填较慢。'),
        'CN': ('喀秋莎火箭砲車', '請求一輛喀秋莎火箭砲車：卡車車斗上的多管火箭發射架，自動瞄準地面目標，曲射齊射 40 發火箭彈覆蓋一片區域。'
                          '裝填較慢。'),
        'JA': ('カチューシャ ロケット砲車', 'カチューシャ ロケット砲車を要請する。トラックの荷台に多連装ロケット発射機、地上の目標を自動で狙い、'
                                 '40 発のロケット弾を曲射で斉射して一帯を制圧する。装填は遅い。'),
        'EN': ('Katyusha Rocket Truck', 'Requests a Katyusha rocket truck: a multiple rocket launcher on a truck bed '
                                        'that aims at ground targets by itself and lobs a 40-rocket salvo over an area. '
                                        'Slow to reload.'),
    },
    'sazabi': {
        'SC': ('沙扎比（MSN-04）', '请求一台由你自己驾驶的沙扎比（MSN-04，全高 25.6 米），空着送到信号弹处。能走能跑，'
                              '推进器冲刺和飞行；光束步枪、光束战斧、盾牌（带导弹）、浮游炮，以及胸部的扩散粒子炮。'),
        'CN': ('沙薩比（MSN-04）', '請求一台由你自己駕駛的沙薩比（MSN-04，全高 25.6 公尺），空著送到信號彈處。能走能跑，'
                              '推進器衝刺和飛行；光束步槍、光束戰斧、盾牌（帶飛彈）、浮游砲，以及胸部的擴散粒子砲。'),
        'JA': ('サザビー（MSN-04）', '自分で操縦するサザビー（MSN-04、全高 25.6 m）を信号弾の位置へ要請する。歩行と走行、'
                               'スラスターによるダッシュと飛行。ビーム・ショット・ライフル、ビーム・トマホーク、シールド（ミサイル付き）、'
                               'ファンネル、そして腹部の拡散メガ粒子砲。'),
        'EN': ('Sazabi (MSN-04)', 'Requests a Sazabi (MSN-04, 25.6 m tall) you pilot yourself, delivered empty to the '
                                  'flare. It walks and runs, dashes and flies on its thrusters; beam shot rifle, beam '
                                  'tomahawk, shield (with missiles), funnels, and the chest mega particle cannon.'),
    },
    'dreadnought': {
        'SC': ('无畏舰', '请求一艘由你自己驾驶的无畏舰（游戏《Dreadnought》的重型无畏舰，全长 500 米），空着送到信号弹处。'
                       '8 座炮塔都能上人操纵（舰桥指挥空炮塔）；两舷装几个机库模块就有几个机库（ini 可选，默认 4 个），'
                       '舰载机（战斗机、防御战斗机、轰炸机、截击机）从各自机库口飞出随舰作战、飞回入库；'
                       '站到机库上方按上车键，就从这个机库驾驶一架出击，返舰时飞回原机库。'),
        'CN': ('無畏艦', '請求一艘由你自己駕駛的無畏艦（遊戲《Dreadnought》的重型無畏艦，全長 500 公尺），空著送到信號彈處。'
                       '8 座砲塔都能上人操縱（艦橋指揮空砲塔）；兩舷裝幾個機庫模組就有幾個機庫（ini 可選，預設 4 個），'
                       '艦載機（戰鬥機、防禦戰鬥機、轟炸機、攔截機）從各自機庫口飛出隨艦作戰、飛回入庫；'
                       '站到機庫上方按上車鍵，就從這個機庫駕駛一架出擊，返艦時飛回原機庫。'),
        'JA': ('ドレッドノート', '自分で操縦するドレッドノート（ゲーム『Dreadnought』の重ドレッドノート、全長 500 m）を'
                            '信号弾の位置へ要請する。8 基の砲塔すべてに搭乗して操作でき（艦橋は空いた砲塔を指揮）、両舷に積んだ格納庫'
                            'モジュールの数だけ格納庫があり（ini で選択、既定 4 基）、艦載機（戦闘機、防衛戦闘機、爆撃機、迎撃機）が'
                            'それぞれの格納庫口から発艦して随伴・帰還する。格納庫の上で搭乗キーを押すとその格納庫から1機で出撃し、'
                            '帰艦時は元の格納庫に戻る。'),
        'EN': ('Dreadnought', 'Requests a Dreadnought you pilot yourself (the heavy dreadnought of the game "Dreadnought", '
                              '500 m long), delivered empty to the flare. All 8 turrets can be manned (the bridge commands the '
                              'free ones); it has a hangar for each hangar module fitted on its broadsides (chosen in the ini, '
                              '4 by default), launching and taking back fighters (fighter, defender, bomber, interceptor) '
                              'through their own openings; stand over a hangar and press board to sortie from it in one, '
                              'and it flies you back into that hangar.'),
    },
    'pjet_fighter': {
        'SC': ('玩家战斗机', '请求一架由你自己驾驶的战斗机，空着送到信号弹处：两门机炮和导弹，轻快，转弯最急。'
                         '前推左摇杆或按上升键加油门，后拉减油门；右摇杆或鼠标转弯和俯仰。'),
        'CN': ('玩家戰鬥機', '請求一架由你自己駕駛的戰鬥機，空著送到信號彈處：兩門機砲和飛彈，輕快，轉彎最急。'
                         '前推左搖桿或按上升鍵加油門，後拉減油門；右搖桿或滑鼠轉彎和俯仰。'),
        'JA': ('戦闘機（自操縦）', '自分で操縦する戦闘機を信号弾の位置へ要請する。機関砲2門とミサイル、軽快で旋回が鋭い。'
                              '左スティック前か上昇でスロットルを上げ、後ろで下げる。右スティックかマウスで旋回と上下。'),
        'EN': ('Fighter (Fly It)', 'Requests a fighter you fly yourself, delivered empty to the flare: two guns and '
                                   'missiles, light and the tightest turner. Left stick forward or ascend opens the '
                                   'throttle, back closes it; the right stick or mouse turns and pitches.'),
    },
    'pjet_strike': {
        'SC': ('玩家攻击机', '请求一架由你自己驾驶的攻击机，空着送到信号弹处：两门机炮和导弹，更耐打，速度和转弯不如战斗机。'
                         '操作同玩家战斗机。'),
        'CN': ('玩家攻擊機', '請求一架由你自己駕駛的攻擊機，空著送到信號彈處：兩門機砲和飛彈，更耐打，速度和轉彎不如戰鬥機。'
                         '操作同玩家戰鬥機。'),
        'JA': ('攻撃機（自操縦）', '自分で操縦する攻撃機を信号弾の位置へ要請する。機関砲2門とミサイル、頑丈だが速度と旋回は'
                              '戦闘機に劣る。操作は戦闘機（自操縦）と同じ。'),
        'EN': ('Strike Jet (Fly It)', 'Requests a strike jet you fly yourself, delivered empty to the flare: two guns '
                                      'and missiles, tougher but slower and wider turning than the fighter. Flown like '
                                      'the fighter.'),
    },
    'interceptor': {
        'SC': ('截击机', '呼叫截击机，优先攻击空中目标；比制空战斗机飞得更快更高，并从更远处发射导弹。'),
        'CN': ('截擊機', '呼叫截擊機，優先攻擊空中目標；比制空戰鬥機飛得更快更高，並從更遠處發射飛彈。'),
        'JA': ('迎撃機', '迎撃機を要請する。空中の敵を優先して攻撃する。制空戦闘機より速く高く飛び、遠くからミサイルを撃つ。'),
        'EN': ('Interceptors', 'Calls interceptors that attack flying targets first, faster and higher than fighters, firing their missiles from farther out.'),
    },
    'strike': {
        'SC': ('对地攻击机', '呼叫对地攻击机，优先攻击地面目标，俯冲投弹。'),
        'CN': ('對地攻擊機', '呼叫對地攻擊機，優先攻擊地面目標，俯衝投彈。'),
        'JA': ('対地攻撃機', '対地攻撃機を要請する。地上の敵を優先し、急降下して爆撃する。'),
        'EN': ('Strike Fighters', 'Calls strike fighters that attack ground targets first, diving onto them with bombs.'),
    },
    'multirole': {
        'SC': ('多用途机', '呼叫多用途战斗机，攻击最近的目标，空中地面皆可。'),
        'CN': ('多用途機', '呼叫多用途戰鬥機，攻擊最近的目標，空中地面皆可。'),
        'JA': ('マルチロール機', 'マルチロール機を要請する。空中・地上を問わず最も近い敵を攻撃する。'),
        'EN': ('Multirole Fighters', 'Calls multirole fighters that attack the nearest target, in the air or on the ground.'),
    },
    'fighter': {
        'SC': ('制空战斗机', '呼叫制空战斗机，优先攻击空中目标。'),
        'CN': ('制空戰鬥機', '呼叫制空戰鬥機，優先攻擊空中目標。'),
        'JA': ('制空戦闘機', '制空戦闘機を要請する。空中の敵を優先して攻撃する。'),
        'EN': ('Air Superiority Fighters', 'Calls air superiority fighters that attack flying targets first.'),
    },
    'carrier': {
        'SC': ('无人机母舰', '呼叫无人机母舰：在空中盘旋，派出无人机攻击范围内的敌人。'),
        'CN': ('無人機母艦', '呼叫無人機母艦：在空中盤旋，派出無人機攻擊範圍內的敵人。'),
        'JA': ('無人機母艦', '無人機母艦を要請する。上空を旋回し、範囲内の敵へ無人機を送り込む。'),
        'EN': ('Drone Carrier', 'Calls a drone carrier that hovers overhead and sends its drones at enemies in range.'),
    },
    'blast_carrier': {
        'SC': ('自爆无人机母舰', '呼叫自爆无人机母舰：悬停在空中，放出近炸无人机，冲到敌人身边自爆。'),
        'CN': ('自爆無人機母艦', '呼叫自爆無人機母艦：懸停在空中，放出近炸無人機，衝到敵人身邊自爆。'),
        'JA': ('自爆ドローン母艦', '自爆ドローン母艦を要請する。上空に滞空し、敵に突っ込んで近接起爆するドローンを放つ。'),
        'EN': ('Blast Drone Carrier', 'Calls a carrier that hovers overhead and sends drones that dive at the enemy and blow up next to it.'),
    },
    'sub': {
        'SC': ('潜水母舰支援', '呼叫潜水母舰在信号弹前方浮上：舰身可以站人，炮塔机炮和导弹自动攻击，导弹在舰内装填，随玩家移动，留到任务结束（同时最多 3 艘）。'),
        'CN': ('潛水母艦支援', '呼叫潛水母艦在信號彈前方浮上：艦身可以站人，砲塔機砲和飛彈自動攻擊，飛彈在艦內裝填，隨玩家移動，留到任務結束（同時最多 3 艘）。'),
        'JA': ('潜水母艦支援', '潜水母艦を信号弾の先に浮上させる。甲板に乗れ、砲塔の機関砲とミサイルで自動攻撃する。ミサイルの装填は潜水母艦内でおこなわれる。プレイヤーに随伴し、作戦終了まで留まる（同時に3隻まで）。'),
        'EN': ('Submarine Carrier', 'Surfaces a submarine carrier past the flare: stand on its deck while its turret guns and missiles attack on their own (missiles reload aboard). It follows you for the rest of the mission (three at most).'),
    },
    'gunship': {
        'SC': ('炮舰机', '呼叫炮舰机：在目标点上空大圈盘旋，从机上向附近的地面敌人持续炮击，不俯冲。'),
        'CN': ('砲艦機', '呼叫砲艦機：在目標點上空大圈盤旋，從機上向附近的地面敵人持續砲擊，不俯衝。'),
        'JA': ('ガンシップ', 'ガンシップを要請する。上空を大きく旋回しながら、付近の地上の敵へ機上から砲撃を続ける。急降下はしない。'),
        'EN': ('Fixed-wing Gunship', 'Calls a fixed-wing gunship that circles wide overhead and keeps shelling nearby ground enemies from the air, without diving.'),
    },
    'doll_carrier': {
        'SC': ('人偶无人机母舰', '呼叫人偶无人机母舰：放出挂着会唱歌跳舞的人偶的无人机，慢慢飞到敌人中间吸引火力，然后自爆。'),
        'CN': ('人偶無人機母艦', '呼叫人偶無人機母艦：放出掛著會唱歌跳舞的人偶的無人機，慢慢飛到敵人中間吸引火力，然後自爆。'),
        'JA': ('人形ドローン母艦', '人形ドローン母艦を要請する。歌って踊る人形を吊るしたドローンが敵の中へ進み、注意を引いてから自爆する。'),
        'EN': ('Doll Drone Carrier', 'Calls a carrier whose drones carry a singing, dancing doll into the enemy, draw their fire, and blow up.'),
    },
    'medic_heli': {
        'SC': ('救护直升机', '呼叫救护直升机：两侧门炮发射治疗弹，命中队友即回复体力；炮手自动瞄准受伤的队友（含玩家），不攻击敌人。'),
        'CN': ('救護直升機', '呼叫救護直升機：兩側門砲發射治療彈，命中隊友即回復體力；砲手自動瞄準受傷的隊友（含玩家），不攻擊敵人。'),
        'JA': ('救護ヘリ', '救護ヘリを要請する。両側のドアガンが回復弾を撃ち、命中した味方の体力を回復する。射手は負傷した味方'
                         '（プレイヤーを含む）を自動で狙い、敵は攻撃しない。'),
        'EN': ('Medic Helis', 'Calls medic helicopters whose door guns fire healing rounds: a friend they hit gets health '
                              'back. Their gunners aim at hurt friends (you included) on their own and never at enemies.'),
    },
    'sortie_heli_strafe': {
        'SC': ('直升机扫射', '出击：两架 Eros 直升机并排低空掠过标记点，沿途用机炮和导弹一路扫射，飞过后直接离场。'),
        'CN': ('直升機掃射', '出擊：兩架 Eros 直升機並排低空掠過標記點，沿途用機砲和飛彈一路掃射，飛過後直接離場。'),
        'JA': ('ヘリ機銃掃射', '出撃。エロス 2 機が並んで指示地点の上を低空で通過し、機関砲とミサイルで一直線に掃射して、そのまま離脱する。'),
        'EN': ('Heli Strafing Pass', 'Sortie: two Eros helicopters sweep low side by side over the mark, raking it with guns '
                                 'and missiles on the way, and fly straight off.'),
    },
    'sortie_heli_hunter': {
        'SC': ('导弹猎手', '出击：一架 Eros 直升机在标记点外约 300 米处悬停，导弹逐发锁定附近不同的敌人连射，打完或 40 秒后离场。'),
        'CN': ('飛彈獵手', '出擊：一架 Eros 直升機在標記點外約 300 公尺處懸停，飛彈逐發鎖定附近不同的敵人連射，打完或 40 秒後離場。'),
        'JA': ('ミサイルハンター', '出撃。エロス 1 機が指示地点から約 300m の位置でホバリングし、近くの別々の敵を 1 発ずつロックして'
                          'ミサイルを連射する。撃ち尽くすか 40 秒で離脱する。'),
        'EN': ('Missile Hunter', 'Sortie: an Eros hovers some 300 m off the mark and fires its missiles one by one, each at a '
                             'different enemy near it, then leaves (empty or after 40 s).'),
    },
    'sortie_heli_circle': {
        'SC': ('死亡回旋', '出击：一架布鲁特直升机绕标记点低空飞一整圈，舱门机枪一直朝着圆心扫射，转完离场。'),
        'CN': ('死亡迴旋', '出擊：一架布魯特直升機繞標記點低空飛一整圈，艙門機槍一直朝著圓心掃射，轉完離場。'),
        'JA': ('デスサークル', '出撃。ブルート 1 機が指示地点の周りを低空で一周し、ドアガンで円の中心を撃ち続け、一周したら離脱する。'),
        'EN': ('Death Circle', 'Sortie: a Brute flies one low circle round the mark, its door guns firing into the middle all '
                           'the way round, then leaves.'),
    },
    'sortie_dive': {
        'SC': ('俯冲扫射', '出击：两架对地攻击机浅角度俯冲，先打火箭，再用机炮拉出直线弹着带，然后爬升离场。'),
        'CN': ('俯衝掃射', '出擊：兩架對地攻擊機淺角度俯衝，先打火箭，再用機砲拉出直線彈著帶，然後爬升離場。'),
        'JA': ('急降下掃射', '出撃。攻撃機 2 機が浅い角度で急降下し、ロケットを撃ってから機関砲で直線の着弾帯を引き、上昇して離脱する。'),
        'EN': ('Diving Strafing Run', 'Sortie: two strike jets dive in at a shallow angle, rockets first, then a straight line '
                                  'of cannon fire, and climb away.'),
    },
    'sortie_missile_pass': {
        'SC': ('导弹掠过', '出击：两架多用途机高速掠过标记点，各自把空对地导弹分别打向不同的目标，不减速飞走。'),
        'CN': ('飛彈掠過', '出擊：兩架多用途機高速掠過標記點，各自把空對地飛彈分別打向不同的目標，不減速飛走。'),
        'JA': ('ミサイル航過', '出撃。マルチロール機 2 機が高速で指示地点の上を航過し、空対地ミサイルを別々の目標へ撃ち分け、減速せずに飛び去る。'),
        'EN': ('Missile Pass', 'Sortie: two multirole jets sweep fast over the mark, each putting its air-to-ground missiles on '
                           'different targets, and fly on without slowing.'),
    },
    'sortie_carpet': {
        'SC': ('地毯轰炸', '出击：三架对地攻击机编队平飞过标记点，炸弹沿航线铺出约 300 米长的爆炸带后离场。'),
        'CN': ('地毯轟炸', '出擊：三架對地攻擊機編隊平飛過標記點，炸彈沿航線鋪出約 300 公尺長的爆炸帶後離場。'),
        'JA': ('絨毯爆撃', '出撃。攻撃機 3 機が編隊で指示地点の上を水平飛行し、爆弾で航路沿いに約 300m の爆発の帯を敷いて離脱する。'),
        'EN': ('Carpet Bombing', 'Sortie: three strike jets fly level in formation over the mark and lay a band of bombs about '
                             '300 m long along their track, then leave.'),
    },
    'sortie_popup': {
        'SC': ('打了就跑', '出击：一架对地攻击机低空进场，在标记点扔下两颗炸弹后急跃升离开。'),
        'CN': ('打了就跑', '出擊：一架對地攻擊機低空進場，在標記點扔下兩顆炸彈後急躍升離開。'),
        'JA': ('ヒット＆アウェイ', '出撃。攻撃機 1 機が低空で進入し、指示地点に爆弾を 2 発落としてから急上昇で離脱する。'),
        'EN': ('Hit and Run', 'Sortie: a strike jet comes in low, drops two bombs on the mark and climbs steeply away.'),
    },
    'sortie_missile_rain': {
        'SC': ('天顶导弹雨', '出击：一架截击机在远处高空朝标记点俯冲，把远程导弹全部射向附近的敌人，导弹从头顶扎下，打完离场。'
                        '远程导弹原本是对空武器，能否锁定地面目标未实机确认。'),
        'CN': ('天頂飛彈雨', '出擊：一架截擊機在遠處高空朝標記點俯衝，把遠程飛彈全部射向附近的敵人，飛彈從頭頂紮下，打完離場。'
                        '遠程飛彈原本是對空武器，能否鎖定地面目標未實機確認。'),
        'JA': ('天頂ミサイルレイン', '出撃。迎撃機 1 機が遠方の高空から指示地点へ降下しつつ、長射程ミサイルを近くの敵へ撃ち尽くす。'
                            'ミサイルは頭上から突っ込む。元は対空兵器のため、地上目標をロックできるかは実機未確認。'),
        'EN': ('Missile Rain', 'Sortie: an interceptor dives at the mark from high and far away, emptying its long-range '
                           'missiles at the enemies near it so they come down from overhead, then leaves. Those missiles are '
                           'air-to-air: whether they lock ground targets is not yet tested in game.'),
    },
    'sortie_hammer': {
        'SC': ('锤与砧', '出击组合：两架 Eros 直升机先低空扫射压制，25 秒后再呼叫一架对地攻击机低空投弹收尾（相当于「直升机扫射」接「打了就跑」）。'),
        'CN': ('錘與砧', '出擊組合：兩架 Eros 直升機先低空掃射壓制，25 秒後再呼叫一架對地攻擊機低空投彈收尾（相當於「直升機掃射」接「打了就跑」）。'),
        'JA': ('ハンマー＆アンビル', '出撃の組み合わせ。エロス 2 機がまず低空掃射で制圧し、25 秒後に攻撃機 1 機が低空爆撃で仕上げる'
                            '（「ヘリ機銃掃射」のあとに「ヒット＆アウェイ」）。'),
        'EN': ('Hammer and Anvil', 'Combined sortie: two Eros helicopters suppress the mark with a strafing pass, and 25 s later '
                               'a strike jet finishes it with a low bombing run (Heli Strafing Pass, then Hit and Run).'),
    },
    'sortie_wolfpack': {
        'SC': ('狼群', '出击：四架制空战斗机从远处扑来，各自俯冲扫射一轮后散开离场。'),
        'CN': ('狼群', '出擊：四架制空戰鬥機從遠處撲來，各自俯衝掃射一輪後散開離場。'),
        'JA': ('ウルフパック', '出撃。制空戦闘機 4 機が遠方から襲いかかり、それぞれ急降下掃射を 1 回行ってから散開して離脱する。'),
        'EN': ('Wolf Pack', 'Sortie: four fighters pounce from afar, each makes one diving strafing run and they scatter away.'),
    },
    'sortie_guided': {
        'SC': ('引导打击', '出击组合：炮艇机在标记点上空盘旋一圈并开火标定，25 秒后再呼叫两架对地攻击机俯冲扫射，然后各自撤离'
                      '（相当于炮艇机一圈接「俯冲扫射」）。'),
        'CN': ('引導打擊', '出擊組合：砲艇機在標記點上空盤旋一圈並開火標定，25 秒後再呼叫兩架對地攻擊機俯衝掃射，然後各自撤離'
                      '（相當於砲艇機一圈接「俯衝掃射」）。'),
        'JA': ('誘導攻撃', '出撃の組み合わせ。ガンシップが指示地点の上空を一周しながら射撃で目標を示し、25 秒後に攻撃機 2 機が急降下'
                      '掃射を行って、それぞれ離脱する（ガンシップ一周のあとに「急降下掃射」）。'),
        'EN': ('Guided Strike', 'Combined sortie: a gunship circles the mark once, firing to mark it, and 25 s later two strike '
                            'jets make their diving strafing runs; then all leave (a gunship lap, then Diving Strafing Run).'),
    },
    'barrage_katyusha': {
        'SC': ('火箭齐射', '远程炮击：标记一点后，约 2.4 公里外的火箭炮连续齐射 40 发，火箭从远处划过头顶，'
                      '覆盖标记点周围约 80 米，散布大。火箭从呼叫方向的后方飞来。'),
        'CN': ('火箭齊射', '遠程砲擊：標記一點後，約 2.4 公里外的火箭砲連續齊射 40 發，火箭從遠處劃過頭頂，'
                      '覆蓋標記點周圍約 80 公尺，散布大。火箭從呼叫方向的後方飛來。'),
        'JA': ('ロケット斉射', '長距離砲撃。地点を指示すると、約 2.4km 先のロケット砲が 40 発を連続斉射する。ロケットは頭上を越えて'
                         '飛来し、指示地点の周囲約 80m を広く覆う。ロケットは要請した向きの後方から飛んでくる。'),
        'EN': ('Rocket Salvo', 'Long-range fire: mark a point and a rocket battery about 2.4 km away ripples off 40 rockets. '
                           'They arc in over your head from behind you and blanket about 80 m round the mark, widely scattered.'),
    },
    'barrage_guided': {
        'SC': ('精确制导火箭', '远程炮击：6 发制导火箭从约 3 公里外高抛升空，到标记点上空后垂直扎下，'
                        '分别打标记点附近不同的敌人（不够就落在标记点周围），散布小。'),
        'CN': ('精確導引火箭', '遠程砲擊：6 發導引火箭從約 3 公里外高拋升空，到標記點上空後垂直紮下，'
                        '分別打標記點附近不同的敵人（不夠就落在標記點周圍），散布小。'),
        'JA': ('精密誘導ロケット', '長距離砲撃。誘導ロケット 6 発が約 3km 先から高く打ち上がり、指示地点の上空から垂直に突っ込む。'
                          '指示地点付近の別々の敵を 1 発ずつ狙う（足りなければ地点の周囲に落ちる）。散布は小さい。'),
        'EN': ('Precision Rockets', 'Long-range fire: six guided rockets climb high from about 3 km away and come straight down '
                                'over the mark, each on a different enemy near it (round the mark when there are fewer). Tight.'),
    },
    'barrage_thermobaric': {
        'SC': ('温压齐射', '远程炮击：约 1.4 公里外的重型火箭炮把 16 发温压火箭砸进标记点约 30 米的小块区域，每发爆炸半径都很大。'),
        'CN': ('溫壓齊射', '遠程砲擊：約 1.4 公里外的重型火箭砲把 16 發溫壓火箭砸進標記點約 30 公尺的小塊區域，每發爆炸半徑都很大。'),
        'JA': ('サーモバリック斉射', '長距離砲撃。約 1.4km 先の重ロケット砲が、サーモバリックロケット 16 発を指示地点の約 30m の狭い範囲に'
                            '叩き込む。1 発ごとの爆発半径が非常に大きい。'),
        'EN': ('Thermobaric Salvo', 'Long-range fire: a heavy launcher about 1.4 km away slams 16 thermobaric rockets into about '
                                '30 m round the mark, each with a very wide blast.'),
    },
    'barrage_cluster': {
        'SC': ('子母火箭', '远程炮击：12 发大型火箭从约 3.5 公里外飞来，在标记点上空约 260 米处分裂，每发撒下 12 颗子弹，'
                      '铺满约 140 米的一大片。'),
        'CN': ('子母火箭', '遠程砲擊：12 發大型火箭從約 3.5 公里外飛來，在標記點上空約 260 公尺處分裂，每發撒下 12 顆子彈，'
                      '鋪滿約 140 公尺的一大片。'),
        'JA': ('クラスターロケット', '長距離砲撃。大型ロケット 12 発が約 3.5km 先から飛来し、指示地点の上空約 260m で分裂、1 発あたり'
                            '12 個の子弾を約 140m の範囲にばらまく。'),
        'EN': ('Cluster Rockets', 'Long-range fire: twelve heavy rockets fly in from about 3.5 km and split some 260 m over the '
                              'mark, each scattering 12 bomblets over about 140 m.'),
    },
    'barrage_creeping': {
        'SC': ('徐进弹幕', '远程炮击：榴弹炮弹幕从标记点开始，一排 8 发、约 120 米宽，每 1.5 秒向呼叫方向前推 30 米，共 8 排。'
                      '炮弹约 9 秒后才落下。'),
        'CN': ('徐進彈幕', '遠程砲擊：榴彈砲彈幕從標記點開始，一排 8 發、約 120 公尺寬，每 1.5 秒向呼叫方向前推 30 公尺，共 8 排。'
                      '砲彈約 9 秒後才落下。'),
        'JA': ('移動弾幕射撃', '長距離砲撃。指示地点から榴弾の弾幕が始まり、幅約 120m の 8 発の列が 1.5 秒ごとに要請した向きへ 30m ずつ'
                         '前進する（8 列）。着弾まで約 9 秒かかる。'),
        'EN': ('Creeping Barrage', 'Long-range fire: a howitzer barrage starts on the mark, lines of 8 shells about 120 m wide, '
                               'walking 30 m on along your heading every 1.5 s, 8 lines in all. The first lands about 9 s in.'),
    },
    'barrage_mrsi': {
        'SC': ('同时弹着射击', '远程炮击：6 发炮弹以不同弧线先后发射，在同一瞬间砸到标记点。'),
        'CN': ('同時彈著射擊', '遠程砲擊：6 發砲彈以不同弧線先後發射，在同一瞬間砸到標記點。'),
        'JA': ('同時弾着射撃', '長距離砲撃。6 発の砲弾を異なる弾道で順に撃ち、同じ瞬間に指示地点へ着弾させる。'),
        'EN': ('Time-on-Target Fire', 'Long-range fire: six shells fired one after another on different arcs all land on the '
                                  'mark in the same instant.'),
    },
    'barrage_heavy': {
        'SC': ('重型臼炮', '远程炮击：一发巨型臼炮弹从约 1.6 公里外高抛砸中标记点，抹平据点。'),
        'CN': ('重型臼砲', '遠程砲擊：一發巨型臼砲彈從約 1.6 公里外高拋砸中標記點，抹平據點。'),
        'JA': ('重臼砲', '長距離砲撃。巨大な臼砲弾 1 発が約 1.6km 先から山なりに飛来して指示地点に直撃し、拠点を吹き飛ばす。'),
        'EN': ('Heavy Mortar', 'Long-range fire: one huge mortar round lobbed from about 1.6 km lands on the mark and flattens it.'),
    },
    'barrage_cruise': {
        'SC': ('巡航导弹', '远程打击：一枚巡航导弹从约 4.5 公里外贴地飞来，越过沿途的山和建筑，接近目标时跃升后俯冲命中标记点。'),
        'CN': ('巡航飛彈', '遠程打擊：一枚巡航飛彈從約 4.5 公里外貼地飛來，越過沿途的山和建築，接近目標時躍升後俯衝命中標記點。'),
        'JA': ('巡航ミサイル', '長距離攻撃。巡航ミサイル 1 発が約 4.5km 先から低空で飛来し、途中の山や建物を越え、目標の手前で'
                         '急上昇してから指示地点へ急降下する。'),
        'EN': ('Cruise Missile', 'Long-range strike: a cruise missile flies in low from about 4.5 km over the hills and roofs on '
                             'its way, pops up short of the mark and dives onto it.'),
    },
    'barrage_ballistic': {
        'SC': ('弹道导弹', '远程打击：远处一道烟柱冲天，约 20 秒后弹头近乎垂直地高速砸下标记点，爆炸极大。'),
        'CN': ('彈道飛彈', '遠程打擊：遠處一道煙柱沖天，約 20 秒後彈頭近乎垂直地高速砸下標記點，爆炸極大。'),
        'JA': ('弾道ミサイル', '長距離攻撃。遠くで煙の柱が空へ昇り、約 20 秒後に弾頭がほぼ真上から高速で指示地点に落ちる。爆発は非常に大きい。'),
        'EN': ('Ballistic Missile', 'Long-range strike: a column of smoke climbs far away, and some 20 s later the warhead comes '
                                'down almost straight onto the mark at great speed. A huge blast.'),
    },
    'ultimate': {
        'SC': ('天罚', '终极支援：用激光指示一点，倒计时后天色骤暗、雾气压近，标记点上空展开魔法阵，数道光束从高空会聚；'
                  '随后半径 220 米内雷暴连劈约 6 秒，最后一道巨型光柱贯下，爆炸由中心一圈圈推到边缘，余火燃烧。'
                  '整场约 25 秒，范围内敌我不分。'),
        'CN': ('天罰', '終極支援：用雷射指示一點，倒數後天色驟暗、霧氣壓近，標記點上空展開魔法陣，數道光束從高空會聚；'
                  '隨後半徑 220 公尺內雷暴連劈約 6 秒，最後一道巨型光柱貫下，爆炸由中心一圈圈推到邊緣，餘火燃燒。'
                  '整場約 25 秒，範圍內敵我不分。'),
        'JA': ('天罰', '最終支援。レーザーで地点を指示すると、カウントダウンの後に空が暗転し霧が迫り、魔法陣が展開して上空から'
                  '光線が収束する。続いて半径 220m に約 6 秒間雷が降り注ぎ、最後に巨大な光柱が貫いて、爆発が中心から'
                  '外縁へ輪になって広がる。全体で約 25 秒。範囲内は敵味方を区別しない。'),
        'EN': ('Divine Judgment', 'The ultimate support. Mark a point with the laser: after the countdown the sky goes dark, '
                              'fog closes in, magic circles open over the mark and beams converge on it from high above; '
                              'then a lightning storm strikes everything within 220 m for six seconds, a colossal beam '
                              'comes down and rings of blasts roll out from the centre to the edge, leaving fires. About '
                              '25 seconds in all; it hits friend and foe alike inside the area.'),
    },
    'heli': {
        'SC': ('武装直升机', '呼叫武装直升机，攻击附近的敌人。'),
        'CN': ('武裝直升機', '呼叫武裝直升機，攻擊附近的敵人。'),
        'JA': ('武装ヘリ', '武装ヘリを要請する。付近の敵を攻撃する。'),
        'EN': ('Gunships', 'Calls gunships that attack nearby enemies.'),
    },
}
KINDS.update({u.key: {lang: xm_units.text(u, lang) for lang in xm_units.LANGS} for u in xm_units.ported()})
MODES: dict[str, tuple[tuple[str, str], tuple[str, str]]] = {  # (hold, follow): (name suffix, sentence)
    'SC': (('·守点', '守在标记的地点上空。'), ('·跟随', '跟随呼叫者行动。')),
    'CN': (('·守點', '守在標記的地點上空。'), ('·跟隨', '跟隨呼叫者行動。')),
    'JA': (('（拠点）', 'マーカーで指定した地点の上空を守る。'), ('（随伴）', '要請した隊員に随伴する。')),
    'EN': ((' (Hold)', 'They hold the marked point.'), (' (Escort)', 'They escort the caller.')),
}
NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件；未安装时为普通 KM6 轰炸。',
    'CN': '需要 EDF6VehicleCrew 插件；未安裝時為普通 KM6 轟炸。',
    'JA': 'EDF6VehicleCrew プラグインが必要。未導入時は通常の KM6 による爆撃になる。',
    'EN': 'Needs the EDF6VehicleCrew plugin; without it this is a plain KM6 bomber call.',
}


VEHICLE_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件和 tools/make_jets.py 写入的飞机文件（EDF6VC_PJET_* / EDF6VC_FLY_*.SGO）。',
    'CN': '需要 EDF6VehicleCrew 插件和 tools/make_jets.py 寫入的飛機檔案（EDF6VC_PJET_* / EDF6VC_FLY_*.SGO）。',
    'JA': 'EDF6VehicleCrew プラグインと tools/make_jets.py が書き出す機体ファイル（EDF6VC_PJET_* / EDF6VC_FLY_*.SGO）が必要。',
    'EN': 'Needs the EDF6VehicleCrew plugin and the aircraft files tools/make_jets.py writes (EDF6VC_PJET_* / '
          'EDF6VC_FLY_*.SGO).',
}
# A plugin vehicle request whose files need more than tools/make_jets.py: its notes in place of VEHICLE_NOTES.
VEHICLE_NOTES_BY_KIND: dict[str, dict[str, str]] = {
    'dreadnought': {
        'SC': '需要 EDF6VehicleCrew 插件，以及安装器用无畏舰模型（models/dreadnought）生成的文件；没有模型时请求来的是普通的 N9 Eros 直升机。',
        'CN': '需要 EDF6VehicleCrew 插件，以及安裝器用無畏艦模型（models/dreadnought）產生的檔案；沒有模型時請求來的是普通的 N9 Eros 直升機。',
        'JA': 'EDF6VehicleCrew プラグインと、インストーラーがドレッドノートのモデル（models/dreadnought）から書き出すファイルが必要。'
              'モデルがない場合は通常の N9 エロス ヘリが来る。',
        'EN': 'Needs the EDF6VehicleCrew plugin and the files the installer makes from the Dreadnought model '
              '(models/dreadnought); without the model the request brings a plain N9 Eros heli.',
    },
    'sazabi': {
        'SC': '需要 EDF6VehicleCrew 插件，以及安装器用沙扎比模型（models/sazabi）生成的文件；没有模型时请求来的是普通的 N9 Eros 直升机。',
        'CN': '需要 EDF6VehicleCrew 插件，以及安裝器用沙薩比模型（models/sazabi）產生的檔案；沒有模型時請求來的是普通的 N9 Eros 直升機。',
        'JA': 'EDF6VehicleCrew プラグインと、インストーラーがサザビーのモデル（models/sazabi）から書き出すファイルが必要。'
              'モデルがない場合は通常の N9 エロス ヘリが来る。',
        'EN': 'Needs the EDF6VehicleCrew plugin and the files the installer makes from the Sazabi model (models/sazabi); '
              'without the model the request brings a plain N9 Eros heli.',
    },
}
THROW_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件和安装器生成的无人机文件；未安装插件时为普通巡逻炸弹。',
    'CN': '需要 EDF6VehicleCrew 插件和安裝器生成的無人機檔案；未安裝插件時為普通巡邏炸彈。',
    'JA': 'EDF6VehicleCrew プラグインとインストーラーが書き出すドローンのファイルが必要。未導入時は通常のパトローラーになる。',
    'EN': 'Needs the EDF6VehicleCrew plugin and the drone files the installer writes; without the plugin this is a plain '
          'Patroller.',
}
# A ground vehicle that needs no EDF6AutoTurret: its notes in place of GROUND_NOTES.
PLUGIN_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件，以及安装器写入的车辆文件。',
    'CN': '需要 EDF6VehicleCrew 插件，以及安裝器寫入的車輛檔案。',
    'JA': 'EDF6VehicleCrew プラグイン、およびインストーラーが書き出す車両ファイルが必要。',
    'EN': 'Needs the EDF6VehicleCrew plugin and the vehicle files the installer writes.',
}
GROUND_NOTES_BY_KIND: dict[str, dict[str, str]] = {'sidecar': PLUGIN_NOTES, 'sedan': PLUGIN_NOTES, 'vulcan': PLUGIN_NOTES, 'bmpt': PLUGIN_NOTES, 'quadvulcan': PLUGIN_NOTES, 'emp_mid': PLUGIN_NOTES, 'emp_short': PLUGIN_NOTES, 'emp_long': PLUGIN_NOTES, 'tunguska': PLUGIN_NOTES, 'pzh2000': PLUGIN_NOTES, 'sturmtiger': PLUGIN_NOTES, 'tos1': PLUGIN_NOTES, 'apocalypse': PLUGIN_NOTES, 'ciws_phalanx': PLUGIN_NOTES, 'flak88': PLUGIN_NOTES, 'gdf005': PLUGIN_NOTES, 'laser': PLUGIN_NOTES, 'railgun': PLUGIN_NOTES, 'patriot': PLUGIN_NOTES}
GROUND_NOTES_BY_KIND.update({u.key: PLUGIN_NOTES for u in xm_units.v603()})   # pylib/xm_units.py
GROUND_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 和 EDF6AutoTurret 插件，以及安装器写入的车辆文件。',
    'CN': '需要 EDF6VehicleCrew 和 EDF6AutoTurret 插件，以及安裝器寫入的車輛檔案。',
    'JA': 'EDF6VehicleCrew と EDF6AutoTurret のプラグイン、およびインストーラーが書き出す車両ファイルが必要。',
    'EN': 'Needs the EDF6VehicleCrew and EDF6AutoTurret plugins and the vehicle files the installer writes.',
}
ULTIMATE_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件和安装器生成的特效文件；未安装插件时为普通的卫星激光呼叫。',
    'CN': '需要 EDF6VehicleCrew 插件和安裝器產生的特效檔案；未安裝插件時為普通的衛星雷射呼叫。',
    'JA': 'EDF6VehicleCrew プラグインとインストーラーが書き出すエフェクトのファイルが必要。未導入時は通常の衛星レーザー要請になる。',
    'EN': 'Needs the EDF6VehicleCrew plugin and the effect files the installer writes; without the plugin this is a plain '
          'satellite laser call.',
}
BARRAGE_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件和安装器生成的弹药文件；未安装插件时为普通 KM6 轰炸。联机时房间里每台机器都要装同一版本插件。',
    'CN': '需要 EDF6VehicleCrew 插件和安裝器產生的彈藥檔案；未安裝插件時為普通 KM6 轟炸。連線時房間裡每台機器都要裝同一版本插件。',
    'JA': 'EDF6VehicleCrew プラグインとインストーラーが書き出す弾のファイルが必要。未導入時は通常の KM6 による爆撃になる。'
          'オンラインでは部屋の全員が同じ版のプラグインを入れる必要がある。',
    'EN': 'Needs the EDF6VehicleCrew plugin and the round files the installer writes; without the plugin this is a plain KM6 '
          'bomber call. Online every player in the room needs the same plugin version.',
}
GRAVITY_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件；未安装时为普通的激光导引装置。联机可用：房间里每台机器都要装同一版本插件（旧版本玩家看不到抓取与投掷）。',
    'CN': '需要 EDF6VehicleCrew 插件；未安裝時為普通的雷射導引裝置。連線可用：房間裡每台機器都要裝同一版本外掛（舊版本玩家看不到抓取與投擲）。',
    'JA': 'EDF6VehicleCrew プラグインが必要。未導入時は通常のレーザー誘導装置になる。オンライン対応：部屋の全員が同じ版のプラグインを入れていること（古い版の人には掴み・投げが見えない）。',
    'EN': 'Needs the EDF6VehicleCrew plugin; without it this is a plain Laser Guide Kit. Works online when every player has this plugin version (an older one does not see the grabs and throws).',
}


GUN_NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件；未安装时为普通的 ＫＦＦ５０ＬＳ 狙击枪。',
    'CN': '需要 EDF6VehicleCrew 插件；未安裝時為普通的 ＫＦＦ５０ＬＳ 狙擊槍。',
    'JA': 'EDF6VehicleCrew プラグインが必要。未導入時は通常のＫＦＦ５０ＬＳ狙撃銃になる。',
    'EN': 'Needs the EDF6VehicleCrew plugin; without it this is a plain KFF 50 LS sniper rifle.',
}


RETIRED_NOTE: dict[str, tuple[str, str]] = {   # an uninstalled call's row: (name suffix, description)
    'SC': ('（已卸载）', 'EDF6VehicleCrew 已卸载。本行只为保住武器表的行号（存档按行号记武器），效果与原版的{stock}相同。'),
    'CN': ('（已解除安裝）', 'EDF6VehicleCrew 已解除安裝。本行只為保住武器表的行號（存檔按行號記武器），效果與原版的{stock}相同。'),
    'JA': ('（アンインストール済み）', 'EDF6VehicleCrew はアンインストール済み。武器表の行番号を保つための行（セーブは行番号で'
                              '武器を記録する）で、効果は原版の{stock}と同じ。'),
    'EN': (' (uninstalled)', "EDF6VehicleCrew is uninstalled. This row only keeps the weapon table's row numbers (saves "
                             'record weapons by row); it works as the stock {stock}.'),
}


def _lang(lang: str) -> str:
    return 'EN' if lang == 'KR' else lang


def call_name(call: Call, lang: str) -> str:
    lang = _lang(lang)
    name = KINDS[call.kind][lang][0]
    return name + MODES[lang][call.follow][0] if call.modal else name


def call_description(call: Call, lang: str) -> str:
    lang = _lang(lang)
    if call.brings == 'vehicle':
        notes = GROUND_NOTES_BY_KIND.get(call.kind, GROUND_NOTES) if call.ground else \
            VEHICLE_NOTES_BY_KIND.get(call.kind, VEHICLE_NOTES)
        return KINDS[call.kind][lang][1] + '\n\n' + notes[lang]
    if call.brings == 'throw':
        return KINDS[call.kind][lang][1] + '\n\n' + THROW_NOTES[lang]
    if call.brings == 'gun':
        return KINDS[call.kind][lang][1] + '\n\n' + GUN_NOTES[lang]
    if call.brings == 'ultimate':
        return KINDS[call.kind][lang][1] + '\n\n' + ULTIMATE_NOTES[lang]
    if call.brings == 'gravity':
        return KINDS[call.kind][lang][1] + '\n\n' + GRAVITY_NOTES[lang]
    if call.brings == 'barrage':
        return KINDS[call.kind][lang][1] + '\n\n' + BARRAGE_NOTES[lang]
    if not call.modal:
        return KINDS[call.kind][lang][1] + '\n\n' + NOTES[lang]
    sep = ' ' if lang == 'EN' else ''
    return KINDS[call.kind][lang][1] + sep + MODES[lang][call.follow][1] + '\n\n' + NOTES[lang]


def retired_name(call: Call, lang: str) -> str:
    return call_name(call, lang) + RETIRED_NOTE[_lang(lang)][0]


def retired_description(lang: str, stock_name: str) -> str:
    """`stock_name`: the template's own name in that language (the stock weapon the row now is)."""
    return RETIRED_NOTE[_lang(lang)][1].format(stock=stock_name)
