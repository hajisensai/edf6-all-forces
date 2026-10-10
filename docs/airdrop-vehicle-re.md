# 运输机投送载具：原版集装箱链的参数与插件实现

分支 `feat/fb1009d-airdrop-vehicle`，接 `docs/feedback-2026-10-09c-bar.md` §3。EDF.dll TimeDateStamp `0x678CCB46`，地址为 RVA。

## 0. 结论

- 原版投放已在游戏里跑通并抓到参数（§2，证据 `docs/evidence/airdrop-probe-2026-10-10.txt`）。
- 插件按原版集装箱流程实现了"直升机投送·坦克 / 装甲运兵车 / 民用轻卡"（§3），实机验证：集装箱放下、
  落地出车、套用车辆自己的任务设定、玩家上车并开走，游戏正常退出（§4，证据 `docs/evidence/airdrop-run-2026-10-10.txt`）。
- 载体是插件的运输直升机，不是运输机：运输机实测转弯半径约 600 m，在 RM015 里始终离目标 200–500 m 绕圈，
  一次也没飞到目标上空（§3.3）。直升机能像原版 Transporter508 一样悬停在点上方。
- 只在离线（不在任何联机房间，包括只有自己的房间）可用：原版集装箱在会话中会用载体的派生 ID 给车辆登记网络
  身份，插件的直升机没有这个身份（§2.4）。

## 1. 静态逆向（H = 指令级）

| 步骤 | 地址 | 说明 |
|---|---|---|
| 运输机初始化 | Transporter508 vtable `0x17D75C0` 槽 50 = `0x5E5070` | 10 个参数，见 1.2 |
| 生成集装箱 | `0x5E5260`：`CreateObject 0x11945E0(*(EDF+0x20B2958), &运输机+0x60, 集装箱SGO, &ContainerInitParam)` | |
| 抓住集装箱 | 运输机 `+0x790/+0x798`；运输机更新 `0x5E58B0` 每帧把自己模型定位矩阵（`+0x780→+0x700`，`+0xB0..+0xEF`）写进集装箱 `+0x60..+0x9F` | 集装箱自己不跟随，由持有者写位置 |
| 配置 | `0x5E8B40(container, shared_ptr<请求者>*, const wchar_t* 载具SGO, const VehicleSetup*, float 等级)`，唯一调用点 `0x5E5687` | 请求者 → `+0xB70/+0xB78`，SGO → `+0xB80`，设定 → `+0xC38`（`0x5E44F0` 拷贝），等级 → `+0xBA0` |
| 释放 | 运输机槽 47 `0x5E5CE0` 调 `0x5E84D0(container)` | 只是把集装箱状态机（`+0xB40`）切到 `0x5E8C00` |
| 下落 | `0x5E8C00` 进入：按当前矩阵建刚体（`+0xBB0`），物理下落 | 没有水平速度：从放手处直落 |
| 出车 | `0x5E8C00` 更新：落稳 → `CreateObject(+0xB80)`（`0x5E8FDC`）→ `SetTeam(veh,5,1)` → `veh+0x67C=等级` → 设定（文字 `0x62D890` 解析，否则值交给车辆槽 46）→ 请求者事件（请求者为空则跳过，`0x5E911C`）→ 会话中注册网络 ID（`0x5E91D7`）→ 删除自己 | |

### 1.2 运输机初始化 `0x5E5070` 的参数

第 2 个 rdx = `shared_ptr<请求者>*`；栈参数（入口 rsp `+0x28` 起）：5 集装箱 SGO，6 载具 SGO，7 设定文字（可空），
8 设定值（variant 指针，第 7 个为空时用），9 int（透传基类），10 等级 float。

## 2. 运行时捕获（2026-10-10，原版空降兵呼叫 Grape）

命令（游戏关闭时）：

```
python tests/autopilot/drive.py run RM015 1 160 <log> --loadout tests/autopilot/loadouts/airraider_grape.ini --cmd "probe airdrop" --shots <dir>
```

原版投放在虚拟手柄点按 Y 后出现（§5）。

### 2.1 集装箱生成时的 InitParam（问题 1）

