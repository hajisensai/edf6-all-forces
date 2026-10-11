"""Stores for the stock vehicles (ini StockVehicleStores, or the older StockHeliStores; on by default since 2026-10-07;
src/payload.cpp, src/stores.cpp, docs/stock-payload-re.md §4). The user, 2026-10-07: "给载具应有的多种挂载增加多种挂载。
例如原版坦克、aa车、直升机等" "应该有的都得有，比如导弹车"; 2026-10-09: "我要各种载具要有应有的挂载（符合设定的，例如坦克
应该有ap和he，甚至炮射导弹如应有的话）". What each stock vehicle should carry besides its own (LOADOUTS, by CATEGORIES; the
inventory of every vehicle and why each gets what it gets: docs/feedback-2026-10-09-loadcamp.md):

  main battle tanks (Blacker 505, Varius 601)      the round the stock gun is not (APFSDS beside a howitzer, HE beside
                                                   a smooth-bore) and a gun-launched LAHAT, from the main gun
  the Grape 401 (an infantry fighting vehicle)     the belt its stock cannon is not (AP or HE) and AGM-114s
  Epsilon railgun 403                              AGM-114s from the railgun (a railgun fires slugs only: no HE round;
                                                   its two gunners keep their machine guns)
  Titan 404                                        AGM-114s on the driver's second control, beside the front gatling
  Naegling missile launcher 402                    AIM-120 for air defence, AGM-65 for armour, a Hydra 70 pod
  Kepler / Volus flak 603                          HE proximity rounds, AIM-9X and AIM-120 surface-to-air missiles
  Freed bikes 503 / 613                            a Hydra 70 pod
  N9 Eros 506 / Heron 602 / Nereid 409             a Hydra 70 pod, AGM-114s and AIM-9X beside the stock missile

The stores are pylib/vcobjects.py STORES: the jets' missiles and rockets (the files tools/make_jets.py writes, recorded
here as needed) and the rounds no jet carries (the gun rounds, vcobjects.Shell: the stock vehicle round of that kind
and tier renamed; the LAHAT), which this tool writes itself (Mods/WEAPON/EDF6VC_<KIND>_<rounds>.SGO).
The old invented coaxial gun is removed: these models have no independent machine-gun mount.
The original 403/Titan machine guns stay on their existing stock mounts. Each hangs on the bone and seat of the stock holder it fires beside (Mount.like): the switch
(PlayerJetSwitchKey R, pad LB) goes round that holder's control's stock weapon and the stores, and that control fires
the one picked (src/payload.cpp). Into <game>/Mods:

  Mods/OBJECT/EDF6VC_<VEHICLE>_STORES.SGO   the stock vehicle with a weapon holder more for each store, after its own
                                            (src/stores.cpp builds them: the stock builds stop at their own)
  Mods/WEAPON/<REQUEST>.SGO                 each stock request (Root.cpk WEAPON) that brings one of them: the same file
                                            with that vehicle, its weapon list the stores after the stock ones, and the
                                            stores among the files it preloads

Only these copies change: the stock vehicles keep their files (a mission's own vehicles, another mod's requests that
name them are as they were), and the weapon table is not touched (the requests keep their names and rows). A request
another mod already put into Mods/WEAPON is left as it is (said); this tool never writes over a file it did not write.
EDF6AutoTurret's flak and Bohr requests (autoturret/tools/build.py) are its own: the installer hands them here first
(build's `overlay`) and writes them back through EDF6AutoTurret's manifest with the stores added, this tool writing only
the vehicle they bring. Without the plugin the stock builds make their own holders and no more: a store's holder has no
weapon and nothing fires it; the installer's uninstall takes them back either way (installer.uninstall_stock_stores). Everyone in an
online room needs the same files: a machine without them has another weapon list for the same request.

Built in memory first, written atomically and recorded in the ledger as this tool's (pylib/ledger.py), the store
weapons recorded as needed (make_jets writes them); --remove releases them, the stock requests coming back.

  python tools/make_stock_stores.py [game dir]               write / refresh
  python tools/make_stock_stores.py [game dir] --out DIR     build and check only, the files written under DIR
  python tools/make_stock_stores.py [game dir] --remove      release them
"""
from __future__ import annotations

