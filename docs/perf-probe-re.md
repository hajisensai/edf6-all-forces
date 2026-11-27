# 帧循环与测量探针的锚点（EDF.dll Steam 0x678CCB46）

测量线 A（`docs/plan-2026-11-24-optimization.md` §3 A）用到的原版地址。代码在 `src/perf_probe.cpp`，每个调用点都由
`tests/perf_probe_native_audit.py` 对着 EDF.dll 核对。实机核对：2026-11-24 M001 和 stress 系列，日志里能看到
`HOOK perf split=1 submits=5/5 meshes=2/2` 和 `HOOK perf present=1`。证据级别：H = 汇编核实；R = 实机日志或采样核实。

## 1. 一帧的结构

主循环函数 `0x11A2E90`（`.pdata` 从这里开始，调用者 `0x703417`；它前面的 `0x11A2E40` 是另一个 0x42 字节的 scalar deleting destructor），每帧的循环在它的链接片段 `0x11A2ECA..0x11A3116` 里，每圈是一帧：

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
- 遮挡查询读取 `0x1631790`（gpu-5，PERF GPU 的 `occlusion query reads`）：
  - 唯一调用点 `0x11E4244`，在 Umbra Commander 的结果循环里。
  - 参数：rcx 渲染器，rdx 查询，r9 指向结果，第 5 个参数（栈上的字节）是「等待」。等待字节在 `0x11E4230` 由
    `mov [rsp+0x20],al` 写入。
  - 函数先取自旋锁 cmdctx+0x29F0，再在 `0x1631860` 经 vt+0xE8 调 GetData。flags 在 `0x163184C` 固定写 0。
  - 返回 al = 结果已就绪。
  - 探针按调用计次数：未就绪（返回 0）、等待字节为 1，以及这两类各自的耗时。
- 蒙皮调色板 `0x11009A0`（gpu-6，PERF GPU 的 `skin palettes`）：
  - 计数挂在 LOD 绘制 `0x1104100` 里的调用点 `0x1104131`，每个 pass 一次。
  - 另有两个调用点 `0x6C2C8A`、`0x1102F2E` 不计入。
  - PERF VISIBLE 的 `skinned instances`：每帧去重的第 3 个参数 r8（调色板读它 +8 的姿态；rcx 的 +0x10 / +0x20 是骨骼
    数组与骨骼数，按模型共享，不能拿来数实例）。去重表按帧锚点（InputHook）换代，不清表。
- Umbra 找到的对象（PERF VISIBLE 的 `visible`）：
  - Commander 回调 `0x11E2120`：rcx 是 commander（存进 r13），edx 是命令号，跳转表在 `0x11E435C`。
  - 命令 3（`0x11E3E1C`）在 `0x11E3E1F` 以 rcx = r13 调 `0x12D61C4`，即 umbra_sandlot 导入
    `Umbra::Commander::getDataCount`，随后按下标 getObjectData / getUserPointer 逐个交给绘制。探针挂这个调用点，
    计调用次数与返回值之和。
  - 按 commander+0xE4 分视图（0、1、其他）；回调在 `0x11E21C6` 等处 `cmp [r13+0xE4], 1` 把视图 1 单独处理。
    视图 0 / 1 各是什么（主视图、阴影等）未实测，只按编号报。
  - `tests/perf_probe_native_audit.py` 核对跳转表、`mov rcx, r13`、导入名和 +0xE4 的比较字节。
- 对象普查（PERF CENSUS，`src/perf_census.cpp`，PerfProbe≥1）：游戏线程每 0.5 s 采样一次，TryEnterCriticalSection
  拿不到锁就跳过这次（计入 skipped），不等锁。按队伍走队伍管理器的 std::set（与 GetTeamObjectCount 同样的过滤），
  按 SGO 名分类；按场景走 System +0x3D0 的对象链表，按 RTTI 类名分类。每次采样自身的耗时单独报。
- 实测（Debug=1 + PerfProbe=1，60 fps）：
  - M001 每帧读约 10.6–13 次遮挡查询，几乎都带等待，但**从没遇到未就绪**；全部读取合计 0.05–0.07 ms/帧，单次最差约 1.6 ms。
  - M001 每帧约 92 次蒙皮调色板，0.08 ms/帧。

## 2.1 设备与适配器（gpu-4）

- `0x111B420..0x111B6BD` 建设备：
  - `0x111B595` 调 D3D11CreateDevice，适配器参数为 NULL（默认适配器）。驱动类型依次试 HARDWARE / WARP / REFERENCE。
  - 设备存在 cmdctx+0x80，立即上下文存在 +0x88。
- 设备建好后：
  - `0x111B65E` 先 QI 出 IDXGIDevice，再 GetAdapter，结果存进 cmdctx+0xF0。
  - 再用 GetParent 取工厂，存进 cmdctx+0xF8。
- cmdctx+0xF0 的使用点：
  - `0x111A64F` 调 EnumOutputs 建显示模式列表。失败时跳到 `0x111A98D`：走 1280x720 默认值，模式列表为空。
  - 另有 `0x111AE8A`、`0x111C2ED`、`0x111C493`、`0x111C6C6`、`0x111C7BC`。
  - 所以「设备所在的适配器」也被当作「输出所在的适配器」。
- 推论（未在混合显卡机器上验证）：
  - 把设备强制建在独显上时，muxless Optimus 笔记本的独显通常没有输出，EnumOutputs 会失败。
  - 游戏会退到空模式列表，全屏和分辨率选择会出问题。
  - 所以不能只改 `0x111B595` 的适配器参数。

## 3. 线程

- `0x3E630` 出现在**所有** xgs 线程的栈上，游戏主线程也有。它是 xgs 线程的入口，不能拿来认 pool worker。
- 游戏主线程在 60 fps 时的分布：75% 在 tick 等待，24% 在栅栏忙等，只有约 1% 在 System::update。所以按 System::update
  认主线程会漏，要按主循环 `0x11A2E90..0x11A316E` 来认（`tools/perf_report.py` `thread_role`）。
- 渲染线程的入口是 `0x111B8E0`（docs/frame-pacing-re.md）。

## 4. 输入到 Present

第 k 帧的画面是用第 k-2 帧的输入步进出来的：采样之后，Application::Update 步进场景、录制绘制，渲染线程在下一帧才执行
那份录制的 Present。PERF RENDER 两个数都给：

- 「本帧采样到 Present」：M001 约 2.8 ms；
- 「步进它的那次采样到 Present」：M001 约 36 ms。
