# Deiroi 主炮与逐腿炮玩家控制

该模块对应用户「所有敌方单位可操控，能使用所有技能」的 Deiroi 子武器部分。主机体原生按钮 4 为脚链踩踏，按钮 5 为主炮；腿炮是独立的 `DeiroiFootParts_Battery`，不能将主机体两个按钮当作全部武器覆盖。

## 对象与技能身份

只沿已接管 Deiroi 的原生 SGO 配置链枚举：`root+AE8/count+AF8` 每条腿 0x40 字节，`row+28` 为腿控制器，控制器 `+10/count+20` 保存原配置顺序的 0x10 字节节点项，项 `+8` 指节点，节点 `+8` 指真正部件。

`31D830..31D8D8` 按原 SGO 腿顺序构造，`324760..324ABB` 按部件配置和重复次数展开。`3248BD→118AF20` 将部件挂入原生 SceneObject 父子注册表；枚举必须同时证明部件仍在 `root+48/count+58` 注册表、`child+38==root`、`child+5C0==root`、精确主虚表 `17AB118` 和原生 RTTI。它不扫堆，不使用 AI 列表，不依赖空间距离。

wire ordinal 为 `(配置腿索引<<16)|展开部件索引`，不传指针，也不按地址或当前剩余部件重新编号。死亡或移除一个炮位不会改变其他炮位含义。Root.cpk 八个主机体的实际腿炮数量依次为：标准12、L12、L2为9、M/S/SM各3、XL33、XL2为30。每门炮分别提供「准星射击」「恢复自动」。

模块持有根与已登记炮位的弱身份，在调用原生状态转换期间临时锁定根/子对象的强引用；地址复用、父属变化、死亡、禁用、未知炮型均拒绝。恢复路径也能按身份找到尚保留尸体的死亡根对象，释放引用和控制槽。

## 原生生命周期与随机数

只过滤 `32D286→32C310` 这一处自动 idle→charge 决策；其他部件及整个子对象 slot5 `32B210` 原样运行，保留死亡、断肢、碰撞和原生 frame5 更新。受控联机的发射与状态使用独立可靠已接受通道，旧网络包只消费原生计数，不重复触发武器。

显式发射必须处于原生 idle `32D0C0`，且部件未禁用、父属/弱身份有效。调用 `32C310(child+AD0,{32C750,0,0},nullptr)` 进入原生 charge；它在真实 `B78` 充能后进入 `32C880`，使用 `B90` 原生武器初始化、连发、结束、收炮与取消。不会裸播 CAS、重造子弹或绕过真实进行中的武器状态。

`B00` 是 idle 从 `B04/B08` 生成的 AI 随机选招间隔，只在 AI 有目标且满足距离时递减，不能冒充武器装填冷却。手动允许盲射并绕过该 AI 决策条件，但不写 `B00`。同一 wire seed 播入根的两个原生 RNG 和实际选中炮位的 `3E8` RNG；每门炮按 sequence 去重。忙碌或禁用的首次拒绝不会改变手动/自动模式。

显式新攻击还必须由核心 `EnemyControlCurrentInput` 证明 absolute aim 新鲜且 sequence 与此次请求相同；`EnemyControlWeaponAim` 可以保留上次已确认点供正在进行的连发结束，不能将它单独当成新攻击的新鲜度证明。权限先取受信 `NativeActionRole`；远端意图不直接进入原生状态。无绑定角色时仅 `EnemyControlNetworkContext` 证明该精确租期离线才执行，未知上下文和缺角色联机均拒绝。

只读 DeiroiGunManual 按真实当前根身份、稳定炮位及 manual 状态区分来源。导弹在手动时使用确认准星和锁点；恢复自动后保留原生 AI 目标选择，网络只记录该次真实目标，不能把自动导弹继续强绑玩家焦点。主炮使用原生玩家按钮5，始终属于玩家瞄准来源。