import copy
import os
from pathlib import Path
import re
import sys
from dataclasses import dataclass
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import ledger  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'stockstores'   # pylib/ledger.py
INI_KEY = 'StockVehicleStores'
INI_KEYS = (INI_KEY, 'StockHeliStores')   # the older key (the helicopters alone, before 2026-10-07) still turns it on
COAX_MG = 'EDF6VC_COAX_MG.SGO'


@dataclass(frozen=True)
class Mount:
    """A store on a stock vehicle: its weapon file (Mods/WEAPON, an EDF6VC_ one) and the stock holder it hangs beside
    (its bone and seat: it fires from that muzzle, on that holder's control). `other`: a gun round's counterpart, carried
    instead when the stock weapon beside is already a round of `weapon`'s kind (pick): one holder, one of the pair.
    The user, 2026-10-10: 「这个he和原本的弹药重复了吧」: most stock tank guns are howitzers, already HE."""
    weapon: str
    like: int
    other: str | None = None

    def options(self) -> tuple[str, ...]:
        return (self.weapon,) if self.other is None else (self.weapon, self.other)


def _store(kind: str, rounds: int, like: int) -> Mount:
    return Mount(vc.store_file(kind, rounds), like)


def _rounds(ap: str, he: str, rounds: int, like: int) -> Mount:
    """The gun round the stock gun beside is not: AP beside a bursting gun (and when it is not known), HE beside one
    that does not burst."""
    return Mount(vc.store_file(ap, rounds), like, vc.store_file(he, rounds))


# Rockets and missiles are recoilless. Keep the BodyRecoil variant, even at zero
# strength: the 505/601/Car callbacks require that variant and dereference its
# payload without a null check. A gun round (vcobjects.Shell) recoils as the stock
# gun it fires from: it takes that holder's own BodyRecoil pair from the request.
# The 403's named AimRecoil is exclusive to its existing gunner mounts (slot 48
# branches on the seat index): it is never copied (_params takes a pair of numbers
# only, anything else is NO_RECOIL).
NO_RECOIL = [0.0, 0.0]

