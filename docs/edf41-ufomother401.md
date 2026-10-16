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
插件每帧在 stock Update 之后用 0x1100B90 把骨架按 1e-4 缩放重建（看不见），槽 16 恒假（不挨打），+0x380 置 0x80（不当目标）。借来的 ragdoll
第一次 Update 时用 `Ragdoll::Disable` 0x6E97C0（每个刚体 +0xA4 清零、模式 0：0x6ED480 → 0x11B1710 把它移出物理世界；EDF6_Jormungand_
AdditionalParts 0x38B321 在对象虚函数里这样调，无世界锁）拿出物理世界，船体原点不再有碰撞体，与 4.1 的「无碰撞」一致（H；实机见记录：600 帧时
刚体仍在世界外）。

### 2.2 数据（`tools/make_edf41_objects.py`）
- 根：`MOTHER401_ROOTS`（15 个故事变体）+ `MOTHER401_DLC_ROOTS`（任务包 2 的 M809 / MP503，也在 `DLC_ROOTS` 里，`DLC_EXCLUDED` 清空）。没有 EDF6
  模板（无 alias），类名照 4.1；`mother401_fix(games, files)`：
  - 船体加 `animation_model` / `ragdoll`（`MOTHER401_HULL_DONORS` 里本变体有的第一个部件的）；
  - 没有 CAS 的部件（4.1 只有 CannonPanel、FlatPanel 两个 CAS）给 `UfoMother401_Still.cas` = FlatPanel 的 CAS 清空轨道（`cas_graft.hollow`），
    两个真 CAS 加 `default` → `close_state`；
  - 从 4.1 的 MAB 定位点算出插件要的点（节点绑定矩阵 × 定位点位置；面板是刚体）：`edf41_401_mounts`（砲台，每个）、`edf41_401_muzzle`（発射口）、
    `edf41_401_spawn`（出現位置 / 排出位置）、`edf41_401_weak`（weakpoint 骨骼）、`edf41_401_lock`（锁定点：MAB 的第 1 张表 `ターゲット`，EDF6 的
    锁定点组件 0x6C74E0 正是把这张表逐项交给 AddLockTarget 0x547A50；能挨打的部件各一个，Panel01 没有）；
  - 充能光 `edf41_401_charge`（`MOTHER401_GLOWS`）：BigCannon01 / FinalCannon01 / FinalGenerator 每种颜色一个 EDF6 `gunbattery::ChargeEffect_StdParticle`
    节点（E510 炮台 `charge_effect_std` 的格式 `[[], ['ChargeEffect_StdParticle', [[键, 值…], [se]]]]`），参数是 4.1 粒子块的原数：EDF6 的
    解析表 0x303710 / 0x3039B0 写的偏移与 4.1 的块一致（键 12 → +0xC4/+0xC8/+0xCC = 80 / 100 / −25，键 13 → 颜色 +0xE0 与 +0x100 = (0,0,0,1)，
    键 8 → +0x50 = 1，键 11 → +0xB8/+0xBC = 0，键 15 → `Particle3_k.dds`，H）；整数 4 / 4 / 2 放键 2 / 3 / 4（EDF6 自己的充能光是 30 / 30 / 2 的同一
    形状；4.1 的 +8/+0xC/+0x10 与 EDF6 的 +8/+0xC/+0x14 哪个对哪个按这个形状取，M）。颜色：BigCannon (1,1,3,1)；FinalCannon 歼灭弹充能
    (10,1,5,1)、闪电充能 (1,1,5,1)、闪电发射 (2,2,10,1)；FinalGenerator (1,10,5,1)（notes_A 1.5、notes_B 1.4 / 3.1）；
  - 音效：EDF6 **自带** `TIKYUU4_EN_EARTHEATER.ACB`（36 个 cue，en_EE_*），但 EDF6 的 SEPRESET 没有 4.1 的 `EE…` 名字；`edf41_401_se` 与
    `se_table` 等长，每项是 4.1 SEPRESET 的 `[cue, 音量, 音高, 近, 远, 优先]`（构建时核对 cue 在本对象 game_sound 的库或常驻库里）。
    `check_sounds` 对插件类（`PLUGIN_SOUND_CLASSES`）跳过 se_table。
