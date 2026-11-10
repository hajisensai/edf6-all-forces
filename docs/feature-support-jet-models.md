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

## 6. 炮舰机、伞兵运输机换成 C-130；空中航母仍缺模型（2026-11-10）

维护者原话（`docs/feedback-2026-11-10-support-plane-models.md`）：「没有模型来源的支援机型 直接去网站上拿呗，不用管授权」。

| 支援 | 现在 | 模型 |
|---|---|---|
| 炮舰机 gunship（`EDF6VC_CALL_GUNSHIP[_F]`、引导打击 `EDF6VC_CALL_SORTIE_GUIDED`） | **AC-130**：新机体 `Body::c130Gunship`，标记 **7065**，`EDF6VC_C130_GUNSHIP.SGO` | 模型包 `c130/gunship.obj` |
| 伞兵运输机 transportPlane | **C-130**：`Body::transportPlane` 改用 `EDF6VC_C130_TRANSPORT.SGO`，标记由 7001 改为 **7066** | 模型包 `c130/transport.obj` |
| 空中航母 carrier / 自爆 / 人偶无人机母舰 | 不变：V508 运输机 × 1.6 | **没找到**，见 6.4 |

### 6.1 来源

- FlightGear FGAddon 的 Lockheed C-130 Hercules：`https://svn.code.sf.net/p/flightgear/fgaddon/trunk/Aircraft/C130`（svn 直链，不用登录，
  2026-11-10 下载到 `D:\APP\edf6-models\downloads\flightgear_c130`），GPL v2（随模型复制 `COPYING`），`c130/SOURCE.txt` 记了来源与改动。
  机体 `Models/c130.ac` + `texture.png`、螺旋桨 `Models/Propellers/propeller.ac`，炮舰机的火炮取它自带的 AC-130 内饰
  `Models/Interior/ac130/armament/gau12`（两门 GAU-12 25 mm 加特林）与 `armament/40`（40 mm 博福斯），按它的 `all-gau12.xml` /
  `all-bofors.xml` 摆在机身左侧。
