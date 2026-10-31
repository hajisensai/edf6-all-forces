# 终极支援「天罚」实现方案（2026-10-19）

用户原话与要求见 [feedback-2026-10-19-ultimate.md](feedback-2026-10-19-ultimate.md)：特效拉满、昏天暗地；性能、误伤不设限；
魔法阵必须有，原版素材不行就自制；清区终极技能，pt 很高。

置信度：H = 反汇编 / 源码直接读到；M = 推断；L = 猜测，待实机。

## 演出时间轴（约 36 s；10-20 首测后放慢：「来的太快了，应该慢一点，不然没那种宏状感」，初版 25 s；10-31 余波缩为 6 s）

| 阶段 | 时长 | 内容 |
|---|---|---|
| 蓄力 charge | 0–12 s（6 个法阵约 7 s 内逐个展开，天色 8 s 压暗） | 环境渐变：雾色转红紫、雾距压近、环境光 / 主光压暗、天空变色、色调压低；地面法阵 3 层逐层展开、正反旋转，天空同心法阵升起；8 道细光从高空圆周会聚到目标点并逐帧变粗；地面 StencilLight 红光脉动；粒子光点上升 |
| 雷暴 storm | 12–22 s | 半径内每帧随机落雷（落点爆炸 40 m，铺满整个法阵；首测嫌「攻击范围那么小」）（LightningBullet01 竖直劈；不行退 PlasmaBullet01 + 闪光），配 Explosion、Quake；法阵加速旋转、亮度脉动 |
| 主炮 cannon | 22–30 s | VSRADON 派生粗光柱（粗 ≥ 48、颜色按主题）2 s 内贯下变粗；光柱满粗后由中心向外每 k 帧一圈 M 个大爆炸，半径逐圈增大，形成环形冲击波；强 Quake |
| 余波 after | 30–36 s | 头 2 s 少量 Blazing 残火与烟（10-31 起有预算：`LooksAt`，docs/feedback-2026-10-31-ultimate-fx.md）；法阵淡出、删除；31 s 起天色 3 s 还原（最后一圈爆炸后约 5 s 复原；10-31 前 32 s 起 8 s） |

## 模块

1. **呼叫武器**：`tools/calls.py` CALLS 末尾追加 `EDF6VC_CALL_ULTIMATE`，`brings='ultimate'`，模板原版卫星 `eWeapon116`
   （Weapon_LaserMarkerCallFire；激光指示、倒计时、语音都是原版的）。ReloadTime ≥ 12000（原版卫星 2400–5400）。
   标记写 +0x8C4（AmmoHitSizeAdjust）：卫星代码段 0x6A0C50–0x6A6900 不读 +0x8C4（H），没插件时就是一把原版卫星。
   连带：`call_weapons.py` 模板 / SGO 分支 / 文本；`gen_calls.py` 生成 `kUltimates`；`selftest.py` brings 白名单与 RELEASED。
2. **触发钩子**：卫星发射态 0x6A59F0 里 0x6A5A29 `call 0x2B8090`（rcx = weapon+0x18A0）（H）。钩住：先调原函数，
   认出是我们的武器就 `UltimateStart(weapon, aim, owner)`，并把 IFC +0x2F0 剩余发数清 0，让状态机自己回待机（M）。
   命中点取 weapon+0x18C0 / +0x1D60（docs/tvguide-re.md §1.1）。
3. **演出状态机** `src/ultimate.cpp` + 纯逻辑 `src/ultimate_plan.h`（种子驱动的确定性时间轴，`tools/ultimate_check.cpp` 离线检查）。
   挂 `SharedFrameTick`（crew.cpp）里的 `GuardedTick`，计时用 `GameFrame()`。`MissionStart` 里 `ResetUltimate()` 只忘指针。
4. **弹幕**：`ShellMake`（src/jet_bay.cpp）发 DemoIndirectFire，运行中 RoundSteer / RoundSize / RoundBlast / RoundDrop。
   新 SGO 由 `tools/make_ultimate.py` 从原版派生（VSRADON 粗光柱、细光、雷、爆炸），保留 `indirect_fire_param` #13 custom。
   颜色运行时改不了（L）：每种颜色一份 SGO。
