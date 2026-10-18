# NPC 背包位武器：逆向笔记与实现

> EDF.dll TimeDateStamp `0x678CCB46`，地址都是 RVA。H = 指令 / 数据直接读到；M = 有代码证据的推断；L = 推测，要实机确认。
> 全部是静态分析（`tools/edfre.py` + capstone、pefile、Root.cpk 只读），**没有启动游戏、没有两机实测**。
> 用户原话（2026-10-18）：「让npc可以使用背包位武器如突击的手雷，飞翼的短剑等」。

## 0. 结论速览

- **根因有两层，缺一不可（H）**：
  1. **NPC 根本没有背包武器**：所有原版 AI 兵模板（`N601_*` 游骑兵、`N606_*` 翼人、`N605_*`，以及重装除打桩型外）的
     `soldier_sub_weapon_slot = 0`、`soldier_load_weapon` 只有一把 AI 武器；士兵初始化 `0x592FA0` 按它把副武器向量
     `human+0x19F8`（`+0x08` 记录指针、`+0x18` 个数 = `+0x1A10`）留空。玩家模板（`P505_RANGER` 等）是
     `[[2,'weapon',0],[3,'weapon',0]]`。
  2. **原版的背包输入只给带手柄的玩家**：`SoldierBase` 第 5 槽 `0x59ABB0` 对每个士兵在 `0x59AC65` 无条件调
     `0x5A1D70`（背包输入），它第一条判断 `0x5A1DA4: cmp qword [rcx+0x340],0; je 0x5A2147`——`+0x340` 是手柄对象
     （`docs/re-notes.md`），NPC 为 0，整个函数直接返回，意图块里的背包键字节 `+0xD7F+i` 从来没人读。
  所以既不是「装上了但 AI 不选」，也不是「AI 选了但动作走不通」，而是**没装上、且入口对 NPC 关着**。
- **实现**：
  - 装上：支援士兵由生成的模板创建，模板按玩家模板的方式把背包武器放进副武器槽（`tools/support_loadout.py with_pack`）。
    原版自己有先例：`N607_AIHEAVYARMOR_SP*`（打桩重装）就是 `soldier_load_weapon` 三把、`soldier_weapon_slot` 只列前两把、
    `soldier_sub_weapon_slot = [[2,'weapon','te_l']]`（第三把是 `AiHeavyDash01`，Weapon_Accessory）（H，数据）。
  - 使用：不走玩家输入（门是手柄），而是做**玩家轻按背包键后原版做的那一步**，也是**其他机器收到消息 0x10 后原版做的那一步**
    （见 §2）：`npc_backpack.cpp NpcPackUse`。时机由插件 AI 判断（`npc_backpack.h`，§3）。
  - 联机：与玩家相同——拥有这名士兵的机器做这一步并发原版消息 0x10（槽号），其他机器上的同一士兵由原版接收代码 `0x59E6FD`
    做同一步（§2.3）。
- **各兵种真正的「背包位」**（CONFIG.SGO SoldierInit + 玩家模板 `soldier_sub_weapon_slot`，`docs/loadout-re.md` §1.2，H）：

| 兵种 | 副武器槽（sub weapon slot） | 内容 | 本次 |
|---|---|---|---|
| 游骑兵 | 槽 2 `Weapon_SlotSupport2`（20–23） | 手雷 / 鼠雷 / DNG / 爆竹（20）、遥控炸弹 / 冲击地雷（21）、哨戒炮（22）、铝热剂 / 二元 / 回复弹（23） | 20 全部、23 的铝热剂与二元弹 |
| 游骑兵 | 槽 3 `Weapon_SlotSupport1`（7 支援装备、8–10 载具） | 呼叫 / 被动装备 | 不做（不是背包武器） |
| 翼人 | 槽 2 `PaleSupport2`（110–116） | 电浆球 110、**短剑 111（Handy / Short / Power / Valiant Saber）**、陷阱 112、护盾 113、特殊 114、诱导弹 115、光枪 116 | 110、111、116 |
| 翼人 | 槽 3 `PaleSupport1`（120 核心） | 飞行核心 | 不做 |
| 重装 | 槽 4 / 5 `FencerSupport`（206–209） | 推进器、护盾辅助、炮口稳定、外骨骼：全是 `Weapon_Accessory`（被动） | 不做 |
| 空降兵 | 槽 3 支援、槽 4 载具 | 呼叫类 | 不做（也没有 AI 空降兵模板） |

  用户说的「飞翼的短剑」= 翼人 category 111 `Weapon_Pale_SupportShort`（`docs/weapons.csv` 605–612：Handy Saber、Short Saber、
  Handy Saber 2/3、Short Saber 2、Power Saber、Handy Saber S、Valiant Saber；DLC 的 Type 0、任务包的 Planet / Galactic Saber 不做）。

