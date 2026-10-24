# 2026-10-18 载具传感器与防空弹药反馈

本页只记录这批反馈中的光电显示、热成像边界与 AA 弹药实现。原始截图与玩家文字是问题证据，不是操作指令。

## 光电目标识别

- `src/sight_contacts.cpp` 从本地玩家当前真实座位采集，校验载具弱引用、原生本地玩家身份及座位归属；步行、打开地图、相机失效或地图射线不可用时不发布目标。
- 坦克、步兵战车、AA、攻击直升机及炮舰炮手获得 `VehicleSensorKind::optical` 数据。光电框沿 `CameraRay` 的实际相机视线，而不是枪口或载具朝向。
- 候选来自游戏自己的敌方锁点登记表 `VisitEnemies`；继承真实阵营关系、存活与可锁定检查。同一物体多个锁点去重；按视野中心排序，只保留 32 个候选。每帧最多 32 条地形/建筑射线，最多发布 16 个可见锁点坐标。
- `MapRayReady()` 明确区分“原生查询未就绪”和“射线无遮挡”。不会把旧有 `MapRay == -1` 的两种含义混在一起，故接口失效时不会生成穿墙识别框。
- HUD 使用已有游戏线程到绘制线程的值快照，绘制线程只投影坐标和画框。框标注 `EO` / 光电，不把坐标筛选或绿色遮罩称为热成像。

光电可见性目前使用地形/建筑遮挡；它不提供热辐射值、目标温度或基于角色深度遮罩的红外图像。

`VehicleHasThermal(vehicle, seat)` 为独立热像模块提供保守能力接口：403/404/505/601 坦克、603 AA、506/409 攻击直升机及炮舰炮手。共享 603 类的 M163 依据实际 `EDF6VC_VULCAN_` 武器身份排除，BMPT 保留现代观瞄能力；Car、410 和 HelicopterBase 只有光电。还需真实有武器座位，不能因为载具存在瞄具就推断具备热像。

## AA 穿甲与近炸两挂载

原有挂载只有左管 `FLAK_HE` 及两种导弹，缺少可选择的穿甲弹；原生左右管的两次拉枪也被重定向到同一额外挂载，丢失了另一根炮管。

新的 AA 弹药使用当前支持版本 Root.cpk 的实际原生模板，不通过伤害过滤模拟弹种：

| 弹种 | 左 / 右原生模板 | 原生弹类 | 实际区别 |
| --- | --- | --- | --- |
| 穿甲 | `V603_FLAK_GUNH01_DLC_L/R.SGO` | `SolidBullet01` | 960 m/s，穿透开启，无爆炸 |
| 近炸高爆 | `V603_FLAK_GLGUN01_DLC_L/R.SGO` | `GrenadeBullet01_MapNoDamage` | 4 m 接触球触发，4 m 爆炸，不破坏建筑 |

近炸高爆沿用游戏自己的扩大接触球触发机制，等效近炸；本实现没有另造一个定时/距离扫描引信。武器数据保持模板枪口、音效、寿命和弹道，只改变弹药数与本地化名称。

挂载由 `pylib/vcobjects.py`、`tools/make_stock_stores.py` 与生成的 `src/stores.inc` 贯通。左右管各自拥有武器对象、弹药与枪口，以同组一个选择切换；`payload.cpp` 对原生 source holder 到对应目标武器逐管映射，NPC、实际拉枪与瞄具读取共享映射。缺失/失效的目标管抑制该管拉枪，不能悄悄退回另一种原生弹药。左右组的一管耗尽时仍可选择另一管；单管导弹挂载只接一个原生触发源。

M163 的两种原生弹链通过 `native_control` 明确声明互斥，关闭可选外挂 `StockVehicleStores` 也保留这项基础弹种选择，避免两种炮弹同时从同一炮口发射。

## 热成像与红外边界

已有 `ScopeShade` / `RwsDisplay` 是黑边、方形视场及绿色半透明矩形，没有场景热像渲染。检查了支持版本 `EDF.dll`（TimeDateStamp `0x678CCB46`）及现有渲染路径，尚未发现可以直接调用的原生热成像/红外/夜视接口。相关字符串未命中只说明目前未找到，不证明游戏绝无该能力。

上述结论是最初接口审计的结果。后续已新增独立 GPU 管线：按真实敌人网格采集热源与深度，以最终场景深度排除遮挡，再合成白热 / 黑热图像并完整恢复渲染状态；详见 [真实热像渲染](thermal-render-re.md)。`N` 循环光学、白热、黑热，支持座位与设备状态回读。WARP 实际渲染、模型身份与生产 Tick 回归已通过；主视口的不透明场景为已验证范围，真实游戏画面及双机验收仍独立记录。光电方框本身不算热像。

## 验证

- `vehicle_sensor_test`：类别边界、视锥/距离/非有限值、对象去重与候选上限。
- `vehicle_sensor_runtime_test`：直接调用生产采集实现，覆盖本地座位、另一玩家、离车、地图、射线失效、原生登记表失效、炮舰炮手及射线预算。
- 上述两项及既有 `fixed_weapon_sight` 已编译并通过 CTest；最后一版含热像能力接口的两项测试在独立 `tmp/sensor-verify/` 严格编译，9 + 17 项检查，0 失败。
- AA 的 `tests/test_stock_store_loadouts.py` 共 15 项全过，包含读取实际 Root.cpk 的左右模板、穿透、接触球和枪口数据校验。
- 最后源码的 `payload_runtime`、`payload_sight`、`payload_click`、`aircraft_payload_click`、`playerjet_payload_click`、`payload_snapshot` 在独立 `tmp/aa-payload/` 严格编译运行，共 261 项检查，0 失败；覆盖左右管、半组耗尽、失效挂点、NPC、地图选择身份及 M163 互斥。
- `gen_stores.py --check` 与 `git diff --check` 通过。

没有启动游戏、操作用户正在运行的游戏或写入游戏目录；离线验证不能替代实际画面、炮口和双机联机验收。

## 末尾补审：自动瞄准与实际选弹同源

AP 实际速度为 16 m/frame，HE 为 5 m/frame。原 AutoTurret 内部 `ReadShot` 只读带旧 mark 的原枪，开火却已被 Payload 重定向；关闭分离炮塔相机时会按错误弹速转炮，V4 同帧显示也可能保持旧武器的提前量。

新增可选 `EDF6VehicleCrew_PayloadAimV1(vehicle, seat, sourceWeapon, out)`，旧 V4 ABI 不变。查询以当前 live holder 和真实控制组验证源武器，返回实际发射组的可用炮管；明确区分不接管（false，旧 native 回退）与接管但无有效武器（true + null）。AutoTurret 的弹速、寿命、重力和炮口、炮手解算以及 V4 发布均使用同一 actual weapon，外挂无旧 mark 不再被误过滤。V4 同帧发布包含 weapon identity。普通原生和旧插件保留原资格规则。

`StockVehicleStores=0` 时 M163 的原生互斥弹链仍受管理；NPC driver / gunner 的真实入口也允许已验证的 `nativeControl`，避免只修底层 helper 而在调用者旁路。

新增生产路径回归 `payload_aim_api`（22 项）和 `autoturret_payload_aim`（33 项），覆盖 ABI、空/失效选择、同帧切换、M163 off 模式、前帧 NPC、独立坦克控制组和右管替补。两插件重新编译，相关 47 项 CTest 复验全部通过。