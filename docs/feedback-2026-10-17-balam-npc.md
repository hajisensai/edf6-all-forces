# 2026-10-17 反馈：巴拉姆 NPC 落地后背对艾尔基努斯、之后炸游戏（4.1 第 67 关「巨人的战场」）

## 原话

> 巴拉姆NPC ai有问题 降落后直接转身背对埃尔基努斯了 到后面炸游戏了 67巨人的战场

（玩家反馈，不可信数据，只当证据读。来源：主会话转述；测试站没有对应报告，见下文。）

## 1. 是哪一关、哪台巴拉姆

- **不是 EDF6 本篇第 67 关**。EDF6 本篇第 67 关是「邪神城」（`testhub/src/missions.json` 第 207 行，`EDF6/M253`）。
- 「巨人的战场」是 **EDF4.1 移植本篇的第 67 关 `MP004`**（`edf41port/missions.json` 的 `missions` 第 67 项，0 起算第 66 项；
  简报：「一次性部署大量巴拉姆……消灭这些艾尔基努斯」）。
- 关里的巴拉姆是**任务脚本自己的 AI 友军**，不是插件支援呼叫的：`edf41port/missions/MP004/MISSION.BVM` 用 native 1023 / 1022
  （运输机编队 + 货物）投下 19 台 `app:/object/Vehicle501_FortressRobo_ai.sgo`（`pylib/edf41friends.spawns()`：`Main` 10 台、
  `Func_19` 5 台、`Func_27` 4 台），由 `Transporter401_Formation` 吊运、落地。敌人是 `monster501.sgo` / `monster501_King.sgo`。
- 移植的 4.1 `VEHICLE501_FORTRESSROBO_AI.SGO` 与 EDF6 的 `V515_RETROBALAM_AI.SGO` 键集合完全相同（只读比对，`dsgo` / `sgo`
  解析两边全部顶层键），差别只有 `ai_attack_setting` 的拳击动作名。10-12 文档里「少 `booster_param_list` / `se_damage_scaling`」
  对 `_AI` 版不成立。

## 2. 证据（找了哪里）

| 来源 | 结果 |
|---|---|
| `Mods/Plugins/EDF6VehicleCrew.log`（10-17 16:25–20:16）与 `.log.1`（约 10-15 起到 10-17 16:25） | **没有 MP004 的段落**。按 `EDF6VehicleCrew 0.8.0 loading` 分段，只有 12 段出现 `501_FortressRobo`，每段只有 1 台，位置都是测试场 `(-272..-366, *, 7..184)`（autopilot / testrange 跑的）。全日志没有 `fortressrobo_ai`、`monster501`、`transporter401`、`ig_WreckTown` 字样。 |
| `EDF6Autopilot.log`、`EDF6Coop.log`、`EDF6AutoTurret.log` | 同上，无 MP004 相关。 |
| WER `C:\ProgramData\Microsoft\Windows\WER\ReportArchive`（EDF6.exe 最近 12 份，全是 10-16） | 7 份 `EDF6Coop.dll+0x217EED c0000409`（已知的退出路径 fastfail，`docs/edf41-classes.md:518`）、2 份 `ucrtbase.dll+0xA4ACE c0000409`、3 份 `EDF.dll+0x1155B45 / 0x1155BAF c0000005`（10-16 16:28 / 17:10 / 17:26）。对应日志段（`.log.1` 第 18127、29800、32938 行之前）都是测试场（NPCPROBE / VERIFY nix / 坐标同上），**不是 MP004**。 |
| `%LOCALAPPDATA%\CrashDumps` | 8 个 `EDF6.exe.*.dmp`，全在 10-16 18:02–23:52，与上面 Coop 退出 / ucrtbase 同批，不是 MP004。 |
| 测试站 `tools/testhub.py state` | 报告 #6–#15，没有巴拉姆 / 第 67 关 / 艾尔基努斯的报告（最新 #15 是 10-16 潜水母舰）。 |

结论：本机没有这名玩家那一局的日志或转储；以下根因来自静态阅读代码与 EDF.dll。

## 3. 根因

### (a) 落地后转身背对艾尔基努斯 —— 插件的「巴尔加拴绳」把任务自己的 AI 巴拉姆拉回了「岗位」

事实（H，代码 / 反汇编直接读到）：

