# 第一人称视图模型：渲染 pass、蒙皮矩阵、挂枪、枪口特效、近裁剪面（静态逆向）

EDF.dll TimeDateStamp `0x678CCB46`，地址都是 RVA。纯静态分析（`tools/edfre.py` + capstone；Root.cpk 经 `pylib/rootcpk.py` 只读），
没有启动游戏。置信度：**H** = 指令里直接读到；**M** = 有代码证据的推断；**L** = 推测，必须实机确认。
姊妹文档：`docs/fp-soldier-re.md`（瞄准同步、射击矩阵 `0x6969A0`、开火 `0x696FD0`）、`docs/view-distance-re.md`（渲染镜头、Umbra 遮罩）、
`docs/camera-re.md`、`docs/drill-re.md` §5.4（「画面姿态」数组的第一次发现）。脚本在 `tmp/fpr_q/`（未提交）。

## 0. 结论速览

1. **方案 A 换 `node+0xB0` 不行，换「画面姿态数组」可以**（§2）。绘制是游戏线程上的立即调用：`System::update 0x1198DA0` 每帧先调
   渲染 `0x11978D0`（`0x1198FA4`），再跑对象更新、最后步进镜头。蒙皮调色板在**绘制调用内部**由 `0x11009A0` 从骨架实例的
   画面姿态数组（`inst+0x30`，每骨 0x40 字节）算出，经 `UpdateSubresource` 立刻拷进常量缓冲；刚体网格也在绘制内读同一数组。
   这个数组只在**更新**里由 `0x1100F10` 从 `node+0xB0` 抄一次（`AnimationModel` 同步 `0x6C06A0`）。所以：在 `0x6BF9F0` 钩子里
   改 `inst+0x30[i]` → 调原函数 → 还原，就能只改这一次绘制；子弹、枪口、碰撞、联机全不受影响（它们读 `node+0xB0`）。
   VR mod「挪武器变换没用」正是因为画面姿态在绘制前（更新里）就定了，绘制根本不读骨骼记录。
2. **pass 号**（§1）：0 = 主视角遮挡体深度预渲染，1 = 主视角 G-buffer（不透明），2 = 主视角半透明（前向、按距离排序），
   3 = 阴影图（每个级联一次），4 = 主视角 bit27 列表（`AnimationModel` 不画），5 = 远景镜头。第一人称下身体：**1/2/5 不画（或只画手臂，见 §6），
   3 照常画（保留影子），0 也跳过**。跳过 1/2/5 时要自己写 `+0x4D0 = 1`、`+0x4D4 = 0`（§3），否则次级物理（裙摆、头发、链条）停摆。
3. **剔除 / LOD**（§3）：对象级 = Umbra 视锥 + 遮挡（按 Umbra 对象包围盒，变换在 `model+0x110`）+ 剔除距离（`umbra 包装 +0x60` 平方）；
   LOD = 绘制里按 `(+0x24[+0x45])²` 与各 LOD 的阈值比。视图模型放在镜头前 5 cm 不会被 LOD 换掉（距离 0 = 最高 LOD）；
   视锥剔除用的是武器**真实**位置（手上），第一人称下它本来就在镜头前方附近。
4. **挂枪**（§4）：武器 SGO `ModelConstraint[0]`（步兵枪 1179 把是 `arms_r`，`te_r` 的子定位点）→ `weapon+0xE88`；
   `0x68FD00` 每帧把这根骨骼的世界矩阵抄给武器根并 `UpdateBone`。枪口 = 武器 SGO `animation_model[2]` 的 MAB 列表 0（名字 `01`，挂在武器模型节点上 + 局部偏移）。
5. **枪口火光可以单独挪，不影响子弹**（§5）：火光对象构造 `0x2BA520` 把 `&muzzle+0x50`（射击矩阵）存在 `flash+0x120`，之后 3 次更新每次照抄它；
   子弹在 `0x696FD0` 里同步生成完了。弹壳 `0x690660` 是世界空间刚体，只能另做本地视觉副本。
6. **近裁剪**（§7）：渲染镜头 `+0x28` 每帧由 `0x118DFF0` 读来建投影和 Umbra 视锥；阴影级联切分是常量表（0.1/27.5/120/200），与它无关。
   0.01 会让远处深度精度掉到 1/10（远/近比 3000/0.01 = 3×10⁵），建议先试 0.03–0.05（L）。

---

## 1. `AnimationModel` 绘制 `0x6BF9F0` 与 pass（H，除注明）

`AnimationModel` vtable `0x17C4030`：slot 0 `0x490990`、**slot 1 绘制 `0x6BF9F0`**、slot 2 半透明绘制 `0x6C2C10`、slot 4 `0x6BFB80`。
签名 `draw(model rcx, ctx rdx, int pass r8d, view r9)`；`view` = `DeferredRenderSystem+0xE0`（镜头参数块），`ctx` = `[sys+0x10]`。

开头选 LOD：默认 `rdi = model+0x160`；遍历 `model+0x250`（个数 `+0x260`，步长 0xF0）：`entry+0xE8 > d²` 就停，否则 `rdi = entry`，
其中 `d = model+0x24[ byte model+0x45 ]`（§3）。然后按 pass：

| pass | 做什么 | 写 `+0x4D0/+0x4D4` |
|---|---|---|
| 0、3 | `0x1104100(lod, ctx, pass, inst=[lod+0x58], list=inst+0x28, flag 0)` | 否（`0x6BFB56` 直接返回） |
| 1 | `0x6CD340([model+0x288])`（L：材质 / 部件状态）后 `0x1104100(..., 1, flag 0)` | 是 |
| 5 | 同上，`0x1104100(..., 1, flag 1)` 再 `(..., 2, flag 1)`（不透明 + 半透明两张网格表） | 是 |
| 2 | slot 2 `0x6C2C10`：每个半透明网格按到镜头的距离排序后画（`model+0x482` 置位时走自己的实例 `+0xA0` / 姿态 `+0xC8`） | 是 |
| 4、≥6 | 什么也不画 | 否 |

