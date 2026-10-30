"""What the test range can place: one catalog for the range's own vehicle / enemy lists (testrange/gen.py VEHICLES,
ENEMIES, testrange/testrange.pyw) and for the plugin's placement (the M map's 「摆放」 panel in the test range and the
debug spawn tool: src/spawn_catalog.inc, written from here by tools/gen_spawn_catalog.py).

The user, 2026-10-17: 「测试场的东西该更新了……测试场支持m里面手动摆放怪物和各种载具之类的」. Rather than a third hand-copied
list, every row comes from a registry the tools already keep:
  - STOCK_VEHICLES / STOCK_ENEMIES: EDF6's own objects (Root.cpk) that the range and the stock missions place. A
    vehicle's SGO carries `mission_setup` (tests/debug_spawn_resource_audit.py reads each in Root.cpk);
  - the range's own vehicles (testrange/gen.py DERIVED: written into Mods/OBJECT by the range's install) that the player
    drives empty (gen.PLACEABLE_DERIVED);
  - the installer's vehicles (pylib/vcobjects.py GROUND_VEHICLES whose SGO already is a mission one: the civilian sedan);
  - EDF4.1's ported objects (edf41port/objects.json, installed into Mods by tools/edf41_objects.py: pre-converted, no
    EDF4.1 needed; a row is there in the game only when its files are, which the plugin and gen.install check);
  - EDF5 Mission Pack 1's fortress (edf5port/dlc/registry.json, tools/edf5_dlc.py).
tools/selftest.py spawn_catalog_* checks that nothing a registry adds is missed (each 4.1 object is a row or named in
EDF41_LEFT_OUT with why) and that src/spawn_catalog.inc is what this writes.

A row: `sgo` the object name as the game's app:/object/ path has it (no folder prefix, no .sgo; lower case; a 4.1 DLC
object keeps its folder: 'edf41mp01/giantant04'), its label, its category (the panel's tab), where its file comes from
(`source`), how far over the clicked ground it is made (`lift`), whether it flies (the range's waves) and whether the
plugin preloads it in the test range by default (`preload`; ini RangePlaceAll=1 preloads every row).
"""
from __future__ import annotations

import json
import os
import re
import sys
from dataclasses import dataclass
from functools import lru_cache

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import xm_units  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
# The registries: bundled next to the frozen installer (tools/build_release.py --add-data, as tools/edf41_objects.py
# reads them), else the source tree's.
ROOT = sys._MEIPASS if getattr(sys, 'frozen', False) else os.path.normpath(os.path.join(HERE, '..'))  # type: ignore[attr-defined]
EDF41_REGISTRY = os.path.join(ROOT, 'edf41port', 'objects.json')
EDF5_DLC_REGISTRY = os.path.join(ROOT, 'edf5port', 'dlc', 'registry.json')

# The panel's tabs, in order (src/debug_spawn.h Category; aircraft and soldiers are the plugin's own rows, written in
# src/debug_spawn.h: the support calls and the support soldiers).
CATEGORIES = ('vehicle', 'ours', 'aircraft', 'enemy', 'enemy41', 'soldier')
# Where a row's file is: Root.cpk ('stock'), Mods/OBJECT written by the range's install ('range'), by the installer
# ('installer', 'edf41', 'edf5dlc').
SOURCES = ('stock', 'range', 'installer', 'edf41', 'edf5dlc')
# A model archive (the 4.1 preload rule: what a variant costs in memory; tools/gen_spawn_catalog.py MODEL_ARCHIVES too).
MODEL_ARCHIVES = ('.MRAB', '.RAB')


@dataclass(frozen=True)
class Row:
    sgo: str
    label: str
    category: str
    source: str
    lift: float = 0.5
    flying: bool = False
    preload: bool = True
    spawn_sgo: str | None = None   # private configured object; sgo remains the saved-plan/log identity

    @property
    def path(self) -> str:
        """The game's path of its SGO (what the plugin preloads and CreateObject reads)."""
        return f'app:/object/{(self.spawn_sgo or self.sgo).upper()}.sgo'

    @property
    def rel(self) -> str:
        """Its SGO under Mods/ (a 'stock' row's is in Root.cpk instead)."""
        return f'OBJECT/{(self.spawn_sgo or self.sgo).upper()}.SGO'

    @property
    def ident(self) -> str:
        """The log's id (ASCII; src/debug_spawn.h Entry::id): its SGO's name, a folder's slash as '/'."""
        return self.sgo.upper()


