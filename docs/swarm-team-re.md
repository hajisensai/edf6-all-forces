# 敌方（team 1）V506 载具：队伍归属逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，ImageBase `0x180000000`，下文地址都是 RVA。纯静态分析（`tools/edfre.py` + capstone，
SGO 用 `pylib/rootcpk.py` + `pylib/sgo.py` 从本机 Root.cpk 读），没有启动、操作游戏。
**确认** = 指令直接可见；**推断** = 证据一致但没直接看到或没实测。

背景：插件打算 `CreateObject → SetTeam(v, 1, true) → RideAi(v, true)` 生成敌方 Vehicle506_Helicopter，
写 `veh+0x2020/0x2021` 开火。

---

## 0. 结论速览（最重要的一条先说）

**按现在的顺序做，载具下一帧就会变回 team 2（友军）。** 原因（全部确认）：

1. RideAi 生成的 DummyVehicleRider **写死** `SetTeam(rider, 2, false)`（`0x6331CC..0x6331D3`）。
2. VehicleBase 每帧更新 `0x630250`（506 的 slot 5 `0x653680` 在 `0x653790` 无条件调用）在载具活着时，
   从 `team = 5` 开始，逐个座位取乘员队伍（≠ -1 就覆盖），最后 **`SetTeam(vehicle, team, true)`**（`0x6304C7` / `0x630520..0x630529` / `0x630589..0x630591`）。
   也就是说**载具队伍 = 乘员队伍，每帧重算**；空载具 = 5。
3. 武器队伍 `weapon+0x214` 每帧从载具 `+0x314` 同步（`0x630421`），开火时再拷进子弹参数。

所以真正决定敌我的是**乘员**的队伍。修法（推断，未实测）：RideAi 之后对每个座位上的 DummyVehicleRider
（vtable `0x17D7320`，座位 `+0x260`）调 `SetTeam(rider, 1, false)`（与 RideAi 一样 reg=false，乘员不进队伍集合），
再 `SetTeam(v, 1, true)`。之后每帧重算得到的仍是 1，武器、子弹、雷达、AI 都按 team 1 走。

---

## Q1. RideAi 生成什么乘员、给什么队伍

RideAi = 载具 slot 50 `0x633030`（506 vtable `0x17DB238` slot 50 = `0x633030`，确认）。

| 步骤 | 指令 | 含义 | 可信度 |
|---|---|---|---|
| dl=true 时 | `0x633063 call 0x62D6E0` → `0x633073 call [vtbl+0x170]`（slot 46 = 506 的 `0x61B770`） | 读 `mission_setup`，按它建武器（`0x61B839 → 0x633330`） | 确认 |
| 关武器友伤标志 | `0x6330A2..0x6330D7`：遍历 `veh+0x638` holder 数组（步长 0x48），`mov byte [weapon+0x8B6], 0` | 见 Q2 的 A26 | 确认 |
| 生成乘员 | `0x633169 call 0x835C0(*(img+0x20B2958), out, &identity(0x1FFF3D0), &InitParamBase{vt 0x1762068})` | `0x835C0` 里 `new 0x5D0` → ctor `0x5E3A90`，ctor 写 vtable `0x17D7320` = **DummyVehicleRider**（基类 ctor `0x545670`）。**不读任何 SGO**，C++ 直接 new | 确认 |
| 队伍 | `0x6331C9 xor r8d,r8d; 0x6331CC lea edx,[r8+2]; 0x6331D3 call 0x54EE70` | **`SetTeam(rider, 2, false)`，写死 team 2，不看载具队伍**，也不登记进 TeamManager 集合 | 确认 |
| 等级 | `0x6331E3 call 0x54E740(rider, 1.0f)` | | 确认 |
| 上座 | `0x6331F4 call 0x633C10(veh, rider, 0, 1)`；`0x6331FF call 0x118AF20(veh, rider)`；`veh+0xE30 = 1` | | 确认 |

