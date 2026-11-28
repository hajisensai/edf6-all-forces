# 载具手（兵种 4，`ZwVehicler`）第 2 阶段设计

范围：`docs/plan-2026-11-28-new-classes-impl.md` §3 的第 2 阶段，包括开局坐车入场、后勤点补车、V1–V3、陀螺喷射手枪定稿。
需求原文见 `docs/plan-2026-11-28-zero-worldline-gear.md` §1。

- EDF.dll Steam `0x678CCB46`，地址都是 RVA。
- 置信度：
  - [确认]：汇编核过。本轮直接核过的，或仓库代码 / 笔记已核过、本文引用的（标「引」）。
  - [推断]：由调用关系或现有代码推出，没逐条核。
  - [猜测]：没有直接证据，只是方向。
- 本轮只读：没开游戏，没改 src。

## 0. 结论一页

1. **载具槽**
   - 不动 `tools/calls.py` 的 `CALLS`。那是空降兵 308 类呼叫武器的登记表，载具手的车不是呼叫武器。
   - 在 `tools/zw_weapons.py` 的 `WEAPONS` 末尾追加 400 类行，每辆车一行。行本身是惰性的 Weapon_Accessory，只当「选了哪辆车」的标签。
   - 车体是插件自己的 `EDF6VC_ZW_*` 载具 SGO，要求 `mission_setup == vehicle_setup`。
   - 行到车体的对应由生成表 `src/zw_vehicles.inc` 给出，插件按表直接造车（support_spawn 的 MakeHull 路线）。
   - zw_weapons 有自己的 RELEASED 只追加规则，照它做就行，calls.py 的规则不受影响。
2. **开局坐车**
   - 不钩 0x5A3F90，outfit41 已经改了它的两个调用点。
   - 由本机玩家的载具手对象满足条件后触发（第 2.2 节）。
   - 每个玩家的车由房主统一规划位置（避免几辆车出生时互相重叠），各机用同一个派生 NetId 造同一辆车体。
   - 车主机器上直接调 RideVehicle `0x5765E0`，再调广播 `0x5763E0`。别的机器靠原版骑乘消息 `0x5775F1` 重放。
3. **后勤点**
   - 不用原版 `0x10000006` 击杀计数：它漏掉载具击杀，而且只认本地 pad。
   - 改在插件已有的伤害钩子 `0x547C30` 上加击杀订阅，只在结算那台机器上触发。
   - 击杀归到「玩家本人」或「玩家驾驶 / 乘坐的车」，记到该玩家的槽位号上。
   - 房主是唯一记账方，非房主结算的击杀报给房主，房主按被击杀对象去重。
   - 满了由车主按键呼叫，走 support_dispatch 的「空车交付」：地图边缘入场、真司机、停车下车，车留给车主。
   - HUD 用插件 HudDraw 自己画一条后勤条，不借原版 pt 也不借能量槽。
4. **下车状态**
   - 手里只有陀螺喷射手枪。400 行是 Weapon_Accessory，不进手持列表 [推断，实机核]。
   - 保留翻滚。关掉游骑兵冲刺（ダッシュ），先用兵种 SGO 参数关，不行再在克隆 vtable 里关（第 4 节）。
   - 游骑兵的其它装备效果来自附件槽，载具手没有附件槽，自然不存在。
5. **拆分**
   - 共 5 块：
     - M1 数据（只动 Python 与资源）
     - M2 造车与调度
     - M3 开局入座
     - M4 后勤点、HUD 与补车
     - M5 下车限制
   - 文件互不重叠，接口头文件由 M2 先落。验收见第 5 节。

## 1. 载具槽：400 行怎么表示「这辆车」

### 1.1 现有「载具武器行 → 车」的管线（只作对照，不套用）

- `tools/calls.py`
  - `Call(id, mark, kind, follow, count, reload, level, brings, vehicle=, ground=…)` 是空降兵的呼叫武器。
  - 追加规则在三处：文件头 11–15 行；`RELEASED` 前缀校验在 :410 附近，selftest 校验；:229 的注释。
- `tools/call_weapons.py`
  - 载具请求行克隆 `VEHICLE_TEMPLATE`（eWeapon394）或 `GROUND_VEHICLES[g].request` 的行。
  - SGO 的 `Ammo_CustomParameter` 第 [4] 项是 `[运输方式, 箱子, 载具路径, setup, 语音]`。生成器把 [2] 改写为 `app:/object/<veh>.sgo`，并换掉 setup 里的武器。
  - `plan_rows`（:284/:298）的规则：已装的行不动，新行按 `IDS + pw.IDS + zw.IDS` 依次追加在末尾。
