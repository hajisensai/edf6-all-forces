# 载具炮声：战雷分层、距离混音与环绕声道（2026-11-26）

## 用户原话

- 「烟不够劲，让他抄抄战雷的」「还有炮声呼啸声什么的」「怎么做的好怎么来。提交没事」
- 「分层和距离混音，环绕声道之类的记得做好」
- 环绕声道：「这个如果麻烦的话就算了，不麻烦就实现吧」
- 「现有的喷气机、车辆声 该优化就优化，做的并不是很好」

本文只讲声音。烟的部分不在这一段。

## 素材

- 来源是 Gaijin 官方给 mod 作者的 FMOD 工程 `GaijinEntertainment/fmod_studio_warthunder_for_modders`，提交 `07fd12ef`。处理脚本是 `tools/wt_sound.py`，产物在 `assets/sound/wt/`：299 个 WAV，共 110.94 MiB（上限 120 MB），另有 `SOURCE.md`。其中引擎 65 个、39.49 MiB（引擎单独上限 40 MB），来自战雷游戏自带的 `sound/aircraft_engines.assets.bank` 和 `sound/tanks_engines.assets.bank`（vgmstream 解出，给 mod 作者的工程里没有引擎录音）。用户允许把它们提交进仓库。
- 处理方法如下：
  - 转成 48 kHz 16 位 PCM，保留原声道数（低频层、远处层、掠过声、炮塔和装填混成单声道，清单见 `wt_sound.MONO`），加 20 Hz 高通。
  - **不逐个文件拉响度**。FMOD 事件里的 dB 值是按素材之间原本的相对电平定的。只在整套素材的峰值超过 0.98 时，所有文件统一乘同一个增益，本次是 0.680167。
  - 开头不切，同一事件的各层按开头对齐。尾部低于 -60 dBFS 的部分切掉。循环（`turret_loop`）整段保留，不切尾不淡出。
  - 引擎另成一套：全部混成单声道，整套一个增益（0.668010，峰值 0.9），不逐个拉响度；循环把末尾 100 ms 等功率交叉淡入开头，首尾无缝（`--check` 查接缝和 40 MB 上限）。
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

## 命中装甲 / 跳弹 / 未击穿（`src/blastsound.cpp` `HitSeen`，ini `ExplosionVolume`；逆向见 `docs/hit-sound-re.md`）

- 接管点：每发弹的命中都由 0x230CA0 记录（参数里有被击物体的 body id、命中点、面法线）。4 个调用点（飞行步 0x2321B0 的 0x23238F / 0x23256D，首帧扫掠 0x233CB0 的 0x23403B / 0x234210）改指 `HitSeen`：先跑原版记录，再用 0x108260 取物体，按 RTTI 层级查装甲白名单（载具、机器人及其装备、运输船、移动要塞、UFO / 母舰 / 时间船及其部件、德洛伊、无人机、轰炸机、发电塔、哨戒炮、诱饵等 35 个类，按 vtable 缓存）。
- 普通弹（SolidBullet01 / Rail，或没有爆炸半径的 SolidExp / Grenade）打到装甲：
  - 按类定口径：SolidBullet01 是子弹，其余是重弹（EDF 没有口径字段）。
  - 按 AmmoIsPenetration（core+0xAF4 0x100）定击穿；按入射角定跳弹：|cos| < 0.26（偏离法线约 75°，ours）且没击穿。
  - 子弹 → `hit_mgun`（跳弹 `rico_mgun`）；重弹 → `hit_big_unpierce`（跳弹 `rico`，击穿 `hit_pierce`）。
  - 飞行步里命中后，同一次调用会在 0x2325EE 放原版 AmmoHitSe（一帧一次；条件 0x400 置位、0x20 清、击穿 0x100 清或带 0x4、core+0xB04 ≠ 3）。`HitSeen` 按同一条件给这发弹打标，`Take` 见标就拿掉原版那一声：原版命中声和我们的只响一个。首帧扫掠那两处后面没有原版命中声，只放我们的。
