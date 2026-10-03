# 空爆 / 支援要请 逆向笔记（EDF6, EDF.dll TimeDateStamp 0x678CCB46）

所有地址为 RVA（ImageBase 0x180000000）。置信度：H = 反汇编 + 数据交叉印证；M = 反汇编推断、未运行验证；L = 推测。
方法：静态反汇编（capstone，`tools/edfre.py`）+ SGO 解包（`testrange` 读 CPK）。**全部未在游戏内运行验证。**

## 1. 支援武器分类（SGO 数据，H）

| 类别 | 武器 SGO 类 | 抛出/标记弹 | 实际投送 | 有无真实飞机对象 |
|---|---|---|---|---|
| 轰炸机（KM6 / Vesta / eRequestBomber） | `Weapon_RadioContact` vt 0x17E4FB0 | 无（瞄准态直接给目标点） | `BombingPlane` 对象（BOMBER401/501/501_2.sgo）直线飞过、沿途投弹 | **有**（GameObjectBase 派生，durability 1000） |
| 炮击 / 迫击（eWeapon001–015） | `Weapon_Throw` | `SmokeCandleBullet01` vt 0x17A5830 | 烟幕弹落地后由 IndirectFireControl 从高 800 发 RocketBullet01 | 无 |
| 空爆机 Whale（eWeapon016–050） | `Weapon_BasicShoot` | `SmokeCandleBullet02` vt 0x17A58F0 | 烟幕弹从高 400 发弹 | 无（只有无线电语音 + 天降弹，M） |
| 导弹（eWeapon083–098） | `Weapon_LaserMarkerCallFire` vt 0x17E4750 | 激光指示 | MissileBullet01（bullet_icbm01.rab） | 无 |
| 卫星（eWeapon099–118） | `Weapon_LaserMarkerCallFire` | 激光指示 | LaserBullet02 | 无 |
| 载具投送（aWeapon3xx） | `Weapon_Sub` | `SmokeCandleBullet01` | Transporter508 运输机 + 集装箱（CreateObject） | 有（运输机） |
| 无人机 | `Weapon_Drone_*` | — | SoldierDrone_Basic | 另一体系，本文不覆盖 |

KM6 的 `Ammo_CustomParameter`：`[30, 1, [planeSgo, 架数, 间隔帧, 100, 速度, ?, 提前/后撤距离, 0, 0, [20], [shotSpec]], [语音]]`。
ShotSpec 格式：`[[..],[..], 发数, 间隔, AmmoClass, 弹速, …, alive, …, [se], [hitse]]`。由共用发射单元解析（见 §3）。

## 2. 呼叫链（轰炸机，H/M）

- `0x6A9680` RadioContact 瞄准态 (weapon, phase)。目标点在 `weapon+0x1640`，航向在 `+0x1650`，owner 在 `+0x120`。(M)
- `0x6A8B50` RadioContact 呼叫态 (weapon, phase)：
  - phase0 在栈上构造 Params P：
    - `+0` owner weak 指针
    - `+0x10` = `weapon+0x8B0`，`+0x14` = `weapon+0x89C`（推测是伤害 / 爆炸系数，M）
    - `+0x18` 随机种子
    - `+0x20..0x5F` 4×4 矩阵（航向旋转 + 目标点，w=1）
    - `+0x60` CP[2] variant
  - 然后调 **`IFC_Start 0x2B5DA0(weapon+0x1660, &P)`**，并置 `weapon+0x140 = 1`。(H)
  - phase1 调 `IFC_Update 0x2B87A0(ifc, &out{u8 spawned; i32 idx; weak plane})`。每出现一架新飞机，就调 `0x5AB460(plane, owner, voiceA, voiceB)`；列表空了回到 idle 态 `0x6A9F90`。(H)
- `IFC_Start 0x2B5DA0`：把 P 拷进 IFC，设置 `+0x80`/`+0x84` = 架数、`+0x88` = 计时器、`+0x8C` = 激活标志。调用者：0x2A9900（SmokeCandle）、0x6A7BD0、0x6A8B50。(H)
- `IFC_Update 0x2B87A0`：按 elem[2] 计时，用 `0x2B4DC0` 算每架飞机的矩阵（位置后撤 elem[6]），然后依次调用：
  1. **`CreateObject 0x11945E0(*(img+0x20B2958), &M, elem[0] wchar 路径, &InitParamBase{vt 0x1762068})`**
  2. 对结果做 dynamic_cast，再 `0x118ADF0(owner, plane)`
  3. **`BombingPlane_Init 0x5AABB0(plane, &targetM, &ownerWeak, xmm3=f14, [rsp+20]=f10, [28]=speed, [30]=elem5, [38]=elem6, [40]=&shotSpec, [48]=seed)`**
  4. `SetTeam 0x54EE70(plane, owner+0x318, 1)`，再置 `plane+0xD16 = 1`、`plane+0x380 |= 0x80`
  5. 把飞机 weak 指针挂进 `ifc+0x90` 链表。(H)
