# 弹体发光声明（round glow）

任意弹药只要在自己的 SGO 里带一条发光声明，插件每帧就在每发**活着且尾焰已点着**的弹的尾焰处画一个加色光晕。弹体模型、原版尾焰和尾迹都不改；AmmoColor 只管尾焰和尾迹，弹体本身的亮由这条声明负责。

代码：`src/gpufx/gpufx_glow.h`（纯逻辑：解析、缓存、上限、卡片），`src/gpufx/gpufx.cpp` 的 `WalkGlows` / `BuildGlows`（遍历子弹表）和 `WorldEnd`（绘制），`src/rounds.cpp` 的 `CustomTail`（读 CP 尾项），`src/gpufx/gpufx_gpu.cpp` 的 `Kind::glow`（着色器）。离线检查：`tests/gpufx_glow_test.cpp`（ctest `gpufx_glow`）、`tests/gpufx_gpu_test.cpp` 第 4b 段，以及 `tools/selftest.py` 的 `store_looks` / `sortie_copies_agree`。

## 声明格式

在弹药的 `Ammo_CustomParameter`（下称 CP）**末尾**追加一个列表：

```
[7146.0, r, g, b, intensity, size(, back)]
```

| 项 | 含义 | 范围 |
|---|---|---|
| 7146.0 | 标记（`GLOW_MARK` / `glow::kMark`），表示这一项是发光声明 | 必须相等 |
| r g b | 光晕颜色，线性值，插件会按最大分量归一 | 每项 ≥ 0，最大项在 (0, 1] |
| intensity | HDR 倍数（颜色 × 它），和原版发光尾迹 AmmoColor 大于 1 的写法同理 | (0, 64] |
| size | 光晕半宽，单位 m | (0, 50] |
| back | 可选，缺省 0。光晕中心沿机头反方向后移多少 m，让它贴在尾焰上而不是罩住整发弹（尾焰和烟尾都从位置沿 −nose 往后铺：update 给光焰 +0xF50 和烟尾 +0x1140 的方向是 nose × −1，常量 `0x1C36AD0`） | [0, 50] |

后面最多还能再跟 9 个数，留给以后加字段（整项不超过 16 个数）。不合法的声明（标记不对、有非有限值、越界、全黑）一律当没有，不画。

Python 侧不要手写，统一用 `pylib/vcobjects.py` 的 `declare_glow(cp, ammo_class, (r, g, b, intensity, size[, back]))`。它会把 CP 补到该类的首个空闲位再追加声明；类没核实过、CP 已经超过空闲位、或缺了原版要读的项，都直接抛错。

## 为什么原版看不到这一项

声明必须放在该类构造函数一项都不读的位置，这样带不带它，原版弹体都完全一样。目前只核实了两类（`glow::kKinds`，按弹体 vtable = core−0x140 匹配）：

| 类 | vtable RVA | 构造 | 读到的 CP | 首个空闲位 |
|---|---|---|---|---|
| MissileBullet01 | 0x17A1C10 | 0x267910 | CP[0..11]，CP[11] 仅在 count > 11 时读，CP[12] 仅在 count > 12 时读（bool，存到 +0x13C0，缺省 0） | 13（CP[12] 补 0，和没有 CP[12] 一样） |
| MissileBullet02 | 0x17A1E28 | 0x26AF70 | CP[0..15] 无条件读，往后不读 | 16 |

其它类（SolidBullet01 等）插件根本不看，即使带了声明也不画。要支持新类，先逆向它的构造函数，确认它读到 CP 的哪一项（注意「count > N 才读」这种条件读），然后同时改三处：

1. `gpufx_glow.h` 的 `kKinds`；
2. `vcobjects.py` 的 `GLOW_FIRST_FREE`（有条件读的项在 `GLOW_PAD` 里给出等价于缺省的填充值）；
3. `selftest.py` 里 `sortie_copies_agree` 的 `kinds` 表。

selftest 会核对这三处是否一致。

## 插件怎么读