- DummyVehicleRider slot 0 `0x5E3B70` 也是 `call 0x54B010` 后 `SetTeam(self, 2, false)`（确认）。slot 0 在载具 vtable 上是快照相关函数（re-notes：`0x62FBF0` 快照），
  所以**快照恢复类路径会把乘员再改回 2**（推断；单机正常游玩里何时触发未查）。插件若依赖乘员 team 1，最好每帧/定期复核。
- **SetTeam `0x54EE70` 不传播**：只写自身 `+0x314`、`+0x318`（≠-1 时）并按 reg 调 `0x5E0B70` / `0x5E1C60`；中间调的 vfunc `+0xB0` 在 506 上是 `0x54ABD0: mov al,1; ret`（确认）。
  所以对载具 SetTeam 不会改乘员（确认）。
- 反方向是有的：载具每帧取乘员队伍（见 §0 第 2 条，确认）。
- 脚本 `SetTeamID(Object, TeamID)`（包装 `0x1C3AA0`）→ `0x1C36D0` 会递归遍历 `obj+0x550` 子对象链再 `SetTeam(obj, team, true)`（确认）；
  乘员经 `0x118AF20` 挂成载具子对象，所以脚本这条路会连乘员一起改（推断：`+0x550` 链就是 `0x118AF20` 维护的那条）。

## Q2. 子弹的攻击方队伍来自谁

506 的三把武器和 409 炸弹全是 `Weapon_VehicleShoot`（SGO 确认：`V_506HELI_GATLING01_L/_R` AmmoClass SolidBullet01；`V_506HELI_MISSILE01` MissileBullet01；`V_409HELI_BOMB01` GrenadeBullet01）。

数据链（全部确认）：

| 环节 | 指令 | 说明 |
|---|---|---|
| holder 归属 | `0x62A51B mov [holder+0x38], r14`（`0x629450` VehicleBase 构造，r14 = 载具）；`0x62A516` holder+0x18 = 骨骼节点 | **每个武器 holder 的 owner 都是载具本身**；座位武器列表（seat+0xC8）存的是指向这个 holder 的指针（`0x62A57E` / `0x62A5E6`） |
| 建武器时 | `0x633961..0x633976`（`0x633330`）/ `0x632FDB..0x632FEE`（`0x632E90`）：`weapon+0x214 = (holder+0x38)->+0x314`，owner 为空时 -1 | 同时 `weapon+0x8B6 = 1`（`0x633950` / `0x632FCB`） |
| 每帧同步 | `0x63041A..0x630427`（`0x630250` 载具更新的 holder 循环）：`weapon+0x214 = (holder+0x38)->+0x314`；`0x633DD0` 同样（`0x633E34..0x633E42`） | 武器队伍 = **载具当前队伍**，不是乘员的，也不是“持有者”另算 |
| 开火 | `0x6976CF mov eax,[r15+0x214]; 0x6976D6 mov [r12+0x90], eax`（WeaponBase 开火 `0x696FD0`，r12 = `weapon+0x800` 或其副本 `weapon+0x9E0`） | 即外层 InitParam `+0x90`（= 内层子弹参数 `+0x60`，`weapon+0x890`）。`0x6B1380` / `0x6B08A0` / `0x6B2750` 也只从 `+0x214` 拷到 `+0x890` |
| 子弹核心 | core = `0x9A0 + 内层参数偏移` → `core+0xA00`；core init `0x232015..0x232023`：`GDI+0x24 (core+0x754) = core+0xA00` | 攻击方队伍 |

注意时序：同一帧里武器同步（`0x630421`）在队伍重算（`0x630591`）**之前**，所以武器队伍比载具晚一帧（确认顺序；影响可忽略）。

### 友伤标志 A26（`weapon+0x8B6` → `core+0xA26` → `GDI+0x60 |= 0x20`）

