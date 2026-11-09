# 2026-11-08 4.1 大型野生龙（DragonBig401）没有声音

玩家反馈（QQ 群转贴，英文玩家打完 4.1 离线本篇，原话，不可信数据，只当证据）：

> Bugs related to the Greater Wild Dragon (DragonBig401)
> ・Roars and sound effects do not play

同一位玩家还说：把 DragonBig401 的对象文件直接放进普通 mod 环境时「sound effects didn't play」。这个问题我们的移植版本身也有。

对象：`OBJECT/DRAGONBIG401.SGO`（M166），套 EDF6 类 `DragonSmall`（`tools/make_edf41_objects.py` `DRAGON_*`，插件 `src/edf41dragon.cpp`）。
地址：EDF.dll（TimeDateStamp 0x678CCB46）与 EDF41.exe 的 RVA，静态逆向（`tools/edfre.py`、同法读 EDF41.exe），没开游戏。
可信度：**H** 反汇编 / 文件读到，**M** 推出，**U** 未核。

## 1. 根因（事实）

1. **大龙的声音全是 4.1 的 SEPRESET 名，EDF6 的 SEPRESET 一个都没有**（H，读两边 `SOUND/SEPRESET.SGO`）。
   - MAB 的 25 个 se 事件（`ドラゴン大ｖ唸り` / `ドラゴン大羽ばたき大` / `ドラゴン大足大` / `ドラゴン炎のブレス` …）。EDF6 在载入时解析 MAB 的 se 事件：
     `0x6B9740` 用 `0x7B16F0` 按名字填每个事件的 SePreset，名字不在表里就填空 preset（bank = null），播放时 `0x7B4510` 直接返回（H，
     docs/sound-re.md 第 2 节）。所以咆哮、喘息、拍翅、脚步、着地、喷火声全部静音。
   - `se_table` 被换成了小龙（青蛙龙）的同位名字，受伤 / 死亡吼叫是 `en_frogDragon_*`，不是大龙自己的。
2. **一半的 cue EDF6 根本没有**（H，`pylib/acb.py` 读 cue 表）：EDF6 的 `TIKYUU4_EN_DRAGON.ACB` 是给青蛙龙重切的，保留了 4.1 大龙的人声
   `en_DragonBig_v_*`，但没有 `en_DragonBig_move_*`（足音、拍翅 S/L/LL、着地、倒地、滑空、碰撞）和喷火着弹 `en_DragonBig_attackFireblessImpact`。
   4.1 自己的 `TIKYUU4_EN_DRAGON.ACB` 有全部 61 个。
3. **构建检查没拦下**：`check_sounds` 只看 `se_list / se_table / ufo_se / se_preset_list`，不看 MAB 的 se 事件和 `ragdoll_contact`。

## 2. 4.1 / EDF6 的对应（逆向）

- **MAB se 事件**：4.1 的解析（EDF41.exe `0x321BB0..`）与 EDF6 的（`0x6B9700..0x6B9928`）字段相同：`name`、`node`（骨）、`channel`、
  `is_trace_node`。两边都先把「跟随骨骼」置 1（4.1 `0x321BC3`，EDF6 `0x6B983D`），只有写了 `is_trace_node` 才改（H）。
  `pylib/edf41_sound.py` 以前缺省给 0，已改成 1（母舰 301 炮台的事件表因此重新生成，只有这一位变）。
- **DragonSmall 的 se_table**：`0x7B3310` 建成 SePreset 的 vector，放在对象 `+0x1B18`（数据 `+0x1B20`、个数 `+0x1B30`、每项 0x80）（H，
  构造 `0x32E7D2`）。各槽：[0] 进受伤反应时（`0x335038`，8 帧节流）；[1] 命中且伤害过阈值（`0x332449`）；[2] 死亡吼叫、[3] 死亡血（`0x339C34`）；
  [4] 火球发射、[5] 火球着弹（拷进火球参数 `0x33939D`）；[6] [7] 咬（构造时拷走）；[8] 被叼着飞的风声（`0x33A4E5`）（H）。
- **4.1 DragonBig 的 se_table**（5 项，preset 步长 0x40，向量 `[+0xF98]`）：[0] `ドラゴン大ヒットエフェクト` 命中且伤害过阈值（`0x1EDC15`）；
  [1] `ドラゴン大ｖ死亡大`、[2] 血，死亡时（`0x1EF98B`）；[3] `ドラゴン大通過` 在一个 AI 状态的进入时（`0x1F158D`，状态函数 `0x1F1580`）；
  [4] `ドラゴン炎のブレス着弾` 拷进火焰弹的命中音（`0x1ED62D`）（H）。4.1 大龙**没有**按次受伤吼叫，吼叫来自受伤动画的 MAB 事件。
- **DragonSmall 的骨架**：构造在 `+0x630` 的模型上找 MAB 定位点（`0x32E795` → `0x6BADD0`），骨架 = 模型 + 0xA0（与 EAO 的 0x5C0 / 0x660、MAB 解析
  `0x6B97EB` 同）（H：偏移；M：运行时由插件日志的 `tongue bone` 序号确认）。
