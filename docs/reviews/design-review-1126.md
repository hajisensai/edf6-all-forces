# EDF6VehicleCrew 设计审查与重构建议（2026-11-26，只读审查）

基线：worktree `air-support-mechanics-be3867`，HEAD `99f29bfc`，工作区干净。行号都按这个提交。
「已验证」表示 grep / 读代码 / 跑脚本得到的结果；「推测」会单独标出来。

## 1. 现状地图

### 1.1 规模
- `src/` 有 750 个 .cpp/.h/.inc，共 195,267 行；autoturret 和 common 另算。
- 按文件名前缀粗分子系统（脚本按正则归组，只是近似）：

| 子系统 | 文件 | 行数 | 代表文件 |
|---|---|---|---|
| 载具 / 座位 / 其他（未归类） | 246 | 51k | crew.cpp, ground, seat_*, turretcam, payload, drill, proteus, sazabi, dreadnought, vehsound |
| 支援呼叫与出击 | 85 | 33k | support_dispatch, airstrike, jet*, heli, subcarrier, transport, airdrop, barrage, ultimate, sortie_plan.h, calls.inc |
| 敌军操控 / PvP | 120 | 25k | enemy_control*（102 个文件）, pvp* |
| 整备 / HUD / UI | 57 | 21k | hud.cpp, room_ui, armory, worldmap, loadout, keyconfig, weapon_equip, ui_text |
| 渲染 / 视角 | 61 | 17k | gpufx/, thermal, scope_*, firstperson, cockpit, frame_* |
| EDF4.1 移植 | 39 | 16k | edf41mother / mother401 / robo / classes / ai / net |
| NPC AI | 23 | 9k | npcai.cpp, npc_*, ground_navigation |
| 核心 | 34 | 8k | crew.h, plugin.cpp, mission.cpp, crew.cpp |
| 联机通道 | 55 | 7.5k | plugin_net, support_net, seat_net, *_net |
| 地图指挥 | 26 | 6k | mapcmd, map, commander, command_* |
| EDF5 移植 | 4 | 1.4k | edf5campaign, edf5online |

### 1.2 依赖关系（跨组 #include 数，已验证，脚本统计）
- 所有组都依赖核心：载具→核心 154，敌控→核心 85，支援→核心 56，EDF4.1→核心 40。
- 核心也反向依赖各业务组：核心→载具 60，→支援 24，→渲染 18，→UI 17，→敌控 16。原因是 `src/crew.h:1667-1681` 这个总头文件在末尾 include 了 core/ground/heli/hud/nix/vhud/mapbounds/payload/map/damagestats/mapcmd/proteus/npcai/qmark/infantryscope，而它被 230 个文件 include。
- 支援和联机互相依赖：支援→联机 32，联机→支援 24。`support_net.cpp` 同时负责支援协议和全部插件通道的注册。
- 实际结构是「crew.h 把所有东西连在一起」，没有分层。

### 1.3 最大的 15 个文件（已验证，wc -l）
| 行数 | 文件 |
|---|---|
| 6269 | src/hud.cpp |
| 4688 | src/npcai.cpp |
| 4214 | src/heli.cpp |
| 3001 | src/weapon_series.inc |
| 2837 | src/firstperson.cpp |
| 2596 | src/edf41robo.cpp |
| 2590 | src/edf41mother.cpp |
| 2258 | src/support_dispatch.cpp |
| 2231 | src/edf41mother401.cpp |
| 2214 | src/playerjet.cpp |
| 2150 | src/gpufx/gpufx.cpp |
| 2011 | src/mapcmd.cpp |
| 1755 | src/worldmap.cpp |
| 1689 | src/crew.h |
| 1575 | src/subcarrier.cpp |

最长的函数（脚本按「行首 `}`」切分，近似）：

| 行数 | 函数 | 位置 |
|---|---|---|
| 645 | LoadConfig | plugin.cpp:543 |
| 331 | MapCommandFrame | mapcmd.cpp:1414 |
| 305 | Plan | support_dispatch.cpp:517 |
| 287 | Validate | plugin.cpp:228 |
| 253 | SupportDispatchTick | support_dispatch.cpp:1969 |
| 211 | HudDraw | hud.cpp:6046 |

