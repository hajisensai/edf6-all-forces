# 2026-10-10 炮舰机的弹打到自己（gunship-own-rounds）

## 用户原话

「炮舰机的机炮有可能会打在自己身上，导致没打出去。」

## 现状与证据

- 10-06 已修过一次（`9c06e8f`、`38c287f`）：三门炮都从 `GunshipMuzzle` 出膛（`src/gunmuzzle.h` 的 `Muzzle`：从整机包围盒中心沿
  到目标的直线，走到「盒子外扩 命中半径 + 0.5 m」处）。
- 日志里没有炮口和命中点，**无法直接证实自伤**。本次在日志里加了两条能直接证明 / 排除的行（见下），没有在游戏里复现。
- 用户日志（只读 grep，`EDF6VehicleCrew.log.1`）：NPC 炮舰机 13:31–13:32 期间 `vy` 在 −31 … +21 m/s 之间、坡度 8–85°，并非只在水平
  盘旋，说明「机体静止 + 直线弹道」这个前提在实战中不成立。

## 根因

两层漏洞，任何一层都能让炮舰机的弹打到自己：

1. **几何只对静止机体成立**（`src/gunmuzzle.h:49-65` 旧 `Muzzle`；调用处 `src/jet_bay.cpp` `GunShot` / `GunAtTarget` /
   `GunshipFire` / `CrewFire`）。弹不继承炮舰机速度（IFC 按自己的弹速沿起点→瞄准点发射），而新造的弹对象可能晚一帧才走第一步
   （`docs/jet-model-re.md` §8.3，未实测）。晚一帧时起点在世界里不动、机体已向前飞了一帧，相对机体等于起点后移一帧航程
   （145 m/s 时 2.4 m，只有 0.5 m 余量）。朝前下方开火时出膛点在机头那一面，后移就落进机头。
   - 离线仿真（`tools/gunship_muzzle_check.cpp` 新增的动态部分，机体沿真实轨迹飞、弹每帧 16 步跟踪）：
     145 m/s 直线飞行、目标在前下方 300–1500 m、俯角 3–60°，旧炮口 **机炮 119/240、加特林 112/240、炮弹 160/240 发** 的命中球
     碰到机体（最深 2.0 / 2.0 / 2.5 m）；74 m/s 时也有约一半（最深 0.8–0.9 m）。拉起、滚转、转弯中同样。
   - 盘旋（目标在圆心或圆外）时旧炮口没有碰到：出膛面与飞行方向平行，后移不进盒子。所以这条只在「朝前开火」时出现，符合「有可能」。
   - 炮弹的「抛物线」是误判：`DEMOGUNSHIPFIREE25` 的 #5 = 8 m/帧、#6 重力系数 = 0（RocketBullet01，安装包检查
     `make_jets.check_gunship_muzzle` 现在对原版文件核对这两个值）；IFC 的弹道求解 `0x2312A0` 用 `param+0xB0`（重力系数）抬高初速，
     为 0 时与直线相同（静态 H）。所以炮弹也是直线，但它最慢（8 m/帧），相对漂移角最大。
2. **owner 排除不可靠**（`src/jet_hooks.cpp:64` 旧 `Passes`：`s!=t` 时不处理 owner==目标，交给原版收集器 `0x232AA0`）。原版只在
   子弹 `core+0xAF4 & 0x80`（「可以打到 owner」）为 0 时才按 owner 指针跳过（`docs/bullet-pass-re.md` §3.2 #1）。这一位除了初始化
   外还在两条移动路径置位：`0x236662`（类型 0 移动 `0x2364D0`，条件 `core+0x6D8` 非空且 `core+0xB04` ∉ {0,3}）、`0x2361E4`（类型 2
   移动 `0x235FA0`，条件 `*(core+0xBE8)+0x28` 或 `sil` 非 0）。IFC 发的弹会不会走到这里没有确认（L）。另外原版只比较 key-0 对象
   指针，机体的飞行刚体（`veh+0x1650`）和 ragdoll 部件刚体（`veh+0x1398`，步长 0xC0，包装 `+0x50`）是否都映射回机体指针也没有在游戏
   里核实（`docs/boarding-re.md` §4 #2，M）。

## 修法

1. **命中候选钩子里排除自己的弹**（新 `src/ownround.h`，纯函数 `ownround::Judge`；`src/jet_hooks.cpp` `Passes` / `Publish`）：
   子弹 owner 是已登记的插件喷气机，且候选刚体的对象就是它、**或候选刚体编号是它自己的刚体之一**（`Publish` 每帧在游戏线程上读出
   飞行刚体和全部 ragdoll 部件刚体的编号：包装 `+0xF0`，`0x11B15E0` 与 `0x108260` 都从这里读，H）→ 一律不加入候选，不读 0x80 位。
   候选刚体没有对象（`0x108260` 返回 0）时也按编号判断（旧代码此时直接交给原版）。僚机穿弹、炸弹穿本编队、其它一律原版，行为不变。
   覆盖范围：同一个收集器的候选同时供 map 最近命中（`core+0x8C8`）和对象 shape cast（`core+0x8D8`）使用（`docs/bullet-pass-re.md` §2），
   所以不加入候选就两条都不会命中。唯一不经收集器的是 layer 22 的地图射线，它只与地图 / 建筑层碰撞（`docs/raycast-re.md`），不含载具。
