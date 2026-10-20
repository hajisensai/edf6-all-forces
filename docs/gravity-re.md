# 重力操作 / 重力枪 / 悬浮炮：逆向笔记（`src/gravity*.{h,cpp}`、`src/gravitygun.cpp`、`src/floatgun.cpp`）

EDF.dll Steam 版 TimeDateStamp `0x678CCB46`，下文地址全部是 RVA。**全部静态分析**（`tools/edfre.py` + capstone、Root.cpk 只读解析），2026-10-20
由四个并行的静态逆向任务完成（角色 / 刚体 / 武器 / M099）；实现后于同日用 autopilot 后台实测，结果与更正见 §10。置信度：**H** = 指令直接可见；**M** = 强推断；**L** = 需实机核对。
分析脚本当时在任务临时目录（`.../jobs/73c2842a/tmp/re-a`、`reB`、`rec`、`reD`），不入库。

用户需求（2026-10-20）：「看看这里面的重力枪，咱们也做一个。要做的话最好让其根据速度和质量造成伤害。同时把"操作重力"做成函数之类的方便操作，
以后要经常使用它。我希望给 银之人（5代主线最后一关）增加可以拾取怪死后掉落的武器/载具坏掉之后掉落的炮塔，在周围进行自动攻击（类似悬浮炮？）」
参考：用户给的第三方插件 `EDF6Plugins.zip`（ParameterHUD，MIT，Quarri6343）的 `Weapon_Test.hpp`（物理枪：自定义 SGO 武器类 `Weapon_Test`、
LinkLineEffect 光束、按「目标点 − 当前位置」× 2 设速度、伤害代码被注释掉）。本仓库的做法不照搬它：不注册新 SGO 类（不装插件时游戏建不出那把枪），
光束用原版激光导引装置自带的，抓取按各类对象的原生控制器分派（参考把狂战士、各类行走敌人落到「通用刚体」分支，对它们无效），伤害按质量和速度。

## 0. 一页结论


| 家族 | 速度入口（推荐） | 偏移 | 着地读法 | 死后 |
|---|---|---|---|---|
| InsectBase（蚁/蛛/蜂/团子虫/小龙） | `CharacterControl_Cling::Launch 0x6DAA10(ctrl, &vel, frames)` | ctrl = obj+0xE00 | ctrl+0x70 byte | ragdoll obj+0xB10（cling 刚体被移出世界） |
| Nephila | 同上 | obj+0xDE0 | 同上 | ragdoll obj+0xB00 |
| WalkingEnemyBase（全部 EDF6_Berserker*） | 同上 | obj+0x1160 | 同上 | — |
| HumanoidBase（殖民者 E506 / 青蛙 E503 / 银之人 E516 / 火星人 / 金星人） | 写 walk+0x30 速度，再调 `Walk::Detach 0x11B9880(walk)` | walk = obj+0xB40 | walk+0x91 == 2 | ragdoll 指针 [obj+0xA78] |
| MonsterBase（501/504） | 同上 | obj+0x1120 | 同上 | ragdoll obj+0xDA0 |
| HumanBase（玩家/EDF 士兵/平民） | 同上（ragdoll 倒地态时改用 ragdoll） | obj+0x680 | 同上 | ragdoll obj+0xD90 |
| 其它 Walk 用户 | 同上 | Merman +0x1800、FlyingDyno +0x10F0、SquidSmall +0xF90、GiantDangoLarge +0xFF0、AlienTrailer +0xAE0 | 同上 | — |
| 尸体 / 任意 ragdoll | `Ragdoll::SetLinearVelocityAll 0x6ECF20(ragdoll, &vel)` 或单刚体 `0x11B18F0` | 见 §5 | — | — |

- **没有「给任意 GameObject 速度/冲量」的单一原生入口（M）**：游戏自己的爆炸击飞是每个家族在自己的受伤路径里各写各的
  （InsectBase 系 → cling Launch；HumanBase → ragdoll 速度 / 点冲量），GameObjectBase 虚表里没有速度槽。插件必须按家族分派。
- **参考插件的漏洞**：Berserker 全家用的是 Cling（WalkingEnemyBase+0x1160），不是 Walk，参考把它们落到「通用刚体」分支；
  GiantBee 飞行时 cling 被设成 mode 1，Launch 无效（§2.4）；参考拒绝抓尸体（`m_is_dead` 判定）。

---


### 0.1 对既有结论的更正（刚体侧）


| # | 原说法 | 实际 | 证据 | 置信 |
|---|---|---|---|---|
| a | `docs/raycast-re.md` §2.3 把 `0x11B1960` 写成「运动类型」 | `0x11B1960` 是 **SetMotionType**(Body*, int)，getter 是 **`0x11B14A0`** | 0x11B1960 先算出当前类型，不等时调写槽 27 `[+0xD8]`。0x11B14A0 只读 `body+0x54` | H |
| b | `src/jetprops.cpp`、`docs/jet-model-re.md` §7：「写接口 vtable 槽 32 = 0xE50720」 | 写接口 vtable 是 `0x18BC740`，不是 `0x18BC720`（后者是 offset 0 的主 vtable）。`0xE50720` 在**槽 28（+0xE0）**；槽 32 是 `0xE4FBF0`。所以 jetprops 的运行时门 `vtable[32]==image+0xE50720` **永远不成立，`JetMotionProps` 从不生效** | ctor `0xDA3140`：`[X]=0x18BC720`、`[X+0x18]=0x18BC740`、`[X+0x20]=0x18BC9C0`。`q(0x18BC740+28*8)=0xE50720`，`q(…+32*8)=0xE4FBF0` | H（静态）；实机没核 |
| c | `HavokRelay.h` 里 hknpMotionProperties 的 `+0x20 m_timeFactor` / `+0x24 m_gravityFactor` | **`+0x08` gravityFactor，`+0x0C` timeFactor**。`+0x20`、`+0x24` 是别的字段：DEBRIS 预设 +0x20 = 0.2，+0x24 = \|g\|×常数 | 预设函数 `0xDCFA10`：DYNAMIC 写 `+8=1.0`，KEYFRAMED 写 `+8=0`，所有预设都写 `+0xC=1.0` | H |
| d | `docs/decoy-blast-re.md` §1.1：0x542490 的 `[rsp+0x20]` 是「uint frame」 | 实际是 **collisionFilterInfo**。来源 `0x106070`/`0x106040`，就是 `0xD929F0(layer=13/12, 0, 0, 0)` 生成 filterInfo 的小函数 | `0x106070: lea ecx,[rdx+0xd]; jmp 0xD929F0` | H |
| e | 「veh+0x13A8 是部件」 | `veh+0x13A8` 是部件**个数**。部件数组在 `veh+0x1398`。两者都属于 Ragdoll 组件 `veh+0x1320`（数组在 +0x78，个数在 +0x88） | `0x663AB6 lea r13,[rcx+0x1320]`；`0x664337 add rbx,[r14+0x1398]`；`0x664372 cmp rax,[r14+0x13A8]` | H |
| f | 「包装 +0xF0 是 body id」「0x108260 参数是 id」 | **两条都对** | 见 §1 | H |

---


## 1. RTTI 与类族判定


#### 1.1 参考的五个 TypeDescriptor（H，名字读自 TD+0x10）

| RVA | 名字 | 参考 |
|---|---|---|
| 0x202A270 | `.?AVInsectBase@@` | 对 |
| 0x203B5B0 | `.?AVHumanoidBase@@` | 对 |
| 0x203E308 | `.?AVMonsterBase@@` | 对 |
| 0x2016A98 | `.?AVHumanBase@@` | 对 |
| 0x2006400 | `.?AVGameObjectBase@@` | 对 |
| 0x2006450 | `.?AVSceneObject@game@xgs@@` | 对（源类型） |

`__RTDynamicCast` thunk = **0x12DA7AA**（`ff 25 e8 b8 47 00` = jmp [IAT]，H；仓库 `src/stockgauge.cpp:43`、`src/debug_spawn.cpp:30` 已在用）。
调用形如 `cast(obj, 0, base+0x2006450, base+<目标TD>, 0)`。

另需的 TD（H）：SoldierBase 0x2006428、WalkingEnemyBase 0x202A678、BasicAnimationCharacter 0x20163F8、
EDF6_BerserkerBase 0x202A648、EventAnimationObject（见仓库 edf41 笔记）。

#### 1.2 具体类主虚表（COL offset 0，H）

| 家族 | 类（SGO 例） | vtable |
|---|---|---|
| InsectBase 0x17C5140 | GiantAnt（GIANTANT01、E650_*） | 0x17BB798 |
| | GiantAntEX（E662 红女王、E665） | 0x17BC648 |
| | GiantSpider（GIANTSPIDER01、E651_*） | 0x17BE120 |
| | GiantSpiderEX（E667_*） | 0x17BE7E0 |
| | GiantBee / GiantBeeEX | 0x17BCEE0 / 0x17BD638 |
| | GiantDango（E514） / DragonSmall | 0x17BDAC0 / 0x17ABA40 |
| HumanoidBase 0x17C29C0 | Humanoid_Basic（**E506_BIGGREY_\***、E503 青蛙） | 0x17C4D70 |
| | Humanoid_BigGreyBoss（**E516_BIGGREYBOSS 銀の人**） | 0x17BEC68 |
| | EDF6_Martian / EDF6_Venus / EDF6_SoftBodyHumanoidBase | 0x17C0BD0 / 0x17C24B0 / 0x17C1EF8 |
| MonsterBase 0x17C5FA8 | Monster501 / Monster504 | 0x17C5628 / 0x17C5A60 |
| HumanBase 0x17CFD20 | SoldierBase 0x17D24D8：AssultSoldier（游骑兵）0x17CDF28、PaleWing（飞行）0x17D0FF8、Engineer（空袭）0x17CF100、HeavyArmor（剑兵）0x17CF5B8 | |
| | People（平民） | 0x17D1868 |
| WalkingEnemyBase 0x17CCB98 | EDF6_BerserkerBase 0x17AC4B8；_TypeA 0x17AE968、_Middle 0x17AE088、_Large 0x17ADA78、_TypeBommer 0x17AED50、_TypeGrapple 0x17AF200、_TypeShooter 0x17AF9F8 | |
| BasicAnimationCharacter 0x17A9F18 | EDF6_Merman 0x17B5370、EDF6_FlyingDyno 0x17B6270、EDF6_SquidSmall 0x17B8898、EDF6_GiantDangoLarge 0x17AFEE0 … | |
| 其它 | Nephila 0x17C84C8、AlienTrailer 0x17A9650、EventAnimationObject 0x17D4D20 | |

SGO → 类映射见仓库 `docs/feedback-2026-10-19-enemy-control-coverage.md`。

#### 1.3 不调用 RTTI 也能判族（H）

1. **精确类**：`*(uintptr_t*)obj - image` 与上表比；构造函数里直接写这些 vtable（如 InsectBase ctor 0x4A6830 在 0x4A6889 写 0x17C5140）。
   子类各有自己的表，所以只适合「白名单具体类」。
2. **族（推荐）**：沿 vtable[-1] 的 COL 读层次描述符，纯内存读，等价于 RTTI 但不调函数：

```cpp
// MSVC x64 RTTI: vt[-1] -> COL{sig, off, cdOff, tdRva, chdRva, selfRva}; CHD{sig, attr, nBase, bcaRva}; BCA[i] -> BCD{tdRva,...}
bool IsA(const void* obj, uint32_t tdRva) {            // e.g. 0x202A270 InsectBase
    auto vt  = *reinterpret_cast<const uintptr_t* const*>(obj);
    auto col = reinterpret_cast<const uint32_t*>(vt[-1]);
    if (col[0] != 1 || col[1] != 0) return false;        // primary vtable only
    auto chd = reinterpret_cast<const uint32_t*>(image + col[4]);
    auto bca = reinterpret_cast<const uint32_t*>(image + chd[3]);
    for (uint32_t i = 0; i < chd[2]; ++i)
        if (*reinterpret_cast<const uint32_t*>(image + bca[i]) == tdRva) return true;
    return false;
}
```
   （读之前用 SEH 或先校验 vt 落在 .rdata、col[5] == col 的 RVA。）
3. **槽函数**：InsectBase 族第 4 槽都是 0x4A8FD0（GiantAnt/EX、Bee/EX、Spider/EX、DragonSmall，H），但 GiantDango 第 4 槽是 0x42D560
   ——槽法易漏，不推荐。

---


## 2. 刚体包装与操作（`xgs::havok::Body`）


- **类名**：`xgs::havok::Body`（H）。它被 `shared_ptr` 持有，`_Ref_count<VBody@havok@xgs>` 的 RTTI 在 `0x11AA979`。
- **大小**：0x110（H）。`0x11AEF00`：`new 0x110` 后调构造 `0x11B0580`。
- 没有 vtable。`+0x00` 是 `enable_shared_from_this` 的 weak_ptr，`0x11AD220` 就从这里 lock（H）。

| 偏移 | 类型 | 含义 | 置信 |
|---|---|---|---|
| +0x00/+0x08 | weak_ptr\<Body\> | 指向自己（shared_from_this） | H |
| +0x10 | 0x50 字节 | 材质副本（从 `X+0x920` 材质库按 `+0xF4` 拷来） | M |
| +0x60 | hknpMotionProperties（0x70 字节） | 运动属性副本 | H |
| +0xD0/+0xD8, +0xE0/+0xE8 | shared_ptr | 构造参数里带进来的两个对象 | M |
| **+0xF0** | u32 | **hknp body id**（低 24 位是索引，0xFFFFFF 表示无效） | H |
| +0xF4 | u16 | 材质 id（`hknpBody+0x8A`） | H |
| **+0xF6** | u16 | 运动属性 id（`X+0x90[id&0xFFFFFF].+4`） | H |
| +0xF8 | u16 | 构造时写 2 | H（含义 L） |
| **+0x100** | ptr | 世界包装 W；`*(W+0x58) = X` | H |

#### 2.0.1 id、Body、SceneObject 互转

| RVA | 签名 | 说明 | 置信 |
|---|---|---|---|
| `0x11AD2E0` | `xgs::havok::Body* __fastcall GetBody(void* W, u32 bodyId)` | 先调读槽 13 `isBodyValid`（`0xDAC200`：`flags&3` 且 `+0x50==id`），再取 body 属性 **key 0x8001**（size 8）。返回裸指针，不加引用；无效时返回 0。陈旧 id 也安全 | H |
| `0x11AD220` | `shared_ptr<Body>* __fastcall GetBodyShared(void* W, shared_ptr<Body>* out, u32 bodyId)` | 同上，再对 `+0` 的 weak 做 lock。失败时抛 `bad_weak_ptr`（`0xB97A0`） | H |
| `0x108260` | `SceneObject* __fastcall BodyToObject(u32 bodyId)` | **参数是 id**。内部 `0x11AD2E0(g+0x10, id)`；有 Body 时再用 `Body+0xF0` 取属性 **key 0**（size 8），返回它的值。没有 Body 或没有 key 0 时返回 0 | H |
| 读槽 29 `0xDAB360` | `void* getBodyProperty(X+0x20, u32 id, u16 key, u64 size)` | 返回属性值的地址，没有则返回 0 | H |
| 写槽 45 `0xDAF270` | `setBodyProperty(X+0x18, u32 id, u16 key, const void* val, u64 size)` | 已知 key：0 = 拥有者 SceneObject*；2 = BarrierInfo（`shield.cpp`）；4 = 部件下标（地图物件）；0x8001 = Body* | H（key 语义 M） |

