# 新兵种（SoldierInit 元素 4/5）注入点逆向

目标 `EDF.dll` TimeDateStamp `0x678CCB46`，下文地址都是 RVA。静态逆向：vanilla-code 汇编、vanilla.sqlite、CONFIG.SGO 实物 dump。没有开游戏，没有改 src。
前置文档：`tmp/newclass-re-architecture.md`、`count.md`、`spawn.md`、`equip.md`。

置信度：
- [确认]：已沿汇编逐条核对。
- [推断]：结构上成立，但没有实机或全路径证据。
- [猜测]：依据不足。

插件原语见 `common/edf/memory.h`：`RedirectCall(site, expectedTarget, repl, changed, Claim, owner)`、`AllocateNearCode`、`AllocateNearThunk`。

---

## 1. SoldierInit / SoldierWeaponCategory 注入

### 1.1 读取链 [确认]

**谁读 SoldierInit**
- `0xE2AD0` 是 ModeData 加载函数，`rsi` = mode = `GS+0x130`。
  - `0xE2C0D..0xE2C1C` 调 `0x528A0(doc, L"SoldierInit")`，结果存进 `mode+0x20`。
  - `0xE2C20..0xE2C2D` 调 `0x528A0(doc, L"SoldierWeaponCategory")`，结果只放在 `rdi` 里，不落盘。
- `0xE2AD0` 只有两个调用点：`0xD9324`，和 SetMode `0xDC460` 内的 `0xDC4B3`。
  - SetMode 的调用者：`0xAC21A`、`0xD6722`、`0xDB6BF`、`0x70FAC9`。
  - 只有一份 `DefaultPackage/CONFIG.SGO`。所有模式（原版、DLC、EDF5 / 4.1 战役、测试场）都走这条路。

**WeaponCategory 表**
- `0xE3EC0` 先清空 `mode+0x28` 的 map。
- 然后 `0xE2C50` 循环用 `0xE0320` 逐条插入。
- map 节点 0x28 字节：

  | 偏移 | 内容 |
  |---|---|
  | `+0x10` | key（int 类别号） |
  | `+0x18` | wchar* 名字，直接指进 SGO |
  | `+0x20` | 兵种 type，初值 -1 |

**按兵种、槽位回填类别**
- `0xE2CB5..0xE2D84` 循环兵种 × 槽位，取出 `E2450` 的 cats 后，对每个 cat 调 `operator[] 0xE00C0`。key 不存在时会插入一个名字为 null 的节点。
- `0xE2D51 mov [rcx+0x20],ebx` 写入兵种 type。
- 结论：新兵种的类别号必须同时写进 SoldierWeaponCategory，否则 map 里会出现名字为 null 的节点。

**CustomColor**
- `0xE314E..0xE33D4` 再读一次 `mode+0x20`。
- 把 `mode+0x198` 处的 vector 扩到兵种数。
- 每个兵种的 `[5][0]` CustomColor SGO 用 `0x84E80` 载入 `[mode+0x1A0]+class*0x10`。
- 新兵种的 `[5][0]` 必须指向真实存在的 CustomColor SGO。

### 1.2 访问器（node E = SoldierInit[class]）[确认]

| 函数 | 读取内容 |
|---|---|
| `E34D0` | 兵种数（数组 a 字段） |
| `E3550` | type，`[E][0]` |
| `E3580` | 名字 key，`[E][1]` |
| `E34E0` | `[E][2][outfit]`，战斗 SGO |
| `E3470` / `E34A0` | `[E][3][0]` / `[3][1]`：HP = base + per_armor × armor |
| `E3680` | 槽位数（`[E][4]` 的 count，`[E+0x34]`）。原版最多 6 槽（Fencer） |
| `E2450(out, class, slot)` | 见下 |
| `E07D0` | `[5][0]` CustomColor |
| `E3420` | `[5][1][outfit]` 菜单 SGO |
| `E1B60` | `[5][2]` rab |
| `E1B10` / `E3520` | `[5][3]` 头像 dds 列表 |

`E2450` 的输出结构：

