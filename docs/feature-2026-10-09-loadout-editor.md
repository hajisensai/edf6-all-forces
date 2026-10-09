# 2026-10-09 新功能：游戏外编辑支援预设（编组 / 每人兵种 / 颜色 / 坦克弹种）

分支 `feat/fb1009d-loadout-editor`，基于 `fix/fb1009b-rts`（07c92d6）。
**本文所有内容只做了离线验证**：编译、离线 ctest、只读 Root.cpk 生成并逐字段核对。没有启动游戏，没有往游戏目录写任何文件。

用户原话：「在游戏外可以编辑小组，还有载具、npc的挂载，npc的外观颜色等。例如可以给我叫的坦克全选ap弹，也可以一半ap一半he」

## 1. 可行性（每项都有原版数据或代码依据）

| 项 | 结论 | 依据 |
|---|---|---|
| a. 小队编制（兵种组合、人数） | **能做，已实现** | 断剑式编组已有：`support_call.h SupportLoadout`（最多 12 人，每 4 人第一位是队长），联机经 `kCapLoadout` 同步（`docs/feedback-2026-10-09c-bar.md` §1）。这次只是把编组的来源加上「游戏外预设」。 |
| b. 支援 NPC 的武器 | **能做原版 AI 武器（14 种），已实现；玩家武器表里的武器不能做** | 见下 §1.1 |
| c. 支援坦克 AP / HE、按车分配 | **能做，已实现**（AP 是同一车体换原版布莱克 A 型的炮） | 见下 §1.2 |
| d. NPC 外观颜色 | **能做（按每个士兵），已实现；只在本机独自部署时生效** | 见下 §1.3 |

### 1.1 NPC 武器

- 支援士兵是用原版 AI 兵模板 `CreateObject` 出来的（`src/support_soldier.cpp kBodies`），武器写在模板里：`soldier_load_weapon = ['app:/weapon/AiSoldierRifle01.sgo']`（Root.cpk `N601_COMMON_RANGER_AF.SGO`，只读 dump）。
- Root.cpk 全部 NPC 模板的 `soldier_load_weapon`（扫了 OBJECT 下所有带该字段的 SGO）**全是 `Ai*` 武器**，没有一个 NPC 用玩家武器表的武器。
- AI 武器和玩家武器同类（都是 `Weapon_BasicShoot`），但数值格式不同：AI 版是标量（`AiSoldierRifle01`：`AmmoCount = 40.0`），玩家版是按星级插值的数组（`ASSULTRIFLE01`：`AmmoCount = [120.0, 0.0, 0.0, 7.0, 0.5, 0.5, 0.0]`）。NPC 的武器初始化怎样解释这种数组没有逆向过。原版也没有这样用的先例。改了 SGO 出问题时，会在玩家游戏里构造出数值错误的武器对象，所以**不做**。
- 所以「每人武器」= 选兵种：5 种游骑兵（步枪 / 火焰 / 火箭 / 霰弹 / 狙击）、5 种翼人（长矛 / 激光 / 狙击 / 雷链 / 雷弓）、4 种重装（重炮 / 中炮 / 打桩 / 霰弹）。这 14 种就是已有的 `SupportWeapon`，各自对应原版模板和它自带的 AI 武器，联机照常同步。
- 原版还有几种 AI 武器没进这张表（`AiSoldierLaserRifle01/02`、`AiSoldier_CommonBlazer01`、`AiHeavyHammer01`、精英翼人）。它们可以按同样方式加成兵种，但要动线格式（兵种 4 位，现有 14 种，最多 16 种）。本轮没加。

### 1.2 坦克 AP / HE

- 支援坦克是 `V505_TANK_MISSION.SGO`（`src/support_spawn.h`），炮由它的 `mission_setup[2]` 决定（`ApplySetup` 读 `0x62D6E0`，`docs/mission-airstrike-re.md`）。原版这辆用的是 `v_505tank_cannon01`，也就是布莱克 E1 的 105 mm 榴弹炮：`RocketBullet01`，爆炸半径 8 m，不穿透。**这就是 HE**。
- 原版的 AP 不是弹种参数，而是**另一个车辆配置**：原版请求「布莱克 A1」`EWEAPON419.SGO` 的 `Ammo_CustomParameter[4][3][2][0]` = `['app:/weapon/v_505tank_cannon01s.sgo', [0.1, 0.4], [25, 0.1, 0.1]]`（90 mm 滑膛炮：`SolidBullet01Rail`，穿透，无爆炸）。E1 用的是同一个车体，只是换了这门炮（只读扫描所有用这两门炮的请求武器）。
- 做法：安装器生成 `EDF6VC_SUPPORT_TANK_AP.SGO`。它就是 `V505_TANK_MISSION`，只改了两处：
  - `mission_setup` / `vehicle_setup` 的炮位换成 A1 那一整条挂载（含后坐数对，与 `autoturret/tools/npc_recoil.py`「任务车用玩家请求的后坐」同一规则）；
  - `resource` 加上这门炮。

  基底优先取 Mods 里别的工具已经改过的那份（AutoTurret 的后坐、实体瞄具的模型重定向），没有就取 Root.cpk。