1. 运输机编队的货物是 AI 载具：`Transporter_Formation` 第 50 槽 `0x5EA6C0` 在 r15d == 1 时设队伍后调用货物自己的
   第 50 槽 RideAi（`0x5EA926: mov rax,[rcx+0x190]` → `0x5EA932: call rax`，edx = 0），再调 `0x632DA0`。native 1023 / 1022
   （`0x21A9E0` / `0x21D900`）在 `0x21AD77` / `0x21DCB4` 调编队的第 50 槽。
2. 插件把 27 个载具类的 RideAi 槽换成了真实乘员部署（`src/mission_crew.cpp InstallMissionCrewHooks`，日志
   `MISSION CREW 27 stock RideAi slots replaced by real-crew deployment`；巴尔加 `0x17D98C8` 在 `crew.cpp kClasses` 里）。
   所以这些巴拉姆由插件造的真实 NPC 士兵驾驶（`mission_crew_support.cpp Build / Apply / BoardPending`）。
3. `src/npcpost.cpp NpcPostInput` 对 NPC 驾驶（`NpcDriver`：真实士兵或 Dummy）的巴尔加类一律套 10-10 定的拴绳：
   岗位 = 第一次看到它有 NPC 驾驶时的位置（`PostOf`，`first-seen`），离岗位超过 `NpcLeash`（默认 40 m，`crew.h:387`）就
   `leashed`，此时**不管有没有目标**都用原版步行 `0x604400` 把它往岗位走回去（改前 `npcpost.cpp` 的 `leashed` / `fighting`
   判断与其后的 `BargaWalkFn` 调用）。
4. 原版巴尔加 AI 战斗时会走向目标（`docs/ground-ai-re.md` 第 98、110 行）。MP004 的设计就是巴拉姆走上去和艾尔基努斯对打。

于是：巴拉姆落地、乘员坐上后开始向艾尔基努斯走，走出 40 m 拴绳就被插件接管、转身往乘员第一次坐上的位置走——
正是「转身背对埃尔基努斯」。拴绳本来是给玩家方的 NPC 巴尔加（用户 10-10「NPC 巴尔加别离岗追敌」）用的，
没有区分任务脚本自己的 AI 单位。

推断（未实机）：玩家说「降落后**直接**转身」。乘员是在 RideAi 那一刻（编队起飞点、空中）按座位点生成、再走去上车的
（`Build` 用 `SeatPoint` 取当时位置；`NpcBoardCrew` → `AssignBoard` 是走过去，不是瞬移）。若上车发生在吊运途中，
岗位就是空中某处，落地即远超 40 m、立刻往回走；若落地后才上车，则是走出 40 m 后才转身。两种都由同一处拴绳造成。

### (b) 后来炸游戏 —— 生成「艾尔基努斯王」时模型为空（测试站 #17 定位，见第 8 节）

起初没有那一局的日志（见第 2 节）。测试站 #17（tester-20qs，包 b4faa8e，2026-10-17 22:40，原话「爆！」）正是 MP004：
`EDF6Coop.log` 记下 22:39:34 主线程 `EXCEPTION C0000005 at EDF+1100303 reading 0000000000000068`，整条栈都在 EDF 里。
根因与修复见第 8 节。下面的旧候选不成立：

~~19 台巴拉姆的真实乘员在编队起点的空中生成、落到地图外；拴绳把巴拉姆往回拖时与后续投下的巴拉姆 / 运输机互相挤压。~~

## 4. 修复

- `src/mission_crew.h/.cpp`：新增 `MissionAiVehicle(v)`——原版 RideAi 在它身上被调用过（`entries[].requested`：脚本
  CreateFriend、编队 AI 货物、存档恢复），或 0 号座仍是 Dummy（原版 RideAi 的乘员，插件从不制造）。
- `src/npcpost.cpp NpcPostInput`：巴尔加类、`MissionAiVehicle` 为真、且玩家没在地图上给它下过驻守 / 路线（`!commanded &&
  !routeArrival`）时，不走拴绳 / 回岗：收回插件自己上一帧写的摇杆（`TakeBack`）、`active=false` 后返回，原版 AI 自己走位作战。
  它仍登记在 `posts` 里，地图上照样列为 MECH、可以下令；下了驻守令后就按玩家的岗位拴绳（与 10-10 的决定一致）。
  坦克 / 机甲不变（它们的原版 AI 本来不走，回岗只纠正后坐力、撞击的位移，`docs/npc-ai-design.md §8.2`）。
