# 诱饵（hololive 招募员）与「任意点爆炸」逆向笔记

EDF.dll TimeDateStamp 0x678CCB46，ImageBase 0x180000000，下文地址全部是 RVA。纯静态分析（tools/edfre.py + capstone），
没有起游戏。每条结论标 **确认**（指令/数据直接可见）或 **推断**（证据一致但没有直接看到或没实测）。

相关已知（见 re-notes.md / airstrike-re.md / mission-airstrike-re.md）：
CreateObject `0x11945E0(*(img+0x20B2958), const Matrix*, const wchar_t* sgo, InitParam*)`；
预载 `0x7A3780(*(img+0x20B29A8), path, 2, -1)`（只在任务加载期经 0x59DE50 包装调用才安全）；
SetTeam `0x54EE70(obj, team, bool reg)`；Delete `0x118A1B0(obj)`；父子链 `0x118AF20(parent, child)`。

---

## 1. 在世界任意点造一次爆炸

### 1.1 引擎里的「爆炸」是什么

**确认**：爆炸伤害 = `GameDamageInfo::ApplyAreaDamage`。RTTI 里的 lambda 名完整修饰为

```
Flag<u8,GameDamageInfo::ResultFlagBits,57,0>
GameDamageInfo::ApplyAreaDamage(std::unordered_set<SceneObject*>* hitSet, float radius,
                                unsigned int frame, SceneObject* ignore) const
```

| RVA | 作用 | 调用约定（x64，返回 Flag 走隐藏指针） |
|---|---|---|
| `0x542490` | 上面这个带半径的重载 | rcx=GDI*，rdx=Flag* 返回，r8=hitSet*，xmm3=radius，[rsp+0x20]=uint frame（来自 `0x106070(*(img+0x20B2970))`），[rsp+0x28]=SceneObject* ignore |
| `0x542670` | 半径取 GDI+0x58 的重载 | rcx=GDI*，rdx=Flag*，r8=hitSet*，r9=SceneObject* ignore；frame 自己取 |
| `0x542860` | 两者共用的实际施加（球查询 `NpTargetCollector` 0x1768F40 + 逐目标扣血） | — |

两个重载都先做一次球形命中查询（中心 = GDI+0x30，半径 = xmm3 或 GDI+0x58），GDI+0x24 != -1 且 GDI+0x60 没有 0x20 位时，
挂一个过滤 lambda（vtable 0x17CD288，`_Do_call` = 0x543DD0）：目标是 GameObjectBase 时调
TeamManager `0x5E1540(*(img+0x20B2978), target+0x318 队伍, GDI+0x24)` 判敌友（**确认**）。所以 **GDI+0x24 就是攻击方队伍**，
GDI+0x60 的 0x20 位 = 不做敌我过滤（**推断**：友伤开关）。

### 1.2 GameDamageInfo 布局（子弹里内嵌在 core+0x730）

子弹 = bullet，core = bullet+0x140（BulletBase 的弹道核心）。初始化在 `0x231CC0`（core init，由 BulletBase ctor 0x22E9C0 在 0x22EA45 调），
GDI 字段在 0x231F41..0x232117 填写（**确认**为写入点，含义标注见右列）：

| GDI 偏移 | core 偏移 | 来源 | 含义 |
|---|---|---|---|
| +0x00 | 0x730 | 0 | 类型/状态（推断） |
| +0x10/+0x18 | 0x740/0x748 | core+0x9A8/0x9B0（射手的 weak_ptr，带引用计数） | 攻击者对象（确认是 shared/weak 指针对；含义推断） |
| +0x20 | 0x750 | 0 | |
| +0x24 | 0x754 | core+0xA00（= 子弹 initparam+0x90） | 攻击方队伍（确认被当队伍用，来源推断） |
| +0x30 | 0x760 | 命中点 core+0xB80，w=1 | 爆心（确认） |
| +0x40 | 0x770 | 速度向量 × AmmoHitImpulseAdjust(core+0xA38) | 冲击方向（推断） |
| +0x50 | 0x780 | AmmoDamage(core+0xA0C) × core+0x9A0 | 伤害（确认） |
| +0x54 | 0x784 | core+0xBE0（Size×HitSizeAdjust） | 命中尺寸（推断） |
| +0x58 | 0x788 | **AmmoExplosion**(core+0xA20) | 爆炸半径（确认） |
| +0x5C | 0x78C | core+0xA1C | （未定） |
| +0x60 u16 | 0x790 | 0x20=core+0xA26；0x02=爆炸弹；0x01=半径≥阈值 | 标志（确认写入，含义推断） |
| +0x64 | 0x794 | core+0xA3C，默认 0.5 | （未定） |
| +0x68 | 0x798 | core+0xA14 | （推断：对护盾伤害） |
| +0x90 | 0x7C0 | unordered_set<SceneObject*> | 本发子弹已命中集合，作为 hitSet 传入（确认） |

