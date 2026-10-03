# 任务脚本空袭 / 轰炸机支援：拦截与「真战机」替换逆向笔记（EDF6, EDF.dll TimeDateStamp 0x678CCB46）

所有地址为 RVA（ImageBase 0x180000000）。置信度：H = 反汇编 + 数据交叉印证；M = 反汇编推断、未运行验证；L = 推测。
方法：静态反汇编（`tools/edfre.py`）+ SGO / MISSION.AC 解包（`testrange`）。**全部未在游戏内运行验证。** 与 `airstrike-re.md`（玩家支援武器）互补，重复处只给引用。

目标：把任务脚本空袭与空军轰炸机的「演出飞机」换成插件自己驾驶的友军喷气机（heli 类载具）。

## 1. 任务空袭对象

### 1.1 类与 SGO（H）

| SGO 名前缀 | C++ 类 | vtable | 工厂 vtable / create | ctor |
|---|---|---|---|---|
| `DEMOAIRSTRIKE*` | `DemoAirStrike`（只派生 `SceneObject`，**不是** GameObjectBase） | 0x17D46E0（12 槽） | `Factory@DemoAirStrike` 0x17D46A0，slot2 @0x17D46B0 = create 0x5B3F00（new 0x180） | 0x5B40C0（唯一调用点 0x5B3F23） |
| `DEMOGUNSHIPFIRE*` / `DEMOINDIRECTFIRE*` / `DEMOMISSILE_*` / `DEMOSATELLITELASER*` | `DemoIndirectFire` | 0x17D4B20 | — | 0x5B55F0（调用点 0x5B5173） |

- `DemoAirStrike` slot5 Update 0x5B46D0：飞机 weak（`+0x168/+0x170`）过期 → `jmp 0x118A1B0(self)` 自删。(H)
- `DemoAirStrike` slot9 0x5B45E0：消息 0x10000008（level）→ 重算伤害 → `0x5AB450(plane, dmg)`；飞机为空时跳过。(H)
- `DemoIndirectFire`：发射单元在 `+0x170`（IndirectFireControl，同 `airstrike-re.md` §3：0x2B5F40 / 0x2B8390 / 0x2B82E0 / 0x2B8460）；slot5 Update 0x5B5C50 = `0x2B95A0(+0x170, dt)` 后 `0x2B7B90` 判完成。**不生成飞机对象**，只是天降弹。(H)

### 1.2 DemoAirStrike ctor 0x5B40C0（H）

- 参数读取：`0x528A0(obj+0xA0, L"name")` 返回节点，float 在 `node+8`，字符串在 `node + *(int*)(node+8)`。
- 读取的键：`bombing_plane_object`、`speed`、`target_adjust`、`target_distance`、`height`、`spread`、`damage`、`bombing_plane_param`。
- 伤害 = `0xD7AE0(GameStatus *(img+0x20B2890), 1.0) * damage / 10 * 0.8`。
- 种子 = `int(pos.x*pos.y*pos.z)`。
- 飞机矩阵：取对象自身行向量，位置 = `obj+0x90 − row2(obj+0x80)*1000`，`y += height`。
  - 即：**目标 = 生成点 `obj+0x90`**，**航向 = 生成点 row2 `obj+0x80`**，从 1000 m 外进场。
- `CreateObject(*(img+0x20B2958), &M, planeSgo, &InitParamBase{vt 0x1762068})`（调用点 0x5B436B）后 dynamic_cast；失败则 log 并 `0x118A1B0(self)`。
- 0x5B4423 调用 `BombingPlane_Init 0x5AABB0`，参数为：
  - `plane`、`&obj+0x90`（目标）、`&weakSelf`；
  - `xmm3` = 伤害，`[20]` = spread，`[28]` = speed，`[30]` = target_adjust，`[38]` = target_distance；
  - `[40]` = `&bombing_plane_param`，`[48]` = 种子。
- 之后**硬编码 `SetTeam(plane, 2, 1)`**，飞机 weak 存到 `obj+0x168/+0x170`。

