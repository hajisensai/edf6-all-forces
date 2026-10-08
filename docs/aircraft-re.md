# 直升机参数、喷气机可行性与运行时生成/删除：逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。全部是静态分析，没有启动游戏。
可信度：**H** = 反汇编直接读出；**M** = 由代码推导，或部分对上了实测；**L** = 推测。
`veh` = 直升机对象（`VehicleHelicopterBase` vtable `0x17DF790`；V506 的 vtable `0x17DB238` 包了一层）。

## 1. 参数从哪来

第 46 槽 `0x6530E0` 负责初始化，读 SGO 时 `rdi = veh+0x1580`。

### `vehicle_setup[1]`

任务里改名为 `mission_setup[1]`。加载函数 `0x655EF0`，共 7 个 float，依次写入 `+0x162C..+0x1644`（H）。

| 下标 | 字段 | 含义 | V506 |
|---|---|---|---|
| 0 | `+0x162C` | 速度增益 k | 80 |
| 1 | `+0x1630` | 水平混合系数 b | 0.0003 |
| 2 | `+0x1634` | 最大偏航，度 × 0.0174533 | 23.5° |
| 3 | `+0x1638` | 偏航平滑 | 0.0011 |
| 4 | `+0x163C` | 着地时旋翼上升速率倍数 | 1.0 |
| 5 | `+0x1640` | 最大倾角，度 × deg2rad | 35° = 0.611 |
| 6 | `+0x1644` | 倾角平滑 | 0.005 |

- `vehicle_setup[2]` = `[999900, 1.666]`，经 `0x5EFA70(veh+0x1690)` 成为燃料（H）。
- 第 46 槽还把 `0x162C`、`0x1634` 复制到 `veh+0x1B80`（经 `0x5F9900`）。可能是 HUD 或其它显示用，L。

### `heli_movement`

加载函数 `0x6574F0`，第 46 槽在 `0x64E9A6` / `0x650A98` 调用（H）。

| 元素 | 字段 | 含义 | V506 |
|---|---|---|---|
| [0][0] | `+0x1614` | 水平阻尼 d | 0.999 |
| [0][1] × 1/60 | `+0x1610` | 每单位旋翼的升力 L | 34/60 = 0.5667 |
| [0][2] | `+0x1618` | 垂直阻尼 | 0.95 |
| [0][3] | `+0x161C` | 质量 / 重力因子 | 1.0 |
| [1][0] | `+0x1620` | 姿态弹簧增益 | 0.15 |
| [1][1] | `+0x1624` | 角速度混合 | 0.125 |
| [1][2] | `+0x1628` | 未知 | 0.05 |

这一表**更正**了旧文档里“`+0x1610` = 70”的说法。

### `heli_roter`

加载函数 `0x650D40`，填旋翼结构 R = `veh+0x1BC8`（H）。

| 字段 | 来源 | 含义 | 值 |
|---|---|---|---|
| R+0 | [0] | — | 7.0 |
| R+4（`veh+0x1BCC`） | [1] | 上升速率 | 0.001 |
| R+8（`veh+0x1BD0`） | [2] | 下降速率 | 0.0007 |
| R+0xC | [3] | 怠速 | 0.13 |
| R+0x10 | [4] | — | 1.65 |
| R+0x14 / R+0x18 | [5] | — | [0.015, 0.25] |
| R+0x1C | [6] | — | 1.0 |
| R+0x30（`veh+0x1BF8`） | 运行时 | 当前转速 | — |

## 2. 第 57 槽 `0x6519A0` 如何使用这些参数

### 水平速度（H）

- 期望速度 = k ·（横向输入 · row0 + 前向输入 · row2）。row0 / row2 是只含航向的基，位于 `veh+0x15C0` / `+0x15E0`。
- 每帧对 x、z 分量：`v' = d·v + b·(desired − d·v)`。
- 稳态速度：`v∞ = b·k / (1 − d(1−b))`。时间常数：`τ = 1/(1 − d(1−b))` 帧。
- V506：v∞ = 18.5 m/s，τ = 769 帧 = 12.8 s。30 s 时约 16.7 m/s，**与实测约 17 m/s 吻合**（M→H）。
- 接触位 bit1 置位（着地）时，本帧不改水平速度。
- 第 57 槽里没有硬性速度上限。Havok 可能有最大线速度，未查（L）。

