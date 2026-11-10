# X-Morph 单位的名字、原设与贴图（2026-11-10，测试站 #32）

反馈原话见 [feedback-2026-11-10-xmorph-names.md](feedback-2026-11-10-xmorph-names.md)。单位登记表是 `pylib/xm_units.py`
（一个单位一行：`title_en` 是原作英文名，`texts` 是四种语言的名字与说明）；移植本身见 [xmorph-port.md](xmorph-port.md)。

## 1. 名字

### 出处

- 原作**没有单位名表**（单位图标 `materials/textures/gui/hud/unit_icons/*.dds` 只有图，实体蓝图里没有显示名）。单位的正式叫法只出现在
  将军 / 士兵的**语音字幕**里：`packs/startup/02_win32_startup.zip` 的 `gui/0_localizations.csv`（本体）与
  `packs/startup/00_win32_startup.zip` 的 `gui/1_dlc_1_localizations.csv`、`gui/2_dlc_3_localizations.csv`（DLC），
  分号分隔，列 `ENGLISH / … / CHINESE_SP / CHINESE_TR / JAPANESE / …`。中文、日文取同一行的官方译文（`CHINESE_SP` 简体、
  `CHINESE_TR` 繁体、`JAPANESE`）。只读用户本机正版 `D:\steam\steamapps\common\X-Morph Defense`，不入库。
- 网上没有可用的单位资料（2026-11-10 WebSearch「X-Morph: Defense human units Bulldog Manticore Tortuga…」只搜到商店页、
  评测与 TV Tropes 概述，都不列单位名），所以以游戏文件为准。
- 单位 ↔ 名字的对应：字幕行的键名（`DIALOG/generic/dialog_soldier_<单位>_ready_NN` 等）与实体名一致，或任务逻辑里这句语音
  紧挨着刷这个实体。证据级别：**H** = 键名 / 任务逻辑直接对上；**M** = 只靠英文名与实体名、定位描述对上。

### 对照表

