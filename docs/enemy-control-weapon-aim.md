# 受控敌人的原生武器瞄准合同

实现：`src/enemy_control_weapon_aim.cpp/.h`。支持原版 Steam `EDF.dll`（时间戳 `0x678CCB46`）。逐类、逐原版对象的证据表为 `tests/enemy_control_weapon_aim_contract.json`，当前覆盖34家族和183个原版SGO。

## 同源点、真实对象与原生发射

Core 在输入进入原生回调前计算并缓存绝对准星点。武器回调只读该缓存，不重复射线、不构造假GameObject。Walking 已接受攻击优先使用主机确认且冻结的攻击点；独立Large炮还有逐炮的已确认点。正常结束的持续攻击保持最后已确认点，退出/失效由控制器取消、还原真实原生状态。

目标坐标在原生速度/重力求解之前替换。炮口位置、参数对象、速度、重力、弹量、冷却、后续散射与随机序列仍属于原生武器。只有真实成员才可接管：普通Humanoid武器校验实际数组与自身weak；Boss子手/普通GunBattery分别校验真实父子weak和各自列表；Large按实际guncontrol列表；Squid按实际component列表。Deiroi主体炮是plain vector row（root+1638/count+1648/stride290），不能把它当脚炮或SceneObject。

Boss原生无AI目标门在受控分支直接进入同一mode分派，mode0继续原231110速度/重力求解，mode2继续原生扇形轨迹。流星在443A93的sender gate之前修改原栈direction；原生包和本机factory读取同一值，443B5E仅委托、不二次瞄准，guest保原包重放。其native弹药LockPoint weak来自Core缓存的真实选择，复制增加弱引用计数后交原接收者释放。没有合法锁点时传合法空pair，不把worldpoint伪装对象，也不把nullptr交548EF0。

## 不同原生武器的接入边界

| 家族/武器 | 接入点与保留行为 |
|---|---|
| Large与其余Walking原生枪/离体手 | Large逐炮34E470真实point回调；direct native unit在6D1800前临时替换目标；离体手3403D1在真实351940构造前替换point。Bommer是手部实体炸弹。 |
| 原生Ant/EX | 40B13C真实酸液工厂的实际骨骼炮口矩阵；EX另有direct unit。40DD1A孤立代码不作为实际酸液发射覆盖证据。 |
| Bee/EX、DragonSmall、Ufo507/515 | 目标存在/为空的原生汇合点，之后保留原生弹道、炮口、散射。Ufo515保留替点后原骨骼查询的时序。 |
| Spider/EX、Nephila | 真实context、炮口和弹速调用原2312A0，结果进入原散射/工厂。EX还覆盖新增unit。 |
| Merman、ShellFish、Spinner | 替换原生扫射基准点，保留各枪局部转换、起止角和插值；不能在最终unit强行逐弹覆盖为同一点。 |
| Dyno | 主火球和横焰是direct point；独立root+1498竖焰走原骨骼模式，保留原生形状。 |
| Squid、Dango | 分别在真实逐组件炮口减向量、滚动动作的point汇合处替换，保留原散射/物理。 |
| Humanoid四族 | 普通逐手物理瞄准与实际weapon+720目标；Boss另有真实手炮、激光/矛与流星。盾保持真实来弹防御与生命周期。 |
| Monster501/504 | 原生颈部solver启用时替换+6B0点，保原角限制/插值、骨骼炮口和504各技能模式；6DC8B0只是材质曲线。 |
| Deiroi | 脚炮要求独立adapter完整；主体主炮在31CC4B目标/空目标分支调用原2312A0一次，随后原cone/RNG/6CFC60，Fire0保真实炮口矩阵与预热状态。 |
| AlienTrailer、TimeShip、DropBoat、Carrier | 原版能力为屏障、实体接触、舱门或生成器，无原生目标点弹丸；生成的敌人是独立root。插件额外Carrier激光不在原版class技能合同内。 |

## 准入与验证

`EnemyControlWeaponAimReady(profile)`只接受真实profile表中的34类。所有typed call当前目标/relay、全部生成跳板完整字节、原函数指纹均必须匹配；任意半安装、未知改写或版本不符均拒绝。Deiroi还必须通过独立脚炮adapter的当前hook所有权检查。非受控路径逐参数/逐指令转发原生链。

`enemy_control_weapon_aim_test.cpp`实际安装并执行生成的x64跳板，核查整数/浮点寄存器与flags、真实目标/空目标分支、原生求解参数、逐枪会员和真实锁弱引用，以及全34类准入与篡改撤销。`enemy_control_weapon_aim_native_audit.py`只读支持版本PE及相邻Root.cpk，核对指令窗口、调用目标和183个对象的真实class/原生物理模式。`edf41dragon_control_fixture.cpp`通过真实Breathe→FlameBullet02发射链验证非零pitch准星、真实炮口、原速度与三个随机消耗。

上述为离线源码/原生文件/生产路径fixture证据；没有驱动游戏或完成两机联机实测。
