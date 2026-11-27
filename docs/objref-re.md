# 游戏对象的引用与存活判定（逆向笔记）

插件里所有「记住一个游戏对象、之后再看它还在不在」的代码都走 `src/objref.h`：`ObjRef`（身份值）、`WeakRef`（持有 weak count 的引用）、`objref::Exists` / `objref::Alive`（唯一的存活判定）。本文是这些判定条件的依据。地址都是 Steam 版 `EDF.dll`（TimeDateStamp `0x678CCB46`）的 RVA，H = 汇编直接可见，M = 由多处汇编推出，L = 推测。

背景：设计审查 `docs/reviews/design-review-1126.md` P1 / 第 2 步。之前插件里有 20 多份各写各的 `Alive` / `Live`，有的不查死亡字节、有的不查删除标志、有的不查 use count，跨帧留着的 `ObjRef` 还会去读可能已经释放的控制块。

## 1. 控制块：MSVC `_Ref_count_base`

- 对象 `+0x28` / `+0x30` 是对象自己的 weak-this（`enable_shared_from_this`）：`{对象指针, 控制块}`。(H：Delete 0x118A1F7 / 0x118A220..0x118A22D 把 `+0x28/+0x30` 当一对 shared_ptr 交出去)
- 控制块 `+8` use count、`+0xC` weak count，vtable `[0]` `_Destroy`（析构对象）、`[1]` `_Delete_this`（释放控制块）。(H)
  - 从 weak 升级成 shared（`weak_ptr::lock`）：读 `+8`，为 0 就放弃，否则 `lock cmpxchg` 加 1（0x118A200..0x118A21C）。游戏自己判断「weak 指向的对象还在」只看 use count。(H)
  - 放掉一个 shared：`lock xadd [+8],-1`，原值为 1 时调 `[0]`，再 `lock xadd [+0xC],-1`，原值为 1 时调 `[1]`（0x118A24A..0x118A270）。也就是说所有 use 合起来占 1 个 weak，最后一个 weak 走时控制块才释放。(H)
- 推论：只要插件自己持有 1 个 weak count，控制块就不会被释放，`+8` 一定读得到，并且一定还是这个对象的块；use count 为 0 时对象已经 `_Destroy`，对象本身的存储是否随控制块一起保留（`make_shared` 合块）插件不依赖（L），所以 use count 为 0 之后不再读对象。(M)
- 插件不持有计数、只记 `{对象, 控制块}` 时，对象被释放后控制块的那块内存可能被堆复用，读到的 `+8` 是任意值。这就是跨帧一律用 `WeakRef` 的原因。

## 2. 存活判定的条件（按 `objref::Identity` 的顺序）

| 条件 | 含义 | 依据 |
| --- | --- | --- |
| 控制块可读且 `+8 > 0` | 对象没被析构 | §1，0x118A200 (H) |
| 对象 `+0x30` 等于记下的控制块 | 这个地址上还是原来那个对象（同地址的新对象控制块不同） | weak-this，§1 (H)；新对象的块是新分配的 (M) |
| 对象 `+0x18` bit 2 未置 | 没被 Delete | 见下 (H) |
| 对象 `+0x2E8` 为 0 | 没死（只对有耐久的 GameObject） | 见下 (H) |

- **删除标志 `+0x18 & 4`**：Delete `0x118A1B0` 开头 `test byte [rcx+0x18],4` 已置就直接返回（0x118A1C2），否则调 vtable `+0x40`（移除回调）、清 `+0x1A`、把 weak-this 交给管理器注销（`0x1195E80`），然后 `or byte [rbx+0x18],4`（0x118A273）。管理器之后才放掉它的 shared_ptr（下一次清理），所以 Delete 之后、析构之前的这段时间 use count 仍 > 0：只看 use count 会把已删除的对象当成活的。(H)
- **死亡字节 `+0x2E8`**：ApplyDamage `0x547C30` 在 hp（`+0x2F8`）≤ 0 时 `+0x2E9 = (原 +0x2E8 == 0)`、`+0x2E8 = 1`（0x548419..0x548429）；原版遍历敌对对象 `0x5E0F20` 用 `cmp byte [rdx+0x2E8],0` 跳过死的（0x5E0FB6）。死亡后对象仍在场景里（坠毁、倒地动画），要等它自己 Delete。(H)
  - 一些部件类自己写 `+0x2E8`、不经 ApplyDamage（`docs/damage-stats-re.md`），语义相同。(M)
  - 纯场景对象（DemoIndirectFire 的 `+0x2E8` 是开火音效标志，`docs/carrier-laser-re.md`；MuzzleFlash、远景物体等）不是这个类族，`+0x2E8` 不是死亡字节，所以它们只问 `objref::Exists`。(H)
- 判定顺序：先控制块、再对象。use count 为 0 时不读对象（§1）。

## 3. 插件里的约定

- `objref::Exists(ref)`：前三条。任何场景对象都可以问。
- `objref::Alive(ref)`：四条全过。只对士兵、载具、敌人、建筑等 GameObject 问。
- `objref::Dead(ref)`：存在但死了（击毁的载具还在场景里）。
- `WeakRef`：构造 / 赋值时 `Identity` 为 present / removed 才拿 1 个 weak，否则存空；复制加 1，覆盖、`Reset()`、析构减 1。模块的跨帧状态（全局数组、`std::vector`、`unordered_map` 的值）用它，`x={}` / `x=Entry{}` / `erase` 都会自动还计数。
  - 含 `WeakRef` 的类型不能在带 `__try` 的函数里做局部变量或临时值（C2712）；这类函数改成在外层建好、或改成直接给成员赋值。
  - 插件退出时（DLL 卸载的静态析构）还剩的 weak 也会还掉：此时 `EDF.dll` 仍加载（插件后加载、先卸载），控制块的 `_Delete_this` 和游戏的堆都还在。(M)
- `objref::Retain` / `objref::Release`：只给和游戏交接的那一个计数用（例如插件把 weak_ptr 写进游戏消息的输出参数，或游戏交给插件一个带计数的 weak_ptr）。插件自己持有一律用 `WeakRef`。
- 对象指针取自同一帧的游戏遍历（队伍集合、座位、消息参数）时，直接 `ObjRef::Of(o)` 同帧使用，不需要持有。
