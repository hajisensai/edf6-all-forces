# 钻头战车（EDF6VC_DRILL）逆向与设计笔记

EDF.dll TimeDateStamp `0x678CCB46`，下文地址全部是 RVA。纯静态分析（`tools/edfre.py` + capstone、Root.cpk / Chunk02.cpk 只读解析），
**没有起游戏**。置信度：**H** = 指令 / 数据直接可见；**M** = 证据一致的强推断；**L** = 需要实机核对。

用户要求（2026-10-05）：「钻头战车（模型 EDF铁雨-钻头战车.rar）。支持破坏石头和建筑；钻头是近战类型；根据转速造成伤害和破坏速度」。

实现：`src/drill.cpp`（插件）、`src/jet_bay.cpp` `DrillCharge`（钻头的「咬合」装药）、`src/hud.cpp` `DrillPanel`（转速）、
`pylib/drill_model.py`（模型）、`tools/make_drill.py`（SGO / 武器 / 装药）、`tools/calls.py` `EDF6VC_CALL_DRILL`（请求）。

## 1. 宿主车辆类：Vehicle505_Tank（Blacker）

候选：Blacker（`V505_TANK.SGO`，`Vehicle505_Tank`，vtable `0x17DADB0`）、`Vehicle403_Tank`（三把武器：主炮 + 两挺机枪）、
带近战臂的类（巴尔加 `Vehicle501_FortressRobo`：步行机甲，不是履带车）。选 Blacker：

- 履带车、一把武器（`vehicle_setup` 只有 `v_505tank_cannon01.sgo`，挂在 `cannon_slide`），类最简单。(H，Root.cpk)
- 它的输入（第 55 槽 `0x61ACD0`）读开火只有一处：`0x61AD14 movss xmm1,[seat+0x2E4]` → `0x62DE50`（≥ 0.8）→ `0x62C000(veh+0x638)`
  （拉 0 号武器槽）；同函数里右摇杆 `seat+0x2D0/0x2D4` 写炮塔转向 `veh+0x2AA0/0x2AA4`。没有别的开火入口。(H)
  所以插件在原版输入**之前**读 `seat+0x2E4` 并清零（`DrillInput`，crew.cpp 的 InputHook 在调用下一个输入函数前执行），原版输入就永远不会开炮。
  指令字节作为签名（`kTriggerSig`），不符则钻头功能关闭。
- 「E551」：任务描述里说的 E551 炮塔，在本仓库里（`pylib/artillery_model.py`）指的就是 `V505_TANK.MRAB` 的炮塔外壳；Blacker 的请求是
  `EWEAPON418`（ブラッカーＥ１）等，`EVEHICLE_TANK01` 同一辆车。请求档位按 E 系列：418 / 421 / 425 / 428 / 433。(H，WEAPON SGO)
- 物理：坦克底盘的碰撞体来自 `ragdoll`（`Ragdoll_v505_tank.shkt`，按骨骼名：body / cannon_main / cannon …），`car_base_constraint_data`、
  `car_base_breaking_parts`、CAS（`v505_tank.cas`）、`tank_caterpillar_animation`（材质名 `Caterpi_l/_r`）都**按名字**引用。(H，SGO)
  所以模型只要保留全部骨骼名就和原版类兼容；新增一根骨骼不影响它们（L：CAS 是否按名字绑定没有逐条跟到，见 §5）。

## 2. 模型（`pylib/drill_model.py`）

- OBJ：一个物体、两个材质。`MI_Tank_C`（车体上部和钻头，贴图 `Tank_C_BC.png` 2048²）；`MI_Tank_B_CS`（履带和底盘，y 0~1.85 m，
  贴图没有随模型提供 → 深钢灰纯色）。MTL 没有 `map_Kd`，贴图按文件名指定。
- 轴向：OBJ +X 前、+Y 上；游戏 +Z 前、+Y 上、+X 为车左。转换 x→z、y→y、z→−x（纯旋转，不镜像；selftest 检查）。
- 尺寸与缩放（理由）：铁雨原尺寸 13.4 m 长（车体 −3.8~3.6 m，钻头 4.9~9.6 m）、4.8 m 宽、5.7 m 高。Blacker 车体 6.8 × 3.2 m，
  它的碰撞体就是这个大小。车体比碰撞体大太多时，车体会穿进墙里却碰不到（物理只认碰撞体）。取 **0.8 倍**：车体 6.0 × 3.8 × 4.6 m，
  和 Blacker 碰撞体相当；钻头 3.77 m 长、根部半径 0.97 m，轴线离地 3.37 m，根部在车体原点前 4.19 m（碰撞体前缘约 3.4 m）。
  整体再向前移 0.25 m，车体前后对齐 Blacker 碰撞体。
