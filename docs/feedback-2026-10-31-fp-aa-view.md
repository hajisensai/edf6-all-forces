# 2026-10-31 反馈：第一人称四管防空车视角不随炮管上抬

用户原话：

> 2.第一人称四管防空车视角不随炮管往上抬

（四管防空车 = 四联火神 `EDF6VC_QUADVULCAN`；单管的 M163 `EDF6VC_VULCAN` 用的是同一个座位，同样有这个问题。）

## 根因（代码路径已核对，未实机）

载具第一人称的镜头 = 模型里标记骨骼 `vc_eye_<席位>` 的世界矩阵（`src/fp_vehicle.cpp VehicleEye` → `src/firstperson.cpp Ride`）。
只有被判成「炮上摄像头」的座位，朝向才改成沿炮口（`src/fp_view.h GunCameraFrame`，`src/vhud.cpp SeatGunLine` 读真实炮口）。
10-25 的判据 `GunCameraSeat` 只看座位有没有武器站显示器（`vc_scr_<席位>_sensor_` 标记），而这个标记只有 `remote_display` 套件有
（`pylib/cockpit_kits.py`）。

M163 和四联火神的 0 号位是开顶炮塔里站在炮后的操作手，用 `open_rider` 套件（`pylib/cockpit_fits_plugin.py`，
`pylib/cockpit_kits_ground.py open_rider`：只有一块手持 status 平板），没有 sensor 屏，于是走「座舱骨骼朝向」：眼点挂在炮塔偏航骨骼
`cannon_main` 上，镜头跟着炮塔转但永远水平，炮抬起来画面不动。10-25 有意保留「有实体座舱的炮手座不变」（`tools/fp_view_check.cpp`
`!GunCameraSeat(false,false,1)`），开放炮位被一起归进了「座舱座」。

## 修复（按套件声明，不按车名）

一个座位的视线跟炮还是跟座舱，由它的套件决定（`pylib/cockpits.py Kit.gaze`）：

| 套件 | gaze | 理由 |
|---|---|---|
| `remote_display`（武器站 / 无人炮塔） | 跟炮 | 画面是炮上摄像头 |
| `open_rider`（开放炮位、车斗乘员） | 跟炮 | 身边没有随炮转的座舱，人站在炮后，看炮指的地方 |
| `tank_gunner` 等封闭座舱 | 跟座舱 | 看的是座舱；沿炮看靠瞄具 `vc_optic_`（`sightzoom.cpp`，不变） |

- 安装器（`cockpits.graft`）给 gaze 套件的座位在眼点旁写一根零顶点标记骨 `vc_gaze_<席位>`（和 `vc_eye_` 同一骨骼、同一矩阵），
  `GRAFTED_BONES` 里加了这个前缀（卸载 / 重新嫁接时一起去掉）。插件车（生成器经 `graft_files`）和原版车（`vehicle_optics`）都走这里，
  某个 gaze 套件的座位不可能漏标。
- 运行时（`fp_vehicle.cpp VehicleEye` 读 `vc_gaze_<席位>` → `RideView::gaze`）：`fp_view.h GunCameraSeat(sensor 屏 || gaze 标记,
  不是自己飞的飞行器的 0 号位)`；`GunCameraWay` 决定朝向：
  - 座位的火控那门炮能偏航 + 俯仰（`SeatGunLine` 给出炮口）：沿炮口。眼点不动，视线随炮转，炮停在仰 / 俯角极限时画面也停在那里；
  - 炮不能转（虚拟瞄准）：有武器站显示器的沿原版镜头这一帧的前向（10-25 原样）；**只有 gaze 标记的开放座位保持骨骼朝向**。
    这一点和最初的设想不同：原来的 `Ride` 在 `SeatGunLine` 失败时一律退到原版镜头方向，若只把判据改成「sensor || gaze」，
    车斗乘员（轻卡 1–4 号位）、X-Morph 运输卡车驾驶员这类没有炮的 `open_rider` 座位会从骨骼朝向变成原版镜头方向，行为就变了。
    所以「没炮 → 不变」要单独写成规则，而不是指望 `SeatGunLine` 返回 false。
- 为什么用标记骨而不是运行时直接认套件：运行时只能从模型里的标记知道座位是什么套件，`open_rider` 唯一可见的标记是 status 屏，
  与 `troop_bay` 等座舱座位相同，无从区分；靠车名判断又违背「按套件声明」。`remote_display` 的座位也写了 `vc_gaze_`（声明一致），
  运行时仍认 sensor 屏（不靠 `graft` 的模型，如无畏舰炮塔，判据不变）。

`open_rider` 现有座位：M163 0 号位、四联火神 0 号位（有能转的炮 → 沿炮口）；轻卡 / BGP 轻卡 1–4 号位、X-Morph 运输卡车 0 号位
（按现有数据它们没有座位武器 → `PayloadSightPicked` 为空 → `SeatGunLine` false → 骨骼朝向，不变；未在游戏内核对原版轻卡乘员位
确实无座位武器。即便有，也只有能偏航 + 俯仰的炮才会让镜头沿炮口，这正是规则本意）。`gun_station` 套件目前没有座位用，未改。

## 测试

- `tools/fp_view_check.cpp GunCamera`：`GunCameraSeat` / `GunCameraWay` 的取舍——gaze 开放座位是炮摄像头；封闭座舱（无 sensor、
  无 gaze）不是；飞行器驾驶座不是；有炮口沿炮口；武器站无炮口沿原版镜头；开放座位无炮口保持骨骼。
- `tests/fp_ride_lifecycle_test.cpp`：生产 `Ride` 的三种新情况——有 `vc_gaze` 的开放座位沿炮口（停在 20° 极限，眼点不动）；
  封闭座舱即使炮能转也保持骨骼朝向；开放座位没有能转的炮保持骨骼朝向（不取原版镜头方向）。
- `tests/cockpit_build_test.py`：seat 用到的套件里 gaze 的恰好是 `remote_display`、`open_rider`；M163 / 四联火神是 gaze 座位；
  `vc_gaze_` 是嫁接骨；整车构建的 Blacker 每个 gaze 座位都有挂在 fit 骨骼下的 `vc_gaze_<席位>`，非 gaze 座位没有；骨骼预算计入它。
- `tests/gun_camera_sweep_guard.py`：全仰角扫描从 `remote_display` 扩到所有 gaze 套件座位，M163 / 四联火神的眼点沿炮口的视线
  -5°..60° 全程不碰车体（35/35 通过）。

## 未实机（需要游戏内确认）

- 四联火神和 M163 抬炮到最大仰角时画面跟着炮抬，到极限停住；俯角同样；
- 抬炮时手持 status 平板留在原位（挂在炮塔上）滑出画面下沿，属预期；
- 轻卡车斗乘员、X-Morph 运输卡车驾驶员第一人称与改动前相同；
- 中键自由观察照旧叠在沿炮口的朝向上。
