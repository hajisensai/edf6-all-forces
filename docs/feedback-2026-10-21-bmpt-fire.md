# 2026-10-21 反馈：BMPT 双炮轮流开火、开火动画、机炮与导弹同时开火

用户原话：

> bmpt应该是双炮管轮流开火的，而且开火动画没了。而且没办法机炮和导弹一块开火，修一下。

本轮只改武器发射与开火动画（`src/payload.cpp`、`src/twin_cadence.h`、`src/barrel_recoil.*`、`src/npcai.cpp` 的副攻击规划、登记表与生成器），不碰相机 / HUD / 模型资源（另两个任务在改）。炮管与炮口一律从登记表和武器自己的炮口骨骼取，不写名字和坐标：换模型后照样成立。

## 1. 机炮和导弹不能同时开火

### 根因（已验证：代码与 git 历史）

- 2026-10-19 的修复 `0b3a0b22`「keep BMPT missiles on independent secondary attack」**从未合进 main**：它只在本地分支 `codex/bmpt-secondary-missile-20261019`（`git merge-base --is-ancestor 0b3a0b22 origin/main` 为假，main 上 `git log -S independent_secondary` 为空，也没有 PR）。
- main 上的实际路径：`FireOf`（`src/payload.cpp`）把 `EDF6VC_BMPT_ATGM_*` 当普通挂载（`IsStoreWeapon && !nativeControl` → `PayloadFire::store`）；V603 只有主攻击这一个原生触发源，`Switch` 把所有挂载排进主攻击的 R/LB 循环；选中导弹时 `RedirectPayload`（`else if(k==0)target=ws[choice]`）把 0 号炮位的拉动转给导弹、1 号炮位转给空（`to=nullptr`，`PullHook` 里 `if(!target)return;`）。所以选导弹 = 两门炮都不响，选炮 = 导弹不响：同一个触发源的二选一，不是缺按键。
- 此后 `15ad366e` 把 BMPT 从 3 挂点扩成 6 挂点（APDS 弹链 L/R、9M120F），同样都挂在主攻击的循环里。

### 修复

