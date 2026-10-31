# 键位体系重新设计（2026-10-25）

来源：[`feedback-2026-10-25-keybinds.md`](feedback-2026-10-25-keybinds.md)（「好多重叠的 加到原生的按键设置里面？同时重新设计一下原生的按键设计吧」）。
原版按键设置的逆向：[`keyconfig-re.md`](keyconfig-re.md)。起草于 `origin/main` `30a795b4`，已合入 `20984503`：PR #254（W/S 油门 / 挡位）与 PR #266（自动瞄准开关 `AutoAimKey`）都已在 main，本方案按它们之后的键位写。

## 0. 结论

1. **真冲突不多，乱在没有统一规则**。同一情境里同键同时触发的只有 4 处（§2 表里标 ✗），其余「重叠」是不同载具各用一套字母、
   或靠分散的特例互相让位（`KeyTaken`、`ChordOwns`、`Offered`、松开触发……）。
2. **插件键放进原版按键设置**（用户 2026-10-26 决定：「按键重绑 阶段2删掉，直接阶段3」）。原版菜单的分页、行数、行标签编译在代码里，
   但键值表有空槽、而且整块存进 COMMON.CFG（`keyconfig-re.md` §6），所以做法是：
   - 插件键存进原版键值表的空槽（`0x4F..`），插件各处读键改为读这些槽（阶段 1「读原版键位」是它的一部分，一并实现）；
   - 在原版菜单里加 6 个「插件」页（页 11..16），用原版的改键对话框、同页冲突检查、恢复默认（阶段 3）。
   - 原「阶段 2：插件内键位菜单」删除。
3. **重新设计**的核心是「同类动作同键」（§3 的键位家族），默认键只改 3 处，另加 1 个统一仲裁机制。

## 1. 盘点（按情境）

键鼠是 Windows VK。手柄默认是「座位按键位」：A=1、B=2、X=4、Y=8、LB=16、RB=32、L3=64、R3=128。只有 `MapButton`、
`VehicleSecondarySwitchButton`、`RoomLoadoutButton` 三个用 XInput 原始位：十字键 1/2/4/8、Start 16、Back 32、L3 64、R3 128、
LB 0x100、RB 0x200、A/B/X/Y 0x1000/2000/4000/8000。

插件读键的前提是游戏窗口在前台。地图打开时，大多数键都不读（`MapHoldsKeys`）。

「原版动作」指插件读的是游戏翻译后的座位轴 / 位（`seat+0x2B0` 区）：W/S（LY）、A/D（LX）、Space（`kSeatAscend`）、鼠标、开火、上车。
物理键由玩家在原版里的设置决定，插件不另设键。

### 1.1 步行

| 键 | 手柄 | ini / 来源 | 作用 |
|---|---|---|---|
| Q | — | `NpcMarkKey` → `npcai.cpp NpcMarkFrame` | 自制 Q 标记（取代原版 Spot，`VanillaSpot=0`）；乘车时也生效 |
| T | — | `NpcFormationKey` | 小队编队（要求步行） |
| Y | — | `NpcPickupKey` | 小队拾取（要求步行） |
| H | — | `PlayerJetHailKey` | 呼叫最近的我方飞机（`PlayerJetAll=1`） |
| V | — | `FirstPersonKey` | 第一人称（松开时切换；`FirstPersonVehicle=1` 时乘车也生效） |
| M / I | Back | `MapKey` / `DamageStatsKey` | 地图 / 伤害统计页 |
| F2 | L3+R3 | `RoomLoadoutKey` | 联机房间 / HQ 的支援战前配置菜单 |
| F5~F9 | — | `DebugSpawn*Key` | 调试召唤（`DebugSpawn=1`，默认关） |

### 1.2 所有载具（座位通用）