- `src/calls.inc` 的 `kCallRows {id, 武器 SGO, 载具 SGO}`：`airstrike.cpp` 的 CheckCallTable 在 `0xDC550` 之前给这些行置持有位 [确认 引]。
- 原版请求武器的运行时是「运输 → 载具出现」，由原版子弹 / 箱子逻辑驱动，不由插件控制入场方式。
- 载具手要的是：开局就坐在车里，补车由 NPC 开进来。这和原版请求武器的运行时不是一回事，所以 **400 行不是请求武器**，不走 Ammo_CustomParameter 那条链。

### 1.2 方案

**M1：数据**

1. `tools/zw_weapons.py`：`Weapon.kind` 加 `'vehicle'`，再加字段 `vehicle: str`（车的键）。
   - 在 `WEAPONS` 末尾追加，顺序固定：`ZW_V1_HSTVL`、`ZW_V2_M247`、`ZW_V3_MBT70`。全部是 category 400，模板是 `aSupportNone`。
   - 生成方式同 `kind == 'none'`：照抄模板行和 SGO，只改名字 / 说明 / 图标文本。
   - 不能有开火能力：Weapon_Accessory 本身不开火 [确认 引 zw_weapons、equip.md]。
   - `ACQUIRE=1`，和第 1 阶段一致：开局就有。之后改成缴获解锁属于第 4 阶段。
   - `RELEASED` 追加一条 `'vehicler stage 2 (…)': (8, sha)`。原来那 5 行不动，selftest 与 `tests/zw_classes_test.py` 校验前缀。
   - `ZW_VehicleNone` 保留，含义是「不带车，步行入场」。不要删，存档里可能正装备着它。
2. 车体登记：新文件 `pylib/zw_vehicles.py`。
   - 每辆车一条，复用 `vcobjects.GroundVehicle` 的数据类：sgo `EDF6VC_ZW_HSTVL` / `EDF6VC_ZW_M247` / `EDF6VC_ZW_MBT70`，stock 车体，weapons，durability，tool。
   - **不加进 `vcobjects.GROUND_VEHICLES`**。那张表的语义是「被 `Call.ground` 引用的请求车」，`call_weapons` 会按它造请求行 [推断：没逐一排查遍历它的测试]。另开一张表，免得多出请求行或被要求必须有 Call。
   - 底盘：
     - V1 HSTV-L、V3 MBT-70 用 `V505_TANK`，vtable `0x17DADB0`，1 座，和支援车体 `kSupportVehicles[0]` 同类 [确认 引 support_spawn.h]。
     - V2 M247 用 `V603_FLAK`，走 `tools/make_tracked_combat.py` 那套生成器，参考 `vulcan` / `gdf005` 的双联炮写法。它的 vtable / 座位数由生成器从 SGO 类名得出，写进生成表，不在 C++ 里手填。
   - setup：三辆车都要 `mission_setup == vehicle_setup`，先例是 `tools/mech_parts.py:421` 和 `edf41tank.py`。原因是插件造车走 `ApplyMissionSetup`（`0x62D6E0` 读 mission_setup → slot 46 应用 → `0x1765220` 析构）[确认 引 support_spawn.h:78]。只写 vehicle_setup 的车造出来会没有武器。
   - `make_tracked_combat.vehicle_sgo`（vcobjects.py:546 附近）只改写 vehicle_setup [确认 引]。V2 的生成器要补一步，把 mission_setup 写成同一份。
3. 生成表：新文件 `tools/gen_zw_vehicles.py` 生成 `src/zw_vehicles.inc`，支持 `--check`，CI 照 `gen_calls` 的方式核。每行：

   ```
   { L"ZW_V1_HSTVL", L"app:/Object/EDF6VC_ZW_HSTVL.SGO", L"EDF6VC_ZW_HSTVL.SGO", vtable, seats,
     halfWidth, halfLength, height, logisticsCost }
   ```

   - 包络尺寸照 `kSupportVehicles` 的做法：从 MDB 读，向外取整。
   - `logisticsCost` 是补这辆车要的后勤点（第 3 节）。

**插件读「这次带了哪辆车」**

- 本机：`wequip::ReadEquipped(player, 4, &slots)` 读 slot 0 的行号，`wequip::ReadRow` 读行名，再用 `zw_vehicles.inc` 查车。
  - GS 每名玩家留了 10 个兵种格（`weapon_equip.h` 的 `kClassesMost=10`、`count.md` §0），`GS+0x6E98+4*0x18` 是合法存储 [确认 引]。
  - wequip 写明只在 UI 线程调用，所以在 MissionStart / 预载阶段读一次，存成本任务快照（M2 的 `zw_vehicle_catalog`）。
- 远端玩家：不读对方的记录。车主请求造车时在消息里给出行名，房主用 `SenderIsPlayer` 核对是不是发送者自己的槽位。
  - 这和 `player_vehicle_logic.h:265` 一样：请求方机器决定自己的东西。
  - 车是车主自己装备的，房主不需要再按存档核一遍；作弊面和「玩家自己选武器」相同。

### 1.3 预载

