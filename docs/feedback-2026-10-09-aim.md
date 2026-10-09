# 2026-10-09 实机反馈：自瞄 / 锁定 / 火控（aim 组）

分支 `fix/fb1009-aim`（基于 `origin/main` b89d26a）。证据来源：用户本机 `Mods/Plugins/EDF6VehicleCrew.log` 与
`EDF6AutoTurret.log`（只读），2026-10-09 11:24–11:41 测试场一局：玩家坐泰坦 `404_Tank`（v=…FB020）驾驶座、
游骑兵 403、两辆 603 防空车；用户 `EDF6AutoTurret.ini` 仍是旧的 `AimModeKey=0x5A`（Z），`SightZoomKey=0x5A`。
地址均为 EDF.dll TimeDateStamp `0x678CCB46` 的 RVA。**以下全部是离线分析与离线测试，没有在游戏里实测。**

## 1. 「v键自瞄按了没反应」

**现象核对**：`EDF6AutoTurret.log` 11:28:26–11:28:39 连续十几行 `PILOT v=…FB020 seat=0: auto-aim (key)` /
`lead circle (key)`——V 键被读到了，模式也在翻转，但什么都不发生。

**根因**：
- 坦克驾驶座的主炮从来不归自瞄：`autoturret/src/gunner.cpp:449`（b89d26a）`DriverFrame` 只
  `PublishAim(vehicle,false,…)`，`ownGun=false`，锁定只交给 AI 炮手；模式键在驾驶座上没有任何作用对象。
- 其它地面载具（505 布莱克、601、402 等）EDF6AutoTurret 根本不挂钩，没有自瞄可切换。
- 自瞄只有在 EDF6AutoTurret 自己挂钩的 603 / 403 / 404 上才存在，且是它自己写转向输入，和 EDF6VehicleCrew
  的炮塔镜头是两只手（`aimlink.h` V2 用 `Steers` 让来让去）。

**修法（同时是第 4 条的统一）**：新增 `common/edf/aimlink.h` V4。
- `EDF6AutoTurret_PlayerAimV4`（`autoturret/src/designate.cpp` `PlayerAim`）：EDF6VehicleCrew 的炮塔镜头每帧为它接管的
  驾驶座调用一次，**任何车型**。它跑同一个 `PilotFrame`（V / Q / 手柄键、模式、锁定规则），发布 HUD 读数（`ownGun=true`），
  并在 AUTO + 有锁定时返回「这门炮的炮弹与锁定目标相遇的点」（`aim::LeadSolve`，目标速度由新的 `TrackLock` 逐帧平滑）。
- `src/turretcam.cpp` `TurretCamFrame`：拿到这个点就用它代替屏幕中心作为炮塔目标，仍走同一个 `Wants`
  （`edf::BallisticArc` 低弧解算），镜头仍归玩家。
- `EDF6VehicleCrew_AimsTurretV4`（`src/turretaim.cpp`）告诉 EDF6AutoTurret「这个座位的炮塔由镜头转」：防空车的 `Steer`
  （`autoturret/src/plugin.cpp`）此时不再自己写转向输入，坦克驾驶座的 `DriverFrame` 不再发布「只给炮手」的读数。
- 帧时钟：不挂钩的车型由导出函数推进 EDF6AutoTurret 的帧计数（`SeeVehicleOnce`，挂钩车型不重复计数）。
- 兼容：任一侧是旧版（缺 V4 导出）时，各自保持 V2/V3 行为；只装 EDF6VehicleCrew 时仍然没有自瞄（与以前相同）。
- 键位：仍是 `EDF6AutoTurret.ini` 的 `AimModeKey`（默认 V）/ `LockKey`（Q）。旧 ini 的 Z 与瞄具冲突时实际用 V（已有逻辑）。

## 2. 「按了右键使用机枪以后，自瞄和瞄具等都无法使用了。需要按左键发射主炮才能恢复」

**现象核对**：`EDF6VehicleCrew.log` 11:27:38 起 `VHUD 1 GUN … aimed 1`（火控选中 900 发的 GUN），11:27:48 之前一直如此；
同期 `EDF6AutoTurret.log` 11:27:48 出现一次 `lead circle (key)`。