| 键 | 手柄 | ini | 作用 |
|---|---|---|---|
| 鼠标中键 | L3 | `FreeLookKey/Button` | 按住自由视角（沙扎比上不提供） |
| C | R3 | `HighCamKey/Button` | 高视角（`HighCamClass=2` 的车） |
| Z | R3 | `SightZoomKey/Button` | 瞄具放大（让位高视角，`sightzoom.cpp`） |
| PgUp/PgDn、滚轮 | 十字上/下 | `SightRangeUp/DownKey` | 放大中调表尺 |
| N | L3+R3 | `VehicleThermalKey/Button` | 光学 / 白热 / 黑热 |
| V | — | `FirstPersonKey` | 座舱第一人称 |
| F、1~9 | B | `SeatNextKey`、`SeatNumberKeys`、`SeatButton` | 换座 |
| R | LB（固定） | `PlayerJetSwitchKey` | 切换挂载 / 弹种（原版武器 ↔ 插件挂载） |
| T | 十字右 | `VehicleSecondarySwitchKey/Button` | 自带副武器的原版车切副武器弹种（BMPT 等） |
| Q | — | `NpcMarkKey` | 自制 Q 标记（乘车也生效） |
| Q | X | AutoTurret `LockKey/Button` | 炮塔座位锁定（坦克驾驶座只锁定）；只在自动瞄准开着时读（`AutoAim=0` 默认关） |
| J | — | AutoTurret `AutoAimKey/Button` | 开关自动瞄准（PR #266；手柄默认不设，避开 L3 自由视角） |

### 1.3 地面车（PR #254 合入后）

- **挡位**：2026-10-31 起挡位是两个单独的键（`VehicleCruiseUpKey` 默认左 Shift、`VehicleCruiseDownKey` 默认左 Ctrl，
  「插件：地面载具」页第 9、10 行），W/S 恢复原版油门 / 刹车且优先级高于挡位：一按 W/S 取消定速回 N、键盘原值直通
  （`docs/feedback-2026-10-31-gears-lock-tone.md`）。挡位 R/N/1~4，按住每 0.4 s 再换一挡（`VehicleGears=1`）。
  实现是改写 `seat+0x2C4`，只作用于玩家键盘驾驶的座位 0。
- 原版「驾驶 / 坦克」页的手刹、喇叭、主副炮照旧，由玩家在原版里设置。
- 钻头战车：R / Y 发射钻头（`DrillLaunchKey/Button`）。
- 多开火键（2026-10-30，`docs/multi-weapon-keys.md`）：X / 鼠标侧键 4 / 侧键 5 / H 是开火键 3～6（`VehicleFire3Key..6`，手柄 `VehicleFire3Button` 十字键左），
  Y 开关自动炮塔（`VehicleAutoTurretKey`）；都在「插件：地面载具」页。
- Proteus：T / RB 切行走 / 架设（`ProteusMode*`），B / LB 护盾（`ProteusShield*`），架设态右键 / LT 发射导弹架（`ProteusSalvoKey`）。

### 1.4 直升机（原版直升机与插件旋翼机）

- 原版「直升机」页（攻击 1 / 2、飞行）照旧。
- 鼠标瞄准飞行时 Ctrl 下降（`PlayerJetBrakeKey`，`heli.cpp`）。
- 插件旋翼机：Shift 巡航、Ctrl 下降，手柄 L3 悬停下降（固定）。

### 1.5 固定翼（插件战机，PR #254 合入后）

| 键 | 手柄 | ini | 作用 |
|---|---|---|---|
| W/S（Shift 同 W） | 不变 | `PlayerJetBoostKey` | 油门杆，松手停在原位；油门到 0 再按 S 是减速板 |
| Space / Ctrl | 不变 | `PlayerJetBrakeKey` | 拉起 / 压低（原来是 W/S 俯仰） |
| R | LB | `PlayerJetSwitchKey` | 切挂载 |
| Q | X | `PlayerJetTargetKey` | 锁定下一目标 |
| X | — | `PlayerJetFlareKey` | 热诱弹 |
| X | — | `PlayerJetChuteCutKey` | 弹射后切伞（0.5 s 后、先松开才算） |
| G | L3 | `PlayerJetGearKey/Button` | 起落架 |
| V（上车时按住） | — | `GunshipGunnerKey` | 炮舰机坐炮手座 |

