"""Proteus menu metadata for the All Forces installer, never standalone AutoTurret.

The runtime rework and its two-seat option can be disabled after installation.
Keep the stock equipment data as explicitly labelled reference rather than
claiming that the generated text can track live INI changes.
"""
from __future__ import annotations

import copy

import dsgo


IDS = ('EWEAPON387', 'EWEAPON388', 'EWEAPON390', 'EWEAPON392')
# These tags delimit only our preface; an update preserves the underlying text,
# including another mod's changes to the same Proteus row.
TAG = '<font color=%dq%#ffc040%dq%>[EDF6VC Proteus]</font>\n'
END = '\n<font color=%dq%#ffc040%dq%>[/EDF6VC Proteus]</font>\n'
NOTES = {
    'EN': ('All Forces default: 2 seats (driver and gunner). The gunner fires both stock cannons; alone, the driver '
           'fires them, and deployed the stock missile launcher. Shield: the Air Raider\'s electromagnetic barrier. '
           'Disabling ProteusRework or ProteusTwoSeats restores 4 seats.\n'
           'Original 4-seat configuration and weapon data follow for reference:'),
    'JA': ('全軍出撃の標準設定：2席（操縦席と砲手席）。砲手は原版の2門のキャノン砲を撃つ。1人なら操縦者が砲を撃ち、'
           '展開時は原版ミサイルランチャーも撃てる。シールドはエアレイダーの電磁トーチカの防壁。'
           'ProteusRework または ProteusTwoSeats を無効にすると4席。\n以下は原版の4席構成と武装データの参考情報：'),
    'CN': ('全軍出擊預設：2席（駕駛席、砲手席）。砲手使用原版兩門加農砲；只有一人時駕駛員開砲，展開後還能發射原版飛彈架。'
           '護盾為空襲兵電磁碉堡的能量牆。關閉 ProteusRework 或 ProteusTwoSeats 時恢復4席。\n以下僅供參考：原版4席配置與武器資料。'),
    'SC': ('全军出击默认：2席（驾驶席、炮手席）。炮手使用原版两门加农炮；只有一人时驾驶员开炮，架设后还能发射原版导弹架。'
           '护盾是空袭兵电磁碉堡的能量墙。关闭 ProteusRework 或 ProteusTwoSeats 时恢复4席。\n以下仅供参考：原版4席配置与武器数据。'),
    'KR': ('전군 출격 기본 설정: 2석(조종석, 사수석). 사수는 원본 캐논포 2문을 쏩니다. 혼자 탈 때는 조종사가 포를 쏘고, '
           '전개 시 원본 미사일 런처도 쏠 수 있습니다. 실드는 에어레이더 전자 토치카의 방벽입니다. '
           'ProteusRework 또는 ProteusTwoSeats를 끄면 4석으로 복원됩니다.\n아래는 원본의 4석 구성과 무장 데이터입니다(참고용).'),
}


def rewrite(row: dsgo.Node, lang: str) -> dsgo.Node:
    """Change only the description, preserving names, stats and foreign edits."""
    if not isinstance(row, dsgo.Node) or len(row.items) != 3 or not isinstance(row.items[1], str):
        raise ValueError('unexpected Proteus weapon text row')
    result = copy.deepcopy(row)
    desc = row.items[1]
    if desc.startswith(TAG):
        end = desc.find(END, len(TAG))
        if end < 0:
            raise ValueError('incomplete Proteus description marker')
        desc = desc[end + len(END):]
    result.items[1] = TAG + NOTES[lang] + END + desc
    return result


def apply(rows: list[dsgo.Node], ids: list[str], lang: str) -> dict[str, tuple]:
    """Rewrite by weapon ID, returning the existing backup/restore row contract."""
    upper = [x.upper() for x in ids]
    if len(rows) != len(ids):
        raise ValueError('Proteus text rows are not aligned with the weapon table')
    changed = {}
    for row_id in IDS:
        if upper.count(row_id) != 1:
            raise ValueError(f'expected exactly one {row_id} weapon row')
        at = upper.index(row_id)
        before = rows[at]
        rows[at] = rewrite(before, lang)
        changed[row_id] = (before, rows[at])
    return changed
