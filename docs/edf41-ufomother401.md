# EDF4.1 的 401 母舰「アースイーター」（UfoMother401）在 EDF6 里

2026-10-15。代码：`src/edf41mother401.cpp`（12 个类）、`src/edf41classes.{h,cpp}`（与 301 / UfoRobo 共用的工厂、克隆虚表、EDF6 构件）；
数据：`tools/make_edf41_objects.py` 的 `MOTHER401_*` / `mother401_fix`；开关：ini `Edf41Mother401`（默认 1）。
4.1 的逆向规格：`jobs/14618f05/tmp/re401/SPEC.md`（+ notes_A..D）；EDF6 构件：`jobs/14618f05/tmp/re6/PRIMITIVES.md`。地址：4.1 为
`EDF41.exe` RVA，EDF6 为 `EDF.dll` RVA（TimeDateStamp 0x678CCB46）。可信度 **H** 读码 / **M** 推断 / **U** 未核。

## 1. 4.1 的类（规格摘要）

- **船体 UfoMother401**（ctor 0x25eba0）：没有模型、碰撞、自己的耐久，是 19×19 六角格的布局控制器。SGO 的 `action_list` 是 8 种指令的表
  （0x260e10）：0 换布局、1 分批飞入、2 立即就位、3 全部消失、4 缓慢自转、5/6 攻击开/关、7 核心飞走；脚本 `ObjectAction(n)` 执行第 n 条。
  布局（0x25f370）：第 i 行第 j 列 `x = 172.5j + (i 奇 ? 86.25 : 0)`、`z = 150i`，整体居中；每格按 `panel_definition` 建一个真对象，停在格位上方
  1000，等 1 / 2 号指令下来；旧表面板按到核心距离 ×0.003 秒错开消失；`keep` 的面板保留。HP 镜像 Core；Core 死 → 所有面板按
  `rand·1200−600+1.5·距离` 帧依次毁坏，面板删光后船体自删。只取偏航（出生点 face 下倾的任务不斜挂）。
- **PanelBase**（0x26f1d0）：飞入（Δ·0.02，限速 c、下限 0.01）、就位音、消失（缩没 0.5 s）、毁坏（等待 → 燃烧 180 帧小爆炸 → 坠落 g=0.002 → 大爆炸删除）、
  飞走（向上加速，局部 y>1000 删）。**受伤过滤**（槽 10 0x26fe60）：只认打在 SGO `weakpoint` 所名 ragdoll 刚体上的命中；没有 weakpoint 的
  Panel01 全家是打不坏的装甲。
- **Panel01**：按 `parts_define` 的权重在每个 `砲台` 定位点挂炮台（`[0,w]` = 空）。**WeakPanel**：死时 200 m（XZ）内所有面板（含 Core）
  rand(180) 帧后毁坏。**Core**：`roll` 刚体，死亡编排 30 s → 24 次爆炸（每 16 帧，+500 降到 −171）→ 8 s → 3 次大爆炸 → 6 s 删除。
  **BigCannon01**：攻击允许且就位时倒数（首次 rand(1800)，之后 1200–1799 帧），CAS open → 充能 5 s → 600 帧每 2 帧一发向下的 LaserBullet02 → close。
  **Generator / FinalGenerator**：4.1 通用生成器组件（SetEnemyGenerator 周期波次、SetInstantEnemyGenerator 一次），Generator 开舱后才放（open 动画），
  FinalGenerator 充能发光。**FinalCannon01**：400–599 帧一轮，1/3 歼灭弹（RocketBullet01_LowLight 向下），2/3 五发瞄准的闪电（LightningBullet01）。
  **炮台**：SmallLaser 700 m / Sniper / Plasma / Rapid 500 m，目标进射程才倒数，Rapid 30 s 追踪扫射。
- 难度：4.1 用船体 +0x490 = 10.0（从未改写）→ s = 1；单机难度表都是 1（SPEC2 1.3）。所以面板 HP = SGO 耐久，伤害是常数（SPEC 1.6 的表）。

## 2. EDF6 实现

### 2.1 类与底类
12 个类都由 `edf41c::AddFactory` 登记进游戏自己的工厂表，全部以 **EventAnimationObject**（ctor 0x5B5FB0，0xC90 字节：模型、CAS 播放器 main、
ragdoll、耐久）为底，虚表克隆（45 槽）：0 析构、5 Update、9 OnMessage、**16 受伤过滤**（ApplyDamage 0x547C30 在改 HP 前问它）、**18 受伤后**。
船体与部件各一张克隆表；4.1 的字段放在插件自己的记录里（按对象指针）。两张表的槽 22（「允许的队伍」，EAO 的 0x5B6BA0 只认 6）换成 `AllowAnyTeam`，
否则 SetTeam 0x54EE70 什么也不改（实机：CreateEnemy 的船体停在 6）。

