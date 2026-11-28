# 新兵种 RE：兵种数量上限写死在哪里（主题 count）

EDF.dll `0x678CCB46`，RVA。静态逆向，没有开游戏。置信度：[确认] 有指令级证据；[推断] 由多处证据推出；[猜测] 只是假设。
工具：`vanilla-code/vanilla.sqlite` 的 instruction 表（全模块按位移扫描）+ `vanilla.py show/refs`。

## 0. 结论速览

1. **引擎里没有写死的「4 个兵种」上限。** 兵种数 = 当前模式 CONFIG.SGO 里 `SoldierInit` 的元素个数：
   `0xE34D0: mov rax,[rcx+0x20]; mov eax,[rax+4]; ret`，`rcx = GS+0x130`（模式对象），`+0x20` 就是 `mission-list-re.md` 记的 `mode+0x20 SoldierInit`。[确认]
   40 个调用点（列表见 §3）全部把它当循环上界或范围检查用：UI 换兵种 `0x7DB681`（`esi < count-1` 才 `inc`）、AngelScript `set_player_soldier 0xAC674`（`class >= count` 拒绝）、默认武器 `0xDC1B6/0xDC22D`、配色 `0xDC0E9/0xDB988`、护甲 `0xAB2DD`、装备合法化 `0xDC7E5` 等。[确认]
2. **存档布局本来就按 10 个兵种预留。** 主块构造函数 `0xDEDA0`（新游戏 `0xDB920` 先用它造一份、再 `memcpy(GS+0x6E68, tmp, 0xDCE8)`）与单玩家记录重置 `0xDEF90` 的循环次数都是立即数 `0xA`：[确认]

   | 范围（GS 偏移，p = 本机玩家 0/1） | 内容 | 证据 |
   |---|---|---|
   | `0x6E68..0x6E90` | 全局护甲道具总数 `int[10]` | `0xDEDB9..0xDEDC5` 清 0x28 字节；`0xAB2FD` 按 class 写 |
   | `0x6E90 + p*0x3E60` | 玩家记录起点（长 0x3E60，正好 2 份到 `0xEB50`） | `0xDEDD2..0xDEF2A` 循环 2 次、`add r10,0x3e60` |
   | `+0x00` | 当前兵种 | `0xDEDD5 mov [r10],0` |
   | `+0x04` | 外观 | `0xDEDD8 mov [r10+4],-1` |
   | `+0x08..+0xF8`（`0x6E98..0x6F88`） | 装备 `int[10][6]` | `0xDEDE4 mov ecx,0xa`，每次写 0x18 字节 -1 |
   | `+0xF8..+0x120`（`0x6F88..0x6FB0`） | 本玩家护甲使用数 `int[10]` | `0xDEE19 lea r8,[r10+0xf8]`，`0xDEE20 r9d=0xa`，`0xDEF12 add r8,4` |
   | `+0x120..+0x3E40`（`0x6FB0..0xACD0`） | 配色 `[10 兵种][4 外观]`，每块 0x188（主 / 副各 0xC4） | `0xDEE12 lea rax,[r10+0x128]`，内层 `edx=4`、`ecx=2`、`add rax,0xc4` |
   | `+0x3E40..+0x3E60` | 0x20 字节，构造函数不写 | 全模块没有找到 `0xACD0..0xACEF` / `0xEB30..0xEB4F` 的 GS 访问 [推断：对齐填充] |
   | `0xEB50..0x14B50` | 武器记录 `0x800 × 12` | `0xDEF30..0xDEF6E` |

   合计 `0x28 + 2*0x3E60 + 0x6000 = 0xDCE8`，与 MAIN.GST 正文长度一致。[确认]
   ModeSave（`DEFP_*.MST`）的通关表也是 `clear[10][0x200]`（`0xDF790 memset(+8,0,0x1400)`，`mission-list-re.md` 已记）；写入 `0xDFAB0`：`ModeSave+8+class*0x200+mission` 置难度位。[确认]
