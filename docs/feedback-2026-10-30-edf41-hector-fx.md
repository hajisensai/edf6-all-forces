# 2026-10-30 测试站 #28：赫克托开火特效改回 4.1

## 原话（策划 planner_3d60b0，版本 0.8.0-build.eba4372）

> 赫克托类敌人使用六代的机械融合生命体特效e602_berserker.efarc。需要改成4.1的原版开火特效，即CANNON加农炮类敌人炮口如蒂洛依DEIROI的主炮口一样蓄力一段时间产生光团后开火，Launcher发射器类开火时炮口仅亮起，GUN机枪类开火如6代青蛙和灰人所持有的E506_HA_HEAVYMACHINEGUN.SGO一样发出开火火焰。

## 事实

- 赫克托套 EDF6 类 `EDF6_Berserker_Large`，`gun_control` 每项从模板 `E602_BERSERKER_LARGE.SGO` 复制：`gun_battery.effect_arc =
  app:/object/e602_berserker.efarc`，`charge.effect.esb = large_gun_charge.esb`，`fire.effect.esb = large_gun_fire.esb`（生成的 HECTOR*.SGO 实读）。
- 这个炮台（DSGO 的 gun_battery）只认 efarc 里的 esb：`effect_arc`（EDF+0x6D49C9）、`charge.effect.{esb, scale, early_end, color}`
  （0x6D7500..）、`fire.effect.{esb, scale, color, is_every_time, follow_muzzle_transform}`（0x6D7C90..）；三者都可省（EDF6 自己有只写
  `charge.time`、`fire` 无 effect 的炮台）。没有别的特效形式，所以 4.1 的样子只能由插件画。
- 4.1 的赫克托武器对象（`HECTOR301_CANNON.SGO` 等）没有 esb：`muzzle_flash_class = MuzzleFlash01`，`muzzle_flash_param =
  [マズルフラッシュ01.dds, 12, 1.2, 0, 寿命, 6, 0.6, 2, 颜色, -0.2]`；寿命 10 帧（301 加特林 3），颜色加农炮 / 加特林 `[1, 2, 16, 10]`（蓝）、
  发射器 `[16, 2, 2, 10]`（红）。
- EDF6 的蒂洛依主炮蓄力是 `ChargeEffect_DeiroiBody`（`E509_DEIROI.SGO` 的 `charge_param`：`Particle3_k.dds`，颜色 (0.3, 0.3, 1)，180 帧）。
- E506 重机枪（策划举的例子）同样是 `MuzzleFlash01` + `マズルフラッシュ01.dds`。

## 修复

- 生成器（`tools/make_edf41_objects.py` `HECTOR_FX` / `hector_fire`）：赫克托炮台去掉 `effect_arc`、`charge.effect`、`fire.effect`；
  非加农炮也去掉 `charge.se`（4.1 发射器张开、加特林起转都不出蓄力音）。`edf41_hector` 新增 `fx`：每臂 `[类别, 光团帧数, 炮口 z,
  r, g, b, 闪光帧数]`，颜色与帧数从 4.1 武器 SGO 的 `muzzle_flash_param` 读。
- 插件（`src/edf41ai.cpp` `Effects`，臂瞄准之后每帧）：炮口 = 臂骨世界矩阵上的定位点 (0, 0, z)；炮台 `+0x2E8` 蓄力剩余帧、`+0x2E0`
  本轮剩余发数（比上一帧少 = 开了一枪）。
  - 加农炮：蓄力状态的最后 `光团帧数`（301 180、401 300，401 先张开 60 帧）每帧在炮口放一个 `Particle3_k.dds` 粒子，随蓄力进度从
    0.15 长到 1 倍（14）——蒂洛依主炮那样聚起光团；开火时一个 `マズルフラッシュ01.dds` 闪光 + 一个 StencilLight。
  - 发射器：开火时只亮一下（StencilLight，红）。
  - 加特林：每发一个 `マズルフラッシュ01.dds` 闪光（蓝）。
- 粒子用 `edf41c::Particle`（ParticleObject，原先 `edf41robo.cpp` 的 4.1 炮口闪光，移进共用模块并带纹理参数）。

## 未验证 / 近似

- 没实机：粒子大小（光团 14、加农炮闪光 12、加特林 6、光照半径 30）与亮度（颜色 ×3）是估的；`+0x2E0` 每发减一是按
  `docs/feedback-2026-10-17-edf41-hector.md` 3.4 的读法，日志 `EDF41AI hector … shot #` 头两发可核对。
- 闪光是一张 `マズルフラッシュ01.dds` 广告牌粒子，不是 EDF6 的 `MuzzleFlash01` 类（那要复刻武器 +0x480 / +0x488 的参数块，见
  `src/firstperson.cpp` FlashCreateHook）；样子不对再做那一步。