- 骨骼：保留 Blacker 全部 49 根骨骼（名字、父子、顺序），在 `body` 子树末尾（第 47 位，原来的 `v505_tank` / `Caterpi` 物体骨骼顺延一位）
  插入 `edf6vc_drill`：蒙皮骨骼（kind 3），原点在钻头轴线的根部，轴向与模型相同。车体全部顶点 100% 蒙到 `body`，钻头（OBJ 里
  z > 3.85 m 的三角形，中间没有几何）100% 蒙到 `edf6vc_drill`；从车体伸进钻头根部的传动轴留在车体上（在钻头半径内，check 允许）。
- 去掉原版全部网格（含履带物体 `Caterpi`），保留全部材质（履带滚动动画按材质名找）。新材质两种，复制 Blacker 车体材质
  `v505_tank`（`snd_BRDF_Common_Basic`）：albedo 换成上面两种，法线换平坦法线，粗糙度/金属/AO 换中性纯色。
- 产物约 16.7 MB（贴图占大头）。`check()`：归档 / 模型往返一致、网格合法（<65536 顶点、索引、材质、蒙皮只到蒙皮骨骼、权重和为 1）、
  贴图成员齐全、每根骨骼 bind × inverse bind = 单位阵、原版骨骼名与顺序不变、钻头骨骼轴向即模型轴向、钻头顶点都在半径 0.97 m、
  长 3.77 m 的圆柱内、其余顶点在 `body` 上且在钻头之后（传动轴除外）、车底贴地。

## 3. 建筑和岩石怎么被破坏（游戏自己的路）

### 3.1 子弹 / 爆炸的「破坏建筑」位（H）

- 子弹的伤害信息 GameDamageInfo（GDI）内嵌在 core+0x730（core = bullet+0x140，`docs/decoy-blast-re.md` §1.2），它的标志字 GDI+0x60
  就是 core+0x790 = **bullet+0x8D0**。
- core init 在 `0x2320E3..0x232117`：`AmmoExplosion > 1e-6` → core 置 0x10（命中走范围伤害）且 GDI 标志置 **0x02**；
  **`AmmoExplosion >= 3.0`（常量 `0x1C369BC` = 3.0）→ GDI 标志再置 0x01**。
- EfsBullet 的扩展参数解析（`0x239210`）按名字读 `Ammo_CustomParameter` 字典：`is_break_building`>0 → `or word [bullet+0x8D0],1`
  （`0x23B45C`）；`no_building_damage`>0 → 清 0x01（`0x23B550`）；`is_force_break_building`>0 → 置 0x04（`0x23B63E`）。
  例：Fencer 的 APFSDS 炮 `HWEAPON108`（EfsBullet、无爆炸、穿透）带 `is_break_building: 1`；Root.cpk 里共 30 个 SGO（26 把武器、4 种敌人）用到这三个键。
- 各子弹类构造里改这个位：`LaserBullet01EX_BreakBuilding`（`0x2538A0`）、`ShockWaveBullet01/02Exp`、`SolidBullet01Rail` 置位；
  `BeamBullet01`、`LaserSpearBullet01`、`PulseBullet01`、`NeedleBullet01`、`SolidExpBullet01`、`GrenadeBullet01_MapNoDamage` 清掉。
  **`SolidBullet01` 不改**（`find_disp(0x8D0)` 全部写入点里没有它），所以它的位只由 core init 按爆炸半径决定。

结论 (H)：**爆炸半径 ≥ 3 m 的 SolidBullet01 爆炸带「破坏建筑」位**。

### 3.2 地图物体怎么吃这个位（H / M）

