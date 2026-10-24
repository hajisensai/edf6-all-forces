# 所有职业上所有载具（AllClassesRide）逆向笔记

2026-10-17，用户原话：「所有载具应该支持所有职业上去」。代码 `src/allclass.cpp`、`src/seat_class.h`，ini `AllClassesRide`（默认 1）。
地址均为 EDF.dll TimeDateStamp `0x678CCB46` 的 RVA。可信度：H = 反汇编 / 读文件确认；M = 调用链推断；L = 没核实，要实机看。

## 1. 上车的职业判定

| 事实 | 依据 | 可信度 |
|---|---|---|
| 判定式：`human+0x31C & seat+0x34 & seat+0x30` 非 0。CanRideSeat `0x6346FC`（`mov eax,[rdx+0x31C]; and eax,[r8+0x34]; test [r8+0x30],eax`），CanRide `0x62DD21`（`rdi` = 座位 +0x30：`mov eax,[rdi+4]; and eax,[rsi+0x31C]; test [rdi],eax`） | 反汇编 | H |
| 不同队伍且载具队伍 ≠ 5 时还要 `(seat+0x34 & seat+0x30)` 的 bit 7（`0x6346E7..0x6346F6`）：救援车乘客座掩码 143 = 0x8F 就是这个位 | 反汇编 | H |
| `human+0x31C` = 人物 SGO 的 `game_object_vehicle_mask`：GameObject 初始化 `0x545ECA` 取这个键，`0x5462AC` 先清 0、`0x5462D5` 写入。Root.cpk 里所有玩家 / NPC 士兵 SGO 的值都是单个职业位：游骑兵 1（45 个）、飞翼 2（38）、剑兵 4（24）、空降兵 8（4） | 反汇编 + 读 Root.cpk | H |
| `human+0x31C` 的读者只有：两处判定（上面）、两个访问器 `0xF85A3` / `0xFC8C9`（把它和队伍 +0x314 拷进访问器 +0x5C / +0x60，再按队伍、队伍 5 遍历载具；推测是上车按钮 / 提示找座位用的）。全 .text 扫 `[reg+0x31C]` 的 dword 访问，其余命中在渲染等无关结构里 | 字节扫描 + 反汇编 | H（人物/载具代码段的读者）/ M（访问器的用途、其余段的结构不是 GameObject） |
| `seat+0x30` = `vehicle_riding_position[i][4]`：座位构造 `0x62B430` 在 `0x62B79C` 写进临时座位（基址 rbp+0x60），`0x62BE33..0x62BE4D` 追加到 `vehicle+0x600` 的座位向量（+8 数据 = `0x608`，+0x18 个数 = `0x618`，步长 0x340），复制构造 `0x628E20` / `0x628A80` 原样拷 +0x30 / +0x34 | 反汇编 | H |
| `seat+0x34`：座位构造函数 `0x6293A4` 写 `0xFFFFFFFF`，之后只被两个复制构造拷贝；在 `0x5F0000..0x680000` 扫 `mov/and/or [reg+0x34]` 没有别的写者 | 字节扫描 | H（该段）|
| `0x62B430` 只有一个调用点：载具基类构造里的 `0x629C47`（每个 `vehicle_riding_position` 项一次） | 全 .text rel32 扫描 | H |
| 联机：上车由上车者自己的机器判定（上车按钮 → FindSeat / CanRideSeat → `0x5763E0` 发 `HUMAN_EVENT_VEHICLE_RIDE`），对端 `0x5775F1` 收到后直接调 `0x5765E0` RideVehicle，**不再检查职业 / 队伍 / 距离** | 反汇编（Recv 分支） | H（Recv）/ M（Send 一侧对每次上车都发） |

## 2. 原版座位的掩码分布（Root.cpk，101 个载具 SGO / DSGO，175 个座位）

掩码 15：121 个；掩码 143（bit 7 + 四职业）：16 个（`507_RESCUE_SHEET1..4`，4 种救援车）；掩码 0：没有。**非 15 的 9（游骑兵 + 空降兵）共 38 个**：

