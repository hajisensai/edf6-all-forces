# mem-3：同一进程反复进出任务，内存每轮上涨（归因进行中）

反馈：同一进程连续进出测试场 19 轮，mission start 时工作集 616→785 MB，约 +9 MB/轮；峰值提交 6199→7022 MB；第一次任务之后基线多出 0.7–0.9 GB。

本文记录已经量到的数据、测量工具和下一步。**根因还没归因到模块**，归因需要一次带堆探针的实机运行（见文末「下一步」）。

## 测量工具（tests/autopilot，只在测试插件 EDF6Autopilot 里）

- `MEM <why>`（opt-measure 的 LogMemory）：进程提交 / 峰值 / 工作集，以及整机剩余。
- `MEMMAP <why>`：用 VirtualQuery 遍历地址空间，按 private / mapped / image 汇总提交。private 再拆成两部分：各堆的 committed（HeapSummary）和「the rest」（堆以外的 VirtualAlloc，EDF.dll 的大池 / 显存影子等），同时给出分配数和 ≥1 MB 大块的数量。
- `HEAPS <why>`：committed 最大的 4 个堆，格式为 committed / allocated。
- cycle 每轮在 `cycle entered` 和 `cycle left` 各记一次 MEM + MEMMAP。
- `--cmd "probe heap N"`：堆探针（`tests/autopilot/heap_probe.cpp`），作用类似 UMDH 两次快照做差。
  - 本机不是管理员，gflags +ust（HKLM IFEO）和 ETW heap 都用不了，所以改在进程内换导入表。
  - 到第 N 次 `cycle left` 时，把当时已加载的每个模块（ntdll / kernel32 / KernelBase 和探针自身除外）导入表里的 HeapAlloc / HeapReAlloc / HeapFree / VirtualAlloc / VirtualFree 换成探针版本。EDF.dll 经 ucrtbase 的 malloc 最终也走 ucrtbase 导入的 HeapAlloc，所以同样会被记到。
  - 之后每次 `cycle left` 输出三类行：
    - `HEAPLIVE`：起点之后出生、此刻仍存活的总量。
    - `HEAPMOD`：按调用栈里第一个非系统模块归类。
    - `HEAPSITE`：最大的 30 个调用路径（6 帧，`模块+RVA`），用插件 PDB（cdb）或 `vanilla.py show <rva>` 符号化。
  - 离线单测 `tests/heap_probe_test.cpp`（ctest `heap_probe`）验证了四点：保留的块按调用路径正确计入；释放的块不计；realloc 只按最终大小计一次；VirtualAlloc 单独计、释放后移除。

## A1：全插件，RM015，cycle 16 轮，每轮停留 20 s（2026-11-24，已验证）

每轮 `cycle entered`（进入任务、加载完成后）的数据：

| 轮 | 提交 MB | 工作集 MB | 堆 committed | 堆 allocated | the rest | ≥1MB 块 |
|---|---|---|---|---|---|---|
| 1 | 1382 | 432 | 228 | 215 | 984 | 74 |
| 2 | 2172 | 564 | 443 | 226 | 1556 | 115 |
| 3 | 2187 | 582 | 473 | 231 | 1540 | 113 |
| 4 | 2190 | 591 | 493 | 236 | 1523 | 120 |
| 5 | 2183 | 593 | 501 | 240 | 1508 | 125 |
| 6 | 2224 | 611 | 507 | 244 | 1543 | 122 |
| 7 | 2215 | 614 | 520 | 250 | 1520 | 119 |
| 8 | 2238 | 634 | 532 | 253 | 1532 | 122 |
| 9 | 2259 | 662 | 541 | 257 | 1543 | 124 |
| 10 | 2585 | 782 | 542 | 261 | 1756 | 131 |
| 11 | 2376 | 686 | 539 | 265 | 1663 | 129 |
| 12 | 2462 | 784 | 536 | 269 | 1753 | 118 |
| 13 | 3244 | 836 | 583 | 279 | 2486 | 146 |
| 14 | 2515 | 832 | 578 | 283 | 1762 | 140 |
| 15 | 2474 | 717 | 575 | 287 | 1725 | 133 |
| 16 | 2511 | 814 | 575 | 291 | 1762 | 132 |
| 17 | 2522 | 823 | 575 | 295 | 1773 | 131 |

image 497 MB、mapped 91 MB 全程不变，没有 DLL 或映射泄漏。

分成三种成分：

1. **第 1→2 轮 +790 MB 的阶跃（复现了反馈里的「基线多 0.7–0.9 GB」）**
   - 「the rest」+572 MB（≥1 MB 大块从 74 个增加到 115 个）。
   - 堆 committed +215 MB，但 allocated 只多 11 MB，也就是 NT 堆保留了第一次任务释放的空间，没有还给系统。
   - 推测（未验证）：第一次任务建起的原版池 / 缓存在撤退后保留，再加上堆保留，而不是逐轮泄漏。之后「the rest」不再线性增长，支持这个推测。
2. **线性成分：主进程堆 allocated 每轮约 +4.6 MB**（215→295，16 轮共 +80 MB；`cycle left` 一列从 234 到 307，斜率相同）。这是唯一稳定单调的量，也是要归因的对象。来源模块未知：可能是插件，也可能是 EDF.dll，两者都经进程堆分配。
3. **「the rest」的阶跃 / 尖峰**：第 10 轮 +213、第 13 轮 +724（下一轮回落）。不是线性的，像池按需扩张或峰值场景时的临时分配。推测（未验证）是原版行为。

反馈里约 9 MB/轮的工作集增长，在 A1 里约为 16 MB/轮（564→823），其中堆的线性泄漏约占 4.6 MB/轮，其余是堆保留和池阶跃的页面变热。

## 下一步（实机，一次运行）

```
python tests/autopilot/drive.py run RM015 1 1800 <LOG> --cycle 12 --cycle-stay 20 --cmd "probe heap 2" \
  --place build/Mods/Plugins/EDF6VehicleCrew.dll=Plugins/EDF6VehicleCrew.dll \
  --place build/Mods/Plugins/EDF6AutoTurret.dll=Plugins/EDF6AutoTurret.dll
```

- 判据：HEAPLIVE 存活堆随 mark 线性增长约 4–5 MB/轮，`dropped` 为 0 或很小。
- 用 HEAPMOD 看大头在哪个模块：
  - 在 EDF6VehicleCrew / EDF6AutoTurret：按 HEAPSITE 的 RVA 用对应 PDB 符号化，修复生命周期，修复配离线单测，修后同参数（不带 probe）复跑，对比每轮 allocated。
  - 在 EDF.dll：用 vanilla.py 看站点；再跑一组原版对照（不放两个插件）确认原版也涨，写原版结论，不硬修。
