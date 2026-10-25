# 对战：带名字的多阵营、角色、房间锁定与房间界面（2026-10-25）

用户原话（2026-10-25）要点：对战地图默认在房间里就能选阵营并显示；4 人以上不要手动翻页，面板往下自动叠加，不必照原版；改原版房间 UI；阵营不只 1 / 2，要带名字（怪物、星岛者、外来者、EDF1 / EDF2……），星岛者和外来者能混战；有指挥官职业（M 视角）和选具体怪物出生；原生只有 7 个队伍号就改队伍；房间里「加锁」解决属性到达时间不一；不兼容旧版本，旧版本直接不让进。

底层依据见 [team-manager-re.md](team-manager-re.md)；原有对战协议见 [plan-2026-10-23-pvp-modes.md](plan-2026-10-23-pvp-modes.md)。**全部只做了静态逆向与离线测试，未实机、未两机验收。**

## 1. 阵营 ≠ 队伍号（`src/pvp_match.h`）

- **阵营**带名字、颜色、允许的角色，每个阵营固定占一个原生队伍号、互不共用：

| 阵营 | 队伍号 | 角色 |
|---|---|---|
| EDF 1 | 2（原版友军 NPC 也在这里） | 士兵、指挥官 |
| EDF 2 / 3 / 4 | 7 / 8 / 9 | 士兵、指挥官 |
| 怪物（所有代的生物） | 1（任务原有怪物也在这里） | 操控单位、指挥官 |
| 星导者（5、6 代外星人与机器） | 10 | 操控单位、指挥官 |
| 外来者（4.1 代外星机器） | 11 | 操控单位、指挥官 |

- 用户写的「星岛者」按游戏与本仓库目录里的译名写作「星导者」（Primer）。
- **关系**：预设（全员混战、人类对外星、EDF 内战、外星内战）或房主自定义表（每两个阵营友 / 敌 / 中立，对称）。`TeamOverlay` 把阵营关系换成原生 16×16 关系表：队伍 0 跟 2 走，3..6 保持原版。

## 2. 原生扩队伍（`src/team_native*`）

- 启动时（TeamManager 建好之前）把构造函数里 13 处 `7` 和全队枚举 `0x5E0C80` 的 `0x188` 改成 16 行：行和关系向量都由游戏自己的容器建、自己的析构释放。管理器已存在则整体拒绝（日志 `TEAMS refused`）。
- 5 个原版重置调用点（`0x5E1E90` 默认值、`0x5E1F90` 单位阵）改走包装：原版重置后，对局开着就在管理器锁内重写关系。
- 插件里写死 `<7` / `<=2` 的地方（4.1 母舰、敌体操控雷达、受控体、支援归属、支援协议）改成按 `kTeamCapacity` / `PvpTeam` 判断。审查补漏（2026-10-26）：受控体的 `ReadAppliedTeam`（`src/pvp_controlled.cpp`，运输舰子单位、4.1 母舰部件读它）原来仍只认队 1 / 2，敌体操控的 PvP 提供者（`enemy_control_pvp_boss` / `_native` / `_berserker` / `_transport`）的队伍校验仍是 `<7`：EDF 2..4、星导者、外来者的成员一接管就 Fault，子单位落回原版队。现在前者按 `PvpTeam`，后者按 `team_native::TeamRow` / `TeamOrClosing`（`src/team_native_logic.h`），`pvp_controlled_fixture`、`enemy_control_pvp_boss_core`、`enemy_control_pvp_transport` 有队 ≥7 的用例。
- 全队枚举 `0x5E0C80` 的调用方（`command_identity` / `mission_participants` / `pvp_native` / `support_source`）原来只检查 7 行可读；扩容后它走 16 行。现在由 `src/team_walk.h` 在调用前核对：`0x5E0D28` 的界是整行、等于管理器 `+0x48` 的行数、行数组可读那么长。
- 原生按队伍号索引的其他数组：任务对象 `+0x280` 的每队倍率表只在生成时读描述符或 `+0x318` 的原版队伍号，插件的单位一律先按原版敌队生成、再改队，不越界。
- 离线：`team_native_test`（补丁位、默认关系、合法性），`team_native_sigs`（逐字节对真实 EDF.dll，含构造函数里没有遗漏的 7）。

