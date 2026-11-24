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

#### A 的基线（2026-11-24，main fcaa54c4 + 测量探针，DLL sha256 6bcc3018…）

运行条件：

- 命令：`python tools/perfrun.py <场景>`，Debug=1 加 PerfProbe=1；
- 窗口：进关预热后取 20 s 干净窗口（只看线程 CPU，不挂起线程），之后 20 s 采栈（间隔 2 ms）；
- 机器：RTX 5090，3840x2160 窗口化，60 Hz；
- 原始数据：`tmp/perf/base-*`（不入库）；锚点见 `docs/perf-probe-re.md`。

**先纠正：旧 PERF 行是每「步」的数字，不是每帧。** 插件的计时钩子在玩家 pre-update 里，每步跑一次。

- §1 引用的「stress:30 = 32.8 ms、stress:100 = 77.4 ms」是每步时间。
- npc-ai-design 的「Readable 8.5 ms/帧」也是每步。
- 现在的 PERF 行以主循环的输入采样 `0x11A2FF3` 为帧锚点，按帧计时，步数单独列出。

| 场景 | 活着的敌 / 友 | 帧 ms（fps） | 步 / 帧 | 一步 ms | 帧内：步进 / 绘制录制 / 栅栏 / 其余 app ms | n = 0 帧地板 ms（cpu-11） | 输入到 Present ms（帧本身 / 步进用的采样，最差） | 每帧 draw 提交 / mesh | 进程 CPU（单核 %） | GPU 3D |
|---|---|---|---|---|---|---|---|---|---|---|
| M001 | 0 / 6 | 16.70（60） | 1.00 | 0.72 | 0.72 / 0.12 / 1.92 / 0.18 | 2.24 | 3.0 / 36.4（40.5） | 300 / 180 | 55 | 17% |
| RM015 | 3 / 4 | 16.70（60） | 1.00 | 0.78 | 0.78 / 0.13 / 3.25 / 0.16 | 3.56 | 4.4 / 37.8（66.3） | 425 / 97 | 78 | 24% |
| stress:1 | 174 / 53 | 16.70（60） | 1.00 | 4.30 | 4.30 / 0.45 / 1.42 / 0.24 | 2.12 | 6.5 / 39.8（45.9） | 1733 / 1523 | 117 | 20% |
| stress:10 | 846 / 242 | 18.07（55） | 1.08 | 13.92 | 15.10 / 1.22 / 0.76 / 0.30 | 2.30 | 17.5 / 53.7（101.9） | 2953 / 3618 | 216 | 20% |
| stress:30 | 1880 / 662 | 75.30（13） | 3.00 | 23.29 | 69.86 / 3.43 / 1.56 / 0.40 | 5.42 | 75.4 / 226（306） | 4693 / 6107 | 225 | 9% |
| stress:100 | 2603 / 2132 | 204.27（4.9） | 3.00 | 64.13 | 192.38 / 8.73 / 2.74 / 0.38 | 11.89 | 204 / 613（1173） | 8383 / 14841 | 235 | 5% |

游戏线程每**步**按阶段拆分（采栈份额 × 帧时间 ÷ 步数）：

| 场景 | object_update | ai_update | pre_update | havok_update | 插件 ThinkHook（含 Think / Drive，每步） |
|---|---|---|---|---|---|
| stress:10 | 7.25 | 2.23 | 2.54 | 1.54 | 1.9 |
| stress:30 | 8.57 | 5.18 | 5.20 | 2.98 | 4.7 |
| stress:100 | 23.79 | 19.17 | 9.25 | 4.16 | 17.8 |

stress:100 插件栈里排前面的还有 ShotOk / ShotClearOf / Grid::Capsule，每帧约 12–13 ms。

进关（hitch-5，`--cycle`；从玩家预载到插件各阶段完成 / 到地图载入，单位 s）：

- M001：冷 4.05 / 9.00，热 3.85 / 9.38、4.32 / 8.96；
- stress:30：冷 7.65 / 13.66，热 7.53 / 13.64。
- **热缓存几乎不快**：大头是插件自己的阶段。support soldiers 每次 2–3.8 s，debug spawn 每次约 3.6 s，support soldier
  weapons 约 1.4 s，与缓存无关。F 线先看这几个阶段。
