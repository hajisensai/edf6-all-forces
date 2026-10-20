# 2026-10-19 反馈：M 地图分组 / 指挥往 RTS 进化

## 用户原话

> 合并分组经常无效，分开也是，感觉你这个分组是不是和rts不太一样。操作手感看看能不能再往rts进化进化。砍掉点没用的东西也行，或者加点有用的

（PR #212 `docs/feedback-2026-10-19.md` 第 13 条。）

## 根因：「分组 P / 合并 L」改的是原版跟随树，玩家看不到效果

旧实现（`src/npcai.cpp` `SplitSquad` / `MergeSquads`，`src/mapcmd.cpp` `Teams`）不是 RTS 的「编队」，而是改原版小队的
`+0x548` 队长 / `+0x550` 跟随链表。原生 `SetFollow 0x54EC50` 本身没有上限、不会失败（静态反汇编：`Unfollow 0x54DD40`
后直接写 `+0x548` 并挂进链表），「无效」来自插件这一层的语义：

1. **跟着玩家的小队拆 / 并没有任何可见变化**：`npcai.cpp` `MarchRoster` 把「所有已招募、没有命令」的小队的队员排成
   **一个**以玩家为锚点的行军队形（`FormationMove`），阵形为默认时则都走原版跟随玩家。把一支跟随小队拆成两半、或把两支
   并成一支，队员仍站在同一个队形里、同样跟着玩家——玩家看来就是「按了没反应」。
2. **合并的目标不是玩家选的那支**：`Teams` 按 `g.list` 顺序取第一个选中的小队当目标，而 `List` 按**对象地址**排序
   （`std::less<const void*>`）。目标若是自由小队，被并入的已招募小队的根变成自由队长，`SeeSquad` 判它的领导归属变了、
   把它的命令清掉（`led by ... its order dropped`），小队不再跟玩家；目标若在冷却中（离队后）或是只能招募的脚本小队，
   `Commandable` 拒绝它，于是**这次所有合并全部失败**，提示「0 个小队」。
3. **拆出的半队不继承命令**：新组长的 `Squad` 条目是下一帧 `SeeSquad` 新建的，`cmd` 为空：守点的小队拆开后一半留守、
   另一半跑回玩家身边或原地发呆；选择里也只剩原来那半。
4. **联机完全不可用**：两者都 `InSession()` 直接拒绝（按钮隐藏，按键只提示「仅限离线」）。
5. 合计超过 16 人时 `MergeSquads` 静默拒绝。

另一个让「分组」显得不可靠的点：地图的选择按小队**队长的身份**记，队长一死（`Succeed` 选出新队长）小队就从选择里掉了。

## 修复：分组改成 RTS 控制组，不再改原版小队

| | 内容 | 为什么 |
|---|---|---|
| 删 | 命令卡「分组」「合并」按钮、P / L 键、`SplitSquad` / `MergeSquads` / `Teams`、相关文字 | 根因 1~5；RTS 的编队是「选择的快捷方式」，不需要改小队 |
| 删 | 数字键 1~9 选小队面板第 N 行 | 数字键给控制组；面板行照样点击选择，行首改显示所在编队号 `[13]` |
| 删 | G 键（与 Z 完全相同的攻击移动别名） | 重复 |
| 改 | H 从「集火指针下的敌人」改为**原地坚守** | 集火已由右键敌人给出（一步到位），H 与它重复；RTS 惯例 H = Hold |
| 加 | **控制组**：Ctrl+1~9 存、Ctrl+Shift+N 追加、N 选、Shift+N 加选、连按两下 N 镜头跳到这队中间；手柄十字键 = 第 1~4 队（点选、连点跳转、按住 0.6 秒存） | RTS 标准；任何单位都能混编（小队、直升机、战机、坦克） |
| 加 | 控制组 / 当前选择跟随队长交接（`npcai.cpp` `Succeed` 记 `handovers`，`NpcSquadSuccessor`） | 队长阵亡后不丢编队 |
| 加 | **左键直接拖框**（不必 Ctrl）；中键拖动平移；没有可指挥单位或已点了待给点的按钮时左键拖动仍是平移 | RTS 标准；之前框选必须按 Ctrl |
| 加 | **双击 / Ctrl+单击**选屏幕上所有同类单位；**Ctrl+A** 全选（按住 Ctrl 时 WASD 不平移，方向键照常） | RTS 标准 |
| 加 | **右键按上下文**：右键你自己 = 跟随 | RTS 标准（右键敌人集火、右键地面移动原已有） |
| 加 | **Shift 排队路点**：Shift+右键 / Shift+Z 把移动 / 攻击移动排在后面（每单位 ≤ 8 个），地图上画出路线；地图关着也推进（`map.cpp` `Frame` 每帧调 `MapCommandQueueTick`）；任何不带 Shift 的命令清空 | RTS 标准 |
| 加 | **原地坚守** H / 命令卡按钮 / 手柄长按 Y（`Order::hold`，命令协议 v6）：每个单位守住自己脚下的位置；联机由权威端读取单位位置 | RTS 标准；之前要原地防守只能逐个对脚下点守点 |
| 留 | 阵形菜单（T / 按钮）、自由交战 J、上下车、招募 / 离队、拾取、回复箱 | 各自没有重复 |

