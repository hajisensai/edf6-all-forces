# 房间 / HQ 配装菜单改用游戏自带对话框（HUiDialogBox）

EDF.dll TimeDateStamp `0x678CCB46`，RVA；2026-10-19。证据为本机原版二进制静态反汇编与 Root.cpk 布局读取，**未启动游戏**。

## 为什么换

用户原话（2026-10-19）：「房间界面f2太垃圾了，换成游戏自带的那种。」此前的配装页是插件往 `LYT_HUIONLINEROOM` / `LYT_HUIHQMAIN` 里加的 TextField、白贴图染色面板和色块，由插件自己读方向键 / Ins / Del / Home 驱动：外观是自绘的，不能用鼠标，按键与原版菜单不同。

调研结论：游戏里「选一项」的原生窗口是 `HUiDialogBox`（`APP:/UI/LYT_HUIDIALOGBOX.SGO`：`Window_Test01_skin` 框体、`HUiMessageBoxDialogText` 说明文字、`HUiMessageBoxDialogButtonGrp` 按钮组、`_HUiButton` = `Button05_skin` 按钮、`default_cancel_action` = `set_exit_code 255`）。士兵画面的「更换士兵模型」弹窗（本仓库 4.1 外观已在其中追加过一项，10-18 截图已见）就是它。它的焦点移动、鼠标点选、确认 / 取消键、音效与开合动画全部由游戏处理。所以菜单整体改为一串这种对话框；插件只决定每页的说明文字和按钮，以及按钮按下后的效果（沿用 `room_menu.h` 的同一数据模型、同一存盘与生成链）。

自绘覆盖层仍在两份布局里：关闭状态下的入口提示照旧；对话框代码签名不符时（`NativeDialogReady` 为假）整个菜单退回原覆盖层，不会无菜单。

## 构造方式（逐条照抄 `0x8AB310` 的弹窗构造）

| 步骤 | 原版位置 | 内容 |
|---|---|---|
| 参数对象 | `0x8AB5A9` | 栈上 `HUiDialogBox::InitParam`（RTTI `.?AUInitParam@HUiDialogBox@ui@@`），基类构造 `0x8C320`，再写 vtable `0x17621F8` |
| 字段初值 | `0x8AB5B5..0x8AB636` | `+0xE8` / `+0x108` 空 wstring（size 0 / cap 7），`+0x128` 0，`+0x130` 取 `0x1765B30` 的 16 字节，`+0x140` 0，`+0x148` -1，`+0x14C` 0，`+0x150` 条目向量 `{?, data +0x158, capacity +0x160, size +0x168}` 空 |
| 条目 | `0x8AB669..0x8AB6F0` | 0x90 字节：`+0` wstring 文字（`0x3D450` assign），`+0x20` `std::function<void(ctx*)>`（impl 指针 `+0x58`，由 lambda 的 `_Copy` 放进内部存储），`+0x60` = 1，`+0x80` 空红黑树（头节点 0x60 字节由游戏 `operator new 0x12D85B0` 分配，三指针指自身，`+0x18` = 0x101），`+0x88` 0 |
| 入向量 | `0x8ABAE0` | 满了 `reserve(cap*2+2)`（`0xB91D0`），先加 size，再 `0x89950(vec, dst, src)` 拷贝构造 |
| 临时条目析构 | `0x8ABC4D` | `0x8E170`（树、std::function、wstring） |
| 类型 / 光标 | `0x8ABB49`、`0x8ABB91` | `+0x128` = 6（与外观弹窗同），`+0x148` 初始焦点，`+0x12C` = 1 |
| 说明文字 | `0x8ABBBE` | `+0x108`（原版是 `ModelIndexChgDlgMsg` 的文字） |
| 打开 | `0x8ABBE8..0x8ABC10` | `0x119C600([0x20B2960] - 0x88, &outWeak, [0x1F77810] = L"APP:/UI/LYT_HUIDIALOGBOX.SGO", &param)`；`out` 是 **weak_ptr**（原版随后只减 `+0xC` weaks） |
| 销毁参数 | `0x8ABC6C`、`0x8ABD38` | `0xA9460(param+0x150)`，两个 wstring，`0x8E540(param)` 基类析构 |

`0x119C600` 共 96 处调用，联机房间自己（`0x8FD6AD`）也用同一函数开同一个对话框（它把 `out` 存进 `+0x7D0` 列表，每帧 `0x900F70` 锁 weak，锁不到即视为已关闭并移除），说明「weak 失效 = 对话框已关闭并释放」是游戏自身的用法。

## 按钮回调

原版 lambda（vtable `0x1805CF0`）实现 = `{vtable, 8 字节捕获}`；`_Copy 0x8AD410` 只拷这 16 字节；`_Do_call 0x8AD560` 的 `rdx` 指向参数，`[rdx]` 交给 `0x7E2EB0` 关闭对话框（`0x8AD5AD`）。插件的 `_Func_impl` 布局相同：`{&ourVtable[1], id}`，`_Copy` / `_Move` / `_Do_call` 为插件函数，`_Target_type` / `_Delete_this` / `_Get` 用原版 lambda 的（捕获同为 8 字节、平凡析构；对象总在 std::function 内部存储，`_Delete_this` 的释放分支不会走到）。按下：记下 id（低 40 位按钮 id、高 24 位对话框序号，过期对话框的点选不被采纳），再与原版一样 `0x7E2EB0([arg])` 关闭。

## 帧流程（`room_ui.cpp DriveDialog`）

