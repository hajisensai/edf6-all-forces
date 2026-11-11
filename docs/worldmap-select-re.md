# 选关界面换成世界地图：`ui::HUiHQMissionSelect` 静态逆向（2026-11-11）

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。工具：`tools/edfre.py`（capstone + RTTI），布局用 `pylib/cpk.py` + `pylib/sgo.py` 从 Root.cpk 读。
可信度：H = 反汇编逐条确认；M = 由调用方式 / 数据流推断，未进游戏；L = 猜测。
前置结论（类、构造、显示列表、确认回调）见 `plan-2026-11-10-worldmap.md` §4，本文不重复，只补「照着实现」需要的部分。

## 0. 一页结论

| 问题 | 结论 | 级别 |
|---|---|---|
| 1 绘制时机 | 换 vtable `0x18052E8` **槽 2**（`HUiBase::Draw 0x7FA970`，槽地址 `0x18052F8`）：`Draw(this, xgl::gs::Context* ctx)`，`ctx+8` = `ID3D11DeviceContext*`。钩子里**先画地图、再调原版 Draw** → 地图在 HQ 背景 / 3D 场景之上、本界面保留的面板之下；对话框是另一个界面对象，排在本界面之后画，盖在地图上 | 槽与参数 H；跨界面先后 M |
| 2 布局 | 列表 = `LeftButtonsBaseMSel`（按钮由代码按 `ButtonMission` 模板逐行加）+ `ScrollBarBase/ScrollBarKnob`；保留 `Window02_SoldierSel1`（缩略图 `TextureWindow` + 通关勋章 `MedalPanel`）、`Window02_SoldierSel2`（简介 `DescTextArea/DescText`）。标题 / 按键提示不在本布局，在 `HUiMainFrame`；本布局没有难度 / 兵种元素 | H |
| 3 光标 | `this+0x770` 是 `ui::HuiButtonListUtil`：`+0x778` 下标、`+0x77C` 变化标志、`+0x780` 按钮组件 id 表。上下键 = 原生按钮焦点移动 → 焦点回调 `0x953B70` 写 `+0x778/+0x77C`；OnUpdate 见 `+0x77C` 才刷新简介 / 缩略图 / 背景 `0x840A30` / BGM `0x8406E0`。选中第 i 项：调 `0x953350(this+0x770, i)`（移原生焦点，推荐）或直接写 `+0x778=i; +0x77C=1` | H |
| 3 确认 / 返回 | `0x8A85C0` = `cbRequestMission(const Callback::Result&)` 的成员函数，`rdx` 不读，可 `fn(this, nullptr)` 直调；返回走布局的 `cancel_action` → `ResultCallback(set_exit_code 255)`，插件不用碰 | H |
| 4 任务名 | 行号 → 标题：`[[EDF+0x20B2890]+0x248]` 这个 SGO 表按行取第 0 个子节点（零分配读法见 §4）；原版按钮文字 = 文本键 `MissionSelect_Format` 把 `%NUM%` 换成 **行号+1**（字段 5 = 1 的分支行换成 `???`），`%TITLE%` 换成标题；状态 3 用文本键 `MissionSelect_NotOpenMissionTitle` 当标题。显示列表 `+4` 不参与标题 | H |
| 5 输入 | `UpdateContext {+0 [EDF+0x20B28A0], +8 ui::Input*（被盖住 / 过场时为 null）, +0x10 dt}`。原版**会用鼠标**（全局光标管理器 `[EDF+0x20B28B0]`、按钮悬停 / 点击、列表与简介的滚轮 / 拖动滚动）。要让原版某帧不处理输入：给原版 OnUpdate 传一份 `+8 = null` 的 ctx 副本（只在我们吃掉鼠标事件的那一帧） | 结构 H；效果 M |
| 6 坐标 | 本界面 Draw 的投影 = 1920×1080 设计坐标铺满当前视口（`0x7FAA18` 起）。像素 = `vp.x + X·vp.w/1920`，`vp.y + Y·vp.h/1080`，视口直接读 `ctx+0x398`（`{int x,y,w,h; float minZ,maxZ}`，`0x113E8F0` 写它再 `RSSetViewports`）。UI 阶段的视口应是整块画面（各镜头视口画完会还原，见 §6） | 投影 H；整块画面 M |

---

## 1. 绘制时机与挂点

