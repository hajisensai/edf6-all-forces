# Havok（hknp）多线程：会跑到 worker 线程上的游戏 / 插件代码

目标：打开 hknp 多线程（往 `hkDefaultTaskQueue` 挂 `hkCpuThreadPool` worker）之前，穷举所有会因此换到 worker 线程上执行的游戏代码与插件钩子，逐个说明写了哪些共享状态、能否并发、最小串行化手段。
所有地址都是 RVA（ImageBase `0x180000000`），针对 Steam 版 `EDF.dll`（TimeDateStamp `0x678CCB46`）。全部来自静态反汇编，**没有启动游戏**。
可信度标注：**已验证** = 反汇编或交叉引用直接看到；**推测** = 由结构推断，还要靠运行时确认。

## 1. 结论先说

1. 世界级回调早就串行了：所有 hknp 事件订阅者、world signal 订阅者，都只在三个单实例的世界级任务里被派发——PostCollide、PreSolve、PostSolve。另外还有不在 step 内、游戏线程上的 `hknpInternalCommandProcessor` 立即派发。
   - 每个任务一次只有一个实例（`[task+0x10]=1`）。
   - 图执行期间，游戏线程停在 `0x9EDDB0` 里，要么自己执行任务，要么等待。
   - 所以打开多线程以后，这些回调**彼此之间仍然不重叠，也不和游戏线程的游戏逻辑重叠**。变化只有两点：执行线程的身份变了；可能和同一张图里**没有排序约束**的其它 Havok 任务并发（见 §7 残余风险）。
   - 结论：**回调之间不需要加锁**。必须保持三个世界级任务是单实例；只需要给插件钩子加一个「不在游戏线程」的 Debug 守卫。
2. 真正新增的并行面有两块：
   - 第一块是 narrowphase / solver 的并行任务，会调用游戏注册的 collision modifier。两个游戏自定义 modifier 只写 per-pair 的参数、对共享状态只读（§4），**安全**。
   - 第二块是 `MTQueryQueue@havok@xgs` 的批量查询。它和物理 step 共用同一个队列，挂上 worker 以后，**物理 step 之外的查询批次也会并行跑在 worker 上**（§5）。其中游戏的 collector 是每个查询条目各自独占的，插件没有钩到这条路径。
3. `hknpEventMergeAndDispatcher` 的 `+0x78` / `+0x80` **不是**多线程延迟机制，帮不上忙（§6）。
4. Havok 内存分配在 worker 上可用：线程上下文按线程懒创建，worker 入口也会主动设置（§8）。
5. reset 路径 `0x118F0A0` 不先 close 就释放 pool 和 queue。挂上 worker 以后必须补上「close → 等 worker 退出 → 释放」的顺序（§9）。
6. 插件（`src/`、`autoturret/`）里**只有两处**代码会在 worker 上执行：
   - `src/physics.cpp` 的 vertical contact cave：只读，外加 `std::atomic<float>` 和 SEH，安全；
   - `src/edf41robo.cpp` 借用游戏的 `RagdollDefaultContactCallback`，写插件自己分配的组件，它被世界级任务串行，安全。

   其余插件钩子都在游戏线程上，或者在同步查询里（§10）。

## 2. 任务图入口与阶段

| 项 | 地址 | 说明 | 可信度 |
|---|---|---|---|
| holder 全局 | `0x20B30A0` | `[G+8]` 是 pool（vtable `0x1AFED80`），`[G+0x20]` 是 queue（vtable `0x182EC50`，子对象 vtable `0x182EC80`，在 `q+0x18`） | 已验证 |
| processTaskQueue | pool vtable slot 4 = `0x130FE20` | worker 主循环；看到 closed 标志就在 `0xA0FBFB` 退出 | 已验证 |
| worker 线程入口初始化 | `0x13102B0` | 先 `0x983010(ecx=[0x213AC78], rdx=线程序号)` 写 TLS 上下文槽，再按需建 monitor stream（`0x97E1D0`） | 已验证 |
| 模拟对象 | `world+0x4C8` | `hknpMultithreadedSimulation`（vtable `0x18C1A28`）：collide 任务由 `0xE7E570` 构建，solve 任务由 `0xE82390` 构建。单线程版 vtable `0x18BCDB8`（`0xE78D10` / `0xE79F50`）本游戏没有用到 | 已验证 |
| 世界级持久任务 | 在 `0xE7D0C0` 创建 | PostCollideTask `sim+0x6A8`（vt `0x18C1D08`，process `0xE819F0`→`0xDAC840`）；PreSolveTask `sim+0x6B8`（vt `0x18C1CA0`，`0xE81C40`→`0xDAD360`）；PostSolveTask `sim+0x6D0`（vt `0x18C21C0`，`0xE81AB0`→`0xDACA60`）。三者都写了 `[task+0x10]=1`（单实例） | 已验证 |
| 图的边 | `graph+0x328` 是 word 对数组，数量在 `+0x330` | PostCollide 句柄存在 `+0x4D6`，前驱是 `+0x4CA`、`+0x4D4`（`0xE7FC5F` / `0xE7FCDB` / `0xE7FD5A`）；PreSolve 存在 `+0x4D8`，前驱是 `+0x4D6`；PostSolve 存在 `+0x4E0`，前驱是 `+0x4DC` | 边已验证；「其余所有并行任务都排在世界级任务之前」**推测** |

