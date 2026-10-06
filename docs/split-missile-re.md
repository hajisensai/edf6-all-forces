# 分裂导弹（MissileBullet02）分裂判定：逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，下文地址均为 RVA。全部来自静态分析（`tools/edfre.py` 的 `vtable_of` / `xrefs` / `callers` / `func_start` / `find_disp` 与 capstone 反汇编），**游戏未运行、未实测**。
置信度：H = 直接读代码得出；M = 强推断；L = 需实机核对。实现见 `src/splitmissile.cpp`、`src/split_fuse.h`，离线测试 `tests/split_fuse_test.cpp`。

用户反馈（2026-10-06，原版问题）：「血腥风暴这种 missilebullet02 的导弹释放子导弹的判定是根据距离锁定目标的中心（也就是那个唯一的中心骨骼）来的，当遇到特别大的目标如 DLC 的卵的时候，就会在释放子导弹之前就命中卵。」

## 1. 类与对象（H）

| 项 | 值 | 证据 |
|---|---|---|
| vtable | `0x17A1E28`（RTTI `.?AVMissileBullet02@@`） | `vtable_of('MissileBullet02')` |
| 构造 | `0x26AF70` | 唯一引用 vtable 的大函数（`0x26AFEE`） |
| update（第 5 槽） | `0x26E4C0` | vtable 第 5 项 |
| 绘制 / 第 3 槽 | `0x26E260` | 只做矩阵与特效 |
| 与 MissileBullet01 共用 | 同一 BulletBase / core（B+0x140），锁定条目 `B+0xB10`（控制块 `B+0xB18`），位置 `B+0x90`，朝向 `B+0x80` | `docs/missile-re.md` |

构造里读 Ammo_CustomParameter 的后几项（`0x26C5F8..0x26C7DE`，变体访问表 `0x179EAF0`，下标写在 `[rsp+0x78]` / `[rbp-0x30]` / ...）：

| CP 下标 | 存放 | 用途 |
|---|---|---|
| 12 | `B+0x1560` | **分裂条件** `[模式, 参数1, 参数2]` |
| 13 | `B+0x1578` | 子导弹发射方向 `[方向模式, ?, 散布锥]`（update `0x26E69C..0x26E88F`） |
| 14 | `B+0x1590` | 存一份（未逐项追） |
| 15 | 交给 `0x2B5F40(B+0x15B0, CP[15], seed)` | 子导弹的「发弹单元」IndirectFireUnit（`docs/airstrike-re.md` §3）：子弹类、数量、间隔…… |

另外 `0x26C82E`：`0x26CD50(B+0x15B0+0xA0, B+0xB10)` 把导弹的锁定条目（弱引用）交给发弹单元，子导弹打同一个锁定点（H）。

## 2. 状态机（H）

`B+0x14A0` 是一个状态对象，切换函数 `0x26EB40(state, {fn, 0}, 0)`：旧状态以 edx=2 退出、新状态以 edx=0 进入；每帧 update 在 `0x26E59F` 以 edx=1 调当前状态。三个状态函数，签名 `(B, int phase)`：

1. **点火前** `0x26EDF0`（构造 `0x26C87E` 装入）：帧数 `B+0x13F0` 未到 CP[7][0] 时继承速度衰减；到了 → 切到 2。
2. **飞行** `0x26EC10`：进入时开尾焰；每帧先按导引类型 CP[0]（`B+0x1380`：1 → `0x26D4B0`，2 → `0x26D8D0`，0 直飞）转向，
   然后 **`0x26ED9C call 0x26CF00(B, 点火后帧数)`** —— 分裂判定，返回真 → `0x26EDAD` 切到 3。
3. **分裂** `0x26EBE0`：进入时 `jmp 0x2B4340(B+0x15B0)` 启动发弹单元；之后每帧，发弹单元剩余数 `B+0x18A0`（单元 +0x2F0）为 0 时 `0x2318E0(core)` 让母弹消失。
   子导弹由 update `0x26E68F..0x26E9F5` 发射：`B+0x187C`（单元 +0x2CC，激活位）非 0 时按 CP[13] 算方向、`call 0x2B95A0(单元, dt)` 发射；是否同一帧循环发完由单元参数决定（`0x26E9E4..0x26E9F5`，未逐项追，M）。
   **这段在 `test byte [rsi+0xC34],1`（core+0xAF4 bit0 死亡）之后，母弹已死就整段跳过（`0x26E63B..0x26E689`）。**

