# ai::World 的寻路 agent 上限（1024 → 65534）

2026-11-07。对象是 Steam 版 EDF.dll（TimeDateStamp `0x678CCB46`），地址都是 RVA（ImageBase `0x180000000`）。实现见 `src/aiworld_capacity_logic.h`（改点数据）、`src/aiworld_capacity.cpp`（安装），检查见 `tests/aiworld_capacity_test.cpp`（离线）和 `tests/aiworld_capacity_audit.py`（对真 EDF.dll）。

## 背景

`tools/perfrun.py stress:100`（分支 `perf/1107-perfrun`，`docs/stress-re.md`）在士兵过千时崩在 AI 线程 `EDF.dll+0xF3E9D`。原因是 ai::World 的 agent id 只有 1024 个，用完后新 agent 拿到 `0xFFFF`，读写 agent 表时越界。蚂蚁不占 id，人形士兵每人占一个（`stress:30` 有 1123 只蚂蚁、662 名友军，没有崩）。

## ai::World 的布局

- RTTI `.?AVWorld@ai@@`，vtable `0x1768A48`：槽 0 `0xB4F790`，槽 1 `0xEED00`（deleting destructor），槽 5 `0xF3E60`。
- 大小 `0x2180`。只有 `0xEBE40` 创建它：`0xEBE5F mov ecx,0x2180` → `0xEBE64 call 0x97A6B0`（Havok 线程内存路由的 heap `blockAlloc`）→ `0xEBE76 call 0xEE020`（构造）→ `0xEBEEE call 0xEF180`（初始化）。之后挂进管理器的 map，由 shared_ptr 控制块（vtable `0x17688E8`）持有，槽 0 `0xEC580` 调 World 的 vtable 槽 1 释放。
- `0xEBE40` 由 `0x121470` 调两次（`0x122A76`、`0x122ABE`），所以同一场景里可能同时有两个 World。每个 World 都走同一条创建 / 释放路径，扩容对每个都成立。
- 释放：`0xEED00` → `0xEE770`（析构，构造之外唯一引用 vtable 的地方）→ `0xEED25 mov edx,0x2180` → `0xEED2D call 0x97A760`（sized free）。`0x97A760` 先看对象 `+8` 的 qword，等于 `0xFFFFFFFF` 时用调用方给的大小；World 构造时 `+8` 写的就是 `0xFFFFFFFF`，所以释放大小以 `0xEED25` 的立即数为准。另一分支（flag 4）`0xEED41 mov edx,0x2180` → `0x7DFA0`，`0x7DFA0` 只是 `ret 0`。
- `+0x58`：CRITICAL_SECTION（`0x50C90` 初始化，`0x50BC0` / `0x50BE0` 进出）。分配、注册、注销、回收都在锁内。
- `+0x80 + id*8`：agent 指针表，内联在对象里，`0x401` 个槽（`+0x80..+0x2088`）。id 0 不发。
- `+0x2088`：空闲 id 栈 `vector<uint16>`（数据 `+0x2090`、容量 `+0x2098`、个数 `+0x20A0`），扩容函数 `0xF0E60`。

## id 的发放与回收

- 初始化 `0xEF180`：
  - `0xEF5C8 lea rax,[rdi+0x80]`、`0xEF5CF mov ecx,0x80`，循环每次清 8 个槽，循环后再清 1 个，共 `0x401` 个。
  - `0xEF613 mov edi,0x401`：先 `reserve(0x401)`，再 `inc si / cmp si,di` 把 id 1..0x400 压进空闲栈。