各阶段做什么：

| 阶段 | 函数 | 线程 | 做什么 |
|---|---|---|---|
| PreCollide | `0xDACD80`（由 `0xDAAD40` 在 processGraph 之前调用） | **游戏线程**（不进图） | `preCollideSignal world+0x858`，经 `0xDAA010` 触发 |
| collide（narrowphase） | `0xE7E570` 构建的并行任务 | 任意线程，可并行 | 调用 collision modifier / filter（§4） |
| PostCollide | `0xDAC840` | 任意**单个**线程 | `TtDispatchCommands`：`0xE781D0(world+0x990, stream, dispatcher=world+0x980)` 派发事件；然后 `TtPostCollideSignal world+0x860`，经 `0xDA9F60` 触发 |
| PreSolve | `0xDAD360` | 任意单个线程 | `preSolveSignal world+0x868`；在 `0xDAD672` 再执行一次 `0xE781D0` 派发 |
| solve | `0xE82390` 构建的并行任务 | 任意线程，可并行 | 调用 Jacobian / contact modifier（MassChanger，§4） |
| PostSolve | `0xDACA60` | 任意单个线程 | `0xE77830`；dispatcher vt+0x28（slot 5 = `0xE76B70`，合并派发）在 `0xDACB99..0xDACBA3`；`postSolveSignal world+0x870`；`TtCopyContactCaches`；`TtClearSimulationContext` |
| step 之外的立即事件 | `hknpInternalCommandProcessor` slot 3 = `0xE68820` | 游戏线程 | 在 `0xE68AA4` 调 `[world+0x980]->vt+0x18` |

hkSignal 的机制（已验证）：

- slot 对象布局：`+0` vtable（基类 `hkSlot` 的 vtable 是 `0x17683C0`），`+8` 是 next 指针并带 flags，`+0x10` 是 object，`+0x18` 是成员函数。
- 触发循环用 `call [vt+0x18]` 调用 slot；回收 slot 用 `call [vt]`，`edx=1`。
- 事件 signal 的触发函数 `0xE75F50` 只被 dispatcher 的三个函数调用：`0xE75D30`、`0xE76850`、`0xE76B70`。
- 取 per-body signal 用 `0xDAB920(world, eventType, bodyId)`，`bodyId=0xFFFFFF` 表示全局；另有 `0xE76120` / `0xE76180`。

## 3. 会在 worker 上执行的游戏回调（串行于世界级任务）

所有事件订阅者都是 `hkSignal2<EventHandlerInput, Event>` 的 slot。它们由 PostCollide 或 PreSolve 的命令派发、或 PostSolve 的合并派发触发；用 `0xE781D0` 派发的事件取决于事件类型，两个阶段都可能出现。
下表的「共享写」来自 `scanw.py` 的 BFS（直接调用闭包，扫 rip-relative 写、锁、导入函数），只覆盖**直接调用**，虚调用目标单独列出。