| 偏移 | 内容 |
|---|---|
| `+0` | 槽名 |
| `+8` | 默认武器（wstring） |
| `+0x18` / `+0x20` / `+0x28` | cats vector |
| `+0x30` | flag `[3]` |
| `+0x31` | flag `[4]` |

原版 4 个元素的完整结构（用 `pylib/sgo.py` 从 Root.cpk 的 `DEFAULTPACKAGE/CONFIG.SGO` 读出；游戏文件内容不入库，要看就本地 dump）。每个元素是 6 项数组：

```
[0] int type
[1] str SoldierType_X
[2] [4]战斗SGO
[3] [2]float{HP基数, 每装甲}
[4] [N]{[0]槽名 [1]默认武器 [2][k]int cats [3]int [4]int}
[5] [4]{[0]CustomColor.sgo [1][4]菜单SGO [2]rab [3][k]dds}
```

### 1.3 注入方案 [确认 指令与字节；推断 时序]

改两个 `call 0x528A0` 调用点：
- `0xE2C17`：`E8 84 FC F6 FF`，后面接 `48 89 46 20`。
- `0xE2C2A`：`E8 71 FC F6 FF`，后面接 `48 8B F8`。

`0xE2C0D` 起的整段指纹：

```
48 8d 15 8c 56 68 01 49 8b cc e8 84 fc f6 ff 48 89 46 20
48 8d 15 91 56 68 01 49 8b cc e8 71 fc f6 ff 48 8b f8
```

替换函数的做法：
1. 先调原 `0x528A0` 拿到原节点。
2. 返回插件自建的连续节点块（深拷贝）：
   - SoldierInit 版本：原 0..3 再追加 4/5。
   - SoldierWeaponCategory 版本：原表再追加新类别。
3. 节点格式为 12 字节 `{type,a,rel}`，rel 是有符号相对偏移（movsxd），字符串是 UTF-16，与 `pylib/sgo.py` 相同。
4. 节点块和字符串必须同处一块常驻内存。不释放，进程生命期内有效，因为 map 的 `+0x18` 直接指进节点。

时序：插件必须在第一次 SetMode 之前装好 [推断，需要核对插件加载时机早于 `0xD9324`]。
另一种做法是直接改写 Root.cpk 里的 CONFIG.SGO。但这和 Mods 安装链冲突，不推荐。

---

## 2. 槽位 83 GetSoldierType（vtable `+0x298`）全部调用点

槽位 83 的实现都返回常数：
- AssultSoldier `0x550A20` 返回 0。
- PaleWing `0x580510` 返回 1。
- Engineer `0x563D90` 返回 2。
- HeavyArmor `0x567240` 返回 3。

所以新兵种返回什么，由插件复制出的 vtable 决定。

全模块共有 18 处 `call [reg+0x298]`，其中 14 处是 SoldierBase 的槽位 83。

| 调用点 | 所在函数 | 判定 | 结论 |
|---|---|---|---|
| `0x467ADD` | `0x467200` | HumanoidBase（Martian 等）槽位 0，不是士兵 | 无关 [确认] |
| `0x47E0D0` | `0x47E0B0` | 同上 | 无关 [确认] |
| `0x65A514` / `0x65A5D3` | `0x65A390` | Vehicle_Car / Truck / RoboTruck 槽位 55，返回值被丢弃 | 无关 [确认] |
| `0x1B5095` | `0x1B5030` | 脚本 GetPlayerSoldierType，原样返回 type。注册在 `0x1E9117`（rip lea） | 见第 6 节 |
| `0x222813` | `0x222740` | 旧 BVM native `0x3ED`(1005)，由 `0x214480` 分派，原样返回 | 见第 6 节 |
| `0x59945C` | `0x598FD0` | SoldierBase/AS/PW/EN 的 vtable 槽 9，调 `0x772950(out,type,text)` | `0x772950` 只在 `0x772A6F` 做一次 `cmp r14d,1`，4/5 按「非 1」处理，安全 [确认] |
| `0x95F5EE` | `0x95F4B0` | UiChat_WatchDog 槽 3，同样调 `0x772950` | 安全 [确认] |
| `0x5D9876` / `890` / `8AB` / `8C6` | `0x5D9720` | 见下方说明 | 4/5 永远不会被排除，安全 [确认] |
| `0x5DF7BC` / `7D1` / `7E7` / `7FD` | `0x5DF770` | 见下方说明 | 安全，但新兵种能上所有座位 [确认] |
| `0x81F83E` | `0x81F6F0` | 伤害数字颜色 | **兵种 5 读越界**，见第 3.2 节 |
| `0x83107E` | `0x830E40`（来自 `0x82E127`） | `cmp eax,3` 选 Fencer 装填图标，其余走默认 | 安全 [确认] |

