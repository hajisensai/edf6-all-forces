# 新兵种（载具手 / 刻印剑士）实现约定

上游清单：[plan-2026-11-28-zero-worldline-gear.md](plan-2026-11-28-zero-worldline-gear.md)。逆向结论：[newclass-re/](newclass-re/)（`architecture.md` 是总架构和反驳核对，`inject.md` 是注入点细节，其余是各子系统）。本文是各模块并行实现时共同遵守的接口约定，改约定先改本文。

EDF.dll TimeDateStamp `0x678CCB46`，地址都是 RVA。

## 0. 已定的方向（2026-11-28）

- 新兵种在引擎里真实存在：SoldierInit 的第 4、5 个元素，type 值 4 / 5，不借 0..3。GameStatus 和存档本来就给每名本机玩家留了 10 个兵种的格子（`docs/newclass-re/architecture.md` §1 #1），装备、护甲、配色、通关位直接用预留格。**不另起插件存档**。
- 玩家对象是插件工厂造的新类，底层用 AssultSoldier 的构造函数（`0x54FF10`，对象 `0x2090` 字节），换克隆 vtable。底层构造函数只是实现手段，移动、能力、资源槽都在插件里另写。
- 原版整备界面（`HUiHQSoldierSelect` / `UiSoldierSelect` / `UiWeaponSelect`）不适配新兵种，入口交给插件的新整备界面 `src/armory*`（维护者 2026-11-28：「整备界面原版的删掉就行，我们弄了新的整备界面」）。
- 联机：全员同版。房间协议号 +1，未装 / 旧版成员由房主拒绝并在公告写明所需版本。EDF6Coop 的兵种上限同步改成 6（另一个仓库，另开 PR）。
- 载具座位：`0x5DF770` 对 type 4/5 全部放行，两个新兵种都能上所有座位，不加限制。

## 1. 编号与命名（各模块必须一致）

| 项 | 载具手 | 刻印剑士 |
|---|---|---|
| 兵种 type / SoldierInit 下标 | 4 | 5 |
| 内部名 | `ZwVehicler` | `ZwRuneBlade` |
| 名字键（SoldierInit `[1]`，文本表） | `SoldierType_ZwVehicler` | `SoldierType_ZwRuneBlade` |
| 工厂名（SGO 的 `xgs_scene_object_class`） | `ZwVehicler` | `ZwRuneBlade` |
| 战斗 SGO（`[2]` 4 个外观都指向本兵种自己的文件） | `app:/object/zw_vehicler_0N.sgo`（N=1..4） | `app:/object/zw_runeblade_0N.sgo` |
| 菜单 SGO（`[5][1]`） | `APP:/MENUOBJECT/zw_vehicler_0N.SGO` | `APP:/MENUOBJECT/zw_runeblade_0N.SGO` |
| CustomColor（`[5][0]`，必须真实存在） | `App:/DefaultPackage/CustomColorZwVehicler.sgo` | `App:/DefaultPackage/CustomColorZwRuneBlade.sgo` |
| 名牌字母 / 颜色（`0x806FE0` 第 4/5 项） | `V`，橙 (1, 0.55, 0) | `S`，蓝紫 (0.6, 0.4, 1) |
| 脚本边界看到的 type（只给任务脚本） | 0 | 0 |

武器类别号（新号段，不和原版重叠；原版最大 334）：

| 类别 | 用途 | 槽 |
|---|---|---|
| 400 | 载具手载具 | 载具手 slot 0 `ZW_SlotVehicle` |
| 401 | 载具手手枪 | 载具手 slot 1 `ZW_SlotPistol` |
| 500 | 刻印剑士近战 | 剑士 slot 0 `ZW_SlotMelee` |
| 501 | 刻印剑士远程 | 剑士 slot 1 `ZW_SlotRanged` |
| 502 | 刻印剑士支援 | 剑士 slot 2 `ZW_SlotSupport` |

`SoldierWeaponCategory` 要同时追加这 5 个类别（`inject.md` §1.1：不追加的话类别表会插进名字为 null 的项），类别名键 `WeaponCategory_ZwVehicle` / `_ZwPistol` / `_ZwMelee` / `_ZwRanged` / `_ZwSupport`。