| 座位名键（[3]） | 载具（SGO） |
|---|---|
| `401_STRIKER_DRIVER` | VEHICLE401_STRIKER(_MPACK2)、V607_ROBOTRUCK(_AI/_F_AI/_G_AI/_M_AI)、V610_PTRUCK_GUN(_AI)、V611_PTRUCK_ROCKET(_AI) |
| `402_ROCKET_DRIVER` | VEHICLE402_ROCKET(_AI)、V402_ROCKET_EDF6BENEFITS |
| `407_BIGBEGARUTA_DRIVER` | V614_PROTEUS_MK2（6 种）、VEHICLE407_BIGBEGARUTA（4 种）的驾驶座 |
| `409_HELI_DRIVER` / `410_HELI_DRIVER` | VEHICLE409_HELI、VEHICLE410_HELI 驾驶座 |
| `502_GROUNDROBO_DRIVER` | VEHICLE502_GROUNDROBO(GOLD) |
| `503_BIKE_DRIVER` / `613_BIKE_DRIVER` | V503_BIKE（3 种）、V613_BIKE |
| `506_HELI_DRIVER` | V506_HELI(_EDF6BENEFITS)、V602_HELI |
| `512_KEITRUCK_DRIVER` / `513_TRAILER_DRIVER` | V512_KEITRUCK(_BGP)、V513_TRAILERTRUCK01CAB |

插件自己生成的飞机：玩家能登的标记座位已是 15（`pylib/vcobjects.py`），NPC 版仍是 506 的 9。

## 3. 「姿势」其实不是 [3]

- `vehicle_riding_position[i][3]`（座位 +0x18，`0x62B6F4` 调 `0x7A3500` 查表）是**文本表的键**：这些名字只出现在 `ETC/TEXTTABLE_*.TXT_SGO` 里，值是「Pilot Seat」「Seat 1」「Machine Gun - Left」等（座位显示名），EDF.dll 和所有 CAS 里都没有这些字符串。`505_TANK_DRIVER` 和 `506_HELI_DRIVER` 都是「Pilot Seat」（H）。所以 2026-10-05 给玩家战斗机把 [3] 从 `506_HELI_DRIVER` 换成 `505_TANK_DRIVER` 只是换了个同义的显示名，跟动画无关（没改回去，免得重装生成文件）。
- 乘员的姿势来自 [1] 那个**座位定位点自带的 MAB 参数**：座位构造 `0x6BADD0` 在载具 MAB 里查定位点 → 座位 +0x1F0，再读定位点的参数 SGO：`Animation`（`0x62B62D` → 座位 +0x10）、`DispCharacter`（`0x62B664` → 座位 +0x254，是否显示乘员）（H）。
- 乘坐状态的入口 HumanBase `0x57ADF0`（SoldierBase 第 79 槽 `0x5A1660` 进来，状态字 +0x5D0 = 0xCD）：`DispCharacter` 非 0 时显示模型、按名字在乘员自己的 CAS 上播 `Animation`（`0x57AE8E` 调 `0x116C2D0(rider+0xB90, [rider+0x1420], seat+0x10, 0)`）；为 0 时隐藏模型、播 `[rider+0x1484]`（H）。
- `0x116C2D0` 先用 `0x11664F0` 把名字换成状态号，CAS 没有这个状态（-1）就直接返回 false，什么都不改（`0x116C2EF`）：**不会崩**，乘员保持上车前的状态（H）。
- 原版座位的 `Animation` / `DispCharacter`（读每个 SGO 的 MAB）：
  - 摩托驾驶座：`vehicle503_bike_driver` / `vehicle613_bike_driver`，显示乘员。这两个状态**只有游骑兵（EDF6ArmySoldier.cas）和空降兵（EDF6Engineer.cas）有**，飞翼（EDF6_wingDiver.cas）、剑兵（EDF6HeavyArmor.cas）没有；
  - 卡车 / 拖车驾驶座（512 / 513）、Striker 和卡车的后座、救援车乘客座：`vehicle_striker_rear`，显示乘员，**四个职业的 CAS 都有**；
  - 其余所有座位（直升机、坦克、机甲、火箭车、普罗透斯、地面机器人……）：`DispCharacter` 0，乘员隐藏，不播座位动画。

## 4. 非允许职业坐进去的风险（逐项）

| 项 | 结论 | 可信度 |
|---|---|---|
| 姿势动画缺失 | 只有飞翼 / 剑兵骑摩托（503 / 613）会缺：不崩，站着 / 飞行姿势骑车。其余掩码 9 的座位乘员都隐藏 | H |
| 武器 / HUD 按职业分支 | 坐上任何座位都进同一个乘坐状态 0xCD：手持武器隐藏、禁用（`docs/sidecar-re.md` §1），载具武器挂在座位上，与职业无关；原版本来就让四职业坐坦克、机甲、普罗透斯炮手座（掩码 15），乘坐时的职业 HUD 路径早已存在 | M |
| 下车 | 下车走同一个乘坐状态的出口（`0x57B187` 角色控制器传送等），不看职业 | M |
| ragdoll | 乘员隐藏 / 坐姿时不进 ragdoll；被打下车走原版同一流程 | L |
| 飞翼的飞行器 / 剑兵的双手武器 | 乘坐时隐藏；下车后状态恢复是否正常要看 | L |

