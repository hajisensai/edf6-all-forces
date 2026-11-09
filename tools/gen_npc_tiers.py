"""Writes src/npc_tiers.inc and docs/npc-weapon-tiers.md: the player weapons each support soldier kind may carry, in
series, and each weapon's previous model in its series (the maintainer, 2026-11-08: a called support soldier carries "与玩家
同数值的武器，但比叫援军的玩家低一档"; docs/feature-npc-weapon-tier.md).

The table is made from docs/weapons.csv (the weapon table's rows: id, class, category, WEAPONTABLE name, English name,
level) and SERIES below: for each soldier kind (src/support_loadout.h kSupportKindNames, SupportWeapon order) the weapon
table category its weapon comes from and its series, each one a regular expression on the English name. A weapon of the
category matching none of the kind's series is not one the kind carries (an acid gun is no flamethrower); one matching
two is an error. In a series the weapons are ordered by their level (column 4, docs/weapons.csv level_raw), then by id:
a weapon's previous model is the one before it there.

Which series a kind has follows the soldier's own stock AI weapon (Root.cpk, read 2026-11-08): the player weapons of the
same model (AiSoldierRifle01 s_assault_af14.mdb: the PA-11; AiLaserLance01 p_lance_lazerlance01.mdb: the Power Lance;
AiIZNR01 p_thunder_izuna01.mdb: the Lightning Bow; AiTB01 p_thunder_thunderbow01.mdb: the Thunder Crossbow;
AiHeavyCannon01 h_cannon_normal01.mdb: the NC hand cannons; AiHeabyMiddleCannon01 / AiHeavyShotGUN01
h_cannon_titanianormal01.mdb: the Galleon cannons / the Dexter shotguns; AiHeavyPileBanker01 h_pile_normal01.mdb: the
Blasthole Spear; ...) and the other series of their kind of weapon in that category.

  python tools/gen_npc_tiers.py            write both files
  python tools/gen_npc_tiers.py --check    exit 1 when either committed file is not what this would write (CI)
"""
from __future__ import annotations

import csv
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
CSV = os.path.join(ROOT, 'docs', 'weapons.csv')
OUT = os.path.join(ROOT, 'src', 'npc_tiers.inc')
DOC = os.path.join(ROOT, 'docs', 'npc-weapon-tiers.md')

# (kind, its label, its class in docs/weapons.csv, the category, [(series, regular expression on name_en)]), in
# SupportWeapon order. The X-Morph infantry (last) carries none: it is no EDF soldier (its own X-Morph weapons).
SERIES: tuple[tuple[str, str, str, int, tuple[tuple[str, str], ...]], ...] = (
    ('rifle', '游骑兵·步枪', 'Ranger', 0, (
        ('PA-11', r'PA-11'), ('Minion', r'^Minion '), ('Slugger NN', r'^Slugger NN'), ('G&M', r'^G&M-'),
        ('Raven', r'Raven$'), ('Stork', r'Stork'), ('Blazer', r'Blazer$'), ('Orchid', r'-Orchid$'),
        ('Sirius', r'Sirius$'), ('Slade', r'Slade$'))),
    ('flame', '游骑兵·火焰', 'Ranger', 6, (
        ('Burner', r'^Burner$'), ('Flamethrower', r'Flamethrower'), ('Flame Cannon', r'^(Flame|Magma) Cannon$'))),
    ('rocket', '游骑兵·火箭', 'Ranger', 3, (
        ('Grant', r'^Grant '), ('Cascade', r'^Cascade '), ('Leopard', r'^Leopard '), ('Goliath', r'^Goliath '),
        ('Hornet', r'Hornet$'), ('Volcano', r'Volcano'), ('Ibis', r'^Ibis '))),
    ('shotgun', '游骑兵·霰弹', 'Ranger', 1, (
        ('Slaughter', r'^Slaughter '), ('SGN', r'^SGN-'), ('Slugger Shot', r'^Slugger Shot'), ('Herring', r'Herring$'),
        ('Breacher', r'Breacher$'), ('Monsoon', r'^Monsoon'), ('Blazer', r'Blazer'), ('Buster Shot', r'^Buster Shot$'),
        ('Fracture', r'^Fracture$'))),
    ('sniper', '游骑兵·狙击', 'Ranger', 2, (
        ('KFF', r'^KFF '), ('NP', r'^NP\d'), ('Flounder', r'^Flounder '), ('Lysander', r'Lysander'), ('Fang', r'Fang$'),
        ('Dunkel', r'^Dunkel '), ('Eagle', r'^Eagle '), ('Ptarmigan', r'^Ptarmigan '), ('Pylon', r'^Pylon '),
        ('Penetrator', r'^Penetrator'), ('Planet', r'^Planet '))),
    ('lance', '翼人·长矛', 'WingDiver', 100, (
        ('Power Lance', r'^Power Lance'), ('Dragoon Lance', r'^Dragoon Lance'),
        ('Multi Lance', r'^(Double|Triple|Twin) Lance'), ('Monster Lance', r'^Monster Lance$'))),
    ('laser', '翼人·激光', 'WingDiver', 101, (
        ('Mag Blaster', r'Mag Blaster'), ('Destroyer Blaster', r'^Destroyer Blaster'), ('Reflectron', r'^Reflectron '),
        ('Spine Blaster', r'^Spine Blaster'))),
    ('monster', '翼人·狙击', 'WingDiver', 104, (
        ('Closed Laser', r'^Closed (Long )?Laser'), ('Bolt Shooter', r'^Bolt Shooter'), ('Monster', r'^Monster'),
        ('Raijin', r'^Raijin'), ('Cosmic Laser', r'^Cosmic Laser'))),
    ('izuna', '翼人·雷链', 'WingDiver', 102, (
        ('Lightning Bow', r'^Lightning (Rapid )?Bow'),)),
    ('thunderbow', '翼人·雷弓', 'WingDiver', 102, (
        ('Thunder Crossbow', r'^Thunder Crossbow'),)),
    ('cannon', '重装·重炮', 'Fencer', 204, (
        ('Hand Cannon', r'Hand Cannon$'), ('Cannon Shot', r'Cannon Shot$'), ('Canister Cannon', r'Canister Cannon$'))),
    ('midcannon', '重装·中炮', 'Fencer', 203, (
        ('Galleon', r'^Galleon '),)),
    ('pilebanker', '重装·打桩', 'Fencer', 201, (
        ('Blasthole Spear', r'^Blasthole Spear'), ('Blast Twin Spears', r'^Blast Twin Spears'))),
    ('fshotgun', '重装·霰弹', 'Fencer', 203, (
        ('Dexter', r'^Dexter Automatic Shotgun'),)),
    ('xminfantry', 'X-Morph 步兵', '', -1, ()),
)
KIND_NAMES = tuple(k[0] for k in SERIES)


