# GPU 粒子渲染器（gpufx）逆向笔记

战雷式烟雾第 1 段的落点与实测结论。代码在 `src/gpufx/`；ini 开关为 `GpuFx` / `GpuFxTest` / `GpuFxNative` / `GpuFxProbe`。
地址都是 Steam 版 `EDF.dll`（TimeDateStamp `0x678CCB46`）的 RVA。

## 插入点

| 位置 | 作用 | 置信度 |
|---|---|---|
| `0x11E223D` → `0x11B6340` | 世界视图开始（与 thermal 共用 hook，`gpufx::WorldBegin`） | 高（thermal 已长期使用） |
| `0x11E3AE2` → `0x11B63A0` | 世界视图结束：在这里画自己的粒子（`gpufx::WorldEnd`） | 高（实测画面正确） |
| nativeCtx+8 | `ID3D11DeviceContext*` | 高 |
| view+0 / +4 | 视口序号 / 类型（0 = 相机世界视图） | 高 |
| view+0x20 / +0x60 | V / P，行向量，视线朝 -Z | 高（实测：探针投影的像素与截图中卡片位置一致） |

实测（run 1，测试场，3840×2160）：

- 世界结束时绑定的 RT：`fmt=10`（R16G16B16A16_FLOAT，HDR），单采样，视口 0,0 3840×2160。
- 绑定的 DSV：`fmt=40`（D32_FLOAT），不是只读 DSV。我们先 `CopyResource` 一份 typeless 深度，再当 SRV 读。
- P：near 0.1、far 约 3000，**非反转深度**。P[2][3] = -1（右手坐标）。
- 整段测试里 P 的 [2][0] / [2][1] 都是 0：**没有 TAA 抖动**。
- 第一人称和瞄准镜沿用同一个 view 的 V/P。fov 变化直接反映在 P 里。瞄准镜只在离线读码时确认过，实机没测。

## 原版粒子绘制（ParticleRender）

- vtable `0x17A8F70`；slot1 `0x302420` 为 `Draw(obj, nativeCtx, pass, view)`，只画 pass 2 / 5，并且要求 `obj+0x200 != 0`。
- `Install()` 按字节校验 `0x302420` 的序言后改写 `0x17A8F78`。扫描 `0x1700000..0x1A00000`，没有找到对这个函数的其他引用（日志：`0 other references`）。
- 对象字段（相对 obj）：

  | 字段 | 偏移 |
  |---|---|
  | blend | +0x84 |
  | stretch | +0x180 |
  | 帧数 | +0x1D8 |
  | 存活数 | +0x200 |
  | 快照数组 | +0x218 |
  | 快照条数 | +0x228 |
  | 纹理 | +0x230 |
  | 纹理 B | +0x240 |
  | 世界矩阵 | +0x2B0 |

- 快照条目 0x40 字节：
  - 位置 vec4；
  - rgba +0x10；
  - 旋转 +0x30；
  - 大小 +0x34；
  - 帧号（byte）+0x38。
- 纹理链：`obj+0x240 → +0x10` 就是绘制时 PS t0 绑定的 SRV（链 1，实测确认）。
- 实测原版绘制状态（测试场的 24 次 dump，全部 blend=1、stretch=0、frames=1）：
  - 混合：`SRC_ALPHA / INV_SRC_ALPHA`（**直通 alpha，不是预乘**），写全部 4 个通道。
  - 深度：开测试、不写，LESS_EQUAL；DSV 可写（没有绑只读 DSV）。
  - PS 的 SRV：
    - t0：粒子纹理（BC7 512²）；
    - t1 / t2：小尺寸纹理；
    - t3：BC6H 立方体 512（环境光 IBL）；
    - t4：R32_FLOAT 2048²，带 RTV 绑定。
  - 结论：**原版粒子 PS 是带光照的**（环境立方体）。t4 尺寸和屏幕不同，更像阴影图，不像软粒子深度：原版是否有软粒子仍未确认。
