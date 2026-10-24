# EDF6 敌方家族 / 变种 / 技能来源静态普查（2026-10-19）

只读侦察；没有修改生产源码 / CMake，没有编译、运行游戏或写游戏目录。分类是资源语义候选，不代表已实现玩家操控。技能字段只证明游戏有该数据，不证明某个统一指令能触发它。

## 数据源与边界

- 工作树：`D:/APP/edf6-wt-1018-feedback-combat`；先读该目录 `CLAUDE.md`。
- 真实游戏：`D:/steam/steamapps/common/EARTH DEFENSE FORCE 6/Root.cpk`，使用本仓 `pylib/rootcpk.py` + `pylib/sgo.py` 只读解析。
- Root.cpk `OBJECT` 下 718 个 .SGO 全部成功解析；692 个带类，共 139 类；26 个无类，全部为 `RAGDOLL_*.SGO` 动画/物理映射。
- `Chunk01.cpk` / `Chunk02.cpk` / `DX11.cpk` 的 OBJECT 目录没有 .SGO（只读 CPK 索引普查）。
- 4.1：`edf41port/objects.json` 123 行，全数通过 `tools/edf41_objects.py.read` 实际解析配套 SGO；不是按游戏记忆列名称。
- EDF5 DLC：`edf5port/dlc/registry.json` 另有 `OBJECT/E510_DLC_FORTRESS.SGO`，属于 MovingFortress 同家族的移植变种。
- 原始全文 JSON：`C:/Users/wrds/AppData/Local/Temp/edf6-enemy-family-inventory-20261019.json`。

## 覆盖边界与确定的盲点

1. `testrange/catalog.py:94-118` STOCK_ENEMIES 只有 21 行，不能当全敌方控制白名单；它漏大量整族和几乎所有技能变种。
2. DropBoat512 = `OBJECT/E512_DROPBOAT.SGO` 是敌方新型传送船；应与 UfoCarrier508 同列候选，不能与我方 Transporter 混淆。
3. `testrange/catalog.py:142` 把 `Transporter_Formation` 放进 enemy41；`pylib/edf41friends.py:79-89` 明确是友军投送编队，table-0 natives 1022/1023。不要按菜单分类把它作为敌人。
4. 原版有六类 Berserker；中型 PUNCH 与常规头炮用不同 AttackSetting，同类名不能当同一技能表。
5. Martian / Venus 的 `humanoid_base_weapon_ex.list` 有四个独立武器 / 盾槽，不能用一个主攻击代替所有手臂；变种的槽内 SGO 不同。
6. UfoMother511 / MovingFortress / GrandMother / Jormungand 是主对象与炮台/弱点/门/环/身体部件组成；只挪主矩阵或只给主对象一发子弹不能满足全部原生技能。
7. 4.1 registry 中 UfoMother401 的 16 个任务变体被召唤目录主动略去；全敌方控制不能继承该去重丢覆盖。UfoMother301 / UfoMother401 / UfoRobo 是插件注册的原生缺失类，不能只扫 EDF.dll 自带 RTTI。
8. 建筑、非角色卵网囊、依附部件须显式排除并给理由；特别 Capsule / 卵囊并非已证实的移动角色，也不应悄悄归成建筑。

## Root.cpk 原版敌方根候选：38 类 / 191 SGO

下表所有文件路径均为上列 Root.cpk 内 `OBJECT/<文件名>.SGO`。保留任务 / 轻量 / 网络同步 / ACE 变种，没有按外观去重。

