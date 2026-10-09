# 2026-10-09 实机反馈（ground 组）：炮塔随车体、钻头战车贴图 / 发射伤害 / 自转 / 弹数

EDF.dll `678CCB46`。全部是静态分析 + 离线检查，**没有启动游戏、没有安装到游戏目录**；下文「未实测」一节如实列出还要进游戏看的部分。

## 1. 「炮塔不会随着车体旋转而旋转」

### 根因（两处都把炮塔锁在世界航向上）

- `src/turretcam.cpp`（战争雷霆式镜头，`DecoupledTurretCam=1` 默认）：镜头航向 `s.yaw` 是世界坐标的，只在视角开始时取一次
  （HEAD `turretcam.cpp:415`）、之后只按鼠标 / 右摇杆转（HEAD `:443`），车体转向从不进入它；炮塔每帧被 `Steer` 转向屏幕中心
  那一点，所以车体一转，炮塔就反向转、守住世界里的那一点。玩家开的**所有**炮塔车都这样：Blacker、Titan、单座坦克、E551、
  Kepler、Grape、喀秋莎、自行榴弹炮、钻头战车、Begaruta / Nix / Proteus 等。
- `src/stab.h Step`（炮管稳定器，`GunStabilizer=1` 默认）：参考线存在世界里，车体的偏航被当成扰动完整抵消（HEAD `stab.h:137` 起，
  设计本意就是「双向（高低 + 方向）稳定」，`docs/camera-re.md` §7）。即使 `DecoupledTurretCam=0`（原版镜头挂在炮塔上），
  有稳定器的主炮和炮手座（Blacker / Titan / 601 / E551 / Kepler / Grape 主炮，坦克和机甲的炮手座，包括 NPC 乘坐的）也不随车体转。

### 判断与理由

战争雷霆的鼠标瞄准确实让炮塔守住世界里的瞄准点，这正是用户报告为 bug 的行为；原版 EDF 里炮塔（和挂在炮塔上的镜头）随车体转。
所以按用户的期望修，同时保留 WT 式手感里有价值的部分：鼠标先转镜头、炮塔按原版转速跟过去、稳定器抵消颠簸 / 俯仰 / 侧倾。
**改的只是「航向」这一个自由度**：没有新的瞄准输入时，车体的航向变化同时带走镜头航向和稳定器的参考线，炮塔相对车体不动；
高低方向和颠簸照旧稳定（在世界里守住仰角）。

### 修法

- `src/stab.h`：`Hold::follow`；`HeadingChange` / `Carry` / `Follow`：参考线按「上一步看到炮的坐标系 → 这一步的」的航向变化
  （绕世界 +y、按车头水平投影算）一起转，指令坐标系 `last` 同步转，保证指令转参考线的结果与不转时完全一致；`Held`（给炮塔镜头和
  EDF6AutoTurret 的前瞻）用同一个转动，否则镜头会把航向当成「车体那部分」扣掉、和跟随打架（变异实测见下）。
- `src/stab.cpp`：每步前 `e.hold.follow=Cfg().turretFollowsHull`。
- `src/turretcam.cpp`：`HullTurn` 每个瞄准步测车体航向变化；`Follow` 把它加到镜头航向、观察键的回转方向，并把观察时炮塔守着的点、
  这一帧屏幕中心的瞄准点绕车体原点转同样的角度（瞄准点取自上一帧的镜头，不转的话炮塔会落后车体一帧的转角）。
  开着 `NixTorsoTwist` 的 Nix 不跟随（用户自己要求过「A/D 只转腿、躯干守世界朝向」，它的「车体」是腿）。
- `src/turretcam.h`：纯函数 `TurnAbout`、`HeadingOf`（离线可测）。
- ini `TurretFollowsHull`（默认 1；0 = 以前的世界航向模式）：`src/crew.h`、`src/plugin.cpp`、`EDF6VehicleCrew.ini`；README 两处各一句。

### 测试

- `tools/stab_check.cpp FollowHull`：30°/s 蛇行 + 颠簸、无输入：炮相对车体的偏航最多偏 0.349°（关跟随时 19.4°，即旧行为），
  世界仰角最多偏 0.047°（颠簸照样稳住）；纯转向时 `Held` 的前瞻不含车体部分；炮塔镜头 + 稳定器都跟随时炮膛守在被带走的瞄准点上
  （最坏 0.048°，只有镜头 0.53°）；`TurnAbout` 只改航向。原有 bumps / camera / outrun / gunner / stops / 读回检查都在 `follow=false`
  下照旧通过。
- 变异实测：去掉 `Step` 里的 `Follow` → 3 条失败（不随车体转、镜头点追不上）；去掉 `Held` 里的 `Follow` → 3 条失败（镜头与稳定器打架、
  前瞻多出车体部分）。
