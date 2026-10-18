"""The EDF4.1 port's own kill counters: each enemy the port brings over counts into a category of its own in EDF6's
battle record (戦績 / 战斗记录), not into an EDF6 one (docs/kill-counter-re.md). The user, 2026-10-18: 「4.1导入单位的
sgo里的gameobjectkillcount写入单独的类别，同时把新添加的怪物击杀计数写入战斗记录里」.

How EDF6 counts and shows a kill (docs/kill-counter-re.md):
  - the counters are the variables of APP:/ETC/Achievement.sgo's AchievementVariable list ([name, 'int', 0]), held in a
    std::map keyed by the name (EDF+0xCDD30); a kill adds 1 to the variable the dead object's SGO names in
    game_object_kill_counter (EDF+0x5499F0 -> 0xCF640), and a name the map lacks counts nowhere;
  - the save (TROPHY.DAT, EDF+0xCFC50 / 0xD00B0) keeps the values by position in the map's order (ordinal UTF-16
    order of the names), 200 slots: so every name here sorts after every stock one (PREFIX is lower case: after any
    upper-case name), the stock ones keep their slots, and a save read without these names loses nothing of EDF6's;
  - the battle record lists a fixed table in its constructor (EDF+0x893C40, 90 rows); the plugin adds these rows to
    it (src/killcount.cpp, the same names: src/edf41_kill_counters.h), each shown when it is not 0, its text the text
    table's 'StatusInfo_' + name (text_keys).

COUNTERS is the order the rows show in; `source` the 4.1 counter names (EDF41.exe's table, 4.1's SGOs) that count into
it. The texts: 4.1's own (JA: 4.1's TEXTTABLE_STEAM, EN: TEXTTABLE_STEAM_EN; SC: the community patch's names,
pylib/edf41zh.py), worded the way EDF6's own rows are, with TAG so a 4.1 row is not taken for EDF6's of a like name.
"""
from __future__ import annotations

from typing import NamedTuple

PREFIX = 'edf41_'
TEXT_PREFIX = 'StatusInfo_'   # EDF+0x894E42: the battle record's label is the text table's 'StatusInfo_' + name
LANGS = ('SC', 'CN', 'EN', 'JA', 'KR')
SLOTS = 200        # TROPHY.DAT's variable slots (EDF+0xCFC50 writes 200 dwords after 200 event bytes, no bound check)
TABLE_ROWS = 127   # the battle record's row count is an imm8 compare (EDF+0x8949A2, 0x895161): signed, at most 127
TAG = {'SC': '（4.1）', 'CN': '（4.1）', 'EN': ' (4.1)', 'JA': '（4.1）', 'KR': ' (4.1)'}


class Counter(NamedTuple):
    key: str                      # the variable's name (game_object_kill_counter of the 4.1 SGOs)
    source: tuple[str, ...]       # the 4.1 counter names that count into it
    text: dict[str, str]          # lang -> the row's label, before TAG


def _c(name: str, sc: str, cn: str, en: str, ja: str, kr: str, *also: str) -> Counter:
    return Counter(PREFIX + name, (name, *also), {'SC': sc, 'CN': cn, 'EN': en, 'JA': ja, 'KR': kr})


