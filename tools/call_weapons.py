"""Air Raider call weapons for the plugin's jets and helicopters: the rows tools/calls.py lists, added to the
shared weapon table. The calls are clones of the stock eWeapon051 (Combat Bomber KM6, Weapon_RadioContact,
category 312) with a marker in its SGO's AmmoHitSizeAdjust. The plugin reads the marker (weapon +0x8C4)
and flies its own planes for the call; the field does nothing for a RadioContact weapon, so without the
plugin the weapon is simply a working KM6 bomber call.
The vehicle requests (Call.brings 'vehicle') are clones of the stock eWeapon394 (N9 Eros, Weapon_Sub,
category 308): the stock request brings the player jet SGO (tools/make_jets.py EDF6VC_PJET_*.SGO, which
must be installed first) with the jet's mark and guns in the request's vehicle setup, empty, for the player
to fly (src/playerjet.cpp).
The thrown drones (Call.brings 'throw') are clones of the stock eWeapon217 (Patroller, Weapon_Sub, category 331,
the Robot Bomb list) with a marker in AmmoHitSizeAdjust (calls.throw_mark: about 1.0004, so the bomb hits as the
stock one): the plugin turns the bomb into its drone where it lands (src/airstrike.cpp kThrows); without the
plugin the weapon is a plain Patroller.

  python tools/call_weapons.py build OUTDIR [--game DIR]     write the SGOs + the stacked tables into OUTDIR
  python tools/call_weapons.py install [--game DIR]          into <game>/Mods (refuses while EDF6 runs)
  python tools/call_weapons.py uninstall [--delete-rows --unequipped] [--game DIR]
  python tools/call_weapons.py repair [--game DIR]           the shared files back to before the first install
  python tools/call_weapons.py check [--game DIR]

The weapon table and its texts (WEAPONTEXT.<LANG>.SGO, index-aligned with the table) are shared with
other mods (autoturret/tools/describe.py rewrites text rows in place), so the base is always the
installed Mods/WEAPON copy when there is one, else Root.cpk. Saves refer to weapons by row index, and the
plugin's GrantCalls sets the owned bit by row index too, so a row of ours never moves and its index is never
given to anything else:
  - install keeps every row of ours where it is (found by id; tools/calls.py slot_of) and replaces it in place;
    the calls a table lacks go at its end, in CALLS order. Every other row is checked unchanged before anything
    is written (verify).
  - uninstall turns our rows into placeholders (the template's stock row under the id EDF6VC_RETIRED_*, named
    as uninstalled): the index stays taken, a save with one equipped still has a working stock weapon, and no
    later mod's row moves or inherits our owned bits. --delete-rows really deletes the rows at the very end of
    the table (only those: deleting one in the middle would move every row after it), for saves where none of
    them is equipped (--unequipped: such a save crashes at the main menu once the row is gone, the menu looks
    the weapon up past the table end; see edf6-jaeger tools/install.py). A later install takes the
    placeholders' rows back.

Writes are one transaction: every file the run changes is first copied to Mods/.edf6vc_backup/txn/ and listed
in a journal (Mods/.edf6vc_calls.txn.json), then each is replaced atomically (temporary file + rename); any
failure rolls all of them back from the copies, and a run that died on the way (the journal still there) is
rolled back by the next one before it starts. install also keeps, once, a copy of each shared file as it was
before our first install (Mods/.edf6vc_backup/), which repair restores when the texts no longer line up with
the table; Mods/.edf6vc_calls.json records what we wrote (with its SHA-256, to tell what another tool changed
since) and where each of our rows is. The manifest is one of the transaction's files: what it records (the
first backups above all) is written with the files it describes, or rolled back with them.
"""
from __future__ import annotations

import argparse
import copy
import json
import os
import shutil
import struct
import sys
from dataclasses import dataclass
from functools import lru_cache

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import calls  # noqa: E402
import dsgo  # noqa: E402
import ported_weapons as pw  # noqa: E402
import ledger  # noqa: E402
import modfiles  # noqa: E402
import vcobjects as vc  # noqa: E402
from calls import CALLS, IDS, Call, call_name  # noqa: E402
from dsgo import Node  # noqa: E402

TEMPLATE = 'eWeapon051'          # Combat Bomber KM6
VEHICLE_TEMPLATE = 'eWeapon394'  # N9 Eros (a heli vehicle request)
THROW_TEMPLATE = 'eWeapon217'    # Patroller (a Robot Bomb: Weapon_Sub, BombBullet01)
LANGS = ('JA', 'EN', 'CN', 'KR', 'SC')
TABLE = 'WEAPON/WEAPONTABLE.SGO'
TEXTS = [f'WEAPON/WEAPONTEXT.{lang}.SGO' for lang in LANGS]
SHARED = [TABLE] + TEXTS
BACKUP = '.edf6vc_backup'
MANIFEST = '.edf6vc_calls.json'
JOURNAL = '.edf6vc_calls.txn.json'
ACQUIRE = 0.0   # WEAPONTABLE column 5: 0 normal (the plugin makes EDF6VC_CALL_* owned at every save load)
PROCESS = modfiles.PROCESS
OWNER = 'calls'  # pylib/ledger.py: the vehicle requests need make_jets' player jet SGOs


class Misaligned(Exception):
    """The texts no longer have a row per table row: a run of an older version that died half way, or another
    tool. Nothing can be stacked on it; repair restores the state before our first install."""


def sgo_file(call: Call) -> str:
    """Mods/WEAPON file name, upper case like the other mod weapons there (EWEAPON389.SGO)."""
    return f'WEAPON/{call.id.upper()}.SGO'


def vehicle_file(call: Call) -> str:
    return f'OBJECT/{call.vehicle.upper()}.SGO'


def our_sgos() -> list[str]:
    """Every weapon SGO install writes: the calls' and the earlier games' (tools/ported_weapons.py)."""
    return [sgo_file(c) for c in CALLS] + [pw.sgo_file(p) for p in pw.PORTS]


# ---------------------------------------------------------------- reading the base


@lru_cache(maxsize=None)
def _game(game_root: str) -> vc.Game:
    return vc.Game(game_root)


def stock(game_root: str, rel: str) -> bytes:
    folder, name = rel.split('/')
    return _game(game_root).read(folder, name)


def _mods(game_root: str, *rel: str) -> str:
    return os.path.join(game_root, 'Mods', *[p for r in rel for p in r.split('/')])