| 偏移 | 抓到的值 | 含义 |
|---|---|---|
| +0x00 | EDF+`0x17D7818` | `Transporter_Container::InitParam` 虚表 |
| +0x08..+0x2F | 全 0 | `InitParamBase@SceneObject` 的字段 |
| +0x30..+0x4F | `5DF72B67 FFFFFFFF 00000000 05000000 …` | 派生网络 ID（`0x776790`：id、计数、0、类型 5）。运输机有网络对象时才填；**没有网络对象的载体得到全 0**（`0x7767AF` 跳过填写） |
| +0x50 | `FFFFFFFF` | 运输机 `+0x778` 的派生计数，离线未设 |

运输机位置 (-167, 125, -70)；请求者（第 2 参数）= 玩家士兵（Engineer 虚表 EDF+`0x17CF100`）。

### 2.2 集装箱 config 里放载具的结构（问题 2）

`VehicleSetup`，0x38 字节，配置时 `0x5E44F0` 拷进集装箱 `+0xC38`：

| 偏移 | 抓到的值 | 含义 |
|---|---|---|
| +0x00..+0x1F | size 0，cap 7 | `std::wstring` 设定的文字形式：原版为空 |
| +0x20..+0x2F | `{指针, 0x6B}` | SGO 值（variant）的存储：指向请求武器 SGO 的节点引用（M：节点 107 = `Ammo_CustomParameter[4][3]`） |
| +0x30 | `0x0002` | variant 类型下标（`0xFFFF` = 空） |

载具 SGO 路径 `app:/object/edf6vc_vehicle401_striker_stores.sgo`（本机装的 mod 把第 1308 行换成了插件版），集装箱 SGO
`app:/object/v509_transportbox.sgo`，等级 1.0。

### 2.3 时序（同一次运行）

- 62.0 s 生成集装箱并配置；79.0 s 释放（集装箱在 (-368, 70, -522)，离地约 70 m）；84.0 s 出车 (-368, 0.9, -522)，
  车辆虚表 EDF+`0x17E01B0`，`+0x67C` 等级 1.0；之后车辆停在地面。

### 2.4 联机

会话中（`0x7748F0`），集装箱出车后用自己 `+0x130` 网络对象（来自 InitParam 的派生 ID）和 `+0xC30` 计数推导车辆 ID
并 `0x781950` 注册。插件的直升机没有网络身份（派生 ID 为全 0），所以插件只在离线时投送，联机由 `Plan` 拒绝并说明。

## 3. 插件实现

### 3.1 数据（`src/airdrop_logic.h`，纯逻辑，`tools/airdrop_check.cpp` 离线测试）

- `airdrop::ContainerInitParam`（0x58，16 字节对齐成 0x60）、`airdrop::NetId`、`airdrop::VehicleSetup`、`airdrop::SharedRef`：
  按 §2 的布局写成有名字的结构，`static_assert` 钉住偏移。
- `EmptySetup()`：无文字、无值 → 集装箱不碰车辆设定，由插件在出车时套用车辆自己的任务设定（与地面支援同一步：
  `support_spawn.cpp ApplySupportVehicleSetup`）。原版的值是请求武器 SGO 里的节点，插件的呼叫没有那件武器。
- `CarryMatrix`：集装箱挂在载体原点下方 8 m，水平，朝载体航向（与地面支援车辆同一套行向量约定）。
- `ReleaseNow`：到点上方 25 m 内；或经过点附近（≤90 m）后开始远离；或悬停后 5 s 不再接近（直升机受地图软边界
  限制，靠边的点只能悬停在边界带内，此时就地投放）。

### 3.2 运行（`src/airdrop.cpp`）

- `AirdropBegin`：`HeliFerry` 让运输直升机飞去悬停，然后按原版方式 `CreateObject` 集装箱（InitParam 派生 ID 为空、计数
  未设）、`0x5E8B40` 配置（无请求者、车辆 SGO、空设定、等级 1）。
- `AirdropTick`：每帧把集装箱矩阵写到直升机下方；满足 `ReleaseNow` 时调 `0x5E84D0` 放手，直升机 `HeliStartLeaving`
  （`Retire` 在场外连驾驶员一起删除）。
- 出车：`0x5E8FDC` 的调用改道到 `VehicleStep`：原版 `CreateObject` 之后，若是本插件放的集装箱（按集装箱 `+0xB80`
  路径缓冲区的地址认），核对车辆类、套用任务设定，然后集装箱照原版设队伍 5、删除自己。
