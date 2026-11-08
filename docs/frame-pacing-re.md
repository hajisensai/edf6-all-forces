# 游戏本体的两处等待：窗口线程空转与帧步进 Sleep(1)

2026-11-07。对象是 Steam 版 EDF.dll（`0x678CCB46`），实现在 `src/frame_pacing.cpp`，签名审计在 `tests/frame_pacing_sig_audit.py`。

## 怎么找到的

`tools/rip_sampler.cpp` 是进程外的采样器，不需要管理员权限，也不用 ETW。每个 tick 只采本 tick 用过 CPU 的线程：挂起线程，用 dbghelp `StackWalk64` 按目标进程的 .pdata 回溯栈，再恢复。`tools/rip_analyze.py` 用 EDF.dll 的 .pdata 把地址归到函数。之后的性能实测用 `tools/perfrun.py`（README「性能实测」）一条命令完成：场景、采样、报告、还原。

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
0x11A2F4F: call 0x11DAE60 ; n = 距上一个 tick 边界过了几个整 tick（QueryPerformanceCounter；period = freq*100/5994）
0x11A2F58: jne 0x11A2F76
0x11A2F60: mov ecx,1; call [Sleep]   ; Sleep(1)
0x11A2F6B: call 0x11DAE60; test eax,eax; je 0x11A2F60
0x11A2F76: n = min(n, 3) ...         ; 这一帧推进 n 个 tick
```

EDF.dll 没有导入 `timeBeginPeriod`，也没有导入 `NtSetTimerResolution`。从 Windows 10 2004 起，计时器分辨率按进程生效，所以这里的 `Sleep(1)` 要睡到下一个系统时钟 tick（默认 15.6 ms）。帧的等待因此被取整到 15.6 ms 的网格上：平均仍是 16.7 ms，但约每 5 帧就有一帧要两个 tick（约 32 ms），这就是 60 fps 下的顿挫。

改法：`0x11A2F65` 的 `call [Sleep]` 改为调 `TickWait`，在高精度定时器（`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`）上等，不改整个进程的计时器分辨率。最初（PR #320）每次等 1 ms，一帧约要醒 16 次、每次回 `0x11DAE60` 重查；2026-11-08 起按下面的 tick 时钟算出下一个边界，一次等到边界（`src/frame_pacing_logic.h`）。

### tick 时钟 `0x11DAE60`

2026-11-08 用 `tools/edfre.py` 只读 `EDF.dll` 静态核实（字节由 `tests/frame_pacing_sig_audit.py` 逐条审计）：

```
0x11DAE66: lea rcx,[0x2139D10]; call 0x66CD0   ; elapsed = QPC - [0x2139D10]（0x66CD0: QueryPerformanceCounter 后 sub rax,[rcx]）
0x11DAE75: call 0x66D60                         ; freq = QueryPerformanceFrequency
0x11DAE7A: mov r8,[0x2139D18]                   ; last
0x11DAE81: imul rcx,rax,0x64                    ; freq*100
0x11DAE88: mov rax,0xAEF006D5604455C3; mul rcx; shr rdx,12   ; period = freq*100/5994（编译器的乘法代替除法）
0x11DAE85/9B/A1: n = (elapsed - last) / period  ; 无符号 64 位
0x11DAEA4: if (n >= 1) [0x2139D18] = last + n*period
return n                                        ; eax
```

- `0x2139D10`（`.data`，base）：开机时的 QPC 读数。CRT 初始化 `0x316B0`（初始化表项 `0x175D440`）经 `0x66CA0` 写一次，游戏初始化 `0x11A3796` 调 `0x11DAED0`（经 `0x66CC0` 即 `jmp [QueryPerformanceCounter]`）再写一次。
- `0x2139D18`（`.data`，last）：上一次走到的 tick 边界相对 base 的 QPC 偏移，只有 `0x11DAE60` 写，只按整 period 前进。所以**下一个边界 = base + last + period**。
- 周期是 `freq*100/5994`，即 1/59.94 s（约 16.683 ms），不是 1/60 s。魔数 `0xAEF006D5604455C3 = ceil(2^76/5994)`，误差 1982 < 2^12，对所有 64 位被除数都是精确除法，所以插件直接写 `freq*100/5994` 与游戏逐位相同（`tools/frame_pacing_check.cpp` 也对照魔数乘法验证）。另一侧，`System::Update`（`0x1198E13`）按 `ticks * 1/60` 累加游戏时间，两者差 0.1%。
- 读写都在游戏线程（`0x11DAE60` 只被 `0x11A2F4F` / `0x11A2F6B` 调），`TickWait` 也在游戏线程、只在 `0x11DAE60` 刚返回 0 后被调，读这两个字不需要同步。

`TickWait`：自己读一次 QPC，`since = now - base - last`（无符号，QPC 回绕时仍精确）；`since >= period` 说明边界已到（游戏数完到我们读之间跨过了），直接返回，循环里的 `0x11DAE60` 会数到它；否则等 `period - since` 个计数，换成 100 ns 向上取整（128 位乘除，不会提前醒），在高精度定时器上等一次。醒后照旧由循环的 `0x11A2F6B` 重新数，计时器偶尔早醒也只是再算一次剩余量，不靠固定的 1 ms 轮询。安装时整段比对 `0x11DAE60` 的 97 字节，不一致（别的 MOD 改了时钟）就两处等待都不改。

离线测试 `tools/frame_pacing_check.cpp`：周期与魔数除法一致；刚过边界、恰在边界、已过一个 / 多个 tick；随机状态下等待结束前一计数游戏数 0、恰好结束时数 1；QPC 回绕前后（base 在 2^64 附近、边界在回绕之后）；100 ns 换算向上取整且最小；在假时钟上跑游戏循环，每帧工作在一个 tick 内时恰好等一次、走一个 tick。

## 主循环里按帧而不是按 tick 执行的点

主循环（`0x11A2ECA` 起）每帧：`n = min(0x11DAE60(), 3)`，`dt` = 实际帧间隔（秒表 `0x66D00`），再调 `Application::Update`（`0x705530`，`Application` vtable `0x17E91A8` 槽 1，从 `0x11A3006 call [rax+8]` 进入），参数是 `{dt, n}`（`[r14]` / `[r14+4]`）。原版的等待保证 `n >= 1`，所以「按帧」与「按 tick」只在追帧（`n` 为 2、3）和下面那一种 0 tick 帧上不同；若有 MOD 让游戏在 0 tick 时也出帧（如第三方 120 fps 研究包），下面这些点就会多跑。2026-11-08 静态核实：

| 地址 | 是什么 | 怎么按帧跑 |
|---|---|---|
| `0x705633` | `call [SteamAPI_RunCallbacks]`；接着 `0x705639` 前台检查 `0x1183D60`，变了才 `EOS_Platform_SetApplicationStatus`（`0x705686`）；`0x705693` 调 `0x788460`（全局对象 `[0x20B2AC8]` 的 Update，第三方分析称 net::Network::Update，我未从 RTTI 核实名字）；`0x7056AE` 调 `0x12ADB30`（内调 `EOS_Platform_Tick`，已核实导入） | 每次 `Application::Update` 跑一遍，不看 n |
| `0x1188480` | 输入回调 `0x1188270` 里 `0x11832D0(cl=1)`：取滚轮累加量 `[0x21360C0]` 并清零（窗口过程 `0x1183E20` 在 `0x1183FE3` 累加） | 回调由初始化 `0x1185C30`（`0x11A378A`）经 `0x11827B0` 挂到输入对象 `[0x20B2888]`，主循环 `0x11A2FF3` 每帧以同一对象调 `0x1182A10`（未逐层核实它就是派发该回调的那一步）。0 tick 帧会把滚轮取走 |
| `0x705963` | `Application+0x6C` 计数：`> 0` 时每帧 `dec`，并调 `0x77200` | 按帧减，不乘 n；同函数 `0x705592` 的 `+0x68` 按 `dt` 累加（与帧率无关） |
| `0x7056BA` | `call 0x1230A0`，参数只有 `rcx = [0x20B2990]`（第三方称 MapMgr::Update） | 每帧一次，不传 n。第三方说它每次给叶子 / 风动时间 +0.0125：我在 `0x1230A0` 三层直接调用内没找到 `0.0125` 常量（唯一的 `0x17A8818` 不在其下），**未核实** |
| `0x1198E33` | `System::Update`（`0x1198DA0`，`0x705715` 调，`r12d = n`）：未暂停时 `[System+0xCE4] += n * (1/60)`，再 `0x112BAA0(&0x2139A90, …, 时间)` 把时间发给 XGL 着色器 | 每帧发一次；时间按 n 推进，0 tick 帧发的是同一个值 |
| `0x1198FB0` | `test r12d,r12d; je 0x1199734` | n = 0 时跳过整段固定步模拟（`0x1198FB9..0x1199734`） |

原版自己的 0 tick 帧：`0x11A2FAC` 若 `[rdi+9]` 置位（谁置它未查），清掉它并令 `dt = 0`、`n = 0`、`[rdi+0xC] = 0`，这一帧走上表的按帧路径、跳过 `0x1198FB0` 之后的模拟。

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
