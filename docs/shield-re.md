# 防护者护盾（Shield Bearer）逆向笔记

2026-10-05，静态分析（EDF.dll TimeDateStamp 0x678CCB46，RVA）。H = 代码里验证过，M = 推断，L = 未核实。
实现：`src/shield.cpp`（用户需求：护盾只挡高速物体，慢的人/子弹能穿过，快的飞机/子弹被挡住）。
**全部还没在游戏里验证。** 开 `Debug=1` 后日志里的 `SHIELD` 行是验证依据：

- `SHIELD n up` 后面每层一行球心与半径；
- `SHIELD rounds:` 每 2 秒统计放过 / 挡下的子弹数，以及最后一颗子弹的速度；
- `SHIELD v=… ran into shield` 表示飞机撞上了护盾。

## 1. 对象

- 防护者：`E513_SHIELDBEARER[_L/_XL/_FIXED*].SGO`，类 `AlienTrailer`，vtable `0x17A9650`，ctor `0x309CC0`（H）。
- 护盾：嵌在本体里的 `AlienTrailer_Barrier`，位于 `+0x10D0`，初始化 `0x313000`，每帧更新 `0x314690`（H）。
- 两层，每层 0x680 字节，构造 `0x312330`，初始化 `0x313530`（H）：
  - 小盾在本体 `+0x18C0`，大盾在 `+0x1F40`；
  - 每层 `+0x00` 是激活标志（byte，M）；
  - `+0x580` 是 xgs body（它的 `+0xF0` 是 body id），`+0x590` 是 `BarrierInfo`（vtable `0x179DFD0`，`+8` 是阵营，每帧从本体 `+0x314` 复制，H）。
- body 的属性 key2 = &BarrierInfo，表示「这是护盾」；碰撞层 14（H）。
- 模型半径：S 50 m、L 100 m、LL 200 m、XL 470 m（M）。插件不用这张表，而是量 body 形状的世界包围盒（见下文）。

## 2. 子弹

- 候选收集器 addBody `0x232AA0`（vtable 槽 `0x179E128`）：
  - body 带 key2 时，若阵营敌对就加入候选，命中处理 `0x2321B0` 会把子弹截停在盾面（H）；
  - 贯通弹也会被挡（H）。
- 子弹速度：`core = 收集器+0x88`，`core+0xB90`，单位 m/s，所有 BulletBase 通用（H）。导弹更新时调 SetVelocity `0x235540`，所以也能读到（M/H）。激光类没核实（L）。
- 插件做法：`AddBodyHook`（`jet_hooks.cpp`）最前面调 `ShieldLetsThrough`。
  - 判定这个 body 是不是护盾层：`0x108260(id)` 取回对象，vtable 是防护者，且 id 等于某个激活层的 body id。
  - 速度低于 `kPassSpeed` = 150 m/s（2.5 m/帧）时，不把它交给原版，子弹就不会撞上护盾，直接穿过。快的照原版处理。
- 限制：这个挂钩随喷气机功能一起安装（`InstallJets`）。喷气机功能关掉时，护盾保持原版。

## 3. 载具

- 原版护盾对载具没有作用。直升机的 `CheckBarrierCollision 0x64FB20` 要求 `BarrierInfo+0xC != 0`，但镜像里找不到写这个字段的代码（M）。
- 插件做法：`ShieldBlock` 在插件喷气机（`JetBodyStep`）和玩家飞机（`PlayerJetBodyStep`）的物理步进里运行，改的是飞机自己保存的速度：
  - 速度超过 `kBlockSpeed` = 40 m/s，且两帧之内会穿过敌对护盾面时，把穿面方向的速度分量反向，按 `kBounce` = 0.3 保留，切向分量保持不变；
  - 慢的载具、步行的人都不受影响。
- 球面的算法照抄直升机检查里 `0x64FC1A..0x64FCD6` 那段：
  - 形状：`0x11B15E0(body)`；
  - 变换：`(*(*(body+0x100)+0x58)+0x20)` 的虚表槽 0x80，参数是 body id；
  - 包围盒：表 `*(img+0x20F1930)` 中按形状类型取 `+type*0x100+0x18`，调用 `(shape, 变换, out[min, max])`；
  - 球心取包围盒中心，半径取 x 方向半宽。
- 护盾列表每帧从锁定注册表收集一次，取 EDF 方的敌人（`VisitEnemiesOf(0)`）。前提是防护者能被锁定（M）。
- 原版直升机、地面载具没有接入（它们不走插件的物理步进）。