# What each kind of stock vehicle carries (a tank's 20 rounds beside its stock 25-30, an autocannon's belt beside
# its stock 240-800, the flak's beside its stock 1000-1500). The category names the setting it follows:
# tests/test_stock_store_loadouts.py holds every vehicle of LOADOUTS to its category's kinds. A tank or the Grape
# gets the one round its stock gun is not (_rounds): 22 of the 28 tank requests bring a howitzer (an HE gun already,
# the HE store's template is one of them), 5 the A series' smooth-bore (AP), one the heat gun.
_MBT = (_rounds('AP', 'HE', 20, 0), _store('GLM', 4, 0))
_IFV = (_rounds('AC_AP', 'AC_HE', 150, 0), _store('AGM_L', 4, 0))
_FLAK = (_store('FLAK_HE', 500, 0), _store('AAM_S', 2, 0), _store('AAM_M', 4, 0))
_LAUNCHER = (_store('AAM_M', 4, 0), _store('AGM', 2, 0), _store('RKT', 19, 0))
_BIKE = (_store('RKT', 19, 0),)
_HELI = (_store('RKT', 19, 2), _store('AGM_L', 4, 2), _store('AAM_S', 2, 2))   # beside the 506's missile (holder 2)
CATEGORIES: dict[str, str] = {
    'V506_HELI': 'heli', 'V506_HELI_EDF6BENEFITS': 'heli', 'V602_HELI': 'heli', 'VEHICLE409_HELI': 'heli',
    'V505_TANK': 'mbt', 'V505_TANK_EDF4': 'mbt', 'V505_TANK_EDF5': 'mbt', 'V505_TANK_EDF6BENEFITS': 'mbt',
    'V505_TANK_MPACK2': 'mbt', 'V601_TANK': 'mbt',
    'VEHICLE401_STRIKER': 'ifv', 'VEHICLE401_STRIKER_MPACK2': 'ifv',
    'VEHICLE403_TANK': 'railgun', 'VEHICLE404_BIGTANK': 'superheavy',
    'VEHICLE402_ROCKET': 'launcher', 'V402_ROCKET_EDF6BENEFITS': 'launcher',
    'V603_FLAK': 'flak',
    'V503_BIKE': 'bike', 'V503_BIKE_EDF6BENEFITS': 'bike', 'V503_BIKE_OMEGAZ': 'bike', 'V613_BIKE': 'bike',
}
_BY_CATEGORY: dict[str, tuple[Mount, ...]] = {
    'mbt': _MBT, 'ifv': _IFV, 'flak': _FLAK, 'launcher': _LAUNCHER, 'bike': _BIKE, 'heli': _HELI,
    'railgun': (_store('AGM_L', 4, 0),),
    'superheavy': (_store('AGM_L', 4, 3),),   # the driver's front gatling (holder 3): his second control
}
LOADOUTS: dict[str, tuple[Mount, ...]] = {stem: _BY_CATEGORY[c] for stem, c in CATEGORIES.items()}
LOADOUTS['VEHICLE409_HELI'] = (_store('RKT', 19, 1), _store('AGM_L', 4, 1), _store('AAM_S', 2, 1))   # its missile: holder 1
# What each category must carry (tests/test_stock_store_loadouts.py): store kinds, or roles ('role:<role>').
REQUIRED: dict[str, tuple[str, ...]] = {
    'mbt': ('AP', 'HE', 'GLM'),                          # AP or HE (whichever the stock gun is not), the gun-launched missile
    'ifv': ('AC_AP', 'AC_HE', 'role:ground'),            # the autocannon's other belt and an anti-tank missile
    'flak': ('FLAK_HE', 'role:air'),                     # HE proximity rounds, surface-to-air missiles
    'launcher': ('role:air', 'role:ground', 'role:rocket'),
    'heli': ('role:rocket', 'role:ground', 'role:air'),
    'bike': ('role:rocket',),
    'railgun': ('role:ground',),
    'superheavy': ('role:ground',),
}
# Every other vehicle a stock request brings, and why it gets no stores (tests/test_stock_store_loadouts.py, with the
# game: every vehicle is in LOADOUTS or here).
NOT_LOADED: dict[str, str] = {
    **{s: '机甲（Vehicle504_begaruta）：武器固定在双手，每只手一个键；插件不为该类补造挂点' for s in (
        'V504_BEGARUTA', 'V504_BEGARUTA_BLUE', 'V504_BEGARUTA_GOLD', 'V504_BEGARUTA_PINK', 'V504_BEGARUTA_RED',
        'V608_OLDROBOT')},
    **{s: '救护车（Vehicle507_Rescuetank）：没有武器' for s in ('V507_RESCUETANK', 'V507_RESCUETANK_MPACK2',
                                                       'V507_RESCUETANK_SIAWASE')},
    'V510_MASER': 'EMC（Vehicle510_Maser）：唯一武器是原子光线炮，插件不为该类补造挂点',
    'V512_KEITRUCK_BGP': '轻卡车：没有武器',
    **{s: '巨型机器人（Vehicle501_FortressRobo）：武器固定在双臂' for s in (
        'V515_RETROBALAM', 'V515_RETROBALAM_GRAY', 'V515_RETROBALAM_GREEN', 'V605_BARGA_CANNON')},
    'V607_ROBOTRUCK': '机器人卡车（Vehicle607_RoboTruck）：火焰喷射器与导弹在双臂，插件不为该类补造挂点',
    **{s: '机甲 Nix（Vehicle612_nix）：武器固定在双臂' for s in ('V612_NIX', 'V612_NIX_BLACK', 'V612_NIX_RED')},
    'V614_PROTEUS_MK2_CALL': 'Proteus（VehicleBigBegaruta）：自带武器由 make_proteus / proteus.cpp 管理',
    'VEHICLE410_HELI': '410 直升机（VehicleHelicopter410）：插件不为该类补造挂点（stores.cpp kBuilds 没有它）',
    'VEHICLE502_GROUNDROBO': '深渊爬行者（Vehicle502_GroundRobo）：三件武器各有一个键，插件不为该类补造挂点',
}
# The vehicle classes src/stores.cpp builds the extra holders of (its kBuilds, and the 506's loop): a stem of another
# class would get holders with no weapon.
BUILT_CLASSES = ('Vehicle402_Rocket', 'Vehicle403_Tank', 'Vehicle404_Tank', 'Vehicle503_Bike', 'Vehicle505_Tank',
                 'Vehicle601_Tank', 'Vehicle603_Flak', 'VehicleHelicopter409', 'Vehicle_Car', 'Vehicle506_Helicopter')