- `0x23206C cmp byte [core+0xA26],0 … 0x2320CD or word [core+0x790], 0x20`（确认）。
- 建武器时置 1（玩家驾驶的默认：有友伤），**RideAi 清 0**（`0x6330C9`，确认）。之后若有代码再建武器（`0x633330` / `0x632E90`），会重新置 1（确认），需要插件再清。

### 伤害三道关（全部确认）

1. **直击候选**（`addBody 0x232AA0`，见 bullet-pass-re.md §3.2）：只有 `A27||A28` 才查阵营，A28 = AmmoExplosion > 阈值（`0x68DB14`）。
   机炮（SolidBullet01，AmmoExplosion 0）不查 → 物理上会打到任何对象；导弹 / 炸弹（有爆炸）对 `relation==1`（友方）跳过。
2. **受击方** `0x547C30`：`rel[GDI+0x24][target+0x314] == 1`（友方）时：GDI+0x60 无 0x20 且目标无 `+0x380 & 0x100000` → 直接 return 不扣血（`0x547DFC je 0x548625`）；
   有 0x20 → 伤害 × `0xD8110` 难度友伤系数（表 `0x1767DA8` = 0.1 / 0.25 / 0.5 / 0.75 / 1.0）。非友方不打折。
3. **范围伤害** `0x542490`：`GDI+0x60 & 0x20` 为 0 且 `GDI+0x24 != -1` 时挂过滤 lambda `0x543DD0`：只收 `rel[target+0x318][GDI+0x24] == 2`（`0x5E1540`）或 `+0x380 & 0x100000` 的目标。

TeamManager 默认关系表（ctor `0x5E03C0`，确认，`rel[行][列]`，行偏移 = team×0x38+0x18）：对角 1；`0↔1 = 2`、`0↔2 = 1`、`1↔2 = 2`、`1↔4 = 2`（`0x5E0614/0x5E0623/0x5E0647/0x5E0659/0x5E0680/0x5E068F/0x5E06B7/0x5E06C6`）；其余（含 3、5、6 的非对角）为 0。

**结论**：载具队伍真正是 1 时（见 §0 修法），且 RideAi 之后没有重建武器（A26=0）：
- 机炮弹：打 team 0（玩家）、team 2（友军 NPC / 友军载具）都扣全额；打到 team 1 同伴会被挡住但不扣血（受击方友方 return）。确认（链路）/推断（实测）。
- 导弹、GrenadeBullet01 炸弹 / 派生 charge：直击跳过 team 1；爆炸范围只伤 team 0、2、4（诱饵），**不伤 team 1，也不伤 3、5（无主载具）**。确认（链路）。
- 若乘员还是 2（现在的做法），载具一帧后变 2，子弹队伍也是 2：玩家方不掉血、敌人掉血 —— 即“敌方载具”实际上仍在帮玩家打。

## Q3. 脚本 `GetTeamObjectCount(TEAM_ID_ENEMY)`

- 声明串 `int GetTeamObjectCount(::TeamID )` `0x1796D30`，注册点 `0x1EB5C7`，函数指针 `0x1EB52F lea rcx,[rip-0x362D6]` = **`0x1B5260`**（确认）。
- `0x1B5260`：`team > 6` 返回 0；否则 `jmp 0x5E13E0(*(img+0x20B2978), team, true)`（确认）。
- `0x5E13E0`：锁 `mgr+8`，遍历 `rows[team]+0` 的 `std::set<GameObject*>`（节点 `+0x20` = 对象），计数条件：
  `!(obj+0x380 & 0x2000)`（第三参为 true 时）且 `obj+0x2E8 == 0`（确认 `0x5E1445..0x5E1462`）。
  - 这个 set 就是 `SetTeam(…, true)` 经 `0x5E0B70` 插入的那个（`0x5E0B94..0x5E0BA8` 按 `+0x314` 找行，确认）。
  - **已死未删（+0x2E8 置位）不计数**（确认）。
  - `+0x380 & 0x2000` 的对象不计数（确认）；该位由少数对象类 ctor 置位（`0x265FB7`、`0x3272D4`、`0x3FA471`），`EventFactor_ObjectAreaCheck`（`0x1FB4B0` / `0x1FBDF0`）也看它；
    `EnumConflictObject 0x5E0F20` 只看 `+0x2E8` 和 `+0x380 & 0x80`，**不看 0x2000**（确认）。推断：0x2000 = “不计入队伍数量”。
