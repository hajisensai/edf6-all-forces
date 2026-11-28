"""The Rune Blade's (ZwRuneBlade, type 5) look and motions (docs/newclass-re/runeblade-look-plan.md).

  MODEL      the battle / menu model's source: None while the Ranger's stands in (make_zw_classes.ASSETS 'placeholder';
             no fitting model found, the plan's §1 lists what was searched and the pipeline still missing)
  MOTIONS    each of the class's actions (docs/plan-2026-11-28-zero-worldline-gear.md §2: the combo, the lance's dash, the
             returning blades, the hammer, the draw, the circle-step) -> the stock CAS states it plays, the CAS and player
             (anmgroup) they are in (docs/newclass-re/motion.md §4: the melee weapons have no hit code of their own, the
             CAS's data events 「武器 / 発射 / キャンセル」 drive them, so the motion is the stock state that carries them)
  FX         the shared effect objects each action shows (tools/make_runeblade_fx.py VARIANTS)

    missing_motions(read) -> [(action, cas, player, state)]   every named state the game's CAS lacks (read: (folder, name)
                                                              -> bytes, the game's Root.cpk); [] when all are there
    python tools/make_runeblade_look.py                      prints the table against the local game
"""
from __future__ import annotations

import os
import sys
from dataclasses import dataclass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, HERE)
import make_runeblade_fx as fx  # noqa: E402

MODEL = None   # no model of its own yet: runeblade-look-plan.md §1

RANGER_CAS = 'EDF6ArmySoldier.cas'    # the class's own (the factory is AssultSoldier-based: src/soldier_factory.h)
FENCER_CAS = 'EDF6HeavyArmor.cas'     # the melee and booster states (Weapon_Swing / ImpactHammer / PileBanker's)


@dataclass(frozen=True)
class Motion:
    action: str                 # plan §2's name
    cas: str
    player: str                 # the CAS anmgroup
    states: tuple[str, ...]     # in the order the action plays them
    fx: tuple[str, ...]         # make_runeblade_fx.VARIANTS it shows
    note: str


# The right hand's states (Weapon_Swing appends the slot's suffix: soldier_weapon_slot 'weapon_r' -> '_r', motion.md §1.4).
MOTIONS: tuple[Motion, ...] = (
    Motion('M1 破晓 三段连斩', FENCER_CAS, 'main', ('sword_thrush_r', 'sword_thrush2_r', 'sword_thrush3_r'), ('TRAIL', 'HIT'),
           '横斩→回斩→上挑；Weapon_Swing 多段条目，「キャンセル」开连段窗口'),
    Motion('M1 破晓 空中急坠下劈', FENCER_CAS, 'main', ('hammer_jumpKnock_r',), ('TRAIL', 'HIT', 'BURST'),
           '空中按下；ImpactHammer 空中档的下砸状态'),
    Motion('M2 天穹 蓄力突刺', FENCER_CAS, 'main', ('pile_setS1_r', 'pile_dashH1_r', 'pile_dashH2_r'), ('TRAIL', 'BURST'),
           '蓄力→沿视线冲刺（PileBanker 冲刺条目 pile_dashH1）；空中同一状态'),
    Motion('M3 群星 双刃乱舞', FENCER_CAS, 'main', ('hammer_turnSwing_r', 'hammer_turnImpact_r'), ('TRAIL', 'HIT'),
           '近身旋斩；掷出光环的回旋弹种另做（plan §3 缺口）'),
    Motion('M4 坠星 地面横扫', FENCER_CAS, 'main', ('hammer_swing_r', 'hammer_KnockRoller_r'), ('TRAIL', 'HIT'),
           '横扫击退'),
    Motion('M4 坠星 空中下砸', FENCER_CAS, 'main', ('hammer_jumpKnock_r',), ('BURST',), '耗尽以太槽，落地大光阵'),
    Motion('M5 残光 居合', FENCER_CAS, 'weapon_r', ('hammer_charge',), ('BACK',),
           '按住架势：ImpactHammer 蓄力分级（帧阈值→动画→爆炸弹，Dynamo Blade 的做法）'),
    Motion('M5 残光 拔刀', FENCER_CAS, 'main', ('hammer_swingK_r',), ('TRAIL', 'HIT'), '满蓄横斩'),
    Motion('踏阵 起跳 / 连踏', FENCER_CAS, 'main', ('base_high_jump_start', 'base_high_jump_up', 'base_high_jump_up_f'),
           ('FOOT',), '剑兵推进跳链（属性 302 次数，0x566BE0）；每次踏阵脚下出 FOOT'),
    Motion('踏阵 滑翔下落 / 落地', FENCER_CAS, 'main', ('base_jump_fall', 'base_jump_land'), ('BACK',), '飞行中背后 BACK'),
    Motion('R1/R2 持枪射击', RANGER_CAS, 'weapon', ('aim', 'assault_recoil1', 'assault_reload1'), ('HIT',),
           '远程槽沿用游骑兵持枪；栓动拉栓另做'),
)


def _states(read, cas: str) -> dict[str, set[str]]:  # noqa: ANN001 - (folder, name) -> bytes
    import cas_graft
    return cas_graft.states(read('OBJECT', cas.upper()))


def missing_motions(read) -> list[tuple[str, str, str, str]]:  # noqa: ANN001 - (folder, name) -> bytes
    have = {cas: _states(read, cas) for cas in {m.cas for m in MOTIONS}}
    return [(m.action, m.cas, m.player, s) for m in MOTIONS for s in m.states if s not in have[m.cas].get(m.player, set())]


def bad_fx() -> list[tuple[str, str]]:
    return [(m.action, f) for m in MOTIONS for f in m.fx if f not in fx.VARIANTS]


def main(argv: list[str]) -> int:
    import vcobjects as vc
    game = vc.Game(argv[0] if argv else vc.DEFAULT_GAME)
    gaps = missing_motions(game.read)
    for m in MOTIONS:
        print(m.action, '|', m.cas, m.player, '|', ', '.join(m.states), '|', '+'.join(m.fx))
    for gap in gaps + bad_fx():
        print('缺', gap)
    return 1 if gaps or bad_fx() else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
