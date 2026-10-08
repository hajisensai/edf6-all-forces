# 钻头战车（EDF6VC_DRILL）逆向与设计笔记

EDF.dll TimeDateStamp `0x678CCB46`，下文地址全部是 RVA。纯静态分析（`tools/edfre.py` + capstone、Root.cpk / Chunk02.cpk 只读解析），
**没有起游戏**。置信度：**H** = 指令 / 数据直接可见；**M** = 证据一致的强推断；**L** = 需要实机核对。

用户要求（2026-10-05）：「钻头战车（模型 EDF铁雨-钻头战车.rar）。支持破坏石头和建筑；钻头是近战类型；根据转速造成伤害和破坏速度」。

实现：`src/drill.cpp`（插件）、`src/jet_bay.cpp` `DrillCharge`（钻头的「咬合」装药）、`src/hud.cpp` `DrillPanel`（转速、热量）、
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
- 骨骼：保留 Blacker 全部 49 根骨骼（名字、顺序），在 `body` 子树末尾（第 47 位，原来的 `v505_tank` / `Caterpi` 物体骨骼顺延一位）
  插入标记骨骼 `edf6vc_drill`（kind 3，原点在钻头轴线根部，**不带几何**：插件靠它认出钻头战车）。钻头（OBJ 里 z > 3.85 m 的三角形，
  中间没有几何）100% 蒙到原版骨骼 **`catapi_body`**（履带骨架的根）：把它移到钻头轴线根部、轴向与模型相同，改成 `body` 的叶子，
  它原来的 14 个子骨骼 `catapiB..H_l/r` 改挂 `body`（绑定世界矩阵不变，生成时核对与原版逐位相同）。为什么用原版骨骼见 §5.4。
  车体全部顶点 100% 蒙到 `body`；从车体伸进钻头根部的传动轴留在车体上（在钻头半径内，check 允许）。
- 去掉原版全部网格（含履带物体 `Caterpi`），保留全部材质（履带滚动动画按材质名找）。新材质两种，复制 Blacker 车体材质
  `v505_tank`（`snd_BRDF_Common_Basic`）：albedo 换成上面两种，法线换平坦法线，粗糙度/金属/AO 换中性纯色。
- 产物约 16.7 MB（贴图占大头）。`check()`：归档 / 模型往返一致、网格合法（<65536 顶点、索引、材质、蒙皮只到蒙皮骨骼、权重和为 1）、
  贴图成员齐全、每根骨骼 bind × inverse bind = 单位阵、原版骨骼名与顺序不变、`catapi_body` 是 `body` 下没有子骨骼的蒙皮骨骼、
  它和标记骨骼都在钻头根部且轴向即模型轴向、**钻头的几何只蒙到 `catapi_body`、车体的几何只蒙到 `body`**（标记骨骼上没有顶点）、
  钻头顶点都在半径 0.97 m、长 3.77 m 的圆柱内、车体顶点在钻头之后（传动轴除外）、车底贴地。变异实测：把 `SPIN_BONE` 换成标记骨骼名，
  check 报 `vertex ... on bone 12`。

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

- 识别：Blacker 类（vtable `0x17DADB0`）且模型实例里有标记骨骼 `edf6vc_drill`；转动的是 `catapi_body`（§5.4）。模型实例内嵌在 veh+0xEE0（所有载具类都用这个偏移：
  `find_disp(0xEE0)` 在 505 的函数 `0x61A130 / 0x61A740 / 0x61AD70` 等里都是 `lea rcx,[reg+0xEE0]`，H），骨骼记录按名字查（`BoneRecord506`）。
  骨骼记录的布局（+0x08 标志、+0x70 local、+0xB0 world，0x110 一条）是引擎模型实例的（`0x1110FC0` 初始化、`0x1100010` 合成），
  与载具类无关；名字 `*506` 只是代码里先给 506 用的。(H)
- 转速：玩家按住（`seat+0x2E4 ≥ 0.8`）时以 `DrillMaxRpm / DrillSpinUpSec` 每秒上升，松开以 `DrillMaxRpm / DrillSpinDownSec` 下降，夹在 [0, 最高]。
  NPC 驾驶（没有扳机）：钻头碰到东西后 1.5 秒内视为按住（每 0.2 秒探测一次）。过热时一律视为松开。
- 热量（2026-10-05 新增，用户要求）：每秒 `+share × (1 + 0.5·钻到东西) / DrillHeatSec − (1 − share) / DrillCoolSec`（share = 转速 / 最高转速），
  夹在 [0, 1]。最高转速空转 `DrillHeatSec` 秒（默认 12）从冷到满，钻东西时快一半（8 秒）；停转 `DrillCoolSec` 秒（默认 8）从满到冷。
  到 1 即过热：不转、不咬，直到降到 `DrillResumeHeat`（默认 0.3）。玩家和 NPC 一样。过热 / 恢复各记一行日志（不受 Debug 限制）。
- 旋转：每帧（按 `GameFrame` 只走一次）把 `catapi_body` 的局部矩阵写成 `Rz(角度) × 绑定姿态`（行向量：第 0、1 行在自身平面内转，第 2 行即轴线不变）。
  画面上每帧最多转 `kSpinStepMost` = 一个外形重复（1/16 圈）的 0.4 倍（9°），按转速比例；原因见 §7。