- 每辆车是一个预载项。`src/preload_set_logic.h` 的 Item 加 `zwVehicle0..2`。
- `LocalNeeds()` 在本机有载具手、且 slot 0 是 V 行时加对应项。
- 联机用房主的 `p=` 并集 [确认 引 feature-2026-11-24-preload-set.md]。车体 SGO 预载后照惯例接 `QueueVehicleWeapons`。车在 `Mods/OBJECT` 里，会走 ScanSgo，不查原版表。
- 协议：这几个位改变了 `p=` 的含义。如果第 2 阶段和房间协议 27（模块 F）在同一版发布，就不用再加；否则 `kAllForcesRoomProtocol` 加 1（`mod_room.h:97`）。不写兼容分支。

## 2. 开局坐车入场

### 2.1 时机与调用链

- 建人链 [确认 引 spawn.md]：
  - 离线：`CreatePlayer → 0x1DC450 → 0x591410 → 0x595C80 → 0x5A3F90`
  - 联机远端：`0x591130 → 0x595960 → 0x591234 → 0x5A3F90`
  - `0x5A3F90` 内的顺序：`0xE34E0` 取 SGO → `0x11945E0` 造对象（`0x5A4065` 做 dynamic_cast）→ `0xDE250` 算 HP → `0xE3680` 取槽数 → `0xE1CF0 → 0x58F360` 挂武器。
- `outfit41.cpp` 已经把两个调用点 `0x591234` 和 `0x59145B` 都改走它自己 [确认 引]，所以**不在 0x5A3F90 上再挂东西**。
- 在 0x5A3F90 返回时也不能造车：这时任务对象世界和联机握手都没就绪，原版也不在那里造载具。
- 触发方式：新文件 `src/vehicler_start.cpp` 在每帧 tick 里（挂法同其它插件模块的 FrameTick）扫本机玩家，满足下列全部条件时发一次请求：
  - `ZwType(obj) == 4` [确认 引 soldier_factory.h:16]
  - 是本机对象：+0x128 bit0 为 0，pad +0x340 非空 [确认 引 heli.cpp / real_driver_native.cpp]
  - 活着；还没请求过：`soldier::Extension(obj)` 里记一位，按对象记，所以同一对象只请求一次
  - 任务环境不是地下：支援车体地下一律不出 [确认 引 support_spawn.h:42]。地下任务就步行入场，写一行日志
  - 本任务快照里 slot 0 是 V 行，且该车预载成功（M2 的 `ZwVehicleReady(kind)`）
  - 联机时 support_dispatch 的 peersReady / 握手已完成（沿用它现有的门，不另写等待）
- 不改 soldier_factory 的 slot 5：那是第 1 阶段模块 B 的文件。扫描一帧最多几个玩家，代价可以忽略。

### 2.2 谁造车、怎么同步

- 复用 `support_dispatch.cpp` 的房主权威框架（plan → prepare → commit）[确认 引]。M2 加一种计划 `ZwStart`：
  1. 请求：车主机器发 `{player slot, 行名}`。房主侧用 `SenderIsPlayer` 核对 [确认 引 command_identity.h:64]。
  2. 规划：房主按所有请求一起排位。
     - 每辆车以各自玩家的出生点为基准，沿玩家朝向的侧向排开，间距 ≥ 两车 halfWidth 之和 + 2 m。
     - 地面高度用 support 现有的落地射线取。
     - 位置被占（别的车、建筑）就往外挪一格，最多 N 格；挪不开就拒绝，那个玩家步行入场，写日志。
     - 原因：联机时几名玩家的出生点只差几米，每人原地造一辆会重叠 [推断]。
  3. 造车：每台机器用房主下发的矩阵和派生 NetId（`DeriveSupportSoldierNetId` 的同一套，support_soldier.cpp:464/480 [确认 引]）造车体，并 `RegisterSupportObject`。
     - 车体函数用 M2 新加的 `SpawnZwVehicle(const ZwVehicleSpec&, entry, heading, owner)`：MakeHull 的第三个入口，和 `SpawnSupportDrillTransport` 并列（support_spawn.cpp:190）。复用它的类 / 座位校验、MarkFirepower、ApplyMissionSetup、SetTeam(2)、SetLevel、NoteLocalCopy。
     - 不新增 `SupportVehicleKind`：加一种会让后面的支援目录序号全部移位。钻地运输车的先例（support_spawn.h:48–56）就是另开 plan resource，`0x20100`。这里用 `kSupportVehicleResource + 0x200 + 车序号` [确认 引 support_call.h:58]。
  4. 没有司机：`ZwStart` 是空车，不经 GroundNavigate，造在终点，车主本人就是驾驶员。
