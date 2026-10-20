# 2026-10-12 借鉴 EDF6 SHKT Editor：SHKT / SGO 绑定校验

## 用户原话

> "C:\Users\wrds\Downloads\Compressed\Source_EdfShktEditor.zip" "C:\Users\wrds\Downloads\Compressed\EDF6_SHKT_Report.zip"
> "C:\Users\wrds\Downloads\Compressed\EdfShktEditor_0.1.0_win-x64.zip" 可以把优点抄过来

## 来源与许可证

- EDF6 SHKT Editor 0.1.0（作者 ktaro）：C# / .NET 10 / WPF，Havok 2020 tagfile 读写、物理形状编辑、SGO 联动、V-HACD 凸分解。
- `LICENSE.txt`：**MIT**（Copyright (c) 2026 ktaro），允许复制修改，需保留版权与许可声明；随附的 V-HACD 4.1 为 BSD-3-Clause。
- 本次**没有复制代码**：检查规则和阈值按它的 `ShktChecker` / `SgoChecker` 与格式报告（EDF6_SHKT_Format_Spec §3.8 / §5、Usage Analysis §1）在 `pylib/hktag.py` 之上用 Python 重写，`pylib/shkt_check.py` 文件头注明出处与 MIT 版权。
- 只读了源码和文档；压缩包里的 exe / 脚本 / 构建一律没有运行。

## 它比我们强的地方（对比 `pylib/hktag.py` / `hkcms.py` / `ragdoll_fit.py` / `aircraft_collision.py` / `sidecar_model.py`）

| 优点 | 我们原来 | 结论 |
|---|---|---|
| SGO ↔ SHKT 刚体名校验：SGO 映射行 / accessory 用到的刚体名在 SHKT 里不存在 → **游戏在任务开始前崩溃**（报告实测） | 没有。各生成器只核自己那份（`ragdoll_fit.problems` 核骨骼位置，不核名字存在） | **已采纳**：`pylib/shkt_check.py` `bound_names` / `sgo_problems` + `tools/check_shkt.py` 扫 Mods 全部 SGO |
| SHKT 自检：重名刚体、materialId / motionPropertiesId 越界、动态刚体质量 ≤ 0、关节两端枢轴在模型空间不重合（原版 ≤ 0.8 mm，编辑器 1 cm 报错）、凸包顶点越出自身平面（派生数据过期）、复合形状 aabb / SIMD 树包不住实例（派生数据过期）、碰撞层 / 系统组与原版约定不一致 | 只有地形压缩网格的 `hkcms.bound_problems` 和侧三轮的专用 `check_collision` | **已采纳**：通用于任何 tagfile SHKT |
| 质量属性计算 + 压缩打包（hknpShapeMassProperties，按位与原版一致） | `aircraft_collision._compound` 把新形状的 properties 清零（「交给 Havok 推导」） | **未采纳**：实测 7 个机身 SHKT 的刚体全是 keyframed（motionType 2）且保留原版自带的 massDistribution，游戏不读形状质量属性，改了在游戏里没有作用；检查器只在「刚体无 massDistribution 且形状也无质量属性」时报 warning（规格书 §5.5：null 时游戏才从形状推导） |
| tagfile 完整写出（按 Havok 规则重排 item，原版 316 个 SHKT 逐字节往返） | `hktag.py` 只读；编辑靠原地改字节或 `aircraft_collision._Edit` 追加 item | **未采纳**：我们的生成器都是「改原版捐体」，追加/原地改已够用，重写 writer 是大工程且无当前需求 |
| V-HACD 凸分解 / 从网格生成凸包、_l/_r 镜像、新建 ragdoll / accessory | 机身用 24×32 体素高度场盒子（`aircraft_collision.mesh_cells`） | **未采纳**：需要 native 依赖（vhacd.dll）；当前机身碰撞没有反馈问题。以后要给新模型做贴身碰撞时再考虑 |
| 旧版 Havok packfile（`1E 0D B0 CA CE FA 11 D0`） | 也不支持 | 两边都没有；见下方缺口 |

