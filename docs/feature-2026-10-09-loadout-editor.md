# 2026-10-09 新功能：游戏外编辑支援预设（编组 / 每人兵种 / 颜色 / 载具挂点）

分支 `feat/fb1009d-loadout-editor`，基于 `fix/fb1009b-rts`（07c92d6）。

**本文所有内容只做了离线验证**：
- 编译与离线 ctest；
- 只读 Root.cpk 和本机已安装的 Mods 文件，生成后逐字段核对；
- 用替身对端跑完整的联机协议状态机。

没有启动游戏，也没有往游戏目录写任何文件。

## 用户原话

- 第一次：「在游戏外可以编辑小组，还有载具、npc的挂载，npc的外观颜色等。例如可以给我叫的坦克全选ap弹，也可以一半ap一半he」
- 纠正一：「我说的是战前配置载具的挂载分布。比如我飞机可以全带炸弹，或者全带导弹，或者带机炮」
  - 一半 AP 一半 HE 指**同一辆坦克同时带两种炮弹**，不是按本关第几辆轮流分配。
  - 第一版的 `SupportTankRounds` 按车轮流方案已删除，不保留两套。
- 纠正二：颜色和载具变体**必须联机生效**。不接受「能力位用满所以联机退回原版」。

## 1. 可行性（每项都有原版数据或代码依据）

| 项 | 结论 |
|---|---|
| a. 小队编制（兵种组合、人数） | 能做，已实现 |
| b. NPC 武器 | 能用原版 AI 武器（14 种），已实现；玩家武器表的武器不能给 NPC |
| c. 载具挂点（每台载具每个挂点配武器） | 坦克、四种战机能做，已实现；直升机不做 |
| d. NPC 颜色 | 能做（按每个士兵），已实现，联机生效 |
| e. 联机同步颜色与载具变体 | 能做，已实现 |

### 1.1 编制（a）

断剑式编组 `SupportLoadout` 原来就有：最多 12 人，每 4 人一个队长，经 `kCapLoadout` 联机同步。这次只是把编组的来源加上游戏外预设。

### 1.2 NPC 武器（b）

- 支援士兵用原版 AI 兵模板生成，武器写在模板里：`N601_COMMON_RANGER_AF` 的 `soldier_load_weapon = ['app:/weapon/AiSoldierRifle01.sgo']`。
- Root.cpk 全部 NPC 模板用的都是 `Ai*` 武器，没有一个 NPC 用玩家武器。
- 玩家武器的数值是按星级插值的数组（`ASSULTRIFLE01` 的 `AmmoCount = [120,0,0,7,0.5,0.5,0]`），AI 武器是标量。NPC 怎么解释数组没有逆向过，所以不做。
- 能选的是 14 种兵种（`SupportWeapon`）：5 种游骑兵、5 种翼人、4 种重装，各对应原版模板和它自带的 AI 武器。

### 1.3 载具挂点（c）

**挂点结构（只读 SGO + 插件原有代码，置信度 H）**

- 挂点行是 `vehicle_weapon_setting`，每行 `[骨骼, 键位]`。
- 武器表放在设定里：
  - 地面车是 `mission_setup[2]`，直升机 / 战机是 `[3]`；
  - 每项 `[武器 SGO, 后坐数对, 炮塔参数]`；
  - 数值项（如 `0.0`）表示空挂点，原版 Heron YG10 就有。
- 各类的第 46 槽只造自己那几个挂点，505 坦克只造 1 个（`docs/stock-payload-re.md` §4.1）。
- 插件 `stores.cpp InstallBuilds` 在原版造完后按同一张表补造其余挂点：它找「项数 = 挂点数」的那张表。
- 支援坦克生成时，`support_spawn.cpp ApplySetup` 调的正是第 46 槽。
- 战机是 506 机体，插件补丁把建造循环上限改成挂点数（`stores.cpp`）。

**原版的 AP / HE 是车辆配置，不是弹种参数**

- 布莱克 E1（`EWEAPON418`）的炮是 `v_505tank_cannon01`：105 mm 榴弹炮，`RocketBullet01`，爆炸 8 m，25 发。
- 布莱克 A1（`EWEAPON419`）在同一车体上换成 `v_505tank_cannon01s`：90 mm 滑膛炮，`SolidBullet01Rail`，穿透、无爆炸，30 发。
- 支援坦克 `V505_TANK_MISSION` 用的就是 E1 那门炮。

**一辆坦克同时带 AP 和 HE（用户要的「一半一半」）**

