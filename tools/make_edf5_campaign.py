"""Optional experimental EDF5 campaign: EDF5's story and its two DLCs as three mission packs of their own.

  python -B tools/make_edf5_campaign.py [GAME_DIR]            install (the game must be closed)
  python -B tools/make_edf5_campaign.py [GAME_DIR] --remove   put the files back as they were
  python -B tools/make_edf5_campaign.py [GAME_DIR] --list     what would be installed (nothing written)

EDF6's Root.cpk holds EDF5's scripts (MISSION/EDF5_OLD_SCRIPT/<id>/MISSION.BVM), subtitles
(MISSION/EDF6_VOICETABLE.*.SGO), and nearly all of EDF5's maps and objects. Its loose SOUND/PC/ directory holds
the voice banks TIKYUU5_VOICE.*.AWB. Script strings only add SetUndergroundSoundReverve compared with EDF5;
that does not establish native bytecode compatibility or completion of every mission. Four DLC missions lack
resources and are excluded.

The packs are modes of the game's mode table (docs/mission-list-re.md §7, static): the mission-pack dialog
(RoomFilter_ContentsName, "任务包") lists every offline mode of DefaultPackage/config.sgo's ModeList, named by its
first member's text key, and enables the ones whose content id the player owns. Each pack gets
  - a ModeList entry appended (offline, content ids after the highest in the list, its own save file DEFP_<MST>);
  - MISSION/MISSIONLIST_<PACK>.OFFLINE.LIST.SGO (DSGO), .TXT.<lang>.SGO (CN EN JA KR SC), .IMAGE.RAB: its own list,
    rows 0.., every row's 11th member named "flags", successors chained, a stock thumbnail of a mission on the map;
  - its name and description keys in ETC/TEXTTABLE_STEAM.<lang>.TXT_SGO (the dialog's and the mode screen's text).
EDF5's own titles and briefings (edf5campaign/missions.json, tools/make_edf5_campaign_text.py).
The content ids are not DLC the platform knows: the plugin owns them (src/edf5campaign.cpp, ini
EDF5CampaignContent = the first of the three, set here). Without the plugin the packs are listed but greyed out.
Packs of their own leave EDF6's story as it is: its list, save (M00.MST), ending, clear ratio and achievements
(those only count content 0). Their last mission plays no ending (the script has endings for contents 0..2 only).

The 2026-10-07 version appended the missions to EDF6's offline list instead (rows 147..281 of M00.MST). That list,
its texts and thumbnails are put back here as they were (the manifest kept them); clears recorded in those rows of
M00.MST are not carried into the packs' saves.

Files this replaces may be another mod's (CONFIG.SGO, the text tables): what was there is kept in the manifest
(Mods/.edf5campaign.json) and put back on removal; a file changed by someone since is left alone and reported.
"""
from __future__ import annotations

import base64
import copy
import json
import os
import re
import struct
import sys
from typing import Callable, TypeVar

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
DISABLED = '.edf5campaign-disabled'
INI = os.path.join('Plugins', 'EDF6VehicleCrew.ini')
INI_KEY = 'EDF5CampaignContent'
LANGS = ('CN', 'EN', 'JA', 'KR', 'SC')
CONFIG = 'DEFAULTPACKAGE/CONFIG.SGO'
TEXTS = {lang: f'ETC/TEXTTABLE_STEAM.{lang}.TXT_SGO' for lang in LANGS}   # app:/etc/TextTable_steam.%LOCALE%.txt_sgo
SCRIPTS = 'EDF5_OLD_SCRIPT'
# The values of EDF6's own 28 EDF5-era rows (RM015B2, M046, ...): the EDF5 soldiers' look, its menu music, flags 8
# (no branch bits 0x80 / 0x100, no 0x20).
BGM = 'BGM_E6M29_MissionEDF5'
FLAGS = 8.0
FIRST_CONTENT = 3   # EDF.dll knows contents 0..2 (0xE3F80's names, MAINSCRIPT's endings)
# ModeList entry members (docs/mission-list-re.md §7): 0 name key, 1 description key, 5 save file, 6 [list, text,
# thumbnails], 8 online, 9 content id, 10 mode type (0 offline scenario, 1 online).
M_NAME, M_DESC, M_MST, M_FILES, M_ONLINE, M_CONTENT, M_TYPE = 0, 1, 5, 6, 8, 9, 10
T = TypeVar('T')


