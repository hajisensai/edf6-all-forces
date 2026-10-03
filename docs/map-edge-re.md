# 地图边缘（空气墙）逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`，地址都是 RVA。只做了静态分析，没有进游戏验证。
置信度：H = 反汇编直接读出；M = 读法确定、语义靠推断；L = 猜测或未验证。

## 结论

1. 把喷气机挡在约 1.2–1.4 km 处的，**不是 Havok**，而是游戏自己的「移动区域」夹紧：`MoveAreaManager`（单例）里存着一个轴对齐盒子，
   直升机族的第 55 槽基类 `0x6543A0` 每帧把**载具位置**夹进这个盒子（按载具半径 `veh+0xE00` 向内收缩），然后调用 `SetPosition 0x11B1A00` 把刚体**瞬移**回来。
   这个过程**只改位置，不改速度**（H）。
2. 所以插件每帧写 180 m/s 的速度：物理步把机体推出去几米，下一帧的第 55 槽又把它拉回边界，实际位移约等于 0，和实测一致（M，症状吻合）。
3. 测试场地图 `ig_Heigen601` 的移动区域是中心 (0,0,0)、半尺寸 999 的正方形（地图自带的 `move_limit` 矩形）。沿坐标轴约 999 m 处、沿对角线约 1413 m 处会撞墙，
   和「离地图中心 1.2–1.4 km」吻合（M：盒子数据是 H，与实测的对应关系是 M）。
4. 原版轰炸机（`BombingPlane`，更新函数 `0x5AB240`，逻辑是 `pos += vel`）**从不调用**这个夹紧，所以能飞出去（H：`0x5AB240` 及其所在一带都没有引用单例 `0x20B2998` 或 `0x11BD90`）。
5. **推荐做法**：对每架喷气机把 `veh+0xE00` 写成一个大的负数（例如 `-1.0e6f`）。夹紧盒子会因此**向外**扩大 1e6 m，X/Z 永远不会被夹紧；天花板那一步不受影响（插件本来就遵守天花板）。
   不需要新增 hook，也不需要改代码段。全 DLL 里对 `+0xE00` 的浮点访问只有初始化时的一次写入和 4 个夹紧函数的读取（H），所以副作用只落在这台载具的夹紧上。
6. 飞出移动区域后，下一道边界是 **Havok 世界的宽相范围：以原点为中心、每个轴 ±3000 m**（H）。刚体超出宽相后 hknp 怎么处理没有查清（L）。
   插件应在机体到达这个范围之前删除喷气机，建议任一轴 |坐标| > 2700 m 时立即删除。

## 1. MoveAreaManager

### 对象与单例（H）

- 单例指针：`*(image+0x20B2998)`，里面存的是 **对象地址 + 8**（第二个 vtable 的位置）。所有读取方都先减 8：
  - `0x11BD90`：`return p ? p-8 : 0`，这就是「取区域管理器」的 getter；
  - 第 55 槽 `0x654441` 也是同样的写法。
- 创建：`0x7014CD`（在 `0x7004F0` 里）`new(0x50)` 后调用构造函数 `0x5A9810`，把 `obj+8` 存进 `0x20B2998`。
- RTTI：`.?AVMoveAreaManager@@`（单例模板 `.?AV?$Singleton@VMoveAreaManager@@@ut@sgs@@`）。主 vtable `0x17D3970`，第二个 vtable `0x17D39D8`。

### 字段（相对对象起始，H）

| 偏移 | 类型 | 含义 | 构造函数默认值 |
|---|---|---|---|
| `+0x10` | float4 | 盒子最小角 (x, y, z, w) | (-500, **-1000**, -500, 1) |
| `+0x20` | float4 | 盒子最大角 | (500, max(500,500)=500, 500, 1) |
| `+0x30` | 链表头 | 未分析 | — |
| `+0x40` | float | Y 的硬上限（夹紧第 1 位使用） | 10000 |
| `+0x44` | float | 天花板（第 55 槽单独检查） | 300 |

