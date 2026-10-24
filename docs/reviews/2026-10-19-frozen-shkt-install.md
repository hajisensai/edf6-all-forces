# 2026-10-19 冻结安装器 SHKT 模块冲突

真实安装 `0d9dda6` 官方包时，M163 生成链 `make_vulcan → make_tracked_combat → tracked_collision._reframe` 在读取 `sc.B` 时抛出 `AttributeError`。此前冻结包自检只导入 `tracked_collision`，没有执行该消费者，所以未覆盖此失败。

## 根因

- `tools/shkt_check.py` 是命令行入口，`pylib/shkt_check.py` 才是提供 `B`、`Shkt` 和几何检查 API 的库。原官方 EXE 的 PYZ 中收录了前者。
- PyInstaller 的 `Analysis._extend_pathex` 先加入主脚本 `tools/installer.py` 的目录，再追加 `--paths`。只调整参数中 pylib 的顺序仍会选中同名 CLI。
- 四个本地搜索目录唯一的重复模块名就是 `shkt_check`。缓存的旧 basename 字典也让 tools 文件覆盖库文件，库修改不能使 Vulcan/BMPT 生成缓存失效。

## 修复

CLI 改名为 `tools/check_shkt.py`，同步现有命令引用。冻结与缓存共用四目录模块收集器；遇到重复名（含 Windows 大小写别名）明确失败，并同时指出两个路径。

`installer --check-package` 执行无游戏依赖的合成 SHKT 消费者：三个刚体的实际重绑定经过原 `sc.B` 访问点，再执行凸包编辑、质量缓存和完整 SHKT 检查；破坏平面必须被生产检查器拒绝。没有打包测试模块，也没有修改生产碰撞算法。

发布构建继续从空临时工作目录、移除 `PYTHONPATH` 后运行实际 onefile EXE 的 `--check-package` 和 `--check-workers`，任一失败都会阻止打包成功。

## 已完成的定向验证

- 冻结包装测试 5/5：含错误 CLI 模块进入真实消费者后必须失败，以及两个冻结入口拒绝重复模块。
- 模块/缓存身份测试 4/4：真实库变化使 Vulcan/BMPT recipe 失效，独立 CLI 变化不影响它们，重复名与第四目录大小写别名被拒绝。
- slim generator 52 项检查通过。
- `release_imports`、`release_module_identity`、`frozen_collision_consumer`、`shkt_check_on_synthetic_data` 四个 selftest 入口通过。

本轮没有写入游戏目录或执行完整安装；实际冻结构建与安装验收结果由对应构建/交付日志记录。