- 接管条件（`GpuFxNative=1`）：
  - 在主世界视图内，且 `view == 当前视图`；
  - stretch == 0；
  - blend 为 1 或 2；
  - 纹理链已确认。

  其余情况一律走原版绘制。

## 灯光块

`*(EDF+0x20B2958)` + 0xB00..0xC40（白天测试场的取值）：

| 偏移 | 取值 | 推测 |
|---|---|---|
| +B40 | (0.633, -0.651, -0.418) | 平行光的传播方向（朝下） |
| +B50 | (0.808, 0.804, 0.798) | 平行光颜色 |
| +B60 | (0.193, 0.216, 0.207) | 环境光 |
| +B80 | (0.326, 0.327, 0.341) | 天空色？ |
| +B90 | (-0.633, 0.651, 0.418) | +B40 取反 |
| +C30 | (1000, 21000, 0.3) | 雾的起止距离？ |

`gpufx::ReadLight` 每帧读 +B40 / +B50 / +B60。第 4 段实机核对了夜间和洞窟（nc4 接管 / nc5 原版，同 DLL t15）：

- 夜间：+B40 方向 (-0.308,-0.521,-0.796)，颜色 (0.187,0.365,0.354)，环境 0.09；304 个 view 抓到光照块，不完整 0，曝光 1.27。原版与接管的烟亮度一致（对照图 `tmp/cmp/nc-night.png`）。
- 洞窟：方向 (-0.19,-0.96,-0.19)，强度 0.238，环境 0.22，曝光 0.954；场景里没有原版带光照粒子，光照块抓取 0 次属正常。玩家头部附近的烟裁图逐帧一致（`tmp/cmp/nc-cave.png`、`tmp/cmp/nc-cave-head.png`），整图个别行的烟色差别来自两次运行的时机不同。

## 性能（run 1，3840×2160）

- 稳态：自己的粒子约 90 张，CPU 0.03–0.05 ms，GPU 0.03–0.46 ms（GPU 时间戳）。
- 首帧 CPU 36 ms，用于编译着色器和建资源，只发生一次。

## 接管实测（run 2，`GpuFxNative=1`）

- 60 秒测试场里原版共绘制 16971 次，接管 16970 次（第一次绘制时纹理链尚未确认，那次走了原版），拒绝 0 次，没有崩溃。
- 每帧约 7–10 个批次、约 190 张卡片。CPU 0.03 ms，GPU 0.03–0.49 ms。
- 当时这张图里看不到可辨认的原版烟，一致性留到第 2 段验证（见下文「同卡片对照」）。

## 同卡片对照（`GpuFxNative=2`）

- 固定时刻的开 / 关两次运行对不齐（加载、提示画面的时长每次不同）。`GpuFxNative=2` 按卡片中心在视图空间的左右分给两边：左半原版、右半我方，中线两侧是同一片烟的两半，看接缝即可。只供调试。
- 曾有的 `GpuFxNative=3`（同一批卡片原版、我方各画一遍，用剪裁矩形分左右半屏，`StockScissor` 借当前光栅状态加 ScissorEnable）已删除：第 4 段实机里原版那一半一张都不出（左半屏没有任何原版粒子），原因没查清，继续用它会把「原版没画」误读成「两边不一致」。按卡片中心分屏不改原版的任何状态。
- `perfrun blast`（3840×2160，每 3 s 截图）：中线两侧的尘柱、烟团、火花亮度与大小一致，看不到接缝，没有缩小或错位。说明快照 `+0x34` 就是世界尺寸（0.01 是火花一类的小粒子），atlas UV / 角点映射（`cornerScale 0.7071`、`rot - π/4`、格数 `ceil(sqrt(frames))`）与原版一致。
- 早先看到的粉色斜边：集 B 原版在 VS 里逐顶点算光，我们当初逐像素算，大卡片上阴影 / 点光的分界变成一条硬斜线。改成同样在 VS 里算（`LitSet == 2` 时 `o.vlight = StockLight(...)`，阴影图与点光资源同时绑给 VS）后消失。
- 结论：`GpuFxNative` 默认改为 1。