| 原生类 | 数量 | 全部 SGO（省略 .SGO） | 技能 / 武器 / 部件定义字段（该类各变种并集） |
|---|---:|---|---|
| `GiantAnt` | 17 | `E650_GIANTANT01`, `E650_GIANTANT01_NETLIGHTSYNCMODE`, `E650_GIANTANT02B`, `E650_GIANTANT03B`, `E650_GIANTANT_GOLD`, `E650_GIANTANT_QUEEN`, `E650_GIANTANT_QUEEN_GOLD`, `GIANTANT01`, `GIANTANT01B`, `GIANTANT01NETLIGHTSYNCMODE`, `GIANTANT02B`, `GIANTANT03B`, `GIANTANT04`, `GIANTANT04SB`, `GIANTANTQUEEN`, `GOLDANT`, `GOLDANTQUEEN` | `ant_Bullet`, `ant_BulletDamageReduce`, `ant_BulletDamageReducePow`, `ant_BulletParam`, `ant_BulletType`, `ant_CaptureSe`, `ant_ex_AlwaysPartsBreak`, `ant_ex_CaptureAnimationParam` |
| `GiantAntEX` | 2 | `E662_GIANTANT_QUEEN_RED`, `E665_GIANTANT_MEDIUM` | `ant_Bullet`, `ant_BulletParam`, `ant_BulletType`, `ant_CaptureSe`, `ant_ex_CaptureAnimationParam`, `gun_battery_list` |
| `GiantSpider` | 10 | `E651_SPIDER01`, `E651_SPIDER01_LIGHT`, `E651_SPIDER01_NETLIGHTSYNCMODE`, `E651_SPIDER01_SILVER`, `E651_SPIDER_QUEEN`, `GIANTSPIDER01`, `GIANTSPIDER01NETLIGHTSYNCMODE`, `GIANTSPIDERQUEEN`, `SILVERSPIDER`, `SILVERSPIDERQUEEN` | `bullet_custom_param`, `bullet_factory` |
| `GiantSpiderEX` | 2 | `E667_SPIDER_MEDIUM`, `E667_SPIDER_QUEEN` | `bullet_custom_param`, `bullet_factory`, `gun_battery_list` |
| `GiantBee` | 5 | `GIANTBEE01`, `GIANTBEE01CHARGE`, `GIANTBEE02CHARGE`, `GIANTBEEQUEEN`, `GIANTBEEQUEEN02` | `custom_bullet_param` |
| `GiantBeeEX` | 2 | `E668_GIANTBEE_MEDIUM`, `E668_GIANTBEE_QUEEN` | `custom_bullet_param`, `gun_battery_list` |
| `GiantDango` | 1 | `E514_DANGO` | `attack_blow_off_return`, `dango_Attack`, `dango_RollingSetting`, `dango_attack_time`, `rolling_se` |
| `EDF6_GiantDangoLarge` | 2 | `E660_HEAVY_DANGO_L`, `E660_HEAVY_DANGO_M` | `EDF6_GiantDangoLarge.attack`, `EDF6_GiantDangoLarge.gun_battery`, `bullet_param`, `charge_param`, `fire_param`, `gun_battery_setting` |
| `Nephila` | 1 | `NEPHILA` | `bullet_custom_param` |
| `DragonSmall` | 6 | `DRAGONSMALL401`, `DRAGONSMALL401_HS`, `REDSMALLDRAGON`, `REDSMALLDRAGON_HS`, `YERROWSMALLDRAGON`, `YERROWSMALLDRAGON_HS` | `dragom_AttackCaptureSetting`, `dragon_AttackBulletSetting` |
| `Humanoid_Basic` | 34 | `E503_ARMORFROG_AF`, `E503_ARMORFROG_AF_LEADER`, `E503_ARMORFROG_LC`, `E503_ARMORFROG_LC_LEADER`, `E503_ARMORFROG_SG`, `E503_ARMORFROG_SG_LEADER`, `E503_FROG_AF`, `E503_FROG_AF_LEADER`, `E503_FROG_LC`, `E503_FROG_LC_LEADER`, `E503_FROG_SG`, `E503_FROG_SG_LEADER`, `E506_BIGGREY_AF`, `E506_BIGGREY_AF_LEADER`, `E506_BIGGREY_LL`, `E506_BIGGREY_LL_LEADER`, `E506_BIGGREY_SG`, `E506_BIGGREY_SG_LEADER`, `E506_HA_BIGGREY_FL`, `E506_HA_BIGGREY_FL_LEADER`, `E506_HA_BIGGREY_HMG`, `E506_HA_BIGGREY_HMG_LEADER`, `E506_HA_BIGGREY_LL`, `E506_HA_BIGGREY_LL_LEADER`, `E506_HA_BIGGREY_ML`, `E506_HA_BIGGREY_ML_LEADER`, `E506_NUDE_BIGGREY_AF`, `E506_NUDE_BIGGREY_LL`, `E506_NUDE_BIGGREY_SG`, `E506_NUDE_HA_BIGGREY_FL`, `E506_NUDE_HA_BIGGREY_HMG`, `E506_NUDE_HA_BIGGREY_ML`, `E617_WILDFROG_G`, `E617_WILDFROG_G2` | `explosion_parts_damage_adjust`, `humanoid_base_parts_config`, `humanoid_base_remove_parts`, `weapon`, `weapon_constraint_parts`, `weapon_node` |
| `EDF6_Martian` | 16 | `E601_MARTIAN_GG`, `E601_MARTIAN_GGSS`, `E601_MARTIAN_GS`, `E601_MARTIAN_LL`, `E601_MARTIAN_LLLL`, `E601_MARTIAN_LLSS`, `E601_MARTIAN_LS`, `E601_MARTIAN_N`, `E601_MARTIAN_NN`, `E601_MARTIAN_NS`, `E601_MARTIAN_SSSS`, `E601_MARTIAN_WS`, `E601_MARTIAN_WWSS`, `E661_MARTIAN_LEADER_AASS`, `E661_MARTIAN_LEADER_CS`, `E661_MARTIAN_LEADER_GGSS` | `_shield_arm_revive_time`, `_weapon_arm_revive_time`, `humanoid_base_parts_config`, `humanoid_base_weapon_ex`, `no_weapon_base_pose` |
| `EDF6_Venus` | 8 | `E616_VENUS_CCSS`, `E616_VENUS_GG`, `E616_VENUS_GGSS`, `E616_VENUS_GS`, `E616_VENUS_LLSS`, `E664_ARMOR_VENUS_BB`, `E664_ARMOR_VENUS_GGGG`, `E664_ARMOR_VENUS_WW` | `_shield_arm_revive_time`, `_weapon_arm_revive_time`, `humanoid_base_parts_config`, `humanoid_base_weapon_ex`, `no_weapon_base_pose` |
| `EDF6_Merman` | 3 | `E603_MERMAN`, `E603_MERMAN_LIGHT`, `E603_MERMAN_M063` | `EDF6_Merman.Action`, `EDF6_Merman.death_reaction`, `EDF6_Merman.gun_battery`, `EDF6_Merman.poison_mist`, `gun_battery_setting`, `poison_mist_gun_battery_setting` |
| `EDF6_ShellFish` | 4 | `E606_SHELLFISH`, `E606_SHELLFISH_ACE`, `E669_SHELLFISH_MEDIUM`, `E669_SHELLFISH_MEDIUM_ACE` | `ShellFishSettings.damage_reaction_scale`, `ShellFishSettings.gun_battery`, `gun_battery_setting` |
| `EDF6_SquidSmall` | 5 | `E618_SQUID_SMALL`, `E618_SQUID_SMALL_HI`, `E618_SQUID_SMALL_HI_NOSMOKE`, `E618_SQUID_SMALL_NOSMOKE`, `E618_SQUID_SMALL_SSMOKE` | `EDF6_SquidSmallSettings.attack` |
| `EDF6_Berserker_TypeA` | 4 | `E602_BERSERKER`, `E602_BERSERKERUG`, `E602_BERSERKER_ACE`, `E602_BERSERKER_ST` | `BerserkerSettings.AttackSetting`, `BerserkerSettings.DamageReactionAdjust`, `BerserkerSettings.PartsShootList`, `ext_bullet_setting` |
| `EDF6_Berserker_TypeBommer` | 3 | `E602_BERSERKER_BOMMER`, `E602_BERSERKER_BOMMER_ST`, `E602_BERSERKER_MIDDLE_BOMMER` | `BerserkerSettings.AttackSetting`, `BerserkerSettings.DamageReactionAdjust`, `BerserkerSettings.PartsShootList` |
| `EDF6_Berserker_TypeGrapple` | 1 | `E602_BERSERKER_GRAPPLE` | `BerserkerSettings.AttackSetting`, `BerserkerSettings.DamageReactionAdjust`, `BerserkerSettings.PartsShootList`, `BerserkerSettings.grapple`, `ext_bullet_setting` |
| `EDF6_Berserker_TypeShooter` | 4 | `E602_BERSERKERUG_BEAMA`, `E602_BERSERKERUG_BEAMB`, `E602_BERSERKER_BEAMA`, `E602_BERSERKER_BEAMB` | `BerserkerSettings.AttackSetting`, `BerserkerSettings.DamageReactionAdjust`, `BerserkerSettings.PartsShootList`, `BerserkerSettings.ShooterSettings`, `gun_battery_settings` |
| `EDF6_Berserker_Middle` | 7 | `E602_BERSERKER_MIDDLE`, `E602_BERSERKER_MIDDLEUG`, `E602_BERSERKER_MIDDLEUG_MG`, `E602_BERSERKER_MIDDLEUG_PUNCH`, `E602_BERSERKER_MIDDLE_MG`, `E602_BERSERKER_MIDDLE_PUNCH`, `E602_BERSERKER_MIDDLE_SPREAD` | `BerserkerSettings.AttackSetting`, `BerserkerSettings.DamageReactionAdjust`, `BerserkerSettings.PartsShootList`, `ext_bullet_setting`, `gun_battery_settings` |
| `EDF6_Berserker_Large` | 4 | `E602_BERSERKER_LARGE`, `E602_BERSERKER_LARGE_ACE`, `E602_BERSERKER_LARGE_LITE`, `E602_BERSERKER_LARGE_PUNCH` | `BerserkerSettings.AttackSetting`, `BerserkerSettings.DamageReactionAdjust`, `BerserkerSettings.PartsShootList`, `ext_bullet_setting`, `gun_battery_aiming`, `gun_battery_bullet_settings`, `gun_battery_charge`, `gun_battery_fire_settings`, `gun_control` |
| `EDF6_FlyingDyno` | 4 | `E604_RADON`, `E604_RADON_M051`, `E604_RADON_M118_5`, `E604_RADON_RM034` | `EDF6_FlyingDyno.action`, `EDF6_FlyingDyno.form2_passive`, `EDF6_FlyingDyno.middle_damage_reaction`, `EDF6_FlyingDyno.shooting_list`, `EDF6_FlyingDyno.small_damage_reaction`, `fire_ball_gun_battery`, `flame_gun_battery`, `flame_vertical_gun_battery`, `flapping_wind_pressure_gun_battery`, `form2_flame_gun_battery`, `form2_flame_vertical_gun_battery`, `form2_small_fire_gun_battery`, `wind_pressure_gun_battery` |
| `Monster501` | 2 | `MONSTER501`, `MONSTER501_KING` | `BulletCustomParameter`, `BulletFactory`, `ai_attack_pattern`, `attack_setting` |
| `Monster504` | 2 | `E504_MONSTERA`, `E504_MONSTERA_M034_3` | `ai_attack_pattern`, `attack_setting`, `bullet_color`, `bullet_factory`, `bullet_level_adjust`, `bullet_model`, `bullet_muzzle_flash`, `bullet_pattern_list`, `bullet_pattern_mine`, `bullet_pattern_mine2`, `bullet_pattern_rolling`, `bullet_pattern_snipe`, `muzzle_group_setting` |
| `UfoSmall507` | 5 | `E507_GOLDUFO`, `E507_GOLDUFO_ACE`, `E507_GOLDUFO_BIG`, `E507_GOLDUFO_CHARGE`, `E507_GOLDUFO_HI` | `ai_attack_pattern`, `bullet_big_bomber_info`, `bullet_class`, `bullet_custom_param`, `bullet_info`, `bullet_pattern` |
| `UfoSmall515` | 3 | `E515_IMPERIALUFO`, `E515_IMPERIALUFO_ACE`, `E515_IMPERIALUFO_PROTOTYPE` | `battery_config`, `bullet_object`, `ufo_muzzle` |
| `EDF6_SpinnerUfo` | 4 | `E605_SPINNERUFO`, `E605_SPINNERUFO_ACE`, `E605_SPINNERUFO_BLUE`, `E605_SPINNERUFO_BLUE_ACE` | `_bullet_adjust`, `_bullet_color`, `_bullet_effect`, `bullet_param`, `charge_param`, `fire_param`, `gun_battery_setting` |
| `Deiroi` | 8 | `E509_DEIROI`, `E509_DEIROI_L`, `E509_DEIROI_L2`, `E509_DEIROI_M`, `E509_DEIROI_S`, `E509_DEIROI_SM`, `E509_DEIROI_XL`, `E509_DEIROI_XL2` | `bullet_object`, `charge_param`, `deiroi_battery_config`, `parts_config` |
| `AlienTrailer` | 6 | `E513_SHIELDBEARER`, `E513_SHIELDBEARER_FIXED`, `E513_SHIELDBEARER_FIXEDL`, `E513_SHIELDBEARER_FIXEDXL`, `E513_SHIELDBEARER_L`, `E513_SHIELDBEARER_XL` |  |
| `MovingFortress` | 2 | `E510_FORTRESS`, `E510_FORTRESS_M055` | `core_hatch_hide_interval`, `core_hatch_interval`, `core_parts_name`, `generators`, `parts_setting` |
| `UfoCarrier508` | 3 | `E508_CARRIER`, `E508_CARRIER_FAR`, `E508_CARRIER_FAR_LIGHT` | `UfoCarrier_CorePartsName` |
| `DropBoat512` | 1 | `E512_DROPBOAT` |  |
| `UfoMother511` | 4 | `E511_MOTHERSHIP`, `E511_MOTHERSHIP_DLC`, `E511_MOTHERSHIP_EDF6`, `E511_MOTHERSHIP_LIGHT` | `UfoMother511WeaponBeam0`, `UfoMother511WeaponBeam1`, `UfoMother511WeaponBeam2`, `UfoMother511WeaponBeam3`, `UfoMother511WeaponLaser0`, `UfoMother511WeaponLaser1`, `UfoMother511WeaponLaser1_dlc`, `UfoMother511WeaponLaser2`, `UfoMother511WeaponLaser3`, `UfoMother511WeaponPlasma0`, `UfoMother511WeaponPlasma1`, `core_parts`, `edf6_extra_parts`, `equipped_parts_barrier_generator`, `equipped_parts_list`, `equipped_parts_none`, `equipped_parts_object_generator`, `hatch_auto_open_close`, `ring_unit_parts_info_list` |
| `EDF6_TimeShip` | 3 | `E611_TIMESHIP`, `E611_TIMESHIP_FULL_LIGHT`, `E611_TIMESHIP_LIGHT` | `EDF6_TimeShip.Anchor`, `EDF6_TimeShip.Capsule` |
| `EDF6_GrandMother` | 1 | `E614_GRANDMOTHER` | `edf6_grand_mother_settings.default_layout_parts`, `edf6_grand_mother_settings.generator` |
| `EDF6_Jormungand` | 1 | `E609_JORMUNGAND` | `jormungand_config.parts` |
| `Humanoid_BigGreyBoss` | 1 | `E516_BIGGREYBOSS` | `generator`, `humanoid_base_parts_config`, `no_weapon_base_pose`, `parts` |