| 实体 | 原作英文名 | 旧名 | 新名（简中） | 繁中 / 日文 | 依据（原作字幕，简中为官方译文） | 级别 |
|---|---|---|---|---|---|---|
| `ground/medium_tank_regular` | Tank | 战斗坦克 | X-Morph 坦克 | 戰車 / 戦車 | 「Tank brigade reporting」→「坦克大队报告」；「常规坦克队」 | M |
| `ground/medium_tank_advanced` | Advanced Shielded Tank | 先进坦克 | X-Morph 高级铠甲坦克 | 先進裝甲戰車 / 新型シールド戦車 | `dialog_soldier_tank_advanced_ready`：「Advanced shielded tanks entering the battlezone」→「高级铠甲坦克进入战场」 | H |
| `ground/medium_tank_aa` | Anti-Air Tank | 防空坦克（不变） | X-Morph 防空坦克 | 防空戰車 / 対空戦車 | 「new anti air tanks … can repair other units」→「新型防空坦克……能够恢复其他部队」 | H |
| `ground/large_tank` | Bulldog Super Heavy Tank | 重型坦克 | X-Morph 超重型斗牛犬坦克 | 超級重裝猛犬戰車 / 超重ブルドッグ戦車 | `story_canada/dialog_soldier_heavy_tank_intro`（任务逻辑里紧挨 `ground/large_tank`）：「Super heavy Bulldog tanks reporting」→「超重型斗牛犬坦克队在战场上报告」 | H |
| `ground/large_tank_emp` | Manticore Tank | EMP 坦克 | X-Morph 蝎狮坦克 | 蠍獅坦克 / マンティコア戦車 | `dlc_3_easter_island/dialog_soldier_emp_tank_intro`：「Manticore tank entering the battlefield. EMP cannon charged」→「蝎狮坦克进入战斗区。电磁脉冲大炮已完成装弹」 | H |
| `ground/medium_artillery_regular` | Mobile Artillery | 自行火炮 | X-Morph 移动火炮 | 機動火炮 / 自走砲 | `dialog_soldier_artillery_ready`：「Mobile artillery team」→「移动炮兵队」；「send in mobile artillery」→「发射移动火炮」 | H |
| `ground/medium_shield_generator` | Tortuga Mobile Shield Generator | 护盾车 | X-Morph 托尔图加移动护盾战车 | 托爾圖加移動護盾戰車 / トルトゥーガシールド戦車 | `dialog_soldier_tortuga_ready`：「Tortuga mobile shield generators」→「托尔图加移动护盾战车」 | H |
| `ground/medium_missile_launcher` | Ravager Mobile Missile Launcher | 导弹发射车 | X-Morph 破坏者移动导弹发射器 | 破壞者移動導彈發射器 / ラヴァジャー移動式ミサイル発射装置 | `dialog_soldier_ravager_ready`：「Ravager mobile missile launcher reporting」→「破坏者移动导弹发射器报告」 | H |
| `ground/medium_apc` | APC | 装甲运兵车 | X-Morph 运兵车 | 裝甲運兵車 / 装甲兵員輸送車 | `dialog_soldier_apc_ready`：「APCs moving towards the alien core」→「运兵车正驶向敌方基地」 | H |
| `ground/medium_truck_cargo` | Cargo Truck | 运输卡车（不变） | X-Morph 运输卡车 | 運輸卡車 / 輸送トラック | 原作是任务道具，字幕里没有它 | M |
| `ground/small_humvee` | Humvee | 悍马（不变） | X-Morph 悍马 | 悍馬 / ハンヴィー | `dialog_soldier_humvee_ready`：「Humvee team engaging hostile」→ 官方简中「输送车队正在攻击敌军」——误译（Humvee 不是输送车），保留通行译名「悍马」 | H（名）/ 译名为我们改 |
| `ground/small_buggy` | Mobile Missile Support Buggy | 越野车 | X-Morph 移动导弹支援车 | 機動飛彈車 / 移動ミサイル支援車 | `dialog_soldier_buggy_ready`：「Mobile missile support unit on site」→「移动导弹支援部队准备就绪」 | H |
| `ground/small_mech` | Grasshopper Assault Infantry | 轻型机甲 | X-Morph 蚱蜢突击步兵 | 蚱蜢突擊步兵 / グラスホッパー攻撃歩兵 | 「Deploy Grasshopper Assault Infantry!」→「部署蚱蜢突击步兵！」；「Grasshoppers … Jetpacks ready」「Laser fences are not a problem」。官方简中另有「蚱蜢战斗机」「轻型侦察机」等误译，取与英文一致的这条 | M |
| `ground/large_mech` | Juggernaut | 重型机甲 | X-Morph 重甲兵 | 重裝機甲 / ジャガノート | 「Juggernauts ready to join the assault. Jump routines calculated」→「重甲兵准备加入袭击」（官方简中也有「重型机甲」「重型卡车」，「重甲兵」出现最多） | M |
| `bosses/highway_tank` | JAGUAR Type S | 美洲豹 S 型 | X-Morph 美洲虎 S 型坦克 | JAGUAR S 型戰車 / ジャガータイプS | 「I'm calling in the prototype JAGUAR Type S tank!」→「我正在呼叫美洲虎S型坦克原型！」 | H |
| `air/large_gunship_core` | Vulture Missile Gunship | 武装运输机 | X-Morph 秃鹰导弹空中炮艇 | 禿鷹導彈空中炮艇 / ヴァルチャーミサイルガンシップ | `dialog_pilot_vulture_ready`：「Vulture missile gunship reporting」→「秃鹰导弹空中炮艇报告」 | H |
| `air/small_helicopter_core` | Drone | 轻型直升机 | X-Morph 无人机 | 無人機 / ドローン | 「Drone formations … collision course with alien core」→「无人机编队……碰撞航向」（自杀无人机） | M |
| `air/medium_helicopter_core` | Assault Helicopter | 直升机 | X-Morph 攻击型直升机 | 攻擊直升機 / アサルトチョッパー | 「Assault helicopters ready」→「攻击型直升机准备」 | M |
| `air/large_helicopter_core` | Heavy Helicopter | 重型直升机（不变） | X-Morph 重型直升机 | 重型直升機 / ヘビーチョッパー | 「Heavy helicopters entering battle zone」→「重型直升机加入战场」 | M |
| `air/large_helicopter_transport` | Transport Helicopter | 运输直升机（不变） | X-Morph 运输直升机 | 運輸直升機 / 輸送ヘリコプター | 「Transport helicopters … Ground support airdrop」→「运输直升机……空投支援」 | M |
| `air/medium_fighter_aa` | Jet Fighter | 战斗机 | X-Morph 喷气战斗机 | 噴射戰鬥機 / ジェット戦闘機 | 「Jet fighters in the zone」→「喷气战斗机抵达」 | M |
| `air/medium_gunship_aa` | Gunship | 攻击机 | X-Morph 空中炮艇 | 空中炮艇 / ガンシップ | 「Gunships attacking the alien」→ 官方简中「武装直升机」，但这台是固定翼，用原作给 gunship 的另一译法「空中炮艇」（同秃鹰） | M |
| `air/large_gunship_aa` | Heavy Assault Gunship | 重型攻击机 | X-Morph 重型突击空中炮艇 | 重裝攻擊炮艇 / 重攻撃ガンシップ | 「heavy assault gunships against the X-Morph fighters」→「重型攻击武装直升机」（同上，固定翼取「空中炮艇」） | M |
| `air/large_bomber_core` | Heavy Bomber | 轰炸机 | X-Morph 重型轰炸机 | 重型轟炸機 / 重装爆撃機 | 「Heavy bombers in range!」→「重型轰炸机已进入射程内！」 | M |
| 防空炮台、防空机枪炮台、步兵 | AA Cannon / AA Machine Gun / Infantry | 不变 | 不变 | — | 字幕里没有专名 | — |