core 的标志字 core+0xAF4（= bullet+0xC34）关键位（**确认**于 0x231D08..0x2320ED、0x23635F、0x236853）：

- 0x10：AmmoExplosion > 0 时置位，命中时走范围伤害（0x232616 处分支：有 0x10 → ApplyAreaDamage，否则 0x543920 单体伤害）。
- 0x20：**寿命到期也爆**。到期判定：core+0xAF8 每帧 +1，≥ AmmoAlive(core+0xA08) 且 0x200 位在 → 到期；到期时如果有 0x20 → 以当前位置调 ApplyAreaDamage，
  然后置死亡位 3 并调 core+0xCD8 的回调（vfunc+0x10）。没有 0x20 的子弹到期只消失。
- 0x200：允许寿命到期（init 时总是置位；SolidExpBullet 黏住后清掉它，改走自己的引信）。

### 1.3 (a) 插件直接调用爆炸函数：能调，但不推荐

**结论**：`0x542490` 可以从插件调用（普通 __fastcall 成员函数，**确认**），但它只做「伤害」：

- 视觉特效、音效都不在这里。子弹命中/到期的特效是子弹类自己生成的，例如 SolidExpBullet 爆炸 `0x28E950` 里用 CreateObject 路径
  `0x274A30(*(img+0x20B2958), out, &Matrix, &InitParam@BlowHit01)`（InitParam@BlowHit01 vtable 0x17A2A70）生成冲击波对象，
  再在 bullet+0xBB0 上调 `0x7B4510` 发声（**确认**调用存在，特效含义推断）。直接调 ApplyAreaDamage 就只有看不见的扣血。
- GDI 没有独立构造函数可用（在 core init 里内联填写，**确认**），+0x10 是带引用计数的 weak_ptr，插件得自己伪造一份正确布局，+0x7C0 的 unordered_set 也要是真的 MSVC 容器。
  布局错一个字节就是崩溃，而且伤害归属（击杀记分）取自 +0x10 的攻击者。
- 不走网络同步。

拿活着的子弹的 core+0x730 做模板也不行：整块拷贝会复制 +0x10 的智能指针而不加引用计数，+0x90 的 unordered_set 也不能按字节复制。
所以 (a) 在工程上不划算。**推荐 (b)。**

### 1.4 (b) 给直升机挂一把「近距自爆」派生武器：推荐

现成底子：**`V_409HELI_BOMB01.SGO`**（409 直升机的炸弹）：AmmoClass `GrenadeBullet01`，AmmoSpeed 0.25，AmmoAlive 2400，
AmmoDamage 500，AmmoExplosion 20，模型 bullet_missile.rab，CP `[0, -0.004, 1, 0, 0, 0]`。

GrenadeBullet01（Factory vtable 0x17A1688，create 0x264890，ctor **0x264E90**）CP 解读（**确认**读取与写入位置，含义部分推断）：

| CP | 写到 | 含义 |
|---|---|---|
| #0 int | bullet+0xF00 | 模式。**只有 ==1 时**在 0x26543E 给 core 置 0x20（到期爆炸，确认），并按 #3 选碰撞响应：#3>阈值 → `0x235990(core,2)` 反弹 + 反弹回调；否则 `0x235990(core,3)` |
| #1 float | （-0.004，推断：重力/阻尼相关） | |
| #2 float | | |
| #3 float | bullet+0x1150 | 反弹系数（推断） |
| #4 float | | |
| #5 int | 随机附加寿命：core+0xA08 = AmmoAlive + rand×#5（0x265332..0x265397，确认） | |

stock 里 #0 的分布：0（炸弹/榴弹，碰撞即爆，**到期不爆**）、1（手雷类，到期爆）、2/3/4（其它）。

**派生武器 EDF6VC_xxx_BLAST.SGO**（用现有 gen.py / dsgo 那套改数值）：

```
AmmoClass          GrenadeBullet01      （不改）
Ammo_CustomParameter #0 = 1              （到期必爆）
                   #3 = 0                （不反弹）
                   #5 = 0                （去掉随机寿命）
AmmoSpeed          0.0 ~ 0.05            （基本原地）
AmmoAlive          1 ~ 3                 （几帧后爆）
AmmoExplosion      想要的半径（例：15~30）
AmmoDamage         想要的伤害
```