### 1.6 炮手座（炮舰机炮手、EDF6AutoTurret 炮塔）

- 炮舰机炮手：R / LB 在炮弹与机炮间切，右键 / RB 按住加特林（`GunshipGatlingKey`）。
- 炮塔：自动瞄准开着时 Q / X 锁定（AutoTurret），同一次按键的 Q 标记跟随锁定目标（有意）；关着时 Q 只是 Q 标记。J 开关自动瞄准。

### 1.7 舰船（无畏舰）

| 键 | 手柄 | ini | 作用 |
|---|---|---|---|
| Shift / Ctrl | A / LB | `DreadnoughtBoost/DescendKey/Button` | 加速 / 下降 |
| Q / E | — | `DreadnoughtStrafeLeft/RightKey` | 左右横移 |
| F、1~9 | RB | `SeatNextKey` / `DreadnoughtSeatButton` | 换座（B 是下舰） |
| H | Y | `DreadnoughtHangarKey/Button` | 舰桥放出 / 召回护航机 |

### 1.8 特殊机体（沙扎比）

| 键 | 手柄 | ini | 作用 |
|---|---|---|---|
| 鼠标中键 | R3 | `SazabiLockKey/Button` | 锁定 |
| Shift | A（固定） | `SazabiDashKey` | 冲刺 |
| Ctrl | — | `SazabiDescendKey` | 加速下落 |
| R | LB | `SazabiFunnelKey/Button` | 浮游炮 |
| E | B | `SazabiCannonKey/Button` | 米加粒子炮（按住蓄力） |
| V | X | `SazabiMeleeKey/Button` | 光束战斧 |
| B | RB | `SazabiGuardKey/Button` | 举盾 |

### 1.9 怪物操控（enemy_control，M 地图下令接管）

- 每个能力一个固定键（`enemy_control_bindings.h`，不可在 ini 改），只给本机第一个本地玩家：
  - 左键 / RT、右键 / LT
  - E / RB、R / LB、F / A、G / B、X / X、B / Y
  - 侧键 4 / 十字上、侧键 5 / 十字右、1 / 十字下、2 / 十字左
  - 3~9、0
- 会飞的单位：Space / Ctrl 升 / 降。
- 移动和视角跟随原版键位（读兵种的键位记录）。

### 1.10 地图指挥（map / mapcmd，硬编码）

- 平移：WASD / 方向键。转向：Q / E（NpcMarkKey 是 Q / E 时地图让位）。俯角：R / F。
- 缩放：+ / -、PgUp / PgDn、滚轮。拖动：中键 / 左键平移，右键转向。
- 回到玩家：Space / Home。关闭：Esc。
- 选择：Tab 轮选；Shift 加选 / 排路点；Ctrl 存编队；Ctrl+A 全选；1~9 控制组。
- 下令：
  - V 跟随、X 解除、J 集火、H 坚守
  - B 登载具、N 下载具、K 解散、U 招募
  - T 编队、Y 拾取、O 状态、P 配速、Z 攻击移动
  - `[` `]` 切支援、C 呼叫
- 测试场摆放面板还有 `, . / Enter Backspace`。
- 手柄：X / Y / RB / LB / 十字键 / A / B / R3 / L3，RT / LT 缩放。

## 2. 冲突表

✗ = 同一情境同时触发、没有处理；△ = 有特例让位，或同键但语义一致；○ = 不同情境 / 不同载具，不会同时生效。