- `tests/stab_native_readback_test.cpp`（实跑原版读回 / 轴步进 / 骨骼映射）：原有世界航向契约在 `TurretFollowsHull=0` 下照旧通过；
  新增跟随模式：同帧物理跟踪差 + 车体偏航 + 玩家输入，稳定器不补偏航、读回照常调和、玩家指令与原版速度保留。

## 2. 「钻头战车的车体少了贴图」

### 根因

`pylib/drill_model.py`（HEAD `:55-56`）只给 `MI_Tank_C`（车体上部和钻头）配了 `Tank_C_BC.png`，`MI_Tank_B_CS`（下半个车体、负重轮、
履带，y 0~1.85 m，车体 10.6 万顶点里的 7.3 万）被当成「贴图没随模型提供」画成 4×4 的纯色深钢灰（`SOLID`）。读已安装的
`Mods/OBJECT/EDF6VC_DRILL.MRAB` 确认：`edf6vc_drill_b` 的 albedo 就是那张纯色图。

但 `Tank_C_BC.png` 本身就是两种材质共用的图集：把 `MI_Tank_B_CS` 的 UV 画到图上，负重轮、诱导轮、履带条、十字件、左侧大块侧板
都和图上 `MI_Tank_C` 从不使用的区域（左下角的轮子 / 履带行、左侧板）逐个对齐；按 256² 网格栅格化 UV：`MI_Tank_C` 覆盖图集 26.6%，
`MI_Tank_B_CS` 38.9%，两者合计 58.4%（两倍多）。打包没有问题（成员、.lod、顺序都对），问题在生成器的材质表。

### 修法

- `MATERIALS` 表：OBJ 材质 → （游戏材质名，albedo 源文件），两种材质都用 `Tank_C_BC.png`；`ALBEDO_TEX` 每个源文件只打包一次
  （档案大小不变，4.23 MB）。原版 Blacker 那 5 个没有网格画的材质改用独立的 `edf6vc_drill_steel.dds`（以前借用 `_b` 的纯色）。
- `drawn_albedo_problems` 并入 `check()`：每个有网格的材质必须是 OBJ 材质、albedo 必须是它的图集，纯色钢板会被拒绝。
- `texture_coverage`：上面那组覆盖率的离线计算，作为「同一图集」的证据进测试。

### 测试

- `tests/drill_model_rotation_test.py`：MATERIALS 每项都有贴图；有私有 OBJ 时覆盖率断言（B_CS > 0.3、合计 > 1.6 × C）；
  `--archive` 时 `drawn_albedo_problems` 为空，并把下半车体改回纯色钢板做负对照（必须报错）。
- 用新生成器离线生成（`python tools/make_drill.py <游戏目录> --out tmp/out`）通过 `check()`；同一检查对**当前已安装**的旧 MRAB 报
  `material edf6vc_drill_b: albedo edf6vc_drill_b.dds, want the atlas edf6vc_drill_c.dds`（就是用户看到的问题）。
- `pylib/model_view.py --color @tex` 渲染新旧两版对比：旧版下半车体纯深灰，新版是和上半车体同一套蓝灰涂装与轮子 / 履带颜色。

## 3. 钻头战车的发射

### (a) 「发射钻头的时候没伤害，回收的时候有」

**根因**：`src/drill.cpp SweepEnemy`（HEAD `:643`）飞行中只咬「身体（脚下 / 锁定点 / 中点）离钻头轴线 ≤ 半径 1.55 + 1 m」的敌人。
钻头从车上沿车头方向平飞，轴线离地 4.21 m；地面敌人的脚在地上、锁定点约 1 m 高，离轴线 3.2~4.2 m，**永远够不到**——这正是
2026-10-05 车头近战犯过的错误（`docs/drill-re.md` §5.5：围着高处轴线的圆柱碰不到在犁的敌人），飞行判定又用回了圆柱。
用户看到的「回收时有伤害」其实是钻头接住后车头近战探测恢复、转速还在最高，立刻咬了车前的敌人。

**修法**：飞行的够得着范围 = 车头接触箱随钻头带走：在钻头自己的坐标系（原点在根部、z 轴线、y 为车辆上方正交化）里
x ±2.4 m、y 从轴线下 4.21 m（发射时的离地高度，一直到地面）到轴线上 1.55 + 0.3 m、z 从根部到尖端前 1 m，敌人身体段
（脚下..锁定点，加 1 m 厚度）与箱相交就算；多个时取离轴线最近的。`BodyInBox` 与车头近战的 `SeeEnemy` 共用；钻头的坐标轴
`FlightRows` 与画飞行姿态的 `PoseFlight` 共用（原来各算一遍）。离车 7 m 内不咬的规则不变。