### 1.1 UI 每帧怎么画（H）

- 主帧函数 `0x705530`（vtable `0x17E91B0` 的槽）依次：游戏 System 更新 `0x1198DA0` → … → 游戏 System 绘制 `0x11977F0`（各镜头视口经 `0x118E500` 把 `0x118D810` 排进渲染命令队列：世界 + HUD）→ **UI 系统更新 `0x119D270`**（`0x7058F7`）→ `0x111B410(EDF+0x2136FF0)` 封队列 → 后续阶段。
- 渲染命令队列 = `EDF+0x2136FF0` 渲染器的 `+0x28A8`（= `EDF+0x2139898`）。写入：`0x51D00` 取槽、填「函数指针 + 参数」、`0x51D60` 提交；执行时函数收到 `(xgl::gs::Context* ctx, reader*)`。世界 / HUD（`0x118E500`）和 UI（`0x11A4610`）进**同一条**队列，按入队顺序执行，所以 UI 一定在本帧 3D 场景与 HUD 之后。
- `0x119D270`（`xgs::ui::SystemCore` 每帧）：
  1. 对象表 `core+0x28`（双向链表）用 `0x119BA80`（链表归并排序）按 `0x11A4470` 的键升序排：键 = 根对象 `+0x24` `<< 16` + 层深（子对象 = 父键 + 1，经 `+0x38/+0x40` 的父 weak_ptr 递归）。
  2. 每个对象 `vt[0]`（`0x11A4550`），再分配输入（`core+0x48` 的 `ui::Input` 列表，`0x119D530..0x119D6E4`），再每个对象 `vt[1]`（`0x11A45C0`，即 OnUpdate；ctx 见 §5）。
  3. 最后按同一顺序：对象 `+0x18` 不带 `0x40`、不带 `1` 的，`0x11A4610(obj)` 入队一条「`0x11A43A0`：`obj->vt[2](obj, ctx)`」（`0x119D781..0x119D79C`，`0x11A43EA: jmp [rax+10h]`）。
- 所以**每个 UI 界面的绘制 = 它自己的 vtable 槽 2**，参数 `rdx` = `xgl::gs::Context*`：
  - `ctx+8` = `ID3D11DeviceContext*`（`0x113E928 mov rcx,[rcx+8]` 后 `call [rax+160h]` = `RSSetViewports`）。
  - `ctx+0x78` 是脏标志、`+0x80..+0x19C` 是缓存的渲染状态（`HUiBase::Draw` 开头 `0x7FA9C7..0x7FA9E8`、`0x112E32B..0x112E3E2`）：插件直接调 D3D 改状态后**必须原样还原**（`scope_pip_gpu.cpp:96 ScopedContextState`），否则 xgl 的缓存与真实状态不一致。
  - `ctx+0x398` = 当前视口（§6）。

### 1.2 挂点：vtable `0x18052E8` 槽 2（H）

| 项 | 内容 |
|---|---|
| 槽地址 | `0x18052E8 + 0x10 = 0x18052F8`，原值 `0x7FA970`（`HUiBase::Draw`，所有 HUi 界面共用这个函数，但**槽是本类自己的**，改槽只影响选关） |
| 原型 | `void __fastcall Draw(HUiHQMissionSelect* self, void* ctx)` |
| 签名 | `0x7FA970: 48 89 5C 24 18 48 89 74 24 20 55 57 41 56 48 8D`；执行器 `0x11A43A0: 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48`，`0x11A43EA: 48 FF 60 10`（`jmp [rax+10h]`）；入队条件 `0x119D788: A8 40 75 09 A8 01 75 05 E8`（`test al,40h / test al,1 / call 0x11A4610`）；`0x113E928: 48 8B 49 08`（ctx+8 是 D3D 上下文） |
| 写法 | 同 `room_ui.cpp` `Chain` 的做法换槽（`PatchVtableSlot`，记下原值，原值不是 `0x7FA970` 时说明有别的插件链过，照链） |
| 线程 | 槽 2 在执行渲染命令队列的线程上跑（与槽 1 OnUpdate 不是同一时刻；是否另一个线程 **M**，按渲染线程对待）：OnUpdate 钩子里算好要画的东西发布，Draw 钩子只读快照 |
| 只在可见时调 | 入队前跳过 `+0x18` 带 `0x40` / `1` 的对象，所以界面被隐藏 / 销毁时我们的 Draw 自然不跑 |

