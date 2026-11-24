# 2026-11-24 全面优化计划

维护者原话：

> 「根本性优化游戏，包括性能、可维护性、帧数解锁，让帧数和游戏解耦，还有任何应该去做的优化。应该多线程的多线程，应该并发的并发。联机优化。优化我们mod的旧功能」
> 「不止这点东西吧，能优化的都优化，应该优化的优化」

依据：九路只读审查，原文与逐条证据在 `docs/reviews/2026-11-24-optimization-audit.md`（下文 `fps-3` 之类即该文条目编号）。
已有基础不重做：帧步进真等待（`frame-pacing-re.md`）、Havok 多线程默认开（`havok-mt-re.md`）、TerrainShare 大地图内存（PR #79）、
不限帧率阶段 1（`unlimited-fps-re.md`）、#330 的插件热点修复、CRI loader 池、主插件异步日志。

## 1. 先纠正的几个前提

1. **性能基线已过时。** 47.9 / 108 ms 是 #330 定稿前的数。#330 后 stress:30 = 32.8 ms、stress:100 = 77.4 ms；stress:100 里
   插件 ThinkHook 21.4 ms、`edf::Readable` 8.5 ms、Havok 约 9 ms，**原版剩约 40 ms 从未按阶段拆过**。PERF 行按帧计时，但 stress 下
   一帧要补 3 步（`0x11A2F76` 夹到 ≤3），「ms/帧」会误导方向（cpu-1）。
2. **GPU 不是瓶颈。** stress 实测 GPU 3D 3–17%，重场景卡在游戏线程。D3D11 deferred context 并行提交不做（gpu-9）。
3. **`unlimited-fps-re.md` 有三处要改**（fps 摘要、latency-4）：滚轮 / 鼠标要在动作封装层累加（输入晚一帧才被步进读到）；
   SyncInterval 不是游戏设置，原版恒为 1、仅追帧后 8 帧为 0；镜头投影调用点 `0x1197B50` 已被 `scope_lens` 占用，插值并进它的钩子。
4. **`frame-pacing-re.md:99` 说渲染栅栏「真等待」不对**：`0x111CC20` 是 `WaitForSingleObject(h,0)+Sleep(0)` 忙等（gpu-1）。
5. **不限帧率不需要升联机协议**：EDF6Coop 的每帧工作都挂在 EOS tick（只经 `0x7056AE`），n = 0 帧守住即与原版同节奏（fps-2）。

## 2. 不做的（有证据的否决）

- 并行化原版对象的 Think / Update 虚函数循环（cpu-9）：竞态与联机分歧风险，收益不可证。
- deferred context / 多线程渲染提交（gpu-9）、渲染与模拟流水线化（cpu-10）。
- 改 ResourceManager 固定 4 槽（hitch-11）：除非加载基线证明是 CPU 瓶颈。
- 模拟改成按真实时间变步长（`unlimited-fps-re.md` §0.3）。

## 3. 分线与排期

七条线。同一批里各线改的文件不重叠，可并行；每条线自成分支和 PR。凡标 G 的都按实机铁律排队拿游戏验收。

### A. 测量（第一批，后面所有取舍靠它）

cpu-1（perf_report 按步归一、线程角色、绘制 / 帧外细分）、hitch-1（慢帧 QPC 时间戳 + 采样器按时间对齐）、hitch-5（进关分段耗时）、
gpu-2（渲染线程分段与 draw 数）、latency-2（PresentMon + 输入到 Present 打点）、net-1（按通道消息 / 字节 / 世界遍历计数）、
cpu-11（n = 0 帧成本）。然后在当前 main 上重测 stress:1/10/30/100 与 M001、RM015 基线，写进本节。
文件：`src/crew.cpp` PerfTick、`tools/perfrun.py`、`tools/perf_report.py`、`tools/rip_sampler.cpp`。

### B. 呈现链与不限帧率（第一批起，顺序做）

1. 小补丁先发：gpu-1 渲染栅栏改真等待；latency-1 `SetMaximumFrameLatency(1)`；文档更正（§1.3、§1.4）。
2. 不限帧率阶段 2（Debug 开关下）：fps-1 帧门小桩、fps-2 节奏守卫逐个改调用（含 `0x7056AE` 守住 Coop）、fps-3 输入在封装层保留、
   fps-4 插件接管 SyncInterval。同批并入 latency-3（封装更新挪到 System::Update 之前，少一个 tick 延迟）——它和 fps-3 挂同一对钩子。