- 每发弹都在子弹系统链表上：`*(EDF+0x20B32F0)`，哨兵节点在 +8，每个节点是 {next, prev, core 在 +0x10}。玩家、NPC、敌人的弹都在这里，联机时远端玩家的弹也在（经 0x696FD0 按同一份 SGO 重放）。所以联机不需要额外协议：只要全员装的是同一版安装包，远端的弹也会发光。
- 每发弹的 bullet core 在 +0xA58 存一份 CP 的 variant（16 字节：document 和 node，tag 字在 +0xA68 = 2），由 0x2307F0 拷入，构造函数也从这里读（0x267AB6 / 0x26B18E）。分裂出来的子弹是 MissileBullet01，它们的 CP 是母弹 CP[15][13]，所以子弹的声明写在子弹 CP 的第 13 项。
- 读 variant 用游戏自己的 count（0x240AF0）/ child（0x2390D0）/ number（0x2390B0），整段放在 `__try` 里。结果按这 16 字节缓存（32 条，1 秒后重读一次），所以每种弹药约每秒只读一次，不会每帧每发都去读。
- 只画活着且已点火的弹（`glow::Shines`）：core+0xAF4 bit0 是死亡标志（0x2318E0 置 3，两类 update 都测 bit0），round+0xF90（core+0xE50）是尾焰开关（MissileBullet01 update 0x26A880 过了 CP[7] 帧后、MissileBullet02 飞行状态 0x26EC10 进入时每帧置 1，死后每帧清 0，构造后到点火前为 0）。
  - **为什么要判死亡**（2026-11-26 玩家：「那个导弹在开始的时候阶段有一些特效会卡住，很久不消失」）：导弹死后对象不会马上离开子弹表——update（0x26A880 / 0x26E4C0）要等 +0x1090 和 +0x1280 两条烟尾都淡完才删对象（0x118A1B0），core 析构 0x230550 才把它摘下链表，最长是烟尾寿命 CP[2]（几百帧）。旧代码只看「在表上且有声明」，于是分裂后的母弹、命中后的子弹在原地继续发光好几秒。按「死了就不画」修的是这条生命周期，不是把寿命改短。
  - 不判点火的话，冷发射坠落那几帧会有一团没有尾焰的光。
- 位置取 core+0xB80，速度取 core+0xB90（m/s），机头取 round+0x80（core−0xC0）。插件的 tick 在玩家士兵的 pre-update 里，比弹体自己的 step 早一步，所以光晕按速度提前 `kLeadSteps` = 1 步（1/60 s）。这一步的置信度是 M，需要实机确认。

## 绘制与预算

- `Kind::glow`：一张面向相机的卡片，不采样贴图，衰减为 (1 − r²)²，颜色 × alpha。混合用 ONE/ONE 加色，输出 alpha 为 0，目标的 alpha 不动。和场景深度做软过渡（soft = max(0.05, size/4)）。卡片朝相机拉近 min(size/2, 距离/2)，免得被弹体自己的模型挡住。
- 每帧最多 `kMostGlows` = 2048 个光晕。超出时每发弹按 id 稳定地以 2048/总数 的概率保留，保留下来的按倒数提亮，整群的总亮度不变。
- 另有独立的填充预算 `kGlowFillPixels` = 8e6 像素（约一个 4K 屏），由 ThinToBudget 剔除，用来处理相机钻进弹群的情况。
- debug 探针日志：`GPUFX   round glows: N glowing rounds, M halos published, K drawn this view (the stock-kinds bucket); not drawn: D dead (trail fading), U unlit (coasting)`。D / U 是带声明但这一帧没画的弹：齐射后 D 应在几秒内从几百降到 0，U 只在出膛后 0.4 s 内非零。

## 现有用法

| 弹药 | 声明 | 位置 |
|---|---|---|
| 齐射母弹（`STORES['SALVO']`，MissileBullet02） | `SALVO_GLOW` = (1.0, 0.75, 0.45, 5, 1.0 m, 后移 0.5 m)，橙白，贴尾焰 | CP[16] |
| 齐射子弹（MissileBullet01，母弹 CP[15][13]） | `SALVO_CHILD_GLOW` = (1.0, 0.8, 0.5, 3.5, 0.6 m, 后移 0.3 m) | 子弹 CP[12] 补 0，CP[13] |

256 发母弹加 1024 发子弹共 1280 个，在 2048 的上限以内，全部都画。

「全部导弹炮弹效果重做」时，给某种导弹 / 炮弹加光，就在它生成 SGO 的地方调一次 `declare_glow`。如果是 MissileBullet01 / 02 以外的类，先按上一节把新类加进来。

## 待实机确认

- 光晕是否落在尾焰上（提前 1 步对不对；落后就是 0 步，超前就是 2 步；后移 back 的 0.5 / 0.3 m 是按模型估的）。
- 齐射后原地不再留光团；探针行的 dead 计数随烟尾淡完归 0；冷发射坠落时不发光、点火后才亮。
- 联机时远端玩家的齐射是否也发光。
- 1280 个光晕 + 原版 1280 条烟尾时的帧时间。
- 亮度和大小的观感（只需调 `SALVO_GLOW` / `SALVO_CHILD_GLOW` 的 intensity 和 size）。
