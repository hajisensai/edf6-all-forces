# 载具炮声：战雷分层、距离混音与环绕声道（2026-11-26）

## 用户原话

- 「烟不够劲，让他抄抄战雷的」「还有炮声呼啸声什么的」「怎么做的好怎么来。提交没事」
- 「分层和距离混音，环绕声道之类的记得做好」
- 环绕声道：「这个如果麻烦的话就算了，不麻烦就实现吧」
- 「现有的喷气机、车辆声 该优化就优化，做的并不是很好」

本文只讲声音。烟的部分不在这一段。

## 素材

- 来源是 Gaijin 官方给 mod 作者的 FMOD 工程 `GaijinEntertainment/fmod_studio_warthunder_for_modders`，提交 `07fd12ef`。处理脚本是 `tools/wt_sound.py`，产物在 `assets/sound/wt/`：192 个 WAV，共 65.27 MiB（上限 80 MB），另有 `SOURCE.md`。用户允许把它们提交进仓库。
- 处理方法如下：
  - 转成 48 kHz 16 位 PCM，保留原声道数（低频层、远处层、掠过声、炮塔和装填混成单声道，清单见 `wt_sound.MONO`），加 20 Hz 高通。
  - **不逐个文件拉响度**。FMOD 事件里的 dB 值是按素材之间原本的相对电平定的。只在整套素材的峰值超过 0.98 时，所有文件统一乘同一个增益，本次是 0.680167。
  - 开头不切，同一事件的各层按开头对齐。尾部低于 -60 dBFS 的部分切掉。循环（`turret_loop`）整段保留，不切尾不淡出。
- 安装器把素材放进 `Mods/Plugins/EDF6VehicleCrew_wt/<组名>_<i>.wav`。插件启动时逐组读取，缺任何一个文件就记一行 `SOUND vehicles: N War Thunder recording(s) missing ... the install is broken`，缺文件的那一组不出声。没有回退到旧音效的分支。
- `python -I tools/wt_sound.py --check` 检查登记表和已提交文件是否一致，不需要源工程，由 ctest 跑。

## 分层事件（`src/sound_event.h`，离线检查 `tools/sound_event_check.cpp`）

一次开炮是一个事件，由若干层组成。每一层包括：录音组、层增益、增益随距离的曲线、低通截止频率随距离的曲线、相对开炮的起始延迟、是否只送 LFE。

`Hear(e, d)` 计算每层的线性增益：事件增益 + Premaster 曲线 + 层增益 + 层的距离曲线。低于 -60 dB 的层丢弃。

`vehsound.cpp` 的 `PlayEvent` 负责播放：
- 每层的变体从本组里轮换（`Rotation`），不会连续两次用同一个。
- 每层延迟「距离 / 声速 + 层自身起始」后播放。
- 扩散度按事件的 sound size 计算，多普勒只取一半。

各口径对应的事件：
- 75–105 mm 炮（`Bore::medium`）：`kMedium`，用 75 mm KwK 40。
- 120 mm 坦克炮（`tank`）：`kTank`，用 122 mm D-25T。
- 超重炮（`heavy`）：`kHeavy`，用 128 mm PaK 44。
- 榴弹炮（`howitzer`）：`kHowitzer`，用 150 mm sFH 18。
- 电磁炮、火箭、导弹、机关炮、榴弹发射器：仍用原来的合成声，WT 工程里没有对应素材。

机枪（`Bore::mg`）每发子弹播一次 `round` 事件（近、远两个 grain 同时播），停火时播一次 `last` 尾音。射击间隔少于 4 帧的用 MG42 素材，其余用 M2。原来的连射循环和 `kClipBurstTail` 不再用于机枪。弹壳雨循环保留。

联机：别的机器开的炮仍由 `vmix::RemoteShot`（`+0x1544` 计数）判定为开火，走同一个 `PlayEvent`，所以远端开炮也是分层声。

### 数值来源

**取自 FMOD 元数据**（`*_ai` 事件，即别人开炮时听到的版本）：
- 事件 master：75 mm +2.5，其余 +2。
- Shot 模块：轨道 +3，曲线 0 m +6 → 700 m -80，模块 master -6。各层：
  - Audio1：75 mm +3，其余 +6。
  - middle：75 mm -3，其余 0。
  - body：0，122 mm 从 0.6 s 起，128/150 mm 从 0.8 s 起。
  - EQ 截止：0 m 22 kHz → 1000 m 20 Hz。
