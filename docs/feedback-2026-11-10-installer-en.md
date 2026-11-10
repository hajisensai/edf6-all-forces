# 2026-11-10 安装器英文版与安装提速

## 原话

> 你做完再打包，同时优化一下打包、安装速度。同时支持一下英文版本

## 英文版：做了什么

- 语言规则（`pylib/i18n.py`）：环境变量 `EDF6_LANG`（`zh` / `en`）优先；没有时看 Windows 显示语言
  （`GetUserDefaultUILanguage` 的主语言是中文 `0x04` → 中文，其它 → 英文）。命令行 `--lang zh|en`
  （`take_lang_arg`）写进 `EDF6_LANG`，安装器开的子进程（模型生成进程、生成器 exe）跟着用同一种语言。
- 安装器 `tools/installer.py` 的菜单、提问、进度、错误提示，菜单 7 / 8（`support_config.py` / `support_loadout.py`）
  的武器名、席位名、挂载名和编辑器，原版启动（`vanilla_launch.py`），测试站反馈（`testhub.py` 里给玩家看的部分，
  它只用标准库，自带 `_tr`），模型包 / EDF4.1 素材包检查（`modelpack.py`、`edf41src.py`），EDF5 战役状态
  （`make_edf5_campaign.py` 的 `check` / `summary`、`edf5_dlc.py`），EDF4.1 地图 / 声音 / 本部背景与 X-Morph 地图的进度行，
  模型生成进程的进度（`model_workers.py`）都有英文。
- 发布包（`build_release.py`）里除了中文 `说明.txt` 再放英文 `README.txt`。
- `tools/selftest.py` 的 `installer_speaks_english`：AST 检查上述模块里 `print` / `ask` / `say` / `SystemExit` /
  `Invalid` / `HubError` / `GameRunning` 的中文字面量都包在 `tr(中文, English)` 里、英文那一边没有中文；
  再分别用 `--lang en` / `--lang zh` 起一次安装器，核对横幅和菜单。自测与 ctest 固定 `EDF6_LANG=zh`
  （CI 的 windows-latest 是英文系统，原有断言按中文写）。

## 英文版：故意不译的

- **写进游戏数据的文字**：EDF5 战役 / 测试场的任务简报、跳过原因（`make_edf5_campaign.placeholder`、
  edf41_maps / objects / sounds 与 edf41src 的 problems 进简报）、ini 里的标记行（`ADDED_HEADER`、`DEFAULTS_MARK*`）。
  这些若随安装器语言变化，中文和英文系统装出的文件就不一样，联机房间要求各机文件一致，会被拒绝进房。
- **给开发者检索的固定关键字**：`REFUSED`、`EDF6VC_*`（如 `EDF6VC_SAVE_RESTORE busy`、`EDF6VC_WORKERS_OK`）。
- 生成器内部的 `ValueError` 文本、`gen.py`（测试场）的错误、`testhub.py` 的崩溃转储判定文字（给开发者看）、
  生成器在模型里打印的逐步进度（`[OBJECT/...] 座舱：……`）仍是中文。

## 安装提速：怎么测的

不写游戏目录：`tmp/perfgame*` 里只有指向本机游戏 `Root.cpk` / `Chunk01.cpk` / `Chunk02.cpk` / `DX11.cpk` /
`EDF.dll` 的符号链接（只读）和一份 `EDF6.exe` 副本，安装器把 `Mods` 写进这个替身目录。16 逻辑核，Python 3.12。
每行输出打时间戳，找最长的空档。

## 安装提速：找到的根因

1. **资源组一个接一个生成**：60 多个资源组（瞄具、战机、各履带载具、X-Morph 单位、无畏舰、C-130、大地图……）
   在安装器进程里依次生成，每组内部最多再开 2 个进程，16 核大部分时间空着。
   → `installer.build_assets`：要生成的组每组一个进程（`buildcache.build_group`），同时
   `model_workers.group_workers()` 组（每 4 核一组、每组按 3 GiB 可用内存、最多 4 组；小机器退回原来的逐组）。
   生成阶段只读，全部生成完才写，写入顺序和「生成失败什么都不写」不变；任一组失败，没开始的组取消，错误原样抛出。