5. **天色**：调游戏自己的环境渐变管理器：`mgr` 读全局 base+0x20B34A0（为空就跳过，不调会新建的 0x5BEFA0），
   `0x5C1160(mgr, type, float4*, dur)` 渐变，`0x5C0840(mgr, type, a, dur)` 还原（H）。用 type 12/13/14/0/21/25。时长单位 L。
   另：`Quake`（edf41classes.cpp）、`StencilLight`、`SceneEffect_Rain` internal 0x1BDAD0（M）可叠暴雨。
6. **魔法阵**：自制模型。每层一个无 ragdoll 的 FarEventObject（照 `make_chute.py` / 伞衣），独立 MRAB，单位半径双面圆盘 / 圆环，
   抄母舰护盾 `snd_BeamShield` 材质（两层图案、`light0/1_color` 可 >1、`alpha` 独立、遮罩只用 R，DXT1 够）。
   每帧写矩阵 +0x60、缩放 +0xAC0..AC8，按名绑定材质参数（edf41mother.cpp 先例）做淡入 / 脉动 / 淡出。
   图案用 PIL 程序化画灰度 PNG 提交进仓库，安装时纯 Python 转 DXT1。原版没有可用的法阵贴图（H）。
7. **联机**：所有效果本地生成、不同步（H）。新 EDF6Coop extension magic `ULTS`：发起者 ID、序号、目标、64 位种子、elapsedMs，
   演出期间约 250 ms 广播一次状态；各端按种子本地重放，伤害靠 OnlineShotCounts 只结算一次（M）。放在原型跑通之后做（2026-10-20 已做，实际设计见文末「第三轮」：每 1 s 一次 keep，不是 250 ms 状态）。

## 待实机确认（原型阶段一并验证）

1. EnvFade 时长单位（秒 / 帧）、a 参数含义；下一关是否清干净。
2. `snd_BeamShield` 的混合方式、深度写入、多层透明排序；300 m 圆盘的裁剪包围盒与坡地穿插。
3. 材质绑定是按实例还是按模型共享（规避：每层独立模型）。
4. LightningBullet01 经 IFC 竖直劈的行为与 custom 含义。
5. 爆炸半径 ≥ 300 是否有上限（退路 `edf41::Blast`）。
6. 远端玩家的卫星副本是否也走 0x6A5A29（决定联机由谁广播）。

## 里程碑

1. M1 本地单机原型：呼叫武器 + 钩子 + 时间轴 + 弹幕 + 天色（无法阵），autopilot 测试场截图。
2. M2 魔法阵：贴图生成、模型、安装链、运行时驱动；截图给用户挑配色与样式。
3. M3 联机广播与重放；gamenet 测试。
4. M4 打磨、文档、PR。

## 第二轮（2026-10-20，反馈见 feedback-2026-10-19-ultimate.md）

- 射线全部从法阵出发：蓄力瞄准线从三层天空法阵的符文带发出（随法阵旋转）；落雷从最低一层天空法阵的盘面落下；主炮光柱从最高一层天空法阵中心贯下。
- 汇聚（雷暴最后 2.5 s）：落雷停歇，所有法阵收缩 18 %、加速旋转，除顶层外每层法阵边缘 4 道金色光脉冲沿射线流向顶层中心（`EDF6VC_ULT_GATHER.SGO`：恰好 120 m 的短激光，从「距中心 120 m 以外」的滑动点发射，终点停在中心、不会穿过去），中心金光渐强；开炮时法阵回弹。
- 冲击波：新模型 `EDF6VC_ULT_CIRCLE_SHOCK`（亮环 + 拖尾波纹 + 外抛条纹），4 道：光柱落地一道、顶层法阵释放一道（空中）、最后一圈爆炸一道大的（跑到 3R，强闪光、强震、前沿扬尘）及其回波。
- 音效：每个节点（蓄力、法阵展开、雷、汇聚、主炮、每圈爆炸、冲击波）先找 DLL 旁的 `EDF6VehicleCrew_ult_<名>.wav`（`tools/sound_pack.py` 从素材包做），没有就播游戏常驻音效库 TIKYUUX_SE.ACB 里的对应 cue（创世系 / 卫星 / XL 爆炸）。
- 原版斯普莱特的无线电台词（CP[5] 与 Ammo_EquipVoice）全部置空。