### 1.3 SGO 参数（H）

| SGO | 机 | 高 | 速 | spread | dmg | dist | 弹 |
|---|---|---|---|---|---|---|---|
| DemoAirStrikeE15_50 | Bomber401 | 150 | 3 | 50 | 500 | 0 | 50×int1 RocketBullet01 |
| DemoAirStrikeE25_10 | Bomber401 | 150 | 3 | 10 | 500 | 100 | 10×10 |
| DemoAirStrikeE50_10 | Bomber401 | 250 | 3 | 10 | 1000 | 100 | 10×20 |
| DemoAirStrikeE50_10_LOW | Bomber401 | 100 | 3 | 10 | 1000 | 100 | 10×20 |
| DemoAirStrikeSolid_100 | Bomber501 | 150 | 3 | 10 | 200 | 100 | 100×2 SolidBullet01 |

- 五个 SGO 的 `target_adjust` 都是 0.5。BOMBER401/501/501_2 = `BombingPlane`，durability 1000。
- GunshipFire：`indirect_fire_param [[1.2,0],[800,0],1,0,…]`，damage 200–2000。

### 1.4 脚本如何创建（H）

| 原生函数 | RVA | 行为 |
|---|---|---|
| `CreateFriend(point, sgo, level, bool)` | 0x1B0310 | 0x6F83B0 解析点 → 0x1D8900 |
| `CreateNeutral` | 0x1B1B80 | 同上，team 不同 |
| `AirStrike(Object,…)` | 0x1AC9E0 | — |
| `AirStrike2(Object target, radius, sgo, yaw)` | 0x1AC3E0 | 位置 = 目标 + 随机偏移（radius），level 1.0、team 3，调用 0x1D8900，返回 void |
| `PointAirStrike(point, radius, sgo, level, count, interval)` | 0x1B82E0 | 按脚本线程 tick（vfunc 0x1A8）对 `int(interval*60)` 取模生成，经 vfunc 0x1A0 yield，脚本阻塞到生成完 count 个（M） |

- 0x1D8900 先 dynamic_cast 到 `GameObjectBase`（0x2006400）。
  - Demo* 全部 cast 失败：只发 0x10000008（level = `ctx[0x280+team*4]*level`，vfunc +0x50/+0x48/+0x58），返回空指针，**不进任务对象登记**。(H)
  - 因此脚本拿到的是 null Object，**从不持有、不等待空袭对象**，返回值被忽略。拦截不会卡死脚本流程。(H)

### 1.5 正式任务的使用点（H，DLC 列表无使用）

| 任务 | 调用 |
|---|---|
| EDF6/RM034A（列表 #63）、RM034B（#97） | `PointAirStrike("空爆01–03",300,DemoIndirectFireE50,1,10,0.6)`；`CreateFriend("機銃掃射01–04",DemoAirStrikeE50_10,1,false)`，间隔 wait 1 / 2 / 0.5 s |
| EDF6/M092_5 | `AirStrike2` DemoGunshipFireSolid / E50，角度 −1.75 |
| EDF6/M116 | `AirStrike2` DemoIndirectFireSolidD_EVENT；`CreateFriend("空爆目標1_01"/"1_02", DemoAirStrikeE50_10_low, 1, true)` |
| EDF6/M118 | `AirStrike_Strafing`：`CreateNeutral(target, DemoAirStrikeSolid_100, level)` ×3，有间隔 |
| EDF6/M083_8、M092 | `AirStrike2` DemoSatelliteLaser_vsRadon / DemoMissile_vsRadon |
| EDF6/M123 | `PointAirStrike` DemoMissile_vsRingMissile |

- 结论：会出现**真飞机**的只有 `DemoAirStrike*`（RM034A/B、M116、M118）。(H)
- Gunship / IndirectFire / Missile / Satellite 只是天降弹，替换成战机属于新增演出，不是「换模型」。(H)
- EDF5_NEW_SCRIPT 与 test 目录不在任务列表中。(H)

## 2. 拦截方案

### 2.1 DemoAirStrike：推荐 patch 工厂 vtable slot2（M）