`+0x4D0 = 1`；`+0x4D4 = min(+0x4D4, d²)`（`0x6BFB16..0x6BFB38`）。

LOD 网格提交 `0x1103DA0(lod, ctx, pass, inst, list, flag)`：pass 3 时 `byte lod+0x60 == 0` 直接返回（这个 LOD 不投影，M）；
pass 2 用网格表 `lod+0xD0/+0xE0`，其它用 `lod+0xB0/+0xC0`；`0x10FF300(ctx, pass)` 按 pass 设状态。

### 1.1 pass 从哪来：`xgs::umbra::Commander`（vtable `0x1AE5288`，slot 1 `0x11E2120`）

`0x11E2120(sys, cmd)` 是 Umbra（`umbra_sandlot.dll`）的 Commander 回调，`cmd` 0..9 跳表 `0x11E435C`。渲染系统 = `sys`（r13），
`sys+0xE4` = 当前查询的视图类型：**0 主视角、1 远景镜头、2 阴影光源**（M：与 `view-distance-re.md` 的镜头遮罩 bit25|27 / bit26 / bit30 对得上）。

- cmd 3（收集，`0x11E3E1C`）：对每个可见对象 `0x11B28C0` 算距离量（§3）；`d² > +0x60` 剔除；然后按对象遮罩 `+0x20` 进列表（`0x11E3F17..0x11E3FBC`）：
  - 视图 0：bit28 且 `ObjectData::isOccluder`（导入 `0x12D616A`）→ 列表 `+0x58`；bit29 → `+0x38`；bit28 → `+0x18`；bit27 → `+0x98`。
  - 视图 1：bit26 且 `+0x68 > +0x6C` → `+0x78`。
  - 视图 2：全部 → `+0x18`。
- cmd 4（`0x11E3FD1`）：视图 0 → 渲染状态 1（`0x11E18C0` 情况 1：颜色写掩码 `ctx+0xA0 = 0`，只写深度）后，列表 `+0x58` 用 **pass 0** 画
  = **遮挡体深度预渲染**（给 Umbra 遮挡查询用，cmd 5 取 `getOcclusionQueryData`）；视图 2 → 同一状态 1，列表 `+0x18` 用 **pass 3** 画 = **阴影图**。
- cmd 1（`QUERY_END`，`0x11E2247`）：视图 0 才画：列表 `+0x18` **pass 1**（状态 2 或 4，颜色写掩码 0xF，G-buffer）→ 列表 `+0x98` **pass 4**
  → `DeferredRenderSystem::imdBeginForward` … → `imdBeginFar` 后列表 `+0x78` **pass 5**（远景镜头收集到的对象）→ 列表 `+0x38` 先按距离排序（`0x11E0D20`）
  再 **pass 2**。视图 1/2 的 cmd 1 不画。
- 提交：`0x11E1630(sys, obj, pass)`：对象 `+0x18` 有批次就进批次（批次 slot 0 再按 pass 画），否则**尾调用 `obj->vtbl[1](ctx, pass, sys+0xE0)`**（`0x11E178E`）；
  pass 5 的循环直接 `call [vtbl+8]`（`0x11E3399`/`0x11E356B`）。
- 阴影级联：`0x118DFF0` 在 `cam+0x38 != 0` 时对 `cam+0x188` 个级联各建一个光源视锥（`0x118BB40`），切分取常量表
  `0x1FEEE90`（近）/`0x1FEEE80`（远）= (0.1, 27.5) (27.5, 120) (120, 200)，`0x1FEEEA0` = 1200（只有这三处读，无写入，H）。每个级联是一次视图 2 查询，所以 pass 3 每帧调用多次。

**第一人称身体的 pass 选择**：跳过 1、2、5（看得见的三个颜色 pass，VR mod 的做法）；pass 3 原样画（影子）；pass 0 也跳过——身体若被当成遮挡体，
它在主视角深度里的轮廓会让后面的 G-buffer 在那里留洞（M：身体是否 `isOccluder` 未知，跳过它没有坏处）。pass 4 本来就不画。

---

## 2. 立即还是延迟？调色板在哪算（H）

调用链：`0x6BF9F0 → 0x1104100(lod, ctx, pass, inst, list, flag)`：

1. `0x11009A0(inst, ctx, list)`：`n = inst+0x20`，骨骼记录 `inst+0x10`（步长 0x110），**矩阵来源 = `[list+8]` = `[inst+0x30]`（每骨 0x40）**；
   每骨算 `记录+0x30（逆绑定）× pose[i]`，转成 3×4 写到栈上 48 KB 缓冲；`0x113F860(inst+0x48, buf)` → `0x113D240` →
   `ID3D11DeviceContext` vtable `+0x180` = **`UpdateSubresource`**（数据在调用时拷走）。
2. `0x1103DA0`：逐网格；刚体网格（`[mesh]+8 <= 0`）把 `pose[骨号]` 抄进 `lod+0x70 → +0x548` 的常量缓冲（`0x1103EB3..0x1103F78`，同样 `0x113F860`），
   再 `VSSetConstantBuffers`（vtable `+0x38`）和 `0x10FF4E0` 画。蒙皮网格用第 1 步的调色板。

画面姿态数组的唯一来源（`AnimationModel` 部分）：