- 4.1 的 `_light.mrab` 4.1 自己也没有（模型列表第二项缺失，照 301 不管）。`material_animation`（4.1 三个裸 MTAB 块）原样留着，插件自己放（2.4）。

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
- 受伤：槽 16 —— 消失 / 毁坏中拒；面板飞行开始前拒（4.1 在 0x270110 延迟走完时才用 0x26fab0 建物理壳，在格位上方 1000 等着的面板打不到）；
  FinalCannon / FinalGenerator / 炮台整块都算；其余只认 `HitBody`（伤害信息子命中最近点的刚体 0x6EA430）== `weakpoint` 刚体。EDF6 的刚体名是 SHKT
  代理名 `RagDollProxys.<weakpoint>`（0x6EA670 整名比较；用 4.1 的裸名查不到，第三轮之前所有弱点部件因此都打不坏）。槽 18 —— HP>0 播受伤音并放
  受击闪材质动画；HP≤0：死亡材质动画，WeakPanel 向下爆炸 + 10 个 StencilLight + 烟 + 200 m 连锁、Core 命中点大爆炸，然后进入毁坏 / Core 死亡。
  掉落：部件死在 ApplyDamage 里，`game_object_drop_item` 照常掉（实机：M018 结算拾得武器 30 / 装甲 62）。
- 锁定点：第一次 Update 用 `AddLockPoint`（AddLockTarget 0x547A50）给有 `edf41_401_lock` 的部件加一个，每帧写成该点的世界坐标；部件登记为敌对时才
  锁得上（锁定过滤 0x689E40），消失 / 毁坏时 `Unregister` 退出队伍即锁不上。DevShoot 也打这个点（AimPoint 取第一个锁定点）。
- 子弹命中音：EDF6 InitParam +0x100 是 SePreset（0x7B3200；4.1 的 +0xB0），`FireHit` 照 PlayCue 填 cue 与 4.1 SEPRESET 的数：炮台 se[4]（Rapid
  se[5]，只在开火态帧号 %16==0 的那发，0x26cc20），FinalCannon 歼灭弹 11、闪电 8（4.1 把 se[5] 再 se[2] 拷进歼灭弹块，第二份本是闪电块的；按
  +6 的原意）。BigCannon01 的块没有命中音。
- 材质动画：EDF6 自己的组件（0x38 字节；ctor 0x6DBE90，0x6DC7B0 绑到模型材质集 = EAO +0x720（模型 +0x5C0 + 0x160，UfoCarrier508 绑 +0x650+0x160、
  0x309CC0 那类绑 +0x720），0x6DC050 加轨，0x6DC830 播放，0x6DC8B0 推进，0x29D120(+8) 释放）。4.1 三条 MTAB 与 EDF6 同格式（`MTAB` 0x200、同样的
  16 字节头），装载模式 0 / 2 / 2 与 E508 相同；[0] 构造即循环播（模式 2），[1] 受击闪（0，HP>0 时），[2] 死亡（1，OnKilled）。实机日志每个部件
  「3 material animations」，截图上面板的发光条纹变成循环的红 / 绿光。
- 生成器：自写 4.1 组件（类型 0 只补死掉的槽，弱引用跟踪）。出生点与方向取 EDF6 的定位点代码（FindLocator 0x6BADD0 / LocatorMatrix 0x6BB5A0）算
  `出現位置` / `排出位置` 节点的世界矩阵（= 4.1 0x3232d0），敌人在节点位置、随机偏航，`SetLevel(块 +0x18)`、`SetTeam(1, true)`（中立母舰放的
  UFO 也要打玩家），`+0x1C` 置位时 0x548D50；然后照 4.1 SpawnerObject 0x3320a0 / EDF6 SpawnerObject 0x6E33C3 的方式（槽 10、9、11）发 0x20102，
  数据 = 节点矩阵第 1 行（UfoSmall507 0x52EDE0 ×60 当初速，Deiroi 0x31ED40 / DragonSmall 0x335110 进下落态；EDF6 自己的生成器 0x6E2170 也发
  节点第 1 行）。FinalGenerator 每生成一只播 se[7]（0x267084）。被打中时叫醒它跟踪的敌人（0x331d10 → EDF6 0x548D50：Generator HP>0 的每次与致死的
  那一次，FinalGenerator 每次）。Generator 的「open 动画长度」取 150 帧（MAB 事件，M）。
  **「传送出现」**：4.1 的 SpawnerObject（0x331e20 / 0x3320a0）只是倒数 1 帧后建对象、设等级 / 队伍 / active、发 0x20102 / 0x20103、记进槽、自删，
  自己不画任何东西；EDF6 的 SpawnerObject（0x6E1080 → 0x6E3250）同样，传送特效来自持有者交给它的回调（+0x640 的 std::function），401 没有。所以
  直接建对象就是 4.1 的样子（差 1 帧），不另加特效（H）。
