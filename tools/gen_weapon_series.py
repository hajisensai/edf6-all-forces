"""Writes src/weapon_series.inc: every row the installed weapon table can have, folded into series, for the 整备 screen's
weapons page (docs/plan-2026-11-24-ui-rewrite.md stage 5; the list logic: src/weapon_list_logic.h).

The rows (in the order the installed table has them, tools/call_weapons.py plan_rows):
  - EDF6's own 1564: docs/weapons.csv (id, class, category, the slots, WEAPONTABLE name, English / Japanese names,
    level, acquire, content pack) and docs/weapon-names-zh.csv (their simplified / traditional Chinese names: WEAPONTEXT.SC
    / .CN, written by --names from the game's Root.cpk; CI has no game, so the file is committed).
  - the call weapons (tools/calls.py CALLS): category of the stock row each is made from (call_weapons.template_of),
    level, the four names of calls.call_name.
  - the earlier games' weapons (tools/ported_weapons.py PORTS, the four registries in release order): category, level,
    names from the registry's texts (SC simplified, CN traditional).
  Source: EDF6 for the stock rows and the calls (a call is flagged), EDF5 / EDF4.1 by the registry a port is in.

The series rule (deterministic, on the English name; no run-time guessing):
  1. Rows group only within one (source, class, category): a series never mixes games or weapon kinds.
  2. The name is cut into words at spaces and hyphens. A word is a model mark when it has a digit (PA-11, M2, E3, NN4,
     X400, 0), is one to four capital letters with nothing else but '&' (LS, SLS, KF, AE, MKX, ZD, W, D, G&M, KFF, NP), or
     is a model word (MODEL_WORDS: Model, Mk, Mark, Ver, Type).
  3. The family is the other words, in order, lower-cased: 'T2 Stork KF' -> 'stork', 'Leopard Guided Rocket Launcher
     Model 0' -> 'leopard guided rocket launcher', 'E2 Radar Support System' -> 'radar support system'.
     A name of model marks only takes its first word's letters (before the digits) as the family: 'PA-11 LS' -> 'pa',
     'G&M-A23' -> 'g&m', 'KFF 50' -> 'kff'.
  4. Rows with the same family (and step 1's key) are one series. In a series the rows go by level, then table order;
     the series go by their lowest level, then the table order of their first row.
  5. A series' label in each language is the longest text every member's name in that language has whole (no run of
     Latin letters / digits cut at either end: 'X400-Orchid', 'X500-Orchid' -> 'Orchid', not '00-Orchid'), with no space,
     hyphen or middle dot at its ends, at least two characters long or one non-Latin one (a CJK word); the first such
     in the shortest name when two are as long. Without one, the name of the series' first row. A single-row series'
     label is that row's name.

Each row's level is the one the game shows and caps (docs/weapon-level-cap-re.md; GetRow 0xE1CF0 at 0xE20B9..0xE20DD):
max(0, (int)(col4 * 0.25f * 100.0f)) in float arithmetic.

The slot rules (src/weapon_list_logic.h CheckEquip; docs/loadout-re.md 1.2, CONFIG.SGO SoldierInit[class][4]): each
class's slots, the categories each takes and whether one row may sit in two slots of the class (the slot's 4th element,
read by 0xE2450 into +0x30; docs/weapon-select-re.md). SLOTS below is SoldierInit as Root.cpk has it (read 2026-11-24);
--names checks it against the game.

  python tools/gen_weapon_series.py            write src/weapon_series.inc
  python tools/gen_weapon_series.py --check    exit 1 when the committed file is not what this would write (CI)
  python tools/gen_weapon_series.py --names    (with the game) rewrite docs/weapon-names-zh.csv and check SLOTS
"""
from __future__ import annotations

import csv
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, HERE)
import call_weapons as cw  # noqa: E402
import calls  # noqa: E402
import ported_weapons as pw  # noqa: E402
import zw_weapons as zw  # noqa: E402

CSV = os.path.join(ROOT, 'docs', 'weapons.csv')
ZH = os.path.join(ROOT, 'docs', 'weapon-names-zh.csv')
OUT = os.path.join(ROOT, 'src', 'weapon_series.inc')

SOURCES = ('EDF6', 'EDF5', 'EDF41')
SOURCE_OF_GAME = {'edf5': 1, 'edf41': 2}
# hudtext::Lang order (src/hudtext.h): en, zhCN, zhTW, ja. WEAPONTEXT / the registries: EN, SC (simplified), CN
# (traditional), JA.
LANGS = ('EN', 'SC', 'CN', 'JA')
MODEL_WORDS = frozenset({'model', 'mk', 'mark', 'ver', 'type'})
STOCK_ROWS = 1564

