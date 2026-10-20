# 终极支援「天罚」实现方案（2026-10-19）

用户原话与要求见 [feedback-2026-10-19-ultimate.md](feedback-2026-10-19-ultimate.md)：特效拉满、昏天暗地；性能、误伤不设限；
魔法阵必须有，原版素材不行就自制；清区终极技能，pt 很高。

置信度：H = 反汇编 / 源码直接读到；M = 推断；L = 猜测，待实机。

## 演出时间轴（约 40 s；10-20 首测后放慢：「来的太快了，应该慢一点，不然没那种宏状感」，初版 25 s）

| 阶段 | 时长 | 内容 |
|---|---|---|
| 蓄力 charge | 0–12 s（6 个法阵约 7 s 内逐个展开，天色 8 s 压暗） | 环境渐变：雾色转红紫、雾距压近、环境光 / 主光压暗、天空变色、色调压低；地面法阵 3 层逐层展开、正反旋转，天空同心法阵升起；8 道细光从高空圆周会聚到目标点并逐帧变粗；地面 StencilLight 红光脉动；粒子光点上升 |
| 雷暴 storm | 12–22 s | 半径内每帧随机落雷（落点爆炸 40 m，铺满整个法阵；首测嫌「攻击范围那么小」）（LightningBullet01 竖直劈；不行退 PlasmaBullet01 + 闪光），配 Explosion、Quake；法阵加速旋转、亮度脉动 |
| 主炮 cannon | 22–30 s | VSRADON 派生粗光柱（粗 ≥ 48、颜色按主题）2 s 内贯下变粗；光柱满粗后由中心向外每 k 帧一圈 M 个大爆炸，半径逐圈增大，形成环形冲击波；强 Quake |
| 余波 after | 30–40 s | Blazing 残火、Smoke 烟墙；法阵淡出、删除；32 s 起天色 8 s 还原（最后一圈爆炸后约 10 s 复原） |

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
