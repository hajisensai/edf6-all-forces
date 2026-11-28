# 新兵种（class 4 载具手 / 5 刻印剑士）联机逆向笔记

EDF.dll Steam 0x678CCB46，地址均为 RVA。GS = GameStatus 全局 0x20B2890。只读静态逆向（vanilla-code 索引汇编为准），未开游戏。
置信度：[确认] = 汇编逐条核过；[推断] = 由调用关系 / 伪代码推出，未逐条核；[猜测] = 无直接证据。

## 0. 结论摘要

- 兵种在联机里走两条独立的路：
  1. **任务开始同步 MissionSync 记录**（打包 0x78E9A0 → 房主转发 0x78D0E0 → 各机解包 0x790600 → GS+0x14C78+idx*0xD4 → 建人 0x591130/0x595960/0x5A3F90，预载 0x59DCD9）。兵种以 **int32** 明文传输，原版三处（打包 / 转发 / 解包）**都不校验兵种范围**，class=4/5 会原样到达每台机器。[确认]
  2. **房间成员面板**走 EOS 大厅成员属性（键 "st" = 兵种，"w0".."w5" 武器，"dr" 护甲等），不经过 MissionSync 记录。[确认 键名；字段映射 推断]
- 越界 / 出错不在线路上，而在**每台机器的消费端**：按兵种索引的 SGO 查表（0xE3680 / 0xE34E0 / 0xE3580，无界）、按 4 个兵种分配的 HUD 名牌表（0x806FE0 只建 4 项）、GS 里按 4 兵种排布的装备 / 颜色 / 护甲数组（本机打包侧）。
- 0xD92B0（HasContent，GS+0xE0 的 std::set）只管任务内容 id，**与兵种无关**；进房没有任何原版兵种门。[确认]
- EDF6Coop 不自己序列化兵种，搭原版记录；它的 weaponguard / armor 都写死 4 个兵种，class≥4 的记录**放行不护**（不是拒绝，也不越界），但护甲上限等扩展逻辑对新兵种失效。[确认]
- 推荐：记录里保留真实 class 4/5（符合「新兵种不换皮」），由房间协议号 +1（kAllForcesRoomProtocol 26→27）+ SEARCH_TYPE 前缀隔离保证全员同版插件；插件在各机修补消费端；新兵种额外状态走 plugin_net 通道（`joined` 发全量）或 EDF6X_ 成员属性。EDF6Coop 需同步改（兵种数 4→6）并提高所需 Coop 版本。

## 1. 打包 0x78E9A0（本机自己的记录）[确认]

- 本地记录在 rbp+0x20（ctor 0x78CEA0）。
- +0 = 成员 +0x48 玩家序号（0x78ECBF）
- +4 = 兵种 = GS+0x6E90（0x78EB20）
- +8 = 外观 GS+0x6E94
- +0xC..+0x24 = 武器 6 个，取 GS+0x6E98+class*0x18（0x78EB40 起，**class 无界**）
- +0x24 = 护甲使用量 GS+0x6F88+class*4
- +0x28..+0xA8 = 颜色，4 次循环，基址 GS+0x6FAC/0x6EEC/0x7070/0x6FB0 + class*0x620（0x78EC53 `imul rcx,r9,0x620`，**无界**）
- +0xA8 = 星级 6×8 字节，GS+0x6E68+(id+0xA69)*12
- 总护甲 GS+0x6E68+class*4 放 rsp+0x34。
- 序列化 0x773840(&armor,&rec,stream,cap)，cap(r15d) 在 GS+0x14C50>0 时来自 0xD8130。
- 线上格式：int armor, int min(armorUse,cap), int8 index(0x12B5790), int class, int outfit, 24 个 rgb float（alpha 不发）, 6 个 int 武器, 0x30 字节星级。≤222 字节（Coop packetfit.h 注释一致）。
- 含义：class 是 int32，4/5 不会被截断；但本机打包时 GS 里 class*0x18 / class*0x620 / class*4 的数组只为 0..3 排布，class=4/5 会读到相邻字段（GS 内部，不崩但内容是垃圾）[推断]。这是「装备」主题的问题（本机 GS 布局），联机层只需保证送出去的值来自插件自己的新兵种存档。

## 2. 房主转发 0x78D0E0 [确认]

- 只收 0≤成员+0x48<4 的用户（Extend 放宽）。
- 0x78D6E3 / 0x78D6FA：0x773740 反序列化 → 0x773840 重序列化，护甲上限取 `[rsi+0x10]`。
- **没有兵种检查**，class 4/5 原样转发。

## 3. 各机解包 0x790600 [确认]

- 0x792120（MissionSync AS 绑定，由 0x792140 调）→ 0x790600。
- 每条记录 0x773740 反序列化，无兵种校验；alpha 置 1.0。
- index<4（0x7908A0）时把 rec+4 起 0xA4 字节复制进 GS+0x14C78+i*0xD4。
- r15 = GS+0x14B80：+0xF8 = class（即 GS+0x14C78），+0x118 护甲使用，武器循环，+0x19C 星级（= rec+0xA4）。
- GS 记录布局（0xD4）：+0 class, +4 outfit, +8..+0x20 weapons[6], +0x20 armorUse, +0x24..+0xA4 颜色 8×float4, +0xA4 星级 0x30。