# CONFIG.SGO SoldierInit[class][4]: (slot name, the categories it takes, one row may be in two slots of the class).
STOCK_SLOTS: tuple[tuple[tuple[str, tuple[int, ...], bool], ...], ...] = (
    (('Weapon_Slot1', (0, 1, 2, 3, 4, 5, 6), False), ('Weapon_Slot2', (0, 1, 2, 3, 4, 5, 6), False),
     ('Weapon_SlotSupport2', (20, 23, 21, 22), False), ('Weapon_SlotSupport1', (7, 8, 9, 10), False)),
    (('Weapon_Slot1', (100, 101, 102, 103, 104, 105, 106), False),
     ('Weapon_Slot2', (100, 101, 102, 103, 104, 105, 106), False),
     ('Weapon_SlotPaleSupport2', (110, 111, 113, 112, 116, 115, 114), False),
     ('Weapon_SlotPaleSupport1', (120,), False)),
    (('Weapon_Slot1', (310, 311, 312, 313, 314, 303, 305), False),
     ('Weapon_Slot2', (310, 311, 312, 313, 314, 303, 305), False),
     ('Weapon_Slot3', (310, 311, 312, 313, 314, 303, 305), False),
     ('Weapon_SlotAirRaiderSupport', (302, 330, 332, 333, 334, 331), False),
     ('Weapon_SlotVehicle', (306, 307, 308, 309, 320), False)),
    (('Weapon_Slot1L', (200, 201, 202, 203, 204, 205), True), ('Weapon_Slot1R', (200, 201, 202, 203, 204, 205), True),
     ('Weapon_Slot2L', (200, 201, 202, 203, 204, 205), True), ('Weapon_Slot2R', (200, 201, 202, 203, 204, 205), True),
     ('Weapon_SlotFencerSupport', (206, 207, 208, 209), False),
     ('Weapon_SlotFencerSupport', (206, 207, 208, 209), False)),
)
# then the new classes 4 and 5 the plugin adds to SoldierInit (tools/zw_weapons.py SLOTS; plan-2026-11-28-new-classes)
SLOTS = STOCK_SLOTS + zw.SLOTS
assert len(STOCK_SLOTS) == zw.FIRST_CLASS
FLAG_CALL, FLAG_INITIAL, FLAG_DLC = 1, 2, 4


def f32(x: float) -> float:
    return struct.unpack('<f', struct.pack('<f', x))[0]


def shown_level(raw: float) -> int:
    """GetRow's +0x20: (int)(col4 * 0.25f * 100.0f), negative -> 0."""
    v = int(f32(f32(f32(raw) * f32(0.25)) * f32(100.0)))
    return max(0, v)


def class_of(category: int) -> int:
    hits = [c for c, slots in enumerate(SLOTS) if any(category in cats for _, cats, _ in slots)]
    if len(hits) != 1:
        raise ValueError(f'category {category} is in the slots of {hits} classes')
    return hits[0]


def slot_mask(category: int) -> int:
    cls = class_of(category)
    return sum(1 << i for i, (_, cats, _) in enumerate(SLOTS[cls]) if category in cats)


_MARK = re.compile(r'^(?:.*\d.*|[A-Z&]{1,4})$')


def family(name_en: str) -> str:
    words = [w for w in re.split(r'[\s\-]+', name_en.strip()) if w]
    rest = [w for w in words if not _MARK.match(w) and w.lower() not in MODEL_WORDS]
    if rest:
        return ' '.join(rest).lower()
    if not words:
        raise ValueError('a weapon without an English name')
    head = re.match(r'^[^\d]*', words[0]).group(0)
    return (head or words[0]).lower()


_TRIM = ' 　-－・･.'


def _alnum(ch: str) -> bool:
    """A Latin letter or digit, half or full width: a label must not cut through a run of them ('X400-Orchid')."""
    return ch.isascii() and ch.isalnum() or '０' <= ch <= '９' or 'Ａ' <= ch <= 'Ｚ' or 'ａ' <= ch <= 'ｚ'


def _whole(name: str, part: str) -> bool:
    """part occurs in name with no Latin letter or digit run cut at either end."""
    at = name.find(part)
    while at >= 0:
        end = at + len(part)
        if not (at and _alnum(name[at - 1]) and _alnum(part[0])) and                 not (end < len(name) and _alnum(name[end]) and _alnum(part[-1])):
            return True
        at = name.find(part, at + 1)
    return False


def label(names: list[str]) -> str:
    """The longest text every name has whole (no Latin word or number cut), trimmed of spaces, hyphens and middle dots;
    at least two characters, or one that is not Latin (a CJK word); else the first name."""
    if len(names) == 1:
        return names[0]
    short = min(names, key=lambda n: (len(n), n))
    for size in range(len(short), 0, -1):
        found = []
        for at in range(len(short) - size + 1):
            part = short[at:at + size].strip(_TRIM)
            if part and part == short[at:at + size] and all(_whole(n, part) for n in names):
                found.append(part)
        found = [f for f in found if len(f) >= 2 or not _alnum(f) and not f.isascii()]
        if found:
            return found[0]
    return names[0]


