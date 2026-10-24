# 2026-10-25 反馈：坦克炮管整圈转、两套自动瞄准、俯瞰看不到落点、M163 / BMPT 第一人称被拽回

用户原话见 `docs/feedback-2026-10-25-batch.md`（另一个任务的整批记录）。本文件只记坦克相关的四条。
**全部是离线分析与离线测试，没有在游戏里实测。**地址均为 EDF.dll TimeDateStamp `0x678CCB46` 的 RVA。

## 1. 「坦克的炮管会360度动」

没有日志和录像，下面两条是沿代码路径能严格成立、且会表现为整圈转的缺陷；用户看到的是哪一条（或两条都有）未实机确认。
委托方怀疑的「±π 包角错」「整圈阈值不一致」已排除：整圈轴的差值都包了角（`turretcam.h SteerAxes`、`stab.h Diff/Keep`、
`autoturret plugin.cpp AxisInput`、`gunner.cpp` 的 delta/off），不限位的轴原版写的是常数 ±π（`docs/artillery-re.md`），各处判定一致。

### 1a. 炮塔镜头的瞄准点落在炮塔背后（玩家的车，`DecoupledTurretCam=1` 默认）

- 链路：`turretcam.cpp Camera` 发布的「正常镜头请求」眼位（`PublishNormalRay`）是**原生镜头碰撞 `0xF6760` 之前**的位置
  （`docs/feedback-2026-10-19-camera-ownership.md` 已记录这一点）；`AimPoint` 从这个眼位沿视线 `MapRay` 取第一个地图命中作炮塔目标，
  `Wants` 只拒绝 `x<=1e-3`，`SteerAxes` 按最短路以原版最高转速转过去。
- 车尾贴墙、车停坡底镜头在坡上、镜头扫过路边建筑时，「眼位 → 注视点 O」这一段先打到障碍物：命中点在炮塔背后，想要的偏航 =
  镜头偏航 + 180°，炮塔掉头；镜头再动一点，命中点沿墙滑、离转轴只有几米，想要的偏航大幅摆动，炮塔跟着整圈甩。
- 修法：`tcam::ViewRayStart`（`src/turretcam.h`）：视线上能当目标的只有炮塔之前的部分，射线从 O（`|look-eye|`）或视线上离炮塔转轴
  最近的点（`TurretPivot`）中更远的一个开始找；没有命中仍取视线 800 m 处。玩家看到的画面本来就是碰撞后的（眼睛在障碍物前），
  那面墙不在画面里。
- 离线：`tools/turret_cam_runtime_check.cpp`：墙在眼位与炮塔之间 → 瞄准点在车前地面、偏航想要值 ≈ 镜头方向（旧路径两条都失败，已用
  起点置 0 的变异验证）；墙在车前 50 m → 照旧瞄墙。

### 1b. AutoTurret 侧炮的轴方向符号被主炮塔的转动学反（泰坦 / 游骑兵坦克的炮手座）

- `autoturret/src/gunner.cpp ReadAim`：炮管几何偏航在**车体坐标系**里量，副炮挂在主炮塔上时 `turned` 含主炮塔的转动，`moved`
  只有副炮自己的轴。主炮塔反向转得比副炮快（1.3m < s < 4m）时比值落在 (−3, −0.3)，4 帧内把符号推成 −1；之后副炮朝远离目标的方向
  转，误差越来越大，整圈的副炮就一圈圈转，直到主炮塔停下才重新学回来。
- 修法：`LearnSign`（`autoturret/src/turret.h`）：只在 0 号座位（主炮塔）的两轴本帧没动（< 1e-4 rad）时学；那时 `turned/moved`
  才是这根轴的符号。不是延迟或阈值掩盖：主炮塔转动的帧本来就不是对这根轴的测量。
- 离线：`tests/gunner_aim_object_test.cpp`：主炮塔 0.02 rad/帧 对副炮 0.01 反向转 120 帧，符号保持 +1；同样输入按旧规则 4 帧翻成 −1；
  副炮单独转仍能学回正确符号。

## 2. 「这个自动瞄准是不是多余了一个版本，删掉一个」

- 委托方的假设是「两个插件各有一套自瞄 HUD 同时显示」。核对后不成立：自瞄只有一处实现（EDF6AutoTurret `designate.cpp`），HUD 只有
  一处画（`hud.cpp TurretAimMarks`，唯一调用点 `HudDraw`），同一帧只会画一种状态。
- 「两个版本」是同一自瞄的两种模式（按 V / 旧 ini 的 Z 切换）：
  - 自瞄模式：屏幕下方「自动瞄准 开」，下一行「[Z] 自动瞄准 关 [Q] 锁定（长按：清除）」——「开」「关」同屏，读成两套；
  - 预瞄圈模式：「自动瞄准 关（预瞄圈）」，另画一套圈 + 炮膛十字。
  预瞄圈的「往哪打」与 EDF6VehicleCrew 载具 HUD 自己的提前量标记（`docs/hud-re.md` §7.3，更新的实现）重复；炮塔镜头下「炮塔归玩家」与
  不锁定时炮塔跟镜头走相同。保留自瞄 + 锁定，删掉预瞄圈模式：
  - EDF6AutoTurret：`AimMode` / `AimModeKey` / `AimModeButton`（代码、ini、README），模式切换、`EffectiveModeBinding`（旧 Z 与瞄具冲突的
    迁移）、`EDF6AutoTurret_ModeBindingV1` 导出、`PublishAim` 的预瞄圈解算；`PlayerGunRule` 去掉 lead 参数。
  - EDF6VehicleCrew：`TurretAimMarks` 的圈、十字、开 / 关两行、切换横幅、绑定冲突提示，`EDF6VehicleCrew_SightBindingV1` 导出，
    `StockExtras::leadGun`（预瞄圈开时让出原版准星），`turretcam.cpp LeadCircleOn` / `BallisticAim` 的 lead 参数，
    对应的 8 条 HUD 文案。现在下方只有一行 `自动瞄准  [Q] 锁定（长按：清除）`。
  - `common/edf/aimlink.h`：`TurretReadoutV1` / `PlayerAimV4` 布局不变（改结构就得换导出名），只用于预瞄圈的字段置空、注明。