3. 阶段 3：fps-5 抓镜头步进结果、fps-6 镜头插值并进 scope_lens、fps-7 玩家 / 载具根变换插值、fps-8 插件镜头切换断点 API、fps-9。
   fps-6 与 fps-7 必须同时对用户开放。
4. latency-5 / gpu-3 交换链翻转模型 + ALLOW_TEARING（不限帧率真正上屏的前提），latency-7 tick 相位锁 vblank，latency-8 Raw Input。
5. 阶段 4 / 5（全部骨骼、植被风、着色器时间、特效）与 fps-10 联机实测。

**B 线进展（2026-11-24）与下一步**

- 第 1 步：PR1（分支 `claude/opt-present-fps-032dd4`）——gpu-1、latency-1（ini `MaxFrameLatency`，默认 1）、文档更正、签名审计。
- 第 2 步：PR2（分支 `claude/fps-stage2-dev`，基于 PR1）——只在 `FrameRateDev=1` 下生效。比计划多做的：
  - 共享随机数 `0x705570` 每帧推进一次，是第 6 个守卫。
  - fps-3 不用 `Shadow` OR 回，而是把封装更新挪到步进之前、只在 tick 帧跑，和 latency-3 是同一个改动。
- 阶段 3 之前要补的核查（审查代理留下的，见 `unlimited-fps-re.md` §2.2）：
  1. 每帧对活动相机 vslot_3 的调用，各相机类内部是否按调用次数累加。
  2. UI 各界面内部是否全按 dt 推进（`0x119D270`）。
  3. `Application+0x60 = n`（`0x7059CF`）由谁读取。
  4. SoundController `0x7B3010` 两处按次数的音量平滑：`+0x3C` 每次 0.1，`+0xD8` 每次 0.01。改成按 dt 缩放，或在 n = 0 帧跳过。
  5. 原版 `[rdi+9]` 那一帧不经过帧门，`Steps()` 沿用帧门上次的 tick 数，可能少取一帧鼠标。要么在帧门里也接管这条路径，要么实机证明它只出现在载入时。
- 阶段 3 的顺序：
  1. fps-5：在步进末尾抓镜头的「上一步 / 这一步」。
  2. fps-6：镜头插值并进 `scope_lens.cpp` 的 `ProjectHook`（`0x1197B50` 已被它占用）。
  3. fps-7：玩家和所乘载具的根变换。fps-6 与 fps-7 只在 `FrameRateDev` 下同时打开。
  4. fps-8：插件镜头（地图俯视、第一人称、敌控、遥控武器站、炮塔）在切换的那一步打断点标记。
  5. 实机验收按 §7-6。阶段 3 验收过后，才考虑让 `FrameRateCap` 脱离开发开关。
- 阶段 3 进展（分支 `claude/fps-stage3-dev`，基于 PR2）：fps-5～9 与翻转模型 + 可等待对象完成，只在 `FrameRateDev=1` 下生效。
  上面 5 条核查：4（音量平滑）、5（`[rdi+9]` 帧）已修，守卫改读主循环最终 n（`0x11A2FD8`），共 8 个守卫；1（相机 vslot_3）、
  3（`Application+0x60`）未查；2（UI）里按调用次数自增的几个界面字段列为人测项（`unlimited-fps-re.md` §4.2、§7-3）。
  实机冒烟（M001，`FrameRateCap=144`，160 Hz 显示器，一次启动进出任务 3 次）：tick 稳定段 59.9–60/s、出帧 125–138 fps，
  投影与玩家根变换每 5 s blended 100–390 次、as stepped 0，无转储，画面正常。本机游戏开着 8× MSAA，交换链按设计保留 blt，
  **翻转模型与可等待对象没测到**，留给维护者关 MSAA 测（§7-6a）。