- 主炮仍是原版那门（HE 榴弹炮，或换成 A1 的 AP 滑膛炮挂载）。
- 每个挂点再加一条武器表项和一行挂点行，挂已有的炮弹挂载 `EDF6VC_AP_<n>` / `EDF6VC_HE_<n>` / `EDF6VC_GLM_<n>`。这几种是 `vcobjects.STORES` 的 Shell，由原版炮弹原样拷贝、只改弹数。
- 炮弹挂点的后坐与主炮相同（与 `make_stock_stores` 同一规则）。
- 这和原版坦克请求早已用上的「额外挂载」是同一机制：`make_stock_stores` 给 MBT 挂 APFSDS 20 / HE 20 / LAHAT 4。

**谁来切换**

- 玩家：R 键（手柄 LB）轮换主炮和各个挂点（`payload.cpp`）。
- NPC 驾驶员：`npcai.cpp DriverPayloadAim → payload.cpp NpcPayloadSelect`，每帧按目标选：
  - 飞行目标不用榴弹类；
  - 地面目标的优先级：制导弹（距离 150 m 以上）> 有爆炸的（HE）> 穿甲；
  - 太近的 HE（2 倍爆炸半径内）不用，射程不够的不用，打空了换下一个。

  所以「HE,AP:25」的坦克，NPC 会先用 HE，近距离或 HE 打完后用 AP。前提是 ini 的 `StockVehicleStores=1`（默认开）和 `NpcGunners`；关掉时 NPC 只用主炮。

**战机挂点**

- 插件战机的武器表是：机炮 L / R、挂载、油箱在第 4 位（`EDF6VC_JET_*.SGO`，`vcobjects.with_fuel`）。只读核对过本机已安装的四种战机。
- 变体保留机炮和油箱，挂点按用户的选择重排；至少 4 个挂点，「只带机炮」时补一个原版式空挂点。
- 战机最多 5 个挂点：机炮 2 + 油箱 1 + 5 = 8 个挂点，正好是 `jet_combat.cpp ReadArms` 能读的上限（8）和 `kMostStores` 7。坦克最多 4 个：`payload.h kMostPayload` 8。

**战机 AI 会不会用多条武器**

- 会，并且按目标类型选：
  - `jet_combat.cpp PickStore`：打飞行目标用空空导弹，打地面用空地导弹；
  - 地面目标走俯冲攻击，期间扔炸弹（`Bomb`）、打火箭（`TriggerStore`）；
  - 机炮一直可用。
- 原来**选目标**只看机种：战斗机优先打飞的，攻击机优先打地面，多用途机就近。这样「全带炸弹的战斗机」会先去追飞行目标。
- 本次新增 `LoadoutPrefer`（`jet_internal.h`），按挂载里还有弹的种类决定优先打哪类目标：
  - 只剩空空弹：优先打飞行目标；
  - 只剩对地弹 / 炸弹 / 火箭：优先打地面；
  - 混挂、只有机炮、或全部打空：按机种原来的偏好。

  原版各机种的默认挂载都不受影响：战斗机 / 截击机全是空空，攻击机 / 多用途机是混挂。

**直升机不做**

- NPC 直升机驾驶员只按原版开火字节打挂点 0–2（`heli.cpp kStoreHolder = 2`），多出来的挂点 NPC 永远不会开火。
- 410 类插件根本不为它补造挂点（`make_stock_stores NOT_LOADED`）。

### 1.4 NPC 颜色（d）

- 模板的 `soldier_color` 是若干项 `[[[网格 或 0, 'change_color0' | 'change_color1']], [R,G,B,A]]`，例如：
  - 游骑兵：`change_color0` 军绿，`change_color1` 偏棕；
  - 翼人：`change_color0` 深蓝，`change_color1` 0.8 灰白；翼人队长另有一项脸部 `p606_face_dx11`。
- 每种（兵种, 队长与否, 主色, 副色）生成一份模板副本，只改 `soldier_color`，脸部那项保留。
- 运行时直接改对象颜色的内存没有逆向过，不做。

### 1.5 联机同步（e）

**旧对端会忽略的字段**

- `support_protocol.h` 的 hello `index` 已经没有空位：`kCapabilities` 必须小于 `kMaxUnits` 16，现已用到 15。
- 但 hello 消息自带的 `unit` 部分，收发双方从来不读：`ValidMessage` 只在 unit 消息里校验 unit。
- unit 消息的 `challenge` 字段（64 位）也从没用过。
- 旧版发来的这些位置都是 0，收到我们的值也不看。线格式仍是 v2，消息长度不变。