COUNTERS: tuple[Counter, ...] = (
    _c('HectorKillCount', '赫克托击破数', '赫克托擊破數', 'Hectors Defeated', 'ヘクトル撃破数', '헥터 격파 수'),
    _c('4LegTankKillCount', '四足要塞击破数', '四足要塞擊破數', 'Quadrupeds Defeated', '四足歩行要塞撃破数',
       '4족 보행 요새 격파 수'),
    _c('DeiroiKillCount', '迪洛伊击破数', '迪洛伊擊破數', 'Deroys Defeated', 'ディロイ撃破数', '디로이 격파 수'),
    _c('AlienTrailerKillCount', '防护者击破数', '防護者擊破數', 'Shield Bearers Defeated', 'シールドベアラー撃破数',
       '실드 베어러 격파 수'),
    _c('UfoRoboKillCount', '阿尔戈击破数', '阿爾戈擊破數', 'Argos Defeated', 'アルゴ撃破数', '아르고 격파 수'),
    # 4.1's 401 mothership: its core (UfoMother401_CorePanel) is what dies; 4.1 names the tally アースイーター in
    # Japanese and "Brains" in English (the same counter, MotherShip401KillCount): both names here
    _c('MotherShip401KillCount', '主脑（地球吞噬者）击破数', '主腦（地球吞噬者）擊破數', 'Brains (Earth Eaters) Defeated',
       'アースイーター（ブレイン）撃破数', '브레인(어스 이터) 격파 수'),
    _c('MotherShip301KillCount', '母舰击破数', '母艦擊破數', 'Motherships Defeated', 'マザーシップ撃破数', '마더십 격파 수'),
    _c('Carrier2KillCount', '大型运输船击破数', '大型運輸船擊破數', 'Large Transport Ships Destroyed', '大型輸送船撃破数',
       '대형 수송선 격파 수'),
    _c('Carrier1KillCount', '运输船击破数', '運輸船擊破數', 'Transport Ships Destroyed', '輸送船撃破数', '수송선 격파 수'),
    _c('UfoSmall401KillCount', '飞行载具击破数', '飛行載具擊破數', 'Flying Vehicles Defeated', '飛行ビークル撃破数',
       '비행 비클 격파 수'),
    _c('UfoSmall301KillCount', '飞行无人机击破数', '飛行無人機擊破數', 'Flying Drones Defeated', '飛行ドローン撃破数',
       '비행 드론 격파 수'),
    _c('UfoSmall301AceKillCount', '红色无人机击破数', '紅色無人機擊破數', 'Red Drones Defeated', 'レッドカラー撃破数',
       '레드 컬러 격파 수'),
    _c('BigDragonKillCount', '神龙击破数', '神龍擊破數', 'Greater Wild Dragons Defeated', 'グレーターワイルドドラゴン撃破数',
       '그레이터 와일드 드래곤 격파 수'),
    _c('DragonKillCount', '双足飞龙击破数', '雙足飛龍擊破數', 'Dragons Defeated', 'ドラゴン撃破数', '드래곤 격파 수'),
    # 4.1's nests: the story's ANTHILL301 (NestKillCount) and the DLC's (4.1's AntHill class; the port had put them
    # into EDF6's AntHillKillCount, EDF6's teleportation devices)
    _c('NestKillCount', '地下隧道出口摧毁数', '地下隧道出口摧毀數', 'Underground Tunnel Exits Destroyed',
       '地下トンネルの出口撃破数', '지하 터널 출구 파괴 수', 'AntHillKillCount'),
    _c('GiantAntKillCount', '蚂蚁型巨型昆虫击破数', '螞蟻型巨型昆蟲擊破數', 'Ant-Type Bugs Defeated', '巨大生物撃破数',
       '개미형 거대 생물 격파 수'),
    _c('GiantBeeQueenKillCount', '死亡女王击破数', '死亡女王擊破數', 'Death Queens Defeated', 'デスクイーン撃破数',
       '데스 퀸 격파 수'),
    _c('NephilaKillCount', '雷塔留斯击破数', '雷塔留斯擊破數', 'Retiarii Defeated', 'レタリウス撃破数', '레타리우스 격파 수'),
)
KEYS = tuple(c.key for c in COUNTERS)
_BY_SOURCE = {s: c.key for c in COUNTERS for s in c.source}


def ported(name: str) -> str:
    """The port's counter for a 4.1 object's game_object_kill_counter `name` (already one of KEYS: itself). KeyError
    for a name with no category here: every 4.1 enemy counts into one of its own (tools/make_edf41_objects.py)."""
    if name in KEYS:
        return name
    return _BY_SOURCE[name]


def text_keys() -> list[str]:
    return [TEXT_PREFIX + k for k in KEYS]


def label(counter: Counter, lang: str) -> str:
    """The battle record's label in `lang`: EDF6's English rows end in ': ' before the value, the others in nothing."""
    text = counter.text[lang]
    tagged = text + TAG[lang]
    return tagged + ':' if lang == 'EN' else tagged


def add_texts(members: dict, lang: str) -> None:
    """The counters' labels into a text table's members (other keys untouched)."""
    for c in COUNTERS:
        members[TEXT_PREFIX + c.key] = label(c, lang)


def add_variables(members: dict) -> None:
    """The counters as AchievementVariable rows ([name, 'int', 0]) of an Achievement.sgo's members, after the rows it
    has (one of ours already there is left as it is). ValueError when a name it holds would not sort before ours (the
    save keeps the values by the names' order: a name after ours would move them) or the slots run out."""
    rows = members['AchievementVariable']
    have = {str(r[0]) for r in rows}
    others = [n for n in have if n not in KEYS]
    late = [n for n in others if n >= min(KEYS)]
    if late:
        raise ValueError(f'Achievement.sgo holds variables that sort after the 4.1 counters: {sorted(late)}')
    rows.extend([k, 'int', 0] for k in KEYS if k not in have)
    if len(rows) > SLOTS:
        raise ValueError(f'Achievement.sgo would hold {len(rows)} variables; TROPHY.DAT keeps {SLOTS}')
