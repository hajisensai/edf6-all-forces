# 伤害统计：伤害结算、子弹到武器、名字与击杀（静态逆向）

EDF.dll TimeDateStamp `0x678CCB46`，地址全是 RVA。H = 反汇编里直接读到；M = 由代码结构推出；L = 推测。全部是静态分析（`tools/edfre.py` + capstone），没有实机核对。实现在 `src/damagestats.cpp`，数据与版面在 `src/damage_stats.h`。

伤害管线的总体（入队、结算、扣血、友伤判定）见 `docs/subcarrier-re.md` §8.1；GameDamageInfo（下称 GDI）的布局见 `docs/decoy-blast-re.md` §1.2。

## 1. 入队与结算

### 1.1 直接命中（H）

`0x230CA0` 在 `0x230EA6` 调用 `0x541FF0(rcx = core+0x6E0, rdx = &weak{target}, r8 = core+0x730)`：

- GDI 指针就是 `core+0x730`，即 `bullet+0x870`（core = `bullet+0x140`）。
- 队列在 `core+0x6E0`（`bullet+0x820`），每发子弹一个。

### 1.2 范围伤害（H）

- `0x542490` / `0x542670` 把 rcx 原样交给实现 `0x542860`；子弹的爆炸与到期爆炸调用它时 rcx 都是 `core+0x730`（`0x232507`、`0x232682`、`0x2326CE`、`0x23645D`、`0x236978` 等）。
- `0x542860` 先在 `0x5429B9` 把 GDI 复制到栈上，对每个命中目标在 `0x542FD4` 入**栈上的局部队列**，再在 `0x54301D` 同步结算。入队时拿不到源 GDI，只有 `0x542860` 的 rcx 是源头。
- `0x542860` 的签名（2026-10-10 主会话对三个调用点核对过）：`(GDI* rcx, Flag* rdx, hitSet* r8, float xmm3 半径, uint32 [rsp+0x20] 帧号, SceneObject* [rsp+0x28] 忽略对象, void* [rsp+0x30])`，返回 Flag*。r9 不用。函数头读 `[rbp+0x3C0]` = 入口 `rsp+0x38`，即第 7 个参数。

### 1.3 队列（H）

| 位置 | 内容 |
|---|---|
| 队列 `+8` / `+0x10` / `+0x18` / `+0x20` | 条目数组 / 容量 / 条目数 / 互斥锁 |
| 条目（0xA0 字节） | `+0/+8` 目标 weak_ptr，`+0x10` GDI 拷贝 |

同一目标已在队里时不追加新条目，只把子命中记录合并进旧条目，第二次命中的 `+0x50` 伤害被丢弃：每次结算一个目标最多一条。

GDI 拷贝（`0x114210`）里**没有任何指回来源的字段**，所以不在入队时建匹配表。

### 1.4 结算（H）

`0x543920(queue, Flag*, hitSet*)` 按插入顺序逐条调目标的 slot 10 / 9 / 11；slot 9 走 `0x54A530`，在 `0x54A586` 调 `0x547C30(obj, GDI = entry+0x10)`，最后在 `0x543BC4` 清空队列。直接命中由 `0x2321B0` 里的 `0x232702`、`0x233CB0` 里的 `0x23426F` 结算，范围伤害由 `0x542860` 里的 `0x54301D` 结算。**全部在调用者内部同步完成**（同一帧、同一线程、同一次调用）。

带 `0x10` 标志的爆炸弹在 `0x23268E` / `0x2326E2` 用 `0x231490` 直接清空直接命中队列，只走范围伤害。

### 1.5 其它入队者（H，都不是子弹，没有武器）

接触伤害组件（`0x6F1640` / `0x6F15B0`；载具撞人在 `veh+0x680`）、InsectBase 系身体攻击（`0x4A8B40` / `0x4A8FD0`）、Merman、GiantDango 系、列表伤害 `0x543340`（BigGreyBoss 陨石）；`0x542490` / `0x542670` 的非子弹调用者：任务脚本、`EfsBullet_HitAreaEffect`、多种敌方动作。这些伤害的攻击者就是 GDI `+0x10` 的对象。

