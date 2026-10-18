# 2026-10-18 PR 集成审查与根因修复

## 范围与基底

从 `origin/main` 的 `1fb6931e0845e61ad3e73c239f984e664b085179` 建立隔离工作树，保留主目录未提交改动。按用户明确授权，草稿也纳入：

| PR | 审查的 head | 内容 |
|---|---|---|
| [#197](https://github.com/hajisensai/edf6-all-forces/pull/197) | `8689dcb646d41e9b5d606efa0f32bd1e01ce7631` | EDF4.1 独立击杀计数与战斗记录 |
| [#198](https://github.com/hajisensai/edf6-all-forces/pull/198) | `da940f92429be46080b29cb03716de7ef10a2b9c` | 支援 NPC 背包武器 |
| [#199](https://github.com/hajisensai/edf6-all-forces/pull/199) | `63ef4fb97bc807c69a010231a2f3b7c164d8d188` | 外部模型贴图路径与缓存一致性 |
| [#200](https://github.com/hajisensai/edf6-all-forces/pull/200) | `0dad68e0acfcb03140775104aa5d520cde7c3613` | EDF4.1 要塞 ObjectAction 崩溃 |
| [#201](https://github.com/hajisensai/edf6-all-forces/pull/201) | `eccee1b5a26cb4037f0a69afd7c473b1e4f2b1a8` | Hector 朝向、行走、步态 |
| [#202](https://github.com/hajisensai/edf6-all-forces/pull/202) | `57f96d6725506a99c6b345059c19a9be6dbf59f3` | 插件崩溃转储与日志回传 |

逐项审查真实运行路径后集成，保留六个原始 head 的提交祖先关系。CMake 离线检查列表、selftest 新增项的冲突均保留双方内容。

## 修复及证据

1. **要塞兼容层不应由可选 AI 开关关闭。** `InstallEdf41Fortress` 的早退使 `Edf41Ai=0` 时 M117 仍走原版错误 ObjectAction。必要的消息、站立和生成器适配始终安装，仅 Wander 受开关控制。新增 `edf41fortress_runtime_test` 直接调用生产安装入口及虚表槽 4/9，153 项检查；临时恢复旧早退时，该回归精确失败。用本机 EDF.dll / EDF41.exe 只读核对动作表与原生 ABI。
2. **Hector 退速与原作契约不符，测试探针有发布和身份问题。** EDF41.exe `0x2237FF` 到 `0x2238BB` 的 xmm7 为 -1，退速应与前速相同；修正旧 0.85 倍，并直接核对原生指令/寄存器及 CAS 契约。探针先设置原函数再发布入口，按对象控制块识别代次，避免第 64 槽及地址复用污染测量。Hector 687 项检查及 100 实例/地址复用探针通过。
3. **NPC 投掷物只查前 6 米且副槽不可用也抢主武器。** 现在沿实际发射角、速度和重力逐段检查完整弧线及友军；FireVector 抬升仅用于视线补偿。`CanUse` 和 `Use` 共用生产原生可用门，接管瞄准前检查，实际使用再检查。忙碌动作结束前持续保持方向。纯弹道 3804 项、原生读取/门/状态转移 19 项，生产 `Drive` 的关门保持主枪、中途墙/友军和长动画场景均通过；真实 EDF.dll 静态审计 62 项。
4. **旧版 peer 降级缺少纯颜色资源，Bloom 假阳性可导致武器不一致。** Python 生成、C++ 配置枚举及预载共同包含完整背包资源和纯颜色资源；`EDF6X_VCPACKS` 仅在全员声明支持时允许房间清单携带背包 token。applied 背包资源必须精确存在，不能只信 Bloom。验证真实碰撞、旧 peer 的实际 Plan、清空 pending 后的配置预载。配置上限为 1666，连同 host 512、pending 128，共 2306；同步键 6 个，值预算 3841 < 4096。dispatch 266、room_sync 143、variants 29、loadout 101、Python 358 项通过。
5. **日志锁可能阻止产生崩溃转储，卡死/崩溃转储还可能并发调用 DbgHelp。** 故障线程持普通日志锁的子进程已复现旧实现不写 dump。现在先写 dump，再用固定缓冲、无普通日志/printf 锁的诊断追加；日志缓冲共用同一实现，try-only 快照不再释放后重获锁。hang/crash 统一到一个专用 worker，原始 DbgHelp 指针只在 worker 执行。受控并发回归记录最大在途写入数为 1，分别核对路径、异常码和故障线程。原生崩溃 100 项、watchdog 19 项通过。此串行要求见 [Microsoft MiniDumpWriteDump 文档](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)。
6. **回传先解析超大 dump 可耗尽内存；时间/目录不能证明 WER 属于当前进程。** 文件大小先于解析检查，模块/时间元数据按偏移有界读取；WER 作为 `Report.candidate.wer` 并明确会话未确认。收集器支持旧时间戳及新 raw CRASH 行，并排除普通日志中的转述。Python 43 项及新版程序真实 AV/持 buffer 锁/持 file 锁日志 3 项跨组件检查通过。
7. **分享素材检查只校验旧 manifest，会遗漏当前必需资产。** 开发者导出器额外校验当前必需集合；底层安装器仍保留合法部分素材包的按组跳过契约。旧清单缺 MENU、地图、声音的三例先红后绿，8 项回归通过，并接入 CTest。当前素材缓存已补齐 MENU_BG01 三文件，59 文件/3,780,592,905 字节，全 SHA-256 检查通过。

#197 的 18 类 SGO、存档槽位尾追加、外部 MOD 表兼容、页面补丁未发现需修复问题；真实原生审计、3 项聚焦 selftest、43 项安装生命周期及外部表重复安装/卸载字节还原通过。#199 新回归在原基底复现 WinError 1117，在 PR 与集成树通过；13 项缓存回归全部通过。

## 整体验证

- MSVC RelWithDebInfo、C++17、/W4 /WX：两个插件 ALL 构建 258/258、offline_checks 构建 313/313 完成。
- 完整 CTest：注册 303，实际 303 Passed、0 Skipped、0 Failed，742.13 秒；包含真实 EDF.dll/游戏资源只读检查。
- 安装链 selftest：287/287 passed。
- 295 个受跟踪 Python 文件 py_compile、四组生成文件检查、git diff --check 通过。
- 定向回归与两轮独立复审均已完成，未遗留已确认的阻断问题。

原始本机输出：隔离工作树 tmp/review-merge-{build-all,build-offline,ctest,selftest,static-checks}.log（不入库）。定向数字是各检查内部断言数量，不代表游戏实测次数。远端 CI 的冻结安装器/worker 自检以对应提交的 GitHub 检查为准；最终推荐包与本机安装分别保留资产清单、SHA-256 和独立安装回执。

## 验证边界

本次使用 MSVC 编译、生产函数记录式入口、子进程崩溃和真实游戏映像的只读审计；未启动或操控游戏。NPC CAS 实际动画/出手方向、战斗记录滚动/保存，以及真实双机联机仍未验收。DbgHelp 本身卡死时，崩溃捕获最多等待 60 秒后继续 Windows 退出流程，不并发重入 DbgHelp。
