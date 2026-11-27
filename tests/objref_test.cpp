// src/objref.h 对伪造的控制块与对象：Identity / Exists / Alive / Dead 的每种组合（空引用、控制块不可读、use count 为 0、
// 地址被新对象复用、已 Delete、存在、已死），以及 WeakRef 的 weak count（复制、移动、覆盖、同值赋值、Reset、析构、Adopt、
// Give、拿不到的引用不加计数、最后一个 weak 走时按 vtable [1] 删控制块）。不加载游戏。
#include "objref.h"
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {
int failures=0;
void Expect(bool ok,const char* what) {
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// 伪造的 _Ref_count_base：vtable [0] _Destroy、[1] _Delete_this（数被调了几次），+8 use、+0xC weak。
int deleted=0;
void __fastcall FakeDestroy(void*) {}
void __fastcall FakeDeleteThis(void*) { ++deleted; }
void* const kVtable[2]={reinterpret_cast<void*>(&FakeDestroy),reinterpret_cast<void*>(&FakeDeleteThis)};

struct alignas(16) Ctrl { const void* vt=kVtable; std::int32_t uses=1; std::int32_t weaks=1; };
static_assert(sizeof(Ctrl)==crew::objref::kCtrlSize,"控制块 0x10 字节");
struct alignas(16) Obj { unsigned char m[0x400]{}; };

void Make(Obj& o,Ctrl& c) {
    std::memset(o.m,0,sizeof(o.m));
    const void* self=&o;const void* block=&c;
    std::memcpy(o.m+edf::kSelf,&self,8);std::memcpy(o.m+edf::kSelfCtrl,&block,8);
    c=Ctrl{};
}

using crew::ObjRef;
using crew::WeakRef;
namespace objref=crew::objref;
using Life=objref::Life;

void Identity() {
    Obj o;Ctrl c;Make(o,c);
    const ObjRef r=ObjRef::Of(&o);
    Expect(r.obj==&o && r.ctrl==&c,"Of 取对象 +0x30 的控制块");
    Expect(objref::Identity(ObjRef{})==Life::none,"空引用 none");
    Expect(objref::Identity(ObjRef{&o,nullptr})==Life::none,"没有控制块 none");
    Expect(objref::Identity(r)==Life::present && objref::Exists(r) && objref::Alive(r) && !objref::Dead(r),"存在且活着");

    o.m[edf::kDead]=1;
    Expect(objref::Exists(r) && !objref::Alive(r) && objref::Dead(r),"死亡字节：存在、不活、死");
    o.m[edf::kDead]=0;

    o.m[objref::kObjectFlags]|=objref::kRemoved;
    Expect(objref::Identity(r)==Life::removed && !objref::Exists(r) && !objref::Alive(r) && !objref::Dead(r),"已 Delete：removed");
    o.m[objref::kObjectFlags]=static_cast<unsigned char>(o.m[objref::kObjectFlags]|0x3);   // 其它位不影响
    o.m[objref::kObjectFlags]&=static_cast<unsigned char>(~objref::kRemoved);
    Expect(objref::Exists(r),"+0x18 其它位不算删除");

    c.uses=0;
    Expect(objref::Identity(r)==Life::gone && !objref::Exists(r),"use count 0：gone");
    c.uses=-1;
    Expect(objref::Identity(r)==Life::gone,"use count 负：gone");
    c.uses=1;

    // 同一地址上的新对象：控制块换了。
    Ctrl other;
    const void* block=&other;std::memcpy(o.m+edf::kSelfCtrl,&block,8);
    Expect(objref::Identity(r)==Life::gone && !objref::Alive(r),"地址被新对象复用：gone");
    Expect(objref::Alive(ObjRef::Of(&o)),"新对象自己的引用存在");
    Expect(!r.Is(&o) && ObjRef::Of(&o).Is(&o),"Is 比控制块");
}

void Unreadable() {
    // 从没被 Readable 问过的新区域，先设成不可访问（Readable 按 tick 缓存区域）。
    auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Expect(page!=nullptr,"VirtualAlloc");
    if(!page)return;
    Obj o;Ctrl c;Make(o,c);
    auto* deadCtrl=page;                  // 控制块所在页不可读
    auto* deadObj=page+0x1000;            // 对象所在页不可读
    std::memcpy(deadCtrl,&c,sizeof(c));
    DWORD old=0;
    Expect(VirtualProtect(page,0x2000,PAGE_NOACCESS,&old)!=0,"VirtualProtect");
    Expect(objref::Identity(ObjRef{&o,deadCtrl})==Life::gone,"控制块不可读：gone，不读对象");
    Expect(objref::Identity(ObjRef{deadObj,&c})==Life::gone && !objref::Alive(ObjRef{deadObj,&c}),"对象不可读：gone");
    VirtualFree(page,0,MEM_RELEASE);
}

void Weak() {
    Obj o;Ctrl c;Make(o,c);
    deleted=0;
    {
        WeakRef a=ObjRef::Of(&o);
        Expect(c.weaks==2 && a.obj==&o && a.ctrl==&c,"构造：+1 weak");
        WeakRef b=a;
        Expect(c.weaks==3,"复制：+1");
        WeakRef m=std::move(b);
        Expect(c.weaks==3 && !b && m.Is(&o),"移动：不变，原来的变空");
        m=a.Ref();
        Expect(c.weaks==3,"同值赋值：不变");
        m=a;
        Expect(c.weaks==3,"同值复制赋值：先加后减，不变");
        m.Reset();
        Expect(c.weaks==2 && !m,"Reset：-1");
        m.Reset();
        Expect(c.weaks==2,"空的 Reset：不变");
        std::vector<WeakRef> v(3,a);
        Expect(c.weaks==5,"vector 复制 3 个");
        v.erase(v.begin());
        Expect(c.weaks==4,"erase：-1");
        v.clear();
        Expect(c.weaks==2,"clear：全还");

        Obj o2;Ctrl c2;Make(o2,c2);
        WeakRef x=ObjRef::Of(&o2);
        x=a.Ref();
        Expect(c2.weaks==1 && c.weaks==3,"覆盖：旧的 -1，新的 +1");
        x=WeakRef{};
        Expect(c.weaks==2,"赋空：-1");

        // 已 Delete 的对象照样持有（控制块此刻还在），不存在的不持有。
        o.m[objref::kObjectFlags]|=objref::kRemoved;
        WeakRef removed=ObjRef::Of(&o);
        Expect(c.weaks==3 && removed,"已 Delete：持有");
        removed.Reset();
        o.m[objref::kObjectFlags]=0;
        c.uses=0;
        WeakRef gone=ObjRef::Of(&o);
        Expect(c.weaks==2 && !gone,"use count 0：不持有、存空");
        WeakRef none=ObjRef{};
        Expect(!none,"空引用：存空");
        c.uses=1;

        // 游戏交过来的计数：Adopt 不加；Give 交出去不减。
        ++c.weaks;   // 游戏写进输出参数的那 1 个
        {
            WeakRef adopted=WeakRef::Adopt(&o,&c);
            Expect(c.weaks==3 && adopted.Is(&o),"Adopt：不加");
        }
        Expect(c.weaks==2,"Adopt 的析构：还掉游戏给的那 1 个");
        WeakRef given=ObjRef::Of(&o);
        const ObjRef out=given.Give();
        Expect(c.weaks==3 && !given && out.ctrl==&c,"Give：交出去不减，自己变空");
        objref::Release(out.ctrl);
        Expect(c.weaks==2,"交出去的由接手方还");
        Expect(deleted==0,"还剩 weak 时不删控制块");
    }
    Expect(c.weaks==1 && deleted==0,"作用域结束：插件的全还，游戏的 1 个还在");
    // 对象析构（use 归 0）后，最后一个 weak 走时删控制块：插件持有的那个比游戏的晚走。
    {
        WeakRef last=ObjRef::Of(&o);
        Expect(c.weaks==2,"再拿 1 个");
        c.uses=0;--c.weaks;   // 游戏放掉最后一个 shared：_Destroy，所有 use 的那 1 个 weak 也走
        Expect(!objref::Exists(last) && deleted==0,"对象没了，块还在（插件的 weak）");
    }
    Expect(c.weaks==0 && deleted==1,"最后一个 weak：vtable [1] 删控制块");
    objref::Retain(nullptr);objref::Release(nullptr);
    Expect(deleted==1,"空控制块的 Retain / Release 什么都不做");
}
}  // namespace

int main() {
    Identity();
    Unreadable();
    Weak();
    if(failures){std::printf("objref_test: %d failure(s)\n",failures);return 1;}
    std::printf("objref_test: ok\n");
    return 0;
}
