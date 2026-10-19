# EDF4.1 母舰真实控制器接管

2026-10-19。实现位于 `src/edf41mother.cpp`、`src/edf41mother401.cpp`，公开 adapter 声明在 `src/enemy_control_legacy_mother.h`。本次未写游戏目录，未执行游戏或双机验收。

## 控制范围

- UfoMother301 本体：仍使用既有 Spin 的速度上限、0.001 平滑、世界矩阵和死亡运动。控制期间暂停本体原生 slot 7 的路线决策，方向来自所有副本共享的绝对 aim；slot 5、碰撞、部件挂接及生命期继续运行。0–2 菜单只描述已实现的旋转行为，不宣称缺失的灯光切换。
- 301 真实主炮、中型炮、非静态炮台面板及发进舱可分别接管；本体接管时保留整组武器命令、补充真实中型炮单次蓄力、束流开关和面板单次发射。主炮沿既有 1200 帧蓄力、61 帧扫射；中型炮沿既有 600 帧蓄力；束流和面板仍从原有 muzzle / Fire 弹丸路径产生。未制造爆炸代替开火。
- 301 的原脚本动作 12 / 17 只写原控制器没有读取的 boost 标志。保留脚本兼容接口，在控制菜单中明确标为 unavailable，不能把它们算作已实现技能。实际存在的单次武器入口独立列出。不存在或已毁的武器组也显示不可用。
- 401 的八种战斗部件：BigCannon、FinalCannon、Generator、FinalGenerator，以及 SmallLaser / Sniper / PlasmaCannon / RapidCannon。FinalCannon 明确列出灭绝炮与五连闪电；其他炮完整沿 Enter 的开门、蓄力、发射、关闭状态流转。附着炮台没有独立平移技能。
- 401 hull 原本就是无模型、无碰撞的 19×19 布局控制器，Panel01 / WeakPanel / Core 为装甲、弱点和生命期结构；这些不作为可驾驶角色，相关战斗部件仍可独立接管。

## 召唤与恢复

召唤路径、数量、间隔、等级只来自任务已经交给 AssignWave / SetGenerator 的真实配置；无外部路径输入。未配置的入口明确 unavailable，路径显式清空。手动请求只执行一次已配置波，既有常驻弱引用槽保持不变；归还控制后恢复原常驻生成器。接管时已经完成一波并在等待/关门的生成器不会擅自再生成一波。

接管暂停新的自主攻击决定；已经进入的蓄力、发射、关闭、声音与弹丸生命周期继续。需要持续授权的攻击输入按释放流程停火。Restore 幂等恢复控制前的启用/攻击标志和路线决策，不重置攻击状态、不恢复生命值；母舰死亡会关闭全部子件手动输入，后续归还不能覆盖死亡停火或重新召唤。401 父面板、核心或母舰死亡也立即使输入失败并归还视角。

## 网络契约

edf41net 仍负责原生对象/子件身份、伤害和生命期。301 本体也 Attach 到已有事件接收层。控制器只在 Decides 的副本消费按键并广播真正接纳的语义命令（301 kind 18、401 kind 19）；guest 不决定、不重发。命令只含固定语义、既有部件索引、种子和租约，不含指针、CAS 字符串或任意资源路径。

命令绑定当前 lease。guest 若尚在上一轮真实攻击，则在有界 inbox 等待原状态机回到合法入口，不按秒猜测；旧 lease 的命令丢弃，301 已毁武器的过期命令不能阻塞存活本体的其他命令。重试 busy 命令不能改写正在进行攻击的 RNG；无关旋转也不能给武器重新设种子。绝对 aim 在各副本通过同一 Control.Aim 计算，瞄准原点优先使用真实锁点。

## 验证

两个生产源文件独立通过 MSVC C++17 `/W4 /WX /utf-8 /permissive-` 编译。`tests/edf41mother_control_fixture.cpp` 直接 include 对应生产源，不复制状态机；引擎边界用可观测 stand-in：

- 无宏目标：301 95 项检查，覆盖真实主炮 Fire 参数、原生平滑运动、独立部件朝向、任务波召唤、未配置路径、归还/死亡、guest 忙态回放、旧 lease 拒绝及已毁武器队头清理。
- `MOTHER401_FIXTURE` 目标：401 78 项检查，覆盖主炮开门/蓄力/真实激光、闪电、任务生成器、等待关门不重生、附着炮台、host 单发事件、guest 重放不重发与 busy RNG 不变。

离线 fixture 证明接口和控制流程；它不替代实机镜头、碰撞、真实音效或双机时序验收。


## PvP 的真实部件与发射阵营

`EnemyControlAdapter::PvpHooks` 已提供实际只读集合与 typed 缓存读写。弹丸路径仍是 `edf41classes::Fire(owner,...)`：每发从真实 owner `+0x314` 同步复制队伍到 InitParam `+0x90`。母舰没有伪造的 WeaponBase；所有实际 `Part.team` 以 `ownerTeamMirror` 枚举，按私有 record、原生 ObjRef 和单调 incarnation 验证后才可读写。未知字段指针、未知仍活着的 owner、不可读内存均为 invalid；已死亡但仍分配的原生对象及私有缓存继续参与恢复。

