# 2026-10-17：4.1 移植任务「NPC 好像也没搬过来」

用户原话（玩家反馈，只当证据读）：

> npc好像也没搬过来

原话没有说是哪一关、是「没有友军出现」还是「友军不是 4.1 的样子」，也没有日志。下面先把两种读法都按代码与数据查清。
全部是静态核对（脚本、存档数据、EDF.dll 反汇编），**没有开游戏**。

## 1. 4.1 任务里的友军是怎么来的

4.1 本篇 89 关的脚本是 EDF5 对 4.1 脚本的重编译版（`tools/make_edf41_missions.py`，原样提交在 `edf41port/missions/`），
由 EDF6 的 BVM 执行器跑（分派 `EDF.dll 0x20FF70`，表 0 按 id 低字节跳转）。创建友军的 native（表 0，EDF5 / EDF6 编号；
4.1 → EDF5 不改号，`pylib/bvm41.py NATIVES` 里没有它们）：

| id | 作用（参数） | EDF6 处理函数 |
|---|---|---|
| 1010 | CreateFriend(点, sgo, 等级, bool)：单个友军，多为 AI 载具 | `0x219720` |
| 1011 | CreateFriendSquad(点, 半径, 队长 sgo, 队员 sgo, 人数, 等级, 可招募) | `0x21A290` |
| 1012 | CreateFriendGroup(点, 半径, sgo, 人数, 等级, bool) | `0x21A010`（成员走 `0x21BB40`） |
| 1022 / 1023 | 运输编队与吊运的载具（5 个字符串 + 1 个浮点） | `0x21D900` / `0x21A9E0` |

执行器会让一次调用「什么都不生成」的条件只有两个（H，读反汇编）：

- 点名在任务 RMPA 里找不到：`0x6F83B0` 失败，1011 在 `0x21A3E1` 直接返回 -1（1010 在 `0x2197F2`、1012 在 `0x21BC5D` 同样查点）。
- 1011 / 1012 用地图射线把每个士兵放到地面（`0x221DC0`：点上方最多 20 m 往下打到 y = -1000，layer 22），打不到地面就不生成
  （1011 `0x21A41D`，成员 `0x21BEB0`）。生成玩家也用同一个探测（`0x22B5E5`），所以玩家能站上去的地图它也能打到（M：推断）。

SGO 按路径原样加载：EDF6 档案里有这个路径就用 EDF6 的文件，否则用移植包的（`edf41port/objects`）。

## 2. 逐关统计（`pylib/edf41friends.py spawns`，两种扫描方式结果一致）

- 本篇 89 关中 **86 关**创建友军（M019、M147、M405 没有）；调用数 1010 × 145、1011 × 264、1012 × 26、1022 × 5、1023 × 15。
- 4.1 原脚本（EDF4.1 Root.cpk `MISSION/<id>/*.BVM`）与转换后脚本：每关点名的友军 SGO **集合完全相同**（因而每个 SGO 涉及的关数
  也相同）；转换后脚本本身就是 EDF5 的重编译文件，`tools/check_bvm41.py` 证明 4.1 → EDF5 的转换逐字节复现它。转换没有删掉、也没有替换任何友军调用。
- 每个 1010–1012 调用的点名都在该关 `MISSION.RMPA` 的点里（435 / 435）。
- 涉及的友军 SGO 共 34 个，**全部能加载**：

| 来源 | SGO（关数） | 游戏实际加载的 |
|---|---|---|
| EDF6 自己的同名文件（28 个） | AiArmySoldier_AF / FL / RL / SG / SN 及各自 _Leader（AF 57 关…FL 4 关）、AiHeavyArmor_SC / SMC / SP（含 _Leader）、AiPaleWing_IZN / LANCE / LR / MS / TB（含 _Leader）、Vehicle402_Rocket_AI、Vehicle403_Tank_AI、Vehicle404_BigTank_AI | EDF6 的游骑兵 / 重装 / 飞行兵（`p505_ranger` / `p507_fencer` / `p506_wingDiver` 模型、EDF6 配色、EDF6 的 AI 武器，例如重装兵是 `AiHeavyShield01Elite` + `AiHeavyDash01`），类与 4.1 相同；EDF6 自己的任务也生成这些文件（AiPaleWing_TB 除外，其键与同类一致） |
| 移植（6 个） | AiArmySoldier_OMG（+_Leader，各 5 关）、Vehicle301_Tank_AI（13 关，EDF6 V505 AI 坦克 + 4.1 外观的替身）、Vehicle501_FortressRobo（+_AI）、Transporter401_Formation（覆盖 EDF6 残留的坏文件） | 4.1 的文件（`tools/make_edf41_objects.py`） |