- 接触（2026-10-05 第二次改，理由见 §5.5）：每 0.2 秒一次，全部在**车辆坐标**（veh+0x60：右、上、前三行 + 位置）里算。
  「钻头区」是一个箱子：x ∈ ±1.9 m（车身半宽）、y ∈ [0, 3.37 + 0.97 + 0.3 = 4.64]（地面到钻头顶）、z ∈ [3.0, 7.96 + 1.0]（车头到钻头尖前 1 m）。
  - 敌人：每个敌方锁定点（`VisitEnemies`）连同它所属物体的位置（object+0x90，脚下 / 根），这一段（各方向再加 `kBodyPad` = 1 m）
    与钻头区相交就算接触，取锁定点最靠近车头的那个；装药瞄准锁定点。
  - 地图：6 条地图射线（layer 22，`MapRay`），都从车辆原点所在的横截面（z = 0，车体内部）往前打到 z = 8.96：中线离地 1.2 / 2.3 / 3.37 / 4.2 m，
    两侧 x = ±1.3 m 离地 2.3 m；取最近的命中，瞄准点 = 命中点再往里 1 m。不低于 1.2 m：更低的线遇到前方的缓坡就会命中地面。
  - 装药起点：瞄准点的车辆坐标里，x 夹到 ±1.9、y 夹到 [1.2, 4.64]、z = max(瞄准点 z − 2, 3.5)（3.5 = 碰撞体前缘 3.4 之外，免得打到自己）。
  - 有敌人时打敌人（伤害 `DrillDamage`），否则打地图（`DrillBreak`），各乘 `转速 / 最高转速 × 0.2 s`；转速低于最高的 15% 或过热时只探测不咬。
- HUD：玩家开着钻头战车时，屏幕下方 80% 高度处显示「DRILL xxx RPM    HEAT xx%」，下面左边转速条（琥珀 = 加减速中，绿 = 满转速）、
  右边热量条（黄 / 70% 以上琥珀 / 90% 以上或过热红）；碰到东西时加 DRILLING，过热时整块变红并显示 OVERHEAT。
  NPC 开的钻头战车头顶的读数种类显示 drill。
- 依赖：钻头装药走喷气机模块的弹舱 / 炮弹函数（`InstallBay` 的签名）和地图射线（直升机模块的 `rayOk`），所以要求直升机模块的
  profile 通过（`HELI profile=1`）；装药要在本关预载（`JET preload ... drill charge 1`），没装 `EDF6VC_DRILL_CHARGE.SGO` 时每 10 秒记一行日志、不咬。

## 5. 2026-10-05 试玩（18:35–18:41，Debug=1）的分析

用户：「钻头不会转。显示转速 300 实际没伤害」「钻头的视角也怪怪的」。日志（`EDF6VehicleCrew.log`）：`CONFIG drill=1`、`HOOK drill trigger=1`、
`JET preload ... drill charge 1`、`HELI profile=1`、`HELI ray=1`；玩家 18:37:30 上车（`505_Tank ... seats=[P]`），HUD 有
`DRILL 300 RPM`，在 (-364,143) → (-272,74) 间开了两分钟；**没有任何其它 DRILL 行**。旧代码里会打 DRILL 行的地方只有三处：
`rewritten`（骨骼局部矩阵被别人改写，不受 Debug 限制）、咬一口时的 Debug 行（`Bite` 里 `Touch` 为真之后，不论装药是否发出）、
没预载装药的提示。所以：

- **局部矩阵没被改写**：每帧比对上一帧写进去的值，两分钟里一次不同都没有（M：运行时证据）。
- **`Touch` 一次也没返回真**（每 0.2 秒一次、转速 300，两分钟约 600 次）：既没有敌人锁定点进入范围，地图射线也一次没命中。

### 5.1 钻头为什么看上去不转

静态核对（H）：
- 505 的模型实例与 506 同在 veh+0xEE0：505 的第 45 槽 `0x61AD70` 在 `0x61AE51` `lea rcx,[rbx+0xEE0]` 后调 `SetWorld 0x1100B90(inst, veh+0x60)`。
- `SetWorld` 写根骨骼 world 后 `update(0)`（`0x1100010`）：对根的整棵子树、标志 `rec+8 == 1` 的每根骨骼算 `world = local × parent.world`；
  标志由 `0x1110FC0` 初始化为 1；在含 `imul …,0x110`（按骨骼记录下标寻址）的函数里扫 `mov byte [r+8],…`，只有 `0x175690`（写 1）和
  `0x678E30`（写的是另一种 0xE0 一条的结构），没找到清零它的地方（M：扫描只覆盖这类函数）。
- 第 45 槽每帧由 VehicleBase 的更新（第 5 槽 `0x630250`，`0x63028C call [rax+0x168]`）调用；我们在第 55 槽（输入）之后写局部矩阵，
  局部矩阵不被改写，所以下一次第 45 槽一定把旋转合进 world（最多晚一帧）。505 的第 45 槽另外只动 CAS（`veh+0x1170`）和炮塔
  （`0x661C00 / 0x661810`），不碰这根新骨骼。

所以写骨骼的位置、偏移、时机都没有问题。实测的原因在**画面**：钻头外形每 1/16 圈重复一次（`pylib/drill_model.py repeat_share`：
转 1/16 圈后 100% 的顶点落在原顶点上，1/2、1/4、1/8 也是；连贴图坐标一起比则每 1/4 圈重复），300 转/分 × 60 帧 = **每帧 30°，
是一个外形重复（22.5°）的 1.33 倍**：看到的是刃纹每帧往前爬 7.5° 或来回闪，贴图纹样（90° 重复）3 帧一循环，看起来不像在转。
修法：画面上的转动每帧最多 `0.4 × 22.5° = 9°`（始终小于半个重复，人眼看到的就是向前转），按转速比例；转速本身不变。
Debug 新增姿态日志（每 3 秒）直接对照「写入的角度」和「引擎合成的 world 里的角度」，若后者不跟随，说明合成这一步另有问题。

### 5.2 为什么一次也没接触到

- 旧的地图射线**从钻头根部（车辆前方 4.19 m）出发**。车的碰撞体前缘约 3.4 m、车头外形 3.0 m，钻头没有碰撞体，所以车头顶住墙时
  钻头根部已经在墙里 ~0.8 m。射线起点在建筑形状内部时找不到这个形状（凸形状的射线从内部出发不报命中；网格背面也不保证），
  所以顶着墙永远「没碰到」。装药也从根部出发，同理会从墙里飞出去碰不到墙。(M：几何事实 H，Havok 从内部出发的行为按惯例推断)