# EDF6's own vehicles that the stock missions place (their `_mission` SGOs, or the unarmed ones with a mission_setup).
# (sgo, label)
STOCK_VEHICLES: tuple[tuple[str, str], ...] = (
    ('vehicle403_tank_mission', '坦克 403'),
    ('vehicle404_bigtank', '大型坦克 404'),
    ('v505_tank_mission', '布莱克战车 505'),
    ('v603_flak_mission', '高射炮车 603'),
    ('v510_maser_mission', 'EMC 510'),
    ('v605_barga_cannon_mission', '巴尔加炮 605'),
    ('v507_rescuetank_ai', '救援装甲车 507'),
    ('v504_begaruta_mission', '机甲 Begaruta 504'),
    ('vehicle407_bigbegaruta_mission', '大型机甲 407'),
    ('v612_nix_g_mission', '机甲 Nix G 612'),
    ('v614_proteus_mk2_mission', '机甲 Proteus 614'),
    ('v608_oldrobot_g_mission', '旧型机甲 608'),
    ('v515_retrobalam_mission', '巨型机甲 Balam 515'),
    ('v503_bike', '摩托 503'),
    ('v613_bike', '摩托 613'),
    ('v512_keitruck', '轻卡车 512'),
    # 2026-10-19 (the user: 「载具与npc少了」): every other stock vehicle a stock mission places with its own mission_setup
    # (Root.cpk, tests/debug_spawn_resource_audit.py): the colours, the DLC bikes and mechs, and the kinds that only come
    # as an NPC-crewed `_ai` SGO (their mission_setup carries the AI's weapons and durability; placed empty all the same).
    ('v504_begaruta_g_mission', '机甲 Begaruta 504（G）'),
    ('v504_begaruta_fw', '机甲 Begaruta 504（FW）'),
    ('v504_begaruta_dlchs_mission', '机甲 Begaruta 504（DLC 高速型）'),
    ('vehicle407_bigbegaruta', '大型机甲 407（原型）'),
    ('vehicle407_bigbegaruta_flame_ai', '大型机甲 407 火焰（NPC 型）'),
    ('v515_retrobalam_gold', '巨型机甲 Balam 515（金）'),
    ('v515_retrobalam_green', '巨型机甲 Balam 515（绿）'),
    ('v515_retrobalam_gray', '巨型机甲 Balam 515（灰）'),
    ('v515_retrobalam_ulti', '巨型机甲 Balam 515（终极）'),
    ('v601_tank_ai', '坦克 601（NPC 型）'),
    ('v607_robotruck_ai', '机器人卡车 607（NPC 型）'),
    ('v607_robotruck_f_ai', '机器人卡车 607 F（NPC 型）'),
    ('v607_robotruck_g_ai', '机器人卡车 607 G（NPC 型）'),
    ('v607_robotruck_m_ai', '机器人卡车 607 M（NPC 型）'),
    ('v610_ptruck_gun_ai', '武装皮卡 610 机枪（NPC 型）'),
    ('v611_ptruck_rocket_ai', '武装皮卡 611 火箭（NPC 型）'),
    ('vehicle402_rocket_ai', '火箭车 402（NPC 型）'),
    ('v612_nix_ai', '机甲 Nix 612（NPC 型）'),
    ('v608_oldrobot_ai', '旧型机甲 608（NPC 型）'),
    ('v608_oldrobot_long_ai', '旧型机甲 608 长炮（NPC 型）'),
    ('v614_proteus_mk2_flame_ai', '机甲 Proteus 614 火焰（NPC 型）'),
    ('v614_proteus_mk2_laser_ai', '机甲 Proteus 614 激光（NPC 型）'),
    ('v503_bike_edf6benefits', '摩托 503（特典）'),
    ('v503_bike_omegaz', '摩托 503（Omega Z）'),
    ('v512_keitruck_bgp', '轻卡车 512（BGP）'),
    ('v513_trailertruck01cab', '拖车头 513'),
)

