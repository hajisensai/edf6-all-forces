# 2026-10-09 实机反馈：地图支援（support 组）

依据：本机游戏目录只读的 `Mods/Plugins/EDF6VehicleCrew.log`（2026-10-09 11:03 起的离线会话、00:11 的单人房间会话）与 `EDF6Coop.log`，
未启动游戏、未安装 DLL。以下“已验证”都指离线 / 原生私有映像测试，**没有任何一项在游戏里实测过**。

## 1. 「召唤的空中支援和支援无法上场，联机中也无法上场，只让离线使用」

### 日志事实
- 11:13:34–11:21:01 用户开着地图约 7.5 分钟，期间没有任何 `SUPPORT ground: ... hull=`、`JET v=... launched` 的支援生成行，也没有拒绝原因——旧代码的拒绝只写 HUD，不写日志，这本身是排查盲区。
- 同一会话 `CONFIG customNpcAi=1 boarding=1 ... jet pilot=1 airRaider=1`、`SUPPORT soldiers=1`，没有兵员构造异常：门禁与资源都放行了，失败在**入口规划**。
- 00:11 的房间 `SUPPORT world participants sealed: actors=1 peers=1`：用户一人开房；联机请求同样交给房主的同一个规划器。

### 根因
1. **地面支援（小队/大队/车辆）**：`src/support_dispatch.cpp` 旧 `Plan()`（原 176-197 行）只在 `MapPlayArea` 的 8 个边缘点找入口，再用
   `GroundNavigate` 证明整条路线可达。规划器有界：`src/ground_navigation.h` `Navigate()` 只搜起点 ±256 格（4 m 格 = ±1024 m）、最多 `kNodes=1536` 节点。
   本机日志 `AREA ... walls x -1600..1597`：地图中部目标离任何边缘都 1100–1700 m，**完整路线根本放不进搜索框**，每个边缘都要把 1536 个节点耗尽才判 blocked。
   每帧只走 4 条边 → 每个边缘约 50 秒，8 个边缘约 7 分钟后才报“找不到与目的地连通的地图边缘入口”——与用户开地图干等 7.5 分钟吻合。
2. **空中支援**：`src/airstrike.cpp` `PlanAirSupport` 要求地图边缘一条 220 m×90 m（每多一架 +65 m）的跑道，三条线每个采样点与起点高差 ≤ **0.5 m**。
   真实地图边缘没有这么平的地方（同一插件给玩家飞机找降落跑道用的是 4 m / 相邻 1.5 m 的规则：`playerjet_board.inc` `StripCost`），所以一律“找不到可用的场外跑道”。
3. **联机**：联机请求的规划在房主机器上跑同一个 `Plan()`，所以 1、2 同样让联机失败；此外：
   - 单人房间（世界里只有房主一个参与者）也必须走 EDF6Coop 扩展传输；传输未就绪（例如没开直连、成员标记未到）时直接拒绝，而此时根本没有需要同步的对端；
   - 传输为什么没就绪没有任何日志。
   “缺 Coop 接口就拒绝”对**有其他玩家的房间**是对的：原生 `CreateObject` 不复制对象，`RegisterSupportObject` 只登记 ID，客户端要看到同一批对象必须靠 EDF6Coop 的可靠扩展通道在每台机器上按房主的矩阵创建（`docs/support-network.md`）。这一部分确实依赖配套 EDF6Coop（≥2.5.0，af-support/2），无法在本仓库单方面去掉。

