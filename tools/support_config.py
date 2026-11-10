"""The map support's out-of-mission configuration, edited by the installer's menu 7 (tools/installer.py).

The plugin reads the same keys of EDF6VehicleCrew.ini [VehicleCrew] (src/support_config.h) at start and on every save of
the ini; this module only edits that text, validating each value the way the plugin does, so a value the menu writes
is never one the plugin would reject. tests/support_config_ini_test.py keeps the tables below equal to the C++ ones.
"""
from __future__ import annotations

import re

import calls
from i18n import is_zh, tr

SECTION = 'VehicleCrew'
# src/support_config.cpp kWeaponNames (the ini spelling, the label shown).
WEAPONS = (('rifle', '步枪'), ('flame', '火焰'), ('rocket', '火箭'), ('shotgun', '霰弹'), ('sniper', '狙击'))
WEAPONS_EN = {'rifle': 'rifle', 'flame': 'flamethrower', 'rocket': 'rocket', 'shotgun': 'shotgun', 'sniper': 'sniper'}
# The catalog keys in catalog order: tools/calls.py's flown calls (EDF6VC_CALL_* without the prefix; src/airstrike.cpp
# SupportAirCallKey), then src/support_dispatch.cpp SupportCallKey's own (the infantry, the ground pairs, the transports, the
# airdrops: its keys[]), then the strikes (calls.STRIKES: the barrages and the ultimate, src/calls.inc kStrikes), and last
# the sea rescue (never on the map's bar, a player in the sea asks for it; disabling it here stops the rescue).
AIR_KEYS = ('INTERCEPTOR', 'INTERCEPTOR_F', 'STRIKE', 'STRIKE_F', 'MULTIROLE', 'MULTIROLE_F', 'FIGHTER', 'FIGHTER_F',
            'CARRIER', 'CARRIER_F', 'HELI', 'HELI_F', 'BLAST_CARRIER', 'BLAST_CARRIER_F', 'DOLL_CARRIER', 'DOLL_CARRIER_F', 'SUB',
            'GUNSHIP', 'GUNSHIP_F', 'MEDIC_HELI', 'MEDIC_HELI_F',
            'SORTIE_HELI_STRAFE', 'SORTIE_HELI_HUNTER', 'SORTIE_HELI_CIRCLE', 'SORTIE_DIVE', 'SORTIE_MISSILE_PASS',
            'SORTIE_CARPET', 'SORTIE_POPUP', 'SORTIE_MISSILE_RAIN', 'SORTIE_HAMMER', 'SORTIE_WOLFPACK', 'SORTIE_GUIDED')
GROUND_KEYS = ('SQUAD', 'PLATOON', 'TANK_CREWED', 'TANK_DELIVERY', 'TRANSPORT_CREWED', 'TRANSPORT_DELIVERY', 'TRUCK_CREWED',
               'TRUCK_DELIVERY', 'SQUAD_HELI', 'PLATOON_HELI',
               'SQUAD_AIRDROP', 'PLATOON_AIRDROP', 'TANK_AIRDROP', 'TRANSPORT_AIRDROP', 'TRUCK_AIRDROP')
STRIKE_KEYS = tuple(c.id[len(calls.ID_PREFIX):] for c in calls.STRIKES)
UNIT_KEYS = AIR_KEYS + GROUND_KEYS + STRIKE_KEYS + ('RESCUE',)
LABELS = dict(zip(AIR_KEYS + GROUND_KEYS, (
    '截击机·守点', '截击机·跟随', '对地攻击机·守点', '对地攻击机·跟随', '多用途机·守点', '多用途机·跟随', '制空战斗机·守点',
    '制空战斗机·跟随', '无人机母舰·守点', '无人机母舰·跟随', '直升机·守点', '直升机·跟随', '自爆无人机母舰·守点',
    '自爆无人机母舰·跟随', '人偶无人机母舰·守点', '人偶无人机母舰·跟随', '潜水母舰', '炮艇机·守点', '炮艇机·跟随',
    '医疗直升机·守点', '医疗直升机·跟随',
    '出击·直升机扫射', '出击·导弹猎手', '出击·死亡回旋', '出击·俯冲扫射', '出击·导弹掠过', '出击·地毯轰炸', '出击·打了就跑',
    '出击·天顶导弹雨', '出击·锤与砧', '出击·狼群', '出击·引导打击', '步兵小队（4人）', '步兵大队（12人）', '坦克·有人', '坦克·空车交付',
    '装甲运兵车·有人', '装甲运兵车·空车交付', '民用轻卡·有人', '民用轻卡·空车交付',
    '直升机机降·小队（4人）', '直升机机降·大队（12人）', '运输机空降·小队（4人）', '运输机空降·大队（12人）',
    '直升机投送·坦克', '直升机投送·装甲运兵车', '直升机投送·民用轻卡')))
