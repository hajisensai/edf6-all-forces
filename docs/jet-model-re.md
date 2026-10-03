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
| `animation_from_ragdoll` / `ragdoll_from_animation` 映射里的模型骨骼 | `0x6E6A50`，绑定在 `0x6E7C6C` 附近 | 找不到时打 `RagdollController::BindDependency( ragdoll to an...` 日志，然后**跳过这一项**。ragdoll 照样存在，只是不驱动模型骨骼 | M |
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
7. `ragdoll`：原样保留 `[Ragdoll_v506_heli.shkt, 内嵌 SGO]`（H：slot 61 需要它）。
   - 内嵌映射里的骨骼找不到时只跳过（M）。
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