- `BombingPlane`（vt 0x17D3A30，45 槽，只覆写 0/1/3/5）：
  - ctor 0x5AA670；发射单元在 `+0xC20`；状态机在 `+0xB40`。
  - Init 0x5AABB0：`+0xB70` 目标点，`+0xB80` 方向×速度，`+0xB90` 速度；调 `0x2B5F40(+0xC20, shotSpec, seed)`、`0x2B8390`（设 owner）、`0x2B82E0(f14)`、`0x2B8460(f10)`。
  - Update（slot5）0x5AB240：`pos(+0x90) += dir*speed`，瞄准点 `+0xC40` = pos + forward*lead（y 取目标点 y），然后 **`0x2B95A0(+0xC20, dt)` 投弹**。
  - 飞走态 0x5AB9A0（由 0x5AB550 在第 0x1E 帧置 `+0x10D0 = 1` 后切入）。(H)

## 3. 共用"发弹单元"（IndirectFireUnit，H）

- 解析：`0x2B5F40(unit, shotSpecVariant, seed)`
- 每帧发射：`0x2B95A0(unit, dt)`，内部经 `0x2B43A0`（弹道计算）和 `0x2B3D90`（逐发生成，M）
- 设置项：`0x2B8390(unit, ownerWeak)`、`0x2B82E0(unit, f)`、`0x2B8460(unit, f)`，总发数 `0x2B8470`
- 0x2B95A0 的使用者：NapalmBullet01 0x2641F0、MissileBullet02 0x26E4C0、ClusterBullet01、SmokeCandleBullet01 0x2A82A0、BombingPlane 0x5AB240、DemoIndirectFire 0x5B5C50、LaserMarkerCallFire（0x6A407C 处）
- 结论：所有"从天而降"的伤害弹都走同一个发弹单元。改一处就能统一"由我们的飞机发弹"。(H)

## 4. 运行时生成对象（H 机制 / M 用法）

- `SceneObject* CreateObject 0x11945E0(void* g=*(img+0x20B2958), const Matrix4* xf, const wchar_t* sgo, InitParamBase* ip)`：
  - 共 88 个调用点。按 SGO 的 `xgs_scene_object_class` 找 Factory，调 `factory->vfunc[2]`，并注册到 `g+0x3B0`。
  - **SGO 必须已预载**，否则向地址 1 写入导致崩溃。(H)
- 预载可借现有 `Preload 0x59DE50` hook（`src/loadout.cpp` 已有）追加 SGO 路径。(M)
- 脚本原生函数（注册函数 0x1E31F0）：
  - `CreateFriend` 0x1B0310
  - `CreateVehicle` → 0x1E2BB0 → `CreateVehicle2` 0x1B28C0(sgo, point, float)：解析路点 0x6F83B0 → `0x1D8900(ctx, &out, &params)` → 0x1BEA30，再 dynamic_cast 到 0x2006480（VehicleBase，M）
  - `Vehicle_Setup` 0x1CC540、`Vehicle_RideAi` 0x1CC170、`Vehicle_SetAiAttack` 0x1CC2A0
  - 这些依赖 MissionContext 和路点名，**插件不宜直接调用**；推荐直接调 CreateObject。(M)
- `0x1D8900`（MissionContext 生成）：先生成对象 0x1D9C20，cast 到 SceneObject；失败时发 msg 0x10000008；成功时调 `SetTeam(obj, params+0x88, 1)`，可选 `0x54E740(obj, hp倍率 = ctx.table[team]*params+0x80)` 与 `0x548D50`，最后 `0x1DB1E0` 登记到任务对象表。(H/M)
- `SetTeam 0x54EE70(obj, int team, bool reg)`：写 `+0x314` / `+0x318`，`reg` 为真时登记到全局阵营管理器 `*(img+0x20B2978)`（`0x5E1C60` 为反注册）。敌 AI 选目标大概率依赖这张表。(H 写字段 / M 索敌)

## 5. 能否让飞机"可被击落、有 HP"

- GameObjectBase ctor 0x545670 在 `+0x2F4`（最大 HP）和 `+0x2F8`（当前 HP）写入 durability。(H)
- BombingPlane 有 HP 1000，**但其 SGO 没有 `.cas` 碰撞体**（只有 `animation_model`）。推断它不可被射中，伤害只能由代码直接扣减 `+0x2F8`。(M)
- 直升机 V506_HELI 有 `v506_heli.cas`、`heli_rigid_body`、durability 600，以及完整的伤害 / 爆炸 / 残骸表，可被击中。(H 数据)
- 方案 A（推荐）：用直升机类（Vehicle506_Helicopter vt 0x17DB238 等）+ 自制 SGO，`animation_model` 换成 bomber501.mrab，但沿用 heli 的 `.cas`（碰撞形状与外观不匹配，命中盒偏小）。风险：
  - heli 动画骨骼名（rotor/tailRotor/body）缺失时，ragdoll / dead_effect 可能空引用崩溃（L，需在游戏内验证）。
  - 自制 SGO 需要能被 CPK 外加载（需验证 mod 加载路径）。
