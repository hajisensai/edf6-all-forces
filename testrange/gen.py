"""EDF6 test range: builds a mission script from the chosen vehicles and enemy waves and installs it
over one offline mission (see SLOTS) through EDFModLoader's file redirect.

The range is the open plain of mission M045 (map ig_Heigen601): its point file (MISSION.RMPA) is
copied next to the generated script, so every point the script names exists. Both files are made on
this machine from the player's own Root.cpk and never distributed.
"""
from __future__ import annotations

import json
import math
import os
import shutil
import sys
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'lib'))
import cpk  # noqa: E402
import crilayla  # noqa: E402
import rmpa  # noqa: E402

DEFAULT_GAME = r'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'
SOURCE = 'M045'            # the plain whose map and points the range uses
MAP = 'app:/Map/ig_Heigen601.mac'
WEATHER = 'cloudy'
MARKER = 'EDF6TestRange.txt'


@dataclass(frozen=True)
class Slot:
    mission: str
    item: int          # position in the offline mission list (1-based)
    label: str


# Slots, by their position in the in-game offline mission list (checked by playing them; the order
# in MISSIONLIST.OFFLINE.LIST.SGO is not the on-screen order). The opening missions are the ruined
# world, where the Air Raider's vehicle and air support requests are accepted but never arrive.
# Item 14 「转机」 is M045, the plain the range is built from, and requests do arrive there.
SLOTS = [
    Slot('M045', 14, '列表第 14 项「转机」（M045）：空袭兵能呼叫载具和空中支援'),
    Slot('M001', 2, '列表第 2 项「非法入侵者」（M001）：新存档也能进，但前期剧情叫不来载具和空中支援'),
]
DEFAULT_SLOT = SLOTS[0].mission


def slot_of(mission: str) -> Slot:
    return next(x for x in SLOTS if x.mission == mission)

# (sgo, label). Only SGOs with a `mission_setup` block (weapon set-up for script-placed vehicles):
# CreateVehicle2 reads it, and a player call-in SGO without it crashes the game (EDF.dll+0x52E44).
# Stock missions place the `_mission` variants. No helicopter has one: call those in through the
# forced loadout (Air Raider vehicle slot) instead.
VEHICLES: list[tuple[str, str]] = [
    ('vehicle403_tank_mission', '坦克 403（AutoTurret 副炮）'),
    ('vehicle404_bigtank', '大型坦克 404（AutoTurret 副炮）'),
    ('v505_tank_mission', '坦克 505'),
    ('v603_flak_mission', '高射炮车 603（AutoTurret）'),
    ('v510_maser_mission', 'EMC 510'),
    ('v605_barga_cannon_mission', '巴尔加炮 605'),
    ('v504_begaruta_mission', '机甲 Begaruta 504'),
    ('vehicle407_bigbegaruta_mission', '大型机甲 407'),
    ('v612_nix_g_mission', '机甲 Nix G 612'),
    ('v614_proteus_mk2_mission', '机甲 Proteus 614'),
    ('v608_oldrobot_g_mission', '旧型机甲 608'),
    ('v515_retrobalam_mission', '巨型机甲 Balam 515（VehicleImpact）'),
    ('v503_bike', '摩托 503'),
    ('v613_bike', '摩托 613'),
    ('v512_keitruck', '轻卡车 512'),
]

# (sgo, label, flying)
ENEMIES: list[tuple[str, str, bool]] = [
    ('giantant01', '巨蚁', False),
    ('e650_giantant01', '巨蚁（EDF6）', False),
    ('giantspider01', '巨蜘蛛', False),
    ('e651_spider01_light', '蜘蛛（轻）', False),
    ('e503_frog_af_leader', '青蛙兵', False),
    ('e503_armorfrog_af', '装甲青蛙', False),
    ('e601_martian_gs', '火星人', False),
    ('giantbee01charge', '巨蜂（飞）', True),
    ('e507_goldufo', '金色 UFO（飞）', True),
    ('e515_imperialufo', '帝国 UFO（飞）', True),
    ('dragonsmall401', '小龙（飞）', True),
    ('shootingtarget', '训练靶子（不动）', False),
]


@dataclass
class Waves:
    enabled: bool = True
    enemy: str = 'giantant01'
    per_wave: int = 8
    max_alive: int = 12      # a new wave comes once fewer than this many enemies live
    first_delay: float = 30.0  # seconds before the first wave (time to try the vehicles)
    interval: float = 20.0
    level: float = 1.0


