# hknpEventDispatcher 的 uint16 索引溢出（stress:100 退关卡死循环）逆向

EDF.dll（Steam，TimeDateStamp `0x678CCB46`），下文地址均为 RVA（ImageBase `0x180000000`）。

## 结论

- 卡死的「属性池」不是 hknpBodyManager 的属性表，而是 **`hknpEventDispatcher`**（RTTI `.?AVhknpEventDispatcher@@`，
  vtable `0x18C1628`）的派生类 **`hknpEventMergeAndDispatcher`**（RTTI `.?AVhknpEventMergeAndDispatcher@@`，
  vtable `0x18C1678`）。它按「body id × 事件类型」给每个 body 挂一条 `hkSignal` 链表，链表节点和空闲表都用 **uint16 索引**。
- 根因：分配新节点时 `0xE75C5E movzx esi, word ptr [rcx+0x48]` 把节点数组的 size 截成 16 位当作新节点索引，
  但新节点的内存是按完整 size 定位的（`0xE75C71 movsxd r14,[rcx+0x48]` → `shl 4`）。size ≥ 0x10000 后，链表头 / next
  指向的是 `size-0x10000` 号老节点（别名），老节点的 next 被改写 → 不同 body 的链互相串起来、成环；索引 0xFFFF 本身又与哨兵
  相撞。实测 size=90514、cap=131072、一个 65312 节点的环。
- 退关时 `0xDA8B00`（hknpWorld 删 body 循环，`0xDA8E1F call 0xE76460`）→ `0xE76460`（onBodyRemoved）→
  `0xE76010`（沿链释放到 0xFFFF 为止）在环上永不结束。正常对局里删 body 走的是同一条路径，同样会卡死，不只是退关。
- 修法（下文「加宽设计」）：把节点 next、空闲表头、全局链表头、每 body 表头数组、待释放列表全部从 uint16 加宽到 uint32，哨兵改为
  `0xFFFFFFFF`。137 处**等长原地改写**（548 字节），**不需要代码洞**，对象大小不变（新字段放进对象与节点里本就空着的字节）。

## 对象由谁创建

- 只有 hknpWorld 构造函数里一处（`0xDA2840` 一带）创建：世界配置 `[cinfo+0x85]` 非 0 时
  `0xDA2863 mov ecx,0x90` / `call 0x97A6B0`（Havok 堆）/ `0xDA2878 call 0xE76640`（派生类构造，0x90 字节）；
  否则 `0xDA287F mov ecx,0x78` / `0xDA2894 call 0xE75980`（基类构造，0x78 字节）。结果存进 **`hknpWorld+0x980`**
  （`0xDA2899 lea r13,[rbx+0x980]`）。EDF 走的是派生类（vtable 0x18C1678，用户实测读到）。
- `0xE76640` 先调基类构造 `0xE75980`，再写派生 vtable、`+0x78 byte=0`、`+0x80` hkArray 清零。
- 全 .text 扫 `E8/E9 rel32` 与 RIP 相对引用：`0xE75980` 只被 `0xDA2894`、`0xE76649` 调用；`0xE76640` 只被 `0xDA2878` 调用。
  `hknpSetEventDispatcherCommand` 字符串存在，但游戏内没有第三处构造，所有实例都出自这两个构造函数。

### vtable

| 槽 | 基类 0x18C1628 | 派生 0x18C1678 | 作用 |
|---|---|---|---|
| 0 | 0x978A40 | 0x978A40 | hkBaseObject 公共槽 |
| 1 | 0xE75AC0 | 0xE767A0 | 析构（deleting）；派生的先释放 +0x80 再调 0xE76680（基类析构体） |
| 2 | 0x978D50 | 0x978D50 | 公共槽 |
| 3 | 0xE75D30 | 0xE76850 | hkSecondaryCommandDispatcher::exec：按事件派发（派生版把可合并事件暂存进 +0x80） |
| 4 | 0xE762A0 | 0xE762A0 | 按命令类型 0..0x4D 分派到 world 的处理函数（不碰索引） |
| 5 | 0xE76000（`ret 0`） | 0xE76B70 | 派生：把 +0x80 里合并后的事件逐个派发 |

## 对象布局（基类 0x78 字节，派生 0x90 字节）

