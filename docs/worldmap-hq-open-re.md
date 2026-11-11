# 单机进本部直接打开选关（世界地图）静态逆向（2026-11-11）

维护者：「开始游戏就是打开地图开始」。目标：单机进本部（HQ）后直接打开选关界面 `HUiHQMissionSelect`（之后会换成世界地图，
见 [plan-2026-11-10-worldmap.md](plan-2026-11-10-worldmap.md) §4 / §5），不再先停在本部菜单让玩家点「任务选择」。

只做静态逆向，未开游戏。地址都是 EDF.dll（TimeDateStamp `0x678CCB46`）的 RVA。脚本行号按 Root.cpk 的
`MAINSCRIPT/MAINSCRIPT.AS`（`pylib/rootcpk.py` 的 `Game.read('MAINSCRIPT','MAINSCRIPT.AS')`，CRLF，56786 字节）。
证据级别：**H** 反汇编 / 脚本 / 布局原文直接证实；**M** 由结构与惯例推断；**L** 猜测；**U** 需实机。

## 0. 结论先行：plan 文档 §4 的两条前提不成立

| plan §4 的说法 | 实际 | 级别 |
|---|---|---|
| 单机本部「由 MAINSCRIPT 按工厂名创建 `HUiHQMissionSelect`」 | 脚本只创建本部菜单 `lyt_HUiHQMain.sgo`（第 952 行），全脚本没有一处打开选关。选关是**本部菜单这个原生 UI 自己**按布局数据打开的子界面：`LYT_HUIHQMAIN.SGO` 里按钮 `ButtonGotoSelectMission` 带 `sgo_path = app:/UI/lyt_HUiHQMissionSelect.sgo` | H |
| `RestoreMissionSelect()`「任务结束回来时恢复选关」 | 原生实现 `0x70F930` 只有两条指令：`mov rcx,[GS]; jmp 0xDBCE0`；`0xDBCE0` 把存档里的「上次选的任务 / 难度」`GS+0x57A4 / +0x57A8` 抄回当前值 `GS+0x48 / +0x4C`，然后 `ret`。**与 UI 无关**，不会打开任何界面。脚本里唯一的调用在联机 `ApplyModeNo`（第 1898 行，模式未变时） | H |

所以「进本部时调 `RestoreMissionSelect()` 来直接开选关」这条路不存在；要走的是本部菜单原生对象自己的「打开子界面」路径。

## 1. MAINSCRIPT `HQMain` 流程（H）

主脚本的「画面」是一串例程，每个返回下一个例程的名字，由 `createCoRoutine(const string&)`（`0x12946F0`，测试插件
`tests/autopilot/autopilot.cpp` 已在钩它改道）按名字启动。单机相关链：

`ModeSelect`（选模式：本篇 / 任务包，第 795 行；`SetMode` → `LoadSaveData_ModeSelect` → `UnlockDownloadContents` →
`Opening()`）→ **`HQMain`**（第 894 行）→ `PlayMission_Offline`（1087）→ `PlayMission_Common`（990）→
`Result_Offline`（1272）→ `Result_Common`（1395，写档）→ 回 **`HQMain`**（1303）。调试入口 `Debug_StartOffline`（1865）同样
以 `return "HQMain"` 结束。

`HQMain` 本身（摘录，第 894–987 行，省略中间的 Epic/EOS 连接检查）：

