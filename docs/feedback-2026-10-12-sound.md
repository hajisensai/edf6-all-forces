# 2026-10-12 反馈：音效换成录音（`tools/sound_pack.py`）

## 用户原话

> 修改的音效重新修改，音效太烂了。可以使用 "C:\Users\wrds\Downloads\QQ\00_win32_audio.zip" 这个

## 问题在哪

插件替换掉的原版声音（地面载具引擎 / 主炮 / 机枪 / 装填、喷气机引擎、沙扎比，`docs/sound-re.md` §5 §9 §10）全部由 `src/vsynth.h` 当场**合成**。用 `vsound_check` 导出的合成音实测（本文末尾的分析方法）：

- 各口径炮声的频谱重心只有 27～90 Hz，2 kHz 以上的能量占比为 0.000，衰减 30 dB 只要 0.5～1.2 s：只有一团低频闷响，没有炮口的炸裂，也没有真实炮声那种 1～2.5 s 的滚动回响。
- 引擎怠速是周期噪声脉冲（重心 106～196 Hz），没有真实发动机的机械细节；喷气机是滤波噪声 + 正弦尖啸。

合成本身没有 bug（电平、接缝、频谱都满足 `vsound_check` 的检查），问题是**听感上的真实度**。插件早就支持用录音替换任一个声音（DLL 旁的 `EDF6VehicleCrew_veh_<名字>.wav`、`EDF6VehicleCrew_jet.wav`，`src/jetaudio.cpp` `MakeClipsNow` / `Start`），所以这次不改合成器，而是按用户给的素材包做一套录音。

## 为什么仓库里只有「配方」，没有声音文件

- 素材包是另一款游戏的音效（1327 个文件：`sound/music`、`sound/samples/...`），本仓库是**公开**的。把它们（或由它们裁剪出的文件）提交进来等于公开分发别人的音频。
- 所以只提交 `tools/sound_pack.py` 里的映射表和处理步骤；玩家（或打分享包时）用自己手里的 zip 现做：

```
python tools/sound_pack.py build "C:\Users\wrds\Downloads\QQ\00_win32_audio.zip" <输出目录>
python tools/sound_pack.py install <输出目录> <游戏目录>     # 游戏在运行时拒绝
python tools/sound_pack.py remove <游戏目录>                 # 全部撤回，回到插件自己的声音
```

- 工具直接读 zip 里的条目（不解压、不执行其中任何东西），每个用到的源文件记 SHA-256 写进 `sound_pack.json`。
- **逐条否决**：删掉 `Mods/Plugins` 里对应的那个 wav 就回到插件合成的那一个；或 `build ... --skip gun_near railgun_charge`。
- 先跑一次 `cmake --build build --target vsound_check && build\vsound_check.exe`，`build` 会把每段录音的响度对齐到它替换的那段合成音（混音电平当初是按合成音调的），默认读 `%TEMP%\edf6_vsound_check`，也可 `--reference` 指定；没有就只按插件给这一段的峰值上限归一。

## 处理（每段都一样）

素材转 48 kHz 单声道（多声道取平均）→ 按配方裁剪 / 变速（变速 = 音高和长度一起变）/ 叠层 / 低通 → **20 Hz 高通去直流**（个别录音有直流偏移，插件自己的合成音最后也是这一步）→
- 循环：取配方给的长度，切点放在该长度 ±5 ms 内相邻样本差最小处，再把后面 0.12 s 等功率交叉淡化到开头，保证无缝；
- 单发：去掉尾部低于峰值 -60 dB 的静音，末尾淡出、开头 2 ms 淡入；

→ 响度对齐到合成音（循环比整体 RMS，单发比最响的 0.4 s 的 RMS），但峰值不超过插件给这一段的 `kClipPeak`（`src/jetaudio.cpp`，工具直接从源码读这张表，名字、是否循环都以它为准）→ 16 位 PCM 单声道（插件 `ReadWav` 接受的格式）。