`src/jet.cpp` 里的 `Ceiling()` 读的 `*(*(image+0x20B2998)+0x3C)` 就是 `obj+0x44`，同一个对象（H）。

### 写盒子：`0x5AA5C0(MoveAreaManager* m, const float4 box[2])`（H）

```
box[0] = 中心 c，box[1] = 半尺寸 h
m+0x10 = c - h，然后把 min.y 强制改成 -1000
m+0x20 = c + h，然后 max.y = max(max.y, 500)
```

调用方共有 4 处：

| 调用点 | 来源 | 说明 |
|---|---|---|
| `0x1229DF`（`0x121470`，地图加载，带 `map::sync::weather_resource` 字符串） | 地图自带的形状集 | 在地图的形状列表（`[r13+0x20]`）里找名字等于 `move_limit` 的形状集（`0x1769C78`），再取第一个类型为 `Rectangle`（`0x1762AC8`）的形状。形状数据 `+0x00` 作为中心，`+0x10` 作为半尺寸（`0x1220DE`–`0x12213D`）。没有这个形状集时不调用，盒子保持默认 ±500 |
| `0x11C4A3`（`0x11BDC0`，地图附加参数） | `app:/Map/additional_map_param` | 先用 `0x5A9D80` 把当前盒子读回中心/半尺寸，再按该地图的 `height_limit {min,max}` 改 Y，写回后再把 `obj+0x44 = max.y`（天花板） |
| `0x1BA2A7`（`0x1BA1B0`） | 任务脚本 `void SetMoveArea(string)` | 注册点 `0x1E838E`（紧挨声明字符串 `0x17956F8` 的 `0x1E83AF`）。按名字在任务 RMPA 里找 `Rectangle` 形状 |
| `0x2287A1`（`0x228610`） | 另一份 SetMoveArea 实现 | 找不到时打印 `SetMoveArea( not found : %ls )`；用的是任务点管理器 `*(image+0x20B28A8)+0x10` |

`additional_map_param.dsgo`（Root.cpk 的 MAP 目录）里只有 `ig_terracemountain`、`ig_steepcoast`、`ig_plateau` 三张图有 `height_limit`，值都是 {min −1000, max 500}，**不影响 X/Z**（H）。

### `ig_Heigen601` 的实际盒子（H：数据；M：与实测的对应关系）

地图文件 `MAP/IG_HEIGEN601.MAC`（在 `Chunk02.cpk`，MARC 容器）里，`+0x150` 处有一份大端 RMPA（`\0PMR`）：

- `+0xBC0`：形状集，名字长度 10，名字在 `+0xE4C`（UTF-16BE `move_limit`），含 1 个形状，形状记录在 `+0xBF0`；
- `+0xBF0`：形状类型名长度 9，类型名在 `+0xDF0`（`Rectangle`）；中心 `+0xC20` 为 (0,0,0)，尺寸 `+0xC30` 为 **(999, 200, 999)**（BE float）；
- `+0xBD8`：另一个形状集（5 个日文字符的名字），里面是尺寸 (1750, 200, 1750) 的矩形。夹紧不使用它。

所以这张图的移动区域是 X、Z ∈ [−999, 999]，Y ∈ [−1000, 500]。夹紧时还要按 `veh+0xE00` 向内收缩。

注意：任务脚本可能调用 `SetMoveArea` 覆盖这个盒子。测试场生成的 `MISSION.AC` 没有调用（`testrange/gen.py` 里搜不到）；原版任务里有没有调用没有逐个查。插件可以在运行时直接读 `obj+0x10/+0x20` 打日志，确认实际数值。

## 2. 夹紧本身

### 第 55 槽基类 `0x6543A0`（`VehicleHelicopterBase` 第 55 槽，H）

506 的第 55 槽 `0x61B8F0` 先调它，再尾跳 `0x64FFD0`。同一个函数也被 `0x64C020`（409）和 `0x64E080`（410）调用。