class Pack:
    """One EDF5 mission pack: missions.json's group, its files' tag, its save file, the stock content whose mode it
    copies (difficulty ranges, image, music: EDF6's story for EDF5's, the matching DLC for EDF5's DLCs), its names."""

    def __init__(self, group: str, tag: str, mst: str, like: int, name: dict[str, str], desc: dict[str, str]) -> None:
        self.group, self.tag, self.mst, self.like, self.name, self.desc = group, tag, mst, like, name, desc
        self.list = f'MISSION/MISSIONLIST_{tag}.OFFLINE.LIST.SGO'
        self.image = f'MISSION/MISSIONLIST_{tag}.OFFLINE.IMAGE.RAB'
        self.txt = {lang: f'MISSION/MISSIONLIST_{tag}.OFFLINE.TXT.{lang}.SGO' for lang in LANGS}
        self.name_key = f'GameMode_Offline_{tag}'
        self.desc_key = f'GameMode_Desc_Offline_{tag}'

    def paths(self) -> list[str]:
        """The mode's [list, text, thumbnails] as config.sgo names them (%LOCALE% the language)."""
        return [f'app:/Mission/MissionList_{self.tag}.offline.list.sgo',
                f'app:/Mission/MissionList_{self.tag}.offline.txt.%LOCALE%.sgo',
                f'app:/Mission/MissionList_{self.tag}.offline.image.rab']

    def files(self) -> list[str]:
        return [self.list, self.image, *self.txt.values()]


_UNVERIFIED = {'CN': '（使用 EDF6 內建的 EDF5 任務腳本，尚未逐關實機驗證）',
               'EN': ' (EDF5 mission scripts shipped with EDF6; not every mission has been verified in game)',
               'JA': '（EDF6 に収録された EDF5 のミッションスクリプト。全ミッションの動作は未確認）',
               'KR': ' (EDF6에 수록된 EDF5 미션 스크립트, 모든 미션의 동작은 미확인)',
               'SC': '（使用 EDF6 自带的 EDF5 任务脚本，尚未逐关实机验证）'}


def _names(part: dict[str, str]) -> tuple[dict[str, str], dict[str, str]]:
    desc = {'CN': f'以離線方式遊玩 EDF5 {part["CN"]}。', 'EN': f'Play EDF5 {part["EN"]} offline.',
            'JA': f'EDF5 {part["JA"]}をオフラインでプレイします。', 'KR': f'EDF5 {part["KR"]}을(를) 오프라인으로 플레이합니다.',
            'SC': f'以离线方式游玩 EDF5 {part["SC"]}。'}
    return {lang: f'EDF5 {part[lang]}' for lang in LANGS}, {lang: desc[lang] + _UNVERIFIED[lang] for lang in LANGS}


