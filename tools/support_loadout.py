"""Out-of-game support loadouts: the editor (installer menu 7 -> l) and the files the plugin needs for them.

The one source of truth is EDF6VehicleCrew.ini [VehicleCrew] (src/support_loadout.h has the syntax; the plugin rereads
the ini on every save):
  SupportPreset_<KEY>=lance@1E3A8A*4,cannon*4      a seated entry's soldiers in seat order, colours, counts
  SupportVehicle_<KEY>=HE,AP:25                    a tank's main gun (HE / AP) and the rounds on its pylons beside it
  SupportVehicle_<KEY>=MK82:6,MK82:6               a jet's pylons (the guns always aboard); GUNS: the guns alone
This module validates every value the way the plugin does (tests/support_loadout_ini_test.py keeps the tables equal to
the C++ ones), so a value the editor writes is never one the plugin would refuse.

The generated files, written into <game>/Mods and recorded in the ledger (pylib/ledger.py, owner `loadout`), each built
from the game's own files (Root.cpk only read). Their names say what they are, so any of them can be made from its name
alone: a peer that lacked one in an online room listed it in Mods/Plugins/EDF6VehicleCrew.variants_pending.txt
(src/support_variants.cpp), and this tool makes every name listed there too, then clears the list.
  OBJECT/EDF6VC_NPC_<KIND>[_L]_<PRIMARY|X>_<SECONDARY|X>.SGO   a coloured soldier: the stock AI template of that kind (its
      _LEADER for a leader) with its soldier_color entries recoloured (change_color0 = primary, change_color1 =
      secondary; a face's own entry is kept). Nothing else changes: the same class, model, CAS, AI weapon.
  OBJECT/EDF6VC_LO_<BODY>_<VARIANT>.SGO   a loaded vehicle (VARIANT: support_loadout.h LoadoutVariant, 16 hex digits):
      TANK  the support tank V505_TANK_MISSION (the Mods copy other tools made, else the stock), its main gun the stock
            Blacker A1's mount (WEAPON/EWEAPON419: the 90 mm smooth-bore) for AP, a holder more for each pylon after it
            (src/stores.cpp builds them; src/payload.cpp: the driver and the player switch round them on the gun's control)
      a jet the installed EDF6VC_JET_<BODY>.SGO (tools/make_jets.py) with its weapon list rebuilt: the guns L / R, the
            pylons, the fuel tank fourth (pylib/vcobjects.py with_fuel), at least four holders
  WEAPON/EDF6VC_<STORE>_<rounds>.SGO   a pylon's weapon for that load when no other tool wrote it (pylib/vcobjects.py
      store_sgo: a missile, a bomb, a stock vehicle's round)
Only coloured soldiers and loaded vehicles the ini or the pending list name get a file; files no longer needed are
released.

  python tools/support_loadout.py [game dir]            write / refresh from the game's EDF6VehicleCrew.ini
  python tools/support_loadout.py [game dir] --remove   release them
"""
from __future__ import annotations

import os
import re
import struct
import sys
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import ledger  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
from modfiles import atomic_write, sha256, sha256_file  # noqa: E402

OWNER = 'loadout'   # pylib/ledger.py
SECTION = 'VehicleCrew'
MOST = 12           # src/support_call.h kSupportLoadoutMost
STOCK = -1          # src/support_loadout.h kStockColour
PENDING = 'EDF6VehicleCrew.variants_pending.txt'   # src/support_variants.cpp NoteMissingVariant (Mods/Plugins)

# src/support_loadout.h kSupportKindNames, in SupportWeapon order, with src/support_soldier.cpp kBodies' templates.
KINDS: tuple[tuple[str, str, str], ...] = (
    ('rifle', '游骑兵·步枪', 'N601_COMMON_RANGER_AF'), ('flame', '游骑兵·火焰', 'N601_COMMON_RANGER_FL'),
    ('rocket', '游骑兵·火箭', 'N601_COMMON_RANGER_RL'), ('shotgun', '游骑兵·霰弹', 'N601_COMMON_RANGER_SG'),
    ('sniper', '游骑兵·狙击', 'N601_COMMON_RANGER_SN'),
    ('lance', '翼人·长矛', 'N606_AIPALEWING_LANCE'), ('laser', '翼人·激光', 'N606_AIPALEWING_LR'),
    ('monster', '翼人·狙击', 'N606_AIPALEWING_MS'), ('izuna', '翼人·雷链', 'N606_AIPALEWING_IZN'),
    ('thunderbow', '翼人·雷弓', 'N606_AIPALEWING_TB'),
    ('cannon', '重装·重炮', 'N607_AIHEAVYARMOR_SC'), ('midcannon', '重装·中炮', 'N607_AIHEAVYARMOR_SMC'),
    ('pilebanker', '重装·打桩', 'N607_AIHEAVYARMOR_SP'), ('fshotgun', '重装·霰弹', 'N607_AIHEAVYARMOR_SSG'),
)
KIND_NAMES = tuple(k[0] for k in KINDS)
# The catalog entries a player can fill (src/support_dispatch.cpp SupportCallSeats) and their seats.
SEATS: dict[str, int] = {'SQUAD': 12, 'PLATOON': 12, 'TRANSPORT_CREWED': 4, 'TRUCK_CREWED': 4,
                         'SQUAD_HELI': 12, 'PLATOON_HELI': 12, 'SQUAD_AIRDROP': 12, 'PLATOON_AIRDROP': 12}