| 偏移 | 类型 | 含义 | 证据 |
|---|---|---|---|
| +0x00 | vtable | | |
| +0x08 | qword | hkReferencedObject（构造 `0x12FE6E0` 写 0xFFFFFFFF） | |
| +0x10 | qword | hkSecondaryCommandDispatcher 字段（构造写 1） | 0x12FE6E5 |
| +0x18 | hknpWorld* | m_world；`[world+0x40]` 是 body 数组大小（`0xE761AC`），`[world+0x20]` 的 vfunc+0x60 给初始 body 容量（`0xE75A29`） | 构造 0xE759E5 |
| +0x20/+0x28/+0x30 | qword×3 | 派发回调上下文；世界在 step 前后写 +0x20/+0x30（0xDAC919/0xDAD65C 写、0xDAC940/0xDAD688 清零），0xE781D0 写 +0x28 | 派发时整段拷到栈上（0xE75D48..0xE75D73） |
| +0x38 | uint16 | **空闲节点表头**（0xFFFF=空） | 0xE75C2F/0xE75C51/0xE7605C/0xE7606D/0xE75F0D/0xE75F1A/0xE760FE/0xE76110 |
| +0x3A..+0x3F | — | **对齐空洞，6 字节，全程无人读写** | 本函数族及 world 侧无任何 `[x+0x3a..0x3f]` 访问 |
| +0x40 | hkArray\<Entry\> | 节点数组：ptr +0x40、size +0x48、capAndFlags +0x4C；初始容量 0x100，扩容走 `0xE75840`（16 字节元素专用 reserve） | 0xE75A00/0xE75C57 |
| +0x50 | hkArray\<uint16\> | **每 body 链表头**（下标 = body id & 0xFFFFFF，值 = 节点索引）：ptr +0x50、size +0x58、cap +0x5C；按 world body 数扩，用 `0xA55860`（2 字节元素 reserve），新槽填 0xFFFF | 0xE75A2C..0xE75A9B、0xE761AC..0xE7622A |
| +0x60 | uint16 | **全局链表头**（body id == 0xFFFFFF 的订阅） | 0xE75D02/0xE75D12/0xE76126 |
| +0x62 | bool | 正在派发；为真时 onBodyRemoved 不立即释放而入待释放列表 | 0xE75D7F/0xE75EC4/0xE764A0 |
| +0x63..+0x67 | — | **对齐空洞，5 字节，全程无人读写** | 同上 |
| +0x68 | hkArray\<uint16\> | **待释放链表头列表**：ptr +0x68、size +0x70、cap +0x74；派发结束时逐个释放后 size=0 | 0xE764BB..0xE7650A、0xE75EC8..0xE75F3A |
| +0x78 | bool（派生） | 合并模式开关 | 0xE76655、0xE76BE1 |
| +0x80 | hkArray\<0x24 字节合并事件\>（派生） | ptr +0x80、size +0x88、cap +0x8C；reserve `0xE76520` | 0xE76A65..0xE76B39 |

### 节点（Entry，16 字节）

| 偏移 | 类型 | 含义 |
|---|---|---|
| +0 | uint16 | next（链表 / 空闲表共用，0xFFFF 结束） |
| +2 | uint16 | key = 事件类型（与事件头 `[event+4]` 的 uint16 比较；0xE76163/0xE76273 写入） |
| +4..+7 | — | **未使用**：没有任何读写；而且 `0xE75840` 扩容搬移时只拷 +0、+2 两个 word 和 +8 的 qword（`0xE758EF..0xE7590B`），+4..+7 根本不随搬移保留 |
| +8 | hkSignal（带标记位的指针） | 订阅槽链；0xE75F50 触发，0x97B7C0 释放，0x97B440 析构 |

## 函数族与全部 uint16 索引访问点

| 函数 | 作用 | 调用者 |
|---|---|---|
| 0xE75840 | hkArray\<Entry\>::reserve（16 字节元素，搬移时逐项拷 +0/+2/+8） | 0xE75A14、0xE75C92 |
| 0xE75980 | 基类构造：+0x38=0xFFFF、+0x60=0xFFFF，节点数组预留 0x100，+0x50 按 body 数填 0xFFFF | 0xDA2894、0xE76649 |
| 0xE75AC0 / 0xE76680 | 基类析构体：释放 +0x68（元素 2 字节）、+0x50（元素 2 字节）、+0x40（元素 16 字节） | vtable / 0xE76802 |
| 0xE75C20 | allocEntry(bodyId)：先取空闲表头，空则 push 新节点（**索引 = (uint16)size**）；挂到 +0x50[body] 或 +0x60 链头 | 0xE7615E、0xE7626E |
| 0xE75D30 / 0xE76850 / 0xE76B70 | 派发：先走 +0x60 全局链，再按事件里的 1 或 2 个 body id 走 +0x50 链，比较 key 后触发；结束时把 +0x68 列表里的链逐条释放 | vtable 槽 3/5 |
| 0xE76010 | freeChain(head)：沿 next 释放每项 +8 的 signal，尾接到空闲表头，表头=head | 0xE764B6、0xE76A3B、0xE76DAB（基类派发里是内联同构循环 0xE75EE0） |
| 0xE76080 | 从某 body 链里摘一项还给空闲表 | **无调用者**（全 .text E8/E9、RIP 相对、.rdata 64 位指针、32 位 RVA 扫描只在 .pdata 出现），仍一并加宽 |
| 0xE76120 | getGlobalSignal(eventType)：+0x60 链里按 key 找，找不到就 allocEntry(0xFFFFFF) | 0xDAB93A、0xDAB998（hknpWorld 包装，`[world+0x980]`）、0xDAD159、0xDAD18E |
| 0xE76180 | getBodySignal(eventType, bodyId)：必要时把 +0x50 扩到 body 数并填 0xFFFF，再按 key 找/建 | 0xDAB934、0xDAB989 |
| 0xE76460 | onBodyRemoved(bodyId)：把 +0x50[body] 置 0xFFFF，正在派发则把旧头压进 +0x68，否则 jmp 0xE76010 | 0xDA8E1F（hknpWorld 删 body 循环 0xDA8B00） |

