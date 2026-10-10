# 2026-10-10 飞行怪：越飞越高、无人机和 NPC 不打蜜蜂（air-chase）

## 用户原话

> 飞行怪好像会导致越飞越高。飞行怪在打无人机，但是无人机和一堆npc竟然没有攻击蜜蜂

「蜜蜂」是 EDF6 的飞行敌人（巨蜂类）。插件对它们没有按种类判断：`jet_combat.cpp Flies` 按几何判定——敌人根部离下方地面超过 15 m（或下方找不到地面）就是飞行目标。巨蜂落地时算地面目标，飞起来算飞行目标（本局日志 13:26:28 同一个 `27D50C9EF10` 先是 `root y=0 ... on the ground`，后来全程 `(air)`）。所以「空中目标」判据覆盖巨蜂，问题不在分类。

## 日志证据（游戏目录 `Mods\Plugins\EDF6VehicleCrew.log` / `.log.1`，本局从 13:25:27 开始）

本局在场的插件飞机只有：母舰（doll carrier）、doll 自爆无人机、炮艇机（gunship），以及三架 NPC 直升机；没有战斗机 / 多用途机。

1. **越飞越高**：4 架 doll 同追 `0000027D50C9EF10`，`13:32:16 y=486 → 13:32:41 y=753 → 13:33:04 y=1009`，`vy` 顶在 12.5（doll 爬升上限），`dist` 在 28–116 m 之间来回，始终贴不上；doll 的 HP 一路掉（目标在打它们）。目标始终在 doll 上方几十米：doll 的终点就是目标锁定点，目标又一直保持在 doll 上方，两者一起往上爬，直到 1200 m 天花板。
2. **无人机打不到**：`.log.1` 的 `50EF5B70` 在 13:28:28–13:29:37 一直 `y=100 dist=15-16 vy=-7.3`——命令往下飞、身体纹丝不动（被目标身体从下方顶住），但自爆只在 `d<6 m`（kDollTrigger）或 `Sense` 报 walled 时 `d<24 m` 才触发；而 `Sense` 对上下方向的阻挡直接 `return false`（「held from below or above: Guard's」），所以永远不算 walled，最后 `sortie over` 空手返航。
3. **NPC 不打**：13:32–13:33 只剩 2–3 个敌人，士兵（reach 150 m）一直锁那只 900 m 高的蜜蜂，`fire=0`（`13:32:54 Ranger ... pos=(86,0,23) reach=750 target=0000027D50C9EF10 fire=0`）；等它降下来后（13:33:35）才 `fire=1`。`npcai.cpp PickTarget` 只按水平距离筛选、按三维最近选，不看自己武器射程，所以够不着的目标和够得着的目标同等对待。
4. **炮艇机**：本局 `gunship patrol air` 73+120 条，全程 `fire=0/0`——`jet_bay.cpp GunAtTarget / GunshipFire / CrewShell` 遇空中目标直接 return，而 `VisitTarget` 只给「非偏好」类目标加 2000 分，没有地面目标时照样锁空中目标，于是整趟不开火。
5. **直升机**：本局三架直升机从未把 `27D50C9EF10` 当目标（它们的目标在自身航区内，日志里有 46 次 `gun=1`），没有证据显示它们对巨蜂不开火，本次不改。战斗机 / 多用途机本局不在场，无日志证据，本次不改（代码上它们按 `Prefer::air` 追空中目标并开火，无类似的整类排除）。

## 根因（file:line 为修复前 origin/main 90e8e85）