## 2. 设计问题清单（按严重度排列，每条都有证据）

### P1 对象引用没有统一模型，存活判定写了 14 份且标准各不相同（严重度：高，容易出 bug）
- `crew.h:854-862` 的 `ObjRef` 是**不持有**引用的 {obj, ctrl}，注释写的是「Read under the caller's __try (the object may be gone)」。也就是说，它靠读一个可能已经释放的控制块、再加 SEH 兜底来判断对象是否还活着。
- EDF4.1 那边另有一套正确的做法：`edf41classes.h:242-246` 的 `WeakRef` 持有 weak count（`WeakSet` / `WeakReset` / `WeakAlive`）。两套模型并存。
- `bool Alive/Live(const ObjRef&)` 在 14 个地方各自实现，判定条件不一样：
  - `airdrop.cpp:67`：use count > 0 + `Is()`，**不查** kDead
  - `commander.cpp:55`：只查 kDead，**不查** use count 和删除标志 0x18&4，还自带 `__try`
  - `enemy_control.cpp:105`：use count + 0x18&4 + kDead + selfCtrl
  - `jet.cpp:286`：use count + kObjDeleted，**不查** kDead
  - `mission_crew_support.cpp:31`：只查 kDead
- 影响：同一个对象在 A 模块看是死的、在 B 模块看是活的；地址被复用、控制块被释放后的误判，全部依赖 SEH 兜底。崩溃和「对象不见了却还在被驱动」这类 bug 很可能出在这里（推测，没有逐例复现）。

### P2 逐帧调度是手写的长序列，SEH 吞掉异常后下一帧照跑（严重度：高，问题被掩盖）
- `crew.cpp:960-1046` 的 `InputHook<I>` 对每辆载具每帧按固定顺序跑约 45 步；`crew.cpp:924-958` 的 `SharedFrameTick` 每帧跑约 30 个 tick，全文件共 78 处 `Guarded*` 调用。步骤之间的先后依赖只写在注释里（例如「before the HUD's publish」「after AimLines」）。
- `crew.cpp:742-765`：`StepFault` 记一条日志（每步每 10 秒最多一条），之后 `EXCEPTION_EXECUTE_HANDLER`，日志原文是「the step is skipped, the others go on」，下一帧继续执行同一步。这和项目规则「不吞异常」相冲突：一步写到一半就 AV 时，已经写进游戏结构的半截状态会留下来，后面的步骤会在这个状态上继续跑。
- 故障 ID 复用，日志会标错来源：`kStepUnderground` 同时给 UnderPlayer、ExitGroundTick、BigWorldProbe、PlayAreaTick 用（crew.cpp:929,930,939,940）；`kStepRescue` 给了 SupportDispatchTick（941）；`kStepPayload` 给了 SeatTurretsInput（972）。
- 「插件关闭时也要跑」的步骤和 `if(Cfg().enabled)` 判断交错写在同一个函数里（crew.cpp:964-999）。JetSoundTick 在 955 和 986 各调一次，靠它内部的每帧去重保证只跑一次。
- 全仓库有 222 个文件、1320 处 `__try`（npcai.cpp 49 处，enemy_control_deiroi_net.cpp 34 处）。

### P3 生命周期（Install / Reset / Frame）没有注册表，靠三处手写清单（严重度：高，改动成本高）
- 安装：`plugin.cpp:1344-1469` 顺序调用 119 个 `Install*`，顺序约束写在注释里（「before the application's init」「last: it chains call sites…」）。返回值约定混用：有的是 void，有的是 `Log("X=%d",Install())`，还有 `if(!InstallCrew())`；另有一段按 heli profile 走条件分支（plugin.cpp:1366）。
- 复位：`mission.cpp:129-215` 的 `MissionStart` 手写了约 70 个 `Reset*()`。而联机通道的 reset 已经通过 `plugin_net::Channel.reset` 注册表来调（support_net.cpp:189-215），例如 `ResetCommandNetwork`、`ResetSubSupportNetwork`、`ResetCarrierDroneNetwork` 只经由注册表调用，同一个仓库里有两种机制。
- 逐帧：见 P2。
- 影响：新功能要在 4 处登记（Install、Reset、Frame 步骤、kStepNames）。漏掉 Reset 就会把上一关的状态带到下一关，而且没有任何静态检查能发现。

