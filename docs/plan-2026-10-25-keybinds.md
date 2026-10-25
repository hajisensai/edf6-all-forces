# 键位体系重新设计（2026-10-25）

来源：[`feedback-2026-10-25-keybinds.md`](feedback-2026-10-25-keybinds.md)（「好多重叠的 加到原生的按键设置里面？同时重新设计一下原生的按键设计吧」）。
原版按键设置的逆向：[`keyconfig-re.md`](keyconfig-re.md)。基于 `origin/main` `30a795b4`，并兼容开放中的 PR #254（W/S 油门 / 挡位）。

## 0. 结论

1. **真冲突不多，乱在没有统一规则**。同一情境里同键同时触发的只有 4 处（§2 表里标 ✗），其余「重叠」是不同载具各用一套字母、
   或靠分散的特例互相让位（`KeyTaken`、`ChordOwns`、`Offered`、松开触发……）。
2. **原版菜单不能「加几行数据」就接入**：分页、行数、行标签都编译在代码里（`keyconfig-re.md` §1、§4）。但原版**键值表可读、而且有空槽**，
   所以分三步：
   - 阶段 1：插件读原版键位（只读），同义动作跟随原版，启动时报告冲突。
   - 阶段 2：插件内键位菜单。
   - 阶段 3：注入原版菜单（大改，需确认）。
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
| `[` `]` | — | `CallPrevKey` / `CallNextKey` | 切换空袭呼叫类型（独立线程） |
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
| Q | X | AutoTurret `LockKey/Button` | 炮塔座位锁定（坦克驾驶座只锁定） |

### 1.3 地面车（PR #254 合入后）

- **W/S**：原版油门 / 刹车，改成点按换挡。挡位 R/N/1~4，按住每 0.4 s 再换一挡（`VehicleGears=1`）。
  实现是改写 `seat+0x2C4`，只作用于玩家键盘驾驶的座位 0。
- 原版「驾驶 / 坦克」页的手刹、喇叭、主副炮照旧，由玩家在原版里设置。
- 钻头战车：R / Y 发射钻头（`DrillLaunchKey/Button`）。
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
- 炮塔：Q / X 锁定（AutoTurret），同时自制 Q 标记（`npcai.cpp` 注释写明是有意的）。

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
| 3 | 炮塔座 | Q | AutoTurret 锁定 + 自制 Q 标记 | △ 有意（标记即锁定点） | `designate.cpp:216`、`npcai.cpp:3377` |
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
就会和插件键撞，插件无从提示。阶段 1 先解决「看得见」。

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
| 指向 | Q | Q 标记、AutoTurret 锁定、战机下一目标（自动瞄准的 Q 提示留在这一族，归另一个会话） | 标记与锁定同时发生是有意的；会让「指向」以外的 Q 用法（无畏舰横移）让位，见 3.3 |
| 武器切换 | R | 挂载 / 弹种（`PlayerJetSwitchKey`）、炮舰机炮弹 ↔ 机炮 | 原版「装填」若也在 R，由占用表处理（阶段 1 才能知道） |
| 副武器 | T | 原版副武器车切弹种 | |
| 特殊武器 | R / E / 右键 | 钻头发射（R）、浮游炮（R）、米加炮（E）、导弹架 / 加特林（右键） | 本身是开火类，跟着各自机体的武器习惯，不强行统一 |
| 形态 | G | 起落架；**Proteus 行走 / 架设（改到这里）** | |
| 防御 | B / X | 护盾 / 举盾（B）；热诱弹 / 切伞（X） | |
| 呼叫 | H | 呼叫飞机（步行）、放出护航机（舰桥） | |
| 视角 | 中键 / C / Z / V / N | 自由视角、高视角、瞄具放大、第一人称、热像 | |
| 座位 | F / 1~9 | 换座 | |
| 全局界面 | M / I / `[` `]` / F2 | 地图、伤害统计、空袭类型、战前配置 | 地图内另有一整套指挥键，由 `MapHoldsKeys` 独占 |

### 3.3 默认键改动（**需用户确认**）

| # | 改什么 | 旧 | 新 | 理由 | 风险 |
|---|---|---|---|---|---|
| A | `ProteusModeKey` | T | **G** | 解冲突 4；归「形态」家族，与起落架同键 | 老玩家肌肉记忆；ini 里自定义过的人不受影响 |
| B | 无畏舰舰桥上的 Q 标记 | Q 横移 + 标记 | 舰桥座位声明占用 Q / E，Q 标记让位（横移保持 Q / E，与地图 Q / E 转向一致） | 解冲突 1 | 舰桥上不能直接放 Q 标记（炮塔座照常） |
| C | 战机 Q | 下一目标 + 标记 / 派点 | **保持同键**，但战机座位上 Q 只做「标记锁定的那个目标」：不取消、不派小队到点 | 冲突 2：换目标与标记同属「指向」族，但「没瞄到敌人就派小队到点」在高速飞行里几乎总是误触 | 要给 `ToggleMark` 加一个按座位区分的模式 |
| D | 手柄：战机起落架 | L3 | **十字键下长按**（L3 留给自由视角） | 解冲突 5；旋翼机 L3 悬停下降同理，在旋翼机座位上不提供 L3 自由视角 | 手柄玩家要重新学；需实机确认十字键下在战机上空闲 |

