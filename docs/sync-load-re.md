# 资源缓存 0x84E80 的未命中同步加载（hitch-2）

2026-11-24，静态逆向（`vanilla-code` 汇编，结论以汇编为准）。地址均为 EDF.dll（TimeDateStamp `0x678CCB46`）的 RVA。
探测：`src/sync_load_probe.cpp`；离线核对：`tests/sync_load_probe_native_audit.py`（ctest `sync_load_probe_native`，需
`EDF6_NATIVE_DLL`）。审查条目：`docs/reviews/2026-11-24-optimization-audit.md` hitch-2。

## 结论

- `0x84E80(cache, out, key)` 是资源缓存的「取或读」：`cache` 是全局 `0x20B28D0`（CreateObject `0x11945E0` 在 `0x11946DF`
  调它，全 DLL 共 39 处调用），`key` 是 `{ std::wstring 路径 (+0..+0x1F)；uint 哈希 (+0x20) }`，`out` 是 16 字节句柄
  （条目指针 + 缓存指针；条目 +0x70 是引用数）。H
- **未命中时在调用线程里同步读文件**，审查里推测的「经 `0x89360`」不准确：`0x89360` 只是把新条目插进已完成表（`std::map`
  find-or-insert，节点 0x78 字节），真正读文件的是条目 +0x68 的读取器。H
  - `0x85012 call [0x2005540]`：读取器工厂（启动时按 CPK / 散文件写入 `0x73970` → `CpkContentFileReader`，或
    `0x42C90` → `FileReader`），对象存条目 +0x68。
  - `0x8502A call [vtbl+8]`（`0x74970` / `0x42D80`）Open(条目 +0x48 缓冲, 路径)：`0x3E5A0` **新建一个线程**
    （`CreateThread` 挂起再 `ResumeThread`）去读文件，`0x3E700` 只是给它起线程名（`0x406D1388`）。
  - `0x85052 call [vtbl+0x10]`（`0x74BB0` / `0x42D10`）Wait(0)：参数 0 时走 `0x3E690`，
    **`WaitForSingleObject(线程, INFINITE)`** 后关句柄。所以每次未命中 = 起一个线程读完整个文件，调用线程阻塞到读完。
  - 读完只是字节进了条目缓冲；SGO / MDB 的解析在调用方（例如 CreateObject）之后做，不在 `0x84E80` 里。M
- 第二种阻塞：**等在读条目**。查找 `0x9C950` 先查「在读表」（`cache+0x10`，锁 `cache+0x50`），命中在读条目时在 `0x9C9F4`
  锁该条目的互斥量（条目 +0x50，读的线程从 `0x84FB3` 起一直持有到读完 `0x85118`），即等别的线程（例如加载线程的预载）把它读完。H
- 未命中分支结构（`0x84E80`）：
  - `0x84ECF call 0x9C950` 第一次查找（r8 = key，来自 r15）；`0x84ED9 test r14,r14; je 0x84F33`：命中走 `0x84EDE`，
    加引用后 `0x84F2E jmp 0x85159`。
  - 未命中 `0x84F33`：新建条目互斥量（`0x50C90`，0x28 字节）并锁住，`0x84F65 call 0x891C0` 插进在读表，解锁
    `cache+0x50`；锁 `cache+0x28`，`0x84FDA call 0x89360` 插进已完成表，解锁；然后上面的工厂 / Open / Wait。
  - 读失败（文件不存在）：`0x850CD call 0xBAF80` 删掉已完成表里的条目、`0x850EF call 0xBAD10` 删在读条目，`out` 置空，
    `0x8510F jmp 0x85159`。这就是 `0x11947EC` 故意崩溃前看到的空句柄（`docs/mission-airstrike-re.md` §3a）。
  - 读成功：条目引用数清零（`0x85111`），删在读条目（`0x85131 call 0xBAD10`），`0x8513F call 0x9C950` 再查一次填 `out`。
  - 三条路都在 `0x85159` 汇合，`0x8515D call 0x50BE0` 是最后一次解锁（`cache+0x50` 的锁守卫）。函数序言之后 rsp 不再变，
    所以同一次调用里各个调用点的返回地址槽地址相同，探测按它配对（嵌套调用也能分开）。H
- 锁：`0x50BC0` 构造锁守卫并加锁，`0x50BE0` 析构（持有则解锁），`0x50C60` 解锁，`0x50C10` 重新加锁；`cache+0x50` 可重入
  （`0x84EBF` 已持有，`0x9C979` 再锁）。M

## 探测（`src/sync_load_probe.cpp`）

- Debug=1 且 `SyncLoadProbe=1`（默认 1，改后要重启游戏）时，在 perf_probe 之后安装。先逐字节核对 13 段签名（序言、key 来源、
  命中判断、两条 jmp、工厂 / Open / Wait、读失败、出口），任何一段不符就写一行 `HOOK sync load probe NOT installed` 并不装。
- 四个调用点链式接入（`perfprobe::ChainCall`：只接受原函数或别的钩子的 near thunk）：`0x84ECF`（查找，计时等待）、
  `0x84FDA`（标记未命中）、`0x850CD`（标记文件不存在）、`0x8515D`（出口：解锁之后结算）。
- 只在游戏线程（perf_probe 帧锚点 `0x11A2FF3` 每帧记下的线程）上记账；别的线程（加载线程的预载、菜单的模型作业线程）直接透传。
- 最外层 `0x84E80` 返回后（此时不持有缓存锁）才写日志：
  - `SYNC LOAD <路径> <ms> ms`：游戏线程上的一次未命中（整次 `0x84E80` 调用的时间）；文件不存在时末尾加 `not found`。
  - `SYNC WAIT <路径> <ms> ms`：查找等了 ≥ 0.5 ms（别的线程正在读它，或缓存锁被占）。
  - `SYNC LOAD STATS since load, game thread: ...`：累计调用数、读次数与总毫秒、等待次数、钩子自身每次调用的耗时（含它自己的
    QPC 读数），最多每 10 s 一行。

## 实测

见 PR 与 `docs/plan-2026-11-24-optimization.md`（hitch-2 一节）。