- 进关后已提交内存：M001 约 3.55 GB，stress 约 6.3–6.8 GB。

读法和后续取舍：

1. **重场景纯粹卡在游戏线程的步进。**
   - stress:30 / 100 每帧都补满 3 步，模拟只剩 40 / 15 步每秒，即原速的 66% / 25%。
   - 游戏线程占 86–87% 核；GPU 3D 只有 5–9%（轻场景 17–24%）。
   - 绘制录制每帧 3.4 / 8.7 ms，只占帧时间的 4–5%。
   - E 线（插件每帧开销）和原版 object_update / ai_update 是主战场。
2. **n = 0 帧地板低（cpu-11）。** 轻场景 2–3.6 ms（不含步进和 tick 等待），其中 1.4–3.3 ms 是栅栏忙等 `0x111CC20`。
   B 线的不限帧率可行，但应先把这个忙等改成真正的等待，否则高帧率时它会吃满一个核。
3. **输入到画面的延迟。** 60 fps 下画面用的是两帧前的采样，约 36–40 ms。stress:30 涨到 226 ms，stress:100 涨到
   613 ms，最差超过 1 s。latency 线的收益在轻场景；重场景只能靠减步进时间。
4. **GPU 提交量（gpu-2）。** pass 3 和 mesh draw 随单位数近线性增长：stress:100 每帧约 1.5 万次 mesh draw，但 GPU 只占
   5%，不是瓶颈，G 线优先级低。
5. **还没测到的：**
   - Umbra resolve 调用点 `0x11B6B1D` 在这些场景里计时恒为 0：resolve 不在这条路径上。umbra_sandlot 的栈叶出现在另一条
     线程上，要另找。
   - 联机（net-1）和 PresentMon（latency-2）这一轮没有数据：离线跑，PresentMon 需要管理员权限。工具已就绪：
     `--presentmon`，以及 coop.log 自动保存。

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

#### G 的结论（2026-11-24，分支 `claude/opt-gpu-1124`）：gpu-4 / 5 / 6 / 7 都不改代码

**共同前提：渲染线程不在关键路径上（A 基线，`tmp/perf/base-*`）。**

- 渲染线程只占一个核的 10.9%（M001）、17.3%（RM015）、19.5%（stress:30）、16.5%（stress:100），而且其中 25–63% 的
  样本停在空闲等待（ntdll `0x51e90`，栈底是渲染线程入口 `0x111B8E0`）。
- 游戏线程在 stress 场景等渲染栅栏的时间：
  - stress:30 每帧 75.3 ms 中只有 1.56 ms（2.1%）；
  - stress:100 每帧 204 ms 中只有 2.74 ms（1.3%）。
  - 渲染线程省下的任何时间，最多也只能从这一段里扣，所以重场景的帧率收益上限就是这 1–2%。
- 轻场景已经锁在 60 fps，被 tick / vsync 卡住，渲染线程省时间不会变成帧率。
- 只有 B 线的不限帧率做完、轻场景的帧地板变成 n = 0 的 2.2–3.6 ms 时，渲染线程才可能上关键路径。
  - 那时地板里的 1.4–3.3 ms 也是栅栏忙等（gpu-1，B 线处理），不在 G 线。

**gpu-5（遮挡查询 GetData 不带 DONOTFLUSH）：不做。**

新探针 PERF GPU 直接数了 `0x1631790` 的每次调用（锚点见 `docs/perf-probe-re.md` §2）：

| 场景 | 每帧读取 | 未就绪 | 带等待 | 合计 ms/帧 | 单次最差 ms | 栈份额（渲染线程内，A 基线） |
|---|---|---|---|---|---|---|
| M001 | 10.6 | 0.0 | 10.5 | 0.073 | 1.63 | 1.3% |
| RM015 | 未实测 | | | | | 5.5% |
| stress:30 | 未实测 | | | | | 3.1% |
| stress:100 | 未实测 | | | | | 1.7% |

