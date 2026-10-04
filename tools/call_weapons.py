"""Air Raider call weapons for the plugin's jets and helicopters: the rows tools/calls.py lists, added to the
shared weapon table. The calls are clones of the stock eWeapon051 (Combat Bomber KM6, Weapon_RadioContact,
category 312) with a marker in its SGO's AmmoHitSizeAdjust. The plugin reads the marker (weapon +0x8C4)
and flies its own planes for the call; the field does nothing for a RadioContact weapon, so without the
plugin the weapon is simply a working KM6 bomber call.
The vehicle requests (Call.brings 'vehicle') are clones of the stock eWeapon394 (N9 Eros, Weapon_Sub,
category 308): the stock request brings the player jet SGO (tools/make_jets.py EDF6VC_PJET_*.SGO, which
must be installed first) with the jet's mark and guns in the request's vehicle setup, empty, for the player
to fly (src/playerjet.cpp).

  python tools/call_weapons.py build OUTDIR [--game DIR]     write the SGOs + the stacked tables into OUTDIR
  python tools/call_weapons.py install [--game DIR]          into <game>/Mods (refuses while EDF6 runs)
  python tools/call_weapons.py uninstall [--delete-rows --unequipped] [--game DIR]
  python tools/call_weapons.py repair [--game DIR]           the shared files back to before the first install
  python tools/call_weapons.py check [--game DIR]

The weapon table and its texts (WEAPONTEXT.<LANG>.SGO, index-aligned with the table) are shared with
other mods (autoturret/tools/describe.py rewrites text rows in place), so the base is always the
installed Mods/WEAPON copy when there is one, else Root.cpk. Saves refer to weapons by row index, and the
plugin's GrantCalls sets the owned bit by row index too, so a row of ours never moves and its index is never
given to anything else:
  - install keeps every row of ours where it is (found by id; tools/calls.py slot_of) and replaces it in place;
    the calls a table lacks go at its end, in CALLS order. Every other row is checked unchanged before anything
    is written (verify).
  - uninstall turns our rows into placeholders (the template's stock row under the id EDF6VC_RETIRED_*, named
    as uninstalled): the index stays taken, a save with one equipped still has a working stock weapon, and no
    later mod's row moves or inherits our owned bits. --delete-rows really deletes the rows at the very end of
    the table (only those: deleting one in the middle would move every row after it), for saves where none of
    them is equipped (--unequipped: such a save crashes at the main menu once the row is gone, the menu looks
    the weapon up past the table end; see edf6-jaeger tools/install.py). A later install takes the
    placeholders' rows back.

Writes are one transaction: every file the run changes is first copied to Mods/.edf6vc_backup/txn/ and listed
in a journal (Mods/.edf6vc_calls.txn.json), then each is replaced atomically (temporary file + rename); any
failure rolls all of them back from the copies, and a run that died on the way (the journal still there) is
rolled back by the next one before it starts. install also keeps, once, a copy of each shared file as it was
before our first install (Mods/.edf6vc_backup/), which repair restores when the texts no longer line up with
the table; Mods/.edf6vc_calls.json records what we wrote (with its SHA-256, to tell what another tool changed
since) and where each of our rows is.
"""
from __future__ import annotations

import argparse
import copy
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from functools import lru_cache

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import calls  # noqa: E402
import dsgo  # noqa: E402
import modfiles  # noqa: E402
import vcobjects as vc  # noqa: E402
from calls import CALLS, IDS, Call, call_name  # noqa: E402
from dsgo import Node  # noqa: E402

TEMPLATE = 'eWeapon051'          # Combat Bomber KM6
VEHICLE_TEMPLATE = 'eWeapon394'  # N9 Eros (a heli vehicle request)
LANGS = ('JA', 'EN', 'CN', 'KR', 'SC')
TABLE = 'WEAPON/WEAPONTABLE.SGO'
TEXTS = [f'WEAPON/WEAPONTEXT.{lang}.SGO' for lang in LANGS]
SHARED = [TABLE] + TEXTS
BACKUP = '.edf6vc_backup'
MANIFEST = '.edf6vc_calls.json'
JOURNAL = '.edf6vc_calls.txn.json'
ACQUIRE = 0.0   # WEAPONTABLE column 5: 0 normal (the plugin makes EDF6VC_CALL_* owned at every save load)
PROCESS = 'EDF6.exe'
OWNER = 'calls'  # pylib/modfiles.py: the vehicle requests need make_jets' player jet SGOs