def derived_name(stem: str) -> str:
    return f'EDF6VC_{stem.upper()}_STORES.SGO'


_DERIVED = re.compile(r'EDF6VC_(.+)_STORES\.SGO$', re.I)


def _path(folder: str, name: str) -> str:
    return f'app:/{folder}/{name.lower()}'


# The kind of round ('ap' / 'he') a stock weapon fires, by its path in a weapon list; None: not known.
RoundKind = Callable[[str], 'str | None']


def no_round_kind(path: str) -> str | None:
    return None


def stock_round_kind(game: vc.Game) -> RoundKind:
    """The kind of round the stock weapons of `game` fire: 'he' with a blast radius (AmmoExplosion), 'ap' without."""
    seen: dict[str, str | None] = {}

    def kind(path: str) -> str | None:
        name = path.split('/')[-1].upper()
        if name not in seen:
            try:
                d = dsgo.to_py(dsgo.parse(game.read('WEAPON', name)).root)
                blast = d.get('AmmoExplosion')
                seen[name] = None if not isinstance(blast, (int, float)) else 'he' if blast > 0 else 'ap'
            except (KeyError, ValueError, OSError):
                seen[name] = None
        return seen[name]
    return kind


def _shell_kind(weapon: str) -> str | None:
    got = vc.store_of(_path('weapon', weapon))
    w = vc.STORES[got[0]].weapon if got else None
    return w.kind if isinstance(w, vc.Shell) else None


def _stock_path(entry: object) -> str | None:
    if isinstance(entry, dsgo.Node):
        entry = dsgo.to_py(entry)
    return entry[0] if isinstance(entry, list) and entry and isinstance(entry[0], str) else None


def pick(m: Mount, beside: object, kind_of: RoundKind) -> str:
    """The weapon file mount `m` carries beside stock weapon list entry `beside`: its `other` when that stock weapon
    already fires `weapon`'s kind of round."""
    path = _stock_path(beside)
    if m.other is not None and path is not None and kind_of(path) == _shell_kind(m.weapon):
        return m.other
    return m.weapon


def store_files() -> list[str]:
    """Every store weapon the loadouts use."""
    return sorted({w for load in LOADOUTS.values() for m in load for w in m.options() if w != COAX_MG})


def jet_store_files() -> list[str]:
    """The loadouts' store weapons make_jets writes (the jets carry them too): recorded here as needed."""
    return [f for f in store_files() if f in vc.STORE_FILES]


def own_store_files() -> list[str]:
    """The loadouts' store weapons no jet carries (the gun rounds, the LAHAT): this tool writes them."""
    return [f for f in store_files() if f not in vc.STORE_FILES]


def is_shell(weapon: str) -> bool:
    """Whether store weapon file `weapon` is a gun round (vcobjects.Shell)."""
    got = vc.store_of(_path('weapon', weapon))
    return got is not None and isinstance(vc.STORES[got[0]].weapon, vc.Shell)


def _is_body_recoil(v: object) -> bool:
    return (isinstance(v, list) and len(v) == 2
            and all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v))


def _params(m: Mount, beside: object = None) -> list:
    """A store's per-weapon parameters in a weapon list: a missile's or rocket's NO_RECOIL; a gun round's the BodyRecoil
    pair of the stock weapon it fires beside (`beside`, as Python lists), NO_RECOIL when that is no such pair."""
    if m.weapon == COAX_MG:
        raise ValueError('旧同轴机枪没有独立模型挂点；不可重新挂到主炮口')
    if is_shell(m.weapon) and _is_body_recoil(beside):
        return [float(x) for x in beside]
    return copy.deepcopy(NO_RECOIL)


def _entry_params(entry: object) -> object:
    """The per-weapon parameters (item 1) of a weapon list entry, as Python lists; None for an empty holder."""
    if isinstance(entry, dsgo.Node):
        entry = dsgo.to_py(entry)
    return entry[1] if isinstance(entry, list) and len(entry) > 1 else None


def _node(v: object) -> object:
    if isinstance(v, (list, tuple)):
        return dsgo.Node([_node(x) for x in v])
    return float(v) if isinstance(v, int) and not isinstance(v, bool) else v


# --- the vehicle -------------------------------------------------------------------------------------------------

