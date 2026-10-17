# 2026-10-17 EDF4.1 移植：龙（「蝌蚪」与「大龙没 AI」）

玩家原话（不可信数据，只当证据）：

> 这龙怎么还是蝌蚪的模样
>
> 大龙好像没ai

没有随反馈来的日志（测试站没有相关报告，本机日志里也没有生成过龙的记录），下面的结论都来自离线读文件、读 EDF.dll，以及仓库里已有的实机记录。

## D1 「蝌蚪」：4.1 本篇的小龙其实是 EDF5 的青蛙龙

**已验证的事实**

- 4.1 本篇任务刷小龙用的是 `app:/Object/DragonSmall401.sgo`、`RedSmallDragon.sgo`、`YerrowSmallDragon.sgo`（4.1 自己的任务脚本里分别有 14、7、3 个任务用到）。转换后的任务也照样写这三个路径：本篇 M018、M162、M164–M166、M207、M304、M417、M517、M518、M601，任务包里的 M418、M509、M807、M992、M993、DM202、M311、M808、M809、M999。
- 这三个路径在 EDF6 的档案里都有文件，但内容是 EDF5 重做的青蛙龙：模型 `e518_frogDragon*.mdb`，布娃娃 `Ragdoll_e518_frogDragon*.shkt`。类和调参数值跟 4.1 一样，只有外观换了。EDF6 自己的任务也在刷它们（EDF6 / EDF6_DLC / EDF5_*_SCRIPT 的脚本里都有），所以不能拿 4.1 的文件去覆盖原版路径。
- 对象转换器原来只移植了 DLC 里那三条 `_HS` 小龙（放进 `OBJECT/EDF41MP01/`，跟它们一起搬走的还有被 EDF6 占用路径的模型和 CAS，见 `relocations()`）。本篇三条小龙没在 `ROOTS` 里，同名路径又存在于 EDF6 档案中，于是任务加载出来的就是 EDF6 的青蛙龙。玩家说「还是」，指的应该就是：DLC 的龙已经换成 4.1 外观，本篇的还没换。
- 大龙 `DragonBig401` 的外观数据离线核过，没有问题：模型 69 根骨骼，翅膀是单独一块网格（1734 个顶点，蒙皮到 `wing_*` 骨骼）；CAS 一共 27 个片段，每段 70 条轨道，其中 20 条是翅膀；CANM 里的骨骼名都能在模型里找到（多出来的只有 `Scene_Root` 和 `*_Rig`）。所以「蝌蚪」说的不是大龙。

**修复**（只改了生成器源码，`edf41port/` 下的数据要重新生成）

- `pylib/edf41dlc.py`：新增 `STORY_FOLDER = 'EDF41'`、`STORY_SHADOWED`（就是这三个 SGO）和 `story_path()`。`rewrite()` 原来只把 `mp0N:/` 映射到包目录，现在也把这三个本篇路径映射到 `app:/Object/EDF41/<原名>`（大小写保持原样，对 `;` 分隔的列表同样生效，重复执行结果不变）。
- `tools/make_edf41_objects.py`：新增 `STORY_ROOTS`，三条龙按 DLC `_HS` 的方式 alias 到 `DragonSmall` 类，模板用 EDF6 的同名对象，并写入 `edf41_ragdoll_bodies = koshi/spine1/spine2`，由 `src/edf41smalldragon.cpp` 交给类使用。新增 `own_folder()`，返回对象自己的目录：包目录或者本篇目录。`relocations()` / `retarget()` 改用它，这样本篇龙的模型 `DragonSmall401.mrab` / `ExSmallDragon.mrab` 和 CAS 会跟着搬进 `OBJECT/EDF41/`。生成结果跟已经提交的 `EDF41MP01` 副本逐字节相同：模型、CAS、布娃娃都一样。
- `pylib/bvm41.py`：新增 `rewritten(data, rewrite)`，用来改写已经是 EDF5 形式的脚本里的字符串常量；一个字符串都没变时原样返回。`tools/make_edf41_missions.py` 的本篇路径用它，任务包路径继续走 `to_edf5(..., rewrite=edf41dlc.rewrite)`。