- 入座只在车主机器上做：
  1. 给车加一个引用，调 `RideVehicle 0x5765E0(human, shared_ptr<vehicle>*, seat=0)` [确认 引 npcai.cpp:1568–1574]。
     - 它不查队伍、兵种掩码、距离，会处理「下旧车」以及 +0x1540 / +0x1548 / +0x1550、SeatRide、骑乘状态，并在 `0x57690D` 释放一个强引用，所以调用方先加一个。
  2. 再调 `0x5763E0(human)` 广播。插件注释说它「没有只限玩家的门：任何本机已登记的 Human，发出载具引用 id、座位号、++rideSequence」[确认 引 real_driver_native.cpp:91]。
  3. 别的机器收到 `HUMAN_EVENT_VEHICLE_RIDE`，在 `0x5775F1 → 0x577661 call 0x5765E0` 重放，同样不查兵种 [确认 引 npcai.cpp:1598、allclass.cpp:26–29]。
  4. 兵种座位规则 `0x5DF770` 对 type 4 一律放行 [确认 引 architecture.md:17]。
  5. 不走按键上车 `0x56D700` / `BoardingRequest`：它要玩家在座位触及范围内，还受 +0x128 bit0 / +0x5D0 bit2 / +0x39C 三道门限制 [确认 引 heli.cpp:1691]，开局造在身边也可能差几米。直接 RideVehicle 不受距离限制，正是想要的。
- 风险 [推断]：插件至今只对 NPC 调过 0x5765E0 + 0x5763E0，玩家走这条路没实机验过。
  - 若玩家的摄像机 / HUD 没切到载具，改成上车点 `BoardingRequest(v)`：先把人放在上车点再按键，boarding.cpp:245 的 PressOn 就是这么做的。
  - 这是唯一的备选，要实机二选一，不两条都留。
- 上车音效：照 `QuietRide` 去掉开局那声上车音（npcai.cpp 内 seatevt::Quiet），可选。
- 原生签名：0x5765E0 头部 16 字节（`kRideSig`）、0x57690D（`kRideReleaseSig`）、0x5763E0 头部，在 M3 自己的 `Install` 里核，不符就写 `VEHICLER start off` 日志、不装。
  - 签名常量从 npcai.cpp 挪进一个小头文件 `src/ride_native.h`，两边共用。挪的只是常量，npcai.cpp 改一行 include。这一行归 M3。

### 2.3 归属

- 车主的车：驾驶座只给车主。
  - NPC 选座（npcai.cpp 的座位候选）跳过「`zw_owner` 记录里属于别的玩家的车」的驾驶座，炮手座、乘员座照常。
  - 别的玩家不拦：原版按键上车由对方机器决定，插件不加跨机的拒绝，[猜测] 实际影响很小。
- 车主下车后车留在原地，是一辆普通的友军车。车毁了也不自动补，补车走后勤点。

## 3. 后勤点补车

### 3.1 击杀来源

- 不用原版 `0x10000006`：
  - `0x547C30` 在 HP 归零时给攻击者发这条消息，`0x598FD0` 收到后要 soldier+0x340（pad）非空才走 `0x599101 → 0x5499F0 → 0xCF640` [确认 引 kill-counter-re.md]。
  - 载具武器的 owner 是车（SetWeaponObject `0x633330`），所以车打死的敌人到不了士兵 [确认 引 gauge.md §5]。
  - 载具手的击杀主要来自车，用这条消息就漏了主要来源。
- 改用伤害钩子：`src/damagestats.cpp` 已在 `0x547C30` 头部 detour，记下调用前后的 HP 与死亡位。
  - 击杀条件：`!deadBefore && obj[+0x2E8] != 0`，排除会自己写 +0x2E8 的部件类。
  - 攻击者：GDI+0x10，控制块 +0x8 > 0。
  - 弹体按 SpawnHook 打的标签归到发射者，车上武器归到持有它的座位乘员（`VehicleSide` / `SeatRider`，damagestats.cpp:118–135）[确认 引]。
  - M4 在这里加一个回调 `OnKill(victim, attacker, weapon)`。damagestats.cpp 只加调用点，不改记账。
- 在哪台机器上触发：伤害只在受击对象的权威机器上结算。
  - 会话中 `0x547E1D..0x547ED9` 问 slot 35 / 34 这台机器是否拥有该对象。非权威机器只给攻击者发 `0x10000007`，并在 `0x548625` 返回，HP 不变 [确认 引 host_settings_logic.h:8–10、damage-numbers-re.md §5]。所以**每次击杀只在一台机器上触发**。
  - 敌人通常由房主拥有 [推断]，但插件或任务刷出的敌人可能归客人。
  - 方案对两种情况都成立：结算的那台机器把攻击者解析成「哪名玩家」：
    - 攻击者是玩家士兵：就是他。
    - 攻击者是车：取持有该武器的座位乘员，没有武器信息时取驾驶座，要求是玩家（`edf::SeatRider == player` 或 `AnyPlayerIn`）。
  - 解析出来后用 `EncodeNetRef` 记成该玩家的槽位号（net_object_resolve.h）[确认 引]。
  - 只给兵种 4 的玩家记：本机玩家看 `ZwType`；远端玩家副本也是克隆 vtable，`soldier_identity` 能认出 4/5（commit c112494f）[确认 引]。
