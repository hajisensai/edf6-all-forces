# Umbra 可见性复用可行性逆向（只读，EDF.dll 0x678CCB46 + umbra_sandlot.dll）

置信度：H = 汇编/样本直接验证；M = 由证据推断；L = 推测。
样本：`D:\APP\edf6-wt-1124-fps-open\tmp\perf\<run>\stacks.txt`（rip_sampler，15 ms tick，只采当 tick 用过 CPU 的线程）。

## 0. 先纠正前提（最重要）

- 「M001 封顶 ~122 fps、渲染线程 33% 在 0x11b5a70」来自旧 DLL（sha 5343f3，16:59 `acc-m001-rs`，log 写 `flip swap chain=1`，显示器 160 Hz）。
- 同场景、同绘制量（pass 0/1/2/3 = 28/28/12/232，180 mesh）在新 DLL（42275a，commit 9ee8beb5，flip 退回开发开关 FrameRateFlip，blt + SyncInterval 1；17:04 `acc3-m001-r`）跑到 **345 fps（2.90 ms/帧）**，栅栏等待 2.45 ms。
- 渲染线程样本对比（H，数据）：

| 类别 | 旧 acc-m001-rs（439 样本） | 新 acc3-m001-r | 新 acc3-rm015-r（146 fps） |
|---|---|---|---|
| 等 Umbra worker（umbra+0xFAD7） | 42% | 5% | 1% |
| Commander 自身 CPU（提交绘制） | 27% | 37% | — |
| NVIDIA 驱动阻塞（nvwgf2umx+55c92e ← Flush） | 22% | 46% | 46% |
| GetData（0x1631790） | 0 | 7% | 11% |

- 结论（H 数据 / M 解释）：委托方的「Umbra 是瓶颈」只在旧构建成立；当前构建里 Umbra 等待几乎消失，渲染线程被驱动等待 + 绘制提交 CPU 主导，复用可见性去不掉这两项。
- 等待为何消失：未定位（L：与 flip 交换链 / present 路径改动相关；17:11 的 8b003f4e 电源节流修复在其后）。

## 1. 输入：相机、视锥从哪来，每帧几次

### 函数身份（H）

- 0x11b5a70 / 0x11b5b30 **不是可见性计算**，是渲染线程命令执行器：
  - 0x51E90 读 0x3C / 0x3D 单元 payload，0x11B51F0 拷出；
  - 0x11E1BC0(commander, view 块 rsp+0x40, wrapper, nativeCtx) 把 view 块拷进 DeferredRenderSystem+0xE0；
  - 再调 processVisibility（thunk 0x12D6116 → IAT 0x1756870），rcx = Umbra::Camera*，rdx = Commander。
  - 0x11b5b30 另外在调用前后把 [全局]+0x49 清零，结束时从 payload+0x3C0 恢复。

### 游戏线程录制函数（H）

| 视图 | 录制函数 | 调用点 | 执行器 | 备注 |
|---|---|---|---|---|
| 主视图 type 0 | 0x11B6AA0 | 0x1198656 | 0x11b5a70（lea 0x11B6B48） | |
| 阴影 type 2 | 0x11B6F90 | 0x1197EC0 | 0x11b5a70（lea 0x11B702A） | 循环 cam+0x178，步长 0x150，到 0x2A0 止 → 2 次；门控 [rdi+0xB00]==0 |
| 远景 type 1 | 0x11B6770 | 0x1197F93 | 0x11b5b30（lea 0x11B6842） | desc+8 = cam+0xF0 远景 wrapper |

- 0x11B7180 只尾跳 resolveVisibility，未找到调用者。
- 录制流程：
  1. 若 wrapper+0x74 置位：清掉，setProperties(0x12D611C, wrapper+0x48 props)，resolveVisibility(0x12D6110)（游戏线程）。
  2. 置 +0x74=1，录 1 单元命令（执行器函数指针）。
  3. 组 payload：[+0x20] Camera*、+0x28 wrapper、+0x30 commander（[system+0x10]）、+0x40 视口号、+0x44 视图类型；0x11B5060 拷描述符矩阵 +0x30..+0x1F0 与 +0x220。
  4. 0x11B41F0 拷进 ring（cmdctx+0x28A8，cmdctx = EDF+0x2136FF0）并发布。

### 次数（H）

- 每视口每帧 4 次 processVisibility：主 1 + 阴影 2 + 远景 1。
- 0x11978D0 遍历 4 个视口槽，scope PIP 开着时翻倍到 8 次。
- 未见反射 pass。
- 第三方记的「~3 次/帧」少算了一次阴影。

### 相机参数写入（H）

