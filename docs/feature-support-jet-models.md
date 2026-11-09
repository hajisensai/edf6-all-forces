# 支援战机的机型模型（2026-11-08）

原话与决定：`docs/feedback-2026-11-08-fighter-models.md`。

## 1. 模型来源调研

| 来源 | 有什么飞机 | 结论 |
|---|---|---|
| EDF6 原版（Root.cpk / Chunk01 / Chunk02 的 OBJECT） | `BOMBER401`（飞翼轰炸机）、`BOMBER501`（含 `bomber501_2`）、`TRANSPORTER401`、`V508_TRANSPORT`、各种 `PD6xx` 无人机 | 没有战斗机 / 截击机 / 攻击机 / 炮艇机。现在的支援机就是这几架缩放来的 |
| EDF5（本机 `EARTH DEFENSE FORCE 5\Root.cpk`） | `BOMBER401`、`BOMBER501`、`TRANSPORTER401`、`V508_TRANSPORT` | 同上，没有新机型 |
| EDF4.1（本机 `Earth Defense Force 4.1\Root.cpk`，`edf41/` 素材包同源） | `BOMBER401`、`TRANSPORTER401`、`VEHICLE302_HELI` | 没有新机型 |
| `edf5port/` | 只有武器登记与资源 | 无飞机模型 |
| 模型包 `dreadnought/`（已在 `RELEASE_MODELS`，`tools/prep_dreadnought.py`，来源见其 `SOURCE.txt`：仓库所有者决定随 mod 发布） | 《Dreadnought》的 RespawnJet：Fighter、Defender、Bomber、Interceptor（插件已用作无畏舰机库舰载机） | **采用**：四种机型正好对应战斗机 / 多用途（防御）/ 攻击（轰炸）/ 截击 |
| 模型包 `xm_*`（X-Morph: Defense，已在 `RELEASE_MODELS`） | 战斗机、攻击机、重型攻击机、轰炸机（固定翼），炮艇（八旋翼直升机） | 已作为各自的「X-Morph」请求机型在用，挂载是 X-Morph 的；炮艇是旋翼机，不能当固定翼炮艇机绕圈飞 |

## 2. 机型对应

| 支援（`kLaunchRows` 的 JetRole） | 新机体 | 模型 | 标记 | SGO |
|---|---|---|---|---|
| 对地攻击机 strike（含俯冲扫射 / 地毯轰炸 / 打了就跑等出击） | `Body::supStrike` | DN Bomber（27.6 × 22.9 m） | 7061 | `EDF6VC_DN_SUPPORT_STRIKE.SGO` |
| 制空战斗机 fighter（含狼群） | `Body::supFighter` | DN Fighter（28.9 × 17.7 m） | 7062 | `EDF6VC_DN_SUPPORT_FIGHTER.SGO` |
| 截击机 interceptor（含天顶导弹雨） | `Body::supInterceptor` | DN Interceptor（17.5 × 30.2 m） | 7063 | `EDF6VC_DN_SUPPORT_INTERCEPTOR.SGO` |
| 多用途机 multirole（含导弹掠过） | `Body::supMultirole` | DN Defender（30.1 × 18.0 m） | 7064 | `EDF6VC_DN_SUPPORT_MULTIROLE.SGO` |

- 每架的耐久、挂载、净重与原来的同类机完全一样（`pylib/vcobjects.py` `SUPPORT_JETS`：从 `JETS['edf6tr_jet_<role>_mission']` 取，
  `JET_MASSES` 同值，支援战前配置 `kVehicleBodies` 的载重上限因此不变），飞法按原 `kKinds` 的角色。
- 为什么不直接让支援飞无畏舰舰载机（`Body::dn*`）：那四个机体是 `crewless`（机库无人放飞），支援机是真实机组驾驶；同一标记只能有一种
  模型与一套无人 / 有人属性。所以另起 4 个机体、4 个标记（7061–7064），SGO 由 `tools/make_dreadnought.py` 用同一份模型档案
  （`EDF6VC_DN_<MODEL>.MRAB`）生成，不重复打包模型。
- 原来的 `Body::strike / fighter / interceptor / multirole`（`EDF6VC_JET_*.SGO`，bomber 外形）保留给：玩家停放 / 请求的「·自驾」孪生机、
  测试场、接管与接机；这些不是「支援」，不在本次范围。

## 3. 碰撞、挂点、尾焰、座舱

- **碰撞**：SGO 的 `ragdoll` 指向该模型的 `EDF6VC_DN_<MODEL>_AIRFRAME.SHKT`（`pylib/aircraft_collision.py` 按模型实测），刚体框是整机框
  （`vcobjects.jet_sgo` 的 airframe 分支），与机库舰载机同一套；`make_dreadnought.check` 校验。