- 客人打房主拥有的敌人时，房主侧看到的攻击者是不是客人的车 / 人的副本、弹体标签能否在远端副本上打上，都**没核实**（damage-stats-re.md:109 已标 L）。必须实机核，见第 5 节 M4。
  - 若房主侧攻击者为空，根因修法是在客人机器上用它发出的 `0x10000007`（非权威端「告诉攻击者击中」那条，`0x547F97`）做本机候选，房主收到后以对象死亡为准确认。
  - 不能改成「谁看到死了就记」，那样会重复计数。

### 3.2 记账：房主唯一

- 新通道 `vehicler logistics`，magic 自选（如 `'ZWLG'` = `0x474C575A`），在 `support_net.cpp` 的 `RegisterPluginChannels`（:192）里注册。现在 24 个槽用了 18 个 [确认 引]。
- 消息：
  - `credit {victim NetRef, player slot, 分值}`：非房主结算方 → 房主。房主结算的直接本地记。
  - `state {每个兵种 4 玩家: slot, 点数, 上限, 在途补车 id}`：房主 → 全体。广播变化，`joined` 时发全量。
  - `call {player slot, 行名}`：车主 → 房主，用 `SenderIsPlayer` 校验。
- 房主处理：
  - 按 victim NetRef 去重，存本任务见过的集合，任务开始清空。同一对象只记一次，防联机重复上报。
  - 分值 = `Credit(victim)`，在纯函数里算（`vehicler_logistics_logic.h`）。初版每个本体击杀 1 点，部件不算。按敌人大小分档留到调数值时再定，接口先留好。
  - 上限是当前选的车的 `logisticsCost`（来自生成表）。到上限就停，不溢出。
- 补车扣点照 SPTS 的 Hold / Settle / Refund：
  - 房主收到 call 先 Hold（点数扣进冻结）。
  - 交付计划 commit 成功就 Settle。规划失败、找不到入口、车体造不出就 Refund，并给车主发状态文本。
  - 参考 `src/support_points.cpp` 的状态机，不复用它的团队池：后勤点按玩家算，SPTS 是团队池，语义不同。
- 客人一侧只显示房主下发的值，不自己累计。离线就是本机当房主，走同一套逻辑。

### 3.3 补车怎么来

- 地面「空车交付」复用 `support_dispatch.cpp` 现有逻辑：
  - `GroundEntryCandidates`（support_entry.h:136，650 / 800 / 950 m 环加地图边界交点，离目标至少 600 m）选入口。
  - 真司机开车：`GroundNavigate` 跨帧 A* + `NpcPrepareVehicleRoutePost`。
  - 到了停稳（speed ≤ 0.5 或卡住 `kStuckMs`），`NpcReleaseVehicleCrew` 让司机下车（support_dispatch.cpp:2220–2228）[确认 引]。
- M2 加计划 `ZwDeliver`：车体用 `SpawnZwVehicle`，司机是一名支援士兵，终点是车主当前位置附近一个可停车点。
  - 和现有空车交付的唯一区别在到达分支：现在是 `FollowSupportSoldier(司机, 玩家)`（:2225–2228）；ZwDeliver 改成「司机跟随车主，车登记为车主的车」（第 2.3 节）。
  - 再给车主发提示「补给车已到」，车主走过去按键上车。自动入座不做：车主可能在跑、在打，突然瞬移进车不对。
- 直升机吊运 / 降落交付推迟到第 4 阶段，和 V6 AH-56 一起做。
  - `transport.cpp` 已有悬停 18 m 绳降、着陆作后备的逻辑（HeliGrounded :155），但吊运一辆车是新东西，不在本阶段。
  - 需求原文里的「直升机降落」本阶段**不做**，要在计划里写明。
  - 不做空投：需求原文否了。airdrop.cpp 也只支持离线。
- 地下任务不补车，规划阶段直接拒绝，原因是车体不许进地下，写日志并 Refund。
- 并发：每名玩家同时最多一次在途补车（state 里记在途 id），交付完成或失败才能再叫。

### 3.4 HUD

- 不用原版能量槽：`gauge_setting[0]=1` 会出翼装能量槽的样子（`0x7FE5D0 → 0x7FDA40 → 0x82E170`，`0x82E3E7..0x82E3FC` 选 `lyt_HudEnergyGuage_R`，读 +0x304 / +0x308）[确认 引 gauge.md §3]。那个位置留给刻印剑士的以太槽；载具手的值不在士兵对象上，写进 +0x308 还会和能量充能武器的扣能互相影响。
- 用插件 HudDraw [确认 引 gauge.md §6、hud-re.md]：
  - GaugeHook 把 `0x8040E0` 内的调用改到 `0x804300` 再调 HudDraw。方块用 `0xC2FB0(drawer=*(EDF+0x2139A78), …)`，文字走游戏字体链。
  - 游戏线程写快照（seqlock），绘制线程读。
  - 画法：一条横条加「后勤 23 / 30」，位置在右下武器槽上方。满了换色，并显示按键提示（ini `VehiclerLogisticsKey`，手柄另配）。在途时显示「补给车接近中 nnn m」。
  - 文本放进 `src/hudtext.inc`，五种语言。离线用 `tools/hud_view` 预览。
  - 只在本机玩家是兵种 4 时画。车里车外都画。