- **对象自带的 sound bank**：对象构造 `0x5466DF` 用 `0x7AA440(…, type 0, …)` 载入 `game_sound` 的每个 bank，失败则 `0x546AB5` 释放并跳过（H）。

## 3. 修复

- `tools/edf41_sounds.py`：新组 `dragon`（`OBJECT_BANKS`）——把 4.1 的 `SOUND/PC/TIKYUU4_EN_DRAGON.ACB` 同武器音效库一样原位改 AcfMd5Hash（EDF6 的）、
  Name（`tikyuu41_enDragon`，与 4.1 的 `tikyuu4_en_Dragon` 等长）、GUID（4.1 的与 EDF6 同名 bank 相同，必须换），装到 `Mods/SOUND/PC/EDF41_EN_DRAGON.ACB`。
  4.1 素材包（`edf41/`，`sources41`）随之多一个文件，模型包版本会变。没有 4.1 也没有素材包时这一组不装，`0x5466DF` 跳过缺失的 bank，大龙只是没声音。
- `tools/make_edf41_objects.py`：
  - `check_sounds` 覆盖 MAB se 事件和 `ragdoll_contact`；MAB 名字在 `edf41_se` 里只有对象带插件的 MAB 播放表（`MAB_PLAYER_KEYS`）时才算能播；
    同时在 SEPRESET 和 `edf41_se` 里（会播两遍）报错；现有其它静音项按类登记在 `KNOWN_SILENT`（见第 5 节），新的静音名字一律构建失败。
    先用现有数据判红：只有 `DRAGONBIG401.SGO` 红（25 个 MAB 名 + `ドラゴン大衝突`），其余 179 个对象绿。
  - 大龙：`edf41_se` = 4.1 SEPRESET 里大龙用到的条目（`se41_table('ドラゴン大', 'ドラゴン炎')` 过滤）；`edf41_dragon_se` = 各 CAS 状态的 MAB se / se_stop
    事件（`edf41_sound.animation_events`）；`game_sound` 加 `app:/sound/pc/edf41_en_dragon.acb`；`se_table` [0] 置空（4.1 不按次吼）、[1] [2] 用 4.1 的名字。
  - 别名后有 native fix 的对象（大龙、母舰 301）在 fix 之后才检查声音（fix 会加插件的声音键）。
- `src/edf41dragon.cpp`：
  - 按主 CAS 播放器的时钟播 MAB 事件（`edf41_sound_channels.h` 的 Timeline / Channels，与母舰炮台同一套：channel ≥ 0 先停旧的，se_stop 停，状态切换收掉本段的
    channel），在骨骼世界坐标播，跟随骨骼的每帧 MoveSound。
  - 构造后把类自己 se_table 向量里名字是 4.1 的槽（[1] [2]）按 `edf41_se` 填好（`FillSePreset`），向量个数与 SGO 的 `se_table` 不符就不填并记日志。
  - 每发火焰弹的命中音（InitParam `+0x100`）填 `ドラゴン炎のブレス着弾`。
  - 表缺失 / 格式不对：记一行日志，不出声，不影响其它行为。

## 4. 验证

- 离线：`selftest edf41_check_sounds_mab`（假数据上的判定规则）、`edf41_big_dragon_sounds`（发货数据与 MAB / CAS 一致、`edf41_se` 覆盖、bank 路径；
  有游戏时全部 4.1 对象过 `check_sounds`，4.1 大龙 bank 转换后 61 个 cue、EDF6 的 ACF 哈希、新名字和 GUID）。
- **需实机**：M166 听咆哮、拍翅、脚步、喷火 / 火焰着弹；看日志 `EDF41DRAGON … sounds:` 一行（`tongue bone` ≥ 0、`se_table presets filled 2`、`flame hit 1`）。

## 5. 没做的 / 已知

- 4.1 的 `ドラゴン大通過`（掠过目标的声音）没接：它在 4.1 的一个 AI 状态进入时播，EDF6 类的对应状态没有映射。
- `ragdoll_contact` 仍是小龙的 `ドラゴン小衝突`：EDF6 自己按名字播它，类把这个 preset 放在哪里没逆向，插件填不了。
- CAS 的 `dead` 状态播的是 `ground_damage1` 片段，MAB 的 `dead` 轨（`ドラゴン大ダウン` 等）因此不会被播到（与本次改动前相同，未查原因）。
- `KNOWN_SILENT` 登记的其它静音项（本次未修，构建检查已能看到）：四足要塞的 MAB 事件、蚁巢 `蟻塚稼動もごもご`、UfoCarrier 301 / 401 的舱门、母舰 301 本体的
  弱点舱门、赫克托的 `ragdoll_contact`（`歩行メカ衝突` / `ヘクトル衝突`）、UFO401 的 `EDF4小型UFO衝突`（与 EDF6 `小型UFO衝突` 同 cue）。