## 已命名攻击 / 动作表

只列 SGO 明写的攻击/动作标识（跨变种并集）；其余技能由上表字段和相应原生状态机定义，不能据本表空白认定无技能。

- `EDF6_Merman`：`attack_bothHand`, `attack_dive`, `attack_roll_crawl_l`, `attack_roll_crawl_r`, `attack_splash`, `both_hand`, `crawl_attack_dive_crawl`, `crawl_attack_flip_skittishly`, `dive`, `hopping`, `roll_l`, `roll_r`, `sliding`, `splash`
- `EDF6_Berserker_TypeA`：`attack_shoot_both`, `attack_shoot_both_end`, `attack_shoot_lr`, `attack_shoot_lr_end`, `attack_shoot_rl`, `attack_shoot_rl_end`
- `EDF6_Berserker_TypeBommer`：`attack_suicideBomb_banzai`, `attack_suicideBomb_try`
- `EDF6_Berserker_TypeGrapple`：`attack_shoot_both`, `attack_shoot_both_end`, `attack_shoot_lr`, `attack_shoot_lr_end`, `attack_shoot_rl`, `attack_shoot_rl_end`
- `EDF6_Berserker_TypeShooter`：`attack_head_beam`
- `EDF6_Berserker_Middle`：`attack_head_cannon`, `attack_shoot_both`, `attack_shoot_both_end`, `attack_shoot_lr`, `attack_shoot_lr_end`, `attack_shoot_rl`, `attack_shoot_rl_end`
- `EDF6_Berserker_Large`：`attack_armStomp_both`, `attack_armStomp_l`, `attack_armStomp_r`, `attack_shoot_both`, `attack_shoot_both_end`, `attack_shoot_lr`, `attack_shoot_lr_end`, `attack_shoot_rl`, `attack_shoot_rl_end`
- `EDF6_FlyingDyno`：`attack_dive_end`, `attack_dive_loop`, `attack_dive_start`, `attack_fire_ball`, `attack_fire_horizon`, `attack_fire_horizon_reverse`, `attack_fire_vertical`, `attack_form2_fire_horizon`, `attack_form2_fire_horizon_reverse`, `attack_form2_fire_vertical`, `dive`, `fire_ball`, `fly_step_l`, `fly_step_r`, `form2_horizontal_flame`, `form2_horizontal_flame_reverse`, `form2_vertical_flame`, `horizontal_flame`, `horizontal_flame_reverse`, `vertical_flame`
- `Monster501`：`attack_breath_b`, `attack_breath_t`, `attack_chop_l`, `attack_chop_r`, `attack_dash`, `attack_dash2`, `attack_ground`, `attack_ground_l`, `attack_ground_r`, `attack_spring`, `attack_step_chop_l`, `attack_step_chop_r`, `attack_step_chop_w`, `attack_tackle`, `attack_tackle_long`, `attack_tail`
- `Monster504`：`attack_bite`, `attack_both_hand_stomp`, `attack_ground_l`, `attack_ground_r`, `attack_jump`, `attack_nearPress`, `attack_parm`, `attack_rolling`, `attack_rolling2`, `attack_rolling2_thorn`, `attack_rolling2_thorn_vs_big`, `attack_rolling2_vs_big`, `attack_rolling_b`, `attack_rolling_b_vs_big`, `attack_rolling_l`, `attack_rolling_l_vs_big`, `attack_rolling_r`, `attack_rolling_r_vs_big`, `attack_rolling_thorn`, `attack_rolling_thorn_vs_big`, `attack_rolling_vs_big`, `attack_side_spark_l`, `attack_side_spark_l_vs_big`, `attack_side_spark_r`, `attack_side_spark_r_vs_big`, `attack_sniperSpark`, `attack_sniperSpark_vs_big`, `attack_spark`, `attack_spark_vs_big`, `attack_step_swing_l`, `attack_step_swing_r`, `attack_swing_l`, `attack_swing_r`, `attack_tackle`