## 原版带光照粒子（第 2 段）

- 内核 `0x3027B0`：`core+0x110`（byte）为 0 走无光照绘制 `0x1155810`，否则走带光照绘制 `0x1154F70`（10 个参数：sys、ctx、world、快照，栈上依次为条数、`+0x114`、`+0x118`、`+0x11C`、帧数、Material*）。条数从栈参数读，所以临时改 `core+0x1B8` 是安全的。
- 两套 shader，由 `core+0x120`（byte）选择：
  - 集 A（`0xA40`，`+0x120 == 0`）：VS `0x196F5C0` / PS `0x196BA60`。顶点步长 0x34，带快照 `+0x20` 的法线。
  - 集 B（`0xC08`）：VS `0x19848A0`（逐顶点光照，阴影 / 灯光资源绑在 VS 的 t0..t4）/ PS `0x19839A0`（alpha < 0.001 时 discard），法线固定为 (0, 1, 0)。
- 法线：`N = normalize(normalize(W·n)·w.x + ViewInv·(角点偏移)·w.y + normalize(camW − viewCorner)·w.z)`，权重为 `+0x114 / +0x118 / +0x11C`。最后一项原版把世界空间与视图空间混用，照抄。
- 集 A 的 PS（照 DXBC 逐条移植进 `gpufx_gpu.cpp` 的 `ShadowAt` / `PointLights` / `StockLight`）：
  - `rgb = tex · g_color · vcol · light`，`a = sat(tex.a · vcol.a) · sat(fade)`，混合与无光照路径相同。
  - `light = 阴影 · sat(−n·L0) · C0 + Σ sat(−n·Li) · Ci (i = 1..3) + 环境 + 分簇点光`。
  - b0 xgl_system（608 字节）：[4..6] 视图→世界（.w 为相机位置）；[20].xy 屏幕尺寸（uint）；[21]/[22] 主光方向 / 颜色（带阴影）；[24]/[25]、[27]/[28]、[30]/[31] 其余三盏方向光；[33] 环境；[35].zw 距离淡出。
  - b3（400 字节）：[0..17] 两级级联阴影（[8].x 分界，[17].x 最远距离），[18] 远处阴影淡出，[19].y 点光数，[23]/[24] 分簇深度投影。
  - t1 阴影 Texture2DArray（手动 2×2 PCF），t2 点光（步长 64），t3..t5 分簇掩码（步长 260，w[64] 为“任一”位）。
- 接管：每个世界视图里第一次原版带光照绘制照常走原版，绘制后从上下文抓取它绑定的 b0 / b3 / 阴影图 / 点光 / 掩码（集 A 取 PS，集 B 取 VS），校验尺寸与视图类型后，本视图其余带光照的卡片由我们画（`Kind::nativeLit*`），世界末尾绑定到我们的 b2–b3 / t3–t7。抓不全时本视图的带光照卡片不接管。
- 实测（`perfrun blast`，`GpuFxNative=2`）：`lit capture (set 1): system 608 bytes, shadow 400 bytes, shadow map dim 5, lights 1 1 1 1 -> complete`；60 秒内带光照接管 2065 次（集 B 0 次），抓取 2152 个视图，失败 0 次。`GpuFxNative=1` 的 blast：接管 37587 次（集 B 18694），抓取 2455 个视图，失败 0。测试场里集 B 占带光照接管的 96%（2398 / 2495）。
- 自家烟（`Kind::smoke`）在本视图已抓到原版光照时，改用原版的主光、环境、级联阴影与点光（与原版粒子同一套 HDR 单位，不再靠场景亮度中位数估曝光）；抓不到时仍用灯光块 + 曝光估计。

## 战雷式分层发射器（`gpufx_layers.h`，`GpuFxLayers`）

