# NPC 驾驶的机体 / 载具不开火、上不了车（2026-10-12）

## 用户原话

- 「尼克斯不开火，上传了日志」
- 测试站报告 #12（n0xe，2026-10-11 22:05，build fb4c60c）：「派NPC驾驶的战斗机体不开火 NPC普罗透斯不开火，并且地图上无法选中 试图派NPC驾驶电磁炮时闪退 NPC无法驾驶巴尔加」
- 报告 #5（10-07）：「泰坦重战车不会使用主炮」

## 报告 #12 的日志与当前 main 的关系

- fb4c60c 是 10-11 17:33 的 PR #138 合并点，**不含** PR #146（0f988cd：机甲开火读骨骼记录、地图集火）。
  报告里 12 条 `FAULT npc ai: C0000005 at EDF+0x63AED9`（Coop 日志同时段 16 次，栈经 EDF6VehicleCrew+0x1E342B / +0x1DA569）
  就是 #146 修掉的那条：故障后该士兵的帧退回原版，机甲随之不再开火。当前 main 实测 0 条 FAULT。
- 巴尔加：`board order dropped (too long)` 每 20 秒一次，座位始终空。普罗透斯：NPC 只坐进 2、3 号座，0/1 号座的指令全部超时。
- 普罗透斯「地图上选不中」：地图只列有 NPC 驾驶员的载具（`npcpost.cpp Available()` 要 `NpcDriver(v)`），驾驶座没人就列不出。

## 根因

1. **上车指令固定 20 秒期限**（`npcai.cpp kBoardMs`）。可派车距离是 `kBoardFar` 150 m，士兵步行实测约 2.3 m/s
   （本次测试：离普罗透斯驾驶座 95 m 的士兵 48 s 才坐上，中途已被丢弃一次）。远一点的座位走到一半就被丢弃、重新分配，
   用户局里小队跟随 / 交战再把人拉回去，于是普罗透斯驾驶座、巴尔加永远上不去。上车点本身在地面高度（实测 y≈0.8，可达半径 2.3–4.5 m），不是几何问题。
2. **普罗透斯的 NPC 驾驶员攻击表为空**（`npcpost.cpp AttackRows`）。插件补攻击表只取 0 号座的武器；普罗透斯 `_MISSION` /
   呼叫版的 0 号座没有武器（`vehicle_weapon_setting`：左炮 1、右炮 2、发射器 3），表为空 → `EnsureMechAiSetup` 失败 → 不跑 AI。
   原版 AI 版 `VEHICLE407_BIGBEGARUTA_AI` 的 `ai_attack_setting` 就是 0/1/2/3 四个座位各一行；普罗透斯的 AI think 0x648F70
   对 1..n-1 号座逐个调用火力入口 0x63AC40（0x649055..0x6490AA）。炮手座上的 NPC 也走不了车辆的炮手逻辑
   （`NpcGunnersInput` 要求 vtable 有 kSeatFire 槽，机甲家族只有 57 槽），所以普罗透斯唯一的开火途径就是这张表。
3. **V614_PROTEUS_MK2 模型没有 `spine` 骨骼**（spine -1，neck 4，body 3；407 有 spine）。原版 slot 6 写死 `L"spine"`
   （0x642C32），插件同样写死，找不到就不开火。

## 改动

- `npc_logic.h BoardWalking` + `npcai.cpp Board`：上车指令「20 秒内没有更接近座位 1 m 才丢弃」，持续接近就一直有效；
  被挡住、被拉走、原地不动的仍在 20 秒后放弃并重新分配。丢弃日志带上车点、可达半径、士兵位置与距离。
- `npcpost.cpp AttackRows`：所有座位的武器各一行（座位号写进行里）；单座机甲（维加尔塔、尼克斯）结果不变。
- `npcpost.cpp EnsureMechAiSetup`：开火原点 `spine` → `neck` → `body`（SGO `begaruta_pilot_class` 里驾驶座的瞄准骨骼）。
- 诊断（Debug=1，按载具节流）：每台 NPC 驾驶的载具每 10 s 记一行「目标、距离、各座位弹药」；每次上车分配；
  驾驶座瞄准走了哪条路；NPC 选武器时每件武器被选 / 被跳过的原因（`PAYLOAD npc ... [0:ok 1:out-of-reach]`）。
- 测试用：ini `NpcCrewTest=1`（空车不必玩家先开过就自动派人）；测试场计划 `squads`（玩家身边放 N 支 6 人友军小队）。

## 验证

离线：`npc_ai_check` 1394 例 0 失败（新增 `Boards`：走 95 m 的士兵过 20 s 仍保有指令；原地不动 / 被拉远 / 抖动不续期）；
`npc_post_test` 78 条通过（新增：类普罗透斯机甲每个炮手座武器一行、座位号正确；无 spine 用 neck；全无则不开火）；
ctest 225 项 0 失败（依赖游戏原生的审计项 skip）。两处变异实测：去掉「接近续期」、退回只看 0 号座，新断言都会失败。

真机（本机 EDF6，tests/autopilot 后台跑测试场，空车 + 2–3 支友军小队 + 蚂蚁波次，每轮结束 Mods 逐字节还原，无新崩溃转储）：

（结果见下表，按每台载具每 10 s 的弹药余量判断是否开火）