**根因**（只读 Root.cpk 核实：`VEHICLE404_BIGTANK.SGO` `vehicle_weapon_setting` 第 4 项 `['front_gun',0]`，
模型骨骼链 `front_gun → body → globalSRT → mdl`，不在炮塔 `cannon_main` 下）：
- 右键是泰坦驾驶座的副控（holder 3 = 车体 `front_gun` 机枪）。火控选择 `PayloadSightPicked` 跟随最后一次扳机边沿
  （`src/payload.cpp` `PayloadFrame`，这本身是对称的：左键回主炮）。
- 但下游把「炮塔是否解耦、炮塔按哪门炮转」也挂在这个选择上：`src/turretcam.cpp:732`（b89d26a）
  `weaponmount::OfWeapon(v,seat,Gun(...))`、`:272` / `:320` 的 `Gun()` 都是选中武器。选中车体机枪 → 两轴都不带动它 →
  `physicalOnly` → 镜头不再解耦、炮塔不再跟鼠标、鼠标圈消失。
- 瞄具：`src/sightzoom.cpp:63` 的能力也只看选中武器；机枪没有光学 → 退出瞄具、`SightZoomCanMount` 变假 →
  `EDF6VehicleCrew_SightBindingV1`（`src/turretaim.cpp:89`）不再保留 Z → `designate.cpp:92` `EffectiveModeBinding`
  把旧 ini 的 Z 交还给自瞄模式键。于是选中机枪时按 Z 不是开瞄具而是切自瞄模式（日志 11:27:48 那次 `lead circle (key)`）。
  只有左键开主炮把火控选回主炮，这一串才恢复。

**修法**：
- `src/turretcam.cpp` 新增 `TurretGun`：炮塔归座位的「两轴都带动的武器」——选中武器能被两轴带动就用它，否则用同座位第一门
  能被带动的活武器（泰坦的主炮），都没有才退回选中武器（固定炮仍是物理标线，行为不变）。`Wants`、`Readout`、
  解耦判定和自瞄询问都用它；高视角观察的落点（`ShotFocus`）仍跟选中武器。
- `src/sightzoom.cpp` `SeatCapability`：选中武器自己没有光学时，用同座位第一门有光学的活武器的瞄具。于是瞄具和
  它保留的 Z 不再随最后一次扳机来回变，Z 永远是瞄具，V 永远是自瞄。
- 火控选择本身（`payload.cpp` 的扳机边沿 / R 键）不改：HUD 弹着点仍跟当前武器（机枪的物理落点照画）。
- 「机枪也能自瞄」：能被炮塔带动的机枪（选中它时）就按它的弹道转炮塔；泰坦的车体机枪物理上不随炮塔转，
  自瞄只能转炮塔（主炮），机枪照它自己的物理标线打——这是模型决定的，没有伪造。

## 3. 「在载具上的q……标记的东西和实际鼠标指向不符」

用户说的「原版的 q」在本仓库与 EDF.dll 里找不到原版载具 Q 功能；日志里载具内的 Q 全是 EDF6AutoTurret 的锁定
（`LOCK … (camera view)`），所以按它处理。镜头射线本身没有问题：`ViewRayV1` 来自 HUD 实际使用的 view-projection
（`src/hud.cpp:3095 CameraRay`），与炮塔镜头替换后的画面、鼠标圈是同一个矩阵，不需要逆向原版准星。

**根因**（两处，都在 `autoturret/src/designate.cpp` `Cycle`）：
- 选目标规则 `aimmath.h:24` `NextPick`：每次按键都从「旧锁定在当前排序里的位置」往外数下一个。准星移到别的敌人上再按，
  锁到的是旧锁定后面那个，而不是准星上的（日志 11:27:34–35 同一秒内两次锁定在两个目标间来回跳）。
- 距离：`designate.cpp:155` 用的是到镜头眼点的距离（泰坦镜头在车后 37 m），而驾驶座锁定范围是炮手射程
  `gunner.cpp:446 cfg.gunnerRange`（300 m），主炮能打 2400 m。准星对着 300 m 外的敌人按 Q →
  `no enemy in sight (0 in the 20 deg cone)`（日志 11:27:32 两次）或锁到更近的别的敌人。

**修法**：`aim::LockPick`：锁准星最近的那个；只有它已经是锁定，或准星没动连续按时，才往外轮换。距离改为从车体算（与
`Check` 的丢失判定一致）；驾驶座锁定范围取主炮射程（不小于 `GunnerRange`），V4 导出取镜头所转那门炮的射程。

## 4. 「防空车的自瞄好像是单独写的，统一一下代码和操作体验」