```angelscript
string HQMain()
{
	g_resource.Entry("app:/ui/UiResourceGroup_HQ.sgo", SYSTEM_GROUP_SCENE, 0);
	g_resource.EntryModeImage(0);
	g_resource.Wait();
	g_fade.Wait();
	g_bg.Play("app:/ui/lyt_bg.sgo");
	g_main_frame.Play("app:/ui/lyt_MainFrame.sgo");
	g_main_frame.GetObject().SetDrawOrder(g_fade.GetObject());
	g_music.PlaySelectedMissionBGM_Start();   // 現在のミッションに設定されたBGMを再生する
	if ( IsEpic() || IsDoneConnectSingleton() ) { ... return "StartToTitle"; ... }   // Steam 版走不到（M）
	UI ui_handle3D = CreateUiFile("app:/ui/lyt_HUiHQ3DModel.sgo");
	UI ui_handle = CreateUiFile("app:/ui/lyt_HUiHQMain.sgo");
	g_fade.In();
	g_fade.Wait();
	ui_handle.WaitCloseRequest();            // 整个本部菜单（含选关、难度、兵种、装备）都在原生 UI 里
	g_music.Stop( g_music_fadetime );
	if( GetUiExitCode() ==  Hui_ReturnOK)    // 0：出击
	{
		g_fade.Out();
		g_fade.Wait();
		return "PlayMission_Offline";
	}
	if( false == IsTGSMode() && GetUiExitCode() ==  Hui_HQMain_Return_GotoTitle)   // 10
		SaveAllData_ReturnTitle(true);
	g_fade.Out(); g_fade.Wait(); ui_handle3D.Close(); g_music.WaitStop();
	return "StartToTitle";
}
```

要点：

- 脚本对本部菜单只有「创建 → `WaitCloseRequest()` → 看退出码」三步。玩家点「任务选择」、选关、选难度、选兵种 / 装备、
  最后确认出击，**全部发生在原生 `HUiHQMain` 及其子界面里**，脚本看不到中间过程（H）。
- 退出码只有两类：`Hui_ReturnOK = 0` → `PlayMission_Offline`；其余（`Hui_HQMain_Return_GotoTitle = 10` 等）→ 回标题
  （`UI_COMMON_CONST.H`）（H）。
- 选中的任务 / 难度不经脚本传递：由原生写进全局状态 `GS+0x48 / +0x4C`（`GS = *(EDF+0x20B2890)`），`PlayMission_Offline`
  直接用（`autopilot.cpp` 的 `HQMain -> PlayMission_Offline` 改道就是写这两个字段后跳过菜单，已实机用着）（H）。
- 任务结束回来：`Result_Offline` 照样 `return "HQMain"`，即**每次都重建一个新的本部菜单对象**，不存在「回到选关」的专门路径（H）。

## 2. `RestoreMissionSelect()`（H）

- 注册：`0x719690` 内，声明 `void RestoreMissionSelect()`（`0x17EAAC0`，`0x71C82F` 取地址），函数指针在 `0x71C7CE`
  `lea rcx,[rip-0xCEA5]` = **`0x70F930`**。
- `0x70F930`：`mov rcx,[rip+0x19A2F59]`（= `[0x20B2890]`，GS）；`jmp 0xDBCE0`。
- `0xDBCE0`：`GS+0x48 = GS+0x57A4; GS+0x4C = GS+0x57A8; ret`。`GS+0x57A4` 起是当前模式的存档数据（`src/airstrike.cpp`
  注释：`LoadSaveData_ModeSelect` 整块读进 `GS+0x57A4`），即「存档里记的当前任务行号 / 难度」。
- 调用者：脚本 `ApplyModeNo`（联机：主机切换的模式与本机相同时，把当前任务恢复为存档值）；原生另有 `0x8E2D41`、`0x9457B8`
  两处直接调 `0xDBCE0`（未细看，M：联机大厅 / 房间侧的同类恢复）。
- 结论：它只是「把当前选中任务恢复为存档值」，**不能**用来打开选关；进本部时调用它也无意义（`LoadSaveData_ModeSelect`
  之后当前值本来就来自存档）。

## 3. 本部菜单怎么打开选关、选完怎么出击（原生，H）

### 3.1 按钮是数据驱动的 `CallWindow`

`LYT_HUIHQMAIN.SGO`（Root.cpk `UI/`，`pylib/sgo.py` 读出）里：

```text
"ButtonGotoSelectMission": [[0,"Button02Base"], [["text","EDFMenu_Start"],
   ["callback", ["CallWindow", [["sgo_path","app:/UI/lyt_HUiHQMissionSelect.sgo"], ["result_callback","ReceiveResult"],
                                ["hide",1], ["transition_dir",0], ["init_args",[]]]]]]]
"focus_start": "ButtonGotoSelectMission"
"default_cancel_action": ResultCallback{set_exit_code 255}      "exit_cancel_action": ResultCallback{set_exit_code 255, set_close_exit 255}
```

