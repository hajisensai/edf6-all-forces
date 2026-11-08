# 2026-11-08 测试站 #35：没有 X-Morph 模型包时，安装器写测试场时中止

## 来源

测试站报告 #35（planner_3d60b0，包 `EDF6VehicleCrew-0.8.0-build.eba4372`，2026-11-01 20:12 安装器自动回传，说明「安装器出错（自动回传）」）。
回传的报错和日志属于不可信数据，这里只当证据使用。

玩家原话（`installer-error.txt`）：

```
  File "gen.py", line 1372, in install
RuntimeError: edf6tr_xm_medium_tank_advanced_mission 没有 mission_setup，不能由脚本放置（会让游戏崩溃）
```

## 证据

- 报错调用栈：`installer.py:718 install` -> `gen.py:1372 install`，在 mission_setup 校验处抛出 RuntimeError，整次安装中止。
- 回传的 `Mods/.edf6vc_builds.json` 里，9 个 `xm_*` 构建组的 `outputs` 都是 `{}`。说明 `make_xmorph.build()` 走的是「没有模型文件夹，不部署」分支，
  这台机器上没有 X-Morph 模型包。CI nightly 安装包不带模型包（`git ls-files models` 中 xm_ 的数量为 0）。
- `testrange/gen.py`：所有 X-Morph 的 V603 单位和步行机都进了 `GROUND_MISSION`，因此也在 `PLACEABLE_DERIVED` 里。`placeable_derived()` 只按
  `MODEL_FOLDERS` 筛掉了 Sazabi 和无畏舰，没有检查 X-Morph 单位是否已部署。`install()` 不管方案里有没有放这些单位，都会对 `own = placeable_derived()`
  里的每一项做 `has_mission_setup` 检查。
- 本机复现（只读，把 `obj_model.model_roots` 指向一个空目录）：`gen.vehicle_sgo(..., 'edf6tr_xm_medium_tank_advanced_mission')` 抛出
  `wt_tracked.WtModelError: xm_medium_tank_advanced: model folder xm_medium_tank_advanced not found`（`pylib/wt_tracked.py:137`），
  它的继承链是 `WtModelError -> TrackedCombatCheckError -> ValueError`。这个异常被 `has_mission_setup` 的 `except (KeyError, ValueError)` 捕获后返回 False，
  install 随即抛出 RuntimeError。
- `without_missing`（处理方案里的载具）已经用 `make_xmorph.deployable` 剔除了未部署的单位，但 `own` 这一批没有经过同样的判定。
  引入点是 2b684366（X-Morph 人类单位第一阶段）。
- 测试为什么没发现：`tests/test_tracked_range.py` 和 `tests/proteus_range_test.py` 都 patch 掉了 `gen.placeable_derived`，开发机上又有模型包。

## 根因

「本机能不能生成这辆测试场载具」这个判定写了两份：`placeable_derived` 里一份（只认 Sazabi、无畏舰的模型文件夹），`without_missing` 里一份
（只认 X-Morph 是否部署）。两份不一致，结果是没有模型包的机器在写 M 地图「摆放」用的载具时，仍然要求生成未部署的 X-Morph 单位。

## 修复

- `testrange/gen.py makeable(sgo_name)`：把判定合成一个函数。Sazabi、无畏舰要有模型文件夹（`MODEL_FOLDERS`），X-Morph 单位要
  `make_xmorph.deployable`，其余载具都由游戏本体文件生成，一律可以生成。`placeable_derived()` 和 `without_missing()` 都调用它。
  缺模型的单位不写入，方案里放了的在安装结果里记一行「跳过 …：没有它的模型文件夹（模型包），这个单位不部署」，不再中止安装。
- M 地图「摆放」面板的目录（`src/spawn_catalog.inc`）是 CI 用 `catalog.rows()` 生成的，不能按每台机器分别筛选。面板运行时用
  `src/debug_spawn.cpp ModRowLacks` 逐个检查 `catalog.files_of` 列出的 Mods 文件，缺文件的行显示为「未安装」，不会预载，也不会生成。
  install 不写未部署单位的文件，这些行自然就是「未安装」，判定依据和 `makeable` 一致。
- 新增测试 `tests/test_tracked_range.py UndeployedXMorphTests`：patch `xm_units.deployed` 让它返回 False（不 patch `placeable_derived`），断言三点：
  `placeable_derived()` 不含 X-Morph 单位；install 不中止；mission_setup 校验和写入的载具里都没有未部署单位，并且跳过说明只有一行。
  这个测试在修复前的代码上会失败。