- **到期爆炸**：确认（0x23635F/0x236853 → 0x542490/0x542670），且碰到东西时因为 AmmoExplosion>0 同样走范围伤害（0x10 位，确认）。
- **归属**：子弹由直升机发射，攻击者 = 发射者（core+0x9A8），队伍 = 发射者队伍 → 友军/玩家方，敌我过滤生效（推断，与 1.1 的过滤器一致）。
- **特效**：沿用 GrenadeBullet01 的爆炸表现（推断，stock 手雷到期会爆出完整特效）。
- 也可选 PlasmaBullet01（CP 开关 bullet+0xF04 → 0x25C010 置 0x20），但手雷/炸弹更接近要的效果。
- **不要**用 RocketBullet01 / MissileBullet01 / SolidExpBullet01：静态没找到它们置 0x20 位（推断：到期只消失；SolidExpBullet 是「黏住后引信 +0xF00 到 0 才爆」，0x28F39D）。

开火：现在 jet/heli 的 0x2020 打 0/1 号武器，0x2021 打 2 号。把 2 号（导弹位）换成这把派生武器，插件在「离最近敌人 < R」时置一帧 veh+0x2021 即可。

### 1.5 插件侧最少工作

1. gen.py 生成派生武器 SGO（同 jet_guns 的路子），在 mission_setup 里放到直升机 2 号武器位。
2. 插件每帧判断距离（已有敌人枚举的话复用；没有的话 TeamManager `EnumConflictObject 0x5E0F20(mgr, team, cb)` 能列出所有与 team 敌对的对象，确认）。
3. 距离够近就写 veh+0x2021=1 一帧；需要「自爆」的话下一帧 Delete 直升机（以及挂着的诱饵）。

---

## 2. hololive 招募员诱饵（DLC_HOLO_DECOY_*）

### 2.1 武器 SGO（**确认**，sgo.py 读 cpk）

六把（Ayame/Mio/Fubuki = `DLC_HOLO_DECOY_*.SGO`；Gura/IRyS/Kiara = `INDLC_HOLO_DECOY_*.SGO`）除了 decoy_sgo 路径和名字以外完全相同：

- `xgs_scene_object_class` Weapon_Sub，AmmoClass **DecoyBullet01**，子弹模型 e_throw_decoycore01.rab
- AmmoAlive 36000，AmmoDamage 360，AmmoSpeed 0.5，AmmoExplosion 0，ReloadTime 2400
- CP `{#0:1, #1:60, #2:32, #3:0, decoy_sgo:'app:/object/e_throw_decoyscreen_X.sgo'}`
- `resource: ['app:/object/e_throw_decoyscreen_X.sgo']` → 武器装备时预载诱饵对象（这是它自己保证对象已加载的方式）

### 2.2 DecoyBullet01（投出去的「核」）

| 项 | 值 |
|---|---|
| Factory vtable | 0x17A4EA0，create(slot2) **0x29D9F0**：new 0x14F0 → ctor **0x29DDC0**(obj, initparam)；名字 getter 0x29D9E0 |
| vtable | **0x17A4F30**；基类 BulletBase（ctor 0x22E9C0），NetworkBullet 子对象在 +0x120 |
| update(slot5) | **0x29F800** |
| 状态机 | Agent 在 +0x1490；初始状态 0x29FC80 |

ctor 读 CP（initparam+0xE8，**确认**）：`+0xE90 = #0`、`+0xE94 = #1`、`+0xE98 = #2`；#3 类型标签==3 时替换子弹模型（+0xEB0 经 0x6BB890），否则用 initparam+0x1B0 的默认模型。

流程（**确认**指令，CP 语义**推断**）：

- 0x29F800 每帧：+0xE94 递减；把物理矩阵 +0xC90 拷到 +0x60；若已生成诱饵（+0x14C8）且诱饵 weak_ptr（+0x14C0）失效，或 core+0xAF4 bit0（死亡），在 0x29FB51 调 Delete(self)。
- 状态 0x29FC80：事件 1 时若 #0==1 且已着地（+0xD40）→ 切到 0x29FC60，倒计时 0。
- slot9 0x29F640：消息 0x2100，仅当 +0xE94==0 才接受，倒计时 +0xE9C = #2 × 参数，切 0x29FC60。
- 0x29FC60：+0xE9C 倒数到 0 → **生成函数 0x29E780**。

