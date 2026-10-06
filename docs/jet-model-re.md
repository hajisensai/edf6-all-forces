# 喷气机换模型：静态逆向结论

目标：用 Vehicle506_Helicopter（vtable `0x17DB238`）当友军喷气机。派生 SGO 基于 V506_HELI.SGO，把 `animation_model` 换成 bomber501.mrab，调整 `heli_rigid_body`，由插件驱动刚体。

本文只做了静态分析（EDF.dll TimeDateStamp `0x678CCB46`，地址都是 RVA），没有开游戏验证。置信度：H 表示读过代码、确定；M 表示推断合理；L 表示猜测。

---

## 1. MRAB / MDB 格式与骨骼名

工具：`python tools/mrab.py BOMBER501.MRAB V506_HELI.MRAB ...`。不带路径的文件名会从 Root.cpk 的 OBJECT/ 里读。它会列出每根骨骼的 index、parent、名字、局部位置、半尺寸和中心。

### 格式（H）

**RAB 容器（"SSA\0"）**
- `0x14` 是文件数，`0x18` 是文件表偏移。
- 每条文件记录 0x20 字节：
  - `+0`：名字偏移，i32，相对本条记录，UTF-16。
  - `+4`：存储大小。
  - `+8`：文件夹号。
  - `+0x18`：u64 数据偏移。

**CMPL 压缩**
- 头是 `"CMPL"`，后跟 u32 大端解压后大小。
- 算法是 Okumura LZSS：
  - 4 KB 窗口，全零初始化，写指针从 `0xFEE` 开始。
  - 标志字节按 LSB 先读，1 表示字面量。
  - 引用两字节 `b0 b1`：偏移 = `b0<<4 | b1>>4`，长度 = `(b1&0xF)+3`。
- EDF.dll 里对应的函数：解码 `0x3F540`，CMPL 判断 `0x3F7F0`，编码 `0x3F350`。
- 已验证：bomber501.mdb 解压后正好 26706 字节；dds 解压后以 `DDS ` 开头。

**MDB0**
- 头部 u32 数组：`[2]` 名字数，`[3]` 名字表，`[4]` 骨骼数，`[5]` 骨骼表。
- 每根骨骼 0xC0 字节：
  - `+4` parent，`+0x10` 名字索引。
  - `+0x20` 局部矩阵。
  - `+0xA0` 半尺寸，`+0xB0` 中心。

### 骨骼（H）

| 模型 | 骨骼 |
|---|---|
| bomber501.mdb | `mdl`, `bomber501` |
| bomber501_2.mdb | `mdl`, `bomber501_2`, `bomber501` |
| bomber401.mdb | `mdl`, `bomber401` |
| v506_heli.mdb | `mdl`, `globalSRT`, `body`, `rearWheelSus`, `rotor`, `bend_roterA–D`, `tailRotor`, `v506_heli` |
| v602_heli.mdb | 根是 `v602_heli`（没有 `mdl`）, `globalSRT`, `body`, `rotor`, `bend_roterA–D`, `tailRotor`, `polymesh` |
| vehicle409_heli.mdb | `mdl`, `Vehicle409_heli(Balkan)`, `globalSRT`, `body`, `balkan_*`, `frontWheelSus_l/r`, `missileA/B_l/r`, `rearWheelSus`, `rotor`, `bend_roterA–F`, `tailRotor`, `tailWheelCover_l/r` |

V602 的根骨骼不叫 `mdl`，所以它的 SGO 里有 `animation_model_bone_mapping = ['v602_heli','rotor']`。这说明引擎允许改骨骼名映射（H）。

### 包围盒（H）

| 模型 | 半尺寸 X / Y / Z | 中心 | 换算 |
|---|---|---|---|
| bomber501 | 12.38 / 1.62 / 15.26 | (0, 0.34, 2.60) | 翼展约 24.8 m，高约 3.2 m，长约 30.5 m，Z 是机身轴 |
| bomber401 | 25.94 / 2.01 / 8.08 | (0, 2.14, 0) | 飞翼，宽约 52 m |
| v506 body | 2.8 / 1.82 / 6.1 | — | 原版 `heli_rigid_body` 为 `[[0,1.45,0.65],[2.8,1.45,3.0],0.305]` |

从 v506 的数据看，`heli_rigid_body[1]` 是刚体盒的半尺寸，而且比网格小（M）。

---

## 2. 直升机代码依赖哪些名字，缺了会怎样

