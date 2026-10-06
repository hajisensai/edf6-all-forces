"""Stores for the stock vehicles (ini StockVehicleStores, or the older StockHeliStores; off unless the player turns it on;
src/payload.cpp, src/stores.cpp, docs/stock-payload-re.md §4). The user, 2026-10-07: "给载具应有的多种挂载增加多种挂载。
例如原版坦克、aa车、直升机等" "应该有的都得有，比如导弹车". What each stock vehicle should carry besides its own (LOADOUTS):

  tanks (Blacker 505, Varius 601) and the Grape    a coaxial machine gun and gun-launched AGM-114s, from the main gun
  Epsilon railgun 403                              AGM-114s from the railgun (its two gunners keep their machine guns)
  Titan 404                                        AGM-114s on the driver's second control, beside the front gatling
  Naegling missile launcher 402                    AIM-120 for air defence, AGM-65 for armour, a Hydra 70 pod
  Kepler / Volus flak 603                          AIM-9X and AIM-120 surface-to-air missiles beside the guns
  Freed bikes 503 / 613                            a Hydra 70 pod
  N9 Eros 506 / Heron 602 / Nereid 409             a Hydra 70 pod, AGM-114s and AIM-9X beside the stock missile

The stores are the jets' (pylib/vcobjects.py STORES, the files tools/make_jets.py writes) and one file of this tool's:
EDF6VC_COAX_MG.SGO, the 403's machine gun under the plugin's name (src/stores.cpp IsLoadoutWeapon tells the stores by
their EDF6VC_ files). Each hangs on the bone and seat of the stock holder it fires beside (Mount.like): the switch
(PlayerJetSwitchKey R, pad LB) goes round that holder's control's stock weapon and the stores, and that control fires
the one picked (src/payload.cpp). Into <game>/Mods:

  Mods/OBJECT/EDF6VC_<VEHICLE>_STORES.SGO   the stock vehicle with a weapon holder more for each store, after its own
                                            (src/stores.cpp builds them: the stock builds stop at their own)
  Mods/WEAPON/<REQUEST>.SGO                 each stock request (Root.cpk WEAPON) that brings one of them: the same file
                                            with that vehicle, its weapon list the stores after the stock ones, and the
                                            stores among the files it preloads
  Mods/WEAPON/EDF6VC_COAX_MG.SGO            the coaxial machine gun

Only these copies change: the stock vehicles keep their files (a mission's own vehicles, another mod's requests that
name them are as they were), and the weapon table is not touched (the requests keep their names and rows). A request
another mod already put into Mods/WEAPON is left as it is (said); this tool never writes over a file it did not write.
EDF6AutoTurret's flak and Bohr requests (autoturret/tools/build.py) are its own: the installer hands them here first
(build's `overlay`) and writes them back through EDF6AutoTurret's manifest with the stores added, this tool writing only
the vehicle they bring. Without the plugin the stock builds make their own holders and no more: a store's holder has no
weapon and nothing fires it, so turn the option off (and run the installer) before removing the plugin. Everyone in an
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
import re
import sys
from dataclasses import dataclass

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
COAX_STOCK = 'V_403TANK_MACHINEGUN.SGO'


@dataclass(frozen=True)
class Mount:
    """A store on a stock vehicle: its weapon file (Mods/WEAPON, an EDF6VC_ one) and the stock holder it hangs beside
    (its bone and seat: it fires from that muzzle, on that holder's control)."""
    weapon: str
    like: int


def _store(kind: str, rounds: int, like: int) -> Mount:
    return Mount(vc.store_file(kind, rounds), like)


# The per-weapon parameters of a store's entry in the weapon list: the recoil (two numbers, or a named one) the stock
# lists give each weapon; a missile or rocket pod none, the machine gun the 403's own.
PARAMS: dict[str, list] = {COAX_MG: ['AimRecoil', [0.0, 0.0026000000070780516]]}
NO_RECOIL = [0.0, 0.0]

_TANK = (Mount(COAX_MG, 0), _store('AGM_L', 4, 0))
_HELI = (_store('RKT', 19, 2), _store('AGM_L', 4, 2), _store('AAM_S', 2, 2))   # beside the 506's missile (holder 2)
LOADOUTS: dict[str, tuple[Mount, ...]] = {
    'V506_HELI': _HELI,
    'V506_HELI_EDF6BENEFITS': _HELI,
    'V602_HELI': _HELI,
    'VEHICLE409_HELI': (_store('RKT', 19, 1), _store('AGM_L', 4, 1), _store('AAM_S', 2, 1)),   # its missile: holder 1
    'V505_TANK': _TANK,
    'V505_TANK_EDF4': _TANK,
    'V505_TANK_EDF5': _TANK,
    'V505_TANK_EDF6BENEFITS': _TANK,
    'V505_TANK_MPACK2': _TANK,
    'V601_TANK': _TANK,
    'VEHICLE401_STRIKER': _TANK,
    'VEHICLE401_STRIKER_MPACK2': _TANK,
    'VEHICLE403_TANK': (_store('AGM_L', 4, 0),),
    'VEHICLE404_BIGTANK': (_store('AGM_L', 4, 3),),   # the driver's front gatling (holder 3): his second control
    'VEHICLE402_ROCKET': (_store('AAM_M', 4, 0), _store('AGM', 2, 0), _store('RKT', 19, 0)),
    'V402_ROCKET_EDF6BENEFITS': (_store('AAM_M', 4, 0), _store('AGM', 2, 0), _store('RKT', 19, 0)),
    'V603_FLAK': (_store('AAM_S', 2, 0), _store('AAM_M', 4, 0)),
    'V503_BIKE': (_store('RKT', 19, 0),),
    'V503_BIKE_EDF6BENEFITS': (_store('RKT', 19, 0),),
    'V503_BIKE_OMEGAZ': (_store('RKT', 19, 0),),
    'V613_BIKE': (_store('RKT', 19, 0),),
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


def store_paths(stem: str) -> list[str]:
    return [_path('weapon', m.weapon) for m in LOADOUTS[stem]]


def store_files() -> list[str]:
    """The jets' store weapons the loadouts use (make_jets writes them)."""
    return sorted({m.weapon for load in LOADOUTS.values() for m in load if m.weapon != COAX_MG})


def _params(m: Mount) -> list:
    return copy.deepcopy(PARAMS.get(m.weapon, NO_RECOIL))


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


def _derived_sgo(data: bytes, stem: str) -> bytes:
    version, m = sgo.read(data)
    rows = _rows_of(m, stem)
    stock = len(rows)
    own = [x for x in m.get('vehicle_setup', []) if _is_list(x) and len(x) == stock]
    for mount in LOADOUTS[stem]:
        rows.append(copy.deepcopy(rows[mount.like]))
        for weapons in own[-1:]:
            weapons.append([_path('weapon', mount.weapon), _params(mount)])
    return sgo.write(version, m)


def _derived_dsgo(data: bytes, stem: str) -> bytes:
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
            weapons.items.append(dsgo.Node([_path('weapon', mount.weapon), _node(_params(mount))]))
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
    return _derived_dsgo(data, stem) if data[:4] == b'DSGO' else _derived_sgo(data, stem)


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


def request_sgo(data: bytes, name: str, rows: dict[str, int]) -> tuple[bytes, str] | None:
    """The request with the stores (see the top) and its vehicle's stem; None: it brings no vehicle of LOADOUTS.
    `rows`: each stem's stock holders (stock_rows)."""
    doc = dsgo.parse(data)
    found = _brought(doc)
    if found is None:
        return None
    entry, stem = found
    weapons = _request_list(entry, rows[stem], name)
    old_vehicle = entry.items[2]
    entry.items[2] = _path('object', derived_name(stem))
    for mount in LOADOUTS[stem]:
        weapons.items.append(dsgo.Node([_path('weapon', mount.weapon), _node(_params(mount))]))
    res = doc.root.get('resource')
    items = [entry.items[2] if isinstance(r, str) and r.lower() == old_vehicle.lower() else r for r in res.items]
    res.items[:] = items + [p for p in store_paths(stem) if p not in items]
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
    for rel, data in files.items():
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
        assert [w[0] for w in weapons[rows[stem]:]] == store_paths(stem), rel
        assert entry[2] in d['resource'] and all(p in d['resource'] for p in store_paths(stem)), rel
    assert all(m.weapon.upper().startswith('EDF6VC_') for load in LOADOUTS.values() for m in load)


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
    for name in requests(game):
        rel = f'WEAPON/{name}'
        if rel.upper() in overlay:
            made = request_sgo(overlay[rel.upper()], name, rows)
            if made is not None:
                handed[rel], stem = made
                stems.add(stem)
            continue
        if os.path.isfile(led.disk(rel)) and OWNER not in led.owners(rel):
            skipped.append(rel)
            continue
        made = request_sgo(game.read('WEAPON', name), name, rows)
        if made is None:
            continue
        out[rel], stem = made
        stems.add(stem)
    for stem in sorted(stems):
        out[f'OBJECT/{derived_name(stem)}'] = derived_vehicle(game, stem)
    if any(m.weapon == COAX_MG for stem in stems for m in LOADOUTS[stem]):
        out[f'WEAPON/{COAX_MG}'] = game.read('WEAPON', COAX_STOCK)
    check({**out, **handed}, rows)
    return out, skipped, handed


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's; what it wrote before and does not now is released (the stock request
    comes back). The store weapons must be installed (make_jets): recorded as needed."""
    led = ledger.Ledger(root)
    for f in store_files():
        led.need(OWNER, f'WEAPON/{f}')
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    keep = {ledger.key(rel) for rel in files} | {ledger.key(f'WEAPON/{f}') for f in store_files()}
    led.release(OWNER, sorted(before - keep))
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


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept because someone else changed them since, or another tool's request
    still brings the vehicle). Its own were all written under the ledger, and the store weapons are only needed:
    neither is a writer's release (the jets' stay)."""
    led = ledger.Ledger(root)
    owned = list(led.owned_by(OWNER))
    hold = _still_brought(root, owned)
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
