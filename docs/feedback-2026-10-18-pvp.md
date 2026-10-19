# 2026-10-18 对战模式反馈处理

用户要求：增加对战模式；游戏内设置阵营；玩家可被锁定，并获得敌人同样的雷达、锁定等待遇。

## 实现与原生依据

- 对战默认关闭。房主通过游戏内入口开启或关闭；各成员只能设置自己的阵营，使用已确认互敌的原版 team 1 / team 2，不修改原生关系矩阵。
- `pvp_native.cpp` 先完成原版全部七个队伍的对象枚举，再执行 `SetTeam(0x54EE70)`。不会在集合遍历中改变其成员，也不只写 `object+0x314`。真实任务 player index 必须由 EDF6Coop 的 `EDF6Coop_ResolveMissionPlayerPuid` 解析为已认证 PUID；房间 ordinal、数组位置、昵称都不能作为身份。
- 玩家、其现座驾、座驾乘员同时使用对应队伍，避免 VehicleBase 的原生乘员重算把座驾改回旧队伍；当前武器的发射队伍 `weapon+0x214` 同步到真实 owner，发射的子弹仍走原生伤害和敌我过滤。依据为 `docs/swarm-team-re.md` 的队伍、holder 与发射参数链。
- 原版敌我关系、TeamManager 登记供雷达和 AI 直接使用。静态复核当前 `EDF.dll`：雷达 `0x82B541..0x82B59D` 每帧从 HUD `+0x778/+0x780` 的 weak owner 经 RTTI 转为 `SoldierBase`，`0x82B89A` 与 `0x82BB80` 各自读取该玩家当前 `+0x314` 后调用敌对枚举；中间 RSI 作临时存储后由 `0x82BB09` 从本帧局部 `+0x170` 恢复，未缓存固定队伍。另发现友军行 `0x82BB42→0x82BB50` 写死队伍 2；新增仅该 callsite 的签名守卫 hook，在对战开启、HUD weak identity 正是已改队的玩家时将参数改成其真实队伍，避免 team 1 玩家把 team 2 同时画成友军。原版 team 4/5 等其他行保持原始调用。HUD hook 只读取原子公布的 mode 位与 HUD 自有 weak owner，并用原生 strong-count CAS 保留对象；不跨 UI/game 线程读取 Saved 或 activeState 缓存。锁定过滤 `0x689E80/+0x689E87` 同样直接读 owner/target 当前 `+0x314`，再跳原生敌对关系判断 `0x5E1540`。对玩家补原生 `AddLockTarget(0x547A50)` 锁定点，位置来自角色实际世界坐标，随每帧更新；死亡时禁用。不会改用地图或第一人称相机坐标。
- 对战对象临时设置 `0x380 & 0x2000`，保留队伍登记和敌对枚举，同时避免 team 1 玩家/座驾计入原版“剩余敌人数量”。原始该位、team 和 registered 状态均保存；退出对战、离车、场景变化时恢复。`0x2000` 的原生依据是 `docs/swarm-team-re.md` Q3 的 `GetTeamObjectCount → 0x5E13E0` 条件；士兵实际武器数组 `+0x1950/+0x1960` 与 `firstperson.cpp::Gather`、`docs/fp-soldier-re.md` 所使用的数组一致。只有本模块新增的锁定点会被禁用，原有锁定点不受影响；原版对象保留锁定点所有权并负责析构。玩家缓存另保留原生 control block 的 weak_count（`+0xC`），始终先查强引用计数再解引用对象；反复开关复用同一锁定点，离车的旧载具/机组释放缓存，场景结束释放全部 weak 引用。
- 两名敌对玩家已经同车时，更改会被拒绝；运行中若出现相同冲突或身份/对象失效，立即恢复原版阵营并通知房主停用对战，不按最后遍历到的乘员决定队伍。

## 支援归属闭环

- `Plan.requester[32]` 携带 canonical native player ID，`Plan.team` 为 0/1/2。不接受 payload 直接声称的 PUID：每端经严格 `ResolveCommandIdentities` 与 `ResolveMarkIdentities` 解析同一 live ObjRef/control/EOS handle，再用任务 slot 解析 Coop 的真实 PUID，并通过 `SupportCommandRequesterMatches` 验证当前封存世界。验证和原生附着边界再次解析。
- `PvpSupportTeam` 在稳定关闭时返回 0；稳定开启时只返回该已提交 owner 的 1/2；任何事务、恢复、未就绪或未知 owner 返回 -1，不能偷偷转换为 team 2。旧协议的全零 owner + team 0 只在稳定关闭时兼容；有归属元数据的非对战调用也必须验证 owner。
- 支援生成后保留独立 owner weak 缓存，覆盖步兵 NPC、空载载具以及其机组；不依赖玩家当时是否坐在载具里。原生 VehicleBase 每帧在 `0x6304C7` 以 team 5 起算、`0x630591` 调 SetTeam；只在全局帧里重设一次会被稍后更新的空载车覆盖。因此对该唯一重算 callsite 另设签名守卫 hook，仅使用持有 weak 的已归属 Saved.assignedTeam，调用原生 SetTeam 并同步真实武器发射 team；未追踪对象原样走原生。此后模式开启/关闭、owner 改队、玩家失联都从相同 native desired-set / SetTeam / weapon emitter-team 路径重算或恢复。模式关闭时生成的对象也记住 owner，以便稍后启用对战。
- 完整批次先检查对象和缓存容量，再附着。最多缓存 2048 个支援对象，不截断单次最多 16 单位的计划；容量或原生应用失败向支援事务返回 false，部署回滚。对象销毁/撤回时按完整 ObjRef 忘记归属、恢复原始 team 并释放 weak；地址重用不会继承前一个对象的 owner。
- 普通网络重置仅恢复阵营并清协议，保留仍在当前世界的 owner；真正 `ResetPvp` 场景释放才额外清 owner，避免临时断线后丢失支援归属。原生恢复未完成时继续保留备份，禁止把恢复待办当作成功。