2. **瞄具组每次更新都整组重建**：缓存判定只数瞄具组账本里的 `.MRAB`，而它的输出还有金色载具炮的 `WEAPON/*.RAB`，
   输出集合永远对不上 → 52 个瞄具模型每次更新都重新生成（约 2 分钟）。
   → `buildcache.Cache.current` 对瞄具按 `make_optics` 的 models 范围（MRAB + RAB）判定。
3. **测试场每次安装都把 31 种履带载具 / X-Morph 载具的模型与碰撞串行重建一遍**：
   - `_write_derived` 对每种履带载具调 `build()`（含座舱嫁接），而安装器刚刚生成或复用了同样的文件；
     → 安装器缓存说该组是最新的、账本里该组拥有测试场需要的全部文件时，测试场只登记「需要」，不重建。
   - 测试场自己的载具 SGO 需要碰撞绑定（`make_tracked_combat._assets` 建模型和碰撞，每种几秒到一分钟），
     在 `has_mission_setup` 检查里逐个串行生成；
     → `gen._make_ground_sgos` 先把这些 SGO 并行生成（同样 `group_workers()` 个进程），本次安装内复用。
4. **输入摘要重复计算**：每个用座舱的组各自把座舱部件（62 MB）和共用的模型目录重新算一遍 SHA-256（全部组的输入摘要合计约 5.5 秒）。
   → `buildcache._file_sha` 按（路径、大小、修改时间）在一次安装内只算一遍（判定结果不变，只省重复计算）。
5. 并行实测时发现：组进程里再跑模型任务（单进程路径）时，`_run` 对已被替换的 stdout 调 `isatty` 崩溃
   → `_TaggedOutput.isatty`，`tests/test_model_workers.py` `test_jobs_inside_a_job` 覆盖。

## 安装提速：实测（秒）

| 场景 | 改前 | 改后 |
|---|---|---|
| 首次安装：生成阶段（到「全部生成完毕」） | 1743 | 339 |
| 首次安装：写入 + 测试场 | 617（见注 1） | 324 |
| 首次安装：合计 | 约 2360（见注 1） | 663 |
| 更新（全部已缓存）：合计 | 804（见注 2） | 206 |
| 检查（菜单 5） | 9.2 | 10.0 |

- 注 1：改前的首次安装在写入阶段因替身目录缺 `DX11.cpk` 中断（EDF4.1 材质要读它），写入 + 测试场的 617 秒取自
  只加了资源组并行、还没修 2、3 的版本（写入与测试场代码与改前相同）。
- 注 2：取自同一版本（只加了资源组并行）：更新时只有瞄具组重建，资源组并行对它无影响，增量路径与改前一致。
- 改后的首次安装与随后的一次更新，`Mods` 下 2595 个文件除三个账本 / 缓存清单外逐字节相同；测试场 SGO 并行生成前后
  的同一次更新，2595 个文件逐字节相同。改前中断的那次已写出的 1177 个文件，与改后相比只有 5 个 `WEAPONTEXT.*.SGO`
  与两个清单不同，差异是之后步骤（EDF6AutoTurret、普罗透斯）才改写的说明行，改前那次没走到。
- 内存：首次安装峰值时安装器及其全部子进程私有内存合计 5.8 GB，其中安装器本身 4.5 GB（各组生成的文件写入前都留在内存里）；
  更新时 2.4 GB。

## 未做 / 未验证

- **没有开游戏**：英文界面、并行生成出的文件都没有在游戏里看过；文件与改前逐字节相同是离线比对的结论。
- 打包（`build_release.py` / 分享包）速度没有动：这次只量了安装，没有量打包。
- 冻结后的安装器 exe 没有在英文 Windows 上实际跑过；`--lang en` 只在源码运行时测过。
- 生成器在模型里逐步打印的进度、生成器内部错误、测试场错误文本仍是中文（见上）。