钩子顺序：**先画地图，再调原版 `0x7FA970`**。效果：

- 地图在 HQ 3D 背景（`HUiBG`，另一个界面，槽 2 也是 `0x7FA970`）之上 —— 条件是 HUiBG 的排序键不大于本界面（背景先开、层不高于选关），**M**。
- 本界面保留的元素（缩略图、勋章、简介窗口）由原版 Draw 画在地图**上面**；被我们隐藏的（`+0x1F8 = 0`）不画。
- 对话框（`MissionSelectNotOpen`、`PlayGoDownload_MissionSelect`、首次进入的 `CLEAR_MSG_A`）都经 `0x952280` → `0x119C600` 以 `app:/UI/lyt_HUiMessageBox.sgo` **新建一个根界面**（`0x952363`），它的 Draw 是自己的槽 2，排在选关之后入队 → 盖在地图上，不需要「对话框出现时不画」。**M**：靠新根界面的 `+0x24` 不小于选关的（排序稳定，同键按加入顺序）。
- 标题栏「MissionSelectTitleBar」与按键提示属于 `HUiMainFrame`（布局 `main_frame_setting`，§2），也是另一个界面：若它排在选关之前，全屏不透明的地图会盖住标题与按键提示。**M，实机必看**；盖住时地图避开顶部标题条与底部提示条的矩形，或半透明。
- 过场：本布局 `transition_slide`（0.3 s 从 `(0,1080)` 滑入 / 滑出）由原版 Draw 内的变换实现（`0x7FAA0A call 0x7F9E90` = `HUiBase` 槽 9 取界面变换）。我们自画的地图不会跟着滑，除非同样取 `0x7F9E90(self, &mat4)` 的平移。**M**：可接受（地图直接出现），或读这个矩阵的 y 平移一起偏移。

为什么不选其它候选：

- 「UI 渲染器 draw 列表提交」：UI 没有单独的 draw 列表，就是上面这条命令队列（通用，世界 / HUD / 调试四边形都走它），挂它要自己判断是哪个界面，不如槽 2。
- `HUiBG` 的绘制：同样是 `0x7FA970`（共用函数），只能换 HUiBG 的槽；那样地图在背景之后、但在**所有** HQ 界面之前（离开选关也要自己判断不画），且与选关对象的生命周期脱钩。
- 「UI 全部画完之后」：没必要，也会盖住对话框。

## 2. 布局 `UI/lyt_HUiHQMissionSelect.sgo`（H，Root.cpk 直读）

`xgs_ui_object_class = "HUiHQMissionSelect"`，`focus_start = "ButtonMission"`，`cancel_action = default_cancel_action = [callback ResultCallback [set_exit_code 255]]`，
`unlocked_mission_thumb = app:/UI/UnlockedMissionThumb.dds`，`transition = transition_slide`，
`main_frame_setting`：`CurrentUIMainName.text = MissionSelectTitleBar`、`CurrentUISubName = " "`、`CurrentUIGuideKey.text = Sys_Control_Description_Mission`（→ 标题与按键提示是 `HUiMainFrame` 画的，不在本布局），
`guide_keys = [Sys_Control_Description_Mission, Sys_Control_Description_MissionView, Sys_Control_Keyboard_Description_Mission, Sys_Control_Keyboard_Description_MissionView]`。

元素树（`layout_tree`，坐标为相对父元素的设计坐标 pos / 尺寸）：

```
BaseFrame            Layout  (0,0) 1920x1080
├─ Panel03_MissionSelect  Layout(Model skin Panel03_MissionSelect)  (960,540)  装饰面板模型
├─ ScrollBarBase          (183,167) 11x707      ← 列表滚动条
│  └─ ScrollBarKnob
├─ LeftArea               (0,0) 600x1080
│  └─ LeftButtonsBaseMSel (192,187.5) 624x696  滚动区（scroll_mergin 58，带 stencil）← 任务列表
│       （子按钮由构造 0x8A2575 按模板 ButtonMission 520x58 逐行 AddByName；
│        PlayGo 未下载的行再挂 RotationMark 0x8A25DD；状态 3 的行禁用 0x8A25FE）
├─ Window02_SoldierSel1   (842,223) 540x274
│  ├─ TextureWindow       (56,29) 443x223  skin MissionSelectTexture_skin.sgo = Texture, 默认 UnlockedMissionThumb.dds ← 缩略图
│  └─ MedalPanel          (188,127) 310x130
│     └─ MedalLine (模板，按兵种数 0x8A2B1A AddByName) → MedalText, ClearMedal_base → ClearMedal x5   ← 通关勋章
└─ Window02_SoldierSel2   (842,497) 540x354
   ├─ ScrollBarBaseDesc → ScrollBarKnobDesc
   └─ DescTextArea        (40,12) 465x306  scroll_bar_info guide [Sys_Scroll_RT, Sys_Scroll_RB]
      └─ DescText         TextField 465x1000                                ← 任务简介
```