- 分配：`0xF1EF0`（注册已有 agent）和 `0xF2540`（新建 agent 再注册）。空闲栈为空时调 `0xE7C60(agent, 0xFFFF)`，不登记；否则弹出一个 id，`0xE7C60` 写 `[agent+0x40]=id`、`[[agent+0x250]+0x18]=id`，再写槽（`0xF1F54` / `0xF260C`）。
- 注销：`0xF3740`、`0xF37D0` 读 `[agent+0x40]`，清槽（`0xF3766` / `0xF380E`），把 id 压回空闲栈。
- 回收循环 `0xF3AA0`：遍历 `+0x2100` 链表里引用计数为 1 的 agent，清槽（`0xF3B3D`），压回 id（`0xF3B76`）。
- 读槽：`0xF3E90`（vtable 槽 5 `0xF3E60`，id 取自 `[[rdx+8]+4]` 的 dword 低 16 位，只排除 0）和 `0xF1526`（`0xF1440` 的一个分块；`0xF1440` 是 `ai::World::CostModifier`（RTTI `.?AUCostModifier@World@ai@@`，vtable `0x17689E0`）的槽 5，World 取自它的 `+0x18`；这个对象由初始化在 `0xEF587..0xEF5A8` 创建，挂在 World `+0x50`）。
- 以上读写都不检查 `0xFFFF`。id 用完后：注册时不写槽，但注销 / 回收时照样往 `slot[0xFFFF]`（World 之后约 512 KB 处）写 0，再把 `0xFFFF` 压回空闲栈；`0xF3E60` 读 `slot[0xFFFF]` 后解引用，崩在 `0xF3E9D`。

## 改法

agent 表挪到一个更大的 World 分配的末尾，id 放宽到 uint16 能表示的全部（`0xFFFF` 仍是原版的「无 id」，不发）。所有改动都是单条指令内的立即数或位移，World 的创建、清零、加锁、释放仍由游戏自己的代码完成，插件不在任何 agent 路径上运行。

| RVA | 原指令 | 改为 | 作用 |
|---|---|---|---|
| `0xEBE5F` | `mov ecx,0x2180` | `mov ecx,0x82188` | 分配大小 = `0x2180` + `0x10001` 个槽 × 8 |
| `0xEED25` | `mov edx,0x2180` | `mov edx,0x82188` | sized free 的大小 |
| `0xEED41` | `mov edx,0x2180` | `mov edx,0x82188` | flag-4 分支（no-op），保持一致 |
| `0xEF5C8` | `lea rax,[rdi+0x80]` | `lea rax,[rdi+0x2180]` | 清零循环从新表开始 |
| `0xEF5CF` | `mov ecx,0x80` | `mov ecx,0x2000` | 清 `0x2000×8+1 = 0x10001` 个槽 |
| `0xF1526` | `mov rsi,[rax+rcx*8+0x80]` | `+0x2180` | cost modifier 读 agent |
| `0xF1F54` | `mov [rdi+rbx*8+0x80],rcx` | `+0x2180` | 注册 |
| `0xF260C` | `mov [rsi+rbx*8+0x80],rcx` | `+0x2180` | 新建并注册 |
| `0xF3766` | `mov qword [rdi+rbx*8+0x80],0` | `+0x2180` | 注销 |
| `0xF380E` | `mov qword [rsi+rbp*8+0x80],0` | `+0x2180` | 注销 |
| `0xF3B3D` | `mov [rsi+rbp*8+0x80],r14` | `+0x2180` | 回收循环 |
| `0xF3E90` | `mov r8,[rcx+rax*8+0x80]` | `+0x2180` | vtable 槽 5 |
| `0xEF613` | `mov edi,0x401` | `mov edi,0xFFFF` | 空闲栈 reserve `0xFFFF`、压入 id 1..0xFFFE |

- 表有 `0x10001` 个槽（下标 0..0x10000），任何 uint16 id（包括 `0xFFFF`）都落在表内。万一真用满 65534 个 id，`0xFFFF` 读到的是表内的 0，写也写在表内，不会再写到 World 之外。
- 每个 World 多占约 512 KB（`0x82188` 字节），初始化多清 512 KB、多压 65534 个 id，只在任务载入时发生一次。空闲栈 `reserve(0xFFFF)` 多占 128 KB。
- 原来的内联槽 `+0x80..+0x2088` 改后没人读写，清零循环也不再碰它，留作空闲。
- 线程：改的都是原来的指令，锁和调用线程都没变（分配 / 注销 / 回收在 `+0x58` 的锁内；`0xF3E60` 在 AI 线程上不加锁读，和原版一样）。
- 安装：`InstallAiWorldCapacity()` 在插件加载时、游戏 init 之前运行（紧跟 `InstallTeamCapacity()`），这时还没有任何 World。先逐个核对 13 处的原始字节，任何一处不符就记一行 `AIWORLD refused` 并完全不改；写入顺序是「分配变大 → 释放大小 → 清零 → 7 处槽 → id 上限」，任何一处写失败就倒序还原已写的。成功时日志 `AIWORLD agents=65534 (stock 1024) world=0x82188 bytes sites=13`。

