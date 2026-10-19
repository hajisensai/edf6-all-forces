# 潜水母舰与普通母舰无人机的认证事务（2026-10-19）

原先潜水母舰由本机武器回调直接 SubLaunch，未经过普通支援全员事务；潜舰 Bay 和普通母舰 gun / blast / doll 也有只在 native authority 本地生成的路径。此轮把真实创建、归属、激活和销毁纳入现有 support Session，删除特殊潜舰 PVP 禁用分支。

## 入口与部署时机

- 地图 SupportCallAt / SupportSubCallAt 和空袭兵 RadioStartHook 统一提交 catalog SUB。RadioStartHook 只接受本地实际玩家的请求；远端 native 回放不再次提交或创建本地副本。真实 native IFC 启动回调仍按原有约定返回。
- 在线房主 PlanForRequester 使用 shared transport 已认证 peer 对应的实际 native 玩家位置。PlanSupportSubmarine 在已预载、空余舰位与真实水域约束下计算一次 host 精确矩阵及独立 afloatY；hull 与一个真实 Ranger pilot 在相同矩阵生成。离线也走同一部署生命周期。
- 各端 PrepareSupportSubmarine 复制 16 字节对齐 native 矩阵，先建空舰，再注册 host 指定 native ID；pilot 使用同一事务的指定 ID。全批对象创建后先 PvpAttachSupportPlan，再绑定母舰 owner，任何失败销毁已建的完整或部分批次。
- Session 全员确认 Active 后，host 才让真实 Ranger 登乘并 Activate；guest 不独立登乘，在 native RideVehicle 事件到达前保留已激活的 remote hull。舰体控制、上浮、伤害、维修、装填、发射和 Bay 只由 native authority 执行。guest 显示 native transform / HP，并消费独立子系统状态。
- 潜舰 Bay 的 SubmitSubmarineDrone 保留已认证 canonical requester，重新解析 owner 当前稳定提交的 PVP side；通过母舰完整 native ID 派生的唯一 key 找回 live parent，再提交一个准确矩阵的 child Plan。child 同样要全员准备、归属附加和 ACK 后激活。

## 普通母舰的相同子实体路径

- jet_carrier 的 online LaunchOne / PlayerLaunchDrone 不再直接生成本机无人机。host 的自动出动走 CommitCarrierDronePlan；guest 只通过 AFDQ 请求 host，返回 queued sentinel，不占本机槽位、不扣 sortie、不建副本。
- guest 请求只携带完整 canonical parent native ID、kind、slot 和目标点。host 严格核对 transport PUID、shared Session 当前实际玩家、真实 Vehicle seat-0 driver、Command / Mark native PUID 和 sealed mission PUID；同队其他玩家不能代发。母舰的实际 pose 由 host 读取，不能由来包提供矩阵、阵营或 owner。
- BuildCarrierDronePlan 使用已登记父实体 owner 或严格认证的实际驾驶员，不能用 team 字段推测 owner。完整 32 字节 native ID 经全队伍 native 枚举验证唯一后产生 parent key；duplicate ID、hash collision、weak identity 失效、枚举器变化均拒绝。
- kind / slot / ordered 位由资源编码承载，Validate 独立校验真实 carrier family、kind 兼容、预载及 slot 范围。host 预检当前槽位、冷却和 sortie；guest 不根据滞后的本机计数否决 host 计划。
- SubmitPreparedSupportPlan 成功后保留 exact token 对应的 slot。Prepare 创建 pending child，ACK 后 Activate 再 Confirm，成功后只扣一次 sortie；失败 Cancel 不退款已发生的出动，也不修改更新 token 的槽位。玩家目标意图只在 Confirm 后生效，NPC 保持原来的母舰周边行为。
- gun / blast / doll 使用同一路径。doll 在各端激活时依据同一个注册 child ID 选相同资源，缺 DLC、native 创建失败或 owner 附加失败触发全员回滚。回收请求也经 AFDQ 认证到 host；实际回收完成、死亡或损失后由 Session 全员 Destroy。

## 网络状态和失败处理