LABELS = {'SQUAD': '步兵小队', 'PLATOON': '步兵大队', 'TRANSPORT_CREWED': '装甲运兵车·有人（乘客）',
          'TRUCK_CREWED': '民用轻卡·有人（乘客）', 'SQUAD_HELI': '直升机机降·小队', 'PLATOON_HELI': '直升机机降·大队',
          'SQUAD_AIRDROP': '运输机空降·小队', 'PLATOON_AIRDROP': '运输机空降·大队'}

# src/support_loadout.h kLoadStores: (code, name, label, role, on jets, on tanks).
LOAD_STORES: tuple[tuple[int, str, str, str, bool, bool], ...] = (
    (1, 'AAM_S', 'AIM-9X 近程空空导弹', 'air', True, False),
    (2, 'AAM_M', 'AIM-120 中程空空导弹', 'air', True, False),
    (3, 'AAM_L', 'AIM-54 远程空空导弹', 'air', True, False),
    (4, 'AGM', 'AGM-65 空地导弹', 'ground', True, False),
    (5, 'AGM_L', 'AGM-114 轻型空地导弹', 'ground', True, False),
    (6, 'MK82', 'Mk 82 炸弹', 'bomb', True, False),
    (7, 'RKT', 'Hydra 70 火箭巢', 'rocket', True, False),
    (8, 'AP', 'APFSDS 穿甲弹', 'gun', False, True),
    (9, 'HE', 'HE 榴弹', 'gun', False, True),
    (10, 'GLM', 'LAHAT 炮射导弹', 'ground', False, True),
)
STORE_CODE = {s[1]: s[0] for s in LOAD_STORES}
STORE_BY_CODE = {s[0]: s for s in LOAD_STORES}
ROUND_COUNTS = (1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 19, 20, 25, 30, 38, 40)   # kRoundCounts
PYLONS_MOST = 7                                                            # kPylonsMost
# src/support_loadout.h kVehicleBodies: name -> (base file, most pylons, tank).
BODIES: dict[str, tuple[str, int, bool]] = {
    'TANK': ('V505_TANK_MISSION.SGO', 4, True),
    'STRIKE': ('EDF6VC_JET_STRIKE.SGO', 5, False),
    'FIGHTER': ('EDF6VC_JET_FIGHTER.SGO', 5, False),
    'INTERCEPTOR': ('EDF6VC_JET_INTERCEPTOR.SGO', 5, False),
    'MULTIROLE': ('EDF6VC_JET_MULTIROLE.SGO', 5, False),
}
# kVehicleKeys: catalog key -> body.
VEHICLE_KEYS: dict[str, str] = {
    'TANK_CREWED': 'TANK', 'TANK_DELIVERY': 'TANK', 'STRIKE': 'STRIKE', 'STRIKE_F': 'STRIKE', 'FIGHTER': 'FIGHTER',
    'FIGHTER_F': 'FIGHTER', 'INTERCEPTOR': 'INTERCEPTOR', 'INTERCEPTOR_F': 'INTERCEPTOR', 'MULTIROLE': 'MULTIROLE',
    'MULTIROLE_F': 'MULTIROLE',
}
VEHICLE_LABELS = {'TANK_CREWED': '坦克·有人', 'TANK_DELIVERY': '坦克·空车交付', 'STRIKE': '对地攻击机·守点',
                  'STRIKE_F': '对地攻击机·跟随', 'FIGHTER': '制空战斗机·守点', 'FIGHTER_F': '制空战斗机·跟随',
                  'INTERCEPTOR': '截击机·守点', 'INTERCEPTOR_F': '截击机·跟随', 'MULTIROLE': '多用途机·守点',
                  'MULTIROLE_F': '多用途机·跟随'}
APPLIED, VEHICLE = 1 << 63, 1 << 62   # kVariantApplied, kVariantVehicle
AP_CALL = 'EWEAPON419.SGO'                      # the stock Blacker A1 request: the AP gun's mount
AP_GUN = 'app:/weapon/v_505tank_cannon01s.sgo'
HE_GUN = 'app:/weapon/v_505tank_cannon01.sgo'
GUN_FILES = ('app:/weapon/edf6vc_jet_gun_l.sgo', 'app:/weapon/edf6vc_jet_gun_r.sgo')   # a jet's guns (vcobjects _GUNS)
FUEL = 'app:/weapon/v_fuel01.sgo'
FUEL_AT = 3                                     # pylib/vcobjects.py FUEL_AT
MISSILE_PARAMS, BOMB_PARAMS, NO_RECOIL = [0.01, 0.1], [0.0001, 0.1], [0.0, 0.0]


