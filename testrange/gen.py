"""EDF6 test range: builds a mission script from the chosen vehicles and enemy waves and installs it
over mission 1 (M001) through EDFModLoader's file redirect.

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
TARGET = 'M001'            # mission 1: unlocked on every save
SOURCE = 'M045'            # the plain whose map and points the range uses
MAP = 'app:/Map/ig_Heigen601.mac'
WEATHER = 'cloudy'
MARKER = 'EDF6TestRange.txt'
MISSION_DIR = ('Mods', 'MISSION', 'EDF6', TARGET)

# (sgo, label). Player versions of every vehicle the game has.
VEHICLES: list[tuple[str, str]] = [
    ('v602_heli', '直升机 V602（插件驾驶）'),
    ('v506_heli', '直升机 V506'),
    ('vehicle409_heli', '直升机 409'),
    ('vehicle410_heli', '直升机 410（多炮手位）'),
    ('vehicle403_tank', '坦克 403（AutoTurret 副炮）'),
    ('vehicle404_bigtank', '大型坦克 404（AutoTurret 副炮）'),
    ('v505_tank', '坦克 505'),
    ('v601_tank', '坦克 601'),
    ('v603_flak', '高射炮车 603（AutoTurret）'),
    ('vehicle402_rocket', '火箭车 402'),
    ('v510_maser', 'EMC 510'),
    ('v504_begaruta', '机甲 Begaruta 504'),
    ('vehicle407_bigbegaruta', '大型机甲 407'),
    ('v612_nix', '机甲 Nix 612'),
    ('v614_proteus_mk2', '机甲 Proteus 614'),
    ('v515_retrobalam', '巨型机甲 Balam 515（VehicleImpact）'),
    ('vehicle502_groundrobo', '机甲 502'),
    ('v503_bike', '摩托 503'),
    ('v613_bike', '摩托 613'),
    ('vehicle401_striker', '装甲车 401'),
    ('v507_rescuetank', '救援车 507'),
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
        'v602_heli': 1, 'vehicle403_tank': 1, 'v603_flak': 1, 'v504_begaruta': 1})
    vehicle_level: float = 1.0
    waves: Waves = field(default_factory=Waves)
    loadout: dict = field(default_factory=dict)


def save_plan(path: str, plan: Plan) -> None:
    data = {'vehicles': plan.vehicles, 'vehicle_level': plan.vehicle_level,
            'waves': plan.waves.__dict__, 'loadout': plan.loadout}
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def load_plan(path: str) -> Plan:
    try:
        with open(path, encoding='utf-8') as f:
            data = json.load(f)
    except (OSError, ValueError):
        return Plan()
    plan = Plan()
    plan.vehicles = {k: int(v) for k, v in data.get('vehicles', plan.vehicles).items()}
    plan.vehicle_level = float(data.get('vehicle_level', plan.vehicle_level))
    plan.waves = Waves(**{k: v for k, v in data.get('waves', {}).items() if k in Waves.__dataclass_fields__})
    plan.loadout = data.get('loadout', {})
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


def mission_dir(game_root: str) -> str:
    return os.path.join(game_root, *MISSION_DIR)


def installed(game_root: str) -> bool:
    return os.path.isfile(os.path.join(mission_dir(game_root), MARKER))


def install(game_root: str, plan: Plan) -> list[str]:
    """Writes the range over mission 1. Refuses to touch a folder another mod put there."""
    out = mission_dir(game_root)
    if os.path.isdir(out) and os.listdir(out) and not installed(game_root):
        raise RuntimeError(f'{out} 已有别的 mod 的文件，不覆盖。请先手动处理。')
    game = Game(game_root)
    points_file = game.read(f'MISSION/EDF6/{SOURCE}', 'MISSION.RMPA')
    lay = layout(rmpa.points(points_file))
    text = script(plan, lay)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'MISSION.AC'), 'wb') as f:
        f.write(b'\xef\xbb\xbf' + text.encode('utf-8'))
    with open(os.path.join(out, 'MISSION.RMPA'), 'wb') as f:
        f.write(points_file)
    with open(os.path.join(out, MARKER), 'w', encoding='utf-8') as f:
        f.write('EDF6 测试场（EDF6VehicleCrew/testrange）。删除本目录即恢复第 1 关。\n')
    chosen = [sgo for sgo, n in plan.vehicles.items() for _ in range(max(0, n))]
    return [f'{p.name}: {s}' for s, p in zip(chosen, lay.vehicle_points)]


def uninstall(game_root: str) -> bool:
    out = mission_dir(game_root)
    if not installed(game_root):
        return False
    shutil.rmtree(out)
    parent = os.path.dirname(out)
    for d in (parent, os.path.dirname(parent)):   # Mods/MISSION/EDF6, Mods/MISSION if now empty
        if os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
    return True