### P4 `crew.h` 是总头文件，导致改一处要重编和重测几乎所有东西（严重度：高，测试慢）
- 被 230 个文件 include，自身 1689 行（Config 结构 22-577、EDF.dll 布局常量、ObjRef、约 400 个函数声明），末尾还拉进 15 个子系统头（1667-1681）。
- `tools/test_domains.py:71` 把整个 `src/` 当成一个域 `plugin-native`；`test_domains.py:438-439` 不追踪 src 内部的 include（「the plugin's own sources: plugin-native already」）。结果 `tests/test_labels.json` 里 716 个 ctest 中有 607 个标成 plugin-native，任何 src 改动都会选中约 607 个 ctest。
- 不拆 crew.h，按 include 图细分测试域就没有意义，因为几乎每个文件都间接 include 了全部头。

### P5 配置是一份手写的四处同步表（严重度：中高，改动成本高）
- 同一个键写在 4 到 5 处：`Config` 结构（crew.h:22-577，约 550 行）、`LoadConfig`（plugin.cpp:543，645 行，498 处 Read*）、`Validate`（plugin.cpp:228，287 行）、`EDF6VehicleCrew.ini`（491 个键）、README。键名字符串在 Read 和 Fix 里各写一遍，例如 plugin.cpp:552 `FixInt("CrewDelayMs",ReadInt(L"CrewDelayMs",…))`。
- 目前靠 `tools/selftest.py:1882-1886` 用正则检查「ini 里有这个键」。范围检查和默认值是否一致没有任何守卫。
- 同类问题：Python 常量手工抄进 C++ 后用 selftest 逐字比对，例如 `sortie_plan.h` 的 `kRainRoundSpeed=660` 对应 `pylib/vcobjects.py SALVO_CHILD_SPEED`，守卫是 `selftest.py:2096` 的 f-string 字面匹配。src 里写着「pylib/xx.py CONST」「tools/xx.py CONST」的引用有 33 处。

### P6 委托方点名的 `sortie_plan.h` 与 `calls.py` 手工对齐：属实，但已有守卫，严重度比委托方说的低（中）
- `SortiePass` 枚举手写在 `crew.h:991`，Python 侧是 `tools/calls.py:134` 的 `SORTIE_PASSES`；BARRAGE_KINDS 和 `barrage_plan.h` 也是同样关系。`gen_calls.py:44,56` 用 `index()` 生成序号，`calls.inc` 本身是生成的。
- 但 `selftest.py:2053-2054`（以及 barrage 的 2012-2014）会用正则把 C++ 枚举和 Python 元组逐项比对，顺序错了会红。所以这里不是「无保护的手工对齐」。要说问题，是「本该生成的东西靠正则守卫」：把这两个枚举交给 gen_calls.py 生成就可以删掉守卫，属于低成本收尾。
- `sortie_plan.h` 本身是纯 constexpr 逻辑头，有 `tools/sortie_check.cpp` 做离线检查，设计上没问题。

### P7 测试和源码文本耦合，会直接抬高重构成本（严重度：高，任何重构的前置障碍）
- `tools/selftest.py` 有 16,255 行，`src('src/…')` 读源码 419 处，形如 `assert '<C++ 片段>' in <源码>` 的断言 369 处，例如 2115 行 `'salvo ? sortie::kRainRoundSpeed : ' in combat`。另外 28 个 `tests/*_guard.py` 也是对源码做文本匹配，其中 12 个各自复制了一份「去掉 // 注释」的函数。
- 影响：改名、拆文件、改格式都会让大量守卫变红，而它们守护的是写法，不是行为。不先处理这一层，下面任何一步重构都会被几十条伪红拖住。