- 「一半 AP 一半 HE」：一次呼叫只来一辆坦克（规划器一个计划只有一个车体），所以按**本关第几辆**分配：
  - 列表 `AP,HE,HE`：逐车、循环；
  - 比例 `AP:1,HE:1`：每辆选「到目前为止最欠份额」的弹种，同分取先写的，结果是 AP、HE、AP、HE……（任意前 n 辆里每种与 n·比例相差不到一辆）。

  计数只算本机部署成功的坦克，「坦克·有人」和「坦克·空车交付」一起算。新关卡从第一辆重新数。

### 1.3 NPC 颜色

- 模板里的 `soldier_color`：每项是 `[[[网格 或 0, 'change_color0' | 'change_color1']], [R, G, B, A]]`。例如：
  - 游骑兵：`change_color0` = (0.159, 0.215, 0.148) 军绿，`change_color1` 偏棕；
  - 翼人：`change_color0` 深蓝，`change_color1` 0.8 灰白；翼人队长另有 `p606_face_dx11` 的脸部一项；
  - 游骑兵队长按头 / 身体分项。

  颜色属于模板，所以「按单位设置」= 每种（兵种, 是否队长, 主色, 副色）生成一份模板副本 `EDF6VC_NPC_<兵种>[_L]_<主色|X>_<副色|X>.SGO`。副本只改 `soldier_color`：`change_color0` 用主色，`change_color1` 用副色，脸部那项不动。生成时逐字段核对，除 `soldier_color` 外与原版模板完全相同。
- 运行时直接改士兵对象颜色的内存位置没有逆向，不做。
- **联机限制**：插件的士兵资源 id 不带模板路径，对端按 id 用原版模板生成。要让对端也用彩色模板，需要一个新的能力位来确认「对端也有这些文件」。hello 的能力位都已占用：`support_protocol.h` 里 `kCapabilities = 1|2|4|8 = 15`，而 `static_assert(kCapabilities < kMaxUnits)` 中 `kMaxUnits = 16`。所以颜色和 AP 坦克**只在本机独自部署时生效**（离线，或房间里只有自己的房主：`LocalAuthority`）。房间里有别人时：
  - 兵种和编组照常同步，只有颜色是原版；
  - 坦克用 HE；
  - 两种情况都写日志说明。

## 2. 配置（唯一真相源）：`EDF6VehicleCrew.ini` 的 `[VehicleCrew]`

插件每次 ini 保存后都会重读。语法写在 `src/support_loadout.h` 文件头；`tools/support_loadout.py` 用同一套校验，`tests/support_loadout_ini_test.py` 保证两边一致。

```ini
; 坦克全部 AP
SupportTankRounds=AP
; 一半 AP 一半 HE（按本关第几辆交替：AP, HE, AP, HE ...）
SupportTankRounds=AP:1,HE:1
; 同样是一半一半，写成逐车列表
SupportTankRounds=AP,HE
; 三成 AP、七成 HE
SupportTankRounds=AP:30,HE:70
; 逐车列表：第 1 辆 AP，第 2、3 辆 HE，然后循环
SupportTankRounds=AP,HE,HE

; 直升机机降大队：蓝色翼人长矛小队 + 重装重炮小队 + 白色副色的步枪小队
SupportPreset_PLATOON_HELI=lance@1E3A8A*4,cannon*4,rifle@X:FFFFFF*4
; 步兵小队：两名红色步枪兵、火箭、狙击（第 1 名是队长）
SupportPreset_SQUAD=rifle@C00000*2,rocket,sniper
; 装甲运兵车（有人）的 4 名乘客
SupportPreset_TRANSPORT_CREWED=pilebanker*2,fshotgun*2
```

