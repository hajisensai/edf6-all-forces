# 子弹穿过同编队僚机：静态逆向结论

目标：本插件的 Vehicle506_Helicopter（vtable `image+0x17DB238`）发射的
`v_506heli_gatling01_l/_r.sgo` 与 `v_506heli_missile01.sgo`，命中判定跳过同编队的其它僚机；
其余友军误伤保持原版不变。

EDF.dll x64，TimeDateStamp `0x678CCB46`。下文地址都是 RVA；可信度 H/M/L 指静态证据强度，未在游戏内验证。

## 1. 结论

| 问题 | 结论 | 可信度 |
|---|---|---|
| 机炮的弹类 | `SolidBullet01`（SGO: AmmoClass SolidBullet01，AmmoExplosion 0，AmmoIsPenetration 1，AmmoSize 0.3） | H |
| 命中检测在哪 | 不在逐弹 update 里，而是在全局批处理 `BulletSystem step 0x237CF0` 中：broadphase 收集候选 → layer 过滤 → shape cast → 命中处理 | H |
| 自身排除在哪 | `BulletControl::CustomCollector::addBody` `0x232AA0`（vtable `0x179E128` slot 0）。它把候选 body 映射回 game object，与 `core+0x9A8`（owner 弱引用）做**指针比较**，相等就不加入候选 | H |
| 是否用碰撞过滤组 | 否。子弹查询的 filterInfo 是纯 layer（systemGroup=0），object 用 layer 11，map 用 layer 12。hknp 的 group 规则对它不生效 | H |
| 机炮为何打到僚机 | 机炮 AmmoExplosion=0，所以 `core+0xA27/0xA28`=0，**完全不做阵营检查**，只剩 owner、死亡、已命中集合、key-2 属性四道检查 | M/H |
| 导弹走哪条路 | `MissileBullet01` 共用同一个 BulletControl core 和同一批处理，所以直击走同一个 `0x232AA0`；AmmoExplosion=12，`A28`=1，原版已跳过友军直击（目标 `+0x380 & 0x100000` 时例外）。**爆炸伤害是另一条路径**（`0x542xxx`），这次没有分析 | 直击 H / 爆炸 L |
| 推荐方案 | 替换 `0x179E128` 处的 vtable 指针，hook `addBody`：owner 是本编队僚机、且候选 object 是同编队另一架僚机时直接 return，否则调用原函数 | M/H |

## 2. 弹类与每帧路径

| 项 | RVA / 偏移 | 说明 | 可信度 |
|---|---|---|---|
| SolidBullet01 vtable | `0x17A3F78` | slot 5 = update `0x28BD30` | H |
| MissileBullet01 vtable | `0x17A1C10` | update `0x26A880` | H |
| BulletBase vtable | `0x179E048` | 继承链 SolidBullet01 → BulletBase → SceneObject；`+0x120` NetworkBullet，`+0xE90` umbra::Object | H |
| BulletControl（core） | `bullet+0x140` | 两种弹共用；**下表 core 偏移都相对 core** | H |
| core update | `0x235D50(core, dt)` | 按 `core+0xAF0` 移动类型分派：0 → `0x2364D0`，2 → `0x235FA0` | H |
| 批处理 step | `0x237CF0` | 由 Application vtable `0x17E91C0` slot 7（`0x704EB0`）调用，在游戏主线程上遍历子弹列表 `rdi+8` | M/H |
| 1. 扫掠 + 候选 | `0x233BD0(core, dt)` | 先清 `core+0x820`，再按类型调用 `0x2349D0` / `0x234420` / `0x233CB0`；内部做 map 射线（layer 22，`0x11A7EE0`），以及 broadphase 收集 `0x11A70B0(collector=core+0x800, wrapper, from=core+0xB80, to, extents)` | H |
| 1b. layer 过滤 | `0x2359C0` → `0x1079D0` ×2 | 候选 `core+0x818` 先用 map info `core+0x890`（layer 12）过滤进 `core+0x8C8`，再用 object info `core+0x894`（layer 11）过滤进 `core+0x8D8`；过滤器 = `*(image+0x20B2970)+8` 的 hknpGroupCollisionFilter | H |
| 2. map 最近命中 | `0x231840` / `0x11AA620` / `0x11A7AC0` / `0x238700` | 收集器 `core+0x8F0`；`0x234D20` 把线段截断到 map 命中点，并置 `core+0xAF4 \|= 4` | H |
| 3. object shape cast | `0x231720` / `0x11A8E40` | 形状 `core+0x18`（半径 `core+0xBE0`），all-hits 收集器 `core+0x990`，最多 0x32 个；**只对 `core+0x8D8` 里的 body 投射** | H |
| 4. 命中处理 | `0x2321B0(core)` → `0x230CA0` | hit 步长 0x70，`+0x48` 是 bodyId，`+0x20` 是 fraction；`0x108260(bodyId)` 取 object，伤害走 `0x541FF0(core+0x6E0, weak{obj+0x28,obj+0x30}, core+0x730)`；爆炸走 `0x542490` / `0x542670` / `0x542860` | H |