- **MaxFrameLatency A/B（#371 审查第 5 条，2026-11-24）**：要的是 GPU 受限的重场景，本机做不到——RTX 5090 在最重的 blast
  （250–300 敌人）里 3D 引擎只有 15–17 %，游戏顶在 60 tick 上限，CPU 也没满；画质设置在 Steam 云同步的二进制存档里，没改。
  能拿到的数据（perfrun blast，干净窗口 30 s，同一份 DLL `0e1c9f25`，每局一次启动；MaxFrameLatency 只在设备初始化时设，
  同一次启动里切不了）：

  | 时间 | MaxFrameLatency | fps | 超 20 ms | 每 5 s 最坏中位数 | 进程 CPU | 3D 引擎 | 敌人 |
  |---|---|---|---|---|---|---|---|
  | 14:16 | 0 | 55.9 | 35.1 % | 100 ms | 125 % | 8.6 % | 125 |
  | 15:54 | 0 | 59.7 | 2.6 % | 30 ms | 190 % | 17.0 % | 269 |
  | 15:48 | 1 | 16.4 | 34.2 % | 450 ms | 44 % | 1.6 % | 19 |
  | 15:57 | 1 | 30.1 | 32.6 % | 273 ms | 63 % | 3.4 % | 117 |
  | 16:04 | 1 | 59.8 | 11.8 % | 56 ms | 191 % | 15.0 % | 288 |

  （另有一局 =0 在 14:24 只有 14 fps、CPU 20 %，当时本机在编译，剔除。）**结论：不能定论，但方向对 =1 不利。**
  =1 的 3 局里 2 局整局降速（从进任务、还没有敌人时就 10–20 fps，CPU 和 GPU 都闲）；=0 的 2 局都正常；两边都正常的那一对里
  平均帧时间相同，但 =1 的抖动大（超 20 ms 11.8 % 对 2.6 %）。降速局的栈没采到（采到的 16:04 局是正常局）。样本太少，
  也不排除本机别的会话同时占机器。建议：在查清之前把发布默认值改回 0（不设，保持游戏的 3 帧队列），
  再做受控的 ABAB（同一时段、机器空闲、记录全机 CPU），降速局要带栈采样；GPU 受限的对比留给维护者在中端显卡上，或把分辨率和画质拉到最高时测。
- 第 4 步的前提不变：不改成翻转模型 + ALLOW_TEARING，上屏就被刷新率卡住；60 Hz 显示器上不限帧率只能降低输入延迟，画面不会更顺。

### C. 联机（第一批起；Coop 仓库部分在 Coop 自己的 worktree）

- 先修：net-2 `extensionFault_` 粘滞（一次积压后本关插件联机全失效，10-30 反馈未修的那条根因）；net-3 NetId 解析每帧缓存；
  net-4 节奏改用 QPC / tick 而非 `GetTickCount64`；net-7 / net-8 收包循环批量加锁、通道更新去重。
  - 2026-11-24 已做（不升线协议；Coop 分支 `claude/1124-ext-fault-recover`，AF 分支 `claude/opt-net-6447d4`）：
    - net-2：改前，任一条链路积压超 1 MiB / 8192 包或收件箱超 4096 条就把 `extensionFault_` 置死，本房间余下时间插件联机全断。
      改后，溢出等同一次就绪闪断：清收件箱、换 generation（AF 照常挂起 → 恢复，房主恢复开新 epoch，各客人通道经 RoomWatch 重置）；
      积压期间保持未就绪，各参与链路降到 ≤512 KiB / ≤4096 包后自动恢复，不再锁死。能力位 bit 2 声明此契约。
    - net-7：改前，每帧最多 512 次 `extensionPoll`，每次拿 DirectNet 的 `mu_` 并 refresh 一次；AF 每条清零 1 KB、strcmp 查对端。
      改后，新可选导出 `EDF6CoopPollExtensionBatch`（能力位 bit 3）：一次加锁取 64 条，发送者直接给 `api.peer` 下标；
      AF 一帧通常一次加锁（满 64 条才再取）。
    - net-8：改前，收包前、收包后各跑一遍全部通道 update。改后，收包前那遍只在房间（就绪 / epoch / 名册 / 本机身份）变了时跑，
      平常每帧一遍。未做：怪物操控每条消息仍 Publish 一次、`EnemyControlNetworkFrame` 与 update 的 Tick 仍各一次（要把夹具里
      「收包即可读状态」的断言改成按帧读，单独做）。
    - net-3：改前，每次 NetId 引用解析都完整遍历世界（0x5E0C80）。改后，同一逻辑帧内第一次遍历顺手建 NetId 索引表（开放寻址，
      带帧戳），之后命中直接用，并按 StillNetRef 同样的条件复核（仍在场、同一对象、NetId 未变、存活条件）；未命中仍遍历，
      所以本帧新建的对象照样找得到。
    - net-4：改前，怪物操控姿态 / 输入按 `GetTickCount64` 的 33 ms、重力按 50 / 100 ms 限速；15.6 ms 步长的时钟把两帧读成
      31 或 32 ms，实际约 20 Hz 且不均匀。改后，按逻辑帧（`GameFrame`，System+0xCE0）：输入 / 姿态每 2 帧（30 Hz），
      重力持握 3 帧、飞行 6 帧；超时仍按毫秒。
    - 房间门槛：AF 要求 EDF6Coop 声明能力位 2、3 并导出批量收包，否则拒绝建房 / 进房并提示升级 EDF6Coop。