## 1. 模板：NPC 怎样「装上」

### 1.1 初始化读的三个键（`0x592FA0`，SoldierBase 构造 `0x58D020` 在 `0x58E0F6` 调用，H）

- `soldier_weapon_slot`（字符串 `0x17D29D0`，`0x593011`）：每个 WeaponSet 一项 `[[武器下标...], 骨骼, x, y]`，下标进 `human+0x1950`。
- `soldier_sub_weapon_slot`（`0x17D2AF8`，`0x593B7B`）：每个副武器槽一项 `[武器下标, 骨骼, 节点]`，写进 `+0x19F8` 向量：
  记录 `0x20` 字节，`+0` 下标（`0x593F24`）、`+8` 节点（第三项是字符串时 `0x11001F0`）、`+0x10` 骨骼号（`0x1166FB0`）、
  `+0x14` 长按帧数、`+0x18` 按住计数、`+0x1C` 按下、`+0x1D` 轻按松开、`+0x1E` 长按到点（`0x5A1E1A..0x5A1E56`）。
- `soldier_load_weapon`（`0x17D2B28`，`0x5942BB`）：逐个路径 `0x58F360(human, path, stars)` 造武器、追加到 `+0x1950`。
  星级缓冲在 `0x59436A..0x5943A4` 先清 0 再 `memset(.., 5, 8)`：**AI 模板载入的武器 8 项属性都按 5 星造**（H 指令；「5 星」
  的含义 M）。资源预载也读这个键（`0x7A5A96`），所以列进去的武器随士兵模板一起预载（M）。

### 1.2 生成的模板（`tools/support_loadout.py with_pack`）

在原模板（已上色的那份）上：`soldier_load_weapon` 末尾加 `app:/weapon/<名>.sgo`（WEAPONTABLE 第 1 列的写法）；
`soldier_weapon_slot` 每组只保留原有下标（原版 `[[0,1]]` 只载一把时第二个下标本来就空着；不改的话背包武器会进主武器组被
d82+i 选成主武器）；`soldier_sub_weapon_slot = [[新下标,'weapon',0]]`（P505 的写法，骨骼与主武器同为 `weapon`）。
`tests/support_loadout_ini_test.py` 在有游戏时逐个核对：只有这三个键与原模板不同，背包武器在副槽、不在任何武器组。

- 文件名：`EDF6VC_NPC_<兵种>[_L]_<主色|X>_<副色|X>_<武器名大写>.SGO`（`support_loadout.h SupportLookFile`），由名字即可生成
  （联机缺文件的一方写进待生成清单，安装器 / 后台生成器按名字造）。
- 动画：NPC 与玩家共用同一个 CAS（游骑兵 `EDF6ArmySoldier.cas`、翼人 `EDF6_wingDiver.cas`，H 数据），背包动作
  （`throwL_swing`、`recoil_sword_a/b/c` 等，武器 SGO 的 `custom_parameter.animation`）在同一套动画里（M：动画号在武器创建时
  按士兵的 CAS 解析）。

### 1.3 预设语法、变体、房间菜单

- ini：`SupportPreset_<KEY>=兵种[@主色[:副色]][+背包武器][*人数]`，背包武器写 WEAPONTABLE 名（大小写不限），必须是该兵种的
  （`npc_pack_table.h PackFits`；重装没有）。插件 `ParseSupportPreset` 与安装器 `parse_preset` 同一结论（测试互相对照）。
- 64 位变体：`LookVariant` 的 bit 50–57 = 背包武器码（表 `kPackWeapons` 的 `code`，1..74，发出去就不再改）。
- 房间 / 基地的支援配置菜单（`room_menu.h`）：每名游骑兵 / 翼人士兵多一栏「背包武器」（第 4 栏，左右切换：无 → 该兵种的
  武器按表顺序 → 无），原有兵种 / 主色 / 副色三栏不变；换成重装时背包武器自动清掉。行文字显示「… + 武器英文名」。