恢复自动或整体释控，仅取消本适配器发起而仍进行中的 charge/fire/close，通过原生 Set→idle 执行旧状态退出及 `6CFC00`，随后放行自动决策。不恢复裸状态指针，不改 AI 目标或原生网络计数。原生转换异常时先用同一取消路径回滚；取消失败保留引用并要求核心重试恢复。

## 瞄准与全部炮型

炮口姿态的原生调用 `32B422→3296E0(child,worldPoint,gain)` 与真实武器的 `32CFE8→6CFC60(holder,mutableMatrix,seed)` 都读取核心 `EnemyControlWeaponAim` 的同一份当帧准星命中缓存，不在武器 hook 重入物理查询。核心仍占有对象但缓存失效时，不换成另一条射线。

最终发射矩阵原地修改，保留发射位置、各行 w、尺度、弹幕相对夹角和随机数。离线继续原调用；受控联机在实际原生 factory 结果成功后记录最终矩阵和参数，由主机可靠流重放。旧 `32CFF8→32B460` 发送器在该租期被抑制，避免迟到原包重复开火。

炮型 `B30==0` 使用原生 `B40` 源轴；`B30==1` 从原生定位器 `6BB5A0(child+B80)` 取源轴。将原有完整矩阵的三条旋转轴统一施加从源轴到玩家目标轴的最短弧旋转，保留扇形/环形子弹彼此夹角；不能直接重建最终 LookAt，否则会把整套弹幕压成单一方向。相反方向使用定位器 right 正交轴做180度旋转。

## 主炮身份与状态

主炮由独立的 `root+1638/capacity+1640/count+1648` 原生 vector 保存，每项 0x290 字节，`row+0==root`，`row+100` 为真实 gun holder。它是根拥有的普通结构，无独立 SceneObject/weak control。适配器只接受精确 root 弱身份下的完整数组成员，wire key 保留 group32，即 `(32<<16)|原生行索引`，不传 row 指针。八个原版 Deiroi 配置各有一行主炮。主炮目录仍使用原生按钮5，逐腿炮目录不因此重新编号。

原生主炮 machine 位于 `row+B0`。`3211C0(machine,callback,nullptr)` 的第二参数是直接函数指针，与腿炮16字节 member descriptor 不同；phase6 为 `31CB60` Fire，phase7 为 `31CDB0` idle。进入和退出不消耗根 RNG，接收已接受主炮 phase 不回写由独立 root action producer 共用的 RNG。死亡根只消费已接受 phase，不进入新状态；已持有的真实 factory 可用于排空死亡前已接受发射。

主炮的 canonical aim 由独立武器适配器在 `31CC4B` 使用原生 `2312A0`、原速率 `row280` 和真实炮口位置计算，再继续原 spread/RNG/factory；`31CB9A→6CFBC0` 保留定位器位置并更新预热/枪口姿态。实际结果在中央 factory dispatcher 中归属 Deiroi provider，主炮和腿炮共享同一可靠结果流，不把 root tag5 开始计数当作实际子弹。

`6D093F` 的 `holder34` 递增只证明发射尝试，下游 factory 可以返回空，不能据此排出假成功事件。完整网络契约和实际成功边界见 `enemy-control-deiroi-net.md`；中央共享 hook 由 `enemy_control_gun_factory` 唯一持有。

只读状态/释放失败不能被解释成对象消失。主炮 row 暂时只读时，恢复返回 false 并保留 owned baseline、根身份和弱引用；页面恢复后再次执行原生 idle 退出。已确认销毁的控制块与仍活着但不可写的对象严格分开处理。

## 导弹实体与旧目标门

原版 Missile 炮位的 SGO 类型是 EntityMissileBody（TypeEntity 参数虚表17E6F08），不是 TypeBullet。真实 Fire6CF910 使用 param280 的 RawIP，BulletCreateInfo 嵌于 RawIP80、即 param300。RawIP50/58 目标 LockPoint 被外层原生构造器保存，再复制给内部 MissileBullet01；真实空目标允许盲射，非空锁点使用稳定 actor 身份和真实 ordinal。