**现状**：防空车的玩家自瞄是 EDF6AutoTurret 的 `Steer` 自己写转向输入（只在 603 上），坦克驾驶座没有自瞄，普通坦克只有
EDF6VehicleCrew 的「炮塔跟屏幕中心」——三套。按键 / 锁定 / 提前量的实现其实只有 `designate.cpp` 一份，分裂在「谁转炮塔、
在哪些车上跑」。

**修法**：见第 1 条的 V4：按键、模式、锁定规则、提示（HUD 读数）、提前量（`aim::LeadSolve` → `edf::BallisticArc`）都是
`designate.cpp` 一份，所有驾驶座都由炮塔镜头这一只手转炮塔。防空车与普通车的差别收进数据表
`common/edf/weapon.h` `kGunRoles`（标记 → 目标偏好 / 高抛），`autoturret/src/plugin.cpp ReadShot` 与 `src/payload.cpp`
`NpcPayloadSelect` 都查这张表；引信（只对 GrenadeBullet01 弹）照旧。

**没统一的部分（如实说明）**：坦克炮手座（403/404 的 1、2 号座）上的玩家仍走 EDF6AutoTurret 自己的炮手辅助（V1 规则：
自己挑目标、推摇杆拖炮），因为炮塔镜头只接管 0 号座；按键与锁定规则是同一份，但转炮的手不同。NPC 炮手、防空车引信不变。

## 测试（全部离线）

- 新增 `tests/player_aim_test.cpp`（生产 `designate.cpp` + 导出函数）：V 经 V4 导出切换模式且对称；Q 锁准星上的敌人、
  视线移到别的敌人再按锁那个、原地连按轮换；距离从车体算；AUTO + 锁定对横穿目标给出提前量点；预瞄圈 / 观察视角不转；
  防空车标记武器与无标记坦克炮对同一目标给出相同答案；空座位无答案。79 项。
- `tools/turret_cam_runtime_check.cpp`（生产 `turretcam.cpp`）：车体机枪被选中时炮塔仍按主炮、镜头仍解耦、询问自瞄用主炮；
  切回主炮对称；可动选中武器按自己的炮口；只有固定炮时仍是物理标线；有锁定时炮塔目标换成提前量点、无锁定回到屏幕中心、
  高视角仍询问但不转。
- `tests/sightzoom_test.cpp`（生产 `sightzoom.cpp`）：选中无光学的车体机枪时瞄具与 Z 保留不丢，来回切换不丢倍率，失效 holder 不借用。
- `tools/turret_lead_check.cpp`：`LockPick` 规则与 `kGunRoles` 表。`tools/selftest.py turret_aim_wired`：V4 导出名与接线。
- 负对照（逐条恢复旧代码重编再跑）：恢复 `NextPick` → player_aim / turret_lead 失败；恢复「到眼点的距离」→ player_aim 3 项失败；
  `TurretGun` 不回退 → turret_cam_runtime 3 项失败；去掉瞄具回退 → sightzoom 失败；去掉提前量覆盖 → turret_cam_runtime 失败；
  去掉 AUTO/锁定/观察门 → player_aim 3 项失败。恢复后全部通过。
- 全量：插件与测试 `/W4 /WX` 构建通过，`offline_checks` 构建通过；`ctest` 180 项中 179 通过，`support_soldier_native`
  在 ctest 下失败（Python ctypes 直接执行 EDF.dll 函数时访问冲突，与本改动无关：脚本不加载任何仓库代码，直接用 miniconda
  python 连跑 3 次均通过，第一次全量 ctest 也通过）。selftest 129/132，3 项失败与基线相同（缺 `pefile`）。

## 没有在游戏里实测的部分

- V4 全链路：泰坦 / 普通坦克驾驶座 AUTO + Q 锁定后炮塔是否稳定转向提前量点、HUD 两行是否出现、炮手是否仍优先打锁定。
- 车体机枪选中后：炮塔镜头保持解耦、瞄具（Z）用主炮光学、Z 不再切自瞄模式。
- 锁定：大型敌人多个锁定点时「准星最近」的手感；主炮 2400 m 射程下的锁定是否过远。
- 防空车由镜头转向后与原先 `Steer` 自转相比的跟踪表现（转速限制都是游戏轴步进本身的，但前馈来源不同）。
- 帧时钟：只有不挂钩车型的任务里 EDF6AutoTurret 的帧计数由导出推进，未在游戏里观察。
- 联机：未测试。
