# 2026-10-09 实机反馈：载具挂载 / EDF5 任务包（loadcamp 组）

全部为静态分析、离线生成与离线测试。**没有启动游戏，没有写游戏目录**，下面凡是「游戏内表现」都标为未实测。

## 反馈 1：「各种载具的挂载没写，我要各种载具要有应有的挂载（符合设定的，例如坦克应该有ap和he，甚至炮射导弹如应有的话）」

### 根因

挂载机制本身（额外挂点、R / LB 轮换、扣扳机转发、载具 HUD 列表）早已通用，缺的是**数据**：

- `tools/make_stock_stores.py` 的 `LOADOUTS`（改前第 70–96 行）给坦克、格雷普只挂了 `AGM-114`（`_TANK = (_store('AGM_L', 4, 0),)`），防空车只有两种空空导弹；全部挂载都取自战机的导弹 / 火箭 / 炸弹表 `pylib/vcobjects.py` `STORES`，**根本没有「炮弹」这一类挂载**，所以坦克不可能有 AP / HE，机炮车没有弹带，防空车没有高爆近炸弹。
- `STORES` 的武器类型只有 `Missile` / `Bomb`（改前 `vcobjects.py:207-216`），生成器 `jet_guns` 只会做这两种；`tools/gen_stores.py` 的 `ROLES` 也没有 `gun`，插件的 `kStores` 认不出任何炮弹挂载。

### 修法

1. **新武器类型 `Shell`**（`pylib/vcobjects.py`）：一种炮弹 = 一件**原版载具武器**原样拷贝，只改载弹量（文件名里的 `<rounds>`）和多语言名字。`ammo_class` 在玩家自己的 Root.cpk 上核对（不符就拒绝生成）；`kind` 为 `ap` 时要求模板无爆炸且穿透，`he` 时要求有爆炸半径。数值全部来自原版同级武器，不自编：

   | 挂载（`STORES` 键） | 模板（原版） | 弹类 | 伤害 | 爆炸 | 弹速 | 说明 |
   |---|---|---|---|---|---|---|
   | APFSDS（`AP`） | `V_505TANK_CANNON01S` 布莱克 A 型 90 mm 滑膛炮 | `SolidBullet01Rail` | 350 | 0 | 18 m/帧（1080 m/s），重力 0.1 | 原版的高初速穿甲弹体，穿透 |
   | HE（`HE`） | `V_505TANK_CANNON01` 布莱克 E 型 105 mm 榴弹炮 | `RocketBullet01` | 350 | 8 m | 3 m/帧起加速，重力 1 | 原版的爆炸弹体 |
   | LAHAT 炮射导弹（`GLM`，导弹） | 原版直升机导弹模板（同 AGM-114） | `MissileBullet01` | 600 | 6 m | 2 m/帧弹出，发动机到 300 m/s | 105/120 mm 炮射反坦克导弹，弹体 icbm01 按 0.975 m 缩放 |
   | AP 弹链（`AC_AP`） | `V_401STRIKER_CANNONS01` 格雷普滑膛炮 | `SolidBullet01` | 40 | 0 | 4 m/帧 | 穿透 |
   | HE 弹链（`AC_HE`） | `V_401STRIKER_CANNON` 格雷普榴弹炮 | `RocketBullet01` | 32 | 5 m | 4 m/帧 | |
   | 近炸引信高爆弹（`FLAK_HE`） | `V603_FLAK_GLGUN01_DLC_L` 沃卢斯高射炸药炮 | `GrenadeBullet01_MapNoDamage` | 9 | 4 m | 5 m/帧 | 接触球 = AmmoSize 4 × AmmoHitSizeAdjust 1 = 4 m（`docs/carrier-laser-re.md` §3），从飞机 4 m 内掠过即引爆，等效近炸；不毁建筑 |

   伤害按原版基数写：载具请求的 `[耐久倍率, 伤害倍率]` 由 `SetWeaponObject` 存进每个挂点（`docs/emc-re.md` §2：`0x633842` 把 `veh+0x678` 存进槽 `+0x40`），插件补造挂点同样走 `SetWeaponObject`，所以这些炮弹随请求档位（E1…E10、A1…A9）一起放大，与原版主炮同一套倍率（M：倍率写入为 H，「每发伤害的乘数」为 M）。