### 修法
- 入口改为以目标为圆心 650/800/950 m 三圈、各 8 个方位的候选点（`src/support_entry.h` `GroundEntryCandidates`）：仍满足离目标 ≥600 m、离呼叫者 ≥800 m、在实测场地内，且全部落在规划器 ±1024 m 搜索框内（`static_assert` 钉住）；同一圈内先试离呼叫者最远的。候选在请求开始时固定。规划每帧最多调用 3 次寻路（共享的每帧查询预算不变）。
- 跑道改用降落跑道同一规则 `RunwayLaneLevel`（起点 ±4 m、相邻采样 ±1.5 m），飞机机组集合点高差同样放宽到 4 m（地面车辆机组仍 0.55 m）。
- 每个拒绝都写日志：`SUPPORT plan catalog=N refused: <原因>`、每个被淘汰的入口 `entry K (...) rejected: <原因>`、成功 `ready`、生成 `SUPPORT spawn ...`；EDF6Coop 传输状态变化写 `SUPPORT NET transport: <原因>`（没装 EDF6Coop / 旧版本无扩展 / 扩展房间未就绪 / ready host|client）。
- **房主权威 + 单人世界本地部署**：`support_net.cpp` `SupportSoloHostWorld()`——本机是房主，且本局已封存的实际参与者只有本机（分屏共用一个 PUID）。此时没有对端需要复制，之后进房的成员被参与者门禁挡到下一局（`SupportMissionPlayerAllowed`），所以房主可以不经传输直接本地规划、创建未注册对象（`support_dispatch.cpp` `LocalAuthority()`、`SpawnDeployment(...,networked=false)`；`support_soldier.cpp` `Gate(...,local)` 只对 `OnlineHostOnly()` 放行，客户端永远不能本地生成）。有其他参与者时仍走原来的房主规划→全员确认→激活协议，客户端只发请求。
- 守卫 `tests/online_gate_guard.py` 更新：联机仍必须经可靠协议，唯一例外必须是 `!InSession() || SupportSoloHostWorld()`。

### 测试
- `support_entry_test`：中部目标在实测 ±1600 地图上有候选、每个候选满足旅程/视野/场地/搜索框；远离呼叫者优先；近边缘不越界；未测场地无候选；跑道 1% 坡通过、5% 坡和 2 m 台阶拒绝。
- `call_pick_test`：生产 `PlanAirSupport` 在 1% 坡的真实边缘给出跑道，5% 坡、每 30 m 一个 2 m 台阶拒绝。
- `support_dispatch_test`：单人世界房主不发网络请求、本地创建、无 ID、不注册；有其他参与者的房主仍走协议；配置停用单位直接拒绝。
- `support_soldier_test`：房主本地生成正确模板且不注册；客户端本地请求被拒；未知资源不创建。

## 2. 「呼叫 npc 降落，直接撞地上爆炸了」

这是 H 键“呼叫降落”（`playerjet_board.inc` Hail，HUD 文案“[H] 呼叫降落”）。日志里两次都在进近前坠毁：
- 10:34:38 fighter：爬升到 680 m 后，在 250 m 高度抵达五边入口时**机头朝反方向**，对入口点做掉头 → `crash: sink 110.4 m/s, speed 121 m/s, banked`；
- 11:24:15 fighter：倒飞俯冲中被呼叫，入口在身后 1.5 km、低 480 m → `crash: sink 50.4 m/s, speed 117 m/s`。

### 根因（`src/playerjet_board.inc` 原 494-537 行）
1. 摆渡段目标 = 入口 − d×(距离/2)，越近提前量越小，最后直接瞄入口点：到达时机头不对准，只能在五边高度（离地约 110 m）急转掉头；
2. `SteerAt` 的瞄准没有下降角限制，只在离地 <30 m（`kCatchFloor`）时把瞄准抹平——对 50–110 m/s 的下沉率太晚；鼠标瞄准律对身后的目标会压坡度到 80° 以上，大坡度时升力不再托住重量，于是“转弯即掉高”。
3. 进入五边后瞄准点在接地点前 450 m 处抹平到 −2 m，飘过接地点 100 m 就复飞；进入五边也不检查高度/横偏，偏了下一帧就判复飞，三帧三次直接放弃。

### 修法（`src/hail_glide.h`，纯函数；`playerjet_board.inc` 小改）
- 摆渡分两段：先飞到入口后方外点（≥1200 m 且 ≥4 个转弯半径），绕过外点转到与跑道同向后进入 `kHailInbound`（新相位），沿五边线做提前量 1.5 个转弯半径的追踪；状态保持到越过入口为止（无状态版本会在外点附近来回切换、绕圈）。
- 下降包线 `LimitAim`：摆渡最陡 8°、五边最陡 7°；摆渡保持离地 60 m，下沉率必须留出 5 s 余量，不够时先改平机翼沿原航迹爬升（转弯延后）；偏离航迹 >60° 的转弯一律平飞转。
- `LimitTurn`：瞄准偏离航迹的角度限制在“机翼按当前速度能平飞维持”的侧向升力内（2 g 的炮艇机不再压到 80° 以上坡度）。
- 五边瞄准点保持在下滑线上直到接地（接地下沉约 6 m/s，<10 m/s 的着陆上限）；只有在下滑道高度与横偏带内才进入五边，否则继续绕场。
- 旋翼机（`RotorHail`）原有下沉限制，未改。

