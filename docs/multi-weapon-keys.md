# 载具多开火键与自动炮塔（2026-10-30）

用户原话（2026-10-30，评 X-Morph 移植时）：「所有载具都要注意的点，不是只有两个攻击按键的，很多东西可以同时发射，不一定需要切换副挂载才行。多加几个按键的事情。还有一些炮塔可以是自动无人炮塔（需要可开关）或者一些应该是独立的座位」。

落地三项：**多开火键**、**自动炮塔**、**独立炮手座**（V603 车型，见「独立炮手座」）。

## 登记表字段（`pylib/vcobjects.py` `Store`）

替换原来的 `independent_secondary`：

| 字段 | 取值 | 含义 |
|---|---|---|
| `key` | `0` | 默认。挂载跟主武器循环（R / LB 切换），或是原生武器。 |
| | `2`（`FIRE_SECONDARY`） | 座位副扳机（鼠标右键 / LT）。只在副扳机原版不读的车型上生效（目前 V603：原版 `0x6214A4` 只读主扳机）。同一辆车上多个 `key=2` 的武器，视为**同一个发射器的不同弹种**，用 `VehicleSecondarySwitchKey`（T / 十字键右）循环切换，例如 BMPT 的 9M120 与 9M120F。 |
| | `3`..`6` | 额外开火键 `VehicleFire3Key`..`VehicleFire6Key`。每个键在一辆车上只能挂一个武器（`vehicle_fire_keys` 在导入时检查）。任何车型都可用：插件挂载的 holder 不会被任何车型的原版开火读到，扣扳机的只有这个键。 |
| `auto` | `True` | 自动炮塔。必须同时设 `key≥2` 或 `seat≥1`，自动炮塔关掉时就用这个键 / 这个座位手动发射。 |
| `seat` | `0` | 默认：武器挂在驾驶座（座位 0）。 |
| | `1`..`7`（`MOST_GUNNER_SEAT`） | 炮手座 n 自己的武器：holder 挂在座位 n（武器设置行 `[骨骼, n]`），由该座位的攻击键（主扳机）发射、由坐在上面的人瞄准。必须同时设 `turret`、不设 `key`。目前只对 V603 车型生效。 |
| `turret` | `Turret(yaw, pitch, low=-5, high=70, arc=180, yaw_rate=60, pitch_rate=45)` | 这件武器自己的炮塔：模型里的偏航骨骼与俯仰骨骼名（各 ≤15 字符，两者都是 `body` 的叶子骨骼，前 16 字符互不相同）、俯仰下限 / 上限（度）、左右各可转多少（度，180 = 全向）、转速（度/秒）。武器的 holder 骨骼应是俯仰骨骼，弹道跟着它走。 |

所有开火键互相独立，可以同时按住：主扳机、副扳机、键 3～6 各打各的，不需要切换。

X-Morph 单位：`pylib/xm_units.py` `Arm.fire`（对应 `Store.key`，默认 2）、`Arm.auto`、`Arm.seat`、`Arm.turret`（`Turret` 的数值参数，骨骼名由 `turret_bones(arm.key)` 定为 `xm_<key>` / `xm_<key>_gun`），由 `vcobjects._xm_arm` 透传。同一个 unit 的 arms 各用不同的非零键；炮手座 arm 设 `fire=0`、`seat` 从 1 起按顺序编号、必须有 `turret`（`tests/xmorph_registry_guard.py` 检查）。`tools/prep_xmorph.py` 按 `turret_bones` 把 arm 的俯仰子树（炮管）挂到 `xm_<key>_gun`、其余（炮座）挂到 `xm_<key>`，两者作为 `body` 的可见叶子骨骼导出；`tools/make_tracked_combat.py` 给车辆加对应的乘坐位置（复制驾驶座那一行，`410_HELI_GUNNER_L` 文案），并在模型上加定位器 `xm_seat_n`（挂偏航骨骼）与 `xm_cam_n`（挂俯仰骨骼，炮手镜头）。