class Misaligned(Exception):
    """The texts no longer have a row per table row: a run of an older version that died half way, or another
    tool. Nothing can be stacked on it; repair restores the state before our first install."""


def sgo_file(call: Call) -> str:
    """Mods/WEAPON file name, upper case like the other mod weapons there (EWEAPON389.SGO)."""
    return f'WEAPON/{call.id.upper()}.SGO'


def vehicle_file(call: Call) -> str:
    return f'OBJECT/{call.vehicle.upper()}.SGO'


# ---------------------------------------------------------------- reading the base


@lru_cache(maxsize=None)
def _game(game_root: str) -> vc.Game:
    return vc.Game(game_root)


def stock(game_root: str, rel: str) -> bytes:
    folder, name = rel.split('/')
    return _game(game_root).read(folder, name)


def _mods(game_root: str, *rel: str) -> str:
    return os.path.join(game_root, 'Mods', *[p for r in rel for p in r.split('/')])


def base(game_root: str, rel: str) -> bytes:
    """The installed Mods copy when present, else Root.cpk."""
    path = _mods(game_root, rel)
    if os.path.isfile(path):
        with open(path, 'rb') as f:
            return f.read()
    return stock(game_root, rel)


def _rows(doc: dsgo.Document, rel: str) -> list:
    return doc.root.get('table' if rel == TABLE else 'text_table').items


def row_ids(table: bytes) -> list[str]:
    return [r.items[0] for r in dsgo.parse(table).root.get('table').items]


def template_of(call: Call) -> str:
    return VEHICLE_TEMPLATE if call.brings == 'vehicle' else TEMPLATE


def _template_index(ids: list[str], template: str = TEMPLATE) -> int:
    upper = [x.upper() for x in ids]
    return upper.index(template.upper())


def check_aligned(game_root: str, n: int) -> None:
    """Every text has a row per table row (`n`), else Misaligned (with what to do)."""
    bad = [(rel, len(_rows(dsgo.parse(base(game_root, rel)), rel))) for rel in TEXTS]
    bad = [(rel, k) for rel, k in bad if k != n]
    if not bad:
        return
    have = os.path.isfile(_manifest_path(game_root))
    fix = ('run `python tools/call_weapons.py repair` (or the installer, which offers it): it puts the weapon table '
           'and its texts back as they were before EDF6VehicleCrew first installed (Mods/.edf6vc_backup); then install '
           'again' if have else 'EDF6VehicleCrew never installed into these files, so it has no copy of them to restore: '
           'reinstall the mod that wrote them, or delete them from Mods/WEAPON to fall back to the stock ones')
    raise Misaligned(f'the weapon texts do not line up with the weapon table ({n} rows): '
                     + ', '.join(f'{rel} has {k}' for rel, k in bad) + f'. To fix it, {fix}.')


# ---------------------------------------------------------------- where the rows go


@dataclass
class Plan:
    at: dict[str, int]      # call id -> its row index in the result
    appended: list[str]     # the call ids added at the table's end, in that order


def plan_rows(ids: list[str]) -> Plan:
    """Where each call's row goes in a table whose row ids are `ids`: a row of ours already there (or the
    placeholder an uninstall left) stays where it is, whatever order an older install put them in; the rest go
    at the end in CALLS order. Raises when a call has two rows."""
    at: dict[str, int] = {}
    for i, x in enumerate(ids):
        c = calls.slot_of(x)
        if c is None:
            continue
        if c in at:
            raise ValueError(f'{c} has two rows in the weapon table: {at[c]} and {i}')
        at[c] = i
    appended = [c for c in IDS if c not in at]
    for k, c in enumerate(appended):
        at[c] = len(ids) + k
    return Plan(at, appended)


def tail_start(ids: list[str]) -> int:
    """Where the run of our rows (or placeholders) that ends the table starts: only those can be deleted
    without moving another row."""
    i = len(ids)
    while i > 0 and calls.slot_of(ids[i - 1]) is not None:
        i -= 1
    return i


def _put(rows: list, at: int, row: Node) -> None:
    if at < len(rows):
        rows[at] = row
    elif at == len(rows):
        rows.append(row)
    else:
        raise AssertionError(f'row {at} past the end ({len(rows)})')


# ---------------------------------------------------------------- building