| 现象 | 根因 |
|---|---|
| 越飞越高 | `src/jet.cpp:240-245` 有目标时 doll 终点直接取目标锁定点（含高度），只有贴地目标才抬高；`src/jet_flight.cpp:733` 垂直速度 `(goal.y-pos.y)*0.5` 只受爬升上限约束，没有高度包络；`src/jet_combat.cpp:125` 当前目标不受距离约束；`src/jet_combat.cpp:96` 地面探测射线只往下 400 m，400 m 以上的飞行物「下方没有地面」，任何按地面高度的判断都无从做起 |
| 无人机贴着不炸 | `src/jet.cpp:596` 只有 `d<trigger` 或 `walled && d<trigger*4` 才引爆；`src/jet_flight.cpp:387` 上下方向被顶住不算 walled |
| 追不上不放弃 | 没有「接近进度」概念：只要目标还在 range 内就一直追，母舰也一直向同一目标放 doll（`src/jet_carrier.cpp:243`） |
| NPC 不打 | `src/npcai.cpp:479-488` 排名不看射程；`src/npcai.cpp:587` 射程外不开火 |
| 炮艇机不打 | `src/jet_combat.cpp:131` 空中目标只加分不排除，而炮艇机的三种武器都打不了空中目标 |

## 修法

新增纯逻辑头 `src/air_chase.h`（无游戏状态，可离线测试）：

- **武器限制 `airchase::Limits`**：炮艇机 `groundOnly`（不选任何空中目标）；自爆无人机（blast / doll）和放它们的母舰带 `kChargeCeiling=200 m` 高度上限——目标锁定点离其下方地面超过 200 m 就不选，**当前目标也不例外**。`jet_combat.cpp VisitTarget` 对每个候选调用 `airchase::Allowed`。地面高度来自同一条飞行探测射线（memo 里多存了 `ground/groundY`），射线深度从 400 m 改为 `kFlyerDepth=2000 m`（超过 1200 m 天花板），目标下方确实没有地面时用无人机自己最近看到的地面代替。
- **当前目标的距离约束**：自爆无人机及其母舰的当前目标也必须在 range 内（原来只有地图命令才约束当前目标）。
- **接近进度 `airchase::Step`**（`jet.cpp JetFrame`）：每架无人机记录对当前目标的最近距离和最近一次「再近 ≥1 m」的时间：
  - `d<trigger` 引爆（原样）；
  - 在 `held=min(trigger*kTriggerHeld, 爆炸半径)` 内，`walled` 或 1.5 s 没再接近 → 引爆（覆盖被目标身体从任意方向顶住，日志里 15–16 m 的情形；爆炸半径 doll 25 m / blast 15 m，来自 `pylib/vcobjects.py` 的 `EDF6VC_*_CHARGE`，写进 `Kind::blast`，selftest 核对两边一致）；
  - 更远处 10 s 没再接近 → 放弃该目标，并把它加入自己和母舰的回避表 20 s（`airchase::ShunList`，4 项），母舰在这段时间不再向它放无人机。地图集火（focus）目标不放弃（玩家的明确指令）。
- **终点高度兜底**：`Rotor` 里自爆无人机的终点高度夹在其下方地面 + 200 m 以内（两次目标探测之间也不会超出包络）。
- **NPC 选目标**：`npc_logic.h` 新增 `TargetScore / PickTarget`：射程（最长武器 reach）内的目标一律排在射程外的前面；都在射程外时仍选最近的（士兵会朝它移动）；当前目标保持 20 m 优先。`npcai.cpp PickTarget` 改为调用它。
- **日志**：`JetLog` 增加 `ty=`（目标高度）；放弃目标时记一行 `gives up ... target y=...`。

## 测试

- `tools/air_chase_check.cpp`（新，CTest `air_chase_check`）：25 个用例——武器限制、终点夹高、引爆/被顶住/追不上的判定（含日志里的 16 m 顶住和 28–110 m 追不上两种）、回避表；以及用替身 doll（爬升 12.5 m/s）追一个 13 m/s 爬升的飞行物：放手且始终不超过 200 m。
- `tools/jet_obstacle_sim.cpp --selftest`（CTest `jet_attack_runs`）新增 9 个 `limits` 用例，走生产 `PickTarget / VisitTarget`：战斗机照样选 483 m 高的飞行物；炮艇机不选飞行物、选地面目标；doll 不选超限目标、超限的当前目标被放掉、超出 range 的当前目标被放掉；doll 母舰不选超限目标；回避期内不选、期满再选。
- `tools/npc_ai_check.cpp` 新增 `Targets()`（9 个断言）；`tools/npc_ai_mutate.py` 加 3 个变异（无视射程 / 无视范围 / 不保持），27/27 全杀。
- `tools/selftest.py` 新增 `air_chase_wired`（接线与爆炸半径一致性），更新 `soft_edge_wired` 的 VisitTarget 文本。
- 结果：`build.cmd` 退出码 0；`ctest` 207/207 通过（原生测试 Skipped）；`selftest.py` 148/148。
- 变异实测：见提交说明 / PR 正文。

