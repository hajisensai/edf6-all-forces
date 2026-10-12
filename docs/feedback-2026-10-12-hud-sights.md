# 载具 HUD / 瞄具四条反馈（2026-10-12）

分支 `claude/1012-hud-sights`。全部离线验证（构建 + 离线检查 + 真实 Root.cpk 只读核对 + hud_view 渲染）；**未进游戏验证**。

## 1. 机甲挂载左右反了

用户原话（附截图 `代办/ea038ab693b32657065e96f9486b9809.png`，尼克斯）：「左侧武器放在右边右侧武器放在左边，修复一下」。
截图里屏幕左边的格子是「电磁激光炮（右）」，右边是「（左）」。

- 根因：`src/hud.cpp` `StockCells` 按座位挂载表（`seat+0xC8`）的顺序排格子，网格从左往右画；尼克斯的座位先列右臂步枪，
  所以右臂的枪画在左边。顺序与武器在车上的位置无关，任何左右成对的车都可能反。
- 修复：`src/vhud.cpp` `SideOf` 读每把武器第一个枪口（`edf::MuzzleFrame`）相对车体中心、沿镜头视线的左右偏移（左 = up × look =
  (look.z, 0, −look.x)，与 `turretcam.h`「车体矩阵第 0 行是它的左」一致），存进 `StockArm::side`；`src/hud_cue.h` `SideOrder`
  按「左侧（> 0.5 m）→ 中线附近 → 右侧（< −0.5 m）」分三组、组内保持座位原顺序（同一门炮上的 APFSDS/HE 等挂载仍按 R 键的顺序），
  `StockCells` 按它排格子并返回选中项所在格（换挂载横幅、直升机挂载条随之改用格子下标）。读不到枪口时 side = 0，保持原顺序。
- 验证：`hud_cue_check`（尼克斯一对、坦克主炮+同轴+左发射器、读不到枪口三例）；`hud_view` 场景 `nix_rifles_left_right`
  （左格「RIFLE (L)」、选中右枪时横幅与青框都在右格）。

## 2. 泰坦炮口准星不随炮管动 / 两个准星要同时显示

用户原话：「泰坦的炮口准星不会随着炮管动」；群聊：「估计是右键的时候」「改成机枪了，修复一下，应该两个准星都同时显示才对」。

- 根因：火控只认「最近按下的那个扳机」（`payload.cpp` `PayloadSightWeapons`，右键 = 副射击组：同轴机枪 / 泰坦车头加特林），
  `vhud.cpp` 只给这一组武器算弹道（`Arm(..., aim=coFired)`），`hud.cpp` `StockMarks` 也只画这一组。右键之后主炮的标记整个消失，
  画面上剩下的是车头加特林（不随炮塔转）的标记，看起来就是「准星不随炮管动」。
- 修复：新增 `PayloadTriggerWeapons`（与 `PayloadSightWeapons` 共用同一段解析 `SeatFireWeapons`：主射击组 + 副射击组，含挂载重定向、
  去重）；`vhud.cpp` 给这些武器标 `armed` 并照常算枪口与落点；`StockMarks` 对「另一个扳机上的炮」画它自己的枪膛十字（随炮管动，
  暗色，下面标武器名，`OtherTriggerMark`）和它的落点，选中的那个仍画完整瞄具。放大镜里（`scopeNow.on`）不加画，那一视场只属于选中的武器。
- 验证：`hud_view` 场景 `stock_tank_loading_two_triggers`（选中同轴机枪时，中心是机枪的十字，主炮的暗十字和「加农炮」名字在它自己的炮膛方向）。

## 3. 榴弹炮显示成「光束 / BEAM」，图标也不对

用户原话（截图 `代办/4b0434acf09352250555e9f73b5fa052.png`，「140毫米高压榴弹炮 25/30」）：「瞄具显示的文案不对，明明是榴弹炮，
怎么显示的光束。而且这个榴弹炮图标也不对」。

- 根因：`src/rounds.cpp` 按弹丸工厂类判弹种，`EfsBullet` / `EfsExposureBullet` 一律记成 BEAM（能量武器）。真实数据（本机 Root.cpk，
  只读）：601 坦克的主炮 `V601_TANK_CANNON01..04_DLC` / `V_601TANK_CANNON01` 发射的是 `EfsBullet`，`AmmoGravityFactor 1`、
  `AmmoExplosion 8–18 m`——会下坠、会爆炸的炮弹；尼克斯 / 普罗透斯的激光是 `EfsExposureBullet`，重力 0。于是 140 mm 炮被标成
  「[光束]」、瞄具弹种写 BEAM、不画下坠标尺，挂载条图标画成能量发射器。
- 修复：`src/reticle.h` `FallingEffectShell`：效果弹类且重力系数 > 0 → 炮弹；`ReadRound` 据此把它记成 `CANNON`、`projectile`。
  瞄具于是是加农炮式、弹种 HE、有下坠标尺；声音分类不变（以前 BEAM 慢速也落到同一个「120 mm 坦克炮」档，`vsound_check` 核过）。
  挂载条新增 `StoreIcon::shell`（一发炮弹：药筒、弹带、弹头、引信），逐发装填的炮（瞄具为 cannon / precision）用它，机枪仍是三发子弹。
- 验证：`tests/sight_data_audit.py` 现在也核对每行的 `AmmoGravityFactor`（25 行全部与 Root.cpk 一致），`reticle_check` 里 601 主炮
  = cannon/HE，新增 `V_612_A_LASERRIFLE_L`（EfsExposureBullet）= energy/BEAM 作为反例；`hud_cue_check` 图标三例；`vsound_check` 601 主炮仍为 120 mm 坦克炮。

## 4. 载具加装填时间显示

用户原话：「载具加个装填时间显示」。

- 原来只有打空弹匣后的换弹匣进度（`0x692100` 的 `+0xE68`）。逐发装填的炮每发之间的等待（`+0xE0C`，射击后被设成 FireInterval
  `+0x36C`，`docs/sound-re.md` §9.2）没有显示。
- 修复：`vhud.cpp` `Cycle`：有弹、FireInterval ≥ 30 帧（0.5 s，机枪的 3–5 帧不显示）、等待在 (0, FireInterval] 内时记
  `cycle`（已装填比例）与 `cycleSec`（剩余秒）；超出 FireInterval 的等待不算装填（普罗透斯接管的发射器被置成 1e9）。
  武器行与挂载条格子在装填中显示「装填 1.8s」（琥珀色）。
- 验证：`hud_view` 场景里 140 mm 炮格子为琥珀色「加农炮 10/10 装填 1.8s」并配炮弹图标。

## 未验证 / 缺口

- 全部未进游戏：枪口左右偏移的符号依据 `turretcam.h` 的约定与离线推导；若游戏内尼克斯仍反，看 `Debug=1` 下格子顺序。
- 泰坦（404）的「两个准星」依赖它的车头加特林在副射击组（`PayloadFire::secondary`）里；没有实测确认它的挂载表分组。
- 插件自己的飞机 / 直升机座舱读数（`PlayerJetReadout`）没有加逐发装填时间，只改了原版载具 HUD。