- 爆炸弹打到装甲：爆炸照旧（kBlast*），按档在上面叠 `hit_small_unpierce`（小档）/ `hit_aphe_mid`（中档）/ `hit_aphe_big`（大档、重磅档）的冲击层和远处层。
- 数值抄 WT：hit_mgun master −4、tank_hit_bullet −6 +10、0 m 0 → 500 m −80、0–200 m 触发、maxVoices 4；rico_mgun +3.84 半音、bullet_whiz 曲线 0 / 11.7 m −3.9 / 23.5 m −11.2 / 60.9 m −16.3 / 98.4 m −80；unpierce gain −22、冲击层 −5 +10、0 m +6 → 23.8 m −2 → 47.7 m −11.6 → 124 m −16.8 → 200.8 m −80、dull 层 0.18 / 0.2 s 起、−6 半音；hit_big master +6 gain −16、冲击层 0 → 99.5 m −2.9 → 500 m −80、远处层 0.19 s 起 −80 → 100 m +10 → 500 m −80；spatialiser 4 m / 80°；maxVoices 3。
  - ours：EQ 用低通近似（22 kHz → 500 m 4 kHz）、跳弹角阈值、rico 两层的距离曲线、pierce 借用 unpierce 的数值（WT 的 hit_ap 是车内玩家自己被击穿的声音）、各类的 cooldown 和 life。
- 不接管的：没有物体（打地面、建筑：原版地面材质声照旧）、物体不在白名单（虫、人、步兵）、激光 / 火焰 / 导弹等别的弹类、模块没武装、出错。被击目标自己的受击声（UFO 0x52E110 / 0x5038D0、移动要塞 0x4C60E0 在伤害够大或死亡时放自己的预设）是目标的反应，不是弹的命中声，保留。

## 炮塔与装填（`src/jetaudio.cpp` `kWtFor`）

- 原来合成的 `turret`（炮塔转动循环）、`turret_stop`、`reload_breech_open`、`reload_eject`、`reload_load`、`reload_shell_ram`、`reload_close` 换成 WT 录音：`turret_turn_loop_1`、`turret_klank_01..03`、`reloading_open` / `reloading_eject_shell` / `reloading_insert_projectile`（推弹和分装的推弹丸共用）/ `reloading_close` 各 4 个变体。
- 这些 clip 不是 FMOD 事件，没有 WT 的 dB 可抄；载具的混音是按各 clip 的 `kClipPeak` 定的，所以一组按一个增益拉到那个峰值（ours）。单发轮流用变体，循环用第一个（插件拷一份已调好电平的）。
- 玩家自己放的 `EDF6VehicleCrew_veh_<名字>.wav` 仍然优先。组没读到时（启动日志那一行 missing）这个 clip 不出声，不回到合成音。
- 时机、音量组（`VehicleTurretVolume` / `VehicleReloadVolume`）、转速调音高都不变，ini 键的含义不变。
- `reload_charge`（装药模块）、`reload_primer`（底火）WT 素材里没有对应的，仍是合成音。已在 War Thunder 全部 bank（`D:\Steam\steamapps\common\War Thunder\sound\*.bank`，含 masterbank.strings）按名字扫过 primer / propellant / powder / charge：只有步兵语音的 melee_charge、海战的 depth_charges 和 KV-220 引擎的 charger loop，坦克装填只有 open / eject / insert / close 和若干枪械的 reload。

## 联机