## 4. 下车状态

- 手持只有手枪。400 行是 Weapon_Accessory 克隆。原版附件（游骑兵 slot 3 一类）挂在附件位、不进切换列表 [推断：由「附件不能手持」这一游戏行为推出，`0x58F360` 按类别分流没逐条核]。所以手持列表 +0x1950 只有 401 一把，不用额外锁切换。
  - 实机核：按切换键不会切出空手 / 空武器。若会切出来，M5 在克隆 vtable 里把切换钳到手枪，不改原版。
- 翻滚保留。翻滚是 AssultSoldier 的 CAS 状态，载具手以 AssultSoldier 为底已经自带 [确认 引 motion.md:274、architecture.md:28]。
- 游骑兵冲刺：
  - 代码 [确认 引 motion.md §3.5]：slot 46 `0x5508E0` 在冲刺状态 `0x552040` / `0x5528C0` 时返回 `1 + (速度倍率 − 1) × 属性 0x68`，`0x552040` 读属性 100 / 0x65 / 0x66。冲刺的含义是 [推断]。
  - 需求是「下车除翻滚外不要特殊动作」（zero-worldline-gear §1），**关掉**。
  - 第一步，纯数据：兵种自己的 `zw_vehicler_0N.sgo` 把冲刺相关属性（100 / 0x65 / 0x66 / 0x68）写成「倍率 1、不可冲刺」的值。工具是 `tools/make_zw_classes.py`，第 1 阶段模块 C 的文件，本阶段归 M5。
  - 第二步：实机看。若仍会进入冲刺状态（动作还在、只是不加速），在克隆 vtable 里拦进入冲刺的那个入口。入口 RVA 本轮**没定位**，要逆 `0x552040` 的调用者，这是 M5 的第一项工作。不要用让 slot 46 恒返回 1 来掩盖，那样动作照样会播。
- 其它游骑兵专属效果：游骑兵的拾取范围、救援等能力来自附件槽的装备参数（`0x592940` 经 dynamic_cast Weapon_Accessory 读 custom_parameter）[确认 引]。载具手没有附件槽，400 行的 SGO 是惰性克隆（custom_parameter 为空），这些效果自然没有，不用写代码关。
- 第 2 阶段不做步行状态的其它改动，比如移动速度、跳跃，都按 AssultSoldier 原值。

## 5. 模块拆分与验收

依赖顺序：M1 → M2，并且 M2 先提交接口头文件 `src/zw_vehicle_dispatch.h`、`src/zw_vehicle_catalog.h`。之后 M3 和 M4 并行；M5 和其它块互相独立。
实机部分按项目规则，最后由父会话用一次 `gamequeue` 启动统一采，子代理不自己排队。

### M1 数据（只动 Python、资源与生成表）

- 文件：
  - `tools/zw_weapons.py`：V 行、Gyrojet 定稿数值、RELEASED。
  - `pylib/zw_vehicles.py`（新）。
  - `tools/make_zw_vehicles.py`（新）：V505 系两车；V2 调 `make_tracked_combat`，只经它的公开函数，不改它的文件。如果非改不可，归 M1。
  - `tools/gen_zw_vehicles.py`（新）→ `src/zw_vehicles.inc`。
  - installer / build_release 接线。
  - `tests/zw_vehicles_test.py`（新），并扩 `tests/zw_classes_test.py`。
  - 归属登记。
- 离线验收：
  - RELEASED 前缀和 sha 正确，旧 5 行位置不变。
  - V 行是 category 400，模板是 aSupportNone，custom_parameter 为空。
  - `gen_zw_vehicles.py --check` 通过。
  - 每个车体 SGO 满足 `mission_setup == vehicle_setup`，类名 / 座位数和生成表一致。
  - `vehicle_weapons` 的扫描能扫出每辆车的武器文件。
  - Gyrojet：6 发，弹速曲线递增（读回 SGO 断言）。
  - `python tools/test_domains.py --write`。
- 实机：三辆车的模型、炮塔、开火、伤害量级；Gyrojet 的手感（慢出膛后加速、命中小爆炸）。

### M2 造车与调度（房主权威）