# EDF6's own enemies: those the range's waves and the grand battle use, the debug spawn tool's, and every other one the
# stock missions place. (sgo, label, flying, lift: m over the clicked ground; a flier is made up there and flies off on
# its own, a ship is made at its height; preloaded at the mission's start, else loaded when first placed)
STOCK_ENEMIES: tuple[tuple[str, str, bool, float, bool], ...] = (
    ('giantant01', '巨蚁', False, 0.5, True),
    ('e650_giantant01', '巨蚁（EDF6）', False, 0.5, True),
    ('giantspider01', '巨蜘蛛', False, 0.5, True),
    ('giantantqueen', '母体（女王巨蚁）', False, 0.5, True),
    ('e651_spider01_light', '蜘蛛（轻）', False, 0.5, True),
    ('e503_frog_af_leader', '青蛙兵（队长）', False, 0.5, True),
    ('e503_frog_af', '青蛙兵', False, 0.5, True),
    ('e503_armorfrog_af', '装甲青蛙', False, 0.5, True),
    ('e601_martian_gs', '火星人 GS', False, 0.5, True),
    ('e601_martian_ls', '火星人 LS', False, 0.5, True),
    ('e602_berserker', '狂战士 E602', False, 0.5, True),
    ('e513_shieldbearer', '护盾运载者 E513', False, 0.5, True),
    ('giantbee01charge', '巨蜂（飞）', True, 20.0, True),
    ('e507_goldufo', '金色 UFO（飞）', True, 30.0, True),
    ('e515_imperialufo', '帝国 UFO（飞）', True, 30.0, True),
    ('e605_spinnerufo', 'UFO E605（飞）', True, 30.0, True),
    ('dragonsmall401', '蝌蚪（五六代，飞）', True, 30.0, True),
    ('e508_carrier', '传送舰（飞）', True, 250.0, True),
    ('e611_timeship', '星导者新舰（飞）', True, 400.0, True),
    ('e511_mothership_edf6', '母舰（飞）', True, 900.0, True),
    ('shootingtarget', '训练靶子（不动）', False, 0.5, True),
    # 2026-10-19 (the user: 「好多怪物少了」): every other enemy the stock missions' scripts place (Root.cpk MISSION/**/
    # MISSION.AC: CreateEnemy*, CreateEnemyGroup*, generators), each kind with its colours, ranks and weapons. The last field:
    # preloaded at the mission's start (its model archives are already the rows' above, so no more memory), else loaded
    # the first time it is placed (debug_spawn.cpp RequestLoad: a few seconds, then it is made where it was asked).
    ('e503_frog_lc', '异星殖民者（激光）', False, 0.5, True),
    ('e503_frog_sg', '异星殖民者（霰弹）', False, 0.5, True),
    ('e503_frog_lc_leader', '异星殖民者（激光·队长）', False, 0.5, True),
    ('e503_frog_sg_leader', '异星殖民者（霰弹·队长）', False, 0.5, True),
    ('e503_armorfrog_lc', '装甲殖民者（激光）', False, 0.5, True),
    ('e503_armorfrog_sg', '装甲殖民者（霰弹）', False, 0.5, True),
    ('e503_armorfrog_af_leader', '装甲殖民者（队长）', False, 0.5, True),
    ('e617_wildfrog_g', '野生殖民者', False, 0.5, False),
    ('e617_wildfrog_g2', '野生殖民者 2', False, 0.5, False),
    ('e506_biggrey_af', '异星航天员（突击）', False, 0.5, False),
    ('e506_biggrey_ll', '异星航天员（激光）', False, 0.5, False),
    ('e506_biggrey_sg', '异星航天员（霰弹）', False, 0.5, False),
    ('e506_biggrey_af_leader', '异星航天员（队长）', False, 0.5, False),
    ('e506_nude_biggrey_af', '异星航天员（无护甲）', False, 0.5, False),
    ('e506_ha_biggrey_hmg', '重装航天员（重机枪）', False, 0.5, False),
    ('e506_ha_biggrey_fl', '重装航天员（火焰）', False, 0.5, False),
    ('e506_ha_biggrey_ml', '重装航天员（导弹）', False, 0.5, False),
    ('e506_ha_biggrey_ll', '重装航天员（激光）', False, 0.5, False),
    ('e516_biggreyboss', '银之人', False, 0.5, False),
    ('e601_martian_gg', '库鲁鲁 GG', False, 0.5, True),
    ('e601_martian_ll', '库鲁鲁 LL', False, 0.5, True),
    ('e601_martian_ws', '库鲁鲁 WS', False, 0.5, True),
    ('e601_martian_ns', '库鲁鲁 NS', False, 0.5, True),
    ('e601_martian_n', '库鲁鲁 N', False, 0.5, True),
    ('e601_martian_nn', '库鲁鲁 NN', False, 0.5, True),
    ('e601_martian_ggss', '库鲁鲁 GGSS', False, 0.5, True),
    ('e601_martian_llss', '库鲁鲁 LLSS', False, 0.5, True),
    ('e601_martian_wwss', '库鲁鲁 WWSS', False, 0.5, True),
    ('e601_martian_ssss', '库鲁鲁 SSSS', False, 0.5, True),
    ('e601_martian_llll', '库鲁鲁 LLLL', False, 0.5, True),
    ('e661_martian_leader_cs', '库鲁鲁队长 CS', False, 0.5, False),
    ('e661_martian_leader_ggss', '库鲁鲁队长 GGSS', False, 0.5, False),
    ('e661_martian_leader_aass', '库鲁鲁队长 AASS', False, 0.5, False),
    ('giantant02b', '侵略生物α（红）', False, 0.5, True),
    ('giantant03b', '侵略生物α（酸）', False, 0.5, True),
    ('giantant04', '侵略生物α 04', False, 0.5, True),
    ('e650_giantant02b', '侵略生物α（红，EDF6）', False, 0.5, True),
    ('goldant', '金色侵略生物α', False, 0.5, False),
    ('e650_giantant_gold', '金色侵略生物α（EDF6）', False, 0.5, False),
    ('e665_giantant_medium', '中型侵略生物α', False, 0.5, False),
    ('giantant01egg', '蚁卵', False, 0.5, False),
    ('e650_giantant_queen', '母体生物（EDF6）', False, 0.5, True),
    ('e650_giantant_queen_gold', '金色母体生物（EDF6）', False, 0.5, False),
    ('goldantqueen', '金色母体生物', False, 0.5, False),
    ('e662_giantant_queen_red', '红色母体生物', False, 0.5, False),
    ('e651_spider01', '侵略生物β', False, 0.5, True),
    ('e651_spider01_silver', '银色侵略生物β（EDF6）', False, 0.5, False),
    ('silverspider', '银色侵略生物β', False, 0.5, False),
    ('e667_spider_medium', '中型侵略生物β', False, 0.5, False),
    ('e663_spider_egg_sac', '蜘蛛卵囊', False, 0.5, False),
    ('giantspiderqueen', '国王', False, 0.5, True),
    ('e651_spider_queen', '国王（EDF6）', False, 0.5, True),
    ('e667_spider_queen', '国王（中型）', False, 0.5, True),
    ('silverspiderqueen', '银色国王', False, 0.5, False),
    ('giantbee01', '飞天型侵略生物（飞）', True, 20.0, True),
    ('giantbee02charge', '飞天型侵略生物 02（飞）', True, 20.0, True),
    ('e668_giantbee_medium', '中型飞天型侵略生物（飞）', True, 20.0, False),
    ('giantbeequeen', '女王（飞）', True, 20.0, True),
    ('giantbeequeen02', '女王 02（飞）', True, 20.0, True),
    ('e668_giantbee_queen', '女王（中型，飞）', True, 20.0, True),
    ('yerrowsmalldragon', '蝌蚪（黄，飞）', True, 30.0, False),
    ('redsmalldragon', '大型蝌蚪（红，飞）', True, 30.0, False),
    ('e604_radon', '飞行恐龙（飞）', True, 40.0, False),
    ('e602_berserker_beama', '机械生命体（光束 A）', False, 0.5, True),
    ('e602_berserker_beamb', '机械生命体（光束 B）', False, 0.5, True),
    ('e602_berserker_ace', '超级机械生命体', False, 0.5, True),
    ('e602_berserker_grapple', '高机动型机械生命体', False, 0.5, True),
    ('e602_berserker_bommer', '掷弹兵', False, 0.5, True),
    ('e602_berserker_middle', '大型机械生命体', False, 0.5, True),
    ('e602_berserker_middle_mg', '大型机械生命体（机枪）', False, 0.5, True),
    ('e602_berserker_middle_punch', '大型机械生命体（拳）', False, 0.5, True),
    ('e602_berserker_middle_spread', '大型机械生命体（散射）', False, 0.5, True),
    ('e602_berserker_middle_bommer', '大型掷弹兵', False, 0.5, True),
    ('e602_berserker_large', '独眼巨人', False, 0.5, True),
    ('e602_berserker_large_ace', '独眼巨人（高阶）', False, 0.5, True),
    ('e602_berserker_large_punch', '独眼巨人（拳）', False, 0.5, True),
    ('e616_venus_gs', '海怪克拉肯 GS（飞）', True, 20.0, False),
    ('e616_venus_ggss', '海怪克拉肯 GGSS（飞）', True, 20.0, False),
    ('e616_venus_llss', '海怪克拉肯 LLSS（飞）', True, 20.0, False),
    ('e616_venus_ccss', '海怪克拉肯 CCSS（飞）', True, 20.0, False),
    ('e664_armor_venus_gggg', '装甲克拉肯 GGGG（飞）', True, 20.0, False),
    ('e664_armor_venus_bb', '装甲克拉肯 BB（飞）', True, 20.0, False),
    ('e664_armor_venus_ww', '装甲克拉肯 WW（飞）', True, 20.0, False),
    ('e618_squid_small', '黑霾（飞）', True, 30.0, False),
    ('e606_shellfish', '涅加', False, 0.5, False),
    ('e606_shellfish_ace', '高阶涅加', False, 0.5, False),
    ('e669_shellfish_medium', '中型涅加', False, 0.5, False),
    ('e669_shellfish_medium_ace', '中型高阶涅加', False, 0.5, False),
    ('e603_merman', '斯库拉', False, 0.5, False),
    ('e514_dango', '侵略生物γ', False, 0.5, False),
    ('e660_heavy_dango_m', '重装侵略生物γ（中）', False, 0.5, False),
    ('e660_heavy_dango_l', '重装侵略生物γ（大）', False, 0.5, False),
    ('e509_deiroi', '迪洛伊', False, 0.5, False),
    ('e513_shieldbearer_l', '防护者 L', False, 0.5, True),
    ('e513_shieldbearer_xl', '防护者 XL', False, 0.5, True),
    ('e507_goldufo_ace', '红色无人机（飞）', True, 30.0, True),
    ('e507_goldufo_big', '大型无人机（飞）', True, 30.0, False),
    ('e515_imperialufo_ace', '帝国无人机（高阶，飞）', True, 30.0, True),
    ('e515_imperialufo_prototype', '第２类无人机（飞）', True, 30.0, True),
    ('e605_spinnerufo_blue', '第３类无人机（蓝，飞）', True, 30.0, True),
    ('e605_spinnerufo_ace', '高阶第３类无人机（飞）', True, 30.0, True),
    ('e605_spinnerufo_blue_ace', '高阶第３类无人机（蓝，飞）', True, 30.0, True),
    ('e505_generator', '传送锚塔', False, 0.5, False),
    ('e505_generator_large', '传送锚塔（大）', False, 0.5, False),
    ('e505_generator_superlarge', '传送锚塔（超大）', False, 0.5, False),
    ('e517_anthill', '传送装置', False, 0.5, False),
    ('e615_hornetnest_small01', '小型土巢', False, 0.5, False),
    ('hornetnest401', '土巢', False, 0.5, False),
    ('e612_anchor', '尾型锚塔', False, 0.5, True),
    ('e666_anchor_small_heavyfire', '小型尾型锚塔（重火力）', False, 0.5, False),
    ('nephila', '厄拉尼亚', False, 0.5, False),
    ('monster501', '艾尔基努斯', False, 0.5, False),
    ('monster501_king', '艾尔基努斯（王）', False, 0.5, False),
    ('e504_monstera', '亚凯鲁斯', False, 0.5, False),
    ('e510_fortress', '前哨基地', False, 0.5, True),
    ('e512_dropboat', '运兵艇（飞）', True, 30.0, False),
    ('e511_mothership', '母舰（EDF5，飞）', True, 900.0, True),
    ('e609_jormungand', '耶梦加得', False, 0.5, False),
    ('e614_grandmother', '圆环（飞）', True, 300.0, False),
)