`0x26CF00` 的调用者只有 `0x26ED9C` 一处（`callers(0x26CF00)`），`0x26EC10` 只由 `0x26EE6F` 的 lea 装入（H）。

## 3. 分裂判定 `0x26CF00(B, frames)`（H）

```
mode = CP[12][0]                                   ; 0x26CF40..0x26CFBE（变体转 int）
if mode == 1: return frames >= CP[12][1]           ; 0x26CFF4..0x26D07E 按时间分裂（地狱风暴 HHELLSTORM01：[1, 120]）
if mode != 0: return false
lock = weak(B+0xB10, ctrl B+0xB18)                 ; 0x26D08F..0x26D174 控制块 +8 use count 加锁
if !lock || !lock+0x29: return false               ; 0x26D0C7
r     = lock+0x10 - B+0x90                         ; 0x26D0D1..0x26D0D8 锁定点（目标的中心骨骼）减导弹位置
cosA  = clamp(dot(B+0x80, r/|r|), -1, 1)           ; 0x26D1B6..0x26D1E9（常量 0x1C369CC = -1.0，0x1C36994 = 1.0）
angle = acos(cosA)                                 ; 0x26D1ED call 0x12DA8A6
if CP[12][1]^2 > |r|^2 && CP[12][2] > angle: true  ; 0x26D233..0x26D2BE（两次 0x23D410 变体转 float）
```

所以「距离」就是导弹到锁定点的直线距离，**和目标的体积无关**。锁定点由目标自己每帧更新（`docs/stores-re.md` §7：锁定条目属于目标，`+0x08` 目标对象，`+0x10` 锁定点），对单骨骼锁定的目标就是它的中心骨骼。

## 4. 原版武器数据（Root.cpk WEAPON/*.SGO，`pylib/dsgo.py` 读出，H）

AmmoClass = MissileBullet02 的共 7 个：

| 文件 | 武器 | CP[12] | CP[6] 极速（米/帧） | CP[15] 子弹（第 2 项按数量理解，M） |
|---|---|---|---|---|
| HCLUSTERMISSILE01 | 血腥风暴 | [0, 50, 0.75] | 1.5 | RocketBullet01 ×20 |
| HWEAPON157 / 163 / 173、MPACK_A_WEAPON051 | M2 / M3 / MV / M4 血腥风暴 | [0, 70, 0.75] | 1.5 | RocketBullet01 ×10 |
| V_402ROCKET_ROCKETCANNON02 | 载具火箭 | [0, 70, 1.0] | — | — |
| HHELLSTORM01 | 地狱风暴 | [1, 120]（按时间） | — | — |

AmmoSize 3（命中半径 3 米）。导弹最快 1.5 米/帧，到 70 米分裂线之后还要飞约 45 帧才会碰到中心。
**目标表面离中心超过 50～70 米（DLC 的卵）时，导弹先撞上表面：core 死亡 → 分裂状态从未进入（或进入的同一帧被撞死，发射段被 §2 的死亡检查跳过），子导弹一发不出，只剩母弹的小爆炸。** 这就是反馈的根因（H 代码路径 / M「卵的半径 > 70 米」，卵的类与碰撞体没逐个查）。

## 5. 联机一致性（H 代码 / M 结论）

- `0x26CF00`、飞行状态 `0x26EC10`、分裂状态 `0x26EBE0` 里没有任何网络所有权判断（不读 NetworkObject `+0x120+8` 的 bit0/bit1，不调 `0x630DF0` 一类）（H）。
- 从 `0x2B95A0` / `0x2B4340` 往下 3 层（`0x22E000..0x240000`、`0x2B0000..0x2C0000` 内）没有调用网络注册族 `0x7813A0` / `0x781950` / `0x782880` 或房间判断 `0x7859A0`（脚本扫描，M：更深层未逐一看）。
- 结论：**每台机器各自对自己那份导弹做分裂判定，各自生成子导弹**（M）。原版本身就是各算各的，位置有细微差异时两边分裂时刻本来就可能差一两帧。
- 插件的影响：装了插件且开着 `SplitMissileSurface` 的机器按表面距离分裂，没装 / 关掉的机器按原版。对卵这类大目标，两边会一边放出子导弹、一边直接撞上；子导弹伤害由谁结算没有追（L）。**建议同房间的玩家都装同一版本并保持同一设置**；ini 注释和 README 已写明。

