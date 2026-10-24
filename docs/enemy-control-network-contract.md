# 敌体玩家操控：联机 authority 与恢复契约

实现分工：`enemy_control_protocol.h` 是可离线运行的协议；`enemy_control_net.cpp` 是真实身份/原生控制桥；`enemy_control_net_input.h` 是规范语义输入编码。原生操控、技能目录、每类运动准入与真实恢复由 `enemy_control.*` / `enemy_control_native.*` 提供。

## 原生与 Coop 边界

敌体操控必须获得本机 `EDF6CoopGetExtensionCapacity` version1 的只读证明：1024 字节负载、至少 32 个远端、4096 条 inbox、1 MiB pending 与扩展拥塞 backlog、可靠有序单路由及原子 bounded TrySend。每个 peer 在 ECTL Hello/Welcome 中独立声明该能力；缺少查询、旧 256 条/256 KiB SDK 或任一成员不符，在线操控明确不可用且不回退为本地敌人。普通支援继续使用原有 ExtensionApi version1，不因新增能力查询而被禁用。配套实现位于独立 Coop 分支，实际限制由导出 API 与 transport 使用同一组常量。

本次只读核对的 EDF6Coop 为 `bbef06cb8c7f8445c1e8edd7eb42a9382649c05f`（本机 `edf6-wt-1018-coop-release-2.6.0`）。以下事实不能由“能广播坐标”替代：

- `multislot/src/networld.cpp` 的 WorldAuthority 仅钩住 `54BF39` 目标重选与 `54D79E` RNG 接收，不会普遍关闭 guest 的 AI、攻击计时器或技能动作。
- `multislot/src/worldrng.cpp::ShouldSyncRng` 只同步 team 1 的已注册本机拥有对象。受控对象若变为其它队伍，不能指望它自动补齐随机状态。
- `multislot/src/hitauth.cpp::DecideHit` 在双方都注册且 HitAuthority 门成立时，由攻击者 owner 判定命中并向目标 owner 转发；门关闭或任一方未注册时回原版。不能把 host-only 技能泛化为所有配置下都能伤害 guest。
- 原生虫类、Carrier 等按各自 NetworkObject writer/reader 校正位姿与部分状态；EventAnimationObject 默认 writer 返回 0、reader 空。每类 native/custom adapter 必须证明运动路径，缺运动契约的对象由 Prepare 拒绝。输入分支存在不等于完整技能与位姿已验证。
- 本仓 `edf41net.*` / `edf41robo.cpp` 已采用“host 语义决定、guest 受控执行同一动作/动画事件/本地弹与范围效果”的方式。这是本实现保留全端执行的依据，额外协议不替代原生 class 的发弹、碰撞或伤害规则。

## 身份和 authority

网络采用 ECTL/version3、capability 5：191 字节头、实际负载和 1 字节保留尾，严格长度为 192 + inputSize，最大 704 字节（SDK 上限为 1024）。控制消息为 192 字节，132 字节语义输入为 324 字节，最多 512 字节的完整 pose/action 批一次装入同一封 SDK 消息。旧版拆成 3–4 封的实际技能结果会放大 SDK 收件队列和重试流量；当前 decoder 拒绝旧版本、尾随 padding、截断和非规范分片元数据。

控制权是独立的 lease，绑定本局 epoch、认证 PUID、原玩家 Soldier NativeNetId、目标敌体 NativeNetId 与实际 profile hash。不会修改敌体 `NetworkObject +0x128` 来伪装客户端拥有者；原敌体仍由原生 host 发送位姿、判定其权威结果并派生子对象身份。

host 收到请求时，PUID 来自唯一 Coop poll 的认证 sender。`ResolveCommandIdentities` 一次原生全队遍历解析 source/target；`SupportCommandRequesterMatches` 将真实原生 PUID 与 sender 及封存本局参与者核对。原 Soldier 的 User/PUID/NativeID 保留。host 验证本机确实拥有目标；guest 验证同一注册 ID 在本机为远端副本。各 peer 在 Prepare、Begin、Activate 与输入应用边界重查身份/原生权限。

