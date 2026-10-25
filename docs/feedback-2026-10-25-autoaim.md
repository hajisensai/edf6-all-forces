# 2026-10-25 反馈：自动瞄准默认开、没有提示、载具上按 Q 提示「对准敌人才能」

## 用户原话

> 怎么自动开启自动瞄准了，而且也没啥提示欸。不应该有对应的hud吗，锁定上的敌人。而且载具上按q可能是自动瞄准后按q会提示对准敌人才能。但是我明明对准了。自动瞄准不要开启，开启以后应该有被瞄准敌人的高亮圈之类的？

**以下全部是离线分析与离线测试，没有在游戏里实测。** 同日的「两套自动瞄准」「M163 第一人称被拽回」见 `docs/feedback-2026-10-25-tank.md`。

## 1. 为什么自动瞄准「自己开了」

- 不是 ini 读错、状态未初始化或联机同步：EDF6AutoTurret 的自瞄从来就是默认开。10-25 之前 `AimMode=0` 的含义是「自动瞄准」，
  只能切到「预瞄圈」；当天的 d4d2d0d5（「两套自动瞄准删掉一个」）删掉预瞄圈模式后，自瞄**一直开着、也没有任何关闭的办法**。
- 默认开在玩家身上的表现：
  - 没有 EDF6VehicleCrew 炮塔镜头的炮位（`DecoupledTurretCam=0`、炮手座的 `GunnerAssist=1`、旧版 VehicleCrew）：`PlayerGunRule`
    的 V1 分支每帧把炮转向插件自己挑的目标，鼠标的小幅输入达不到 `DragDeadzone`，炮被拽走；
  - 有炮塔镜头时不锁定不转炮，但 Q 被炮塔锁定占用（见 3），HUD 一直写「自动瞄准」，读起来就是「自动开了」。
- 「没啥提示」：d4d2d0d5 之后 HUD 只剩一行 `自动瞄准  [Q] 锁定（长按：清除）`，没有开 / 关状态、没有切换键、没锁定时目标上什么都不画。

## 2. 改法：默认关闭、J 切换、明确提示、目标高亮圈

- EDF6AutoTurret（`autoturret/src/designate.cpp`）：ini 新增 `AutoAim=0`（每个任务开始时的状态：EDF6VehicleCrew 的 `MissionStart` 调 `EDF6AutoTurret_MissionResetV1`，回到 ini 值并解除锁定；只装 AutoTurret 时保持到退出游戏；改 ini 立即生效）、`AutoAimKey=0x4A`（J）、
  `AutoAimButton=0`（手柄不设：L3 是 VehicleCrew 的自由视角；HUD 会提示去 ini 设）。
  - **关闭**：`PlayerControlRule` 第一条就返回「不转、不拖」（炮塔镜头有无、第一人称、炮手座都一样），`Designated` 无锁定，
    锁定键不读（按住 Q 跨过切换也不算一次按键），readout 的 `mode=manual`、不带目标。防空炮的定时引信仍按跟踪距离空爆（只影响引信，
    不转炮）。
  - **开启**：与之前相同（Q 锁定、长按清除、锁定后自瞄只打它）；另外每帧算出「此刻按 Q 会锁定的那个」（与锁定同一套拾取：
    `InSight` = 视线 `LockCone` 内、锁定距离内、看得见，取第一个），没锁定时给 HUD 当高亮目标。插件自己正在转炮对准的目标优先于它；
    只为引信跟踪、并没有转炮的目标不再当作目标显示（`plugin.cpp Steer` / `gunner.cpp SteerSeat` 只在 `rule.steer` 时交出目标）。
- 键位：不用 V。V 是 EDF6VehicleCrew 的 `FirstPersonKey`，`FirstPersonVehicle=1`（默认）时任何载具座位松开 V 都切第一人称
  （`firstperson.cpp Toggle`），炮塔座位上按 V 会同时切第一人称和自动瞄准（主会话审查指出；`crew.h` / ini 里「只在步行时生效」的旧描述
  一并更正）。按 #264 的键位盘点（`docs/plan-2026-10-25-keybinds.md` 占用表），J 在步行、所有载具、炮手座都没有占用，只有地图指挥里的
  「集火」用 J，而地图开着时插件不读自己的键（`MapHoldsKeys`）。
- 链路（`common/edf/aimlink.h`）：`Mode` 增加 `manual=2`（自动瞄准关）；`TurretReadoutV1` 的 `modeKey` / `modeButton` 重新用于切换键
  （它们原本就是模式切换键），布局不变；`PlayerAimV4.mode` 关闭时是 `manual`，从不 `steer`。
- EDF6VehicleCrew HUD（`src/hud.cpp TurretAimMarks`）：
  - 下方一行：关 `自动瞄准：关   [V] 开启`（白）；开 `自动瞄准：开   [V] 关闭   [Q] 锁定（长按：清除）`（绿，锁定后红、写「下一个」）；
    只有一个「自动瞄准」，不再出现上下两行「开」「关」。
  - 切换后屏幕中间大字「自动瞄准 已开启 / 已关闭」1.5 秒（`AutoAimBanner`，`hudcue::Changed`）：只要有炮塔 readout 就画，瞄具放大、
    高视角、炮手瞄具、第一人称这些不画下方那一行的视角里也显示；没有 readout 时忘掉旧值，上车不会误报。
  - 开启且未锁定：目标上画绿色圆圈 + 四根短刻线（`TargetRing`）；锁定后照旧是黄色收缩方框 → 红色菱形。