- 位置：`0x17D46B0`，期望值 0x5B3F00。也可 RedirectCall 调用点 0x5B3F23（ctor）。
- Hook 步骤：
  1. 先调原函数，让 ctor 完整跑完（飞机已生成、伤害已算好）。
  2. 读 `obj+0x90`（目标）、`obj+0x80`（航向 row2），需要时用 `0x528A0(obj+0xA0, L"speed")` 等读 SGO 参数。
  3. 调 `0x118A1B0(plane)` 删除 `obj+0x168` 处的飞机。
  4. 在同一帧自己生成喷气机（见 §3），飞向目标。
- 之后的链路：
  - `DemoAirStrike` 下一帧 Update 发现 weak 过期 → 自删。(H 代码路径 / M 时序)
  - slot9 的 level 消息遇到空飞机安全跳过。(H)
- 备选：RedirectCall 0x5B436B 的 CreateObject 让它返回 null，走原生失败路径（ctor 内自删；`CreateObject` 本身也会在 deleted 时返回 NULL）。
  - 但这样拿不到构造完成的参数，且依赖 dynamic_cast(null) 分支，风险高于主方案。(M)

### 2.2 DemoIndirectFire（若也要替换）：patch vtable slot5（M）

- 位置：`0x17D4B48`，期望值 0x5B5C50。
- 对要抑制的对象调 `0x118A1B0(self)` 后 return（原生 DemoAirStrike 也在 Update 里自删）；目标 = `self+0x90`。
- 首帧之前 ctor 已初始化 IFC，但第一发弹在 Update 内才出，首帧即删就不会出弹。(M)

### 2.3 轰炸机支援 Weapon_RadioContact（H 路径 / M 效果）

- IFC_Start 0x2B5DA0(ifc, P) 拷贝的字段：
  - `P+0/+8` owner weak；
  - `P+0x10` = `weapon+0x8B0`（AmmoExplosion → spread 槽 / 0x2B8460）；
  - `P+0x14` = `weapon+0x89C`（AmmoDamage，星级缩放 → `xmm3` 伤害）；
  - `P+0x18` 种子；
  - `P+0x20..0x5F` 矩阵（位置在 `P+0x50`）；
  - `P+0x60` CP[2] variant（类型 tag 为 `+0x70` 处的 u16）。
  - 然后置 `ifc+0x80 = ifc+0x84 = CP[2][1]`（架数）、`+0x88 = 0`、`+0x8C = 0`。
- 唯一 RadioContact 调用点 0x6A8DFB（`ifc = weapon+0x1660`），返回后调用方设置：
  - `+0x16EC = 1`（`ifc+0x8C` active）、`+0x16E4 = +0x16E0`（`ifc+0x84 = ifc+0x80`）、`+0x16E8 = 0`；
  - `weapon+0x140 = 1`。
- IFC_Update 0x2B87A0（调用方 0x2A9BE8 / 0x6A8EC8）：
  - 先清理死飞机链表 `ifc+0x90`（size `+0x98`）；
  - 只有在 `+0x8C != 0 && +0x84 > 0` 时生成。生成路径：`CreateObject`（0x2B8DDB）→ `BombingPlane_Init 0x5AABB0`（0x2B924E）→ `SetTeam(plane, owner+0x318, 1)`（0x2B932E，队伍取 owner 的）。
- 回到 idle：0x6A915C 在 `weapon+0x16F8 == 0 && +0x16E4 == 0` 时调用 `0x6A8AB0(weapon+0x1610, 0x6A9F90 idle, 0)`。
- **抑制配方**：只把 0x6A8DFB 这一处 call 重定向到 wrapper。wrapper 先调原 0x2B5DA0，再写 `ifc+0x80 = ifc+0x84 = 0`。
  - 调用方随后把 0 拷进 `+0x84`，于是 IFC_Update 不生成飞机，武器回 idle。(M)
  - 这样只碰轰炸机，不影响 IFC_Start 的其他两个调用者 0x2A9B62（SmokeCandle）与 0x6A8137（fn 0x6A7BD0）。(H)
