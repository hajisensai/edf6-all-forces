# 2026-10-10 支援入场：凭空出现与空降航线（support-arrival）

最新 nightly 90e8e85 实机反馈，用户原话：

> 「我叫的支援直接凭空出现了。」
> 「空降时外援飞来的路线不对，既然地图外飞来，直线之类的更合理的路线才对吧。」

证据来自游戏日志 `Mods/Plugins/EDF6VehicleCrew.log.1`（只读 grep），本机 ini：`BigWorld=6000`、`ViewDistance=3000`。

## 一、支援凭空出现

### 根因 A（主因）：“场外入口”其实在场地内，而且就在玩家眼前

- `src/support_entry.h`（旧版 :9、:25-30）`AirRoute` 的 8 个候选入口取**实测场地**四边中点 / 四角再内缩 `kEntryInset=30` 米。实测场地本身已比地面边缘内缩 `kVoidMargin=150` 米（`playarea.h` `Combine`；日志 `AREA ... 150 m inside its edge`），所以入口在地面以内 180 米。
- 与玩家的距离只要求 `kObserverClear=800` 米；近景相机画到 `FarClipZ`（每关 1000 米，`view.cpp` 按 ini `ViewDistance` 抬到 3000 米）。
- 日志 13:14–13:17 那一关（场地 x -1030..1508，z -1569..1269）：cat18 炮艇机生成于 (-1000,209,-150)，离玩家 898 米；cat32 运输机同一点 894 米；cat30 935 米；cat15 941 米。全部在绘制距离内。
- `airstrike.cpp PlanAirSupport` 还要求每个编队机位都在实测场地内（旧 :554），等于把飞机钉死在地面上空。
- 文档 `support-deployment.md:7`、`feedback-2026-10-09-support.md:106-111` 写的“场外”前提本身就是错的，已在原处加更正。

### 根因 B：远景渲染对所有支援机失效

- `src/jet_spawn.cpp FarRender` 只在 `*(v+0xE40) == image+0x176B9A8` 时打开远景位。日志每架支援机都打 `render node ... has vtable 00007FFB8DBC4030, not the model node's: far rendering off for it`，`far rendering on` 一次都没有。
- 静态逆向（EDF.dll 0x678CCB46，H 级）：`v+0xE40` 就是渲染节点，偏移没错，错的是校验。它是 `AnimationModel`（RTTI `0x17C4030`，基类 `umbra::Object@0`、`snapshot::IRecordable@0x70`）。构造函数 `0x6B8740` 先调 `umbra::Object` 构造 `0x11B2400`（写 vtable `0x176B9A8`），再在 `0x6B875D` 覆盖为自己的 vtable `0x17C4030`。`0x7FFB8DBC4030 - 0x17C4030 = 0x7FFB8C400000`，是 64 KB 对齐的基址，和日志对得上。原版也对 AnimationModel 调同一个开关（`0x37BCC2` 对 `+0x1160`，`0x5C3EC7` 对 `+0x5C0`）。
- 这跟机体换成 506（87380ce / 23b1037）无关；`0x6B8740` 在载具区段里只有 `0x62958A` 一处调用，原推导（063bf99，`docs/view-distance-re.md` 第 4 节）从一开始就是 M 级推断，错在没看到派生类覆盖 vtable。
- 另外，支援直升机不走 `JetFrame`，以前根本不会开远景。

### 根因 C：炮艇机生成后第一帧被瞬移

- 计划点 (-1000,209,-150)，0.03 秒后在 (-276,211,-150)，离玩家 454 米。当时移动区域还是地图自己的 1107×1313 米（中心 (252,-216)，13:14:47 才被 BigWorld 加宽）。
- 槽 55 的原版输入（基类 `0x6543A0`）每帧把直升机类机体夹回“移动区域按 `veh+0xE00` 收缩”的盒子里，并且直接瞬移（`docs/map-edge-re.md`）。`crew.cpp` 的输入钩子先调原版输入 `nextInput[I]`，**之后**才调 `HeliStep → JetFrame`（`crew.cpp:793` 对 `:821`），所以 `JetFrame` 写的 `kNoInset` 要到下一帧才生效，第一帧必定被夹一次。直升机从来没有清过 inset，场外生成后每帧都会被夹回。

### 根因 D：地面支援在玩家看得见的地方生成

- `support_entry.h GroundEntryCandidates`（旧 :62-90）在目标周围 650/800/950 米圈上生成候选，离玩家只要求 800 米，不管玩家看不看得见。路线规划器只覆盖起点周围 ±1024 米（`ground_navigation.h`），所以地面入口不可能像空中那样放到绘制距离外。

### 修法

