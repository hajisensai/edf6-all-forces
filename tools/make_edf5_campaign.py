"""Optional experimental EDF5 campaign: EDF5's story and its two DLCs as three mission packs of their own, each
offline and online (more than one player: split screen offline as the stock modes, online rooms).

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
  - ModeList entries appended, an offline and an online one (one content id after the highest in the list, one save
    file DEFP_<MST> for both, as the stock packs);
  - MISSION/MISSIONLIST_<PACK>.{OFFLINE,ONLINE}.LIST.SGO (DSGO), .TXT.<lang>.SGO (CN EN JA KR SC), .IMAGE.RAB: its own list,
    rows 0.., every row's 11th member named "flags", successors chained, a stock thumbnail of a mission on the map;
  - its name and description keys in ETC/TEXTTABLE_STEAM.<lang>.TXT_SGO (the dialog's and the mode screen's text).
EDF5's own titles and briefings (edf5campaign/missions.json, tools/make_edf5_campaign_text.py).
The content ids are not DLC the platform knows: the plugin owns them (src/edf5campaign.cpp, ini
EDF5CampaignContent = the first of the three, set here). Without the plugin the packs are listed but greyed out.
Packs of their own leave EDF6's story as it is: its list, save (M00.MST), ending, clear ratio and achievements
(those only count content 0). Their last mission plays no ending (the script has endings for contents 0..2 only).

The test range (testrange/gen.py) is a mission pack of the same kind, always installed with the plugin (RANGE, an
offline and an online mode, one row, ini TestRangeContent): it used to replace offline mission RM015 (item 14 「转机」),
whose script never ends, so the story could not go past it (2026-10-09). A pack's row 0 is open from the start, and
its own save keeps the story's untouched. The EDF5 packs are optional (disable(): the range pack stays); both share
this tool's manifest because they share CONFIG.SGO and the text tables.

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
from typing import Callable, NamedTuple, TypeVar

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
# EDF5's own thumbnails of these missions (EDF5's three offline IMAGE.RABs merged, names as EDF5 has them: the
# mission's path with '/' as '_', M001.dds, DLC_DM011.dds); tools/make_edf5_campaign_text.py, committed like TEXT.
THUMBS = os.path.join(os.path.dirname(TEXT), 'thumbnails.rab')
MANIFEST = '.edf5campaign.json'
DISABLED = '.edf5campaign-disabled'
INI = os.path.join('Plugins', 'EDF6VehicleCrew.ini')
INI_KEY = 'EDF5CampaignContent'
RANGE_INI_KEY = 'TestRangeContent'
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


OFFLINE, ONLINE = 'Offline', 'Online'   # a mode's kind: its file names, its text keys, ModeList members 8 and 10


class Pack:
    """One mission pack: its rows' group, its files' tag, its save file, the stock content whose mode it copies
    (difficulty ranges, image, music: EDF6's story for EDF5's, the matching DLC for EDF5's DLCs), its names, and its
    kinds (offline; the test range online too, as the stock packs are: one save file for both)."""

    def __init__(self, group: str, tag: str, mst: str, like: int, name: dict[str, str], desc: dict[str, str],
                 kinds: tuple[str, ...] = (OFFLINE,)) -> None:
        self.group, self.tag, self.mst, self.like, self.name, self.desc = group, tag, mst, like, name, desc
        self.kinds = kinds
        self.list, self.image, self.txt = self.kind_files(OFFLINE)
        self.name_key, self.desc_key = self.keys(OFFLINE)

    def kind_files(self, kind: str) -> tuple[str, str, dict[str, str]]:
        """The kind's list, thumbnails and texts (rel under Mods)."""
        stem = f'MISSION/MISSIONLIST_{self.tag}.{kind.upper()}'
        return f'{stem}.LIST.SGO', f'{stem}.IMAGE.RAB', {lang: f'{stem}.TXT.{lang}.SGO' for lang in LANGS}

    def keys(self, kind: str) -> tuple[str, str]:
        """The kind's name and description text keys (the stock ones: GameMode_Offline_MissionPack01, ...)."""
        return f'GameMode_{kind}_{self.tag}', f'GameMode_Desc_{kind}_{self.tag}'

    def paths(self, kind: str = OFFLINE) -> list[str]:
        """The mode's [list, text, thumbnails] as config.sgo names them (%LOCALE% the language)."""
        stem = f'app:/Mission/MissionList_{self.tag}.{kind.lower()}'
        return [f'{stem}.list.sgo', f'{stem}.txt.%LOCALE%.sgo', f'{stem}.image.rab']

    def files(self) -> list[str]:
        out = []
        for kind in self.kinds:
            listed, image, txt = self.kind_files(kind)
            out += [listed, image, *txt.values()]
        return out


_UNVERIFIED = {'CN': '（使用 EDF6 內建的 EDF5 任務腳本，尚未逐關實機驗證）',
               'EN': ' (EDF5 mission scripts shipped with EDF6; not every mission has been verified in game)',
               'JA': '（EDF6 に収録された EDF5 のミッションスクリプト。全ミッションの動作は未確認）',
               'KR': ' (EDF6에 수록된 EDF5 미션 스크립트, 모든 미션의 동작은 미확인)',
               'SC': '（使用 EDF6 自带的 EDF5 任务脚本，尚未逐关实机验证）'}


def _names(part: dict[str, str]) -> tuple[dict[str, str], dict[str, str]]:
    # One text for both kinds (the pack's offline and online modes): no 'offline' in it.
    desc = {'CN': f'遊玩 EDF5 {part["CN"]}。', 'EN': f'Play EDF5 {part["EN"]}.',
            'JA': f'EDF5 {part["JA"]}をプレイします。', 'KR': f'EDF5 {part["KR"]}을(를) 플레이합니다.',
            'SC': f'游玩 EDF5 {part["SC"]}。'}
    return {lang: f'EDF5 {part[lang]}' for lang in LANGS}, {lang: desc[lang] + _UNVERIFIED[lang] for lang in LANGS}


# Each an offline and an online mode (the user, 2026-10-09: the packs for more than one player, not single-player only),
# as the stock packs: one content id and one save file for both, the online one a copy of the stock online mode.
PACKS = (
    Pack('main', 'EDF5', 'E5M0.MST', 0, *_names({'CN': '本篇', 'EN': 'Main Story', 'JA': '本編', 'KR': '본편', 'SC': '本篇'}),
         (OFFLINE, ONLINE)),
    Pack('dlc1', 'EDF5DLC1', 'E5D1.MST', 1, *_names({'CN': '任務包１', 'EN': 'Mission Pack 1', 'JA': 'ミッションパック１',
                                                     'KR': '미션 팩 1', 'SC': '任务包１'}), (OFFLINE, ONLINE)),
    Pack('dlc2', 'EDF5DLC2', 'E5D2.MST', 2, *_names({'CN': '任務包２', 'EN': 'Mission Pack 2', 'JA': 'ミッションパック２',
                                                     'KR': '미션 팩 2', 'SC': '任务包２'}), (OFFLINE, ONLINE)),
)
# The test range's pack. Its one row is testrange/gen.py's mission folder (Mods/MISSION/EDF6/EDF6TR_RANGE: the
# script, the site's points), with RM015's row values (progress 0.2, its music, flags 8): an EDF-era row, where the
# Air Raider's vehicle and air support requests arrive (the ruined world's rows 0..12 have flags 65/66 and 2/1 in
# members 6/7, and there they never come). Thumbnail: the plain's (M045), the default site.
RANGE_MISSION = 'EDF6TR_RANGE'
RANGE = Pack('range', 'EDF6TR', 'TR00.MST', 0,
             {'CN': 'EDF6VehicleCrew 測試場', 'EN': 'EDF6VehicleCrew Test Range', 'JA': 'EDF6VehicleCrew テストレンジ',
              'KR': 'EDF6VehicleCrew 테스트 레인지', 'SC': 'EDF6VehicleCrew 测试场'},
             {'CN': '試駕 EDF6VehicleCrew 的載具與武器：只有靶子，沒有敵人；不影響劇情進度。',
              'EN': "Try EDF6VehicleCrew's vehicles and weapons: targets only, no enemies; the story's progress is untouched.",
              'JA': 'EDF6VehicleCrew のビークルと武器を試せます。標的のみで敵は出ません。本編の進行には影響しません。',
              'KR': 'EDF6VehicleCrew의 탈것과 무기를 시험합니다. 표적만 있고 적은 없으며 본편 진행에는 영향이 없습니다.',
              'SC': '试驾 EDF6VehicleCrew 的载具与武器：只有靶子，没有敌人；不影响剧情进度。'},
             (OFFLINE, ONLINE))
RANGE_LIKE = 'EDF6/RM015'   # the stock row whose music, progress and flags the range's row copies (range_row)
RANGE_ROW = {'group': 'range', 'key': f'EDF6/{RANGE_MISSION}', 'map': None, 'thumb': 'EDF6/M045',
             'title': {lang: RANGE.name[lang] for lang in LANGS},
             'brief': {'CN': '開闊的平原，載具停在出生點附近，靶子在遠處（一部分在空中）。\n這一關不會結束：測完從暫停選單撤退。',
                       'EN': 'An open plain: the vehicles wait by the start, the targets are far off (some in the air).\n'
                             'This mission never ends: withdraw from the pause menu when done.',
                       'JA': '開けた平原。ビークルはスタート地点の近くに、標的は遠く（一部は空中）にあります。\n'
                             'このミッションは終わりません。テストが済んだらポーズメニューから撤退してください。',
                       'KR': '탁 트인 평원. 탈것은 시작 지점 근처에, 표적은 멀리(일부는 공중)에 있습니다.\n'
                             '이 미션은 끝나지 않습니다. 시험이 끝나면 일시 정지 메뉴에서 철수하십시오.',
                       'SC': '开阔的平原，载具停在出生点附近，靶子在远处（一部分在空中）。\n这一关不会结束：测完从暂停菜单撤退。'}}
FILES = (CONFIG, *TEXTS.values(), *(f for p in (*PACKS, RANGE) for f in p.files()))
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


class Contents(NamedTuple):
    """The content ids the plugin owns (its ini): the first EDF5 pack's (0 = the campaign is not installed) and the
    test range's."""
    campaign: int
    range: int


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


def new_row(i: int, row: dict) -> dsgo.Node:
    key = row['key']
    return dsgo.Node([float(i), f'app:/Mission/{key}', key, dsgo.Node([float(i + 1)]), float(i), 0.0, 0.0, 0.0,
                      row['bgm'], float(row['progress']), float(row['flags'])], {10: 'flags'})


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
            rows.append({'group': pack.group, 'path': m['path'], 'key': f'{SCRIPTS}/{m["path"]}', 'bgm': BGM,
                         'flags': FLAGS, 'progress': m['progress'], 'map': first_map(bvm),
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


def pack_list(game: rootcpk.Game, rows: list[dict], kind: str = OFFLINE) -> bytes:
    """A pack's mission list: the stock list's document (of the same kind) with the pack's rows, chained 0 -> 1 -> ...
    (the last row has none: a successor must be < the row count)."""
    doc = dsgo.parse(game.read('MISSION', f'MISSIONLIST.{kind.upper()}.LIST.SGO'))
    table = doc.root.get('table').items
    table[:] = [new_row(i, r) for i, r in enumerate(rows)]
    table[-1].items[3].items.clear()
    return dsgo.compact(doc)


def pack_text(game: rootcpk.Game, lang: str, rows: list[dict], kind: str = OFFLINE) -> bytes:
    """A pack's titles and briefings, one [title, brief] per row (read by row, unchecked: one per list row)."""
    ver, members = sgo.read(game.read('MISSION', f'MISSIONLIST.{kind.upper()}.TXT.{lang}.SGO'))
    members['table'] = [[r['title'][lang], r['brief'][lang]] for r in rows]
    return sgo.write_depth_first(ver, members)


def edf5_thumbs() -> dict[str, mdb.RabFile]:
    """EDF5's thumbnails (THUMBS) by upper-case name."""
    with open(THUMBS, 'rb') as f:
        return {x.name.upper(): x for x in mdb.rab_read(f.read()).files}


def pick_thumbs(rows: list[dict], own: dict[str, mdb.RabFile], stock: dict[str, mdb.RabFile],
                by_map: dict[str, str]) -> list[mdb.RabFile]:
    """A thumbnail per row, named by its key (EDF.dll 0xE0D80): a row with a stock 'thumb' (the range) shows that one;
    an EDF5 mission EDF5's own (`own`, THUMBS: 440x220 DXT1 like EDF6's), else a stock mission's on its map, else
    M046's. `own` and `stock` by upper-case name, `by_map` map file -> stock key (stock_maps)."""
    fallback = stock.get(thumb_name('EDF6/M046').upper()) or next(iter(stock.values()))
    out = []
    for r in rows:
        mine = None if r.get('thumb') else own.get(thumb_name(r.get('path', '')).upper())
        src = mine or stock.get(thumb_name(r.get('thumb') or by_map.get(r['map'] or '', '')).upper(), fallback)
        out.append(mdb.RabFile(thumb_name(r['key']), src.folder, src.flag, src.stored, src.unk))
    return out


def pack_image(game: rootcpk.Game, rows: list[dict], kind: str = OFFLINE) -> bytes:
    """The kind's thumbnails RAB of `rows` (pick_thumbs), in the stock one's layout."""
    rab = mdb.rab_read(game.read('MISSION', f'MISSIONLIST.{kind.upper()}.IMAGE.RAB'))
    listed = dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
    by_map = stock_maps(game, listed) if any(not r.get('thumb') for r in rows) else {}
    own = edf5_thumbs() if any(r.get('path') for r in rows) else {}
    rab.files = pick_thumbs(rows, own, {f.name.upper(): f for f in rab.files}, by_map)
    return mdb.rab_write(rab)


def range_row(game: rootcpk.Game) -> dict:
    """The test range's row: RANGE_ROW with RANGE_LIKE's music, progress and flags as the game has them."""
    table = dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
    like = next((r for r in table if r.items[2] == RANGE_LIKE), None)
    if like is None:
        raise Refused(f'原版离线任务列表里没有 {RANGE_LIKE}：不知道测试场这一关按什么取参数。')
    return {**RANGE_ROW, 'bgm': like.items[8], 'progress': like.items[9], 'flags': like.items[10]}


def pack_files(game: rootcpk.Game, pack: Pack, rows: list[dict]) -> dict[str, bytes]:
    """Every kind's list, thumbnails and texts of `pack` (rel under Mods -> bytes)."""
    out = {}
    for kind in pack.kinds:
        listed, image, txt = pack.kind_files(kind)
        out[listed] = pack_list(game, rows, kind)
        out[image] = pack_image(game, rows, kind)
        for lang, rel in txt.items():
            out[rel] = pack_text(game, lang, rows, kind)
    return out


def modes(members: dict) -> list:
    return members['ModeList']


def mode_entry(template: list, pack: Pack, content: int, kind: str = OFFLINE) -> list:
    """The pack's ModeList entry of `kind`: a copy of the stock mode `template` (of that kind), with the pack's names,
    save file, files and content id; members 8 and 10 are 0 offline, 1 online (the stock modes')."""
    entry = copy.deepcopy(template)
    entry[M_NAME], entry[M_DESC] = pack.keys(kind)
    entry[M_MST], entry[M_FILES] = pack.mst, pack.paths(kind)
    online = int(kind == ONLINE)
    entry[M_ONLINE], entry[M_CONTENT], entry[M_TYPE] = online, content, online
    return entry


def ours_in(table: list) -> list[str]:
    """Modes in `table` naming a pack's files (none of this tool's: base_bytes reads what it replaced)."""
    stems = tuple(f'app:/mission/missionlist_{p.tag.lower()}.' for p in (*PACKS, RANGE))
    return [str(e[M_NAME]) for e in table if str(e[M_FILES][0]).lower().startswith(stems)]


def pack_config(data: bytes, packs: tuple[Pack, ...] = PACKS) -> tuple[bytes, dict[str, int]]:
    """config.sgo with the packs' modes appended (the modes before them untouched) and each pack's content id (by
    tag): after every id in the table, each pack at its fixed place in (*PACKS, RANGE) whichever packs are given. A
    room's mode is its content id: the test range's must not move to the EDF5 story's when a player opted out of the
    campaign (the host on M01, the guest on the range)."""
    ver, members = sgo.read(data)
    table = modes(members)
    ours_there = ours_in(table)
    if ours_there:
        raise Refused(f'CONFIG.SGO 里已经有任务包模式（{", ".join(ours_there)}），但不是本工具留下的那份：不重复添加。')
    msts = {str(e[M_MST]).upper() for e in table}
    if any(p.mst.upper() in msts for p in packs):
        raise Refused('CONFIG.SGO 里已有模式用了任务包的存档文件名：存档会混在一起，不添加。')
    content = max([FIRST_CONTENT, *(int(e[M_CONTENT]) + 1 for e in table)])
    stock = {kind: {int(e[M_CONTENT]): e for e in table if int(e[M_TYPE]) == int(e[M_ONLINE]) == int(kind == ONLINE)}
             for kind in (OFFLINE, ONLINE)}
    for kind in {k for p in packs for k in p.kinds}:
        if 0 not in stock[kind]:
            raise Refused(f'CONFIG.SGO 里没有{"在线" if kind == ONLINE else "离线"}剧情模式：不知道任务包按什么取难度参数。')
    ids = {}
    slots = {p.tag: k for k, p in enumerate((*PACKS, RANGE))}
    for pack in packs:
        ids[pack.tag] = content + slots[pack.tag]
        for kind in pack.kinds:
            table.append(mode_entry(stock[kind].get(pack.like, stock[kind][0]), pack, ids[pack.tag], kind))
    return sgo.write_depth_first(ver, members), ids


def text_table(data: bytes, lang: str, packs: tuple[Pack, ...] = PACKS) -> bytes:
    """The game's text table with the packs' mode names and descriptions (other keys untouched)."""
    ver, members = sgo.read(data)
    for pack in packs:
        for kind in pack.kinds:
            name_key, desc_key = pack.keys(kind)
            members[name_key] = pack.name[lang]
            members[desc_key] = pack.desc[lang]
    return sgo.write_depth_first(ver, members)


def legacy_blocked(root: str, manifest: dict) -> bool:
    """The 2026-10-07 appended list was changed by someone since: it can no longer be put back."""
    entry = manifest.get('files', {}).get(LEGACY_LIST)
    data = modfiles.read(rel_path(root, LEGACY_LIST))
    return entry is not None and data is not None and not ours(entry, data) and not restored(entry, data)


def build(root: str, campaign: bool = True, test_range: bool = True) -> tuple[dict[str, bytes], Contents, dict]:
    """The files to write (rel under Mods -> bytes), the content ids, and the plan: with `campaign` the three EDF5
    packs, with `test_range` the test range's after them (the EDF5 ids first, as before the range had a pack)."""
    if not (campaign or test_range):
        raise ValueError('no pack to build: remove() takes them all out')
    manifest = load_manifest(root)
    if legacy_blocked(root, manifest):
        raise Refused('旧版 EDF5 战役追加过的离线任务列表之后被其他工具改过，不能还原：不改成任务包。')
    game = rootcpk.Game(root)
    p = plan(root) if campaign else {'rows': [], 'skipped': []}
    by_pack = {pack.group: [r for r in p['rows'] if r['group'] == pack.group] for pack in PACKS}
    empty = [pack.tag for pack in PACKS if campaign and not by_pack[pack.group]]
    if empty:
        raise Refused(f'没有可安装的任务：{", ".join(empty)}（不写入空任务包）。')
    by_pack[RANGE.group] = [range_row(game)] if test_range else []
    packs = (*(PACKS if campaign else ()), *((RANGE,) if test_range else ()))
    config, ids = parsed(CONFIG, lambda: pack_config(base_bytes(root, game, CONFIG, manifest), packs))
    out = {CONFIG: config}
    for lang, rel in TEXTS.items():
        out[rel] = parsed(rel, lambda rel=rel, lang=lang: text_table(base_bytes(root, game, rel, manifest), lang, packs))
    for pack in packs:
        rows = by_pack[pack.group]
        if len(rows) > 512:
            raise Refused(f'{pack.tag} 有 {len(rows)} 关，超过存档 512 关的容量。')
        out.update(pack_files(game, pack, rows))
    return out, Contents(ids.get(PACKS[0].tag, 0), ids.get(RANGE.tag, 0)), p


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


def set_content(root: str, value: Contents) -> str | None:
    """Set the plugin's [VehicleCrew] EDF5CampaignContent and TestRangeContent; the old row cap (EDF5CampaignRows, read
    by no plugin since) goes to 0. Unrelated sections and settings are kept."""
    path = os.path.join(root, 'Mods', INI)
    if not os.path.isfile(path):
        return None
    with open(path, encoding='utf-8-sig') as f:
        text = f.read()
    for name, v in ((INI_KEY, value.campaign), (RANGE_INI_KEY, value.range), (LEGACY_INI_KEY, 0)):
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


def manifest_record(content: Contents, files: dict[str, dict]) -> dict:
    return {'version': 2, 'content': content.campaign, 'range': content.range, 'files': files}


def install(root: str, built: tuple[dict[str, bytes], Contents, dict] | None = None) -> list[str]:
    """Writes the files (the ones they replace kept in the manifest), puts back the older version's appended list,
    and sets the plugin's content ids. A build without the campaign (disable()) takes its packs out: the mode and
    text tables are rebuilt from what they replaced, and the EDF5 packs' own files, no longer written, are retired."""
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
    if content.campaign and os.path.isfile(disabled):
        os.remove(disabled)
    # The manifest first, accepting the old bytes and the new, and still holding what is to be put back: a run cut
    # short leaves every file recognised (and what it replaced on record); once all are written only the new are ours.
    modfiles.save_json(mpath, manifest_record(content, {**retired, **entries}))
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
    modfiles.save_json(mpath, manifest_record(content, {**left, **entries}))
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
        set_content(root, Contents(0, 0))
    return done, kept


def set_packs(root: str, campaign: bool, test_range: bool) -> list[str]:
    """The mode table with exactly these packs: rebuilt from what it replaced, or put back as it was when there are
    none. Raises Refused when it cannot be rebuilt (nothing written) or put back (a table changed since)."""
    if campaign or test_range:
        return install(root, build(root, campaign, test_range))
    done, kept = remove(root)
    if kept:
        raise Refused('模式表（CONFIG.SGO）或旧版任务列表被其他工具改过，不能还原：保留任务包文件。')
    return done


def disable(root: str) -> list[str]:
    """The user's opt-out of the EDF5 packs: the packs rebuilt without them (the test range's stays). Built before the
    choice is recorded, so a refusal (nothing written) leaves the campaign wanted as it is; recorded before anything is
    written, so an interrupted run is finished by the next update instead of opting back in."""
    modfiles.refuse_while_running()
    test_range = range_installed(root)
    built = build(root, False, test_range) if test_range else None
    if built is None and removal_blocked(root):
        raise Refused('模式表（CONFIG.SGO）或旧版任务列表被其他工具改过，不能还原：保留任务包文件。')
    modfiles.atomic_write(os.path.join(root, 'Mods', DISABLED), b'Disabled by the user.\n')
    return install(root, built) if built is not None else set_packs(root, False, False)


def enabled(root: str) -> bool:
    """The EDF5 packs are installed: the owned mode table with the campaign's content id (a manifest from before the
    test range had a pack has no 'range' and was all campaign), or the older version's owned list; leftover edited
    image/text files do not count."""
    manifest = load_manifest(root)
    files = manifest.get('files', {})
    if CONFIG in files:
        return bool(manifest.get('content')) or 'range' not in manifest
    return LEGACY_LIST in files


def range_installed(root: str) -> bool:
    """The test range's pack is in the owned mode table."""
    manifest = load_manifest(root)
    return CONFIG in manifest.get('files', {}) and bool(manifest.get('range'))


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
    if CONFIG not in files:
        if LEGACY_LIST in files:
            print('任务包：EDF5 战役还是旧版（追加在 EDF6 离线任务列表末尾）；重新安装会改成独立任务包')
            return False
        print('任务包：未安装（测试场任务包由安装器写入；EDF5 战役可选，安装器菜单 6 管理）')
        return True
    # The installed packs' files must be as written; a record kept for a retired file another tool changed since
    # (a pack taken out, what it replaced kept to put back) is reported, not a fault.
    current = {CONFIG, *TEXTS.values(), *(f for p in PACKS if enabled(root) for f in p.files()),
               *(RANGE.files() if range_installed(root) else ())}

    def foreign(rel: str, entry: dict) -> bool:   # a retired file someone changed since: kept, its record with it
        data = modfiles.read(rel_path(root, rel))
        return rel not in current and data is not None and not ours(entry, data) and not restored(entry, data)

    kept = [rel for rel, e in files.items() if 'also' not in e and rel in FILES and foreign(rel, e)]
    bad = [rel for rel, e in files.items() if rel not in kept and (
        'also' in e or rel not in FILES or rel not in current or modfiles.sha256_file(rel_path(root, rel)) != e['sha'])]
    for rel in kept:
        print('  保留（别的工具改过，卸载时不动）', rel)
    wanted_ids = ((INI_KEY, manifest.get('content', 0)), (RANGE_INI_KEY, manifest.get('range', 0)))
    values = {name: ini_value(root, name) for name, _ in wanted_ids}
    ids_ok = all(values[name] == want for name, want in wanted_ids)
    packs = (['EDF5 战役 3 个'] if enabled(root) else []) + (['测试场'] if range_installed(root) else [])
    print(f'任务包：{"、".join(packs) or "无"}，{len(files)} 个文件，缺失、被改过或待还原 {len(bad)}；'
          + '，'.join(f'{name}=' + ('（缺失）' if values[name] is None else str(values[name]))
                     + ('' if values[name] == want else f'（应为 {want}）') for name, want in wanted_ids))
    for rel in bad:
        print('  缺失、被改过或待还原', rel)
    return not bad and ids_ok


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


def summary(built: tuple[dict[str, bytes], Contents, dict]) -> str:
    content = built[1]
    out = f'测试场任务包（内容编号 {content.range}，离线和在线的「任务包」里选「{RANGE.name["SC"]}」）'
    if not content.campaign:
        return out
    counts = '、'.join(f'{pack.name["SC"]} {sum(r["group"] == pack.group for r in built[2]["rows"])} 关' for pack in PACKS)
    return (f'EDF5 任务包：{counts}（离线模式的「任务包」列表里选择；内容编号 {content.campaign}–'
            f'{content.campaign + len(PACKS) - 1}）；' + out)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