- key 0 的值按 `SceneObject*` 存（H）。地图物件的接触回调 `0x12ED80` 对它做 `__RTDynamicCast(SceneObject 0x2006450 → MapObject_Base 0x200F080)`。

#### 2.0.2 hknpBody（0xB0，`bodies = *(X+0x38)`，个数 `*(X+0x40)`）

| 偏移 | 含义 | 置信 |
|---|---|---|
| +0x00..+0x2F | 旋转三列（float4×3）。三列的 w 分量拼成局部质心（`0x11B1100`） | H / M |
| +0x30 | 位置（float4）。`0x11B15B0` 返回 `&body+0x30` | H |
| +0x40 | 位置的低位补偿（float4）。`setBodyPosition` 用 `(double)+0x30 + (double)+0x40` 当真实位置 | M |
| +0x50 | id | H |
| +0x54 | flags：bit0 STATIC、bit1 DYNAMIC（已加入）、bit2 KEYFRAMED、bit3 ACTIVE；其余位见 §5 | H（bit3 是 M） |
| +0x78 | shape* | H |
| +0x80 | motionId | H |
| +0x88 | u8，非 0 = 已在世界里 | M |
| +0x8A | u16 材质 id | H |
| +0x8C | u32 collisionFilterInfo（`0x11B1240` 读，写槽 35 写） | H |

#### 2.0.3 hknpMotion（0x80，`motions = *(X+0x178)`，`motion = motions + motionId*0x80`）

| 偏移 | 含义 | 置信 |
|---|---|---|
| +0x00 | 朝向四元数 | H |
| +0x10/+0x18/+0x20 | 世界质心，**double×3** | H |
| +0x40 | 4×hkHalf（float 的高 16 位）：xyz = 局部逆惯量，**w = 逆质量** | H |
| +0x48 | 第一个挂接的 body id | H |
| +0x58 | u16 motionPropertiesId | H（`0xDF76D0`） |
| +0x60 | 线速度 xyz；**w = 最大线速度**（来自 props+0x10） | H |
| +0x70 | 角速度（局部坐标）xyz；w = 最大角速度（来自 props+0x14） | H |

---



调用约定都是 MS x64 `__fastcall`。

- 「写槽」都会先往 `X+0x868` 发一条 API 命令（命令号在 `0x00XX0200` 里）。
- 写槽的 activation 参数为 0 表示 ACTIVATE。速度和冲量类写槽的处理：
  1. body 未加入世界（`flags&2==0`）时什么也不做。
  2. body 在世界里（`+0x88!=0`）、不是 static|active（`flags&9==0`）、activation==0 时，调 `0xDAFF20(X, id, 0)` 激活。
  3. 再写速度，并按 `motion+0x6C` 夹住最大速度。

  也就是说，**设速度会自动唤醒休眠刚体**（H，`0xE4FE46..0xE4FE64`）。

> 16 字节签名是 RVA 处的文件字节，插件按固定 RVA 逐字节比对。「16B 命中」是这 16 字节在整个 .text 出现的次数。>1 时用右栏的更长字节区分。

#### 2.1 EDF 自带的 Body 包装

| RVA | 签名（C++） | 实际调用 | 前 16 字节 | 16B 命中 / 区分 | 置信 |
|---|---|---|---|---|---|
| `0x11B18F0` | `void SetLinearVelocity(Body*, const float4* v)` | 写槽 21 `[+0xA8]` `0xE4FD90(X+0x18, id, v, 0)`，tail-jmp | `48 8B 81 00 01 00 00 4C 8B C2 8B 91 F0 00 00 00` | 5；`+0x1F` 是 `FF A0 A8 00 00 00` | H |
| `0x11B1760` | `void SetAngularVelocity(Body*, const float4* w)`（世界系，M） | 写槽 22 `[+0xB0]` `0xE4F620` | 同上 | 5；`+0x1F` 是 `FF A0 B0 00 00 00` | H |
| `0x11B1300` | `const float4* GetLinearVelocity(Body*)` | 返回 `&motion+0x60`。**w 是速度上限，不是 0** | `40 53 48 83 EC 20 48 8B 81 00 01 00 00 48 8B D9` | 3；`+0x4A` 是 `48 83 C0 60` | H |
| `0x11B1060` | `float4* GetAngularVelocity(Body*, float4* out)`（世界系） | `0xDF68A0(motion,out)` | `48 89 5C 24 18 57 48 83 EC 40 48 8B 05 E7 DF E3` | 1（含 cookie 位移） | H |
| `0x11B1520` | `float4* GetPointVelocity(Body*, float4* out, const float4* worldPt)` | `0xDF6A50`：v + ω×(p−com) | `40 53 56 57 48 83 EC 40 48 8B 05 29 DB E3 00 48` | 1 | H |
| `0x11B15B0` | `const float4* GetPosition(Body*)` | `&hknpBody+0x30` | `48 83 EC 28 48 8B 81 00 01 00 00 8B 91 F0 00 00` | 4；`+0x23` 是 `48 83 C0 30` | H |
| `0x11B1460` | `void GetTransform(Body*, float m[16])` | `0x97D870(body, m)`，tail-jmp | `40 53 48 83 EC 20 48 8B 81 00 01 00 00 48 8B DA` | 1 | H |
| `0x11B1190` | `float4* GetCenterOfMassWorld(Body*, float4* out)` | motion+0x10 的 double3 转 float | `48 89 5C 24 18 57 48 83 EC 40 48 8B 05 B7 DE E3` | 1 | H |
| `0x11B1100` | `float4* GetCenterOfMassLocal(Body*, float4* out)` | body 旋转三列的 w 分量 | `40 53 48 83 EC 40 48 8B 05 4B DF E3 00 48 33 C4` | 1 | M |
| **`0x11B13D0`** | **`float GetMass(Body*)`**（xmm0） | `1/half(motion+0x46)`；逆质量为 0（static / keyframed / 无穷大质量）时返回 **0**。单位按 Havok SI 是 kg（M：世界重力 14.7 m/s²，长度单位是米） | `40 53 48 83 EC 20 48 8B 81 00 01 00 00 48 8B D9` | 3；`+0x4A` 是 `0F 57 D2 F2 0F 10 40 40` | H |
| `0x11B1270` | `float4* GetInertiaAndMass(Body*, float4* out)` | xyz = 1/局部逆惯量，w = 质量 | `48 89 5C 24 08 57 48 83 EC 20 48 8B 81 00 01 00` | 3；`+0x2F` 是 `0F 57 D2 44 8B 80 80 00` | H |
| 逆质量 | 无包装 | 自己读：`half→float(*(u16*)(motion+0x46))` | — | — | H |
| `0x11B1930` | `void SetMass(Body*, float mass)`（xmm1） | 写槽 31 `[+0xF8]` `0xE4FF50(X+0x18, id, mass, 0)`：写 `X+0x90[id].+0`；动态体还会调写槽 52 `0xDADAB0` 重建质量属性。布娃娃 mode 4 用它写部件质量 | `48 8B 81 00 01 00 00 45 33 C9 8B 91 F0 00 00 00` | 2；`+0x1F` 是 `FF A0 F8 00 00 00` | H（语义 M） |
| `0x11B14A0` | `int GetMotionType(Body*)` | 0 = static（flags&1），1 = keyframed（flags&4），2 = dynamic | `48 83 EC 28 48 8B 81 00 01 00 00 8B 91 F0 00 00` | 4；`+0x23` 是 `8B 48 54 F6 C1 01` | H |
| `0x11B1960` | `void SetMotionType(Body*, int type)` | 类型不变时直接返回；否则调写槽 27 `[+0xD8]` `0xE507F0(X+0x18, id, type, 0, 0)` | `48 89 5C 24 08 57 48 83 EC 30 48 8B 81 00 01 00` | 1 | H |
| **`0x11B0CB0`** | **`void ApplyPointImpulse(Body*, const float4* impulse, const float4* worldPt)`** | 写槽 26 `[+0xD0]` `0xE4E780(X+0x18, id, J, P, 0)`：`v += J·invM`，`ω += I⁻¹·((P−com)×J)`，都夹到上限。EDF 内有 30 处调用 | `48 83 EC 38 48 8B 81 00 01 00 00 4D 8B C8 4C 8B` | 1 | H |
| 线冲量 / 角冲量 | 无包装 | 写槽 24 `[+0xC0]` `0xE4EAD0(X+0x18, id, const float4* J, act)` 是 applyLinearImpulse（`v += J·invM`，H）；写槽 25 `[+0xC8]` `0xE4E550` 是 applyAngularImpulse（M）。EDF 只在 `0x11B0B20` 附近用到 | — | — | H / M |
| `0x11B1A00` | `void SetPosition(Body*, const float4* p)` | float 转 double 后调写槽 18 `[+0x90]` `0xE51040(X+0x18, id, const double4*, 0)`。**只改位置，不改速度** | `40 55 48 83 EC 70 48 8D 6C 24 40 48 83 E5 E0 48` | 1 | H |
| `0x11B0A40` | `void MoveToKeyframed(Body*, const float m[16], float dt)`（xmm2） | 矩阵转四元数（`0x63DA0`）后调 `0x11B0B20` | `48 89 5C 24 20 57 48 83 EC 50 0F 29 74 24 40 48` | 2（24B 唯一） | H |
| `0x11B0B20` | `void MoveTo(Body*, const float4* pos, const float4* quat, ?, float dt)`（xmm3） | 两种情况：<br>• **在世界里**：`0xEA10B0` 按 1/dt 算出「下一步正好到达目标」的线 / 角速度，再调写槽 20 `[+0xA0]` setBodyVelocity。这是**硬关键帧**，会推开挡路的东西<br>• **不在世界里**：`0x98F400`（四元数转旋转）后调写槽 17 `[+0x88]` setBodyTransform(id, rot, double4 pos, 0) | `48 89 5C 24 10 55 56 57 41 56 41 57 48 81 EC 00` | 1 | H（`0xEA10B0` 的语义 M） |
| 带朝向瞬移 | 无包装 | 写槽 17 `[+0x88]` `0xE51570(X+0x18, id, const float4 rot[3], const double4* pos, int act)`；写槽 19 `[+0x98]` `0xE50D90(X+0x18, id, const float4* quat, int pivot, int act)` 是 setBodyOrientation | — | — | 槽 17 H（照 `0x11B0C49` 的用法）；槽 19 M，pivot 取值 L |
| `0x11B0940` | `void AddToWorld(Body*)` | 不在世界里时调写槽 13 `[+0x68]` | `40 53 48 83 EC 20 48 8B D9 E8 D2 DD FC FE 84 C0` | 1 | H |
| `0x11B1710` | `void RemoveFromWorld(Body*)` | 在世界里时调写槽 16 `[+0x80]` `0xDAE1B0`（"LtRemoveBodies"） | `40 53 48 83 EC 20 48 8B D9 E8 02 D0 FC FE 84 C0` | 1 | H |
| `0x17E720` | `bool IsInWorld(Body*)` | 读槽 14：`+0x88`，或 `X+0x150` 的待加入表 | `40 53 48 83 EC 20 48 8B 81 00 01 00 00 48 8B D9` | 3；`+0x22` 是 `FF 50 70 85` | H |
| `0x11B1020` | `void EnableBodyFlags(Body*, u32 flags)` | 写槽 43 `[+0x158]` `0xE4ED90`：`body+0x54 \|= flags & ~0xF` | `48 83 EC 38 48 8B 81 00 01 00 00 44 8B C2 8B 91` | 1 | H |
| 清 flags | 无包装 | 写槽 44 `[+0x160]` `0xE4EC90(X+0x18, id, flags, 0, 0)`：`&= ~flags` | — | — | H |
| `0x11B15E0` | `const hknpShape* GetShape(Body*)` | `body+0x78` | `48 83 EC 28 48 8B 81 00 01 00 00 8B 91 F0 00 00` | 4；`+0x23` 是 `48 8B 40 78` | H |
| `0x11B1240` / `0x11B1840` | `u32 GetFilterInfo(Body*)` / `void SetFilterInfo(Body*, u32, bool)` | `body+0x8C`；写槽 35 `[+0x118]` | 1240：同 15B0 前缀，`+0x23` 是 `8B 80 8C 00`；1840：`48 83 EC 38 48 8B 81 00 01 00 00 45 33 C9 4C 8B` | 1840：1 | H |

#### 2.2 唤醒、运动属性、重力

- **唤醒**：设速度 / 冲量 / setPointVelocity（写槽 20–26）在 activation=0 时会自动激活（H）。EDF 的包装都传 0。
  单独激活可直调 `0xDAFF20(hknpWorld* X, u32 id, int 0) -> bool`（hknp 内部函数，签名 `48 8B C4 44 89 40 18 48 89 48 08 55 53 48 8D 68`，唯一，M）。抓取期间每帧都在设速度，用不到它。
- **对 static 体设速度是危险的**：`0xE4FD90` 在 `flags&2==0` 时直接返回。但 static 体的 motionId 是共享的 0 号 motion，绝不能对它写速度。插件必须先判 `GetMotionType==2`（H 读码；危害程度 L）。
- **运动属性库**：`lib = *(X+0x928)`，`entries = *(lib+0x40)`，`count = *(int*)(lib+0x48)`，每项 0x70 字节（H）。
  - 默认 5 项由 `0xE155D0(lib, |g|)` 建立，下标 0..4 依次是 STATIC / DYNAMIC / KEYFRAMED / FROZEN / DEBRIS（`0xDCFA10` 的 switch，H）。
  - DYNAMIC：maxLinear 200、maxAng 100、gravityFactor 1；KEYFRAMED：gravityFactor 0；DEBRIS：100 / 10、阻尼 0.01 / 0.5。

  hknpMotionProperties 布局（H）：

  | 偏移 | 字段 |
  |---|---|
  | +0x00 | isExclusive，必须为 0 才会去重（jetprops 已知） |
  | +0x04 | flags |
  | **+0x08** | **gravityFactor** |
  | +0x0C | timeFactor |
  | +0x10 | maxLinearSpeed |
  | +0x14 | maxAngularSpeed |
  | +0x18 | linearDamping（`0xDEA61F`：`v *= max(0, 1 − d·dt)`） |
  | +0x1C | angularDamping |
  | +0x20 起 | 求解器 / 休眠参数 |
  | +0x54 | 5 / maxLinear |
  | +0x58 | 0.005 / maxLinear |

