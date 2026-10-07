"""The EDF5 campaign in EDF6: EDF5's missions appended to the offline mission list, with all of their dialogue.

  python -B tools/make_edf5_campaign.py [GAME_DIR]            install (the game must be closed)
  python -B tools/make_edf5_campaign.py [GAME_DIR] --remove   put the files back as they were
  python -B tools/make_edf5_campaign.py [GAME_DIR] --list     what would be appended (nothing written)

EDF6's Root.cpk already holds everything a mission needs: EDF5's scripts (MISSION/EDF5_OLD_SCRIPT/<id>/MISSION.BVM,
EDF5's bytecode kept up for EDF6: the only change from EDF5's is a SetUndergroundSoundReverve call), EDF5's voice
bank (SOUND/PC/TIKYUU5_VOICE.*.AWB) and subtitles (MISSION/EDF6_VOICETABLE.*.SGO), and nearly all of EDF5's maps
and objects. What is missing is a way in: this writes, under Mods/MISSION/,

  MISSIONLIST.OFFLINE.LIST.SGO         (DSGO) the list's rows kept as they are, one row per EDF5 mission appended
  MISSIONLIST.OFFLINE.TXT.<lang>.SGO   title and briefing per row, for every language the game ships (CN EN JA KR SC)
  MISSIONLIST.OFFLINE.IMAGE.RAB        a thumbnail per new row (a stock one of a mission on the same map)

The titles and briefings are EDF5's own (edf5campaign/missions.json, tools/make_edf5_campaign_text.py).
Why appending is safe and what it changes (docs/mission-list-re.md, static, not yet run in the game):
- the save keeps 512 missions per mode (clear[10][512], seen[512]) indexed by row: 257 rows fit, and an old save's
  columns past 147 are zero, so the new missions start uncleared and no save is migrated;
- the stock rows must keep their order (the online story shares the save, M00.MST): only appended to;
- every row's 11th member must be named "flags" (read by name: missing, the mission start crashes), every
  language's text table must have a row per list row (read by row, unchecked), successors must be < the row count;
- the last row decides the ending and the clear ratio counts every row: the plugin counts only the rows before
  the first EDF5 one there (src/edf5campaign.cpp, ini EDF5CampaignRows, set here).
EDF5's first mission is also made a successor of row 0 (M000B), so it opens once the first EDF6 mission is
cleared instead of after M152; row 0 then has two successors and the game no longer moves on to M001 by itself.

The files a list replaces may be another mod's: what was there is kept in the manifest (Mods/.edf5campaign.json)
and put back on removal; a file changed by someone since is left alone and reported.
"""
from __future__ import annotations

import base64
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (os.path.join(HERE, '..', 'pylib'),):
    if _p not in sys.path:
        sys.path.insert(0, _p)
import cpk  # noqa: E402
import dsgo  # noqa: E402
import mdb  # noqa: E402
import modfiles  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

TEXT = (os.path.join(sys._MEIPASS, 'edf5campaign', 'missions.json') if getattr(sys, 'frozen', False)  # type: ignore[attr-defined]
        else os.path.join(HERE, '..', 'edf5campaign', 'missions.json'))   # the installer exe: tools/build_release.py
MANIFEST = '.edf5campaign.json'
INI = os.path.join('Plugins', 'EDF6VehicleCrew.ini')
INI_KEY = 'EDF5CampaignRows'
LANGS = ('CN', 'EN', 'JA', 'KR', 'SC')
LIST = 'MISSION/MISSIONLIST.OFFLINE.LIST.SGO'
IMAGE = 'MISSION/MISSIONLIST.OFFLINE.IMAGE.RAB'
TXT = {lang: f'MISSION/MISSIONLIST.OFFLINE.TXT.{lang}.SGO' for lang in LANGS}
FILES = (LIST, IMAGE, *TXT.values())
SCRIPTS = 'EDF5_OLD_SCRIPT'
GROUPS = (('main', '[EDF5] '), ('dlc1', '[EDF5 DLC1] '), ('dlc2', '[EDF5 DLC2] '))
# The values of EDF6's own 28 EDF5-era rows (RM015B2, M046, ...): the EDF5 soldiers' look, its menu music, flags 8
# (no branch bits 0x80 / 0x100, no 0x20).
BGM = 'BGM_E6M29_MissionEDF5'
FLAGS = 8.0