LABELS.update({k: calls.call_name(c, 'SC') for k, c in zip(STRIKE_KEYS, calls.STRIKES)})   # kStrikes' labels
LABELS['RESCUE'] = '海上救援直升机'
# The same in English, for an English installer (pylib/i18n.py): shown only, LABELS stays the C++ tables' copy.
LABELS_EN = dict(zip(AIR_KEYS + GROUND_KEYS, (
    'Interceptor (hold)', 'Interceptor (follow)', 'Ground attack jet (hold)', 'Ground attack jet (follow)',
    'Multirole jet (hold)', 'Multirole jet (follow)', 'Air superiority fighter (hold)', 'Air superiority fighter (follow)',
    'Drone carrier (hold)', 'Drone carrier (follow)', 'Helicopter (hold)', 'Helicopter (follow)',
    'Suicide drone carrier (hold)', 'Suicide drone carrier (follow)', 'Doll drone carrier (hold)',
    'Doll drone carrier (follow)', 'Submarine carrier', 'Gunship (hold)', 'Gunship (follow)',
    'Medical helicopter (hold)', 'Medical helicopter (follow)',
    'Sortie: helicopter strafe', 'Sortie: missile hunter', 'Sortie: death circle', 'Sortie: dive strafe',
    'Sortie: missile pass', 'Sortie: carpet bombing', 'Sortie: hit and run', 'Sortie: zenith missile rain',
    'Sortie: hammer and anvil', 'Sortie: wolf pack', 'Sortie: guided strike', 'Infantry squad (4)',
    'Infantry platoon (12)', 'Tank, crewed', 'Tank, delivered empty', 'APC, crewed', 'APC, delivered empty',
    'Light truck, crewed', 'Light truck, delivered empty', 'Helicopter insertion: squad (4)',
    'Helicopter insertion: platoon (12)', 'Airdrop: squad (4)', 'Airdrop: platoon (12)', 'Helicopter delivery: tank',
    'Helicopter delivery: APC', 'Helicopter delivery: light truck')))
LABELS_EN.update({k: calls.call_name(c, 'EN') for k, c in zip(STRIKE_KEYS, calls.STRIKES)})
LABELS_EN['RESCUE'] = 'Sea rescue helicopter'
WEAPON_KEYS = (('SupportSquadWeapon', '4 人小队队员'), ('SupportSquadLeaderWeapon', '所有小队队长'),
               ('SupportVehicleCrewWeapon', '车辆机组'), ('SupportAircraftCrewWeapon', '飞机机组'))
WEAPON_KEYS_EN = {'SupportSquadWeapon': '4-soldier squad members', 'SupportSquadLeaderWeapon': 'every squad leader',
                  'SupportVehicleCrewWeapon': 'vehicle crews', 'SupportAircraftCrewWeapon': 'aircraft crews'}


def label(key: str) -> str:
    """A unit's name in the installer's language."""
    return LABELS[key] if is_zh() else LABELS_EN[key]


def weapon_label(name: str) -> str:
    return dict(WEAPONS)[name] if is_zh() else WEAPONS_EN[name]


def weapon_key_label(key: str) -> str:
    return dict(WEAPON_KEYS)[key] if is_zh() else WEAPON_KEYS_EN[key]