2. **按类别配挂载**（`tools/make_stock_stores.py` 的 `CATEGORIES` / `_BY_CATEGORY` / `REQUIRED`，取代逐个手写）：

   | 类别 | 载具 | 挂载 | 依据 |
   |---|---|---|---|
   | `mbt` 主战坦克 | 布莱克 505 全部变体、瓦利乌斯 601 | APFSDS 20、HE 20、LAHAT 4（从主炮口） | 主战坦克的标准弹种 + 炮射导弹 |
   | `ifv` 步兵战车 | 格雷普 401（含早期型） | AP 弹链 150、HE 弹链 150、AGM-114 4 | 机炮双弹链 + 反坦克导弹 |
   | `flak` 防空车 | 克卜勒 / 沃卢斯 603 | 近炸高爆弹 500、AIM-9X 2、AIM-120 4 | 高炮近炸弹 + 防空导弹 |
   | `launcher` 导弹车 | 奈格林 402 | AIM-120 4、AGM-65 2、Hydra 70 19 | 不变 |
   | `heli` 直升机 | N9 Eros 506、602 Heron、409 Nereid | Hydra 70 19、AGM-114 4、AIM-9X 2 | 不变 |
   | `bike` 摩托 | 503 / 613 | Hydra 70 19 | 不变 |
   | `railgun` | 艾普瑟隆 403 | AGM-114 4 | 电磁炮只打动能弹，不加 HE；不变 |
   | `superheavy` | 泰坦 404 | AGM-114 4（驾驶员左扳机） | 主炮本身就是巨型榴弹（镇魂炮），不变 |

   炮弹的「每件武器参数」（请求武器表项第 2 项）**复制它所挂原版主炮的 BodyRecoil 数对**（`_params` / `_entry_params`），开炮有和原版主炮一样的后坐；导弹 / 火箭仍是 `[0, 0]`。403 的具名 `AimRecoil` 永不复制（只认两个数的数对，否则回落 `[0,0]`），`check()` 逐项核对。
3. **生成与安装**：炮弹和 LAHAT 没有战机使用，由 `make_stock_stores` 自己写（`own_store_files()`，`vc.store_sgo` 统一分派导弹 / 炸弹 / 炮弹），战机也用的仍记为「需要」由 `make_jets` 写（`jet_store_files()`）；全部经 `ledger`，别人占用的同名文件跳过不覆盖；旧版装的 AGM-114 坦克请求升级时由账本按新内容重写。
4. **插件侧**：`tools/gen_stores.py` 增加 `gun` 角色，重新生成 `src/stores.inc`（新增 6 种 `kStores`）。R / LB 轮换、扣扳机转发、载具 HUD 列表、NPC 选弹全部复用现有机制（`payload.cpp` 按 `IsStoreWeapon` 认挂载，HUD 显示武器自己的多语言名字 `weapon+0x1B0`），不改火控切换状态机。唯一的代码改动：`NpcPayloadSelect`（`src/payload.cpp:436` 附近）原来把 `rounds.cpp` 判为 `lobbed` 的弹类（榴弹类）一律不用于空中目标，近炸高爆弹用的正是榴弹类，NPC 防空车就永远不会拿它打飞机；现在已编目的 `StoreRole::gun` 炮弹不受这条限制（其余榴弹 / 迫击炮弹照旧）。
5. **未加挂载的载具**都写进 `NOT_LOADED` 并给出原因（机甲、巨型机器人、深渊爬行者、EMC、Brute 410、救援车、轻卡车、Proteus），测试要求每台原版请求带来的载具要么在 `LOADOUTS`、要么在 `NOT_LOADED`。

### 全部可玩载具盘点（原版请求带来的载具；武器为各档位请求出现过的全部原版武器）

