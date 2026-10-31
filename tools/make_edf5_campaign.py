"""Optional experimental EDF5 campaign: EDF5's story and its two DLCs as three mission packs of their own, each
offline and online (more than one player: split screen offline as the stock modes, online rooms).

  python -B tools/make_edf5_campaign.py [GAME_DIR]            install (the game must be closed)
  python -B tools/make_edf5_campaign.py [GAME_DIR] --remove   put the files back as they were
  python -B tools/make_edf5_campaign.py [GAME_DIR] --list     what would be installed (nothing written)

EDF6's Root.cpk holds EDF5's scripts (MISSION/EDF5_OLD_SCRIPT/<id>/MISSION.BVM), subtitles
(MISSION/EDF6_VOICETABLE.*.SGO), and nearly all of EDF5's maps and objects. Its loose SOUND/PC/ directory holds
the voice banks TIKYUU5_VOICE.*.AWB. Script strings only add SetUndergroundSoundReverve compared with EDF5;
that does not establish native bytecode compatibility or completion of every mission. Four DLC missions need what
EDF6 lacks (two maps, the DLC fortress): tools/edf5_dlc.py installs it, pre-generated; one whose part it leaves out
is a placeholder row (not playable, says why).

The packs are modes of the game's mode table (docs/mission-list-re.md §7, static): the mission-pack dialog
(RoomFilter_ContentsName, "任务包") lists every offline mode of DefaultPackage/config.sgo's ModeList, named by its
first member's text key, and enables the ones whose content id the player owns. Each pack gets
  - ModeList entries appended, an offline and an online one (the pack's fixed content id, CONTENT; one save file
    DEFP_<MST> for both, as the stock packs);
  - MISSION/MISSIONLIST_<PACK>.{OFFLINE,ONLINE}.LIST.SGO (DSGO), .TXT.<lang>.SGO (CN EN JA KR SC), .IMAGE.RAB: its own list,
    one row per mission of its definition (missions.json), every row's 11th member named "flags", a stock thumbnail
    of a mission on the map;
  - its name and description keys in ETC/TEXTTABLE_STEAM.<lang>.TXT_SGO (the dialog's and the mode screen's text).
Every build also writes the EDF4.1 port's kill counters (pylib/edf41kills.py, docs/kill-counter-re.md): their variables
into ETC/ACHIEVEMENT.SGO (AchievementVariable, after the stock ones) and their battle record labels (StatusInfo_<name>)
into the same text tables; the 4.1 objects count into them (tools/make_edf41_objects.py) and the plugin lists them in
the battle record (src/killcount.cpp). Both files are another mod's or the game's with only these added, owned
through the manifest like CONFIG.SGO.
EDF5's own titles and briefings (edf5campaign/missions.json, tools/make_edf5_campaign_text.py).
The content ids are not DLC the platform knows: the plugin owns them (src/edf5campaign.cpp, ini
EDF5CampaignContent = the first of the three, set here). Without the plugin the packs are listed but greyed out.

A room publishes only its content id (DLC) and the row (MISSION_NO), so both are the same on every machine
(docs/mod-room-isolation.md, 2026-10-18): the ids are fixed (CONTENT, whatever other mods or packs are installed; a
mod already using one refuses the install, said why), and a mission whose resources this machine lacks keeps its row
as a placeholder (locked by the plugin, skipped by the successors), so a row is its mission's place in the pack's
definition everywhere. The packs' identity (PACKS_FILE: each pack's definition digest and placeholder rows) is what
the plugin publishes with a room and checks before joining one.
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
import hashlib
import json
import os
import re
import struct
import sys
from typing import Callable, Collection, Mapping, NamedTuple, TypeVar, Union

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (os.path.join(HERE, '..', 'pylib'),):
    if _p not in sys.path:
        sys.path.insert(0, _p)
import cpk  # noqa: E402
import dsgo  # noqa: E402
import edf41kills  # noqa: E402
import mdb  # noqa: E402
import modfiles  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402

TEXT = (os.path.join(sys._MEIPASS, 'edf5campaign', 'missions.json') if getattr(sys, 'frozen', False)  # type: ignore[attr-defined]
        else os.path.join(HERE, '..', 'edf5campaign', 'missions.json'))   # the installer exe: tools/build_release.py
# EDF5's own thumbnails of these missions (EDF5's three offline IMAGE.RABs merged, names as EDF5 has them: the
# mission's path with '/' as '_', M001.dds, DLC_DM011.dds); tools/make_edf5_campaign_text.py, committed like TEXT.
THUMBS = os.path.join(os.path.dirname(TEXT), 'thumbnails.rab')
# EDF4.1's story (plan P6, tools/make_edf41_missions.py): its 89 missions' scripts (EDF5's recompile, RMPA in EDF6's
# format), titles and pictures, committed and bundled like TEXT. Installed under Mods/MISSION/EDF41/<id>/.
EDF41 = os.path.join(os.path.dirname(os.path.dirname(TEXT)), 'edf41port')
EDF41_TEXT = os.path.join(EDF41, 'missions.json')
EDF41_THUMBS = os.path.join(EDF41, 'missions_thumbnails.rab')
EDF41_FOLDER = 'EDF41'
MANIFEST = '.edf5campaign.json'
DISABLED = '.edf5campaign-disabled'
# The packs' older save layouts (legacy_layouts), kept past an uninstall: the saves stay, and a later install moves them.
LEGACY_FILE = '.edf5campaign-legacy.json'
# Every save layout a release of the packs had (committed, edf5campaign/pack_layouts.json): per pack tag, each
# layout's mission keys in row order, oldest first. The first is 2026-10-18's, the layout every unmarked save is
# measured against (stable_rows); every older one gets a map to the rows now (packs_ini Older / From<layout>), so a
# save marked by any release moves straight to the rows now. A change of a pack's rows appends its new layout here
# (tests/test_edf5_campaign_lifecycle.py checks that the definition's layout is the last one).
LAYOUTS = os.path.join(os.path.dirname(TEXT), 'pack_layouts.json')
MAX_FROM = 4   # src/mission_packs.h kMaxFrom: the older layouts (Legacy included) a pack's ini section may name
INI = os.path.join('Plugins', 'EDF6VehicleCrew.ini')
INI_KEY = 'EDF5CampaignContent'
RANGE_INI_KEY = 'TestRangeContent'
EDF41_INI_KEY = 'EDF41Content'
LANGS = ('CN', 'EN', 'JA', 'KR', 'SC')
CONFIG = 'DEFAULTPACKAGE/CONFIG.SGO'
TEXTS = {lang: f'ETC/TEXTTABLE_STEAM.{lang}.TXT_SGO' for lang in LANGS}   # app:/etc/TextTable_steam.%LOCALE%.txt_sgo
ACHIEVEMENT = 'ETC/ACHIEVEMENT.SGO'   # APP:/ETC/Achievement.sgo (EDF+0xCDD30): the counters, the EDF4.1 port's added
SCRIPTS = 'EDF5_OLD_SCRIPT'
# A row's music (list member 8) is what the HQ / sortie screen and the online room play while it is the selected
# mission (EDF.dll 0x8406E0 reads it through 0xE0A30; called from the mission select's cursor 0x8A5AA7 / 0x8A5C1D,
# the HQ's entry 0x904F1E on the current mission, the room 0x9062C4; MAINSCRIPT HQMain / Room
# PlaySelectedMissionBGM_Start). Empty, it falls back to the mode's ModeList member 3, then to BGM_E4S02_EDFHonbu.
# Each pack plays its own game's HQ music (the user, 2026-10-18: EDF5's packs EDF5's own, as EDF6 rows 13..36 =
# missions 14..37 do; EDF4.1's packs EDF4.1's): the game's HQMain plays GetMainMenuBgm(), the current mode's
# ModeList member 3 (EDF5 MAINSCRIPT HQMain; EDF41.exe 0x3475F0, default BGM_E4S02_EDFHonbu), and their lists have
# no music member:
#   EDF5 story / Mission Pack 1 / 2 config.sgo ModeList: BGM_E5S02_EDFHonbu (all three)
#   EDF4.1 story config.sgo ModeList: BGM_E4S02_EDFHonbu; Mission Pack 1 package.sgo: BGM_Sinkou_intermission;
#   Mission Pack 2 package.sgo: BGM_Sinkou_taisenmenu.
# EDF6's SOUND/PC/TIKYUUX_MUSIC.ACB holds every one of these cues. Pack.bgm; the test range keeps RM015's.
BGM5 = 'BGM_E5S02_EDFHonbu'
BGM41, BGM41_MP01, BGM41_MP02 = 'BGM_E4S02_EDFHonbu', 'BGM_Sinkou_intermission', 'BGM_Sinkou_taisenmenu'
# The values of EDF6's own EDF5-era rows (RM015, M046, ...): the EDF5 soldiers' look (member 6 = 0), EDF5's HQ
# background (member 7 = 0: lyt_bg.sgo's BG_BASE, EDF5's own BG.sgo), flags 8 (no branch bits 0x80 / 0x100, no 0x20).
# Member 7 is the colour scheme of the WHOLE UI, not just the background: EDF.dll 0x83E8A8 calls 0xE0950 for the row's
# member 7 and stores it at the UI base +0x764 (0 = blue-blue, 1 = khaki, 2 = dark green; the late original rows
# M092_5..M094, M131, M148..M152 use 2). The sortie soldier / weapon select (LYT_HUIHQSOLDIERSELECT / WEAPONSELECT)
# and the HUD follow it, as do the soldier preview lights (menu_model_light_setting[2]). EDF4.1 packs write UI_EDF41
# (its original dark green; BG_BASE_EDF41 already overrides the background); EDF5 packs and the range write 0.
UI_BLUE, UI_EDF41 = 0.0, 2.0
FLAGS = 8.0
# The packs' content ids: fixed, CONTENT_BASE + the pack's place in SLOTS (docs/mission-list-re.md §7). EDF.dll knows
# 0..2 (0xE3F80's names, MAINSCRIPT's endings); other mods append theirs after the highest (3, 4, ...), as this tool
# did until 2026-10-18, when an id depended on the mods installed and two machines disagreed on a pack's id. The id is
# an int everywhere it is read (the ModeList member at +0x74, the lobby's DLC attribute: movsxd), never indexed.
CONTENT_BASE = 65
# The installed packs' identity for the plugin (src/mission_packs.h): one [Pack<id>] section per pack, written by every
# build, owned through the manifest like the rest.
PACKS_FILE = 'Plugins/EDF6VehicleCrew.packs.ini'
# ModeList entry members (docs/mission-list-re.md §7): 0 name key, 1 description key, 5 save file, 6 [list, text,
# thumbnails], 8 online, 9 content id, 10 mode type (0 offline scenario, 1 online); 3 the mode's HQ music (the
# fallback of a row without one, BGM5 above).
M_NAME, M_DESC, M_BGM, M_MST, M_FILES, M_ONLINE, M_CONTENT, M_TYPE = 0, 1, 3, 5, 6, 8, 9, 10
# Member 7: one row a difficulty; a row's element 6 the online weapon level cap, element 7 the armour cap
# (docs/weapon-level-cap-re.md: EDF.dll 0xD8850 -> 0xE2710 reads [GS+0x148] member 7 [difficulty][6]: a list of floats
# interpolated by the mission's place in the list, x25 the level; not a list, or negative: -1, no cap; 0xD8130 -> 0xE06B0
# the same for element 7). The stock offline modes have 0 in both (no cap), the online ones lists ([0.1, 1.1] on Easy).
M_LIMITS, LIMIT_WEAPON, LIMIT_ARMOR = 7, 6, 7
# No cap, as EDF.dll's own online modes say it (Mission Pack 2's Inferno: [-1.0, -1.0]).
NO_WEAPON_CAP = [sgo.Float(struct.pack('<f', -1.0)), sgo.Float(struct.pack('<f', -1.0))]
T = TypeVar('T')
# What the EDF4.1 install leaves out after all: object rels ('OBJECT/X.SGO') and map names, or each with its reason.
Skip41 = Union[Collection[str], Mapping[str, str]]


OFFLINE, ONLINE = 'Offline', 'Online'   # a mode's kind: its file names, its text keys, ModeList members 8 and 10


class Pack:
    """One mission pack: its rows' group, its files' tag, its save file, the stock content whose mode it copies
    (difficulty ranges, image: EDF6's story for EDF5's, the matching DLC for EDF5's DLCs), its names, its kinds
    (offline; the test range online too, as the stock packs are: one save file for both), and its HQ music (BGM5 ...:
    every row's and its modes'; None: the stock mode's and the rows' own, the test range)."""

    def __init__(self, group: str, tag: str, mst: str, like: int, name: dict[str, str], desc: dict[str, str],
                 kinds: tuple[str, ...] = (OFFLINE,), bgm: str | None = None, weapon_cap: bool = True) -> None:
        self.group, self.tag, self.mst, self.like, self.name, self.desc = group, tag, mst, like, name, desc
        self.kinds, self.bgm = kinds, bgm
        # False: its online mode has no weapon level cap (mode_entry; the test range's, the user, 2026-10-17:
        # 「测试场不限制武器等级」). The stock online modes cap it by the difficulty and the mission's place in the list.
        self.weapon_cap = weapon_cap
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
         (OFFLINE, ONLINE), BGM5),
    Pack('dlc1', 'EDF5DLC1', 'E5D1.MST', 1, *_names({'CN': '任務包１', 'EN': 'Mission Pack 1', 'JA': 'ミッションパック１',
                                                     'KR': '미션 팩 1', 'SC': '任务包１'}), (OFFLINE, ONLINE), BGM5),
    Pack('dlc2', 'EDF5DLC2', 'E5D2.MST', 2, *_names({'CN': '任務包２', 'EN': 'Mission Pack 2', 'JA': 'ミッションパック２',
                                                     'KR': '미션 팩 2', 'SC': '任务包２'}), (OFFLINE, ONLINE), BGM5),
)
# The test range's pack. Its one row is testrange/gen.py's mission folder (Mods/MISSION/EDF6/EDF6TR_RANGE: the
# script, the site's points), with RM015's row values (progress 0.2, its music, flags 8): an EDF-era row, where the
# Air Raider's vehicle and air support requests arrive (the ruined world's rows 0..12 have flags 65/66 and 2/1 in
# members 6/7, and there they never come). Thumbnail: the plain's (M045), the default site.
RANGE_MISSION = 'EDF6TR_RANGE'
RANGE = Pack('range', 'EDF6TR', 'TR00.MST', 0,
             {'CN': 'EDF6VehicleCrew 測試場', 'EN': 'EDF6VehicleCrew Test Range', 'JA': 'EDF6VehicleCrew テストレンジ',
              'KR': 'EDF6VehicleCrew 테스트 레인지', 'SC': 'EDF6VehicleCrew 测试场'},
             {'CN': '試駕 EDF6VehicleCrew 的載具與武器：只有靶子，沒有敵人；另有幾關聯機對戰預製關；不影響劇情進度。',
              'EN': "Try EDF6VehicleCrew's vehicles and weapons: targets only, no enemies; plus a few PvP presets for online play; the story's progress is untouched.",
              'JA': 'EDF6VehicleCrew のビークルと武器を試せます。標的のみで敵は出ません。オンライン対戦用のプリセットもあります。本編の進行には影響しません。',
              'KR': 'EDF6VehicleCrew의 탈것과 무기를 시험합니다. 표적만 있고 적은 없으며 온라인 대전용 프리셋도 있습니다. 본편 진행에는 영향이 없습니다.',
              'SC': '试驾 EDF6VehicleCrew 的载具与武器：只有靶子，没有敌人；另有几关联机对战预制关；不影响剧情进度。'},
             (OFFLINE, ONLINE), weapon_cap=False)
RANGE_LIKE = 'EDF6/RM015'   # the stock row whose music, progress and flags the range's rows copy (range_rows)
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
# The PvP presets (the user, 2026-10-25, on docs/plan-2026-10-23-pvp-modes.md: 「这个太复杂了，不适合初次测试，直接弄几个
# 预制关卡就行了」): rows 1.. of the range's pack, after the range (row 0 keeps its place: a save's clears are by row), each
# a mission folder of its own (testrange/gen.py PVP_PLANS: empty vehicles by the start, no enemy, no target, never
# ends). The sides are the M map's PvP panel's (the host turns it on, each player picks side 1 or 2: src/pvp.h). No row
# of the pack is ever cleared, so the plugin opens them all in the mission select (src/edf5campaign.cpp OpenRows).
# (folder, thumbnail, EN title, SC title, EN brief, SC brief): CN takes the SC text, JA and KR the EN one.
_PVP_HOW = {'EN': '\nPicking this map turns PvP on. Each player picks a faction and role on the room\'s PvP page (the loadout '
                  'menu); the start waits until every member has confirmed the teams.\n'
                  'This mission never ends: withdraw from the pause menu when done.',
            'SC': '\n房主选这张图即开启对战。每人在房间的「对战」页（配装菜单）选阵营与角色，开打前等全员确认阵营表。\n'
                  '这一关不会结束：打完从暂停菜单撤退。'}
PVP_PRESETS = (
    ('EDF6TR_PVP_JET', 'EDF6/M045', 'Jet Duel', '对战：战斗机对决',
     'The plain: four player fighters parked by the start (1v1 or 2v2). Take off and split up before the fight.',
     '平原，出生点旁停着 4 架玩家战斗机（1v1 或 2v2）。起飞后拉开距离再开打。'),
    ('EDF6TR_PVP_HELI', 'EDF6/M045', 'Helicopter Duel', '对战：直升机对决',
     'The plain: two Eros and two Brute helicopters parked by the start.',
     '平原，出生点旁停着 Eros、Brute 直升机各 2 架。'),
    ('EDF6TR_PVP_TANK', 'EDF6/RM016B', 'Tank Duel', '对战：坦克对决',
     'Train City: four tanks parked by the start. Drive into the streets and find cover.',
     '列车城城区，出生点旁停着 4 辆坦克。开进街区各自找掩护。'),
    ('EDF6TR_PVP_BALAM', 'EDF6/M045', 'Balam Duel', '对战：巴尔加对决',
     'The plain: two Balam giant mechs by the start, for a giants\' brawl.',
     '平原，出生点旁站着 2 台巨型机甲巴尔加，巨人格斗。'),
    ('EDF6TR_PVP_ERGINUS', 'EDF6/M045', 'Balam vs Erginus', '对战：巴尔加对艾尔基努斯',
     'The plain: two Balams by the start, two Erginus out on the plain. Co-op: both of you in Balams against them. '
     'PvP: one player joins the Monsters in the room and takes over an Erginus from the M map (Control this unit).',
     '平原，出生点旁站着 2 台巴尔加，远处 2 只艾尔基努斯。合作：两人都开巴尔加打它们；'
     '对战：一人在房间里选「怪物」阵营，在 M 地图选中艾尔基努斯「操控此单位」，另一只由 AI 帮他。'),
    ('EDF6TR_PVP_INFANTRY', 'EDF6/RM016B', 'Infantry Duel', '对战：步兵对决',
     'Train City, no vehicles: pick your class and weapons before the sortie.',
     '列车城城区，不放载具，兵种和武器在出击前自选。'),
    ('EDF6TR_PVP_MIXED', 'EDF6/M045', 'Combined Arms', '对战：空地混战',
     'The plain: two each of fighters, helicopters, tanks, Balams and Begaruta mechs by the start; pick any.',
     '平原，出生点旁停着战斗机、直升机、坦克、巴尔加、机甲 Begaruta 各 2 台，自由挑选，空地混战。'),
)
PVP_ROWS = [{'group': 'range', 'key': f'EDF6/{folder}', 'map': None, 'thumb': thumb,
             'title': {lang: sc if lang in ('SC', 'CN') else f'PvP: {en}' for lang in LANGS},
             'brief': {lang: brief_sc + _PVP_HOW['SC'] if lang in ('SC', 'CN') else brief_en + _PVP_HOW['EN']
                       for lang in LANGS}}
            for folder, thumb, en, sc, brief_en, brief_sc in PVP_PRESETS]
PVP_MISSIONS = tuple(r['key'].removeprefix('EDF6/') for r in PVP_ROWS)
# EDF4.1's story: its own pack, slotted after the test range so no installed pack's content id moves. Its missions
# need EDF4.1's maps, converted at install from the player's EDF4.1 (tools/edf41_maps.py): a mission whose map or
# objects will not be there is left out, and without EDF4.1 the pack is not built at all (not a refusal).
_UNVERIFIED_41 = {'CN': '（EDF5 重新編譯的 4.1 任務腳本；地圖由已安裝的 EDF4.1 轉換，天氣與尋路借用 EDF6 地圖；尚未逐關實機驗證）',
                  'EN': " (EDF5's recompile of the 4.1 missions; maps converted from your EDF4.1, weather and AI paths "
                        "borrowed from EDF6 maps; not every mission has been verified in game)",
                  'JA': '（EDF5 で再コンパイルされた 4.1 のミッション。マップはインストール済みの EDF4.1 から変換、天候と経路は EDF6 '
                        'のマップを流用。全ミッションの動作は未確認）',
                  'KR': ' (EDF5에서 재컴파일된 4.1 미션, 맵은 설치된 EDF4.1에서 변환, 날씨와 경로는 EDF6 맵을 차용, 모든 미션의 동작은 미확인)',
                  'SC': '（EDF5 重新编译的 4.1 任务脚本；地图由已安装的 EDF4.1 转换，天气与寻路借用 EDF6 地图；尚未逐关实机验证）'}
PACK41 = Pack('edf41', 'EDF41', 'E41M.MST', 0,
              {'CN': 'EDF4.1 本篇', 'EN': 'EDF4.1 Main Story', 'JA': 'EDF4.1 本編', 'KR': 'EDF4.1 본편', 'SC': 'EDF4.1 本篇'},
              {lang: d + _UNVERIFIED_41[lang] for lang, d in {
                  'CN': '遊玩 EDF4.1 本篇。', 'EN': 'Play the EDF4.1 story.', 'JA': 'EDF4.1 の本編をプレイします。',
                  'KR': 'EDF4.1 본편을 플레이합니다.', 'SC': '游玩 EDF4.1 本篇。'}.items()},
              (OFFLINE, ONLINE), BGM41)

# EDF4.1's two DLC mission packs (tools/make_edf41_missions.py --dlc, from the DLC archives of a 4.1 that has them;
# committed like the story): packs of their own, as 4.1 had them, slotted right after the story so the three 4.1
# packs' content ids run EDF41Content .. +2 (src/edf5campaign.cpp owns that run). Their maps are 4.1's story maps
# (converted from the player's EDF4.1, as the story's); the player needs no 4.1 DLC. Like the 4.1 DLC, their
# difficulty ranges are those of EDF6's own DLCs (`like` 1 / 2).
def _names41(part: dict[str, str]) -> tuple[dict[str, str], dict[str, str]]:
    desc = {'CN': f'遊玩 EDF4.1 {part["CN"]}。', 'EN': f'Play EDF4.1 {part["EN"]}.',
            'JA': f'EDF4.1 {part["JA"]}をプレイします。', 'KR': f'EDF4.1 {part["KR"]}을(를) 플레이합니다.',
            'SC': f'游玩 EDF4.1 {part["SC"]}。'}
    return ({lang: f'EDF4.1 {part[lang]}' for lang in LANGS},
            {lang: desc[lang] + _UNVERIFIED_41_DLC[lang] for lang in LANGS})


_UNVERIFIED_41_DLC = {'CN': '（4.1 任務包的腳本轉換為 EDF5 格式；地圖由已安裝的 EDF4.1 轉換；尚未逐關實機驗證）',
                      'EN': " (the 4.1 mission pack's scripts converted to EDF5's form; maps converted from your EDF4.1; "
                            'not every mission has been verified in game)',
                      'JA': '（4.1 ミッションパックのスクリプトを EDF5 形式に変換。マップはインストール済みの EDF4.1 から変換。'
                            '全ミッションの動作は未確認）',
                      'KR': ' (4.1 미션 팩 스크립트를 EDF5 형식으로 변환, 맵은 설치된 EDF4.1에서 변환, 모든 미션의 동작은 미확인)',
                      'SC': '（4.1 任务包的脚本转换为 EDF5 格式；地图由已安装的 EDF4.1 转换；尚未逐关实机验证）'}
PACK41_MP01 = Pack('edf41mp01', 'EDF41MP01', 'E4P1.MST', 1,
                   *_names41({'CN': '任務包１', 'EN': 'Mission Pack 1', 'JA': 'ミッションパック１', 'KR': '미션 팩 1',
                              'SC': '任务包１'}), (OFFLINE, ONLINE), BGM41_MP01)
PACK41_MP02 = Pack('edf41mp02', 'EDF41MP02', 'E4P2.MST', 2,
                   *_names41({'CN': '任務包２', 'EN': 'Mission Pack 2', 'JA': 'ミッションパック２', 'KR': '미션 팩 2',
                              'SC': '任务包２'}), (OFFLINE, ONLINE), BGM41_MP02)


class Pack41(NamedTuple):
    """A 4.1 pack's committed data (missions.json, missions/<id>/, missions_thumbnails.rab) and its Mods folder
    (MISSION/<folder>/<id>/)."""
    pack: Pack
    data: str
    folder: str


PACKS41 = (Pack41(PACK41, EDF41, EDF41_FOLDER), Pack41(PACK41_MP01, os.path.join(EDF41, 'mp01'), 'EDF41MP01'),
           Pack41(PACK41_MP02, os.path.join(EDF41, 'mp02'), 'EDF41MP02'))
SLOTS = (*PACKS, RANGE, *(p.pack for p in PACKS41))   # each pack's content id: CONTENT_BASE + its index here
CONTENT = {p.tag: CONTENT_BASE + k for k, p in enumerate(SLOTS)}   # fixed: the same on every machine
# every build writes these
FILES = (CONFIG, *TEXTS.values(), ACHIEVEMENT, *(f for p in (*PACKS, RANGE) for f in p.files()), PACKS_FILE)
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
    """The content ids the plugin owns (its ini): the first EDF5 pack's (0 = the campaign is not installed), the
    test range's and EDF4.1's story's."""
    campaign: int
    range: int
    edf41: int = 0


def managed(rel: str) -> bool:
    """A file this tool writes: FILES, or one of the EDF4.1 story's (only with EDF4.1: its pack's lists and texts,
    its missions' scripts)."""
    return rel in FILES or any(rel in p.pack.files() or rel.upper().startswith(f'MISSION/{p.folder}/') for p in PACKS41)


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


def new_row(i: int, row: dict, successors: list[int]) -> dsgo.Node:
    key = row['key']
    return dsgo.Node([float(i), f'app:/Mission/{key}', key, dsgo.Node([float(n) for n in successors]), float(i), 0.0,
                      0.0, float(row['ui']), row['bgm'], float(row['progress']), float(row['flags'])], {10: 'flags'})


def available(row: dict) -> bool:
    """A row whose mission this machine has (not a placeholder: plan's 'missing' says why it is not)."""
    return not row.get('missing')


def successors(rows: list[dict]) -> list[list[int]]:
    """Each row's successor: the next row that is not a placeholder (none after the last one). A placeholder is never
    any row's successor, so clearing its predecessor opens the mission after it; the plugin keeps it locked
    (src/edf5campaign.cpp, docs/mission-list-re.md §7)."""
    out: list[list[int]] = []
    following: list[int] = []
    for i in range(len(rows) - 1, -1, -1):
        out.append(following)
        if available(rows[i]):
            following = [i]
    return out[::-1]


def placeholder(row: dict, why: str) -> dict:
    """A mission this machine cannot install, kept at its row (a room's MISSION_NO is the row: every machine's rows
    must be the definition's) with its title marked and the reason as its briefing."""
    title = {lang: _MISSING[lang] + row['title'].get(lang, '') for lang in LANGS}
    brief = {lang: f'{_MISSING_BRIEF[lang]}\n{why}' for lang in LANGS}
    return {**row, 'missing': why, 'title': title, 'brief': brief}


_MISSING = {'CN': '［未安裝］', 'EN': '[Not installed] ', 'JA': '［未インストール］', 'KR': '[미설치] ', 'SC': '［未安装］'}
_MISSING_BRIEF = {'CN': '這一關需要的資源本機沒有，無法遊玩（補齊後重新執行安裝器）。',
                  'EN': 'This mission needs resources this machine lacks and cannot be played (run the installer again '
                        'once they are there).',
                  'JA': 'このミッションに必要なリソースがこの PC にないため、プレイできません（揃えてからインストーラーを再実行）。',
                  'KR': '이 미션에 필요한 리소스가 이 PC에 없어 플레이할 수 없습니다 (갖춘 뒤 설치 프로그램을 다시 실행).',
                  'SC': '这一关需要的资源本机没有，无法游玩（补齐后重新运行安装器）。'}


def plan(root: str, root41: str | None = None, skip41: Skip41 = frozenset()) -> dict:
    """What would be installed, from the game and the text; nothing written. 'rows': every mission of every pack's
    definition, in order (a mission this machine cannot install is a placeholder: 'missing' says why); 'skipped': those
    placeholders (group, path, why), and a 4.1 pack not built at all."""
    game = rootcpk.Game(root)
    text = json.load(open(TEXT, encoding='utf-8'))
    # EDF5 Mission Pack 1's maps and fortress EDF6 lacks, as far as tools/edf5_dlc.py will write them (else why not)
    import edf5_dlc
    maps5, names5, why5 = edf5_dlc.offered(root)
    maps = map_names(root, game) | maps5
    names = root_names(game) | names5
    # What every EDF6 install has, for stable_rows: the game's own packages (Root.cpk, Chunk01/02.cpk: the base
    # install's), nothing tools/edf5_dlc.py adds
    base_maps = map_names(root, game)
    base_names = root_names(game)
    rows, skipped = [], []
    for pack in PACKS:
        for m in text[pack.group]:
            folder = f'MISSION/{SCRIPTS}/{m["path"]}'
            row = {'group': pack.group, 'path': m['path'], 'key': f'{SCRIPTS}/{m["path"]}', 'bgm': pack.bgm,
                   'ui': UI_BLUE, 'flags': FLAGS, 'progress': m['progress'], 'map': None,
                   'title': {lang: m['title'][lang] for lang in LANGS},
                   'brief': {lang: m['brief'][lang] for lang in LANGS}}
            try:
                bvm = game.read(folder, 'MISSION.BVM')
            except KeyError:
                why = 'MISSION.BVM 不在 Root.cpk'
            else:
                gone = missing_assets(names, maps, bvm)
                why = '缺少 ' + ', '.join(sorted({_gone41(r, why5) for r in gone})) if gone else ''
                row['map'] = first_map(bvm)
                # every install has had it: its script and all it loads are in the game's own packages
                row['stable'] = not missing_assets(base_names, base_maps, bvm)
            if why:
                skipped.append((pack.group, m['path'], why))
                row = placeholder(row, why)
            rows.append(row)
    rows41, skipped41 = plan41(root, maps, root41, skip41)
    return {'rows': rows + rows41, 'skipped': skipped + skipped41}


def find_edf41(root: str) -> str | None:
    """The player's EDF4.1, else an EDF4.1 asset pack (pylib/edf41src.py find), for the entry points that stand for
    the player's machine."""
    import edf41src
    return edf41src.find(near=root)


def edf41_text(data: str = EDF41) -> dict:
    """A 4.1 pack's missions.json (`data`: its Pack41.data); a pack whose data is not committed has none."""
    path = os.path.join(data, 'missions.json')
    if not os.path.isfile(path):
        return {'missions': []}
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def edf41_script(mid: str, name: str, data: str = EDF41) -> bytes:
    with open(os.path.join(data, 'missions', mid, name), 'rb') as f:
        return f.read()


def unavailable41(root: str, root41: str | None) -> dict[str, str]:
    """What the install of EDF4.1's objects and maps will leave out, as plan41's `skip41` (key -> why): the object
    rels and map names edf41_objects.unavailable and edf41_maps.unavailable name."""
    import edf41_maps
    import edf41_objects
    if not root41:   # no 4.1 pack at all (plan41): nothing to leave out of it
        return {}
    return {**dict(edf41_objects.unavailable(root)), **dict(edf41_maps.unavailable(root, root41))}


def plan41(root: str, maps: set[str], root41: str | None,
           skip41: Skip41 = frozenset()) -> tuple[list[dict], list[tuple[str, str, str]]]:
    """EDF4.1's packs' rows, every mission of each pack's missions.json in order: a mission whose map or files will
    not all be there is a placeholder (plan's 'missing'), still at its row. Its maps come from the player's
    EDF4.1 (or EDF4.1 asset pack, pylib/edf41src.py) at `root41` (tools/edf41_maps.py MAPS, converted at install), its objects from edf41_objects; without
    EDF4.1 (None), none. `skip41` names what the install will NOT write after all (edf41_objects.unavailable /
    edf41_maps.unavailable, a map whose conversion failed): object rels like 'OBJECT/VEHICLE301_TANK_AI.SGO', map
    names like 'NW_KOUSOUBLD01' (a dict gives the reason of each). A mission naming any of them, in MISSION.BVM or
    BEFORE.BVM, is a placeholder, and says why."""
    import edf41_maps
    import edf41_objects
    if not root41:
        return [], [(PACK41.group, '*', '没有找到 EDF4.1 也没有 EDF4.1 素材包（4.1 的地图要从其中之一转换）')]
    why = {k.upper(): (skip41[k] if isinstance(skip41, Mapping) else '') for k in skip41}
    names = {(d.upper(), n.upper()) for name in ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk')
             if os.path.isfile(os.path.join(root, name)) for d, n in cpk.Cpk(os.path.join(root, name)).index}
    names |= {tuple(rel.upper().rsplit('/', 1)) for rel in edf41_objects.registry()['files']}
    names -= {tuple(k.rsplit('/', 1)) for k in why if '/' in k}
    maps41 = (maps | {f'{name}.MAC' for name in edf41_maps.MAPS}) - {f'{k}.MAC' for k in why if '/' not in k}
    rows, skipped = [], []
    for p41 in PACKS41:   # the story, then the two DLC packs (no data committed: no rows)
        text41 = edf41_text(p41.data)
        missions = text41['missions']
        # a mission its converter refused (a 4.1 script native EDF6's script runtime has no entry for: pylib/bvm41.py)
        skipped += [(p41.pack.group, r['id'], f'脚本没法转换：{r["why"]}') for r in text41.get('refused', [])]
        for i, m in enumerate(missions):
            bvm = edf41_script(m['id'], 'MISSION.BVM', p41.data)
            gone = missing_assets(names, maps41, bvm)
            if 'BEFORE.BVM' in m['files']:   # loads maps and objects too; not every mission has one
                gone += missing_assets(names, maps41, edf41_script(m['id'], 'BEFORE.BVM', p41.data))
            row = {'group': p41.pack.group, 'path': m['id'], 'key': f'{p41.folder}/{m["id"]}', 'bgm': p41.pack.bgm,
                   'ui': UI_EDF41, 'flags': FLAGS, 'progress': round(i / len(missions), 4), 'map': first_map(bvm),
                   'title': m['title'], 'brief': m['brief'], 'files': m['files'], 'sha': m.get('sha', {})}
            if gone:
                reason = '缺少 ' + ', '.join(sorted({_gone41(ref, why) for ref in gone}))
                skipped.append((p41.pack.group, m['id'], reason))
                row = placeholder(row, reason)
            rows.append(row)
    return rows, skipped


def _gone41(ref: str, why: dict[str, str]) -> str:
    """A missing asset's text; one `skip41` named says it was not installed, and why."""
    folder, _, name = ref.replace(chr(92), '/').rpartition('/')
    key = name.upper().removesuffix('.MAC') if folder.upper() == 'MAP' else f'{folder}/{name}'.upper()
    if key not in why:
        return ref
    return f'{ref}（未安装' + (f'：{why[key]}' if why[key] else '') + '）'


def parsed(rel: str, read: Callable[[], T]) -> T:
    """`read()` of a file that may be another mod's: one this tool cannot read refuses the install (said why,
    nothing written) instead of stopping the whole installer."""
    try:
        return read()
    except (ValueError, KeyError, IndexError, TypeError, AssertionError, struct.error, UnicodeDecodeError) as e:
        raise Refused(f'Mods/{rel} 读不了（{type(e).__name__}: {e}），可能是别的 MOD 用了别的格式：不改动它。') from e


def pack_list(game: rootcpk.Game, rows: list[dict], kind: str = OFFLINE) -> bytes:
    """A pack's mission list: the stock list's document (of the same kind) with the pack's rows, each chained to the
    next that is not a placeholder (successors; the last has none: a successor must be < the row count)."""
    doc = dsgo.parse(game.read('MISSION', f'MISSIONLIST.{kind.upper()}.LIST.SGO'))
    table = doc.root.get('table').items
    table[:] = [new_row(i, r, nxt) for i, (r, nxt) in enumerate(zip(rows, successors(rows)))]
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


def own_thumbs() -> dict[str, mdb.RabFile]:
    """Each pack's own mission pictures by '<group>:<NAME>' (EDF5's and EDF4.1's mission ids overlap: M007)."""
    edf5 = edf5_thumbs()
    out = {f'{pack.group}:{n}': x for pack in PACKS for n, x in edf5.items()}
    for p41 in PACKS41:
        path = os.path.join(p41.data, 'missions_thumbnails.rab')
        if os.path.isfile(path):
            with open(path, 'rb') as f:
                out.update({f'{p41.pack.group}:{x.name.upper()}': x for x in mdb.rab_read(f.read()).files})
    return out


def pick_thumbs(rows: list[dict], own: dict[str, mdb.RabFile], stock: dict[str, mdb.RabFile],
                by_map: dict[str, str]) -> list[mdb.RabFile]:
    """A thumbnail per row, named by its key (EDF.dll 0xE0D80): a row with a stock 'thumb' (the range) shows that one;
    a mission its own (`own`: EDF5's THUMBS, EDF4.1's EDF41_THUMBS, 440x220 DXT1 like EDF6's), else a stock mission's
    on its map, else M046's. `own` by '<group>:<NAME>' (own_thumbs), `stock` by upper-case name, `by_map` map file ->
    stock key (stock_maps)."""
    fallback = stock.get(thumb_name('EDF6/M046').upper()) or next(iter(stock.values()))
    out = []
    for r in rows:
        mine = None if r.get('thumb') else own.get(f"{r.get('group')}:{thumb_name(r.get('path', '')).upper()}")
        src = mine or stock.get(thumb_name(r.get('thumb') or by_map.get(r['map'] or '', '')).upper(), fallback)
        out.append(mdb.RabFile(thumb_name(r['key']), src.folder, src.flag, src.stored, src.unk))
    return out


def pack_image(game: rootcpk.Game, rows: list[dict], kind: str = OFFLINE) -> bytes:
    """The kind's thumbnails RAB of `rows` (pick_thumbs), in the stock one's layout."""
    rab = mdb.rab_read(game.read('MISSION', f'MISSIONLIST.{kind.upper()}.IMAGE.RAB'))
    listed = dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
    by_map = stock_maps(game, listed) if any(not r.get('thumb') for r in rows) else {}
    own = own_thumbs() if any(r.get('path') for r in rows) else {}
    rab.files = pick_thumbs(rows, own, {f.name.upper(): f for f in rab.files}, by_map)
    return mdb.rab_write(rab)


def range_rows(game: rootcpk.Game) -> list[dict]:
    """The test range pack's rows: RANGE_ROW, then the PvP presets (PVP_ROWS), with RANGE_LIKE's music, progress and
    flags as the game has them."""
    table = dsgo.parse(game.read('MISSION', 'MISSIONLIST.OFFLINE.LIST.SGO')).root.get('table').items
    like = next((r for r in table if r.items[2] == RANGE_LIKE), None)
    if like is None:
        raise Refused(f'原版离线任务列表里没有 {RANGE_LIKE}：不知道测试场这一关按什么取参数。')
    # stable: every install since the range had a pack (2026-10-09) has had this one row
    return [{**row, 'ui': like.items[7], 'bgm': like.items[8], 'progress': like.items[9], 'flags': like.items[10],
             'stable': True}
            for row in (RANGE_ROW, *PVP_ROWS)]


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
    save file, files, content id and HQ music (when it has its own); members 8 and 10 are 0 offline, 1 online (the
    stock modes')."""
    entry = copy.deepcopy(template)
    entry[M_NAME], entry[M_DESC] = pack.keys(kind)
    if pack.bgm is not None:
        entry[M_BGM] = pack.bgm
    entry[M_MST], entry[M_FILES] = pack.mst, pack.paths(kind)
    online = int(kind == ONLINE)
    entry[M_ONLINE], entry[M_CONTENT], entry[M_TYPE] = online, content, online
    if online and not pack.weapon_cap and isinstance(entry[M_LIMITS], list):
        # The test range: the online weapon level cap gone on every difficulty (M_LIMITS; the room's level limit switch
        # then has nothing to limit); the armour cap and every other difficulty parameter the stock online mode's. A
        # template with no table there (not a list: as the offline modes' 0) has no cap to lift. tools/selftest.py
        # edf5_campaign_range_pack checks every row of the game's own online story is lifted.
        for row in entry[M_LIMITS]:
            if isinstance(row, list) and len(row) > LIMIT_WEAPON:
                row[LIMIT_WEAPON] = copy.deepcopy(NO_WEAPON_CAP)
    return entry


def ours_in(table: list) -> list[str]:
    """Modes in `table` naming a pack's files (none of this tool's: base_bytes reads what it replaced)."""
    stems = tuple(f'app:/mission/missionlist_{p.tag.lower()}.' for p in SLOTS)
    return [str(e[M_NAME]) for e in table if str(e[M_FILES][0]).lower().startswith(stems)]


def pack_config(data: bytes, packs: tuple[Pack, ...] = PACKS) -> tuple[bytes, dict[str, int]]:
    """config.sgo with the packs' modes appended (the modes before them untouched) and each pack's content id (by
    tag): CONTENT, the same whichever packs are given and whatever else the table holds. A room's mode is its content
    id: the test range's must not move to the EDF5 story's when a player opted out of the campaign (the host on M01,
    the guest on the range), nor a pack's to another id on a machine with another mod's modes (2026-10-15: the guest
    then did not own the host's pack). A mode already using one of the ids refuses the install, saying which."""
    ver, members = sgo.read(data)
    table = modes(members)
    ours_there = ours_in(table)
    if ours_there:
        raise Refused(f'CONFIG.SGO 里已经有任务包模式（{", ".join(ours_there)}），但不是本工具留下的那份：不重复添加。')
    msts = {str(e[M_MST]).upper() for e in table}
    if any(p.mst.upper() in msts for p in packs):
        raise Refused('CONFIG.SGO 里已有模式用了任务包的存档文件名：存档会混在一起，不添加。')
    reserved = set(CONTENT.values())
    clash = sorted({(int(e[M_CONTENT]), str(e[M_NAME])) for e in table if int(e[M_CONTENT]) in reserved})
    if clash:
        raise Refused(f'CONFIG.SGO 里别的 MOD 的模式用了内容编号 {", ".join(f"{c}（{n}）" for c, n in clash)}，'
                      f'而 {min(reserved)}..{max(reserved)} 是全军出击任务包的固定编号（联机房间按编号认任务包，每台机器必须一样）：'
                      '不安装任务包。请先卸载那个 MOD，或请它的作者换一个编号。')
    stock = {kind: {int(e[M_CONTENT]): e for e in table if int(e[M_TYPE]) == int(e[M_ONLINE]) == int(kind == ONLINE)}
             for kind in (OFFLINE, ONLINE)}
    for kind in {k for p in packs for k in p.kinds}:
        if 0 not in stock[kind]:
            raise Refused(f'CONFIG.SGO 里没有{"在线" if kind == ONLINE else "离线"}剧情模式：不知道任务包按什么取难度参数。')
    ids = {}
    for pack in packs:
        ids[pack.tag] = CONTENT[pack.tag]
        for kind in pack.kinds:
            table.append(mode_entry(stock[kind].get(pack.like, stock[kind][0]), pack, ids[pack.tag], kind))
    return sgo.write_depth_first(ver, members), ids


def text_table(data: bytes, lang: str, packs: tuple[Pack, ...] = PACKS) -> bytes:
    """The game's text table with the packs' mode names and descriptions and the EDF4.1 kill counters' battle record
    labels (other keys untouched)."""
    ver, members = sgo.read(data)
    for pack in packs:
        for kind in pack.kinds:
            name_key, desc_key = pack.keys(kind)
            members[name_key] = pack.name[lang]
            members[desc_key] = pack.desc[lang]
    edf41kills.add_texts(members, lang)
    return sgo.write_depth_first(ver, members)


def achievement_table(data: bytes) -> bytes:
    """Achievement.sgo with the EDF4.1 kill counters' variables after its own (edf41kills.add_variables: they sort
    after every stock name, so TROPHY.DAT's slots of the stock ones stay; ValueError when they cannot)."""
    ver, members = sgo.read(data)
    edf41kills.add_variables(members)
    return sgo.write_depth_first(ver, members)


def achievement_or_base(data: bytes, plan: dict) -> bytes:
    """achievement_table(data), or `data` as it is when it cannot take the counters (another mod's table that does not
    read as one, or whose names would move the save's slots: edf41kills.add_variables): the packs do not depend on
    it, so only the counters are left out (the 4.1 kills then count nowhere, as before; the plugin shows no row for a
    name the game's table lacks), said in the plan's 'notes'."""
    try:
        return achievement_table(data)
    except (ValueError, KeyError, IndexError, TypeError, struct.error, UnicodeDecodeError) as e:
        plan.setdefault('notes', []).append(f'EDF4.1 击杀计数没有加进 {ACHIEVEMENT}（{type(e).__name__}: {e}）：'
                                            '4.1 敌人的击杀不计数，战斗记录里也没有这几行。')
        return data


def legacy_blocked(root: str, manifest: dict) -> bool:
    """The 2026-10-07 appended list was changed by someone since: it can no longer be put back."""
    entry = manifest.get('files', {}).get(LEGACY_LIST)
    data = modfiles.read(rel_path(root, LEGACY_LIST))
    return entry is not None and data is not None and not ours(entry, data) and not restored(entry, data)


def build(root: str, campaign: bool = True, test_range: bool = True,
          root41: str | None = None, skip41: Skip41 = frozenset()) -> tuple[dict[str, bytes], Contents, dict]:
    """The files to write (rel under Mods -> bytes), the content ids, and the plan: with `campaign` the three EDF5
    packs, with `test_range` the test range's after them (the EDF5 ids first, as before the range had a pack)."""
    if not (campaign or test_range):
        raise ValueError('no pack to build: remove() takes them all out')
    manifest = load_manifest(root)
    if legacy_blocked(root, manifest):
        raise Refused('旧版 EDF5 战役追加过的离线任务列表之后被其他工具改过，不能还原：不改成任务包。')
    game = rootcpk.Game(root)
    p = plan(root, root41, skip41) if campaign else {'rows': [], 'skipped': []}
    by_pack = {pack.group: [r for r in p['rows'] if r['group'] == pack.group] for pack in PACKS}
    empty = [pack.tag for pack in PACKS if campaign and not any(available(r) for r in by_pack[pack.group])]
    if empty:
        raise Refused(f'没有可安装的任务：{", ".join(empty)}（不写入空任务包）。')
    by_pack[RANGE.group] = range_rows(game) if test_range else []
    for p41 in PACKS41:
        by_pack[p41.pack.group] = [r for r in p['rows'] if r['group'] == p41.pack.group]
    with41 = [p41 for p41 in PACKS41 if campaign and any(available(r) for r in by_pack[p41.pack.group])]
    packs = (*(PACKS if campaign else ()), *((RANGE,) if test_range else ()), *(p41.pack for p41 in with41))
    config, ids = parsed(CONFIG, lambda: pack_config(base_bytes(root, game, CONFIG, manifest), packs))
    out = {CONFIG: config}
    for lang, rel in TEXTS.items():
        out[rel] = parsed(rel, lambda rel=rel, lang=lang: text_table(base_bytes(root, game, rel, manifest), lang, packs))
    out[ACHIEVEMENT] = achievement_or_base(base_bytes(root, game, ACHIEVEMENT, manifest), p)
    for pack in packs:
        rows = by_pack[pack.group]
        if len(rows) > 512:
            raise Refused(f'{pack.tag} 有 {len(rows)} 关，超过存档 512 关的容量。')
        out.update(pack_files(game, pack, rows))
    for p41 in with41:
        for r in by_pack[p41.pack.group]:
            for name in r['files'] if available(r) else ():   # a placeholder's mission is not installed
                out[f'MISSION/{p41.folder}/{r["path"]}/{name}'] = edf41_script(r['path'], name, p41.data)
    p['legacy'] = legacy_layouts(root, manifest, packs, by_pack)
    out[PACKS_FILE] = packs_ini(packs, by_pack, p['legacy'])
    return out, Contents(ids.get(PACKS[0].tag, 0), ids.get(RANGE.tag, 0), edf41_content(ids)), p


def installed_keys(root: str, rel: str, manifest: dict) -> list[str] | None:
    """The mission keys, row by row, of the list file `rel` this tool wrote before (None: none of ours there)."""
    data = modfiles.read(rel_path(root, rel))
    if not ours(manifest.get('files', {}).get(rel), data):
        return None
    try:
        return [str(r.items[2]) for r in dsgo.parse(data).root.get('table').items]
    except (ValueError, KeyError, IndexError, TypeError, AssertionError, struct.error, AttributeError):
        return None


def legacy_layouts(root: str, manifest: dict, packs: tuple[Pack, ...], by_pack: dict[str, list[dict]]) -> dict:
    """Each pack's rows as a save written before 2026-10-18 has them, where they differ from now (tag -> mission keys).
    A mode's save (DEFP_<MST>) keys its clears by row (docs/mission-list-re.md §1: clear[class][row], the current row),
    and the older installs left out a mission whose resources were missing, so its rows after that one sat one higher
    up. The list this tool wrote last is that layout; once recorded it is kept (the manifest's 'legacy') for every
    later install, so a save first loaded after several installs is still moved right. The plugin moves an unmarked
    save to the new rows once (src/mission_packs.h MigrateModeSave) and marks it."""
    kept = modfiles.load_json(os.path.join(root, 'Mods', LEGACY_FILE), {})
    out = {tag: list(keys) for tag, keys in {**kept, **manifest.get('legacy', {})}.items()}
    for pack in packs:
        if pack.tag in out:
            continue
        keys = installed_keys(root, pack.list, manifest)
        if keys is not None and keys != [r['key'] for r in by_pack[pack.group]]:
            out[pack.tag] = keys
    return out


def legacy_map(old: list[str], rows: list[dict]) -> list[int]:
    """Each old row's row now (-1: that mission is no longer in the pack)."""
    now = {r['key']: i for i, r in enumerate(rows)}
    return [now.get(k, -1) for k in old]


def layout_id(tag: str, keys: list[str]) -> str:
    """A save layout's marker (src/mission_packs.h Pack::layout): its pack and its rows' mission keys in order, nothing
    else (a script's change keeps it; the definition digest, which hashes the scripts, is the room's business). 8 hex
    digits, never 0."""
    h = hashlib.sha256(f'AF-LAYOUT-1\n{tag}\n{len(keys)}\n'.encode())
    for k in keys:
        h.update(f'{k}\n'.encode())
    v = h.hexdigest()[:8]
    return v if int(v, 16) else '00000001'


def definition_keys(pack: Pack) -> list[str]:
    """`pack`'s rows' mission keys from its committed definition alone (no game: every machine's rows, placeholders
    included, are these)."""
    if pack is RANGE:
        return [r['key'] for r in (RANGE_ROW, *PVP_ROWS)]
    for p41 in PACKS41:
        if p41.pack is pack:
            return [f'{p41.folder}/{m["id"]}' for m in edf41_text(p41.data)['missions']]
    with open(TEXT, encoding='utf-8') as f:
        return [f'{SCRIPTS}/{m["path"]}' for m in json.load(f)[pack.group]]


def released_layouts(tag: str) -> list[list[str]]:
    """LAYOUTS' layouts of the pack `tag`, oldest first (none: a pack no release had)."""
    return [list(keys) for keys in modfiles.load_json(LAYOUTS, {}).get(tag, [])]


def stable_rows(pack: Pack, rows: list[dict]) -> int:
    """How many leading rows every older layout had where they are now (src/mission_packs.h Pack::stableRows): rows
    of the first released layout (the one of the saves from before the markers), each of them and all before it
    installed by every install (row 'stable': all its script loads is in Root.cpk). An unmarked save using only these
    rows needs no moving; one past them is moved only by this machine's record of its older list (Legacy)."""
    first = (released_layouts(pack.tag) or [[r['key'] for r in rows]])[0]
    n = 0
    while n < len(rows) and n < len(first) and rows[n]['key'] == first[n] and rows[n].get('stable'):
        n += 1
    return n


def older_layouts(pack: Pack, rows: list[dict], legacy: bool) -> list[tuple[str, list[str]]]:
    """The released layouts of `pack` other than the rows now, as (marker, keys), the most recent MAX_FROM of them
    (one fewer when the pack has a Legacy map, which takes a slot of its own)."""
    now = layout_id(pack.tag, [r['key'] for r in rows])
    out: list[tuple[str, list[str]]] = []
    for keys in released_layouts(pack.tag):
        marker = layout_id(pack.tag, keys)
        if marker != now and all(marker != m for m, _ in out):
            out.append((marker, keys))
    return out[-(MAX_FROM - (1 if legacy else 0)):] if out else []


def edf41_content(ids: dict[str, int]) -> int:
    """EDF41Content: the 4.1 story's fixed id (the first of the 4.1 packs' run), when any 4.1 pack is in; the
    plugin owns the ones of the run PACKS_FILE lists. 0: none."""
    return CONTENT[PACK41.tag] if any(p41.pack.tag in ids for p41 in PACKS41) else 0


def pack_digest(pack: Pack, rows: list[dict]) -> str:
    """A pack's definition, as a room compares it (src/mission_packs.h): its tag and every row's mission key in order,
    with the committed scripts' hashes where the pack ships its own (EDF4.1's). Placeholders count as their mission
    (a row is the definition's on every machine); which rows this machine lacks is missing_mask's.

    So is what the pack's modes change from the stock mode they copy where each machine reads its own copy: the online
    weapon level cap (mode_entry, weapon_cap=False: the test range). EDF.dll computes the cap from the local
    config.sgo's ModeList (docs/weapon-level-cap-re.md), so a guest whose install predates the change keeps the stock
    cap in the host's room; with it in the definition that guest is refused (pack-definition-differs) and told to
    reinstall. A pack that keeps the stock cap adds nothing: its digest (and so its saves' layout marker,
    src/mission_packs.h MigrateModeSave) is what it was."""
    h = hashlib.sha256(f'AF-PACK-1\n{pack.tag}\n{len(rows)}\n'.encode())
    if not pack.weapon_cap and ONLINE in pack.kinds:
        h.update(b'online-weapon-cap none\n')
    for r in rows:
        h.update(f'{r["key"]}\n'.encode())
        for name, sha in sorted(r.get('sha', {}).items()):
            h.update(f' {name}={sha}\n'.encode())
    return h.hexdigest()[:16]


def missing_mask(rows: list[dict]) -> str:
    """The placeholder rows as hex digits, four rows a digit (row i: bit i % 4 of digit i // 4)."""
    digits = [0] * ((len(rows) + 3) // 4)
    for i, r in enumerate(rows):
        if not available(r):
            digits[i // 4] |= 1 << (i % 4)
    return ''.join(f'{d:x}' for d in digits)


def packs_ini(packs: tuple[Pack, ...], by_pack: dict[str, list[dict]], legacy: Mapping[str, list[str]] | None = None) -> bytes:
    """PACKS_FILE: a [Pack<content id>] section per installed pack, ASCII for GetPrivateProfileString:
      Tag, Digest, Rows, Missing   the pack's identity and placeholder rows (a room's AF_PACK);
      Layout, StableRows           its saves' marker (layout_id) and the leading rows no older layout had elsewhere
                                   (stable_rows);
      Legacy                       when this machine's last list from before the markers differs from the rows now: the
                                   unmarked saves' "<old row count>:<each old row's row now, -1 none>";
      Older, From<marker>          the released layouts other than now (older_layouts), each one's map in that form.
    The plugin owns exactly these ids, keeps the Missing rows locked, marks and moves a save by its own marker and
    content (src/mission_packs.h MigrateModeSave), publishes the current pack's line with a room and refuses a room
    whose pack differs (src/mod_room.cpp)."""
    lines = ['; Written by tools/make_edf5_campaign.py with the mission packs: do not edit (run the installer again).', '']
    for pack in packs:
        rows = by_pack[pack.group]
        keys = [r['key'] for r in rows]
        lines += [f'[Pack{CONTENT[pack.tag]}]', f'Tag={pack.tag}', f'Digest={pack_digest(pack, rows)}',
                  f'Rows={len(rows)}', f'Missing={missing_mask(rows)}', f'Layout={layout_id(pack.tag, keys)}',
                  f'StableRows={stable_rows(pack, rows)}']
        old = (legacy or {}).get(pack.tag)
        if old and old != keys:
            lines.append(f'Legacy={len(old)}:' + ','.join(str(i) for i in legacy_map(old, rows)))
        older = older_layouts(pack, rows, bool(old and old != keys))
        if older:
            lines.append('Older=' + ','.join(m for m, _ in older))
            lines += [f'From{m}={len(k)}:' + ','.join(str(i) for i in legacy_map(k, rows)) for m, k in older]
        lines.append('')
    return '\r\n'.join(lines).encode('ascii')


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
    """Set the plugin's [VehicleCrew] EDF5CampaignContent, TestRangeContent and EDF41Content; the old row cap (EDF5CampaignRows, read
    by no plugin since) goes to 0. Unrelated sections and settings are kept."""
    path = os.path.join(root, 'Mods', INI)
    if not os.path.isfile(path):
        return None
    with open(path, encoding='utf-8-sig') as f:
        text = f.read()
    for name, v in ((INI_KEY, value.campaign), (RANGE_INI_KEY, value.range), (EDF41_INI_KEY, value.edf41),
                    (LEGACY_INI_KEY, 0)):
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


def manifest_record(content: Contents, files: dict[str, dict], legacy: Mapping[str, list[str]] | None = None) -> dict:
    out = {'version': 2, 'content': content.campaign, 'range': content.range, 'edf41': content.edf41, 'files': files}
    if legacy:
        out['legacy'] = {tag: list(keys) for tag, keys in legacy.items()}   # legacy_layouts: kept for later installs
    return out


def install(root: str, built: tuple[dict[str, bytes], Contents, dict] | None = None) -> list[str]:
    """Writes the files (the ones they replace kept in the manifest), puts back the older version's appended list,
    and sets the plugin's content ids. A build without the campaign (disable()) takes its packs out: the mode and
    text tables are rebuilt from what they replaced, and the EDF5 packs' own files, no longer written, are retired."""
    modfiles.refuse_while_running()
    files, content, planned = built or build(root)
    legacy = {**load_manifest(root).get('legacy', {}), **planned.get('legacy', {})}
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
    modfiles.save_json(mpath, manifest_record(content, {**retired, **entries}, legacy))
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
    modfiles.save_json(mpath, manifest_record(content, {**left, **entries}, legacy))
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
    if manifest.get('legacy'):   # the saves outlive the packs: a later install still moves their rows
        modfiles.save_json(os.path.join(root, 'Mods', LEGACY_FILE), manifest['legacy'])
    if left:
        modfiles.save_json(mpath, {**manifest, 'files': left})
    elif os.path.isfile(mpath):
        os.remove(mpath)
    if not any(rel in left for rel in ANCHORS):
        set_content(root, Contents(0, 0))
    return done, kept


def set_packs(root: str, campaign: bool, test_range: bool, root41: str | None = None,
              skip41: Skip41 = frozenset()) -> list[str]:
    """The mode table with exactly these packs: rebuilt from what it replaced, or put back as it was when there are
    none. Raises Refused when it cannot be rebuilt (nothing written) or put back (a table changed since)."""
    if campaign or test_range:
        return install(root, build(root, campaign, test_range, root41, skip41))
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
    current = {CONFIG, PACKS_FILE, *TEXTS.values(), ACHIEVEMENT, *(f for p in PACKS if enabled(root) for f in p.files()),
               *(RANGE.files() if range_installed(root) else ()),
               *(f for f in files if manifest.get('edf41') and managed(f) and f not in FILES)}

    def foreign(rel: str, entry: dict) -> bool:   # a retired file someone changed since: kept, its record with it
        data = modfiles.read(rel_path(root, rel))
        return rel not in current and data is not None and not ours(entry, data) and not restored(entry, data)

    kept = [rel for rel, e in files.items() if 'also' not in e and managed(rel) and foreign(rel, e)]
    bad = [rel for rel, e in files.items() if rel not in kept and (
        'also' in e or not managed(rel) or rel not in current or modfiles.sha256_file(rel_path(root, rel)) != e['sha'])]
    for rel in kept:
        print('  保留（别的工具改过，卸载时不动）', rel)
    wanted_ids = ((INI_KEY, manifest.get('content', 0)), (RANGE_INI_KEY, manifest.get('range', 0)),
                  (EDF41_INI_KEY, manifest.get('edf41', 0)))
    values = {name: ini_value(root, name) for name, _ in wanted_ids}
    ids_ok = all(values[name] == want for name, want in wanted_ids)
    packs = ((['EDF5 战役 3 个'] if enabled(root) else []) + (['测试场'] if range_installed(root) else [])
             + ([p41.pack.name['SC'] for p41 in PACKS41
                 if any(managed(f) and f.upper().startswith(f'MISSION/{p41.folder}/') for f in files)]
                if manifest.get('edf41') else []))
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
        root41 = find_edf41(root)
        p = plan(root, root41, unavailable41(root, root41))
        for r in p['rows']:
            print(r['group'], r['path'], r['map'], r['title']['SC'])
        for g, path, why in p['skipped']:
            print('占位', g, path, why)
        held = sum(not available(r) for r in p['rows'])
        print(len(p['rows']) - held, '关' + (f'（另 {held} 关缺资源，留作占位行）' if held else ''))
        return 0
    try:
        root41 = find_edf41(root)
        built = build(root, root41=root41, skip41=unavailable41(root, root41))
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
    out += ''.join(f'\n  注意：{note}' for note in built[2].get('notes', ()))
    if not content.campaign:
        return out
    rows = built[2]['rows']

    def count(group: str) -> str:
        n = sum(r['group'] == group and available(r) for r in rows)
        held = sum(r['group'] == group and not available(r) for r in rows)
        return f'{n} 关' + (f'（另有 {held} 关缺资源，占位锁定）' if held else '')
    counts = '、'.join(f'{pack.name["SC"]} {count(pack.group)}' for pack in PACKS)
    out41 = ''
    if content.edf41:
        for p41 in PACKS41:
            if any(r['group'] == p41.pack.group and available(r) for r in rows):
                out41 += f'；{p41.pack.name["SC"]} {count(p41.pack.group)}（内容编号 {CONTENT[p41.pack.tag]}）'
    return (f'EDF5 任务包：{counts}（离线模式的「任务包」列表里选择；内容编号 {content.campaign}–'
            f'{content.campaign + len(PACKS) - 1}）{out41}；' + out)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