### P8 联机同步有 5 种传输方式（严重度：中，容易出 bug，新人难上手）
1. `plugin_net` 通道（13 个，support_net.cpp:189-215）：有 magic、takesState、reset、joined，是推荐路径。但 pvp 通道没有 magic，靠嗅探字节 `'AFPV'` 识别（support_net.cpp:182-185）。
2. `seat_net` tag15：挂在各载具类 NetworkObject 的 slot 17 上。drill / proteus / sazabi 各自硬编码 `kReceiveSlot` RVA（drill_net.cpp:9、proteus_net.cpp:10、sazabi_net.cpp:10）。
3. `edf41net` tag12：复制 vtable 后改 slot 17（edf41net.h:40，edf41net.cpp:67,130）。机制和 2 相同，却是另写的一套。
4. `coop_attr` 成员属性。
5. `room_sync_logic.h:324` 把配置行分片编码。
- enemy_control 的 9 个 `*_net.cpp` 都在同一个 "enemy control" 通道里再做一层多路复用。
- 2 和 3 合并成一个「对象消息」层、pvp 补上 magic，收益明确；1、4、5 的用途不同，可以保留。

### P9 打补丁原语已经有了，但仍有 7 处手写 VirtualProtect（严重度：中）
- `common/edf/patch.h` 的 `edf::PatchCode` 和 `code_owner.h` 的 ClaimCode 所有权登记都已存在，`aiworld_capacity.cpp:12` 等也在用。
- 下面几处是手写的：
  - `physics.cpp:16-30` 重新实现了一个签名不同的 `PatchCode`
  - `edf5online.cpp:247` `WriteImage`
  - `enemy_control_pvp_berserker.cpp:671-702` 两段几乎相同的「call 到 thunk + nop」
  - `enemy_control_timeship_pods_net.cpp:484-494`
  - `crash_dump.cpp:358`、`edf41natives.cpp:79,134`、`viewdiag.cpp:130`
- 其中 berserker 先 FlushInstructionCache 再还原保护，而且还原失败时直接 return false，此时代码已经改了、状态记录却没写。属于小缺陷（已读代码确认，未实测）。
- 「hook 安装散落」的说法只对一半：所有权登记是统一的，缺的是「call→thunk」「改 N 字节」这类高层原语，以及 P3 说的安装注册表。

### P10 单文件过大且职责混杂（严重度：中，改动成本高、合并冲突多）
- `hud.cpp`（6269 行）一个 TU 里装了：文字基础设施（403/516）、载荷与座位提示（722/862）、战机挂载（1133）、俯仰梯（1881）、直升机面板（2422）、Sazabi（2551/2667）、原版 HUD 与弹药格（3273/3615）、**地图**罗盘 / 图钉 / 小队面板 / 指挥卡（4012/4139/4314/5408）、救援横幅（5752）、步兵瞄具（5901）。地图 UI 本应属于 mapcmd 子系统。
- `heli.cpp:462-517` 的 `struct Heli` 有约 26 个 bool 字段（`called, guard, leaving, reap, extend, orbiting, cmdMoving, ownGuard, medic…`），是一个隐式状态机：合法状态组合没有定义，「离场中又被指挥移动」这类组合只能靠逐个 if 处理。`jet_internal.h:517-579` 的 `struct Jet` 也有约 20 个 bool。transport 有显式 `Phase`（transport_logic.h:29），airdrop 和 playerjet 也有，各家写法不同。

### P11 UI 文本限宽是硬裁切，没有测量、省略号、缩放或换行（严重度：中，横切缺陷）
- `ui_text.cpp:198-206`：超过 `maxW` 时直接改 scissor 边界裁掉。`worldmap.cpp:1297-1301` 又自写了一个 `Text()`，用 UV 裁切，默认 `maxW=4000`。
- `hud.cpp:342-348`：`Line{ wchar_t text[128] }` 配合 `_vsnwprintf_s(...,_TRUNCATE)` 静默截断。`kMaxLines=256`（343）满了就丢弃，`*at>=kMaxLines` 检查在 hud.cpp 里手写了十多处（823/832/845/878/974/998/1048…）。
- 头文件里没有任何文本宽度测量 API，也没有「文本放得下」的测试。四种语言的文本（hudtext.inc 1231 行）长度差别很大，中文以外的语言最容易被裁（推测，未逐屏核对）。
- 文本栈有两套：GDI 栅格化的 `ui_text`（worldmap / armory）和游戏字形 `hudtext/glyphs`（hud）。两套各有理由（ui_text.h:7-10 写了原因），但限宽策略应该统一。

