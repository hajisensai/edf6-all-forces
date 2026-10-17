# 2026-10-17 4.1 赫克托（双足机器人）：移动、瞬移、攻击动作、外表动效

玩家反馈（原话，不可信数据，只当证据）：「4.1的双足机器人外表有的缺少动效，盾牌没有显示。双足机器人的移动方式不对，甚至会瞬移到地图外面，缺少攻击动画，有的武器是黑块」。

对象：4.1 赫克托 21 个变体（`HECTOR301*` / `HECTOR401*`），套 EDF6 类 `EDF6_Berserker_Large`（模板 `E602_BERSERKER_LARGE.SGO`），
生成器 `tools/make_edf41_objects.py` 的 `HECTOR_*` / `hector_alias`，插件 `src/edf41ai.cpp` + `src/hector_ai.h`。

**不在本文范围**：武器黑块、盾牌不显示 —— 根因是 MRAB 里武器 / 盾牌贴图排在本体模型之后，另一分支 `fix/edf41-map-hector-1017`
（ceb9d423，`docs/feedback-2026-10-17-edf41-map-hector.md`）已修。

全部地址：EDF.dll（TimeDateStamp 0x678CCB46）与 EDF41.exe 的 RVA，静态逆向（`tools/edfre.py`、同法读 EDF41.exe），没开游戏。
可信度：**H** 反汇编读到，**M** 推出，**U** 未核。

## 1. 瞬移到地图外 / 移动方式不对

### 1.1 委托方给的三个候选，逐个核对

- **MoveSpeed [6.0, 0.1] 的单位**：没有换算错。EDF6 `0x53D7CC` 速度 = 行走向量 × `MoveSpeed[0]`，与根运动 ÷ (1/60)（`0x53D842`，
  即 ×60 成每秒）加在同一个量上，交给角色控制器（`0x6DAF80`），所以是 **单位/秒**（H）；`MoveSpeed[1]` 是每帧的速度缓动（`0x53DBB5`，H）。
  4.1 Hector（`0x2216F0`）：局部速度每帧向 0.1 × 行走命令缓动 10 %，再 ×60（`0x221B30`）→ 命令 1.0 时 6 单位/秒（H）；命令是对象
  `+0x278`，GameObjectBase 构造给 1.0（`0x28D0EF`，M）。所以 [6.0, 0.1] 本身对。
- **后退在水平距离≈0 时归一化零向量得 NaN**：不成立。MoveTo（`0x33E3B0`）先比 `距离 > a` 才写行走向量（`0x33E563`），
  归一化前判零长（`0x33E5DB`），零向量不除（H）。
- **布娃娃倒地后 WakeupNode waist 重定位**：**成立，是瞬移的根因之一**（见 1.2）。

### 1.2 根因（事实）

1. **倒地起身 = 把整个对象搬到布娃娃腰骨的位置**。类在爆炸伤害 / SGO 耐久（`+0x182C` = `+0x2EC`，不随等级放大）超过
   `ExplosionDownBorder` 时倒地（`0x33F068..0x33F0EE`，H），`DownRagdollBodies` 变布娃娃；起身（`0x345CA0`）把对象位置 `+0x90` 和角色
   控制器直接设成 WakeupNode 骨（waist）的世界位置（`0x345E41..0x345E56`，H），再播 `wakeup_f / wakeup_b`（`0x33D34F` / `0x33D36D`
   查名），赫克托的 CAS 没有这两个状态（H：CAS 只有 sleep / wakeup / stand / walk_* / turn_* 和嫁接来的 damage / random /
   additive_action）。原来给的 `ExplosionDownBorder 10` = 爆炸伤害超过 10 × 1200 才倒，高难度一发大爆炸就能到（M）：布娃娃被炸飞多远，
   起身就瞬移到多远。4.1 的赫克托根本没有倒地 / 起身（EDF41.exe `0x21E9A4` 的状态表，H）。
2. **翻越障碍 = 先把对象放到障碍顶上**。模板的 `WalkingEnemyBase.ClimbOver`（高 20 / 40，`climb_over_small / _large`）被原样带进
   赫克托：行走中探测到前方障碍（`0x540C00`），进入翻越状态（`0x541701` → `0x53F6A0`），**先**用 `0x6DB060` 把角色控制器设到探测
   打中的障碍顶点（`0x53F764`，H），再播 `climb_over_*` —— 赫克托没有这两个状态。于是碰到 40 单位以下的障碍（建筑、地图边缘的墙）
   就瞬移上去（M：玩家看到的「瞬移到地图外」最可能是这条或 1）。4.1 的赫克托不翻越。
3. **转向慢 10 倍 → 侧着、倒着走**。EDF6 每帧把想要的偏航加 `TurnSpeed[0] × clamp(角差, ±0.1)`（FaceTarget `0x33E840`，两个行走状态
   都传 0.1；`0x53D5BD`），偏航再以 `TurnSpeed[1]` 每帧缓动（`0x53D63D`）（H）。4.1 是每帧 0.03 × clamp(10 × 角差, −0.85, 1)、偏航
   10 % 缓动（`0x223874..0x2238A6`、`0x221862..0x22198B`，H）。要一致 `TurnSpeed` 应为 **[0.3, 0.1]**；原来的 [0.03, 0.03] 最快只有
   0.003 rad/帧，转不过来，行走向量指向目标而身体朝别处，CAS 按行走向量对朝向播 walk_l / walk_r / walk_b（M：「移动方式不对」）。
