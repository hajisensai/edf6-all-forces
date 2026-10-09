"""Out-of-game support loadouts: the editor (installer menu 7 -> l) and the files the plugin needs for them.

The one source of truth is EDF6VehicleCrew.ini [VehicleCrew] (src/support_loadout.h has the syntax; the plugin rereads
the ini on every save):
  SupportPreset_<KEY>=lance@1E3A8A*4,cannon*4     a seated entry's soldiers in seat order, colours, counts
  SupportTankRounds=AP  /  AP,HE,HE  /  AP:1,HE:1  the support tanks' main gun: all, a per-tank list, or a ratio
This module validates every value the way the plugin does (tests/support_loadout_ini_test.py keeps the tables equal to
the C++ ones), so a value the editor writes is never one the plugin would refuse.

Two kinds of generated files, written into <game>/Mods/OBJECT and recorded in the ledger (pylib/ledger.py, owner
`loadout`), each built from the game's own files (Root.cpk only read):
  EDF6VC_NPC_<KIND>[_L]_<PRIMARY|X>_<SECONDARY|X>.SGO   a coloured soldier: the stock AI template of that kind (its
      _LEADER for a leader) with its soldier_color entries recoloured (change_color0 = primary, change_color1 =
      secondary; a face's own entry is kept). Nothing else changes: the same class, model, CAS, AI weapon.
  EDF6VC_SUPPORT_TANK_AP.SGO   the support tank V505_TANK_MISSION (the Mods copy other tools made, else the stock) with
      the stock Blacker A1's gun mount (WEAPON/EWEAPON419: v_505tank_cannon01s, the 90 mm smooth-bore) in place of
      its Blacker E1 howitzer, and that gun added to its preloaded resources.
Only coloured soldiers in the presets get a file; files no longer needed are released.

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
from modfiles import sha256, sha256_file  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

OWNER = 'loadout'   # pylib/ledger.py
SECTION = 'VehicleCrew'
MOST = 12           # src/support_call.h kSupportLoadoutMost
LOOKS_MOST = 15     # src/support_call.h kSupportLooksMost: distinct coloured soldiers a mission takes
STOCK = -1          # src/support_loadout.h kStockColour

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
# src/support_loadout.h kTankRoundNames.
ROUNDS = (('HE', '榴弹（原版）'), ('AP', '穿甲'))
ROUND_MIX_MOST = 16     # kRoundMixMost
ROUND_WEIGHT_MOST = 1000
# The catalog entries a player can fill (src/support_dispatch.cpp SupportCallSeats) and their seats.
SEATS: dict[str, int] = {'SQUAD': 12, 'PLATOON': 12, 'TRANSPORT_CREWED': 4, 'TRUCK_CREWED': 4,
                         'SQUAD_HELI': 12, 'PLATOON_HELI': 12, 'SQUAD_AIRDROP': 12, 'PLATOON_AIRDROP': 12}
LABELS = {'SQUAD': '步兵小队', 'PLATOON': '步兵大队', 'TRANSPORT_CREWED': '装甲运兵车·有人（乘客）',
          'TRUCK_CREWED': '民用轻卡·有人（乘客）', 'SQUAD_HELI': '直升机机降·小队', 'PLATOON_HELI': '直升机机降·大队',
          'SQUAD_AIRDROP': '运输机空降·小队', 'PLATOON_AIRDROP': '运输机空降·大队'}

TANK_FILE = 'EDF6VC_SUPPORT_TANK_AP.SGO'        # kSupportTankApFile
TANK_STOCK = 'V505_TANK_MISSION.SGO'            # support_spawn.h kSupportVehicles[tank]
AP_CALL = 'EWEAPON419.SGO'                      # the stock Blacker A1 request: the AP gun's mount
AP_GUN = 'app:/weapon/v_505tank_cannon01s.sgo'
HE_GUN = 'app:/weapon/v_505tank_cannon01.sgo'


class Invalid(ValueError):
    """A value the plugin would refuse; the message names it."""


Look = tuple[int, int]   # (primary, secondary), 0xRRGGBB or STOCK
Soldier = tuple[str, Look]


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


def parse_rounds(text: str) -> tuple[bool, list[tuple[str, int]]]:
    """A SupportTankRounds value as the plugin reads it (ParseRoundMix): (ratio, [(round, weight)])."""
    names = [r for r, _ in ROUNDS]
    entries: list[tuple[str, int]] = []
    weighted = plain = 0
    for token in _tokens(text):
        head, colon, weight = token.partition(':')
        name, star, n = head.partition('*')
        r = name.strip().upper()
        if r not in names:
            raise Invalid(f'未知弹种（AP / HE）：{name.strip()}')
        if colon:
            w = weight.strip()
            w = w[:-1] if w.endswith('%') else w
            if star:
                raise Invalid(f'比例应为 弹种:1 到 {ROUND_WEIGHT_MOST}：{token}')
            if len(entries) >= ROUND_MIX_MOST:
                raise Invalid(f'弹种项过多（最多 {ROUND_MIX_MOST} 项）')
            entries.append((r, _count(w, ROUND_WEIGHT_MOST, '比例 弹种:')))
            weighted += 1
            continue
        count = _count(n, ROUND_MIX_MOST, '重复次数 *') if star else 1
        if len(entries) + count > ROUND_MIX_MOST:
            raise Invalid(f'逐车列表过长（一轮最多 {ROUND_MIX_MOST} 辆）')
        entries += [(r, 1)] * count
        plain += 1
    if weighted and plain:
        raise Invalid('不能混用逐车列表与比例（要么 AP,HE,HE，要么 AP:1,HE:2）')
    return weighted > 0, entries


def pick_round(ratio: bool, entries: list[tuple[str, int]], k: int) -> str:
    """The k-th tank's round (0-based), as src/support_loadout.h PickRound."""
    if not entries:
        return 'HE'
    if not ratio:
        return entries[k % len(entries)][0]
    total = sum(w for _, w in entries)
    given = [0] * len(entries)
    pick = 0
    for step in range(k + 1):
        deficits = [(step + 1) * w - total * g for (_, w), g in zip(entries, given)]
        pick = max(range(len(entries)), key=lambda i: (deficits[i], -i))
        given[pick] += 1
    return entries[pick][0]