独立 enemy_control_deiroi_entity_aim 模块只在精确 manual-source 或 accepted-replay factory scope 中绕过6CF95A 的旧 AI 目标门。原来目标存在但没有锁点时会提前退出，连真实 factory 都不到；新路径进入6CFA71，保留原生创建、弱引用清理、muzzle效果、post-create和身份注册。恢复自动的炮位原样执行该 AI 目标门及目标选择。

14字节 patch 精确覆盖 TEST RDX/JE/MOV EAX/TEST EAX，并在原6CF968 JE前恢复 native fallback；八次 push 加 sub80 保持原16字节栈对齐，保留7个 volatile GPR、6个 volatile XMM、flags/DF和 Windows shadow space。原参数 RBP 不改，shim 不写 RNG、不制造目标对象、不改 actor 上的旧弱引用。运行时逐字节检查当前 site 及整个 thunk，foreign替换会拒绝就绪；早期安装不依赖后续中央 factory 初始化结果。

6D03C2→781630 按父身份哈希、nativeParam90 index、nativeParam94 sub 注册子对象的同一原生身份和拥有端，不是生成广播。该原生后处理必须保留；可靠流只负责一次实际创建，不能再把注册误作另一条创建通道。

## 状态显示与验证

只读状态查询保留技能 ID/名称，只刷新原生可用性：失效/禁用为 unavailable，自己发起的 charge/fire/close 为 active，其他非 idle 状态为 cooldown，idle 可用；自动条目反映该炮当前是否已恢复自动。没有可信秒数，因此 `cooldownKnown=false`。

`tests/enemy_control_deiroi_test.cpp` 直接包含生产实现，以合成对象及原生边界桩检验配置顺序、Scene 注册、双父属、弱/强引用、断肢稳定 ordinal、单炮控制/恢复、忙碌拒绝、异常回滚、死亡根释放、同源准星、原地网络矩阵、弹幕夹角和真实状态显示。

`tests/enemy_control_deiroi_native_audit.py` 独立读取支持版 EDF.dll 与 Root.cpk，校验实际指令、真实虚表/RTTI、原生生命周期和八个配置变体。该审计不加载 DLL，不运行游戏，不写游戏目录。离线通过仍不等于实机炮口对齐、真实伤害或双机联机验收。

当前源适配器验证：严格 `/W4 /WX /permissive-` 生产夹具 158 项通过，支持版 EDF.dll/Root.cpk 独立审计 453 项通过。覆盖主炮 direct-callback ABI、数组身份、共享 RNG 不回退、新攻击新鲜度、未知网络上下文拒绝、真实只读页面阻断取消、控制块只读阻断释放、解锁后重试以及死亡根对象清理。网络 provider 的独立测试和冻结状态单列于网络契约，源夹具通过不代替其验收。

原版 XL 的实际配置为30门 Beam（85发/轮、间隔7 native frames、充能240 frames）和3门 Missile（4发/轮、间隔8 frames、充能120 frames），每次调用 factory 数1。按60次原生更新/秒、所有炮同时 eligible 的保守配置上界为 `3915/14≈279.64` 发射尝试/秒。60秒独立配置推导轨迹为10608次尝试和864个phase；这是原资源和原生时序推导，未计 AI 等待/收炮延迟，不是实机录制，也不保证每次尝试分配出真实子弹。审计可在工作区 tmp/build 导出 JSON 供可靠窗口压力测试。尚未进行实机或双机验收。

独立导弹 gate 夹具另有240项检查，直接执行生产生成的 x64 并和原生分支检查点比较，覆盖 NULL/strong0/strong>0、PAGE_NOACCESS旧目标下的手动和重放、predicate异常fallback、GPR/XMM/nonvolatile/flags/栈/RNG保持及 foreign site/thunk 拒绝。对应只读 native audit 23项通过，未运行或改动游戏。
