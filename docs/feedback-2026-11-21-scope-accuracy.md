# 2026-11-21 反馈：有瞄准镜的武器做开镜 / 不开镜精度差分

**离线实现与离线测试，没有在游戏里实测。** 逆向与实现细节见 `docs/scope-accuracy-re.md`。

## 原话

玩家（只当证据读）：

- 「给有瞄准镜(即开镜)功能的武器做一下不开镜和开镜时的精准度差分(狙击枪降低不开镜精准，开镜时恢复精度，除狙击枪外其他武器如突击步枪不开镜时只略降低精度，开镜时略增加精度)」

## 现状（改之前）

原版的散布只看武器的 FireAccuracy：每发子弹在半角 `E14 × FireAccuracy` 的锥里随机偏一下，**开不开镜完全一样**
（散布路径 `0x691560` 不读开镜标志 `weapon+0x780`）。狙击枪腰射和开镜一样准。

## 这次改了什么

- **只对本机玩家亲手打出的子弹**（含本机分屏的第二个玩家）：NPC、远端玩家的重放、载具武器都保持原版。
- 有瞄准镜的武器（SecondaryFire_Type = 1）分两类，按**武器表的类别**判（SGO 参数分不出狙击枪与步枪）：
  - **狙击枪**（游骑兵 SniperRifle，类别 2；空降翼兵 Pale_Long 长射程，类别 104；EDF5 / EDF4.1 移植的狙击枪也登记在这两类）：
    开镜 = 原版；不开镜散布锥 = 原版 × `ScopeSniperHipSpread`（默认 4），且至少 `ScopeSniperHipMinSpread` 弧度
    （默认 0.015 ≈ 0.86°，原版散布为 0 的狙击枪腰射也会散开）。
  - **其它有镜武器**（突击步枪、火箭筒、导弹、Fencer 重炮、空袭兵的呼叫类等）：不开镜 × `ScopeHipSpread`（默认 1.25，略散），
    开镜 × `ScopeAimSpread`（默认 0.8，略准）。原版散布为 0 的（空袭兵的标记 / 呼叫类大多如此）保持 0。
- 做法：射击入口每发子弹之前临时改这把武器这一发用的散布值，原版算完这发立刻写回；不常驻改任何武器字段，所以武器菜单数值、
  联机同步模式、蓄力武器的蓄力散布都不变。狙击枪的类别在玩家建武器时（`0x5A4484`）从武器表行里记下。
- ini（`EDF6VehicleCrew.ini`，`InfantryScope` 下面）：
  - `ScopeSpread=1`（0 = 原版）
  - `ScopeSniperHipSpread=4.0`（1~20）
  - `ScopeSniperHipMinSpread=0.015`（弧度，0~0.2）
  - `ScopeHipSpread=1.25`（1~5）
  - `ScopeAimSpread=0.8`（0.1~1）
  
  超范围的值夹到范围端点并写日志；`CONFIG scopeSpread=…` 一行打印五个值。
- **联机按房主的设置**：房主的这组值经 EDF6Coop 成员属性 `EDF6X_AF_ROOM`（房间字版本 2，`s=` 字段，特性位 `kFeatScopeSpread`）
  发给客人，客人每发子弹用房主的值；房主的字还没收到时用本机 ini，房间菜单会提示。房主与本机设置不同时房间菜单显示
  「按房主设置：狙击枪不开镜散布 ×…」。理由：子弹散布在射手本机算、打敌人的命中也在射手本机判，各读各的 ini 会让一个成员打原版散布、
  别人被削弱。房间字升到版本 2 是破坏性变更，所以全军出击房间协议同时升到 11（`src/mod_room.h` `kAllForcesRoomProtocol`）：旧版全军出击进不了新版的房间，不会出现旧版成员打原版散布、招募 / 掩护退回本机 ini 的情况（EDF6Coop 经 `EDF6AF_RoomPrefix` / `EDF6AF_RoomProfile` 读取，不用改）。
- 远端玩家看到的是原版散布的重放（只是画面）；友伤由被打方按重放判，与射手本机的弹道会有一点不一致。
- 签名不符（`0x5A4479` 调用点字节、`0x691AFA` 散布读取、射击入口钩子）时写一行 `SCOPESPREAD REFUSED …`，该功能不装，散布保持原版。
- 不依赖步兵瞄准镜画面（`InfantryScope` 0 / 1 / 2 三种模式都一样），只读武器自己的开镜标志，没有改 `infantryscope.cpp`。

## 日志

- `SCOPESPREAD installed …` / `SCOPESPREAD REFUSED <原因>`
- `SCOPESPREAD weapon <指针> row <行> cat <类别> sniper <0|1> scope <0|1> accuracy <FireAccuracy>`（每把玩家武器建出来时一行）
- `SCOPESPREAD <指针> zoom <0|1> cone <原版锥> -> <这发的锥> (cat <类别>)`（每把武器每次开镜状态变化时一行）
- `SCOPESPREAD row <行> out of table`（这把武器保持原版）
- 联机：`HOSTCFG playing by the host's ScopeSpread=…`

## 测试

- `scope_spread`（`tests/scope_spread_test.cpp`）：倍率选择、下限、关掉、写法（只写 E14 / 写 E14=1 + FireAccuracy）、范围夹紧。
- `scope_spread_guard`（`tests/scope_spread_guard.py`）：`docs/weapons.csv` 里类别 2 / 104 正是狙击枪两类、移植狙击枪的类别、
  ini / crew.h / plugin.cpp 五个键一致、钩子顺序、房间字特性位。
- `scope_spread_native`（`tests/scope_spread_native_audit.py`，需要 `EDF6_NATIVE_DLL`）：钉住 `docs/scope-accuracy-re.md` 里实现依赖的 H 级地址。
- `coop_attr`：房间字版本 2 的格式、解析、夹紧与「客人按房主」的决定。

## 待实测 / 待定

- 默认倍率（狙击不开镜 ×4 且至少 0.015 rad；其它不开镜 ×1.25、开镜 ×0.8）需要维护者或玩家实测定值。
- 实机日志确认 `SCOPESPREAD weapon …` 的类别与武器一致、`zoom` 行的锥变化符合预期、步兵武器的 `E14` 为 1.0。
- Fencer 的 Heavy_Cannon（如加利亚重炮）目前按「其它有镜武器」处理；玩家若认为它也算狙击，再把类别 204 加进 `SniperCategory`。
- HUD 准星是原版固定美术，不显示不开镜时实际变大的散布锥。