穷举方法（可复现，脚本思路写进将来的 `tests/havok_props_audit.py`）：

1. 全 .text 原始字节扫 `E8/E9 rel32`、`0F 8x rel32` 条件跳转（唯一命中 0xDAB934 jne 0xE76180）、`48/4C xx 05 disp32` RIP 相对、各节 64 位指针、32 位 RVA，目标集合为上表全部函数与两个 vtable：
   外部引用只有上表「调用者」列那些，全部是 call/jmp，没有内联访问对象字段的外部代码。
2. 对象指针的来源只有 `hknpWorld+0x980`：全 .text 搜 `+0x980]`，hknp 世界相关的只有构造/析构（0xDA2210、0xDA28C5、0xDA3319、0xDA3B11、0xDAF471）、
   getSignal 包装（0xDAB920、0xDAB960、0xDAD14D、0xDAD184）、上下文字段写入（0xDAC90E..0xDAC944、0xDAD652..0xDAD690，只写 +0x20/+0x28/+0x30）、
   vtable 调用（0xDACB99 槽 5、0xE68A9A 槽 3）和删 body（0xDA8E06，`[bm+0x968]`=world+0x980）。没有任何外部代码直接读写 +0x38..+0x74。
3. 对 .pdata 函数加函数间空隙（覆盖无 .pdata 的叶函数）全量反汇编，按「16 位访问 `[r+0x38]`/`[r+0x60]` + `0xFFFF`」「`word [r+r*8+2]` 节点 key 访问」
   「`[r+0x50]` 后接 `word [r+r*2]`」等模式过滤：命中且属于本对象的只有 0xE75840..0xE76E10 这一段；其他命中（0xE8C120、0xEA7C14、0xE2E730、0xC02B9B 等）是别的结构
   （结构体拷贝、其他池），字段语义对不上。
4. 在本段内列出所有 16 位内存访问、16 位寄存器比较、`movzx r32, r16`、`0xFFFF` 立即数、`*2` 寻址 / 步长 2、元素大小 2 的 free、对 `0xA55860` 的调用，逐条归类：
   要么是索引（下表全部改写），要么是事件类型 key（`[entry+2]`、`[event+4]`、`sub ax,0xd`/`cmp ax,0xc`，保持 16 位）。
   改写后再跑同一过滤，剩下的只有 key 访问与 `0xE758FA/0xE75900`（搬移循环里对 +2 的 word 拷贝，加宽后冗余但无害）。

## 加宽设计

| 数据 | 原 | 改 |
|---|---|---|
| 节点 next | +0 uint16 | **+4 uint32**（节点原本空着的 4 字节；+0 的旧 word 不再使用） |
| 空闲表头 | 对象 +0x38 uint16 | **对象 +0x3C uint32**（+0x3A..+0x3F 空洞） |
| 全局链表头 | 对象 +0x60 uint16 | **对象 +0x64 uint32**（+0x63..+0x67 空洞；+0x62 的 bool 不动） |
| 每 body 表头数组 +0x50 | hkArray\<uint16\> | hkArray\<uint32\>：reserve 改调 `0x978600`、步长 4、`*4` 寻址、free 元素大小 4 |
| 待释放列表 +0x68 | hkArray\<uint16\> | hkArray\<uint32\>：同上 |
| 哨兵 | 0xFFFF | 0xFFFFFFFF |
| 新节点索引 | `(uint16)size` | `(uint32)size` |
| 节点搬移 0xE75840 | 拷 +0 word、+2 word、+8 qword | +0 改为 qword 拷贝（覆盖 +0..+7，带上新的 next） |

- 索引上限变为 hkArray 容量上限 0x3FFFFFFF（远高于 0xFFFFFF 的目标），哨兵 0xFFFFFFFF 永远不会与真实索引相撞。节点仍是 16 字节，
  对外返回的 `hkSignal*`（= 节点 +8）语义不变；对象仍是 0x78/0x90 字节，构造处的分配大小不用改。
- `0x978600` 与 `0xA55860` 逐字节相同，只有元素大小立即数（4 vs 2）和两处 rel32 位移不同，两处 call 的目标都是 `0x97C750`（溢出报错）与 `0x97C890`（按元素大小 realloc），
  参数约定一致（rcx 分配器、edx 新容量、r8 旧指针、r9d 旧 size、`[rsp+0x20]` capAndFlags 指针），所以 +0x50/+0x68 的扩容只需改 call 目标。
