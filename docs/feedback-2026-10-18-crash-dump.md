# 2026-10-18 测试者崩溃回传没有 dump

## 现象

测试站 report #19 / #20（EDF4.1 M16 崩溃，EDF6Coop 日志记 `EXCEPTION C0000005 at EDF+4C3A30`）回传的 zip 里没有 `crash/` 目录，
连 `crash/dumps.txt` 都没有。

## 根因

1. **崩溃时没有任何代码写 dump。** 插件只在任务开局卡死时写 hang dump（`src/mission_watch.cpp` → `Mods\Plugins\EDF6VehicleCrew.hang.dmp`）。
   `tools/testhub.py collect()` 收的崩溃 dump 只有 `%LOCALAPPDATA%\CrashDumps\EDF6*.dmp`，那是 Windows WER LocalDumps 写的。
2. **LocalDumps 只认 HKLM，测试者机器没开。** 本机能有 dump，是因为 `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps`
   这个键存在（根键下没有值，只有 ASUS / Razer / NVIDIA 等软件各自的子键；推测是这些软件的安装器建子键时带出来的）：只要这个键在，WER 就按默认值
   （`%LOCALAPPDATA%\CrashDumps`、10 个、DumpType 1）给**所有**程序写 dump。没有这个键的机器上，崩溃一个 dump 都没有。
3. **本机的 WER dump 也从没被回传过。** 本机 EDF6 的 WER dump 每个约 100 MB（`EDF6.exe.138560.dmp` 101,522,075 字节），
   超过 `DUMP_MAX`（60 MB）；旧的 `collect()` 在列表推导里按大小直接过滤掉，`dumps.txt` 里一个字都没有。
4. **没有 dump 时 `dumps.txt` 也不写。** 旧代码只有 verdicts 非空才写；没有 dump 又没有 loading 行时什么都不留。
   （测试者装的是旧安装器 b4faa8e，连 adf7af42 的 `dump_in_session` / `dumps.txt` 都没有。）

## 修复

### 插件自己写崩溃 dump（`src/crash_dump.cpp`）

- `EML6_Load` 在第一个补丁前 `InstallCrashDump()`（插件从这里起不会被卸载，filter 指向的代码一直在）。
  写到 `%LOCALAPPDATA%\CrashDumps\EDF6.exe.<pid>.plugin.dmp`（`collect()` 的 `EDF6*.dmp` glob 能收到）。
  类型同 hang dump（`kDumpType`：ThreadInfo | IndirectlyReferencedMemory | ProcessThreadData | UnloadedModules），
  带 `MINIDUMP_EXCEPTION_INFORMATION`。实测游戏内 5.5 MB。dbghelp 的加载两边共用（`MiniDumpWriter()`）。
- **只抓未处理异常**：`SetUnhandledExceptionFilter`。被 `__try` / `catch` 处理掉的一次机会异常（EDF6Coop 的 VEH 记的那些、
  `_com_error` 等）根本到不了 top-level filter，不会写 dump（`crash_dump_test` 的 handled 用例）。
- **dump 由预先建好的专用线程写**：崩溃线程只做原子抢占 + SetEvent + 等待，栈溢出（只剩一页栈）也能写；
  其它同时崩溃的线程等这份 dump 写完再往下走（一次进程一份，`race` 用例 4 线程同时 AV 只有一份）。
  dump 线程自己崩了不会自锁；写 dump 超过 60 s 就放行给 Windows。
- **hang 与 crash 共用同一个写入服务**：`MiniDumpWriter()` 返回串行服务入口，不暴露原始 dbghelp 函数；
  watchdog 提交 hang 的文件 / 上下文并等完成，crash 使用独立事件，不拿普通提交锁；待处理的 crash 优先。
  所有实际 `MiniDumpWriteDump` 调用只在同一 worker 执行，避免任务开局卡住后又崩溃时并发重入。
  [微软要求 DbgHelp 调用串行](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)。
  若 worker 无法启动则明确失败，不回退到调用方并发调用；若 DbgHelp 自身卡死，crash 仍按 60 s 上限放行给 Windows。
