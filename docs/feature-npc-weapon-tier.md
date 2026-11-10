# 援军士兵：与玩家同数值的武器，低一档

> 需求与原话：[feedback-2026-11-08-npc-weapon-tier.md](feedback-2026-11-08-npc-weapon-tier.md)；
> 任务乘员 / 救援机组 / X-Morph 步兵（2026-11-10）：[feedback-2026-11-10-crew-player-arms.md](feedback-2026-11-10-crew-player-arms.md)。档位表（生成文件）：[npc-weapon-tiers.md](npc-weapon-tiers.md)。
> EDF.dll TimeDateStamp `0x678CCB46`，地址都是 RVA。H = 指令直接读到（`tools/edfre.py` 只读反汇编，2026-11-09 复核）；
> M = 有代码证据的推断；L = 推测。**没有开游戏实测。**

## 1. 原版机制（逆向复核）

| 地址 | 内容 | 级别 |
|---|---|---|
| `0x54E740` SetLevel | `+0x498` = 传入等级 × `game_object_level_adjust`；`0x54E85F call 0xD7AE0` → `+0x49C`（× 当前难度行 lv 区间按任务进度插值）；`0x54E902 call 0x118A4B0(obj,&8)` 置 `+0x1A` bit3 并调 `vt+0x30`（OnLevel） | H |
| `0x596820` 士兵 OnLevel | `0x59684B..0x596875`：HP = 耐久 × (`+0x49C`/10) 经 `0x59686D call 0xD7810`（友军 HP 系数）；`0x5968AE mov rcx,[GameData]` / `0x5968B5 call 0xD77B0`（xmm1 = `+0x49C`/10，友军攻击系数）/ `0x5968C8 movss [rdi+1CF0h],xmm0`。`0xD77B0` 全程序只有这一个调用点 | H |
| `0x59ABB0` 士兵每帧（`0x59B4A6..0x59B584`） | bit3 置位：xmm1 = `+0x1CF0`（自由小队掩护预算期再 ×常数）；否则 xmm1 = xmm8（1）。再 × `+0x398`，写给 `+0x1950` 表里**每一把**武器的 `weapon+0x788` | H |
| `0x594498..0x5944A9` 士兵构造的 `soldier_load_weapon` 循环 | `lea r8,[rbp+150h]`（8 字节星级）、`lea rdx,[rbp+188h]`（路径 std::wstring）、`mov rcx,r13`（human）、`call 0x58F360`。星级缓冲在 `0x59436A..0x59439B` 清 0 后 `memset(..,5,8)`：AI 模板的每把武器都按 5 星造 | H |
| `0x58F360` | 造武器并追加进 `+0x1950`；另一个调用点 `0x5A4484`（不是构造循环，不动它） | H |

所以一发子弹的伤害 = AmmoDamage × `weapon+0x788`，NPC 的 `weapon+0x788` = 友军攻击系数（难度行第 4 项 [1]）× 有效等级/10 × `+0x398`，
玩家（不在 AI 表）= 1 × `+0x398`。NPC 模板的 AI 武器是标量（`AiSoldierRifle01` AmmoDamage 5），玩家武器按星级插值。

## 2. 新规则

对**本插件造出的士兵**（`support_dispatch.cpp` 的步兵班、载具 / 运输机带来的士兵、X-Morph 步兵、海上救援机组，
以及 `mission_crew_support.cpp` 给任务载具补的乘员；单机与联机同一规则，基准玩家见 §2.4）：

1. **武器**：模板的主武器（`soldier_load_weapon` 第 0 把，14 个原版模板都是主武器；重装的第 1 把是 AI 盾，不动）换成 WEAPONTABLE
   的玩家武器，星级用叫援军玩家那把武器的星级（不是写死的 5）。
2. **攻击系数**：这样的士兵在 OnLevel 里拿玩家的 1，不拿难度的系数；于是 `0x59ABB0` 写出的 `weapon+0x788` = 1 × `+0x398`，
   与玩家同一规则。
3. **血量不改**：`0x59686D call 0xD7810` 不动，援军血量仍按原版随难度 / 进度缩放（维护者只说了伤害）。
4. 不改：原版任务脚本生成的友军；测试场行。这些仍是原版模板、原版规则。
   （2026-11-08 版本还把任务载具乘员、海上救援机组、X-Morph 步兵列为不改项；2026-11-10 维护者改为「拿玩家武器」，见 §2.4。）

### 2.1「低一档」（表驱动，`src/npc_tier.h`）

