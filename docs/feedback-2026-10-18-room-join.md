# 2026-10-18：联机时进不去房间（EDF4.1 任务包房）

用户原话：

> 进不去，联机的时候，修复一下

> 感觉是因为4.1或者5代的任务包的原因 前者，过了一下弹

> 全部根本性修复
> 你看看日志站的日志

截图：大厅选中房主 `STEAM:Faze.Kyousuke` 的 8 人 MultiSlot 房（任务「3.有翼的女武神」=
EDF4.1 任务包 `EDF41/M191`，HARD，1/8），点加入后过一会儿弹原版「无法参加房间。」。

## 证据（测试站 #18，客人日志）

- `EDF6VehicleCrew.log`：`ROOM content check ... allowed=1`、`ROOM join allowed: route=eos`，
  任务包拥有判断与房间隔离都放行。
- `EDF6Coop.log`（10-17 构建的 Coop main，含 a488387）：Steam / EOS 都进房成功，随后
  `DIRECT room host ... advertises no direct-link address` →
  `ROOM NOT entering room ...: its host's member slots did not come (10 s, ...)` →
  `LOBBY join finished: code 2`。第二次加入相同。

## 根因与修复

- 直接原因在 EDF6Coop：装了全军出击的客人加入时必须等房主的位次表 / 世界状态，而它们只经直连
  发送，默认（`Mode=off`）的房主从不发送。修复：hajisensai/edf-coop-stable#36（经 EOS P2P 送达）。
- 任务包本身不是这次的直接原因，但查出的契约缺陷一并修复：
  - 固定编号、占位行、房间发布 `AF_PACK`：本 PR（#193）。
  - 8 人房跑 EDF5 / 4.1 脚本只建 4 个玩家：#192。