- 远端玩家开炮：沿用第一段的路径（`vehsound` 按收到的射击计数补射击声，走同一套事件）。
- 爆炸、掠过、来袭：远端玩家的弹在本机是游戏自己的副本，走同样的命中 / 引爆调用，也在同一条子弹链表里。所以听到的一样，不加任何消息。
- 喷气机引擎的油门杆（`src/jet_lever_net.cpp`，线格式与节流 `src/jet_lever_protocol.h`，离线测试 `tests/jet_lever_net_test.cpp`）：
  - 已注册的喷气机（原版任务里 NPC 开的、玩家开的）只有一台机器在跑（`OnlineRunsHere`），别的机器上是原生同步的副本，没有插件飞控，也就没有油门杆。由跑它的那台机器把杆发出去：NPC 的取飞控的杆（`wing_engine.h`），玩家的取油门。插件自己造的未注册副本（呼叫来的战机）每台机器各自飞、各有各的杆，不发。
  - 通道 `AFJL`（`plugin_net.h` 注册表，`support_net.cpp` 注册），状态消息（`StateStream::jetLever`，键 = 发送方的槽位；不可靠、只留最新）。44 字节：魔数、版本、喷气机的 NetId、杆量化成 1 字节（保留加力：> 0.95 的杆量化后仍 > 0.95）、标志（旋翼 / 停着 / 没人开）。
  - 发送节流：杆变化 ≥ 6/255 且距上次 ≥ 100 ms 才发；加力点燃 / 熄灭、标志变化立刻发；不变时每秒一次心跳。新成员进房时下一帧全部重发。3.5 s 没收到就当没听到。
  - 收方只认跑它的那台机器发来的：座位 0 是玩家就要求是那个玩家的机器，否则必须是房主。
  - 收方的引擎声（`jetsound.cpp`）、加力和尾焰（`JetFlames`）都读同步来的杆。原来按速度和加速度估油门、≥ 240 m/s 开加力的那段已删掉。没收到杆的有人坐的喷气机只放怠速，不估。
  - 房间协议升到 22（`mod_room.h`），旧版本进不了房。

## 引擎（`src/sound_engine.h`，离线检查 `tools/sound_engine_check.cpp`；播放 `src/jetsound.cpp`、`src/vehsound.cpp`）

原来插件合成的喷气机轰鸣 / 涡轮啸叫（含 `_jet.wav` 替换）、坦克 / 轻车 / 摩托引擎的 idle / load 两层和履带声全部删掉，换成战雷的分层引擎。不叠加：两套不会同时响。ini 的 `JetSoundVolume`、`VehicleEngineVolume` 含义不变，仍是这两类的音量。

**模型**：一个引擎是一个事件，事件由若干组组成，每组是若干条循环录音（每条一个 XAudio2 声道，`jetaudio` 的 `OpenWtLoop`）。
- 每层在自己的参数（rpm、负荷、加力、车速）区间内交叉淡入淡出，音高随参数走（FMOD autopitch）。
- 组的增益随距离、指向锥角（声源正前 0°、正后 180°）和参数变化；组可以有随锥角 / 参数的音高。
- 参数越过某点时放一次性声：柴油的负荷切换、涡轮泄压、汽油的轰油门 / 收油、加力点火、履带停下、轮子起步。
- 层的增益高于 -60 dB 才开声道，低于 -66 dB 才关（不抖）。

**数值来源**：`GaijinEntertainment/fmod_studio_warthunder_for_modders` 的元数据——
- 喷气机：`Master/aircraft/engines/engine05_ej200_WEP`（自己飞的）、`engine05_ej200_WEP_ai` 与嵌套的 `engine_jet_far_ai`（别的飞机）。组：后向（idle / max）、前向（idle / max）、加力、涡轮啸叫（5 层），AI 另有远处低频 / 高频层与接近声。
- 地面：`Master/ground/engines/abrams`（燃气轮机）、`leo2a5`（柴油）、`m8`（汽油），`tracks_heavy` / `tracks_wheels`（自己的履带 / 轮子），`tracks_ai_heavy` / `tracks_ai_wheels` 与 `ai/abrams`、`ai/leo2a5`、`ai/m8`（别人的）。
- 抄的是各 sheet 的区间与淡入淡出、乐器音量 / 音高 / autopitch、组的 GainEffect 与音高自动化（Distance、EventConeAngle、参数）、spatialiser 的最小 / 最大距离与多普勒比例、各 master 与 bus。逐条写在 `sound_engine.h` 注释里。