- 游戏线程 0x118DFF0 经 0x118E0C6 / 0x118E0FA / 0x118E10A 调 Umbra Camera 的 setFrustum / setCameraToCellMatrix（取 cam+0x220）。
- 相机 wrapper 布局见 docs/view-distance-re.md：+0x40 Umbra::Camera*，+0x58..0x70 视锥；节点 +0x10 Object*。
- 绘制用矩阵来自录制的描述符（view +0x20 / +0x60），不来自 Umbra；Commander cmd 2 只取 getProjectionMatrix / getCellToCameraMatrix 给遮挡 pass。

## 2. 输出与消费者；时间花在哪

### Commander 0x11E2120（vtable 0x1AE5288 slot 1，跳表 0x11E435C）（H）

| cmd | 地址 | 作用 |
|---|---|---|
| 0 | 0x11E21B8 | begin，清列表 |
| 1 | 0x11E2247 | QUERY_END：绘制 pass 1/4/5/2 |
| 2 | 0x11E3B42 | 视图参数：getViewer / getProjectionMatrix / getCellToCameraMatrix |
| 3 | 0x11E3E1C | 可见实例：getDataCount / getObjectData / getUserPointer / isOccluder，填列表 +0x18/+0x38/+0x58/+0x78/+0x98 |
| 4 | 0x11E3FD1 | 遮挡体深度 pass 0（或阴影 pass 3），末尾 ctx->Flush（0x11E4101） |
| 5 | 0x11E410C | 发遮挡查询：getOcclusionQueryData / getQueryObject / Begin(vt+0xD8) / 顶点与三角形 / End(vt+0xE0) |
| 6 | 0x11E41F5 | 取结果：getOcclusionQueryResultData / getWaitForResult / 0x1631790 GetData / setResult |
| 7, 8 | 0x11E42E6 | 空 |
| 9 | 0x11E4270 | getLine3D（调试） |

- ctx->Flush（vt+0x378）另在 0x11E2225、23E6、24EC、2599、2A6C、2E25、35B8、3A7C。
- 绘制提交就在回调里，经 0x11E1630 → obj->vtbl[1]。

### 输出（H）

- Umbra 的输出是存活引擎渲染节点 wrapper 的 user pointer 列表，在同一次 processVisibility 内被 cmd 1 / 4 直接消费画掉。
- 没有独立的「可见集」对象留到下一帧。

### 「0x11b5a70 self 33%」的真身（H）

- processVisibility（umbra+0x2CCA0）：
  - 往 camera 队列 impl+0x438 写消息（全局 B2E88+0x110 的 3×4 矩阵、impl+0x480、impl+0x4A0、0xB150 的结果）；
  - 0xEDA0；
  - 泵 0xF9C0。
- 泵 0xF9DE..0xFBB1 的行为：
  - 有挂起查询时 0xF1C0 + Sleep(0) 自旋；
  - 否则 0xFAD2 → 0x86F0 → 0x8620 → WaitForSingleObject(INFINITE) 等 worker 输出。
- 符号化时这段等待被记在 EDF 的 0x11b5a70 名下。它是跨线程握手，不是 CPU。
- worker（0x30970 → 0x29D60）：
  - 旧样本 CPU 6%（约 0.5 ms/帧）；105 样本里 65 在消息后 Sleep(0)（返回 0x29E42），17 在遍历中等查询结果（0x2A243..0x2A261 → 0x26190，置 [+0x6618]，阻塞在输入队列 +0x7EC0），12 空闲，约 10 在计算。
- 所以旧构建的 42% 是遮挡查询往返延迟 + 线程乒乓，不是 Umbra 算得慢（M）。

## 3. 0-tick 帧复用旧可见性的失效条件

### 硬约束（H）

- 列表存的是活对象指针，跨帧持有就要处理对象销毁（敌人死亡、弹体、破坏物）。引擎没有给 Umbra 列表做引用计数的迹象（未见 AddRef，M）。
- 遮挡查询有时序：cmd 5 / 6 的 setResult 喂回 Umbra 的时间相干状态。跳过一帧 processVisibility 等于不发查询；下帧 worker 等的结果来源变了（L：可能导致长时间 pop 或等待异常）。
- 第三方已实测（docs/unlimited-fps-re.md:159）：
  - 跳 resolveVisibility → 读图卡死；
  - 「Umbra 重发缓存」→ 无收益；
  - 降辅助可见性频率 → 黑闪、阴影块状。

### 必须失效的情形

- 有 tick 的帧（对象增删、LOD）。
- 镜头切换 / 传送：插件 camera interpolation 的 refused 计数路径可复用这个判断。
- FOV / 缩放（scope）变化、视口数变化（PIP 开关）。
- 阴影级联：分割表 0x1FEEE90，随相机位置移动，2 次阴影 pass 都要失效。
- 远景 wrapper 的 far² 变化。