def _is_list(v: object) -> bool:
    """A weapon list (as Python lists): entries [path, ...], or a number for a holder left empty (the Heron YG10's
    missile is 0.0: the stock build gives that holder no weapon)."""
    def entry(w: object) -> bool:
        return isinstance(w, list) and bool(w) and isinstance(w[0], str)
    return (isinstance(v, list) and any(entry(w) for w in v)
            and all(entry(w) or isinstance(w, (int, float)) and not isinstance(w, bool) for w in v))


def _node_is_list(v: object) -> bool:
    return isinstance(v, dsgo.Node) and _is_list(dsgo.to_py(v))


def _rows_of(m: dict, stem: str) -> list:
    rows = m.get('vehicle_weapon_setting')
    if not isinstance(rows, list) or not rows:
        raise ValueError(f'{stem}: 没有挂点（vehicle_weapon_setting）')
    for mount in LOADOUTS[stem]:
        if not 0 <= mount.like < len(rows):
            raise ValueError(f'{stem}: 挂载 {mount.weapon} 挂在第 {mount.like} 个挂点旁，可它只有 {len(rows)} 个')
    return rows


def _derived_sgo(data: bytes, stem: str, kind_of: RoundKind) -> bytes:
    version, m = sgo.read(data)
    rows = _rows_of(m, stem)
    stock = len(rows)
    own = [x for x in m.get('vehicle_setup', []) if _is_list(x) and len(x) == stock]
    for mount in LOADOUTS[stem]:
        rows.append(copy.deepcopy(rows[mount.like]))
        for weapons in own[-1:]:
            weapons.append([_path('weapon', pick(mount, weapons[mount.like], kind_of)),
                            _params(mount, _entry_params(weapons[mount.like]))])
    return sgo.write(version, m)


def _derived_dsgo(data: bytes, stem: str, kind_of: RoundKind) -> bytes:
    doc = dsgo.parse(data)
    rows = doc.root.get('vehicle_weapon_setting')
    _rows_of({'vehicle_weapon_setting': list(rows.items)}, stem)
    stock = len(rows.items)
    try:
        setup = doc.root.get('vehicle_setup')
    except KeyError:
        setup = None
    own = [x for x in (setup.items if isinstance(setup, dsgo.Node) else []) if _node_is_list(x) and len(x.items) == stock]
    for mount in LOADOUTS[stem]:
        rows.items.append(copy.deepcopy(rows.items[mount.like]))
        for weapons in own[-1:]:
            weapons.items.append(dsgo.Node([_path('weapon', pick(mount, weapons.items[mount.like], kind_of)),
                                            _node(_params(mount, _entry_params(weapons.items[mount.like])))]))
    return dsgo.write(doc)


def _read_any(data: bytes) -> dict:
    return dsgo.to_py(dsgo.parse(data).root) if data[:4] == b'DSGO' else sgo.load(data=data)


def vehicle_class(game: vc.Game, stem: str) -> str:
    return str(_read_any(game.read('OBJECT', stem + '.SGO')).get('xgs_scene_object_class', ''))


def derived_vehicle(game: vc.Game, stem: str) -> bytes:
    data = game.read('OBJECT', stem + '.SGO')
    cls = vehicle_class(game, stem)
    if cls not in BUILT_CLASSES:
        raise ValueError(f'{stem}: 载具类 {cls} 的额外挂点插件不会造（src/stores.cpp kBuilds）')
    kind_of = stock_round_kind(game)
    made = _derived_dsgo(data, stem, kind_of) if data[:4] == b'DSGO' else _derived_sgo(data, stem, kind_of)
    import make_optics
    return make_optics.redirect(made)[0]


def stock_rows(game: vc.Game, stem: str) -> int:
    return len(_read_any(game.read('OBJECT', stem + '.SGO'))['vehicle_weapon_setting'])


# --- the requests ------------------------------------------------------------------------------------------------

_VEHICLE = re.compile(r'app:/object/([a-z0-9_]+)\.sgo$', re.I)


