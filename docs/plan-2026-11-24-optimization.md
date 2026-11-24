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

### C. 联机（第一批起；Coop 仓库部分在 Coop 自己的 worktree）

- 先修：net-2 `extensionFault_` 粘滞（一次积压后本关插件联机全失效，10-30 反馈未修的那条根因）；net-3 NetId 解析每帧缓存；
  net-4 节奏改用 QPC / tick 而非 `GetTickCount64`；net-7 / net-8 收包循环批量加锁、通道更新去重。
- 再做（P，升协议，旧版拒绝进房）：net-5 扩展通道加「状态类」不可靠只留最新 + 独立有序域；net-6 删掉已被房间协议覆盖的兼容层
  （kExt 位、PeerHas、名册广播、命令协议 v6/v7 协商）；net-9 怪物操控远端姿态带时间戳插值；net-11 客人间消息不经房主中转（大房间）。
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