A（攻击移动）和 S（停止）是地图镜头的 WASD 平移键，不能抢：攻击移动仍是 **Z**，停止仍是 **X**。

## 离线验证

- `tools/map_cmd_check.cpp`：控制组按键语义、双击判定、手柄点按 / 长按（含被侧面板吞掉的按键松开时不算点按）、同类判定、
  排队的入队 / 推进 / 被别的命令打断后清空 / 满 8 个、HOLD 的线上值与车辆映射、右键自己 = 跟随。
- `tools/map_command_runtime_check.cpp` `RtsGroups`：跑真实 `MapCommandFrame`——不按 Ctrl 的左键拖框（期间不平移）、
  Ctrl+1 / 1 / 连按 1 居中、空编队提示、Ctrl+Shift+N / Shift+N、双击同类、队长交接后编队转到接任小队、Shift 右键排两点后
  地图关着时到点推进下一点且一帧只推进一次、X 清空排队、H 原地坚守、右键自己跟随、Ctrl+A。
- `tools/npc_core_check.cpp`：`Succeed` 的交接记录、`Order::hold` 在 `npcai.cpp` 变成守住队长当前位置。
- `tests/command_protocol_test.cpp`：协议 v6、HOLD 往返、HOLD 不允许带队形槽。

## 未实机验证

- 左键拖框与平移的分界（有可指挥单位时左键拖动不再平移）是否顺手；中键拖动平移的方向。
- 排队到点半径：小队 `NpcGuardRadius`+10 米、直升机 `HeliGuardRadius`+40 米、战机 600 米、地面载具 30 米；坦克 / 爬行者的
  守点若被模块挪动超过 5 米（`kSamePoint`），排队会被当作「命令被改」而清空（安全退化：单位停在当前命令）。
- 直升机 / 战机原地坚守 = 在当前位置下方地面守点盘旋 / 巡逻。
- 手柄长按 Y 原地坚守后，Y 点按改为在**松开**时下令（之前按下即下令）。

## 2026-10-20 审查修复（PR #220 / #224 / #221 的审查）

用户要求「所有功能都要支持联机」。审查发现的问题与修法：

| # | 问题（审查时的代码） | 修法 |
|---|---|---|
| B1 | 联机 Shift 排队变成立即替换：远端单位打包直接 `SubmitMapCommand`，排队只对本机单位，`QueueTick` 遇到非权威单位清空 | 命令协议 v7：请求带 `kQueued`；排队放到**单位的权威端**（`MapCommandAuthority`，本机 `Dispatch` 与网络 `Execute` 共用这一入口）按单位保存。与 v6 对端协商：hello 一律按 v6 发并带最高版本，双方按较低版本；v6 房主收不到排队指令（本机拒绝并提示），不会静默变成立即命令 |
| B2 | 客人编队长阵亡丢小队：`Succeed` 只在房主跑，客人的 `NpcSquadSuccessor` 查不到，`KeepGroup` 删组 | 编队记全体队员身份（`mapcmd_logic.h UnitRef`），找不到队长时取任一活着队员的当前队长（`npcai.cpp NpcSquadTopOf`，读原版同步的跟随树，客人也有）；选择同样 |
| B3 | `QueueTick` 挂在玩家地图帧上（玩家死 / 观战停）；`Replace` 对所有普通命令登记，每帧 `List()`；单位暂离列表或队长阵亡立即丢 | 场景 tick 推进（每个士兵的 pre-update + 载具输入 `SharedFrameTick`，每帧一次）；不按 Shift 不登记；按 UnitRef 查找，暂离列表保留，全部阵亡才丢 |
| B4 | 换任务不清编队与队列 | 核实：`MissionStart → ResetMap → ResetMapCommands` 原本就整份重置地图状态（编队随之清空）；排队移到权威端后由 `ResetMapCommands` 显式清空，离线检查覆盖 |
| B5 | 左键点敌人 `SetEnemyCandidate` 强制展开怪物面板，下面的面板跳动 | 点选只高亮、不改折叠；展开 / 收起只由点击标题（或 L3）触发。**「停靠列每块固定段」没有做**：已有的布局契约（`enemy_hud_layout`：折叠时只占标题行；`pvp_hud_layout`；`hud_layout` 要求「摆放」列表至少约 25 个可点项）与固定段冲突，按固定段排版后「摆放」只剩 1 个可点项。根因是自动展开，去掉后面板只在用户自己点击时移动 |
| B6 | 操控怪物的移动语义改了（摇杆前后、右向），协议仍是 v3 | `enemy_control_protocol.h kVersion=4`：v3 的 hello 在 v4 房主处解不开、永远不 capable，房主拒绝整个房间的操控（离线检查覆盖混装房间） |
| B7 | 按需加载只记一个待生成项，新点选静默顶掉；未做 `WaitPreload` 的渲染线程同步 | 待生成项改成队列（16 个，满了拒绝并提示）；完成后照 `WaitPreload`（`0x1BC200`）的顺序：完成查询 → `0x111CC20(&0x2136FF0)`（向渲染线程命令队列压一个栅栏并等它，期间泵窗口消息；静态阅读，用途为推测）→ 再查询，签名不符就跳过这一步 |

