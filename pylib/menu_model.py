"""The unified model file: one SGO a unit's battle loader and the room / HQ 3D preview's menu renderer both read
(docs/unified-model-re.md). The battle file stays as it is; the keys only the menu reads are added:

  menu_animation_model   the menu's [[archive, model], menu CAS, MAB]: the one key both sides read with different values
                         is animation_model (the menu needs a CAS with the menu poses and an anmgroup named exactly
                         `main`), so the plugin's hook (src/room_preview.cpp MenuModelLoad, EDF.dll 0x93AD30) makes the
                         menu read this key whenever a document has it
  change_color           the menu's colour selectors ([[selector, 'change_color0'], [selector, 'change_color1']])
  random_animation       the menu's idle poses
  soldier_weapon_slot / soldier_weapon_grab_adjust   a soldier's battle values serve both (the same shape the menu
                         reads); a vehicle gets the stock Ranger menu's, its slot's anmgroup the menu CAS's `main`
                         (no weapons are hung on it; the Ranger's `weapon` group is in no vehicle CAS: vehicle_menu_keys)

None of these is a key a battle reader looks up (the table in the RE note), so every key the battle reads is left byte
for byte as it was: battle_equal() checks it, menu_problems() refuses a file missing anything either side needs.
Both containers: a classic SGO's members (pylib/sgo.py values) and a DSGO's root (pylib/dsgo.py Node).
"""
from __future__ import annotations

import struct
from typing import Callable

import dsgo
import sgo

MENU_MODEL = 'menu_animation_model'   # src/room_preview_logic.h kMenuModelKey
BATTLE_MODEL = 'animation_model'
SOLDIER_KEYS = ('soldier_weapon_slot', 'soldier_weapon_grab_adjust')
ADDED = (MENU_MODEL, 'change_color', 'random_animation')   # what a soldier's battle file gains
VEHICLE_ADDED = (*ADDED, *SOLDIER_KEYS)   # what a vehicle's battle file gains (vehicle_menu_keys)
MENU_NEEDS = (MENU_MODEL, 'change_color', 'random_animation', 'soldier_weapon_slot')
MENU_ANMGROUP = 'main'   # the anmgroup the menu plays, looked up case-sensitively (EDF.dll 0x93FB85; -1 crashes it)
# How many soldier_weapon_slot groups the menu renderer reads, by the shown class (docs/room-3d-preview-re.md 6.1): the
# async build 0x93F910 pushes each entry's anmgroup number (entry[1] looked up in the menu CAS, 0x93FD3D) only when
# found, then 0x93FEF5 writes the slot count 1 (2 at 0x93FF07 for Desc+0x20 == 3, the Fencer) unconditionally, and the
# view update 0x93D640 reads that many numbers (0x93ED2E) with no size check: one short reads past the vector (an
# empty one: address 0). By the template's prefix (STOCK_MENU); a vehicle is shown as the Ranger (1).
MENU_WEAPON_SLOTS = {'N607': 2}
CHANNELS = ('change_color0', 'change_color1')
EVERY = 0   # change_color selector: every material
MENU_FOLDER = 'MENUOBJECT'
# The stock menu SGO whose menu CAS / MAB / idle poses a class uses (Root.cpk MENUOBJECT), by the battle template's
# prefix; a vehicle takes the Ranger's idle poses and weapon slot (it hangs none).
STOCK_MENU = {'N601': 'P505_RANGER.SGO', 'N606': 'P606_WINGDIVER.SGO', 'N607': 'P607_FENCER.SGO'}
VEHICLE_STOCK = 'P505_RANGER.SGO'
JET_CAS = 'app:/object/v506_heli.cas'   # every plugin jet's CAS (pylib/vcobjects.py jet_sgo): `Main`, no `main`


def menu_cas_name(cas_path: str) -> str:
    """The menu CAS a vehicle CAS without an exact `main` anmgroup gets (tools/menu_preview.py writes it):
    'app:/object/v506_heli.cas' -> 'EDF6VC_MENU_V506_HELI.CAS' (under Mods/MENUOBJECT)."""
    return 'EDF6VC_MENU_' + cas_path.rsplit('/', 1)[-1].upper()


