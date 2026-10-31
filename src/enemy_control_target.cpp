// Supported EDF.dll 0x678CCB46. Game-thread native actor/LockPoint selection and
// target weak ownership. Never called from weapon hooks, rendering or transport.
#include "enemy_control_target.h"
#include "memory.h"
#include "net_object_resolve.h"
#include "support_soldier.h"
#include <cmath>
#include <cstring>

namespace crew::enemy_target {
namespace {
constexpr unsigned kTeams=0x20B2978,kWalk=0x5E0F20,kBodyObject=0x108260,kNativeSet=0x364EB0;
constexpr unsigned kStreamCtor=0x79A460,kStreamByte=0x12B5790,kStreamDtor=0x760180,kOnline=0x7748F0;
constexpr unsigned kMaxTeams=64,kMaxLocks=1024,kMaxRayHits=32;
constexpr float kCone=0.025f,kLosSlack=1.0f,kRayStep=0.25f;
constexpr unsigned char kWalkSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x55,0x41,0x56};
constexpr unsigned char kBodySig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0xED,0xA6,0xFA,0x01,0x8B,0xD1,0x48,0x8D,0x48};
constexpr unsigned char kSetSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
constexpr unsigned char kCtorSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0xE2,0x5C,0xFC,0xFF};
constexpr unsigned char kByteSig[]={0x8D,0x42,0x0E,0x4C,0x8D,0x41,0x10,0x3C,0x1D};
constexpr unsigned char kDtorSig[]={0x48,0x8D,0x05,0x01,0xCE,0x08,0x01,0x48,0x89,0x01,0xE9};
constexpr unsigned char kOnlineSig[]={0x48,0x8B,0x05,0x99,0xDF,0x93,0x01,0x8B,0x48,0x38,0x83,0xF9,0xFF,0x75,0x03,0x32};
bool ready=false;