- 事件来源（都在本机发生，远端玩家的射击经 `0x692540 → 0x696FD0`（replay=1）也会到）：
  - 开火：`0x696FD0` 的入口钩子（`heli.cpp` 的 `MedicShotHook`）。原调用前 `gpufx::ShotBegin`：载具武器且 AmmoExplosion（`weapon+0x8B0`）≥ 2 m 时，把 `weapon+0x480`（炮口效果，`0x697F85` 为空就跳过）临时置空；原调用后 `gpufx::ShotEnd` 放回，并按枪口条目（`+0x1D0` 数组、`+0x1E0` 个数、步长 0xF0，`+0x70` 前向、`+0x80` 位置）排队一条开火事件。
  - 爆炸：`EffectGenUtil::Explosion 0x2E5ED0` 入口钩子（15 字节序言，与 `0x696FD0` 相同），参数 `(egu, pos[4], dir[4], size, shake, color[4], light)`，size = 半径 / 5。开 `GpuFxLayers` 时把 `0x2E68BC`（`0F 84 B2 03 00 00` → `E9 B3 03 00 00 90`，跳过三个 ParticleObject 的循环）和 `0x2E6F75`（`0F 84 55 01 00 00` → `E9 56 01 00 00 90`，跳过附加粒子 `0x2DFEF0`）改成必跳；震屏、焦痕贴花、点光照旧。`edf41classes.cpp` 的指纹表对已被钩住的 `0x2E5ED0` 放行（`gpufx::ExplosionHooked()`）。
  - `Explosion_Big 0x2E7A30`（第 3 段）：同一个头钩（`HeadHook`）入队 `BlastEvent`；开 `GpuFxLayers` 时 NOP 原版烟卡 `0x2E7A81` 与 15 团烟 `0x2E7BD4` 两个 call，震屏、贴花、点光照旧。
- 分层（单位尺寸 × 事件尺寸）：
  - 开炮（载具、AmmoExplosion ≥ 2）：炮口闪光（加色 0.1 s）、冲击环（沿炮管横截面向外的快速淡烟，< 1 s）、地面扬尘环（炮口离地 < 5 m，3–6 s）、浓烟团（沿炮管推出后膨胀上升随风，8–20 s）。手持发射器只有较小的烟团。同一武器 0.35 s 内的连发只出薄的一份。
  - 着弹：火球（加色）、土柱（上抛后回落）、黑烟柱（10–18 s）、碎片（弹道）、低矮残留烟（15–25 s）；半径 ≥ 12 m（炮击、炸弹）加地面大尘云（20–30 s）。
- 尺寸：单位尺寸乘 `BlastScale(r)`：半径 ≤ 6 m 线性（下限 1），再往上按 `6·sqrt(r/6)` 增长（24 m → 12），否则大爆炸的尘云能到 60 m 一团，把玩家周围糊成一堵不透明的墙。残留烟与大尘云的 alpha 峰值 0.45 / 0.5。自家烟离相机近于自身尺寸时按 `(距离 − 0.25·尺寸) / (0.75·尺寸)` 淡出，相机钻进烟团时不会满屏糊住。
- 预算与降级：池 8192 颗（`Renderer::kMaxInstances` 65536 的其余给接管）；满时替换最接近寿命末尾的粒子。LOD：60 m 内满量，越远越少（最低 1/5），池超过 3/4 再减半；数量减少时单颗按 `1/sqrt(lod)` 放大，覆盖面积不变。同一帧超过 24 个爆炸时其余每个降到 0.35。相机位置用玩家位置代替（游戏线程拿不到相机）。

## efs 渲染器（`0x2EDE20`，第 3 段接管 Texture<quad>，`GpuFxEfs`，默认关）