| RVA | 名称 | 共享写 | 结论 |
|---|---|---|---|
| `0x1351C0` | MapDestruction GlobalSlot（全局事件类型 `0x1B`，在 app init `0x704FFD` 经 `0x11C580` 订阅） | `0x135B56` 处 `dec [0x20B2BD4]`（经 `0x135A30`）；破坏状态（游戏对象） | 已串行，安全；`dec` 不是原子操作，必须保持单实例 |
| `0x135170` | MapObject_CrashModel | 自身对象；malloc / free | 安全 |
| `0x2C5B60` | ShellCase_RB | 自身对象（播音 / 状态） | 安全 |
| `0x328280` | DeiroiFootParts_Base | 自身对象 | 安全 |
| `0x34A5E0` | EDF6_Berserker_Equipment_Bomb | `0xFF5918` 处 `inc [0x20F4C68]`，`0xFF5D1C` 处 `dec`（计数器） | 已串行，安全 |
| `0x3679A0` | EDF6_GiantDangoLarge | 经 `0x541FF0` 把伤害**入队**；`0x50BC0` 是 EnterCriticalSection 的 scoped lock | 安全 |
| `0x3A7A00`、`0x3A4480` | EDF6_Merman | `0x3A4480` 同上，伤害入队并加锁 | 安全 |
| `0x42D070` | Ball@GiantDango | 自身对象 | 安全 |
| `0x51EE10` | UfoMother511 ContactLisner | 自身对象 | 安全 |
| `0x567100` | HeavyArmor | 自身对象 | 安全 |
| `0x5E85A0`、`0x5E8590` | Transporter_Container | 自身对象 | 安全 |
| `0x5ED6C0`、`0x5ED5B0` | ContactLisnerBase::Lisner | 自身对象，加上虚调用派生类 | 安全（派生类**推测**同类） |
| `0x5F21B0` | RigidBodyContactProcedure | 虚调用 procedure：VehicleSeHelper ContactPlaySe、RigidBodyContactSelfDamage、Begaruta、LandingPlaySe（播音 / 伤害入队） | 已串行，安全；播音线程安全性同今天（**推测**） |
| `0x62EA70` | VehicleBase | 自身载具对象 | 安全 |
| `0x643F50` | BegartaCtrlWalk | 自身对象 | 安全 |
| `0x652B30` | VehicleHelicopterBase | 自身对象；`0x50BC0` 加锁 | 安全 |
| `0x6CE6E0` | FlyingCharacterControl | 自身对象 | 安全 |
| `0x6DAC10` | CharacterControl_Cling@xgs | 虚调用 Callback（InsectBase 等）→ `0x4A8B40` → 在 `0x4A8D2B` 调 `0x541FF0`（伤害入队，`0x50BC0` 加锁） | 安全 |
| `0x6EC450` | RagdollDefaultContactCallback | 组件冷却；播音 `0x7B4510` / `0x7A8CD0` | 已串行，安全 |
| `0x6F1220` | RigidBodyContactDamageControl（`0x6F0EA0` / `0x6F0F70` 经 `0x6F0A20` / `0x6F0AC0` 注册；析构 `0x6F0C30` / `0x6F0D70` 注销） | 伤害入队 `0x541FF0`，`0x50BC0` 加锁 | 安全 |
| `0x7C79F0` | BoxObject | 自身对象 | 安全 |
| `0x11AFE68` | TriggerVolumeBase@xgs | 触发器状态（自身对象） | 安全 |
| `0x11B94F0` | CharacterControl_Walk@xgs | 自带自旋锁（`0x11B95A2`，失败时 Sleep） | 安全 |
| `0x11DFD40` | hknpCharacterRigidBody@custom（事件类型 1） | 角色刚体自身 | 安全 |
| `0x11AEA30` | xgs World::Initialize lambda_7（全局事件 `0xD`）slot 3 | `ret 0`，空操作 | 安全 |

world signal 订阅者：

| RVA | 名称 | 阶段 / 线程 | 结论 |
|---|---|---|---|
| `0x11DFF90` | custom char RB preCollide（`+0x858`，在 `0x11DDD20` 订阅） | 游戏线程 | 不变 |
| `0x11DFD50` | custom char RB postCollide（`+0x860`） | PostCollide 任务（worker 或游戏线程） | 已串行，安全；内部有插件 cave（§10） |
| `0x11DFF70` | custom char RB postSolve（`+0x870`） | PostSolve 任务 | 已串行，安全 |
| `0x1DC240` | MissionContext::CreateSceneObject 的 body add/remove lambda | add / remove body 时 | 只在游戏线程增删 body，不受影响；如果有回调在任务里增删 body，就跟着进那个任务（仍然串行） |
| `0x772560`、`0x7725F0` | UniqueKeyHolder::OnHavokBegin lambda | 同上 | 同上 |
| — | World::Initialize lambda 1..6 | 库 / buffer signal | **未逐个审**，推测在游戏线程 |

伤害链（已验证）：

- `0x541FF0` 遍历 pending-hit 列表（步长 `0xA0`）去重，再用 `0x124190` 追加一条，**只入队**。
- 真正结算在游戏线程 `0x543920`，之后才走 `ApplyDamage 0x547C30`。从 step 的任务入口出发，BFS **没有**到达 `0x547C30`。
- 因此 `src/damagestats.cpp` 的 `0x547C30` 钩子以及它的 `thread_local sourceStack/sourceDepth`（`damagestats.cpp:218-219`）都不会在 worker 上跑。

全部回调的 BFS 闭包里，只有一个函数用临界区：`0x50BC0`（`mov [rcx],rdx; call EnterCriticalSection`，即 scoped lock 构造）。其余能看到的只有 malloc / free（CRT，线程安全）、引用计数原子操作、`__RTDynamicCast` 和 sqrtf。

## 4. 并行的 narrowphase / solver 代码（真正并发）

modifier 在 `0x704F20` 经 `0xDD3E00` 注册，共三个：Havok 原版 FlipBackface、sandlot_custom FlipBackface、MassChangerModifier。

