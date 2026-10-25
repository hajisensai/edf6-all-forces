# 联机房成员面板（ui::HUiRoom / PlayersGroup）：不分页、N 行自动堆叠的静态逆向与方案

EDF.dll Steam 版 TimeDateStamp `0x678CCB46`（`bytes.py` 读 PE 头核对过），地址为 RVA。只做了静态反汇编和 Root.cpk 数据读取，**没有开游戏，没有实机验证**。
【证实】= 从反汇编 / 数据直接读出来的；【推断】= 没逐条核到底，需要实机或进一步逆向确认。

依据：`edf.asm`（整份反汇编）、Root.cpk `UI/LYT_HUIONLINEROOM.SGO`（用 pylib/sgo.py 导成 `scratchpad/room_lyt.json`）、EDF6Coop `origin/main` 的
`multislot/src/roomview.{h,cpp}`、`patches.cpp`（`RoomViewCalls`）、本仓库 `docs/room-ui-re.md`、`docs/room-native-menu-re.md`、`tools/room_layout.py`。

---

## 0. 结论

1. **「四项模板表」不是数据段里的表，是构建函数 `0x8F8020` 自己栈上的 4 个指针** `[rbp+0xA0..0xB8] = {L"Button_Master", L"Button_Member" ×3}`；循环 `i < members.size` 每轮取 `[rbx]` 再 `rbx += 8`。到第 5 个成员时读到的是 `[rbp+0xC0]`（栈 cookie），往后是保存的寄存器 / 返回地址，被当成 `wchar_t*` 传给 `0x845BB0`。【证实】
2. 面板是用 **`0x845BB0(HUiLayout* screen, const wchar_t* name)`** 按名字从 `layout_tree` 里实例化的：在树里找到 `Button_Master` / `Button_Member` 节点和它的父节点 `PlayersGroup`，把整棵子树（`_pl_*`、语音图标）挂到 PlayersGroup 下面。【证实】
   **位置全部来自布局数据**：模板 `flags [8]`（接在前一个兄弟下边），PlayersGroup `flags [256,2048]`（纵向列表）。Master 高 210、Member 高 188，四个加起来 774，刚好塞进 PlayersGroup 的 780。第 5 个起会继续往下排，排出组的范围。【证实（数据）】
3. **除了这张栈表，构建 / 填充 / 语音图标 / 焦点这条链上找不到其他「固定 4 个」的地方**：成员是 vector（+0x900），面板弱引用表是会扩容的 vector（+0xB50），填充和语音图标按名字表查出来的面板数量循环，语音图标还额外用成员数做了上界检查。只要面板数 = 成员数，N 个都安全。【证实（逐个函数读过，见 §3）】
4. 所以「不分页」只要改两条指令（11 字节，都在 `0x8F8020` 里面，不需要 cave）：第 1 个用 Master，第 2 个起一直用 Member，对任意 N 都不越界（§4.1）。
5. 每个组件的局部 4×4 矩阵在 **+0x170..+0x1AF**（平移在 +0x1A0/+0x1A4，缩放在对角线 +0x170/+0x184），area 在 **+0x1B0..+0x1BC**。绘制函数 `0x7EC790` 每帧都重新读这个矩阵；游戏自己运行时改按钮大小用的是 `0x7E7FA0 HUiButton::SetArea`。
   所以构建完以后用代码改每行的平移 / 缩放 / area，再隐藏或挪动子元素，就能做出紧凑行。不需要改布局文件。【证实（读取方式与 setter）/ 推断（子元素跟着父矩阵一起变换）】