要点：步骤 3 只投射到步骤 1 留下的候选 body 上。所以只要在步骤 1 的 `addBody` 里不加入某个 body，子弹就会从它身上穿过去，不会产生命中和伤害（H）。

## 3. 自身排除：`BulletControl::CustomCollector`

### 3.1 收集器布局（由 core ctor `0x22FD80` 构造在 `core+0x800`）

| 收集器偏移 | core 偏移 | 含义 | 可信度 |
|---|---|---|---|
| `+0x00` | `+0x800` | vtable `0x179E128`（slot 0 = `0x232AA0`，slot 1 = `0x230B90`） | H |
| `+0x18` / `+0x20` | `+0x818` / `+0x820` | 候选 bodyId 列表 / 数量 | H |
| `+0x28` (byte) | | 0 | H |
| `+0x29` (byte) | | 1（compound body 走 key 1 的路径） | M |
| `+0x30` | | 单个忽略 object，子弹恒为 0 | H |
| `+0x38` / `+0x3C` / `+0x40` | | 基类可选的阵营检查参数，子弹为 0 / -1 / 0 | M |
| `+0x80` | | 可选 functor，子弹为 0 | M |
| `+0x88` | | 回指 core | H |

继承链：CustomCollector → `hit_query::NpTargetCollector`（vtable `0x1768F40`，基类 addBody `0x109DA0`）→ `xgs::havok::BroadPhaseCollector`。

### 3.2 `0x232AA0(this, uint32 bodyId)` 逻辑（core = `*(this+0x88)`）

| 顺序 | 检查 | 字段 | 可信度 |
|---|---|---|---|
| 0 | body = `0x11AD2E0(g+0x10, id)`；obj = `0x108200(body)`（= body 属性 key 0，SceneObject*）；`rbp = dynamic_cast<GameObjectBase*>(obj)` | g = `*(image+0x20B2958)` | H |
| 1 | **owner 排除**：若 `!(core+0xAF4 & 0x80)`，锁 owner 弱引用 `core+0x9A8`（ctrl `core+0x9B0`），**owner == obj 则跳过** | bit 0x80 = 「可以打到 owner」，init 时由 `param+0x8B` 置位（`0x231D24`），`0x236662` / `0x2361E4` 两条移动路径也会置位 | H |
| 2 | `core+0xAF4 & 0x1000` 且 `rbp+0x2E8`（已死亡）→ 跳过 | | H |
| 3 | 除非 body 有 key 2 属性，否则查已命中集合（FNV 哈希，`core+0x7C0`，列表 `+0x7C8` / `+0x7D8`，mask `+0x7F0`），已命中 → 跳过 | 用于贯通弹 | M/H |
| 4 | 阵营检查：仅当 `rbp` 存在且 `core+0xA27 \|\| core+0xA28`；`relation(rbp+0x314, core+0xA00) == 1`（友方）时，若 `core+0xA29` 跳过，否则只有 `rbp+0x380 & 0x100000` 才不跳过 | 关系表：`rows = *(*(image+0x20B2978)+0x38)`，`rel = *(rows + team*0x38 + 0x18)`，`rel[other]`：1 友 / 2 敌 | H |
| 5 | `core+0xA2A` 且 obj 是 HumanBase → 跳过 | | H |
| 6 | `core+0x788 > const` 且有 key 5 → 跳过；`core+0xB04 == 3` 且有 key 9 → 跳过 | | M |
| 7 | body 有 key 2 属性时，`0x22E800(*prop, core+0xA00)` 为假 → 跳过。只有该属性的 team（`prop+8`）与子弹 team 为敌对（rel==2）或任一方为 -1 才能命中 | | M/H |
| 8 | 调用基类 `0x109DA0(this, id)`：比较 `this+0x30` 单个忽略对象、可选阵营检查、compound 路径，最后 `0x11A9EC0(this, id)` 加入列表 | | H |

### 3.3 子弹参数 → core 字段

`0x22E9C0` 调用 `0x231CC0(core, matrix, InitParam*)`，后者用 `0x2307F0` 把参数复制到 `core+0x9A0`，所以 **core 偏移 = 0x9A0 + param 偏移**。武器开火 `0x696FD0` 把模板 `weapon+0x830` 复制到 `weapon+0xA10` 再发射，所以 **weapon 偏移 = 0x830 + param 偏移**。