- Bump：75 mm 轨道 +9、master -2；大口径轨道 +6、Audio1 +3。曲线 0 m -80 → 98.39 m 0。
- Close：+3，曲线 0 m +8 → 700 m -80，EQ 与 Shot 相同。150 mm 的轨道 +10。
- Far：+6，曲线 0 m -80 → 93.22 m +8 → 2000 m -80。150 mm 的轨道 +4。
- Premaster：0 m 0 → 500 m -6 → 2000 m -80。总线 cannons -6。`weapon_ai` 及以下的总线没算。
- Spatialiser：sound size 14 m，最小扩散 40°，多普勒 50 %。
- 玩家自己开炮的事件，层值按 player frame 计（75 mm 加 +6，大口径加 +9）：
  - bass：75 mm -10，122 mm -8（从 0.2 s 起）。
  - metal：0 m → 10 m 为 -5/-15（75 mm，从 0.2 s 起）、-2/-12（122 mm，从 0.15 s 起）。
  - 榴弹炮机构声：-20/-24（从 0.45 s 起）。
  - tail：75 mm -3（从 0.5 s 起），122 mm -6（从 0.6 s 起）。
- 机枪：
  - close：-12，曲线 0 m +4 → 300 m -80，EQ 22 kHz → 2.6 kHz@200.565 m。
  - far：M2 -16、MG42 -18，平 +10，EQ 22 kHz → 3.5 kHz@1 km → 2.6 kHz@2 km。
  - Premaster +5，曲线 0 m +2 → 500 m -4 → 2000 m -80。尾音再 -6，MG42 的 last_far 额外 +6。
  - 总线 mguns +6。grain 长 0.105 s，所以每发一个 grain。

**我们自己定的（经验值）**，都在 `sound_event.h` 里标了 `Ours`：
- `kTrimDb = -6`：整体余量，让近处最响的层约在满刻度。
- 近距层的淡出距离。WT 只给玩家自己的炮播这些层，而且只在 0 m 处：bass 到 150 m 淡完，metal 和机构声到 40 m 淡完。
- tail 在任何距离都播，不随距离衰减（开阔地的回声远处也听得到）。
- `sub` 层：WT 没有这一层（它的 spatialiser 把整个事件以 -18 dB 送 LFE）。我们用 cannons_sub 录音单独送 LFE，-6 dB，曲线 0 m 0 → 300 m -12 → 1000 m -80。
- WT 不做滤波的层（Bump、Far、bass、metal、tail）加空气低通：0 m 16 kHz → 2500 m 1.2 kHz。
- 截止频率在两个曲线点之间按几何插值（Hz 取对数）。FMOD 编辑器里的插值方式没核实，这是假设。
- **500 m 以外**（`kHold`）的处理：保持 500 m 处 WT 的混音比例，整体按 1/d 衰减（每倍距离 -6 dB），低通仍按真实距离算。原因：WT 的 Premaster 加 Far 曲线到 2000 m 是 -80 dB，大约 900–1000 m 外就几乎听不到。这适合战雷的战场，但 EDF 的交战距离到 2500 m（`vmix::kFarAt`），1 km 外的炮应该还能听到。
- 128 mm 和 150 mm 的近距层沿用 122 mm 的值。75 mm 没有 body_mid 层，素材里没有。
- 机枪按射速选 MG42 / M2（WT 没有加特林），阈值 4 帧。机枪的 spatialiser 抄 cannons 的，因为元数据视图里没有机枪事件的 spatialiser。
- FMOD 的 panBlend 没复刻。

## 环绕声道（`src/surround.h`，离线检查 `tools/surround_check.cpp`）

- 按主输出声道的 channel mask 建扬声器布局：2.0 / 2.1 / quad / 5.1 / 7.1。设备没报 mask 时按声道数取常见布局。
- 点声源在左右相邻的两个扬声器之间做等功率平移。扩散度大（近处、体积大）时向所有扬声器均摊，总功率不变。
- LFE 只接收标了 `sub` 的层，其余层一点都不送。没有 LFE 声道时，`sub` 层均摊到所有扬声器，不丢。
- 立体声录音当成两个声源，按扩散度分开。
- 立体声输出时，后方有一段 300° 的空档，按声源的横向分量折算，正左、正右都是满偏。
- 喷气机、沙扎比、Q 标记、终极技等所有走 `jetaudio` 的声音都用同一套矩阵。

