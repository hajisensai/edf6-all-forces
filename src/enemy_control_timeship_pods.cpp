#include "enemy_control_timeship_pods.h"
#include "enemy_control_timeship_pods_net.h"
#include "enemy_control_timeship_pods_script.h"
#include "enemy_control.h"
#include "enemy_control_action_native.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_TIMESHIP_PODS_TEST
void* TimeShipPodsTestFunction(unsigned) noexcept;
#endif
namespace timeship_pods {
constexpr unsigned kRootVtable=0x17B93C0,kCapsuleVtable=0x17BA800,kControllerVtable=0x17B9340;
constexpr unsigned kMaxRoots=8,kMaxPods=64,kQueued=16;
constexpr std::uint32_t kTag=0x31504854; // THP1, typed hatch outcome only
struct Sig {unsigned rva;unsigned char bytes[16];};
constexpr Sig kSigs[]={
    {0x3EC680,{0x48,0x89,0x4C,0x24,0x08,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41}},
    {0x3ECC40,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56}},
    {0xD7480,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x4D,0x8B,0xD0}},
    {0x53060,{0x48,0x8B,0x11,0x44,0x8B,0x49,0x08,0x4C,0x39,0x4A,0x18,0x76,0x26,0x48,0x8B,0x42}},
    {0x3ED3C0,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
    {0x3F3780,{0x48,0x89,0x5C,0x24,0x08,0x55,0x56,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xEA,0x48}},
    {0x1CB594,{0x48,0x8B,0x80,0x10,0x11,0x00,0x00,0x88,0x98,0x23,0x01,0x00,0x00,0x48,0x8B,0x4C}},
};
struct Handle {void* tree=nullptr;unsigned index=0,pad=0;};
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Event {std::uint32_t magic=kTag,version=1,value=0,reserved=0;std::uint64_t ordinal=0,input=0;};
static_assert(sizeof(Event)==32,"typed hatch outcome codec");
struct Root {
    Ref ref{},controller{};const NativeProfile* profile=nullptr;
    std::uint64_t sequence[4]{};bool result[4]{},faulted=false,quiescing=false,hatchOwned=false,executing=false;
    unsigned char savedHatch=0,expectedHatch=0;std::uint64_t generated=0,applied=0,scriptSequence=0;Event last{};
    Event queue[kQueued]{};unsigned head=0,count=0;
};
Root roots[kMaxRoots]{};
unsigned char* admittedImage=nullptr;
using VoidFn=void(__fastcall*)(void*);
using CreateFn=void(__fastcall*)(void*,unsigned);
using DropFn=void(__fastcall*)(void*,const float*,float,float,int);
using ChildFn=Handle*(__fastcall*)(const Handle*,Handle*,const wchar_t*);
using CountFn=int(__fastcall*)(const Handle*);
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_TIMESHIP_PODS_TEST
    return reinterpret_cast<T>(TimeShipPodsTestFunction(rva));
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
bool Pin(Ref r) noexcept {
    if(!Current(r) || !Readable(r.control,16,true))return false;
    auto* uses=reinterpret_cast<volatile LONG*>(r.control+8);LONG was=*uses;
    while(was>0 && was<0x7FFFFFFF) {
        const LONG actual=InterlockedCompareExchange(uses,was+1,was);
        if(actual==was)return true;was=actual;
    }
    return false;
}
void Unpin(Ref r) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8))!=0)return;
    auto* const* table=At<void* const*>(r.control,0);
    reinterpret_cast<VoidFn>(table[0])(r.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0)reinterpret_cast<VoidFn>(table[1])(r.control);
}
Root* Find(const NativeLease& l) noexcept {
    for(auto& r:roots)if(r.ref.object==l.object && r.ref.control==l.control)return &r;
    return nullptr;
}
Root* Acquire(const NativeLease& l) noexcept {
    if(auto* r=Find(l))return r;
    for(auto& r:roots)if(!r.ref.control){if(!Retain(Reference(l)))return nullptr;r.ref=Reference(l);r.profile=l.profile;return &r;}
    return nullptr;
}
bool Native() noexcept {
    if(!image)return false;if(admittedImage==image)return true;
    for(const auto& s:kSigs)if(!Readable(image+s.rva,sizeof(s.bytes)) || std::memcmp(image+s.rva,s.bytes,sizeof(s.bytes)))return false;
    admittedImage=image;return true;
}
bool Ready(const NativeLease& l) noexcept {return Family(l) && Native() && Current(Reference(l));}
unsigned char* Controller(const NativeLease& l) noexcept {
    auto* p=At<unsigned char*>(l.object,0x1110);auto* control=At<unsigned char*>(l.object,0x1118);
    if(!p || !Readable(control,16) || At<LONG>(control,8)<=0 || p!=control+16 || !Readable(p,0x130,true) ||
       At<const void*>(p,0)!=image+kControllerVtable || At<const void*>(p,0x20)!=l.object || p[0x123]>1)return nullptr;
    return p;
}
bool Child(const Handle& parent,const wchar_t* key,Handle* out) noexcept {
    *out={nullptr,0xFFFFFFFFu,0};Fn<ChildFn>(0xD7480)(&parent,out,key);return out->tree!=nullptr;
}
unsigned ConfigurationCount(const NativeLease& l) noexcept {
    Handle root{l.object+0xA8,0,0},family{},capsules{};
    if(!Child(root,L"EDF6_TimeShip",&family) || !Child(family,L"Capsule",&capsules))return 0;
    const int count=Fn<CountFn>(0x53060)(&capsules);
    return count>0 && count<=static_cast<int>(kMaxPods) ? static_cast<unsigned>(count) : 0;
}
struct Roster {unsigned char* rows=nullptr;std::size_t count=0;std::uint64_t attached=0,pending=0;};
bool ReadRoster(const NativeLease& l,Roster* out) noexcept {
    *out={};out->rows=At<unsigned char*>(l.object,0x1140);out->count=At<std::size_t>(l.object,0x1150);
    const auto capacity=At<std::size_t>(l.object,0x1148);
    if(out->count>kMaxPods || out->count>capacity || (out->count && !Readable(out->rows,out->count*16)))return false;
    for(std::size_t i=0;i<out->count;++i) {
        auto* link=At<unsigned char*>(out->rows+i*16,0);auto* ref=At<unsigned char*>(out->rows+i*16,8);
        if(!Readable(ref,16) || At<LONG>(ref,8)<=0 || link!=ref+16 || !Readable(link,0x80) || At<const void*>(link,0x18)!=l.object)return false;
        auto* child=At<unsigned char*>(link,0x48);auto* weak=At<unsigned char*>(link,0x50);
        if(!child && !weak)continue;
        if(weak && Readable(weak,16) && At<LONG>(weak,8)==0)continue;
        if(!child || !Readable(weak,16) || At<LONG>(weak,8)<=0 || !Readable(child,0x40) ||
           At<const void*>(child,0x30)!=weak || At<const void*>(child,0)!=image+kCapsuleVtable ||
           At<const void*>(child,0x38)!=l.object)return false;
        out->attached|=std::uint64_t{1}<<i;
        if(At<int>(link,0x58)>0)out->pending|=std::uint64_t{1}<<i;
    }
    return true;
}
bool Dropped(const Roster& before,const Roster& after) noexcept {
    if(before.count!=after.count)return false;
    for(std::size_t i=0;i<before.count;++i)if(before.attached&(std::uint64_t{1}<<i)) {
        auto* link=At<const unsigned char*>(after.rows+i*16,0);
        if(i==0) {if((after.attached&1) || link[0x70]!=1)return false;}
        else {
            if(!(after.pending&(std::uint64_t{1}<<i)) || At<int>(link,0x58)!=static_cast<int>(i*30))return false;
            for(unsigned axis=0;axis<4;++axis)if(!std::isfinite(At<float>(link,0x60+axis*4)))return false;
            if(At<float>(link,0x6C)!=1.0f)return false;
        }
    }
    return true;
}
bool NativeAuthority(const NativeLease& l,bool* online) noexcept {
        bool authority=false;
    if(NativeActionRole(l.object,&authority)){*online=true;return authority && !NativeActionQuiescing(l.object);}
    *online=true;if(!EnemyControlNetworkContext(l.object,online))return false;return !*online;
}
bool Needs(const NativeLease& l) noexcept {return Family(l);}
bool Before(const NativeLease& l,bool,Input*) noexcept {return Ready(l);}
bool Capture(const NativeLease& l,void* out,std::size_t capacity,std::size_t* used) noexcept {
    if(!used)return false;*used=0;auto* r=Find(l);if(!r || !r->count)return true;
    if(!out || capacity<sizeof(Event) || r->faulted)return false;
    std::memcpy(out,r->queue+r->head,sizeof(Event));r->head=(r->head+1)%kQueued;--r->count;*used=sizeof(Event);return true;
}
bool Owns(const void* bytes,std::size_t size) noexcept {
    return bytes && size>=4 && Readable(bytes,4) && At<std::uint32_t>(bytes,0)==kTag;
}
bool Hatch(Root& r,const NativeLease& l,unsigned value) noexcept {
    auto* p=Controller(l);    auto* control=At<unsigned char*>(l.object,0x1118);
    if(!p || value>1 || (r.controller.control && (r.controller.object!=p || r.controller.control!=control)))return false;
        if(!r.hatchOwned) {
        if(!Readable(control,16,true) || At<LONG>(control,12)<=0 || At<LONG>(control,12)==0x7FFFFFFF)return false;
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(control+12));r.controller={p,control};
        r.savedHatch=p[0x123];r.hatchOwned=true;
    }
    p[0x123]=static_cast<unsigned char>(value);r.expectedHatch=p[0x123];return r.expectedHatch==value;
}
enemy_control::ActionResult Apply(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    using Result=enemy_control::ActionResult;
    if(size!=sizeof(Event) || !Readable(bytes,size) || !Ready(l))return Result::failed;
    Event event{};std::memcpy(&event,bytes,sizeof(event));bool authority=false;
    if(event.magic!=kTag || event.version!=1 || event.value>1 || event.reserved>1 || !event.ordinal || !event.input ||
       !NativeActionRole(l.object,&authority) || authority)return Result::failed;
    auto* r=Acquire(l);if(!r || r->faulted)return Result::failed;
    if(event.ordinal==r->applied)return std::memcmp(&event,&r->last,sizeof(event))==0 ? Result::applied : Result::failed;
    if(event.ordinal<r->applied)return Result::failed;
    if(event.ordinal!=r->applied+1)return Result::pending;
    if(!Hatch(*r,l,event.value))return Result::failed;
    if(event.reserved==1)r->savedHatch=static_cast<unsigned char>(event.value);
    r->applied=event.ordinal;r->last=event;return Result::applied;
}
bool Quiesce(const NativeLease& l) noexcept {if(auto* r=Find(l))r->quiescing=true;return true;}
bool Empty(const NativeLease& l) noexcept {const auto* r=Find(l);return !r || !r->count;}
bool Restore(const NativeLease& l) noexcept {
    auto* r=Find(l);if(!r)return true;if(r->count || r->executing)return false;
    if(r->hatchOwned && Current(r->ref,false) && Readable(r->controller.control,16) && At<LONG>(r->controller.control,8)>0) {
        auto* p=Controller(l);if(!p || p!=r->controller.object || At<const void*>(l.object,0x1118)!=r->controller.control)return false;
        // A native/script change back to the baseline already relinquished our override.
        if(p[0x123]==r->expectedHatch)p[0x123]=r->savedHatch;
        else if(p[0x123]!=r->savedHatch)return false;
    }
    if(!Release(r->controller) || !Release(r->ref))return false;*r={};return true;
}
bool Abort(const NativeLease& l) noexcept {if(auto* r=Find(l)){r->count=0;r->quiescing=true;}return Restore(l);}
bool Register() noexcept {
    const NativeActionExtension extension{kTag,Needs,Before,nullptr,Capture,Owns,Apply,Quiesce,Empty,Abort,Restore};
    return RegisterNativeActionExtension(extension) && RegisterTimeShipPodsNetwork();
}
unsigned CommandSlot(int argument) noexcept {
    return argument==7 ? 0 : argument==8 ? 1 : argument==10 ? 2 : argument==11 ? 3 : 4;
}
bool IsAbility(const NativeAbility& a) noexcept {return a.kind==AbilityKind::timeShipCommand && !a.held && !a.pressed && CommandSlot(a.argument)<4;}
bool Available(const NativeLease& l,int argument,Roster* roster,unsigned* configured) noexcept {
        if(!Ready(l) || !InstallTimeShipPodControl() || !ReadRoster(l,roster))return false;
    bool authority=false;const bool bound=NativeActionRole(l.object,&authority);bool online=bound;
    if(!bound && !EnemyControlNetworkContext(l.object,&online))return false;
    if(bound && NativeActionQuiescing(l.object))return false;
    if(argument==7 || argument==8)return Controller(l)!=nullptr;
    if(online)return TimeShipPodsNetworkAvailable(l,argument==10);
    if(roster->pending)return false;
    if(argument==11)return roster->attached!=0;
    *configured=ConfigurationCount(l);
    return *configured && !roster->attached && roster->count+*configured<=kMaxPods && Readable(At<void*>(l.object,0x1128),16);
}
}
bool InstallTimeShipPodControl() noexcept {
        __try {
        if(!timeship_pods::Native() || !timeship_pods::Register())return false;
        if(InstallTimeShipPodScriptGates())return true;
        RestoreTimeShipPodsNetworkHooks();return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){RestoreTimeShipPodsNetworkHooks();return false;}
}
bool TimeShipPodScriptLease(void* object,NativeLease* out) noexcept {
    if(!out)return false;*out={};
    __try {
        bool authority=false;
        if(!NativeActionRole(object,&authority) || !authority || NativeActionQuiescing(object))return false;
        std::size_t count=0;const auto* profiles=Profiles(&count);
        for(std::size_t i=0;i<count;++i)if(profiles[i].vtableRva==timeship_pods::kRootVtable) {
            NativeLease lease{};lease.object=static_cast<unsigned char*>(object);lease.profile=profiles+i;
            if(!Readable(object,0x38))return false;lease.control=At<const void*>(object,0x30);
            if(!timeship_pods::Ready(lease))return false;*out=lease;return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}return false;
}
std::uint64_t TimeShipPodScriptSequence(const NativeLease& l) noexcept {
    __try {
        auto* r=timeship_pods::Acquire(l);
        if(!r || r->faulted || r->quiescing || r->scriptSequence==UINT64_MAX)return 0;
        return ++r->scriptSequence;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
void ObserveTimeShipVanillaHatch(void* object,bool open) noexcept {
    using namespace timeship_pods;
    __try {
        for(auto& r:roots)if(r.ref.object==object && r.hatchOwned && Current(r.ref)) {
            NativeLease lease{};lease.object=r.ref.object;lease.control=r.ref.control;lease.profile=r.profile;
            auto* controller=Controller(lease);
            if(controller==r.controller.object && At<const void*>(object,0x1118)==r.controller.control) {
                r.savedHatch=r.expectedHatch=open ? 1u : 0u;
            }
            return;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
bool AcceptTimeShipScriptHatch(const NativeLease& l,bool open) noexcept {
    using namespace timeship_pods;
    __try {
        bool online=false;if(!Ready(l) || !NativeAuthority(l,&online) || !online)return false;
        auto* r=Acquire(l);if(!r || r->faulted || r->quiescing || r->count==kQueued || r->generated==UINT64_MAX)return false;
        const auto sequence=TimeShipPodScriptSequence(l);if(!sequence || !Hatch(*r,l,open ? 1u : 0u))return false;
        // AS changes the persistent native baseline on both peers. Player-only
        // overrides still restore their pre-control value when ownership ends.
        r->savedHatch=r->expectedHatch;
        Event event{};event.value=r->expectedHatch;event.reserved=1;event.ordinal=++r->generated;event.input=sequence;
        r->queue[(r->head+r->count)%kQueued]=event;++r->count;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
unsigned ListTimeShipPodAbilities(const NativeLease& l,NativeAbility* out,unsigned capacity) noexcept {
    __try {
        if(!timeship_pods::Ready(l) || !InstallTimeShipPodControl())return 0;
        static const NativeAbility abilities[]={
            {"timeship_hatch_open",L"强制打开舱门",0,0,AbilityKind::timeShipCommand,nullptr,7},
            {"timeship_hatch_close",L"解除强制打开舱门",0,0,AbilityKind::timeShipCommand,nullptr,8},
            {"timeship_create_pods",L"准备原生胶囊（空舱）",0,0,AbilityKind::timeShipCommand,nullptr,10},
            {"timeship_drop_pods",L"向瞄准区域投放胶囊",0,0,AbilityKind::timeShipCommand,nullptr,11},
        };
        unsigned count=0;for(const auto& a:abilities){if(out && count<capacity)out[count]=a;++count;}return count;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
bool ReadTimeShipPodAbilityState(const NativeLease& l,const NativeAbility& a,enemy_control::Ability* out) noexcept {
    if(!out || !timeship_pods::IsAbility(a))return false;
    out->state=enemy_control::AbilityState::unavailable;out->cooldownKnown=false;out->cooldownSeconds=0;
    __try {
        timeship_pods::Roster roster{};unsigned configured=0;
        if(!timeship_pods::Available(l,a.argument,&roster,&configured))return true;
        if(auto* r=timeship_pods::Find(l))if(r->faulted || r->quiescing)return true;
        if(a.argument==7 || a.argument==8) {
            const auto* p=timeship_pods::Controller(l);const bool requested=a.argument==7;
            out->state=(p[0x123]!=0)==requested ? enemy_control::AbilityState::active : enemy_control::AbilityState::native;
        } else out->state=enemy_control::AbilityState::native;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ExecuteTimeShipPodAbility(const NativeLease& l,const NativeAbility& a,std::uint64_t sequence,std::uint64_t) noexcept {
    using namespace timeship_pods;
    __try {
        if(!IsAbility(a) || !sequence || !Ready(l) || !InstallTimeShipPodControl() || !EnemyControlTargetActive(l.object))return false;
        bool online=false;if(!NativeAuthority(l,&online))return false;
        auto* r=Acquire(l);if(!r || r->faulted || r->quiescing || r->executing)return false;
        const auto slot=CommandSlot(a.argument);if(sequence<r->sequence[slot])return false;
        if(sequence==r->sequence[slot])return r->result[slot];
        r->sequence[slot]=sequence;r->result[slot]=false;
        Roster before{};unsigned configured=0;if(!Available(l,a.argument,&before,&configured))return false;
        if(slot<2) {
            if(online && (r->count==kQueued || r->generated==UINT64_MAX))return false;
            if(!Hatch(*r,l,a.argument==7 ? 1u : 0u))return false;
            if(online) {Event event{};event.value=r->expectedHatch;event.ordinal=++r->generated;event.input=sequence;r->queue[(r->head+r->count)%kQueued]=event;++r->count;}
            r->result[slot]=true;return true;
        }
        // Native code owns creation, parent/child registration,
        // passenger contents, the fall state and each staggered future emission.
        if(online){if(r->scriptSequence==UINT64_MAX)return false;r->result[slot]=ExecuteTimeShipPodsNetwork(l,a.argument,++r->scriptSequence);return r->result[slot];}
        if(slot==2) {
                                    bool pinned=false;const Ref heldRoot=r->ref;
            __try {pinned=Pin(heldRoot);if(!pinned)return false;r->executing=true;Fn<CreateFn>(0x3EC680)(l.object,configured);}
            __finally {r->executing=false;if(pinned)Unpin(heldRoot);}
                        Roster after{};
            const auto mask=configured==64 ? UINT64_MAX : ((std::uint64_t{1}<<configured)-1)<<before.count;
            r->result[slot]=Current(r->ref) && ReadRoster(l,&after) && after.count==before.count+configured && after.attached==mask;
        } else {
            alignas(16) float point[4]{0,0,0,1};if(!EnemyControlWeaponAim(l.object,point))return false;
            for(unsigned i=0;i<3;++i)if(!std::isfinite(point[i]) || std::fabs(point[i])>1000000.0f)return false;
            constexpr float inner=50.0f,outer=100.0f;
            const double scaled=static_cast<double>(((((point[0]+point[1])+point[2])+inner)+outer)*100.0f);
            if(scaled<(std::numeric_limits<int>::min)() || scaled>(std::numeric_limits<int>::max)())return false;
                                    bool pinned=false;const Ref heldRoot=r->ref;
            __try {pinned=Pin(heldRoot);if(!pinned)return false;r->executing=true;Fn<DropFn>(0x3ECC40)(l.object,point,inner,outer,static_cast<int>(scaled));}
            __finally {r->executing=false;if(pinned)Unpin(heldRoot);}
            Roster after{};r->result[slot]=Current(r->ref) && ReadRoster(l,&after) && Dropped(before,after);
        }
        if(!r->result[slot])r->faulted=true;return r->result[slot];
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* failedRoot=timeship_pods::Find(l))failedRoot->faulted=true;return false;}
}
bool RestoreTimeShipPodControl(const NativeLease& l) noexcept {
    __try {return RestoreTimeShipPodsNetwork(l) && timeship_pods::Restore(l);} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