class Invalid(ValueError):
    """A value the plugin would refuse; the message names it."""


Look = tuple[int, int]   # (primary, secondary), 0xRRGGBB or STOCK
Soldier = tuple[str, Look]
Pylon = tuple[str, int]  # (store name, rounds)


class Loadout:
    """A vehicle's pylons (support_loadout.h VehicleLoadout): its body, a tank's AP main gun, the pylons in order."""

    def __init__(self, body: str, ap_gun: bool = False, pylons: list[Pylon] | None = None) -> None:
        self.body, self.ap_gun, self.pylons = body, ap_gun, list(pylons or [])

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Loadout) and (self.body, self.ap_gun, self.pylons) == (other.body, other.ap_gun, other.pylons)

    def __repr__(self) -> str:
        return f'Loadout({self.body!r}, {self.ap_gun}, {self.pylons})'


def _tokens(text: str) -> list[str]:
    return [t.strip() for t in re.split(r'[,，;、]', text) if t.strip()]


def _colour(text: str) -> int:
    t = text.strip()
    if t in ('X', 'x'):
        return STOCK
    if not re.fullmatch(r'[0-9A-Fa-f]{6}', t):
        raise Invalid(f'颜色应为 @RRGGBB 或 @RRGGBB:RRGGBB（X = 原色）：@{text}')
    return int(t, 16)


def _count(text: str, most: int, what: str) -> int:
    t = text.strip()
    if not t.isdigit() or len(t) > 5 or not 1 <= int(t) <= most:
        raise Invalid(f'{what}应为 1 到 {most}：{text}')
    return int(t)


def kind(text: str) -> str:
    t = text.strip().lower()
    if t not in KIND_NAMES:
        raise Invalid(f'未知兵种：{text}（可选 ' + ' / '.join(f'{n}={label}' for n, label, _ in KINDS) + '）')
    return t


def parse_preset(text: str, seats: int) -> list[Soldier]:
    """A SupportPreset_<KEY> value as the plugin reads it (ParseSupportPreset): the soldiers in seat order."""
    out: list[Soldier] = []
    for token in _tokens(text):
        head, star, n = token.partition('*')
        name, at, colours = head.partition('@')
        k = kind(name)
        look: Look = (STOCK, STOCK)
        if at:
            primary, colon, secondary = colours.partition(':')
            look = (_colour(primary), _colour(secondary) if colon else STOCK)
        count = _count(n, MOST, '人数 *') if star else 1
        if len(out) + count > MOST:
            raise Invalid(f'超过 {MOST} 人')
        out += [(k, look)] * count
    if out and len(out) > seats:
        raise Invalid('该单位没有可编组的座位' if seats <= 0 else f'人数 {len(out)} 超过该单位的座位数 {seats}')
    return out


def _colour_text(v: int) -> str:
    return 'X' if v == STOCK else f'{v:06X}'


def format_preset(soldiers: list[Soldier]) -> str:
    """The shortest value for `soldiers` (consecutive equal soldiers as *n)."""
    out: list[str] = []
    i = 0
    while i < len(soldiers):
        j = i
        while j < len(soldiers) and soldiers[j] == soldiers[i]:
            j += 1
        k, (p, s) = soldiers[i]
        token = k + ('' if (p, s) == (STOCK, STOCK) else '@' + _colour_text(p) + ('' if s == STOCK else ':' + _colour_text(s)))
        out.append(token + (f'*{j - i}' if j - i > 1 else ''))
        i = j
    return ','.join(out)


def look_file(k: str, leader: bool, look: Look) -> str:
    """src/support_loadout.h SupportLookFile."""
    return f'EDF6VC_NPC_{k.upper()}{"_L" if leader else ""}_{_colour_text(look[0])}_{_colour_text(look[1])}.SGO'


# ------------------------------------------------------------------------------------------------ vehicle pylons


def parse_vehicle(text: str, body: str) -> Loadout | None:
    """A SupportVehicle_<KEY> value for `body` as the plugin reads it (ParseVehicleLoadout); None: empty (stock)."""
    tokens = _tokens(text)
    if not tokens:
        return None
    if body not in BODIES:
        raise Invalid('该单位没有可配置的挂点')
    _, most, tank = BODIES[body]
    out = Loadout(body)
    if not tank and len(tokens) == 1 and tokens[0].upper() == 'GUNS':
        return out
    if not tank and any(t.upper() == 'GUNS' for t in tokens):
        raise Invalid('GUNS 表示只带机炮，不能再写挂载')
    if tank:
        if tokens[0].upper() not in ('HE', 'AP'):
            raise Invalid(f'坦克的第一项是主炮 HE 或 AP：{tokens[0]}')
        out.ap_gun = tokens[0].upper() == 'AP'
        tokens = tokens[1:]
    for token in tokens:
        name, colon, n = token.partition(':')
        name = name.strip().upper()
        store = STORE_BY_CODE.get(STORE_CODE.get(name, 0))
        if store is None or not (store[5] if tank else store[4]):
            raise Invalid(('坦克可挂：AP / HE / GLM，不能挂：' if tank else
                           '飞机可挂：AAM_S / AAM_M / AAM_L / AGM / AGM_L / MK82 / RKT，不能挂：') + name)
        t = n.strip()
        if not colon or not t.isdigit() or int(t) not in ROUND_COUNTS:
            raise Invalid('挂点写成 名称:数量，数量可选 ' + ' '.join(map(str, ROUND_COUNTS)) + f'：{token}')
        if len(out.pylons) >= most:
            raise Invalid('坦克最多 4 个挂点' if tank else '飞机最多 5 个挂点')
        out.pylons.append((name, int(t)))
    return out