def _read_csv(path: str) -> list[dict]:
    with open(path, encoding='utf-8-sig', newline='') as f:
        return list(csv.DictReader(f))


def stock_rows() -> list[dict]:
    rows = _read_csv(CSV)
    zh = _read_csv(ZH)
    if len(rows) != STOCK_ROWS or len(zh) != STOCK_ROWS:
        raise ValueError(f'docs/weapons.csv has {len(rows)} rows, docs/weapon-names-zh.csv {len(zh)}: want {STOCK_ROWS}')
    out = []
    for i, (w, z) in enumerate(zip(rows, zh)):
        if int(w['id']) != i or z['sgo_name'] != w['sgo_name']:
            raise ValueError(f'row {i}: weapons.csv and weapon-names-zh.csv disagree ({w["sgo_name"]} / {z["sgo_name"]})')
        category = int(w['category'])
        if class_of(category) != int(w['class_id']):
            raise ValueError(f'row {i}: class {w["class_id"]} but category {category} is {class_of(category)}\'s')
        mask = sum(1 << int(s) for s in w['slot_indices'].split('|')) if w['slot_indices'] else 0
        if mask != slot_mask(category):
            raise ValueError(f'row {i}: slots {w["slot_indices"]} are not what SLOTS gives category {category}')
        acquire = int(float(w['acquire']))
        flags = (FLAG_INITIAL if acquire == 1 else 0) | (FLAG_DLC if acquire == 3 else 0)
        out.append({'id': w['sgo_name'], 'source': 0, 'category': category, 'raw': float(w['level_raw']),
                    'flags': flags, 'names': (w['name_en'], z['name_sc'], z['name_cn'], w['name_ja'])})
    return out


def call_rows(stock: list[dict]) -> list[dict]:
    by_id = {r['id'].upper(): r for r in stock}
    out = []
    for c in calls.CALLS:
        tpl = by_id[cw.template_of(c).upper()]
        out.append({'id': c.id, 'source': 0, 'category': tpl['category'], 'raw': float(c.level), 'flags': FLAG_CALL,
                    'names': tuple(calls.call_name(c, lang) for lang in LANGS)})
    return out


def port_rows() -> list[dict]:
    return [{'id': p.id, 'source': SOURCE_OF_GAME[p.game], 'category': p.category, 'raw': float(p.level), 'flags': 0,
             'names': tuple(p.text[lang][0] for lang in LANGS)} for p in pw.PORTS]


def zw_rows(stock: list[dict]) -> list[dict]:
    """The new classes' rows (tools/zw_weapons.py): a none row named as its stock template, the rest by their texts."""
    by_id = {r['id'].upper(): r for r in stock}
    return [{'id': w.id, 'source': 0, 'category': w.category, 'raw': zw.LEVEL, 'flags': FLAG_INITIAL,
             'names': tuple(w.text[lang][0] for lang in LANGS) if w.text else by_id[w.template.upper()]['names']}
            for w in zw.WEAPONS]


def rows() -> list[dict]:
    """Every row in installed-table order (EDF6's, then call_weapons.plan_rows' appended order: calls, ports, then the
    new classes' weapons)."""
    stock = stock_rows()
    out = stock + call_rows(stock) + port_rows() + zw_rows(stock)
    if [r['id'] for r in out[STOCK_ROWS:]] != list(calls.IDS + pw.IDS + zw.IDS):
        raise ValueError('the appended rows are not in call_weapons.plan_rows order')
    for i, r in enumerate(out):
        r['index'] = i
        r['cls'] = class_of(r['category'])
        r['level'] = shown_level(r['raw'])
        r['slots'] = slot_mask(r['category'])
        if not all(r['names']):
            raise ValueError(f'{r["id"]} lacks a name in one of {LANGS}')
    ids = [r['id'].upper() for r in out]
    if len(ids) != len(set(ids)):
        raise ValueError('a WEAPONTABLE name twice')
    return out


def series() -> list[dict]:
    """[{'key', 'source', 'cls', 'category', 'rows' (by level, then index), 'labels'}], by (source, class, category),
    then lowest level, then first index."""
    groups: dict[tuple, list[dict]] = {}
    for r in rows():
        groups.setdefault((r['source'], r['cls'], r['category'], family(r['names'][0])), []).append(r)
    out = []
    for key, members in groups.items():
        members.sort(key=lambda r: (r['level'], r['raw'], r['index']))
        out.append({'key': key, 'source': key[0], 'cls': key[1], 'category': key[2], 'rows': members,
                    'labels': tuple(label([m['names'][k] for m in members]) for k in range(len(LANGS)))})
    out.sort(key=lambda s: (s['source'], s['cls'], s['category'], s['rows'][0]['level'], s['rows'][0]['index']))
    return out


