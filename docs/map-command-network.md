# Authoritative NPC map commands

Map commands use the existing authenticated Coop extension transport and the
single `support_net.cpp` poll. There is no second consumer, socket, player slot
fabrication, or client-side write to host NPC state. Command messages have a
separate `NCMD` magic, version 3, and explicit 828-byte little-endian encoding.
Version 3 (2026-10-09) adds the RTS orders `move` and `attackMove` (appended to
`mapcmd::Order`; earlier values unchanged) and the `riding` refusal (appended to
`NpcCommandReason`); a version-2 peer rejects version-3 packets unexecuted.
They fit the existing 1024-byte extension ABI; Coop changes are not required.

The command capability is negotiated with the host inside the already accepted
support mission epoch. A host without the new command executor never advertises
the capability; an older host simply leaves the new command feature unavailable.
The support v2 protocol remains compatible. Future incompatible command changes
must bump the command version, independently of the support spawn wire format.

Each request carries the current player's canonical native object ID, one to
sixteen distinct squad IDs, an order and finite world point, and an explicit
enemy native ID for focus. Counts above sixteen are rejected, never truncated.
No resource paths, object addresses, player indexes or caller-selected PUID are
accepted on the wire. The existing native all-team walker resolves the live
objects in one pass, including their current weak identities. Requester must be
a genuine current-mission player. Its native User PUID must equal the transport's
authenticated sender and appear in the sealed world participant set.

Point-order batches (guard, move, attack-move) retain each unit's global formation slot and the complete eligible
selection count, including locally executed units. The host validates distinct
slots within the map's 96-unit capacity, uses the shared 30 m spacing and formation
function, then verifies a nearby standable floor before dispatch. Remote squads
therefore do not collapse onto one point or get a second client-side offset.

Only the host invokes the configured `NpcSquadCommandForRequester` executor. It
receives the actual requester ObjRef, not `PlayerHuman()` on the host. That
executor revalidates authority, script control, faction and squad ownership at
the mutation point. Following/recruiting another player's squad is refused;
legal free squads remain usable. Focus targets belong to the command, not the
host's global mark. Non-soldier remote units return unsupported instead of
pretending to run a vehicle command.

Request numbers are monotonic per client and mission. Host caches the latest
request and result per authenticated peer/PUID. Identical retries return the
cached result; a reused number with different content, an older number, another
sender, or a previous mission epoch cannot execute the order again. The cache is
bounded at 1024 peers. A 200 ms sender limit prevents command spam. Requests are
retried once a second while awaiting the result, and reach an explicit 10-second
acknowledgement deadline. A timeout means the execution result was not received,
not proof that the order never ran. Late results cannot reopen that deadline.

`SubmitMapCommand` returning an ID means queued, not executed. The map polls
`ReadMapCommandNetworkResult` and correlates that ID before displaying the
per-unit native reason and affected count. Partial acceptance remains partial.
For boarding, accepted means real walking/seat assignments were made, not that
every soldier is already seated. Session suspension interrupts pending commands
without replaying them in the next world.

## Version 7 (2026-10-20): the Shift queue and the formation order

