# Agent 规则细则

[CLAUDE.md](../CLAUDE.md) 只放每轮都要遵守的规则，这里放展开的做法和背景。CLAUDE.md 里提到「见细则」的条目，具体内容在这里。

## 子代理与额度

- 子代理类型定义在 `.claude/agents/`，每种只带实际用到的工具，省得每轮都重读工具说明。模型由 `tools/claude_hooks/subagent_model_router.py` 按类型定死，`general-purpose` 和省略类型会被改派到 `general`。workflow 里的 `agent()` 由 `tools/claude_hooks/workflow_agent_type_default.py` 默认补上 `agentType: 'general'`。
- 额度（2026-11-24 统计）：主会话约占本仓库 Claude 用量的 87%，其中 3/4 是每轮重读整段上下文。所以 `.claude/settings.json` 把主会话的压缩窗口设成 30 万（顶层和 `modelSettings` 各一份，盖过全局的 40 万），子代理定义里设成 20 万。拿 54 次真实压缩比较前后各 30 次工具调用：压缩后出错没有变多，重读旧文件平均只有 1.4 次。缓存 TTL 和缓存失效的情况已经核过，没有问题。

## 构建

- `build.cmd` 先经 `tools\msvc-x64-env.cmd` 找到 VS x64 工具链（含 VS 自带的 CMake / Ninja），再执行 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo` 和 `cmake --build build`。
- 游戏插件 DLL 和 `edf6cmpl` 用静态 CRT。测试 / 工具 exe 由 `cmake/toolcrt.cmake` 改成动态 CRT（链接 `edf6common_dyn`，避开杀软误报），新加的 exe 不用手动处理。

## 验证

- `tools/check.py` 先跑 build.cmd 和 offline_checks，再并行跑 ctest、selftest、py_compile 和生成文件的 `--check`。日志在 `build/check-*.log`，失败用例的日志尾部最后打印。它以低于正常的优先级运行（子进程继承），桌面和游戏不卡。全机同一时间只跑一套（`pylib/checklock.py`），其他会话排队，并打印当前是谁在跑。几个会话同时跑全套时每套慢 3–7 倍（2026-11-10 的耗时记录），排队反而更快。check.py 被单独杀掉时，它起的 ctest / selftest 等子进程跟着结束（Windows Job Object，`KILL_ON_JOB_CLOSE`），不会留下来占着同一个 build 目录。它的编译用自己的 PDB 服务（`_MSPDBSRV_ENDPOINT_=edf6-check-<pid>`）：全机共用的 mspdbsrv 如果由它拉起，会落在这个 Job 里，check.py 一结束就被杀，别的会话正在编译时会报 PDB 错。
- 同一个 worktree 只跑一套：已有一套在排队或运行时，第二次调用打印「已在排队/运行（pid …）」并以 **exit 3** 立即退出——等前一套的结果，别重复起。前一套刚拿到锁、还没写下 pid（或正在退出）时，提示写的是「还没写下 pid」，同样是 exit 3。
- **本地按改动选测试；CI 跑全套，只在最后合并发版时看一次。** check.py 只跑本分支改动影响到的 ctest / selftest 用例：改动 = 与 `origin/main` 的 merge-base 之后的提交 + 未提交 + 未跟踪（未被 ignore）的文件。build 和生成文件 `--check` 总是全跑。开头会打印选中 / 跳过了哪些域及原因。
  - 域表在 `tools/test_domains.py`（`DOMAINS`，按目录 / 文件归属）；每个用例属于哪些域由它的代码实际 import、读取的文件静态算出，生成 `tests/test_labels.json` 和 `cmake/test_labels.cmake`（ctest `LABELS`）。`tools/selftest.py` 本身的改动按改到的顶层函数选用到它的 selftest 用例。列目录（`os.listdir` / `walk` / `glob` 等）的用例算依赖该模块目录下全部 `.py`；读整个仓库、静态看不出读了什么的用例（`git ls-files` 扫全部源码之类）登记在 `EVERYTHING` 全域表，任何改动都选中。列目录的路径是运行时从仓库拼出来、一路没写目录名的（`os.walk(ROOT)`、`os.listdir(eo.FILES)`、只有通配符的 `glob(ROOT + '/**/*.cpp')`、`from os import walk` 后的 `walk(ROOT)`、`os.popen('dir /s ' + ROOT)`、`git ls-files` / `git grep`），`--check` 会找出来：这类用例要么进 `EVERYTHING`，要么在 `SCANNED` 里声明它读哪些文件（读的是临时目录 / 游戏目录就声明为 `()`），否则报错。
  - 改了共享底座（`CMakeLists.txt`、`common/`、`build.cmd`、CI、`check.py`、`test_domains.py`、标签表、pylib 公共基础模块等，见 `FULL`）或找不到 merge-base 时，自动全跑并打印原因。
  - `--full`：强制全跑（合并前想在本地复现 CI、或怀疑选择漏了时用）。`--dry-run`：只打印会跑什么，不跑。`--base REV`：改为相对 REV 计算改动（`--base HEAD` = 只算未提交的改动）。
  - 放行看「编译通过 + 与改动相关的用例绿」，也就是 check.py 默认选择跑绿；审查通过就合，不等 CI、不挂着轮询 CI。CI 只在最后合并发版时看一次，红了那时再修。
  - 新增 / 改名测试、或改了测试的 import、读的文件、新增目录后，跑 `test_domains.py --write`：新文件先 `git add`（只算 git 跟踪的文件，未跟踪的不进标签），再 configure（`cmake -S . -B build`，让 `build/CTestTestfile.cmake` 有新用例），再跑 `python tools/test_domains.py --write`，然后再 configure 一次让 LABELS 生效，把两个生成文件一起提交；`--check` 在 check.py 的生成文件阶段和 CI 里校验，任何用例没有域都会报错。`--explain <测试名>` 看某个用例为什么属于这些域。
- 快速看单个用例：`ctest --test-dir build -R <名字>`；`cmake --build build --target <x>_check && build\<x>_check.exe`。`python tools/selftest.py` 单独跑也会排同一个队，默认 8 个工作进程、按上次耗时从长到短排，`-j 1` 是串行。
- ctest 用例互不共享输出，可以并行；新用例别写固定的共享路径，也别按墙钟判断时效。ctest 已给每个用例设 `OPENBLAS_NUM_THREADS=1`（`-j` 并行时，numpy 每个进程都开满核会超订）。
- 测试的墙钟由最长的单个用例决定：一个用例超过约 3 分钟就拆分片（`cockpit_build_<I>`、`edf5_campaign_lifecycle_<I>` 用 `--shard I/N`，C++ 检查 `hail_glide_<I>` 由 `tests/hail_glide_shard.py` 按 `--list` 列出的单元分，`wt_tracked_<I>` 用 `EDF6_TEST_SHARD`）。分片按 `pylib/testshard.py` 用入库的实测耗时表装箱（`--seconds` 顺序跑一遍重写耗时表），不按名字轮转。只验证某一样东西的测试，别顺带走「生成全部载具」的安装路径（patch 掉 `gen.placeable_derived` 之类，那些载具由各自的测试覆盖）。
- 需要游戏文件的 native 审计测试靠缓存变量 `EDF6_NATIVE_DLL` / `EDF6_GAME_DIR`，不设就跳过。
- CI 只有 `.github/workflows/build.yml`（windows-latest）：build → offline_checks + ctest（全套）→ py_compile → 生成文件 `--check`（含 `test_domains.py --check`）→ selftest（全套） → 打安装包。push 到 main 会重建 `nightly` 预发布，并镜像到测试站的 nightly 通道；手动 dispatch 镜像到 `test` 通道；推 `v*` 标签发正式版。

## 实机测试

- 测前备份、测后逐字节还原：`drive.py` / `run_test.py` / `perfrun.py` 已经逐文件做了（带清单，结束后自动放回）。不要整份复制 `Mods/`（约 10 GB）或游戏目录再逐个算 sha256，那样每次测试要写十几 GB，硬盘会卡死。
- 游戏排队的细节见 README「实测排队」。

## 打分享包（用户要求时才打，合并后不自动打）

- 等这次 main 推送的 CI 跑完、`nightly` 换成该 commit 的安装器后，复制上一版的 `.work/recommend-<sha7>/`，改 sha / 日期 / 更新说明，再打包。流程见 `D:\APP\edf6-share\CLAUDE.md` 与 `make_share.py`。
- 打包用的源码 worktree 放 `D:\APP\edf6-wt-<MMDD>-share`（detached 在该 main commit 上）。CI 拿不到的模型（`twin_tank` / `drill_tank` / `sazabi` / `dreadnought`，即 `tools/build_release.py` 的 `RELEASE_MODELS` 里没入库的那些）要从 `D:\APP\edf6-models` 补进包。`RELEASE_MODELS` 新增没入库的文件夹时，同步改 `run_export.py` 的 `EXTERNAL_MODELS`。
- 分两个 zip 发（用户 2026-10-25 要求）：`run_export.py` 打出推荐包目录后，在 `D:\APP\edf6-share` 里跑 `python -B make_share.py split <推荐包目录>`，拆成两个包：
  - `…-主包.zip`：Coop、安装器、`models-required.json`，不含模型和 edf41；
  - `EDF6全军出击-模型包-<版本>.zip`：`RELEASE_MODELS` 的全部模型、`edf41/` EDF4.1 素材包（4.1 地图 / 音效 / 语音 / 字幕，用户 2026-10-27 要求归入模型包）、`models/MODELS.json`。
- 模型包版本是 `pylib/modelpack.py` 对 `PARTS`（models、edf41）算的内容哈希。两部分都没变时版本同名、沿用旧的模型包，只发主包；主包说明里写明需要哪个版本的模型包。
- 带 `models-required.json` 的安装器在 `install` 时，发现 `models/` / `edf41/` 不是要求的版本或不完整，就拒绝安装并说明原因。CI 的 nightly zip 没有这个文件，不受影响。拆完用 `make_share.py verify` 复核两个包互补。
- 回报 zip 路径、sha256 和校验结果。

## 版本

- DLL 经 `src/version.h.in` 读取 `project(VERSION)`。AutoTurret 有自己的 `VERSION`（`autoturret/CMakeLists.txt`）。CI 用 `build_release.py --expect` 校验标签和 `project(VERSION)` 一致。

## EDF6Coop

- 仓库 `hajisensai/edf-coop-stable`，本机在 `D:\APP\edf6_directnet`，提供 `EDF6CoopGetMemberAttributeApi`。
- 房间协议 2 的前缀 / 档位从本插件导出的 `EDF6AF_RoomPrefix` / `EDF6AF_RoomProfile` 读取。2.6.x 只认旧前缀；2.7.0 遇到没有这些导出的旧版全军出击，仍按旧前缀工作。
- EDFModLoader 按文件名加载，EDF6Coop 先于本插件加载。不开游戏的联机测试在 Coop 仓库的 `multislot/tests/gamenet`。