- `TEAM_ID_*` 枚举（注册点 `0x1E324D..0x1E32D1`，确认）：PLAYER 0、**ENEMY 1**、FRIEND 2、NEUTRAL 3、VEHICLE 5。
- DummyVehicleRider 用 reg=false，不进任何集合，不计数（确认）。
- **影响**（推断）：team 1 的插件载具会让 `GetTeamObjectCount(TEAM_ID_ENEMY)` +1，脚本里“敌人全灭才推进/过关”的等待会等它死。
  想让它不卡任务流程，可给载具置 `+0x380 |= 0x2000`（仍留在 team 1 集合里，雷达、AI 选目标照常），但要注意载具每帧 `SetTeam(v, team, true)` 不会动这一位（`0x548D50` / `0x548E50` 也不碰，确认），其它写 `+0x380` 的路径没逐个查。
  不要用 reg=false 的办法：载具更新每帧以 reg=true 重登记（`0x630589 mov r8b,1`），且不在集合里会让 EnumConflictObject 找不到它（AI、雷达看不见）。

## Q4. 锁定点登记：玩家能锁 team 1 的 506 吗

- 锁定登记：`GameObjectBase::AddLockTarget 0x547A50` → `0x22E210(*(img+0x20B2AB0), out, obj, 0)` 新建锁定点（ctor `0x22DB80`：`+0` type = 0、`+8` 对象、`+0x10` 位置初值 obj+0x90、**`+0x29/+0x2A` = 1/1**、`+0x48 = -1`），
  再置 `+0x44=1`、`+0x48 = obj+0x378`，存进 `obj+0x360` 列表（确认）。
- 唯一调用者 `0x6C7500`（入口 `0x6C74E0`）：按模型 MAB 定位点表逐项（名字经 `0x11001F0` 找骨骼）各登记一个（确认）。
- VehicleBase 构造 `0x629450` 在 `0x629B1C` 无条件调用 `0x6C74E0(veh+0x650, veh, veh+0xE40)`（确认）→ **所有载具都会登记锁定点**。
- V506_HELI.SGO 的 `animation_model[2]`（MAB 块）按 `0x6C74E0` 的解析方式（`mab+8+*(i32*)(mab+0x14)`，计数 u16 `+2`，表 `+4`，步长 0x20）读出 **1 项：`body`**（推断：`*(veh+0xE40+0x280)` 指向 MAB 块没逐条追到写入点；同法读 E504_MONSTERA 得 `neck0`，符合“锁定点”语义）。
- 在载具/对象代码里没找到把锁定点 `+0x29/+0x2A` 清零的写入（全镜像搜 `mov byte [x+0x29/0x2a]`，命中都在武器代码 `0x689xxx..0x6Axxx` 的锁定状态结构，推断与此无关）。
- 武器锁定过滤 `0x689E40`：`IsEnemy 0x5E1540(target+0x314, owner+0x314)` 即 `rel[target][owner] == 2`（确认）。玩家 team 0 对 team 1：`rel[1][0] = 2` → 可锁。

**结论**（推断，强）：team 1 的 506 有一个 `body` 锁定点，常驻有效、可锁，玩家的锁定导弹能锁它；
插件自己的 `ForEachEnemyOf`（读同一 registry）对 team 1 载具会把玩家 / 友军的锁定点当目标。

## Q5. 写死“友方”或假设站在玩家一边的地方