# EDF4.1's objects by class: the panel's category and words, how far up it is made, whether it flies.
EDF41_CLASSES: dict[str, tuple[str, str, float, bool]] = {
    'Vehicle601_Tank': ('vehicle', '4.1 坦克', 0.5, False),
    'Vehicle_Car': ('vehicle', '4.1 边三轮摩托', 0.5, False),
    # Empty vehicles wait on the ground for boarding; a helicopter is not an airborne enemy wave.
    'Vehicle506_Helicopter': ('vehicle', '4.1 直升机 Nereid', 0.5, False),
    'VehicleBegaruta': ('vehicle', '4.1 步行机 Begaruta', 0.5, False),
    'Vehicle501_FortressRobo': ('vehicle', '4.1 巨型机甲 Balam', 0.5, False),
    'AntHill': ('enemy41', '4.1 蚁巢', 0.5, False),
    'BigNest': ('enemy41', '4.1 巨型巢穴', 0.5, False),
    'Deiroi': ('enemy41', '4.1 迪洛伊', 0.5, False),
    'AlienTrailer': ('enemy41', '4.1 护盾运载者', 0.5, False),   # EDF6's E513 shield bearer is this class too
    'EDF6_Berserker_Large': ('enemy41', '4.1 赫克托', 0.5, False),
    'MovingFortress': ('enemy41', '4.1 四足步行要塞', 0.5, False),
    'GiantAnt': ('enemy41', '4.1 巨蚁', 0.5, False),
    'GiantBee': ('enemy41', '4.1 巨蜂女王（飞）', 20.0, True),
    'Nephila': ('enemy41', '4.1 巨型蜘蛛', 0.5, False),
    'DragonSmall': ('enemy41', '4.1 龙（飞）', 30.0, True),
    'UfoSmall515': ('enemy41', '4.1 UFO（飞）', 40.0, True),
    'UfoSmall507': ('enemy41', '4.1 UFO（飞）', 40.0, True),
    'UfoCarrier508': ('enemy41', '4.1 运输船（飞）', 150.0, True),
    'Transporter_Formation': ('enemy41', '4.1 运输船编队（飞）', 150.0, True),
    'UfoRobo': ('enemy41', '4.1 UFO 机器人（飞船 / 机器人形态）', 150.0, True),
    'UfoMother301': ('enemy41', '4.1 母船（飞）', 300.0, True),
    'UfoMother401': ('enemy41', '4.1 地球吞噬者（飞）', 300.0, True),
}
# File identities distinguish 4.1's dragons from EDF5/6's frog dragons (the same native DragonSmall class).
EDF41_DRAGON_LABELS: dict[str, str] = {
    'EDF41_DRAGONSMALL401': '4.1 小龙（绿，飞）',
    'EDF41_REDSMALLDRAGON': '4.1 小龙（红，飞）',
    'EDF41_YERROWSMALLDRAGON': '4.1 小龙（黄，飞）',
    'EDF41MP01/DRAGONSMALL401_HS': '4.1 DLC 小龙（绿，飞）',
    'EDF41MP01/REDSMALLDRAGON_HS': '4.1 DLC 小龙（红，飞）',
    'EDF41MP01/YERROWSMALLDRAGON_HS': '4.1 DLC 小龙（黄，飞）',
    'DRAGONBIG401': '4.1 大龙（飞）',
}
# Registry objects that are no row, and why (selftest: every other one is).
EDF41_LEFT_OUT_CLASSES: dict[str, str] = {
    'AssultSoldier': 'a friendly 4.1 soldier: the plugin makes soldiers only as its support soldiers (support_soldier.cpp), '
                     'a script soldier needs its squad set-up',
    'HeavyArmor': 'a friendly 4.1 Fencer soldier: as AssultSoldier',
    'PaleWing': 'a friendly 4.1 Wing Diver soldier: as AssultSoldier',
    'SpiderNet': 'a web fitted to the 4.1 map it was made for (_FIT): no object to stand on open ground',
}
# A 4.1 mission's own copy of an object (same model, the mission's tweaks): its base object is the row.
EDF41_MISSION_VARIANT = re.compile(r'(M\d{3}|_DLC_MP?\d{3})$')
# The 4.1 vehicles' NPC-crewed twin when an empty one is a row too.
EDF41_AI_TWIN = re.compile(r'_AI$')