### 测试
`tools/hail_glide_check.cpp`（CTest `hail_glide`）：用 `playerjet.cpp Air/AimSteer` 同一套公式（`pjet_handling.h` 的实际函数，镜像 `pjet_turn_sim.cpp`）在平地上飞完整个呼叫降落：
- 两次日志状态：**旧律两次都坠毁（下沉 107 / 90 m/s，负对照，复现了用户看到的现象）**，新律两次都在五边着陆（下沉 6.1 m/s）；
- 8 种可登乘固定翼 × 1296 个起始状态（高度 250–800 m、三档速度、俯冲/平飞/爬升、0°/90°/175° 坡度、四个方位、四个航向）：0 坠毁，10368/10368 着陆。

### 未验证 / 限制
- 模型是插件飞控公式本身，不是 Havok 物理；平地、无建筑、没有地图边墙（`WallTurn`）。外点可能在入口后方 2 km 以上，小地图上会碰到场地边墙，墙把航迹转向后能否继续完成进场**没有离线覆盖**。
- 没有在游戏里实测任何一次呼叫降落。

## 3. 战斗外配置可呼叫支援单位与挂载

### 现状
仓库原有的战斗外入口：`EDF6VehicleCrew.ini`（插件启动和每次保存时重读）与安装器菜单。支援目录完全写死，NPC 只有一种步枪兵模板。

### 修法
- `src/support_config.h/.cpp`：从 `EDF6VehicleCrew.ini [VehicleCrew]` 读取并校验（`plugin.cpp LoadConfig` 末尾调用，随 ini 热重载）：
  - `SupportDisabled=FIGHTER,TANK_CREWED,...` 不可呼叫的单位（29 个稳定键：空中呼叫 = 武器行 id 去掉 `EDF6VC_CALL_`；`SQUAD PLATOON TANK_CREWED ...`）；
  - `SupportSquadWeapon / SupportSquadLeaderWeapon / SupportPlatoonWeapons(三个) / SupportVehicleCrewWeapon / SupportAircraftCrewWeapon`：`rifle flame rocket shotgun sniper`。
    这是 Root.cpk 里现成的原版 Ranger 模板 `N601_COMMON_RANGER_{AF,FL,RL,SG,SN}[_LEADER]`（同一 `AssultSoldier` 类、模型和 CAS，各自加载原版 AI 武器：步枪/火焰喷射器/火箭筒/霰弹枪/狙击枪），每关全部预载；
  - `SupportAircraftCount_<KEY>=n`：一次呼叫的架数（0 = 默认，1–8，再受 16 个单位上限约束）。
  - 非法值保留默认，日志 `CONFIG support ... INVALID: <哪一项=什么值 改用什么>`，并在本关第一次呼叫的 HUD 提示里写出。
- 兵员资源编号 `(武器<<8)|角色`，步枪保持协议 v2 的 1/2；对端只校验结构和“是原版模板”，不拿自己的 ini 校验房主的配置（武器、架数以房主为准）。
- 安装器菜单 7（`tools/support_config.py`）：列出 29 个单位开/关、改各类兵员武器、大队三个小队武器、空中单位架数、恢复默认；每个输入都按插件同样的规则校验，非法输入不写入并说明原因；游戏运行时也可改（插件保存即重读）。
- `EDF6VehicleCrew.ini` 增加上述键与说明（旧用户安装时由 `merge_ini` 自动补入）。

### 测试
- `support_config_test`：默认值、大小写/全角逗号、非法武器/未知单位/大队数量不对/架数越界都保留默认并逐项点名；经真实 ini 文件（`GetPrivateProfileStringW`）加载并发布。
- `support_dispatch_test`：真实 ini → 停用单位被拒；小队队长狙击、队员霰弹；大队三个小队分别火焰/火箭/步枪；架数 3 传到跑道规划、机组带配置武器；对端接受房主配置、拒绝非模板资源。
- `support_config_ini_test.py`：菜单的武器/单位表与 C++ 解析器、`calls.py` 一致；脚本化走一遍菜单；只写合法值。
- `support_soldier_native_audit.py`：十个模板都从 Root.cpk 读出并核对类和各自的原版武器。

