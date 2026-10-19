// EDF.dll 678CCB46: actual capsule objects, stable native identities, accepted
// native ray/scatter results. Guests never rerun DropAll's RNG or ground rays.
#include "enemy_control_timeship_pods_net.h"
#include "enemy_control_action_native.h"
#include "enemy_control_action_types.h"
#include "enemy_control.h"
#include "enemy_control_pvp_transport.h"
#include "command_identity.h"
#include "support_soldier.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_TIMESHIP_PODS_NET_TEST
void* TimeShipPodsNetTestFunction(unsigned) noexcept;
bool TimeShipPodsNetTestHook() noexcept;
#endif
namespace timeship_pods_net {
constexpr unsigned kRootVtable=0x17B93C0,kCapsuleVtable=0x17BA800;
constexpr unsigned kMaxRoots=8,kMaxPods=64,kBatch=8,kQueued=16;
constexpr unsigned kDropSite=0x3ECF6A,kRelease=0x3F3780;
constexpr std::uint32_t kTag=0x314E5054,kNamespace=0xD9000000; // TPN1, a private derived-child ordinal range
struct Sig {unsigned rva;unsigned char bytes[16];};
constexpr Sig kSigs[]={
    {0x3EC000,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
    {0x3EFF40,{0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0x49,0x08,0x48,0x85,0xC9}},
    {0x11945E0,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9}},
    {0x3ED3C0,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
    {0x3EC680,{0x48,0x89,0x4C,0x24,0x08,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41}},
    {0x3ECC40,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56}},
    {0x3ECC10,{0x48,0x8B,0x81,0x40,0x11,0x00,0x00,0x44,0x8B,0xD2,0x4D,0x03,0xD2,0x4A,0x8B,0x0C}},
    {0x3ED9C0,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x48,0x8D,0x54,0x24,0x20,0xE8}},
    {0x3F3780,{0x48,0x89,0x5C,0x24,0x08,0x55,0x56,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xEA,0x48}},
    {0x776790,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x33,0xC0,0x0F,0x57,0xC0,0x48}},
    {0x781950,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24}},
    {0x118A1B0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48}},
    {0xD7480,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x4D,0x8B,0xD0}},
    {0x53060,{0x48,0x8B,0x11,0x44,0x8B,0x49,0x08,0x4C,0x39,0x4A,0x18,0x76,0x26,0x48,0x8B,0x42}},
};
struct Handle {void* tree=nullptr;unsigned index=0,pad=0;};
struct Row {std::uint32_t index=0,delay=0;unsigned char id[32]{};float point[4]{};};
struct Event {
    std::uint32_t magic=kTag;std::uint16_t version=1;std::uint8_t kind=0,count=0;
    std::uint64_t ordinal=0,input=0;std::uint32_t before=0,total=0;std::int32_t sourceSeed=0;std::uint32_t reserved=0;
    Row rows[kBatch]{};
};
static_assert(sizeof(Row)==56 && offsetof(Event,rows)==40 && sizeof(Event)==488,"bounded typed pod outcome");
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Memo {unsigned char id[32]{};float point[4]{};bool scheduled=false;};
struct Root {
    Ref ref{};bool strong=false,strongBase=false,strongDtorFailed=false;Memo memo[kMaxPods]{};unsigned settling=0;bool settlingStarted=false;std::uint64_t lastInput=0;std::uint64_t sequence[3]{},generated=0,applied=0;bool result[3]{},faulted=false,quiescing=false,executing=false;
    Event last{},queue[kQueued]{};unsigned head=0,count=0,rollbackFrom=kMaxPods+1;
};
struct Roster {unsigned count=0;unsigned char* links[kMaxPods]{};unsigned char* bodies[kMaxPods]{};std::uint64_t attached=0,pending=0;};
struct DropScope {Ref root{};unsigned char* first=nullptr;float point[4]{};bool captured=false,invalid=false;};
Root roots[kMaxRoots]{};DropScope* dropping=nullptr;
unsigned char* admittedImage=nullptr;bool hookInstalled=false;
struct HookPatch {unsigned char* site=nullptr;unsigned char original[5]{},patched[5]{};bool owned=false;};
HookPatch hookPatch{};
// Monotonic for the process, rather than the lease: exit/re-enter cannot reuse
// an ID still held by a released native capsule. IDs also derive from root ID.
std::uint32_t nextOrdinal=1;
using VoidFn=void(__fastcall*)(void*);
using CreateFn=void(__fastcall*)(void*,unsigned);
using DropFn=void(__fastcall*)(void*,const float*,float,float,int);
using SingleFn=void(__fastcall*)(void*,unsigned,int,const float*);
using ReleaseFn=void(__fastcall*)(void*,const float*);
using DeriveFn=void*(__fastcall*)(void*,const void*,unsigned);
using ChildFn=Handle*(__fastcall*)(const Handle*,Handle*,const wchar_t*);
using CountFn=int(__fastcall*)(const Handle*);
using RegisterFn=const unsigned char*(__fastcall*)(void*,ObjRef*,const unsigned char*);
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_TIMESHIP_PODS_NET_TEST
    return reinterpret_cast<T>(TimeShipPodsNetTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Family(const NativeLease& l) noexcept {
    if(!l.profile || !l.profile->name || l.profile->vtableRva!=kRootVtable || std::strcmp(l.profile->name,"EDF6_TimeShip"))return false;
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(l.profile==profiles+i)return true;
    return false;
}
Ref Reference(const NativeLease& l) noexcept {return {l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))};}
bool Current(Ref r,bool alive=true) noexcept {
    return r.object && Readable(r.control,16) && At<LONG>(r.control,8)>0 && Readable(r.object,0x1470) &&
        At<const void*>(r.object,0x30)==r.control && (!alive || (!(r.object[0x18]&4) && !r.object[0x2E8]));
}
bool Retain(Ref r) noexcept {
    if(!Current(r) || !Readable(r.control,16,true) || At<LONG>(r.control,12)<=0 || At<LONG>(r.control,12)==0x7FFFFFFF)return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(r.control+12));return true;
}
bool Release(Ref& r) noexcept {
    if(!r.control){r={};return true;}
    if(!Readable(r.control,16,true) || At<LONG>(r.control,12)<=0)return false;
    auto* const* table=At<void* const*>(r.control,0);
    if(At<LONG>(r.control,12)==1 && (!Readable(table,16) || !table[1]))return false;
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0)reinterpret_cast<VoidFn>(table[1])(r.control);
    r={};return true;
}
bool Pin(Ref r,bool alive=true) noexcept {
    if(!Current(r,alive) || !Readable(r.control,16,true))return false;
    LONG count=At<LONG>(r.control,8);
    while(count>0 && count<0x7FFFFFFF) {
        const LONG observed=InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(r.control+8),count+1,count);
        if(observed==count)return true;count=observed;
    }
    return false;
}
Root* Find(const NativeLease& l) noexcept {
    for(auto& r:roots)if(r.ref.object==l.object && r.ref.control==l.control)return &r;
    return nullptr;
}
Root* Acquire(const NativeLease& l) noexcept {
    if(auto* r=Find(l))return r;
    for(auto& r:roots)if(!r.ref.control){
        if(!Retain(Reference(l)))return nullptr;r.ref=Reference(l);
        if(!Pin(r.ref)){if(Release(r.ref))r={};else r.faulted=true;return nullptr;}
        r.strong=true;return &r;
    }
    return nullptr;
}
bool ReleasePinned(Root& r) noexcept {
    if(!r.ref.control)return true;if(r.strongDtorFailed)return false;
    // Every native-owned counter stays journalled until its mutation succeeds.
    // A dead native world reference may leave this lease as the last strong pin.
    if(!Readable(r.ref.control,16,true) || At<LONG>(r.ref.control,12)<=0)return false;
    const auto* table=At<void* const*>(r.ref.control,0);
    if(!Readable(table,16) || !table[0] || !table[1])return false;
    if(r.strong) {
        const LONG count=At<LONG>(r.ref.control,8);if(count<=0)return false;
        if(count==1 && !Readable(r.ref.object,0x1550,true))return false;
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.ref.control+8))==0) {
            r.strong=false;r.strongBase=true;r.strongDtorFailed=true;reinterpret_cast<VoidFn>(table[0])(r.ref.control);r.strongDtorFailed=false;
        } else r.strong=false;
    }
    if(r.strongBase) {
        if(!Readable(r.ref.control,16,true) || At<LONG>(r.ref.control,12)<=1)return false;
        InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.ref.control+12));r.strongBase=false;
    }
    return Release(r.ref);
}
bool Point(const float* p) noexcept {
    if(!Readable(p,16) || p[3]!=1.0f)return false;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(p[i]) || std::fabs(p[i])>1000000.0f)return false;
    return true;
}
bool Canonical(const unsigned char* id) noexcept {
    if(!id || At<unsigned>(id,12)!=5 || At<unsigned>(id,20))return false;
    unsigned any=0;for(unsigned i=0;i<32;++i)any|=id[i];return any!=0;
}
bool Ready(const NativeLease& l,bool alive=true) noexcept {
    return Family(l) && admittedImage==image && image && hookInstalled && Current(Reference(l),alive);
}
bool Role(const NativeLease& l,bool wanted) noexcept {
    bool authority=false;return NativeActionRole(l.object,&authority) && authority==wanted;
}
bool ReadRoster(const NativeLease& l,Roster* out) noexcept {
    *out={};const auto total=At<std::size_t>(l.object,0x1150),capacity=At<std::size_t>(l.object,0x1148);
    const auto* rows=At<const unsigned char*>(l.object,0x1140);
    if(total>kMaxPods || total>capacity || (total && !Readable(rows,total*16)))return false;
    out->count=static_cast<unsigned>(total);
    for(unsigned i=0;i<out->count;++i) {
        auto* link=At<unsigned char*>(rows+i*16,0);const auto* ref=At<const unsigned char*>(rows+i*16,8);
        if(!Readable(ref,16) || At<LONG>(ref,8)<=0 || link!=ref+16 || !Readable(link,0x80) || At<const void*>(link,0x18)!=l.object)return false;
        out->links[i]=link;auto* body=At<unsigned char*>(link,0x48);const auto* weak=At<const unsigned char*>(link,0x50);
        if(!body && !weak)continue;
        if(weak && Readable(weak,16) && At<LONG>(weak,8)==0)continue;
        if(!body || !Readable(weak,16) || At<LONG>(weak,8)<=0 || !Readable(body,0x138) ||
           At<const void*>(body,0x30)!=weak || At<const void*>(body,0)!=image+kCapsuleVtable || At<const void*>(body,0x38)!=l.object)return false;
        if(body[0x18]&4u)continue;
        out->bodies[i]=body;out->attached|=std::uint64_t{1}<<i;
        if(At<int>(link,0x58)>0)out->pending|=std::uint64_t{1}<<i;
    }
    return true;
}
unsigned ConfigurationCount(const NativeLease& l) noexcept {
    Handle root{l.object+0xA8,0,0},family{},capsules{};
    Fn<ChildFn>(0xD7480)(&root,&family,L"EDF6_TimeShip");if(!family.tree)return 0;
    Fn<ChildFn>(0xD7480)(&family,&capsules,L"Capsule");if(!capsules.tree)return 0;
    const int count=Fn<CountFn>(0x53060)(&capsules);
    return count>0 && count<=static_cast<int>(kBatch) ? static_cast<unsigned>(count) : 0;
}
bool Derive(const NativeLease& l,unsigned ordinal,unsigned char* id) noexcept {
    if((ordinal&0xFF000000u)!=kNamespace || !(At<unsigned>(l.object,0x128)&3u))return false;
    const auto* control=At<const unsigned char*>(l.object,0x130);
    if(!Readable(control,16) || At<LONG>(control,0)<=0)return false;
    const auto* entry=At<const void*>(control,8);if(!Readable(entry,0x28))return false;
    std::memset(id,0,32);Fn<DeriveFn>(0x776790)(id,entry,ordinal);
    return Canonical(id) && At<unsigned>(id,4)==ordinal;
}
bool Unused(const unsigned char* id) noexcept {
    MarkIdentities found{};return ResolveMarkIdentities(nullptr,id,&found) && !found.target;
}
bool MatchesId(const void* body,const unsigned char* id) noexcept {
    unsigned char actual[32]{};return ReadNativeObjectId(static_cast<const unsigned char*>(body),actual) && !std::memcmp(actual,id,32);
}
bool Available(const NativeLease& l,bool create,Roster* roster,unsigned* configured,unsigned requested=kBatch) noexcept {
    bool authority=false;if(!NativeActionRole(l.object,&authority) || !Ready(l) || !ReadRoster(l,roster) || roster->pending)return false;
    if(auto* r=Find(l))if(r->faulted || r->quiescing || r->count==kQueued || r->generated==UINT64_MAX)return false;
    if(create) {
        *configured=ConfigurationCount(l);const unsigned actual=requested<*configured ? requested : *configured;
        return actual && !roster->attached && roster->count+actual<=kMaxPods && Readable(At<void*>(l.object,0x1128),16) &&
            EnemyControlTransportPvpCanPrepareCapsules(ObjRef{l.object,l.control},actual);
    }
    unsigned count=0;
    for(unsigned i=0;i<roster->count;++i)if(roster->bodies[i]) {
        unsigned char id[32]{};if(!ReadNativeObjectId(roster->bodies[i],id) || !Canonical(id))return false;++count;
    }
    return count>0 && count<=kBatch;
}
void __fastcall ReleaseObserved(void* link,const float* point) noexcept {
    auto* scope=dropping;
    __try {
        if(scope && link==scope->first && Current(scope->root) && At<const void*>(link,0x18)==scope->root.object) {
            bool host=false;
            if(!NativeActionRole(scope->root.object,&host) || !host || scope->captured || !Point(point))scope->invalid=true;
            else {std::memcpy(scope->point,point,16);scope->captured=true;}
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){if(scope)scope->invalid=true;}
    Fn<ReleaseFn>(kRelease)(link,point);
}
bool Install() noexcept {
    if(!image)return false;if(admittedImage==image && hookInstalled)return true;
    for(const auto& s:kSigs)if(!Readable(image+s.rva,16) || std::memcmp(image+s.rva,s.bytes,16))return false;
#ifdef ENEMY_CONTROL_TIMESHIP_PODS_NET_TEST
    if(!TimeShipPodsNetTestHook())return false;
#else
    if(!Readable(image+kDropSite,5) || image[kDropSite]!=0xE8 ||
       image+kDropSite+5+At<std::int32_t>(image+kDropSite,1)!=image+kRelease)return false;
    HookPatch patch{};patch.site=image+kDropSite;std::memcpy(patch.original,patch.site,5);bool changed=false;
    if(!RedirectCall(patch.site,image+kRelease,reinterpret_cast<void*>(&ReleaseObserved),changed))return false;
    if(changed){std::memcpy(patch.patched,patch.site,5);patch.owned=true;hookPatch=patch;}
#endif
    admittedImage=image;hookInstalled=true;return true;
}
bool Settle(const NativeLease&,Root&) noexcept;
bool Needs(const NativeLease& l) noexcept {return Family(l);}
bool Before(const NativeLease& l,bool,Input*) noexcept {return Ready(l,Find(l)==nullptr) && Acquire(l)!=nullptr;}
std::size_t Bytes(const Event& event) noexcept {return offsetof(Event,rows)+event.count*sizeof(Row);}
bool Capture(const NativeLease& l,void* out,std::size_t capacity,std::size_t* used) noexcept {
    if(!used)return false;*used=0;auto* r=Find(l);if(r && r->quiescing && !Settle(l,*r))return false;if(!r || !r->count)return true;
    const auto& event=r->queue[r->head];const auto bytes=Bytes(event);
    if(!out || capacity<bytes)return false;std::memcpy(out,&event,bytes);
    r->head=(r->head+1)%kQueued;--r->count;*used=bytes;return true;
}
bool Owns(const void* bytes,std::size_t size) noexcept {return bytes && size>=4 && Readable(bytes,4) && At<std::uint32_t>(bytes,0)==kTag;}
bool Rollback(const NativeLease& l,unsigned from) noexcept {
    if(!Current(Reference(l),false))return false;
    const auto total=At<std::size_t>(l.object,0x1150),capacity=At<std::size_t>(l.object,0x1148);
    const auto* rows=At<const unsigned char*>(l.object,0x1140);
    if(total<from || total>kMaxPods || total>capacity || (total && !Readable(rows,total*16)))return false;
    bool complete=true;
    // A malformed later row cannot prevent cleanup of an independently proven
    // earlier/newer native child. Unknown rows retain the rollback journal.
    for(unsigned i=from;i<total;++i) {
        __try {
            auto* link=At<unsigned char*>(rows+i*16,0);const auto* ref=At<const unsigned char*>(rows+i*16,8);
            if(!Readable(ref,16) || At<LONG>(ref,8)<=0 || link!=ref+16 || !Readable(link,0x80) || At<const void*>(link,0x18)!=l.object){complete=false;continue;}
            auto* body=At<unsigned char*>(link,0x48);const auto* weak=At<const unsigned char*>(link,0x50);
            if(!body && !weak)continue;
            if(weak && Readable(weak,16) && At<LONG>(weak,8)==0)continue;
            if(!body || !Readable(weak,16) || At<LONG>(weak,8)<=0 || !Readable(body,0x138) ||
               At<const void*>(body,0x30)!=weak || At<const void*>(body,0)!=image+kCapsuleVtable){complete=false;continue;}
            if(body[0x18]&4u)continue;
            if(At<const void*>(body,0x38)!=l.object){complete=false;continue;}
            Fn<VoidFn>(0x118A1B0)(body);
            if(!Readable(weak,16) || (At<LONG>(weak,8)>0 && (!Readable(body,0x19) || !(body[0x18]&4u))))complete=false;
        } __except(EXCEPTION_EXECUTE_HANDLER){complete=false;}
    }
    return complete;
}
// Same consumed weak-reference/RegisterObject contract as support_soldier.cpp,
// but explicit bound action authority survives a transient session-status probe.
// A live native network manager is still mandatory; no offline adoption exists.
bool CanRegister(const NativeLease& l,void* body,const unsigned char* id,bool authority) noexcept {
    if(!Current(Reference(l),false) || !Role(l,authority) || !Readable(image+0x20B2AC8,8) || !Readable(body,0x138) || !Canonical(id) || (At<unsigned>(body,0x128)&3u))return false;
    void* manager=At<void*>(image,0x20B2AC8);auto* control=At<unsigned char*>(body,0x30);
    if(!manager || !Readable(control,16,true) || At<LONG>(control,8)<=0 || At<LONG>(control,12)<=0 || At<LONG>(control,12)==0x7FFFFFFF)return false;
    return true;
}
bool RegisterChild(const NativeLease& l,void* body,const unsigned char* id,bool authority) noexcept {
    if(!CanRegister(l,body,id,authority))return false;
    void* manager=At<void*>(image,0x20B2AC8);auto* control=At<unsigned char*>(body,0x30);
    ObjRef consumed=ObjRef::Of(body);InterlockedIncrement(reinterpret_cast<volatile LONG*>(control+12));
    const auto* registered=Fn<RegisterFn>(0x781950)(manager,&consumed,id);
    return registered && Readable(registered,32) && !std::memcmp(registered,id,20) && !std::memcmp(registered+24,id+24,8) &&
        (At<unsigned>(body,0x128)&3u)==(authority ? 2u : 1u) && At<void*>(body,0x130);
}
bool Create(const NativeLease& l,Event& event,bool authority,Root& journal) noexcept {
    if(!Role(l,authority) || !Readable(image+0x20B2AC8,8) || !At<void*>(image,0x20B2AC8))return false;
    Roster before{};if(!ReadRoster(l,&before) || before.attached || before.pending || before.count!=event.before ||
        (!event.count || event.count>ConfigurationCount(l)) || event.total!=event.before+event.count || event.total>kMaxPods)return false;
    for(unsigned i=0;i<event.count;++i) {
        auto& row=event.rows[i];const unsigned ordinal=At<unsigned>(row.id,4);unsigned char expected[32]{};
        if(row.index!=event.before+i || row.delay || !Derive(l,ordinal,expected) || std::memcmp(expected,row.id,32) || !Unused(row.id))return false;
        if(i && ordinal!=At<unsigned>(event.rows[0].id,4)+i)return false;
    }
    std::uint64_t pvpToken=0;
    if(!EnemyControlTransportPvpPrepareCapsules(ObjRef{l.object,l.control},event.count,&pvpToken)) {
        EnemyControlTransportPvpFinishCapsules(pvpToken);return false;
    }
    bool complete=false;
    __try {
        Fn<CreateFn>(0x3EC680)(l.object,event.count);
        Roster after{};if(!Current(Reference(l),false) || !ReadRoster(l,&after) || after.count!=event.total)__leave;
        // Preflight the entire created set before publishing the first native
        // registration; a bad later child must not register an earlier prefix.
        for(unsigned i=0;i<event.count;++i) {
            auto* body=after.bodies[event.rows[i].index];
            if(!body || !CanRegister(l,body,event.rows[i].id,authority) || !Unused(event.rows[i].id))__leave;
        }
        for(unsigned i=0;i<event.count;++i) {
            auto* body=after.bodies[event.rows[i].index];
            if(!EnemyControlTransportPvpAdoptCapsule(pvpToken,ObjRef{l.object,l.control},ObjRef::Of(body)))__leave;
        }
        for(unsigned i=0;i<event.count;++i) {
            auto* body=after.bodies[event.rows[i].index];
            // Construction used InitParamBase with no network ID. Never steal an
            // already registered stock child, nor expose a second random ID.
            if(!body || (At<unsigned>(body,0x128)&3u) || !RegisterChild(l,body,event.rows[i].id,authority) || !MatchesId(body,event.rows[i].id))__leave;
            const unsigned wanted=authority ? 2u : 1u;if((At<unsigned>(body,0x128)&3u)!=wanted)__leave;
        }
        complete=true;
    } __finally {
        EnemyControlTransportPvpFinishCapsules(pvpToken);
        if(!complete){journal.rollbackFrom=event.before;if(Rollback(l,event.before))journal.rollbackFrom=kMaxPods+1;}
    }
    return complete;
}
bool HostDrop(const NativeLease& l,Event& event,const Roster& before,const float* aim,float inner,float outer,int seed) noexcept {
    event.before=event.total=before.count;event.sourceSeed=seed;
    for(unsigned i=0;i<before.count;++i)if(before.bodies[i]) {
        if(event.count==kBatch)return false;auto& row=event.rows[event.count++];row.index=i;
        if(!ReadNativeObjectId(before.bodies[i],row.id) || !Canonical(row.id))return false;
    }
    if(!event.count || dropping)return false;
    DropScope scope{Reference(l),before.bodies[0] ? before.links[0] : nullptr};
    dropping=&scope;
    __try {Fn<DropFn>(0x3ECC40)(l.object,aim,inner,outer,seed);}
    __finally {dropping=nullptr;}
    Roster after{};if(!Current(Reference(l)) || (before.bodies[0] && (scope.invalid || !scope.captured)) || !ReadRoster(l,&after) || after.count!=before.count)return false;
    for(unsigned i=0;i<event.count;++i) {
        auto& row=event.rows[i];row.delay=row.index*30;
        if(!row.index) {
            if(after.bodies[0] || !before.links[0][0x70])return false;std::memcpy(row.point,scope.point,16);
        } else {
            if(after.bodies[row.index]!=before.bodies[row.index] || At<int>(after.links[row.index],0x58)!=static_cast<int>(row.delay))return false;
            std::memcpy(row.point,after.links[row.index]+0x60,16);
        }
        if(!Point(row.point))return false;
    }
    return true;
}
bool Drop(const NativeLease& l,const Event& event) noexcept {
    Roster roster{};if(!ReadRoster(l,&roster) || roster.pending || roster.count!=event.total || event.before!=event.total)return false;
    std::uint64_t expected=0;
    for(unsigned i=0;i<event.count;++i) {
        const auto& row=event.rows[i];if(row.index>=roster.count || row.delay>static_cast<unsigned>((std::numeric_limits<int>::max)()) || (event.kind==2 && row.delay!=row.index*30) || !Point(row.point) ||
            !roster.bodies[row.index] || !MatchesId(roster.bodies[row.index],row.id) || (expected&(std::uint64_t{1}<<row.index)))return false;
        expected|=std::uint64_t{1}<<row.index;
    }
    if(event.kind==2 ? expected!=roster.attached : (event.count!=1 || (expected&roster.attached)!=expected))return false;
    // All identity, destination and bounds checks precede the first native call.
    // Failure after a native mutation is terminal, never a pending/retry prefix.
    for(unsigned i=0;i<event.count;++i) {
        const auto& row=event.rows[i];if(event.kind==2)Fn<VoidFn>(0x3ED9C0)(roster.links[row.index]);
        if(!Current(Reference(l),false))return false;
        Fn<SingleFn>(0x3ECC10)(l.object,row.index,static_cast<int>(row.delay),row.point);
        if(!row.delay) {if(At<void*>(roster.links[row.index],0x48) || At<void*>(roster.links[row.index],0x50))return false;}
        else if(At<int>(roster.links[row.index],0x58)!=static_cast<int>(row.delay) || std::memcmp(roster.links[row.index]+0x60,row.point,16))return false;
    }
    return true;
}
void Remember(Root& root,const Event& event) noexcept {
    root.lastInput=event.input;
    for(unsigned i=0;i<event.count;++i) {
        const auto& row=event.rows[i];auto& memo=root.memo[row.index];
        std::memcpy(memo.id,row.id,32);
        if(event.kind!=1){std::memcpy(memo.point,row.point,16);memo.scheduled=true;}
    }
}
bool Settle(const NativeLease& l,Root& root) noexcept {
    if(!root.quiescing || !root.settlingStarted || root.settling>=kMaxPods || root.count==kQueued)return true;
    if(!Current(root.ref,false))return false;
    Roster roster{};if(!ReadRoster(l,&roster))return false;
    Event event{};event.kind=4;event.input=root.lastInput;event.ordinal=root.generated+1;
    event.before=event.total=roster.count;event.reserved=Current(root.ref) ? 0u : 1u;
    unsigned cursor=root.settling;
    for(;cursor<kMaxPods && event.count<kBatch;++cursor) {
        const auto& memo=root.memo[cursor];if(!memo.scheduled)continue;
        if(cursor>=roster.count || !Point(memo.point))return false;
        auto* link=roster.links[cursor];const int remaining=At<int>(link,0x58);
        if(!roster.bodies[cursor] && !link[0x70])continue; // ordinary native child death remains its registered HP/death channel
        if(roster.bodies[cursor] && (remaining<0 || !MatchesId(roster.bodies[cursor],memo.id)))return false;
        auto& row=event.rows[event.count++];row.index=cursor;row.delay=roster.bodies[cursor] ? static_cast<unsigned>(remaining) : 0u;
        std::memcpy(row.id,memo.id,32);std::memcpy(row.point,memo.point,16);
    }
    if(event.count) {
        if(!event.input || root.generated==UINT64_MAX)return false;
        root.queue[(root.head+root.count)%kQueued]=event;++root.count;++root.generated;
    }
    root.settling=cursor;return true;
}
bool Settled(const NativeLease& l,Root& root,const Event& event) noexcept {
    Roster roster{};if(!ReadRoster(l,&roster) || event.before!=roster.count || event.total!=roster.count)return false;
    ObjRef bodies[kBatch]{};
    for(unsigned i=0;i<event.count;++i) {
        const auto& row=event.rows[i];if(row.index>=roster.count || row.delay>static_cast<unsigned>((std::numeric_limits<int>::max)()))return false;
        const auto& memo=root.memo[row.index];if(!memo.scheduled || std::memcmp(memo.id,row.id,32) || std::memcmp(memo.point,row.point,16))return false;
        if(roster.bodies[row.index]) {
            if(!MatchesId(roster.bodies[row.index],row.id))return false;bodies[i]=ObjRef::Of(roster.bodies[row.index]);
        } else if(!roster.links[row.index][0x70])return false;
        // A host which died before a delayed release cancels that still-attached
        // capsule. If this guest ran ahead, resolve only that exact real ID.
        if(event.reserved && row.delay && !bodies[i]) {
            MarkIdentities resolved{};
            if(!ResolveMarkIdentities(nullptr,row.id,&resolved))return false;
            if(resolved.target) {
                if(!Readable(resolved.target.obj,0x138) || At<const void*>(resolved.target.obj,0)!=image+kCapsuleVtable ||
                    !MatchesId(resolved.target.obj,row.id))return false;bodies[i]=resolved.target;
            }
        }
    }
    for(unsigned i=0;i<event.count;++i) {
        const auto& row=event.rows[i];if(!bodies[i])continue;
        if(event.reserved && row.delay)Fn<VoidFn>(0x118A1B0)(const_cast<void*>(bodies[i].obj));
        else if(roster.bodies[row.index])Fn<SingleFn>(0x3ECC10)(l.object,row.index,static_cast<int>(row.delay),row.point);
    }
    return true;
}
bool Decode(const void* bytes,std::size_t size,Event* event) noexcept {
    if(!bytes || !Readable(bytes,size) || size<offsetof(Event,rows) || size>sizeof(Event))return false;
    *event={};std::memcpy(event,bytes,size);
    if(event->magic!=kTag || event->version!=1 || (event->kind!=1 && event->kind!=2 && event->kind!=3 && event->kind!=4) || !event->count || event->count>kBatch ||
       Bytes(*event)!=size || !event->ordinal || !event->input || (event->kind==4 ? event->reserved>1 : event->reserved!=0) || event->total>kMaxPods)return false;
    for(unsigned i=0;i<event->count;++i) {
        const auto& row=event->rows[i];if(!Canonical(row.id))return false;
        if(event->kind==1) {
            if(event->sourceSeed || row.delay || row.point[0] || row.point[1] || row.point[2] || row.point[3])return false;
        } else if(!Point(row.point) || ((event->kind==3 || event->kind==4) && event->sourceSeed) || (event->kind==3 && event->count!=1))return false;
        for(unsigned j=0;j<i;++j)if(!std::memcmp(row.id,event->rows[j].id,32))return false;
    }
    return true;
}
enemy_control::ActionResult Apply(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    using Result=enemy_control::ActionResult;Event event{};
    if(!Ready(l,Find(l)==nullptr) || !Role(l,false) || !Decode(bytes,size,&event))return Result::failed;
    auto* r=Acquire(l);if(!r || r->faulted)return Result::failed;
    if(event.ordinal==r->applied)return !std::memcmp(&event,&r->last,sizeof(event)) ? Result::applied : Result::failed;
    if(event.ordinal<r->applied)return Result::failed;if(event.ordinal!=r->applied+1)return Result::pending;
    if(r->executing)return Result::pending;
    if(event.kind==1 && !EnemyControlTransportPvpCanPrepareCapsules(ObjRef{l.object,l.control},event.count))return Result::pending;
    bool applied=false;r->executing=true;
    __try {
        __try {applied=event.kind==1 ? Create(l,event,false,*r) : event.kind==4 ? Settled(l,*r,event) : Drop(l,event);}
        __finally {r->executing=false;}
    } __except(EXCEPTION_EXECUTE_HANDLER){applied=false;}
    if(!applied){r->faulted=true;return Result::failed;}
    if(event.kind!=4)Remember(*r,event);r->last=event;r->applied=event.ordinal;return Result::applied;
}
bool Quiesce(const NativeLease& l) noexcept {
    if(auto* r=Find(l)){r->quiescing=true;bool authority=false;
        if(NativeActionRole(l.object,&authority) && authority){r->settlingStarted=true;return Settle(l,*r);}
    }return true;
}
bool Empty(const NativeLease& l) noexcept {const auto* r=Find(l);return !r || (!r->count && (!r->settlingStarted || r->settling>=kMaxPods));}
bool Restore(const NativeLease& l) noexcept {
    auto* r=Find(l);if(!r)return true;if(!Empty(l) || r->executing || (dropping && dropping->root.object==l.object))return false;
    if(r->rollbackFrom<=kMaxPods){if(!Rollback(l,r->rollbackFrom))return false;r->rollbackFrom=kMaxPods+1;}
    if(!ReleasePinned(*r))return false;*r={};return true;
}
bool Abort(const NativeLease& l) noexcept {if(auto* r=Find(l)){r->count=0;r->quiescing=true;r->settling=kMaxPods;}return Restore(l);}
} // namespace timeship_pods_net