def menu_cas_path(cas_path: str) -> str:
    return f'app:/{MENU_FOLDER.lower()}/{menu_cas_name(cas_path).lower()}'


# ------------------------------------------------------------------------------------------------ values


def _f(x: float) -> sgo.Float:
    return sgo.Float(struct.pack('<f', float(x)))


def to_dsgo(v: object) -> dsgo.Value:
    """A classic SGO value (sgo.read) as a DSGO value: lists -> Node, numbers -> float, bytes -> Blob."""
    if isinstance(v, list):
        return dsgo.Node([to_dsgo(c) for c in v])
    if isinstance(v, sgo.Float):
        return float(v.value)
    if isinstance(v, bool):
        raise ValueError('bool is no SGO value')
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, bytes):
        return dsgo.Blob(v, 2)
    if isinstance(v, str):
        return v
    raise ValueError(f'{type(v).__name__} is no SGO value')


def plain(v: object) -> object:
    """Either container's value as plain Python (floats for every number, bytes for blobs): the comparisons' form."""
    if isinstance(v, dsgo.Node):
        if v.names and len(v.names) == len(v.items):
            return {v.names[i]: plain(c) for i, c in enumerate(v.items)}
        return [plain(c) for c in v.items]
    if isinstance(v, dsgo.Blob):
        return v.data
    if isinstance(v, list):
        return [plain(c) for c in v]
    if isinstance(v, sgo.Float):
        return float(v.value)
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        return float(v)
    return v


# ------------------------------------------------------------------------------------------------ the menu keys


def soldier_menu_keys(stock_menu: dict, archive: str, model: str, selectors: list) -> dict:
    """A soldier's added keys (classic SGO values): the stock menu SGO's menu CAS / MAB / idle poses on the battle
    file's own model, change_color with `selectors` (EVERY or a material name, one a channel)."""
    menu = stock_menu[BATTLE_MODEL]
    return {MENU_MODEL: [[archive, model], menu[1], menu[2]],
            'change_color': [[s, c] for s, c in zip(selectors, CHANNELS)],
            'random_animation': stock_menu['random_animation']}


def weapon_slots(name: str) -> int:
    """How many soldier_weapon_slot groups the menu reads for unit file `name` (a template name, a vehicle's anything
    else): MENU_WEAPON_SLOTS."""
    return MENU_WEAPON_SLOTS.get(name.rsplit('/', 1)[-1].upper()[:4], 1)


def vehicle_menu_keys(stock_menu: dict, battle_model: list, cas_path: str) -> dict:
    """A vehicle's added keys (classic SGO values): its own model and MAB with the menu CAS `cas_path`, the stock
    Ranger menu's selectors (its channels match no vehicle material), idle poses, grab adjust and weapon slot, the
    slot's anmgroup MENU_ANMGROUP: the Ranger's `weapon` is in no vehicle CAS, and a name not found pushes no number
    while the renderer still reads one (MENU_WEAPON_SLOTS; the 整备 -> 载具 crash). `main` is the one group every
    vehicle menu CAS has (menu_problems); what the renderer plays on it there is each shown weapon's AimAnimation state
    (0x93D2F0, a name not in the group skipped at 0x93D31E), and a vehicle is shown with no weapon."""
    return {MENU_MODEL: [list(battle_model[0][:2]), cas_path, battle_model[2]],
            'change_color': stock_menu['change_color'],
            'random_animation': stock_menu['random_animation'],
            'soldier_weapon_slot': [[e[0], MENU_ANMGROUP, *e[2:]] for e in stock_menu['soldier_weapon_slot']],
            'soldier_weapon_grab_adjust': stock_menu['soldier_weapon_grab_adjust']}


def add_sgo(members: dict, keys: dict) -> dict:
    """A classic SGO's members with `keys` added (ValueError when one is there with another value: never overwrite a
    battle value)."""
    out = dict(members)
    for k, v in keys.items():
        if k in out and plain(out[k]) != plain(v):
            raise ValueError(f'{k} 已存在且值不同：菜单与战斗冲突')
        out[k] = v
    return out