def _brought(doc: dsgo.Document) -> tuple[dsgo.Node, str] | None:
    """The request's vehicle entry (Ammo_CustomParameter's [transport, box, vehicle, setup, ...]) and the vehicle's stem,
    when it brings one of LOADOUTS."""
    try:
        cp = doc.root.get('Ammo_CustomParameter')
    except KeyError:
        return None
    if not isinstance(cp, dsgo.Node):
        return None
    for x in cp.items:
        if isinstance(x, dsgo.Node) and len(x.items) >= 4 and isinstance(x.items[2], str):
            m = _VEHICLE.match(x.items[2])
            if m and m.group(1).upper() in LOADOUTS:
                return x, m.group(1).upper()
    return None


def _brought_any(doc: dsgo.Document) -> str | None:
    """The file name (lower case) of the vehicle a request brings, whichever it is; None: it brings none."""
    try:
        cp = doc.root.get('Ammo_CustomParameter')
    except KeyError:
        return None
    for x in cp.items if isinstance(cp, dsgo.Node) else []:
        if isinstance(x, dsgo.Node) and len(x.items) >= 4 and isinstance(x.items[2], str) and _VEHICLE.match(x.items[2]):
            return x.items[2].split('/')[-1].lower()
    return None


def _request_list(entry: dsgo.Node, rows: int, where: str) -> dsgo.Node:
    """The vehicle setup's weapon list (index 2 on the ground vehicles, 3 on the helicopters): one entry a holder."""
    setup = entry.items[3]
    found = [x for x in (setup.items if isinstance(setup, dsgo.Node) else []) if _node_is_list(x)]
    if len(found) != 1 or len(found[0].items) != rows:
        raise ValueError(f'{where}: 找不到与 {rows} 个挂点对应的武器表')
    return found[0]


def request_sgo(data: bytes, name: str, rows: dict[str, int],
                kind_of: RoundKind = no_round_kind) -> tuple[bytes, str] | None:
    """The request with the stores (see the top) and its vehicle's stem; None: it brings no vehicle of LOADOUTS.
    `rows`: each stem's stock holders (stock_rows); `kind_of`: the round its stock weapons fire (pick)."""
    doc = dsgo.parse(data)
    found = _brought(doc)
    if found is None:
        return None
    entry, stem = found
    weapons = _request_list(entry, rows[stem], name)
    old_vehicle = entry.items[2]
    entry.items[2] = _path('object', derived_name(stem))
    carried = []
    for mount in LOADOUTS[stem]:
        carried.append(_path('weapon', pick(mount, weapons.items[mount.like], kind_of)))
        weapons.items.append(dsgo.Node([carried[-1], _node(_params(mount, _entry_params(weapons.items[mount.like])))]))
    res = doc.root.get('resource')
    items = [entry.items[2] if isinstance(r, str) and r.lower() == old_vehicle.lower() else r for r in res.items]
    res.items[:] = items + [p for p in carried if p not in items]
    return dsgo.write(doc), stem


def check(files: dict[str, bytes], rows: dict[str, int]) -> None:
    """Each vehicle has its stock holders and one more a store, each a copy of the holder it hangs beside; each request
    brings a vehicle written here (or already there) and lists as many weapons as it has holders, the stores last and
    preloaded; every store is an EDF6VC_ file (payload.cpp tells them by it)."""
    holders: dict[str, int] = {}
    for rel, data in files.items():
        m = _DERIVED.match(rel.split('/')[-1])
        if not rel.startswith('OBJECT/') or not m:
            continue
        stem = m.group(1).upper()
        got = _read_any(data)['vehicle_weapon_setting']
        assert len(got) == rows[stem] + len(LOADOUTS[stem]), (rel, len(got))
        for k, mount in enumerate(LOADOUTS[stem]):
            assert got[rows[stem] + k] == got[mount.like], (rel, k)
        holders[rel.split('/')[-1].lower()] = len(got)
    own = {f'WEAPON/{f}'.upper() for f in own_store_files()}
    for rel, data in files.items():
        if rel.upper() in own:
            kind, rounds = vc.store_of(_path('weapon', rel.split('/')[-1]))
            got = dsgo.to_py(dsgo.parse(data).root)
            assert got.get('AmmoCount') == float(rounds), (rel, got.get('AmmoCount'))
            continue
        if not rel.startswith('WEAPON/') or rel.upper().endswith(COAX_MG):
            continue
        d = dsgo.to_py(dsgo.parse(data).root)
        entry = next(x for x in d['Ammo_CustomParameter'] if isinstance(x, list) and len(x) >= 4 and isinstance(x[2], str)
                     and x[2].lower().startswith('app:/object/edf6vc_'))
        vehicle = entry[2].split('/')[-1]
        stem = _DERIVED.match(vehicle).group(1).upper()
        weapons = next(x for x in entry[3] if _is_list(x))
        assert len(weapons) == rows[stem] + len(LOADOUTS[stem]), (rel, len(weapons))
        assert vehicle in holders or not holders, (rel, vehicle)
        carried = [w[0] for w in weapons[rows[stem]:]]
        assert all(w in [_path('weapon', o) for o in m.options()] for m, w in zip(LOADOUTS[stem], carried)), rel
        for mount, w in zip(LOADOUTS[stem], weapons[rows[stem]:]):
            want = _params(mount, _entry_params(weapons[mount.like]))
            assert _is_body_recoil(w[1]) and w[1] == want, (rel, 'added stores require the BodyRecoil pair', w, want)
        assert entry[2] in d['resource'] and all(p in d['resource'] for p in carried), rel
    assert all(w.upper().startswith('EDF6VC_') for load in LOADOUTS.values() for m in load for w in m.options())
    assert all(COAX_MG not in m.options() for load in LOADOUTS.values() for m in load), 'no independent coaxial-gun mount'


