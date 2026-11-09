# 任务进出循环与 Havok 线程池复位点（2026-11-08）

目的：在一次游戏启动里反复「进任务 → 离开 → 再进同一任务」，走到 Havok 线程池的两个复位点，实测
`HavokMultithread` 补丁（分支 `perf/1108-havok-mt`，`docs/havok-mt-re.md` §9 / §12）的复位顺序不会死锁。
地址都是 EDF.dll（TimeDateStamp `0x678CCB46`）的 RVA。脚本行号按 Root.cpk 的 `MAINSCRIPT/MAINSCRIPT.AS`
（`pylib/rootcpk.py` 取出）。标记：H 为反汇编或脚本原文直接证实，U 为未实测。

## 1. 两个复位调用点分别由什么流程触发

两个函数都是注册给主脚本的原生函数（AngelScript `RegisterGlobalFunction`），只经函数指针调用，所以找不到直接的
E8 / E9 调用者。

| 复位点 | 所在函数 | 身份（注册处） | 何时调用复位 `0x118F0A0` |
|---|---|---|---|
| `0x70E38E` | `0x70E330` | 脚本函数 `void FreeResourceGroup(int)`（`0x71D094` 取地址，声明字符串 `0x17EAD70`） | 只在参数为 2，并且线程上下文对象 vt+0x1A8 返回 0 时：`0x7A2EE0(*(0x20B29A8), 2)` 释放资源组 → `0x112A750` → `0x119290(*(0x20B2990))` → `0x118F0A0(*(0x20B30A0))`（H） |
| `0x70FEC6` | `0x70FE00` | 脚本函数 `void SystemReset()`（`0x71C246` 取地址，声明字符串 `0x17EA990`）；另由 `0x718B50` 尾跳进入 | 每次调用都复位（H） |

### 1.1 `FreeResourceGroup(2)`：离开任务（正常游戏里唯一的复位路径）

脚本 `UTILITY.H` 的 `g_resource.FreeGroup(g)` 先 `Wait()`，再调 `FreeResourceGroup(g)`。`COMMONTYPES.H` 定义
`RESOURCE_GROUP_TITLE=0, MODE=1, SCENE=2`，`COMMONTYPES.INNER.H` 定义 `SYSTEM_GROUP_SCENE=2`，因此 2 就是
「场景组」（H）。`MAINSCRIPT.AS` 里释放场景组的位置：

| 行 | 所在例程 | 时机 |
|---|---|---|
| 211 | `InitializeAll()` | `StartToTitle`、`Invite`、`ActivityBoot`、`Debug_Start*` 开头，即回标题时 |
| 561 | 公司 logo 结束 | 启动时 |
| 1136、1138 | `PlayMission_Offline()` | `Before()`（任务前演出）的前后 |
| 996 | `PlayMission_Common()` 开头 | 进任务加载前 |
| **1058** | `PlayMission_Common()`，`Mission()` 循环结束后 | **任务结束（通关、撤退、失败退出）后释放游戏场景** |

插件的 `BeforeReset` 只在线程池 `+0x140` 非空，也就是 worker 已经接上时才执行 close 和等待并写日志。一局任务里
worker 在第一次物理步进时接上，所以带着 worker 走到复位的就是第 1058 行：任务一结束，场景组随即释放。进任务前的
996、1136、1138 行同样会调用复位，但那时 worker 还没接上，插件不会动作（H，按 `havok-mt-re.md` §12 的条件推断）。

注意：「重新开始」（`MISSION_RESULT_RETRY`）在 `PlayMission_Common` 的 `for(;;){ result = Mission(); if(result != RETRY) break; }`
里直接再跑一次 `Mission()`，中间**不释放场景组**，所以重开不会经过复位点（H）。

### 1.2 `SystemReset()`：正常游戏里走不到