- **单个刚体改 gravityFactor（可行，H）**：
  1. 照 jetprops 的做法：拷一份 `entries[Body+0xF6]`，把 `+0` 清零，改 `+0x08`。
  2. 调 `0xE15190(lib, u16* outId, const props*)` 加进库（满时 outId = 0xFFFF；之后要重读 entries）。
  3. 调写槽 **28** `[+0xE0]` `0xE50720(X+0x18, u32 id, u16 propsId)` 换到新项。
  4. 同步 `Body+0xF6` 和 `Body+0x60`。

  0xE50720 会写 `X+0x90[id].+4`，非静态体还会调 `0xDF76D0(motion, propsId, &props)`，后者写 `motion+0x58` 并重夹 `+0x6C`/`+0x7C`。
  注意 props 属于 **motion**：一个 motion 只挂一个 body 时等于逐刚体（多 body 共用一个 motion 的情况，M）。
  顺手可把 maxLinear（`+0x10`，以及 `+0x54`/`+0x58`）调高：**默认 DYNAMIC 上限 200 m/s，投掷速度会被夹到这里**（H）。
- **世界重力**：`*(float4*)(X+0x900)`（H）。
  - `getCinfo 0xDAB480` 把 `X+0x900` 回写到 cinfo+0x70；ctor `0xDA22E1` 从 cinfo+0x70 读进来。
  - EDF 建世界时（`xgs::havok::World` 创建 `0x11AD340`）把 cinfo 重力设成常量 `0x1AE3220` = **(0, −14.7, 0, 0)**，即 1.5 g；broadphase 范围是 ±3000（H）。
  - 改全局重力会影响所有刚体，不推荐。
- 抓取不需要改重力：`MoveToKeyframed` 每帧按目标反算速度，一步内重力造成的下坠只有 g·dt²≈4 mm（M）。gravityFactor 只在想做「漂浮投掷」时才需要。

---


## 3. 角色控制器与对象的刚体

### 3.1 CharacterControl_Cling（昆虫 / Berserker 的 0x6DAA10）


类名 `xgs::havok::CharacterControl_Cling`（H：同区段有 `MemberSlot<CharacterControl_Cling>` 0x6D9F76、`FitTargetCollector@CharacterControl_Cling`
在 Update 0x6DB3AA 里用）。**无虚表**的结构体，ctor 0x6D9FF0，init 0x6DA410，Update 0x6DB200。

#### 3.1.1 0x6DAA10 = Launch（H，整函数 4 条指令）

```
c6 41 70 00            mov byte [rcx+0x70], 0      ; 离地
44 89 81 b8 00 00 00   mov [rcx+0xB8], r8d          ; 着地抑制帧数
0f 10 02 / 0f 11 81 80 movups [rcx+0x80], [rdx]     ; 弹道速度
c3
```
`void __fastcall ClingLaunch(void* ctrl, const float vel[4] /*m/s, w=1*/, int noLandFrames)`
fp `c6 41 70 00 44 89 81 b8 00 00 00 0f 10 02 0f 11`。

- 设的是**速度**（不是力、不是冲量），单位 m/s：Update 用 `vel += gravity(+0xA0) * dt`，dt = 1/60（InsectBase 传 `0x179F358` = 0.016667，0x4A9230，H）。
- 第三参 = 接下来 N 次 Update 不允许重新着地（Update 0x6DB5F6：`+0xB8 != 0 → +0x70 = 0, +0xB8--`，H）。起跳时刚体仍贴地，不给帧数会立刻被接触事件判回着地。
  游戏自己：爆炸击飞 **2**（GiantAnt 0x40ABF1），跳跃 **0**（地面时常量速度，0x409E28 等），Berserker 4 / 8（0x540997 / 0x35D524）。
- 参考每帧 `Launch((target-pos)*2, 2)`：每帧重置离地 + 2 帧抑制，所以抓着时永远不着地（H）；松手后 2 帧即可落地。

#### 3.1.2 结构布局（H，来自 ctor/init/Update）

| 偏移 | 类型 | 含义 |
|---|---|---|
| +0x00 | int | mode：≠0 时 Update 整段跳过速度处理（GiantBee 飞行 0x4202B4 写 1；其它若干昆虫状态也写 1） |
| +0x08 | Shape 句柄（0x11A62C0 构造） | |
| +0x10 | `xgs::havok::Body*` | 控制器刚体（InsectBase = obj+0xE10）。读真实速度：`0x11B1300(body)` |
| +0x20 | vec4 | 刚体位置缓存（Update 开头 0x11B15B0 取） |
| +0x50 / +0x60 | vec4 | 附着法线 / 平滑法线 |
| +0x70 | byte | **着地（附着）**。1 → Update 清零 +0x80、不设线速度 |
| +0x73 | byte | 本步有接触（接触回调 0x6DAC10 置 1，0x6DAE20） |
| +0x80 | vec4 | **弹道速度 m/s**（未着地时每次 Update `SetLinearVelocity(body, +0x80 + +0x120)` 后 `+= g*dt`） |
| +0x90 | vec4 | 本步接触法线累加（回调 0x6DAE19 累加） |
| +0xA0 | vec4 | **重力向量**（ctor 默认 (0,-9.8,0)；`0x6DAF00(ctrl, scale)` = 世界重力 × scale；InsectBase ctor 用 **2.0**，0x4A7AA2→0x4A7AB4） |
| +0xB0 | float | 碰撞球半径 = SGO `insectbase_CollisionSetting[0]`（0x6DA410 创建球形 0x11A66B0） |
| +0xB4 | float | `insectbase_CollisionSetting[1]`（含义 M：地面探测附加距离，Update 用 +0xB0+0xB4 做探测半径） |
| +0xB8 | int | Launch 的着地抑制倒计时 |
| +0x120 | vec4 | 单帧附加速度（`0x6DAE50` 累加，Update 末尾清零） |

#### 3.1.3 同族函数（H）

| RVA | 签名 | 说明 | fp |
|---|---|---|---|
| 0x6DAA10 | `(ctrl, const vec4* v, int frames)` | Launch | 见上 |
| 0x6DAE50 | `(ctrl, const vec4* dv)` | +0x120 += dv（只作用一帧，且仅在未着地时生效） | `48 83 ec 18 0f 10 89 20 01 00 00 0f 58 0a f3 0f` |
| 0x6DAF00 | `(ctrl, float scale /*xmm1*/)` | +0xA0 = 世界重力 × scale | `40 53 48 83 ec 40 48 8b 41 10 48 8b d9 0f 29 74` |
| 0x6DAB90 | `(ctrl, const vec4* pos, const vec4* n)` | 传送到 pos + n·R 并设附着法线 | `48 89 5c 24 08 57 48 83 ec 40 0f 10 0a 48 8b d9` |
| 0x6DAE80 | `(ctrl)` | 停用：刚体移出世界（0x11B1710），清 +0x70/+0x73/+0x90（**死亡时调**，0x4A8E4D） | `40 53 48 83 ec 20 48 8b d9 48 8b 49 10 e8 7e 68` |
| 0x6DB200 | `(ctrl, float dt /*xmm1*/)` | Update | `48 8b c4 48 89 58 10 48 89 70 18 55 57 41 56 48` |

#### 3.1.4 谁用 Cling（ctor 0x6D9FF0 的调用者，H）

InsectBase **+0xE00**（0x4A68FF）、Nephila **+0xDE0**（0x4DBDA4）、WalkingEnemyBase **+0x1160**（0x53AD2B，全部 Berserker）、
Vehicle502_GroundRobo +0x1810（我方载具，别抓）。

注意：
- **GiantBee 飞行**：第 5 槽 0x420180 把 mode(+0x00) 设 1，Update 跳过速度块（0x6DB309 `cmp [rbx],0; jne`，H）→ Launch 在飞行中无效。
  要抓飞行的蜂，只能 `0x11B18F0(*(obj+0xE10), v)` 直接设刚体速度，并与蜂自己的飞行代码抢（L）。
- 地面行走时 Update 不设线速度（+0x70=1 分支跳过 0x11B18F0，H）——地面移动由别处直接摆位置；所以「先清 +0x70」是必要的，单写 +0x80 没用。
- Launch 不改动画状态机：GiantAnt 自己击飞后还会写 +0x1EE0 = 2、+0x1EF0 = 速度（0x40AC25，只在有权威端、非强制、未死时），插件不写 → 空中仍播行走动画（L）。

#### 3.1.5 游戏自己的爆炸击飞（GiantAnt 第 47 槽 0x40AA60，H）

由 InsectBase OnDamaged（第 18 槽 0x4A8DA0）调第 47 槽（GDI+0x58 爆炸半径 > 阈值，或硬直槽 +0x1008 耗尽）。
条件：`GDI+0x58 > 1e-6`（爆炸）且 `obj+0x5C8 == 0`。速度：

```
c   = ragdoll(+0xB10) 包围盒（0x6E9F90）取的顶点（out[0], out[1]+out[5], out[2]）   // M：中心+半高
d   = c - GDI+0x30(爆心);  d.y *= 1.5
d   = normalize(d) * (R/15)        // 0x4D940 = SetLength(vec*, len)，R = GDI+0x58
d.y *= 1.5;  v = d * 0.5 * 60      // => |v| ≈ 2R m/s，偏上
ClingLaunch(obj+0xE00, v, 2)
```
**不看伤害、不看质量**（只看爆炸半径和方向）。`AmmoHitImpulseAdjust`（GDI+0x40 冲量）**不参与昆虫击飞**。
GDI+0x40 的消费者：HumanBase OnDamaged 0x5717D0 里给特效方向（0x2E1CF0，0x571B2D..）和 ragdoll 态的速度（0x57232A..0x572398，见 §5.3）。

---


### 3.2 CharacterControl_Walk（Humanoid +0xB40 / Monster +0x1120 / Human +0x680）


类名 `xgs::havok::CharacterControl_Walk`（H：`MemberSlot<CharacterControl_Walk>` 0x11B8B16 紧挨 ctor 0x11B8B90）。无虚表结构体，
内部是 Havok 角色刚体（xgs 自定义 `hknpCharacterRigidBody`）。

#### 3.2.1 布局（H 读码；语义标注）

| 偏移 | 类型 | 含义 |
|---|---|---|
| +0x00 | ptr | 角色刚体包装（[+0]+0x18 body id、[+0]+0x28 world；0x11AF750 用） |
| +0x08 | `xgs::havok::Body*` | 刚体；`0x11B1300(body)` 读真实线速度（H：Step 0x11B9B2D 用同一指针） |
| +0x20 | vec4 | **期望移动速度**：AI/动画每帧在 Step 前写（Humanoid 0x472858、Monster 0x4BD2A0，紧接着调 Step） |
| +0x30 | vec4 | **外加 / 弹道速度 m/s**（y = +0x34） |
| +0x40 | vec4 | 本帧实际下发速度 = +0x30 + 投影后的 +0x20 + +0x60 + +0x1C0（Step 末尾 0x11AFA70 下发到刚体） |
| +0x60 | vec4 | 单帧附加速度（`0x11B8D90` 累加，Step 末尾清零） |
| +0x80 | float | **竖直重力加速度**（= 世界重力.y × scale，`0x11B9800(walk, scale)`；Humanoid 读 SGO `gravity_factor`，M） |
| +0x84 | float | 离开台阶时的下压速度 6.0（init 0x11B912A） |
| +0x90 | 支撑信息块 | Step 开头 `0x11AF750(walk, walk+0x90)` 刷新 |
| +0x91 | byte | **支撑状态**：0 无支撑（空中）、1 滑坡、2 站稳（与 Havok `hknpCharacterSurfaceInfo::SupportedState` 一致，H 读码 / M 命名） |
| +0xA0 | vec4 | 地面法线 |
| +0xD0 | byte | **外加速度保持（launch）标志** |
| +0x1C0 | vec4 | 另一单帧附加速度（Step 末尾清零） |

#### 3.2.2 Step = 0x11B9890（H；不是 0x11B9880）

`void __fastcall WalkStep(void* walk)` fp `48 8b c4 48 89 58 10 48 89 78 18 4c 89 70 20 55`。
调用者：Humanoid 第 5 槽 0x470D10（0x472862）、MonsterBase 第 5 槽 0x4BD100（0x4BD2A7）、HumanBase 0x5750E0（0x575BCA）、Merman、FlyingDyno、
SquidSmall、GiantDangoLarge、AlienTrailer、Vehicle501。

逻辑（g = +0x80 / 60）：
- 状态 0（空中）/ 1（滑坡）：`vy(+0x34) += g`；+0x30 保留。例外：上帧是 2 且未 launch → 本帧给 −6 m/s 下压（贴坡）。
- 状态 2（站稳）：
  - `+0xD0 == 0`：期望速度投影到地面，**+0x30 清零**（外加速度被丢弃）。
  - `+0xD0 != 0`：跳过投影和清零；若 `dot(+0x30, 法线) > ε` → `vy += g` 且状态置 0（离地）。
- 下发 +0x40；刚体不在世界/非动态 → +0x30 清零。
- **`vy < 0` 时清 `+0xD0`**（0x11B9BC3..C9）。

#### 3.2.3 游戏自己的「给速度」写法（H）

```
walk+0x30 = v;                       // HumanBase 跳跃 0x5669A8..C4：(0, 10, 0)
Walk_Detach(walk);                   // 0x11B9880: +0x91 = 0; +0xD0 = 1; ret
```
`0x11B9880` fp `c6 81 91 00 00 00 00 c6 81 d0 00 00 00 01 c3 cc`（整函数，H）。Humanoid 第 65 槽 0x47A890 同样先 Detach 再写 +0x30。

参考 `ApplyVelocityToWalkingCharacter`（写 +0x30..+0x3C、+0xD0=1）**功能上对**，与游戏写法等价，只是没把 +0x91 置 0（Step 开头会重算，无影响，M）。

#### 3.2.4 会不会被覆盖

- +0x30 的写者只有：Step（站稳且未 launch 时清零；vy<0 只清 +0xD0 不清速度）、游戏状态代码。
  Humanoid 只有第 65 槽写 +0xB70（H）；Monster 只有 ctor（H）；**HumanBase 有十几处**（跳跃、乘车、PaleWing 飞行状态 0x583240/0x583820 等，H 写者 / L 频度）
  → 抓剑兵/飞行兵时可能被其状态机每帧改写。
- **AI 移动是叠加的**：+0x20 每帧被写并加进下发速度（H），抓住时敌人若仍在「走」，会在插件速度上再加一份（L 影响大小）。
- 插件每帧写 +0x30 = 完全接管竖直速度（重力每帧被覆盖）。松手后：vy<0 时 +0xD0 自动清 → 落地后 +0x30 清零，自然恢复。

#### 3.2.5 其它函数（H）