Two additions, both in the two words a version-6 message keeps zero, so the layout and the 828-byte size are 6's:
the word after `kind` carries `Request::flags` (`kQueued`: a point after the ones the unit is on its way to), the word
after the point carries `Command::param` (the formation order's: a guarding squad's defence cycled or set, the
requester's march set; `mapcmd_logic.h FormationParam`). `mapcmd::Order::formation` is appended (14).

The versions are negotiated, not assumed. Every hello goes out at version 6 with the sender's highest version in its
`formationTotal` (a field version 6 never reads in a hello); the host answers each peer at the lower of the two and
the client speaks that version from then on (`Session::HostVersion`). So a version-6 host serves a version-7 client
every version-6 order; a queued or formation order to it is refused before anything is sent, with the reason on the
map (`SubmitMapCommand`), never sent as an immediate order instead. A version-6 client of a version-7 host is answered
at 6; a version-7 request claiming to come from it is not executed. `support_net.cpp NoteForeignVersion` counts both
versions as this build's (`command_net::Compatible`), so the room's version notice does not flag them.

The queue lives where the unit's AI runs. `command_net.cpp Execute` hands every unit to `mapcmd.cpp MapCommandAuthority`,
the same entry the host's own map uses for its own units: a queued point is kept after the points the unit is on its
way to (accepted, nothing given yet), a plain order drops the queue and is given by the executor. The scene's tick
(`MapCommandQueueTick`, from every soldier's pre-update and every vehicle's input, once a frame) advances the queues
whatever map is open and whether the requester lives; a unit out of the list a while keeps its queue, a squad whose
top died goes on under the top of any living member (`mapcmd_logic.h UnitRef`). A queued point for a unit the host
does not list is refused as stale.

Version 7 also has the host's queues travel the other way, for the clients' maps (`Kind::ways`): one unit's points
after the one it is on its way to, sent to every version-7 peer when they change (`Session::PublishWays`, from
`MapCommandQueueTick` through `SyncMapCommandWays`), a clearing when its queue is gone, every queue again to a peer
that says hello. Newest revision wins; the epoch forgets them; a version-6 peer is never sent one.

The march formation is per player on the squads' authority (`npcai.cpp PeerMarch`): a guest's formation order for its
own recruited squads sets the march the host keeps round that guest (anchored on the guest, facing its body), the host
player's own march unchanged.

## Integration and checks

`InstallNpcAi` registers the executor once through `ConfigureNpcCommandNetwork`,
before the map is opened. `support_net` supplies the current authenticated
context, routes packets, and resets command state with the mission. The UI passes
its actual viewport player, selection ObjRefs and explicit focus target.

`command_protocol_test` covers the version-7 words and their refusals at version 6, the negotiation of every pair of
versions (a version-6 host never sent a queued or formation order, a version-6 client served at 6), encoding bounds, epoch/sender/capability checks,
at-most-once execution, changed-content replay, partial results, rate limits,
result loss/retry and deadlines. `command_identity_test` executes the production
resolver and ID reader against fixture objects and the real EDF.dll private
all-team walker with no DllMain. `support_net_runtime_test` verifies the shared
poll dispatch. Native command semantics are checked against the production NPC
executor separately and through the command dispatch regression.

No running game, game installation write or two-machine gameplay test is part of
these checks. Real movement, boarding animations and remote convergence remain
gameplay acceptance boundaries.

## 地图接线与实际执行

客户端地图通过独立只读原生友军遍历列出远端小队，不调用这些副本的 Think、Gather 或 SeeSquad，也不修改移动、射击、跟随关系。小队行传递绘制时的 ObjRef，重新排序、对象更换或失焦不会把旧点击应用给别的行。只有当前选择真正支持的命令才在 HUD 中启用。

本机拥有 AI 权威的单位走真实执行入口；远端小队通过一次最多 16 队的 RPC 请求执行。2026-10-20（命令协议 v7）：所有命令统一走 `Dispatch`（本机权威的单位进 `MapCommandAuthority`，远端的打包一次请求），Shift 排队带 `kQueued` 标志由权威端保存、场景每帧推进；阵形（T / 阵形菜单）作为 `Order::formation` 走同一通道；与 v6 对端协商为 v6，v6 房主收不到排队 / 阵形指令（本机先拒绝并提示）。混合选择先按同一排序计算全局 guard 阵形槽和总数，远端报文传槽号，不预先偏移坐标；本机及房主均使用相同间距并要求真实近地面命中。UI 区分排队与权威端逐项受理，显示过期、归属、脚本控制、无车辆、无兼容空座和超时原因，不把发送成功当作完成登车。

ForRequester 使用消息认证后的真实玩家，而非房主 PlayerHuman。原生跟随另一名玩家的队伍不可抢占；上下车逐成员核验 authority 与实际空席。队长职业不再代表所有队员，车辆选择按成员可用的实际座位入口距离；队员已在执行有效登车任务时不重复清空或延长任务。host 所有的 NPC 可通过原有 host 登车权限进入 guest 控制车辆的空副席，已占驾驶席保持不变。每队明确攻击目标使用独立弱引用，不覆盖房主的全局 Q 标记；换令、回收小队记录与任务重置释放引用。

验证包括生产 MapCommandFrame/Issue 的 UI 捕获、失焦与视口变更、混合阵形与结果关联；实际 npcai ForRequester→Board→原生 Ride 调用/公告入口，以及 guard→MoveTo、engage→目标取得/战斗移动。网络完整认证/去重/原生 ID 跨层测试另由 command_native_dispatch_test 执行。离线夹具不代替双机游戏验收。