- 脚本里只有 `ReBoot()`（第 185–206 行）调用它：先停音乐、`WaitJob()`，再 `SystemReset()`，然后 `return "Boot"`（H）。
- 脚本里没有任何例程返回 `"ReBoot"`。原生代码按名字启动它（`0x718B70`，字符串 `0x17F1810`）的地方只有两处（H）：
  - `0x7C70F0`：`Sys_Warning`、`Sys_WarningNotFoundOwnership`、`Sys_Warning_DeviceError_*` 系统警告的处理，由
    `Application` 的 vt 槽 1（`0x705530`，位置 `0x705996`）调用。也就是所有权校验失败、设备错误之类的系统级重启。
  - `0x7C7000`：没有直接调用者。
- `0x718B50` = `0x718830` 后接 `SystemReset`，被 `0xB1980` 和 `0xB7B70` 调用。这两个函数会重新编译
  `app:/MainScript/MainScript.as` 并启动指定例程，是 `0xA1F90` 里开发用启动参数 `main_script`、`main_script2`、
  `boot_script` 的 lambda（H）。

结论：玩家正常操作（撤退、通关、回 HQ、回标题）只经过 `0x70E38E`。`0x70FEC6` 只在系统警告重启或开发启动参数下
出现。要测它，只能让脚本走一次 `ReBoot`（见 §3 的 `reboot` 选项）。

## 2. 离开任务：暂停菜单的「撤退」

- `HUiPause` 的 vt 槽 1（`0x935210`）是确认后的处理（H）：
  - `[+0x79C] == 1` 时调用 `MissionMgr::End(mgr, r)`（`0x935340`）。房间（`[+0x798] == 2`）的 `r` 是 4（LOBBY），离线是 2（ABORT）。
  - `[+0x79C] == 2` 时调用 `End(mgr, 3)`（RETRY），再调 `0x8D4140`。
- `MissionResult`（`COMMONTYPES.INNER.H`）：UNKNOWN 0、CLEAR 1、ABORT 2、RETRY 3、LOBBY 4、RETURNTITLE 5（H）。
- `MissionMgr`（`mgr = *(EDF+0x20B28A8)`，RTTI `MissionMgr`）（H）：
  - `End` `0x1F67F0`：`mgr+0x54 = r`，然后调 `impl->vt[3](r)`，最后调 `0x7E3590(2)`。impl 是 `mgr+0x40`；BVM 实现
    `0x220B40` 写 `+0x12C = r`、`+0x128 = 3`，并停掉声音等。
  - `IsRunning` `0x1F68B0`：impl 非空且 `vt[6]()`（BVM 读 `+0x128`）不等于 3。
  - `Update` `0x1F6F40`：impl 未结束时调 `vt[4](dt)`，即任务脚本每步的更新。调试标志下它自己就会 `End(1)`，所以
    在这里结束任务有原版先例。它由 `Application::OnWorldStepBegin`（`0x705A60`，`Application` vtable 槽
    `EDF+0x17E91E0`，只经 vtable 调用）每个世界步调用一次；同一函数里还驱动主脚本（`0x718CA0(*(0x20B29A0))`）。
- 脚本函数 `int Mission()`（`0x70F0B0`）：第一次调用时加载 `mission.bvm`（`0x1F68D0`）。之后 `IsRunning` 为真就让出，
  否则返回 `0x1F6820`（`vt[7]`，BVM 读 `+0x12C`）的结果（H）。

因此「撤退」在原生层就是 `End(mgr, 2)`。之后 `Mission()` 返回 ABORT，`PlayMission_Common` 第 1058 行释放场景组，
复位点 `0x70E38E` 随之执行。接着 `PlayMission_Offline` 返回 `"Result_Offline"`。

`Result_Offline` → `Result_Common`（第 1395 行起）依次做 `ApplyResult`（把结果写进存档数据）、
`Save(false, "Contents_AutoSaveRetry", true)`（**写用户存档**）、`ui_handle.WaitCloseRequest()`（等玩家按键关闭结算画面），
最后 `return "HQMain"`。脚本里被注释掉的 `// return "HQMain";`（第 1165 行）说明开发时也这样跳过过结算。