| RVA | 名称 | 共享写 | 结论 |
|---|---|---|---|
| `0x1064A0` / `0x106770` / `0x1064B0` | `sandlot_custom::hknpFlipBackfaceCollisionModifier`（vt `0x17E89E8`）slot 1（返回 6）、3、5 | 只读：全局 shape 函数表 `[0x20F1930]`（shape type×0x100+0xA8，`call r10`）；调用 Havok `0x989580` / `0xD9A1A0`。唯一的写是 per-pair manifold 参数 `or [[r14]+0x2C],0x100` | 可并发，**安全**；`0xD9A1A0` 没有深审（Havok 库函数） |
| `0x5F0860` | MassChangerModifier slot 3 | 只读 `[0x20B2958]`（xgs world）、`0x11AD2E0`（world vt+0x68 isValid、vt+0xE8 取属性 `0x8001`）、`__RTDynamicCast`、`0x11B1240`；写只落在 per-pair jacobian `[arg7+0xC0/+0xD0/+0x95]` | 可并发，**安全**——前提是游戏线程在 step 期间不改那几个属性；step 期间游戏线程停着，成立 |
| `0x1063F0` / `0x1062A0` / `0x106370` | BroadPhaseConfig@sandlot_custom | 纯函数，只写 out 参数 | 安全 |
| — | `hknpGroupCollisionFilterBase<Config<4,4,4,16>>`（vt `0x1761BA8`） | Havok 模板（`0xD93xxx`），只读 | 安全 |
| `0x11B8800` | hknpUFMShapeTagCodec（vt `0x1AE3388`） | **未审** | 推测只读 |

## 5. MTQueryQueue@havok@xgs：step 之外的并行查询

- 查询批次投到同一个队列 `[G+0x20]+0x18`（已验证）。投递方：
  - `0x11BE590`（flush）。由 `0x11BEBE0` 调用，System::update 在 `0x11991AF`、`0x11992E0`、`0x11994E3`、`0x1199603` 调它。
  - `0x11A7AC0`（射线批）和 `0x11A8E40`（形状批），由 `0x237CF0` 调用（`0x238053` / `0x2382DA`），`0x237CF0` 又由 `0x704EB0` 调用。
- 投递方用 `pool->getNumThreads()+1` 作为任务个数，构造 `hknpCollisionQueryTask`（构造 `0x1607ED0`，process `0x16080B0`），再经 `[q]+0xA8` 执行 processGraph，返回后由 `0x11BE100` 在游戏线程做后处理。
  - 当前 worker 数为 0，这些批次全在游戏线程上跑。挂上 worker 以后，它们会被 N+1 个线程**同时**处理，不在物理 step 里。
- 用户查询命令：
  - CastRayToBodiesCommand@xgs：execute `0x11AA7D0` → `0x11A77C0`；
  - CastShapeToBodies：`0x11AA800` → `0x11A8A90`。
  - 静态扫描（`reach_11a77c0` / `reach_11a8a90` / `reach_16080b0`）只看到 Havok 引用计数原子操作，**没有全局写**。
- BulletControl 的批量查询（已验证）：
  - 射线批条目 `+0x10` = `bullet+0x8F0`。这个 collector 的 vtable 在构造 `0x22FF25` 处设为 `0x1768B28`（hknpCollisionQueryCollector）。
  - 形状批条目 `+0x10` = `[bullet+0x990]`，构造时为空，之后另行分配；它的类**未识别**。
  - 这两个 collector 都是每颗子弹各自独占，一个条目只会被一个任务实例处理（**推测**，基于 hknpCollisionQueryTask 按条目分片）。
- 结论：只读世界加上独占 collector，**可并发**。前提是批次执行期间没有别的线程改 hknp 世界；今天游戏线程也是停在 processGraph 里等，所以这一点不变。

## 6. hknpEventMergeAndDispatcher 的 `+0x78` / `+0x80`

已验证：

- 派生类的 exec `0xE76850` 把类型字 `[ev+4]==0` 的事件（trigger 类 enter / exit，计 ±1）暂存进 `+0x80` 数组（每项 0x24 字节，扩容函数 `0xE76520`）。
- PostSolve 里 slot 5（`0xE76B70`）统一派发：计数为正给状态 0，为负给 1；为零的跳过，除非 `+0x78` 置位（此时给状态 2）。
- `+0x78` 只在构造 `0xE76655` 处写成 0。

结论：这只是**同一帧内 begin / end 事件的对冲合并**。合并在 PostSolve 任务里完成，派发线程和其它事件一样，是任意的单个线程。它既不把派发推迟到游戏线程，也不提供跨线程缓冲，**对多线程没有帮助**。不要指望靠置 `+0x78` 把回调挪回主线程。

## 7. 残余风险与实现前补审的结论