**谁听哪个事件**（战雷的分法）：本机玩家坐在里面（任何座位）的那辆用玩家事件（自己的距离曲线就是混音）；其他一律用 AI 事件（NPC 和联机的其他玩家；FMOD 的反比衰减夹在 min / max 之间，再乘它的距离曲线）。上下车换事件时关掉旧声道。

**参数从哪来（ours）**：
- 地面载具：CarBase 的引擎（`0x676370` 那条）按车类名查 `engine::kDrives` 选燃气轮机 / 柴油 / 汽油和履带 / 轮子：505（Blacker）、601、603 防空车是燃气轮机（像 M1），403、404（泰坦）、510 Maser 是履带柴油，402 火箭车是轮式柴油，`Car` 类是汽油，503 / 511 摩托是汽油双缸、音高 ×1.25（按车类名分，ours）。
  - rpm 0..100：车速经变速箱换算（各挡顶速按 0.7 倍递减，88 升挡、40 降挡；起步离合打滑不低于 20 + 60 × 油门；停着回怠速 5；换挡瞬间负荷掉一下）。倒车（沿车头方向的速度往后超过 1 km/h）照战雷只用 1 个倒挡：传动比按顶速的 0.75 算，整段倒车都在这一挡，离合打滑下限和 1 挡一样，所以转速偏高；进出倒挡算一次换挡，负荷掉一下。刹车（往前走着拉后杆）仍走前进挡往下降。燃气轮机没有挡，rpm 跟油门（和保持的车速）走，带涡轮迟滞。
  - 负荷 = 原版引擎自己的油门 `+0x1A80`。履带速度加上转向（`kTrackHalf × 转角速度`），原地转向也有履带声。
  - 驾驶位有人时引擎起转、离开时停转（柴油停机放 `di_off`）。没人开但还在滑行时，履带 / 轮子仍按车速响。
- 喷气机：rpm 800 怠速 .. 2600 军推 .. 2750 加力，按油门 spool（约 4 s 从怠速到满）；加力份额（wep）点燃 / 熄灭各有过渡，点燃时放 `jet_ab_start`。
  - 自己飞：玩家飞行状态的油门 / 加力；停着（速度 < 3）怠速。
  - NPC：插件自己的飞控（`src/wing_engine.h`，`JetSteer` 每帧调用，离线检查 `tools/wing_engine_check.cpp`）。和玩家战机同一套油门语义：0..1 是满推力的份额，0.55 维持平飞巡航，> 0.95 开加力。
    - 油门杆每秒最多走 1.5，跟随飞控这一帧真正下的推力指令：按 v² 算的阻力份额（巡航时 0.55），加上逼近目标速度所用的推力（稳定爬升 / 转弯时就是它们吃掉的部分）。
    - 推力指令饱和就开加力：加速冲向攻击速度、陡爬升（60°）、持续 5 g 转弯、带转弯追击（229 / 235 m/s）。平飞保持攻击速度、快速巡航不再加速、俯冲减速时熄火。不按速度门槛判断。
    - 同一根油门杆也驱动尾焰（`JetFlames`）。
  - 旋翼起飞的插件喷气机（不是原版直升机，原版直升机不动）：功率取 Hover 的 power，悬停不掉到怠速。
  - 联机时别的机器跑的（已注册的 NPC 战机、别的玩家开的）：读跑它的机器同步来的油门杆（见「联机」），加力同一个阈值；没收到时有人坐只放怠速，不按速度估。
  - AI 事件的 rpm 表只到 2400，玩家的范围线性映射过去。

**空间化**：
- 声速延迟：每台记最近 640 帧（10 s）的位置 / 速度 / 机头 / 参数，按「传播时间 = 样本年龄」取当时的样本来发声（推迟时间），远处的飞机先看到后听到。
- 多普勒按事件的比例（喷气机 AI 按锥角），扩散度 `surround::SpreadAt`，空气低通按距离，左右 / 前后走 `surround.h` 的输出矩阵（立体声到 7.1）。
- 地面声源放在车体中心后 2 m、上 1 m 的引擎舱。