3. **所以 class 4/5 写进 GS 不会越界、不踩任何别的字段**：class 4 的装备在 `0x6EF8..0x6F10`，class 5 在 `0x6F10..0x6F28`（p0），护甲 `0x6F98/0x6F9C`，配色块 16..23，通关表第 4、5 行——全是构造时已初始化（-1 / 0 / 默认色）的预留格，MAIN.GST 和 DEFP_*.MST 长度不变。[确认（布局）/推断（没有别的代码借用这些格）]
   扫描依据：全模块位移落在 `0x6E68..0x6FFF` 的指令共 127 条（§3），直接位移落在 `0x6EF8..0x6F87`、`0x6F98..0x6FAB` 的一条都没有；所有访问都是 `base + class*stride` 或按 `E34D0` 计数循环。
4. 真正把「兵种 = 0..3」写死的是**按兵种类型分支的代码**，不是数组长度。类型来自 `SoldierInit[class][0]`（`0xE3550`），不是 class 下标本身：
   - `0xDCDC1`（`0xDC9E0` 内）、`0x8930FD`（`0x892FF0` 内）：`switch(type){0..3 → 兵种名串}`，其它值走默认（空串 / 不拼）。[确认]
   - `0x80D344 cmp eax,3; cmove`：类型 == 3（Fencer）时传 1 给 `0x7CDE50`。[确认]
   - `0x70FC2D`（`0x70FBD0`，由 `0x71CD56` 注册的函数指针调用）：**直接按 class 下标**（不经 E3550）`switch 0..3` → `L"Ranger"/L"WingDiver"/L"AirRaider"/L"Fencer"`（`0x1768008/0x1768018/0x1768030/0x1768048`），再拼 `L"PlayCount"`（`0x17E9B08`）写进 `GS+0x14B50` 的计数表（`0xCF640`）；class ≥ 4 时只拼出 `"PlayCount"`，不崩，但计数串号。[确认]
   - 其余 `cmp …,3/4` 命中经核对都不是兵种：`0x7DA0C3` 是外观 `+1 ≤ 4` 钳制，`0x70D8CC` 是难度，`0x7DD8F6` 是 `[rdx+0x4518] < 4` 的 4 个 0xDD0 记录（会话成员）[推断]，`0x87B86C` 是 vector 长度。
5. **造人不按兵种分支 C++ 类。** `0x5A3F90`：`0x5A3FFC call 0xE34E0(mode, class, outfit)` 取 `SoldierInit[class]` 里的模型 SGO 路径 → `0x5A4040 CreateObject 0x11945E0` → `0x5A4065 __RTDynamicCast(obj, SceneObject → SoldierBase)`（类型描述符 `0x2006450`=`.?AVSceneObject@game@xgs@@`、`0x2006428`=`.?AVSoldierBase@@`），失败跳 `0x5A456E` 不出人。[确认]
   C++ 类由 SGO 里登记的工厂决定（与 `edf41-classes.md` 注册新 SceneObject 工厂同一机制）。新兵种只需一个插件工厂造出 `SoldierBase` 派生对象。[推断：工厂按 SGO 选类这一点在 edf41 已实测，用在士兵上未验证]

## 1. 在引擎里把兵种数扩到 6 要改的点

存档 / GS 层面**不用改**（已有 10 格）。要做的是：

