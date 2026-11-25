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

`gpufx::ReadLight` 每帧读 +B40 / +B50 / +B60。只验证过一张白天地图，夜间和室内地图没核对。

## 性能（run 1，3840×2160）

- 稳态：自己的粒子约 90 张，CPU 0.03–0.05 ms，GPU 0.03–0.46 ms（GPU 时间戳）。
- 首帧 CPU 36 ms，用于编译着色器和建资源，只发生一次。

## 接管实测（run 2，`GpuFxNative=1`）

- 60 秒测试场里原版共绘制 16971 次，接管 16970 次（第一次绘制时纹理链尚未确认，那次走了原版），拒绝 0 次，没有崩溃。
- 每帧约 7–10 个批次、约 190 张卡片。CPU 0.03 ms，GPU 0.03–0.49 ms。
- **画面一致性未得到验证**：这张图视野内看不到可辨认的原版烟。dump 里的原版粒子大小只有 0.01，颜色为 (1, 1, 1, 0.19)，可能是不可见的小粒子，也可能 +0x34 根本不是大小字段。要在有大量原版烟的任务（爆炸、坠机）里做 A/B 对比之后，才能把 `GpuFxNative` 默认打开。

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
- 实测（`perfrun blast`，`GpuFxNative=2`）：`lit capture (set 1): system 608 bytes, shadow 400 bytes, shadow map dim 5, lights 1 1 1 1 -> complete`；60 秒内带光照接管 2065 次（集 B 0 次），抓取 2152 个视图，失败 0 次。
- 自家烟（`Kind::smoke`）在本视图已抓到原版光照时，改用原版的主光、环境、级联阴影与点光（与原版粒子同一套 HDR 单位，不再靠场景亮度中位数估曝光）；抓不到时仍用灯光块 + 曝光估计。

## 战雷式分层发射器（`gpufx_layers.h`，`GpuFxLayers`）

- 事件来源（都在本机发生，远端玩家的射击经 `0x692540 → 0x696FD0`（replay=1）也会到）：
  - 开火：`0x696FD0` 的入口钩子（`heli.cpp` 的 `MedicShotHook`）。原调用前 `gpufx::ShotBegin`：载具武器且 AmmoExplosion（`weapon+0x8B0`）≥ 2 m 时，把 `weapon+0x480`（炮口效果，`0x697F85` 为空就跳过）临时置空；原调用后 `gpufx::ShotEnd` 放回，并按枪口条目（`+0x1D0` 数组、`+0x1E0` 个数、步长 0xF0，`+0x70` 前向、`+0x80` 位置）排队一条开火事件。
  - 爆炸：`EffectGenUtil::Explosion 0x2E5ED0` 入口钩子（15 字节序言，与 `0x696FD0` 相同），参数 `(egu, pos[4], dir[4], size, shake, color[4], light)`，size = 半径 / 5。开 `GpuFxLayers` 时把 `0x2E68BC`（`0F 84 B2 03 00 00` → `E9 B3 03 00 00 90`，跳过三个 ParticleObject 的循环）和 `0x2E6F75`（`0F 84 55 01 00 00` → `E9 56 01 00 00 90`，跳过附加粒子 `0x2DFEF0`）改成必跳；震屏、焦痕贴花、点光照旧。`edf41classes.cpp` 的指纹表对已被钩住的 `0x2E5ED0` 放行（`gpufx::ExplosionHooked()`）。
  - `Explosion_Big 0x2E7A30` 是另一条路径，本段未接。
- 分层（单位尺寸 × 事件尺寸）：
  - 开炮（载具、AmmoExplosion ≥ 2）：炮口闪光（加色 0.1 s）、冲击环（沿炮管横截面向外的快速淡烟，< 1 s）、地面扬尘环（炮口离地 < 5 m，3–6 s）、浓烟团（沿炮管推出后膨胀上升随风，8–20 s）。手持发射器只有较小的烟团。同一武器 0.35 s 内的连发只出薄的一份。
  - 着弹：火球（加色）、土柱（上抛后回落）、黑烟柱（10–18 s）、碎片（弹道）、低矮残留烟（15–25 s）；半径 ≥ 12 m（炮击、炸弹）加地面大尘云（20–30 s）。
- 预算与降级：池 8192 颗（`Renderer::kMaxInstances` 16384 的其余给接管）；满时替换最接近寿命末尾的粒子。LOD：60 m 内满量，越远越少（最低 1/5），池超过 3/4 再减半；数量减少时单颗按 `1/sqrt(lod)` 放大，覆盖面积不变。同一帧超过 24 个爆炸时其余每个降到 0.35。相机位置用玩家位置代替（游戏线程拿不到相机）。

## efs 渲染器（`0x2EDE20`，未接管，方案）

- `0x2EDE20` 是 `effect::EfsObject` 渲染子对象（vtable `0x17A84E0`，槽 1，构造 `0x2ED6E0`）的 `Draw(sub, ctx, pass, view*)`，是多种 efs RenderInfo 的调度器，不经过 `0x3027B0 / 0x1155810 / 0x1154F70`。
- 结构：`efsInst+0x40` 链表 → 发射器组（`node[2]+0x70`，快照数组 `+0x28`，每视图 0xA0 字节）→ `efs::Emitter`（vtable `0x19AD1C0`，槽 6 `0x10D4C30` 绘制 → `0x10D5C40`）→ RenderInfo（13 种：Texture、Distortion、Sprite、Life、Model、Locus、Line、ExtLine、LocusLine、LocusExtendLine、EmittingExtendLine、Null）。
- Sprite 的 end `0x10F2000`：存活索引（`RenderInfo+0xB8` 的 uint 向量）按深度排序后，把 `RenderInfo+0x128` 的实例数组（每条 0xC0 字节）交给 `0x10DA360`，最终 `0x10DD1F0`（`Texture<quad>` flush）`DrawIndexedInstanced`。实例数据走逐帧环形缓冲，没有 CPU 动态 VB。
- 走 efs 的效果：EfsBullet、EfsExposureBullet、EfsMuzzleFlash、SpiderStringBullet03、MapObject_Efs、HumanoidPartsBreakEffect_Efs、若干怪物部件效果，及脚本 `CreateEfs`。
- 方案：只接 Sprite / Texture quad，其余 RenderInfo 放行。钩 `0x10F2000`（或 `0x10DA360`），先用探针 dump 0xC0 实例记录确定位置 / 大小 / 颜色 / UV / 旋转偏移，再按视图过滤（`*(efsInst+0x110)+0xC0`）照搬，读出后进 `nativeInstances`；blend 取 `ctx+0x19C / 0x1A0`。shader 按材质变体从缓存取，没有固定地址，光照要另行移植。工作量与第 1、2 段的 ParticleRender 接管相当，本段未做。

## 未验证

- 原版 atlas 的 UV 和角点映射（`cornerScale 0.7071`、`rot - π/4`、格数 `ceil(sqrt(frames))`）：要用 `GpuFxNative=1` 和原版截图对比。
- 快照 +0x34 是否就是世界尺寸：dump 里出现过 0.01，可能是火花这类小粒子。
- 原版 PS 的光照公式和 t4 的用途。
- efs `0x2EDE20`：另一个渲染器，本段未接管。
- 嵌入式发射器 `0x3027B0`。