def _edf41_registry() -> dict:
    with open(EDF41_REGISTRY, encoding='utf-8') as f:
        return json.load(f)


def edf41_left_out(obj: dict) -> str | None:
    """Why the registry's object `obj` is no row (None: it is one)."""
    stem = obj['object'].removeprefix('OBJECT/').removesuffix('.SGO')
    if obj['class'] in EDF41_LEFT_OUT_CLASSES:
        return EDF41_LEFT_OUT_CLASSES[obj['class']]
    if EDF41_MISSION_VARIANT.search(stem.split('/')[-1]):
        return 'a 4.1 mission\'s own variant of an object that is a row'
    names = {o['object'] for o in _edf41_registry()['objects']}
    if EDF41_AI_TWIN.search(stem) and obj['object'].replace('_AI.SGO', '.SGO') in names:
        return 'the NPC-crewed twin of a vehicle that is a row (placed empty)'
    if obj['class'] not in EDF41_CLASSES:
        return None   # a new class: selftest asks for it to be named
    return None


@lru_cache(maxsize=None)
def edf41_rows() -> tuple[Row, ...]:
    """The 4.1 rows, most used in 4.1's missions first within a class. The plugin preloads by default the first of each
    class and any other whose model archives (MODEL_ARCHIVES) the rows before it bring already (a variant on the same
    model costs no more memory: a ragdoll SHKT or a CAS of its own is no model). All ported vehicles are preloaded; the
    other enemies load the first time they are placed (src/debug_spawn.cpp RequestLoad; ini RangePlaceAll=1 preloads
    every row at the mission's start)."""
    reg = _edf41_registry()
    objects = [o for o in reg['objects'] if edf41_left_out(o) is None and o['class'] in EDF41_CLASSES]
    order = {c: i for i, c in enumerate(EDF41_CLASSES)}
    objects.sort(key=lambda o: (order[o['class']], -int(o['missions']), o['object']))
    rows: list[Row] = []
    brought: set[str] = set()
    first: set[str] = set()
    for o in objects:
        category, words, lift, flying = EDF41_CLASSES[o['class']]
        stem = o['object'].removeprefix('OBJECT/').removesuffix('.SGO')
        words = EDF41_DRAGON_LABELS.get(stem, words)
        models = {f for f in o['files'] if f.upper().endswith(MODEL_ARCHIVES)}
        preload = category == 'vehicle' or o['class'] not in first or models <= brought
        first.add(o['class'])
        if preload:
            brought |= {f for f in o['files'] if f.upper().endswith(MODEL_ARCHIVES)}
        spawn = o.get('spawn_object')
        spawn = spawn.removeprefix('OBJECT/').removesuffix('.SGO').lower() if spawn else None
        rows.append(Row(stem.lower(), f'{words} {stem.split("/")[-1]}', category, 'edf41', lift, flying, preload, spawn))
    return tuple(rows)