CP 含义（推断）：#0=1 着地即展开；#1=60 帧武装时间（之前不接受 0x2100 触发）；#2=32 每单位延迟；#3=可选子弹模型。

### 2.3 生成诱饵对象：0x29E780（只执行一次，置 +0x14C8=1）

**确认**（逐条指令）：

1. 按键名 `decoy_sgo` 从 CP 取路径。
2. 栈上构造 **InitParam@Decoy**，0x38 字节：`+0 vtable 0x17A4FF0`，`+8..+0x2F = 0`，`+0x30 byte = (owner+0x128 & 1)`（owner 来自 bullet+0x120 的 vfunc+8；这是网络「远端拥有」标志）。
3. `CreateObject(*(img+0x20B2958), &bullet+0x60, path, &ip)`（0x29E986）。
4. dynamic_cast 到 GameObjectBase（TD 0x2006400），weak_ptr 存 bullet+0x14B8/+0x14C0。
5. `0x118AF20(bullet, decoy)`：子弹成为诱饵的父。
6. **`SetTeam(decoy, 4, 1)`**。
7. dynamic_cast 到 Decoy（TD 0x2026A50），调 **Decoy_Setup `0x5ACC30`(rcx=decoy, rdx=&bullet+0x60（矩阵指针）, xmm2=float bullet+0xB4C, r9d=int bullet+0xB48)**。
   bullet+0xB48 = core+0xA08 = AmmoAlive，bullet+0xB4C = core+0xA0C = AmmoDamage（core = bullet+0x140，core+0x970+X = initparam+X，
   与武器加载器 0x68A920 写 weapon+0x898 AmmoAlive / +0x89C AmmoDamage 的布局一致 → **推断，证据一致**）。
8. 联机时（GameStatus 判断）发生成包 0x7813A0。
9. 没有 decoy_sgo 时走旧路径 DecoyBody01（InitParam@DecoyBody01 vtable 0x17A4FE0，CP 4..7）。

所以 CP 的 decoy_sgo 就是「生成哪个对象」，诱饵 HP = AmmoDamage(360)，寿命 = AmmoAlive(36000 帧 = 10 分钟)。

### 2.4 Decoy 类（诱饵对象本体）

| 项 | 值 |
|---|---|
| Factory vtable | 0x17D3F40，create **0x5ABD80**：new 0x1120 → ctor **0x5AC240**(obj, initparam) |
| vtable | **0x17D3F88**；继承 Decoy : BasicAnimationCharacter（ctor 0x314AD0）: GameObjectBase |
| Setup | **0x5ACC30**(Decoy*, const Matrix* follow, float hp, int aliveFrames) |
| update(slot5) | **0x5ACCE0** |
| 死亡(slot48) | 0x5ACB90 → 状态 0x5AECA0（destroy 动画） |
| 网络 | slot47 0x5AC9D0、0x5AD180/0x5AD2C0 序列化（看 +0x128&1）；slot53 0x5AD3C0 反序列化 |
| slot52 | 0x5ACCB0 返回 1（基类返回 0）；只在 0x317670 选物理/碰撞类型（+0x5C0）用，**与吸引无关**（确认） |

字段（**确认**写入点）：+0x10D0 跟随矩阵指针（ctor 置 0）；+0x10D8 剩余寿命（ctor 置 -1）；+0x10E0 待播应答语音；+0x10F0 rotation 欧拉角；
+0x1100 动画表；+0x1058 声音发射器；+0x1030 状态机；+0x1118 网络计数。ctor 读 DecoySettings 的 standby_time / animation_table / rotation。

Setup 0x5ACC30（**确认**）：`+0x2F4 = +0x2F8 = hp`（覆盖对象 SGO 的 durability 200），`+0x484 = hp × +0x480 / +0x2EC`，`+0x10D0 = follow`，`+0x10D8 = aliveFrames`。

update 0x5ACCE0（**确认**）：

1. +0x10D0 非空 → 每帧把它指向的 64 字节矩阵拷到 +0x60..+0x9F；
2. 用 rotation 算渲染矩阵 +0x860；
3. +0x10D8 > 0 时递减，到 0 → 状态 0x5AECA0（destroy）；
4. 基类 update 0x317B20、状态机 tick、声音 0x7A8C20（位置 +0x90）、+0x10E0 非空则 PlayAnswerVoice **0x5AD5B0**（0x5AD63A 处调 TeamManager 0x5E0E40，推断：找附近友方来「应答」）。

