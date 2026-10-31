# PvP 预设敌体出生：原生网络身份静态逆向与插件配方

对象：Steam `EDF.dll`，TimeDateStamp `0x678CCB46`（`sigs_netspawn.py` 读到 `0x678ccb46`）。地址都是 RVA。
只读：没有写游戏目录，没有启动游戏，没有改仓库。
依据：`edf.asm` 全量反汇编、pefile 读出的入口字节、仓库现有代码（`src/support_soldier.cpp`、`src/support_protocol.*`、`src/debug_spawn.cpp`、`src/edf41net.*`、`docs/enemy-control-network-contract.md`）。
标记：【证实】= 指令或字节已核对；【推断】= 由结构推出，没有逐条证明，也没有实机验证。

---

## 0. 结论先行

1. 【证实】**EDF6 原生 AngelScript 任务的 CreateEnemy 路径（`0x1AD220 → 0x1D8900 → 0x1D9C20 → CreateObject 0x11945E0`）本身不调用任何注册函数。** 网络身份由另一处给出：脚本执行器 `0x1DE8A0` 在执行脚本前，把一个 lambda 装到对象管理器的「对象创建回调」`objMgr+0xC70`（`0x1198BD0`）上，脚本跑完再清掉。窗口内由脚本直接创建的每个顶层对象，都会被这个 lambda `0x1DED20` 用 `0x7834F0` 注册。
   - NetId 类型为 0（`0x7772D0`），`+0` 是一个 32 位脚本序号，由 `0x1D94C0` 从当前 AngelScript「`__event_thread_info`」记录拼出（线程索引 / 字节 / 每线程计数器），**取一次计数器加一**。
   - 拥有者是房主：`remoteOwned = IsGuest (0x784120)`，所以房主上 `+0x128` 置 bit1，客人上置 bit0。
   - **每台机器各自跑同一份脚本、各自创建、各自注册**，没有生成包。客人在注册前还会先调 `0x54EDB0(obj)`。
   - BVM（EDF5 / 4.1 格式）的 CreateEnemy `0x2154C0` 等直接调 `0x7834F0`，ID 换成字节码偏移 `0x10AC6E0`，其余相同。
2. 【证实】`0x781630` 是**子对象注册**：`(mgr, weak 子*, weak 父*, int index, int sub, byte remoteOwned)`。ID 是类型 4，内容为 `{u16 index, u16 sub, u32 父 ID 的哈希}`（`0x7766B0`）。父对象还没注册时直接放弃（`0x784A10` 返回 0 → `0x7818B3`）。各调用者传 `remoteOwned = 父+0x128 & 1`，父是远端时先 `0x54EDB0(子)`。
   - `0x776790` 是派生 ID：类型 5，`{u32 父 ID 哈希, u32 ordinal}`。载具投送和插件的 support_net 都用它。
   - 已见的类型字节（`+0xC`）有 0..8 和 0xFFFFFFFF。
3. **配方**：直接复用现有 support_net 事务（`SubmitSupportRequest` → 房主 Plan → 全员 PREPARE/READY → COMMIT 各自 Create+Register → ACTIVATE）和 `RegisterSupportObject`（`0x781950`，拥有者 = 房主）。
   - ID 由房主用 `0x776790(房主玩家的 NetId 条目, ordinal)` 派生，放进锁定的计划表下发。
   - 敌体操控网（`enemy_control_net`）的契约就是「原生拥有者 = 房主，控制者只发语义输入，房主 30 Hz 发位姿」。**不要**把拥有者设成控制者的机器（不要用 `0x7813A0` 并传 remoteOwned=0）。

---

## 1. 任务脚本的 CreateEnemy 怎样得到网络身份

### 1.1 AngelScript 路径（EDF6 原生任务）

