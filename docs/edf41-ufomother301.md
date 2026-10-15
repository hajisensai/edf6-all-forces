# UfoMother301（EDF4.1 母舰）原生类移植规格

2026-10-13。只有规格，没有代码。来源：`EDF41.exe`（Steam 版，ImageBase 0x140000000）与 `EDF.dll`（EDF6）的反汇编、两个游戏的 Root.cpk / Chunk01.cpk 里的 SGO / MRAB / MAB。
地址一律是 RVA（不含 ImageBase）。可信度标记：**(H)** 反汇编直接读到；**(M)** 由读到的代码推出；**(U)** 没读，只知道有。
结论先行，细节在后。
> **现状（2026-10-15）**：已实现并正式发布（不再是开发开关）：`src/edf41mother.cpp` + 公用的 `src/edf41classes.{h,cpp}`（EDF6 侧的基础能力见 `docs/edf41-classes.md`）。
> ini `Edf41Mother=1` 默认开；`Edf41MotherParts`（部件位掩码，发布 31）、`Edf41MotherForce`（开发：强制启用开火 / 击毁部件）、`Edf41MotherPlace`（开发）。
> 下文第 0–6 节是最初的规格，「补完规格」一节纠正了其中几处（`+0x180` 是队伍、`+0xa24` 是自转角速度等），以补完规格为准；「实现（2026-10-15）」一节写的是插件实际怎么做、实机验证到哪一步；「补完（2026-10-15 下午）」一节是受伤 / 击毁 / 4.1 的船体死亡 / 发光 / 弱点开合 / 排气 / 音效的补完与实机记录（ini `Edf41MotherForce` 新位 16）。

## 0. 结论

1. UfoMother301 不是一个类，是 **5 个原生类 + 1 个共同基类**：`UfoMother301`（船体）、`_PartsBase`、`_Hatch`（SGO 里 HatchLow 与 HatchMiddle/Cover 共用）、`_Panel`、`_Cannon`、`_BigCannon`。脚本通过 `GetPartsObject`（3300）把炮当对象拿到手，再对炮单独设耐久触发，所以部件必须是真正的、带句柄的游戏对象，不能是船体内部的伪部件。EDF6 里没有任何一个类能承担这 5 个类的工作（见 `docs/edf5-weapons-plan.md` 8.1）。
2. 船体与部件都从 `GameObjectBase` 直接派生，EDF6 同样有 `GameObjectBase`（`UfoMother511` 的 RTTI 层级：`UfoMother511 → GameObjectBase → SceneObject → IRecordable(snapshot) → NetworkObject`）。差别：EDF6 多了 `IRecordable`，虚表由 4.1 的 28 槽长到 45 槽（`UfoMother511` 0x17ca278），消息分派的编号全换了。所以**不能把 4.1 的类整段搬过来，只能按 EDF6 的虚表和对象布局重新写**，4.1 的状态机、数据、计时是移植的规格。
3. 路线：插件里注册 5 个类工厂（照 `UfoMother511` 的 0x17ca200 工厂形状），每个类**克隆一个 EDF6 已有类的虚表与构造路径作底**（船体借 `UfoCarrier508` 或 `UfoMother511` 的基础设施，部件借 `MovingFortress_PartsBase`/`UfoMother511PartsBase`），只覆盖 Update、OnMessage、OnDamage、OnDestroy 等必要槽位；子弹、特效、音效、碰撞、模型、CAS、发生器、事件因子（9200）全部交给 EDF6 自己的服务。**行为逻辑（状态机、计时、旗标、动作表）我们自己写，一比一照 4.1。**
4. 估计：剩余逆向 1–1.5 周，实现 3–4 周，加上实机迭代；只有一个人、没法在本机启动游戏的话，真实日历时间按 6 周计。**最大风险是没有一次能离线验证的路径，且每个未判空的解引用都是整局崩溃（M153 的教训）。**

## 1. 4.1 的 UfoMother301

### 1.1 类与对象

| 类 | 基类 | 虚表 | 构造 | SGO 键里的类名 |
|---|---|---|---|---|
| UfoMother301 | GameObjectBase | 0xa99e08（28 槽）、次基表 0xa99ef0 | 0x24ece0 (H) | `UfoMother301` |
| UfoMother301_PartsBase | GameObjectBase | 0xa9a3f0 的前缀 | 0x25e480 起（挂接）(H) | — |
| UfoMother301_Hatch | PartsBase | 0xa9a3f0 | 未单独读 (U) | `UfoMother301_Hatch`（Hatch.sgo 与 Cover.sgo 都是它） |
| UfoMother301_Panel | PartsBase | 0xa9a530 | 0x25cad0 (H 确认键) | `UfoMother301_Panel` |
| UfoMother301_Cannon | PartsBase | 见 vt.py | 0x258eb0 (H 确认键) | `UfoMother301_Cannon` |
| UfoMother301_BigCannon | PartsBase | 见 vt.py | 0x255c30 (H 确认键) | `UfoMother301_BigCannon` |

虚表槽（4.1，部件各类共有的部分是 PartsBase）：0 析构、1 = 0x25e840（部件世界矩阵每帧更新）、2 = Update(dt)、3 = 子类后更新、6 = OnMessage(msg, data)（船体 0x250870；Hatch 0x25ba00；其余转基类 0x28db80）、9 = 部件 `Init/Reset`、11 = OnDamage/OnDeath（船体 0x251170，Panel 0x25d1c0，BigCannon 0x256880，Cannon 0x259c00）。(M)

船体关键字段（this+偏移，H 除非标注）：
- 0x60..0x8f 旋转基（yaw 的 sin/cos 填 0x60/0x68/0x80/0x88），0x90 位置 vec4；0x16c 当前耐久（float，≤0 即死）；0x180 等级/难度（4.1 的 `level` 概念，Cannon 用来缩放）；0x258/0x25c 帧时间系数（对子对象调 0x28eab0）。
- 0x320 带骨骼的模型实例，0x390 节点集（`waist`、`weak` 在这里按名查），0x740 ragdoll（`RagDollProxys` 物理壳，来自 `ragdoll[0]` 的 SHKT），0x898 碰撞体绑定。
- 0x868 **序列器**（vtable 0xa99df0）：一个小协程调度器，0x254c20 往里排一个 `functor(this, phase)`，phase 0 = 进入，1 = 每帧，2 = 退出；船体用它跑「发光 / 死亡」。0xb00/0xb10 是第二个状态机宿主，船体自己的 HSM（初始状态 0x252700，`UfoMother301::State_Main`）。
- 0x9a0..0x9bf、0xa10..0xa2f：两组 vec4 颜色参数的当前值与目标值，被序列 0/1/2 改写，每帧向目标插值（比例 `0xa30`/`0x9c0`），再喂给 CAS / 材质动画的 `light0_color`、`scroll_light0`（PartsBase 0x25e5e0 里是这两个名字）。(H)
- 0x9d0 路径目标点，0x9e0 速度向量，0x9f0 目标速度向量（向目标按 0.001/帧收敛，死后改乘 `0.99xxx` 衰减），0xa00/0xa04/0xa08 俯仰/偏航/横滚，0xa10 角速度，0xa20 目标角速度。(H)
- 部件列表（每个是 16 字节项 `{control*, object*}` 的弱引用向量，begin 在 +8、count 在 +0x18）：0xa40 全部子对象，0xa60 HatchLow，0xa80 BigCannon，0xaa0 Cannon，0xac0 Panel，0xae0 第 5 组（空）。(H)
- 0xb40 烟发射器数组（每项 0x300 字节，数量 = `mp_HatchEmission[0]`），0xb48 起始指针，0xb58 数量；0xb70 受击震动帧计数；0xb78/0xb80/0xb90/0xb94 弱点节点指针与参数；0xba4 CAS 里 `main` 播放器的索引；0xbb0 一个 6 项帧计数器（每帧自减，用来限流特效）；0xbd0/0xc10/0xc20/0xc60 三个音效句柄（飞行 / 排气 / 受损）；0xcc0..0xcf4 网络平滑用（remote 副本）。(H)

### 1.2 船体 SGO 键（`UfoMother301.sgo`，ctor 0x24ece0 读取）

| 键 | 值（实测） | 含义 |
|---|---|---|
| `animation_model` | `[[ [UfoMother301.mrab, MotherShip301Body.mdb], [..._light.mrab, ..._light.mdb] ], app:/object/UfoMother301.cas, MAB]` | 船体模型（骨骼：`mdl`、`MotherShip301Body`、`globalSRT`、`waist`、`hatch`、`weak`）；CAS；MAB 里只有一个目标定位点 `ターゲット`@`weak`（半径 5） (H) |
| `ragdoll` | `[app:/object/MotherShip301Body.shkt, SGO块]` | 船体物理壳 (H) |
| `game_object_durability` | 4000 | 船体耐久 (H) |
| `game_object_encount` | `[-10, 2000]` | 遭遇半径，4.1 用来触发「发现玩家」；负值 = 默认已遭遇 (M) |
| `game_object_camera_setting`、`game_object_ai_route_update_distance` (50) | 通用 | 基类读 (H) |
| `game_object_kill_counter` | `MotherShip301KillCount` | 击杀计数器名 (M) |
| `game_sound` | `tikyuu4_en_MotherShip.acb` | ACB；EDF6 自带该 ACB (H，见 plan 8) |
| `mp_Weakpoint` | `["weak", [0,-40,0], 80, 20]` | 弱点：节点名、偏移、两个半径（0xb94 = 80、0xb90 = 20；受击时在弱点附近随机爆炸，80 是散布半径，20 是特效尺寸） (M) |
| `mp_HatchLow` | 4 项 | 4 个舱门，见 1.3 |
| `mp_HatchMiddle` | 4 项 | 4 个舱盖 Cover（同一个类），见 1.3 |
| `mp_Panel` | 4 组：22 / 40 / 40 / 23 块 | 125 块装甲兼炮台，见 1.3 |
| `mp_PanelParameter` | `[[0.0007,1],[-0.0013,1],[0.0007,0],[-0.0002,0]]` | 4 个 Panel 组各一对（速率，旗标）。读取位置未单独核 (U)：很可能是各组板甲的缓慢起伏 / 自转速率 |
| `mp_Cannon` | 10 项 | 10 门中型炮 |
| `mp_BigCannon` | 1 项 | 主炮（原点） |
| `mp_HatchEmission` | `[16, "weak", 85, 20]` | 烟发射器：16 个粒子槽、挂在 `weak` 节点，半径 85 / 20；贴图 `煙_01.dds`（0x252fd0 读取）(H) |
| `resource` | 6 个子 SGO | 预载列表 (H) |

### 1.3 部件怎么放（0x24fff0，H）

每个 `mp_*` 项是 `[sgo 路径, vec3 A, vec3 B]`。对每项：
1. 用对象工厂（全局 0xcc8528，0x5686e0/0x8d8f9c）按路径建出子对象；
2. 调 0x25e480(部件, 船体, **`waist` 节点**, A, B)：A 是**欧拉角（度）**，在部件的 0x320 世界矩阵里依次作 X（sin/cos 直接填）、Y（0xca080）、Z（0xeb910）旋转；B 是**相对 `waist` 节点的局部位置**（写进 0x350，w = 1）；部件 +0x360 = 船体指针，+0x368 = 节点指针，并把部件塞进船体 +0x38 的子列表（船体 0x50/0x58）；
3. 第 5 个参数 `r9b`：1 = 正常，0 = 部件 +0x904 置 1（这个部件是**静态装饰**，更新时不受旗标驱动，见 0x250520）。`mp_Panel` 的前两组（22、40）`r9b = 1`，后两组（40、23）`r9b = 0`；其余 mp_* 都是 1；
4. 随机种子：`0x210`/`0x250` 两个随机状态用位置哈希初始化（`0xdeadbeef`/`0xbeefdead` + 位置混合），所以同一位置的部件每局时序相同 (H)；
5. 对子对象调虚表 +0xd8（构造后初始化）；把弱引用追加到 0xa40 和对应组列表（HatchLow → 0xa60，BigCannon → 0xa80，Cannon → 0xaa0，Panel 四组 → 0xac0（第一组 assign、其余 append））。**HatchMiddle 的 Cover 只进 0xa40，不进任何组**。

具体数据（实测）：
- 舱门 4 个，欧拉 `[48.5, yaw, 0]`，yaw = 0 / 180 / -90 / 90，位置 `(0,-283.3,±239.2)` / `(±239.2,-283.3,0)`；舱盖 4 个，欧拉 `[0, yaw, 0]`，位置 `(0,0,±378)` / `(±378,0,0)`。
- 10 门中型炮：欧拉 `[-61.5, 27+36k, 0]`（k = 0..9），位置在半径 ≈10.4、y ≈ -5.8 的圆上（炮口朝下斜向外）。
- 主炮 1 门，原点。
- 板甲 125 块：组 0 = `Panel02`（22 块，`mp_type 1`，PlasmaBullet01 [1,16]），其余 `Panel`（`mp_type 0`，LaserBullet02 [0]）。

### 1.4 脚本动作：`ObjectAction` 消息（4.1：虚表槽 6 = 0x250870，消息 0x80000009，跳转表 0x250f6c，18 项）(H)

| 动作 | 效果 |
|---|---|
| 0 / 1 / 2 | 往序列器排 functor 0x251e50 / 0x251e90 / 0x251ef0（phase 0 时执行一次，无后续）：**0**：颜色 A、B 立即清零 `(0,0,0,1)`、B = `(0,0,0,1)`，+0xa24 = 0（发光全灭）；**1**：A = `(10,10,10,1)`、B = `(2,2,2,1)`，+0xa24 = 0（强白光）；**2**：B 目标 = `(2,0,0,1)`、+0x9c0 = 0.01、+0xa24 = 0.0003（缓慢转红，也是构造函数末尾排的初始序列）。对应材质动画 `light0_color` / `scroll_light0`。纯外观。 |
| 3 / 4 | 0x251bc0(this, 0, 1/0)：**BigCannon 组全员的 +0x900 置 1 / 0**（仅当部件的 +0x360 对象耐久 > 0；0x251bc0 第二参 0 = BigCannon，1 = Cannon，2 = Panel，3 = Hatch，其他 = 第 5 组） |
| 5 / 6 | BigCannon 组的 +0x901 置 1 / 0 |
| 7 | BigCannon 组的 +0x902 置 1 |
| 8 / 9 | Cannon 组的 +0x900 置 1 / 0（同样仅耐久 > 0 的） |
| 10 / 11 | Cannon 组的 +0x901 置 1 / 0 |
| 12 | Cannon 组的 +0x902 置 1 |
| 13 / 14 | Panel 组的 +0x900 置 1 / 0 |
| 15 / 16 | Panel 组的 +0x901 置 1 / 0 |
| 17 | Panel 组的 +0x902 置 1 |

旗标语义（由使用点推，M）：+0x900 = 「启用」（总开关，动作 3/8/13 开）；+0x901 = 「攻击」（允许进入开火状态，Cannon 的 0x259f00 / Panel 的 0x25d4ac 等都判 `+0x901 && !(+0x904)`）；+0x902 = 「强化 / 一次性触发」（置位后不清，具体读取点 (U)，Cannon 里没见到判它，疑在 Panel / BigCannon 的状态里）。
没有任何动作操作舱门。脚本对舱门的唯一接口是生成器（见 1.7）。

其他消息：
- **0x80000012**（GetPartsObject）：`{int group; int index; out 弱引用(16B)}`：group 0 = BigCannon 列表的**第一项**（忽略 index），group 1 = Cannon[index]（**无边界检查**），其他返回空。脚本里 `GetPartsObject(obj, grp, idx)` 的 grp/idx 对应这个。(H)
- **0x80000013**：`{int group; out int}` 返回该组仍存活的部件数（0x251c60，同样 0 = BigCannon、1 = Cannon、2 = Panel、3 = Hatch）。脚本里没找到使用点 (U)。
- **0x20100**：`{...; int +0x40 = 舱门索引}` 转发给 HatchLow[索引] 的同号消息，舱门回 `+0xad0` 的世界变换（= 舱门 `hatch` 骨骼的当前矩阵）。这是**生成器位置**，见 1.7。(H)
- 其余转基类 0x28db80。

