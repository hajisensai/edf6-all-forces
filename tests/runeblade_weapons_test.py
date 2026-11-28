"""The Rune Blade's weapon rows (tools/zw_weapons.py, docs/newclass-re/runeblade-weapons-plan.md): every weapon of plan §2.2 /
§2.3 in its slot, the marks equal to src/runeblade_weapons_logic.h's and apart from every other registry's, the rows whose
run time is not in yet unowned, and, when Root.cpk is at hand, their SGOs and text rows built from the stock templates.
Plain asserts: python tests/runeblade_weapons_test.py"""
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
sys.path.insert(0, str(ROOT / 'tools'))
import call_weapons as cw  # noqa: E402
import gamedir  # noqa: E402
import zw_weapons as zw  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    assert ok, why


# plan §2.2 / §2.3: five melee (500), three ranged (501), one support (502), beyond the none rows
RUNE = {'ZW_RuneSaber01': 500, 'ZW_RuneLance01': 500, 'ZW_RuneRing01': 500, 'ZW_RuneHammer01': 500, 'ZW_RuneIai01': 500,
        'ZW_RuneCarbine01': 501, 'ZW_RuneRevolver01': 501, 'ZW_RuneStars01': 501, 'ZW_EtherBeacon01': 502}
for rid, cat in RUNE.items():
    check(rid in zw.BY_ID and zw.BY_ID[rid].category == cat, f'{rid}: in category {cat}')
    w = zw.BY_ID[rid]
    check(set(w.text) == set(zw.LANGS) and all(n.strip() and d.strip() for n, d in w.text.values()), f'{rid}: 5 languages')
    check(not any('试作' in n or '試作' in n or 'Prototype' in n or '시제' in n for n, _ in w.text.values()),
          f'{rid}: no prototype name')
# R1 is the class default (src/soldier_classes*): owned; the rows whose run time is not in yet: unowned
check(zw.BY_ID['ZW_RuneCarbine01'].acquire == 1.0 and zw.BY_ID['ZW_RuneRevolver01'].acquire == 1.0, 'R1 / R2 owned')
for rid in ('ZW_RuneSaber01', 'ZW_RuneLance01', 'ZW_RuneRing01', 'ZW_RuneHammer01', 'ZW_RuneIai01', 'ZW_RuneStars01',
            'ZW_EtherBeacon01'):
    check(zw.BY_ID[rid].acquire == zw.LOCKED == 0.0, f'{rid}: locked until its run time is in')
inc = (ROOT / 'src' / 'weapon_series.inc').read_text(encoding='utf-8')
for rid, w in ((r, zw.BY_ID[r]) for r in RUNE):
    m = re.search(r'\{L"%s",\d+,\d+,5,%d,\d+,\d+,\d+,(\d+),' % (rid, w.category), inc)
    check(m is not None, f'{rid}: in weapon_series.inc, class 5')
    check(bool(int(m.group(1)) & 2) == (w.acquire == 1.0), f'{rid}: the initial flag follows acquire')

# marks: the Python and C++ sides agree, unique, in the free range
logic = (ROOT / 'src' / 'runeblade_weapons_logic.h').read_text(encoding='utf-8')
base = int(re.search(r'kMarkBase=0x([0-9A-Fa-f]+)u', logic).group(1), 16)
check(base == zw.RUNE_MARK_BASE, 'mark base agrees')
enum = {k: int(v) for k, v in re.findall(r'^\s+(\w+)=(\d+),', logic, re.M) if k != 'none'}
check(enum == zw.RUNE_MARKS, f'marks agree: {enum} vs {zw.RUNE_MARKS}')
check(int(re.search(r'kMarks=(\d+);', logic).group(1)) == max(zw.RUNE_MARKS.values()) + 1, 'kMarks one past the last')
check(len(set(zw.RUNE_MARKS.values())) == len(zw.RUNE_MARKS), 'marks unique')
tuned_marks = [w.tune.mark for w in zw.WEAPONS if w.kind == 'tuned' and w.tune.mark]
check(len(tuned_marks) == len(set(tuned_marks)) and zw.RUNE_MARKS['carbine'] not in tuned_marks, 'one weapon a mark')
for n in zw.RUNE_MARKS.values():
    bits = struct.unpack('<I', struct.pack('<f', zw.rune_mark(n)))[0]
    check(bits & 0xFFFFF000 not in (0x3F800000, 0x3F801000) and bits not in (0x3F8F4A36, 0x3F8F4A37),
          'apart from the drones / barrages / Andryusha')
    check(1.0 < zw.rune_mark(n) < 1.001, 'hit radius as good as 1.0')

# with the game: SGOs and text rows from the stock templates (CI has no game)
game_root = gamedir.find()
if game_root is None:
    print('runeblade_weapons: no game here, Root.cpk part skipped')
else:
    import dsgo  # noqa: E402
    table = dsgo.parse(cw.stock(game_root, 'WEAPON/WEAPONTABLE.SGO'))
    rows = cw._rows(table, 'WEAPON/WEAPONTABLE.SGO')
    ids = [r.items[0] for r in rows]
    texts = {lang: cw._rows(dsgo.parse(cw.stock(game_root, f'WEAPON/WEAPONTEXT.{lang}.SGO')), lang) for lang in zw.LANGS}
    for rid in RUNE:
        w = zw.BY_ID[rid]
        sgo = dsgo.parse(zw.build_sgo(w, cw.stock(game_root, f'WEAPON/{w.template.upper()}.SGO'))).root
        check(sgo.get('name.en') == w.text['EN'][0], f'{rid}: its name')
        if w.kind == 'tuned':
            for k, v in w.tune.params.items():
                got = dsgo.to_py(sgo.get(k))
                check(got == (list(v) if isinstance(v, (list, tuple)) else v), f'{rid}: {k} {got} != {v}')
            if w.tune.mark:
                check(sgo.get('AmmoHitSizeAdjust') == zw.rune_mark(w.tune.mark), f'{rid}: marked')
            if w.tune.color:
                check(dsgo.to_py(sgo.get('AmmoColor')) == list(w.tune.color), f'{rid}: colour')
        else:
            check(sgo.get('AmmoHitSizeAdjust') == zw.rune_mark(zw.RUNE_MARKS['carbine']), 'R1: marked')
        i = ids.index(w.template)
        trow = zw.table_row(rows[i], w)
        check(trow.items[0] == rid and trow.items[2] == float(w.category) and trow.items[5] == w.acquire, f'{rid}: row')
        for lang in zw.LANGS:
            tr = zw.text_row(texts[lang][i], w, lang)
            check(tr.items[0] == w.text[lang][0], f'{rid} {lang}: name')
            stats = w.tune.stats if w.kind == 'tuned' else zw.CARBINE_STATS
            for k in stats:
                line = tr.items[2].items[k]
                check(len(line.items) == 2 and '$' not in line.items[1], f'{rid} {lang} line {k}: {dsgo.to_py(line)}')
print(f'runeblade_weapons: {checks} checks passed')