- 方案 B：仍生成 BombingPlane，由插件自己做命中判定（射线 / 球体），命中后扣 `+0x2F8`，归零时切到飞走 / 爆炸态。改动最小，但"可被射中"是模拟出来的。(M)

## 6. 友军血条 / 标记

- `HudPlayer_FollowerDurability`（vt 0x17F6C08）的绘制函数 **0x804300**：
  - 递归遍历 `obj+0x550` 链表（跟随者树），把 `node->obj+0x90` 投影到屏幕。
  - 按 `obj+0x2F8 / obj+0x2F4` 绘制头顶血条，经 `0xC2FB0`（画布 `*(img+0x2139A78)`）。(H 读字段 / M 语义)
- 载具和 BombingPlane 不在这条链上，**没有原生头顶血条**。雷达类为 `HUiHudRader`（未深挖）。
- 建议：hook 0x804300（签名唯一）在其后追加遍历我们自己的飞机列表，复用 0xC2FB0 绘制；或在插件 overlay 中自绘。不要把飞机塞进 `+0x550`，那条链也驱动小队 AI（L）。

## 7. 推荐 hook 设计

1. **呼叫时刻**：hook `IFC_Start 0x2B5DA0(ifc, P)`。
   - 这是唯一一个同时覆盖轰炸机、炮击、Whale、载具投送的入口；LaserMarkerCallFire 走 0x6A1090 / 0x6A407C 路线，需另行确认。
   - 从 P 读 owner（`+0/+8`）、目标矩阵（`+0x50` 处为目标点）、variant（`+0x60`）。
   - 抑制原版：调用原函数后清 `ifc+0x8C`、将 `ifc+0x84` 置 0（IFC_Update 不再生成），或直接不调原函数（要确认调用方不依赖 IFC 状态，M）。
2. **我方飞机生成**：先通过 Preload hook 预载自制 SGO，再调 `CreateObject 0x11945E0` → `SetTeam(obj, owner+0x318, 1)` → 写入航线（位置 `+0x90` / 矩阵 `+0x60`）。
3. **发弹**：让飞机持有一个 IndirectFireUnit。
   - 照 BombingPlane 做法：`0x2B5F40(unit, shotSpec, seed)` + `0x2B8390(unit, ownerWeak)`，每帧调 `0x2B95A0(unit, dt)`。
   - 弹药 / 燃油计数在插件侧维护，耗尽后停止调用并改飞离航向。
   - 最省事的做法：直接生成 BombingPlane 并 hook 其 Update 0x5AB240 来控制航线 / 弹量（方案 B）。
4. **击落**：命中靠 .cas（方案 A）或插件判定（方案 B）；HP 在 `+0x2F4`/`+0x2F8`，死亡标志在 `+0x2E8`。

## 8. 签名（RVA + 前 16 字节，kHeliSignatures 风格；按固定 RVA 校验，不要求全局唯一）

```
{0x6A8B50,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x57,0x41,0x54,0x41},16}, // RadioContact 呼叫态
{0x6A9680,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x57,0x41,0x56,0x48},16}, // RadioContact 瞄准态
{0x2B5DA0,{0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05},16}, // IFC_Start
{0x2B87A0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56},16}, // IFC_Update
{0x5AABB0,{0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48},16}, // BombingPlane_Init
{0x5AB240,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},16}, // BombingPlane_Update
{0x2B5F40,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56},16}, // FireUnit_ParseShotSpec
{0x2B95A0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55},16}, // FireUnit_Update
{0x2B8390,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x48},16}, // FireUnit_SetOwner
{0x11945E0,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9},16}, // CreateObject
{0x54EE70,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41},16}, // SetTeam
{0x1B28C0,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24},16}, // Script CreateVehicle2
{0x1B0310,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24},16}, // Script CreateFriend
{0x1D8900,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x55},16}, // MissionContext 生成
{0x804300,{0x4C,0x8B,0xDC,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x49,0x8D,0xAB},16}, // 跟随者血条绘制
```

## 9. 未决 / 需游戏内验证

- `weapon+0x89C` / `+0x8B0` 的语义（伤害？爆炸范围？）；`0x2B3D90` 逐发参数。
- 直升机类配 jet 模型时，骨骼缺失是否崩溃；mod SGO 的加载路径。
- 抑制 IFC 后，SmokeCandle 弹体 / RadioContact 状态机能否正常结束（列表空 + `+0x16E4 == 0` 才回 idle）。
- LaserMarkerCallFire（导弹 / 卫星）不走 IFC_Start；它的起点是 0x6A1090 / 0x6A407C，需要单独 hook。
- 网络联机：RadioContact slot30 0x6A8610 负责网络接收，生成的对象需要同步，否则只有主机可见（L）。
