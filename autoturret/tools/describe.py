"""Rewrite the in-game descriptions of the vehicles build.py changes.

Each row is regenerated from the stock text: the gun stat lines get the built guns' damage, range
and blast, the durability line the built call's, and a note says what the turret now does. The rows
are then written into the WEAPONTEXT tables already installed in Mods (stock if none), at the
same index, so other mods' rows stay as they are and rerunning gives the same result.

The text tables are index-aligned with WEAPONTABLE: a table whose row count differs from the weapon
table's is refused (rows would land on other weapons). build_texts also hands back each rewritten row
as it was before and as written, so build.py uninstall can put the original back.
"""
from __future__ import annotations

import os
import re
from dataclasses import dataclass
from functools import lru_cache

import dsgo
import gamefs
import vehicle_setup
from dsgo import Node

LANGS = ('JA', 'EN', 'CN', 'KR', 'SC')
STATS = re.compile(r'<font color=%dq%#00ffff%dq%>.*?</font>')
NUMBER = re.compile(r'(?<![#\d])\d+(?:\.\d+)?')   # not the digits of the #00ffff colour
NOTE_FONT = '<font color=%dq%#ffc040%dq%>{}</font>'
# The stock Bohr's stat line: the only one with a blast field, so its wording is the template.
BLAST_ROW = 'MPACK_B_WEAPON025.SGO'

NOTES = {
    'flak': {
        'JA': '【防空MOD】炸裂弾に換装。近接・時限・着発信管で目標の近くで炸裂する。砲塔は自動で照準し、空中の敵を優先する。照準スティックを倒している間は手動で照準できる。',
        'EN': '[Anti-Air mod] Fires exploding flak with proximity, time and contact fuses. The turret aims itself, air targets first; hold the aim stick to aim by hand.',
        'CN': '【防空MOD】改用炸裂彈，具備近炸、定時與觸發引信，在目標附近爆炸。砲塔會自動瞄準，優先攻擊空中目標；推動瞄準搖桿時可手動瞄準。',
        'KR': '[대공 MOD] 근접·시한·착발 신관을 갖춘 작렬탄으로 교체. 목표 근처에서 폭발한다. 포탑이 자동으로 조준하며 공중의 적을 우선한다. 조준 스틱을 기울이는 동안에는 수동으로 조준할 수 있다.',
        'SC': '【防空MOD】改用炸裂弹，具备近炸、定时与触发引信，在目标附近爆炸。炮塔会自动瞄准，优先攻击空中目标；推动瞄准摇杆时可手动瞄准。',
    },
    'air': {
        'JA': '【防空MOD】砲塔は自動で照準し、空中の敵を優先する。照準スティックを倒している間は手動で照準できる。',
        'EN': '[Anti-Air mod] The turret aims itself, air targets first; hold the aim stick to aim by hand.',
        'CN': '【防空MOD】砲塔會自動瞄準，優先攻擊空中目標；推動瞄準搖桿時可手動瞄準。',
        'KR': '[대공 MOD] 포탑이 자동으로 조준하며 공중의 적을 우선한다. 조준 스틱을 기울이는 동안에는 수동으로 조준할 수 있다.',
        'SC': '【防空MOD】炮塔会自动瞄准，优先攻击空中目标；推动瞄准摇杆时可手动瞄准。',
    },
    'ground': {
        'JA': '【防空MOD】砲塔は自動で照準し、地上の敵を優先する。弾道を計算して曲射で狙う。炸薬弾の爆発で建物も破壊できる。照準スティックを倒している間は手動で照準できる。',
        'EN': '[Anti-Air mod] The turret aims itself, ground targets first, lobbing rounds on their arc. The blasts also wreck buildings. Hold the aim stick to aim by hand.',
        'CN': '【防空MOD】砲塔會自動瞄準，優先攻擊地面目標，並依彈道拋射瞄準。炸藥的爆炸也能摧毀建築物。推動瞄準搖桿時可手動瞄準。',
        'KR': '[대공 MOD] 포탑이 자동으로 조준하며 지상의 적을 우선한다. 탄도를 계산해 곡사로 조준한다. 작약탄의 폭발로 건물도 파괴할 수 있다. 조준 스틱을 기울이는 동안에는 수동으로 조준할 수 있다.',
        'SC': '【防空MOD】炮塔会自动瞄准，优先攻击地面目标，并按弹道抛射瞄准。炸药的爆炸也能摧毁建筑物。推动瞄准摇杆时可手动瞄准。',
    },
}


@dataclass
class Vehicle:
    call: str            # call SGO file name
    gun: str             # one gun of the pair (both sides have the same stats)
    note: str            # key into NOTES


def _row_ids(table: bytes) -> list[str]:
    return [dsgo.to_py(r)[0].upper() for r in dsgo.parse(table).root.get('table').items]


def _durability_mul(call: bytes, name: str) -> float:
    return vehicle_setup.durability(vehicle_setup.of_call(dsgo.parse(call).root, name))


def _gun(data: bytes) -> tuple[float, float, float, float]:
    r = dsgo.parse(data).root
    return r.get('AmmoDamage'), r.get('AmmoSpeed') * r.get('AmmoAlive'), r.get('AmmoExplosion'), r.get('AmmoCount')


def _blast_template(stock_desc: str) -> str:
    """The text after the range of the stock Bohr stat line, number replaced by {}."""
    line = STATS.search(stock_desc).group(0)
    n = list(NUMBER.finditer(line))
    return line[n[2].end() + 1:n[3].start()] + '{}' + line[n[3].end():n[3].end() + 1]