不在树里的定义：`ButtonMission`（用）、`ButtonMissionSP`（Button00 skin，代码里没有引用它的字符串，**M** 未用）、`Button02Base`、`RotationMark`、`MedalLine/MedalText/ClearMedal*`、`MenuDescWindow/MenuDescText`、`MedalPanelSingle`、`LeftButtonsBase`、`LeftButtonsBaseSlot`、`CenterArea`、`RightArea`、`RightWindow`、`Window`、`HudBase`、`ScrollBar*Tmpl`、`ScrollBarGuide`。没有引用其它 `lyt_*.sgo` 子布局，只引用 skin SGO。

| 处理 | 元素 | 说明 |
|---|---|---|
| 隐藏 | `LeftButtonsBaseMSel`（连同子按钮）、`ScrollBarBase`（连同 `ScrollBarKnob`） | 任务列表本体与它的滚动条。`LeftArea` 只装列表，可一起隐藏 |
| 视需要 | `Panel03_MissionSelect` | 屏幕中央的装饰面板模型（`(960,540)`、flag 256），全屏地图下多半要隐藏；实机看 |
| 保留 | `Window02_SoldierSel1`（`TextureWindow` 缩略图 + `MedalPanel` 勋章）、`Window02_SoldierSel2`（`DescTextArea/DescText` 简介 + 简介滚动条） | 右中部两块窗口，随光标由 OnUpdate 刷新（§3） |
| 不在本布局 | 标题、按键提示（`HUiMainFrame`）；难度、兵种（本界面没有，选中后由 `cbHQRequestMission` 打开后续界面） | — |

缩略图元素名 = **`TextureWindow`**：OnUpdate `0x8A598B..0x8A59BC` 按名取元素 → `0x953D80` 取 `Skin_Texture` → `0x8A4CB0(self, &skin, entry)` 换贴图（构造里 `0x8A290B..0x8A29E0` 同样一遍）；`0x8A4CB0` 内用 `unlocked_mission_thumb`（`0x8A4D04`）作未开放行的图。

隐藏写法同 `room_ui.cpp:466-476`（`kFind 0x839FD0` + `+0x1F8 = 0`）。**风险（M）**：隐藏的按钮是否仍参与原生焦点导航与鼠标悬停命中，未确认（§3、§5 都依赖它）；若仍参与，把 `LeftButtonsBaseMSel` 移出屏幕而不是只清 `+0x1F8`。

## 3. 光标、确认、返回

### 3.1 字段（H）

`this+0x770` 是 `ui::HuiButtonListUtil`（RTTI：`GetDefaultFocusCallback@HuiButtonListUtil@ui`）：

| 偏移（相对 this） | 内容 | 证据 |
|---|---|---|
| `+0x770` | 所属界面指针 | 构造 `0x8A1576 mov [rsi+770h],rsi` |
| `+0x778` | 当前下标（显示列表下标） | 焦点回调 `0x953BD0 mov [rdi+8],ecx` |
| `+0x77C` | 下标变了（byte） | `0x953BD3 mov byte [rdi+0Ch],1` |
| `+0x780/+0x788/+0x790` | `std::vector<int>`：每个显示行对应的按钮组件 id（`Component+0x110`） | 构造 `0x8A2603..0x8A2653`，`0x953690` 存入 |
| `+0x7BC` | OnUpdate 处理过的下标（刷新用） | `0x8A5681` |
| `+0x7C0` | BGM 试听防抖计时（float，秒） | `0x8A5A7E..0x8A5AAC`、`0x8A5BDD..0x8A5C1D` |
| `+0x7C8` | 简介文字（`std::wstring`） | `0x8A56A8..0x8A56D8` |