```
0x6543CC  xmm7 = -[veh+0xE00]                    // r，载具半径取负
0x6543E1…0x654437  从刚体取世界矩阵 → veh+0x60..0x9F
0x654441  area = *(image+0x20B2998) - 8;  ceil = area+0x44
loop:
0x654463  if (pos.y > ceil) { pos.y = ceil; changed = 1; goto loop }
0x654478  if (0x5A9E50(0x11BD90(), &pos, r, 3)) { changed = 1; goto loop }
0x65449B  if (changed) { 0x11B1A00(body=[veh+0x1650], &pos); veh+0x90 = pos }
0x65450E  …清空输入块 +0x1540..，再尾跳 vtable+0x1C0（第 56 槽）
```

`0x11B1A00` 是 `world->vtbl[+0x90](world, bodyId, double3 pos, 0)`，也就是 hknp 的 setBodyPosition。**只改位置，速度原样保留**（H）。

### 夹紧盒子：`bool 0x5A9E50(MoveAreaManager* m, float4* pos, float r /*xmm2*/, int mask /*r9d*/)`（H）

```
lo = m+0x10 - r;   hi = m+0x20 + r          // r = -veh+0xE00，所以盒子按半径向内收缩
if (mask & 1) { 把 pos.x 夹进 [lo.x, hi.x]，把 pos.z 夹进 [lo.z, hi.z] }
if (mask & 2) {
    if (pos.y < lo.y)                    pos.y = hi.y     // 原样照录：低于底时写的是 hi.y（对飞机无影响，底是 -1000-r）
    else if (pos.y > min(m+0x40, hi.y))  pos.y = min(m+0x40, hi.y)
}
return 有没有改动
```

全 DLL 共有 17 个调用点，分属玩家、NPC、各类载具等对象。和载具相关的 4 个函数都读 `veh+0xE00` 作半径：`0x645790`、`0x6543A0`、`0x6636E0`、`0x673A60`。
另有 `0x654230` 是 `0x6543A0` 的姿态变体（刚体取自 `this+0xD0`），找不到直接调用方，未分析（L）。

### `veh+0xE00` 是什么（H）

- 唯一写入点：载具初始化 `0x629450` 中的 `0x629D81`，值为各座位定位点到载具中心距离的最大值（`sqrt(max |seat − center|²)`）。
- 全 DLL 对 `+0xE00` 的浮点访问只有这一次写入和上面 4 个夹紧函数里的读取（`find_disp(0xE00)` 按 movss/comiss/maxss/minss/算术指令过滤，共 5 条）。
  在载具代码范围（`0x5F0000`–`0x690000`）里也没有覆盖它的 16 字节向量读写（`+0xDF0..+0xE00`）。
  范围内唯一另一处 `+0xE00` 是 `0x68E94E` 的指针写入，属于别的结构（M）。
- 所以它实际上就是「夹紧边距」。改它只影响这台载具的移动区域夹紧（M：没有排除经由 memcpy 或拷贝构造的整块复制）。

## 3. Havok 宽相（下一道边界）

- 物理世界在 `0x11AD340` 创建（调用方 `0x1193400`）：
  - `0xDD2A80` 构造 `hknpWorldCinfo`（位于栈 `rsp+0x40`）；
  - `0x11AD395`–`0x11AD3A6` 把 `cinfo+0x90 / +0xA0`（宽相 AABB 的最小 / 最大角）改成 **±(3000, 3000, 3000)**，常量在 `0x17BE0E0`；
  - 把 `cinfo+0x70`（重力）设为 (0, −14.7, 0)；
  - `0xDA2020`（hknpWorld 构造）把这两个角拷到 `world+0x4F0 / +0x500`（`0xDA2A5C`–`0xDA2A71`）（H）。