def _stat_line(line: str, count: float, damage: float, rng: float, blast: float, blast_tpl: str) -> str:
    """Positional numbers: shots, damage, range[, blast]; each number is followed by its unit."""
    n = list(NUMBER.finditer(line))
    if len(n) > 3:
        line = line[:n[3].start()] + f'{blast:.1f}' + line[n[3].end():]
    elif blast > 0:
        cut = n[2].end() + 1
        line = line[:cut] + blast_tpl.format(f'{blast:.1f}') + line[cut:]
    line = line[:n[2].start()] + f'{rng:.1f}' + line[n[2].end():]
    line = line[:n[1].start()] + f'{damage:.1f}' + line[n[1].end():]
    return line[:n[0].start()] + f'{count:.0f}' + line[n[0].end():]


def _built(files: dict[str, bytes], name: str) -> bytes:
    """The rebuilt file, or the stock one for a vehicle whose call is left alone."""
    return files.get(f'WEAPON/{name}') or gamefs.read('WEAPON', name)


def _describe(row: list, v: Vehicle, lang: str, files: dict[str, bytes], blast_tpl: str) -> list:
    name, desc, stats = row
    old_dmg, old_rng, _, old_count = _gun(gamefs.read('WEAPON', v.gun))
    dmg, rng, blast, count = _gun(_built(files, v.gun))
    lines = list(STATS.finditer(desc))
    shown = [float(x) for x in NUMBER.findall(lines[0].group(0))[:3]]
    # The stat line shows the call's damage multiplier times the gun's damage; the range and shot
    # count are the gun's own. Anything else means this table is not the one these rules came from.
    assert abs(shown[2] - old_rng) < 0.05 and shown[0] == old_count, (v.call, lang, shown)
    mul = shown[1] / old_dmg
    out = desc[:lines[0].start()]
    for i, m in enumerate(lines):
        out += _stat_line(m.group(0), count, dmg * mul, rng, blast, blast_tpl)
        out += desc[m.end():lines[i + 1].start()] if i + 1 < len(lines) else ''
    end = lines[-1].end()
    out += '\n\n' + NOTE_FONT.format(NOTES[v.note][lang]) + desc[end:]
    old_mul = _durability_mul(gamefs.read('WEAPON', v.call), v.call)
    durability = round(float(stats[1][1]) / old_mul * _durability_mul(_built(files, v.call), v.call))
    return [name, out, [stats[0], [stats[1][0], str(durability)]]]


def _node(v: object) -> object:
    return Node([_node(x) for x in v]) if isinstance(v, list) else v


def _installed(mods: str, name: str) -> bytes:
    path = os.path.join(mods, 'WEAPON', name)
    if os.path.exists(path):
        with open(path, 'rb') as f:
            return f.read()
    return gamefs.read('WEAPON', name)


@lru_cache(maxsize=None)
def _stock_ids() -> tuple[str, ...]:
    return tuple(_row_ids(gamefs.read('WEAPON', 'WEAPONTABLE.SGO')))


def stock_ids() -> list[str]:
    """The stock weapon table's row ids, upper case."""
    return list(_stock_ids())


def text_rel(lang: str) -> str:
    return f'WEAPON/WEAPONTEXT.{lang}.SGO'


def table_ids(mods: str) -> list[str]:
    """The installed weapon table's row ids (stock when none is installed), upper case."""
    return _row_ids(_installed(mods, 'WEAPONTABLE.SGO'))


def text_rows(doc: dsgo.Document, ids: list[str], rel: str) -> list:
    """The rows of a WEAPONTEXT table; SystemExit when they are not aligned with the weapon table."""
    rows = doc.root.get('text_table').items
    if len(rows) != len(ids):
        raise SystemExit(f'{rel}: {len(rows)} rows but WEAPONTABLE has {len(ids)}: the tables are not index-aligned '
                         '(another mod changed one without the other); fix that before installing')
    return rows


@dataclass
class Texts:
    files: dict[str, bytes]              # Mods-relative path -> the rewritten table
    rows: dict[str, dict[str, tuple]]    # path -> row id -> (row before, row written), dsgo values


def build_texts(vehicles: list[Vehicle], files: dict[str, bytes], mods: str) -> Texts:
    stock = stock_ids()
    ids = table_ids(mods)
    out = Texts({}, {})
    for lang in LANGS:
        name = f'WEAPONTEXT.{lang}.SGO'
        rel = text_rel(lang)
        stock_rows = dsgo.parse(gamefs.read('WEAPON', name)).root.get('text_table').items
        doc = dsgo.parse(_installed(mods, name))
        rows = text_rows(doc, ids, rel)
        blast_tpl = _blast_template(dsgo.to_py(stock_rows[stock.index(BLAST_ROW[:-4])])[1])
        changed: dict[str, tuple] = {}
        for v in vehicles:
            row_id = v.call[:-4].upper()
            at = ids.index(row_id)
            new = _node(_describe(dsgo.to_py(stock_rows[stock.index(row_id)]), v, lang, files, blast_tpl))
            changed[row_id] = (rows[at], new)
            rows[at] = new
        out.files[rel] = dsgo.compact(doc)   # no stale strings in the pool: a rerun writes the same bytes
        out.rows[rel] = changed
    return out