## 4.1 新增 / 覆盖敌方根候选：14 类 / 73 SGO

下表为 `edf41port/objects.json` 的注册路径；不是仅 catalog 选择出的默认预载行。Root.cpk 自带的旧系敌人已列在原版表。

| 类 | 数量 | 全部注册 SGO | 技能 / 部件字段 |
|---|---:|---|---|
| `AlienTrailer` | 5 | `OBJECT/ALIENTRAILER401.SGO`, `OBJECT/ALIENTRAILER401_L.SGO`, `OBJECT/ALIENTRAILER401_FIXED.SGO`, `OBJECT/ALIENTRAILER401_FIXEDXL.SGO`, `OBJECT/ALIENTRAILER401_XL.SGO` |  |
| `Deiroi` | 5 | `OBJECT/DEIROI401.SGO`, `OBJECT/DEIROI401S.SGO`, `OBJECT/DEIROI401_2.SGO`, `OBJECT/DEIROI401L.SGO`, `OBJECT/DEIROI401_3.SGO` | `bullet_custom_param`, `bullet_factory`, `bullet_object`, `charge_param`, `deiroi_battery_config`, `parts_config` |
| `DragonSmall` | 7 | `OBJECT/DRAGONBIG401.SGO`, `OBJECT/EDF41/DRAGONSMALL401.SGO`, `OBJECT/EDF41/REDSMALLDRAGON.SGO`, `OBJECT/EDF41/YERROWSMALLDRAGON.SGO`, `OBJECT/EDF41MP01/DRAGONSMALL401_HS.SGO`, `OBJECT/EDF41MP01/REDSMALLDRAGON_HS.SGO`, `OBJECT/EDF41MP01/YERROWSMALLDRAGON_HS.SGO` | `dragom_AttackCaptureSetting`, `dragon_AttackBulletSetting`, `dragon_flame_bullet_class`, `dragon_flame_bullet_param` |
| `EDF6_Berserker_Large` | 21 | `OBJECT/HECTOR301CC.SGO`, `OBJECT/HECTOR301CG.SGO`, `OBJECT/HECTOR301CL.SGO`, `OBJECT/HECTOR301GC.SGO`, `OBJECT/HECTOR301GG.SGO`, `OBJECT/HECTOR301GL.SGO`, `OBJECT/HECTOR301LC.SGO`, `OBJECT/HECTOR301LG.SGO`, `OBJECT/HECTOR301LL.SGO`, `OBJECT/HECTOR401CC.SGO`, `OBJECT/HECTOR401CG.SGO`, `OBJECT/HECTOR401CS.SGO`, `OBJECT/HECTOR401GG.SGO`, `OBJECT/HECTOR401GL.SGO`, `OBJECT/HECTOR401LC.SGO`, `OBJECT/HECTOR401LG.SGO`, `OBJECT/HECTOR401LL.SGO`, `OBJECT/HECTOR401LS.SGO`, `OBJECT/HECTOR401SC.SGO`, `OBJECT/HECTOR401SG.SGO`, `OBJECT/HECTOR401SS.SGO` | `BerserkerSettings.AttackSetting`, `BerserkerSettings.DamageReactionAdjust`, `gun_battery_aiming`, `gun_battery_bullet_settings`, `gun_battery_charge`, `gun_battery_fire_settings`, `gun_control` |
| `GiantAnt` | 4 | `OBJECT/EDF41MP01/GIANTANT04.SGO`, `OBJECT/EDF41MP01/GIANTANT04SB.SGO`, `OBJECT/EDF41MP02/GIANTANT04.SGO`, `OBJECT/EDF41MP02/GIANTANT04SBMP02.SGO` | `ant_CaptureSe`, `ant_ex_AlwaysPartsBreak`, `ant_ex_CaptureAnimationParam` |
| `GiantBee` | 2 | `OBJECT/EDF41MP01/GIANTBEEQUEEN02.SGO`, `OBJECT/EDF41MP02/GIANTBEEQUEEN02.SGO` | `custom_bullet_param` |
| `MovingFortress` | 1 | `OBJECT/ALIEN4LEGTANK301.SGO` | `core_hatch_hide_interval`, `core_hatch_interval`, `core_parts_name`, `generators`, `parts_setting`, `shield` |
| `Nephila` | 3 | `OBJECT/EDF41MP01/NEPHILA02L.SGO`, `OBJECT/EDF41MP02/NEPHILA02L.SGO`, `OBJECT/EDF41MP02/NEPHILA03L.SGO` | `bullet_custom_param`, `nephila_ex_attack_param` |
| `UfoCarrier508` | 2 | `OBJECT/UFOCARRIER301.SGO`, `OBJECT/UFOCARRIER401.SGO` | `UfoCarrier_CorePartsName` |
| `UfoMother301` | 1 | `OBJECT/UFOMOTHER301.SGO` | `UfoCarrier_CorePartsName`, `mp_BigCannon`, `mp_Cannon`, `mp_HatchEmission`, `mp_HatchLow`, `mp_HatchMiddle` |
| `UfoMother401` | 17 | `OBJECT/UFOMOTHER401.SGO`, `OBJECT/UFOMOTHER401M009.SGO`, `OBJECT/UFOMOTHER401M012.SGO`, `OBJECT/UFOMOTHER401M019.SGO`, `OBJECT/UFOMOTHER401M201.SGO`, `OBJECT/UFOMOTHER401M412.SGO`, `OBJECT/UFOMOTHER401M550.SGO`, `OBJECT/UFOMOTHER401M551.SGO`, `OBJECT/UFOMOTHER401M552.SGO`, `OBJECT/UFOMOTHER401M553.SGO`, `OBJECT/UFOMOTHER401M554.SGO`, `OBJECT/UFOMOTHER401M555.SGO`, `OBJECT/UFOMOTHER401M556.SGO`, `OBJECT/UFOMOTHER401M557.SGO`, `OBJECT/UFOMOTHER401M602.SGO`, `OBJECT/EDF41MP02/UFOMOTHER401_DLC_M809.SGO`, `OBJECT/EDF41MP02/UFOMOTHER401_DLC_MP503.SGO` | `action_list` |
| `UfoRobo` | 1 | `OBJECT/UFOROBO401.SGO` | `cannon_bullet_class`, `cannon_bullet_custom_param`, `charge_bullet_class`, `charge_bullet_custom_param`, `genocide_bullet_class`, `genocide_bullet_custom_param`, `robo_attack_table`, `robo_large_muzzle_setting`, `robo_middle_muzzle_setting`, `robo_small_muzzle_setting`, `small_bullet_class`, `small_bullet_custom_param` |
| `UfoSmall507` | 2 | `OBJECT/UFO401.SGO`, `OBJECT/EDF41MP02/UFO401ACE.SGO` | `ai_attack_pattern`, `bullet_class`, `bullet_custom_param`, `bullet_info`, `bullet_pattern` |
| `UfoSmall515` | 2 | `OBJECT/UFO301.SGO`, `OBJECT/UFO301ACE.SGO` | `battery_config`, `bullet_class`, `bullet_custom_param`, `bullet_object`, `ufo_muzzle` |