- 修法：射线从**车身中心上方的轴线点**出发（车身挡着墙，那里不会在地图里），一直打到尖端前 1 m；装药从接触点前 2 m、但不早于车头出发。
- 轴线和装药都改从车辆矩阵算，不再依赖骨骼世界矩阵（§4）。
- 敌人：判定本身（锁定点离轴线 3.47 m 内）对地面上的蚂蚁类够得着（锁定点约 1 m 高，离 3.37 m 的轴线约 2.4 m）。日志里没有敌人
  接触，最可能是这两分钟里车头没有顶到敌人（玩家下车后 HUD 才显示附近有 crawler）。新的 Debug 日志每秒记一次最近的敌人离钻头多远，
  下一次试玩可以直接看出是「没靠近」还是「靠近了也不算」。
- 岩石：钻头最低处离地 2.4 m（下沿射线 2.6 m），更矮的石块钻头物理上够不着，只有车身顶上去（README 已写明）。

### 5.3 新增的 Debug 日志（每辆钻头车分别限速）

- （§5.5 起的格式）`DRILL v=… touch: nothing (… rpm, heat …%): no map hit on 6 rays (0..9.0 m along, 1.2..4.2 m up); N enemies seen, the nearest body X m from the drill box (lock point a aside, b up, c along); box x +-1.9, y 0..4.64, z 3.0..8.96, body pad 1.0`（每秒最多一行）
- `DRILL v=… touch: enemy|map at (x,y,z) = a aside, b up, c along (its body in the box|centre ray|side ray), charge from (…); … rpm[ (too slow to bite)], heat …%[ OVERHEATED: no bite]`（每秒最多一行）
- `DRILL v=… bite: enemy|map, D damage, charge fired|NOT fired (n fired, m not)`（每秒最多一行；没发出时每次都记）
- （§5.4 起的格式）`DRILL v=… pose: … rpm, written A deg, spin bone world B deg, drawn pose C deg|n/a (marker world D deg); bone origin X m off the drill base, its axis . forward F`（转动时每 3 秒）
- 不受 Debug 限制：`DRILL v=… overheated (player|NPC|empty): it stops until it cools to 30%`、`DRILL v=… cooled to …%: it turns again`。

### 5.4 2026-10-05 19:55–19:57 试玩：骨骼的矩阵在转，画面上的钻头不转

用户：「钻头没伤害，视角太矮，并且钻头没转起来」。日志（`EDF6VehicleCrew.log`，从 `19:54:55 EDF6VehicleCrew 0.7.1 loading` 起）：
姿态行 `300 rpm, written 47 deg, the engine's world 38 deg`、`written 218 / world -151（= 209）`、`written 112 / world 103`：
新骨骼 `edf6vc_drill` 的**世界矩阵确实每帧跟着写入转**（晚一帧，差一步 9°），骨骼原点离钻头根部 0.03～0.16 m、轴向点积 1.00；
局部矩阵没被改写（没有 `rewritten`）。所以问题不在写骨骼，而在**画面用的不是这份世界矩阵**。

离线核对模型（`python tools/make_drill.py --out <wt>/tmp/out` 后逐网格统计蒙皮）：网格 0（`edf6vc_drill_c`，flags `00010100`，
与原版 Blacker 网格 5 同为单影响、同一 shader `snd_BRDF_Common_Basic`、同一 vsize 60 顶点格式）里 33458 个顶点在 `body#2`、
4882 个在 `edf6vc_drill#47`；另两个网格全在 `body`。**钻头几何确实只蒙到钻头骨骼**，混用骨骼的网格和原版一样（原版网格 6 一份网格
里混着 body / cannon_main / 轮子等 30 根骨骼）；蒙皮索引是全局骨骼序号，原版 369 个模型里用到的最大序号 125（无 48 之类的上限）。(H)

静态逆向画面姿态（H 为指令可见）：
- 模型实例（veh+0xEE0）除了骨骼记录（+0x10）之外还有一个**每骨骼一个 4×4 的「画面姿态」数组**：+0x28 处的向量，数据指针 +0x30、
  个数 +0x40；`0x11002D0`（实例初始化）把它按骨骼数分配并填单位阵。(H)
- 蒙皮调色板 `0x11009A0(inst, …, list)`：对每根骨骼算 `记录+0x30（逆绑定）× list.data[i]`，转置成 3×4 后经 `0x113F860` 上传；
  调用方 `0x1102ED0` 和 `0x1104100` 传的 list 都是 `inst+0x28`（AnimationModel 第 2 槽 `0x6C2C10`：`byte [this+0x482]` 为 0 时走
  `0x1104100`，否则直接用 `this+0xA0` 的实例和 `this+0xC8` 的 list；按偏移推算 veh+0xE40 处是这个 AnimationModel，M）。(H / M)
- 往画面姿态里写的一条路：`0x1100D50` 把每条记录的 world（`rec+0xB0`）抄进渲染命令，回调 `0x1100E90` 在渲染线程把它整块拷进
  `inst+0x30`。`0x1100D50` / `0x1103C80` 没有直接调用者，也不在任何 vtable 里，是谁、在什么条件下调用它们**没有跟到**（L）。
- 所以「记录的 world 在转，画面不转」只能是：这辆车画面姿态里钻头骨骼那一项不是从记录 world 来的（或不是每帧来）。
  旁证：喷气机的升降副翼（`elevon_L/R`，同样是我们新加、不在 CAS 骨架里的骨骼，同样写局部矩阵）用户也说「没看到动」
  （a8dd36f 当时归因于舵面太小）；而喀秋莎抬发射架写的是原版骨骼的局部矩阵，用户看到了发射架抬起（还看出了液压杆分离）。
  这辆车的动画 `v505_tank.cas` 里有完整的原版骨骼名表（49 根，含 `catapi_body`），没有 `edf6vc_drill`。
  推断（M）：**画面姿态只覆盖 CAS 骨架里有名字的骨骼**，新加骨骼一直按单位阵 / 绑定姿态画，于是钻头在原位不动。