- 宽相是 `hknpSimdTreeBroadPhase`（还有 `sandlot_custom::BroadPhaseConfig`）。DLL 里有 `leavingBroadPhaseBehavior`、`hknpBodyExitedSafeRegionEvent` 的反射 / 事件名，
  但 cinfo 里这一项的偏移、本游戏设成了什么值、刚体出界后会怎样（不处理 / 冻结 / 移除），都**没有查清**（L）。
  旧版 hkp 的 `BROADPHASE_BORDER_*` 那一套是死代码，和 raycast-re.md 第 6 节同理，不相干。
- 因此在 ±3000 以内是安全的（这就是游戏平时的工作范围）。超出以后可能出现：AABB 被裁剪导致碰撞异常、刚体被冻结，最坏是被移除——那样插件手里的 body 句柄会悬空，
  `SetLinearVelocity` 可能写到无效的 body id 上。**不要让喷气机飞出 ±3000**。
- 这道边界离 `ig_Heigen601` 的移动区域边缘约 2 km，和「飞出边界后在视线外删除」的需求不冲突。

## 4. 让单架喷气机放行：几种做法对比

| 做法 | 改什么 | 优点 | 风险 | 推荐 |
|---|---|---|---|---|
| **A. 写 `veh+0xE00 = -1e6f`** | 一个 float，每架喷气机生成或接管时写一次（`JetFrame` 每帧再写一次也无妨） | 不加 hook、不改代码段；`0x6543A0` 原样运行（天花板、输入清零、尾跳第 56 槽都保留）；不再触发 `SetPosition` | 万一还有别的代码按值复制或读取 `+0xE00`（静态没找到）；喷气机被击毁后残骸若是同一个对象，同样不受移动区域约束（会被插件或游戏删除，影响很小） | 首选（M，实测后可升 H） |
| B. 在插件已有的 506 第 55 槽钩子里，调原函数前记下真实位置 P、调完如果被改就 `SetPosition(P)` 还原 | `jet-model-re.md` 末尾的方案 | 不碰任何游戏字段 | 每帧可能多两次 `setBodyPosition`，等于先瞬移再瞬移回来，可能干扰 hknp 的接触缓存，这或许就是 vy ±100 抖动的来源之一（L）；还会一并撤销天花板夹紧 | 备选 |
| C. 把 `0x65448E` 的 `call 0x5A9E50` 改成跳到插件桩：`rdi` 在此处就是 veh（`0x6543C9 mov rdi,rcx`，rdi 是非易失寄存器），是喷气机就返回 `al=0`，否则跳原函数 | 改代码段 5 字节 + 一个 ±2GB 内的跳板 | 精确，只跳过区域夹紧 | 要改代码段并做签名校验；和别的 mod 冲突的概率比改 vtable 大 | 只有 A 不生效时才用 |
| D. 改 `MoveAreaManager` 的盒子（`+0x10/+0x20`）或调 `SetMoveArea` | 全局 | 简单 | **所有**单位（玩家、NPC、载具）都会跟着放开，破坏关卡设计 | 不推荐 |
| E. 把刚体改成 keyframed（`0x11B1960`）/ 改碰撞层 | 刚体 | — | 没用：边界不在 Havok 里，`SetPosition` 照样会把它拉回来 | 不推荐 |
| F. 改宽相常量 `0x17BE0E0` | 全局，世界创建前 | 能把 ±3000 放大 | 世界创建时机、树宽相精度、其他用途（`0x42C7A0` 也引用这个常量）都没查；不需要 | 不推荐 |

### 做法 A 的具体写法

```cpp
constexpr std::size_t kAreaMargin=0xE00;   // VehicleBase: seat radius, only read by the move-area clamp (0x6543A0 etc.)
// on crew/launch of a jet (and harmlessly every JetFrame):
Put<float>(v,kAreaMargin,-1.0e6f);
```