### 2.5 CustomParameter / 对象 SGO 内容（**确认**）

对象 `E_THROW_DECOYSCREEN_{AYAME,MIO,FUBUKI,GURA,KIARA,IRYS}.SGO`：`xgs_scene_object_class 'Decoy'`，durability 200（被 Setup 覆盖），
ragdoll .shkt，BasicAnimationCharacterSettings，animation_model（.mrab/.cas/.efarc）。

DecoySettings：`standby_time {min 120, max 180}`；`rotation [0, π, 0]`；animation_table = wakeup / standby(idle_default_loop) / random_action / damage(8) / destroy；
每条 `{animation, material_animation, priority, voice[, answer{delay, table, is_wait_end}]}`。

**唱歌跳舞来自对象 SGO 的 random_action，不在子弹里：**

| 诱饵 | random_action 条数 | 歌/舞 |
|---|---|---|
| Ayame | 37 | song_edf01 / 02B / 02C，dance_holo_onikemo_cut（语音 IROHANI_ONIKEMODANCE_FINAL_MIX_CUT） |
| Mio | 39 | 同上 |
| Fubuki | 39 | 同上 |
| Gura | 33 | 只有 song_edf01（G_SONG1/2），无舞 |
| Kiara | 38 | 只有 song_edf01（K_SONG1/2），无舞 |
| IRyS | 33 | 只有 song_edf01（I_SONG1/2），无舞 |

待机 standby_time 120~180 帧后随机挑 random_action（推断：按 priority 加权）。

### 2.6 诱饵怎么吸引敌人

**确认**：生成后 `SetTeam(decoy, 4, 1)`。TeamManager（vtable 0x17D71F8，ctor 0x5E03C0，全局 `*(img+0x20B2978)`）7 支队伍，
关系表在 mgr+0x38 + team×0x38，+0x18 是 int 数组；对角=1，0–1=2（敌对），0–2=1，2–1=2，**4–1=2**：队伍 4 只与队伍 1（敌人）敌对。
`EnumConflictObject 0x5E0F20(mgr, team, cb)` 枚举所有关系=2 的对象（跳过 +0x2E8 已死、+0x380 bit 0x80）。

**推断**：吸引就是「诱饵在敌人眼里是个合法敌对目标」。静态没找到 decoy 专属的仇恨优先级代码；它被敌人 AI 的普通选目标逻辑选中（离得近、站着不动、不会还手）。

### 2.7 能不能移动

**推断（强）**：能。Decoy_Setup 第二个参数就是「跟随矩阵指针」，update 每帧拷 64 字节到 +0x60。原版里它指向子弹的 +0x60（核落地后不动，所以诱饵也不动）。
插件把 decoy+0x10D0 指向**插件自己持有、生命周期覆盖诱饵的** 64 字节矩阵，每帧更新它即可让诱饵跟着走；或者把 +0x10D0 置 0、自己写 +0x60..+0x9F。

风险：碰撞/物理体（BasicAnimationCharacter 的刚体、ragdoll）是否跟着 +0x60 走没有确认；敌人锁定点大概率取 +0x60/+0x90（与 Decoy 自己更新声音位置用 +0x90 一致），但没逐个敌人 AI 验证。
**不要**把 +0x10D0 直接指向直升机的 +0x60：直升机被删时就是悬垂指针。

### 2.8 能不能在敌人附近爆炸

**确认否**：Decoy 的寿命到期（+0x10D8→0）和死亡（slot48）都只切到 destroy 动画状态 0x5AECA0，没有找到任何 ApplyAreaDamage / 爆炸子弹调用。
要爆炸得用第 1 节的方法（直升机派生武器）。

### 2.9 插件能否自己生成诱饵

**推断（调用链全部来自 0x29E780 的确认指令，未实测）**：能，走现有 CreateObject 路径即可：

1. **预载**：任务加载期调 `0x7A3780(*(img+0x20B29A8), L"app:/object/e_throw_decoyscreen_ayame.sgo", 2, -1)`（与 jet 预载同一个时机/包装）。
   或者更省事：让直升机/玩家的某把武器 SGO 的 `resource` 列上这个对象（诱饵武器本身就是这么做的）。