def requests(game: vc.Game) -> list[str]:
    """The stock request files that bring a vehicle of LOADOUTS (Root.cpk WEAPON)."""
    out = []
    for name in game.names('WEAPON'):
        if not name.upper().endswith('.SGO'):
            continue
        data = game.read('WEAPON', name)
        if data[:4] == b'DSGO' and _brought(dsgo.parse(data)) is not None:
            out.append(name.upper())
    return out


def build(root: str, overlay: dict[str, bytes] | None = None
          ) -> tuple[dict[str, bytes], list[str], dict[str, bytes]]:
    """({path under Mods: bytes} to write, the requests left alone because another mod put its own into Mods/WEAPON,
    {path: bytes} of `overlay`'s requests with the stores added). `overlay`: Mods files another tool of this installer
    writes after this one (EDF6AutoTurret's): its requests are built on from its bytes and handed back, not written here."""
    game = vc.Game(root)
    led = ledger.Ledger(root)
    overlay = {k.upper(): v for k, v in (overlay or {}).items()}
    out: dict[str, bytes] = {}
    handed: dict[str, bytes] = {}
    skipped: list[str] = []
    stems: set[str] = set()
    rows = {stem: stock_rows(game, stem) for stem in LOADOUTS}
    kind_of = stock_round_kind(game)
    for name in requests(game):
        rel = f'WEAPON/{name}'
        if rel.upper() in overlay:
            made = request_sgo(overlay[rel.upper()], name, rows, kind_of)
            if made is not None:
                handed[rel], stem = made
                stems.add(stem)
            continue
        if os.path.isfile(led.disk(rel)) and OWNER not in led.owners(rel):
            skipped.append(rel)
            continue
        made = request_sgo(game.read('WEAPON', name), name, rows, kind_of)
        if made is None:
            continue
        out[rel], stem = made
        stems.add(stem)
    for stem in sorted(stems):
        out[f'OBJECT/{derived_name(stem)}'] = derived_vehicle(game, stem)
    for f in own_store_files():   # the rounds no jet carries: this tool's (never over another's file)
        rel = f'WEAPON/{f}'
        if os.path.isfile(led.disk(rel)) and OWNER not in led.owners(rel):
            skipped.append(rel)
            continue
        out[rel] = vc.store_sgo(game, f)
    check({**out, **handed}, rows)
    return out, skipped, handed


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's; what it wrote before and does not now is released (the stock request
    comes back). The store weapons must be installed (make_jets): recorded as needed."""
    led = ledger.Ledger(root)
    for f in jet_store_files():
        led.need(OWNER, f'WEAPON/{f}')
    before = set(led.owned_by(OWNER))
    import make_optics
    game = None
    paths, dependencies = [], set()
    for rel, data in files.items():
        changed_object = rel.upper().startswith('OBJECT/') and rel.upper().endswith('.SGO') and led.changed(rel)
        if changed_object:
            data = Path(led.disk(rel)).read_bytes()  # retain the foreign edit and its original ledger fingerprint
        if rel.upper().startswith('OBJECT/') and rel.upper().endswith('.SGO'):
            # This owner's bytes are redirected before its one authoritative ledger write.
            data, needs = make_optics.redirect(data)
            if needs:
                if game is None:
                    game = vc.Game(root)
                data, needs = make_optics.range_vehicle(led, game, data, OWNER)
            dependencies.update(ledger.key(dep) for dep in needs)
        if not changed_object:
            paths.append(led.put(OWNER, rel, data))
    keep = {ledger.key(rel) for rel in files} | {ledger.key(f'WEAPON/{f}') for f in jet_store_files()} | dependencies
    obsolete = before - keep
    led.release(OWNER, sorted(obsolete - _held_outputs(root, led, list(obsolete))))
    return paths


def _still_brought(root: str, rels: list[str]) -> set[str]:
    """Of this tool's vehicles `rels`, those a request in Mods/WEAPON that is not this tool's still brings (the ones the
    installer handed back to EDF6AutoTurret): deleting them would leave those requests a vehicle that is not there."""
    led = ledger.Ledger(root)
    folder = os.path.join(root, 'Mods', 'WEAPON')
    wanted = {rel.split('/')[-1].lower(): rel for rel in rels if rel.upper().startswith('OBJECT/')}
    if not wanted or not os.path.isdir(folder):
        return set()
    out: set[str] = set()
    for name in os.listdir(folder):
        rel = f'WEAPON/{name}'
        if not name.upper().endswith('.SGO') or OWNER in led.owners(rel):
            continue
        try:
            with open(os.path.join(folder, name), 'rb') as f:
                data = f.read()
            found = _brought_any(dsgo.parse(data)) if data[:4] == b'DSGO' else None
        except (OSError, ValueError, KeyError, IndexError):
            continue
        if found in wanted:
            out.add(wanted[found])
    return out


def _held_outputs(root: str, led: ledger.Ledger, rels: list[str]) -> set[str]:
    hold = _still_brought(root, rels)
    import make_optics
    for rel in rels:
        if not rel.endswith('.SGO') or not rel.startswith('OBJECT/') or not os.path.isfile(led.disk(rel)):
            continue
        if rel in hold or led.changed(rel):
            raw = Path(led.disk(rel)).read_bytes()
            _, needs = make_optics.redirect(raw)
            if needs:
                hold.add(rel)
                hold.update(ledger.key(dep) for dep in needs)
    return hold


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept because someone else changed them since, or another tool's request
    still brings the vehicle). Its own were all written under the ledger, and the store weapons are only needed:
    neither is a writer's release (the jets' stay)."""
    led = ledger.Ledger(root)
    owned = list(led.owned_by(OWNER))
    hold = _held_outputs(root, led, owned)
    deleted, kept = led.release(OWNER, [r for r in owned if r not in hold])
    return deleted, kept + sorted(hold)