## 查过的范围

- 槽表的所有访问：
  - 全 `.text` 里 `[base + index*8 + 0x80]` 的 qword 访问一共 29 处：7 处是上面的改点；其余 22 处在 `0x95F930`、`0x1004548`、`0x12F8B30`、`0x15917CB` 四个函数里，逐个看过，base 都不是 World（各自的理由写在 `tests/aiworld_capacity_audit.py` 的 `NOT_WORLD` 里）。
  - 「先算好 `world+id*8` 再加 `0x80`」或「id 先左移 3 位再用 scale 1」的写法：全 `.text` 线性反汇编扫过一次，只有 `0xBFFD89` 一处字节访问，与 World 无关。这一项没写进 ctest，因为全量反汇编要 1 分钟以上。
  - World 自己的代码：ai 模块 `0xE4000..0xF8000` 里，凡是经参数寄存器碰到 World 专有字段（`+0x2088..+0x2180`）的函数（33 个），追踪该寄存器的副本，它们对 `+0x80..+0x2087` 的访问只有初始化的 `lea rax,[rdi+0x80]` 和上面的槽访问。没有任何按指针遍历整个槽表的循环；遍历 agent 都走 `+0x2100` 链表。
  - World 指针还会存进别的对象（如 `0xEF5A8` 写 `[CostModifier+0x18]=world`），经这些对象的访问靠上面的全 `.text` 索引扫描覆盖（`0xF1526` 就是这样找到的）。
- 大小 `0x2180`：只有 `0xEBE40` 创建（构造函数 `0xEE020` 唯一调用点 `0xEBE76`），只有 vtable 槽 1 释放（`0xEE770` 唯一调用者 `0xEED00`，`0xEED00` 没有直接调用者），这两个函数里的 `0x2180` 都是改点。
- 其他按 id 编址的数组：
  - ai 模块里没有别的 `0x400` / `0x401` / `0x3FF` 上限，也没有 10 位打包。`0xEAEDF`、`0xF4C84`、`0xF5248` 的 `0x400` 是别的容器的大小；`0xE4A2B`（agent 构造写 `[agent+0x40]=0xFFFF`）、`0xEA681`（注销时把 `[char+0x18]` 写回 `0xFFFF`）、`0xF6620` / `0xF67F2`（射线查询的过滤值）都和上限无关。
  - id 还会传给 `0xE7170`（第 5 个参数）→ `0xB7E5E0`，以 dword 存在 `+0xC4`，并写进 `[char+0x18]`（qword）。读回的地方（`0xF14FC`、`0xF3E83`）都是 dword 读再 `movzx` 16 位，id 到 `0xFFFE` 都放得下。
  - 没有找到其他以 agent id 为下标、长度 1024 的数组。

## 未验证

- 没有实机跑过：不知道 1024 之后还有没有别的上限（Havok AI 自身的 character / 避障容量、寻路线程的负载、Havok heap 的预算）。下一步用 `stress:100` 去掉 `MAX_SQUADS = 120` 实测，看士兵过千后能否稳定运行。
- `0x97A6B0` 对 `0x82188` 字节的分配：按 Havok heap 的惯例，大块交给 large-block 分配器，没有逐行核对。
- `0xB7E5E0` / `+0xC4` 这一侧的消费者（Havok 侧对这个 dword 的用途）只确认了宽度，没有追完所有读者。