- 再做（P，升协议，旧版拒绝进房）：net-5 扩展通道加「状态类」不可靠只留最新 + 独立有序域；net-6 删掉已被房间协议覆盖的兼容层
  （kExt 位、PeerHas、名册广播、命令协议 v6/v7 协商）；net-9 怪物操控远端姿态带时间戳插值；net-11 客人间消息不经房主中转（大房间）。
  设计要点（2026-11-24 补）：
  - net-5：Coop 扩展 ABI v2 加 `sendState(generation, peer, key, data)`，按 (发送者, key) 只留最新一份，带每 key 序号，
    不重传、不计入可靠积压（溢出后的恢复门槛只看事务类）；接收端丢弃比已收序号旧的。事务类改用每个扩展 channel 自己的
    ReliableReceiver（独立序号空间），不再排在被升级的游戏包后面。AF `plugin_net` 加 SendState / BroadcastState，先迁怪物操控姿态、
    重力持握 / 飞行、终极打击 keep，逐个通道核对哪些消息可丢；租约、支援、动作批仍走可靠有序。Coop 协议、AF 房间协议各加一。
  - net-6：先确认 `Joins` 只依赖 hello，再删 kExt 功能位（留运行时会变的 kExtVariants）、PeerHas 门、名册广播与版本 0 /
    NoteForeignVersion 分支、命令协议 v6/v7 协商；整份 build 只认 `kAllForcesRoomProtocol`（必要时进房附 build 哈希由房主拒绝）。
    房间协议升到下一号，和 net-5 合成一次升级。
  - net-9：先逆向 0x77C2C0 的 reader 是直接写还是软拉，再定插值层；姿态包加发送端逻辑帧号和线速度，客机按「最新样本 +
    一个采样间隔」插值 / 外推，误差按时间常数收敛（照 Coop `multislot/src/netplayer.*` 的数学与测试）。依赖 net-4（已做，
    发送节奏现在均匀 30 Hz）和 net-5；enemy_control_protocol kVersion 7→8，房间协议加一。
  - net-11：依赖 net-5 和 Coop W1 多路径。状态类走客人直连网状路径，事务类留房主中转；中转的状态类套用 relayBytesPerSecond
    预算和兴趣过滤，超预算丢旧的而不是 fail-closed（不再触发 net-2 的恢复）。只在大房间有收益，排在最后。
- net-10：Havok 多线程下的联机分歧对比（与 B 的 fps-10 同一次实机做）。

### D. 工程（第一批做不碰全局结构的；结构性重构放第二批开头一次做完）

- 第一批：eng-1 check.py 自动传 `EDF6_NATIVE_DLL` / `EDF6_GAME_DIR`（108 个 native 审计现在永远 Skipped）；eng-2 长测试拆尾巴、
  按实测分片；eng-10 统一游戏目录定位；eng-11 = hitch-7 = mem-6 日志收进 common（AutoTurret 改异步写盘 + 轮转）；eng-12 文档交叉引用守卫。
