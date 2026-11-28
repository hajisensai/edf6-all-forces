# 刻印剑士外观与动作（RB-LOOK）

对应总清单 `docs/plan-2026-11-28-zero-worldline-gear.md` §2 开头：一战风格以太联军军官制服加短披风，飞行时背后和脚下浮出刻印光阵；以太蓝紫光痕、命中点小刻印光阵，所有武器共用一套光阵贴图和光痕材质。

代码：
- `tools/make_runeblade_fx.py`：共用光阵和光痕（工具绘制，不用外部素材）。
- `tools/make_runeblade_look.py`：模型来源 `MODEL`、动作选择 `MOTIONS`（每个动作对应哪些原版 CAS 状态、显示哪些特效）。
- `tools/make_zw_classes.py`：把特效文件并进剑士的文件（`files_of`、`ASSETS` 标 `generated`、`build`），由同一套 ledger（`zwclass`）安装和卸载。
- 测试：`tests/zw_classes_test.py`。有游戏时，逐个自检特效档，并核对 `MOTIONS` 里的每个状态都在原版 CAS 里。

## 1. 模型：仍用占位（未交付）

现状：战斗 / 菜单 SGO 和 CustomColor 仍是游骑兵的（`ASSETS` 标 `placeholder`），`make_runeblade_look.MODEL = None`。

2026-11-28 按 `sketchfab-models.md` 搜过 Sketchfab，条件是可下载，按点赞排序。关键词：`ww1 officer`、`world war 1 officer rigged`、`ww1 officer cape`、`steampunk officer cape rigged`、`military officer greatcoat cape`。没有合适的：
- 结果大多是道具或扫描件（军帽、夹克扫描 111 万面、骑兵雕塑 132 万面）。
- 唯一的人形是 `Afrika korps officer`（uid `719093edec5a40a9a2267d7b16f31281`，作者 Tomrs，约 9.7k 面），但它是二战制服、没有骨骼、没有披风。
- 没有找到任何带骨骼的一战军官，也没有带披风的。

即使找到模型也接不上，缺的是整条人形管线：
1. glb → EDF6 玩家 MDB，要蒙皮到游骑兵骨架上（`EDF6ArmySoldier.cas` 的骨骼名、逆绑定矩阵），4 个外观，带 LOD。
   - 仓库现有的 `pylib/mdb.py graft_skinned` 只接武器类蒙皮件。
   - 可以参考 `pylib/xm_rig.py` / `xm_infantry.py`，它们把 X-Morph 步兵绑到 AssultSoldier 上。