修法（根因在「几何挂在一根画面不更新的骨骼上」）：钻头几何改蒙到原版骨骼 `catapi_body`。它在 CAS 骨架里；这辆车没有履带物体，
它本来不带任何几何；SGO（`car_base_*`、`tank_caterpillar_animation`、`vehicle_*`）、ragdoll（`Ragdoll_v505_tank.shkt`）和
EDF.dll 的字符串里都没有它的名字（H，逐个搜过），所以挪动它不影响物理、履带动画和任何代码。**更正（2026-10-06，§5.6）：漏查了 SGO `ragdoll` 第二项的物理绑定，那里有一行 `RagDollProxys.body -> catapi_body`。**它的 14 个子骨骼改挂 `body`，
绑定世界矩阵不变，钻头转时它们不跟着转。`edf6vc_drill` 留作标记骨骼（认车用），不带几何。
仍需实机确认（L）：钻头是否转；`catapi_body` 是否被 CAS 片段（`default` / `fire`）的轨道每帧改写（会记 `its spin bone's local matrix was
rewritten`）。Debug 姿态行同时打印 `spin bone world`、`drawn pose`（`inst+0x30` 数组里该骨骼那一项）和 `marker world`：
若钻头仍不转，看 `drawn pose` 是否跟着 `written` 走——跟着走说明画面姿态不是这个数组，不跟说明它没被更新。

### 5.5 为什么撞进敌群也没伤害

19:56:17–19:56:21 玩家 300 rpm 开进蚁群，`touch` 行：`28 enemies seen, the nearest 6.1 m from the drill (1.7 m along)`、
`5.5 m (1.1 m along)`、`6.5 m (-0.3 m along)`、`8.0 m (-2.1 m along)`，可达距离 3.5 m，一次接触都没有。
旧判定是「锁定点离钻头轴线 ≤ 0.97 + 2.5 m」，而轴线离地 3.37 m，地面敌人的锁定点约 1 m 高，正好在钻头下方的敌人离轴线也有
2.4 m 以上，稍偏一点就超过 3.5 m；那几次最近的敌人在车辆原点附近（along 1.1～1.7 m），离轴线约 4.5 m，在车身侧前方。
钻头往前顶的时候，真正被「钻」的是车头前方从地面到钻头顶这整块空间里的东西。所以改成 §4 的钻头区箱子，敌人按身体（脚下到锁定点
这一段，加 1 m）算。仍需实机：箱子是否偏大（车头前 6 m 内地面上的敌人都会挨钻）、侧面贴车身的敌人不算接触是否合适。

### 5.6 2026-10-06：0.8.0 试玩「钻头的模型依旧有问题」——钻头被物理绑定拉进车体

离线实跑生成器（`python tools/make_drill.py --out <wt>/tmp/drill`）后用 `ragdoll_fit.problems(模型, Ragdoll_v505_tank.shkt, SGO 的绑定)`
核对，只有一条不一致（H，数据）：

    bone catapi_body is at (-0.000, 3.368, 4.194), its proxy RagDollProxys.body draws it at (0.000, 0.881, 0.000)

V505_TANK.SGO 的 `ragdoll` 第二项（`animation_from_ragdoll`，按名字）有 44 行，其中 `RagDollProxys.body -> catapi_body`，偏移 (0, 0.881, 0)
（原版 Blacker 的 `catapi_body` 就在那里）。每帧 `0x6EDCA0` 把动力学刚体的世界矩阵乘偏移写进这些骨骼的世界行，并清掉骨骼的组合标志
（`docs/artillery-re.md` §2，H）。所以 §5.4 把钻头挪到 `catapi_body` 以后：

- 画出来的钻头 = 顶点 × 逆绑定（−钻头根部）× 车体刚体 ∘ (0, 0.881, 0)：整根钻头被平移 (0, −2.49, −4.19)。实算钻头 4882 个顶点的包围盒，
  应在 x ±0.97、y 2.40～4.34、z 4.20～7.96（车头前），被画在 y −0.09～1.85、z 0.00～3.77（车体 z −2.78～4.26 之内，还略低于地面）：
  **钻头埋在车体里**，只有尖端可能从车头下部露一点。
- 插件写的局部矩阵不参与（世界矩阵直接由刚体写入、标志被清），钻头仍然不转；它不改写局部矩阵，所以插件也记不到 `rewritten`。

修法（`tools/make_drill.py free_spin_bone`）：钻头战车自己的 SGO 去掉这一行，其余 43 行和 `ragdoll_from_animation` 原样（stock 绑定先核对能原样写回）。
`catapi_body` 于是像普通动画骨骼一样按「局部 × `body` 的世界」组合：钻头画在车头前、按插件写的角度转。`check()` 在有 Root.cpk 时
用 `ragdoll_fit.problems` 核对物理绑定把模型每根骨骼都画在它的绑定位置（变异：不去掉这一行 → check 报上面那条）。selftest
`drill_spin_bone_free_of_the_ragdoll` 在原版 Blacker 骨架上重放（挪动 `catapi_body` → 原版绑定报错，去掉那一行 → 一致）。

其余系统检查（离线，无异常）：三个网格的绕序与法线一致（几何法线与存储法线 100% 同向，体积为正，和原版 E551 相同约定）；
UV 已按游戏的左上原点翻转、贴图渲染位置正常（`pylib/model_view.py --color @tex`）；钻头在 +Z（车头），车底贴地（最低 −0.00）；
蒙皮只到 `body` / `catapi_body`。无法离线判断：铁雨模型导出时是否镜像（贴图上没有文字可对照；钻头和车体基本对称）。

仍需实机（L）：钻头是否画在车头前并转动（Debug 姿态行的 `drawn pose` 应跟着 `written` 走）；`catapi_body` 的组合是否用到本帧车体的
世界矩阵（若用上一帧，高速行驶时钻头会落后车体一帧）。