1. **图的边是否完备**。实现时把任务图静态导出了一遍（`tests/havok_mt_audit.py` 第 6 项，`graph()` 线性跟踪 addTask `0x9EC400` / `0x9EC4F0` / `0x9EC630` / `0x9EC6E0` 返回的句柄流到 `graph+0x328` 的 word 对）：
   - collide 图（`0xE7E570`）新建 18 个任务，30 条边全部解析出来，外部输入只有 `+0x4C8` / `+0x4CA` / `+0x4CC`。**18 个任务全都是 PostCollide（`sim+0x6A8`，句柄 `+0x4D6`）的祖先**（已验证）。所以 PostCollide 运行时，本步的 narrowphase 任务已经全部结束。
   - solve 图（`0xE82390`）：PreSolve（`+0x4D8`）以 `+0x4D6` 为前驱；求解任务在 PreSolve 之后、汇合到 `+0x4DA`；`+0x4DA` → `+0x4DC` → PostSolve（`+0x4E0`）（已验证）。
   - 没展开的：`0xE823F5` 的 `0x9EC7A0` 插入的子图（`sim+0x190`，`+0x4DA` 取的是它里面的一个节点）和 `0xE82AC7` 的 `0x9ECCE0`。它们按 Havok 的设计汇合在 `+0x4DA`，**推测**不和 PreSolve / PostSolve 并发；这是 Havok 自己发布的多线程模式，世界级任务存在的意义就是串行点。
2. 回调在 worker 上调用非线程安全的游戏子系统（音频、特效、对象创建）。游戏线程在整个图执行期间停在 `0x9EDDB0`，所以回调和游戏线程代码互斥，和 AI 线程、渲染线程、CRI 线程的交叠与今天完全相同，变化只是 thread id。thread id 会造成差别的只有三类，逐一核对过：
   - TLS：见下面第 3 条，没有游戏自己的 thread_local（已验证）。
   - `GetCurrentThreadId`：`0x6D3C0` / `0x6D3F0` 是 debug owner 跟踪，不阻塞；其余是 render / CRI / CRT。
   - **可重入锁的所有者**：如果游戏线程在步进期间持有某个 CRITICAL_SECTION，回调在游戏线程上重入同一把锁今天能过，换到 worker 上就会死锁。System::update `0x1198DA0` 里唯一的临界区是 `0x1199006` 的 scoped lock（`0x50BC0` = EnterCriticalSection），在 `0x119908B` 由 `0x50BE0`（LeaveCriticalSection）释放，到两次步进 `0x1199359` / `0x1199479` 之间没有再加锁，函数里也没有别的锁导入；上一层 `0x705530`（帧函数，`0x705715` 调 System::update）里也没有锁导入、没有调 `0x50BC0`（已验证；System::update 部分由审计第 5 项守着）。再往上的主循环没有查（推测不在锁里跑帧）。`CharacterControl_Walk` 的自旋锁 `0x11B95A2` 本来就不可重入，换线程不改变什么。
   - 音频 `0x7B4510` → `0x7AA8C0` → `0x7ABE30` 是游戏自己的播放请求，只在回调里被调用（PostCollide / PreSolve / PostSolve 任务串行），和游戏线程互斥；CRI 侧本来就有自己的服务线程。音频换线程身份后的行为放进实机待测项（§13）。
3. TLS：游戏范围内用 `gs:[0x58]` 的 57 个函数全部属于两种模式之一——(a) MSVC magic static 的 epoch（TLS+0x20），(b) Havok monitor stream（TLS+8，看 `+0x24` 是否已初始化，`0x97E1D0` 懒创建）。**没有发现**游戏自己的 thread_local 状态（已验证）。
4. 补审完的两项（已验证）：
   - 形状批 collector `[bullet+0x990]`：在 `0x235929` 经 `0x230770` 赋值，是 `std::shared_ptr`（控制块 vtable `0x179E1A0` = `_Ref_count<hknpClosestHitCollector>`，另一支 `0x179E178` = `_Ref_count<hknpAllHitsCollector>`），collector 本身是 Havok 原版类（`hknpClosestHitCollector` vt `0x1768B78` 在 `0x2357F9` 写入），每颗子弹各自一个，只写自身。
   - `hknpUFMShapeTagCodec`（vt `0x1AE3388`）：槽 3 `0xDF9DE0` 是 `ret 0`；槽 4 decode `0x11B8800` 只读 `this+0x20` 的表，写只落在 out 参数 `[r9]` / `[r9+4]` / `[r9+8]`（审计第 7 项）。纯函数，可并发。
5. 仍未逐个审：World::Initialize lambda 1..6（库 / buffer signal，推测在游戏线程），以及 §5 里同一批次内同一颗子弹是否可能出现两次（推测不会，一颗子弹每帧一条）。

## 8. Havok 内存与 TLS 在 worker 上

已验证：