其余按钮同构：`ButtonGotoSelectArmy` → `lyt_HUiHQSoldierSelect.sgo`，`ButtonGotoShowHistory` → `lyt_HUiHQHistory.sgo`，
`ButtonGotoTitle` → `lyt_HUiMessageBox.sgo`；游戏设置 / 系统设置两个按钮用 HUiHQMain 构造函数自己注册的
`CallHUiHQGameSetting` / `CallHUiHQSystemSetting`。本部一打开，焦点就在「任务选择」（`focus_start`）上。

- `HUiHQMain` 构造 `0x89DA40`（vtable `0x1804DD8`）：对 `ButtonGotoSelectMission` 只取了它的说明文字
  `EDFMenuInfo_Start`（`0x89EC8F` / `0x89ECC6`）和焦点顺序，**没有**用 `0x8456D0` 给它另绑回调；它的行为全来自布局里的
  `CallWindow`（H）。
- 通用动作表 `0x847990`（所有 HUi 窗口共用）：`Default` → `0x84EE90`，**`CallWindow` → `0x84EEA0`**，
  `ResultCallback` → `0x84F780`，`ReceiveResult` → `0x84F440`，`ButtonApply` → `0x84EB40`（每个都是先 `lea` 处理函数、
  再 `0x850810` 按名字登记）（H）。
- 按钮的回调在布局解析时绑定（`0x841800`）：参数解析进 `HUiButton+0x2B8`（`0x8426ED` → `0x841060`），按名字造出窗口的
  动作仿函数（`0x850450(window, &out, name)`，`0x842774`），再用 `0x7EAA40`（`Component::AddAction`，插件 `room_ui.cpp`
  已在用，`kAddAction`）挂到按钮的 `"Click"` 事件（`0x17FE020`）上（H）。
- `CallWindow` 处理函数 `0x84EEA0`：读 `sgo_path` / `result_callback` / `hide`，用 `0x850450` 把 `"ReceiveResult"`
  造成结果回调，最后 `0x119C600(window->[+0x48], &out, path, &params)` 打开子窗口。联机房间按钮 `0x8FC2C0` 走的
  `0x845250`（`HUiBase` 的打开子窗口）也是同一个 `0x119C600`（H）。

### 3.2 选关 → 难度 → 出击

- 选关确认 `0x8A85C0` → 打开难度对话框 `lyt_HUiHQDifficultySelectDlg.sgo`（`0x8A8120` 内 `0x8A844F` 调 `0x845250`）（H）。
- 难度对话框的结果由 `0x8A7C00` 处理：`0x8A2DD0` 组 MissionResult，往结果里放 **`set_exit_code = 0`、`set_close_exit = 0`**
  （`0x8A7D44 mov r12d,0`；`0x8A7FAD` / `0x8A8034` 两次 `0x85190` 插入），经 `[this+0x558]`（打开它时给的
  `result_callback`）交回本部（H）。
- 本部的 `ReceiveResult` `0x84F440` 读结果里的 `set_exit_code` / `set_close_exit` 并照做（`0x7E2EB0` / `0x7E3180`）：
  本部以退出码 0 关闭 → 脚本 `GetUiExitCode() == Hui_ReturnOK` → `PlayMission_Offline`（H 字段 / M 两个函数的具体含义）。
  所以出击前不经过兵种 / 装备画面：那些是本部菜单上单独的按钮（`ButtonGotoSelectArmy`），玩家要先在本部配好（H，按布局）。
- 选关里按「返回」：选关布局的 `default_cancel_action` 只有 `set_exit_code 255`、没有 `set_close_exit`，所以只关掉选关，
  本部（打开时 `hide 1` 隐藏了）重新显示，焦点回到按钮上（H 布局 / M 显示恢复的具体函数）。这与维护者描述的原版行为一致。

### 3.3 能不能只改布局（`timeout_action`）实现？——不行（H）

