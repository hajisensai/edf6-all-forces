"""潜舰对战母舰: the offline mission M082 (list item 104, 潜艇母舰‧保卫作战, the coast where the stock
submarine carrier surfaces next to the Mothership) remade as a fight on the deck of the plugin's
submarine carrier. Same map and weather as M082, and its MISSION.RMPA with five points moved (moved()):

  - the submarine carrier (tools/make_sub.py EDF6VC_SUB_CARRIER.SGO, 1664 m, flown by EDF6VehicleCrew's
    src/subcarrier.cpp) at the stock carrier's point 潜水母艦近５, 2 km out at sea, its body origin at the
    point's y (-130, where M082 puts the stock one): the plugin holds a mission's submarine at the height
    the mission put it, so its deck (the top of its collision box, kDeck over the origin, one flat face
    242 m wide and 1664 m long: the tower and the turrets have no collision) stays at y = -130 + kDeck;
  - the player (プレイヤー) kDrop over that deck, kPlayerAft back from the hull's middle;
  - three dropships (輸送船01..03) kCarrierUp over the deck along the hull, which drop ants onto it;
  - the deep-sea monsters (merman) come from M082's own areas 深海怪獣1/2, out at sea; giant bees from
    潜水母艦遠's direction (潜水母艦, kBeeUp up).

The fight, wave by wave (each waits until the enemies are down to a few):
  1. ants dropped onto the deck;
  2. mermen from the sea and bees from the air;
  3. the Mothership as M082 makes it (e511_mothership_light, invincible body): its Genocide cannon is the
     target, with gold UFOs; once the cannon is destroyed it blows itself up;
  4. the full Mothership of M116 (e511_mothership_edf6), fought as M116 fights it: its 5 barrier generators,
     then its Genocide cannon, then the Mothership itself (it holds 40%, then 0%, as M116). Destroying it
     clears the mission.

The submarine carrier has HP (its SGO's, times kSubDurability %): the plugin shows it as the game's
follower gauge (subcarrier.cpp), and once it is destroyed the mission is failed (AsCommon.h's
MissionGameOverEvent, the stock failure screen with its retry).

Writes Mods/MISSION/EDF6/M082 (MISSION.AC, MISSION.RMPA and the marker) and nothing else; refuses a folder
another mod put there; --remove deletes it only when the marker is there. The submarine's files come from
tools/make_sub.py (run it first, with the game closed).

  python testrange/sub_vs_mothership.py [game dir]
  python testrange/sub_vs_mothership.py [game dir] --remove
"""
from __future__ import annotations

import math
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'lib'))
sys.path.insert(0, HERE)
import gen  # noqa: E402
import rmpa  # noqa: E402

MISSION = 'M082'
ITEM = 104
MARKER = 'EDF6SubVsMothership.txt'
MAP = 'app:/map/ig_SteepCoast.mac'
WEATHER = 'afternoon'
SUB = 'app:/object/edf6vc_sub_carrier.sgo'
SUB_FILES = ('EDF6VC_SUB_CARRIER.SGO', 'EDF6VC_SUB.MRAB')
MOTHER = 'app:/object/e511_mothership_light.sgo'
MOTHER2 = 'app:/object/e511_mothership_edf6.sgo'
UFO = 'app:/object/e507_goldufo.sgo'
CARRIER = 'app:/object/e508_carrier.sgo'
ANT = 'app:/object/e650_giantant01.sgo'
BEE = 'app:/object/e668_giantbee_medium.sgo'
MERMAN = 'app:/object/e603_merman.sgo'

SUB_POINT = '潜水母艦近５'
CARRIERS = ('輸送船01', '輸送船02', '輸送船03')
BEE_POINT = '潜水母艦'
kDeck = 193.08          # the top of the submarine's collision box over its origin (gen.JETS' sub rigid)
kDrop = 12.0            # the player starts this far over the deck
kPlayerAft = 300.0      # ...and this far back from the hull's middle
kCarrierUp = 160.0      # the dropships hover this far over the deck...
kCarrierAlong = (-550.0, 50.0, 550.0)   # ...this far along the hull from its middle
kBeeUp = 250.0          # the bees' point, this far over the sea
kSubDurability = 300.0  # the submarine's HP, % of its SGO's