## 播放端（`src/jetaudio.cpp`）

- 低通改为一极滤波。XAudio2 的 SVF 低通在 rate/6 以上就封顶了，近处会发闷。
- 一次性声音的 voice 从 64 个加到 160 个，因为一发炮最多 11 层。`vehsound` 的待播队列从 96 加到 256。
- 主输出挂了 XAPOFX 的 `FXMasteringLimiter`（默认 release 和 loudness），防止多门炮的层叠加后削波。启动日志 `SOUND XAudio2 up: ... limiter on` 显示是否挂上了。

## 爆炸（`src/blastsound.cpp`，ini `ExplosionVolume`）

- 接管点：原版每发炮弹的爆炸声是它自己的 AmmoHitSe 预设（core+0xA70），由 0x7B4510 播放。8 个调用点（命中 0x2325EE、到时 / 定时引爆 0x23FB84 / 0x264BEF / 0x28E9CD、SolidExp 和炸弹 0x297283 / 0x297D6D / 0x297FA3、尾跳 0x233BCA）改指 `ExplodeSe`。有爆炸的弹（core+0xAF4 的 0x10、爆炸半径 core+0x788 / AmmoExplosion core+0xA20 大于 0）不放原版声，改放我们的；普通子弹、空预设、模块没武装（关掉、音量 0、声音没做好、这一帧没有监听者）、出错都走原版。不会出现「原版被拿掉但没有替代」或「两份都响」。
- 分档按爆炸半径：< 3 m `kBlastSmall`（explosion_ground_20_37），3–8 m `kBlastMid`（explosion_ground），8–16 m `kBlastBig`（art_explosion_ground），≥ 16 m `kBlastHeavy`（bomb_explosion_ground）。各层的增益、距离曲线、起始延迟、Premaster（到 5000 m）、spatialiser 的 sound size / min extent 都抄 WT 元数据，数值逐条写在 `sound_event.h` 的注释里。
- 标「ours」的（WT 元数据里没有，自己定的）：远处回声尾巴 `ex_tail`、只送 LFE 的隆隆 `ex_sub`（WT 是把整个事件按 -12 / -8 dB 送 LFE）、距离 EQ 用低通近似、重磅档的 master 0、小档和重磅档的 voice 上限、各档 `life`。
- 声音放在爆炸点，按 距离 / 340 m/s 延迟，每档有 voice 预算（最多 N 个在 life 秒内起播、cooldown 内不再起）。

## 掠过与来袭（`src/blastsound.cpp` `Flyby`，ini `FlybyVolume`）

- 每帧在游戏线程遍历一次子弹系统的链表（`*(EDF+0x20B32F0)`，哨兵 `*(sys+8)`，节点 +0x10 是 core；来源 0x704EB0 → 0x237CF0，`docs/bullet-pass-re.md`）。读 core 的位置 +0xB80、速度 +0xB90、加速度 +0xBA0、寿命帧 +0xAF8、爆炸半径；按 vtable 分子弹 / 炮弹 / 下落弹 / 炸弹。一帧最多走 4096 个。
- **掠过**（WT `bullet_passby` / `shell_whiz`）：算出这一帧内到达最近点、且最近距离 ≤ 120 m 的弹（已飞不到 30 m 的不算，免得自己开枪就响）。按爆炸半径分四个口径：步枪弹（无爆炸）、< 3 m 轻、3–8 m 中、≥ 8 m 重，对应 WT 的 0–31 / 31–65 / 65–140 / 140–400 mm。每档最多三四层：crack（超音速爆裂，≥ 600 m/s，中口径 ≥ 400 m/s）、bypass、whiz，中重口径另加 shell_whiz。层组增益抄 WT：crack +6、bypass 0、whiz −14、shell_whiz −7.26；shell_whiz 的距离曲线 0 m 0 / 10.5 m −6 / 20 m −80 抄 WT。每层的触发概率抄 WT（0.5 / 0.705 / 0.695 / 1）。预算：maxVoices 4、cooldown 50 ms（WT）、life 1.5 s（ours）。在最近点播放，延迟 = 到最近点的时间 + 距离 / 340。
  - ours：crack / bypass / whiz 的距离曲线 `kMiss`（0 m 0 / 20 m −6 / 120 m −30；WT 的 passby 曲线是按它自己的 3D 衰减，这里没有）、每档各层的层增益（表在 `sound_event.h` `kPassRifle` .. `kPassHeavy`）、口径按爆炸半径换算。
