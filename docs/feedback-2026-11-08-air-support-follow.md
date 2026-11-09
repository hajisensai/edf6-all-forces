# 2026-11-08 反馈：跟随版空中支援不跟随、炮艇机要盘旋一会才参战

维护者转贴（2026-11-08 10:12）：「全军出击叫出来的部分援军无法正常跟随，炮艇机要盘旋一会才能参战甚至在山地会卡在山坡的」

## 根因（读代码确认，未实机）

### A. 跟随版固定翼永远不跟随

- 入场：`ActivateSupportAircraft`（`src/jet_spawn.cpp`）给每架支援机下一个 guard 命令，目标是呼叫点；跟随版固定翼另设
  `escort`。guard 生效期间 `CommandAnchor`（现在在 `src/jet_internal.h`）只返回 `cmd.at`，`escort` 改的 `j.anchor`
  用不上。
- 解除：要等派遣器认定到达后发 `Order::none`。旧判据（`src/support_dispatch.cpp` 末尾）是「所有机体都在呼叫点水平 150 m
  以内」。固定翼的飞控绕巡逻圈飞（`Patrol`，`src/jet_flight.cpp`，半径 600–1500 m 再加每架 `patrolStep`），进圈会被推回
  圈上，所以这个条件永远不成立：guard 一直不解除，interceptor_F / strike_F / multirole_F / fighter_F / gunship_F 整场绕着
  呼叫点飞。直升机守点圈 120 m（`HeliGuardRadius`）、航母无目标时悬停锚点上方，能进 150 m，所以是「部分」不跟随。
- 第二条路径：入场途中任一单位死亡时，派遣器直接置 `delivered` 并 `continue`，同样不解除幸存机的 guard。直升机跟随版遇到
  这种情况也会永远守点。
- 同源的设计矛盾：`MapCommandMoving` 的注释早已写明「到达必须把巡逻圈算进去」，派遣器却另写了一个 150 m。

### B. 炮艇机要盘旋一会才参战

- guard 的入场段里 `MapCommandMoving` 为真时 `JetFrame` 不调 `PickTarget`，`GunshipFire` 因没有目标直接返回。
- 到达半径 = 巡逻圈 + 40，炮艇机约 640–1040 m；它选目标的距离（`TargetRange`，从锚点量）是 1500 m，带加农炮约 1795 m，
  炮弹 / 加农炮射程 1800 / 2500 m。它从场外飞进来，在 1800 m 处已经够得着目标，却要沿 `Patrol` 螺旋收敛到 640 m 圈才开始选目标。

## 修法

- 到达交给飞控自己判定、逐架解除：`SupportEntry`（`src/support_aircraft.h`）由喷气机 `SupportEntryOf`（`src/jet_internal.h`，
  `JetSupportEntry` 导出）和直升机 `HeliSupportEntry`（`src/heli.cpp`）回答「仍是激活时的 guard 且 `cmdMoving` 已被飞控清掉」。
  派遣器 `AirArrival`（`src/support_dispatch.cpp`）对每架单独判定、单独发 `Order::none`，全部交接后才置 `delivered`；
  模块这一帧不接命令时下一帧再问。玩家期间给了别的命令、出击（sortie）自己飞一趟、撤离 / 删除的，不再解除、视为已交接。
- 入场途中有损失：空中部署不再跳过解除，记一次日志和提示，幸存机照常逐架到达、解除；地面部署维持原样。
- 炮艇机（`Weapon::shells`）的到达半径改为 `max(巡逻圈 + 40, range)`，`range` 即它选目标用的 `TargetRange`；leash 仍是
  到达 + 100 m，保持迟滞。其他机种不变（它们「进圈前不换目标」是为防被新命令半路拖走）。

## 测试

- `support_dispatch_test`：喷气机 / 直升机跟随版两架，飞控未到达时即使在呼叫点正上方也不解除；第一架到达就单独解除、只解除一次；
  模块不接命令时下一帧重试；途中损失一架后幸存机到达才解除并交付；玩家自己的命令与撤离不被覆盖。
- `jet_command_test`：炮艇机在 `range` 内即到达、其他机种仍按巡逻圈；leash 迟滞；`SupportEntryOf` 的各状态；解除后 `CommandAnchor`
  回到跟随玩家的锚点。
- `tools/selftest.py` `air_support_arrival_is_the_flight_controls`：派遣器里不得再出现固定距离的空中到达判据。

## 待实机确认

- 「在山地会卡在山坡」：没有日志，代码上只能推测（炮艇机 maxG 2、滚转 0.3 rad/s，巡逻高度只取目标或锚点地面 + 350 m，
  `HoldOffGround` 不管前方上坡，撞坡后 `Sense` 沿障碍滑行），也不能排除说的是地面支援车辆。需要山地图那一局的
  `EDF6VehicleCrew.log`（Debug 开），grep `JET v=.* blocked`、`held off the ground`、`SUPPORT TRACE` 再定修法，本次不改。
- 联机时直升机和喷气机跟随的是本机全局 `player`，客机叫来的跟随版支援可能跟着房主走，是否也属于「不能正常跟随」需单独核实。
