# 性能面板（PerfHud）

对应代码：`src/perf_hud.h` / `src/perf_hud.cpp`（数据）、`src/perf_probe.cpp`（挂钩）、`src/hud.cpp` `PerfPanel`（绘制）。
只支持 Steam 版 `EDF.dll`（TimeDateStamp `0x678CCB46`），下文地址都是 EDF.dll 的 RVA。

## 用法

- `EDF6VehicleCrew.ini` 里设 `PerfHud=1`，游戏运行中保存即生效；与 `Debug` / `PerfProbe` 无关。
- 任务中屏幕左侧（血条下方）显示，每 0.5 秒刷新，数字是这 0.5 秒的平均：
  - 帧率；每帧耗时和这段时间里最差的一帧；每帧走几步（场景每帧最多补 3 步）。帧率 55 以上绿色，30 以上黄色，再低红色。
  - 每步耗时，以及 5 个阶段：AI（`ai_update`）、前置（`pre_update`，子弹扫掠在这里）、Havok、对象（`object_update`）、镜头。
  - 每帧的绘制部分：安全更新（`safe_update`）、渲染命令（`render_command`），以及「其余」。「其余」= 帧时间 − 步数 × 每步 − 绘制两项，主要是等下一个 tick、Present 和主循环里的其他部分。
  - 敌人 / 友军数，与任务脚本的计数方式相同；游戏进程的 CPU（1.0 = 占满一个核）；进程私有内存。
- 菜单、读盘、暂停时不显示（场景不走步就不发布新数据，2 秒后隐藏）。

## 数据来源

- 原版自带分段计时，`System::update` `0x1198DA0` 的每个阶段都被一对调用包住：
  - `0x11AA830`（开始）：rcx 是调用方栈上的区段对象，rdx 是 UTF-16 区段名。`[scope]` 写入 QPC 基准，`+8` 写区段名，`+0x10` 写 QPC − 基准。
  - `0x11AA880`（结束）：`+0x18` 写 QPC − 基准，然后跳到 `0x119F6B0` 交给原版的收集器。
  - 所以 `+0x18 − +0x10` 就是这个阶段的耗时，单位是 QPC tick。
- 挂的是 7 个结束调用点 `kScopeEndCalls`。先调原版，再读区段对象。

  | 区段名 | 开始 | 结束 |
  |---|---|---|
  | `xgs_scene_object_ai_update` | `0x11990E0` | `0x11991B9` |
  | `xgs_scene_object_pre_update` | `0x1199215` | `0x1199323` |
  | `xgs_havok_update` | `0x119933F` | `0x1199362` |
  | `xgs_scene_object_update` | `0x11993C5` | `0x119952E` |
  | `xgs_scene_camera_update` | `0x1199546` | `0x1199696` |
  | `xgs_scene_object_safe_update` | `0x119791F` | `0x1197A35` |
  | `xgs_scene_render_command` | `0x1197A7B` | `0x11987D4` |

  `tests/perf_probe_native_audit.py` 逐个核对：每个结束点之前最近的一次区段调用是开始，开始前面 `lea rdx` 指向的正是这个名字；开始 / 结束函数写 `+8` / `+0x10` / `+0x18` 的那几条指令也都在。
- 挂钩方式与 perf_probe 的其他探针相同（`Chain`，叠在原版调用或别的钩子之上），加载时总是安装。7 个都挂上才显示面板，否则各项加不起来。日志一行 `HOOK perf scopes=7/7`。
- 帧：按主循环的输入采样点（`perf_probe.cpp` 的帧锚点，`0x11A2FF3` 调 `0x1182A10`）计，每帧一次，两次之间的 QPC 差就是帧时间；超过 1 秒的视为读盘或暂停，不计。不用渲染段计帧：录制函数 `0x11978D0` 在视图没有内容时会直接跳到 `0x1198835`，跳过段尾（区段开了没关），渲染段不保证每帧都结束一次。帧锚点不论 Debug 都会装；`DevFrameCap` 仍只在 Debug=1 时生效。
- Debug=1 时每次发布记一行 `PERFHUD frames=… steps=… calls …（7 段各自结束次数）ms …（7 段这 0.5 秒的总毫秒）`，核对面板数值用。实测（射击场，2026-11-28）：每 0.5 秒 85 帧、30 步，7 段计数 30×5 / 85 / 85，每步约 3.4 ms。
- 步：镜头阶段结束一次就算一步（每步最后一个阶段）。发布也在这里做：游戏线程、一步刚走完，此时取队伍计数（`GetTeamObjectCount` 要拿队伍管理器的锁）、进程 CPU 和内存。
- 发布：游戏线程写一份 `Readout`，用 SRW 锁保护；绘制线程只 try 拿锁，拿不到就沿用上一份，从不等待。
- 开销：每步 5 次、每帧 2 次读区段对象。`PerfHud=0` 时只累加，不取计数、不发布。
