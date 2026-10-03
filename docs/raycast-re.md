# 射线检测（世界几何）：逆向笔记

EDF.dll TimeDateStamp `0x678CCB46`（与 `re-notes.md` 同一版本），下文地址均为 RVA。全部来自静态分析，**游戏未运行、未实测**。
置信度：H = 直接读代码得出；M = 强推断；L = 需要实机核对的猜测。
分析脚本在 `C:\Users\wrds\.claude\jobs\cfb4e99e\tmp\raycast\pe.py`（字符串 / RTTI / rip 相对引用 / E8 调用 / pdata 函数边界扫描）。

## 0. 结论

可以从插件直接调用。推荐入口是游戏引擎（`xgs::havok`）对 hknp 的射线封装：

```
void CastRay(void* havokWorldWrapper, hknpClosestHitCollector* collector, const EdfRayInput* in);   // RVA 0x11A7EE0
```

- `havokWorldWrapper = *(uint8_t**)(image + 0x20B2958) + 0x10`
- `collector` 用游戏自带的 `hknpClosestHitCollector`（vtable `image + 0x1768B78`，大小 `0xA0`）
- `in` 是起点、终点加碰撞过滤信息。用 **layer 22**（`filterInfo = 0x16`），它只与地图 / 建筑层（15–20）以及 26 层碰撞，不与角色、载具层碰撞。游戏自己的地面探测、直升机离地检测都用这一层。

调用后：`collector+0x0C != 0` 表示命中；命中点在 `+0x30`，法线在 `+0x40`，命中比例在 `+0x50`。

物理引擎是 **hknp（Havok Physics 新版）**。DLL 里也链接了旧版 hkp（`hkpWorld::castRay`），但那条路是死代码，**不要用**，见第 6 节。

## 1. 物理世界在哪里（H）

| 项 | 值 | 证据 |
|---|---|---|
| 全局指针 | `*(image + 0x20B2958)`，下文记作 `g` | `internal_GetGroundPosition 0x208860`：`0x2088AA mov rsi,[rip→0x20B2958]`，`0x2089BC lea rcx,[rsi+0x10]`，然后 `call 0x11A7EE0`。`0x214C80`、`0x235AE0`、`0x1082E0` 用的也是同一个全局。 |
| 射线封装的 this | `g + 0x10` | 同上 |
| 世界持有者 | `*(g + 0x68)`，也就是 `*(wrapper + 0x58)` | `0x11A7F0E mov rcx,[rcx+0x58]; add rcx,0x20`，然后 `call [vtbl+0x1B0]`；`0x2088B1 mov rcx,[rsi+0x68]` 被传给锁函数 |
| hknpWorld | `*(g+0x68) + 0x20`。它的 vtable 第 54 槽（`+0x1B0`）是 `hknpWorld::castRay 0xE4D160`（内含 `"TtWorldCastRay"` 计时字符串） | hknpWorld vtable `0x18BC9C0`（RTTI `.?AVhknpWorld@@`）。第 47–57 槽依次是 QueryAabb×4 / CastAabb×2 / **CastRay** / CastShape / PointQuery / GetClosestPoints |

插件侧的判空：`g != nullptr && *(void**)(g + 0x68) != nullptr` 时才调用（M）。游戏代码在这里从不判空，只在任务中使用。菜单或读盘期间 `g` 是否有效没有静态证据（L），所以必须判空，并且只在任务帧的 hook 里调用。

直升机对象自己也缓存了这个封装指针：`veh + 0x1C10`（HeliBase 第 57 槽 `0x651CF6 mov rcx,[rdi+0x1C10]`，此处 `rdi` 就是 veh）。这是 HelicopterBase 专有的字段，按全局取更通用，不推荐依赖它（M）。

## 2. `CastRay 0x11A7EE0`（H）

调用约定是 MS x64：`rcx = wrapper`，`rdx = collector*`，`r8 = const EdfRayInput*`，无返回值（结果全部写进 collector）。函数内部：