| 依赖 | 在哪里查 | 缺了会怎样 | 置信度 |
|---|---|---|---|
| `.cas` 文件（`animation_model[1]`） | `0x650560` 里的 `Main` / `Roter` / `roter_speed` / `roter_up` / `RoterRoll` / `Body` | 没有 cas 时 Main 下标是 -1，`0x11664F0` / `0x116DF20` 都不做边界检查，`0x6509BC` 会读 `[-1*0x160+0xE0]`，**必崩** | H |
| cas 节点与模型骨骼的绑定 | `CASController::Initialize 0x1167520` | 节点找不到时只打日志，在 `0x1168540` 把绑定槽写成 null，然后继续。v506 的 cas 配 bomber 的 mdb 可以用，只是动画不会作用到模型上 | H（行为） / M（整体） |
| 根骨骼，`R+0x14` = veh+`0x1574`（默认查 `mdl`） | `0x650560` → `0x11002A0`，找不到返回 -1 | 每帧在 `0x651B8F` 用它的位置做地面射线，结果写 veh+`0x1C40`/`0x1C44`。`0x1100280` 不检查 -1，会越界读到垃圾。bomber 有 `mdl`，所以没问题 | H |
| rotor 骨骼，`R+0x10` = veh+`0x1570`（默认查 `rotor`） | 同上 | bomber 没有这根骨骼，结果是 -1，会越界读到骨骼数组前面的堆内存：<br>- 初始化时 `0x64EBE2` 取记录 `+0xF0` 当 rotor 半径，交给 `0x650D40` 建 Havok 形状。<br>- 每帧 `0x6519E7` 取 `+0xB0` 当世界矩阵，在 `0x651B45` 做 rotor 接触查询。<br>- `0x653AF6`、`0x65567A` 也会用到。<br>结果是垃圾半径和垃圾矩阵，**可能崩**。**必须映射到一根存在的骨骼** | H（无检查） / M（后果） |
| ragdoll 记录 `'body'` | `0x6EA4B0` 在 ragdoll 自己的哈希表里查（名字来自 .shkt，不是模型骨骼），结果写 veh+`0x1530` | slot 61（`0x650010`，经 `0x61B6E0` 调用）在 edx=1 时，如果下标是 -1，会执行 `mov rdx,[0+0x50]`，**空指针崩溃**。保留 `Ragdoll_v506_heli.shkt` 就不会有问题 | H |
| `animation_from_ragdoll` / `ragdoll_from_animation` 映射里的模型骨骼 | `0x6E6A50`（BindDependency） | **实测崩溃（2026-10-03）**：原样带 V506 的映射配 bomber 模型时，载具构造在 `0x629912 → 0x6EB4C0 → 0x6E8284` 读空指针（映射表项 `+0x60` 为空）。之前「找不到只跳过」的判断是错的。映射里每个模型侧骨骼都必须存在 | H |
| MAB 定位点的父骨骼（`animation_model[2]`） | `0x6BADD0`：先按名字找定位点，再用 `0x11002A0` 找父骨骼；任何一步失败都返回 false，句柄是 {0,0} | `0x62B430`（riding position）**不检查返回值**。之后 `0x629CEE` 在座位循环里调 `0x6BB420(seat+0x1E0)` 读 `[bone+0xB0..]`，也不判空，**父骨骼缺失就在 VehicleBase 构造时崩溃** | H（无检查） / M（必崩） |
| MAB 定位点，damage effect 用 | `0x6C74E0` → `0x11001F0`（会返回 null） | 存下的骨骼指针可能是 null，消费方没查 | L |
| `vehicle_weapon_setting` 的骨骼名（查 **MDB 骨骼**，不是 MAB） | `0x629450` → `0x11001F0` | 找不到时打 `weapon node not found` 日志（release 版里日志函数 `0x3E4A0` 是空函数）。之后**跳过**：holder 的节点（+0x18）和 owner（+0x38）都没设置，也没有登记到座位。后续用到时可能空指针 | H（跳过） / M（后续） |
| `vehicle_dead_effect` 的骨骼名 | `0x64EDB0` → `0x5F42A0` 等，`0x11002A0` 的结果原样存下（`0x5F509B` / `0x5F5F4A` / `0x5F6345`） | 播放时 `0x5F46FB` / `0x5F4830` / `0x5F8693` 直接调 `0x1100280`，不检查 -1，会在垃圾位置播特效或读坏内存 | H（无检查） / M（后果） |
| `heli_effect_shock_wave` | `0x64F048` → veh+`0x1C90` vtable+8 / `0x5F0640` | 参数只有 3 个浮点数，不带骨骼名 | M |
| `roter_contact_damage_scale` | `0x6530E0` | 设成 0 可以让 rotor 接触伤害为 0 | M |

**.cas 是做什么的**：驱动旋翼转速、旋翼升降和机身倾斜的动画（动画集 Main / Body / Roter / RoterRoll，变量 roter_speed / roter_up 等）。直升机代码在 `0x650560` 里要求它存在。它可以搭配别的 mrab 一起用，只是绑不上的节点会变成 null，动画不生效（H/M）。

---

## 3. 结论：bomber501 能用吗？最小 SGO 改动

**能用，前提是下面每一项都做到**（M，整体没有实机验证）：

1. `animation_model = [[bomber501.mrab, bomber501.mdb], v506_heli.cas, <改过的 v506 MAB 块>]`
   - **cas 必须保留**，否则必崩（H）。
2. 加上 `animation_model_bone_mapping = ['mdl', 'bomber501']`
   - `[0]` 是根骨骼，`[1]` 是 rotor 骨骼。读取顺序见 `0x650560`（H）。
   - rotor 映射到机身骨骼后，rotor 的半径（运行时记录 `+0xF0`）和接触查询会作用在机身上。所以必须配合第 5 条（M）。
3. MAB 块：把里面的父骨骼名 `body` / `rotor` / `tailRotor` 原地改成 `mdl`，后面补 NUL。
   - v506 的 MAB 块里这几个 UTF-16 字符串分别在块内 `0x360` / `0x372` / `0x37E`。`mdl` 本身在 `0x36A`。
   - 查找函数遇到 NUL 就停（`0x6BAE65`），所以只能缩短，不能加长（H）。
   - 不改的话，riding position 会在构造时崩溃（M）。
   - 改完以后定位点的局部偏移是相对 `mdl` 的，不再相对 `body`。座位和相机的位置会偏一点，这对 NPC 没有影响。
4. `vehicle_weapon_setting`：四项的骨骼名都从 `'body'` 改成 `'mdl'` 或 `'bomber501'`（H：查的是 MDB 骨骼）。枪口偏移要按 bomber 的尺寸重新填。
5. `roter_contact_damage_scale = 0`。如果不想被撞伤，`heli_contact_damage_scale` 也一起降低（M）。
6. `vehicle_dead_effect`：其中的 `body` / `rotor` / `tailRotor` 全部改成 `mdl`（M）。
7. `ragdoll`：保留 `Ragdoll_v506_heli.shkt`（H：slot 61 需要它，缺字段会抛 bad_variant_access），但内嵌 SGO 的映射要改写：`ragdoll_from_animation` 的模型侧全部改成 `bomber501`，`animation_from_ragdoll` 只留 body→`bomber501` 一项（gen.py `_jet_ragdoll`）。
   - 内嵌映射里的骨骼找不到会让构造崩溃（见上表，实测）。
   - 坠毁时 ragdoll 的体积是直升机大小，与 bomber 模型不符，只影响外观（L）。
8. `heli_rigid_body`：用机身盒，**不要把翼展算进去**，否则低空时翼尖会一直接触地面和建筑。
   - 建议值 `[[0, 0.34, 2.6], [2.0, 1.6, 13.0], 0.305]`，第三个值沿用原版，含义未确认（M）。
   - 完整包围盒是半尺寸 (12.38, 1.62, 15.26)，中心 (0, 0.34, 2.60)（H）。
9. 机头朝 +Z（H，见文末「机头朝向」一节）。

bomber401 是飞翼（半宽 26 m），不适合当战斗机。

---

## 4. 飞控

### 世界变换（H）
- slot 55 的基类 `0x6543A0` 一开头就会把刚体的世界变换拷到 veh 上：
  - `veh+0x60` / `+0x70` / `+0x80`：旋转矩阵的三行。
  - `veh+0x90`：位置。
- 读取顺序：`body=[veh+0x1650]`，`w=[[body+0x100]+0x58]+0x20`，然后调 `w->vtbl[+0x80](w, [body+0xF0])` 得到运动对象，再用 `0x97D870(obj, &mat4)` 转成 4x4 矩阵（`0x6543E1`–`0x654437`）。
- 所以**在 slot 55 之后**读 veh+0x60..0x9F，得到的就是这一帧的取向和位置。插件需要在其他时机读的话，就照这个顺序自己调一次（取向 getter 就是这条链，H）。
- `0x11B1060`（GetAngularVelocity）走的是同一条链：`+0x80` → `+0xF8` → `0xDF68A0`。