bool Same(const ObjRef& a,const ObjRef& b) noexcept {return a.obj==b.obj && a.ctrl==b.ctrl;}
bool Current(const ObjRef& r) noexcept {
    return r.obj && r.ctrl && Readable(r.ctrl,16) && At<LONG>(r.ctrl,8)>0 &&
        Readable(r.obj,kSelfCtrl+8) && At<const void*>(r.obj,kSelf)==r.obj && At<const void*>(r.obj,kSelfCtrl)==r.ctrl;
}
bool Alive(const ObjRef& r) noexcept {
    const auto* object=static_cast<const unsigned char*>(r.obj);
    return Current(r) && Readable(object,kObjectFlags+4) && !(object[0x18]&4) && !object[kDead];
}
bool Finite(const float* p) noexcept {return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);}
bool Zero(const unsigned char* id) noexcept {for(unsigned i=0;i<32;++i)if(id[i])return false;return true;}
// A focus is an object reference (net_object_ref.h): a player by its mission slot (an enemy player in PvP: its NetId is
// another on every machine), anything else by its NetId.
bool Canonical(const unsigned char* id) noexcept {return id && netref::Valid(id,false);}
// `actor`'s reference is `id` (what it is called on every machine: net_object_resolve.h EncodeNetRef).
bool Named(const ObjRef& actor,const unsigned char* id) noexcept {
    unsigned char actual[32]{};
    return EncodeNetRef(actor,actual) && !std::memcmp(actual,id,32);
}
bool Frame(const ObjRef& owner,const ObjRef& controlled,const Ray& ray) noexcept {
    const float d2=ray.direction[0]*ray.direction[0]+ray.direction[1]*ray.direction[1]+ray.direction[2]*ray.direction[2];
    return ready && Alive(owner) && Alive(controlled) && !Same(owner,controlled) && Finite(ray.origin) && Finite(ray.direction) &&
        d2>0.99f && d2<1.01f && std::isfinite(ray.range) && ray.range>0.0f && ray.range<=6000.0f && MapRayReady();
}
// Readable caches regions per frame. Re-query protection before touching a
// retained weak counter; a protection change inside that frame must be visible.
bool WritableNow(const void* pointer,std::size_t size) noexcept {
    auto at=reinterpret_cast<std::uintptr_t>(pointer);
    if(!at || !size || at+size<at)return false;
    const auto end=at+size;
    while(at<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
            (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
            !(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto limit=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
        if(limit<=at)return false;
        at=limit<end ? limit : end;
    }
    return true;
}
bool WeakWritable(const ObjRef& r) noexcept {
    if(!r.obj && !r.ctrl)return true;
    if(!r.obj || !r.ctrl || !Readable(r.ctrl,16,true) || !WritableNow(r.ctrl,16) || At<LONG>(r.ctrl,12)<=0)return false;
    // The final weak release calls control vtable slot1, without dereferencing
    // the expired actor. Check that contract before decrementing the token.
    if(At<LONG>(r.ctrl,12)==1) {
        const auto* table=At<const void* const*>(r.ctrl,0);
        if(!Readable(table,16) || !Readable(table[1],1))return false;
    }
    return true;
}
void Hold(const ObjRef& r) noexcept {
    if(r.ctrl)InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(const_cast<void*>(r.ctrl))+12));
}
bool Drop(ObjRef& r) noexcept {
    __try {
        if(!WeakWritable(r))return false;
        if(!r.ctrl)return true;
        auto* ctrl=static_cast<unsigned char*>(const_cast<void*>(r.ctrl));
        const LONG remaining=InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+12));
        // Only a successful decrement consumes this journal entry. Never erase
        // it before a write fault, nor release it twice after a destructor fault.
        r={};
        if(remaining==0)reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(ctrl,0)[1])(ctrl);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WeakAssign(ObjRef& owned,const ObjRef& next,ObjRef& retired) noexcept {
    if(!WeakWritable(owned) || !WeakWritable(next) || !WeakWritable(retired))return false;
    if(!Drop(retired))return false;
    if(Same(owned,next))return true;
    Hold(next);retired=next;
    const auto prior=owned;owned=retired;retired=prior;
    return Drop(retired);
}
int ObserverTeam(const ObjRef& controlled) noexcept {
    const int team=At<int>(controlled.obj,kTeam);
    // Native closed/invulnerable phases unregister with team=-1 while +318
    // preserves the effective previous team. It remains the observer's side.
    return team==-1 ? At<int>(controlled.obj,0x318) : team;
}
bool Hostile(const ObjRef& controlled,const ObjRef& candidate) noexcept {
    if(!Alive(candidate) || Same(controlled,candidate))return false;
    const auto* object=static_cast<const unsigned char*>(candidate.obj);
    const auto flags=At<std::uint32_t>(object,kObjectFlags);
    if(!(flags&0x40u) || (flags&0x80u))return false; // real registration and native NoTarget
    const int source=ObserverTeam(controlled),target=At<int>(object,kTeam);
    if(source<0 || target<0 || source>=static_cast<int>(kMaxTeams) || target>=static_cast<int>(kMaxTeams))return false;
    const auto* manager=At<const unsigned char*>(image,kTeams);
    if(!Readable(manager,0x50))return false;
    const auto* rows=At<const unsigned char*>(manager,0x38);
    if(!Readable(rows+source*0x38,0x38))return false;
    const auto count=At<std::uint64_t>(rows+source*0x38,0x28);
    const auto* relation=At<const int*>(rows+source*0x38,0x18);
    return static_cast<unsigned>(target)<count && count<=kMaxTeams && Readable(relation,static_cast<std::size_t>(count)*4) && relation[target]==2;
}
bool Lock(const ObjRef& actor,int index,ObjRef* lock,float* point) noexcept {
    if(!Alive(actor) || index<0 || !Readable(actor.obj,0x380))return false;
    const auto count=At<std::uint64_t>(actor.obj,0x378);
    const auto* table=At<const unsigned char*>(actor.obj,0x368);
    if(count>kMaxLocks || static_cast<unsigned>(index)>=count || !Readable(table,static_cast<std::size_t>(count)*16))return false;
    const auto* record=table+static_cast<unsigned>(index)*16;
    const ObjRef ref{At<const void*>(record,0),At<const void*>(record,8)};
    const auto* p=static_cast<const unsigned char*>(ref.obj);
    if(!p || !ref.ctrl || !Readable(ref.ctrl,16) || At<LONG>(ref.ctrl,8)<=0 || !Readable(p,0x50) ||
        At<const void*>(p,8)!=actor.obj || At<int>(p,0)!=0 || !p[0x29] || !p[0x2A] || !Finite(reinterpret_cast<const float*>(p+0x10)))return false;
    *lock=ref;std::memcpy(point,p+0x10,12);return true;
}
float Score(const Ray& ray,const float* point) noexcept {
    const float d[3]={point[0]-ray.origin[0],point[1]-ray.origin[1],point[2]-ray.origin[2]};
    const float along=d[0]*ray.direction[0]+d[1]*ray.direction[1]+d[2]*ray.direction[2];
    if(!Finite(point) || !(along>0.01f) || along>ray.range)return -1.0f;
    const float d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2],off=d2-along*along;
    if(d2>ray.range*ray.range)return -1.0f;
    const float slope=off>0.0f ? off/(along*along) : 0.0f;
    return slope<=kCone*kCone ? slope+along*1.0e-9f : -1.0f;
}
// Layer-11 native nearest object query -> property-key-0 SceneObject. Advance
// only over the authenticated source or controlled body; every other body blocks.
bool ObjectVisible(const ObjRef& owner,const ObjRef& controlled,const ObjRef& target,const float* from,const float* to) noexcept {
    float a[3]={from[0],from[1],from[2]};
    const float delta[3]={to[0]-from[0],to[1]-from[1],to[2]-from[2]};
    const float length=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
    if(!(length>0.01f) || !std::isfinite(length))return false;
    const float unit[3]={delta[0]/length,delta[1]/length,delta[2]/length};
    for(unsigned n=0;n<kMaxRayHits;++n) {
        float at[3]{};std::uint32_t body=0;
        const float distance=ObjectRay(a,to,at,&body);
        if(distance<0.0f)return true;
        if(!std::isfinite(distance) || !Finite(at))return false;
        const auto* object=reinterpret_cast<const void*(__fastcall*)(std::uint32_t)>(image+kBodyObject)(body);
        if(object==target.obj)return true;
        if(object!=owner.obj && object!=controlled.obj)return false;
        for(unsigned i=0;i<3;++i)a[i]=at[i]+unit[i]*kRayStep;
        const float remain=(to[0]-a[0])*unit[0]+(to[1]-a[1])*unit[1]+(to[2]-a[2])*unit[2];
        if(remain<=0.0f)return true;
    }
    return false;
}
bool Visible(const ObjRef& owner,const ObjRef& controlled,const ObjRef& target,const Ray& ray,const float* point) noexcept {
    const float d[3]={point[0]-ray.origin[0],point[1]-ray.origin[1],point[2]-ray.origin[2]};
    const float distance=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    const float map=MapRay(ray.origin,point,nullptr);
    return std::isfinite(map) && (map<0.0f || map+kLosSlack>=distance) && ObjectVisible(owner,controlled,target,ray.origin,point);
}
struct Search {
    const void* const* vtable;
    const ObjRef* owner;
    const ObjRef* controlled;
    const Ray* ray;
    const void* only;      // a resolved focus (Resolve / Normalize): no other actor is a candidate
    const void* direct=nullptr;
    Selection selected{};
    float score=1.0f;
};
void __fastcall Ignore(void*,unsigned) noexcept {}
void __fastcall Visit(Search* s,const void* object) noexcept {
    __try {
        if(!object || object==s->owner->obj || object==s->controlled->obj || !Readable(object,kSelfCtrl+8))return;
        if(s->only && object!=s->only)return;
        const ObjRef actor=ObjRef::Of(object);
        if(!Hostile(*s->controlled,actor))return;
        const auto count=At<std::uint64_t>(object,0x378);
        if(count>kMaxLocks)return;
        // Actors without a lock point remain valid physical/capture targets. A
        // homing hook receives no LockPoint for them, and never fabricates one.
        for(unsigned i=0;i<(count ? count : 1);++i) {
            Selection next{};next.actor=actor;
            if(count) {
                next.lockIndex=static_cast<int>(i);
                if(!Lock(actor,next.lockIndex,&next.lock,next.point))continue;
            } else std::memcpy(next.point,static_cast<const unsigned char*>(object)+kPosition,12);
            float score=Score(*s->ray,next.point);
            if(object==s->direct) {
                // A large body's real surface may be under the reticle while its
                // lock locator is off center. Keep that actor and its real point.
                const float d[3]={next.point[0]-s->ray->origin[0],next.point[1]-s->ray->origin[1],next.point[2]-s->ray->origin[2]};
                const float along=d[0]*s->ray->direction[0]+d[1]*s->ray->direction[1]+d[2]*s->ray->direction[2];
                if(!(along>0.0f) || along>s->ray->range)continue;
                const float square=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
                if(square>s->ray->range*s->ray->range)continue;
                score=-2.0f+(square-along*along)/(1.0f+square);
            }
            if((score<0.0f && object!=s->direct) || score>=s->score || !Visible(*s->owner,*s->controlled,actor,*s->ray,next.point))continue;
            s->selected=next;s->score=score;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
const void* const kVisitor[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};
const void* Direct(const ObjRef& owner,const ObjRef& controlled,const Ray& ray) noexcept {
    float end[3]{};for(unsigned i=0;i<3;++i)end[i]=ray.origin[i]+ray.direction[i]*ray.range;
    float ground[3]{};const float map=MapRay(ray.origin,end,ground);
    if(map>=0.0f){if(!Finite(ground))return nullptr;std::memcpy(end,ground,12);}
    float from[3]={ray.origin[0],ray.origin[1],ray.origin[2]};
    for(unsigned n=0;n<kMaxRayHits;++n) {
        float at[3]{};std::uint32_t body=0;
        if(ObjectRay(from,end,at,&body)<0.0f)return nullptr;
        if(!Finite(at))return nullptr;
        const void* object=reinterpret_cast<const void*(__fastcall*)(std::uint32_t)>(image+kBodyObject)(body);
        if(object!=owner.obj && object!=controlled.obj)return object;
        for(unsigned i=0;i<3;++i)from[i]=at[i]+ray.direction[i]*kRayStep;
        const float remain=(end[0]-from[0])*ray.direction[0]+(end[1]-from[1])*ray.direction[1]+(end[2]-from[2])*ray.direction[2];
        if(remain<=0.0f)return nullptr;
    }
    return nullptr;
}
bool SearchTarget(const ObjRef& owner,const ObjRef& controlled,const Ray& ray,const unsigned char* id,Selection* out) noexcept {
    if(!out)return false;
    *out={};
    if(!Frame(owner,controlled,ray) || (id && !Canonical(id)))return false;
    // A focus named by reference is found as every receiver finds one (ResolveNetRef, live): a player by its slot (its live
    // object of that slot), the rest by NetId. Not here (dead, gone): an empty selection; two of it: a failure.
    NetResolved focus{};
    if(id) {
        if(!ResolveNetRef(id,NetLife::live,&focus) || focus.ambiguous)return false;
        if(!focus.found)return true;
    }
    const auto* manager=At<const unsigned char*>(image,kTeams);
    const int team=ObserverTeam(controlled);
    if(team<0 || team>=static_cast<int>(kMaxTeams) || !Readable(manager,0x50))return false;
    const auto* rows=At<const unsigned char*>(manager,0x38);
    if(!Readable(rows+team*0x38,0x38))return false;
    const auto count=At<std::uint64_t>(rows+team*0x38,0x28);
    if(!count || count>kMaxTeams || !Readable(rows,static_cast<std::size_t>(count)*0x38) ||
        !Readable(At<const void*>(rows+team*0x38,0x18),static_cast<std::size_t>(count)*4))return false;
    Search search{kVisitor,&owner,&controlled,&ray,id ? focus.obj.obj : nullptr,Direct(owner,controlled,ray)};
    reinterpret_cast<void(__fastcall*)(const void*,int,void*)>(image+kWalk)(manager,team,&search);
    if(!Alive(owner) || !Alive(controlled) || At<const void*>(image,kTeams)!=manager ||
        At<const void*>(manager,0x38)!=rows)return false;
    if(search.selected.actor) {
        auto& selected=search.selected;
        if(!Alive(selected.actor))return false;
        if(id) {
            // Still the object the reference found (the walk ran game code since).
            if(!Same(selected.actor,focus.obj) || !StillNetRef(id,NetLife::live,focus))return false;
            std::memcpy(selected.ref,id,32);
        } else if(!EncodeNetRef(selected.actor,selected.ref))std::memset(selected.ref,0,32);   // offline: no network name
        if(selected.lockIndex>=0) {
            ObjRef lock{};float point[3]{};
            if(!Lock(selected.actor,selected.lockIndex,&lock,point) || !Same(lock,selected.lock))return false;
        }
    }
    *out=search.selected;return true;
}
// Acquire the replacement before publishing it; retain the old native field's
// exact owned token in the lease until its release actually succeeds.
bool Store(void* object,unsigned at,const ObjRef& next,ObjRef& retired) noexcept {
    const ObjRef prior{At<const void*>(object,at),At<const void*>(object,at+8)};
    if(!Readable(static_cast<unsigned char*>(object)+at,16,true) || !WritableNow(static_cast<unsigned char*>(object)+at,16) ||
        !WeakWritable(prior) || !WeakWritable(next) || !WeakWritable(retired))return false;
    if(!Drop(retired))return false;
    if(Same(prior,next))return true;
    Hold(next);retired=prior;
    Put<const void*>(object,at,next.obj);Put<const void*>(object,at+8,next.ctrl);
    return Drop(retired);
}
void Notify(void* object,const void* target) noexcept {
    auto* bytes=static_cast<unsigned char*>(object);
    if(!reinterpret_cast<bool(__fastcall*)(void*)>(image+kOnline)(bytes+0x120) || (bytes[0x128]&1) || !bytes[0x591] || bytes[kDead])return;
    // This is the stock GOB retarget message path 54C18C..54C1EB. Serializer is
    // virtual slot 37 so each concrete native family keeps its own wire contract.
    alignas(16) unsigned char stream[0x610]{};
    reinterpret_cast<void*(__fastcall*)(void*,int)>(image+kStreamCtor)(stream,0x40);
    reinterpret_cast<bool(__fastcall*)(void*,signed char)>(image+kStreamByte)(stream,1);
    const auto* vtable=At<void* const*>(bytes,0);
    reinterpret_cast<void(__fastcall*)(void*,void*,const void*)>(vtable[37])(bytes,stream,target);
    const auto* network=At<void* const*>(bytes,0x120);
    reinterpret_cast<void(__fastcall*)(void*,void*,bool)>(network[14])(bytes+0x120,stream,true);
    reinterpret_cast<void(__fastcall*)(void*)>(image+kStreamDtor)(stream);
}
} // namespace

bool Install() noexcept {
    __try {
        ready=Matches(kWalk,kWalkSig,sizeof(kWalkSig)) && Matches(kBodyObject,kBodySig,sizeof(kBodySig)) &&
            Matches(kNativeSet,kSetSig,sizeof(kSetSig)) && Matches(kStreamCtor,kCtorSig,sizeof(kCtorSig)) &&
            Matches(kStreamByte,kByteSig,sizeof(kByteSig)) && Matches(kStreamDtor,kDtorSig,sizeof(kDtorSig)) &&
            Matches(kOnline,kOnlineSig,sizeof(kOnlineSig));
        return ready;
    } __except(EXCEPTION_EXECUTE_HANDLER){ready=false;return false;}
}
bool Pick(const ObjRef& owner,const ObjRef& controlled,const Ray& ray,Selection* out) noexcept {
    __try {return SearchTarget(owner,controlled,ray,nullptr,out);} __except(EXCEPTION_EXECUTE_HANDLER){if(out)*out={};return false;}
}
bool Resolve(const ObjRef& owner,const ObjRef& controlled,const unsigned char id[32],const Ray& ray,Selection* out) noexcept {
    __try {return SearchTarget(owner,controlled,ray,id,out);} __except(EXCEPTION_EXECUTE_HANDLER){if(out)*out={};return false;}
}
bool Normalize(const ObjRef& owner,const ObjRef& controlled,unsigned char id[32],const Ray& ray,Selection* out) noexcept {
    if(out)*out={};
    if(!id)return false;
    __try {
        if(!Frame(owner,controlled,ray)){std::memset(id,0,32);return false;}
        Selection next{};
        if(!Canonical(id) || !SearchTarget(owner,controlled,ray,id,&next) || !next.actor)std::memset(id,0,32);
        else if(out)*out=next;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){std::memset(id,0,32);return false;}
}
bool Valid(const ObjRef& owner,const ObjRef& controlled,const Ray& ray,const Selection& selected) noexcept {
    __try {
        if(!Frame(owner,controlled,ray) || !Hostile(controlled,selected.actor) || Same(owner,selected.actor))return false;
        if(!Zero(selected.ref) && (!Canonical(selected.ref) || !Named(selected.actor,selected.ref)))return false;
        float point[3]{};ObjRef lock{};
        if(selected.lockIndex>=0) {
            if(!Lock(selected.actor,selected.lockIndex,&lock,point) || !Same(lock,selected.lock))return false;
        } else {
            if(selected.lock.obj || selected.lock.ctrl || At<std::uint64_t>(selected.actor.obj,0x378))return false;
            std::memcpy(point,static_cast<const unsigned char*>(selected.actor.obj)+kPosition,12);
        }
        return (Score(ray,point)>=0.0f || Direct(owner,controlled,ray)==selected.actor.obj) && Visible(owner,controlled,selected.actor,ray,point);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool TryAssign(Selection& retained,const Selection& next) noexcept {
    __try {
        if(!Readable(&retained,sizeof(retained),true) || !WritableNow(&retained,sizeof(retained)) ||
            !Readable(&next,sizeof(next)) || !WeakWritable(retained.actor) || !WeakWritable(retained.lock) ||
            !WeakWritable(retained.retiredActor) || !WeakWritable(retained.retiredLock) ||
            !WeakWritable(next.actor) || !WeakWritable(next.lock))return false;
        const Selection desired=next;
        if(!Drop(retained.retiredActor) || !Drop(retained.retiredLock))return false;
        // Each newly acquired pin is journaled before the next possible native
        // counter mutation, then the old pair becomes the retirement journal.
        Hold(desired.actor);retained.retiredActor=desired.actor;
        Hold(desired.lock);retained.retiredLock=desired.lock;
        const auto oldActor=retained.actor,oldLock=retained.lock;
        retained.actor=retained.retiredActor;retained.lock=retained.retiredLock;
        retained.retiredActor=oldActor;retained.retiredLock=oldLock;
        std::memcpy(retained.point,desired.point,12);retained.lockIndex=desired.lockIndex;std::memcpy(retained.ref,desired.ref,32);
        const bool actor=Drop(retained.retiredActor),lock=Drop(retained.retiredLock);
        return actor && lock;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool TryClear(Selection& retained) noexcept {
    __try {
        if(!Readable(&retained,sizeof(retained),true) || !WritableNow(&retained,sizeof(retained)) ||
            !WeakWritable(retained.actor) || !WeakWritable(retained.lock) ||
            !WeakWritable(retained.retiredActor) || !WeakWritable(retained.retiredLock))return false;
        const bool actor=Drop(retained.actor),lock=Drop(retained.lock);
        const bool oldActor=Drop(retained.retiredActor),oldLock=Drop(retained.retiredLock);
        if(!actor || !lock || !oldActor || !oldLock)return false;
        retained={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Assign(Selection& retained,const Selection& next) noexcept {TryAssign(retained,next);}
void Clear(Selection& retained) noexcept {TryClear(retained);}
bool CopyLock(const Selection& selected,void* out16) noexcept {
    __try {
        if(!out16 || !Readable(out16,16,true) || !Alive(selected.actor))return false;
        ObjRef lock{};float point[3]{};
        if(!Lock(selected.actor,selected.lockIndex,&lock,point) || !Same(lock,selected.lock) || !WeakWritable(lock) || !WritableNow(out16,16))return false;
        Hold(lock);std::memcpy(out16,&lock,16);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Begin(const ObjRef& controlled,NativeLease* lease) noexcept {
    if(!lease || lease->active)return false;
    __try {
        if(!ready || !Alive(controlled) || !WeakWritable(controlled) || !Readable(controlled.obj,0x598,true) ||
            !WritableNow(static_cast<const unsigned char*>(controlled.obj)+0x518,0x20) || !WritableNow(lease,sizeof(*lease)))return false;
        const ObjRef current{At<const void*>(controlled.obj,0x518),At<const void*>(controlled.obj,0x520)};
        const ObjRef forced{At<const void*>(controlled.obj,0x528),At<const void*>(controlled.obj,0x530)};
        if(!WeakWritable(current) || !WeakWritable(forced))return false;
        Hold(controlled);Hold(current);Hold(forced);
        lease->root=controlled;lease->savedCurrent=current;lease->savedForced=forced;lease->active=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Apply(NativeLease& lease,const Selection* selection) noexcept {
    __try {
        if(!ready || !lease.active || !Alive(lease.root) || !WritableNow(&lease,sizeof(lease)) ||
            !WeakWritable(lease.root) || !WeakWritable(lease.applied) || !WeakWritable(lease.retiredApplied))return false;
        ObjRef next{};
        if(selection && selection->actor) {
            if(!Hostile(lease.root,selection->actor))return false;
            if(!Zero(selection->ref) && (!Canonical(selection->ref) || !Named(selection->actor,selection->ref)))return false;
            next=selection->actor;
        }
        void* object=const_cast<void*>(lease.root.obj);
        const ObjRef current{At<const void*>(object,0x518),At<const void*>(object,0x520)};
        const ObjRef forced{At<const void*>(object,0x528),At<const void*>(object,0x530)};
        if(!WeakWritable(current) || !WeakWritable(forced) || !WeakWritable(next) ||
            !WritableNow(static_cast<unsigned char*>(object)+0x518,0x20))return false;
        const bool changed=!Same(current,next);
        if(!Same(current,next) || !Same(forced,next)) {
            // 364EB0 takes a by-value weak argument: two field acquisitions and
            // one input release. This extra weak pin is consumed by that call.
            ObjRef argument=next;Hold(argument);
            reinterpret_cast<void(__fastcall*)(void*,const ObjRef*)>(image+kNativeSet)(object,&argument);
        }
        if(!WeakAssign(lease.applied,next,lease.retiredApplied))return false;
        if(changed)Notify(object,next.obj);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Restore(NativeLease& lease) noexcept {
    if(!lease.active)return true;
    __try {
        // Validate every retained counter before any field or count mutation.
        // A temporarily read-only control is not proof that its actor is gone.
        if(!WritableNow(&lease,sizeof(lease)) || !WeakWritable(lease.root) || !WeakWritable(lease.applied) ||
            !WeakWritable(lease.savedCurrent) || !WeakWritable(lease.savedForced) || !WeakWritable(lease.retiredCurrent) ||
            !WeakWritable(lease.retiredForced) || !WeakWritable(lease.retiredApplied))return false;
        if(lease.root.ctrl && At<LONG>(lease.root.ctrl,8)>0 && !Readable(lease.root.obj,kSelfCtrl+8))return false;
        if(lease.root.ctrl && Current(lease.root)) {
            void* object=const_cast<void*>(lease.root.obj);
            if(!Readable(object,0x538,true) || !WritableNow(static_cast<unsigned char*>(object)+0x518,0x20))return false;
            const ObjRef before{At<const void*>(object,0x518),At<const void*>(object,0x520)};
            const ObjRef forced{At<const void*>(object,0x528),At<const void*>(object,0x530)};
            if(!WeakWritable(before) || !WeakWritable(forced))return false;
            if(!Store(object,0x518,lease.savedCurrent,lease.retiredCurrent) ||
                !Store(object,0x528,lease.savedForced,lease.retiredForced))return false;
            if(!Same(before,lease.savedCurrent))Notify(object,Alive(lease.savedCurrent) ? lease.savedCurrent.obj : nullptr);
        }
        const bool applied=Drop(lease.applied),current=Drop(lease.savedCurrent),forced=Drop(lease.savedForced);
        const bool oldCurrent=Drop(lease.retiredCurrent),oldForced=Drop(lease.retiredForced),oldApplied=Drop(lease.retiredApplied);
        if(!applied || !current || !forced || !oldCurrent || !oldForced || !oldApplied)return false;
        if(!Drop(lease.root))return false;
        lease={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