def format_vehicle(l: Loadout) -> str:
    parts = (['AP' if l.ap_gun else 'HE'] if BODIES[l.body][2] else []) + [f'{n}:{r}' for n, r in l.pylons]
    return ','.join(parts) or 'GUNS'


def variant(l: Loadout) -> int:
    """support_loadout.h LoadoutVariant."""
    v = VEHICLE | len(l.pylons) | (8 if l.ap_gun else 0)
    for i, (name, rounds) in enumerate(l.pylons):
        v |= (STORE_CODE[name] | ROUND_COUNTS.index(rounds) << 4) << (4 + 8 * i)
    return v


def of_variant(v: int, body: str) -> Loadout | None:
    """support_loadout.h LoadoutOfVariant: None when it does not decode for `body`."""
    bits = v & ~APPLIED
    if body not in BODIES or not bits & VEHICLE or (bits >> 60) & 3:
        return None
    _, most, tank = BODIES[body]
    count, ap = bits & 7, bool(bits & 8)
    if count > most or (ap and not tank):
        return None
    out = Loadout(body, ap)
    for i in range(PYLONS_MOST):
        byte = (bits >> (4 + 8 * i)) & 0xFF
        if i >= count:
            if byte:
                return None
            continue
        store = STORE_BY_CODE.get(byte & 0xF)
        if store is None or not (store[5] if tank else store[4]):
            return None
        out.pylons.append((store[1], ROUND_COUNTS[byte >> 4]))
    return out


def vehicle_file(l: Loadout) -> str:
    """support_loadout.h VehicleVariantFile."""
    return f'EDF6VC_LO_{l.body}_{variant(l):016X}.SGO'


def store_file(name: str, rounds: int) -> str:
    return f'EDF6VC_{name}_{rounds}.SGO'   # pylib/vcobjects.py store_file