301：选中任何一个部件也收集其真实 hull 和整个 `h.parts`，所选 root 保持列表首项且只出现一次。`Masking` 会优先读取这个精确成员已经成功应用的租约阵营，防止 disabled hull 保持 native team -1 时，原 `PartTeam(-1/3 → 1)` 又把所有部件改回敌方。模式关闭后恢复原 `PartTeam` 映射。集合无截断：生产 collector 在 256 个实际成员、255 个缓存镜像时完整返回，在 257 成员时失败。

401：八种可接管战斗部件都不创建结构子部件；只有禁止接管的 Panel01 拥有 mounts、隐形 hull 拥有布局。collector 保留实际 root 与 `Part.team`，并对意外 mounts / linked child 明确失败。`HullLogic` 对每个精确已应用 PvP lease 的战斗部件暂停上层队伍回写，其他兄弟保持原父子关系；暂停期间发生的父阵营变化用 `pvpParentDeferred` 保存，模式关闭后无需母舰再次换队即会补回。这是明确隔离后的独立武器树，不把 19×19 布局截断成假完整树。

新生 301 部件的缓存从实际 native owner 队伍初始化，额外的 first-Masking 门保留原来首次 SetTeam 行为，避免把无效 sentinel 当出生基线。缓存和 native 队伍的原值分别保存，不能拿 selected part 的 rootTeam 推测 hull、兄弟或私有缓存的原值。301 的结构部件在 factory 返回前由 `SpawnParts` 创建一次，永久 `built` 门阻止控制期新增；401 可控战斗部件不拥有结构创建路径。现有 binding 一生不重建；意外新增成员或缓存无法给出原基线时明确拒绝，绝不把继承到的临时 PvP 阵营保存为原值。

召唤出的普通敌人是独立生命周期对象：301 不把它们登记为 Part；401 只有常驻波弱槽，一次生成没有 Part 记录；清理母舰不会删除这些敌人。`NetSpawn` 的 network child identity 不能替代真实结构归属。它们通过 `MotherSpawnOwnership` 桥交给当前 controlled-PVP 租约的独立 group 写集：先按真实原语义 `SetLevel / SetTeam` 初始化，301 只在 `HasTemporaryTeam(hull)` 仍为真时从保存的 hull baseline 恢复原 `PartTeam` 映射；稳定关闭 PvP 后直接使用任务当前 hull 阵营，避免在下一 generic Frame 前套用旧快照；401 保留原生成器 team 1；然后捕获该新 GOB 及其真实子树/发射器的基线，再继承操作者 PvP 队伍。成功采用后才 `Activate`、写入原常驻槽和分配 native spawn identity。

`canPrepare / prepare` 在手动命令、自动开门和接管已经在途波时进行整波资源证明/容量预检。无法完整证明的路径或整波预算不足直接标为不可用并拒绝，不能先按敌方队伍生兵再补标签。保留令牌绑定当前精确 parent 和 lease，由 `adopt` 逐个消费，`finish` 只归还未用预算。已采用的新单位由 backend 保存它自己的 typed provider Hooks，绝不送给只认识母舰私有缓存的 reader；结束一波不丢失这些单位的原基线，切队、关闭 PvP、退出控制都恢复它们但不删除它们。它们没有被伪装为 Vehicle/Seat，也没有被写入长期 support/PUID 身份。

Source 端桥和调用已完成；`pvp_controlled_spawn` 负责真实 binder，native/ported resolver 负责资源到 `SpawnProof` 的静态 class 证明。301 同时注册已读取真实源 SGO 的精确 `app:/Object/UfoMother301.Sgo` resolver：最大 145 个真实成员、144 个缓存，Cfg 只能删减相应构造组；未知相似路径不能猜成同一工厂。实际新生 class、原生 child 链、弱身份和上界还会在采用前再次验证。401 隐形布局 hull 仍不是受控战斗 body，独立裸生的无 parent 炮台也不能假装已具有合法控制器。

原生构造返回空、或采用返回失败会使控制失败并归还；此处没有新增原生 SEH 恢复承诺。只有本次刚构造、尚未激活/发表且没有成功采用的对象会被清理。已经成功采用的召唤单位始终只恢复字段，不删除。

新增离线断言还直接执行真实 SpawnOne / HatchTick / TickPanel / RunGenerator，provider 边界使用可观测 stand-in，验证 unknown 资源与整波超额零构造、原队捕获早于采用、采用早于激活、波完成与退出不删除成功单位、控制捕获失败归还预算。完整树只读、真实 typed 缓存写入/恢复、实际发射 owner 阵营、disabled hull 映射、401 父级回写隔离与延期恢复、未知/不可读 owner 拒绝、死亡对象保留恢复写集，以及容量边界。引擎边界为 fixture stand-in，实机与双机行为仍未执行。


最终只读复核另确认并补了两项回归：仅更新 401 持久生成配置时保留仍在运行的单次波 token；301 whole-hull / sibling 的失败接管归还不得清除另一控制者的波预算或 fault。所有构造失败测试指返回空/失败值，没有假称已新增原生 SEH 恢复。


稳定 mode-off 的 lease 仍可存在。新增实际 `SpawnOne / HatchTick` 回归覆盖：临时写已恢复 → 任务改变 hull 阵营 → generic Frame 尚未刷新快照 → 立即出生并采用；其原队使用当前任务阵营。临时写仍在且原基线无法确定时拒绝出生。