- 每处改写都是同一条指令的等长替换：16 位 `movzx r32, word [m]`（`0F B7`，2 字节操作码）换成 `mov r32, dword [m+4]`（`8B` + disp8），
  `66` 前缀的 16 位 store / cmp 去掉前缀补 `90`；跳转目标都落在指令起点，不受影响；补的 `nop` 不改标志位（`cmp` 与 `je` 之间的 nop 已逐条核对）。
  因此**不需要 AllocateNearCode / 代码洞**。
- 完整性：上节的穷举证明所有读写这 5 类索引的代码都在 0xE75840..0xE76E10 内，且该段内每一处索引读写、每个 0xFFFF 哨兵、每个 2 字节步长 / 元素大小都在下表里；
  漏一处就会出现 16/32 位混用，所以补丁必须**全有或全无**：先校验 137 处原始字节全部吻合，任何一处不符整套不打、写一行 refused 日志。
- 安装时机：补丁改变了字段位置（+0x3C/+0x64、节点 +4），必须在任何 hknpEventDispatcher 存在之前打（插件初始化时，世界在任务加载时才由 0xDA2840 一带创建）。
  若初始化时已有 hknpWorld，旧对象的 +0x3C/+0x64 是未初始化内存，会立刻坏掉——见「待确认」。
- 内存代价：+0x50 数组每 body 多 2 字节（body 数级别），+0x68 列表通常很短，可忽略。

## 补丁点（137 处，548 字节，全部等长原地改写）