### 不构成问题或证据不足（不列入重构）
- 全局可变状态：仓库里确实很多（粗略 grep：文件作用域的非 const 定义约 1400 处，`[kXxxMost]` 固定槽数组 97 处），但基本都在匿名 namespace 里，只在游戏线程访问；`Cfg()` 用的是 `std::atomic<const Config*>` 发布（plugin.cpp:87-90），没有看到数据竞争。单独为它重构收益不大，随 P3 的模块化顺带收进各模块即可。
- EDF4.1 几个敌人文件骨架相似（mother 和 mother401 有 22 个同名函数），但已经共用 `edf41classes.cpp:221 BuildClone`，剩下的是各类型自己的逻辑，强行抽象收益低。

## 3. 重构路线（按收益/风险排序）

每一步都能独立合并。前一步的护栏是后一步的前提。

### 第 1 步：把源码文本守卫改成行为测试或结构化守卫（先做，解除对后续步骤的阻塞）
- 范围：只改 `tools/selftest.py`、`tests/*_guard.py`，不碰 src。
- 做法：
  - 「A 常量等于 Python 某值」这类断言改成「生成头 + `--check`」（见第 3 步），或者改成 C++ `static_assert` 配合由 Python 生成的 `.inc`。
  - 「某段代码在某函数里」这类断言改成对应 `*_logic.h` 的离线 ctest；做不到的保留，但抽一个公用的 `pylib/srcscan.py`（去注释、取函数体），替换 12 份复制的实现。
  - 每 PR 处理 30 到 50 条，先处理 P10 / P3 将要动到的那几个文件对应的断言。
- 护栏：转换前后对同一提交跑 `check.py --full`，被删的断言要有对应的新测试或生成检查。
- 规模：约 369 条断言，分 6 到 8 个 PR，每个几百行。风险低，不动游戏代码。

### 第 2 步：统一对象引用：一个 `ObjRef` 与一个 `Alive()`（解决 P1）
- 范围：`crew.h` 的 ObjRef、`edf41classes.h` 的 WeakRef、14 处 Alive/Live。
- 做法：
  - 在 common 或 `src/objref.h` 中定义唯一的存活判定：use count > 0、!(0x18&4)、!kDead、selfCtrl 匹配，判定条件以汇编结论为准，写进 `docs/*-re.md`。
  - 需要跨帧持有的对象改用持有 weak count 的引用（沿用 WeakRef 的实现），这样读已释放控制块的情况就根本不会发生。
  - 各文件的 Alive 先改成转调新函数（行为差异逐个审：原来不查 kDead 的地方是有意为之还是漏了），然后删掉旧实现。
- 护栏：为 objref 写离线 ctest，覆盖伪造控制块的各种组合；实机用 autopilot 跑一局「呼叫支援后被击落 + 换关」，看 FAULT 日志的计数。
- 规模：约 15 个文件，每处几行，再加一个新头。风险中等：语义收紧后，某些原本「当作活着」的对象会被放弃，需要逐处判断。

### 第 3 步：配置表和枚举单一来源生成（解决 P5、P6）
- 范围：新增 `tools/config_keys.py`（键、类型、默认值、范围、注释），由它生成 `src/config.inc`（结构字段 + Load + Validate）、`EDF6VehicleCrew.ini` 和 README 段落；`SortiePass`、Barrage `Kind` 交给 `gen_calls.py` 输出。
- 做法：先让生成器输出与现有代码逐字等价的结果，再按键分批迁移。有特殊逻辑的键（例如 Validate 里依赖别的键的 `HeliCombatRange`）留一个手写钩子。
- 护栏：gen `--check` 接入 CI；ini 来回读写测试，默认值等于生成表。
- 规模：一个新生成器约 300 行，删掉 LoadConfig / Validate 约 900 行，ini 改成生成。风险低到中，行为可以逐字对比。
- 进展（2026-11-27，分支 `wave1-config`）：已做。`tools/config_keys.py` 一张表（500 键 + 11 个退役键），`tools/gen_config.py` 生成 `src/config.inc`（`EDF6_CONFIG_FIELDS` / `_LOAD` / `_VALIDATE` / `_RETIRED` 四段）和 `EDF6VehicleCrew.ini`；手写只留跨键逻辑（FrameRateCap、HudLanguage、机库、TestBoardVehicle、日志路径、任务包、ViewDistance）。`SortiePass`、Barrage `Kind` 由 `gen_calls.py` 生成 `src/sortie_pass.inc` / `src/barrage_kind.inc`。`--check` 接入 `tools/check.py` 与 CI；往返测试 `tests/config_roundtrip_test.py`。README 没有配置键表（只有一句指向 ini），所以不生成 README 段落。