**为什么船体用 EventAnimationObject 而不是 301 的 UfoCarrier508**：401 船体什么都不画、不挨打，508 会带进它的生成器组件、锁定点、se_list 和
「只在舱门开时、只认核心刚体」的伤害规则（PRIMITIVES 2d），全是 401 不要的；EAO 只有模型 + ragdoll + 耐久，最便宜。它的构造对
`animation_model` / `ragdoll` 不判空（缺 ragdoll 时 0x5B61ED 抛异常），所以数据给船体一个本套最小部件的模型与 ragdoll（小激光炮台，约 10×30），
插件每帧在 stock Update 之后用 0x1100B90 把骨架按 1e-4 缩放重建（看不见），槽 16 恒假（不挨打），+0x380 置 0x80（不当目标）。剩下一个
小碰撞体在船体原点（核心格内部），与 4.1 的「无碰撞」差这一点。

### 2.2 数据（`tools/make_edf41_objects.py`）
- 根：`MOTHER401_ROOTS`（15 个故事变体）+ `MOTHER401_DLC_ROOTS`（任务包 2 的 M809 / MP503，也在 `DLC_ROOTS` 里，`DLC_EXCLUDED` 清空）。没有 EDF6
  模板（无 alias），类名照 4.1；`mother401_fix(games, files)`：
  - 船体加 `animation_model` / `ragdoll`（`MOTHER401_HULL_DONORS` 里本变体有的第一个部件的）；
  - 没有 CAS 的部件（4.1 只有 CannonPanel、FlatPanel 两个 CAS）给 `UfoMother401_Still.cas` = FlatPanel 的 CAS 清空轨道（`cas_graft.hollow`），
    两个真 CAS 加 `default` → `close_state`；
  - 从 4.1 的 MAB 定位点算出插件要的点（节点绑定矩阵 × 定位点位置；面板是刚体）：`edf41_401_mounts`（砲台，每个）、`edf41_401_muzzle`（発射口）、
    `edf41_401_spawn`（出現位置 / 排出位置）、`edf41_401_weak`（weakpoint 骨骼）；
  - 音效：EDF6 **自带** `TIKYUU4_EN_EARTHEATER.ACB`（36 个 cue，en_EE_*），但 EDF6 的 SEPRESET 没有 4.1 的 `EE…` 名字；`edf41_401_se` 与
    `se_table` 等长，每项是 4.1 SEPRESET 的 `[cue, 音量, 音高, 近, 远, 优先]`（构建时核对 cue 在本对象 game_sound 的库或常驻库里）。
    `check_sounds` 对插件类（`PLUGIN_SOUND_CLASSES`）跳过 se_table。
- 4.1 的 `_light.mrab` 4.1 自己也没有（模型列表第二项缺失，照 301 不管）；`material_animation` 是裸 blob，EAO 不读，插件也不放（见 4）。

### 2.3 船体
- 第一次被碰（第一帧或第一条消息）：只留偏航的矩阵、种子 `(int)(x+z)`、队伍（脚本 SetTeam 之后的 +0x314）、环境音 `en_EE_ambient`（循环，跟随面板平均位置）。
- `0x1000000D` ObjectAction：照 1.3.2 执行；越界只记日志（M012 不可达的 OA(8)）。`0x10000017` GetPartsObject `{组, 序号, out weak_ptr}`：按格序第
  序号个该组面板（含已死未删的），写出它自己的弱引用并加一个 weak 计数（0x222470 的约定）。`0x20100` 生成器：数据块 = 4.1 +8（`+0x48` 序号），
  转给本布局第 N 个生成器面板。`0xF0000000` 吞掉。
- 每帧：自转（每帧 +2e-7，上限 0.0002 rad/帧）、向目标点的平移（没有路线，停在原地）、偏航插值 0.01；HP 镜像 Core（+0x2F8 / 最大 +0x2F4）；
  Core 死 → 船体 HP 0、+0x2E8 = 1（脚本 9201 看得见）、所有面板 `Destroy(rand·1200−600+1.5·距离)`；面板全没了船体自删。
- 脚本的 `SetObjectDurability`（3020，EDF6 0x2288C0）写的是**耐久下限** +0x2F0 = 百分比 × 最大（H），ApplyDamage 把 HP 钳在下限之上——正是 M018
  65 → 35 → 0 的「阶段门」语义，不用插件做任何事。

### 2.4 面板 / 炮台
- 建在格位（`CreateFromPath` 0x11945E0，矩阵 = 船体矩阵、平移 = 格位）；记录：目标 = 格位、当前 = 格位上方 1000、随机 60° 朝向、keep、攻击许可 =
  船体的、队伍 = 船体的（`SetTeam(…, true)`）。第一次 Update：读 SGO 点与音、HP = `0xD7720(base)`（EDF6 的难度 / 联机耐久系数；EAO 不按等级自己算）、
  能挨打的清 +0x380 bit0、全部置 0x2000（不算进「敌人数」），Panel01 置 0x80（不当目标）；远距绘制 0x11B3020（模型节点 +0x5C0；面板散在 3.3 km 宽的格上）。
