# 弹的命中声：逆向笔记（2026-11-26，EDF.dll TimeDateStamp 0x678CCB46）

用途：`src/blastsound.cpp` 的 `HitSeen` / `Take`（命中装甲 / 跳弹 / 未击穿的战雷声，`docs/feature-wt-sound.md`）。地址都是 RVA。

## 命中记录 0x230CA0

- `void(core rcx, from rdx, vel r8 = core+0xB90, const u32* body r9, [rsp+0x20] const float* point, [rsp+0x28] const float* normal, [rsp+0x30] u32 material)`。
- 调 0x108260(*body) 取物体；物体为空时提前返回（0x230EC7），否则一律把这次命中推进 core+0x6E0 的命中队列，中间没有别的提前返回。
- 调用点（E8 rel32，四处都是 7 个参数）：
  - 飞行步 0x2321B0：0x23238F（逐条处理本帧的扫掠结果，point = 结果+0、normal = 结果+0x10、body = 结果+0x48）、0x23256D（core+0x8FC 那条起点命中，point = core+0x920、normal = core+0x930、body = core+0x968）。
  - 首帧扫掠 0x233CB0：0x23403B、0x234210（point = rsp+0x40，normal = rbp+0x70，body = rbp+0xA8；之后 `or [core+0xAF4],4`）。这个函数里没有 AmmoHitSe。
- 0x108260 开头 `48 83 EC 28 48 8B 05 ED A6 FA 01 8B D1 48 8D 48`（`enemy_control_target.cpp` / `gravity.cpp` 也用）；0x230CA0 开头 `48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55`。

## 原版的命中声

- 飞行步 0x2321B0 的命中循环跑完后，**一帧最多一次**在 0x2325EE 调 0x7B4510(core+0xA70 AmmoHitSe, core+0xB80, 0)，条件是
  `!(AF4 & 0x20) && core+0xB04 != 3 && (AF4 & 0x400) && (!(AF4 & 0x100) || (AF4 & 4))`。
  - 0x400 由初始化给每发弹置位（`fuze.h` kHitSound），0x100 是 AmmoIsPenetration：击穿弹默认不放命中声。
  - 这是普通弹打到任何东西（地面、建筑、敌人、载具）时唯一的「弹的命中声」，爆炸弹的爆炸声也走这一处（`ExplodeSe`）。
- 原版没有跳弹，也没有入射角模型：0x231640 只在 core+0xB04 为 1 / 2（弹跳模式）时按法线反射速度，不放声音。
- 地面材质的弹痕声只给没有物体的命中记录放。
- 目标自己的受击声在它的伤害函数（vtable 第 18 槽）里：
  - VehicleBase 0x62E830、WalkingEnemyBase 0x53E670、运输船、德洛伊、无人机都不直接放声音。
  - UFO（0x52E110、0x5038D0）和移动要塞（0x4C60E0）在单次伤害 ≥ +0x484 时放自己的预设 4，死亡时放预设 5。这是目标的反应，保留。

## 预设的播放（Debug 探针）

- 0x7B4460 / 0x7B44B0（按材质下标）/ 0x7B4510 / 0x7B4560（按材质下标）四个都在 bank（preset+0x20 或材质槽）为空时直接返回，否则调 0x7AA8C0，调用点 0x7B449F / 0x7B44FC / 0x7B454F / 0x7B45AE。
- 0x7AA8C0：rcx = bank、edx = cue、r8 = preset、xmm3 / [rsp+0x20] float、[rsp+0x28] 指针、[rsp+0x30] 0。开头 `48 83 EC 58 F3 0F 10 84 24 80 00 00 00 4D 8B C8`。
- 载具扣住的预设（`vehsound.cpp` `Hold`：把五个 {bank, cue} 清零）走不到 0x7AA8C0。探针把「扣住的预设也起播了」单独计成 `stock_se_held`，正常应为 0。

## 被击物体的分类

`HitSeen` 按物体 vtable 的 RTTI 层级（complete object locator 在 vtable−8，层级 +0x10，基类数 +8、数组 +0xC，类型描述符名在 +0x10）判断是否装甲。下面的类名都确认在 EDF.dll 里有 `.?AV<名>@@`：
- 载具：VehicleBase、EventHelicopter。
- 机器人：WalkingEnemyBase、EDF6_BerserkerBase，以及 EDF6_Berserker_Equipment_{Bomb,GrappleWire,RocketPunch}。
- 运输船：TransporterBase、Transporter_Container。
- 移动要塞：MovingFortress、MovingFortress_PartsBase。
- UFO：UfoMother511、UfoMother511PartsBase、UfoSmall507、UfoSmall515、UfoCarrier、UfoCarrier508、EDF6_SpinnerUfo。
- 德洛伊：Deiroi、DeiroiFootParts_Base。
- 时间船：EDF6_TimeShip、EDF6_TimeShip_Anchor、EDF6_TimeShip_Anchor_Barrier、EDF6_TimeShip_Capsule。
- GrandMother：EDF6_GrandMother、EDF6_GrandMotherPartsBase、EDF6_GrandMother_Arm。
- 其他：DropBoat512、SoldierDroneBase、BombingPlane、GeneratorPoll、SentryGunBullet_Open01、Decoy、DecoyBody01、EDF6_Jormungand_AdditionalParts。

弹类按弹的 vtable 判断（`blastsound.cpp` `KindOf` 那几个）：SolidBullet01 0x17A3F78、Rail 0x17A4038、SolidExpBullet01 0x17A4498、GrenadeBullet01 0x17A17E0、GrenadeBullet01_MapNoDamage 0x17A16E8（玻尔斯和插件近炸高爆弹的弹类，不毁建筑；RTTI 里三张虚表与 GrenadeBullet01 同在 0 / 0x120 / 0xE90，同一对象布局）；只看落下的 RocketBullet01 0x17A38E8、RocketBullet01_LowLight 0x17A39A8、BombBullet01 0x17A47D0。2026-11-26 之前漏了两个派生类，近炸高爆弹没有掠过声和来袭声。
