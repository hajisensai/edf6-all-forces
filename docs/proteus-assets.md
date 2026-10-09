# 普罗透斯资源（`tools/make_proteus.py`）

2026-10-09 起只有一个资源：`OBJECT/EDF6VC_PROTEUS_SHIELD.SGO`，原版空袭兵电磁碉堡（EWEAPON196，弹种 `BarrierBullet01`）的那面能量墙，
作为一个 DemoIndirectFire 的唯一一发（由原版 `DEMOGUNSHIPFIRESOLID.SGO` 改成：速度 0、无重力、寿命 2^30 帧、AmmoSize 1、命中半径系数 0.01、
颜色与展开音效取自 EWEAPON196、Ammo_CustomParameter = [120°, 12 m, 17 m, [1,1,1], [0,0,0]]）。墙的外观、材质、碰撞、HP 都是游戏自己的；
插件怎么立起、跟随、收起见 `docs/proteus-re.md` §4。数值与 `src/proteus_shield.inc` 的 `kBarrierArcDeg/kBarrierRadius/kBarrierHeight` 一致，
由 `tools/test_proteus_assets.py` 核对（含按原版 0.0872 步长算出的分段数 24，用来和电磁碉堡的 27 区分）。

## 旧版资源的撤销

2026-10-08 版本生成过私有模型 `EDF6VC_V614_PROTEUS_MK2.MRAB/.CAS`、`EDF6VC_VEHICLE407_BIGBEGARUTA.MRAB/.CAS`（36 块盾板 + 36 根骨），
并把全部 VehicleBigBegaruta SGO 改去引用它们；那份 CAS 曾因通道表未 16 字节对齐导致进图崩溃（`docs/proteus-cas-crash.md`）。
现在游戏重新加载原版模型。安装器仍走原来的写前日志（`Mods/.edf6vc_proteus.json`、原件备份 `.edf6vc_proteus_backup/`）：
更新时被改写过的 SGO 恢复原件，私有模型对删除；如果某个散装 SGO（第三方改过的）仍引用旧模型，这一对模型保留，日志里列为保留。
测试场（`testrange/gen.py`）不再改写普罗透斯 SGO，只登记对护盾 SGO 的依赖（`make_proteus.range_shield`；单独安装测试场时自己生成同一个文件）。

测试：`tools/test_proteus_assets.py`（离线夹具 + 有 Root.cpk 时的真实生成）、`tests/proteus_install_test.py`（旧版安装 → 更新撤销 → 卸载，
每一步写入后中断都能恢复）、`tests/proteus_range_test.py`。只写临时目录，未写游戏目录。