def edf41_files(sgo: str) -> list[str]:
    """The Mods files a 4.1 row needs (its registry entry's)."""
    rel = f'OBJECT/{sgo.upper()}.SGO'
    return next(o['files'] for o in _edf41_registry()['objects']
                if o['object'] == rel or o.get('spawn_object') == rel)


@lru_cache(maxsize=None)
def edf5dlc_rows() -> tuple[Row, ...]:
    """EDF5 Mission Pack 1's objects (tools/edf5_dlc.py groups of kind 'object'): the DLC fortress."""
    with open(EDF5_DLC_REGISTRY, encoding='utf-8') as f:
        groups = json.load(f)['groups']
    out = []
    for name, g in groups.items():
        if g.get('kind') != 'object':
            continue
        stem = name.removeprefix('OBJECT/').removesuffix('.SGO')
        out.append(Row(stem.lower(), f'EDF5 DLC 要塞 {stem}', 'enemy', 'edf5dlc', 0.5, False, True))
    return tuple(out)


def edf5dlc_files(sgo: str) -> list[str]:
    """The Mods files of an EDF5 DLC object: its group's own. Its `needs` are Root.cpk's (the stock fortress's missile and
    debris models, E509 / E510): tools/edf5_dlc.py checks them in the player's game when it installs; asking for them
    under Mods kept the row 「未安装」 for good (2026-10-19)."""
    with open(EDF5_DLC_REGISTRY, encoding='utf-8') as f:
        g = json.load(f)['groups'][f'OBJECT/{sgo.upper()}.SGO']
    return list(g['files'])