### 第 4 步：模块注册表：Install / Reset / Frame 一处登记（解决 P2、P3）
- 范围：新增 `src/module.h`：`struct Module{ name; install; reset; tick; vehicleStep; phase; runsWhenDisabled; }`。`plugin.cpp` 的 119 个 Install、`mission.cpp` 的约 70 个 Reset、`crew.cpp` 的 78 个 Guarded 改成从注册表遍历。
- 做法：
  - 顺序用显式的 `phase` 枚举表达（kPreInit / kPhysics / kAircraft / kUi / kLast…），替代注释。先按现有顺序逐字搬过来，保证行为不变。
  - 每个模块独立拥有一个故障 ID，消除 P2 的 ID 复用。
  - 故障策略改成：同一步连续出错 N 次后禁用该模块本局运行，写 crash 报告并在 HUD 提示，不再每帧重试吞掉。这一条是行为变化，单独开 PR。
  - 联机通道的 `Channel.reset` 并入同一套注册表。
- 护栏：
  - ctest 断言注册表里每个模块都有 reset（有状态的模块必须提供，不能为空）。
  - 源码守卫：`mission.cpp` 和 `plugin.cpp` 里不再出现裸的 `Reset*()` / `Install*()`，这类结构化守卫值得保留。
  - autopilot 跑两关连续换关，对比日志里各模块的 install / reset 行。
- 规模：动 3 个核心文件，加上每个模块 1 到 3 行登记，共约 1500 行改动。风险中等，搬迁阶段做到逐字等价可以把风险压低。

### 第 5 步：拆 `crew.h` 总头，按子系统细分测试域（解决 P4）
- 范围：
  - crew.h 拆成 `edf_layout.h`（RVA 和偏移常量）、`objref.h`、`config.h`（第 3 步生成）、`log.h`（已有），以及各子系统头。
  - 删除 crew.h:1667-1681 的反向 include，230 个包含者改为只 include 自己需要的头。
  - `test_domains.py` 开始追踪 src 内部的 include 图，并把 `plugin-native` 拆成子系统域。
- 做法：先保留 crew.h 作为转发头（只含 include），然后一个子系统一个 PR 地改 include，最后删掉转发头。
- 护栏：`/WX` 构建本身就是护栏；`test_domains.py --check`；对比拆分前后 `--select src/heli.cpp` 选中的测试数（应该从约 607 降下来）。
- 规模：机械改动，跨 230 个文件，适合派 worker 批量做。风险低，主要成本是合并冲突，需要和在飞的 PR 错开。
- 进展（2026-11-27，分支 `wave3-crewh`）：已做，一次做完，没有按子系统分多个 PR。
  - 头文件拆分：crew.h 已删除，声明拆进 `edf_layout.h`、`config.h`、`plugin_log.h`、`scene.h`、`world.h`、`view.h`、`seats.h`、`jet.h`、`playerjet.h`、`sound.h`、`patches.h`、`edf41.h` 等 36 个新头；`objref.h` 是第 2 步就有的。反向 include 已去掉，每个文件只包含自己用到的头，`/WX` 构建通过。selftest 和守卫改读这些新头。
  - `test_domains.py` 改动一：C++ 用例的依赖。原来只有 ninja 有记录的目标（101 / 435）能拿到依赖，其余目标退回整个 `plugin-native` 域。现在 build 里没有记录的目标，改用它源文件的静态 `#include` 闭包：用该对象自己的 `-I` 目录，各个 `#if` 分支都算进去，遇到宏 include 就当未知。给 labels 生成依赖时一律用静态闭包。在 ninja 有记录的 101 个目标上核对过，静态闭包与 ninja 记录完全一致（2321 个文件里缺 0、多 0）。
  - `test_domains.py` 改动二：`plugin-native` 拆成 `plugin-enemy` / `plugin-ports` / `plugin-support` / `plugin-air` / `plugin-hud` / `plugin-net` / `plugin-sound` / `plugin-core` 八个子系统域。剩下的 `plugin-native` 只管 common、CMake、autoturret、third_party 和测试 / 工具的 C++。
  - 改动前后 `--select` 选中的用例数（ctest + selftest）：
    - `src/heli.cpp`：415 + 80 = 495 → 63 + 72 = 135
    - `src/mapcmd.cpp`：409 + 75 = 484 → 57 + 67 = 124
    - `src/hud_map.cpp`：410 + 75 = 485 → 60 + 66 = 126
    - `src/objref.h`：420 + 75 = 495 → 199 + 67 = 266
    - 改动前的数字是在 ninja 只记录了 101 个目标的 build 上测的。