def wanted(ini_text: str) -> bool:
    """INI_KEYS in [VehicleCrew] (any case) of the ini's text (the player's own, else the shipped one): on when either
    is 1 (the plugin reads them the same way, src/plugin.cpp)."""
    section = ''
    on = False
    for line in ini_text.splitlines():
        s = line.strip()
        if s.startswith('[') and s.endswith(']'):
            section = s[1:-1].strip()
            continue
        m = re.match(r'([A-Za-z0-9_]+)\s*=\s*([^;]*)', s)
        if m and section.lower() == 'vehiclecrew' and m.group(1).lower() in (k.lower() for k in INI_KEYS):
            on = on or m.group(2).strip() not in ('', '0')
    return on


def main(argv: list[str]) -> int:
    import modfiles
    args = [a for a in argv if not a.startswith('--')]
    out = argv[argv.index('--out') + 1] if '--out' in argv else None
    if out in args:
        args.remove(out)
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        modfiles.refuse_while_running()
        deleted, kept = remove(root)
        for p in deleted:
            print('删除', p)
        for p in kept:
            print('保留（之后被别的工具改过，或别的工具的请求还在用它）', p)
        return 0
    files, skipped, _ = build(root)
    for rel in skipped:
        print('跳过（别的 mod 已经放了自己的）', rel)
    if out:
        for rel, data in files.items():
            path = os.path.join(out, *rel.split('/'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as f:
                f.write(data)
            print('生成', path)
        return 0
    modfiles.refuse_while_running()
    for path in install(root, files):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