4. **速度不是 4.1 的**。类的两个行走状态给 MoveTo 的速度：追击 0.125（`0x34DA46`，H）、跟随 (0.9..1.2) × linear_speed（`0x34EC6F..
   0x34EC9B`，H）。赫克托 × MoveSpeed 6 → 追击只有 0.75 单位/秒（4.1 是 6，H）。插件只在 `near` 内接管目标点、速度照用类的，所以近身
   前进 / 后退都是爬。4.1 后退是 −0.85 × 命令（`0x223832`、`0x2238BB`，H），旧注释写的「−1」不对。

### 1.3 修复

- `tools/make_edf41_objects.py`：`ExplosionDownBorder` 1e30（不再倒地）；`hector_alias` 的 `drop` 去掉 `WalkingEnemyBase/ClimbOver`
  （类的两处探测都先判这个节点为空就退出，`0x540C82`、`0x53CC00`，H）；`alias_dsgo` 的 `drop` 支持路径；`check_states` 新增
  `CLIMB_KEY`：ClimbOver 点名的状态 CAS 里没有就拒绝生成；`TurnSpeed` [0.3, 0.1]。
- `src/hector_ai.h` `Steer`：每帧都给 4.1 的速度 —— 前进 1、后退 0.85、站定 0、远处（类自己的目标点）也是 1、没目标也是 1；
  `src/edf41ai.cpp` `MoveToHook` 对每个赫克托都替换速度，接管时 `a = b = 0`（4.1 直线全速走到底，不减速）。

## 2. 缺攻击动作

### 2.1 根因（事实）