6. 阵营色块：最稳的做法是在每个面板下面用 `0x8456D0 CreateByName` 新建一个原版定义里已有的 TextField（`_pl_Name`），写字、写字色 +0x2B0。代码里没有任何地方按字符串查 `_pl_*`（.rdata 里没有这些宽字符串），所以多出来一个同名元素不会被原版逻辑误用。【证实（无引用）/ 推断（新建元素的序号与位置，需要核）】
   要实心色块就得往布局里加定义，而 `Mods/UI/LYT_HUIONLINEROOM.SGO` 归 EDF6VehicleCrew（`tools/room_layout.py`，账本 owner `roomui`）。Coop **不能另外装一份**，只能和本仓库协调，在 room_layout.py 里加一条不进 layout_tree 的定义（像 `vcLoad` 那样）。

---

## 1. 面板构建函数 `0x8F8020`（HUiRoom，参数 rcx = room）

调用点（与 Coop `RoomViewCalls` 一致）【证实】：
- `0x8FBABE`（`e8 5d c5 ff ff`）：重建，前面是 `0x8F7CF0` 重建成员 vector；
- `0x8FFE45`（`e8 d6 81 ff ff`）：刷新，条件是 `test byte [room+0x7A8], 1`；
- 全文只有这两处调用 `0x8F8020`。

流程【证实，除非另外标注】：
| 地址 | 内容 |
|---|---|
| 8F805B–8F807E | 栈表：`[rbp+0xA0]=0x180B428 L"Button_Master"`，`[rbp+0xA8]=[rbp+0xB0]=[rbp+0xB8]=0x180B448 L"Button_Member"` |
| 8F80A4–8F80C6 | `0x839FD0(room, &out, L"PlayersGroup"(0x180AE88))`，再 `__RTDynamicCast` 成 HUiLayout 类型（0x2062F00→0x20637C8），结果存 `[rsp+0x50]` |
| 8F81AC–8F81D4 | `focusIdx = min(members.size-1, PlayersGroup.focusList.cur)`（`0x7F0120` 返回 `[comp+0x228+0x20]`） |
| 8F81EC–8F83A9 | room+0x7B0/+0x7B8 是 PlayersGroup 的弱引用（构造时 8F36C0 写入，名字查的是 `PlayersGroup`），用来判断它的子项当前有没有焦点（`0x7EBDB0`），有的话记下被聚焦面板绑定的成员（面板 +0x200） |
| 8F83E7–8F83F8 | 调 PlayersGroup 虚表槽 1 (r8=0, rdx=0)，再 `0x7EC370`（提交待处理的子项增删），即清空旧面板【推断：槽 1 是 RemoveAllChildren】 |
| **8F83FD–8F842B** | **循环 `for (i=0; i<[room+0x910]; ++i) 0x845BB0(room, table[i])`，rbx 从 `rbp+0xA0` 开始每轮 +8** |
| 8F8443–8F8742 | 复制 PlayersGroup 的子项链表（+0x158），把每个面板的弱引用压进 `room+0xB50` 的 vector（data +0xB58，capacity +0xB60，size +0xB68，计数 int +0xB78）。会按 `cap*2+2` 扩容，不是定长 |
| 8F8765 | `0x7EB0A0(PlayersGroup, 1, 0.0f, true)`：刷新内容尺寸 / 滚动条【推断】 |
| 8F876D | **`0x902030(room)`：把成员填进面板** |
| 8F87A9 | `0x8FAF70(room, &out, &prevMember)`：在 PlayersGroup 的焦点表里找绑定着原先那个成员的面板，再 `0x7EE1C0` 把焦点给它；找不到就退回 focusIdx |

HUiRoom 成员 vector：`+0x900 data`（每项 16 字节 = shared_ptr<MemberInfo>），`+0x908 capacity`，`+0x910 size`。构造时预留 4 个（8F25F4 / 8F272E），会扩容。Coop 注释里写的 `{data; capacity; size}` 是对的。【证实】