### 旋翼（`0x656660`，H）

- `speed += rate·(target − speed)`：上升用 R+4（着地时再乘 `0x163C`），下降用 R+8。
- 引擎开着、燃料 > 0 且转速低于怠速时，目标转速强制为 1。燃料 ≤ 0 时目标为 0。
- 燃料消耗跟转速挂钩，经 `0x5EF8E0`。

### 垂直（M，假定 Havok 以 1/60 施加重力）

- `hover = g·M/(60·L)` ≈ 0.288。其中 L = `0x1610`，M = `0x161C`。
- `t = clamp((rotor − idle)/(hover − idle), 0, 1)`。
- `vy' = lerp(1, 0x1618, t)·vy + rotor·L`，在 `0x651F47` 经 `0x11B18F0` 写回。
- 最大爬升 ≈ `(L − g·M/60)/(1 − 0x1618)` ≈ 8 m/s。

### 姿态（`0x654A80`，rbx = `veh+0x15C0`，H）

- **2026-10-08 更正：** `+0x1604` 是滞后的目标航向角偏移，不是 rad/s。`yawOffset = lerp(yawOffset, maxYawAngle·yawIn, 0x1638)`；`0x654ECA` 将它加到目标航向，`0x6CE9C1` 才乘姿态增益并除以 1/60 得到角速度，随后还经过 `+0x1624` 的角速度混合。上文把 `+0x1634` 标为“最大偏航”的数值不能直接当成角速度上限。
- `pitch(+0x1600) = lerp(pitch, maxTilt·fwd, 0x1644)`。
- `roll(+0x1608) = lerp(roll, −maxTilt·lat, 0x1644)`。
- 任何接触（bit0）都会把俯仰和横滚清零；bit1 时跳过输入。
- 目标姿态 = 航向 + yawRate，再叠加俯仰和横滚。随后调用 `0x6CE8D0(body, &rot, 0x1620, 0x1624)`：
  - `ωt = 轴角·0x1620·60`
  - `ω' = ω + (ωt − ω)·0x1624`
  - 经 `0x11B1760` 写入。
- 倾角只影响外观，因为速度是直接写的。
- 实测偏航（maxYaw 0.26–0.70 对应 45–70°/s）比 maxYaw 的线性增长慢，说明姿态弹簧 `0x1620/0x1624` 也在限速（M）。

玩家控制现在按实际 `yawOffset`、混合系数和姿态增益反算下一原生步所需输入。`tests/heli_yaw_native_audit.py` 在私有映像执行完整 `0x654A80` 和 `0x6CE8D0`；Havok 读写角速度由夹具记录，旋转矩阵按记录角速度积分。Brute 的鼠标停止场景，旧控制器在第 20–30 秒仍有最大 69.152° 航向误差，修复后 0.004°。这不等于游戏内或双机验收。

## 3. 调参建议

记 d = `0x1614`，b = `0x1630`，k = `0x162C`。

**极速**
- 只把 k 放大 S 倍：v∞ 也放大 S 倍，τ 不变。
- 副作用：起步阶段加速度同样放大 S 倍，但到达极速的时间不变（12.8 s）（H）。

**加速**
- 目标时间常数 T 帧：`b' = 1 − (1 − 1/T)/d`。
- 想同时保持极速 V：`k' = V/(T·b')`。
- 例 1：37 m/s 且 T = 180 帧 → b' = 0.00456，k' = 45。
- 例 2：120 m/s 且 T = 300 帧 → b' = 0.00234，k' = 171。
- 注意：单独调大 b 也会提高极速（H）。

**偏航**
- 转向手感主要由 `0x1638` 决定，0.0011 时 τ ≈ 15 s。可提到 0.005–0.02。
- 角速度上限：调 `0x1634`；若已饱和，再加 `0x1620`（以及 `0x1624`）。
- 副作用：`0x1620/0x1624` 同时控制俯仰和横滚回正，过大可能抖动（M）。