@dataclass
class Plan:
    vehicles: dict[str, int] = field(default_factory=lambda: {
        'vehicle403_tank_mission': 1, 'v603_flak_mission': 1, 'v504_begaruta_mission': 1})
    vehicle_level: float = 1.0
    waves: Waves = field(default_factory=Waves)
    loadout: dict = field(default_factory=dict)
    slot: str = DEFAULT_SLOT


def save_plan(path: str, plan: Plan) -> None:
    data = {'vehicles': plan.vehicles, 'vehicle_level': plan.vehicle_level,
            'waves': plan.waves.__dict__, 'loadout': plan.loadout, 'slot': plan.slot}
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def load_plan(path: str) -> Plan:
    try:
        with open(path, encoding='utf-8') as f:
            data = json.load(f)
    except (OSError, ValueError):
        return Plan()
    plan = Plan()
    known = {sgo for sgo, _ in VEHICLES}
    plan.vehicles = {k: int(v) for k, v in data.get('vehicles', plan.vehicles).items() if k in known}
    plan.vehicle_level = float(data.get('vehicle_level', plan.vehicle_level))
    plan.waves = Waves(**{k: v for k, v in data.get('waves', {}).items() if k in Waves.__dataclass_fields__})
    plan.loadout = data.get('loadout', {})
    if data.get('slot') in {x.mission for x in SLOTS}:
        plan.slot = data['slot']
    return plan


class Game:
    """Read-only view of the game's Root.cpk."""

    def __init__(self, root: str) -> None:
        self.root = root
        self.cpk = cpk.Cpk(os.path.join(root, 'Root.cpk'))

    def read(self, folder: str, name: str) -> bytes:
        for (d, n), e in self.cpk.index.items():
            if d.upper() == folder.upper() and n.upper() == name.upper():
                with open(self.cpk.path, 'rb') as h:
                    h.seek(self.cpk.base + int(e['FileOffset']))
                    data = h.read(int(e['FileSize']))
                return crilayla.decompress(data) if int(e['ExtractSize']) != int(e['FileSize']) else data
        raise KeyError(f'{folder}/{name}')


@dataclass
class Layout:
    player: rmpa.Point
    vehicle_points: list[rmpa.Point]
    enemy_points: list[rmpa.Point]


def layout(points: list[rmpa.Point]) -> Layout:
    """Vehicle spots: flat points 30-160 m from the player start, 15 m apart; enemy spots 180-450 m."""
    player = next(p for p in points if p.name == 'プレイヤー')
    by_distance = sorted(points, key=lambda p: math.dist(p.pos, player.pos))
    spots: list[rmpa.Point] = []
    for p in by_distance:
        d = math.dist(p.pos, player.pos)
        if 30 <= d <= 160 and abs(p.pos[1] - player.pos[1]) < 4 and all(math.dist(p.pos, s.pos) >= 15 for s in spots):
            spots.append(p)
    enemies = [p for p in by_distance if 180 <= math.dist(p.pos, player.pos) <= 450]
    return Layout(player, spots, enemies)