### 1.1 实例化 `0x845BB0(screen, const wchar_t* name)`【证实】
- 在 `screen+0x70`（布局 sgo）里找 `L"layout_tree"`（0x17FDC88），用 `0x84C420` 在树中按名字找到节点，同时拿到父节点名和它在父节点下的序号；
- `0x839FD0(screen, &parent, 父节点名)` 取到父组件（这里是 PlayersGroup），把父节点现有子项的末尾作为前一个兄弟；
- `0x8436E0(screen, node, 序号, &parent, &prevSibling)` 用 sgo 工厂建出整棵子树。
- 另一个入口 `0x8456D0 CreateByName(screen, Shared* out, Shared* parentByValue(会被消费), const wchar_t* member)`：不经过 layout_tree，按顶层定义名建一个元素，挂到任意父组件下（本仓库 vcLoad 按钮就是这样建的，见 docs/room-native-menu-re.md）。

### 1.2 布局数据（Root.cpk `UI/LYT_HUIONLINEROOM.SGO`）【证实（数据）】
```
PlayersGroup     Layout Transparent  pos(1283,160) area 506x780  flags[256,2048]
ButtonMasterBase Button Window10_RoomMember_skin  area 455x210  flags[8]  font 24
ButtonMemberBase Button Window10_RoomMember_skin  area 455x188  flags[8]
Button_Master/Member = {继承 *Base, callback Request_Profile}
layout_tree: PlayersGroup → [Button_Master{…}, Button_Member{…}]   ← 树里只各有 1 个，靠 0x845BB0 反复实例化
```
面板子元素（序号是 layout_tree 里的子项顺序，运行时写在子组件 +0x110）：
| 序号 | 名字 | pos / area | 说明 |
|---|---|---|---|
| 0 | _pl_SoldierType | (11,9) 149x35 | TextField 16 号字 |
| 1 | _pl_Name | (166,9) 250x35 | TextField 16 |
| 2 | _pl_ArmorDesc | (332,53) 106x19 | `text Room_ArmorTitle` |
| 3 | _pl_Armor | (332,85) 117x23 | TextField 20 |
| 4 | _pl_ClearRatioDesc | (332,122) | `text Room_ClearRate` |
| 5 | _pl_ClearRatio | (332,151) 119x23 | |
| 6–11 | _pl_Weapon0..5 | (9,51)… 316x21 | 每个下面有 WeaponName / WeaponLevel |
| 12 | _pl_Location | (92.5,58) 270x112 | RoomMemberStatus_skin（成员当前在哪个画面 / 是否就绪） |
| 13 | _pl_RoomMaster（只有 Master 有） | (420,-20) 48x30 | 房主标记 |
| 13–16 / 14–17 | VoiceChatIcon_On/Off/Mute/Restrict | (417,9) 36x36 | |

flags 的含义见 room-ui-re §5：8 = 接在前一兄弟下边【证实】；0x100 = 纵向列表控制器【推断】；0x800 疑似裁剪【推断】。

### 1.3 填充 `0x902030(room)`【证实】
- `0x839830(screen, &vec<int>, L"Button_Master")` 和 `(…, L"Button_Member")` 在画面的「名字→下标」multimap（screen+0x470，equal_range）里查出所有同名面板，按 Master、Member 的顺序拼起来；
- 对第 i 个面板：`0x839560` 取到组件，**面板 +0x200/+0x208 = members[i]**（shared_ptr；这一步**不检查 i 是否小于成员数**，靠面板数 = 成员数保证）；
- `0x8F4B60(&view, panel)` 按序号（`0x8E1950` 在子项链表 +0x158/+0x138 里匹配 +0x110）取出 {0 兵种, 1 名字, 3 护甲, 5 通关率, 12 位置(`0x8F1610`), 6..11 武器各两个字段}；
- `0x8F7690(&view, MemberInfo*, room+0xB80)` 写字。都是 `0x7F8BB0(TextField*, text, nullptr)`：兵种名 `0x959170(class)`，名字 MemberInfo+0x18，护甲 / 通关率 / 武器（`0xE3680` 取武器数，`0xE1CF0` 查武器表）。**整个填充过程不写 +0x1F8（显隐）**。【证实】