### 5.7 实际截图的上下两个钻头：还漏了 CAS 的绝对位置

2026-10-06 新截图显示车头下方的小锥体和上方露出的巨大扇形座。读取实际安装的 MRAB/SGO（只读）确认：
只有一份锥体几何，且 §5.6 的 ragdoll 行已经删除，仍然出错。18:54:51 的日志另有
`its spin bone's local matrix was rewritten by the game between frames`。

Root.cpk 的 `V505_TANK.CAS` 内嵌 CANM 0x300；`default` 和 `fire` 两个片段都向 `catapi_body` 写绝对局部位置
`(0, 0.8808, 0)`，不是相对 MDB 绑定姿态的增量。新绑定是 `(0, 3.36818, 4.19409)`。
将这份真实动画应用于实际 MRAB，可以重现截图：锥体被向下向后移动，车顶留下暴露的扇形座。
因此 §5.6 的静态绑定一致性检查不足以证明实机位置正确。

生成器现在同时生成 `OBJECT/EDF6VC_DRILL.CAS`，SGO 引用它。两段动画移除插件接管的 `catapi_body` 轨道，
保留其骨骼名表项；其它改过父级的骨骼按新旧局部绑定差值重定位，其余动画的关键帧和运动幅度不变。
插件从 MDB 取得正确的初始局部矩阵，后续旋转不会再被原版轨道拖回履带根部。

回归：`python tests/test_cas_pose.py` 是不需要游戏的二进制边界/共享通道/运动幅度测试；
`python tools/vehicle_cas_check.py --out build/vehicle-cas-review` 用真实 Root.cpk 和用户 OBJ 生成最终 MRAB/SGO/CAS，
读取生成产物回放旧/新动画并输出前后预览。它要求显式输出目录，拒绝写入游戏目录。
尚需游戏内复核实际旋转及最终渲染；离线回放验证位置修复，不声称完成实机 E2E。

## 6. 镜头（`tools/make_drill.py CAMERA`）

`game_object_camera_setting` 是 `[向量0, 向量1, 1, 0.1]`，解析在 `0x54DDF0`（向量 0 → obj+0x170，向量 1 → obj+0x180，第 4 项 → +0x190）。
向量 0 按注视中心（车辆坐标）理解；**向量 1 是镜头位置还是相对注视中心的偏移没有逆向确认**（L；另一个分支 feat/artillery-high-cam 在查）。

原版载具（Root.cpk 只读；车高按各自模型绑定姿态的顶点量）：

| 载具 | 车高 m | 向量 0 | 向量 1 | 注视中心 / 车高 |
|---|---|---|---|---|
| Blacker（505） | 2.6 | (0,4,0) | (0,4,-15.5) | 1.53 |
| Grape（403） | 3.0 | (0,4,0) | (0,4,-15.5) | 1.32 |
| 救援车（507） | 6.7 | (0,8,0) | (0,8,-15.5) | 1.20 |
| Titan（404） | 9.2 | (0,8,0) | (0,8,-25.5) | 0.87 |
| 502 机器人 | 3.0（绑定姿态） | (0,5,0) | (0,7,-10) | — |
| 摩托 / 汽车 | 1.3～3.6 | (0,0.7,0) | (0,3,-13.5) | — |

坦克类都是「向量 1 与向量 0 同高」；按「位置」读是平视，按「偏移」读是镜头比注视中心再高一倍。

钻头战车车高 4.6 m，钻头轴线 3.37 m、尖端 z 7.96，车头上沿 (y 4.6, z 3.0)。之前 `(0,5.5,0)/(0,5.5,-18)`、`(0,5.7,0)/(0,10.5,-17)` 用户都说太低
（注视中心只有车高的 1.2 倍）。新值 **`(0,7.5,0) / (0,12.5,-17)`**：注视中心 = 车高 × 1.63（高于原版坦克的比例），`camera_check()` 在生成时
按两种读法各算一遍（+ = 在画面中心以下）：

| 读法 | 镜头离地 | 下俯 | 镜头到注视中心 | 视线越过车头上沿 | 钻头尖 | 钻头尖下的地面 | 车尾上沿 |
|---|---|---|---|---|---|---|---|
| 位置 | 12.5 m | 16.4° | 17.7 m | 0.58 m | +3.7° | +10.2° | +14.1° |
| 偏移 | 20.0 m | 36.3° | 21.1 m | 2.07 m | −2.7° | +2.4° | +12.6° |

要求：注视中心 ≥ 1.6 × 车高、视线越过车头上沿 ≥ 0.3 m、钻头尖 / 钻头尖下的地面 / 车尾都在画面中心 ±28° 内（竖直视场按约 60° 估计，L）。
旧值按「位置」读视线只比车头高 0.19 m，钻头尖下的地面在中心以下 7°。

## 7. 静态核实 vs 需要实机

静态核实（H）：
- 505 输入读扳机的位置与字节（`0x61AD14`），清零扳机能挡住原版开炮；
- GDI 破坏建筑位的来源（爆炸 ≥ 3 m、`is_break_building` 等），SolidBullet01 不改这个位；
- 地图物体第 13 槽按这个位扣耐久、耐久初值来自定义结构 +0x18；
- veh+0xEE0 是所有载具类的模型实例；505 第 45 槽每帧 `SetWorld` 合成全部骨骼的 world（§5.1）；
- 钻头外形每 1/16 圈重复（生成时 `repeat_share` 核对 > 0.99）；
- 离线：`python tools/make_drill.py --out <wt>/tmp/out` 用 Root.cpk + OBJ 生成全部 4 个文件，`check()`（含 `camera_check`、钻头根部位置、重复数）全部通过；
  `python tools/selftest.py` 全部通过（`drill_copies_agree`：C++ 的骨骼名、长度、半径、根部位置、重复数与 Python 一致）；
  `build.cmd`（/W4 /WX）两个 DLL 都链接成功。