def _q(text: str) -> str:
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def script(plan: Plan, lay: Layout) -> str:
    """The mission script. Same skeleton as the stock generated scripts (event 0 = Main)."""
    chosen = [sgo for sgo, n in plan.vehicles.items() for _ in range(max(0, n))]
    if len(chosen) > len(lay.vehicle_points):
        raise ValueError(f'载具太多：这张地图玩家附近只有 {len(lay.vehicle_points)} 个空位')
    w = plan.waves
    flying = next((f for s, _, f in ENEMIES if s == w.enemy), False)
    preload = sorted({f'app:/object/{s}.sgo' for s in chosen} | ({f'app:/object/{w.enemy}.sgo'} if w.enabled else set()))
    lines = [
        '//',
        '// EDF6 test range, generated by testrange/gen.py (EDF6VehicleCrew). Delete this folder to restore mission 1.',
        '//',
        '',
        '#include "app:/Mission/AsCommon.h"',
        '',
        'Talkers g_soldiers;',
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
        'void Main_usercode()',
        '{',
        f'\tMap({_q(MAP)}, {_q(WEATHER)});',
        f'\tCreatePlayer({_q(lay.player.name)});',
    ]
    for sgo, point in zip(chosen, lay.vehicle_points):
        lines.append(f'\tCreateVehicle2({_q(point.name)}, {_q("app:/object/" + sgo + ".sgo")}, {plan.vehicle_level:.2f});')
    if w.enabled and lay.enemy_points:
        spawn = 'CreateEnemyGroup'
        pts = ', '.join(_q(p.name) for p in lay.enemy_points)
        lines += [
            '',
            '\t// Enemy waves: once fewer than max_alive enemies live, the next spot gets a new group.',
            f'\tarray<string> spots = {{ {pts} }};',
            '\tuint next = 0;',
            f'\tWait({w.first_delay:.1f});',
            '\twhile( true ) {',
            f'\t\tif( GetTeamObjectCount(TEAM_ID_ENEMY) < {int(w.max_alive)} ) {{',
            f'\t\t\t{spawn}(spots[next % spots.length()], 20, {_q("app:/object/" + w.enemy + ".sgo")}, {int(w.per_wave)}, {w.level:.2f}, true);',
            '\t\t\tnext++;',
            f'\t\t\tWait({w.interval:.1f});',
            '\t\t}',
            '\t\tWait(1.0);',
            '\t}',
        ]
        if flying:
            lines.insert(lines.index('\twhile( true ) {'), '\t// flying enemies are spawned at ground points too; they take off on their own')
    lines += [
        '\t// The mission must never return on its own (stock scripts end the same way); leave from the pause menu.',
        '\twhile( true ) sys_Yield();',
        '}',
        '',
        'void __0000_voice_event()',
        '{',
        '\t::__0000_data.m_voice_thread_sync.Update(__END_SYNC);',
        '\t::__0000_data.m_game_thread_sync.Wait(__END_SYNC);',
        '}',
        '',
    ]
    return '\n'.join(lines)


def has_mission_setup(game: Game, sgo_name: str) -> bool:
    import sgo
    try:
        values = sgo.load(data=game.read('OBJECT', sgo_name.upper() + '.SGO'))
    except KeyError:
        return False
    return isinstance(values, dict) and 'mission_setup' in values


def mission_dir(game_root: str, mission: str) -> str:
    return os.path.join(game_root, 'Mods', 'MISSION', 'EDF6', mission)


def ours(game_root: str, mission: str) -> bool:
    return os.path.isfile(os.path.join(mission_dir(game_root, mission), MARKER))


def installed(game_root: str) -> Slot | None:
    """The slot the range is installed in now, if any."""
    return next((x for x in SLOTS if ours(game_root, x.mission)), None)


def install(game_root: str, plan: Plan) -> list[str]:
    """Writes the range over plan.slot (and removes it from the other slot). Refuses to touch a
    folder another mod put there."""
    out = mission_dir(game_root, plan.slot)
    if os.path.isdir(out) and os.listdir(out) and not ours(game_root, plan.slot):
        raise RuntimeError(f'{out} 已有别的 mod 的文件，不覆盖。请先手动处理。')
    game = Game(game_root)
    for sgo_name in {s for s, n in plan.vehicles.items() if n > 0}:
        if not has_mission_setup(game, sgo_name):
            raise RuntimeError(f'{sgo_name} 没有 mission_setup，不能由脚本放置（会让游戏崩溃）')
    points_file = game.read(f'MISSION/EDF6/{SOURCE}', 'MISSION.RMPA')
    lay = layout(rmpa.points(points_file))
    text = script(plan, lay)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'MISSION.AC'), 'wb') as f:
        f.write(b'\xef\xbb\xbf' + text.encode('utf-8'))
    with open(os.path.join(out, 'MISSION.RMPA'), 'wb') as f:
        f.write(points_file)
    with open(os.path.join(out, MARKER), 'w', encoding='utf-8') as f:
        f.write('EDF6 测试场（EDF6VehicleCrew/testrange）。删除本目录即恢复这一关。\n')
    for other in SLOTS:
        if other.mission != plan.slot:
            _remove(game_root, other.mission)
    chosen = [sgo for sgo, n in plan.vehicles.items() for _ in range(max(0, n))]
    return [f'{p.name}: {s}' for s, p in zip(chosen, lay.vehicle_points)]


def uninstall(game_root: str) -> bool:
    return any([_remove(game_root, x.mission) for x in SLOTS])


def _remove(game_root: str, mission: str) -> bool:
    out = mission_dir(game_root, mission)
    if not ours(game_root, mission):
        return False
    shutil.rmtree(out)
    parent = os.path.dirname(out)
    for d in (parent, os.path.dirname(parent)):   # Mods/MISSION/EDF6, Mods/MISSION if now empty
        if os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
    return True