- `0x11A7EFD`–`0x11A7F96`：读 `in+0x00`（起点）和 `in+0x10`（终点），算出 `dir = to - from`，并把 `dir.w` 强制设为 `1.0`（`unpckhps/shufps 0xC4`，常量 `0x1F82840 = {1,1,1,1}`）。这个 w 就是 hkcdRay 的最大比例，所以**命中比例的范围是 [0,1]，按线段长度计算**。随后用 `rcpps` + 牛顿迭代求出 `invDir`。
- `in+0x20` → query `+0x14`（collisionFilterInfo）；query `+0x10` = `0xFFFF`（材质 id），`+0x18` = 0，`+0x20` = 1，`+0x22` = `0xFB`。
- `in+0x24` → query `+0x60`。所有游戏调用点都传 0。
- query `+0x00/+0x08` 来自 world vtable 的 `+0x18/+0x10`（filter / codec 指针），由函数自己填写。
- `0x11A7FD6 call [rax+0x1B0]` 调用 `hknpWorld::castRay(query, collector)` → `world+0x4C0` 上的 dispatcher `+0xB0`。

### 2.1 入参结构 `EdfRayInput`（大小 0x28，按 0x30 对齐分配）

| 偏移 | 类型 | 含义 | 证据 |
|---|---|---|---|
| `+0x00` | float[4] | 起点（xyz，w 设为 1.0，与游戏写法一致） | `0x11A7EFD movups xmm5,[r8]` |
| `+0x10` | float[4] | 终点（xyz，w 不使用） | `0x11A7F04 movups xmm2,[r8+0x10]` |
| `+0x20` | u32 | collisionFilterInfo，填 `0x16`（layer 22） | `0x11A7F1E`；`0x235AE0` 中 `[rsp+0x40] = 第 5 参` |
| `+0x24` | u32 | 未知，填 0 | `0x11A7FC6`；`0x235B0D mov [rsp+0x44],0` |

### 2.2 输出 `hknpClosestHitCollector`（大小 0xA0，16 字节对齐）

vtable 在 `0x1768B78`（RTTI `.?AVhknpClosestHitCollector@@`），各槽：0 `0x978880`，1 析构 `0xF6260`（delete 大小 `0xA0`），2 reset `0xFDF00`，3 getHits `0xFDDB0`（返回 `this+0x30`），4 addHit `0xD93980`，5 空函数 `0xFDEF0`。

addHit `0xD93980`：若 `hit.fraction < this->earlyOut`，就把 `0x70` 字节的 hknpCollisionResult 拷到 `+0x30`，再置 `numHits = 1` 和 `earlyOut = fraction`。所以返回的是**最近的命中**（H）。

| 偏移 | 类型 | 含义 | 置信度 / 证据 |
|---|---|---|---|
| `+0x00` | ptr | vtable = `image+0x1768B78` | H |
| `+0x08` | u8 | hints，填 0 | H（`0x20DEAF` 构造时清零；reset 不写这个字段） |
| `+0x0C` | i32 | **命中数（0 表示没命中）** | H：游戏全都写 `cmp dword [col+0xC],0`，见 `0x208AB7`、`0x654706`、`0x651D02` |
| `+0x10` | float[4] | earlyOut（reset 为 `0x5F7FFFF0`，约 1.8e19） | H |
| `+0x20` | u8 | 0 | H |
| `+0x30` | float[4] | **命中点（世界坐标）** | H：GetGroundPosition 在 `0x208AC8` 直接取它作为地面点 |
| `+0x40` | float[4] | **命中面法线** | H（字段位置）/ M（已归一化）：直升机 `0x654720` 把它作为法线输出 |
| `+0x50` | float | **命中比例 ∈ [0,1]**，未命中时为 `3.4e38` | H（字段）/ M（含义）：命中点 = 起点 + (终点 − 起点) × 比例，见 `0x214D1D`；直升机向下 5 m，比例 ≤ 0.04（0.2 m）即判为着地，见 `0x651D31`–`0x651D4A` |
| `+0x58..+0x77` | — | 发出查询的 body 信息（射线查询时为无效值） | H |
| `+0x78` | u32 | **命中 body id**（24 位，`0xFFFFFF` 表示无效） | H：`0x67DC45 mov edx,[result+0x48]` → `0x11AD2E0` 查 body |
| `+0x7C` | u32 | 命中 shapeKey | H（`0x11A9BF0`） |
| `+0x88` | u16 | 命中材质 id（`0xFFFF` 表示无） | H（`0x11A9B6F`） |
| `+0x8C` | u32 | 命中 shape 的 tag / filter | M（`0x11A9BEA`） |
| `+0x9C` | u32 | 标志。`NpObjectCreatePosCollector` 只接受 `& 2` 的命中 | H（字段）/ L（含义） |