- wrapper 里可直接读到的数据：

  | 数据 | 位置 |
  |---|---|
  | 目标 | `P+0x50` 或 `weapon+0x1640`（vec4） |
  | 航向 yaw | `weapon+0x1650` |
  | owner | `weapon+0x120` |
  | 架数 | `ifc+0x80`（清零前读） |
  | 伤害 / 爆炸 | `weapon+0x89C` / `weapon+0x8B0`（由 WeaponBase SGO 加载器 0x68A920 写入） |
  | CP variant | `+0x8E8` |

  - variant 访问器跳表：0x179EAF0（数组）、0x179EA90（int）、0x179EAA8（double），tag 为 `+0x10` 处的 u16。(H)
  - 弹药数在 `weapon+0x248`。(H)
- 弹药扣除发生在呼叫前，抑制不退弹药。(M)

### 2.4 轰炸机武器表（H，SGO 数据）

36 把 `Weapon_RadioContact`，全部属于 `Weapon_Engineer_Call_Attacker`。CP 格式：
`[CP0 30|120, 1, [机SGO, 架数, 间隔帧, 高, 速, target_adjust, target_distance, e7, e8, [e9], shotSpec], [语音]]`

| id | 名称 | 机 | 架 | 间隔帧 | 高 | 速 | 发×间隔 | 弹 | 伤害 | 爆炸 |
|---|---|---|---|---|---|---|---|---|---|---|
| 973 | KM6 | Bomber501_2 | 2 | 30 | 100 | 3 | 70×1 | Solid | 90 | 8 |
| 974 | 偵察爆撃機 | Bomber401 | 1 | 15 | 150 | 3 | 20×5 | Rocket | 420 | 15 |
| 975 | カムイ | Bomber501 | 1 | 15 | 150 | 7.5 | 10×2 | Rocket | 590 | 15 |
| 976 | ウェスタ | Bomber401 | 1 | 15 | 150 | 3 | 10×3 | Napalm | 1000 | 15 |
| 977 | KM6 プラン３ | Bomber501_2 | 3 | 40 | 150 | 3 | 100×1 | Solid | 160 | 8 |
| 978 | フォボス | Bomber401 | 1 | 15 | 150 | 3 | 40×5 | Rocket | 1000 | 15 |
| 979 | KM6 Ｘ２ | Bomber501_2 | 2 | 30 | 100 | 3 | 100×1 | Solid | 275 | 8 |
| 980 | フォボス ２ | Bomber401 | 2 | 60 | 150 | 3 | 20×5 | Rocket | 900 | 8 |
| 981 | ウェスタＢ | Bomber401 | 1 | 15 | 150 | 3 | 20×2 | Napalm | 1650 | 15 |
| 982 | KM6 Ｗ２ | Bomber501_2 | 2 | 1 | 150 | 3 | 150×1 | Solid | 350 | 8 |
| 983 | カムイＢ | Bomber501 | 1 | 15 | 150 | 7.5 | 10×2 | Rocket | 750 | 10 |
| 984 | フォボス ３ | Bomber401 | 3 | 15 | 150 | 3 | 40×5 | Rocket | 650 | 15 |
| 985 | KM6 Ｘ４ | Bomber501_2 | 4 | 30 | 150 | 3 | 100×1 | Solid | 576 | 8 |
| 986 | カムイＢ２Ｍ | Bomber501 | 1 | 15 | 150 | 7.5 | 10×1 | Rocket | 1080 | 15 |
| 987 | KM6Ｆ１ | Bomber501_2 | 1 | 30 | 150 | 6 | 100×0 | Solid | 685 | 8 |
| 988 | フォボス ４ | Bomber401 | 4 | 15 | 150 | 3 | 30×5 | Rocket | 1375 | 15 |
| 989 | ウェスタＢ Ｘ２ | Bomber401 | 2 | 15 | 150 | 3 | 15×3 | Napalm | 3000 | 15 |
| 990 | KM6 Ｘ９ | Bomber501_2 | 9 | 15 | 150 | 6 | 50×2 | Solid | 860 | 8 |
| 991 | KM6Ｆ２ | Bomber501_2 | 1 | 30 | 150 | 6 | 150×0 | Solid | 900 | 8 |
| 992 | カムイＣ１ | Bomber501 | 1 | 15 | 150 | 7.5 | 11×2 | Rocket | 2250 | 10 |
| 993 | フォボス １０ | Bomber401 | 10 | 180 | 150 | 3 | 16×8 | Rocket | 1750 | 15 |
| 994 | フォボスＢＸ | Bomber401 | 1 | 15 | 150 | 3 | 40×5 | Rocket | 3000 | 15 |
| 995 | KM6 Ｗ３ | Bomber501_2 | 3 | 7 | 150 | 3 | 120×1 | Solid | 1200 | 8 |
| 996 | フォボス クラスター | Bomber401 | 1 | 60 | 250 | 2 | 200×1 | Rocket | 2000 | 50 |
| 997 | KM6Ｆ２ Ｗ２ | Bomber501_2 | 2 | 1 | 150 | 6 | 150×0 | Solid | 1650 | 8 |
| 998 | ウェスタＣ Ｗ２ | Bomber401 | 2 | 30 | 150 | 3 | 16×3 | Napalm | 6000 | 15 |
| 999 | フォボスＺ | Bomber401 | 1 | 20 | 250 | 3 | 20×20 | Rocket | 3600 | 15 |
| 1000 | KM6 Ｘ５ | Bomber501 | 5 | 30 | 150 | 3 | 100×1 | Solid | 2000 | 8 |
| 1001 | カムイＣＸ | Bomber501 | 1 | 15 | 150 | 7.5 | 12×2 | Rocket | 5250 | 10 |
| 1002 | KM6ＦＸ | Bomber501_2 | 1 | 30 | 150 | 6 | 200×0 | Solid | 2800 | 8 |
| 1003 | ウェスタＤＡ | Bomber401 | 1 | 15 | 150 | 3 | 40×2 | Napalm | 10000 | 15 |
| 1004 | KM6 Ｚ４ | Bomber501_2 | 4 | 15 | 150 | 3 | 150×1 | Solid | 2800 | 8 |
| 1005 | フォボスＺ ４ | Bomber401 | 4 | 30 | 150 | 6 | 20×3 | Rocket | 5500 | 15 |
| 1432 | MPACK_A フォボス Ｊクラスター | Bomber401 | 1 | 60 | 250 | 2 | 200×1 | Rocket | 4000 | 50 |
| 1534 | MPACK_B KM6 Ｘ１８ | Bomber501_2 | 18 | 15 | 150 | 6 | 50×1 | Solid | 3600 | 4 |
| 1535 | MPACK_B ウェスタＥ Ｘ２ | Bomber401 | 2 | 30 | 150 | 4.5 | 20×2 | Napalm | 4000 | 15 |

