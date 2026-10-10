# 2026-10-09（第二轮）用户反馈：空中支援作战（air 组）

分支 `fix/fb1009b-air`，基于 `origin/main` 255ecda（#98）。
依据：本机游戏目录只读的 `Mods/Plugins/EDF6VehicleCrew.log`（16:50 启动的会话，地图墙 x/z −1600..1597），未启动、未关闭游戏，未安装 DLL。
**以下全部是离线验证（编译、离线模拟、单元测试、变异测试），没有在游戏里实测。**

## 1. 「多用途机好像不会攻击地面」

### 日志事实
- 17:06:50 呼叫的三架多用途机（`SUPPORT plan catalog=11 ... in the air at (-2,107,1567)`，入场点在软边界 ±958 之外 600 米）之后 4 分钟：相位直方图 approach 235 / dive 64 / pull 42 / extend 481 / withdraw 354 行，`guns=3600` 从头到尾没少，`fire=0/0`。
- 每一行都带 ` back`（软边界「先飞回来」标志）；俯冲时 `bank` 82–86°、`spd=131/225`（实际 131 m/s，是持续极限转弯的平衡速度）；extend 一次正好 12 s（`kExtendMs` 超时）；extend 途中位置到 z=1858，越过了场地墙 1600。
- 炮舰机（gunship，绕目标盘旋）同局打了 169 发，所以问题只在「俯冲扫射」型（多用途机、攻击机）。

### 根因（`src/jet_combat.cpp` `Strike`，`src/jet_flight.cpp` `SoftEdge`）
原版尺寸地图的软边界盒只有约 ±960 米，而 Strike 的航线参数是按大地图定的（`extendOut` 2300 m、`diveStart` 1600 m、按 `TightSpeed` 131 m/s 的转弯半径约 570 m）。一次对地攻击在盒子里放不下，三处互相打架：
1. **俯冲被软边界掰偏**：整条攻击航线都落在软边界的「提前减弱朝外分量」范围内，入场时又在边界外（`back` 模式要求近乎水平地飞回来），`KeepIn` 改写了俯冲的 `want`，机头对不上提前量点（离线复现 `miss` 15–60°，开火锥只有 2°）。
2. **extend 与软边界顶牛**：extend 沿当前航向往外飞 2300 m，撞到软边界后被 `KeepIn` 往里拧，extend 又往外拉，直到 12 s 超时；接着 approach 发现目标在转弯圈内又进 extend，循环。
3. **掉头后离目标太近**：直着外飞再 180° 掉头，横向偏出 2 个转弯半径，转到对准目标时只剩 300–600 m，仍在 500 m 高度，要 50° 以上俯冲，压不下去就进 pull。

攻击机（strike）完全同一套代码，这局只是没叫；离线同样复现。

### 修法
- **俯冲不被软边界掰偏**（`SoftEdge`）：在 `Mode::dive` 且目标点在软边界盒内时，`KeepIn` 在副本上跑（边界状态照常更新），不改写俯冲方向。安全性：俯冲终点就是盒内的目标点（`gunClose` 前拉起），软边界外的缓冲带宽度按转弯半径定（`JetSoftBox`），拉起后的转弯仍在场地墙内；pull 阶段照常受软边界约束。
- **攻击航线速度**（`StrikeSpeed`）：场地容不下它全速转弯时（`TightSpeed` < 攻击速度，原版地图上所有固定翼都是这样）对地攻击航线用最低速（`minSpeed`）飞；转弯半径约为原来的四分之一，进入段有几秒让机头稳定。大地图上仍是攻击速度。
- **外飞按空间规划**（`PlanExtend`）：大地图上（沿当前航向外飞 `extendOut` 加转弯仍在软边界内）与原来完全相同；否则做「泪滴形」折返——在 16 个方向 × 两侧里选一个转折点 `outAt`（目标旁偏 2 个转弯半径、沿该方向外飞 `outRun`），从那里掉头正好落在穿过目标的航线上、离目标 `outRun`；选进入距离最长（上限 `diveStart`）且离当前去向最近的。都放不下时退回直飞到盒子允许的距离。`Debug=1` 日志 `JET v=... extend: teardrop|straight ...`。
- 试过但没采用：按进入距离降低进近高度（单独用时最低离地 1 m，与泪滴合用时总开火时间反而少）。

