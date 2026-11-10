# 援军士兵：与玩家同数值的武器，低一档

> 需求与原话：[feedback-2026-11-08-npc-weapon-tier.md](feedback-2026-11-08-npc-weapon-tier.md)。档位表（生成文件）：[npc-weapon-tiers.md](npc-weapon-tiers.md)。
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

只对**本插件援军调度叫来的士兵**（`support_dispatch.cpp` 的步兵班、载具 / 运输机带来的士兵；单机与联机同一规则）：

1. **武器**：模板的主武器（`soldier_load_weapon` 第 0 把，14 个原版模板都是主武器；重装的第 1 把是 AI 盾，不动）换成 WEAPONTABLE
   的玩家武器，星级用叫援军玩家那把武器的星级（不是写死的 5）。
2. **攻击系数**：这样的士兵在 OnLevel 里拿玩家的 1，不拿难度的系数；于是 `0x59ABB0` 写出的 `weapon+0x788` = 1 × `+0x398`，
   与玩家同一规则。
3. **血量不改**：`0x59686D call 0xD7810` 不动，援军血量仍按原版随难度 / 进度缩放（维护者只说了伤害）。
4. 不改：原版任务脚本生成的友军；任务载具的乘员（`mission_crew_support.cpp`，不是玩家叫来的，没有「叫援军的玩家」）；
   海上救援的机组（`support_dispatch.cpp RescueHere`，只开飞机）；测试场行；X-Morph 步兵（不是 EDF 兵，用自己的 X-Morph 武器）。
   这些仍是原版模板、原版规则。

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

## 3. 联机

- 发起方决定：`SubmitSupportRequest` 调 `LocalRequesterArms`，`RequesterArms`（每种类一个 64 位字：bit63 有、bit0-15 武器 id、
  bit16-47 8 个星级各 4 位）放进请求消息原本全零的 unit 字段（每种类 6 字节：u16 id，0xFFFF 无；u32 星级）。
- 房主规划后由 `Session::Advance` 给每名士兵填它种类的那个字（规划器不填）；每条 unit 消息带上（低 32 位在 request 字，高 32 位在末尾保留字）。
- 每台机器 `Hooks::prepare`（`NpcArmsLoaded`）确认武器已载入才 READY；房主自己也在 BEGIN 前问一次（先派生 NetId，只派生一次）。
- 编解码校验：武器必须是该种类系列里的、星级 ≤10、无关位为 0；载具不带；BEGIN 不带。
- 支援线协议仍是 v2；**房间协议 `kAllForcesRoomProtocol` 7 → 8**（`src/mod_room.h`），旧版本不能进房。

## 4. 载具与战机

| 对象 | 生成顺序 | 火力是否随难度 | 依据 |
|---|---|---|---|
| 援军地面载具（`support_spawn.cpp`） | CreateObject → `ApplyMissionSetup`（slot 46 → `0x62F800`）→ SetTeam → SetLevel(1) | 否（M） | `0x62F800` 在 bit3 未置时 `+0x678` = setup 伤害倍率 × 1 × `+0x67C`；车辆 OnLevel（`vt+0x30` = `0x6731C0`，经 `0x62E770`）不重算 `+0x678`（H）；之后是否还有别的路径重跑 setup 未逐一核对 |
| 援军飞机 / 直升机（`jet_spawn.cpp PrepareSupportAircraft`）、发射的战机（`SpawnJet`）、潜舰母舰（`subcarrier.cpp`） | CreateJet → SetTeam → `LevelVehicle`（SetLevel 1，置 bit3）→ `PrepareNpcVehicle`（`ApplyMissionSetup`）/ RideAi 的 setup | **是**（H 指令，M 顺序） | bit3 置位时 `0x62F89E..0x62F8CC`：xmm7 = `+0x49C`/10，xmm6 = `0xD7870`（友军攻击系数，全程序唯一调用点 `0x62F8CC`）；`0x62F9AA` `+0x678` = xmm6 × xmm7 × setup 伤害倍率；`0x633842` 建武器存进槽 `+0x40`，每帧 `0x630446` `weapon+0x788 = veh+0x398 × 槽+0x40` |
| 玩家呼叫的载具 | 请求的 setup（`tools/call_weapons.py vehicle_sgo`：`[耐久, 伤害] = request_tier(曲线, 等级)`），不 SetLevel | 否 | `+0x678` = request_tier 伤害倍率 × `+0x67C` |

