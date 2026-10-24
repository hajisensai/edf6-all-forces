# 2026-10-21 反馈：BMPT / 火神炮没有第一人称；进瞄具时准星没隐藏

用户原话：

> bmpt和火神炮缺少第一人称……进入瞄具状态的时候没隐藏准星

（同一句里「模型也不对」由另一个任务处理，这里不改两车的外形几何。）

## 1. BMPT / M163 没有第一人称

### 根因（已验证）

载具第一人称的眼点只有一个来源：模型里的标记骨骼 `vc_eye_<席位>`（`src/fp_vehicle.cpp` `VehicleEye`）。找不到它，`firstperson.cpp` `VehicleYields` 返回「no cockpit in this seat」，镜头留在原版第三人称。标记由安装器把座舱嫁接进模型时加上（`pylib/cockpits.py` `graft` / `graft_files`），条件是：

1. `pylib/cockpit_fits*.py` 的 `FITS` 按模型文件名（stem）给这个席位配了座舱；
2. 生成器的 `build()` 最后调用 `cockpits.graft_files`；
3. `pylib/buildcache.py` 的 `COCKPIT_GROUPS` 列出这组资源（座舱改动才会触发重建）。

BMPT（`EDF6VC_BMPT.MRAB`）和 M163（`EDF6VC_VULCAN.MRAB`）三条都没有：

- `pylib/cockpit_fits_plugin.py` 的 `FITS` 没有这两个 stem（插件其它地面车 `EDF6VC_SEDAN` / `KATYUSHA` / `ARTILLERY` / `DRILL` / `SIDECAR` 都有）；
- `tools/make_tracked_combat.py` 的 `build()` 只做 `check()` 就返回，从不嫁接（对照 `make_artillery.py:370` / `make_katyusha.py` 等都以 `return cockpits.graft_files(game, files)` 结尾）；
- `COCKPIT_GROUPS` 没有 `vulcan` / `bmpt`。

实测证据：游戏目录里已安装的 `Mods/OBJECT/EDF6VC_BMPT.MRAB`（40 根骨骼）和 `EDF6VC_VULCAN.MRAB`（41 根）没有任何 `vc_` 骨骼；同为 V603 类的 `EDF6VC_ARTILLERY.MRAB` 有 175 根，含 `vc_eye_0` 与 `vc_i_*`。日志里对应「FP not on camera …: no cockpit in this seat」。

### 修复

按统一管线接上，不加逐车特例：

- `tools/make_tracked_combat.py`：拆成 `authored()`（原 `build()`：生成并 `check()` 作者几何的全部文件）和 `build()` = `cockpits.graft_files(game, authored(...))`，与其它生成器一致。几何契约测试与只读武器的 `gen_spawn_catalog.py` 用 `authored()`。
- `pylib/cockpit_fits_plugin.py`：两车都是 Kepler 同类（`Vehicle603_Flak`），只有 0 号位，驾驶员兼射手（驾驶位 = 炮手位）：
  - BMPT：Terminator 的武器模块是无人炮塔，乘员在车体里通过瞄准镜看。0 号位用遥控武器站（`remote_display`，与 Blacker / Titan 的无人副炮同一套），眼点在炮手瞄准镜镜头前 `(-0.38, 2.43, 0.56)`，挂炮塔偏航骨骼 `cannon_main`。不加任何网格，模型外观不变。
  - M163：开顶炮塔，操作手坐在 M61 右侧（篮筐座椅 x = −0.37）。0 号位 `open_rider`（只加一块手持状态平板），眼点在护栏上方 `(-0.30, 2.78, -0.25)`，挂 `cannon_main`，随炮塔转。试过 `gun_station`：它的护盾板从车外可见、改变外形且挡住视野，弃用。
- `pylib/buildcache.py`：`COCKPIT_GROUPS` 加 `vulcan`、`bmpt`。

离线预览（`python tools/cockpit_preview.py EDF6VC_BMPT --generator make_bmpt`、`EDF6VC_VULCAN --generator make_vulcan`）：BMPT 视角在双 30 mm 炮之间；M163 左侧是 M61 六管、正前方护栏、右侧瞄具头、下方状态平板；车外视图无座舱零件穿出。

注意：另一个任务在改两车模型。外形变了，这两行眼点要按新几何重新量（`tools/cockpit_preview.py`）。