# The stock heli's weapons in the request's vehicle setup and resources -> the player jet's (vcobjects.JETS).
_VEHICLE_SWAP = {
    'app:/weapon/v_506heli_gatling01_l.sgo': vc._GUNS[0],
    'app:/weapon/v_506heli_gatling01_r.sgo': vc._GUNS[1],
}


def _object_path(call: Call) -> str:
    return f'app:/object/{call.vehicle.lower()}.sgo'


def vehicle_sgo(template: bytes, call: Call) -> bytes:
    """The N9 Eros request bringing the player jet. Ammo_CustomParameter[4] = [transport, box, vehicle SGO,
    vehicle setup [multipliers, heli params (first: the speed gain k = the jet's mark), fuel, weapons],
    voice lines]; `resource` preloads the same paths."""
    doc = dsgo.parse(template)
    r = doc.root
    r.get('ReloadTime').items[0] = float(call.reload)
    req = r.get('Ammo_CustomParameter').items[4]
    stock_vehicle = req.items[2]
    req.items[2] = _object_path(call)
    setup = req.items[3]
    setup.items[1].items[0] = float(call.mark)
    for w in setup.items[3].items:
        w.items[0] = _VEHICLE_SWAP.get(w.items[0].lower(), w.items[0])
    res = r.get('resource')
    swap = {stock_vehicle.lower(): _object_path(call), **_VEHICLE_SWAP}
    res.items = [swap.get(x.lower(), x) for x in res.items]
    for lang in LANGS:
        key = f'name.{lang.lower()}'
        if key in r.names.values():
            r.set(key, call_name(call, lang))
    return dsgo.write(doc)


def vehicle_durability(game_root: str, call: Call) -> float:
    """What the menu shows: the jet's durability times the request's HP multiplier."""
    root = dsgo.parse(stock(game_root, f'WEAPON/{VEHICLE_TEMPLATE.upper()}.SGO')).root
    mult = float(root.get('Ammo_CustomParameter').items[4].items[3].items[0].items[0])
    return vc.JETS[call.jet].durability * mult


def weapon_sgo(template: bytes, call: Call) -> bytes:
    if call.brings == 'vehicle':
        return vehicle_sgo(template, call)
    doc = dsgo.parse(template)
    r = doc.root
    r.set('AmmoHitSizeAdjust', float(call.mark))
    reload = r.get('ReloadTime')
    reload.items[0] = float(call.reload)
    custom = r.get('Ammo_CustomParameter').items[2]
    custom.items[1] = float(call.count)
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


def _text_row(template: Node, call: Call, lang: str, durability: float | None = None) -> Node:
    row = copy.deepcopy(template)
    row.items[0] = call_name(call, lang)
    row.items[1] = calls.call_description(call, lang)
    # A vehicle request's stats: [re-request, durability, fuel, fuel cost]; the durability is the jet's.
    stats = row.items[2].items
    if durability is not None and len(stats) > 1 and len(stats[1].items) == 2:
        stats[1].items[1] = f'{durability:.0f}'
    # The stat list stays KM6's, except the reload line's curve, which the game shows as $0pt
    # from that list: it must be the weapon's own ReloadTime or the menu shows KM6's 1020.
    for stat in row.items[2].items:
        if len(stat.items) == 3 and str(stat.items[1]).startswith('$0pt') and isinstance(stat.items[2], Node):
            stat.items[2].items[0] = float(call.reload)
    return row


def _retired_table_row(template: Node, call: Call) -> Node:
    """The placeholder for an uninstalled call: the template's own stock row (its stock SGO, level, acquire)."""
    row = copy.deepcopy(template)
    row.items[0] = calls.retired_id(call.id)
    return row


def _retired_text_row(template: Node, call: Call, lang: str) -> Node:
    row = copy.deepcopy(template)
    row.items[0] = calls.retired_name(call, lang)
    row.items[1] = calls.retired_description(lang, str(template.items[0]))
    return row


def _compact(doc: dsgo.Document) -> bytes:
    """dsgo.write keeps every pool string it parsed, so replaced rows' names would stay behind in the pool.
    Parsing the output keeps only the referenced strings, in their order, so a table that is back to stock is
    byte-identical to it again."""
    return dsgo.write(dsgo.parse(dsgo.write(doc)))