示例：

```python
# 一个车型：主炮（原生两门）+ 机枪塔（副扳机）+ 防空导弹塔（键 3，自动炮塔）+ 火箭巢（键 4）
arms=(Arm('mg', ..., fire=2),
      Arm('aam', ..., fire=3, auto=True),
      Arm('rockets', ..., fire=4))
# 炮手座（EMP 坦克的两座防空机炮塔）：
Arm('aa_l', ..., fire=0, auto=True, seat=1, turret={'low': -5, 'high': 80, 'yaw_rate': 90, 'pitch_rate': 60})
# 非 X-Morph 车型直接在 STORES 里写：
'APOC_SAM': Store('Apocalypse SAM', 'air', 0.0, 0.0, Missile(...), names, key=FIRE_SECONDARY, auto=True),
```

生成链：`tools/gen_stores.py` 把 `key` / `auto` / `seat` / `turret` 写进 `src/stores.inc` 的 `StoreSpec::fireKey` / `autoTurret` / `seat` / `turret`（`StoreTurret`，角度换算成弧度；CI 用 `--check` 校验）。

## 运行时（`src/payload.cpp`）

### 统一接口（用户 2026-10-30：「让他可以统一函数之类的方便后续维护」）

一个武器归谁扣扳机、能不能打、怎么扣，只在下面三个函数里判断；玩家座位、自动炮塔、NPC 乘员都走它们，新增开火来源只改这里：

| 函数 | 作用 |
|---|---|
| `ControlOf(class, weapon)` → `Control{source, key, autoTurret}` | 开火来源：`cycle`（座位原版扳机与挂在上面的挂载，`FireOf` / `PullHook` 重定向）、`coax`（同轴机枪）、`second`（V603 原版不读的副扳机，一个发射器的多种弹，`Switch` 循环）、`key`（额外键 3～6；炮手座的武器是 `key` = `kSeatTriggerKey`（1），即该座位的主扳机，V603 只读座位 0 的主扳机）。`OwnKey` / `IndependentSecondary` / `AutoTurret` 都由它派生。 |
| `HasShot(vehicle, weapon, target, blocked)` | 判射（玩家自动炮塔、NPC 的同轴机枪与额外键共用）：导弹看游戏自己的锁定 + `NpcWeaponCandidate`；炮看射程内、炮口方向 3° 内、线上无友军。同轴机枪射程取武器自己的、最多 900 m，不套 `NpcWeaponCandidate`。`target` 为空时找车辆的任意敌人（自动炮塔）。 |
| `FireSeat(vehicle, class, ws, readout, SeatIntent)` | 按座位本帧的意图扣扳机（唯一的 `Pull`：置 `weapon+0x139`，同轴机枪先写曳光弹色）。`SeatIntent`：副扳机按住 + 选中的那一弹、同轴扳机按住、键 3～6 按住、自动炮塔开关、NPC 的目标。 |

调用方：

- 玩家座位（`PayloadFrame`）：读扳机、键、自动炮塔开关，填 `SeatIntent` 调一次 `FireSeat`。
- NPC 乘员（`NpcOwnWeaponsFire`，原 `NpcCoaxFire` + `NpcOwnKeysFire` 合并）：`SeatIntent.crew` = 乘员的目标，同轴机枪与键 3～6 的武器各按 `HasShot` 开火。副扳机的导弹仍由 `NpcSecondarySelect` 选弹、原生瞄准后经 `FireIndependentSecondary`（同样是 `FireSeat`）发射。
- `src/npcai.cpp`：驾驶座（`DriverPayloadAim`）与炮手座（`NpcGunnersInput`）共用 `CrewMainGun`（选主武器、`WeaponHoldsFire` 判是否白打、原生瞄准）与 `CrewOwnWeapons`（副扳机 + `NpcOwnWeaponsFire`）。
- HUD（`hud.cpp PayloadControls`）的键名取 `FireKeyVk` / `FireKeyButton`（`crew.h`），与开火读的是同一份绑定。