可选：`0x1082E0(const void* result /* = collector+0x30 */)` 返回命中面的地表类型（u8），内部用 `g+0x10` 查材质表。直升机命中后会调用它（`0x654711`），避障用不到。

## 3. 碰撞层（H，数据来自 `CollisionFilter` 构造函数 `0x105510`）

`0x106100` 等是按层生成 filterInfo 的小函数（`0xD929F0`：`((((sub<<5)|dontCollide)<<5)|group)<<5 | layer`）。layer 22、其余字段为 0 时，`filterInfo = 0x16`。

构造函数先禁用 1–29 层之间的所有碰撞（`0xD92A90`），再用 `0xD92E50(a, b)` 逐对开启，一共 141 对。与 **layer 22** 碰撞的层：`{15, 16, 17, 18, 19, 20, 26}`。

- 15/16/17/18/20 层只在地图对象 / 建筑的创建代码里使用：`Preload_Structure 0x157580`、`Preload_Model 0x1555A0`、`Preload_AnimationModel 0x14EC50`、`MapObject_AnyObject 0x145060`、`MapObject_FixedCrashModel 0x13E260`（18）、`BuildingObject 0x7C7C20`（15/17）。所以射线**会打到建筑**（H）。
- 地形：GetGroundPosition 用 layer 22 找地面放置对象，直升机用 layer 22 做 5 m 离地检测（第 57 槽 `0x6519A0`）。所以**地形也会被打到**（H）。地形具体在哪一层没有单独确认（19 或 26，L）。
- 角色、载具所在的层（1–14 一带）与 22 不碰撞。所以这条射线**不会打到自己的直升机、友军或普通敌人**（M）。代价是巨大敌人、敌方载具同样检测不到。要躲这些，得另用 `layer 25`（与 1,2,3,6,7 碰撞）再查一次，这部分没有展开分析（L）。

layer 22 的使用者有 66 处（`calls(0x106100)`），包括 `0x6519A0 / 0x6545D0 / 0x6563B0`（直升机）、`0x62DE60`（载具）、`0x208860`（GetGroundPosition）、`0x1D7490`（`MissionContext::internal_CastRayCreatePosition`）。它就是游戏的“打地图”专用层。

## 4. 线程与时机（H / M）

- 锁：GetGroundPosition 在射线前后调用的 `0xDAC5A0` / `0xDB04A0`（hknpWorld 读锁 / 解锁）在 release 版里都是 `ret 0`。**不需要加锁**（H）。
- 直升机每帧的顺序（见 `heli-input-re.md` §1）：第 60 槽 `0x652630` 依次调用第 55 槽（输入）→ 第 57 槽（物理和武器）。**第 57 槽 `0x6519A0` 自己就在 `0x651CFD` 调用 `0x11A7EE0`**（向下 5 m 的着地检测）。所以在第 55 槽的后置 hook 里调用，和游戏自己的射线处于同一调用链、同一线程（H）。物理 step 不会和这段代码并发，因为游戏本身就这么用（M）。
- 别在渲染回调、网络线程或 DLL 加载期调用。每帧射线的数量由插件自己控制（hknp 射线开销小，每架直升机每帧几条没有问题，M）。