| 步骤 | 地址 | 内容 | 标记 |
|---|---|---|---|
| 脚本原生 CreateEnemy | `0x1AD220` | 填创建描述符：team 1（`0x1AD37F`）、level（`+0x80`）、`+0x84` 激活 bool；调用 `0x1D8900` | 证实 |
| 公共创建 | `0x1D8900` | `0x1D9C20` 创建 → 转型为 GameObjectBase（`0x1D893E`）→ `SetTeam 0x54EE70(obj, team, 1)`（`0x1D8A6D`）→ `+0x85` 时 `SetLevel 0x54E740`（`0x1D8A99`）→ `+0x84` 时 `0x548D50`（`0x1D8AAB`）→ `0x1DB1E0` 入脚本对象组 | 证实 |
| 实际创建 | `0x1D9C20` | 可选落地修正（`+0x86`），再 `CreateObject 0x11945E0(objMgr [0x20B2958], matrix, sgoPath, InitParam{vt 0x1762068})`（`0x1D9DD5`） | 证实 |
| 上面三个函数里的注册调用 | — | **没有**。`0x7834F0` 的调用者里没有 `0x1AD`… / `0x1D8`… / `0x1D9`… 开头的地址 | 证实 |

**真正的注册在脚本执行器里**：

- `0x1DE8A0`（入口 `40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 D9 48 81 EC 00 01 00 00`）
  - `0x1DE8BA`：在栈上构造 `std::function`，vtable `0x1793200`（slot2 `_Do_call` = `0x1DED20`），捕获 `this`。
  - `0x1DE8DC`：`0x1198BD0(objMgr, &fn)`，把它写进 `objMgr+0xC70`（`0x1198BDD add rcx,0xC70`）。
  - `0x1DE8E5`：`0x1292EA0(this+0x50)`，执行脚本。【推断】这里是 AngelScript 上下文 / 事件线程的执行。
  - `0x1DE8FC`：再调一次 `0x1198BD0`，传一个空 function（`[rbp+0x17]=0`，也就是 `fn+0x38 = 0`），回调被清掉。
- CreateObject 内部（`0x11945E0`）：`0x119486E inc [objMgr+0xCB0]`，`0x119487A` 调工厂的 Create，`0x119487D dec`，然后在 `0x1194927..0x1194952` 调 `objMgr+0xCA8` 处的回调，参数是 `(&obj, &depth=[objMgr+0xCB0])`。另一个创建入口在 `0x119436F`，`0x1197050` 是独立的通知入口。【证实】
- lambda `0x1DED20`（入口 `40 56 48 83 EC 40 41 83 38 00 48 8B 71 08 0F 85`），逐条：
  1. `cmp [r8],0; jne`：**只处理深度 0 的对象**，也就是工厂在构造期间造出的子对象不处理。【证实】
  2. `__RTDynamicCast(obj, SceneObject, net::NetworkObject 0x200CB20)`，然后调 NetworkObject vfunc+8 并传 `&1`，返回真就跳过。【证实，语义推断：查询「不参与网络」一类的标志】
  3. `isGuest = 0x784120(mgr [0x20B2AC8])`（`0x1DED93`）。
  4. `id32 = 0x1D94C0(ctx+0xE8)`（`0x1DEDA2`）。
  5. 转型为 GameObjectBase；**客人上**调 `0x54EDB0(obj)`（`0x1DEDD6`）。
  6. `0x7834F0(mgr, weak obj（另持一个弱引用）, byte isGuest, int id32, int -1, float 0.0)`（`0x1DEE20`）。
- `0x1DE2E0` 是同一套逻辑的非 lambda 版本（同样调 `0x7834F0`，在 `0x1DE3DA`），没找到指针引用。【证实存在，调用方未知】

**脚本序号 `0x1D94C0`**（入口 `48 83 EC 28 E8 67 0B 00 00 4C 8B C8 48 85 C0 75`）【证实】：