- `0x97A6B0`（Havok 分配入口）取 `TLS[0x213A670]` 块，检查 `+0x24` 是否已初始化（未初始化时调 CRT 的 `0x12D922C` 做 TLS 初始化）。
- 然后取 `+8` 处的线程上下文；为空就调 `0x97E1D0(0,1)` 懒创建并回写。
- 最后 `jmp [[[ctx]+0x58]]+8`，即经线程上下文里的 allocator 分配。
- worker 入口 `0x13102B0` 在进主循环前调 `0x983010`，走的是同样的懒创建路径，并把线程序号写入 `ctx+0x10+idx*8`。

所以 worker 上第一次分配就会建好自己的上下文，**能用**。底层堆（`ctx+0x58` 指向的 allocator）是否线程安全属于 Havok 自身的设计（推测是线程安全的 hkMemoryRouter heap），没有反汇编验证。

## 9. reset `0x118F0A0` 与析构 `0x118EC70` 的退出顺序

已验证：

- 析构 `0x118EC70` 的顺序：
  1. `0x118ECB5` / `0x118ECB8` 先 close：`q+0x18` vt+0x68 = `0xA15050`，置 `+0x348` / `+0x360`；
  2. `0x118ED07` 释放 pool；
  3. `0x118ED19` 释放 queue。
- pool 析构 `0x130F670` 在 `[pool+0x155]!=0` 时会等待 `[pool+0x148]`。
- worker 看到 closed 后从 `0xA0FBFB` 退出。
- reset `0x118F0A0` **没有 close**，直接在 `0x118F114` / `0x118F121` 释放 pool，在 `0x118F12B` / `0x118F138` 释放 queue。worker 数为 0 时不出问题；挂上 worker 以后，worker 可能仍在 queue 上等待，就会出现 use-after-free 或者挂死。

最小修正：

- reset 和退出路径都按「`0xA15050` close → pool vt+0x40（`0x1310630`，waitForCompletion，清 `+0x154` / `+0x155` / `+0x140`）→ 再调 `0x118F0A0`」的顺序执行。
- 修补点要么一起打，要么都不打，每处都先核对指纹。实际实现（§12）用了三处：步进前唯一调用空函数的 `0x119932F` 改成每帧接队列（两次步进 `0x1199359` / `0x1199479` 都在它之后，不用分别改），两个 teardown 调用点 `0x70E38E`、`0x70FEC6` 改成先 close + 等待。
- 挂 worker 前核对 vtable（pool `0x1AFED80`、queue `0x182EC50` / `0x182EC80`），传给 pool 的是完整 queue 指针，不是 `+0x18` 的子对象。

## 10. 插件钩子逐个排查

做法：把 `src/` 和 `autoturret/` 里出现的全部 RVA 常量，和上面每个入口的 BFS 闭包取交集（`plugx.py`）。交集里只有签名或锚点（`Matches` / `memcmp`），以及下面两处真正会执行的代码。

| 钩子 | 运行线程 | 共享写 | 结论 |
|---|---|---|---|
| `src/physics.cpp`：在 `0x11DDFD0` 内 `0x11DE049` 处打的 code cave（custom character vertical contact），由 postCollide `0x11DFD50` 到达 | **PostCollide 任务（可能在 worker）** | 无：只读，`VerticalContactMaxImpulse` 读 `std::atomic<float> stepDt`，带 SEH | 安全 |
| `src/physics.cpp`：CheckSupportHook，替换 vtable 槽 `0x1AE4D50`（checkSupport `0x11DE5B0`） | 游戏线程（character 更新） | 只写 atomic `stepDt` | 安全 |
| `src/edf41robo.cpp:294-329`：经 `0x6E3A60` 把游戏回调 `0x6EC450` 挂到 signal `0xDAB920(world,2,id)`，组件是插件 `new unsigned char[0x100]` 分配的 | **事件派发任务（可能在 worker）** | 插件自己的组件（冷却） | 已串行，安全；组件释放必须在游戏线程注销之后（注销在 step 之外，成立） |
| `src/jet_hooks.cpp`：替换 CustomCollector@BulletControl 的 vtable 槽 `0x179E128`（addBody `0x232AA0`） | 同步宽相查询 `0x11A70B0`，由 `0x233CB0` 在 `0x234334` 以 `bullet+0x800` 调用；**不在任务里** | 无变化 | 不受影响 |
| `src/splitmissile.cpp`、`src/heli.cpp`：复制 vtable `0x1768B78`、替换 addHit，经 `0x11A7EE0` 同步射线（栈上 collector） | 调用线程（游戏线程）；`0x11A7EE0` 不投任务队列 | 栈上 | 不受影响 |
| `src/damagestats.cpp`：`0x547C30` 头部钩子、`0x232702` / `0x23426F` / `0x23250E` / `0x5425D6` / `0x5427BF` 的调用点 | 游戏线程（结算阶段） | `thread_local sourceStack` | 不受影响（step 里只入队，见 §3） |
| `src/boarding.cpp`：`0x230EA6` 处调用 `0x541FF0` 的调用点钩子 | 子弹命中路径，游戏线程 | — | 不受影响；contact 回调走的是别的调用点 |
| `src/enemy_control_pvp_native.cpp`：克隆敌人 vtable 的槽 4 / 5 / 7 / 14 | 游戏线程（敌人更新） | — | 不在 Havok 回调里 |
| `src/proteus.cpp` `0x11B95EB`、`src/vehsound.cpp` `0x7B4510`、`src/enemy_control_native_look_extra.cpp` `0x52F71` / `0x52FAE`、`src/sidecar.cpp` `0x114251`、autoturret 的 `0x231D97..0x2349D0` | — | — | 只是签名、锚点或文档引用，不是钩子 |

