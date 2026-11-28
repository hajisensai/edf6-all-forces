# 刻印剑士（兵种 5，ZwRuneBlade）实现方案

上游：[plan-2026-11-28-zero-worldline-gear.md](../plan-2026-11-28-zero-worldline-gear.md) §2；接口约定：[plan-2026-11-28-new-classes-impl.md](../plan-2026-11-28-new-classes-impl.md)；逆向：[gauge.md](gauge.md)、[motion.md](motion.md)。写法照 [vehicler-plan.md](vehicler-plan.md)。

本文分三段：§1 对外接口（其他模块对着写）、§2 RB-CORE 的实现、§3 缺口。地址都是 EDF.dll（0x678CCB46）RVA。

## 1. 对外接口（RB-CORE 提供，其他模块只用这些）

### 1.1 以太槽 `src/runeblade_ether.h`

以太存在士兵 GameObject 自带的能量字段里（gauge.md §1：所有士兵都有，非飞翼兵种原版没人写）：

| 字段 | 含义 |
|---|---|
| `+0x304` | 上限（插件第一次见到这名剑士时写 `kEtherMax` = 100） |
| `+0x308` | 当前值 |
| `+0x2FC` | 1 = 耗尽（归零时置位，落地回充到 `kRecoverAt` 才清） |

所以原版能量补给弹（`0x5478A0` 加 `+0x308` 并夹到 `+0x304`）也会给以太回充，不用另写。

权威：**这名剑士所在的那台机器**（玩家的本机，`+0x128` bit 0 为 0）。以太和移动一样是本机模拟的东西，房主也只能看到本机报上来的结果；别的机器上的副本由通道 ZWRB 写进同一组字段（§2.3），只读。

```cpp
namespace crew::runeblade {
bool IsRuneBlade(const void* soldier) noexcept;        // 活着的 ZwRuneBlade 对象（soldier_factory ZwType == 5）
bool EtherOwned(const void* soldier) noexcept;         // 剑士且是本机的（只有本机能扣）
float EtherLevel(const void* soldier) noexcept;        // 当前值；非剑士 -1
float EtherMax(const void* soldier) noexcept;          // 上限；非剑士 -1
bool EtherDepleted(const void* soldier) noexcept;      // +0x2FC 耗尽
// 够就扣、返回 true；不够 / 耗尽 / 非本机剑士：不扣、返回 false。
bool EtherSpend(void* soldier,float amount) noexcept;
// 全部扣光（坠星「耗尽以太槽」），返回扣掉的量（0：没有可扣的）。扣光即置耗尽。
float EtherDrain(void* soldier) noexcept;
// 按帧持续扣（七星按住时浮起光阵用）：扣 min(amount, 余量)，返回实扣；扣到 0 置耗尽。
float EtherBleed(void* soldier,float amount) noexcept;
}
```

用法约定：
- 七星（R3）：每浮起一个光阵 `EtherSpend(s, 每阵耗量)`，返回 false 就不再浮起；松手齐射不再扣。
- 坠星（M4）：空中蓄力开始时检查 `EtherLevel(s) > 0 && !EtherDepleted(s)`；下砸那一刻 `EtherDrain(s)`，伤害按返回值和高度算。
- 都只在本机剑士上调（`EtherOwned`）。远端副本上调用一律返回 false / 0，不会把别人的槽扣掉。
- 数值（各动作的耗量）由各武器模块自己定，写进各自的 plan；RB-CORE 的数值在 `src/runeblade_ether_logic.h`。

### 1.2 近战连段 `src/runeblade_combo.h`（状态机：`src/runeblade_combo_logic.h`）

RB-CORE 只给状态机和每名剑士一份运行时状态，不做任何具体武器。一把近战武器用一张 `combo::Spec` 描述：