- `SupportPreset_<单位>`：单位为 `SQUAD PLATOON SQUAD_HELI PLATOON_HELI SQUAD_AIRDROP PLATOON_AIRDROP`（最多 12 人），以及 `TRANSPORT_CREWED TRUCK_CREWED`（4 名乘客；驾驶员仍是 `SupportVehicleCrewWeapon`）。
  - 每项写成 `兵种[@主色[:副色]][*人数]`，按座位顺序排，每 4 人第一位是队长。
  - 颜色是 6 位十六进制，`X` 表示保留原色。
  - 兵种：`rifle flame rocket shotgun sniper lance laser monster izuna thunderbow cannon midcannon pilebanker fshotgun`。
- 套用：
  - 地图支援栏的编组面板以预设为初始编组，在面板里改动的座位按原版外观；
  - 不经面板的呼叫（无线电武器、C 键）直接带预设；
  - 客机的预设随请求发给房主（`kCapLoadout`）。
- 回落（Never break userspace）：
  - 键不写或留空：与以前完全相同，按 `SupportSquadWeapon` 等配置出兵，坦克用 HE；
  - 预设写错：语法错、超过座位、给坦克这种没有座位的单位写预设。整条预设作废、按该单位默认兵员出兵，并写明是哪个键、为什么。HUD 第一次呼叫时显示「支援配置有误：…」，日志 `CONFIG support ... INVALID: ...`；
  - `SupportTankRounds` 写错：全部按 HE 出动，提示方式同上；
  - 彩色模板或 AP 文件缺失（还没生成，或在本关开始之后才生成）：该士兵 / 坦克按原版出动。日志 `SUPPORT look ... not generated` / `SUPPORT ground: EDF6VC_SUPPORT_TANK_AP.SGO not installed` / `SUPPORT plan ... wants AP but ...: stock HE`；
  - 生成的 AP 车体在游戏里类 / 座位 / setup 不符：只停用 AP，原版坦克照常可呼叫。日志 `... AP disabled until restart`。

## 3. 编辑器（游戏外）

仓库原有的安装器是控制台菜单（`tools/installer.py`，打包成 `EDF6VehicleCrew安装器.exe`），没有 GUI。按惯例扩展它：

1. 运行安装器，输入 **7**（配置地图支援）。
2. 输入 **l**，进入「编辑支援预设」：
   - 列出 8 个可编组单位当前的预设，按小队分组显示；
   - 也列出坦克弹种，以及按它算出的前 8 辆分别是什么弹种。
3. 输入单位编号后写预设（屏幕上有写法说明），`-` 清除；输入 **t** 编辑坦克弹种。
   - 非法值会直接拒绝并说明原因，不会写进 ini；
   - 合法值会规范化后写入，例如连续相同的士兵合并成 `*n`。
4. 回车返回，再回车保存。
   - 游戏**关闭**时，保存后立即生成需要的文件：带颜色的士兵模板、AP 坦克。用 `pylib/ledger.py` 记账（owner `loadout`），不再需要的旧文件同时删掉；
   - 游戏**运行中**时只保存 ini，并提示退出游戏后再进菜单 7 保存一次（或选 1 安装）。在这之前兵种和编组照常生效，颜色和 AP 按原版出动。

安装（菜单 1）也会按玩家的 ini 生成这些文件，顺序在 AutoTurret 和实体瞄具写完 `V505_TANK_MISSION` 之后。卸载选 1 时一起删除。开发者也可以直接运行：

```
python tools/support_loadout.py [游戏目录]            生成 / 刷新
python tools/support_loadout.py [游戏目录] --remove   删除
```

## 4. 插件数据流

| 数据 | 拥有者 | 说明 |
|---|---|---|
| `SupportConfig::preset[catalog]` / `tankRounds` | `support_config.cpp`（ini 解析后发布） | 解析用 `support_loadout.h` 的 `ParseSupportPreset` / `ParseRoundMix`，按 `SupportCallSeats` 校验座位 |
| 本关的彩色模板表（最多 15 种） | `support_soldier.cpp` | 关卡开始时 `PreloadSupportLooks`（`support_dispatch.cpp`，在 `PreloadSupportSoldiers` 之后）只把文件存在的模板加入预载 |
| 士兵资源 id 第 12–15 位 = 模板表下标 | 计划（`support_call.h`） | 只在 `LocalAuthority` 时由 `ComposedResource` 写入。`Validate` 拒绝别的机器发来的带模板下标的计划；`Body()` 找不到对应模板时用原版 |
| 坦克车体 id 第 8 位起 = 弹种 | 计划 | `PlanTankRound`：本机部署且 AP 已预载时才写；`SpawnSupportVehicle(..., round)` 用 AP 路径；`tanksCalled` 每关清零 |