- 特效：燃烧中的面板每 8 帧 `SmallBurst`（EDF6 0x2E7D00 = 4.1 0x1b1910，两者常数全同：0.97、0.01、0.2、0.1、1.5、0.4、30、31，H），炮台死后
  每 16 帧 Flame（0x2E7FF0 = 4.1 0x1b16b0，同样常数全同，H）；Explosion / Explosion_Big（PRIMITIVES 5）；WeakPanel 的 10 个 StencilLight
  （EDF6 InitParam@StencilLight，vtable 0x1762108，+0x30 位置、+0x40/+0x50 颜色、+0x60 半径、+0x64 帧数，与 4.1 同布局；4.1 的 WeakPanel 直接填
  (0.5,0.5,0.5,2.5)、i·300、180 不经 ×2.5 / ×5 的辅助函数，这里也直接填，0x84880 建）与烟（0x1b2d90 → ExplosionWind 0x2E4D90）。
- 充能光（**默认关**，开发位 4194304 打开；实机会在退出时崩，见 4）：每种颜色一个 `ChargeEffect_StdParticle`（工厂 0x6D92D0，MovingFortress_Battery 0x4D3F22 同样调），+0xC = 0 显示 / 1 隐藏，每帧槽 1
  （UpdateContext、炮口矩阵）与槽 0 把更新与渲染同步排进场景的工作表（管理器 +0x358）；状态进入时照 4.1 打开（BigCannon 充能 / 发射、FinalCannon
  三态、FinalGenerator 召唤充能），离开即关。渲染作业（槽 0）排在同步渲染阶段（克隆槽 3，EAO 0x5B6D10），更新作业排在 Update（两个一起排在 Update 里会并发，发射器渲染作业 `bad_alloc`）；每帧作业后同步状态（[fx+8]=[fx+0xC]、[fx+0x10]=0，同 MovingFortress_Battery 0x4D1B96）。部件析构时立刻释放效果。

### 2.5 联机
四个类只在对象的模拟在本机跑时（`OnlineRunsHere`）工作：主机布局、开火、生成；客户端什么也不建。4.1 的同步（动作号、状态号）没有移植。

### 2.6 开发开关（发布版都是 0）
`Edf41Mother401Dev` 位：1 攻击一直允许、2 攻击等待缩到 ≤2 s、4 面板立刻就位、8 / 16 开局 `Edf41Mother401DevKill` 秒后毁掉 Core / 一块 WeakPanel、
32 同时刻执行 `Edf41Mother401DevAction` 号动作、64 共用的 `Edf41DevShoot` 也打生成器放出的敌人（看补槽）、128 玩家装甲一直满（autopilot 不会躲）、
256 DevShoot 等 `Edf41Mother401DevKill` 秒后才打（M809 的脚本从它的 OA(0) 起才盯弱点）、1024 / 2048 不放闪光 / 子弹命中音、4194304 打开充能光（排查；默认关）、32768 玩家 DevShoot 改打普通敌人（让船体在脚本事件关上出现；每 30 帧一只，262144 改快速）、65536 诊断（退出报告写 %TEMP%\edf41m401_shutdown.txt）、131072 充能光建了立刻删、524288 充能光永不显示（排查）；
`Edf41Mother401Place`：把船体放在玩家头顶这么多米。共用的 `Edf41DevShoot=n` / `Edf41DevShootDamage`：每个能挨打的部件每 n 帧挨一发玩家的
LaserBullet02（锁定点一发、弱点骨骼一发），日志每 600 帧按类统计「taken / refused」。

## 3. 实机

（见文末「实机记录」。）