- **A**：`AirRoute` 重写（`support_entry.h`）。围绕目标 16 个方位，每个方位上入口离目标的距离在 [地面边缘 + `kOffMap`(200 米), 物理正方形边] 区间内取：最近的一个“离玩家 ≥ 绘制距离 + 200 米”的点；整个区间都做不到时取离玩家最远的点（`Route::beyond=false`，日志会写明）。先选超出绘制距离的，其中航向最接近“玩家→目标”方向的；都超不出时选离玩家最远的。地面边缘外的航线采样没有地面（虚空），不再当作拒绝理由。
  - 物理正方形 `ArrivalHalf()`（`crew.h`）= min(Havok 世界实际半宽, `WorldHalf`) − 300 米。Havok 半宽由 `bigworld.cpp` 记录补丁是否真打上（`HavokHalf`）：BigWorld 补丁失败时 Havok 仍是 ±3000，而 `WorldHalf` 会照抄 ini。这张图：BigWorld 补丁 `bounds=1`，Havok ±6000，物理正方形 ±5700；BigWorld 关闭时 ±2700。
  - 绘制距离 `NearDrawDistance()` = max(1000, ini ViewDistance)。
  - `PlanAirSupport` 的编队机位改为检查物理正方形，不再要求在场地内；成功时日志 `SUPPORT air entry ...` 打出离目标、离玩家距离和是否超出绘制距离。
- **B**：`FarRenderOn`（`jet_spawn.cpp`）同时认 `0x17C4030` 和 `0x176B9A8`，且 `node+0x10`（Umbra::Object*）为空时先不调（`0x11B3020` 不判空），下一帧再试。支援机在 `PrepareSupportAircraft` 创建时就开，没开成的由 `SupportAircraftFrame` 每帧补（直升机也有）。喷气机仍由 `JetFrame → FarRender` 每帧检查。
- **C**：`PrepareSupportAircraft` 在机体创建完、交还给调用方之前（也就是第一次物理步和第一次原版输入之前）记下原版 inset 并写 `kNoInset`，所有机器都写（远端副本同样不会被夹）。喷气机一直保持（`JetFrame` 照旧每帧写）；直升机由 `SupportAircraftFrame`（`HeliFrame` 最开头调用，任何机器、任何驾驶者）在它进入“移动区域按原 inset 收缩”的盒子后还原原 inset，此后和其它直升机一样（玩家可能驾驶它；`heli.cpp SoftEdge` 的 `HeldBox` 读 inset）。
- **D**：`GroundEntryCandidates` 增加每个方位与场地边界（内缩 30 米）的交点（在 950 米圈以内、且不与某个圈重合时），并按“玩家看不见”排序：超出绘制距离 + 200 米 → 玩家眼睛（+1.7 米）到车顶（+3 米）之间有地图遮挡（`MapRay`）→ 看得见。看得见的只作最后退路，日志 `in the caller's sight` 标出。

## 二、空降航线（cat32 运输机）

### 根因

- 生成点在机体软边界之外：飞控的软边界 = 实测场地再缩 `AirSoftEdge`=600 米（`jet_flight.cpp JetSoftBox`），规划用的是场地边界，两者不一致。`SoftEdge/KeepIn` 一出生就强制掉头（13:17:21 `past the soft edge at (-1000,-150) ... back in first`）。
- `Ferry`（旧 `jet_flight.cpp:787-803`）是追点：飞过点就进 `ferryOut`，要离点 ≥ 2.2 个转弯半径（≈1240 米）才回头，小图上做不到，13:17:32–13:19:20 在东北角绕了近 2 分钟。转弯半径 564 米大于离点距离时擦过：最近只到 288 米。
- 跳伞 `transport.cpp`（旧 :37 `kDropRadius=400`、:331 `JumpNow`）只看 400 米之内就开始：伞兵落在 (135..261, -764..-497)，投放点 (-115,-509)，偏东 250–400 米。

### 修法

- 规划：运输机的 `AirRoute` 带上转弯半径（`SupportPassTurn` → `jet_flight.cpp FerryTurn`，strike 机体 98 m/s 时约 266 米），要求这条直线从目标两侧都能离开地图并留出 2.5 个转弯半径 + 100 米的掉头空间（`PassEnd`）；`Route::to` 为远端。
- 航线：`support_entry.h MakePassLine` + 新文件 `src/ferry_line.h`。直线过投放点，两端在地面边缘外 300 米（受物理正方形与掉头空间限制，至少 100 米）。`Ferry` 改为航线跟踪：瞄准自己在航线上投影点前方 `Lead`（1.5 个转弯半径，至少 250 米）处，横向误差在这段距离内收敛，过投放点时就在线上，不再追点。过端点就掉头（固定向右转，不会每帧在两个等价方向间抖动），沿同一条线反向再飞一趟。
- 计划阶段就把航向写在机体矩阵里：`BoardAirborne` 在激活当帧就调 `JetFerry(机体, 目标, 矩阵前向)`，不再等所有对端确认后（`Assign`）才从 strike 巡逻改成渡运。`Assign` 只把投放点交给 `TransportParadrop`。
- 软边界：渡运中的飞机（`j.ferry` 且未撤离）不受 `KeepIn` 改向（`jet_flight.cpp SoftEdge` 对副本跑 `KeepIn`，状态照常跟踪），天花板、地形、障碍、学到的墙、世界边缘删除都照旧。撤离时恢复软边界。
- 投放：`transport_logic.h StickStarts / StickLead`。提前量 = 漂移（伞兵带走飞机速度 × `kJumpInherit`，伞以 `kChuteBleed`=0.6/s 衰减 → 速度/0.6）+ 半条“棒”的长度（(人数−1) × 350 ms × 速度 / 2），使整队落点以投放点为中心。只在投放点离航迹 ≤ 80 米、且处于 [提前量 − 1.5 秒航程, 提前量] 窗口内开始；错过就等下一趟（掉头后同线反向）。速度和航迹由 `DropFrame` 用上一帧位置差得出。
- 投完：`JetFerryDone` 让飞机沿当前方向飞到这一趟的端点（场外）再 `reap`（`Ferry` 设 `ferryGone`，`JetFrame` 置 `reap`，由 `support_dispatch.cpp Retire` 连同机组删除）。没有航线的飞机（`JetFerry` 失败）仍旧 `JetWithdrawNow`。
- 直升机机降 / 投送载具（`HeliFerry`）本来就直线飞向点：入口改到场外后仍是直线，`heli.cpp SoftEdge` 只限制向外的速度，飞机向内飞不受影响。

