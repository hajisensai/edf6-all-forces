# EDF6 任务脚本格式与迁移边界（2026-10-18）

本记录区分三件事：文件格式识别、执行器兼容修复，以及旧任务到原生 AngelScript 的语义迁移。把 BVM 改名为 AC 不构成迁移；能解析或生成源码也不等于任务已经能够通关。

调查基线：仓库 `a904787e`；本机 EDF6 的 `Root.cpk` 与 `EDF.dll` 只读检查，DLL TimeDateStamp 为 `0x678CCB46`。本文不包含游戏原版脚本正文，不重新分发游戏资源。

可信度：**H** 为本轮读取文件或反汇编直接确认；**R** 为本仓库已有逆向记录，本文没有完整重跑；**待验证** 为尚未取得执行证据。

## 1. 两种格式确实不同

| 项目 | EDF4.1 / EDF5 BVM | EDF6 AC |
|---|---|---|
| 文件内容 | 自有 VM 的二进制程序，签名 `BVM ` | 本机样本为带 UTF-8 BOM 的 AngelScript 源码 |
| 表达方式 | 全局/函数表、指令、跳转、原生函数表编号 | 声明、类、对象值、命名 API、线程与事件辅助类型 |
| 字符串 | UTF-16LE 字符串池，指令保存池偏移 | 源码字符串与 `string` 类型 |
| 游戏绑定 | opcode `0x2C..0x2F` 加数字 native ID | 按声明注册的 AngelScript API |
| 现有项目能力 | `pylib/bvm41.py` 解析、重建、字符串改写、4.1 到 EDF5 BVM 转换 | `testrange/gen.py::script` 生成测试场的 AC 源码 |

**H：** 实读 `MISSION/EDF6/RM015B2/MISSION.AC` 与 `MISSION/EDF6/M046/MISSION.AC`，均为文本源码，并引用 `app:/Mission/AsCommon.h`。文件生成器标识为 `E2DtoMissionScript.exe`。`MISSION/ASCOMMON.H` 也确实存在于本机归档中。

**H：** `pylib/bvm41.py` 的格式说明和 `parse/build` 处理的是头长 `0x50`、版本 `0x01150138` 的 BVM：函数表、初始化代码、函数字节码与 UTF-16LE 池均有自己的偏移。该模块的 `to_edf5` 返回另一份 BVM，没有 AC 输出后端。

**R：** `docs/edf5-online-re.md` 记录 BVM 单步执行器 `MissionScriptBVMImplement` 在 `0x10AB600`，opcode 表在 `0x10AC49C`，native 分派器为 `0x20FF70`。这些地址只适用于上述 DLL 版本。

## 2. 本轮实际统计

通过 `pylib/rootcpk.py` 读取本机 `Root.cpk`，没有提取或写回游戏文件。

| 范围 | 文件数 |
|---|---:|
| `MISSION/` 子树全部 `.AC` | 549 |
| 其中 `MISSION/EDF6/` 子树全部 `.AC` | 193 |
| `MISSION/` 子树全部 `.BVM` | 252 |
| `MISSION/EDF5_OLD_SCRIPT/` 子树 `.BVM` | 146 |

这些是文件数，不能直接当作可玩关数；范围中可能包含测试或未被当前任务包收录的脚本。EDF6 子树的 193 个 AC 中，190 个文件名为 `MISSION.AC`，另有 3 个非标准名称的版本文件；先前把 190 标成全部 AC 的统计范围已更正。全体 AC 按第二级目录计数为 EDF6 193、TEST 101、EDF6_DLC 84、GOTOTEST 80、EDF5_NEW_SCRIPT 44、SAMPLE 30、SOUND_WORK 12、TGS 5，合计 549。

对上述 146 个 EDF5 旧脚本逐个执行 `bvm41.parse`，解析失败为 0。另按本轮任务包定义清单枚举（含需要补充资源、在某些机器上可能占位的任务），EDF5 为 139 个任务、EDF4.1 为 138 个任务，共 277 个主任务脚本；再加 4 个开场脚本，合计 281 个 BVM，均能解析。EDF5 脚本从本机 Root.cpk 读取，EDF4.1 脚本从仓库资源读取；这不代表它们已在每台机器上安装或可玩。归档存量与任务包定义是两个统计范围。

遍历上述 146 个 EDF5 旧脚本全部函数中的 native 指令后，得到：

