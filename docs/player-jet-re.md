# 玩家驾驶的战斗机（src/playerjet.cpp）逆向与设计笔记

EDF.dll TimeDateStamp 0x678CCB46，地址都是 RVA。置信度：**H** = 反汇编 + 实测（本仓已有功能在用），**M** = 反汇编读出、未在本功能里实测，**L** = 推断，需要实机确认。

## 1. 机体与身份

| 事实 | 来源 | 置信度 |
|---|---|---|
| 玩家机和 NPC 战机一样是 `Vehicle506_Helicopter`（vtable 0x17DB238），SGO 从 `V506_HELI` 派生 | jet.cpp / testrange/gen.py JETS | H |
| 用速度增益 k（`veh+0x162C`，SGO `mission_setup[1][0]` / `vehicle_setup[1][0]`）当标记：7201 战斗机、7202 攻击机。标记区间表只有一份（body506.cpp `kMarks`：喷气机 7001–7099、潜舰 7101、玩家机 7201–7299），`IsPlayerJet` 走 `BodyOf`，不再各自比较浮点 | body506.cpp | H |
| crew.cpp `Crew()` 对这两个标记不派 NPC（`IsPlayerJet`），空机一直停着 | 本次改动 | H（代码） |
| 每帧两段：slot 55 输入（原版之后，crew.cpp InputHook → `PlayerJetFrame`）算速度；slot 57 物理 0x61B710 只由 body506.cpp 挂一次，按标记分发到 `PlayerJetBodyStep`，用 0x11B18F0 / 0x11B1760 写刚体线速度和角速度 | jet.cpp 同款 | H |
| 机身部件索引 `veh+0x1530` 在换了外形的模型里是 -1，坠毁流程不检查就读；用 0x6EA4B0 按 `bomber501` / `bomber401` / `body` 找回来（body506.cpp `FixBodyPart506`，与潜舰共用） | jet.cpp 同款 | H |
| 移动区域收缩 `veh+0xE00` 驾驶时设 -1e6（不被空气墙夹回），下机还原 | docs/map-edge-re.md | H |

## 2. 输入

| 事实 | 置信度 |
|---|---|
| 座位 0 的摇杆块：LX `+0x2C0`、LY `+0x2C4`、RX `+0x2D0`、RY `+0x2D4`、上升扳机 `+0x2E0`（手柄模拟量 0..1，键盘 0/1）；主射击 `+0x2E4`、副射击字 `+0x2E8` 的 0x20 位由原版 506 第 55 槽自己转成开火（机炮 0/1、导弹 2），插件不动 | H（docs/heli-input-re.md） |
| 机体输入块 `+0x1540` 横移(-LX)、`+0x1544` 油门、`+0x1548` 前进(-LY)、`+0x154C`=1、`+0x1550` 偏航(-RX)；驾驶时每帧清零（无旋翼升力、无直升机操纵） | H（jet.cpp 同款） |
| 朝向角 a 的机头 = (sin a, 0, cos a)（0x4CD10），偏航 = -RX 加到 a 上：RX>0 右转、a 减小；所以「右」= (-fwd.z, 0, fwd.x)。LX>0 同样向右 | M-H |
| 摇杆往前推 Y 为负（前进 = -LY）。俯仰按同样约定：-RY = 拉杆抬头；鼠标上下映射到 RY 的符号和幅度**没实测**，`PlayerJetInvertPitch=1` 可反过来 | M / L |
| 油门：左摇杆前推（LY<-0.3）或上升键（>0.5）加，后拉减，每秒 0.6，松手保持 | 设计 |
| 转弯 = clamp(RX+LX)；地面是机头轮转向（0.8 rad/s，25 m/s 以上按速度递减），空中是压坡度拉杆 | 设计 |

## 3. 飞行模型（街机，`kKinds`）

| | 战斗机 7201 | 攻击机 7202 |
|---|---|---|
| 最低空速 / 起飞 / 最高（m/s） | 65 / 75 / 260 | 60 / 70 / 240 |
| 推力 / 减速（m/s²） | 16 / 20 | 11 / 15 |
| 最大过载 / 滚转率 | 6 g / 2.6 rad/s | 5 g / 1.6 rad/s |
| 无损接地最高速 | 130 | 120 |
| 撞击半径（`ram`） | 10 m | 12 m |

数值以 `src/playerjet.cpp` 的 `kKinds` 为准（本表 2026-10-04 按代码改正：此前写的 195/180、7 g 是旧值）。