### 1.5 部件类（均为每个部件一个 HSM，状态函数签名 `state(this, phase)`，phase 0/1/2 = 进入/每帧/退出；状态机宿主在部件 +0xb00 或 +0xd40，帧计数在 `[+0xb28]+4`）

共性 (H)：
- PartsBase 每帧（0x25e840）：把部件世界矩阵 = 局部欧拉 ∘ 局部位置 ∘ 船体 `waist` 当前矩阵，写进 0x60..0x9f；`+0x905`（首次耐久 ≤ 0 的边沿）触发一次「坠落」：0xa10..0xa18 = 100，0xa30 = 0.02，之后部件按 `0x920` 速度脱离船体下落，每帧 y 速度 -0.002。
- 船体 0x250520 每帧遍历 0xa40，对每个存活子对象调 0x28ec10(child, 难度, 允许)；子对象 +0x1e8 的 bit0 在「+0x900 为 0、或 +0x906 与船体 +0x180 == 3 不符」时置位（= 屏蔽 / 休眠）。
- 网络：状态切换时 0x25a740 拼一条同步消息（`NetworkObject`）；EDF6 对应物是 `IRecordable` + 网络状态消息，**联机必须重写**，见 3.5。

**Cannon**（中型炮，耐久 600，`mp_CannonCustomParameter [2, 40, 0, 0.0005]`；ctor 0x258eb0；Update 0x2599d0）：
- SGO：`mpc_CannonBulletFactory = RocketBullet01`（炮）、`mpc_BeamBulletFactory = BeamBullet01` + `mpc_BeamBulletCustomParam`（束）、`mpc_FireCannonSetting ["cannon", [0,-360,0]]`（炮口节点 + 相对方向）、`mpc_FireBeamSetting` 4 项 `[hatch_r/f/l/b, 方向]`（4 个束口节点，方向 `(±18,-230,0)`/`(0,-230,±18)`）、`mpc_ExplosionJoint "cannon"`、`mp_PurgeSetting ["waist"]`（坠落时脱离的节点）、`game_object_drop_item`。炮模型 `MotherShip301tyucannon.mdb` 骨骼：`waist`、`cannon`、`hatch_l/b/r/f`。CAS 播放器 `main`，状态 `appear`、`open`、`close`、`close_state`。
- 状态（地址即 functor）：0x259d20 出现（播 `appear`）→ 0x259f00 待机（等 +0x901 且 !+0x904）→ 0x259f90 蓄力等待：**随机 1800–3599 帧**（`0x708 + rand%0x708`，退出时重掷）→ 0x25a060 开炮：按 `耐久/600` 缩放 0xa10.. 并逐帧充能，**450 帧**生成充能粒子，**≥600 帧**发射一发 RocketBullet01（0xe6ed0 子弹生成，方向来自 `mpc_FireCannonSetting`）→ 回 0x259f00；0x25a5c0 / 0x25a650 = 束齐射：每 `[+0xd04]` 帧（来自 `mp_CannonCustomParameter`）用 BeamBullet01 从 4 个束口开火并播 `母船UFOパネルレーザー`；0x25a2d0 = 被击毁（排 0x25a410 坠落 288–320 帧间每 4 帧一次爆炸，粒子 30 单位、速度 100+30n）→ 完毕后 0x56bbc0（销毁）。
- 效果 / 音效名（全是 ACB cue 名，EDF6 同 ACB）：`母船UFOパネルキャノン / レーザー / 爆発 / 破壊 / 起動`、`母船UFO中型砲台墜落`；发生位置节点 `発生位置`。
- 读完度：HSM 骨架与计时 (H)；束齐射细节（每帧方向、`+0xd04`、0x259710 / 0x25ac10 内部）(U)；击毁坠落细节 (M)。

**BigCannon**（主炮，耐久 2400，`mp_LaserCustomParameter [[0],[2]]`；ctor 0x255c30；Update 0x256760）：
- 状态：`close_state` / `open` / `close`（CAS 动画）、充能（`母船UFO砲台チャージ`→`チャージ完了`）、发射（`砲台発射`、命中 `砲台命中`）、破坏 / 爆发（`砲台破壊` / `砲台爆発`）、受损（`砲台ダメージ`）。
- 子弹：LaserBullet02（通用 `LaserBullet02` 工厂，参数 `[0]` / `[2]` 两种），计时常数 480 帧、30 帧、10 单位、1.5 倍、-790（弹道下偏）。具体状态图：函数入口 0x256950 / 0x256a90 / 0x256c20 / 0x256ce0 / 0x256f60 / 0x257380 / 0x2574f0 / 0x257630。
- 读完度：骨架 (M)，状态图细节 (U)。

**Panel**（板甲 / 小炮台，耐久 120，`mp_type` 0 = LaserBullet02 `[0]`、1 = PlasmaBullet01 `[1,16]`；ctor 0x25cad0；Update 0x25cff0）：
- 状态入口 0x25d360 / 0x25d400 / 0x25d510 / 0x25d620 / 0x25d740；效果 `母船UFOパネルキャノン(命中)` `…レーザー` `…起動` `…破壊` `…爆発`。+0x900 开 → 起动动作，+0x901 开且 !+0x904 → 定时开火，+0x904 = 装饰板，不开火。`mp_PanelParameter` 的值让不同组板甲在起动后做缓慢起伏 (U)。
- 读完度：骨架 (M)，开火间隔 / 瞄准 (U)。

**Hatch**（舱门 / 舱盖，耐久 100，无额外键；Update 0x25ba30，消息 0x25ba00）：
- 状态 `open` / `close`（CAS）、0x25bb00 / 0x25bba0 / 0x25bd30 / 0x25bde0；效果同 Panel 的几条；每帧用 `hatch` 骨骼的矩阵更新 +0xad0（生成器变换，0x331270，来源 0x3232d0 对 +0xcd8 的节点求值）。
- **什么时候开 / 关**：HatchLow 在 0xa60，只有 0x251bc0(…,3,…) 能写它的 +0x900，而动作表里没有 3 号组调用；`open` 因此可能由生成器流程（`SetInstantEnemyGenerator` 之后）或船体内部状态触发，**(U) 必须读 0x25bb00 一族的进入条件**，这是最小子集的前置读。
- 读完度：消息接口 (H)，开合触发 (U)。

### 1.6 船体行为

- **移动**：基类 Update（0x28d700，被 `UfoMother301` 的 Update 首先调用）完成路径跟随（`SetAiRoute` 3101 / `SetAiMoveSpeed` 3100 / 到达 `Factor_AiMoveEnd` 9300 都是基类能力）；船体自己只做平滑：速度每帧向目标速度按 0.001 收敛，位置 += 速度，偏航角速度同理，**不转向速度**；死亡后速度每帧乘 `0.99xxx`（f=0xab6624，约 0.9995 级）并且 y 速度每帧 -0.0003（`+0xa31` 坠落）。(H)
- **旋转**：偏航 = 0xa04（sin/cos 填基）；无俯仰 / 横滚驱动，0xa00/0xa08 只做平滑累加。(H)
- **受击**：船体耐久 = 弱点（`weak` 节点）受的伤；任一受击把 0xb70 置成若干帧；每帧 `0xb70 & 7 == 0` 时随机选弱点附近一点（半径 `0xb94 = 80` 乘随机）生成爆炸（0x1b16b0，尺寸 `0xb90 = 20`），并在 0xbd0 播放受损音。伤害数值走基类的统一伤害管线（`GameObjectBase` 的 `OnDamage` 把耐久写进 0x16c）。(M)
- **死亡**：0x251170（OnDeath，仅一次，置 0x8b8）→ 排 functor 0x251f30：先在 `y-350` 处生成一次「本体墜落前爆発」；然后关闭所有烟发射器（0xb0 置 0）、清 0xa24、把所有 Cannon 的 +0x900 置 0（仅耐久 > 0 的）……后续（Panel / BigCannon 的关闭、坠落速度、摧毁时序）(U)。
- **发光**：序列 0/1/2（见 1.4）；船体每帧按 0.0016667/帧推进 0x994 时间，并让两组 vec4 向目标插值，再喂材质动画。
- **烟**：`mp_HatchEmission` 16 个发射槽，0x2537f0 每帧按 `weak` 节点矩阵更新各槽位置，是纯外观。
- **相机 / 遭遇**：基类。
- **和玩家的碰撞**：船体 ragdoll 壳（`MotherShip301Body.shkt`）+ 弱点；部件各有 `ragdoll`（`MotherShip301cannon.shkt`、`…panel.shkt`、`…hatch.shkt`、`…cover.shkt`、`…tyucannon.shkt`）。

### 1.7 与脚本的接口

| 脚本原生 | 4.1 | 对船体 / 部件的要求 |
|---|---|---|
| `CreateEnemy` 2000 / `CreateNeutral` 1020 | 创建对象，返回句柄 | 工厂按 `xgs_scene_object_class` 建出 `UfoMother301` |
| `ObjectAction` 3200 | 消息 0x80000009 | 见 1.4；M016 用 3 4 7；M110 用 3 7 4；M130 用 0 2 13 14 15 16；M210 用 3 5 8 10 13 15；M603 12 次（动作号见 `tmp/mother41/all_m.py` 输出） |
| `GetPartsObject` 3300 | 消息 0x80000012 | 只有 M210 用（grp 0 idx 0 = 主炮，grp 1 idx 0..9 = 十门炮） |
| `Factor_ObjectDurability` 9200 / 9201 | 基类事件因子 | 对船体以及 `GetPartsObject` 返回的炮；耐久百分比由基类从 0x16c 与 `game_object_durability` 算 |
| `SetEnemyGenerator` 2100 / `SetInstantEnemyGenerator` 2101 | `(obj, 舱门索引, sgo, 数量, 间隔 1.0, 半径/时长 1.5, 1)` | **生成位置 = HatchLow[索引] 的 `hatch` 骨骼世界矩阵**（消息 0x20100）。M016 在船体上建 4 个 `Ufo301.sgo` 发生器（索引 0..3），M110 建 16 个 |
| `SetAiRoute` 3101 / `SetAiMoveSpeed` 3100 / `Factor_AiMoveEnd` 9300 / `DeleteObject` 3000 | 基类 | 基类路径跟随即可 |
| `LookCameraToObject` 420 | 基类 | M201 用（401 系列） |
| `SetObjectDurability` 3020（M130） | 基类 | **设的是最低耐久**：EDF6 原生 0x2288C0 写 `+0x2F0 = 百分比/100 × 最大耐久`（ApplyDamage 0x548172 把 HP 夹在 `[+0x2F0, +0x2F4]`）。M130 开局给 100：船体在这一段打不动（2026-10-15 实机：弱点刚体命中、受击回调都到了，HP 一直 5464 / 5464、最低 5464）。原「设成 100%」的理解是错的 |

## 2. EDF6 里能直接用的构件（EDF.dll）

| 需要 | EDF6 里的东西 | 证据 |
|---|---|---|
| 类工厂形状 | 工厂对象虚表 0x17ca200（UfoMother511）：+0 析构 0x4fd940、+8 取类名 0x4fd900（返回宽串 `UfoMother511`）、+0x10 创建 0x4fd910（`operator new(0x2110)` + 构造 0x4fe740）；引用该虚表的两处：0x50a1e7 与 0xfc14ba（后者在一个大的静态初始化函数里，是登记点） | H |
| 消息分派 | `SendMessage` = 0x1da480(obj, msg, data*)；对象句柄 → 对象 = 0x20c150；三步分派：虚表 +0x50（预处理）、+0x48（处理，返回是否处理）、+0x58（后处理），见 3300 的 0x222470 | H |
| `ObjectAction`（3200） | 原生分派函数 0x20ff70（两层跳转表 0x210014 / 0x21016d），3200 的情形在 0x212a36：取 (句柄, int) → 0x20c150 → `SendMessage(obj, 0x1000000d, &int)`。类的 OnMessage（`UfoMother511` 是虚表槽 9 = 0x503a70）按消息号分派：0x20100、0x20101、0x20103、0x1000000d、其余走 0x504319 | H |
| `GetPartsObject`（3300） | 0x222470：拼 `{int grp; int idx; out 弱引用}`，经三步分派发消息 **0x10000017**，把弱引用转成脚本句柄返回 | H |
| `Factor_ObjectDurability`（9200） | 0x221320：对象存在时，在事件管理器（`rdi+0x138`）里新建一个 `EventFactor_ObjectDurability`（RTTI 名 0x179c908），读目标对象的耐久比例触发脚本回调 | M：耐久读取走哪个虚函数需确认 |
| 生成器位置 | 4.1 用消息 0x20100 向 Hatch 要变换；EDF6 的 `UfoMother511RingUnitParts`（unit1 的 `generators` 键）和 `UfoCarrier508` 的舱门在 EDF6 里同样接 `SetInstantEnemyGenerator`：对应消息 / 虚函数是 0x20100 / 0x20101（511 的 OnMessage 里 case 0x20100、0x20101），**需要读 0x503dfc / 0x503d03 两段** | M |
| 子弹 | EDF6 保留了 `LaserBullet02`、`PlasmaBullet01`、`BeamBullet01`、`RocketBullet01` 工厂（E510 炮台 SGO、E511 歼灭炮 SGO 里就在用同样的名字和参数形状）。生成函数的 EDF6 对应物：4.1 的 0xe6ed0（工厂表 0xcc8528，名字，参数，发射信息）在 EDF6 要找 | M / U |
| 特效 / 音效 | 4.1 的 0x3e83f0（特效管理 0xcc8508，位置，cue 名 `母船UFO…`）；EDF6 的对应函数同样按 cue 名播 ACB（`tikyuu4_en_MotherShip.acb` EDF6 自带） | M：对应函数地址 U |
| 多部件敌人 | `MovingFortress`（`parts_setting`: `[类型, ?, ?, 节点名, 子 SGO]` 按节点名挂炮台子对象，`fort_parts_destroy_info`，`core_hatch_interval`）、`UfoMother511`（`core_parts` 4 门歼灭炮 + 环单元）、`EDF6_GrandMother`（臂 / 门 / 弱点）。这三族说明 EDF6 的「主对象拥有若干带句柄子对象」的模式存在，对象创建 + 挂接可以借它们的函数 | H（SGO 键） |
| 弱点 / 碰撞 | ragdoll 壳 + 目标定位点（MAB `ターゲット`）这条通路 `UfoMother511` 与 4.1 一致（`ragdoll` 键、`RagDollProxys.*`）；SHKT 2014 packfile EDF6 能读（plan 8，蚁巢实机验证过） | M |
| 数据驱动路径跟随 | 基类；EDF6 `UfoCarrier508` 在 EDF6 里同样接 `SetAiRoute`，已经被 4.1 的 Carrier301/401 当宿主使用（plan 8），说明 `GameObjectBase` 子类能吃脚本的路径原生 | H（已实机） |
| 先例 | 本仓 `src/edf41ai.cpp`（feat/edf41-behaviour）：SGO 键 `edf41_hector` 门控、`RedirectCall` 钩子、用游戏自己的 SGO 访问器读键（0xD7480 / 0x53060 / 0x52DE0 / 0x52F30）。同一套手法可以做「按 SGO 键识别 4.1 母舰」。 | H |

## 3. 实现设计

### 3.1 总体：一个插件模块 `src/edf41mother.cpp`

- SGO 里 `xgs_scene_object_class` 保持 4.1 原名（`UfoMother301` 等；`make_edf41_objects` 不做别名，只做格式转换，类似第一层对象）。
- 模块在加载时：登记 5 个工厂；对每个工厂：`name()` 返回类名；`create()` 分配 `sizeof(EDF6底类) + sizeof(扩展)` 字节，调底类构造，**换虚表指针**为克隆表（见 3.2），构造扩展区。
- 键门控：这些类只会出现在 4.1 的任务里，不需要键；但在 SGO 里加 `edf41_mother: 1` 作为存档标记，便于诊断。

