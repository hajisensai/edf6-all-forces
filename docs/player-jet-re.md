# 玩家驾驶的战斗机（src/playerjet.cpp）逆向与设计笔记

EDF.dll TimeDateStamp 0x678CCB46，地址都是 RVA。置信度：**H** = 反汇编 + 实测（本仓已有功能在用），**M** = 反汇编读出、未在本功能里实测，**L** = 推断，需要实机确认。

## 1. 机体与身份

| 事实 | 来源 | 置信度 |
|---|---|---|
| 玩家机和 NPC 战机一样是 `Vehicle506_Helicopter`（vtable 0x17DB238），SGO 从 `V506_HELI` 派生 | jet.cpp / testrange/gen.py JETS | H |
| 用速度增益 k（`veh+0x162C`，SGO `mission_setup[1][0]` / `vehicle_setup[1][0]`）当标记：7201 战斗机、7202 攻击机。不在 jet.cpp `kKinds`（7001–7010）里，也不是潜舰（7101），所以 NPC 飞控不碰它 | jet.cpp IsJet、subcarrier.cpp | H |
| crew.cpp `Crew()` 对这两个标记不派 NPC（`IsPlayerJet`），空机一直停着 | 本次改动 | H（代码） |
| 每帧两段：slot 55 输入（原版之后，crew.cpp InputHook → `PlayerJetFrame`）算速度；slot 57 物理 0x61B710 串在 jet.cpp、subcarrier.cpp 之后，用 0x11B18F0 / 0x11B1760 写刚体线速度和角速度 | jet.cpp 同款 | H |
| 机身部件索引 `veh+0x1530` 在换了外形的模型里是 -1，坠毁流程不检查就读；用 0x6EA4B0 按 `bomber501` / `bomber401` / `body` 找回来 | subcarrier.cpp 同款 | H |
| 移动区域收缩 `veh+0xE00` 驾驶时设 -1e6（不被空气墙夹回），下机还原 | docs/map-edge-re.md | H |

## 2. 输入

| 事实 | 置信度 |
|---|---|
| 座位 0 的摇杆块：LX `+0x2C0`、LY `+0x2C4`、RX `+0x2D0`、RY `+0x2D4`、上升扳机 `+0x2E0`（手柄模拟量 0..1，键盘 0/1）；主射击 `+0x2E4`、副射击字 `+0x2E8` 的 0x20 位由原版 506 第 55 槽自己转成开火（机炮 0/1、导弹 2），插件不动 | H（docs/heli-input-re.md） |
| 机体输入块 `+0x1540` 横移(-LX)、`+0x1544` 油门、`+0x1548` 前进(-LY)、`+0x154C`=1、`+0x1550` 偏航(-RX)；驾驶时每帧清零（无旋翼升力、无直升机操纵） | H（jet.cpp 同款） |
| 朝向角 a 的机头 = (sin a, 0, cos a)（0x4CD10），偏航 = -RX 加到 a 上：RX>0 右转、a 减小；所以「右」= (-fwd.z, 0, fwd.x)。LX>0 同样向右 | M-H |
| 摇杆往前推 Y 为负（前进 = -LY）。俯仰按同样约定：-RY = 拉杆抬头；鼠标上下映射到 RY 的符号和幅度**没实测**，`PlayerJetInvertPitch=1` 可反过来 | M / L |
| 油门：左摇杆前推（LY<-0.3）或上升键（>0.5）加，后拉减，每秒 0.6，松手保持 | 设计 |
| 转弯 = clamp(RX+LX)；地面是机头轮转向（0.8 rad/s，25 m/s 以上按速度递减），空中是压坡度拉杆 | 设计 |

## 3. 飞行模型（街机，`kKinds`）

| | 战斗机 7201 | 攻击机 7202 |
|---|---|---|
| 最低空速 / 起飞 / 最高（m/s） | 65 / 75 / 195 | 60 / 70 / 180 |
| 推力 / 减速（m/s²） | 16 / 20 | 11 / 15 |
| 最大过载 / 滚转率 | 7 g / 2.6 rad/s | 5 g / 1.6 rad/s |
| 无损接地最高速 | 130 | 120 |