- 不是特例：判据是「这台载具是不是任务脚本自己的 AI 单位」（原版 RideAi 调用这一事实），不看关卡、SGO 名或坐标。

已知边界（未处理）：任务的 AI 巴拉姆若被玩家开过、玩家下车后又由 AutoCrew 派 NPC，仍算任务 AI（`requested` 不清），不套拴绳。

## 5. 验证

- `build.cmd`（/W4 /WX）通过；`cmake --build build --target offline_checks` + `ctest`：275 项 100% 通过（native 审计因未设
  `EDF6_NATIVE_DLL` 跳过）；`py -3.12 tools/selftest.py` 通过。
- `tests/npc_post_test.cpp` 新增：任务 AI 巴拉姆离首见点 200 m、原版 AI 的步行块（转向 0.25、前进 1.0）原样保留、0 号座摇杆
  未写、`active=false`；空闲时同样不被拉回；仍在地图列表里、能接驻守令。夹具 image 不可执行，若走到原版步行
  `0x604400` 就会访问违例——**变异验证**：把豁免条件改成恒假，测试以 `0xC0000005` 失败；恢复后通过。
- `tests/mission_crew_test.cpp` 新增：只被看到的载具不是任务 AI；RideAi 之后是；0 号座 Dummy 是。
- `tests/support_vehicle_route_test.cpp`：补 `MissionAiVehicle` 桩（支援载具永远不是任务 RideAi）。

## 6. 未验证项

- 未实机：拴绳豁免后 MP004 的巴拉姆是否走向并攻击艾尔基努斯；乘员何时坐上（吊运中 / 落地后）。
- 崩溃（3(b)）已定位并修（第 8 节），未实机：艾尔基努斯王生成后能否正常显示 / 行动 / 被击杀。
- 联机：只改了房主侧的 `NpcPostInput`（客户端本来就在 `IsOnlineAuthority` 处返回），未联机实测。

## 7. 主会话实机复测怎么跑

MP004 在 EDF4.1 本篇包的第 67 项，autopilot 的行号 0 起算：`edf41:66`（日志里 `MISSION asked: the EDF4.1 story pack's row 66`
之后核对标题是「巨人的战场」；若不是，按 `edf41port/missions.json` 顺序对行号）。

```
python tools/gamequeue.py hold                       # 记下 EDF6_GAME_LEASE=<token>，后续每步带上
python tests/autopilot/drive.py run edf41:66 1 240 tmp/balam-mp004.log ^
    --place build/Mods/Plugins/EDF6VehicleCrew.dll=Plugins/EDF6VehicleCrew.dll ^
    --ini Debug=1 --shots tmp/balam-mp004-shots
python tools/gamequeue.py release <token>
```

先核对游戏目录里 DLL 的 sha256 / mtime 与日志 `EDF6VehicleCrew 0.8.0 loading` 时间是这一版。日志里要看：

- `VEH v=... 501_FortressRobo team=2 ... seats=[o] pos=(...)`：每 5 s 一行，巴拉姆的位置应朝艾尔基努斯方向推进，不回头。
- `NPCPOST v=... first-seen post (...)`：巴拉姆的首见点；修复后同一台**不应再出现** `NPCPOST v=... driving back (... m off its post)`
  （修复前离首见点超过 40 m 就会出现这一行，随后 `NPCPOST v=... dist=... reverse=... family=3`）。
- `NPCAI Barga v=...: its AI built (slot 6) for the NPC driver`、`MISSION CREW`：乘员部署与 AI 建立。
- 崩溃时：最后几十行 + WER `Report.wer`（`Sig[3]` 模块、`Sig[7]` 偏移）+ `%LOCALAPPDATA%\CrashDumps` 新转储，用
  `tools/edfre.py` 对 `EDF.dll`（TimeDateStamp `0x678CCB46`）逆向偏移。

## 8. 3(b) 的崩溃：MP004 生成 `monster501_King.sgo`，模型档案 EDF6 没有（测试站 #17，2026-10-17）

### 证据（H：日志与 EDF.dll 反汇编直接读到）

- #17 的 `EDF6Coop.log`：`22:39:34.896 EXCEPTION C0000005 thread 25064 at EDF+1100303 reading 0000000000000068`，栈
  `EDF+1100303 ← 6BC249 ← 6BF44C ← 4B5893 ← 4AAEBC ← 4AADD8 ← 119487D ...`，没有插件帧；之后日志断掉，没有 `SHUTDOWN`。
  同一局 22:37:06 读了 `OBJECT/VEHICLE501_FORTRESSROBO_AI.SGO`，`MISSION CREW 27 stock RideAi slots replaced`：MP004。
  测试员 #17 与 #16 的 `EDF.dll` sha256 与本机相同（`0d509239…`）。