| 指标 | 数量 |
|---|---:|
| 不同 `(native table, native ID)` | 213 |
| 存在于当前 `bvm41.SIG6` 的条目 | 114 |
| 未存在于当前 `bvm41.SIG6` 的条目 | 99 |

**这里的 99 是本项目明确参数签名的覆盖缺口，不代表游戏不支持这 99 个 native。** `SIG6` 记录的是已测执行路径读取的参数槽和返回数量，也不是完整的 AngelScript 等价实现表。现有 4.1 转换能工作的证据不能外推成完整 EDF5 到 AC 的覆盖证明。

可用以下独立统计代码复核，不输出游戏脚本正文：

```python
import sys
from collections import Counter
sys.path.insert(0, 'pylib')
import bvm41
import rootcpk

game = rootcpk.default()
mission = [(d, n) for d, n in game.cpk.index
           if d.upper().startswith('MISSION/')]
print(Counter(n.upper().rsplit('.', 1)[-1] for d, n in mission))
print('EDF6 AC', sum(d.upper().startswith('MISSION/EDF6/')
                    and n.upper().endswith('.AC') for d, n in mission))
legacy = [(d, n) for d, n in mission
          if d.upper().startswith('MISSION/EDF5_OLD_SCRIPT/')
          and n.upper().endswith('.BVM')]
natives = set()
for directory, name in legacy:
    program = bvm41.parse(game.read(directory, name))
    for function in program.funcs:
        for instruction in function.code:
            if instruction.op in bvm41.OP_NATIVE:
                natives.add((instruction.op, instruction.v))
known = natives & bvm41.SIG6.keys()
print('BVM files', len(legacy), 'native IDs', len(natives),
      'explicit signatures', len(known), 'missing', len(natives - known))
```

运行时使用 `python -B`，避免产生字节码缓存。统计依赖游戏归档和当前 `SIG6` 版本，后续修改后应重新记录数量。

## 3. ABI 与生命周期不能只按名字替换

### 3.1 原生调用的参数类型

**H：** 当前 BVM 参数签名只区分字符串指针、float、int 和未知槽。`SIG6` 注释明确说明 int 类同时包含数字、对象句柄和布尔。AC API 则存在 `Object`、`ObjectGroup`、`UI` 等类型；例如 DLL 内注册的 `CreateEnemy` 声明返回 `Object`，参数包含两个字符串引用、float 和 bool。

因此，把堆栈上的每个整数都生成为 AC 的 `int` 会丢失对象语义。转换器需要按 native 的实际读写恢复值类型、返回值、对象所有权/引用关系与缺省参数；缺失映射时应报告具体函数和指令位置，不能吞掉调用。

### 3.2 任务框架与线程

**H：** 上述 AC 样本定义事件状态、互斥量和游戏/语音同步计数器。其入口依次完成事件初始化、资源预载、玩家资源预载、预载等待、在线开始同步、进入用户任务代码，以及对应语音线程的完成等待。`testrange/gen.py::script` 已采用这一类框架。

这证明项目有生成新 AC 的基础；它不能单独证明 BVM 的事件注册、重复事件、等待条件、语音顺序、任务中止和结算已经等价映射。无结构跳转转成可编译源码也只是语法层面的完成。

**R：** `docs/edf5-online-re.md` 已记录两类执行器的玩家对象布局和全灭处理不同：BVM 对象内玩家表与 AS 绑定对象内玩家表不是同一个 ABI，AS 的创建流程还会登记全灭事件。因此不能把一个 AS native 的函数地址普遍拿来替代同名 BVM native；每个替代都需要核对调用约定和上下文。

### 3.3 已确认的加载选择与失败边界

**H：** 主任务入口 `0x70F20B` 调用选择器 `0x1F68D0`。选择器先用 `0x444D0` 去掉输入扩展名，在 `0x1F6A33` 拼 `.ac`，由 `0x1F6B2A` 调 `0x50E20` 检查能否打开。成功则在 `0x1F6CE4` 构造 AS 执行器（构造函数 `0x1DD520`）；只有这次文件打开检查失败，才在 `0x1F6B94` 拼 `.bvm`，于 `0x1F6C9F` 构造 BVM 执行器（`0x20DC10`）。因此相同基本路径下可打开的 AC 优先。

