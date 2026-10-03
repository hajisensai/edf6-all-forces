# 传送舰激光（src/carrierlaser.cpp）逆向笔记

EDF.dll TimeDateStamp 0x678CCB46，均为 RVA。置信度：H = 反汇编直接读到，M = 推断/间接证据，L = 猜测。

## 1. 传送舰 e508

- `app:/object/e508_carrier.sgo`：`xgs_scene_object_class` = `UfoCarrier508`，`game_object_durability` 1000，核心部件 `RagDollProxys.catapult_A`。(H)
- UfoCarrier508 vtable 0x17C9DC0 → UfoCarrier 0x17C9930 → GameObjectBase → SceneObject（RTTI）。(H)
- HP：最大 +0x2F4、当前 +0x2F8，UfoCarrier 自己的代码在读写（0x4F0FB0、0x4F3600、0x4F6D00、0x4F81D0、0x4F94A4）。(H)
- 舱口：E508_CARRIER.MRAB 的 hatch_A..H 环在本体原点下 16.81 m，半径约 35 m；in_ring 在原点附近，绕竖轴。插件取原点沿 -up 20 m 作为传送口出射点。(M)
- 测试关 sub_vs_mothership.py 生成后立即 `UFOCARRIER_ACTION_OPEN_MODE`，舱门开着，核心可被打伤。打断判据依赖这一点。(M)

## 2. DemoIndirectFire（任务里的卫星激光 DEMOSATELLITELASER*）

- vtable 0x17D4B20，ctor 0x5B55F0（唯一调用点 0x5B5173），Update = slot 5 0x5B5C50。(H)
- 发射单元 IndirectFireControl 在 +0x170（ctor 0x5B56AC `lea rsi,[rdi+0x170]`）。(H)
- ctor 读取 `indirect_fire_param` 后调 config 0x2B5F40，owner 设为自己（0x2B8390，参数 = 自身 +0x28/+0x30），spread 0（0x2B8460），伤害 = 0xD7AE0 系数 × `indirect_fire_damage`（0x2B82E0），瞄准点 +0x20 = 自身位置，再用 0x2B4330 开火。(H)
- Update：step 0x2B95A0(+0x170, dt)，0x2B7B90 判完成后 0x118A1B0 删除自己；不改写 +0x20 / +0x300。(H)
- 不是 GameObjectBase（dynamic_cast 失败）。(H)

## 3. IFC 字段

| 字段 | 含义 | 置信度 |
|---|---|---|
| +0x20 | 瞄准点 | H |
| +0x78/+0x80 | owner weak（0x2B8390 自己 `lock inc` 弱计数） | H |
| +0xD0 | team，每次 step 从 owner+0x314 刷新 | H |
| +0xDC | 伤害（0x2B82E0） | H |
| +0x224 | 散布（0x2B8460） | H |
| +0x2CC | 已开火 | H |
| +0x2D8 | 首发前帧数（param #15） | M |
| +0x2E0 | 发间隔计数（每发从 param #3 重新取） | H |
| +0x2E8 / +0x2E4 | 开火音效只放一次 / 已放次数（param #17[0]） | M |
| +0x2F0 | 剩余发数（param #2） | H |
| +0x2F8 | 弹道（0 = 直线） | H |
| +0x2F9 | 从指定点发射；非 0 时每发起点取 +0x300（0x2B970D / 0x2B9756），否则由 0x2B43A0 按 param #0/#1 算天上的点 | H |
| +0x300 | 起点 | H |

`indirect_fire_param` 其余下标（config 0x2B5F40 的解析顺序推断）：#4 子弹类，#5 速度（+0x220，米/帧），#7 粗细（+0x100），#9 冲击（+0xF0），#10 存活帧（+0xD8），#11 穿透（+0xF4），#12 颜色，#14 爆炸，#18 命中音效。(M)

## 4. 插件做法

- 派生 SGO（`python tools/make_jets.py`，testrange/gen.py `portal_lasers`）：
  - `EDF6VC_PORTAL_SIGHT.SGO`：瞄准光，红色细光束，每帧一发、存活 6 帧，最多 270 发；伤害 0。
  - `EDF6VC_PORTAL_LASER.SGO`：主炮，单发、粗 8、存活 45 帧；伤害由插件写（CarrierLaserDamage）。
- 预载 0x7A3780 后，用 CreateObject 0x11945E0 在目标点生成，然后：owner = 传送舰的 weak-this，伤害写 +0xDC，`+0x2F9 = 1`，+0x300 = 舱口，+0x20 = 目标；瞄准光每帧更新这两点。
- 充能 4 s，最后 1 s 锁定瞄准点；充能期间舰损失 ≥ CarrierLaserBreak × 最大 HP 或被击落 → 打断。

## 5. 未验证

- 未进游戏实测：光束外观（粗细/颜色下标含义）、音效是否刷屏、伤害是按次还是按帧、0 伤害光束擦过玩家有无受击反应、舱口点是否正好在传送口。
- 伤害未乘难度系数（ctor 会乘 0xD7AE0 的系数，插件直接写绝对值）。