PACKS = (
    Pack('main', 'EDF5', 'E5M0.MST', 0, *_names({'CN': '本篇', 'EN': 'Main Story', 'JA': '本編', 'KR': '본편', 'SC': '本篇'})),
    Pack('dlc1', 'EDF5DLC1', 'E5D1.MST', 1, *_names({'CN': '任務包１', 'EN': 'Mission Pack 1', 'JA': 'ミッションパック１',
                                                     'KR': '미션 팩 1', 'SC': '任务包１'})),
    Pack('dlc2', 'EDF5DLC2', 'E5D2.MST', 2, *_names({'CN': '任務包２', 'EN': 'Mission Pack 2', 'JA': 'ミッションパック２',
                                                     'KR': '미션 팩 2', 'SC': '任务包２'})),
)
FILES = (CONFIG, *TEXTS.values(), *(f for p in PACKS for f in p.files()))
# The 2026-10-07 version's files (appended to EDF6's own offline list): put back, never written any more.
LEGACY_LIST = 'MISSION/MISSIONLIST.OFFLINE.LIST.SGO'
LEGACY = (LEGACY_LIST, 'MISSION/MISSIONLIST.OFFLINE.IMAGE.RAB',
          *(f'MISSION/MISSIONLIST.OFFLINE.TXT.{lang}.SGO' for lang in LANGS))
LEGACY_INI_KEY = 'EDF5CampaignRows'
# A change by someone else to one of these keeps the whole group: they name the others (a mode its list, a list row
# its text and thumbnail) and the engine reads those unchecked.
ANCHORS = (CONFIG, LEGACY_LIST)


class Refused(RuntimeError):
    """The install would break something (said why); nothing was written."""


# ------------------------------------------------------------------------------------------- reading
def rel_path(root: str, rel: str) -> str:
    return os.path.join(root, 'Mods', *rel.split('/'))


def load_manifest(root: str) -> dict:
    return modfiles.load_json(os.path.join(root, 'Mods', MANIFEST), {})


def ours(entry: dict | None, data: bytes | None) -> bool:
    """`data` is a file this tool wrote: the manifest's sha, or while an install is writing (the manifest saved before
    the files) also the one it is replacing (`also`), so a run cut short leaves every file recognised either way."""
    return bool(entry) and data is not None and modfiles.sha256(data) in (entry['sha'], *entry.get('also', ()))


def restored(entry: dict | None, data: bytes | None) -> bool:
    """Removal already restored this backup before an interruption left its ledger entry behind."""
    return bool(entry) and entry['original'] is not None and data == base64.b64decode(entry['original'])


def base_bytes(root: str, game: rootcpk.Game, rel: str, manifest: dict) -> bytes:
    """What the list file is without this tool: the file it replaced (kept in the manifest) while ours is still in
    place, else the one in Mods (another mod's), else the game's."""
    entry = manifest.get('files', {}).get(rel)
    on_disk = modfiles.read(rel_path(root, rel))
    if ours(entry, on_disk):
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
    """What would be installed (rows by pack, skipped missions), from the game and the text; nothing written."""
    game = rootcpk.Game(root)
    text = json.load(open(TEXT, encoding='utf-8'))
    maps = map_names(root, game)
    names = root_names(game)
    rows, skipped = [], []
    for pack in PACKS:
        for m in text[pack.group]:
            folder = f'MISSION/{SCRIPTS}/{m["path"]}'
            try:
                bvm = game.read(folder, 'MISSION.BVM')
            except KeyError:
                skipped.append((pack.group, m['path'], 'MISSION.BVM 不在 Root.cpk'))
                continue
            gone = missing_assets(names, maps, bvm)
            if gone:
                skipped.append((pack.group, m['path'], '缺少 ' + ', '.join(sorted(set(gone)))))
                continue
            rows.append({'group': pack.group, 'path': m['path'], 'progress': m['progress'], 'map': first_map(bvm),
                         'title': {lang: m['title'][lang] for lang in LANGS},
                         'brief': {lang: m['brief'][lang] for lang in LANGS}})
    return {'rows': rows, 'skipped': skipped}


def parsed(rel: str, read: Callable[[], T]) -> T:
    """`read()` of a file that may be another mod's: one this tool cannot read refuses the install (said why,
    nothing written) instead of stopping the whole installer."""
    try:
        return read()
    except (ValueError, KeyError, IndexError, TypeError, AssertionError, struct.error, UnicodeDecodeError) as e:
        raise Refused(f'Mods/{rel} 读不了（{type(e).__name__}: {e}），可能是别的 MOD 用了别的格式：不改动它。') from e