- **挂点**：武器全挂在 `body`（`jet_weapon_bones` 没有该模型的挂架时的规则，同机库舰载机）；`check` 校验每根武器骨骼都在模型里。
- **尾焰**：四个 DN 模型原来没有喷口骨骼（舰载机一直没有尾焰）。本次按模型网格实测排气口，写进 `pylib/jet_models.py` 的 `NOZZLE_EXITS`，
  `pylib/dreadnought_model.py` `NOZZLED` 在建模时加 `nozzle_<i>` 骨骼（`src/exhaust_nozzles.h FindNozzles` 按名字读，机库舰载机同样受益）：
  - Fighter：脊背两侧两个八角形喷口，口缘 z −7.6，中心 (±3.65, 2.31)，直径 1.43 m；
  - Interceptor（原作 "Booster"）：中线尾部一个推进器，12 边形端面 z −15.1，中心 (0, 6.16)，直径 0.97 m；
  - Bomber：两翼后缘高处两个大八角形喷口，z −10.77，中心 (±7.71, 7.59)，直径 3.39 m；
  - Defender：翼尖两个引擎吊舱的斜后端，中心 (±14.11, 1.04, −7.76)，直径 1.94 m（吊舱端面是斜面，判断依据是侧视图里的深色吊舱，**需实机确认观感**）。
  `make_dreadnought.check` 对每个舰载机档案跑 `jet_models.check_nozzle_bones`（骨骼与实测一致、可逆、不改蒙皮）。
- **座舱**：DN 模型没有座舱件（机库舰载机也没有），玩家登上支援机时没有第一人称座舱内饰。插件的 F-16 座舱（`pylib/cockpit_fits_plugin.py`）
  需要在每个模型里找机内空间，DN 战机是实心外壳，没有可放座舱的空腔；未做，列入待办。

## 4. 安装与缺模型

- 模型包的 `dreadnought/` 已在 `tools/build_release.py` `RELEASE_MODELS` 和分享包 `models-required.json` 的校验里；不需要新增模型文件夹。
- 没有模型文件夹时 `make_dreadnought` 不写任何 DN 文件（安装器一行提示，现在也说明这四种支援不出动）；插件在**规划阶段**就拒绝这四种支援
  （`src/support_dispatch.cpp`：`SupportAircraftReady` 为假时日志写出缺的 SGO、地图上提示「模型包缺失」），不花 pt、不退回旧外形。
  CI 的 nightly 安装包不带模型包，所以 nightly 单独安装时这四种支援不可用（与无畏舰、X-Morph 相同）。

## 5. 联机

每台机器按计划自己造支援机（`PrepareSupportAircraft`）。旧版会造 bomber 外形、标记 7001–7004 的机体，新版造 7061–7064：房间协议从 7 升到 8
（`src/mod_room.h`），旧版不能进新版房间。

## 6. 保持原样、需要的外部模型

| 支援 | 现在 | 需要的模型 |
|---|---|---|
| 炮舰机 gunship（`EDF6VC_CALL_GUNSHIP[_F]`、引导打击） | 原版 BOMBER401 | 固定翼炮艇机（AC-130 一类：侧向火炮、运输机机体），翼展 40 m 级。模型包里只有 X-Morph 的旋翼炮艇，飞法不符 |
| 空中航母 carrier / 自爆 / 人偶无人机母舰 | V508 运输机 × 1.6 | 大型旋翼 / 推力矢量空中母舰（能放无人机的机腹舱），约 60–80 m |
| 伞兵运输机 transportPlane | 原版 BOMBER401 | 固定翼运输机（C-130 一类：尾门、货舱） |

以上都没有已知授权清楚的来源，未自行下载。

## 7. 验证

- 离线：`python tools/make_dreadnought.py --out tmp/dnout`（生成 + `check`：SGO 模型 / 标记 / 耐久 / 挂载 / 碰撞 / 武器骨骼 / 喷口骨骼）、
  `tools/selftest.py` `support_jets_wear_their_models`（C++ `kLaunchRows` / `kBodies` / `kBoardable` / `kVehicleBodies` 与 vcobjects、
  support_loadout 一致，标记不撞，协议 ≥ 8）、`support_dispatch_test`（机体未安装：规划阶段拒绝并说明，什么也不造）。
- 需实机确认：四种模型的大小与飞行观感（DN 模型比原 bomber 外形宽）、尾焰位置（尤其 Defender 翼尖吊舱）、导弹 / 机炮从 `body` 发出的观感、
  编队间距（模型更宽，翼尖可能相碰；NPC 刚体是整机框）、低空俯冲时整机框与地面的余量、玩家登机后的视角（无座舱内饰）。