名字统一由 `xm_units.py` 出（呼叫表、载具表、M「摆放」目录 `src/spawn_catalog.inc`、测试场、HUD 武器名都从这里取），
`README.md`、`docs/multi-weapon-keys.md` 已同步。名字只是显示文本，不改任何文件名 / 实体键 / 武器表行号，存档与联机不受影响，
房间协议号不变。

## 2. 原设核对

对照原作实体蓝图（`entities/**/*.ent` 的 `FirePattern`、`fire_rate`、弹头蓝图；数值的换算见 README「X-Morph: Defense 的人类单位」与
[xmorph-port.md](xmorph-port.md)「武器」）逐个看了定位与武装。武装本身（哪门炮在哪个炮口、射速、连发、弹种）当初就是从蓝图取的，
这次没有发现「用错武器 / 错角色」的单位；**名字与说明**之前与原设不符（例如把斗牛犬叫「重型坦克」、把蚱蜢突击步兵 / 重甲兵叫
「轻型 / 重型机甲」、把移动导弹支援车叫「越野车」，看不出它们在原作里是谁），这次全部改正，并在说明里写明原作的定位。

仍与原作不符、这次**没有改**的（都需要新机制或改 native，见第 4 节待办）：

| 单位 | 原作 | 现在 | 原因 |
|---|---|---|---|
| 喷气战斗机 `air/medium_fighter_aa` | 只有两条翼下对空导弹，没有机炮 | 机炮键是插件喷气机的机炮 | 插件的喷气机前两个挂载位固定是机炮对（`src/jet_combat.cpp` `kGunWeapons`，`pylib/vcobjects.py` `XM_LOADS` 没有机炮就补 `_GUNS`），没有「无机炮喷气机」 |
| 空中炮艇 `air/medium_gunship_aa` | 只有两个火箭巢，没有机炮 | 同上 | 同上 |
| 运输直升机 `air/large_helicopter_transport` | 没有武器（只空投） | 保留 EDF 506 直升机原有的机炮和导弹 | EDF 的 V506 不论挂什么都建四个武器位，空着要改 native |
| 高级铠甲坦克 | 带等离子护盾 | 护盾未移植 | EDF 没有对应的载具护盾机制 |
| 托尔图加移动护盾战车 | 用护盾罩保护附近地面部队 | 护盾罩未移植，只有车顶小激光塔 | 同上 |
| 防空坦克 | 给 30 米内友军修理 | 未移植 | 需要新的友军修理机制 |
| 运兵车 | 从后门放出步兵小队 | 未移植 | 需要接入步兵编队生成 |
| 蚱蜢突击步兵 / 重甲兵 | 喷气背包跳过激光围栏 / 防御工事 | 借原版步行机底盘，不能跳 | EDF 步行机没有跳跃 |
| 无人机 | 成群撞向目标的自杀无人机 | 单架、机首撞击炸药由玩家 / NPC 发射 | 呼叫是一架可驾驶直升机 |

这些差异都写进了各单位的游戏内说明（「原作……，未移植」），玩家不会把它们当成原设。

## 3. 贴图（「很糊」）

### 处理链

