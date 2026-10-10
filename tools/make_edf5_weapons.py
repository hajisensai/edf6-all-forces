"""Developer tool: which EDF5 weapons EDF6 lacks, and how each comes over -> edf5port/weapons.json (committed).

  python -B tools/make_edf5_weapons.py [--edf5 DIR] [--edf6 DIR]

Runs once on a machine with both games; the JSON is the registry tools/ported_weapons.py installs from. A weapon is
EDF5's (a row of its WEAPONTABLE) that no row of EDF6's table names, in Japanese or English (NFKC, blanks dropped:
EDF5 writes 'ニクス  レッドガード', EDF6 'ニクス レッドガード'). Each comes from one of two sources:
  'edf6'  EDF6 ships the weapon's SGO but its table never names it (Edf6.leftover; the EDF5 DLC weapons): that file,
          already converted by the developers (Weapon_Sub where EDF6 wants one, its own balance and names), installed
          as a copy under our id. Needs nothing of EDF5's.
  'edf5'  converted from EDF5 here (pylib/edf5port.py weapon), the result kept in the registry ('weapon'), so the
          install needs no EDF5: there it takes its category's EDF6 class (Weapon_Sub in the support and vehicle
          slots, edf5port.to_sub, from EDF6's own template) and its family's AmmoDamageAttribute.
Its category is EDF5's, but where EDF6 has no slot for it (TARGET) or its class needs another (a Weapon_Sub outside the
support / vehicle categories goes to the support category of its soldier that holds its ammo class). Its template, the
EDF6 weapon of its category and class (the same ammo class first) whose level is nearest, gives the table columns EDF5
lacks, the placeholder an uninstall leaves, and a converted Weapon_Sub's custom_parameter form. A converted weapon also
takes its category and ammo class's AmmoDamageAttribute (EDF6's shield multiplier, Edf6.damage_attribute).
A weapon naming a resource EDF6 lacks (Root.cpk, Chunk01/02.cpk, SOUND/PC; followed into every SGO it names) is listed
under 'skipped': the decoy launchers and three vehicle skins, whose models are EDF5's alone (docs/edf5-weapons-plan.md
P3). The texts are EDF5's (JA, EN, CN, KR) with curves in EDF6's 7-element form, the numbers of an 'edf6' weapon's
changed fields its own; SC is CN through OpenCC t2s (pip install opencc-python-reimplemented).
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
import dsgo  # noqa: E402
import edf5port  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

STEAM = r'D:\Steam\steamapps\common'
OUT = os.path.join(HERE, '..', 'edf5port', 'weapons.json')
# The weapons that need files EDF6 lacks, converted from EDF5 (pylib/legacy_assets.py): their own registry, released
# after edf41port's (tools/ported_weapons.py REGISTRIES).
OUT_MODELS = os.path.join(HERE, '..', 'edf5port', 'models.json')
LANGS5 = ('JA', 'EN', 'CN', 'KR')
ID_PREFIX = 'EDF6VC_E5_'
# EDF5 categories EDF6 has no slot for -> the one that keeps the weapon in the slot EDF5 had it in:
#   304 Air Raider sentry guns / beetles / Robot Bomb D (weapon slots 1-3) -> 305 Weapon_Engineer_Special (slots 1-3)
#   107 Wing Diver thrown cluster weapons (weapon slots) -> 105 Weapon_Pale_Explosive (weapon slots)
#   108 Wing Diver cores -> 120 Weapon_Pale_EnergyCore (the core slot)
TARGET = {304: 305, 107: 105, 108: 120}
ARCHIVES = ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk')
FPS = 60.0


def key(s: str) -> str:
    return re.sub(r'\s+', '', unicodedata.normalize('NFKC', s or ''))


def words(s: str) -> str:
    """An English name as its words in order of spelling: EDF5's 'Robot Bomb Type D' is EDF6's 'Type D Robot Bomb'
    (the Japanese moved too: ロボットボムＤ型 / Ｄ型ロボットボム)."""
    return ' '.join(sorted(re.findall(r'\w+', unicodedata.normalize('NFKC', s or '').lower())))


def soldier(category: int) -> int:
    return category // 100 if category < 300 else 3


def sgo_name(path: str) -> str:
    return path.rsplit('/', 1)[-1]


def _strings(v: object) -> list[str]:
    """Every string leaf of a plain SGO value."""
    if isinstance(v, dict):
        return [x for c in v.values() for x in _strings(c)]
    if isinstance(v, list):
        return [x for c in v for x in _strings(c)]
    return [v] if isinstance(v, str) else []


class Edf6:
    """EDF6's weapons and resources, read only."""

    def __init__(self, root: str) -> None:
        self.games = [rootcpk.Game(root, a) for a in ARCHIVES if os.path.isfile(os.path.join(root, a))]
        self.game = self.games[0]
        self.files = {f'{d}/{n}'.upper() for g in self.games for d, n in g.cpk.index}
        # app:/sound/adx/<bank>.acb is a loose file, <game>/SOUND/PC/<BANK>.ACB (the banks are not in the archives)
        sounds = os.path.join(root, 'SOUND', 'PC')
        self.sounds = {n.upper() for n in os.listdir(sounds)} if os.path.isdir(sounds) else set()
        self.rows = sgo.load(data=self.game.read('WEAPON', 'WEAPONTABLE.SGO'))['table']
        self.tabled = {sgo_name(r[1]).upper() for r in self.rows}
        names = {lang: [r[0] for r in sgo.load(data=self.game.read('WEAPON', f'WEAPONTEXT.{lang}.SGO'))['text_table']]
                 for lang in ('JA', 'EN')}
        self.names = {key(n) for lang in names for n in names[lang]} | {words(n) for n in names['EN']}
        self._sgo: dict[str, dict] = {}
        self._named: set[str] | None = None

    def weapon(self, file: str) -> dict:
        if file.upper() not in self._sgo:
            self._sgo[file.upper()] = sgo.load(data=self.game.read('WEAPON', file))
        return self._sgo[file.upper()]

    def has_file(self, folder: str, file: str) -> bool:
        return f'{folder}/{file}'.upper() in self.files

    def has(self, path: str) -> bool:
        rel = path[len('app:/'):].upper()
        if rel.startswith('SOUND/ADX/'):
            return rel[len('SOUND/ADX/'):] in self.sounds
        return rel in self.files

    def named(self) -> set[str]:
        """Every app:/ path some EDF6 weapon names: what the game takes there, file or not ('app:/weapon/icon/none',
        the no-icon mark of every support item)."""
        if self._named is None:
            self._named = {p.lower() for r in self.rows for p in _strings(self.weapon(sgo_name(r[1])))
                           if p.lower().startswith('app:/')}
        return self._named

    def missing(self, paths: set[str], seen: set[str] | None = None) -> set[str]:
        """The paths among `paths` EDF6 lacks, following every SGO it has to the paths that one names (a vehicle
        call's object SGO is EDF6's, but a skin's names a model EDF6 lacks)."""
        seen = set() if seen is None else seen
        out: set[str] = set()
        for p in paths:
            if p.lower() in seen:
                continue
            seen.add(p.lower())
            if not self.has(p):
                if p.lower() not in self.named():
                    out.add(p)
                continue
            if p.lower().endswith('.sgo'):
                folder, name = p[len('app:/'):].split('/', 1)
                g = next(g for g in self.games if (folder.upper(), name.upper()) in
                         {(d.upper(), n.upper()) for d, n in g.cpk.index})
                inner = sgo.load(data=g.read(folder, name))
                out |= self.missing({x for x in _strings(inner) if x.lower().startswith('app:/')}, seen)
        return out

    def leftover(self, file: str, members5: dict, names: list[str]) -> bool:
        """EDF6 ships this file with the EDF5 weapon's model, its table never names it, and it is the same weapon: the
        file carries one of the weapon's names, or it is a DLC_ file (named for its one item; EDF6 renamed EDF5's
        Volcanic Cracker Volcanic Firecracker). A numbered file is no proof alone: EDF6 reused them for other weapons
        (its untabled eWeapon182 is a Power Assist Gun, EDF5's a Life Spout Gun, on the same support-unit model)."""
        if not self.has_file('WEAPON', file) or file.upper() in self.tabled:
            return False
        mine = self.weapon(file)
        model = lambda m: (m.get('animation_model') or [None])[0]  # noqa: E731
        if model(mine) != model(members5):
            return False
        own = {key(mine.get(f'name.{lang}', '')) for lang in ('ja', 'en')}
        return bool(own & {key(n) for n in names}) or file.upper().startswith('DLC_')

    def sub_categories(self) -> set[int]:
        """The categories whose weapons are all Weapon_Sub but for a 'no equipment' row (support / vehicle slots)."""
        by: dict[int, list[str]] = {}
        for r in self.rows:
            by.setdefault(int(r[2]), []).append(self.weapon(sgo_name(r[1])).get('xgs_scene_object_class'))
        return {c for c, classes in by.items() if classes.count(edf5port.SUB) >= len(classes) - 1}

    def category_for(self, category: int, cls: str, ammo: str, subs: set[int]) -> int:
        """`category`, unless a Weapon_Sub goes elsewhere: the support category of the same soldier holding most
        Weapon_Subs of its ammo class."""
        if cls != edf5port.SUB or category in subs:
            return category
        count: dict[int, int] = {}
        for r in self.rows:
            c = int(r[2])
            w = self.weapon(sgo_name(r[1]))
            if c in subs and soldier(c) == soldier(category) and w.get('AmmoClass') == ammo:
                count[c] = count.get(c, 0) + 1
        if not count:
            raise SystemExit(f'no EDF6 support category of soldier {soldier(category)} holds {ammo}')
        return max(count, key=lambda c: (count[c], -c))

    def damage_attribute(self, category: int, ammo: str) -> dict | None:
        """AmmoDamageAttribute ({'shield': damage multiplier on shields}, EDF6's alone) of most EDF6 weapons of this
        category and ammo class: a family rule (31 of the 35 solid-round sniper rifles 1.5, every laser 2.0), so a
        converted weapon of the family gets it too. None when most have none."""
        count: dict[str, int] = {}
        for r in self.rows:
            w = self.weapon(sgo_name(r[1]))
            if int(r[2]) == category and w.get('AmmoClass') == ammo:
                k = json.dumps(w.get('AmmoDamageAttribute'), sort_keys=True)
                count[k] = count.get(k, 0) + 1
        return json.loads(max(count, key=lambda k: (count[k], k))) if count else None

    def template(self, category: int, cls: str, ammo: str, level: float) -> str:
        rows = [r for r in self.rows if int(r[2]) == category and float(r[3]) == 1.0]
        rows = [r for r in rows if self.weapon(sgo_name(r[1])).get('xgs_scene_object_class') == cls] or rows
        same = [r for r in rows if self.weapon(sgo_name(r[1])).get('AmmoClass') == ammo]
        pool = same or rows
        if not pool:
            raise SystemExit(f'EDF6 has no weapon in category {category}')
        return min(pool, key=lambda r: (abs(float(r[4]) - level), r[0]))[0]