## 替换清单（请逐条否决）

**选择依据只有文件名 + 实测元数据（时长、声道、采样率）和频谱/包络（频谱重心、200 Hz 以下 / 2 kHz 以上占比、起音、衰减 30 dB 的时间、电平稳定度、连射的重复频率）。我听不到声音，没有任何一条是试听过的。**「响度 vs 原合成音」为负数的，是录音的峰均比比合成音高、被峰值上限压住了，游戏里会比原来小这么多分贝（可用 ini 的组音量补）。

| # | 插件声音（文件） | 选用的素材（`sound/samples/` 下） | 处理 | 时长 | 响度 vs 原合成音 | 理由（依据：文件名 + 实测元数据/频谱） |
|---|---|---|---|---|---|---|
| 1 | `jet`（EDF6VehicleCrew_jet.wav） | `engines/fa-18_hornet_steady_ext.wav` | 8–16.4 s<br>8 s 无缝循环 | 8.00 s | — | F/A-18 外部稳态录音，43 s 内电平起伏 ±2 dB（最稳的喷气机素材）；取 8 s 做无缝循环 |
| 2 | `engine_heavy_idle`（EDF6VehicleCrew_veh_engine_heavy_idle.wav） | `engines/engine_tank.wav` | 0.5–6.7 s、×0.7 速<br>6 s 无缝循环 | 6.00 s | -0.5 dB | 坦克引擎录音，8 s 电平几乎恒定（波动 0.20）；降 0.7 倍当怠速，满转时插件再升 2.3 倍≈录音的 1.6 倍 |
| 3 | `engine_heavy_load`（EDF6VehicleCrew_veh_engine_heavy_load.wav） | `engines/apc_engine_loop.wav` | 0.3–6.4 s、×0.7 速<br>5.5 s 无缝循环 | 5.50 s | -0.0 dB | 装甲车引擎循环（文件名即 loop），7.4 s 恒定，频谱比坦克怠速更亮（重心 1.4 kHz），当负荷层 |
| 4 | `engine_light_idle`（EDF6VehicleCrew_veh_engine_light_idle.wav） | `engines/engine_hummer.wav` | 0.2–3.4 s、×0.85 速<br>3 s 无缝循环 | 3.00 s | +0.0 dB | 悍马引擎录音（轻型车），3.5 s 稳定；降 0.85 倍当怠速 |
| 5 | `engine_light_load`（EDF6VehicleCrew_veh_engine_light_load.wav） | `engines/pickup_engine_02.wav` | 0.3–5.9 s、×0.85 速<br>5 s 无缝循环 | 5.00 s | +0.0 dB | 皮卡引擎录音，6 s 稳定（波动 0.21），当轻型车负荷层 |
| 6 | `mg_burst`（EDF6VehicleCrew_veh_mg_burst.wav） | `weapon_fire/4leg_mech_minigun_01_loop.wav` | 0.1–1.5 s、×0.984 速<br>1 s 无缝循环 | 1.00 s | +0.0 dB | 机枪连射循环素材，实测 12.2 发/秒（插件按 12 发/秒设计）；按 12/12.2 变速后取整 12 发做无缝循环 |
| 7 | `gatling_burst`（EDF6VehicleCrew_veh_gatling_burst.wav） | `test/minigun_fire.wav` | 原样<br>0.72 s 无缝循环 | 0.71 s | +0.0 dB | 转管机枪开火录音，0.87 s 电平恒定（波动 0.14）、无单发间隙，正是转管炮的连续嘶吼 |
| 8 | `burst_tail`（EDF6VehicleCrew_veh_burst_tail.wav） | `weapon_fire/machine_gun_burst.wav` | 0.28–1.54 s | 1.26 s | -1.5 dB | 机枪点射录音停火后的回响段（0.28 s 起：-19 dB 起逐渐衰减到 -42 dB），当停火回响 |
| 9 | `autocannon`（EDF6VehicleCrew_veh_autocannon.wav） | `weapon_fire/big_gun_01.wav`<br>`weapon_fire/sniper_shot_01.wav` | 原样<br>0–0.6 s、-8 dB | 0.81 s | -0.0 dB | 大口径单发（起音 75 ms 内 -9 dB、主体在 400 Hz 以下）+ 低 8 dB 的步枪弹爆裂补高频，当 20~40 mm 机炮 |
| 10 | `grenade_shot`（EDF6VehicleCrew_veh_grenade_shot.wav） | `weapon_fire/mortar_fire_short.wav` | 原样 | 0.50 s | -0.0 dB | 迫击炮发射录音：5 ms 起音、0.5 s 的空洞「嗵」、几乎没有 2 kHz 以上（0.3%），就是低膛压榴弹 |
| 11 | `gun_medium_near`（EDF6VehicleCrew_veh_gun_medium_near.wav） | `weapon_fire/105_MM_Cannon_Close.wav` | 原样 | 2.79 s | -0.0 dB | 105 mm 坦克炮近距录音（前 100 ms 有 870 Hz 重心的炮口炸裂，随后 2.8 s 低频轰鸣），对应 75~105 mm 档 |
| 12 | `gun_medium_far`（EDF6VehicleCrew_veh_gun_medium_far.wav） | `weapon_fire/105_MM_Cannon_Medium_Distance.wav` | 原样 | 2.79 s | -0.0 dB | 同一门 105 mm 炮的中距离录音（没有炸裂，低频滚动 2.8 s） |
| 13 | `gun_near`（EDF6VehicleCrew_veh_gun_near.wav） | `weapon_fire/105_MM_Cannon_Close.wav`<br>`weapon_fire/105_MM_Cannon.wav` | ×0.92 速<br>×0.92 速、-6 dB | 3.31 s | +0.0 dB | 120 mm：105 mm 近距录音降 0.92 倍（更低更长）+ 低 6 dB 的 105 mm 主录音加厚主体 |
| 14 | `gun_far`（EDF6VehicleCrew_veh_gun_far.wav） | `weapon_fire/105_MM_Cannon_Medium_Distance.wav` | ×0.92 速 | 3.04 s | -0.8 dB | 105 mm 中距离录音降 0.92 倍 |
| 15 | `howitzer_near`（EDF6VehicleCrew_veh_howitzer_near.wav） | `weapon_fire/105_MM_Cannon_Close.wav`<br>`explosions/explosion_generic_large_close_01_mono.wav` | ×0.8 速<br>-10 dB、延后 30 ms | 3.49 s | -0.0 dB | 155 mm：105 mm 近距录音降 0.8 倍 + 低 10 dB 的大型近爆（20 ms 起音、1.5 s 衰减）当大装药的冲击波 |
| 16 | `howitzer_far`（EDF6VehicleCrew_veh_howitzer_far.wav） | `weapon_fire/105_MM_Cannon_Medium_Distance.wav` | ×0.78 速 | 3.58 s | -1.2 dB | 105 mm 中距离录音降 0.78 倍 |
| 17 | `gun_heavy_near`（EDF6VehicleCrew_veh_gun_heavy_near.wav） | `weapon_fire/105_MM_Cannon.wav`<br>`explosions/explosion_generic_large_close_02_mono.wav` | ×0.72 速<br>-8 dB、延后 20 ms | 4.22 s | +0.0 dB | Titan 主炮：105 mm 主录音（主体最厚，-3 dB）降 0.72 倍 + 低 8 dB 的大型近爆 |
| 18 | `gun_heavy_far`（EDF6VehicleCrew_veh_gun_heavy_far.wav） | `weapon_fire/105_MM_Cannon_Medium_Distance.wav` | ×0.7 速 | 3.99 s | -1.4 dB | 105 mm 中距离录音降 0.7 倍 |
| 19 | `railgun_shot`（EDF6VehicleCrew_veh_railgun_shot.wav） | `weapon_fire/railgun_fire.wav` | 原样 | 1.31 s | -0.0 dB | 素材自带的电磁炮：前 0.2 s 470 Hz 的主体，随后电弧从 0.9 kHz 升到 4.6 kHz，没有火药轰鸣 |
| 20 | `railgun_far`（EDF6VehicleCrew_veh_railgun_far.wav） | `weapon_fire/railgun_fire.wav`<br>`weapon_fire/railgun_tower_shot_01.wav` | 低通 1500 Hz<br>-6 dB、低通 1500 Hz | 1.30 s | -1.4 dB | 同一发电磁炮滤到 1.5 kHz 以下 + 低 6 dB 的炮塔电磁炮低频轰响，远处听不到电弧 |
| 21 | `railgun_charge`（EDF6VehicleCrew_veh_railgun_charge.wav） | `weapon_fire/charging_player_weapon.wav` | 2–3.5 s | 1.50 s | -4.7 dB | 武器充能录音的 2.0~3.5 s：啸声重心从 2.2 kHz 一路升到 4.2 kHz，正好 1.5 s（插件在就绪前 90 帧播）；charging_power 只是 130 Hz 的低频嗡声，不像电容充电 |
| 22 | `railgun_ready`（EDF6VehicleCrew_veh_railgun_ready.wav） | `misc/beep_tech_05.wav` | 0–0.42 s | 0.42 s | -2.1 dB | 科技提示音的第一声（0.42 s），当电容充满 |
| 23 | `missile_launch`（EDF6VehicleCrew_veh_missile_launch.wav） | `weapon_fire/rocket_fire.wav` | 原样 | 1.08 s | +0.0 dB | 火箭发射录音：点火后 0.3 s 内重心从 0.5 kHz 升到 2.6 kHz 的喷射声，1.1 s |
| 24 | `rocket_rail_launch`（EDF6VehicleCrew_veh_rocket_rail_launch.wav） | `weapon_fire/rocket_fire_fast.wav` | 原样 | 1.03 s | -0.0 dB | 快速火箭发射录音：比导弹短（0.3 s 内衰减 20 dB）、更干脆，对应喀秋莎每根导轨一声 |
| 25 | `sazabi_beam_shot`（EDF6VehicleCrew_veh_sazabi_beam_shot.wav） | `weapon_fire/plasma_fire_8.wav` | 原样 | 0.93 s | -0.5 dB | 等离子射击：第一个 100 ms 重心 3.7 kHz 的电光，随后 1.5 kHz 主体和低频尾音 |
| 26 | `sazabi_beam_hit`（EDF6VehicleCrew_veh_sazabi_beam_hit.wav） | `weapon_hit/bullet_hit_plasma_01.wav` | 原样 | 0.59 s | -0.0 dB | 等离子命中：4.2 kHz 的灼烧爆裂 + 170 Hz 闷响，0.6 s（alien_laser_enemy_hit 实测是 5 s 持续嘶声，不用） |
| 27 | `sazabi_cannon_shot`（EDF6VehicleCrew_veh_sazabi_cannon_shot.wav） | `weapon_fire/railgun_tower_shot_01.wav`<br>`weapon_fire/plasma_fire_8.wav` | ×0.85 速<br>×0.7 速、-4 dB | 1.39 s | -3.1 dB | 胸部扩散炮：电磁炮塔的重型轰响降 0.85 倍 + 降 0.7 倍的光束电光，比步枪大得多 |
| 28 | `sazabi_funnel_shot`（EDF6VehicleCrew_veh_sazabi_funnel_shot.wav） | `weapon_fire/active_armor_laser_02.wav` | 原样 | 0.40 s | +0.0 dB | 细激光：0.2 s、重心 6~9 kHz，几乎没有 200 Hz 以下，比步枪细很多（浮游炮） |
| 29 | `sazabi_footstep`（EDF6VehicleCrew_veh_sazabi_footstep.wav） | `machines/mech_stomp.wav` | 原样 | 1.71 s | -0.0 dB | 机甲踏步录音：撞击的 1.4 kHz 金属敲击后接 50 Hz 的低频余震，1.7 s |
| 30 | `sazabi_land`（EDF6VehicleCrew_veh_sazabi_land.wav） | `machines/mech_stomp2.wav` | 原样 | 2.21 s | -3.1 dB | 另一段更长的机甲踏步（2.2 s，中段还有金属件的余振），当落地 |
| 31 | `sazabi_dash`（EDF6VehicleCrew_veh_sazabi_dash.wav） | `weapon_fire/power_wooshes_01.wav` | 原样 | 1.65 s | -0.0 dB | 能量冲刺「呼」声：0.1 s 升起、0.5 s 内衰减 20 dB |