- 「伤害」列是 SGO 原值，星级缩放后写入 `+0x89C`。
- Call_Gunship / Call_Cannon / Missile / Satellite 不是 RadioContact（分别是 BasicShoot / Throw / LaserMarkerCallFire），本配方不适用。(H)

## 3. 运行时生成喷气机

### 3a. 预载（H 函数 / M 用法）

- **0x59DE50 不是通用预载**，而是 `PreloadPlayerResource` 内的玩家资源预载（loadout 钩的就是它）。它内部调用通用预载：
  `0x7A3780(*(img+0x20B29A8), const wchar_t* path, 2, -1)`（调用点 0x59DF21）。(H)
- 脚本 `Preload(const string&in, int)` 原生 = 0x1B87E0，直接尾调 `0x7A3780(*(img+0x20B29A8), path, 2, int)`。(H)
- 0x7A3780 内部先对 `mgr+0x98` 上锁（0x50BC0），路径上限 0x400 字符。(H)
- 预载是异步的：脚本随后 `WaitPreload()`。未预载就 `CreateObject` 会写 `[1]` 崩溃。(H)
- 推荐做法：在已有的 0x59DE50 wrapper（调用点 0x1B8F52 / 0x225FB5 = 脚本 `PreloadPlayerResource`）里额外调一次
  `0x7A3780(mgr, L"app:/object/<JET>.sgo", 2, -1)`。
  - 所有正式任务都是 `PreloadPlayerResource(); WaitPreload();` 的顺序，所以资源在任务开始前就绪。(M)
  - **不要**在任务中途临时预载后立即生成。(M)
