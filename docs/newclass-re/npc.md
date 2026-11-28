# NPC 新兵种：支援载具手 / 刻印剑士（第 6 阶段）

计划见 [plan-2026-11-28-zero-worldline-gear.md](../plan-2026-11-28-zero-worldline-gear.md) §0.1 第 7 条、§3 第 6 步。**全部未实测**，实机清单在文末。

## 1 兵种与本体

- 支援兵种 `SupportWeapon::zwVehicler = 15`、`zwRuneBlade = 16`（`support_call.h`）。它们只在 `SupportKindInstalled` 为真时出现在编成、地图按钮和解析里。装配每名士兵占 5 位，房间协议升到 28（`mod_room.h`），全员要求同版本。
- 本体由 `tools/make_zw_classes.py` 从 `N601_COMMON_RANGER_AF[_LEADER]` 生成，换成新兵种工厂：
  - 载具手 `OBJECT/EDF6VC_ZW_VEHICLER[_LEADER].SGO`：武器只有 `ZW_Gyrojet01`，下车就用它。
  - 刻印剑士 `OBJECT/EDF6VC_ZW_RUNEBLADE[_LEADER].SGO`：Ranger 模板唯一武器位本来就列了载入 `[0, 1]`，这里装 `ZW_RuneCarbine01`（0）和 `ZW_RuneSaber01`（1）。`npcai.cpp ChooseArm` 按射程在两把之间切换，敌人贴近时才换刀。模板武器位不是 `[0, 1]` 时生成器直接报错。
- 新兵种不接玩家配装（`npc_arms.h`：`kArmsKinds` 以外的兵种不带 `arms` / `offhand`），带的是本体自带的武器，所以没有 NPC 星级曲线。
- 每场预载 `soldier0 + 16`，`zwVehicle0 = 96`（`preload_set_logic.h`）。

## 2 载具手自带车（目录 1016，`kZwCrewCatalog`）

- **登记**：支援步兵出场后（`support_dispatch.cpp SpawnDeployment`，只在本机拥有时），`NoteZwCrewVehicler` 登记 `ZwType == 4` 的士兵，最多 16 名。只在房主或离线时登记。
- **请求时机**：士兵在地面（`kHumanRiding` 为空）站稳 `kSettleMs` 后才请求造车（`zw_crew_vehicle_logic.h`）。
  - 被拒后隔 `kRetryMs` 再试，最多 `kMostTries` 次，之后放弃并记一行日志。
  - 联机时同一时间只发一单。
- **造车**：
  - 离线：直接 `Apply` / `Activate`。
  - 联机：房主经 `SubmitSupportRequest(1016, …)` 走支援交易，各端按同一份计划造同一车体 `ZW_V1_HSTVL`（kind 0，开局即解锁的那辆）。
  - 计划的 `variant` 是票据 `kind | slot<<8 | generation<<16`，`ValidateZwVehiclePlan` 校验它。`PlanZwVehicle` 只接受 peer 0（房主自己）发起、且票据对应的士兵还活着的请求。
- **上车**：车造好后由房主 `NpcBoardCrew` 把士兵送上驾驶位。车主就是该士兵（`OwnerOf`），只有它能坐驾驶位；士兵死后车不再归任何人。开车作战走插件现有的 NPC 驾驶管线。
- **放弃**：地图在地下，或该车没装好（资源缺失）时放弃，记日志，不出车。

## 3 刻印剑士踏阵（`runeblade_npc_logic.h`，`npcai.cpp RuneHop`）

- 剑士 NPC 的本体是 AssultSoldier，它自己的跳键 `+0xD76` 在地面是翻滚，抬不起来。所以补了一个按对象驱动的最小接口 `RuneBladeNpcTakeoff(soldier)`（`runeblade.h`）：
  - 它只接受本机的 NPC 剑士，不接受玩家；
  - 剑士下一帧在地面时，`runeblade.cpp` 写游戏自己的跳跃请求：`+0x1294` = 10（原版地面跳速度），`+0x1290` = 1。
  - 这个请求在地面也会被 `0x575A7A` 接走：`0x11B9450` 写竖直速度、清 `+0x711` 并脱离地面；之后游戏按本机的普通跳跃同步给其他人（`0x575AB9`）。以上是由汇编推断的。
  - 地面起跳不耗以太。
- **空中踏阵**：照常是跳键加摇杆，由 `runeblade.cpp` 的帧钩 `StepFrame` 扣以太（NPC 本来就走这条路，帧钩不分玩家和 NPC）。
- **触发条件**：
  - 插件自己的走路（`WayOf`），剩余距离超过 25 m，`DashFree`，控制掩码允许移动，以太 ≥ 3 步；
  - 每跳最多 2 步，两步间隔 ≥ 350 ms，空中帧数要超过 `kAirborneFrames`，并且始终留一步的以太；
  - 落地后歇 2.5 s；起跳 0.8 s 内没离地就算结束。
- 剑士 NPC 不再翻滚（不走 `SprintRoll`）。

## 4 不支持 / 遗留

- **近战特技只对玩家**：`runeblade_melee.cpp` 只驱动本机玩家（`ReadEquipped(0, …)`、`MyRef` 要求玩家引用），它管第 3 段光痕、急坠下劈、长枪 / 环 / 锤 / 居合、R2 / R3。NPC 拿刀只有原版武器自身的斩击伤害。要给 NPC 加这些特技，要把 `Local me` 改成按对象的表，房主结算伤害，再给 ZWRM 加 NPC 引用（接收方只认房主发的）。这些都没做。
- **NPC 以太不广播**：ZWRB 的 state 只发给有玩家的剑士，客人那边看不到 NPC 剑士的以太值。踏阵本身靠游戏的跳跃同步和位置复制，各端看到的动作一致。
- **NPC 驾驶员可能不开火**：自带车 `SpawnZwVehicle` 出的车体是否在 AI 表里（`0x118A4B0` / slot 6）没核实。如果不在，NPC 驾驶员可能不开火。
- **房主自己叫支援会被挡**：自带车的请求占用房主自己（peer 0）的请求通道。一单在途时，以及之后 2 s 内，房主本人发的地图呼叫会被拒（「its map call in flight or queued」）。
- **客人看不到车主**：客人不知道自带车的车主，`ZwVehicleOwner` 在客人端返回 nullptr。
- **预载有内存代价**：`ZW_V1_HSTVL` 现在每场都预载。
- **外观是临时的**：预览和外观用的是 Ranger 模板。

## 5 实机清单（一次启动采齐）

1. 支援叫一队载具手：落地约 2 s 后身旁出 `ZW_V1_HSTVL`，士兵上驾驶位，开车跟队、开火。看日志里的 `ZW CREW` 行。
2. 载具手下车（车毁）后用陀螺喷射手枪作战。
3. 支援叫刻印剑士，下长路程移动命令：看 `RUNEHOP takeoff` / `step` 行，确认确实离地、空中踏两步、以太下降、落地回充。
4. 剑士贴近敌人时换刀斩击，远处用卡宾枪。
5. 联机（房主加一名客人）：客人端同一辆车、同一驾驶员；剑士的跳跃两端一致；房主本人在自带车请求在途时叫一次支援，看会不会被拒。