### 未做 / 限制
- **载具弹药**：坦克/运兵车/轻卡与支援飞机的武器、弹药来自原版 SGO 与安装器生成的挂载（`make_stock_stores.py`，属挂载组），运行时没有可靠的“换弹药”入口，本次没有做载具弹药配置，不宣称支持。
- `GetPrivateProfileStringW` 按 ANSI 代码页读 UTF-8 ini，中文武器名在 ini 里不可用，请用英文名（安装器菜单写的就是英文名）。
- 游戏内未验证新模板的 AI 行为（例如火焰兵的交战距离）。

## 构建与测试
`cmake --build build -j 4`、`--target offline_checks`、`ctest -j 3`：见提交说明。

## 附：`support_soldier_native` 偶发 access violation（另一提交）
- 根因：审计用 `LoadLibraryExW(..., DONT_RESOLVE_DLL_REFERENCES)` 私有映射 EDF.dll，加载器不建隐式 TLS、CRT 启动不运行。`776AB0` 的 CRC 表是 MSVC 线程安全局部静态：经 `gs:[58h][_tls_index]`（`_tls_index` RVA 213A670，映射中恒为 0）读本线程 `_Init_thread_epoch`，实际读到的是 Python 进程自己 TLS 槽 0 偏移 20h 的任意值。值“够大”时跳过构建、用全零 CRC 表算出非原生的哈希（测试照样通过）；值“偏小”时进入 `_Init_thread_header`，调用未解析的 `EnterCriticalSection` IAT 槽——槽里是导入名 RVA `0x1F52128`，正是各组看到的 access violation 地址。与并发无关，取决于进程里那块 TLS 内容。
- 修法（只改测试夹具，不放宽、不重试）：按加载器/CRT 的职责为私有映像准备它用到的部分——从映像自己的 TLS 模板建 TLS 块、解析 KERNEL32 导入、初始化 CRT 线程安全静态用的临界区与事件；调用时由一段机器码 thunk 在原生代码内部临时把 `gs:[58h]` 换成映像的 TLS 数组、返回前换回（不能在 Python 里换：CPython 3.13 的线程状态本身用隐式 TLS，ctypes 调用路径就会读它）。新增断言：原生静态初始化真的建出了标准 CRC-32 表、并把 epoch 写回了该 TLS 块。
- 验证：并发 3 路 × 5 轮共 15 次全部通过；此前单跑也会挂。

## 附：新旧版本混房（整合审查【中】，另一提交）
- 问题：配置武器使用新兵员资源号（如大队默认 rocket 0x201、sniper 0x401），旧版客户端的 `Validate` 只认 1/2，而线上协议版本没变：新房主 + 旧客户端呼叫大队时，旧客户端在事务中途拒绝，界面只显示“未受理/取消”，原因不可见；配置的飞机架数同理（旧 `Validate` 要求等于默认架数）。
- 取舍：不提升 `EDF6AF_SupportProtocolVersion`/线上 `kVersion`。配套 EDF6Coop 只认 `version()==2` 和 `af-support/2`，提升会让整个扩展传输失效、所有联机支援都不可用；提升线上版本则旧新双方互相丢包，只能看到“联机支援尚未就绪”，同样看不到原因。改为**能力协商**：客户端在 hello 的 `index` 字段宣告 `kCapSoldierVariants`（v2 线格式不变，旧房主的同一校验接受、忽略该字段；旧客户端发 0）。房主 `Session::PeersHave` 只有在本局所有对端都宣告该能力时才按配置的武器/架数规划；否则回退为协议 v2 的计划（步枪资源号 1/2、各呼叫默认架数），支援照常出动，并在房主 HUD（`hudtext.inc supportLegacyPeers`）和日志（`SUPPORT plan: a peer runs an older All Forces ...`）说明“房间里有旧版，本次兵员用步枪、默认架数”。同版本房间行为不变。
- 限制：提示显示在规划的房主机器上；若请求者是另一台新版客户端，它只看到受理/出动状态，看不到回退原因（旧客户端也无从显示新文字）。
- 测试：`support_protocol_test` Capabilities（能力 hello 在 v2 线格式往返、线版本仍为 2；全新房间 PeersHave 为真；含旧客户端的房间握手照常、PeersHave 为假且 v2 计划被所有端接受并生成；对端更新后以最新 hello 为准）；`support_dispatch_test`（旧对端时大队/小队为步枪 2/1、飞机为默认架数和步枪机组、整个计划只含 v2 资源号、HUD 含说明、关卡重置后清除）。