### 保留插件合成的声音（素材包里没有这类录音）

| 声音 | 原因 |
|---|---|
| `tracks`（履带） | 素材包没有履带 / 链条录音（`mechanical_07` 是 4.5 kHz 的高频机械声，不像履带） |
| `turret` / `turret_stop`（炮塔电机） | 没有电机 / 伺服录音；`tower_*` 是 0.7 s 持续的机关运动，不是停止的一声咔 |
| `reload_*` / `case_*` / `brass`（装填、退壳、弹壳） | 没有炮闩、推弹、弹壳落地的拟音素材（`destructibles` 是玻璃 / 金属碎裂） |
| `engine_bike_*`（摩托） | 没有摩托 / 两缸发动机录音 |
| `rocket_rail_load`（喀秋莎上弹） | 没有合适的金属卡扣声 |
| `sazabi_saber_*` / `sazabi_whoosh` / `sazabi_funnel_launch` / `sazabi_funnel_dock` / `sazabi_thrusters` / `sazabi_cannon_charge` / `sazabi_shield_block` | 没有光剑 / 浮游炮出入 / 推进器循环 / 挡格这类素材 |
| `mark_own` / `mark_team` / `mark_off`（Q 标记提示音） | 界面提示音，不在「音效太烂」的范围里，没动 |

