# 2026-10-09 用户反馈：普罗透斯（proteus 组）

> 「普罗透斯的护盾用原版的护盾样式吧。还有这个普罗透斯做的稀烂，给我完全重做」

分支 `fix/fb1009-proteus`（基于 origin/main b89d26a）。**全部改动都没有进游戏验证**（按约定不启动游戏、不安装）；下面分清哪些是离线 / 原生映像验证过的，哪些只能进游戏看。

## 1. 旧实现「稀烂」在哪里（审视结论）

| 问题 | 位置（旧代码） | 性质 |
|---|---|---|
| 护盾是自制的：给两种模型生成私有 MRAB/CAS，加 36 根骨、36 块 Fencer 手持盾材质的平板，按角度缩放拼成一圈；并改写全部 10 个 VehicleBigBegaruta SGO 去引用私有模型 | `pylib/proteus_model.py`、`tools/make_proteus.py`、`src/proteus_visual.inc`、`proteus_pose.h Panel` | 视觉不是原版护盾；改写原版模型 / 动画的风险已兑现过一次（CAS 通道表未对齐，进图崩溃，`docs/proteus-cas-crash.md`） |
| 护盾不挡东西：子弹照样穿过平板，伤害靠钩 `0x54A586 → 0x547C30` 按「命中点是否在角度内」改伤害；为此又复刻了原生许可门（友伤、无敌、场景禁伤…）| 旧 `proteus.cpp Shield/DamageHook`、`proteus_damage_gate.h` | 绕过原生机制的补丁，和 coop / 其他伤害路径耦合 |
| 驾驶员武器是假的：用炮舰机的 40 mm 弹（DemoIndirectFire）冒充机炮，用炮舰机火箭弹冒充导弹齐射，要「标记目标」、要预载、要按档位手算伤害；原版导弹架反而被「停住」（倒计时写 1e9），还连累 `vehsound.cpp` 识别「停住」 | 旧 `DriverGun/Salvo/Mark/Led/RoundFrom`、`jet_bay.cpp ProteusGunRound/SalvoRound`、`kProteusHoldCountdown` | 与原版武器两套并行、到处特判 |
| 驾驶员 / 炮手 / 右炮 / 导弹架各有一段特判（右炮跟左炮一套、驾驶员遥控右炮又一套、导弹架跟驾驶员又一套），外加两个瞄准 vtable 钩子与安装顺序约束 | 旧 `FollowCannon`、`AimHook<0/1>`、`ProteusEmptyWeapon`、`UserHook` | 同一件事（空座武器由谁用）写了四遍 |
| 「EDF5 普罗透斯是双人机甲」的前提不成立 | — | EDF5 Root.cpk 的 V407 也是驾驶员 + 三个炮手四座（`docs/proteus-re.md` §0）；两席是用户 2026-10-06 的需求，保留 |

## 2. 重做后的结构

- **数据**：`proteus_logic.h` 两张表——姿态 / 护盾规则（`State`、`Step`、`Refill`、`Sense`），武器借用表 `kMounts`（每座武器的借用链 + 驾驶员用哪个扳机 + 是否只在架设时）与纯函数 `Operator(mount, 占位, 姿态)`。离线检查遍历全部 16 种占位 × 4 种姿态。
- **武器**（`src/proteus_weapons.inc`）：只用原版两门加农炮和导弹架。座位空着的武器由链上第一个在座士兵借用：瞄准轴抄操作者座位并应用到骨骼（slot 5 之前）、原版激活 `0x68F8E0`、操作者机器上按扳机时原版拉扳机 `0x62C000`、开火步的使用者答借用者（空座的原版答案是「上一任乘员」，被覆盖）。驾驶员一个人时用主射击键开两门炮，架设后用鼠标右键 / LT 打原版导弹架（原版锁定 / 追踪）。四条特判合成一个循环。
- **护盾**（`src/proteus_shield.inc` + `tools/make_proteus.py`）：原版空袭兵「電磁トーチカ」的能量墙（弹种 `BarrierBullet01`），由一个生成的 DemoIndirectFire SGO（`EDF6VC_PROTEUS_SHIELD.SGO`）发出一发，弧 120°、半径 12 m、高 17 m；钩它的 slot 5 每帧把它放回机体脚下、朝车头 / 驾驶员视线；墙的原生 HP 就是护盾 HP；原生碰撞按阵营挡敌弹、放己方弹。资源名与地址见 `docs/proteus-re.md` §4。
- **删除**：私有模型 / CAS 生成、36 块盾板、伤害钩与许可门、插件机炮 / 齐射 / 标记、导弹架停住、两个瞄准钩子；ini 键 `ProteusMarkKey/Button`、`ProteusShieldArc/Block`、`ProteusDriverGun`、`ProteusGunRate/Damage`、`ProteusSalvoCount/Damage/CooldownSec/Range`（旧 ini 里留着也只是不再读取）。`ProteusSalvoKey` 保留，含义改为「驾驶员发射原版导弹架」。
- **安装 / 卸载**：`make_proteus.install` 仍走原写前日志，更新时自动撤销旧版：被改写的 SGO 恢复原件、私有模型删除（第三方仍引用时保留）。测试场不再改写 SGO，只登记护盾 SGO 依赖。
- **联机**：协议升到版本 2（`barrier` = 墙 HP 比例，新增 `kBroken`）；owner 计 HP 并发布，各机器各立一面本地墙（收起不广播）。见 `docs/proteus-online.md`。
- **HUD**：姿态、护盾（状态 / 耐久 / 热量 + 耐久条）、力场、驾驶员导弹架提示；借用的原版武器出现在座位武器列表里（`vhud.cpp` 改用 `ProteusBorrowedWeapons`）。