1. F2 / L3+R3 或原生菜单按钮：载入 ini（同以前 `Opened`），从单位页开始。
2. 每帧：对话框活着 → 只取点选；weak 失效（游戏已关闭并释放）→ 有点选则 `Apply`，否则视为游戏自带的取消（Esc / B / 右键）`Cancel` = 返回上一页；单位页取消 = 关闭（有未保存修改先问「放弃 / 保存 / 继续编辑」）。
3. 画面重新成为输入拥有者（`GetInput 0x8DAD00` 非空）后打开下一页。菜单期间画面本身的输入照旧被挡（ctx+8 置空），3D 预览跟随当前编辑的士兵 / 乘员。
4. 画面替换 / 析构、任务开始、`Enabled` / `RoomLoadout` 关闭：`CloseDialog` 锁 weak（`0x83E4F0`）后 `0x7E2EB0` 关掉还开着的对话框，它的点选不再被采纳。何时关见下节「关闭时机」。
5. 打开失败（游戏未返回对话框）：菜单关闭并记日志，不留不可见的菜单状态。

页面（`room_dialog_logic.h`）：单位列表 → 单位（士兵 / 主炮 / 挂点 / 乘员颜色，添加、恢复原配）→ 士兵（兵种、主色、副色、背包武器、在后复制、删除）/ 挂点（挂载、弹数、删除）/ 乘员（主色、副色）→ 取值页。每页最多 10 个条目，超出加「上一页 / 下一页」；取值页打开在当前值所在页、焦点在当前值；每页最后一个按钮「返回」（单位页「关闭」）。

## 关闭时机（2026-10-20，#218 审查）

对话框挂在游戏的全局 UI 上（`0x119C600` 的 system = `[0x20B2960] - 0x88`），不是房间 / HQ 画面的子对象：画面析构或不再更新，它照样留在屏幕上。原先只在下一个画面帧（`ForgetMenu`）关，画面析构钩子（`NoteGone`）与任务开始（`ResetRoomUi`）都只打标记，所以房主开任务时客人开着的菜单会被带进读盘。现在：

- **画面析构**：`RoomGoneHook` / `HqGoneHook` → `NoteGone` 在 UI 线程（记录在画面帧里的线程号；游戏在 UI 线程析构画面）上直接 `CloseDialog`。不在 UI 线程时只打标记（菜单状态是 UI 线程的），由下面的对话框自身帧兜底。
- **任务开始**：`ResetRoomUi`（`MissionStart` 的 preload，以及新增的 `MissionSync_Begin` 钩子，见 `docs/loadout-re.md` §3.5）调 `RequestCloseDialogs()`（任何线程，原子量）：记下「到目前为止打开的对话框」。`HUiDialogBox::OnUpdate`（vtable `0x17F7260`，RTTI `.?AVHUiDialogBox@ui@@`，slot 1 = `0x810D10`，与各 HUi 画面同布局）被链上：对话框自己的帧里，若它是插件的、且在请求之前打开，就在原帧之后 `0x7E2EB0(self)` 关掉（与原版按钮在帧内关闭同一做法），之后打开的不受影响。它只要对话框还在屏幕上就会被更新，不依赖房间画面是否还在跑。（推测：读盘期间全局 UI 仍逐帧更新对话框；比较用的 `weak.object` 就是对话框对象本身，与原版 lambda 交给 `0x7E2EB0` 的是同一种指针；**未实机**。）链不上时日志说明，退回只靠画面帧 / 析构钩子。
- **打开时出错**：`OpenDialog` 的 `__except` 若发生在 `0x119C600` 已返回 weak 之后（拆参数时出错），先 `CloseNow` 关掉已显示的对话框，再释放 weak（控制块 `+0xC` 减一），不再泄漏引用、也不留一个没人驱动的对话框。
- **点选按对话框存放**：点选结果原来是全局单例（`picked` / `hasPicked`），现在放进各自的 `Handle`（`picked` / `hasPicked`）。打开时登记 Handle 的地址（最多 8 个），按钮 id 高 24 位的序号找到它所属的 Handle；关闭 / 遗忘时注销。被「请求关闭」关掉的对话框之后的点选不采纳。

与士兵画面「更换士兵模型」弹窗（`outfit41.cpp`）共用的条目操作抽到 `src/game_dialog.h`（向量扩容 `0xB91D0` 按 capacity*2+2、先加 size 再 `0x89950` 拷贝、按下标拷贝时在扩容后再取源、`0x8E170` 析构截断、`_Func_impl` vtable 克隆），游戏 `std::wstring` 的布局与 `0x3D450` / `0x3D3C0` 在 `common/edf/wstr.h`，签名检查统一走 `src/code_signatures.h CheckSignatures`。离线：`tests/game_dialog_test.cpp`（ctest `game_dialog`）。

## 验证范围

- `tests/room_dialog_test.cpp`（ctest `room_dialog`）：页面、跳转、编辑、分页、焦点、取消、未保存确认、两万步随机点选不越界且每次保存都能被真实解析器读回。
- `tests/room_dialog_native_audit.py`（ctest `room_dialog_native`）：`room_dialog_signatures.inc` 与 EDF.dll 一致；构造者的调用顺序、全局与数据指针、InitParam 的 RTTI、原版 lambda 的拷贝形状与关闭调用、LYT_HUIDIALOGBOX 的类与结构。
- 插件运行时还逐段比对 31 段签名（含 `HUiDialogBox::OnUpdate 0x810D10`）、核对 `[0x1F77810]` 指向布局名；不符则不启用对话框（回退旧覆盖层）。

**未实机**：读盘期间对话框自身帧是否继续运行（任务开始时的关闭靠它）、对话框能否同时容纳 14 个按钮而不出屏、多行说明文字（`\n`）的排版、关闭动画期间的时序、联机房间里与 Coop 的共存，都需要游戏内验收。