def base(game_root: str, rel: str) -> bytes:
    """The installed Mods copy when present, else Root.cpk."""
    path = _mods(game_root, rel)
    if os.path.isfile(path):
        with open(path, 'rb') as f:
            return f.read()
    return stock(game_root, rel)


def _rows(doc: dsgo.Document, rel: str) -> list:
    return doc.root.get('table' if rel == TABLE else 'text_table').items


def row_ids(table: bytes) -> list[str]:
    return [r.items[0] for r in dsgo.parse(table).root.get('table').items]


def template_of(call: Call) -> str:
    """The stock row a call's row and SGO are made from: the bomber call, the N9 Eros request (a player jet), a
    ground vehicle's own request (vcobjects.GROUND_VEHICLES: the Naegling's for the Katyusha), or the Patroller (a
    thrown drone: its row, and so its place in the Robot Bomb list, category 331)."""
    if call.gun:
        return call.gun
    if call.ground:
        return vc.GROUND_VEHICLES[call.ground].request
    if call.brings == 'throw':
        return THROW_TEMPLATE
    return VEHICLE_TEMPLATE if call.brings == 'vehicle' else TEMPLATE


def templates() -> tuple[str, ...]:
    """Every template a call uses, the bomber's and the Eros's first."""
    out = [TEMPLATE, VEHICLE_TEMPLATE]
    for c in CALLS:
        if template_of(c) not in out:
            out.append(template_of(c))
    return tuple(out)


def _template_index(ids: list[str], template: str = TEMPLATE) -> int:
    upper = [x.upper() for x in ids]
    return upper.index(template.upper())


def check_aligned(game_root: str, n: int) -> None:
    """Every text has a row per table row (`n`), else Misaligned (with what to do)."""
    bad = [(rel, len(_rows(dsgo.parse(base(game_root, rel)), rel))) for rel in TEXTS]
    bad = [(rel, k) for rel, k in bad if k != n]
    if not bad:
        return
    have = os.path.isfile(_manifest_path(game_root))
    fix = ('run `python tools/call_weapons.py repair` (or the installer, which offers it): it puts the weapon table '
           'and its texts back as they were before EDF6VehicleCrew first installed (Mods/.edf6vc_backup); then install '
           'again' if have else 'EDF6VehicleCrew never installed into these files, so it has no copy of them to restore: '
           'reinstall the mod that wrote them, or delete them from Mods/WEAPON to fall back to the stock ones')
    raise Misaligned(f'the weapon texts do not line up with the weapon table ({n} rows): '
                     + ', '.join(f'{rel} has {k}' for rel, k in bad) + f'. To fix it, {fix}.')


# ---------------------------------------------------------------- where the rows go


def slot_of(row_id: str) -> str | None:
    """The call or earlier game's weapon (tools/ported_weapons.py) a table row belongs to, its own row or its placeholder."""
    return calls.slot_of(row_id) or pw.slot_of(row_id)


@dataclass
class Plan:
    at: dict[str, int]      # call / EDF5 weapon id -> its row index in the result
    appended: list[str]     # the ids added at the table's end, in that order


def plan_rows(ids: list[str]) -> Plan:
    """Where each call's row and each EDF5 weapon's goes in a table whose row ids are `ids`: a row of ours already
    there (or the placeholder an uninstall left) stays where it is, whatever order an older install put them in; the
    rest go at the end, the calls in CALLS order, then the EDF5 weapons in the registry's. Every EDF5 weapon gets its
    row whether this machine can build it or not (stack puts a placeholder then), so every install of a release has
    the same rows at the same indices, with or without EDF5. Raises when one has two rows."""
    at: dict[str, int] = {}
    for i, x in enumerate(ids):
        c = slot_of(x)
        if c is None:
            continue
        if c in at:
            raise ValueError(f'{c} has two rows in the weapon table: {at[c]} and {i}')
        at[c] = i
    appended = [c for c in IDS + pw.IDS if c not in at]
    for k, c in enumerate(appended):
        at[c] = len(ids) + k
    return Plan(at, appended)


def tail_start(ids: list[str]) -> int:
    """Where the run of our rows (or placeholders) that ends the table starts: only those can be deleted
    without moving another row."""
    i = len(ids)
    while i > 0 and slot_of(ids[i - 1]) is not None:
        i -= 1
    return i


def _put(rows: list, at: int, row: Node) -> None:
    if at < len(rows):
        rows[at] = row
    elif at == len(rows):
        rows.append(row)
    else:
        raise AssertionError(f'row {at} past the end ({len(rows)})')


# ---------------------------------------------------------------- building


# The stock heli's weapons in the request's vehicle setup and resources -> the player jet's (vcobjects.JETS).
_VEHICLE_SWAP = {
    'app:/weapon/v_506heli_gatling01_l.sgo': vc._GUNS[0],
    'app:/weapon/v_506heli_gatling01_r.sgo': vc._GUNS[1],
}


def vehicle_weapons(call: Call) -> tuple[str, ...]:
    """The weapons a vehicle request's setup carries: the player jet's (vcobjects.JETS) or the ground vehicle's."""
    return vc.GROUND_VEHICLES[call.ground].weapons if call.ground else vc.JETS[call.jet].weapons


def vehicle_needs(call: Call, request: bytes | None = None) -> list[str]:
    """What a vehicle request's SGO names and so needs installed (by tools/make_jets.py, or the ground vehicle's own
    tool): the vehicle SGO and its weapons of ours (EDF6VC_*). A stock weapon it keeps (the sidecar bike's guns and
    fuel tank) is the game's own, in Root.cpk: nothing to install."""
    weapons = vehicle_weapons(call)
    if call.jet == vc.SAZABI_JET and request is not None:
        # Follow the prepared/installed request, not today's model directory: the model may have been
        # added or removed since these bytes were built. The stock-Eros fallback has only stock weapons.
        setup = dsgo.parse(request).root.get('Ammo_CustomParameter').items[4].items[3]
        weapons = tuple(w.items[0] for w in setup.items[3].items)
    ours = [w for w in weapons if w.split('/')[-1].upper().startswith('EDF6VC_')]
    return [vehicle_file(call)] + [f'WEAPON/{w.split("/")[-1].upper()}' for w in ours]


def _object_path(call: Call) -> str:
    return f'app:/object/{call.vehicle.lower()}.sgo'