| 写入 | 内容 | 线程 / 时机 |
|---|---|---|
| `0x1100F10(inst)` | `pose[i] = rec[i]+0xB0..+0xEF`（骨骼世界矩阵整块拷） | 游戏线程，更新 |
| `0x6C06A0(model)`（`+0x31C & 1` 时） | `0x11005E0`（包围）→ **`0x1100F10(model+0xA0)`** → `0x11B30C0(model, model+0x110)`（Umbra 变换）→ 声音/特效挂点 `+0x488` → 记账 `+0x4D0..+0x4D8`（§3） | 武器 slot 3（如 `Weapon_BasicShoot 0x69AB30`）经 `0x6934C0` |
| `0x6BA950(model, frame, &world)` | 根矩阵 `0x1100B90` → `0x11005E0` → `0x1100F10` | 士兵更新 `0x5717D0` 在 `0x571E40` 对 `soldier+0x860` 调 |
| `0x1100D50` + 回放 `0x1100E90` | 把世界矩阵录进命令流 `ctx+0x28A8`，回放时拷进 `inst+0x30` | **没有调用者**（死代码或经函数表，M） |

- 渲染在游戏线程：`0x1198DA0`（System 每帧）在 `0x1198FA4` 调 `0x11978D0`（内含 `0x1197B50 → 0x118DFF0` 建视锥、Umbra 查询 → Commander 回调 → 绘制）。
  之后才是 N 个定步长对象更新（`0x1198FB9` 起）和镜头步进 `0x118DF40`（`0x11995A6`，`CharacterGhostCamera 0xF86A0`）。
  **一帧 = 用上一轮的更新结果 + 上一轮的镜头画面，然后更新、步进镜头。**（H 调用顺序；没看到另起渲染线程，M）
- `[ctx+8]` 是立即上下文还是延迟上下文没查（L），但两种 `UpdateSubresource` 都在调用时复制数据，所以「改 → 画 → 还原」都安全。

**方案 A 的正确写法**（M，未实测）：

```
hook 0x6BF9F0(model, ctx, pass, view):
  if model 不是本机玩家的武器 / 身体 或 pass ∉ {1,2,5}: return orig(...)
  for 每个 LOD 实例 inst（model+0x160 起，及 model+0x250 的每项的 [e+0x58]，去重）:
      备份 inst+0x30 的 n 项；pose[i] = pose[i] × T   （T 见 §6）
  orig(model, ctx, pass, view)
  还原
```

- 不要改 `node+0xB0`：同一帧的绘制不读它，下一轮更新又会被 `UpdateBone` 从局部矩阵重算覆盖；而且枪口 / 射击矩阵 / 挂点都读它（§4、§5）。
- 基础 LOD 的 `[lod+0x58]` 应当就是 `model+0xA0`（`0x6C2C10` 的另一分支明写 `this+0xA0`、`this+0xC8`），各 LOD 是否各有一个实例没核到（M）。
  稳妥做法是对 LOD 表里所有不同的 `[e+0x58]` 都换。
- 比 `0x6BF9F0` 更早能起作用的点：只有更新里的 `0x1100F10`（改了会进下一帧所有 pass，包括影子）；更晚的点：`0x11009A0`（没有 pass 参数）。
  所以 `0x6BF9F0` 是按 pass 区分的唯一合适位置。

---

## 3. 可见性记账、剔除、LOD（H 代码 / M 语义）

| 字段（`AnimationModel`，即 `umbra::Object` 包装） | 含义 |
|---|---|
| `+0x20` | Umbra 遮罩（bit26 远景、bit27/28/29 主视角三张表、bit30 阴影；setter 见 `view-distance-re.md` §2） |
| `+0x24..+0x40` | `0x11B28C0` 收集时写的距离量：`+0x24` 距离、`+0x28` 距离+半径、`+0x2C` max(距离−半径,0)、`+0x30` |z| 等；半径 `+0x64` |
| byte `+0x44` / `+0x45` | 剔除 / LOD 用第几个距离量 |
| `+0x60` | 剔除距离²（Umbra `setCullDistance`）；`d² > +0x60` 不进任何列表（`0x11E3F0D`） |
| `+0x68 / +0x6C` | 远景 pass 进入条件（`+0x68 > +0x6C`） |
| `+0x110` | Umbra 对象变换（`0x11B30C0` 写），视锥 / 遮挡剔除按它和模型包围盒 |
| `+0x160` / `+0x250..+0x260` | 基础 LOD / 其余 LOD（0xF0 一项，`+0xE8` = 进入该 LOD 的距离²） |
| byte `+0x4D0` / float `+0x4D4` | 本帧被颜色 pass 画过 / 画到时的最小 d² |
| byte `+0x4D1` / float `+0x4D8` | 更新 `0x6C06A0` 末尾把上两项搬过来（`0x6C07DD`），再把 `+0x4D0 = 0`、`+0x4D4 = FLT_MAX` |
| `+0x298` | 次级物理控制器：`0x6B6040(ctrl, 上帧画过, sqrt(+0x4D8))`：画过且距离 ≤ `ctrl+0x200` 才激活（`ctrl+0x204`），否则休眠 |

- `+0x4D0/+0x4D4` 只被 pass 1/2/5 置位，所以身体不在颜色 pass 里画时，`0x6B6040` 收到「没画过」→ 关掉裙摆 / 头发 / 链条之类的次级物理
  （Wing Diver SGO `animation_model` 里就有 `ragdoll_skirt*` 一串）。这就是 VR mod 必须写 `+0x4D0 = 1`、`+0x4D4 = 0` 的原因。写法：
  在跳过绘制的那次钩子里直接写（它们在下一次 `0x6C06A0` 才被读）。（H 读写 / M 「次级物理」语义）
- 骨骼本身的动画更新不看这两个值（士兵 `0x6BA950` 无条件跑），所以「身体不画 → 挂点不更新」应是次级物理 / 布娃娃代理，不是骨架。（M）
- **LOD**：视图模型的 LOD 由**真实**位置到镜头的距离决定（收集时算），离镜头 < 1 m → 基础 LOD；身体 LOD 阈值在 SGO（Wing Diver / Fencer
  `-lod1` 15、`-lod2` 30；Ranger / Air Raider 的玩家 SGO 没有 LOD）。（H 数据）