def restat(text: list, old: dict, new: dict) -> list:
    """A text row's stat curves for a weapon whose fields changed from `old` to `new` (EDF6's own copy): a curve that
    shows a changed field (its star parameters the field's, its base the field's base in the field's unit, per second
    or in seconds) takes the new field's numbers."""
    out = json.loads(json.dumps(text))
    def field(m: dict, k: str) -> object:
        v, at = m.get(k), edf5port.CURVES[k]
        return v[at] if at is not None and isinstance(v, list) and len(v) > at else v

    changed = [(k, field(old, k), field(new, k)) for k in edf5port.CURVES]
    changed = [(k, a, b) for k, a, b in changed if isinstance(a, list) and len(a) == 6 and isinstance(b, list)
               and len(b) == 7 and a != b[:6]]
    for stat in out[2]:
        for c in stat[2:]:
            if not (isinstance(c, list) and len(c) == 7):
                continue
            for _k, was, now in changed:
                for f in (1.0, FPS, 1.0 / FPS):
                    if c[1:6] == list(was[1:6]) and abs(c[0] - was[0] * f) <= 1e-6 * max(1.0, abs(c[0])):
                        c[0], c[1:6] = now[0] * f, list(now[1:6])
                        break
    return out


def _t2s(v: object, t2s) -> object:  # noqa: ANN001 - opencc.OpenCC
    if isinstance(v, str):
        return t2s.convert(v)
    if isinstance(v, list):
        return [_t2s(x, t2s) for x in v]
    return v