def pack_list(game: rootcpk.Game, rows: list[dict]) -> bytes:
    """A pack's mission list: the stock offline list's document with the pack's rows, chained 0 -> 1 -> ... (the last
    row has none: a successor must be < the row count)."""
    doc = dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO'))
    table = doc.root.get('table').items
    table[:] = [new_row(i, r['path'], r['progress']) for i, r in enumerate(rows)]
    table[-1].items[3].items.clear()
    return dsgo.compact(doc)


def pack_text(game: rootcpk.Game, lang: str, rows: list[dict]) -> bytes:
    """A pack's titles and briefings, one [title, brief] per row (read by row, unchecked: one per list row)."""
    ver, members = sgo.read(game.read('MISSION', f'MISSIONLIST.OFFLINE.TXT.{lang}.SGO'))
    members['table'] = [[r['title'][lang], r['brief'][lang]] for r in rows]
    return sgo.write_depth_first(ver, members)


def pack_image(game: rootcpk.Game, rows: list[dict]) -> bytes:
    """A thumbnail per row (named by its key, EDF.dll 0xE0D80): a stock mission's on the same map, else M046's."""
    stock = dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
    rab = mdb.rab_read(game.read('MISSION', 'MISSIONLIST.OFFLINE.IMAGE.RAB'))
    by_name = {f.name.upper(): f for f in rab.files}
    by_map = stock_maps(game, stock)
    fallback = by_name.get(thumb_name('EDF6/M046').upper(), rab.files[0])
    files = []
    for r in rows:
        src = by_name.get(thumb_name(by_map.get(r['map'] or '', '')).upper(), fallback)
        files.append(mdb.RabFile(thumb_name(f'{SCRIPTS}/{r["path"]}'), src.folder, src.flag, src.stored, src.unk))
    rab.files = files
    return mdb.rab_write(rab)


def modes(members: dict) -> list:
    return members['ModeList']


def mode_entry(template: list, pack: Pack, content: int) -> list:
    """The pack's ModeList entry: a copy of the stock mode `template`, with the pack's names, save file, files and
    content id; offline (members 8, 10 = 0)."""
    entry = copy.deepcopy(template)
    entry[M_NAME], entry[M_DESC], entry[M_MST] = pack.name_key, pack.desc_key, pack.mst
    entry[M_FILES] = pack.paths()
    entry[M_ONLINE], entry[M_CONTENT], entry[M_TYPE] = 0, content, 0
    return entry


def pack_config(data: bytes) -> tuple[bytes, int]:
    """config.sgo with the packs' modes appended (the modes before them untouched) and the first pack's content id."""
    ver, members = sgo.read(data)
    table = modes(members)
    ours_there = [e[M_NAME] for e in table if str(e[M_FILES][0]).lower().startswith('app:/mission/missionlist_edf5')]
    if ours_there:
        raise Refused(f'CONFIG.SGO 里已经有 EDF5 任务包模式（{", ".join(map(str, ours_there))}），但不是本工具留下的那份：不重复添加。')
    msts = {str(e[M_MST]).upper() for e in table}
    if any(p.mst.upper() in msts for p in PACKS):
        raise Refused('CONFIG.SGO 里已有模式用了 EDF5 任务包的存档文件名：存档会混在一起，不添加。')
    content = max([FIRST_CONTENT, *(int(e[M_CONTENT]) + 1 for e in table)])
    offline = {int(e[M_CONTENT]): e for e in table if int(e[M_TYPE]) == 0 and int(e[M_ONLINE]) == 0}
    if 0 not in offline:
        raise Refused('CONFIG.SGO 里没有离线剧情模式：不知道任务包按什么取难度参数。')
    for k, pack in enumerate(PACKS):
        table.append(mode_entry(offline.get(pack.like, offline[0]), pack, content + k))
    return sgo.write_depth_first(ver, members), content