def _with_path(entry, path: str):
    """A copy of a vehicle weapon list entry ([path, [params]]) for another weapon."""
    import copy
    e = copy.deepcopy(entry)
    e.items[0] = path
    return e


# The stock N9 Eros requests (the template and its five stronger versions): each multiplies its vehicle's durability and
# weapons' damage by its vehicle setup's [0] (1.3 at level 0.44 ... 25 at 3.42). A player jet request (no difficulty
# scaling: a request is not a script's CreateFriend) takes the multiplier its own level has on that curve.
EROS_REQUESTS = ('eWeapon394', 'eWeapon395', 'eWeapon396', 'eWeapon397', 'eWeapon398', 'eWeapon399')


def request_family(call: Call | None) -> tuple[str, ...]:
    """The stock requests whose levels and multipliers set a vehicle request's: the Eros's, or a ground vehicle's own."""
    return vc.GROUND_VEHICLES[call.ground].family if call is not None and call.ground else EROS_REQUESTS


def request_curve(game_root: str, family: tuple[str, ...] = EROS_REQUESTS) -> list[tuple[float, float, float]]:
    """(level, durability multiplier, damage multiplier) of each stock request of `family` (the Eros's), by level."""
    rows = {str(r.items[0]).upper(): r for r in _rows(dsgo.parse(stock(game_root, TABLE)), TABLE)}
    curve = []
    for w in family:
        level = float(rows[w.upper()].items[4])
        mult = dsgo.parse(stock(game_root, f'WEAPON/{w.upper()}.SGO')).root.get('Ammo_CustomParameter').items[4].items[3].items[0]
        curve.append((level, float(mult.items[0]), float(mult.items[1])))
    return sorted(curve)


def request_tier(curve: list[tuple[float, float, float]], level: float) -> tuple[float, float]:
    """The multipliers a request of `level` has on the stock curve (linear between its points, held past its ends)."""
    if level <= curve[0][0]:
        return curve[0][1], curve[0][2]
    for (l0, d0, w0), (l1, d1, w1) in zip(curve, curve[1:]):
        if level <= l1:
            t = (level - l0) / (l1 - l0)
            return d0 + (d1 - d0) * t, w0 + (w1 - w0) * t
    return curve[-1][1], curve[-1][2]


def vehicle_sgo(template: bytes, call: Call, tier: tuple[float, float], *, fallback: bool = False) -> bytes:
    """The N9 Eros request bringing the player jet. Ammo_CustomParameter[4] = [transport, box, vehicle SGO,
    vehicle setup [multipliers, heli params (first: the speed gain k = the jet's mark), fuel, weapons],
    voice lines]; `resource` preloads the same paths."""
    doc = dsgo.parse(template)
    r = doc.root
    r.get('ReloadTime').items[0] = float(call.reload)
    req = r.get('Ammo_CustomParameter').items[4]
    stock_vehicle = req.items[2]
    req.items[2] = _object_path(call)
    if fallback:
        # make_sazabi writes an unchanged V506_HELI under this stable alias. Keep the Eros request's
        # mark, holder count, weapons and multipliers too; its setup overrides the object's on spawn.
        if call.jet != vc.SAZABI_JET:
            raise ValueError('only the Sazabi has a stock vehicle fallback')
        res = r.get('resource')
        res.items = [_object_path(call) if x.lower() == stock_vehicle.lower() else x for x in res.items]
        for lang in LANGS:
            key = f'name.{lang.lower()}'
            if key in r.names.values():
                r.set(key, call_name(call, lang))
        return dsgo.write(doc)
    setup = req.items[3]
    setup.items[0].items[0], setup.items[0].items[1] = tier   # request_tier: its level's on its family's curve
    if not call.ground:
        setup.items[1].items[0] = float(call.mark)   # the heli params' speed gain: the jet's mark
    # The weapon list: the setup's last entry ([multipliers, heli params, fuel, weapons] in the Eros's request;
    # [multipliers, vehicle params, weapons] in a ground vehicle's, the Naegling's; [multipliers, bike params, fuel,
    # weapons] in the Freed bike's, the sidecar's).
    at = len(setup.items) - 1 if call.ground else 3
    weapons = setup.items[at].items
    entry = weapons[0]
    jet_weapons = vehicle_weapons(call)
    swap = {stock_vehicle.lower(): _object_path(call), **_VEHICLE_SWAP}
    if call.ground:
        # The ground vehicle's weapons in its request's entries' places (the stock request's per-weapon parameters):
        # one entry a holder of its vehicle_weapon_setting, as the stock one.
        if len(weapons) != len(jet_weapons):
            raise ValueError(f'{template_of(call)}: {len(weapons)} weapons in its setup, {call.ground} has {len(jet_weapons)}')
        swap.update({str(w.items[0]).lower(): new for w, new in zip(weapons, jet_weapons)})
        setup.items[at].items = [_with_path(w, new) for w, new in zip(weapons, jet_weapons)]
    else:
        # The jet's own weapons (its guns and stores, vcobjects.JETS) with the Eros's fuel tank where the jet SGO has
        # it (vcobjects.with_fuel): as many entries, in the same order, as its vehicle_weapon_setting holders
        # (src/stores.cpp builds one weapon a holder from this list).
        fuel = weapons[-1]
        if 'fuel' not in str(fuel.items[0]).lower():
            raise ValueError(f'{VEHICLE_TEMPLATE}: its weapon list does not end in the fuel tank')
        setup.items[at].items = vc.with_fuel([_with_path(entry, w) for w in jet_weapons], fuel)
    res = r.get('resource')
    items = [swap.get(x.lower(), x) for x in res.items]
    res.items = items + [w for w in jet_weapons if w not in {x.lower() for x in items}]
    for lang in LANGS:
        key = f'name.{lang.lower()}'
        if key in r.names.values():
            r.set(key, call_name(call, lang))
    return dsgo.write(doc)


def vehicle_durability(game_root: str, call: Call) -> float:
    """What the menu shows: the vehicle's durability times the request's HP multiplier (vehicle_sgo: request_tier)."""
    hp = vc.GROUND_VEHICLES[call.ground].durability if call.ground else vc.JETS[call.jet].durability
    return hp * request_tier(request_curve(game_root, request_family(call)), call.level)[0]