**做法**

1. **hello 扩展**：
   - `unit.resourceId` = 扩展能力字，`kExtVariants`；
   - `unit.netId`（32 字节）= 本机本关已预载的变体文件的 Bloom 过滤器（256 位，3 个探针，FNV-1a 64 哈希）。
   - 房主每次收到 hello（客机每秒发一次）就更新：`Session::PeersHaveVariant`。
2. **每个计划单位带 64 位变体**（`Unit::variant`，放在 unit 消息的 `challenge` 里），能完整解码出它是什么：
   - 士兵颜色：主 / 副色各 24 位，加 2 个标志位；
   - 载具挂点：挂点数、AP 主炮位、每个挂点 4 位挂载码 + 4 位弹数码；
   - 最高位 `kVariantApplied`：这个单位按变体文件生成。不置位时，只是告诉对端房主想要什么，单位本身按原版生成。
3. **文件名由内容决定**（`EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO`、`EDF6VC_LO_TANK_4000000000000C81.SGO`），所以：
   - 两台机器只要配置相同，生成的就是同名、同内容的文件；
   - 对端只凭文件名就能重新生成，不需要传文件。
4. **房主规划时**：本机有这个文件、全员都宣告了 `kExtVariants`、并且每个对端的过滤器里都有它，才置 `kVariantApplied`。否则单位按原版生成，并且：
   - 写日志；
   - 在支援状态旁提示，例如「房间里有人还没有 X：本次按原版出动；对方退出游戏后运行安装器菜单 7 即可自动生成」。
5. **对端收到计划后**：
   - 自己缺的变体文件，名字追加到 `Mods/Plugins/EDF6VehicleCrew.variants_pending.txt`。安装器（菜单 7 保存或选 1）按名字生成，生成后从清单删掉。下一局 hello 的过滤器里就有它了。
   - 对端缺**载具变体**文件时，`Validate` 拒绝整个计划。武器表和房主不同，就不是同一台车，硬生成可能让双方的挂点对不上。房主那边提示「对端缺少…」，对端写进待生成清单。
   - 对端缺**士兵颜色**文件时（Bloom 误报，约 2% 以内），只把那名士兵按原版颜色生成：兵种、武器、AI 都一样，只是外观不同。

**为什么不选另外两条路**

- 运行时生成：SGO 要从 Root.cpk 读模板、按格式写回，战机还依赖 make_jets 的产物。把这套搬进 C++ 插件，量大且风险高。
- 联机时传文件：EDF6Coop 只有固定 176 字节的可靠消息通道。

所以选「内容寻址文件名 + 过滤器预检 + 待生成清单」：
- 每台机器只用自己安装器生成的文件；
- 房主在规划前就知道对端有没有，不会出现半个房间看到彩色、半个房间看到原版的情况；
- 缺的文件下次自动补上。

## 2. 配置（唯一真相源：`EDF6VehicleCrew.ini` 的 `[VehicleCrew]`）

```ini
; 坦克全选 AP：换成 A1 的 90mm 滑膛炮（30 发），再挂 30 发 AP
SupportVehicle_TANK_CREWED=AP,AP:30
; 同一辆坦克一半 AP 一半 HE：原版 105mm 榴弹炮 25 发 HE + 一个挂点 25 发 AP（R 键 / NPC 自动切换）
SupportVehicle_TANK_CREWED=HE,AP:25
; 再加 4 发炮射导弹
SupportVehicle_TANK_CREWED=HE,AP:25,GLM:4

; 战斗机全带炸弹 / 攻击机全带导弹 / 多用途机只带机炮
SupportVehicle_FIGHTER=MK82:6,MK82:6
SupportVehicle_STRIKE=AGM:6,AAM_S:2
SupportVehicle_MULTIROLE=GUNS

; 小队：两名蓝色翼人长矛（第 1 名是队长）、一名重装重炮、一名副色白的步枪兵
SupportPreset_SQUAD=lance@1E3A8A*2,cannon,rifle@X:FFFFFF
```

**坦克**
- 适用的键：`TANK_CREWED` / `TANK_DELIVERY`。
- 第一项是主炮 `HE` 或 `AP`，之后每项一个挂点：`AP` / `HE` / `GLM`，写成 `名称:数量`，最多 4 个挂点。

**战机**
- 适用的键：`STRIKE` / `FIGHTER` / `INTERCEPTOR` / `MULTIROLE`，各自还有跟随版 `_F`。
- 机炮一直在，每项一个挂点，最多 5 个：
  - 空空：`AAM_S` / `AAM_M` / `AAM_L`；
  - 空地：`AGM` / `AGM_L`；
  - 炸弹：`MK82`；
  - 火箭巢：`RKT`。