| RVA | 签名 | 说明 | fp |
|---|---|---|---|
| 0x11B8D90 | `(walk, const vec4* dv)` | +0x60 += dv（单帧） | `48 83 ec 18 0f 10 49 60 0f 58 0a f3 0f 10 41 6c` |
| 0x11B9800 | `(walk, float scale)` | +0x80 = 世界重力.y × scale | `40 53 48 83 ec 30 48 8b 41 08 48 8b d9 0f 29 74` |
| 0x11B9470 | `(walk, const vec4* n)` | 若 +0x30 朝 n 的反向，去掉该分量（撞墙消速，M） | `48 83 ec 18 0f 10 22 0f 10 59 30 0f 28 c4 0f 59` |
| 0x11B8B90 | ctor | | `40 53 48 83 ec 20 48 8b d9 e8 d2 69 ff ff 48 c7` |

#### 3.2.6 读当前速度 / 着地

```cpp
auto* w = obj + kWalkOff;                         // 0xB40 / 0x1120 / 0x680
bool grounded = *(uint8_t*)(w + 0x91) == 2;
const float* vExtra   = (float*)(w + 0x30);        // 弹道部分
const float* vApplied = (float*)(w + 0x40);        // 上一帧下发的总速度
const float* vBody = reinterpret_cast<const float*(__fastcall*)(void*)>(image + 0x11B1300)(*(void**)(w + 8)); // 真实刚体速度
```
Cling：`grounded = *(uint8_t*)(ctrl+0x70)`；弹道速度 `ctrl+0x80`；真实速度 `0x11B1300(*(void**)(ctrl+0x10))`。

---


### 3.3 哪些对象带动态刚体，如何从 SceneObject 找到它们


#### 3.3.1 通用方法（任意 SceneObject，H）

遍历 hknp body 数组，`bodies=*(X+0x38)`，`n=*(u32*)(X+0x40)`，每项 0xB0：

1. 跳过 `(flags&3)==0` 的项。
2. 用读槽 29 `0xDAB360(X+0x20, id, 0, 8)` 取 key 0。
3. 值等于该 SceneObject 的就是它的刚体，再用 `0x11AD2E0` 拿 Body*。

O(N)，只在抓取那一刻跑一次就够。

#### 3.3.2 已知的组件位置

**Ragdoll 组件**（ctor `0x6E3B10`，init `0x6EB4C0`）：

- 结构：`entries=*(r+0x78)`，`count=*(u64*)(r+0x88)`，每项 0xC0。
  - entry+0x50 是 `Body*`（shared_ptr 的 ptr 部分）。
  - entry+0x68 是质量。
  - entry+0x9C 是 mode，+0xA0 是上一个 mode，+0xA4 是 enabled。
  - entry+0xB0/+0xB8 是附属对象：其 +0x10 是第二个 Body*（`0x6EA430` 也会比对它）。
- 按 id 查下标：`0x6EA430(r, id)`，按名查：`0x6EA670(r, L"RagDollProxys.xxx")`（H）。
- **mode 的含义**（`0x6ED480`，H）：

  | mode | 处理 |
  |---|---|
  | 0 | RemoveFromWorld |
  | 1 | RemoveFromWorld，再调 `0x6EC230` |
  | 2 / 3 | SetMotionType(1 = keyframed)，加入世界 |
  | 4 | SetMotionType(2 = dynamic)、SetMass(entry+0x68)、开约束 |

  活着的角色 / 载具的布娃娃一般是 keyframed，`0x6E40B0` 激活后全部变成 mode 4。

| 对象 | 读法 | 置信 |
|---|---|---|
| VehicleBase 系（CarBase、Heli、506 …） | ragdoll = `veh+0x1320`（entries `veh+0x1398`，count `veh+0x13A8`）。CarBase 的部件刚体是动态的，`veh+0x1710` 是它们的质量和 | H |
| HelicopterBase 飞行刚体 | `Body* = *(veh+0x1650)` | H |
| EventAnimationObject | ragdoll = `obj+0xAA0`；EAO 构造把这些刚体改到层 0x11 | H |
| UfoCarrier508 / MovingFortress | ragdoll = `+0xB60` / `+0xB00` | H |
| Transporter_Container（集装箱） | `shared_ptr<Body>` 在 `obj+0xBB0`（`0x5E7DFD`），层 6 | M（this 寄存器没追到底） |
| BuildingObject / BoxObject | `shared_ptr<Body>` 在 `obj+0x190`（`0x7C7E58`、`0x7C76D7`），层 15 | M |
| ModelDebris（建筑碎片） | `shared_ptr<Body>` 在 `this+0x370`（ctor `0x2F9920`，`r12=this`，`0x2FA7C6`）；碎片层 27 或 29（`0x2FA637`/`0x2FA63E`）。**ctor 里没有设 key 0**，所以 `0x108260` 大概率返回 0（纯物理碎片）。层 29 在碰撞表里不与任何层碰撞，只是视觉碎片，射线打不到 | H（偏移、层）/ M（无拥有者） |
| MapObject_*（地图可破坏物：车、道具…；`MapObject_CrashModel` 带 `SimulationBody`/`IBody` 子类） | 物理部件组件：`+0x08/+0x10` 是 `shared_ptr<Body>`，由 `0x135D00` 写入。`0x133E10(comp, owner, idx)` 做三件事：`EnableBodyFlags(0x80)`；订阅事件 2；设 key 0 = owner（SceneObject，可 dynamic_cast 成 `MapObject_Base`） | H（流程）/ L（各子类里组件的偏移没展开） |
| 布娃娃尸体 | 角色自身的 Ragdoll 组件（偏移因类而异），mode 4 之后才是动态。最省事的是走 §3.1 或 §4 的 id 路线 | M |

---


## 4. 质量与射线层



- **游戏自己的击飞不用质量**：昆虫只按爆炸半径（§2.5）；HumanBase ragdoll 速度按冲量模长开方（§5.3）。Cling 控制器直接设速度，**昆虫没有有效质量**（H）。
- Walk 胶囊与质量写在 init cinfo 里，结构体本身不留：

| 家族 | 胶囊（端点 A/B、半径） | 质量 | 来源 |
|---|---|---|---|
| HumanoidBase（所有 Humanoid 一样） | (0,4.5,0)–(0,20.5,0)，r 4.5 | 100 | 硬编码 0x464EDD..0x464F04（常量 0x17C37C0/0x17C37F0、0x40900000、0x1765A68），H |
| MonsterBase | (0,20,0)–(0,35,0)，r 20 | 100 | 硬编码 0x4B599A..0x4B59FD，H |
| HumanBase | 见 SGO | SGO **`human_character_control_mass`** → cinfo+0x34 | 0x56ADF5..0x56AECF，H |
| InsectBase | 球 r = SGO `insectbase_CollisionSetting[0]`（运行时 obj+0xE00+0xB0） | 无 | H |
| WalkingEnemyBase | SGO `CollisionSetting`（0x53BA10）→ cling 半径 | 无 | M |
| GiantDangoLarge | SGO `body_radius` / `body_mass`（0x36108F / 0x36406C） | 有 | M |
| Berserker 系 | `BerserkerSettings.BlowAdjust` / `ExplosionBlowAdjust` / `RagdollGravityFactor`（0x33CB53 / 0x33CD5D / 0x33D784） | 游戏自己的击飞倍率，比「质量」更合适 | M（未跟到消费者） |

- 体型估计可用：ragdoll 包围盒 `0x6E9F90(ragdoll, float out[8])`（min/max 或中心/半宽，内部 minps/maxps，M）；锁定点 GOB+0x360（vector<shared_ptr>，data +0x368、count +0x378，H 仓库已用）。
- 建议：重力枪的「重」直接按家族给常数 / 用 cling 半径 R³ 分档；Humanoid 不同体型（青蛙 vs 殖民者）胶囊相同，需看 SGO 缩放（L）。

---




#### 4.1 射线层

碰撞表从 `CollisionFilter` ctor `0x105510` 的 141 对规则模拟得到（H）。层号对应的 filterInfo 生成函数是 `0x1060xx`（例如 `0x106100`=22、`0x1060C0`=15、`0x106160`=6、`0x106030`=11）。

| 层 | 碰撞对象 |
|---|---|
| 22 | {15–20, 26}：只打地图 |
| 11（子弹查对象） | {5–9, 14} |
| **15** | {1, 2, 3, 6, 7, 8, 9, 10, 12, 13, 21, 22, 24, 27, 28}：几乎所有可动物，包括层 6 的布娃娃 / 载具 / 集装箱和层 27 的碎片，但**不含**地图 15–20 |

建议：
- 抓物体用 `filterInfo=0x0F`（层 15）打一条 `CastRay 0x11A7EE0`（M）。
- 地图上的物件如果在 15–20 层，再用层 22 打一条，取更近的那个（L：地图物件到底在哪一层要看具体类）。
- 层 15 会打到玩家自己（层 1–3、7–9 一带）。所以射线起点要放在自身胶囊外，或者用 ParameterHUD 那种带过滤回调的 broadphase（`0x27C8C0`）（M）。

#### 4.2 判定

```cpp
// id = collector.hitBodyId（+0x78）
if ((id & 0xFFFFFF) == 0xFFFFFF) return no;
Body* b = GetBody(W, id);                       // 0x11AD2E0：内部已判 isBodyValid
if (!b) return no;
if (GetMotionType(b) != 2) return no;           // 0x11B14A0：0=static(地图), 1=keyframed(活体布娃娃/动画件)
float m = GetMass(b);                           // 0x11B13D0：动态体必 > 0
if (!(m > 0 && m < kMaxGrabMass)) return no;
SceneObject* o = BodyToObject(id);              // 0x108260：可能为 nullptr（ModelDebris 等纯物理碎片）
// o 非空时按 RTTI 分类（__RTDynamicCast，类型描述符：SceneObject 0x2006450、GameObjectBase 0x2006400、
//   MapObject_Base 0x200F080、InsectBase 0x202A270、HumanBase 0x2016A98 …）
// 排除：自己（玩家 / 自己的载具，对 o 或 Body 比对）；可选排除 VehicleBase 的部件（抓一个部件会拖着整车走）。
```

依据：
- 地图静态体 `flags&1`，返回 0（H）。
- 活着的角色 / 敌人的布娃娃是 keyframed，返回 1，会被自动排除（H，按 §3.2 的 mode 表）。
- 死后的布娃娃、车部件、集装箱、碎片是 dynamic，返回 2（M）。

---


## 5. 尸体


#### 5.1 死亡后是什么（H 读码，M 语义）

- `+0x2E8` 在 ApplyDamage 0x547C30 第一次 hp≤0 时置 1（仓库 `docs/edf41-classes.md` §2c，H），**尸体就是同一个 GameObject**，`+0x2E8 == 1`；
  不是另建 EventAnimationObject（`raycast-re.md` 说的 EAO layer 17 只是 EAO 自己，0x5B6401）。
- InsectBase：OnDamaged 0x4A8DA0 见 +0x2E8 → `0x6DAE80(obj+0xE00)` 移除 cling 刚体；死亡状态（如 0x422C70）`0x4A8570` 激活 ragdoll（→0x6E4160）、
  `0x6ECF20(obj+0xB10, 0x11B1300(*(obj+0xE10)))` 把 cling 刚体当时的速度交给 ragdoll（0x4230B0..D2，H）。
- HumanoidBase：第 60 槽 0x479640：`0x6E40B0(*(obj+0xA78))` 激活 ragdoll，再 `0x6EED10(*(base+0x20B30E0), ragdoll, weak self)` 登记到全局 ragdoll 管理器（H 调用；管理器语义/上限 M）。
- HumanBase：ragdoll obj+0xD90，`+0x1180` = ragdoll 态（M），爆炸倒地也进 ragdoll（不死也会）。
- MonsterBase：ragdoll obj+0xDA0，ctor 设层 8（`0x6E9320(rg, 8)`，0x4B5964，H）。

#### 5.2 ragdoll 结构与函数（H）

- 刚体记录数组 `[rg+0x78]`，个数 `[rg+0x88]`，步长 **0xC0**，记录 +0x50 = `xgs::havok::Body*`（0x6ECF4B..70）。Body+0xF0 = Havok body id，+0x100 = 世界包装。
- `int RagdollBodyById(rg, uint32 id)` 0x6EA430（fp `4c 8b 41 78 45 33 c9 48 8b 81 88 00 00 00 4c 8d`）；按名 0x6EA670（名字要带 `RagDollProxys.` 前缀）。
- `void SetLinearVelocityAll(rg, const vec4*)` **0x6ECF20**：加世界锁 0xDAC5B0 → 每个在世界里的刚体 0x11B18F0 → 解锁 0xDB04B0。
- `void AddLinearVelocityAll(rg, const vec4*)` **0x6E6110**：同上但 v = Get(0x11B1300) + dv。
  这两个函数头 16 字节相同，fp 至少比 32 字节：
  `0x6ECF20: … 41 56 48 83 ec 20 4c 8b f1 48 8b ea 48 8b 89 c0 00` / `0x6E6110: … 41 56 48 83 ec 30 48 8b e9 4c 8b f2 48 8b 89 c0 00`。
- 单刚体点冲量 `0x11B0CB0(Body*, const vec4* impulse, const vec4* point)`（world 包装 vfunc +0xD0；HumanBase 爆炸 0x5722AF 用，H 调用 / M 语义）。

#### 5.3 能不能抓尸体

能（M）：射线命中的 body id → `0x108260(id)` 得到对象（ragdoll 刚体也映射回对象，仓库 `feedback-2026-10-10-gunship-own-rounds.md` 对载具 ragdoll 已确认 H，敌人 M）
→ `+0x2E8 == 1` → 用该家族 ragdoll 偏移 → `0x6EA430` 找刚体 → 设该刚体速度（`0x11B18F0`）或整具（`0x6ECF20`）。
未知（L）：ragdoll 是否会睡眠 / 被管理器冻结或回收、对象何时被删除（插件只持弱引用）、射线过滤层能否打到 ragdoll 层（参考 collector filter=10 是否覆盖层 6/8，L）。

---


## 6. 重力枪：模板武器 / 每帧读取 / 光束 / 武器表

### 6.1 模板候选


| | 表 id | SGO | 名称 | 类 | 兵种 / category | AmmoClass |
|---|---|---|---|---|---|---|
| **推荐** | 1064 | eLockonMarkerL01 | Laser Guide Kit（激光诱导装置） | Weapon_LaserMarker（vt 0x17E45E8） | AirRaider / 305 Weapon_Engineer_Special | ""（无子弹） |
| 备选 A | 14 | aWeapon014 | Blazer | Weapon_BasicShoot（vt 0x17E36E0） | Ranger / 0 Weapon_AssaultRifle | LaserBullet02 |
| 备选 B | 384 | pRapier01 | Rapier | Weapon_BasicShoot | WingDiver / 100 Weapon_Pale_Short | LaserBullet01（耗能，射程约 3 帧） |