- **先 dump，再独立追加诊断**：审查复现了原实现的死锁：故障线程若持有普通日志缓冲锁，dump 线程会卡在第一条 `Log("CRASH ...")`，
  到不了 `FlushLog` 的超时保护，更到不了 dump。现在先写 dump，再用固定缓冲自行格式化整数 / UTF-16 文本，直接以 `FILE_APPEND_DATA`
  追加 `CRASH ...` 与结果行；不经过普通日志的缓冲 / 文件锁、`InitOnce` 或 printf / locale 锁。诊断追加失败不影响已生成的 dump。
  模块名用 `VirtualQuery` + `K32GetMappedFileNameW`，不碰加载器锁；截断的模块路径不作为以零结尾的字符串读取。
  普通日志仍为 50 ms 一刷；`FlushLog` 通过共用 `log_buffer.h` 一次性 try-lock 并交换缓冲，失败就返回，消除先释放再重获锁的窗口。

### filter 覆盖情况（静态 + 实测）

- 静态（`pefile` + capstone，找 `SetUnhandledExceptionFilter` 的真实调用点、看参数）：
  - `EDF.dll`：只有 CRT 自带的 `__raise_securityfailure` / `__scrt_fastfail`（`SetUnhandledExceptionFilter(NULL)` 再自己调
    `UnhandledExceptionFilter`，`/GS` 失败路径），`__scrt_set_unhandled_exception_filter`（0x12D9DA4）没有任何引用。**游戏 DLL 不装 filter。**
  - `EDF6.exe`：启动时（`0x10E0` → `0x17DC`）装 CRT 的 `__scrt_unhandled_exception_filter`，早于插件加载；我们装的时候拿到它，崩溃写完 dump 后链给它。
  - EDF6Coop、Patcher、steam_api64(_o)、umbra、EOSSDK、gameoverlayrenderer64、steamclient64、vstdlib：只有 CRT 的 NULL 调用。
  - `tier0_s64.dll` 导出 `EnableCrashingOnCrashes`（会装一个写 dump 后 `TerminateProcess` 的 filter），但游戏进程里没有任何模块静态引用它。
- 不靠「没人会覆盖」：`kernelbase!SetUnhandledExceptionFilter` 开头改成跳到我们的函数，之后谁再设 filter 都排在我们后面、
  dump 后按顺序调用它（调用方拿回的是排在后面的那个，不会再链回我们）。开头已经是跳转（别人 hook 过）就不动，
  改为每次任务开始 `CheckCrashFilter()` 把 filter 放回最前并记日志。
- **实测（游戏内，2026-10-18 18:13，RM015，autopilot）**：日志
  `CRASHDUMP installed: ...EDF6.exe.138560.plugin.dmp (dbghelp 1, dumping thread 1, filter kept first by redirect, ucrtbase abort/invalid parameter 1)`；
  从插件加载（steamclient64 / tier0 / EOSOVH / NVIDIA 等都在其后加载）到任务开始没有任何模块再调 `SetUnhandledExceptionFilter`
  （没有 `CRASHDUMP SetUnhandledExceptionFilter(...)` 行），任务开始时 ucrtbase 的 SIGABRT / 非法参数 handler 也没被换掉。
  ReShade / dxgi 代理：本机没装，没实测；它们若调 `SetUnhandledExceptionFilter` 会被重定向排到后面，若先于我们 inline hook 了该函数则走任务开始的重挂。

### CRT abort / terminate / 非法参数

- 插件是静态 CRT（`CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded`）；游戏（EDF.dll / EDF6.exe / mod.dll / Patcher …）用 `ucrtbase.dll`。
- ucrtbase 的 `abort()` 先 `raise(SIGABRT)`，`std::terminate()` 默认调 `abort()`（以前 bad_cast → ucrtbase abort 就是这条），
  所以给 **ucrtbase** 的 `signal(SIGABRT)` 装 handler（GetProcAddress），在 handler 里 `RtlCaptureContext` 造异常上下文（码 `0x40000015`
  STATUS_FATAL_APP_EXIT）写 dump，然后照原路径继续（fast fail）。ucrtbase 的 SIGABRT 是进程全局的；`set_terminate` 在 UCRT 里是**每线程**的，
  所以不用它（SIGABRT 已覆盖 terminate）。插件自己的静态 CRT 也装一份。任务开始时再查一次，被换掉就记日志并串在我们后面。
