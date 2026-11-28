# 子弹扫掠在组展开处按包围盒剔除部件（ExpandCull）逆向笔记

对应代码：`src/bullet_expand_cull.cpp` / `src/bullet_expand_cull_logic.h`，ini `ExpandCull`（默认 1，每次投射经 `Cfg()` 读取，支持热重载）。
只支持 Steam 版 `EDF.dll`（TimeDateStamp `0x678CCB46`）。以下 RVA 都是 EDF.dll 的地址。

## 背景

- bench:4000 时原版 pre_update 每步约 72–76 ms，大部分花在子弹扫掠上。
- 已经证伪、不再做的方向：同组去重、在源头按层或阵营剔除、缩小扫掠盒。
- 消费侧剔除（旧 SweepCull，在 `0x2382B6` 包 `0x231720`）实机更慢，已删除。r4 同局 ABBA 的数据：
  - bench:2000 pre_update 20.7 / 14.9 ms，bench:4000 86.0 / 75.6 ms。
  - 4000 敌时每 5 s 有 21380 次投射、5729 万候选部件，剔除 93.6%，但剔除本身每帧 81 ms，因为每个候选都要调一次 calcAabb。
  - 窄相 `0x11A8E40` 本来就在 Havok 工作线程上跑（hknpCollisionQueryTask），主线程省不到。
- 现在的做法（方案 A）：在组展开时就不追加与扫掠盒不相交的部件。这样展开、追加、`0x1079D0` 拆表、命令构建、窄相都一起变少；每个 body 的盒子每步只算一次，之后查缓存。

## 调用路径

- 批处理 step `0x237CF0`。它唯一的调用点是 `0x704ED4`（在 `0x704EB0` 里）。
  - step 1 `0x233BD0` 在 `0x234334`（移动类型 1）、`0x234982`（2）、`0x234B98`（3）这三处调 broadphase 扫掠 `0x11A70B0(world rdx, …)`。
  - 扫掠盒：`0x11AA760` 的 `this+0x30..+0x48`，包住子弹射线 `0x8A0 → 0x8B0` 和 step-3 球（半径 `0xBE0`）。
- broadphase 对每个重叠的 body 调 collector 的 `hit_query::NpTargetCollector::addBody` `0x109DA0(this, body id)`。
  - 对组 body，`0x109F6F..0x109FED` 遍历组内的全部部件，逐个经 `0x11A9EC0` 追加 id 到 collector 的列表 `0x818` / `0x820`。
  - 原版这里不做任何盒子检查，敌人越密，每次投射的候选就越多（实测每次约 2680 个）。
- `0x2359C0`（step 1 的过滤）：
  - 只看 `0x820` 的个数；为空时 `b80 = to`，不投射。
  - 不空时由 `0x1079D0` 把 `0x818` 拆进 `0x8C8`（射线）/ `0x8D8`（球）。
  - `0x1079D0` 对每个 id 先经 `System+0x68`（即 `world+0x58`）调 reader slot 0x68 isValid，再调 slot 0x80 取 body。
- `0x231720`（step-3 命令构建，`0x2382B6` 是唯一调用点）：
  - 总是先 reset collector（vt+0x10）。
  - 爆炸弹，或 `8E0 == 0` 时，直接返回 0。
  - 否则调 `0x11AA620` 取 body，交给 `0x11A8E40` 做窄相。
- 命中读取：`0x2321F8` 经 collector 的 slot 3（+0x18）拿命中数组，个数在 `+0xC`，每条 0x70 字节。

## reader 与 body 盒子

- reader 是 `world(rdx)+0x58 → hknpWorld`，再取 `+0x20`，vtable `0x18BC9C0`：
  - slot 0x68 = `0xDAC200` isValid：检查 index 在范围内、`flags & 3`、`+0x50 == id`，返回 int 0 / 1。
  - slot 0x80 = `0xDAB170` getBody：`bodies + (id & 0xFFFFFF) * 0xB0`。
- 盒子：shape 取 `body+0x78`，calcAabb 是 `[*(image+0x20F1930) + shape[0x18]*0x100 + 0x18](shape, body, out[8])`，和窄相用的是同一个 shape、同一个变换。
- 没用 hknpBody 自带的 `+0x40` 盒子（broadphase 用的那份），原因有三：
  - 它是量化成 int16 的 hkAabb16，换回世界坐标需要 broadphase 的量化偏移和缩放，这个换算的位置没找到；
  - 它可能落后于当前变换（按 Havok 惯例这份盒子在 step 末才更新，属于推测）；
  - 如果按它剔除，就得证明它总是包住 calcAabb 的结果，这一点证明不了。