**倾角**
- `0x1640` 和 `0x1644`，只影响外观（H）。

**爬升**
- 把 `0x1610` 和 `0x161C` 同比放大：悬停转速不变，最大爬升按比例增加（M）。
- 想更快到达最大爬升，加大旋翼上升速率 `veh+0x1BCC` 和下降速率 `+0x1BD0`。
- 副作用：转速升得更快，燃料消耗也更快。

**写入时机**
- 这些字段只在第 46 槽初始化时从 SGO 读一次，之后每帧直接读字段。所以插件在生成后、或在第 57 槽之前写字段即可生效（H）。

## 4. 喷气机可行性

### 现有模型（H）

| SGO | 类 | 刚体 / 直升机参数 |
|---|---|---|
| BOMBER401 | BombingPlane | 无 |
| BOMBER501 | BombingPlane | 无 |
| BOMBER501_2（KM6 / Kamuy 的 Ammo_CustomParameter 引用） | BombingPlane | 无 |
| V508_TRANSPORT | Transporter508，带 boost | — |
| E508_CARRIER | 敌方 | — |
| PD607_DRONE_AIRSTRIKE | — | — |

- BombingPlane 的 SGO 只有 animation_model、camera、durability、class，**没有刚体，也没有直升机参数**。
- DEMOAIRSTRIKE* 使用 Bomber401，速度 3.0，高度 150。
- 鲸鱼炮艇（DEMOGUNSHIPFIRE*）是 DemoIndirectFire，画面外开火，**没有模型**（M）。

### 复用直升机类会坏在哪里

**速度**
- 改 k/b 能做到 80–150 m/s，代码里没有速度上限（H）。
- Havok 可能有最大线速度上限，以及高速穿模问题（L）。

**天花板 / 地图边界**
- 第 55 槽 `0x6543A0` 每帧执行：
  - 天花板 Y 读自 `[*(0x20B2998) − 8 + 0x44]`；
  - 再调用 `0x5A9E50(区域管理器, &pos, −veh+0xE00, 3)` 把位置推回边界内；
  - 位置有调整时，用 `0x11B1A00` 写回。
- 喷气机会被压在天花板下（H：读法；M：语义）。
- 绕开方法：第 55 槽已被插件串接，可以在原版之后再改位置，或改天花板值（L）。

**接触**
- bit0 会把俯仰和横滚清零，bit1 会冻结水平速度，`heli_contact_damage_scale` 会造成接触伤害。
- 后果：贴地高速时手感会断，撞击伤害会大（M）。

**旋翼骨骼**
- 直升机的骨骼映射 `0x650560` 要找 `rotor` / `Roter` / `roter_speed` / `roter_up` / `RoterRoll` / `Body`。
- 换成 BombingPlane 的模型很可能缺这些骨骼（M）。是缺了就跳过，还是会崩，未验证（L）。

**直接运动学控制**（body = `*(veh+0x1650)`）

| RVA | 函数 | 可信度 |
|---|---|---|
| `0x11B1A00` | SetPosition(body, vec4*) | H |
| `0x11B18F0` | SetLinearVelocity | H |
| `0x11B1760` | SetAngularVelocity | H |
| `0x11B1300` | GetLinearVelocity | H |
| `0x11B1060` | GetAngularVelocity(body, out) | H |
| `0x11B1960` | SetMotionType（0 = static，1 = keyframed，2 = dynamic） | M |

- 推荐做法：保持 dynamic，在第 57 槽**之后**用 `0x11B18F0` 覆盖线速度、用 `0x11B1760` 覆盖角速度（M）。
- 姿态的 setter 没找到（候选是世界接口 vtable 的 +0x88 / +0x98，L），所以转向只能靠角速度。

### 结论

- **可行，但只能作为“高速直升机”**：用 V506 的壳、改参数再加速度覆盖，难点是天花板和接触。
- 想要真正的战斗机外观，需要换模型，旋翼骨骼映射是风险。
- BombingPlane / Bomber501_2 只能当**纯视觉**的替代方案（没有刚体，不能驾驶）。

