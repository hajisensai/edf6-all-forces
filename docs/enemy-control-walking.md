# Walking 敌人明确技能与原生生命周期

本模块只支持 Steam EDF.dll 时间戳 `0x678CCB46`，原版 Root.cpk 的 6 个 Walking 子类、23 个 SGO。这里只读检查原版二进制；离线夹具执行的是私有内存上的合成原生边界，不是游戏实测或双机验收。

## 明确选招

所有子类的普通 pad 入口共享 `33F7A0`：记录 0 为跳跃，5 为 `StartAttack(0)`，4 为 `StartAttack(1)`。Middle、TypeA、Bommer、Shooter 忽略后一个参数，两个攻击键原本是重复入口；Grapple 的记录 4 在已抓取状态使用 held，保留抓钩/拉拽/释放的原生上下文。新增菜单保留有用途的原生随机攻击和 Large 全炮齐射，同时按每个实际 SGO `BerserkerSettings.AttackSetting[1]` 逐行列出明确攻击。

`33EDA0(root)` 进入完整原生攻击状态机，而不是直接播放 CAS。状态机 `root+1890` 的入口描述符是 `33E124`，其 virtual `+268` 在六类均指向 `345310`。phase 0 从 `root+17C8` 读取实际 AttackSetting；`34539B..3453C8` 推进原生 `root+3E8` RNG 并选行；`345444` 将所选行 handle 存入 `root+17D8`。唯一新 hook 位于紧随其后的 `34544B -> D7480(settings,out,L"animation_speed")`，只在当前明确技能的 TLS scope 内将该 handle 替为请求的真实表行。随后仍由原生从该行取动画名、进入 `116C2D0`、设置 `116DF20` 速率、执行 MAB、结束判定和 phase 2 清理 `77C8D0`。原生 RNG 的推进、行内结束动画和部件释放保持原生。

普通调用、另一身体和 scope 之外的同一原生调用不改变行为。scope 记录弱身份快照，重入归还/复用会拒绝；finally 始终归还 TLS 和强引用。目录大小先验证整张实际表以及真实 main CAS player 的已初始化非空 state 表，再按原生 `11664F0` 逐个核对行内的开始/结束状态。缺失 CAS 状态的继承模板行不列为攻击（21 个移植 Hector 原有 Large 重击模板没有真实动作）；合法零行通过 `ReadWalkingActionCount` 的 bool 与异常/未初始化明确区分。容量不足仍返回真实 total；任何一行结构损坏则整表失败，不交付截断目录。支持 4096 行边界，夹具明确调用超过 96 的第 128 行。

## 各具体变体

| 子类 | 原版 SGO | 实际明确攻击 | 原生装备 |
|---|---|---|---|
| Large | LARGE / ACE / LITE | 左臂、右臂、双臂重击 | 四个 PulseBullet01 gun_control，首发间隔 0/60/120/180 |
| Large | LARGE_PUNCH | 双拳齐发、先左后右、先右后左 | 两个 PartsShootList 火箭拳，另有四个 EfsBullet gun_control |
| Middle | MIDDLE / MIDDLEUG / MIDDLEUG_MG / MIDDLE_MG / MIDDLE_SPREAD | 头部炮击 | 单个真实 GunBattery；变体分别为 BeamBullet01 / SolidBullet02 / PlasmaBullet01，枪口列表由原生维护 |
| Middle | MIDDLE_PUNCH / MIDDLEUG_PUNCH | 三种火箭拳顺序 | 两个 PartsShootList；未把继承模板里未被实际攻击表调用的头炮设置发明成新技能 |
| TypeA | BERSERKER / BERSERKERUG / BERSERKER_ACE / BERSERKER_ST | 三种火箭拳顺序 | 两个 PartsShootList |
| Bommer | BOMMER / BOMMER_ST | 举弹自爆、前扑自爆 | 两枚真实 BombEquipment |
| Bommer | MIDDLE_BOMMER | 举弹自爆 | 两枚真实 Middle BombEquipment |
| Grapple | GRAPPLE | 三种火箭拳顺序 | 两个真实抓取部件；明确表攻击只在原生主击上下文开放，拉拽/释放继续原 pad |
| Shooter | BEAMA / BEAMB / UG_BEAMA / UG_BEAMB | 头部光束 | 单个真实 BeamBullet01 GunBattery |

`Bommer` 的真实攻击入口 `358760` 在进入攻击状态前设置 `root+380 |= 0x10000000`，新明确入口也保留这个语义。自爆属于可主动使用的真实技能，施放后操控会随死亡结束。

## 状态与归还

状态读取只改状态/冷却字段：当前原生状态机描述符、实际行、非 ready 阶段以及 main CAS player 的真实当前状态（`+104`，开始或结束动画）一致才显示 active；原生完成后即使残留旧描述符也不会继续显示施放中，原生入口门槛不满足显示 cooldown；不伪造剩余秒数或用按键 held 冒充施放成功。Large 的实际门槛是 `12D0&1`；其他普通子类还要求 `12DC!=1`；Grapple 明确选招要求原生 idle 主击上下文且无已发射部件。

每个 native commit 保持根强引用，顺序记录保持弱引用。相同 sequence/ordinal/两条 RNG 状态/原生部件序号重试不重复进入；同 sequence 改动作或 seed 拒绝。公开 Execute 在去重之前不写种子，拒旧/缓存命令保持当前 RNG。原生回调进行中以 intentInFlight/nativeInFlight 拒绝同身体重入及提前归还，普通 foreign 身体仍完全透传。`RestoreWalkingControl` 先要求 accepted journal 真实归还，之后才清本模块的顺序记录和弱引用；失败保留身份与状态供上层重试，同一身体重新接管可从新 lease 的 sequence 重新开始。

## 联机边界

原生 Large tag 10 只有整体动作 0/1、RNG 和上下文，不能携明确表行或单炮序号。明确选招执行完整 `33EDA0` 而不发送原来的整体 tag 10，避免远端先施放一个随机动作。已绑定 native role 始终按该 role 处理；无绑定时必须取得 Core 保留会话的 `EnemyControlNetworkContext`，其中 online 为不可变属性并覆盖 closing/fault 阶段。未知上下文或 online 但无 role 都拒绝，不使用旧全局探针的 cached-false 当作离线授权。实际 host accepted 行、两条 entry 前 RNG、`root+1940` 原生部件序号、canonical point 和真实 emitter outcome 由 `enemy_control_walking_net.*` 通过通用 `NativeActionExtension` 传递；guest 的输入只表达意图，真实 native commit 必须来自 accepted Apply 并验证后才 ACK。原生网络和双机实机效果仍须最终验收。

## 离线检查

- `tests/enemy_control_walking_test.cpp`：生产选择器的完整目录、原生行选择、FSM 提交、引用/TLS 归还、重入、native busy、128 行、联网角色/队列和正常重试夹具。
- `tests/enemy_control_walking_native_audit.py`：真实 DLL 指纹、pad/入口/行选择/结束调用、23 个实际变体攻击表；不执行游戏代码。
- 独立臂炮细节与原生 projectile 生命周期由 `enemy_control_walking_guns.*` / 对应 audit 保持。