| param | core | weapon | 含义 | 来源 | 可信度 |
|---|---|---|---|---|---|
| `+0x08` / `+0x10` | `+0x9A8` / `+0x9B0` | | owner 弱引用（ptr / ctrl），同时复制到 `core+0x740/+0x748`（attacker） | SetOwner `0x2355F0`，ClearOwner `0x2315C0` | H |
| `+0x60` | `+0xA00` | `+0x890` | 子弹 team | `0x6B1380` 从 `weapon+0x214` 写入 | H |
| `+0x64` | `+0xA04` | `+0x894` | 速度 | AmmoSpeed | H |
| `+0x86` | `+0xA26` | `+0x8B6` | 未定名标志 | `0x632FCB` / `0x633950` 置 1，RideAi `0x6330C9` 置 0 | M |
| `+0x87` | `+0xA27` | `+0x8B7` | 启用阵营检查 | 武器/载具范围内没找到写入者 | M |
| `+0x88` | `+0xA28` | `+0x8B8` | 启用阵营检查 | 武器 init `0x68A920` 在 `0x68DB14`：AmmoExplosion（`weapon+0x8B0`）> const 时置 1 | H |
| `+0x89` | `+0xA29` | `+0x8B9` | 友方无条件跳过（同时强制 A27=1） | 没找到写入者 | M |
| `+0x8A` | `+0xA2A` | `+0x8BA` | 不打 HumanBase | 没找到写入者 | M |
| `+0x8B` | `+0xAF4` bit 0x80 | `+0x8BB` | 可以打到 owner | `0x231D24` | H |

推论：机炮 AmmoExplosion=0，所以 A27=A28=0，阵营检查整段不执行，僚机只靠 owner 指针比较排除，而它只能排除开火的那一架（M/H）。

## 4. 方案比较

| 方案 | 做法 | 能否满足需求 | 可信度 |
|---|---|---|---|
| **A（推荐）vtable hook** | 把 `image+0x179E128` 的 8 字节指针换成我们的函数 | 能。只影响「owner ∈ 本编队、目标 ∈ 同编队其它僚机」这一组合，机炮和导弹直击都覆盖，其余全部交给原函数 | M/H |
| B 碰撞过滤组 | 把僚机 body 放进同一个 hknp systemGroup，用 sub/dont 位互斥 | 不能。子弹查询的 systemGroup=0，走的是 layer 表，group 规则不参与；只会关掉僚机之间的 body-body 碰撞 | H |
| C 给僚机 body 加 key 2 属性 | 写入僚机 team | 不能。所有友方子弹（包括玩家的）都会穿过僚机，改变了原版误伤 | M/L |
| D 设置收集器 `+0x30` 单个忽略对象 | 在开火时 hook 写入 | 不够。只能放一个对象，编队有多架僚机 | M |
| E 设置 `param+0x89` / `weapon+0x8B9`（数据层） | 让武器的子弹跳过所有友方 | 不满足需求：玩家、友军 NPC 也会被穿过。若可以接受「本编队子弹不伤任何友军」，这是零 hook 的退路（改武器对象字段，而不是游戏文件） | M |

### 4.1 方案 A：签名与调用约定

| 项 | 值 | 可信度 |
|---|---|---|
| 被替换的 vtable 槽 | `image+0x179E128`（.rdata，写前要 `VirtualProtect`） | H |
| 槽内原值 | `image+0x232AA0`（已静态确认）；全镜像中这个函数**没有其它指针或直接 call 引用** | H |
| 原函数签名（MS x64） | `void __fastcall addBody(void* collector, uint32_t bodyId)`（rcx, edx），无返回值 | H |
| `0x232AA0` 序言（32 字节） | `48 89 4C 24 08 53 55 56 57 41 54 41 56 41 57 48 83 EC 30 4C 8B F1 45 33 E4 44 89 A4 24 80 00 00` | H |
| `0x108260` body → object（序言） | `48 83 EC 28 48 8B 05 ?? ?? ?? ?? 8B D1 48 8D 48 10 E8 ?? ?? ?? ?? 48 8B D0 48 85 C0 75 05`；`void* __fastcall(uint32_t bodyId)`，返回 SceneObject*（body 属性 key 0），找不到返回 0；内部用全局 `image+0x20B2958` | H |
| `0x109DA0` 基类 addBody（序言） | `48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 30 80 79 28 00 8B F2 48 8B E9 75 11` | H |
| 线程 | 批处理 `0x237CF0` 在主循环 Application slot 7 中调用 | M |

### 4.2 方案 A：实现草图