### 测试
- 新 `jet_obstacle_sim --strike-room-suite`（CTest `jet_strike_room`）：原版尺寸场地（地面 ±1750、墙 ±1600），多用途机（挂载 ×1.18）、攻击机（×1.21、空载）三种 × 4 个目标（日志的 z=450、中心、两个角落）× 3 种起点（日志的入场点与高度、另一入场点、场内按机种高度），飞 120 s，要求 45 s 内首次开火、至少 2 次独立攻击、不越过场地墙。
  - 修复前：36 例 31 例失败，总开火 92 s，最多越墙 666 m（负对照，复现日志）。
  - 修复后：36 例全过，总开火 538 s，最低离地 34 m，不越墙。
  - 变异：去掉俯冲豁免 9 例失败；去掉降速 22 例失败；去掉泪滴 3 例失败。
- `jet_obstacle_sim --selftest`（大地图攻击航线、300 例拉起）、`--edge-suite`（405 例软边界）不变，全过。

## 2. 「飞机没办法指定攻击目标」

### 日志事实
17:09:07–17:09:47 五次 `MAPCMD FOCUS FIRE (0,0,0) to 1 selected: 0 of 56 units took it`；整局飞机只收过 `map command: guard` / `release`。

### 根因
`src/mapcmd.cpp` `Takes`：载具只接受 `VehicleOrder`（守点 / 移动 / 攻击移动 / 跟随 / 解除），集火只给小队；`JetCommand` / `HeliCommand` 也只认这几种，没有「目标」的概念。

### 修法（与小队集火同一条命令路径）
- `mapcmd_logic.h` `AirOrder` = 载具命令 + 集火；`mapcmd.cpp` 只改两处：`Takes` 对 `Owner::heli / jet` 用 `AirOrder`，`Give` 把下令时的标记身份 `g.focus`（就是小队集火用的那个）传给 `HeliCommand / JetCommand` 的新参数 `focus`（`mapcmd.h` 默认 `{}`）。
- 飞机侧（`jet.cpp` / `jet_internal.h` / `jet_combat.cpp`、`heli.cpp`）：集火时验证目标确实是它当前的敌人（同 npcai 对小队的判据），记在 `Jet::focus` / `Heli::focus`，**不改原命令和守点**；选目标时它无条件优先、不受守点射程限制，集火期间不走「先飞到命令点」的不接敌逻辑；目标在场地墙外时暂不追、照常打别的；目标不再出现在敌人列表里（死亡、消失）即放开，回到原守点 / 跟随，日志 `focus target ... no longer among the enemies: back to its order`。其它命令清除集火。地图面板对集火中的飞机显示「集火」。直升机集火期间不按守点盘旋（炮舰型改为绕目标盘旋）；医疗直升机不接集火。
- 焦点身份不钉控制块（与 `Heli::target` 相同）：每帧必须作为敌人被遍历到才算有效，所以不会打到复用地址的非敌对象。

### 测试
- `jet_obstacle_sim --selftest` 新增 5 例（生产 `PickTarget` / `VisitTarget`，可配置敌人列表）：守点射程外的敌人不打 → 集火后优先打它（原命令保留）→ 目标在墙外时打别的、集火保留 → 目标消失后集火放开、回到原目标 → 其它命令放开集火。变异：去掉集火优先，2 例失败。
- `jet_command_test`：`ApplyMapFocus` 保留命令与目标点、丢弃旧攻击；新命令放开集火。`heli_command_test`：没有标记时集火被拒；其它命令放开集火。`map_cmd_check`：`AirOrder` 含集火、不含其它小队命令，`VehicleOrder` 不含集火。
- 所有 `HeliCommand / JetCommand` 测试桩改为三参数；`tools/selftest.py` 按新签名切片。

## 构建与测试
`cmake --build build`（插件 + `offline_checks`）无警告；`ctest -R "jet_|heli_|map_cmd|map_command|support_dispatch|support_infantry|create_jet|airbound|selftest"` 25 项全过（`heli_yaw_native`、`create_jet_native` 因本 worktree 未配置 EDF.dll 路径跳过）。

## 还没在游戏里实测的（如实）
1. 最低速飞对地攻击航线在 Havok 物理下的观感（会不会显得太慢、失速姿态）；离线模型是插件飞控公式本身，地面平坦、没有建筑与地形起伏。
2. 原版地图边缘丘陵上的泪滴转折点是否会撞地形（`Guard` 的避地照常生效，但离线没有地形）。
3. 集火：右键敌人 / H 对选中的直升机、战机是否真正生效（`Debug=1` 日志 `JET|HELI v=... map command: focus`）；集火远处目标时会不会被软边界挡在外面。
4. 联机：飞机集火与其它载具命令一样只由本机驾驶权威执行，未改联机协议，也未双机实测。