| 载具文件 | 名称 | 类 | 原版武器 | 现在的挂载 |
|---|---|---|---|---|
| `V505_TANK` | 布莱克战车 E1… | Vehicle505_Tank | 90/110 mm 滑膛炮（A 型，穿甲）、105–140 mm 榴弹炮 / 长距离榴弹炮（E 型）、热熔枪 | APFSDS、HE、LAHAT |
| `V505_TANK_EDF4` / `_EDF5` / `_EDF6BENEFITS` / `_MPACK2` | 布莱克 4.1 号 / 5 号 / 6 号 / E11 早期型 | Vehicle505_Tank | 散射榴弹炮 / 定时引信榴弹炮 / 140 mm 长距离榴弹炮 | APFSDS、HE、LAHAT |
| `V601_TANK` | 瓦利乌斯 TZ1… | Vehicle601_Tank | 120–140 mm 高压榴弹炮 | APFSDS、HE、LAHAT |
| `VEHICLE401_STRIKER` / `_MPACK2` | 格雷普装甲车 / 早期型 | Vehicle_Car | 榴弹炮、榴弹炮 S、滑膛炮、轻型速射炮 / 散弹速射炮 | AP 弹链、HE 弹链、AGM-114 |
| `V603_FLAK` | 克卜勒 / 沃卢斯 | Vehicle603_Flak | 高射机炮（V1–V3、HV）、高射炸药炮（DLC） | 近炸高爆弹、AIM-9X、AIM-120 |
| `VEHICLE402_ROCKET` / `V402_ROCKET_EDF6BENEFITS` | 奈格林导弹车 / 6 号 | Vehicle402_Rocket | 导引飞弹、聚合导引飞弹炮、导引粒子光束炮 | AIM-120、AGM-65、Hydra 70 |
| `VEHICLE403_TANK` | 艾普瑟隆电磁炮 | Vehicle403_Tank | 电磁炮各型、机枪（炮手） | AGM-114 |
| `VEHICLE404_BIGTANK` | 泰坦 | Vehicle404_Tank | 镇魂炮、滑膛炮 / 榴弹炮（副炮）、机枪、飞弹 / 榴弹发射器、腐蚀流射器 | AGM-114（驾驶员左扳机） |
| `V506_HELI` / `_EDF6BENEFITS` | N9 Eros / 6 号 | Vehicle506_Helicopter | 机炮 / 秃鹰炮、飞弹 / 连装烧夷炮 / 烧夷弹荚舱 | Hydra 70、AGM-114、AIM-9X |
| `V602_HELI` | Heron YG10 | Vehicle506_Helicopter | 机炮、飞弹（部分型号空） | Hydra 70、AGM-114、AIM-9X |
| `VEHICLE409_HELI` | Nereid | VehicleHelicopter409 | 自动捕捉加农炮 / 烈焰枪、火箭炮 / 无导引炸弹 | Hydra 70、AGM-114、AIM-9X |
| `V503_BIKE` / `_EDF6BENEFITS` / `_OMEGAZ` / `V613_BIKE` | 弗里德摩托各型 | Vehicle503_Bike | 轻型机炮 / 原力军刀 / 小型飞弹 | Hydra 70 |
| `VEHICLE410_HELI` | Brute HU04 | VehicleHelicopter410 | 速射炮、重机炮、火焰枪 | 不加：插件不为该类补造挂点 |
| `VEHICLE502_GROUNDROBO` | 深渊爬行者 | Vehicle502_GroundRobo | 格林机炮 + 双臂各种炮 | 不加：三件武器各有一个键，插件不为该类补造挂点 |
| `V504_BEGARUTA*`、`V608_OLDROBOT` | 贝加尔塔各色 / 旧型 | Vehicle504_begaruta | 双臂武器 | 不加：机甲双臂各一键 |
| `V612_NIX*` | Nix 各色 | Vehicle612_nix | 双臂武器 | 不加 |
| `V515_RETROBALAM*`、`V605_BARGA_CANNON` | 巴尔加各型 | Vehicle501_FortressRobo | 炮塔 / 卡巴破坏炮 | 不加：巨型机器人 |
| `V607_ROBOTRUCK` | 战斗货车 | Vehicle607_RoboTruck | 火焰喷射器、极限发射器 | 不加：插件不为该类补造挂点 |
| `V510_MASER` | EMC | Vehicle510_Maser | 原子光线炮 | 不加 |
| `V614_PROTEUS_MK2_CALL` | 普罗透斯 | VehicleBigBegaruta | 自带 | 不加：proteus 组管理 |
| `V507_RESCUETANK*`、`V512_KEITRUCK_BGP` | 救援车、轻卡车 | — | 无 | 不加 |