- 时间步：所有积分（油门、空中、地面、舵面）和「实测速度」都用游戏推进的时间：两帧之间的 GameMs 差，最多 1/60 s（body506.cpp `GameStep`）。游戏每帧只推进 1/60 s 的位移，低于 60 帧时用墙钟 dt 会把实测速度算小一半以上，被误判成撞墙（2026-10-04 审查：约 30 帧以下必误判）。
- 空中（playerjet.cpp `Air` / `Hold`）：升力只沿飞机自己的上方（`PJet::up`，横滚杆绕航迹转它），大小最多 maxG·min(1, (v/corner)²)·g；松杆时是 `Hold`：g·cos(爬升角) 的 kHold（0.988）倍，下沉率越接近 kSettleSink（1.5 m/s）越补满，所以平飞稳定在 1.5 m/s 的缓下滑、爬升慢慢压低、俯冲保持；右摇杆转弯时自动压坡度到 kTurnBank（1.2 rad），`Hold` 再除以 cos(坡度)（协调转弯，最多 4 倍）；拉杆在 Hold 与最大升力间插值，推杆最多到 -0.5 倍最大升力。重力整体作用在航迹上（不再抵消）。速度：推力 thrust·(want/top)²（want 是油门对应的平飞速度）减寄生阻力 thrust·(v/top)²、诱导阻力 0.3·n²·(corner/v)²、减速板 brake·(v/top)²（拉杆向后时）、g·sinθ；夹在 [kStallFloor 25, kBodyTop 340] m/s（25 m/s 时机翼几乎无升力，航迹自然下坠：失速改出；机体自己的运动属性解除了 Havok 的 200 m/s 上限，jetprops.cpp 600）。座舱读数显示 G 与 STALL（最大升力 < 1.05 倍维持航迹所需）。数值验证：tools 外的点质量模拟（同公式）——巡航松杆 60 s 掉 71 m、稳定 -1.5 m/s；满舵持续转弯 20 s 172→152 m/s 高度基本不变；慢速满拉 48 m/s 出现 STALL 后俯冲改出。
- 地面：沿机头水平滑跑，油门 0 时 12 m/s² 刹停，停下且油门关闭 = parked（插件不写速度，原版代码把它放在原地）；到起飞速度拉杆（俯仰 >0.2）离地，或油门 ≥60% 且再快 20 m/s 自动离地；从边缘开出去（离地 >6 m）进入空中。
- 接地（离地 <3 m 且在下沉，或沿速度方向 3 帧内会碰到）：下沉 ≤10 m/s、机身 up.y ≥0.77、机头俯角不超过约 15 度、速度 ≤ 无损接地速度 → 降落转滑跑；否则坠毁扣血，并把下沉截到地面上方。
- 离地高度：取地面（地图射线）和水面（`SeaAt`，游戏自己的水域）中较高的一个。地图射线穿过水面打到海床（docs/water-re.md），以前把海床当地面。
- 撞到东西（建筑、敌人、地图墙）：150 ms 内实际位移不到下达速度的一半（空中 >40 m/s，滑跑 >20 m/s）→ 按损失速度坠毁扣血；空中水平速度反向、以最低空速弹开，滑跑直接停下。
- 撞击伤害（2026-10-04 用户要求；2026-10-05 改为按质量和速度）：上面这种撞到东西的坠毁（不含撞地、落水），在机头位置（沿下达速度方向半个撞击半径）调 `ImpactDamage`（jet_bay.cpp）：伤害 = ½·m·v² ÷ `kRamJoulesPerDamage`（2.87e5 J，按 Mk 82 的 1500 伤害 ≈ 430 MJ 装药定）× 强度倍率 × `PlayerJetRamDamage`（`RamDamage`）。m = `JetMassOf(BodyMark(v))` 的空重 × `Burden.mass`（挂载）；v = 下达速度 − 实测沿该方向的速度（被挡掉的部分 = 沿接触法向的接近速度，对方迎面飞来时实测为负，v 更大）；强度倍率 = 最大 HP ÷ 机体 SGO 的耐久（`kJetMasses` 第三列，`tools/gen_stores.py` 从 `JETS` 生成），与游戏放大武器伤害的倍数一致（**L**：假定耐久和武器伤害按同一倍数放大，原版 tier 两个乘数相同时成立）。半径 = 机型的 `ram`，只伤敌方阵营、记在本机名下。与坠毁同一个 1 秒节流。每种插件飞机都有质量（selftest `jet_masses_cover_every_jet`），没有质量的机种不造成撞击伤害（日志说明）。
- 天花板（`*(image+0x20B2998)+0x3C`）下 12 m 不再上升。
- ±2.4 km 世界边界：越过边界且朝外飞时，航向沿墙转向（保留沿墙方向的分量；正对墙时转向右侧），并带 0.3 的向内分量，速度大小不变（`WallTurn`）。以前只把向外分量清零，正对墙垂直撞上时水平速度为 0，下一帧又被清零，就悬停在墙上。
- 落水：506 收到水消息（0x10000025）会当直升机落水、每帧给自己发 2 倍 HP 的伤害；body506.cpp 的 slot 9 钩子把它拦下，交给插件自己的模型：空中触水一律算坠毁（不能水上降落）；浮在水面（滑跑/停着，或空机泡在水里）每秒算一次坠毁（每次至少 20% 最大 HP），约 5 秒解体。
- 舵面：模型有 `elevon_L/R` 骨骼时照 jet.cpp 动（攻击机外形有，战斗机的截击机外形没有）。

## 4. 坠毁伤害与死亡

| 事实 | 置信度 |
|---|---|
| HP `veh+0x2F8`、最大 HP `veh+0x2F4`、死亡字节 `veh+0x2E8` | H |
| 扣血 = 最大 HP × clamp(0.2 + 0.04×(下沉-10) + 0.01×(速度-无损速度) + 坡度过大 0.3, 0.2, 1.5)，1 秒内只算一次 | 设计 |
| 0x6329B0(vehicle) 是消息处理 0x62ECB0 收到 0x1000000F 时走的死亡流程：踢出所有座位、HP 置 min(0, max)、置死亡字节、jmp 0x62F200（爆炸/残骸） | 反汇编 M |
| HP 扣到 ≤0 时，插件把死亡消息 0x1000000F 经机体自己的消息槽（vtable slot 9）发过去（body506.cpp `Die506`，数据块全 0），走 506 的 0x652E70 → 0x62ECB0 → 0x6329B0，和游戏投递消息的路径一样，不再直接调 0x6329B0。前提：0x652E70 头部、0x652E8E 的 `cmp edx,10000025h`、0x6329B0 的签名（`48 89 5C 24 08 57 48 83 EC 20 48 69 81 18 06 00 00 40 03 00 00`）都对得上 | 路径 M；0x62ECB0 的 0x1000000F 分支不读数据块 **L** |
| 死亡路径校验不通过时，坠毁最多把 HP 扣到 1（日志说明一次），绝不留下「HP 0 还在飞」的半死状态；之后敌人的下一次命中走游戏自己的死亡 | 设计 |