两个掩码函数的细节：
- `0x5D9720`：
  - 先 `__RTDynamicCast` GameObjectBase→SoldierBase（`0x2006400` / `0x2006428`）。
  - 掩码字节 `[r8]` 的 bit k 为 0 时排除兵种 k（k = 0..3）。
  - 调用者 `0x1BAD56`（在 `0x1BABA0` 内）和 `0x2291FB`（在 `0x229040` 内）都传 0x1F。
- `0x5DF770`：
  - 座位兵种掩码，结构与 `0x5D9720` 相同。
  - bit 0x10 控制 `[obj+0x460]==1` 检查。
  - type 4/5 永远返回 1，所以新兵种能上所有座位。
  - 要限制的话，在插件里 hook 这个函数，或者让槽位 83 返回基底兵种。

---

## 3. 表扩容

### 3.1 `0x806FE0` 名牌表（HUD+0xB0）[确认]

调用链：
- 唯一调用点是 `0x804875`（在 `0x8047C0` 内）。
- `0x8047C0` 由 HUD 构造 `0x7FD5B9` 调用，每个 HUD 只跑一次。

函数内的结构：
- 拥有者 T 为 `rdi` = HUD+0xB0。
- 数据指针 `[T+8]`，容量 `[T+0x10]`，大小 `[T+0x18]`。
- 元素 0x30 字节：`+0..0x1F` 是 wstring（MSVC，`+0x10` size，`+0x18` cap，cap<8 时为 SSO），`+0x20/24/28/2C` 是 float r, g, b, a。

按 `esi` 分支：

| esi | 字符串 | 地址 | 颜色 |
|---|---|---|---|
| 0 | L"R" | `0x17F6CE8` | (1,0,0) |
| 1 | L"W" | `0x17F6CEC` | (0,0,1) |
| 2 | L"A" | `0x175F450` | (0,1,0) |
| 3 | L"F" | `0x1767F80` | (1,1,0) |
| default | L"E" | `0x1767F7C` | (1,1,0) |

所有分支的 a 都是 0.7（`0x3F333333`）。

**扩到 6 的补丁**（地址都是立即数字节所在的位置）：

| 地址 | 原字节 | 改为 | 作用 |
|---|---|---|---|
| `0x8071F0` | `48 83 7F 10 04` | 立即数 04→06 | 容量比较 |
| `0x8071F8` | `B9 C0 00 00 00` | `C0 00`→`20 01` | 分配 0x120 字节 |
| `0x807214` | `48 83 FB 04` | 04→06 | |
| `0x807223` | `BB 04 00 00 00` | 04→06 | |
| `0x8072B0` | `48 C7 47 10 04 00 00 00` | 04→06 | 写入容量 |
| `0x8072B9` | `BA 04 00 00 00` | 04→06 | `0x8082B0(T,n,&default)` resize |
| `0x8073AC` | `83 FE 04` | 04→06 | 循环上限 |

- 只打这组补丁时，第 4/5 项走 default 分支，显示黄色 "E"。
- 想要专属字母和颜色：
  1. RedirectCall `0x804875`（`E8` → `0x806FE0`）。
  2. 在替换函数里先调原函数。
  3. 再对 `[T+8]+4*0x30` / `+5*0x30` 用 `0x3D450(wstring*, wchar*, len)` 赋值，并写入颜色。