```
rec = 0x1DA030(ctx)     // 按名字 "__event_thread_info"（0x17928B0）取当前事件线程记录：ctx+0x2C8 + idx*0x38；取不到返回 -1
id  = (((([0x20B28A8]+0x5C) << 12) | (rec+0x30 & 0xFFF)) << 8 | (byte)rec+0x2C) << 11 | (rec+0x28 & 0x7FF)
rec+0x30 += 1           // 每线程的创建计数
```

- 位布局：bit0..10 是线程字段 `+0x28`，bit11..18 是 `+0x2C`，bit19..30 是计数（12 位，会回绕），bit31 是 `[0x20B28A8]+0x5C` 的最低位。
- 【推断】`+0x28` / `+0x2C` 标识事件线程，`[0x20B28A8]+0x5C` 是任务级的轮次值。
- 所以 ID 由「脚本走到哪条事件线程的第几次创建」决定。前提是每台机器按同样的顺序执行同样的脚本。

### 1.2 BVM 路径（EDF5 / 4.1 格式任务）

- `0x2154C0`（CreateEnemy 2000）、`0x215120`、`0x21B590`（CreateNeutral 1020）等 18 处直接调 `0x7834F0`。见 `0x215414`：
  - 需要 `0x7734B0`（GameStatus 联机标志）为真，且 `0x773490`（= `0x7859A0`，在房间里）为真。
  - `isGuest = 0x784120`，客人上先 `0x54EDB0`。
  - `id32 = 0x10AC6E0(ctx+0x70)` = `pc - codeBase`，即**字节码偏移**。【证实】
- 玩家对象 `0x22AB90` 也走 `0x7834F0`（`0x22B045`），`id32` 同样来自 `0x10AC6E0`，玩家序号走第 5 个参数，而 `0x7772D0` 不读它（与 `docs/edf5-online-re.md` 一致）。

### 1.3 `0x7834F0` 本体

- 入口 32 字节：`40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 E9 48 81 EC C0 00 00 00 0F 29 B4 24 B0 00 00 00 48`
- **注意**：前 16 字节与 `0x781630` 完全相同，签名至少要比到第 17..23 字节（`C0` 对 `D0`）。
- 参数：`(rcx mgr, rdx weak<SceneObject>*, r8b remoteOwned, r9d id32, [rsp+0x28] int（不用）, [rsp+0x30] float（不用）)`。
- 流程【证实】：
  1. `0x22FCA0` 取出 NetworkObject。
  2. `0x7772D0(&id, id32)`：NetId 0x20 字节清零，`+0 = id32`，`+0xC` 类型 = 0；再由 `0x776450` 对前 0x14 字节算 FNV 哈希，写进 `+0x18`。
  3. `0x774BD0(netObj, &id)`：分配 0x48 字节的条目（vt `0x17EE1D8`），`+8..+0x1F` 抄 ID 和哈希。
  4. `0x782720(mgr, …, weak, …, remoteOwned, 1)` → `0x782880`：
     - 房间对象 `[0x20B2AC0]-0x98 → +0xD0` 为空时直接返回，所以离线时不入表（`0x78290F`）。
     - 否则按 remoteOwned 设 `NetworkObject+8` 的 bit0 或 bit1（`0x782945..0x782976`）。
     - 遍历它的子 NetworkObject（`+0x28` 数组 / `+0x38` 个数），逐个派生 ID 并 `0x774BD0`。
     - 最后 `0x75BE80` 把对象追加进房间 `+0x50` 的链表。**这一步不查重。**

### 1.4 回答问题 1

| 问题 | 答案 | 标记 |
|---|---|---|
| 哪个注册函数 | `0x7834F0`。AngelScript 任务经执行器装在 objMgr 上的创建回调 `0x1DED20` 调到它；BVM 任务由脚本原生函数直接调 | 证实 |
| NetId 怎么来 | 类型 0，`+0` = 32 位脚本序号。AngelScript：事件线程记录拼出的序号，每次创建计数加一。BVM：字节码偏移 | 证实 |
| 注册拥有者 | 房主（`remoteOwned = IsGuest`） | 证实 |
| 客人是自己造还是收包 | 每台机器自己跑脚本、自己 CreateObject、自己注册；客人另外先调 `0x54EDB0` | 证实（「没有生成包」这一点与 docs §2.3 的载具结论一致） |

