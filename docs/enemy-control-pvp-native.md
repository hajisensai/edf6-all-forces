# 敌体操控的原生 PVP 阵营租约

适用 Steam `EDF.dll` 时间戳 `0x678CCB46`。这一层只处理已认证操控者对应的原生阵营与发射器缓存；不改原玩家 User/PUID、对象网络身份、全局碰撞表或滤弹规则。

## 数据与调用时序

`pvp_controlled.h/.cpp` 保存完整 `{object, control}` 弱身份、原 `+314` 阵营、原 `+318` lastTeam，以及每个 typed emitter 的独立原值。普通敌体不能套用 VehicleBase/SeatCount。根对象的 `+550` 原生 follower 链递归收集；Deiroi 脚、Boss 部件、机械臂/炸弹、TimeShip 部件与运输船生成器还要走各自真实 Scene/容器合同。

1. Prepare 阶段 `Probe` 枚举完整 typed 集合，不改阵营、技能或网络状态。为防 ABA，provider 可以保留有界、幂等的弱身份记录。
2. Core 在改输入、意图、私有 vtable 之前 `Acquire`，因此保存的本体基线来自真实接管前状态。PUID 只接受上游已经认证的 owner。
3. 每帧先 `Frame` 阵营，再采样输入/执行技能。`PvpSupportTeam` 为 1/2 时继承，为 0 时恢复原阵营但保留操控；未定态拒绝继续攻击。
4. 退出先在仍持有 PVP 租约时排空已接受的原生动作（包含晚生 Capsule），再完成 PVP `Restore`，随后恢复输入/vtable/AI 与源玩家保护。部分失败保留完整租约及弱引用，不把恢复请求当成恢复成功。
5. `EnemyControlNativePvpInstall` 启动时安装 pass-through 出生观察/发布门。场景清理调用聚合 `EnemyControlNativePvpReset`；存在任何 pending/restoring 租约时拒绝清账，保留恢复所需证据。

原生私有 vtable 仅接受原输入槽 4/5/7 和 Core 精确认证的目标选择槽 14；其它槽不同即拒绝。只有相同 COL 并不足以证明仍是已审计实现。

强引用仍存活但暂不可读的对象属于 invalid，不等于 gone。恢复必须保留它的完整写集与 weak，直到身份再次可验证，或控制块/删除标记证明该对象真正结束。子生成器通过 `ReadControlRoot` 查真实租约根，不能把已死亡的直接 Anchor 父体误当成整个操控者已经结束。

## 关闭状态与原生缓存

原生 `SetTeam(0x54EE70)` 只改自身。`314 == -1` 表示关闭；`318` 保存重新打开时的阵营。当前 `380 & 0x40` 注册状态会随技能变化，不可锁定到接管时的旧值。

操作前一次性采集全部成员的关闭/注册状态，避免父 slot-22 递归 SetTeam 污染后续子成员判断。关闭成员保持 `314=-1`，只继承 lastTeam；恢复时保留当前开闭状态并还原 lastTeam。原 `(-1,-1)` 永久结构件没有可猜的有效开启阵营；若被非法打开，保留恢复请求，只有真实合法关闭后才能释放。

| emitter 类型 | 应用时 | 退出时 |
| --- | --- | --- |
| 原生 WeaponBase/IFC/私有 mirror | typed 字段使用请求阵营 | 各自保存的原字段值 |
| `liveOwnerTeam` / `liveOwnerLastTeam` | 证明每次发射实时读真实 owner314/318，不写不存在的缓存 | 随本体字段恢复 |
| `ownerTeamCache` / `ownerLastTeamCache` | 真实缓存跟随 owner 当前314/318，包含关闭的 -1 | 独立恢复缓存原值 |

GunBattery2 的 owner 是 `+2B0/+2B8`，每次新发射读取 owner318；不能误把 `+10` 浮点配置或 muzzle 方向当 owner。普通已发弹丸保留原版逐发快照。原生继续保活的束与机械臂实际伤害载体则需要 typed 缓存：完整 BulletBase 的候选阵营 `+B40` 和 GDI 阵营 `+894` 分别保存/恢复，双 source weak 必须仍精确等于真实发射 owner。

独立存活的缓存还在 `Emitter.storage` 携带完整 SceneObject 弱身份；它不是 GOB，绝不调用 SetTeam。即使原 owner 已死，或旧武器容器已移除该束，只要 storage 仍活，必须先还原缓存再释放弱引用。仅“容器里找不到”不能证明存储已销毁。member 本体字段可省略 storage，沿 member 生命周期处理。

## 动态出生与整波预算

母舰、Boss 陨石、机械臂/炸弹、运输船及 Capsule 载荷都必须在真实初始化基线之后、激活/网络发布之前纳入当前租约。新对象保存自己的 hooks/group，不把 native emitter 送给母舰私有 Part.team reader。

`CanPrepareSpawns/PrepareSpawns/AdoptSpawn/FinishSpawns` 以真实资源 proof 和整波数量预留成员、发射器与 group 预算。`Finish` 只返还未用额度；已经 adopt 的对象在退出时恢复阵营而不删除。失败的新生对象在未发布边界撤回；普通未操控 AI 继续原生路径。机械臂 spawn-group 自身无 emitter，但它在父集合新增的两个持久伤害缓存也必须算入整波额度。

机械臂的 PVP 作用域在 walking-parts 已接受的原生执行层进入；guest 尚未接受时不进入创建/预算作用域，构造器与 fallback 由唯一 hook owner 委托，发射器通过公共 native dispatcher，避免同一调用点二次 Redirect。