### Vehicle506 的 vtable 槽（H，`q(0x17DB238+n*8)`）

| 槽 | 函数 | 作用 |
|---|---|---|
| 55 | `0x61B8F0` | 调 `0x6543A0`（输入 / 夹紧），尾跳 `0x64FFD0` |
| 57 | `0x61B710` | 调 `0x6519A0`（物理），之后调 3 次 `0x62C000` |
| 61 | `0x61B6E0` | 调 `0x650010`，再调 `0x64FFD0` |

### 签名（文件字节，本版本 EDF.dll）
"命中数"是这段字节在整个 .text 里出现的次数。插件按固定 RVA 逐字节比对，所以不要求唯一。如果要扫描，用 32 字节版本。

| RVA | 前 16 字节 | 16 字节命中数 / 32 字节命中数 | 接下来 16 字节 |
|---|---|---|---|
| `0x61B710` slot57 | `40 53 48 83 EC 20 48 8B D9 E8 82 62 03 00 80 BB` | 1 / 1 | `20 20 00 00 00 74 1C 48 8B 8B 38 06 00 00 E8 CD` |
| `0x6519A0` heli 物理 | `48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55` | 168 / 7 | `41 54 41 55 41 56 41 57 48 8D A8 D8 FE FF FF 48` |
| `0x61B8F0` slot55 | `48 89 5C 24 18 48 89 6C 24 20 56 48 83 EC 20 48` | 5 / 1 | `8B F1 E8 99 8A 03 00 48 8B AE 08 06 00 00 48 8B` |
| `0x6543A0` 基类输入 | `48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48` | 316 / 2 | `89 78 20 41 56 48 81 EC B0 00 00 00 0F 29 70 E8` |
| `0x11B18F0` SetLinearVelocity | `48 8B 81 00 01 00 00 4C 8B C2 8B 91 F0 00 00 00` | 5 / 5 | 和 `0x11B1760` 的区别在 `+0x1F`：`FF A0 **A8** 00 00 00`（jmp [rax+0xA8]） |
| `0x11B1760` SetAngularVelocity | 与上一行相同 | 5 / 5 | `+0x1F`：`FF A0 **B0** 00 00 00` |
| `0x11B1A00` SetPosition | `40 55 48 83 EC 70 48 8D 6C 24 40 48 83 E5 E0 48` | 1 / 1 | `8B 05 42 D6 E3 00 48 33 C4 48 89 45 20 0F 10 0A` |
| `0x11B1300` GetLinearVelocity | `40 53 48 83 EC 20 48 8B 81 00 01 00 00 48 8B D9` | 3 / 3 | `48 8B 48 58 48 8B 41 20 48 83 C1 20 8B 93 F0 00` |
| `0x11B1060` GetAngularVelocity | `48 89 5C 24 18 57 48 83 EC 40 48 8B 05 E7 DF E3` | 1 / 1 | `00 48 33 C4 48 89 44 24 30 48 8B 81 00 01 00 00` |

- `0x11B18F0` 和 `0x11B1760` 的前 0x1F 字节完全一样，只有 jmp 的 vtable 偏移不同。签名至少要覆盖 34 字节才能区分这两个函数（H）。
- `0x11B1060` 和 `0x11B1A00` 里有 rip 相对的 cookie 偏移。换游戏版本后这几个字节会变，这本身就是想要的效果。

### slot 55 `0x6543A0` 的夹紧（H：读法，M：语义）

1. 先刷新 veh+0x60..0x9F，见上文。
2. 天花板：`ceil = [[0x20B2998] − 8 + 0x44]`，等价于 `*(*(image+0x20B2998)+0x3C)`，和 `src/jet.cpp` 一致。如果 `pos.y > ceil`，就把 y 设成 ceil，并把标志置 1。
3. 区域：`0x5A9E50(区域 = 0x11BD90(), &pos, r = −[veh+0xE00], mask=3)`。
   - 盒子范围：下界 `[区域+0x10] − r`，上界 `[区域+0x20] + r`。因为 r 是负的，盒子实际按 veh+0xE00（载具半径）往里缩。
   - bit0：X 和 Z 夹进盒子。
   - bit1：Y 超过 `min([区域+0x40], max.y)` 时压到那个值；低于 `min.y` 时代码写入的是 max.y，原样照录，语义未确认（L）。
   - 只要有调整就返回 true，回到第 2 步再检查一遍，直到稳定。
4. 有任何调整就调 `0x11B1A00(body, &pos)`，并写 veh+0x90。**只改位置，不改速度**。

**只对单台载具解除夹紧**（M）：
- 插件的 slot 55 钩子在调用原函数**之前**，用上面的 getter 链（或 `0x11B1A00` 对应的读法）取出真实位置 P。
- 调完原函数后，如果 veh+0x90 ≠ P，就调 `0x11B1A00(body, &P)` 并把 veh+0x90 写回 P。
- 这样只影响这一台，不改全局的天花板和区域。
- 不建议改 veh+0xE00：它是从座位定位点算出来的载具半径，别处也会用到（L）。
- 不建议改天花板全局值：所有载具都会受影响。
- 不建议跳过原函数：它还负责清空输入块并尾跳 slot 56 的输入逻辑（见 heli-input-re.md）。

---

## 机头朝向

结论：**BOMBER501 / BOMBER501_2 / BOMBER401 的机头都是模型局部 +Z**（置信度 H）。这推翻了 §3 第 9 条的“未确认（L）”。

证据（EDF.dll `0x678CCB46`，RVA）：

1. `BombingPlane_Init 0x5AABB0` 用飞行方向构造对象旋转：
   - `0x5AAC04`–`0x5AAC67`：`+0xB80 = 目标(+0xB70) − pos(+0x90)`，然后把 `+0xB84`（y）清零。
   - `0x5AAC7A call 0x4D940(+0xB80, speed)`：把它归一化再乘速度，所以 `+0xB80` 就是速度向量。
   - `0x5AAC86 call 0x4E1F0`：`yaw = atan2f(d.x, d.z)`（IAT `0x1756450` = atan2f，xmm0 = x，xmm1 = z）。
   - `s = sinf(yaw)`（`0x12DA8B8` → IAT `0x1756408`），`c = cosf(yaw)`（`0x12DA8B2` → IAT `0x1756400`）。
   - 写对象矩阵（`0x5AACAF`–`0x5AACEA`）：
     - row0 `+0x60` = (c, 0, −s)
     - row1 `+0x70` = (0, 1, 0)
     - row2 `+0x80` = (s, 0, c) = **+归一化水平飞行方向**
   - row0 × row1 = row2，是右手系，没有镜像。