`EDF6AutoTurret` 插件（`autoturret/`）是另一个 DLL，管原版炮塔座位的转向与判射（`gunner.cpp OnTarget`：按炮的偏航 / 俯仰误差与目标尺寸），与这里的自有键武器不重叠，代码不共享。

### 行为

- 玩家座位（`PayloadFrame`）：副扳机（`seat+0x2E0`）只发 `Switch` 选中的那一弹；键 3～6 按住就扣对应武器，射速由武器自己的 `FireInterval` 控制。地图打开时不读键。
- 自动炮塔（`HasShot`）：开着时每帧判断：
  - 导弹：游戏自己的锁定（`lockon.cpp`）锁住了目标，且这枚弹能打这个目标（`NpcWeaponCandidate`：防空导弹只打空中目标、射程内）就发射；
  - 炮：射程内有敌人在炮口方向 3° 以内，且中间没有友军（`FriendInLine`）就开火。
  - 副扳机上的自动炮塔只发当前循环选中的那一种弹。
  - 开关：`VehicleAutoTurretKey`（默认 Y，手柄默认不设），只在座位上有自动炮塔时读；ini `AutoTurrets` 是每个任务开始时的状态（默认开）。HUD 控制行显示「[Y] 自动炮塔：开 / 关」。
- NPC 乘员：主武器与副扳机照旧（`NpcPayloadSelect`、`AimIndependentSecondary`）；同轴机枪与键 3～6 的武器由 `NpcOwnWeaponsFire` 同时开火，判据同自动炮塔（目标是乘员选中的那个）。只在运行该 NPC 的机器上执行（`AiGunner`）。
- 联机：开火只在操作该座位的机器上置扳机锁存，射击由原版的武器同步复制到其他机器，和副扳机、同轴机枪一样。
- HUD（`hud.cpp PayloadControls`）：控制行列出每个额外键与它发射的武器（自动炮塔标「（自动）」），以及自动炮塔开关状态；载具 HUD 的瞄准标记把额外键的武器也当作「扳机上的武器」画出（`PayloadTriggerWeapons`）。

## 键位

原版按键设置的「插件：地面载具」页（页 14，原「钻头战车 / 普罗透斯」页）新增 5 行，槽位 `0x79..0x7D`：

| 行 | ini | 默认 | 手柄（ini，XInput 位） |
|---|---|---|---|
| 开火键 3 | `VehicleFire3Key` | X | `VehicleFire3Button` = 4（十字键左） |
| 开火键 4 | `VehicleFire4Key` | 鼠标侧键 4 | `VehicleFire4Button` = 0 |
| 开火键 5 | `VehicleFire5Key` | 鼠标侧键 5 | `VehicleFire5Button` = 0 |
| 开火键 6 | `VehicleFire6Key` | H | `VehicleFire6Button` = 0 |
| 自动炮塔开关 | `VehicleAutoTurretKey` | Y | `VehicleAutoTurretButton` = 0 |

选键：E 是原版上下车、K 是插件改过的语音键、T 是副武器切换；X 在地面载具上空闲（只有战机热诱弹 / 步行切伞 / 沙扎比使用），H、Y 只在步行时使用。`tests/keybind_test.cpp` 的冲突矩阵确认没有新的同情境重叠。

## 已迁移