4.1 特有技能证据：

- UfoRobo `robo_attack_table`：`attack_chop_l`, `attack_chop_r`, `attack_fire360`, `attack_fire_cross`, `attack_fire_genocide`, `attack_fire_straight`, `attack_stomp_l`, `attack_stomp_r`。另有 `small/cannon/charge/genocide_bullet_*` 四组；变形与步行 / 飞船模式由插件状态机实现。
- UfoMother301 `mp_BigCannon` / `mp_Cannon` / `mp_HatchEmission` / `mp_HatchLow` / `mp_HatchMiddle` / `mp_Panel` / `mp_Weakpoint`。
- UfoMother401 `action_list` 不是攻击名称列表：本体基础 SGO 动作号含 0..6，三种 `panel_layout_map*` 布局；必须走自有实现解释，不能按普通生物攻击表解释。
- 4.1 DragonSmall 的大龙与 HS 变种带 `dragon_flame_bullet_class/param`；Nephila02/03 带 `nephila_ex_attack_param`。
- 21 个 Hector 文件已转为 EDF6_Berserker_Large，但炮种/左右挂载取自各自 SGO，不能当原版 E602 的默认炮。

## 建筑与其他不应直接作为角色根的对象

### 原版静态建筑候选：4 类 / 15 SGO