## 验证

- `python tools/sound_pack.py build <真实 zip> <输出>`：31 个文件全部写出，`check` 0 个问题（插件 `ReadWav` 能读、峰值不超 `kClipPeak`、无直流、循环接缝的跳变不大于循环内 99.9% 的相邻差，与 `vsound_check` 同一判据）。
- `tests/sound_pack_test.py`（ctest `sound_pack`，8 个用例）：用生成的替身素材（16 位立体声 / 24 位 / 浮点 / 22～48 kHz）走完整 build + check，不依赖受版权保护的原包；变异实测：去掉去直流那一步，3 个用例变红。
- 成品对比合成音（同一分析方法）：炮声衰减 30 dB 的时间 0.5～1.2 s → 1.2～2.5 s；机炮 2 kHz 以上占比 0 → 0.10；光束步枪重心 39 Hz → 1.1 kHz。

## 未验证 / 缺口

- **没有试听，也没有进游戏**：全部听感（每段像不像、音量平衡、引擎变调到 2.3 倍时是否尖、机枪循环和实际射速的配合）都要用户在游戏里听。
- 没装进用户的游戏：等 PR 合并后打分享包时 `build` + `install`，或用户自己运行上面的命令。
- `_jet.wav` 存在时插件不再合成涡轮尖啸（`jetaudio.cpp Start`），喷气机只剩这一段轰鸣的音高 / 音量变化。
- 机枪循环素材 12.2 发/秒，按 12/12.2 变速后循环刚好 12 发；加特林素材没有可测的单发周期（连续嘶吼），按插件 20 发/秒的假设播放。