| RVA | 原始字节 | 改后字节 | 所在函数 | 说明 |
|---|---|---|---|---|
| 0xE758EF | `0fb741f8668902` | `488b41f8488902` | realloc 0xE75840 | movzx eax,w[rcx-8];mov w[rdx],ax -> mov rax,q[rcx-8];mov q[rdx],rax (copy +0..+7) |
| 0xE759A1 | `beffff0000` | `beffffffff` | ctor 0xE75980 | mov esi,0xFFFF -> 0xFFFFFFFF |
| 0xE759F1 | `66897338` | `89733c90` | ctor 0xE75980 | mov w[rbx+0x38],si -> mov d[rbx+0x3c],esi |
| 0xE75A55 | `e806febdff` | `e8a62bb0ff` | ctor 0xE75980 | +0x50 reserve 0xA55860(u16) -> 0x978600(u32) |
| 0xE75A76 | `488d1448` | `488d1488` | ctor 0xE75980 | fill: lea rdx,[rax+rcx*2] -> *4 |
| 0xE75A85 | `4883c202` | `4883c204` | ctor 0xE75980 | fill: add rdx,2 -> 4 |
| 0xE75A8E | `668930` | `893090` | ctor 0xE75980 | fill: mov w[rax],si -> mov d[rax],esi |
| 0xE75A9E | `66897360` | `89736490` | ctor 0xE75980 | mov w[rbx+0x60],si -> mov d[rbx+0x64],esi |
| 0xE75B02 | `458d4702` | `458d4704` | base dtor 0xE75AC0 | free +0x68 elemSize 2 -> 4 |
| 0xE75B3A | `41b802000000` | `41b804000000` | base dtor 0xE75AC0 | free +0x50 elemSize 2 -> 4 |
| 0xE75C2F | `0fb77138` | `8b713c90` | allocEntry 0xE75C20 | movzx esi,w[rcx+0x38] -> mov esi,d[rcx+0x3c] |
| 0xE75C33 | `b8ffff0000` | `b8ffffffff` | allocEntry 0xE75C20 | mov eax,0xFFFF -> 0xFFFFFFFF |
| 0xE75C3D | `663bf0` | `3bf090` | allocEntry 0xE75C20 | cmp si,ax -> cmp esi,eax |
| 0xE75C4D | `410fb701` | `418b4104` | allocEntry 0xE75C20 | movzx eax,w[r9] -> mov eax,d[r9+4] |
| 0xE75C51 | `66894138` | `89413c90` | allocEntry 0xE75C20 | mov w[rcx+0x38],ax -> mov d[rcx+0x3c],eax |
| 0xE75C5E | `0fb77148` | `8b714890` | allocEntry 0xE75C20 | movzx esi,w[rcx+0x48] -> mov esi,d[rcx+0x48] (index=(uint16)size truncation) |
| 0xE75CE0 | `410fb71408` | `428b144190` | allocEntry 0xE75C20 | movzx edx,w[r8+rcx] -> mov edx,d[rcx+r8*2] |
| 0xE75CE5 | `66418911` | `41895104` | allocEntry 0xE75C20 | mov w[r9],dx -> mov d[r9+4],edx |
| 0xE75CED | `6641893408` | `4289344190` | allocEntry 0xE75C20 | mov w[r8+rcx],si -> mov d[rcx+r8*2],esi |
| 0xE75D02 | `0fb74760` | `8b476490` | allocEntry 0xE75C20 | movzx eax,w[rdi+0x60] -> mov eax,d[rdi+0x64] |
| 0xE75D0B | `66418901` | `41894104` | allocEntry 0xE75C20 | mov w[r9],ax -> mov d[r9+4],eax |
| 0xE75D12 | `66897760` | `89776490` | allocEntry 0xE75C20 | mov w[rdi+0x60],si -> mov d[rdi+0x64],esi |
| 0xE75D53 | `41bfffff0000` | `41bfffffffff` | dispatch base 0xE75D30 | mov r15d,0xFFFF -> 0xFFFFFFFF |
| 0xE75D77 | `0fb74160` | `8b416490` | dispatch base 0xE75D30 | movzx eax,w[rcx+0x60] -> mov eax,d[rcx+0x64] |
| 0xE75D83 | `66413bc7` | `413bc790` | dispatch base 0xE75D30 | cmp ax,r15w -> cmp eax,r15d |
| 0xE75D90 | `0fb7d8` | `8bd890` | dispatch base 0xE75D30 | movzx ebx,ax -> mov ebx,eax |
| 0xE75DBA | `0fb704d9` | `8b44d904` | dispatch base 0xE75D30 | movzx eax,w[rcx+rbx*8] -> mov eax,d[rcx+rbx*8+4] |
| 0xE75DBE | `66413bc7` | `413bc790` | dispatch base 0xE75D30 | cmp ax,r15w |
| 0xE75E13 | `0fb71448` | `8b148890` | dispatch base 0xE75D30 | movzx edx,w[rax+rcx*2] -> mov edx,d[rax+rcx*4] |
| 0xE75E17 | `66413bd7` | `413bd790` | dispatch base 0xE75D30 | cmp dx,r15w |
| 0xE75E21 | `0fb7da` | `8bda90` | dispatch base 0xE75D30 | movzx ebx,dx |
| 0xE75E4B | `0fb714d9` | `8b54d904` | dispatch base 0xE75D30 | movzx edx,w[rcx+rbx*8] -> d[+4] |
| 0xE75E4F | `66413bd7` | `413bd790` | dispatch base 0xE75D30 | cmp dx,r15w |
| 0xE75E80 | `0fb71448` | `8b148890` | dispatch base 0xE75D30 | movzx edx,w[rax+rcx*2] -> *4 |
| 0xE75E84 | `66413bd7` | `413bd790` | dispatch base 0xE75D30 | cmp dx,r15w |
| 0xE75E90 | `0fb7da` | `8bda90` | dispatch base 0xE75D30 | movzx ebx,dx |
| 0xE75EBA | `0fb714d9` | `8b54d904` | dispatch base 0xE75D30 | movzx edx,w[rcx+rbx*8] -> d[+4] |
| 0xE75EBE | `66413bd7` | `413bd790` | dispatch base 0xE75D30 | cmp dx,r15w |
| 0xE75EE4 | `0fb70c06` | `8b0c0690` | dispatch base 0xE75D30 | pending: movzx ecx,w[rsi+rax] -> mov ecx,d[rsi+rax] |
| 0xE75EE8 | `440fb7f1` | `448bf190` | dispatch base 0xE75D30 | movzx r14d,cx -> mov r14d,ecx |
| 0xE75EF0 | `0fb7d9` | `8bd990` | dispatch base 0xE75D30 | movzx ebx,cx -> mov ebx,ecx |
| 0xE75F04 | `0fb70b` | `8b4b04` | dispatch base 0xE75D30 | movzx ecx,w[rbx] -> mov ecx,d[rbx+4] |
| 0xE75F07 | `66413bcf` | `413bcf90` | dispatch base 0xE75D30 | cmp cx,r15w |
| 0xE75F0D | `0fb74738` | `8b473c90` | dispatch base 0xE75D30 | movzx eax,w[rdi+0x38] -> d[rdi+0x3c] |
| 0xE75F13 | `668903` | `894304` | dispatch base 0xE75D30 | mov w[rbx],ax -> mov d[rbx+4],eax |
| 0xE75F16 | `4883c602` | `4883c604` | dispatch base 0xE75D30 | add rsi,2 -> 4 |
| 0xE75F1A | `6644897738` | `4489773c90` | dispatch base 0xE75D30 | mov w[rdi+0x38],r14w -> mov d[rdi+0x3c],r14d |
| 0xE76024 | `0fb7f2` | `8bf290` | freeChain 0xE76010 | movzx esi,dx -> mov esi,edx |
| 0xE7602A | `0fb7c2` | `8bc290` | freeChain 0xE76010 | movzx eax,dx -> mov eax,edx |
| 0xE7602D | `bdffff0000` | `bdffffffff` | freeChain 0xE76010 | mov ebp,0xFFFF |
| 0xE76040 | `0fb7d8` | `8bd890` | freeChain 0xE76010 | movzx ebx,ax |
| 0xE76054 | `0fb703` | `8b4304` | freeChain 0xE76010 | movzx eax,w[rbx] -> d[rbx+4] |
| 0xE76057 | `663bc5` | `3bc590` | freeChain 0xE76010 | cmp ax,bp -> cmp eax,ebp |
| 0xE7605C | `0fb74738` | `8b473c90` | freeChain 0xE76010 | movzx eax,w[rdi+0x38] -> d[rdi+0x3c] |
| 0xE76065 | `668903` | `894304` | freeChain 0xE76010 | mov w[rbx],ax -> d[rbx+4] |
| 0xE7606D | `66897738` | `89773c90` | freeChain 0xE76010 | mov w[rdi+0x38],si -> d[rdi+0x3c],esi |
| 0xE760A1 | `beffff0000` | `beffffffff` | unlink 0xE76080 (no caller, widened anyway) | mov esi,0xFFFF |
| 0xE760B3 | `440fb7de` | `448bde90` | unlink 0xE76080 (no caller, widened anyway) | movzx r11d,si -> mov r11d,esi |
| 0xE760B7 | `4a8d1448` | `4a8d1488` | unlink 0xE76080 (no caller, widened anyway) | lea rdx,[rax+r9*2] -> *4 |
| 0xE760BB | `420fb70448` | `428b048890` | unlink 0xE76080 (no caller, widened anyway) | movzx eax,w[rax+r9*2] -> mov eax,d[rax+r9*4] |
| 0xE760C0 | `663bc6` | `3bc690` | unlink 0xE76080 (no caller, widened anyway) | cmp ax,si |
| 0xE760C5 | `0fb7c8` | `8bc890` | unlink 0xE76080 (no caller, widened anyway) | movzx ecx,ax |
| 0xE760CD | `0fb7c8` | `8bc890` | unlink 0xE76080 (no caller, widened anyway) | movzx ecx,ax |
| 0xE760D0 | `440fb7d8` | `448bd890` | unlink 0xE76080 (no caller, widened anyway) | movzx r11d,ax |
| 0xE760D7 | `0fb704cb` | `8b44cb04` | unlink 0xE76080 (no caller, widened anyway) | movzx eax,w[rbx+rcx*8] -> d[+4] |
| 0xE760DB | `663bc6` | `3bc690` | unlink 0xE76080 (no caller, widened anyway) | cmp ax,si |
| 0xE760E0 | `66443bde` | `443bde90` | unlink 0xE76080 (no caller, widened anyway) | cmp r11w,si -> cmp r11d,esi |
| 0xE760E6 | `410fb700` | `418b4004` | unlink 0xE76080 (no caller, widened anyway) | movzx eax,w[r8] -> d[r8+4] |
| 0xE760EA | `410fb7cb` | `418bcb90` | unlink 0xE76080 (no caller, widened anyway) | movzx ecx,r11w -> mov ecx,r11d |
| 0xE760F1 | `668904cb` | `8944cb04` | unlink 0xE76080 (no caller, widened anyway) | mov w[rbx+rcx*8],ax -> d[+4] |
| 0xE760F7 | `410fb700` | `418b4004` | unlink 0xE76080 (no caller, widened anyway) | movzx eax,w[r8] -> d[r8+4] |
| 0xE760FB | `668902` | `890290` | unlink 0xE76080 (no caller, widened anyway) | mov w[rdx],ax -> d[rdx] |
| 0xE760FE | `0fb74738` | `8b473c90` | unlink 0xE76080 (no caller, widened anyway) | movzx eax,w[rdi+0x38] -> d[rdi+0x3c] |
| 0xE7610C | `66418900` | `41894004` | unlink 0xE76080 (no caller, widened anyway) | mov w[r8],ax -> d[r8+4] |
| 0xE76110 | `6644895738` | `4489573c90` | unlink 0xE76080 (no caller, widened anyway) | mov w[rdi+0x38],r10w -> d[rdi+0x3c],r10d |
| 0xE76126 | `0fb74160` | `8b416490` | getGlobalSignal 0xE76120 | movzx eax,w[rcx+0x60] -> d[rcx+0x64] |
| 0xE7612A | `41b8ffff0000` | `41b8ffffffff` | getGlobalSignal 0xE76120 | mov r8d,0xFFFF |
| 0xE76132 | `66413bc0` | `413bc090` | getGlobalSignal 0xE76120 | cmp ax,r8w |
| 0xE76140 | `0fb7c0` | `8bc090` | getGlobalSignal 0xE76120 | movzx eax,ax -> mov eax,eax |
| 0xE76150 | `0fb700` | `8b4004` | getGlobalSignal 0xE76120 | movzx eax,w[rax] -> d[rax+4] |
| 0xE76153 | `66413bc0` | `413bc090` | getGlobalSignal 0xE76120 | cmp ax,r8w |
| 0xE761A6 | `41beffff0000` | `41beffffffff` | getBodySignal 0xE76180 | mov r14d,0xFFFF |
| 0xE761DA | `e881f6bdff` | `e82124b0ff` | getBodySignal 0xE76180 | +0x50 reserve 0xA55860 -> 0x978600 |
| 0xE761FB | `488d1448` | `488d1488` | getBodySignal 0xE76180 | fill: lea rdx,[rax+rcx*2] -> *4 |
| 0xE76213 | `4883c202` | `4883c204` | getBodySignal 0xE76180 | fill: add rdx,2 -> 4 |
| 0xE7621C | `66448930` | `44893090` | getBodySignal 0xE76180 | fill: mov w[rax],r14w -> d[rax],r14d |
| 0xE76239 | `0fb71448` | `8b148890` | getBodySignal 0xE76180 | movzx edx,w[rax+rcx*2] -> d[rax+rcx*4] |
| 0xE7623D | `66413bd6` | `413bd690` | getBodySignal 0xE76180 | cmp dx,r14w |
| 0xE76250 | `0fb7c2` | `8bc290` | getBodySignal 0xE76180 | movzx eax,dx |
| 0xE76260 | `0fb710` | `8b5004` | getBodySignal 0xE76180 | movzx edx,w[rax] -> d[rax+4] |
| 0xE76263 | `66413bd6` | `413bd690` | getBodySignal 0xE76180 | cmp dx,r14w |
| 0xE7648C | `b9ffff0000` | `b9ffffffff` | onBodyRemoved 0xE76460 | mov ecx,0xFFFF |
| 0xE76491 | `420fb73c40` | `428b3c8090` | onBodyRemoved 0xE76460 | movzx edi,w[rax+r8*2] -> d[rax+r8*4] |
| 0xE76496 | `663bf9` | `3bf990` | onBodyRemoved 0xE76460 | cmp di,cx |
| 0xE7649B | `6642890c40` | `42890c8090` | onBodyRemoved 0xE76460 | mov w[rax+r8*2],cx -> d[rax+r8*4],ecx |
| 0xE764A6 | `0fb7d7` | `8bd790` | onBodyRemoved 0xE76460 | movzx edx,di -> mov edx,edi |
| 0xE764EB | `e870f3bdff` | `e81021b0ff` | onBodyRemoved 0xE76460 | +0x68 reserve 0xA55860 -> 0x978600 |
| 0xE764FE | `488d1448` | `488d1488` | onBodyRemoved 0xE76460 | lea rdx,[rax+rcx*2] -> *4 |
| 0xE76507 | `66893a` | `893a90` | onBodyRemoved 0xE76460 | mov w[rdx],di -> d[rdx],edi |
| 0xE766BF | `458d4602` | `458d4604` | derived dtor body 0xE76680 | free +0x68 elemSize 2 -> 4 |
| 0xE766F7 | `41b802000000` | `41b804000000` | derived dtor body 0xE76680 | free +0x50 elemSize 2 -> 4 |
| 0xE76896 | `0fb74160` | `8b416490` | dispatch derived 0xE76850 | movzx eax,w[rcx+0x60] -> d[rcx+0x64] |
| 0xE7689E | `41bfffff0000` | `41bfffffffff` | dispatch derived 0xE76850 | mov r15d,0xFFFF |
| 0xE768AC | `66413bc7` | `413bc790` | dispatch derived 0xE76850 | cmp ax,r15w |
| 0xE768C0 | `0fb7f0` | `8bf090` | dispatch derived 0xE76850 | movzx esi,ax |
| 0xE768EA | `0fb704f1` | `8b44f104` | dispatch derived 0xE76850 | movzx eax,w[rcx+rsi*8] -> d[+4] |
| 0xE768EE | `66413bc7` | `413bc790` | dispatch derived 0xE76850 | cmp ax,r15w |
| 0xE76953 | `0fb71448` | `8b148890` | dispatch derived 0xE76850 | movzx edx,w[rax+rcx*2] -> *4 |
| 0xE76957 | `66413bd7` | `413bd790` | dispatch derived 0xE76850 | cmp dx,r15w |
| 0xE76961 | `0fb7f2` | `8bf290` | dispatch derived 0xE76850 | movzx esi,dx |
| 0xE7698B | `0fb714f1` | `8b54f104` | dispatch derived 0xE76850 | movzx edx,w[rcx+rsi*8] -> d[+4] |
| 0xE7698F | `66413bd7` | `413bd790` | dispatch derived 0xE76850 | cmp dx,r15w |
| 0xE769D0 | `0fb71448` | `8b148890` | dispatch derived 0xE76850 | movzx edx,w[rax+rcx*2] -> *4 |
| 0xE769D4 | `66413bd7` | `413bd790` | dispatch derived 0xE76850 | cmp dx,r15w |
| 0xE769E0 | `0fb7f2` | `8bf290` | dispatch derived 0xE76850 | movzx esi,dx |
| 0xE76A0A | `0fb714f1` | `8b54f104` | dispatch derived 0xE76850 | movzx edx,w[rcx+rsi*8] -> d[+4] |
| 0xE76A0E | `66413bd7` | `413bd790` | dispatch derived 0xE76850 | cmp dx,r15w |
| 0xE76A37 | `0fb71406` | `8b140690` | dispatch derived 0xE76850 | pending: movzx edx,w[rsi+rax] -> d |
| 0xE76A42 | `488d7602` | `488d7604` | dispatch derived 0xE76850 | lea rsi,[rsi+2] -> +4 |
| 0xE76BA6 | `41bcffff0000` | `41bcffffffff` | dispatch merged 0xE76B70 | mov r12d,0xFFFF |
| 0xE76C1F | `0fb74360` | `8b436490` | dispatch merged 0xE76B70 | movzx eax,w[rbx+0x60] -> d[rbx+0x64] |
| 0xE76C2C | `66413bc4` | `413bc490` | dispatch merged 0xE76B70 | cmp ax,r12w |
| 0xE76C40 | `0fb7f8` | `8bf890` | dispatch merged 0xE76B70 | movzx edi,ax |
| 0xE76C6A | `0fb704f9` | `8b44f904` | dispatch merged 0xE76B70 | movzx eax,w[rcx+rdi*8] -> d[+4] |
| 0xE76C6E | `66413bc4` | `413bc490` | dispatch merged 0xE76B70 | cmp ax,r12w |
| 0xE76CC3 | `0fb71448` | `8b148890` | dispatch merged 0xE76B70 | movzx edx,w[rax+rcx*2] -> *4 |
| 0xE76CC7 | `66413bd4` | `413bd490` | dispatch merged 0xE76B70 | cmp dx,r12w |
| 0xE76CD1 | `0fb7fa` | `8bfa90` | dispatch merged 0xE76B70 | movzx edi,dx |
| 0xE76CFB | `0fb714f8` | `8b54f804` | dispatch merged 0xE76B70 | movzx edx,w[rax+rdi*8] -> d[+4] |
| 0xE76CFF | `66413bd4` | `413bd490` | dispatch merged 0xE76B70 | cmp dx,r12w |
| 0xE76D39 | `0fb71448` | `8b148890` | dispatch merged 0xE76B70 | movzx edx,w[rax+rcx*2] -> *4 |
| 0xE76D3D | `66413bd4` | `413bd490` | dispatch merged 0xE76B70 | cmp dx,r12w |
| 0xE76D50 | `0fb7fa` | `8bfa90` | dispatch merged 0xE76B70 | movzx edi,dx |
| 0xE76D7A | `0fb714f9` | `8b54f904` | dispatch merged 0xE76B70 | movzx edx,w[rcx+rdi*8] -> d[+4] |
| 0xE76D7E | `66413bd4` | `413bd490` | dispatch merged 0xE76B70 | cmp dx,r12w |
| 0xE76DA7 | `0fb71407` | `8b140790` | dispatch merged 0xE76B70 | pending: movzx edx,w[rdi+rax] -> d |
| 0xE76DB2 | `488d7f02` | `488d7f04` | dispatch merged 0xE76B70 | lea rdi,[rdi+2] -> +4 |

