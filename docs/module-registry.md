# 模块注册表（Install / Reset / Tick / 载具步骤一处登记）

来由：[design-review-1126.md](reviews/design-review-1126.md) 第 4 步（P2、P3）。插件的生命周期以前靠三处手写清单：`plugin.cpp` 的 `EML6_Load` 里约 120 个 `Install*`，`mission.cpp` 的 `MissionStart` 里约 70 个 `Reset*`，`crew.cpp` 的 `SharedFrameTick` / `InputHook` 里 78 处 `Guarded*`。故障 ID 有复用，漏写 Reset 也没有任何检查能发现。现在这几处只调用注册表的遍历，自己不再列出任何功能。

## 文件

| 文件 | 内容 |
|---|---|
| `src/module_logic.h` | 纯逻辑，不依赖游戏：各遍的阶段枚举、规则检查（constexpr）、故障策略 `FaultStreak`。`tests/module_registry_test.cpp` 只 include 它 |
| `src/module.h` | 对外接口：`InstallAll` / `ResetAll` / `RunTicks` / `RunVehicle`，以及 `ResetModule`、`ClearFaults`、`ModuleNotice` |
| `src/module_table.h` | 表本身：`EDF6_MODULES` 行（`M(id, 名字, stateless)`），以及 `kInstalls`、`kResets`、`kTicks`、`kVehicleSteps` 四张表和对它们的 `static_assert` |
| `src/modules_load.cpp` | 装载和任务开始这两遍（`InstallAll`、`ResetAll`、`ResetModule`） |
| `src/modules.cpp` | 逐帧这两遍（`RunTicks`、`RunVehicle`），每条在所属模块的故障保护下执行，另含故障策略 |

调用方：`plugin.cpp EML6_Load` → `modules::InstallAll(context)`；`mission.cpp MissionStart` → `modules::ResetAll()`；`crew.cpp SharedFrameTick` → `modules::RunTicks()`；`crew.cpp InputHook` 在各接缝处调用 `modules::RunVehicle(阶段, …)`。

## 模块与各遍

- 一个模块就是 `EDF6_MODULES` 里的一行，它的 `Id`（行号）就是它的故障 ID，`名字` 会出现在故障日志和 HUD 提示里。每个模块各有一个 ID，不再共用。
- 各遍按阶段执行，阶段相同时按表中顺序。阶段表达的是「谁必须在谁之前」，用来替代原来的注释：
  - 安装（`InstallPhase`）：`kCrashDump`（第一个）→ `kBeforeGameStatus` → `kBeforeAppInit` → `kPhysics` → `kHeliProfile`（只在直升机 profile 检查通过时安装）→ `kFeatures` → `kMission` → `kLast`。
  - 重置（`ResetPhase`）：`kReleaseHolds`（先放开插件占着的游戏对象：被控敌人的租约在原生输入恢复之前，PvP 队伍在它们之后）→ `kMissionState`。联机通道的 `plugin_net::Channel.reset` 也并进了这一遍（`kPluginNet` 模块），每个任务只跑一次。
  - 帧 tick（`TickPhase`）：`kWorld` → `kBeforeHudPublish` → `kHudPublish` → `kAfterHudPublish`。
  - 载具步骤（`VehiclePhase`，对应 `InputHook` 的接缝）：`kBeforeGears` → `kBeforeStockInput` →（原版输入）→ `kAfterStockInput` → `kSound` → `kAfterSound` →（Enabled 判断、共享帧 tick）→ `kFrame`。
- 带 `kOff`（`kRunsWhenDisabled`）标记的载具步骤，在插件关闭（ini `Enabled=0`）时也会执行：它们负责把自己改过的东西还回去，或者本身是时钟。

## 重置规则（编译期检查）