2. `BombingPlane::Update 0x5AB240`：
   - `0x5AB2C1`：`pos += +0xB80`，也就是沿 +row2 前进。
   - `0x5AB2E3 call 0x1100B90(+0x660, &+0x60)`：把这个矩阵交给模型实例。
   - `0x5AB2FC`：瞄准点 = pos + row2 × lead（`+0xC10`）。
3. 三个 bomber MDB 的 `mdl` 与机体骨骼的局部矩阵都是单位阵（`mrab.py` 读 `+0x20` 前 12 个 float，结果是 1,0,0 / 0,1,0 / 0,0,1）。所以模型空间等于对象空间，渲染时没有额外翻转。
4. v506_heli 是同一套约定：尾桨 `tailRotor` 在 z = −7.15，后轮 `rearWheelSus` 在 z = −7.25，所以机头是 +Z。这和插件把 `veh+0x80` 当前向的做法一致。

对插件的影响：直接把速度方向写进 row2（`+0x80`）即可，row0 = row1 × row2，不要取反。bomber501 包围盒中心 z = +2.6 只说明前后不对称，这个结论不依赖它。

## 6. 空中航母的推力矢量舱（V508 的四个 booster）

插件实现：`src/jet_flight.cpp` `Thrusters()`、`kThrustBack` 一段注释。数据来源：`python pylib/mdb.py dump V508_TRANSPORT.MRAB`，`V508_TRANSPORT.SGO` / `V508_TRANSPORT.CAS`（Root.cpk 只读），`tools/edfre.py` 查字符串。

### 骨骼（H）

`v508_transport.mdb`：`mdl → globalSRT → body → boosterB_l / boosterB_r / boosterF_l / boosterF_r`，另有网格骨骼 `v508_transport`（挂在 `mdl` 下）。

| 骨骼 | 绑定局部矩阵（未缩放） | 包围盒半尺寸 / 中心（骨骼自身坐标） |
|---|---|---|
| boosterB_l / _r | 旋转 = 单位阵，平移 (±6.32, 4.50, −19.57) | (2.41, 2.22, 4.17) / (±2.55, 0, −0.70) |
| boosterF_l / _r | 旋转 = 单位阵，平移 (±13.57, 3.47, 2.64) | (2.41, 4.92, 8.13) / (±2.58, 0, −0.71) |

- 骨骼原点就是舱的转轴（挂点），绑定姿态是**水平**的（舱长轴沿局部 z，机头 +z）。
- 航母模型是这个 mdb ×1.6（`pylib/jet_models.py`），只改平移，旋转不变，所以下面的角度原样适用。
- 名字带 `_l` 的在 +x。插件不依赖 l/r 的含义，按绑定平移的 x 正负决定偏航差动的方向。

### 原版怎么动它们：只有 CAS 动画片段（H）

- EDF.dll 里**没有** `boosterF_l` 等任何 booster 骨骼名（窄、宽字符串都查过，只有无关的 `booster`）。Transporter508（`xgs_scene_object_class`）不按名字用代码驱动这些骨骼。
- `V508_TRANSPORT.CAS` 里的片段：`default`、`hover_start`、`hover_end`、`fly`（还有 `t_fly` / `t_hover_start` / `t_hover_end`）。
- 四根 booster 的轨道都是绕局部 X 的四元数 (x, 0, 0, w)：
  - `hover_start`：(0,0,0,1) → (−0.7071, 0, 0, 0.7071)，也就是 0° → **−90°**，末尾有一点过冲（−0.7117）再回到 −90°。
  - `hover_end`：−90° → 0°。
- 按 DirectX 行向量约定，绕 X 转 θ 是 `[[1,0,0],[0,c,s],[0,−s,c]]`，和 `Elevons()` 用的 `local = Rx(θ) × bind` 同形（M：四元数转矩阵的约定按 DirectX 推断，没有反汇编 CAS 求值代码）。
- θ = −90° 时，舱的 +z（机头）转到父骨骼 +y：机头朝上、喷口朝下，推力沿机身向上，是悬停姿态；θ = 0 是平飞姿态（M：方向由上面的约定推出）。
- SGO 里还有 `boosts`（6 条：名字、两个 float、一个开关），多半是喷口特效的挂点表，属于 Transporter508 类，航母用的 V506 壳不会读它（L：没有追代码）。

### 航母为什么从来不动舱（H）

- 航母的 SGO 是 V506 改的（`testrange/gen.py` `jet_sgo`）：`animation_model = [航母 mrab, v506_heli.cas, 改过的 V506 MAB]`。V508 的 CAS 根本没加载，`hover_start` 永远不会播放。
- V506 的 CAS 只驱动它自己的节点名（`body` / `rotor` / `tailRotor` 等），booster 骨骼没有轨道，局部矩阵一直是绑定值。

### 插件直接写骨骼局部矩阵是可行的（H）

- 方法同升降副翼（`docs/mdb-format.md` §3）：骨骼记录 `+0x70` 写 `Rx(θ) × bind`，引擎每帧 `world = local × parent.world`。
- 证据：本机 `EDF6VehicleCrew.log` 里 `elevons: found` 出现 236 次，「rewritten by the game between frames」0 次——没有动画轨道的骨骼，局部矩阵不会被游戏改回去。推力舱沿用同一检测并会记日志。
- 画面上的效果（舱是否真的转、转向是否与预期一致）**未在游戏里看过**（L）。

### 插件的映射

- 推力向量 T = Hover 给的加速度 + 重力 + 演示用阻力 `kThrustDrag × 速度`（插件直接设速度，没有阻力；不加的话匀速飞行时舱会竖着）。
- T 换到机身坐标：`θ = atan2(−T·up, T·forward)`，限制在 [−110°, 0°]，每秒最多转 `kThrustRate`。悬停 −90°；前飞、加速往 0° 倾；刹车超过 −90°（向后）。
- 舱只能在机身的俯仰平面里转：侧向加速仍靠机身横滚；前后方向机身只承担 `kCarrierPitchShare`（1/4），其余由舱承担。
- 偏航：位于 x 的舱向前倾产生绕机身 up 的力矩 −x·Fz（r × F），所以要按 `ω·up` 转时，两侧舱反向各倾 `kThrustYaw`（倾转旋翼机悬停时的偏航方式）。
- 爬升、下降改变 T 的竖直分量，相同前后加速下爬升时舱更竖、下降时更倾。推力大小本身（喷口火焰）没有表现，见上面 `boosts`。

## 7. Own motion properties：固定翼自己的 Havok 运动属性（2026-10-04）

### 为什么固定翼被限在 ~200 m/s（H）