@dataclass
class Shared:
    """The shared table and its texts as they are now (Mods, else stock), parsed."""
    table: dsgo.Document
    texts: dict[str, dsgo.Document]

    @property
    def rows(self) -> list:
        return _rows(self.table, TABLE)

    @property
    def ids(self) -> list[str]:
        return [r.items[0] for r in self.rows]

    def text_rows(self, rel: str) -> list:
        return _rows(self.texts[rel], rel)


def load_shared(game_root: str) -> Shared:
    table = dsgo.parse(base(game_root, TABLE))
    shared = Shared(table, {rel: dsgo.parse(base(game_root, rel)) for rel in TEXTS})
    check_aligned(game_root, len(shared.rows))
    return shared


def stack(game_root: str) -> dict[str, bytes]:
    """The SGOs and the shared table + texts with every call in (plan_rows), keyed by path under Mods. Reads
    only; raises (Misaligned, ValueError) before anything could be written."""
    s = load_shared(game_root)
    before = s.ids
    plan = plan_rows(before)
    tpl = {t: _template_index(before, t) for t in (TEMPLATE, VEHICLE_TEMPLATE)}
    template_sgo = {t: stock(game_root, f'WEAPON/{t.upper()}.SGO') for t in tpl}
    out: dict[str, bytes] = {sgo_file(c): weapon_sgo(template_sgo[template_of(c)], c) for c in CALLS}
    order = sorted(CALLS, key=lambda c: plan.at[c.id])   # appended rows in their order
    rows = s.rows
    templates = {c.id: rows[tpl[template_of(c)]] for c in CALLS}
    for c in order:
        _put(rows, plan.at[c.id], _table_row(templates[c.id], c))
    out[TABLE] = _compact(s.table)
    durability = {c.id: vehicle_durability(game_root, c) if c.brings == 'vehicle' else None for c in CALLS}
    for lang, rel in zip(LANGS, TEXTS):
        text = s.text_rows(rel)
        text_templates = {c.id: text[tpl[template_of(c)]] for c in CALLS}
        for c in order:
            _put(text, plan.at[c.id], _text_row(text_templates[c.id], c, lang, durability[c.id]))
        out[rel] = _compact(s.texts[rel])
    verify(game_root, out, plan)
    return out


def verify(game_root: str, out: dict[str, bytes], plan: Plan) -> None:
    """Before anything is written: every row not ours unchanged at its index, each of ours where plan_rows put
    it, the table grown by exactly the rows appended, the texts as long as the table."""
    before = load_shared(game_root)
    ids = before.ids
    after = row_ids(out[TABLE])
    if len(after) != len(ids) + len(plan.appended):
        raise ValueError(f'the table grew by {len(after) - len(ids)} rows, {len(plan.appended)} were appended')
    for cid, i in plan.at.items():
        if after[i] != cid:
            raise ValueError(f'row {i} is {after[i]}, expected {cid}')
    ours = set(plan.at.values())
    for rel in SHARED:
        old = [dsgo.to_py(r) for r in (before.rows if rel == TABLE else before.text_rows(rel))]
        new = [dsgo.to_py(r) for r in _rows(dsgo.parse(out[rel]), rel)]
        if len(new) != len(after):
            raise ValueError(f'{rel}: {len(new)} rows, the table has {len(after)}')
        for i, row in enumerate(old):
            if i not in ours and new[i] != row:
                raise ValueError(f'{rel}: row {i} is not ours and changed')


def retire(game_root: str, delete_rows: bool) -> tuple[dict[str, bytes], list[str]]:
    """The shared table + texts with every row of ours a placeholder, or (delete_rows) deleted when it is in the
    run of ours that ends the table. Returns (files, the ids whose rows are deleted)."""
    s = load_shared(game_root)
    ids = s.ids
    plan = plan_rows(ids)
    present = {c: i for c, i in plan.at.items() if i < len(ids)}
    cut = tail_start(ids) if delete_rows else len(ids)
    deleted = sorted((c for c, i in present.items() if i >= cut), key=lambda c: present[c])
    tpl = {t: _template_index(ids, t) for t in (TEMPLATE, VEHICLE_TEMPLATE)}
    by_id = {c.id: c for c in CALLS}
    rows = s.rows
    for cid, i in present.items():
        if i < cut:
            rows[i] = _retired_table_row(rows[tpl[template_of(by_id[cid])]], by_id[cid])
    del rows[cut:]
    out = {TABLE: _compact(s.table)}
    for lang, rel in zip(LANGS, TEXTS):
        text = s.text_rows(rel)
        for cid, i in present.items():
            if i < cut:
                text[i] = _retired_text_row(text[tpl[template_of(by_id[cid])]], by_id[cid], lang)
        del text[cut:]
        out[rel] = _compact(s.texts[rel])
    after = row_ids(out[TABLE])
    if after[:cut] != [x if calls.slot_of(x) is None else calls.retired_id(calls.slot_of(x)) for x in ids[:cut]]:
        raise AssertionError('retire moved a row')
    return out, deleted