## 附：空中支援改为场外空中飞入（用户追加反馈，另一提交）
用户：“空中支援不是场外飞进来吗，不需要真起飞吧”。上面第 1 节把跑道规则放宽到 ±4 m 仍是在错误前提上修补：空中支援不应在地面集结起飞。

### 改动
- 规划（`airstrike.cpp PlanAirSupport`）：删除跑道/起降垫平整度、长度、水面与机组集合地面的检查和“找不到可用的场外跑道或直升机起降点”“飞机旁没有可供真实机组集合的地面”等拒绝；只验证目标上空开放天空、`AirRoute` 的边缘入口（高度 = 航线最高地形 + 巡航高度）、每个编队机位在场地内、高于自身地形且整机航线净空（`EntryClear`）。拒绝文案改为“从地图边缘到目标没有净空的空中航线”。
- 编队（`support_entry.h AirFormationSlot`）：长机在入口，其余左右交替横向排开，间隔 70 m（直升机 ×0.6），同一高度；不排在入口后方（入口距场地边界只有 30 m，后方会落到场外）。
- 生成（`support_dispatch.cpp BoardAirborne`）：计划中的飞机资源号为 `kSupportAircraftResource+kSupportAirborneOffset+目录`；机组矩阵 = 所在飞机矩阵。各端创建飞机与机组后，机组用 `CreateSupportSoldierUnregistered` 创建（带 ID 但暂不登记），`npcai.cpp NpcSeatCrewNow` 当场用原版 `RideVehicle` 入座（第一名坐驾驶席，仍然是原生士兵坐真实座位、真实机组驾驶），然后才登记飞机与机组的网络身份——这样不会有原生上车事件先于其他玩家知道对象 ID。房主（或离线/单人世界）随即 `ActivateSupportAircraft(...,airborne=true)`：固定翼按 `jet_spawn.cpp Launch` 的做法以机型巡航速度沿机头方向飞（模式 patrol），直升机经 `HeliCalled`（已有“空中生成时旋翼设为 0.5、不先掉高”的处理），插件飞控当帧接管；对端副本只入座不飞，由原生复制驱动。机组 AI 仍保持冻结到全员确认（Active），`Assign` 对空中飞机不再发登车请求。
- 联机能力：`kCapAirborneAir`（hello 能力位 2，线格式仍 v2）。房主只有在所有对端都具备该能力时才规划空中支援，否则拒绝并在 HUD（`hudtext supportAirNeedsUpdate`）与日志说明“房间里有旧版，需全员更新”，不会出现一端在地上、一端在空中。新版客户端收到旧版房主的跑道计划（旧资源号）时按旧流程（地面空壳、机组走上去、入座后起飞）执行。
- 删除已无用途的 `RunwayLaneLevel` 及其测试。

### 测试
- `call_pick_test`：生产 `PlanAirSupport` 把入口放在空中（≥ 巡航高度）；整张地图都是 5% 坡加每 30 m 一个 2 m 台阶、没有任何平地时 4 架也能规划；编队机位高于自己脚下地形；屋顶下与场地未测定仍拒绝。
- `support_entry_test`：编队长机在入口、其余横向交替、同高度、互相间隔 ≥70 m、不在入口后方，直升机更紧。
- `support_dispatch_test`：生成即在空中（入口高度），机组在飞机内同位置创建并坐进驾驶席，无登车请求、无起飞，生成当帧以 airborne 方式启动飞行且只启动一次；机组无法入座时飞机与机组一并回滚、不起飞；联机计划中飞机与机组都在入座之后才登记身份，房主立即飞行，全员确认后释放冻结且仍无人走路登机；对端副本同样入座但不飞；旧版对端时拒绝且 HUD 说明原因；旧版房主的跑道计划仍通过校验并按旧流程登机。
- `support_soldier_test`：带 ID 的机组先创建不登记，之后由调度器登记。

### 仍需进游戏确认
- 原版 `RideVehicle` 对“刚创建、尚在空中、与飞机重叠”的士兵入座是否总能成功（离线测试是桩，未执行原生函数）；入座后士兵模型是否正确附着。
- 固定翼在空中生成首帧：原生物理是否在插件写入速度前施加一帧重力；直升机水平初速为 0，从悬停起步加速。
- 编队横排在窄地图边角入口的实际间距是否足够、会不会互相碰撞（离线未模拟飞行间相互碰撞）。
- 回滚时删除已入座机组再删飞机的原生行为（离线是桩）。
- 联机：对端在登记身份前本地入座、登记后原生复制是否与房主一致；未双机实测。