- 空中：速度方向被升力弯折（升力 ≤ maxG，满舵转弯用 90%，其余抵消重力），机身上方向沿升力（压坡度），俯推时机身保持至少 0.35 g 的「上」（不倒飞）；爬升/俯冲最陡约 70 度（sin 0.94），不翻筋斗；爬升按 g·sinθ 掉速、俯冲加速，超过 1 g 每 g 掉 3 m/s²；速度夹在 [minAir, min(top, 195)]（Havok 刚体约 200 m/s 上限）。不失速。
- 地面：沿机头水平滑跑，油门 0 时 12 m/s² 刹停，停下且油门关闭 = parked（插件不写速度，原版代码把它放在原地）；到起飞速度拉杆（俯仰 >0.2）离地，或油门 ≥60% 且再快 20 m/s 自动离地；从边缘开出去（离地 >6 m）进入空中。
- 接地（离地 <3 m 且在下沉，或沿速度方向 3 帧内会碰到）：下沉 ≤10 m/s、机身 up.y ≥0.77、机头俯角不超过约 15 度、速度 ≤ 无损接地速度 → 降落转滑跑；否则坠毁扣血，并把下沉截到地面上方。
- 撞建筑：空中 150 ms 内实际位移不到下达速度的一半（且 >40 m/s）→ 按损失速度坠毁扣血，水平速度反向、以最低空速弹开。
- 天花板（`*(image+0x20B2998)+0x3C`）下 12 m 不再上升；±2.4 km 世界边界外向分量清零。
- 舵面：模型有 `elevon_L/R` 骨骼时照 jet.cpp 动（攻击机外形有，战斗机的截击机外形没有）。

## 4. 坠毁伤害与死亡

| 事实 | 置信度 |
|---|---|
| HP `veh+0x2F8`、最大 HP `veh+0x2F4`、死亡字节 `veh+0x2E8` | H |
| 扣血 = 最大 HP × clamp(0.2 + 0.04×(下沉-10) + 0.01×(速度-无损速度) + 坡度过大 0.3, 0.2, 1.5)，1 秒内只算一次 | 设计 |
| 0x6329B0(vehicle) 是消息处理 0x62ECB0 收到 0x1000000F 时走的死亡流程：踢出所有座位、HP 置 min(0, max)、置死亡字节、jmp 0x62F200（爆炸/残骸）。HP 扣到 ≤0 时插件直接调它（字节签名 `48 89 5C 24 08 57 48 83 EC 20 48 69 81 18 06 00 00 40 03 00 00` 校验通过才调，失败则只把 HP 写 0） | 反汇编 M，**从 slot 55 内部调用的安全性 L** |

## 5. 数据（SGO / 呼叫武器）

| 事实 | 置信度 |
|---|---|
| `testrange/gen.py` JETS 新增 `edf6tr_pjet_fighter_mission`（截击机外形 bomber501_2，耐久 1400）、`edf6tr_pjet_strike_mission`（带舵面 bomber501，耐久 2200），`player=True`：除 `mission_setup` 外再写一份同内容的 `vehicle_setup`，并把 `game_object_camera_setting` 的偏移改成 (0,6,-24) / (0,8,-32)（原版 506 是 (0,5.5,-11.5)） | 生成 M；镜头字段含义 **L**（只按 V506_HELI 的值推断是相机偏移） |
| `tools/make_jets.py` 写 `Mods/OBJECT/EDF6VC_PJET_FIGHTER.SGO` / `EDF6VC_PJET_STRIKE.SGO` | H（代码） |
| `tools/call_weapons.py` 追加 2 行（共 19）：`EDF6VC_CALL_PJET_FIGHTER` / `_STRIKE`，复制原版 eWeapon394（N9 Eros 载具请求，Weapon_Sub，类别 308）：`Ammo_CustomParameter[4]` = [运输机, 箱子, 载具 SGO, 载具设定]，载具 SGO 换成玩家机，设定 `[1][0]`（k）换成标记，机炮换成 `edf6vc_jet_gun_l/r`，`resource` 同步替换；装填 6000 / 6500 | 生成 H（离线 build 已核对）；运输机能否把这个派生 SGO 正常投下 **L** |
| 名字带 `EDF6VC_CALL_` 前缀，airstrike.cpp 读档时一并设为已拥有；airstrike 只拦 Weapon_RadioContact 的标记，这两行（Weapon_Sub）不受影响，也不进 `]`/`[` 切换 | M |
| 一个 SGO 同时有 `vehicle_setup` 和 `mission_setup`：请求武器用的是武器里自带的设定，任务放置用 `mission_setup` | **L** |
| 启动器「NPC 驾驶」栏里的玩家机也按空机放（`placements`），否则 NPC 坐上去会被直升机飞控按 k=7201 乱飞 | H（代码） |

## 6. 待实机确认

1. RY / 鼠标上下的符号与幅度（默认 -RY = 抬头）；键盘玩家用什么键俯仰。
2. 镜头偏移是否就是 `game_object_camera_setting[1]`，距离是否合适。
3. HP 扣到 0 时调 0x6329B0 是否安全（会不会在本帧的输入步骤里踢人导致崩溃）。
4. 载具请求能否把玩家机送到并正常卸下；燃料（20000，耗 1.5）耗尽后原版会不会强制它下坠（插件写速度，应该无影响）。
5. 地面滑跑与接地判定的高度（机体原点离地约 1.3 m 的假设）。

日志（`Debug=1`）以 `PJET` 开头：`boarded`、`takeoff`、每 2 秒一行状态（相位、速度、爬升率、油门、离地高度、HP、以及座位原始 LX/LY/RX/RY/上升量）、`landed`、`crash`、`blocked`、`destroyed`、`left`。