**为什么必须扩**：
- 读取点在 `0x8056E2..0x8056EF`，索引 `[rsp+0x60]` = `[GS+0x14C78+idx*0xD4]`。这是成员记录的 `+0` class，`+4` 是 outfit。
- idx 是 `[player+0x48]`，在 `0x7FFBD0` 的 `0x7FFD95..0x7FFDB1` 设置。
- 成员记录的写入路径：网络消息 `0x790873..0x7908BD`，以及 `0x59DD01` → `0xDE470`。
- 只有 `0x804ECF` 做了下界检查。
- 不扩容时 class 4/5 会读出 0xC0 堆块之外，wstring 指针变成垃圾，可能崩溃 [推断]。

第二张表在 T+0x28/0x30/0x38（= HUD+0xD8），由 `0x8073F5..0x807540` 填写，按玩家下标 0..3 索引，不需要动 [确认]。

### 3.2 `0x81F6F0` 伤害数字颜色表（this+0x920）[确认]

调用者：
- `0x81F2E3`（在 `0x81F290` 内）。
- `0x81FD0B`（在 `0x81FBB0` 内，即 ui::DamageNumberDrawTexture 槽 1）。
- `0x8217EF` 和 `0x821E9E`（在 `0x821100` 内）。

取色规则：
- 地址 = `r15 + 0x920 + type*0x30 + rbx*0x10`。
- rbx ∈ 0..2，由 `[GS+0x14FF4]==2` 和 `[r15+0x798]` 决定。
- 另一条分支：`[r12+0x1550]->+8 != 0` 时读 `0x9E0+rbx*0x10`。

表的范围与来源：
- 0x920..0xA10 共 15 个 16 字节颜色，即 5 行 × 3。
- 构造 `0x81DCE0`（来自 `0x816D23`）在 `0x81DE12` 的循环里初始化。
- 加载 `0x8224A0`（来自 `0x81DF8B` 和 `0x82053E`）在 `0x822EEE..0x822FB3` 从 SGO 读 5 行 × 3。

后果：
- type 4 读到第 4 行，也就是另一条分支用的那一行。颜色不对，但内存合法。
- type 5 读到 0xA10 起的下一个成员（list/string 对象），颜色是垃圾，只读不写，不会崩 [确认 算式]。

修复方案：
1. 把 `0x81F837..0x81F85A`（0x23 字节）改成 `E9` 跳到近程 stub，剩余字节填 NOP。
2. stub 原样执行 `call [rax+0x298]`。
3. `eax>=4` 时 `lea` 插件颜色表 `[type-4][rbx]`；否则按原式计算。
4. 最后 `jmp 0x81F85A`。
5. 写代码前先 dump `0x81F837..0x81F85A` 的原字节，确认寄存器约定（r15 / rbx / 结果寄存器）[确认 区间；落地前复核寄存器]。

更简单的替代方案：让新兵种的槽位 83 返回基底 type。但这会影响第 2 节所有分支，见第 6 节的权衡。

---

## 4. 存档替身（GS+0x6EF8..0x6F87）与 `0xDC550` 读档后检查

### 4.1 布局 [确认]

装备槽地址公式：

```
GS + 0x6E68 + 4*(p*0xF98 + 6*(class+2) + slot)
```

- p ∈ {0,1} 是存档档位，每档相差 0x3E60 字节。
- class 的槽位有 6 个。
- 构造 `0xDEDA0`（rcx = GS+0x6E68 块，r10 = +0x28）把两档各 10 兵种 × 6 槽全部置 -1。
- 同一区域的其他字段：
  - `[GS+0x6E68+class*4]` 是装甲计数（第 5 节用到）。
  - `[GS+0x6E90]` = 0 是当前兵种，`[+0x6E94]` = -1。
  - GS+0x6F88 起是另一组每兵种 10 dword 的数组。

p=0 时各兵种的装备区：

| 兵种 | 范围 |
|---|---|
| 4 | 0x6EF8..0x6F0F |
| 5 | 0x6F10..0x6F27 |
| 6..9 | 0x6F28..0x6F87 |

