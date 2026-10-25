# 2026-10-25 反馈：四联装火神炮停火后开火声还在

用户原话：

> 停火的时候这个四联装火神炮的开火声音还是有

范围：只查开火声的生命周期。不碰转管动画、模型、相机、HUD。

## 根因（已验证：生成器源码 + ACB 元数据 + EDF.dll 静态读取）

1. **武器文件的开火声配置错了**。`tools/make_tracked_combat.py gun_sgo`（2026-10-19 `aaf35f9f` 起）把 M163 / 四联装 / 其它同类火炮的 `FireSe` 整条换成
   `[0.0, 'weapon_VHC_heli302_gatling', 0.55, 1.2, 1.0, 50.0]`，注释写的前提是「每发是有限长的一次性 cue」。
2. **这个 cue 没有终点**。`TIKYUUX_SE.ACB` 里 `weapon_VHC_heli302_gatling` 的 `Length = 0xFFFFFFFF`（`pylib/acb.py cue_lengths`，无限 / 未知长度）。原版所有无限长 cue 都以循环 owner 模式播放（`FireSe[0] = 1`）：`V603_FLAK_GUN01_*`、`V_403TANK_MACHINEGUN`、`V_503_BIKE_GUN_*` 全是；
   原版的一次性模式（`FireSe[0] = 0`）只配有限长的 cue。本轮对游戏 `Root.cpk` 全部车载武器的扫描没有反例。
3. **原生只给「循环 owner」留停止句柄**（`EDF.dll`，TimeDateStamp `0x678CCB46`）：
   - `0x698005..0x69804B`：preset `+0x78`（循环标志）非零时，句柄 `+0xE28` 没在播就开声，并把保持计数 `+0xE20 = FireInterval + 4`（`+0x36C`，`0x698056..0x69809B`）。
   - `0x693FD3..0x69400B`：武器每帧更新，扳机（`+0x13A`，由 `+0x139` 锁存在 `0x6935AB` 搬来）松开后 `+0xE20` 倒数，到 0 用 `0x7A8CD0(handle, 8)` 渐出停声。
   - `+0x78 == 0`（`0x6980CC..`）：每发起一个新声音，句柄放进 `+0xE38` 的列表，**不走 `+0xE28`，没有任何地方会停它**。
   所以四联装（`FireInterval = 1`、每帧两列各一发）每秒向 `weapon_VHC_heli302_gatling` 开 120 个永远不会结束的声音；松开扳机后一个都不停。同一份生成器给出的 M163、CIWS、通古斯、BMPT、GDF-005 都是这个配置。
4. **为什么有时听不到**：`src/vehsound.cpp` 在插件接管该武器时会把武器自己的 `FireSe` cue 清空（`Keep`/`Hold`，`0x7B4510` 遇空 bank 什么也不播），原版那条路被吃掉。所以只要插件的载具声音没接管（`VehicleSound=0`、对应组音量 0、合成音还没准备好 / 监听者还没放好的那几帧、插件的 `Enabled=0`，或者这把武器在它被登记前就开过火）原版那条无主声音就响，且之后不会停。

## 修复

- `fire_se(interval, pitch)`（`tools/make_tracked_combat.py`）按武器射速选原版的生命周期：
  - `FireInterval <= 5`（与 `src/vehmix.h kRapidFrames` 同一口径，插件也把它当连射）→ 循环 owner 模式（`FireSe[0] = 1`），仍是 `weapon_VHC_heli302_gatling`：原生开声一次、每发续期、松扳机后渐出停声，与原版 M163 同一路径。M163、四联装（HE 与 AP 两条弹链）、CIWS、通古斯。
  - 更慢的（BMPT 12 帧、GDF-005 6 帧）→ 一次性模式配**有限长**的 Striker 机炮 cue `weapon_VHC_striker401_cannonTekkoRapid`（2192 ms，原版 `V_401STRIKER_CANNONS02` 10 帧一发的同款）。同属这个根因（无限长 cue 当一次性播）。插件对这两种车的分类不变（> 5 帧、非循环 → 机炮「每发一声」）。
- 插件侧分类不受影响：M163 / 四联装 / CIWS 的 `FireInterval ≤ 5`，改前改后 `vmix::ProfileOf` 都是「连射」。

## 验证

- 离线：`tests/test_tracked_combat.py FireSoundTests`：对 `tools/make_tracked_combat.py` 所有种类生成的每个武器文件，读游戏 `SOUND/PC` 全部 ACB，断言「`FireSe` 的 cue 无限长 ⇒ 循环 owner 位为 1」；变异（改回 `FireSe[0] = 0`）实测被杀。`fire_se` 的射速分界另有单测。
- 没有启动游戏、没有听。**未实机项**：
  1. 四联装停火后原版声音确实消失（需要重新运行安装器重生成武器文件；已装的旧武器文件仍是一次性配置）；
  2. 循环模式下四联装的听感（音量 0.55、音高 1.2 是沿用旧值，原版 M163 的是 0.6 / 1.0 / 0.8）；
  3. BMPT / GDF-005 换成 Striker 机炮 cue 的听感；
  4. `VehicleSound=0` 时（插件不接管）停火 → 原生 `+0xE20` 倒数停声的全过程。

## 不确定点

- `0x697FE4` 前的另一条路（`rdi` 非空且 `[rdi+0x50] != 0` 时用 `[rdi+0x30]` 的 preset）：`0x696FD0` 的 13 个调用者里只有 2 个传非空（`0x69FDC6`、`0x6AF9A8`），没有确认它们是不是这些车载炮的类；`sound-re.md` §9.4 早有同一条未决。若车载炮走这条路，插件清不到那份 preset。本轮没有证据显示这些武器走它，未处理。
- 本轮未验证旧存档 / 已安装的武器文件的升级路径之外的缓存：`pylib/buildcache.py` 按生成器源码哈希失效，重装即重生成（见 `tests/test_buildcache_module_identity.py`）。
