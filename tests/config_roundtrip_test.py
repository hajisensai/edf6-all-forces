"""出厂 ini 往返（tools/config_keys.py → tools/gen_config.py → EDF6VehicleCrew.ini）：把生成的 ini 按插件的读法
（[VehicleCrew] 段、key=value、; 开头是注释）解析回来，每个出厂键的值都等于表里的默认值，也等于 crew.h Config
成员（src/config.inc EDF6_CONFIG_FIELDS）的初值；不出厂的键、已退役的键不出现在 ini 里；提交的 config.inc 与 ini
都是生成器现在会写的内容。纯文本，不需要游戏文件。"""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import config_keys as ck  # noqa: E402
import gen_config  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def parse(text: str) -> dict[str, list[str]]:
    """{key: [value, ...]} of the [VehicleCrew] section, the way GetPrivateProfileString sees it (keys case-blind)."""
    section, out = None, {}
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith(';'):
            continue
        m = re.fullmatch(r'\[(.+)\]', s)
        if m:
            section = m.group(1)
            continue
        check('=' in s, f'ini line without "=": {line!r}')
        k, v = s.split('=', 1)
        if section == ck.SECTION:
            out.setdefault(k.strip().lower(), []).append(v.strip())
    return out


def number(key: ck.Key, text: str):
    """The value the plugin reads from text (ReadBool / ReadFloat / ReadInt)."""
    if key.type == ck.BOOL:
        check(text in ('0', '1'), f'{key.name}: bool {text!r}')
        return text == '1'
    if key.type == ck.FLOAT:
        return float(text)
    return int(text, 0)


def member(key: ck.Key):
    """The Config member's initialiser in config.inc as a number."""
    m = re.search(rf'^    \w+ {key.field}=([^;]+);', inc, re.M)
    check(m is not None, f'{key.name}: no member {key.field} in src/config.inc')
    text = m.group(1)
    if key.type == ck.BOOL:
        return text == 'true'
    if key.type == ck.FLOAT:
        return float(text.rstrip('f'))
    return int(text, 0)


check(gen_config.main(['--check']) == 0, 'src/config.inc / EDF6VehicleCrew.ini are stale: python tools/gen_config.py')
outputs = dict(gen_config.outputs())
inc = outputs[gen_config.INC]
with open(gen_config.INI, encoding='utf-8-sig') as f:
    committed = f.read()
for name, text in (('generated', outputs[gen_config.INI]), ('committed', committed)):
    ini = parse(text)
    shipped = [k for k in ck.KEYS if k.shipped]
    for k in shipped:
        values = ini.get(k.name.lower(), [])
        check(len(values) == 1, f'{name} ini: {k.name} {len(values)} times')
        check(values[0] == ck.ini_value(k), f'{name} ini: {k.name}={values[0]}, table {ck.ini_value(k)}')
        if k.field and k.type != ck.TEXT and k.cxx is None:
            check(number(k, values[0]) == member(k), f'{k.name}: ini {values[0]} but Config {k.field} starts elsewhere')
    for k in ck.KEYS:
        if not k.shipped:
            check(k.name.lower() not in ini, f'{name} ini ships {k.name}, which the table keeps out')
    for old, _ in ck.RETIRED:
        check(old.lower() not in ini, f'{name} ini still ships retired {old}')
    check(set(ini) == {k.name.lower() for k in shipped}, f'{name} ini: keys outside the table')
print(f'config round trip: {sum(1 for k in ck.KEYS if k.shipped)} shipped keys parse back to the table defaults; '
      f'{checks} checks')