## 3. 改动的共享文件（都很小）

`src/crew.h`（Config 删键、`EmcRound::proteusShield`、删 Proteus 弹声明）、`src/jet_bay.cpp`（EMC 表加一行护盾 SGO、删 Proteus 弹函数、`PreloadShells` 去掉 proteus 参数）、`src/jet_spawn.cpp` / `jet_internal.h`（同上）、
`src/plugin.cpp`（ini 读取 / 校验 / 日志）、`src/hud.cpp`（`ProteusLinesOf` / `ProteusMarks`）、`src/hudtext.inc`（普罗透斯文字）、`src/vhud.cpp`（借用武器）、`src/vehsound.cpp`（删停住判断）、`src/subcarrier.cpp`（伤害调用不再被改写）、
`tools/hud_view.cpp`、`tools/selftest.py`、`tools/installer.py`（标签）、`tools/build_release.py`（模块表）、`testrange/gen.py`、`tests/fixed_weapon_sight_test.cpp`、`autoturret/tools/proteus_describe.py`（五语说明）、`EDF6VehicleCrew.ini`、`README.md`、`CMakeLists.txt`。

## 4. 验证

已做（不启动游戏，只写临时目录）：

- 构建：`cmake --build build -j 4`、`--target offline_checks` 全部通过（/W4 /WX）。
- `ctest -j 3`：175 项中 174 通过；`proteus_*` 12 项全过（`proteus_check`、`proteus_frame_test`、`proteus_weapon`、`proteus_weapon_native`、`proteus_net`、`proteus_net_runtime`、`proteus_net_guard`、`proteus_visual_profile`、`proteus_assets`、`proteus_install`、`proteus_range`、`proteus_text`）。失败的 `support_soldier_native` 与本组无关（Python 原生调用 `0x776790` 访问冲突，脚本与 origin/main 相同、不涉及本组代码）；前一轮 `mission_crew` 并行时偶发失败一次，单独 `ctest -R mission_crew` 5/5 通过、复跑全量也通过。
- `python tools/selftest.py`：132/132（改前也是 132/132）。`hud_view`：普罗透斯两种场景 0 重叠，16:9 / 21:9 版面不撞。
- 原生映像（`proteus_weapon_native`，私有映射真实 EDF.dll，不跑 DllMain）：真实 `0x62D950` 返回空座上一任乘员而钩子答借用的驾驶员；真实激活 + 拉扳机通过真实就绪门 `0x6912F0`（驾驶员的炮、架设时的导弹架）；行走时真实停用；补丁后的空座回调在插件关闭时走原版停用；用户钩签名失败时什么都不借用；电磁碉堡墙全部签名在真实 DLL 上成立、slot 5 挂链；真实受击 `0x2913B0` 扣墙 HP 后护盾读到并击碎、`0x40` 重放不重复扣。
- 生产代码夹具（`proteus_frame_test` / `proteus_net_runtime`）：立墙、认领（不碰空袭兵的碉堡）、跟随转向和移动、读 HP、粘住后重立、击碎、关掉、立墙失败不重试、还车撤墙；owner 发布 HP、副本只显示 owner 的数。
- 真实 Root.cpk：生成的护盾 SGO 弹种 / 颜色 / 音效取自 EWEAPON196，分段数 24（碉堡 27）；EDF5 Root.cpk 设定核对。

**必须进游戏才能确认（未做）**：墙的外观、位置、大小是否合适（尤其墙底裙边与地形）；行走时墙跟随是否同步、会不会擦到敌人被锚定（插件会丢弃并重立）；墙是否真的挡住敌弹并按耐久破碎；墙的装置模型是否藏在机体里、展开音效能否播放（需要空袭兵音效库已加载）；驾驶员一人开两门炮的瞄准方向；驾驶员借用的导弹架能否原生锁定和追踪（锁定 tick 取武器方向，静态推断）；联机两台机器上各自的墙与 HP 同步；从旧版安装升级后模型恢复原版。

## 5. 未完成 / 后续

- 护盾弧度 / 半径 / 高度在 SGO 里写死（墙在构造时建网格），不能按 ini 调；若要可调，需要生成多个 SGO 或在 IFC 发射前改 InitParam 的自定义参数数组（未逆向）。
- 架设后机身升高（腿部 IK）仍未做，只升镜头。
- 原版 NPC 炮手 AI 对借用逻辑的影响（玩家驾驶、NPC 坐炮手位时 NPC 用自己的扳机锁存）只在夹具里验证。
