# 任务脚本反馈：解析契约、难度语义与加载验证

用户要求参考 `mission-script-format-re.md` 做根因修复。本轮基于 `a904787e`，只读核验本机受支持的 EDF.dll（TimeDateStamp `0x678CCB46`）与游戏归档。以下是脚本工具的实际修复，不能据此宣称所有旧战役已迁移 AC 或已经通关。

## 已修复的实际问题

### 函数签名高位被误当作参数数

`bvm41.parse` 原先把函数表第四个 u32 全部当作参数数量。实际 EDF5 `_SetupBarrier` 的 `0x103` 和 `CruiseAndRespawn` 的 `0x104` 分别只有 3、4 个参数；旧实现误读成 259、260 个参数，会把后续字符串和填充字节当作类型，破坏静态调用分析与重建。

真实函数启动路径在 `0x10ACB0B/0x10ACB37`、`0x10ACC28/0x10ACC64` 按 signed byte 读取表项 `+0x0C`。`0x100` 签名函数的函数体留下一槽返回值，调用方通过 POP 或 STOREIND 消费；CALL `0x10AC1DB` 与 RET `0x10AC1FD` 的返回地址操作也与此一致。

修复将低字节参数数和高 24 位 `signature_flags` 分开保存，重建与 4.1 转换保留标志；CALL 分析纳入返回槽，返回平衡按函数结果数核对。仅对已确认的标志 0、`0x100` 分析，其他标志或 signed 参数数为负时明确拒绝推断，仍能保留容器字段。

### CFG 合流遗漏可达分支

`_walk` 过去只记录抵达指令的第一条路径。同一原生调用前两条分支分别传字符串和整数时，`type_check` 可能报告正常；两条分支传不同 SGO 时，也只扫描其中一个模型。

现在合并所有前驱直到数据流稳定：相同字面量保持精确；存在多个值时保留有限候选集合，不伪确定某条分支的值；类型检查逐候选核对。不一致栈深、栈下溢会明确报错，增长循环不会无限扩展。`edf41friends.spawns` 同步扫描全部候选 SGO，歧义的出生点保持未知。

### 已核实的固定倍率调用缺签名

native 22 只写 GameStatus `+0x44`，不读参数、不返回值、不改 `+0x4C` 的难度枚举；此前 NET6/SIG6 没登记它，教程分析在此止步。现在按真实 DLL 证据补充零参数、零返回和零栈效应。

146 个 EDF5 旧脚本的 213 个 native ID 中，明确签名现为 **115** 个，仍缺 **98** 个。这里是本项目静态签名覆盖范围，不能理解成引擎不支持这些调用，也不能当成完整 AC 映射表。

### AngelScript 实现地址定位错误

`edfre.script_impl` 原先从任意字节边界向前找最后一个 `lea rdx`，会把声明字符串地址当作代码地址。真实 DLL 上：`void SetMissionLevel(float )` 原返回 `0x17952F8`（数据），其余两个难度 API 返回空。

现在从 `.pdata` 函数边界解码，按实际 `asSFuncPtr` 注册块追踪代码指针并检查它在可执行区。实读正确结果为：

| 声明 | 实现 RVA |
|---|---:|
| `void SetMissionLevel(float )` | `0x1BA190` |
| `void SetMissionLevelFix()` | `0x1BA1A0` |
| `::Difficulty GetDifficulty()` | `0x1B4BB0` |

### 损坏文件可能让分析器不结束

UTF-16 字符串读取过去没有文件边界，无终止符会无限循环。现在字符串、指令长度、表区间、函数 stub 和参数区间都有界限检查，报错包含位置或具体原因。

## 可实际运行的只读审计

新增 `tools/audit_mission_scripts.py`，输出元数据，不导出原版脚本正文，不写游戏目录：

```powershell
python -B tools/audit_mission_scripts.py --mission EDF5_OLD_SCRIPT/M000 --mission EDF6/M046
python -B tools/audit_mission_scripts.py --edf5-candidates --root-only --json
```

可用 `--game <EDF6目录>` 指定游戏。默认资源范围是 Root.cpk 与 Mods，`--root-only` 忽略 Mods；报告明确记录此范围，不声称枚举了所有可能挂载的第三方归档。

审计内容包括：

- 按现有加载契约预测 AC/BVM 选择、来源与哈希。可打开的 AC 优先，即使文本为空或解析失败也不会退回同名 BVM。
- BEFORE 的 `.bvm` 存在性前置门槛；只有 BEFORE.AC 时明确报告受阻。
- 原生调用的函数名、指令序号、原文件偏移、签名与栈效应；初始化段也计入。
- native 20 与 AC `SetMissionLevel` 的队伍作用范围审查，以及 native 22 与 `SetMissionLevelFix` 的调用点差异。
- 现成 AC 候选的直接文件引用、A 系列语音标识、事件调用点、RMPA 和等级调用差异。调用点数量相同只代表静态计数相同。

真实 Root 中 44 份本篇 AC 候选均完成审计，0 个解析错误；M007、M034_5、M081 标为需要等级队伍范围核验。报告始终将当前编译、单人流程与多人流程标为未验证，不把历史 JSON 编译成功或集合相同当作行为等价。

## 验证与保留边界

- `test_as_registration.py`：5 项；`test_mission_script.py`：21 项；`test_bvm41_dataflow.py`：12 项，合计 **38 项通过**。
- 只读扫描 Root.cpk 中 252 个 BVM 与仓库 142 个 BVM，共 394 个，均能解析与运行静态分析；其中 30 个函数使用高位签名。
- 97,826 个 native 元数据偏移均与原始 opcode / ID 对应。
- 393 / 394 个文件 `build(parse(data))` 完全相同。M069 有既存的一字节布局差异，原 HEAD 实现同样复现；未将它计为通过。
- 真实 DLL 的三个 AngelScript 注册定位已核验；难度枚举、固定倍率、重置消费者的 170 项原生指令测试由 `mission_difficulty_native` 单独覆盖。

没有证据支持“全体 BVM 忽略难度”或“普遍固定标志泄漏”，因此没有强行删除 native 22，也没有把战役全局改成 AC。native 20 涉及的队伍范围仍不能与同名 AC API 直接等同。现成 AC 的逐关编译、任务推进、失败/重试、房主/客机同步，以及完整 BVM 到 AC 迁移仍未完成；本轮没有启动游戏或写入游戏目录。
