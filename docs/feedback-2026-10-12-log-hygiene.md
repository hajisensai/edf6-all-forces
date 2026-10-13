# 2026-10-12 回传日志：刷屏日志与崩溃转储来源

## 用户原话

> 直升机的水面判定日志一局刷了 2.6 万行，把回传的 12 MB 日志挤满了。修
> 回传日志时附带的崩溃转储没核对来源，可能把别的旧转储打包进去：有必要修吗，有必要就修吧

## 1. 刷屏日志

测试者回传的 `EDF6VehicleCrew.log`（`tools/testhub.py pull`，报告 #3–#12）按行首前缀统计。发行版 ini 默认 `Debug=1`，
所以测试者机器上调试日志全开。

| 来源 | 报告 | 行数 / 总行数 | 根因 |
|---|---|---|---|
| `WATER`（`src/heli.cpp` `LogSea`） | #11 | 26 363 / 31 723 | 注释写的是「每种不同答案只记一次」，实现却只和**上一次**比。直升机在岸边时探测在水面 / 陆地间来回切换，每切一次写一行。 |
| `JET v=… gun N: dir=…`（`src/jet_combat.cpp` `JetLog`） | #6 | 18 672 / 58 835 | 每架飞机每秒的状态行后面都附带每把武器的枪管对机头方向（最多 8 行）。这些几何对同一架飞机是不变的。 |
| `NPCAI … weapons unreadable, left stock`（`src/npcai.cpp` `Think`） | #6 | 1 580 / 58 835（`NPCAI Ranger` 共 11 892 行，其余是有节流的 2 秒状态行） | 同一个士兵的武器一直读不出，这个状态不会变，却每 2 秒每人写一行。 |

此外，`WATER` 的陆地分支格式有错：格式串是 `%s%.1f`，陆地时前缀为空字符串，于是输出成 `…on the map0.0`。

### 改动

- `src/log_once.h` 新增 `SeenOnce<N>`：记住出现过的答案，同一答案只返回一次「新」。表满时只写一行「further ones not logged」，之后只计数，不会悄悄丢掉。
- `LogSea`：
  - 按（水面 / 陆地，水域数，水面高度按 0.5 m 取整）作为键，每种答案每局只记一次。`ResetHelis`（任务开始）时清空，每张地图仍会把自己的答案记一遍。
  - 陆地分支单独写格式，不再拼出 `map0.0`。
  - 诊断本身保留：它仍然回答「水域列表在运行时的布局、水面高度」这两个未验证的问题。
- 枪管几何：拆成 `JetBarrelLog`，每架飞机只记一次（`Jet::barrelsLogged`）。座位上还没有武器时不算已记，之后读到武器时再记。
- `NPCAI … weapons unreadable`：每段「读不出」的状态只记一次。武器恢复可读后清零，下次进入时再记。原来的 2 秒节流仍保留，状态来回抖动时不会比以前刷得更多。

其它前缀（`BIGWORLD probe` 957 行、`SUPPORT TRACE` 708 行、`JET … gun gate` 每 250 ms 一行等）都是有节流的状态跟踪，量级小一个数量级，这次不动。

## 2. 崩溃转储来源

报告 #6（10-10 14:55）附带的 `crash/EDF6.exe.5468.dmp` 就是报告 #5（10-07 22:13）的那份：Windows 的 `LocalDumps`
保留了它，而 `collect()` 只看「三天内最新的一份」。转储模块表里的插件是 PE `0x6AC5DF87`，#6 当时装的是 `0x6AC9C50B`。

### 改动（`tools/testhub.py`）

- `dump_modules`：读 minidump 的 ModuleListStream（类型 4，`MINIDUMP_MODULE` 108 字节一行），得到每个模块的完整路径、TimeDateStamp 和 SizeOfImage。
- `dump_matches`：判断转储是否属于本次安装，满足三条才算：
  1. 转储里的 `EDF6.exe` 来自当前游戏目录；
  2. 它加载过的每个 `Mods/Plugins/*.dll`，PE TimeDateStamp 和 SizeOfImage 都与现在装的这份一致；
  3. 插件还没加载就崩的转储，没有可比对的东西，算作本次安装。
- `collect()`：从新到旧找第一份属于本次安装的转储上传。插件自己写的卡死转储（`*.hang.dmp`）同样核对。
- 新增 `crash/dumps.txt`：列出每份候选转储的时间和结论（「本次安装」或「来源不符，未上传：原因」）。没上传的也有记录，不会无声消失。

## 验证

- `tests/log_once_test.cpp`（CTest `log_once`）：两种答案来回切换 10000 次只记 2 行；表满时只报一次；`Clear` 重新开始。变异实测：把 `SeenOnce` 改回「只和上一次比」，11 项检查红 3 项。
- `tests/testhub_dump_source_test.py`（CTest `testhub_dump_source`）：用合成的 minidump 与 PE 头覆盖以下场景：
  - #6 的情形：只有旧版插件的转储 → 不上传，`dumps.txt` 写明原因；
  - 本次安装的转储比旧转储更早 → 只上传本次安装的那份；
  - 来自另一个游戏目录 → 拒绝；
  - 插件没加载就崩 → 保留；
  - 非 minidump 文件 → 拒绝。

  本机若有报告 #5 的真实转储，还会核对能读出其中插件的 `0x6AC5DF87`。变异实测：让 `dump_matches` 恒判通过，8 项检查红 5 项。
- 构建 `build.cmd` 加 `offline_checks` 后，`ctest -j 8`：227/227 通过（需要原生 EDF.dll 的 native 用例按设计跳过）。

## 未验证

- 没进游戏重放岸边直升机的场景；日志行数的下降是按代码路径推出来的，没有实测。
- 安装器 exe 里打包的是 `tools/testhub.py`：要等 PR 合入后重新出安装器，测试者的菜单 4 才会用上新逻辑。