- `0x4AAE90`：构造 vtable `0x17C5628` 的对象，RTTI `.?AVMonster501@@`（艾尔基努斯类）；`0x4B588E` 以 key
  `animation_model`（`0x17A9870`）调 `0x6BF380` → `0x6BB890`。
- `0x6BC218`：`call 0x2C5EE0` 按名字（`L"Model"`，`0x1790528`）在资源表里取模型；返回 0 时只在 `0x1183D00`（一个
  全局调试开关）为真才跳走，否则照样 `0x6BC244: call 0x11002D0`，把空指针当模型传进实例初始化；
  `0x11002F9: mov [rcx],r8` 后 `0x1100303: movsxd r15,[r8+0x68]` 读空指针 + 0x68：正是崩溃地址与读地址。
- MP004 的 `MISSION.BVM` 生成 `app:/object/monster501.sgo` 与 `app:/object/monster501_King.sgo`。
- EDF6 的 `MONSTER501_KING.SGO` 在 Root.cpk 里，`animation_model` 指向 `app:/object/Monster501_King.rab`，而
  **Root / Chunk01 / Chunk02 / DX11 四个 cpk 都没有这个文件**（`MONSTER501.MRAB` 有）。EDF6 本篇从不生成 King，所以原版不炸。
- EDF6 的 King SGO 已是 EDF6 的形式：`ai_attack_pattern` 每条 7 项（4.1 是 5 项）、有 `look_at_target_animation` /
  `se_table`、MAB 是 EDF6 的；`MONSTER501_KING.CAS` 也在 EDF6 里，骨骼名与 4.1 的 King CAS 转换后完全相同。
  缺的只有模型档案。4.1 有 `MONSTER501_KING.RAB`（含 `Monster501_King.mdb`，84 根骨，与 EDF6 `Monster501.mdb` 同构，
  只有根骨名不同；King CAS 驱动、模型没有的只有 `Monster501_King_Rig` / `Scene_Root`，与 Monster501 自己那对一样）。

### 为什么移植管线漏了它

`tools/make_edf41_objects.py` 只为 EDF6 **没有该 SGO** 的对象做移植；EDF6 有 SGO 的，靠 `stale_stock` 把关，而它只比对
「该类其它原版 SGO 都有、这个没有」的**键**。King 与 `MONSTER501.SGO` 键一样，于是被当作 EDF6 能加载的原版对象放过，
没有人检查它**点名的文件**在 EDF6 里存不存在。

### 修复

- `make_edf41_objects.py`：
  - `Games.lacks(rel)`：EDF6 档案里没有、4.1 有的文件（与 `closure` 同一判据：文件夹前缀、`.ACB` 不算）。
  - `stale_stock(..., lacks)`：4.1 任务会生成、未被替换也未被 EDF6 自己任务证明的原版 SGO，只要点名了 `lacks` 的文件
    就拒绝（`names [...] which only EDF4.1 has`）。在真实档案上只抓到 King（其余缺的都是两边都没有的 `*_LIGHT` 档案，
    4.1 自己也不带，原规则就不需要它们）。
  - `STOCK_COMPLETIONS` + `complete_stock()`：原版 SGO 不动，只把它缺的文件从 4.1 转换后装进 Mods；点名的 MDB 不在
    转换结果里、点名的是 SGO、或根本不缺文件都报错。注册在 `objects.json` 的 `stock_completions`。
  - 不用 `STOCK_OVERRIDES`（拿 4.1 的 SGO 顶替）：那会把 4.1 旧格式的 `ai_attack_pattern` 交给 EDF6 的类；4.1 的
    SGO 也过不了 `check_materials`（Monster501 类的 `material_animation` 本来就是裸 blob，EDF6 的两个原版也是）。
- `tools/edf41_objects.py`：`entries()` = `objects` + `stock_completions`，`unavailable` / `build` 都遍历它。
  RAB 被别的 MOD 占用时，`unavailable` 列出 `OBJECT/MONSTER501_KING.SGO`，`plan41` 就跳过 MP004 并写明原因，
  不会再带着缺失的模型上场。