推荐 eLockonMarkerL01 的理由（H，除注明外）：
- 根本不产生子弹：slot 16（开火）是 `0x681A50: ret`；共用的 TryFire `0x690BB0` 调用者列表里没有 LaserMarker（0x69B062/0x69C8DA/…/0x6B4169），所以 AmmoCount=1 永远不会被消耗，等于无限弹药。不需要拦截任何东西。
- 按住就是持续激光：slot 5 `0x6A3C30` 先调基类更新 `0x6934F0`，然后 `on = +0x13A && +0xBE8>0 && +0x144==0 && +0xE0C<常数 && !(+0x145C&1)`（0x6A3C45..0x6A3CF1），再调 `0x6A64A0(marker=+0x1570, dt, &枪口, &终点, on, 0)`（0x6A3D0E）。
- 自带光束和命中环：marker 的 init `0x6A28B0` 加载贴图 `ParticleAlpha_4.dds`（0x179FBC0）和 `LockOnRing_alpha.dds`（0x17A5A18）。颜色取 AmmoColor（`lea r9,[rbx+0x8D0]`，0x6A0F0B），粗细取 AmmoSize（+0x8C0 → marker+0x1F4）。SyncRender 是 slot 3 `0x6A3BE0`：基类 0x6934C0 之后再调 marker 的 0x6A6230，阶段由原版保证。
- 射程 = 枪口 + 瞄准方向(+0x170) × AmmoSpeed(+0x894) × AmmoAlive(+0x898)（0x6A3C53..0x6A3C97），原版是 700×1 = 700 m。
- 联机原生同步：slot 30 `0x6A3110` 收消息 9–11 转给 marker；命中点作为消息 10 发出（0x6A4620，见 docs/tvguide-re.md §1.2）。
- 循环音效 FireSe `weapon_Engineer_LM_laserMark`。没有伤害，没有冲击。
- 缺点：只有 Air Raider 能用。Ranger / Fencer 的武器里没有 `hand` 动作组，只有 WingDiver 和 AirRaider 有（anim.py 扫描）。跨兵种移植属于 L，而且 calls.py 现有约定是「每个兵种保留原生模板」（boarding gun 那一组）。
- 副作用（M）：marker 会在锁定登记表里登记一个 type 2 条目（`0x22E210(..., owner, 2)` @0x6A2981，条目存在 marker+0x108），命中时 +0x29=1、+0x10=命中点。这本来就是激光诱导的用途，友军的激光诱导类导弹可能会去追光点。

备选 A（Blazer）：FireInterval 1、999 发、AmmoAlive 10、AmmoSpeed 30（300 m）、ReloadTime 600。它有可见光束，但那是真子弹，见 §2。


### 6.2 拦截原版子弹


- LaserMarker 模板：不需要拦截（见上）。
- Blazer 模板有两种做法：
  - (a) 只改 SGO：AmmoDamage=0、AmmoHitImpulseAdjust=0。子弹仍会命中、仍走伤害 / 受击路径（0 伤害时的硬直、仇恨、命中特效未验证，L）；联机经 `0x694910` 同步，各端看到一致。命中半径 = AmmoSize×AmmoHitSizeAdjust，所以标记只能取接近 1.0 的位模式。不装插件时就是一把无伤 Blazer。
  - (b) hook BasicShoot 的 slot 16（vtable 项 0x17E3760 → `0x69AA10`）。它按 shot%枪口数调用 `0x690CC0`，后者再调 `0x694910`（联机开火消息）和 `0x696FD0`（读 +0x7F8 弹工厂，`0x2307F0` 复制 InitParam 并生成子弹）。按标记跳过以后，本机不生成子弹，也不发联机消息，光束随之消失。这个 vtable 原版有 66+ 把 BasicShoot 共用，必须对其它武器原样放行。扣弹位置没有找到（L）。
  - 结论：Blazer 要么保留子弹（a，有副作用），要么把光束一起去掉（b），两种都不如 LaserMarker。


### 6.3 每帧读取


- 扳机：基类更新 `0x6934F0` 里（H）：`+0x13D = (旧 +0x13A==0 && +0x139)`（刚按下的边沿），`+0x13A = +0x139`（本帧按住），`+0x139 = 0`（0x693584..0x6935B1）。+0x13B/+0x13C 是副扳机的同一套。参考代码把 +0x13A 叫 m_bIsFiring，其实它表示「按住」，不代表有弹，也不代表真的开了火。
- 谁写 +0x139（H）：士兵 `0x590850`，把 `*(*(human+0x1970)+0x140)` 当前武器的 +0x139 置 1；载具持有者 `0x62C000`。
- +0x13E 是拿在手上（H）：`0x68F8E0` 置 1、`0x690230` 置 0，调用点在士兵切枪代码（0x590C39/0x590CB3/0x59F27C/0x5A17A0…）。基类在 +0x13E==0 时走收起后的装填分支（0x693AC3）。
- 「激光开着」：用和原版相同的条件（§1），或者直接读 marker 的结果：weapon+0x1780（= marker+0x210）是本帧命中点，weapon+0x1790（= marker+0x220）=1 表示命中（0x6A67ED/0x6A67F4），条目 +0x29 表示有效。0x6A64A0 遇到远端持有者会直接返回，只在本机算。
- 枪口（H）：数组在 weapon+0x1D0，数量 +0x1E0，步长 0xF0；基类每帧用 `0x6969A0(muzzle, weapon+0x150)` 重建 muzzle+0x50..+0x8F 矩阵（0x693642..0x693679），+0x80 是位置行。仓库已有 `edf::MuzzleFrame / MeanMuzzle`（common/edf/weapon.h）。
- 瞄准方向（H）：原版激光用 weapon+0x170，也就是 +0x150 矩阵的第 2 行（0x6A3C5B）。`CameraRay`（src/hud.cpp:3452）是屏幕中心射线，两者有视差。建议抓取目标沿激光线段取，保证看到的和抓到的一致。marker 只存命中点，不存命中物体；插件要用 `ObjectRay`（src/heli.h:53，返回 body id）沿同一段再打一次射线。
- 时序（M）：FrameTick 在玩家士兵的 pre-update 里跑（src/map.cpp:731 MapHumanFrame），武器 slot 5 在之后的更新里跑，所以读到的是上一帧的值，差 1 帧。


### 6.4 光束视觉


- 推荐直接用 LaserMarker 自带的光束，不用自己画。
- LinkLineEffect 能不能由插件独立持有：
  - 堆上 new 一个是原版做法（H）：UfoMother511CoreSmallCannon 就是 `0x12D85B0(0x2A0)` 加 ctor `0x4FA2E0`（0x518B6F..0x518B86）。
  - 它有自己的 vtable 0x17CA190（RTTI `.?AVLinkLineEffect@@`）：slot 0 = 析构 0x4FA730（edx 是标志，M），slot 1 = 0x4FB780（带 pass 参数的渲染回调，M）。SyncRender 0x4FBA70 在 +0x70≠+0x78 时调 `0x11B3480` 切换渲染登记（0x4FBA76..0x4FBA8D）。
  - 原版的调用位置：SyncRender 都从对象的 SyncRender 槽调（MaserBullet vt 0x17E5DD8 slot 2 = 0x6B25C0；UfoMother511CoreSmallCannon vt 0x17CB0A0 slot 3 = 0x5193F0），Update 从对象的 Update 调（0x6B26F6、0x5194F7）。
  - 插件 tick 在更新阶段（士兵 pre-update），在那里调 SyncRender 会和渲染线程抢数据，不安全（M，风险高）。真要用，就 hook Weapon_LaserMarker 的 slot 3（vt 项 0x17E4600）和 slot 5（0x17E4610），只对带标记的武器驱动它；还要在武器析构（slot 1 0x6A2380）里先 Deactivate 0x4FB140，再走 vtable slot 0 析构。
- 参考插件 Weapon_Test 的两个硬伤：
  - 它注册了一个原版不存在的 SGO 类 "Weapon_Test"，不装插件时游戏建不出这把武器，违反仓库「不装插件就是一把原版武器」的约定。
  - 它从不析构 linkLineEffect，泄漏的同时可能留下悬空的渲染登记。


### 6.5 武器表与 SGO


- 新增 Call（只能追加；同时在 RELEASED 里加一条）。新 brings 类型例如 'gravity'，模板 eLockonMarkerL01；不能复用 'gun'，因为 gun_sgo 会强制改成 SolidBullet01。行 id 用 `EDF6VC_CALL_GRAVITY_GUN`，GrantCalls 会自动给 EDF6VC_CALL_* 拥有位。
- 要改的 SGO 字段：
  - name.ja/en/cn/kr/sc，以及 WEAPONTEXT 的说明行。
  - AmmoHitSizeAdjust 写标记。
  - AmmoColor：光束颜色。
  - AmmoSpeed[0]：射程 = AmmoSpeed×AmmoAlive，例如 150–300 m。
  - AmmoSize：光束粗细。
  - 可选：把插件参数（抓力、质量上限）放进 AmmoDamage 这类带星级曲线的字段，插件读 weapon+0x89C。
- 不要改的字段：AmmoClass 保持 ""，AmmoCount 1，ReloadTime 0，FireSe，use_underground，Ammo_CustomParameter（[3,3] 传给 marker init 写进锁定条目 +0x20/+0x24，含义未明）。
- 标记建议：AmmoHitSizeAdjust 的位模式 = `THROW_MARK_BASE | 0xE01` = 0x3F800E01（约 1.000427）。
  - 现有标记：call 的浮点 7003–7011 / 7101–7121 / 7201–7202 / 7401；throw 的 0x3F800D61–D63；gun 的标签在 AmmoColor.a（0x3F801C85–88）；LockonTargetType 7301–7303。0xE01 和这些都不冲突。
  - 取接近 1.0 的值，是为了即使将来换成真子弹模板，命中半径也几乎不变；LaserMarker 不读这个字段：武器代码里只有 SGO 加载器 0x68D901 写它，它只随 InitParam 块进子弹。
  - 插件识别时同时核对 vtable == 0x17E45E8，双保险。

## 7. 悬浮炮（M099「无名者」：拾取殖民者的枪）

> 2026-10-20 更正：载具残骸的主炮已去掉（用户：「载具炸了以后还是个整体其实」，原版残骸不分离炮塔）。下文关于载具主炮的逆向仍留作记录，代码里已不用。

### 7.0 M099 实际刷了什么（H：从 BVM 字符串池里扫 `app:/`）


`MISSION/EDF5_OLD_SCRIPT/M099/MISSION.BVM`（38688 B）引用了这些对象：
`e506_biggrey_af`、`e506_biggrey_sg`、`e506_biggrey_sg_leader`、`e506_biggrey_ll_leader`、`e506_ha_biggrey_hmg`、`e506_ha_biggrey_ml_leader`、`e516_biggreyBoss`、AiArmySoldier_{RL,RL_Leader,SN,SN_Leader,S_Follower1..3,S_OMG_Leader}、AiHeavyArmor_{sp,SP_leader}、AiPaleWing_elite_{IZN_leader,LR}，地图 `nw_traincity.mac`。

---


### 7.1 运行时怎样知道当前任务是 M099


| 项 | 内容 | 可信度 |
|---|---|---|
| GameStatus | `GS = *(EDF+0x20B2890)` | H |
| 任务开始状态函数 | `0x70F0B0`（状态机 `0x719690` 里 `0x71D952` 用 lea 取它的地址）。依次做：`0x70F100: edx=[GS+0x48]`（当前行号）→ **`0x70F103: [GS+0x15000]=edx`**（本局行号快照）→ `0x70F117: call 0xE1560(GS+0x130, row)` 取字段 1（任务目录）→ 拼上 `L"/mission.bvm"`（`0x17E9B38`）→ `0x70F20B: call 0x1F68D0(*(EDF+0x20B28A8), wstring 按值传入)` 选执行器 | H |
| `GS+0x15000` 的写入者 | 只有 `0x70F103` 一处（读者有 `0x70D884` 结算、`0x83E896`） | H（find_disp 全扫） |
| 字段 1 访问器 | `const wchar_t* 0xE1560(void* modeCfg /*GS+0x130*/, unsigned row)`：先 `cfg+0xF0` 文档，再 `0x52DE0` 两次取子节点，最后 `0x551E0` 取字符串指针（指向任务列表文档内部，任务期间一直有效） | H |
| 字段 1 的值 | 本仓库生成的列表写的是 `f'app:/Mission/{key}'`（`tools/make_edf5_campaign.py:432`），M099 的值是 **`app:/Mission/EDF5_OLD_SCRIPT/M099`**；字段 2 是 `EDF5_OLD_SCRIPT/M099` | H（生成器源码） |

**推荐读法**（插件里最省事、也最可靠）：在 `mission.cpp` 的 `MissionStart()` 里读。它挂在 0x59DE50 / 0x59DC90 的预载上，而预载是任务脚本运行起来以后由 native 调用的，所以一定晚于 `0x70F103`：

```cpp
auto* gs = *(unsigned char**)(image + 0x20B2890);
unsigned row = *(unsigned*)(gs + 0x15000);
auto get = (const wchar_t*(__fastcall*)(void*, unsigned))(image + 0xE1560);
const wchar_t* dir = get(gs + 0x130, row);   // 先拷进插件自己的 std::wstring，再忽略大小写比较后缀 L"EDF5_OLD_SCRIPT/M099"
```

- 要包 `__try`，并且先核对 `0xE1560` 的函数头字节（`40 53 48 83 EC 40 48 81 C1 F0 00 00 00`）。
- 比较时只看后缀，不看行号。这样换任务包、重排行都不受影响。如果还要排除其他模式，再加 `GameContentId()==Cfg().edf5CampaignContent`（edf5campaign.cpp 已有这个函数）。
- 客人也走 `0x70F0B0`：它是场景状态，不分在线离线，客人一侧的行号是房间设定的（M）。
- 备选方案：把 `0x70F20B` 那一次对 `0x1F68D0` 的调用改调到插件，第 2 个参数（wstring）就是完整的 `…/M099/mission.bvm`。这条路线连测试场和自定义任务也能认出来。代价是又多改一处原版代码，所以只在必须时用。

---


### 7.2 e506 殖民者（Primer 步兵）


#### 7.2.1 类与武器容器（H）
- 本体：`Humanoid_Basic`，vtable `0x17C4D70`，继承 `HumanoidBase` `0x17C29C0`；构造 `0x46C530`。无名者 `e516` 是 `Humanoid_BigGreyBoss`（`0x17BEC68`），SGO 里没有 `weapon` 键。
- 武器：在构造里读 SGO 的 `weapon`，`0x46D16F` 用 `CreateObjectSGO 0x11945E0` 创建成**独立的场景对象**。之后：`0x47C510` 挂到 `weapon_node`（`arms_r`）；读 `weapon_constraint_parts`（右腕或右手部位），用 `0x46AA60` 在部位表 `humanoid+0x6C0` 里找到它；再调 `0x4A4BA0(weapon, owner, …, partWeak)`。
- 列表：`humanoid+0x1120` 是 16 字节一行的数组 `{Weapon*, ctrl*}`（弱引用，用的时候按 ctrl+8 先 lock），个数在 `+0x1130`。`enemy_control_humanoid.cpp` 也是这样读的。
- 武器类：`HumanoidWeapon_Basic` vtable `0x17C4920`（构造 `0x4A52D0`）、`HumanoidWeapon_Heavy` `0x17C4B50`（构造 `0x4A5C20`），基类 `HumanoidWeaponBase` `0x17C4428`（构造 `0x49D900`）。开火在第 17 槽（`0x4A5720` / `0x4A6000`）。
- 武器对象的字段（H）：

