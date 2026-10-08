# 实际载具光学眼位

源为本机只读 Root.cpk，EDF.dll `0x678CCB46`。没有启动游戏或改动安装目录。

## 已落实的来源

`pylib/vehicle_optics.py` 不使用炮口偏移。505/601/404 的源镜片均经实际贴图模型正面/斜视图检查；面片索引的 MDB SHA-256 被钉定。眼位是源镜片三角面的面积加权质心，前向是面法线，父骨保持真实炮塔/炮俯仰骨。403 两机枪使用原版座位的近眼 camera locator。

| 车型/选枪 | 实际来源 | 父骨 | 模型坐标眼位（米） |
|---|---|---|---|
| 505 主炮 | Light02 材质、炮手前向潜望镜最大玻璃面 | cannon_main | (0.603516, 2.126953, 0.724121) |
| 601 主炮 | v601_body、object/mesh 0/0、三角 475–482，顶部深色观瞄窗 | cannon_main | (0.429199, 2.470703, -0.197388) |
| 403 左/右机枪 | SGO MAB `カメラ２/３`，local (0,0.5,-0.45) | MachineGun_A/B_aim | (±1.031, 2.976846, -0.415797) |
| 404 主炮 | barbette8、object/mesh 0/0、绿色玻璃三角 768/769 | cannon_aim | (0, 5.847656, 5.496094) |
| 404 左/右副炮 | barbette8、玻璃三角 1220–1223 / 3100–3103 | subCannon_A/B_roll | (±2.039551, 7.654297, -3.941406) |

标记为 `vc_optic_00..02`，每个父骨只有一个，无可见新增几何。插入后骨数组按 DFS 重排，全部 Object.bone 与 BLENDINDICES 一起重映射；原骨的 local/inverse-bind、所有顶点姿态、三角顺序和其它归档成员保持不变。不能简单将 marker 追加到数组末尾：原生局部更新只遍历父骨的连续子树。

## 不冒充原有瞄具的资源

- 505/601 主驾驶 camera locator 在炮骨 local `(0,2,-10)`；404 主炮 `(0,6.3,-40.45)`；EMC510 的 cannon_top `(0,20,-50)`；Flak603 的 cannon_l `(-0.75,3,-10)`。这些都是第三人称取景点，不是光学眼位。
- 抽查这些型号的原版载具武器 SGO 均为 `SecondaryFire_Type=0`、`SecondaryFire_Parameter=[0]`，没有 `Sight_animation_model`。不能凭此字段给所有武器生成开镜能力。
- 对照真实步兵武器：aWeapon081/pWeapon127/eWeapon120/hCannon01 的 `Sight_animation_model` 分别引用 `app:/HUD/sight05/10/13/17.rab`；对应 MDB 只有 mdl/SightXX 两骨，所有顶点 z=0，是 HUD 平面准星资源，不提供物理镜头眼位。eWeapon120 为 SecondaryFire type 3 仍有 Sight13，HUD 字段也不是开镜能力判据。
- Proteus 的 `照準１..４` 被 `begaruta_aiming_shape` 使用，位置在 body/gun_mount/missile_launcher 枢轴，不能据此认定是镜片。
- 403 主炮没有确认到实际镜片；EMC510、Flak603、Proteus 本轮没有登记真实光学点。404 hull `front_gun`、烟幕和换了自制模型的钻头/喀秋莎不能借同类 vtable 取得主炮瞄具。

## 已执行检查

- 四个实际新模型共 7 个眼位：实际三角网格的前向射线无遮挡；加入眼前阻挡三角的负对照能被检查检出。
- 原模型全顶点蒙皮前后相等，原三角/材质/其它归档成员不变；marker 连续子树和唯一父骨检查通过。
- `tests/vehicle_optics_native_audit.py <EDF.dll>`：实际执行 `1100B90` 完整 SetWorld 和 `1100010` 父骨局部子树更新，共 14 个检查通过。运行时记录来自序列化 MDB 的 parent/child/depth_delta；没有运行 CAS、GPU 渲染器或完整游戏。

安装事务、writer 协作与实际游戏开镜画面需要分别验证；骨骼与射线检查不等于游戏内视角验收。

## 独立静态绑定复核

- 投影链 `118E03F` 读取 `cam+24`，`118E051` 取得 `cam+220`，`118E07C` 将其传给 `71F70`；实体瞄具写入的是实际投影消费的矩阵。
- CAS 绑定 `6BCC5E..6BCCB8` 按模型骨 UTF-16 名字建立映射，值指向当前骨记录的 local/world；`6BD354` 将映射传入 `1167520`，`11683CD/1168486` 按 CAS 名字查找，`1168552` 存入绑定槽。因此保留原 CAS 不会因为 MDB 的 DFS 序号重排而直接错绑。
- MAB 定位点 `6BAE43` 读取父骨名，`6BAE86` 调用 `11002A0` 按名字查当前索引，`6BAEE0` 取得新记录。没有把旧 MDB 数字索引当作新模型索引复用。

这些是对支持版本真实 DLL 的静态指令核查，不是完整 CAS 动画或游戏渲染重放。
## 安装与共享 writer 合同

`tools/make_optics.py` 提供 `build_models(root)`（仅 4 个唯一 MRAB，可缓存）与 `build_stock_redirects(root)`（每轮读取当前可变 SGO，不生成模型）。`install_models` 和 `install_stock_redirects` 分阶段安装，只清理各自文件类，避免互删；完整 `build/install` 保留 CLI 兼容。模型先装、消费者后装，使用 `.edf6vc_optics.json` 写前日志，覆盖前备份，卸载恢复可中断重入。

SGO 安装只对当前 bytes 的 model path 操作，不用旧备份重盖其它 writer 字段。卸载也只逆向自己的 model path；无其他变化时恢复原始字节，有后来改动时保留字段及当前其它 owners，并刷新 ledger SHA。改用自制模型的 SGO 不重定向；原路径 loose MRAB 与真实几何合同不同则明确拒绝，不替换它。另一消费者仍引用的 marker 模型保留。

`vehicle_optics_check.py --game` 已通过真实 SGO+DSGO 的两轮共享写入：505/601 使用实际 `make_stock_stores.derived_vehicle` 增加挂载，并两轮改变耐久；每轮光学处理都保留全部新武器/参数，缓存模型安装不删 SGO。卸载后新字段仍在，model path 返回原模型，stockstores owner 与 SHA 一致。另有写入/恢复中断、已有修改文件、外来模型、分阶段清理的临时目录测试。所有写入只在临时目录。

实际游戏开镜画面仍未验收；骨骼、原生矩阵与射线检查不等于完整游戏验证。installer/cache/发布包及下游派生生成器接线由 integration owner 统一完成。