def throw_sgo(template: bytes, call: Call) -> bytes:
    """The Patroller with the thrown drone's marker (calls.throw_mark), its magazine (AmmoCount[0]: the bombs, so the
    drones, a reload), its reload (frames) and names; all else the stock bomb's (thrown and landing as it does)."""
    doc = dsgo.parse(template)
    r = doc.root
    r.set('AmmoHitSizeAdjust', float(call.mark))
    r.get('AmmoCount').items[0] = float(call.count)
    r.get('ReloadTime').items[0] = float(call.reload)
    for lang in LANGS:
        key = f'name.{lang.lower()}'
        if key in r.names.values():
            r.set(key, call_name(call, lang))
    return dsgo.write(doc)


# The boarding gun (Call.brings 'gun'), a debugging tool (the user, 2026-10-07: "it should arrive at once, with no
# spread, reload at once or never run out"): each curve's base (the star curves keep their template's shape) and
# scalar set here. Its rounds go 750 m a frame for 2 frames: 1500 m in 33 ms (the stock shape cast still decides what
# it hits, src/boarding.cpp); no spread, no recoil; 999 rounds a magazine, reloaded
# in a frame, 10 shots a second (the stock bolt waits 90 frames).
GUN_CURVES = {'AmmoSpeed': 750.0, 'FireAccuracy': 0.0, 'AmmoCount': 999.0, 'ReloadTime': 1.0, 'FireInterval': 6.0}
GUN_SCALARS = {'AmmoAlive': 2.0, 'FireRecoil': 0.0, 'AmmoExplosion': 0.0,
               'AmmoGravityFactor': 0.0, 'AmmoIsPenetration': 0.0, 'AmmoOwnerMove': 0.0,
               'FireCount': 1.0, 'FireBurstCount': 1.0}
FPS = 60.0   # the menu shows m/frame as m/s, frames as seconds


# ...and carry its tag in AmmoColor's alpha: the float 1 + mark ulps (src/boarding.cpp kTagBits compares the bits). The
# colour is the one bullet parameter copied as it is (no star curve, no fire modifier) that only the drawing reads.
GUN_TAG = 'AmmoColor'


def gun_tag_bits(mark: float) -> int:
    return 0x3F800000 + int(mark)


def gun_tag(color: Node, mark: float) -> Node:
    """The template's AmmoColor [r, g, b, a] with a = the tag (gun_tag_bits as a float)."""
    tagged = copy.deepcopy(color)
    while len(tagged.items) < 4:
        tagged.items.append(1.0)
    tagged.items[3] = struct.unpack('<f', struct.pack('<I', gun_tag_bits(mark)))[0]
    return tagged


def gun_sgo(template: bytes, call: Call) -> bytes:
    """The boarding gun: the stock sniper rifle (laser sight, scope) under its own name, its rounds tagged with the
    call's mark (GUN_TAG, which src/boarding.cpp reads off the bullet), GUN_CURVES / GUN_SCALARS set."""
    doc = dsgo.parse(template)
    r = doc.root
    r.set(GUN_TAG, gun_tag(r.get(GUN_TAG), call.mark))
    # Each class retains its model/animation, weapon class and sight. Use a direct-hit solid
    # round: the Wing Diver lightning and Raider limpet have different collision/damage paths.
    r.set('AmmoClass', 'SolidBullet01')
    r.set('AmmoModel', 0.0)
    r.set('Ammo_CustomParameter', Node([]))
    if call.gun == 'eWeapon120':
        r.set('SecondaryFire_Type', 1.0)
        r.set('SecondaryFire_Parameter', 5.5)
    for key, base in GUN_CURVES.items():
        field = r.get(key)
        if isinstance(field, Node):
            field.items[0] = float(base)
        else:
            r.set(key, float(base))
    for key, value in GUN_SCALARS.items():
        r.set(key, float(value))
    for lang in LANGS:
        key = f'name.{lang.lower()}'
        if key in r.names.values():
            r.set(key, call_name(call, lang))
    return dsgo.write(doc)


def weapon_sgo(template: bytes, call: Call, curve: list[tuple[float, float, float]] | None = None,
               *, fallback: bool = False) -> bytes:
    if call.brings == 'gun':
        return gun_sgo(template, call)
    if call.brings == 'vehicle':
        return vehicle_sgo(template, call, request_tier(curve or [], call.level), fallback=fallback)
    if call.brings == 'throw':
        return throw_sgo(template, call)
    doc = dsgo.parse(template)
    r = doc.root
    r.set('AmmoHitSizeAdjust', float(call.mark))
    reload = r.get('ReloadTime')
    reload.items[0] = float(call.reload)
    custom = r.get('Ammo_CustomParameter').items[2]
    custom.items[1] = float(call.count)
    for lang in LANGS:
        key = f'name.{lang.lower()}'
        if key in r.names.values():
            r.set(key, call_name(call, lang))
    return dsgo.write(doc)


def _table_row(template: Node, call: Call) -> Node:
    row = copy.deepcopy(template)
    stock_path = template.items[1]
    prefix = stock_path[:stock_path.rfind('/') + 1]           # app:/weapon/
    suffix = stock_path[stock_path.rfind('.'):]                 # .sgo
    row.items[0] = call.id
    row.items[1] = f'{prefix}{call.id}{suffix}'
    row.items[4] = float(call.level)
    row.items[5] = ACQUIRE
    return row


def gun_stats(template: bytes) -> list[tuple[float, list, float]]:
    """What the gun's menu lines become: (the template line's base, its curve's star parameters, the gun's base). A
    line shows a curve in its own unit (the base times 1, FPS or 1/FPS: rounds, ROF, damage, accuracy, reload seconds,
    shot speed m/s) or the range (AmmoSpeed's base times AmmoAlive), its star parameters the field's."""
    r = dsgo.parse(template).root
    out = []
    for key, base in GUN_CURVES.items():
        value = r.get(key)
        if not isinstance(value, Node):
            continue
        curve = [float(x) for x in value.items]
        for f in (1.0, FPS, 1.0 / FPS):
            out.append((curve[0] * f, curve[1:-1], float(base) * f))
    for key, base in GUN_SCALARS.items():
        value = r.get(key)
        if isinstance(value, Node):
            curve = [float(x) for x in value.items]
            out.append((curve[0], curve[1:-1], float(base)))
    speed = [float(x) for x in r.get('AmmoSpeed').items]
    out.append((speed[0] * float(r.get('AmmoAlive')), speed[1:-1], GUN_CURVES['AmmoSpeed'] * GUN_SCALARS['AmmoAlive']))
    return out