def convertible(g5: rootcpk.Game, missing: list[str]) -> tuple[dict[str, str], str | None]:
    """The files EDF6 lacks as {Mods path: EDF5 file} (EDF6 names a model .mrab where EDF5 has the same archive as
    .rab), each converted once here so the install will not meet a refusal, or ({}, why one cannot be)."""
    import legacy_assets
    index = {(d.upper(), n.upper()): (d, n) for d, n in g5.cpk.index}
    out: dict[str, str] = {}
    for path in missing:
        folder, name = path[len('app:/'):].split('/', 1)
        stem, ext = os.path.splitext(name)
        found = next((index[(folder.upper(), n.upper())] for n in (name, stem + '.rab') if (folder.upper(), n.upper())
                      in index and (ext.lower() == '.mrab' or n == name)), None)
        if found is None:
            return {}, f'EDF6 没有、EDF5 也没有：{path}'
        source = f'{found[0]}/{found[1]}'
        try:
            legacy_assets.convert(source, g5.read(*found))
        except ValueError as e:   # a converter's refusal; anything else is this machine's problem, raised
            return {}, f'EDF6 没有、不能从 EDF5 转换：{path}（{e}）'
        out[f'{folder.upper()}/{name.upper()}'] = source
    return out, None


def build(edf5: str, edf6: str) -> dict:
    import opencc
    t2s = opencc.OpenCC('t2s')
    g5 = rootcpk.Game(edf5)
    e6 = Edf6(edf6)
    subs = e6.sub_categories()
    rows5 = sgo.read(g5.read('WEAPON', 'WEAPONTABLE.SGO'))[1]['table']
    texts5 = {lang: sgo.read(g5.read('WEAPON', f'WEAPONTEXT.{lang}.SGO'))[1]['text_table'] for lang in LANGS5}
    weapons, skipped = [], []
    for i, row in enumerate(rows5):
        sgo_id, path, cat5, _one, level, _acquire, stars, pack = row
        names = [texts5[lang][i][0] for lang in ('JA', 'EN')]
        if key(names[0]) in e6.names or key(names[1]) in e6.names or words(names[1]) in e6.names:
            continue
        file = sgo_name(path)
        members = sgo.read(g5.read('WEAPON', file))[1]
        plain = sgo.plain(members)
        source = 'edf6' if e6.leftover(file, plain, names) else 'edf5'
        own = e6.weapon(file) if source == 'edf6' else plain
        missing = sorted(e6.missing({p for p in _strings(own) if p.lower().startswith('app:/')}))
        assets, why = convertible(g5, missing)
        if why:
            skipped.append({'sgo': sgo_id, 'name': names[0], 'reason': why})
            continue
        category = TARGET.get(int(cat5), int(cat5))
        cls = own['xgs_scene_object_class']
        if source == 'edf5' and category in subs and cls == 'Weapon_BasicShoot':
            cls = edf5port.SUB
        category = e6.category_for(category, cls, own.get('AmmoClass', ''), subs)
        level = float(sgo.plain(level))
        text = {lang: edf5port.text_value(texts5[lang][i]) for lang in LANGS5}
        if source == 'edf6':
            text = {lang: restat(t, plain, own) for lang, t in text.items()}
        text['SC'] = _t2s(text['CN'], t2s)
        if source == 'edf6':   # EDF6's own names for its copy (its newer translations, and SC)
            for lang, t in text.items():
                t[0] = own.get(f'name.{lang.lower()}') or t[0]
        weapons.append({
            'id': ID_PREFIX + sgo_id.upper(), 'sgo': file, 'source': source, 'edf5_row': i,
            'edf5_category': int(cat5), 'category': category, 'class': cls, 'level': level,
            'stars': [int(x) for x in stars], 'pack': int(pack),
            'template': e6.template(category, cls, own.get('AmmoClass', ''), level), 'text': text,
            'damage_attribute': e6.damage_attribute(category, own.get('AmmoClass', '')) if source == 'edf5' else None,
            **({'assets': assets} if assets else {}),
            # The weapon itself, converted here (edf5port.weapon): the install builds it from this, with no EDF5.
            **({'weapon': dsgo.dump(edf5port.weapon(members, {lang.lower(): t[0] for lang, t in text.items()}).root)}
               if source == 'edf5' else {}),
        })
    return {'source': 'EDF5 Root.cpk WEAPON/WEAPONTABLE.SGO + WEAPONTEXT.*; SC = CN via OpenCC t2s',
            'weapons': weapons, 'skipped': skipped}