- `0x2EDE20` 是 `effect::EfsObject` 渲染子对象（vtable `0x17A84E0`，槽 1，构造 `0x2ED6E0`）的 `Draw(sub, ctx, pass, view*)`，是多种 efs RenderInfo 的调度器，不经过 `0x3027B0 / 0x1155810 / 0x1154F70`。
- 结构：`efsInst+0x40` 链表 → 发射器组（`node[2]+0x70`，快照数组 `+0x28`，每视图 0xA0 字节）→ `efs::Emitter`（vtable `0x19AD1C0`，槽 6 `0x10D4C30` 绘制 → `0x10D5C40`）→ RenderInfo（13 种：Texture、Distortion、Sprite、Life、Model、Locus、Line、ExtLine、LocusLine、LocusExtendLine、EmittingExtendLine、Null）。
- Sprite 的 end `0x10F2000` → `0x10DA360` → `Texture<quad>::Draw 0x10DD1F0`（只经 vtable 槽调用，槽地址 `0x1A09DC0`；第 3 段初稿误写 `0x1A09DD0`，实机日志 `not as expected` 后对照 EDF.dll 改正）。走 efs 的效果：EfsBullet、EfsExposureBullet、EfsMuzzleFlash、SpiderStringBullet03、MapObject_Efs、HumanoidPartsBreakEffect_Efs、若干怪物部件效果，及脚本 `CreateEfs`。
- 接管点：`0x10DD1F0(self, ctx, world float[16] 行向量, desc)` 换 vtable 槽（`PatchVtableSlot`，先核序言与槽内容）。它做一次 `DrawIndexedInstanced(6, count, 0, 0, 0)`，shader 组由 soft / 光照选。
  - desc：`+0` 材质（`+0x20` 网格，`*(u32*)(网格+0x28)` 索引数，须为 6；`+0x28` fetch 资源）、`+8` billboard 字节、`+0xC` normal_pow、`+0x10` soft（|soft| > 0 走变体 2）、`+0x14` zbias、`+0x18` near、`+0x20` 光照指针、`+0x28` ushort 记录索引（单位 uint4，空 = 顺序）、`+0x38` 记录（0x30：translate、scale、quat）、`+0x40` 个数、`+0x48` composite（0x40：xrot、dist、distR、pad；position；texcoord；color）、`+0x50` 纹理对象（空 = 默认纹理，不接）。
  - ctx：`+8` D3D 上下文；`+0x84` 混合开关、`+0x88` 六个 D3D11 枚举 {Src, Dst, Op, SrcA, DstA, OpA}，表 `0x10FAEE0`：0 = {5,6,1,2,1,1}（alpha）、1 = {5,2,1,2,1,1}（加）、2 = {5,2,3,2,1,1}（反减）；`+0x19C` 深度写（须 0）、`+0x1A0` 比较（8 ALWAYS / 2 LESS / 4 LESS_EQUAL）。
  - 原版 VS `0x2130`：角点 `rot2D(xrot)·fetch.xy·position.zw + position.xy`，乘 scale.xy，`q v q*`（未归一），乘 basis（world 行；billboard 时为相机行 `*(0x2139A90+8)+0x10` 乘 world 第 k 行长度），加 world 中心；沿视线推 `nd = max(|v| + zbias, near)`；雾距 `|wc + normalize(wc)·(d − nd)|`，`o2 = (dist·S34.x + S34.y, 1 − (dist·S35.z + S35.w), dist·S34.w + S35.x)`。PS `0x1D0`：`fog = sat(o2.x)^S34.z·S36.w`，`bf = sat(o2.z)^S35.y·S37.w·S37.rgb`，`rgb = lerp(max(bf, tex·col), S36.rgb, fog)`，`a = sat(o2.y)·sat(tex.a·col.a)`。S = 原版 xgl_system（608 字节，VS/PS b0）。