## 5. 运行时生成与删除

### 脚本 native

签名：`Object* fn(Ctx* this, Object* ret, const wstring& point, const wstring& sgo, float level[, bool])`（H）。

| 脚本函数 | RVA | 说明 |
|---|---|---|
| CreateFriend | `0x1B0310` | — |
| CreateVehicle2 | `0x1B28C0` | — |
| CreateVehicle | `0x1E2BB0` | 以固定 level 调用 `0x1B28C0` |
| SetupVehicle | `0x1F5D70` | — |
| RideVehicle | `0x1F53C0` | — |
| Vehicle_RideAi | `0x1CC170` | — |
| SetPosition(string) | `0x1C3480` | — |
| DeleteObject | `0x1E2BE0` | 调用 `0x1BFA80` |
| Object.Delete() | `0x1BFA80` | — |

### 生成流程

1. 用 `0x6F83B0(*(0x20B28A8)+0x10, &mat, pointName)` 解析地图点，得到 mat4（H）。
2. 填描述符（H）：

   | 偏移 | 内容 |
   |---|---|
   | +0x00 | mat4 |
   | +0x40..0x78 | std::function（impl vtable `0x1791E18` 是捕获 `const wstring* sgo` 的 SingleObjectPath lambda，impl 指针在 +0x78） |
   | +0x80 | float level |
   | +0x84 / +0x85 / +0x86 | 字节 |
   | +0x88 | int 队伍（CreateFriend = 2，CreateVehicle2 = 5，默认 −1） |
   | +0x90 | 指针 |

3. 调用 `0x1D8900(ctx->+8, &outSharedPtr, &desc)` 创建对象，level 会乘上 `mgr+0x280+team·4` 处的队伍系数（H）。
4. 动态转换为 VehicleBase（H）。
5. 按入口区分后续处理（H）：
   - **CreateFriend**：`obj+0x540` = bool 参数，再以 dl=1 调第 50 槽 RideAi（`+0x190`）。
   - **CreateVehicle2**：调 `0x633280(veh, f(level))`，即空投方式，`+0xE30 = 2`。

**Ctx**

- Ctx = `mission_script::GlobalFunctions`，由构造函数 `0x1A92C0` 创建，`ctx+8 = *(MSAI+0xE8)`（任务对象）（H）。
- 它挂在 `MissionScriptASImplement+0xF8`，而 MSAI 由 `0x1DD520` 创建（H）。
- **没有找到全局单例**。

**插件做法（M）**

1. hook `0x1A92C0`，记下 rcx，即每局的 ctx。或者 hook 任一常用 native 截获 `this`。
2. 换任务时清空这个指针。
3. 先确保 SGO 已经 Preload（任务脚本里有 `Preload`；未预载会怎样未验证，L）。
4. 在游戏线程调用 `0x1B0310(ctx, &ret, L"点名", L"app:/object/V506.sgo", level, false)`。
5. 想要任意位置：用任意已有点名生成，再用 `0x11B1A00` 设位置；或者自己构造描述符直接调 `0x1D8900`（得复制 std::function 的布局，风险高）。

### 删除

- `0x1BFA80` 把脚本 Object 视为 `{?, obj(+8), weak 控制块(+0x10)}`：先 lock 这个 weak，再调用 `0x118A1B0(obj)`（H）。
- `0x118A1B0(GameObject*)` 的流程：
  1. `obj+0x18` 的 bit2 表示已移除，置位就直接返回；
  2. 调用 vtable `+0x40`（第 8 槽，移除回调）；
  3. 清 `+0x1A`；
  4. 把 `obj+0x28/+0x30` 的 shared_ptr 交给 `0x1195E80(*(0x20B2958), &sp)` 注销（H）。
- **插件做法**：在游戏线程调用 `0x118A1B0(veh)`，它本身是幂等的（H）。
- 删除前建议先用 SeatKick `0x62E1A0` 清掉座位上的 DummyVehicleRider（消息 `0x10000015` 会让它死亡、消失），避免留下孤儿乘员（M）。
- 如果玩家正坐在上面，应先让玩家下车（`0x62D350`，L）。