### 第 6 步：拆 `hud.cpp`，统一文本限宽（解决 P10 前半、P11）
- 范围：
  - hud.cpp 按 2.P10 列出的块拆成 `hud_text.cpp`（Line / Text 基础设施）、`hud_vehicle.cpp`、`hud_air.cpp`、`hud_map.cpp`（4012-5751 整段，归 mapcmd）、`hud_scope.cpp`。
  - 新增文本布局 API：`Fit(text, maxW, policy={ellipsis|shrink|wrap})`。ui_text 和 worldmap 的两个 Text 都改走它；`kMaxLines` 的检查收进 `LineSink::Push()`，溢出时记一次日志而不是静默丢。
- 护栏：新增离线测试，按 hudtext.inc 里全部 4 种语言的每条文本，用 GDI 测量后断言能放进各自的槽位宽度，这一项正好能抓到现在的硬裁问题。hud 拆分本身用 autopilot 截图对比。
- 规模：hud.cpp 搬运约 6000 行，新 API 约 300 行。风险中低（纯搬运加一处策略变化）。

### 第 7 步：联机对象消息层合并（解决 P8）
- 范围：`seat_net`（tag15）与 `edf41net`（tag12）合并成一个 `object_net`，各类的 slot 17 hook 由它统一安装（drill / proteus / sazabi 的 `kReceiveSlot` 改成登记表）；pvp 通道补上 magic。
- 前提：改 wire 就要升 `kAllForcesRoomProtocol`，按项目规则直接拒绝旧版本，不写兼容分支。
- 护栏：现有 `*_net_test.cpp` 加上 tests/gamenet 不开游戏的联机测试；新增「同一对象两种 tag 消息互不干扰」用例。
- 规模：约 6 个文件，1000 行左右。风险中等（联机），需要一次双机实测。
- 状态（2026-11-27，分支 `wave1-objnet`）：已做。`src/object_net.h`（共用头、Gate、种类登记表 `Kinds`、类表 `kClasses`）+ `src/object_net.cpp`
  （每个链上的表一个接收钩子、一个发送者身份与序号、slot 16 发送、开发回环）；drill / proteus / sazabi / dreadnought 改为 `ChainClass`，
  `edf41net` 改为对象消息的 `E41N` 种类（tag 12 → 15，头版本 1 → 2，不再复制虚表）；pvp 通道补 magic `AFPV`（`6d2b1281`）。
  按任务要求没有升 `kAllForcesRoomProtocol`（23）：改了线格式的只有 `E41N`，旧版本插件的原版接收对 tag 15 只读类型就返回，不会误解析。
  测试：`object_net_test`（登记表、类表、两种消息同一对象互不干扰）、`edf41net_test`、四个 native 测试；双机实测未做。