- `AntHill`：`OBJECT/E517_ANTHILL.SGO`, `OBJECT/E615_HORNETNEST_SMALL01.SGO`, `OBJECT/E615_HORNETNEST_SMALL02.SGO`, `OBJECT/E615_HORNETNEST_SMALL03.SGO`
- `BigNest`：`OBJECT/HORNETNEST401.SGO`
- `GeneratorPoll`：`OBJECT/E505_GENERATOR.SGO`, `OBJECT/E505_GENERATOR_DIRT.SGO`, `OBJECT/E505_GENERATOR_LARGE.SGO`, `OBJECT/E505_GENERATOR_LARGE_FAR.SGO`, `OBJECT/E505_GENERATOR_SUPERLARGE.SGO`
- `EDF6_TimeShip_Anchor`：`OBJECT/E612_ANCHOR.SGO`, `OBJECT/E612_ANCHOR_FULL_LIGHT.SGO`, `OBJECT/E612_ANCHOR_HEAVYFIRE.SGO`, `OBJECT/E612_ANCHOR_LIGHT.SGO`, `OBJECT/E666_ANCHOR_SMALL_HEAVYFIRE.SGO`

4.1 静态建筑 / 拟合网：AntHill 三个（`ANTHILL301`、`EDF41MP01/ANTHILL301UG`、`EDF41MP02/ANTHILL301UGMP02`）；BigNest 一个（`EDF41MP02/ANTHILL301LARGEMP02`）；SpiderNet 两个（`EDF41MP01/SPIDERNET160_FIT`、`SPIDERNET80_FIT`）。后者依赖原地图拟合，不是可随地创建的角色。

### 原版非角色卵 / 网 / 囊：4 类 / 25 SGO


- `EDF6_TimeShip_Capsule`：`OBJECT/E613_CAPSULE.SGO`
- `EDF6_SpiderEggSac`：`OBJECT/E663_SPIDER_EGG_SAC.SGO`, `OBJECT/E663_SPIDER_EGG_SAC_LARGE.SGO`, `OBJECT/E663_SPIDER_EGG_SAC_LARGE_TILT.SGO`, `OBJECT/E663_SPIDER_EGG_SAC_SHRINK_PERCENT.SGO`, `OBJECT/E663_SPIDER_EGG_SAC_SMALL.SGO`, `OBJECT/E663_SPIDER_EGG_SAC_SMALL_TILT.SGO`, `OBJECT/E663_SPIDER_EGG_SAC_TILT.SGO`
- `GiantAntEgg`：`OBJECT/E619_SQUID_SMALL_EGG.SGO`, `OBJECT/E619_SQUID_SMALL_EGGB.SGO`, `OBJECT/GIANTANT01EGG.SGO`, `OBJECT/GIANTANT01EGGB.SGO`, `OBJECT/GIANTANT02BEGG.SGO`, `OBJECT/GIANTANT02BEGGB.SGO`, `OBJECT/GOLDANTEGG.SGO`, `OBJECT/GOLDANTEGGB.SGO`
- `SpiderNet`：`OBJECT/SPIDERNET10.SGO`, `OBJECT/SPIDERNET160.SGO`, `OBJECT/SPIDERNET20.SGO`, `OBJECT/SPIDERNET240.SGO`, `OBJECT/SPIDERNET320.SGO`, `OBJECT/SPIDERNET40.SGO`, `OBJECT/SPIDERNET480.SGO`, `OBJECT/SPIDERNET640.SGO`, `OBJECT/SPIDERNET80.SGO`

### 敌方武器 / 盾：5 类 / 32 SGO


- `HumanoidWeapon_Basic`：`OBJECT/E503_ARMOR_ASSAULT.SGO`, `OBJECT/E503_ARMOR_LAUNCHER.SGO`, `OBJECT/E503_ARMOR_SHOTGUN.SGO`, `OBJECT/E503_ASSAULT.SGO`, `OBJECT/E503_LAUNCHER.SGO`, `OBJECT/E503_SHOTGUN.SGO`, `OBJECT/E506_ASSAULT.SGO`, `OBJECT/E506_HA_LAUNCHER.SGO`, `OBJECT/E506_LAUNCHER.SGO`, `OBJECT/E506_SHOTGUN.SGO`, `OBJECT/E617_WILDFROG_RIFLE.SGO`, `OBJECT/E617_WILDFROG_RIFLE_LR.SGO`
- `HumanoidWeapon_Heavy`：`OBJECT/E506_HA_FLAMETHROWER.SGO`, `OBJECT/E506_HA_HEAVYMACHINEGUN.SGO`, `OBJECT/E506_HA_MISSILE.SGO`
- `EDF6_MartianWeapon_Barrage`：`OBJECT/E664_BUBBLE.SGO`, `OBJECT/E664_BUBBLE_WALL.SGO`
- `EDF6_MartianWeapon_Basic`：`OBJECT/E601_LAUNCHER.SGO`, `OBJECT/E601_RIFLE.SGO`, `OBJECT/E601_SHOTGUN.SGO`, `OBJECT/E601_SNIPER.SGO`, `OBJECT/E616_CANNON.SGO`, `OBJECT/E616_HOMING.SGO`, `OBJECT/E616_LASER.SGO`, `OBJECT/E661_ACID_SPLAY.SGO`, `OBJECT/E661_CANNON.SGO`, `OBJECT/E661_RIFLE.SGO`, `OBJECT/E661_RIFLE2.SGO`, `OBJECT/E664_GUN.SGO`
- `EDF6_SoftBodyWeapon_Shield`：`OBJECT/E601_SHIELD.SGO`, `OBJECT/E616_SHIELD2.SGO`, `OBJECT/E661_SHIELD.SGO`

### 依附部件 / 特效：39 类 / 113 SGO

主角准入应排除，所属根角色的技能实现仍要覆盖相应部件。