| # | 情境 | 键 | 谁和谁 | 现状 | 证据 |
|---|---|---|---|---|---|
| 1 | 无畏舰 | Q | 横移左 + 自制 Q 标记 | ✗ 每次按 Q 横移都会执行一次 Q 标记：准星下有敌人就标记（同一个再按就取消），没有就把小队派到准星所指的点（`ToggleMark` → `SendToPoint`） | `dreadnought.cpp:141`、`npcai.cpp:3381` |
| 2 | 固定翼 | Q | 锁定下一目标 + 自制 Q 标记 | ✗ 每次换目标都连带执行一次 Q 标记（同上：标记 / 取消 / 派小队到点） | `playerjet.cpp:443`、`npcai.cpp:3381` |
| 3 | 炮塔座 | Q | AutoTurret 锁定 + 自制 Q 标记 | △ 有意：标记跟随锁定；自动瞄准关（默认）时不读锁定键 | `designate.cpp`、`npcai.cpp NpcMarkFrame` |
| 4 | Proteus / 原版副武器车 | T | `ProteusModeKey` 与 `VehicleSecondarySwitchKey` | ✗（未核实 Proteus 是否走副武器切换）语义不同却同键 | `proteus.cpp:309`、`payload.cpp:644` |
| 5 | 固定翼（手柄） | L3 | 起落架 + 自由视角 | ✗ 代码上未见互斥，需实机核对 | `playerjet.cpp:1649`、`vehicle_look.cpp:48` |
| 6 | 插件旋翼机（手柄） | L3 | 悬停下降 + 自由视角 | 同上，未核对 | `playerjet_board.inc:380` |
| 7 | 沙扎比（手柄） | B | 米加粒子炮 + 换座 | 未核对（单座时换座不出手的话无害） | `sazabi_arms.inc:36`、`seatswitch.cpp:265` |
| 8 | 载具 | V | 第一人称 + 沙扎比格斗 / 炮舰机选座 | △ `KeyTaken` / 松开触发 | `firstperson.cpp:604-668` |
| 9 | 载具 | 中键 | 自由视角 + 沙扎比锁定 | △ `Offered` | `vehicle_look.cpp:48` |
| 10 | 载具（手柄） | R3、L3+R3 | 高视角 / 瞄具放大 / 沙扎比锁定、热像组合 | △ `ChordOwns`、让位 | `sightzoom.cpp:306,340`、`highcam.cpp:95` |
| 11 | 固定翼 | X | 热诱弹 + 切伞 | △ 时间窗 + 先松开 | `playerjet.cpp:1438` |
| 12 | 固定翼 | Ctrl、Space、W/S | PR #254 改语义：Space/Ctrl 俯仰、W/S 油门 | ○ 与旋翼机 / 沙扎比 / 无畏舰「Ctrl = 下」一致 | PR #254 |
| 13 | 地面车 | W/S | PR #254 点按换挡改写原版 LY | ○ 只在座位 0、键盘驾驶 | PR #254 `drivegear.cpp` |
| 14 | 载具之间 | R、B、E、右键、Shift、Ctrl | 不同载具不同含义 | ○（但不一致，见 §3） | — |
| 15 | 步行 vs 载具 | H、T | 呼叫飞机 / 编队 vs 机库 / 副武器 | ○ | `playerjet_board.inc:790`、`npcai.cpp:446` |
| 16 | 地图 vs 其它 | 几乎全部字母 | 地图下令键 | ○ `MapHoldsKeys` 挡住其它键 | `map.cpp`、`mapcmd.cpp` |
| 17 | 原版 vs 插件 | 未知 | 原版各页默认键与插件键 | **未知**：插件不读原版键位，原版默认键位表也还没找到 | `keyconfig-re.md` §3 |

第 17 条是「好多重叠」里最可能被玩家碰到、而插件现在完全看不见的那部分。例如玩家把原版「上车 / 救援」或「直升机 · 飞行」绑在 R / T / G 上，
就会和插件键撞。原版默认键已读出（`keyconfig-re.md` §5.2），与插件默认键的重叠由冲突矩阵列出（§4.3）。

## 3. 重新设计

### 3.1 原则

1. **原版能表达的动作跟随原版**，插件不另设键：移动、上升（Space）、开火、换武器、上车，以及原版「驾驶 / 坦克 / 直升机」页的动作。
2. **同类动作同键**，跨载具一致（键位家族，3.2）。新功能先找家族，没有家族才新开键。
3. **一个情境一张占用表**：当前座位 / 载具声明自己用了哪些键，全局热键（Q 标记、V 第一人称、F / 数字换座）遇到已被占用的键就让位。
   这样取代现在分散的 `KeyTaken`、`Offered`、`ChordOwns` 等特例（3.4）。