def look_file(k: str, leader: bool, look: Look) -> str:
    """src/support_loadout.h SupportLookFile."""
    return f'EDF6VC_NPC_{k.upper()}{"_L" if leader else ""}_{_colour_text(look[0])}_{_colour_text(look[1])}.SGO'


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


def looks_wanted(text: str) -> list[tuple[str, bool, Look]]:
    """The distinct coloured soldiers of the ini's presets, as src/support_dispatch.cpp PreloadSupportLooks walks them."""
    out: list[tuple[str, bool, Look]] = []
    for soldiers in presets(text).values():
        for i, (k, look) in enumerate(soldiers):
            entry = (k, i % 4 == 0, look)
            if look != (STOCK, STOCK) and entry not in out:
                out.append(entry)
    return out


# ------------------------------------------------------------------------------------------------ generated files


def _source(root: str | None, game: rootcpk.Game, name: str) -> bytes:
    """OBJECT/`name` as the game would load it: the Mods copy another tool made, else Root.cpk's."""
    loose = os.path.join(root, 'Mods', 'OBJECT', name) if root else ''
    if loose and os.path.isfile(loose):
        with open(loose, 'rb') as f:
            return f.read()
    return game.read('OBJECT', name)


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


def soldier_files(game: rootcpk.Game, wanted: list[tuple[str, bool, Look]]) -> dict[str, bytes]:
    templates = {k: t for k, _, t in KINDS}
    out = {}
    for k, leader, look in wanted[:LOOKS_MOST]:
        out['OBJECT/' + look_file(k, leader, look)] = recolour(game.read('OBJECT', templates[k] + ('_LEADER' if leader else '') + '.SGO'), look)
    return out


def ap_mount(game: rootcpk.Game) -> list:
    """The stock Blacker A1's gun mount ([weapon, recoil, turret]), from its request's vehicle setup."""
    call = sgo.load(data=game.read('WEAPON', AP_CALL))
    mount = call['Ammo_CustomParameter'][4][3][2][0]
    if not (isinstance(mount, list) and isinstance(mount[0], str) and mount[0].lower() == AP_GUN):
        raise ValueError(f'{AP_CALL} 不是原版布莱克 A1（主炮不是 {AP_GUN}）')
    return mount


def _as_sgo(v: object) -> sgo.Value:
    if isinstance(v, list):
        return [_as_sgo(c) for c in v]
    if isinstance(v, float):
        return sgo.Float(struct.pack('<f', v))
    return v


def tank_file(root: str | None, game: rootcpk.Game) -> bytes:
    """EDF6VC_SUPPORT_TANK_AP.SGO: the support tank with the AP gun (classic SGO, written as the stock layout)."""
    data = _source(root, game, TANK_STOCK)
    version, members = sgo.read(data)
    mount = _as_sgo(ap_mount(game))
    swapped = 0
    for key in ('mission_setup', 'vehicle_setup'):
        guns = members.get(key)
        if not isinstance(guns, list) or len(guns) < 3 or not isinstance(guns[2], list) or len(guns[2]) != 1:
            raise ValueError(f'{TANK_STOCK} 的 {key} 不是一门主炮的布局')
        if not str(guns[2][0][0]).lower() == HE_GUN:
            raise ValueError(f'{TANK_STOCK} 的 {key} 主炮不是 {HE_GUN}（被别的 MOD 改过？）')
        guns[2][0] = mount
        swapped += 1
    resource = members.get('resource')
    if isinstance(resource, list) and AP_GUN not in [str(r).lower() for r in resource]:
        resource.append(AP_GUN)
    out = sgo.write_depth_first(version, members)
    check_tank(out, data)
    return out


