# 传送舰激光（src/carrierlaser.cpp）逆向笔记

EDF.dll TimeDateStamp 0x678CCB46，均为 RVA。置信度：H = 反汇编直接读到，M = 推断/间接证据，L = 猜测。

## 1. 传送舰 e508

- `app:/object/e508_carrier.sgo`：`xgs_scene_object_class` = `UfoCarrier508`，`game_object_durability` 1000，核心部件 `RagDollProxys.catapult_A`。(H)
- UfoCarrier508 vtable 0x17C9DC0 → UfoCarrier 0x17C9930 → GameObjectBase → SceneObject（RTTI）。(H)
- HP：最大 +0x2F4、当前 +0x2F8，UfoCarrier 自己的代码在读写（0x4F0FB0、0x4F3600、0x4F6D00、0x4F81D0、0x4F94A4）。(H)
- 舱口：E508_CARRIER.MRAB 的 hatch_A..H 环在本体原点下 16.81 m，半径约 35 m；in_ring 在原点附近，绕竖轴。旧做法（斜射退路）取原点沿 -up 20 m 作为出射点；竖直模式从核心 catapult_A（原点下 5 m）出射，见 §6。(M)
- 测试关 sub_vs_mothership.py 生成后立即 `UFOCARRIER_ACTION_OPEN_MODE`，舱门开着，核心可被打伤。打断判据依赖这一点。(M)

## 2. DemoIndirectFire（任务里的卫星激光 DEMOSATELLITELASER*）

- vtable 0x17D4B20，ctor 0x5B55F0（唯一调用点 0x5B5173），Update = slot 5 0x5B5C50。(H)
- 发射单元 IndirectFireControl 在 +0x170（ctor 0x5B56AC `lea rsi,[rdi+0x170]`）。(H)
- ctor 读取 `indirect_fire_param` 后调 config 0x2B5F40，owner 设为自己（0x2B8390，参数 = 自身 +0x28/+0x30），spread 0（0x2B8460），伤害 = 0xD7AE0 系数 × `indirect_fire_damage`（0x2B82E0），瞄准点 +0x20 = 自身位置，再用 0x2B4330 开火。(H)
- Update：step 0x2B95A0(+0x170, dt)，0x2B7B90 判完成后 0x118A1B0 删除自己；不改写 +0x20 / +0x300。(H)
- 不是 GameObjectBase（dynamic_cast 失败）。(H)

## 3. IFC 字段

| 字段 | 含义 | 置信度 |
|---|---|---|
| +0x20 | 瞄准点 | H |
| +0x78/+0x80 | owner weak（0x2B8390 自己 `lock inc` 弱计数） | H |
| +0xD0 | team，每次 step 从 owner+0x314 刷新 | H |
| +0xDC | 伤害（0x2B82E0） | H |
| +0x224 | 散布（0x2B8460） | H |
| +0x2CC | 已开火 | H |
| +0x2D8 | 首发前帧数（param #15，见下；插件的 ShellMake 写 0） | H |
| +0x2E0 | 发间隔计数（每发从 param #3 重新取） | H |
| +0x2E8 / +0x2E4 | 开火音效只放一次 / 已放次数（param #17[0]） | M |
| +0x2F0 | 剩余发数（param #2） | H |
| +0x2F8 | 弹道（0 = 直线） | H |
| +0x2F9 | 从指定点发射；非 0 时每发起点取 +0x300（0x2B970D / 0x2B9756），否则由 0x2B43A0 按 param #0/#1 算天上的点 | H |
| +0x300 | 起点 | H |

`indirect_fire_param` 其余下标（config 0x2B5F40 逐条跟到写入位置；IFC+0x70 起就是子弹 InitParam，core 偏移 = IFC − 0x70 + 0x9A0）：
#4 子弹类，#5 速度（+0x220 与 +0xD4 AmmoSpeed，米/帧），#6 AmmoGravityFactor（+0x120），#7 AmmoSize 粗细（+0x100），#8 AmmoHitSizeAdjust（+0x104，命中半径 = #7×#8），
**#9 AmmoExplosion 爆炸半径**（+0xF0 = core+0xA20；原版 DEMOGUNSHIPFIRE**E15/E25/E35/E50** 的 #9 正是 15/25/35/50），#10 AmmoAlive 存活帧（+0xD8），#11 AmmoIsPenetration（+0xF4），#12 AmmoColor，
#13 Ammo_CustomParameter（+0x128，交给弹种），#14 自定义模型名（0 = 不用，与爆炸无关），#15 开火音效后到发弹的延迟帧（+0x2D8：config 在 0x2B61ED 取下标 15、0x2B624D 写入；step 每帧在 0x2B97BC / 0x2B97D3 减 1，到 0 才发射，起点取发射那一刻的 +0x300，0x2B9B7D），#16 开火音效方式，#17 开火音效，#18 AmmoHitSe 命中音效。(H，#14 M)
旧版本笔记把 #9 写成「冲击」、#14 写成「爆炸」，是错的。
IFC 的爆炸按队伍过滤：IFC+0xD0（每步从 owner+0x314 刷新）不为 -1 时只伤敌对方；owner 无效时队伍 -1，伤所有人。RocketBullet01 寿命到期不爆，GrenadeBullet01 在 CustomParameter #0 = 1 时到期必爆。