# The installer's vehicles placeable as they are (their SGO keeps the mission_setup of the stock SGO it is made from):
# (sgo, label, the files of it in Mods). The sedan (tools/make_sedan.py: V512_KEITRUCK's SGO on the city car's model).
INSTALLER_VEHICLES: tuple[tuple[str, str, tuple[str, ...]], ...] = (
    ('edf6vc_sedan', '民用轿车', ('OBJECT/EDF6VC_SEDAN.SGO', 'OBJECT/EDF6VC_SEDAN.MRAB', 'OBJECT/EDF6VC_SEDAN.SHKT')),
)
# ... and the X-Morph cars the same way (pylib/xm_units.py car: pylib/car_model.py keeps the V512's mission_setup).
INSTALLER_VEHICLES += tuple((f'edf6vc_{u.key}', xm_units.label(u),
                             tuple(f'OBJECT/EDF6VC_{u.key.upper()}.{ext}' for ext in ('SGO', 'MRAB', 'SHKT')))
                            for u in xm_units.v512())


def short(label: str) -> str:
    """A range label without its notes in brackets (the panel's row: '直升机 506 Eros（测试场生成）' -> '直升机 506 Eros')."""
    return label.split('（')[0].strip()


def rows() -> list[Row]:
    """Every row, in the panel's order (category, then the registries' order). Imports the range's own module for its
    vehicles (gen.VEHICLES, gen.PLACEABLE_DERIVED)."""
    import gen
    out = [Row(s, label, 'vehicle', 'stock') for s, label in STOCK_VEHICLES]
    out += [r for r in edf41_rows() if r.category == 'vehicle']
    labels = dict(gen.VEHICLES)
    out += [Row(s, short(labels[s]), 'ours', 'range') for s in gen.PLACEABLE_DERIVED]
    out += [Row(s, label, 'ours', 'installer') for s, label, _ in INSTALLER_VEHICLES]
    out += [Row(s, label, 'enemy', 'stock', lift, flying, preload) for s, label, flying, lift, preload in STOCK_ENEMIES]
    out += list(edf5dlc_rows())
    out += [r for r in edf41_rows() if r.category == 'enemy41']
    return out


def files_of(row: Row) -> list[str]:
    """The Mods files a non-stock row needs (a 'range' row's SGO is the range's own: install writes it)."""
    if row.source == 'edf41':
        return edf41_files(row.sgo)
    if row.source == 'edf5dlc':
        return edf5dlc_files(row.sgo)
    if row.source == 'installer':
        return list(next(f for s, _, f in INSTALLER_VEHICLES if s == row.sgo))
    if row.source == 'range':
        import gen
        return list(gen.range_files(row.sgo))
    return []


def available(game_root: str, row: Row) -> bool:
    """Whether `row`'s files are in this game's Mods (a stock row: always; a range row: written by the range's install)."""
    if row.source == 'stock':
        return True
    return all(os.path.isfile(os.path.join(game_root, 'Mods', *rel.split('/'))) for rel in files_of(row))