DEFAULTS = {'SupportDisabled': '', 'SupportSquadWeapon': 'rifle', 'SupportSquadLeaderWeapon': 'rifle',
            'SupportPlatoonWeapons': 'rifle,rocket,sniper', 'SupportVehicleCrewWeapon': 'rifle', 'SupportAircraftCrewWeapon': 'rifle'}
AIRCRAFT_MOST = 8   # src/support_config.h kSupportAircraftMost


class Invalid(ValueError):
    """A value the plugin would reject; the message names it."""


def weapon(text: str) -> str:
    """The ini spelling of a weapon given by its name, label or number (1-5)."""
    t = text.strip()
    for i, (name, label) in enumerate(WEAPONS, 1):
        if t.lower() == name or t == label or t.lower() == WEAPONS_EN[name] or t == str(i):
            return name
    raise Invalid(tr(f'未知武器“{text}”：可选 ', f'unknown weapon "{text}": one of ')
                  + ' / '.join(f'{n} ({weapon_label(n)})' for n, _ in WEAPONS))


def platoon(text: str) -> str:
    parts = [p for p in re.split(r'[,，;、\s]+', text.strip()) if p]
    if len(parts) != 3:
        raise Invalid(tr(f'大队武器需要正好三个（每个小队一个），收到 {len(parts)} 个',
                         f'a platoon needs exactly three weapons (one per squad), got {len(parts)}'))
    return ','.join(weapon(p) for p in parts)


def units(text: str) -> str:
    parts = [p.upper() for p in re.split(r'[,，;、\s]+', text.strip()) if p]
    bad = [p for p in parts if p not in UNIT_KEYS]
    if bad:
        raise Invalid(tr('未知单位：', 'unknown units: ') + ','.join(bad))
    return ','.join(dict.fromkeys(parts))


def aircraft_count(text: str) -> str:
    t = text.strip()
    if not t.isdigit() or int(t) > AIRCRAFT_MOST:
        raise Invalid(tr(f'架数“{text}”无效：0（该呼叫默认）到 {AIRCRAFT_MOST}',
                         f'aircraft count "{text}" is not valid: 0 (the call\'s default) to {AIRCRAFT_MOST}'))
    return str(int(t))


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
    """The value as the plugin reads it (GetPrivateProfileString): trimmed, no inline comment taken off (';' is one of
    SupportDisabled's separators there)."""
    i = _line(text.splitlines(), key)
    if i is None:
        return DEFAULTS.get(key, '')
    return text.splitlines()[i].split('=', 1)[1].strip()


def put(text: str, key: str, value: str) -> str:
    """`text` with `key` set to `value` in [VehicleCrew] (replaced where it is, else appended to the section)."""
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines()
    i = _line(lines, key)
    if i is not None:
        lines[i] = f'{key}={value}'
    else:
        head = next((j for j, x in enumerate(lines) if re.match(rf'^\s*\[{SECTION}\]', x, re.I)), None)
        if head is None:
            lines += [f'[{SECTION}]', f'{key}={value}']
        else:
            end = next((j for j in range(head + 1, len(lines)) if re.match(r'^\s*\[', lines[j])), len(lines))
            while end > head + 1 and not lines[end - 1].strip():
                end -= 1
            lines.insert(end, f'{key}={value}')
    return nl.join(lines) + nl


def disabled(text: str) -> list[str]:
    """The units the plugin disables (src/support_config.cpp Tokens): separated by , ， ; 、 and trimmed; spaces inside
    a token make it an unknown unit there, so not one here."""
    parts = (p.strip().upper() for p in re.split(r'[,，;、]', get(text, 'SupportDisabled')))
    return [k for k in parts if k in UNIT_KEYS]