## 6. 修法（`src/splitmissile.cpp`）

不改判定公式本身，只把「距离」换成到目标表面的距离：

1. **重定向唯一调用点** `0x26ED9C` 的 `call 0x26CF00`（`RedirectCall`，只在它仍指向 `0x26CF00` 时写入）。只有 MissileBullet02 的飞行状态走这里，别的弹种不受影响。
2. 钩子里（`__try` 保护）读导弹的锁定：控制块 use count > 0、条目 `+0x29` 有效、目标对象 `+0x08`、锁定点 `+0x10`，导弹位置 `B+0x90`。
3. **只看目标自己的射线**：用游戏的射线封装 `CastRay 0x11A7EE0`（`docs/raycast-re.md`）从导弹位置打到锁定点，filterInfo = `0x0B`（layer 11，子弹查对象用的层，与 {5..9, 14} 碰撞，不打地图，`docs/bullet-pass-re.md` §6）。
   收集器是游戏的 closest-hit 收集器（vtable `0x1768B78`）的副本，第 4 槽 addHit 换成插件的 `TargetAddHit`：命中记录 `+0x48` 的 body id 经 `0x108260` 取对象，**只有等于锁定目标时才交给原版 addHit `0xD93980`**，所以中间挡着的其它敌人、友军都被穿过去，返回的是目标自己最近的表面（命中比例 × 线段长）。
4. **把表面距离交给原版判定**：表面比锁定点近时（`split::Proxy`），把 `B+0x90` 暂时写成「锁定点沿视线退回表面距离」的点，调原版 `0x26CF00`，再写回原位置。视线方向不变，所以原版的角度判定不变；按时间分裂（模式 1）不读位置，也不变。射线没打到目标（锁定点在目标体外、射线打不到）时原样调用原版。
5. ini `SplitMissileSurface`（默认 1）每次调用时读；0 = 原版。启动时校验 10 段签名（`0x26CF00` 序言、调用点、锁定读取与 `+0x90` 减法、CastRay / reset / addHit / `0x108260`）和收集器 vtable 第 0 / 4 槽，任一不符就不装。

副作用：普通目标（半径几米）提前约一个半径分裂（1.5 米/帧下一两帧），子导弹散布起点前移同样距离。

## 7. 验证

- `tests/split_fuse_test.cpp`（CTest `split_fuse`）：直线飞向半径 100 米的「卵」、1.5 米/帧、分裂距离 70、命中半径 3：
  原版规则在分裂前撞上表面（复现反馈）；插件规则在表面距离 69.0 米（第 154 帧）分裂。半径 1.5 米的小目标：原版第 154 帧、插件第 153 帧。
  视线上挡着另一个单位时仍按卵的表面分裂；代理点距离等于表面距离、在同一视线上。
- 变异：`Proxy` 恒返回 false → `FAIL: fixed: the round splits before the egg's surface`；`Counts` 不比目标 → `FAIL: a unit in the way is let through to the target's surface`。
- `tools/selftest.py` `split_missile_wired`：调用点、重定向、ini 默认值、CMake 测试接线。

## 8. 待实机确认

1. 卵（及其它大目标）确实在 layer 11 射线上可被命中，`0x108260(body)` 返回的对象指针与锁定条目 `+0x08` 相同（M：子弹命中同样靠这个映射，`docs/boarding-re.md` §4 第 2 条也待核）。不相同时插件静默退回原版（日志没有 `SPLIT missile` 行）。
2. hknp 射线是否对 addHit 返回的 earlyOut 之外的命中继续调用 addHit（M：closest-hit 收集器本来就靠 addHit 逐个比较）。
3. `Debug=1` 时日志 `SPLIT missile <p>: target surface X m, its centre Y m: splits`：打卵时 X ≈ 70、Y 远大于 70，子导弹在卵外放出。
