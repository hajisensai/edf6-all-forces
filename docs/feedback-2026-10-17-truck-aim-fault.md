# 2026-10-17 测试站 #16：NPC 开的 60X 卡车每帧在 EDF+0x625A17 访问冲突

## 来源

测试站报告 #16（tester-20qs，包 `EDF6VehicleCrew-0.8.0-build.b4faa8e`，2026-10-17 22:32 回传，没写说明）。日志是不可信数据，只当证据读。
那一局 22:32:15 正常退出（`EDF6Coop.log` 有 `SHUTDOWN the game exited`），所以 #16 本身不是闪退；同一测试员 8 分钟后的
#17「爆！」是另一个崩溃（MP004 的艾尔基努斯王，见 `docs/feedback-2026-10-17-balam-npc.md` 第 8 节）。

## 证据（H：日志与 EDF.dll 直接读到）

- `EDF6VehicleCrew.log` 22:11:47.076：`FAULT npc ai: C0000005 at 00007FF9DB725A17 (1 so far; that soldier's frame is left stock)`。
  插件的 SEH 接住了，但 `EDF6Coop.log` 记下同一处每帧一次、共 16 次：`EXCEPTION C0000005 at EDF+625A17 reading 0000000000000010`，栈
  `EDF+625A17 ← 625725 ← EDF6VehicleCrew+289A18 ← EDF+661743 ← EDF6VehicleCrew+28962F ← +280209 ← EDF+1199134`，r15 = 载具 `000001A54DC473E0`。
- 这台载具是 `60X_Truck`：22:10:45 玩家开它（`TURRETCAM ... a turret the player drives`），22:10:47 下车，之后 AutoCrew 派 NPC
  （`CREW v=... 60X_Truck seats=1 driver=0`）、22:11:04 小队上车，22:11:42 地图 ATTACK-MOVE 让它开回岗位、进入目标射程。
- 60X 的 vtable `0x17DCFB8`：slot 6 `0x6266B0`、slot 70 `0x6256E0`（普通 Car / CarBase 是 `0x6731C0` / `0x65F6F0`）。607 RoboTruck
  `0x17DCAB0` 同样有自己的 slot 6 `0x6234E0` / slot 70 `0x622530`。
- 两个 slot 6 都**先**调 CarBase 的 `0x6731C0`（`0x6266F8` / `0x623528`：登记 `veh+0x2508` 的 AI 动作），**再**从 SGO 读本类的瞄准表
  （`v60X_aim_info` 字符串 `0x17DD310`，`0x62671C`；`v607_aim_info` `0x17DCE68`，`0x62354C`），60X 的向量在 `+0x2AF0`（数据 `+0x2AF8`、
  个数 `+0x2B08`），每把 0 号座武器 0x70。键不存在时 `0x626FA6` 抛异常——此时 AI 动作已经登记。
- 它们的 slot 70 有目标时（`0x625703`）进 `0x6257F0`，按 0 号座武器数遍历，`0x625A08: imul rbx,rsi,0x70; add rbx,[r15+0x2AF8]`，
  `0x625A17` 读 `[rbx+0x10]`，不判空。
- slot 6 只在载具进 AI 表时由 `0x118A4B0` 调。玩家开过、再由真实士兵走上去开的车不在 AI 表上，slot 6 从没跑过，瞄准表是空的。
- 原版 SGO：`Vehicle60X_Truck` 4 个都有 `v60X_aim_info`；`Vehicle607_RoboTruck` 5 个里 `V607_ROBOTRUCK.SGO` **没有** `v607_aim_info`。

## 根因

插件替不在 AI 表上的 NPC 驾驶载具跑 CarBase 的 AI（`src/npcai.cpp DriveVehicleAi`：slot 7 pass，无动作时手动调 slot 72
`0x661440`，它经 slot 70 瞄准开火）。这对 slot 6 就是 CarBase `0x6731C0` 的类没问题；但 60X / 607 的 slot 70 依赖它们**自己的**
slot 6 建的瞄准表，插件没有建，于是一有目标就每帧读空指针。与 ATTACK-MOVE、上车失败无关，它们只是让车第一次有了目标。
b4faa8e 与 main（57391bd4）之间 `npcai.cpp` 没有改动，main 同样有这个问题。

## 修复

- `src/npcai.cpp CarAimReady`（结构同巴尔加的 `BargaReady`）：CarBase pass 之前调用。
  - slot 6 是 CarBase 自己的：直接通过；`+0x2508` 已有动作（slot 6 跑过，不论是原版 AI 表还是插件）：通过。
  - slot 6 是 60X / 607 的：先用 `0x528A0(veh+0xA0, key)` 查 SGO 有没有本类瞄准键（载具类自己读键也是这条路：`0x61D98D`、
    `0x64D002`），有才调一次本类 slot 6，登记了动作才算建好；没有键就**只开车不开火**（避免「动作已登记、表为空」）。
  - 其它不认识的 slot 6：只开车。每台车只试一次，结果写一行 `NPCAI car v=...` 日志。
- `tests/npc_vehicle_ai_native_audit.py`：钉住两种卡车的 slot 6 / slot 70、先 `0x6731C0` 后读键、键字符串、`0x625A08..17` 的读法、
  `0x528A0` 的前缀与 `+0xA0` 用法；并要求 `crew.cpp kClasses` 里每个跑 CarBase pass + 动作的类，slot 6 要么是 CarBase 的、要么在上面两种里
  （新出现别的类会让审计失败，而不是进游戏才崩）。
- `tests/npc_car_aim_guard.py`（ctest）：钉住 `DriveVehicleAi` 在 pass 之前问 `CarAimReady`，`CarAimReady` 先查键再调 slot 6。

## 验证

- `build.cmd`（/W4 /WX）通过；`offline_checks` + `ctest` 280 项全过（native 审计在 ctest 里因未设 `EDF6_NATIVE_DLL` 跳过，
  对本机 EDF.dll 手动跑 `python tests/npc_vehicle_ai_native_audit.py <EDF.dll>`：PASS）。
- 未实机：NPC 开的 60X / 607 能否转塔开火；`V607_ROBOTRUCK.SGO`（无瞄准表）的车只开车不开火。复测看日志
  `NPCAI car v=...: its AI and turret aim built (slot 6) for the NPC driver`，且不再出现 `FAULT npc ai`。