## 第三轮：联机与生命周期（2026-10-20，用户：「所有功能都要支持联机」「有问题全部根本性修复」）

- **联机通道 ULTS**（`src/ultimate_net_protocol.h`，plugin_net 通道，能力位 `kExtUltimate`=512）：发起者在开演时广播 `start`，
  每 1 s 一次 `keep`（新进房 / 丢包的队友由此接上；对端新出现该能力位时立即补发），收尾或中止发 `end`。字段：本次任务运行号
  origin + 序号 serial（演出的键）、发起者的原生 ID（接收方用 `SenderIsPlayer` 核对它就是发送方本人，否则丢弃）、已贴地中心、
  三个地面法阵高度（发起者的 MapRay 结果，各机法阵一样高）、种子、伤害、发送时已播帧。
- **各机自己播**：法阵、天色、音效、粒子、弹幕都按 `ultimate_plan.h` 纯函数本地生成；伤害只结算一次：各机都用本机上发起者的
  副本当 owner 发弹，`jet_bay.cpp ShellMake` 里 `OnlineShotCounts` 对另一台机器的已登记玩家返回 false（`online_authority.h`
  `Authority`：`(net & kNetRemote)==0`），伤害置 0（H，代码路径已核对）。
- **追帧**（`src/ultimate_show.h`）：落后 ≤ 60 帧逐帧补放（每圈爆炸都会发生）；更多（中途接上、长卡顿）则 Seek：只按纯函数重建
  持续状态（法阵、冲击波、瞄准 / 汇聚光、主光柱、天色），跳过帧的一次性事件（雷、爆炸、闪光、音效、残火）丢弃。接收方起点 =
  发送帧 + 6 帧（约 100 ms 传输，**推测**，未测延迟）；落后发起者 > 30 帧时向前追，超前不回退。
- **多场并存**：最多 6 场，按 {origin, serial} 分槽；本机演出满或特效没装时 `UltimateStart` 返回 false，钩子不清发数，原版卫星光束
  照常发射（之前先清 0 再被拒，15000 pt 白花）。
- **天色引用计数**：第一场压暗时拍快照，最后一场到 32 s 还原；还原途中又开一场则沿用原快照。还原时逐项比对当前值与演出最后写入的
  目标值（容差 2 %），不一致的视为任务脚本中途 EnvFade 改过，保留不动（**推测**：渐变结束时值精确落在目标上，未实机读回）。
- **原版光束**：本机玩家的呼叫，演出开起来才清发数；其他机器玩家的呼叫，只有已收到（或在 4 s 内、原版弹还没生成前收到）发起者的
  演出才压掉原版光束，收不到（对端旧版本 / 没能力位 / 对端演出没开起来）就保留原版光束。远端武器副本也走 0x6A5A29 仍是**推测**
  （待确认 6）；原版单元首发前的等待帧数（压制窗口）是 M。
- **发起者失效**：发起者用游戏自己的 weak_ptr（持有弱计数）每帧验活，掉线 / 被删后停止发弹、删掉持续光束，只把特效播完；房间重组
  （peer 序号变）不打断远端演出，按键继续认。`MissionStart` 的 `ResetUltimate` 清空所有槽、天色引用与待压原版光束。
- 发起者屏幕提示：房间里有对端没宣告 `kExtUltimate` 时，开演后显示「部分队员版本旧，可能看到原版光束」。
- 离线测试 `tests/ultimate_net_test.cpp`：协议编解码与校验、多实例槽位、天色引用计数与「仍是演出写的值」判断、Seek 不补发一次性事件。
  联机部分**未实机**（无两机测试）。

## 第四轮：冒烟测试的两个问题（2026-10-20，日志 `vcE.log` / `cdbE.out`）

### 每帧 `FAULT ultimate: C0000005`

- 栈：`UltimateTick → CirclesFrame → Sound → SoundAt → Place → Dot`。`Sound` 播玩家录音（`UltClipThere`）时调
  `SoundAt(pos,nullptr,&p)`，`jetsound.cpp Place` 无条件 `Dot(vel,u)` 读空指针（H，代码与栈一致）。只在装了录音时触发；
  没有录音走 `PlayCue`，不经 `SoundAt`。
