# 子弹 broadphase 的组展开（addBody 0x109DA0）

代码：`src/group_expand_logic.h`（常量、memo）、`src/group_expand.cpp`（安装、cave、展开）。
ini 键 `GroupExpandMemo`（默认 1，0 = 什么都不装）。离线对照测试：`tests/group_expand_test.cpp`（ctest `group_expand`，
需要 `EDF6_NATIVE_DLL` 指向 Steam 版 EDF.dll，否则跳过，返回 77）。
以下地址都是 EDF.dll RVA（TimeDateStamp 0x678CCB46），结论以汇编为准。

## 1. 症状

perfrun bench 里子弹步的 pre_update 随敌人数超线性增长：500 / 1000 / 2000 / 4000 只敌人时分别是 1.8 / 3.4 / 18.0 / 79.5 ms，
4000 只时占一帧（295 ms）的 80%。热栈：`e4d340 -> 11a73b8 -> 234b9d -> 233ca2 -> 237ffc`。
0x11A73B8 在 xgs `World::CastShape`（0x11A70B0）里面，它回调的收集器最后进到 addBody 0x109DA0。

## 2. 原版代码路径

### 子弹步的三个 cast

子弹步按移动方式有三处调用 0x11A70B0，三处都是 `rcx = core+0x800`（收集器），一共 5 个参数（第 5 个在 `[rbp+0x5C0]`），
返回值在 rax：

| 调用点 | 说明 |
|---|---|
| 0x234334 | 移动方式 1 |
| 0x234982 | 移动方式 2 |
| 0x234B98 | 移动方式 3（热栈上的 0x234B9D 就是它的返回地址） |

### 收集器 addBody 0x109DA0

收集器字段：`+0x18` id 数组，`+0x20` 个数（int），`+0x24` 容量（低 30 位），`+0x28` = 0，`+0x29` = 1（展开组），
`+0x30` 要忽略的对象，`+0x38` 队伍，`+0x80` 过滤 functor（可能为空）。

broadphase 每重叠一个 body 就调一次 addBody(collector, id)。body 的属性 key 1 表示它属于一个组（对象的一串碰撞体）。
这时 addBody 拿到组 X，遍历 std::list：`head = *(X+0xA0)`，对每个 `node+0xA0 != 0`（激活）的节点，
functor 非空时先用原来那个 id 的地址调它的 slot 2，通过了再追加；functor 为空就直接追加 `*(node+0x10)->+0xF0`
（节点 body 的 id），追加函数是 0x11A9EC0。

追加 0x11A9EC0：`(int)(cap & 0x3FFFFFFF) >= count+1` 时写 `buf[count] = id; count++`（`buf+count*4 == 0` 时不写只加数）；
否则走 0x107320 扩容，扩到 `max(2·cap, needed)`。

**没有去重**：同一个组的 h 块 body 被重叠，组的整串链表（M 个节点）就被走 h 遍、追加 h×M 个 id。
密集敌群里一颗子弹一步扫过一只敌人的很多块碰撞体，所以成本是 O(子弹 × 重叠 body × 组大小)，敌人越密、越多越陡。

### 下游

- 过滤 0x2359C0 / 0x1079D0 把列表从 0x818 拷到 0x8C8，再拷到 0x8D8。
- 第 3 步 0x231720 -> 0x11AA620 给每个候选建 body 指针表（重复的也建），然后 0x11A8E40 做 shape cast（全命中，上限 0x32）。
- 0x230CA0 的命中记录不去重；伤害由 0x543920 的已命中集合去重。

所以列表里的重复 id 下游也要付一份代价（bench 采样：过滤约 160 个样本、第 3 步约 161 个）。但「把重复去掉」会改变命中上限
0x32 被占满的方式和重复特效，等价性没有证明，**这次不做，留作后续**（见第 6 节）。

## 3. 改法（等价）

1. 0x109F6F 起的 15 个原版字节（遍历循环的入口）换成 `E9 rel32` 加 NOP，跳到 cave。这里 rbp = 收集器、rdi = 组、
   esi = id，rsp 16 字节对齐。cave：`mov rcx,rbp; mov rdx,rdi; mov r8d,esi; call [rip+0x11]; mov rax,image+0x109FED; jmp rax`，
   0x109FED 是原版循环结束后的位置。cave +32 放目标地址，+40 是 unwind info，+64 是用 RtlAddFunctionTable 登记的 RUNTIME_FUNCTION。