- 第二批开头（会碰几乎所有文件，等第一批合完、其它线暂停时一次做）：eng-3 拆 `crew.h` / Config；eng-4 + mod-2 配置单一事实源
  （一张表生成字段 / 读取 / 校验，热重载移出游戏线程、只解析一次）；eng-7 + mod-5 裸 RVA 集中登记到 `src/edf_rva.h` 加棘轮；
  eng-5 CMake 收拢；eng-6 测试不再 include 生产 .cpp；eng-8 补丁所有权与重叠检测；eng-9 拆 selftest.py。

### E. 插件每帧开销（第二批，按 A 的数据排序）

mod-1 实机复测插件占比；cpu-2 `Readable` 对游戏列表交来的对象用本步存活集合；mod-3 每帧世界快照只建一份；mod-4 AutoTurret 用真实帧时钟、
网格找敌、Relations 收进 common；mod-6 射线 / 敌人登记层拆出 heli.cpp；mod-7 跨钩子交接按帧序号判新鲜；cpu-8 区分每 tick / 每帧；
mod-9、mod-10。cpu-4 插件 worker 池 + parallel-for，cpu-3 / mod-12 士兵 AI「并行决策 + 串行执行」——仅当 A 测得 ThinkHook 仍占大头时做。
cpu-5 原版 AI 热点先找算法问题；cpu-6 Havok worker 数对齐；cpu-7 xgs 池 fork-join 成本先量。

### F. 加载、卡顿与内存（第一批做小项，第二批做大项）

- 第一批：hitch-9 `ModFileIn` 缓存文件清单；hitch-8 VehicleCrew 发布默认 `Debug=0`；mem-7 edf41dragon customCopies 去重释放。
- 第二批：hitch-2 同步加载探测；hitch-3 M 地图 120–220 ms 尖峰、hitch-6 暂停菜单 868 ms / 603 输入 293 ms（靠 A 的慢帧栈定位）；
  hitch-4 + mem-1 + mem-5 预载按房间并集收窄（P：房主汇总下发预载集合）；mem-2 同场档案重复贴图 134 MB；mem-3 任务循环内存涨 9 MB/轮归因；
  mem-4 `.data` 120 MB 静态网络缓冲改按需分配；hitch-10 驱动着色器首用编译（有数据才做）。

### G. 渲染小项（第二批，按 A 的 gpu-2 数据）

gpu-4 混合显卡笔记本跑核显（先核实插件能否影响 exe 导出 / 适配器选择）；gpu-5 Umbra GetData 不带 DONOTFLUSH；gpu-6 蒙皮调色板每 pass 重算；
gpu-7 小物体按屏幕尺寸剔除距离；gpu-8 阴影低一级 LOD（低端机选项）；gpu-10 Umbra resolveVisibility 只量。

## 4. 第一批可并行的会话

| 会话 | 内容 | 主要文件 |
|---|---|---|
| 测量 | A 全部 + 重测基线 | `src/crew.cpp` PerfTick、`tools/perf*`、`tools/rip_sampler.cpp` |
| 呈现链 / 不限帧率 | B1 → B2 | `src/frame_pacing.cpp`、新 `src/frame_gate.cpp`、`src/scope_lens.cpp`（B3 起） |
| 联机修复 | C 的非协议项（Coop 仓库 + 插件 net） | `D:\APP\edf6_directnet`、`src/plugin_net*`、`src/*_net.cpp` |
| 工程与小项 | D 第一批 + F 第一批 | `tools/check.py`、`CMakeLists.txt`（分片）、`common/`、`autoturret/src/plugin.cpp`、`src/mod_file.h` |

第二批开头先由一个会话做 D 的结构性重构，合入后再开 E、F 大项、G 与 C 的协议项。

## 5. 附：逆向工具现状

审查中 `vanilla.sqlite` 报「拒绝访问」（可能被其它进程占用），`vanilla.py refs` 用不了，fps 一路改用 `rg` 搜 `assembly/EDF.dll.asm`。
本次核实的新地址（`0x11995A6` 镜头步进、`0x111D890` SyncInterval 链、`0x111CC20` 忙等栅栏、交换链参数 `0x111B4C0..0x111B53A` 等）
在各线落地时按 CLAUDE.md「逆向」节提交到 edf6-vanilla-re。
