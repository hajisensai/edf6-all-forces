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

## 载具武器 SGO

- 游戏的资源预载（`0x7A3780`）预载载具 SGO 时**不会**带上它 `vehicle_setup` / `mission_setup` 里写的 `app:/weapon/*.sgo`：
  hitch2 的 SYNC LOAD 探针在一次测试场里看到这些武器在造车时于游戏线程同步读了 41 次、共 4.1 s（原版 V_* 27 次 3.0 s，
  插件 EDF6VC_* 14 次 1.1 s），是进关同步读里最大的一块（总共 7.1 s）。
- 现在每个预载载具 SGO 的地方（`support_spawn.cpp` 支援车体与钻地运输、`support_variants.cpp` 的 `EDF6VC_LO_` 载具、
  `debug_spawn.cpp` 任务预载与按需加载、`player_vehicle.cpp`、`subcarrier.cpp`）紧接着调 `QueueVehicleWeapons`
  （`src/vehicle_weapons.h`），把这辆车的武器 SGO 以同样的参数排进同一预载组，同一任务内每个文件只排一次。
  武器跟着载具走：载具在可达集合里，它的武器就在；集合的位不变，联机房主下发与离线走同一算法，协议号不因此再变。
- 武器从哪来：
  - Mods 里的载具（插件的 EDF6VC_*、测试场派生的 EDF6TR_*、EDF4.1、`EDF6VC_LO_` 变体，以及 Mods 里的原版覆盖）：
    预载时读 `<游戏目录>\Mods\OBJECT\NAME.SGO`，扫其中的 UTF-16LE `app:/weapon/....sgo` 字符串（`ScanSgo`）；
  - 原版载具（在 Root.cpk 里，插件不读）：查生成表 `src/vehicle_weapons.inc`（`tools/gen_vehicle_weapons.py` 从 Root.cpk
    生成；收录插件会预载的全部原版载具：spawn_catalog 的原版载具行 + `kSupportVehicles`）；
  - 两者都不是：记一行日志，不回退。
- 每个任务在预载后写一行 `PRELOAD vehicle weapons: <n> weapon files queued with <m> vehicles`。

## 联机协议

- host-settings 词 v3 → v4（加 `n=` / `p=`，各 32 位大写十六进制，高字在前），`kWordMost` 128 → 200；
  新能力位 `kFeatPreloadSet=2048`（房间公告里的名字 `featPreloadSet`）。
- 房间协议 `kAllForcesRoomProtocol` 取合并时 main 的值 + 1（写这份文档时 main 是 16，本分支为 17；与 net-6 谁先合谁用下一个号，后合的再 +1）：旧版本不公布需求却会照旧全量预载，房主的集合就漏掉它的呼叫，所以旧版不能进房
  （房主侧按协议拒绝，房间公告所需版本，沿用现有机制）。不写兼容分支。
- 为什么不用 `plugin_net.h` 通道：预载在 MissionSync 之后、任务对象建立之前，此时 plugin_net 还没起来；成员属性（coop_attr）
  在房间阶段就可读，正好是 host-settings 词已有的传输。

## 已知边界

- 房主每 0.5 s 发布一次 `p=`，读到客人的 `n=` 再到下一拍发布有约 1 s 延迟：客人在开局前 1 s 内换了呼叫武器，房主的集合可能
  缺它。后果是那件东西首次使用时同步加载卡一下（喷气机武器），或无畏舰机库起飞失败；不崩溃。
- 弹射回收（catch）文件永远不预载：该功能已无入口。`bomber401` / `bomber501_2` 同理（`JetLaunchBomber` 已无调用）。

## 验证

- 离线：`preload_set`（呼叫行表、Always ⊂ All、ByMode、RoomSet、AtSync、文本格式往返与坏输入）、`coop_attr`（v4 词往返、
  v3 词与残缺 v4 词一律拒收）、`player_identity_guard`（协议号）、`command_identity` / `command_native_dispatch` /
  `support_soldier` / `support_spawn` 等原有夹具；`python tools/check.py` 全绿。
- 载具武器：`vehicle_weapons`（扫描：奇偶偏移、大小写、诱饵；hitch2 清单里的原版载具 VEHICLE404 / 403 / 407、V510、V505、V503
  的武器都在其行里；Mods 优先于原版行；同一任务去重）、`vehicle_weapons_covers`（无游戏：插件会预载的原版载具都有行）、
  `vehicle_weapons_stock`（生成表与 Root.cpk 一致，无游戏 77 跳过）、`vehicle_weapons_native`（用插件自己的扫描扫 spawn_catalog
  全部 113 个载具行的真实 SGO：原版行与生成表一致，hitch2 清单的 39 个武器文件全部被某辆可预载载具带上；无游戏 77 跳过）；
  `support_spawn` 断言三个支援车体的 3 个武器随车体排队。
- 实机（未做，交协调者统一采）：同一任务 `PreloadMode=1` 对 `PreloadMode=0` 各 1–2 次，对比进图时长、MISSION 阶段 ms、
  MEMORY 行；配合 hitch2 的 `SyncLoadProbe=1` 看叫出各类呼叫时有没有 `SYNC LOAD ...EDF6VC_...` 漏项；
  同时看进关的 `SYNC LOAD` 里 V_* / EDF6VC_* 武器行是否消失，以及 `PRELOAD vehicle weapons` 行。