2. `struct alignas(16) InitParamDecoy { const void* vtable /*img+0x17A4FF0*/; uint8_t zero[0x28]; uint8_t remote /*=0*/; uint8_t pad[7]; };`（0x38 字节，+0x30 填 0 = 本地拥有）。
3. `obj = CreateObject(*(img+0x20B2958), &matrix, path, &ip)`（同 jet.cpp，包在 __try 里）。
4. `SetTeam(obj, 4, 1)`。
5. `Decoy_Setup(obj, followMatrix /*插件持有*/, hp, aliveFrames)` —— rcx=obj，rdx=const Matrix*，xmm2=float hp，r9d=int frames（第三参是 float，第四参 int 用 r9d，确认于 0x29E780 的调用点）。
   该调用前应先 dynamic_cast 确认是 Decoy；插件可直接比 vtable == img+0x17D3F88。
6. 收尾：没有父子弹，就没有「子弹没了诱饵也删」的联动；靠寿命/打死，或插件自己 Delete 0x118A1B0。插件被卸载/任务结束前必须先把 +0x10D0 置 0 或删掉诱饵，避免指向已释放的矩阵。

DLC 风险：对象 SGO 在 Root.cpk 里能读到（确认），但 Ayame/Mio/Fubuki 是 DLC_、Gura/IRyS/Kiara 是 INDLC_，对应的模型/动画/语音资源是否要求拥有 DLC 未确认。
没装 DLC 时 CreateObject 可能返回空或资源缺失，插件必须判空。

联机：0x29E780 联机时会发 0x7813A0 生成包；插件自建诱饵不发，**只在单机可靠**（推断）。

---

## 3. 推荐方案：「会唱跳、会移动、贴近敌人自爆的人偶无人机」

最小、最稳的组合（不碰 GameDamageInfo，不改诱饵类）：

1. **载体**：插件已控制的 Vehicle506_Helicopter（或 jet）当「无人机」。
2. **人偶**：任务加载时预载 e_throw_decoyscreen_ayame.sgo；直升机生成后插件 CreateObject 一个 Decoy，SetTeam 4，
   Decoy_Setup(obj, &plugin_matrix, hp, 很大的寿命)；插件每帧把 plugin_matrix = 直升机矩阵 × 偏移（例如机腹下 2 m）。
   唱跳由 Decoy 自己的 random_action 播放，不用管。（推断，需实测矩阵跟随与碰撞）
3. **吸引**：Decoy 在队伍 4，敌人会去打它（确认队伍关系；吸引效果推断）。
4. **爆炸**：直升机 2 号武器换成 GrenadeBullet01 派生弹（CP#0=1、#3=0、#5=0、AmmoSpeed≈0、AmmoAlive 1~3、AmmoExplosion/AmmoDamage 按需）。
   插件用 EnumConflictObject 或已有敌人列表算最近距离，< R 时写 veh+0x2021=1 一帧；需要「自毁」就随后 Delete 诱饵与直升机。
5. **清理**：任何路径删除直升机前，先 Delete 诱饵或把 decoy+0x10D0 置 0。

风险清单：

| 风险 | 级别 | 说明 / 缓解 |
|---|---|---|
| 诱饵碰撞体不跟矩阵走 | 中 | 只确认了 +0x60 每帧被覆盖；刚体可能留在原地。实测：移动后用子弹打旧位置/新位置。不行就退回「诱饵钉在一点、直升机去撞」 |
| 悬垂矩阵指针 | 高 | +0x10D0 指向插件内存，必须在插件结构释放前清零；绝不指向游戏对象 |
| DLC 资源缺失 | 中 | CreateObject 判空；改用玩家拥有的那一只 |
| 预载时机 | 中 | 0x7A3780 只在任务加载期安全（已有结论）；或用武器 `resource` 让引擎自己预载 |
| 爆炸归属 | 低 | 由直升机开火，归属 = 直升机队伍；直升机队伍必须是玩家方 |
| 友伤 | 低 | GDI+0x60 bit 0x20（core+0xA26）会关掉敌我过滤；GrenadeBullet stock 是否带这个位未查，派生时不要改相关字段 |
| 联机 | 中 | 插件自建的诱饵不同步；只按单机设计 |
| 诱饵 HP | 低 | Setup 的 hp 覆盖 durability；给大值即可让它不被秒 |

## 4. 未完成 / 待实测

- 消息 0x2100 的发送方（推断是落地/触发事件），不影响方案。
- 爆炸特效在 GrenadeBullet01 里的具体生成点没有逐条追（只追到伤害和死亡回调 core+0xCD8）。
- GDI +0x5C/+0x64/+0x68 的确切含义。
- weapon+0x890（→ GDI+0x24 队伍）在开火时由谁写入。
