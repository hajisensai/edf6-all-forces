# 安德柳沙燃烧子母火箭炮车：设计与逆向笔记（测试站 #37）

用户原话见 `docs/feedback-2026-11-10-andryusha.md`。证据等级：**H** = 反汇编 / 已有插件长期实测确认；**M** = 由多处一致的布局推出、未在游戏里单独验证；**L** = 推断，必须实机确认。

## 1. 需求对照

| 用户要求 | 做法 |
| --- | --- |
| 单独请求、不替换喀秋莎 | 新增 `EDF6VC_CALL_ANDRYUSHA`（`tools/calls.py`，载具请求，空降兵，档位同喀秋莎），武器表多 1 行（共 111 行） |
| 弹药大幅减少（参考现实变体安德柳沙） | `AmmoCount` = 3 轮齐射 × 16 = 48（喀秋莎 10 轮 160），`tools/make_katyusha.py` `ANDRYUSHA_SALVOS` |
| 子母弹：母弹本身爆炸 + 子弹燃烧，先炸后烧 | 母弹 `GrenadeBullet01` 自己爆炸 10 m；插件在爆点放出 8 枚子弹，每枚 = 一发 6 m 爆破弹 + 一发 `NapalmBullet01` 燃烧弹（同一弹道），燃烧弹落地留下持续燃烧的地面火 |
| 碰炸 / 近炸可切换 | 座位上按副武器切换键（`VehicleSecondarySwitchKey` / `VehicleSecondarySwitchButton`，默认 T / 十字键右）切换，HUD 发射架那一栏多一行引信状态 |

## 2. 车与武器（`pylib/vcobjects.py` `GROUND_VEHICLES['andryusha']`、`tools/make_katyusha.py`）

- 车体就是喀秋莎：`EDF6VC_ANDRYUSHA.SGO` 由同一个原版 V607 SGO 改出，`animation_model` 指向同一个 `EDF6VC_KATYUSHA.MRAB`（不另打一份 19 MB 的档案，`GroundVehicle.model`）。姿态、驾驶室上方最低仰角、高视角、落点 HUD、AutoTurret 的高抛自瞄都按武器的 `LockonTargetType` = 7304（`kMarkKatyusha`）认车，所以全部原样生效（**H**：这些功能都只读这个标记，`src/katyusha.cpp`、`src/launcher.cpp`、`autoturret/`）。
- 武器 `EDF6VC_ANDRYUSHA_ROCKETS.SGO`：喀秋莎火箭弹的弹速、重力、寿命、散布、连发数、模型全部相同（落点 HUD 与自瞄的弹道不变），只改 `AmmoCount`、`AmmoExplosion`（12 → 10 m，其余伤害由子弹带）与 `AmmoHitSizeAdjust`（引信标记，见 §3）。离线核对：`make_katyusha.check_andryusha`。

## 3. 引信标记与切换

- 母弹靠 `AmmoHitSizeAdjust` 的位模式识别：碰炸 `0x3F8F4A36`、近炸 `0x3F8F4A37`。两者都是喀秋莎的接触系数（1.5 m / AmmoSize 1.34）换掉低 12 位，接触球半径误差 < 1 mm（`tools/andryusha_check.cpp`）。弹体核心（bullet+0x140）+0xA34 存的就是这个系数（**H**：喀秋莎、齐射弹幕的标记一直这样认，`src/barrage.cpp`）。
- 切换：插件把座位当前武器的模板副本 `weapon+0x8C4` 写成另一个标记。依据：武器的弹药参数块在 `weapon+0x830`，`AmmoHitSizeAdjust` 在块内 +0x94（与 IFC 布局 +0x104 一致，也与 `common/edf/weapon.h` 的 `kWeaponAmmoSpeed` 0x894 / `kWeaponAmmoAlive` 0x898 同一套偏移）——**M**。之后发射的母弹从这里复制系数。**待实机确认**：写入后新发射的弹确实带上新标记（日志里切换时有 `ANDRYUSHA v=… fuse: …` 一行，之后每个爆点一行 `ANDRYUSHA airburst/impact burst at …`，应与切换后的引信一致）。
- 只有自己座位上的切换写本机武器；NPC 开的安德柳沙用这辆车武器当前的引信（新叫来的车是 SGO 默认的碰炸；玩家切过再下车，NPC 接着用切过的那个）。

### 3.1 联机同步（2026-11-10，房间协议 10，`src/andryusha_net.h`）

维护者 2026-11-10 对「安德柳沙的引信切换要不要在联机时同步（要同步就得再升一次协议）」的回答是「都要改」（原话见 `docs/feedback-2026-11-10-andryusha-sync.md`）。