插件自己生成的载具：战机 / 无人机 / 炮舰机 / 潜水母舰的挂载由 `vcobjects.JETS`（`_FIGHTER` 4×AIM-120 + 2×AIM-9X、`_INTERCEPTOR`、`_MULTIROLE`、`_STRIKE` 6×AGM-65 + 38 火箭 + 6×Mk 82 + 2×AIM-9X、`_DRONE` 4×AGM-114、`_SHIP` 32×ESSM）早已按机型配齐，本次不变。**自行榴弹炮（`make_artillery`）和喀秋莎（`make_katyusha`）没有加第二弹种**：它们的炮弹是 EDF6AutoTurret 按高抛弹道瞄准的，额外挂点的炮弹要 AutoTurret 也认得才能瞄，这条链路不在本组文件内、离线也证明不了，未做。

### 测试

- 新增 `tests/test_stock_store_loadouts.py`（CMake `stock_store_loadouts`）：每台 `LOADOUTS` 载具有类别、带齐该类别要求的弹种（主战坦克必须有 AP、HE、炮射导弹且都挂主炮）；每种挂载都在 `src/stores.inc` 里、角色合法，炮弹类必须是 `gun`、五种语言名字齐全、由本工具写；轮换上限 `kMostPayload` 装得下；炮弹复制主炮后坐、导弹不复制、`AimRecoil` 不被复制。有游戏时：每台原版请求带来的载具要么有挂载、要么在 `NOT_LOADED` 写了原因；本工具写的每件炮弹与原版模板逐字段一致（只差载弹量和名字），AP 无爆炸且穿透，HE 有爆炸。变异实测：从瓦利乌斯的挂载里删掉 HE，测试报错。
- `tests/payload_runtime_test.cpp` 加两条：`gun` 角色的榴弹类炮弹（近炸高爆弹）会被 NPC 用来打飞机；非挂载的榴弹类仍不打飞机。
- `tests/test_stock_store_recoil.py`：炮弹的武器参数等于所挂原版武器的数对，导弹仍为 `[0,0]`。
- `tools/selftest.py`：`store_looks` 跳过炮弹（炮弹飞的是模板自己的弹体）；`stock_payload_and_seats_wired` 改为「每件挂载要么 make_jets 写、要么本工具写」。

### 未实测（需要进游戏确认）

- 坦克 / 格雷普 / 防空车多出来的挂点能造出武器、R / LB 能在原版主炮与 APFSDS / HE / LAHAT 之间轮换、HUD 显示中文名「APFSDS（穿甲弹）」等（`STORES v=… holder N built`、`PAYLOAD v=… picked`）。
- 炮弹从主炮口打出、后坐与原版主炮一致；近炸高爆弹从飞机旁 4 m 内掠过时引爆（接触球推断，M）；伤害随请求档位放大（M）。
- 近炸高爆弹从左炮一门打出（一个挂点只有一门炮），射速是模板的 5 帧一发，不是双联炮的两倍。
- 近炸高爆弹在载具 HUD 上显示为榴弹（`rounds.cpp` 按弹类把它归为 lobbed，图标和落点十字是 aim / HUD 组的区域，本组未改）。

## 反馈 2：「任务包里面没有显示 edf5 的主线和两个 dlc」

### 根因

- 旧版 `tools/make_edf5_campaign.py:60,218-241`（b89d26a）把 135 关**追加到 EDF6 本篇离线列表**（`MISSIONLIST.OFFLINE.LIST.SGO`）第 147 行之后，从没注册成任务包。玩家说的「任务包」是离线模式里「离线任务模式」弹出的对话框（标题键 `RoomFilter_ContentsName`），它列的是 `DefaultPackage/config.sgo` ModeList 里的离线模式，追加的行不会出现在那里。
- 本篇选关遇到第一个「未通关且仍锁定」的行就不再往下显示（`EDF.dll 0x8A2330..0x8A237E`），没打通 EDF6 的玩家在本篇选关里也看不到第 147 行以后（`docs/mission-list-re.md` §3 原写「开放行」，已更正）。
- 只往 ModeList 加模式还不够：对话框要过「是否拥有」判断 `0xD92B0(mgr, 内容编号)`，查 `mgr+0xE0` 集合，里面只有 0 和平台已购 DLC 的编号（`0xDBB50`、`0xDA600`、`0xDBD40`），新编号会显示成灰色「尚未购买」。内容编号也不能复用 0：`GetModeNo` 和对话框选中都按「（模式类型, 内容编号）取第一个」（`0x70E9D0`、`0x8C4CA0`），会被解析成 EDF6 本篇。细节与置信度见 `docs/mission-list-re.md` §7。

### 修法