- 房间阶段预同步（`room_sync_logic.h`）：短 token 加 `+` 两位十六进制码（`aXX+01`）。**旧版插件读到这种 token 会拒绝整张清单**
  （原有规则：任何读不懂的 token 都整张拒绝），这时旧版成员走待生成清单（行为同以前缺文件）。

## 2. 「使用」的原版代码（H，`tests/npc_backpack_native_audit.py` 逐条核对字节与跳转目标）

### 2.1 背包输入 `0x5A1D70(human, dt)`（只有玩家走到这里）

每个副武器槽 i（`0x5A1DD3` 起的循环）：

1. 门：`bpl = !(+0x5D0 & 4) && !(+0x39C & ~2) && (+0x158C & (i==0 ? 0x2000 : 0x4000))`（`0x5A1DD3..0x5A1E06`）。
   `+0x158C` 就是 `npcai.cpp` 已在用的控制掩码（bit 0 移动、1 视角、4 扳机、11 翻滚；bit 13 / 14 = 背包槽 0 / 1）。
2. 按键 `+0xD7F+i` 进记录的按住计数，算出「轻按松开」`+0x1D` / 「长按到点」`+0x1E`。
3. 武器：记录 `+0` 下标 → `+0x1950[下标]` → `__RTDynamicCast(w, 0, WeaponBase TD 0x2045768, Weapon_Sub TD 0x20476E8, 0)`
   （`0x12DA7AA`）。转不成（Weapon_Accessory：重装的推进器等被动件）就什么都不做。
4. 忙：WeaponSet 0（`+0x1970`）`+0x140` 非空且 `+0x1A20 == 0`；`+0x5D0 & 4`；`+0x1A28` 非空且 `+0x1A30 >= 1` → 跳过。
5. 轻按（`+0x1D`）：`+0x1588 |= 0x1000 / 0x2000`，门 1 通过且弹数 `+0xBE8 > 0` 时：
   - （`+0x140` 已有且 `+0x1A20` 置位：先切回 `0x5A2730` 结束旧的）
   - `*(*(set+0x40))+0xEC4 = 0`（u16）、`set+0x140 = sub`、`0x5A0A60(set+0x110, 0x5A28A0, 0)`、`+0x1A20 = 0`（`0x5A1FC1..0x5A1FF8`）；
   - 联机且不是副本（`+0x128 & 1 == 0`）：写流 `0x79A460(buf, 0x40)`、`0x12B5790(buf, 0x10)`、`0x12B5790(buf, i)`，
     经 `human+0x120`（NetworkObject）虚表 `+0x70`（第 14 槽）发出，`0x760180` 析构（`0x5A2013..0x5A207A`）。
6. 长按（`+0x1E`）：武器 `+0x690 != 0` 时置 `+0x13B = 1`（`0x5A208B`），即武器自己的「装填键」（武器更新 `0x6935C1`
   把它当成按下沿）；否则 `sub+0x1678 & 4` 时对所有副武器调虚表 `+0x98`（遥控炸弹的引爆，M）。

### 2.2 背包动作的状态 `0x5A28A0(set, phase)`

WeaponSet 的状态机（`set+0x110`，转移函数 `0x5A0A60(machine, state, 0)`）。进入：按武器 `0x6ACE60` 给的动画号在
`human+0xB90` 播放（或 `+0x1678 bit0` 时走 `0x5A0540` 整体动作），把所有武器 `0x695F10`，`0x696150(sub, 1)` 扣扳机；
更新：动画播完 `0x116BBE0` → 转回 `0x5A2730`；退出：`0x6ACED0`、`0x696150(sub, 0)`、`0x695830`、`set+0x140 = 0`。
所以**武器在自己的动画里自己出手**，方向是士兵当时的视线（NPC 的视线由插件写意图块 `d60/d64`，与主武器一样）。

### 2.3 其他机器：SoldierBase 接收 `0x59E210`（NetworkObject 第 15 槽，`this = human+0x120`）

读一个字节的类型，`类型 - 0xC` 走跳转表 `0x59E870`；类型 0x10 的表项（`0x59E880`）= `0x59E6FD`：读槽号，
同 2.1 第 3 步取武器并转 Weapon_Sub，**没有手柄门、没有 `+0x158C` 门、没有弹数门**，直接做 2.1 第 5 步的那一组写入
（`0x59E76B..0x59E7DE`，旧的在用就先结束）。槽号不在 `+0x1A10` 以内时取到空指针，转换返回空，什么都不做——所以对端如果
缺文件、生成的是不带背包的原版士兵，收到消息也只是不播动作（安全，但那台机器上看不到这次投掷）。

