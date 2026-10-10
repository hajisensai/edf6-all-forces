# EDF5 任务包联机：BVM 玩家原生函数的联机分支（2026-10-10，静态，H 除非另注）

## 现象

2026-10-10 两台机器联机打 EDF5 DLC2 包第 1 关（`EDF5_OLD_SCRIPT/DLC/DM002`，房间 `DLC = 5`、`MISSION_NO = 1`）。两边一直黑屏，38 秒后房主退出：

- 房主以约 20 Hz 给客人推送对象状态（`NETTYPE 0x03300`）；客人那边房主的远程副本一直在报 `WORLD target: remote copy … chooses its own target (it has none)`。DM002 的敌人（`e506_ha_biggrey_ll`）是在 CreatePlayer **之后**才生成的（字节码 pc 0x62D 起），所以脚本已经越过了 CreatePlayer，只是场上一个玩家都没有，敌人也就没有目标。
- 两边插件都没有记 `MISSION start`（AF 挂在玩家预加载上），Coop 也没有记 `MISSION CreatePlayers`。

原版 EDF6 离线、在线都不会运行 `EDF5_OLD_SCRIPT` 里的脚本：原版那 28 关 EDF5 时代的关卡用的是重写过的 `EDF6/RM015B2` 等（`MISSION.AC`）。所以 BVM 执行器的联机路径从来没有被原版走过。

## BVM 执行器

- `MissionScriptBVMImplement`：单步函数在 `EDF+10AB600`（跳转表 `10AC49C`，opcode 0..0x38）。opcode 0x2C..0x2F 是原生调用，立即数是原生编号，经 `[[proc+0xD8]]+0`（vtable `179D298` 的 slot 0）进入分派器 `EDF+20FF70`。
- DM002 的 `Main`：BeginLoading(0x0A) → Preload(0x0D)… → PreloadMap(0x0E) → **PreloadPlayerResource(0x10)** → WaitPreload(0x0C) → Online_WaitStart(0x2710) → EndLoading(0x0B) → … → **CreatePlayer(0x3E8)** → 敌人分队 / 敌人。和 AngelScript 的 `M001.AC` 是一一对应的顺序。

## 两个空的联机分支

联机判据和 AngelScript 那边一样：`[GS+0x38] != -1`（GS = `[EDF+20B2890]`），并且 `[[GS+0x20][slot]+0x10]` 的虚基对象 `+0x68 != 0`。

| 原生函数 | BVM 实现 | 联机时 | AngelScript 对应 |
|---|---|---|---|
| 0x10 PreloadPlayerResource | `225E30`，唯一调用点 `210AE0`（`lea rcx,[r14-0x30]`） | `225EA9 call 20C5B0` 传 `rdx = r8 = 0`（`vector<shared_ptr>::assign` 赋空区间），然后析构空 vector 并返回；`225FAC` 那个联机调用永远走不到 | `1B8CC0`：从会话对象（`[EDF+20B2AC0]-0x98` 的 `+0xD0`，调用 `vf+8`）取出玩家列表，逐个调用 `59DC90`（`1B8E98`）；离线分支是同样的 `59DE50(i)` 循环（`1B8F52`）。两个函数都不读参数 |
| 0x3E8 / 0x3E9 / 0x3EA CreatePlayer / _NoWeapon / _InitWeapon | `22B1C0(rcx = BVM 对象, rdx = wchar_t* 出生点名, r8d = 模式 0/1/2)`，调用点 `2142F2 / 21430E / 214332` 与跳板 `21D8C3 / E6 / F6` | `22B36C..22B399` 同样赋空区间，然后 `22B39F jmp 22BB94` 跳过整个创建循环 | `1BCC50(rcx = AS 绑定对象, rdx = const wstring*, r8d = 模式)` → `1D9520`：联机和离线**共用一个循环**，只有人数不同：离线 `GS+0x14FF4`，联机 `GS+0x14FF8`（`1D95F4`） |

第三个同样的空壳 `22C4D0` 只在 `.pdata` 里有记录，没有调用者，也没有被虚表引用。全映像里调用 `20C5B0` 的只有这 3 处。

AngelScript 的实现不能直接拿来给 CreatePlayer 用：AngelScript 原生函数的 rcx 是 AS 实现对象 `+0xF8` 处的绑定对象（注册函数 `1E31F0` 由 `1DD8BD` 以 `rdx = [rdi+0xF8]` 调用），它的 `+8` 是 AngelScript 任务对象，玩家表在 `+0x100`。BVM 对象的玩家表在 `+0x168`，两边各管各的。

## 修法（`src/edf5online.cpp`）