**电平（ours）**：`kTrimDb = -2`，按录音实测定（`Hear` 出来的各层增益乘各录音 RMS，骑乘镜头 14 m、在后方）：
- 自己的坦克满负荷比同距离 75 mm 炮声的前 0.3 s 低 14 dB，怠速低 22 dB；
- 自己的喷气机满功率在后方 15 m 比 75 mm 炮声低 16 dB。
- 战雷数据里自己飞机开加力时后向 max 层压低 6 dB、换成加力层，所以总响度和军推差不多、音色变了（不是更响）。

**日志**：`Debug=1` 时自己的载具、`Debug=2` 时所有载具每 2 s 一行 `SOUND jet ...` / `SOUND vehicle ...`：类名、事件（own / ai）、引擎种类、挡位、rpm、负荷、车速、距离、锥角、延迟、每层增益、开着的声道数和全插件的循环声道数。载具行还带沿车头的有符号车速、驾驶员此刻在踩油门 / 刹车 / 松杆（driving / braking / coasting，按座位 0 的左摇杆 Y 对比行驶方向），倒挡记作 `gear R`；刹车、开动、松杆或进出倒挡时立即补记一行（带 `now: brake` 之类，最快 250 ms 一行），不只靠 2 s 采样，因为一次刹车通常不到 2 s 就结束。

**实机（2026-11-26，`Debug=2`，build eec80a5259，测试场后台 autopilot）**：
- 自己的 403 怠速：diesel，3 声道，引擎层 0.118 / 0.106 / … / 0.043，14 m、锥角 168、延迟 0.05 s。
- AI 404：4 挡 rpm 78 负荷 0.40、38 km/h，履带层 0.074；2 挡 rpm 83 负荷 1.00、20 km/h。AI 轿车（Grape）汽油机怠速；柴油换挡 one-shot 触发 108 次。
- 没人坐的 403：`on 0.00`、0 声道（熄火不出声）。
- 自己的 403 行驶（ini `TestDriveCycle=1`：插件按固定循环写驾驶位油门，因为后台 autopilot 按 W 开不动坦克）：
  - 满油门：1 挡 rpm 52 负荷 0.97、8 km/h → 4 挡 rpm 83、43 km/h → 5 挡 rpm 61→68、45→50 km/h。换挡 one-shot 97 / 98 每挡触发一次。
  - 引擎层随 rpm 交叉：怠速层 0.119 / 0.107 → 0，高负荷层 0.169 / 0.141 → 0.215 / 0.209。
  - 履带层随车速切换：8 km/h 低速层 0.336，43–50 km/h 中速层 0.166 → 0.237。
  - 40% 巡航：负荷 0.72，降挡 5→4→3，20–25 km/h，低速履带层 0.472。
- AI 战机 rpm 1884→2277：270 m 延迟 0.81 s、多普勒 0.82；1924 m 延迟 5.67 s（≈340 m/s）；411 m 多普勒 0.93。每架 1–5 声道，全插件循环 20–25 个。
- 自己的战机：停机 rpm 800，3 声道；点火 one-shot 在 16 m（0.126）；W 推油门约 4 s 内 rpm 1567→2575→2750、wep 1.00，95 m/s 起飞，满功率 4–5 声道；松开后 wep 0.23→0、rpm 2600→2157，后向加力层 0.117→0、军推层 0.451→0.525。

## 没做的

- 原版直升机的旋翼声不换（保持原版）。

## 实机数据：炮声 / 命中 / 爆炸（Debug=1，2026-11-26）