### 3.2 虚表克隆而不是继承

EDF6 的 `GameObjectBase` 子类虚表约 45–54 槽，且槽位语义没有符号。做法：
1. 为每个类选一个**底类**（船体 `UfoMother511`、部件 `UfoMother511PartsBase`/`MovingFortress_PartsBase`——取 Update/OnMessage/OnDamage 槽位最清晰、构造器最容易复用的）；
2. 运行时拷贝底类虚表（含 RTTI 指针前一格）到插件的静态数组，替换我们要接管的槽：Update、OnMessage（0x1000000d / 0x10000017 / 0x20100 …）、OnDamage / OnDestroy、析构；其余槽继续用底类的；
3. 因为我们自己创建对象，不需要 hook 游戏的创建路径；工厂登记是唯一必须改游戏状态的点（对登记表追加一项；表在 0xfc14ba 所在的静态初始化里，**需要确认是 `std::vector` / 固定数组 / 链表**）。

代价：对象里会带着底类的字段（511 的环、屏障、发生器等）。构造时不调用 511 的 SGO 键读取（0x505010），所以那些字段保持零值；每个被我们覆盖的槽都必须不碰这些字段，未覆盖的槽（例如 `Draw`、`GetBounds`）要逐个确认对零值字段安全。**这是整个设计里最需要先做原型验证的一环。**

备选：自己从零派生 `GameObjectBase`（45 槽全自己填）。能避开底类的包袱，但每个槽都要有一个正确实现，逆向量约翻倍。

### 3.3 各类的自写 / 委托边界

| 行为 | 委托给 EDF6 | 自己写（逐条照 4.1） |
|---|---|---|
| 对象创建 / 句柄 / 弱引用 / 脚本句柄转换 | 工厂框架、0x20c150、弱引用控制块 | 工厂登记 |
| SGO 读取、模型、CAS、ragdoll 加载 | 基类构造器（`animation_model` / `ragdoll` / `game_object_*` 由基类读）| `mp_*`、`mpc_*`、`mp_Weakpoint`、`mp_HatchEmission`、`mp_PanelParameter`、`mp_CannonCustomParameter` 等 4.1 专有键（用 `edf41ai.cpp` 的访问器读） |
| 部件放置 | 子对象创建（对象工厂）| 欧拉 → 世界矩阵（照 0x25e480）、挂 `waist`、静态旗标 +0x904、随机种子初始化 |
| 路径跟随 / 速度 / 偏航 | 基类（3100 / 3101 / 9300）| 船体的平滑（0.001 收敛）、死后阻尼与下坠 |
| 耐久 / 受击 / 9200 | 基类伤害管线、`EventFactor_ObjectDurability` | 弱点受击震动与随机爆炸；部件死亡的状态切换 |
| 子弹 | EDF6 子弹工厂 | 炮口节点、方向、速度、参数、间隔（Cannon 的火箭 / 束，Panel 的激光 / 等离子，BigCannon 的激光） |
| 特效 / 音效 | EDF6 特效与 ACB | cue 名与触发时机（从 4.1 读到的表） |
| 状态机 | — | 全部：每个类一份 C++ 状态机（phase 0/1/2 + 帧计数），照 4.1 函数一一翻译，不做「差不多」 |
| 动作表 / 消息 | 消息总线（0x1da480 / 三步分派）| `0x1000000d`：18 项动作表（1.4）；`0x10000017`：GetParts；`0x20100`：舱门变换；+ 存活数查询 |
| 发光序列 | 材质动画 / CAS 变量写法 | 序列器（可以直接用成员变量 + 插值，不需要重造协程框架） |
| 网络 / 快照 | `IRecordable` / `NetworkObject` 框架 | 状态序列化（`0x25a740` 对应物）——见 3.5 |

### 3.4 数据侧（`tools/make_edf41_objects.py`）

- 新增 1 个 ROOT：`UFOMOTHER301.SGO`（6 个关）。闭包 = 6 个子 SGO（`Hatch`、`Cover`、`Panel`、`Panel02`、`Cannon`、`BigCannon`）+ `UFOMOTHER301.MRAB`（6 个 MDB）+ 5 个 CAS（主、BigCannon、Cannon、Cover、Hatch）+ 6 个 SHKT（`MotherShip301Body/Cannon/Cover/Hatch/Panel/TyuCannon`）+ `UFOMOTHER301_light.mrab`（4.1 自己也没有，跳过）。现有转换管线（`legacy_assets` / `cas_legacy` / `legacy_sgo`）覆盖这些格式。
- SGO 不改类名。登记表 `objects.json` 里这个对象的 `class` 是 `UfoMother301`——**selftest `edf41_motherships_not_faked` 当前禁止它**，插件落地时与 8.1 节一起删，换成「类名在插件的工厂表里」的断言。
- 401 系列（本文不覆盖）：板格 + `action_list` 数据驱动，是另一份规格。

### 3.5 联机 / 回放

EDF6 的对象有 `IRecordable`（snapshot）和 `NetworkObject`。4.1 的部件在状态切换时发网络同步（0x25a740）。移植时：
- 主机是权威，客户端只接受状态 / 位置快照并播放；
- 最小做法：快照只含 (船体位置 / 偏航 / 耐久 / 各部件存活位 / 当前状态号)；客户端不跑 AI、不开火（只显示子弹，子弹由 EDF6 子弹同步）；
- 这块 EDF6 的接口与 4.1 完全不同，需要单独读 `IRecordable` 的虚函数（UfoMother511 的 45 槽里 34–44 可能是）。**不处理联机会让联机里出现看不见的船或崩溃**，必须至少做到「联机时拒绝加载这些关」或「客户端只显示」。

## 4. 范围

### 4.1 最小可玩子集（6 关全部要的，且每项都与 4.1 一致）

1. 船体：出生、路径跟随、偏航平滑、耐久 4000、弱点受击爆炸、死亡（含坠落和关闭炮）、`9200`。
2. 部件放置：1 + 10 + 4 + 4 + 125 个子对象，位置 / 欧拉 / 随机种子 / 静态旗标。
3. 动作 0–17 全部（含发光序列 0/1/2；发光外观先接材质动画变量）。
4. Cannon：完整 HSM（出现、待机、1800–3599 帧蓄力、450/600 帧充能发射 RocketBullet01、束齐射、击毁坠落）；Panel：激光 / 等离子开火；BigCannon：开合 + 充能 + 激光 + 击毁；Hatch：开合 + 生成器变换（0x20100）。
5. 脚本接口：`GetPartsObject`（grp 0/1）、`0x80000013` 对应查询、生成器（索引 0..3）、`Factor_ObjectDurability`、`SetObjectDurability`。
6. 联机：至少「联机时不加载」。

**这就是全部**——因为动作表里的每一项、每个部件、每个状态都被这 6 关的某一关用到（M016 用主炮，M130 / M603 用板甲，M210 用十门炮 + 主炮 + 板甲）。没有可以安全砍掉的一块，除非先逐关核对（建议先做 M016 → 因为它只用动作 3 4 7、`3101`、`9300` 和 4 个生成器，是最小的闭环）。

### 4.2 到完全一致还要加

（2026-10-15 补完后剩下的；做完的——船体死亡编排、受击 / 飞行 / 排气音、部件坠落、难度缩放、`mp_PanelParameter`（4.1 不读）——见「补完」两节。）
- 排气口用的是 EDF6 的 ExplosionWind 烟团，不是 4.1 自己的持久粒子发射器（0x1C2D80 一族：寿命 300 帧的 `煙_01` 流）：位置、开关时机、随耐久缩放照 4.1，外观近似（M）。
- 船体活着时的移动用 UfoCarrier508 自己的 AI 移动控制器（`+0x340`：控制器 +0xC / +0x24 给 xz 速度、+0x3C 给偏航，0x4F74D0），不是 4.1 的「速度向（路线点 − 位置，长度截到移动速度 × 0.5）以 0.001 收敛」（补完规格 6.1）。改写需要从控制器取路线点并保证 `Factor_AiMoveEnd`（9300）照常触发，任务流程依赖它；没做。死后的移动是 4.1 的（插件接管）。
- 联机主客同步（3.5 完整版；另一条分支 `feat/edf41-online` 在做）。
- `.shkt` 物理：4.1 的物理壳在 EDF6 里能用（刚体名 `RagDollProxys.*`，弱点按名找到、命中判定按刚体），与 4.1 碰撞细节的差异未逐项比。

## 5. 还没读完的（逆向待办，按优先级）

（1–5、8、9 已读完：见「阶段 1」「补完规格」与「补完」三节。6 的结论见 4.2 第 2 条。）

| # | 要读什么 | 地址 | 为什么 |
|---|---|---|---|
| 7 | `IRecordable` 与网络消息 | EDF6 `UfoMother511` 虚表 34–44 | 联机（`feat/edf41-online`） |

## 6. 工作量与风险

### 工作量（一个人，含迭代）

| 阶段 | 内容 | 估计 |
|---|---|---|
| A 逆向补完 | 第 5 节 1–9 | 1–1.5 周 |
| B 原型 | 工厂登记 + 克隆虚表跑通一个空船体（出生、不崩溃、被脚本删除）| 1 周（含试错，这一步最不确定）|
| C 船体 + 动作表 + 生成器 + 9200 | 到 M016 能跑 | 1 周 |
| D 四个部件类 | 状态机逐个翻译、子弹、特效 | 1.5–2 周 |
| E 数据管线 + selftest | 闭包、转换、守卫、移除旧守卫 | 3–4 天 |
| F 联机兜底 | 至少拒绝加载 | 2 天；完整同步另算 1–2 周 |
| G 实机 | 6 关逐关，每关一轮崩溃 / 行为差异 | 1 周以上（需要用户 / 实机）|
| 合计 | | **约 5–6 周**；完整联机同步再加 1–2 周 |

### 风险（按严重度）

1. **没有离线验证手段。** 虚表克隆里的任何一个槽接错、任何一个未判空的解引用，都是整局崩溃。M153 的教训：模板里一个被类无检查读取的键缺失，崩在 0x551E0。缓解：每个类先只接最少的槽，逐槽打开；所有读 SGO 键的地方走「缺失即退化」的访问器；崩溃现场用 `CrashMap`（本仓已有 crash-map 工具）定位。
2. **底类包袱。** 借 `UfoMother511` 的对象布局，会带着环 / 屏障 / 发生器等零值字段；未覆盖的槽（绘制、包围盒、快照、回收）可能对零值崩溃。缓解：优先选「槽最少」的底类做原型，B 阶段先测 `Draw` / 回收 / 关卡结束销毁。
3. **工厂登记点。** 0xfc14ba 在一个巨大的静态初始化里；如果登记表是编译期固定数组，就要改成钩子（拦截「按类名取工厂」）而不是追加。缓解：先在 B 阶段找到「按名查工厂」的查找函数，钩它（本仓钩子框架可用 `RedirectCall`）。
4. **联机 / 回放。** `IRecordable` 与网络同步接口完全不同；如果不处理，主机跑得好好的，客户端看不见船或直接崩。缓解：联机时拒绝加载这 6 关，或客户端只做表现。
5. **物理 / 碰撞差异。** 4.1 的 SHKT（Havok 2014）EDF6 能读（蚁巢实测），但 125 块板甲 + 10 门炮的 ragdoll 数量大，EDF6 的约束 / 内存上限未知。缓解：实机先看加载时间和帧率。
6. **时序与随机。** 4.1 靠位置哈希初始化随机种子，计时（1800–3599 帧、450 / 600 帧）以 60 FPS 为基准。EDF6 的 dt 语义（固定步长 / 变长）要核对，否则「帧」变成「秒」。缓解：逆向第 6 项明确基类的步进。
7. **范围蔓延。** 401 系列（板格）是另一个同样大小的工程，不要在 301 的原型里提前泛化。

## 附录 A：与此规格相关的已有资料

