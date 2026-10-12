# 2026-10-12 反馈：EMC、巴拉姆模型、潜水母舰召唤

## 1. 潜水母舰在水里也没法召唤（测试站 #12）

**原话**：「潜水母舰在水里也没法召唤」（n0xe，build fb4c60c）。

**根因**：#88（`c21f499`）把呼叫武器统一改走支援规划器：`airstrike.cpp RadioStartHook` → `SupportCallAt` → `support_dispatch.cpp Plan`，
`AirCatalog` 分支里 `AircraftSpecOf` → `SupportAircraftSpec` 对 `Brings::sub` 恒返回 false（它不带飞机），于是一律
`Refuse("no verified entry for this unit")`。原来 `LaunchCall` 里的潜舰分支（`SubLaunch`）随之被删掉，`SubLaunch` 从此没有调用点。
#95 在 `docs/support-deployment.md` 里写成「目录明确拒绝」，理由是潜舰没有入场方式、不能在目的地旁凭空创建。

**证据**：测试日志与本机日志共 5 行 `SUPPORT plan catalog=16 refused: no verified entry for this unit`（catalog 16 = `EDF6VC_CALL_SUB`）；
同一份日志里 `SUB preload carrier=1`（资源在、已预载），没有任何 `SUB launch` 行。

**修复**：给潜舰一个真实的入场方式——**从水下上浮**，不经规划器：
- `support_dispatch.cpp SupportSubCallAt`：信号弹往前（背离呼叫者）1000 / 600 / 300 / 0 米依次找第一个不是陆地的点，`SubLaunch`；全是陆地就提示「目标附近没有水域」。
- `subcarrier.cpp SubLaunch`：水上生成时指挥塔顶在水面下 `kSubmerged` 20 米、船底（碰撞箱）不低于海床，`surfacing` 期间 `Drive` 以 `kSurfaceClimb` 12 m/s 升到浮起高度（`s.floor`），到位前 `FireStep` 不开火、`Bay` 不放无人机。
- `airstrike.cpp RadioStartHook`：潜舰呼叫在每台机器的重放里各自上浮一艘（归呼叫者，`SetSpawnOwner(CopyOwnerOfCaller)` 与 `SubLaunch` 的 `NoteLocalCopy`，与 #88 之前一致），不再提交支援请求，也不让原版轰炸机顶替。
- M 地图：单机走同一路径（30 秒冷却同其它支援）；联机拒绝并提示用空袭兵的呼叫武器（地图请求只在本机生成，别的玩家看不到）。

**测试**：`tests/support_dispatch_test.cpp` 新增：陆地拒绝且不生成；水上立即上浮、不建规划对象；远端重放各自上浮；联机地图拒绝、不发主机请求。
`support_dispatch_test` 226 项、`support_infantry_route_test`、`support_vehicle_route_test`、`call_pick_test` 通过；全量 ctest 225/225（native 项跳过）。

**未验证**：未真机验证。上浮外观（深水时约 17 秒，浅水时只升几米）、水下生成时碰撞箱与海床的关系、浅水图上是否看起来还是「凭空出现」都要实机看。

## 2. 巴拉姆模型依然是巴尔加（测试站 #12）

**原话**：「巴拉姆模型依然是巴尔加」。

**根因**：`pylib/edf41calls.py VEHICLES` 把 4.1 的巴尔姆 `vehicle501_fortressrobo.sgo` 映射成 EDF6 的 `v515_retrobalam.sgo`。
EDF6 的「Retro Balam」就是巴尔加（`docs/feedback-2026-10-09-loadcamp.md` 的表：`V515_RETROBALAM*` = 巴尔加各型）。而 4.1 的巴尔姆本体早在 P4
（`tools/make_edf41_objects.py`）就整套移植进了 `edf41port/objects`（SGO / RAB / CAS / SHKT，安装器按 4.1 原名写进 `Mods/OBJECT`），
呼叫却没有指向它。4 把呼叫（`EDF6VC_E41_FORTRESSROBO01`、`WEAPON720` B 型、`WEAPON738` 激光堡垒、`WEAPON770` F 型）都受影响。

**修复**：`VEHICLES` 改指 4.1 本体；`make_edf41_weapons.py CallTemplates.FAMILY` 让它借巴尔加的呼叫作模板（同类 `Vehicle501_FortressRobo`，
4.1 B 型和 EDF6 巴尔加呼叫的炮塔是同一把 `v_501_baram_Cannon01`，设定块排法一致）；`call_entry` 的缺资源检查认得 P4 移植的对象（`ported_object`）。
重跑 `tools/make_edf41_weapons.py`：`edf41port/calls.json` 只有这 4 条的载具路径变了（diff 8 行），`weapons.json` 不变。

**未验证**：未真机验证。移植的 4.1 SGO 比 EDF6 巴尔加的 SGO 少两个键：`booster_param_list`、`se_damage_scaling`（P4 原样搬运，任务里放的那台同样缺）；
EDF6 的 `Vehicle501_FortressRobo` 读不到它们时是否安全没有逆向过。另外安装器若因别的模组占了同名文件而跳过 4.1 对象（`edf41_objects.build` 会打印「跳过」），这几把呼叫会指向不存在的 SGO。

## 3. 「emc没换成我们新作的」

**原话**：「emc没换成我们新作的」。

**查到的事实**：蓄力光束本身在用户和测试员的游戏里都生效（日志里有 `EMC v=… fired` / `beam over`，四个 `EDF6VC_EMC_*.SGO` 都已安装）。
仓库和所有分支里没有「新做的 EMC 模型」。所以这句话的确切所指**我没能定下来**，可能是下面两种之一：

(a) **终点大爆炸几乎从不出现**（已修）：用户 2026-10-06 的设定是「光束贯穿……同时要有几百米范围的爆炸」，README 写的是「在它照到的地面（或最后一栋建筑）上爆出」。
代码（`emc.cpp End`）只看**最后一帧**光束终点是不是地面：光束穿透建筑不停，所以穿过一整片楼射向天空时从不爆；开火时扫到地面、最后一帧没扫到也不爆。
实测日志：`fired … its end 208 m (the ground)` → `beam over: … nothing at its end: no blast`；`beam over: 35 building charge(s) fired … nothing at its end: no blast`。
用户和测试员日志里每一发都是 `no blast`。修复：`emc_plan.h BlastAt`（地面终点，否则线上最后一栋建筑），`emc.cpp NoteBlast` 每帧记下最后一个，`End` 炸在那里。
`tools/emc_check.cpp` 新增：终点是地面时炸在地面；穿过整片楼射向天空时炸在最后一栋（楼塌后仍保留）；全程空中不炸。

(b) **NPC 开的 EMC 仍是原版连射**（未改）：README 明写「NPC 开的 EMC 不变」，测试站 #12 同时报了「派 NPC 驾驶电磁炮时闪退」——NPC 驾驶 EMC 属于 NPC 开火主题，留给那边。
如果用户指的是这一条，需要另做：让 NPC 扣扳机也走蓄力光束。

**未验证**：(a) 未真机验证（爆炸位置、建筑倒塌后的效果）。