## 5. 数据（SGO / 呼叫武器）

| 事实 | 置信度 |
|---|---|
| `testrange/gen.py` JETS 新增 `edf6tr_pjet_fighter_mission`（截击机外形 bomber501_2，耐久 1400）、`edf6tr_pjet_strike_mission`（带舵面 bomber501，耐久 2200），`player=True`：除 `mission_setup` 外再写一份同内容的 `vehicle_setup`，并把 `game_object_camera_setting` 的偏移改成 (0,6,-24) / (0,8,-32)（原版 506 是 (0,5.5,-11.5)） | 生成 M；镜头字段含义 **L**（只按 V506_HELI 的值推断是相机偏移） |
| `tools/make_jets.py` 写 `Mods/OBJECT/EDF6VC_PJET_FIGHTER.SGO` / `EDF6VC_PJET_STRIKE.SGO` | H（代码） |
| `tools/call_weapons.py` 追加 2 行（共 19）：`EDF6VC_CALL_PJET_FIGHTER` / `_STRIKE`，复制原版 eWeapon394（N9 Eros 载具请求，Weapon_Sub，类别 308）：`Ammo_CustomParameter[4]` = [运输机, 箱子, 载具 SGO, 载具设定]，载具 SGO 换成玩家机，设定 `[1][0]`（k）换成标记，机炮换成 `edf6vc_jet_gun_l/r`，`resource` 同步替换；装填 6000 / 6500 | 生成 H（离线 build 已核对）；运输机能否把这个派生 SGO 正常投下 **L** |
| 名字带 `EDF6VC_CALL_` 前缀，airstrike.cpp 读档时一并设为已拥有；airstrike 只拦 Weapon_RadioContact 的标记，这两行（Weapon_Sub）不受影响，也不进 `]`/`[` 切换 | M |
| 一个 SGO 同时有 `vehicle_setup` 和 `mission_setup`：请求武器用的是武器里自带的设定，任务放置用 `mission_setup` | **L** |
| 启动器「NPC 驾驶」栏里的玩家机也按空机放（`placements`），否则 NPC 坐上去会被直升机飞控按 k=7201 乱飞 | H（代码） |

## 6. 待实机确认

1. RY / 鼠标上下的符号与幅度（默认 -RY = 抬头）；键盘玩家用什么键俯仰。
2. 镜头偏移是否就是 `game_object_camera_setting[1]`，距离是否合适。
3. HP 扣到 0 时从输入步骤里发死亡消息 0x1000000F 是否安全（会不会在本帧的输入步骤里踢人导致崩溃；0x62ECB0 是否读消息数据）。
6. 撞击伤害：`ImpactDamage` 的范围和归属（jet.cpp 实现），被撞的敌人掉血是否和本机受损相称。
7. 世界边界：越界后沿墙转向的手感；水面坠毁与浮水解体的时间。
4. 载具请求能否把玩家机送到并正常卸下；燃料（20000，耗 1.5）耗尽后原版会不会强制它下坠（插件写速度，应该无影响）。
5. 地面滑跑与接地判定的高度（机体原点离地约 1.3 m 的假设）。

日志（`Debug=1`）以 `PJET` 开头：`boarded`、`takeoff`、每 2 秒一行状态（相位、速度、爬升率、油门、离地高度（`(water)` = 水面）、HP、以及座位原始 LX/LY/RX/RY/上升量）、`landed`、`crash`、`hit the water`、`blocked`、`rammed`、`destroyed`、`left`。

## 7. 空中弹射和降落伞（2026-10-05）

全部是静态分析（H 读代码确认，M 推断），尚未在游戏里实测。

**人物速度**：
- 步行控制器在 `human+0x680`，每帧由 `0x11B9890` 推进。
- 携带速度在 `human+0x6B0/+0x6B4/+0x6B8`，单位 m/s，写入会保留到下一帧。空中只有 y 分量每帧加重力（`human+0x700`，乘 1/60），x、z 不变（H）。
- 着地状态在 `human+0x711`：2 地面，1 滑行，0 空中。
- 不要写 Havok 刚体的速度，每帧都会被覆盖（H）。

**下车清零**：
- 原版下车时，乘坐状态在结束阶段（`0x57B11A`）清掉 `human+0x380` 的 0x80 位，并把 `0x6A0..0x6B8` 清零（H）。
- 所以插件等这个位清掉以后再写速度，写入就不会被覆盖。

**弹射**：
- 走游戏自己的跳跃请求：`+0x1294` 跳跃速度、`+0x1290` 请求标志，在 `0x575A7A` 被消费，调用 `0x11B9450(ctrl, vy)`，同时同步给其他玩家（H）。
- 插件给 25 m/s 向上，加上飞机水平速度的 30%。只在离地 15 m 以上下车时触发。

**降落伞**：
- 越过最高点后，每帧把 `+0x6B4` 限制在 −6 m/s 以上；水平速度、步行推力 `+0x1210` 和受击推力 `+0x11F0` 的 x、z 每秒衰减约 60%。
- 以下情况停止：
  - 着地（`+0x711 == 2`）。
  - 死亡（`+0x2E8`）。
  - 进入附着或布娃娃状态（`+0x39C != 0`）。
  - 玩家自己往上飞，也就是翼装或剑兵推进，竖直速度一帧涨 3 m/s 以上。
  - 超过 3 分钟。
