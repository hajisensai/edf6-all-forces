"""The map support's out-of-mission configuration, edited by the installer's menu 7 (tools/installer.py).

The plugin reads the same keys of EDF6VehicleCrew.ini [VehicleCrew] (src/support_config.h) at start and on every save of
the ini; this module only edits that text, validating each value the way the plugin does, so a value the menu writes
is never one the plugin would reject. tests/support_config_ini_test.py keeps the tables below equal to the C++ ones.
"""
from __future__ import annotations

import re

SECTION = 'VehicleCrew'
# src/support_config.cpp kWeaponNames (the ini spelling, the label shown).
WEAPONS = (('rifle', '步枪'), ('flame', '火焰'), ('rocket', '火箭'), ('shotgun', '霰弹'), ('sniper', '狙击'))
# The catalog keys in catalog order: tools/calls.py's flown calls (EDF6VC_CALL_* without the prefix; src/airstrike.cpp
# SupportAirCallKey), then src/support_dispatch.cpp SupportCallKey's own.
AIR_KEYS = ('INTERCEPTOR', 'INTERCEPTOR_F', 'STRIKE', 'STRIKE_F', 'MULTIROLE', 'MULTIROLE_F', 'FIGHTER', 'FIGHTER_F',
            'CARRIER', 'CARRIER_F', 'HELI', 'HELI_F', 'BLAST_CARRIER', 'BLAST_CARRIER_F', 'DOLL_CARRIER', 'DOLL_CARRIER_F', 'SUB',
            'GUNSHIP', 'GUNSHIP_F', 'MEDIC_HELI', 'MEDIC_HELI_F')
GROUND_KEYS = ('SQUAD', 'PLATOON', 'TANK_CREWED', 'TANK_DELIVERY', 'TRANSPORT_CREWED', 'TRANSPORT_DELIVERY', 'TRUCK_CREWED',
               'TRUCK_DELIVERY')
UNIT_KEYS = AIR_KEYS + GROUND_KEYS
LABELS = dict(zip(UNIT_KEYS, (
    '截击机·守点', '截击机·跟随', '对地攻击机·守点', '对地攻击机·跟随', '多用途机·守点', '多用途机·跟随', '制空战斗机·守点',
    '制空战斗机·跟随', '无人机母舰·守点', '无人机母舰·跟随', '直升机·守点', '直升机·跟随', '自爆无人机母舰·守点',
    '自爆无人机母舰·跟随', '人偶无人机母舰·守点', '人偶无人机母舰·跟随', '潜水母舰', '炮艇机·守点', '炮艇机·跟随',
    '医疗直升机·守点', '医疗直升机·跟随', '步兵小队（4人）', '步兵大队（12人）', '坦克·有人', '坦克·空车交付',
    '装甲运兵车·有人', '装甲运兵车·空车交付', '民用轻卡·有人', '民用轻卡·空车交付')))
WEAPON_KEYS = (('SupportSquadWeapon', '4 人小队队员'), ('SupportSquadLeaderWeapon', '所有小队队长'),
               ('SupportVehicleCrewWeapon', '车辆机组'), ('SupportAircraftCrewWeapon', '飞机机组'))
DEFAULTS = {'SupportDisabled': '', 'SupportSquadWeapon': 'rifle', 'SupportSquadLeaderWeapon': 'rifle',
            'SupportPlatoonWeapons': 'rifle,rocket,sniper', 'SupportVehicleCrewWeapon': 'rifle', 'SupportAircraftCrewWeapon': 'rifle'}
AIRCRAFT_MOST = 8   # src/support_config.h kSupportAircraftMost


class Invalid(ValueError):
    """A value the plugin would reject; the message names it."""


def weapon(text: str) -> str:
    """The ini spelling of a weapon given by its name, label or number (1-5)."""
    t = text.strip()
    for i, (name, label) in enumerate(WEAPONS, 1):
        if t.lower() == name or t == label or t == str(i):
            return name
    raise Invalid(f'未知武器“{text}”：可选 ' + ' / '.join(f'{n}（{l}）' for n, l in WEAPONS))


