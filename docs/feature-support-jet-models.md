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
| 对地攻击机 strike（含 DN 轰炸机·出击） | `Body::supStrike` | DN Bomber（27.6 × 22.9 m） | 7061 | `EDF6VC_DN_SUPPORT_STRIKE.SGO` |
| 制空战斗机 fighter（含 DN 战斗机·出击） | `Body::supFighter` | DN Fighter（28.9 × 17.7 m） | 7062 | `EDF6VC_DN_SUPPORT_FIGHTER.SGO` |
| 截击机 interceptor | `Body::supInterceptor` | DN Interceptor（17.5 × 30.2 m） | 7063 | `EDF6VC_DN_SUPPORT_INTERCEPTOR.SGO` |
| 多用途机 multirole（含 DN 防御者战斗机·出击） | `Body::supMultirole` | DN Defender（30.1 × 18.0 m） | 7064 | `EDF6VC_DN_SUPPORT_MULTIROLE.SGO` |

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

## 6. 炮舰机、伞兵运输机换成 C-130；空中航母换成 Helicarrier（2026-11-10）

维护者原话（`docs/feedback-2026-11-10-support-plane-models.md`）：「没有模型来源的支援机型 直接去网站上拿呗，不用管授权」。

| 支援 | 现在 | 模型 |
|---|---|---|
| 炮舰机 gunship（`EDF6VC_CALL_GUNSHIP[_F]`、C-130 炮艇机·出击 `EDF6VC_CALL_SORTIE_GUIDED`） | **AC-130**：新机体 `Body::c130Gunship`，标记 **7065**，`EDF6VC_C130_GUNSHIP.SGO` | 模型包 `c130/gunship.obj` |
| 伞兵运输机 transportPlane | **C-130**：`Body::transportPlane` 改用 `EDF6VC_C130_TRANSPORT.SGO`，标记由 7001 改为 **7066** | 模型包 `c130/transport.obj` |
| 空中航母 carrier / 自爆 / 人偶无人机母舰 | **Helicarrier**：新机体 `Body::supCarrier` / `supBlastCarrier` / `supDollCarrier`，标记 **7067 / 7068 / 7069**，`EDF6VC_HELICARRIER[_BLAST\|_DOLL].SGO` | 模型包 `helicarrier/helicarrier.obj`，见 6.4 |

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
    `kC130Gunship` = `make_jets.muzzle_airframe(make_c130.C130_AIRFRAME)`：框以载具原点为中心（`veh+0x90` 是碰撞箱中心，炮舰机的碰撞箱就是整机包围盒，`make_jets.check_rigid_centre` 在生成时核对），`make_c130.check_muzzle` 对模型、`selftest support_c130_models` 对头文件各校验一次。（2026-11-10 审查更正：最初把框放在机腹原点上方 5.8 m，向下开火的出膛点会落进机身；bomber401 炮舰机的 `kGunship` 同因偏高 2.1 m，一并改正。）
  - 运输机：原来与打击机共用 7001，`BoardRowOf` 再按模型骨骼认成 bomber401；C-130 不是 bomber401，会被认成打击机（elevon 轰炸机），
    所以改用 7066，`kBoardable` 给它自己一行（按打击机的飞法）。
- 质量：两者都按 C-130J 机体的 54.6 t（`JET_MASSES`，同 7011；2026-11-26 起，见 §8）；挂载同各自原来的（`stores.inc` 已重新生成）。
- 联机：每台机器按计划各自造支援机，旧版会造 bomber401 外形、标记 7011 / 7001 的，新版造 7065 / 7066：房间协议 8 → **9**。

### 6.4 空中航母：Sketchfab 的 Helicarrier

