# 地面载具 AI 逆向：格雷普斯（Vehicle_Car）与深渊爬行者（502_GroundRobo）

EDF.dll TimeDateStamp 0x678CCB46，地址全部是 RVA。「确认」= 反汇编/表项直接读到；「推断」= 由代码结构推出、尚未进游戏验证。

## 背景：坦克为什么会自己动

- CarBase 的 AI 不是每帧直接调用 slot 72。CarBase slot 6 `0x6731C0` 通过 vcall thunk `0x65F2B4`（`jmp [rax+0x240]`，即 slot 72）把 AI 函数注册进 veh+0x25A0 的 ActionTable 状态机。坦克共用的 AI action 是 `0x661440`。（确认）
- RideAi（VehicleBase slot 50，`0x633030`）往 0 号座放一个 DummyVehicleRider（vtable `0x17D7320`）。这个乘员**从不写**座位摇杆块（seat+0x2C0..0x2E8），载具是否会动完全取决于载具类自己有没有 AI action。（确认）
- 插件 `Crew()` 只对 `kClasses` 表里登记、且输入槽被 hook 的类运行，这是派 NPC 的唯一入口。（确认，src/crew.cpp）

## 格雷普斯（武装装甲车）

| 项 | 值 |
|---|---|
| 武器 | `EVEHICLE_STRIKER01`（Weapon_Sub，召唤） |
| SGO | `OBJECT/VEHICLE401_STRIKER.SGO`（及 `_MPACK2`），`xgs_scene_object_class='Vehicle_Car'` |
| 类 / vtable | `Vehicle_Car`，**0x17E01B0**，84 槽（确认） |
| slot 4 | `0x65A850`：先调 CarBase 预更新 `0x673A40`（内部调 slot 55），再把炮塔块 +0x2980 交给 seat0 的 VehicleWeaponAim（确认） |
| slot 6 | `0x6731C0`（CarBase，注册 AI action）（确认） |
| slot 49 | **`0x65B910`**，不是 stock FindSeat `0x633B80`：全局联机/选项条件成立且座位数 ≥5 时按首选座位顺序（`0x633AE0`）找座，否则调 stock `0x633B80`（确认） |
| slot 50 | `0x633030`（stock RideAi）（确认） |
| slot 55 | `0x65A390`，输入函数 `(veh, bool dl, r8 = 驾驶块 veh+0x1BE0)`；dl=0 时清零 +0x2980 与 +0x29B1（确认） |
| slot 72 | `0x661440`，与所有坦克相同的 CarBase AI（确认） |
| +0x1AD0 | 驾驶模式：slot 67 `0x677540` 置 1，slot 69 `0x6775A0` 置 2，slot 68 `0x6777C0` 置 dl（确认） |

**根因（确认）**：游戏本身**有** AI。问题在插件：`InstallCrew` 要求 `kClasses` 每个类的 slot 49 都是 stock `0x633B80`，Vehicle_Car 的 slot 49 是 `0x65B910`，所以它从未登记进 `kClasses`，`Crew()` 从不对它运行，也就从不派 NPC。

**修法**：`kClasses` 新增 `{0x17E01B0, 0x65A390, "Car", findSeat=0x65B910, slot 55}`（当时带 `armedOnly`，2026-10-07 删除，见下）；FindSeat hook 改为按类调用各自的原 slot 49；`InstallCrew` 对 slot 49 不符的类改为记日志跳过（不再整体失败）。同一 vtable 还被 `V512_KEITRUCK`、`V513_TRAILERTRUCK01CAB` 使用，二者 `vehicle_setup` 武器表为空，所以加 `armedOnly`：武器 holder 数 veh+0x648 为 0 的不派人。

**推断/待验证**：NPC 坐上后由 `0x661440` 驾驶（与坦克同一 action），炮塔由 seat0 的 VehicleWeaponAim 瞄准开火——未进游戏验证。

同因被排除、2026-10-07 已登记（用户：「所有载具都要支持 ai」）：607 RoboTruck（0x17DCAB0）、60X Truck（0x17DCFB8），slot 49 都是 0x65B910、slot 55 都是 Car 的 0x65A390，slot 6（0x6234E0 / 0x6266B0）第一步就调 CarBase 的 0x6731C0（确认），所以 AI action 0x661440 照样注册；507 Rescuetank（0x17DB590，slot 4 是 TankBase 的 0x67FAD0，slot 55 0x61BFD0）slot 49 是 0x61D310：与 0x65B910 同形，全局选项不成立时尾调 0x633B80（0x61D50D），否则按 0x633AE0 的首选顺序找座（确认）。同日 `armedOnly` 删除：没有武器的车（轻卡、拖车头、卡车、救援车）也派 NPC 司机，0x661440 原版就给任务里的无武器卡车沿路线开车用；没有路线时由 npcpost.cpp 按驻守点 / 地图命令开车（推断：未进游戏验证）。

全部载具类的 vtable（2026-10-07，按 slot 50 = RideAi 0x633030 扫 .rdata）：slot 72 = 0x661440 的是 402 / 403 / 404 / 503 / 505 / 507 / 510 / 511 / 601 / 603 / 607 / 60X / Car（含 BikeBase、CarBase、TankBase）；54 槽的 501 FortressRobo、502 GroundRobo，57 槽的 504 begaruta、Begaruta、612 nix，62～64 槽的直升机 506 / 409 / 410 / HelicopterBase 没有这个 AI action。