def platoon(text: str) -> str:
    parts = [p for p in re.split(r'[,，;、\s]+', text.strip()) if p]
    if len(parts) != 3:
        raise Invalid(f'大队武器需要正好三个（每个小队一个），收到 {len(parts)} 个')
    return ','.join(weapon(p) for p in parts)


def units(text: str) -> str:
    parts = [p.upper() for p in re.split(r'[,，;、\s]+', text.strip()) if p]
    bad = [p for p in parts if p not in UNIT_KEYS]
    if bad:
        raise Invalid('未知单位：' + ','.join(bad))
    return ','.join(dict.fromkeys(parts))


def aircraft_count(text: str) -> str:
    t = text.strip()
    if not t.isdigit() or int(t) > AIRCRAFT_MOST:
        raise Invalid(f'架数“{text}”无效：0（该呼叫默认）到 {AIRCRAFT_MOST}')
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
    i = _line(text.splitlines(), key)
    if i is None:
        return DEFAULTS.get(key, '')
    return text.splitlines()[i].split('=', 1)[1].split(';', 1)[0].strip()


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
    return [k for k in (p.upper() for p in re.split(r'[,，;、\s]+', get(text, 'SupportDisabled')) if p) if k in UNIT_KEYS]


def summary(text: str) -> str:
    off = set(disabled(text))
    rows = [f'  {i + 1:2d}. [{"关" if k in off else "开"}] {LABELS[k]}（{k}）' for i, k in enumerate(UNIT_KEYS)]
    rows.append('  武器：' + '，'.join(f'{label} {get(text, key)}' for key, label in WEAPON_KEYS) +
                f'，大队三个小队 {get(text, "SupportPlatoonWeapons")}')
    counts = [f'{k}={get(text, "SupportAircraftCount_" + k)}' for k in AIR_KEYS if get(text, 'SupportAircraftCount_' + k) not in ('', '0')]
    rows.append('  架数：' + ('，'.join(counts) if counts else '全部为该呼叫默认'))
    return '\n'.join(rows)


def edit(text: str, ask) -> str:
    """The menu: `ask(prompt) -> str` until an empty answer. Returns the edited text (validated values only)."""
    while True:
        print(summary(text))
        pick = ask('输入编号切换开/关；w 改兵员武器；p 改大队三个小队武器；c 改飞机架数；r 恢复默认；回车保存并返回：').strip().lower()
        if not pick:
            return text
        try:
            if pick.isdigit() and 1 <= int(pick) <= len(UNIT_KEYS):
                key = UNIT_KEYS[int(pick) - 1]
                off = disabled(text)
                off = [k for k in off if k != key] if key in off else off + [key]
                text = put(text, 'SupportDisabled', units(','.join(off)))
            elif pick == 'w':
                for i, (key, label) in enumerate(WEAPON_KEYS, 1):
                    print(f'  {i}. {label}（{key}）= {get(text, key)}')
                which = ask('改哪一项（编号）：').strip()
                if not which.isdigit() or not 1 <= int(which) <= len(WEAPON_KEYS):
                    raise Invalid(f'没有第 {which} 项')
                key = WEAPON_KEYS[int(which) - 1][0]
                text = put(text, key, weapon(ask('武器（' + ' / '.join(f'{i}={n}' for i, (n, _) in enumerate(WEAPONS, 1)) + '）：')))
            elif pick == 'p':
                text = put(text, 'SupportPlatoonWeapons', platoon(ask('三个小队的武器，逗号分隔（如 rifle,rocket,sniper）：')))
            elif pick == 'c':
                key = ask('哪个空中单位（如 FIGHTER、HELI_F）：').strip().upper()
                if key not in AIR_KEYS:
                    raise Invalid(f'“{key}”不是空中单位')
                text = put(text, 'SupportAircraftCount_' + key, aircraft_count(ask(f'架数（0-{AIRCRAFT_MOST}，0 = 默认）：')))
            elif pick == 'r':
                for key, value in DEFAULTS.items():
                    text = put(text, key, value)
                for key in AIR_KEYS:
                    if _line(text.splitlines(), 'SupportAircraftCount_' + key) is not None:
                        text = put(text, 'SupportAircraftCount_' + key, '0')
            else:
                raise Invalid(f'不认识“{pick}”')
        except Invalid as e:
            print(f'  未修改：{e}')