- `GUNS` 表示只带机炮。

**弹数**可选 1 2 3 4 5 6 8 10 12 15 19 20 25 30 38 40。

**小队预设**
- 写成 `兵种[@主色[:副色]][*人数]`。
- 兵种：`rifle flame rocket shotgun sniper lance laser monster izuna thunderbow cannon midcannon pilebanker fshotgun`。
- 颜色是 RRGGBB，`X` 表示原色。

**回落**
- 不写或留空：与以前完全相同。
- 写错（语法、挂点数、坦克上挂炸弹、给没有载具的单位写挂点等）：整条作废，按原版出动，并点名是哪个键、为什么：
  - HUD：「支援配置有误：…」；
  - 日志：`CONFIG support ... INVALID: ...`。
- 文件缺失、旧版对端、对端缺文件：按原版出动，日志加状态提示（见 §1.5）。
- 生成的载具文件在游戏里类 / 座位 / setup 不符：只拒绝这个变体，原版车仍可呼叫。日志 `... is not the stock hull's class / seats / setup`。

## 3. 编辑器（游戏外）

仓库原有的安装器是控制台菜单（`tools/installer.py`，打包成 `EDF6VehicleCrew安装器.exe`），没有 GUI，所以在它上面扩展。

1. 运行安装器，输入 **7**（配置地图支援），再输入 **l**。
2. 屏幕列出 8 个小队预设和 10 个载具挂载的当前内容：
   - 输入数字编辑小队预设（有写法说明，`-` 清除）；
   - 输入 `v1` … `v10` 进入某台载具（**载具 → 挂点 → 武器**）：显示「主炮 / 机炮 + 挂点1 … 挂点n」。

     | 输入 | 作用 |
     |---|---|
     | 挂点编号 | 改那个挂点（从列表选武器，再填数量） |
     | `a` | 加一个挂点 |
     | `d` | 删掉最后一个挂点 |
     | `g` | 坦克：换主炮 HE / AP；战机：只带机炮 |
     | `s` | 恢复原版 |
     | 回车 | 保存并返回 |

3. 只写插件能接受的值，写入前会按插件的同一套规则校验，并读回比对。
4. 回车返回，再回车保存。游戏关闭时，保存后立即生成需要的文件：
   - 彩色士兵模板；
   - 配了挂载的坦克 / 战机；
   - 它们挂的、别的工具没写过的挂载武器（`EDF6VC_<挂载>_<数量>.SGO`）；
   - 待生成清单里的文件。

   游戏运行中只保存 ini，并提示退出后再保存一次。

安装（菜单 1）同样会生成这些文件，顺序在 AutoTurret 和实体瞄具写完 `V505_TANK_MISSION` 之后。卸载选 1 时一起删除。开发者也可以直接运行：

```
python tools/support_loadout.py [游戏目录]            生成 / 刷新（含待生成清单）
python tools/support_loadout.py [游戏目录] --remove   删除
```

## 4. 插件数据流

| 数据 | 拥有者 | 说明 |
|---|---|---|
| `SupportConfig::preset[catalog]` / `vehicle[catalog]` | `support_config.cpp` | ini 解析后发布；`support_loadout.h` 的 `ParseSupportPreset` / `ParseVehicleLoadout` |
| 本关已预载的变体文件 | `support_variants.cpp` | 关卡开始时扫描 `Mods/OBJECT/EDF6VC_NPC_*.SGO`、`EDF6VC_LO_*.SGO` 并预载（最多 64 个）；同时生成 hello 的 Bloom 过滤器 |
| `Unit::variant` | 计划 | 房主的 `DecideVariant` 决定是否置 applied；`Validate` 解码并检查；`SpawnDeployment` 把文件路径交给 `ApplySupportSoldierResource` / `SpawnSupportVehicle` / `PrepareSupportAircraft` |
| 对端的扩展字与过滤器 | `support_protocol.cpp Session` | 每次 hello 更新；查询接口 `SupportPeersApplyVariants` / `SupportPeersHaveVariantFile` |
| 待生成清单 | `support_variants.cpp NoteMissingVariant` 写，`tools/support_loadout.py` 读并清理 | `Mods/Plugins/EDF6VehicleCrew.variants_pending.txt`；卸载时一并删除 |

## 5. 测试

