# Sazabi 联机动作复制

适用 EDF.dll `0x678CCB46`，代码 `sazabi_net.cpp/.h/.inc`。机体位置和普通 holder 枪弹继续走原版 506 同步；这里补齐插件自己的关节姿态、盾、近战动作、蓄能炮及浮游炮，所有要显示这些动作的机器需安装此版本。

## 通道与载荷

- 只链 506 NetworkObject slot 17：vtable 项 `0x17DB4C8`，原函数 `0x6325B0`；发送经对象 `+0x120` 的 slot 16 / `+0x80`（`0x773DA0`）。原版对象描述符负责寻址同一机体。
- 类型 15 + `SZBI` magic + version 1。钻头的 505 接收槽 `0x17DB0D0` 独立；13/14（coop）及其它 magic 读位置复原后交给下一处理器，不吞掉别的插件消息。
- wire 是显式整数/浮点字段，两个保留字初始化为 0 并在接收端核对。没有指针、`bool`、原生 `PoseInput` 内存或隐式尾部填充。`sizeof(State)==888`、保留位 offset 880 编译期钉死。
- 普通状态每 50 game ms；动作/盾门槛变化、光束和声音立即发布；空机每 500 ms 重发。绝对快照恢复漏掉的发射、收回和松盾，不依赖 coop 分片。
- 真实 DLL 测试运行 `773DA0 → 72EED0`，以私有 recorder 替代 `750130` 的队列/peer 访问，再运行原版 `750380` 最终事件包写入。最大宽度对象/event/sequence ID 实测：状态 **888 B**、对象消息 **896 B**、完整应用包 **905 B / EOS 1170 B**。没有发送 EOS 网络请求。

## 操作者、对象生命周期与播放

复用 `online_authority` 契约：有活跃注册 seat-0 驾驶员时由其机器决定；空座和只有 host 存在的 Dummy 都由 host 决定，不沿用末任驾驶员。wire controller 也是当前注册驾驶员 ReferenceId，空座/Dummy 为 -1。`0x785050` 会消费 weak 参数，调用端传增持的本地副本，不消耗座位或目标对象持有的 weak。

每个 `Mech` 的 `ObjRef` 内保存最多 1024 sender 的序号水位。拒绝重复、乱序、本机 authority 的回送，以及当前 controller 不匹配的旧驾驶员包。任务重置和地址复用不继承旧对象的水位。远端盾只接受 1500 ms 内、仍匹配当前 controller 的状态；失去 authority 的旧本地状态也立即失效。

远端在 `Drive` 最早分支进入播放：直接写本机骨骼记录及特效，不读取本机键盘/手柄，不运行 NPC 选敌、武器触发或伤害决策。`SazabiBodyStep` 同时检查 remote 标记与当前 authority，不能因复制来的 `driven=true` 覆盖原版机体物理。

六个浮游炮各带 phase、world position/direction/velocity、目标 ReferenceId 及最小计时数据。远端不把 wire ID 当成自己的候选敌人数组索引，也不依赖目标已在本机出现；按定点 world 快照画。驾驶权转移时，新 authority 从收到的 world 位置规划返航，重新建立自己的控制输入和目标列表，不接续其它机器的未传输曲线路径或未完成攻击。

## 盾与特殊效果

盾复制 guard、朝向与驾驶端 GuardShare。目标对象 owner 在 `SazabiMessage`（slot 9）看到这些状态后执行减伤，位置与驾驶 authority 可以不同。coop W3 的转发 pre-filter（slot 10）先于它；被转发的命中在目标 owner 上减盾一次，原值仍由原有 `MessageRestore` 恢复。

蓄能/炮扇使用 authority 判定后的固定起终点；浮游炮每次单发有序号、起终点和年龄。远端按序号播放一次，超过 500 ms 的历史单发不补播；短声音用四槽最近事件环，同样去重和过期。普通 holder 枪弹不再触发一遍。远端特殊光束只调用 `EmcFire(...,0.0f)`；底层仍会创建有碰撞回调的对象。

`tests/zero_damage_native_audit.py` 执行真实 setter `0x2B82E0`、Bullet 参数复制 `0x2307F0`、GDI 写入与衰减 `0x231080`，证明零伤害不会被恢复成默认值或最小 1；**未覆盖所有目标类的受击、硬直或冲量副作用**，因此不能把零 HP 伤害称为完全没有碰撞副作用。

## 可复现验证与边界

- `sazabi_net_test`：固定 wire、字段范围、未知身份、丢中间包、重复乱序、controller 交接、1024 sender、对象重置。
- `sazabi_net_runtime_test`：直接执行生产 receive/Drive/Pose/Message/BodyStep，验证盾臂骨骼、近战/浮游炮姿态、零伤害且不选敌、单发去重、目标 owner 护盾、过期与换人失效、物理权威隔离。
- `sazabi_net_guard.py`：模块边界守卫，防止播放路径重新调用输入/AI/攻击，或物理/护盾绕过网络门。
- `sazabi_net_native_test <EDF.dll>`：独立进程 `DONT_RESOLVE_DLL_REFERENCES` 映射 DLL，不运行入口、不附加游戏。两种载具插件同时装钩，原版 codec、未知 tag/magic 透传、原版接收忽略、完整封装预算。

以上是离线生产路径与原生 ABI 验证；没有运行实际双机房间。20 Hz 绝对姿态尚无插值，实际丢包/延迟观感、完整目标受击反应和长时间房间移交仍需实机验证。