- 移动、状态机、开火、生成器照第 1 节一比一，帧计数按每次 Update 一帧（60 fps）。子弹按 SPEC2 第 7 节换算（速度 +0x94、寿命 +0x98、伤害 +0x9C、
  爆炸半径 +0xB0、大小 +0xC0、+0xC4、+0xC8、+0xCC、颜色 +0xD0、自定义 +0xE8）；方向：BigCannon / 歼灭弹沿面板 −Y，闪电 / 炮台瞄 GameObjectBase
  的目标（0x30C0F0，每 60 帧取最近敌对）再在锥内随机。FinalCannon 的音用 SGO 的 6–11（4.1 代码用 0–5，见 SPEC 1.6，按原意 +6）。
- 受伤：槽 16 —— 消失 / 毁坏中拒；FinalCannon / FinalGenerator / 炮台整块都算；其余只认 `HitBody`（伤害信息子命中最近点的刚体 0x6EA430）==
  `weakpoint` 刚体（0x6EA670）。槽 18 —— HP>0 播受伤音；HP≤0：WeakPanel 向下爆炸 + 200 m 连锁、Core 命中点大爆炸，然后进入毁坏 / Core 死亡。
- 生成器：自写 4.1 组件（类型 0 只补死掉的槽，弱引用跟踪），敌人在出生点、随机偏航，`SetLevel(块 +0x18)`、`SetTeam(1, true)`（不论船体中立与否都是
  敌人：M009 等中立母舰放的 UFO 要打玩家），`+0x1C` 置位时 0x548D50（脚本的 active）。Generator 的「open 动画长度」取 150 帧（MAB 事件，M）。
- 特效：Flame / Blazing / Explosion / Explosion_Big（EffectGenUtil，PRIMITIVES 5），尺寸照 4.1 的数（外观对应 M）。

### 2.5 联机（`edf41net`，2026-10-15；总述见 `docs/edf41-classes.md`「联机」）
- **原来**：船体和部件只在 `OnlineRunsHere` 的机器上跑，客人上什么也不建；而客人上脚本的 ObjectAction 因此落到 EventAnimationObject 的
  OnMessage（`0x5B6540`），船体 SGO 没有 `action` 键——UfoRobo 在 M411 上走同一函数时抛过异常（`EDF+0x5B6A01`）：**客人会在第一次
  ObjectAction 时崩**（M，没在客人上跑过）。
- **现在**：每台机器都跑船体和部件（脚本的 ObjectAction / GetPartsObject / 生成器到每台机器；布局以船体位置为种子，部件的种子来自船体的序列，
  所以每台机器排出同样的面板、同样的顺序）。每个面板（按船体所有布局里的序号）、炮台（面板序号 + 挂点）、生成器放的敌人（面板序号 + 计数）
  取得游戏自己的子对象身份（`0x781630`，MovingFortress 部件的做法）：耐久、受伤、击毁、掉落、目标（FinalCannon01 与炮台置 `+0x591`）走原版同步。
  三处随机的攻击起点（BigCannon01 开盖、FinalCannon01 选炮、炮台开火）只在房主决定，经部件自己的 NetworkObject 发 `kPartEnter`，客人副本在待机
  状态等它。HP 只经 HP 差到达的副本补跑 `PartDamaged` 的击毁（`KillFromHp`）。4.1 自己的同步（动作号、状态号）不照搬。
- `Edf41Net=0`：不登记、不发事件（每台机器仍都跑，不再有上面的崩溃）。

### 2.6 开发开关（发布版都是 0）
`Edf41Mother401Dev` 位：1 攻击一直允许、2 攻击等待缩到 ≤2 s、4 面板立刻就位、8 / 16 开局 `Edf41Mother401DevKill` 秒后毁掉 Core / 一块 WeakPanel；
`Edf41Mother401Place`：把船体放在玩家头顶这么多米。

## 3. 实机

（见文末「实机记录」。）

## 4. 没做的 / 与 4.1 的差别
- 材质动画（出现 / 受伤闪 / 死亡三段）、充能发光粒子（Particle3_k）、StencilLight、炮台火花的确切特效、子弹命中音（4.1 填 InitParam +0xB0 的 se）。
- 受击让生成的敌人「发现玩家」（4.1 0x331d10）；EDF6 的 SpawnerObject 式传送出现特效（直接 CreateFromPath）。
- 面板没有锁定点（EAO 不登记），锁定武器锁不上面板（4.1 能）。
- 4.1 换布局时也对 keep 的面板调消失（SPEC 待办 3）：这里 keep 的保留不消失。
- 船体原点有一个小碰撞体（2.1）。
- 联机：做了（2.5），但没在两台机器上跑过（本机一个 Steam 账号、一台机器）；单机下的验证见 `docs/edf41-classes.md`「联机」D。
- 用户转来的线索（未核、未用）：MAB 事件回调 0x6B6E30 / 0x116BE30、范围伤害 0x542670、ragdoll 激活 0x6E40B0。