- 签名：`CreateObject`、`0x5E8B40`、`0x5E84D0`、`0x7A3780` 的序言字节，两个虚表可读，`0x5E8FDC` 的 rel32 目标；任一不符则
  不提供投送（`AirdropReady` 为假，支援栏说明原因）。
- 集装箱 SGO 在任务开始时与地面支援车辆一起预加载（`PreloadAirdrop`）。

### 3.3 支援栏（`src/support_dispatch.cpp`）

- 在运输条目之后追加 3 项（旧序号、ini 键、线上值都不变）：`TANK_AIRDROP` / `TRANSPORT_AIRDROP` / `TRUCK_AIRDROP`，
  名称「直升机投送·坦克 / 装甲运兵车 / 民用轻卡」，一行（直升机图标），载具作为三个小块（新 `SupportVariant::tank/apc/truck`）。
- 计划 = 运输直升机 + 驾驶员（空中入场，同机降）；`Validate` 只接受这两个单位；`Assign` 交给 `AirdropBegin`，失败则直升机离场。
- 运输机最初做过：飞到点上方需要转回来，最慢 98 m/s 时转弯半径约 600 m，RM015 内两分钟始终离点 200–500 m（实测日志），
  因此载体改为直升机，未改动战机飞行代码。

### 3.4 测试开关

`EDF6VehicleCrew.ini [VehicleCrew] AirdropTest=1/2/3`（仅测试，默认 0）：任务开始测完场地 8 s 后，在玩家朝地图中央 40 m 处
请求对应投送，出车 4 s 后用登车枪同一条路径（`BoardingRequest`，原版上车按钮）把玩家放进车里。

## 4. 实机验证（2026-10-10，RM015 离线，后台 CLI）

```
python tests/autopilot/drive.py run RM015 1 165 <log> --place build/Mods/Plugins/EDF6VehicleCrew.dll=Plugins/EDF6VehicleCrew.dll \
    --place EDF6VC_HELI_TRANSPORT.SGO=OBJECT/EDF6VC_HELI_TRANSPORT.SGO --ini AirdropTest=2 --shots <dir> --key stick_up@135:6000
```

- 02:12:00 请求「装甲运兵车」；直升机带集装箱飞来；02:12:38 在 (-336, 30, -381) 悬停处放手（目标靠地图南缘，直升机软边界带内
  最近点，离目标 118 m）；02:12:43 出车 V507 (-336, 2, -381)，任务设定已套用，队伍 5；02:12:47 玩家上车（驾驶座）；
  第 135 s 虚拟手柄左摇杆前推 6 s，车开到 (-351, 0, -331)（约 50 m），截图里显示「驾驶席 你」、14 km/h、耐久 100%。
- 游戏按原版退出路径正常退出；替换的 DLL / ini、新增的 SGO 均逐字节还原或删除；之后游戏目录 Plugins 与测试前备份
  sha256 全部一致。
- 后台：7022 次采样中游戏前台 0 次、光标裁剪 0 次；真实光标移动后 2 s 未被拉回。

未验证：坦克 / 轻卡两项（同一路径，只换 SGO）；点不靠边时直升机悬停在点上方的精度（本图玩家靠边）；联机（设计上拒绝）。

## 5. CLI（tests/autopilot）

- 后台不碰系统鼠标：USER32 的 `SetCursorPos/GetCursorPos`（及 Physical 别名）、`ClipCursor`、`SendInput`、`SetForegroundWindow`、
  `GetKeyboardState` 在游戏进程内改道（Epic 覆盖层会改写 EDF.dll 的导入，只改导入不够）；只改道导入存根（按 unwind 表判定），
  改道失败时不让游戏以为自己在前台。窗口在屏幕外建、不激活。
- 虚拟手柄：游戏的玩法输入走 XInput（`xinput9_1_0` 的 `XInputGetState/XInputGetCapabilities`），键盘导入只用来问"有没有键按下"。
  按键码 0x100+位 = 手柄键，0x110/0x111 扳机，0x112–0x115 左摇杆。
- 支援道具（空降兵的载具呼叫）按**点按**使用（`0x5A1D70` 数帧，松开早于门限才用）；原版呼叫载具 = 手柄 Y 点按。
- `drive.py run` 新选项：`--cmd`、`--loadout`、`--shots`、`--key SPEC@秒[:毫秒]`、`--place SRC=DEST`、`--ini KEY=VALUE`；
  每次运行都有 DesktopWatch（前台进程、光标裁剪、光标被拉的检测）。