新兵种的槽只能列新类别，默认武器只能是新武器行（`architecture.md` §2 第 4 条：`0xE2D51` 会把类别改判给后写的兵种）。

## 2. 模块分工（文件不重叠）

| 模块 | 负责 | 主要文件 |
|---|---|---|
| A 兵种数据与注入 | 运行时把 SoldierInit / SoldierWeaponCategory 换成「原版 0..3 + 插件 4/5」的常驻副本（`0xE2C17` / `0xE2C2A`）；兵种定义表；SGO 节点构建（纯逻辑可测）；装甲统计 / 按键菜单 / PlayCount 的兵种数钳制 | `src/soldier_classes*.{h,cpp}`、`src/sgo_build_logic.h`、`tests/soldier_classes_test.cpp` |
| B 玩家对象与按兵种定长表 | 工厂 `ZwVehicler` / `ZwRuneBlade`（AssultSoldier ctor + 克隆 vtable，slot 83 返回 4/5，析构 thunk）；名牌表扩到 6；伤害数字颜色 stub；脚本边界 type 映射；战斗中按 type 查的其它表（按键配置等）逐个查清并补 | `src/soldier_factory*.{h,cpp}`、`src/soldier_type_patches*.{h,cpp}`、`src/edf41classes.*`（扩展克隆槽数，只加不改行为） |
| C 资源与武器行 | 战斗 / 菜单 SGO、CustomColor、文本键（5 种语言）、新武器行的登记表与生成器（追加在现有登记表之后，行号永不移动）、installer / build_release 接线、`gen_weapon_series` 认新兵种 | `tools/make_zw_classes.py`、`tools/zw_weapons.py`、`zwport/`（资源目录）、installer 相关 |
| D 存档 | 存盘副本里两名本机玩家的 `+0x6E90` 写替身（原版兵种 0），真实兵种记进兵种 9 预留格；读档后替身未被改过才恢复 | `src/soldier_save*.{h,cpp}`（钩子挂在 `weapon_records` 已有的存档链上，只加回调不改它的逻辑） |
| E 插件侧兵种数与整备界面 | `kClasses=4` 全部改成 6 / 运行时取；armory 武器页加两个兵种页；预设文件格式升版；原版兵种 / 武器选择入口改开 armory | `src/armory*`、`src/player_slots.h`、`src/weapon_page_logic.h`、`src/loadout_preset.h`、`src/npcai.cpp` 等 |
| F 联机 | 房间协议号 +1、旧版拒绝与公告；记录里 class 4/5 原样传；EDF6Coop 兵种上限 6（另一个仓库） | `src/mod_room.*`、`D:\APP\edf6_directnet` |

模块 A 和 C 之间的接口就是 §1 的路径与类别号：A 只引用这些路径，C 保证这些文件被安装。

## 3. 分阶段交付

1. **地基（本阶段）**：A–F。槽位一开始就按最终结构定（载具手 2 槽、剑士 3 槽，之后不再改槽序，免得存档里的装备格错位）。载具手：载具槽只有「无」行 `ZW_VehicleNone`（400），手枪槽是陀螺喷射火箭手枪雏形 `ZW_Gyrojet01`（401）。剑士：近战槽「无」行 `ZW_MeleeNone`（500），远程槽是晨钟雏形 `ZW_RuneCarbine01`（501），支援槽「无」行 `ZW_SupportNone`（502）。「无」行照原版 `aSupportNone` 的做法。模型暂用占位（在资源登记里标 `placeholder`，不算交付）。
2. 载具手：开局坐车入场、后勤点补车、V1–V3、陀螺喷射火箭手枪定稿。
3. 刻印剑士：外观与动作、以太槽与踏阵、M1、R1、R4。
4. V4–V9、关卡缴获解锁。
5. M2、M4、M5、R2、R3。
6. M3 回旋刃、NPC 新兵种。

实机测试按维护者要求放到最后一次采齐（`gamequeue` 一次启动，多条 `--cmd`）。