## 4. 读记录兵种的消费端（每台机器都会跑）

| 地址 | 作用 | 对 class=4/5 的后果 | 置信度 |
|---|---|---|---|
| 0x59DCD9（预载） | idx<4 后 `ebp=[rec+0]`，0xDE470(outfit)，0xE34E0(GS+0x130,class,outfit) → 0x7A3780 资源请求（0x59DD27） | SGO 相对偏移无界读：CONFIG.SGO SoldierInit 没有 4/5 项就读到别的节点，请求错误/无效资源 | 调用 [确认]，后果 [推断] |
| 0x595960（为 0x591130 填 desc） | 0x5959E1 idx<4；r12d=class(GS+0x14B80+0xF8)；武器 +0x100..+0x114；护甲 +0x118；0x595ACD 外观；desc+8=class；0xE3680(GS+0x130,class) 槽数（0x595B1F）；→ 0x5A3F90 建人 | 同上无界 SGO 读；建人按 class 选玩家类 / 武器槽（5 个玩家类 vtable 见 Coop netplayer_game.h）——class 4/5 走哪个类需「建人」主题确认 | 调用 [确认]，建人分支 [推断] |
| 0x7FFBD0（HUD 取成员序号+兵种） | 读 GS+0x14C78+[user+0x48]*0xD4，序号无界 | 不越界（记录本身合法），把 4/5 交给调用方 | [确认] |
| 0x82A249（调用方 0x82A205） | switch class 0..3 取 float，default 0.0 | 安全 | [确认] |
| 0x804C90 HudPlayer_MultiPlayStatus 名牌（vtable 0x17F6C98 slot3=0x8077B0） | 0x804EC4/0x804ECF 只查 idx≥0、class≥0；0x8056E2 读 `class*0x30+[this+0xB8]` | [this+0xB8] 由 ctor 0x8047C0 → 0x806FE0(this+0xB0) 分配**恰好 4 项**（0xC0 字节，循环 `i+1<4`，每项 wstring+颜色）。class 4/5 = **堆越界读**（wstring 指针为垃圾 → 极可能崩） | 越界 [确认]，崩溃 [推断] |
| 0xE3680 / 0xE34E0 / 0xE3580 | `[GS+0x130+0x20]` SGO 相对偏移算术，无界 | CONFIG.SGO 补了 4/5 项就安全（另一主题） | [确认] |

另有约 30 个函数按 GS+0x6E90（本机兵种）索引（例：0x5914C0, 0x595C80, 0x59DE50, 0x70FB11, 0x747ED0, 0x7865A0, 0x7DA480, 0x7DB520, 0x7DD680, 0x8FDFE0(0x8FEF15→0x8A6580), 0x901100, 0x938760）——这些是本机 UI / 存档侧，归「装备 / 兵种数」主题，不是联机特有。

## 5. 房间成员面板 [确认 键名；字段映射 推断]

- 本机发布：HUiRoom update 0x901100（第 99 行附近）→ 0x747730 → 0x7445F0 → 0x747ED0 写成员属性。
- 键指针表 0x1F75CF0：w0..w5, cr, **st（0x1F75D28）= 兵种**（0x74809A 发 GS+0x6E90）, rn, df, mn, al, wl, **dr = 护甲**（0x7480AC，0xD7BD0(class)）, vc, lo。
- 0x747F70：武器读 GS+0x6E68+((class+2)*6+slot)*4，按兵种索引。
- 远端解析：0x744AB0 解析 "st"（0x745124 引用键，[r13+0x30]）→ 成员+0x30；0x747340 读 +0x30（-1→0，ret 2）；0x901100 第 702 行写 MemberInfo+0xC（+8 是「有兵种」标志）。
- 面板填充 0x902030 → 0x8F7690：0x8F76E6 `0x959170(class)` → 0xE3580(GS+0x130,class) 取 SoldierType 文本键 → 0x7A3500；0x8F7860 `0xE3680(class)` 取武器数；武器经 0xE1CF0 查。class 4/5 = 无界 SGO 读。
- 0x901848：本机兵种与 room+0x95C 比较触发刷新（无界无关）。
- 0x786BB0（0x7865A0 内）：UI 对话框参数，用 class 与 0xDE250 护甲；仅本机。

## 6. 0xD92B0 进房内容检查 [确认]

- HasContent，查 GS+0xE0 的 std::set，7 个调用点（docs/mission-list-re.md §7、docs/room-start-gate-re.md），插件在 src/edf5campaign.cpp 挂钩，内容 id 65..71。
- 只管任务内容，没有兵种门。可借鉴其模式但**不是**兵种拒绝点；兵种的「全员同版」由房间协议号实现（见 §8）。