- 每个士兵种类带它那类武器的若干「系列」（`tools/gen_npc_tiers.py SERIES`：按 `docs/weapons.csv` 的类别 + 英文名正则；
  系列内按等级、再按 id 排序），生成 `src/npc_tiers.inc` 与 [npc-weapon-tiers.md](npc-weapon-tiers.md)。CI 用 `--check` 校验。
- **基准**：叫援军玩家当前兵种装备槽里（按槽顺序）第一把属于该种类系列的武器；没有就取已拥有（武器记录 owned 位）里等级最高的；
  还没有就取该种类等级最低的那把、0 星（新存档）。
- **低一档**：基准在系列里的前一型号，星级沿用基准的；基准是系列第一把：同型号、每项星级 -1（0 保持 0；全 0 就是它本身）。
- 星级来自 GameStatus 武器记录（`weapon_records.h`：u32 标志、8 字节星级，`docs/loadout-re.md`），上限 10。

### 2.2 实现

- `src/support_soldier.cpp`：
  - `RedirectCall(0x5944A9 → LoadWeaponHook)`：只在 `Spawn` 正在造一名带武器的援军时（`arming.on`），把**第一次**武器载入换成
    玩家武器路径 + 星级，并记下是哪个 human；`Spawn` 拿到对象后核对确实是它，否则删掉、不出动（不会退回模板武器）。
  - `RedirectCall(0x5968B5 → stub → CoefHook)`：stub `mov r8,rdi` 把士兵指针交给钩子；钩子只对本模块 `owned[]` 里登记为
    `armed` 的对象返回 1，其余（原版友军、敌人、本插件不带武器的士兵）原样调 `0xD77B0`。判据是插件自己登记的对象，不是地址特例。
  - 两个钩子同进同出；任一点字节不符就记一行日志、`SupportSoldierArmsReady()` 为 false，带武器的计划被 `Validate` 拒绝（不出动）。
- `src/npc_arms.cpp`：`LocalRequesterArms` 读本机玩家（GameStatus 槽 0）的当前兵种装备与武器记录，按 `PickArms` 给每个种类挑一把；
  `PreloadNpcArms` 在任务开始随原版预载排队；`NpcArmsLoaded` 在生成前确认武器已载入（`0x7A3780` 排队、`0x7A3600` 查完成），
  没载完返回 pending（事务等它），排不进就 refused。
- 单机：`support_dispatch.cpp` 离线路径规划后给每名士兵填 `arms`，等 `NpcArmsLoaded` ready 再生成。

### 2.3 背包武器的双重缩放

背包武器（`tools/support_loadout.py with_pack`）是 5 星的玩家武器，也在 `+0x1950` 表里，原来同样乘难度系数：炼狱下等于玩家 5 星 ×8–14。
背包只出现在援军步兵班的预设上（`CrewColour` 只有颜色，乘员没有背包），而这些士兵在新规则下都带玩家武器、拿系数 1，
所以背包武器也是 1 × `+0x398`，双重缩放消失。剩下一种情况：本机武器钩子没装上（字节不符，已记日志）时士兵退回原版、背包仍乘系数——
这属于「安装 / 版本坏了」，不另写补偿。

### 2.4 任务载具乘员、海上救援机组、X-Morph 步兵（2026-11-10）

维护者：「任务载具乘员、海上救援机组、X-Morph 步兵目前按原版规则，不拿玩家武器。改成拿玩家武器」。三类都走 §2.2 的同一对钩子
（武器载入换第 0 把、系数钩子对 `armed` 返回 1），只是「基准玩家」和「种类」要各定一条确定、各机一致的规则：