- `MapObject_Base` vtable 第 13 槽 `0x1267A0`（`MapObject_Model / AnimationModel / CrashModel / FixedCrashModel / Efs / Fmex` 共用；
  `MapObject_Structure` 的第 13 槽 `0x142E70` 先处理已损坏时的分块，再尾调这个函数）：参数 rdx 的 +0x50 是伤害、+0x60 是标志，
  与 GDI 布局一致 (H)。
  - `0x1268C1`：标志 0x01（破坏建筑）**或**物体类别 `*(obj+0x1C8)` ∈ {2,4,5,6}（`1<<type & 0x74`）时，耐久 `obj+0x19C` −= 伤害，
    夹在 [0, `obj+0x198`]；否则耐久不变。然后 `0x126EF0` 判定是否倒塌。(H)
  - 耐久初值：`0x125DF3`，`obj+0x198 = obj+0x19C = *(obj+0x1C8)+0x18`。(H)
- 地图数据（Chunk02.cpk 的 MAC，`map.mapb` 的 0x3C 定义块 +0x00 类别、+0x18 浮点）：(M：定义块即 `obj+0x1C8` 所指结构，数值吻合)
  - 平原 `IG_HEIGEN601`：大石块 `sk_rock601a_m.mosb` 100 个，**类别 11、耐久 1**；`sk_randomrock604a/605a.mdx` 类别 1、耐久 0（推断是不可破坏的装饰石）。
  - 城市 `NW_KOUSOUBLD601`：建筑 `.mosb` 类别 1/3/5/6/8/11，耐久 0~1500（常见 10、50、400、900、1200、1500），树 `.trb` 类别 2。
  - 所以「岩石」在 EDF6 里是和建筑同一类的地图构造物（`.mosb`，MapObject_Structure），类别 11 不在「任何伤害都扣耐久」的集合里，
    需要破坏建筑位；它们耐久只有 1，一次带位的伤害就碎。
- 范围伤害 `0x542860`（ApplyAreaDamage 的实际施加）对每个目标调单体伤害 `0x543920`，由它分派到目标的处理函数 (H 调用存在；
  分派到地图物体第 13 槽这一步没有逐条跟到，M)。

### 3.3 钻头用什么去咬（选择与理由）

- 不自己伪造 GDI 直接调伤害（`docs/decoy-blast-re.md` §1.3：GDI 内含带引用计数的 weak_ptr 和 unordered_set，伪造不可取）。
- 用已经在用的「撞击装药」那条路（`docs/jet-model-re.md` §9.1，`src/jet_bay.cpp` `Shell`）：原版炮舰炮弹 `DEMOGUNSHIPFIRESOLID.SGO`
  （DemoIndirectFire + SolidBullet01）改成 1 发、无散布、无等待；插件用 CreateObject 生成，归属设为钻头战车（IFC 每步从归属者 +0x314
  取队伍：只伤敌方，击杀算这辆车的），伤害由插件写 IFC+0xDC，从钻头根部沿直线（IFC+0x2F8 = 0）射向接触点。
- 钻头装药 `EDF6VC_DRILL_CHARGE.SGO`（`tools/make_drill.py`，`make_jets.impact_charge` 同一配方）：**爆炸半径 4 m**（≥ 3 m → 带破坏建筑位，
  所以能破坏建筑和岩石；同时不至于大到炸到车体后面）、2.5 m/帧、4 帧寿命（最远约 10 m）。碰到东西就在那里爆开；什么也没碰到时
  SolidBullet01 到期不爆（`docs/decoy-blast-re.md` §1.2：只有 0x20 位的子弹到期才爆），只是消失。
- 一次装药的伤害同时扣地图物体的耐久（§3.2，不随距离衰减：GDI 是 const 传给每个目标，M）。

## 4. 插件逻辑（`src/drill.cpp`）

- 识别：Blacker 类（vtable `0x17DADB0`）且模型实例里有骨骼 `edf6vc_drill`。模型实例内嵌在 veh+0xEE0（所有载具类都用这个偏移：
  `find_disp(0xEE0)` 在 505 的函数 `0x61A130 / 0x61A740 / 0x61AD70` 等里都是 `lea rcx,[reg+0xEE0]`，H），骨骼记录按名字查（`BoneRecord506`）。
- 转速：玩家按住（`seat+0x2E4 ≥ 0.8`）时以 `DrillMaxRpm / DrillSpinUpSec` 每秒上升，松开以 `DrillMaxRpm / DrillSpinDownSec` 下降，夹在 [0, 最高]。
  NPC 驾驶（没有扳机）：钻头碰到东西后 1.5 秒内视为按住（每 0.2 秒探测一次）。