- 契约定为：`vel` 可空，表示静止声源（`src/crew.h` 注释）。定位算术抽到 `src/sound_place.h`（纯函数，监听者速度同样可空），
  `jetsound.cpp Place` 只转调它；离线检查 `tools/sound_place_check.cpp`（ctest），`tools/selftest.py sound_at_takes_a_still_sound`
  守 jetsound.cpp 里不再有直接读速度的副本。其它调用点：`vehsound.cpp` 7 处都传载具的速度数组（非空），`sazabi_sound.cpp`
  已自行换成 `kStill`。
- `GuardedTick` 吞掉异常后演出为什么还在：SEH 从 `Sound` 一路退出整个 `UltimateTick`。`Play` 开头已写 `s.done=f`，下一帧从
  `f+1` 接着播，所以演出照常推进、到时 `Finish`。但出错那一帧 `Sound` 之后的事件**永久丢失**（不是只晚一帧）：
  - 法阵展开音：同帧后面的法阵晚一帧生成（`tried` 还没置）；同帧的 `WavesFrame` 与阶段帧（蓄力 / 风暴 / 主炮 / 余波）整帧丢。
  - 风暴的雷声在第 0 道闪电之后：同帧其余闪电（含伤害弹）和汇聚光那一帧丢。
  - 主炮第 0 帧 `Sound(kUltBeam)` 在发主光柱之前，而主光柱只在第 0 帧或 Seek 落点发：**有录音时主光柱与核心光柱整段不出现**
    （伤害的 6 % 与主视觉一起没了）。
  - 每圈爆炸：弹已发，之后的震屏 / 闪光丢；冲击波：大冲击波的闪光与震屏丢。
  - 同一 tick 里排在后面的其它演出、追帧剩余帧与 `Finish` 只是推迟到下一帧。
  根因修掉后这些都不再发生；`GuardedTick` 仍只是最后一道防线（让出错帧重播会每帧重复出错，所以保持「跳过该帧」）。

### 日志 `damage 2400`（应为 12000）

- `UltimateStart` 用的是 `weapon+0x89C`。它由武器加载器 `0x68A920` 在 `0x68D6E3` 调星级曲线 `0xD7CE0` 求值后写入（`0x68D6EC`），
  不是 SGO 原值（H，反汇编）。`0xD7CE0`：曲线 `[基值, 模式, 星级槽, 星级上限, 比例, 指数, 整数位]`，
  `s = min(武器该槽星级, 上限)`、`x = 比例 × (s / 5)^指数`；模式 `& 3 == 0`（上升）取 `基值 × (1 − 比例 + x)`，`== 1`（下降）取
  `基值 × (1 + 比例 − x)`，其它为 0；不小于 0，保留基值符号（常量 5.0 在 `0x1765A38`，H）。即 5 星时等于基值。
- 呼叫武器由插件以 **0 星**拥有（`src/unlock_ported.h Own`：首次拥有清空 8 个星级字节，与 DLC 武器相同）。卫星模板 `eWeapon116`
  的 AmmoDamage 曲线 `[670, 8, 0, 10, 0.8, 0.7, 1]`：0 星 = 0.2 × 基值，**12000 × 0.2 = 2400**，与日志完全一致（10 星约 1.5 倍）。
  ReloadTime 曲线 `[3200, 21, 1, 10, 1.0, 0.3, 0]` 是下降型，0 星 = 2 × 基值：原来 15000 pt 实际要 **30000 pt**（同一根因，
  菜单的 `$0pt` 行用同一条曲线）。
- 不是测试者的运行时补丁造成的：`D:\APP\edf6-smoke-1020\run_installer.py` 的 `ultimate_sgo` 与源码一样写
  `AmmoDamage.items[0] = ULTIMATE_DAMAGE`，补的只是丢掉的名字循环与 `return`；2400 恰为 0 星的 0.2 倍。
- 修法（生成器写对字段，让游戏算出预期值）：`tools/call_weapons.py flat_curve` 把天罚 SGO 的 AmmoDamage、ReloadTime 两条曲线的
  「比例」置 0、基值设为 12000 / 15000，任何星级都得到这两个值；菜单行（伤害、`$0pt`）同样处理。`ultimate_sgo` 生成后用
  `star_value`（`0xD7CE0` 的 Python 版）在 0 / 5 / 10 星核对，模板曲线变成未知形状就拒绝安装。插件端不改：继续按游戏算好的
  `+0x89C` 分配伤害。12000 / 15000 本身是方案定的数值，未调。
