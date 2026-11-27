// 游戏对象引用与存活判定：整个插件只有这一份（docs/objref-re.md；设计审查 docs/reviews/design-review-1126.md 第 2 步）。
//
// 游戏对象由 MSVC shared_ptr 管：对象 +0x28 / +0x30 是它自己的 weak-this {对象, 控制块}，控制块是 _Ref_count_base
// （vtable [0] _Destroy、[1] _Delete_this，+8 use count，+0xC weak count；所有 use 合起来另占 1 个 weak）。
//   - ObjRef：{对象, 控制块} 的身份值，不持有任何计数，可平凡复制（联机消息、同帧内的临时值、查找键用它）。同一地址上
//     的新对象（下一关、重生）控制块不同，所以不会被当成旧的那个。跨帧留着的 ObjRef 指向的控制块可能已经被释放，
//     判定时只能靠 Readable 兜底，所以跨帧持有一律用 WeakRef。
//   - WeakRef：持有控制块的 1 个 weak count 的 ObjRef（RAII：复制加 1，覆盖 / Reset / 析构减 1，减到 0 时按控制块
//     vtable [1] 删掉它）。持有期间控制块不会被释放，判定读到的一定是这个对象自己的块。
//   - Exists / Alive：唯一的存活判定，条件以汇编为准（docs/objref-re.md 第 2 节）：
//       Exists = 控制块 use count > 0、对象 +0x30 仍是这个控制块、对象 +0x18 bit 2（Delete 0x118A1B0 的「已移除」）未置；
//       Alive  = Exists 且 +0x2E8（受伤 0x547C30 写的死亡字节）为 0。死亡字节只属于有耐久的 GameObject 一族，
//       纯场景对象（特效、DemoIndirectFire、远景物体等）的 +0x2E8 是别的字段，它们只问 Exists。
//   先读控制块、后读对象：use count 为 0 时对象已析构（存储可能已还给堆），之后不再碰对象。
// 只在游戏线程判定（对象由游戏线程删）；计数用 Interlocked，与游戏自己的 lock xadd 一致，任何线程都可以复制 / 释放。
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <intrin.h>
#include "edf/layout.h"
#include "edf/memory.h"

namespace crew {

struct ObjRef {
    const void* obj=nullptr;
    const void* ctrl=nullptr;
    static ObjRef Of(const void* o) noexcept { return ObjRef{o,o ? edf::At<const void*>(o,edf::kSelfCtrl) : nullptr}; }
    bool Is(const void* o) const noexcept { return o && o==obj && edf::At<const void*>(o,edf::kSelfCtrl)==ctrl; }
    explicit operator bool() const noexcept { return obj!=nullptr; }
};

namespace objref {
// 控制块布局（MSVC _Ref_count_base）与对象字段：docs/objref-re.md 第 1、2 节。
constexpr std::size_t kCtrlUses=edf::kCtrlUses,kCtrlWeaks=0xC,kCtrlSize=0x10;
constexpr std::size_t kObjectFlags=0x18;
constexpr unsigned char kRemoved=4;            // +0x18 bit 2：Delete 0x118A1B0 置位（0x118A273），之后管理器才放手
constexpr std::size_t kDeadByte=edf::kDead;    // +0x2E8：ApplyDamage 0x547C30 在 hp ≤ 0 时写 1（0x548429）

// 引用此刻的状态，按判定顺序：none 空引用；gone 控制块不可读 / use count 为 0 / 对象已不是这个块的（地址被新对象
// 复用）；removed 已经 Delete（还在管理器里，但游戏的遍历已经不当它存在）；present 存在（死活另问 Dead）。
enum class Life : unsigned char { none, gone, removed, present };

inline Life Identity(const ObjRef& r) noexcept {
    if(!r.obj || !r.ctrl)return Life::none;
    if(!edf::Readable(r.ctrl,kCtrlSize) || edf::At<std::int32_t>(r.ctrl,kCtrlUses)<=0)return Life::gone;
    if(!edf::Readable(r.obj,edf::kSelfCtrl+sizeof(void*)) || edf::At<const void*>(r.obj,edf::kSelfCtrl)!=r.ctrl)return Life::gone;
    return (edf::At<unsigned char>(r.obj,kObjectFlags)&kRemoved) ? Life::removed : Life::present;
}
// 对象还在场景里（未 Delete）：任何场景对象都可以问。
inline bool Exists(const ObjRef& r) noexcept { return Identity(r)==Life::present; }
// 存在且没死：只对有耐久的 GameObject（士兵、载具、敌人、建筑）问。
inline bool Alive(const ObjRef& r) noexcept {
    return Exists(r) && edf::Readable(r.obj,kDeadByte+1) && !edf::At<unsigned char>(r.obj,kDeadByte);
}
// 存在但已经死了（击毁的载具还在播坠毁、尸体还没消失）。
inline bool Dead(const ObjRef& r) noexcept {
    return Exists(r) && edf::Readable(r.obj,kDeadByte+1) && edf::At<unsigned char>(r.obj,kDeadByte)!=0;
}

// 控制块的 weak count：拿 1 个 / 还 1 个（还掉最后一个时按 vtable [1] 删控制块，同 0x118A260..0x118A270）。
// 拿之前必须确认块还在（use count > 0，或调用方本来就持有一个 weak）；WeakRef 之外只给游戏交给插件 / 插件交给游戏
// 的那一个计数用（例如消息的 weak_ptr 输出参数）。
inline void Retain(const void* ctrl) noexcept {
    if(ctrl)_InterlockedIncrement(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(const_cast<void*>(ctrl))+kCtrlWeaks));
}
inline void Release(const void* ctrl) noexcept {
    using DeleteThis=void(*)(void*);
    auto* block=static_cast<unsigned char*>(const_cast<void*>(ctrl));
    if(block && _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(block+kCtrlWeaks),-1)==1)
        (*reinterpret_cast<DeleteThis* const*>(block))[1](block);
}
}  // namespace objref