p=1 时各地址再加 0x3E60。

### 4.2 `0xDC550` 读档后检查 [确认]

- 外层：r13 每次 +0xF98，到 0x1F30 停止（`0xDC8B3..0xDC8C5`），即 p=0,1。
- 中层：class = 0..`E34D0`。
- 内层：slot = 0..`E3680(class)`（`0xDC7DC..0xDC8AD`）。
- 槽值为 -1，或物品未拥有（`[GS+(id+0x139C)*12]&1==0`，`0xDC849..0xDC858`）时：
  - 用 `E2450` 取默认武器名。
  - 再用 `E1BA0` 换成物品 id，写回槽位（`0xDC87D`）。
- 注入 6 个兵种后，class 4/5 的装备区成为正常的活跃存储，由 `0xDC550` 校验并补默认值。这正是想要的行为。

### 4.3 替身选址

- 兵种 6..9 的区（`GS+0x6F28..0x6F87`，以及 p=1 的 `+0x3E60`）只通过以 `E34D0` 为上限的计算下标访问。
- 全模块没有落进这个范围的直接位移 [确认 已扫描范围内]。
- 所以兵种 6..9 区可以放插件替身数据。
- 不能放进兵种 4/5 区：注入后那里会被 `0xDC550` 改写。
- 这块是否随存档整块序列化 [推断]：构造时整块初始化，读写看起来是整块拷贝，未逐字段核实。

---

## 5. 三处 hook 设计

### 5.1 `0xDCDC1`（在 `0xDC9E0` 内，由 `0x70FF97` 尾跳进入）

循环与字符串：
- 以 `E34D0` 为上限循环兵种。
  - `0xDCD63` 是 `E8 68 67 00 00`。
  - 循环尾 `0xDDB25` 是 `E8 A6 59 00 00`，`0xDDB2A` 是 `cmp r13d`。
- 装甲计数取 `[GS+0x6E68+class*4]`。
- 拼出 "<Class>Armor"（L"Armor" 在 `0x1768058`）等统计名，再调 `0xCFA60` / `0xD02C0`。
- 兵种分支在 `0xDD398` / `0xDD558` / `0xDD713` / `0xDD8E5`（`cmp r13d,1/2/3/4`）。
- default 分支前缀为空，新兵种会写成无前缀的 "Armor" 统计。

方案：
- RedirectCall 两处 `E34D0` 调用，返回 `min(count,4)` [确认 调用点]。
- 统计名是内部统计 / Steam 统计 [推断]。新兵种的装甲统计另行设计。

### 5.2 `0x8930FD`（在 `0x892FF0` 内，来自 `0x88BEBF`，按键设置菜单）

- 循环上限 `r12d` = `E34D0`（`0x893066`：`E8 65 04 85 FF`）。r13b=0 时上限为 6。
- type → 文本 key，经 `0x957EC0` 查表：

  | type | key | 地址 |
  |---|---|---|
  | 0 | OptionPlayer_KeyConfig_Ranger | `0x1802C48` |
  | 1 | OptionPlayer_KeyConfig_WingDiver | `0x1802C90` |
  | 2 | OptionPlayer_KeyConfig_AirRaider | `0x1802CE0` |
  | 3 | OptionPlayer_KeyConfig_Fencer | `0x1802D28` |

- default 分支 `0x893119` 执行 `mov rax,rdi`，rdi=0，随后做 strlen。**空指针，必崩** [确认]。

两种修法：
- 方案 A：RedirectCall `0x893066`，返回 `min(count,4)`。菜单不显示新兵种。
- 方案 B：把 `0x893119`（`48 8B C7 EB 27`，5 字节）改成 `E9` 跳到 stub。stub 按 type 返回插件提供的 key 字符串，然后 `jmp 0x893145`。

战斗中新兵种的按键配置怎么取（是否按 type 查表）[待查]。

### 5.3 `0x70FC2D`（在 `0x70FBD0` 内，脚本函数，rip 引用在 `0x71CD56`）