1. **每个模式的 CONFIG.SGO `SoldierInit` 追加第 4、5 个元素**（EDF6 本篇、DLC、EDF5 战役等所有 `mode+0x20` 指向的 SoldierInit；只追加，不动 0..3）。元素内容必须齐全，因为下列访问器全部按下标直取、没有边界检查：
   `0xE3550`（[0] 类型）、`0xE34E0`/`0xE10F0`（[2] 模型与默认外观）、`0xE3420`（[5] 菜单模型）、`0xE3680`（[4] 槽定义个数，**必须 ≤ 6**，否则写进下一个兵种的装备格）、`0xE2450`（[4][i] 默认武器名）、`0xE07D0`（默认配色）、`0xE3470`/`0xDE250`（护甲 base/step）、`0xE34A0`、`0xE23F0`、`0xE15B0`、`0xE0650`（各自 20~27 处调用，本轮没逐个确认它们读 SoldierInit 的哪一格 [推断]）。
   同时要配 `SoldierWeaponCategory`、WeaponTable 新 category 的行（武器记录 0x800 上限是另一个题目，见 `loadout-re.md §8`）。
   替代方案：不改 SGO，钩这些叶函数（都是 `mov r?,[rcx+0x20]` 开头的短函数，`0xE34D0` 只有 8 字节到 int3）对 class ≥ 4 返回插件自己的数据。与 edf41 一样「新增不顶替」，但要钩十几个访问器，比追加 SGO 元素脆。
2. **类型值**：`SoldierInit[4][0]`、`[5][0]` 用新值 4、5（不借 0..3，符合「全新兵种」）。后果：`0xDCDC1`/`0x8930FD` 的兵种名为空，`0x80D344` 当非 Fencer。需要插件补名字（钩这两处或其下游 `0x3D450`/`0x957EC0` 的入参）。[确认（分支形状）/推断（只影响显示）]
3. `0x70FBD0` PlayCount：按 class 下标，class 4/5 的计数写到无前缀键 `"PlayCount"`。要么钩它给出新键名，要么接受串号。[确认]
4. **玩家 C++ 对象**：新 SGO + 插件注册的工厂，产物必须是 `SoldierBase` 派生（`0x5A4065` 的 dynamic_cast）。兵种行为（移动、装备槽语义、HUD）在 SoldierBase 派生类的虚函数里，不在这里的计数里。
5. **UI**：换兵种界面 `0x7DB520` 按 `E34D0` 计数循环，`SoldierInit` 加元素后自动能翻到 4、5 [确认]；但界面上兵种图标 / 名字文本等资源表本轮没查，可能是定长 [猜测]。
6. **联机**：MissionSync 记录 `+0 class`（`0x78EB20` 读 `GS+0x6E90`）是一个 int，可以传 4/5；但对方用自己的 CONFIG 按下标取 SoldierInit，未装插件 / 旧版的成员会越界读 SGO。按项目规则在房主侧拒绝版本不符者进房，不写兼容。[推断]

## 2. 存档：新兵种数据能不能放进 MAIN.GST

- **能放，且不破坏原版存档格式**：class 4/5 的装备、护甲、配色、通关位都落在原版已序列化的预留格（§0.2/0.3），文件长度、CRC 计算方式不变；原版游戏读这份档只按自己 SoldierInit 的 4 个元素循环，看不见 4..9 的数据，也不会清它（`0xDC550` 合法化按 `E34D0` 计数；只有新游戏 `0xDB920` 整块重置）。[确认（循环上界）/推断（没有其它代码动 4..9）]
- **唯一的硬伤是「当前兵种」字段 `+0x6E90`**：若存档时当前兵种 = 4/5，之后卸掉插件用原版读档，`0x595CB5`、`0x59DE87`、`0x7DA0A7` 等直接拿它当 SoldierInit 下标（`0xE3680`/`0xE34E0` 无边界检查），读 SGO 数组外，主菜单 / 出击即崩。[推断：访问器无检查是确认的，崩溃没实测]
  原版的 `set_player_soldier`（`0xAC674`）会做 `< count` 检查，但读档路径不做。
- 建议：新兵种的**装备 / 护甲 / 配色 / 通关**直接用 GS 预留格（零额外文件，存档、云同步、备份全部白拿）；**当前兵种为 4/5 这件事不写进 MAIN.GST**：在存档拷块处（插件已钩的 `0x7BCD40` 存档请求 / `0x7B9990` 写盘，见 `loadout-re.md §8`）把块副本里的 `+0x6E90` 写成一个原版兵种，并把「实际选的是 4/5」记进现有 sidecar（`MAIN.GST.edf6vc`）或一个同类 sidecar；读档完成函数 `0x7BE970`（插件已改尾跳 `ResultCopyHook`）之后再恢复。这样原版读到的永远是合法兵种。
  这不是兼容分支，是防止原版读档越界；如果项目决定「装过新兵种的存档不保证能被原版读」，这一步也可以省，但那样卸载插件 = 主菜单崩，需要明确接受。