- 结果**从没出现未就绪**：DONOTFLUSH 只影响未就绪时的 Flush，而这条路上根本不触发 Flush。改了也省不下东西。
- 几乎每次都带等待，但等待从不真的发生，因为结果总是已经就绪。审计担心的「自旋最长 1 s」在这些场景里没有出现。
- RM015 和 stress 场景按原则没有再开游戏（M001 已是一次实机），原因：
  - A 基线里 `0x1631790` 在 stress 渲染线程内只占 1.7–3.1%，乘上渲染线程本身 16–20% 的核占用，不到 0.6% 核；
  - 渲染线程又不在关键路径上。
- 何时值得重做：
  - PERF GPU 里「not in yet」或「waiting」的耗时明显上升时，比如某类场景每帧超过 0.5 ms，或单次最差超过一帧；
  - 并且渲染线程已在关键路径上（不限帧率）。
  - 那时先查是哪类查询要求同步，从源头改，不加超时。

**gpu-6（蒙皮调色板每 pass 重算）：不做。**

- M001 每帧 92 次，合计 0.08 ms/帧。
- 在 A 基线的 stress:30 里，LOD 绘制 `0x1104100`（含调色板）只占渲染线程样本的约 10.5%，约合 2% 核，而且不在关键路径上。
- 缓存方案要把第一人称按 pass 改姿态的逻辑算进缓存键里（fp-render-re.md §2、§17），风险中等，换不来可见收益。
- 何时值得做：不限帧率时，同屏蒙皮单位多的场景里渲染线程忙到接近满核，PERF GPU 的 `skin palettes` ms 占帧地板的 10% 以上。

**gpu-7（小物体按屏幕尺寸剔除）：不做。**

- 它省的是渲染线程和 GPU。GPU 3D 在 stress 只有 5–9%，渲染线程不在关键路径上，所以现在剔除碎片换不来帧率。
- 它还会带来可见变化：远处小物体提前消失，要按 view-distance-re.md 回归远景。
- 何时值得做：
  - GPU 成为瓶颈时，比如低端显卡、4K 不限帧率时 GPU 3D 接近 100%；
  - 或者 PERF RENDER 的 pass 3 / mesh draw 在实际关卡里以碎片和道具为主时。
  - 那时再按 pass 计数定系数 k，并截图对比远景。

**gpu-4（混合显卡笔记本跑核显）：插件改不了，不做。**

- 插件确实在建设备前加载（aiworld-re.md:60、cri-fs-loaders-re.md:61），时机来得及，但有两条路都走不通。
- 一，导出符号：
  - NvOptimusEnablement / AmdPowerXpressRequestHighPerformance 按 NVIDIA / AMD 的说明只认**进程 exe** 的导出，
    DLL 里导出不起作用。
  - EDF6.exe 没有导出表。
  - 这一条依据的是厂商文档和社区经验，本机没有混合显卡，无法实测。
- 二，改调用点 `0x111B595`，用 EnumAdapterByGpuPreference(HIGH_PERFORMANCE) 选适配器：
  - 游戏把设备所在的适配器（cmdctx+0xF0，`0x111B65E` 取得）也当作输出所在的适配器。`0x111A64F` 在它上面
    EnumOutputs 建显示模式列表，失败就退到 1280x720 加空列表（`0x111A98D`）。另外还有 5 处用到 +0xF0（perf-probe-re.md §2.1）。
  - muxless Optimus 笔记本的独显通常没有输出（推测，未在实机验证）。强制选独显会弄坏模式列表和全屏。
  - 要做对，就得把这 6 处输出相关的用法都改到「有输出的那块适配器」上。这是结构性改动，而且本机只有 RTX 5090、没有核显，没法验收。
- 三，没有受影响的证据：
  - 测试站 51 份反馈 / 日志（只当证据读）里，能看到渲染适配器的是 ReShade 日志：「Running on NVIDIA GeForce GTX 1050」
    （同一用户的网卡是 Intel Wi-Fi，像笔记本，已经跑在独显上）和 RTX 4060。
  - 没有任何一份显示 Intel / AMD 核显在渲染。
- 可行的替代（未做，属于文档）：有用户反馈笔记本帧率异常低时，让他在 Windows「图形设置」里把 EDF6.exe 设成「高性能」。
  这是系统级设置，作用于 exe，不需要插件。
- 何时重做：测试站出现核显渲染的日志，并且有混合显卡机器能验收。届时连同 +0xF0 的 6 处输出用法一起改。

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