建议：在 Debug 构建里给 `physics.cpp` 的 cave 以外的所有插件钩子入口加一个守卫——「当前线程不是游戏主线程就记一行日志」，只记录不拦截，用来在 A/B 实测里抓遗漏的路径。

## 11. 推荐的串行化与上线条件

- 回调之间**不加锁**。前提是三个世界级任务保持单实例（`[task+0x10]=1`），这是任务对象自带的属性，不要去改。
- 新的并发面（narrowphase / solver modifier、MTQueryQueue）都只做只读加 per-item 写，不需要额外串行化。
- 必须做的：
  - §9 的 teardown 顺序，和打开一起打（§12）；
  - 挂 worker 前核对 vtable；
  - 插件钩子的非游戏线程 Debug 守卫：没有做。§10 已逐个确认会在 worker 上跑的插件代码只有两处且都安全，其余钩子都不在物理任务里；新加的钩子若落在物理回调里，按 §3 / §10 的方法重审。
- 实现默认开（ini `HavokMultithread=1`，委托方要求）；A/B 实测（§13）有问题就改默认值。实测时重点看 §7 第 1 条没展开的子图。

## 附：扫描脚本

扫描脚本都在会话 scratchpad 里，没有入库，按需重写：

- `rtti.py`：枚举全部 `.?AV` / `.?AU` 类和它们的 vtable；
- `subs.py`：hkSignal 订阅点；
- `fires.py`：signal 触发点；
- `scanw.py`：直接调用 BFS，收集写、锁、TLS、导入函数；
- `plugx.py`：插件常量和 BFS 闭包取交集。

## 12. 实现（`src/havok_mt.cpp`、`src/havok_mt_logic.h`）

ini `HavokMultithread=1`（默认开，只在插件启动时读，改了要重启游戏）。为 0 时什么都不改，日志 `HAVOKMT off`。

插件启动时（`src/plugin.cpp`，`InstallPhysics()` 之后）先核对，任何一项不符就一处不改、记一行 `HAVOKMT refused: …`：

- 三个调用点前后 12 字节（`mov rcx,[rip→0x20B30A0]; call rel32`）和它们原来的目标；
- 6 段函数指纹：`0x118F270`（`ret 0`）、processTaskQueue `0x130FE20`、waitForCompletion `0x1310630`、close `0xA15050` 的开头，复位 `0x118F110..0x118F13D`（先放 pool 再放 queue、中间没有 close），析构 `0x118ECAD`（自带 vt+0x68 close）；
- 四个 vtable 槽：pool `0x1AFED80` 槽 4 / 8 / 10 = `0x130FE20` / `0x1310630` / `0x130FF70`，队列子对象 `0x182EC80` 槽 13 = `0xA15050`。

然后改写三个 rel32（`RedirectCall`，经近处 12 字节 thunk 调到插件函数），**先**改两个复位调用点，**最后**改步进前的调用点；任何一处失败就把已改的按原字节写回（全有或全无）：

| 调用点 | 原来 | 现在（游戏线程） |
|---|---|---|
| `0x119932F`（System::update，两次步进之前） | `call 0x118F270`（`ret 0`） | `BeforeStep(holder)`：读 holder / pool / queue，核对三个 vtable；`[pool+0x140]` 为空、worker 数 > 0、队列没关时调 `0x130FE20(pool, queue)`（完整 queue 指针），日志 `HAVOKMT N worker threads process the physics task queue` |
| `0x70E38E`、`0x70FEC6` | `call 0x118F0A0`（复位） | `BeforeReset(holder)`：`[pool+0x140]` 非空且 vtable 都对时 `0xA15050(queue+0x18)` close → `0x1310630(pool)` 等每个 worker 退出 process（清 `+0x140` / `+0x154` / `+0x155` / `[queue+0x380]`），日志 `HAVOKMT reset: queue … closed and the workers back in X ms`；然后调原来的 `0x118F0A0` |