def text_table(data: bytes, lang: str) -> bytes:
    """The game's text table with the packs' mode names and descriptions (other keys untouched)."""
    ver, members = sgo.read(data)
    for pack in PACKS:
        members[pack.name_key] = pack.name[lang]
        members[pack.desc_key] = pack.desc[lang]
    return sgo.write_depth_first(ver, members)


def legacy_blocked(root: str, manifest: dict) -> bool:
    """The 2026-10-07 appended list was changed by someone since: it can no longer be put back."""
    entry = manifest.get('files', {}).get(LEGACY_LIST)
    data = modfiles.read(rel_path(root, LEGACY_LIST))
    return entry is not None and data is not None and not ours(entry, data) and not restored(entry, data)


def build(root: str) -> tuple[dict[str, bytes], int, dict]:
    """The files to write (rel under Mods -> bytes), the first pack's content id, and the plan."""
    manifest = load_manifest(root)
    if legacy_blocked(root, manifest):
        raise Refused('旧版 EDF5 战役追加过的离线任务列表之后被其他工具改过，不能还原：不改成任务包。')
    game = rootcpk.Game(root)
    p = plan(root)
    by_pack = {pack.group: [r for r in p['rows'] if r['group'] == pack.group] for pack in PACKS}
    empty = [pack.tag for pack in PACKS if not by_pack[pack.group]]
    if empty:
        raise Refused(f'没有可安装的任务：{", ".join(empty)}（不写入空任务包）。')
    config, content = parsed(CONFIG, lambda: pack_config(base_bytes(root, game, CONFIG, manifest)))
    out = {CONFIG: config}
    for lang, rel in TEXTS.items():
        out[rel] = parsed(rel, lambda rel=rel, lang=lang: text_table(base_bytes(root, game, rel, manifest), lang))
    for pack in PACKS:
        rows = by_pack[pack.group]
        if len(rows) > 512:
            raise Refused(f'{pack.tag} 有 {len(rows)} 关，超过存档 512 关的容量。')
        out[pack.list] = pack_list(game, rows)
        out[pack.image] = pack_image(game, rows)
        for lang, rel in pack.txt.items():
            out[rel] = pack_text(game, lang, rows)
    return out, content, p


# ------------------------------------------------------------------------------------------- writing
def key_setting(text: str, name: str) -> tuple[list[str], int | None, int]:
    """INI lines, the key's line index and insertion position in [VehicleCrew] (Win32 case/space rules)."""
    lines = text.splitlines()
    section = ''
    first = None
    end = None
    key = None
    for i, line in enumerate(lines):
        head = re.match(r'^\s*\[([^\]]+)\]', line)
        if head:
            if section == 'vehiclecrew' and end is None:
                end = i
            section = head.group(1).strip().lower()
            if section == 'vehiclecrew' and first is None:
                first = i
            continue
        if section == 'vehiclecrew' and key is None and re.match(rf'^\s*{name}\s*=', line, re.I):
            key = i
    if first is None:
        lines.append('[VehicleCrew]')
        end = len(lines)
    return lines, key, len(lines) if end is None else end


def set_content(root: str, value: int) -> str | None:
    """Set the plugin's [VehicleCrew] EDF5CampaignContent; the old row cap (EDF5CampaignRows, read by no plugin since)
    goes to 0. Unrelated sections and settings are kept."""
    path = os.path.join(root, 'Mods', INI)
    if not os.path.isfile(path):
        return None
    with open(path, encoding='utf-8-sig') as f:
        text = f.read()
    for name, v in ((INI_KEY, value), (LEGACY_INI_KEY, 0)):
        lines, key, end = key_setting(text, name)
        if key is None and name == LEGACY_INI_KEY:
            continue
        if key is None:
            lines.insert(end, f'{name}={v}')
        else:
            lines[key] = f'{name}={v}'
        text = '\n'.join(lines) + '\n'
    modfiles.atomic_write(path, text.encode('utf-8'))
    return path