布局有 `timeout_action` / `window_timeout`（原版只有 `LYT_TGSEND.SGO` 用：5 秒后 `ResultCallback set_close_exit 1`）。解析在
`0x84A700`：`+0x638` 计时（缺省 −1.0），`+0x640` 动作仿函数，`+0x650` 参数。每帧基类更新 `0x84B490` 在 `0x84B702..0x84B742`
里 `+0x638 -= dt`，**一旦 ≤ 0 就每帧都调一次动作，从不复位**。把 `CallWindow 选关` 放进本部的 `timeout_action` 会在
每一帧都再开一个选关（按返回也会立刻再开），本部菜单再也回不去。所以不能单靠布局。

## 4. 联机房间（H，不需额外改动）

`HUiRoom` 的「选择任务」按钮回调 `0x8FC2C0` 在 `0x8FC4D5` 取 `app:/UI/lyt_HUiHQMissionSelect.sgo`（`0x180B260`）、`0x8FC4E4`
调 `0x845250` 打开；本部按钮 `CallWindow` 打开的是同一个布局路径，布局的 `xgs_ui_object_class` 决定类，所以两边都是
`HUiHQMissionSelect`（vtable `0x18052E8`）。该路径字符串的全部引用只有房间 `0x8FC2C0`、房间搜索 `0x90F490`、建房 `0x91C260`
三处代码 + 本部布局数据（H）。世界地图接管 vtable `0x18052E8` 槽 1 后两边自动都是地图。

「进本部自动开选关」只挂 `HUiHQMain`（vtable `0x1804DD8`），房间是 `HUiRoom`（`0x180AC40`），互不影响；联机流程走
`Room()` / `PlayMission_Online`，不经过 `HQMain`（H，脚本）。

## 5. 实现方案比较与推荐

| 方案 | 做法 | 评价 |
|---|---|---|
| A. 改 MAINSCRIPT | 安装器往 `Mods/MAINSCRIPT/` 放改过的 `MAINSCRIPT.AS` | **不推荐**。脚本没有「从本部打开选关」的 API（选关是本部原生 UI 的子窗口，见 §3），脚本层最多 `CreateUiFile` 顶层打开选关，结果回调 / `MissionResult` 交回谁都没有着落（L）；整份 56 KB 原版脚本要随 Mods 分发并与原版逐行同步；仓库和 EDF6Coop 都没有替换 MAINSCRIPT 的先例（H，grep），EDFModLoader 对 `MAINSCRIPT/` 的覆盖是否生效未验证（U）。 |
| B. 改布局 `timeout_action` | 见 §3.3 | **不可行**（每帧重复触发）。 |
| C. 插件在本部第一帧「按下」任务选择按钮 | 在插件已链好的 `HUiHQMain::OnUpdate` 钩子里，每个本部对象一次，像原版 `ButtonApply` 那样触发 `ButtonGotoSelectMission` 的 `"Click"` | **推荐**。走的就是玩家按确认时那条 `CallWindow` 路径：参数、结果回调、隐藏 / 恢复、难度对话框、出击全是原版的；不加新补丁点。 |

### 5.1 推荐方案 C 的细节

挂点（都已在 `src/room_ui.cpp` 核对签名并在用，H）：

- `HUiHQMain::OnUpdate`：vtable 槽 `EDF+0x1804DE0`，原函数 `0x89F740`，签名 `kHqUpdateSig`
  `40 53 48 83 EC 30 48 8B D9 E8 42 BD FA FF 48 8D`。`room_ui.cpp` 的 `screens[1]`（`"hq"`）已经链了这个槽
  （读出再链，不假设原值；EDF6Coop 也可能链）。新逻辑放进同一个钩子，不另开补丁点。
- 析构 `0x89F480`（槽 `0x1804E00`，`kHqGoneSig`）：`room_ui.cpp` 已用它判定「同一个本部对象」（新对象可能落在旧地址上，
  不能只比指针）。「已自动打开过」的标记跟着对象走，析构时清除。
- `HUiBase::GetInput` `0x8DAD00`（`kGetInputSig` `48 89 5C 24 08 57 48 83 EC 20 48 83 B9 D8 00 00`）：返回非空 = 本部此刻
  自己读输入（没有子界面 / 对话框 / 过渡盖着）。
- `HUiLayout` 按名找元素 `0x839FD0`（`kFind`，参数 `(this, shared_ptr<Component>* out, const std::wstring* name)`）。

新增的原生函数（需要加签名核对，H 地址 / 字节）：