- 非法 CRT 参数：ucrtbase 没有 handler 时会 `_invoke_watson` → fast fail。装全局 handler：写 dump（码 `0xC0000417`）后照样调
  `_invoke_watson`。若已有别人的 handler，交给它决定（它可能让调用返回 EINVAL，不是崩溃，不写 dump）。
- 栈溢出：见上，专用线程写。实测 `crash_dump_test` 的 stack 用例 1.2 MB dump，cdb 栈 `__chkstk ← Deep ← Deep …`。

### 仍然抓不到的

- `__fastfail`：内核直接结束进程，进程里任何代码都不运行。包括 `/GS` 栈破坏检测、CFG、堆管理器发现的堆损坏、
  非法参数 handler 被别人接管后由它 fast fail。
- 进程被外部结束（任务管理器、反作弊、OOM 杀进程）、卡死（任务开局卡死仍由 hang dump 管）。
- 游戏代码自己 `__try/__except(EXECUTE_HANDLER)` 吞掉后用 `ExitProcess` 退出的「崩溃」：不是未处理异常。实测游戏线程上的 AV
  能穿过 EDF.dll 的帧到达 filter（见下），没发现游戏有这种全局吞法。
- 以上只能靠 Windows LocalDumps（HKLM，测试者机器没有）或 WER 报告（见下）。

### HKCU LocalDumps 实测：不生效，不做

本机测试（`tmp/hkcu/crashtest_hkcu.exe`，故意 AV / `__fastfail`；测前记录 HKLM 与 HKCU 现状：HKLM 有空的 LocalDumps 根键，
没有 EDF6 / 测试程序的子键；HKCU WER 下只有 `Consent, Hangs, Plugins, RuntimeExceptionHelperModules`，没有 LocalDumps）：

| 设置 | 结果 |
|---|---|
| 无 HKCU 键（对照） | dump 写到 `%LOCALAPPDATA%\CrashDumps`，188 KB（HKLM 根键默认值） |
| `HKCU\...\LocalDumps\crashtest_hkcu.exe`：DumpFolder=`tmp\hkcu\out`、DumpType=2 | 指定目录为空；dump 照旧写到默认目录，181 KB（不是 full dump）——**被忽略** |
| HKCU LocalDumps 根键上设 DumpFolder / DumpType=2 | 同上，被忽略；`__fastfail` 也由 HKLM 默认值写到默认目录 |

测后删掉 HKCU LocalDumps，HKCU WER 子键恢复为原来的四个（已复核）。HKLM 未碰。
结论：WER 只读 HKLM 的 LocalDumps，安装器**不**加 HKCU 配置（没用）；写 HKLM 需要管理员，安装器也不做。
fast fail 类崩溃没有兜底 dump，只有下面的 Report.wer。

### `tools/testhub.py collect()`

- `crash/dumps.txt` **总是写**。没有上传任何 dump 时写明原因：
  - 插件这边（读本次游戏的插件日志）：已写出 / 看到崩溃但没写成（引用 `CRASH` 行）/ 已装好但没走到（fast fail、被结束、卡死）/ 旧版插件没有此功能；
  - Windows 这边：`HKLM` LocalDumps 是否存在（以及目录 / 类型 / 个数）。