## 3. autopilot 的 `cycle` 命令（`tests/autopilot/autopilot.cpp`）

`cycle N [STAY] [reboot]`：可以和 `mission` 一起在启动前下发，也可以在任务里下发。

1. 钩住 `Application::OnWorldStepBegin` 的 vtable 槽（`edf::ChainVtableSlot`）。挂钩前先核对 `End`、`IsRunning`、
   `OnWorldStepBegin` 的开头字节，不符就记 `CYCLE refused`，不挂。
2. 每个世界步在原函数之后（任务脚本这一步已经更新完，与 `MissionMgr::Update` 自己结束任务处于同一位置、同一线程）
   检查：任务第一次 `IsRunning` 时记 `CYCLE i/N entered`；运行满 STAY 秒（默认 20）后记 `CYCLE i/N leaving`，再调
   `End(mgr, 2)`，与暂停菜单撤退的离线分支相同。这一步不依赖窗口、键盘或菜单。
3. 主脚本下一个例程是 `Result_Offline` 时，改为启动 `PlayMission_Offline`，同一行、同一难度再进一次（GS+0x48 / +0x4C
   不变；`ProceedNextMission` 只在被跳过的 `Result_Common` 里）。这样不出结算画面，也**不写存档**；记
   `CYCLE i/N left`。任务若自己结束（死亡后选退出、通关），同样计一次 left，并注明 `ended on its own`。
4. 第 N 次 left 之后再进一次，进去后记 `CYCLE done`，之后照常 `quit`。如果脚本中途回到标题（`StartToTitle`，例如
   `MISSION_RESULT_RETURNTITLE` 或 EOS 断线），记 `CYCLE failed`。
5. `reboot`：第 3 步改为启动 `ReBoot`（`SystemReset` → `0x70FEC6`），并重新设好行号、难度和模式包，让 `StartToTitle` →
   `Debug_StartOffline` → `HQMain` 再按 `mission` 命令的路线进同一任务（U：没有实机跑过）。

跳过的部分：结算画面与存档（离开任务和复位都发生在这之前，跳过不影响测复位），以及 HQ 菜单（`HQMain` 只做 UI
资源进出，进任务前 `PlayMission_Offline` 的 1136 / 1138 行和 `PlayMission_Common` 的 996 行照常释放场景组）。

`drive.py run … --cycle N [--cycle-stay S] [--cycle-reboot]`：把命令和 `mission` 一起交给插件，看到 `CYCLE done` 就
退出游戏；SECONDS 是整个循环的期限，超时或出现 `CYCLE failed` / `CYCLE refused` 时返回 1。

## 4. 预期日志（U：待主会话实机）

`EDF6Autopilot.log`：

```
CYCLE armed: leave and enter again 20 times, 20.0 s in each mission, back in by PlayMission_Offline
CYCLE 0/20 entered: the mission runs (row 0)
CYCLE 1/20 leaving: MissionMgr::End(ABORT), as the pause menu's withdraw, after 20.0 s (game thread …)
CYCLE 1/20 left: Result_Offline skipped (no result screen, nothing saved) -> PlayMission_Offline
CYCLE 1/20 entered: the mission runs (row 0)
…
CYCLE 20/20 entered: …
CYCLE done: left and entered again 20 times
```

EDF6VehicleCrew 日志（`HavokMultithread=1`）：每次 `leaving` 之后出现一条
`HAVOKMT reset: queue … closed and the workers back in X ms (detach #n)`，对应第 1058 行释放场景组；下一次
`entered` 之后的第一次物理步进出现下一条 `HAVOKMT … attach`。整轮应有 N+1 次 attach（首局加 N 次再进）和 N 次复位
detach（最后一局由 `quit` 关窗口结束，走析构路径，不经过复位点）。如果某次 `leaving` 之后既没有 `left` 也没有 `reset`，游戏卡住、
drive.py 超时，就说明复位顺序有问题；这时游戏会留在运行状态，由主会话抓 dump。
