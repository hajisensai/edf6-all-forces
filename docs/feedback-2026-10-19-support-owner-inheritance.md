# 支援子实体的 PvP 归属继承（2026-10-19）

范围：航母的 gun / blast / doll 无人机、抛投无人机、人偶 Decoy，以及潜水母舰动态无人机的网络事务边界。

- PvpInheritSupport 从父实体当前弱身份对应的 native owner 缓存复制已认证 PUID。父实体确为当前玩家，或是真 VehicleBase 的实际 seat-0 玩家驾驶员时，复用 canonical requester → Command / Mark → Coop mission PUID → sealed native PUID 的同一解析器。父实体的 team 字段不是归属证据。
- 原生 Collect 将已验证实际驾驶员的 PUID 连同驾驶员 weak identity 保存到载具缓存。Plan owner 优先；同队乘客不能抢 owner，也不因为不同 PUID 使同车 Apply 失败。驾驶员变动立即使旧 source cache 失效。
- 生成边界先取得 PvpSupportTeam 的稳定提交值再 attach；Busy、Restoring、未就绪、未知 owner 均拒绝。稳定关闭且没有 owner 时保留原有离线行为；能够认证 owner 的 mode-off 支援仍登记归属，便于随后开启或换队。
- PvpRefreshSupportOwner 重新认证保存的 canonical requester 并覆盖 plan.team，不信任旧阵营，不尝试枚举 0 / 1 / 2 猜测。
- jet::Launch 在首次 Publish 前统一继承，失败 forget + Delete。实际 Delete、entry Release 和场景 reset 清除归属。
- Decoy vtable EDF+0x17D3F88 是普通 GameObject，不走 Soldier 武器或 Vehicle seat 枚举；原始 team 4 被保存并可恢复。人偶建立或继承失败时先清 follow 指针，随后 forget + Delete。
- 动态支援无人机使用 support_drone.h 的 Prepare / Activate / Delete：Prepare 直接消费 host 精确矩阵，不执行 ClearGround / Facing；独立弱记录支持已销毁 entry 的可靠回滚；pending entry 不参与 Publish / 地图命令 / AI 开火。全部参与者确认后才 Activate。注册到其他机器的副本不运行本机 AI / Fire，保留 native 网络物理。

聚焦验证：

- 五个改动 C++ translation unit 已用 MSVC /std:c++17 /W4 /WX /permissive- 编译。
- pvp_support_fixture：94 检查。
- pvp_inherit_support_test：113 检查，覆盖缓存优先、实际玩家和原生载具驾驶员的完整认证链、weak 身份替换、无 owner / Busy、mode-off 登记及 plan 刷新。
- pvp_native_inherit_test：在既有 native restore / ABA / 部分失败注入 fixture 基础上新增 40 检查，覆盖 Decoy 4 → 1 → 2 → 4、非载具路径、owner 失联、缓存读取与弱引用释放；同队不同 PUID 同车 Apply 成功且 owner 只取驾驶员，乘客遍历次序和进出不改变 driver owner。
- support_drone_runtime_test：20 检查，执行 production Prepare / Activate / Delete / common Launch，覆盖 host 矩阵、全三种无人机继承顺序、失败删除、pending / 首次发布、幂等激活、地址复用及 entry 已释放后的回滚。
- create_jet_native_test 已补无关世界服务 sentinel 并通过 MSVC 链接；未运行需要 EDF.dll 的 native 构造边界。

以上是离线源码与服务 fixture 验证。未写游戏目录，未启动游戏；全员联机事务、Decoy 原生仇恨 / 雷达 / 锁定表现仍须实机验收。

## 普通空中母舰的动态全员事务

- SupportDroneKind 明确 gun / blast / doll；资源和 slot 由 host 计划指定。CarrierSupportDroneReady 校验真实 carrier body、当前 weak identity、完整原生注册 ID、预载、kind 兼容性及 slot < 6。guest 不以自己的冷却或剩余 sortie 估计拒绝 host 计划。
- 在线 LaunchOne 只提交 SubmitCarrierSupportDrone。guest 的 kCarrierDroneRequestQueued 仅表示已排队给 host，不占本机 Session slot、不扣 sortie、不创建本机副本；真正 Prepare 到来后使用本机真实 Session token 保留槽位。
- Session 在 Prepare 前失败、尚未创建 Deployment / child 时，CancelCarrierSupportTransaction 直接扫描已有 carrier metadata 的 exact token 撤销 pending，不依赖 parent native 指针仍活着。旧 token、0 和 queued sentinel 不影响新事务。
- Reserve / Confirm / Cancel 绑定 exact parent weak identity、slot 与 process-monotonic token。pending 防止重复使用；Confirm 在 Activate 成功后只扣一次 sortie；Cancel 不退款已消耗 sortie，保留已回收/自爆的 10 秒补充，损失走原 30 秒补充。旧 token、旧 Dock、旧 Detonate 不能修改新 slot。
- 缺少本机母舰 entry 的端仅创建 metadata，不发布或运行 AI。已激活的 remote support child 以 native Alive/dead/reap 判断存在，不因本机 seen 超过两秒被误判丢失。
- PrepareCarrierSupportDrone 的 ordered 位仅承载经过认证的玩家目标意图；该意图在 Confirm 时才同步母舰 order 并清旧 child target，取消时丢弃。NPC ordered=false 保留原围绕母舰的逻辑。
- 回收通过 SubmitCarrierSupportRecall 到 host；ApplyCarrierSupportRecall 仅 host/offline 改实际 child 的 recover mode。完成回收或死亡通过 SupportDroneLeft 交给 Session 的全员 Destroy，JetReap 不自行删除待事务管理的 live support child。
- staged doll 在每端激活前创建，根据已注册 child ID 选择相同资源。缺 DLC/创建失败/归属继承失败均保持 pending，交由全员事务回滚。Delete 清 Decoy follow、归属和 native 对象。
- prepared weak 账本容量为 support_net::kMaxTransactions（128），与本局 Session 上限一致，独立于 live jet table；自然死亡的历史身份保留到 Destroy/reset，延迟回滚不会删除同地址的新控制块对象。

聚焦 MSVC fixture：carrier_drone_transaction_test 共 386 检查，链接真实 jet_carrier.cpp 并直接执行 production jet_spawn.cpp。以三个端的服务环境依次覆盖三种实体的 Prepare / Activate / Confirm / Cancel / 回收、人偶资源一致、host preflight、guest queued/recall、过期 seen、40 个已激活死亡历史与同地址新 weak identity 的旧事务回滚。该测试是单进程服务模拟，不是实际双机或三机联机验收。

该 target 需要与 src/jet_carrier.cpp 一起编译，链接 edf6common，并使用 /Gy 和 /OPT:REF；fixture 已直接包含 jet_spawn.cpp，不能再次单独加入同一 target。