### 1.4 语音图标 `0x903160(room)`（调用点 `0x8FFBED`，每帧）【证实】
- 查 6 个名字的下标表：Button_Master、Button_Member、VoiceChatIcon_On/Off/Mute/Restrict（0x180B030/058/080/0A8）；
- 按面板数循环，**`cmp rsi, [room+0x910]; jae`（0x903995）用成员数做上界**，把 4 个图标 +0x1F8 全部置 0，再按语音状态把其中一个置 1。
- 面板数 = 成员数时天然一一对应，不需要 Coop 现在这个分页钩子。

### 1.5 其他会看成员数 / 面板数的地方（都没有固定 4）【证实】
- `0x8FAF70`：`for i < [room+0x910]` 读 `[PlayersGroup+0x230][i]`（焦点表，16 字节一项），**不和焦点表自身的 size (+0x240) 比**。它只在构建里（8F87A9）、面板刚建完时被调用，所以面板数 = 成员数时安全；如果面板比成员少（比如分页时的 4 个面板对 8 个成员），就是越界读。Coop 现在的做法是把成员 vector 临时换成那一页的成员，所以没出事。
- `0x8FEA01`（OnUpdate）：按 `room+0xB68` 遍历面板弱引用表做鼠标判定（`0x968220`），动态长度。
- `0x8FE965/8FE98C` 的 `cmp r8d,4` 是本地玩家槽（stride 0xDD0），和房间成员无关。
- 名字表里的旧面板：组件被移除时 `0x84E6A0 → 0x83A660` 注销【推断】；原版每次重建都靠 `0x839830` 的计数等于当前面板数，说明旧条目确实被清掉了。

---

## 2. 组件变换与 setter

`ui::Component` 构造 `0x7E9790`【证实】：`+0x170..+0x1AF` 是 4×4 局部矩阵，构造时设为单位阵（0x170 / 0x184 / 0x198 / 0x1AC = 1.0）；`+0x1B0..+0x1BC` 是 area {x, y, w, h}；`+0x1F8` 显隐（=1）。
`Component::Init 0x7EBC30(comp, InitParam*)`【证实】：InitParam 的 `+0x50..+0x8F` 拷到矩阵 `+0x170..+0x1AF`，`+0x90` 拷到 area `+0x1B0`，`+0xA0` 拷到序号 `+0x110`，`+0xC8/+0xCC` 拷到 hcoord / vcoord `+0x1F0/+0x1F4`。
布局解析 `0x84C7A0`【证实】：`pos` 写进 InitParam `+0x80..+0x88`，也就是矩阵的平移行（再由 `0x840430` 按 flags 4/8/0x10/0x20 根据前一个兄弟算出相对位置）；`area` 写进 `+0x90`；缩放对角线 `+0x50/+0x64/+0x78 = 1`。
→ **flags 8 的「接在前一个下边」只在创建时算一次，结果就是矩阵里的平移。**【证实（创建路径）/ 推断（没找到每帧重排；0x100 列表控制器有没有随焦点滚动没逐条核）】

读取方：
- 绘制 `0x7EC790(comp, const DrawCtx* parent, …)`：开头检查 +0x1F8，**每次调用都重新读 `+0x170..+0x1A0` 和 area**（0x7EC9A9–0x7EC9D3、0x7ECA06–0x7ECA68），再递归画子项（0x7EDEED / 0x7EDF35）。【证实】子项矩阵乘上父矩阵【推断（强）】。
- 命中 / 范围计算 `0x7EB6xx–0x7EBBxx` 用 `矩阵 × area`，所以鼠标判定跟着移动。【证实（读取）】

