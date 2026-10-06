"""Stores for the stock helicopters (ini StockHeliStores, off unless the player turns it on; src/payload.cpp,
docs/stock-payload-re.md §4): the jets' Hydra 70 rocket pod and AGM-114 Hellfires (pylib/vcobjects.py STORES, the files
tools/make_jets.py writes) on the stock 506-class helicopters the player requests, the secondary button firing the one
picked and the jets' switch key (R, pad LB) going round them and the stock missile. Into <game>/Mods:

  Mods/OBJECT/EDF6VC_<VEHICLE>_STORES.SGO   the stock vehicle (LOADOUTS: V506_HELI, its EDF6 Benefits copy, V602_HELI)
                                            with a weapon holder more for each store, on its pilot seat, after the fuel
                                            tank: holders 4 and on, which the stock 506 never builds and the plugin's
                                            weapon build does (src/stores.cpp)
  Mods/WEAPON/<REQUEST>.SGO                 each stock request (Root.cpk WEAPON) that brings one of them: the same file
                                            with that vehicle, its weapon list the stores after the stock four, and the
                                            stores among the files it preloads

Only these copies change: the stock vehicles keep their files (a mission's own helicopters, another mod's requests that
name them are as they were), and the weapon table is not touched (the requests keep their names and rows). A request
another mod already put into Mods/WEAPON is left as it is (said); this tool never writes over a file it did not write.
Without the plugin the game builds the first four holders as always: the stock helicopter, no stores. Everyone in an
online room needs the same: a machine without these files has another weapon list for the same request.

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

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import ledger  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'stockstores'   # pylib/ledger.py
INI_KEY = 'StockHeliStores'
# The stock 506-class helicopters (Vehicle506_Helicopter: the plugin's weapon build makes every holder, src/stores.cpp)
# and what each carries besides its own four (guns L / R, missile, fuel): an attack helicopter's rocket pod and
# Hellfires, the order the switch goes round after the stock missile. The 409 is not here: its weapon build is three
# calls written out (0x64B3C0), no holder past them would get a weapon.
LOADOUTS: dict[str, tuple[tuple[str, int], ...]] = {
    'V506_HELI': (('RKT', 19), ('AGM_L', 4)),
    'V506_HELI_EDF6BENEFITS': (('RKT', 19), ('AGM_L', 4)),
    'V602_HELI': (('RKT', 19), ('AGM_L', 4)),
}
STOCK_WEAPONS = 4          # the 506's own list: gun L, gun R, missile (or none), fuel tank
FUEL_AT = vc.FUEL_AT       # 3: the fuel tank, built with or without the plugin
STORE_BONE, STORE_SEAT = 'body', 0


def derived_name(stem: str) -> str:
    return f'EDF6VC_{stem.upper()}_STORES.SGO'


def _path(folder: str, name: str) -> str:
    return f'app:/{folder}/{name.lower()}'


def store_paths(stem: str) -> list[str]:
    return [_path('weapon', vc.store_file(kind, n)) for kind, n in LOADOUTS[stem]]


def store_files() -> list[str]:
    return sorted({vc.store_file(kind, n) for load in LOADOUTS.values() for kind, n in load})


def _check_list(names: list[str], where: str) -> None:
    """The stock weapon list a store list goes after: four entries, the fuel tank fourth."""
    if len(names) != STOCK_WEAPONS or 'v_fuel01' not in names[FUEL_AT].lower():
        raise ValueError(f'{where}: 武器表不是原版 506 的四项（{names}）')


def _missile_first(weapons: list) -> list:
    """The weapon list with its missile (the third: the secondary's) first: a store takes its per-weapon parameters, as
    the jets' stores take the stock missile's (pylib/vcobjects.py jet_sgo), else the first weapon's that has them."""
    return [weapons[2], *weapons[:2], *weapons[3:]] if len(weapons) > 2 else list(weapons)


# --- the vehicle -------------------------------------------------------------------------------------------------

def _derived_sgo(data: bytes, stem: str) -> bytes:
    version, m = sgo.read(data)
    rows, setup = m['vehicle_weapon_setting'], m['vehicle_setup']
    weapons = setup[3]
    _check_list([w[0] if isinstance(w, list) and w and isinstance(w[0], str) else '' for w in weapons], stem)
    if len(rows) != STOCK_WEAPONS:
        raise ValueError(f'{stem}: {len(rows)} 个挂点，不是 4 个')
    params = copy.deepcopy(next((w[1] for w in _missile_first(weapons) if isinstance(w, list) and len(w) > 1), [0.0, 0.0]))
    for path in store_paths(stem):
        rows.append([STORE_BONE, STORE_SEAT])
        weapons.append([path, copy.deepcopy(params)])
    return sgo.write(version, m)


def _node(v: object) -> object:
    if isinstance(v, (list, tuple)):
        return dsgo.Node([_node(x) for x in v])
    return float(v) if isinstance(v, int) and not isinstance(v, bool) else v


def _derived_dsgo(data: bytes, stem: str) -> bytes:
    doc = dsgo.parse(data)
    rows, setup = doc.root.get('vehicle_weapon_setting'), doc.root.get('vehicle_setup')
    weapons = setup.items[3]
    _check_list([w.items[0] if isinstance(w, dsgo.Node) and w.items and isinstance(w.items[0], str) else '' for w in weapons.items],
                stem)
    if len(rows.items) != STOCK_WEAPONS:
        raise ValueError(f'{stem}: {len(rows.items)} 个挂点，不是 4 个')
    params = next((copy.deepcopy(w.items[1]) for w in _missile_first(weapons.items) if isinstance(w, dsgo.Node) and len(w.items) > 1),
                  _node([0.0, 0.0]))
    for path in store_paths(stem):
        rows.items.append(_node([STORE_BONE, STORE_SEAT]))
        weapons.items.append(dsgo.Node([path, copy.deepcopy(params)]))
    return dsgo.write(doc)


def derived_vehicle(game: vc.Game, stem: str) -> bytes:
    data = game.read('OBJECT', stem + '.SGO')
    return _derived_dsgo(data, stem) if data[:4] == b'DSGO' else _derived_sgo(data, stem)


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


def request_sgo(data: bytes, name: str) -> tuple[bytes, str] | None:
    """The request with the stores (see the top) and its vehicle's stem; None: it brings no vehicle of LOADOUTS."""
    doc = dsgo.parse(data)
    found = _brought(doc)
    if found is None:
        return None
    entry, stem = found
    weapons = entry.items[3].items[3]
    _check_list([w.items[0] if isinstance(w, dsgo.Node) and w.items and isinstance(w.items[0], str) else '' for w in weapons.items],
                name)
    old_vehicle = entry.items[2]
    entry.items[2] = _path('object', derived_name(stem))
    params = next((copy.deepcopy(w.items[1]) for w in _missile_first(weapons.items) if isinstance(w, dsgo.Node) and len(w.items) > 1),
                  _node([0.0, 0.0]))
    for path in store_paths(stem):
        weapons.items.append(dsgo.Node([path, copy.deepcopy(params)]))
    res = doc.root.get('resource')
    items = [entry.items[2] if isinstance(r, str) and r.lower() == old_vehicle.lower() else r for r in res.items]
    res.items[:] = items + [p for p in store_paths(stem) if p not in items]
    return dsgo.write(doc), stem


def check(files: dict[str, bytes]) -> None:
    """Each vehicle's holders and weapon list agree (the plugin builds one weapon an entry, src/stores.cpp), the fuel tank
    is fourth, the stores follow it; each request brings a vehicle written here and carries the same list."""
    holders: dict[str, int] = {}
    for rel, data in files.items():
        if not rel.startswith('OBJECT/'):
            continue
        d = sgo.load(data=data)
        rows, weapons = d['vehicle_weapon_setting'], d['vehicle_setup'][3]
        assert len(rows) == len(weapons) > STOCK_WEAPONS, rel
        assert 'v_fuel01' in str(weapons[FUEL_AT][0]).lower(), rel
        holders[rel.split('/')[-1].lower()] = len(rows)
    for rel, data in files.items():
        if not rel.startswith('WEAPON/'):
            continue
        d = dsgo.to_py(dsgo.parse(data).root)
        entry = next(x for x in d['Ammo_CustomParameter'] if isinstance(x, list) and len(x) >= 4 and isinstance(x[2], str)
                     and x[2].lower().startswith('app:/object/edf6vc_'))
        vehicle = entry[2].split('/')[-1]
        assert vehicle in holders, (rel, vehicle)
        assert len(entry[3][3]) == holders[vehicle], (rel, len(entry[3][3]), holders[vehicle])
        assert entry[2] in d['resource'] and all(p in d['resource'] for p in (w[0] for w in entry[3][3][STOCK_WEAPONS:])), rel


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


def build(root: str) -> tuple[dict[str, bytes], list[str]]:
    """({path under Mods: bytes}, the requests left alone because another mod put its own into Mods/WEAPON)."""
    game = vc.Game(root)
    led = ledger.Ledger(root)
    out: dict[str, bytes] = {}
    skipped: list[str] = []
    stems: set[str] = set()
    for name in requests(game):
        rel = f'WEAPON/{name}'
        if os.path.isfile(led.disk(rel)) and OWNER not in led.owners(rel):
            skipped.append(rel)
            continue
        made = request_sgo(game.read('WEAPON', name), name)
        if made is None:
            continue
        out[rel], stem = made
        stems.add(stem)
    for stem in sorted(stems):
        out[f'OBJECT/{derived_name(stem)}'] = derived_vehicle(game, stem)
    check(out)
    return out, skipped


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


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept because someone else changed them since). Its own were all written
    under the ledger, and the store weapons are only needed: neither is a writer's release (the jets' stay)."""
    return ledger.Ledger(root).release(OWNER)


def wanted(ini_text: str) -> bool:
    """INI_KEY in [VehicleCrew] of the ini's text (the player's own, else the shipped one): on when 1."""
    section = ''
    for line in ini_text.splitlines():
        s = line.strip()
        if s.startswith('[') and s.endswith(']'):
            section = s[1:-1].strip()
            continue
        m = re.match(r'([A-Za-z0-9_]+)\s*=\s*([^;]*)', s)
        if m and section.lower() == 'vehiclecrew' and m.group(1).lower() == INI_KEY.lower():
            return m.group(2).strip() not in ('', '0')
    return False


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
            print('保留（之后被别的工具改过）', p)
        return 0
    files, skipped = build(root)
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
