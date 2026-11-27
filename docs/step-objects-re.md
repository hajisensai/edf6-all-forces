# System 对象表与按帧快照（stepobj）

实现：`common/edf/step_objects.h`、`common/step_objects.cpp`；挂钩：`src/step_objects_hook.cpp`；测试：`tests/step_objects_test.cpp`、`tests/step_objects_guard.py`、`tools/npc_core_check.cpp`（计数）。
本文只记结论（以 Steam 版 `EDF.dll` 0x678CCB46 的汇编为准）。

## 为什么要做

stress:100 每步约 6.3 万次 `edf::Readable`，每帧 4.6–8.15 ms。绝大多数请求问的是「这个指针还是不是活对象」，而这些对象本帧都在 System 的对象表里。
所以根因修法是：每帧拷一次对象表，命中就不问 Readable；不再让每个检查各自变快。

## System 的对象表

- System 指针在 `EDF+0x20B2958`。
- `+0x3D0`：持有引用的全部对象表，节点数在 `+0x3D8`。`+0x3B0`：本帧新加的对象表，节点数在 `+0x3B8`。两张都是双向链表，头节点作哨兵。
- 节点布局：`next +0x00`、`prev +0x08`、对象指针 `+0x10`、控制块 `+0x18`。节点持有强引用，所以对象在表里就不会被释放。
- 两张表由 `+0x308` 的 CRITICAL_SECTION 保护。快照用 TryEnter：拿不到锁就不拷，本期退回逐个问 Readable。
- `+0x3E0` 只是脏集合，不是全部对象，不能拿来做快照，所以没有挂 0x1195F20。
- 删除对象会置 `+0x18` 的 bit 2（值 4）。

## 什么时候释放

- `0x1197680` 是释放函数，会放掉表里已删除对象的强引用。三处调用都是 E8，返回值都没用到：
  - `0x1198F71`：帧内调用，在 tick 循环之前，也就是每帧的起点。
  - `0x11959F8`、`0x1196A0D`：flush。
- `0x11956C0` 是拆除函数（rcx=System），会清空两张表，不经过释放函数，直接放掉对象。它有 25 处调用：24 处 E8，`0x22487D` 是 E9 尾跳。抽查过的调用点都从 `0x20B2958` 取 rcx。
  - 如果同一帧里先拆除、再加回对象，旧快照里的指针会被误判为活对象。所以 25 处全挂，拆除后关闭本期。

## 快照期（period）

- 释放 / 拆除开始前调 `Releasing()`：期号加一并记「正在释放」，这期间 `Period()` 返回 0。析构函数在这时查询，走的是 Readable，不会读到旧快照。
- 结束后调 `Released(frame)`：只有帧内那处调用传 true，它记下游戏线程并开新期。拆除结束后调 `Close()`。
- 任务场景结束（`mission.cpp` ReleaseGroupHook）也调 `Close()`。
- 拷贝是懒的：每期第一次查询时才拷，并且只在游戏线程上拷。别的线程、没开期、没装好时 `Period()` 都返回 0，调用方逐个问 Readable，行为和原来一样。
- 挂钩前先核对 28 个调用点的 E8/E9 字节和目标。有一处不符就写 `STEPOBJ refused`，不安装。
- AutoTurret 链接了 edf6common，但不调用 Install，所以它那边的查询结果全是 false，保持旧行为。

## 调用方怎么用

- `stepobj::Reads(p, n)` 等于 `Trusted(p) || edf::Readable(p, n)`。对象在本期快照里就不再问 Readable。
- 判断「这个对象还活着」用 `Has`。弱引用的控制块等于 `ControlOf(p)` 时，就是同一个活对象（座位乘员、npcai 的目标 / 消失判断、WastedFire 锚点）。
- 判断 vtable 改用 `edf::InImage`（`common/edf/host.h`），只比较是否落在映像范围内，不再走 Readable。
- 本帧不变的量用 `stepobj::Once` 按期缓存，每期只读一次：
  - 会话 `InSession`（`ReadSession`）
  - 暂停 `GamePaused`（`ReadPaused`）
  - 房主 `Host`（`ReadHost`）
  - 队伍关系表和锁定表头（`world_tables.h`）。锁定表的头节点每次都复核，节点数每次现读。
- 快照里的对象读成员时不再做尺寸检查。npcai 的 PreThink / Think 跑在 ThinkHook 的 `__try` 里，`seat.cpp` 的成员读也放进了 `__try`。
- `live_set`（npcai 自己登记的对象集）由快照取代，已删除。

## 尚未核实的假设

- `SeatCount` 假设快照里的载具，其座位数组整帧不会重新分配。
- 按期缓存的量最多晚一帧。
- 拆除函数的 25 处调用点没有逐个确认 rcx 都是 System，只抽查过一部分；挂钩不依赖这一点，只会多关几次期。