class Refused(RuntimeError):
    """The install would break something (said why); nothing was written."""


# ------------------------------------------------------------------------------------------- reading
def rel_path(root: str, rel: str) -> str:
    return os.path.join(root, 'Mods', *rel.split('/'))


def load_manifest(root: str) -> dict:
    return modfiles.load_json(os.path.join(root, 'Mods', MANIFEST), {})


def base_bytes(root: str, game: rootcpk.Game, rel: str, manifest: dict) -> bytes:
    """What the list file is without this tool: the file it replaced (kept in the manifest) while ours is still in
    place, else the one in Mods (another mod's), else the game's."""
    entry = manifest.get('files', {}).get(rel)
    on_disk = modfiles.read(rel_path(root, rel))
    if entry and on_disk is not None and modfiles.sha256(on_disk) == entry['sha']:
        if entry['original'] is None:
            return game.read(*rel.split('/'))
        return base64.b64decode(entry['original'])
    if on_disk is not None:
        return on_disk
    return game.read(*rel.split('/'))


def utf16_strings(data: bytes) -> list[str]:
    return [m.group().decode('utf-16le') for m in re.finditer(rb'(?:[\x20-\x7e]\x00){4,}', data)]


def asset_refs(bvm: bytes) -> list[str]:
    """The app:/ files a mission script names (a string may hold several, ';'-separated)."""
    out = []
    for s in utf16_strings(bvm):
        out += [p.strip() for p in re.findall(r'(?i)app:/([^;]+)', s)]
    return out


def map_names(root: str, game: rootcpk.Game) -> set[str]:
    names = {n.upper() for n in game.names('MAP')}
    for chunk in ('Chunk01.cpk', 'Chunk02.cpk'):
        path = os.path.join(root, chunk)
        if os.path.isfile(path):
            names |= {n.upper() for d, n in cpk.Cpk(path).index if d.upper() == 'MAP'}
    return names


def root_names(game: rootcpk.Game) -> set[tuple[str, str]]:
    return {(d.upper(), n.upper()) for d, n in game.cpk.index}


def missing_assets(names: set[tuple[str, str]], maps: set[str], bvm: bytes) -> list[str]:
    out = []
    for ref in asset_refs(bvm):
        folder, _, name = ref.replace('\\', '/').rpartition('/')
        if folder.upper() == 'MAP':
            if name.upper() not in maps:
                out.append(ref)
            continue
        if (folder.upper(), name.upper()) not in names:
            out.append(ref)
    return out


def first_map(bvm: bytes) -> str | None:
    for ref in asset_refs(bvm):
        folder, _, name = ref.replace('\\', '/').rpartition('/')
        if folder.upper() == 'MAP':
            return name.upper()
    return None


def stock_maps(game: rootcpk.Game, rows: list) -> dict[str, str]:
    """Map file -> the key of the first stock row whose mission loads it (for the thumbnails)."""
    out: dict[str, str] = {}
    for row in rows:
        folder = row.items[1].split('app:/', 1)[1]
        try:
            text = game.read(folder, 'MISSION.AC').decode('utf-8', 'replace')
        except KeyError:
            continue
        m = re.search(r'(?i)app:/Map/([A-Za-z0-9_]+\.mac)', text)
        if m:
            out.setdefault(m.group(1).upper(), row.items[2])
    return out


# ------------------------------------------------------------------------------------------- building
def thumb_name(key: str) -> str:
    """The thumbnail a row shows: its key with '/' as '_', .dds (EDF.dll 0xE0D80)."""
    return key.replace('/', '_') + '.dds'


def new_row(i: int, path: str, progress: float) -> dsgo.Node:
    key = f'{SCRIPTS}/{path}'
    return dsgo.Node([float(i), f'app:/Mission/{key}', key, dsgo.Node([float(i + 1)]), float(i), 0.0, 0.0, 0.0,
                      BGM, float(progress), FLAGS], {10: 'flags'})