2. 披风是次级动画：EDF6 的玩家没有布料，只能做成刚性分段骨挂在背骨上，或者烘进 CAS。
3. 菜单 SGO（`MENUOBJECT`）与战斗 SGO 共用同一个模型（统一模型文件的规定）。
4. 下载的文件放 `D:\APP\edf6-models\<目录>\`，附 `SOURCE.txt`，并加进 `build_release.py RELEASE_MODELS` 和分享包 `EXTERNAL_MODELS`。

下一步可以换关键词再搜（`officer rigged mixamo`、`victorian soldier rigged`、`ww1 soldier rigged`），找一个带骨骼的普通一战士兵，披风和肩章另外建模。也可以请维护者提供模型。

## 2. 共用特效（已做：工具生成，随剑士文件安装）

所有特效共用两套贴图，每套各一张线条图和一张光晕图（DXT1，带 mip 和 `.lod`）：

| 贴图 | 尺寸 | 内容 |
|---|---|---|
| `zw_rune_circle_lines/glow.dds` | 1024² | 刻印光阵：双圈、符文带、60/12 刻度（刻时）、七芒星和 7 个小圈（七星）、内圈符文、中心印 |
| `zw_rune_trail_lines/glow.dds` | 512² | 光痕：沿 u 从尾到头，中心细白芯，外面是宽的蓝紫光带；尾端淡出 |

材质沿用终极打击魔法阵那一套（`pylib/magic_circle.py`）：
- 原版母舰屏障材质 `snd_BeamShield`。light0 是线条颜色、刃口偏白，light1 是光晕颜色、蓝紫。
- 两面四边形挂在 FarEventObject 上，不带 ragdoll。

每个变体是一个 `Mods/OBJECT/ZW_RUNE_<变体>.MRAB` + `.SGO`。变体之间只有颜色强度和基准尺寸不同；各武器要的尺寸由插件生成对象后按 scale 调。

| 变体 | 基准尺寸 | 用途 |
|---|---|---|
| FOOT | 2.6 m | 踏阵：起飞和每次踏阵时脚下的光阵 |
| BACK | 2.0 m | 飞行时背后的光阵；居合架势 |
| HIT | 1.2 m | 命中点的小刻印光阵（所有武器） |
| BURST | 12 m | 坠星落地冲击波、骑枪刺中、炮击信标落点 |
| TRAIL | 4 × 0.5 m | 光痕（x 为长）：刀弧、枪尾、弹道 |

缺口（归各武器模块和踏阵模块，本模块不做）：
- 插件里还没有生成和放置这些对象的代码。做法照 `src/ultimate.cpp` 的魔法阵：房主权威，走 `plugin_net` 通道广播「在哪、多大、哪个变体、朝向」，客人本地生成。
- 光痕现在是一张直条四边形，还不是沿刀弧走的拖尾网格。要贴合刀弧，需要插件逐帧拉伸或旋转多段，或者新做带 UV 的弧形网格。
- 原版斩击弹（SlashWave / EfsBullet）自带的特效没换掉。换成光痕材质需要逆向它们的特效引用（efarc），这一步留给 M1 / M2 模块。
- 表盘刻印（M5 敌人身上那种）、北斗七星排布（R3）复用同一张光阵贴图，由插件排布多个 HIT。

## 3. 动作选择（已做：数据表 + 离线核对）

全部是原版 CAS 状态。2026-11-28 用本机 Root.cpk 核对过，全部存在（`python tools/make_runeblade_look.py` 返回 0）。

| 动作 | CAS / player | 状态 |
|---|---|---|
| M1 三段连斩 | HeavyArmor / main | sword_thrush_r → sword_thrush2_r → sword_thrush3_r |
| M1 空中急坠下劈、M4 空中下砸 | HeavyArmor / main | hammer_jumpKnock_r |
| M2 蓄力突刺 | HeavyArmor / main | pile_setS1_r → pile_dashH1_r → pile_dashH2_r |
| M3 双刃乱舞 | HeavyArmor / main | hammer_turnSwing_r, hammer_turnImpact_r |
| M4 地面横扫 | HeavyArmor / main | hammer_swing_r, hammer_KnockRoller_r |
| M5 居合架势 / 拔刀 | HeavyArmor / weapon_r，main | hammer_charge / hammer_swingK_r |
| 踏阵 起跳 / 连踏 | HeavyArmor / main | base_high_jump_start, base_high_jump_up(_f) |
| 踏阵 下落 / 落地 | HeavyArmor / main | base_jump_fall, base_jump_land |
| R1 / R2 持枪 | ArmySoldier / weapon | aim, assault_recoil1, assault_reload1 |

**阻碍（需要父会话定）：** 近战和踏阵的状态都在剑兵的 `EDF6HeavyArmor.cas` 里，远程持枪在游骑兵的 `EDF6ArmySoldier.cas` 里。现在剑士的工厂用 AssultSoldier ctor（`src/soldier_factory.h`），战斗 SGO 用游骑兵模板（单槽 `weapon`，CAS 是 ArmySoldier）。剑兵的近战状态不在这份 CAS 里，Weapon_Swing / ImpactHammer 构造时按名字解析不到，会拿 -1（motion.md §1.5）。可选做法：

1. 剑士改用 HeavyArmor 为底（motion.md §0 的建议）：工厂改调 HeavyArmor ctor，战斗 SGO 换剑兵模板（`weapon_r` / `weapon_l`、`heavy_booster_setting`、`EDF6HeavyArmor.cas`），远程持枪从剑兵 CAS 的 attach 系列里挑。这要动 `soldier_factory`（B 模块）和本工具的 `TEMPLATE_CLASS`。
2. 保留 AssultSoldier，新做一份合并 CAS：游骑兵 CAS 加上剑兵这些状态，连同骨骼轨道。`pylib/cas_graft.graft` 搬过来的片段**不带轨道**（「clips that move nothing」），所以必须新写一个带轨道的搬运工具，并核对两套骨架的骨名是否一致。

两条路都不在本模块范围，`MOTIONS` 先按状态名定下来，换底以后照样能用。

## 4. 联机

特效对象只在本机显示。生成由房主权威的事件驱动，走 `plugin_net` 通道，由各武器 / 踏阵模块注册，不加 Receive* 链。资源属于安装包内容，全员同版本，不做缺失回退：缺文件就记一行日志，该特效不生成。