def check_tank(made: bytes, base: bytes) -> None:
    """The AP tank differs from its base only in the two mounts (the AP gun) and the AP gun added to `resource`."""
    a, b = sgo.load(data=made), sgo.load(data=base)
    assert set(a) == set(b), 'same members'
    for key in a:
        if key in ('mission_setup', 'vehicle_setup'):
            assert a[key][:2] == b[key][:2] and a[key][3:] == b[key][3:], key
            assert len(a[key][2]) == 1 and a[key][2][0][0].lower() == AP_GUN, key
        elif key == 'resource':
            assert [r for r in a[key] if r.lower() != AP_GUN] == [r for r in b[key] if r.lower() != AP_GUN], key
        else:
            assert a[key] == b[key], key


def build(root: str | None, ini_text: str, game: rootcpk.Game | None = None) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}: the ini's coloured soldiers and the AP tank."""
    game = game or (rootcpk.Game(root) if root else rootcpk.default())
    return {**soldier_files(game, looks_wanted(ini_text)), 'OBJECT/' + TANK_FILE: tank_file(root, game)}


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's, each one only when it is not already exactly that (every install and every
    menu-7 save rebuilds them: an unchanged file is not rewritten); what it wrote before and does not now is released.
    Returns the paths written."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = []
    for rel, data in files.items():
        if ledger.key(rel) in before and not led.changed(rel) and sha256_file(led.disk(rel)) == sha256(data):
            continue
        paths.append(led.put(OWNER, rel, data))
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept changed)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


# ------------------------------------------------------------------------------------------------ the editor


def put(text: str, key: str, value: str) -> str:
    import support_config
    return support_config.put(text, key, value)


def summary(text: str) -> str:
    rows = ['  支援预设（游戏外编辑，呼叫时套用；空 = 该单位自己的兵员）：']
    for i, key in enumerate(SEATS, 1):
        value = get(text, 'SupportPreset_' + key)
        try:
            soldiers = parse_preset(value, SEATS[key])
            shown = describe(soldiers) if soldiers else '（默认兵员）'
        except Invalid as e:
            shown = f'（无效，插件按默认兵员：{e}）'
        rows.append(f'  {i:2d}. {LABELS[key]}（{key}，{SEATS[key]} 座）：{shown}')
    try:
        ratio, entries = parse_rounds(get(text, 'SupportTankRounds'))
        tanks = '，'.join(pick_round(ratio, entries, k) for k in range(8))
        rounds = (get(text, 'SupportTankRounds') or 'HE（原版）') + f'  → 前 8 辆：{tanks}'
    except Invalid as e:
        rounds = f'（无效，插件全部 HE：{e}）'
    rows.append(f'   t. 支援坦克主炮（SupportTankRounds）：{rounds}')
    return '\n'.join(rows)


def describe(soldiers: list[Soldier]) -> str:
    labels = {n: label for n, label, _ in KINDS}
    parts = []
    for i in range(0, len(soldiers), 4):
        squad = soldiers[i:i + 4]
        parts.append('[' + '、'.join(labels[k] + ('' if look == (STOCK, STOCK) else f'@{_colour_text(look[0])}:{_colour_text(look[1])}')
                                     for k, look in squad) + ']')
    return ' '.join(parts)


HELP = ('  写法：兵种[@主色[:副色]][*人数]，逗号分隔，按座位顺序，每 4 人第一位是队长。\n'
        '  兵种：' + ' '.join(f'{n}={label}' for n, label, _ in KINDS) + '\n'
        '  颜色：6 位十六进制 RRGGBB（如 1E3A8A），X = 保留原色；例：lance@1E3A8A*4,cannon*4,rifle@X:FFFFFF*4\n'
        '  直接回车 = 不改；输入 - = 清除（恢复该单位默认兵员）。')
ROUND_HELP = ('  写法：AP（全部穿甲） / HE（全部榴弹，原版） / AP,HE,HE（逐车列表，循环） / AP:1,HE:1（按比例交替，一半一半）\n'
              '  每次呼叫来一辆坦克，按本关第几辆分配；直接回车 = 不改；- = 清除（全部 HE）。')


def edit(text: str, ask: Callable[[str], str]) -> str:
    """The loadout menu: `ask(prompt) -> str` until an empty answer. Returns the edited text (validated values only)."""
    while True:
        print(summary(text))
        pick = ask('输入编号编辑该单位的预设，t 编辑坦克主炮，回车返回：').strip().lower()
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
            elif pick == 't':
                print(ROUND_HELP)
                value = ask('SupportTankRounds = ').strip()
                if not value:
                    continue
                if value == '-':
                    text = put(text, 'SupportTankRounds', '')
                    continue
                parse_rounds(value)
                text = put(text, 'SupportTankRounds', ','.join(_tokens(value)).upper())
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
    for path in install(root, build(root, text)):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