- `GeneratorPoll_Destroy` (3)：`E505_GENERATORDESTROY.SGO`, `E505_GENERATOR_LARGE_DESTROY.SGO`, `E505_GENERATOR_SUPERLARGE_DESTROY.SGO`
- `GeneratorPoll_FallEffect` (1)：`E505_GENERATORFLARE.SGO`
- `GeneratorPoll_SpawnEffect` (3)：`E505_GENERATORSPAWNEFFECT.SGO`, `E511_MOTHERSHIP_UNIT_SPAWN_EFFECT.SGO`, `E516_GENERATORSPAWNEFFECT.SGO`
- `DeiroiFootParts_Battery` (2)：`E509_DEIROIFOOTPARTS_BEAM.SGO`, `E509_DEIROIFOOTPARTS_MISSILE.SGO`
- `DeiroiFootParts_Joint` (2)：`E509_DEIROIFOOTPARTS_JOINT.SGO`, `E509_DEIROIFOOTPARTS_ROOT.SGO`
- `DeiroiFootParts_Toe` (1)：`E509_DEIROIFOOTPARTS_TOE.SGO`
- `DeiroiFallEffect` (8)：`E509_DEIROI_FALL.SGO`, `E509_DEIROI_FALL_L.SGO`, `E509_DEIROI_FALL_L2.SGO`, `E509_DEIROI_FALL_M.SGO`, `E509_DEIROI_FALL_S.SGO`, `E509_DEIROI_FALL_SM.SGO`, `E509_DEIROI_FALL_XL.SGO`, `E509_DEIROI_FALL_XL2.SGO`
- `MovingFortress_Debris` (4)：`E510_FORTRESS_DEBRIS1.SGO`, `E510_FORTRESS_DEBRIS2.SGO`, `E510_FORTRESS_DEBRIS3.SGO`, `E510_FORTRESS_DEBRIS4.SGO`
- `MovingFortress_LargeCannon` (1)：`E510_FORTRESS_PARTS_LARGECANNON.SGO`
- `MovingFortress_MediumCannon` (2)：`E510_FORTRESS_PARTS_MEDIUMCANNON.SGO`, `E510_FORTRESS_PARTS_MEDIUMCANNON_2.SGO`
- `MovingFortress_Battery` (9)：`E510_FORTRESS_PARTS_SMALLCANNON1.SGO`, `E510_FORTRESS_PARTS_SMALLCANNON1_AA.SGO`, `E510_FORTRESS_PARTS_SMALLCANNON1_G.SGO`, `E510_FORTRESS_PARTS_SMALLCANNON1_P.SGO`, `E510_FORTRESS_PARTS_SMALLCANNON1_X.SGO`, `E510_FORTRESS_PARTS_SMALLCANNON2.SGO`, `E510_FORTRESS_PARTS_SMALLCANNON2_AA.SGO`, `E510_FORTRESS_PARTS_SMALLCANNON2_X.SGO`, `E510_FORTRESS_PARTS_SMALLCANNON3.SGO`
- `UfoMother511CoreBigCannon` (1)：`E511_MOTHERSHIP_GENOCIDE_L.SGO`
- `UfoMother511CoreSmallCannon` (1)：`E511_MOTHERSHIP_GENOCIDE_S.SGO`
- `UfoMother511RingUnitParts_DummyRing` (1)：`E511_MOTHERSHIP_LIGHT_UNIT.SGO`
- `UfoMother511RingUnitParts` (5)：`E511_MOTHERSHIP_UNIT1.SGO`, `E511_MOTHERSHIP_UNIT2.SGO`, `E511_MOTHERSHIP_UNIT3.SGO`, `E511_MOTHERSHIP_UNIT4.SGO`, `E511_MOTHERSHIP_UNIT5.SGO`
- `Humanoid_BigGreyBoss_HandMuzzle` (3)：`E516_BIGGREYBOSS_CENTERMUZZLE.SGO`, `E516_BIGGREYBOSS_GENOCIDEMUZZLE.SGO`, `E516_BIGGREYBOSS_HANDMUZZLE.SGO`
- `Humanoid_BigGreyBoss_EffectModel` (3)：`E516_BIGGREYBOSS_EFFECTDIVEATTACK.SGO`, `E516_BIGGREYBOSS_EFFECTLARGE.SGO`, `E516_BIGGREYBOSS_EFFECTMIDDLE.SGO`
- `Humanoid_BigGreyBoss_EffectFire` (1)：`E516_BIGGREYBOSS_EFFECTKICKFIRE.SGO`
- `Humanoid_BigGreyBoss_Meteor` (6)：`E516_BIGGREYBOSS_METEOR_LARGE.SGO`, `E516_BIGGREYBOSS_METEOR_MIDDLE.SGO`, `E516_BIGGREYBOSS_METEOR_SMALL.SGO`, `E609_JORMUNGAND_METEOR_LARGE.SGO`, `E609_JORMUNGAND_METEOR_MIDDLE.SGO`, `E609_JORMUNGAND_METEOR_SMALL.SGO`
- `Humanoid_BigGreyBoss_MeteorBreak` (6)：`E516_BIGGREYBOSS_METEOR_LARGEBREAK.SGO`, `E516_BIGGREYBOSS_METEOR_MIDDLEBREAK.SGO`, `E516_BIGGREYBOSS_METEOR_SMALLBREAK.SGO`, `E609_JORMUNGAND_METEOR_LARGEBREAK.SGO`, `E609_JORMUNGAND_METEOR_MIDDLEBREAK.SGO`, `E609_JORMUNGAND_METEOR_SMALLBREAK.SGO`
- `Humanoid_BigGreyBoss_Ring` (2)：`E516_BIGGREYBOSS_RING.SGO`, `E516_BIGGREYBOSS_RING_SMALL.SGO`
- `Humanoid_BigGreyBoss_Ring_Large` (1)：`E516_BIGGREYBOSS_RING_LARGE.SGO`
- `EDF6_Berserker_Equipment_Bomb` (2)：`E602_BERSERKER_EQUIP_BOMB.SGO`, `E602_BERSERKER_MIDDLE_EQUIP_BOMB.SGO`
- `EDF6_Jormungand_Parts_Body` (7)：`E609_JORMUNGAND_BODY.SGO`, `E609_JORMUNGAND_BODY_GEN.SGO`, `E609_JORMUNGAND_BODY_L01.SGO`, `E609_JORMUNGAND_BODY_L02.SGO`, `E609_JORMUNGAND_BODY_S01.SGO`, `E609_JORMUNGAND_TAIL_MIDDLE.SGO`, `E609_JORMUNGAND_TAIL_TERM.SGO`
- `EDF6_Jormungand_Parts_AlienHead` (1)：`E609_JORMUNGAND_FACE.SGO`
- `EDF6_Jormungand_Parts_AlienHead_Destroy` (1)：`E609_JORMUNGAND_FACE_DESTROY.SGO`
- `EDF6_Jormungand_AdditionalParts_Generator` (1)：`E609_JORMUNGAND_GENERATOR.SGO`
- `EDF6_Jormungand_AdditionalParts` (1)：`E609_JORMUNGAND_GUARD.SGO`
- `EDF6_Jormungand_AdditionalParts_Cannon` (3)：`E609_JORMUNGAND_GUNBATTERYL01.SGO`, `E609_JORMUNGAND_GUNBATTERYL02.SGO`, `E609_JORMUNGAND_GUNBATTERYS01.SGO`
- `EDF6_Jormungand_Parts_Head` (1)：`E609_JORMUNGAND_HEAD.SGO`
- `EDF6_Jormungand_AdditionalParts_HeadRing` (1)：`E609_JORMUNGAND_HEADRING.SGO`
- `EDF6_TimeShip_Anchor_Barrier` (2)：`E612_ANCHOR_BARRIER.SGO`, `E666_ANCHOR_SMALL_BARRIER.SGO`
- `EDF6_TimeShip_SpawnEffect` (1)：`E612_ANCHOR_SPAWN_EFFECT.SGO`
- `EDF6_GrandMotherParts_GunBattery` (9)：`E614_GRANDMOTHER_ARMPARTS_M123_BEAMSPREAD.SGO`, `E614_GRANDMOTHER_ARMPARTS_M123_BIGLASER.SGO`, `E614_GRANDMOTHER_ARMPARTS_M123_GENOCIDE.SGO`, `E614_GRANDMOTHER_ARMPARTS_M123_LASERSPREAD.SGO`, `E614_GRANDMOTHER_ARMPARTS_M123_PLASMACANNON01.SGO`, `E614_GRANDMOTHER_ARMPARTS_M61_BEAMSPREAD.SGO`, `E614_GRANDMOTHER_CANNON01.SGO`, `E614_GRANDMOTHER_CANNON01_L.SGO`, `E614_GRANDMOTHER_CANNON02.SGO`
- `EDF6_GrandMother_Arm` (10)：`E614_GRANDMOTHER_ARM_M099A.SGO`, `E614_GRANDMOTHER_ARM_M099B.SGO`, `E614_GRANDMOTHER_ARM_M123A.SGO`, `E614_GRANDMOTHER_ARM_M123B.SGO`, `E614_GRANDMOTHER_ARM_M123C.SGO`, `E614_GRANDMOTHER_ARM_M302A.SGO`, `E614_GRANDMOTHER_ARM_M302B.SGO`, `E614_GRANDMOTHER_ARM_M61_5A.SGO`, `E614_GRANDMOTHER_ARM_M61_5B.SGO`, `E614_GRANDMOTHER_ARM_PARTS_TEST.SGO`
- `EDF6_GrandMotherParts_Gate` (1)：`E614_GRANDMOTHER_GATE.SGO`
- `EDF6_GrandMotherParts_Weak` (1)：`E614_GRANDMOTHER_WEAKPOINT.SGO`
- `GiantAntEgg_Break` (4)：`E619_SQUID_SMALL_EGG_BREAK.SGO`, `GIANTANT01EGG_BREAK.SGO`, `GIANTANT02BEGG_BREAK.SGO`, `GOLDANTEGG_BREAK.SGO`
- `EDF6_SpiderEggSacNet` (1)：`E663_SPIDER_EGG_SAC_STRING.SGO`