- 旋转：每帧把钻头骨骼的局部矩阵写成 `Rz(角度) × 绑定姿态`（行向量：第 0、1 行在自身平面内转，第 2 行即轴线不变），角度按转速累加。
  和喷气机舵面同一做法（`jet_flight.cpp` PoseSurfaces，局部矩阵 rec+0x70，引擎每帧 world = local × parent）。若游戏在两帧之间改写了它
  （某段动画在驱动这根骨骼），日志记一行。
- 接触：每 0.2 秒一次。钻头轴线用骨骼的世界矩阵（rec+0xB0：第 3 行原点、第 2 行轴线；上一帧的值）。
  - 敌人：敌方锁定点（`VisitEnemies`，插件已有的锁定登记表遍历）在轴线 [根部后 0.5 m, 尖端前 1 m] 线段周围 `0.97 + 2.5` m 以内，取最近的；
  - 地图：地图射线（layer 22，`MapRay`，只命中地形 / 建筑）沿轴线从根部到尖端前 1 m；没碰到再沿钻头下沿（轴线下方 0.8 × 半径，按车体的上方向）打一条。
    瞄准点取命中点再往里 1 m，装药飞过去在墙面上爆开。
  - 有敌人时打敌人（伤害 `DrillDamage`），否则打地图（`DrillBreak`），各乘 `转速 / 最高转速 × 0.2 s`；转速低于最高的 15% 时只探测不咬。
- HUD：玩家开着钻头战车时，屏幕下方 80% 高度处显示「DRILL xxx RPM」和转速条（琥珀 = 加减速中，绿 = 满转速，碰到东西时加 DRILLING）；
  NPC 开的钻头战车头顶的读数种类显示 drill。
- 依赖：钻头装药走喷气机模块的弹舱 / 炮弹函数（`InstallBay` 的签名）和地图射线（直升机模块的 `rayOk`），所以要求直升机模块的
  profile 通过（`HELI profile=1`）；装药要在本关预载（`JET preload ... drill charge 1`），没装 `EDF6VC_DRILL_CHARGE.SGO` 时每 10 秒记一行日志、不咬。

## 5. 静态核实 vs 需要实机

静态核实（H）：
- 505 输入读扳机的位置与字节（`0x61AD14`），清零扳机能挡住原版开炮；
- GDI 破坏建筑位的来源（爆炸 ≥ 3 m、`is_break_building` 等），SolidBullet01 不改这个位；
- 地图物体第 13 槽按这个位扣耐久、耐久初值来自定义结构 +0x18；
- veh+0xEE0 是所有载具类的模型实例；
- 离线：`python tools/make_drill.py --out <wt>/tmp/out` 用 Root.cpk + OBJ 生成全部 4 个文件，`check()` 全部通过；
  `python tools/selftest.py` 21/21（含 `drill_copies_agree`：C++ 常量与 Python 一致）；`build.cmd` 两个 DLL 都链接成功，无警告。

需要实机验证（L / 未测）：
1. 新增的钻头骨骼是否会被 CAS / 物理改写（日志 `DRILL ... rewritten`），钻头是否看得见地旋转；引擎是否每帧对这根骨骼做 world = local × parent。
2. 钻头装药（SolidBullet01，半径 4 m）是否真的推倒建筑、打碎岩石（`sk_rock601a_m` 耐久 1）；`DrillBreak` 600/s 时建筑倒塌的快慢是否合适。
3. 装药的伤害是否再被难度系数乘、敌人掉血是否和 `DrillDamage` 相称；击杀是否记在玩家名下。
4. 装药爆炸的冲击力（`AmmoHitImpulseAdjust` 沿用炮舰炮弹）是否把钻头战车自己顶开，爆炸特效每 0.2 秒一次是否太吵。
5. 模型：0.8 倍是否合适、履带是否和 Blacker 的车轮 / 碰撞体对得上（贴地、不浮空）、深钢灰履带的观感、相机（抬高到 5.5 m、后移到 18 m）。
6. Blacker 的炮塔 / 炮管碰撞体（`cannon_main`、`cannon`）仍在、随右摇杆转动但看不见；炮塔「被打飞」（`car_base_breaking_parts`）时也看不见。
7. NPC 驾驶时 AI 若开火，只会打出看不见、没有伤害的「钻头」子弹（`EDF6VC_DRILL_BIT.SGO`），是否完全无声无痕。
8. 联机：装药只在开车的那台机器上生成（插件的其它装药同样如此）。