所有载具刚体共用同一个 hknpMotionProperties 预设，其 `+0x10` maxLinearSpeed = 200 m/s。插件每帧
SetLinearVelocity 写多少都没用，Havok 在积分时夹到 200：18:58 那次测试里指令 260-360 m/s 的喷气机实测只飞 200-211。
这就是「固定翼像套了直升机」的硬上限。原地改这个共享预设不行：所有直升机、卡车都用它。

### 结构（H：读字节；M：字段语义）

- 刚体包装（Vehicle `kBody` 指向的对象）：`+0x60` 它自己的 props 副本（0x70 字节），`+0xF0` bodyId（u32），
  `+0xF6` propsId（u16），`+0x100` 世界包装，世界包装 `+0x58` = hknpWorld。
  jet.cpp 的 SetLinearVelocity 签名字节就是这条链：`mov rax,[rcx+100h]; mov edx,[rcx+F0h]; mov r10,[rax+58h]; lea rcx,[r10+18h]`。
- hknpWorld `+0x18` 是写接口，vtable 槽 32 = `image+0xE50720`：`setBodyMotionProperties(iface, u32 bodyId, u16 propsId)`。
- hknpWorld `+0x928` 是运动属性库：`+0x40` entries，`+0x48` count，每项 0x70 字节。
- `image+0xE15190` = `add(library, u16* outId, const props*)`：库满时 outId 写 0xFFFF；可能重新分配 entries（调用后必须重读）。
  props `+0` 必须为 0 它才会复用相等的已有项（去重）。
- props `+0x10` maxLinear，`+0x54` = 5/maxLinear，`+0x58` = 0.005/maxLinear（M：两个随 maxLinear 缩放的阻尼/休眠项，按原值比例推出）。

### 插件怎么做（`src/jetprops.cpp`）

1. 安装时核对两个函数的签名，不符就整体不启用（日志 `HOOK jet motion props=0`，喷气机照旧 200 m/s 内）。
2. 每个物理步、在写速度之前（jet.cpp / playerjet.cpp 的 PhysicsHook）：读该刚体当前 propsId；
   若已是 600 m/s 的副本直接返回；否则把该预设复制一份，`+0` 清零、maxLinear=600 及两项倒数，`add` 进库，
   再 `setBodyMotionProperties` 换到新项，并同步包装里的 `+0xF6` / `+0x60` 副本。每个世界每个预设只加一项（缓存 8 条）。
3. 从不修改库里原有的项。

### 未验证（L）

- 在物理步回调里调 setBodyMotionProperties 是否立即生效、还是被 Havok 推迟/忽略：没有在游戏里测过。
  失败时的表现应是「没效果、仍 200 m/s」（全部包在 `__try` 里），日志里看 `JET motion props: preset a -> b` 是否出现、
  以及喷气机实测速度是否超过 211。
- 速度上限：NPC `kBodyTop` 250 m/s（5 g 转弯半径 1275 m × Guard 的墙裕度要装进 ±2400），玩家 260 m/s。

## 8. 2026-10-04 追加：Booster 尾焰、空母俯仰/压坡度、炮舰机、远景渲染

全部未在游戏里测过（L），只确认编译通过、代码路径自洽。

### 8.1 空母的 Booster 尾焰（`src/booster.cpp`）

- 空母（V508 四旋翼机体）挂四个原版 Booster 对象，骨骼 `boneF_l` / `boneF_r` / `boneB_l` / `boneB_r`。
- 构造：`op_new` `image+0x12D85B0`(0x410) → ctor `image+0x2CB810`，vtable `image+0x17A6D58`；挂骨骼 `image+0x118AF20`，
  注册 `image+0x1195A20` / `image+0x1197050`。
- 更新是 vtable 槽 5 `image+0x2CBE30`：每帧把 `*(+0x3D8)` 指向的骨骼矩阵复制到 `+0x60`，所以挂上后跟着骨骼走。
- 插件每帧写 `+0x3EC` = 推力份额（随该旋翼的推力），`+0x3F0` = 1，`+0x3F4` = 3。
- 尺寸：前 56/16，后 40/12（V508 原版 35/10、25/7.5 的 1.6 倍，空母机体放大过）。
- 空母离开 1000 ms 后或死亡时删除四个 Booster。
- 火焰朝向（2026-10-04 实测反了，已修）：原版的火焰挂在 V508 SGO `animation_model[2]` 内嵌 MAB 里的定位点「ブースト0..3」上（父骨骼 boosterF_l/F_r/B_l/B_r，欧拉角都是 (0, π, 0)，平移 (±2.15, −0.04, −5.95) / (±1.45, 0, −4.30)），世界矩阵 = L × 骨骼世界矩阵（0x6BB5A0，行向量），火焰沿矩阵 +Z 喷出（方向常量 0x1765B70 = (0,0,1)）。插件现在按同样方式算（`NozzleMatrix`：X、Z 行取反，平移 ×1.6）。第 4/5 项挂在 body 上、标志 0，只在载具某个状态（推测加速）时点火，插件不做。
- 未验证：尺寸是否合适。

### 8.2 空母的俯仰与压坡度（`src/jet_internal.h` / `src/jet_flight.cpp`，`Lean`）

- `struct Lean { pitchShare, drag, respond, jerk, maxLean, bank; }`；空母用 `kCarrierLean = {0.8, kThrustDrag, 2.5, 1.2, 0.3, 2.5}`。
- 平滑后的加速度分成沿机头方向和横向两部分：前后部分 × `pitchShare`（0.8）变成低头/抬头，横向部分 × `bank`（2.5）变成压坡度，
  合起来限制在 `maxLean` 0.3 rad（约 17°）以内。转弯时向内侧压坡度，看起来不再是平移。
- 未验证：方向符号（压坡度方向是否朝内），幅度是否太大。

### 8.3 炮舰机（`Role::gunship`，呼叫 7118 / 7119）

- 角色行 `"gunship"`（`src/jet_internal.h` `kKinds`），`Weapon::shells`（不俯冲、不开机炮，绕锚点盘旋），巡航 120 m/s、高度 350 m、
  巡逻圆 600 m（编队里每多一架 +80 m）、取目标范围 1500 m、燃料 3.0 倍。
- 机体是自己的 SGO `EDF6VC_JET_GUNSHIP.SGO`（`tools/make_jets.py`：对地攻击机套 BOMBER401 模型，mark 改成 7011），
  所以条目重建时按 mark 认得出是炮舰机。2026-10-04 之前它借用接管轰炸机的 `EDF6VC_BOMBER401.SGO`（mark 7001），
  条目一重建就被当成对地攻击机。没装这个 SGO 时炮舰机呼叫退回战斗机。