def add_dsgo(root: dsgo.Node, keys: dict) -> None:
    """A DSGO root with `keys` (classic values) added in place; the same refusal as add_sgo."""
    for k, v in keys.items():
        d = to_dsgo(v)
        try:
            old = root.get(k)
        except KeyError:
            root.set(k, d)
            continue
        if plain(old) != plain(d):
            raise ValueError(f'{k} 已存在且值不同：菜单与战斗冲突')


# ------------------------------------------------------------------------------------------------ checks


def members_of(data: bytes) -> dict:
    """Either container's top-level keys as plain Python."""
    if data[:4] == b'DSGO':
        root = dsgo.parse(data).root
        return {n: plain(root.items[i]) for i, n in root.names.items()}
    return {k: plain(v) for k, v in sgo.read(data)[1].items()}


def cas_groups(cas: bytes) -> list[str]:
    import cas_legacy
    lay = cas_legacy.cas_layout(cas)
    return [cas_legacy._text(cas, at + cas_legacy._u(cas, '<i', at)[0]) for at, _ in lay.tree.anmgroups]


def menu_problems(data: bytes, read_cas: Callable[[str], bytes | None], slots: int = 1) -> list[str]:
    """What keeps `data` from being a unified file ([] none): the battle model, every key the menu needs, the menu
    model's shape, and the menu CAS (`read_cas`: a CAS path -> its bytes, None when nowhere) having an anmgroup named
    exactly MENU_ANMGROUP and every soldier_weapon_slot entry's group, with at least `slots` entries (weapon_slots).
    Always checked: the renderer indexes those groups' numbers without a check (EDF.dll 0x93EBAC: -1 crashed test site
    #50; 0x93ED2E: a vehicle's `weapon` slot, not in its CAS, read address 0), so no file without them may be written."""
    m = members_of(data)
    out = [f'缺 {k}' for k in (BATTLE_MODEL, *MENU_NEEDS) if k not in m]
    model = m.get(MENU_MODEL)
    slot = m.get('soldier_weapon_slot')
    names = None
    if slot is not None:
        if not (isinstance(slot, list) and all(isinstance(e, list) and len(e) >= 2 and isinstance(e[1], str)
                                               for e in slot)):
            out.append('soldier_weapon_slot 不是 [[武器号], 动画组, ...] 的列表')
        elif len(slot) < slots:
            out.append(f'soldier_weapon_slot 只有 {len(slot)} 项，渲染器要读 {slots} 个动画组下标（0x93ED2E 越界）')
        else:
            names = [e[1] for e in slot]
    if model is not None:
        if not (isinstance(model, list) and len(model) == 3 and isinstance(model[0], list) and len(model[0]) == 2
                and all(isinstance(s, str) for s in model[0]) and isinstance(model[1], str)):
            out.append(f'{MENU_MODEL} 不是 [[archive, model], CAS, MAB]')
        else:
            cas = read_cas(model[1])
            if cas is None:
                out.append(f'菜单 CAS {model[1]} 不存在')
            else:
                groups = cas_groups(cas)
                if MENU_ANMGROUP not in groups:
                    out.append(f'菜单 CAS {model[1]} 没有名为 {MENU_ANMGROUP!r} 的动画组（{groups}）：渲染器会拿 -1 当下标')
                lost = [n for n in names or [] if n not in groups]
                if lost:
                    out.append(f'菜单 CAS {model[1]} 没有 soldier_weapon_slot 的动画组 {lost}（{groups}）：'
                               '渲染器少存下标、照读（0x93ED2E 读野指针）')
    colours = m.get('change_color')
    if colours is not None and not (isinstance(colours, list) and len(colours) == 2
                                    and [c[1] for c in colours] == list(CHANNELS)):
        out.append('change_color 不是 change_color0 / change_color1 两项')
    return out


def battle_equal(before: bytes, after: bytes) -> list[str]:
    """The keys whose value differs between `before` (the battle file) and `after` (its unified file) other than the
    added menu keys, or that `after` lost ([] none: every battle-read key the same)."""
    a, b = members_of(before), members_of(after)
    added = set(ADDED) | set(SOLDIER_KEYS) | {'random_animation', 'change_color'}
    out = [k for k in a if k not in b or a[k] != b[k]]
    out += [k for k in b if k not in a and k not in added]
    return out