def variant_hash(file: str) -> int:
    """support_loadout.h VariantHash: FNV-1a 64 of the upper-case name, two bytes a UTF-16 unit."""
    h = 1469598103934665603
    for ch in file.upper():
        c = ord(ch)
        for byte in (c & 0xFF, (c >> 8) & 0xFF):
            h = ((h ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


_NPC_NAME = re.compile(r'EDF6VC_NPC_([A-Z]+)(_L)?_([0-9A-F]{6}|X)_([0-9A-F]{6}|X)\.SGO$')
_LO_NAME = re.compile(r'EDF6VC_LO_([A-Z]+)_([0-9A-F]{16})\.SGO$')


def decode_name(file: str) -> tuple | None:
    """A generated file's name back into what it is: ('npc', kind, leader, look) or ('lo', Loadout); None: not one."""
    name = file.strip().upper()
    m = _NPC_NAME.match(name)
    if m and m.group(1).lower() in KIND_NAMES:
        look = tuple(STOCK if g == 'X' else int(g, 16) for g in (m.group(3), m.group(4)))
        if look != (STOCK, STOCK):
            return 'npc', m.group(1).lower(), bool(m.group(2)), look
        return None
    m = _LO_NAME.match(name)
    if m:
        l = of_variant(int(m.group(2), 16), m.group(1))
        if l is not None and not int(m.group(2), 16) & APPLIED and vehicle_file(l) == name:
            return 'lo', l
    return None


# ------------------------------------------------------------------------------------------------ ini text


def _line(lines: list[str], key: str) -> int | None:
    section = ''
    for i, line in enumerate(lines):
        m = re.match(r'^\s*\[([^\]]+)\]', line)
        if m:
            section = m.group(1).strip().lower()
            continue
        m = re.match(r'^\s*([A-Za-z0-9_]+)\s*=', line)
        if m and section == SECTION.lower() and m.group(1).lower() == key.lower():
            return i
    return None


def get(text: str, key: str) -> str:
    """The value as the plugin reads it (GetPrivateProfileString: trimmed, nothing taken off)."""
    lines = text.splitlines()
    i = _line(lines, key)
    return '' if i is None else lines[i].split('=', 1)[1].strip()


def presets(text: str) -> dict[str, list[Soldier]]:
    """Every valid preset in the ini, {key: soldiers}; an invalid one is left out (the plugin ignores it too)."""
    out = {}
    for key, seats in SEATS.items():
        try:
            soldiers = parse_preset(get(text, 'SupportPreset_' + key), seats)
        except Invalid:
            continue
        if soldiers:
            out[key] = soldiers
    return out


def vehicles(text: str) -> dict[str, Loadout]:
    """Every valid vehicle loadout in the ini, {key: loadout}."""
    out = {}
    for key, body in VEHICLE_KEYS.items():
        try:
            l = parse_vehicle(get(text, 'SupportVehicle_' + key), body)
        except Invalid:
            continue
        if l is not None:
            out[key] = l
    return out


def wanted_files(text: str, pending: list[str] = ()) -> list[str]:
    """Every generated file the ini's presets and loadouts need, then the pending list's (decodable ones), in order."""
    out: list[str] = []
    for soldiers in presets(text).values():
        for i, (k, look) in enumerate(soldiers):
            if look != (STOCK, STOCK):
                out.append(look_file(k, i % 4 == 0, look))
    out += [vehicle_file(l) for l in vehicles(text).values()]
    out += [p.strip().upper() for p in pending if decode_name(p)]
    return list(dict.fromkeys(out))


# ------------------------------------------------------------------------------------------------ generated files


def _source(root: str | None, game: rootcpk.Game, folder: str, name: str) -> bytes | None:
    """`folder`/`name` as the game would load it: the Mods copy another tool made, else Root.cpk's (None: neither)."""
    loose = os.path.join(root, 'Mods', folder, name) if root else ''
    if loose and os.path.isfile(loose):
        with open(loose, 'rb') as f:
            return f.read()
    try:
        return game.read(folder, name)
    except KeyError:
        return None


def recolour(data: bytes, look: Look) -> bytes:
    """A soldier template (DSGO) with its soldier_color recoloured; every other value as it was."""
    if data[:4] != b'DSGO':
        raise ValueError('不是 DSGO 士兵模板')
    doc = dsgo.parse(data)
    colours = doc.root.get('soldier_color')
    changed = 0
    for entry in colours.items:
        selectors, rgba = entry.items
        channels = {sel.items[1] for sel in selectors.items}
        meshes = [sel.items[0] for sel in selectors.items]
        if any(isinstance(m, str) and 'face' in m.lower() for m in meshes):
            continue   # the Wing Diver leader's face tint: a face is not a uniform
        for channel, value in (('change_color0', look[0]), ('change_color1', look[1])):
            if channel in channels and value != STOCK:
                rgba.items[0:3] = [((value >> s) & 0xFF) / 255.0 for s in (16, 8, 0)]
                changed += 1
    if look != (STOCK, STOCK) and not changed:
        raise ValueError('模板里没有可改的 soldier_color')
    return dsgo.write(doc)


def soldier_file(game: rootcpk.Game, k: str, leader: bool, look: Look) -> bytes:
    template = {n: t for n, _, t in KINDS}[k] + ('_LEADER' if leader else '')
    return recolour(game.read('OBJECT', template + '.SGO'), look)


def _as_sgo(v: object) -> sgo.Value:
    if isinstance(v, list):
        return [_as_sgo(c) for c in v]
    if isinstance(v, float):
        return sgo.Float(struct.pack('<f', v))
    return v


def ap_mount(game: rootcpk.Game) -> list:
    """The stock Blacker A1's gun mount ([weapon, recoil, turret]), from its request's vehicle setup."""
    call = sgo.load(data=game.read('WEAPON', AP_CALL))
    mount = call['Ammo_CustomParameter'][4][3][2][0]
    if not (isinstance(mount, list) and isinstance(mount[0], str) and mount[0].lower() == AP_GUN):
        raise ValueError(f'{AP_CALL} 不是原版布莱克 A1（主炮不是 {AP_GUN}）')
    return mount


def _weapon(name: str) -> str:
    return 'app:/weapon/' + name.lower()


def _store_params(name: str, beside: list | None = None) -> list:
    """A pylon's per-weapon parameters: a tank round recoils as the gun beside it (tools/make_stock_stores.py _params),
    a missile none; on a jet, a missile's or rocket's and a bomb's as the jets' own (tools/make_jets.py)."""
    role = STORE_BY_CODE[STORE_CODE[name]][3]
    if role == 'gun':
        return list(beside) if isinstance(beside, list) and len(beside) == 2 else list(NO_RECOIL)
    if beside is not None:   # a tank's missile (the gun-launched LAHAT)
        return list(NO_RECOIL)
    return list(BOMB_PARAMS if role == 'bomb' else MISSILE_PARAMS)


def tank_sgo(root: str | None, game: rootcpk.Game, l: Loadout) -> bytes:
    """The support tank with `l`: its main gun (AP: the A1's mount), then a holder a pylon after it."""
    data = _source(root, game, 'OBJECT', BODIES['TANK'][0])
    version, members = sgo.read(data)
    rows = members.get('vehicle_weapon_setting')
    if not isinstance(rows, list) or len(rows) != 1:
        raise ValueError(f'{BODIES["TANK"][0]} 不是一个挂点的布局')
    main = _as_sgo(ap_mount(game)) if l.ap_gun else None
    for key in ('mission_setup', 'vehicle_setup'):
        guns = members.get(key)
        if not isinstance(guns, list) or len(guns) < 3 or not isinstance(guns[2], list) or len(guns[2]) != 1:
            raise ValueError(f'{BODIES["TANK"][0]} 的 {key} 不是一门主炮的布局')
        if str(guns[2][0][0]).lower() != HE_GUN:
            raise ValueError(f'{BODIES["TANK"][0]} 的 {key} 主炮不是 {HE_GUN}（被别的 MOD 改过？）')
        if main is not None:
            guns[2][0] = main
        beside = [x.value if isinstance(x, sgo.Float) else x for x in guns[2][0][1]]
        for name, rounds in l.pylons:
            guns[2].append([_weapon(store_file(name, rounds)), _as_sgo(_store_params(name, beside))])
    members['vehicle_weapon_setting'] = rows + [list(rows[0]) for _ in l.pylons]
    resource = members.get('resource')
    if isinstance(resource, list):
        for path in ([AP_GUN] if l.ap_gun else []) + [_weapon(store_file(n, r)) for n, r in l.pylons]:
            if path not in [str(x).lower() for x in resource]:
                resource.append(path)
    return sgo.write_depth_first(version, members)


def jet_sgo(root: str | None, game: rootcpk.Game, l: Loadout) -> bytes:
    """The installed jet of `l.body` with its weapon list rebuilt from `l`: guns, pylons, fuel fourth, four holders at
    least (an empty holder is the stock 0.0 entry: the Heron YG10's missile has one)."""
    base = BODIES[l.body][0]
    data = _source(root, game, 'OBJECT', base)
    if data is None:
        raise ValueError(f'{base} 还没有安装：先用安装器选 1 安装')
    version, members = sgo.read(data)
    setup, rows = members['mission_setup'], members['vehicle_weapon_setting']
    guns = setup[3]
    if [str(g[0]).lower() for g in guns[:2]] != list(GUN_FILES) or not any(
            isinstance(g, list) and str(g[0]).lower() == FUEL for g in guns):
        raise ValueError(f'{base} 不是插件战机的武器表（机炮 L / R 与油箱）')
    gun_rows = rows[:2]
    fuel = next(g for g in guns if isinstance(g, list) and str(g[0]).lower() == FUEL)
    fuel_row = rows[[i for i, g in enumerate(guns) if isinstance(g, list) and str(g[0]).lower() == FUEL][0]]
    weapons = list(guns[:2]) + [[_weapon(store_file(n, r)), _as_sgo(_store_params(n))] for n, r in l.pylons]
    w_rows = list(gun_rows) + [list(gun_rows[0]) for _ in l.pylons]
    while len(weapons) < FUEL_AT:
        weapons.append(sgo.Float(struct.pack('<f', 0.0)))
        w_rows.append(list(gun_rows[0]))
    weapons.insert(FUEL_AT, fuel)
    w_rows.insert(FUEL_AT, fuel_row)
    setup[3] = weapons
    members['vehicle_weapon_setting'] = w_rows
    if 'vehicle_setup' in members:
        members['vehicle_setup'][3] = weapons
    return sgo.write_depth_first(version, members)


def check_vehicle(made: bytes, base: bytes, l: Loadout) -> None:
    """A loaded vehicle differs from its base only in its weapon list, its holders and (a tank) its preloads; the weapon
    list and the holders agree, and hold the guns, the pylons and (a jet) the fuel tank fourth."""
    a, b = sgo.load(data=made), sgo.load(data=base)
    assert set(a) == set(b), 'same members'
    tank = BODIES[l.body][2]
    at = 2 if tank else 3
    for key in a:
        if key in ('mission_setup', 'vehicle_setup'):
            assert a[key][:at] == b[key][:at] and a[key][at + 1:] == b[key][at + 1:], key
            assert len(a[key][at]) == len(a['vehicle_weapon_setting']), f'{key}: a weapon a holder'
        elif key in ('vehicle_weapon_setting', 'resource'):
            continue
        else:
            assert a[key] == b[key], key
    weapons = [w[0].lower() if isinstance(w, list) else None for w in a['mission_setup'][at]]
    stores = [_weapon(store_file(n, r)) for n, r in l.pylons]
    if tank:
        assert weapons == [AP_GUN if l.ap_gun else HE_GUN] + stores, weapons
    else:
        assert weapons[:2] == list(GUN_FILES) and weapons[FUEL_AT] == FUEL, weapons
        assert [w for i, w in enumerate(weapons) if i >= 2 and i != FUEL_AT and w] == stores, weapons
        assert len(weapons) >= 4, 'the 506 builds four holders without the plugin'


def build(root: str | None, ini_text: str, game: rootcpk.Game | None = None, pending: list[str] = ()) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}: the ini's (and the pending list's) coloured soldiers and
    loaded vehicles, and the pylon weapons they carry that are not on disk yet."""
    game = game or (rootcpk.Game(root) if root else rootcpk.default())
    out: dict[str, bytes] = {}
    for file in wanted_files(ini_text, pending):
        what = decode_name(file)
        if what[0] == 'npc':
            out['OBJECT/' + file] = soldier_file(game, *what[1:])
            continue
        l = what[1]
        tank = BODIES[l.body][2]
        made = tank_sgo(root, game, l) if tank else jet_sgo(root, game, l)
        check_vehicle(made, _source(root, game, 'OBJECT', BODIES[l.body][0]), l)
        out['OBJECT/' + file] = made
        for name, rounds in l.pylons:
            weapon = store_file(name, rounds)
            there = root and os.path.isfile(os.path.join(root, 'Mods', 'WEAPON', weapon))
            if not there and 'WEAPON/' + weapon not in out:
                import vcobjects as vc
                out['WEAPON/' + weapon] = vc.store_sgo(game, weapon)
    return out


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's, each one only when it is not already exactly that (every install and every
    menu-7 save rebuilds them: an unchanged file is not rewritten); what it wrote before and does not now is released.
    A pylon weapon another tool already wrote is only recorded as needed. Returns the paths written."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = []
    for rel, data in files.items():
        if sha256_file(led.disk(rel)) == sha256(data) and (ledger.key(rel) in before or led.owners(rel)):
            if ledger.key(rel) not in before:
                led.need(OWNER, rel)
            continue
        paths.append(led.put(OWNER, rel, data))
    # The pylon weapons a loaded vehicle carries that another tool wrote (tools/make_jets.py, make_stock_stores.py): needed
    # by this one too, so releasing theirs never pulls one from under it.
    needed = set()
    for rel in files:
        what = decode_name(rel.split('/')[-1]) if rel.upper().startswith('OBJECT/') else None
        for name, rounds in (what[1].pylons if what and what[0] == 'lo' else []):
            weapon = 'WEAPON/' + store_file(name, rounds)
            if weapon not in files and os.path.isfile(led.disk(weapon)):
                led.need(OWNER, weapon)
                needed.add(ledger.key(weapon))
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files} - needed))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def pending_path(root: str) -> str:
    return os.path.join(root, 'Mods', 'Plugins', PENDING)


def read_pending(root: str) -> list[str]:
    """The names the plugin listed (src/support_variants.cpp NoteMissingVariant); undecodable lines are ignored."""
    path = pending_path(root)
    if not os.path.isfile(path):
        return []
    with open(path, encoding='utf-8', errors='replace') as f:
        return [line.strip() for line in f if line.strip()]


def clear_pending(root: str, made: dict[str, bytes]) -> list[str]:
    """Drops from the pending list every name now made (or one no file can be made from); returns the names dropped."""
    path = pending_path(root)
    names = read_pending(root)
    if not names:
        return []
    keep = [n for n in names if decode_name(n) and 'OBJECT/' + n.upper() not in made]
    gone = [n for n in names if n not in keep]
    if keep:
        atomic_write(path, ''.join(n + '\r\n' for n in keep).encode('utf-8'))
    else:
        os.remove(path)
    return gone


# ------------------------------------------------------------------------------------------------ the editor


def put(text: str, key: str, value: str) -> str:
    import support_config
    return support_config.put(text, key, value)


def describe(soldiers: list[Soldier]) -> str:
    labels = {n: label for n, label, _ in KINDS}
    parts = []
    for i in range(0, len(soldiers), 4):
        squad = soldiers[i:i + 4]
        parts.append('[' + '、'.join(labels[k] + ('' if look == (STOCK, STOCK) else f'@{_colour_text(look[0])}:{_colour_text(look[1])}')
                                     for k, look in squad) + ']')
    return ' '.join(parts)


def describe_vehicle(l: Loadout | None) -> str:
    if l is None:
        return '（原版挂载）'
    labels = {s[1]: s[2] for s in LOAD_STORES}
    head = (['主炮 ' + ('90mm 滑膛炮（AP，30 发）' if l.ap_gun else '105mm 榴弹炮（HE，25 发）')] if BODIES[l.body][2]
            else ['机炮'])
    return '，'.join(head + [f'挂点{i + 1} {labels[n]}×{r}' for i, (n, r) in enumerate(l.pylons)])


def summary(text: str) -> str:
    rows = ['  小队预设（游戏外编辑，呼叫时套用；空 = 该单位自己的兵员）：']
    for i, key in enumerate(SEATS, 1):
        value = get(text, 'SupportPreset_' + key)
        try:
            soldiers = parse_preset(value, SEATS[key])
            shown = describe(soldiers) if soldiers else '（默认兵员）'
        except Invalid as e:
            shown = f'（无效，插件按默认兵员：{e}）'
        rows.append(f'  {i:2d}. {LABELS[key]}（{key}，{SEATS[key]} 座）：{shown}')
    rows.append('  载具挂载（战前配置每个挂点的武器）：')
    for i, key in enumerate(VEHICLE_KEYS, 1):
        try:
            shown = describe_vehicle(parse_vehicle(get(text, 'SupportVehicle_' + key), VEHICLE_KEYS[key]))
        except Invalid as e:
            shown = f'（无效，插件按原版挂载：{e}）'
        rows.append(f'   v{i}. {VEHICLE_LABELS[key]}（{key}）：{shown}')
    return '\n'.join(rows)


HELP = ('  写法：兵种[@主色[:副色]][*人数]，逗号分隔，按座位顺序，每 4 人第一位是队长。\n'
        '  兵种：' + ' '.join(f'{n}={label}' for n, label, _ in KINDS) + '\n'
        '  颜色：6 位十六进制 RRGGBB（如 1E3A8A），X = 保留原色；例：lance@1E3A8A*4,cannon*4,rifle@X:FFFFFF*4\n'
        '  直接回车 = 不改；输入 - = 清除（恢复该单位默认兵员）。')


def _pick(ask: Callable[[str], str], options: list[tuple[str, str]], prompt: str) -> str:
    for i, (_, label) in enumerate(options, 1):
        print(f'    {i}. {label}')
    got = ask(prompt).strip()
    if not got.isdigit() or not 1 <= int(got) <= len(options):
        raise Invalid(f'没有第 {got} 项')
    return options[int(got) - 1][0]


def edit_vehicle(text: str, key: str, ask: Callable[[str], str]) -> str:
    """载具 → 挂点 → 武器: one vehicle's pylons, until an empty answer. Writes only a loadout the plugin accepts."""
    body = VEHICLE_KEYS[key]
    _, most, tank = BODIES[body]
    try:
        l = parse_vehicle(get(text, 'SupportVehicle_' + key), body) or Loadout(body)
    except Invalid:
        l = Loadout(body)
    allowed = [(s[1], f'{s[1]}  {s[2]}') for s in LOAD_STORES if (s[5] if tank else s[4])]
    changed = False
    while True:
        print(f'  {VEHICLE_LABELS[key]}：{describe_vehicle(l)}')
        menu = ('编号 = 改那个挂点；a 加挂点；d 删最后一个挂点；' + ('g 换主炮（HE / AP）；' if tank else 'g 只带机炮（清空挂点）；') +
                's 恢复原版；回车保存并返回：')
        pick = ask(menu).strip().lower()
        if not pick:
            if changed:
                text = put(text, 'SupportVehicle_' + key, format_vehicle(l))
                assert parse_vehicle(get(text, 'SupportVehicle_' + key), body) == l   # what the plugin reads back
            return text
        try:
            if pick == 's':
                return put(text, 'SupportVehicle_' + key, '')
            if pick == 'g':
                if tank:
                    l.ap_gun = not l.ap_gun
                else:
                    l.pylons = []
                changed = True
                continue
            if pick == 'd':
                l.pylons = l.pylons[:-1]
                changed = True
                continue
            if pick == 'a' or (pick.isdigit() and 1 <= int(pick) <= len(l.pylons)):
                if pick == 'a' and len(l.pylons) >= most:
                    raise Invalid(f'最多 {most} 个挂点')
                name = _pick(ask, allowed, '挂什么（编号）：')
                rounds = ask('数量（' + ' '.join(map(str, ROUND_COUNTS)) + '）：').strip()
                if not rounds.isdigit() or int(rounds) not in ROUND_COUNTS:
                    raise Invalid(f'数量“{rounds}”不在可选范围内')
                pylon = (name, int(rounds))
                if pick == 'a':
                    l.pylons.append(pylon)
                else:
                    l.pylons[int(pick) - 1] = pylon
                changed = True
                continue
            raise Invalid(f'不认识“{pick}”')
        except Invalid as e:
            print(f'  未修改：{e}')