前端 Describe 仅接受本机玩家，不能用于 host 验证 guest。网络桥要求 UI 提供非零 profile hash，再由可接受真实远端玩家的只读 `EnemyControlPrepare` 在 host 重算并比较；缺项、不同类/模型/技能映射或未知运动路径不会用“近似 profile”放行。

fullFocusId 只表示原生目标身份，瞄准 origin/方向由真实受控体与 absoluteAim 重算，客户端不能自报原点。房主在广播前重新验证并把死亡/非法目标清零，不因此撤销有效控制 lease；guest 再失去目标时清本地 weak。

同 PUID 的分屏玩家由不同 source NativeID 区分：一个 source 只能控制一个目标，一个目标只能有一个 source。每条渲染请求另外绑定 native revision、ownerHumanKey 与请求 nonce，换世界/换房主重用 lease 数字也不会让旧输入、旧界面或旧取消动作套入新控制体。

## 开始、输入和退出

所有成员先宣告理解 ECTL/version3、capability 5（包括输入版本 3、原子物理状态同步和已接受动作确认）；旧 capability 不能开始控制。协议最多 32 个远端 peer、16 个协议 lease；原生 core 可有更小容量，满额必须明确拒绝，绝不截断或只在某端附体。

开始是内部同步屏障：Prepare（只读验证）→全员确认→Begin（捕获原始状态、零输入与暂停自主 AI）→全员确认→Activate（授予原生控制）→全员确认→最终输入 grant。尚未 grant 时不应用任何玩家技能。任一端失败则全员退出并恢复各自实际修改过的状态。

规范输入固定 132 字节（输入版本 3）：版本/保留位、move[3]、look[2]、规范 uint32 absoluteAim 标志、绝对 aimYaw/aimPitch、最多 8 个并发原生通道的 stable ability ID/hold 或 press phase，以及完整 32 字节 fullFocusId（无目标全零）。8 是同帧并发通道上限，不是完整技能目录的条数上限。移动/观察轴必须有限且在 [-1,1]；绝对瞄准为弧度，yaw 在 [-π,π]、pitch 在 [-1.2,1.2]，关闭 absoluteAim 时仅允许无移动、无观察轴、无技能、无目标的完整 neutral 样本；空 ID/phase、重复 ID 和未知 phase 拒绝；技能总目录及 ID/phase 支持由当前原生 lease 校验。没有原始指针、函数地址、CAS 索引或任意状态字符串进入网络。

controller 的 sequence/seed 不进入 payload。host 分配单调输入序号，并从 epoch/lease/序号产生一致 seed；所有 peer 依序验证并执行同一语义输入一次。原生 adapter 在真实技能起始边沿同步其已验证 RNG 落点，不能仅凭协议种子相同宣称原生散布一致。

稳态 move/look/held 合并到约 30 Hz，技能 press 与通道释放/变化立即发送，不被后续 look 覆盖。原生端另用有界有序输入队列保留同一 poll 中 press→release/换技能的先后，只允许相同 held 集合的纯轴样本合并；满队列明确失败并退出，不静默丢技能。

## 原生运动同步和补充物理状态

`enemy_control_native_net.cpp` 精确检查 Steam 支持版本的 34 类 primary profile、`+0x120` NetworkObject 次级 RTTI/COL、真实 writer/reader 槽和代码指纹。29 类有已核实的 XYZ 与朝向复制；Deiroi、GiantDangoLarge、TimeShip 原版 writer 为空，SquidSmall、UfoSmall515 缺少完整朝向复制。这 5 类由独立 `enemy_control_native_pose.*` 提供补充协议，不能因“有一个 writer”就误判同步完整。

补充状态由原生 host 约 30 Hz 读取并发布，控制者不会获得原生网络 owner 身份。每个 Pose 包带本局 epoch、lease、准确 source/target NativeID、profile hash、请求 nonce 和独立单调 pose sequence。guest 仅接收已认证房主发送的当前 lease 状态。完整 typed 状态必须在一封规范 SDK 消息中收齐后一次应用；负载上限 512 字节，当前五类规范状态为 232 字节。任何错配、收齐后原生 setter 失败或发送失败都会结束控制并恢复；guest 超过 500 ms 没有完整状态也会退出。