- 我方（`gpufx_efs.h` + `gpufx_gpu.cpp` 的 `EfsVs/EfsPs`）：一个四边形两个实例（中心 + 两轴、zbias、near、颜色、uv 矩形、深度测试标志），视图 / 投影 / 雾都用原版 xgl_system（第一次原版 efs 绘制时从 VS b0 取，换了计 `system changed`）；材质的四角 fetch 与前 6 个索引在第一次原版绘制后经 staging 读回一次，只收居中矩形、两三角在对角线相接（`EfsStripFetch`）。深度测试对照深度副本，不写深度。纹理链（`T+0x10` / `T+8` / `T+0x10→+0x10`）首次用 PS t0 核实。光照、soft、其他混合 / 深度组合、默认纹理照旧走原版（探针计数 `refused …`）。
- `GpuFxNative=2` 时与粒子一样按卡片中心分屏：左半原版、右半我方。离线：`gpufx_sim` 逐角对照 VS asm 抄写、`gpufx_gpu` WARP 覆盖三种混合、雾、距离透明、辉光地板、深度测试。
- 实机核对（第 4 段，探针 `GpuFxProbe=1`）：
  - 原版 sampler：第一次原版 efs 绘制的 PS s0 是 linear、WRAP（`filter 21 addr 1 1`），原版 ParticleRender 同样 WRAP；我方原先是 clamp，已改：efs 与原版粒子种类用 wrap 采样器（s1），自家烟的 flipbook 仍 clamp。efs 四角 fetch 的 uv 是 −1..1（日志 `fetch[0] -1.000 -1.000 uv -1.000 1.000`），clamp 与 wrap 结果不同，这一处是真差异。
  - cull：原版 efs 不剔除（cull 1 NONE），与我方相同；原版 ParticleRender 是 BACK（cull 3），粒子卡片总是正对相机、绕序不翻，BACK 不剔除任何一张，我方不剔除等价。
  - xgl_system 跨帧不变（`system changed 0`），efs 绘制都在 WorldBegin/WorldEnd 之间（`outside 0`），所有 efs 网格索引都是 `0 1 2 3 1 2`（索引为空即顺序的假设没被用到）。
  - 深度：本机绑定的 DSV 是 D32_FLOAT（`fmt=40`），没有 24 位量化；原版 efs 的比较按 ctx `+0x1A0` 逐绘制带进实例（0 不测、1 LESS、2 LESS_EQUAL）。深度目标若是 D24，PS 先把片元深度按 24 位 UNORM 取整再比（硬件测试比较的是换成目标格式后的值），`gpufx_gpu` 在 D24 上覆盖「场景深度正好等于卡片深度：LESS 不画、LESS_EQUAL 画」。
  - 画面对照：`GpuFxNative=2` 按卡片中心分屏，测试场里 Fencer 挥刀的 efs 四边形中心全在右半屏（探针 `split stock 0`），原版那一半没有可比的卡片；改成两次运行对照（`efsab3` `GpuFxEfs=0` / `efsab4` `GpuFxEfs=1`，同一 DLL t15，`GpuFxNative=1`），整轮只有约 45 个 efs 四边形（每秒约 2 个），0.25 s 一张的截图一张都没拍到，两次运行的场景本身也会分叉（爆炸、车辆位置）。所以 efs 的画面一致还没有被截图证实，`GpuFxEfs` 保持默认关。要证实需要让 efs 四边形落在左半屏的测试（转身挥刀或在左侧生成 `CreateEfs`），本轮没做。

## 性能（第 2 段，`perfrun blast`，3840×2160，`GpuFxNative=1`，20 s 干净窗口）

| | 帧数 | 平均 | 超 20 ms | 每 5 s 最差的中位 / 最大 | GPU 3D 均值 / P95 | gpufx 本身 GPU |
|---|---|---|---|---|---|---|
| `GpuFxLayers=0` | 900 | 16.70 ms（59.9） | 0.11% | 17.9 / 21.6 ms | 19% / 20% | 0.13–0.36 ms |
| `GpuFxLayers=1` | 900 | 16.70 ms（59.9） | 0.22% | 18.1 / 26.7 ms | 22% / 44% | 1.06–2.41 ms（1419 颗） |

