# 测试场更新：新内容、M 地图摆放、不限武器等级（2026-10-17）

用户原话：「测试场的东西该更新了……测试场支持m里面手动摆放怪物和各种载具之类的。并且测试场不限制武器等级。」

## A. 一份清单，从登记表派生（`testrange/catalog.py`）

以前有两份手抄清单：测试场的 `testrange/gen.py VEHICLES / ENEMIES`，调试召唤的 `src/debug_spawn.h kEntries`，近期合并的内容两边都没跟上。现在两边都从 `testrange/catalog.py` 来：

| 来源 | 登记表 | 分类（面板标签） | 文件在哪 |
|---|---|---|---|
| EDF6 原版载具 | `catalog.STOCK_VEHICLES`（带 `mission_setup` 的 SGO，新增救援装甲车 `V507_RESCUETANK_AI`） | 载具 | Root.cpk |
| EDF4.1 移植的载具 | `edf41port/objects.json`（类 `Vehicle505_Tank` / `Vehicle501_FortressRobo`） | 载具 | 安装器写入 Mods |
| 测试场自己生成的载具 | `gen.PLACEABLE_DERIVED`（直升机、玩家战斗机 / 攻击机、沙扎比、每种可开飞机的停放版、喀秋莎、自行榴弹炮、钻头战车、边三轮、Grape、深渊爬行者） | 插件载具 | 测试场安装写入 Mods |
| 安装器的载具 | `catalog.INSTALLER_VEHICLES`（民用轿车 `EDF6VC_SEDAN`，SGO 保留 V512 的 `mission_setup`） | 插件载具 | 安装器写入 Mods |
| EDF6 原版敌人 | `catalog.STOCK_ENEMIES`（原测试场波次、调试召唤、大混战用到的全部：新增狂战士、护盾运载者、E605 UFO、传送舰、星导者新舰、母舰等） | 敌人 | Root.cpk |
| EDF5 任务包１的要塞 | `edf5port/dlc/registry.json`（`E510_DLC_FORTRESS`） | 敌人 | 安装器写入 Mods |
| EDF4.1 移植的敌人 | `edf41port/objects.json`（母船 `UFOMOTHER301`、地球吞噬者 `UFOMOTHER401`、UFO 机器人 `UFOROBO401`、赫克托、迪洛伊、护盾运载者、四足步行要塞、运输船、UFO、龙、巨蚁 / 巨蜂女王 / 巨型蜘蛛等） | EDF4.1 敌人 | 安装器写入 Mods |