def rows() -> list[dict]:
    with open(CSV, encoding='utf-8-sig', newline='') as f:
        return list(csv.DictReader(f))


def table() -> list[dict]:
    """Every (kind, series, weapon) row: {'kind', 'series', 'id', 'prev' (-1: first of its series), 'level', 'sgo',
    'name'}, by kind, then series, then level."""
    weapons = rows()
    out: list[dict] = []
    for kind_index, (kind, _, cls, category, series) in enumerate(SERIES):
        members: dict[int, list[dict]] = {i: [] for i in range(len(series))}
        for w in weapons:
            if w['class'] != cls or int(w['category']) != category:
                continue
            hits = [i for i, (_, pattern) in enumerate(series) if re.search(pattern, w['name_en'])]
            if len(hits) > 1:
                raise ValueError(f'{kind}: {w["name_en"]} matches series {[series[i][0] for i in hits]}')
            if hits:
                members[hits[0]].append(w)
        for i, (name, _) in enumerate(series):
            if not members[i]:
                raise ValueError(f'{kind}: series {name} has no weapon')
            prev = -1
            for w in sorted(members[i], key=lambda w: (float(w['level_raw']), int(w['id']))):
                out.append({'kind': kind_index, 'series': i, 'id': int(w['id']), 'prev': prev,
                            'level': round(float(w['level_raw']) * 1000), 'sgo': w['sgo_name'], 'name': w['name_en']})
                prev = int(w['id'])
    ids = [(r['kind'], r['id']) for r in out]
    if len(ids) != len(set(ids)):
        raise ValueError('a weapon is in two series of one kind')
    return out


def render() -> str:
    lines = ['// Generated by tools/gen_npc_tiers.py from docs/weapons.csv and its SERIES. Do not edit: python tools/gen_npc_tiers.py',
             '// {kind (SupportWeapon), series (its index in the kind\'s SERIES), weapon id (the weapon table row), the previous',
             '//  model in its series (-1: none), level x 1000 (docs/weapons.csv level_raw), WEAPONTABLE name}',
             'inline constexpr TierRow kTierRows[]={']
    for r in table():
        lines.append(f'    {{{r["kind"]},{r["series"]},{r["id"]},{r["prev"]},{r["level"]},L"{r["sgo"]}"}},   // {r["name"]}')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def render_doc() -> str:
    t = table()
    lines = ['# 援军士兵的玩家武器系列表（生成文件）', '',
             '由 `tools/gen_npc_tiers.py` 按 `docs/weapons.csv` 与脚本里的 `SERIES` 生成，不要手改。'
             '规则见 [feature-npc-weapon-tier.md](feature-npc-weapon-tier.md)：同一系列里按等级排，「前一型号」是排在它前面的那把。', '']
    for kind_index, (kind, label, cls, category, series) in enumerate(SERIES):
        if not series:
            lines += [f'## {label}（`{kind}`）', '', '不带玩家武器（X-Morph 步兵用自己的 X-Morph 武器，仍按原版规则）。', '']
            continue
        lines += [f'## {label}（`{kind}`，{cls} 类别 {category}）', '']
        for i, (name, pattern) in enumerate(series):
            chain = ' → '.join(f'{r["name"]}（{r["id"]}）' for r in t if r['kind'] == kind_index and r['series'] == i)
            lines.append(f'- {name}（`{pattern}`）：{chain}')
        lines.append('')
    return '\n'.join(lines)


def main(argv: list[str]) -> int:
    check = '--check' in argv
    status = 0
    for path, text in ((OUT, render()), (DOC, render_doc())):
        if check:
            try:
                with open(path, encoding='utf-8', newline='') as f:
                    have = f.read().replace('\r\n', '\n')
            except OSError:
                have = None
            if have != text:
                print(f'{os.path.relpath(path, ROOT)} is stale: python tools/gen_npc_tiers.py', file=sys.stderr)
                status = 1
        else:
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
            print(f'wrote {os.path.relpath(path, ROOT)}')
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