- Mods/OBJECT 覆盖：testrange 把派生 SGO 放进 `Mods/OBJECT/<NAME>.SGO`，经 `app:/object/<NAME>.sgo` 能正常预载和生成（EDFModLoader 重定向），新文件名同样可用。(M：实测路径来自 testrange，不是本次验证)

### 3b. 载具生成（H 链路 / M 必要性）

脚本 `CreateFriend` → 0x1D8900 → 0x1D9C20 的步骤：

1. 0x1D9C20：描述符 `+0x86` 为真时对地吸附（0x1DC2D0）。然后调用 `CreateObject 0x11945E0(*(img+0x20B2958), &matrix, sgoPath, &InitParam)`：
   - InitParam 的 vt 在 `rip` 0x1D9D7E 处（同类 InitParamBase）；
   - 返回 shared_ptr（`[rax]` 为对象，`+0x30` 为对象指针）；
   - 返回 NULL 表示失败。
2. 0x1D8900：dynamic_cast 到 `GameObjectBase`。成功后：
   - `SetTeam 0x54EE70(obj, team, 1)`（友军 team = 描述符 `+0x88`）。(H)
   - 若 `+0x85` 置位：`0x54E740(obj, ctx[0x280+team*4] * level)`，即读 SGO `game_object_level_adjust`，按难度 / 等级缩放耐久与火力。(H)
   - 若 `+0x84` 置位：`0x548D50(obj)`，作用是切换 `+0x380` 的状态位，并经 vfunc +0xB8 计算 bit22（推测为 AI / 活动态，L）。
   - `0x1DB1E0(ctx, &shared)`：把对象加入任务对象组 `ctx+0xD8` 当前组（脚本句柄、计数、「全灭」判定用）。(H)
3. `CreateFriend` 0x1B0310 返回前：
   - 调 `obj->vfunc[0x190](obj, 1)`（0x1B051E，含义未定，L）；
   - 再经 0x1BEA30 封装成脚本 Object。
- `mission_setup` vs `vehicle_setup`：
  - `0x62D620` 读 `vehicle_setup`，调用方 0x92660 / 0xAF380 / 0xAF6A0（玩家呼叫路径）。(H)
  - `0x62D6E0` 读 `mission_setup`，调用方包括 RideAi slot50 `0x633030`（0x633063）、0x633280、0x62FBF0 等。(H)
  - 所以**插件给载具上 AI 驾驶员（RideAi）时，SGO 必须带 `mission_setup`**；只有 `vehicle_setup` 的呼叫型 SGO 会崩（与 gen.py 记录的 EDF.dll+0x52E44 一致）。(M)
  - testrange 的「把 `vehicle_setup` 改名为 `mission_setup`」派生法可以直接复用。(H)
- 插件自建友军喷气机的最小步骤：
  1. 按 3a 预载。
  2. `CreateObject(*(img+0x20B2958), &M, L"app:/object/<JET>.sgo", &InitParamBase)`，`InitParamBase` 的 vt 为 0x1762068（同 DemoAirStrike）。
  3. `SetTeam(obj, 2, 1)`（与原生空袭一致：team 2 = 友军；`AirStrike2` 描述符里的 team 3 只用于 Demo 对象的 level 消息）。
  4. `RideAi = vfunc[50](obj, false)` 塞 DummyVehicleRider（`crew.cpp` 已验证）。
- 可选步骤：
  - 0x54E740：要按任务难度缩放耐久时才需要。
  - 0x1DB1E0：只在希望脚本计数 / 「友军全灭」判定看到它时才需要；**不加更安全**，不会影响任务通关条件。(M)
  - 0x548D50：可省。(L)