2. 三处 cast 调用点改成调 `BulletCast`：进入时 depth++、memo 换代，退出（包括异常）时 depth--、memo 再换代，再调原版 0x11A70B0。
3. `Expand(collector, group, id)`：
   - functor 非空：照原版逐节点调 functor 再追加（functor 可能有状态，一次都不能省）。
   - 不在子弹 cast 里（别的查询）：照原版走一遍，id 先攒进 256 个一批的缓冲，再批量追加。
   - 在 cast 里、没有 functor：每个线程一张 memo（512 槽、最多 384 个组、65536 个 id），组第一次被展开时走链表并记下 id 序列；
     这次 cast 里再碰到同一组，直接把记下的序列批量追加，不再走链表。memo 满了就退回逐次走。
   - 批量追加：列表有空间（`buf` 非空且 `cap >= count+n`，即原版逐个追加时一次都不会扩容）时 memcpy 再改 count；
     否则逐个调原版 0x11A9EC0，扩容序列和原版一模一样。

为什么 cast 内可以复用：从组第一次展开到这次 cast 结束，本线程上只有 broadphase 在一个接一个地回调收集器，
回调里只读组、只写收集器的列表；组的链表和节点的激活标志在这段时间内不会被改。debug 打开时每 64 次复用抽 1 次
重新走链表核对，不一致就记 `GROUPEXPAND MISMATCH`（最多 32 行），并用走出来的结果。

追加的 id、顺序、扩容时机都和原版逐个追加相同，所以下游（过滤、shape cast、命中、伤害、特效）看到的是同一个列表。

## 4. 安装前的核对

- addBody 0x109DA0..0x10A002 的 FNV-1a 64 = `0x4cfbba1b6be3fbd5`；追加 0x11A9EC0..0x11A9F30 = `0xd95186e44c367004`。
- 0x109F6F 的 15 个字节等于 `kWalkStock`；0x1D2C420 的 unwind info 等于 `kAddBodyUnwindStock`。
- 三处 cast 调用点仍是调 0x11A70B0 的 call（`edf::RedirectCall` 只在调用目标是它时才改）。
- 指纹或 unwind 不符：什么都不改，记一行 `GROUPEXPAND ... off`。挂钩没有全部装上：已装的按原字节撤回，
  记 `GROUPEXPAND hooks not all in (n): off`，原版照跑。

## 5. cave 的 unwind（踩过的坑）

cave 里面会调用 C++（`Expand`），C++ 异常（比如 functor 抛的异常）必须能穿过 cave 和 addBody 的栈帧。
cave 本身不动 rsp，栈布局就是 addBody 在 0x109F6F 时的布局，所以 cave 的 unwind codes 抄 addBody 的 prolog
（`push rdi; sub rsp,..; mov [rsp+..],rbx/rbp/rsi`）。

**坑**：RtlVirtualUnwind 会跳过 CodeOffset 大于当前 pc 偏移的 code，**SizeOfProlog = 0 也一样跳**。
照抄原版的 CodeOffset（0x14、0x10……）时，cave 里 pc 偏移很小，code 全被跳过，异常一穿过 cave 就成了未处理异常
（退出码 0xE06D7363）。所以 cave 的 code 的 CodeOffset 全写 0：
`kCaveUnwind = 01 00 08 00 00 64 0B 00 00 54 0A 00 00 34 09 00 00 52 00 70`。
离线测试分别从原版、直接调 cave、中间隔一个栈帧、经 BulletCast 四种情况抛异常，都要能被外层 catch 到，
并且 depth 回到 0。

## 6. 后续（未做）

- 列表去重：重复 id 让过滤和第 3 步多做 h 倍工作。要先证明去重不改变 0x32 命中上限的占用方式、0x230CA0 的命中记录和特效次数，
  才能做。
- 不在子弹 cast 里的展开（别的查询）仍逐次走链表。

## 7. 联机

只改本机的碰撞收集过程，结果和原版相同，不涉及同步数据；房主、客人各自本机生效，开关不影响对方。

## 8. 测量

PerfProbe=1 时每个 PerfTick 周期写一行 `PERF GROUPEXPAND`（casts、cast 内耗时、walks / copies 及其 id 数、
`expansions a walk` = 原版每走一遍组这次复用了几次、outside casts、memo 满了的次数、shadow 核对数和不一致数），
`tools/perf_report.py read_group_expand` 解析，report 里有「Bullet casts: group expansion」一节。

离线（`tests/group_expand_test.cpp` Timing，5120 次 cast，每次 120 个重叠、组 40 块）：

| 场景 | 原版 | 不在 cast 里（批量追加） | cast 内 memo |
|---|---|---|---|
| 400 只，内存紧凑 | 58–61 ms | 54–58 ms | 34–36 ms（1.7x） |
| 4000 只，内存打散 | 97–104 ms | 113–115 ms | 72–78 ms（1.2–1.4x） |

不在 cast 里、内存打散时比原版慢约 12%（逐个追加的纯 C++ 循环更慢，约 135 ms，所以保留批量）。游戏内这条路径占多少，
看 PERF 行的 outside casts。