- 伞开（越过最高点）时显示伞衣模型，见 §8。

**空中接人**（ini `PlayerJetCatch`，默认开；2026-10-05 改为从场外飞进来）：
- 每个任务开始时预载两种玩家飞机的车辆文件（`PreloadPlayerJets`）。
- 弹射 4 秒后，如果玩家离地还有 40 m 以上，就沿原航向往回 1.5 km 处生成一架同型号的空飞机，朝玩家方向：
  - 走 `CreateObject` 生成，车辆文件自带 `mission_setup` 武器。
  - 阵营设为 5（无主载具），按任务难度调等级（`LevelVehicle`）。
- 由插件自动驾驶飞进来（`AutoFly`）：
  - 用玩家飞机自己的飞行模型（`Air`），按鼠标瞄准那套转向逻辑，瞄准点是玩家下方 2 m、按其漂移提前 1 s 的会合点；离地不低于 30 m。
  - 250 m 以外满油门；最后 250 m 直接朝会合点飞，速度取原飞机的速度，至少比抬轮速度快 40 m/s。
  - 物理步进把自动驾驶中的飞机当成有人驾驶的飞机一样推进（`PlayerJetBodyStep`）。
- 离玩家 9 m 以内时每帧替玩家按上车键（`PressBoardButton`）。上车后飞机按原来的速度交给玩家，伞降结束。
- 45 秒没接上就放弃：那架飞机空着飞走、落下，伞降继续。

## 8. 伞衣模型（2026-10-05）

EDF.dll TimeDateStamp `0x678CCB46`，下列地址都是 RVA；纯静态分析（`tools/edfre.py` + capstone），没有实机验证。

**挑哪种对象来显示一个静态模型**（Root.cpk 里全部 `OBJECT/*.SGO` 的 `xgs_scene_object_class` 都过了一遍）：

| 候选 | 结论 |
|---|---|
| `Decoy`（招募员人偶，`jet_carrier.cpp` 用过） | 不用。`BasicAnimationCharacter`，有布娃娃刚体、受伤动画、阵营 4 会被敌人当目标；还要骨架和 `.cas` 动画对得上模型 |
| `RouteGuide_Arrow`（`GUIDEARROW.SGO`） | 不用。更新 `0x5C9F20` 每帧调寻路对象 `+0x1B0`（没有就空指针），模式 1 没有目标时自删（`0x5C9DA0`），朝向每 30 帧被路线改写（`0x5CA8B0`） |
| `Humanoid_BigGreyBoss_EffectModel` | 不用。更新里 `0x44D740` 读 owner 弱指针，没有 owner 时读绝对地址 `0x2F8`，必崩 |
| **`FarEventObject`**（远景的工厂、挖掘机，如 `EV601_PLANT.SGO`） | **用它**，见下 |

**FarEventObject**（vtable `0x17D5DA0`，工厂 `0x5C3A00`：`new 0xAD0` → GameObjectBase ctor `0x545670(obj, initparam)`）（H）：
- ctor 置 `+0xAC0..+0xAC8` 缩放为 (1,1,1)，读 SGO 的 `setting`（`0x5C3E50`）：`scale`、`default_animation`（可选，查不到键就跳过），再调 `0x5C4970`。
- `0x5C4970` 只在 SGO 有 `ragdoll` 键时才建物理体；**没有 `ragdoll` 就没有刚体**。原版远景对象都没有这个键。
- 更新（vtable 槽 5，`0x5C4FB0`）：GameObjectBase 基类更新 `0x54BE40`，然后把 `+0x60..+0x9F`（对象矩阵）乘缩放写进渲染矩阵（`0x1100B90(obj+0x660, …)`），有动画就推进 `+0xAA0`。**它从不写 `+0x60`**，所以插件每帧写这块矩阵就能让它跟着走。
- `animation_model` 写 `[[mrab, mdb], 0, 0]`（不带 `.cas`、动画数据、伤害网格）：`CollapseModel`、`RouteGuide_Arrow`、BigGreyBoss 特效模型的原版 SGO 都是这种写法。

**插件**（`src/playerjet.cpp` Chute*；模型 `pylib/chute_model.py`，安装 `tools/make_chute.py`）：
- 任务开始时，若 `Mods/OBJECT/EDF6VC_CHUTE.SGO` 存在就预载（与接人飞机同一时机，`PreloadPlayerJets`）。
- 伞开时 `CreateObject(mgr, 矩阵, L"app:/object/edf6vc_chute.sgo", InitParamBase@SceneObject)`，核对 vtable 是 FarEventObject，经 `SetTeam` 设为中立阵营 3（与谁都不敌对，M）。
- 每帧矩阵 = 玩家脚下位置 + 4 m，直立，正面朝水平漂移方向（漂移低于 0.5 m/s 时保持上一次的朝向）。
- 伞降结束（着地、自己飞走、被接走、死亡、附着、超时、接人飞机没了）时删除（`0x118A1B0`）；任务开始时只忘掉指针（对象随上一任务一起没了）。日志行 `CHUTE made / gone: 原因 / not made: 原因`。
- 安装时核对 `0x5C4FB0` 的开头字节，并核对 vtable 槽 5 指向它；对不上就不做伞衣（`HOOK player jets … chute=0`）。

