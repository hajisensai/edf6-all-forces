# 2026-11-21 新功能：机甲自定义装配（4 武器槽 + 机体槽 + 核心槽）

## 玩家原话

「想到一个好主意，机甲弄成自定义化的 四个武器槽位+一个机体槽位+加一个核心（可能）」

## 1. 盘点：仓库里能驾驶的机甲

| 机甲 | 类 / SGO | 挂点 | 本次 |
|---|---|---|---|
| 尼克斯（贝加尔塔，EDF6 504） | `Vehicle504_begaruta`，`V504_BEGARUTA[_BLUE/_RED/_GOLD].SGO` | 8 行：手 r/l、前臂 r/l、肩 r/l、上臂 r/l | **做** |
| 艾伦（612） | `Vehicle612_nix`，`V612_NIX[_BLACK/_RED].SGO` | 同上 8 行（最后两行是肩） | **做** |
| 古拉比斯（608 旧型机器人） | `Vehicle504_begaruta`，`V608_OLDROBOT.SGO` | 同上 8 行 | **做** |
| 普罗透斯 | `VehicleBigBegaruta`，`V614_PROTEUS_MK2_CALL.SGO` | 3 行（两门炮 + 导弹架），插件两形态改造自己管武器（`docs/proteus-re.md`） | 不做 |
| 深渊爬行者 | `Vehicle502_GroundRobo` | 3 行（机炮 + 左右臂炮），布局固定 | 不做 |
| EDF4.1 贝加尔塔 406 | `VehicleBegaruta`（移植，`pylib/edf41mech.py`） | 6 行，骨架不同 | 不做（后续） |
| 巴尔姆 / 巴尔加 / 泰坦 | — | — | 任务排除 |

依据（只读 Root.cpk；数据表见 `pylib/mech_parts.py`，有游戏时 `tests/mech_parts_test.py` 逐项对照 Root.cpk）：

- 504 / 612 / 608 三类共用同一套 8 行 `vehicle_weapon_setting`，同一套每帧 / AI / 开火代码（`docs/nix-re.md` §1）。
- 原版**每一个**玩家机甲请求（`EWEAPON356..378`、`DLC_Vehicle_v504_begaruta_gold`、`MPACK_A_Weapon075/076`、`MPACK_B_Weapon109`，共 27 个，WEAPONTABLE 类别 309）的武器表都只用 0..5 行，而且每侧手 / 前臂二选一：
  - 右臂 = 行 0（手持：机枪、战斗火枪、动力光刃）或行 2（前臂：转轮炮、火箭炮、电磁激光炮……）；
  - 左臂 = 行 1 或行 3；
  - 右肩 = 行 4，左肩 = 行 5。

  这就是玩家要的「四个武器槽」，每种组合在原版里都有先例（例如 `EWEAPON366` 左手机枪 + 右前臂榴弹）。
- 请求武器 `Ammo_CustomParameter[4]` = `[运输机, 集装箱, 载具 SGO, 设定块]`，设定块 6 项：`[0]` 装甲倍率对、`[1]` 移动 4 数、`[2]` 推进 / 能量 6 数、`[3]` 瞄准、`[4]` 8 行武器表（每项 `[武器 SGO, 后坐数对]`，空行是数值）、`[5]` 尾项。
- 推进块 `[2]` 在原版请求里只有 15 种（例如 `[20,1.5,45,240,0.28,50]`、尼克斯的 `[20,2.5,65,240,0.28,50]`、重装的 `[10,1,25,760,0.34,40]`）。第 4 个数 240 / 420 / 480 / 540 / 580 / 760 随机型明显分档，看起来是推进能量。**六个数各自的含义没有逆向**，所以核心只在原版现成的推进块之间选，不编新数值。
- 武器伤害随请求的等级（WEAPONTABLE 第 5 列，0.1 … 4.5，集装箱 `+0xBA0` → 车 `+0x67C`）缩放：同一把 `v_504_h_machinegun01` 在 Ａ型（0.1）是 14.4，在Ａ２（0.3）是 17.4。

## 2. 设计

### 2.1 五个槽 + 底座