- AFSB 以 shared hostPeer / epoch / authenticated PUID 传送潜舰四系统 wear、down mask、剩余维修 / 装填时间、导弹 / Bay 冷却及两座炮塔瞄准。guest 只更新当前已注册、已激活的 remote hull；包本身不创建、激活或启动技能。
- 编码显式写字段，完整包验证后才修改对象；使用 snapshot sequence 与 per-hull revision。未知或尚未激活的 hull、暂时 apply 失败不消耗该 hull 的 revision，下一次快照可以正常应用。上下文丢失退役当前 epoch，不能用旧包重新绑定旧世界。
- Session 的显式 PreparedKind::dynamicDrone 只接受 catalog 16、一个 role 0 child、37 个合法资源编码、非零 parent key、canonical requester 以及全零待派生 child ID。旧 missionCrew 默认门禁保留；新增全员 detail bit，在 host 准备 / 提交 / 激活和 guest 准备 / 提交时复核。动态 child 使用独立 channel，同一 parent + slot 的 pending 互斥；其它 slot / parent 和普通地图支援可以并行。
- AFDQ 使用 host 认证、每 peer nonce 和相同 epoch。拒绝的 handler 请求仍消耗 nonce，失败发送不重用 nonce。request 协议不自建世界实体，也不重试或退回本地生成。
- ECTL、AFSB、AFDQ、PVP、Command、Q mark 共用 support_net 的唯一 poll。SupportNetTick 的 game-thread 重入 guard 阻止 request handler 同步 SubmitPreparedSupportPlan 时再次 poll / 推进 Session。
- 注册、owner attach、parent bind、真实 pilot 登乘、child Activate / Confirm 的任何失败都交回 Session failure / Destroy。Prepare 之前已有 token 但还没有 Deployment / child 时，Destroy 仍先执行 metadata-only CancelCarrierSupportTransaction，按 exact token 清母舰 pending slot，无须读取可能死亡的 parent。之后先清实体 owner，再走实际 hull / drone 删除；潜舰实际 crew 先删除。保留已被真实玩家登乘的对象与机组的既有回滚约定仍适用。
- 场景 reset 释放弱记录；transport / epoch 丢失使状态协议与 request broker 失效。remote hull 不自行飞行、开火、维修或补充无人机。

## 聚焦验证

- MSVC /std:c++17 /W4 /WX /utf-8 /permissive-：support_dispatch.cpp、support_source.cpp、support_net.cpp、airstrike.cpp 编译通过；subcarrier / carrierlaser / jet 的单元编译由对应实现 fixture 核对。
- submarine_support_fixture：249 检查；carrier_laser_team_fixture：175 检查，覆盖 native 生命周期、矩阵对齐、owner 与远端控制限制、不同母舰阵营关系。
- sub_support_dispatch_test：32 检查，执行 production dispatcher 的 hull + real pilot 准备 / 全员激活 / Bay owner 更新，以及注册、owner、bind 和登乘失败回滚。
- carrier_support_dispatch_test：59 检查，执行 production dispatcher 和认证 host handler，覆盖三类 child、真实驾驶员 / 完整 ID、host 变换、guest 无本地副本、ACK 后确认、回滚和回收。
- support_source_test：17 检查，执行 native 枚举入口的服务 shim，覆盖全 ID / 唯一性 / 弱身份 / 地址复用 / registry 变化。
- support_protocol_test：4488 检查，执行真实 Session 三端动态 child 的 admission、完整 ACK 屏障、旧端拒绝、并行通道及 prepare 前后 / native 失败回滚。
- carrier_drone_transaction_test：386 检查，包含创建前 token 取消、再出动和 old parent / token 地址复用隔离。
- sub_support_net_test：905 检查；carrier_drone_net_test：350 检查，执行真实 codec 和双端协议状态机。
- support_net_runtime_test：110 检查，执行 production bridge 的唯一 poll、认证 sender、生命周期失效与同步 nested enqueue。
- support_dispatch 276、support_infantry_route 20、support_vehicle_route 15、call_pick 1554 的聚焦回归已通过。jet / owner 继承的其余 fixture 见 feedback-2026-10-19-support-owner-inheritance.md。

以上是源码、实际生产适配和离线服务模拟证据。没有启动或操作游戏，没有写游戏目录；实际双机的 native 登乘复制、母舰物理与音效 / 视觉行为仍需实机验收。
