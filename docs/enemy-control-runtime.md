# 敌方操控运行时与覆盖边界

支持 Steam EDF.dll 时间戳 `0x678CCB46`。这是源码、生产夹具和本机 DLL 的只读审计记录；没有运行游戏、写游戏目录或完成双机验收。

## 运行时

`enemy_control.cpp` 在游戏线程持有最多 8 个独立会话。原玩家保留真实 User/PUID/NativeNetId 和原位置；受控敌人保留真实 GameObject、物理、动画、部件及原 NetworkObject 所有者。原玩家暂时暂停，按租约只新增 `+0x380` 的普通免伤 bit 0 与 NoTarget bit 7，归还时只清本租约新增的位。任务脚本强制伤害仍走原生规则。

普通原生对象按实例复制完整虚表及 RTTI，只接已核输入槽 4/5、AI 决策槽 7 和真实目标选择槽 14。每次原生输入调用短暂借用稳定的控制器存储，并由原生适配层最终归还 `+0x340/+0x354`。自有 4.1 控制器通过 `EnemyControlAdapter` 使用其真实状态机。敌人自己的移动、武器充能、CAS/MAB 事件、碰撞伤害与死亡继续走原生路径。

共享瞄准由受控体真实锁点及接受的绝对 yaw/pitch 构成；射线只在游戏线程生成缓存。武器钩仅读缓存世界点、真实 actor/LockPoint 弱引用，不做物理查询、不伪造 GameObject。空目标也由私有槽 14 返回空，防止原生 AI 再次选回旧目标。`fullFocusId[32]` 只有真实网络身份；房主按自己的身体位置重建射线并验证，目标失效归零，不撤销整个操控会话。

通用 Unit 发射描述符在原生回调完成后、实际 Factory 前替换真实目标弱引用，保留原作者矩阵和弹幕形状；空目标清除模板中残留的旧 AI 锁点，离线真实锁点不依赖网络 ID。原描述符、选中锁点和 root 引用由持久账本保留，页不可写或原生释放不确定时停止新触发并等待恢复。实现和 6332 项生产机器码检查、374 项 DLL 审计见 `enemy-control-unit-guide.md`。

主输入按序保留边沿、释放及技能切换，纯相同 held 集合的移动采样可以合并。队列上限 64，溢出明确失败；同一 sequence 的 pressed 只进入原生一次。没有绝对视向的输入只允许完全 neutral，NativeID padding 固定为零。不同本地玩家不共享键盘、手柄、会话 token 或技能卡。

正常在线退出先 Quiesce：停止新意图，保留已经真实接受的动作与出弹账本，经可靠协议 Capture/Applied ACK 排空后再 Restore。连接/epoch 丢失等可信本地失效才允许 Abort 退休旧账本。关闭期间保留原生角色门与弱身份；同一 body/control 的旧会话未恢复完成前不可开始下一会话。字段、虚表、目标锁或弱计数页暂时不可写时，保留恢复基线，不发送虚假的完成确认。原玩家保护直至实际控制归还。

HUD 只读发布的数值快照：受控体真实 HP/maxHP、当前技能原生可用性、阵营及原角色保护状态。小雷达和 M 跟随受控体；绘制线程不解引用控制体。关闭/退出发布空雷达集。原生闭合阶段 team=-1 时，雷达观察者使用已核 `+0x318` lastTeam。

## 资源覆盖矩阵

资源全集来自 `feedback-2026-10-19-enemy-control-coverage.md`，不是召唤菜单的 21 行默认列表。

| 来源 | 原生/自有类型 | 资源候选 | 分派与限制 |
|---|---:|---:|---|
| Root 普通敌人 | 34 原生类 | 183 SGO | 精确原生输入表；真实变种装备/动作动态枚举，未知配置拒绝完整准入 |
| 4.1 原生对应敌人 | 10 原生类 | 53 SGO | 同一原生类型适配，但保留实际 4.1 武器、CAS 和扩展插件行为 |
| 4.1 自有敌人 | UfoRobo、UfoMother301、UfoMother401 | 19 SGO | 自有记录/工厂身份，不按其借用的 EAO/Carrier RTTI 误分类；401 的全部 17 变种不去重遗漏 |
| 结构型特殊根 | MovingFortress、UfoMother511、EDF6_GrandMother、EDF6_Jormungand | Root 8 + 4.1 1 + EDF5 DLC 1 | 多部件结构/建筑型例外；不把主矩阵移动当作完整操控 |

34 个原生类分组：

- 昆虫与盾兵：AlienTrailer、Deiroi、DragonSmall、GiantAnt/EX、GiantBee/EX、GiantDango、EDF6_GiantDangoLarge、GiantSpider/EX、Nephila。
- 战斗机体：EDF6_Berserker_Large/Middle/TypeA/TypeBommer/TypeGrapple/TypeShooter。
- 人形：Humanoid_Basic、Humanoid_BigGreyBoss、EDF6_Martian、EDF6_Venus。
- 其他生物：EDF6_Merman、EDF6_FlyingDyno、EDF6_ShellFish、EDF6_SquidSmall、Monster501/504。
- 飞船：EDF6_SpinnerUfo、EDF6_TimeShip、DropBoat512、UfoCarrier508、UfoSmall507/515。

Hector 的继承模板若没有真实 CAS 动作，不列出不存在的 armStomp；保留该变种实际左右炮。Humanoid 独立武器沿真实装备槽过滤，断臂与未知活武器分别处理。UFO507 的大炸弹是对应变种既有武器配置，原生缩放调试键不是技能。DropBoat512 原生只有舱门/降落转换，没有移动轴，不人为制造移动。盾兵保持原生转身与护盾约束。

建筑、卵、蜘蛛网/卵囊、时空囊、独立炮台/弱点/脚/门和特效对象不作为独立玩家角色。TimeShip 的 9 项玩家命令有各自完整入口；任意 AS `CreatePodCrue` 字符串资源工厂不属于这些玩家技能，控制中的在线额外脚本注入受约束，已存在乘员仍按原生投放。

## 验证

核心生产夹具直接包含 `enemy_control.cpp`，模块边界显式 stub，其原生行为由各模块独立夹具/真实 DLL 审计验证。核心当前 183 项通过，覆盖：多源身份、pressed/held/同帧 press-release、队列溢出、失败恢复保弱引用、死亡对象归还控制器、源保护原值、真实 HP、地图/雷达归属、相机近障碍和 t=0、真实 actor/LockPoint、空槽 14、技能 revision、接受动作排空与可信 abort、晚到原生身份注册、旧网络失败不污染新候选、P1/P2 地图与关图 drain 不串输入、借用真实导引身份不增弱计数、原生导引钩子故障保留弱引用账本并在解除保护后恢复。`enemy_control_source_native_audit.py` 10 项通过。

输入/动作/武器、PVP emitter 树、实际 Factory 返回与可靠出弹 outcome 的更细证据分别见 `enemy-control-native-re.md`、`enemy-control-target-re.md`、各家族及网络文档。类型/SGO 数是资源与分派覆盖数，不能替代实机手感、每个任务环境和真实双机验收。
