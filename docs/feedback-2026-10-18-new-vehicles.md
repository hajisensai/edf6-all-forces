# 2026-10-18 反馈：M163 火神与 BMPT

用户原话：「增加火神炮（机枪那种），增加 bmpt 载具。可以使用 api 去那边下载」。随后已明确选择 M163 火神防空车。

## 实现

- 新增 `EDF6VC_CALL_VULCAN`、`EDF6VC_CALL_BMPT`，只追加在已有呼叫尾部，保留已发版本存档的武器行顺序。调用列表由 `tools/calls.py` 生成；安装器和测试场使用 `GROUND_VEHICLES` 同一份登记。
- M163：自建 M113 短车体、每侧五负重轮、开放炮塔、六根有开口的 20mm M61 炮管、弹鼓、光学瞄具。HE / AP 为两个原生槽对应的备用弹链，复用挂载菜单、R / LB 切换，互斥击发。两个槽均跟随同一个 `cannon_slide_l` 实体炮管，避免仰角或后坐变化后第二弹种从隐形右炮发射。抛壳点跟随自建接收机，弹壳按 20mm / 30mm 调小，避免沿用原车浮空的约 70mm 弹壳。
- BMPT：自建长车体、每侧六负重轮、双 30mm 炮管、左右各二根 Ataka 导弹筒、光学观察设备。两个原生机炮同时开火，另加第三导弹挂点，挂载菜单 / R / LB 在机炮组与四发导弹之间切换。双炮切到单个导弹时，第二触发源被抑制，防止一次操作重复消耗两发。
- 所有车辆外部几何重新构造，不把其它车辆整个外形改名。从玩家自己的 Root.cpk 复用原生 V603 材质、纹理和 40 个原骨 / 原矩阵，CAS 仅在私有副本关闭两条旧雷达自转轨道，M163 另加有明确声明的旋转叶骨；独立碰撞与 binding 由实际几何重新拟合。生成器不带分发原版资产。
- `tools/make_vulcan.py` / `tools/make_bmpt.py` 分别生成 6 / 7 个完整文件（含独立拟合 SHKT 与私有瞄具 CAS），`--check` 只在内存生成校验，`--out DIR` 导出到指定目录。安装与删除用独立 `vulcan` / `bmpt` ledger 归属。
- M 地图摆放目录现逐一要求私有任务 SGO、模型和所有武器文件都存在。文件引用允许 Mods 下 OBJECT / WEAPON 的安全相对路径，并兼容原有 EDF41MP01 / EDF41MP02 子目录；逐段拒绝绝对路径、冒号、空段、父目录和控制字符。生成器与 C++ constexpr / 磁盘读取使用同一合同，模型档案记录可从内存新车资源计算，不要求先写入游戏。
- BMPT 的原生请求模板只有两个槽，`call_weapons.vehicle_sgo` 对这一登记明确执行 2→3 扩展，并将导弹的 BodyRecoil 设为 `[0,0]`。其它请求的槽数检查保持严格。

## 资源与来源

2026-10-18 实际查询官方 Sketchfab 搜索、模型详情与下载 API：

| 模型 | UID | 元数据许可 | 下载结果 |
| --- | --- | --- | --- |
| M163 VADS / FamousUnityHubSoldier | `5ea816d46f5844dbbb616b76a9b2e769` | Free Standard | HTTP 401，未下载 |
| BMPT Terminator 2 / SanderWolf_ | `8d023ae2bccb473ca0af973397b58e6d` | CC BY 4.0 | HTTP 401，未下载 |
| Bmpt / sacher | `37ebbc6899444e13b203c2329fed6b1a` | CC BY 4.0 | HTTP 401，未下载 |
| M163 / andrej.zapasnik | `54bf2317be68403d9b5a1d7e75e68d4d` | CC BY 4.0 | HTTP 401，未下载 |

下载端点为 `https://api.sketchfab.com/v3/models/<uid>/download`，返回 `Authentication credentials were not provided.`。本机已有 38 份 Sketchfab 模型记录和 `edf6-models` 未包含所需两车。当前进程也没有 Sketchfab 认证变量，因此本次使用上述明确标注的自建模型，没有宣称外部下载成功。

比例 / 构型依据：

- [MCIA Saudi Arabia Country Handbook](https://info.publicintelligence.net/MCIA-SaudiArabiaHandbook.pdf)，M163 条目给出长 4.86m、宽 2.85m、高 2.736m 与六管 20mm 系统。
- [US Army ARMOR, Summer 2022, Grau / Bartles](https://www.benning.army.mil/Armor/eARMOR/content/issues/2022/Summer/3Grau_Barttles22.pdf)，第 2 页列 BMPT 车体长 6.7m、宽 3.8m、高约 3.4m，双 30mm 与四枚 Ataka。仅据公开结构与比例自行建模，没有提取该文照片。

游戏参数是 EDF 的平衡参数：M163 每弹链 2000 发、每帧一发、1020m/s；HE 4 伤害 / 0.6m 爆炸，AP 6 伤害 / 穿透。BMPT 每根机炮 425 发、间隔 12 帧、960m/s、22 伤害；导弹四发、游戏锁定距离 1800m、峰值 550m/s、1200 伤害。这些数值不声称完整复现实车弹道与射速。

## 验证与边界

本机实际只读 Root.cpk，未启动或控制游戏、未写游戏目录：

- `python tests/test_tracked_combat.py -v`：7 项通过，检查历史呼叫前缀、runtime 使用的挂载登记、完整模型 / SGO 资源集合、原骨及相同实体炮口、MAB 弹壳等其它表未丢失、BMPT 请求与实体 3 槽一致。
- `python tools/make_vulcan.py --check` / `python tools/make_bmpt.py --check`：4 / 5 个文件实际生成并校验通过。
- 自建模型：M163 7408 三角、BMPT 8700 三角；重复生成字节一致、MDB 写读写一致、CAS 保留原字节。改骨矩阵、改蒙皮、混入原版网格、改三角索引四种损坏负例均被模型检查拒绝。
- `tests/debug_spawn_files_test.cpp` 以 MSVC `/W4 /WX` 实际编译运行，25 个安全资源路径用例通过；`gen_spawn_catalog.py --archives` 及 `--check` 通过。
- 已查看离线模型预览 `tmp/combat-models/preview.png`，两车分别具有六管短底盘 / 双管四导弹长底盘的可识别构型。

2026-10-19 已补齐 M163 六管随实际开火旋转、停火惯性停止；两车拥有独立拟合 SHKT、与五轮 / 六轮几何一致的悬挂轮表及半径，详细真实原生证据与验证见 [本轮记录](feedback-2026-10-19-tracked-native.md)。

尚未完成实机验收：动态驾驶、炮塔俯仰 / 后坐、履带滚动、碰撞接触和多人同步仍需游戏验证。碰撞采用实际几何范围内的凸包分片拟合与实体 AABB 惯量近似，不声称有实车物理标定。BMPT 导弹架随炮塔转向，发射轴独立于机炮仰角；导弹发射后使用现有制导模块。