**模型**：半椭球伞衣，直径 7.5 m、高 3 m、底部开口，内外两层（各自朝外/朝内，互相错开 2 cm，背面剔除开不开都能看见）；16 根伞绳从伞缘收到玩家肩部（每根两条十字交叉的双面细条）。材质整个照搬 Grape（`VEHICLE401_STRIKER.MRAB`）座椅布料：`snd_BRDF_Common_SeparateOcc`，`v401_interiorSheet_*` 128 px 平铺布纹，1 m 一个重复；遮蔽贴图的 texcoord1 钉在 `v401_inner_occ.DDS` 最亮的一个 DXT1 块上。三角形绕序按原版：`cross(b−a, c−a)` 与顶点法线同向（V401 / V506 每个三角形都如此）；切线 = dP/du，副法线 = dP/dv。

**待实机确认**：
- 伞衣确实显示、位置正确，玩家和子弹都穿得过去，敌人不打它（M：没有刚体与中立阵营都是静态结论）。
- 插件写矩阵与对象更新的先后：若插件在对象更新之后写，渲染矩阵晚一帧（刚弹射时水平速度大，伞衣会落后约 1 m）。
- 远处的剔除包围盒取 bone 的 half/centre（已按模型重算）。

## 9. 战斗机 HUD 的数据（2026-10-05）

每帧 `Fly` 在 `Stores` 之后调 `Sight`（机头、航迹方向、机炮弹道读自机炮武器、弹着圈、按锁定点差分出目标速度后的提前量）和
`Threats`（`MissilesHomingAt` 来袭导弹的位置、`jet::LockersOf` 锁定本机的敌机位置），存进 `PJet::sym`，`PlayerJetHud` 随读数发布；
绘制、原版红线的隐藏和验证见 `docs/hud-re.md` §5、`docs/aim-line-re.md` 第 5 条。全部按 `PJet`（玩家正在驾驶的那架）取数，
不按机型标记，玩家以后能开的其它插件飞机同样适用；没有机炮的飞机不画瞄准具，悬停（速度 < 5 m/s）时不画速度矢量。

## 起落架（2026-10-05）

模型、骨骼、收放角度、离线自检数值和需要实机确认的项目见 `docs/jet-model-re.md`「起落架」。飞行上的影响都在 `src/playerjet.cpp` 的几处调用里：
- `Air`：阻力的寄生项加上 `GearDragShare`（放下时 1.5 倍干净构型）。
- `Touch`：着陆条件都满足但 `GearDown` 为假 → `BellyLanding`（`Crash` 的伤害，转为滑行）。
- `Ground`：`GearDown` 为假时（机腹着地）只减速（10 m/s²），不转向、不起飞。
- `PilotGear`：读键（`PlayerJetGearKey`）/ 手柄位（`PlayerJetGearButton`，座位按键位 `seat+0x2E8`，docs/stores-re.md §4），交给 `gear.cpp PlayerGear`；地面上收起被拒绝。

## 10. 插件的其它飞机也能开（2026-10-05，`src/playerjet_kinds.h`、`src/playerjet_board.inc`）

用户：「所有飞机我们都能开对吧……逻辑能复用的复用，不能复用的就写。我希望能开是真正的每个都很好用」。

### 10.1 走同一条路

- 可登机的机种表是 `playerjet_kinds.h` 的 `kBoardable`（机体 `jet::Body` → 飞法 wing / rotor → 自带武器 → 性能 `Perf`）。性能由 NPC 自己飞的那一行（`jet_internal.h kKinds`，轰炸机用原版轰炸机 180 m/s 的一行 `kStockBomber`）按固定规则换算，规则在文件头；同规则算出的 NPC 战斗机 / 攻击机与手调的 `kKinds[0] / [1]` 每项相差 ≤ 10%，`playerjet_kinds.inc` 里 `static_assert` 检查；`tools/pjet_kinds.cpp`（`cmake --build build --target pjet_kinds`）打印整张表并检查可飞性。
- 识别：`BodyOf == PluginBody::jet` 时 `KindOf` 走 `BoardKindOf`（标记 → `kBodies` 行；7001 被攻击机和两种接管轰炸机共用，按模型骨骼 `BomberBody` 区分）。敌方机体（`BodyRow::hostile`）没有行。
- 玩家坐上后它和玩家战斗机是同一条路径：`PJet` 记录、`Fly`、`Air` / `Ground`、挂载、座舱读数（`PlayerJetHud`）、弹射与接机。`PJet::board` 非空就是这种飞机，只多出三处分支：旋翼机的 `HoverStep`、特殊挂载（`SpecialStore` / `FireSpecial` / `SpecialFrame`）、下机（`Left`）。其它人做的座舱 HUD、起落架都挂在 `PJet` 上，对它们同样生效。
- 物理步：`body506.cpp` 对 `PluginBody::jet` 先问 `PlayerJetBodyStep`（玩家开着、或它在为玩家降落时写速度），否则照旧 `JetBodyStep`；消息（落水）同理先给 `PlayerJetMessage`，它只接玩家开着的。
- 持有（`Held` / `PlayerJetHolds`）：玩家在座位上、它在下来接玩家（hail）、在接弹射的玩家、或停在玩家下机的地方。持有期间 `JetFrame` 一开头就返回（燃料、撤离、呼叫的航线都不动），`crew.cpp Crew` 不给它派 NPC。交回 NPC 时 `jet::ResumeNpc` 把这段时间加到 `bornAt` 上（燃料和出击计时停住）、清掉运动状态、模式回到巡逻（贴地则起飞；撤离、投弹、回母舰保持）。