| 偏移 | 含义 |
|---|---|
| `+0x60` | 世界矩阵（`+0x90` 是位置） |
| `+0x100` | 武器 SGO 根节点（variant，tag 是 `+0x110` 的 u16） |
| `+0x170` | 子弹 factory（由 `bullet_setting[0]` 经 `0x1195C50` 取得） |
| **`+0x180`** | **内嵌 InitParam，0x1E0 字节**（构造在 `0x49D9C0: call 0x1001D0(weapon+0x180)`） |
| `+0x480` | 每发弹丸数（`fire_setting[0]`） |
| `+0x484` | 每次连射的发数（`[2]`） |
| `+0x488` | 发与发之间的帧数（`[4]`） |
| `+0x48C` | 散布（`[6]`） |
| `+0x490` | 一次连射后的冷却帧数（`[8]`） |
| `+0x5B0` | 持有者（裸指针） |
| `+0x5B8` / `+0x5C0` | 约束部位（弱引用） |
| `+0x5E2` | 启用标志 |
| `+0x620` / `+0x630` | 枪口数组 / 个数，每项 0x90 字节，`+0x50` 处是 4×4 世界矩阵 |

  `+0x480..0x490` 各字段的含义是 **M**：按装填的下标和各变体的数值推断。比如 HMG 是「150 发、2 帧一发、300 帧冷却」，导弹是「5 发、15 帧一发」。
- **原版开火的写法**（`0x4A0100`，H）：
  1. `0x32EF10(&local, weapon+0x180)` 拷构造一份 InitParam；
  2. `local+0x90 = [owner+0x318]`（队伍，`0x4A0399`；edf41dragon 写的是 `+0x314`，也能用）；
  3. `local+0x38` = 持有者的 weak（`owner+0x28/+0x30`）；
  4. 导弹的目标 weak 写在 `local+0x60/+0x68`（M）；
  5. 种子写 `+0x1A0`；
  6. 矩阵从 `weapon+0x620[i]+0x50` 取，再加散布；
  7. `0x1194280(*(EDF+0x20B2958), matrix, weapon+0x170, &local)`；
  8. 最后 `0x100C90(&local)` 析构。

#### 7.2.2 SGO 定义（H：sgo.py 读 Root.cpk）

`bullet_setting` 的下标对应关系（H，来自解析函数 `0x4A1D60` 的存储点；InitParam 偏移按玩家 WeaponBase 的命名键 `0x68D5EA..` 对齐）：

| 下标 | 存到 | 含义 |
|---|---|---|
| [0] | factory | 子弹类 |
| [1]×曲线[2] | +0x9C | 伤害；之后还乘难度 `0xD7700(GS,…)`，在线模式再乘 `bullet_online_damage_adjust`（0.833） |
| [3] | +0xCC | 未命名 |
| [4]×[5] | +0xB0 | 爆炸半径 |
| [6]×[9] | +0xC0 | 弹体尺寸 |
| [7] | +0xC4 | HitSizeAdjust |
| [8] | +0xC8 | HitImpulseAdjust |
| [10]×[11] | +0x94 | 速度，m/帧 |
| [12]×[13] | +0xE0 | 重力 |
| [14]+[15] | +0xD0 | 颜色 vec4 |
| [16]×[17] | +0x98 | 寿命，帧，int |
| **[18]** | **+0xE8** | **custom parameter**（variant，指向武器 SGO 里的节点；`0x4A2A63` 调 `0x244CB0`） |
| [19] | weapon+0x4A0 / +0x520 | 命中音 |
| [20] | weapon+0x280 | 开火音 |

曲线 `[a,b]` 的算法（`0x49FCC0`，H）：`f = (level−1)·a + 1`，再取 `min(f, b)`；空曲线时 f = 1。level 就是敌人等级（`0x478160` 重新装填时传进来）。所以**最终数值只能运行时从 `weapon+0x180` 读**，下表只是 SGO 里的基数。

| 变体（M099 实际刷的） | 武器 SGO / 类 | 子弹 | 伤害基数 [1]×[2] | 速度 [10]×[11] | 寿命 [16] | 尺寸 [6] | 颜色 [14] | custom [18] | fire_setting：弹丸 / 连射 / 间隔 / 散布 / 冷却 |
|---|---|---|---|---|---|---|---|---|---|
| biggrey_af | e506_assault / Basic | SolidBullet01 | 1.75×[1.0] | 4.0×[0.1,10] | 240 | 0.8 | [1,.5,.5] | [0,0.5,0.4] | 1/15/4/0.005/150 |
| biggrey_sg(_leader) | e506_shotgun / Basic | SolidBullet01 | 1.667×[1.25] | 4.0×[0.2,4] | 240 | 0.8 | [1,.5,.5] | [0,0.5,0.4] | 20/15/30/0.05/300 |
| biggrey_ll_leader | e506_launcher / Basic | LaserBullet01 | 1×[1.0] | 60.0 | 540 | 3.0 | [.06,.03,.12]×[.05,1] | 激光块 `[[1,1,1,1],[.5,.5,.5,.5],3,3,10,5,0,1.5,1.6,1,10]` | 1/180/0/0.005/300 |
| ha_biggrey_hmg | e506_ha_HeavyMachineGun / **Heavy** | SolidBullet01 | 0.95×[1.0] | 8.0×[0.1,10] | 340 | 0.8 | [1,.5,.5] | [0,0.5,0.4] | 1/150/2/0.025/300 |
| ha_biggrey_ml_leader | e506_ha_missile / **Heavy** | MissileBullet01（`bullet_resource` e506_ha_weapon.mrab/bullet_missile.mdb） | 19.0×[1.0] | 0.5 | 700 | 5.0 | [1,1,1] | 导弹块 `[0,1,60,[0,0,-.05],0.01,0,10,[30,0.9],0,0,[],[0,"weapon_Ranger_ML_go_Medium",…]]` | 1/5/15/0.005/300 |

- 爆炸半径：只有导弹是 8×[0.0125,2]，其余都是 0。
- 本体：耐久 400，`game_object_drop_item [0,7,3.39]`（ha 系是 8.475）。这只是原版道具箱（DropItemManager），**和武器无关**。

#### 7.2.3 武器怎样离开持有者（H）

- 每帧第 4 槽 `0x4A1550` 做 `SetEnabled 0x4A4C80(weapon, part+0xA8 > 0)`，也就是**约束部位（右腕或右手）的 HP**。启用变成关闭时调 `0x4A4FC0`。
- `0x4A4FC0` → 工厂 `0x49CE80(mgr=*(EDF+0x20B2958), &outShared, weapon+0x60, &InitParam)`：`new 0x840` 字节，构造 `0x49D4B0`，再登记进场景（`0x1195A20` / `0x1197050`）。
  - 生成的类是 **`HumanoidWeaponBase::Drop`**（vtable `0x17C4308`，InitParam vtable `0x17C42F8`）。
  - 它用武器 SGO 的 `animation_model` 建模型（`+0x170`，`0x6BB890`），用 `ragdoll` 建物理体（`+0x650`，`0x6EB4C0` / `0x6E40B0`）。
  - 第 5 槽更新 `0x4A1730`：`[+0x830]++`，**超过 600 帧（`0x258`）就调 `0x118A1B0` 删除自己**；每帧依次走布娃娃 `0x6EDCA0`、`0x1100B90(drop+0x210, drop+0x60)` 把矩阵交给模型、`0x6ED800`。
  - 也就是说，EDF5「打掉手里的枪」在 EDF6 里是**纯物理道具，10 秒消失，没有任何拾取逻辑**。
- 联机：房主一侧关闭武器时 `weapon+0xCC0++`，并经 `0x473E10` 发出；客人一侧由 `0x475D20` 反序列化，经 `0x4A1B10` / `0x474F90` 重放。
- **持有者死亡时**：静态上**没有找到**「死亡 → 关闭武器或掉落」的路径。掉落的唯一触发条件是约束部位 HP ≤ 0。按推测（L），枪会跟着尸体走，到尸体清除时再一起清掉，具体时机没有核实。要确认的话，在 `0x4A506B` 改调计数，再在 M099 里打死几个殖民者（头部爆头 / 打手 / 打身体各几次）看次数即可。
- 方案**不依赖**这一点：在击杀的那一帧，武器对象一定还活着（弱引用能 lock 成功）。就在这一帧做快照。

---


### 7.3 「悬浮炮」载体：比较与推荐


| 候选 | 能不能由插件生成 | 能不能自己找目标开火 | 能不能每帧挪位置 | 能不能完整带上殖民者那把枪的子弹 | 结论 |
|---|---|---|---|---|---|
| ZE-GUN（`SentryGunBullet01`，`AWEAPON286..301` 等 18 个武器 SGO；展开后是 `SentryGunBullet_Open01`） | 能，用 `0x1194280`，factory 名 `SentryGunBullet01` | 能（原版逻辑） | **未知**：它本身是子弹，扔出去受重力落地（AmmoGravityFactor 1、AmmoSpeed 0.4），寿命 AmmoAlive 1800 | **不能**：子弹由炮塔自己按 CP[9] 类名和 CP[10..16] 少数几个字段组装，LaserBullet01 / MissileBullet01 的 custom 块带不过去；模型也固定是 ZE-GUN | 不推荐 |
| 射击无人机 `Weapon_Drone_*` | 不能单独生成：它是玩家的 Weapon，靠标记锁定，zip1 的两个补丁也都是改它的状态机 / range 节点 | 依赖玩家武器的状态 | 不能 | 不能 | 不推荐 |
| **插件自己开火（edf41dragon 的做法）+ 可见模型** | 能（H：0x1194280 已在仓库实战） | 由插件选目标（`0x5E11D0` / `0x5E0F20` 遍历队伍、`hit_predict.h` 算提前量、`HoldFire` 过滤） | 能（模型是插件自己摆的） | **能，并且是原样**：直接拷 `weapon+0x180` 的 InitParam 和 `weapon+0x170` 的 factory，等级、难度、在线系数都已经算在里面 | **推荐** |

#### 7.3.1 推荐方案的数据搬运（击杀那一帧，游戏线程）

1. 用 damagestats 的伤害钩子（`0x54A586 → 0x547C30`，见 `damagestats.cpp`）判定「HP 从 >0 变成 ≤0」，并且持有者 vtable 是 `0x17C4D70`（或者看 SGO 路径前缀 `e506_`）。
2. 遍历 `humanoid+0x1120` / `+0x1130`，lock 弱引用，取 vtable 是 `0x17C4920` / `0x17C4B50` 的武器。
3. 快照（全部存进插件自己的内存）：
   - `factory = *(void**)(w+0x170)`。factory 是全局注册表里的对象，长期有效（H：edf41dragon 也是每次按名字取）。
   - `0x32EF10(myParam, w+0x180)` 原生拷构造；用完必须 `0x100C90(myParam)` 析构。
   - **custom（param+0xE8，tag 在 +0xF8）指向武器 SGO 资源内部**。资源在本局应该常驻（任务预载了 e506 的 SGO，M）。稳妥做法与 edf41dragon 相同：把这个节点子树深拷到插件内存，自己写 `+0xE8` 指针和 tag 1。
   - 开火节奏：`w+0x480..0x490`，即弹丸数 / 连射发数 / 间隔 / 散布 / 冷却。
   - 枪口局部矩阵：`muzzleLocal = w+0x620[0]+0x50 × inverse(w+0x60)`。
   - 种类键：武器 SGO 路径，或者 `bullet_setting[0]`。
4. 悬浮炮开火：每一发都从快照再拷构造一次（`0x32EF10`），然后：
   - `+0x90` 写拾取者的队伍（`player+0x314`，与 edf41dragon 相同；原版 HumanoidWeapon 读的是 `owner+0x318`，两个字段的区别没有核实）；
   - `0x6CEBB0(param+0x38, player+0x28)` 写持有者 weak。这样击杀算给玩家，`+0xB7` 的「直击穿过友军」也照原样生效；
   - 导弹额外写目标 weak（`+0x60/+0x68`，M）；
   - 写种子 `+0x1A0`；
   - 调 `0x1194280(mgr, m, factory, param)`，最后析构。
   - 矩阵约定：第 3 行（z）是飞行方向，第 4 行是位置，与 edf41dragon 的 Breathe 相同。
   - 激光（LaserBullet01）：仓库里有 `RemoveLaserBullet02SkipCheck` 一类问题的前例，`docs` 也记过 LaserBullet02 必须带 custom，否则构造函数会抛异常。**这里 01 的 custom 必须带上**（H：SGO 里有）。
5. 弹药 / 寿命：插件自己定，例如「一次拾取 = N 轮连射」或「存在 T 秒」。原版没有对应的东西。

#### 7.3.2 可见模型

- **首选：直接用游戏自己的 `HumanoidWeaponBase::Drop`**。殖民者那把枪的模型和物理都现成，数据就是武器 SGO 本身，不需要另做 SGO 或预载（M099 的殖民者已经把这些武器 SGO / mrab 载入了）。
  - 生成：改调 `0x4A506B`（`call 0x49CE80`），拿到原版掉落返回的 shared_ptr（打手掉枪时）；或者在击杀那一帧由插件自己调 `0x49CE80`。
  - 插件自己调时，InitParam 照 `0x4A4FC0` 的写法：`+0` 写 vtable `0x17C42F8`；`+8..+0x28` 清零（`+8` 的矩阵指针在函数里由 r8 填）；`+0x30` 用 `(*(fn*)(image+0x179E1C0+tag*8))(param+0x30, &srcPtr)` 拷入 `weapon+0x100` 的 variant；用完 `(*(fn*)(image+0x1765220+tag*8))(param+0x30, &tmp)` 析构。全部 H。
  - **地上的拾取物**：保持原版物理，让它自然落地。插件每帧把 `drop+0x830` 写回 0，它就不会在 600 帧时自删。
  - **转成悬浮炮**：包一层 Drop 的第 5 槽（vtable `0x17C4308+0x28`，原函数 `0x4A1730`，先核对签名）。插件名下的 Drop 不走原函数，改为：`+0x830=0`、写 `+0x60..+0x9F`（绕玩家的轨道）、调 `0x1100B90(drop+0x210, drop+0x60)`；跳过布娃娃步进 `0x6EDCA0` / `0x6ED800`。
  - 风险（M / 待测）：
    - 布娃娃刚体会留在落地点。体积是一把枪，影响小；
    - 原版在线时 Drop 经 `0x474F90` 登记在持有者网络状态里，插件自造的不登记，只存在于本机；
    - 删除用 `0x118A1B0`，必须在任务结束（MissionStart）前清掉插件持有的指针。