- 安装器 `tools/make_edf5_campaign.py`：在 `CONFIG.SGO` 的 ModeList 末尾追加 3 个离线模式（EDF5 本篇 110 关、DLC1 11 关、DLC2 14 关），原有 6 项一字不动；底板优先用 `Mods/` 里别的 MOD 已有的那份。内容编号接在最大编号之后（原版即 3、4、5），存档 `E5M0` / `E5D1` / `E5D2`，撞名拒装；难度数值照抄 EDF6 本篇 / 任务包 1 / 任务包 2 的离线模式。每包独立的列表、5 种语言文本、缩略图；包名和说明写进 `ETC/TEXTTABLE_STEAM.<语言>.TXT_SGO`（其余键不动）。全部写入走 `Mods/.edf5campaign.json`（version 2），卸载还原；模式表被别人改过时整组保留并取消卸载。
- 旧版升级：把旧版改过的本篇列表 / 文本 / 缩略图还原到安装前，中断后可续；旧列表被别人改过时拒绝升级；ini 旧键 `EDF5CampaignRows` 置 0。**旧版记在 `M00.MST` 第 147 行以后的 EDF5 通关记录不会迁移到新包存档。**
- 插件 `src/edf5campaign.cpp`：删掉旧的 11 处行数 / 进度补丁（各包用自己的列表，不再需要）；改为把 `0xD92B0` 的全部 7 处调用换成「原生为真，或编号落在 ini `EDF5CampaignContent`..+2 内即算拥有」。照旧先核对每处都是原版 call、全部改完才生效；`EDF5CampaignContent=0`（没装）时完全是原生行为。没装插件时 3 个包仍会列出，但显示「尚未购买」。
- 副作用：EDF5 包打通最后一关不放结局（游戏脚本只给内容 0~2 放结局）；EDF6 本篇的列表、存档、结局、通关率、成就都不受影响（统计只在内容 0 时提交，`0xDD1D9`..`0xDDB7E`）。

### 测试

- `tests/test_edf5_campaign_lifecycle.py`：25 条（含旧版升级还原、升级中断、旧列表被改时拒绝、各包文件与存档名唯一）。
- `tools/selftest.py` 的 `edf5_campaign_*`（真实 Root.cpk）：ModeList = 原 6 项 + 3 个离线包，编号 3/4/5，（类型, 编号）与存档名唯一，难度来自对应原版模式；5 种语言文本表都有 3 个包的名字和说明且原文本不变；每包列表与文本行数一致、每行带 `flags`、后继成链、缩略图齐全、BVM 都在；关数 110/11/14，跳过 DM015/018/019/020；与别的 MOD 共存（编号避让、卸载字节级还原）；`0xD92B0` 调用点恰为 7 处（变异：删一处即红）。
- `tests/edf5_campaign_native_test.cpp`：在私有映射的 `EDF.dll` 上用伪造拥有集合跑原生判断和 7 个改写调用点（0 拥有、1/2 不拥有、3–5 拥有、6 不拥有），并测改写中途失败保持原生结果。

### 还没在游戏里实测

- 「任务包」对话框里 3 个包的名字、说明、图片、能否选中；进包后读写 `DEFP_E5*.MST`。
- `Mods/ETC/TEXTTABLE_STEAM.*.TXT_SGO` 能否被 ModLoader 读到（读不到时包名显示为文本键，不会崩）。`DEFAULTPACKAGE` 可被覆盖有旁证（本机 `Mods-20261001` 里护甲倍率 MOD 的 CONFIG.SGO；存档里存在 `DEFP_MOD1.MST` / `DEFP_MOD3.MST`，说明自定义 MST 的模式曾被游戏接受）。
- EDF5 BVM 能否跑到结算。
- 内容编号 ≥3 在其它代码路径的影响：已逐处看过读该编号的地方，未逐个证明（M）。
- 之后若再装会整份覆盖 `CONFIG.SGO` 的 MOD，3 个包会消失（不崩），需重跑安装器。

## 构建与测试（本分支）

- 构建：`tools\msvc-x64-env.cmd` + `cmake -G Ninja RelWithDebInfo`，`cmake --build -j 4` 与 `--target offline_checks` 均通过（/W4 /WX）。
- `ctest -j 3`：180 项中 179 过；`support_soldier_native`（support 组的原生审计）间歇性 access violation，单独重跑 3 次 2 过 1 败，与本分支改动无关。
- `python tools/selftest.py`：131/131。