2026-11-08 的结论（当时没改代码）：援军飞机类的火力受难度 + 进度缩放，地面援军载具（静态看）不受。当时没改的原因：
`+0x678` 的耐久与伤害共用同一个等级项 xmm7，只换 `0xD7870` 的返回值去不掉等级项，要么得返回「目标 / 等级」这种补偿系数（违背根因规则），
要么改生成顺序（setup 在 SetLevel 之前）——那会同时改掉血量缩放（维护者只要求改伤害），且 SetLevel 同时承担 AI 表登记，
飞机的 setup 还会被 RideAi（`0x633063` → slot 46）再跑一次，顺序依赖要实机确认。建议的做法（待维护者确认后另开分支）：
援军飞机的 SGO（`pylib/vcobjects.py JET_TIER`）按对应玩家呼叫的等级取 `request_tier` 倍率，生成顺序改为「setup → SetLevel」，
血量是否随之改为玩家曲线需要维护者决定。2026-11-10 维护者定了等级来源，实现见 §4.1（没有改生成顺序，也没有用补偿系数）。

### 4.1 援军载具与战机的火力按等级（2026-11-10）

维护者原话见 [feedback-2026-11-10-air-support-level.md](feedback-2026-11-10-air-support-level.md)：「援军载具和战机的火力 改成根据等级来。
这个等级是类似联机模式的那个，等级会随着难度变化」。只改火力，血量维持原样。

**等级（`src/support_level.h SupportLevel`）**：原版联机剧情（`DEFAULTPACKAGE/CONFIG.SGO` ModeList `GameMode_OnlineScenario`）
成员 7[难度] 的元素 6，也就是联机房间的武器等级上限（[weapon-level-cap-re.md](weapon-level-cap-re.md) §1，H：本机 Root.cpk 读出、
`EDF.dll 0xE2710` 逐条读过）。原始值是「界面等级 / 25」，按任务在列表中的位置（`0xD7B60` = `GS+0x48` / (任务数 − 1)，H）插值，
`0xE2710` 的算法逐步照抄（进度夹到 [0, 1]、`floorf`、最后一格直接取末元素、×0.25×100 截断）：

| 难度（`GS+0x4C`） | 元素 6 | 首关等级 | 末关等级 |
|---|---|---|---|
| 简单 / 普通 | `[0.1, 1.1]` | 2 | 27 |
| 困难 | `[0.2, 2.1]` | 5 | 52 |
| 硬核 | `[1.4, 3.1]` | 35 | 77 |
| 炼狱 | `[2.4, 3.2, 3.999, 3.999]` | 60 | 99 |

不论当前是哪个模式（离线剧情、任务包、EDF5 战役），都用联机剧情这张表（维护者说的「联机模式的那个」）；进度是当前模式自己列表里的位置。
联机：难度与任务序号全房间相同，每台机器自己造的援军副本算出同一个等级，不需要额外同步字段。

**伤害倍率（`LevelDamage`）**：= `tools/call_weapons.py request_tier(曲线, 等级 / 25)` 的伤害列，即玩家呼叫同等级的这种载具时它带的倍率：

| 援军 | 曲线 | 原版请求 |
|---|---|---|
| 援军飞机 / 直升机、出击的战机、发射的战机（`SpawnJet` 友军）、潜舰母舰放出的 | N9 Eros（`EROS_REQUESTS` eWeapon394..399） | 插件所有飞机呼叫都用这条（`request_family`） |
| 布莱克战车（`V505_TANK_MISSION`） | Blacker（`GROUND_VEHICLES['drill'].family`，EWEAPON418/421/425/428/433） | 伤害列（第 1 项是耐久列，不用） |
| 救援装甲运兵车（`V507_RESCUETANK_AI`） | Grape（EWEAPON400/404/408/411/414/415，都带 `v507_rescuetank.sgo`） | |
| 民用轻卡 | 无武器，不处理 | |