- **EDF4.1 的物体不需要玩家装 EDF4.1**：它们是预先转换好、随安装器发布的（`tools/edf41_objects.py`，`edf41port/objects/`）；只有 4.1 的地图和音效才需要 4.1 本体或资源包（`pylib/edf41src.py`）。所以「缺 4.1 资源时跳过」的实际条件是「这些物体的文件不在 Mods 里」（安装器没跑过，或被别的 mod 的同名文件挡住时安装器会跳过，`edf41_objects.unavailable`）。测试场与插件都按文件是否在 Mods 判断，缺了就平稳跳过：
  - `gen.install`：计划里放的安装器载具缺文件时从计划中去掉并在结果里写一行「跳过 …」（`gen.without_missing`）；敌人波次选了缺文件的 4.1 / EDF5 敌人时拒绝并说明缺哪个文件。
  - 插件：`Mods` 行（`Entry::mod`）的 SGO 不在 `Mods\OBJECT\` 下时既不预载也不生成，面板显示「（未安装）」。
- 不进清单的（`catalog.edf41_left_out`，selftest 要求每个 4.1 物体要么是一行、要么有原因）：4.1 友军士兵（插件只按支援士兵的方式生成士兵）、蜘蛛网（贴合 4.1 原地图生成的 `_FIT` 物体）、4.1 任务自己的变体（`UFOMOTHER401M009` 等：同一模型、任务内的调整；基础物体是一行）、已有空车版的 NPC 乘员版（`VEHICLE501_FORTRESSROBO_AI`）。插件驾驶的 NPC 喷气机、敌方战斗机、星导者生物、航空潜舰也不进「摆放」：它们要脚本 `CreateFriend` 的驾驶员或插件的机组（有人驾驶的机种在「飞机」分类里以支援呼叫的方式到场，和调试召唤一样）。
- 波次（启动器的「敌人种类」）也从清单来；母舰类（离地 100 米以上出现的）不进波次（波次在地面点位刷出），只在 M 地图里摆放。
- 生成：`python tools/gen_spawn_catalog.py` 把对象行写成 `src/spawn_catalog.inc`（`debug_spawn.h kEntries` 包含它），CI 用 `--check` 校验；`tools/selftest.py spawn_catalog_*` 校验它是最新的、每个登记表的新内容都进了清单（新的 4.1 类、新的 `GROUND_VEHICLES` 种类、新的玩家 / 停放机种都会让它失败，直到给出分类或理由）。

### 安装器装的 `target_range` 预设

在原有内容上加 `RANGE_NEW`：救援装甲车 507、民用轿车、4.1 坦克（`VEHICLE301_TANK_AI`）。用本机 M045 的真实点位核对（`tools/selftest.py target_range_fits_the_real_plain`）：31 → 34 个放置，互不重叠，靶子点位仍有 8 个。4.1 的巨型机甲 Balam 体积不明（测试场不知道它的占地半径），不放进预设，用 M 地图摆放。启动器旧标题里的「地图上最多 12 台」并不是真实约束（平原有 36 个平地点位，`gen.spots_for` 按占地半径分配，放不下时安装报错），已改成如实说明。

## B. M 地图「摆放」面板（只在测试场）

- 判定：任务开始时（`mission.cpp MissionStart` → `PreloadDebugSpawn`）读当前模式的内容号（`edf5campaign.cpp TestRangeMode`，与游戏脚本 `GetContentID` 同一读法，见 `docs/weapon-level-cap-re.md` §3）是否等于 ini 的 `TestRangeContent`。是测试场时 `RangePlace=1`（默认）就开，**不受 `DebugSpawn` 影响**；其它任务没有这个面板。
- 预载的代价（2026-10-18 实机）：整份清单常驻后，带 HD 贴图的模型档案超过了游戏 CRI File System 的 loader 池（原版 130），
  第 95 个档案打不开，原版贴图加载不判空，进测试场必崩（`EDF+0x1134CCB`）。插件加载时把 `num_loaders` 提到 512
  （`src/fsbudget.cpp`，逆向与实测见 [cri-fs-loaders-re.md](cri-fs-loaders-re.md)）。
- 预载：测试场里预载清单中文件在的每一行（物体行）；`Entry::preload=false` 的行（另外带一个约 17 MB 模型的 4.1 变体，主要是赫克托的 19 种武器组合）只有 `RangePlaceAll=1` 才预载，否则显示「（未预载）」。默认预载 4.1 每类一种以及与它共用模型的变体。（2026-10-19 更正：判断只看模型档案，赫克托等变体不另带 17 MB 模型；未预载的行改为第一次放置时按需加载，见 [feedback-2026-10-19-mapui.md](feedback-2026-10-19-mapui.md)。）联机时同样预载（2026-10-20 起联机也能摆放全部种类）。调试召唤（DebugSpawn=1）在其它任务里仍只预载原版行，不为 4.1 / 测试场的行付出内存。
- 生成：与调试召唤是同一条路（`debug_spawn.cpp MakeRow`）：载具 = `CreateObject` + `mission_setup` + 己方阵营、空车；敌人 = 脚本 `CreateEnemy` 的步骤；飞机 = 地图空中支援呼叫；士兵 = 支援士兵。位置是地图上点到的地面点（`mapcmd.cpp TargetPoint`：指针下的地图射线，手柄时是屏幕中心），加上该行的离地高度（飞行敌人、母船在自己的高度出现：母舰 900 米、4.1 母船 / 地球吞噬者 300 米、运输船 / UFO 机器人 150 米）。载具朝背离玩家的方向，敌人朝向玩家。
- 操作（`src/range_place.h`，参照支援栏）：
  - 鼠标：右侧面板，点「摆放」标题展开 / 收起；点分类标签换分类；点一行选中并「待放」（亮起），之后在地图上每左键一次放一个（一直待放，可连续放），右键或再点这一行取消；▲▼ 翻页。待放时其它指令 / 支援自动取消（同一时间只有一件事待点）。
  - 键盘：`,` / `.` 上一项 / 下一项，`/` 换分类（面板关着时第一下先展开），`Enter` 在指针处放下选中的一项，`Backspace` 收起。地图原有按键全被占用（WASD QE RF G V X …），这几个键原来没用。
  - 手柄：十字键上下选择、左右换分类（第一下展开），`A` 在屏幕中心放下，`B` 收起面板。面板展开时地图自己的 A（回到玩家）/ B（关地图）让给面板，B 收起面板后要松开 A / B 才还给地图（`MapPlaceHoldsPad`），按住 B 不会接着把地图关掉。
- 联机：2026-10-20 起**可用**（敌人、载具、友军士兵、无人机；飞机行即支援呼叫），走支援呼叫同一套事务：房主规划、每个成员预载（未预载的行在房主规划前、各成员 PREPARE 时按需加载，加载完才 READY）、全员用同一个 32 字节网络 ID 创建并登记，全员确认后激活（敌人 `0x548D50`、士兵放开、无人机起飞、空车无需激活）。资源号高字节区分种类（敌人 0xE1、载具 0xE2、士兵 0xE3、无人机 0xE4），旧版只认 0xE1，其余在 PREPARE 被拒绝。下面是当初禁用时的核实（仍成立，事务协议正是为此）：核实：插件直接 `CreateObject` 生成的物体只在本机——EDF6 联机里物体要在每台机器上各自生成、再用同一个网络 ID 注册才互相对应（脚本的 `CreateEnemy` 在每台机器上都跑；空投的车辆在 `0x5E91D7` 注册网络 ID；插件的地图支援靠自己的事务协议让每台机器用同一个 32 字节 ID 生成，`docs/support-network.md`：“registration itself does not replicate creation”）。房主一个人生成的物体，客人那边不存在，所以不能「经原版同步」；要联机可用，得给摆放另写一套与支援呼叫同级的事务协议，这次不做。

## C. 测试场不限制武器等级

见 [weapon-level-cap-re.md](weapon-level-cap-re.md)：上限是模式数据，测试场的联机模式改成「无上限」，单机原来就没有限制。

## 改动文件

- 清单：`testrange/catalog.py`（新）、`testrange/gen.py`、`tools/gen_spawn_catalog.py`（新）、`src/spawn_catalog.inc`（生成）、`src/debug_spawn.h`、`.github/workflows/build.yml`。
- 摆放：`src/range_place.h`（新）、`src/debug_spawn.cpp`、`src/mapcmd.h` / `mapcmd.cpp`、`src/map.cpp`、`src/hud.cpp`、`src/hudtext.inc`、`src/edf5campaign.cpp`、`src/crew.h`、`src/plugin.cpp`、`EDF6VehicleCrew.ini`（`RangePlace` / `RangePlaceAll`）。
- 武器等级：`tools/make_edf5_campaign.py`。
- 启动器：`testrange/testrange.pyw`（载具列表可滚动，标题如实说明，敌人列表加宽）。
- 测试：`tests/range_place_test.cpp`（新）、`tests/debug_spawn_test.cpp`、`tests/debug_spawn_launch_guard.py`、`tests/debug_spawn_resource_audit.py`、`tests/map_command_identity_test.cpp`、`tools/map_command_runtime_check.cpp`（`PlacePanel`）、`tools/hud_view.cpp`（6 个摆放场景，四种语言）、`tools/selftest.py`。

## 待实机验证

1. 测试场（离线）：按 M，右侧有「摆放」；其它任务没有。日志 `DEBUGSPAWN on (the test range's place panel): N vehicles and M enemies preloaded, K not installed`，进关时间与内存（任务开始的 `LogMemory`）与更新前比较。
2. 每个分类各放一个：原版载具、插件载具（停放战斗机能上、沙扎比 / 喀秋莎能开）、民用轿车、507、4.1 坦克 / Balam；原版敌人；EDF5 要塞；EDF4.1 母船（300 米高）、地球吞噬者、UFO 机器人（飞船形态起飞 / 变形）、赫克托、迪洛伊、运输船编队；支援呼叫；友军士兵。看出现位置（地面 / 高度）、朝向、AI 是否动起来、母船类的部件与音效。
3. 插件代码生成的停放喷气机（`CreateObject` + `mission_setup`，阵营 2）与脚本 `CreateVehicle2`（阵营 5）放的是否一样能登机、起飞（`jet::Adopt`）。
4. 鼠标：点行待放、连续放、右键取消、翻页；键盘 `, . / Enter Backspace`；手柄十字键 / A / B，B 收起面板时地图不跟着关，再按 B 才关地图。
5. 联机房间的测试场：房主和客人各摆一个敌人、一辆空车、一名士兵、一架无人机：两边都出现、位置一致，日志 `RANGE NET registered` / `activated`；摆一个未预载的行：两边先加载（`DEBUGSPAWN ... load queued`），加载完才出现。混一个旧版成员：非敌人行被拒绝并提示。（2026-10-20，未实机）
6. 安装器的测试场：34 台载具都在、不重叠；没有 4.1 物体的安装（例如别的 mod 挡住）时安装输出「跳过 …」而不是失败。
7. 武器等级：见 `docs/weapon-level-cap-re.md`「待实机验证」。