2026-10-05 第二次修改（§5.4–§6）的离线核实：`build.cmd`（/W4 /WX）两个 DLL 链接成功；`python tools/selftest.py` 41/41
（`drill_copies_agree` 另核对 `kSpinBone` = `SPIN_BONE`、钻头区不宽于车身、装药起点在碰撞体前缘之外）；`python tools/make_drill.py --out <wt>/tmp/out`
生成并通过 check（钻头 4882 个顶点全在 `catapi_body#12`，车体全在 `body#2`，其余 48 根原版骨骼的绑定世界矩阵与原版逐位相同；
把 `SPIN_BONE` 换成标记骨骼名后 check 报错）；`camera_check()` 两种读法都通过。

需要实机验证（L / 未测）：
1. 钻头是否看得见地旋转（§5.4：蒙到 `catapi_body` 后；姿态日志里 `spin bone world` / `drawn pose` 是否跟着 `written` 走，有没有 `rewritten`）；9°/帧上限的观感。
2. 顶着建筑 / 岩石时是否出现 `touch: map ... axis ray hit` 和 `bite: map ... fired`，建筑是否倒、岩石是否碎；`DrillBreak` 600/s 的快慢。
3. 冲向敌人时是否出现 `touch: enemy ... (its body in the box)`，敌人是否掉血、击杀是否记在玩家名下；装药伤害是否再被难度系数乘；钻头区（§4）大小是否合适。
4. 热量：最高转速空转约 12 s 过热、过热后停转、冷到 30% 恢复，HUD 的热量条和 OVERHEAT；NPC 车同样过热。
5. 装药爆炸的冲击力是否把钻头战车自己顶开，爆炸特效每 0.2 秒一次是否太吵；装药从车头前出发时是否会撞到自己的车身（理论上车头前已在碰撞体外）。
6. 新镜头 `(0,7.5,0) / (0,12.5,-17)`：是否够高、整车和钻头前方地面是否在画面里、镜头是否穿进地形 / 建筑；向量 1 是绝对位置还是相对偏移（§6）。
7. 模型：0.8 倍是否合适、履带是否和 Blacker 的车轮 / 碰撞体对得上（贴地、不浮空）、深钢灰履带的观感。
8. Blacker 的炮塔 / 炮管碰撞体（`cannon_main`、`cannon`）仍在、随右摇杆转动但看不见；镜头跟着看不见的炮塔转，与车头（钻头）朝向无关。
9. NPC 驾驶时 AI 若开火，只会打出看不见、没有伤害的「钻头」子弹（`EDF6VC_DRILL_BIT.SGO`）。2026-10-05 的试玩里两辆 NPC 钻头车一直停在原地没开动（与钻头无关，地面 AI 的问题，未查）。
10. 联机：装药只在开车的那台机器上生成（插件的其它装药同样如此）。

## 8. 2026-10-06：回旋镖发射、击杀降温、放大到原尺寸

用户：「钻头可以发射喷气的那种然后射完回收类似回旋镖攻击……同时把钻头车放大一点吧」，随后「热量不删了，改成击杀减热量。
然后把热量的累计速度或者上限调高，现在太低了」。

### 8.1 放大（`pylib/drill_model.py SCALE`）

- `SCALE` 0.8 → **1.0**（铁雨原尺寸）。实测（`drill_axis` / 车体顶点）：钻头长 4.71 m、根部半径 1.21 m、根部 (0, 4.21, 5.18)；
  车体宽 4.77 m、高 5.74 m、前后 −3.54～3.84 m（不含伸进钻头根部的传动轴）。C++ 副本（`kDrillLength` 等）、接触箱（半宽 2.4 m）、
  6 条地图探测线（离地 1.2 / 2.9 / 4.21 / 5.2 m，两侧 ±1.6 m）和镜头 `(0,9.4,0)/(0,15.6,-21)`（按 1.25 倍放大）一起改，
  `camera_check()` 两种读法都通过（视线越过车头上沿 0.83 / 2.77 m）。
- **碰撞体没有放大**：仍是 Blacker 的 `Ragdoll_v505_tank.shkt`（约 6.8 × 3.2 m）。车身两侧比碰撞体各宽约 0.8 m、车头长约 0.3 m，
  贴墙时这部分会穿进墙里（§2 当初取 0.8 倍正是为了避免这个）。要根治得像 `pylib/sidecar_model.py build_collision` 那样
  按轴缩放凸包并重建包围树，同时核对 `car_base_constraint_data` 等按骨骼名的约束，本次没做（L：放大后的履带 / 车身与碰撞体是否违和需实机看）。
- `check()` 的钻头圆柱容差从 1 mm 改为一个半精度格点（`STORED_STEP` = 1/256 m）：顶点位置以半精度存储，4～8 m 处间隔 1/256 m。
  0.8 倍时恰好没超；1.0 倍时一个刃边顶点（5.33 m 高）存成后比半径多 1.2 mm，旧容差误报。

### 8.2 发射（`src/drill.cpp` `Launch` / `FlyOut` / `FlyBack`）

- 输入：键盘 `DrillLaunchKey`（默认 R）/ 手柄 `DrillLaunchButton`（座位按键位 0x08 = Y），按下沿触发。插件在 505 驾驶位上没有别的模块读 Y；
  原版 505 的输入（第 55 槽 `0x61ACD0`）是否读 Y 未逆向（L）。键盘读取经 `MapHoldsKeys()` 给地图让路（selftest `map_wired` 已登记）。
- 去程：从钻头在车上的位置沿车辆前向，以 `DrillLaunchSpeed`（70 m/s）出发、匀减速，到 `DrillLaunchRange`（60 m）停住。
  每帧一条地图射线从钻头尖上一帧的位置打到下一帧的位置（第一条从车辆原点上方开始，和 `kRays` 一样，车头顶墙时也能碰到）；
  命中就在命中点放一发钻头装药（`DrillBreak` × 0.4 的破坏）并立刻返回。