- `JetRole::gunship`（`src/crew.h`）→ `kLaunchRows` 的 `Body::gunship`；角色由机体的 mark 决定（`kBodies`）。
- 炮弹：任务里鲸鱼炮舰的 `DEMOGUNSHIPFIREE25`（DemoIndirectFire，见 `docs/mission-airstrike-re.md`），
  用 CreateObject 造（与 `carrierlaser.cpp` 的光束同一方式）：先核 vtable `image+0x17D4B20`，接口在对象 `+0x170`，
  设归属（炮舰机自己与其控制器，不打自己的机体）、伤害 300、`+0x2F9`=1、起点（`+0x300`）= 炮口（见下）、终点 = 目标锁定点，
  首发前等待帧（`+0x2D8`）写 0。不符 vtable 就删掉对象并关掉本任务的炮击。
- 炮口（`src/gunmuzzle.h`，2026-10-06，用户：「炮舰机的机炮会打到自己身上」「炮舰机的轰炸炮弹，感觉在飞机后面出现的」）：
  - 以前起点是载具原点（`veh+0x90`）。bomber401 模型的原点在机腹底（最低顶点 y 0.13，机翼 y 1.3–2.8，翼展 52 m）。炮舰机绕目标
    盘旋时向内压坡度（NPC 2 g 约 60°，玩家最多 `kTurnBank` 69°），而俯视目标只有 22–30°：在机体自己的坐标里，从机腹到目标的
    这条线是向上斜穿机身和内侧机翼的。离线仿真（`tools/gunship_muzzle_check.cpp`）：NPC 盘旋时机炮弹道有 6.6 m 在整机包围盒里，
    玩家 74 m/s、半径 400 m 时 17.6 m。
  - 现在：从整机包围盒（`tools/make_jets.py GUNSHIP_AIRFRAME`，即 `jet_models.model_box('bomber401')`，构建时对模型核对）的中心
    沿到目标的线，走到「包围盒各向外扩 弹的命中半径 + 0.5 m」的边界处出膛（机炮弹命中半径 0.8 × 2 = 1.6 m，炮弹 10 × 1 = 10 m）。
    扩大后的盒仍是凸的，所以出膛之后到目标的整段都在机体外，与姿态无关。只靠子弹的 owner 排除（`docs/bullet-pass-re.md` §3.2）
    也不打自己，但弹道穿过机体时曳光和命中特效看起来就是打在自己身上。
  - 原版 `DEMOGUNSHIPFIREE25` 的 #15 是 60 帧（任务里炮舰在画面外，先响炮声、1 秒后炮弹才到），IFC 只在这个计数减到 0 时才发射，
    起点读的是那时的 `+0x300`（`0x2B9B7D`）。插件只在造对象时写一次起点，于是炮弹从 1 秒前炮舰机所在的地方出来：120 m/s 盘旋时落后
    约 120 m（仿真 120.4 m，玩家 145 m/s 时 144.9 m）。`ShellMake` 现在把 `+0x2D8` 写 0（签名 `0x2B624D` / `0x2B97BC` / `0x2B97D3`，
    不符则保留原版等待并记日志），炮弹在第一步就从炮口出去；插件自己的 SGO（机炮、撞击装药、钻头、EMC）本来就是 0。
    普罗透斯的齐射同样用这种炮弹，它按飞行时间算提前量，以前多等的 1 秒没算进去，现在也一并对上。
  - 仍未进游戏验证：新造的对象是在当帧还是下一帧走第一步（仿真按最多落后 1 帧算：起点离当时的炮口不超过 1 帧的飞行距离，2.4 m）。
- `GunshipFire`（`src/jet_bay.cpp`）：每 2.5 s 一发；只打非飞行目标、目标在 1800 m 内；开火门控与其它喷气机武器同一个
  `WeaponsFree`（JetPilot、有目标、不在起飞 / 回收 / 撤离），不再看直升机的 `HeliFire`。用到的 IFC 函数（`kIfcOwner` /
  `kIfcDamage`）走弹舱的签名校验（`bayOk`），DemoIndirectFire 的 vtable 也先确认可读。
- SGO `app:/object/demogunshipfiree25.sgo` 随炮舰机的机体一起预载（`PreloadShells`）；机体没预载时炮舰机退回战斗机。
- 呼叫：7118「炮舰机·守点」、7119「炮舰机·跟随」（`airstrike.cpp kCalls` 与 `tools/call_weapons.py CALLS` 同序追加在
  7117 之后，安装器按前缀续写已装的行）。
- 未验证：炮弹是否从炮舰机身上出发可见、伤害与命中、盘旋半径与高度是否合适、bomber401 机体是否正常飞。

### 8.4 远景渲染（`FarRender`，详细逆向见 `tmp/view-distance-re.md`）

- 场景通过两个 Umbra 相机绘制：近相机 0.1 m 到 LightEnv FarClipZ（所有任务里都是 1000 m），只画遮罩含 bit25|bit27 的节点；
  远相机 500 m 到 20 km，只画 bit26（`0x04000000`）。
- 载具的渲染节点建立时遮罩是 `0x12000000`，没有 bit26，所以飞出 1000 m 的喷气机就看不见了。
- 游戏自己的开关是 SGO `FarRender` / `use_far_render` 走的 `image+0x11B3020(node, true)`。
  喷气机的节点是模型组件 `vehicle+0xE40`（vtable `image+0x176B9A8`），遮罩在节点 `+0x20`。
- 插件每帧（`JetFrame` → `src/jet_spawn.cpp` `FarRender`）检查：vtable 不符 → 记日志、这架不再处理；bit26 已在 → 什么都不做；
  否则调用开关并复查，没生效就关掉这架的远景。近相机那一路不变。
- 开关函数在安装时核签名（`kSetFarRenderSig`：`44 8B 41 20 41 8B C0 0F BA F0 1A 41 0F BA E8 1A`，即 `mov r8d,[rcx+20h]; mov eax,r8d;
  btr eax,26; bts r8d,26`，取自同一 TimeDateStamp 的 EDF.dll），不符就整体不开远景渲染（H）。
- 风险：500–1000 m 两个相机都画（可能重影/闪烁）；远相机的光照/阴影可能不同；模型若有子节点，子节点可能没有 bit26。
- 备选（未实现）：改环境 `env+0x1a0` 的近裁剪距离，把 1000 m 拉远——影响全场景，代价大，只在上面方式无效时考虑。

## 9. 代码结构与生命周期（2026-10-04 设计审查修复）