def edit(text: str, ask: Callable[[str], str]) -> str:
    """The loadout menu: `ask(prompt) -> str` until an empty answer. Returns the edited text (validated values only)."""
    while True:
        print(summary(text))
        pick = ask('输入编号编辑小队预设，v编号编辑载具挂载（如 v1），回车返回：').strip().lower()
        if not pick:
            return text
        try:
            if pick.isdigit() and 1 <= int(pick) <= len(SEATS):
                key = list(SEATS)[int(pick) - 1]
                print(HELP)
                value = ask(f'{LABELS[key]}（最多 {SEATS[key]} 人）= ').strip()
                if not value:
                    continue
                if value == '-':
                    text = put(text, 'SupportPreset_' + key, '')
                    continue
                text = put(text, 'SupportPreset_' + key, format_preset(parse_preset(value, SEATS[key])))
            elif pick.startswith('v') and pick[1:].isdigit() and 1 <= int(pick[1:]) <= len(VEHICLE_KEYS):
                text = edit_vehicle(text, list(VEHICLE_KEYS)[int(pick[1:]) - 1], ask)
            else:
                raise Invalid(f'不认识“{pick}”')
        except Invalid as e:
            print(f'  未修改：{e}')


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else rootcpk.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    ini = os.path.join(root, 'Mods', 'Plugins', 'EDF6VehicleCrew.ini')
    text = open(ini, encoding='utf-8-sig').read() if os.path.isfile(ini) else ''
    files = build(root, text, pending=read_pending(root))
    for path in install(root, files):
        print('写入', path)
    for name in clear_pending(root, files):
        print('已生成联机时缺少的文件', name)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