1. 原作贴图：`packs/00_win32_data.zip` 的 `materials/textures/units_ground|units_air|…/*.dds`，DXT1 / DXT5，**原生就是 128–512**
   （例：`medium_tank_regular.dds` 256×256 DXT1，`_normal` 256，`_spec` 256；只有发光图 1024）。各包（00 / 01 / 02、DLC）里
   同一贴图只有这一个尺寸，没有更高分辨率的原件（H：逐包列了 `units*/*.dds` 的 DDS 头）。
2. `tools/prep_xmorph.py` 解码成 PNG 放进模型包 `D:\APP\edf6-models\xm_*\textures\`（无损，不再降采样）。模型包里现在的最短边：
   地面单位多为 256（悍马、越野车部分 128），大坦克 / 导弹车 / 机甲 512，秃鹰 1024，美洲虎 2048。
3. 生成时（`pylib/wt_tracked.py` `_dds_of` / `_normal_dds`）：最短边小于 `min_side` 的按整数倍 Lanczos 放大，再编码：颜色 DXT1、
   法线 BC5U（逐级单位向量 2×2 盒滤波，完整 mip 链）、参数图 DXT1。

### 发现

- **地面履带 / 轮式单位**早就在 `model.json` 写了 `min_side: 1024`，放大到 1024 再压缩。
- **飞机（`pylib/xm_air_model.py`）与步行机甲（`pylib/xm_rig.py`，X-Morph 步兵的身体 `xm_infantry` 也走它，颜色图只有 128）没有放大**：256 的颜色图直接 DXT1，一个 4×4 块覆盖机身 4–8 cm，
  块状色斑与 mip 叠在一起，比地面单位更糊。**已修**：两条路径都读同一个 `xm_units.TEXTURE_MIN_SIDE = 1024`（`prep_xmorph.MIN_SIDE`
  也改成引用它），颜色与法线放大后再压缩。
- 颜色图用 DXT1：原作颜色图本身就是 DXT1（无 alpha），我们的材质也不用颜色 alpha，不丢信息。法线是 BC5（比 DXT1 好得多，
  `docs/texture-quality.md`），绿通道已按 OpenGL → DirectX 翻转。mip 是完整链、盒滤波，没有 LOD 偏置。**处理链本身没有额外损失。**
- 根本原因是**原作贴图分辨率**：X-Morph 是俯视角游戏，256 的贴图铺在 9 米的坦克上（约 28 texel/米），EDF 原版载具约 200 texel/米。
  在 EDF 第三人称贴近看，放大只是让块压缩不再添块状噪点，细节本来就没有；原作没有更高分辨率的原件可取。

### 最优处理（结论）

- 以原生分辨率解码 → 整数倍 Lanczos 放大到最短边 1024 → 颜色 DXT1、法线 BC5（放大后重新单位化）→ 完整 mip。现在所有 X-Morph
  单位都走这一条。再往上放大到 2048 只增大文件，不增加细节（Lanczos 不产生新信息），不做。
- 想要真正更清楚，只能**重绘 / AI 超分**（ESRGAN 类）模型包里的贴图：那是对素材的二次创作，效果需要人眼判断，属于模型包内容，
  不在仓库代码里做；需要维护者决定（见待办）。

## 4. 待办（没做完的部分）

1. **无机炮喷气机**（喷气战斗机、空中炮艇）：让 `pylib/vcobjects.py` `XM_LOADS` 允许没有机炮对，`src/jet_combat.cpp` / `src/stores.cpp`
   在前两个挂载位为空时不把它当机炮、HUD 不显示机炮键，NPC 飞行员不走机炮攻击分支。改 native 的挂载布局，需实机验证。
2. **无武器的运输直升机**：V506 的四个武器位换成不开火的占位武器（弹数 0、不显示），或在插件里对这台跳过开火输入；需逆向确认
   V506 空武器位的行为（证据级别目前 L）。
3. 护盾（高级铠甲坦克、托尔图加）、防空坦克的修理、运兵车放兵、机甲跳跃：各需一个新机制，单独立项。
4. 贴图超分：如维护者同意，用 AI 超分把模型包 `xm_*\textures` 的颜色 / 法线图重做到 1024 原生，再发新版模型包（模型包版本随
   `pylib/modelpack.py` 内容哈希变）。
5. 实机确认：新名字在呼叫菜单、M 摆放、测试场、HUD 里显示正常；飞机 / 机甲贴图放大后在游戏里不再有块状色斑。