def keep_order(path: str, weapons: list[dict]) -> list[dict]:
    """`weapons` in the order the registry at `path` already has them, new ones after (tools/ported_weapons.py
    RELEASED: an installed row never moves, so a released registry only grows at its end). A weapon the old registry
    has and this run lost is an error: its row would be nobody's."""
    if not os.path.isfile(path):
        return weapons
    with open(path, encoding='utf-8') as f:
        old = [w['id'] for w in json.load(f)['weapons']]
    by = {w['id']: w for w in weapons}
    lost = [i for i in old if i not in by]
    if lost:
        raise SystemExit(f'{path}: these weapons are no longer found: {", ".join(lost)} (a released row cannot go)')
    return [by[i] for i in old] + [w for w in weapons if w['id'] not in set(old)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--edf5', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 5'))
    ap.add_argument('--edf6', default=os.path.join(STEAM, 'EARTH DEFENSE FORCE 6'))
    a = ap.parse_args()
    data = build(a.edf5, a.edf6)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    for path, weapons in ((OUT, keep_order(OUT, [w for w in data['weapons'] if 'assets' not in w])),
                          (OUT_MODELS, keep_order(OUT_MODELS, [w for w in data['weapons'] if 'assets' in w]))):
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            json.dump({**data, 'weapons': weapons, **({'skipped': data['skipped']} if path == OUT else {'skipped': []})},
                      f, ensure_ascii=False, indent=1)
            f.write('\n')
        print(f'{path}: {len(weapons)} weapons')
    print(f'{len(data["skipped"])} skipped')
    for s in data['skipped']:
        print(f'  skipped {s["sgo"]} {s["name"]}: {s["reason"][:160]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