| 位置 | 行为 | 对 team 1 载具的影响 | 可信度 |
|---|---|---|---|
| RideAi `0x6331CC` | 乘员写死 team 2 | 载具一帧后回到 team 2（§0） | 确认 |
| 载具更新 `0x6304C7..0x630591` | 队伍 = 乘员队伍，空载具 = 5，每帧 reg=true | 乘员被踢 / 被打死 / 被 SeatKick → 载具变 5（无主），玩家可上车，插件 crew.cpp 的 autoCrew 可能再给它 RideAi → 变友军 | 确认（机制）/ 推断（插件联动） |
| `+0x1A & 8` 跳过重算（`0x6304AB`） | 该位在大量 AI 代码里被测试 | 推断是联机远端副本标志，单机不影响 | 推断 |
| DummyVehicleRider slot 0 `0x5E3B70` | 快照恢复时乘员回 team 2 | 若触发，载具随之回 2 | 确认（代码）/ 推断（触发时机） |
| 武器重建 `0x633330` / `0x632E90` | `weapon+0x8B6 = 1`（开友伤，关敌我过滤） | RideAi 之后若再建武器：爆炸伤害不分敌我（team 1 同伴也会被炸），直击友方按难度系数打折而非免伤 | 确认 |
| CanRide / CanRideSeat（re-notes） | 双方队伍不同且载具队伍 ≠ 5 时要求乘坐掩码第 7 位 | 玩家上不了 team 1 载具（正常） | 确认（re-notes） |
| 雷达 `ui::HUiHudRader` slot 2 `0x82B4D0` | 敌方点 = `EnumConflictObject(player+0x314)`（`0x82B89A..0x82B8CA`），另取 team 5、4、2 的集合（`0x82B907/0x82B94A/0x82BB50/0x82BBED`） | team 1 载具出现在“敌对”那一组（显示颜色未查，推断为敌色） | 确认（数据来源）/ 推断（颜色） |
| 受伤通知 `0x547C30 → 0x54E3F0(obj, obj+0x314, …)`、`0x54BE40 → 0x54E3F0` | 按对象自身队伍通知 | 按 team 1 走，没看到写死友方 | 推断 |
| 载具死亡 slot 48 `0x62EA10` | 该函数里没有读 `+0x314/+0x318` | 没发现“友军载具被毁”惩罚 / 语音的队伍判断 | 推断（只做了队伍字段扫描） |
| 插件自身 `hud.cpp HudSee` | 只收玩家队 / 2 / 5 | team 1 载具不显示插件血条（插件侧，符合预期） | 确认（源码） |
| 插件 `heli.cpp ForEachEnemyOf` | team 5 当玩家队处理 | team 1 时目标 = 0、2、4 方 | 确认（源码） |

另：脚本还有 `SetTeamDamageAdjust(TeamID, float)` / `_Squad`（声明串 `0x1798838` / `0x1798870`），是按攻击方队伍调整受伤倍率的对象属性，本次没追它的存储与默认值（未查）。

## 插件侧建议（推断，未实测）

1. `CreateObject → SetTeam(v,1,true) → RideAi(v,true)` 之后，遍历座位（`veh+0x608`，数 `+0x618`，步长 0x340），乘员 `+0x260` 的 vtable 是 `img+0x17D7320` 时 `SetTeam(rider, 1, false)`；再 `SetTeam(v, 1, true)`。
2. 之后不要再建武器；若要换武器，换完把每个 holder 武器的 `+0x8B6` 清 0（与 RideAi `0x6330C9` 同法）。
3. 每帧（或低频）核对乘员与载具 `+0x314`：乘员被改回 2（快照）或载具变 5（乘员没了）时按需处理，并在 crew.cpp 的 autoCrew / bump 路径里排除这些敌方载具。
4. 是否要 `+0x380 |= 0x2000` 让它不计入 `GetTeamObjectCount(TEAM_ID_ENEMY)`，取决于设计：不置位则任务会等它被击毁。