- slot46 init（0x6530E0）读哪个 setup 块未确认；以 RideAi 的 `mission_setup` 依赖为准。(L)

### 3c. 删除 / 下车约定（H）

- `Delete 0x118A1B0(obj)`，`__fastcall`、单参数、无返回值：
  - `+0x18` bit2 已置位时直接返回，因此幂等。
  - 否则依次：调 vfunc +0x40；清 `+0x1A`；若原先置位则 `0x1195E80(*(img+0x20B2958), &weak)` 反注册；`+0x18 |= 4`；调 0x11975A0；递归删除子对象（链表 `+0x48/+0x58`）。
  - 对象内存由 shared_ptr 计数回收。插件持 weak 时用 `+8` use-count 判活。
- `SeatKick 0x62E1A0(vehicle, seatPtr)`：首指令 `test rdx,rdx` 判空。seat = `vehicle + 0x608 + i*0x340`，乘员在 `seat+0x260`（见 `crew.h`）。
  - 删除载具前先踢 DummyRider，避免悬挂乘员。(M)

### 3d. 线程安全（M）

- slot55 输入钩在载具 Update 内、主游戏线程上执行（`heli-input-re.md`）。原生代码也在 Update 期间生成和删除对象：
  - IFC_Update（武器 Update）里 `CreateObject` + `BombingPlane_Init`；
  - DemoAirStrike Update 里 `Delete(self)`。
  - 因此在 slot55 post 钩里 `CreateObject` / `SetTeam` / `RideAi` / `Delete` 与原生时序同类，可以接受。(M)
- 注意事项：
  - 避免在 slot55 里删除当前正在 Update 的载具自身；删除喷气机统一用「标志位 + 下一帧处理」更稳。Delete 只置删除位，实际回收由 shared_ptr 延后完成，风险较低。(M)
  - 预载 0x7A3780 带锁，但仍只在任务加载阶段调用（3a）。(M)

## 4. 钩点签名（RVA + 前 16 字节，H）

多数函数开头是通用 MSVC 序言，**只能做「固定 RVA + 字节比对」校验（同 loadout.cpp 的 `Matches`），不能拿来全局扫描**。

| 用途 | RVA | 前 16 字节 |
|---|---|---|
| DemoAirStrike create（工厂 slot2 @0x17D46B0 存的值） | 0x5B3F00 | 40 53 48 83 EC 20 48 8B DA B9 80 01 00 00 E8 9D |
| DemoAirStrike ctor | 0x5B40C0 | 48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 |
| DemoAirStrike Update | 0x5B46D0 | 48 8B 81 70 01 00 00 48 85 C0 74 06 83 78 08 00 |
| DemoAirStrike 消息处理 slot9 | 0x5B45E0 | 48 89 5C 24 08 48 89 6C 24 10 57 48 83 EC 40 0F |
| DemoIndirectFire Update（slot5 @0x17D4B48 存的值） | 0x5B5C50 | 48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 |
| DemoIndirectFire ctor | 0x5B55F0 | 48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 |
| IFC_Start | 0x2B5DA0 | 48 89 5C 24 18 56 57 41 56 48 83 EC 60 48 8B 05 |
| RadioContact 调 IFC_Start 的调用点 | 0x6A8DFB | E8 A0 CF C0 FF C6 87 EC 16 00 00 01 8B 87 E0 16 |
| IFC_Update | 0x2B87A0 | 48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 |
| BombingPlane_Init | 0x5AABB0 | 48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 |
| AirStrike2 | 0x1AC3E0 | 48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 |
| PointAirStrike | 0x1B82E0 | 48 8B C4 48 89 58 18 48 89 70 20 55 57 41 54 41 |
| CreateNeutral | 0x1B1B80 | 40 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 60 |
| CreateFriend | 0x1B0310 | 48 89 5C 24 20 55 56 57 41 56 41 57 48 8D AC 24 |
| 脚本对象生成 + 登记 | 0x1D8900 | 48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 |
| 生成（CreateObject 封装） | 0x1D9C20 | 48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 |
| CreateObject | 0x11945E0 | 40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 D9 |
| Delete | 0x118A1B0 | 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 48 |
| SGO 节点查找 | 0x528A0 | 40 53 48 83 EC 20 48 8B D9 E8 E2 FE FF FF 83 F8 |
| SetTeam | 0x54EE70 | 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 |
| SeatKick | 0x62E1A0 | 48 85 D2 0F 84 98 01 00 00 48 89 5C 24 08 48 89 |
| RideAi（slot50 实现） | 0x633030 | 48 89 5C 24 10 48 89 74 24 18 55 57 41 56 48 8D |
| 读 mission_setup | 0x62D6E0 | 48 89 5C 24 18 56 57 41 56 48 83 EC 40 48 8B DA |
| 脚本 Preload 原生 | 0x1B87E0 | 48 83 7A 18 08 72 03 48 8B 12 48 8B 0D B7 A1 EF |
| 通用资源预载 | 0x7A3780 | 48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 |
| 玩家预载（已被 loadout 钩） | 0x59DE50 | 48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 |

