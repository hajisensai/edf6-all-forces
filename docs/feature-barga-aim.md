# 巴尔加落点提示（BargaAimMarker）

> 用户原话：「巴尔加加个落点提示？」——全周天驾驶舱正前方看不到手臂，出拳时也看不到拳头，「不知道攻击打到哪里」（[feature-panoramic-cockpit.md](feature-panoramic-cockpit.md)「已实机」一节）。

玩家自己驾驶 501_FortressRobo（V515 复古巴尔加各色、V605 巴尔加加农）时，HUD 在世界里画三样东西：

| 时机 | 画什么 |
| --- | --- |
| 待机（没有命中判定亮着） | 两只拳头够得到的弧线，画在出拳的高度（表里够及点的 y，约 35～48 m）：这一下连招能出的一侧亮绿、加粗，另一侧暗绿（画的是它的起手拳） |
| 命中判定亮着（出拳 / 出脚中） | 每个判定球画一个圈（大小与球在屏幕上的大小一致；碰到地面时变红）；球正下方的地面打黄色十字；扫过敌人锁定点时在敌人身上套绿色目标圈 |
| V605 `ground_charge` / `attack_cannon` 中、连射进行中 | 剩余炮弹的落点连线（蓄力时琥珀色、连射中红色），每 4 发和最后一发画 50 m（AmmoExplosion）爆炸圈，下一发打黄色十字；连射中只画还没打出去的；打不到任何东西（射线在空中结束）的弹不画，连线在那里断开 |

- 只看本机：只在本机玩家坐 0 号座位（驾驶位）时读，房主和客人一样，不发联机消息。
- 开关：`EDF6VehicleCrew.ini` 的 `BargaAimMarker=1`（默认开，0 关）。`CONFIG hud ... bargaAimMarker=%d` 一行记下读到的值。
- 和原版载具 HUD（`src/vhud.cpp`）不重复：这里在画 V605 加农时（`BargaOwnsCannon`：同一把武器、开关开着、本帧结果新鲜），vhud 不再给这门炮画自己的弹着点；标记关掉或没在画时，vhud 照旧画。
- 为什么弧线不画在地面：全周天第一人称的眼睛在脚上约 41.3 m、前方约 6.3 m，视野竖直 ±22.5°（fovY = π/4，`docs/zoom-re.md`）；脚前 25～35 m 的地面在视线下方约 50°，整条弧都在屏幕外（2026-11-27 实机：日志有数据、画面什么都没有）。

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
  - 2026-11-27 实机：`+0x10` 与本帧算出的球心相同（`moved=0.00`），即判定是本帧位置的重叠检测，不是从上一帧扫过来；这也印证了球心公式。
- **左右拳按按键分**：mask 1 = LB 那侧（左）、2 = RB 那侧（右）（实机：`pad_rb` 出的是 1 号攻击 `jab_r`）。按肢体分会错：`maxBeat` 在 V515 里是左臂动画、却由 RB（mask 2）出。
- **加农**（Weapon_VehicleSwingShoot，vtable `0x17E6320`；0x6B4100 / 0x6B4080 / 0x6B4190）：
  - 状态机在 `w+0x1650`，当前处理函数在 `w+0x1668`：0x6B4100 = 等待（每帧把 `*(w+0x120)+0x80` 冻结到 `w+0x1670`），0x6B4080 = 扫射中，其它 = 不在这两态（不画）。
  - 扫射中剩余 `+0xE18` 到 0 且扳机还按着（0x691080：连射数 < 2 且 `+0x143` 为 0 时读 `+0x13A`，否则读 `+0x13D`）时，从 t = 0 沿**旧的**冻结方向重扫一轮；所以剩 0 + 按着 = 还有一整轮，剩 0 + 松开 = 不画（`RoundsToCome`）。
  - 每发的仰角偏移 a(t) = Σ begin + (end − begin)·t^pow（`+0x1680` / `+0x1690` / `+0x16A0`；V605 是 0.75 / 0 / 0.25）。
  - t = 1 − 剩余 `+0xE18` / FireBurstCount `+0x370`。
  - 正角度让炮口向下（`barga_aim.h` `kSweepSign`，要翻转只改这一个常数）。
  - 落点按 `RoundLands`（与 HUD 其它弹着点同一套弹道和地图射线）从 `MeanMuzzle` 的位置算出。

## 已离线验证

- `barga_aim_check`：190 项全过，`build.cmd` 编译零警告。覆盖的内容：
  - 扫射角公式与端点、方向（仰角减扫射角、保持航向、零向量取 +Z）。
  - 连射进度、剩余弹采样（下一发与最后一发必在、按发射顺序）。
  - 判定球心。
  - 两套生成表上的连招可出规则与变体匹配（错一条就不匹配）。
  - 两侧拳头的选择（按按键：两套表的 `maxBeat` 都在右侧；V515 `ground_charge` 之后右 = `maxBeat`、左 = `maxHook_l`）。
  - 弧线在出拳高度；按实机眼位（脚上 41.3 m、前 6.3 m）待机两条弧都在 ±22.5° 视野内，同样的弧放在地面则在视野外。
  - 连射状态：扫射中按剩余、等待按整轮、剩 0 按着重扫 = 整轮、剩 0 松开 = 0、其它状态 = 0。
  - 表的合理性（够及点在脚前、脚上，左拳在 +X）。