Debug 日志的两种行：`SOUND event <名>: N layer(s) at D m; first gain … side … front … spread … rate … cutoff … Hz; matrix [...] (N ch)`（同名每 2 s 至多一行）与每 5 s 一行的 `SOUND stats 5 s: one-shots started X dropped Y, busy peak P/160; loops open O peak Q/96, none free Z; limiter on|off, N ch; 名=次数 ...`。计数里：`stock_hit_kept` 是原版命中声真的起播（预设有 bank），`stock_hit_taken` / `stock_blast_taken` 是被我们顶替的，`stock_silent` / `stock_silent_armour` 是原版预设本来就没声音（后者是命中装甲、只响我们的），`stock_se` 是全部原版预设起播（0x7AA8C0 探针），`stock_se_held` 是载具扣住的预设仍起播（应为 0）。

- 第一轮（测试场狂战士波次 + 3 辆 NPC 403 + 3 队士兵，`tools/perfrun.py swarm:e602_berserker:4:12:0.3`，120 s，DLL sha256 836b6fc0…，日志 loading 行 build 4b0eacb451 与之相符）：
  - 挂钩：`HOOK blast sound=1 (8/8)`、`HOOK armour hit sound=1 (4/4)`、`HOOK stock sound probe (Debug) 4/4`；`War Thunder's: 78 of 78 groups`。
  - 出现的事件：12mm_m2_ai / _last（NPC 机枪）、hit_mgun 53、rico_mgun 9、hit_pierce 8、passby 2、turret_loop 16 / turret_stop 14、reload_insert 31。
  - 27 个 5 s 窗口：one-shots dropped 全 0，busy 峰值 ≤ 5/160，loops 峰值 9/96、none free 全 0，限幅器 on（2 声道）；`stock_se_held` 全程 0。
  - 这一轮坦克只开了同轴机枪、场上没有爆炸弹：主炮、爆炸、来袭、装甲上的爆炸层没有触发，留给下一轮（坦克炮击 + 火箭 / 榴弹，打狂战士）。
  - 发现计数错位：`Take` 先看预设有没有 bank 再取标记，没声音的原版预设被记成 `stock_hit_kept`、标记留着过期，`stock_hit_taken` 恒为 0，证明不了「不两个都响」。已改成先取标记、再分「真起播」和「本来没声音」计数（见上一段）。

## 没验证的

- 没在游戏里试听（只有上面的日志数据）：各口径的音量平衡、近距层淡出距离、500 m 外 1/d 衰减的手感、机枪 grain 的连贯性。
- 真实 5.1 / 7.1 设备上的方位，以及 `FXMasteringLimiter` 在用户机器上能否挂上（看日志）。
- 联机时远端开炮、爆炸、掠过的覆盖：只确认了代码路径相同，没实测。
- 爆炸 hook 的 8 个调用点已在游戏里挂上（上面），爆炸事件本身还没在实机触发过；掠过的触发频率、来袭提前量的手感、炮塔 / 装填按峰值对齐后的响度都没试听。
- 自己坦克的刹车 / 倒车、NPC 战机的加力：只在日志里核对过（eng_live4，build 8e1b51dbfc，`Debug=2`），没试听。坦克每轮「brake / reverse」一开始就补记 `braking, now: brake`（20 km/h、3 挡），约 0.6 s 刹到 1 km/h，再过 0.25 s 记 `gear R … now: into reverse`；倒车全程 R 挡，rpm 80→90、最高 50 km/h；下一轮起步记 `now: out of reverse`（1 挡、4 km/h）。三架 AI fighter 各 70 行：wep 大于 0.5 的分别有 60 / 59 / 62 行（rpm 2400、700–825 km/h），收油门时 wep 落到 0、rpm 回 800，也就是 AI 大部分时间开着加力。停住时会停在 R 挡怠速，直到下次往前开。
- 联机同步油门杆：只有离线测试（量化、节流、线格式，`tests/jet_lever_net_test.cpp`），没在两台机器上实测过收发、加力和尾焰。
- `turret_turn_loop_1` 的首尾接缝只看了样点连续（首 953、尾 1346，同向），没试听。