bool RestoreTimeShipPodsNetworkHooks() noexcept {
    using namespace timeship_pods_net;
    __try {
        if(dropping)return false;for(const auto& r:roots)if(r.ref.control)return false;
        if(hookPatch.owned) {
            if(!Readable(hookPatch.site,5))return false;
            if(std::memcmp(hookPatch.site,hookPatch.original,5)) {
                // Never remove a later mod's hook or overwrite an unknown patch.
                if(std::memcmp(hookPatch.site,hookPatch.patched,5))return false;
                DWORD old=0;if(!VirtualProtect(hookPatch.site,5,PAGE_EXECUTE_READWRITE,&old))return false;
                std::memcpy(hookPatch.site,hookPatch.original,5);FlushInstructionCache(GetCurrentProcess(),hookPatch.site,5);
                DWORD ignored=0;if(!VirtualProtect(hookPatch.site,5,old,&ignored))return false;
            }
        }
        hookPatch={};admittedImage=nullptr;hookInstalled=false;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RegisterTimeShipPodsNetwork() noexcept {
    using namespace timeship_pods_net;
    __try {
        if(!Install())return false;
        const NativeActionExtension extension{kTag,Needs,Before,nullptr,Capture,Owns,Apply,Quiesce,Empty,Abort,Restore};
        return RegisterNativeActionExtension(extension);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool TimeShipPodsNetworkAvailable(const NativeLease& l,bool create) noexcept {
    __try {timeship_pods_net::Roster roster{};unsigned configured=0;return RegisterTimeShipPodsNetwork() && timeship_pods_net::Available(l,create,&roster,&configured);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
namespace timeship_pods_net {
bool ExecuteRequestBody(const NativeLease& l,bool create,unsigned requested,const float* aim,float inner,float outer,int seed,std::uint64_t sequence) noexcept {
    __try {
        if(!sequence || !RegisterTimeShipPodsNetwork() || !Ready(l) || !Role(l,true) ||
           NativeActionQuiescing(l.object) || !EnemyControlTargetActive(l.object))return false;
        auto* r=Acquire(l);if(!r || r->faulted || r->quiescing)return false;const unsigned slot=create ? 0u : 1u;
        if(sequence<r->sequence[slot])return false;if(sequence==r->sequence[slot])return r->result[slot];
        r->sequence[slot]=sequence;r->result[slot]=false;
        if(!create && (!Point(aim) || !std::isfinite(inner) || !std::isfinite(outer) || inner<0 || outer<inner || outer>1000000.0f))return false;
        if(create && !requested){r->result[slot]=true;return true;}
        Roster roster{};unsigned configured=0;if(!Available(l,create,&roster,&configured,requested))return false;
        Event event{};event.kind=static_cast<std::uint8_t>(slot+1);event.ordinal=r->generated+1;event.input=sequence;
        bool applied=false;
        if(create) {
            const auto actual=requested<configured ? requested : configured;
            if(nextOrdinal>0x00FFFFFFu-actual)return false;
            event.before=roster.count;event.total=roster.count+actual;event.count=static_cast<std::uint8_t>(actual);
            const auto first=nextOrdinal;nextOrdinal+=actual; // reserve once even after a failed native birth
            for(unsigned i=0;i<actual;++i) {
                auto& row=event.rows[i];row.index=event.before+i;if(!Derive(l,kNamespace|(first+i),row.id))return false;
            }
            applied=Create(l,event,true,*r);
        } else applied=HostDrop(l,event,roster,aim,inner,outer,seed);
        if(!applied){r->faulted=true;return false;}
        Remember(*r,event);r->queue[(r->head+r->count)%kQueued]=event;++r->count;++r->generated;r->result[slot]=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* r=Find(l))r->faulted=true;dropping=nullptr;return false;}
}
bool ExecuteRequest(const NativeLease& l,bool create,unsigned requested,const float* aim,float inner,float outer,int seed,std::uint64_t sequence) noexcept {
    __try {
        if(!RegisterTimeShipPodsNetwork() || !Ready(l) || !Role(l,true))return false;
        auto* r=Acquire(l);if(!r || r->executing || !r->strong)return false;
        r->executing=true;bool result=false;
        __try {result=ExecuteRequestBody(l,create,requested,aim,inner,outer,seed,sequence);}
        __finally {r->executing=false;}
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* r=Find(l))r->faulted=true;return false;}
}
}
bool ExecuteTimeShipPodsNetworkCreate(const NativeLease& l,unsigned count,std::uint64_t sequence) noexcept {
    return timeship_pods_net::ExecuteRequest(l,true,count,nullptr,0,0,0,sequence);
}
bool ExecuteTimeShipPodsNetworkDrop(const NativeLease& l,const float* point,float inner,float outer,int seed,std::uint64_t sequence) noexcept {
    return timeship_pods_net::ExecuteRequest(l,false,0,point,inner,outer,seed,sequence);
}
bool ExecuteTimeShipPodsNetworkDropOneBody(const NativeLease& l,unsigned index,int delay,const float* point,std::uint64_t sequence) noexcept {
    using namespace timeship_pods_net;
    __try {
        if(!sequence || delay<0 || !Point(point) || !RegisterTimeShipPodsNetwork() || !Ready(l) || !Role(l,true) ||
           NativeActionQuiescing(l.object) || !EnemyControlTargetActive(l.object))return false;
        auto* r=Acquire(l);if(!r || r->faulted || r->quiescing)return false;
        constexpr unsigned slot=2;if(sequence<r->sequence[slot])return false;if(sequence==r->sequence[slot])return r->result[slot];
        r->sequence[slot]=sequence;r->result[slot]=false;
        if(r->count==kQueued || r->generated==UINT64_MAX)return false;
        Roster before{};if(!ReadRoster(l,&before) || before.pending || index>=before.count || !before.bodies[index])return false;
        Event event{};event.kind=3;event.count=1;event.ordinal=r->generated+1;event.input=sequence;event.before=event.total=before.count;
        auto& row=event.rows[0];row.index=index;row.delay=static_cast<unsigned>(delay);std::memcpy(row.point,point,16);
        if(!ReadNativeObjectId(before.bodies[index],row.id) || !Canonical(row.id))return false;
        Fn<SingleFn>(0x3ECC10)(l.object,index,delay,point);
        const bool applied=Current(Reference(l)) && (delay ? At<int>(before.links[index],0x58)==delay && !std::memcmp(before.links[index]+0x60,point,16) :
            !At<void*>(before.links[index],0x48) && !At<void*>(before.links[index],0x50));
        if(!applied){r->faulted=true;return false;}
        Remember(*r,event);r->queue[(r->head+r->count)%kQueued]=event;++r->count;++r->generated;r->result[slot]=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* r=timeship_pods_net::Find(l))r->faulted=true;return false;}
}
bool ExecuteTimeShipPodsNetworkDropOne(const NativeLease& l,unsigned index,int delay,const float* point,std::uint64_t sequence) noexcept {
    using namespace timeship_pods_net;
    __try {
        if(!RegisterTimeShipPodsNetwork() || !Ready(l) || !Role(l,true))return false;
        auto* r=Acquire(l);if(!r || r->executing || !r->strong)return false;
        r->executing=true;bool result=false;
        __try {result=ExecuteTimeShipPodsNetworkDropOneBody(l,index,delay,point,sequence);}
        __finally {r->executing=false;}
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* r=Find(l))r->faulted=true;return false;}
}
bool ExecuteTimeShipPodsNetwork(const NativeLease& l,int argument,std::uint64_t sequence) noexcept {
    __try {
        if(argument==10)return ExecuteTimeShipPodsNetworkCreate(l,timeship_pods_net::kBatch,sequence);
        if(argument!=11)return false;
        alignas(16) float point[4]{0,0,0,1};if(!EnemyControlWeaponAim(l.object,point) || !timeship_pods_net::Point(point))return false;
        const float seed=(((point[0]+point[1])+point[2])+50.0f+100.0f)*100.0f;
        if(!std::isfinite(seed) || static_cast<double>(seed)<(std::numeric_limits<int>::min)() || static_cast<double>(seed)>(std::numeric_limits<int>::max)())return false;
        return ExecuteTimeShipPodsNetworkDrop(l,point,50.0f,100.0f,static_cast<int>(seed),sequence);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RestoreTimeShipPodsNetwork(const NativeLease& l) noexcept {
    __try {return timeship_pods_net::Restore(l);} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew::enemy_native