- 效果：`0x5A9E50` 收到的 r = +1e6，盒子变成 [min−1e6, max+1e6]，第 0 位（X/Z）永不触发。
  第 1 位的 Y 上限变成 `min(area+0x40, max.y+1e6) = area+0x40`（默认 10000），实际不会触发。
  天花板 `area+0x44` 仍然生效，插件已经用 `Ceiling()-kCeilingGap` 避开它。
- 用 `-1e6` 而不是 `-INF` 或 NaN：NaN 会让 `comiss` 判断全部失败，结果碰巧也是不夹紧，但不好预测；1e6 加到 ±1000 量级上仍在 float 精度之内。
- 要不要恢复原值：喷气机是插件专用对象，删除时整个对象一起销毁，不需要恢复。如果日后要把普通直升机临时放行，先存原值、放行结束后写回。
- 删除条件（`JetFrame` 撤离分支）：现有的「离玩家 1.6 km」不变，另加一条硬上限：`|x|`、`|z|`、`|y|` 任一 > 2700 时立即删除（避开 ±3000 的宽相边界，留出一帧最多约 7.5 m（450 m/s）以及机体半尺寸约 16 m 的余量）。
  现有的撞墙学习（`Sense` / `NearWall`）对移动区域就不再需要了，但对建筑和别的飞机仍有用，保留。

### 实测验证清单（需要用户进游戏，本次未做）

1. 生成喷气机后打日志：`area=(min,max)` 读 `*(image+0x20B2998)-8` 的 `+0x10/+0x20`，以及 `veh+0xE00` 的原值。确认测试场是 ±999（扣掉半径）。
2. 写入 `-1e6` 后让喷气机朝地图外飞：位置应持续增长，越过 999，`blocked` 不再触发。
3. 观察越过移动区域以后 vy 的 ±100 抖动是否消失。如果仍然抖动，说明抖动另有来源，例如地形外缘的碰撞或插件的 `Sense` 逻辑，与本文无关。
4. 让一架喷气机故意飞到 2700 m 以外（先把删除距离临时调大），确认在 ±3000 附近会发生什么。这一步决定 2700 这个上限能否放宽，有崩溃风险，请先存档。

## 5. 关于 vy ±100 的弹跳（L）

夹紧只改 X/Z（Y 只在超过天花板 / 10000 时改），而且不动速度。每帧在两个位置之间来回瞬移，会让 hknp 每帧重新计算这具刚体的接触和积分，
同时插件的 `Sense` 会把「被挡住」解释成撞墙并改变期望方向。弹跳最可能来自这两者的叠加，但没有静态证据，放开夹紧后再看。

## 附：本次用到的地址速查

| RVA | 是什么 | 置信度 |
|---|---|---|
| `0x20B2998` | `MoveAreaManager` 单例（存 obj+8） | H |
| `0x11BD90` | 取区域管理器（返回 obj） | H |
| `0x5A9810` | MoveAreaManager 构造 | H |
| `0x5A9D80` | 读盒子为中心 / 半尺寸 | H |
| `0x5A9E50` | 夹紧 `bool(m, float4* pos, float r, int mask)` | H |
| `0x5AA5C0` | 写盒子 `(m, {center, half})` | H |
| `0x121470` | 地图加载：`move_limit` 矩形 → 盒子 | H |
| `0x11BDC0` | `additional_map_param` 的 height_limit → Y / 天花板 | H |
| `0x1BA1B0` / `0x228610` | 脚本 `SetMoveArea(string)` | H |
| `0x6543A0` | 直升机基类第 55 槽：天花板 + 区域夹紧 + SetPosition | H |
| `0x629D81` | `veh+0xE00` 唯一写入点（座位半径） | H |
| `0x11AD340` | 创建 hknp 世界；`0x11AD395` 宽相 ±3000（常量 `0x17BE0E0`） | H |
| `0xDD2A80` | hknpWorldCinfo 默认构造 | H |
| `0xDA2020` | hknpWorld 构造；宽相 AABB 存 `world+0x4F0/+0x500` | H |