- **视锥剔除绕开**：武器真实位置在手上、第一人称镜头在头上，正常都在视锥内；仰头 / 低头极限时可能被剔（L）。真被剔时的两个办法：
  ① 在 `0x6C06A0` 之后把 `model+0x110` 写成视图模型的世界矩阵再调 `0x11B30C0`（只影响 Umbra 剔除 / 排序距离，不影响画面姿态）——
  但阴影视图也用它，武器影子可能跟着丢（L）；② 在 cmd 1 的 pass 1 之后（`0x11E2589` 附近）自己补一次 `model->vtbl[1]([sys+0x10], 1, sys+0xE0)`，
  前提是确认它这一帧没进列表（M）。剔除距离 `+0x60` 对近处无影响。

---

## 4. 武器怎么挂在手上、枪口在哪（H）

### 4.1 挂接

- `WeaponBase` 构造（`0x68A920`）读 SGO `ModelConstraint`（`0x68AEE9`）：第 0 项 = 拥有者骨骼名，拼上拥有者的后缀字符串
  （`[owner+0x58]`，`0x68AF70..0x68AF92`；Fencer 的 `arms` 因此变成左右手，M）→ `0x11001F0(拥有者骨架, 名字)` → **`weapon+0xE88`**（骨骼记录指针）；
  找不到报 `WeaponBase::WeaponBase(root node not found. :%ls)`。之后的项是 [拥有者节点, 武器节点] 对 → `weapon+0xE98`（个数 `+0xEA8`，步长 0x10）。
- 统计（Root.cpk `WEAPON/*.SGO`）：`arms_r` 1179、`arms_l` 224（手雷等副武器）、`arms` 105（Fencer）、`armsFlip` 43（盾）、`backWeapon` 48、
  `["arms",[chain…]]` 18（加特林）、`armsLowerFlip` 6，其余是载具武器。
- 每帧挂接 `0x68FD00(weapon)`（模型已注册 `0x11B2870` 时）：`weapon+0x60..+0x9F = [E88]+0xB0..+0xEF`；对每对节点抄世界矩阵；
  `0x1100B90(weapon+0xFD0, weapon+0x60)` 写武器根骨的世界矩阵并 `UpdateBone`。调用者：士兵动画回调 `0x597F6C`（`0x597F29` 前面先把士兵 `+0x60`
  写进 `weapon+0x150`）、士兵 slot 5 `0x59B498`、载具 `0x630441` / `0x633E5B`。
- 武器模型 = **内嵌**在 `weapon+0xF30` 的 `AnimationModel`，骨架实例 `weapon+0xFD0`（= `+0xF30+0xA0`），画面姿态 `[weapon+0x1000]`（`+0xFD0+0x30`）。
- 武器 slot 3（更新）→ `0x6934C0` → `0x6C06A0`：此时才把武器节点世界矩阵抄进画面姿态。
- 一帧里的顺序：渲染（用上一轮结果）→ 对象更新（士兵 `0x5717D0` 骨架 + 身体姿态 `0x6BA950`；士兵回调 / slot 5 挂枪 `0x68FD00`；武器 slot 3 抄姿态）
  → 镜头 `0xF86A0`。武器 slot 3 与士兵 slot 5 谁先没核（若武器先，武器画面姿态比手慢一帧，原版也一样，M）。

### 4.2 枪口

- 枪口来自武器 SGO `animation_model[2]`（MAB 块）的列表 0（构造 `0x68B62F..0x68BC97`，`pylib/vcobjects.py mab_muzzles`）：每条 = 名字、武器模型节点名、局部位置。
  枪口条目 `weapon+0x1D0`（步长 0xF0）：`+0` 节点记录指针、`+0x10` 局部矩阵、`+0x50` 射击矩阵（`fp-soldier-re.md` §2.1）。**读的是节点 `+0xB0`，不是画面姿态。**
- 实测（只读 Root.cpk）：

| 武器 SGO | 模型 | 骨骼 | 枪口（节点, 局部位置） |
|---|---|---|---|
| ASSULTRIFLE01 | `s_assault_AF14d.mdb` | `body` `magazine` | `body` (0, 0.089, 0.683) |
| AISOLDIERSNIPERRIFLE01 | `s_sniper_mmf01.mdb` | `body` `cock` `magazine` | `body` (0, 0.105, 1.112) |
| AISOLDIERSHOTGUN01 | `s_shotgun_buffalo01.mdb` | `body` `grip` | `body` (0, 0.087, 0.704) |
| AISOLDIERROCKETLAUNCHER01 | `s_rocket_stingray01.mdb` | （只有根） | 根 (0, 0.126, 0.672) |
| AILAZR01（Wing Diver） | `p_lazer_LAZR01.mdb` | （只有根） | 根 (0, 0.078, 0.663) |
| AIIZNR01（Wing Diver） | `p_thunder_izuna01.mdb` | `body` `arrow_*` `parts*` … | `body` (0, 0.094, 0.846) |
| AIHEAVYCANNON01（Fencer） | `h_cannon_normal01.mdb` | `body` `magazine` `nozzle` `slide` | `nozzle` (0, 0, 1.486) |
| HGATLING01（Fencer） | `h_gatling_normal01.mdb` | `body` `roll` `chain0/1` `chain_bp` | `roll` (0, 0, 0.956) |

  武器 MDB 里**没有**叫 muzzle 的骨骼；枪口全在 MAB。视图模型的枪口 = `局部位置 × 节点在视图模型里的姿态`（即 `pose_vm[节点]`）。

---

## 5. 枪口火光、弹壳的生成变换（H 代码 / M 语义）