不改的（理由已在表里）：X 热诱弹 / 切伞、V、中键、R3 的现有让位，地图指挥键，怪物操控固定键（接管时玩家不在载具里，与载具键不同时生效）。

### 3.4 统一仲裁（中等改动，阶段 1 一起做）

新增 `src/keyclaim.h/.cpp`：每帧由当前座位 / 载具填一张「占用键」位图（键鼠 VK + 手柄位）；全局热键读键前查表。
`firstperson.cpp KeyTaken`、`vehicle_look.cpp Offered` 里的沙扎比判断、`npcai.cpp NpcMarkFrame` 的 Q、`seatswitch.cpp` 的 F / 数字键
统一改成查表。

离线测试：一张「情境 × 键」矩阵（`tests/keyclaim_test.cpp`），断言每个情境里同一个键只有一个动作。ini 默认值由测试读 `EDF6VehicleCrew.ini` 得到，
以后加键时冲突会直接红。

## 4. 原版按键设置：分阶段

### 阶段 1 —— 读原版键位（只读，低风险）

- 读输入管理器的绑定表 `[[0x20B2890]+0x318] + 0x28`，索引为（本地玩家、兵种、动作字节），见 `keyconfig-re.md` §3。
  挂钩前核对 `0xD4260` / `0x7CD2B0` 的指令（审计测试已钉住），不符就不读，插件行为与现在一样。
- 需先补的逆向：游戏键码（`0x12..0x78`）→ VK 对照、原版默认键位表。
- 用途：
  1. 启动后（进 HQ 时）把原版各页当前绑定写一次日志，顺便得到原版默认键位，作为「重新设计原版键位」的依据。
  2. 插件键与原版**同一情境**的键相同时，日志 + HUD 提示一次（冲突 17）。
  3. 同义动作跟随原版：自制 Q 标记默认跟随原版「标记」（`BaseSpot`，动作 `0x1B`）的键（`NpcMarkKey=-1` 表示跟随，显式写键码则照旧）。
     AutoTurret `LockKey` 同理。

### 阶段 2 —— 插件内键位菜单

- 复用 `room_ui` 的 HUD 菜单框架，在 HQ / 联机房间的「支援战前配置」菜单里加一页「插件按键」：
  - 按 §1 的情境分组列出全部插件键
  - 回车后按下新键即可改
  - 冲突（插件之间，以及阶段 1 读到的原版键）当场标红
- 写回 `EDF6VehicleCrew.ini` / `EDF6AutoTurret.ini`。安装器的 ini 合并保留用户值（selftest 已覆盖），升级不丢。

### 阶段 3 —— 注入原版菜单（**大改，需用户确认**）

- 在原版菜单里加「插件」行（或借直升机页、坦克页这些短页的空位），用原版的改键对话框和冲突检查。
- 键值存进原版绑定表的空槽（`0x4F..0xFE`）。
- 要改的 6～7 处见 `keyconfig-re.md` §4：
  - 动作清单
  - 行数立即数
  - 两处标签 switch
  - 文本表
  - 布局行数
  - 编辑缓冲
  - 持久化
- 风险：
  - 多处原版代码补丁，DLL 一更新就全部失效（审计测试会红，插件会拒绝挂钩，退回阶段 2 的菜单）
  - 原版存档是否保存新槽未知；若不保存，要靠插件存，再写回内存表
  - 原版按兵种存键，同一插件键要么四个兵种各设一次，要么插件强制四份同步
- 先做阶段 1、2；阶段 3 等用户确认后再开一个独立 PR，带实机验证。

### 「重新设计原版按键」

原版默认键位表还没读出来（阶段 1 的日志会给出）。在那之前不改原版默认值。

原版没有固定翼、舰船、炮手座、地图分页。这些操作在插件里按 §3 的家族统一，并在阶段 2 的菜单里成组呈现。

拿到原版默认键后，再出一份「推荐原版设置」：只改原版里与 §3 家族冲突的键，由玩家在原版菜单里照着改，或阶段 3 后提供一键套用。

## 5. 顺手修正（本 PR）

- `EDF6VehicleCrew.ini` / `crew.h`：`FirstPersonKey` 注释原写「只在步行时生效」，实际 `FirstPersonVehicle=1` 时乘车也生效（`firstperson.cpp ToggleDown`），已改。

## 6. 本 PR 不做

- 不改任何默认键、不加仲裁代码。§3.3 的 A~D 与 §4 阶段 3 等用户确认。
- 不碰自动瞄准逻辑（另一个会话）。
- 不改 PR #254 的键位。本方案与它的 W/S、Space、Ctrl 语义一致。