# ---------------------------------------------------------------- game dir: transaction, manifest


def game_running() -> bool:
    r = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {PROCESS}', '/NH'],
                       capture_output=True, text=True, errors='replace')
    return PROCESS.lower() in r.stdout.lower()


def _manifest_path(game_root: str) -> str:
    return _mods(game_root, MANIFEST)


def load_manifest(game_root: str) -> dict:
    path = _manifest_path(game_root)
    manifest = {'created': [], 'replaced': [], 'written': {}, 'rows': {}}
    if os.path.isfile(path):
        with open(path, encoding='utf-8') as f:
            manifest.update(json.load(f))
    return manifest


def _save_manifest(game_root: str, manifest: dict) -> None:
    modfiles.write_atomic(_manifest_path(game_root), json.dumps(manifest, indent=1).encode('utf-8'))


def recover(game_root: str) -> bool:
    """Rolls back a run that died between its first and last write (its journal is still there): every file it
    listed back as it was before that run. True when there was one."""
    journal = _mods(game_root, JOURNAL)
    if not os.path.isfile(journal):
        return False
    with open(journal, encoding='utf-8') as f:
        existed: dict[str, bool] = json.load(f)
    for rel, had in existed.items():
        path = _mods(game_root, rel)
        if had:
            with open(_mods(game_root, BACKUP, 'txn', rel), 'rb') as f:
                modfiles.write_atomic(path, f.read())
        elif os.path.isfile(path):
            os.remove(path)
    os.remove(journal)
    shutil.rmtree(_mods(game_root, BACKUP, 'txn'), ignore_errors=True)
    return True


def commit(game_root: str, changes: dict[str, bytes | None]) -> None:
    """Writes every file in `changes` (None: delete it) or none: see the module doc."""
    txn = _mods(game_root, BACKUP, 'txn')
    shutil.rmtree(txn, ignore_errors=True)
    existed: dict[str, bool] = {}
    for rel in changes:
        path = _mods(game_root, rel)
        existed[rel] = os.path.isfile(path)
        if existed[rel]:
            os.makedirs(os.path.dirname(_mods(game_root, BACKUP, 'txn', rel)), exist_ok=True)
            shutil.copy2(path, _mods(game_root, BACKUP, 'txn', rel))
    modfiles.write_atomic(_mods(game_root, JOURNAL), json.dumps(existed, indent=1).encode('utf-8'))
    try:
        for rel, data in changes.items():
            path = _mods(game_root, rel)
            if data is not None:
                modfiles.write_atomic(path, data)
            elif os.path.isfile(path):
                os.remove(path)
    except BaseException:
        recover(game_root)
        raise
    os.remove(_mods(game_root, JOURNAL))
    shutil.rmtree(txn, ignore_errors=True)


def _first_backup(game_root: str, manifest: dict, rels: list[str]) -> None:
    """Keeps, once, each file as it was before our first write to it (repair restores these)."""
    for rel in rels:
        if rel in manifest['created'] or rel in manifest['replaced']:
            continue
        path = _mods(game_root, rel)
        if os.path.isfile(path):
            bak = _mods(game_root, BACKUP, rel)
            os.makedirs(os.path.dirname(bak), exist_ok=True)
            shutil.copy2(path, bak)
            manifest['replaced'].append(rel)
        else:
            manifest['created'].append(rel)


def changed_since(game_root: str, manifest: dict, rels: list[str]) -> list[str]:
    """The files among `rels` that are not what we last wrote into them (another tool changed them since)."""
    return [rel for rel in rels if rel in manifest['written']
            and modfiles.file_sha(_mods(game_root, rel)) not in (None, manifest['written'][rel])]


def build(game_root: str, outdir: str) -> dict[str, bytes]:
    if os.path.normcase(os.path.abspath(outdir)).startswith(os.path.normcase(os.path.abspath(game_root))):
        raise SystemExit('build writes outside the game dir; use install for that')
    files = stack(game_root)
    for rel, data in files.items():
        modfiles.write_atomic(os.path.join(outdir, *rel.split('/')), data)
    return files