- 火光：开火每发 `0x696FD0`（`0x697F85`）：`weapon+0x480`（SGO `MuzzleFlash` 名 → 类，`0x68DB89` 由 `0x1195C50` 查）非空时，先 `0x1194BF0(..., 射击位置 [rbp], 半径)`
  （L：离镜头太远就不生成），再 `weapon+0x4C0 = &muzzle+0x50`、`0x1194280(工厂, …, 类, &weapon+0x488)` 生成，随后 `+0x4C0` 清 0。
- `MuzzleFlashBase` 构造 `0x2BA520(this, param)`：`0x118AF20(param+0x30 = weapon, this)`（挂到武器下，M），**`this+0x120 = param+0x38 = &muzzle+0x50`**。
  10 个火光类（`MuzzleFlash01/02`、`LaserMuzzleFlash01–03`、`EfsMuzzleFlash`、`MuzzleFlashRailGun`、`PileBunkerMuzzleFlash01` …）都经它构造。
- `MuzzleFlash02` slot 3 `0x2C0D50`（更新）：把 `*(this+0x120)` 抄到 `this+0x60` 和 Umbra 节点（`0x11B30C0(this+0x130)`）；slot 5 `0x2C0DD0` 计数 `+0x220`，第 3 次后 `0x118A1B0` 删除。
  → 火光活 3 次更新，每次跟着 `muzzle+0x50`。`muzzle+0x50` 只在开火时由 `0x6969A0` 重建；子弹在 `0x696FD0` 内同步生成完，网络全量包
  （`0x694910`）也在它之前发（`fp-soldier-re.md` §2.3）。
- **把原版火光挪到视图模型枪口、子弹不变**：钩 `0x2BA520`，当 `param+0x30` 是本机玩家的武器时，把 `this+0x120` 换成插件自己的矩阵（每帧写视图模型枪口的世界矩阵）。
  时序注意：火光 slot 3 在对象更新里抄矩阵，早于镜头步进 → 用上一轮镜头会慢一帧；更准的做法是镜头步进之后（`0xF86A0` 返回后）直接
  `0x11B30C0(flash, &m)` 写它的 Umbra 节点（M）。视图模型缩小 0.1 倍时火光矩阵也要同比缩小，`0x11B30C0` 只替换长度近 0 的行、保留缩放（M），
  火光是否另走特效系统（`EfsMuzzleFlash`）未查（L）。
- 弹壳：`0x690660(weapon, i)`：抛壳定位点 `weapon+0x1F0`（个数 `+0x200`，步长 0x90：`+0` 节点记录、`+0x10` 局部矩阵）→ 世界 = 局部 × 节点 `+0xB0`，
  随机初速写 `weapon+0x550/+0x560`，`0x1194280(…, [weapon+0x4E0] 类 = SGO ShellCase 如 ShellCase_RB, …)`（`0x690B3F`）。调用：开火起点 `0x698500`（`weapon+0x600` 时）、
  `0x68FBA0`、`0x69047B`、`0x694830`。弹壳是世界里的刚体，生成后独立运动：第一人称下会从真实手位（离视图模型 ~0.3 m）弹出。
  要本地视觉副本：钩 `0x690660`，调用期间把定位点 `+0` 换成一个假记录（`+0xB0` 放视图模型里的抛壳点世界矩阵），返回后还原（M；
  尺寸是真实尺寸，缩小后的枪旁边会显大，L）。

---

## 6. 视图模型的变换和「只画手臂」

- 记 `R` = 参考系（士兵瞄准矩阵 `soldier+0x60`，或头骨 `head` 的画面姿态），`C` = 本帧渲染镜头世界矩阵（`CharacterGhostCamera +0x220..+0x25F`，
  渲染时就是上一轮步进的结果），`S` = 绕镜头的均匀缩放 s 再加偏移。`T = R⁻¹ × S × C`（行向量约定，`p' = p · M`），视图模型姿态 `pose_vm[i] = pose[i] × T`。
  身体手臂与武器用同一个 `T`，相对关系不变，手就握在枪上。（M）
- 均匀缩放会让调色板里的法线也缩 s 倍；G-buffer 着色器若不重新归一化，光照会变暗（L）。
- **只画手臂，不做新网格**：身体只有一个蒙皮对象 `polymesh`（3–5 个网格），可以在 pass 1/2/5 里把非手臂骨骼的画面姿态写成全零矩阵（顶点塌到原点 → 退化三角形，不出像素），
  手臂链（`kata_* → jowan* → kawan* → te_* → 手指`）写 `pose[i] × T`。混合权重跨肩膀的顶点会被拉向原点：
  Ranger 78461 顶点里 19768 个只受手臂骨骼影响、3156 个混合；Wing Diver 10676 / 878；Fencer 19750 / 246；Air Raider 21614 / 3492（H，脚本统计）。
  混合顶点集中在肩部，第一人称一般看不到；看得到就把 `kata_*` 也设成全零，从上臂截断（L）。pass 3 用原姿态 → 全身影子照常。
  这样身体也被 pass 1 画到，`+0x4D0/+0x4D4` 自然置位，不必手写。

---

## 7. 近裁剪面 `+0x28`（H 代码 / L 效果）

- 渲染镜头（renderer `+0x4D8` 起 4 个槽，`view-distance-re.md` §2.2）的 `+0x28` 只在构造 `0x118AFC0` 写 0.1；`0x118DFF0` **每帧**读它：
  `0x118E039` 投影 `0x71F70(..., near=+0x28, far=+0x2C)` → `cam+0x1A0`；`0x118E089` 近景 Umbra 视锥。所以写一次就持续生效，每帧写也无害；
  地图视角 `view.cpp ViewMapClip` 也会改它并在关闭时写回原值，两者要协调（谁最后写谁生效）。