母舰资源桥由 `pvp_controlled_spawn` 安装，native 资源表由 `tools/gen_enemy_control_pvp_spawn_profiles.py` 从原 Root.cpk SGO 精确 class/配置生成。实际 newborn 仍再次检查类、完整树、弱身份与上界；资源路径字符串本身不构成所有权。ported resolver 独立注册，不能冒充原生 Root.cpk proof。`tools/gen_enemy_control_ported_spawn_profiles.py` 从本仓 EDF4.1 登记与实际二进制 SGO 生成独立清单，校验资源 class、武器配置、依赖与 SHA256；只接受清单里的完整规范路径。Robo、龙、Hector 和 NPC 各用真实 typed provider，不把相同资源目录当成相同原生类。当前独立表包含 75 项：Robo 1、龙 7、Hector 21、游骑兵 12、飞翼兵 10、重装兵 6、其它已审普通类 18；这不是整个移植目录的全量可控声明。

`SpawnProof.prepareInstance` 只在提交 Adopt 时、实际新生对象通过类/完整树/预算/跨租约检查之后执行。执行后重新解析 token、租约身份和完整集合，再保存基线，拒绝初始化回调中的重入替换。Robo 通过它运行原来的 `Setup`：原 team6 真实转为1，任务已设的其它阵营保留；四 Gun 只有弹丸描述，伤害每发读取 owner314，没有伪造的持久 team 字段。普通未受控创建仍由原首帧 Update/Message 初始化。

NPC 的原生 Soldier 1950 武器 roster 与实际 Scene 子表共同校验，发射用真实 Weapon214；BasicShoot 活持久束会阻止新一批发射，因此清单从实际 FireCount/FireType/Burst 配置给出每武器的活束上界。Weapon214 与各活伤害缓存均保存独立 Scene storage 弱身份，退出归还不依赖原 NPC 或武器 holder 仍在 roster。重装兵盾击的伤害是独立 ShieldBash `EF0`，source 在 `E98/EA0`；它没有普通弹丸 core，不能套用 `B40/894`。三种 NPC 表的 94 槽、四种武器的 41 槽和 ShieldBash 的 14 槽逐一验证；共享 slot7 的唯一例外由 `NpcOwnsNativeThink` 精确认证本仓 wrapper 及其原生下游，未知 chain 拒绝。

中性的 `PvpNativeWeaponEmitters/ReadWeaponEmitter/WriteWeaponEmitter` 只验证真实 typed emitter 与 storage，不取得 controlled lease，也不查询操控状态。普通玩家/支援 PVP 调用者必须独立持有 weak、保存基线和完成恢复；该 API 精确核验 29 种原生 WeaponBase 主表及各 41 槽，不构成所有派生类或其它技能生命周期的白名单。它验证整个 F20 weak 链，只输出 `C30==1` 且 `C34 bit14` 未停止的真实保活缓存；普通已发弹丸和停止态保留发射时快照，ShieldBash 也按同一保活条件处理。

关闭阶段已经由协议接受的 TimeShip Capsule 另走窄 accepted-token 桥：父体逻辑已删除但同一完整弱身份仍被强持有时，先只读预算；原生构造完成后、RegisterChild 之前还回真实出生基线并同步 Adopt。这里只给已接受消费者使用，不把普通死亡对象开放为新召唤入口；未知 owner、失效身份、未知基线和已经开始 generic 恢复均拒绝。最终 Restore 会包含这些晚生 group，不依赖关闭阶段已跳过的下一 Frame。

当前固定上界是每租约 256 成员、512 发射器、128 独立 spawn group；整波超出即在创建前拒绝，不执行半波后再报告成功。

## 原生家族分工与环境对象

主 provider 对照 `enemy_control_native_profiles.inc` 的全部 34 个受控 native profile。普通家族发射与接触伤害在 `enemy_control_pvp_native`，复杂闭包分别在 `enemy_control_pvp_boss`、`enemy_control_pvp_berserker`、`enemy_control_pvp_transport`。每类都有精确 vtable/RTTI 与实际 owner/容器指纹；未知实际子对象或发射分支不能静默视作零发射器。

Nephila 的 SpiderNet 是多个根共享、固定 team6 的任务环境：原生按 team6 搜索最近 web，web 保存多个 occupant 弱引用，未转移 owner 或 parent。保持其环境语义，不纳入某只 Nephila 的写集。GrappleWire 也是非 GOB 的移动辅助，真正机械臂伤害沿 RocketPunch 与其持有的 BulletBase 闭包处理。

## 验证边界

离线 fixture 分别执行真实通用租约、资源桥、typed provider 与真实 Boss/core、Transport/core 接缝；原生地址审计 `tests/enemy_control_pvp_native_audit.py` 只把支持的 DLL 当数据读取，不加载或执行它。各 fixture 的 ABI 桩与覆盖边界应以测试源说明为准。`enemy_control_legacy_robo_test` 还读取实际 `UFOROBO401.SGO` 并执行真实生产 `Setup→Adopt→Restore`，检查中立6转换后保存1和任务4保留4；CAS、模型、声音和 SetTeam 原生 ABI 仍由 fixture 提供。

这些验证不等于游戏内操控手感或真实双机对战验收。最终组合检查与游戏/联机实测仍按仓库验证流程进行，不能用单个 fake-native fixture 代替。
