# 游戏本体的两处等待：窗口线程空转与帧步进 Sleep(1)

2026-11-07。对象是 Steam 版 EDF.dll（`0x678CCB46`），实现在 `src/frame_pacing.cpp`，签名审计在 `tests/frame_pacing_sig_audit.py`。

## 怎么找到的

`tools/rip_sampler.cpp` 是进程外的采样器，不需要管理员权限，也不用 ETW。每个 tick 只采本 tick 用过 CPU 的线程：挂起线程，用 dbghelp `StackWalk64` 按目标进程的 .pdata 回溯栈，再恢复。`tools/rip_analyze.py` 用 EDF.dll 的 .pdata 把地址归到函数。

在 M001 里用 `tests/autopilot/drive.py` 后台挂机，采样 90 s（装着本插件与 EDF6Coop）：

| 线程 | CPU（90 s 内） | 在做什么 |
|---|---|---|
| 窗口线程 | 82 s | `0x1184460` 循环：72% 的样本在 `Sleep(0)`，其余在 `PeekMessage` 与 EOS 覆盖层的钩子里 |
| 游戏线程 | 16 s | 帧步进：`Sleep(1)` 与等渲染线程的栅栏（`0x111CC20`） |
| 渲染线程 | 11 s | 大部分时间等游戏线程的命令（`0x51EC2`）与驱动 |

插件自身（EDF6VehicleCrew）只占全部样本的 0.8%。

## 窗口线程：`Sleep(0)` 空转

```
0x1184480: switch([rdi+0x18]) { 0: 0x1183470  1: 0x1183880（游戏中）  2: 初始化 }
0x11844E8: xor ecx,ecx; call [Sleep]          ; Sleep(0)
0x11844F0: cmp byte [rdi+0x29],0; je 0x1184480 ; 没退出就继续
```

`0x1183880` 做的事：`PeekMessage` / `Translate` / `Dispatch` 抽干消息队列；`GetKeyboardState` 存到 `0x21360E0`（游戏线程的 `0x1188270` 读，见 `keyconfig-re.md`）；前台与窗口检查；锁鼠标时用 `GetCursorPos` 取光标偏离窗口中心的量累加进 `0x1FEEE20`，再 `SetCursorPos` 拉回中心（游戏线程的 `0x11831F0` 取走后清零，见 `camera-re.md`）。

`Sleep(0)` 只把 CPU 让给同优先级的就绪线程，没有就立即返回，所以这个线程从开游戏到关游戏一直占满一个核。

改法：`0x11844EA` 的 `call [Sleep]` 改为调 `PumpWait`，用 `MsgWaitForMultipleObjectsEx(高精度定时器 1 ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE)`。来了输入或消息立即醒，否则最多睡 1 ms。鼠标偏移是累加量，键盘状态游戏线程每帧读一次，所以游戏读到的值不会变晚，也不会丢。

## 游戏线程：帧步进 `Sleep(1)` 被系统时钟取整

```
0x11A2F4F: call 0x11DAE60 ; n = 距上一帧过了几个 1/60 s（QueryPerformanceCounter；period = freq*100/6000）
0x11A2F58: jne 0x11A2F76
0x11A2F60: mov ecx,1; call [Sleep]   ; Sleep(1)
0x11A2F6B: call 0x11DAE60; test eax,eax; je 0x11A2F60
0x11A2F76: n = min(n, 3) ...         ; 这一帧推进 n 个 tick
```

EDF.dll 没有导入 `timeBeginPeriod`，也没有导入 `NtSetTimerResolution`。从 Windows 10 2004 起，计时器分辨率按进程生效，所以这里的 `Sleep(1)` 要睡到下一个系统时钟 tick（默认 15.6 ms）。帧的等待因此被取整到 15.6 ms 的网格上：平均仍是 16.7 ms，但约每 5 帧就有一帧要两个 tick（约 32 ms），这就是 60 fps 下的顿挫。

改法：`0x11A2F65` 的 `call [Sleep]` 改为调 `TickWait`，在高精度定时器（`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`）上等 1 ms，不改整个进程的计时器分辨率。

## 实测（2026-11-07，M001，难度 1，后台挂机，A/B 交替各 2 次）

A 与 B 是同一份源码；A 不调用 `InstallFramePacing`。PERF 来自插件的 `Debug=1`，统计进入任务 10 s 之后、每 5 s 一行，共 26 行，约 7800 帧。

| | 超过 20 ms 的帧 | 每 5 s 最差帧（中位数） | 进程 CPU（75 s 内） |
|---|---|---|---|
| A 原版等待 | 19.0% / 18.0% | 31.8 / 31.9 ms | 110.8 / 108.7 s |
| B 改后 | 0.03% / 0.05% | 18.3 / 18.3 ms | 36.0 / 36.6 s |

内存不变：两者任务中提交约 4.1 GB（峰值 4.5 GB），工作集约 0.5 GB。

## 没有改的

- `0x111CC20`（游戏线程等渲染栅栏时也抽消息、`Sleep(0)`）：采样里它主要是 `WaitForSingleObject` 真等待，不空转。
- 提交内存约 4 GB，其中常驻 RAM 只有 0.5–0.9 GB，其余是游戏预先提交、没有碰过的堆，只占提交额度、不占内存，不值得动。