## 3. 插件：什么时候用（`src/npc_backpack.h`，`tools/npc_backpack_check.cpp` 离线检查）

`npcai.cpp Drive` 在主武器的瞄准与扳机之后调 `Backpack`（只对插件自己驱动、已知手中武器的士兵；脚本控制的单位不用）：

| 类型 | 武器 | 条件 |
|---|---|---|
| thrown | 游骑兵 20 / 23，翼人 110 | 目标 `kGroup` 8 米（或武器半径更大时取半径）内至少 2 只敌人，或 1 只大型（锁定点 ≥ 3）；不在空中（高于自身 15 米）；水平距离 ≥ 半径 × 1.5 + 2；距离 ≤ min(真实射程, 弹速 × 2.5 秒, 150 米)；有低抛物线解且飞行 ≤ 2.5 秒；落点半径（爆炸 `+0x8B0` 或燃烧 / 毒气的 `area` 8 米）×1.25 内没有友军 / 玩家；出手方向前 6 米没有友军、没有地形；两次之间 ≥ 4 秒 |
| direct | 翼人 111 短剑、116 光枪 | 距离 ≤ 真实射程 × 0.9；弹道上没有友军；地图射线不挡；两刀之间 ≥ 0.7 秒 |

- 抛物线：`LobPitch`（低解，`tan θ = (v² − √(v⁴ − g(gx² + 2yv²)))/(gx)`），v = `+0x894 × 60`，g = `+0x8E0 × 14.7`
  （与 `heli.cpp kGravity` 同一实测值）；视线仰角再减去武器 `FireVector` 的出手抬升 `atan(y/z)`（游骑兵手雷 0.05–0.125）。
  离线检查把每个解按小步长飞一遍，落点误差 < 5 cm。
- 先转视线、对准（投掷 0.05 rad，直接型按 1.5 米 / 距离）才调 `NpcPackUse`；用的那一刻起 `kHoldFrames` 45 帧内视线保持在
  投掷方向，且 WeaponSet 0 的 `+0x140` 非空期间都不再用、主扳机与换枪键清零（换枪或开火可能打断动作，M）。
- 手动装填的武器（短剑）打空后，按 2.1 第 6 步的长按做法置一次 `+0x13B`（同一次打空只置一次，15 秒后仍空再置一次）。
- 背包武器不进 `ArmsOf` 的可选武器（不在任何 WeaponSet，不能用 d82+i 选，也不计入交战距离和爆炸半径否决）。
- ini：`NpcBackpack`（默认 1；0 = 带着但不用）。`CustomNpcAi=0` 时整套 AI 不跑，自然不用。

## 4. `NpcPackUse` 与原版的对应（`src/npc_backpack.cpp`）

- 先做 2.1 的门：`+0x140` 空（**比原版严**：原版在 `+0x1A20` 置位时会打断正在进行的；插件从不打断）、`+0x5D0 & 4`、
  `+0x39C & ~2`、`+0x1A28/+0x1A30`、掩码 bit 13/14、弹数，并且武器必须是 `npc_pack_table.h` 里的一把（按武器 `+0x08` 资源键的
  SGO 名）。再做 2.1 第 5 步的四个写入。
- 联机消息：`InSession()` 且这名士兵已注册、归本机（`+0x128` 低两位 = 2）才发，与原版「不是副本才发」一致；未注册的副本各机
  各自运行，没有对端可发。决定由谁来做的是 `npcai.cpp Think` 的 `IsOnlineAuthority(h)`（原版 AI 也只在拥有端写意图块）。
- 安装：`InstallNpcAi` 调 `InstallNpcBackpack`，逐段核对 `npc_backpack_signatures.inc`（`tools/npc_backpack_sigs.py` 从
  EDF.dll 生成的 22 段，含门、偏移、调用目标、消息、类型名），任何一段不符就整项关闭、写日志，NPC 不用背包武器。
  调用中出 SEH 异常也整项关闭（不重试）。

## 5. 联机协议