def install(game_root: str, files: dict[str, bytes] | None = None) -> dict[str, str]:
    """Writes `files` (stack, made now when None) in one transaction; returns {path under Mods: sha256}."""
    if game_running():
        raise SystemExit(f'{PROCESS} is running: close the game first')
    if recover(game_root):
        print('rolled back the weapon table files of an earlier run that did not finish')
    files = stack(game_root) if files is None else files
    missing = [vehicle_file(c) for c in CALLS if c.vehicle and not os.path.isfile(_mods(game_root, vehicle_file(c)))]
    if missing:
        raise SystemExit(f'{", ".join(missing)} not installed: run python tools/make_jets.py first')
    manifest = load_manifest(game_root)
    for rel in changed_since(game_root, manifest, list(files)):
        note = 'its other rows are kept' if rel in SHARED else 'overwritten'
        print(f'note: {rel} was changed by another tool since our last install ({note})')
    old_rows = dict(manifest['rows'])
    _first_backup(game_root, manifest, list(files))
    _save_manifest(game_root, manifest)
    commit(game_root, dict(files))
    ids = row_ids(files[TABLE])
    manifest['written'] = {rel: modfiles.sha256(data) for rel, data in files.items()}
    manifest['rows'] = {c: ids.index(c) for c in IDS}
    _save_manifest(game_root, manifest)
    moved = {c: (old_rows[c], manifest['rows'][c]) for c in old_rows if old_rows[c] != manifest['rows'].get(c)}
    for c, (was, now) in moved.items():
        print(f'WARNING: {c} was row {was}, now {now}: another tool rewrote the weapon table without it; a save '
              f'that had it equipped or owned refers to row {was}')
    led = modfiles.Ledger(game_root)
    for c in CALLS:
        if c.vehicle:
            led.need(OWNER, vehicle_file(c))
    return dict(manifest['written'])


def uninstall(game_root: str, delete_rows: bool = False, unequipped: bool = False) -> None:
    """Our rows become placeholders (or, delete_rows, the ones ending the table are deleted), our SGOs go."""
    if game_running():
        raise SystemExit(f'{PROCESS} is running: close the game first')
    if recover(game_root):
        print('rolled back the weapon table files of an earlier run that did not finish')
    manifest = load_manifest(game_root)
    shared, deleted = retire(game_root, delete_rows)
    if deleted and not unequipped:
        raise SystemExit(f'deleting the rows of {", ".join(deleted)}: unequip these weapons in every save first (a '
                         'save with one equipped crashes the game once its row is gone), then rerun with --unequipped')
    changes: dict[str, bytes | None] = {}
    for rel, data in shared.items():
        if rel in manifest['created'] and data == stock(game_root, rel):
            changes[rel] = None   # we made it and nobody else's rows are left in it
        elif data != base(game_root, rel):
            changes[rel] = data
    for c in CALLS:
        rel = sgo_file(c)
        path = _mods(game_root, rel)
        bak = _mods(game_root, BACKUP, rel)
        if rel in manifest['replaced'] and os.path.isfile(bak):
            with open(bak, 'rb') as f:
                changes[rel] = f.read()
        elif os.path.isfile(path):
            if rel in changed_since(game_root, manifest, [rel]):
                print(f'kept {rel}: another tool changed it since our install')
                continue
            changes[rel] = None
    _first_backup(game_root, manifest, list(shared))
    _save_manifest(game_root, manifest)
    commit(game_root, changes)
    for rel, data in changes.items():
        print(f'{"removed" if data is None else "wrote"} {rel}')
    modfiles.Ledger(game_root).release(OWNER, [vehicle_file(c) for c in CALLS if c.vehicle])
    ids = row_ids(base(game_root, TABLE))
    left = [x for x in ids if calls.slot_of(x)]
    if left:   # placeholders stay ours: a later install takes their rows back, repair can still restore
        manifest['written'] = {rel: modfiles.sha256(d) for rel, d in changes.items() if d is not None}
        manifest['rows'] = {calls.slot_of(x): ids.index(x) for x in left}
        _save_manifest(game_root, manifest)
        print(f'{len(left)} rows are placeholders now (EDF6VC_RETIRED_*, stock weapons), keeping their row numbers')
        return
    shutil.rmtree(_mods(game_root, BACKUP), ignore_errors=True)
    if os.path.isfile(_manifest_path(game_root)):
        os.remove(_manifest_path(game_root))