## 实现

- `pylib/shkt_check.py`：`Shkt`（任意 tagfile SHKT 的刚体 / 关节 / 凸包 / 复合形状读取，偏移全部来自 TBDY 类型表）、`problems(shkt)`、`body_names`、`load_plain`（SGO 与 DSGO，内嵌块保留原始字节——`dsgo.to_py` 会把它们丢成 `<blob N>`）、`bound_names`、`sgo_problems`。
- `tools/check_shkt.py`：`python tools/check_shkt.py` 检查游戏 `Mods/OBJECT`、`Mods/WEAPON` 的每个 tagfile SHKT，以及每个 Mods SGO 绑定的刚体名（SHKT 先找 Mods，否则 Root.cpk / Chunk01 / Chunk02，与游戏加载顺序一致）；`--stock` 对原版文件做同样检查（校准）。有 error 退出码 1。
- `tests/test_shkt_check.py`：可移植部分 5 项（selftest 已接入，CI 会跑）；有游戏时另跑 5 项原版文件变异实测。

## 验证（2026-10-12，本机游戏目录 `D:\steam\steamapps\common\EARTH DEFENSE FORCE 6`）

- 原版校准：`python tools/check_shkt.py --stock` → `0 error(s), 0 warning(s), 0 skipped`。覆盖 316 个 SHKT、605 个 SGO 的 45,037 条绑定（animation_from_ragdoll 30,710 / ragdoll_from_animation 14,027 / accessory 300；报告统计原版映射行 44,159，量级一致）。
  - 凸包平面阈值按原版实测定：原版凸包顶点最多越出自身平面 0.118 m（6.7 m 的凸包），相对值最大 1.75 %，所以阈值取 `max(1 mm, 2 % × 凸包尺寸)`；固定 1 mm 会对原版误报 125 处。
- 我们装在游戏里的 Mods：`python tools/check_shkt.py` → `0 error(s), 0 warning(s), 18 skipped`；核对了 Mods SGO 的 3,606 条绑定（694 条指向我们生成的 9 个 tagfile SHKT，2,912 条指向 15 个原版 SHKT）。机身 / 火炮 / 侧三轮的包围盒、SIMD 树、凸包平面、关节都合格。
- 变异实测（`python -B tests/test_shkt_check.py -v`，10/10 通过）：把原版 `RAGDOLL_V506_HELI.SHKT` 的刚体 `RagDollProxys.rotor` 改名 → `V506_HELI.SGO` 报缺失；挪一个凸包顶点 5 m → 报平面过期；把 SIMD 叶子缩 75 % → 报树过期；把关节一端刚体挪 0.5 m → 报枢轴分离。原样文件四项全无报错。

## 未覆盖 / 缺口

> 2026-10-20 更新（`docs/model-pipeline-2026-10-20.md`）：下面第一条已解决——旧版文件由 `pylib/hk2014.py` 读出刚体名，`check_shkt` 不再 skipped（Mods 里指向它们的绑定 4,838 条全部核对）；tagfile 完整写出、质量属性、凸包 / 凸分解也已移植（`pylib/hkfile.py`、`pylib/hkshape.py`、`pylib/model_pieces.py`）。

- 18 个旧版 Havok packfile SHKT（EDF4.1 移植的 Deiroi / 异星拖车 / 蚁巢 / 运兵船 / 士兵 / 要塞机器人 ragdoll）读不出刚体名：Mods SGO 指向它们的 1,056 条绑定**没有核对**，工具列为 skipped。
- 检查器不在安装流程里自动跑（生成器各自已有专用检查；装游戏前手动跑 `tools/check_shkt.py`）。是否接进 `installer.install` 之后的 `check` 留给后续决定。
- 未真机验证：本工具只读文件，不改任何游戏资源，不涉及运行时行为。