def removal_blocked(root: str) -> bool:
    """A changed anchor (the mode table, or the old appended list) may still name the rest of the group."""
    manifest = load_manifest(root)
    for rel in ANCHORS:
        entry = manifest.get('files', {}).get(rel)
        data = modfiles.read(rel_path(root, rel))
        if entry is not None and data is not None and not ours(entry, data) and not restored(entry, data):
            return True
    return False


def _put_back(root: str, rel: str, entry: dict) -> str | None:
    """Puts back what `rel` was before this tool (deleted if nothing): its path, None when already so, or raises
    KeyError when someone changed it since (left alone)."""
    path = rel_path(root, rel)
    data = modfiles.read(path)
    if data is None or restored(entry, data):
        return None
    if not ours(entry, data):
        raise KeyError(rel)
    if entry['original'] is None:
        os.remove(path)
    else:
        modfiles.atomic_write(path, base64.b64decode(entry['original']))
    return path


def install(root: str, built: tuple[dict[str, bytes], int, dict] | None = None) -> list[str]:
    """Writes the files (the ones they replace kept in the manifest), puts back the older version's appended list,
    and sets EDF5CampaignContent."""
    modfiles.refuse_while_running()
    files, content, _ = built or build(root)
    mpath = os.path.join(root, 'Mods', MANIFEST)
    manifest = load_manifest(root)
    kept = manifest.get('files', {})
    entries: dict[str, dict] = {}
    for rel in files:
        old = kept.get(rel)
        on_disk = modfiles.read(rel_path(root, rel))
        entry = {'sha': modfiles.sha256(files[rel])}
        if ours(old, on_disk):   # ours from before: what it replaced stays what to put back
            entry['original'] = old['original']
            entry['also'] = [modfiles.sha256(on_disk)]
        else:
            entry['original'] = None if on_disk is None else base64.b64encode(on_disk).decode('ascii')
        entries[rel] = entry
    retired = {rel: e for rel, e in kept.items() if rel not in files}   # the older version's, put back below
    # Commit enable intent before the first resource mutation. If writing fails, ordinary updates must repair
    # the manifest's partial install rather than treating the earlier opt-out as still in force.
    disabled = os.path.join(root, 'Mods', DISABLED)
    if os.path.isfile(disabled):
        os.remove(disabled)
    # The manifest first, accepting the old bytes and the new, and still holding what is to be put back: a run cut
    # short leaves every file recognised (and what it replaced on record); once all are written only the new are ours.
    modfiles.save_json(mpath, {'version': 2, 'content': content, 'files': {**retired, **entries}})
    paths = []
    for rel, data in files.items():
        path = rel_path(root, rel)
        modfiles.atomic_write(path, data)
        paths.append(path)
    left = {}
    for rel, entry in retired.items():
        try:
            path = _put_back(root, rel, entry)
        except KeyError:
            left[rel] = entry
            continue
        if path:
            paths.append(path)
    for entry in entries.values():
        entry.pop('also', None)
    modfiles.save_json(mpath, {'version': 2, 'content': content, 'files': {**left, **entries}})
    ini = set_content(root, content)
    if ini:
        paths.append(ini)
    return paths