def plan(root: str) -> dict:
    """What would be appended (rows, skipped missions), from the game and the text; nothing written."""
    game = rootcpk.Game(root)
    text = json.load(open(TEXT, encoding='utf-8'))
    maps = map_names(root, game)
    names = root_names(game)
    rows, skipped = [], []
    for group, prefix in GROUPS:
        for m in text[group]:
            folder = f'MISSION/{SCRIPTS}/{m["path"]}'
            try:
                bvm = game.read(folder, 'MISSION.BVM')
            except KeyError:
                skipped.append((group, m['path'], 'MISSION.BVM 不在 Root.cpk'))
                continue
            gone = missing_assets(names, maps, bvm)
            if gone:
                skipped.append((group, m['path'], '缺少 ' + ', '.join(sorted(set(gone)))))
                continue
            rows.append({'group': group, 'path': m['path'], 'progress': m['progress'], 'map': first_map(bvm),
                         'title': {lang: prefix + m['title'][lang] for lang in LANGS},
                         'brief': {lang: m['brief'][lang] for lang in LANGS}})
    return {'rows': rows, 'skipped': skipped}


def build(root: str) -> tuple[dict[str, bytes], int, dict]:
    """The files to write (rel under Mods -> bytes), the number of rows before the first EDF5 one, and the plan."""
    game = rootcpk.Game(root)
    manifest = load_manifest(root)
    p = plan(root)
    stock = dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
    doc = dsgo.parse(base_bytes(root, game, LIST, manifest))
    table = doc.root.get('table').items
    if [r.items[2] for r in table[:len(stock)]] != [r.items[2] for r in stock]:
        raise Refused('Mods/MISSION 里的任务列表改过原版任务的顺序或数量：存档按行号对应，不能再往后追加。')
    if any(str(r.items[1]).lower().startswith(f'app:/mission/{SCRIPTS.lower()}/') for r in table):
        raise Refused('任务列表里已经有 EDF5 任务行，但不是本工具写的（没有清单记录）：不重复追加。')
    base = len(table)
    for k, r in enumerate(p['rows']):
        table.append(new_row(base + k, r['path'], r['progress']))
    first = table[0].items[3]
    if float(base) not in first.items:
        first.items.append(float(base))
    out = {LIST: dsgo.compact(doc)}
    for lang, rel in TXT.items():
        ver, members = sgo.read(base_bytes(root, game, rel, manifest))
        rows = members['table']
        if len(rows) != base:
            raise Refused(f'{rel} 有 {len(rows)} 行，任务列表有 {base} 行：文本按行号读，行数不一致会崩溃。')
        rows += [[r['title'][lang], r['brief'][lang]] for r in p['rows']]
        out[rel] = sgo.write_depth_first(ver, members)
    rab = mdb.rab_read(base_bytes(root, game, IMAGE, manifest))
    by_name = {f.name.upper(): f for f in rab.files}
    by_map = stock_maps(game, stock)
    fallback = by_name.get(thumb_name('EDF6/M046').upper(), rab.files[0])
    for r in p['rows']:
        src = by_name.get(thumb_name(by_map.get(r['map'] or '', '')).upper(), fallback)
        name = thumb_name(f'{SCRIPTS}/{r["path"]}')
        if name.upper() not in by_name:
            rab.files.append(mdb.RabFile(name, src.folder, src.flag, src.stored, src.unk))
    out[IMAGE] = mdb.rab_write(rab)
    return out, base, p


# ------------------------------------------------------------------------------------------- writing
def set_rows(root: str, value: int) -> str | None:
    """The plugin's ini with EDF5CampaignRows=value (the line kept where it is, added if missing); None without ini."""
    path = os.path.join(root, 'Mods', INI)
    if not os.path.isfile(path):
        return None
    with open(path, encoding='utf-8-sig') as f:
        text = f.read()
    line = f'{INI_KEY}={value}'
    text, n = re.subn(rf'(?m)^{INI_KEY}=.*$', line, text)
    if not n:
        text = text.rstrip('\n') + '\n' + line + '\n'
    modfiles.atomic_write(path, text.encode('utf-8'))
    return path