4. 手柄只用于有对应原版按钮语义的动作；摇杆按下（L3 / R3）一个情境最多给一个动作。

### 3.2 键位家族（键鼠）

| 家族 | 键 | 成员（现有） | 说明 |
|---|---|---|---|
| 速度 | W / S（PR #254） | 战机油门杆、地面车挡位 | 战雷式；Shift 在战机上同 W |
| 升降 | Space / Ctrl | 战机拉起 / 压低、旋翼机 / 直升机 / 沙扎比 / 无畏舰 / 怪物下降 | 「Space 上、Ctrl 下」全线一致 |
| 加速 | Shift | 旋翼机巡航、沙扎比冲刺、无畏舰加速 | |
| 指向 | Q（开关自动瞄准 J） | Q 标记、AutoTurret 锁定、战机下一目标；自动瞄准的开关是 J（PR #266） | 标记与锁定同时发生是有意的；会让「指向」以外的 Q 用法（无畏舰横移）让位，见 3.3 |
| 武器切换 | R | 挂载 / 弹种（`PlayerJetSwitchKey`）、炮舰机炮弹 ↔ 机炮 | 原版步兵「装填」默认 R（`keyconfig-re.md` §5.2），只在步行时生效，与载具里的 R 不同时 |
| 副武器 | T | 原版副武器车切弹种 | |
| 特殊武器 | R / E / 右键 | 钻头发射（R）、浮游炮（R）、米加炮（E）、导弹架 / 加特林（右键） | 本身是开火类，跟着各自机体的武器习惯，不强行统一 |
| 形态 | G | 起落架；**Proteus 行走 / 架设（改到这里）** | |
| 防御 | B / X | 护盾 / 举盾（B）；热诱弹 / 切伞（X） | |
| 呼叫 | H | 呼叫飞机（步行）、放出护航机（舰桥） | |
| 视角 | 中键 / C / Z / V / N | 自由视角、高视角、瞄具放大、第一人称、热像 | |
| 座位 | F / 1~9 | 换座 | |
| 全局界面 | M / I / F2 | 地图、伤害统计、战前配置 | 地图内另有一整套指挥键，由 `MapHoldsKeys` 独占 |

### 3.3 默认键改动（2026-10-26 用户全部批准，已实现）

| # | 改什么 | 旧 | 新 | 理由 | 风险 |
|---|---|---|---|---|---|
| A | `ProteusModeKey` | T | **G** | 解冲突 4；归「形态」家族，与起落架同键 | 老玩家肌肉记忆；ini 里自定义过的人不受影响 |
| B | 无畏舰舰桥上的 Q 标记 | Q 横移 + 标记 | 舰桥座位声明占用 Q / E，Q 标记让位（横移保持 Q / E，与地图 Q / E 转向一致） | 解冲突 1 | 舰桥上不能直接放 Q 标记（炮塔座照常） |
| C | 战机 Q | 下一目标 + 标记 / 派点 | **保持同键**，但战机座位上 Q 只做「标记锁定的那个目标」：不取消、不派小队到点 | 冲突 2：换目标与标记同属「指向」族，但「没瞄到敌人就派小队到点」在高速飞行里几乎总是误触 | 要给 `ToggleMark` 加一个按座位区分的模式 |
| D | 手柄：战机起落架 | L3 | **十字键下长按**（L3 留给自由视角） | 解冲突 5；旋翼机 L3 悬停下降同理，在旋翼机座位上不提供 L3 自由视角 | 手柄玩家要重新学；需实机确认十字键下在战机上空闲 |

**实现（2026-10-26）**：

- A：`ProteusStanceKey`（新 ini 名，默认 0x47 = G；旧 `ProteusModeKey` 不再读、日志写明，同 `DrillHeatSec → DrillOverheatSec` 的先例：
  安装器从不改玩家已有的值，换名才能让旧安装也拿到新默认）。键位表里它在第 14 页（与钻头战车一页），因为第 13 页的战机起落架也是 G。
