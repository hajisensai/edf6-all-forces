# 巴尔加落点提示（BargaAimMarker）

> 用户原话：「巴尔加加个落点提示？」——全周天驾驶舱正前方看不到手臂，出拳时也看不到拳头，「不知道攻击打到哪里」（[feature-panoramic-cockpit.md](feature-panoramic-cockpit.md)「已实机」一节）。

玩家自己驾驶 501_FortressRobo（V515 复古巴尔加各色、V605 巴尔加加农）时，HUD 在世界里画三样东西：

| 时机 | 画什么 |
| --- | --- |
| 待机（没有命中判定亮着） | 两只拳头够得到的地面弧线：这一下连招能出的一侧亮绿、加粗，另一侧暗绿（画的是它的起手拳） |
| 命中判定亮着（出拳 / 出脚中） | 每个判定球画一个圈（大小与球在屏幕上的大小一致；碰到地面时变红）；球正下方的地面打黄色十字；扫过敌人锁定点时在敌人身上套绿色目标圈 |
| V605 `ground_charge` / `attack_cannon` 中、连射进行中 | 剩余炮弹的落点连线（蓄力时琥珀色、连射中红色），每 4 发和最后一发画 50 m（AmmoExplosion）爆炸圈，下一发打黄色十字；连射中只画还没打出去的 |

- 只看本机：只在本机玩家坐 0 号座位（驾驶位）时读，房主和客人一样，不发联机消息。
- 开关：`EDF6VehicleCrew.ini` 的 `BargaAimMarker=1`（默认开，0 关）。`CONFIG hud ... bargaAimMarker=%d` 一行记下读到的值。

## 代码

- `src/barga_aim.h`：纯函数（离线检查 `tools/barga_aim_check.cpp`）。
- `src/barga_aim.cpp`：游戏线程读原生状态。入口是 `crew.cpp` InputHook 里 501 的每帧输入（0x60AEC0）上的 `Guarded(kStepBargaAim,&BargaAimFrame,v)`，它在 `StockHudFrame` 之后。
- 读到的结果用 `stepfresh` 发布；`hud.cpp` `HudPublish` 用 `PlayerBargaAim` 取，`BargaAimMarks` 负责画。
- 换任务时由 `mission.cpp` 调 `ResetBargaAim` 清掉。
- 攻击表 `src/barga_reach.inc` 由 `tools/gen_barga_reach.py` 在模型动画上离线采样生成，`--check` 在 `tools/check.py` 和 CI 里校验。

## 原生状态（静态逆向，见各常量注释）

- **连招**（0x60F570 / 0x6058C0 / 0x605C90）：
  - 车体字段：`+0x2390` 攻击条目数组（每条 0x40，`+0x30` 前置数，`+0x20` 前置下标数组 u64）、`+0x23A0` 条目数、`+0x23A8` 可接受输入（byte）、`+0x23AC` 当前攻击（−1 待机）、`+0x23BC & 1` 标志。
  - 某条能出的条件：标志是 flags 的子集；前置数为 0 时只在待机能出，否则当前攻击须在前置里；还要求可接受输入。
  - 变体识别：16 条的前置表逐条与生成表比对，不认识就不画，并记一行日志。
- **命中判定**（0x6C8A50 / 0x6BB420）：
  - `+0x2350` 是近战组件，组件 `+0x10` 是 map 的头节点。
  - 节点字段：名字 wstring 在 `+0x20`；组的点数组在 `+0x48`、点数在 `+0x58`，每点 0x30；`+0x138` 表示亮着。
  - 每个点：`[+0]` 记录（偏移在记录 `+*(int*)(记录+0xC)` 处，半径在记录 `+0x10`），`[+8]` 骨骼（世界行矩阵在 `+0xB0`），`+0x10` 上次判定位置。
  - 球心 = 偏移 (x, y, z, w) × 骨骼四行。
  - 组名（右腕 / 左腕 / 右足 / 左足 / 胴体）对应到肢体。
- **加农**（Weapon_VehicleSwingShoot，vtable `0x17E6320`；0x6B4100 / 0x6B4080 / 0x6B4190）：
  - 连射开始时把 `*(w+0x120)+0x80` 冻结到 `w+0x1670`。
  - 每发的仰角偏移 a(t) = Σ begin + (end − begin)·t^pow（`+0x1680` / `+0x1690` / `+0x16A0`；V605 是 0.75 / 0 / 0.25）。
  - t = 1 − 剩余 `+0xE18` / FireBurstCount `+0x370`。
  - 正角度让炮口向下（`barga_aim.h` `kSweepSign`，要翻转只改这一个常数）。
  - 落点按 `RoundLands`（与 HUD 其它弹着点同一套弹道和地图射线）从 `MeanMuzzle` 的位置算出。

## 已离线验证

- `barga_aim_check`：158 项全过，`build.cmd` 编译零警告。覆盖的内容：
  - 扫射角公式与端点、方向（仰角减扫射角、保持航向、零向量取 +Z）。
  - 连射进度、剩余弹采样（下一发与最后一发必在、按发射顺序）。
  - 判定球心。
  - 两套生成表上的连招可出规则与变体匹配（错一条就不匹配）。
  - 两侧拳头的选择、地面弧线几何，以及表的合理性（够及点在脚前、脚上，左拳在 +X）。
- `gen_barga_reach.py --check`：`current`。

## 未验证（等实机，Debug=1 日志每 500 ms 一组）

- `BARGA v=... <SGO> current=.. accept=.. flags=.. lit=[组名] spheres=.. arcs L=..(*) R=..(*) fwd=(..)`：核对两件事。一是每个攻击下标亮的是哪些组名；二是带 `*` 的一侧与按键（RB / LB）对得上。
- `BARGA sphere i limb=.. r=.. c=(..) hull=(..) moved=..`：
  - `hull` 是球心在车体系里的坐标，应接近表里的够及点（尤其 +X 是否为左手侧；这用来验证 `gen_barga_reach.py` 的 ORDER=0）。
  - `moved` 是本帧与上次判定位置的距离（判断判定是扫掠还是重叠）。
- `BARGA cannon on=.. burst=.. left=.. count=.. t=.. frozen=(..) src=..(vehicle) native=(..) predicted=(..) ...`：核对三件事。
  - `native`（炮口 +0xD0）和 `predicted` 是否一致。如果只有 y 的符号相反，就翻转 `kSweepSign`。
  - `src` 后面有没有 `(vehicle)`，即 `w+0x120` 是不是车体本身。
  - `first` / `last` 落点是否合理。
- `BARGA v=... variant none`：表不认识这台车的攻击列表。
- 原版 HUD（`StockHudFrame`，501 现在有输入钩子）是否已经给 V605 的加农画了自己的弹着点，会不会和这里的重复。

## 实机怎么测（一次运行）

- V515：`python tests/autopilot/drive.py run range:4 --ini TestBoardVehicle=501_FortressRobo --ini FirstPersonStart=1 --ini Debug=1`，约 20 s 登车。登车后按住 `pad_rb` / `pad_lb` 出拳（`--key`），中途截图。
- V605：原版任务 M118（离线列表第 135 项，`drive.py` 行号 134，任务里唯一的 501 类是 `v605_barga_cannon_mission`）：`python tests/autopilot/drive.py run 134 --ini TestBoardVehicle=501_FortressRobo --ini Debug=1`。测试场 `range:1..7` 的预设里没有 605。
