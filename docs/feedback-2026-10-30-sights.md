# 2026-10-30 反馈：瞄具周围全黑、单眼瞄具

用户原话（2026-10-30）：「有些瞄具周围不应该是全黑的，有些是单眼看的瞄具」

## 现状与根因

- 所有圆形瞄具只有一种画法：`src/hud.cpp Eyepiece` 在视场（屏幕短边的 0.46）外画 64 片不透明黑（`kScopeDark` 0.97）
  一直到屏幕角——双目目镜（两眼都贴在目镜罩里）的样子。
- 选哪种遮罩只看火控类型（`sightzoom.h MaskOf(Kind)`：光学 / 导弹 / 火箭 = 圆形黑遮罩，能量 / 炮舰炮手 = 传感器屏），
  **不看这个座位实际看的是什么**。镜头定位骨只有一个家族 `vc_optic_NN`（`src/optic_mount.h`），数据里没有「这是目镜还是摄像机」。
- 结果：无人炮塔（第一人称是 `remote_display` 监视器的座位）开镜时也被盖上整圈黑——操作员看的是显示器，本不该有目镜；
  步兵的狙击镜 / ACOG / 火箭筒镜都是单眼望远镜，也被画成双目全黑。

## 修法：镜头定位骨的名字就是它的目镜类型（数据驱动）

- 登记表 `pylib/vehicle_optics.py`：`EYEPIECES` 前缀 → 类型，`Lens.marker` 的前缀即类型，安装器建模型时校验（`eyepiece()`，
  名字不合规直接停）：
  - `vc_optic_NN` 双目目镜（binocular）：视场外全黑（原样）；
  - `vc_monoc_NN` 单眼目镜（monocular）：视场较小（短边 0.40），外面一圈目镜罩黑环（视场半径的 12%），再往外是**变暗但可见**的
    周围画面（`kSurroundAlpha` 0.55，另一只眼看到的东西），不是全黑；
  - `vc_camera_NN` 无人站的炮塔摄像机：没有目镜，按传感器监视器画（全屏、细边框、角标、视场名）。
- 运行时 `src/optic_mount.h kFamilies` 按三个前缀找定位骨，`Pose.eyepiece` → `sightzoom.cpp Capability.eyepiece` →
  `SightScope.eyepiece` → `sightzoom.h MaskOf(kind, eyepiece)`（`Mask::round / monocular / sensor`）→ `hud.cpp ScopeShade / Eyepiece`。
  分划板按对应视场半径裁剪（`scopeNow.field`）。能量武器 / 炮舰炮手仍是传感器屏，不受定位骨影响。
- 步兵开镜（`infantryscope.h EyepieceOf`）：手持武器的瞄具（狙击镜、ACOG 类战斗瞄具、火箭筒望远镜）都是单眼望远镜 → 单眼样式。

## 盘点：每个有实体瞄具的座位

只有下表这些座位有实体镜头定位骨（`vehicle_optics.MODELS`）；插件自制载具（BMPT、通古斯卡、88 炮、X-Morph 单位等）没有定位骨，
不会开镜、不出遮罩，第一人称是 `remote_display` 监视器，本次不涉及。

| 模型 / 座位 | 镜头来源 | 现在的类型 | 依据 |
|---|---|---|---|
| `VEHICLE403_TANK` 两侧机枪 (seat 1/2) | 原生炮手镜头 カメラ２/３ | **camera**（改） | 无人炮塔（用户 2026-10-13「明明是无人炮塔」），座位是 `remote_display` |
| `VEHICLE404_BIGTANK` 泰坦两侧副炮 (seat 1/2) | 副炮塔观瞄玻璃 | **camera**（改） | 无人副炮塔，座位是 `remote_display` |
| `VEHICLE401_STRIKER` 遥控炮塔 | 炮塔顶瞄准头的窗 | **camera**（改） | 遥控炮塔（塔里没有空间，驾驶员看监视器） |
| `V505_TANK` E551 主炮 | 前向潜望镜面 Light02 | binocular（待定） | 虚构车型，真实对应的光学类型不确定 |
| `V505_TANK_EDF6BENEFITS` E551 特典涂装主炮 | 同上 | binocular（待定） | 同上 |
| `V601_TANK` 主炮 | 车体观瞄面 | binocular（待定） | 同上 |
| `VEHICLE403_TANK` 主炮 | 炮塔圆顶观瞄块（观瞄窗，非同轴望远镜） | binocular（待定） | 同上 |
| `VEHICLE404_BIGTANK` 泰坦主炮 | 主炮塔 barbette 观瞄窗 | binocular（待定） | 同上 |
| 步兵：狙击镜 / 战斗瞄具 / 火箭筒镜 | 原生开镜 | **monocular**（改） | 手持武器瞄具都是单眼望远镜 |