- 回程：从静止匀加速到 `DrillLaunchSpeed`，直线追向「钻头在车上的位置」（车在动也追得上）；距离 ≤ 1 m（或一帧步长）即接住；
  超过 12 s 强制复位。轴向：去程 = 发射方向；回程最后 15 m 内逐渐转回车辆前向，接住时与车上的姿态一致。
- 画面：`catapi_body` 是 `body` 的叶子（§5.6），插件把钻头的世界位姿（原点 = 钻头根部、+Z = 轴向、x = 车辆上方 × 轴向，再按 `angle` 自转）
  乘 `body` 世界矩阵的逆写成局部矩阵（`exhaust::Inverse` / `Mul`）。`body` 的世界矩阵取最近一次合成的（M：最多晚一帧，
  高速行驶时飞行中的钻头可能有一帧抖动）。找不到 `body` 骨骼时不发射（记一行日志）。
- 喷气：`FlareFlames`（照明弹的 Booster 火焰，`booster.cpp`），飞行中每帧在钻头尾端刷新、方向逆着运动方向（去程在根部，回程在尖端），
  接住时传 0 个让火焰熄灭。依赖 booster 的签名核对（`sigOk`）；失败时只是没有火焰（L：火焰大小 4 m / 1.5 m 是照明弹的，观感需实机看）。
- 飞行伤害：每 0.1 s 找身体（脚下～锁定点）离钻头轴线段最近、且在半径 + 1 m 以内的敌人，打一发钻头装药（`DrillLaunchDamage` = 800），
  装药从敌人往钻头方向退 2 m 处出发。离车辆原点 7 m 以内的敌人飞行中不咬（装药可能从车里出发或碰到自己的车）。
  飞行中车头的近战探测暂停；钻头保持最高转速（`held`），照常升温。NPC 不发射。

### 8.3 热量

- `DrillHeatSec`（默认 12 s）改名 **`DrillOverheatSec`**（默认 **30 s**）：安装器的 `merge_ini` 只补新键、不改玩家已有的值，
  只改默认值的话已安装的 ini 还是 12；换新键后旧键进 `IgnoreRetired`（读到时记一行「不再读取」）。钻东西时约 20 s 过热。
- 每次发射 +`DrillLaunchHeat`（12%）；过热时不能发射。
- **击杀降温**：插件拿不到装药的击杀回报，所以按「被钻头咬到的敌人在 1.5 s 内死亡」认定为钻头的击杀（每辆车记最近 16 个），
  每次 −`DrillKillCool`（10%）；过热中降到 `DrillResumeHeat` 以下即恢复。死亡判定：控制块计数归零、对象被复用、`kDead` 或 HP ≤ 0。
  （M：1.5 s 内被别的东西打死的敌人也会算进来；Debug 每次击杀记一行 `DRILL v=… kill N: heat a% -> b%`。）

### 8.4 离线核实 / 需要实机

离线：`build.cmd`（/W4 /WX）两个 DLL 链接成功；`ctest` 46/46；`python tools/selftest.py` 105/105（新增 `drill_settings_documented`：
所有 `Drill*` 键在 ini / plugin.cpp / README 一致、`DrillHeatSec` 已退役）；`python tools/make_drill.py --out <tmp>` 用 Root.cpk + OBJ
生成 1.0 倍模型并通过 `check()`。

需要实机（L / 未测）：发射后钻头是否画在飞行位置并在转、回程是否顺滑接住；火焰的位置与大小；飞行中咬敌人是否掉血、击杀降温是否生效
（Debug 日志 `launched` / `turns back` / `back on the hull` / `kill N`）；撞墙返回是否在墙上留下破坏；放大后车身穿墙的程度是否可接受；
Y 键是否与原版 505 的某个操作冲突。

> 上述 §8.1/8.2 的 4.71 m 钻头和尖端回程喷气是旧实现；2026-10-08 实机反馈后的完整模型与回程方向修复见 §9。

### 8.5 联机发射与姿态复制（审查修复）

旧实现只有本机 R/Y 把 `launchAsked` 置位，远端复制的车辆输入里没有这个插件动作；`OnlineShotCounts` 只能避免重复伤害，不能让其它机器的钻头离开车头。

`src/drill_net.cpp` 使用原版车辆的 NetworkObject（车辆 `+0x120`）发送，不另开连接。505 的 NetworkObject vtable 接收槽
`0x17DB0D0`（slot 17）原指向 `0x6325B0`，发送槽 slot 16（`+0x80`）为 `0x773DA0`，经原版事件 7 和对象描述符送到同一辆车的副本。
仅链这个接收 vtable 槽；coop 的 GameObjectBase `0x54D770` 中间钩子不改。消息类型 **15**（coop 的伤害 13、RNG 14 原样交给下一接收函数）
后接固定 96 字节版本化状态块。原版未安装插件的接收器对 15 只读类型后返回，不修改车辆。

状态含进程随机 sender、单调序号、当前注册驾驶员的原版 ReferenceId、去程/回程/车头阶段、世界位置/方向/轴、速度、自转角、RPM、热量、
过热锁、已飞距离和回程经过时间。对象身份由原版描述符路由；本地状态绑定 `ObjRef`（地址 + weak-this 控制块），对象地址复用重新建状态。
驾驶员身份只取 seat 0 当前 weak（`+0x260`）中的活跃注册驾驶员；空座、过期 weak 和无网络身份的 NPC DummyVehicleRider 都是 host 控制纪元 `-1`，
不读末任 weak（`+0x300`）。共享 `IsOnlineAuthority` 与 coop W3 同样改为「无活跃注册驾驶员即 host」：主动覆盖原版保留末任驾驶员的插件工作规则，
否则 host 的 Dummy 与客机的空座 + 本机末任驾驶员会同时宣称权威。玩家 42 下车后双方 epoch 为 `-1`，其迟到包即使序号更大也被丢弃；
玩家 99 上车后 epoch 为 99，之前 host 的 `-1` 包也不能覆盖新驾驶员。
`0x785050` **按值消费** weak_ptr（`0x78511A..133` 释放 weak count）：传本地增持的副本，绝不把座位里的 weak 原地交给它释放。