### (b) 「为什么钻头发射的时候还会受到左键的控制是否旋转」

**根因**：飞行中 `held` 被强制为真（HEAD `drill.cpp:785`），但转速是从发射那一刻的值按 `DrillSpinUpSec`（1.8 s）慢慢升上去的；
`Launch`（HEAD `:573` 附近）不动转速。没按住射击键就发射，钻头飞出去时几乎不转、整个去程（约 1.7 s）都在加速——看起来像
「按没按左键决定它转不转」。

**修法**：`Launch` 时转速直接置为最高（喷气驱动的自转），飞行中保持最高、与射击键无关；只有过热会让它减速（与扳机无关）。

### (c) 「钻头为什么有25的弹药」

**根因**：钻头这件武器 `EDF6VC_DRILL_BIT.SGO` 由 `tools/make_drill.py bit_sgo` 从原版 Blacker 炮 `V_505TANK_CANNON01.SGO` 改出，
`BIT`（HEAD `make_drill.py:69`）没改 `AmmoCount`，沿用了炮的 25 发弹匣（离线读生成的 SGO：`AmmoCount 25.0`），原版 HUD 和插件 HUD
都读武器的实时弹数（`+0xBE8`）显示 25。

**修法与依据**：钻头只有一个、可回收，所以弹数语义 = 发射-回收循环：`BIT` 加 `AmmoCount: 1.0`（`check()` 随 BIT 一起核对）；
插件 `ShowRound` 每帧按飞行状态写实时弹数：在车上 1、发射出去 0、接住回到 1（联机副本按它显示的飞行状态写，同一规则）。
写实时弹数与 `heli.cpp` / `emc.cpp` 已有做法相同。

### 测试

- 新增 `tests/drill_launch_test.cpp`（ctest `drill_launch`，直接执行生产 `DrillInput` / `DrillFrame`，假车辆内存 + 假手柄）：
  车上弹数 1；不按扳机发射后转速立刻最高、弹数 0；去程中每帧切换扳机，转速和每帧转角都不变；去程咬到前方 30 m 地面上的敌人
  （锁定点）；接住后弹数回到 1；离飞行线 6 m 的敌人不咬。
- 变异实测（各自单独回退）：去掉 `ShowRound` → 弹数断言失败；去掉 `Launch` 置转速 → 自转断言失败；`SweepEnemy` 换回旧圆柱判定
  → 去程咬敌断言失败。
- `drill_sync_test`（联机复制、回程转向、姿态写入）25 条照旧通过。

## 构建与测试结果

- `cmake --build build -j 4`：两个 DLL 链接成功（/W4 /WX 无警告）；`--target offline_checks` 成功。
- `ctest -j 3`：180/180 通过（含新增 `drill_launch`；`stab_native_readback` 用真实 EDF.dll 执行）。
- `stab_check` / `grape_turret_check` / `turret_cam_check` 全部 all passed。
- `python tools/selftest.py`：132/132 通过。
- `python tests/drill_model_rotation_test.py --archive tmp/out/OBJECT/EDF6VC_DRILL.MRAB`：通过。

## 未实测（需要进游戏看）

1. 炮塔跟随：开坦克原地转向 / 行进转弯时，镜头和炮塔是否一起随车体转、松开鼠标后炮塔相对车体不漂；颠簸路面炮管仰角是否仍稳；
   `TURRETCAM` 行 `view` 航向应随车体变化、`yaw` 轴角基本不变。屏幕中心瞄准点绕车体原点转是近似（镜头支点不完全在车体原点），
   转弯时炮塔可能残留零点几度的偏差，靠 `Steer` 每帧收敛，需实机确认观感。
2. `TurretFollowsHull=0` 应回到以前的世界航向行为（离线检查覆盖，未实机）。
3. 钻头战车下半车体的贴图是否正确对齐（离线只做了 UV / 图集覆盖分析和顶点采样渲染；不同区域有少量 UV 与图案并不完全吻合的地方，
   铁雨原作是否另有一张 `Tank_B` 贴图无法离线确认）。**需要重新运行安装器**生成新的 MRAB 和 BIT SGO，只换 DLL 不会变。
4. 发射中钻头掠过地面敌人时是否掉血、击杀降温是否计入；装药从敌人往钻头方向退 2 m 起爆，敌人在钻头正下方 3~4 m 时装药起点在
   敌人斜上方，是否会先撞到别的东西未验证。
5. 弹数显示：原版 HUD 是否显示 1 / 0；把实时弹数写成 0 时原版 505 的装填逻辑（`ReloadTime -1`）是否有副作用（静态未追到写入点）。
