# 2026-10-30 测试站 #27：联机操控怪物

## 原话（tester-20qs，0.8.0-build.eba4372，附日志 logs-20261027-225634）

> 联机操控怪物 我人没操控 然后还导致我打不了怪物方 并且以泰坦位置为例子 在后方我发现了空气里飞出个镇魂炮 而泰坦在前方

## 日志里的事实（只有客机一侧：本机是客机，房主 peer 1，2 人房，EDF6Coop 2.7.0，任务 EDF6/RM034A）

- **接管从没成功**：22:41:05–22:41:16 有 10 次 `ENEMYCTRL NET result=1`（Pending，只有本机发出请求才会这样）紧接 `result=6`
  （Unauthorized，往返 45–220 ms：房主 `HostRequest` 的 `Authenticate` 拒绝）；全程 `active=0`，没有 `ENEMYCONTROL begin`。
  「我人没操控」= 没接管成功；enemy control 没改过任何阵营（无 lease、PvP 关）。
- **同一局里别的通道也认不出对面玩家**：`PLAYERVEH peer 1: a sync for a player that is not its sender's: dropped`（22:40:10 ×5）、
  `BARRAGE NET peer 1: … names a caller that is not its sender's player: dropped`（22:41:10）。这两条走 `ResolveMarkIdentities`（不做
  同 ID 多对象检查），仍然失败；而 `SUPPORT world participants sealed: actors=2 peers=2`。所以失败在「玩家对象 → 原生 PUID 文本 →
  与传输层认证的发送者比对 / 参与者」这条共用链上，双向都坏，不是 enemy control 独有。客机日志判断不了是哪一步。
  连接：经直连（DN DIRECT）入房，EOS 链路本局 80 次 INTERRUPTED，直连 / 中继反复切换。
- **打不了怪**：EDF6Coop.log 本局 16 次 `EXCEPTION … EDF6Coop.dll+A3850 reading FFFFFFFFFFFFFFFF`，在 Coop 自己钩的命中结算
  `EDF+54AA50` 里，被 SEH 吞掉的那一发命中丢失；Coop 同时记 `refused: not the owner 937, duplicate 1175, attacker over the event rate 910`。
  问题在 EDF6Coop（`multislot/src/hitauthgame.cpp`），本仓库不改，证据转交。
- **镇魂炮从泰坦后方飞出**：泰坦是任务原版 `404_Tank`，NPC 乘员只在房主侧（`mission_crew.cpp`）；客机上是原版远端副本，插件对它
  没有位姿同步（本局 `netprobe` 位姿行只有 6 台直升机）。原版只在房主开火、子弹按房主的炮口位置复制过来，客机车体靠原版软修正追
  （`docs/online-re.md` §4.3–§5 已记：插件写给 NPC 驾驶的输入不被复制）。与 enemy control 无关。

## 本次改动

- 只加证据，不改行为：
  - `SupportCommandRequesterMatches`（所有「替玩家说话」的通道共用的发送者检查）失败时记是哪一步：原生 PUID 无文本 / 玩家的 PUID 与
    发送者不同（两者前 12 字符）/ 不在本局封存的参与者里。
  - 房主处理接管请求时，`Requester` 解析不到唯一的活玩家（找不到、不是士兵、同 ID 两个对象）记一行。
- 根因要等下一份**房主和客机两边**的日志（带这两行）再定；泰坦错位是 NPC 驾驶地面载具缺权威位姿同步，另立任务。