// 持有 1 个 weak count 的对象引用。只有 Identity 为 removed / present 的引用（块此刻确实还在、对象确实是这个块的）
// 才会被持有，其余（空、已经没了）存成空引用。
class WeakRef {
public:
    WeakRef() noexcept=default;
    WeakRef(const ObjRef& r) noexcept { Take(r); }   // NOLINT: 隐式，`held=ObjRef::Of(o)` 与聚合初始化照常写
    WeakRef(const WeakRef& o) noexcept : ref_(o.ref_) { objref::Retain(ref_.ctrl); }
    WeakRef(WeakRef&& o) noexcept : ref_(o.ref_) { o.ref_={}; }
    WeakRef& operator=(const WeakRef& o) noexcept {
        if(this!=&o){objref::Retain(o.ref_.ctrl);Drop();ref_=o.ref_;}
        return *this;
    }
    WeakRef& operator=(WeakRef&& o) noexcept {
        if(this!=&o){Drop();ref_=o.ref_;o.ref_={};}
        return *this;
    }
    WeakRef& operator=(const ObjRef& r) noexcept {
        if(r.obj!=ref_.obj || r.ctrl!=ref_.ctrl){Drop();Take(r);}
        return *this;
    }
    ~WeakRef() { Drop(); }
    void Reset() noexcept { Drop(); }
    // 游戏交给插件的一个已经带 1 个 weak count 的 weak_ptr {对象, 控制块}（创建函数的输出参数）：接过来，不再加计数。
    static WeakRef Adopt(const void* object,const void* control) noexcept {
        WeakRef w;
        if(control)w.ref_=ObjRef{object,control};
        return w;
    }
    // 把持有的这 1 个计数交出去（写进游戏的 weak_ptr 输出参数，由游戏还）：返回引用，自己变空，不减计数。
    ObjRef Give() noexcept { const ObjRef r=ref_; ref_={}; return r; }
    const ObjRef& Ref() const noexcept { return ref_; }
    operator const ObjRef&() const noexcept { return ref_; }   // NOLINT: 传给只读 ObjRef 的函数
    // 对象指针（游戏函数要非 const 的）与控制块。`obj` / `ctrl` 是只读属性（MSVC __declspec(property)）：读法与 ObjRef
    // 相同，写它们编译不过，引用只能经构造、赋值、Reset 换，计数不会漏。
    unsigned char* Obj() const noexcept { return static_cast<unsigned char*>(const_cast<void*>(ref_.obj)); }
    const void* ObjPtr() const noexcept { return ref_.obj; }
    const void* Ctrl() const noexcept { return ref_.ctrl; }
    __declspec(property(get=ObjPtr)) const void* obj;
    __declspec(property(get=Ctrl)) const void* ctrl;
    bool Is(const void* o) const noexcept { return ref_.Is(o); }
    explicit operator bool() const noexcept { return ref_.obj!=nullptr; }
private:
    void Take(const ObjRef& r) noexcept {
        const objref::Life life=objref::Identity(r);
        if(life!=objref::Life::present && life!=objref::Life::removed)return;
        objref::Retain(r.ctrl);
        ref_=r;
    }
    void Drop() noexcept {
        const void* const block=ref_.ctrl;
        ref_={};
        objref::Release(block);
    }
    ObjRef ref_{};
};
static_assert(sizeof(WeakRef)==sizeof(ObjRef),"WeakRef 只有一个 ObjRef");

}  // namespace crew