def _text_row(template: Node, call: Call, lang: str, durability: float | None = None,
              gun: list[tuple[float, list, float]] | None = None) -> Node:
    row = copy.deepcopy(template)
    row.items[0] = call_name(call, lang)
    row.items[1] = calls.call_description(call, lang)
    if call.brings == 'throw':
        _throw_stats(row, call)
        return row
    # A vehicle request's stats: [re-request, durability, fuel, fuel cost]; the durability is the jet's.
    stats = row.items[2].items
    if durability is not None and len(stats) > 1 and len(stats[1].items) == 2:
        stats[1].items[1] = f'{durability:.0f}'
    if call.brings == 'gun':
        # A gun's lines that show a field it sets (gun_stats), told by their values (the labels differ by language).
        # The last star parameter differs between a field and its line (the menu's own direction flag): not compared.
        changed = 0
        for st in row.items[2].items:
            if len(st.items) != 3 or not isinstance(st.items[2], Node):
                continue
            curve = [float(x) for x in st.items[2].items]
            for old, stars, new in gun or []:
                if curve[1:-1] == stars and abs(curve[0] - old) <= 1e-6 * max(1.0, abs(old)):
                    st.items[2].items[0] = new
                    changed += 1
                    break
        if changed < 5:
            raise ValueError(f'{call.id} {lang}: only {changed} of its template text lines matched its fields')
        return row
    # The stat list stays KM6's, except the reload line's curve, which the game shows as $0pt
    # from that list: it must be the weapon's own ReloadTime or the menu shows KM6's 1020.
    for stat in row.items[2].items:
        if len(stat.items) == 3 and str(stat.items[1]).startswith('$0pt') and isinstance(stat.items[2], Node):
            stat.items[2].items[0] = float(call.reload)
    return row


# The Patroller's stat list: [count, damage, search distance, reload, blast range]. A thrown drone's bomb never goes
# off as the Patroller's, so only its count and reload (seconds, the list's curve: ReloadTime frames / 60) stay.
_THROW_COUNT_STAT, _THROW_RELOAD_STAT = 0, 3


def _throw_stats(row: Node, call: Call) -> None:
    stats = row.items[2].items
    if len(stats) <= _THROW_RELOAD_STAT:
        return   # another tool rewrote the Patroller's text: left as it is
    keep = [stats[_THROW_COUNT_STAT], stats[_THROW_RELOAD_STAT]]
    for stat, value in zip(keep, (float(call.count), float(call.reload) / 60.0)):
        if len(stat.items) == 3 and isinstance(stat.items[2], Node) and stat.items[2].items:
            stat.items[2].items[0] = value
    row.items[2].items = keep


def _retired_table_row(template: Node, call: Call) -> Node:
    """The placeholder for an uninstalled call: the template's own stock row (its stock SGO, level, acquire)."""
    row = copy.deepcopy(template)
    row.items[0] = calls.retired_id(call.id)
    return row


def _retired_text_row(template: Node, call: Call, lang: str) -> Node:
    row = copy.deepcopy(template)
    row.items[0] = calls.retired_name(call, lang)
    row.items[1] = calls.retired_description(lang, str(template.items[0]))
    return row


@dataclass
class Shared:
    """The shared table and its texts as they are now (Mods, else stock), parsed."""
    table: dsgo.Document
    texts: dict[str, dsgo.Document]

    @property
    def rows(self) -> list:
        return _rows(self.table, TABLE)

    @property
    def ids(self) -> list[str]:
        return [r.items[0] for r in self.rows]

    def text_rows(self, rel: str) -> list:
        return _rows(self.texts[rel], rel)


def load_shared(game_root: str) -> Shared:
    table = dsgo.parse(base(game_root, TABLE))
    shared = Shared(table, {rel: dsgo.parse(base(game_root, rel)) for rel in TEXTS})
    check_aligned(game_root, len(shared.rows))
    return shared


def stack(game_root: str) -> dict[str, bytes]:
    """The SGOs and the shared table + texts with every call in (plan_rows), keyed by path under Mods. Reads
    only; raises (Misaligned, ValueError) before anything could be written."""
    s = load_shared(game_root)
    before = s.ids
    ports, left_out = pw.build(game_root, lambda rel: stock(game_root, rel))
    if left_out:
        reasons = sorted(set(left_out.values()))
        print(f'Ported weapons (EDF5, EDF4.1): {len(ports)} of {len(pw.PORTS)} built, {len(left_out)} wait as placeholders: '
              + '; '.join(reasons[:3]) + (' ...' if len(reasons) > 3 else ''))
    plan = plan_rows(before)
    tpl = {t: _template_index(before, t) for t in templates()}
    template_sgo = {t: stock(game_root, f'WEAPON/{t.upper()}.SGO') for t in tpl}
    curves = {request_family(c): request_curve(game_root, request_family(c)) for c in CALLS if c.brings == 'vehicle'}
    import sazabi_model
    fallback = sazabi_model.model_dir() is None
    out: dict[str, bytes] = {sgo_file(c): weapon_sgo(template_sgo[template_of(c)], c, curves.get(request_family(c)),
                                                fallback=fallback and c.jet == vc.SAZABI_JET)
                             for c in CALLS}
    order = sorted(CALLS, key=lambda c: plan.at[c.id])   # appended rows in their order
    # An EDF5 weapon this run cannot build keeps the row an earlier install gave it (its SGO is still in Mods), else
    # its row is a placeholder until EDF5 is there (pw.pending_*): its index is taken either way.
    kept = {p.id for p in pw.PORTS if p.id not in ports and plan.at[p.id] < len(before)
            and before[plan.at[p.id]] == p.id and os.path.isfile(_mods(game_root, pw.sgo_file(p)))}
    put_ports = [p for p in pw.PORTS if p.id not in kept]
    port_tpl = {p.id: _template_index(before, p.template) for p in put_ports}
    out.update({pw.sgo_file(p): ports[p.id] for p in put_ports if p.id in ports})
    rows = s.rows
    row_template = {c.id: rows[tpl[template_of(c)]] for c in CALLS}
    port_rows = {p.id: pw.table_row(rows[port_tpl[p.id]], p) if p.id in ports
                 else pw.pending_table_row(rows[port_tpl[p.id]], p) for p in put_ports}
    puts = sorted([(plan.at[c.id], _table_row(row_template[c.id], c)) for c in order]
                  + [(plan.at[p.id], port_rows[p.id]) for p in put_ports], key=lambda x: x[0])
    for at, row in puts:
        _put(rows, at, row)
    out[TABLE] = dsgo.compact(s.table)
    durability = {c.id: vehicle_durability(game_root, c) if c.brings == 'vehicle' else None for c in CALLS}
    if fallback:
        import sgo
        hp = float(sgo.load(data=stock(game_root, 'OBJECT/V506_HELI.SGO'))['game_object_durability'])
        mult = dsgo.parse(template_sgo[VEHICLE_TEMPLATE]).root.get('Ammo_CustomParameter').items[4].items[3].items[0].items[0]
        for c in CALLS:
            if c.jet == vc.SAZABI_JET:
                durability[c.id] = hp * float(mult)
    stats = {c.id: gun_stats(template_sgo[template_of(c)]) for c in CALLS if c.brings == 'gun'}
    for lang, rel in zip(LANGS, TEXTS):
        text = s.text_rows(rel)
        text_templates = {c.id: text[tpl[template_of(c)]] for c in CALLS}
        port_text = {p.id: pw.text_row(p, lang) if p.id in ports
                     else pw.pending_text_row(text[port_tpl[p.id]], p, lang) for p in put_ports}
        puts = sorted([(plan.at[c.id], _text_row(text_templates[c.id], c, lang, durability[c.id], stats.get(c.id)))
                       for c in order] + [(plan.at[p.id], port_text[p.id]) for p in put_ports], key=lambda x: x[0])
        for at, row in puts:
            _put(text, at, row)
        out[rel] = dsgo.compact(s.texts[rel])
    verify(game_root, out, plan)
    return out