## 测试

- `tests/support_entry_test.cpp`（重写）：入口在地面边缘外 ≥200 米、物理正方形内、超出绘制距离；正方形容不下时取最远点（角）；正方形太小时拒绝而不是放回场内；虚空不再拒绝；日志那一关（cat32）的复现：入口离玩家 ≥3200 米、入口—投放点—远端共线、两端都在场外且留足掉头空间；运输机在没有掉头空间的图上拒绝、普通飞机仍可入场；地面候选的“看不见”排序和场地边缘候选。
- `tests/ferry_line_test.cpp`（新增）：质点按转弯半径飞 `ferry::Steer`，三种起始横偏（0 / 250 / −400 米）都在线上（<10 米）三次过点、在两端之间掉头两次、掉头外冲不超出预留空间、投完后在端点外消失；掉头方向固定。
- `tools/jet_obstacle_sim.cpp --selftest`（CTest `jet_attack_runs`）的渡运用例换成生产 `Ferry + Wing + Guard`，地图地面 ±1750（软边界 ±1000）：从场外沿线 / 偏 300 米 / 斜角三种入场，过点时离线 ≤1 米，到达两个端点，投完后在地图外删除，最远 2347 米（世界删除线 2950）。
- `tools/transport_check.cpp`、`tests/transport_runtime_test.cpp`：提前量公式、窗口、横偏 150 米不投、过窗不投、整队落点居中；投完交给 `JetFerryDone`，没有航线时撤离。
- `tests/call_pick_test.cpp`：生产 `PlanAirSupport` 的入口在地面边缘外、`ArrivalHalf` 内。
- `tests/support_dispatch_test.cpp`：运输机在 `BoardAirborne` 就以计划航向调用 `JetFerry`，只调一次。
- `tools/selftest.py support_arrives_from_off_the_map`：游戏内存那一半的源码约束（inset 在创建时、交还前写；原版输入先于插件帧步；`HeliFrame` 第一句就是 `SupportAircraftFrame`；远景认 `0x17C4030` 且判空；`PlanAirSupport` 用物理正方形；投放用 `StickStarts`、投完 `JetFerryDone`）。`soft_edge_wired` 补上渡运豁免。
- 变异实测见提交说明。

## 未实测（只做了离线验证，没进游戏）

- 远景渲染：`0x17C4030` 的结论是静态 H 级，开关后 500–1000 米双相机重叠是否闪烁、远处是否可见，都没在实机看过。螺旋桨、武器等是否有独立渲染对象（会在远处消失）没查。
- 创建时写 `kNoInset` 是否被之后的原版初始化（`PrepareNpcVehicle` 的 RideAi 读 SGO `mission_setup`）覆盖：`docs/map-edge-re.md` 说全 DLL 只有初始化时写一次 `+0xE00`，但没有确认它发生在 `PrepareSupportAircraft` 返回之前。若被覆盖，直升机由 `SupportAircraftFrame` 每帧重写，喷气机由 `JetFrame` 每帧重写，但第一帧仍可能被夹一次。
- 伞兵出舱是否继承飞机速度（`kJumpInherit=1`）未实测；`JumpFrame` 现在会记一行 `paratrooper ... out at N m/s level`，下次日志可以据此校准。若实际不继承，整队会落在投放点前方约 160 米（98/0.6）。
- 支援直升机从 3 km 外飞来要更久（BigWorld 下入口可能在 3–5 km 外）；没测实际到达时间。
- 地面候选的视线遮挡只看玩家一个人（房主的 `player.pos`），联机时其他玩家可能看得见。BigWorld 关闭的小图上空中入口可能超不出 3000 米绘制距离（日志会写 `the farthest taken`）。
- 远端副本的直升机：inset 由远端自己的 `SupportAircraftFrame` 还原，位置由网络同步驱动，没有联机实测。