- 附上 Windows 自己的候选 `Report.wer`：`%LOCALAPPDATA%` 与 `%ProgramData%` 下 `Microsoft\Windows\WER\ReportArchive|ReportQueue\AppCrash_EDF6.exe_*`，
  只取这个游戏目录（`AppPath` / `UI[2]` / `LoadedModule[0]`）、本次游戏时段内（`EventTime` ≥ 插件 loading 行）最新一份，
  作为 `crash/Report.candidate.wer`，`dumps.txt` 里摘一行「异常码 于 模块+偏移」（`Sig[6]` / `Sig[3]` / `Sig[7]`，索引不受本地化影响）。
  路径与时间下界不能证明进程身份，可能来自后续未记 loading 的启动，因此明确标记「会话未确认」，不认作本次崩溃的确定报告。
  没有 dump 时仍保留这份候选证据。
- 插件 dump（`*.plugin.dmp`）与 Windows dump 各取一份：都要过 `dump_matches`（本次安装）与 `dump_in_session`（本次游戏），
  插件的优先；超过 `DUMP_MAX` 或会让上传超过 `UPLOAD_MAX` 的写进 `dumps.txt`，不再无声丢弃。
  大小检查在打开 dump 前完成，数 GB 的 full dump 不会先整份读入内存；元数据解析按偏移读取所需的有限区段。

## 验证

- `crash_dump_test`（ctest，子进程逐类崩溃，审查修复后 100 项检查全过）：AV（`C0000005`）、未捕获 C++ 异常（`E06D7363`）、栈溢出（`C00000FD`）、
  ucrtbase `abort` / `terminate`、插件静态 CRT `abort`（`40000015`）、ucrtbase 非法参数（`C0000417`）、之后再设的 filter 被排在后面并在 dump 后调用、
  4 线程同时崩溃只一份 dump；每份 dump 的异常流线程 = 崩溃线程；诊断回调进入前 dump 已生成；被处理的异常不写 dump。
  新增真实共用日志实现的缓冲锁 / 文件锁被故障线程持有、诊断文件打不开三类回归，均仍写出 dump 并正常走完崩溃链；
  try-only flush 的持锁退出、缓冲交换 / 顺序与丢行计数也单独验证。
  可控 fake writer 阻塞 hang 时提交 crash，验证最大 in-flight = 1、两份请求使用同一 worker，且各自文件路径 / 异常码 / 线程信息保持正确。
- cdb（`.ecxr; kc`）：
  - av：`crash_dump_test!AccessViolation ← kernel32!BaseThreadInitThunk ← ntdll!RtlUserThreadStart`，`mov dword ptr [10h],1`；
  - abort：`CaptureHere ← OnUcrtAbort ← ucrtbase!raise ← ucrtbase!abort ← Child ← main`；terminate 多一层 `ucrtbase!terminate`；
  - cpp：`KERNELBASE!RaiseException ← _CxxThrowException ← Throw`。
- 游戏内（RM015，autopilot `crash av game`：在游戏线程的 `GetKeyboardState` 里 AV）：
  `CRASH unhandled exception: exception C0000005 at EDF6Autopilot.dll+0xd919, writing 0x10, thread 106636` →
  `CRASH dump written: ...EDF6.exe.138560.plugin.dmp, 5507 KB`。cdb：`EDF6Autopilot!KeyboardState ← EOSOVH_Win64_Shipping ← EDF!… ← kernel32!BaseThreadInitThunk`，
  线程 0x1a08c = 106636 即游戏线程。同次崩溃本机 HKLM LocalDumps 写了 101 MB 的 `EDF6.exe.138560.dmp`（超 `DUMP_MAX`，新 `collect()` 会在 `dumps.txt` 里写明）。
  游戏目录由 harness 逐字节还原（DLL sha 与测前一致，autopilot 文件已移除）；两份 dump 已从 CrashDumps 挪进 worktree 的 `tmp/ingame/dumps/`。
- `tests/testhub_dump_source_test.py` 审查修复后 43 项、`tools/selftest.py` 的两项 dump 相关守卫通过（C++ 的文件名格式 / 日志行与 `collect()` 的 glob / 判定互相对上）。
  原生测试实际生成的 av / bufferlock / filelock 三份原始无时间戳日志也直接交给 `plugin_crash_note` 验证，均识别为已写出。
