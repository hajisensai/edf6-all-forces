# 源码守卫改造（design-review-1126 P7 第 1 步，2026-11-27）

目标：守卫守的是「结构 / 数值 / 行为」，不是写法。改名、换行、加注释不该让守卫变红；守卫的意图一条不丢。
本轮不碰读 `src/hud.cpp` 的断言（另一任务负责）。

## 1. 一个 C++ 读法：`pylib/srcscan.py`

以前 `tools/selftest.py` 和 `tests/*_guard.py` 各自带一份「去 // 注释」「按 `\n}\n` 截函数体」的函数，规则互不相同
（有的把 `"http://..."` 里的 `//` 当注释，有的把字面量里的 `{` 算进括号）。现在统一用 `pylib/srcscan.py`
（单测 `tests/srcscan_test.py`，ctest `srcscan`）：

| 函数 | 作用 |
|---|---|
| `code(text)` | 去注释，字面量原样保留（字符串、字符、原始字符串里的 `//`、`/*` 不算注释） |
| `masked(text)` | `code()` 再把字面量内容涂成空格（等长）：括号、名字只在代码里算 |
| `body(text, head, regex=, braces=)` | 定义的函数体，括号配平；跳过声明（`{` 前有 `;`） |
| `definitions / functions / function` | 文件里的函数定义（命名空间 / 类层级） |
| `calls / mentions / callers / order` | 调用某名字、哪些函数调用它、调用先后 |
| `before(text, *parts)` / `precedes(a, b)` | 顺序：依次出现 / `a` 首次出现早于 `b` 首次出现（闸门在活之前） |
| `defines / value` | 是否定义某名字、它的初始化式（同一语句的第二个声明符也能取） |
| `has(text, phrase)` | `phrase in text` 的替代：去掉注释，token 之间空白随意，字面量逐字匹配 |
| `takes(text, name, 'pyconst::kX')` | `name` 取自生成的常量（初始化式就是它，或 `using pyconst::kX;`） |

### 守卫里被替换掉的自带函数（意图不变，结果逐项比对过：改前改后各跑一遍全部守卫，输出一致）

| 文件 | 原自带函数 | 现在 |
|---|---|---|
| tests/allclass_guard.py、dreadnought_slot_guard.py、unmanned_launch_guard.py | `code_only`、`body` | `srcscan.code`、`srcscan.body(regex=True, braces=False)` |
| tests/doll_follow_guard.py、support_exit_guard.py | 同上 + `functions_calling` | 同上 + `srcscan.callers` |
| tests/debug_spawn_launch_guard.py、npc_car_aim_guard.py | `code_only` | `srcscan.code` |
| tests/debug_spawn_vehicle_level_guard.py、player_identity_guard.py | `code_only`、`body` | `srcscan.code`、`srcscan.body` |
| tests/npc_backpack_guard.py | `code_only`、`body`、`before`（依次） | `srcscan.code / body / before` |
| tests/online_gate_guard.py | `code_only`、`body`、`before`（首次早于首次） | `srcscan.code / body / precedes` |
| tests/frame_rate_net_guard.py | `code`、`body` | `srcscan.code`、`srcscan.body(regex=True, braces=False)` |
| tests/frame_rate_off_guard.py | `code` | `srcscan.code` |
| tests/module_registry_guard.py、room_ui_wiring_guard.py | `code`、`body` | `srcscan.code`；`body` 包一层：找不到函数直接 AssertionError |
| tests/redirect_owner_guard.py、coop_attr_guard.py | `body` | `srcscan.body` |
| tests/proteus_net_guard.py、sazabi_net_guard.py、rescue_support_guard.py | 从 tests/ 互相 import 剥离函数 | `from srcscan import body, code`（rescue 的 `before` → `precedes`） |
| tests/frame_pacing_install_guard.py | 按 `\n}\n` 正则截 5 个函数体；数补丁点用原文 | `srcscan.body(braces=False)`；`GateSitesAsRead` 问 `FramePacingTickWaitInstalled` 改 `srcscan.calls`；补丁点按 `masked` 文本数调用 |
| tests/enemy_control_monster_net_native_audit.py | `without_comments` | `srcscan.code` |
| tests/enemy_control_native_pose_audit.py、input_source_guard_test.py | 自带「去注释 + 涂字面量」 | `srcscan.masked` |
| tests/keyconfig_native_audit.py、support_resource_audit.py、test_emplacement.py | 行内 `re.sub(r'//.*', '', x)` | `srcscan.code(x)` |
| tools/selftest.py（18 处） | 行内 `re.sub(r'//[^\n]*', '', x)` / `re.sub(r'//.*', '', x)` | `srcscan.code(x)` |

## 2. 「C++ 常量 == Python 值」→ 生成头 `src/py_consts.inc`

`tools/gen_py_consts.py` 从 `pylib/vcobjects.py` / `pylib/drill_model.py` 生成 `src/py_consts.inc`（`namespace pyconst`，
`inline constexpr`，不加静态数据）。C++ 改成取 `pyconst::kX`，不再手抄字面量。`--check`（进 `tools/check.py GENERATED`
和 CI `build.yml`）：文件过时，或有常量没有任何源码引用（又被手抄回去），都退出 1。selftest 只剩结构断言
`srcscan.takes(...)`。