setter：
- `0x7E7FA0 HUiButton::SetArea(Button*, const float4* area)`：写 +0x1B0，再把按钮内部的 TextField（+0x220/+0x228 弱引用）按边距 `0x7EB770` 缩放。游戏自己在 `0x80F588` 运行时用它改按钮宽度（先把 +0x1B0 读出来，改 w，再写回）。【证实】面板是 Button（ButtonMasterBase 的类型是 "Button"），**改面板的 area 要用它**。
- 平移 / 缩放：没有独立的 setter（没找到写 +0x1A0 的通用函数，只有 Init 一次性拷贝），直接写 `+0x1A0/+0x1A4`（x, y）和 `+0x170/+0x184`（sx, sy）。【证实（没有 setter）/ 推断（直接写是安全的：绘制每帧都读）】
- TextField 字色 `+0x2B0..+0x2BC`（RGBA float），每帧乘进去。这一条已在本仓库 room_ui.cpp 实际用过。【证实（room-ui-re §7）】
- 本仓库插件**从来不用代码改位置**：room_ui.cpp / room_layout.py 都是改布局数据，只用 +0x1F8、+0x2B0、贴图、文字。所以「用代码改矩阵」在本项目里没有先例，需要实机验证。

---

## 3. 「>4 个成员是否安全」逐项清单

| 位置 | 是否固定 4 | 面板数 = 成员数 = N 时 |
|---|---|---|
| 8F8020 栈表 `[rbp+0xA0]` | **是（唯一的固定 4）** | 越界 → §4.1 的补丁修掉 |
| 成员 vector +0x900 | 否（会扩容） | 安全 |
| 面板弱引用表 +0xB50 | 否（cap*2+2） | 安全 |
| 0x902030 填充 | 按面板数循环，没有上界检查 | 安全（两边相等） |
| 0x903160 语音图标 | 按面板数循环，上界是成员数 | 安全 |
| 0x8FAF70 焦点恢复 | 按成员数索引焦点表，不检查表长 | 安全（两边相等）；面板少于成员时才会越界 |
| PlayersGroup 区域 780px | 布局数据 | 显示不下 → §4.3 用代码重新排 |
| UiVoiceChat_Notify / eos Users 等 | 4，Coop 已加宽 | 与本题无关 |

---

## 4. EDF6Coop 实现方案

### 4.1 字节补丁：去掉 4 项表（替代分页）
两处都在 `0x8F8020` 里面，原地改写，不需要 cave、不加跳转：

| RVA | 原字节（签名） | 原指令 | 新字节 | 新指令 |
|---|---|---|---|---|
| `0x8F807E` | `48 89 85 B8 00 00 00` | `mov [rbp+0xB8], rax`（rax = L"Button_Member"） | `48 89 45 28 0F 1F 00` | `mov [rbp+0x28], rax ; nop3` |
| `0x8F841D` | `48 8D 5B 08` | `lea rbx, [rbx+8]` | `48 8D 5D 28` | `lea rbx, [rbp+0x28]` |

签名上下文（安装前逐字节校验）：
```
8F8069  48 8D 05 D8 33 F1 00   lea rax,[0x180B448]
8F8070  48 89 85 A8 00 00 00   mov [rbp+0xA8],rax
8F8077  48 89 85 B0 00 00 00   mov [rbp+0xB0],rax
8F807E  48 89 85 B8 00 00 00   mov [rbp+0xB8],rax        ← 补丁 1
8F8408  48 8D 9D A0 00 00 00   lea rbx,[rbp+0xA0]
8F840F  90
8F8410  48 8B 13               mov rdx,[rbx]
8F8413  49 8B CD               mov rcx,r13
8F8416  E8 95 D7 F4 FF         call 0x845BB0
8F841B  FF C7                  inc edi
8F841D  48 8D 5B 08            lea rbx,[rbx+8]           ← 补丁 2
8F8421  48 63 C7               movsxd rax,edi
8F8424  49 3B 85 10 09 00 00   cmp rax,[r13+0x910]
8F842B  72 E3                  jb 0x8F8410
```
效果：第 0 轮读 `[rbp+0xA0]`（Master），之后每轮读 `[rbp+0x28]`（Member），对任意 N 都没有越界读。【证实（编码用 capstone 反汇编核过）】
`rbp+0x28` 在这个函数里从头到尾都没被用过（统计了 0x8F8020–0x8F8A33 所有 rbp 偏移：+0x20 只当 dword 用，+0x28 没出现），而且在 8 字节对齐的帧内（帧顶 rbp+0xD0）。【证实】
`[rbp+0xB8]` 改完后不再写入，也不再被读（循环只读 +0xA0 和 +0x28）。【证实】