- 调研脚本与反汇编：`C:\Users\wrds\.claude\jobs\4bf89026\tmp\mother41\`（任务脚本的动作 / 部件使用表 `all.py`）；本轮新增的：`D:\APP\vs_claude_code\mother-scratch\`（`ctor301.txt` 船体构造，`cannon_all.txt` / `big_all.txt` / `hatch_all.txt` / `panel_all.txt` 各部件区段反汇编，`fn_*.txt` 单函数）。
- 任务脚本：`edf41port/missions/<id>/MISSION.BVM`（EDF5 对 4.1 重编的脚本，原生函数号三代相同）。
- 4.1 → 6 对照表（已核）：`ObjectAction` 3200：4.1 消息 0x80000009 → EDF6 消息 0x1000000d；`GetPartsObject` 3300：0x80000012 → 0x10000017；生成器位置 0x20100（两边同号，EDF6 的 511 OnMessage 里有）。

## 阶段 1 原型（2026-10-13）：工厂登记 + 克隆虚表，仅开发开关

代码：`src/edf41mother.cpp`；数据：`python -B tools/make_edf41_objects.py --mother-dev` → `edf41port_dev/`（git 忽略，不进安装包）。

**登记（风险 3，已定）**：用游戏自己的登记函数，不改表、不钩查找。`EDF.dll` 0x1194410（按 SGO 建对象）读类名（0x119ADE0）→ `GetSceneObjectFactory` 0x1195C50（类名字节的 FNV-1a 哈希进 manager+0x2C8 的 unordered_map，找不到就记 `xgs::game::System::GetSceneObjectFactory` 并返回空）→ 调工厂虚表槽 2 `Create(factory, createInfo)`。游戏自己的类由 0x79D570 在启动时登记（0x701859 处的 call）：每个类一个只有虚表 {析构, 取名, 创建} 的 8 字节工厂、一个 shared_ptr 控制块（虚表、use@+8、weak@+0xC、对象@+0x10）、再调 `RegisterSceneObjectFactory` 0x119AF00(manager=[0x20B2958], &{工厂, 控制块})。我们钩 0x701859（RedirectCall，校验被换的目标），原函数返回后用同一个 0x119AF00 登记 `UfoMother301`，再用 0x1195C50 回查并记 `lookup gives ... OK/FAILED`。

**克隆虚表（风险 2）**：船体由 UfoCarrier508 自己的构造函数（0x4F8690，0x1630 字节）造，字段全部按类的做法初始化；之后只把主虚表指针（+0，次表 +0x120 不动）改成 0x17C9DC0 的拷贝（49 槽，RTTI 指针一并拷贝，dynamic_cast 仍认 UfoCarrier508）。接管：槽 0（析构）、槽 5（Update(dt)，类自己的，内部调 GameObjectBase::Update 0x54BE40）、槽 9（OnMessage(msg,data)，三步 SendMessage 0x1DA480 用槽 10/9/11）；伤害是槽 9 收到的消息 0xF0000000（基类 0x54A530 → ApplyDamage 0x547C30），ObjectAction 是 0x1000000D（int），GetPartsObject 是 0x10000017。其余 46 槽继承，每个换成「lock inc 计数 + jmp 原函数」的存根，日志里能看到游戏真正调用了哪些槽。

**数据**：UfoMother301.SGO 以 E508_CARRIER 为模板（类名保持 `UfoMother301`）；CAS 补 in_ring/out_ring 与 open_state/close_state（别名到 open/close）；MAB 补 `排出口`、`ダメージエフェクト` 两个定位点（508 的构造函数按名取用不判空）；去掉 `mp_*`、`resource`（部件类还不存在）。已知未解：`UfoCarrier_CorePartsName` 在 301 的 ragdoll 里查不到（索引 -1，类自己有默认），`material_animation` 来自 E508（名字对不上，按名跳过）。

**开发开关**：ini `Edf41MotherDev`（默认 0）。安装器仅在「源码树运行 + 环境变量 `EDF41_MOTHER_DEV=1` + edf41port_dev 存在」时并入开发对象、让战役包放行 M016 / M110 / M210 / M603（M123 / M130 另缺 `sk_*` 地图），并把 ini 写成 1；冻结的安装包、没有变量、没有目录时一律不并入且 ini 写回 0。

## 阶段 2 步骤 1（2026-10-13）：部件作为真对象

代码：`src/edf41mother.cpp`（`SpawnParts` / `MakePart` / `PartUpdateWrap`）；数据：`tools/make_edf41_objects.py` 的 `mother_dev_fix` + `mother_parts_fix`（仍只在 `--mother-dev`）；开关：ini `Edf41MotherParts`（位掩码 1 舱门 / 2 舱盖 / 4 中型炮 / 8 主炮 / 16 板甲 125 块，默认 15；0 = 不建）。

**EDF6 的「按 SGO 路径建子对象」（逆向，H）**：`0x11945E0(manager=[0x20B2958], const float matrix[16], const wchar_t* sgoPath, Context*)`。脚本原生 CreateEnemy（2000，`0x2154C0`）与 `UfoMother511` 的构造（`0x4FE740` 内 `0x4FEF00`）都走它：经资源缓存（`0x20B28D0`）取 SGO 树，`0x119ADE0` 取类名，`0x1195C50` 找工厂，把 `{矩阵, SGO 树, 根节点, 输出 shared_ptr}` 填进 Context（`+8/+0x10/+0x18/+0x20`），调工厂虚表槽 2 `Create`，再把对象插进 manager 的对象表（`manager+0x3B0` 链表，`+0xCB0` 是重入计数）。返回对象指针（已删除的对象返回 0）。Context 是 0x30 字节、首 8 字节为虚表 `0x1762068`、其余清零。对象自己的弱引用是 `+0x28`（对象）/`+0x30`（控制块）。传入的矩阵就是新对象的初始 `+0x60`。

**部件类的底**：`EventAnimationObject`（工厂虚表 `0x17D4CD0`，创建 `0x5B5EE0`：`new(0xC90)` + 构造 `0x5B5FB0`；虚表 `0x17D4D20` 45 槽，槽 0 析构 / 5 Update / 9 OnMessage 与 `UfoCarrier508` 同义）。构造只读 `animation_model`（模型 + CAS，CAS 玩家 `main` 取状态 `default`）与 `ragdoll`；Update `0x5B6D20` 调基类 Update `0x54BE40` 并用 `0x1100B90` 把对象矩阵（`+0x60`）交给模型。所以 4.1 的部件 SGO（同样只有 animation_model / ragdoll / 耐久 / 遭遇 / 镜头键）直接可用，只需要 CAS 有 `default` 状态（`mother_parts_fix`：别名到休息态；炮的 CAS 只有 `appear`；板甲 4.1 没有 CAS，借舱门的）。

**放置**：部件矩阵 = 本地矩阵 × 船体 `+0x60` 矩阵（行向量约定）；本地矩阵 = Rx(A.x)·Ry(A.y)·Rz(A.z)，位置放在最后一行（4.1 的顺序 X、Y、Z；**欧拉约定与 `waist` 节点相对关系未对照实机，截图核对**）。船体不在时（脚本 DeleteObject）部件在下一次 Update 里自己 `0x118A1B0` 删除。

**脚本消息的约定（EDF6，H）**：原生 SetEnemyGenerator / SetInstantEnemyGenerator（2100 / 2101，`0x20FF70` 内 `0x211E90` 起）向对象发消息 `0x20100`，数据块里 `+0x20` 是 SGO 路径宽串、`+0x40` 是整数 1、**`+0x48` 是单元索引**（`UfoMother511` 读的就是 `[data+0x48]`，并把同一条消息转发给第 N 个子单元；4.1 读 `+0x40`，见 1.4——EDF6 的数据块布局不同，舱门的处理要按 `+0x48`）。`UfoCarrier508` 收到 `0x20100` 交给 `[this+0xD60]` 的生成器组件（`0x6E1630`）。当前船体是克隆的 508，所以 `0x20100` 暂由它自己的生成器组件处理。

**验证用任务（读 21 个 MISSION.BVM）**：只有开场的「前一段」就创建并使用母舰的才适合不清波次：
- **M110**（NW_KOUSOUBLD01，`CreateNeutral` 母舰01 在初始化块里、无任何等待）：紧接着对索引 0..3 各发一条 `SetInstantEnemyGenerator`（Ufo301 ×8），0.25 s 后再 4 条（×10），之后还有 16 条；动作 3、7、4 在后面的计时函数里。生成器与舱门测试用它。
- **M130**（SK_SANGAKU02，`CreateEnemy` 母舰在初始化块里）：立刻 `ObjectAction` 0 与 14、`SetObjectDurability` 100；约 1–3 s 后 1、2、13、15 在一个计时函数里。动作表与耐久用它。
- M016 的母舰要等前面的波次（创建在 `Func_11` 链里），autopilot 过不去；M123 / M603 / M210 同理或更晚。

## 阶段 2 步骤 2（2026-10-14）：生成器、舱门开合、相机就绪、M130 的时序更正

**M130 更正**：上一节说动作 1、2、13、15 在 1 到 3 秒后到——错了。M130 的 `Func_3`（动作 1、2、13、15）由 `Func_13` 末尾的 `nat 9110`（带数字 40：按「剩余敌人数」触发的事件）启动，前面是 Func_8 → 9 → 10 → 11 → 13 依次靠 3 / 3 / 4 / 5 / 1 秒的计时放蚁群；要把蚁群杀到 40 只以下才轮到它。autopilot 不杀敌，所以只会看到创建时的动作 0 和 14。`SetObjectDurability(100.0)`（原生 3020）按「设成 100%」理解（4.1 规格 1.7 也是这个值），耐久本就是满的，所以日志里耐久不变是对的；没有证据它是绝对值（绝对值会把 5866 降到 100，没发生）。**（下午更正：3020 设的是最低耐久 = 100% × 最大耐久，这一段船体打不动；见 1.7 表与「补完」的实机记录。）**

**`0x20100` 数据块（EDF6，H）**：`+0x08` 种类（2101 为 1）、`+0x10` 数量、`+0x14` 间隔（秒 × 60 帧）、`+0x18` 浮点（第一个浮点参数 × 难度系数）、`+0x1C` 字节标志、`+0x20` `std::wstring` 敌人 SGO 路径、`+0x40` 整数 1、**`+0x48` 舱门索引**。`UfoCarrier508` 的生成器组件（`0x6E1630`）读 `+0x40`（恒为 1，所以克隆的 508 一直报「index 1」）。

**实现**：船体收到 `0x20100` 且舱门部件已启用（`Edf41MotherParts` 含 1）时自己接管：记一个任务，对应舱门播 CAS 状态 `open`（`SetState` = `0x116C2D0(对象+0x8F0, [对象+0xC80], 状态名, 0)`，构造函数里设 `default` 用的就是它），每 `间隔` 帧在舱门位置下方 12 m 处用 `0x11945E0` 造一只，最后一只后 90 帧播 `close`。位置是舱门**对象原点**（不是 `hatch` 骨骼矩阵）、环形散开，间隔 / 半径是按数据块字段的推测，**待实机核对**。船体先收到消息（创建当帧）后建部件，任务等部件建好再跑。

**相机**：游戏刚开始时相机是单位阵（眼睛 0 0 0、朝 +Z），不算就绪；`GameCamera` 现在跳过它，所以 `Edf41MotherPlace` 等到相机真有视角才摆。

## 阶段 2 步骤 3（2026-10-14）：动作 3–17、船体死亡带走部件、子弹探针；开火的前置逆向

**动作 3–17**：五个一组（主炮 3–7、中型炮 8–12、板甲 13–17）：+0 启用、+1 停用、+2 攻击开、+3 攻击关、+4 强化；只对耐久 > 0 的部件生效。标志记在部件记录里（`PartRec.enabled/attack/boost`）。可见效果：主炮启用播 CAS `open`、停用播 `close`；中型炮启用播 `appear`；板甲没有动画，只有标志。标志真正放行的开火要等子弹。日志 `EDF41MOTHER action N on ...: <组> <动作> -> k parts changed`。

**船体死亡**：船体 `destroyed` 字节第一次为 1 时，生成器任务清掉，部件每 3 帧删一个（板甲先）。这是占位：4.1 是让它们坠落爆炸（1.6，没读到底）。

**开火——已查明的和没查明的（EDF6，H 除注明）**
- 每颗子弹都走 `0x1194280(manager, matrix, 子弹工厂, info)`，与 `0x11945E0`（按 SGO 路径造）是兄弟：`info` 的 `+8/+0x10/+0x18/+0x20` 是同一套 `{矩阵指针, 0, 0, 输出}`，工厂槽 2 `Create(info)` 造子弹。子弹基类构造 `0x22E9C0` 读 `info+0x30`（矩阵拷贝）、`+0x38/+0x40`（发射者弱引用）、`+0x1A0`（整数）。`LaserBullet02` / `RocketBullet01` / `BeamBullet01` / `PlasmaBullet01` 的工厂创建分别是 `0x253D40` / `0x286B30` / `0x24BF70` / `0x25B580`（对象大小 0x1590 / 0x1350 / 0x1060 / 0x13F0）。`info` 的全部布局（子弹设定块：速度、寿命、伤害系数、大小、颜色）没读完；`UfoSmall507` 一族把它做成对象内嵌的成员（`+0x700`）并从 `bullet_info` 表或默认行 `0x1F76080` 拷贝设定。
- 所以先上探针（ini `Edf41MotherBulletProbe=1`）：在 38 个调用点上挂钩，记每种子弹类第 1、2 次的 `info` 前 0x1C0 字节和之后每 200 次的摘要。要看真实的要塞 / UFO 开火。
- **EDF6 的 E510 要塞炮台是 4.1 母舰部件的后继设计**（`MovingFortress_Battery / MediumCannon / LargeCannon`，`OBJECT/E510_FORTRESS_PARTS_*.SGO`）：`bullet_big_cannon = LaserBullet02 [0, [0, 20.0, 1, 140, 8, 60.0, 10, 0.0, 11, 0.0, 2, 50.0, 12, [4,4,4,1]], [2]]`（自定义参数 `[2]`、`[0]` 与 4.1 的 `mp_LaserCustomParameter [[0],[2]]` 同形）；`LargeCannon` 的 `attack [[1800, 4200]]` 与 4.1 炮的随机 1800–3599 帧蓄力同量级；`mf_laser_angle [30, -30]`；`bullet_medium_entity = EntityMissileBody/MissileBullet01` 是火箭。通用子弹对象 = `[工厂名, [标志, [公共参数键值对: 0 模式, 1 寿命帧?, 8 大小, 12 颜色…], 自定义参数]]`。这些炮台类由 `MovingFortress` 父对象创建后设 `child+0x5C8/+0x5D0`（父弱引用）、`child+0x5C4`（槽号），再 `0x118AF20(父, 子)` 登记；单独造出来没有父对象，不能直接当部件类用（待读）。

**子弹创建信息的构造（H，当天补）**：`0x1001D0(this)` 是 `BulletCreateInfo` 的构造（先设基类虚表 `0x1762068`——就是脚本造对象用的那个上下文，再换成派生虚表 `0x1769028`；`+0x30` 起是一块默认设定：`+0x30` 浮点 1.0、`+0x60` = -1、`+0x64` = 2.0、`+0x68` = 180、`+0x70/+0x74/+0x78` = 1.0、`+0x90` = 0.1、`+0x9C` = 0.5、`+0xC8` 变体空(0xFFFF)、`+0xD0` 子对象 `0x7B3200`；`+0x180..+0x1D8` 清零、`+0x1C0` 变体空）。`UfoSmall507` 把它放在对象 `+0x700`（构造里 `lea rcx,[rbx+0x10]; call 0x1001D0`，`rbx = this+0x6F0`，`+0x6F0` 本身放子弹工厂指针），之后 `0x52AE10` 把「设定行」（`bullet_info` 表行或默认行 `0x1F76080`，0x7A 字节）拷到 `info+0x100..0x179`，自定义参数节点放 `info+0xE8`，开火时 `0x1194280(manager, 矩阵, 工厂, info)`。要塞炮台 SGO 里的 `[标志, [键,值,…], 自定义参数]`（键 0 = 20.0 速度?、1 = 140 寿命帧?、2 = 50.0 伤害系数?、8 = 大小、12 = 颜色 …）就是喂给这个设定块的参数表；把键解释成 `info` 偏移要靠探针日志对照。

## 阶段 2 步骤 4 的前置（2026-10-14）：中型炮开火参数（4.1 逆向）与两个可直接复用的结论

**4.1 中型炮（Cannon）开火**（EDF41.exe，H）：
- 开火状态 `0x25a060` 等到第 `0x258`（600）帧后切回待机 `0x259f00`。
- 方向是 `cannon` 节点矩阵的轴乘 -1（`0xab6b58` = -1.0）。
- 生成子弹：`0xe6ed0(管理器, 矩阵, 工厂 [+0xEB0], 参数块 [+0xD80])`，生成前写 `[+0xDE0] = [+0x180]`（难度）。
- 参数块由构造 `0x258eb0` 填写（以下偏移都相对参数块起点 `+0xD80`）：

| 4.1 偏移 | 值 |
|---|---|
| +0x64 | 0.01 |
| +0x68 | 2700（int，寿命帧？） |
| +0x6C | **伤害 = s × 350 × 难度表项 × 第二表项**；`s = 构造参数 / 10`，即本仓 4.1 敌人通用的 `s = level / 10` |
| +0x70 | 80.0 |
| +0x7C | 80.0 |
| +0x90..+0x9C | 颜色 (1, 0.5, 0.25, 1) |
| +0xA8 | 自定义参数 = `mp_CannonCustomParameter [2, 40, 0, 0.0005]` 的节点 |

另外：`+0xEB0` = `mpc_CannonBulletFactory`（RocketBullet01），`+0xE30` 命中音 `母船UFO中型砲台着弾`。

**4.1 参数块与 EDF6 `InitParam`（`0x1001D0` 构造）对照的假设**：已知字段在 EDF6 里整体后移。
- 速度、寿命、伤害后移 0x30：4.1 `+0x64/+0x68/+0x6C` 对 EDF6 `+0x94/+0x98/+0x9C`。
- 大小、颜色、自定义参数后移 0x40：EDF6 `+0xC0/+0xD0/+0xE8`。
- 两个 80.0（4.1 `+0x70`、`+0x7C`）落在 EDF6 的哪个字段（爆炸半径？），要用 `Edf41MotherBulletProbe=1` 看一次 E510 要塞中型炮的真实 RocketBullet01 来定。**未核对，不要直接照搬。**

**巨龙分支里已解出、真机用过的两条**（`feat/edf41-objects-3`，PR #179）：
- **EDF6 子弹生成**：`InitParam` 用 `0x1001D0` 构造，填速度 `+0x94`、寿命 `+0x98`、伤害 `+0x9C`、大小 `+0xC0`、硬直 `+0xCC`、颜色 `+0xD0`、自定义参数 `+0xE8`（标签 `+0xF8` = 1）、种子 `+0x1A0`、队伍 `+0x90`（发射者 `+0x314`）、发射者弱引用 `+0x38`（`0x6CEBB0` 赋值）。然后调 `0x1194280(管理器 [EDF+0x20B2958], 矩阵, 工厂, 参数)`，最后 `0x100C90` 析构。中型炮开火可以直接照这个写。
- **CAS 播放器的时间是 `+0xE8`，不是 `+0x100`**：`+0x100` 是播放速度；`+0xF4` 是本帧待加的增量，CAS 更新 `0x1165B40` 加完就清零。本模块如果要按动画时间计时（舱门、主炮开合），读 `+0xE8`。

**开火怎么在真机上触发**：「攻击开」是各组的 +2：主炮 5、中型炮 10、板甲 15。
- 中型炮只有 M210（`3 5 8 10 13 15`）和 M603 用到。
- autopilot 还没跑过这两关的母舰段，下一步要先确认它们在开发开关下能进。

## 补完规格（2026-10-15，EDF41.exe 逆向；纠正了上文几处，见本节第 0 小节）

2026-10-15。接 `docs/edf41-ufomother301.md`（下称「原文」）第 5 节的待办。地址一律是 `EDF41.exe` 的 RVA（EDF6 的地址标 `EDF.dll`）。可信度：**(H)** 反汇编直接读到；**(M)** 由读到的代码推出；**(U)** 没读。
反汇编与脚本在本目录：`dd.py`（按控制流收整函数、标浮点 / 宽串 / 跳转表）、`scan.py`、`rtti.py`、`findcls.py`、`e6callers.py`、`cfgk.py`（4.1 SGO）、`cfg6.py`（EDF6 SGO）；转储 `big*.txt`、`panel*.txt`、`cannon*.txt`、`hatch*.txt`、`gen*.txt`、`hull_*.txt`、`pb_*.txt`、`b167/b168/b178.txt`（4.1 子弹基类）、`e6_*.txt`（EDF6 InitParam / 子弹核 / 键值解析）。

### 0. 先改原文的错（都 H）

| 原文说法 | 实际 |
|---|---|
| `+0x180` = 等级 / 难度 | **队伍（team）**。基类 0x28d700 用它在队伍注册表（0xcc8510）里找目标；开火时写进子弹参数块 `+0x60`（team）。等级是 `+0x258`（原始）/`+0x25c`（有效）。 |
| 船体 `+0xa24` 是发光量 | `+0xa00/+0xa04/+0xa08` = 俯仰/偏航/横滚角，`+0xa10` = 角速度，`+0xa20` = 目标角速度（每帧 `a10 += (a20−a10)·0.01`，`a00 += a10`），只有偏航 `a04` 进矩阵。所以 **`+0xa24` = 目标偏航角速度**：动作 2 / 构造末尾 = 0.0003 rad/帧（船缓慢自转），动作 0 = 0 且 `a14` 立即清零，动作 1 = 0（自转渐停），死亡 = 0。船体的发光是 `+0x9a0`（当前）/`+0x9b0`（目标）/`+0x9c0`（插值率）。 |
| 0x251bc0「仅当部件耐久 > 0」 | 判的是部件 `+0x360`，即**船体**的耐久 > 0；不看部件自己的耐久。所以船体死后动作 3–17 全部失效；死亡 functor 里「Cannon +0x900=0（仅耐久>0）」**实际是空操作**（船体已死）。 |
| `+0xd04` 来自 `mp_CannonCustomParameter` | 常量 **4**（0x258eb0 写死）：束每 4 帧一发。 |
| `+0x905` 触发部件坠落 | `+0x905` = 「船体耐久首次 ≤ 0」的边沿，只做发光闪白再熄灭。坠落标志是 **`+0x910`**，由部件自己的击毁状态置位。 |
| 0x258eb0 / 0x25cad0 是构造 | 是 **SetLevel(level)**（虚表槽 3 调它，ctor 末尾也用 10.0 调一次）。真正的构造：Cannon 0x258070、Panel 0x25c550、Hatch 0x25b470、BigCannon 0x255c30（这个是真构造，SetLevel 是 0x2566b0）。 |
| 中型炮开火「按耐久/600 缩放」 | 是**帧数/600**（充能进度），见 3.2。 |
| `mpc_FireBeamSetting` 的向量是方向 | 是**束口偏移**（在 `hatch_r/f/l/b` 骨骼局部空间里）；方向是瞄目标。 |
| `mp_PanelParameter` 是各组板甲起伏速率 (U) | **4.1 根本不读**：exe 里没有 `mp_PanelParameter`、`mp_PurgeSetting` 这两个字符串（`findstr.py` 全文搜过）。板甲转速是写死的 0.001。 |

### 1. 公共机制

### 1.1 状态机与计时 (H)
- 每个部件有 `StateMachineTable::Agent`（BigCannon/Panel/Cannon 主机在 `+0xb00`，Hatch 在 `+0xca8`，Cannon 的束机在 `+0xd40`）；状态记录 `{float time; int frames}`：主机在 `+0xad0/+0xad4`（Cannon 束机 `+0xd10/+0xd14`，Hatch `+0xc78/+0xc7c`，船体主机 `+0xb00/+0xb04`，船体序列器 `+0x838/+0x83c`）。`[agent+0x28]` 指向记录，`[记录+8]` = agent。
- 切状态（BigCannon 0x257d00、Cannon 0x25b110、Panel 0x25dc00、Hatch 0x25c1b0，同一模板）：旧状态 phase 2 → **记录整个 qword 清零（time=0, frames=0）** → 新状态 phase 0。
- 每帧顺序（各类 Update）：基类更新 → `[记录+8]->tick(phase 1, dt)` → `time += dt; frames++`。所以从另一状态的 phase 1 里切进来的新状态，第一次 phase 1 看到 `frames = 1`（构造时设的初始状态第一次看到 0）。下文「第 N 帧」都按这个计数。
- `dt` 是秒（time 与 2.0 / 6.0 / 10.0 / 30.0 / 60.0 比较）(M)；`frames` 按 60 FPS。
- 随机数：`x = x·0x5D588B656C078965 + 0x269EC3`，`r = ((x>>32)·10000)>>32 · 0.00010001` ∈ [0,1]。部件状态 `+0x210`（位置哈希做种，原文 1.3）；全局 0xc64a08 用于特效散布。
- 随机方向 0x51b7c0(out, axis, minAng, maxAng, rng)：与 `axis` 夹角在 `[minAng,maxAng]` 均匀、方位角 [0,2π) 均匀的单位向量 (H)。

### 1.2 音效 / 特效函数 (H)
| 4.1 | 作用 |
|---|---|
| 0x3e8150(sndmgr 0xcc8508, &desc, cue名) | 预解析 cue 到 0x40 字节描述（`+0x10` cue id，-1=无） |
| 0x3e3f20(…) | 在一点播一次描述里的 cue |
| 0x3e85c0(&desc, &pos, &handle) | 播并返回句柄（可跟随 / 可停）；0x1675c0(&handle) 停 |
| 0x3e83f0(sndmgr, &pos, cue名, &handle 或 0) | 按名字直接播 |
| 0x1b16b0(fxmgr 0xcc8500, pos, dir, size) | 火焰爆发（`火炎_01.dds`） |
| 0x1b1ba0(fxmgr, pos, dir, size) | 烟+火团（`煙_01`+`火炎_01`） |
| 0x1b2070(fxmgr, pos, dir, size, 1.0) | 定向爆炸（火+烟） |
| 0x1b2ab0(fxmgr, pos, dir, size, k) | 大爆炸（多个 0x1b1ba0 + 放射，内部常量 100/300/600） |
| 0x337a60(ragdoll +0x7a0, out) | 在部件物理壳包围盒里取一点（M） |

**音效限流**（H）：船体 `+0xbb8` 是 6 个 int 的数组（`+0xbc8` = 6），每帧各自减到 0。部件播某些音之前：`old = slot; slot = 16; if old == 0 → 播`。即每个槽**全船每 16 帧最多一次**：槽 0 板甲起動、1 板甲レーザー、2 板甲キャノン、3 中型炮チャージ、4 中型炮発射、5 中型炮ビーム。

### 1.3 难度 / 等级 (H)
- 难度表：`DEFAULTPACKAGE/CONFIG.SGO` 的 `ModeList[mode][6]`（全局 `[0xcc84a0]+0xc0` 指向当前 mode 的项），5 行 = 5 个难度，`[0xcc84a0]+0x44` = 难度下标 0..4，`[0xcc84a0]+0xa804` = 1..4 的第二下标（多半是联机人数，M）。每行 `R = [[lvMin,lvMax],[p1,p2,p3,p4],[…3],[h,d,…],…]`。
  - 单机全部 `R[1]=[1,1,1,1]`、`R[3]=[1,1,1]`；联机 `R[1]` 如 `[1.2,0.8,1.0,1.2]`、`R[3]` 如 `[1.8,1.5,1.0]`/`[2.0,2.4,1.0]`。
- 有效等级（0x28eab0 → 0xd98e0）：`L = L_raw × game_object_level_adjust(缺省1) × lerp(lvMin, lvMax, 任务序号/(任务数−1))`，写 `+0x25c`。船体 0x253a10 把自己的 `+0x258` 发给每个子对象（0x28eab0），所以**部件与船体同一原始等级**，各自算有效等级，再调自己的槽 3（SetLevel）。`s = L/10`。
- 公式：**HP = s × game_object_durability × R[3][0] × R[1][p−1]**；**伤害 = s × 基数 × R[3][1] × R[1][p−1]**（单机两个表项都是 1）。

### 1.4 子对象屏蔽：船体 0x250520（每帧，H）
`T = 船体 +0x180`。若 `T == −1 或 3`：子对象队伍 = 1、`可被锁定` = 否；否则子对象队伍 = T、`可被锁定` = 是。对 `0xa40` 里每个仍存在的子对象：
- `+0x900 == 0` → 不可锁定，且 `+0x1e8 bit0 = 1`；
- `+0x906 && T == 3` → 同上（只有 BigCannon 构造里 `+0x906 = 1`：中立船的主炮无敌）；
- `+0x904`（静态板甲）→ 不可锁定（但 bit0 不受影响）；
- 0x28ec10(child, team, 可锁定)：写子对象 `+0x180`，在队伍注册表里登记/注销（`+0x1e8 bit6`）。
- **`+0x1e8 bit0` = 免伤**：基类伤害 0x28e1f0 遇到 bit0 时只接受带强制标志（伤害信息 `+0x48` 的 0x40）的伤害 (H)。所以**没启用（+0x900=0）的部件打不动**；舱门 / 舱盖永远没人给 +0x900，**永远无敌**（Hatch 槽 11 也是空函数）。

### 1.5 PartsBase 每帧矩阵 0x25e840（虚表槽 1，H）
- 行向量：`M = L2 × A × L1 × waist骨骼世界矩阵`，`L2` = `+0xa90..+0xac0`（3×3 + 平移行 `+0xac0`），`A` = `+0x320`（挂接时由欧拉 + 位置算好），`L1` = `+0xa50..+0xa80`。结果写 `+0x8c0..+0x8f0` 再拷进 `+0x60..+0x90`。同时 `+0x920 = 新位置 − 旧位置`（若 `+0x930` 有骨骼指针则用其平移）。
- `+0x905`：船体 `+0x16c ≤ 0` 的第一帧置 1，发光 `+0xa10..18 = 100`、目标 `+0xa20..28 = 0`、`+0xa30 = 0.02`（闪白后熄灭）。
- `+0x910`（坠落）：不再跟船；每帧 `pos += v(+0x920)`，`v.y −= 0.002`（单位/帧²），朝向冻结。`v` 初值 = 脱离前一帧的位移（继承船的速度与部件自转）。不碰地、不删除，删除由部件自己的状态计时做。
- 发光：部件 `+0xa10`（当前色）→ `+0xa20`（目标），每帧按 `+0xa30` 插值（0x25e680），`+0xa04 += 1/600`；喂材质动画 `light0_color` / `scroll_light0`（0x25e5e0）。
- 掉落物：基类伤害 0x28e1f0 在耐久首次 ≤ 0 时（`+0x1e8 |= 0x10`），主机且 `+0x1e8 bit5` 时按 `game_object_drop_item [_, a, b]` 调 0x1ae060(物品管理 0xcc84c0, 位置, a, b) (H)。Cannon `[0,0,2.29]`、BigCannon `[0,0,7.29]`、Panel `[0,0,0.29]`。
- 删除：0x56bbc0 DestroyObject 递归删除 `+0x48` 子列表；挂接 0x25e480 把部件塞进船体 `+0x40` 列表，所以**船体被删时所有尚存部件一起删** (H)。

### 2. BigCannon（主炮）

构造 0x255c30，SetLevel 0x2566b0，Update 0x256760，OnDamage 0x256880，状态表 0x256380。耐久 2400。

### 2.1 字段与参数块 (H)
- `+0x906 = 1`；初始状态 0x256950；`+0xe50` = CAS 播放器 `main` 下标；`+0xc60` = `mp_LaserCustomParameter` 节点；`+0xc64` = 本次扫射的随机角 φ。
- 音效描述：`+0xcb0` 砲台チャージ、`+0xcf0` チャージ完了、`+0xd30` 砲台発射、`+0xd70` 砲台破壊、`+0xdb0` 砲台爆発、`+0xdf0` 砲台ダメージ；`+0xe38` 充能循环音句柄，`+0xe40` 其跟随结构；`+0xe30` 受击音冷却。
- **参数块 P = `+0xb30`**（`BulletBase::InitParam`，vtable 0xa91fc8）：

| P+ | 字段 | 值 |
|---|---|---|
| 0x38 | 发射者弱引用 | 自己 |
| 0x60 | team | 开火时 = `+0x180` |
| 0x64 | 速度 | 20.0 |
| 0x68 | 寿命 | 60 帧 |
| 0x6c | 伤害 | **5000（常数，不随等级 / 难度）** |
| 0x70 | 爆炸半径 | 每 8 帧那一发 100，其余 0 |
| 0x7c | 大小 | 50.0 |
| 0x90 | 颜色 | (1,1,1,1) |
| 0xa8 | 自定义参数 | 每 8 帧那一发 `[2]`，其余 `[0]`（`mp_LaserCustomParameter[1]`/`[0]`） |
| 0xb0 | 命中音 | 每 8 帧那一发 `母船UFO砲台命中`，其余清空 |

子弹直接用 0x1cf960 构造 **LaserBullet02**（不查工厂，`new 0xbc0` + 0x178a30）。LaserBullet02 自定义 `[mode]`：mode 2 → `+0x1e8 |= 8`、`+0x3b0 = 3`（M：穿透/不受地形挡）；mode 1 改粒子参数。EDF6 的 E510 主炮正是同一套：`mf_LaserCustomParameter [[0, 0.0, [0]], [0, 100.0, [2]]]`（爆炸半径 0 配 `[0]`、100 配 `[2]`）。
- HP = s × 2400 × 表（1.3）。

### 2.2 每帧（0x256760，H）
基类 → `+0xe30--` → **自转**：`+0xe54 −= 0.0003`，`L2 = RotY(e54)`（`a90=cos, a98=−sin, ab0=sin, ab8=cos, aa4=1`，只转自身轴）→ tick → time/frames → 充能音跟随位置。

### 2.3 状态图（H）
1. **0x256950 关闭 `close_state`**：进入：CAS `close_state`，速度 1。每帧：`+0x900` → 2。
2. **0x256a90 打开 `open`**：进入：CAS `open`。每帧：动画播完（`player+0x114 == −1` 或 `time+step ≥ length`）→ 3。退出：发光目标 (1,1,1,1)、当前 (10,10,10,1)。
3. **0x256c20 待命**：进入：网络同步 0x257810。每帧：`!+0x900` → 7（`close`）；`(+0x901 && !+0x904)` → 4；否则 `(+0x902 && !+0x904)` → 4；**每帧把 `+0x902` 清零**（`+0x902` = 一次性开火触发，动作 7）。
4. **0x256ce0 充能**：每帧 f = frames：
   - `prog = max(0, (f−720)/480)`；发光 `a10..a18 = max(当前, prog·10 + 2)`；
   - f == 720：起充能循环音 `砲台チャージ`（句柄 `+0xe38`，跟随）；f == 1045：`チャージ完了`；f == 1180：停循环音；
   - `!+0x900` → 7；f ≥ 1200 → 5。退出时停循环音。
   即开火前 20 秒充能（前 12 秒无声无光，后 8 秒亮起）。
5. **0x256f60 发射**：进入：同步；`φ = rand − 0.5`（rad，±28.6°）；播 `砲台発射`。每帧：发光 (50,50,100,1)；主机（`!(+0xc8 & 1)`）每帧 0x2570c0(f) 发一发 + 0x257a10 同步；f > 60 → 3（若 `+0x901` 仍开，马上再充能 20 秒）。
   - **方向**（0x2570c0）：`θ = (f/30 − 1)·1.5`，局部方向 `d = (sin θ, −cos θ·cos φ, −cos θ·sin φ)`，即在绕 X 轴倾斜 φ 的平面里从 −X 扫到 +X（±86°），f=30 时正下方。用 0x51b570 由 d 建矩阵，**平移 = 局部 (0, −790, 0)**，再乘部件世界矩阵 `+0x60`。每帧一发，共 61 发（f=1..61），f % 8 == 0 的那几发（8,16,…,56，共 7 发）是带爆炸 100 + `[2]` + 命中音的「重」发。
6. **0x2574f0 击毁**（OnDamage 首次 HP≤0，`+0xe58` 防重入）：进入：发光闪 (100,100,100,1) → 0（率 0.1）；播 `砲台破壊`。每帧：f < 180 且 f % 8 == 0：物理壳内随机点放烟火团 0x1b1ba0 size 0.25；f > 300 → 8。
7. **0x257380 关闭 `close`**：进入：CAS `close`。动画完 → 1。
8. **0x257630 坠落**：进入：按名播 `母船UFO砲台墜落`；`+0x910 = 1`（脱离坠落）。每帧：f == 360：`砲台爆発` + 大爆炸 0x1b2ab0 于 (x, y−400, z)，方向绕 (0,−1,0) 0..0.7π 随机，size 3，k 1；f == 380：y−500 同上（无音）；f == 400：y−600；f > 420 → DestroyObject。
   整段：死后 300 帧原地冒烟，再坠落 420 帧后删除（约 12 秒）。
- **OnDamage（0x256880）**：HP > 0 时 `+0xe30 ≤ 0` → `+0xe30 = 60`，在命中点（伤害信息 `+0x58`）播 `砲台ダメージ`。

`+0x902`（动作 7）只在状态 3 读，读完清零；Panel / Cannon 不读 `+0x902`（全文扫过，H）。

### 3. Cannon（中型炮）

构造 0x258070，SetLevel 0x258eb0，Update 0x2599d0，OnDamage 0x259c00。耐久 600。两台状态机：主机 `+0xb00`（火箭），束机 `+0xd40`（只在自身 HP>0 时 tick）。

### 3.1 字段（H）
- `+0x11c0` = `mpc_FireCannonSetting` 骨骼 `cannon`，`+0xec0` = 炮口局部偏移 (0,−360,0,1)；Update 每帧算 `+0xf80` = 炮口世界点、`+0xf90` = −(`cannon` 骨骼 Y 轴)；`+0xed0` = 充能光点粒子（`Particle3_k.dds`，色 (1,0.5,0.25,1)），`+0xf70` 开关，`+0xfe4/+0xfe8` 大小。
- `+0xc80 + 0x20k`（k=0..3）= 束口 {偏移 xyz,1}，`+0xc90 + 0x20k` = 骨骼（`hatch_r/f/l/b` 的顺序同 SGO）；`+0xd08` = 下一个束口下标；`+0xd04 = 4`；`+0xd00` = 束散布角 `max(0.005, 0.2 − 0.016·s)` rad；`+0xd70` = 「本轮已开打」标志。
- `+0xb30` = `mpc_ExplosionJoint` 骨骼；`+0x11c8` = 下次开炮前等待帧数（SetLevel 置 0）；`+0x1390` 受击音冷却。
- 音效：`+0x1210` 中型砲台チャージ、`+0x1250` 発射、`+0x1290` ビーム、`+0x12d0` 破壊、`+0x1310` 爆発、`+0x1350` ダメージ。
- **火箭参数块 P = `+0xd80`**，工厂 `+0xeb0` = `mpc_CannonBulletFactory`（RocketBullet01）：P+0x64 速度 0.01、+0x68 寿命 2700、+0x6c 伤害 **350·s·表**、+0x70 爆炸半径 80、+0x7c 大小 80、+0x90 色 (1,0.5,0.25,1)、+0xa8 = `mp_CannonCustomParameter [2,40,0,0.0005]`、+0xb0 命中音 `中型砲台着弾`、+0x60 team。
  - RocketBullet01 自定义 `[mode, a, b, accel]`（0x18f030，H）：mode 2 → `+0x1e8 |= 8`；mode ≠ 1 时沿初速方向加加速度 `accel·3600`（单位/秒²）；a、b 两个整数缺省 40、120（尾迹粒子参数，M）；碰撞半径 = `max(0.01, 大小·0.25)` = 20，显示 0.5·大小。即初速 0.6 m/s、加速度 1.8 m/s² 的「慢启动重型火箭」，45 秒寿命。
- **束参数块 P = `+0xb40`**，工厂 `+0xc70` = `mpc_BeamBulletFactory`（BeamBullet01）：速度 `min(20, 5 + 0.1·s)`、寿命 180、伤害 **0.5·s·表**、大小 `max(2, 4 − 0.2·s)`、色 `(0.1c, 1.2c, 0.1c, 1)`，`c = min(1, 1+0.1s)` = 1（绿）、+0xa8 = `mpc_BeamBulletCustomParam [0,[1,1,1,1],[1,1,1,1],1,1,2,16]`、命中音 `中型砲台ビーム着弾`、team。
- HP = s × 600 × 表。

### 3.2 主机（火箭，H）
1. **0x259cd0 休眠**（初始）：每帧 `+0x900` → 2。
2. **0x259d20 出现**：进入：CAS `appear`。动画完 → 3。退出：发光目标 (5,5,5,1)，当前 `max(当前, 10)`。（之后再无收回动画：Cannon 不响应 `+0x900` 清零。）
3. **0x259f00 待命**：进入：同步。每帧：`+0x901 && !+0x904` → `+0xd70 = 1`，→ 4。
4. **0x259f90 等待**：每帧：`+0x11c8 < f` → 5。退出：`+0x11c8 = 1800 + rand(1800)`（1800..3599）。**第一次 `+0x11c8 = 0`，所以开打后立即进入充能**；之后每轮间隔 1800–3599 帧。**本状态与 5 都不再检查 `+0x901`**：清攻击标志只在本轮打完回到 3 时生效（船体死后也可能再打一发，忠实移植保留）。
5. **0x25a060 开炮**：进入：`+0xf70 = 1`（充能光点开），大小清零。每帧：`t = f/600`；发光 `max(当前, 10t)`；光点大小 `40t`；f == 450：限流槽 3 → `中型砲台チャージ`（句柄跟随）；f ≥ 600：→ 3，然后**沿 `cannon` 骨骼 −Y 方向**、从 `cannon` 骨骼 × (0,−360,0) 处生成火箭（0xe6ed0），限流槽 4 → `中型砲台発射`。**火箭不瞄准**，方向就是炮管方向（欧拉 `[-61.5, 27+36k, 0]` 斜向外下）。退出：`+0xf70 = 0`。
6. **0x25a2d0 击毁**（OnDamage 首次 HP≤0，`+0xb38` 防重入）：进入：发光闪白再熄（0.1），`中型砲台破壊`。每帧：f < 180、f % 8 == 0 → 物理壳随机点 0x1b1ba0 size 0.25；f > 300 → 7。
7. **0x25a410 坠落**：进入：按名播 `母船UFO中型砲台墜落`；`+0x910 = 1`。每帧：f ≥ 288 且 (f−288) % 4 == 0、k = (f−288)/4 < 8：k 为偶数时播 `中型砲台爆発`；在 `mpc_ExplosionJoint` 骨骼位置 − 骨骼Y×(100 + 30k) 处（即沿炮管向外 100,130,…,310）放定向爆炸 0x1b2070 size 10，方向绕 (0,−1,0) 0..0.7π 随机；f > 320 → DestroyObject。共 8 次爆炸（288..316 帧）。
- OnDamage（0x259c00）：HP > 0 且 `+0x1390 ≤ 0` → 60 帧冷却，命中点播 `中型砲台ダメージ`。

### 3.3 束机（H）
- **0x25a5c0 待命**：进入：`+0xd70 = 0`，同步 0x25aa10。每帧：`+0x901 && !+0x904 && +0xd70` → 齐射。
- **0x25a650 齐射**：每帧：f % 4 == 0：限流槽 5 → `中型砲台ビーム`；主机调 0x259710 + 同步 0x25ac10。`!+0x901 || +0x904` → 待命（待命入口清 `+0xd70`，要等主机下一次回到待命且攻击开才再置 1）。
- **0x259710 一发**：需要有目标（基类 `+0x2a8/+0x2b0`，每 60 帧按队伍找一次最近的敌对目标）。起点 = 束口 k 的偏移 × 对应 hatch 骨骼世界矩阵；目标点 = 目标 `+0x90`（若目标 `+0x1e0` 非空则取其挂点 `[[+0x1d0]+8]+0x10`）；方向 = 目标点 − 起点，再在 `[0, +0xd00]` 圆锥内随机；0xe6ed0 生成 BeamBullet01。之后 `k = (k+1) % 4`（无目标也轮转）。
  → **攻击开着就持续每 4 帧一束（15 发/秒），四个舱口轮流，瞄目标。**

### 4. Panel（板甲）

构造 0x25c550，SetLevel 0x25cad0，Update 0x25cff0，OnDamage 0x25d1c0。耐久 120。

### 4.1 字段与参数（H）
- `+0xb30` 已起动；`+0xb34` 环转角；`+0xb38 = 0.001`（转速，常数）；`+0xb3c` 0→1 的起转系数；`+0xb40 = 3 + rand·4` 秒（击毁后爆炸延时）；`+0xb44` 击毁后帧数；`+0xb4c = mp_type`；`+0xb50..+0xb5c` = (0, 0, `+0xb58`, 1)，`+0xb58` 0→100 外推距离；`+0xb60` 击毁标志；`+0xcb0` 散布角；`+0xcb4` 激光间隔。
- 音效：`+0xcd0` パネル起動、`+0xd10` パネルレーザー、`+0xd50` パネルキャノン、`+0xd90` パネル破壊、`+0xdd0` パネル爆発；参数块 `+0xc30` 命中音（仅等离子：`パネルキャノン命中`）。
- **参数块 P = `+0xb80`**，工厂 `+0xb70` = `mp_BulletFactory`，自定义 `+0xc28` = `mp_BulletCustomParameter`：

| | mp_type 0（Panel，LaserBullet02 `[0]`） | mp_type 1（Panel02，PlasmaBullet01 `[1,16]`） |
|---|---|---|
| 间隔 | `+0xcb4 = max(30, int(120 − 5s))` 帧 | **300 帧**（状态里写死；`+0xcb4 = 120` 不用） |
| 散布 `+0xcb0` | `max(0.005, 0.1 − 0.008s)` rad | 0.01 rad |
| P+0x64 速度 | 20 | `min(2, 0.5 + 0.075(s − 0.1))` |
| P+0x68 寿命 | 60 | 1200 |
| P+0x6c 伤害 | **2·s·表** | **25·s·表** |
| P+0x70 爆炸半径 | 0 | `min(15, 10 + 0.05(s − 10))` |
| P+0x7c 大小 | `max(0.5, 2 − 0.125s)` | 5 |
| P+0x90 色 | (2, 0.1, 0.2, 1) | (3, 1, 20, 1) |
- PlasmaBullet01 `[1,16]`：碰撞半径 = 大小 × 0.1 × P+0x80（0x18bfb0）；1 = 模式、16 = 粒子数 (M)。

### 4.2 每帧（0x25cff0，H）
- `b34 −= b38·b3c`（客户端另做平滑）；`L1 = RotY(b34)`；`L2` 的平移行 `+0xac0 = +0xb50`。按 1.5 的矩阵顺序：**板甲先沿自身局部 Z 外推 `b58`，再整体绕船体 waist 的 Y 轴转 `b34`**——起动后整圈板甲向外张开 100 并缓慢环转（最高 0.001 rad/帧 ≈ 3.4°/s，方向为负），全部板甲同向同速。
- 船体活着才继续：起动后 `b58 += (100 − b58)·0.02`，`b3c += (1 − b3c)·0.001`（时间常数约 1000 帧）；CAS 更新。

### 4.3 状态（H）
1. **0x25d360 休眠**（初始）：每帧 `+0x900` → 2。
2. **0x25d400 起动**：进入：发光 (10,10,10,10)。每帧（t = time 秒）：t > 2 且首次：限流槽 0 → 在**船体位置**播 `パネル起動`；t > 2：`+0xb30 = 1`（开始外推 / 环转）；t > 6 且 `+0x901 && !+0x904` → 激光 3 或等离子 4。**此后再不回休眠**（动作 14 清 `+0x900` 只会让它重新免伤、不可锁定）。
3. **0x25d510 激光**：每帧：`+0x901 && !+0x904`：f % `+0xcb4` == 0 → 限流槽 1 → `パネルレーザー`，开一发（0x25d220）；否则 → 2。
4. **0x25d620 等离子**：同上，f % 300 == 0，限流槽 2 → `パネルキャノン`。
5. **0x25d220 开火**：要目标（同 3.3）；方向 = 目标点 − **板甲自身位置**，`[0, +0xcb0]` 圆锥随机；矩阵平移 = 板甲位置；0xe6ed0。**瞄的是基类目标（最近的敌对目标），不是正下方。**
6. **0x25d740 击毁**（OnDamage 首次 HP≤0，`+0xb60`）：进入：发光闪白再熄，`パネル破壊`，定向爆炸 0x1b2070 size 5（方向全球面随机），**`+0x910 = 1` 立即脱离坠落**。每帧：t < 2 且 `b44 % 8 == 0`：物理壳随机点火焰爆发 0x1b16b0 size 5；`b44++`；t > `+0xb40`（3–7 秒）：定向爆炸 size 10 + `パネル爆発` → DestroyObject。

### 5. Hatch（舱门 / 舱盖）与生成器

构造 0x25b470，初始化 0x25b810，Update 0x25ba30，消息 0x25ba00，状态表 0x25b650。

### 5.1 结构（H）
- `+0xad0` 是一个完整的 4.1 **ObjectGenerator**（构造 0x330c40，加规格 0x330e70，每帧 0x331270，单只生成 0x3313c0）。舱门收到 `0x20100`（船体按 `[data+0x40]` 转发给 HatchLow[索引]）就是 0x330e70(gen, data)。
- 生成矩阵：每帧（船体与舱门都活着时）由 MAB 定位点 **`発生位置`**（`+0xcd8`，0x322a90 按名取）求世界矩阵交给 0x331270。不是 `hatch` 骨骼。
- `gen+0xb8 = 舱门自己`（取队伍）、`gen+0xc0 = 1`（**需许可**）、`gen+0xf0/+0xf4 = 0`（散布半径 0）。`+0xcf4 = +0xcf0 = 240`（关门前停留帧）；`+0xcec` = CAS `open` 片段长度（取整）；`+0x1e8 bit0` 构造即置位（无敌）。

### 5.2 生成器语义（0x330e70 / 0x331270 / 0x3313c0，H）
4.1 数据块：`+0` 种类、`+4` 波间延时、`+8` 数量、`+0xc` 间隔（帧）、`+0x10` 浮点、`+0x14` 字节、`+0x18` wstring SGO、`+0x38` 整数、`+0x40` 舱门索引。**EDF6 的 0x20100 块 = 4.1 偏移 + 8**（EDF6 `+0x08` 种类 … `+0x48` 索引，与原文阶段 2 的结论一致）。
- 种类 0（SetEnemyGenerator 2100，常驻）：规格存 `gen+0x20`，一波打完由 0x331200 重装（`+0x6c` = 波间延时）；种类 1（2101，一次性）：直接成为当前波 `gen+0x68..`。SGO 里的 `SummonEffect` 两个浮点存 `+0x60/+0x64`（或 `+0xa8/+0xac`）。
- 每帧：主机才跑；有当前波或常驻规格时：先倒数 `+0x6c`（波间延时）、再倒数 `+0xb0`（只间隔）；**`gen+0xc0 && !gen+0xc1` 时停住不生成**；否则数量 `+0x70` 减一并生成一只，`+0xb0 = 间隔`；数量到 0：`gen+0xc2 = 1`（完成）、`gen+0xc1 = 0`（收回许可）、重装常驻规格。
- 单只位置：生成矩阵平移 + (sinθ·r, 0, cosθ·r)，θ∈[0,2π)，r∈[`+0xf0`,`+0xf4`] = 0 → **正好在 `発生位置`**；朝向随机偏航 [0,2π)；不给初速（敌人自己飞）。

### 5.3 舱门状态（H）
1. **0x25bb00 关闭**（初始，CAS `close_state`）：每帧：舱门有队伍（`+0x180 ≠ −1`，船体每帧给）且（有当前波或常驻规格）且 `gen+0x6c ≤ +0xcec`（离开波 ≤ 开门动画长）→ 2。
2. **0x25bba0 开门**：CAS `open`；动画完 → 3。
3. **0x25bd30 开着**：进入：**`gen+0xc1 = 1`（许可生成）**，`+0xcf0 = 240`。每帧：`+0xc70`（Update 在 `gen+0xc2` 的那帧置 1）后每帧 `cf0--`，减到 0 → `+0xc70 = 0` → 4。
4. **0x25bde0 关门**：CAS `close`；动画完 → 1。
- 所以：**来了生成请求就提前开门，门全开才开始按间隔一只只生成，最后一只后停 240 帧关门**；常驻生成器下一波临近时再开。没有任何动作或 `+0x900` 参与舱门开合；舱盖（HatchMiddle）同类但从不收到 0x20100，永远关着；两者都无敌。

### 6. 船体

Update 0x2512c0，OnDamage 0x251170，命中判定（槽 10）0x250fc0，SetLevel 0x253a10 → 0x250620，路线槽 4 0x253a90。

### 6.1 移动 / 自转（H）
- 槽 4：有路线（`+0x270` 且 `+0x290`）→ `+0x9d0` = 路线点、`+0xa30 = 1`，否则 `+0xa30 = 0`。
- 活着：`+0xa30 == 0` 时速度 `+0x9e0 *= 0.99`；目标速度 `+0x9f0 = +0x9d0 − 位置`，长度截到 `max(0, +0x278·0.5)`（0xe5a60）；`+0xa31 == 0` 时 `v += (目标 − v)·0.001`。死后（`+0xa31 == 0`）`v *= 0.99`。`+0xa31`（坠落）时只 `v.y −= 0.0003`。`pos += v`。
- 角度：`a10 += (a20 − a10)·0.01; a00 += a10`，偏航 `a04` 填矩阵（见第 0 节）。构造末尾排的 functor 0x251ef0 = 动作 2：**船一出生就以 0.0003 rad/帧（≈1°/s）缓慢自转、发光渐红**。

### 6.2 受伤（H）
- **槽 10（0x250fc0）**：在伤害信息的接触点列表里找离命中点最近的物理体；**若 BigCannon 列表第一项的对象还存在 → 拒绝**；否则仅当该物理体 == `weak` 体（`+0xba0`）才接受。基类遇拒绝只接受强制伤害（0x40）。
  → **船体只能打弱点，而且要等主炮对象被删掉（主炮死后约 720 帧坠落完）才能打**。部件的伤害不转给船体（各算各的耐久）。
- OnDamage（HP > 0）：`+0xb38 = 1`；0x253750：16 个烟槽的尺寸 = `2·HP/HPmax`、另一参数 = `1 − 0.65·HP/HPmax`（M）；`+0xb70 == 0` 时：0x2544b0 同步、0x251de0：`+0xb70 = 64`、命中点播受损音（`+0xc70`）、发光闪 (10,10,10,1)。
- 每帧 `+0xb70 ≠ 0`：`b70 & 7 == 0` 时在 `weak` 节点空间里取点 `mp_Weakpoint` 偏移 (0,−40,0) + (r·sinα, 0, r·cosα)（α∈[0,2π)，r = rand·80），方向 = −节点Y，火焰爆发 0x1b16b0 size 20；`b70--`。即**一次受击 = 64 帧里 8 次随机小爆炸，期间新受击不重复触发**。
- HP = s × 4000 × 表。

### 6.3 主状态机（agent `+0xb10`，只在活着时 tick，H）
1. **0x252700 合着**：每帧：BigCannon 存在且其 `+0x900` → 2。
2. **0x2527d0 张开**：主炮存在 → CAS `open`，否则 `show`；动画完 → 3。
3. **0x252a30 张开中**：进入：`+0xb38 = 0`、`+0xb3c = 540`；16 个烟槽**开**；播 `+0xc30` 的音；联机同步。每帧：主炮存在：其 `+0x900 == 0` → 5（`close`）。主炮已不在：主机且 `+0xb3c == 0` → 4。（`+0xb3c` 只在 `+0xb38`（受过伤）时每帧减 1。）
4. **0x252c00 收起**：CAS `hide`，烟槽关。time > 30 s → 2（`show`）。
5. **0x252dd0 合上**：CAS `close`，烟槽关；动画完 → 1。
→ 主炮没了以后弱点露出（`show`），一挨打就开始 540 帧倒数，到点收起 30 秒，再露出，循环。

### 6.4 死亡编排（序列器，H）
OnDamage 首次 HP ≤ 0（`+0x8b8`）：发光 `9a0 = (100,100,100,1)` → `9b0 = 0`；排 A。
- **A 0x251f30**：进入：大爆炸 0x1b2ab0 于 (x, y−350, z)、方向 (0,−1,0)、size 2、k 1；按名播 `母船UFO本体墜落前爆発`；烟槽全关；`a24 = 0`；`+0x8bc = 0`；Cannon 组、Panel 组 `+0x901 = 0`（`+0x900` 那步是空操作，见第 0 节）。BigCannon 不处理。每帧：`8bc & 63 == 0`：位置 + 随机方向（全球面）×350 处大爆炸 size 2、k 0.6；序列时间 > 10 s → B。
- **B 0x2522c0**：进入：`+0xa31 = 1`（自由落体）；停飞行音（`+0xc10`）；按名播 `母船UFO本体墜落`（循环、跟随，句柄 `+0xca0`）；建一个半径 300 的 Havok 探测体（0x5e5060/0x607430，存 `+0x8c0`）；`8bc = 0`。每帧：探测体移到船位；`8bc & 63 == 0` → `weak` 节点 − 20·Y 处烟火团 0x1b1ba0 size 1.5（方向绕 −Y 0..1 rad）；**探测体碰到东西（地面）或时间 > 60 s**：再按名播一次 `母船UFO本体墜落`，(x, y−200, z) 处连放 3 次大爆炸 size 6、方向 (0,1,0)、k 1 → C。退出：停循环音、移除探测体。
- **C 0x2526e0**：时间 > 2 s → DestroyObject（连带所有部件）。
- 其间部件：所有部件闪白熄灭（`+0x905`）；板甲 / 主炮继续自转；中型炮若在等待 / 开炮态还会打完这一轮。

### 7. 子弹参数块：4.1 `BulletBase::InitParam` ↔ EDF6 `InitParam`（`EDF.dll` 0x1001D0）

依据（H）：4.1 默认值 0x168150 与 EDF6 0x1001D0 的默认值逐项对齐；4.1 子弹核 0x168b90 与 EDF6 0x231cc0 的用法对齐（速度 × 前向轴、`+爆炸半径 > 1e−6` 置爆炸位、`≥ 3` 置大爆炸位、大小 × 系数 = 碰撞半径、重力 × 全局重力向量）；EDF6 武器键值解析 0x6cede0（键 → 偏移）与 UfoSmall507 的 `bullet_info`（0x52ae10）。两边块都从 `+0x30` 起拷进子弹（4.1 拷到 core+0x210，EDF6 拷到 core+0x9a0）。

| 含义 | 4.1 | EDF6 | EDF6 键值（0x6cede0） | 备注 |
|---|---|---|---|---|
| 生成信息 {矩阵*, 0, 0, 输出*} | +0x08..+0x20 | 同 | | H |
| 伤害倍率（缺省 1） | +0x30 | +0x30 | | H |
| 发射者弱引用 | +0x38/+0x40 | +0x38/+0x40 | | H |
| 附加速度 vec3 | +0x40..+0x48 | +0x50..+0x58 | | H |
| team（缺省 −1） | +0x60 | **+0x90** | | H |
| 速度（单位/帧，缺省 2） | +0x64 | **+0x94** | 0 | H |
| 寿命（帧，缺省 180） | +0x68 | **+0x98** | 1 | H |
| 伤害 | +0x6c | **+0x9C** | （武器伤害） | H |
| 爆炸半径（0 = 不爆） | +0x70 | **+0xB0** | 2 | H（E510 键 2 = 50） |
| 字节标志 | +0x74 / +0x75（→ 0x20 位） / +0x78 | +0xB4 / +0xB6（→ 0x20 位） / +0xB5,+0xB7,+0xBA,+0xBB | 3–7 | M；301 的部件都不写，保持缺省 |
| 大小（缺省 0.1） | +0x7c | **+0xC0** | 8 | H |
| 碰撞半径系数 | +0x80 | +0xC4 | 9 | H |
| 速度系数（冲击？） | +0x84 | +0xC8 | 10 | H 对应，含义 M |
| 缺省 0.5 的项（「硬直」） | +0x88 | **+0xCC** | 11 | H 对应 |
| （4.1 独有） | +0x8c | — | | 301 不写 |
| 颜色 RGBA | +0x90 | **+0xD0** | 12 | H |
| 重力系数 | +0xA0 | +0xE0 | 13 | H |
| 自定义参数 | +0xA8（SGO 节点指针） | **+0xE8**（variant，标签 u16 `+0xF8`，1 = SGO 节点） | | H |
| 命中音 / 效果描述 | +0xB0（0x40 字节，`+0x10` cue id） | +0x100（0x7b3200 子对象，0x7b16f0(0x20B2950, dst, 节点, 0) 按 cue 名填） | 14（字符串） | H |
| 种子 | +0x110 | +0x1A0 | | H |

**换算口诀**：team/速度/寿命/伤害 +0x30；爆炸半径、颜色、重力、自定义 +0x40；大小及其后三项 +0x44。

301 各发子弹按 EDF6 偏移填（`s = L/10`，表 = 1.3）：

| 子弹 | 工厂 | +0x94 | +0x98 | +0x9C | +0xB0 | +0xC0 | +0xD0 | +0xE8 | +0x100 |
|---|---|---|---|---|---|---|---|---|---|
| 主炮激光 | LaserBullet02 | 20 | 60 | 5000 | 100（每 8 帧）/0 | 50 | 1,1,1,1 | `[2]`/`[0]` | 砲台命中（每 8 帧） |
| 中型炮火箭 | RocketBullet01 | 0.01 | 2700 | 350s·表 | 80 | 80 | 1,.5,.25,1 | `[2,40,0,0.0005]` | 中型砲台着弾 |
| 中型炮束 | BeamBullet01 | min(20,5+0.1s) | 180 | 0.5s·表 | 0 | max(2,4−0.2s) | .1,1.2,.1,1 | `[0,[1,1,1,1],[1,1,1,1],1,1,2,16]` | 中型砲台ビーム着弾 |
| 板甲激光 | LaserBullet02 | 20 | 60 | 2s·表 | 0 | max(.5,2−.125s) | 2,.1,.2,1 | `[0]` | — |
| 板甲等离子 | PlasmaBullet01 | min(2,.5+.075(s−.1)) | 1200 | 25s·表 | min(15,9.5+.05s) | 5 | 3,1,20,1 | `[1,16]` | パネルキャノン命中 |

团队一律写发射部件的 `+0x180`（EDF6 写部件 `+0x314`）。生成：EDF6 `0x1194280(manager [0x20B2958], 矩阵, 工厂, 参数)`，工厂按名 0x1195C50（巨龙分支已实机用过）。

### 8. 仍未读 / 存疑
- 4.1 字节标志 +0x74/+0x75/+0x78 与 EDF6 +0xB4..+0xBC 的精确一一对应（301 不用）(M)。
- `[0xcc84a0]+0xa804` 的含义（联机人数？单机为 1）(M)。
- 0x1b16b0 / 0x1b1ba0 / 0x1b2070 / 0x1b2ab0 对应 EDF6 EffectGenUtil 的 Flame / Blazing / Explosion / Explosion_Big（docs/edf41-classes.md 第 5 节）：函数对应是读出来的，外观相似度是 M。
- 联机同步（0x257810 / 0x257a10 / 0x25a740 / 0x25aa10 / 0x25ac10 / 0x2541d0 / 0x2543c0）只认出是状态 / 开火帧同步，未逐字段读 (U)。
- 排气口粒子发射器（0x252FD0 的描述块 +0x2A0..+0x380）的逐字段含义 (M)：位置 / 方向 / 开关 / 规模已读（H），见「补完」一节。
- LaserBullet02 自定义 mode 2 的 `+0x1e8 |= 8`、`+0x3b0 = 3` 的确切效果（穿透地形？）(M)。

## 实现（2026-10-15）

- **数据**：`tools/make_edf41_objects.py` 把 `UFOMOTHER301.SGO` 当普通根对象生成（`MOTHER301_ALIAS` 以 E508_CARRIER 为模板补齐 UfoCarrier508 构造读的键，`mother301_fix` 加两个定位点、写 `edf41_se` 音效表；部件 CAS 补 `default` 态）。6 关本篇 + 任务包１ M993 由此解锁（安装器的任务规划按登记表放行）。
- **船体**：UfoCarrier508 构造 + 克隆虚表（槽 0/5/9 + 槽 16 伤害过滤；下午起又接管槽 3 绘制、槽 18 受击）。基类负责路线、耐久按等级缩放；插件做 4.1 的自转（动作 0/1/2）、弱点规则（只认 `RagDollProxys.weak` 刚体——上午的版本按 `weak` 查不到、退成「任何刚体都算」，下午改正——且主炮对象删除后才吃伤害）、受击后 64 帧弱点附近 8 次火焰爆发。死亡：下午起是 4.1 的三段编排（见「补完」），不再走 UfoCarrier508 的。
- **部件**：EventAnimationObject + 克隆虚表，挂在船体上（L2 × A × L1 × 船体矩阵）；首帧 `SetLevel(部件, 船体原始等级)` + 照 UfoCarrier508 的公式算耐久。未启用（+0x900 = 0）的部件保留 EventAnimationObject 构造置的「不掉血」位（+0x380 bit0），不登记队伍（不是目标、不计入敌人数）；启用后清掉该位并登记；静态板甲环永不登记。主炮、中型炮（火箭 + 束机）、板甲、舱门生成器的状态机照「补完规格」逐条实现（帧数、随机区间、子弹参数、音效名、击毁 / 坠落 / 爆炸时序）。
- **脚本接口**：ObjectAction 0–17（组标志只在船体活着时生效）、GetPartsObject（组 0 主炮、组 1 第 N 门中型炮，返回部件的弱引用）、生成器 0x20100（`+0x48` 舱门号；母舰创建当帧就来的命令先存着，部件建好后执行）。生成出的敌人照 EDF6 自己的生成器（0x6E335F）设等级、队伍并激活。
- **没有移植**（下午补完后剩下的，见 4.2）：联机同步（各端各跑部件状态机；子弹是 EDF6 自己的）、活着时的 4.1 移动平滑、排气口的 4.1 粒子发射器本身（换成 EDF6 烟团）。发光、弱点开合循环、4.1 的船体死亡编排、子弹命中音、飞行 / 排气 / 受击音已在「补完」一节补上。
- **实机**（M110，`Edf41MotherForce=15`）：船体与 144 个部件 16 ms 建完；动作 3/5/8/10/13/15 生效；主炮张开、中型炮出现、板甲外推环转、主炮扫射与火箭爆炸、部件被毁后冒烟坠落都在截图里；无崩溃，Mods 与存档逐字节还原。

## 补完（2026-10-15 下午）：受伤与击毁的实机验证、4.1 的船体死亡、发光、弱点开合、排气、音效

代码：`src/edf41mother.cpp`（本类）；共用文件只追加：`src/edf41classes.{h,cpp}` 的 `Bullet::hitSe` / `FillSePreset`（子弹命中音），`src/plugin.cpp` 的 `Edf41MotherForce` 上限 15 → 31（新开发位 16）。

### 新读到的 EDF6 / 4.1 事实（H，除注明）

| 事 | 4.1 | EDF6 |
|---|---|---|
| 材质参数绑定 | 0x32FEF0(绑定, 模型+0x450, 材质名或空, 参数名)；船体 `light0_color` 绑在 +0x930、`scroll_light0` 绑在 +0x8D0（0x250730）；部件 +0x9A0 / +0x940（0x25E5E0） | 绑定对象 0x60 字节：构造 0x6DCBF0、析构 0x3799B0；绑定 0x6DCC80(绑定, **模型组件+0x160**, 材质名或空=所有有此参数的材质, 参数名)；`+8` {材质, 参数下标} 表（数据 / 容量 / 个数 +0x18），`+0x28` 绑定时从第一个材质读到的缺省值（个数 +0x38），`+0x48` 当前值；0x6DD2F0 全部设成一个 vec4；0x6DD420 写进各材质常量并标脏（0x6DCB30）。UfoSmall507 在构造里绑（0x528B79），在**槽 3（绘制）**0x52EEC0 里先写再画（模型组件 +0xC00 → 绑 +0xD60；UfoCarrier508 自己 0x6DC7B0 也用 +0x650+0x160 = +0x7B0）|
| 每帧写什么 | 船体 0x2507D0：`scroll_light0` = 缺省 + (+0x990 向量，+0x994 每帧 += 1/600)；`light0_color` = +0x9A0；部件同理（+0xA00 / +0xA10） | 本类：槽 3 包一层，`ApplyGlow` 后再调原槽 3 |
| 颜色变化 | 起始 (2,0,0,1)，目标 (2,0,0,1)，率 0.01（每帧 xyz 向目标插值，w 不动）。动作 0：当前 = (0,0,0,1)，目标 y、z = 0、w = 1（x 保留：又慢慢回红）；动作 1：当前 (10,10,10,1)、目标 (2,2,2,1)；动作 2：目标 (2,0,0,1)、率 0.01；受击 0x251DE0：当前 (10,10,10,1)；死亡 0x251170：当前 (100,100,100,1)、目标 (0,0,0,1)。部件：+0x905（船体首帧 HP≤0）当前 100、目标 0、率 0.02；主炮张开完 当前 10、目标 (1,1,1,1)，充能 `max(当前, (f−720)/480·10+2)`，扫射每帧 (50,50,100,1)；中型炮出现完 目标 (5,5,5,1)、当前 `max(当前,10)`，开炮 `max(当前, 10·f/600)`；板甲起动 (10,10,10,10)；三者击毁 当前 100、目标 0、率 0.1 | 照抄 |
| 船体 CAS | 主播放器状态 `open` / `show` / `hide` / `close`（0x2527D0 等，`player+0x110 = 1` 速度） | UfoCarrier508 的 CAS 播放器在 +0x980，主播放器下标在 +0xB30（载具自己的状态函数 0x4F823D）；SetState 0x116C2D0(+0x980, [+0xB30], 名字, 0)；播放器数组 `*(+0x980+0x60)`，每个 0x160，时间 +0xE8、长度 +0xF0 |
| 船体受击回调 | 槽 11 0x251170 | **槽 18 OnDamaged(obj, GDI)**（ApplyDamage 接受了伤害才调）。载具的 0x4F9250：活着时放 se_list[0]（E5キャリア508ダメージ）、材质动画闪一下（0x4F8AF0）、+0x1590 计时内在 ダメージエフェクト 点上放火（0x4F8B80）；**死了**（+0x2E8）且 +0x1598 = 0 时进入它自己的死亡状态机（+0x15A0：0x4F9800 爆炸 + 停飞行音 + 坠落 → 0x4F9A00 落地探测 → 0x4F9C10 大爆炸 + 半径 60 区域伤害 + se[2][3] → 删除） |
| 载具的其他死亡入口 | — | 槽 9 消息 **0x1000000F**（0x4F94C0：HP = min(0, max)、0x54DDB0 注销队伍、进入同一个死亡状态） |
| 载具的飞行音 | 4.1 构造 0x24F4AD：`母船UFO飛行` 播进 +0xC10 并跟随 | 0x4F7DE0：SGO `se_fly_name`（模板 E508 的 `E5キャリア508飛行`）播进 **+0xE10** |
| 船体刚体名 | SHKT 里是 `RagDollProxys.weak` / `.waist` / `.hatch` | 0x6EA670 按全名查：`weak` 查不到（之前的实现因此「任何刚体都算」），要用 `RagDollProxys.weak` |
| 子弹命中音 | 参数块 +0xB0（0x40 字节描述） | InitParam **+0x100** 是一个 SePreset（构造 0x1001D0 用 0x7B3200 初始化；武器参数键 14 按 SEPRESET 名填它，0x6CEF81 → 0x7B16F0）。4.1 的名字不在 EDF6 的 SEPRESET 里：`FillSePreset` 按 `edf41_se` 查 cue 名，再像 `PlayCue` 一样填 cue 引用、音量、音高、三个距离 |
| 排气口 | `mp_HatchEmission [16, "weak", 85, 20]`：0x252FD0 在 `weak` 节点空间放 16 个发射器，第 k 个在 (85 sin a, 20, 85 cos a)，a = 2π(k+0.5)/16，沿节点 −Y 喷；粒子 `煙_01`、寿命 300 帧、色 (2,2,2,0.35)→(2,2,2,0)、大小 16→24；0x253750 受伤时把规模设成 2·HP/HPmax；船体状态 3 进入时全开，4 / 5 / 死亡时全关 | 4.1 的持久发射器（0x1C2D80 一族）没有移植：本类在同样 16 个点上每 8 帧放一个 EffectGenUtil 的 ExplosionWind（`煙_01`）烟团，大小 = 2·HP/HPmax（**M：外观近似**） |

### 本类现在怎么做（每条都按上表 / 补完规格第 6 节）

- **受伤**：船体槽 16（`HullAccept`）照 4.1 槽 10：主炮对象还在 → 拒绝；否则只认 `RagDollProxys.weak` 刚体。槽 18 换成 `HullDamaged`（不再调载具的）：受击置 `damaged`、排气规模 = 2·HP/max；`hitFrames == 0` 时 64 帧受击爆炸、在 GDI 命中点放 `母船UFOダメージ`、发光闪 (10,10,10,1)。部件照旧（启用前免伤；主炮 / 中型炮受伤音 60 帧冷却）。
- **主状态机**（只在活着时跑）：合着 → 主炮存在且启用 → `open`（主炮已没了就 `show`），动画完 → 张开（16 个排气口开、`母船UFO排気`，`damaged = 0`、倒数 540）→ 主炮被停用 → `close` → 合着；主炮没了且受过伤后 540 帧 → `hide`（排气口关）30 秒 → `show` → 张开 ……
- **死亡**（槽 18 / 消息 0x1000000F / 每帧检查 HP，三处都只进一次）：发光闪白后熄；`y−350` 处大爆炸 size 2 + `母船UFO本体墜落前爆発`；排气口关、自转目标 0；中型炮、板甲停火；舱门生成器清空；部件全体闪白熄灭（+0x905）。A：每 64 帧在船体周围 350 处随机方向大爆炸（size 2，k 0.6），10 秒后 → B：停飞行音、`母船UFO本体墜落`（循环跟随），每帧 `v.y −= 0.0003`，每 64 帧在 `weak` 节点下 20 处放烟火团（size 1.5，方向在 −Y 的 1 弧度内）；船心下方 300 内碰到地图（`GroundHit`，跳过自己的刚体）或 60 秒 → 再放一次 `母船UFO本体墜落`、`y−200` 处三次大爆炸（size 6，向上）→ C：2 秒后 DeleteObject（部件随之删除）。死后位置由本类按 4.1 积分（死前最后一帧的位移为初速，A 期间每帧 ×0.99，B 起只有重力），每帧写在载具自己的移动之上。
- **发光**：船体与 144 个部件各自在第一次 Update 绑定（所有有这两个参数的材质：船体 2 个，部件各 1 个），每帧按上表插值，槽 3 里写入材质再画。
- **音效**：船体第 1 帧停掉载具的 `se_fly_name` 循环（+0xE10），放 `母船UFO飛行` 并每帧跟随；B 开始时停。子弹命中音：主炮每第 8 发 `母船UFO砲台命中`、中型炮火箭 `母船UFO中型砲台着弾`、束 `母船UFO中型砲台ビーム着弾`、板甲等离子 `母船UFOパネルキャノン命中`（激光没有，同 4.1）。
- **开发开关**：`Edf41MotherForce` 新位 16 = 开局 5 秒后各组只启用不开火（配合 `Edf41DevShoot`：伤害一路打到船体死亡，autopilot 的玩家不会先被母舰打死）；新位 32 = 脚本设的最低耐久（原生 3020，见 1.7）一直压回 0（M130 / M993 的船体在开局那一段按脚本是打不死的）。`Edf41DevShoot` 打部件时瞄炮管（主炮 `waist` 骨骼 × (0,−450,0)、中型炮 `cannon` 骨骼 × (0,−200,0)），因为它们的原点在船体壳里。

### 实机记录（补完）

| 运行（`tmp/m301f/<目录>`） | 任务 | 开关 | 日志 / 截图里看到的 |
|---|---|---|---|
| r02_M130_base | M130 | `Edf41MotherForce=1`、`Edf41DevShoot=20`、伤害 200（补完前的代码） | 玩家子弹打掉 122/125 块板甲、6/10 门中型炮；主炮没打到（瞄的是它在船体壳里的原点）；船体耐久不变；玩家后来被打死。无崩溃、逐字节还原 |
| r03_M130_dmg | M130 | `Force=16`、`DevShoot=20`、200 | 10 门中型炮、主炮被玩家子弹击毁（`items dropped 1`、`destroyed byte 1`：掉落与击杀走的是游戏自己的 ApplyDamage）；主炮坠落 12 秒后删除；船体主状态 2（`open`）→ 3（张开）→ 首次受击后 540 帧 → 4（`hide`）→ 30 秒 → 2（`show`）→ 3；材质绑定：每个部件 1 个材质、船体 2 个，两个参数都绑上。发现 `weak` 刚体按名查不到（-1，改成 `RagDollProxys.weak`）。无崩溃、逐字节还原 |
| r04_M130_kill | M130 | `Force=16`、`DevShoot=10`、1000 | 弱点刚体 = 2（waist 0、hatch 1）：打在 0 上的被拒（`refused`），打在 2 上的被接受、受击回调到了（`hit at …`、受击音、闪白）；船体 HP 一直 5464 / 5464，最低耐久也是 5464——M130 的脚本原生 3020 把最低耐久设成了 100%（见 1.7），这一段船体本就打不动。截图：张开 / 收起、排气烟、发光闪白后回红。无崩溃、逐字节还原 |
| r05_M993_kill | 任务包１ M993 | `Force=16`、`DevShoot=10`、2000 | 主炮、10 门中型炮被击毁；船体 18000 → 16000 → 5400 停住（M993 的脚本把最低耐久设成 30%）；状态循环同上；脚本之后的动作 1 / 13 / 15 生效，4 个舱门生成器各出 DragonSmall401（`hatch #k: enemy #n`）；`GetPartsObject` 在主炮删除后返回空。无崩溃、逐字节还原 |
| r06_M993_death | M993 | `Force=48`（16 + 32）、`DevShoot=10`、2000 | **4.1 的船体死亡全程**：主炮删除后弱点两发打到 0 → 序列 A（帧 1151，`y−350` 大爆炸 + 墜落前爆発，10 秒里每 64 帧一次周围大爆炸，截图满屏爆炸）→ 序列 B（从 y 410 开始自由落体，14.2 秒后船心离地 300：`GroundHit` 在 y 0 命中）→ 序列 C → 2 秒后删除（帧 2731），部件随之消失；截图：坠落中船体灯光转暗、落地爆炸、船体消失后浓烟，**任务随即判定胜利**（脚本的击毁条件照常触发）。无崩溃、逐字节还原 |
| s_M016 | M016 | 无（开发开关全 0） | 船体出生（4573 / 4573，队伍 1），载具的飞行音停、`母船UFO飛行` 接上；船体 2 个材质、部件各 1 个材质绑上发光；30 秒发光 (2,0,0)、自转 0.0003；无崩溃、逐字节还原 |
| s_M110 | M110 | 无 | 中立船（队伍 3）；创建当帧的 4 个生成器任务在部件建好后执行，4 个舱门在 `発生位置` 各出 Ufo301（8 只一波，之后又一波 10 只）；无崩溃、逐字节还原 |
| s_M123 | M123 | 无 | 中立船出生、主状态 1；无崩溃、逐字节还原 |
| s_M130 | M130 | 无 | 动作 0（发光当前清零，之后照 4.1 慢慢回红：30 秒时 (2,0,0)）、14；船体最低耐久 100%；无崩溃、逐字节还原 |
| s_M210 | M210 | 无 | 脚本 45 秒后动作 3 / 5、`GetPartsObject` 拿到主炮；船体 `open`（主状态 2 → 3），主炮充能 20 秒后扫射 61 发；无崩溃、逐字节还原 |
| s_M603 | M603 | 无 | 创建当帧动作 3：船体当帧 `open`，16 秒后张开；动作 5 后主炮两轮扫射（122 发）；无崩溃、逐字节还原 |
| s_M993 | 任务包１ M993 | 无 | 动作 0、`GetPartsObject`；约 1 分钟后动作 1 / 13 / 15：板甲启用开火（74 发），友军打掉 7 块；无崩溃、逐字节还原 |
| t_M110_attack | M110 | `Force=15` | 各组开火（中型炮火箭 + 束 6860 发、主炮扫射、板甲激光 / 等离子），截图满天的束与等离子；中型炮出现后发白光（目标 (5,5,5)）；60 秒主炮被开关直接击毁（不走伤害，`items dropped 0` 合理），12 秒后删除；子弹命中音的 4 个预设全部在 `edf41_se` 里找到（日志没有任何 `hit sound` / `no sound cue` 行）；无崩溃、逐字节还原 |

所有补完运行：`EDF6VehicleCrew.log` 里没有 `check failed`、没有指纹不符、没有缺音效预设 / cue 的行。听不到声音，音效只能证到「预设与 cue 都找到、播放函数被调用」（M）。

### 还剩下的（及原因）

- 活着时的移动平滑、排气口的 4.1 粒子发射器本体、联机同步：见 4.2（原因写在那里）。
- 船体的锁定点：UfoCarrier508 的锁定点组件按 MAB 定位点建锁定点，`mother301_fix` 为了让载具构造函数不崩加的两个定位点（`排出口`、`ダメージエフェクト`，挂在 `waist`）也成了锁定点；4.1 的船体只有 `ターゲット`（`weak` 上）一个。锁定武器可能锁到 `waist` 上的点，打在 `waist` 刚体上会被弱点规则拒绝。没改（要么去掉这两个定位点并让载具构造在缺定位点时不崩，要么在运行时把这两个锁定点设成不可锁定 `+0x2A = 0`，后者 docs/edf41-classes.md 第 7 节是 M）。
- M993 的实机截图里出现过一次字幕 `**不明な再生キー**`（脚本的语音 / 字幕键在表里查不到）：任务数据那边的事，不在本类。