三条曲线的点与联机上限表写死在 `support_level.h`，`tools/selftest.py support_level_tables_match_the_game` 有游戏时从本机 Root.cpk 读出逐项比对（无游戏只比族）。

**写法（`src/support_firepower.cpp`）——只改伤害、不碰血量、不用补偿系数**：

- 生成时（setup 之前）`MarkSupportFirepower(载具, 曲线)` 登记（ObjRef，按控制块判活，任务开始清空），**登记时就取好等级与倍率**：
  一个任务内难度与任务位置不变，之后每次 setup 写同一个值；取不到等级的载具在登记这一步就被拒绝，不会出现「setup 已按原版写完、再写等级失败」而留在原版缩放上的情况。
- 每次 mission setup（`real_driver_native.cpp` / `support_spawn.cpp` 的 `ApplyMissionSetup`，slot 46 → `0x62F800`）之后，
  `ApplySupportFirepower` 对登记过的载具直接写 `+0x678 = 登记时的 LevelDamage × +0x67C`（`+0x67C` 构造时 1.0，`0x6295FA`，H；即未置 bit3 分支的规则，
  玩家呼叫载具走的就是这条），并把每个武器槽 `+0x638[i]`（数量 `+0x648`，步长 0x48）的 `+0x40` 改成同值——setup 建武器时把
  `+0x678` 抄进槽 `+0x40`（`0x633842` 读、`0x633936` 写，H），每帧 `0x6303C4..0x63044E` `weapon+0x788 = veh+0x398 × 槽+0x40`（H），所以两处都要写。
- 飞机仍先 SetLevel(1)（bit3），setup 照原版算血量（`耐久 × setup 耐久 × 0xD78D0 友军 HP 系数 × 等级项`）；setup 写的 `+0x678` 随即被覆盖。
  生成顺序不变，血量与原版一致；不需要「目标 / 等级」这种补偿系数，也不 hook `0xD7870`（它仍服务原版任务友军）。
- 插件把原版 RideAi 槽换成自己的 `RideHook → PrepareNpcVehicle → ApplyMissionSetup`（`mission_crew.cpp`），所以援军飞机没有绕过这一步的 setup 路径（M：
  `0x633063` 的原版 RideAi 不再被调用；`0x633280` 一类别的 setup 入口只用于调试生成）。
- 登记失败（`0xD7B60` 特征码不符、取不到等级、登记表 256 满、对象已死）：生成路径删除该载具，日志一行，不退回原版缩放。
  地面援军（`SpawnSupportVehicle`）只拒绝这一台，不把这种载具标成坏资源（表满是暂时的）；`0xD7B60` 特征码不符时
  `SupportVehicleReady` 对布莱克 / 运兵车直接答「不可用」（轻卡没有武器，照常）。
  空投（`airdrop.cpp VehicleStep`）的车是原版集装箱造的：登记失败时不跑 setup（与原有的 setup 失败同一处理：日志「its mission setup FAILED」，
  车留在原地、没有建武器），插件不在集装箱的流程中途删除它。
  写入失败只剩武器槽不可读 / 访问异常（对象已坏），此时 setup 报失败。