- `Component::FireAction` **`0x7EAB90`**`(Component* c, const wchar_t* event, Callback* info)`：在 `c+0x1D0` 的事件表里按名字
  找到动作并调用。开头 `48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57`。原版 `ButtonApply`（`0x84ED79`）、焦点事件
  （`0x83B741` `"focus"`、`0x83B831` `"kill_focus"`）都这样调。
- `Component::HasAction` `0x7EA8D0`（`40 53 56 48 83 EC 68 48 8B 05 7A 47 80 01 48 33`）：`ButtonApply` 在触发前先查
  `"Click"` 是否存在（`0x84EC0A`）。
- 事件参数对象：两字 `{ vptr = EDF+0x17FE090, HuiCallbackInfo* }`，vtable 是
  `ui::Callback::ResultImpl<ui::HuiCallbackInfo>`（COL `0x1CA4388`），槽 0 `0x84E780` 返回 `[this+8]`，槽 1 `0x84EA20`
  返回 true。`ButtonApply` 传的是按钮自己的 `HUiButton+0x2B0`（`0x84EC25`，绑定时参数解析在 `+0x2B8`）（H）。
- 原版 `ButtonApply` 在触发前还做了 `0x83A0C0(window, button->[+0x110])`（把焦点移到该按钮，`0x84EC17..0x84EC20`）并写
  `[button+0x2B0]`（`0x84EC32`）。实现时照抄这段顺序（M：两处写入的含义没细看，照原版顺序做即可，不要自己简化）。

每帧逻辑（在原 `OnUpdate` 之后、`room_ui` 现有逻辑之前或之后均可，需与 F2 战前配置菜单互斥）：

1. 本对象未自动打开过，且 `GetInput(this, ctx) != nullptr`（本部已可操作），且战前配置菜单（room_ui）没开着；
2. `kFind(this, &c, L"ButtonGotoSelectMission")`，`dynamic_cast` 到 `HUiButton`（RTTI `0x2062F50`，源 `Component` `0x2062F00`，
   原版用 `0x12DA7AA` 做同样的转换）且 `HasAction(c, L"Click")`；
3. 照 `ButtonApply` 的顺序触发 `FireAction(c, L"Click", &{EDF+0x17FE090, c+0x2B0})`，记「已打开」，写一行日志；
4. 任一步失败：记一行日志、本对象不再尝试（本部保持原样，玩家照常手点）。

### 5.2 返回落到哪里（问题 3 的建议）

**落回本部菜单，保持原版行为**（§3.2 已证明原版就是这样）。理由：兵种 / 装备 / 履历 / 设置 / 回标题都只在本部菜单上；
自动打开只做「每个本部对象一次」，返回后不再自动打开，玩家点「任务选择」（`EDFMenu_Start`，此时打开的就是地图）即可回到
地图。不建议「返回 = 重新打开地图」或去掉本部菜单：那样装备 / 设置没有入口。可选的小改进（不必须）：把
`EDFMenu_Start` 的文字改成「世界地图 / 出击」，让玩家知道这个按钮就是地图。

### 5.3 EDF5 / EDF4.1 任务包

- 任务包只是 EDF6 的「模式」（`make_edf5_campaign.py` 写 ModeList、任务列表、`MISSION/EDF5_OLD_SCRIPT/<id>/MISSION.BVM`
  任务脚本、文本），**没有自己的 MAINSCRIPT**；仓库里没有任何地方写 `MAINSCRIPT/`（H，grep）。`src/edf41before.cpp` 说的
  「EDF4.1 的 MAINSCRIPT」指 EDF4.1 原作的脚本，作对比用，并未移植。
- 所有模式都由同一份 EDF6 `MAINSCRIPT.AS` 的 `HQMain` 创建同一个 `lyt_HUiHQMain.sgo`（安装器已把它覆盖为
  `Mods/UI/LYT_HUIHQMAIN.SGO`，`tools/room_layout.py`，按钮数据原样保留），所以方案 C 对 EDF6 本篇、DLC1/2、EDF5 / EDF4.1
  任务包、测试场一律生效（H 脚本与布局 / U 实机）。

