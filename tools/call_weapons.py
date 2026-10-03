"""Air Raider call weapons for the plugin's jets and helicopters: 16 rows appended to the shared
weapon table, each weapon a clone of the stock eWeapon051 (Combat Bomber KM6, Weapon_RadioContact,
category 312) with a marker in its SGO's AmmoHitSizeAdjust. The plugin reads the marker (weapon +0x8C4)
and flies its own planes for the call; the field does nothing for a RadioContact weapon, so without the
plugin the weapon is simply a working KM6 bomber call.

  python tools/call_weapons.py build OUTDIR [--game DIR]     write the 16 SGOs + the stacked tables into OUTDIR
  python tools/call_weapons.py install [--game DIR]          into <game>/Mods (refuses while EDF6 runs)
  python tools/call_weapons.py uninstall --unequipped [--force] [--game DIR]
  python tools/call_weapons.py check [--game DIR]

The weapon table and its texts (WEAPONTEXT.<LANG>.SGO, index-aligned with the table) are shared with
other mods (autoturret/tools/describe.py rewrites text rows in place), so the base is always the
installed Mods/WEAPON copy when there is one, else Root.cpk. Our rows are one contiguous block:
appended on first install, replaced in place on reinstall (saves refer to weapons by row index, so they
keep their index). New calls are only ever appended to CALLS: an installed block that is a prefix of it
(an older install) grows in place, as long as no other mod's rows follow it (those would move). Every
other row is checked unchanged before anything is written.

install backs up every Mods file it overwrites the first time into Mods/.edf6vc_backup/ and records what it
wrote in Mods/.edf6vc_calls.json. uninstall cuts only our rows out of the CURRENT shared files (never rolls
back to the backup: other mods' later edits stay), writes the texts back, and deletes WEAPONTABLE.SGO only
when we created it and what is left is byte-identical to the stock one.

A save that still has one of these weapons equipped crashes the game at the main menu once its row is
gone (the menu looks the weapon up by row index past the table end; see edf6-jaeger tools/install.py):
unequip them in every save first and confirm with --unequipped.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from functools import lru_cache

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'autoturret', 'tools'))
sys.path.insert(0, os.path.join(HERE, '..', 'testrange'))
import dsgo  # noqa: E402
import gen  # noqa: E402
from dsgo import Node  # noqa: E402

TEMPLATE = 'eWeapon051'          # Combat Bomber KM6
LANGS = ('JA', 'EN', 'CN', 'KR', 'SC')
TABLE = 'WEAPON/WEAPONTABLE.SGO'
TEXTS = [f'WEAPON/WEAPONTEXT.{lang}.SGO' for lang in LANGS]
SHARED = [TABLE] + TEXTS
BACKUP = '.edf6vc_backup'
MANIFEST = '.edf6vc_calls.json'
ACQUIRE = 0.0   # WEAPONTABLE column 5: 0 normal (the plugin makes EDF6VC_CALL_* owned at every save load)
PROCESS = 'EDF6.exe'


@dataclass(frozen=True)
class Call:
    id: str
    mark: float          # AmmoHitSizeAdjust; the plugin's call marker
    kind: str            # key into KINDS
    follow: bool         # escorts the caller instead of holding the marked point
    planes: float        # Ammo_CustomParameter[2][1]: the stock bomber fallback's plane count
    reload: float        # ReloadTime[0], the base of the star curve
    level: float         # WEAPONTABLE column 4, same units as docs/weapons.csv level_raw


CALLS: tuple[Call, ...] = (
    Call('EDF6VC_CALL_INTERCEPTOR', 7101, 'interceptor', False, 2, 900, 0.3),
    Call('EDF6VC_CALL_INTERCEPTOR_F', 7102, 'interceptor', True, 2, 1035, 0.5),
    Call('EDF6VC_CALL_STRIKE', 7103, 'strike', False, 3, 1500, 0.5),
    Call('EDF6VC_CALL_STRIKE_F', 7104, 'strike', True, 3, 1725, 0.7),
    Call('EDF6VC_CALL_MULTIROLE', 7105, 'multirole', False, 3, 1800, 0.8),
    Call('EDF6VC_CALL_MULTIROLE_F', 7106, 'multirole', True, 3, 2070, 1.0),
    Call('EDF6VC_CALL_FIGHTER', 7107, 'fighter', False, 4, 2000, 1.0),
    Call('EDF6VC_CALL_FIGHTER_F', 7108, 'fighter', True, 4, 2300, 1.2),
    Call('EDF6VC_CALL_CARRIER', 7109, 'carrier', False, 1, 3000, 1.8),
    Call('EDF6VC_CALL_CARRIER_F', 7110, 'carrier', True, 1, 3450, 2.0),
    Call('EDF6VC_CALL_HELI', 7111, 'heli', False, 2, 1600, 0.4),
    Call('EDF6VC_CALL_HELI_F', 7112, 'heli', True, 2, 1800, 0.6),
    # Appended 2026-10-04: carriers whose drones blow themselves up next to the enemy.
    Call('EDF6VC_CALL_BLAST_CARRIER', 7113, 'blast_carrier', False, 1, 3300, 2.0),
    Call('EDF6VC_CALL_BLAST_CARRIER_F', 7114, 'blast_carrier', True, 1, 3800, 2.2),
    Call('EDF6VC_CALL_DOLL_CARRIER', 7115, 'doll_carrier', False, 1, 3600, 2.2),
    Call('EDF6VC_CALL_DOLL_CARRIER_F', 7116, 'doll_carrier', True, 1, 4100, 2.4),
)
IDS: tuple[str, ...] = tuple(c.id for c in CALLS)

# Per kind: name and what it does, per language (KR reuses EN).
KINDS: dict[str, dict[str, tuple[str, str]]] = {
    'interceptor': {
        'SC': ('截击机', '呼叫截击机，优先攻击空中目标；比制空战斗机飞得更快更高，并从更远处发射导弹。'),
        'CN': ('截擊機', '呼叫截擊機，優先攻擊空中目標；比制空戰鬥機飛得更快更高，並從更遠處發射飛彈。'),
        'JA': ('迎撃機', '迎撃機を要請する。空中の敵を優先して攻撃する。制空戦闘機より速く高く飛び、遠くからミサイルを撃つ。'),
        'EN': ('Interceptors', 'Calls interceptors that attack flying targets first, faster and higher than fighters, firing their missiles from farther out.'),
    },
    'strike': {
        'SC': ('对地攻击机', '呼叫对地攻击机，优先攻击地面目标，俯冲投弹。'),
        'CN': ('對地攻擊機', '呼叫對地攻擊機，優先攻擊地面目標，俯衝投彈。'),
        'JA': ('対地攻撃機', '対地攻撃機を要請する。地上の敵を優先し、急降下して爆撃する。'),
        'EN': ('Strike Fighters', 'Calls strike fighters that attack ground targets first, diving onto them with bombs.'),
    },
    'multirole': {
        'SC': ('多用途机', '呼叫多用途战斗机，攻击最近的目标，空中地面皆可。'),
        'CN': ('多用途機', '呼叫多用途戰鬥機，攻擊最近的目標，空中地面皆可。'),
        'JA': ('マルチロール機', 'マルチロール機を要請する。空中・地上を問わず最も近い敵を攻撃する。'),
        'EN': ('Multirole Fighters', 'Calls multirole fighters that attack the nearest target, in the air or on the ground.'),
    },
    'fighter': {
        'SC': ('制空战斗机', '呼叫制空战斗机，优先攻击空中目标。'),
        'CN': ('制空戰鬥機', '呼叫制空戰鬥機，優先攻擊空中目標。'),
        'JA': ('制空戦闘機', '制空戦闘機を要請する。空中の敵を優先して攻撃する。'),
        'EN': ('Air Superiority Fighters', 'Calls air superiority fighters that attack flying targets first.'),
    },
    'carrier': {
        'SC': ('无人机母舰', '呼叫无人机母舰：在空中盘旋，派出无人机攻击范围内的敌人。'),
        'CN': ('無人機母艦', '呼叫無人機母艦：在空中盤旋，派出無人機攻擊範圍內的敵人。'),
        'JA': ('無人機母艦', '無人機母艦を要請する。上空を旋回し、範囲内の敵へ無人機を送り込む。'),
        'EN': ('Drone Carrier', 'Calls a drone carrier that hovers overhead and sends its drones at enemies in range.'),
    },
    'blast_carrier': {
        'SC': ('自爆无人机母舰', '呼叫自爆无人机母舰：悬停在空中，放出近炸无人机，冲到敌人身边自爆。'),
        'CN': ('自爆無人機母艦', '呼叫自爆無人機母艦：懸停在空中，放出近炸無人機，衝到敵人身邊自爆。'),
        'JA': ('自爆ドローン母艦', '自爆ドローン母艦を要請する。上空に滞空し、敵に突っ込んで近接起爆するドローンを放つ。'),
        'EN': ('Blast Drone Carrier', 'Calls a carrier that hovers overhead and sends drones that dive at the enemy and blow up next to it.'),
    },
    'doll_carrier': {
        'SC': ('人偶无人机母舰', '呼叫人偶无人机母舰：放出挂着会唱歌跳舞的人偶的无人机，慢慢飞到敌人中间吸引火力，然后自爆。'),
        'CN': ('人偶無人機母艦', '呼叫人偶無人機母艦：放出掛著會唱歌跳舞的人偶的無人機，慢慢飛到敵人中間吸引火力，然後自爆。'),
        'JA': ('人形ドローン母艦', '人形ドローン母艦を要請する。歌って踊る人形を吊るしたドローンが敵の中へ進み、注意を引いてから自爆する。'),
        'EN': ('Doll Drone Carrier', 'Calls a carrier whose drones carry a singing, dancing doll into the enemy, draw their fire, and blow up.'),
    },
    'heli': {
        'SC': ('武装直升机', '呼叫武装直升机，攻击附近的敌人。'),
        'CN': ('武裝直升機', '呼叫武裝直升機，攻擊附近的敵人。'),
        'JA': ('武装ヘリ', '武装ヘリを要請する。付近の敵を攻撃する。'),
        'EN': ('Gunships', 'Calls gunships that attack nearby enemies.'),
    },
}
MODES: dict[str, tuple[tuple[str, str], tuple[str, str]]] = {  # (hold, follow): (name suffix, sentence)
    'SC': (('·守点', '守在标记的地点上空。'), ('·跟随', '跟随呼叫者行动。')),
    'CN': (('·守點', '守在標記的地點上空。'), ('·跟隨', '跟隨呼叫者行動。')),
    'JA': (('（拠点）', 'マーカーで指定した地点の上空を守る。'), ('（随伴）', '要請した隊員に随伴する。')),
    'EN': ((' (Hold)', 'They hold the marked point.'), (' (Escort)', 'They escort the caller.')),
}
NOTES: dict[str, str] = {
    'SC': '需要 EDF6VehicleCrew 插件；未安装时为普通 KM6 轰炸。',
    'CN': '需要 EDF6VehicleCrew 插件；未安裝時為普通 KM6 轟炸。',
    'JA': 'EDF6VehicleCrew プラグインが必要。未導入時は通常の KM6 による爆撃になる。',
    'EN': 'Needs the EDF6VehicleCrew plugin; without it this is a plain KM6 bomber call.',
}


def _lang(lang: str) -> str:
    return 'EN' if lang == 'KR' else lang


def call_name(call: Call, lang: str) -> str:
    lang = _lang(lang)
    return KINDS[call.kind][lang][0] + MODES[lang][call.follow][0]


def call_description(call: Call, lang: str) -> str:
    lang = _lang(lang)
    sep = ' ' if lang == 'EN' else ''
    return KINDS[call.kind][lang][1] + sep + MODES[lang][call.follow][1] + '\n\n' + NOTES[lang]


def sgo_file(call: Call) -> str:
    """Mods/WEAPON file name, upper case like the other mod weapons there (EWEAPON389.SGO)."""
    return f'WEAPON/{call.id.upper()}.SGO'


# ---------------------------------------------------------------- reading the base


@lru_cache(maxsize=None)
def _game(game_root: str) -> gen.Game:
    return gen.Game(game_root)


def stock(game_root: str, rel: str) -> bytes:
    folder, name = rel.split('/')
    return _game(game_root).read(folder, name)


def base(game_root: str, rel: str) -> bytes:
    """The installed Mods copy when present, else Root.cpk."""
    path = os.path.join(game_root, 'Mods', *rel.split('/'))
    if os.path.isfile(path):
        with open(path, 'rb') as f:
            return f.read()
    return stock(game_root, rel)


def _rows(doc: dsgo.Document, rel: str) -> list:
    return doc.root.get('table' if rel == TABLE else 'text_table').items


def row_ids(table: bytes) -> list[str]:
    return [r.items[0] for r in dsgo.parse(table).root.get('table').items]


def our_count(ids: list[str]) -> int:
    """How many of our rows `ids` holds (an older install holds a prefix of IDS)."""
    return sum(1 for x in ids if x in IDS)


def our_block(ids: list[str]) -> int | None:
    """Index of our first row, None when none is present. Raises unless the rows there are IDS, or a
    prefix of it (an older install), contiguous and in order."""
    present = [i for i, x in enumerate(ids) if x in IDS]
    if not present:
        return None
    at = present[0]
    n = len(present)
    if ids[at:at + n] != list(IDS[:n]):
        raise ValueError(f'our rows are partial or out of order at {present}: '
                         f'{[ids[i] for i in present]}')
    return at


def _template_index(ids: list[str]) -> int:
    upper = [x.upper() for x in ids]
    return upper.index(TEMPLATE.upper())


# ---------------------------------------------------------------- building


def weapon_sgo(template: bytes, call: Call) -> bytes:
    doc = dsgo.parse(template)
    r = doc.root
    r.set('AmmoHitSizeAdjust', float(call.mark))
    reload = r.get('ReloadTime')
    reload.items[0] = float(call.reload)
    custom = r.get('Ammo_CustomParameter').items[2]
    custom.items[1] = float(call.planes)
    for lang in LANGS:
        key = f'name.{lang.lower()}'
        if key in r.names.values():
            r.set(key, call_name(call, lang))
    return dsgo.write(doc)


def _table_row(template: Node, call: Call) -> Node:
    row = copy.deepcopy(template)
    stock_path = template.items[1]
    prefix = stock_path[:stock_path.rfind('/') + 1]           # app:/weapon/
    suffix = stock_path[stock_path.rfind('.'):]                 # .sgo
    row.items[0] = call.id
    row.items[1] = f'{prefix}{call.id}{suffix}'
    row.items[4] = float(call.level)
    row.items[5] = ACQUIRE
    return row


def _text_row(template: Node, call: Call, lang: str) -> Node:
    row = copy.deepcopy(template)
    row.items[0] = call_name(call, lang)
    row.items[1] = call_description(call, lang)
    # The stat list stays KM6's, except the reload line's curve, which the game shows as $0pt
    # from that list: it must be the weapon's own ReloadTime or the menu shows KM6's 1020.
    for stat in row.items[2].items:
        if len(stat.items) == 3 and stat.items[1] == '$0pt' and isinstance(stat.items[2], Node):
            stat.items[2].items[0] = float(call.reload)
    return row


def _place(rows: list, new: list, at: int | None, old: int) -> int:
    """Puts the block of new rows at `at` (replacing our `old` rows) or appends it; returns where it went."""
    if at is None:
        at = len(rows)
        rows.extend(new)
    else:
        rows[at:at + old] = new
    return at


def stack(game_root: str) -> dict[str, bytes]:
    """The SGOs and the shared table + texts with our block in, keyed by path under Mods/."""
    table_doc = dsgo.parse(base(game_root, TABLE))
    rows = _rows(table_doc, TABLE)
    ids = [r.items[0] for r in rows]
    at = our_block(ids)
    old = our_count(ids)
    if at is not None and old < len(IDS) and ids[at + old:]:
        raise SystemExit(f'our block would grow by {len(IDS) - old} rows and move the rows after it '
                         f'(saves refer to them by index): {ids[at + old:]}')
    tpl = _template_index(ids)
    out: dict[str, bytes] = {}
    template_sgo = stock(game_root, f'WEAPON/{TEMPLATE.upper()}.SGO')
    for call in CALLS:
        out[sgo_file(call)] = weapon_sgo(template_sgo, call)
    new_rows = [_table_row(rows[tpl], call) for call in CALLS]
    placed = _place(rows, new_rows, at, old)
    out[TABLE] = dsgo.write(table_doc)
    for lang, rel in zip(LANGS, TEXTS):
        doc = dsgo.parse(base(game_root, rel))
        text = _rows(doc, rel)
        if len(text) != len(ids):  # text rows are index-aligned with the table
            raise ValueError(f'{rel}: {len(text)} rows, base table has {len(ids)}')
        new_text = [_text_row(text[tpl], call, lang) for call in CALLS]
        if _place(text, new_text, at, old) != placed:
            raise AssertionError(rel)
        out[rel] = dsgo.write(doc)
    verify(game_root, out)
    return out


def verify(game_root: str, out: dict[str, bytes]) -> None:
    """Before anything is written: every other row unchanged, the table grown by exactly the rows we did
    not have yet, our block contiguous, the texts as long as the table."""
    before = [r.items[0] for r in _rows(dsgo.parse(base(game_root, TABLE)), TABLE)]
    had = our_block(before) is not None
    old = our_count(before)
    after_ids = row_ids(out[TABLE])
    at = our_block(after_ids)
    if at is None:
        raise ValueError('our rows missing from the result')
    grown = len(after_ids) - len(before)
    if grown != len(IDS) - old:
        raise ValueError(f'table grew by {grown} rows (had {old} of ours)')
    for rel in SHARED:
        old_all = [dsgo.to_py(r) for r in _rows(dsgo.parse(base(game_root, rel)), rel)]
        new = [dsgo.to_py(r) for r in _rows(dsgo.parse(out[rel]), rel)]
        if len(new) != len(after_ids):
            raise ValueError(f'{rel}: {len(new)} rows, table has {len(after_ids)}')
        old_rest = old_all[:at] + old_all[at + old:]
        new_rest = new[:at] + new[at + len(IDS):]
        if old_rest != new_rest:
            bad = next(i for i, (a, b) in enumerate(zip(old_rest, new_rest)) if a != b)
            raise ValueError(f'{rel}: another row changed (row {bad} outside our block)')


def missing_rows(game_root: str) -> list[str]:
    """Our weapon ids not in the effective (Mods, else stock) weapon table."""
    ids = set(row_ids(base(game_root, TABLE)))
    return [x for x in IDS if x not in ids]


# ---------------------------------------------------------------- game dir


def game_running() -> bool:
    r = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {PROCESS}', '/NH'],
                       capture_output=True, text=True, errors='replace')
    return PROCESS.lower() in r.stdout.lower()


def _sha(path: str) -> str:
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def _manifest_path(game_root: str) -> str:
    return os.path.join(game_root, 'Mods', MANIFEST)


def _load_manifest(game_root: str) -> dict:
    path = _manifest_path(game_root)
    if os.path.isfile(path):
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    return {'created': [], 'replaced': [], 'written': {}}


def _save_manifest(game_root: str, manifest: dict) -> None:
    data = json.dumps(manifest, indent=1).encode('utf-8')
    with open(_manifest_path(game_root), 'wb') as f:
        f.write(data)


def _write(path: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def build(game_root: str, outdir: str) -> dict[str, bytes]:
    if os.path.normcase(os.path.abspath(outdir)).startswith(os.path.normcase(os.path.abspath(game_root))):
        raise SystemExit('build writes outside the game dir; use install for that')
    files = stack(game_root)
    for rel, data in files.items():
        _write(os.path.join(outdir, *rel.split('/')), data)
    return files


def install(game_root: str) -> dict[str, str]:
    """Returns {Mods-relative path: sha256} of the files written."""
    if game_running():
        raise SystemExit(f'{PROCESS} is running: close the game first')
    mods = os.path.join(game_root, 'Mods')
    manifest = _load_manifest(game_root)
    files = stack(game_root)
    for rel in files:  # back up what we overwrite the first time, before writing anything
        dst = os.path.join(mods, *rel.split('/'))
        if rel in manifest['created'] or rel in manifest['replaced']:
            continue
        if os.path.isfile(dst):
            bak = os.path.join(mods, BACKUP, *rel.split('/'))
            os.makedirs(os.path.dirname(bak), exist_ok=True)
            shutil.copy2(dst, bak)
            manifest['replaced'].append(rel)
        else:
            manifest['created'].append(rel)
    _save_manifest(game_root, manifest)
    for rel, data in files.items():
        dst = os.path.join(mods, *rel.split('/'))
        _write(dst, data)
        manifest['written'][rel] = _sha(dst)
        print(f'installed {rel}')
    _save_manifest(game_root, manifest)
    return {rel: manifest['written'][rel] for rel in files}


def _without_our_rows(game_root: str, at: int, n: int) -> dict[str, bytes]:
    """The shared files as they are now, minus our block."""
    out: dict[str, bytes] = {}
    table_rows = -1
    for rel in SHARED:
        doc = dsgo.parse(base(game_root, rel))
        rows = _rows(doc, rel)
        if rel == TABLE:
            table_rows = len(rows)
        elif len(rows) != table_rows:
            raise SystemExit(f'{rel}: {len(rows)} rows, table has {table_rows}')
        del rows[at:at + n]
        out[rel] = _compact(doc)
    return out


def _compact(doc: dsgo.Document) -> bytes:
    """dsgo.write keeps every pool string it parsed, so our rows' names would stay behind in the pool
    after the rows are gone. Parsing the output keeps only the referenced strings, in their order, so
    a table that is back to stock is byte-identical to it again."""
    return dsgo.write(dsgo.parse(dsgo.write(doc)))


def uninstall(game_root: str, unequipped: bool, force: bool) -> None:
    if not unequipped:
        raise SystemExit('unequip these call weapons in every save first (a save with one equipped '
                         'crashes the game once its row is gone), then rerun with --unequipped')
    if game_running():
        raise SystemExit(f'{PROCESS} is running: close the game first')
    mods = os.path.join(game_root, 'Mods')
    manifest = _load_manifest(game_root)
    ids = row_ids(base(game_root, TABLE))
    at = our_block(ids)
    shared: dict[str, bytes] = {}
    if at is not None:
        n = our_count(ids)
        later = ids[at + n:]
        if later and not force:  # saves refer to rows by index: removing ours shifts these
            raise SystemExit(f'rows after ours would shift down by {n}: {later}; use --force')
        shared = _without_our_rows(game_root, at, n)
    for rel, data in shared.items():
        path = os.path.join(mods, *rel.split('/'))
        if rel == TABLE and rel in manifest['created'] and data == stock(game_root, rel):
            os.remove(path)
            print(f'removed {rel} (back to stock)')
            continue
        _write(path, data)  # the texts are always written back: other tools edit them too
        print(f'removed our rows from {rel}')
    for call in CALLS:
        rel = sgo_file(call)
        path = os.path.join(mods, *rel.split('/'))
        bak = os.path.join(mods, BACKUP, *rel.split('/'))
        if rel in manifest['replaced'] and os.path.isfile(bak):
            shutil.copy2(bak, path)
            print(f'restored {rel}')
        elif os.path.isfile(path):
            os.remove(path)
            print(f'removed {rel}')
    shutil.rmtree(os.path.join(mods, BACKUP), ignore_errors=True)
    if os.path.isfile(_manifest_path(game_root)):
        os.remove(_manifest_path(game_root))


def check(game_root: str) -> bool:
    """Prints the state of the installed tables; True when our block is in and everything lines up."""
    ids = row_ids(base(game_root, TABLE))
    missing = missing_rows(game_root)
    print(f'{TABLE}: {len(ids)} rows ({"Mods" if os.path.isfile(os.path.join(game_root, "Mods", TABLE)) else "stock"})')
    ok = True
    try:
        at = our_block(ids)
    except ValueError as e:
        print(f'  NOT contiguous: {e}')
        at, ok = None, False
    if at is None:
        print(f'  our rows missing: {missing}')
        ok = False
    else:
        n = our_count(ids)
        print(f'  our rows contiguous at {at}..{at + n - 1}:')
        for i, x in enumerate(IDS[:n]):
            print(f'    {at + i:5d} {x}')
        if n < len(IDS):
            print(f'  older install: {len(IDS) - n} rows not in yet ({", ".join(IDS[n:])}); rerun install')
            ok = False
        later = ids[at + n:]
        if later:
            print(f'  rows after ours: {later}')
    for rel in TEXTS:
        n = len(_rows(dsgo.parse(base(game_root, rel)), rel))
        aligned = n == len(ids)
        ok &= aligned
        print(f'{rel}: {n} rows {"aligned" if aligned else "NOT ALIGNED with the table"}')
    for call in CALLS:
        path = os.path.join(game_root, 'Mods', *sgo_file(call).split('/'))
        if not os.path.isfile(path):
            print(f'{sgo_file(call)}: missing')
            ok = False
    return ok


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('action', choices=('build', 'install', 'uninstall', 'check'))
    ap.add_argument('outdir', nargs='?')
    ap.add_argument('--game', default=gen.DEFAULT_GAME)
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--unequipped', action='store_true', help='these weapons are unequipped in every save')
    a = ap.parse_args()
    if a.action == 'build':
        if not a.outdir:
            ap.error('build needs OUTDIR')
        for rel in build(a.game, a.outdir):
            print(f'wrote {rel}')
    elif a.action == 'install':
        for rel, sha in install(a.game).items():
            print(f'{sha}  {rel}')
    elif a.action == 'uninstall':
        uninstall(a.game, a.unequipped, a.force)
    else:
        sys.exit(0 if check(a.game) else 1)


if __name__ == '__main__':
    main()
