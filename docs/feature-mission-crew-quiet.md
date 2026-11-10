# 任务 AI 载具乘员坐车时不说话、不标记、不出上车声、雷达上不单独出点

需求：`docs/feedback-2026-11-08-mission-crew-quiet.md`（测试站 #41，方案 A）。逆向：`docs/mission-crew-quiet-re.md`。

## 判据

`mission_crew_support.cpp` 已有的部署登记：每个已提交的部署记着车辆与它的乘员（`ObjRef`）。`MissionCrewRiding(vehicle, human)` 只对「本部署的乘员坐在本部署的车里」为真。它经 `SetNpcMissionCrew` 交给 `npcai.cpp`，后者在三个地方问：

1. 上车之后（`Board`、`NpcRestoreMissionSeat`、车内换座 `NpcMoveSeat`、联机副本的 `RemoteRide`）：为真就 `seatevt::Quiet`，这次上车没有声音。
2. 合流标记 `JoinMarkHook`：为真就不画（不论 `VanillaRecruit`）。
3. 不需要：台词位由 `mission_crew_support.cpp` 自己每帧管（下面）。

## 台词位的生命周期（`src/mission_crew_quiet.h` `crewquiet::Next`）

每帧（`SupportDispatchTick` → `MissionCrewQuietTick`，每台机器、包括对方部署在本机的副本）对每个部署的每名乘员走一步：

| 状态 | 动作 |
|---|---|
| 在车上（活着、`+0x1548` 是本部署的活车、插件开着）且 bit 18 置着 | 清掉，记「欠一次恢复」 |
| 在车上且 bit 已是 0、没有欠账 | 不写（别人关的，不归插件） |
| 不在车上（下车 / 死亡 / 车毁 / 插件关）且有欠账 | 放回 bit 18，清欠账 |
| 对象已不在 | 忘掉，不写 |
| 部署回滚 `DestroyMissionCrewPlan` | 先放回所有欠账，再删士兵 |
| 任务重置 `ResetMissionCrewSupport` | 整表清空、不写（场景归任务拆除） |

上车声与合流标记都是逐帧 / 逐次按判据算的，不留状态，下车自然恢复。

## 雷达点（2026-11-10，`docs/feedback-2026-11-10-crew-radar-dot.md`）

`MissionCrewQuietTick` 每帧把「坐在本部署自己的车里」的乘员（与台词位同一判据，`Deployment::aboard`）整份发布给 `src/mission_crew_radar.cpp`；原版雷达的友方一行（`pvp_native.cpp` `RadarFriends` → `0x82B220`）填完后按同一集合顺序删掉这些乘员的行，只剩载具自己的点。下车 / 死亡 / 车毁 / 插件关的下一帧就不在集合里，步兵点回来；回滚时先放回、重发集合，再删士兵，任务重置清空。逆向见 `docs/mission-crew-quiet-re.md` §2.1。

## 联机

四样都是本地表现：台词事件每台机器按自己副本的 `+0x380` 判闸门，合流标记是本机 HUD，上车声由本机车辆的座位变化位播放，雷达是本机 HUD。每台机器只改自己的副本，不加网络消息，不改房间协议。对方装旧版插件时它那边仍会听到 / 看到（只影响它自己的表现，不影响同步）。

## 测试

`tests/mission_crew_radar_test.cpp`（ctest `mission_crew_radar`）：合成的 MSVC `std::set` 与原生行向量上跑生产过滤：中序遍历、首 / 中 / 尾乘员删行且其余顺序不变、节点数与行数不符不动、全隐藏、空集合、清空后恢复、经队伍管理器取集合、`0x82B220` 字节不符不装；`tests/mission_crew_radar_native_audit.py`（ctest `mission_crew_radar_native`，需 `EDF6_NATIVE_DLL`）在真实 PE 上核对整段遍历字节、布局常量与 4 个调用点。

`tests/mission_crew_support_test.cpp`（ctest `mission_crew_support`）：雷达隐藏集合随坐车 / 下车 / 死亡 / 车毁 / 插件关 / 回滚 / 任务重置 / 对方副本 / 未提交部署同步变化；纯步骤 6 条；登记 / 解除对称：坐车清位、下车放回、再上车再清、乘员死亡放回、车毁放回、插件关放回、他人先关的位不放回、回滚先放回再删、任务重置不写、对方部署的副本同样处理、未提交的部署不算；判据：本车本乘员为真，陌生士兵 / 别的车为假，判据已登记到 npcai。

## 需实机确认

- 4.1 M190：任务坦克里坐着乘员时右上雷达只有坦克一个点；乘员下车 / 车毁幸存后步兵点出现；玩家自己叫的支援 / 指挥载具乘员照原样出点。

- 4.1 M190：增援坦克的乘员上车无声；车里的游骑兵不说话、字幕不出图标；`VanillaRecruit=1` 时车顶不出合流标记。
- 乘员下车（车毁后幸存）后恢复说话。
- 双机：客机上同样安静（`RemoteRide` 的挂钩日志 `NPCAI peer ride message not as read` 不应出现）。