两个 DLC 任务包（`edf41port/mp01`、`mp02`）的友军也都落在这 34 个之内，同样全部可加载。

另查了移植的欧米伽队士兵：它的 4.1 CAS 比 EDF6 士兵少两个变量 `v_vehicle_grip_l/r_ik_target`，EDF6 在 `0x56E49F` 按名查到 -1
后，使用处 `0x57526C` / `0x575320` 先判 -1 再写，不会越界（H）；缺的只是若干动作状态，按 `pylib/cas_graft.py` 的结论无害。

## 3. 结论

- **「友军被删掉 / 引用了不存在的 SGO 而不出现」不成立**（H，静态）：调用一条不少、SGO 全部可加载、点名全部存在。
  委托时「部分友军被删」的假设与证据不符。
- 最可能的读法（推断，未向玩家确认）：玩家看到的友军是 **EDF6 的士兵**，不是 4.1 的样子。EDF6 沿用了 4.1 的文件名
  （`AiArmySoldier_AF` …），所以 4.1 任务点名的游骑兵、重装兵、飞行兵加载的是 EDF6 的模型、配色、武器和语音设置；
  只有 EDF6 没有的欧米伽队等 6 个是 4.1 原物。这是现有设计（「EDF6 有同路径文件就用 EDF6 的」），不是丢失。
- 不能把 4.1 的士兵直接放到这些路径下：EDF6 自己的任务也生成同名文件，会把 EDF6 本篇的友军一起换掉。要 4.1 外观，
  需要把 4.1 士兵移植到独立路径（例如 `OBJECT/EDF41/…`），并让 4.1 脚本改指向它（本篇脚本目前是 EDF5 文件原样，需改为
  4.1 原脚本经 `bvm41.to_edf5(..., rewrite=...)` 生成，DLC 包已经这样做），且 4.1 的游骑兵 / 飞行兵 CAS 要按 `cas_graft` 补齐
  EDF6 类查找的 player。这是一个外观功能，范围和风险都不小，本次没有做，留给决定。

## 4. 本次改动

- `pylib/bvm41.py`：`type_check` 的栈遍历抽成 `_walk`，栈上值带上字符串字面量（`Arg`）；新增 `native_calls` 取某些 native
  每次调用的实参。`type_check` 行为不变（`tools/check_bvm41.py` 全部通过，含交换参数自检 31 / 31）。
- `pylib/edf41friends.py`（新）：友军 native 表、`spawns()`、全部友军 SGO 的来源与类（`FRIENDS`），以及上面的地址依据。
- `tools/selftest.py`：
  - `edf41_friends_spawn_as_named`（不需要游戏，CI 跑）：三个 4.1 包每个友军调用的 SGO 都在 `FRIENDS` 里、点名都在 RMPA 点里、
    移植来源的都在 `objects.json`；本篇调用数与「86 关」钉死，`FRIENDS` 没有多余项。变异验证：删一项 / 加多余项 / 丢一个点 /
    丢一条调用都报错。
  - `edf41_friends_load_in_edf6`（需要 4.1、5、6，否则打印 skip）：EDF6 来源的文件在 EDF6 档案里、类对、引用的文件全在；
    移植来源的类对；每关 4.1 原脚本点名的友军集合 = 转换后脚本创建的集合。

没有改 `edf41port/` 下的数据，也没有需要再生成的数据（本次只加检查）。

## 5. 未验证 / 需要实机确认

- 玩家指的是哪种情况：请玩家说明是哪一关、是「一个友军都没有」还是「友军长得不是 4.1 的」。
- 实机进 4.1 关（例如 M104、M119、M123，友军多）看友军小队是否出现在出生点附近。地面探测在转换后的 4.1 地图上能否命中
  只做了推断（玩家生成用同一探测）；`docs/edf41-uforobo.md` 记录 M411（SK_KAIGAN01）实机里机器人被友军打掉血，是旁证。
- 4.1 地图的寻路网格借用 EDF6 地图（`tools/edf41_maps.py` 已记为未解决）：友军能生成，但走位可能不对，可能被玩家理解为「没有 NPC」。
- 欧米伽队（4.1 原文件）从未实机生成过。