## 5. 测试

| 测试 | 内容 |
|---|---|
| `support_loadout`（新，C++，403 项） | 14 个兵种名；预设语法（颜色、X、`*n`、全角逗号、超 12 人、超座位、无座位、各种格式错误整条拒绝）；面板改过的座位按原版外观；弹种：全 AP、逐车列表循环、`*n`、1:1 前 40 辆每个前缀都是 ⌈n/2⌉ 辆 AP、百分比、5 组比例在前 60 辆的每个前缀都不差一辆、各种错误；文件名与 CreateObject 路径；士兵 id 的模板位不影响兵种和队长判断 |
| `support_loadout_ini`（新，Python，95 项） | 兵种 / 模板 / 弹种 / 文件名 / 座位数与 C++ 一致；同一批值两边结论相同；编辑器脚本化操作：写入合法预设、拒绝 13 人、写弹种、`-` 清除、保持 CRLF；有游戏时用 Root.cpk 只读生成：彩色模板除 `soldier_color` 外与原版逐字段相同，主色写入、副色 X 与脸部保持原色；AP 坦克除两处炮位和 `resource` 外与 `V505_TANK_MISSION` 相同，炮是穿透、无爆炸的 `SolidBullet01Rail` |
| `support_dispatch_test`（加） | 从真实 ini 读预设和弹种；给坦克写预设会被拒绝并点名；关卡开始预载 3 个彩色士兵、文件缺失时不预载；预设作为面板初始编组；无线电呼叫带预设，离线时彩色士兵带模板位、无色的为原版；面板改过的座位为原版；模板未预载时为原版；带模板位的计划在有对端的房间被 `Validate` 拒绝；`AP:1,HE:1` 第 1 / 2 / 3 辆为 AP / HE / AP（空车交付也计数）；AP 文件缺失时用 HE；单人房主可用 AP，有对端时 AP 车体 id 被拒；运兵车不接受弹种；新关卡从第一辆重新数 |
| `support_spawn_test`（加） | 没有 AP 文件时只有 HE，带 AP 的生成请求在任何构造之前就被拒绝；有文件时与原版三种一起预载；AP 用生成的 SGO、HE 用原版、运兵车忽略弹种；AP 车体类别不对时只停用 AP，原版坦克可用，跨关卡保持停用 |
| `support_config`（改） | 新增 `SupportCallSeats` 桩；配置日志仍是一行，并写明预设数和弹种 |
| `tools/selftest.py`（改） | 安装后 loadout 文件写入、从已安装的 ini 生成、卸载选 1 后删除（整机目录比对）；`SupportPreset_` 在随附 ini 里有示例；PyInstaller 隐式导入加入 `support_loadout` |
| `tools/test_installer_incremental.py`（改） | 第二次安装照常从 ini 重建 loadout 文件，但内容没变就不重写（时间戳不变） |

全量：`cmake --build build` 与 `offline_checks` 0 警告；`ctest -j 3` 195 / 195 通过。

`tools/selftest.py` 另有 4 项在基底 `fix/fb1009b-rts`（07c92d6）上就失败，与本分支无关，本轮没有动：

- `every_npc_aircraft_boardable`：运输机 `transportPlane` 不在 `kBoardable` 里；
- `npc_ai_wired`；
- `hud_text_localized`：`src/hud.cpp` 有一个字面量 `Shift`；
- `soft_edge_wired`。

## 6. 没在游戏里验证的（如实）

1. 彩色模板：
   - 生成的副本经插件 `CreateObject` 后能否正常生成；
   - 颜色观感：游戏可能把 `soldier_color` 当线性值使用，十六进制 /255 写进去后看起来会比网页色偏亮。
2. AP 坦克：
   - `EDF6VC_SUPPORT_TANK_AP.SGO` 生成后开火是否为滑膛炮弹道；
   - NPC 驾驶时的瞄准：AutoTurret 的瞄准按弹种区分，滑膛炮初速 1080 m/s、重力 0.1，与榴弹炮差很多。
3. 预载时机：在本关开始之后才生成的文件要到下一关才生效（设计如此，但未实测）。
4. 联机：颜色 / AP 在有对端时回落原版，未双机实测。
5. 本机游戏目录里没有用这些文件跑过任务（按约束未安装）。