def _q(text: str) -> str:
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def points(stock: bytes) -> bytes:
    """M082's points with the player, the dropships and the bees' point moved onto and over the submarine."""
    at = {p.name: p for p in rmpa.points(stock)}
    sub = at[SUB_POINT]
    fx, fz = sub.face[0] - sub.pos[0], sub.face[2] - sub.pos[2]
    n = math.hypot(fx, fz)
    fx, fz = fx / n, fz / n
    deck = sub.pos[1] + kDeck

    def along(d: float, y: float) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
        pos = (sub.pos[0] + fx * d, y, sub.pos[2] + fz * d)
        return pos, (pos[0] + fx * 100.0, y, pos[2] + fz * 100.0)

    to = {'プレイヤー': along(-kPlayerAft, deck + kDrop)}
    for name, d in zip(CARRIERS, kCarrierAlong):
        to[name] = along(d, deck + kCarrierUp)
    bee = at[BEE_POINT]
    to[BEE_POINT] = ((bee.pos[0], kBeeUp, bee.pos[2]), (sub.pos[0], kBeeUp, sub.pos[2]))
    return rmpa.moved(stock, to)


def script() -> str:
    """The mission: the stock skeleton (event 0 = Main, as gen.script), run straight through."""
    preload = [MOTHER, MOTHER2, UFO, CARRIER, ANT, BEE, MERMAN, SUB]
    carriers = [f'c{i}' for i in range(len(CARRIERS))]
    return '\n'.join([
        '//',
        '// 潜舰对战母舰, generated by testrange/sub_vs_mothership.py (EDF6VehicleCrew). Delete this folder to',
        '// restore M082.',
        '//',
        '',
        '#include "app:/Mission/AsCommon.h"',
        '',
        'Talkers g_soldiers;',
        'Object sub;',
        'Object mother;',
        'Object cannon;',
        *[f'Object {c};' for c in carriers],
        'Object b0;', 'Object b1;', 'Object b2;', 'Object b3;', 'Object b4;',
        '',
        'class __EventData {',
        '\tuint m_counter  = 0;',
        '\tbool m_is_break = false;',
        '\tbool m_is_pass = false;',
        '\tinternal_SyncCounter m_game_thread_sync;',
        '\tinternal_SyncCounter m_voice_thread_sync;',
        '\tMutex m_mutex;',
        '};',
        '',
        '__EventData __0000_data;',
        '',
        'const uint __END_SYNC = 0x7fffffff;',
        '',
        'void Main()',
        '{',
        '\tif( IsBoolState(0,::__0000_data.m_is_break,::__0000_data.m_is_break) ) return;',
        '\t::__0000_data.m_counter++;',
        '\tconst uint32 MAX_EVENT_ID = 1;',
        '\tinternal_Initialize(MAX_EVENT_ID);',
        '\tinternal_GetEventState(0).SetName("開始");',
        '\tMutex_LockObject@ __internal_thread_lock = ::__0000_data.m_mutex.Lock();',
        '\tinternal_InitEventThread(0, ::__0000_data.m_counter, "Main", "開始");',
        '',
        '\tBeginLoading();',
        f'\tPreloadMap({_q(MAP)}, {_q(WEATHER)}, -1);',
        '\tPreload("app:/ui/UiResourceGroup_InMission.sgo", -1);',
        '\tPreload("app:/ui/UiResourceGroup_MissionCleared.sgo", -1);',
        '\tPreload("app:/ui/UiResourceGroup_MissionFailed.sgo", -1);',
        *[f'\tPreload({_q(p)}, -1);' for p in preload],
        '\tPreloadPlayerResource();',
        '\tWaitPreload();',
        '\tOnline_WaitStart();',
        '\tEndLoading();',
        '',
        '\tWaitVoice();',
        '\tg_soldiers = GetTalkers();',
        '\t::__0000_data.m_game_thread_sync.Reset();',
        '\t::__0000_data.m_voice_thread_sync.Reset();',
        '\t::internal_CreateThread("__0000_voice_event");',
        '',
        '\tMain_usercode();',
        '',
        '\t::__0000_data.m_game_thread_sync.Update(__END_SYNC);',
        '\t::__0000_data.m_voice_thread_sync.Wait(__END_SYNC);',
        '}',
        '',
        '// False (and the mission failed) once the submarine carrier is gone.',
        'bool SubOk()',
        '{',
        '\tif( sub.IsAlive() ) return true;',
        '\tMissionGameOverEvent();',
        '\treturn false;',
        '}',
        '',
        '// Waits `second` seconds; false once the submarine is gone.',
        'bool Hold(float second)',
        '{',
        '\tfor( float t = 0.0f; t < second; t += 1.0f ) {',
        '\t\tif( !SubOk() ) return false;',
        '\t\tWait(1.0f);',
        '\t}',
        '\treturn SubOk();',
        '}',
        '',
        '// Waits until at most `left` enemies are left; false once the submarine is gone.',
        'bool Clear(int left)',
        '{',
        '\twhile( left < GetTeamObjectCount(TEAM_ID_ENEMY) ) {',
        '\t\tif( !SubOk() ) return false;',
        '\t\tWait(1.0f);',
        '\t}',
        '\treturn true;',
        '}',
        '',
        '// Waits until `o` is destroyed; false once the submarine is gone.',
        'bool Down(Object o)',
        '{',
        '\twhile( o.IsAlive() ) {',
        '\t\tif( !SubOk() ) return false;',
        '\t\tWait(1.0f);',
        '\t}',
        '\treturn true;',
        '}',
        '',
        '// The fight; false once the submarine is gone (the mission is failed by then).',
        'bool Fight()',
        '{',
        '\t// Wave 1: dropships over the deck drop ants onto it.',
        *[f'\t{c} = CreateEnemy({_q(n)}, {_q(CARRIER)}, 1, true);' for c, n in zip(carriers, CARRIERS)],
        '\tAlert_EnemyIncoming();',
        *[f'\t{c}.SetInstantEnemyGenerator(0, {_q(ANT)}, 12, 1, 0.5f, true).Action(UFOCARRIER_ACTION_OPEN_MODE);'
          for c in carriers],
        '\tif( !Hold(20.0f) || !Clear(5) ) return false;',
        '',
        '\t// Wave 2: mermen from the sea, bees from the air, and more ants.',
        '\tAlert_EnemyIncoming();',
        f'\tCreateEnemyGroup_Area("深海怪獣1", {_q(MERMAN)}, 4, 1.0f, true);',
        f'\tCreateEnemyGroup_Area("深海怪獣2", {_q(MERMAN)}, 4, 1.0f, true);',
        f'\tCreateEnemyGroup({_q(BEE_POINT)}, 150, {_q(BEE)}, 20, 1.0f, true);',
        *[f'\tif( {c}.IsAlive() ) {c}.SetInstantEnemyGenerator(0, {_q(ANT)}, 8, 1, 0.5f, true);' for c in carriers],
        '\tif( !Hold(20.0f) || !Clear(5) ) return false;',
        '',
        '\t// Wave 3: the Mothership as M082 makes it: the body invincible, the Genocide cannon the target.',
        '\tAlert_StrongEnemyIncoming();',
        f'\tmother = CreateEnemy("マザーシップ", {_q(MOTHER)}, 1, true).SetInvincible(true).ExcludeTeamManager(true);',
        '\tmother.Action(UFO_MOTHER511_ACTION_LIGHT_WEIGHT_EXPLOSION);',
        '\tcannon = mother.GetPartsObject(UFO_MOTHER511_PARTS_TYPE_CORE_BIG_CANNON, 0);',
        '\tcannon.SetDurability(150);',
        '\tif( !Hold(10.0f) ) return false;',
        '\tmother.Action(UFO_MOTHER511_ACTION_CORE_HATCH_OPEN);',
        '\tmother.Action(UFO_MOTHER511_ACTION_GENOCIDE_AUTO_ATTACK);',
        f'\tmother.SetEnemyGenerator(0, {_q(UFO)}, 8, 1, 0.5f, 70, true);',
        '\tif( !Down(cannon) ) return false;',
        '\tObjectSuicide(mother);',
        '\tif( !Hold(10.0f) ) return false;',
        '',
        '\t// Wave 4: the full Mothership, fought as M116 fights it.',
        '\tAlert_StrongEnemyIncoming();',
        f'\tmother = CreateEnemy("マザーシップ", {_q(MOTHER2)}, 1, false).SetDurabilityMin(20).ExcludeTeamManager(true);',
        '\tSetAiMoveSpeed(mother, 0.5f);',
        '\tcannon = mother.GetPartsObject(UFO_MOTHER511_PARTS_TYPE_CORE_BIG_CANNON, 0);',
        *[f'\tb{i} = mother.GetPartsObject(UFO_MOTHER511_PARTS_TYPE_BARRIER_GENERATOR, {i});' for i in range(5)],
        '\tmother.Action(UFO_MOTHER511_ACTION_ATTACK_MODE);',
        '\twhile( b0.IsAlive() || b1.IsAlive() || b2.IsAlive() || b3.IsAlive() || b4.IsAlive() ) {',
        '\t\tif( !SubOk() ) return false;',
        '\t\tWait(1.0f);',
        '\t}',
        '\tObjectAction(mother, UFO_MOTHER511_ACTION_CORE_HATCH_OPEN);',
        '\tif( !Hold(5.0f) ) return false;',
        '\tObjectAction(mother, UFO_MOTHER511_ACTION_GENOCIDE_EVENT_ATTACK);',
        '\tObjectAction(mother, UFO_MOTHER511_ACTION_GENOCIDE_AUTO_ATTACK);',
        '\tif( !Down(cannon) ) return false;',
        '\tmother.Action(UFO_MOTHER511_ACTION_CORE_HATCH_AUTO_CTRL);',
        '\tSetObjectDurabilityMin(mother, 40);',
        '\tif( !Hold(20.0f) ) return false;',
        '\tmother.SetDurabilityMin(0);',
        '\treturn Down(mother);',
        '}',
        '',
        'void Main_usercode()',
        '{',
        f'\tMap({_q(MAP)}, {_q(WEATHER)});',
        '',
        '\t// The submarine carrier first (flown by the plugin, not in the squad): the player starts on its deck.',
        f'\tsub = CreateFriend({_q(SUB_POINT)}, {_q(SUB)}, 1.00, false).SetDurability({kSubDurability:.0f});',
        '\tCreatePlayer("プレイヤー");',
        '\tWait(5.0f);',
        '',
        '\tif( !Fight() ) {',
        '\t\twhile( true ) sys_Yield();',
        '\t}',
        '\tWait(8.0f);',
        '\tMissionClear();',
        '\twhile( true ) sys_Yield();',
        '}',
        '',
        'void __0000_voice_event()',
        '{',
        '\t::__0000_data.m_voice_thread_sync.Update(__END_SYNC);',
        '\t::__0000_data.m_game_thread_sync.Wait(__END_SYNC);',
        '}',
        '',
    ])


