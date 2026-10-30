# 载具多开火键与自动炮塔（2026-10-30）

用户原话（2026-10-30，评 X-Morph 移植时）：「所有载具都要注意的点，不是只有两个攻击按键的，很多东西可以同时发射，不一定需要切换副挂载才行。多加几个按键的事情。还有一些炮塔可以是自动无人炮塔（需要可开关）或者一些应该是独立的座位」。

本次落地前两项：**多开火键**与**自动炮塔**。第三项「独立座位」没有做，原因与设计方向见文末。

## 登记表字段（`pylib/vcobjects.py` `Store`）

替换原来的 `independent_secondary`：

| 字段 | 取值 | 含义 |
|---|---|---|
| `key` | `0` | 默认。挂载跟主武器循环（R / LB 切换），或是原生武器。 |
| | `2`（`FIRE_SECONDARY`） | 座位副扳机（鼠标右键 / LT）。只在副扳机原版不读的车型上生效（目前 V603：原版 `0x6214A4` 只读主扳机）。同一辆车上多个 `key=2` 的武器，视为**同一个发射器的不同弹种**，用 `VehicleSecondarySwitchKey`（T / 十字键右）循环切换，例如 BMPT 的 9M120 与 9M120F。 |
| | `3`..`6` | 额外开火键 `VehicleFire3Key`..`VehicleFire6Key`。每个键在一辆车上只能挂一个武器（`vehicle_fire_keys` 在导入时检查）。任何车型都可用：插件挂载的 holder 不会被任何车型的原版开火读到，扣扳机的只有这个键。 |
| `auto` | `True` | 自动炮塔。必须同时设 `key≥2`，自动炮塔关掉时就用这个键手动发射。 |

所有开火键互相独立，可以同时按住：主扳机、副扳机、键 3～6 各打各的，不需要切换。

X-Morph 单位：`pylib/xm_units.py` `Arm.fire`（对应 `Store.key`，默认 2）、`Arm.auto`，由 `vcobjects._xm_arm` 透传。同一个 unit 的 arms 各用不同的键（`tests/xmorph_registry_guard.py` 检查）。

示例：

```python
# 一个车型：主炮（原生两门）+ 机枪塔（副扳机）+ 防空导弹塔（键 3，自动炮塔）+ 火箭巢（键 4）
arms=(Arm('mg', ..., fire=2),
      Arm('aam', ..., fire=3, auto=True),
      Arm('rockets', ..., fire=4))
# 非 X-Morph 车型直接在 STORES 里写：
'APOC_SAM': Store('Apocalypse SAM', 'air', 0.0, 0.0, Missile(...), names, key=FIRE_SECONDARY, auto=True),
```

生成链：`tools/gen_stores.py` 把 `key` / `auto` 写进 `src/stores.inc` 的 `StoreSpec::fireKey` / `autoTurret`（CI 用 `--check` 校验）。

## 运行时（`src/payload.cpp`）

### 统一接口（用户 2026-10-30：「让他可以统一函数之类的方便后续维护」）

一个武器归谁扣扳机、能不能打、怎么扣，只在下面三个函数里判断；玩家座位、自动炮塔、NPC 乘员都走它们，新增开火来源只改这里：

| 函数 | 作用 |
|---|---|
| `ControlOf(class, weapon)` → `Control{source, key, autoTurret}` | 开火来源：`cycle`（座位原版扳机与挂在上面的挂载，`FireOf` / `PullHook` 重定向）、`coax`（同轴机枪）、`second`（V603 原版不读的副扳机，一个发射器的多种弹，`Switch` 循环）、`key`（额外键 3～6）。`OwnKey` / `IndependentSecondary` / `AutoTurret` 都由它派生。 |
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
| X-Morph 重型坦克 | 机枪塔 / 防空导弹 / 车头火箭 | 2 / 3 / 4 | 防空导弹 |
| X-Morph EMP 坦克 | EMP 高射弹 / 防空机炮塔 / 导弹塔 | 2 / 3 / 4 | 防空机炮塔、导弹塔 |

坦克的 AP / HE / HEAT / 霰弹、防空车的 AP / 近炸弹链等仍是**同一门炮的不同弹种**，保持主武器循环；同轴机枪本来就在副扳机上独立开火。直升机、战机的挂载暂未改动（挂载种类在多种载具间共用，`key` 是按种类设的）。

## 限制与未做

- **武器不会独立转向**：每个武器沿它挂载骨骼的方向发射，骨骼随座位的炮塔转（X-Morph 的 arm 挂在 `cannon_main` / `cannon_l` 上，见 `pylib/xm_units.py` `Arm.bone`）。所以自动炮塔 = 炮口对上敌人时自动扣扳机、导弹锁上就发，不会自己把炮塔转向另一个目标。
- **独立座位没有做**：要让别的玩家 / NPC 坐上某个炮塔单独操作，需要：
  1. 车辆 SGO 增加乘坐位置（`vehicle_riding_position`），V603 对座位 ≥1 的武器建造、瞄准、输入是否支持没有逆向过（Car 类 `0x65B14B` 只给座位 0 建枪，见 `docs/edf41-bike-re.md`）；
  2. 每个副炮塔要有自己的 yaw / pitch 骨骼，插件按无畏舰炮塔（`src/dreadnought_turrets.inc`）的方式摆骨骼；摆骨骼后原版弹道是否跟随骨骼也需要实机验证；
  3. 每座的镜头、HUD、联机同步。
  设计方向：`Store` 加 `seat: int`（乘员座位）与 `turret: (yaw 骨, pitch 骨, 俯仰限)`；座位无人且是自动炮塔时由插件按目标瞄准，有人时由乘员瞄准。X-Morph 模型要做各自瞄准的炮塔时，先把每个副炮塔做成独立骨骼（名字前 16 个字符要能区分，见 `src/vulcan_rotor.h`）。