- 阴影级联切分用常量表（§1.1），第一级从 0.1 m 起，**不随 `+0x28` 变**；远景镜头用 `+0x30/+0x34`，不受影响。
- 深度精度：远 / 近比从 1000/0.1（`ViewDistance` 3000 时 3×10⁴）变成 3×10⁵。若是常规 24 位非反转深度，1 km 处深度步长约放大 10 倍，
  远处地形 / 建筑会闪（`camera-re.md` §8.4 已在 6000/0.1 时观察到闪烁）。深度格式 / 是否反转 Z 没查（L）。
  建议：视图模型缩放 s 与近裁剪 n 一起选，使最近的视图模型顶点（手掌约在眼前 0.2–0.3 m × s）> n；先试 s = 0.25、n = 0.04。

---

## 8. 士兵骨骼（Root.cpk，玩家 SGO → MDB，H）

| 兵种（SGO 类） | 模型 | 骨骼数 | 武器挂点 |
|---|---|---|---|
| Ranger（`P505_RANGER.SGO`, AssultSoldier） | `P505_RANGER.MRAB / p505_ranger.mdb` | 83 | `arms_r`（#44，`te_r` 子，局部 (−0.104,−0.058,0.022)），`arms_l` #18 |
| Wing Diver（PaleWing） | `P506_WINGDIVER.MRAB / p506_wingDiver.mdb`（+ lod1 15 m、lod2 30 m） | 99 | `arms_r` #50（(−0.071,0.009,0.039)） |
| Fencer（HeavyArmor） | `P507_FENCER.MRAB / p507_fencer.mdb`（+ lod1/lod2） | 101 | `arms_l/r` #32/#60，`armsFlip_*`、`armsLowerFlip_*`、`backWeapon_*`(`Joint`)、`chain*`、`arm_ik_l/r` |
| Air Raider（Engineer） | `P508_AIRRADER.MRAB / p508_airRader.mdb` | 75 | `arms_r` #44 |

- 手臂链（四个兵种同名）：`mune` → `kata_l/r`（肩）→ `jowan0`（上臂，子 `jowan1`/`jowan2` 扭转）→ `kawan0`（前臂，子 `kawan1`/`kawan2` 扭转）→ `te`（手）
  → `arms`（挂点，kind 0 无顶点）、`fing0→ring0..2`、`fing1→little0..2`、`index0..2`、`middle0..2`、`thumb0..2`。头 `kubi → head`，瞄准 `globalSRT → aim_center → aim_target`。
- 全部顶点带 `BLENDINDICES/BLENDWEIGHT`（每顶点 2–4 个影响，索引是全局骨号），所以按骨号切手臂可行（§6 的统计）。
  Fencer 的 `kawan1/2_*` 是 kind 0、没有顶点（前臂不扭转）。
- 镜头相关：`human_aim_target_adjust_node = aim_center`；`game_object_camera_setting` 四档（Ranger 第 0 档眼 (0,1.8,−3.5)、看 (0,1.8,0)）。
- 武器端：步兵武器根骨直接等于 `arms_r` 的世界矩阵（无额外偏移，`soldier_weapon_grab_adjust` 只有 Fencer 非零 (−90,−35,0)，用途未查，L）。

---

## 9. 需要实机确认（L 项）

| 未知 | 怎么确认 |
|---|---|
| `[lod+0x58]` 与 `model+0xA0` 是否同一实例、各 LOD 是否各有一份 | 钩 `0x6BF9F0` 打印 `[model+0x1B8]`、`model+0xA0`、各 LOD 的 `[e+0x58]` |
| 身体是否 `isOccluder`（pass 0 有没有被调到） | 钩子按 pass 计数，第一人称下看身体收到哪些 pass |
| 改 `pose` 后视图模型确实出现在镜头前，影子留在原处 | 只对武器做 §2 的交换，s=1、`T = R⁻¹ × C`（不缩放）先看位置 |
| 缩放后法线 / 光照是否正常 | s = 0.25 时比较亮度 |
| 近裁剪 0.04 / 0.01 时远处是否闪 | 大地图、`ViewDistance` 3000，对比远山 |
| 极限仰俯时武器有无被 Umbra 剔除 | 观察闪断；必要时 §3 的两个绕开办法 |
| `0x1194BF0` 的含义、火光是否另走特效系统 | 钩 `0x2BA520` 打印 `this+0x120` 与枪口地址、`this` 的 RTTI |
| 跳过颜色 pass 后次级物理是否停（若采用「不画身体」而非 §6） | 不写 `+0x4D0` 时看 Wing Diver 裙摆 |

---

## 10. 实测（2026-10-12，`drive.py run range`，后台，Ranger，测试场）

`src/firstperson.cpp` 按 §2 的写法实现后进游戏核对（截图和日志在本机 worktree 的 `tmp/fp-run1..3/`，不入库）：

| 项 | 结果 |
|---|---|
| 钩 `AnimationModel` vtable `0x17C4030` 第 1 槽，能不能同时拦到身体（`soldier+0x860`）和武器（`weapon+0xF30`） | **能**：武器模型就是这个类（运行时检查武器 vtable 的第 1 槽，没有发现别的类），日志 `FP draws in 5 s by pass` 两者都有 |
| 每帧各 pass 调用次数（5 秒约 300 帧） | 身体与手里的武器：pass 0 / 1 / 3 各约 1 次一帧；pass 2、5 在测试场没出现（身体没有半透明网格、近处不走远景） |
| §2 方案（绘制钩子里换 `inst+0x30`，画完还原） | **成立**：手臂和武器出现在镜头前，原位置不再画身体；影子仍是整个身体（pass 3 原样） |
| §6「只画手臂」（非手臂骨骼塌到自己的关节） | 成立：看不到头、躯干；看得到双手与手套 |
| 缩放 0.5 + 近裁剪 0.03 | 视图模型完整、光照没有明显变暗；远景没有看到闪烁（只看了测试场） |
| 以瞄准坐标系为锚（`T = R⁻¹·Tr(hold−eye)·S·C`） | Ranger 步枪在右下角，但换成榴弹发射器后整套手臂和武器跑出画面（第三人称持握姿势不同）；**改为以手里武器的根骨为锚**（`fp_view.h` `AnchoredTransform`，Fencer 仍用瞄准坐标系） |
| 原版红色弹道线（榴弹发射器） | 从身体上的真实枪口画出，第一人称下像从脚下伸出 → `0x6888D9` 的 `0x6899F0` 调用改起点（§5 同理） |