`0x54EDB0`（`C7 81 38 03 00 00 FF FF FF FF 48 C7 81 40 03 00`）：`+0x338 = -1`，`+0x340 = 0`，再对 `+0x348` 调 `0x11C1090`。【推断】它清掉本地目标状态，让拥有者的目标消息（消息 1）来驱动。载具集装箱路径在联机时**每台机器**都调它（`0x5E91D7 → 0x7748F0 → 0x5E91E7`）；脚本路径和子对象路径**只在远端副本上**调。

---

## 2. `0x781630` 与 ID 派生族

### 2.1 `0x781630`【证实】

- 入口 32 字节：`40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 E9 48 81 EC D0 00 00 00 48 8B 05 0A DA 86 01 48 33`
- 签名：`void* 0x781630(mgr [0x20B2AC8], weak<Scene>* child, weak<Scene>* parent, int index (r9d), int sub ([rsp+0x20]), byte remoteOwned ([rsp+0x28]))`。
- 两个 weak 参数由被调者释放（`0x781878` / `0x781894`），这与 `src/edf41net.cpp:11` 的注释一致。
- 流程：
  1. `0x22FCA0(child)` 取 NetworkObject。
  2. `0x784A10(parent)` 取父的 NetId 条目；为 0 就放弃（`0x7816AB je 0x7818B3`）。所以**父必须已经注册**。
  3. `0x7766B0(&id, parentEntry, index, sub)`：类型 4，`+0 u16 index`、`+2 u16 sub`、`+4 = 0x776AB0(parentEntry+8)`（对父 ID 前 16 字节做哈希），`+0x18 = 0x776450`。
  4. `0x774BD0` 写进 ID，`0x782720(…, remoteOwned, 1)` 入表。
- 调用者的固定写法：`remoteOwned = byte(父+0x128) & 1`（例如 `0x3937D0 and r8b,1`、`0x4F4063 and cl,1`）；父是远端时先 `0x54EDB0(子)`（例如 `0x4F4048..0x4F4050`、`0x4135F8`）。
- 各调用者的 (index, sub)【证实取值来源，类名未逐个核对】：

| 调用点 | index | sub |
|---|---|---|
| `0x324440` | 0 | 0 |
| `0x32520A` | 1 | r8d |
| `0x37E5B5` | 0 | eax |
| `0x393831` | r15d | ebp |
| `0x402227` | 0 | 0 |
| `0x413671` | r15d | 0 |
| `0x44BDFA` | 0 | 0 |
| `0x4750D1` | r15d | ebp |
| `0x4C7B33`（MovingFortress） | 0 | 父计数 `+0x1D4C` |
| `0x4F40C6` / `0x4F50F4` | 父 `+0x1790` | esi / edi |
| `0x6D03C2`（Deiroi） | 原参数 `+0x90` | 原参数 `+0x94` |
| `0x775E84` | r13d | r12d |

### 2.2 NetId 构造族（`+0xC` 是类型）【证实】

| 构造 | 类型 | 内容 |
|---|---|---|
| `0x7772D0` | 0 | `+0 = id32`（脚本 / 玩家） |
| `0x777276` | 1 | — |
| `0x776A7C` | 2 | — |
| `0x77737F` | 3 | — |
| `0x7766B0` | 4 | 子对象：`u16 index, u16 sub, u32 父哈希` |
| `0x776790` | 5 | 派生：`+0 = 父哈希`（`0x776AB0(parentEntry+8)`），`+4 = ordinal`。入口 `48 89 5C 24 08 57 48 83 EC 20 33 C0 0F 57 C0 48`。**它不写 `+0x14..+0x17`**，所以插件调它前要先 memset |
| `0x7765BF` | 6 | `0x7813A0` 的子弹派生路径用 |
| `0x7768F8` | 7 | — |
| `0x7768B8` | 8 | — |
| `0x7774C8` / `0x777C9F` | `0xFFFFFFFF` | — |