- 窗口内分层发射器收到开火 32 次、爆炸 32 次，排出 1629 颗，队列满 0 次。开销主要在大烟团的填充率（4K 下 2.4 ms）。天罚级别（同帧几十个爆炸）的压力场景还没有实测，只靠上面的同帧上限与 LOD 兜底。

## 合批（第 4 段，`gpufx_batch.h`）

- 原版每个发射器一次 Draw（ParticleRender），efs 每个四边形一次（`Texture<quad>::Draw`，n = 1）。接管后不再一对一重画：一帧内接管的绘制按原版顺序收成记录（实例区间 + 键），世界末尾合成尽量少的实例化绘制。
- 键（`SameBatch`）：种类（原版无光照 alpha / 加色、带光照 alpha / 加色、efs 加色 / 反减 …）、格数、光照集与法线权重、几何、贴图组。能合的只有两种情况，都不改画面：
  - 相邻且同键：总能合（原版顺序不变）。
  - 加色（dst + src、不写深度，任意顺序结果相同）的连续一段里，同键的记录即使中间隔着别的键也合成一组；alpha / 反减记录截断这一段，什么都不越过它。各组按首条记录的顺序画，组内保持原版顺序。
- 贴图表：原版种类 1..4 相邻、只差贴图的记录合进同一次绘制，最多 8 张（`Tex` t1 + `TexMore[7]` t8..t14），实例在 `soft` 里带槽号（原版卡片的 soft 范围由 VS 按 `clamp(size·0.5, 0.05, 3)` 算，不占这个字段），PS 用 `SampleSlot` 按槽取（`SampleGrad`，导数在分支外求）。
- 第 4 段压测里断批原因几乎都是「种类交替」（无光照 alpha ↔ 带光照 alpha 逐发射器交替）。把它们也合并要把种类 / 格数 / 光照集改成逐实例状态（结构化缓冲），没做：我方整段渲染的 CPU 每帧 0.1–0.4 ms（见下面的表），瓶颈在 GPU 填充率而不是绘制次数。
- 上限：`Renderer::kMaxInstances` 65536、`kMaxBatches` 1024（原 16384 / 256）。原来接管可用的实例只有 8192，天罚场景里装满后每帧约 245 次原版绘制被拒（`refused` 计数）退回原版。

## 填充预算（`gpufx_sim.h` `ThinToBudget`）

- 自家烟逐像素打光、软粒子，开销就是覆盖的屏幕面积。天罚从烟里看出去时一视图覆盖 3000 屏以上（第 4 段 st7：每帧 GPU 240 ms，4–7 fps）；减少颗数本身没用（LOD 放大单颗、覆盖面积不变）。
- 单颗覆盖（屏）：`s²·|P00·P11| / d²`，上限 1；相机在烟团里（`d ≤ s` 且 `d > −s`）记 1 整屏；VS 会淡掉的（中心距离 ≤ 0.25 s）与相机后面的记 0。
- 预算 `kFillPixels` 5000 万像素（4K 约 6 屏）。超出时按「水位」分配：求 cap 使 `Σ min(覆盖ᵢ, cap) = 预算`，每颗以稳定概率 `min(1, cap / 覆盖ᵢ)` 保留（`SerialUnit(生成序号)`，不逐帧闪），保留的 alpha 提到 `1 − (1 − a)^(1/keep)`，透过一团烟的不透明度不变。小于 cap 的（远处、轮廓）全留，只稀释贴脸的大烟团。
- 初版是统一比例、下限 1/5：天罚里一视图 6000 颗、覆盖 3000 屏时仍留 600 屏，GPU 仍 240 ms（st7）。改成水位后同场景 GPU 9–38 ms（st9）。

## 天罚压测（第 4 段，`GpuFxTest=2`：前方 60 m 三发天罚，3840×2160，垂直同步 60，每 5 s 一窗）