## 5. C++ 调用草图

```cpp
#include <cstdint>
#include <cstring>

namespace rva {
constexpr uintptr_t kHavokWrapperGlobal = 0x20B2958;   // *(image+X) = g; wrapper = g+0x10
constexpr uintptr_t kCastRay            = 0x11A7EE0;   // void(wrapper, collector*, const EdfRayInput*)
constexpr uintptr_t kClosestHitVtbl     = 0x1768B78;   // hknpClosestHitCollector vftable
constexpr uintptr_t kClosestHitReset    = 0xFDF00;     // vtable slot 2
}

struct alignas(16) EdfRayInput {          // 0x30
    float    from[4];                     // +0x00  w = 1
    float    to[4];                       // +0x10  w = 1
    uint32_t filterInfo;                  // +0x20  0x16 = layer 22 (map/terrain/buildings)
    uint32_t unk24;                       // +0x24  0
    uint64_t pad28;
};
static_assert(sizeof(EdfRayInput) == 0x30);

struct alignas(16) HknpClosestHitCollector {   // 0xA0
    void*    vtbl;            // +0x00
    uint8_t  hints;           // +0x08
    uint8_t  _p09[3];
    int32_t  numHits;         // +0x0C  !=0 -> hit
    float    earlyOut[4];     // +0x10
    uint8_t  _p20[0x10];      // +0x20
    float    hitPos[4];       // +0x30
    float    hitNormal[4];    // +0x40
    float    fraction;        // +0x50  [0,1] along from->to
    uint8_t  _p54[0x24];      // +0x54  (query body info @+0x58)
    uint32_t hitBodyId;       // +0x78  0xFFFFFF = none
    uint32_t hitShapeKey;     // +0x7C
    uint8_t  _p80[8];         // +0x80
    uint16_t hitMaterialId;   // +0x88
    uint8_t  _p8A[0x16];      // +0x8A .. +0x9F
};
static_assert(sizeof(HknpClosestHitCollector) == 0xA0);

using CastRayFn  = void (*)(void* wrapper, void* collector, const EdfRayInput* in);
using ResetFn    = void (*)(void* collector);

struct RayHit { float pos[3]; float normal[3]; float fraction; uint32_t bodyId; };

// Call only from a mission frame (e.g. heli slot-55 post hook).
inline bool CastRayMap(uintptr_t image, const float a[3], const float b[3], RayHit* out) {
    auto* g = *reinterpret_cast<uint8_t**>(image + rva::kHavokWrapperGlobal);
    if (!g || !*reinterpret_cast<void**>(g + 0x68)) return false;   // no physics world

    EdfRayInput in{};
    in.from[0] = a[0]; in.from[1] = a[1]; in.from[2] = a[2]; in.from[3] = 1.0f;
    in.to[0]   = b[0]; in.to[1]   = b[1]; in.to[2]   = b[2]; in.to[3]   = 1.0f;
    in.filterInfo = 0x16;

    HknpClosestHitCollector col;
    std::memset(&col, 0, sizeof(col));
    col.vtbl = reinterpret_cast<void*>(image + rva::kClosestHitVtbl);
    reinterpret_cast<ResetFn>(image + rva::kClosestHitReset)(&col);   // same defaults as game ctor

    reinterpret_cast<CastRayFn>(image + rva::kCastRay)(g + 0x10, &col, &in);
    if (col.numHits == 0) return false;
    if (out) {
        std::memcpy(out->pos, col.hitPos, 12);
        std::memcpy(out->normal, col.hitNormal, 12);
        out->fraction = col.fraction;
        out->bodyId = col.hitBodyId;
    }
    return true;
}
// collector 是栈上对象，不调用析构（游戏自己也不调用，见 0x6545D0 / 0x208860）。
```

避障用法建议：沿速度方向 + 左右 ±30° 各打一条，长度取 `速度 × 前瞻时间`，再向下打一条测离地高度。命中距离 = `fraction × |b − a|`。