只有 `IsOnlineAuthority(vehicle)` 所选机器模拟飞行、判地图碰撞、产生装药；伤害仍走 `OnlineShotCounts`。其它机器 `DrillFrame` 每帧直接画复制姿态和喷气，
不依赖 `DrillInput` 是否认出本机玩家，也不独立判碰撞。阶段变化立即发送；飞行每 50 game ms 发送绝对快照，车头每 500 game ms 补送，
所以缺发射、中间状态或末次接住消息都能从后续快照恢复。状态包含接管所需积分量，换驾驶员时新 authority 能继续已有飞行。
接收先检查会话、505 钻头模型、本机非 authority、当前驾驶员纪元、字段有限值和显式保留位为 0，再按 sender 的序号拒绝重复/乱序；每辆车保留最多 1024 个 sender 水位（与 coop 房间容量一致），驾驶员回来时保留原 sender 水位，
旧驾驶员的迟到包不能盖过新驾驶员的状态。不在会话或没有注册网络身份时保留原有本地路径；会话退出清飞行、待发射输入和接收水位，任务 reset 清整个 ObjRef 状态。

验证分层：`drill_net_test` 检查格式/边界、重复乱序、驾驶员切换、退出、序号回绕和单端伤害；`drill_sync_test` 直接执行生产
`DrillNetReceived / DrillFrame / PoseFlight`，检查远端无输入仍改真实骨骼记录、返回/灭火、丢 catch 后补送、对象复用等；
`online_authority_seat_test` 直接执行生产 seat/weak 读取，用 host Dummy / client 空座 + 活跃末任玩家的不同内存状态验证两端只有一个 authority；
`drill_net_native_test <EDF.dll>` 以 `DONT_RESOLVE_DLL_REFERENCES` 在独立测试进程映射游戏 DLL（不运行入口，不附加游戏，不写磁盘），
实跑原版消息读写、生产发送/接收、505 对 tag 15 的忽略和 13/14 透传，以及真实 `0x785050` 退出支路对 weak_ptr 的消费（有未增持的负对照）。
离线验证不等同于实际两机房间画面/延迟/伤害 E2E；所有需要显示飞行的机器都要安装此版本。

## 9. 2026-10-08 实机反馈：最后一节、回程朝向与发射提示

**资源根因（已读安装产物验证）**：当日已装 `Mods/OBJECT/EDF6VC_DRILL.MRAB` 中，`catapi_body` 的顶点仅覆盖 Z=5.180..9.891，
`body` 却仍有 Z=5.266 的钻头顶点。用户 OBJ 的最大后锥是一个完整的 384 三角形连接部件，Z=3.345807..5.265679，轴心 Y=4.210223，
最远螺纹顶点半径 1.551222。原先按「三角形所有角的 Z>4.75」切分，把跨过该面的后锥大半留在车身骨骼上；前面四段锥体与尖端才随钻头转。

`drill_model.split` 现在按焊接位置连接部件整体归属：部件前端越过选择面即整件属于钻头，不从中间切断后锥。
完整六部件（后锥、四段前锥、尖端）全部 100% 绑 `catapi_body`；完整轴参数同步到模型与生产 C++：base=(0,4.21,3.35)、length=6.55、radius=1.55。
尖端位置仍约 Z=9.89，车身与原版碰撞体没有向前挪动。重新生成的 MRAB 中旋转骨骼顶点覆盖 Z=3.346..9.891；静态车身前端退回真实 Z=3.842。
需要用新生成器同时更新 MRAB/CAS/SGO；只换 DLL 不能修改旧 MRAB 已经错误的 skin 权重。本轮产物只生成到 worktree 的 `build-feedback/generated`，没有覆盖游戏目录。

**回程根因**：旧 `FlyBack` 始终用发射方向 `d.dir` 混向车辆前向，而 `Jet` 单独把火焰移到尖端，因此钻头依旧背对移动方向。
现在整体 `d.axis` 转向回程路径，`BlendAxis` 定义了恰好 180° 时的转动平面（不会线性混出零向量），角速度限制为 2π rad/s。
接近车身时按至少 15 m、并随回程速度扩展的对接距离逐渐恢复车辆前向，接住前已基本对正。火焰一直在真实后端 `d.pos`，用同一 `d.axis` 定向，
不再从根部跳到尖端。原有网络快照已包含 axis，远端 `PoseFlight` 与 `Jet` 使用同一轴，无另一路本地回程猜测。

**操作提示**：`DrillCue.keys` 由实际 seat 输入模式发布，普通载具 HUD 的钻头读数与独立 `DrillPanel` 都经 `DrillLaunchHint` 显示发射绑定。
键鼠取 `Cfg().drillLaunchKey`（含鼠标键名）；手柄取实际 EDF seat mask `Cfg().drillLaunchButton`，多按钮显示允许的按钮组合。不是硬编码 R/Y；
关闭 DrillLaunch 时不显示可用发射提示。四种 HUD 语言均有对应文本。

**回归证据**：`drill_model_rotation_test.py` 的跨切面合成部件在旧 split 下失败；新版通过。私有 OBJ 六部件、重新生成 MRAB 的每个顶点 skin、
真实 bind/inverse-bind palette 的六部件旋转全部通过，生成器自身 MRAB/CAS/ragdoll 检查也通过。`drill_sync_test` 直接执行回程 180° 转向、
生产骨骼写入、后端火焰、对接前向、网络副本同轴回放及输入模式 cue；`drill_hud_binding_test` 直接调用生产 HUD，覆盖改键 T、改手柄 X、多按钮、鼠标键及关闭开关。
离线 HUD 使用游戏字体渲染并检查中文提示/布局。尚未启动游戏重测，不能把这些资源与生产路径验证称为实机 E2E 已通过。