| 原断言（tools/selftest.py） | 原比较 | 现在的守卫 |
|---|---|---|
| sazabi_bones_agree | `kSazabiMark={SAZABI_MARK:.1f}f` 字面 | sazabi.cpp `kSazabiMark=pyconst::kSazabiMark`（takes）+ gen --check |
| dreadnought_agrees、hand_copies_agree | `kDreadnoughtMark=` / `kKeel=` 字面 | dreadnought.cpp 取 `pyconst::kDreadnoughtMark / kDreadnoughtKeel`（takes）+ gen --check |
| drill_copies_agree | `kDrillBone[]=L"..."`、`kSpinBone[]=`、`kDrillLength/Radius`、`kDrillBaseY/Z`、`kSpinRepeat=2π/16` 正则 | drill.cpp `using pyconst::kDrillBone/kSpinBone`，长度 / 半径 / 基点取 pyconst（takes），`kSpinRepeat` 的初始化式是 `2.0f*kPi/pyconst::kDrillFolds`（value）；`DRILL_BASE[0]==0` 留在 Python 断言 |
| drill_copies_agree | jet_bay.cpp `kDrillChargeFile[] / kDrillChargeSgo[]` 字面 | `using pyconst::kDrillChargeFile / kDrillChargeSgo`（takes） |
| emc_copies_agree | `kBeamRange`、`kSightThin`（及 `kSightThick > EMC_SIGHT_SIZE`）、`kSgoBlastRadius` 正则 | 取 `pyconst::kEmcBeamRange / kEmcSightSize / kEmcBlastRadius`（takes）；「变粗」改为 emc.cpp `static_assert(kSightThick>kSightThin)`；`EMC_BEAM_RANGE == SPEED×LIFE` 仍是 Python 断言 |
| ultimate_copies_agree | `kRingBlast`、`kSparkRadius`、`kGatherReach`、`kBoltFrames` 正则 | 取 `pyconst::kUltBlastRadius / kUltSparkRadius / kUltGatherReach / kUltBoltRounds`（takes） |
| sortie_copies_agree | `kRainRoundSpeed={SALVO_CHILD_SPEED*60:.1f}f`、glow `kMark=`、salvo `kRounds/kVolley/kVolleyGap` 字面 | 取 `pyconst::kSalvoChildSpeed / kGlowMark / kSalvoRounds / kSalvoVolley / kSalvoVolleyGap`（takes）；`SALVO_VOLLEY % 4 == 0` 仍断言（C++ 侧本有 static_assert） |

剩余仍是手抄 + 文本比对的常量见第 5 节。

## 3. 「这段代码在那个函数里」→ `srcscan.has`

`tools/selftest.py` 里不读 `src/hud.cpp` 的测试中，形如 `'<C++ 片段>' in X`（`X` 是 `src('src/…')` 等 C/C++ 源码，或测试里
直接从它赋值的名字；片段含 `( ; { } -> ::`）的断言，机械改为 `srcscan.has(X, '<片段>')`（`not in` → `not srcscan.has`）。
片段原样保留，所以守的东西不变；差别只在：注释里的片段不再算数，换行和空格不再让它变红。共 285 处、61 个测试
（`cv.py` 的 AST 转换，清单可由 `git diff` 复现）。

## 4. 不稳定用例

| 用例 | 原因 | 修法 |
|---|---|---|
| tests/testpool_test.py `test_width_and_caps` | 靠 sleep 让 4 个任务「碰巧」同时在跑；还会在别的检查占核时排队 | 条件变量闸门：4 个都到了才一起放行，断言峰值；`checklock.core` 打桩为空上下文 |
| tests/live_set_test.cpp | 用真 `Sleep` 越过 `memoepoch::kStallMs`，机器忙时边界抖 | `LiveSet` 加 `AddAt / HasAt(nowMs)`，测试用自己的时钟走到 `kStallMs-1` 与 `kStallMs` |

另：`map_commands_wired` 在基线（13eb6257）就红：wave 1 把地图指令键挪进 ini（`Map<名>Key`），断言还找
`int vk[count]={'Q',...}`。改为：按 `map_keys.h Of()` 的字段顺序取 `tools/config_keys.py` 的默认值，必须是断箭那套
`Q E R T U F H Z X C V B Tab F1-F4`。

另：`infantry_scope_pip_default_migrates` 漏了 `@test`，从来没跑过（单独调用是绿的），已补上。

## 5. 没做完的（下一步）

- selftest 里仍有约 700 条片段断言没动：读 `src/hud.cpp` 的 339 条（另一任务）；其余是对 README / ini / CMake /
  Python 文件的文本断言（`srcscan.has` 不适用于非 C++，去注释会误伤 `https://`），以及多行片段、正则断言。
- 还没把「代码在函数里」改成 `*_logic.h` 上的行为 ctest：本轮只做到结构化（`has` / `takes` / `calls`）。
- 仍手抄的 Python 常量：`body506.cpp kMarks` 表项、`kHullFront / kBoxHalfX` 这类与 Python 只做不等式比较的值、
  其余 src 里注释写着「pylib/xx.py CONST」的约 8 处，没进 `py_consts.inc`。
- 9 个改用 srcscan 的守卫（coop_attr、doll_follow、frame_rate_net、input_source、online_gate(_bridge)、player_identity、
  redirect_owner、rescue_support）因为 `sys.path.insert(..'pylib')` + glob，被 `test_domains.py` 记成依赖 `pylib/*`，
  多挂了 cockpit / edf41 / edf5 / installer / worldmap 域：只会多跑，不会漏跑；精确到 `pylib/srcscan.py` 要改 test_domains 的推断。