- **EDF6 的 Berserker 不为炮转任何骨骼**。`gun_control` 的 `chase_speed / swing_angle` 只移动炮台的世界瞄准点（每炮更新 `0x34F900`：
  `0x34F97D..0x34FB74` 写 GunBattery `+0x2D0` 并置标志 2），这个点只决定子弹方向（`0x6D2AAF`）（H）。`gun_battery.aiming` 的字段
  是 angle_limit_x / y、speed_max、fix_vector、is_aim_local_space 等（`0x6D59A0`，H），是射击方向相对炮口的限位，没有骨骼名
  （EDF6 自己用它的是 E604_RADON 的火焰 / 火球、E602_BERSERKER_LARGE_PUNCH 的 fix_vector；全扫 OBJECT/*.SGO）。
  原版 Berserker 看起来在「瞄」，是 CAS 里的 `attack_shoot_both / _lr / _rl` 状态（数据驱动，EDF.dll 里没有这些名字），赫克托的 CAS 没有。
- **4.1 的赫克托是武器对象自己转**。武器是挂在 `arms_l / arms_r` 骨上的独立对象（类 `Hector_WeaponBase`，各武器虚表 `0xA988E0..0xA98C10`），
  `0x22ADF0`：攻击开始（`0x2259D8`，`+0xF2 = 1`）到结束（`0x229FFB`）之间，把目标换到武器坐标系（`0x22AA90`）得
  pitch = −atan2(y, |xz|)、yaw = atan2(x, z)，pitch 限 [−π/2, 0.4π]，side 1（arms_r，`hector_weapon` 第三项）yaw 过 −0.6π 放弃（回 0）、
  过 0.7π 停住，side 0 镜像；每帧缓动 10 %；攻击之外每帧 ×0.95 回 0（H）。合并进本体模型后武器挂在 `arms_l / arms_r` 下面
  （`gun_arms_l*` 骨，`mdb.graft_rigid / graft_skinned`），没人转它们 → 开火时手臂一动不动。

### 2.2 修复（`src/edf41ai.cpp` `AimArms` + `src/hector_ai.h` 的 aim 部分）

- 在类的基础更新（`0x343DE0`，骨架在它开头由各骨局部矩阵建世界矩阵：`0x317B88` → `0x1100B90`，只写 `+0xB0`，H）之后：
  arms 骨的局部 `+0x70` 改成 Rx(pitch)·Ry(yaw) × 动画的局部；它和它下面所有骨（炮的骨；炮口定位点 砲台１/２ 就挂在 arms 骨上）的世界矩阵
  当场按同一变换移过去 —— 这一帧的炮更新、炮口、绘制都看得到，不依赖 CAS 下一次写局部和下一次建骨架谁先谁后。CAS 没重写局部时用上次记下的
  动画局部，避免累乘（与 UfoRobo 的瞄准骨同法）。
- 什么时候瞄：该臂的炮台在攻击 —— 用类自己每炮瞄准的那个判断（`0x34F913..0x34F94E`），且有目标；对象已毁（`+0x2E8`）则还原、停。
- 角度、限位、缓动、回零照 4.1（`hector::AimAngles / StepAim / AimRotation`），离线检查覆盖。

## 3. 外表缺动效

### 3.1 根因（事实）

- 4.1 每个赫克托部件（本体、炮、加特林、发射器、盾牌）各有两块材质动画：循环发光（480 帧）和死亡熄灯（180 帧，只播一次）
  （EDF41.exe `0x21F062`、`0x222A72`，M）。生成器原来只取本体的第 0 块，填满模板的 4 个槽。
- 武器网格并进本体模型后，材质名仍是 4.1 的 `ufosurface`，和本体同名。EDF6 按名字绑定材质动画，**只绑第一个同名材质**（`EDF+0x110D570`
  线性扫描、命中即返回，H），所以所有武器都不发光；401 盾牌玻璃 `ufoglass`（alpha + 滚动光）一个槽都没进；死亡熄灯块被丢掉。
- BerserkerBase 的槽：0 出生（`0x348D1D`）、1 接替 0（`0x343C70`）及死亡状态（`0x34B2DE`）、2 每次受击（`0x33EF81`）、3 死亡爆炸后
  模式 1 播一次（vtable 槽 48 `0x33F4A0` → `0x33F76F`）（H；原注释说槽 3 来自 `0x37ACDE`，那是 GrandMotherParts 的，已更正）。

### 3.2 修复（`tools/make_edf41_objects.py`）

- `hector_materials`：每件武器的材质改名为 `<名>.<武器模型>`（如 `ufosurface.hector401cannon`、`ufoglass.hector401shield`），
  `mtab_merge` 把本体和各武器自己的 4.1 动画块按新名字合并成一块（块内除顶层表外的偏移都是相对自身的，整块复制即可）；
  槽 0–2 放合并后的循环发光，槽 3（`BERSERKER_DEATH_SLOT`）放合并后的死亡块。`merged_model` / `missing_textures` 没动。
- `tools/selftest.py`：`edf41_hector_material_merge`（手工 MTAB，不需要游戏）；`edf41_hector_alias` 断言材质名唯一、每槽覆盖本体与武器、
  槽 3 为 180 帧不循环。

### 3.3 仍缺（未修，需要插件运行时驱动）

- 401 炮 / 发射器开火前后的 `open` / `close`（`0x227CBF`、`0x2280BA`、`0x229B69`、`0x229F0A`）、401 加特林转管（待机转速 1、开火每帧
  +0.01 到 5，`0x228C54`、`0x228F17`，M）、本体 `head` 播放器的 open / close（何时播 U）：这些是 4.1 武器类状态机播武器自带的 CAS，
  合并后 CAS 没带过来、Berserker 类也不会播。要补需插件按骨骼（`gun_arms_*_gun_rolling` 等）驱动，或给 CAS 加常驻播放器。
- 301 炮自己 120 帧的开火光效：Berserker 没有对应槽。

## 4. 验证

- 离线：`cmake --build build --target hector_ai_check && build\hector_ai_check.exe`（新增速度、瞄准角、限位、缓动、骨骼跟随的断言）；
  `build.cmd`（/W4 /WX）；`cmake --build build --target offline_checks` + `ctest`；`python -B tools/selftest.py`（`edf41_hector_alias`：
  ExplosionDownBorder ≥ 1e30、CAS 无 wakeup_f/_b、无 ClimbOver、check_states 拒绝点名缺失翻越状态、MoveSpeed [6, 0.1]、TurnSpeed [0.3, 0.1]）；
  `python -B tools/make_edf41_objects.py --only HECTOR301CC.SGO --only HECTOR401CS.SGO --out tmp\...`。
- 结果（2026-10-17）：`build.cmd` 通过（/W4 /WX）；`hector_ai_check` 648 项 0 失败；ctest 273/273 通过（native 审计用例无游戏变量照常跳过）；
  `selftest.py` 240/240（Python 3.13 与 3.12）；HECTOR301CC / 401CS 等 6 个变体重建通过 check_materials / check_states。
- 没提交 `edf41port/` 的再生成数据：上面的数据改动要在下次重新生成 edf41port 时才进安装包。

## 5. 未验证（要实机确认）

- 瞬移是否消失：高难度大爆炸打赫克托（不再倒地）；赶它走向建筑 / 地图边缘（不再翻越瞬移）。死亡布娃娃用 4.1 的 SHKT，层号由类设置，
  没核是否会掉穿地面（只影响残骸，U）。
- 转向 [0.3, 0.1]、速度（前进 6、后退 5.1 单位/秒）是否和 4.1 观感一致；`near` 内站定半径、后退周期仍按 `hector_ai.h`。
- 手臂瞄准：这一帧改世界矩阵后是否还有别的步骤在绘制前从局部重建骨架（会让本帧看不到，下一帧仍生效）；arms 骨是 kind 0、没有蒙皮
  顶点（M：从骨类型推），转它只动武器；side 的 yaw 符号与 4.1 是否同向（U：只影响 > 108° 的极端侧后方目标）；4.1 的 roll（随 yaw 的
  小幅倾斜）没做。
- 跟随状态原速度里的 linear_speed（`0.32 × V252`）V252 是什么没查；现在赫克托一律 1.0，不再依赖它。