### 视锥放宽量（M）

- 每 tick 的位移 = 速度 / 59.94。0-tick 帧之间的插值相机最多移动一整个 tick 的位移与转角，所以视锥要外扩「一个 tick 的平移 + 转角」。
- 例：快速转视角 180°/s 约 3°/tick；高速载具 / 飞行器 ~50 m/s 约 0.83 m/tick。
- 遮挡剔除对平移敏感：被遮挡体刚露出时会出现一帧空洞，只能靠关遮挡或扩大保守量弥补，等于放弃 Umbra 的收益。

## 4. 能否移到别的线程 / 加 worker

- `initThreads`（0x2CC10）只调 0x30C90，而 0x30C90 是单条 `ret`。它是空桩，sandlot 版没有多 worker 能开（H）。
- 只有一个 Umbra worker 线程，且未导入 SetThreadPriority（H）。
- Umbra 计算本来就在独立线程。游戏线程的 resolveVisibility 只投递 {1, camera impl} 到全局队列 B33E8（0x86C0 / 0x81F0 → 0x8210 ReleaseSemaphore），约 0 ms（H）。
- 渲染线程必须在场：它要录绘制、发 / 取 D3D 遮挡查询。D3D11 immediate context 单线程，不能把 Commander 回调挪走（H 结构，M 结论）。
- 可做的只有调度层面，比如减少 worker 的 Sleep(0) 乒乓。这要改第三方 DLL 的二进制，不建议。

## 5. 结论

| 项 | 结论 |
|---|---|
| 可行性 | 低 |
| 预期收益 | 当前构建每个 0-tick 帧最多约 0.15 ms（2.9 ms × 5%）；旧构建理论上限约 3.4 ms/帧，但这段等待已不存在（M） |
| 工作量 | 5–8 人日：保存 / 回放列表、对象生命周期钩子、失效判定、阴影 / PIP / 远景四路、联机与载具切换回归（L） |
| 风险 | 悬空指针崩溃（销毁对象仍在列表里）；遮挡 pop / 空洞；阴影级联错位与块状（第三方已见）；遮挡查询时间相干被打断导致 worker 等待异常；scope PIP 双倍状态 |

### 决定（2026-11-24）

- 不做 0-tick 帧 Umbra 可见性复用。
- 下面第 3 条（gpu-5，GetData flags=0）已在 #377 实测并决定不做。
- 驱动阻塞（46%）和提交绘制（37%）只记在这里，暂不展开。

### 建议（按收益排序，M）

1. **先量后改。** 读 umbra_sandlot+0xB32DC：float 型，是 rdtsc 累计等待时间（除以 2.5e9，常量在 0x30C50），永不清零。前后两帧相减，就得到每帧真实的 Umbra 等待，不用再靠采样归因。
2. **主攻驱动等待。** 当前占渲染线程 46%：Commander 内 9 处 ctx->Flush（vt+0x378，见 §2 列表）。逐个评估去掉或合并 Flush；首个就是 cmd 4 末尾 0x11E4101。
3. **GetData 轮询（gpu-5）。** 0x1631790 / 0x1631860 用 flags=0 自旋，最长 1 s；当前占 7–11%。
4. **查清旧构建 122 fps 封顶的来源**，它更可能在 present / flip 路径而非 Umbra（L）。旧 log 里有 `flip swap chain=1`，新 log 是 `flip swap chain=0`。160 Hz 显示器下 122 fps 也不是刷新率的整除值，应检查 flip 下的排队 / 等待。

## 证据索引

- umbra_sandlot.dll（`D:/steam/steamapps/common/EARTH DEFENSE FORCE 6/`）：
  - 导出：processVisibility 0x2CCA0、resolveVisibility 0x2CF60、initThreads 0x2CC10 → 0x30C90 ret。
  - 泵 0xF9C0 / 0xFAD2；信号量 0x8620 / 0x8210；worker 0x30970 / 0x29D60。
- EDF.dll：
  - 执行器 0x11b5a70 / 0x11b5b30；录制 0x11B6AA0 / 0x11B6F90 / 0x11B6770；视口循环 0x11978D0；相机 0x118DFF0；Commander 0x11E2120 / 跳表 0x11E435C。
  - Umbra thunk 0x12D6110..0x12D61D6 → IAT 0x1756868..。
- 文档：docs/view-distance-re.md、scope-pip-re.md §4.3–4.5、fp-render-re.md §1.1、unlimited-fps-re.md:159、reviews/2026-11-24-optimization-audit.md gpu-5（均在 D:\APP\edf6-wt-1124-fps-open）。
- 样本：tmp/perf/acc-m001-rs、acc3-m001-r、acc3-rm015-r、acc3-m001-0（stacks.txt / vehiclecrew.log）。
