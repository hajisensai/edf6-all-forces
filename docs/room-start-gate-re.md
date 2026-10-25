# 联机房开打（出撃）链与房主扣住开打的挂点（静态逆向，2026-10-25）

EDF.dll TimeDateStamp 0x678CCB46，RVA（基址 0x180000000）。【证实】= 反汇编 / 数据直接读出；【推断】= 未逐条验证。**未实测，未开游戏。**

## 0. 结论先行

- 开打不是房主单方面按一个“开始”按钮，而是一个**全员就绪握手**（房间按钮 `Request_ReadyWait`，所有成员都按）。握手对象在 HUiRoom `+0x9D0`，每帧由房间 OnUpdate 驱动 `0x73D960`。【证实】
- 房主那边唯一一个“提交并通知客人”的点：所有成员都已就绪时，房主把成员 id 列表写进**自己的 EOS lobby 成员属性 `l1` / `l2`**（`0x73E808` / `0x73E81E`），同时把握手锁死（`+4 = 0`，之后不可取消）。客人只靠轮询房主的 `l2` 得知开打。【证实】
- **推荐挂点**：`0x73E3E9  call 0x12BE580`（IsLocalHost(lobby)），返回值唯一用途是 `0x73E648` 的“我是房主才发布列表”判断。把这个 call 重定向到插件函数：`return orig(lobby) && 阵营表已全员确认;`。条件不满足时房主不发布、不锁定，状态原样留在“等待”（state 2，`+4 = 1`），下一帧自动重试；按钮仍可取消；客人那边什么都没收到。【证实（代码路径）/ 推断（实机行为）】

## 1. 按钮与回调

- HUiRoom 构造（`0x8F1xxx..0x8F4xxx`）用 `0x850810(screen, &name, callback)` 按名字登记回调，回调经 `0x811FC0` + `0x8FB930`（弱画面回调）包装。登记表【证实】：

| 名字（UTF-16 字面量） | 处理函数 |
|---|---|
| `Request_Profile` (0x180AD68) | 0x8FC790 |
| `Request_DLC` (0x180AD88) | 0x8FBFA0 |
| **`Request_ReadyWait`** (0x180ADA0) | **0x8FD8F0** |
| `Request_Mission` (0x180ADC8) | 0x8FC2C0 |
| `Request_Equipment` (0x180ADE8) | 0x8FC020 |
| `Request_RoomSetting` (0x180AE10) | 0x8FDC00 |
| `Request_History` (0x180AE38) | 0x8FC1E0 |
| `Request_OptionSetting` (0x180AE58) | 0x8FC540 |
| `Request_Exit` (0x1809D10) | 0x8FC120 |

  没有单独的“出撃 / Start”回调；开打按钮就是 `Request_ReadyWait`（按钮文字是否对房主与客人不同未核）。【证实 / 文字推断】

- `Request_ReadyWait` = `0x8FD8F0(room)`【证实】：
  - `room+0x9D0`（握手 state）≠ 0 → `0x73D2B0(room+0x9D0, 0)`：取消自己的就绪（同一个按钮再按一次 = 取消）。
  - state == 0：`0x787370([0x20B2AC8])` 且 `0xD92B0(GS, 当前任务内容 id)`（本插件 edf5campaign.cpp 在 `0x8FD963` 挂着这次调用）不满足 → `ContentSelect_Warning` 对话框；
    否则 `room+0x919`（有成员缺内容，`0x901100` 写）为 0 → `0x73D400(room+0x9D0)` 开始就绪，并 `0x8F7CF0(room, &2)`；为 1 → `+0x91A = 1`，`0x8F9D50` 列出缺内容的成员。
- `0x8F7CF0(room, const int* mode)`：写 `room+0x944 = mode`，按 mode 对 `room+0x7F0..` 的 8 个按钮调 `0x7EE280 SetEnabled`；mode 0 = 全部可用，mode 1 / 2 = 只留第一个按钮（就绪 / 取消）。【证实（写入）/ 推断（按钮语义）】

## 2. 就绪握手对象（HUiRoom +0x9D0）