## 11. 实测（2026-10-12 晚，四个兵种，`tests/autopilot/loadouts/*_fp.ini`）

| 发现 | 证据 | 处理（`src/firstperson.cpp`） |
|---|---|---|
| 镜头矩阵第 0 行朝**屏幕左** | 握把放在 +x 时枪画在左边 | ini 的「右」、动作层的 x / yaw / roll 全部取反后写入 |
| 以瞄准坐标系为锚，榴弹发射器、Fencer 的武器跑出画面 | 截图 | 每只手锚在自己武器的根骨（画面姿态第 0 骨）上；`weapon+0xE88` 挂点骨骼名以 `_l` 结尾为左手（`arms_l`、`armsFlip_l`），否则右手 |
| 非手臂骨骼塌到各自关节，肩部接缝画出黑色尖片 | 截图 | 塌到**眼睛**：混合顶点落在「手臂位置—眼睛」连线上，投影与手臂重合 |
| 换弹时整个玩家（含阴影）约 0.5 秒没画 | `draws in 5 s` 少 33 帧 | Umbra 包围盒是 `model+0x110`（`0x11005E0` 由骨骼算出，等于骨架实例 `+0x70`；`0x11B30C0` 不改写它）；第一人称时在渲染开始前（`0x1198FA4` 调 `0x11978D0` 处）给 Umbra 一个以眼睛为中心的 4 m 盒子，离开时交回真实盒子。只交给已注册的模型（`model+0x10` 非空，`0x11B2870`）：交了未注册武器的盒子时 `umbra_sandlot!Camera::resolveVisibility` 崩过一次 |
| Wing Diver 武器第一人称下只有约 1/5 帧进可见列表（第三人称每帧都画；关掉上面的放置也一样） | 两组计数对比 | pass 1 列表画完、收尾 `0x11E1590(system, 1)`（调用点 `0x11E2572`）之前，补交本帧 pass 1 没画到的视图模型（`0x11E1630(system, model, 1)`）。原因未查清（L：镜头在头里时 Umbra 遮挡查询把它判为被挡） |
| 提交 `0x11E1630`：`obj+0x18` 有批次就交给批次画，不调对象第 1 槽 | 反汇编 | 第一人称时手里武器的批次暂时摘掉（实测几把武器都没有批次，保留为防护） |
| 武器包围盒（半长，右 × 上 × 前） | `Debug=1` | 粘弹枪 0.28×0.17×0.46、Mag Blaster 0.03×0.12×0.50、Fencer 盾 0.47×0.32×0.70、加农炮 0.56×0.76×1.06、刺枪 0.69×0.30×1.17 → 粗或长的武器握把放远（`FirstPersonFit`） |
| 测试场里 UFO 构造崩溃（`EDF+0x6DC844`，`UfoSmall515` 的 `material_animation` 为空却 `Play(0)`） | 转储 | 游戏本体缺陷，与插件无关 |

## 12. 近战与盾牌的第一人称旋转（2026-10-15）

电刃刀五式（武器表 737 / `hWeapon046`）属于 `Weapon_Swing`，不是可按枪口轴归正的枪械。每帧以武器根完整逆矩阵锚定，会抵消它相对瞄准系的持握角和挥击转角；放大或改某一把刀的偏移不能修复这个契约问题。

`fpview::NativeGripTransform` 使用角色瞄准系的逆矩阵，将武器根的位置放到配置握把点，保留它相对瞄准系的旋转。刀/锤/刺枪/盾的原生类通过 RTTI 判断，描述符及其 RVA 必须在已验证的 EDF 映像内。火器仍用 `AnchoredTransform`。变换仅用于第一人称颜色绘制，原生武器世界矩阵、攻击判定和网络数据仍不改。该方案保留旋转，根平移仍由第一人称握把定位，并非完整复制第三人称武器轨迹。

根描述符静态核对（`tools/edfre.py vtable_of`）：`Weapon_Swing 0x17E5950`、`Weapon_ImpactHammer 0x17E42A0`、`Weapon_PileBanker 0x17E4A80`、`Weapon_Shield 0x17E51A0`。运行时按类描述符识别，不使用这四个地址给单把武器硬套姿态。

## 13. 颜色网格拓扑与激光附件（2026-10-16）

之前「把身体骨骼塌到眼点」的方案已由拓扑筛选取代。`fp_skin.h` 保留的面必须满足：三个顶点所有非零权重都属于同一只可见手臂。这样跨躯干/肩部、跨双手的面没有机会在冲刺或双持时连成大三角形；不会用 NaN、近裁剪或另一个塌陷位置遮掩。

颜色绘制的网格提交调用点 `0x1103FD1 -> 0x10FF4E0`（H）重定向后，仅本机身体颜色范围使用原生准备 `0x10FFAC0`、材质通道绑定 `0x1151330`、脏状态刷新 `0x11597E0`，将最后的 `DrawIndexed` 交给过滤索引提交器。其它模型和阴影继续调用原 helper。不改 D3D COM 公共虚表。原 helper 的前导 `0x40` 指令前缀必须包含在签名内。