- 构造 `0x8A264C: 0x953690(this+0x770, &ids)`：对每个按钮 `AddAction(btn, L"focus", GetDefaultFocusCallback())`（`0x9536FF..0x953706`，`0x7EAA40` 即 `room_ui.cpp kAddAction`）。
- 焦点回调 `0x953B70`（`std::_Func_impl` vtable `0x1810B38` 槽 2）：在 ids 里找获得焦点的按钮的 `+0x110`，写 `+0x778 = i`、`+0x77C = 1`。
- 所以**上下键移动光标 = 原生焦点导航**（方向键 / 摇杆 / 鼠标悬停都会让按钮获得焦点），本类代码里除构造外没有别处写 `+0x778`（全库扫 `+0x778` 的写入，选关只有 `0x8A1595`）。
- 构造末 `0x8A27B8: 0x953350(this+0x770, 初始下标)` 设初始焦点：`0x953350(util, int i)` = `ComponentAt(owner, ids[i])`（`0x7D3090`）→ `0x7EE1C0(owner+0x160 焦点管理器, comp)`。

### 3.2 光标变化时原版做什么（OnUpdate `0x8A4F60`，H）

`0x8A5666: cmp byte [rdi+77Ch],0`，非 0 时（`0x8A5673` 清零）：

1. `+0x7BC = +0x778`；`entry = +0x798 + 0x14·下标`。
2. 简介：`0x8A4140(self, &tmp, entry)` → 存 `+0x7C8` → 按名取 `DescText` → `0x954030` 设文字（`0x8A5743..0x8A57F2`），并重置简介滚动。
3. 缩略图：`TextureWindow` → `0x8A4CB0(self, &skin, entry)`（`0x8A5A5F`）。
4. 背景：`0x840A30(ecx = entry.行号)`（`0x8A5A79`）。
5. BGM：`+0x7C0 <= 0` 时立即 `0x8406E0(ecx = 行号, dl = 1)` 并把 `+0x7C0` 设 1.0；否则等 `+0x7C0` 按 `ctx+0x10`（dt）减到 0 再播（`0x8A5BDD..0x8A5C1D`；`+0x7B4` 位 1 置位时不播）——即光标停留 1 秒后才换曲。
6. `[EDF+0x20B2890]` 某状态允许时 `0xDC0B0(gs, 行号, 1)`（`0x8A5B2F`，含义 **L**，疑似「当前选中任务」）。
7. `0x7F74A0` 渐显。

**插件选中第 i 个显示项**（在我们的 OnUpdate 钩子里、调原版 OnUpdate **之前**做）：

- 推荐：`0x953350(self + 0x770, i)`（签名 `85 D2 0F 88 22 01 00 00 48 89 5C 24 08 48 89 74`）。原生焦点移过去 → 焦点回调写 `+0x778/+0x77C` → 原版 OnUpdate 刷新 2..7。原生焦点与 `+0x778` 一致，之后按方向键从这一项继续。**M**：焦点事件是同步触发还是下一帧，实机看日志里 `+0x77C` 何时置位；按钮被隐藏时 `0x7EE1C0` 是否拒绝聚焦（若拒绝则回退下一条）。
- 直接写：`*(int*)(self+0x778) = i; *(u8*)(self+0x77C) = 1`（签名 `0x8A5666: 80 BF 7C 07 00 00 00`、`0x953BD0: 89 4F 08 C6 47 0C 01`）。原版刷新同样全部发生（H），但原生焦点还停在旧按钮：之后按上下键会从旧按钮算起把 `+0x778` 改回去。只在原生焦点不可用时用。
- 不需要自己调 `0x840A30` / `0x8406E0`：它们只由 `+0x77C` 驱动，自己调会和原版的 1 秒防抖打架。

### 3.3 确认 `0x8A85C0`（H）