## 2. 子弹 → 武器

### 2.1 子弹里没有武器（H）

子弹只认 owner：参数块 `+0x08`，复制到 `core+0x9A8`（= `bullet+0xAE8`，控制块 `bullet+0xAF0`）和 GDI `+0x10`。owner 来自武器 `+0x838`，只在武器 init（`0x68D5AD`）写一次。

### 2.2 开火时同步创建子弹（H）

`0x696FD0(weapon, muzzle, overrideParam, counter, replay)`：`r14 = weapon+0x7F8` 弹类工厂；`r12 = weapon+0x800` InitParam，有 override 时 `r12 = weapon+0x9E0`。散弹的每一颗都经同一个调用点 `0x69799F: call 0x1194280(mgr, &matrix, factory, InitParam)`，它同步返回新对象。网络回放 `0x692540` 也经 `0x696FD0`，一样走到 `0x69799F`。所有玩家武器类（33 个 `Weapon*`）开火都经 `0x696FD0`。

`0x696FD0` 的函数头已被 `heli.cpp` `InstallMedicPermission` 占用，所以标签打在 `0x69799F`（RedirectCall，目标 `0x1194280`），由 InitParam 减 `0x800` 或 `0x9E0` 得到武器，再用 RTTI 核对类名以 `.?AVWeapon` 开头。

### 2.3 打不上标签的情况（实现里按攻击者归类）

| 情况 | 说明 |
|---|---|
| Maser（EMC，`Weapon_VehicleMaser`） | `0x6B27B6` → `0x2B3340` → `0x2B2A10`，在 `0x2B2BAD` 构造 `DummyBullet`，不经 `0x69799F`（M/L） |
| 派生子弹 | 通用开火单元 `0x2B95A0` 在 `0x2B9F68` 创建（NapalmBullet01、MissileBullet02、ClusterBullet01、`DemoIndirectFire`）；哨戒炮 `0x2A4291` / `0x2A42F2`（M）。可按「InitParam 落在已打标签的父子弹内部」继承标签，**未实现** |
| 敌人 | 不用玩家的 Weapon 类（`HumanoidWeaponBase` 等各自的路径，owner 是敌人本体，H），只按攻击者分类 |

## 3. 武器的名字（H）

`weapon+0x1B0`：`const wchar_t*`，**游戏当前语言的显示名**（武器 init `0x68A920` 读 SGO 里的 `name.<语言码>`，在 `0x68CC19` 存入，原版 HUD 显示的就是它）。空时用 `weapon+0x08` 资源节点的 SGO 文件名（`stores.cpp` `WeaponFile`）。标签在开火时把名字**拷贝**下来：武器可能先于子弹销毁。

## 4. 对象的类型

- RTTI：`vtable-8` 是 CompleteObjectLocator，`+0xC` 类型描述符、`+0x10` 类层次（`+8` 基类数、`+0xC` 基类数组，每项的 `+0` 是基类的类型描述符），名字在类型描述符 `+0x10`（H）。插件用类层次判断「是不是 `SoldierBase`」。
- SGO：所有 SceneObject 的基类构造 `0x1189C00` 在 `0x1189D1D` 写 `obj+0x08 = 资源节点`，经 `CreateObject 0x11945E0` 生成的敌人、载具、士兵、武器都有（结构 H；所有敌人都经此路径 M）。子弹经 `0x1194280` 创建，`+0x08` 为空。大型敌人的部件可能为空或是部件自己的 SGO（M）。
- **敌人没有本地化显示名**（M）：`name.*` 只给武器用。统计页显示 SGO 名（如 `E501_ANT_RED`），读不到时显示 RTTI 类名。

## 5. HP 与击杀（H，来自 `0x547C30`）