def repair(game_root: str) -> list[str]:
    """The shared table and texts back to before our first install (the copies install kept), our SGOs gone;
    returns what it changed. Another tool's later edits to those files are lost (install reports them)."""
    if game_running():
        raise SystemExit(f'{PROCESS} is running: close the game first')
    recover(game_root)
    manifest = load_manifest(game_root)
    if not os.path.isfile(_manifest_path(game_root)):
        raise SystemExit('EDF6VehicleCrew never installed its call weapons here: nothing to restore')
    changes: dict[str, bytes | None] = {}
    for rel in SHARED + [sgo_file(c) for c in CALLS]:
        bak = _mods(game_root, BACKUP, rel)
        if rel in manifest['replaced'] and os.path.isfile(bak):
            with open(bak, 'rb') as f:
                changes[rel] = f.read()
        elif rel in manifest['created'] or (rel not in SHARED and os.path.isfile(_mods(game_root, rel))):
            changes[rel] = None
    commit(game_root, changes)
    shutil.rmtree(_mods(game_root, BACKUP), ignore_errors=True)
    os.remove(_manifest_path(game_root))
    modfiles.Ledger(game_root).release(OWNER, [vehicle_file(c) for c in CALLS if c.vehicle])
    return sorted(changes)


def check(game_root: str) -> bool:
    """Prints the state of the installed tables; True when every call is in, at the row it was installed at,
    and everything lines up."""
    ok = True
    if os.path.isfile(_mods(game_root, JOURNAL)):
        print('an earlier run did not finish: the next install or uninstall rolls it back first')
        ok = False
    table = dsgo.parse(base(game_root, TABLE))
    ids = [r.items[0] for r in _rows(table, TABLE)]
    print(f'{TABLE}: {len(ids)} rows ({"Mods" if os.path.isfile(_mods(game_root, TABLE)) else "stock"})')
    for rel in TEXTS:
        n = len(_rows(dsgo.parse(base(game_root, rel)), rel))
        aligned = n == len(ids)
        ok &= aligned
        print(f'{rel}: {n} rows {"aligned" if aligned else "NOT ALIGNED with the table (python tools/call_weapons.py repair)"}')
    try:
        plan = plan_rows(ids)
    except ValueError as e:
        print(f'  {e}')
        return False
    rows = load_manifest(game_root)['rows']
    for c in CALLS:
        i = plan.at[c.id]
        state = 'missing' if i >= len(ids) else 'placeholder' if ids[i] != c.id else 'in'
        moved = f' (installed at {rows[c.id]})' if c.id in rows and rows[c.id] != i and state != 'missing' else ''
        print(f'  {i if state != "missing" else "-":>5} {c.id}: {state}{moved}')
        ok &= state == 'in' and not moved
        if state == 'in' and not os.path.isfile(_mods(game_root, sgo_file(c))):
            print(f'        {sgo_file(c)} missing')
            ok = False
        if state == 'in' and c.vehicle and not os.path.isfile(_mods(game_root, vehicle_file(c))):
            print(f'        {vehicle_file(c)} missing (python tools/make_jets.py)')
            ok = False
    return ok


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('action', choices=('build', 'install', 'uninstall', 'repair', 'check'))
    ap.add_argument('outdir', nargs='?')
    ap.add_argument('--game', default=vc.DEFAULT_GAME)
    ap.add_argument('--delete-rows', action='store_true', help='delete the rows that end the table instead of '
                    'leaving placeholders')
    ap.add_argument('--unequipped', action='store_true', help='these weapons are unequipped in every save')
    a = ap.parse_args()
    try:
        if a.action == 'build':
            if not a.outdir:
                ap.error('build needs OUTDIR')
            for rel in build(a.game, a.outdir):
                print(f'wrote {rel}')
        elif a.action == 'install':
            for rel, sha in install(a.game).items():
                print(f'{sha}  {rel}')
        elif a.action == 'uninstall':
            uninstall(a.game, a.delete_rows, a.unequipped)
        elif a.action == 'repair':
            for rel in repair(a.game):
                print(f'restored {rel}')
        else:
            sys.exit(0 if check(a.game) else 1)
    except Misaligned as e:
        raise SystemExit(str(e)) from None


if __name__ == '__main__':
    main()