### 10.2 上机

- `crew.cpp FindSeatHook / PromptHook` 原来对一切 `IsJet` 直接拒绝；现在 `PlayerJetBoardable` 为真时照常走「挤掉 NPC」：我方阵营、没在被删除 / 自爆、离地 ≤ 12 m（`GroundClearance`）、速度 ≤ 8 m/s（玩家记录的实测速度，否则 jet.cpp 的 `m.real`）。座位检查按「无主载具」队伍 5 做（`OwnTeam`），因为 NPC 飞行员把它放在友军队伍 2，原版检查不让玩家进 2。喷气机只有一个座位，NPC 被 `kSeatKick` 踢掉（死亡）。
- 呼叫（`HailTick`，ini `PlayerJetHailKey`）：步行时按键，最近的我方插件飞机（NPC 在飞、未撤离）下来。旋翼机：选身边一处平地（`PickSpot`），用 NPC 的 `Hover` 平飞过去（离得远时保持至少「落点 + 80 m」高度），近了垂直落下，停住后等。固定翼：`PlanStrip` 每帧评估 12 个候选（6 个环 × 12 个方位 × 12 个航向，按环由近到远，一环内有可用的就停），条件见代码常量；然后 `Approach`：先飞到 1500 m 外的进近入口并对准跑道（目标点在入口后方，距离一半处，把航线拉到跑道延长线上），进入 400 m 且航迹与跑道夹角 < 32° 转入五边，沿 4° 下滑道以「起飞速度 + 10」飞（`SteerAt`：自动驾驶的瞄准转向 + 油门/减速板控速），由 `Air` 的接地判定落地（下沉 ≤ 10 m/s、机翼水平、速度 ≤ landMax）；过头 100 m、偏离 150 m、高于下滑道 80 m 或低 40 m 都复飞，三次失败作罢。`Rollout` 沿机头滑跑、按 `sqrt(2·8·剩余距离)` 收油门刹到停止点。停下后等 90 s（NPC 还在座位上），超时交回。

### 10.3 武器

- 机炮和挂载同玩家战斗机。挂载循环里多一项「特殊挂载」（`Store::weapon` 为空，`stores.cpp` 的锁定 / 扳机函数原本就对空武器什么都不做）：
  - **BOMB BAY**：喷气机还带着接管来的弹仓（`BayState::ifc` 且没开过）。副射击 → `jet::PlayerOpenBay`：`bombAt` = 当前 CCIP 落点，`bombDir` / `bombSpeed` = 此刻的水平速度，`bayFrom = -reach`，所以第一颗炸弹瞄 CCIP，之后每帧 `BayFrame` 按原版轰炸机每帧前移一个速度（`docs/airstrike-re.md`）。
  - **SHELLS**（炮舰机）：`jet::PlayerShell`，同 `GunshipFire` 的炮弹、间隔、射程，目标是屏幕中心视线与地面的交点（`CameraRay`：由上一帧 view-projection 求逆，眼睛 = (0,0,1,0)·VP⁻¹，中心点 = (0,0,0.5,1)·VP⁻¹）；按住目标键绕点盘旋（`Orbit`：`steered` 让 `Air` 按瞄准点转向，鼠标只转镜头）。
  - **DRONES**（三种航母）：`jet::PlayerLaunchDrone` 复用 NPC 的发射（`LaunchOne`，从 `LaunchDrones` 抽出来，行为不变），同时给航母记 `CarrierState::order`：它放出的无人机以这一点（抬高 40 m）为锚点、400 m 内找目标（`jet.cpp JetFrame`）；目标键 `RecallDrones`。
  - **CHARGE**（自爆 / 人偶无人机）：副射击后 100 ms 内每帧置 0x2021（2 号挂架，炸药，同 `jet_carrier.cpp Blast`），300 ms 后 `Kill`（原版死亡消息，踢出座位，玩家按 §7 弹射）。

### 10.4 下机（`Left`）

- 地面上（`Phase` 不是 air）：`keep`，原地等玩家。
- 空中且弹射、固定翼、`PlayerJetCatch=1`：这架飞机本身就是接机的那架（`catchFlight` / `bail.caught` 指向它，§7 的 `AutoFly` / `Catch` 原样用）；没接上（玩家落地、死亡，或 45 s 放弃）就交回 NPC。
- 其它空中情况：`HandBack`：座位空则 `RideAi(false)` 坐上 NPC，`ResumeNpc`。

### 10.5 验证状态

- 静态：`build.cmd` 无警告（/W4 /WX）；`static_assert`（行一致性、派生与玩家战斗机/攻击机的偏差）；`build\pjet_kinds.exe` 13 行全部通过；`tools/selftest.py` 新增 `every_npc_aircraft_boardable`（每个我方喷气机体都有行、敌方没有、ini 键被读且有文档）。
- 未实机验证（需要在游戏里逐项看）：
  1. 每种飞机的上车点（`vehicle_riding_position`）是否在原版上车距离内够得着：航母（59×77 m 机体，放大 1.6 倍）、轰炸机（BOMBER401 原尺寸）、无人机（5.7 m）。
  2. 挤掉 NPC 后座位队伍、`RideAi(false)` 交回后 NPC 能否正常接着飞（日志 `JET v=... back to its NPC pilot`、之后的 `JET` 模式行）。
  3. 呼叫：旋翼机落点、下降与停放；固定翼找跑道的耗时与成功率（城市、山地各一张图）、进近是否稳定接地（`PJET hail ... on the final` → `landed` → `down ... waiting`）、滑跑能否停在停止点附近；`MapRay` 每帧约 240 条射线的开销（`PERF` 行）。
  4. 每种固定翼的手感（尤其炮舰机 2 g、轰炸机 3 g）；旋翼机键鼠/手柄操作、航母推进舱是否随推力转、落地判定。
  5. 特殊挂载：弹仓第一颗是否落在 CCIP；炮弹落点与屏幕中心十字是否一致（`CameraRay` 的反投影约定：行向量、D3D 深度）；盘旋方向（目标在左）与半径；无人机按指定点找目标、召回、在玩家开着的航母上停靠；自爆后玩家被抛出、伤害不伤友军。
  6. 交回 NPC 后：燃料计时确实停过（`back to its NPC pilot ... s of its fuel flown`）、轰炸机的投弹航线、无人机回到母舰。