## 3. 协议 v3（`src/pvp_protocol.h`）

- 每个成员：阵营、角色、单位，队伍号由阵营推出（线上带着做一致性校验）；状态带关系表（64 字节）。最大报文 656 字节。
- 成员改自己的选择（`RequestPick`），房主改关系（`RequestRelations`）或一次性下发整局（`RequestMatch`：房间锁定表）。仍走 Prepare / Apply / Final 全员确认；旧 v2 报文直接拒绝。

## 4. 房间（`src/room_pvp*`，`src/room_ui.cpp`）

- **页面**：房间菜单（配装按钮）第一项「对战：阵营 / 角色」进入对战页：房主开关对战、切换关系预设；每人选阵营、角色、单位（单位按阵营分页列出）；下面列出全员的选择与确认状态。用的是游戏自己的对话框。
- **属性**（EDF6Coop 成员属性，均带协议版本）：`EDF6X_PVPSEL` 每人的选择；`EDF6X_PVPSET` 房主的设置；`EDF6X_PVPLOCK` 房主的锁定表（序号、设置、全员选择）；`EDF6X_PVPACK` 成员对锁定表的回执（序号 + 文本哈希）。
- **加锁**（逆向见 [room-start-gate-re.md](room-start-gate-re.md)）：原版开打是全员就绪握手，房主统计到全员就绪后才写开始名单 `l1 / l2`，客人只认这份名单。插件把这一步唯一的「我是不是房主」调用 `0x73E3E9` 换成「是房主且全员确认了锁定表」：对战开着时，房主先发布锁定表、等全员回执；有人改选择或进出就换下一个序号重来。等待期间握手原样停着，房主再按就绪就是原版取消，客人什么都收不到。放行那一帧冻结锁定表。
- **进任务**：房主的协议就绪后用锁定表发一次 `RequestMatch`（每个任务世界一次，重试也会重发）；各端拿到的是房主的表，不是各自读到的属性。
- **版本门禁**（`src/mod_room.h`）：房间协议号 2，`SEARCH_TYPE` 前缀 `0x41460200`、`AF_PROFILE=2`。旧版本找不到也进不了新房间，新版本拒绝旧房间。EDF6Coop 改为从 `EDF6AF_RoomPrefix` / `EDF6AF_RoomProfile` 读取（Coop 仓库同步改动）。
- **EDF6Coop 2.7.0**（Coop#42）：房间协议 2 要 EDF6Coop 2.7.0 及以上。2.6.x 要求 `AF_PROFILE==1`，REJOIN 与虚拟房间会被拒，`lobbystate.cpp` 也解不开 `0x412602xx`。`mod_room.cpp` `CoopVersionProblem` 读 `EDF6CoopVersion`：不够就拒绝建房 / 进房并弹窗写明当前版本与要升级到的版本；EDF6Coop 自己的路线问 `EDF6AF_RoomIsolationReady` 时答「不可以」并每分钟最多弹一次同样的提示。

## 5. 角色

- **士兵**：原样。
- **指挥官**：各端都给他的人物打原生 NoTarget 位（`+0x380 & 0x80`，敌方遍历 `0x5E0FBF` 跳过）且不加锁定点；本机 M 地图自动打开，地图键 / Esc 不关（统计键照常）。对局结束恢复原值。
- **操控单位**（`src/debug_spawn.cpp`，目录 `src/pvp_units.*`）：成员自己的机器在对局确认后，在玩家前方 12 m 请求自己的单位；走测试场联机生成同一条可靠事务（目录号 1021）：房主核对请求者已确认的选择就是这个单位，各端加载、按请求者阵营的队伍号创建、用房主派发的网络 ID 登记，全员登记后启用；请求者本机随后经敌体操控申请接管（每 2 秒重试到拿到为止）。单位死亡 8 秒后重新请求。开局时每台机器预加载锁定表里所有人的单位。