def verify(game_root: str, out: dict[str, bytes], plan: Plan) -> None:
    """Before anything is written: every row not ours unchanged at its index, each of ours where plan_rows put
    it, the table grown by exactly the rows appended, the texts as long as the table."""
    before = load_shared(game_root)
    ids = before.ids
    after = row_ids(out[TABLE])
    if len(after) != len(ids) + len(plan.appended):
        raise ValueError(f'the table grew by {len(after) - len(ids)} rows, {len(plan.appended)} were appended')
    for cid, i in plan.at.items():
        if after[i] not in (cid, retired_id(cid)):
            raise ValueError(f'row {i} is {after[i]}, expected {cid}')
    ours = set(plan.at.values())
    for rel in SHARED:
        old = [dsgo.to_py(r) for r in (before.rows if rel == TABLE else before.text_rows(rel))]
        new = [dsgo.to_py(r) for r in _rows(dsgo.parse(out[rel]), rel)]
        if len(new) != len(after):
            raise ValueError(f'{rel}: {len(new)} rows, the table has {len(after)}')
        for i, row in enumerate(old):
            if i not in ours and new[i] != row:
                raise ValueError(f'{rel}: row {i} is not ours and changed')


def retire(game_root: str, delete_rows: bool) -> tuple[dict[str, bytes], list[str]]:
    """The shared table + texts with every row of ours a placeholder, or (delete_rows) deleted when it is in the
    run of ours that ends the table. Returns (files, the ids whose rows are deleted)."""
    s = load_shared(game_root)
    ids = s.ids
    plan = plan_rows(ids)
    present = {c: i for c, i in plan.at.items() if i < len(ids)}
    cut = tail_start(ids) if delete_rows else len(ids)
    deleted = sorted((c for c, i in present.items() if i >= cut), key=lambda c: present[c])
    by_id = {c.id: c for c in CALLS}
    tpl = {c: _template_index(ids, template_of(by_id[c]) if c in by_id else pw.BY_ID[c].template) for c in present}

    def retired_row(cid: str, template: Node, lang: str | None) -> Node:
        if cid in by_id:
            c = by_id[cid]
            return _retired_table_row(template, c) if lang is None else _retired_text_row(template, c, lang)
        p = pw.BY_ID[cid]
        return pw.retired_table_row(template, p) if lang is None else pw.retired_text_row(template, p, lang)

    rows = s.rows
    for cid, i in present.items():
        if i < cut:
            rows[i] = retired_row(cid, rows[tpl[cid]], None)
    del rows[cut:]
    out = {TABLE: dsgo.compact(s.table)}
    for lang, rel in zip(LANGS, TEXTS):
        text = s.text_rows(rel)
        for cid, i in present.items():
            if i < cut:
                text[i] = retired_row(cid, text[tpl[cid]], lang)
        del text[cut:]
        out[rel] = dsgo.compact(s.texts[rel])
    after = row_ids(out[TABLE])
    if after[:cut] != [x if slot_of(x) is None else retired_id(slot_of(x)) for x in ids[:cut]]:
        raise AssertionError('retire moved a row')
    return out, deleted


def retired_id(row: str) -> str:
    """The placeholder id of a call's or an EDF5 weapon's row."""
    return pw.retired_id(row) if row in pw.BY_ID else calls.retired_id(row)


# ---------------------------------------------------------------- game dir: transaction, manifest


def _manifest_path(game_root: str) -> str:
    return _mods(game_root, MANIFEST)


def load_manifest(game_root: str) -> dict:
    path = _manifest_path(game_root)
    manifest = {'created': [], 'replaced': [], 'written': {}, 'rows': {}}
    if os.path.isfile(path):
        with open(path, encoding='utf-8') as f:
            manifest.update(json.load(f))
    return manifest


def _manifest_bytes(manifest: dict) -> bytes:
    """The manifest as commit writes it: it is one of the transaction's files, so what it records (the first
    backups above all) lands with the files it describes, or is rolled back with them."""
    return json.dumps(manifest, indent=1).encode('utf-8')


def recover(game_root: str) -> bool:
    """Rolls back a run that died between its first and last write (its journal is still there): every file it
    listed back as it was before that run. True when there was one."""
    journal = _mods(game_root, JOURNAL)
    if not os.path.isfile(journal):
        return False
    with open(journal, encoding='utf-8') as f:
        existed: dict[str, bool] = json.load(f)
    for rel, had in existed.items():
        path = _mods(game_root, rel)
        if had:
            with open(_mods(game_root, BACKUP, 'txn', rel), 'rb') as f:
                modfiles.atomic_write(path, f.read())
        elif os.path.isfile(path):
            os.remove(path)
    os.remove(journal)
    shutil.rmtree(_mods(game_root, BACKUP, 'txn'), ignore_errors=True)
    return True