- 文件：`src/jet.cpp`（条目表、身份、每帧调度 `JetFrame`、回收 `JetReap`、关卡重置）、`jet_flight.cpp`（固定翼 / 悬停飞行、地面、天花板、墙）、
  `jet_combat.cpp`（选目标、机炮导弹攻击、统一开火门控 `WeaponsFree`）、`jet_carrier.cpp`（母舰、无人机、自爆装药、人偶）、
  `jet_bay.cpp`（弹舱、炮舰炮弹、撞击装药）、`jet_spawn.cpp`（机体表、预载、生成、远景渲染）、`jet_hooks.cpp`（506 物理步、子弹穿僚机、安装）；
  共享声明在 `jet_internal.h`。
- 机体表 `kBodies`（SGO、文件名、mark、角色、母舰的无人机种类）是机体与 mark 的唯一来源；`kKinds` 每个角色一行，带飞行模型
  （固定翼 / 悬停）、武器（机炮导弹 / 炮击 / 放无人机 / 自爆装药）、姿态骨骼（升降舵 / 推力舱）；`kLaunchRows` 把 `JetRole` 映射到机体，
  三张表都有 `static_assert` 检查顺序与一致性。
- 条目身份 = `ObjRef`（地址 + weak-this 控制块），条目存在期间持有控制块的一个弱引用（与 `booster.cpp` 相同的 MSVC `_Ref_count_base`
  布局：use +8、weak +0xC），所以对象被销毁看 use count，控制块地址不会被别的对象复用。条目只在喷气机被删 / 销毁 / 击落时回收
  （`JetReap` 每帧一次），不再因为「1.5 秒没被飞」被抢；表满时新飞机不接管（限频日志）。
- 关卡开始（`MissionStart` → `ResetJets` / `ResetAirstrikes` / `ResetBoosters`）：上一关的条目、人偶、学到的墙、被接管轰炸机、
  推力舱火焰全部忘掉，不调用游戏的析构 / Delete，不放弱引用（宁可泄漏）。
- `JetPilot` 热改成 0：仍在飞的喷气机在下一帧被删除（不再挂着假驾驶员悬停）。推力舱火焰的清理挂在每帧回收入口上。
- 学到的墙：只在撞点两侧 400 m 内有效，120 s 没再撞到就忘掉；四面世界墙单独一组，不会被学到的墙覆盖。
- 子弹穿僚机的 addBody 钩子只读游戏线程每帧（及条目增减时）发布的编队快照（SRW 锁保护），不读条目表、不用游戏时钟。
  依据：`docs/bullet-pass-re.md` 说批处理在主循环里（M），没证实与载具更新同线程，所以按跨线程处理。
- 巡逻圈半径、母舰绕圈方向按编队内序号（`Jet::wing`），不再按条目下标。

### 9.1 撞击伤害（`ImpactDamage`，`src/jet_bay.cpp`）

- 自爆无人机的装药是挂在无人机 2 号武器位上的 GrenadeBullet01（`EDF6VC_BLAST_CHARGE.SGO`），伤害和半径写在武器 SGO 里，
  而且必须由带这把武器的载具开火；玩家喷气机没有这把武器，运行时也没有已逆向的改写入口（`docs/decoy-blast-re.md` §1.3 说
  直接调 `ApplyAreaDamage` 要伪造 GameDamageInfo，不可取）。
- 所以撞击伤害走炮舰炮弹同一条已逆向的路：DemoIndirectFire（`tools/make_jets.py` 的 `EDF6VC_IMPACT_08/16/32/64.SGO`，2026-10-06 追加 02/04/12，由原版
  `DEMOGUNSHIPFIREE25` 改成 1 发（#2）、无间隔（#3）、无等待（#15）、子弹类 GrenadeBullet01（#4，CustomParameter #13 = [1,0,1,0,0,0]：
  到期必爆、无重力、不反弹、无随机寿命）、速度 0.25 m/帧（#5）、无重力（#6）、不穿透（#11）、2 帧寿命（#10）、爆炸半径 8/16/32/64（及 2/4/12）m（#9 AmmoExplosion），
  下标见 `docs/carrier-laser-re.md` §3）。插件按请求半径选比例上最接近的一档（|ln(档位/半径)| 最小，相同时取小的；`src/vehicleram.h` `ram::NearestCharge`；2026-10-06 前是「不小于它的最小一档」，25 m 的机体因此炸 32 m），旧安装缺新档位时取已装的最接近一档并记日志，
  归属设为撞击者（IFC 每步从归属者 +0x314 取队伍：击杀算撞击者的，只伤敌对方——H），伤害由插件写 `+0xDC`，从撞击点上方 0.5 m
  沿直线（IFC +0x2F8 = 0）朝撞击点打下去，2 帧后在撞击点爆开（碰到东西则提前爆）。没预载（没装或本关没预载）时返回 false 并限频记日志。
- 限制：半径只能按档位。
- 未验证（需实机）：爆炸特效与音效（沿用炮舰炮弹的开火音效 #17）、伤害数值是否被难度系数再乘。


## 模型落地（2026-10-04）

游戏生成载具时，会把载具的原点放在地面上；原版 506 直升机的碰撞箱底部正好在原点（0～2.9 m）。我们的喷气机模型原来有一截在原点下方：
- 截击机外形 0.83 m，舵面轰炸机 1.29 m，无人机 1.51 m，空中母舰 3.5 m。
- 碰撞箱按模型量出来后，也跟着伸到原点下方。
- 结果：停放的玩家飞机一生成就插在地形里，从地形网格里穿了下去。

修法：`pylib/jet_models.py` 的 `grounded`。
- 生成模型时，把整个模型抬高到最低顶点正好在原点：
  - 根骨骼局部平移 y 加 d。
  - 每根骨骼的逆绑定矩阵变为 T(−d)·inv。
  - 蒙皮网格（模型空间）顶点 y 加 d；刚性网格（骨骼局部空间）跟着骨骼走，不改。
- 生成时自检：包围盒正好上移 d，每根骨骼的「世界 × 逆绑定」不变。
- 碰撞箱都从抬高后的模型量（`model_box` / `fuselage_box`），不再手写；`vcobjects.on_origin` 兜底，确保碰撞箱底部不低于原点。
- 潜艇母舰（`grounded=False`）不抬。


## 起落架（2026-10-05）

用户要求：「给飞机加上起落架，可以收起和展开」「飞机应该有的 hud 和组件，例如起落架」。

### 模型（`pylib/jet_gear.py`，H：离线生成并自检；外观未在游戏里看过）

- 供体：`VEHICLE410_HELI.MRAB` 的 `frontWheelSus_l`（+x）/ `frontWheelSus_r`（−x）：减震支柱 + 摇臂 + 一个机轮，各蒙皮到一根骨骼，材质 `MaterialLibrary.helicopter6`（`helicopter_body_*` 贴图）。主起落架各用一条；前起落架把两条并排放（双轮）。
- 每条腿整体复制、等比缩放、平移（不旋转，法线 / 切线不用改），使：
  - 机轮落在同一个「地面」：模型最低点再往下 `drop`（源模型米）；
  - 支柱进入供体机身的那条线（供体 `body` 在支柱顶上方的下表面）落在喷气机下表面上（前起落架再往里 `nose_inset`，给收起留长度）。