- `0b3a0b22` 落地（cherry-pick 后按六挂点重做）：`Store.independent_secondary`（`pylib/vcobjects.py`），`BMPT_ATGM` 与 `BMPT_ATGMF` 两种导弹都是；`gen_stores.py` 生成 `StoreSpec::independentSecondary`。运行时只在 V603（其输入 `0x6214A4` 只读主攻击，副攻击 `seat+0x2E0` 原生不读）上认它。
- `Switch` 拆成两个循环：主攻击（HE 原生弹链 / APDS 弹链，`Pick::weapon`，照旧重定向两门炮）与副攻击（两种导弹，`Pick::secondary`）。主攻击拉炮、副攻击拉所选导弹（`FireIndependentSecondary(v,seat,p->secondary)`），互不重定向、互不抑制；可同时按住。
- 切换（用户否决了第一版「R / LB 切最近扣过扳机的那一路」：「不太好吧，你想想怎么做。学学战雷」）：按战争雷霆的做法，每一路有自己的开火键和自己的弹种切换，与最近开过哪一路无关：
  - 战争雷霆地面载具：主炮开火、机枪开火（默认空格）、特种武器（反坦克导弹）开火（默认左 Ctrl）是三个独立的键；炮的弹种用数字键 1–4 直接选（BMPT 的 APDS / HEI 弹链就是按 1 / 2 切）；BMP-2/3 这类车用「Select secondary weapon」在 30mm 与 ATGM 瞄准之间切，但左 Ctrl 发射 ATGM 时可以留在主炮视角、炮照样能打。出处：[DefKey 战雷坦克键位表](https://defkey.com/war-thunder-tank-shortcuts)、[官方论坛 BMP-2 ATGM 讨论](https://forum.warthunder.com/t/to-use-the-tech-tree-bmp-2s-atgm-it-makes-you-to-switch-weapon-and-doesnt-let-you-use-the-autocannon-is-this-a-bug/254762)、[官方论坛 BMPT 弹链 1 / 2 切换](https://forum.warthunder.com/t/bmpt-ammo-bug/315204)、[官方 wiki 多炮塔射击](https://wiki.warthunder.com/mechanics/5392-how-to-shoot-on-multi-turreted-tanks)（均为社区 / wiki 资料，未在游戏内核对默认键）。
  - 本插件：开火仍是原版主攻击 / 副攻击两个键（游戏自己的键位设置）。主武器弹种 = `PlayerJetSwitchKey`（R）/ 手柄 LB，只切主攻击的循环；副武器导弹 = 新键 `VehicleSecondarySwitchKey`（默认 T）/ `VehicleSecondarySwitchButton`（XInput 位，默认 8 = 十字键右），只切副攻击的循环。数字键 1–9 在载具里已是 `SeatNumberKeys`（换座位），所以没学战雷的数字键直选；T 只在步行（编队）和普罗透斯上有用，载具里空闲；手柄座位按键字只有 A/B/X/Y/LB/RB/L3/R3 且都有功能，十字键上 / 下在开镜时是表尺，十字键右在载具里没有占用，从 XInput 读（与地图键同一读法）。地图打开时两键都不起作用。
  - HUD：挂载条同时点亮主武器当前弹种和副武器当前导弹（`StockArm.ownPick`），下方按键行写 `[R] 主武器：<弹种>   [T] 副武器：<导弹>`（手柄为 `LB` / `十字右`）；地图 M 的挂载菜单两路的当前项都标出，都能点。
  - 所选导弹用完（无弹且不再装填）自动换到另一种。
- `StockVehicleStores=0` 只关可选挂载（APDS 弹链属于挂载，随之关闭）；两种导弹是车自带的，照常工作。
- HUD / 瞄具：`PayloadTriggerWeapons` 的副攻击组只给所选的那一枚导弹；副攻击边沿后瞄具跟导弹。`PayloadAimWeapon` 对自带副攻击直接回它自己。
- 联机：`ApplyRemotePicks` 改用与本机相同的分组（`SwitchSecondary`、NPC 记录把自带副攻击标为 other），远端选弹只描述主攻击那一件，不会被导弹挤歪；导弹本身只在操作者那台机器拉（本机玩家或 `AiGunner` 为真的 NPC），射击经原生武器消息（每个 holder 一条）到各副本，副本不会再拉第二发。
- NPC：主炮与导弹分开规划（`NpcPayloadSelect` / `NpcSecondarySelect`，`npcai.cpp AimIndependentSecondary`），各自过原生 `slot 70 / 0x65F6F0` 的锁定与安全门；NPC 记下自己选的导弹，下一帧优先同一枚（锁定不白丢）。

## 2. 双炮管轮流开火

### 根因（已验证：EDF.dll 静态读取）

V603 的输入在主攻击上同时拉 0、1 两个 holder（`0x6214A4`，见 `docs/stock-payload-re.md` §2）。两门炮各自的 `FireInterval` 相同（BMPT 12 帧），装弹状态相同，于是同一帧一起出膛：一次两发、成对，从来不是交替。原版开普勒也是这样（它就是成对齐射）；BMPT 继承了这个类。

### 修复

- 登记：`GroundVehicle.slots`（每个武器是左炮 / 右炮 / 导弹，原来写在 `make_tracked_combat.BMPT_SLOTS`，现由登记表给出）和 `alternate=True`。`vcobjects.twin_barrels()` 把同一弹链的第 k 个左炮与第 k 个右炮配成一对（HE 一对、APDS 一对），`gen_stores.py` 生成 `kTwinBarrels`，`stores.cpp TwinPairOf` 按武器文件名查。
- `src/twin_cadence.h`（纯逻辑）+ `payload.cpp TwinBarrelFrame`：每帧所有拉动（玩家、挂载重定向、NPC、远端副本）之后、武器自己更新之前，读两门炮的原生状态：伙伴的冷却 `+0xE0C`（开火时置为 `FireInterval +0x36C`：`0x6981E0 / 0x6981F7`；更新时先减步长 `+0xE10` 再判 `FLT_EPSILON`：`0x693A58..0x693A95`）在本帧倒数之后若仍大于半个间隔，就清掉这一门的扳机锁存 `+0x139`（更新在 `0x6935B1` 读并清它）。稳态：左、右、左……每 6 帧一发，每门仍是自己的 12 帧；同帧都就绪时剩弹多的先打（相等则左），两条弹链用量保持一致。伙伴没弹（装填或打空）就不算伙伴，另一门按自己的射速打。
- 只认这四处原生字节都对上时才启用（`InstallPayload` 的 `twinOk`），否则保持原版成对齐射并写日志。
- 联机：门控在每台机器自己的拉动上做，玩家、NPC、远端副本同一规则；射击本身仍是各自 holder 的原生武器消息，伤害只在开火那台机器结算。

M163：一门 M61、HE/AP 两条互斥弹链（原生 `nativeControl`），不是双炮，不配对，行为不变；它的「开火动画」是六管转动（`vulcan_rotor.cpp`），本轮未改动。

## 3. 开火动画没了

### 核对结论：没有哪次提交删掉了原生开火动画（委托方的方向不成立）

- 原生开火动画只有一份：V603 类 slot 48（`0x621700`）对**任何** holder 的一发都播 CAS 玩家 `main` 的 `Fire` 状态（`0x116BFC0(veh+0x1170,[+0x2AC0],[+0x2AC8],1)`），片段 `fire_loop` 只有 `cannon_slide_l/_r` 两条轨道、共用 1 个平移通道，12 帧内最多 4.4 cm 的抖动；随后按挂点 `BodyRecoil` 给车体一下（`0x5F9FB0`）。
- `0b3a0b22`、`65408db5`、`2ccb4f3d` 都没有动它：私有 CAS 与原版只差 8 字节（两根雷达骨在 `doppler_radar_loop` 的 T/R 索引；本轮离线比较：均 4868 字节、8 字节不同，`main` 玩家的 `Default` / `Fire` 状态与 `fire_loop` 一致）；骨架指纹不变；碰撞拟合（`65408db5`）后已安装实体的 `animation_from_ragdoll` 映射对（物理体→骨骼）与原版 36 对相同，没有物理体写 `cannon_slide`；`2ccb4f3d` 只改网格原语的生成位置（提交说明称两车模型逐字节不变，本轮未逐字节复核），不涉及骨骼、CAS 或武器。`tests/twin_cadence_native.py` 钉住 slot 48 与 `Fire` 状态的调用。
- 真实的缺口有两处：
  1. **这段原生动画表达不了「哪一门在打」**：两门共用一个通道一起抖，幅度对 3.5 m 的 30 mm 炮管在第三人称几乎看不见；轮流开火后更显得「没有开火动画」。
  2. **同一辆车按生成方式后坐不一致（数据缺陷）**：呼叫来的 BMPT（`call_weapons.vehicle_sgo`）HE 两门保留原版挂点的 `BodyRecoil [0, 0.01]`，APDS 弹链却是 `[0, 0]`；测试场 / M 地图摆放的实体（`make_tracked_combat.vehicle_sgo`，自 `aaf35f9f` 起）所有挂点都是 `[0, 0]`：同一辆车，摆放来的开火时车体完全不动，换到 APDS 也不动。

### 修复

- `src/barrel_recoil.*`：每门炮在它自己那一发的那一帧把它的炮管骨骼沿炮管方向推后 12 cm，5 帧内回位（在 6 帧的交替间隔之内：一门复位时另一门正好开火）。骨骼取该武器第一个炮口挂的骨骼记录（即它 holder 的 `vehicle_weapon_setting` 骨骼），两条弹链同侧共用一根；不写骨骼名和坐标。开火判据只用武器自己的计数：本机打出的剩弹减少，或副本收到的操作者射击数 `+0x1544` 超过已播放数 `+0xBD0`（与 M163 转管同一判据）；装填回弹不算。写在 603 世界矩阵构建前的最后一步（复用 M163 转管已审计的 `0x621654` 钩子，钩子仍对每辆 603 原参调用一次原函数）；只叠加在本帧 CAS 留下的局部矩阵上，CAS 某帧没重写时从上次的底值算，不会累加；死亡、插件关闭、没有双炮的车（开普勒、M163）一概不碰。原生 `Fire` 抖动照旧保留。
- 后坐一致：两条生成路径同一规则（`GroundVehicle.slots`）——BMPT 的每个炮管 holder（两条弹链）用原版同侧挂点的 `BodyRecoil`，导弹 `[0, 0]`；M163 不变。

## 测试

- `tests/payload_runtime_test.cpp`（生产 `payload.cpp`）：六挂点 BMPT——两个循环；双扳机同帧时左炮 + 阿塔卡同时出、右炮等半个间隔；左、右、左；没弹的伙伴不挡；剩弹多者先打；`twinOk` 关闭时原版成对；LB 只切机炮弹链（副攻击刚开过火也一样）、导弹不动；十字键右只切导弹（主攻击刚开过火也一样）、弹链不动，按住只算一次，键盘座位不读手柄键，地图占键时都不切；读数同时给出两路的当前项与键；HUD 扳机组只含所选导弹；瞄具跟副攻击；所选导弹打空自动换；`StockVehicleStores=0` 时 HE 与阿塔卡照样同射；NPC 主炮与导弹分开、只拉被批准的导弹、炮也轮流；远端副本不拉导弹、炮的拉动也轮流。原 `0b3a0b22` 的三挂点用例保留。
- `tests/twin_cadence_test.cpp`：按原生冷却模型模拟 60 帧：严格左右交替、间隔恰为 6 帧、首发为左、弹链持平；停火后再开由剩弹多者先；单门有弹时按自身射速；不同弹链 / 无配对不受影响；半间隔边界。
- `tests/barrel_recoil_test.cpp`（生产 `barrel_recoil.cpp`）：左炮一发只推左炮骨骼、只沿炮管方向、同帧二次 pose 不重复；逐帧回位、CAS 不重写时不累加且静止时还原；右炮、APDS（同侧骨骼）、副本收到的射击；装填不算；关闭 / 残骸 / 无双炮车不碰。`vulcan_rotor_runtime_test` 加一条：同一钩子每次构建调用一次后坐。
- `tests/twin_cadence_native.py`（需 `EDF6_NATIVE_DLL`，本机实跑通过）：冷却 = `FireInterval`、倒数 `+0xE10` 与 `FLT_EPSILON`、更新读清 `+0x139`、拉动写 `+0x139`、V603 slot 48 对任意 holder 播 `Fire`。
- `tests/test_tracked_combat.py`：两种导弹是自带副攻击、不是弹链；`twin_barrels()` 恰为 HE 一对、APDS 一对且与生成的 C++ 表一致；请求与实体的每个炮管 holder `BodyRecoil` 等于原版同侧挂点、导弹为 `[0,0]`。
- 变异实测见提交说明。

## 未实测

- 没有进游戏：本机游戏被其它会话排队占用（`tools/gamequeue.py status` 时前面 3–4 个会话），未拿租约、未装 DLL、未写游戏目录。以下需要实机：轮流开火的实际观感与 6 帧间隔；后坐 12 cm / 5 帧是否合适；导弹锁定在副攻击下的提示音与锁定进度；双机房间里远端副本上的轮流与后坐（副本的冷却是否随网络射击推进未核，门控若在副本上失真只影响副本自己的拉动，不影响伤害结算）；APDS / HE 切换后车体后坐。