### 5.1 单位目录（`src/pvp_units.h` / `src/pvp_units.inc`）

- **谁决定能不能玩**：调试召唤目录（`debugspawn::kEntries`）里 `Category::enemy` / `enemy41` 的行，其最终原生类能被敌体操控接管才算（`docs/enemy-control-runtime.md`）：34 个原生 profile（`enemy_control_native_profiles.inc`），加 4.1 自有控制器 UfoRobo / UfoMother301 / UfoMother401。类的来源：EDF5/EDF6 行读 Root.cpk 里原 OBJECT SGO 的 `xgs_scene_object_class`；EDF4.1 行读仓库内 `edf41port/objects/OBJECT` 的 SGO（Hector 实为 EDF6_Berserker_Large，两种 UFO 为 UfoSmall507/515，运输船为 UfoCarrier508，护盾运载者 AlienTrailer，迪洛伊 Deiroi，蝌蚪 DragonSmall）。
- **阵营**：按类归属。`monster`＝任何一代的生物；`primer`＝5、6 代异星人及其机器；`foreigner`＝4.1 的外星机器。`edf` 阵营没有单位。共 180 行：怪物 70、星导者 75、外来者 35。
- **排除**（不是可操控的独立角色）：建筑与结构、结构型特殊根（4.1 四足步行要塞、EDF5/EDF6 母舰、耶梦加得、圆环）、TRANSPORTER401_FORMATION、Root.cpk 里没有的 EDF5 DLC 要塞。
- **再生成**：`python tools/gen_pvp_units.py`（要游戏目录，只读）；`--check` 在 CI 里跑。目录（`spawn_catalog.inc`）变了就要重跑。

## 6. 房间成员列表（EDF6Coop 仓库）

- 原版只有 4 块成员面板的根源是 `0x8F8020` 栈上一张 4 项的模板表；Coop 改两条指令让游戏自己建出 N 块面板，再按人数重新排版（行高缩小、再多分列），删掉翻页；每行用 `EDF6AF_PvpMemberLabel` 显示阵营名与颜色。见 Coop 仓库对应提交。

## 7. 设计缺口（本次不改）

- **NPC 友方判定仍按原版 0 / 2 / 5**：`src/npcai.cpp` 的 `Think`（`team!=0 && team!=kTeamFriend` 就不接管 AI）、`PreThink`（班长继承）、`FriendlyVehicle`（上车）、班组 / 载具命令的 `notFriendly` 判定（`NpcSquadCommandForRequester` 一带），以及 `src/npcpost.cpp:147` `Available`（只收 0 / 2 / 5 的车）。EDF 1 的 NPC 本来就在队 2，所以只有 EDF 1 的玩家能招募、指挥 NPC 和给 NPC 派车；EDF 2 / 3 / 4（队 7 / 8 / 9）的玩家（包括选了指挥官的）在 M 地图下命令会得到「不是友军」，也没有自己阵营的 NPC。要补得先定「阵营的 NPC 从哪来」（生成时改队？锁定表里给每个 EDF 阵营分配 NPC？），再把这些判定改成按关系表的友方（`NativeTeamRelation`），不是简单把 7..9 加进白名单。
- **EDF6Coop 的 worldrng 只同步队 1**（推测，未在本仓库核实，Coop 侧代码也没对照）：若属实，队 7..11 的单位在客机上的随机数驱动行为（生成、AI 抽签）可能与房主不同步。需在 Coop 仓库确认后再定是否扩到全部阵营队。

## 8. 还没验证的

- 全部未实机：16 行管理器在真实游戏里的表现、重置后重写、敌对锁定 / 伤害 / 雷达颜色、指挥官 NoTarget 对 AI 的实际效果、单位生成与接管、开打拦截的手感、Coop 面板排版。
- 联机：客机关系表是否各自由 `0x5E1E90` 生成（推断是，各端都写同一张表）；单位死亡后尸体的清理与回合结束的删除同步（原生大概没有删除复制）。