## 4. 插件做法

- 派生 SGO（`python tools/make_jets.py`，testrange/gen.py `portal_lasers`）：
  - `EDF6VC_PORTAL_SIGHT.SGO`：瞄准光，红色细光束，每帧一发、存活 6 帧，最多 750 发（12.5 s，盖住 12 s 的充能）；伤害 0。
  - `EDF6VC_PORTAL_LASER.SGO`：主炮，单发、粗 8、存活 45 帧；伤害由插件写（CarrierLaserDamage）。
- 预载 0x7A3780 后，用 CreateObject 0x11945E0 在目标点生成，然后：owner = 传送舰的 weak-this，伤害写 +0xDC，`+0x2F9 = 1`，+0x300 = 舱口，+0x20 = 目标；瞄准光每帧更新这两点。
- 充能 12 s；充能期间舰损失 ≥ CarrierLaserBreak × 最大 HP 或被击落 → 打断。冷却 30–45 s、全局间隔 6 s、同时只有一艘，均未变。
- 新流程（§6、§7）：先飞到潜航母舰上空停住，再从核心竖直向下充能、开火。飞不了时退回旧做法：从舱口斜射玩家（400 m 内）或甲板，最后 1 s 锁定。

## 5. 未验证

- 未进游戏实测：光束外观（粗细/颜色下标含义）、音效是否刷屏、伤害是按次还是按帧、0 伤害光束擦过玩家有无受击反应、舱口点是否正好在传送口。
- 伤害未乘难度系数（ctor 会乘 0xD7AE0 的系数，插件直接写绝对值）。
- 飞行部分全部未进游戏验证，见 §8。

## 6. 传送舰怎么移动（静态逆向）

帧内顺序（对象管理器 0x1198DA0）：先对 +0x488 表里的对象调 slot 7（AI，`call [rax+0x38]`，0x1199131 / 0x119930B，返回值不用），再 slot 4，再 slot 5 Update。进 +0x488 表的条件是 +0x1A 的标志位（0x1195F20）。(M：没找到 508 置位的代码，插件运行时校验，见 §7)

- 基类 AI 0x54A000（GameObjectBase slot 7）：+0x4A8/+0x4B0 是 AiRouteExplorerBase（vtable 0x17CD3B8）的 shared_ptr；`+0x4D0 = explorer->slot3(&+0x4C0)`，即「有下一个目标点」和目标点；explorer 走完调 0x5478C0 清掉；+0x380 & 0x800000 则删对象。(H)
- 路线：脚本 SetAiRouteNavigate 0x1C2370、SetAiRoute_DeleteWhenDone 0x1C2880（0x6F7E60 查 RMPA 路线 → 0x1C48F0 设 explorer）；GameObject_SimpleRouteExplorer vtable 0x17D6A08 / ctor 0x5CFE30。explorer +0x28 速度、+0x2C 走完；对象 +0x4B8 速度倍率（SetAiMoveSpeed 0x1C1A10，默认 1.0）、+0x4BC 到达半径（默认 5）；IsAiMoveEnd 0x1C0B10 = (+0x4A8 == 0)。(H)
- UfoCarrier AI 0x4F6D70（508 vtable 0x17C9DC0 slot 7 也是它）：清零 +0xB40..+0xB5C（+0xB4C = 1.0），调基类；有 explorer 且 +0x4D0 时 `+0xB40 = unit(+0x4C0 − pos) × explorer+0x28 × +0x4B8`，并写转向率 +0xB54。(H)
- UfoCarrier Update 0x4F74D0（508 的 slot 5 0x4F9540 先跑状态机 0x4F8B80 再调它）：非手柄分支若 !(+0x1A & 8) 清零 +0xB40；+0xDF0 == 0 时 `+0x5D0 = +0xB40`，速度 `+0x5C0 += (+0xB40 − +0x5C0) × 0.01`，`pos +0x90 += +0x5C0`（米/帧）；+0xDF0 == 1 坠落，其他值减速；`+0x600 += +0xB50`（朝向）。(H)
- 状态机 +0x16F0（0x4F1760 注册）：0 0x4F2AB0 空闲；1 0x4F2A20 走路线 +0x1728；2 0x4F2990 走路线 +0x1740 后删除；3 0x4F2730 飞到 +0x1760（半径 +0x1780）后转 5；4 0x4F2660 飞到 +0x1770 后删除；5 0x4F2850；6 0x4F28E0。+0x1760/+0x1770/+0x1780 由 0x4F3600 写。(H，各状态的脚本含义 M)
- 原生「飞向一点」0x4F2370(rcx 舰, rdx const float* 点 vec4, xmm2 半径, xmm3 速度 米/帧)：取目标点周围水平半径 `radius` 圆上的点（0 = 点本身），把单位三维方向写到 +0xB40（0x4F2487 `lea rdi,[rbx+0xB40]`），0x4F2AC0 转向（写 +0xB50/+0xB54），再按 速度 × (1 − 转向惩罚) 缩放 +0xB40。近处不减速。状态 3/4 用的就是它。(H)
- 结论：最干净的移动方式是在 AI 之后用游戏自己的 0x4F2370 写「想要的速度」+0xB40，让 Update 的物理（0.01 缓动 + 位移）自己飞过去，不写位置。(H)