- 改为每线程一份缓存：
  - 2^17 个 32 字节的槽，按 `id & 0xFFFFFF` 直接映射，用 `VirtualAlloc` 分配。槽里存 `lo[3], id, hi[3], gen`。
  - 命中路径是内联的（`Slot` / `Current`），没命中时走 noinline 的 `FillBox`。
  - 代数 `gen` 由 `0x704ED4` 的 step 包装每步原子加一（跳过 0），所以盒子最多只在一步内复用；同一步里 body 不动。
  - 取不到 body / shape / 表，或得到的盒子不是有限值时，记为「无界」，这个部件保留。

## 剔除判定（Sweep）

- e = `max|extent| + 0.1`。
- 扫掠体取线段 `from → to` 被半边长 e 的立方体扫过的范围。它包含 broadphase 扫的盒子、射线，以及 step-3 的球。
- `Touches` 依次做三步检查：
  1. float 粗检：对比 `flo` / `fhi`，这两个是 double 盒子用 nextafter 向外取整后的 float，所以只会多留，不会多剔；
  2. double AABB 重叠检查；
  3. slab 测试：倒数在每次投射时算一次，某一轴退化时跳过该轴（这时第 2 步已经覆盖了它）。
- 扫掠的输入不是有限值时，这次投射整次不剔。

## 挂钩

- `0x109F6F` 换成跳到 walk cave。cave 调 `Expand(collector rbp, group rdi, id esi)`，然后跳到 `0x109FED` 的尾声。cave 有自己的 unwind 信息，经 `RtlAddFunctionTable` 注册，异常能正常穿过。
- `Expand` 只在当前线程处于一次 cut 投射中时才剔除，其余情况按原版逐个追加。
- 三处投射调用点包成 `BulletCast`：
  - 设置线程局部的 Cast（collector、Bodies、Sweep、cut 标志）；
  - 嵌套投射时保存并恢复外层的 Cast；
  - Scope 析构时恢复 Cast 并 Flush 统计。
- 以下情况按原版走，不剔除：
  - 带 functor 的投射；
  - 不在投射里的展开；
  - `ExpandCull=0`；
  - 爆炸弹（`AF4 & 0x1000`，core = collector − 0x800）；
  - 取不到 world 或 reader；
  - shadow 抽样。
- 爆炸弹不剔除的原因：`0x2321B0` 的爆炸经 `0x542860` 直接拿 `8D8` 做点查询，点查询的半径不受扫掠盒约束（没有证明它在扫掠盒内）。
- 保留一个：如果一个组一个部件都没留下，但它至少有 1 个有效部件，而且 collector 的个数为 0，就追加它的第一个有效部件。

## 等价性论证

1. 剔除后的列表是原版列表的有序子序列。`Expand` 按原版顺序遍历，只少追加一些部件。
2. 被剔除的部件，它的 calcAabb 盒子不碰扫掠体（扫掠体已扩 0.1），所以射线和半径 ≤ |extent| 的球都碰不到它，窄相不可能产生命中。
3. 窄相逐个 body 独立处理，过滤器无状态，所以剩下部件的命中不变。命中顺序按 fraction 排，与被剔除的部件无关。这一条是推测，由游戏内的 shadow 对比核验。
4. 空列表：`0x2359C0` 只看 `0x820`。如果剔除后为空，就不投射，`b80 = to`。原版即使投射了那些碰不到的部件，也不会有命中，结果一样都是「无命中」。`0x231720` 在 `8E0 == 0` 时先 reset collector 再返回 0，与原版建了命令但无命中是同一个结果。
   - 保留一个只是为了让 `0x820` 不变空，其实空列表也等价。`0x8C8`（射线列表）同理，这一点是推测。
5. `0x818` / `8C8` / `8D8` 每步重建，剔除的结果不会泄漏到下一步。

## 命中字段与 MISMATCH

- 游戏读的命中字段（`0x2321B0` / `0x230CA0` / `0x238700`）：
  - `+0x00` 位置 xyz；
  - `+0x10` 法线 xyz；
  - `+0x20` fraction；
  - `+0x48` body id。
- `0x230CA0` 会把两个向量整份拷贝（含 w），但没找到读这两个 w 的地方。
- 所以 shadow 只比较 dword `{0,1,2,4,5,6,8,0x12}`。其余 dword 的差异记进 `unreadMask`，写成 `EXPANDCULL UNREAD` 日志行，不算不一致。
- r4 的 32 次 `SWEEPCULL MISMATCH` 都是「同 body、同 fraction」，只是 0x70 字节里别的字段不同。推测原因：`0x11A8A90` 栈上的 BodyInfo 有一个没初始化的 u16 填充（`rsp+0x42`，这一帧只写了 `+0x40` 的 word 和 `+0x44` 的 dword），被整块拷进了命中记录。这个推测要靠实机的 unreadMask 确认。

