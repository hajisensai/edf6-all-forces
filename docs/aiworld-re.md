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
- 避障过滤：`ai::World::AvoidanceFilter`（RTTI `.?AVAvoidanceFilter@World@ai@@`，vtable `0x1768A18`）由初始化在 `0xEF43E..0xEF450` 创建（`[filter+0x18]=World`），存在 World `+0x20D8`。它的槽 3 `0xF4160` 和槽 4 `0xF41B0` 是**没有 `.pdata` 的叶函数**，对一对 Havok character 各取 `[char+0x18]` 的 id，读两次槽：`0xF4168` / `0xF4175`、`0xF41C4` / `0xF41EA`，随后直接解引用（`0xF417D` / `0xF41CC` 读 `[agent+0xE8]`），不判空。`0xF41B0` 只对第二个 id 判 `0xFFFF`。
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
| `0xF4168` | `mov rdx,[r9+rax*8+0x80]` | `+0x2180` | 避障过滤槽 3，第一个 agent |
| `0xF4175` | `mov rcx,[r9+rax*8+0x80]` | `+0x2180` | 避障过滤槽 3，第二个 agent |
| `0xF41C4` | `mov r8,[rcx+rax*8+0x80]` | `+0x2180` | 避障过滤槽 4，第一个 agent |
| `0xF41EA` | `mov rcx,[rcx+rax*8+0x80]` | `+0x2180` | 避障过滤槽 4，第二个 agent |
| `0xEF613` | `mov edi,0x401` | `mov edi,0xFFFF` | 空闲栈 reserve `0xFFFF`、压入 id 1..0xFFFE |

- 表有 `0x10001` 个槽（下标 0..0x10000），任何 uint16 id（包括 `0xFFFF`）都落在表内。万一真用满 65534 个 id，`0xFFFF` 读到的是表内的 0，写也写在表内，不会再写到 World 之外。
- 每个 World 多占约 512 KB（`0x82188` 字节），初始化多清 512 KB、多压 65534 个 id，只在任务载入时发生一次。空闲栈 `reserve(0xFFFF)` 多占 128 KB。
- 原来的内联槽 `+0x80..+0x2088` 改后没人读写，清零循环也不再碰它，留作空闲。所以漏改一处读槽就是读到未初始化的内存再解引用，必崩（避障过滤那 4 处最初就漏了，见下）。
- id 的发放顺序没改：初始化按 1..0xFFFE 升序压栈，分配从栈尾弹出，所以第一个 id 是 `0xFFFE`（原版同理，第一个是 `0x400`）。考虑过改成降序压栈让小 id 先发，但这对漏改的读点没有任何缓解：漏改的读点读的是旧内联区，那里改后不再写入，与 id 大小无关；而且要同时改 `si` 的初值、`inc si`→`dec si`、循环终值和 `reserve` 的大小（同一个 `edi`），多 3 处改点却不换来安全性，所以不改。
- 线程：改的都是原来的指令，锁和调用线程都没变（分配 / 注销 / 回收在 `+0x58` 的锁内；`0xF3E60` 在 AI 线程上不加锁读，和原版一样）。
- 安装：`InstallAiWorldCapacity()` 在插件加载时、游戏 init 之前运行（紧跟 `InstallTeamCapacity()`），这时还没有任何 World。先逐个核对 17 处的原始字节，任何一处不符就记一行 `AIWORLD refused` 并完全不改；写入顺序是「分配变大 → 释放大小 → 清零 → 11 处槽 → id 上限」，任何一处写失败就倒序还原已写的。成功时日志 `AIWORLD agents=65534 (stock 1024) world=0x82188 bytes sites=17`。

## 查过的范围

