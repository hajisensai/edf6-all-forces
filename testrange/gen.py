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
import struct
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


# Slots, by their position in the in-game offline mission list: item N is entry N-1 of
# MISSION/MISSIONLIST.OFFLINE.LIST.SGO, and its title is entry N-1 of MISSIONLIST.OFFLINE.TXT.*.SGO
# (item 14 「转机」 = RM015, item 2 「非法入侵者」 = M001). The range only borrows the slot: its map and
# points are always M045's (SOURCE). The opening missions are the ruined world, where the Air Raider's
# vehicle and air support requests are accepted but never arrive; in item 14 they do.
SLOTS = [
    Slot('RM015', 14, '列表第 14 项「转机」（RM015）：空袭兵能呼叫载具和空中支援'),
    Slot('M001', 2, '列表第 2 项「非法入侵者」（M001）：新存档也能进，但前期剧情叫不来载具和空中支援'),
]
DEFAULT_SLOT = SLOTS[0].mission


def slot_of(mission: str) -> Slot:
    return next(x for x in SLOTS if x.mission == mission)

# (sgo, label). Only SGOs with a `mission_setup` block (weapon set-up for script-placed vehicles):
# CreateVehicle2 reads it, and a player call-in SGO without it crashes the game (EDF.dll+0x52E44).
# Stock missions place the `_mission` variants. No helicopter has one, so the range makes its own
# (see DERIVED): the call-in SGO with `vehicle_setup` renamed to `mission_setup` (same layout).
# Four heli bodies: V506 Eros (and its DLC No. 6 body), VEHICLE409 Nereid, VEHICLE410 Brute, V602 Heron
# (a DSGO; class Vehicle506_Helicopter like the Eros, two nose gatlings and a missile).
VEHICLES: list[tuple[str, str]] = [
    ('edf6tr_v506_heli_mission', '直升机 506 Eros（测试场生成）'),
    ('edf6tr_v506_heli_edf6benefits_mission', '直升机 Eros No.6（测试场生成）'),
    ('edf6tr_vehicle409_heli_mission', '直升机 409 Nereid（测试场生成）'),
    ('edf6tr_vehicle410_heli_mission', '直升机 410 Brute（测试场生成）'),
    ('edf6tr_v602_heli_mission', '直升机 602 Heron（测试场生成）'),
    ('edf6tr_jet_strike_mission', '对地攻击机（插件驾驶，测试场生成）'),
    ('edf6tr_jet_fighter_mission', '制空战斗机（插件驾驶，测试场生成）'),
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

# Vehicles the range generates into Mods/OBJECT (name -> stock SGO it is made from). The DERIVED_PREFIX
# marks them as ours: install/uninstall only ever touch files with it.
DERIVED_PREFIX = 'edf6tr_'
DERIVED: dict[str, str] = {
    'edf6tr_v506_heli_mission': 'V506_HELI',
    'edf6tr_v506_heli_edf6benefits_mission': 'V506_HELI_EDF6BENEFITS',
    'edf6tr_vehicle409_heli_mission': 'VEHICLE409_HELI',
    'edf6tr_vehicle410_heli_mission': 'VEHICLE410_HELI',
    'edf6tr_v602_heli_mission': 'V602_HELI',
    'edf6tr_jet_strike_mission': 'V506_HELI',
    'edf6tr_jet_fighter_mission': 'V506_HELI',
}


@dataclass(frozen=True)
class Jet:
    mark: float        # mission_setup[1][0], the speed gain k: how EDF6VehicleCrew (src/jet.cpp) tells a jet
    durability: float
    weapons: tuple[str, ...]


# Jets (src/jet.cpp, docs/jet-model-re.md): the V506 heli body (rigid body, HP, weapons, crash) with the
# BOMBER501 model, flown by the plugin. The 506 fires 0x2020 -> weapons 0 and 1, 0x2021 -> weapon 2.
_GUNS = ('app:/weapon/v_506heli_gatling01_l.sgo', 'app:/weapon/v_506heli_gatling01_r.sgo')
_MISSILE = 'app:/weapon/v_506heli_missile01.sgo'
JETS: dict[str, Jet] = {
    'edf6tr_jet_strike_mission': Jet(7001.0, 1500.0, _GUNS + (_MISSILE,)),
    'edf6tr_jet_fighter_mission': Jet(7002.0, 1000.0, _GUNS + (_MISSILE,)),
}
JET_MODEL = ['app:/object/bomber501.mrab', 'bomber501.mdb']
JET_ROOT_BONE = 'mdl'
# The V506 MAB block's locator parent names (UTF-16, block offsets), shortened in place to JET_ROOT_BONE:
# the bomber has only `mdl` and `bomber501` (docs/jet-model-re.md §1, §3.3).
JET_MAB_BONES = ((0x360, 'body'), (0x372, 'rotor'), (0x37E, 'tailRotor'))
# Fuselage only (half extents; the 25 m wingspan left out so low passes do not scrape), centre as the model.
JET_RIGID_BODY = [[0.0, 0.34, 2.6], [2.0, 1.6, 13.0]]


def _rebone(v, names: set[str]):
    """`v` with every string in `names` replaced by JET_ROOT_BONE (deep)."""
    if isinstance(v, list):
        return [_rebone(c, names) for c in v]
    return JET_ROOT_BONE if isinstance(v, str) and v in names else v


JET_BODY_BONE = 'bomber501'


def _jet_ragdoll(blob: bytes) -> bytes:
    """The ragdoll's embedded binding SGO with every model-side bone one the bomber has. The binder
    (RagdollController::BindDependency 0x6E6A50) does not survive a missing bone: an entry left unbound
    crashes it at 0x6E8284 while the vehicle is built (seen 2026-10-03 with the V506 names). Every proxy
    follows the fuselage bone; only the body proxy drives it (the rotor proxies spin)."""
    import sgowrite
    version, inner = sgowrite.read(blob)
    inner['ragdoll_from_animation'] = [[[JET_BODY_BONE, e[0][1]]] + e[1:] for e in inner['ragdoll_from_animation']]
    inner['animation_from_ragdoll'] = [[[e[0][0], JET_BODY_BONE]] + e[1:]
                                       for e in inner['animation_from_ragdoll'] if e[0][1] == 'body']
    return sgowrite.write(version, inner)


def jet_sgo(game: Game, name: str) -> bytes:
    import sgowrite
    jet = JETS[name]
    version, m = sgowrite.read(game.read('OBJECT', DERIVED[name] + '.SGO'))
    if 'vehicle_setup' not in m or 'mission_setup' in m:
        raise ValueError('V506_HELI 没有 vehicle_setup')
    setup = m.pop('vehicle_setup')
    setup[1][0] = jet.mark
    stock = {w[0]: w for w in setup[3]}   # each weapon keeps its stock per-weapon parameters
    setup[3] = [stock.get(w, [w, [0.0001, 0.1]]) for w in jet.weapons] + [stock['app:/weapon/v_fuel01.sgo']]
    m['mission_setup'] = setup
    m['game_object_durability'] = jet.durability
    model = m['animation_model']
    mab = model[2]
    for at, old in JET_MAB_BONES:
        mab = sgowrite.replace_utf16(mab, at, old, JET_ROOT_BONE)
    m['animation_model'] = [list(JET_MODEL), model[1], mab]
    m['animation_model_bone_mapping'] = [JET_ROOT_BONE, 'bomber501']
    bones = {'body', 'rotor', 'tailRotor'}
    m['vehicle_weapon_setting'] = [[JET_ROOT_BONE, 0]] * len(jet.weapons) + [[JET_ROOT_BONE, -1]]
    m['vehicle_dead_effect'] = _rebone(m['vehicle_dead_effect'], bones)
    m['roter_contact_damage_scale'] = 0.0
    m['heli_contact_damage_scale'] = 0.0005
    rb = m['heli_rigid_body']
    m['heli_rigid_body'] = [JET_RIGID_BODY[0], JET_RIGID_BODY[1], rb[2]]
    rag = m['ragdoll']
    m['ragdoll'] = [rag[0], _jet_ragdoll(rag[1])]
    return sgowrite.write(version, m)

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
    # Vehicles spawned with an NPC driver (CreateFriend, as stock missions spawn allied vehicles). An
    # NPC-piloted heli is flown by the plugin, several of them in formation.
    friends: dict[str, int] = field(default_factory=dict)


def placements(plan: Plan) -> list[tuple[str, bool]]:
    """(sgo, NPC-driven) for every vehicle to place, empty ones first."""
    return ([(s, False) for s, n in plan.vehicles.items() for _ in range(max(0, n))] +
            [(s, True) for s, n in plan.friends.items() for _ in range(max(0, n))])


def save_plan(path: str, plan: Plan) -> None:
    data = {'vehicles': plan.vehicles, 'vehicle_level': plan.vehicle_level,
            'waves': plan.waves.__dict__, 'loadout': plan.loadout, 'slot': plan.slot,
            'friends': plan.friends}
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
    plan.friends = {k: int(v) for k, v in data.get('friends', {}).items() if k in known}
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
    chosen = placements(plan)
    if len(chosen) > len(lay.vehicle_points):
        raise ValueError(f'载具太多：这张地图玩家附近只有 {len(lay.vehicle_points)} 个空位')
    w = plan.waves
    flying = next((f for s, _, f in ENEMIES if s == w.enemy), False)
    preload = sorted({f'app:/object/{s}.sgo' for s, _ in chosen} | ({f'app:/object/{w.enemy}.sgo'} if w.enabled else set()))
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
    for (sgo, npc), point in zip(chosen, lay.vehicle_points):
        path = _q('app:/object/' + sgo + '.sgo')
        if npc:   # last argument: does it join the player's squad (no: the plugin flies/drives it)
            lines.append(f'\tCreateFriend({_q(point.name)}, {path}, {plan.vehicle_level:.2f}, false);')
        else:
            lines.append(f'\tCreateVehicle2({_q(point.name)}, {path}, {plan.vehicle_level:.2f});')
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


def as_mission_sgo(data: bytes) -> bytes:
    """A call-in vehicle SGO turned into a script-placeable one: its `vehicle_setup` name becomes
    `mission_setup` (same length, same value layout) and the name table is re-sorted. Little-endian SGO:
    header {count, data offset, name count, name table offset} at 8, names {string offset from the entry,
    member index}. DSGO: see _sort_dsgo_names."""
    old, new = 'vehicle_setup'.encode('utf-16le') + b'\0\0', 'mission_setup'.encode('utf-16le') + b'\0\0'
    if data[:4] not in (b'SGO\0', b'DSGO'):
        raise ValueError('不是小端 SGO / DSGO')
    if data.count(old) != 1 or new in data:
        raise ValueError('vehicle_setup 不唯一或已有 mission_setup')
    buf = bytearray(data.replace(old, new))
    if data[:4] == b'DSGO':
        _sort_dsgo_names(buf)
        return bytes(buf)
    _, _, name_count, name_off = struct.unpack_from('<4I', buf, 8)
    entries = []
    for i in range(name_count):
        p = name_off + i * 8
        rel, idx = struct.unpack_from('<iI', buf, p)
        entries.append((_utf16_at(buf, p + rel), p + rel, idx))
    for i, (_, at, idx) in enumerate(sorted(entries)):
        p = name_off + i * 8
        struct.pack_into('<iI', buf, p, at - p, idx)
    return bytes(buf)


def _sort_dsgo_names(buf: bytearray) -> None:
    """Re-sort the top-level dictionary's name table of a DSGO (see lib/sgo.py): node 0 at the node
    table is that dictionary, {name table offset, name count, ...} at node + value; names are {string
    offset from the entry, member position}, kept sorted like the stock files."""
    table = struct.unpack_from('<I', buf, 4)[0]
    raw, typ = struct.unpack_from('<QI', buf, table)
    if typ != 3:
        raise ValueError('DSGO 顶层不是字典')
    d = table + raw
    name_off, name_count = struct.unpack_from('<II', buf, d)
    entries = []
    for k in range(name_count):
        e = d + name_off + k * 8
        so, member = struct.unpack_from('<II', buf, e)
        entries.append((_utf16_at(buf, e + so), e + so, member))
    for k, (_, at, member) in enumerate(sorted(entries)):
        e = d + name_off + k * 8
        struct.pack_into('<II', buf, e, at - e, member)


def _utf16_at(buf: bytes, off: int) -> str:
    end = off
    while buf[end:end + 2] != b'\0\0':
        end += 2
    return buf[off:end].decode('utf-16le')


def vehicle_sgo(game: Game, sgo_name: str) -> bytes:
    """The SGO bytes the mission will load for this vehicle (generated ones are made here)."""
    if sgo_name in JETS:
        return jet_sgo(game, sgo_name)
    stock = DERIVED.get(sgo_name)
    if stock:
        return as_mission_sgo(game.read('OBJECT', stock + '.SGO'))
    return game.read('OBJECT', sgo_name.upper() + '.SGO')


def has_mission_setup(game: Game, sgo_name: str) -> bool:
    import sgo
    try:
        values = sgo.load(data=vehicle_sgo(game, sgo_name))
    except (KeyError, ValueError):
        return False
    return isinstance(values, dict) and 'mission_setup' in values


def object_dir(game_root: str) -> str:
    return os.path.join(game_root, 'Mods', 'OBJECT')


def _write_derived(game_root: str, game: Game, wanted: set[str]) -> None:
    """Makes Mods/OBJECT hold exactly the generated vehicles in `wanted` (files with our prefix only)."""
    _remove_derived(game_root, keep=wanted)
    for name in sorted(wanted):
        os.makedirs(object_dir(game_root), exist_ok=True)
        with open(os.path.join(object_dir(game_root), name.upper() + '.SGO'), 'wb') as f:
            f.write(vehicle_sgo(game, name))


def _remove_derived(game_root: str, keep: set[str] = frozenset()) -> bool:
    d = object_dir(game_root)
    if not os.path.isdir(d):
        return False
    gone = [f for f in os.listdir(d) if f.lower().startswith(DERIVED_PREFIX) and f.lower()[:-4] not in keep]
    for f in gone:
        os.remove(os.path.join(d, f))
    if not os.listdir(d):
        os.rmdir(d)
    return bool(gone)


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
    for sgo_name in {s for s, _ in placements(plan)}:
        if not has_mission_setup(game, sgo_name):
            raise RuntimeError(f'{sgo_name} 没有 mission_setup，不能由脚本放置（会让游戏崩溃）')
    points_file = game.read(f'MISSION/EDF6/{SOURCE}', 'MISSION.RMPA')
    lay = layout(rmpa.points(points_file))
    text = script(plan, lay)
    os.makedirs(out, exist_ok=True)
    _write_derived(game_root, game, {s for s, _ in placements(plan) if s in DERIVED})
    with open(os.path.join(out, 'MISSION.AC'), 'wb') as f:
        f.write(b'\xef\xbb\xbf' + text.encode('utf-8'))
    with open(os.path.join(out, 'MISSION.RMPA'), 'wb') as f:
        f.write(points_file)
    with open(os.path.join(out, MARKER), 'w', encoding='utf-8') as f:
        f.write('EDF6 测试场（EDF6VehicleCrew/testrange）。删除本目录即恢复这一关。\n')
    for other in SLOTS:
        if other.mission != plan.slot:
            _remove(game_root, other.mission)
    return [f'{p.name}: {s}{"（NPC 驾驶）" if npc else ""}' for (s, npc), p in zip(placements(plan), lay.vehicle_points)]


def uninstall(game_root: str) -> bool:
    return any([_remove(game_root, x.mission) for x in SLOTS] + [_remove_derived(game_root)])


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