构造 `0x73D0D0`【证实】。字段：

| 偏移 | 含义 |
|---|---|
| +0 | state：0 空闲，1 刚开始，2 已发出自己的就绪，3 列表已锁定 / 倒计时，4 开打 |
| +4 | byte，可取消标志：构造 = 1；房主发布列表或客人看见自己在列表里时 = 0（锁定）；`0x73D2B0` 只在它 ≠ 0 时才取消 |
| +8 | 进入 state 2 时记下的 lobby 成员数（之后成员数变化 → 取消） |
| +0x20 | `_Xtime_get_ticks` 时间戳（100ns）；+0x28 float 倒计时毫秒，`0x73D400` 置 1000.0（`[0x1765A80]`） |
| +0x30 | 取消原因 |
| +0x38..+0x70 | `std::function<void(const int& state)>`，impl 指针在 +0x70（= HUiRoom+0xA40）；对象在 HUiRoom+0xA08 |
| +0x78..+0xB0 | `std::function<void(const int& reason)>`，impl 在 +0xB0（= HUiRoom+0xA80）；对象在 HUiRoom+0xA48 |

- 两个 std::function 在 HUiRoom 构造 `0x8F3489..0x8F3532` 装入：state 回调 lambda vtable `0x180B7B0`（Do_call 槽 2 = **`0x904810`**），原因回调 lambda vtable `0x180B7E8`（Do_call = `0x904CB0`）。【证实】
- `0x904810(lambda, const int* state)`【证实】：
  - state == 4：`room+0xAC0 = 6`（开打）、`0x7E3180(0)`（写 `[0x20B39D8]` 并经 `0x119D1B0` 做画面切换【推断】）、`0x7E2EB0(room)` 关掉房间画面。
  - state == 3：`0x8F7CF0(room, &1)`。
- `0x904CB0(lambda, const int* reason)`：先 `0x8F7CF0(room, &0)`（按钮全部恢复），reason 1 / 3 → `Room_DoneCancel_NumCheck` 对话框（`+0x770 == 0` 时），reason 2 → 缺内容提示。reason 0（自己取消）只恢复按钮。【证实】

### 2.1 成员属性（EOS lobby member attribute）

- 写本机属性：`0x747710 → jmp 0x7445F0(ecx 未用, const char* key, const std::string* value)`：取 lobby（`*( [0x20B2AC0]-0x98 + 0xC0 )`）的本机成员 `0x12BCF50`，`0x12C47F0(member+0x50, &out, key, value, 1)`。【证实】
- 读某成员属性：`0x12C50E0(member+0x50, &out, key)`，值字符串在 `*out + 0x28`。【证实】
- 键表 `0x1F75D70..`：`cm ch ca fc l1 l2 l3 fw p0 p1 ss rt ps`；值表 `0x1F75DD8..`：`"" cs c1 ac`。【证实（数据）】
- 握手用到的：`cm`（本机就绪：房主 `c1`、客人 `cs`，取消 = `""`），`l1` / `l2`（房主发布的参战成员 id，逗号分隔），`ca`（开打时本机写成员数，格式 `%d`，0x17EC860）。
- `cm` / `l2` / `l3` 由脚本绑定 `int UserSync_Finally(int)`（0x17EF188，实现 `0x793430 → 0x78FA40`）清空，所以上一局的 `l2` 不会残留到下一轮。【证实（写空串）/ 推断（脚本在任务后调用）】

### 2.2 每帧更新 `0x73D960(ready, bool memberLacksContent)`

调用点：HUiRoom OnUpdate `0x8FE3CB lea rcx,[rdi+0x9D0]; movzx edx, byte [rdi+0x919]; call 0x73D960`（`0x8FE3D9`），UI 线程。【证实】