### 第 8 步：直升机和战机的状态显式化（解决 P10 后半）
- 范围：`heli.cpp` 的 `struct Heli` 和 `jet_internal.h` 的 `struct Jet` 里描述「任务阶段」的那几个 bool（called / guard / leaving / reap / extend / orbiting / cmdMoving…）收成 `enum class Phase` 加一个 `Transition()` 函数，放进 `heli_logic.h` / `jet_logic.h`，可离线测试。飞控学习量（hover、k、params）不动。
- 做法：先加 `Phase Derive(const Heli&)` 只读推导，跑一段时间核对日志，确认推导结果和现有 bool 一致后再切换写入路径。
- 护栏：转移表的离线 ctest（非法转移直接拒绝）；autopilot 跑一局「呼叫、指挥移动、离场」。
- 规模：每个文件改动约 500 行。风险中高（飞行行为），排在最后，并且要在第 2、4 步之后做。
- 已做（分支 `wave1-phases`）：
  - 直升机：`src/heli_logic.h` 的 `heliphase::State`（phase：follow / guard / ferry / ferryLand / leaving / gone，加上值守 duty、指令前的值守 own）取代 guard / leaving / reap / ferry / ferryLand / ownGuard 六个 bool，heli.cpp 只经 `Enter()` → `Transition()` 改它。called / keep / medic（属性）、extend（交战战术）、orbiting / orbitSet（飞控）、cmdMoving（与战机共用的 `AirCommandTransit` 滞回）不是阶段，留在原处。
  - 战机：`src/jet_logic.h` 的 `jetphase::Phase`（fly / ferry / recover / withdraw）取代 `Mode::withdraw`、`Mode::recover` 和 ferry / ferryGone，jet*.cpp 只经 `EnterPhase()` 改它；退场 / 回收时 Mode 停在 patrol，由 Guide 先按阶段飞（Away / Recover）。reap（待删除）与阶段正交，仍是 bool。
  - `HeliCommand` / `JetCommand` 签名不变。
  - 离线护栏：`heli_phase` / `jet_phase` 两个 ctest，覆盖转移表（含拒绝）、`Derive()` 遍历旧代码能产生的全部 bool 组合，并用随机操作序列把旧模型和新转移逐步对照。
  - 被改判的隐式组合（其余都与旧行为一致）：
    - 直升机运送中受损退场：旧代码不清 ferry，会悬停或降落在投放点，`HeliFerry(nullptr)` 也结束不了；现在退场时丢下运送。
    - 战机退场 / 回收中再 `JetFerry`：旧代码接受，但 ferry 被盖住不起作用；现在拒绝。
    - 退场中的前运送机 `JetFerryDone`：现在返回 false，调用方随即 `JetWithdrawNow`，本来就在退场。
    - 取消退场（玩家上炮手位、机库放飞）后：现在不再接着运送。
    - 运送机最后一程：在同一帧由 `Ferry()` 直接置 reap，取代 ferryGone 这个中间态。
  - 仍待实机：autopilot 跑一局「呼叫、指挥移动、离场」。

### 第 9 步（低成本收尾）：剩余手写补丁并入 `edf::PatchCode` / 新的 `RedirectToThunk`（解决 P9）
- 范围：physics.cpp、edf5online.cpp、berserker、timeship、edf41natives、viewdiag、crash_dump（crash_dump 在加载锁内运行，确认新原语不取锁后再迁）。
- 护栏：已有 `redirect_owner_guard.py` 和 `edf5_online_native_test` 的回滚用例。
- 规模：约 200 行。风险低。

### 明确不建议动的部分
- `*_logic.h` 纯逻辑头加离线 ctest 的模式（39 个文件）：这是仓库里最好的部分，后续步骤应该往这个方向靠，不要改它。
- `calls.py` → `gen_calls.py` → `calls.inc` 生成链：方向正确，只需要把剩下的枚举也纳入。
- `plugin_net` 通道注册表、`edf::code_owner` 代码所有权登记、`Cfg()` 的原子发布：设计合理，只扩展，不重写。
- gpufx / thermal 渲染子系统：内部边界清楚，依赖少（渲染→核心 36），这次重构没有理由碰。
- EDF4.1 各敌人文件：已经共用 BuildClone，剩下的差异属于各类型本身。
- 各 hook 的 RVA 和字节签名：只换调用它们的框架，不改逆向结论本身。
- 不要一次性大爆炸：每一步都要能单独合并，`check.py` 保持绿，实机部分按 CLAUDE.md 攒成一次运行。

### 建议顺序
1 → 3 → 2 → 4 → 5 → 6 → 9 → 7 → 8。1 和 3 风险最低，并且为后续步骤清障；2 和 4 收益最大；5 只有在 4 完成后才能真正缩短测试时间；7 和 8 涉及联机和飞行行为，放在最后。