- **来袭**（WT `art_shells_incoming` / `bombs_incoming`）：下落陡（竖直速度 < −0.5 × 水平速度）、有爆炸半径（炸弹不要求）的弹，按当前速度和加速度解二次方程算落到耳朵高度的时间和落点；落点在 300 m 内、离落地不到 2.3 s（炮弹）/ 2.0 s（炸弹，= 素材长度）时播一次。电平：master +6、模块 +4、组 +6、art bus −16（炸弹另 +6 GainEffect）；距离曲线 0 m 0 → 600.28 m −80，EQ 22 kHz → 103.1 m 12 kHz → 400 m 1.6 kHz，按落点距离算；70 % 概率；maxVoices 炮弹 4 / 炸弹 1，cooldown 200 ms，都抄 WT。同一发 4 s 内只判一次。声音放在弹当前位置。
  - ours：300 m 的判定半径、陡度阈值、提前量用素材长度。

## 炮塔与装填（`src/jetaudio.cpp` `kWtFor`）

- 原来合成的 `turret`（炮塔转动循环）、`turret_stop`、`reload_breech_open`、`reload_eject`、`reload_load`、`reload_shell_ram`、`reload_close` 换成 WT 录音：`turret_turn_loop_1`、`turret_klank_01..03`、`reloading_open` / `reloading_eject_shell` / `reloading_insert_projectile`（推弹和分装的推弹丸共用）/ `reloading_close` 各 4 个变体。
- 这些 clip 不是 FMOD 事件，没有 WT 的 dB 可抄；载具的混音是按各 clip 的 `kClipPeak` 定的，所以一组按一个增益拉到那个峰值（ours）。单发轮流用变体，循环用第一个（插件拷一份已调好电平的）。
- 玩家自己放的 `EDF6VehicleCrew_veh_<名字>.wav` 仍然优先。组没读到时（启动日志那一行 missing）这个 clip 不出声，不回到合成音。
- 时机、音量组（`VehicleTurretVolume` / `VehicleReloadVolume`）、转速调音高都不变，ini 键的含义不变。
- `reload_charge`（装药模块）、`reload_primer`（底火）WT 素材里没有对应的，仍是合成音。

## 联机

- 远端玩家开炮：沿用第一段的路径（`vehsound` 按收到的射击计数补射击声，走同一套事件）。
- 爆炸、掠过、来袭：远端玩家的弹在本机是游戏自己的副本，走同样的命中 / 引爆调用，也在同一条子弹链表里。所以听到的一样，不加任何消息。

## 没做的（引擎类）

用户也说了喷气机和车辆声做得不好。炮声、爆炸、掠过 / 来袭、炮塔和装填已换成 WT 素材，下面这些没做：
- 喷气机按转速分层、地面载具引擎的 rpm / load 分层 loop：**给 mod 作者的 FMOD 工程里没有引擎录音**。元数据引用了 `/engines_new/jet` 等，但 Assets 只有 common / explosion / explosive / fx / tanks / weapons（15,783 个 wav，没有引擎）。没有素材就换不了，引擎仍是合成音。
- 装甲命中 / 跳弹声：没有可靠办法区分「打在装甲上」和「打在地面 / 建筑上」，没做。

## 没验证的

- 全部只做了离线检查，没在游戏里试听：各口径的音量平衡、近距层淡出距离、500 m 外 1/d 衰减的手感、机枪 grain 的连贯性。
- 真实 5.1 / 7.1 设备上的方位，以及 `FXMasteringLimiter` 在用户机器上能否挂上（看日志）。
- 联机时远端开炮、爆炸、掠过的覆盖：只确认了代码路径相同，没实测。
- 爆炸 hook 的 8 个调用点、子弹链表的偏移都只做了静态核对（vanilla.py 反汇编），没在游戏里跑过；掠过的触发频率、来袭提前量的手感、炮塔 / 装填按峰值对齐后的响度都没试听。
- `turret_turn_loop_1` 的首尾接缝只看了样点连续（首 953、尾 1346，同向），没试听。