原生蒙皮调色板在 VS 常量槽 1；`inst+0x68` 的持有对象首指针就是它的 GPU buffer。每个网格通道按实际绑定 buffer 找回 LOD 实例、读取骨名，所以不要求所有 LOD 骨序相同（飞翼潜行兵确实不同）。60/64 字节声明的权重为末尾前 20 字节 float4、骨索引为末尾 4 字节 ubyte4。首次遇到不可变 VB/IB 时限量回读、建立过滤 IB，缓存最多 32 个条目、每个源缓冲最多 16 MiB；离开第一人称时释放。GPU 绑定提交后恢复，原 GPU 数据不覆写。

激光：主对象虚表 `0x17E2458`；武器在主对象 `+8`。其 umbra 子对象偏移 `+0x10`，虚表 `0x17E2480` slot 1 为 `0x6882A0`（H）。该 draw 读取子对象 `+0x120` 起点、`+0x130` 终点；世界射线由 `0x6890A0` 处理，`0x688970` 将其结果整理到绘制字段。`LaserDrawHook` 只在所属武器等于本机在手武器时临时变换起点并恢复，世界碰撞结果保持原样。


## 14. 通用载具观察输入事务（2026-10-16）

`vehicle_look.cpp` 在 `crew.cpp InputHook` 入口创建栈上事务，覆盖原生输入与本插件控制器的整个调用期；只对本机 `PlayerHuman` 当前所属载具和实际占用席位生效，身份含载具／人物 weak 控制块与座位号。座位的 `+2D0/+2D4` 是鼠标／右摇杆的本帧份额；按住观察或回正时缓存后清零，退出遍历还原原 8 字节。`GameFrame` 防重复积算，状态在 SRW 锁下发布；HUD 仅读游戏线程的快照。没有相机钩子不接管，高视角保留 turretcam 的既有观察。

第一人称 `Ride` 在 `vc_eye_` 正交帧上叠局部偏航／俯仰，保留眼位与机体滚转；第三人称在原生最终帧上叠加，并记下 before/written，仅撤回自己仍然持有的写入，避免原生分支不写矩阵时每帧累积。瞄具 `Up` 在观察期间为 false，恢复时沿用原倍率；turretcam 在此期间交还操纵，不把观察射线变为炮塔目标。

实机尼克斯从偏航 -0.008 到 -1.012 rad，武器轴一直为 `(1.222,0.050)`；Barga 605 对应轴为 `(0,0)`。原生民用车输入 `0x65A548` 从 seat+2E0 读油门、`0x65A551` 的按钮 0x10 为倒车，`0x65A56D` 起读 +2C0 横向转向；正常驾驶不是右摇杆。该路径保留于轿车，测试必须用 LT 与左摇杆横向。


## 15. 实体仪表语言与整车状态图（2026-10-16）

`Readings.language` 使用 `hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage())`，顺序 en／zh-CN／zh-TW／ja。`l_language_gate` 选择语言网格，gate 0 无条件，其他值为 field+1 的可用性条件；`v_field` 隐藏不可用栏底；`p_field_place_segment` 从 share 生成 0–100 的百分比，保留 `d_` 对旧资源的原始读数语义；`c_field_level` 选择三种整体耐久颜色。当前席位数据不扩散成其他席位的数值。全部操作只发生在模型绘制姿态，随后还原。

生成器在原模型三角形上做俯视／机甲正面投影，保留长宽比例，得到该模型自己的轮廓。语言字形按屏幕合并并共享控制骨骼；乘客不重复驾驶位的大数字和燃料栏，所有原有多席载具继续满足 ubyte 骨骼索引上限。`snd_BRDF_Common_Light` 的 light_color 是独立的发光项，固定色仪表必须设置相应线性 RGB，不能一律白色。

视觉统一：模型投影栅格为 96×144，合并内部与轮廓的水平连续段。静态内部使用低亮度仪表色，三种耐久状态只点亮轮廓；正常轮廓与字符共用同一绿色强度，未知时保留中性轮廓。相比原来的暗色实心剪影加亮白数字，图形与读数具有一致的显示语言。百分号与数值底部对齐并紧邻，内容留在标题线下方；仅调整几何与材质，不增加控制骨骼。


数据语义：旧 `ScreenFeed.hp` 在 armor gauge 存在时是乘员护甲，`hull` 才是旧仪表的车辆项。新缩略图保留独立 `vehicleHp/vehicleHpMax`，来源为当前 StockHud／PlayerJet／Heli 的载具数据，在乘员护甲覆盖前保存。物理 HP／percent 控件使用该独立读数；仅有护甲快照时不推造车况。`hud_view --check-feed` 用不同乘员／车辆耐久及飞机快照跑实际 FeedOf 和 PublishInstruments，验证乘员受伤、车辆受伤和缺少载具来源的分支。

## 16. 机甲座舱与仪表设备的视觉统一（2026-10-16）

机甲的屏框、仪表台、窗沿及头顶开关原先采用 KC-135／C-141C 照片裁片，在转动视角后仍是平面照片的透视与光照，与真实控制台几何及实时仪表割裂。`cockpit_kits_mech` 现使用立体屏框、内沿、后壳、固定螺钉、下排按键和通向窗沿的实体安装臂；窗沿／侧壁／头顶使用模型旋钮和实体刻线。读数仍在原 `Screen` 坐标系内，后壳在读数后方，屏框开口与显示区域匹配。

所有新增外壳使用同一组非自发光石墨色材料，随场景照明；原座椅／控制台资产也映射到相同色系，保留适量红／琥珀色开关罩。原有五套机甲空间适配和席位选择沿用，控制骨骼数量不增加。此前照片素材可仍在外部素材库中，本套件不再引用其材质；此变化需安装器重新生成座舱模型。