### 其余 49 类 / 316 SGO

我方玩家/NPC/载具、运输投送、环境/演出/测试对象。分类不代表这类对象永远不能被任务脚本设为敌方；这里是原版资源的角色身份，不是运行时 team 值。

`AssultSoldier`, `BombingPlane`, `CaveWall`, `ClingTest`, `CollapseModel`, `DamageMapTestObject`, `Decoy`, `DemoAirStrike`, `DemoIndirectFire`, `Engineer`, `EventAnimationObject`, `EventHelicopter`, `EventParticle`, `FarEventObject`, `HeavyArmor`, `PaleWing`, `People`, `RagdollTest`, `RouteGuide_Arrow`, `RouteTest`, `ShootingTarget`, `SoldierDrone_Basic`, `TestObjectSato01`, `Test_n`, `Transporter`, `Transporter508`, `Transporter_Container`, `Transporter_Formation`, `Vehicle402_Rocket`, `Vehicle403_Tank`, `Vehicle404_Tank`, `Vehicle501_FortressRobo`, `Vehicle502_GroundRobo`, `Vehicle503_Bike`, `Vehicle504_begaruta`, `Vehicle505_Tank`, `Vehicle506_Helicopter`, `Vehicle507_Rescuetank`, `Vehicle510_Maser`, `Vehicle601_Tank`, `Vehicle603_Flak`, `Vehicle607_RoboTruck`, `Vehicle60X_Truck`, `Vehicle612_nix`, `VehicleBigBegaruta`, `VehicleHelicopter409`, `VehicleHelicopter410`, `Vehicle_Car`, `Vehicle_Tank2`。

### 4.1 其余注册行

- 28 个友军兵（AssultSoldier12、HeavyArmor6、PaleWing10）。
- 15 个我方载具（Vehicle505_Tank7、Vehicle501_FortressRobo2、Vehicle506_Helicopter1、VehicleBegaruta4、Vehicle_Car1）。
- 1 个友军 Transporter_Formation。
- 6 个静态建筑 / 原地图拟合网（上列）。

## 验收含义

本普查能证明目录、字段、变种与排除边界；不能证明玩家控制、技能触发、镜头、死亡回退、真实联机或游戏手感。需要逐家族原生控制适配与运行验证后才可称“全敌方可操控”。