## 实机记录

2026-10-15，`run_m126.py`（经 `tools/gamequeue.py`）跑本分支的 DLL 与数据，Mods / 存档逐字节还原，**全部没有崩溃转储**。

| 任务 | 开关 | 看到的（日志 `EDF41M401` + 截图） |
|---|---|---|
| M019 | Dev=3 | t=0 布局 init 161 块、立即就位；~8 s 起 4 个 IGen 在生成器出生点造出 Deiroi401；~16 s phase2 46 块飞入、161 块 keep 保留；截图：六角面板成片悬在空中、主炮向下的绿色激光柱 |
| M018 | Dev=11, DevKill=40 | 布局 361 块 + Panel01S/SC 上 392 个炮台（共 753 对象，布局 10–12 ms）；GetPartsObject 组 3 → Core；船体队伍 1，炮台瞄玩家开火（小激光 / 狙击束 / 等离子），主炮向下激光；40 s 毁 Core → 360 块按距离依次燃烧坠落（截图：满屏爆炸），Core 死亡编排四段，约 74 s 后面板删光、船体自删 |
| M012 | Dev=3 | CreateNeutral 船体（队伍 3），~90 s 布局 276 块按 `[1,(0,0,0),0,60,1.5]` 分 60 s 飞入，主炮开火 |
| M201 | Dev=35, DevAction=4 | init 只有 Core；25 s 执行动作 4 → Core 飞走、上升到局部 y>1000 后删除；对空句柄的 3300/3020 无事 |
| M602 | Dev=0（原样） | 布局 + 立即就位，5 个生成器按 IGen 每只间隔出 Ufo401（日志 #1–#8），~2.5 min 脚本 OA(7) 攻击开，炮台开火 |
| M809（任务包 2） | Dev=19, DevKill=30 | 船体 2001 建、init 361 块（5 WeakPanel）、GetPartsObject 组 2 序号 0–4 都返回 WeakPanel；OA(0) 攻击开；30 s 毁一块 WeakPanel → 200 m 内 6 块连锁毁坏 |
| MP503（任务包 2） | Dev=0 | init 197 块就位，2 s 时同一帧 phase2 + 飞入 + phase3 + 飞入（34 + 130 块，keep 保留），最后 361 块 |

| M412 | Dev=0 / Edf41Mother401=0 | 布局 361 块、`[1,(-10000,0,10000),4,0.1,0.3]` 飞入；约 30 s 后**游戏崩溃，三次都是，关掉 401（ini 0）也一样**：崩在 `Deiroi` 的更新
（虚表 0x17AA668 的 0x31ED90 → 0x321FA0 的一个状态，0x3220F8 `xor ecx,ecx; call 0xA954F0` 拿空的物理世界做射线，0xA95599 读 [0+0xC0]）——4.1 的
Deiroi401L/S 在 EDF6 的 `Deiroi` 类里走到的一条 stock 必崩路径，与 401 无关，另行处理 |

**性能**（日志每 600 帧一行）：361 块 + 392 炮台（753 个对象）时插件 180–370 µs/帧、它们的 stock Update 220–670 µs/帧；无炮台的 361 块约
70 + 100 µs/帧。一次布局建 361 个对象 10–12 ms（一帧内）。

**实机修正**（都已在代码里）：EAO 的虚表槽 22（0x5B6BA0）只允许队伍 6，SetTeam 0x54EE70 先问它 → CreateEnemy 的船体和所有部件都停在 6
（不敌对、炮台没有目标）；克隆表用共享的 `AllowAnyTeam`。DeleteObject 标记或游戏自己停止更新（死亡）的部件，记录要到析构才清，船体一度认为
「还有面板」不自删；现在被删 / 300 帧没有 Update 的部件算没了。

**没在实机上看到的**：玩家亲手打弱点 / 打炮台（autopilot 不开火：受伤过滤、受伤音、掉落只读过代码）；FinalCannon / FinalGenerator（M018 / M809
的最终阶段要先打穿前面）；M809 的 DeleteObject + 重建（要打掉 5 块弱点）；周期生成器（SetEnemyGenerator）的补槽；动作 3（全部消失）与 4（自转）；
音效是否真的响（日志里没有 `did not play`，但没有听）；M009 / M132 / M142 / M145 / M149 / M308 / M312 / M516 / M518 只有数据测试；联机。
