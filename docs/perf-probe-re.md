# 帧循环与测量探针的锚点（EDF.dll Steam 0x678CCB46）

测量线 A（`docs/plan-2026-11-24-optimization.md` §3 A）用到的原版地址。代码在 `src/perf_probe.cpp`，每个调用点都由
`tests/perf_probe_native_audit.py` 对着 EDF.dll 核对。实机核对：2026-11-24 M001 和 stress 系列，日志里能看到
`HOOK perf split=1 submits=5/5 meshes=2/2` 和 `HOOK perf present=1`。证据级别：H = 汇编核实；R = 实机日志或采样核实。

## 1. 一帧的结构

主循环函数 `0x11A2E40`，循环体从 `0x11A2E80` 开始，每圈是一帧：

| 地址 | 内容 | 证据 |
|---|---|---|
| `0x11A2F43..0x11A2F74` | tick 等待。frame_pacing 改写了这一段 | H |
| `0x11A2F76` | 本帧要补的步数 n = 到期的整 tick 数，`cmp ebx, ebp` 加 `cmova`，最多 3 | H |
| `0x11A2FF3` | `call 0x1182A10`：本帧唯一一次输入采样（ControllerManager），帧锚点挂在这里 | H, R |
| `0x11A3006` | `call [vtable+8]`：Application::Update `0x705530`（Application 的 vtable 槽 1） | H, R |
| `0x11A305A` | `call 0x111CC20`：等渲染栅栏。实际是忙等，见 plan §1 第 4 条；M001 主线程 24% 的采样落在这里 | H, R |
| `0x11A309B` | `call 0x111D890`：投递 Present 命令。rcx 是渲染器 `0x2136FF0`（`0x11A308E` 的 lea），dl 是 vsync 标志 | H, R |

Application::Update `0x705530` 在 `0x705715` 调 System::update `0x1198DA0`，参数是 xmm1 = dt、r8d = n。
System::update 在 `0x1198FA4` 先调一次绘制录制 `0x11978D0`，然后跑 n 步。

**由此得出：** 插件的 `SharedFrameTick`（玩家 pre-update）每**步**跑一次，不是每帧一次。所以 2026-11-24 之前的 PERF 行
「N frames, mean X ms」其实是每步的数字。

- stress 场景下一帧要补 2 到 3 步，旧数字和真实帧时间差 2 到 3 倍。
- 旧文档里的「ms/帧」都要按「ms/步」重读。
- 现在的 PERF 行写作 `N frames, M steps`，帧时间以 `0x11A2FF3` 为锚点计算。

## 2. 渲染侧

- 绘制录制内：`0x11B6B1D` 调 `0x12D6110`，这是 `umbra_sandlot.dll!Umbra::Camera::resolveVisibility` 的导入跳板。
- 绘制提交 `0x11E1630(system, object, pass)`，pass 在 r8d。调用点共 5 处：
  - `0x11E255C`（thermal 先挂了这里）
  - `0x11E25F9`
  - `0x11E3A3F`
  - `0x11E4029`
  - `0x11E40BC`
- M001 每帧的 pass 0..3 分别约 28 / 28 / 12 / 232 次。
- 网格绘制 `0x10FF4E0`（5 个参数）有 2 个调用点：`0x1103FD1`（firstperson 先挂了这里）和 `0x6C30CB`。M001 每帧约 180 次。
- 渲染线程的 Present 命令 `0x1118AF0`，在 `0x1118B45` 执行 `call [rax+0x40]`：rax 是 `[[0x2136FF0+0x90]]`，也就是
  IDXGISwapChain 的 vtable，+0x40 是槽 8 Present。所以渲染器 +0x90 存的是 swap chain。实机上对它做 QI 能拿到
  IDXGISwapChain，desc 为 3840x2160、1 个 buffer、DISCARD。

## 3. 线程

- `0x3E630` 出现在**所有** xgs 线程的栈上，游戏主线程也有。它是 xgs 线程的入口，不能拿来认 pool worker。
- 游戏主线程在 60 fps 时的分布：75% 在 tick 等待，24% 在栅栏忙等，只有约 1% 在 System::update。所以按 System::update
  认主线程会漏，要按主循环 `0x11A2E80..0x11A316E` 来认（`tools/perf_report.py` `thread_role`）。
- 渲染线程的入口是 `0x111B8E0`（docs/frame-pacing-re.md）。

## 4. 输入到 Present

第 k 帧的画面是用第 k-2 帧的输入步进出来的：采样之后，Application::Update 步进场景、录制绘制，渲染线程在下一帧才执行
那份录制的 Present。PERF RENDER 两个数都给：

- 「本帧采样到 Present」：M001 约 2.8 ms；
- 「步进它的那次采样到 Present」：M001 约 36 ms。