- `gen_barga_reach.py --check`：`current`。

## 2026-11-27 实机（V515，range:4 巴尔加对决，全周天第一人称，Debug=1）

- 数据路径通：`BARGA v=` / `BARGA sphere` 行都在，`limb` 按组名解析正确，`ground=1`（脚前 60 m 内射线碰到地面，是预期）。
- 什么都没画，原因两条：一是待机弧在地面上、整条在视野下方（见上，已改到出拳高度）；二是带组名的那几行整行为空——`%ls` 遇到日文组名在 C locale 下让 vsnprintf_s 失败，`lit=[]`、`spheres=0` 看上去像没读到，其实是日志丢了（已改为先转 UTF-8 再 `%s`）。出拳中判定圈有没有画出来，当时没有能核对的日志，下面的 `BARGA draw` 行就是为此加的。

## 未验证（等实机，Debug=1 日志每 500 ms 一组）

- `BARGA v=... <SGO> current=.. accept=.. flags=.. lit=[组名] spheres=.. arcs L=..(*) R=..(*) fwd=(..)`：`lit` 现在应有日文组名；核对每个攻击下标亮的是哪些组名，带 `*` 的一侧与按键（RB 右 / LB 左）对得上。
- `BARGA draw WxH arcs L=a/b R=a/b mid=(x,y) d m spheres a/b first=(x,y) d m rounds n`（HUD 线程，`a` 是投影到屏幕上的点数）：待机时亮的那条弧 `a` 应接近 `b`（13），`mid` 在屏幕内；出拳中 `spheres a/b` 的 `a` > 0。有 `BARGA v=` 而没有 `BARGA draw`：发布 / 新鲜度那一环断了。之后一行 `BARGA draw rounds X landed of N, Y on the screen`（只在有炮弹时）。
- `BARGA sphere i limb=.. r=.. c=(..) hull=(..) moved=.. ground=.. touch=.. target=..`：`hull` 是球心在车体系里的坐标，应接近表里的够及点（尤其 +X 是否为左手侧，验证 `gen_barga_reach.py` 的 ORDER=0）。
- `BARGA cannon on=.. state=sweeping|waiting|other(+RVA) held=..(+13D .. +13A ..) burst=.. left=.. count=.. toCome=.. t=.. frozen=(..) src=..(vehicle) native=(..) predicted=(..) ... first=..[(air)] last=..[(air)]`：
  - `state` 的 RVA 应是 0x6B4100 / 0x6B4080；按住扳机时 `held=1`。
  - `native`（炮口 +0xD0）和 `predicted` 是否一致；只有 y 的符号相反就翻转 `kSweepSign`。
  - `src` 后面有没有 `(vehicle)`（`w+0x120` 是不是车体本身）；`first` / `last` 落点是否合理。
- `BARGA v=... variant none`：表不认识这台车的攻击列表。
- 画面上 vhud 不再给 V605 加农画第二个弹着点。
- V605 的 `ground_charge`（mask 16）/ `attack_cannon`（mask 64）由哪个键出没有核对过（推测在扳机或面键上）。

## 实机怎么测（一次运行）

- V515：`python tests/autopilot/drive.py run range:4 0 90 --ini TestBoardVehicle=501_FortressRobo --ini FirstPersonStart=1 --ini Debug=1 --shots DIR --every 1.5`，约 20 s 登车；登车后待机截几张，再 `--key pad_rb@35:600`、`--key pad_lb@40:600`、`--key pad_rb@45:600`（出拳，按键前后各截一张）。
- V605：原版任务 M118 里 90 s 内没出现 501，不用它。改用调试召唤（`DebugSpawn=1`，菜单打开时默认就是「载具」分类，巴尔加炮 605 是第 6 项）：在没有 501 的测试场关卡（`range`，`range:4` 会先登上 V515）里 `--ini DebugSpawn=1 --ini TestBoardVehicle=501_FortressRobo`，`--key 77@25:200`（F8 开菜单）、`--key 75@27:200` … 共 5 次 F6（各隔 1 s）、`--key 78@33:200`（F9 召唤），召唤出来 6 s 后 TestBoardVehicle 把玩家送上去；之后依次按住几个候选键（如 `pad_lt` / `pad_rt` / `pad_x` / `pad_y`，各 3 s）找出蓄力 / 加农。M 地图「摆放」面板也能放，但要鼠标点，autopilot 按不了。