| 车 | 武器 | key | auto |
|---|---|---|---|
| BMPT | 9M120 Ataka、9M120F | 2（循环） | |
| 2S6 通古斯卡 | 9M311 | 2 | |
| 天启坦克 | 防空导弹 | 2 | 是 |
| 爱国者 | PAC-3 | 2 | |
| X-Morph 防空坦克 | 导弹 | 2 | |
| X-Morph 重型坦克 | 防空导弹 / 车头火箭；机枪塔为炮手座 1（原作 `machinegun_turret` 单骨炮塔，整座随炮手转动、抬起） | 3 / 4；炮手座攻击键 | 防空导弹、机枪塔（空座时） |
| X-Morph EMP 坦克 | EMP 高射弹 / 导弹塔；左右防空机炮塔为炮手座 1 / 2（`seat`，各有自己的炮塔骨骼） | 2 / 3；炮手座攻击键 | 导弹塔、两座防空机炮塔（空座时） |
| 格雷普（401）| AGM 反坦克导弹架 | 3 | |
| 403 电磁炮坦克 | AGM 导弹 | 3 | |
| 404 超重型坦克 | AGM 导弹 | 3 | |
| 开普勒（V603 防空车） | AAM 防空导弹（两种弹，T 切换） | 2 | 是 |
| 摩托（503 / 613） | 火箭巢 | 3 | |

### 原版车：表驱动（`vcobjects.STOCK_MOUNTS`）

原版车的插件挂载按「车类别 → 挂载种类 → 控制方式」登记在 `pylib/vcobjects.py` `STOCK_MOUNTS`（类别即 `tools/make_stock_stores.py CATEGORIES`），不写逐车代码：

```python
STOCK_MOUNTS = {
    'ifv': {'AGM_L': Mounting(3)},                      # 开火键 3
    'flak': {'AAM_S': Mounting(FIRE_SECONDARY, auto=True), ...},   # 副扳机 + 可开关自动炮塔
    ...
}
```

表里的种类在这些车上变成独立的派生种类 `mounted(kind, how)` = `<KIND>_K<key>[A]`（如 `AGM_L_K3`、`AAM_S_K2A`，自己的武器文件 `EDF6VC_AGM_L_K3_4.SGO`），`make_stock_stores._mounted` 把该类别 loadout 里的挂载换成派生种类；同一种类在别的车上（如直升机挂架上的 `AGM_L`）保持原控制。表里没有的挂载照旧走主武器循环。新增一种映射只改这张表。

三选一的判定：

- **同一门炮 / 同一发射架的另一种弹**（坦克 AP / HE / HEAT / 霰弹、炮射导弹 GLM、防空炮弹链、402 火箭车与直升机挂架上的几种弹）→ 不映射，留在主武器循环。
- **独立挂载、无独立炮塔骨骼**（原版车的导弹架、火箭巢；X-Morph 固定火箭巢）→ 开火键（`key`）；能自己判射又适合无人值守的（防空导弹）再加 `auto`。
- **有独立炮塔骨骼的副炮塔**（X-Morph 原作蓝图里 yaw 骨骼下另有俯仰骨骼 / 单骨炮塔，且是炮而不是要锁定的导弹）→ 炮手座（`seat` + `turret`），空座时 `auto`。只对 V603 车型可用，原版车模型没有插件的炮塔骨骼，不做炮手座。
- 需要锁定的导弹塔（X-Morph 重型 / EMP 坦克的导弹塔）不做炮手座：空座时没有人的锁定可用来开火，保持开火键 + 自动。

直升机、战机的挂载不在这次范围内（挂架上是同一发射位的几种弹）。

## 独立炮手座（`src/seat_turret.cpp`）

逆向结论见 `docs/independent-seat-re.md`：V603 只给座位 0 建武器、瞄准、读输入、摆骨骼；行 ≥2 的武器由插件 `stores.cpp` 建造，holder 按武器设置行的 `[骨骼, 座位]` 挂到对应座位；原版射击从 holder 骨骼的世界矩阵取方向（`0x630250`），所以插件在世界矩阵构建前（`0x621654`，与 `vulcan_rotor.cpp Build` 同一处）改骨骼本地矩阵，弹道就跟着炮塔走。