## 11. 炮舰机的机组：驾驶座 + 侧炮手座（2026-10-05，`src/playerjet_crew.inc`）

用户：「炮舰机应该是多人开的」，先做单机多座位（联机同步另行研究）：AC-130 那样一个驾驶、一个侧炮手，玩家可以坐任一个，另一个由 NPC 担任。

### 11.1 座位从哪来（静态逆向）

| 事实 | 来源 | 置信度 |
|---|---|---|
| V506_HELI.SGO 的 `vehicle_riding_position` **只有一项**（驾驶座：上车口 `搭乗口１`、座位 `操縦席１`、姿势 `506_HELI_DRIVER`、职业掩码 9、数字 10.0、按键行 5）。「直升机副座 / 炮艇模式」说的是 VEHICLE410_HELI 的两个门炮手座（`410_HELI_GUNNER_L/R`，掩码 15，数字 0，按键行 6），506 机体本身没有第二个座位 | 读 Root.cpk | H |
| 座位数 = `vehicle_riding_position` 的项数：车辆初始化 0x629B36 起逐项调 0x62B430(veh, 项)，建完写 `veh+0x618`（座位数）、`+0x620`、`+0x624`（占用掩码 = (1<<n)-1），并把各座位上车口离原点的最大距离写进 `veh+0xE00` | 反汇编 | H |
| 一项的 7 个元素（0x62B430 往临时座位结构写，结构基址 = 座位）：[0] 上车口定位点 → 座位 `+0x1E0`（CanRideSeat 0x6346D0 的距离判定用它和定位点自身 `+0x10` 的半径）；[1] 座位定位点 → `+0x1F0`；[2][0] 第三个定位点 → `+0x208`、[2][1..2] → `+0x240/+0x244`；[3] 姿势名（0x7A3500 查表）→ `+0x18`；[4] 职业掩码 → `+0x30`；[5] 浮点 → `+0x258`（含义未查）；[6] 整数 → `+0x2B4` | 反汇编 | H（偏移）/ L（+0x258 的含义） |
| 定位点按名字在 `veh+0xE40` 的表里找（0x6BADD0）。喷气机的 MAB 只有 V506 那一组定位点（父骨骼已改成 `mdl`，`vcobjects.JET_MAB_ROOT`），找不到的名字在初始化里读空指针（EDF+0x62B619，见 jet_sgo 的注释）。所以炮手座**共用驾驶座的三个定位点**（同一个舱门上，人都看不见机舱里面） | 反汇编 + 已有崩溃记录 | H |
| `seat+0x2B4` 是按键配置行：HumanBase 0x57339A 以行偏移 `0xA8 × (human+0xD40 × 16 + seat+0x2B4)` 读键位表（该函数里的 rbx）的 +0x940 / +0x958（键盘的上升 / 主射击，写 `seat+0x2E0/+0x2E4`）和其后各按键位。5 = 直升机驾驶员，6 = 门炮手 / 乘员（V507 救援车的座位、Proteus 炮手也是 6）；0x56D7D8 另把 3 当特例 | 反汇编 | H（读法）/ L（行 6 的主射击键在键鼠上是哪个键） |
| 506 的第 55 / 57 槽只读座位 0 的输入，只开 `veh+0x638` 的持有者 0–2（heli-input-re.md §2b）：炮手座的扳机原版不接任何武器；炮手座上也不放武器（`vehicle_weapon_setting` 的座位号全是 0 或 -1）。炮手的炮是插件的炮弹（`jet_bay.cpp` 的 `CrewFire`，与 `GunshipFire` 同弹、同间隔、同射程） | 反汇编 + 生成器检查 | H |

生成：`tools/make_jets.py` 的 `with_gunner_seat` 从本机的 VEHICLE410_HELI.SGO 复制门炮手座的 [3]–[6]（姿势、掩码、数字、按键行，先核对是 `410_HELI_GUNNER_L` / 6），拼在驾驶座的 [0]–[2] 后面；`check_gunner_seat` 回读检查（两个座位、定位点相同、掩码 15、按键行 6、没有武器挂在炮手座、标记 7011）。`tools/selftest.py gunship_gunner_seat` 用合成 SGO 测这两个函数（含拒绝的情况）以及 `crew.h kGunnerSeat`、ini 键的接线。离线生成：`make_jets.build(游戏目录)` 写到 `tmp/` 检查过，除 `vehicle_riding_position` 外其它成员与原来逐个相等。

### 11.2 上车与座位