`tmp/stress.sh`，`range` 任务。原版这一段每视图约 550–850 次粒子绘制。表里是重灾窗口（第 7–9 窗）的帧率；「我方 GPU」是探针记录的单帧我方整段（含深度副本）的 GPU 时间，「fill」是同一帧 PS 调用数折算的屏数。

| 运行 | DLL / 设置 | 第 7 / 8 / 9 窗 fps | 原版绘制 / 视图 → 我方绘制 / 视图 | 我方 GPU（ms） | 我方 CPU（ms） |
|---|---|---|---|---|---|
| st18 | 原版（`GpuFxNative=0`） | 51 / 41 / 40 | 811 / 750 / 651 → — | — | — |
| st15 | t11 接管，`GpuFxLayers=0` | 15–23 | 805 → 247 | 24–44 | 0.15–0.2 |
| st17 | t12（PS 拆分、集 B 三元式修正） | 29 / 27 / 28 | 874 / 773 / 656 → 253 / 277 / 263 | 10 / 21 / 26 | 0.21–0.26 |
| st20 | t13（单帧贴图只采一次） | 32 / 34 / 34 | 846 / 744 / 614 → 245 / 263 / 271 | 8 / 16 / 27（fill 61 / 57 / 59 屏） | 0.15–0.16 |
| st23 | t15（零输出像素不算光照） | 34 / 34 / 35 | 801 / 719 / 635 → 233 / 272 / 285 | 7 / 15 / 24（fill 51 / 66 / 57 屏） | 0.29–0.33 |
| st19 | t12，`GpuFxLayers=1`（默认配置） | 35 / 46 / 54 | 644 / 423 / 269 → 50 / 48 / 44 | 6 / 8 / 4 | 0.30–0.32 |

- 绘制次数：接管后每视图的绘制约为原版的 1/3（合批）；我方整段 CPU 每帧 0.1–0.35 ms。剩下的差距全在 GPU。
- 按桶的 GPU 时间（探针 `gpu by bucket`，st23 第 9 窗那一帧）：自家烟 2.0 ms（85 颗）、原版无光照 3.2 ms（4532）、集 B 0.4 ms（1274）、**集 A（0xA40，逐像素光照）18.7 ms（5539）**。同样约 5500 张集 A 卡片，在天罚的点光熄灭后（第 11 窗）只要 1.1 ms：开销来自逐像素的分簇点光循环，天罚的爆炸点光把它放大十几倍。这段 PS 是照原版 DXBC 逐条移植的，推断原版也付这份钱（原版同窗口也从 60 掉到 40 fps），但没有直接测原版 PS 的 GPU 时间。
- 已做的根因修正：
  - 集 B 的光照写成 `?:`，HLSL 两边都算，等于每像素都跑一遍集 A 的光照：改成分支。
  - 一个 PS 包含所有路径，寄存器压力拖累所有卡片：拆成四个入口（自家、原版无光照、原版带光照、efs）。
  - 单帧贴图（原版卡片几乎全是）也采两次再插值：`Frames > 1` 才采第二帧。
  - 粒子种类改用只读 DSV 的硬件深度测试（LESS_EQUAL；反向深度用 GREATER_EQUAL），被场景挡住的像素不进 PS。
  - 输出为零的像素（alpha 种类 a = 0；加色种类贴图色为零或软淡出为零）跳过光照，结果逐位不变。
- 仍有的差距：`GpuFxLayers=0` 时重灾窗口 34–35 fps，原版 40–51。默认配置（`GpuFxLayers=1`，天罚的爆炸粒子换成自家分层烟）最差 35 fps，随后回到 46–54。集 A 每张卡比原版贵在哪里还没有定论：要拿 GPU 抓帧（PIX）逐个对比原版 PS 与我方 PS 的同一次绘制，本轮没做。

## 未验证 / 未做

- efs 以外的 RenderInfo（Distortion、Model、Locus/Line 系）未接。
- 集 A 带光照卡片在大量点光下比原版贵（见上一节），根因需要 GPU 抓帧对比。
