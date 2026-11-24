# 任务预载按可达集合收窄（hitch-4 / mem-1 / mem-5）

审查条目：`docs/reviews/2026-11-24-optimization-audit.md` hitch-4、mem-1、mem-5。分支 `claude/opt-preload-1124`。

## 做了什么

以前 `mission.cpp` MissionStart 的各个 `Preload*`（jet_spawn / jet_bay / playerjet / subcarrier / carrierlaser / support_spawn /
support_soldier / airdrop / ultimate）每个任务都把装了的插件内容全部排进预载组 2（`0x7A3780(mgr,sgo,2,-1)`）：38 个机体及其武器、
无畏舰 111 发弹、潜航母舰、激光、全部弹射回收文件等，不管这一局能不能叫出来。

现在每个任务开始前先定一个「本任务可达集合」（`src/preload_set_logic.h`，纯逻辑，`tests/preload_set_test.cpp`），
各 `Preload*` 逐项问 `preload::Want(item)`，不在集合里的不排队：

- 本机需求 `LocalNeeds()`（`src/preload_set.cpp`）：
  - 所有任务都要：载具弹药（vehicleRounds）、降落伞、全部支援士兵种类（士兵也给任务自带载具当乘员，编制面板任务中可换种类，且
    `support_soldier.cpp` 只有一个 `preloaded` 标志，所以不收窄）；
  - 支援目录里本机 `SupportDisabled` 没关掉的条目能带出的东西（`SupportCatalogReachOf`：机体 / 运输机降落伞 / 直升机绳索 /
    空投箱 / 地面支援车体 / 地下钻地运输 / 终极打击魔法阵 / 潜航母舰 + 激光 + 无人机；出击的第二波 `then` 也算）；
  - 四个兵种全部已装备的呼叫武器（GS+0x6E98 的武器槽，行名 `EDF6VC_CALL_*` → `AddCallItems` 表）；
  - 调试刷怪、测试场、PvP 对战：全部（它们能造任意机体）。
- 联机：每个成员在 host-settings 词（`EDF6X_AF_ROOM`，v4）里公布 `n=`（自己的需求）和 `p=`（自己看到的房间并集）；
  MissionSync 时所有成员用**房主的 `p=`**（`AtSync`），保证每台机器预载同一套（对象在每台机器上都要造）。
  房主自己用它已公布、房间看得到的那份 `p=`；房主一份都还没发出去时用当场现算的并集。
- 离线：同一算法只看本机（`HostSettingsOfflineMission` → `DecideOffline`）。
- `PreloadMode`（ini，开发者 A/B 开关）：0 按需（默认）、1 全部（旧做法）、2 插件内容一概不预载。联机时以房主的集合为准。
- 每个任务开始写一行 `PRELOAD this mission's set <32 hex> (<n> items, <来源>; this machine's PreloadMode ...)`。

## 联机协议

- host-settings 词 v3 → v4（加 `n=` / `p=`，各 32 位大写十六进制，高字在前），`kWordMost` 128 → 200；
  新能力位 `kFeatPreloadSet=2048`（房间公告里的名字 `featPreloadSet`）。
- 房间协议 `kAllForcesRoomProtocol` 15 → 16：旧版本不公布需求却会照旧全量预载，房主的集合就漏掉它的呼叫，所以旧版不能进房
  （房主侧按协议拒绝，房间公告所需版本，沿用现有机制）。不写兼容分支。
- 为什么不用 `plugin_net.h` 通道：预载在 MissionSync 之后、任务对象建立之前，此时 plugin_net 还没起来；成员属性（coop_attr）
  在房间阶段就可读，正好是 host-settings 词已有的传输。

## 已知边界

- 房主每 0.5 s 发布一次 `p=`，读到客人的 `n=` 再到下一拍发布有约 1 s 延迟：客人在开局前 1 s 内换了呼叫武器，房主的集合可能
  缺它。后果是那件东西首次使用时同步加载卡一下（喷气机武器），或无畏舰机库起飞失败；不崩溃。
- 弹射回收（catch）文件永远不预载：该功能已无入口。`bomber401` / `bomber501_2` 同理（`JetLaunchBomber` 已无调用）。

## 验证

- 离线：`preload_set`（呼叫行表、Always ⊂ All、ByMode、RoomSet、AtSync、文本格式往返与坏输入）、`coop_attr`（v4 词往返、
  v3 词与残缺 v4 词一律拒收）、`player_identity_guard`（协议 16）、`command_identity` / `command_native_dispatch` /
  `support_soldier` / `support_spawn` 等原有夹具；`python tools/check.py` 全绿。
- 实机（未做，交协调者统一采）：同一任务 `PreloadMode=1` 对 `PreloadMode=0` 各 1–2 次，对比进图时长、MISSION 阶段 ms、
  MEMORY 行；配合 hitch2 的 `SyncLoadProbe=1` 看叫出各类呼叫时有没有 `SYNC LOAD ...EDF6VC_...` 漏项。