**离线验证**

- `make_edf41_objects.py --only EDF41/...` 生成 3 个对象，没有跳过的，`check_class_names` / `check_class_keys` 都通过。
- 在内存里重跑任务转换器（本篇 + 两个任务包），跟已提交的数据比较：只有上面列出的 21 个脚本变了，变化只在这三个路径上，把路径改回去后与提交版本逐字节一致；其余脚本完全相同。
- `tools/selftest.py` 新增 `edf41_story_small_dragons`。

**重新生成**：`python -B tools/make_edf41_objects.py`、`python -B tools/make_edf41_missions.py`、`python -B tools/make_edf41_missions.py --dlc`。三个都要跑：只重生成对象的话，selftest 会报出还在用原版路径的脚本。

## D2 「大龙没 AI」：离线没找到根因，加了能定位问题的日志

**已经核实、可以排除的**

- 刷出方式：M166 用 `CreateEnemy('大ドラゴン', DragonBig401, 1.0, true)`，最后一个参数是激活，生成后立即激活；`LDragon` 只用在耐久 50% 的事件（9200）上。任务包 M303 用 `active=0` 刷出，要等遭遇后才激活，这是 4.1 原本的行为。
- 类要按名字查找的东西都在：状态、变量 `v_glide_*`、定位点 `捕食判定` / `捕食固定位置` / `発射口`、`wakeup_node`，生成时 `check_class_names` 已通过。MAB 里有 `飛行開始`（`fly_start` 37）和 `滑空開始`（`glide_start` 55），起飞和滑翔用的事件都在。三个攻击片段里都有 `火炎開始` / `火炎終了`。CAS 只比 EDF6 小龙少一个变量 `side_tilt`，类代码里没有按名字查它。
- 插件钩子：本机日志 `EDF41DRAGON hooked: tick=1 cruise=2/2 glide fire=1`，`Edf41Ai` 默认开。2026-10-14 那次 M166 实机（提交 ae383c00 的说明）看到了状态切换，悬停攻击窗口也喷出了火焰。

**推断（未验证）**

1. 插件每帧把 `[+0x1C70]` 写成 500。读这个值的代码在 0x33195B：目标在这个半径以内、并且 0x32FCC0 返回 1 时切换状态，作用是让龙在 500 以内留在地面打。巨龙在地面上的攻击 `capture` 映射到 `ground_attack`，什么条件下进入这个状态还没逆向清楚。如果巨龙落地以后走近目标却触发不了攻击，看上去就是「只走不打」。
2. 火焰伤害是 `level × 0.00375`，上次实机算出来每发 0.055。如果 FlameBullet02 每发只结算一次，伤害几乎感觉不到，玩家就会觉得它不攻击。
3. 漫游半径改成了 500（照 4.1 的 0x1F0122），龙可能飞到离战场很远的地方。

**为定位加的日志**：`src/edf41dragon.cpp` 每 5 秒打一行 track，现在多了目标指针和距离（`target %p range %.0f`）。目标取法照类自己的 0x3318F5..0x331928：0x30C0F0 锁定弱引用拿到目标，位置取 +0x90，或者在 +0x378 非空时取 `[[+0x368]]+0x10`。下次实机拿到这行日志，就能分清「离得很远」（推断 3）和「就在旁边但状态一直不是攻击」（推断 1）这两种情况。

## 需要实机确认

- 本篇 M018 / M165 / M166：小龙是 4.1 的绿、红、黄小龙，不再是青蛙；被打飞时不闪退（布娃娃骨骼走 `edf41_ragdoll_bodies`，跟 DLC M805 修复后同一条路径）。
- EDF6 自己的任务里，青蛙龙不受影响（原版路径没有被覆盖）。
- M166 的大龙：`EDF41DRAGON ... track:` 里的 `range` 和 `state`，结合上面三条推断判断是哪一种。