1. `210AE0` 的 `call 225E30` 改为调用 `1B8CC0`（经 `RedirectCall`，先核对原目标）。之后 AF 已有的 `1B8E98` / `1B8F52` 钩子照常触发 `MissionStart`。
2. `22B36C` 写入 17 字节：`mov rax, &Edf5BvmOnlinePlayers; call rax; mov r13d, eax; jmp short 22B3B0`。人数是会话人数 `GS+0x14FF8`，然后进入原有的创建循环。
   - 状态：离线路径也是从 `22B353 / 22B36A` 跳到 `22B3B0`。那时 `xmm0 = 0`（`22B333`），`r15 = 0`，`[rbp-0x58]` 是空 vector；循环用到的输入只有 `r13d`。
   - 调用约定：插入点的 rsp 是 16 字节对齐的（函数序言之后），被调函数可能写的 `[rsp+0..0x1F]` 是本函数的出参影子区，此时没有活数据（`22B38A` 读的 `[rsp+0x50]` 不在这个范围内）。
   - 单人创建 `22AB90` 创建对象的联机分支是完整的：`22AC2F` 调用 `591130(ecx = 序号, rdx = 矩阵, r8d = 模式)`，和 AngelScript 的 `1DC525`（`ecx = [记录+8]`，`r8d = [记录+0x54]`）一致。
   - **但它的第 5、6 个参数在联机时不对**（2026-10-10 审查发现，`d8254fe` 修复）。第 5 个是本机手柄号，循环从常量 `{0,1,2,3}`（`1765B00`，`rbp+0x150`）里取；第 6 个是分屏除数，就是循环人数 `r13d`（`22B60F`）。`22AB90` 在 `22ACF6` 读手柄号：小于 0 时跳到 `22AFB1`，既不绑定输入也不建视口；否则先 `54ED40` 绑定这个手柄，再按 `[rbp+0x108]`（除数）切分视口宽度（`22AD43..22AD5E`），然后 `1198C40` 建视口。原样跑的话，每台机器都会按会话人数分屏，而且所有机器的 0 号玩家都绑定本机手柄。
   - AngelScript 的做法（`1D9520`）：先取会话用户列表（与 `1B8CC0` 同一个会话对象，`734670` 拷贝 `vf+8` 的结果），对每个用户调用 `12AC420`，即 `(用户+0x10 >> 1) & 1` 的远端位（`1D98B1`）。手柄号：远端填 -1，本机填本机计数（`1D9A64..1D9A74`）；本机计数只在创建成功、且玩家 `+0x128` 第 0 位为 0 时递增（`1D9B50..1D9B5F`）。除数填本机人数 `GS+0x14FF4`（`1D9A77`）。
   - 修法：`22B36C` 处的人数函数进入循环前读一次会话用户的远端位，按序号记下；参与者准入门包住的正是 `22B626 → 22AB90` 这个调用（`mission_participant_gate.cpp` 的 `UpperHook`），在那里按 AngelScript 的规则改写第 5、6 个参数，创建后回报玩家对象，供本机计数递增；循环最后一个序号处理完就解除。准入门在 `MissionStart` 的 `ResetSupportDispatch` 里安装，早于创建。准入门没装上、或者会话用户列表读不到时，人数函数返回 0，一个玩家都不创建，并记日志：宁可黑屏，也不能绑错手柄、分屏。
   - 用户列表的释放照游戏的写法：每个 `shared_ptr` 按 MSVC `_Decref`（`+8` 减到 0 调 vtbl[0]，`+0xC` 减到 0 调 vtbl[1]），存储用游戏带大小的 `operator delete`（`12D85EC`），`>= 0x1000` 字节时取 `[ptr-8]`、大小加 0x27（`1B8DA4..1B8E1F`）。
   - 循环里的 `1F74A0` 是把玩家 `push_back` 进脚本当前的对象分组（`[rsi+0x2D0]` 是分组序号），不是镜头。
   - AngelScript 在循环之后会把 `MissionGameOverEvent` 注册为全灭事件（`1D1CB0`）。BVM 脚本自带同名函数，由脚本自己处理全灭，所以 BVM 不需要这一步。
3. **人数上限 4**：出生点偏移是栈上 4 个 16 字节槽位（`rbp+0x170..0x1A0`，`rbp+0x1B0` 是栈 cookie），BVM 玩家表是 4 个 0x18 字节条目（`+0x168..+0x1C7`，`+0x1C8` 起是对象列表）。会话超过 4 人时只创建前 4 个序号；如果本机玩家的序号在 4 以后，会额外记一行 `this machine's player is past the script's 4 slots`，因为这台机器上就不会有自己的玩家，并记一行 `EDF5 online: N players in the session, an EDF5 mission creates 4`。EDF6Coop 的 `[Mission] Extend=1` 会把第 5 个以后的条目当作空槽读（`multislot/src/patches.h` 的 BvmPlayerTableHooks），所以不会越界读。

EDF6Coop 已经知道这张表（同一处注释："filled by its CreatePlayers (22B1C0) for the split-screen players (GameStatus+0x14FF4)"），并且指出那些读取它的原生函数在联机时按 `GS+0x14FF8` 循环（`225290`）。也就是说，读取方本来就期待联机时表里是会话里的玩家，这次补上的是写入方。Coop 在 BVM 一带只改了 `179DE90 → 22BCF0`（Online_GameOverWait 的槽位），和这两处补丁不重叠。

## 验证

- `tests/edf5_online_native_test.cpp`（43 项）用一个假的会话对象覆盖：客人视角（远端在前）和房主视角的手柄号、本机人数 2 时的分屏、创建失败或被标为非本机的玩家不计数、列表与存储正确释放、超过 4 人截断、准入门未装或列表读不到时返回 0。把手柄号或除数改回旧规则的两个变异都会被抓到。`tests/mission_participant_gate_test.cpp` 检查改写后的参数确实传到原函数，以及创建后的回报。原有说明：（`edf5_online_native` / `edf5_online_foreign_build`，需要 `EDF6_NATIVE_DLL`）：在私有映射的真实 EDF.dll 上检查原版字节是空壳；安装后 native 0x10 指向 `1B8CC0`，`22B36C` 的 17 字节内容正确、跳转目标是 `22B3B0`；人数取会话人数而不是本地人数、超过 4 截断并只记一次日志；任一处原字节不符时完全不写。改成读本地人数的变异会被测试抓到。
- **未实机验证**：需要两台机器联机打一关 EDF5 包，确认两边都出现玩家并能结算。