## 3. 「载具上按 Q 明明对准了却提示对准敌人才能」

- 根因：同一个 Q 在炮塔座位上**同时**触发两套拾取，规则不同：
  - EDF6AutoTurret 的锁定（`designate.cpp Cycle`）：VehicleCrew 的镜头视线、`LockCone=20°`、锁定距离、地图射线判遮挡；松开时触发。
  - EDF6VehicleCrew 的 Q 标记（`npcai.cpp ToggleMark`）：屏幕中心射线、`NpcMarkCone=8°`、量到敌人锁定点、不判遮挡；按下时触发；
    最近的敌人在 8°～24°（`kPointClear` 倍）之间时什么都不做，只提示 `npcPingNearEnemy`「对准敌人才能标记」。
  - 所以敌人离准星 8°～20°（大型敌人瞄身体、锁定点在别处时很常见），锁定框已经框住它，标记却说「对准敌人才能标记」。拾取射线、座位相机
    都没错，是同一个键两次拾取、两套阈值的契约问题。
- 修法（炮塔座位只有一套拾取，自动瞄准开或关都一样）：EDF6AutoTurret 导出 `EDF6AutoTurret_TurretLockV1`，玩家在它的炮位上就为真，
  给出「此刻按锁定键会锁定的那个」（`InSight`：视线 `LockCone` 内、锁定距离内、看得见的第一个，每帧都算）、自动瞄准状态、是否键鼠、
  锁定键与锁定目标。VehicleCrew 的 `NpcMarkFrame` 在炮塔座位上不再用 8°、不判遮挡的旧拾取（`ToggleMark` 只留给步行和其它座位），
  改走 `TurretMark`（`qmark_ray.h TurretMarkPress`）：
  - 自动瞄准关：Q 标记这个敌人，再按一次同一个取消；视线内没有敌人时照旧标地面点、派地图里选中的小队（`SendToPoint`）；
  - 自动瞄准开且用键鼠（Q 就是锁定键）：按下时有敌人就交给锁定，标记跟随锁定（`FollowTurretLock`，锁定换到新敌人时标记它、提示音一次，
    锁定解除标记保留到敌人死亡）；没有敌人时同样标地面点；
  - 用手柄时锁定是 X：键盘 Q 仍是标记（同一套拾取），不会被锁定吞掉。
  - 炮塔座位上不会再出现「对准敌人才能标记」。

## 离线验证

- `autoturret_binding_test`：默认关（readout `manual`、写 J、不带目标；`TurretLockV1` 为真但 `autoAim` 假；有锁定 / 无炮塔镜头都不转不拖）；
  V、Z 不切换；按 J 只切一次；关闭时按住的 Q 切换后不算按键；开启后 readout / `TurretLockV1`；再按 J 关闭后 V1 不再拽炮；`MissionResetV1`
  回到 ini 值（0 关、1 开）并忘掉座位；原有的观察 / 瞄具 / 第一人称用例照旧（开启状态）。
- `player_aim_test`：readout 写 J；V 不切换；J 关闭 → 锁定解除、不转炮、Q 不锁；再按 J 开启 → Q 锁定后照常转向。`autoturret_payload_aim_test`
  与其余炮塔用例在夹具里显式开启自动瞄准；`plugin_pair_load` 导出清单加 `EDF6AutoTurret_TurretLockV1`。
- `qmark_test`：`KeyIsTurretLock` / `TurretMarkPress` / `MarkLock` 的各分支。
- `npc_core_check`（含 `command_native_dispatch_test`）：炮塔座位、自动瞄准关：旧拾取能选中的敌人被墙挡住（锁定规则看不到）→ 标地面点、派小队；
  锁定规则的敌人 → 标记，再按 → 取消；自动瞄准开：按 Q 不自己拾取、锁定后标记跟随且只响一次、锁定解除标记保留；手柄：键盘 Q 仍是标记。
- `hud_view TurretLayoutApart`：开 / 关各只有一行写自动瞄准，关闭时写切换键（J）、不提供锁定键；横幅单独一行；与原版块 / RWR 不重叠。
- `selftest`：`turret_aim_wired`（新 ini 键、默认 0、J、关闭先于规则、导出名一致、炮塔座位标记走 `TurretMark`、任务开始重置）、
  `hud_switch_cues_wired`（横幅在 overlay 条件之外）、`npc_ai_wired`。

## 未实机验证

- J 在各炮位（防空车、玻尔斯、泰坦炮手座 / 驾驶座、M163 / BMPT）上是否都能切换（第一人称 / 瞄具里也读得到），横幅与高亮圈的位置和观感；
- 高亮圈每帧做的视线遮挡测试（最多一条地图射线 / 每个遮挡候选）对帧率的影响；
- Q 跟随锁定后 NPC 是否集中打锁定目标；联机时队友看到的标记（走原有 Q 标记同步）。
- 按键整体重设计在 #264（keybinds）：J 按它的占用表选；#264 若引入「占用表让位」机制，`AutoAimKey` 需要登记进去。