**H：** 选择后 `0x1F6DC2` 调执行器 vtable `+0x10` 加载，并无“编译失败后再选 BVM”的分支。AS 加载函数 `0x1DDF50` 在 `0x1DE167` 调 `0x1DEEC0`，失败从 `0x1DE16E` 到 `0x1DE235` 收尾，不会在该路径再构造 BVM。不能靠同时放一份 BVM 来兜底错误的 AC。

**H：** 开场入口 `0x70DBE3` 使用相同选择器，但之前 `0x70DAD2` 会先检查 `/before.bvm` 能否打开，失败在 `0x70DB21` 跳过开场。静态上仅有 `BEFORE.AC` 会被前置门槛拦住；这一行为尚未做游戏内受控验收。

[BlueAmulet EDFModLoader v1.0.10 源码](https://raw.githubusercontent.com/BlueAmulet/EDFModLoader/v1.0.10/EDFModLoader/dllmain.cpp) 的 `crifsio_hook`（248–269 行）只在 `Mods` 同路径文件存在时改写路径，再调用原打开函数；它不改扩展名，也没有打开或编译失败后重新读取 Root 的重试。结合游戏的 AC 优先选择，同基本路径 Root 中已有 AC 时，仅添加 Mods 的同名 BVM 不会取得优先权。

仍待验证：AS 编译缓存的失效规则、加载失败时的具体界面表现，以及开场脚本门槛在实机中的完整流程。上述选择顺序来自静态控制流，不能代替任务成功加载、胜负或重试验收。

## 4. 难度路径的最终核验

### 4.1 难度枚举与固定倍率是两个状态

**H：** 以相同 `EDF.dll` 版本追踪声明注册、BVM 分派及消费者，确认 GameStatus 的 `+0x4C` 是当前难度枚举，`+0x44` 是固定倍率标志。不能把设置固定倍率描述为把难度枚举重置为简单。

| 调用 | BVM 路径 | AC 路径 | 确认的操作 |
|---|---|---|---|
| 读取难度 | native 21，`0x210DD8..0x210DF8` | 注册 `0x1E7A88` → `0x1B4BB0` → `0x1DA2D0` | 两者读取同一个 GS `+0x4C` |
| 固定任务倍率 | native 22，`0x210E10` | `SetMissionLevelFix`，`0x1BA1A0` | 两者只写 GS `+0x44 = 1`，不改 GS `+0x4C` |

本轮用 Unicorn 执行这些真实函数的相关指令，对难度枚举 0..4 得到一致读数。它验证的是指令级读写语义，不能代替游戏中菜单、资源创建、战斗和多人流程的验收。

**H：** 倍率计算器 `0xD7AE0` 消费 GS `+0x44`：未固定位时按当前模式的难度区间与任务进度计算；固定位时使用固定倍率。本机读出的模式编号 65..71 的五档难度区间并不相同。配置与计算路径不支持“所有 BVM 任务都会忽略难度”的推断。

### 4.2 重置点存在，没有得到普遍状态泄漏的证据

**H：** BVM 重置函数 `0x214F10` 在 `0x215060` 清 GS `+0x44`，在 `0x21506E` 把 BVM 对象的任务倍率字段 `+0x49C` 还原为 1。以下生命周期路径都调用它：

| 路径 | 调用现场 RVA |
|---|---|
| 加载函数 `0x222A70` 的入口 | `0x222AA2` |
| 析构 | `0x20EA2D` |
| 结束路径 | `0x22A35C`、`0x22A397` |

因此，本轮假设过的“BVM 不清固定标志，导致后续任务普遍固定难度”没有得到支持。不能基于这一假设给全体 BVM 加一个强制忽略 native 22 的补丁。

**H：** 扫描任务包定义对应的 281 个 BVM，固定调用 native 22 只在 EDF5 `M000` 出现一次，对应本篇第 1 关教程。扫描到的 AC 固定调用位于 `EDF5_NEW_SCRIPT/M000`、`EDF6/RM000`、`TEST/T_FADE` 三个文件。固定等级是两种执行器都提供的脚本能力，原版教程的有意固定应保留。

这些静态与指令级结果不证明每关的难度手感正确。仍需按具体报告中的任务、模式、难度和对象，沿实际有效等级与 HP/攻击参数继续核对。

### 4.3 同名等级 API 仍有语义差异

**H：** BVM native 20 在 `0x210DB5` 把参数原始 32 位写入 BVM 对象 `+0x49C`。分派器当时的 `r14` 为对象 `+0x30`，所以指令中的相对偏移是 `+0x46C`。多个 BVM 创建函数会乘此字段；例如 `0x20F820` 读取该倍率，随后 `0x20F895` 设置队伍 3、`0x20F8AB` 设置等级。

**H：** AC 的 `SetMissionLevel(float)` 在 `0x1BA190` 从绑定对象 `+8` 取得任务对象，写其 `+0x284`，这是 enemy team 1 的倍率。BVM 单字段倍率涉及的队伍范围更广，所以把 native 20 一律映射成同名 AC API，不能认为已经精确等价。转换器应按调用上下文保留有效等级，而不是只按函数名字建立映射。

前述 AS 注册现场分别在 `0x1E793E`、`0x1E79E3`；`GetDifficulty` 的函数指针装载在 `0x1E7A88`。这些形式不符合 `tools/edfre.py::script_impl` 的简化匹配模式，应以实际反汇编为准。

### 4.4 已确认的局部缩放缺陷与回归边界

`src/edf41mother401.cpp` 的噬地者移植曾把子部件等级系数固定为 1，导致这些部件的 HP 与攻击参数不随船体有效等级正确缩放。当前基线已有修复：记录船体有效等级，再按其等级 / 10 计算部件；这是具体对象移植代码的问题，不能归因于使用 BVM 后缀。详见 `docs/feedback-2026-10-17-eartheater.md` 及对应源码中的等级与部件初始化路径。

本轮新增的 `tests/mission_difficulty_native_test.cpp` 已实际通过 **170 checks**：在不调用入口点的私有 DLL 映射中运行真实难度/固定倍率指令，覆盖五档难度 × 三个任务进度、BVM/AS 一致性、固定倍率后难度枚举不变，以及真实 reset 写入片段恢复倍率。测试还核对上述 4 个生命周期调用现场确实指向 reset。它没有运行完整任务加载/析构/结算链，也不证明单人或多人游戏体验；这些验收仍独立记录。

## 5. 可行的分阶段迁移

### 5.1 优先核验 44 份现成 AC 候选

**H：** 本机 `Root.cpk` 的 `MISSION/EDF5_NEW_SCRIPT/` 有 44 份 `MISSION.AC`，全部与 `EDF5_OLD_SCRIPT` 的任务 ID 对应，且全部属于 `edf5campaign/missions.json` 的本篇清单，即 **44 / 110 个本篇任务**。这是一条优先于新建通用转译器的研究路径，不是已经提供给用户的后端开关或自动切换机制。

44 个目录都同时具有 `MISSION.AC`、`MISSION.JSON` 和 `MISSION.RMPA`。对应新旧目录的 44 份 RMPA 全部逐字节相同，脚本直接地图路径集合也 44 / 44 相同。去掉 AC 注释后，从字符串字面量提取的直接 `app:/` 文件引用，在本机 `Root.cpk`、`Chunk01.cpk`、`Chunk02.cpk` 的并集内全部存在；这只验证直接引用，未遍历每个 SGO 的依赖闭包或实际播放所有语音。

配套 JSON 的历史编译 `result` 全部为 true，合计有 15 条 Warning。它们是随游戏提供的构建诊断，不能当作本轮或当前 Mod 环境重新编译成功。

静态比较也显示了必须逐关解释的差异：

| 比较项 | 已确认的差异 | 不能据此直接推断的内容 |
|---|---|---|
| 语音标识字面量集合 | 12 个任务的 AC 比同 ID BVM 少合计 42 个不同标识，未发现新增标识 | 字符串数不直接等于实际少播了多少台词；仍需追踪可达路径、共同辅助代码和播放调度 |
| 静态事件注册调用数 | M000 为 53→51、M007 为 66→62、M029 为 86→76、M032_5 为 44→42、M035 为 87→86；其余 39 个任务相同 | 事件合并/移除是否等价，不能只按个数断定 |
| 对象 SGO 直接引用 | M000、M071、M072_2、M079、M080、M091_4 共 6 个任务存在增减，涉及军曹/队长类资源 | 引用差异可能来自预载或辅助函数调整，不足以直接证明生成对象和行为相同 |
| 等级倍率 API | M007、M034_5、M081 同时涉及 BVM native 20 与 AC 同名等级 API | 见 §4.3，两者涉及的队伍范围并不天然相同 |

语音标识比较仅匹配脚本中的 A 系列标识字符串，不声称已经证明全部语音 API、动态生成标识或资源索引相同。事件比较是 BVM native 2 与 AC `internal_CreateEvent` 的静态调用点数量。两者均属于筛查证据，不是行为等价证明。

因此可先为这 44 个任务建立差异清单、解释原生脚本版本差异，再逐关验证编译、单人任务推进、失败/重试和多人同步。未经这些核验，不能把任务列表整体指向 NEW_SCRIPT。其余 66 个本篇、DLC 和 EDF4.1 任务也不在这 44 份候选的覆盖范围中。

### 5.2 迁移实施阶段

1. **先审查现成 AC 并补齐运行契约。** 优先完成上述 44 关的逐关语义对照；确认 AC 缓存/错误处理和开场门槛。建立同一小型测试任务在 BVM 和 AC 下的加载、事件、胜负和重试观测方法。完成条件是明确知道实际运行了哪份程序，不能只看文件已经写入。
2. **建立 native 清单。** 以实际收录任务为范围列出每个 native 的调用点，补齐参数、结果类型和 AS 等价关系。区分直接等价、需要包装、仅能由插件补充、仍未知四种情况。未知项阻止对应任务迁移；不要把全局 213 个 ID 的统计当作某个任务的迁移结果。
3. **实现独立 AC 输出后端。** 复用 BVM 的解析结构，建立有类型的中间表示、控制流和事件模型，再输出 AC。保留来源 BVM 指令位置和映射版本，便于定位。不得通过删除无法翻译的调用获得可编译文件。
4. **先迁移小范围样本。** 挑选依赖少的任务，补充包含分支、循环、重复事件、语音等待、生成器、对象消亡和胜负路径的测试样本。离线验证编译和执行跟踪；有游戏绑定的地方需要真实引擎证据。
5. **逐任务发布与验收。** 每个任务记录转换版本、原生覆盖、游戏编译结果、单人通关/失败/重试，以及多人房主/客人同步结果。只有达到已约定验收范围的任务才切换默认执行器，安装与卸载也要覆盖旧资源清理和回退。

证据层级应分别报告：解析成功、输出确定、静态类型/控制流检查通过、游戏实际选中 AC、游戏编译成功、单人流程验收、多人流程验收。任何前一级都不能替代后一级。

本轮成果是难度路径的根因审计与回归防护，没有依据未经证明的状态泄漏假设修改运行时。现阶段新自定义关卡可以复用现有 AC 生成框架；已有战役的全量 AC 迁移尚未实现，需要新增转译器和上述验收体系。未来发现具体执行器缺陷时，应按证据修复对应契约，并与迁移结果分别报告。

## 6. 上游核验

2026-10-18 在线查看作者仓库：

- [AUK233/edf-tools](https://github.com/AUK233/edf-tools) 的支持格式列出 `.txt` 到 `.bvm`，没有列出 BVM 到 AC。该工具能处理旧脚本，不构成存在完整 AC 转译能力的证据。
- [KCreator/Earth-Defence-Force-Documentation](https://github.com/KCreator/Earth-Defence-Force-Documentation) 提供 BVM 命令、示例和旧代引擎研究。可用作研究线索，不能当作 EDF6 原生 AC ABI 或全部任务兼容性的证明。

本轮未找到可直接接入项目、覆盖上述 native 和生命周期的现成 BVM 到 EDF6 AC 转换工具。这是本次检索结果，不是对所有未检索工具的不存在证明。

## 7. 本轮落地与验证

- 新增 `mission_difficulty_native` 并纳入 `offline_checks` / CTest；真实 DLL 上 170 项断言通过。
- 插件全量构建与 `offline_checks` 构建通过。
- 最终 CTest 显式排除与本轮脚本无关的 `cockpit_build`：300 项通过，3 项按原契约跳过（`allclass_census`、`katyusha_mount`、`sight_data_audit`），0 失败。先前含驾驶舱资产生成的基线运行被主动中止，不作为全量通过证据。
- 安装链 `python -B tools/selftest.py`：287 / 287 通过。

本轮没有修改关卡或执行器运行逻辑，没有写游戏目录，也没有启动游戏。上述结果是构建、离线回归和指令级证据；完整战役 AC 迁移、逐关战斗及多人验收仍未完成。