def summary(text: str) -> str:
    off = set(disabled(text))
    rows = [f'  {i + 1:2d}. [{tr("关", "off") if k in off else tr("开", "on")}] {label(k)} ({k})' for i, k in enumerate(UNIT_KEYS)]
    rows.append(tr('  武器：', '  Weapons: ') + tr('，', ', ').join(f'{weapon_key_label(key)} {get(text, key)}' for key, _ in WEAPON_KEYS) +
                tr(f'，大队三个小队 {get(text, "SupportPlatoonWeapons")}', f', the platoon\'s three squads {get(text, "SupportPlatoonWeapons")}'))
    counts = [f'{k}={get(text, "SupportAircraftCount_" + k)}' for k in AIR_KEYS if get(text, 'SupportAircraftCount_' + k) not in ('', '0')]
    rows.append(tr('  架数：', '  Aircraft: ') + (tr('，', ', ').join(counts) if counts else tr('全部为该呼叫默认', 'each call\'s default')))
    return '\n'.join(rows)


def edit(text: str, ask) -> str:
    """The menu: `ask(prompt) -> str` until an empty answer. Returns the edited text (validated values only)."""
    while True:
        print(summary(text))
        pick = ask(tr('输入编号切换开/关；w 改兵员武器；p 改大队三个小队武器；c 改飞机架数；'
                      'l 战前配置（编组 / 每人兵种 / 颜色 / 坦克与战机挂载，主菜单 8 直达）；r 恢复默认；回车保存并返回：',
                      'Enter a number to turn it on / off; w crew weapons; p the platoon\'s three squads\' weapons; c aircraft '
                      'counts; l pre-battle loadouts (squads / each soldier\'s class / colours / tank and jet stores; main menu 8 '
                      'goes straight there); r defaults; Enter to save and go back: ')).strip().lower()
        if not pick:
            return text
        try:
            if pick.isdigit() and 1 <= int(pick) <= len(UNIT_KEYS):
                key = UNIT_KEYS[int(pick) - 1]
                off = disabled(text)
                off = [k for k in off if k != key] if key in off else off + [key]
                text = put(text, 'SupportDisabled', units(','.join(off)))
            elif pick == 'w':
                for i, (key, _) in enumerate(WEAPON_KEYS, 1):
                    print(f'  {i}. {weapon_key_label(key)} ({key}) = {get(text, key)}')
                which = ask(tr('改哪一项（编号）：', 'Which one (number): ')).strip()
                if not which.isdigit() or not 1 <= int(which) <= len(WEAPON_KEYS):
                    raise Invalid(tr(f'没有第 {which} 项', f'there is no item {which}'))
                key = WEAPON_KEYS[int(which) - 1][0]
                text = put(text, key, weapon(ask(tr('武器（', 'Weapon (') + ' / '.join(f'{i}={n}' for i, (n, _) in enumerate(WEAPONS, 1))
                                                 + tr('）：', '): '))))
            elif pick == 'p':
                text = put(text, 'SupportPlatoonWeapons', platoon(ask(tr('三个小队的武器，逗号分隔（如 rifle,rocket,sniper）：',
                                                                         'The three squads\' weapons, comma separated (like rifle,rocket,sniper): '))))
            elif pick == 'c':
                key = ask(tr('哪个空中单位（如 FIGHTER、HELI_F）：', 'Which air unit (like FIGHTER, HELI_F): ')).strip().upper()
                if key not in AIR_KEYS:
                    raise Invalid(tr(f'“{key}”不是空中单位', f'"{key}" is not an air unit'))
                text = put(text, 'SupportAircraftCount_' + key, aircraft_count(ask(tr(f'架数（0-{AIRCRAFT_MOST}，0 = 默认）：',
                                                                                     f'Aircraft (0-{AIRCRAFT_MOST}, 0 = default): '))))
            elif pick == 'l':   # the out-of-game loadouts (tools/support_loadout.py, src/support_loadout.h)
                import support_loadout
                text = support_loadout.edit(text, ask)
            elif pick == 'r':
                for key, value in DEFAULTS.items():
                    text = put(text, key, value)
                for key in AIR_KEYS:
                    if _line(text.splitlines(), 'SupportAircraftCount_' + key) is not None:
                        text = put(text, 'SupportAircraftCount_' + key, '0')
            else:
                raise Invalid(tr(f'不认识“{pick}”', f'"{pick}" is not a choice'))
        except Invalid as e:
            print(tr(f'  未修改：{e}', f'  Not changed: {e}'))