- 备选：`EventAnimationObject`（构造 `0x5B5FB0`，0xC90 字节，第 5 槽 `0x5B6D20` 把 `+0x60` 交给模型），配一个插件自带的 SGO，模型用 `e506_weapon.mrab/e506_assault.mdb`。
  - **必须带 CAS**：`0x5B6111` 的 `0x1166FB0` 没有 CAS 时返回 −1，随后 `0x116C2D0 → 0x11664F0(players, −1)` 会按 −1×0x160 去索引，等于访问野指针（H）。
  - e506 的 assault 和 shotgun **没有 CAS**；launcher、HMG、导弹有（`e506_launcher.cas`、`e506_ha_gatling.cas`、`e506_ha_missile_launcher.cas`，状态里是否有 `default` 没有查）。
  - 所以备选方案要另做一个只含 `default` 状态的 CAS（仓库里有 cas_* 工具），工作量比 Drop 方案大。
- 各武器模型：

| 武器 | mrab / mdb | CAS | 布娃娃 |
|---|---|---|---|
| assault | `e506_weapon.mrab / e506_assault.mdb` | 无 | `Ragdoll_e506_assault.shkt` |
| shotgun | `e506_handgun.mdb` | 无 | 有 |
| launcher | `e506_launcher.mdb` | 有 | 有 |
| HMG | `e506_ha_weapon.mrab / e506_gatling.mdb` | 有 | 有 |
| 导弹 | `e506_missile_launcher.mdb` | 有 | 有 |

---


### 7.4 载具被毁 → 掉炮塔


- 判定（H，`common/edf/layout.h`、`src/layout.h`）：`veh[0x2E8] != 0`（kDead）或 HP `veh+0x2F8 <= 0`（最大值在 `+0x2F4`）。原版的死亡路线是消息 `0x1000000F` → `0x62ECB0` → `0x6329B0`：踢出所有座位、HP 置 0、置死亡字节（`body506.h`）。M099 里的载具只有插件 / 支援呼叫来的，插件本来就跟踪着它们，每帧检查一次死亡字节由 0 变 1 即可，不需要新钩子。
- 主武器（H）：
  - 挂载点 `veh+0x638`，每个 0x48 字节，个数在 `+0x648`；挂载点 `+8` 是 ctrl，`+0x10` 是 Weapon*。
  - Weapon 是玩家系的 `WeaponBase`（构造 `0x68A920`）：factory 在 **`+0x7F8`**，**InitParam 在 `+0x800`**（0x1E0 字节；AmmoSpeed `+0x894`、AmmoAlive `+0x898`、AmmoDamage `+0x89C`、Explosion `+0x8B0`、Size `+0x8C0`、Color `+0x8D0`、Gravity `+0x8E0`、custom `+0x8E8`）。
  - 次级参数块在 `+0x9E0`（开火时 `0x6970DC` 按条件换用，M）。
  - 开火节奏：FireType `+0x340`、SpreadType `+0x360`、SpreadWidth `+0x364`、FireCount `+0x368`、FireInterval `+0x36C`、BurstCount `+0x370`、BurstInterval `+0x374`、LockonType `+0x6B0`。
  - 原版开火：`0x696FD0` 原地改写 `weapon+0x800`（`+0x90` = `weapon+0x214` 队伍，`+0x60/+0x68` 锁定目标），再 `0x1194280(mgr, m, [weapon+0x7F8], weapon+0x800)`（`0x69799F`）。
- **能搬**：与 §3.1 相同，在死亡那一帧对 `weapon+0x800` 做 `0x32EF10` 快照，factory 取 `+0x7F8`。悬浮炮那一侧的代码不用改。
- 限制：
  - 不是普通子弹的武器不能这样搬：引导激光 / 支援标记、IndirectFire、需要车体动画的武器；
  - LockonType=1 的导弹需要插件写目标；
  - 加特林转速一类状态在 Weapon 子类里，不在 InitParam 里，搬不过去，只能退化成固定射速；
  - 残骸可见模型：载具武器没有单独的 Drop 类。可以用 EAO 配插件 SGO（同样要 CAS），或者先用殖民者那类 Drop 外观 / 道具箱模型凑合（L）。

---


### 7.5 掉落物显示、拾取与联机


- **不用 DropItemManager**（`docs/itembox-re.md`）：它的类型固定是 4 种（模型表 `0x1F57358`），武器箱会计入结算抽奖（`mgr+0xE04`），联机消息 0/1/2 也固定。往里塞自定义类型会污染结算和同步。e506 本来就会掉原版箱子，两者互不影响。
- 显示：殖民者武器用 §3.2 的 Drop（原样落地，插件每帧把 `+0x830` 清零续命）；载具炮塔见 §4 的限制。可以在 Drop 位置（`drop+0x90`）上加 HUD 提示，仓库的 hud / qmark 已有世界坐标转屏幕的路径。
- 拾取判定（建议）：
  - 只对本机玩家（`soldier+0x340` 手柄对象非空，与原版拾取门 `0x59B67C` 相同）；
  - 用水平距离加高度差判断：`|xz| ≤ 3.5 m` 且 `|dy| ≤ 3 m`。原版道具箱的半径是 `max(3.0×装备参数, h+0x1B48)`，缺省 3 m，坐载具时用 `seat+0x258`；
  - 再按住或点一下某个键确认，避免在战斗里误捡；
  - 也可以直接沿用原版半径 3 m 自动拾取。
  - 距离一律按游戏单位算，不要凭画面估（仓库记忆里有画面距离偏短的教训）。
- 悬浮炮的上限：每名玩家最多 K 门（建议 2–4 门），超出时替换最旧的。每门的寿命 / 弹量由插件定。任务结束（MissionStart）时全部清掉，与仓库其他模块的 Reset 一样。
- **联机**（`online_authority.h`：EDF6 复制的是输入和事件，不复制生成）：
  1. 插件造的 Drop、悬浮炮、子弹都**不同步**，每台机器只有自己的副本；
  2. 伤害必须只算一次。悬浮炮的伤害只由**拾取者所在的机器**发出（与无人机按投掷者算的规则相同，`NoteLocalCopy` / `SetSpawnOwner`）。其他机器如果要看到炮，只画装饰副本：0 伤害，或者干脆不开火；
  3. 掉落物要一致：由房主决定「掉了什么、编号多少」并广播；拾取走「客人请求 → 房主认领 → 广播确认」，照抄 DropItemManager 消息 1/2 的思路，用插件已有的网络通道（`support_net`、`EDF6Coop` 的扩展 API）；
  4. **建议第一期只做本机 / 离线**：`InSession()` 为真时整个功能关闭，或者只在房主一侧、只给房主玩家用。第二期再做同步。原版打手掉枪的 Drop 本身是同步的（`0x473E10` / `0x475D20`），但插件续命它、改它的更新函数都只在本机生效，客人那边仍然会在 10 秒后消失。

---


### 7.6 关键 RVA 一览（全部为 H，另有标注的除外）


| RVA | 是什么 |
|---|---|
| `0x20B2890` / `+0x15000` / `+0x130` | GS / 本局任务行号 / 模式 cfg |
| `0xE1560` | 字段 1（任务目录）访问器 |
| `0x70F0B0` / `0x70F103` / `0x70F20B` | 任务开始 / 写行号快照 / 调选择器 `0x1F68D0` |
| `0x17C4D70` / `0x17C29C0` | Humanoid_Basic / HumanoidBase |
| `0x17C4920` / `0x17C4B50` / `0x17C4428` | HumanoidWeapon_Basic / _Heavy / Base |
| `0x4A1D60` | 解析 bullet_setting / fire_setting（装填 `weapon+0x170 / +0x180 / +0x480..`） |
| `0x4A0100` | 人形武器开火（`0x32EF10` 拷构造 → `0x1194280`） |
| `0x4A1550` / `0x4A4C80` / `0x4A4FC0` | 第 4 槽查部位 HP / SetEnabled / 生成 Drop |
| `0x49CE80` / `0x49D4B0` / `0x4A1730` | Drop 工厂 / 构造 / 更新（600 帧自删） |
| `0x17C4308` / `0x17C42F8` | Drop vtable / Drop::InitParam vtable |
| `0x1001D0` / `0x32EF10` / `0x100C90` | InitParam 构造 / 拷构造 / 析构 |
| `0x1194280` / `0x1195C50` / `0x20B2958` | 发射子弹 / 按名取 factory / 管理器 |
| `0x49FCC0` | 等级曲线 `min((L−1)·a+1, b)` |
| `0x68A920` / `0x696FD0` | 玩家（载具）WeaponBase 构造 / 开火（`+0x7F8` factory，`+0x800` InitParam） |
| `0x5B5FB0` / `0x1166FB0` / `0x11664F0` | EAO 构造 / CAS 取 player（没有 CAS 时返回 −1）/ 拿 −1 去索引（越界） |
| `0x118A1B0` | 删除对象 |


### 7.7 待实测清单


1. 殖民者死亡时到底掉不掉枪（`0x4A506B` 计数）；武器对象在尸体清除前是否一直存在。
2. Drop 第 5 槽包装后，模型能否随 `+0x60` 移动；布娃娃刚体残留会不会碰撞。
3. 快照出的 SolidBullet01 / LaserBullet01 / MissileBullet01 能否正常发射（LaserBullet01 的 custom 必须带；MissileBullet01 不写目标 weak 时会怎样）。
4. `param+0x90` 用 `player+0x314` 还是 `+0x318`（击杀归属、友伤）。
5. 武器 SGO 资源是否本局常驻；即使常驻，也建议深拷 custom 节点。

## 8. 撞击判定

### 8.1 角色侧（被抓敌人的「着地伤害」）


- **没有原生摔落 / 撞击伤害（M）**：没有 fall_damage 类 SGO 键；Cling Update / Walk Step 均不调伤害；`ragdoll_contact`（0x6EBBC0 组件）只放音效 + 震屏
  （仓库 `docs/edf41-uforobo.md` 已实测回调在跑）。
- 原生「接触伤害组件」0x6F15B0 / 0x6F1640（Monster 第 5 槽 0x4BD3AA、Merman、GiantDangoLarge 等调用，最终 0x541FF0 直伤）是**本体去伤害它碰到的东西**，
  攻击者 = 敌人自己，不能拿来记玩家的击杀（M）。
- 插件自判（推荐）：
  - Cling：Update 着地那一步会清 +0x80，所以插件每帧缓存上一帧 `+0x80`，见到 `+0x70` 0→1（或 +0x73 置位且速度骤降）即撞击；撞墙时接触法线在 +0x90。
  - Walk：`+0x91` 变为 2 时取上一帧 `vy(+0x34)`；撞墙用「上一帧下发 +0x40 与真实 `0x11B1300(walk+8)` 之差」。
  - ragdoll：取刚体速度差。
  - 伤害投送沿用仓库已有链路：`ram::Damage`（½mv² 公式）+ `jet_bay.cpp ImpactDamage`（DemoIndirectFire 装药，归属 = 玩家，击杀记玩家，`docs/vehicle-ram-re.md` §1），
    或 GDI 消息 0x10000000 → ApplyDamage 0x547C30（攻击者弱引用填玩家，`docs/edf41-classes.md` §2b）。

---


### 8.2 刚体侧（撞击判定）


#### 8.2.1 hknp 事件回调：存在，游戏自己也在用（H），但生命周期复杂

- **取信号**：`hkSignal* 0xDAB920(hknpWorld* X, int eventType, u32 bodyId)`。
  按 body 走 `X+0x980` 事件分发器的 `0xE76180`；id 无效时走全局 `0xE76120`。
  签名 `48 8B 89 80 09 00 00 41 8B C0 25 FF FF FF 00 3D`，唯一。
- **订阅**有两种：
  - `0x12BEA0(hkSignal*, void* obj, const MemFn{void* fn; i64 adj}*)`：不返回句柄，new 一个 0x28 字节节点（vtable `0x176AD98`），头插进链表。
  - `0x64E2B0(hkSignal*, Handle* out, void* obj, const MemFn*)`，即 `0x97B9A0(…, 0x10)`：返回句柄。
  退订函数**没有分析**（L）。
- **回调签名**（M）：`void __fastcall cb(Obj* self, hknpWorld** w /* *w = X */, const Event* ev)`。
  - 地图物件的回调 `0x135170`：`ev+0x08` = bodyIdA，`ev+0x0C` = bodyIdB（H）。
  - 直升机的回调 `0x652B30`：`ev+0x10` 是接触 / 流形对象，用 `0xDD1BD0(ev, X, out)` 取接触信息，output 布局 L；直升机用 out+0x14>0 判断是否压在地上。
- **事件类型 2 = 接触 / 流形事件**（M）：
  - 直升机：`0x654A00` 对 `veh+0x1650` 订阅类型 2。
  - 地图物件：`0x133E10` 先 `EnableBodyFlags(0x80)` 再订阅类型 2。0x80 推测是 RAISE_xxx_EVENTS 位（L）。
  - 类型 1 也被大量使用（BoxObject、集装箱、`0x6F0A7D` 等），含义没追（L）。
- **风险**：
  - 回调在物理步内触发，可能在 hknp 的工作线程里（L）。
  - 必须在刚体销毁 / 插件卸载前退订，而退订入口不明。
  - 被扔的东西可能在飞行中被游戏删除（碎片寿命到期）。

  **第一版不建议挂回调。**

#### 8.2.2 推荐替代：Δv 判撞加区域伤害

1. 在物理步之后的钩子里（仓库现有的 PhysicsHook / 第 55 槽后置）每帧做：
   - `v = *GetLinearVelocity(b)`（取 xyz，忽略 w）；
   - `Δv = |v_prev − v|`。