- B：`dreadnought.cpp DreadnoughtBridgeClaims`：键鼠坐在舰桥（座位 0），且标记键等于左 / 右横移键时，标记让位（`qmark_ray.h SeatMarkPress`）。
- C：`playerjet.cpp PlayerJetMarkTarget` + `npcai.cpp JetMarkFrame`：坐在插件飞机驾驶座按标记键时不走 `ToggleMark`。标记键与
  `PlayerJetTargetKey` 相同（默认都是 Q）时，等这次「下一目标」带来的新锁定后标记它；最多等 2.5 秒，只有一个目标时标记原锁定。
  不取消已有标记、不派小队到地面点（`qmark_ray.h JetMarkDue`）。锁定对象取武器锁定表的条目 `+0x8`（`lockon.cpp WeaponLockObject`；
  与 `heli.cpp kTargetObject` 同一结构，推断，未实机核对）。
- D：`PlayerJetGearPadButton`（新 ini 名，XInput 位，默认 0x0002 = 十字键下，按住 `kGearPadHoldSec` 0.4 秒；旧 `PlayerJetGearButton`
  不再读）。插件旋翼机座位上，手柄自由视角键含 L3 时不提供（`vehicle_look.cpp Offered`，旋翼机 L3 是悬停下降）。

**与原版通用页默认键的重叠（T / V / Ctrl / E）**：用户 2026-10-26 批准建议表（「编队改 U、副武器保持 T；插件保持 V，原版语音改到
不常用键；原版聊天改到 Enter；无畏舰横移保持 Q/E，米加炮你挑一个空键」），已实现：

| 键 | 旧默认 | 新默认 | 怎么改 | 理由 |
|---|---|---|---|---|
| 编队 | T | **U** | ini 换名 `NpcFormationCycleKey`（旧 `NpcFormationKey` 不再读） | T 是原版通用页 Shotcut（快捷消息），步行同时触发；U 步行时原版和插件都不用（地图里的 U「招募」只在地图打开时读，地图独占按键） |
| 副武器 | T | T（不变） | — | 用户决定保持；与原版 Shotcut 的重叠保留在冲突矩阵 |
| 原版聊天（Chat，动作 0x1D） | 左 Ctrl | **Enter** | 改原版默认表 `0xD48B0` 的立即数（四套各一处），见 `keyconfig-re.md` §5.2 | Ctrl 是插件「Space 上、Ctrl 下」家族（战机压低、无畏舰 / 沙扎比下降）；Enter 是常见的聊天键，原版与插件都不用（测试场地图摆放面板的 Enter 只在地图里读），原版对话框 `0x7CCD10` 允许聊天用它 |
| 原版语音（VoiceChat，动作 0x21） | V | **K** | 同上 | V 是插件第一人称 / 炮手座 / 沙扎比战斧；K 是原版任何页、插件任何默认键都不用的字母键（地图里的 K「解散」只在地图里读），离常用区较远、不易误触，原版对话框允许 |
| 无畏舰横移 | Q / E | Q / E（不变） | — | 用户决定保持；E 与原版上下车的重叠保留在冲突矩阵（舰桥上下车走座位键） |
| 沙扎比米加粒子炮 | E | **X** | ini 换名 `SazabiMegaCannonKey`（旧 `SazabiCannonKey` 不再读） | E 是原版上下车；X 在沙扎比座位上空闲：原版没有沙扎比页，插件 X 只用于战机热诱弹（战机页）和切伞（步行），沙扎比页（鼠标中 / Shift / Ctrl / R / V / B）和载具通用页都没有 X；靠近 Z / C，左手好按 |

原版聊天 / 语音只改「新建键位表」与「通用页恢复默认」写入的默认值，**不碰玩家 COMMON.CFG 里已有的键位**：已经玩过的存档仍是 Ctrl / V，
要改需在通用页按「恢复默认」或手动改。