def ours(out: str) -> bool:
    return os.path.isfile(os.path.join(out, MARKER))


def install(root: str) -> str:
    out = gen.mission_dir(root, MISSION)
    if os.path.isdir(out) and os.listdir(out) and not ours(out):
        raise RuntimeError(f'{out} 已有别的 mod 的文件，不覆盖。')
    missing = [f for f in SUB_FILES if not os.path.isfile(os.path.join(gen.object_dir(root), f))]
    if missing:
        raise RuntimeError(f'缺少潜舰文件 {missing}：游戏关闭时先运行 python tools/make_sub.py')
    game = gen.Game(root)
    moved = points(game.read(f'MISSION/EDF6/{MISSION}', 'MISSION.RMPA'))
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'MISSION.AC'), 'wb') as f:
        f.write(b'\xef\xbb\xbf' + script().encode('utf-8'))
    with open(os.path.join(out, 'MISSION.RMPA'), 'wb') as f:
        f.write(moved)
    with open(os.path.join(out, MARKER), 'w', encoding='utf-8') as f:
        f.write('潜舰对战母舰（EDF6VehicleCrew/testrange/sub_vs_mothership.py）。删除本目录即恢复 M082。\n')
    return out


def remove(root: str) -> bool:
    out = gen.mission_dir(root, MISSION)
    if not ours(out):
        return False
    shutil.rmtree(out)
    return True


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else gen.DEFAULT_GAME
    if '--remove' in argv:
        print('已删除' if remove(root) else '没有装过')
        return 0
    print('写入', install(root))
    print(f'离线任务列表第 {ITEM} 项「潜艇母舰‧保卫作战」（{MISSION}）现在是潜舰对战母舰：在潜舰甲板上作战。')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