## 待用户确认

1. 四台有人主炮（E551 / E551 特典、601、403 主炮、泰坦主炮）是双目目镜（全黑）还是单眼目镜（周围变暗可见）？
   改一行 `pylib/vehicle_optics.py` 的 `vc_optic_00` → `vc_monoc_00` 即可，重新安装生效。
2. 步兵狙击镜要不要保留全黑（很多游戏的习惯画法），只把 ACOG / 火箭筒镜改成单眼？现在三种都是单眼。
3. 单眼样式周围的亮度（现为 55% 黑）是否合适。

## 验证

- `scopeview_check`：单眼视场在双目视场圆内，视场内清晰、目镜罩只黑、罩外到屏幕四角都是变暗片，11 种分辨率。
- `sightzoom_test`：`vc_monoc_` 定位骨 → monocular 遮罩；`vc_camera_` → 仍是实体瞄具（眼位、倍率），遮罩为传感器屏；
  编号超出 `kMaxAnchors` 不算定位骨。
- `hud_view`：`stock_tank_sight_zoom_6x_monocular`（罩 64 片 + 变暗 64 片 + 数据行）、`_camera`（无目镜片、监视器倍率名）、
  步兵三种开镜都为单眼。
- `tools/vehicle_optics_check.py`：每个镜头的类型清单、前缀与 `optic_mount.h` 一致、非法名字报错、同模型编号不重复。
- **未进游戏**：单眼样式在实机画面里的观感、无人炮塔第一人称下与监视器（`RwsDisplay`）叠放未实测。

## 追加：第一人称默认驾驶室，按瞄具键进瞄具

用户原话（2026-10-30）：「驾驶室视角得按观察键才显示。该成瞄准键？」

- 根因（读代码确认）：`firstperson.h FirstPersonGunnerSight()` 只要第一人称开着就为真，`sightzoom.cpp Up` = 不在观察 且
  （放大 或 第一人称），所以有实体瞄具的座位（上表那些）一进第一人称就是 1 倍瞄准镜，`VehicleYields` 把镜头让给瞄具；只有按住
  观察键（`VehicleLookActive` 让 `Up` 为假）才露出驾驶室。没有实体瞄具的座位本来就是驾驶室，不受影响。
- 修法：第一人称多一个「瞄具举起」状态（`Toggle.raised` / `Cue.raised`），纯函数 `sightzoom.h Press`：
  - 第三人称：Z / R3 照旧 1× → 3× → 6× → 1×（1× 是炮塔镜头）；
  - 第一人称：驾驶室 → 瞄准镜 1× → 3× → 6× → 驾驶室；每次打开第一人称都从驾驶室开始；第三人称放大着切到第一人称，倍率保留，
    继续按到 6× 之后回驾驶室；
  - 观察键只扭头：举起瞄具时按住观察键仍临时让出镜头（看周围），松开回瞄具，不改变举起状态。
  - `SightZoomRaised` 取代 `turretcam.cpp TurretCamFreeLook` 里的「放大或第一人称」判断。
- 键位选择：用插件已有的瞄具键（键鼠 Z `SightZoomKey`、手柄 R3 `SightZoomButton`），切换而不是按住。没有用鼠标右键 / LT：
  载具上右键 / LT 是副射击组（同轴机枪、泰坦车头加特林，`payload.cpp PayloadSightWeapons`），占用会打不了副武器。
- 待用户确认：
  1. 想要的「瞄准键」是 Z / R3（现做法，切换）还是鼠标右键 / LT「按住瞄准」？后者要把副武器挪到别的键，需要拍板。
  2. 手柄 R3 在有高视角的车（喀秋莎、大车）上让给高视角（`HighCamButton`），这些车手柄进瞄具没有键，是否另配？
- 验证：`sightzoom_test`（第一人称先驾驶室、按键举起 1×、3×、6×、回驾驶室；观察键不放下瞄具；重开第一人称从驾驶室开始；
  第三人称 3× 进第一人称保留）；`sightzoom.h` 的 `static_assert`；`turret_cam_runtime_check`。**未进游戏**。