## 5. 做法（`src/allclass.cpp`）

1. **座位掩码**：把唯一调用点 `0x629C47` 的 `call 0x62B430` 改指向 `SeatBuild`：先跑原版构造，再对它刚追加的那一个座位（调用前后座位数 +1）写 `seatclass::Widen(mask)`：掩码里有任何职业位就补齐四位，0 和其它位不动。等同于 SGO 里本来就写着「四职业都能坐」：原版判定、上车提示访问器、插件自己的 `npcai.cpp SeatTakes`、`seatswitch.cpp ClassMay`、`crew.cpp PlayerBoardingEntrance`、`heli.cpp DoorSeat`、`mission_crew_support.cpp` 都读同一个字段，玩家与 NPC 一致，不用各改一处。
   - 普罗透斯重构的封座（`proteus.cpp Seats`：保存 +0x30、写 0；`OpenSeats` 写回）发生在运行时、在构造之后：保存的是放宽后的值，封住时 0 对谁都不开，放开后写回放宽值。
   - 读开关的时机是每个座位构造时：游戏里把 `AllClassesRide` 改成 0，已经生成的载具保持放宽，之后生成的按原版。
2. **乘员姿势**：把 `0x57AE8E` 的 `call 0x116C2D0` 改指向 `RideState`：先按座位的 `Animation` 播；被拒（乘员 CAS 没有这个状态）且开关开着时，改播 `vehicle_striker_rear`（四职业都有的坐姿）。只影响飞翼 / 剑兵骑摩托：坐姿、手不在车把上（L，要实机看观感）。
3. 两个钩子都先核对字节签名（`kSeatBuildCallSig` 等，见源码）与调用目标，不符就不装、写日志 `ALLCLASS ... not as read`。

## 6. 联机

- 判定在上车者自己的机器上（NPC 在其权威机器上，`npcai.cpp` 走 `0x5765E0`），对端收下上车消息时不检查职业（§1）。所以**只要想上车的那个人自己装了插件并开着 `AllClassesRide`**，就能坐；不要求全员都装，各端也不需要判定一致。
- 姿势是每台机器自己算的：没装插件（或关着）的机器上，飞翼 / 剑兵骑摩托显示为上车前的姿势（只是画面）。
- 没装插件的玩家自己仍按原版掩码（飞翼 / 剑兵上不了直升机等），这是他本机的判定。

## 7. 离线检查

- `tests/allclass_test.cpp`（ctest `allclass`）：真实的 `SeatBuild` / `RideState` 跑在私有镜像上（`0x62B430`、`0x116C2D0` 是跳到替身的跳板）：原版掩码 9 / 15 / 143 / 0 放宽结果、只放宽新追加的座位、开关关闭保持原版、普罗透斯封座 / 放开、插件判定与原版判定一致、姿势回退。
- `tests/allclass_guard.py`（ctest `allclass_guard`）：源码守卫（唯一写掩码处、普罗透斯封座语义、插件判定式、ini / README / 文档）。
- `tests/allclass_native_audit.py`（ctest `allclass_native`，需 `EDF6_NATIVE_DLL`）：源码里的签名逐字节对 EDF.dll、`0x62B430` 只有一个调用点、原版判定字节、`0x116C2D0` 的拒绝路径。
- `tests/allclass_census_check.py`（ctest `allclass_census`，需 `EDF6_GAME_DIR`）：§2 / §3 的普查（掩码集合、士兵掩码单职业位、[3] 是文本键、显示乘员的座位动画、回退姿势四职业都有）。

## 8. 要实机看的

1. 飞翼 / 剑兵 / 游骑兵 / 空降兵分别上：N9 Eros（506）驾驶座、Brute（410）驾驶座、摩托（503 / 613）、火箭车、普罗透斯驾驶座、卡车驾驶座：能上、能开、能下车，下车后职业能力（飞翼飞行、剑兵冲刺 / 双手武器）正常；日志 `ALLCLASS vehicle ... class mask 9 -> 15`。
2. 飞翼 / 剑兵骑摩托：坐姿是否可接受（`ALLCLASS rider CAS ... vehicle_striker_rear played instead`），穿模程度。
3. 普罗透斯重构 `ProteusTwoSeats=1`：右炮座 / 导弹座仍然谁都坐不进去，下车后四个座位恢复且四职业都能坐。
4. NPC 飞翼 / 剑兵小队登直升机 / 摩托（`npcai.cpp BoardSquad`），以及载具内换座（`SeatSwitch`）到原掩码 9 的座位。
5. 联机：只有一端装插件时，那一端的飞翼上直升机，另一端看到她在机上、能正常下车。