vtable 槽校验：`*(img+0x17D46B0) == img+0x5B3F00`，`*(img+0x17D4B48) == img+0x5B5C50`。(H)

全局指针：

| 指针 | 位置 |
|---|---|
| 对象管理器 | `*(img+0x20B2958)` |
| 资源预载管理器 | `*(img+0x20B29A8)` |
| GameStatus | `*(img+0x20B2890)` |

## 5. 未决 / 风险

- 2.1 的「先调原 create 再删飞机」：同一帧里 BombingPlane 已注册进对象表，删除走正常反注册路径，但还没实测是否会闪现一帧或播放语音。(M)
- 2.3 抑制后的武器状态机只按代码路径推断回 idle，未实测是否会卡在呼叫态。(M)
- 0x528A0 能否直接读 DSGO 支撑的武器 SGO（`weapon+0xA0`）未验证，读武器参数优先用 `+0x89C/+0x8B0/+0x8E8` 等已解析字段。(L)
- vfunc 0x190(obj,1)、0x548D50 的语义未定，插件生成时先不调用；若 AI 不动再补。(L)

## 6. 原版轰炸机的投弹节奏与收尾（2026-10-03）

- 投弹单元 `0x2B8470` 返回投弹帧数 `(弹数-1) × (间隔+1)`，读的是配置 variant 的第 2 项和第 9 项。
- 原版更新 `0x5AB240` 每帧固定前进 `+0xB90`，不乘 dt。投弹点 `+0xC40` = 机位 + 前向 × `+0xC10`（target_distance），高度取目标高度。
  - 每颗弹落在投弹点加散布半径（`+0x224`）内的随机点，由 `0x2312A0` 按弹速 `+0x220` 解弹道。
  - 所以地毯长度就是「帧数 × 每帧速度」。
  - 插件的 BayFrame 按步数推进投弹点，不跟喷气机的实际位置。
- 状态机：
  - 接近态 `0x5AB680`：沿航线距目标的投影小于 `+0xC14` + 一帧位移时，切到开舱态。
  - 开舱态 `0x5AB940`：进入时开舱（`0x2B4340`）；弹数 `+0xF10` 归零后，切到离场态 `0x5AB550`。
  - 最后一个状态 `0x5AB9A0`：
    - 进入时：速度 ×0，`0x6C04B0(plane+0x5C0, 0)` 停止绘制，`0x54DDB0` 从雷达注销；
    - 之后每帧：投弹单元 Done（`0x2B7B90`）后才 `0x118A1B0` 删除自己。
- 呼叫的目标红圈和飞机共存亡：插件以前在第一帧就删掉原版飞机，红圈在第一颗弹落地前就没了。现在第一帧照上面的进入态把飞机藏起来、停止更新，等喷气机的投弹单元没了（`JetHolds`）再删，最长保留 180 秒。