- 文件：
  - `src/zw_vehicle_catalog.{h,cpp}`（新）：生成表、本任务快照、预载状态。
  - `src/zw_vehicle_dispatch.h`（新）：对 M3 / M4 的接口，`RequestZwStart`、`RequestZwDeliver`、`ZwVehicleOwner(v)`。
  - `src/support_dispatch.cpp`：ZwStart / ZwDeliver 两种计划、resource `0x20200+i`。
  - `src/support_spawn.{h,cpp}`：`SpawnZwVehicle`、预载。
  - `src/preload_set_logic.h`、`src/preload_set.cpp`：三个 Item。
  - `src/support_dispatch_logic.h`（若计划校验在那里）。
  - 测试：`tests/zw_dispatch_test.cpp`（新）、`tests/preload_set_test.cpp`，扩 `tests/support_spawn*`。
- 离线验收：
  - 开局排位纯函数：4 名玩家出生点相距 2 m 时车不重叠，挪不开时拒绝。
  - plan 的线上往返；非车主的请求被拒（SenderIsPlayer 假桩）。
  - ZwDeliver 到达分支不调 `FollowSupportSoldier(…, PlayerHuman)` 给别的玩家。
  - preload_set：Always ⊂ All、RoomSet 包含 zw 项。
  - 支援车体的原生审计照现有 `support_spawn` 用例扩上 SpawnZwVehicle。
  - 若协议号变了，`player_identity_guard` 跟着改。
- 实机：离线任务和双机联机里，车出现在正确位置，各机同一个 NetId（日志对得上）；补车从地图边缘开进来、停下、司机下车；地下任务拒绝并写日志。

### M3 开局入座与归属

- 文件：
  - `src/vehicler_start.{h,cpp}`（新）、`src/vehicler_start_logic.h`（新）。
  - `src/ride_native.h`（新，签名常量）。
  - `src/npcai.cpp`：改一行 include，加驾驶座归属过滤，调 `ZwVehicleOwner`。
  - 模块表里 tick 和 install 的那一行。
  - `tests/vehicler_start_test.cpp`（新）、`tests/vehicler_start_native_audit.py`（新）。
- 离线验收：
  - 触发门的纯逻辑：非本机、非 4 号兵种、已请求、地下、未预载、ZW_VehicleNone，都不发请求。同一对象只发一次。
  - 原生审计读 EDF.dll，核 `0x5765E0`、`0x57690D`、`0x5763E0`、`0x577661` 的字节和 RideVehicle 的参数形态。
- 实机（关键）：
  - 离线开局人就在驾驶座，摄像机和 HUD 是载具视角，能开、能开火。
  - 双机联机：两人各自在车里，对方屏幕上也看到对方在车里。
  - NPC 不抢车主的驾驶座。
  - 不行就按第 2.2 节换成 BoardingRequest，二选一。

### M4 后勤点、HUD、补车触发

- 文件：
  - `src/vehicler_logistics.{h,cpp}`（新）、`src/vehicler_logistics_logic.h`（新）、`src/vehicler_logistics_net.h`（新）。
  - `src/damagestats.cpp`：只加 OnKill 调用点。
  - `src/support_net.cpp`：只加通道注册一行。
  - `src/hud_vehicler.inc`（新），以及 hud 绘制入口那一处 include（hud.cpp）。
  - `src/hudtext.inc`：五语词条。
  - `tests/vehicler_logistics_test.cpp`（新）、`tools/hud_view` 的样例。
- 离线验收：
  - 记账纯逻辑：同一 victim 报两次只记一次；到上限截断；Hold → Settle / Refund 平衡；在途时第二次 call 被拒；任务开始清空。
  - 线上往返，坏长度 / 坏 magic 拒收；非本人槽位的 call 被拒。
  - 攻击者解析纯函数：玩家本人、玩家驾驶的车、玩家坐炮手座时那门炮的武器归属，以及 NPC 车不算。
  - `hud_view` 截图核布局与满格样式。
- 实机：
  - 离线时手枪击杀、车炮击杀都加点；部件不加。
  - 满了按键呼叫，HUD 显示在途距离，到达后提示。
  - **双机联机：客人开车打房主拥有的敌人，房主账上要加到客人的槽位**（第 3.1 节未核的那一点）；客人 HUD 和房主下发的值一致。

### M5 下车限制

- 文件：
  - `tools/make_zw_classes.py`：兵种 SGO 的冲刺参数。
  - `src/vehicler_foot.{h,cpp}`（新）：只在需要拦冲刺入口时才写。
  - `tests/zw_classes_test.py` 不归这里：它是 M1 的文件，M5 要断言的 SGO 参数写进新的 `tests/zw_vehicler_sgo_test.py`。
- 离线验收：
  - 兵种 SGO 里冲刺属性取值正确。
  - 若写了 vtable 拦截，原生审计核入口字节。
- 实机：下车只有手枪，切换键切不出别的；翻滚可用；冲刺键没有冲刺动作和加速。

### 文件归属核对（不重叠）