## 4. 没做的 / 与 4.1 的差别
- **充能光（Particle3_k）默认关**（开发位 4194304 才开）。做法见 2.2 / 2.4（EDF6 的 `gunbattery::ChargeEffect_StdParticle`，4.1 的粒子块数值经 EDF6
  的键表放进去）。实机证据：开着它的三次运行（M018 ds5 / ds6、M019 ds1）在**退出游戏时**都崩在 EDF 的声音库缓存析构（EDF+0x7ADCFB 读已销毁的
  声音系统 0x20B2948；静态表 EDF+0x20B38A0 退出时还剩 1 项），M809 ds2 在任务中崩在发射器更新工作里的 `bad_alloc`（EDF+0x30316D ← 0x302D10，
  `reserve` 的个数离谱）；同一 M018 流程关掉它（ds7）干净结束。没查清的：4.1 粒子块与 EDF6 发射器参数的语义对应（EDF6 另有一个与 4.1
  0x1c1920 默认值逐项相同的 `ParticleEmitterCore::InitParam` 构造，0x300D..；ChargeEffect 走的是键表 0x303710 的另一种布局）、ChargeEffect
  的音效预设 / 句柄在退出时为何还挂着库。要做成：照 4.1 0x1c2d80 的用法直接嵌 EDF6 的发射器（0x301290 构造、0x302200 初始化、作业 0x302EE0），
  参数用与 4.1 同布局的那种 InitParam。
  补完轮的调查（2026-10-16）：任务中的 `bad_alloc` 已修（渲染作业改排同步渲染阶段，M809 `m401f_M809_g3` 干净）。**退出崩没修好**：崩点仍是 EDF+0x7ADCFB（声音库缓存静态析构 0x7A9850 经 atexit 0x1750870 触碰应用析构 0x7021F0 里已销毁的声音系统）。退出报告（`%TEMP%\edf41m401_shutdown.txt`，插件日志退出时已丢）总是只列 `APP:\SOUND\PC\EDF41_WEAPON_SE.ACB` 引用数 1（我们的 edf41sound 取的引用，没有释放，普通运行同样如此）。用 `AtGameShutdown`（edf41classes，重定向应用析构调用点 0x70293F）在应用析构前销毁全部充能光：M809 g4 干净，M018 g4 / M019 g3 g4 g5 / 关闭显示的 M019 h1 仍崩；M132 / M516 e4（7 个光）干净。所以「建了就崩」随任务而异，不是显示造成；创建后立刻销毁（E1）干净。未验证的猜想：声音库表里的引用数与创建的发射器声音预设有关。因此仍默认关。
- 联机同步（2.5）：只在本机模拟的一端跑，客户端什么也不建。
- Generator 的「open 动画长度」取 150 帧（M）。弱点 `core` 在 FlatPanel 外壳（刚体 0）里面：激光一类单点命中打在外壳上一律被拒（M019 / M018
  实机 Generator 0 taken / 91–508 refused），只有爆炸类的子命中能落到 `core`——4.1 用同一 SHKT 与同一「离命中中心最近的子命中」规则，应同样
  如此（M），所以「受击叫醒敌人」只在代码里看过，实机没打中过 Generator。
- 用户转来的地址：0x6E40B0（ragdoll 激活）核过（锁世界、每个刚体模式 4、约束 0x6E4A70），旁边的 0x6E97C0（停用）用上了；MAB 事件回调
  0x6B6E30 / 0x116BE30、范围伤害 0x542670 用不上（4.1 的 401 不用范围伤害；Generator 的开舱长度取 CAS 时钟）。第三方的 Earth Eater 实现
  （ParameterHUD.dll，只做了 Panel / Cannon / GeneratorPanel 三个类）没有可借的东西。
- 已解决、不再是差别：材质动画、StencilLight、炮台 / 面板的确切火花、子弹命中音、受击叫醒（代码）、锁定点、keep 面板（4.1 本就不让它们消失，
  见 Layout 的注释：0x25f833..0x25f87b 先清旧表那一项的弱引用）、船体原点的碰撞体、「传送出现」（4.1 本就没有特效，2.4）。

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

### 补完轮的实机记录（2026-10-16 / 15，`Edf41DevShoot=20`、`Edf41DevShootDamage=100`，输出目录在 `jobs/14618f05/tmp/gametest/`；全部没有崩溃转储，Mods / 存档逐字节还原，除非写明）