| 对象 | 基准玩家（谁的武器） | 种类 / 系列 | 载入时机 | 代码 |
|---|---|---|---|---|
| 任务载具乘员（`kMissionCrewCatalog`） | **造这份计划的机器的玩家**：单机 = 本机玩家；联机只有房主造（`Request` 要求 `OnlineHostOnly`）= **房主** | 乘员是步枪模板（`kSupportRangerResource` / `kSupportLeaderResource`）→ `rifle` 系列 | 房主 / 单机：本机玩家的武器在任务开始随原版预载（`PreloadNpcArms`）；`Request` 先问 `NpcArmsLoaded`，pending 就不派（`mission_crew.cpp` 下一帧再问）。客户端：`Hooks::prepare` 里 `NpcArmsLoaded` 载完才 READY | `mission_crew_support.cpp Build / Request / Apply / ValidateMissionCrewPlan` |
| 海上救援机组（救援目录项） | **落水求救的玩家**（请求发起者）：单机本机玩家；联机由求救的那台机器 `SubmitSupportRequest` 带上（不再对 `ownChannel` 例外） | 机组种类按 `SupportCfg().aircraftCrew`（默认步枪） | 单机 `RescueHere`：`GiveLocalArms` 后 `NpcArmsLoaded`，pending / refused 就不出动、写救援提示，`heli.cpp` 过重试间隔再叫；联机同援军（房主 BEGIN 前、各机 READY 前） | `support_dispatch.cpp RescueHere`、`support_net.cpp SubmitSupportRequest` |
| X-Morph 步兵（`SupportWeapon::xmInfantry`） | 叫援军的玩家（它本来就是援军调度叫来的） | 它的士兵 SGO 是游骑兵 `N601_COMMON_RANGER_AF` 换模型（`pylib/xm_infantry.py STOCK`，第 0 把是 `AiSoldierRifle01`），原作是机枪步兵 → **游骑兵突击步枪系列**，与 `rifle` 同表（`tools/gen_npc_tiers.py RANGER_RIFLES`） | 同援军 | 只改生成表；`KindArmed(xmInfantry)` 变 true，`LocalRequesterArms` 自动给它挑 |

- 为什么任务乘员用「造计划的机器」而不是「全体玩家里某规则」：乘员计划只有一个造它的人（房主），武器字随计划下发，
  各机只照做、不各自重算，不需要再约定玩家排序；房主没装武器钩子 / 预载不符（`LocalRequesterArms` 全 0）时乘员是原版模板，
  这时计划里本来就没有武器字，各机仍一致。客户端没有钩子而计划带武器：`ValidateMissionCrewPlan` 拒绝（不退回原版）。
- `LocalRequesterArms` 现在也要求资源预载（`0x7A3780` / `0x7A3600`，字节同 `debug_spawn.cpp`，H）对得上：对不上就给不出武器、
  全 0（任务开始记一行日志），否则任务乘员会每帧被 `NpcArmsLoaded` 拒绝、一直刷日志。
- 共用的「把本机玩家的武器填进计划」收成 `npc_arms.h GiveLocalArms`（离线援军、离线救援、任务乘员三处）。

## 3. 联机

- 发起方决定：`SubmitSupportRequest` 调 `LocalRequesterArms`，`RequesterArms`（每种类一个 64 位字：bit63 有、bit0-15 武器 id、
  bit16-47 8 个星级各 4 位）放进请求消息原本全零的 unit 字段（每种类 6 字节：u16 id，0xFFFF 无；u32 星级）。
- 房主规划后由 `Session::Advance` 给每名士兵填它种类的那个字（规划器不填）；每条 unit 消息带上（低 32 位在 request 字，高 32 位在末尾保留字）。
- 每台机器 `Hooks::prepare`（`NpcArmsLoaded`）确认武器已载入才 READY；房主自己也在 BEGIN 前问一次（先派生 NetId，只派生一次）。
- 编解码校验：武器必须是该种类系列里的、星级 ≤10、无关位为 0；载具不带；BEGIN 不带。
- 支援线协议仍是 v2；**房间协议 `kAllForcesRoomProtocol` 7 → 8**（`src/mod_room.h`），旧版本不能进房。
- 2026-11-10（§2.4）：**房间协议 8 → 9**。任务乘员的预备计划（`SubmitPrepared`，`t.external`，`Session::Advance` 不覆盖它的 `arms`）
  每个 unit 消息带房主挑的武器字；8 的客户端会无视它造原版士兵（各机不一致），且会拒绝 X-Morph 种类的武器字，所以升版拒旧。
  救援请求现在也带求救者的 `RequesterArms`。线格式不变（支援线仍 v2）。

## 4. 载具与战机（评估）