- 槽表的所有访问：
  - 全 `.text` 里 `[base + index*8 + 0x80]`（disp32）的 qword 访问和 lea 一共 33 处：11 处是上面的改点；其余 22 处在 `0x95F930`、`0x1004548`、`0x12F8B30`、`0x15917CB` 四个函数里，逐个看过，base 都不是 World（各自的理由写在 `tests/aiworld_capacity_audit.py` 的 `NOT_WORLD` 里）。另有 14 处同样字节模式的是 xmm / 字节访问（不可能是 qword 指针槽），不计。
  - 第一版只认 `.pdata` 函数里的命中，丢掉了没有 `.pdata` 的叶函数，漏了避障过滤的 4 处（对抗审查发现：改后第一次避障配对查询就空指针崩，几乎任何有士兵的任务都会触发）。现在每个字节模式命中都按它所在代码单元（`.pdata` 函数，或没有 `.pdata` 时由相邻 `.pdata` 函数与 int3 对齐填充界定的一段）的线性反汇编解码，要求该位置正好是某条指令的位移；不在线性解码边界上的命中，再从命中前 1..5 字节逐个试解，确认都不是带下标的 qword `+0x80` 访问。没有 `.pdata` 的命中只有这 4 处，必须都是改点。
  - 「先算好 `world+id*8` 再加 `0x80`」或「id 先左移 3 位再用 scale 1」的写法：全 `.text` 线性反汇编扫过一次，只有 `0xBFFD89` 一处字节访问，与 World 无关。这一项没写进 ctest，因为全量反汇编要 1 分钟以上。在 World 自己的代码里（下一条），这两种写法会以「经 World 寄存器的带下标访问 / lea」被逐条拦下，那部分在 ctest 里。
  - World 自己的代码：代码单元包括 ai 模块 `0xE4000..0xF8000` 的全部 `.pdata` 函数、同一范围内全部没有 `.pdata` 的叶代码，以及 World（`0x1768A48`，this 就是 World）、CostModifier（`0x17689E0`）、AvoidanceFilter（`0x1768A18`）三张 vtable 前 8 槽的函数（后两者 World 在 `[this+0x18]`），共 653 个。每个参数寄存器按两种深度跟踪：它本身是 World，或它 `+0x18` 处装着 World（跟踪 `mov r,[holder+0x18]` 得到的寄存器）。经跟踪寄存器碰过 World 专有字段（`+0x2088..+0x2180`），或 vtable 说明它是 World 的，就算 World 代码（51 个单元，其中没有 `.pdata` 的 9 个：`0xEF6D0`（读 `+0x20E0`）、`0xF39A0`（写 `+0x20F0`）、`0xF4160`、`0xF41B0`，以及 vtable 里的通用小函数 `0x978D50`、`0xB975D0`、`0xB975E0`、`0xB9B010`、`0xB9B020`）。这些单元里经 World 寄存器对 `+0x80..+0x2087` 的任何访问、任何带下标的访问或 lea，都只能是改点（初始化的 `lea rax,[rdi+0x80]` 和 11 处槽访问）。没有任何按指针遍历整个槽表的循环；遍历 agent 都走 `+0x2100` 链表。
  - 审计自检：把避障过滤 4 处从改点里拿掉，索引扫描一节报 `FAIL 0xf4168 (no .pdata, 0xf4160) ... not a site`；再关掉索引扫描一节，World 代码一节也独立报 `FAIL 0xf4168 (0xf4160, depth 1) ... World +0x80..+0x2087 not a site`。
- 大小 `0x2180`：只有 `0xEBE40` 创建（构造函数 `0xEE020` 唯一调用点 `0xEBE76`），只有 vtable 槽 1 释放（`0xEE770` 唯一调用者 `0xEED00`，`0xEED00` 没有直接调用者），这两个函数里的 `0x2180` 都是改点。
- 其他按 id 编址的数组：
  - ai 模块里没有别的 `0x400` / `0x401` / `0x3FF` 上限，也没有 10 位打包。`0xEAEDF`、`0xF4C84`、`0xF5248` 的 `0x400` 是别的容器的大小；`0xE4A2B`（agent 构造写 `[agent+0x40]=0xFFFF`）、`0xEA681`（注销时把 `[char+0x18]` 写回 `0xFFFF`）、`0xF6620` / `0xF67F2`（射线查询的过滤值）都和上限无关。
  - id 还会传给 `0xE7170`（第 5 个参数）→ `0xB7E5E0`，以 dword 存在 `+0xC4`，并写进 `[char+0x18]`（qword）。读回的地方（`0xF14FC`、`0xF3E83`）都是 dword 读再 `movzx` 16 位；避障过滤 `0xF4164` / `0xF4170` 是 `movzx` word，`0xF41B0` 读 qword 后和 `0xFFFF` 比较再 `movzx`。id 到 `0xFFFE` 都放得下。
  - 没有找到其他以 agent id 为下标、长度 1024 的数组。

## 未验证

- 没有实机跑过：不知道 1024 之后还有没有别的上限（Havok AI 自身的 character / 避障容量、寻路线程的负载、Havok heap 的预算）。下一步用 `stress:100` 去掉 `MAX_SQUADS = 120` 实测，看士兵过千后能否稳定运行。
- `0x97A6B0` 对 `0x82188` 字节的分配：按 Havok heap 的惯例，大块交给 large-block 分配器，没有逐行核对。
- `0xB7E5E0` / `+0xC4` 这一侧的消费者（Havok 侧对这个 dword 的用途）只确认了宽度，没有追完所有读者。
- World 代码的跟踪只从参数寄存器出发、深度到 `[x+0x18]` 一层，过 call 丢弃易失寄存器，不跟踪栈上的溢出 / 重载、也不跨 `.pdata` 链式分块。从别处（管理器的 map、`+0x20D8` 之外的成员、全局）取到 World 再做**不带下标**的 `+0x80..+0x2087` 访问，只有在 ai 模块之外、或经这些路径时才可能漏；带下标的写法由全 `.text` 扫描兜底，不受此限。
- ai 模块之外没有 `.pdata` 的代码，只有落在字节模式命中上的才被解码检查；`0xE4000..0xF8000` 这个范围是按符号 / vtable 分布划的，没有逐个确认模块边界。