def commit(game_root: str, changes: dict[str, bytes | None]) -> None:
    """Writes every file in `changes` (None: delete it) or none: see the module doc."""
    txn = _mods(game_root, BACKUP, 'txn')
    shutil.rmtree(txn, ignore_errors=True)
    existed: dict[str, bool] = {}
    for rel in changes:
        path = _mods(game_root, rel)
        existed[rel] = os.path.isfile(path)
        if existed[rel]:
            os.makedirs(os.path.dirname(_mods(game_root, BACKUP, 'txn', rel)), exist_ok=True)
            shutil.copy2(path, _mods(game_root, BACKUP, 'txn', rel))
    modfiles.atomic_write(_mods(game_root, JOURNAL), json.dumps(existed, indent=1).encode('utf-8'))
    try:
        for rel, data in changes.items():
            path = _mods(game_root, rel)
            if data is not None:
                modfiles.atomic_write(path, data)
            elif os.path.isfile(path):
                os.remove(path)
    except BaseException:
        recover(game_root)
        raise
    os.remove(_mods(game_root, JOURNAL))
    shutil.rmtree(txn, ignore_errors=True)


def _first_backup(game_root: str, manifest: dict, rels: list[str]) -> None:
    """Keeps, once, each file as it was before our first write to it (repair restores these). Only `manifest`
    in memory records it: the caller commits the manifest with the files, so a run that is rolled back leaves no
    record (a stale copy here is overwritten by the next first backup)."""
    for rel in rels:
        if rel in manifest['created'] or rel in manifest['replaced']:
            continue
        path = _mods(game_root, rel)
        if os.path.isfile(path):
            bak = _mods(game_root, BACKUP, rel)
            os.makedirs(os.path.dirname(bak), exist_ok=True)
            shutil.copy2(path, bak)
            manifest['replaced'].append(rel)
        else:
            manifest['created'].append(rel)


def changed_since(game_root: str, manifest: dict, rels: list[str]) -> list[str]:
    """The files among `rels` that are not what we last wrote into them (another tool changed them since)."""
    return [rel for rel in rels if rel in manifest['written']
            and modfiles.sha256_file(_mods(game_root, rel)) not in (None, manifest['written'][rel])]


def build(game_root: str, outdir: str) -> dict[str, bytes]:
    if os.path.normcase(os.path.abspath(outdir)).startswith(os.path.normcase(os.path.abspath(game_root))):
        raise SystemExit('build writes outside the game dir; use install for that')
    files = stack(game_root)
    for rel, data in files.items():
        modfiles.atomic_write(os.path.join(outdir, *rel.split('/')), data)
    return files


def install(game_root: str, files: dict[str, bytes] | None = None) -> dict[str, str]:
    """Writes `files` (stack, made now when None) in one transaction; returns {path under Mods: sha256}."""
    if modfiles.game_running():
        raise SystemExit(f'{PROCESS} is running: close the game first')
    if recover(game_root):
        print('rolled back the weapon table files of an earlier run that did not finish')
    files = stack(game_root) if files is None else files
    needs = {rel for c in CALLS if c.vehicle for rel in vehicle_needs(c, files[sgo_file(c)])}
    missing = [rel for rel in sorted(needs) if not os.path.isfile(_mods(game_root, rel))]
    if missing:
        raise SystemExit(f'{", ".join(missing)} not installed: run python tools/make_jets.py first')
    manifest = load_manifest(game_root)
    for rel in changed_since(game_root, manifest, list(files)):
        note = 'its other rows are kept' if rel in SHARED else 'overwritten'
        print(f'note: {rel} was changed by another tool since our last install ({note})')
    old_rows = dict(manifest['rows'])
    _first_backup(game_root, manifest, list(files))
    ids = row_ids(files[TABLE])
    manifest['written'] = {rel: modfiles.sha256(data) for rel, data in files.items()}
    manifest['rows'] = {slot_of(x): i for i, x in enumerate(ids) if slot_of(x)}   # placeholders too, as uninstall
    commit(game_root, {**files, MANIFEST: _manifest_bytes(manifest)})
    moved = {c: (old_rows[c], manifest['rows'].get(c)) for c in old_rows if old_rows[c] != manifest['rows'].get(c)}
    for c, (was, now) in moved.items():
        print(f'WARNING: {c} was row {was}, now {now}: another tool rewrote the weapon table without it; a save '
              f'that had it equipped or owned refers to row {was}')
    led = ledger.Ledger(game_root)
    before = set(led.owned_by(OWNER))
    for rel in sorted(needs):
        led.need(OWNER, rel)
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in needs}))
    return dict(manifest['written'])


def uninstall(game_root: str, delete_rows: bool = False, unequipped: bool = False) -> None:
    """Our rows become placeholders (or, delete_rows, the ones ending the table are deleted), our SGOs go."""
    if modfiles.game_running():
        raise SystemExit(f'{PROCESS} is running: close the game first')
    if recover(game_root):
        print('rolled back the weapon table files of an earlier run that did not finish')
    manifest = load_manifest(game_root)
    shared, deleted = retire(game_root, delete_rows)
    if deleted and not unequipped:
        raise SystemExit(f'deleting the rows of {", ".join(deleted)}: unequip these weapons in every save first (a '
                         'save with one equipped crashes the game once its row is gone), then rerun with --unequipped')
    changes: dict[str, bytes | None] = {}
    for rel, data in shared.items():
        if rel in manifest['created'] and data == stock(game_root, rel):
            changes[rel] = None   # we made it and nobody else's rows are left in it
        elif data != base(game_root, rel):
            changes[rel] = data
    for rel in our_sgos():
        path = _mods(game_root, rel)
        bak = _mods(game_root, BACKUP, rel)
        if rel in manifest['replaced'] and os.path.isfile(bak):
            with open(bak, 'rb') as f:
                changes[rel] = f.read()
        elif os.path.isfile(path):
            if rel in changed_since(game_root, manifest, [rel]):
                print(f'kept {rel}: another tool changed it since our install')
                continue
            changes[rel] = None
    _first_backup(game_root, manifest, list(shared))
    ids = row_ids(shared[TABLE])   # the table as this commit leaves it (deleted only when it is stock again)
    left = [x for x in ids if slot_of(x)]
    if left:   # placeholders stay ours: a later install takes their rows back, repair can still restore
        manifest['written'] = {rel: modfiles.sha256(d) for rel, d in changes.items() if d is not None}
        manifest['rows'] = {slot_of(x): ids.index(x) for x in left}
    commit(game_root, {**changes, MANIFEST: _manifest_bytes(manifest) if left else None})
    for rel, data in changes.items():
        print(f'{"removed" if data is None else "wrote"} {rel}')
    ledger.Ledger(game_root).release(OWNER)
    if left:
        print(f'{len(left)} rows are placeholders now (EDF6VC_RETIRED_*, stock weapons), keeping their row numbers')
        return
    shutil.rmtree(_mods(game_root, BACKUP), ignore_errors=True)