## 7. EDF6Coop（hajisensai/edf-coop-stable，origin/main d2b95fc = 2.9.0）[确认]

- 不自己序列化兵种 / 装备，搭原版 MissionSync 记录与成员属性。
- `multislot/src/weaponguard.h`：`kWeaponClasses = 4`；GuardRoomWeapons 挂 0x744AB0（属性解析，未知武器 id→空）；GuardLoadout 挂 0x790600（未知 id 换成本机同兵种同槽武器）；注释 "A record of no class 0..3 is left"，weaponguard.cpp:128 `if (weaponClass < 0 || weaponClass >= kWeaponClasses) return swaps;` —— class 4/5 记录**放行不护**。kLoadoutClass=0x0, kLoadoutWeapons=0x8, kLoadoutLevels=0xA4；kEquipped=0x6E98（每兵种 6 个，相隔 0x18）。
- `multislot/src/armor.h/.cpp`：`kSoldierTypeCount = 4`；armor.cpp:62/195 `soldier >= kSoldierTypes → return false`，:141 只填 4 个上限；读 GS+0x6F88+(slot*0xF98+class)*4（0x78EC3D, 0x595CD9, 0x595A12）。新兵种护甲扩展失效（不越界）。
- `multislot/src/mission.h`：kLoadoutRecords=0x14C78、kLoadoutRecordSize=0xD4；LoadoutSidecar 给 idx 4..kMaxPlayers-1 的旁路记录（Extend）。新兵种对大房间同样经旁路，不受影响 [推断]。
- `multislot/src/packetfit.h`：记录 ≤222 字节，同步预算 1100；兵种是 int，不改记录大小。
- `multislot/src/memberattr.h`：EDF6X_ 扩展成员属性，最多 16 键，值 ≤999 字符，总计 4096 字节，可打印 ASCII。

## 8. 插件同步方案

1. **记录里放真实 class 4/5**（不写原版兵种 + 旁路真兵种：那等于换皮，违反「新兵种不借原版兵种」；且原版 / 旧版机器会把它当成原版兵种建人，表现不一致）。
2. **版本门**：`src/mod_room.h` `kAllForcesRoomProtocol` 26→27（plugin_net.h 规则：线上改动取下一个房间协议号，不加按功能能力位）。SEARCH_TYPE 前缀 0x41460000+(protocol<<8) 让旧版插件 / 未装插件的成员搜不到房；`OtherAllForcesProtocol` 已有进房拒绝信息（mod_room.cpp），直接复用。同时把所需 EDF6Coop 版本提到改了兵种数的那一版（同 protocol 16/17 要求 2.8.0/2.9.0 的做法）。
3. **各机消费端修补**（插件在每台机器上都跑，所以都是本地补丁）：
   - 0x806FE0 名牌表扩到 6 项（或挂 0x8056E2 读取处按新兵种取插件自己的名称 / 颜色）——**必须做，否则 class 4/5 进任务即越界读**。
   - 0xE3680 / 0xE34E0 / 0xE3580 / 0x959170：要么 CONFIG.SGO 补 SoldierInit 等 4/5 项（数据路线，覆盖预载 0x59DD27、建人 0x595B1F、面板 0x8F76E6/0x8F7860、发布 0x747F70 全部调用点），要么逐点挂钩。优先数据路线。
   - 0x5A3F90 建人按 class 选玩家类：交给「建人」主题确认 class 4/5 的分支。
4. **额外状态**：新兵种的装备 / 颜色 / 护甲若存在插件自己的存档（GS 的 4 兵种数组放不下），本机打包 0x78E9A0 时由插件把新兵种数据写入记录（挂 0x78EB20..0x78EC53 读 GS 处）；记录装不下的（如载具手的载具清单、刻印）走新的 plugin_net::Channel：选 magic、RegisterPluginChannels 注册、收消息用 SenderIsPlayer 校验、`joined` 发全量；房间面板展示用 EDF6X_ 成员属性（Coop memberattr，16 键 / 4096 字节内）。
5. **EDF6Coop 改动**：kSoldierTypeCount / kWeaponClasses 4→6（或由插件经 coop 扩展 API 告知兵种数），GuardLoadout / GuardRoomWeapons 对 class 4/5 用新兵种武器表校验；armor 上限数组扩到 6。

## 9. 风险 / 阻塞

- 名牌 0x806FE0 只建 4 项：不补就堆越界（推断会崩）。
- SGO 无界查表依赖 CONFIG.SGO 能否加 SoldierInit[4]/[5]（另一主题）；做不到就要逐点挂钩约 6 处联机相关 + 约 30 处本机。
- 0x5A3F90 建人对 class 4/5 的玩家类选择未在本主题核实。
- Coop 是另一个仓库，需要先发 Coop 新版本，再把插件所需 Coop 版本提上去。
- 未实测：所有结论为静态逆向；崩溃后果需实机确认（父会话统一排批）。