## 联机契约

`EDF6X_PVP=1` 成员属性只公告支持能力，不决定战斗阵营。运行时使用 support_net 的唯一 extension poll，AFPV v1 消息按已认证 sender PUID 验证，不信任 payload 声称的身份。

协议先收齐全员版本与身份，再建立由房主发送的共享 epoch。成员只请求自己的 team；模式切换只由房主发起。每次更改依次经过 `Prepare → 全员 Prepared ACK → Apply → 全员 Applied ACK → Final`。界面收到 true 只表示请求已排队；只有最终确认才显示完成。未确认、未知成员、旧协议、旧 epoch/revision/token、超时、发送/原生应用失败都不能被当作成功。

失败恢复此前设置；回滚本身失败、Final 丢失或已提交后的原生不变量失效会触发全员停用。场景/运输上下文失效恢复原版阵营。不会使用各端可能相差数秒的 lobby attribute 到达时间独自修改队伍。原生恢复失败不会清掉原始 team / registered / weak 备份；协议进入恢复待办，后续游戏帧完成恢复前禁止建立新对战状态。

当前 EDF6Coop 2.6.0 的 extension 明确使用 `ReliableOrdered`（`extension_transport.cpp:145`）。`direct_net.cpp:933-935` 即使底层首次 send 拒绝也保留可靠重传；`1660-1690` 的最老未确认消息超过 stall timeout 会关闭链路。`extension_transport.cpp:70-108` 在链路缺失、过旧、拥塞或 pending 过限时重置 generation，收件箱溢出也触发 extension fault。AFPV 依赖“可靠保序送达，或 context / generation 失效”契约；后者立即恢复原版阵营。没有在原生生命周期之上另加延时重试来掩盖失效。

AFPV v1 当前最多 8 个 PUID 成员、每成员可包含同 PUID 的本地分屏玩家；分屏共用同一阵营。超出人数明确拒绝，未静默截断。单条最大 560 字节，低于 Coop 的 1024 字节上限。

## 接线与验证

- 插件初始化阶段调用 `InstallPvp`，在 game/UI 更新启动前安装两个默认 pass-through 的原生 callsite hook；开战中的 Prepare 只检查 profile/已安装状态，不懒写代码。
- `pvp.h`：`SetPvpEnabled`、`SetPvpLocalTeam`、`ReadPvpStatus`、`ReadPvpMembers` 供游戏内菜单使用。`pending` 与 `result` 为事务结果；`note` 提供具体原因。
- support_net：使用现有 `CommandNetworkContext` 调 `UpdatePvpNetwork`，唯一 poll 按 AFPV magic 分发 `ReceivePvpNetwork`；运输重置调用 `ResetPvpNetwork`。UI动作同时绑定显示时的共享 epoch、revision、真实本机 PUID，游戏线程消费时复核三者，旧菜单点击无法应用到新场景。
- 玩家 `MapHumanFrame` 与载具 tick 都在游戏线程调用 `PvpFrame`，内部以已验证的 `GameFrame` 去重并在网络/场景重置时清帧闩；无载具的步行任务仍会消费请求、更新锁点/武器和完成恢复。HQ / 房间 UI 线程只调用 `PvpRoomTick` 公告能力；任务场景释放前调用 `ResetPvp`。禁用插件也会恢复原生状态。
- `tests/pvp_protocol_check.cpp` 是不依赖游戏的多节点网络模拟，覆盖严格字节解码、8 人名单、身份/权限校验、全 ACK、发送/Prepare/Apply/回滚失败、重放、缺版本、超时、Final 丢失与场景重置。
- `tests/pvp_native_fixture.cpp` 调用生产原生缓存/写入/恢复代码，100 次开关只创建一个锁定点，并检查真实 team/武器恢复、weak 引用释放、地址复用前先检查 control block，以及独立支援 NPC/空载载具改队、关闭/网络重置后恢复、forget、原生空载 team 5 重算仍保持 owner1/2、雷达友军行映射。
- `tests/pvp_support_fixture.cpp` 直接测试生产归属桥，94 项检查覆盖 canonical/严格双解析/Coop 文本/封存世界、事务中拒绝、错误阵营、16 个对象逐项传递且无截断。
- `tests/pvp_native_audit.py` 对本机受支持 `EDF.dll` 的雷达 owner/当前 team、固定友军行、锁定关系和武器发射 team 链完成只读字节审计。
- 新增运行时源已通过 MSVC C++17 `/W4 /WX /utf-8 /permissive-` 编译。完整构建、CTest 由集成任务统一执行。

离线通过证明协议与编译路径，不能代替两机游戏验收。尚需实际确认：敌对玩家雷达与锁定显示、互相直击/爆炸伤害、死亡与复活、地图/载具/分屏、队伍改变后原版任务脚本以及联机快照的行为。本次未安装 DLL、未驱动用户游戏，也未声明这些实机项目已验收。