| 部分 | 做法 |
|---|---|
| 摆骨骼 | `SeatTurretsPose`（`vulcan_rotor.cpp Build` 调用）：偏航骨骼绕自己的竖轴转；俯仰骨骼（与偏航骨骼同为 `body` 的叶子）旋转 = 俯仰 × 偏航、原点绕偏航骨骼原点转（`src/seat_turret_pose.h`，`tests/seat_turret_test.cpp`）。绑定姿态取骨架定义（`BoneBind506`），不取上一帧的本地矩阵。 |
| 谁来瞄 | `SeatTurretsFrame`（`crew.cpp`，`PayloadFrame` 之后）每帧按座位上的人决定：本机玩家 → 自己的炮手视线（见「镜头」）中心对准的地面点 / 3000 m 外，不跟主炮；本机运行的 NPC 炮手 → `npcai.cpp CrewOwnWeapons` 经 `SeatTurretsTrain` 给的目标；座位空着、是自动炮塔、本机运行这辆车（驾驶座本机玩家 / 本机 NPC，空车看 `IsOnlineAuthority`）且自动炮塔开着 → 射程内、射界内、类型合适（防空 → 只打空中）的最近敌人；以上都不是 → 跟随其他机器发来的角度（600 ms 内），否则回正。按 `Turret` 的转速与限位转动。 |
| 开火 | 玩家：该座位攻击键 → `SeatIntent.keys[kSeatTriggerKey]` → `FireSeat`。NPC：`NpcOwnWeaponsFire`（`HasShot` 判炮口 3° 内）。空座自动炮塔：`UnmannedTurretsFire`（`FireSeat` + `autoOn`），同样 `HasShot` 判射。都是置扳机锁存，射击走原版武器同步。 |
| 开关 | 自动炮塔开关（`VehicleAutoTurretKey`）同时管炮手座的空座自动炮塔：本机玩家在车上时按其开关，否则开。驾驶座 HUD 的开关计数包含其他座位的自动炮塔（`SeatAutoTurrets`）。 |
| HUD | 炮手座的控制行显示「[攻击] 开火：<武器>」（`Tx::controlSeatFire`），自动炮塔带「（自动）」。 |
| 镜头 | 本机炮手的视线是插件自己的（`SeatTurretCamera`，`turretcam.cpp LookHook` 里与无畏舰炮塔同一处接管）：世界坐标下的偏航 / 俯仰，由炮手自己的鼠标 / 右摇杆转（`SeatTurretsInput` 在原版输入前读 `seat+0x2D0`，速率 `TurretCamRate`），车体和主炮塔转动不带动它；镜头在炮塔俯仰轴心上方 2.2 m、沿视线后退 6 m。炮塔按自己的转速追视线。模型上的 `xm_cam_n` 定位器只在插件不接管时（如插件关闭）给原版镜头用。 |
| 联机 | tag 15 新类型 `'STUR'`（`src/seat_turret_net.h`）：本机在瞄的炮塔（holder 序号 + 角度）每 50 ms 发给其他副本；接收方只更新本机没在瞄的炮塔。 |

**未实机验证**（需要 `gamequeue.py` 排队实测）：座位 ≥1 的主扳机（`seat+0x2E4`）在 V603 上是否由原版写入；座位 ≥1 的 holder 在联机时的开火权限（`0x630DF0`）；摆骨骼后原版弹道是否确实跟随；原版对座位 ≥1 的镜头是否使用 `xm_cam_n`。

## 限制与未做

- **炮手座只做了 V603**：其他车型（`ControlOf` 的 `flak603` 限定）的座位 ≥1 原版自己会建 / 读武器，没有逆向过，`seat` 在这些车型上不生效。
- 驾驶座上的挂载武器仍沿挂载骨骼方向发射、不独立转向：自动炮塔 = 炮口对上敌人时自动扣扳机、导弹锁上就发。需要独立转向的副炮塔做成炮手座（`seat` + `turret`）。