## 未实测

- 全部未进游戏实测（规则禁止碰游戏）。
- 200 m 高度上限、1.5 s 顶住判定、10 s 放弃、20 s 回避都是按日志估的值，需实机日志（`ty=`、`gives up`）确认。
- 被顶住在 16–24 m 处引爆时 doll 25 m 爆炸半径能否真正伤到目标（锁定点到身体的距离因敌人而异）未实测。
- 巨蜂之所以一路爬升（是否在追 doll）只是从日志推断：目标一直在 doll 上方几十米；本修复让 doll 在 200 m 处放手，无论哪一方主导都打断这个循环。
- 战斗机 / 多用途机 / 直升机对巨蜂的表现本局无日志，未改。

## 2026-10-10 用户更正：要打飞行怪，不是放过它们

> 自爆无人机不再越飞越高、追不上会放弃；炮舰机不再选空中目标 这个谁说的 完全和我说的反了。 我是说明明天上有怪，但是无人机和空母却不打，谁说炮舰机之类的不选空中目标了

上面「修法」里的三项都是在减少对飞行怪的攻击，与用户要的相反，已撤回：

- **撤回**：炮艇机 `groundOnly`（不选空中目标）；自爆无人机 / 母舰的 200 m 高度上限（选目标与终点高度夹紧 `UnderCeiling`）；追不上 10 s 放弃 + 自己和母舰回避 20 s（`ShunList`）。`air_chase.h` 只剩 `Closing / Step`（引爆判定）。
- **保留**：被目标身体顶住、在爆炸半径内 1.5 s 不再接近就引爆（「贴着蜜蜂不炸」的根因修复）；自爆无人机 / 母舰的当前目标超出 range 就放掉；NPC 优先射程内目标；目标探测射线 2000 m；日志 `ty=`。
- **越飞越高的真正原因与修法**：自爆无人机的垂直速度上限是 `cruise*0.5`（doll 12.5 m/s），日志里蜜蜂在它上方约 13 m/s 爬升，于是永远差 28–110 m、一起爬到天花板。现在追**飞行目标**时用满 `cruise`（doll 25 m/s）爬升（`jet.cpp Rotor`），追上后由 `Step` 引爆；地面目标照旧半速。
- **炮艇机打飞行怪**：侧炮（机炮 / 加特林）是直线弹、本来就按目标速度算提前量，`GunAtTarget` 去掉 `j.t.flyer` 排除；`GunshipFire` / `CrewShell` 对飞行目标照样开侧炮，只有炮弹（DemoIndirectFire，落点打击）仍只打地面。选目标仍是地面目标优先（`Prefer`），附近没有地面目标时打飞行目标。
- 测试：`air_chase_check` 改为「追不上时继续追」+ 替身无人机半速追不上、全速在 400 m 内追上并引爆；`jet_obstacle_sim` 的 flyers 组：战斗机 / 炮艇机（无地面目标时）/ doll / doll 母舰都选 483 m 高的飞行目标、doll 保持该目标、炮艇机有地面目标时先打地面、超出 range 的当前目标放掉；selftest `air_chase_wired` 反向钉住（`Shunned` / `Allowed` / `kChargeCeiling` / `groundOnly` / `UnderCeiling` / `giveUp` 不得再出现）。
- 未实测：未进游戏；炮艇机侧炮对快速飞行目标的命中率、无人机全速爬升追蜜蜂的实际表现需实机日志确认。