## 实现建议（参照 docs/aiworld-re.md 那套）

- `src/havok_props_logic.h`：`kSites[]`，每项 `{rva, size, stock[], patched[]}`，按上表；`static_assert` 总数 137。
  另加两条只校验不改写的「锚点」：`0xA55860` 与 `0x978600` 的字节等价（除元素大小与 rel32），保证换调用目标的前提成立。
- `src/havok_props.cpp`：插件初始化时校验全部 stock 字节 → 全部吻合才 `VirtualProtect` 写入；否则一行 `REFUSED havok-props` 日志、整套不打。
- `tests/havok_props_audit.py`（`EDF6_NATIVE_DLL`）：
  1. 每个 kSite 的 stock 字节与 DLL 一致；
  2. 把补丁打到内存镜像后，重新反汇编 0xE75840..0xE76E10 的函数，断言不再有 `movzx r, r16`、16 位寄存器比较、`0xFFFF` 立即数、`word ptr` 访问，
     白名单仅 key 访问（`[entry+2]`、`[event+4]`、`cmp ax,0xc`/`sub ax,0xd`/`cmp ax,0xb`）与 0xE758FA/0xE75900；
  3. 全 .text 引用扫描：本函数族的外部引用集合与文档一致（新增引用就失败）；
  4. 全 .text 模式扫描（16 位 `[r+0x38]`/`[r+0x60]` + 0xFFFF、`word [r+r*8+2]`）在本段外的命中集合与文档一致。
- 离线单测：在 C++ 里用 16 字节节点 + 对象缓冲区模拟 allocEntry/freeChain 的加宽语义不是必须的；关键是 audit 的第 2、3、4 步。

## 待确认

- 插件初始化时是否已存在 hknpWorld（标题画面背景）。EDFModLoader 在进程早期加载插件，预期没有；实现时在安装处断言 / 记日志，
  并由主会话实机确认。
- 实测 size=90514 是峰值同时存活的订阅数（节点释放会回到空闲表，size 不回落）。4100 个单位各自若干 body × 若干事件类型可以解释这个量级，
  但没有逐类统计；若后续发现节点泄漏（body 删除后节点未回收），那是另一个问题，加宽后不会再卡死但内存会涨。
- 0xE76080 没有任何调用者，加宽它只是为了一致；若 audit 发现它有了调用者，说明 DLL 不是本版本。