- 上车键仍走 `crew.cpp FindSeatHook`。炮舰机（`GunshipCrewSeats`：炮舰机体、≥2 个座位、`PlayerJetAll`）且 `PlayerJetBoardable` 时，先走 `GunshipSeat`：按 `GunshipBoardSeat`（ini `GunshipBoardGunner`，按住 `GunshipGunnerKey` 取另一个）选座位；原版 FindSeat 会取第一个空座位（NPC 在飞时就是炮手座），所以必须先选。选中的座位上有 NPC 时用原有的 `Bump`：另一个座位空就挪过去（驾驶员去炮位、炮手上驾驶座），否则踢掉——所以任何时候只有一个驾驶员。然后照原版 FindSeat 命中时的做法（0x633BF5：`0x633FE0(veh, human, seat)` 预约并返回该座位）。可乘判定用原有的「隐藏 NPC + 自己的队伍」（`WithDummiesHidden` / `WithTeamField`）。
- 玩家坐驾驶座：同 §10，`PJet` 的整条路径。炮手座上有 NPC 时 `CrewGunner` → `jet::CrewShell`：`PickTarget` 以炮舰机自己为中心、1800 m 内选地面目标，炮好了就打一发（`ResumeNpc` 交回时会清掉这个目标）。空中下机照 §10.4（接机或 `HandBack`：驾驶座空则原版 RideAi 坐一个新的 NPC），炮手留着；`JetReap` 现在踢掉所有 NPC 座位再删除，任何座位上有玩家就不删。
- 玩家坐炮手座（`GunnerFrame`，在 `PlayerJetFrame` 开头、`Held` 之前）：呼叫下来在等的（hail）或停着等玩家的（keep）立刻 `HandBack`（驾驶座空则 RideAi，`ResumeNpc` 在地面上转 takeoff）；之后由 `jet.cpp JetFrame` 的 NPC 飞行照常飞，只是：`GunnerHold`（这一帧的时间加到 `bornAt`，燃料与出击计时停住；正在撤离的取消）、不问 `Leave`、不开 NPC 自己的炮、锚点 = `GunnerAnchor`（玩家 30 s 内打中的地面点 → 呼叫的标记 → 上机点；跟随型不再把锚点拉到 `player.pos`，因为玩家就在机上）。盘旋是原有的 `Patrol`：切向 (out.z, 0, -out.x)，中心在航迹左侧。驾驶座若空，`EnsurePilot` 用原版 RideAi 补一次（失败只记日志，不重试）。
- 炮手的扳机：座位 `+0x2E4 ≥ 0.8`（所有载具的主射击约定，heli-input-re.md §3）；瞄准点 = 屏幕中心视线与地面的交点（`CameraRay` + `MapRay`，3000 m 内）；`jet::PlayerShell` 开炮（共用一门炮的间隔）。HUD：`hud.cpp GunnerMarks`（黄色落点十字、超射程红色、距离与 READY / 装填秒数、青色方框 = 盘旋中心），只在玩家不在驾驶座时画。
- 炮手下机（`GunnerLeft`）：离地 15 m 以上用 §7 的弹射跳伞（`EjectStart`，`bail.mark = 0`：不接机）；驾驶座有 NPC 且队伍不是友军 2 时 `SetObjectTeam` 回 2（玩家上车时原版可能把载具改成玩家的队伍，呼叫键只找友军队伍的飞机）。炮舰机不见了（删除、残骸消失）由 `GunnerTick` 每帧检查。
- 炮手在机上时 `hud.cpp HudSee` 不在这架上方画 NPC 载具读数。

### 11.3 验证状态

- 静态：`build.cmd` 无警告（/W4 /WX）；`tools/selftest.py` 全过（新增 `gunship_gunner_seat`）；离线生成炮舰机 SGO 并回读检查通过。
- 未实机验证（需要在游戏里逐项看）：
  1. 新 SGO 能否正常生成载具（第二个座位共用定位点、`seat+0x258` = 0）、炮舰机呼叫照常飞（日志 `CREW` / `JET ... crewed: gunship`、`VEH ... seats=[d-]`）。
  2. 上车：默认坐驾驶座且原驾驶员挪到炮手座（`BUMP ... npc moved to gunner seat 1`、`BOARD ... pilot seat`）；按住 V 坐炮手座（`BOARD ... gunner seat`），NPC 留在驾驶座；`GunshipBoardGunner=1` 反过来；上车提示是否出现；三种职业（掩码 15 的姿势 `410_HELI_GUNNER_L`）能否坐炮手座、姿势是否正常。
  3. 炮手座上的镜头：能否用鼠标 / 右摇杆自由转动、`CameraRay` 的屏幕中心与黄色十字是否一致；按键行 6 下键鼠的主射击（`seat+0x2E4`）是哪个键（日志 `GUNNER ... trigger 1.00 (keys)`）；按住射击是否每 2.5 秒一发、炮弹是否落在十字上。
  4. 炮手在机上时：呼叫下来停在地面的那架能否交回 NPC 并起飞（`PJET ... back to its NPC pilot: the player took its gun`、`JET ... takeoff` → `patrol`），之后绕打中的点盘旋（目标在左侧、约 600 m、350 m 高）；燃料是否停走（下机后 `JetHud` 的剩余燃料）、不撤离、不被删除；RideAi 在玩家已在炮手座时把载具队伍改成 2 是否有副作用。
  5. 玩家驾驶时炮手 NPC 是否自动开炮（`JET ... gunship shell #n from its NPC gunner`），与自己的 SHELLS 是否共用间隔。
  6. 下机：炮手在空中下机是否正常跳伞（`PJET ejected`、`CHUTE made`）、炮舰机继续执行呼叫；驾驶员在空中下机时炮手是否留下、接机 / 交回是否正常；在地面下机后再上（`keep` 时炮手 NPC 挪到驾驶座）。