| 槽 | 可选 | 约束 |
|---|---|---|
| 底座请求 | 27 个原版玩家机甲请求之一 | 只有呼叫**这一个**请求时才换成自定义机甲；装甲 `[0]`、移动 `[1]`、瞄准 `[3]`、尾项 `[5]`、等级都取它 |
| 机体 | 8 种：尼克斯（原色 / 蓝 / 红 / 金属涂层）、艾伦（原色 / 黑 / 红）、古拉比斯（粉色尼克斯 `DLC_VEHICLE_BEGARUTA01/02` 不在 WEAPONTABLE，没有可呼叫的请求，不列） | 只换模型；三类耐久都是 1200，差别都在请求的设定块里，机体本身不改数值 |
| 右臂 / 左臂 | 手持 3 种 + 前臂 9 种（只列那一侧有文件的） | 武器的等级（携带它的原版请求里最低的等级）不高于底座请求 |
| 右肩 / 左肩 | 12 种（粒子光束炮只有右、幻象荚舱 / 动力达因ＧＣ只有左） | 同上 |
| 核心 | 15 种原版推进块（按来源请求命名） | 同上；「-」= 底座请求自己的 |

- 武器槽可以留空，但至少一个武器。
- 「合理约束」（2026-10-25 用户：「合理的改，不是完全随心所欲」）：低级请求不能拿晚期武器 / 核心；装甲、移动、等级永远跟着实际呼叫（付了呼叫点数、解锁了）的那个请求走。

### 2.2 配置（唯一真相源：ini）

```ini
; 底座请求,机体,右臂,左臂,右肩,左肩,核心
SupportVehicle_OWN_MECH=EWEAPON378,NIX_RED,EMLASER,EMLASER,PBEAM,MIRAGE,-
SupportVehicle_OWN_MECH=EWEAPON362,OLDROBOT,HEAVYREVCANNON,-,HOWITZER,HOWITZER,EWEAPON374
```

- `-`：武器槽留空；机体 / 核心取底座请求自己的。
- 写错（名字、侧别、等级超出、全空）：整条作废，按原版出动，HUD / 日志点名原因（与其它 `SupportVehicle_*` 同一规则）。
- 名字表：`pylib/mech_parts.py`（Python 唯一数据源）→ `tools/gen_mech_parts.py` 生成 `src/mech_parts.inc`（CI `--check`）。

### 2.3 生成的文件

`EDF6VC_LO_MECH_<16 位变体>.SGO`（内容寻址，与战机挂载同一命名规则，`support_loadout.h VehicleVariantFile`）：

- 以机体 SGO 为底；
- `vehicle_setup` / `mission_setup` = 底座请求的设定块，`[2]` 换成核心，`[4]` 换成 8 行武器表（右臂按武器类型放行 0 或 2，左臂行 1 或 3，肩行 4 / 5，其余数值 0）；武器的后坐数对取原版携带它的最低等级请求里的那一对；
- 加 `resource` 预载列表：用到的武器 SGO（底座请求的预载表里没有别的请求的武器）。

变体 64 位：位 62 载具、位 61 机甲；位 0–5 底座请求、6–9 机体、10–13 核心、14–37 四个武器槽各 6 位。

### 2.4 召唤（每台机器同样换）

沿用玩家召唤载具的链路（`docs/feature-2026-10-25-room-menu-review.md` §4、`src/player_vehicle_logic.h`），加三处：

1. **认机甲请求**：集装箱配置（0x5E5687）的载具路径是 504 / 612 / 608 家族任一文件 → 机体 `MECH`。每台机器都为这名玩家计数（seq），保证计数一致。
2. **认底座请求**（只在请求者本机）：配置调用本来就带着载具路径和等级（`0x5E5687` 的 r8 与 `[rsp+20h]`），27 个原版请求的「载具文件 + 等级（float32）」两两不同（测试守着），所以用这一对认请求（`mech_loadout.h MechRequestOf`），不必去读设定节点。是配置里的底座请求 → 自定义机甲；不是 → 本次原版（仍占一个 seq，广播「原版」）。
3. **落地**：`PlayerVehicleStepPath` 换路径（已有）；机甲另外要换设定——原版集装箱在 `CreateObject` 之后把**请求自己的**设定交给第 46 槽（静态反汇编 H，capstone 复核：`0x5E9048` 等级 `+0xBA0 → 车+0x67C`；`0x5E9054` 文字长度 `+0xC48` 为 0 时看 `+0xC68` 的变体 tag，`0xFFFF` 跳过，否则 `call [vtable+0x170]` 传 `+0xC58`；这条路径不析构 `+0xC58`，由集装箱析构时按 tag 处理），那会在自定义机体上造原版武器。所以 `VehicleStep`（`0x5E8FDC` 的重定向）在 `CreateObject` 之后调 `PlayerVehicleMade`（`src/player_vehicle.cpp`，安装时校验 `0x5E9048` 起 53 字节特征、`0x62D6E0` 读设定函数头与析构表可读，不符则机甲一律原版）：
   - 先按原版顺序写等级（`veh+0x67C = box+0xBA0`，原版在设定之前写）；
   - 析构集装箱的设定变体（变体析构表 `0x1765220`，与 `ApplyMissionSetup` 同一张）并把 tag 置 `0xFFFF`；
   - 对新车调 `ApplyMissionSetup`（读车自己的 `mission_setup` → 第 46 槽）。

   之后原版继续：队伍、等级（同值再写一次）、设定（已空，跳过）、请求者事件、网络 ID。