联机缺口：

- **阵形 T / 阵形菜单**：作为 v7 的 `Order::formation`（参数在 `Command::param`：守点防御阵形轮换 / 设定，行军阵形设定）走同一通道。行军阵形按玩家分别排（`npcai.cpp` `PeerMarch` / `MarchOf`，`MarchRoster(ms, march, player)`）：房主替每名客人保存一份行军状态（阵形、朝向、跃进分组），锚点是那名客人的位置，朝向取他身体的前向（客人的镜头在他自己的机器上，房主读不到）；客人的 T 发出「行军设为下一个」，客人本机同步记下这个阵形，下一次 T 从它往后轮换。
- **客人地图画排队路线**：v7 新消息 `ways`（`command_protocol.h`）：房主在场景 tick 里把本机各单位的排队（目标点之后的路点）与上次发出的比较，有变化才发给 v7 成员，排队没了发清除；成员 hello 时补发全部；按修订号新者为准，换任务清空；v6 成员从不收到。客人地图对非本机权威的单位画房主发来的路点。
- **摆放载具 / 士兵 / 无人机、按需加载**：资源号高字节区分种类（敌人 0xE1、载具 0xE2、士兵 0xE3、无人机 0xE4，`debug_spawn_net.h`），`SpawnRangeEnemy` 按种类生成（`SpawnVehicle` / `ApplySupportSoldierResource` / `PrepareSupportDrone`）后 `RegisterSupportObject`，激活照旧（敌人 `0x548D50`、士兵放开、无人机起飞）。按需加载：房主规划时返回 pending 直到本机加载完；各成员 PREPARE 时排入加载，加载完才回 READY（`support_net.h Hooks::prepare`，`support_protocol.cpp` 的 READY 延后，受事务原有 20 秒期限约束）。

统一：`UnitRef`（`mapcmd_logic.h`）；`Dispatch(units, Command, {queue})`（本机权威 → `MapCommandAuthority`，其余一次请求；Issue、排队推进、`MapCommandGuardAt`、阵形都走它）；`NetSpawn`（测试场的物体行联机走同一套 preload → prepare → 全员创建 → activate；支援呼叫行仍走支援呼叫自己的规划器，同一会话）。

离线验证：`tests/command_protocol_test.cpp`（v7 字段、v6 拒收、各版本组合协商、`ways` 的编解码 / 只发 v7 成员 / 不变不重发 / hello 补发 / 清除 / 旧修订不覆盖 / 换任务清空）、`tests/command_native_dispatch_test.cpp`（客人对自己小队设行军阵形：房主为他单独保存并把他的小队排进他的行军，房主自己的行军不变）、`tools/map_cmd_check.cpp`（阵形参数、`Replace` 不登记、`UnitRef` 回选）、`tools/map_command_runtime_check.cpp`（网络排队经权威入口保存并由场景 tick 推进、暂离列表保留、换任务清空、客人视角经队员找回编队、阵形联机请求）、`tests/debug_spawn_net_test.cpp`（各种类的计划与三机事务、PREPARE 延后到加载完、永不加载则超时且什么都不生成）、`tests/enemy_control_protocol_test.cpp`（v3 成员在房间里时拒绝操控）。

未实机：以上全部；特别是联机摆放的载具 / 士兵 / 无人机在客人那边的表现、`0x111CC20` 在任务中途调用是否会卡顿或死锁、客人经跟随树找回编队的时机。