- `SpawnJet` 的 `team` 不是友军时不登记，仍按原版（目前所有调用者都用默认的友军）。
- 之后不会再有绕过 `ApplySupportFirepower` 的 setup（H，2026-11-10 复核）：空投集装箱在 `VehicleStep` 之后写 `+0x67C = 等级`（`0x5E904E`，插件传 1.0），
  设定文字为空（`+0xC48`）且设定值类型为 0xFFFF（`+0xC68`，插件传空设定）时跳过它自己的 slot 46（`0x5E9054..0x5E906A`）；
  快照 `0x62FBF0` 只在 `+0xE30 == 2`（`0x633280` / 快照自身写入）时重跑 slot 46（`0x630153..0x63017A`），`+0xE30 == 1` 走 RideAi 槽（已换成 `RideHook`），
  援军载具不写 `+0xE30 = 2`。布莱克 / 运兵车 / 506 / 409 / 410 / 直升机基类的 slot 46（`0x61B060` / `0x61C430` / `0x61B770` / `0x64B3C0` / `0x64DA60` / `0x6530E0`）
  都是先基类 setup 再 `0x633330` 建武器（H）。全程序读 `[reg+0x678]` 的指令里，除 `0x633842` 外只有 `0x6054D5`（函数 `0x605150`，
  由 `0x607DB0` 当回调登记、`0x601DBF` 调用），它不在上述 slot 46 的直接调用里（M：没逐层展开间接调用）。

**房间协议 `kAllForcesRoomProtocol` 8 → 9**：每台机器各自造援军副本，旧版副本按旧规则打伤害，结果不一致，所以旧版不能进房。

**待实机确认**：`DamageStats=1` 下援军飞机 / 直升机 / 布莱克 / 救援装甲车每发伤害 = 武器 SGO 伤害 × 日志 `SUPPORT firepower ... damage x` 值；
各难度首关 / 末关日志里的 `level=` 与上表一致；血量与改动前相同。

## 5. 测试

- `tests/npc_tier_test.cpp`：档位表形状（每系列唯一首型号、前一型号同系列不高于本型号）、低一档规则、基准选择（装备 > 已拥有最高 > 最低 0 星）、
  64 位打包与校验、CreateObject 路径。
- `tests/support_soldier_test.cpp`：带武器的士兵只换第 0 把武器、星级是请求的；第二把（重装 AI 盾）不动；不带武器的照旧；
  系数钩子只对插件登记为带武器的士兵返回 1，原版 / 其他士兵走原函数；别种类武器、载入没生效、钩子没装上都不出动。
- `tests/support_protocol_test.cpp Arms()`：请求 / unit / BEGIN 的编解码与非法值拒绝、支援线仍 v2、三机房间里每台造出同样武器、
  房主 BEGIN 等武器载入、载入失败则拒绝并通知请求方。
- `tests/player_identity_guard.py`：房间协议号 9（§4.1）。
- `tests/support_level_test.cpp`：等级表（各难度首末关、单调、`0xE2710` 的夹取 / 插值 / 无上限）、三条请求曲线插值与两端保持。
- `tests/support_spawn_test.cpp`：地面援军在 setup 前登记、setup 后写火力；坦克 Blacker、运兵车 Grape、轻卡不登记；登记 / 写入失败即拒绝（含空投路径）。
  登记失败的坦克在 setup 前删除、不影响下一台；`0xD7B60` 不符时有武器的地面援军不可用、轻卡可用。
- `tests/support_firepower_test.cpp`：`support_firepower.cpp` 本身（私有映像、`0xD7B60` 用带原版序言的桩）：登记时取等级、之后改难度不影响；
  `+0x678` 与每个武器槽 `+0x40` 写成同值、槽数之外不动；未登记的不动；无等级拒绝；同地址重登记替换；槽数异常报失败；表满拒绝、死对象槽复用、任务开始清空。
- `tests/real_driver_native_test.cpp`：spawned setup 之后恰好调一次火力步骤。
- `tools/selftest.py support_level_tables_match_the_game`：表与本机游戏数据一致。

## 6. 需实机确认

- 换上的玩家武器在 AI 士兵手里能正常开火（AI 用的射程 / 弹道参数来自武器本身；翼人能量、重装双手武器等）。
- `DamageStats=1` 下同一把武器在玩家手上与援军手上每发伤害一致（简单 / 炼狱 × 首关 / 末关）。
- 任务中途叫援军时武器按需载入（`NpcArmsLoaded` pending → ready）不卡顿；联机三机各自载入后同时出现。