两个补丁属于 Coop（它已经接管了房间画面这几处调用）。EDF6VehicleCrew **不要**改这两处；如果另一个插件已经改过，按签名校验就会拒绝。

### 4.2 roomview 里改什么
- 删掉：`PageSlice` / `SliceForPage` / `PageCount` / `NextPage`、翻页按键 / 手柄 / `pageHint`、`CallWithMembers`（临时替换成员 vector）、`UpdateVoiceIconsHook`（§1.4：面板与成员一一对应后不需要；`RoomViewCalls` 去掉 `0x8FFBED` 那一项）。
- 保留：`0x8FBABE` / `0x8FFE45` 两处调用改到 `BuildPanelsHook`，但改成 **先调原函数（N 个面板），再 `ApplyRowLayout(room)`**；`RoomOnUpdateHook`（假成员按键、名单日志、`kRebuild=0x940` 的重建请求）照旧。
- 成员上界：`members.size > kMaxPlayers` 或 data 为空时不排版（与现在的保护一样）。
- `RoomPageView` / 菜单标签改成只报 `shown`。

### 4.3 N 行排版（`ApplyRowLayout`，在构建返回后、UI 线程上）
取面板：锁 `room+0x7B0/+0x7B8`（PlayersGroup 弱引用）→ 遍历它的子项链表 `+0x158`（节点 +0x10 是对象，+0x18 是控制块），顺序就是创建顺序 = 成员顺序；也可以用 `room+0xB58[i]`（16 字节弱引用，数量在 +0xB68）。【证实】
`N ≤ 4` 时完全不动（保持原版）。

```
H = 780, W = 506 (PlayersGroup area), PW = 455 (面板设计宽度), gap = 4
rowsFit(h) = floor((H + gap) / (h + gap))
if N <= 4: return
h = floor((H - gap*(N-1)) / N)
if h >= 92:      mode = TwoLine; rowH = min(h, 120); cols = 1; s = 1      // N = 5..8
elif h >= 40:    mode = OneLine; rowH = min(h, 60);  cols = 1; s = 1      // N = 9..19
else:            mode = OneLine; cols = ceil(N / rowsFit(40)); s = (W - 6*(cols-1)) / cols / PW
                 rowH = floor((H - gap*(ceil(N/cols)-1)) / ceil(N/cols)) / s   // 局部单位，缩放前
for i, panel:
    col = i / rowsPerCol, row = i % rowsPerCol
    panel+0x170 = s, panel+0x184 = s                       // 只有多列时 s<1
    panel+0x1A0 = col * (PW*s + 6),  panel+0x1A4 = row * (rowH*s + gap)
    0x7E7FA0(panel, {0, 0, PW, rowH})                     // area 用 HUiButton::SetArea
    按 +0x110 序号处理子项（遍历 panel+0x158 和 +0x138）：
      OneLine: 隐藏 2,4,5,6..11,12(+0x1F8=0)；0 兵种 → (8, (rowH-35)/2) 宽 104；1 名字 → (116, …) 宽 176；
               3 护甲 → (296, …)；语音图标 → (417, (rowH-36)/2)；房主标记(Master 13) → (420, -10)
      TwoLine: 第一行同原版（兵种 / 名字 / 语音），隐藏 2,4,5,6..11；3 护甲 → (11, 47)；
               12 位置 → 缩放 0.5 放在 (300, 40)
```
- 子元素用 TextField 时改宽度：TextField 有没有自己的 SetArea 没核；先只写 +0x1B8（宽）【推断，需验证文字是否裁剪 / 换行】。字号在布局里，代码改不了的话就用矩阵缩放 TextField。
- 隐藏的子项不会被原版重新显示：填充不写 +0x1F8，只有语音图标每帧被改，而它们不在隐藏名单里。【证实】
- 每次重建都会重新创建面板，所以必须每次在构建后重新排（两处调用点都经过 `BuildPanelsHook`，覆盖了）。【证实】
- 超过 3 列（大约 N > 51）就看不清了：要么限制最大列数，让焦点列表自然滚动（需要给 PlayersGroup 加 scroll_bar_info，属于布局改动），要么接受缩得更小。这个上限交给用户决定。
- 焦点 / 上下键：PlayersGroup 的纵向列表控制器按子项顺序移动焦点，与 N 无关【推断】；多列时左右键不会跨列【推断】。

