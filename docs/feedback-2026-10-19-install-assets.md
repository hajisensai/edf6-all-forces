# 新载具与 EDF4.1 材质的安装、打包、测试场接线

## 实际安装路径

安装器对 M163 Vulcan 与 BMPT 分别执行 `build_asset`，完整生成成功后才进入资源写入；每组包括实际机体 SGO、模型 MRAB 和全部武器 SGO。两车分别使用 `vulcan` / `bmpt` 账本 owner 与构建缓存组。升级会校验生成器依赖闭包、原版 CPK 标识和所有已安装输出，任何一个弹药文件缺失都会使该组重新生成。完整卸载释放两组；测试场仍引用的文件留到最后一个 owner 释放，玩家改过的文件按账本规则保留。

`tools/build_release.py` 显式冻结两车入口、共用生成器和三个模型模块，以及 `edf41_materials` / `edf41materials`。发布时的 `--check-package` 自检也实际 import 新链并核对 owner / cache 注册；已有的真实冻结 EXE package / worker 检查门保持生效。

## 材质开关

安装器读取本次安装后将使用的 planned INI：`[VehicleCrew] Edf41MaterialCompatibility` 缺省开启，明确为 0 时关闭。此值传入 EDF4.1 objects、outfits 与 maps 三条写入链的 `material_compat` 关键字；它是安装生成期参数，修改后需重新安装生成资源。现有明确为 0 的玩家设置在升级合并中保留。共享模型仍等武器事务登记 owner 后再构建对象，防止把共享资产当作别的 MOD 文件漏装。

## 测试场

- 新建私有 `edf6tr_vulcan_mission` / `edf6tr_bmpt_mission`，通过真实车辆生成器转成带 `mission_setup` 的脚本可用对象。
- 模型与每一件武器均登记给测试场 owner。摆放目录使用标准 Mods 相对依赖路径，包含 OBJECT 与 WEAPON；对应生成器和运行时检查由同一批整合修改接线。
- 默认测试场加入两车。真实 M045 点位验证为 36 个载具摆放、8 个安全靶位，无占地重叠；原有 4 架战斗机与 3 架攻击机无需删减。
- EDF4.1 新恢复的主坦克使用 `vehicle301_tank`，旧保存计划里的 `vehicle301_tank_ai` 迁移为空车条目；新导入直升机与 Begaruta 也已进入完整目录。

## 验证

- `test_tracked_combat.py`：6 项通过，含本机 Root.cpk 的模型 / 枪口 / 武器挂点只读验证。
- `test_tracked_install.py`：6 项通过，临时目录中的安装、缓存、缺弹药重建、卸载、共享归属和发布命令 / recipe 验证。
- `test_tracked_range.py`：12 项通过，含真实 M045 点位、模型占地、完整资源生命周期与旧计划迁移。
- `test_frozen_cockpit_package.py`：3 项通过；`test_edf41_material_install.py`：3 项通过。
- 原 selftest 的 `release_imports`、`installer_removes_what_it_writes`、`pack_install_upgrade_uninstall`、`edf41_objects_after_weapons`、`spawn_catalog_covers_every_registry`、`range_leaves_out_what_is_not_installed`、`footprint_covers_the_stock_models` 均通过。

这些验证只写临时目录，没有写游戏目录、驱动游戏或中断游戏。冻结测试检查源码 import 链、冻结命令与失败门，本专项没有新建真实 PyInstaller EXE；游戏内碰撞、武器操作和新材质观感仍属实机验收。