def repair(game_root: str) -> list[str]:
    """The shared table and texts back to before our first install (the copies install kept), our SGOs gone;
    returns what it changed. Another tool's later edits to those files are lost (install reports them)."""
    if modfiles.game_running():
        raise SystemExit(f'{PROCESS} is running: close the game first')
    recover(game_root)
    manifest = load_manifest(game_root)
    if not os.path.isfile(_manifest_path(game_root)):
        raise SystemExit('EDF6VehicleCrew never installed its call weapons here: nothing to restore')
    changes: dict[str, bytes | None] = {}
    for rel in SHARED + our_sgos():
        bak = _mods(game_root, BACKUP, rel)
        if rel in manifest['replaced'] and os.path.isfile(bak):
            with open(bak, 'rb') as f:
                changes[rel] = f.read()
        elif rel in manifest['created'] or (rel not in SHARED and os.path.isfile(_mods(game_root, rel))):
            changes[rel] = None
    commit(game_root, {**changes, MANIFEST: None})
    shutil.rmtree(_mods(game_root, BACKUP), ignore_errors=True)
    ledger.Ledger(game_root).release(OWNER)
    return sorted(changes)


def check(game_root: str) -> bool:
    """Prints the state of the installed tables; True when every call is in, at the row it was installed at,
    and everything lines up."""
    ok = True
    if os.path.isfile(_mods(game_root, JOURNAL)):
        print('an earlier run did not finish: the next install or uninstall rolls it back first')
        ok = False
    table = dsgo.parse(base(game_root, TABLE))
    ids = [r.items[0] for r in _rows(table, TABLE)]
    print(f'{TABLE}: {len(ids)} rows ({"Mods" if os.path.isfile(_mods(game_root, TABLE)) else "stock"})')
    for rel in TEXTS:
        n = len(_rows(dsgo.parse(base(game_root, rel)), rel))
        aligned = n == len(ids)
        ok &= aligned
        print(f'{rel}: {n} rows {"aligned" if aligned else "NOT ALIGNED with the table (python tools/call_weapons.py repair)"}')
    try:
        plan = plan_rows(ids)
    except ValueError as e:
        print(f'  {e}')
        return False
    rows = load_manifest(game_root)['rows']
    for c in CALLS:
        i = plan.at[c.id]
        state = 'missing' if i >= len(ids) else 'placeholder' if ids[i] != c.id else 'in'
        moved = f' (installed at {rows[c.id]})' if c.id in rows and rows[c.id] != i and state != 'missing' else ''
        print(f'  {i if state != "missing" else "-":>5} {c.id}: {state}{moved}')
        ok &= state == 'in' and not moved
        if state == 'in' and not os.path.isfile(_mods(game_root, sgo_file(c))):
            print(f'        {sgo_file(c)} missing')
            ok = False
        request_path = _mods(game_root, sgo_file(c))
        request = None
        if state == 'in' and c.vehicle and os.path.isfile(request_path):
            with open(request_path, 'rb') as f:
                request = f.read()
        if state == 'in' and c.vehicle and any(not os.path.isfile(_mods(game_root, rel)) for rel in vehicle_needs(c, request)):
            print(f'        {vehicle_file(c)} missing (python tools/make_jets.py)')
            ok = False
    return check_ports(game_root, ids, rows) and ok


def check_ports(game_root: str, ids: list[str], rows: dict[str, int]) -> bool:
    """The EDF5 weapons' part of check: every one has its row (a placeholder while EDF5 is missing is fine, it is
    counted), at the row it was installed at, and a built one its SGO."""
    ok = True
    states = {'in': 0, 'placeholder': 0, 'missing': 0}
    at = {pw.slot_of(x): i for i, x in enumerate(ids) if pw.slot_of(x)}
    for p in pw.PORTS:
        i = at.get(p.id)
        state = 'missing' if i is None else 'placeholder' if ids[i] != p.id else 'in'
        states[state] += 1
        if state == 'missing':
            ok = False
            continue
        if p.id in rows and rows[p.id] != i:
            print(f'  {i:>5} {p.id}: {state} (installed at {rows[p.id]})')
            ok = False
        if state == 'in' and not os.path.isfile(_mods(game_root, pw.sgo_file(p))):
            print(f'  {i:>5} {p.id}: {pw.sgo_file(p)} missing')
            ok = False
    print(f'  ported weapons (EDF5, EDF4.1): {states["in"]} in, {states["placeholder"]} placeholders (waiting for their '
          f'game, or uninstalled), '
          f'{states["missing"]} missing (of {len(pw.PORTS)})')
    return ok

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('action', choices=('build', 'install', 'uninstall', 'repair', 'check'))
    ap.add_argument('outdir', nargs='?')
    ap.add_argument('--game', default=vc.DEFAULT_GAME)
    ap.add_argument('--delete-rows', action='store_true', help='delete the rows that end the table instead of '
                    'leaving placeholders')
    ap.add_argument('--unequipped', action='store_true', help='these weapons are unequipped in every save')
    a = ap.parse_args()
    try:
        if a.action == 'build':
            if not a.outdir:
                ap.error('build needs OUTDIR')
            for rel in build(a.game, a.outdir):
                print(f'wrote {rel}')
        elif a.action == 'install':
            for rel, sha in install(a.game).items():
                print(f'{sha}  {rel}')
        elif a.action == 'uninstall':
            uninstall(a.game, a.delete_rows, a.unequipped)
        elif a.action == 'repair':
            for rel in repair(a.game):
                print(f'restored {rel}')
        else:
            sys.exit(0 if check(a.game) else 1)
    except Misaligned as e:
        raise SystemExit(str(e)) from None


if __name__ == '__main__':
    main()