```cpp
namespace crew::runeblade::combo {
struct Stage {
    std::uint16_t length;      // 这一段的帧数（出刀 + 收招）
    std::uint16_t hitFrame;    // 出刀帧：这一帧 Output.hit = true（武器模块在这帧出伤害弹）
    std::uint16_t cancelFrom;  // 从这一帧起，缓冲的输入可以接下一段
    std::uint16_t window;      // 段结束后还等输入的帧数（过了就断连）
    std::int8_t next;          // 下一段的下标；-1 = 连段终点
    std::uint8_t flags;        // kAirOnly / kGroundOnly / kNoInsert / kLandEnds
};
struct Spec {
    const Stage* stages; int count;
    std::int8_t airEntry;      // 空中第一下按攻击进哪一段（-1：空中和地面一样从 0 开始）
    std::int8_t airBranch;     // 空中派生：地面连段途中离地后再按攻击，接到这一段（-1：没有）
    std::uint16_t insertFrames;// 远程插射时连段时钟停多少帧（萤火的一枪）
};
struct Input { bool attack; bool airborne; bool ranged; };
struct Output {
    int started=-1;   // 这一帧开始的段（-1：没有）
    bool hit=false;   // 这一帧是当前段的出刀帧
    bool insert=false;// 这一帧的远程插射被接受（武器模块照常开火，连段不断）
    bool ended=false; // 连段这一帧断了（收招完没接上、或落地断）
    int stage=-1;     // 当前段（-1：空闲）
};
}
namespace crew::runeblade {
// 本机剑士用：每帧由近战武器模块调用一次（它自己的武器帧里），喂这一帧的输入，拿到要做的事。
// 第一次调用时绑定 spec；换了 spec（换武器）自动从空闲开始。非本机剑士返回空 Output。
combo::Output ComboFeed(void* soldier,const combo::Spec* spec,const combo::Input& in) noexcept;
bool ComboActive(const void* soldier) noexcept;   // 正在连段（某段里或等输入窗口里）
int ComboStage(const void* soldier) noexcept;     // 当前段；-1 空闲
void ComboCancel(void* soldier) noexcept;         // 硬断（受击倒地、上车）
// 远端副本播放段的回调（RB-MELEE 注册）：别的机器上的剑士开始第 stage 段时，本机对它的副本调用。
using RemoteStageFn=void(*)(void* soldier,std::uint8_t weaponTag,int stage) noexcept;
void SetRemoteStageSink(RemoteStageFn fn) noexcept;
// 本机剑士开始一段时，ComboFeed 自动发给全房间（weaponTag = ComboFeed 调用前设的 ComboTag）。
void ComboTag(void* soldier,std::uint8_t weaponTag) noexcept;
}
```

规则（`tests/runeblade_test.cpp` 逐条测）：
1. 空闲时按攻击：在空中且 `airEntry >= 0` 进 `airEntry`，否则进 0（`kAirOnly` / `kGroundOnly` 段不符合时这一下不出）。
2. 段内按攻击先缓冲；到 `cancelFrom` 帧且有缓冲就接下一段。接哪段：在空中、`airBranch >= 0`、当前段不是空中段 → `airBranch`（空中派生）；否则 `next`。`next == -1` 时缓冲作废，收招完断。
3. 段结束（`length` 帧）后进等待窗口；窗口内按攻击直接接下一段，过了 `window` 帧就断（`ended`）。
4. 远程插射：连段中（段内或窗口内）`ranged` 按下，当前段没有 `kNoInsert` → `insert = true`，连段时钟冻结 `insertFrames` 帧；冻结期间攻击照样缓冲。空闲时按远程不归连段管（`insert = false`，武器照常开火）。
5. 段带 `kLandEnds`（急坠下劈这种）时，落地那一帧这段收掉、连段断。

### 1.3 踏阵（RB-CORE 自己做完，不对外）

- 地面跳：原版跳（不耗以太），记一次「起飞」给特效。
- 空中按跳：踏阵一步，耗 `kStepCost`，再跳一次（`+0x1294` 跳速 / `+0x1290` 请求，`0x575A7A` 消费并由游戏自己同步给其他玩家），水平速度按摇杆方向给到至少 `kStepPush`；两步之间至少 `kStepGapFrames` 帧。
- 空中按住跳且在下落：滑翔，下落速度夹到 `-kGlideSink`，每帧耗 `kGlideDrain`。
- 槽不够一步、或耗尽：空中按跳什么都不做（不降级成别的动作）。
- 落地 `kRefillDelay` 帧后按 `kRefillPerFrame` 回充。

### 1.4 HUD `src/hud_runeblade.inc`

本机玩家是剑士时，屏幕右侧画「以太」标题、数值、条（耗尽时琥珀色并写「耗尽」，滑翔时青色）。读的是 `RuneBladeHudNow` 的快照（游戏线程写、绘制线程读，seqlock 一样的整份拷贝）。

## 2. 实现（RB-CORE）

| 文件 | 内容 |
|---|---|
| `src/runeblade_ether_logic.h` | 以太与踏阵的纯逻辑（`Ether`、`StepIn` / `StepOut`、`StepFrame`），数值常量 |
| `src/runeblade_combo_logic.h` | 连段状态机（§1.2） |
| `src/runeblade_net.h` | ZWRB 线格式（纯） |
| `src/runeblade_ether.h` / `src/runeblade_combo.h` | §1 的对外 API |
| `src/runeblade.h` / `src/runeblade.cpp` | 帧钩、扩展状态、通道、HUD 快照、安装 |
| `src/hud_runeblade.inc` | HUD |
| `tests/runeblade_test.cpp` | 纯逻辑单测 |