| 测试 | 内容 |
|---|---|
| `support_loadout`（C++，78 项） | 兵种名；小队预设语法；坦克 `HE,AP:25` / `AP,AP:20,GLM:4`，战机全炸弹 / 全导弹 / `GUNS`；9 种错误整条拒绝并点名原因；变体编码与解码往返（applied 位、黑色、杂位、挂点数之外的字节、坦克挂点不能用在战机上）；文件名；Bloom 过滤器（大小写无关、哈希值与 Python 一致、200 个其它名字最多 2 个误报、旧对端的空过滤器）；`LoadoutPrefer` |
| `support_loadout_ini`（Python，67 项） | 挂载 / 弹数 / 机体 / 键 / 变体位 / 待生成清单路径与 C++ 一致；同一批值两边结论相同；文件名解码往返；编辑器脚本化操作（小队、坦克加 AP 挂点、战斗机改挂点 2、恢复原版、打开不改不写）；待生成清单的读取与清理；有游戏时只读生成，逐字段核对彩色模板、坦克（HE 炮 + 25 发 AP 独占一个挂点、后坐同主炮）、25 发 AP 武器文件、战斗机（机炮、炸弹、油箱在第 4 位、导弹）、只带机炮时 4 个挂点 |
| `support_protocol`（加） | hello 扩展字与过滤器、unit 消息的变体都能在 v2 线格式上往返；房主知道哪个对端缺哪个文件；对端下一次 hello 更新后结论跟着变；每个对端的计划里都带房主的变体；旧对端不宣告扩展时整个房间不用变体 |
| `support_dispatch_test`（改） | 从 ini 读预设和挂点，给没有载具的单位写挂点、战机挂 AP 都被拒并点名；离线时彩色士兵用自己的文件（队长用 `_L` 文件），无色的用原版；文件缺失时按原版、记下想要的变体、写进待生成清单、状态里点名；房主有旧对端 / 对端缺文件时按原版，全员都有时生效；坦克计划的车体带挂点变体、机组不带；对端缺坦克文件时拒绝计划并写清单；解不开的变体、士兵带挂点都被拒；攻击机呼叫用全炸弹文件生成 |
| `support_spawn_test`（加） | 配了挂载的坦克用自己的文件生成；类别不符的变体只拒绝自己，原版坦克仍可用；不带变体时用原版 |
| `selftest` / `test_installer_incremental`（改） | 安装写入、从已安装的 ini 加待生成清单生成、卸载选 1 删除；第二次安装内容没变就不重写；随附 ini 里有 `SupportPreset_` / `SupportVehicle_` 示例 |

**变异测试**（都先确认变异版本编译通过）：

| 变异 | 结果 |
|---|---|
| unit 消息不带变体 | `support_protocol` 变红 |
| 房主不查对端过滤器 | `support_dispatch_test` 变红 |
| 对端缺坦克文件时不拒绝 | `support_dispatch_test` 变红 |
| 不查旧对端 | `support_dispatch_test` 变红 |

**全量**：
- `cmake --build build` 与 `offline_checks` 0 警告；
- `ctest -j 3` 195 / 195 通过。

`tools/selftest.py` 另有 4 项在基底 07c92d6 上就失败，与本分支无关，本轮没有动：
- `every_npc_aircraft_boardable`：`transportPlane` 不在 `kBoardable` 里；
- `npc_ai_wired`；
- `hud_text_localized`：`hud.cpp` 有一个字面量 `Shift`；
- `soft_edge_wired`。

## 6. 没在游戏里验证的（如实）

1. **彩色模板**：
   - 生成后经 `CreateObject` 能否正常出现；
   - 颜色观感：十六进制 /255 直接写进 `soldier_color`，游戏若按线性值使用，会比网页色偏亮。
2. **带挂点的坦克**：
   - 多出来的挂点在支援坦克（任务车 setup）上是否被 `stores.cpp InstallBuilds` 补造。这条路径和原版坦克请求的额外挂载是同一个第 46 槽钩子，但没有在支援坦克上实测；
   - NPC 驾驶员的 AP / HE 切换；
   - AutoTurret 对滑膛炮弹道的瞄准。
3. **带挂点的战机**：AI 投弹 / 发射是否符合预期，尤其「只带机炮」的 4 挂点布局下机体是否正常。
4. **联机**：没有双机实测。
   - 过滤器、hello 扩展、unit 变体只在协议状态机替身上验证过；
   - 对端缺文件时的提示与待生成清单的写入没有在游戏里跑过。
5. **加载时机**：本关开始之后才生成的文件，要到下一关才会预载并生效。
