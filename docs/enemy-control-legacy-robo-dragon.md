# EDF4.1 阿尔戈与大龙的真实玩家控制

2026-10-19。本轮在既有 EDF4.1 控制器上加玩家输入，不创建替代爆炸、不把单独播放 CAS 当作技能。
实现：`src/edf41robo.cpp`、`src/edf41dragon.cpp`、`src/edf41ai.cpp`；三类自定义 adapter 由
`src/enemy_control_legacy.cpp` 的 `InstallEnemyControlLegacy()` 统一注册。母舰另见
[母舰接口与验证](enemy-control-legacy-mother.md)。未写游戏目录，未执行游戏或双机验收。

## 阿尔戈完整目录和生命周期

UFOROBO401 的源 `robo_attack_table` 有八项：左右劈击、左右踩踏、双手环扫、交叉光束、歼灭炮、直射光束。
目录逐项读取实际 `Robo.attacks`，不固定截成八个，也不接收调用方的 CAS 字符串。加上真实单向船→机器人变形、
膝部双管/船形小齐射、肩炮/船形中齐射及飞船巴斯特，共十二个语义入口；新源条目会改变 profile fingerprint。
源 Robo 没有抓投技能，不虚构一个抓投动画。

接管只替换 `AiTick` 的决定输入，并暂停 `GunTick` 的自主 autoFire 倒计时。输入仍进入原 `BodyTick`、
`GunEnter`、`GunTick`、脚步 root motion、MAB events、瞄准骨和 IK。变形仍走 transform→fall→landing，
船形齐射仍走 S1/S2/S3，机器人攻击走 S7 chooser→S9，双炮独立进入原 G1 蓄力。MAB 砲撃開始/終了控制
真正手炮发射期，歼灭炮仍由原 MAB 事件起始。已有动作、声音、弹丸和死亡状态继续，不被接管取消。

平移输入先由源相机方向转为实际本体命令，沿原速度平滑/根运动执行。在步行中请求攻击时先给原 chooser 中性移动，经过实际归脚片段再进入攻击，防止持续按前进让两脚一直交替而
技能永远等不到入口。退出只释放输入和未消费请求，原 AI/自动炮计时器原地续跑；不恢复生命值、不回退已经完成的变形。
死亡、析构和关卡清理释放 control 记录，原布娃娃/掉落/死亡事件不变。

## 同一输入不能因接收帧率改变瞄准

源输入包含摇杆轴和鼠标增量，网络会以约 33 ms 合并最新采样。旧的每次 `Apply(false)` 再积分 look 会让各端因
重用同一采样次数不同而转到不同角度；只按新 sequence 积分也会漏掉源镜头在合并前的鼠标运动。

核心现在在源镜头积分之后发送 `Input.absoluteAim/aimYaw/aimPitch`。共享 `enemy_legacy::Control` 只在新 sequence
直接采用这个绝对方向，不再积分 look；重复采样只维持移动状态，不重放 press，也不改朝向。sequence=0 只允许
全零 neutral，用于 pending/释放输入。视线原点优先用真实 AimPoint 锁点，fallback 本体位置+2，和核心相机一致。

Robo 仍由原 edf41net 的 host 决定状态，所有 peer 都可完成 registered 准备、租约捕获和输入校验。guest 不执行
`PlayerTick`，只更新同一绝对瞄准并重放原 body/gun/MAB。敌人的原 guest 本机弹丸负责该机玩家的受击判定，
因此保留原 GunTick，不能把它误作第二条玩家触发路径。

控制期的 body/gun 事件分别用 private kind 4/5，将原状态载荷与 lease、单调事件编号、源 RNG、绝对 aim 一起传递。
旧租约或重复事件不重开攻击，也不重设正在发射的 RNG。非接管 AI 保持原 kind 1/2 格式。准备门使用
`edf41net::Enabled && Registered`；`IsMirror` 仅是离线开发 loopback，不是在线 guest 的判定。

## 大龙的安装后原生 hook 链

`InstallEdf41Dragon` 将 DragonSmall 的 vt RVA `0x17ABA40` slot7 从 `0x334930` 替换为自己的 `TickHook`。
原严格原生 profile 会拒绝这个已安装的自有 wrapper，而直接跳过 slot7 又会把 wrapper 中的 FlameBullet02 窗口处理停掉。

现在通过 native controller 的 `RegisterControlledAiExtension` 显式登记这条已核对链。`Owns` 同时要求：当前槽是本文件
`TickHook`、保存的 nextTick 是确切的 native `0x334930`、全局槽仍指向本 wrapper。未知或被他人再次替换的 hook 不放行。
原 `TickHook` 仍先原 AI、后原 post-tick；受控 callback 只执行同一个 post-tick，不运行自主 AI。