状态包含真实物理位置/单位四元数及各类必需的当前/目标角度、速度和技能形变缓存，按类型验证有限值、四元数和未用字段。不会复制原始对象内存。Large、Squid 与 515 通过真实 physics wrapper 读写 transform；TimeShip 更新姿态/跃迁缓存后重建骨架与物理代理；Deiroi 必须对当前/历史求解点作整体刚体变换并提交原生 ragdoll/part 物理，单独写 root/render 矩阵会在下一帧回弹。具体原生契约另由逐类离线审计验证。

## 已接受技能动作

语义输入按序消费不代表每个 peer 的原生冷却/状态机都接受了这次技能；位置同步也不证明技能状态完整。因此需要补充的原生 profile 由 host 捕获实际已接受的 typed 状态/技能步骤，通过独立 `Action` / `ActionAck` 流传输。数据仍绑定同一完整身份、epoch、lease、profile 与请求 nonce，不能发送函数指针、原始 CAS 地址或对象内存。

host 不会因发送而再次执行自己的动作。guest 收齐一批后调用真实原生应用函数，只有整批 `applied` 才确认；累计 ACK 的序号仅表示此前所有批都实际按序应用，房主还会验证这些批确已发给该 peer；`pending` 保留步骤游标并在下一 tick 重试，同一个 action sequence 不重演已完成步骤；`failed` 或 5 秒超时启动全员恢复。重复或丢失 ACK 仅重新确认，不重放已应用技能。每 lease 最多 128 批在途，全 session 共享 1024 批有界存储（16 位池索引），覆盖核心最多 8 个同时控制者各自完整窗口；guest 缓冲后按序实际应用，后续 ACK 可先登记，但只连续释放已全员确认的队头，超限明确失败，不静默丢弃动作。

生产桥只在有窗口与存储余量时读取原生 ledger，背压期间 host 继续记录自己的实际步骤；一次最多 512 字节，将多个步骤打包并流水交付。发送器由真实共享 game frame 推进，最小间隔 16 ms，每个目标 peer 每帧最多 32 封 Action，跨 lease/peer 轮转，全局硬上界为 1024 封；guest 每 32 ms 最多发送一封回执，合并同 peer 的 Prepared/Started/Activated/DrainAck/Restored/Fault/累计 Applied ACK；孤立的拒绝/无 lease 回执与该批共享同一个单封预算。host 对全部条目先验证不可变 lease+请求 nonce 绑定，再按本机封存的完整 spec 处理；同批故障先于成功终止状态。每条 link 另有 768 封已发送未确认 Action 和严格小于 768 KiB 的字节额度：按实际网络包长度加 192 字节最坏 DirectNet 认证 framing 估算，只在真实 Applied ACK 后归还。正常 quota/额度耗尽只保留原动作池，不能报 SendFailed 或丢已接受结果。Applied ACK 只证明原生动作已应用，不证明 DirectNet 独立 transport ACK 已抵达；发送前另经 EDF6CoopTrySendExtension 在 SDK 锁内按真实完整 datagram 与共享 pending 作原子准入，限制 768 KiB/768 包。wouldBlock 不改变连接代际、不消费 SDK sequence，原动作保留到真实队列恢复可写；unavailable 才是发送失败。这两层门仅保证 Action 发送不会自行越过真实 768 KiB/768 包上限，不能把它解释为 SDK 对所有业务预留了内存。普通游戏可靠包与其它扩展仍共用同一 pending；若这些背景业务自行把 shared pending 推过 1 MiB，旧布尔扩展接口仍会明确失败并关闭该扩展代际，本实现不承诺这种全局过载下继续正常控制。生产模式明确要求真实 SDK ReliableOrdered 单路由，每个 Action 只提交一次，丢包重传由 SDK 完成；不会用新的应用层副本重复占用队列。待应用的 native 结果仍在每个 game tick 重试，保留真实 5 秒故障期限。原生模块还必须证明 remote 输入/原生网络消息的确权与去重，不能一边独立按键触发技能、一边又重放 host 事件。Deiroi 的原生不同网络模式、charge/fire/cancel 与迟到消息尤其需要独立处理。

入场同一时刻只推进一个 Prepare/Begin/Activate 屏障，其它同时申请明确返回忙碌状态且不修改原生角色，完成后可重试。退出可以并行，控制回执以一封批次回复，避免 32 个远端分别对 8 个角色逐条回复造成收件溢出。