- 对 p ∈ [0, `[GS+0x14FF4]`) 读 `[GS+0x6E90+p*0x3E60]`（当前兵种），按值分支取前缀：

  | 兵种 | 前缀地址 |
  |---|---|
  | Ranger | `0x1768008` |
  | WingDiver | `0x1768018` |
  | AirRaider | `0x1768030` |
  | Fencer | `0x1768048` |

- default 分支在 `0x70FC45`（`75 3E`）跳到 `0x70FC85`，前缀为空。
- 接着拼 L"PlayCount"（`0x17E9B08`），调 `0xCF640(GS+0x14B50, 0, name, 1, true)` 自增。新兵种会把无前缀的 "PlayCount" 加 1 [确认]。

修复方案：
1. 把 `0x70FC2D..0x70FC35`（`8B 94 11 90 6E 00 00 85 D2`，9 字节）改成 `E9 rel32` 加 4 个 NOP。
2. stub 内容：

   ```
   mov edx,[rcx+rdx+0x6E90]
   cmp edx,4
   jae 0x70FD82        ; 即 inc ebx 处，跳过本玩家
   test edx,edx
   jmp 0x70FC36
   ```

3. `[rsp+0x30]` 的串是 SSO，跳过时不需要释放。
4. 落地前复核 `0x70FD82` 处确实是 `inc ebx` 和循环尾，以及 flags 与原 `test` 一致。
5. 如果想给新兵种单独计数，在 stub 里自己拼名字再调 `0xCF640` [推断]。

---

## 6. 脚本拿到 4/5 时的行为

### 6.1 新脚本（GetPlayerSoldierType，`0x1B5030`）[确认 文本；推断 后果]

EDF6 教程在这些位置用 `if (GetPlayerSoldierType(0) != PLAYER_TYPE_X) return;` 分出兵种专属段：
- `EDF6/M000B`：915 / 4148 / 4149 / 5063 / 6348 行。
- `EDF6/RM000`：638 / 5861 / 5980 / 6106 行。
- 另有 `EDF5_NEW_SCRIPT/M000`、`GOTOTEST/EDF6_TEST/M000`、`TEST/T_FADE`。

type 4/5 会跳过所有兵种段。教程可能缺少推进条件而卡住 [推断]。

其他行为：
- 目标等级：非 ENGINEER 一律取 0.3。
- `EDF5_NEW_SCRIPT/M034_5:545` 写的是 `== PALEWING return`，所以 4/5 会播语音。

### 6.2 旧 BVM（native 1005，`0x222740`）[确认 文本]

这些脚本同样按 type 分支：
- `EDF5_OLD_SCRIPT/M000`、`M034_5`、`M046_2`。
- `DLC/DM023`。
- `TEST/OLD/T_ALIENTALK`。

### 6.3 建议 [推断]

把脚本看到的 type 映射成基底兵种 type（新兵种的「教程身份」），引擎内部仍保留真实的 4/5：
- 新脚本：把 `0x1E9117` 的 `lea` 注册指针换成近程 thunk。thunk 调原 `0x1B5030`，返回值 ≥4 时映射成基底兵种。
- 旧 BVM：在 `0x214480` 的分派表里把 `0x3ED` 的入口换掉，或对 `0x222813` 之后的写回做 RedirectCall 包装。
- 时序：脚本函数注册发生在插件装好之后还是之前要核对。如果注册更早，就改注册表本身的那一项。
- 不建议让槽位 83 整体返回基底 type：会让第 3 / 5 节的 HUD 名牌、伤害颜色、按键和统计都看不到新兵种。只在脚本边界映射。

---

## 待办 / 未核实

1. 插件加载时机与第一次 SetMode（`0xD9324`）的先后。
2. `0x81F837..0x81F85A` 原字节和寄存器约定的最终 dump。
3. 战斗中新兵种的按键配置来源。
4. GS+0x6E68 块是否整块存档，以及替身区跨版本是否稳定。
5. 脚本注册表和 native 1005 分派表的具体替换位置。
6. `0x5DF770` 座位限制的产品决策：新兵种要不要能上所有座位。