| 字段 | 含义 |
|---|---|
| `+0x2F8` / `+0x2F4` / `+0x2F0` | 当前 HP / 上限 / 下限 |
| `+0x394` | 受伤倍率 |
| `+0x2E8` / `+0x2E9` | 死亡字节 / 「这一下打死的」 |

正伤害 `hp += -GDI+0x50 × [+0x394] × 友伤系数 × 衰减`（`0x548109..0x54817A`），负伤害走治疗分支 `0x548284`，同样写 `+0x2F8`；已死时不写。`0x54840B` 判断 `hp ≤ 0` 且原本未死，就写 `+0x2E9 = 1`、`+0x2E8 = 1`，再给攻击者发消息 `0x10000006`。原版调用点 `0x54A579` / `0x54A593` 也用调用前后的 `+0x2E8` 判断死亡。`kMsgDie = 0x1000000F` 不是常规死亡路径。

所以：**实际伤害 = 调用前 HP − 调用后 HP（负数即治疗），击杀 = 调用前未死且调用后已死**。

例外（M）：部分部件类在自己代码里写 `+0x2E8`、不经 `0x547C30`（DeiroiFootParts、Mother511 各炮台和 RingUnit、MovingFortress、Monster504、AssultSoldier / Engineer 的 slot 41 等），重写了 slot 9 且不链回 `0x54A530` 的类也看不到。

## 6. 谁开的火

- 步兵武器：owner = 士兵本人（H）。本机玩家 `edf::IsPlayer`，其他机器的玩家 `IsAnyPlayer`。
- 载具武器：owner = **载具本身**，不是驾驶员（`SetWeaponObject 0x633330`，H）。插件在打标签时找握着这件武器的座位（座位 `+0xC8` 持有者指针数组、`+0xD8` 个数，持有者 `+0x10` 是武器），看那个座位上坐的是谁，结果存进标签，玩家中途下车也不会改归属。

## 7. 钩子

| 位置 | 方式 | 作用 |
|---|---|---|
| `0x547C30` 函数头 14 字节（`48 8B C4 48 89 58 18 55 56 57 41 54 41 55`，整条指令、无 rip 相对寻址） | detour（同 `heli.cpp` 的 trampoline） | 读调用前后的 HP 与死亡字节，记一笔 |
| `0x69799F`（E8 → `0x1194280`） | RedirectCall | 给子弹打标签 |
| `0x232702`、`0x23426F`（E8 → `0x543920`，rcx = `core+0x6E0`） | RedirectCall | 线程局部「来源栈」压入 `rcx+0x50`（= `core+0x730`），调原函数后弹出 |
| `0x23250E`、`0x5425D6`、`0x5427BF`（E8 → `0x542860`） | RedirectCall | 压入 rcx（源 GDI），参数按 §1.2 原样转发 |

- **不改 `0x54A586`**：`subcarrier.cpp` 的 `DamageCallReaches()` 要求那条 call 仍指向 `0x547C30`，改了会让潜舰的伤害分流在安装时被静默关掉。
- 来源栈支持嵌套：击杀触发的爆炸会在结算里再进 `0x5425D6`。
- `0x547C30` 里查栈顶来源：`子弹 = 源 GDI − 0x870`，标签要与子弹现在的 vtable、owner（`bullet+0xAE8`）以及这一下的攻击者（GDI `+0x10`）都对得上才用。
- 这些位置仓库里都没被别的模块占用（`sidecar.cpp` 改的是 `0x542FD4` / `0x54360E`，`boarding.cpp` 改的是 `0x230EA6`）。

## 8. 未核实

- 联机（L）：没有核实非主机一侧是否也对敌人执行 `0x547C30`。原版死亡分支会发 `RequestDead`，伤害权威可能在主机，客机统计到的可能不全。
- 线程（M）：子弹批处理在 Application slot 7，推断是游戏主线程；结算在压栈的调用内部同步完成，所以来源栈在同一线程上一定可靠（H）。标签表另加了读写锁，开火若在别的线程也不会出错。