2026-10-24（随 #233 战雷模型合入）已重新量：BMPT `(0.445, 2.06, 0.17)`，炮手瞄准镜窗（模型部件 `cannon_main.wt_optic_glass_c`，武器模块左侧）正前方、遮光罩前沿之外，双炮在右上方；M163 `(0.05, 2.95, -0.65)`，M61 后方炮塔井里的座椅正上方，头高过机匣，六管炮正前方、雷达罩在右手边。`tests/test_wt_tracked.py` `test_grafted_eyes_sit_on_the_war_thunder_geometry` 守住这两个位置。缺 `models/wt_*` 退回自建几何时眼点不是按那套几何量的。

## 2. 进瞄具时原版准星没隐藏

### 根因（代码路径已核对，未实机）

- 进入车载瞄具（有 `vc_optic_` 的席位放大，或第一人称 1 倍的炮手瞄准镜）时，`firstperson.cpp` `VehicleYields` 返回「the gunner's sight」，第一人称把镜头让给 `sightzoom.cpp`；第一人称不在了，`FirstPersonHidesStockHud` 为假，`map.cpp` `StockHud` 把原版 HUD 开关放回，原版准星 `HudPlayer_CrossHair`（第 3 槽 `0x8039F0`）照常在屏幕中心画腰射圈。
- 同时 `hud.cpp` `HudDraw` 画瞄具视场（`ScopeShade`）和武器分划（`ScopeArm`）。两套准星叠在一起。
- 原版准星的绘制已经被 `infantryscope.cpp` `CrossDrawHook` 挂着，但它只认步兵开镜（`InfantryScopeNow`），车载瞄具从没接进来（`docs/hud-re.md` §7.4 原文：「原版准星（屏幕中心的图标）没有动」）。

对所有有瞄具的载具同样成立：403 / 404 / 505 / 601 等带 `vc_optic_` 的席位放大时都走这条路（日志 17:55 Blacker `optic mounted` + `3x by the key` + 「FP not on camera …: the gunner's sight」）。BMPT / M163 没有 `vc_optic_`，瞄具不会放大；它们的第一人称（修复 1 之后）是座舱 / 显示器画面，屏幕座舱让 `FirstPersonHidesStockHud` 把原版 HUD 连准星一起关掉。

### 修复

- `src/sightzoom.h` `ScopeField`：瞄具视场是否盖在画面上的纯判据（有视场的火控面，且放大或已装镜头的 1 倍瞄具）。`sightzoom.cpp` `SightZoomScope` 从同一份座位快照给出视场、看瞄具的士兵和倍率；`hud.cpp` 改用它（原来是三次各自取快照的 `SightZoomNow` / `SightZoomMounted` / `SightZoomView`，结果等价）。
- `src/scope_hold.h`：`HudDraw` 每次记下「为哪名士兵画了视场、何时」（不画就记空）；`HudScopeHoldsCrosshair` 在 250 ms 内、且是同一名士兵时返回真。
- `infantryscope.cpp` `CrossDrawHook`：步兵开镜或车载瞄具视场画给了这名士兵时，原版准星不画（同原来的早退方式）。只在插件真的画了视场时才藏：关了载具 HUD、地图、暂停都不会出现「两套都没有」。

## 联机

两项都只改本机画面：

- 第一人称：眼点是本机模型里的标记骨骼，镜头只在本机（README「联机：只改本机看到的画面」）。嫁接只加无顶点的标记骨骼和座舱网格；物理、ragdoll 绑定、CAS 都按骨骼名绑定（`tracked_combat_model.py` 文档），与已在用的同为 V603 类的自行榴弹炮、原版 Kepler 的嫁接方式相同。同步数据（载具状态、炮塔输入、开火）不变，不新增网络字段，不需要能力位。房间里有人装的是旧模型，只是他自己没有第一人称。
- 准星：按 `+0x580`（准星所属士兵）只藏本地看瞄具那名士兵的；分屏 2P、远端玩家的准星不受影响。

## 验证

- `tests/cockpit_coverage_guard.py`（ctest `cockpit_coverage_guard`）：`GROUND_VEHICLES` 每辆车的模型 stem 都有 0 号位座舱、生成器会嫁接、`COCKPIT_GROUPS` 有它。修复前对 vulcan / bmpt 各报 4 项失败。
- `tests/test_tracked_combat.py` `test_build_grafts_each_seats_eye`：`build()` 的模型对每个 fit 都有 `vc_eye_<seat>`，挂在 fit 的骨骼上、位置等于眼点；其余文件与 `authored()` 逐字节相同。
- `tests/sightzoom_test.cpp`：`SightZoomScope` 在 1 倍第三人称、放大、地图、换人、第一人称 1 倍有镜头 / 无镜头、俯瞰视角下的结果。
- `tests/scope_crosshair_test.cpp`（ctest `scope_crosshair`）：生产 `CrossDrawHook` 对看瞄具的士兵不画、对其他士兵照画、视场消失后恢复、步兵开镜行为不变；`scope_hold.h` 的新鲜度与归属。
- `tools/hud_view.cpp`（ctest `hud_layout`）：各瞄具样式放大场景里 `HudDraw` 之后 `HudScopeHoldsCrosshair` 对该士兵为真、1 倍为假、对其他士兵为假。