1. state == 4 → 直接返回；state == 1 → `0x73D5E0`：本机 `cm` = 房主 `c1` / 客人 `cs`（`0x12BE510(lobby, 本机)` 判房主），记 `+8` 成员数，state = 2（回调 2）。成员只有 1 人时 `0x73D400` 立刻走这里。【证实】
2. **所有机器（含房主自己）**：读房主成员（`0x12BCEF0` = owner）的 `l2`（`0x73DABF`），按 `,` 拆开；本机 id 在列表里 → （state 0 时先 `0x73D400`）`+4 = 0`，回调 3（`0x73DD6F..0x73DD8F`）。【证实】
3. 列表非空且 `+4 == 0`：从 `+0x20` 起满 `+0x28`（1000 ms）且 `[lobby+0x78] != 0`（含义未核）→ 本机 `ca = 人数`（`0x73E0CF`），**`0x783CB0([0x20B2AC8], owner, 参战成员表)`**：建游戏会话（`0x7494F0` 造会话对象，`0x7284B0` 装进管理器），`[mgr+0x1640] = 5`，state = 4（`0x73E17D`），回调 4 → 房间 `+0xAC0 = 6` 并关闭画面。`+4 != 0` 时每帧刷新 `+0x20`（`0x73E214`）。【证实（代码）/ 推断（会话语义）】
4. state ≠ 0 时（`0x73E36A`）：`[rsp+0x30] = 0x12BE580(lobby)`（**`0x73E3E9`**，本机是不是房主），遍历 lobby 成员读 `cm`，计数 r13（房主成员要 `c1`，其余非空即算）。然后：
   - `+4 && memberLacksContent` → `0x73D2B0(ready, 2)`；
   - 没有 owner → `0x73D2B0(ready, 1)`；
   - 成员数 ≠ `+8` → `+4` 时 `0x73D2B0(ready, 3)`；
   - **`+4 && r13 == 成员数 && [rsp+0x30]`**（`0x73E632..0x73E64D`）→ 拼成员 id 列表，`0x747710(-1, "l1", 列表)`（`0x73E808`）、`0x747710(-1, "l2", 列表)`（`0x73E81E`），`+4 = 0`，回调 3。【证实】

### 2.3 客人怎么知道开打

只有一个信号：房主自己的 EOS 成员属性 `l2`（`l1` 同值，读的只有 `l2`）。客人在 2. 里看见自己在列表中就锁定，1 秒后自己调 `0x783CB0` 建会话并关房间画面。没有单独的“开始”网络包；在房主写 `l2` 之前，客人只看见各成员的 `cm`（就绪 / 取消）。【证实（读写点）/ 推断（EOS 属性同步细节）】

## 3. 推荐挂点

```
73E3DD  48 85 FF                test rdi, rdi             ; lobby
73E3E0  0F 84 88 05 00 00       je   0x73E96E
73E3E6  48 8B CF                mov  rcx, rdi
73E3E9  E8 92 01 B8 00          call 0x12BE580            ; IsLocalHost(lobby)  ← 重定向这里
73E3EE  88 44 24 30             mov  byte [rsp+0x30], al  ; 只在 0x73E648 用到
...
73E648  80 7C 24 30 00          cmp  byte [rsp+0x30], 0
73E64D  0F 84 81 02 00 00       je   0x73E8D4             ; 不是房主 / 被挡：不发布
```

- 被调方 `0x12BE580`：`bool __fastcall(RoomImpl* lobby)`，只用 rcx；`[lobby+0x160]+0x18` = 本机 User，`0x12AC6F0` 取 `User+0x10` bit 2（房主标志）。签名字节 `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 0F 57 C0 0F 11 44 24 20 48 8B 89 60 01 00 00`。EDF6Coop 的测试也把它叫 IsLocalHost（`multislot/tests/tests.cpp`，只校验、不改）。【证实】
- 校验：call 位点 `E8 92 01 B8 00`（0x73E3EE + 0xB80192 = 0x12BE580）及前 `48 8B CF`、后 `88 44 24 30`；再校 `0x73E648` 的 `80 7C 24 30 00 0F 84`。用插件现有 `RedirectCall(image+0x73E3E9, image+0x12BE580, hook, changed)`（与 mission.cpp 的 MissionSync 挂法相同）。
- 钩子：