- 身份：构造 `0x8A1B8A` 把它作为成员函数指针（`{ptr, adj=0}`）装进 `Callback::Agent_Object<HUiHQMissionSelect>`（vtable `0x1805398`），`0x850810(self, L"cbRequestMission", cb)` 按名注册；布局里 `ButtonMission` 的 `callback = cbRequestMission`。调用经 `Agent_Object::Invoke 0x8A4060`：锁 weak_ptr 得到界面 → `(screen+adj)->*pmf(arg)`，所以 `this` = 界面本身，签名 `void HUiHQMissionSelect::cbRequestMission(const ui::Callback::Result&)`。
- 函数体**不读 `rdx`**（`0x8A85F8 lea rdx,…` 是对 rdx 的第一次使用）。插件直调：`reinterpret_cast<void(__fastcall*)(void*, const void*)>(image+0x8A85C0)(self, nullptr)`，签名 `48 89 5C 24 10 48 89 7C 24 18 55 48 8D 6C 24 A9`。
- 逻辑：读 `+0x778` → `+0x7B0 == 0` 且 `entry+0x10 == 0` → `PlayGoDownload_MissionSelect` 对话框（`+0x7B0` 每次 OnUpdate 开头置 1，`0x8A4FA5`，实际只在首帧前有意义）；`entry+8 > 2` → `MissionSelectNotOpen` 对话框；否则 `0x850450(self, &cb, L"cbHQRequestMission")` 按名取请求方回调（构造 `0x8A195A` 在没人注册时装默认的 `cbHQRequestMission 0x8A8120`，即单机本部流程），用 `0x8A2DD0` 组 `MissionResult{行号, weak 界面, this+0x6B0}` 调它，再改全局光标管理器 `[EDF+0x20B28B0]+0x56/+0x57`。
- 线程：在 UI 更新线程（OnUpdate 钩子里），**调完原版 OnUpdate 之后**再调（不在原版遍历按钮动作的过程中重入，`room_ui.cpp NativeEntryClicked` 同理）。调之前先按 §3.2 把 `+0x778` 设到目标行（同帧直接写 `+0x778` 即可，确认只读它）。

### 3.4 返回（H 布局 / M 处理者）

- 返回键走布局的 `cancel_action = default_cancel_action = [callback ResultCallback [set_exit_code 255]]`，是 HUiLayout 通用的结果回调（不是本类函数，本类 RTTI 里没有返回相关成员）。插件**不拦、不调**，保留原版返回。
- `cbHQRequestMission` 的两个 lambda（`0x8A76C0` / `0x8A77D0 → 0x8A7C00`）是确认之后打开的后续界面的结果处理，与返回无关。

## 4. 任务名文字（H）

原版每行按钮的文字（构造 `0x8A26BA..0x8A2711`，`0x83B880` 设按钮文字）：

- 状态 0..2：`0x958A60(std::wstring* out, int row)`；状态 3：`0x9591A0(out, row)`（标题换成文本表 `[EDF+0x20B29A8]` 里 `MissionSelect_NotOpenMissionTitle` 的串，`0x7A3500(table, key)` 返回 `const wchar_t*`）。参数 `row` = 显示项 `+0`（行号，`0x8A26CB/0x8A26DC mov edx,[rbp-60h]`）。
- 两者都进 `0x958AB0(out, row, const wchar_t* title)`：
  - `%NUM%`：`0xE0CA0(gs+0x130, row)`（字段 5）== 1 → `"???"`；否则 `0x767CF0(row + 1)` 的十进制（`0x958BFA lea edx,[rbx+1]`）。**显示列表 `+4`（字段 4）不在这里用。**
  - `%TITLE%`：title 原样。
  - 模板：文本键 `MissionSelect_Format`（`0x958DAD → 0x957CD0`）。
- 标题本身（`0x958A60` 的取法，零分配，插件可直接照抄）：

```cpp
// mgr = *(u8**)(EDF + 0x20B2890); 表 = MISSIONLIST.OFFLINE.TXT.<LANG>.SGO（每行 [标题, 简介]，mission-list-re.md「TXT 列表」）
const u8* n = *(const u8**)(mgr + 0x248);
const u8* c = n + *(const i32*)(n + 8);          // 子节点数组
const u8* r = c + 12 * row;                       // 第 row 行（节点 12 字节）
const u8* a = r + *(const i32*)(r + 8);           // 该行的子节点数组
const wchar_t* title = (const wchar_t*)(a + *(const i32*)(a + 8));   // 第 0 个子节点 = 标题
```

  原版**不检查边界**（mission-list-re.md 已述），插件先用 `0xE0650(gs+0x130)` 取行数或只对显示列表里的行号取。每帧读同一指针即可，不必缓存成自己的串（换语言时表会重载，**M**）。