### 4.4 阵营色块与名字（以后加）
方案 A（不改布局文件，推荐先做）：
1. 构建后，对每个面板：`uses+1` 复制一份面板的 shared_ptr，调 `0x8456D0(room, &out, &copy, L"_pl_Name")`（复制的那份会被消费），得到一个新的 TextField（Transparent 皮肤、`text_dr ''`、16 号字），挂在这个面板下面。它随面板一起销毁，生命周期完全由原版管理。
2. 用 `0x7F8BB0(tf, L"■ 红队", nullptr)` 写字（和原版填充用的是同一个函数），`tf+0x2B0` 写阵营色 RGBA；位置写 `+0x1A0/+0x1A4`（OneLine 放在 x≈356..412，即护甲和语音图标之间）。
3. 阵营从哪里来：Coop 已有成员属性接口（`EDF6CoopGetMemberAttributeApi`）；按面板 +0x200 绑定的 MemberInfo 对应到成员，读阵营属性。
- 证据 / 风险：.rdata 里没有任何 `_pl_*` 宽字符串，原版只按序号 +0x110 找子项【证实】。`0x8456D0` 给新元素的 +0x110 是多少还没核【推断】：如果恰好等于某个原版序号，`0x8E1950` 取的是链表里第一个匹配的，原版子项排在前面，所以仍会取到原版的那个【推断】。实现时应当记住新元素的指针，不要按序号去找。「■」字形字体里有没有也没核（没有就退回「[红]」这样的文字）。
方案 B（实心色块）：需要一个 HUDIconTexture_skin 的 Layout 定义，配合本仓库的白贴图 + Skin_Texture 染色（room-ui-re §7/§8）。这要在本仓库 `tools/room_layout.py` 里加一条不进 layout_tree 的定义，因为 LYT_HUIONLINEROOM 的覆盖文件归 EDF6VehicleCrew。Coop 的代码要在定义不存在时不显示色块（`0x8456D0` 名字缺失时会消费 parent 并返回空）。

---

## 5. 待验证（实机 / 进一步逆向）
1. 改面板矩阵后，子项（文字、皮肤 9 宫格、语音图标）是否跟着平移 / 缩放；Window10_RoomMember_skin 在 area 高 40–120 时显示是否正常。
2. PlayersGroup 的 0x100 列表控制器 / 0x800 标志会不会每帧重排或裁剪子项（会的话，平移要在每帧 OnUpdate 后写，或者改用别的方式）。
3. `0x8456D0` 新建元素的 +0x110 序号和初始位置；TextField 改宽度的正确 setter。
4. 焦点高亮（focus_color_mode 1）与鼠标命中在缩放后是否对齐。
5. 5–8 个真人房间里 0x8FAF70 的焦点恢复与语音图标（静态上判断是安全的）。