def remove(root: str, *, remember_disabled: bool = False) -> tuple[list[str], list[str]]:
    """Puts back what each file was before (deleted if there was none): (restored or deleted, kept changed).
    A changed anchor preserves the whole dependency group and its recovery records. Other changed files stay
    with their manifest entry (what they replaced is only there)."""
    modfiles.refuse_while_running()
    mpath = os.path.join(root, 'Mods', MANIFEST)
    manifest = load_manifest(root)
    if removal_blocked(root):
        # The mode table names the packs' lists and the old list's rows index its text/thumbnails, unchecked:
        # deleting the rest would leave them reading files that are gone. Keep the recovery metadata too.
        return [], [rel_path(root, rel) for rel in manifest.get('files', {})
                    if os.path.isfile(rel_path(root, rel))]
    if remember_disabled:
        # Record the choice before removing files: an interrupted removal must not opt back in on update.
        modfiles.atomic_write(os.path.join(root, 'Mods', DISABLED), b'Disabled by the user.\n')
    done, kept = [], []
    left: dict[str, dict] = {}
    for rel, entry in manifest.get('files', {}).items():
        try:
            path = _put_back(root, rel, entry)
        except KeyError:
            kept.append(rel_path(root, rel))
            left[rel] = entry
            continue
        if path:
            done.append(path)
    for d in ('MISSION', 'ETC', 'DEFAULTPACKAGE'):
        d = os.path.join(root, 'Mods', d)
        if os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
    if left:
        modfiles.save_json(mpath, {**manifest, 'files': left})
    elif os.path.isfile(mpath):
        os.remove(mpath)
    if not any(rel in left for rel in ANCHORS):
        set_content(root, 0)
    return done, kept


def enabled(root: str) -> bool:
    """The owned mode table (or the older version's owned list) records installation; leftover edited image/text
    files do not count."""
    files = load_manifest(root).get('files', {})
    return any(rel in files for rel in ANCHORS)


def wanted(root: str) -> bool:
    """Install by default, unless the user explicitly disabled the campaign."""
    return not os.path.isfile(os.path.join(root, 'Mods', DISABLED))


def installed(root: str) -> bool:
    return os.path.isfile(os.path.join(root, 'Mods', MANIFEST))


def ini_value(root: str, name: str) -> int | None:
    path = os.path.join(root, 'Mods', INI)
    text = ''
    if os.path.isfile(path):
        with open(path, encoding='utf-8-sig') as f:
            text = f.read()
    lines, key, _ = key_setting(text, name)
    m = re.match(rf'^\s*{name}\s*=\s*(\d+)\s*$', lines[key], re.I) if key is not None else None
    return int(m.group(1)) if m else None


def check(root: str) -> bool:
    """Reading only: the files as written and the plugin's content id as the manifest says. True when complete, or
    not installed (it is optional: refused when another mod's files cannot take it, which the install says)."""
    manifest = load_manifest(root)
    files = manifest.get('files', {})
    if not enabled(root):
        print('EDF5 战役：未启用（可选实验；安装器菜单 6 管理，保留的其他工具文件不算已启用）')
        return True
    if CONFIG not in files:
        print('EDF5 战役：还是旧版（追加在 EDF6 离线任务列表末尾）；重新安装会改成 3 个独立任务包')
        return False
    bad = [rel for rel, e in files.items()
           if 'also' in e or rel not in FILES or modfiles.sha256_file(rel_path(root, rel)) != e['sha']]
    value = ini_value(root, INI_KEY)
    content_ok = value is not None and value == manifest.get('content')
    print(f'EDF5 战役：3 个任务包，{len(files)} 个文件，缺失、被改过或待还原 {len(bad)}；'
          f'{INI_KEY}=' + ('（缺失）' if value is None else str(value))
          + ('' if content_ok else f'（应为 {manifest.get("content")}）'))
    for rel in bad:
        print('  缺失、被改过或待还原', rel)
    return not bad and content_ok


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else rootcpk.DEFAULT_GAME
    if '--remove' in argv:
        done, kept = remove(root, remember_disabled=True)
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
    print(summary(built))
    for g, path, why in built[2]['skipped']:
        print('跳过', g, path, why)
    return 0


def summary(built: tuple[dict[str, bytes], int, dict]) -> str:
    counts = '、'.join(f'{pack.name["SC"]} {sum(r["group"] == pack.group for r in built[2]["rows"])} 关' for pack in PACKS)
    return f'EDF5 任务包：{counts}（离线模式的「任务包」列表里选择；内容编号 {built[1]}–{built[1] + len(PACKS) - 1}）'


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