- 若要原版格式的整行（「第 N 关 标题」）：调 `0x958A60` / `0x9591A0` 得到游戏 `std::wstring`（MSVC 布局 0x20 字节），用完必须用**游戏的** `0x3D3C0`（wstring 析构，选关里每个临时串都用它，如 `0x8A56B7`）释放，不能用插件自己的 CRT（插件是静态 CRT，堆不同）。签名 `0x958A60: 40 53 48 83 EC 20 48 8B 05 23 9E 75 01 48 8B D9`。只在 UI 线程调。
- 简介：`0x8A4140(self, std::wstring* out, entry)`（同一 TXT 表第 1 个子节点加状态说明，**M** 未逐条读），原版放 `DescText`；地图悬停时直接显示保留的简介窗口即可，不必自己取。

## 5. 输入

### 5.1 原版怎么读（结构 H）

- OnUpdate 的 `rdx` = `UpdateContext*`（`0x119D270` 栈上 `[rbp-9]`）：`+0 = [EDF+0x20B28A0]`，`+8 = ui::Input*`，仅当对象 `+0x28`（本帧分到输入）为真时非空（`0x119D72B..0x119D73B`），`+0x10 = float dt`（`0x8A5303 movss xmm3,[r13+10h]`）。
- 选关取 `r14 = ctx+8`，另有过场判断（`this+0xD8` 过场对象，`0x8A5001..0x8A5029`），同 `room_ui.cpp kGetInput 0x8DAD00`。对话框打开时本界面分不到输入 → `ctx+8 == null`（**M**：分配规则 `0x119D3D0..0x119D6E4` 按排序键与「独占」位 4 处理，未逐条推演）。
- 原版用鼠标：全局光标管理器 `[EDF+0x20B28B0]`（`+0x10` 光标位置 float2，`0x8A5078..0x8A50E0` 与上一帧 `this+0x8E0` 比较判断鼠标是否在动；`+0x47/+0x50/+0x56/+0x57` 标志）；对 `LeftButtonsBaseMSel` 和 `DescTextArea` 调 `0x96ADD0`（两次）/ `0x96AF10` / `0x968220` / `0x96B0F0`（滚轮 / 拖动 / 摇杆滚动；读 `Input+0x2D0/+0x2D1`），`0x8A7090(self, input, dt)`。按钮本身由原生组件处理悬停 / 点击 / 方向键 / 确认（HUiLayout 更新 `0x84B490(self, ctx)`，`0x8A5661`）。
- `DescTextArea` 的滚动提示是 `Sys_Scroll_RT / Sys_Scroll_RB`（布局），简介区可能吃右摇杆或扳机做滚动 **M**——若是右摇杆，会与地图平移冲突。

### 5.2 让原版不处理我们吃掉的输入（M）

- 不需要在设备层吞：原版只通过 `ctx+8` 的 `ui::Input`（和全局光标管理器）读输入。
- 我们吃掉一次鼠标事件（在地图上点选 / 拖动 / 滚轮）的那一帧：OnUpdate 钩子里复制一份 ctx（0x18 字节，`room_ui.cpp kCtxSize`）把 `+8` 置 null 传给原版 → 原版这一帧当作「被盖住」：不滚动、不响应按钮。`+0x77C` 刷新块（`0x8A5666`）在 `r14` 判空（`0x8A506F`）之外，照常执行，所以同帧 `0x953350` 选中的行仍会刷新简介 / 缩略图 / BGM。代价：同一帧的键盘 / 手柄输入也丢一帧（可忽略）。其余帧原样传，方向键 / 确认 / 返回照旧。
- 鼠标在地图上**移动**（不点）：原版只会让鼠标下的按钮获得焦点；列表已隐藏，**若隐藏按钮仍可被悬停命中**，鼠标划过左侧原列表区域会改光标 → 实机确认，必要时把列表移出屏幕（§2）。
- 滚轮：原版的滚轮只滚 `LeftButtonsBaseMSel` / `DescTextArea`（按组件区域命中，**M**）；在地图区域滚轮不会滚简介，但会滚隐藏列表（无害）。我们用滚轮缩放地图时按上一条置 null 即可。
- 右摇杆：若实机确认简介区吃右摇杆，地图用右摇杆平移的帧同样置 null，或地图改用别的键。
- 鼠标 / 滚轮 / 手柄的读取用插件现成设施（`src/map.cpp:259` 鼠标增量、`:180-235` 滚轮子类化、`room_ui.cpp:83 kPadTable`）；光标位置可直接读 `[EDF+0x20B28B0]+0x10`（与原版同源，**M**：坐标是 1920×1080 设计坐标还是像素未确认）。