### 5.1 启动期签名校验（16 字节，在本版本 `.text` 中各自唯一，H）

| RVA | 16 字节 | 说明 |
|---|---|---|
| `0x11A7EE0` CastRay | `40 53 56 57 48 81 EC A0 00 00 00 48 8B 05 66 71` | 末尾 `66 71` 是 `__security_cookie` 的 rip 位移，换版本必变，正好用作版本门 |
| `0xFDF00` collector reset | `33 D2 B8 FF FF 00 00 89 51 0C 0F 28 05 1F 4B E8` | |
| `0xD93980` addHit（只校验，不调用） | `F3 0F 10 4A 20 0F 10 41 10 0F C6 C9 00 0F 2E C1` | |

另外做两项数据校验：`*(uint64_t*)(image+0x1768B78) == image+0x978880`，以及 `*(uint64_t*)(image+0x1768B78+0x20) == image+0xD93980`（vtable 第 0 / 4 槽）。任意一项不匹配就禁用避障，直升机照常飞。

## 6. 排除掉的候选（避免走弯路）

- **hkp（旧版 Havok）`hkpWorld::castRay 0xA954F0`**（`"TtworldCastRay"`）：3 个调用点 `0x97101`、`0xA1E29`、`0x3220FA` 都是 `xor ecx,ecx` 后直接调用，也就是 `this = nullptr`。函数会解引用 `[this+0xC0]`、`[this+0x88]`，所以这些调用点是 hkp 世界被编译期折叠成空指针后留下的**死代码**。`0xA1DC0` 看起来像个漂亮的 `bool(outPt, hkpWorldRayCastOutput*, from, to, filter)` 封装，但同样不能用（H）。
- **`0x235AE0`**：`void(?, collector*, const Vector* from, const Vector* to, u32 filterInfo)`，等价于“用全局 + 0x10 调 0x11A7EE0”的 5 行 thunk。它在整个模块里**没有任何引用**（rip 引用、指针表都扫过），属于孤立代码，功能上可用，但没必要依赖它。直接调 `0x11A7EE0`（56 个调用点）更稳（H）。
- **`0x214C80`**：`bool(?, Vector* outPos, Vector* outNormal, const Vector* from, const Vector* to, float scale)`，用 `NpObjectCreatePosCollector` 过滤（只收 `flags & 2` 的命中）。同样**零引用**，而且过滤语义不明，不推荐（H / L）。
- **`internal_GetGroundPosition 0x208860`**：`bool(const Vector* pos, float bottomY /*xmm1*/, Vector* out)`。第一次从 `pos.y+1` 向下打到 `bottomY`；命中后再从 `命中点.y − 0.01` 打到 `y = −1000`，用的也是 `flags & 2` 过滤。结果的语义（“最下层地面”）不适合测离地高度，不推荐（M）。
- 直升机自带的 `0x6545D0`：`float(void** wrapperHolder, const Vector* origin, Vector* outNormal, u32* outSurface, float len)`，向下打 `len` 米，返回命中比例，未命中返回 −1。它没有直接调用者，只能向下打，作为参考即可。
- 形状扫掠：`xgs` 封装里还有 CastShape（`0x11A9270` / `0x11A70B0` → world `+0x1B8`）和 GetClosestPoints（`0x11A9970` → world `+0x1C8`，直升机 `0x651B45` 用它做机体包围体检测）。可以用来做“胖射线”避障，但入参（shape 指针、变换矩阵）没有展开分析（L）。

## 7. 还需要实机确认的

1. 任务中 `g` 和 `*(g+0x68)` 非空；回到菜单后是否置空（L）。
2. 对一栋已知建筑打水平射线，确认命中点、法线和比例的含义（比例应在 [0,1]，M）。
3. 26 层是什么（地形还是别的），以及 `+0x9C & 2` 的含义（L）。
4. 已经倒塌的建筑是否还会挡射线（预期不会，因为 body 已移除，L）。