## 深渊爬行者（Depth Crawler，デプスクロウラー）

| 项 | 值 |
|---|---|
| 武器 | `GroundRobo01`、eWeapon379–386 等 |
| SGO | `OBJECT/VEHICLE502_GROUNDROBO.SGO`（及 `GROUNDROBOGOLD`） |
| 类 / vtable | `Vehicle502_GroundRobo`，**0x17DA028**，主表 54 槽，同 VehicleBase 0x17DD720（确认） |

旧表里它的「slot 55」`0x610F8C` 其实是 veh+0x120 NetworkObject 副表里的调整 thunk（`sub rcx,0x120; jmp 0x6110C0`），「slot 72」`0x6325B0` 同样是副表。kClasses 里它的 input 填 0，输入从未 hook，`Crew()` 从不运行。（确认）

**第二层根因（确认）**：机器人代码里完全不访问 AI 字段（0x518/0x4A8/0xE08/0xE09/0xE10/0x25A0 均未出现），没有 AI action。即使 RideAi 放了 NPC，它也不会动。所以只补登记不够，需要插件侧驾驶器（src/ground.cpp）。

> 附带更正：re-notes 里「机甲 AI 是 slot 72 的 0x773B50」不对——Begaruta/612 的表只有 57 槽，那也是读到了副表。501_FortressRobo（0x17D98C8）同为 54 槽表、同样问题，本次未处理。

### 输入数据流（确认）

slot 4 = **`0x612D20`**（只有 this）：先调 VehicleBase `0x62EEC0`，然后**不检查乘员**地把 seat0 摇杆块拷进载具：

| 偏移 | 内容 |
|---|---|
| +0x15F0 vec4 | (-LX, 0, -LY, 1.0) 移动 |
| +0x1600 vec4 | (-RY, -RX, 0, 1.0) 视角 |
| +0x1610 | 按钮 bit0\|bit1（跳） |
| +0x1611 | RT(+0x2E4) > 0.5 → holder 0（机炮） |
| +0x1612 / +0x1613 | bit4 / bit5 → holder 1 / 2（左右臂炮） |
| +0x1614 | LT(+0x2E0) > 0.5（侧冲） |

之后调 `0x116DFB0(veh+0x1170)`，把 {veh+0x1170, +0x1530} 加进全局队列。

slot 5 = `0x614510`（物理），由 +0x1620 标志门控（构造函数 `0x610350` 在 `0x6108BB` 设初始状态 `0x616BB0`，+0x1620 = 0x1F，无「有人驾驶」门槛）：

- bit1：视角向量 × +0x17EC → +0x1AF0，平滑到 +0x1B00；y 分量（来自 -RX）传给 `0x6DB1B0(veh+0x1810)` 转身。
- bit0：移动向量 × +0x17E8 → +0x1AC0，平滑到 +0x1AD0；速度 = x·row(+0x60) + y·row(+0x70) + z·row(+0x80)，按车身矩阵行投影。
- bit3 且 +0x1610：跳（`0x615C10`）；bit4 且 +0x1614 且 LX 过阈值：侧冲。
- bit2：i=0..2，byte[+0x1611+i] 非零就开第 i 个 holder（`0x62C000`，holder 数组 veh+0x638，步长 0x48）。
- 俯仰：+0x1B10 += +0x1600.x × 0.02（每帧弧度，常量在 `0x614D16`），夹在 ±π/2；动画参数 = pitch/π + 0.5（`0x116DE10`）。

`0x615660` 是联网复制，不是状态逻辑。

### 插件驾驶器（src/ground.cpp）

kClasses 的 502 项改为 `{0x17DA028, 0x612D20, slot 4}`：在 stock slot 4 之后、slot 5 之前，对 seat0 是 dummy 乘员的爬行者重写输入块：

- 移动：目标点的水平方向投影到车身 row0(+0x60)/row2(+0x80)，写 +0x15F0.x/.z；力度按 (距离-停止距离)/10 m 渐变，带 5 m 滞回。车身行投影由 slot 5 自己做，**符号按构造正确（推断）**。
- 转身：写 +0x1604，符号初值 +1，按航向实际变化在线投票学习（同 heli.cpp 偏航）。（世界方向符号为推断，靠学习纠正）
- 俯仰：写 +0x1600，用 `GunBarrel` 测到的炮管仰角闭环；+0x1B10 与炮管仰角的关系在线学习，卡在 ±π/2 不收敛 1 秒就翻转符号；无目标时回到 0。
- 开火：holder 0..2 分别判断——射程（速度×寿命）内、炮管锥角在 max(atan(3/距离), ~3°) 内、地图射线通、玩家不在弹道上，就置 +0x1611+i。
- 跳、侧冲（+0x1610/+0x1614）恒为 0。
- 行为：无敌人跟随玩家停在 GroundFollow 米；GroundRange 米内有敌人就转向、逼近到最长射程的 70%（至少 25 m）并开火；离玩家超过 GroundLeash 米就回到玩家身边。
- profile：`0x612D20`、`0x612D85`、`0x61468B`、`0x614862`、`0x614D0E` 字节签名不符则整个驾驶器关闭。

**全部待进游戏验证（推断）**：holder 1/2 的武器是否对应左右臂；`GunBarrel` 能否给出机器人臂炮的炮口；转身符号与俯仰符号的学习能否收敛；移动速度与地形（爬墙时航向无定义，代码在车身前向水平分量 < 0.3 时停止转向学习）。