| 文件 | 归属 |
|---|---|
| `tools/zw_weapons.py`、`pylib/zw_vehicles.py`、`tools/make_zw_vehicles.py`、`tools/gen_zw_vehicles.py`、`src/zw_vehicles.inc`、`tests/zw_classes_test.py` | M1 |
| `src/zw_vehicle_catalog.*`、`src/zw_vehicle_dispatch.h`、`src/support_dispatch.cpp`、`src/support_spawn.*`、`src/preload_set*` | M2 |
| `src/vehicler_start*`、`src/ride_native.h`、`src/npcai.cpp`、模块表 | M3 |
| `src/vehicler_logistics*`、`src/damagestats.cpp`、`src/support_net.cpp`、`src/hud_vehicler.inc`、`src/hud.cpp`、`src/hudtext.inc` | M4 |
| `tools/make_zw_classes.py`、`src/vehicler_foot.*`、`tests/zw_vehicler_sgo_test.py` | M5 |

`CMakeLists.txt`、`cmake/test_labels.cmake`、`tests/test_labels.json` 每块都要加行，会撞。按项目惯例，最后合并时各块在自己的提交里只追加自己的行，冲突时按追加合并。这是唯一的共享点。

## 6. 未决与风险

1. 玩家走 `0x5765E0 + 0x5763E0` 直接入座，摄像机 / HUD / 输入是否正常切换没实机验过 [推断]。备选是 BoardingRequest。
2. 联机击杀在房主侧的攻击者解析没核（第 3.1 节）。若不成立，要走 `0x10000007` 那条客人本地候选路线。
3. 冲刺入口 RVA 未定位（第 4 节）。
4. V3 MBT-70 的液压悬挂升降：本轮没调研，原版没有对应机制可借 [猜测]。建议第 2 阶段 V3 先不带升降交付，升降单独逆向立项。不要在本阶段做半成品开关。
5. 直升机交付推迟到第 4 阶段，和需求原文「直升机降落」有出入，需在计划里写明。
6. 「补车时旧车怎么办」本设计选择不处理（旧车留作普通友军车），没问过用户。若要「替换」语义（旧车消失），在 M2 的 ZwDeliver commit 时删旧车，但要先确认用户意图。

## 7. 地址速查

| RVA | 作用 | 置信 |
|---|---|---|
| `0x5A3F90` | 建玩家士兵（SGO / 造对象 / HP / 槽数 / 挂武器） | 确认 引 spawn.md |
| `0x591234` / `0x59145B` | 联机 / 离线调用 0x5A3F90 的点（outfit41 已改） | 确认 引 outfit41.cpp |
| `0x58F360` | 挂武器到 +0x1950 | 确认 引 equip.md |
| `0x11945E0` | 造对象（mgr `[0x20B2958]`） | 确认 引 support_spawn.cpp |
| `0x62D6E0` / slot 46 / `0x1765220` | 读 mission_setup / 应用 / 析构 | 确认 引 support_spawn.h:78 |
| `0x7A3780` | 资源预载（组 2） | 确认 引 |
| `0x17DADB0` | V505 战车 vtable | 确认 引 support_spawn.h:27 |
| `0x5765E0` / `0x57690D` | RideVehicle / 其末尾释放引用 | 确认 引 npcai.cpp:1569 |
| `0x5763E0` | 骑乘广播（任何本机 Human） | 确认 引 real_driver_native.cpp:91 |
| `0x5775F1` / `0x577661` | 远端骑乘消息重放 | 确认 引 npcai.cpp:1598 |
| `0x56D700` | 按键上车（三道门） | 确认 引 heli.cpp:1691 |
| `0x5DF770` | 座位兵种掩码，type 4 全放行 | 确认 引 architecture.md:17 |
| `0x547C30` | ApplyDamage（插件已 detour 头部） | 确认 引 damagestats.cpp |
| `0x547E1D..0x547ED9` / `0x548625` | 权威判定 / 非权威返回 | 确认 引 host_settings_logic.h:8 |
| `0x547F97` | 非权威端给攻击者发 0x10000007 | 确认 引 damage-numbers-re.md §5 |
| `0x598FD0` / `0x599101` / `0x5499F0` | 原版击杀计数（不用） | 确认 引 kill-counter-re.md |
| `0x633330` | SetWeaponObject（载具武器 owner = 车） | 确认 引 gauge.md §5 |
| `0x7FE5D0` / `0x82E170` / `0x82E3E7` | 士兵 HUD / 武器槽 / 能量槽布局选择（不用） | 确认 引 gauge.md §3 |
| `0x8040E0` → `0x804300` / `0xC2FB0` | 插件 HUD 绘制入口 / 方块 | 确认 引 gauge.md §6 |
| `0x5508E0` / `0x552040` / `0x5528C0` | 游骑兵冲刺速度 / 冲刺状态 | 代码确认，语义推断 引 motion.md §3.5 |
| `0x592940` | 读装备 custom_parameter | 确认 引 |
