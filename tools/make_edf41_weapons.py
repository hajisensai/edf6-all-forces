"""Developer tool: which EDF4.1 weapons neither EDF5 nor EDF6 has, and how each comes over -> edf41port/weapons.json
(committed).

  python -B tools/make_edf41_weapons.py [--edf41 DIR] [--edf5 DIR] [--edf6 DIR]

Runs once on a machine with the three games; the JSON is a registry tools/ported_weapons.py installs from, each weapon
converted here (pylib/edf5port.py weapon41) and kept in it ('weapon'), so the install needs no EDF4.1 (2026-10-10 user:
make them ahead of time, as EDF5's). A weapon is a row of
4.1's WEAPON/_WEAPONTABLE.SGO no row of EDF5's or EDF6's table names, in Japanese or English (make_edf5_weapons.key /
words, and without 4.1's unit suffix: its 榴弾砲〔砲兵隊〕 is EDF6's 榴弾砲); one EDF5 has but EDF6 lacks is
tools/make_edf5_weapons.py's to bring over.

Category: 4.1 numbers its own (CATEGORY: by soldier and kind, the same kinds EDF6 has), its Air Raider calls by class
(CALLS). Left out, listed under 'skipped' with why:
  - the vehicle requests (4.1 categories 36-39): 4.1 drops a vehicle with a Transporter401 its Weapon_Throw calls, a
    class EDF6 no longer has; and the calls of 31 that are not artillery: the Whale gunship's (Weapon_BasicShoot
    smoke candles) and the bombers' (Weapon_Throw smoke candles of mode 2, Ammo_CustomParameter[3], bringing
    Bomber401; every EDF6 call is mode 0, artillery, or 1, a vehicle): docs/edf5-weapons-plan.md P5;
  - a weapon naming a resource EDF6 lacks (make_edf5_weapons.Edf6.missing; mostly decoy models, P3).
Its class: a Weapon_BasicShoot in a category whose EDF6 weapons are all Weapon_Sub becomes one (edf5port.to_sub), every
other keeps 4.1's. Template, AmmoDamageAttribute: as make_edf5_weapons. Sound cues: a cue no EDF6 bank holds (it would
play nothing; pylib/acb.py) takes the template's cue at the same place ('cues'); a weapon with one the template has no
cue for stays out.
Texts: names and descriptions JA / EN are 4.1's, CN / SC / KR translated (edf41port/translations.json; 4.1 has no
Chinese or Korean). 4.1 writes the stats inside the description (a FixedFont block, 'label：value'); each line EDF6's own
texts show the same way (its label and format, matched with its numbers) becomes an EDF6 stat line [label, value] in
all five languages (STAT_FORMATS, from EDF6's texts aligned row by row); a line EDF6 has no form for stays in the
description, in its language for JA / EN and in English for the others.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import acb  # noqa: E402
import dsgo  # noqa: E402
import edf5port  # noqa: E402
import make_edf5_weapons as m5  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

STEAM = r'D:\Steam\steamapps\common'
OUT = os.path.join(HERE, '..', 'edf41port', 'weapons.json')
TRANSLATIONS = os.path.join(HERE, '..', 'edf41port', 'translations.json')
LANGS = ('JA', 'EN', 'CN', 'SC', 'KR')
ID_PREFIX = 'EDF6VC_E41_'
# 4.1 category -> EDF6 category (the kinds line up: assault rifle 0 -> 0 ... the Wing Diver's 10-16 -> 100-106, her
# thrown cluster weapons 17 into the weapon slots as EDF5's 107 (make_edf5_weapons.TARGET), the Fencer's 20-25 ->
# 200-205, the Air Raider's guide kits 30 -> 305, Life Vendors 32 -> 302, Limpet guns 33 -> 303, bombs and sentry guns
# 34 -> 305 (EDF5's 304: weapon slots), barriers / decoys / string 35 -> 302 when a Weapon_Sub there, else 305).
CATEGORY = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 10: 100, 11: 101, 12: 102, 13: 103, 14: 104, 15: 105, 16: 106,
            17: 105, 20: 200, 21: 201, 22: 202, 23: 203, 24: 204, 25: 205, 30: 305, 32: 302, 33: 303, 34: 305, 35: 305}
VEHICLES = {36, 37, 38, 39}
# 4.1's category 31 by class: thrown artillery calls (mode 0) are EDF6's 310 (its mortars and howitzers are
# Weapon_Throw mode 0 too); laser-marker airstrikes 313 or 314 by their series (airstrike_category); the rest stays out.
CALLS = {'Weapon_Throw': 310, 'Weapon_LaserMarkerCallFire': None}
SUFFIX = re.compile(r'\s*(〔[^〕]*〕|［[^］]*］|\[[^\]]*\]|（[^）]*）|\([^)]*\))\s*$')
MODEL = re.compile(r'[A-Za-z0-9\-.+]+$')
SEP = {'JA': '：', 'EN': ':'}


def bare(name: str) -> str:
    """A 4.1 name without its unit suffix (榴弾砲〔砲兵隊〕, Howitzer (Artillery Squad))."""
    return SUFFIX.sub('', name)


def series(name: str) -> str:
    """A weapon name without its model code: ライオニック２０ / ライオニックＵ２０ -> ライオニック."""
    return MODEL.sub('', unicodedata.normalize('NFKC', bare(name)).strip()).strip()


def airstrike_category(name: str, e6: m5.Edf6, ja6: list[str]) -> int:
    """313 (missiles) or 314 (lasers, satellites) for a 4.1 laser-marker airstrike: the EDF6 category whose weapons
    share its series (EDF6's 313 holds ライオニック / テンペスト, its 314 バルジレーザー / スプライトフォール); a series EDF6
    lacks by what it fires (ミサイル: 313), else 314."""
    mine = series(name)
    for i, r in enumerate(e6.rows):
        if int(r[2]) in (313, 314) and series(ja6[i]) == mine:
            return int(r[2])
    return 313 if 'ミサイル' in name else 314


def plain_table(g: rootcpk.Game, name: str, key: str) -> list:
    return sgo.load(data=g.read('WEAPON', name))[key]


def stat_block(text: str) -> tuple[list[str], str]:
    """A 4.1 description: (its stat lines, the description after them)."""
    parts = re.split(r'<font face=%dq%\$NormalFont%dq%[^>]*>', text)
    if len(parts) < 2:
        return [], re.sub(r'<[^>]+>', '', text).strip()
    head = re.sub(r'<[^>]+>', '', parts[0])
    return [l.strip() for l in head.split('\n') if l.strip()], re.sub(r'<[^>]+>', '', parts[-1]).strip()


def stat_formats(g6: rootcpk.Game) -> dict[str, list[dict[str, tuple[str, str]]]]:
    """EDF6's stat lines in all five languages, aligned by weapon row and line: JA label -> its forms, most used first,
    each {lang: (label, value format)}."""
    texts = {L: plain_table(g6, f'WEAPONTEXT.{L}.SGO', 'text_table') for L in LANGS}
    count: dict[str, dict[str, int]] = {}
    forms: dict[str, dict] = {}
    for i in range(len(texts['JA'])):
        rows = {L: texts[L][i][2] for L in LANGS}
        if len({len(r) for r in rows.values()}) != 1:
            continue
        for j in range(len(rows['JA'])):
            line = {L: rows[L][j] for L in LANGS}
            if not all(len(x) >= 2 and isinstance(x[0], str) and isinstance(x[1], str) for x in line.values()):
                continue
            form = _generalize({L: (line[L][0], line[L][1]) for L in LANGS})
            k = json.dumps(form, ensure_ascii=False, sort_keys=True)
            forms[k] = form
            count.setdefault(line['JA'][0], {})
            count[line['JA'][0]][k] = count[line['JA'][0]].get(k, 0) + 1
    return {label: [forms[k] for k in sorted(c, key=lambda k: -c[k])] for label, c in count.items()}


NUMBER = r'(-?[0-9]+(?:\.[0-9]+)?)'
NUMBERS = re.compile(r'[0-9]+(?:\.[0-9]+)?')
# Stat labels 4.1 shows and EDF6 never does: {JA: {lang: label}}.
LABELS41: dict[str, dict[str, str]] = {
    '供給量': {'JA': '供給量', 'EN': 'Supply Rate', 'CN': '供給量', 'SC': '供给量', 'KR': '공급량'},
}
# A value made only of these is the same in every language (an accuracy grade, a dash, a plain number or percent).
NEUTRAL = re.compile(r'[0-9A-Z.+\-%％－――ー/]+')


def _generalize(form: dict[str, tuple[str, str]]) -> dict[str, tuple[str, str]]:
    """A form whose value is a literal ('90.0秒' / '90.0 sec' / ...) as a format ('$0秒' / '$0 sec' / ...), when every
    language shows the same numbers in the same order."""
    numbers = {L: NUMBERS.findall(unicodedata.normalize('NFKC', v)) for L, (_, v) in form.items()}
    first = numbers['JA']
    if not first or any(n != first for n in numbers.values()) or any('$' in v for _, v in form.values()):
        return form
    out = {}
    for L, (label, value) in form.items():
        if NUMBERS.findall(value) != first:   # the digits are full width in this language's text: keep the literal
            return form
        k = 0

        def number(_m: re.Match) -> str:
            nonlocal k
            k += 1
            return f'${k - 1}'
        out[L] = (label, NUMBERS.sub(number, value))   # the game's own text around the numbers, as written
    return out


def fragments(formats: dict) -> dict[str, dict[str, str]]:
    """The text between the numbers of EDF6's formats, JA (NFKC, no blanks) -> each language's, from the forms whose
    formats split into as many pieces in every language: '発/秒' -> '發/秒', '/sec', ... The most used wins."""
    count: dict[tuple[str, str], int] = {}
    pieces: dict[tuple[str, str], dict[str, str]] = {}
    for forms in formats.values():
        for form in forms:
            split = {L: re.split(r'\$[0-9]', form[L][1]) for L in LANGS}
            if len({len(x) for x in split.values()}) != 1:
                continue
            for i, piece in enumerate(split['JA']):
                k = (_norm(piece), json.dumps({L: split[L][i] for L in LANGS}, ensure_ascii=False))
                count[k] = count.get(k, 0) + 1
                pieces[k] = {L: split[L][i] for L in LANGS}
    best: dict[str, tuple[int, dict[str, str]]] = {}
    for (ja, _), n in count.items():
        if ja and (ja not in best or n > best[ja][0]):
            best[ja] = (n, pieces[(ja, _)])
    return {ja: v for ja, (_, v) in best.items()}


def _compose(value: str, pieces: dict[str, dict[str, str]]) -> dict[str, str] | None:
    """A value as numbers and known pieces (longest first), each language's pieces put in: None if a piece is not
    known."""
    text = _norm(value)
    out = {L: '' for L in LANGS}
    at = 0
    keys = sorted(pieces, key=len, reverse=True)
    while at < len(text):
        num = re.match(NUMBER, text[at:])
        if num:
            for L in LANGS:
                out[L] += num.group(1)
            at += num.end()
            continue
        hit = next((k for k in keys if text.startswith(k, at)), None)
        if hit is None:
            return None
        for L in LANGS:
            out[L] += pieces[hit][L]
        at += len(hit)
    return out


def _norm(s: str) -> str:
    return re.sub(r'\s+', '', unicodedata.normalize('NFKC', s))


def segments(line: str) -> list[tuple[str, str]]:
    """A 4.1 JA stat line as its (label, value) pairs: some lines hold two, apart by a run of blanks ('弾数：30
    ズーム:2倍'); the separator is a full or a half width colon. A part without one goes on the value before it
    ('ダメージ：60.0    ×30')."""
    out: list[tuple[str, str]] = []
    for part in re.split(r'\s{2,}', line.strip()):
        sep = re.search(r'[：:]', part)
        if sep is None:
            if not out:
                return []
            out[-1] = (out[-1][0], out[-1][1] + part.strip())
            continue
        out.append((part[:sep.start()].strip(), part[sep.end():].strip()))
    return out


def stat_lines(line: str, formats: dict, pieces: dict[str, dict[str, str]]) -> list[dict[str, list[str]]] | None:
    """A 4.1 JA stat line as EDF6 stat lines [{lang: [label, value]}], or None when a part of it has no EDF6 form: its
    label EDF6's (most used form's) or LABELS41's; its value through one of the label's formats (its numbers put in),
    as is when language-neutral (NEUTRAL), or put together from EDF6's pieces (fragments)."""
    parts = segments(line)
    if not parts:
        return None
    out = []
    for label, value in parts:
        forms = formats.get(label, [])
        labels = {L: forms[0][L][0] for L in LANGS} if forms else LABELS41.get(label)
        if labels is None:
            return None
        values = None
        for form in forms:
            pattern = re.escape(_norm(form['JA'][1]))
            for n in range(10):
                pattern = pattern.replace(re.escape(f'${n}'), NUMBER)
            hit = re.fullmatch(pattern, _norm(value))
            if hit:
                values = {}
                for L in LANGS:
                    text = form[L][1]
                    for n, num in enumerate(hit.groups()):
                        text = text.replace(f'${n}', num)
                    values[L] = text
                break
        if values is None and NEUTRAL.fullmatch(_norm(value)):
            values = {L: value for L in LANGS}
        if values is None:
            values = _compose(value, pieces)
        if values is None:
            return None
        out.append({L: [labels[L], values[L]] for L in LANGS})
    return out


def cue_swaps(own: dict, template: dict, edf6_cues: set[str], known: set[str]) -> dict[str, str]:
    """Each cue of `own` (a 4.1 cue: in `known`) EDF6 has no bank for -> the template's cue at the same place."""
    swaps: dict[str, str] = {}

    def walk(a: object, b: object) -> None:
        if isinstance(a, str):
            if a in known and a not in edf6_cues and isinstance(b, str) and b in edf6_cues:
                swaps.setdefault(a, b)
        elif isinstance(a, list):
            for i, x in enumerate(a):
                walk(x, b[i] if isinstance(b, list) and i < len(b) else None)
        elif isinstance(a, dict):
            for k, x in a.items():
                walk(x, b.get(k) if isinstance(b, dict) else None)

    walk(own, template)
    return swaps


def missing_cues(own: dict, swaps: dict[str, str], edf6_cues: set[str], known: set[str]) -> list[str]:
    return sorted({s for s in m5._strings(own) if s in known and s not in edf6_cues and s not in swaps})


def build(edf41: str, edf5: str, edf6: str) -> dict:
    g4 = rootcpk.Game(edf41)
    g5 = rootcpk.Game(edf5)
    e6 = m5.Edf6(edf6)
    subs = e6.sub_categories()
    formats = stat_formats(e6.game)
    pieces = fragments(formats)
    names5 = {m5.key(r[0]) for L in ('JA', 'EN') for r in plain_table(g5, f'WEAPONTEXT.{L}.SGO', 'text_table')}
    names5 |= {m5.words(r[0]) for r in plain_table(g5, 'WEAPONTEXT.EN.SGO', 'text_table')}
    cues6 = acb.game_cues(edf6)
    cues41 = acb.game_cues(edf41)
    with open(TRANSLATIONS, encoding='utf-8') as f:
        translated = json.load(f)
    rows = plain_table(g4, '_WEAPONTABLE.SGO', 'table')
    texts = plain_table(g4, '_WEAPONTEXT.SGO', 'text_table')
    ja6 = [r[0] for r in plain_table(e6.game, 'WEAPONTEXT.JA.SGO', 'text_table')]
    weapons, skipped = [], []
    for i, (row, text) in enumerate(zip(rows, texts)):
        sgo_id, path, cat41, _one, level, _acquire = row
        ja, en = text[0], text[2]
        known = (m5.key(ja), m5.key(en), m5.words(en), m5.key(bare(ja)), m5.key(bare(en)), m5.words(bare(en)))
        if any(n in e6.names or n in names5 for n in known):
            continue
        file = m5.sgo_name(path)
        own = sgo.load(data=g4.read('WEAPON', file))
        cls = own['xgs_scene_object_class']
        ammo = own.get('AmmoClass', '')
        cat41 = int(cat41)
        if cat41 in VEHICLES:
            skipped.append({'sgo': sgo_id, 'name': ja, 'reason': '载具呼叫：4.1 用 Transporter401 投送，EDF6 已没有这个类（P5）'})
            continue
        custom = own.get('Ammo_CustomParameter')
        mode = custom[3] if isinstance(custom, list) and len(custom) > 3 and isinstance(custom[3], (int, float)) else 0
        if cat41 == 31 and (cls not in CALLS or (ammo == 'SmokeCandleBullet01' and mode != 0)):
            skipped.append({'sgo': sgo_id, 'name': ja, 'reason': '攻击机 / 轰炸机呼叫：EDF6 没有这种呼叫方式（P5）'})
            continue
        if cat41 == 31:
            category = CALLS[cls] or airstrike_category(ja, e6, ja6)
        else:
            category = CATEGORY[cat41]
        missing = sorted(e6.missing({p for p in m5._strings(own) if p.lower().startswith('app:/')}))
        if missing:
            skipped.append({'sgo': sgo_id, 'name': ja, 'reason': 'EDF6 没有这些资源：' + ', '.join(missing)})
            continue
        members = sgo.read(g4.read('WEAPON', file))[1]
        try:   # what the install will do with it (tools/ported_weapons.py build_sgo): refused here, not there
            edf5port.weapon41(members, {})
        except edf5port.Unsupported as e:
            skipped.append({'sgo': sgo_id, 'name': ja, 'reason': f'转换不支持：{e}'})
            continue
        if category in subs and cls == 'Weapon_BasicShoot':
            cls = edf5port.SUB
        category = e6.category_for(category, cls, ammo, subs)
        template = e6.template(category, cls, ammo, float(level))
        swaps = cue_swaps(own, e6.weapon(f'{template}.SGO'), cues6, cues41)
        silent = missing_cues(own, swaps, cues6, cues41)
        if silent:
            skipped.append({'sgo': sgo_id, 'name': ja, 'reason': 'EDF6 没有这些音效且无可替换：' + ', '.join(silent)})
            continue
        lines_ja, desc_ja = stat_block(text[1])
        lines_en, desc_en = stat_block(text[3])
        stats = {L: [] for L in LANGS}
        left_ja, left_en = [], []
        for k, line in enumerate(lines_ja):
            mapped = stat_lines(line, formats, pieces)
            if mapped is None:
                left_ja.append(line)
                if k < len(lines_en):
                    left_en.append(lines_en[k])
                continue
            for one in mapped:
                for L in LANGS:
                    stats[L].append(one[L])
        t = translated[file]
        desc = {'JA': desc_ja, 'EN': desc_en, **{L: t[L][1] for L in ('CN', 'SC', 'KR')}}
        extra = {'JA': left_ja, 'EN': left_en, 'CN': left_en, 'SC': left_en, 'KR': left_en}
        names = {'JA': ja, 'EN': en, **{L: t[L][0] for L in ('CN', 'SC', 'KR')}}
        entry_text = {L: [names[L], '\n'.join(extra[L] + ([''] if extra[L] else []) + [desc[L]]), stats[L]]
                      for L in LANGS}
        weapons.append({
            'id': ID_PREFIX + sgo_id.upper(), 'sgo': file, 'source': 'edf41', 'edf41_row': i,
            'edf41_category': cat41, 'category': category, 'class': cls, 'level': float(level), 'stars': [],
            'pack': 0, 'template': template, 'text': entry_text,
            'damage_attribute': e6.damage_attribute(category, ammo), 'cues': swaps,
            'unmapped_stats': len(left_ja),
            # The weapon itself, converted here: the install builds it from this, with no EDF4.1.
            'weapon': dsgo.dump(edf5port.weapon41(members, {L.lower(): names[L] for L in LANGS}).root),
        })
    return {'source': "EDF4.1 Root.cpk WEAPON/_WEAPONTABLE.SGO + _WEAPONTEXT.SGO; CN / SC / KR edf41port/translations.json",
            'weapons': weapons, 'skipped': skipped}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--edf41', default=os.path.join(STEAM, 'Earth Defense Force 4.1'))
    ap.add_argument('--edf5', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 5'))
    ap.add_argument('--edf6', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 6'))
    a = ap.parse_args()
    data = build(a.edf41, a.edf5, a.edf6)
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write('\n')
    print(f'{OUT}: {len(data["weapons"])} weapons, {len(data["skipped"])} skipped')
    reasons: dict[str, int] = {}
    for s in data['skipped']:
        r = s['reason'].split('：')[0]
        reasons[r] = reasons.get(r, 0) + 1
    print('  skipped:', reasons)
    print('  weapons with stat lines left in the description:', sum(1 for w in data['weapons'] if w['unmapped_stats']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
