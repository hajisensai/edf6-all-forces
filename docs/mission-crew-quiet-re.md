# 任务 AI 载具乘员的台词 / 标记 / 上车声（逆向，测试站 #41）

2026-11-08，EDF.dll（Steam，TimeDateStamp `0x678CCB46`）静态反汇编，`tools/edfre.py` 只读。H = 读到分支与写入；M = 由用法推断；L = 未查清。

## 1. 士兵自己的无线电台词（含字幕与说话人图标）

| 项 | 内容 | 可信度 |
|---|---|---|
| 闸门 | TalkManager 的单人说话入口 `0x5DF050(mgr, speaker, ...)`：`0x5DF08C test dword [rdx+0x380], 0x40000; je 0x5DF442`——说话人 GameObject `+0x380` bit 18 清掉就直接返回，后面的语音、字幕（含说话人兵种图标）都不出 | H |
| 谁调它 | `0x5DAED3`（在 `0x5DAD50` 内）与 `0x5DF03E`（在 `0x5DEF60` 内）。`0x5DAD50` 唯一的调用者是 `0x7575DF`（`Event_TalkManager` 收包 `0x7575D0`，`eos_Event_TalkManager.cpp`） | H |
| 脚本开关 | `SetCharacterRandomTalk(const Object&, bool)` 注册到 `0x1B9F10`，就是这一位，别的不动 | H（注册）/ M（只这一位：读实现） |
| 默认值 | 士兵出生时这一位是置上的（插件测试夹具里的 `0x4004420|0x40000` 取自原版默认标志） | M |
| 联机 | 台词是 `Event_TalkManager` 事件，每台机器收到后用**自己那份对象**的 `+0x380` 判闸门，`+0x380` 本身不随网络复制。所以是本地表现状态：每台机器写自己的副本即可，不需要新协议 | H（收包路径）/ M（不复制） |

插件的处理（`src/mission_crew_quiet.h` 纯逻辑、`src/mission_crew_support.cpp` `QuietCrew`）：任务 AI 载具的某名乘员坐在**本部署自己的那辆车**里（`human+0x1548 == 部署车辆`、活着、车活着、插件开着）时清这一位，并记下「欠一次恢复」；一旦不满足（下车、死亡、车毁、部署回滚、插件关）就把这一位放回。下车前这一位本来就是 0（脚本关过）时不记欠账，下车后也不放回。对象已经不在（不是登记的那个对象）只忘掉、不写。任务结束（`ResetMissionCrewSupport`）不写：场景归任务拆除管。

## 2. 「友军标记」

被反馈的「显示游骑兵图标」有两处来源：

| 来源 | 内容 | 处理 | 可信度 |
|---|---|---|---|
| 台词字幕的说话人图标 | 与第 1 节同一闸门 `0x5DF050`，清 bit 18 后不出 | 同第 1 节 | H（闸门）/ M（图标随字幕走，未截图） |
| 头顶的合流标记 `合流マーク.dds` | `CharacterGhostCamera::InterflowMarkDraw`（vtable `0x1768BC8`）第 1 槽 `0xF7280`：`+0x540 != 0` 且 `+0x548 == 0` 时把 `+0x90`（y + 2.25 m）推进位置表并画标记。**不看是否在车上**：坐在车里的士兵 `+0x90` 在车里，标记就画在车顶。任务乘员的驾驶员用支援队长资源出生，`support_soldier.cpp` 写 `+0x540 = 1`，又没有上级，正好满足 | 插件已有的 `JoinMarkHook`（`npcai.cpp`）在 `WalkUpOff()` 时整体不画；现在另加：本机登记的任务乘员坐在自己的车里时也不画（`VanillaRecruit=1` / 关 AI 时同样）。只在原版自己的条件成立时才去查登记，不给每个士兵的遍历加 `Readable` | H |
| 雷达 `HUiHudRader` 第 2 槽 `0x82B4D0` | 按队伍集合取对象 `+0x90` 位置画点（`0x82B220`），不分兵种图标；车里的士兵与车重叠成一点 | 不改 | H（数据来源） |

`InterflowMarkDraw` 是本机 HUD 对象，联机时每台机器按自己的副本画：本地表现，无协议。

## 3. 上车声

| 项 | 内容 | 可信度 |
|---|---|---|
| 写入 | 原版 SeatRide `0x633C10` 把新乘员的座位记为占用并置「本帧变化」位（`0x633D90 → 0x633FE0`：`or [+0x628], bit; or [+0x628], bit << [+0x620]`；`0x633FE0` 的直接调用者 `0x633B6D / 0x633BFE`） | H |
| 播放 | 车辆更新（车 `0x674B1A`、直升机 `0x652713`、步行机 `0x6437D0`）调 `0x632A60`，有变化位的座位：有人播上车预设、没人播下车预设；基类更新 `0x630596` 清变化位。细节见 `src/seat_events.h` | H |
| 本机 | 插件自己的上车 `Board`（士兵走到上车点后调 `RideVehicle 0x5765E0`）、`NpcRestoreMissionSeat`（回到原座位）与 `NpcMoveSeat`（玩家上车时 `crew.cpp` `Bump` / `MoveRider` 把乘员挪到别的座位；对方机器收到这次上车消息同样走 `RemoteRide` 静音，两边一致）之后，若是任务乘员坐进自己的车，立刻 `seatevt::Quiet`：车辆视这个座位为一直有人，下一帧没有变化位可播 | H |
| 联机副本 | 对方机器的上车消息 `HUMAN_EVENT_VEHICLE_RIDE` 走 HumanBase slot 15 `0x5774C0`，在 `0x577661` `call 0x5765E0`（前一条 `0x57765A lea rcx,[rsi-0x120]`，字节 `48 8D 8E E0 FE FF FF E8` 已核对）。插件把这一处 call 重定向到 `RemoteRide`：先原样调 RideVehicle（按值传入的 shared_ptr 交给被调方释放），再对本机登记的任务乘员副本做同一个 `Quiet`。`RideVehicle` 的另一个直接调用者 `0x6326F1` 不动 | H |

玩家自己上车、玩家指挥 / 支援的载具乘员都不在任务乘员登记里，判据返回假，照原版出声。

## 4. 判据

唯一判据是 `mission_crew_support.cpp` 的部署登记：`MissionCrewRiding(vehicle, human)` = 某个已提交（`applied`）的部署的车辆是 `vehicle`（`ObjRef` 身份）且 `human` 是它登记的乘员之一。不按地址、不按 SGO 名。登记随部署建立，回滚（`DestroyMissionCrewPlan`）先把台词位放回再删士兵，任务重置时整表清空。
