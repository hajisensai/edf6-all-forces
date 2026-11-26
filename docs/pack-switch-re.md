# HQ 主界面 Q / E 切换任务包（`src/pack_switch.cpp`）

2026-11-26，静态逆向（`EDF.dll` TimeDateStamp `0x678CCB46`，地址为 RVA）。地址与契约由 `tests/pack_switch_native_audit.py` 对真文件核对，纯逻辑由 `tests/pack_switch_test.cpp` 覆盖。**未实机验证。**

## 1. 需求与做法

原版换任务包要「回到标题 → 选存档 → 任务包」。插件在离线 HQ 主菜单（`ui::HUiHQMain`）按 `Q` / `E`（手柄 `LB` / `RB`）切到上一个 / 下一个任务包，回到同一个 HQ。

做法不碰存档格式，全程借脚本自己的路径：

1. HQ 的 OnUpdate（vtable `0x1804DD8` 槽 1，原版 `0x89F740`）里读按键沿；HQ 被盖住（`GetInput 0x8DAD00` 返回空）、整备开着、世界地图占着按键时不响应。
2. 选目标模式（§3），记下来，调 `0x7E3180(10)` 关 HQ：这就是原版「回到标题」确认框 Yes 的 `set_close_exit 10`（`LYT_HUIHQMAIN.SGO` 的 `ButtonGotoTitle`）。脚本 `HQMain()` 看到 `Hui_HQMain_Return_GotoTitle`（`UI_COMMON_CONST.H` = 10）就 `SaveAllData_ReturnTitle(true)` 存档，然后 `return "StartToTitle"`。
3. `main()` 用 `"string " + routine + "()"` 调 `createCoRoutine`（`0x12946F0`）。插件钩它：有待切换的模式、且请求的是 `string StartToTitle()` 时，换成 `string AF_PackSwitchN()`。
4. 这个例程第一次用时编进 MAINSCRIPT 的模块（`asCModule::CompileFunction`，`asCOMP_ADD_TO_MODULE`）：
   `string AF_PackSwitchN() { if( ApplyModeNo(N) ) return "HQMain"; return "StartToTitle"; }`
   `ApplyModeNo(int)`（MAINSCRIPT.AS）是大厅里切模式用的脚本函数：`ModeChangeFixup` 保住主块，`SetMode`、`EntryModeImage`、`LoadSaveData_ModeSelect`、`UpdateAchievementCounter`、`UnlockDownloadContents`（插件的任务包存档迁移挂在这里，`airstrike.cpp` `0x70FF87`）。失败时它自己弹框，例程回标题，与原版「回到标题」相同。

任务列表、进度都由 `HQMain` 重新进入时按新模式的存档重建，插件不另外刷新。

## 2. 地址

| RVA | 作用 |
|---|---|
| `0x1804DD8` | `ui::HUiHQMain` vtable（RTTI `.?AVHUiHQMain@ui@@`），槽 1 `0x89F740` OnUpdate |
| `0x8DAD00` | `HUiBase::GetInput(this, ctx)`，被盖住 / 过渡中返回空 |
| `0x7E3180` | `(int code)`：写 UI 退出码 `0x20B39D8`（`GetUiExitCode` `0x70EA90` 读它）并发关闭请求 2；`ResultCallback` 的 `set_close_exit`（`0x84FA62`）调它 |
| `0x12946F0` | `createCoRoutine(out, const std::wstring* decl)`：`asGetActiveContext`（`0x11E5290`）→ `ctx->GetFunction(0)`（vtable `+0x148`）→ `->GetModule()`（`+0x30`）→ 声明经 `0x40890` 转窄串 → `GetFunctionByDecl`（`+0x68`） |
| `0x1AEE1F8` | `asCModule` vtable（AngelScript 2.28.2）：槽 6 `CompileFunction` `0x1233650`，槽 13 `GetFunctionByDecl` `0x1233B90` |
| `0x20B2890` | GameStatus：模式数组 `+0x20`、个数 `+0x30`、当前下标 `+0x38`；模式的 ModeList 项 8（在线）`+0x68`、9（内容）`+0x74`、10（类型）`+0x80` |

`tests/autopilot` 也钩 `createCoRoutine`（5 字节 jmp）。插件看到函数头已是 `E9` 时接在它后面（原目标当作下一跳），所以两者装载顺序无关。

## 3. 轮转规则

- 只在**离线剧情模式**之间转（项 8 = 0、项 10 = 0，即任务包对话框列出的那些），按 ModeList 顺序，首尾相接；在线模式永远不选。
- 未拥有的任务包跳过：判定与任务包对话框同一个（`0xD92B0`，插件给自己的任务包加的归属钩子 `edf5campaign.cpp` `OwnedHook<0>`）。
- 没有别的可选任务包时不动，只记一行日志 `PACKSWITCH ...: no other mission pack`。

## 4. 没做的部分

- **联机**：联机走待机房间，不经过 HQ 主菜单，房间的任务包在建房时定。房主在房间里切任务包并同步给客人，要走房间自己的换模式消息并更新房间属性（DLC / MISSION_NO / AF_PACK），另做。
- **世界地图**：HQ 默认自动打开世界地图选关（`HqAutoMissionSelect=1`），地图上 `Q` / `E`、`LB` / `RB` 已用来切整备页签，所以只有回到 HQ 主菜单时才能切任务包。
- **DLC 默认解锁**：没做。原版 DLC 任务包是付费内容，插件把未购买的 DLC 视作已拥有就是绕过购买，不做。本功能照原版归属判定跳过未拥有的包。