这个 post-tick 保留：大龙真实 CAS/MAB 喷火窗口、FlameBullet02 的原定位点/参数构造/owner team/原速度伤害、
滑翔攻击片段完成、受击恢复及追踪。普通没有 EDF4.1 marker 的 DragonSmall 返回无额外工作。
批准的技能 seed 通过可选 OnSeed callback 直接交给真正的大龙 flame RNG，不再按对象指针/时钟混种子。

地面喷火的实际模型映射在 `tools/make_edf41_objects.py`：`DRAGON_FLAME_STATES` 和 `DRAGON_ALIAS` 将原生 `capture`
映射为源大龙 `ground_attack`；仍由真实 native capture/body 生命周期启动。按键 record 编号由原生目录审计负责，
不因旧文档的 bit 术语自行猜测或新增裸 CAS 入口。

## 普通4.1 Hector：自有 callsite hooks 不能抢回玩家意图

Hector 的 native vtable 没有被替换，不代表已经避开全部旧 AI：`edf41ai.cpp` 的 slot5 base-update callsite
仍会更新 back-off 和旧目标，枪启动 callsite 会按 `PickArms` 随机换臂，瞄准骨会把枪转回旧目标。
因此只暂停核心 slot7 还不完整。

现在只对本模块实际 `Entry`、匹配 weak ctrl 和核心当前私有输入 vtable 的受控对象隔离这些决定：Tick 暂停 back-off
及其 RNG；瞄准骨使用核心已经按当前绝对视角与场景射线预缓存的同一个 `EnemyControlWeaponAim` 点，枪/姿态 hook 不
另跑物理射线。玩家显式选择的原枪不再被随机 arm mask 拦掉。

三组 MoveTo/FaceTarget hook 位置（0x34D8D8/0x34D955/0x34DA65 和 0x34D8FD/0x34D96A/0x34DA7A）
已由原生调用链核对为 slot7 AI follow/point/chase；控制期间若陈旧调用抵达则不写 +0x1290/+0x12A4，
原生 pad 移动、技能冲刺/近战的其它 MoveTo 路径保留。原 base update、4.1 枪炮动画、gait、蓄力灯和音弹生命周期照常。
退出后原 target/back-off/arm chooser 立即恢复；普通非4.1对象不经过这个私有 Entry 门。

## PVP 队伍证明

Robo 没有持久 `r.team` 镜像或 WeaponBase 子件。四个 Gun 是参数描述：`FireOne→FireSe→Fire` 每次从 owner +0x314
复制到子弹 InitParam +0x90；劈击/踩踏的 `AreaDamage` 每次从 owner +0x314 复制到 GDI +0x24。
charge particle、muzzle flash 和 lock-point 不发伤害。其 PvpHooks 因而严格枚举实际 root 一成员、零个持久 emitter-team
字段，校验当前 weak ctrl；未知 emitter 回调拒绝，原 +0x314/+0x318 baseline 由 PVP lease 层保存恢复。
大龙 Breathe 同样每发动态读 owner +0x314→InitParam +0x90，不能虚构一个私有 team 字段替代。

## 已执行验证

- Robo 独立 MSVC `/W4 /WX /utf-8 /permissive-` 编译通过。`tests/enemy_control_legacy_robo_test.cpp` 直接包含生产
  controller、链接生产 `enemy_control_logic.cpp`，引擎边界为可观察 stand-in。332 条断言覆盖完整源技能目录、真实
  Body/Gun 路由、MAB 蓄力→两炮真实 FireSe 参数→结束、移动/归还、动态新技能、未知 ID、同序列去重、容量拒绝、
  不同重复帧数的绝对 aim 相等，以及 lease/seed 原子网络重放和 root-only PVP proof。
- `tests/edf41dragon_control_fixture.cpp` 直接包含生产 Dragon 代码，仅在本进程私有 image 布置 native seam trampolines。
  18 项通过：真实安装注册、未知 hook 拒绝、受控时无 AI、真实 FlameBullet02 构造与析构/伤害/炮口、相同 seed 的
  完全一致射向和弹丸 seed、窗口外/死亡无火焰、滑翔片段结束，以及非接管时原 AI 一次转发。

- `tests/edf41hector_control_fixture.cpp` 直接包含生产 Hector hooks，14 项通过：真实配置、受控 back-off/RNG 冻结、
  核心同一场景瞄准点、陈旧 AI MoveTo/Face 不写玩家意图、原会压掉双臂的 PickArms mask 不阻止显式开火、
  原 base update 保留、neutral 不回退 AI，以及退出恢复和普通原版 owner 透传。

这些验证覆盖接口和原控制路径，仍需实机确认模型、镜头、碰撞、真实音效及双机在延迟下的技能时序。