### 2.1 帧钩

和载具手 `vehicler_foot.cpp` 一样，链 AssultSoldier 活 vtable 的 slot 5（`0x17CDF28` +5*8；克隆在每个对象造出前从活表复制，所以对剑士也生效）。只认 `vptr == soldier::zwVtables[1]`（一次比较，别的士兵直接走原函数）。

- 本机剑士（`+0x128` bit 0 = 0）：原 slot 5 之前读这一帧的跳按下 `+0xD76`、按住 `+0xD77`、着地 `+0x711`（2 地面）、移动摇杆 `+0xD50`、视线偏航 `+0x1244`，跑 `StepFrame`，需要踏阵就写跳跃请求和水平速度 `+0x6B0/+0x6B8`；原 slot 5 之后滑翔夹 `+0x6B4`。死亡 `+0x2E8`、乘车 `+0x380` bit 0x80、附着 / 布娃娃 `+0x39C` 时什么都不做（不回充、不踏阵）。
- 每名剑士的插件状态放 `soldier::SetExtension`（第一次帧里分配，对象析构时释放）：步数、间隔计时、滑翔、着地帧数、连段状态、序号。
- 远端副本：不跑踏阵，字段由 ZWRB 写。

### 2.2 联机通道 ZWRB（`runeblade_net.h`）

`plugin_net` 通道 "runeblade"，magic `'ZWRB'`，版本 1，固定 48 字节：

| 偏移 | 内容 |
|---|---|
| 0 | magic |
| 4 | version |
| 8 | kind：1 state（BroadcastState，key = `StateStream::runeBlade` + 本机玩家序号）、2 step（可靠）、3 stage（可靠） |
| 12 | 玩家引用（net_object_ref.h，必须是玩家） |
| 44 | state：以太 u16（×100 定点）、flags u8（bit0 耗尽、bit1 滑翔）、序号 u8；step：步序号 u8、种类 u8（0 起飞 / 1 空中踏阵）；stage：武器 tag u8、段号 u8 |

- 发：每帧在通道 update 里对本机剑士发 state（值变了且隔 `kGapMs`，或心跳 `kHeartbeatMs`）；踏阵 / 起飞 / 段开始当场发可靠事件。
- 收：`SenderIsPlayer`（发送者必须是这名玩家本人），解析到对象且不是本机的才写字段 / 调回调。
- 新成员加入（`joined`）：下一帧重发 state。

### 2.3 为什么不是房主权威

以太和踏阵是玩家本机模拟的移动资源，跳跃本身也由游戏在本机发起、自己同步（`0x575AB9` 起的那段）。房主拿不到更准的数据，改成房主判定只会多一个往返的输入延迟。所以权威放在玩家本机，房主和别的客人只收它的结果，并且只收**这名玩家本人**发的（`SenderIsPlayer`）。

## 3. 缺口（这次没做完的）

1. **光阵特效**：脚下起飞光阵、空中每步的光阵、滑翔光痕都还没有贴图 / efarc。现在踏阵、起飞在本机和远端都会调 `SetStepCueSink` 注册的回调（没人注册就什么都不画），特效模块做好后注册即可，不用改 RB-CORE。
2. **摇杆方向**：`+0xD50` 是 (−LX, 0, −LY)（sidecar.cpp 的注释），换成世界方向的符号是按 `enemy_control.cpp` 前向 (sin yaw, 0, cos yaw) 推的 [推断]，要实机确认一次；反了只改 `runeblade_ether_logic.h` 的 `WorldPush`。
3. **HUD 字样**：插件自己画以太条，没动原版 `gauge_setting`。如果兵种 SGO 以后设成 `[1,1]`，原版能量槽也会画同一组字段（会重复），到时二选一。
4. **连段的动作与出刀**：状态机只给时机（`started` / `hit` / `insert`），播哪个 CAS 动画、出哪种弹是 RB-MELEE 的事；新 CAS 数据事件的制作工具仍缺（motion.md §5 第 3 条）。
5. **数值**：`kStepCost` 等全部是初值，没开游戏调过。
6. **全部未实测**：按维护者要求实机测试放到最后一次采齐（drive.py 一次启动：剑士开局、空中连按跳 6 次看第 6 次被拒、落地回充、客人看房主剑士的以太值）。