（核对：本机游戏目录已装的 `Mods/UI/LYT_HUIHQMAIN.SGO` 里 `ButtonGotoSelectMission` 的 `CallWindow` 参数与 `focus_start`
和 Root.cpk 原版逐项相同，H。）

## 6. 风险与各入口路径

| 路径 | 脚本 / 原生行为 | 方案 C 的表现 | 级别 |
|---|---|---|---|
| 新存档首次进入 | `ModeSelect` 末尾 `Opening()`（只内容 0、只一次，放完写档）在 `return "HQMain"` **之前**跑完；EDF6 没有本部教程（`PlayTutorialOpening` 只被调试例程 `TutorialOpeningTest` 调用） | 开场文字结束、本部建好、可操作后才打开地图 | H 脚本 |
| 本部上的原生提示（新武器 / DLC 解锁等，若有） | 盖在本部上时 `GetInput` 为空 | 等提示关掉再打开，不会叠在提示下 | M（是否存在这类提示未查，U） |
| 淡入 | `HQMain` 在 `CreateUiFile` 之后才 `g_fade.In(); g_fade.Wait()`；`g_fade` 是另一个 UI，本部的 `GetInput` 可能在淡入期间就非空 | 选关可能在淡入途中打开（视觉上提前），无功能问题；若要等淡入结束，需另查 `g_fade` 状态 | U |
| 任务结束返回 | `Result_Offline` → `Result_Common`（写档、关结算）→ `return "HQMain"`：**新**本部对象 | 自动再开地图，光标在 `ProceedNextMission` 推进后的当前任务（`GS+0x48`） | H |
| 撤退 / 失败 / 重开 | 撤退、失败同上回 `HQMain`；「重新开始」在 `PlayMission_Common` 循环内，不回本部 | 同上 / 不涉及 | H |
| 回标题、换存档槽、换模式（本篇 / DLC / 任务包） | 都经 `StartToTitle` → … → `ModeSelect`（`SetMode`、`LoadSaveData_ModeSelect`）→ 新 `HQMain`；本部内没有换模式的入口 | 每次新本部对象都自动开一次，模式与存档已就绪 | H |
| 读档失败 | `ModeSelect` 直接回 `SlotSelect`，不进 `HQMain` | 不涉及 | H |
| 选关里返回 | 回本部菜单（§3.2），同一对象 | 不再自动打开 | H 布局 / U 实机 |
| 本部里打开装备 / 设置再回来 | 同一对象 | 不再自动打开 | H（按设计） |
| F2 战前配置菜单（room_ui，HQ 也有） | 只在本部自己读输入时出现 | 地图开着时 F2 菜单隐藏冻结；第 1 步须在 room_ui 菜单开着时不触发 | H 代码 |
| 2P 分屏 | 同一 `HUiHQMain`，同一按钮（构造 `0x89E7FC` 按人数分支只影响装备按钮） | 同样生效 | M |
| 测试插件 `tests/autopilot` 的 `mission … hq`（拍本部画面） | 停在 `HQMain` | 会被地图盖住；需要时给 autopilot 一个开关，或改拍地图 | H 代码 |
| EDF.dll 不是已知版本 / 签名不符 | — | 和 room_ui 一样整体 REFUSED，本部保持原样 | H（按现有机制） |

## 7. 需要实机确认的点

1. `FireAction(按钮, "Click", …)` 在 `OnUpdate` 里调用是否与玩家按确认完全等价：选关正常出现、本部被隐藏、返回后本部正常
   显示且焦点在「任务选择」上。
2. 选关 → 难度 → 本部以退出码 0 关闭 → `PlayMission_Offline`，进入的是选中的任务 / 难度（日志里 `GS+0x48 / +0x4C`）。
3. 淡入期间就打开是否好看；若不好看，再查 `g_fade` 的完成状态作为额外条件。
4. 任务结束 / 撤退回来、回标题再进、换任务包（EDF5 / EDF4.1 / 测试场）后，各自恰好自动打开一次。
5. 进本部时是否有原生提示框盖在本部上，确认此时地图等它关掉才出现。
6. 与 EDF6Coop 同装时，HQ 槽的链式钩子顺序不影响（Coop 主要链房间槽）。