- 通道：载具的 tag 15 消息（`src/seat_net.h`，与座位武器选择 `SPCK`、炮手座炮塔 `STUR` 同一条），新种类 `AFUZ`（`kMagic` 0x5A554641，v1）：24 字节公共头 + 发射架在载具挂点数组里的下标（各机副本相同）+ 引信（1 碰炸 / 2 近炸）+ 3 字节 0，共 32 字节。
- 谁发：发射架座位（座位 0）上的玩家所在的机器；座位上没有玩家时，由运行这辆车的机器（`IsOnlineAuthority`）发它手上那份（即它最后收到的引信）。同一时刻只有一个发送方；控制纪元 = 座位 0 乘员的 ReferenceId（`seat_net::SeatController`，空座 -1），收方用 `seat_net::Gate` 按发送者序号去重、拒绝旧纪元和自己作为发送方时收到的块。
- 何时发：切换当帧立即发（`AndryushaFrame` 排在 `LauncherFrame` 之后），此外每 1 秒补发一次（`kRefreshMs`）：**后加入的成员、丢包、换座**都在 1 秒内拿到当前引信；换了发送方（玩家上 / 下车）时新发送方立即发一次。
- 收到后：找到本机副本同一挂点的武器（喀秋莎标记 7304 且 `+0x8C4` 是安德柳沙的引信标记），写成同一标记，日志一行 `ANDRYUSHA v=… fuse from its speaker's machine: …`。
- **载具重生**：车被毁再叫来是新的武器对象，各机都从 SGO 默认碰炸开始，状态一致；任务结束 / 换房间清掉发送记录与收方水位（`ResetAndryusha`、`plugin_net::RoomGeneration`）。
- 旧版本：旧版收到 `AFUZ` 会静默忽略（`seat_net.h` 说明），其副本仍按碰炸分裂，所以房间协议 9 → **10**，旧版本不能进新房间（仓库规则：不做兼容）。
- **未实机验证（L）**：两台机器上切换后对方日志出现上面那行、对方看到的子弹散布随之改变；`AFUZ` 走的是载具 NetworkObject 的原生发送，与 `STUR` / `SPCK` 同一条路径（这两者已在联机用），但安德柳沙这个车型（喀秋莎的 402 类）是否已被 `ChainVehicle` 接上收端只在代码上确认（`AndryushaFrame` 在每台机器上都对它调用 `ChainVehicle`）。

## 4. 母弹的处理（`src/andryusha.cpp`）

- 挂 `GrenadeBullet01` 的每帧更新（vtable `0x17A17E0` 第 5 槽，原函数 `0x264AB0`；安装前核对 `0x236888` / `0x236899` 处的字节）——**H**（AutoTurret 的 Fuze 挂的是同一槽）。两插件都改为 `edf::ChainVtableSlot` 链式挂接，不再要求槽里是原版地址，装载顺序无关。
- 每帧前后对比 flags（core+0xAF4，0x1 = 已死）：标记是安德柳沙的母弹死亡时（碰到地面 / 建筑 / 敌人，或寿命到）记下位置，进队列，由游戏线程的 `AndryushaTick`（`GuardedTick(kStepBarrage, …)`）放子弹。弹体线程里不做射线。
- 近炸：游戏线程每帧对带近炸标记、正在下落、发射满 60 帧的母弹向下打一条 `MapRay`（长度够它再飞两帧），离地 ≤ 35 m 时置 `flags |= 0x20` 且 `age = life`，让游戏自己在这一帧引爆（和 AutoTurret 的 Fuze 同一方法，**H**）。地面以下 / 没有地面时不引爆，落地照碰炸处理。
- 高度、角度等常数都在 `src/andryusha.h`（`kBurstHeight` 35、`kArmFrames` 60、`kBomblets` 8、碰炸环 10～26 m、近炸散布半径 30 m），离线检查 `andryusha_check`。

## 5. 子弹（`pylib/vcobjects.py` `andryusha_bomblets`、`src/jet_bay.cpp` `AndryushaBomblet`）

- 两份子弹 SGO 走支援呼叫用的 `DemoIndirectFire` 弹种表（`EmcRound::andryushaBlast` / `andryushaFire`，`kEmcFiles`）：
  - `EDF6VC_ANDRYUSHA_BLAST.SGO`：`GrenadeBullet01`，爆炸 6 m。
  - `EDF6VC_ANDRYUSHA_FIRE.SGO`：`NapalmBullet01`，自定义参数照抄原版 `V_506HELI_UNDER_NAPALM01`（地面火是它生成的 `FlameBullet02`）。
  - 都按 0.5 m/帧、重力系数 1 抛射，落点由 `ImpactAims`（碰炸：爆点周围 10～26 m 一圈，均匀分布在 8 个扇区）或 `AirburstAims`（近炸：爆点正下方半径 30 m 的圆盘，葵花籽分布）给出，`ShellMake(…, straight=false)` 解抛物线，发射者记在车上（击杀归属、友伤与原版一致）。
- 伤害：爆破弹 = 母弹伤害 × 0.35，燃烧弹 = × 0.12（每枚子弹各一份）。随机种子取爆点坐标，同一爆点各机器分布相同。
- 联机：子弹走 `OnlineShotCounts`，只在开这辆车的那台机器上造成伤害，其它机器只看效果（与齐射弹幕相同，**H**）。
- **待实机确认（L）**：子弹模型是否显示（`Ammo_CustomParameter` p[14] = 0）；`NapalmBullet01` 从地面火开始到熄灭的持续时间与范围；8 × 2 发的音量是否太吵。

## 6. 已知未做 / 风险

- 联机引信同步已做（§3.1，房间协议 10），未实机验证。切换后到对方收到之间（一次网络往返）发射的母弹，在对方机器上仍按旧引信分裂。
- 母弹还在空中车就被毁时，弹不再归属到活着的车，这批子弹不放出（只剩母弹自己的爆炸）。
- 在飞母弹表 `carriers[128]` 满时，多出的母弹不分裂（只剩母弹自己的爆炸，日志一行）；同一帧爆点超过 `bursts[64]` 时多出的也不分裂。3 轮 × 16 发远低于上限，多辆车同时齐射才可能碰到。