正常退出先进入 draining，停止新输入与后续触发，保留角色与原生 weak 身份，继续交付已接受的 root/child outbox；原生队列为空、协议所有批已实际应用确认、全员 Quiesce 成功后才 End/Restore。guest 的负 ACK/Fault 必须匹配当前完整 spec 与请求 nonce，不能直接清房主队列。若在房主已有动作 5 秒期限前恢复，继续完成交付；否则由房主显式广播 Abort，将该 lease 标记为失败退休并安全还原，不声称全部效果已复制，也不影响其它 lease。本地连接/epoch 丢失可退休旧 epoch outbox，绝不跨世界重放；原生还原失败仍保留弱引用重试。正常退出的 5 秒时钟从退出开始计算，并独立检查当前未确认批次的原有 5 秒期限；不会继承已完成旧批次的时间。失败退休的数据额度仍保留为在途债，直到同一路由的 End token 回执证明对端已 poll 过此前包后才能释放；不靠退休记账清零伪造队列余量。

退出、未 Begin 的取消、身份死亡/变化、原生权限变化、网络代际失效或任何一端原生应用失败，都会启动恢复。request nonce 使取消绑定到准确 source/target/候选 revision；已超时、已取消或被新请求替换的迟到 Prepare 不会突然附体。

Restore 返回失败时保留 lease/spec、原始 vptr/输入/AI 基线及 weak controls，下一 tick 可继续真实恢复；不得清记录、释放基线或报告完成。只有确认全端恢复后才给最终 Complete。恢复控制状态不撤销已经由原生引擎产生的弹丸、伤害或其它世界效果。

## 验证与实机边界

`tests/enemy_control_protocol_test.cpp` 使用真实 Encode/Decode 的三端模拟覆盖身份、屏障、输入序号/seed、分屏、请求结果关联、退出/恢复失败及旧包。`tests/enemy_control_net_input_test.cpp` 检查规范输入 codec；`tests/enemy_control_pose_protocol_test.cpp` 复用基础协议回归并验证认证房主、完整身份绑定、原子组包、防重放和物理应用/恢复失败；`tests/enemy_control_pose_codec_test.cpp` 验证五类规范状态。`tests/enemy_control_action_protocol_test.cpp` 验证原子动作批、applied/pending 累计 ACK、重传、失败、身份、队列上限、八个实际控制者同时满窗、16 位共享池索引回绕、全局/逐 peer 发送额度与 SDK framing 字节额度，以及真实 ACK 恢复后的完整排空。`tests/enemy_control_net_fixture.cpp` 包含生产桥，替换游戏入口/对象和 core 来验证真实桥的身份、authority、前端输入/取消、状态读回与代际处理。

`tools/enemy_control_trace_pack.py` 将生产原生 fixture 导出的 typed hex 轨迹规范化，`tests/enemy_control_provider_trace_test.cpp` 通过生产协议与 codec 重放。轨迹由原生 ABI seam 的实际非空 factory 返回、真实 Capture/Encode 和已核实的资源参数生成，保留空 factory 不产生结果、native invocation clock 与未解析 VM 参数的区别；它们不是实机录制。模型包含 250/600 ms RTT、有序恢复延迟、Applied ACK 丢失、配套 SDK 4096 条 inbox、共享帧最多 512 次 poll 和 1 MiB 完整 datagram pending 字节门，背景档位 0/32/64 KiB 是每条 link 固定保留的共享 pending（按 1 KiB 完整 datagram 计数），并非每秒发送速率；Action 同时经过新的真实 pending 字节/包数量准入模型。模型在实际 backlog/in-flight 未空时开始正常退出。各原生 producer fixture 的导出、打包和重放由 CTest fixture 依赖连接，可从全新 checkout 的 build 目录重建，不依赖临时快照。

这些测试不证明每个敌种的真实技能、网络动画、抓取/近战/GDI 之外的攻击、原生物理观感或两台实际游戏中的完整行为。原生能力以 adapter 准入和只读契约审计为准，完整实机验收必须按具体 profile/技能进行。本次没有给游戏发键、安装 DLL 或运行双机游戏。
