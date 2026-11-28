# 符文剑士（ZwRuneBlade，type 5）近战与特殊武装：实现与缺口

代码：`src/runeblade_melee_logic.h`（纯逻辑）、`src/runeblade_melee.cpp`（运行时）、`src/runeblade_melee_net.h`（ZWRM 编解码）、
`src/runeblade_ether.h`（以太槽最小版，RB-CORE 的正式版落地后由父会话对齐）、`tests/runeblade_melee_test.cpp`。

## 1. 武器行名（唯一出处 `runeblade_melee_logic.h` kArmRows）

| 行 | 武装 | 类别 |
|---|---|---|
| `ZW_RuneSaber01` | M1 三段连斩、空中下劈、第三段剑痕留 0.5 s 持续伤害 | 500 |
| `ZW_RuneLance01` | M2 蓄力后沿视线冲刺，直线贯穿 | 500 |
| `ZW_RuneRing01` | M3 双刃乱舞；按住 ≥20 帧松开 = 掷环绕身一周飞回，路过的敌人记印，回手后依次爆 | 500 |
| `ZW_RuneHammer01` | M4 地面横扫击退；空中蓄力竖劈，伤害随落差增长（每米 +0.1×，上限 5×），清空以太 | 500 |
| `ZW_RuneIai01` | M5 架势蓄力（8 格表盘），满蓄一线横斩，被斩中者约 1 s（60 帧）后爆 | 500 |
| `ZW_RuneRevolver01` | R2 连斩间隙副扳机射击，不打断连段（6 发 / 冷却 6 帧 / 装填 90 帧） | 501 |
| `ZW_RuneStars01` | R3 按住升起至多 7 个法阵（北斗形，每 10 帧一个，每个 8 以太），松开齐射 | 501 |

行本身由 RB-WEAPONS 写进 `tools/zw_weapons.py` 的 WEAPONS 末尾；本模块不写行。行没装上 = 该槽 Arm::none，武装不出动。

## 2. 运行时

- 链 AssultSoldier vtable 0x17CDF28 槽 5（原版 0x550C30，先核字节头），在 vehicler_foot 之后；克隆 vtable 复制活槽，所以符文剑士也走这里。
- 只驱动本机玩家（`PlayerHuman`）且 vptr 为 `soldier::zwVtables[1]` 的对象，在原版帧之后运行；SEH 包住，异常只记一行。
- 读：位置 +0x90、视角 +0x1240/+0x1244、扳机 +0xD70、离地帧 +0x1264、手中武器（+0x1950 列表中 +0x13E 置位者）、
  副扳机 +0x13C、AmmoDamage +0x89C；槽位武装每 30 帧从游戏状态读（`wequip::ReadEquipped(0,5)` + `ReadRow`）。
- 写：速度 +0x6B0（冲刺 / 下劈 / 竖劈），以太 +0x308（扣量 / 清空）。
- 命中：每个 Strike 按点 / 波 / 延迟展开（`Queue`），每点一个 `edf41c::Blast`（静止 GrenadeBullet01，玩家所有，击杀归玩家）；
  R2 弹与 R3 弹为 LaserBullet02（`edf41c::Fire`，带 custom 节点）。

## 3. 原版近战的关系

原版近战靠 CAS 事件（Weapon_Swing / ImpactHammer 及其子弹）出伤害。本模块只追加原版武器做不到的部分（剑痕、贯穿、环、印记爆、
落差竖劈、居合线、法阵齐射）。M2 / M5 / R3 的原版开火需由行数据压掉（RB-WEAPONS 负责），否则会叠加原版射击。

## 4. 联机（plugin_net 通道 "runeblade melee"，magic `ZWRM`，版本 1，定长 96 字节）

- 规则同 online_authority ShotCounts：玩家自己的弹只在本机算伤害。本机出伤害，并广播 strike / ring / bolt 三种消息。
- 对端：`SenderIsPlayer` 核发送者与玩家引用一致，strike 以伤害 0、属主为该玩家副本重放；ring 在对端按 `RingAt` 绕该玩家副本飞一遍
  （只显示）；bolt 以伤害 0 开一发。玩家位移由 EDF6Coop 的玩家同步带过去。
- 布局见 `runeblade_melee_net.h` 头注释；越界字段整条丢弃。

## 5. 缺口（未做 / 未核实）

1. **没有新特效 / 弹种**：所有区域命中暂用 GrenadeBullet01 爆炸外观，剑痕、环、印记、居合线都没有专用视觉。需要做 Effect → 特效的映射。
2. **R3 弹不追踪**：发射时朝最近的不同敌人瞄准，飞行中不转向。
3. **未实机核实**：+0x6B0 速度单位（按 m/s 推断）、+0x1264 「>0 即在空中」（npc-mobility-re 与 firstperson 笔记说法冲突）、
   +0x13C 为副扳机按住、武器列表下标 = 槽位、`wequip` 只保证 UI 线程（这里在游戏帧线程读）。
4. 印记在**记印时的位置**爆，不跟随敌人移动后的位置（消息只带坐标，不带对象引用）。
5. 只处理本机玩家 0；分屏 2P 不驱动。
6. 依赖 RB-WEAPONS 的行（§1）与 RB-CORE 的以太槽；`runeblade_ether.h` 的偏移（+0x304/+0x308/+0x2FC）需与 RB-CORE 对齐。
7. 未开游戏验证（本轮只做离线：纯逻辑单测 + 单独编译）。