核心位置：E508_CARRIER.MRAB 骨骼 in_ring 在原点；核心 catapult_A 在 (0,−5,0)，竖轴上；hatch_A..H 环 y −16.814、半径 34.9；碰撞盒半尺寸 (47, 43.5, 75.9)、中心 y −0.51，底部约在原点下 44 m。(H：MDB 绑定姿态；catapult_A 是否有动画 M，它在轴上，影响不大)

## 7. 插件做法（飞到上空竖直开火）

- 包裹 UfoCarrier508 vtable slot 7（0x17C9DC0 + 7×8，期望值 0x4F6D70，非原值时串接另一插件）：先调原 AI；被派出的舰再调 0x4F2370(舰, 引导点, 0, 速度)。全部在 `__try` 内，出错关飞行。
- 签名：0x4F6D70 AI 开头、0x4F6DA9 读 explorer、0x4F2370 开头、0x4F2487 写 +0xB40、0x4F76F5 判 +0xDF0、0x4F76FF 消费 +0xB40；任一不符 → 不挂钩，退回旧的斜射。
- 母舰表：每艘调 CarrierLaserFrame 的潜航母舰（最多 3 艘）每帧记位置，平滑算出速度（米/帧）。
- 目标点：母舰体坐标 x = 0，z ∈ {+500 舰首平甲板, +120 中部（炮塔前）, −520 舰尾（无人机舱上）}，高度 = 甲板 193.08 + 220 m（塔顶在甲板上 173 m、z ≈ −280，舰底在原点下 44 m）；选离舰最近的点。
- 速度：min(0.75 米/帧 ≈ 45 m/s, 剩余距离 × 0.005)，配合 0.01 的速度缓动约为临界阻尼 0.7；引导点 = 目标点 + 母舰速度 / 0.005（≤ 150 m），母舰在航行时舰在目标点上以同速跟随。离目标点远（>300 m）且低 40 m 以上时先爬升。水平 40 m 内把 +0xB54 清零，免得绕点打转。
- 判定：水平、垂直都在 40 m 内 → 「over the carrier: charging」，开始 12 s 充能，期间继续保持在目标点；瞄准光与主炮起点 = 核心（原点沿 −up 5 m），终点 = 核心正下方 (0,−1,0) 方向与母舰甲板平面的交点，每帧重算（竖直模式不锁定）。
- 退路：60 s 没到、或包裹函数 2 s 没飞过这艘舰（说明 508 不在 slot 7 表里，此后本局都不再派飞）→ 原地斜射。舰被击落 / 母舰没了 → 打断。
- 开火后松手：有路线的舰的 explorer 只是被盖住、没被删，下一帧 AI 照常沿路线走；无路线的舰（测试关就是）+0xB40 归零，速度缓动到 0，悬停在原地。选择「松手」而不是「送回去」：不碰路线就不会破坏任务脚本，投放怪物（状态机）全程没动。

## 8. 未在游戏内验证

- 508 是否真在 slot 7 表里（M）：日志若出现「the 508 AI wrapper never ran」即不在，已自动退回。
- 若任务脚本把舰放在状态 3/4（原生飞向一点），状态机在 slot 5 里会覆盖插件写的 +0xB40（M），表现为飞不到 → 60 s 后退回。
- 转向惩罚会让舰在掉头时变慢；母舰航行中能否稳定停在 40 m 内未知。
- 220 m 高度投下的蚂蚁会不会摔死 / 落到甲板外；核心出射的光束外观；45 m/s 的手感。