- 标识共 0x20 字节：前 0x14 字节是身份，`+0x18` 是哈希。`src/support_soldier.cpp:332` 的 `ReadNativeObjectId` 照此复制 `entry+8` 的 20 字节和 `entry+0x20` 的 8 字节。
- 游戏自己用类型 5 的地方【证实】：
  - 烟幕弹 `0x2A7C6D` / `0x2AA90A`：父 = 子弹 NetworkObject vfunc+8 给出的对象，ordinal = 子弹 `+0x128`。
  - Transporter `0x5E518D` / `0x5EA82F`：ordinal = ebx（计数）。
  - 集装箱 `0x5E9280`：ordinal = `+0xC30`。
  - 【推断】这些 ordinal 都是从 0 起的小计数，父对象不是房主玩家就是投送链上的对象。

### 2.3 `0x781950`（载具投送 / 插件 `RegisterSupportObject` 用的入口）【证实】

- 入口：`48 89 5C 24 20 55 56 57 41 56 41 57 48 8D 6C 24`
- 签名：`0x781950(mgr, weak<Scene>* obj（被调者释放）, const NetId* id)`。
- 先把 ID 转成字符串写日志（格式 `"Vehicle :: %ls\n"`，`0x17EE6B8`），再 `0x774BD0` 写入，然后 `remoteOwned = !IsHost(0x784210)`（`0x781A0C..0x781A16 sete cl`），最后 `0x782720`。
- 也就是说，**拥有者固定为房主，调用者改不了**。
- 要指定拥有者只能用 `0x7813A0(mgr, weak*, NetId*, r9=0, byte remoteOwned)`，但它内部用类型 6 的构造（见 docs §2.1）。【推断：会改写传入 ID 的类型】

---

## 3. 插件配方：每台机器造同一个敌体，绑成一个网络对象

### 3.1 拥有者应该是谁【证实，依据仓库契约】

- `docs/enemy-control-network-contract.md:21` 原文是「不会修改敌体 `NetworkObject +0x128` 来伪装客户端拥有者；原敌体仍由原生 host 发送位姿…」。同文件 :23 写明 host 验证本机拥有目标，guest 验证同一注册 ID 在本机是远端副本；:47 写明房主约 30 Hz 读取并发布状态。
- `src/enemy_control_net.cpp:1` 的注释：「Native ownership stays with the host」。
- 所以 PvP 出生的敌体**必须是房主拥有**：房主上 `+0x128&3 == 2`，客人上 `== 1`。控制者（可能是客人）的输入走现有 `enemy_control_net` 送到房主，房主跑原生 AI / 动作，再把位姿发给全员。
- 改成「拥有者 = 控制者的机器」会破坏现有 lease 验证（房主会拒绝不归自己的目标），也会让原生的命中判定、HP 广播、子对象拥有者跟着错位。

### 3.2 步骤（复用 support_net 事务，与 `debug_spawn.cpp` 的 `How::enemy` 联机路径同构）

1. **预载（每台机器）**：任务开始时用 `0x7A3780(preloadMgr [0x20B29A8], sgo, 2, -1)` 预载候选 SGO，或者按需预载并让 PREPARE 等它完成（`LoadedHere` 的模式）。EDF4.1 移植敌体要求每台机器都装了同一份 Mods 文件。
2. **房主 Plan**：每个出生单位一项。
   - `unit.matrix` 由房主算好（出生点矩阵）。
   - `unit.netId` 由房主派生：`memset(id,0,32); 0x776790(id, 房主本机玩家的 NetId 条目, ordinal)`，即现有的 `DeriveSupportSoldierNetId(PlayerHuman(), ordinal)`。派生只在房主做、随计划表下发，所以全员用的是同一个 ID。注意：玩家自己的 NetId 在各机器上**不保证相同**——原版（AS）任务按脚本线程的注册计数编号（`1D94C0`），BVM 任务见 PR #299。所以计划表里的请求者不用 NetId，而用任务玩家序号（`src/player_requester.h`，2026-10-31）。
   - `plan.team` 填 PvP 阵营。计划表锁定后下发，**客人不自己派生**，只做校验。