联机使用 PVEH v4 和房间协议 14。机甲资源及原生换装入口都就绪时，该机才向其他成员报告可创建该机甲。

### 2.5 界面

- 房间 / HQ 战前配置：「我召唤的·机甲」一个单位，7 行（底座请求、机体、右臂、左臂、右肩、左肩、核心），每行进入后从合法值里选；换底座请求时把超出等级的武器 / 核心清掉。原版 = 不写这个键。
- 安装器菜单 8：同一个键，按行编辑。

## 3. 未在游戏里验证（如实）

1. 504 机体挂 612 的激光武器 / 612 机体挂 504 的手持武器：骨骼名一致、类共用代码，但跨类挂没有原版先例（原版只在同类里换武器）。
2. 机甲 SGO 里加的 `resource` 预载表被对象预载读到（坦克 / 请求 SGO 用同名成员，M）。
3. 换掉的设定在三类上都正确建出武器、开火键位与原版同排法。
4. 联机双机：各端同一台机甲、武器一致。
6. 战前配置的 3D 预览一律是尼克斯模型（`EDF6VC_MENU_V_MECH.SGO`，按 body 取），换机体不跟着变；取景按坦克比例，距离没进游戏调过。
5. 配置调用收到的等级就是 WEAPONTABLE 第 5 列的 float32（静态看 `0x5E5687` 前 `movss [rsp+20h],xmm6`；没在游戏里核对过数值）。日志 `PLAYERVEH this player's mech request #n (<路径>, level <x>: <请求>)` 会把看到的值和认出的请求打出来；认不出就是原版，不会出错机。

## 4. 实现记录

- 数据：`pylib/mech_parts.py`（Python 唯一数据源：8 机体、24 武器、15 核心、27 请求，四语名称取自武器 SGO 的 `name.*` 与 `WEAPONTEXT.*`；机体名手写）→ `tools/gen_mech_parts.py` 生成 `src/mech_parts.inc`（`tools/check.py` 与 CI `--check`）。
- 规则（C++ `src/mech_loadout.h` + `src/support_loadout.h ParseMechLoadout / FormatMechLoadout`，Python `pylib/mech_parts.py`）：`VehicleBody::mech`（11）、`SupportVehicle_OWN_MECH`、变体位 61；同一组合法 / 非法值两边各测一遍，变体值互相钉死（`0x600000185CB2C1D8`）。
- 生成文件：`tools/support_loadout.py` 按名字生成 `EDF6VC_LO_MECH_<变体>.SGO`（机体原格式 SGO / DSGO 写回），房间成员的机甲文件走已有的 pending 名单。
- 召唤：`player_vehicle_logic.h` `TwinOfPath` 认整个机甲家族、`Decide(…, eligible)`、PVEH `kVersion` 4；`player_vehicle.cpp` 认底座请求与落地换设定；`airdrop.cpp` 调 `PlayerVehicleMade`。
- 界面：房间 / HQ 战前配置「我召唤的·机甲」（`src/room_menu.h` 等，见下）；安装器菜单 8 的 `edit_mech`。
- 测试：`tests/mech_parts_test.py`（规则；有游戏时逐项对照 Root.cpk，并验证 27 个请求原样生成的机甲设定与请求自己的一致、8 个机体都能装最宽的配置）、`tests/support_loadout_test.cpp`、`tests/player_vehicle_test.cpp`（机甲家族识别、只有底座请求换、其它机甲请求原版但照样计数、have 消息携带）、`tests/support_loadout_ini_test.py`。
- 一处取舍：同一把武器在不同请求里的后坐数对偶有不同（如肩射榴弹炮在 362 是 `[25, 0.025]`、在 370 是 `[20, 0.025]`），表里取携带它的最低等级请求的那一对。