- 离线：`hud_view TurretLayoutApart`（只有一行写自动瞄准、和原版块 / RWR 不重叠）、`autoturret_binding_test`（V / Z 不再是任何键，读数
  只有锁定键）、`player_aim_test`、`turret_lead_check`（新的 `PlayerGunRule`）、`plugin_pair_load_test`（导出清单）、`selftest
  turret_aim_wired` / `hud_switch_cues_wired`。
- 老用户 ini 里留着的三项不再读取（无害）；README 写明可删。

## 3. 「俯瞰视角目的是为了看落点位置的信息……现在完全没办法看瞄准位置」

- 根因：2026-10-23（#241）按当时的反馈「以车辆为中心、不随炮塔摇晃」改成 `tcam::HullPlace`：镜头在车上方 90 m、后方 55 m 看车，
  落点几百米外就出了画面上沿。
- 修法（`src/turretcam.cpp Camera`、`src/turretcam.h`）：镜头看 `ShotFocus` 的预测落点（与 HUD 落点标记同一预测），
  `tcam::OrbitPlace` 从落点上方 `HighCamHeight`、后方 `HighCamBack` 看，朝向 `tcam::ShotHeading` = 车体原点 → 落点的水平方向。
  兼顾 10-23 的「随炮塔上下摇晃」：炮管俯仰只让落点沿这条线前后移动、不改朝向；观察中心按 `kRigEase`（12%/帧）缓动到落点。
  读不到武器时以车为中心。
- **取舍（说清楚）**：落点本身随炮口俯仰大幅移动（平射炮每抬 1° 几十到上百米），「看落点」和「画面完全不动」不可兼得。这里选看落点，
  摇晃只换来平滑的跟随，不是静止；HUD 的落点标记仍是精确值。若实机觉得跟随太晃，下一步是换成稳定器保持的世界方向算落点，而不是
  加大缓动。
- 离线：`turret_cam_check HighView`（朝向 = 车到落点、俯仰变化不改朝向、落点在车边保持朝向）、`turret_cam_runtime_check`
  （炮稳定时看着落点且静止；摇晃时每帧移动有界；停摇后回到落点）。

## 4. 「M163火神防空车(BMPT一样)在第一人称时会自动看向一个特定方向 并且人工转向其他方向仍然会回转到那个方向」

- 根因（代码路径已核对，未实机）：两车都是 `Vehicle603_Flak`，武器带 EDF6AutoTurret 的标记（`tools/make_tracked_combat.py`：M163
  `7301` 防空、BMPT `7302` 对地），由 EDF6AutoTurret 的防空车钩子 `plugin.cpp Steer` 每帧瞄准。玩家的炮位归谁由
  `PlayerGunRule(CameraTurret(...), locked)` 决定；第三人称时 EDF6VehicleCrew 的炮塔镜头接管，`EDF6VehicleCrew_CameraTurretV2` 为真，
  只在锁定时转。第一人称时炮塔镜头让出（`turretcam.cpp ExternalView`），导出返回假 → V1 规则：EDF6AutoTurret 每帧把炮塔转向它
  自己挑的目标，只有摇杆超过 `DragDeadzone`（0.3）才算玩家手动；鼠标每帧的输入很小，达不到，于是视角（眼点挂在 `cannon_main`
  上、随炮塔转）被拽回那个目标。不是「第一人称自动瞄准」功能，也不是固定朝向写回。
- 修法：`src/turretaim.cpp EDF6VehicleCrew_CameraTurretV2`：玩家在这个座位第一人称时也答「玩家自己的视角在转它」（第一人称下鼠标就是
  座位的原生转向输入）。于是和第三人称一样：不锁定时 EDF6AutoTurret 不转、也不把摇杆当拖炮；锁定（Q）后才转向锁定目标。
  泰坦 / 游骑兵坦克炮手座的第一人称同理。实体瞄具 / 俯瞰仍由 `TurretObserverV1` 完全让出。
- 离线：`autoturret_binding_test`：用真实导出、炮塔镜头关、第一人称开：不锁定 → 不转不拖；锁定 → 只转向锁定；都关 → V1（对照）；
  别的座位不算。

## 未实机验证

- 1a / 1b 是否就是用户看到的那种整圈转（需要 `Debug=1` 的 `TURRETCAM` / `GUNNER` 日志复现）；
- 俯瞰的观感、缓动跟随在行进间的晃动程度、大车放大比例；
- M163 / BMPT 第一人称下鼠标转向是否顺手、锁定后转向是否符合预期；
- 预瞄圈删除后，习惯用它打防空的玩家是否满意载具 HUD 自己的提前量标记。