- 其它呼叫武器也有同样的星级依赖（ReloadTime 都是下降型 `比例 1.0`，0 星时 pt 为基值的 2 倍，例如 KM6 模板 1020 → 2040），
  这次没改：它们的 pt 是按实机手感调过的，改成平直会改变已有平衡，留给用户决定。
- 未实机：改后的伤害日志应为 `damage 12000`，菜单显示 12000 / 15000pt（待下次冒烟测试确认）。

### 测试覆盖缺口

合并时 `ultimate_sgo` 丢了 `return dsgo.write(doc)`、`_text_row` 的 ultimate 分支丢了 `return row`，离线测试都没发现。
`tools/selftest.py` 新增：
- `every_call_kind_builds_synthetic`：`calls.CALLS` 每一种 brings（jets / helis / sub / vehicle / throw / gun / ultimate / gravity）
  用合成模板（无游戏，CI 能跑）调 `weapon_sgo` 与 `_text_row`：返回 bytes / Node，各自写的字段写对；天罚伤害与 pt 在 0–10 星
  都是预期值；非载具呼叫的文本行不被写上耐久（ultimate 分支漏 `return` 会掉进载具分支，被这一条抓到）。新增 brings 种类没有合成
  模板时测试直接失败。三种回归（去掉两个 `return`、去掉 `flat_curve`）已逐一验证会被抓到。
- `every_call_kind_builds_real`：有游戏时用真实模板与菜单行跑全部 calls（同 `stack` 的参数），并核对卫星伤害曲线仍是 0 星 0.2 倍
  的形状；无游戏跳过。

## 2026-10-25：特效与伤害同一组落点（用户：「天罚的伤害和实际落点是不是不一样啊，感觉外面的闪电爆炸没伤害」）

- 核对（代码，H）：雷暴的火花弹与 `Explosion` 特效同点（雷落点），主炮每圈的爆炸弹与 `BigExplosion` 同点；但二者在
  `StormFrame` / `CannonFrame` 里分开写，弹没造出来（某种弹没预载、对象没造成）时特效照画、伤害没有。开演只检查主光柱一种弹
  （`EmcRoundReady(ultBeam)`），火花 / 爆炸弹缺了照样开演。**地面冲击波**（光柱落地、最后一圈及其回波）沿波前每 5 帧 16 处
  扬尘，最远到 1.6 R = 352 m，**完全没有伤害**，且大部分在 220 m 的伤害区外——这就是「外面的爆炸没伤害」最直接的代码对应。
- 修复：`ultimate_plan.h BurstsAt` 是每帧地面爆炸的唯一清单：雷落点（就是 `BoltsAt` 的同一组数）、每圈爆炸、地面冲击波波前
  （`kWaveBursts` 处、每 `kWaveEvery` 帧、到 `kWaveBurstReach`）。`ultimate.cpp BurstsFrame` 对每一处：在同一点、以与该弹 SGO
  爆炸半径相同的半径（火花 40 m = `kSparkRadius`，圈爆 55 m = `kRingBlast`）发伤害弹，特效（`Explosion` / `BigExplosion` 按
  半径×0.5，波前再加扬尘）只在弹造出来时画；发起者还在而弹造不出来的计数，演出结束日志写出。开演（本机与远端）要求七种弹全部
  预载（`ShowRoundsReady`）。波前每处伤害 `kWaveShare` = 0.05（600，**未调**）。
- 联机：清单是种子与帧的纯函数，各机同一组落点；伤害仍只在发起者机器结算（`ShellMake` 的 `OnlineShotCounts`），其他机器的弹
  伤害为 0、特效照画；发起者离开后只播特效（原设计）。
- 离线：`ultimate_check` `BurstChecks`（雷落点一一对应、圈爆在圈上、波前只在地面波前且不超 1.6 R、每处都有伤害且半径等于
  弹的爆炸半径、同种子重放一致、单帧不超上限）；`selftest` 核对 `kSparkRadius == ULT_SPARK_RADIUS`。
- 未实机：火花 / 爆炸弹在游戏里是否确实造出并结算（日志 `N bursts, M not shown for want of their round` 可看）；波前伤害手感。