2. **炮口按真实相对弹道选，开火前逐帧验证**（`src/gunmuzzle.h` `Launch` / `Clears` / `PathInside`；`src/jet_bay.cpp`
   `GunshipMuzzle` / `GunshipClears`）：
   - `Launch`：出膛点沿「弹速向量 − 机体速度」（机体坐标）从盒子中心走到盒子外扩「命中半径 + 0.5 m + 一帧航程」处；机体静止时就是旧炮口。
   - `Clears`：按机体速度与角速度（Jet 条目的 `m.vel` / `m.omega`，NPC 与玩家机都有）逐帧（每帧 4 步）把弹位置换到当时的机体坐标，
     0 帧和 1 帧延迟都跟到瞄准点或 40 帧；命中球碰到机体包围盒就不打，记日志
     `JET v=... gunship <炮> held: own airframe (<谁>): ...`（不受 Debug 限制，每门炮最多 2 秒一行）。加特林散布后的实际瞄准点也验证。
   - 弹的参数表 `gunmuzzle::kCannon / kGatling / kShell`（弹速、下坠、命中半径），机炮 / 加特林的提前量弹速用 `static_assert` 与之对齐。
3. **诊断日志**：钩子拦下自己的弹时计数并打 `GUNSHIP own round kept off its airframe: N candidate bodies (... by a part's body id only, not its object) ...`
   （非炮舰机写 `JET`；第一条立即打，之后最多 5 秒一行，不受 Debug 限制）。下次用户日志里出现这行就证明「原版 owner 排除没挡住、
   弹真的碰到了自己」，「by a part's body id only」不为 0 就说明部件刚体映射到的不是机体指针。

## 测试

- `tools/own_round_check.cpp`（新，进 `offline_checks` / CTest）：自己的弹按对象 / 按部件刚体编号（对象是别的或为空）都排除；
  别的喷气机的刚体编号不算；僚机、炸弹、他编队、非喷气机弹与旧规则一致；旧规则（`Before`）确实把自己的弹交给原版。
- `tools/gunship_muzzle_check.cpp`（扩充）：原有静态检查不变；新增动态部分——4 种盘旋 × 三门炮 × 目标在圆心 / 圆外，直线飞行
  145 / 74 m/s 目标在前方、拉起 0.6 rad/s、滚转 1.2 rad/s、转弯 0.3 rad/s、目标在后方，以及一种假想的下坠弹（验证弹道求解落点与
  路径跟踪）。断言：新炮口打出的弹没有一发碰到机体；`Clears` 从不把真实会碰到的路径判为通过（对新旧两种起点都核）；盘旋时没有被
  `held` 的弹、朝前飞行时被 `held` 的不超过 1/10（实测全是 0）；旧炮口在朝前飞行场景必须仍显示问题（场景有效性）。
- `tools/selftest.py` `gunship_muzzle_wired`：钩子走 `ownround::Judge` 且带刚体编号、`Publish` 读刚体、三处开火前都有 `GunshipClears`、
  弹参数与 `make_jets.py` 一致、两个离线检查都在 CTest 里。
- `tools/make_jets.py check_gunship_muzzle`：额外核对原版炮弹 #5 = `SHELL_SPEED` 8、#6 = `SHELL_FALL` 0（对本机游戏文件实跑通过，
  改成 9 时报错）。

## 未实测

- 没有进游戏：新造弹对象是当帧还是下一帧走第一步；IFC 弹的 0x80 位在实战中是否被置位；部件刚体的 key-0 对象是不是机体；
  `GUNSHIP own round kept off its airframe` / `held: own airframe` 在实战中出现的频率。
- 炮弹 IFC 自己的 ±1.2 m 水平散布（param #0，`+0x224`）没有算进 `Clears`（相对数百米射程是 0.1° 级）。
- 联机时由玩家开火的弹，owner 是玩家本人而非炮舰机（`ShellMake` 的 `OnlineAttacker`），这时不走本次的 own 排除，仍由原有的
  `SparesOwnRide`（玩家所乘载具）处理。
- 机体用的是整机包围盒（52 × 4 × 16 m），比真实外形保守：贴近机翼平面开火的极端角度可能被 `held`，离线场景里没有出现。