不改的（理由已在表里）：X 热诱弹 / 切伞、V、中键、R3 的现有让位，地图指挥键，怪物操控固定键（接管时玩家不在载具里，与载具键不同时生效）。

### 3.4 统一仲裁（中等改动，未做：见 §4.3）

新增 `src/keyclaim.h/.cpp`：每帧由当前座位 / 载具填一张「占用键」位图（键鼠 VK + 手柄位）；全局热键读键前查表。
`firstperson.cpp KeyTaken`、`vehicle_look.cpp Offered` 里的沙扎比判断、`npcai.cpp NpcMarkFrame` 的 Q、`seatswitch.cpp` 的 F / 数字键
统一改成查表。

离线测试：一张「情境 × 键」矩阵（`tests/keyclaim_test.cpp`），断言每个情境里同一个键只有一个动作。ini 默认值由测试读 `EDF6VehicleCrew.ini` 得到，
以后加键时冲突会直接红。

## 4. 原版按键设置：注入原版菜单（正式方案，2026-10-26 实现）

用户 2026-10-26 决定删掉原阶段 2（插件内键位菜单），直接做阶段 3；原阶段 1（读原版键位）是阶段 3 的前提，一并实现：
阶段 3 把插件键写进原版表，插件读键自然就从表里读。逆向与补丁点见 `keyconfig-re.md` §5～§7，代码 `src/keyconfig.cpp`、
`src/keyconfig_menu.h`、`common/edf/keybind.h`。

### 4.1 存储与读取

- 每个插件键占原版键值表一个空槽（动作号 `0x4F` 起，顺序固定在 `keybind.h kSlots`，COMMON.CFG 会记住它，永不改号，新键只往后加）。
- 插件页是「四兵种同写」页（`0x7CDE40`），插件只读第 0 套；键盘是第一个本地玩家的。
- 播种：每个插件第一次见到键值表时，把自己 ini 里的键（`LoadConfig` 读入并校验过的值）写进自己的槽，再写标记（`0xFE` / `0xFD`）。
  标记 = `"BK"` 标签 | 已播种到的动作号（该插件最高槽号 + 1）：以后新增的槽号总在后面，所以只给标记之后的新槽播种，玩家改过的旧槽不动。
  此后 ini 的 `*Key` 不再读，日志写明；改键只在游戏的按键设置里。旧 COMMON.CFG 读入或「全部恢复默认」后标记消失，会从 ini 再播种一次。
- 以上都以插件页确实挂上为前提（`InstallKeyConfig` 全部补丁成功）：没挂上时没有地方能改这些槽，所以不播种、不读键值表，键仍是 ini 的
  （自制 Q 也仍是 `NpcMarkKey`）。EDF6AutoTurret 按名字查 `EDF6VehicleCrew_KeyConfigPagesV1`（`keybind.h kPagesExport`），查不到（单独装、
  EDF6VehicleCrew 还没加载）或返回 false 时同样只用自己的 ini。
- 自制 Q 标记与 EDF6AutoTurret 的锁定键跟随原版「标记」（`BaseSpot`，动作 `0x1B`）：`NpcMarkKey` / `LockKey` 只剩 0 = 关闭的意义。
- 只做键盘鼠标。原版手柄页是固定 11 个按钮（没有十字键、Start、组合键），插件的手柄按钮仍在 ini。

### 4.2 菜单

顶层列表（通用 / 四兵种 / 载具）之后追加 6 项：