- `tools/prep_c130.py`（开发者用，读 AC3D：`pylib/ac3d.py`，只当数据读）→ `D:\APP\edf6-models\c130\`：两个 OBJ（游戏坐标、米，+Y 上、+Z 机头、
  +X 机体左侧）、每个材质的颜色 / 平法线 / 粗糙度贴图、`c130.json`（骨骼：机体 + 4 个螺旋桨 `c130_prop_<i>`）。起落架去掉（飞行姿态），
  舱门保持关闭；窗玻璃改为不透明深色。整机 40.29 × 11.63 × 29.64 m（翼展 × 高 × 长；原机 40.41 × 11.9 × 29.79）。
- 公开模型站上 AC-130 / C-130 的其它候选（Sketchfab、cgtrader 免费区）都要登录才能下载，FGAddon 的是唯一的直链，且自带 AC-130 火炮。

### 6.2 生成与接入

- `pylib/c130_model.py`：OBJ → `EDF6VC_C130_<GUNSHIP|TRANSPORT>.MRAB`（骨架照无畏舰：`mdl` → 模型骨骼，`globalSRT` → `body`（V506 锚点，
  BODY_AT）→ `c130_hull` → `c130_prop_1..4`；材质借 V505 车体材质）。`build_model(textures=False)` 只建网格不压贴图，给
  `jet_models._model_of` 量包围盒 / 碰撞用。
- `tools/make_c130.py`（安装器分组 `c130`，ledger 所有者 `c130`，模型输入 `c130/`）：两个模型档案、各自的碰撞
  `EDF6VC_C130_<M>_AIRFRAME.SHKT`（`aircraft_collision.C130_FILES`，24 × 32 格，SGO 带 `edf6vc_airframe` 成员，同 DN 支援机）、两个 SGO：
  - 炮舰机：`vcobjects.C130_JETS` 的 `edf6tr_c130_gunship`，炮舰机原有的耐久与挂载，`make_jets.with_gunner_seat` 加侧炮手座；
  - 运输机：`edf6tr_c130_transport`，打击机的耐久与挂载，`with_passenger_seats` 加 12 个乘客座（共 13 座）。
  - 座舱（`pylib/cockpit_fits_plugin.py`）：驾驶座 `plane_deck_gun` / `plane_deck` 眼点 (0.6, 3.0, 10.0)（更靠前时座舱面板从机头露出），
    炮手座 `remote_display` (1.5, 2.4, 3.0) 朝左（yaw 90），乘客 `plane_hold` 首座 (-0.9, 2.2, 3.0)。眼点周围净空用射线实测（见该文件注释）。
  - `check`：SGO 的模型 / CAS / 标记 / 耐久 / 挂载 / 碰撞文件 / 武器骨骼、炮手座与乘客座、模型档案 `c130_model.check_archive`（跳过座舱嫁接的骨骼与材质）。
  - 没有 `c130/` 文件夹时什么也不写（安装器一行提示）；插件在规划阶段就拒绝炮舰机支援 / 伞兵运输机（`SupportAircraftReady`：机体 SGO 未预载），
    不退回 bomber401 外形。CI 的 nightly 安装包不带模型包，所以 nightly 单装时这两种支援不可用（同无畏舰支援机）。
- `make_jets` 不再写 bomber401 外形的伞兵运输机（`EDF6VC_JET_TRANSPORT.SGO`、`EDF6VC_TRANSPORT_PLANE.MRAB` 及其座舱）。
  `EDF6VC_JET_GUNSHIP.SGO`（`Body::gunship`，标记 7011）与 `EDF6VC_FLY_GUNSHIP.SGO`（空袭兵请求来自驾的炮舰机）保持 bomber401 外形：
  标记 7011 仍是请求孪生机的，机库 / 抓取规则不变。

### 6.3 为什么另起标记

- 插件按 mission_setup 标记认机体（`jet.cpp` `IsJetVehicle` / `CrewlessJet`、`playerjet_kinds.inc` `BoardRowOf`）。
  - 炮舰机：侧炮的出膛点按整机包围盒算（`src/gunmuzzle.h`），bomber401 是 51.9 × 4.0 × 16.2 m，C-130 是 40.3 × 11.6 × 29.6 m。
    同一个标记分不出两种外形，所以 C-130 炮舰机用 7065，`jet_bay.cpp AirframeOf` 按标记取 `kC130Gunship` 或 `kGunship`；
    `kC130Gunship` = `make_c130.C130_AIRFRAME`，`make_c130.check_muzzle` 对模型、`selftest support_c130_models` 对头文件各校验一次。
  - 运输机：原来与打击机共用 7001，`BoardRowOf` 再按模型骨骼认成 bomber401；C-130 不是 bomber401，会被认成打击机（elevon 轰炸机），
    所以改用 7066，`kBoardable` 给它自己一行（按打击机的飞法）。
- 质量：两者都按 AC-130 的 70 t（`JET_MASSES`，同 7011）；挂载同各自原来的（`stores.inc` 已重新生成）。
- 联机：每台机器按计划各自造支援机，旧版会造 bomber401 外形、标记 7011 / 7001 的，新版造 7065 / 7066：房间协议 8 → **9**。

### 6.4 空中航母：没找到可下载的模型（待办）

- 需要：大型旋翼 / 推力矢量空中母舰，60–80 m，机腹能放无人机；插件还依赖现模型的 4 个可倾转推进器骨骼（`boosterF_l` 等，
  `aircraft_collision.NACELLES`、`src/nacelle_reach.h`）、着陆架舱作最低点、无人机停靠位置。
- 2026-11-10 搜过：Poly Pizza（airship / carrier：只有低多边形飞艇、民用船）、Sketchfab（科幻飞行航母为付费模型）、cgtrader（"flying aircraft
  carrier" 付费；免费区 "futuristic aircraft carrier" 要登录下载，且看不出是飞行母舰）。没有不登录就能直接下载、外形合适的。
- 待办：维护者若能提供（或允许用账号下载）一个模型，按 C-130 的流程做 `prep_<x>.py` / `<x>_model.py`，并另做推进器骨骼与无人机停靠点的映射；
  在那之前空中航母保持原版 V508 × 1.6。

### 6.5 需实机确认

- 两架 C-130 的大小与飞行观感（比 bomber401 窄 12 m、长 13 m、高 7 m），炮舰机绕圈时侧炮从机体包围盒外出膛的观感；
- 驾驶座 / 炮手座 / 乘客座的第一人称座舱位置与朝向（只做了离线净空检查）；
- 螺旋桨不转（骨骼已留，插件未驱动）；没有尾焰（涡桨，模型没有喷口骨骼）；
- 运输机投伞兵的位置（仍是插件按机体原点投放）。

## 7. 验证

- 离线：`python tools/make_dreadnought.py --out tmp/dnout`（生成 + `check`：SGO 模型 / 标记 / 耐久 / 挂载 / 碰撞 / 武器骨骼 / 喷口骨骼）、
  `tools/selftest.py` `support_jets_wear_their_models`（C++ `kLaunchRows` / `kBodies` / `kBoardable` / `kVehicleBodies` 与 vcobjects、
  support_loadout 一致，标记不撞，协议 ≥ 8）、`support_dispatch_test`（机体未安装：规划阶段拒绝并说明，什么也不造）。
- 需实机确认：四种模型的大小与飞行观感（DN 模型比原 bomber 外形宽）、尾焰位置（尤其 Defender 翼尖吊舱）、导弹 / 机炮从 `body` 发出的观感、
  编队间距（模型更宽，翼尖可能相碰；NPC 刚体是整机框）、低空俯冲时整机框与地面的余量、玩家登机后的视角（无座舱内饰）。