## 6. 屏幕坐标与视口

- 投影（H，`HUiBase::Draw 0x7FAA0F..0x7FAA8B`）：`this+0x7C == 1` 时 `0x4E000` 建正交投影：中心 `(1920/2, −1080/2)`、宽 1920、高 1080、深度 0..−4000（常量 `0x17F5B88 = 1920.0`、`0x17F5B8C = 1080.0`、`0x17F6594 = −4000.0`；签名 `0x7FAA18: F3 0F 10 2D 68 B1 FF 00 F3 0F 10 1D 64 B1 FF 00`）；否则 `0x4DD70` 透视（视角 `this+0x430`，选关布局不用，**M**）。即 1920×1080 设计坐标**拉伸**铺满当前视口（非 16:9 时 x、y 缩放不同，原版 UI 本来就这样）。
- 像素 = `vp.x + X · vp.w / 1920`，`vp.y + Y · vp.h / 1080`（Y 向下）。视口在 Draw 钩子里直接读 `ctx+0x398`：`{int x, y, w, h; float minZ, maxZ}`（`0x113E8F0` 写 `+0x398/+0x3A8` 再 `RSSetViewports`，H）；或 `RSGetViewports`。不要用 `hudscale.h` 的 `*(*(EDF+0x2137090)+0x10)+0x20/+0x24` 推算：它是 `EDF+0x2136FF0` 渲染器 `+0xA0` 下的目标尺寸（**M**：与延迟渲染的 `+0x6A8/+0x6C8` 目标同一类对象，`0x112FC10` 用它们的 `+0x20/+0x24` 设全屏视口），通常等于视口但不保证。
- UI 阶段的视口是否为整块后台缓冲：各镜头视口命令 `0x118D810` 进来时存下 `ctx+0x398`、设镜头视口、画完 `0x118D9CB` 还原（H）；UI 命令在它们之后，所以视口 = 进入镜头前的那个，即 `0x112FC10` 设的全屏 `(0,0,目标宽,目标高)`（**M**，实机日志打印一次 `ctx+0x398` 与后台缓冲尺寸核对）。
- 渲染目标：沿用当前绑定的 RT（原版 UI 画在哪我们就画在哪），不要自己 `OMSetRenderTargets`；深度：原版 UI 用的深度状态在 `ctx` 缓存里，地图用自己的「不测深度」状态并在结束时还原。

## 7. 需实机确认

1. Draw 槽 2 钩子里先画地图：HQ 背景（HUiBG）在地图下、选关保留的两块窗口在地图上、`MissionSelectNotOpen` / `PlayGoDownload` / `CLEAR_MSG_A` 对话框在地图上。
2. 标题栏（`MissionSelectTitleBar`）与底部按键提示（`HUiMainFrame`）是否被地图盖住。
3. 日志打印一次 `ctx+0x398` 视口与后台缓冲尺寸（含窗口化、非 16:9、4K），确认 UI 阶段是整块画面。
4. 隐藏 `LeftButtonsBaseMSel`（`+0x1F8 = 0`）后：方向键是否还能在隐藏按钮间移动焦点；鼠标划过原列表区域是否改光标；`0x953350` 能否聚焦隐藏按钮。
5. `0x953350` 之后 `+0x77C` 是同帧还是下一帧置位；简介 / 缩略图 / 背景 / BGM（停留 1 秒）是否跟上。
6. 直调 `0x8A85C0(self, nullptr)`：开放行进入后续流程，状态 3 行弹「未开放」，联机房间 / 建房 / 搜房打开时同样交回请求方。
7. 原版 OnUpdate 收到 `+8 = null` 的 ctx 副本那一帧：不滚动、不响应，其余帧方向键 / 确认 / 返回正常。
8. 简介区是否吃右摇杆（`Sys_Scroll_RT/RB`）；`[EDF+0x20B28B0]+0x10` 光标坐标的单位。
9. 打开 / 关闭的 0.3 s 滑动过场里地图不随动是否可接受。