3. **ordinal 的取值**：现有 `support_protocol.h:217` 从 `0x40000000` 单调递增，满 `0x80000000` 就失败关闭；`support_protocol.cpp:54` 只接受类型 5 且 ordinal 在 `[0x40000000, 0x80000000)` 之间。游戏自己的类型 5 计数从 0 起（【推断】不会到 2^30），所以不撞。
   - 如果要「由共享数据确定性推出」，建议在同一区间里划出子段，例如 `0x70000000 | (matchEpoch & 0xFFF) << 12 | memberIndex`，同时把 support_net 的上限压到 `0x70000000`，两者就不重叠。
   - 我更推荐直接走 support_net 的单调分配：房主是唯一分配者，事务本身已经带世界 epoch 和全员确认，不需要再设计一套 epoch 编码。
4. **PREPARE / READY（每台机器）**：校验 SGO 已加载、profile 合法（`EnemyControlNativePvpSpawnProof` 那类资源证明）。
5. **COMMIT（每台机器，同一协议步）**，照 `debug_spawn.cpp` 的 `MakeNetworkObject` 敌体分支：
   - `obj = 0x11945E0(objMgr [0x20B2958], matrix, sgo, InitParam{vt 0x1762068, 其余 0})`，入口 `40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 D9 48 81 EC F0 00 00 00`。
   - 转型为 GameObjectBase（`0x12DA7AA`，类型 `0x2006450` → `0x2006400`）。
   - `SetTeam 0x54EE70(obj, team, true)`（`48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41`）。
   - `SetLevel 0x54E740(obj, float)`（`48 8B C4 48 89 58 18 55 56 57 41 56 41 57 48 8D`）。
   - **补一步（与原生脚本路径对齐）**：客人上（`0x784120` 为真）调 `0x54EDB0(obj)`，再注册。现在 `debug_spawn.cpp:586-595` 没有这一步。【推断：不调的后果是客人副本保留自己的本地目标状态】
   - `RegisterSupportObject(obj, netId)` → `0x781950`。它已经校验了：注册前 `+0x128&3==0`、ID 类型 5、返回的 ID 与传入一致、`+0x128&3` 等于期望值（房主 2，客人 1）、`+0x130` 非空。
   - 先**不**激活。
6. **ACTIVATE（全员都已注册之后）**：`0x548D50(obj)`（`40 53 48 83 EC 20 80 B9 A0 04 00 00 00 48 8B D9`），对应 CreateEnemy 的 `+0x84`。
7. **交给敌体操控**：控制者的机器发起现有 enemy-control lease（source = 控制者玩家的 NativeId，target = 上面的 netId，类型 5 符合 `CanonicalId` 的要求，`enemy_control_protocol.h:87`），再加 PvP 阵营租约（`docs/enemy-control-pvp-native.md`）。

### 3.3 时机硬约束【证实机制 / 推断后果】

**插件的 CreateObject 绝不能放在脚本执行窗口里**（`0x1DE8A0` 的 `0x1DE8DC..0x1DE8FC` 之间，例如某个脚本原生函数的 hook 内部）。否则：

- 原生 lambda 会先用类型 0 的脚本序号注册它，同时让该事件线程的计数加一。
- 随后 `RegisterSupportObject` 发现 `+0x128&3 != 0`，返回失败。
- 只有部分机器这样做时，**这条事件线程之后所有脚本敌人的 ID 在各机器之间整体错位**，全局失步。