2. 释放后，`Δv > 阈值`（建议 ≥ 5 m/s，且不能用重力解释：`Δv > 1.5·|g|·dt`）即判为撞击。
3. 撞击点用 `GetPosition` 或 `GetCenterOfMassWorld`。要更准的话，可以从上一帧位置沿 v_prev 打一条层 15/22 射线，取命中点和命中 body（这样还能知道撞到了谁）。
4. 撞击能量 `E = ½·m·(|v_prev|² − |v|²)`，m 取 `GetMass`；或直接用 `m·Δv` 当冲量。
5. 伤害投送，二选一：
   - **(a) 复用仓库现成的撞击路径（推荐）**：`ram::Damage` 算伤害（½mv² ÷ 2.87e5 J 那一套），`jet_bay.cpp ImpactDamage` 用 DemoIndirectFire 装药投送。归属、队伍、击杀记分、爆炸特效都已经实测可用。`docs/vehicle-ram-re.md` §1、`docs/jet-model-re.md` §9.1。
   - **(b) 直调 `0x542490`**：
     `Flag<u8>* __fastcall GameDamageInfo::ApplyAreaDamage(const GDI* gdi, Flag* out /*≥1 字节*/, std::unordered_set<SceneObject*>* hitSet /*可 nullptr*/, float radius /*xmm3*/, u32 filterInfo /*[rsp+0x20]*/, SceneObject* ignore /*[rsp+0x28]，可 nullptr*/)`。
     - 签名 `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C`（16B 4 处，24B 唯一）。
     - 实现：以 `gdi+0x30` 为中心、半径 radius，用 `0x11A70B0` 做 AABB 查询。`gdi+0x24 != -1` 且 `!(gdi+0x60 & 0x20)` 时挂敌我过滤。之后由 `0x542860` 逐目标调 `0x541FF0` 扣血（H）。
     - 游戏调用时 filterInfo 用 `0x106070`（层 13）或 `0x106040`（层 12），调完紧接着调 `0x11AC740(g+0x10)`（写槽 15，`0xDA72A0`，看起来是提交待加入的刚体，M）。插件也应照做。
     - 层 13 碰撞 {5–9, 14, 15, 16, 19, 20}，ParameterHUD 用的 0xB（层 11）碰撞 {5–9, 14}。
     - 姊妹函数 `0x542670(gdi, out, set, ignore)`：半径取 `gdi+0x58`，filter 固定为层 13。
     - 代价：GDI 要自己按 0x90 字节的布局伪造。+0x10 是带引用计数的 weak_ptr，`+0x90` 的 set 必须是真 MSVC 容器；没有特效，不走网络同步。`docs/decoy-blast-re.md` §1.3 已判为不推荐（H / M）。
6. 「撞击点附近有谁」只想**查询**、不想扣血时，不要用 0x542490（它一定会扣血）。改用 broadphase：`0x11A6B00` ctor，`0x11A70B0(collector, W, from, to, halfExt)`，`0x11A7480` 处理命中，`0x1013D0` 清理（ParameterHUD 的写法）。或者在 §5.2 第 3 步的射线命中 id 上调 `0x108260`。

---


## 9. 插件的取舍（2026-10-20 实现）

- 抓取：重力枪沿激光线段用层 15（`filterInfo=0x0F`）打射线，跳过自己和自己乘的载具，先按命中刚体的所属对象分派（§1 类族、§3 控制器、§5 尸体），
  没有可抓的就找射线附近 2 米（或距离的 4%）内的敌人锁定点（准星辅助：活角色的刚体不一定在层 15 上）。射线不穿过地图（先打一条地图射线截断）。
- 移动：一律设速度（`gravity_logic.h` HoldVelocity：τ = 0.12 s 的速度弹簧，限速随质量：2 吨以内 60 m/s，再重按 √ 降，60 吨以上不动）；
  虫类 Launch 每帧给 2 帧离地，行走类写 +0x30 再 Detach，尸体 / 车辆整具布娃娃设速度，其它动态刚体设刚体速度。没有用硬关键帧移动 `0x11B0A40`。
- 质量：刚体 / 布娃娃的物理质量（车辆用 `veh+0x1710`，读不到时各部件之和）；角色按 §4 的估计（`gravity_logic.h` FamilyKg）。
- 撞击：每帧 Δv 判撞（`gravity_logic.h` Flight：扣除 2·g·dt 后 ≥ 6 m/s；角色落地时取落下的速度），加上飞行中经过敌人锁定点的接触；
  伤害 = ½ m v² ÷ 287 kJ × `GravityDamage` × 调用方倍率（重力枪：武器 AmmoDamage），经撞击装药投送（`jet_bay.cpp ImpactDamage`）。没挂 hknp 接触回调（§8.2 的理由）。
- 重力枪的武器行：`tools/calls.py` `EDF6VC_CALL_GRAVITY_GUN`（brings `gravity`，模板 `eLockonMarkerL01`），标记 AmmoHitSizeAdjust 位 `0x3F800E01`；
  `tools/call_weapons.py gravity_sgo`：射程 AmmoSpeed 150、光束颜色、粗细 1.5、AmmoDamage 1（插件读作伤害倍率）。
- 悬浮炮：任务识别用 `GS+0x15000` 行号 + `0xE1560`（§7.1），击杀来自伤害函数的钩子（`damagestats.cpp`，下一帧再取枪），掉落物用 Drop 工厂 `0x49CE80`
  （§7.3.2 的做法），悬浮时由 Drop 第 5 槽的钩子写矩阵，开火照 `0x4A0100`：新构造的参数块 + 保存下来的数值区（+0x90..+0xE8）+ 玩家的队伍 / 持有者 / 目标 +
  深拷的 custom 节点（tag 1 才取，其它 tag 不取这把枪）+ 枪口矩阵指针（+0x70，每个槽一块永不释放的对齐内存）。仅离线。

## 10. 实机结果（2026-10-20，autopilot 后台驾驶，`tests/autopilot/drive.py run`，本分支 DLL `--place`，测后游戏目录逐字节还原）

| 跑 | 任务 | 验证到的 | 日志要点 |
|---|---|---|---|
| 1 | RM015，重力枪在 0 号槽，虚拟手柄 RT 按住 / 松开 | 真扳机下 `Pick`（层 15 射线）抓到身边的友军士兵（行走类、人类 0.1 t），松开以 80 m/s 甩出，落地判撞 82 m/s | `GRAVITYGUN grab ... (kind 3, family 4): 0.10 t`、`GRAVITY throw ... 80.0 m/s`、`GRAVITY impact ... 82.4 m/s` |
| 2 | M099（`edf5:109`） | 任务识别读出 `app:/Mission/EDF5_OLD_SCRIPT/M099` → 悬浮炮开启（`FloatGuns=1`）；RM015 读出 `.../EDF6/RM015` → 关闭 | `FLOATGUN mission ...: EDF5's last mission, float guns on` |
| 3–6 | EDF5 M201（`edf5:71`，殖民者多），`GravityAutoTest=1`、`FloatGuns=2` | 殖民者（行走类 1.5 t）被拉到玩家前方目标点、砸下 46 m/s 结算 111–131 点；被砸死的殖民者掉枪（原版 Drop 模型）→ 拾取 → 画在玩家身边空中 → 自动开火命中（画面伤害数字 3.92 = 快照下来的突击枪单发伤害）；霰弹枪 40 弹丸的那门也飘起 | `FLOATGUN colonist gun dropped ... picked up: 2 floating`、`drawn floating at ...`、`shot 1 at ..., 85 m off` |
| 11 | EDF5 M005（`edf5:4`，巨蚁） | 虫类（贴地控制器 Launch）：从 150–280 m 外拉回、举住、31.8 m/s 砸下、撞击 41 m/s 结算约 290 点，巨蚁（HP 32）当场死亡 | `GRAVITYTEST grab ... (kind 2, family 0): 5.03 t`、`GRAVITY impact ... 294 damage` |

实测中发现并已修正的（都已在代码里）：

1. **Drop 工厂 `0x49CE80` 交回的是弱引用，不是 shared_ptr**（§7.3.2 原写「返回的 shared_ptr」是错的）。它把对象交给场景（`0x49CEF9 call 0x1195A20`），再锁出一个临时强引用，返回前 `0x49CFB2 lock inc [ctrl+0xC]`（弱 +1）、`0x49CFB8 lock xadd [ctrl+8],-1`（强 −1）。第一版按强引用减了一次，扣掉了场景那份：Drop 被提前释放，随后 EDF 工作线程 `RtlFreeHeap` 报堆损坏（`c0000374`，跑 4 崩溃，WER dump 栈在 `ucrtbase!free_base` ← EDF）。改为只释放这份弱引用（`+0xC` 减 1）。
2. **殖民者子弹的 custom 参数句柄 tag 是 2**（不是 node 指针 tag 1）。第一版只认 tag 1，于是没取到枪（跑 3 `custom parameter is not a node (tag 2)`）。改为：tag 1 仍深拷节点；其它 tag 用游戏自己的句柄拷贝表（`0x179E1C0[tag]`，`0x32EF10` 拷参数块时同一张）复制一份留在槽里，开火时再拷进参数块（参数块析构时由 `0x1765220[tag]` 结束那一份）。
3. **撞击装药放在被扔物体的根点（脚下）、半径取物体尺寸时，大半打不到殖民者自己**：殖民者 HP 576，每次结算 111 点，实际只掉十几到几十点（跑 4）。改为放在它自己的锁定点（身体中部），半径至少 4 m；之后每次撞击实打约 110 点（跑 5：576 → 465 → 355 …）。
4. `fire_setting` 读数（`+0x480..+0x490`）是基数 × 等级曲线 `0x49FCC0` 的结果（`0x4A32D8..0x4A32F2`），所以「间隔 15 帧、冷却 300 帧、霰弹 32~40 弹丸」就是游戏给那个等级的殖民者的节奏；弹丸上限放宽到 64。

仍未实测 / 已知局限：

- 载具被毁留下主炮：已按用户意见去掉（原版残骸是整体）。
- 激光（`LaserBullet01`）与导弹（`MissileBullet01`）殖民者的枪：M201 实测到的是突击枪和霰弹枪；激光（`+0x70` 枪口指针）和导弹（锁定点目标）的悬浮炮开火未实测。
- 抓着的虫在被拉回途中被友军打死时，抓取中断（尸体布娃娃没有接上，`Refresh` 返回 false），跑 11 有几次。待查：死亡那一帧虫的布娃娃刚体可能还不是动态的。
- 蜂飞行中、士兵类状态机改写速度、尸体布娃娃回收：未实测。
- 悬浮炮的枪模型在画面里很小（一把枪在玩家头顶 3 m 处），截图里不易辨认；绘制本身由日志 `drawn floating` 和命中数字确认。

## 11. 联机（2026-10-20，静态逆向 + 设计；用户：「只在离线时生效，联机时插件改的速度不会同步。不可接受」）

### 11.1 游戏自己同步什么（静态，未两机实测）

| 类 | 网络接口写 / 读 / 收消息 | 发什么、多久一次 | 远端怎么用 | 置信 |
|---|---|---|---|---|
| GiantAnt（InsectBase） | vtable `0x17BB990`：写槽 5 `0x40C510`，读槽 6 `0x408400`，收槽 15 `0x40BB10` | `net::WalkCharacterSyncControl`（obj+0x1F60，写 `0x77D500` / 读 `0x77C2C0`）：**只在着地且活着时**发脚下位置 + 四元数，40 / 90 帧一次（受击后 3–12 帧）；**不发速度**。空中（obj+0xE70==0，`0x409797`）只发标志 | 5 格快照环 → `0x77C8E0` 一帧修正速度，误差 ≥ 40 m（`0x1765A50`）或超时硬拉 | H 结构 / M 数值 |
| WalkingEnemy（狂战士） | `0x17CCDE0`：写 `0x53EF80`，读 `0x53CAD0`，收 `0x53EEB0` | 同上（obj+0x1510），约 40 帧 | 同上 | H / M |
| Humanoid_Basic（殖民者） | `0x17C5028`：写槽 7 `0x477340`，读槽 8 `0x46A1B0` | 位置（obj+0x90，`0x77AD00`）每 16 帧，**不看是否着地** | obj+0x12D0 组件：加修正速度，误差 > 40 m 或 300 帧瞬移（`0x11AFA60`） | H |
| MonsterBase | 写槽 7 `0x4BE400`，读槽 8 `0x4B7CE0` | 位置每 2 帧（基类掩码 `0x4B8390`；Monster501/504 子类没读） | 同上 | H / L |
| SoldierBase（NPC、玩家） | 写槽 7 `0x59FA80`，读槽 8 `0x592130` | 位置 + 朝向；布娃娃时（位 0x80）位置 + 布娃娃平均速度（`0x6EB300`） | 布娃娃：远端 `0x596539` 调 SetLinVelAll | H |
| 尸体 | 各写函数、读函数对死亡对象提前返回（`0x40C544` / `0x477372` / `0x4BE426` / `0x53EFB2`；读 `0x408406` / `0x46A1C6`） | 不发 | 布娃娃各机各算；对象保留 +0x128 与网络 ID（没看到注销，M） | H / M |
| 地图物件、集装箱、碎片 | MapObject_Base 等与 Transporter_Container（`0x17D7E28`）保留基类空槽 5–8；ModelDebris 没有网络接口 | 不发 | 各机各算 | H |
| 载具 | `docs/online-re.md` §3–4 | 座位 0 乘员的机器发；空车只有「最后驾驶者」在场时有人发 | 软拉 | H |

原版击飞：GiantAnt 槽 47 `0x40AA60` 在**结算伤害的那台机器**上排队击飞（obj+0x1EE0 = 2，速度 obj+0x1EF0），槽 56 `0x409860` 用消息 9 把**绝对速度**发给全房间（`0x409ADC`..），每台机器（含主人）收到后 `ClingLaunch(obj+0xE00, v, 2)`（`0x40BC43`..）。狂战士同理（消息 7）。即「结算处广播速度，各副本重放」，不是只在主人机器上做。

### 11.2 由此的做法

- **重力操作**（`src/gravity_net.cpp`）：抓的人那台机器照常做本地抓持 / 甩出（`gravity.h`），同时每 50 ms 把抓取状态（抓取编号、抓的人与目标的原生 ID、抓取射线与命中点、拉向的点及其速度）发给每个对端，甩出 / 松手各发一次。各对端在自己的副本上找到同一目标（ID；没有 ID 或按 ID 取不成目标的，沿同一射线重新 Pick，命中点离原抓取点 4 m 内才算），每帧 `HoldAt`，甩出时 `SetVelocity` 同一速度。伤害只在抓的人机器上由它自己的 `Track` 结算（`ImpactDamage(by=该玩家)`，`online::ShotCounts` 在其机器上为真）。对有位置同步的类（殖民者、怪物、士兵、载具），主人机器也执行同一抓持，于是主人发出的位置与各机一致，不互相拉扯；虫在空中主人不发位置，各机各自执行同一抓持。
- **悬浮炮**（`src/floatgun.cpp`）：房主决定掉落与归属（`drop` / `pick` / `owner` / `gone`），各机从自己那份殖民者取同一把枪（武器弱引用表的序号各机一致），绕持有者画并开火，子弹持有者是那名玩家：伤害规则（`EDF6Coop docs/net-re/damage.md`：攻击者是别的机器的玩家时，本机命中不结算，只认网络传来的）保证每发只算一次。死亡判定改为轮询殖民者的死亡标志（不再靠伤害钩子：别的机器打死的殖民者在本机未必走伤害函数）。

### 11.3 没核实的

- 两机实测一次都没做（本机只能开一个游戏）：位置修正的具体手感（主人每 16 帧发位置、远端修正，抓持中会不会抖）、`0x77C8E0` / `0x77AD70` 的插值、Monster501/504 的掩码。
- 远端玩家被抓：各机都对该玩家的副本做同一抓持，该玩家机器发的位置为准（M）。
- 悬浮炮的子弹在非持有者机器上不结算伤害，依据是 damage.md 的原版规则；Coop 的命中权威（`HitAuthority=1`）开着时对插件子弹的处理没实测。