- 来源：Sketchfab「Helicarrier」`https://sketchfab.com/3d-models/helicarrier-97cf6cccb2fa48c18fbef5c5600c434d`（uid
  `97cf6cccb2fa48c18fbef5c5600c434d`，上传者 ramonaflowers，glb 元数据署名作者 CoolCat；许可 **CC Attribution（CC BY 4.0）**；
  12841 三角形、一张 1024² 贴图）。用账号 token 走下载 API（token 只在运行时从 `%USERPROFILE%\.edf6-sketchfab	oken` 读，不进仓库 / 日志），
  原件在 `D:\APP\edf6-models\downloads\sketchfab_helicarrier\`，只当数据读（`python -I`）。选它是因为在可下载的候选里它有「四个升力风扇 + 甲板 +
  舰岛」，风扇正好对上插件的四个倾转推进器，面数也适合游戏。
- `tools/prep_helicarrier.py`（开发者用）→ `D:\APP\edf6-models\helicarrier\`：缩放到全长 80 m（系数 0.202），x 以四个风扇中心的平均为 0、
  y 以最低点为 0、z 居中；整机 **45.96 × 16.39 × 80.0 m**（宽 × 高 × 长；原版 V508 × 1.6 是 59.4 × 17 × 76.8 m）。舰首 +z（双叉甲板），舰岛在右舷（-x）。
  四个升力风扇（轮毂 + 辐条 + 转子圈）从船体里按连通面切出来作独立部件，绕轮毂**立起来**（轴朝 +z）存进绑定姿态，SOURCE.txt 记来源与改动。
- 推进器映射：插件 `jet_flight.cpp Thrusters` 按骨骼名 `boosterF_l / F_r / B_l / B_r` 把推进器绕本地 X 从 0° 转到 -110°（悬停 -90°）。
  四个风扇就用这四个名字（`helicarrier_model.THRUSTERS`，轴心为轮毂：前 (±15.5, 8.9, 0.7)、后 (±13.8, 9.0, -23.0)，直径 8.7–9.7 m），
  所以巡航时风扇竖着、悬停时转平落回风扇圈里，插件逻辑不改。每个风扇下挂一根 `nozzle_<i>`（`jet_models.with_nozzles`，宽 = 风扇直径），尾焰沿用。
- 着地倾转限幅：`src/nacelle_reach.h` 另给 `kHcFront` / `kHcBack`（`aircraft_collision.nacelle_lowest` 对新模型实测，`aircraft_collision_check`
  在有模型包时逐项核对），`jet.cpp NewEntry` 按标记记下 `Jet::liftFans`，`Thrusters` 据此选表。风扇最低点离船底至少 5 m，实际不会限制倾转。
- 无人机：放出点 / 停靠点不改（`jet_carrier.cpp kLaunchBelow` 25 m、`jet_internal.h kDockBelow` 15 m，都在载具原点 = 碰撞箱中心之下）。
  新模型高 16.39 m，停靠点在船底下 6.8 m、放出点 16.8 m，都在船体投影内；`make_helicarrier.check_drone_bay` 生成时从源码读这两个常量核对
  （离船底不小于 5 m、在投影内）。
- 生成：`pylib/helicarrier_model.py`（骨架同 C-130：`mdl` → `helicarrier` → `globalSRT` → `body` → `hc_hull` → 四个风扇，再加喷口骨骼）、
  `tools/make_helicarrier.py`（安装器分组 / ledger 所有者 `helicarrier`，模型输入 `helicarrier/`）：`EDF6VC_HELICARRIER.MRAB`、碰撞
  `EDF6VC_HELICARRIER_AIRFRAME.SHKT`（`aircraft_collision.HELICARRIER_FILES`，SGO 带 `edf6vc_airframe`）、三个 SGO（`vcobjects.HELICARRIER_JETS`：
  各自原机种的耐久、挂载与质量，标记 `HELICARRIER_MARKS` 7005→7067、7009→7068、7010→7069）。
- 座舱：舰岛里的眼点 (-4.0, 12.5, -19.0) 净空只有约 1–3 m，`plane_deck` 收缩 8 轮仍穿出舰岛斜壁，所以用 `remote_display`（`cockpit_fits_plugin`）。
- 另起标记的原因同 6.3：新外形的包围盒、喷口、倾转表都不同，按标记才分得开。V508 外形的空中航母（7005 / 7009 / 7010）仍是空袭兵请求来自驾 /
  测试场停放的孪生机，不变。可驾驶：三行 `kBoardable`（旋翼、无人机挂载，同原航母）。
- 没有 `helicarrier/` 文件夹时什么也不写，三种航母支援在规划阶段拒绝（`SupportAircraftReady`：机体 SGO 未预载），不退回 V508 外形。
- 联机：新版造 7067–7069 的航母，房间协议 9 → **10**。

### 6.5 需实机确认

- Helicarrier：舰首方向（+z 双叉甲板朝前）与大小观感；四个风扇巡航竖立、悬停转平落回风扇圈（倾转方向）；尾焰从风扇下方喷出；
  碰撞与降落（整机最低点着地）；无人机从机腹下放出 / 回收的位置；舰岛 `remote_display` 座舱的位置与朝向；
- 两架 C-130 的大小与飞行观感（比 bomber401 窄 12 m、长 13 m、高 7 m），炮舰机绕圈时侧炮从机体包围盒外出膛的观感；
- 驾驶座 / 炮手座 / 乘客座的第一人称座舱位置与朝向（只做了离线净空检查）；
- 螺旋桨不转（骨骼已留，插件未驱动）；没有尾焰（涡桨，模型没有喷口骨骼）；
- 运输机投伞兵的位置（仍是插件按机体原点投放）。

### 6.6 天顶截击机：Sketchfab 的 TU-1600 Zvezda（2026-11-26）

- 来源：Sketchfab「SPACESHIP PAK DA (K) TU-1600 ZVEZDA」（uid `30e58a90fe874f21afdfaa5c4022b311`，作者 Ilya Shevchuk / shilma513，
  许可 **CC Attribution（CC BY 4.0）**；177534 三角形、7 个材质、4096² 贴图）。原件在 `D:\APP\edf6-models\downloads\salvo_tu1600\`，只当数据读
  （`python -I`，脚本放在下载目录外、路径当参数传）。替换的是 `tools/calls.py` `sortie_missile_rain`（天顶截击机）的外观，原来是
  bomber501_2 加一圈发光导弹舱圆柱。
- `tools/prep_tu1600.py`（开发者用）→ `D:\APP\edf6-models\salvo_tu1600\`：源模型 +Y 朝上、机头朝 -x，转到游戏坐标 (z, y, -x)（绕 Y 旋转，
  绕向不变）；缩放到翼展 40 m（系数 1.39958），整机 **40.0 × 9.67 × 61.8 m**（宽 × 高 × 长）；最低点为 y 0，x、z 居中。
  `mesh_simplify` 按材质分组、保 UV 缝，从 177534 减到 72000 三角形（最大误差 0.03 m）。去掉 Decals 材质（MASK 贴花，62 个三角形）：船体材质不透明，
  留着会露出透明角。贴图减半到 2048²（同原版机体量级），法线是 DirectX 绿通道（G 朝下），按 `pbr_maps` 统一成 R 粗糙度 / G 金属度。
  SOURCE.txt 记署名与改动，`tu1600.json` 记材质、尺寸和两个喷口。
- 喷口：机尾六个钟形喷管，两侧各三个，呈三角形排布。`MAX_NOZZLES` 是 4，放不下 6 个，所以每侧三个合成一束：`nozzle_0` / `nozzle_1`
  位于三个喷口端面的中心 (±5.44, 4.27, -25.90)，宽 4.73 m（三个喷口的外接宽度），长 23.6 m（宽 × 5）。
- 生成：`pylib/salvo_model.py`，骨架同 C-130：`mdl` → `tu1600` → `globalSRT` → `body` → `tu_hull`，再挂两个喷口骨骼和 `hp_gun` / `hp_store`。
  材质用 V505 船体材质，换上本模型的贴图（7 个材质，BC7 / BC5 / DXT1），LOD 由 `model_lod.finish` 自动生成（72k / 25k / 12.6k）。
  `tools/make_jets.py` `salvo_files` 写出 `EDF6VC_SALVO_INTERCEPTOR.MRAB` 和碰撞 `EDF6VC_SALVO_INTERCEPTOR_AIRFRAME.SHKT`
  （`aircraft_collision.SALVO_FILES`，属于 `PACK_FILES`，SGO 带 `edf6vc_airframe`）。安装器缓存的模型输入是 `jets` → `salvo_tu1600/`，
  发布包和分享包（`run_export.py EXTERNAL_MODELS`）都带这个目录。
- 发射点：256 枚导弹从 `hp_store` 出膛，偏移沿用原版导弹舱的四个炮口偏移（`vcobjects.hardpoint_muzzles`，所有战机共用，本模型不改）。
  决定出膛位置的是**骨骼位置**，不是偏移：`jet_hardpoints.with_hardpoints` 把 `hp_store` 放在机身纵向中部，在满足「每个炮口都在蒙皮内、
  离地不少于 `GROUND_CLEAR`」的前提下取最高处。实测 `hp_store` 在 (0, 1.42, 0)，八个炮口落在机腹中线两侧 x ±1.05…±1.96、y 0.75…0.94、
  z 0.2…1.5。没有外挂圆柱，原模型的发光导弹舱随之去掉，发光只来自导弹本身。
- 缺 `salvo_tu1600/` 目录时，`make_jets` 打一行提示，不写模型、碰撞和 `EDF6VC_JET_SALVO_INTERCEPTOR.SGO`；插件规划时找不到预载的 SGO，
  天顶截击机就不出动（同 6.4），不退回旧外观。
- `vcobjects.JETS[SALVO_JET]` 指向新模型：`(app:/object/edf6vc_salvo_interceptor.mrab, edf6vc_salvo_interceptor.mdb)`、文件 `SALVO_MODEL`、
  `body` / `anchor` 都是 `body`（同 C-130 / Helicarrier）。
- 需实机确认：机头朝向与大小观感（62 m 长，编队间距是否够）；两束尾焰的位置；导弹从机腹出膛的观感；碰撞与着地；
  质量（2026-11-26 起）按同尺寸的 Tu-160 取 240 t（§8），不再借截击机的 `JET_MASSES[7003]`：满挂（4.5 t 导弹 + 挂架）几乎不拖慢它，
  撞击伤害是原来 20 t 时的 12 倍。

## 7. 验证

- 离线：`python tools/make_dreadnought.py --out tmp/dnout`（生成 + `check`：SGO 模型 / 标记 / 耐久 / 挂载 / 碰撞 / 武器骨骼 / 喷口骨骼）、
  `tools/selftest.py` `support_jets_wear_their_models`（C++ `kLaunchRows` / `kBodies` / `kBoardable` / `kVehicleBodies` 与 vcobjects、
  support_loadout 一致，标记不撞，协议 ≥ 8）、`support_dispatch_test`（机体未安装：规划阶段拒绝并说明，什么也不造）。
- C-130（2026-11-10）：`python tools/make_c130.py --out <scratch>`（生成 + `check`：两个 SGO 的模型 / CAS / 标记 / 耐久 / 挂载 / 碰撞 /
  `edf6vc_airframe`、炮手座与 13 座、模型档案骨骼 / 蒙皮 / 着地、`check_muzzle` 整机包围盒 = `C130_AIRFRAME`；本机约 1 分 46 秒）、
  `selftest` `support_c130_models`（kLaunchRows / kBodies / kBoardable / gunmuzzle.h / jet_bay.cpp / 协议 ≥ 9）、
  `gunship_muzzle_check`（AC-130 箱：正下方出膛在机腹下、平射在翼尖外、横穿长度 = 翼展）、`cockpit_build_*`（C-130 两个生成模型的座舱骨骼预算按
  `c130_model.SKELETON_BONES`）。
- 需实机确认：四种模型的大小与飞行观感（DN 模型比原 bomber 外形宽）、尾焰位置（尤其 Defender 翼尖吊舱）、导弹 / 机炮从 `body` 发出的观感、
  编队间距（模型更宽，翼尖可能相碰；NPC 刚体是整机框）、低空俯冲时整机框与地面的余量、玩家登机后的视角（无座舱内饰）。

## 8. 机体质量按真实原型（2026-11-26）

用户：「重量全得重新做，全都还原一下」，选「按真实机型重做」。`pylib/vcobjects.py` `JET_MASSES`（`src/stores.inc` `kJetMasses`）是
净重：挂载的重量比 `store_burden.h Ratio`、玩家撞击 `playerjet.cpp RamMass` 和支援战前配置的载重上限（40%，`support_loadout.h`
`kVehicleBodies`）都以它为准；飞行的推力 / 过载常数按机种、与绝对质量无关，所以**空载飞法不变**，变的只是挂满后被拖慢多少、撞击多重、载重上限。

- 口径统一：**空重 + 满内油**，不含外挂（外挂由 `Ratio` 另加，算进来就重复了），不含乘员；只公布油量体积的按 0.8 kg/L 折算；取整到 100 kg。
- 有作战角色的机体按该角色的真机取（不管它穿哪个原版模型）；科幻 / X-Morph / 无人机没有原型，取**尺寸最接近的真机**，质量直接用、不按体积缩放
  （按包围盒体积缩放会让小机体满挂的质量比到 1.8 左右）。
- 来源：未注明的都是 Wikipedia 对应条目的 Specifications 段；B-1B 另对照 USAF 资料页。

| 机型（标记） | 原型 | 质量（旧 → 新） | 口径 | 来源 |
|---|---|---|---|---|
| 攻击机 7001、玩家攻击机 7202、支援 7061、无畏舰轰炸机 7043 | F-15E | 22 / 22 / 22 / 22 t → 21.6 t | 15,694 空重 + 5,858 内油 | Wikipedia F-15E |
| 战斗机 7002、玩家战斗机 7201、支援 7062、无畏舰战斗机 7041 | F-15C | 16 / 16 / 16 / 14 t → 19.3 t | 13,154 + 6,103 | Wikipedia F-15 |
| 截击机 7003、支援 7063、无畏舰截击机 7044 | F-14D（AIM-54 挂载） | 20 / 20 / 13 t → 27.2 t | 19,838 + 7,348 | Wikipedia F-14 |
| 多用途机 7004、支援 7064、无畏舰防卫机 7042 | F/A-18E | 18 / 18 / 15 t → 21.2 t | 14,552 + 6,668 | Wikipedia F/A-18E/F |
| 空中航母 7005 / 7009 / 7010、Helicarrier 7067–7069 | C-5M（尺寸 67.9 × 19.8 × 75.3 m；V508×1.6 为 59.4 × 17.0 × 76.8 m，Helicarrier 46.0 × 16.4 × 80.0 m） | 120 t → 327.2 t | 172,365 + 154,880 | Wikipedia C-5 Galaxy |
| 无人机 7006–7008（含爆破 / 人偶无人机） | BQM-167A（6.1 m 长、3.4 m 翼展；模型 5.7 m） | 2.2 t → 0.7 t | 313 空重 + 约 435 L 燃油 ×0.8 | Kratos 公开资料（Wikipedia 条目未取到）；油量**未核实** |
| 炮舰机 7011、C-130 炮舰机 7065、C-130 运输机 7066 | C-130J 机体 | 70 t → 54.6 t | 34,274 + 6,700 US gal（25,360 L）×0.8；AC-130J 的炮与任务设备没有公开重量，不计（武器挂载另算） | Wikipedia C-130J；内油量**待复核** |
| 敌方战斗机 7020 | Su-27SK | 16 t → 25.8 t | 16,380 + 9,400 | Wikipedia Su-27 |
| 原初者战斗机 7030（翼展 19.5 m，机身框约同 F-16） | F-16C Block 50 | 12 t → 11.8 t | 8,573 + 3,200 | Wikipedia F-16 |
| X-Morph 喷气战斗机 7051（5.7 × 1.6 × 7.7 m） | Folland Gnat F.1 | 5 t → 2.8 t | 2,177 + 800 L ×0.8 | Wikipedia Folland Gnat |
| X-Morph 空中炮艇 7052（4.8 × 2.8 × 9.2 m） | Folland Gnat F.1 | 6 t → 2.8 t | 同上 | 同上 |
| X-Morph 重型突击炮艇 7053（12.3 × 4.0 × 14.5 m） | Su-25 | 15 t → 12.4 t | 9,800 + 3,200 L ×0.8 | Wikipedia Su-25 |
| X-Morph 重型轰炸机 7054（20.1 × 2.8 × 15.0 m） | A-6E | 30 t → 19.3 t | 12,093 + 9,030 L ×0.8 | Wikipedia A-6 Intruder |
| 天顶截击机 7070（TU-1600，40.0 × 9.7 × 61.8 m） | Tu-160（对照 B-1B 207 t） | 20 t（借 7003）→ 240 t | 110,000 + 130,000 | Wikipedia Tu-160；B-1B 87,090 + 120,326（USAF） |
| 沙扎比 7401、无畏舰 7601 | 不是飞机 | 71.2 t / 1e8 不变 | 设定值 / 超级航母排水量 | — |

影响（离线算，原版挂载满挂时的质量比，推力与过载按它除）：

- 战机：攻击 1.244 → 1.249、战斗 1.064 → 1.053、截击 1.140 → 1.103、多用途 1.173 → 1.147，无畏舰截击机 1.215 → 1.103：手感几乎不变。
- 变重的：X-Morph 战斗机 1.19 → 1.34、炮艇 1.22 → 1.46、重型炮艇 1.28 → 1.33、轰炸机 1.19 → 1.29，无人机 1.12 → 1.38：满挂时推力 / 过载
  少约 11–19%，打空后恢复；不算剧烈，没改飞行代码。天顶截击机 1.23 → 1.02，C-130 1.08 → 1.10，空母约 1.003。
- 撞击伤害与质量成正比：空母 / Helicarrier ×2.7、天顶截击机 ×12、X-Morph 战斗机 ×0.56 等。
- 载重上限（40%）：攻击 8800 → 8640、战斗 6400 → 7720、截击 8000 → 10880、多用途 7200 → 8480 kg。**攻击机（含玩家攻击机）下降 160 kg**：
  已保存的 8640–8800 kg 攻击机挂载（如 38 枚 Mk 82 = 8740 kg）会按超重整条作废、按原版挂载出动并在日志点名；其余机体只放宽。
- 玩家旋翼升力（`hover_lift.h PlayerLift`，按净重的 −1/3 次方）：空母直接按 327.2 t 算会从 10.57 掉到 7.57 m/s²，刹车 1 s 时仍以 0.7 m/s
  下沉（`hover_lift_check` 失败），重现 2026-10-06「空母…完全没办法正常开」。所以把升力满额的基准质量 `kFullLiftMass` 随这次重估同比放大
  （10 t × 327.2 / 120 = 27.3 t），空母 / Helicarrier 保持 10.57、无人机仍是满额；`hover_lift_check` 改用真实的 327.2 t。
- 原版直升机的净重（`store_burden.h` `kHeliMass506` 5.5 t / `409` 6.8 t / `410` 15 t）这次没动。
- 需实机确认：空母 / Helicarrier 玩家悬停爬升是否与之前一致；X-Morph 机与无人机满挂的手感；天顶截击机撞击与满挂手感；各机撞击伤害量级。