- **不需要**把新兵种数据整体另存插件文件：GS 已有位置，另存反而要处理两份数据同步（参见武器记录 sidecar 的坑）。

## 3. 证据清单

### 3.1 `0xE34D0` 调用点（40 处）
`0x92BC7 0xAB2DD 0xAB33C 0xAC674 0xDB988 0xDB9BE 0xDC0E9 0xDC139 0xDC1B6 0xDC22D 0xDC7E5 0xDC8A6 0xDCD63 0xDDB25 0xDDB8F 0xDDC3D 0xDDCE7 0xDDD91 0xDDE3B 0xDDEFB 0xDF8C9 0xDF905 0xDF9D0 0xDF9FE 0x2C94CD 0x70F01C 0x70F070 0x74810C 0x74815A 0x7481AF 0x7D9EF9 0x7DA076 0x7DB681 0x8839F0 0x883B96 0x893066 0x8A2AF9 0x8A2B2F 0x8A3BBD 0x8A4806`
抽查的全部是 `for (c = 0; c < E34D0(); ++c)` 或 `c < E34D0()` 检查；没有一处拿它和常量比较。

### 3.2 GS 每兵种数组的访问方式（全部按 class 下标 × 步长，上界来自计数）
- 装备：`idx = p*0xF98 + 6*(class+2) + i`，`[GS + idx*4 + 0x6E68]`（`0x595D87..0x595DA5`、`0xDC1D1..0xDC206`、`0x883C03..0x883C2E`、`0xDC806..0xDC839`）。
- 护甲：`[GS + (p*0xF98+class)*4 + 0x6F88]`（`0x595CCF..0x595CD9`、`0x87A79C..0x87A7A6`）；全局 `[GS + class*4 + 0x6E68]`（`0x87A795`、`0x2C77EB`、`0xAB2FD`；`0xAB2C0` 循环里三处同写 `0x6E68/0x6F88/0xADE8`）。
- 配色：`GS + 0x6FB0 + p*0x3E60 + (class*4+outfit)*0x188`（`0x78EC53 imul rcx,r9,0x620`，`0x93B189 imul r8,r9,0xc4`）。初始化 `0xDB920`/`0xDC0C0`：`for p<2, for c<E34D0(), for outfit<4: 0xDF1B0`。
- 通关：`0xDFAB0`：`ModeSave + 8 + (class<<9) + mission`，`bts`；`0xDF990` 按 `E34D0` 累加。

### 3.3 按兵种类型 / 下标分支
`0xDCDC1`、`0x8930FD`（E3550 类型 switch，有默认）、`0x80D344`（type==3）、`0x70FC2D`（class 下标 switch，默认只拼 "PlayCount"）。
`0xE3550`：`SoldierInit[class]` 的第 0 个子元素的 int 值；调用点 `0xDCDC1 0xDCE2E 0x80D337 0x8930FD 0x8AA8AA 0x8ACC5E 0x8ADCAC 0x8AE018 0x93B166 0x93B45B`。

## 4. 没查的 / 风险
- 兵种图标、名字、HUD、结算画面里是否有按 class 下标的定长资源表（只扫了读 `GS+0x6E90` 的 33 处与 `E3550` 的 10 处）。[未查]
- COMMON.CFG（`mgr+0x318..0x57A4`）里有没有每兵种数组。[未查]
- `0xE0650`/`0xE23F0`/`0xE34A0`/`0xE15B0` 具体读 SoldierInit 哪一格。[未查]
- SoldierBase 派生类需要实现的虚函数集合（另一个主题）。