未实机：两车第一人称的实际画面、瞄具下准星确实消失（需要游戏内看）。

## 3. 追加：中键自由视角（同日用户反馈）

用户原话：

> 鼠标中键移动视角的时候，底下的挂载会变化，应该挂载顺序不动才对。还有鼠标中键的时候应该是绕着车转，而不是车后面原地转。可以参考战雷。还有战雷一样的那个圆的鼠标指示点怎么没了

### 3a. 挂载条随视角重排

根因（已验证，代码路径）：挂载条左右顺序由 `hud_cue.h` `SideOrder` 按每件武器的 `side` 分组，`side` 来自 `vhud.cpp` `SideOf`：炮口离车体中线的距离投影在**相机视线**的左方向上（`a.side=SideOf(w,r.pos,r.lookOk ? r.look : r.hull)`，`r.look` 是 `CameraRay` 的画面中心射线）。按住中键转相机，左右随之翻转，挂载条就重排。

修复：`SideOf` 改投影到**武器自己的炮膛方向**的左方（炮膛竖直时退回车头方向）。炮膛随炮塔 / 躯干 / 手臂转，不随观察转；原来要解决的 Nix 左右手问题（2026-10-12）照样成立。测试 `tests/fixed_weapon_sight_test.cpp`：同一门炮在三种「视线」下 `side` 相同，且等于按炮膛算的值。

### 3b. 中键在车后原地转 / 3c. 战雷式圆形鼠标指示点不见了

根因（已验证，git 与代码路径）：同一个原因。

- 炮塔镜头 `turretcam.cpp`（2026-10-06 `4e5f4503`）本来就有战雷式自由视角：按住 FreeLookKey，视角在镜头支架（rig）上绕车转，炮塔保持指向按下时的点，HUD `TurretMark` 用青色圆圈标住那个点（`r.aimValid && (r.decoupled || r.freeLook)`）。
- 2026-10-16 `450a1346` 加了通用的 `vehicle_look.cpp`（所有座位按住观察键扭头）。它在 `crew.cpp` `InputHook` 里先于炮塔镜头运行，把座位的视角输入清零，再在 `VehicleLookCamera` 里以**相机自己的位置**为枢轴转相机（`pivot={0,0,0}`）：第三人称下就是在车后原地转。
- 2026-10-19 `c12b9a88` 的 `turretcam.cpp` `ExternalView` 把 `VehicleLookActive(v)` 算作外部视角：中键一按，炮塔镜头 `SuspendView`，发布空的读数（`Publish(TurretCamReadout{})`），`TurretMark` 的圆圈随之消失。

修复：

- `turretcam.cpp` `TurretCamFreeLook(vehicle, seat)`：这个座位的自由视角归炮塔镜头（它放置相机的炮塔 0 号位、第三人称、没开地图、没开第一人称座舱、瞄具没起）。`vehicle_look.cpp` 在拿锁之前问它，是则这一帧让出：不清零摇杆、不转相机、`VehicleLookActive` 为假，炮塔镜头的绕车自由视角和青色圆圈回来。第一人称（座舱扭头）与瞄具里的观察不变。
- 炮塔镜头不管的车（卡车、直升机、摩托等）第三人称下，`VehicleLookCamera(…, orbit=true)` 改为绕车转：枢轴取视线上与车体原点等深的点（`vehicle_look.h` `OrbitPivot`），眼点保持到枢轴的距离、视线仍指向它；第一人称仍在眼点原地扭头。车在眼点后方时退回原地转。

测试：`tests/vehicle_look_test.cpp`（第三人称绕枢轴、保持距离、仍看着枢轴、沿弧移动；第一人称原地；炮塔镜头的座位让出：不取摇杆、不转相机、不压炮塔镜头）。

与瞄具隐藏准星不冲突：瞄具起来时 `TurretCamFreeLook` 为假、炮塔镜头本就让位给瞄具（`ExternalView` 的 `SightZoomMounted`），`TurretMark` 在 `mountedOptic` 时也不画；瞄具里只有瞄具分划，原版准星按第 2 节藏起。

未实机：绕车转时眼点贴地 / 穿墙（炮塔镜头的 rig 有原生防穿地，`vehicle_look` 的绕车没有）；圆圈在中键自由视角中的实际显示。