- `support_protocol.h kExtBackpack = 32`（hello 的扩展字，`kExtBuilt` 里每个 hello 都宣告；欢迎消息的 features 字最后一位，
  `kFeatures = 1023`）。旧版 `LookOfVariant` 遇到 bit 50 以上直接判「解不开」，`Validate` 会**拒绝整个支援计划**；所以房主在
  `SupportPeersApplyBackpacks()` 为假时把这名士兵的背包码去掉（只带颜色），日志与 HUD 说明原因。
- 背包武器文件和颜色文件一样「可缺省」：对端没有文件时那名士兵按原版生成（不带背包），不拒绝计划。房主只有在所有对端的
  Bloom 过滤器都有这个文件名时才置 applied，所以正常情况下各机一致。
- 版本提示：`Tx::versionFeatBackpack`（「支援士兵的背包武器」）。

## 6. 文件

| 文件 | 内容 |
|---|---|
| `src/npc_pack_table.h` | 74 把可用背包武器（code、WEAPONTABLE 名、兵种、用法、category、抬升、燃烧半径、英文名），`PackNamed` / `PackFits` / `CyclePack` |
| `src/npc_backpack.h` | 时机判断（纯函数） |
| `src/npc_backpack.cpp` | 原版使用与消息（游戏侧），签名核对 |
| `src/npc_backpack_signatures.inc` | 生成的签名（`tools/npc_backpack_sigs.py`） |
| `src/npcai.cpp` | `Backpack`、`ArmsOf` 排除、安装 |
| `src/support_loadout.h` | 语法、变体位、文件名 |
| `src/room_menu.h` / `src/room_ui_logic.h` / `src/hudtext.inc` | 菜单第 4 栏与文字 |
| `src/room_sync_logic.h` | 房间预同步 token |
| `src/support_protocol.h` / `src/support_net.*` / `src/support_dispatch.cpp` | `kExtBackpack` 与房主降级 |
| `tools/support_loadout.py` | 安装器侧语法、文件名、`with_pack` 生成 |
| `tools/npc_backpack_check.cpp` | 离线检查（表、语法、抛物线、时机、友军、节奏） |
| `tools/npc_core_check.cpp` | 生产 `ArmsOf` / `Drive` 的背包场景（记录式替身） |
| `tests/npc_backpack_native_audit.py` | 对 EDF.dll 的字节与跳转核对（无 DLL 跳过） |
| `tests/npc_backpack_guard.py` | 源码守卫 |

## 7. 未实机验证的风险（实测时重点看）

1. **NPC 模板加副槽后的创建**（M）：数据格式有原版先例（打桩重装），但游骑兵 / 翼人 AI 模板带 Weapon_Sub 没有先例；
   生成的模板若让 `0x592FA0` 或资源预载出问题，表现会是那名士兵创建失败（`support_soldier.cpp` 记录阶段并停用后续生成）。
2. **NPC 上背包动作能否完整播放、武器是否在动作中出手**（M）：同 CAS、同状态函数，但 NPC 的上半身动作层 `+0x1A68`、
   `0x5781D0` 换动作集对 AI 士兵是否有额外前提没有逐条追到。最坏情况：动作不播、`+0x140` 一直非空 → 这名士兵背包「一直忙」，
   主武器照常（插件只在 `+0x140` 空时出手，不会卡住 AI 主逻辑）。
3. **出手方向**（M）：手雷沿士兵视线加 `FireVector` 抬升出手是对玩家的读法；NPC 的瞄准点若由 AI 目标而不是视线决定，
   抛物线会偏（日志 `NPCAI soldier ... backpack ...` 有每次出手的距离）。
4. **5 星属性**（M）：AI 模板载入的武器按 8 个 5 造，背包武器的伤害 / 爆炸半径 / 装填会是该武器 5 星档（不是玩家自己的星级）。
5. **短剑装填**（M）：`+0x690` 的含义（装填方式）与 `+0x13B` 作为装填键是按原版长按分支照搬，没有对每把短剑核实。
6. **联机**（M）：消息 0x10 的收发与原版玩家同一路径，但支援士兵的 NetworkObject 发这条消息、对端副本播放动作没有两机实测；
   对端缺文件时看不到投掷（伤害由哪台机器结算按原版规则）。
7. **房间预同步与旧版**：带背包的 token 会让旧版成员拒绝整张预同步清单（只影响旧版成员的提前生成，任务里仍走待生成清单）。
