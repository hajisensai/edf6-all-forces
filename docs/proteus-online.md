# 普罗透斯状态与防护同步

`proteus_net.cpp/.h/.inc` 使用原版 BigBegaruta 的 NetworkObject 对象事件；不另开连接，也不转发第二份命中。适用 EDF.dll `0x678CCB46`。

## 两种权威

驾驶权与伤害结算权不同：

- **控制权威**由 `IsOnlineAuthority` 决定：当前注册驾驶员所在的机器；空座或仅有未注册 Dummy 时为 host。发送架设阶段、计时、开盾/热量/过热、盾方向、可见范围、两席模式和力场参数。
- **防护结算权威**为目标对象的注册 owner（车辆 `+0x128` remote bit 为 0）。原版 BigBegaruta slot 34 `0x6347C0` 对普通命中就是这条规则：远端副本只接受 `fromNetwork` 重放。coop W3 也在同一目标 owner 结算，故 allforces 单装和与 coop 并装使用同一实现。

barrier 的消耗和恢复只在目标 owner 发生，用独立 defense 状态流回报驾驶员与其他副本。control 包不能覆盖 barrier/quiet；被屏障全部吸收、车体 HP 没变的命中也立即发布屏障剩余量。普通原生 HP delta 本身不经过 `0x547C30`，也不携带屏障余量。
远端副本收到原生 `0x40` damage replay 不再吸收一次；owner 上的 `0x40` 不能一概拒绝，因为 W3 的无攻击者新事件也用它标记合法结算。

吸收前还要满足 `proteus_damage_gate.h` 的 BigBegaruta 原生许可门：scene veto、自伤弱引用身份、友伤权限、damage-disabled、GDI veto 与无敌标记。
这些门原本内联在 `0x547C30`，没有可单独调用的完整 preflight。helper 只适用于已确认 slot 16/17 均为原版恒真的 BigBegaruta；安装时校验对应 vtable 与关键指令。
被拒友伤/无敌不会先扣屏障；合法全吸收即使 HP 差为 0 也照常扣屏障，完全不使用 HPdiff 判定合法性。友伤获准后及其他原生减伤倍率仍由原有伤害路径处理。

## 协议和生命周期

BigBegaruta NetworkObject slot 17 的 vtable 项为 `0x17DEE98`（原版 `0x6325B0`）；发送 slot 16 `0x17DEE90` 是 `0x773DA0`。
类型 15 + 独立 `PRNT` magic + version 1；kind 区分 control 与 defense，两条去重水位独立。类型 13/14、其他 magic 原位交给下一接收函数。
发送/接收共用原版 stream codec，wire 中没有指针、原生 `bool` 或隐式尾填充。状态固定 **136 B**；原生最大宽度对象/event 序号封装实测完整应用包 **153 / 1170 B**，不要求 coop 分片。

每辆车按 `ObjRef` 保存身份与最多 1024 sender 的去重水位。包必须匹配当前注册驾驶员 ReferenceId；空座/NPC 纪元为 -1。
更换驾驶员清空按键边沿、标记和未完成齐射，旧驾驶员/旧 host 的迟到包不能夺回控制。控制状态活动时每 50 game ms 发、空机每 500 ms 发，切换动作立即发；defense 变化立即发，另每 250 ms 补发。
真实已注册 NPC 驾驶员保留自己的 ReferenceId，按其实际 machine owner 判控制权；只有空座/未注册 Dummy 使用 -1。host NPC 开车、guest 真人留在炮手席时，host 仍发布活动状态，不要求 host 自己有真人坐在车上；最后一名真人离车后停止本轮特殊重构状态。
超过 1500 ms 没有有效控制包时隐藏盾、收回支撑姿态、撤销该源的待应用力场增益。明确下车/撤收/关闭、对象销毁或地址复用同样停止旧状态；任务重置清空全部水位和效果记录。

## 远端消费边界

远端不运行驾驶员按键、架设 `Step`、`Legs`、`Mark`、`DriverGun` 或 `Salvo`。原版仍同步车辆运动和普通枪弹；现有 slot 45 姿态钩子用收到的阶段/时长驱动真实支撑桩，用收到的 arc/nose 驱动真实盾面板。
同一组参数用于防护判定，不用接收机的 INI 猜测防御强度。两席布局在副本上同步，只有**本机真实炮手（玩家或有实际本机权威的真实 NPC）**继续操作自己的原版并联炮；Dummy 不能冒充炮手，复制来的远端 NPC 也不触发第二份。普通观察副本不拉扳机、不踢 NPC、不再发一份自定义齐射。

## 力场

控制状态包含 field radius、defense、attack、fire rate、energy、power 和团队。fresh 部署副本在本机遍历对应友军，只修改该目标实际 owner 的对象字段；所以驾驶员在 guest、周围 NPC 属于 host 时，host 的 NPC 也能得到减伤。

`proteus_field.inc` 按目标 ObjRef、来源 ObjRef、GameFrame 记贡献。多个场取每项最强效果，不把减伤/增伤再次相乘；射速倒计时、能量和积分仅追加与已给值的差。每帧最多记录 1024 个实际覆盖的目标。
未被原生更新消费的临时乘数保留写前值，换帧或撤销时恢复；已被原生消费/reset 或被外部改动的值不强行覆盖。撤销一个场后仍保留其他有效场的最强贡献，目标所有权变化后不继续写它的远端副本。
已经为过去时间发放的能量/积分不倒扣；停止来源后不再发放。原生已经折算进当帧属性的倍率由原生下一次对象更新刷新，不把修改未知内部累计值当作“即时撤销”。

## 验证边界

- `proteus_net_test`：协议、双权威、乱序/重复、驾驶员/host 纪元和退出。
- `proteus_net_runtime_test`：直接执行生产收包、Frame、Shield、FieldVisit 和模型姿态，覆盖真实桩/面板矩阵写入、不重复驾驶/齐射、全吸收的屏障通知、远端重放、host NPC 获 guest 力场、重叠/重复/撤销/过期。
- `proteus_net_native_test <EDF.dll>`：独立进程映射 DLL、不运行入口；钻头、Sazabi、Proteus 三通道同时安装，运行真实 codec、原生未知消息处理、对象/最终事件包封装。
- 原有 `proteus_frame_test` 与 `proteus_visual_native_profile.py` 分别验证本机生产操作和真实 SetWorld ABI。
- `proteus_damage_native_audit.py --gate-dll <proteus_damage_gate_bridge.dll>`：生产 header 与完整原生 `0x547C30` 对照，17 项包含被拒友伤/无敌、显式友伤许可、合法全吸收、弱引用过期和 W3 需要的 owner `0x40` 例外；生产网络回归也检查被拒命中不扣池、不发 defense。

没有运行游戏或实际双机房间；这些生产路径、假对象和私有原生映像测试不能称为双机 E2E 已完成。
