# 普罗透斯状态与防护同步

`proteus_net.cpp/.h/.inc` 使用原版 BigBegaruta 的 NetworkObject 对象事件；不另开连接，也不转发第二份命中。适用 EDF.dll `0x678CCB46`。
2026-10-09 重做后协议版本为 **2**（`barrier` 字段改为原版电磁碉堡墙的 HP 比例，新增 `kBroken`）。

**新旧插件混房**（整合审查 2026-10-09 指出：旧说法「双方都只看到原版」不对——v1 包只是被吞掉，新插件那端原本仍立墙、借武器）。现在：
- 收到同 magic、**别的版本**的 Proteus 包（`proteus_net.h ForeignVersion`），不解码，交给 `ProteusNetIncompatible`：这台普罗透斯在本机立刻还原为原版（收墙、不借武器、腿和座位还原），直到离开联机或任务重置。
- 本机是驾驶员、车却归别的机器所有时，owner 若是新插件每 250 ms 必发一次护盾计数；`kDefenseSilentMs`（3 s）收不到即判定 owner 不是本版本，同样还原原版。
- 旧插件那端本来就不认 v2 包（版本校验不过），它那里的普罗透斯是旧版行为或原版，由旧插件自己决定；本插件只保证自己这端不再「一边有墙一边没墙」。
- 无法探测的情形：本机既是驾驶员又是 owner、对方是旧插件的纯旁观副本时，对方不发任何 Proteus 包。此时本机照常重构，对方看到的是旧版 / 原版——墙只在本机存在，挡的也只是本机结算的命中，不会出现 HP 卡死。

## 两种权威

- **控制权威**由 `IsOnlineAuthority` 决定：当前注册驾驶员所在的机器；空座或仅有未注册 Dummy 时为 host。发送姿态、计时、护盾开关、热量 / 过热、护盾朝向、两席布局和力场参数。
- **护盾 HP 权威**为车辆的注册 owner（`+0x128` remote bit 为 0），原版伤害在这里结算（BigBegaruta slot 34 `0x6347C0`）。owner 读自己那面墙的 HP（`proteus_shield.inc BarrierStep`），
  下降时立即、平时每 250 ms 发 defense 包（`barrier` = HP 比例，`kBroken` = 击碎锁定）；也只有 owner 在护盾放下后回复 HP（`proteus::Refill`）。
  驾驶员不是 owner 时，它本机 `Step` 算出的 HP 被丢弃，以 owner 的为准。控制包永远不改 HP / quiet / 击碎（`ApplyControl`）。

## 每台机器上的墙

电磁碉堡墙的创建不广播（BarrierBullet01 只在删除时发包，`0x291B99`）。所以每台机器按收到的控制状态**各自立一面本地的墙**：
控制权威按本机状态，副本按控制包的开关 / 朝向（`ApplyControl` 的 `nose`）；副本把 owner 发来的 HP 写进自己的墙，不用本机挨的打。
插件收起墙前先置 `+0x154C`，删除不广播，不会删掉别人机器上的对象。墙是否在每台机器上都挡住了「在那台机器结算的」敌弹，需双机实测（L）。

## 协议和生命周期

BigBegaruta NetworkObject slot 17 的 vtable 项为 `0x17DEE98`（原版 `0x6325B0`）；发送 slot 16 `0x17DEE90` 是 `0x773DA0`。
类型 15 + 独立 `PRNT` magic + version 2；kind 区分 control 与 defense，两条去重水位独立。状态固定 **136 B**，完整应用包 **153 / 1170 B**。
每辆车按 `ObjRef` 保存身份与去重水位；包必须匹配当前注册驾驶员 ReferenceId；空座 / NPC 纪元为 -1。更换驾驶员清空按键边沿，旧驾驶员的迟到包不能夺回控制。
控制状态活动时每 50 ms 发、空机每 500 ms 发，切换动作立即发。超过 1500 ms 没有有效控制包：收回支撑桩、收起墙、撤销该源的力场。任务重置清空全部状态。

## 武器

借用的原版武器（`proteus_weapons.inc`）在每台机器上都按占位激活（画面一致），只在**操作者所在机器**拉扳机；开火步的使用者答借用者本人。
原版武器的开火与命中照原版同步。远端复制来的 NPC / 玩家不在本机触发第二份。

## 力场

控制状态包含 field radius、defense、attack、fire rate、energy、power 和团队。副本在本机遍历友军，只改该目标实际 owner 的字段（`proteus_field.inc`，多个场取最强）。

## 验证边界

- `proteus_net_test`：协议、双权威、乱序 / 重复、纪元和退出。
- `proteus_net_runtime_test`：生产收包与帧：远端驾驶的姿态 / 座位 / 本地墙、owner 发布 HP、控制包不能回滚 HP、副本只显示 owner 的数、owner 击碎收起副本的墙、力场、过期 / 纪元 / 死亡。
- `proteus_net_native_test <EDF.dll>`：私有映射真实 DLL，运行真实 codec 与对象事件封装。
- `proteus_net_guard`：源码层守卫上述归属。

没有运行游戏或实际双机房间；这些不能称为双机 E2E 已完成。