每帧的决策是纯函数 `DecideStep`（`havok_mt_logic.h`）：已接上 → 不动；接着别的队列、vtable 不认得、没有 worker、队列已关 → 不接，每种情况只记一次日志。复位会新建 pool 和 queue（`0x118F185` / `0x118F1E7`），新 pool 没接队列，下一帧 `BeforeStep` 再接，所以每次复位日志里应该是一对 `reset … closed` / `… process the physics task queue (attach #n)`。

正常析构 `0x118EC70` 自己在 `0x118ECB8` close，再释放 pool（pool 析构等 worker 退出），不需要改。只有 `0x118EB67`（持有者构造）调用的 `0x118F2E0` 也会建 pool / queue，那时都还是空的；`0x118F8B0` / `0x118F930` 没有任何引用（审计第 4 项）。

worker 空闲时睡在信号量上（mode 0 的处理循环 `0xA0F4F0`：`0xA0FC3B` LeaveCriticalSection → `0x9EAE60` WaitForSingleObject(INFINITE) → EnterCriticalSection），不自旋。

插件自己在物理任务里跑的代码只有 `src/physics.cpp` 的 vertical contact cave（只读、`std::atomic<float>`、SEH）和 `src/edf41robo.cpp` 借用的 `RagdollDefaultContactCallback`（写插件自己的组件，被世界级任务串行），都不用改（§10）。

### 离线验证

- `tests/havok_mt_test.cpp`（ctest `havok_mt`）：调用点字节解码、`CallBytes` 往返与 rel32 边界、指纹里决定顺序的字段与常量一致、`DecideStep` / `ResetNeedsDetach` 每个分支。
- `tests/havok_mt_audit.py`（ctest `havok_mt_native`，要 `EDF6_NATIVE_DLL`）：§12 的全部字节与 vtable 槽、RTTI 名；`0x118F270` / `0x118F0A0` 的 rel32 引用恰好是表里的调用点、没有绝对地址引用；复位与析构的顺序、setNumThreads 的 close → wait → reopen → processTaskQueue；持有者全局只在三处被写；System::update 步进期间不持锁；任务图（§7 第 1 条）；codec。带反向检查。
- `tests/havok_mt_sim.py`（ctest `havok_mt_sim`，要 unicorn，没有就跳过）：真 EDF.dll 代码在 unicorn 下跑——改写后的调用点经 12 字节 thunk 进桩时 rcx = holder、返回后栈平衡；processTaskQueue 在 3 个 worker 的假 pool 上写对字段、唤醒每个 worker；close 在锁内置位并唤醒每个睡着的 worker；waitForCompletion 等 3 次并清字段；之后 pool 析构不再等。反向：接上而不 close，pool 析构一上来就等 3 次 worker（真机上等不到，就是死锁）。

## 13. 实机待测项（主会话做）

装 DLL 前核对日志里 `HAVOKMT on`，进关卡后有 `HAVOKMT 15 worker threads process the physics task queue`（worker 数随 CPU 线程数）。A/B 用 `HavokMultithread=0/1`。

1. **帧时间**：stress-1 / 10 / 30 / 100 各跑一段，记录平均与 99% 帧时间。预期 stress-100 每帧省 8–13 ms、stress-30 省 3–6 ms。
2. **havok_update 占比**：同一场景下 System::update（`0x1198DA0`）每帧耗时与占帧比例；看 worker 是否真的分到任务（任务管理器 / ETW 里 15 个 worker 线程在步进期间有 CPU 时间）。
3. **任务切换 50 次无死锁**：反复进出任务 / 重开，每次日志应成对出现 `HAVOKMT reset: … closed and the workers back in X ms`（X 应在毫秒以内）和下一次 `attach #n`；任何一次卡住就是复位顺序还有漏洞。退出游戏（回主菜单、直接关窗口）同样不能挂住。
4. **worker 空闲时 CPU**：暂停 / 菜单 / 过场时 15 个 worker 应睡着（占用接近 0）；不应出现自旋占满核。
5. **联机影响**：两台机器一开一关、都开，看物理对象（尸体、碎片、集装箱、载具）表现与同步是否和原版一样；hknp 多线程求解的结果是否和单线程逐位一致没有验证，房主 / 客人各算各的物理对象可能出现比原版更大的分歧。
6. **回调换线程**：碰撞音效（载具撞击、弹壳落地、布娃娃落地）、碰撞伤害（`RigidBodyContactDamageControl`、巨型单位挤压）、地图破坏、EDF4.1 机器人（edf41robo）的接触冷却，与原版逐项对比，留意崩溃、丢声、重复伤害。
7. 残余推测项：§7 第 1 条里没展开的 `0x9EC7A0` / `0x9ECCE0` 子图——实测中出现回调期间的随机崩溃或物理状态错乱时，优先怀疑这里（`HavokMultithread=0` 能消失即是）。