| 页 | 名称 | 键 |
|---|---|---|
| 11 | 插件：步行与通用 | 编队 T、拾取 Y、呼叫飞机 H、第一人称 V、地图 M、伤害统计 I、战前配置 F2、切伞 X、炮塔自动瞄准 J（AutoTurret） |
| 12 | 插件：载具通用 | 自由视角 鼠标中键、高视角 C、瞄具放大 Z、表尺 PgUp/PgDn、热成像 N、换座 F、切换挂载 R、副武器 T、炮舰机炮手座 V、炮舰机加特林 鼠标右键 |
| 13 | 插件：战机 | 加速 Shift、压低/下降 Ctrl、下一目标 Q、热诱弹 X、起落架 G |
| 14 | 插件：钻头战车/普罗透斯 | 钻头发射 R、普罗透斯 行走/架设 **G**、护盾 B、导弹架 鼠标右键 |
| 15 | 插件：无畏舰 | 加速 Shift、下降 Ctrl、横移 Q/E、机库 H、向前看 鼠标右键 |
| 16 | 插件：沙扎比 | 锁定 鼠标中键、冲刺 Shift、下落 Ctrl、浮游炮 R、米加炮 E、战斧 V、举盾 B |

默认键：§3.3 A（普罗透斯 G）已改，其余与此前相同。普罗透斯的 G 与战机起落架的 G 同属「形态」键，所以两者分在两页（同页一键一行）。分页按情境：同一页内键必须唯一（原版的同页冲突检查会互换），所以同一个键在
不同载具上的不同用途（R、Shift、Ctrl、鼠标右键……）分在不同页。

### 4.3 冲突

- 页内：原版检查，撞键互换。
- 插件页与原版页：原版的「兵种 / 载具页不许用通用页键」只管表行 3..10，插件行不套用（否则插件默认键里与通用页重叠的 T / V / Ctrl / E
  会在「恢复默认」时被清空，等于擅改默认键）。重叠由 `tests/keybind_test.cpp` 的冲突矩阵逐条列出（§3.3 实现后 7 处，全部是有意保留的
  既有重叠），新增重叠会让测试变红。
- §3.4 的统一占用表（`src/keyclaim.*`）不是这套存储 / 菜单运作所必需的，本次不做：它解决的是同一时刻两个动作同键时谁让位，
  与键存在哪、在哪里改无关。

### 4.4 风险

- 多处原版代码补丁（6 处分派、6 个 lea、1 字节、8 个默认键立即数、3 个调用、顶层列表）。DLL 一变，签名不符，整个功能不挂，键仍是 ini 的。
  补丁顺序：对原版页无影响的先打（分派桩对页 0..10 原样转回原版跳转表），默认键立即数和顶层列表最后；这两步任一失败就把默认键改回原版，
  所以要么插件页和新默认键都有，要么都没有。
- 桩在函数中部调插件回调：`tests/keyconfig_native_audit.py` 逐条核对每个接回点 / 默认出口之后原版不读桩改过的易失寄存器（含 xmm0–5 和标志位），
  调用处栈 16 字节对齐，所以桩不保存这些寄存器。
- 未实机验证：菜单页面的显示、改键对话框、恢复默认、COMMON.CFG 保存后重启仍在，全部只做了静态分析和离线测试。

### 「重新设计原版按键」

不重新设计原版默认键。唯一的改动是 §3.3 批准的通用页「聊天」改 Enter、「语音」改 K（只改新建键位表和通用页「恢复默认」写入的默认值，
不碰玩家 COMMON.CFG 里已有的键位）；其余原版默认值不改。

原版没有固定翼、舰船、炮手座、地图分页。这些操作在插件里按 §3 的家族统一，并在原版菜单的插件页（§4.2）里成组呈现。

原版默认键位已读出（`keyconfig-re.md` §5.2）。「推荐原版设置」：只改原版里与 §3 家族冲突的键，由玩家在原版菜单里照着改；是否提供一键套用另议。

## 5. 已在 main 修正

- `FirstPersonKey` 注释原写「只在步行时生效」，实际 `FirstPersonVehicle=1` 时乘车也生效：main 已改（本 PR 起草时也改了同一处，合并时采用 main 的写法）。

## 6. 本 PR 不做

- （#264）不改任何默认键、不加仲裁代码。2026-10-26 起 §4 已实现；§3.3 的 A~D 已于 2026-10-26 批准并实现（见 §3.3）。
- 不碰自动瞄准逻辑（另一个会话）。
- 不改 PR #254 / #266 的键位。本方案与它们的 W/S、Space、Ctrl、Q、J 语义一致。