## 校验与观测

- 安装前检查调用点 / 跳转点的字节，并对以下范围做 FNV-1a 指纹（值见 `kFingerprints`、`kAddBodyFnv`、`kAppendFnv`）：
  - `0x109DA0..0x10A002`（addBody）
  - `0x11A9EC0..0x11A9F30`（列表追加）
  - `0x231720..0x231839`
  - `0x11AA620..0x11AA64C`
  - `0x11A8A90..0x11A8E37`
  - `0x11A8E40..0x11A9261`
  - `0x2359C0..0x235AD5`
  - `0x238286..0x2382E2`
  - `0x233C39..0x233C47`
  - `0x2321F8..0x232240`
  - `0xDAB170..0xDAB183`（getBody）
  - `0xDAC200..0xDAC234`（isValid）
- 任何一项不符就写一行 `EXPANDCULL REFUSED`，什么都不装。
- 重定向的顺序：step → walk → 三处 cast → （仅 Debug）build。失败时全部撤销。
- `PerfProbe` 打开时，每 5 s 输出一行 `PERF EXPANDCULL`，字段有：
  - 投射次数（cut / 爆炸 / 关闭 / shadow 抽样）；
  - cut 遍历次数、部件数、保留数、剔除数及比例、保留一个的次数；
  - 剔除本身的耗时：总 ms、每帧 ms、每部件 ns，用 TSC 计时并以 QPC 校准；其中盒子计算的 ms；
  - 盒子缓存的命中 / 未命中 / 无界次数；
  - 按原版处理的遍历次数（不在投射里 / 被过滤）；
  - shadow 的核对次数、不一致次数、未读字段有差异的次数和 mask。
- `Debug=1` 时，`0x2382B6` 额外挂一个 shadow：抽样的投射用完整列表重跑展开，再分别 `0x231720` + `0x11A8E40` 各跑一次，比较读到的命中字段。不一致时写 `EXPANDCULL MISMATCH`（最多 32 行），这一次换回完整列表的结果。

## 离线测试（`tests/bullet_expand_cull_test.cpp`，ctest `bullet_expand_cull`）

- 几何：40 万组用例，以 L∞ 黄金分割搜索求出的距离为准；另含 NaN 和无界的情况。结果：130780 组碰得到，全部保留；269220 组被剔；没有因舍入多留的。
- 私有映射：在 `DONT_RESOLVE_DLL_REFERENCES` 映射上安装，全部指纹匹配，重定向到位；非 Debug 时 build 调用点不改。
- 随机世界：伪造 world、reader（slot 0x68 / 0x80 / 0xE8）、AABB 表、collector，跑 2400 次投射，与原版 addBody 逐次比较：
  - 投射分布：964 次 cut、299 次嵌套、295 次不在投射里、268 次带 functor、294 次 `ExpandCull=0`、280 次爆炸弹。
  - cut 的投射是原版列表的有序子序列，剔掉的都碰不到，「是否为空」与原版一致。cut 列表共 446840 个 id，剔掉 82.3%。
  - 其余几类与原版完全相同。
- 另外覆盖了保留一个，以及异常穿过 cave（及上一层栈帧）和 cast 的 Scope。
- 耗时：
  - 400 敌、部件紧凑：每部件比原版遍历多 2.3–2.8 ns，剔除 99.3%。
  - 4000 敌、部件分散：每部件多 5–13 ns，波动大。
  - 只算判定（查缓存的盒子并检查），16000 个 body 的盒子（在缓存里）时，按组顺序 2.2 ns，随机顺序 3.1 ns。
  - 160000 个 body（缓存 5 MB）时，按组顺序 6.5 ns，随机顺序 11.6 ns，受内存带宽限制。测试只对 400 敌断言 < 5 ns。
- 离线测试测不到的部分：
  - 离线的「原版」只包含追加。游戏里下游的 `0x1079D0` 拆表、命令构建、工作线程上的窄相都没模拟，真实收益只能用 perfrun 测。
  - 真实窄相 `0x11A8E40` 要 Havok 的 TLS 内存路由和栈分配器、任务队列、真实 hknp shape，离线跑不起来。命中等价只能靠游戏内 `Debug=1` 的 shadow 来证明。