```cpp
using AddBodyFn = void(__fastcall*)(void* collector, uint32_t bodyId);
using BodyToObjFn = void*(__fastcall*)(uint32_t bodyId);

static AddBodyFn  g_origAddBody;   // image + 0x232AA0（从 vtable 槽读出，先核签名）
static BodyToObjFn g_bodyToObj;    // image + 0x108260

// 由编队管理维护：只在游戏主线程写，或用 SRWLock 保护。
bool IsOurJet(const void* obj);                  // obj 是本插件管理的某架僚机（vehicle 基址）
bool SameFlight(const void* a, const void* b);   // a、b 同属一个编队且 a != b

static void __fastcall AddBodyHook(void* collector, uint32_t bodyId) {
    auto* core  = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(collector) + 0x88);
    auto* owner = core ? *reinterpret_cast<void**>(core + 0x9A8) : nullptr;  // 弱引用的裸指针
    if (owner && IsOurJet(owner)) {
        void* obj = g_bodyToObj(bodyId);
        if (obj && obj != owner && SameFlight(owner, obj))
            return;                      // 不加入候选 → shape cast 不打它 → 子弹穿过
    }
    g_origAddBody(collector, bodyId);
}

// 安装：核对 *(uint64*)(image+0x179E128) == image+0x232AA0，以及 0x232AA0 的序言字节；
// 任一不符就不装（版本不匹配）。VirtualProtect(PAGE_READWRITE) → 写槽 → 恢复保护。
// 卸载：把原值写回。
```

说明：

- `core+0x9A8` 是弱引用里的裸指针，原函数在比较前会先锁 ctrl `core+0x9B0` 判断是否过期。hook 里只用它做**集合成员查找**（与 `IsOurJet` 里登记的指针比较），不解引用。所以即使 owner 已销毁，也只会得到「不在集合中」，然后回落到原函数（M/H）。
- 僚机销毁时要先从 `IsOurJet` 集合中移除，再调用 `kDelete`，避免同一地址被新对象复用后误判（M）。
- `IsOurJet` 必须按 SceneObject 基址比较，也就是 key 0 属性返回的那个指针。`0x232AA0` 本身就是用这个指针和 owner 比较的，所以只要 owner == 载具基址成立，两边口径就一致（见 §5 第 2 条）。

## 5. 必须在游戏内验证的事项

| # | 验证内容 | 怎么看 | 当前可信度 |
|---|---|---|---|
| 1 | hook 对机炮子弹会被调用 | 计数日志：owner 是僚机时的调用次数 > 0 | M/H |
| 2 | **僚机机炮子弹的 owner（`core+0x9A8`）就是载具基址**，而不是驾驶员 NPC 或武器对象 | 记录 owner 与已知僚机指针、驾驶员指针的对比。若是驾驶员，就在 `IsOurJet` 里同时登记驾驶员指针 | M |
| 3 | 僚机 body 的 key 0 属性就是载具基址 | 记录 `0x108260(bodyId)` 的返回值 | M/H |
| 4 | 僚机互相不再被打中，而玩家、友军 NPC、敌人照常被打中 | 观察血量和命中特效 | — |
| 5 | 导弹直击同样被跳过（原版本来就因 A28=1 跳过友军，主要确认没有回归） | 同上 | H |
| 6 | **导弹爆炸溅射是否仍会伤到僚机**。`0x542860` 等爆炸路径会使用候选列表 `core+0x8D8`，有可能顺带被本 hook 排除，也可能走独立的范围查询 | 让导弹在僚机旁边爆炸，看僚机是否掉血 | L |
| 7 | 贯通弹（AmmoIsPenetration 1）穿过僚机后能继续命中后面的目标 | 观察 | M |
| 8 | compound body（收集器 `+0x29` 路径）不会绕过 slot 0 | 僚机由多个 body 组成时检查是否仍被命中 | M |

## 6. 附：相关全局与工具函数

| 名称 | RVA | 说明 |
|---|---|---|
| ObjectMgr 全局 g | `image+0x20B2958` | `g+0x10` 是物理 wrapper；`0x11AD2E0(g+0x10, id)` 按 bodyId 取 xgs body |
| CollisionFilter 包装 | `image+0x20B2970` | `+8` 是 hknpGroupCollisionFilter（vtable `0x18BBFC0`，ctor `0xD92730`，isCollisionEnabled `0xD93700` / `0xD93790`）；包装类 vtable `0x17692A8`，ctor `0x105510` |
| 阵营关系表 | `image+0x20B2978` | 见 §3.2 第 4 条 |
| filterInfo 构造 | `0xD929F0(layer, group, sub, dont)` | 位布局：layer 0–4，sub 5–9，dont 10–14，group 15–30 |
| 子弹 object layer | 11 | 与 {5, 6, 7, 8, 9, 14} 碰撞 |
| 子弹 map layer | 12 | 与 {15–20} 碰撞 |
| xgs body → object | `0x108200(xgsBody)` | 属性 key 0 |
| bodyId → object | `0x108260(bodyId)` | 同上，经全局 g |
| 候选加入 | `0x11A9EC0(collector, id)` | |
| 候选 layer 过滤 | `0x1079D0` | 调用 filter vtbl+0x30 |