所以出生要放在 support_net 的游戏帧 tick 里（`SupportNetTick` / `RangeEnemyNetworkTick` 那一层）。同样要避免在某个敌体工厂的构造过程里造（深度 >0 时原生 lambda 虽然跳过，但工厂可能另外调 `0x781630` 派生子 ID）。

---

## 4. 风险

| 风险 | 说明 | 处理 |
|---|---|---|
| ID 碰撞 | 注册链 `0x782880 → 0x75BE80` 只是追加进链表，**不查重**【证实】。重复 ID 的收包路由不确定【推断：命中先找到的那个】。类型 0 的脚本 ID、类型 4 的子对象 ID 都不会和插件的类型 5 撞（类型在 0x14 字节的身份里）。可能撞的只有：同一锚点（房主玩家）下的类型 5 ordinal | 只用 `[0x40000000, 0x80000000)`；多个插件模块共用 support_net 的单一分配器，不要另开一个从 0 起的计数器 |
| 锚点 | 房主玩家对象换了（复活或重建出新对象），ID 也会变 | ID 在 Plan 时算好随表下发，各机器不各自重算 |
| 自定义类型字节 | 0..8 都被游戏用了；接收端是否按类型分派没有查完 | 不要自造类型 |
| 中途加入 | 中途加入的成员拿不到这次事务。现有 support_net 把参与者封存到「本世界」，新成员等下一个世界（`support_net.h` 的 `SupportMissionPlayerAllowed` / `SupportParticipantAllowed`）【证实设计】 | PvP 预设出生也只对封存参与者有效；新世界开始时重新跑一遍出生事务 |
| 某个客人创建失败 | support_net 的 PREPARE 不就绪、COMMIT 失败或超时，都会 Cancel 并让全员回滚（`DestroyRangeEnemy`），**不会留下只有部分机器有的对象** | 照用。失败要报给 UI，不要重试成部分机器有 |
| 原生子对象 | Boss 类敌体会在构造或首帧用 `0x781630` 给部件 / 生成物派生类型 4 的 ID，要求父已注册、各机器创建顺序相同 | 注册放在 Create 之后、首帧 Update 之前（现有顺序就是这样）。插件自己的部件走 `edf41net::Child` / `Pump` |
| 死亡与消失同步 | 原生**没有**「删除对象」的网络消息【推断：脚本对象由各机器的脚本各自删】。HP 变化走对象事件类型 2，击杀由 `0x7746C0` 排队、各机器重放（docs/edf41-classes.md 联机 A 节）。尸体清理在各机器本地 | PvP 回合结束或重生时，用插件自己的可靠消息在同一协议步让每台机器 `Delete 0x118A1B0`。support_net 的 Destroy 现在只用于回滚，需要加一个「退场」类事务，或让 PvP 层自己做全员删除确认。客人先删自己的副本可以接受（【推断】房主发给这个 ID 的包会落空），但控制 lease 要先 drain 再 Restore（契约 :61） |
| 离线 / 单人房 | `0x782880` 不在房间时不入表；`0x784210` 不在房间时返回 1 | 离线走 `debug_spawn` 的本地路径，不注册 |
| 签名 | `0x7834F0`、`0x781630`、`0x11945E0`、`0x1DE8A0` 的前 11 字节都是 `40 55 53 56 57 41 54 41 56 41 57`，前两个连前 16 字节都一样 | 校验至少 24 字节，或者附加一个内部固定偏移 |

---

## 5. 没有确定的

- lambda 第 2 步 NetworkObject vfunc+8(1) 的确切语义。
- `[0x20B28A8]+0x5C` 是什么。
- `0x1292EA0` 是否就是 AngelScript 事件线程的推进。
- `0x54EDB0 → 0x11C1090` 的作用。
- 收包时按 ID 查对象的函数，以及重复 ID 的实际行为。
- 原生有没有对象删除的复制。

这些都只做了静态阅读，没有实机验证。本机开不出两端的房间，联机结论都要等双机实测。