| 任务（目录） | 开关 | 看到的 |
|---|---|---|
| M018 `m401f_M018_ds8` | DevShoot + Dev=128（玩家不死） | 玩家打弱点 / 炮台 / 核心：槽 16 过滤按类统计（日志每 600 帧「hits so far」）：WeakPanel 70 taken / 433 refused，Core 614 / 1，BigCannon01 110 / 1，炮台全是 taken，Generator 0 taken / 505 refused（见 4）；炮台、主炮、WeakPanel 死亡（含 200 m 连锁）、**阶段门走完**：OA(5) 换 map2 → OA(6) 换 final（38 块，307 块旧的消失，1 块 keep 保留）→ OA(1) 飞入 → 打穿 34 门 FinalCannon01（72 → 238 发命中后死）→ 4 个 FinalGenerator 死 → Core 死（614 发）→ Core 死亡编排 → 约 50 s 后面板删光、船体自删；截图 24：**MISSION CLEAR 结算，拾得 ARMOR / 武器掉落**（掉落、命中音走游戏自己的伤害管线） |
| M018 `m401f_M018_ds7` | 同上 + Dev=575（无充能光） | 同一流程干净退出，无崩溃（对照：开充能光的 ds5 / ds6 / M019 ds1 退出时崩，见 4） |
| M809 `m401f_M809_ds3` | DevShoot + Dev=384（256：30 s 后才打，让脚本的弱点监听先就位）、DevKill=30 | 5 块 WeakPanel 被玩家打死（每块 200 m 连锁 4–6 块）→ 脚本 OA(6) = **动作 3「全部消失」（329 块缩没）**→ DeleteObject 船体并同变量**重建**（新船体、`panel_layout_map_phase2`、3 个生成器按 `Ufo401ace` 一次 + `giantbee02Charge` / `silverspider` / `goldant` 周期波次）→ 再 3 块弱点 → OA(6) 全消失（338 块）→ 再建（`phase3`，39 块，4 个生成器宿主）→ 打穿 34 门 FinalCannon01 → 4 个 FinalGenerator → Core 死。**最终阶段由玩家子弹真打出来**（不再用开发开关毁） |
| M019 `m401f_M019_ds2` | Dev=192（64：也打生成器放出的敌人） | 周期生成器放敌人，日志「enemy #n gets 0x20102 (0 −1 0 0)」；Generator 的核心刚体被外壳挡住，命中全拒（见 4），所以「受击叫醒」没在实机触发 |
| 无开关各跑一遍：M009 M012 M018 M019 M142 M149 M201 M412 M518 M602、M809、MP503 | 全 0 | 能进任务、布局 / 生成器 / 动作正常，**无崩溃**；M009 `panel_layout_map_init_m009` 137 块、7 个生成器宿主；M142 205 块、M149 148 块 |
| M145 `m401f_M145_e2`、M308 `m401f_M308_e2`、M132 `m401f_M132_e4`、M516 `m401f_M516_e4` | DevShoot + Dev=32896（32768：玩家子弹打普通敌人，让脚本事件触发；128 不死）、DevShootDamage=300 / 3000 | **船体出现**（M145 361 格、4 个生成器宿主；M308 290 格；M132 203 块；M516 193 块、3 个生成器宿主；队伍 3），布局 / 生成器正常，数分钟无崩溃转储，M132 的 7 个充能光（Dev 含 4194304 前的默认开版）退出干净 |
| M018 `m401f_M018_k1` | DevShoot + Dev=193（1 攻击一直允许、64 也打生成器敌人、128 不死），默认无充能光 | 无崩溃转储、干净退出；开着的生成器被打中走到「叫醒」路径：日志「Generator … is hit: 0 of its 3 / 6 live enemies notice」——有活的受跟踪敌人，但 `+0x4A0` 已是 1（生成时就 Activate 过），所以没有要叫醒的；该路径（Encount → Activate 0x548D50）在实机上跑过，「真的叫醒一只沉睡敌人」仍没见到 |
| M312 `m401f_M312_e4` | 同上 | 慢杀（每 30 帧一只）后任务仍先清关（结算截图），船体没出现；这一关仍只有数据测试 |
| M018 / M019 / M809 充能光退出崩调查 `m401f_M018_g4`、`m401f_M019_g3/g4/g5/h1`、`m401f_M809_g3/g4` | 见 4 | 详见 4 的充能光条；默认关 |
| M132 M145 M308 M312 M516 无开关各跑一遍（`m401f_plain_*`） | 全 0 | 无崩溃；但 90 s 内船体没出现——脚本在事件（击杀、计时）后才 CreateNeutral，autopilot 不打，没触发（这五关仍只有数据测试） |
| 目前的一次性问题 | | 链式连跑被系统内存吃紧杀掉过一次（不是游戏问题），已重跑缺的 M009 |

本轮在实机上**新验证**的功能：锁定点与受击音（日志无「did not play」，未听）、材质动画（每部件日志「3 material animations」，截图红 / 绿循环光）、
弱点刚体过滤（修好 `RagDollProxys.` 前缀后 taken / refused 如表）、StencilLight（M018 / M809 的 WeakPanel 死亡，无崩溃，外观未逐帧核）、
周期生成器的补槽（只补死掉的，数据块「waves of 6 …」，每波 6 只日志 #1–#6 一帧一个）、动作 3（M809）、动作 4（M018 自转）、
DeleteObject + 重建（M809）、最终阶段 FinalCannon / FinalGenerator（M018 / M809 玩家真打穿）、船体 ragdoll 移出物理世界（日志 600 帧时「0 of its 1
ragdoll bodies are back in the world」）。

**仍没在实机上看到的**：M312 的船体（打光敌人任务先清关）；Generator 被打中时叫醒敌人（命中全被外壳挡住）；音效实际听感；
动作 3 / 4 之外的 7（M201 飞走）上一轮已见；联机。