- 有状态的模块（`stateless` 为 `nullptr`）必须恰好有一条重置条目：上一关留下的对象地址，到下一关可能已经是别的对象了。
- 无状态的模块不能有重置条目，必须写明理由（非空字符串）。只在装载时安装、本身不留每关状态的模块，理由统一写 `kLoadOnly`。
- `module_table.h` 用 `static_assert(FirstResetBreak(kModules,kResets)==kModuleCount, …)` 在编译期检查这条规则，违反就编不过。`tests/module_registry_guard.py` 再从源码读一遍表，按模块名报出违反者。

## 加一个功能

1. 在 `EDF6_MODULES` 里加一行：有状态的写 `nullptr`，没有状态的写理由。
2. 在对应的表里加条目，写明阶段。
3. 有状态就在 `kResets` 里加它的重置。
4. 不要在 `plugin.cpp` / `mission.cpp` / `crew.cpp` 里直接调用；守卫会拦（见下）。

## 故障策略

以前的做法：某一步出了异常就记一条日志（每步每 10 秒最多一条），这一次跳过，下一帧照常重跑。写到一半就出错留下的半截状态，会被后面的帧继续读写，而且没有任何提示。

现在的做法（`module_logic.h FaultStreak`，`modules.cpp`）：

- 一条 tick 或载具步骤出了异常：这一次跳过，其他条目照常执行；日志 `FAULT <模块>: <代码> at EDF+<RVA> (…)` 仍按模块限频。
- 同一条目连续 `kFaultLimit`（3）个游戏帧出错，整个模块在本局剩下的时间里停用，它的所有条目都不再执行：
  - 停用的同时执行一次该模块的重置条目，每条都在保护下执行（出错的正是它的状态）：丢掉写了一半的状态，放开它占着的游戏对象，和任务开始时一样。重置本身出错也会记日志，模块仍然保持停用。
  - 写一条 `MODULE <模块> off for the rest of the mission: <表>[<序号>] faulted in 3 frames in a row (… the last <代码> at EDF+<RVA>); <its state reset | its reset faulted too | it keeps no state>; …`，并立即 `FlushLog()` 落盘：之后游戏即使崩溃，这一行也在。这就是这次故障的报告，没有另写 minidump。
  - HUD 在打击呼叫提示下方显示 8 秒的琥珀色提示（`hudtext.inc moduleOff`，四种语言），说明哪个模块因为连续出错在本局停用了，详情见日志。
- 计数规则：一帧里只要出错一次就算这一帧出错。载具步骤每帧对每辆载具各跑一次，所以只要有一辆载具每帧都出错，也算连续出错。该条目运行过、但整帧都没出错的帧会打断连续计数。该条目根本没有运行的帧，既不计数，也不打断。
- 下一局开始时，`ResetAll` 跑完所有重置后调用 `ClearFaults()`：所有模块重新启用，计数清零。
- 不受保护的部分：安装和任务开始时的重置照旧，不包故障保护：这两处出错就是安装坏了，或者插件有缺陷，让崩溃转储记下来，不吞掉。

## 守卫

- `module_registry_test`（ctest）：在夹具表上检查重置规则、阶段顺序、ID 范围、未用模块，以及 `FaultStreak` 的各种情形。
- `module_registry_guard`（ctest，Python）：
  - 重置规则：按模块检查；
  - `mission.cpp` / `plugin.cpp` 里没有裸的 `Reset*()` / `Install*()` 调用（唯一例外：`mission.cpp` 的 `ResetRoomUi`，它属于 `MissionSyncHook`，不在 `MissionStart` 里）；
  - `InputHook` 按枚举顺序执行各阶段，原版输入夹在 `kBeforeStockInput` 和 `kAfterStockInput` 之间；
  - 故障策略的接线。
- `selftest.py` 和 `tests/*_guard.py` 里原来读 `plugin.cpp` / `mission.cpp` / `InputHook` 文本的断言，改成通过 `registry()` / `registry_index()` / `installs()` / `resets()` / `ticks()` / `vehicle_step()` / `vehicle_phase()` / `runs_when_disabled()` 读注册表。