```cpp
bool __fastcall ReadyPublishGate(std::uintptr_t lobby) {
    const bool host = reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(image + 0x12BE580)(lobby);
    if(!host) return false;          // 客人：原样
    return PvpTableAckedByAll();     // 房主：阵营表锁定且全员确认才放行
}
```

  扣住时什么都不用重置：返回 false 只是跳过 `l1/l2` 发布、不置 `+4 = 0`、不回调 3；state 停在 2、`+4` 仍是 1，下一帧 `0x73D960` 再走到这里重试。房主再按按钮 = `0x73D2B0(...,0)` 正常取消（回调 0 恢复全部按钮，`cm` 清空）；成员进出 = 原版 reason 3 取消。【证实（代码路径）/ 推断（实机）】
- 只扣房主就够：客人不可能在房主写 `l2` 之前锁定或开打（2. 只认房主的 `l2`）。【证实】

### 3.1 备选（不推荐）

- 不调 `0x73D960`（跳过 `0x8FE3D9`）：成员进出、缺内容等原版取消也一起停掉，房主自己的 `cm` 已发出，状态显示不一致。
- 跳过 `0x747710("l1"/"l2")`：`+4 = 0` 已锁、回调 3 已发，房主再也取消不了且永远等不到自己的 `l2`，卡死。
- 挡 `Request_ReadyWait`（`0x8FD8F0`）：只能挡“按下”那一刻，按下后再失去确认就挡不住；可以和推荐点并用做提示（例如未确认时不让房主按），但不能代替它。注意 `0x8FD963` 已被 edf5campaign.cpp 占用，返回 false 会弹 `ContentSelect_Warning`，不能复用。

## 4. 本机是不是房主

- 房间里用 `0x12BE580(lobby)`，lobby = `*(std::uintptr_t*)(*(std::uintptr_t*)(image+0x20B2AC0) - 0x98 + 0xC0)`（`[0x20B2AC0]` 为空时 base 取 0，原版也这样判空：`0x73E38C..`）。这就是握手自己的判据。【证实】
- **不要**在房间里用 `0x784210`（插件 `OnlineHostOnly` / `online_authority`）：它看的是游戏会话 `mgr+0xD0`，会话在 `0x783CB0` 之前不存在，此时 `0x784210` 走 `0x7842E9 mov al,1`，每台机器都返回“房主”。【证实】
- HUiRoom 本身没有找到房主标志字段（未穷举，`+0x919` / `+0x91A` 是缺内容，`+0x944` 是按钮模式）。【推断】

## 5. 风险

- **锁定后不可撤回**：放行那一帧之后 `+4 = 0`，1 秒后各机自己开打，原版没有取消路径。阵营表必须在放行时冻结，之后任何修改 / 撤回确认都不能再影响这一局。
- 确认集合要按 **当前 lobby 成员列表**（与 `0x73E443` 同一来源：lobby vtable 槽 1）算，包括房主自己；有人进出时原版 reason 3 会先取消，插件的确认也要随成员列表失效。
- 某个成员永远不确认（没装插件 / 版本不符）会把房主一直扣住：房主能用同一按钮取消，但插件要给出提示（例如按钮说明或对话框），不能静默卡住。
- 线程：`0x73D960` 由 HUiRoom OnUpdate 调用（UI 线程），钩子里只读插件的原子 / UI 线程状态，不要阻塞。
- EDF6Coop（`D:\APP\edf6_directnet` origin/main）：房间相关挂点是 HUiRoom OnUpdate 槽 `0x180AC48`、面板 `0x8FBABE` / `0x8FFE45` / `0x8FFBED`、成员列表 `0x901170`（`0x7468C0`，测试模式加假成员）、lobby 成员状态 `0x12BEF00`、`0x788AB3`（另一处 IsLocalHost 调用）。`0x73D960..0x73E9FF`、`0x73E3E9`、`0x12BE580` 本体、`0x7445F0`、`0x783CB0` 都没有被 Coop 改。【证实（grep）】Coop 测试模式的假成员只进房间画面的列表，不进 lobby 列表，握手不受影响。【推断】
- `[lobby+0x78]`（`0x73E091`，开打前最后一个条件）含义未核。