def install(root: str, built: tuple[dict[str, bytes], int, dict] | None = None) -> list[str]:
    """Writes the files (the ones they replace kept in the manifest) and sets EDF5CampaignRows."""
    modfiles.refuse_while_running()
    files, base, _ = built or build(root)
    manifest = load_manifest(root)
    kept = manifest.get('files', {})
    entries: dict[str, dict] = {}
    for rel in files:
        old = kept.get(rel)
        on_disk = modfiles.read(rel_path(root, rel))
        if old and on_disk is not None and modfiles.sha256(on_disk) == old['sha']:
            original = old['original']   # ours from before: what it replaced stays what to put back
        else:
            original = None if on_disk is None else base64.b64encode(on_disk).decode('ascii')
        entries[rel] = {'sha': modfiles.sha256(files[rel]), 'original': original}
    # the manifest first: a write cut short leaves a record of what to put back
    modfiles.save_json(os.path.join(root, 'Mods', MANIFEST), {'version': 1, 'rows': base, 'files': entries})
    paths = []
    for rel, data in files.items():
        path = rel_path(root, rel)
        modfiles.atomic_write(path, data)
        paths.append(path)
    ini = set_rows(root, base)
    if ini:
        paths.append(ini)
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Puts back what each file was before (deleted if there was none): (restored or deleted, kept changed)."""
    modfiles.refuse_while_running()
    mpath = os.path.join(root, 'Mods', MANIFEST)
    manifest = load_manifest(root)
    done, kept = [], []
    for rel, entry in manifest.get('files', {}).items():
        path = rel_path(root, rel)
        data = modfiles.read(path)
        if data is None:
            continue
        if modfiles.sha256(data) != entry['sha']:
            kept.append(path)
            continue
        if entry['original'] is None:
            os.remove(path)
        else:
            modfiles.atomic_write(path, base64.b64decode(entry['original']))
        done.append(path)
    for d in (os.path.join(root, 'Mods', 'MISSION'),):
        if os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
    if os.path.isfile(mpath):
        os.remove(mpath)
    set_rows(root, 0)
    return done, kept


def installed(root: str) -> bool:
    return os.path.isfile(os.path.join(root, 'Mods', MANIFEST))


def check(root: str) -> bool:
    """Reading only: the files as written and the plugin's row count as the manifest says. True when complete."""
    manifest = load_manifest(root)
    if not manifest:
        print('EDF5 战役：未安装')
        return False
    bad = [rel for rel, e in manifest.get('files', {}).items()
           if modfiles.sha256(modfiles.read(rel_path(root, rel)) or b'') != e['sha']]
    path = os.path.join(root, 'Mods', INI)
    text = open(path, encoding='utf-8-sig').read() if os.path.isfile(path) else ''
    m = re.search(rf'(?m)^{INI_KEY}=(\d+)', text)
    rows_ok = m is not None and int(m.group(1)) == manifest.get('rows')
    print(f'EDF5 战役：{len(manifest.get("files", {}))} 个文件，缺失或被改过 {len(bad)}；'
          f'{INI_KEY}=' + (m.group(1) if m else '（缺失）') + ('' if rows_ok else f'（应为 {manifest.get("rows")}）'))
    for rel in bad:
        print('  缺失或被改过', rel)
    return not bad and rows_ok


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else rootcpk.DEFAULT_GAME
    if '--remove' in argv:
        done, kept = remove(root)
        for path in done:
            print('还原', path)
        for path in kept:
            print('保留（之后被别的工具改过）', path)
        return 0
    if '--list' in argv:
        p = plan(root)
        for r in p['rows']:
            print(r['group'], r['path'], r['map'], r['title']['SC'])
        for g, path, why in p['skipped']:
            print('跳过', g, path, why)
        print(len(p['rows']), '关')
        return 0
    try:
        built = build(root)
    except Refused as e:
        print('没有安装：', e)
        return 1
    for path in install(root, built):
        print('写入', path)
    print(f'追加 {len(built[2]["rows"])} 关（列表原有 {built[1]} 行）')
    for g, path, why in built[2]['skipped']:
        print('跳过', g, path, why)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