- 新骨骼 `gear_nose`、`gear_main_l`（+x，原版载具的 `_l` 约定）、`gear_main_r`，都是机身骨骼（bomber501 / bomber401）的子骨骼，插在它原有子骨骼之后（先序不变），kind 3、bounded 1。骨骼原点在支柱顶，局部 Y = 模型上方，局部 Z = 收起方向的反方向；**绑定姿态 = 放下**。
- 收起 = 绕局部 X 转 `LEG_UP`：前起落架向后 1.578 rad、主起落架向内 1.821 rad——供体腿本身是斜的（向外 14.3°、向前 0.4°），这个角度让腿轴正好放平；所有模型都等比缩放同一供体，所以角度相同。`src/gear.cpp kLegUp` 与之相等（selftest `gear_legs_as_the_models_fold_them`）。
- 舱门：原版的轮舱盖（Vehicle409 `tailWheelCover_l/_r`）是 V 形的曲面壳，放在喷气机平底下要么挂成一个整流罩，要么关上时穿过收起的机轮，所以没有做。
- 放置（`SPECS`，源模型米）：
  - bomber501：前起落架 z 4（机身此处 2.5 m 厚；z 10 只有 1.8 m，放不下和主起落架一样长的腿），双支柱 ±0.3；主起落架 x ±4、z −4；`drop` 1.0；前起落架 `nose_inset` 0.9。
  - bomber401（先用 `skin_rigid` 把刚性网格改成和舵面轰炸机一样的蒙皮骨架）：前 z 5.5、主 x ±4、z 0；`drop` 1.0；`nose_inset` 1.2。
- 生成时的自检（`check_gear`，`jet_models.build` / `elevon_archive` 都会跑）：每根骨骼「世界 × 逆绑定」= 单位阵；三条腿的最低点就是模型最低点、在同一高度；机身离这个平面至少 0.1 m×缩放；收起后每条腿的轴水平（±5°）、方向对（前向后、主向内），收起后的所有顶点离机身下表面 / 上表面不超过腿高的 5%。`jet_models.check` 另外对比：原版几何的包围盒 = 源 × 缩放 + 抬升，整个模型的包围盒 = 带起落架的未缩放模型 × 缩放 + 抬升，新增的档案成员恰好是供体贴图（字节与供体相同）。
- 落地：`grounded` 把机轮接地点抬到原点，所以模型整体比以前高了起落架的高度（舵面轰炸机 +1.0 m、截击机 +0.65 m、多用途机 +0.4365 m），尾焰位置（`NOZZLES` / `kJetNozzles`）同步上移。碰撞箱照旧从模型量（玩家机 `model_box`、NPC `fuselage_box`），**箱底 = 机轮接地点 = 原点**。

离线生成结果（`python pylib/jet_models.py <wt>/tmp/jetmodels`，2026-10-05，单位：落地后模型米）：

| 模型 | 碰撞箱底 | 前轮接地 x / z | 主轮接地 x / z | 腿高 前 / 主 | 机身离地 | 收起后露出 |
|---|---|---|---|---|---|---|
| EDF6VC_JET（舵面 bomber501，×1） | 0.0（玩家、NPC 箱都是） | ±1.23 / 4.0 | ±4.63…4.76 / −4.0 | 2.89 / 2.36 | 1.0 | 前轮在下表面下 0.057，主 0 |
| EDF6VC_INTERCEPTOR（×0.65） | 0.0 | ±0.80 / 2.6 | ±3.01…3.09 / −2.6 | 1.88 / 1.53 | 0.65 | 前 0.037，主 0 |
| EDF6VC_MULTIROLE（bomber401 ×0.5） | 0.0 | ±0.63 / 2.75 | ±2.33…2.40 / 0.0 | 1.51 / 1.23 | 0.5 | 0 |

代价：每个带起落架的档案多了供体的 4 张 HD 贴图和低清版（约 10 MB），三个档案共约 30 MB。

### 插件（`src/gear.cpp`，M：照搬舵面的写骨骼方式；未在游戏里跑过）

- 每帧把每条腿的局部矩阵写成 `Rx(at × kLegUp) × bind`（`at` 0 放下 … 1 收起），和舵面一样；模型里找不到三根骨骼就什么都不写。
- 顺序：收起时前起落架先动、主起落架 0.6 / 0.9 s 后跟上；放下时主起落架先动；每条腿 2.5 s。
- 玩家：`PlayerJetGearKey`（G）/ `PlayerJetGearButton`（0x40 = L3）切换；地面（不在 `Phase::air`）上不能收起。放下时阻力加干净构型寄生阻力的 1.5 倍；`Touch` 时没放下锁好 = 机腹迫降（`Crash` 的伤害，然后 `Phase::rolling`，`Ground` 里 10 m/s² 减速、无推力、无转向、不能起飞）。登机时在地面 = 放下，接机的飞机飞来时收起。
- NPC：`jet_flight.cpp Wing` 里每帧 `NpcGear`：低于 60 m 且慢于 70 m/s 放下，否则收起（第一次见到直接到位）。
- 座舱：`hud.cpp GearPanel`（在 `Cockpit` 之后一行调用），数据经 `GearHudLatest`（读写锁保护的一份拷贝）从游戏线程传到绘制线程。

### 需要实机确认

1. 三条腿是否画出来、贴图对不对（供体材质的着色器 `snd_BRDF_Common_SeparateOcc` 与机身的 `snd_Mech` 混在一个对象里）。
2. 停放 / 生成时机轮是否正好接地（不悬空、不陷地）；箱底的数值在上表。
3. 收放动画方向、顺序是否正确，收起后是否藏进机身（尤其前起落架那 4–6 cm）。
4. 前起落架位置是否可以接受（舵面轰炸机在机身中段偏前，因为机头太薄）。
5. L3 在直升机座位里是否被游戏另作他用；G 键是否与游戏的键位冲突。
6. 机腹迫降：伤害、滑停是否正常；迫降后机身停在机轮高度（碰撞箱不随起落架变）。
7. NPC 飞机起飞 / 降落时是否放下（目前 NPC 很少落地）。
8. 尾焰位置在新高度上是否仍对准喷口。
9. 直接用原版 BOMBER401 / BOMBER501_2 档案的接管机（`EDF6VC_BOMBER401/501_2.SGO`、炮舰机）没有起落架；它们和舵面轰炸机共用标记 7001 的尾焰表，原来就有高度差，现在差得更多。