| 对象 | 生成顺序 | 火力是否随难度 | 依据 |
|---|---|---|---|
| 援军地面载具（`support_spawn.cpp`） | CreateObject → `ApplyMissionSetup`（slot 46 → `0x62F800`）→ SetTeam → SetLevel(1) | 否（M） | `0x62F800` 在 bit3 未置时 `+0x678` = setup 伤害倍率 × 1 × `+0x67C`；车辆 OnLevel（`vt+0x30` = `0x6731C0`，经 `0x62E770`）不重算 `+0x678`（H）；之后是否还有别的路径重跑 setup 未逐一核对 |
| 援军飞机 / 直升机（`jet_spawn.cpp PrepareSupportAircraft`）、发射的战机（`SpawnJet`）、潜舰母舰（`subcarrier.cpp`） | CreateJet → SetTeam → `LevelVehicle`（SetLevel 1，置 bit3）→ `PrepareNpcVehicle`（`ApplyMissionSetup`）/ RideAi 的 setup | **是**（H 指令，M 顺序） | bit3 置位时 `0x62F89E..0x62F8CC`：xmm7 = `+0x49C`/10，xmm6 = `0xD7870`（友军攻击系数，全程序唯一调用点 `0x62F8CC`）；`0x62F9AA` `+0x678` = xmm6 × xmm7 × setup 伤害倍率；`0x633842` 建武器存进槽 `+0x40`，每帧 `0x630446` `weapon+0x788 = veh+0x398 × 槽+0x40` |
| 玩家呼叫的载具 | 请求的 setup（`tools/call_weapons.py vehicle_sgo`：`[耐久, 伤害] = request_tier(曲线, 等级)`），不 SetLevel | 否 | `+0x678` = request_tier 伤害倍率 × `+0x67C` |

结论：援军飞机类的火力受难度 + 进度缩放，地面援军载具（静态看）不受。**这一块本分支没有改代码**，原因：
`+0x678` 的耐久与伤害共用同一个等级项 xmm7，只换 `0xD7870` 的返回值去不掉等级项，要么得返回「目标 / 等级」这种补偿系数（违背根因规则），
要么改生成顺序（setup 在 SetLevel 之前）——那会同时改掉血量缩放（维护者只要求改伤害），且 SetLevel 同时承担 AI 表登记，
飞机的 setup 还会被 RideAi（`0x633063` → slot 46）再跑一次，顺序依赖要实机确认。建议的做法（待维护者确认后另开分支）：
援军飞机的 SGO（`pylib/vcobjects.py JET_TIER`）按对应玩家呼叫的等级取 `request_tier` 倍率，生成顺序改为「setup → SetLevel」，
血量是否随之改为玩家曲线需要维护者决定。

## 5. 测试

- `tests/npc_tier_test.cpp`：档位表形状（每系列唯一首型号、前一型号同系列不高于本型号）、低一档规则、基准选择（装备 > 已拥有最高 > 最低 0 星）、
  64 位打包与校验、CreateObject 路径。
- `tests/support_soldier_test.cpp`：带武器的士兵只换第 0 把武器、星级是请求的；第二把（重装 AI 盾）不动；不带武器的照旧；
  系数钩子只对插件登记为带武器的士兵返回 1，原版 / 其他士兵走原函数；别种类武器、载入没生效、钩子没装上都不出动。
- `tests/support_protocol_test.cpp Arms()`：请求 / unit / BEGIN 的编解码与非法值拒绝、支援线仍 v2、三机房间里每台造出同样武器、
  房主 BEGIN 等武器载入、载入失败则拒绝并通知请求方。
- `tests/player_identity_guard.py`：房间协议号 9。
- 2026-11-10（§2.4）：`tests/mission_crew_support_test.cpp`（乘员每席带造计划机器的步枪武器、载具不带；单机载入中不派 / 载不了不派且不退回原版；
  房主载完才提交事务；客户端照房主的武器造、没钩子拒绝；没挑出武器则原版）；`tests/support_dispatch_test.cpp`（单机救援的驾驶员与炮手各带本机玩家
  该种类的武器、载入中 / 载不了都不出动）；`tests/support_protocol_test.cpp ExistingArms()`（房主的乘员预备计划在三机房间里各机武器一致、
  各机都问过载入、别种类武器拒绝）与 `Arms()`（X-Morph 步兵的步枪武器字往返、长矛拒绝）；`tests/npc_tier_test.cpp`（每个种类都带武器；
  X-Morph 行与步枪行逐行一致、同一玩家挑出同一把）。

## 6. 需实机确认

- 换上的玩家武器在 AI 士兵手里能正常开火（AI 用的射程 / 弹道参数来自武器本身；翼人能量、重装双手武器等）。
- `DamageStats=1` 下同一把武器在玩家手上与援军手上每发伤害一致（简单 / 炼狱 × 首关 / 末关）。
- 任务中途叫援军时武器按需载入（`NpcArmsLoaded` pending → ready）不卡顿；联机三机各自载入后同时出现。
- §2.4：任务载具乘员换了武器后仍能正常登车、在车上开火 / 下车后用玩家武器；联机客户端载入房主的武器后乘员同时出现。
- §2.4：救援直升机的驾驶员 / 炮手拿玩家武器后炮手座（门炮）照常开火；X-Morph 步兵的动力装甲模型拿玩家步枪时握持姿势正常。