def _s(text: str) -> str:
    return 'L"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def render() -> str:
    ss = series()
    out = ['// Generated by tools/gen_weapon_series.py from docs/weapons.csv, docs/weapon-names-zh.csv, tools/calls.py and the',
           '// ported weapon registries (tools/ported_weapons.py): do not edit (CI checks it). The rule: the generator\'s doc.',
           '// kWeaponRows: by series (kWeaponSeries order), in a series by level then table order:',
           '//   {WEAPONTABLE name, table index when installed with nothing else, source (0 EDF6, 1 EDF5, 2 EDF4.1), class,',
           '//    category, level shown (row +0x20), series, slots (bit i: slot i of the class takes it), flags (1 call,',
           '//    2 initial equipment, 4 DLC), names {en, zhCN, zhTW, ja}}',
           f'inline constexpr std::uint32_t kWeaponSeriesStockRows={STOCK_ROWS};',
           'inline constexpr WeaponRowInfo kWeaponRows[]={']
    n = 0
    heads = []
    for si, s in enumerate(ss):
        heads.append((n, len(s['rows'])))
        for r in s['rows']:
            names = ','.join(_s(x) for x in r['names'])
            out.append(f'    {{{_s(r["id"])},{r["index"]},{r["source"]},{r["cls"]},{r["category"]},{r["level"]},{si},'
                       f'{r["slots"]},{r["flags"]},{{{names}}}}},')
            n += 1
    out += ['};', '// {source, class, category, first row in kWeaponRows, rows, label {en, zhCN, zhTW, ja}}',
            'inline constexpr WeaponSeriesInfo kWeaponSeries[]={']
    for s, (first, count) in zip(ss, heads):
        labels = ','.join(_s(x) for x in s['labels'])
        out.append(f'    {{{s["source"]},{s["cls"]},{s["category"]},{first},{count},{{{labels}}}}},')
    out += ['};', '// CONFIG.SGO SoldierInit[class][4]: {class, slot, may hold a row another slot of the class holds, categories}',
            'inline constexpr WeaponSlotRule kWeaponSlotRules[]={']
    for cls, slots in enumerate(SLOTS):
        for i, (name, cats, dup) in enumerate(slots):
            cat = ','.join(str(c) for c in cats) + ',-1' * (8 - len(cats))
            out.append(f'    {{{cls},{i},{str(dup).lower()},{{{cat}}}}},   // {name}')
    out.append('};')
    return '\n'.join(out) + '\n'


def write_names() -> int:
    """With the game: docs/weapon-names-zh.csv from Root.cpk's WEAPONTEXT.SC / .CN, and SLOTS checked against CONFIG.SGO."""
    import dsgo
    import rootcpk
    import sgo
    game = rootcpk.default()
    table = [r.items[0] for r in dsgo.parse(game.read('WEAPON', 'WEAPONTABLE.SGO')).root.get('table').items]
    texts = {lang: [r.items[0] for r in dsgo.parse(game.read('WEAPON', f'WEAPONTEXT.{lang}.SGO')).root.get('text_table').items]
             for lang in ('SC', 'CN')}
    if len(table) != STOCK_ROWS or any(len(t) != STOCK_ROWS for t in texts.values()):
        raise SystemExit(f'the stock table has {len(table)} rows: want {STOCK_ROWS}')
    config = sgo.load(data=game.read('DEFAULTPACKAGE', 'CONFIG.SGO'))
    have = tuple(tuple((s[0], tuple(s[2]), bool(s[3])) for s in c[4]) for c in config['SoldierInit'])
    if have != STOCK_SLOTS:
        raise SystemExit(f'STOCK_SLOTS is not CONFIG.SGO SoldierInit: {have}')
    with open(ZH, 'w', encoding='utf-8-sig', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(['id', 'sgo_name', 'name_sc', 'name_cn'])
        for i, name in enumerate(table):
            w.writerow([i, name, texts['SC'][i], texts['CN'][i]])
    print(f'wrote {os.path.relpath(ZH, ROOT)}')
    return 0


def main(argv: list[str]) -> int:
    if '--names' in argv:
        return write_names()
    text = render()
    if '--check' in argv:
        try:
            with open(OUT, encoding='utf-8', newline='') as f:
                have = f.read().replace('\r\n', '\n')
        except OSError:
            have = None
        if have != text:
            print(f'{os.path.relpath(OUT, ROOT)} is stale: python tools/gen_weapon_series.py', file=sys.stderr)
            return 1
        return 0
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(f'wrote {os.path.relpath(OUT, ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
