// Steam 678CCB46: Boss GunBattery final Unit outcomes. The outer native
// launcher still advances counters and muzzle effects on every peer. Only the
// final 84C5A ->1194280 projectile factory is authoritative. Stock Boss
// TypeBullet descriptors have no persistent +70 muzzle; unknown live-matrix
// variants are refused rather than being claimed as birth-only synchronization.
#include "enemy_control_boss_gun_net.h"
#include "enemy_control_action_native.h"
#include "enemy_control_gun_factory.h"
#include "enemy_control_projectile_native.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_BOSS_GUN_NET_TEST
void* BossGunNetTestFunction(unsigned) noexcept;
#endif
namespace boss_gun_net {
using Result=enemy_control::ActionResult;
constexpr std::uint32_t kTag=0x31474242u; // BBG1
constexpr unsigned kRoots=8,kGuns=128,kQueued=1024,kMaxParts=128;
constexpr std::size_t kHeader=8,kMaximum=512;
constexpr std::size_t kEventBytes=8+2+8+4+128+kProjectileWords*4+kProjectileBytes+2+12+64+4+3;
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Binding {
    Ref part{},gun{},maker{};void* factory=nullptr;unsigned char* descriptor=nullptr;
    std::uint64_t digest=0;bool held=false,partHeld=false,gunHeld=false,makerHeld=false;unsigned char padding[4]{};alignas(16) float muzzle[16]{};
};
struct Event {std::uint64_t serial=0,digest=0;unsigned key=0;std::uint32_t nativeId=0xFFFFFFFFu;NativeProjectileSample sample{};};
struct Receipt {std::uint64_t serial=0,hash=0;};
struct Stream {
    Ref root{};const NativeProfile* profile=nullptr;bool held=false,rootHeld=false,authority=false,quiescing=false,faulted=false,retired=false,applying=false,restoring=false,cleanupUncertain=false;
    unsigned guns=0,head=0,count=0;std::uint64_t next=1,applied=0;
    Binding bindings[kGuns]{};NativeProjectileJournal projectile{};Event queue[kQueued]{};Receipt receipts[kQueued]{};
};
struct Scope {Scope* previous=nullptr;Stream* stream=nullptr;Binding* binding=nullptr;unsigned key=0;const void* root=nullptr;bool suppress=true,seen=false;};
// Reinitialize only a fully released receipt tombstone in place. Avoid a
// queue-sized aggregate assignment temporary on the native update stack.
void ResetStream(Stream& s) noexcept {
    static_assert(std::is_trivially_destructible_v<Stream>);::new(static_cast<void*>(&s)) Stream{};
}
thread_local Scope* activeScope=nullptr;
Stream streams[kRoots]{};unsigned char* admittedImage=nullptr;bool installed=false;
struct Sig {unsigned rva;unsigned char bytes[16];};
constexpr Sig kSigs[]={
    {0x6D05C0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x57,0x41,0x54,0x41}},
    {0x6CF7D0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x48,0x83,0xEC}},
    {0x84C40,{0x40,0x56,0x48,0x83,0xEC,0x20,0x49,0x8B,0xC1,0x4D,0x8B,0xD0,0x4C,0x8B,0x4C,0x24}},
    {0x1001D0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x48,0x89,0x4C,0x24,0x08,0x57}},
};
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_BOSS_GUN_NET_TEST
    return reinterpret_cast<T>(BossGunNetTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Family(const NativeLease& l) noexcept {
    if(!l.profile || !Readable(l.profile,sizeof(*l.profile)))return false;
    std::size_t count=0;const auto* p=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(l.profile==p+i)return p[i].vtableRva==0x17BEC68;
    return false;
}
bool Current(Ref r) noexcept {
    return Readable(r.object,0x38) && Readable(r.control,16,true) && At<LONG>(r.control,8)>0 && At<const void*>(r.object,0x30)==r.control;
}
bool Strong(Ref r,std::size_t bytes) noexcept {return Readable(r.object,bytes,true) && Readable(r.control,16,true) && At<LONG>(r.control,8)>0;}
bool Pin(Ref r) noexcept {
    if(!Readable(r.control,16,true))return false;auto* uses=reinterpret_cast<volatile LONG*>(r.control+8);LONG before=*uses;
    while(before>0 && before<(std::numeric_limits<LONG>::max)()) {const LONG actual=InterlockedCompareExchange(uses,before+1,before);if(actual==before)return true;before=actual;}
    return false;
}
bool CanUnpin(Ref r) noexcept {const auto vt=Readable(r.control,16,true) ? At<void* const*>(r.control,0) : nullptr;return Readable(vt,16) && vt[0] && vt[1] && At<LONG>(r.control,8)>0;}
void Unpin(Ref r) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8))!=0)return;
    auto* const* vt=At<void* const*>(r.control,0);reinterpret_cast<void(__fastcall*)(void*)>(vt[0])(r.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12))==0)reinterpret_cast<void(__fastcall*)(void*)>(vt[1])(r.control);
}
// Clear ownership before entering a possibly final native destructor. Its
// side effects cannot be rolled back after SEH, so a failed decrement is never
// retried and the stream remains a faulted cleanup record.
bool DropStrong(Stream& s,Ref r,bool* held) noexcept {
    if(!*held)return true;if(s.cleanupUncertain || !CanUnpin(r))return false;
    *held=false;s.cleanupUncertain=true;
    __try {Unpin(r);s.cleanupUncertain=false;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){s.faulted=true;return false;}
}
Ref RootRef(const NativeLease& l) noexcept {return {l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))};}
NativeLease Lease(const Stream& s) noexcept {NativeLease l{};l.object=s.root.object;l.control=s.root.control;l.profile=s.profile;return l;}
Stream* FindRoot(const void* root) noexcept {for(auto& s:streams)if(s.held && s.root.object==root)return &s;return nullptr;}
Stream* Find(const NativeLease& l) noexcept {auto* s=FindRoot(l.object);return s && s->root.control==l.control && s->profile==l.profile ? s : nullptr;}
bool SameCol(const void* object,unsigned rva) noexcept {
    if(!Readable(object,8) || !Readable(image+rva-8,8))return false;const auto* table=At<const unsigned char*>(object,0);
    return Readable(table-8,8) && At<const void*>(table-8,0)==At<const void*>(image+rva-8,0);
}
std::uint64_t HashBytes(std::uint64_t hash,const void* bytes,std::size_t size) noexcept {const auto* p=static_cast<const unsigned char*>(bytes);for(std::size_t i=0;i<size;++i){hash^=p[i];hash*=1099511628211ull;}return hash;}
std::uint64_t Number(std::uint64_t hash,std::uint64_t value,unsigned size=8) noexcept {for(unsigned i=0;i<size;++i){const auto b=static_cast<unsigned char>(value>>(i*8));hash=HashBytes(hash,&b,1);}return hash;}
bool NameHash(const unsigned char* value,std::uint64_t* hash) noexcept {
    if(!Readable(value,0x20))return false;const auto size=At<std::size_t>(value,0x10),capacity=At<std::size_t>(value,0x18);
    if(!size || size>256 || capacity<size)return false;const auto* name=capacity<8 ? value : At<const unsigned char*>(value,0);
    if(!Readable(name,(size+1)*2) || At<std::uint16_t>(name,size*2))return false;
    *hash=HashBytes(*hash,name,(size+1)*2);return true;
}
bool Managed(Ref r,std::size_t bytes) noexcept {return Strong(r,bytes) && Readable(r.control,24) && At<const void*>(r.control,0x10)==r.object;}
bool BindingReady(const Stream& s,const Binding& b) noexcept {
    if(!b.held || !b.partHeld || !b.gunHeld || !b.makerHeld || !s.rootHeld || !Current(s.root) || !Current(b.part) || !Managed(b.gun,0x300) || !Managed(b.maker,0x280))return false;
    const auto* g=b.gun.object;const auto* h=g+0x20;
    return At<const void*>(g,0x200)==s.root.object && At<const void*>(g,0x208)==s.root.control &&
        At<const void*>(g,0x210)==b.part.object && At<const void*>(g,0x218)==b.part.control &&
        At<const void*>(b.part.object,0xAE8)==s.root.object && At<const void*>(b.part.object,0xAF0)==s.root.control &&
        At<const void*>(h,0x10)==s.root.object && At<const void*>(h,0x18)==s.root.control &&
        At<const void*>(h,0x20)==b.maker.object && At<const void*>(h,0x28)==b.maker.control &&
        At<const void*>(b.maker.object,0)==image+0x17E6EE0 && At<void*>(b.maker.object,0x28)==b.factory &&
        b.descriptor==b.maker.object+0xA0 && Readable(b.factory,8) && At<const void*>(b.descriptor,0)==image+0x1769028 && !At<const void*>(b.descriptor,0x70);
}
bool KeepBindings(Stream& s) noexcept {
    if(!Current(s.root) || !Readable(s.root.object,0x17F8))return false;
    const auto* head=At<const unsigned char*>(s.root.object,0x17E8);const auto count=At<std::size_t>(s.root.object,0x17F0);
    if(count>kMaxParts || !Readable(head,16))return false;const auto* node=At<const unsigned char*>(head,0);const void* previous=head;unsigned used=0,walked=0;
    for(;node!=head;++walked) {
        if(walked>=count || !Readable(node,0x50) || At<const void*>(node,8)!=previous)return false;
        Ref part{At<unsigned char*>(node,0x40),At<unsigned char*>(node,0x48)};
        if(Current(part) && SameCol(part.object,0x17BFAE0)) {
            if(!Readable(part.object,0xB78) || At<const void*>(part.object,0xAE8)!=s.root.object || At<const void*>(part.object,0xAF0)!=s.root.control)return false;
            const auto n=At<std::size_t>(part.object,0xB70);const auto* rows=At<const unsigned char*>(part.object,0xB60);
            if(n>kGuns-used || (n && !Readable(rows,n*16)))return false;
            for(unsigned j=0;j<n;++j) {
                Ref gun{At<unsigned char*>(rows+j*16,0),At<unsigned char*>(rows+j*16,8)};
                if(!Strong(gun,0x300))return false;const auto* h=gun.object+0x20;
                Ref maker{At<unsigned char*>(h,0x20),At<unsigned char*>(h,0x28)};
                if(!Strong(maker,0x280) || At<const void*>(maker.object,0)!=image+0x17E6EE0)return false;
                auto hash=Number(1469598103934665603ull,s.profile->profileHash,4);
                if(!NameHash(node+0x10,&hash))return false;hash=Number(Number(hash,walked,2),j,2);
                for(auto offset:{0u,4u})hash=Number(hash,At<unsigned>(gun.object,offset),4);
                for(auto offset:{0x30u,0x3Cu,0x40u,0x44u,0x48u})hash=Number(hash,At<unsigned>(h,offset),4);
                auto& b=s.bindings[used];
                if(b.held) {
                    if(b.part.object!=part.object || b.part.control!=part.control || b.gun.object!=gun.object || b.gun.control!=gun.control || b.maker.object!=maker.object || b.maker.control!=maker.control || b.digest!=hash || !BindingReady(s,b))return false;
                } else {
                    b.part=part;b.gun=gun;b.maker=maker;b.factory=At<void*>(maker.object,0x28);b.descriptor=maker.object+0xA0;b.digest=hash;
                    // Keep partial acquisition in the stream too: a later Pin
                    // failure must not enter an unjournaled rollback destructor.
                    if(!Pin(part))return false;b.partHeld=true;
                    if(!Pin(gun))return false;b.gunHeld=true;
                    if(!Pin(maker))return false;b.makerHeld=true;b.held=true;
                    if(!BindingReady(s,b))return false;
                }
                ++used;
            }
        }
        previous=node;node=At<const unsigned char*>(node,0);
    }
    if(walked!=count || At<const void*>(head,8)!=previous || (s.guns && s.guns!=used))return false;s.guns=used;return used!=0;
}
bool Ready() noexcept {
    if(!image)return false;if(admittedImage)return admittedImage==image;
    for(const auto& sig:kSigs)if(!Readable(image+sig.rva,16) || std::memcmp(image+sig.rva,sig.bytes,16))return false;
    if(!Readable(image+0x17E6EE0,32) || At<const void*>(image+0x17E6EE0,8)!=image+0x6CF7D0 || At<const void*>(image+0x17E6EE0,0x18)!=image+0x6D0460)return false;
    admittedImage=image;return true;
}
Stream* Bind(const NativeLease& l,bool authority) noexcept {
    if(!installed || !Ready() || !Family(l) || !Current(RootRef(l)))return nullptr;
    for(auto& old:streams)if(old.held && old.retired) {bool role=false;if(!NativeActionRole(old.root.object,&role) || (old.root.object==l.object && !NativeActionQuiescing(l.object)))ResetStream(old);}
    if(auto* s=Find(l))return s->authority==authority ? s : nullptr;
    for(auto& s:streams)if(!s.held) {if(!Pin(RootRef(l)))return nullptr;s.root=RootRef(l);s.profile=l.profile;s.held=s.rootHeld=true;s.authority=authority;return &s;}
    return nullptr;
}
bool Needs(const NativeLease& l) noexcept {return Family(l);}
bool Before(const NativeLease& l,bool authority,Input*) noexcept {
    __try {auto* s=Bind(l,authority);if(!s || s->restoring || s->cleanupUncertain)return false;if(s->quiescing || s->retired)return true;if(s->faulted || !KeepBindings(*s)){s->faulted=true;return false;}return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool After(const NativeLease& l) noexcept {const auto* s=Find(l);return s && (!s->faulted || s->quiescing);}
bool RootRole(const void* root,bool* authority) noexcept {
    return root && SameCol(root,0x17BEC68) && NativeActionRole(root,authority);
}
bool GunRoot(const void* holder,const void** root) noexcept {
    if(!holder || !root)return false;
    for(auto& s:streams)if(s.held)for(const auto& binding:s.bindings)if(binding.gun.object && (binding.held || s.restoring || s.cleanupUncertain) && binding.gun.object+0x20==holder){*root=s.root.object;return true;}
    const auto* h=static_cast<const unsigned char*>(holder);const auto* g=h-0x20;
    if(!Readable(g,0x220))return false;const auto* r=At<const unsigned char*>(g,0x200);const auto* c=At<const unsigned char*>(g,0x208);
    if(!Current({const_cast<unsigned char*>(r),const_cast<unsigned char*>(c)}) || !SameCol(r,0x17BEC68) ||
       At<const void*>(h,0x10)!=r || At<const void*>(h,0x18)!=c)return false;
    *root=r;return true;
}
bool OwnsGun(const void* holder) noexcept {
    __try {const void* root=nullptr;bool authority=false;if(!GunRoot(holder,&root))return false;const auto* s=FindRoot(root);return (s && (s->restoring || s->cleanupUncertain)) || RootRole(root,&authority);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void FaultGun(const void* holder) noexcept {const void* root=nullptr;if(GunRoot(holder,&root))if(auto* s=FindRoot(root))s->faulted=true;}
void InvokeGun(void* holder,const float* matrix,std::uint64_t* seed) {
    const void* root=nullptr;bool authority=false;if(!GunRoot(holder,&root) || !RootRole(root,&authority))return;
    auto* s=FindRoot(root);if(s && (s->restoring || s->cleanupUncertain))return;Binding* binding=nullptr;unsigned key=0;
    if(s)for(;key<s->guns;++key)if(s->bindings[key].held && s->bindings[key].gun.object+0x20==holder){binding=s->bindings+key;break;}
    const bool valid=s && !s->retired && binding && s->authority==authority && BindingReady(*s,*binding) && Readable(matrix,64) && Readable(seed,8,true);
    if(s && !s->retired && !valid)s->faulted=true;
    Scope scope{};scope.previous=activeScope;scope.stream=s;scope.binding=binding;scope.key=key;scope.root=root;
    scope.suppress=!valid || !authority || s->quiescing || s->faulted;
    if(activeScope && activeScope->root==root){if(s && !s->retired)s->faulted=true;scope.suppress=true;}
    if(valid && authority && !scope.suppress && (s->count==kQueued || !s->next || s->next==(std::numeric_limits<std::uint64_t>::max)())){s->faulted=true;scope.suppress=true;}
    // Even a role whose extension stream has just restored remains claimed:
    // run its native counters/FX, but no real projectile may leak through.
    activeScope=&scope;
    __try {InvokeOriginalNativeGunFactory(holder,matrix,seed);}
    __finally {activeScope=scope.previous;}
    if(valid && !scope.seen && !scope.suppress)s->faulted=true;
}
bool DescriptorRoot(const void* descriptor,const void** root) noexcept {
    if(!Readable(descriptor,0x48) || At<const void*>(descriptor,0)!=image+0x1769028)return false;
    auto* r=At<unsigned char*>(descriptor,0x38);auto* c=At<unsigned char*>(descriptor,0x40);
    if(!Current({r,c}) || !SameCol(r,0x17BEC68))return false;*root=r;return true;
}
bool OwnsUnit(const void* factory,const void* descriptor) noexcept {
    __try {
        // Even a native destructor callback whose root strong count just hit
        // zero remains claimed by its exact persistent cleanup record.
        if(Readable(descriptor,0x48) && At<const void*>(descriptor,0)==image+0x1769028)
            for(const auto& s:streams)if(s.held && (s.restoring || s.cleanupUncertain) && At<const void*>(descriptor,0x38)==s.root.object && At<const void*>(descriptor,0x40)==s.root.control)return true;
        if(activeScope && activeScope->binding && activeScope->binding->factory==factory && activeScope->binding->descriptor==descriptor) {
            bool authority=false;return RootRole(activeScope->root,&authority);
        }
        const void* root=nullptr;bool authority=false;return DescriptorRoot(descriptor,&root) && RootRole(root,&authority);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void FaultUnit(const void*,const void* descriptor) noexcept {
    const void* root=nullptr;if(activeScope && activeScope->stream)activeScope->stream->faulted=true;
    else if(DescriptorRoot(descriptor,&root))if(auto* s=FindRoot(root))s->faulted=true;
}
bool EventValid(const Event& e) noexcept {return e.serial && e.digest && e.key<kGuns && e.nativeId==0xFFFFFFFFu && !e.sample.hasMuzzle && ValidNativeProjectileSample(e.sample);}
void* InvokeUnit(void* manager,const float* matrix,void* factory,void* descriptor) {
    auto* scope=activeScope;
    if(!scope || !scope->stream || !scope->binding || scope->binding->factory!=factory || scope->binding->descriptor!=descriptor) {FaultUnit(factory,descriptor);return nullptr;}
    auto& s=*scope->stream;auto& b=*scope->binding;
    if(scope->seen){s.faulted=true;return nullptr;}scope->seen=true;
    if(scope->suppress || s.quiescing || s.faulted || s.restoring || s.cleanupUncertain || !s.authority)return nullptr;
    if(!BindingReady(s,b) || s.count==kQueued || !s.next || s.next==(std::numeric_limits<std::uint64_t>::max)()) {s.faulted=true;return nullptr;}
    Event event{};event.serial=s.next;event.key=scope->key;event.digest=b.digest;
    if(!CaptureNativeProjectileSample(Lease(s),b.descriptor,matrix,descriptor,&event.sample) || !EventValid(event)){s.faulted=true;return nullptr;}
    void* result=InvokeOriginalNativeUnitFactory(manager,matrix,factory,descriptor);
    if(!result)return nullptr; // a refused/failed factory is never an accepted outcome
    if(!Readable(result,0x12C) || At<unsigned>(result,0x128)!=0xFFFFFFFFu){s.faulted=true;return result;}
    s.queue[(s.head+s.count)%kQueued]=event;++s.count;++s.next;return result;
}
void PutWire(unsigned char*& p,std::uint64_t v,unsigned n) noexcept {for(unsigned i=0;i<n;++i)*p++=static_cast<unsigned char>(v>>(i*8));}
std::uint64_t GetWire(const unsigned char*& p,unsigned n) noexcept {std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(*p++)<<(i*8);return v;}
void PutFloats(unsigned char*& p,const float* values,unsigned n) noexcept {for(unsigned i=0;i<n;++i){std::uint32_t v=0;std::memcpy(&v,values+i,4);PutWire(p,v,4);}}
void GetFloats(const unsigned char*& p,float* values,unsigned n) noexcept {for(unsigned i=0;i<n;++i){const auto v=static_cast<std::uint32_t>(GetWire(p,4));std::memcpy(values+i,&v,4);}}
void Write(unsigned char*& p,const Event& e) noexcept {
    PutWire(p,e.serial,8);PutWire(p,e.key,2);PutWire(p,e.digest,8);PutWire(p,e.nativeId,4);const auto& a=e.sample;
    PutFloats(p,a.matrix,16);PutFloats(p,a.muzzleMatrix,16);for(auto v:a.scalar)PutWire(p,v,4);for(auto v:a.bytes)*p++=v;
    PutWire(p,a.scalar178,2);PutWire(p,a.random,4);PutWire(p,a.shot,4);PutWire(p,a.flags,4);
    for(auto v:a.target)*p++=v;for(auto v:a.extra)*p++=v;PutWire(p,a.lock,4);*p++=a.hasMuzzle ? 1 : 0;*p++=a.extraObject ? 1 : 0;*p++=a.resourceMask;
}
bool Owns(const void* bytes,std::size_t size) noexcept {if(!Readable(bytes,4) || size<4)return false;const auto* p=static_cast<const unsigned char*>(bytes);return GetWire(p,4)==kTag;}
bool Encode(const Event& e,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;if(!used || !EventValid(e) || capacity<kHeader+kEventBytes || !Readable(bytes,kHeader+kEventBytes,true))return false;
    auto* p=static_cast<unsigned char*>(bytes);PutWire(p,kTag,4);*p++=1;*p++=1;PutWire(p,0,2);Write(p,e);
    *used=static_cast<std::size_t>(p-static_cast<unsigned char*>(bytes));return *used==kHeader+kEventBytes && *used<=kMaximum;
}
bool Decode(const void* bytes,std::size_t size,Event* out) noexcept {
    if(!out || size!=kHeader+kEventBytes || size>kMaximum || !Readable(bytes,size))return false;
    const auto* p=static_cast<const unsigned char*>(bytes);if(GetWire(p,4)!=kTag || *p++!=1 || *p++!=1 || GetWire(p,2))return false;
    Event e{};e.serial=GetWire(p,8);e.key=static_cast<unsigned>(GetWire(p,2));e.digest=GetWire(p,8);e.nativeId=static_cast<unsigned>(GetWire(p,4));auto& a=e.sample;
    GetFloats(p,a.matrix,16);GetFloats(p,a.muzzleMatrix,16);for(auto& v:a.scalar)v=static_cast<unsigned>(GetWire(p,4));for(auto& v:a.bytes)v=*p++;
    a.scalar178=static_cast<std::uint16_t>(GetWire(p,2));a.random=static_cast<unsigned>(GetWire(p,4));a.shot=static_cast<unsigned>(GetWire(p,4));a.flags=static_cast<unsigned>(GetWire(p,4));
    for(auto& v:a.target)v=*p++;for(auto& v:a.extra)v=*p++;a.lock=static_cast<unsigned>(GetWire(p,4));const unsigned muzzle=*p++,extra=*p++;a.resourceMask=*p++;
    if(muzzle>1 || extra>1 || a.resourceMask>3)return false;a.hasMuzzle=muzzle!=0;a.extraObject=extra!=0;
    if(!EventValid(e) || p!=static_cast<const unsigned char*>(bytes)+size)return false;*out=e;return true;
}
std::uint64_t Hash(const Event& e) noexcept {unsigned char bytes[kEventBytes]{};auto* p=bytes;Write(p,e);return HashBytes(1469598103934665603ull,bytes,sizeof(bytes));}
bool Capture(const NativeLease& l,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;auto* s=Find(l);if(!s || !used || !s->authority || s->restoring || s->cleanupUncertain || !Current(s->root) || (s->faulted && !s->quiescing))return false;
    if(!s->count)return true;if(!Encode(s->queue[s->head],bytes,capacity,used))return false;s->head=(s->head+1)%kQueued;--s->count;return true;
}
Result Apply(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    bool authority=false;if(!NativeActionRole(l.object,&authority) || authority)return Result::failed;
    auto* s=Find(l);Event e{};if(!s || s->authority || !Decode(bytes,size,&e))return Result::failed;
    if(e.serial<=s->applied){const auto& receipt=s->receipts[e.serial%kQueued];return receipt.serial==e.serial && receipt.hash==Hash(e) ? Result::applied : Result::failed;}
    if(s->applying)return Result::pending;if(s->retired || s->faulted || s->restoring || s->cleanupUncertain)return Result::failed;
    if(e.serial!=s->applied+1)return Result::pending;if(e.key>=s->guns)return Result::pending;
    auto& b=s->bindings[e.key];if(!BindingReady(*s,b) || b.digest!=e.digest)return Result::failed;
    bool prepared=false;void* result=nullptr;s->applying=true;
    __try {prepared=CanReplayNativeProjectileSample(l,b.factory,b.descriptor,b.muzzle,e.sample);if(prepared)result=ReplayNativeProjectileSampleInPlace(s->projectile,l,b.factory,b.descriptor,b.muzzle,e.sample);}
    __finally {s->applying=false;}
    if(!prepared)return Result::pending;
    if(!result || !NativeProjectileJournalEmpty(s->projectile)){s->faulted=true;return Result::failed;}
    if(!Readable(result,0x12C) || At<unsigned>(result,0x128)!=e.nativeId) {
        if(Readable(result,0x38))Fn<void(__fastcall*)(void*)>(0x118A1B0)(result);s->faulted=true;return Result::failed;
    }
    // 6CF83E's separate muzzle effect already ran in the local native tick.
    // Bullet_TypeBullet has no persistent bullet cache, list or NetID poststep.
    s->applied=e.serial;s->receipts[e.serial%kQueued]={e.serial,Hash(e)};return Result::applied;
}
bool Quiesce(const NativeLease& l) noexcept {if(auto* s=Find(l))s->quiescing=true;return true;}
bool Empty(const NativeLease& l) noexcept {const auto* s=Find(l);return !s || (!s->count && !s->applying && !s->restoring && !s->cleanupUncertain && !(activeScope && activeScope->root==l.object) && NativeProjectileJournalEmpty(s->projectile));}
bool Restore(const NativeLease& l) noexcept {
    auto* s=Find(l);if(!s)return true;if(s->restoring || s->cleanupUncertain)return false;if(s->retired)return true;
    if(s->count || s->applying || (activeScope && activeScope->root==l.object))return false;
    s->restoring=true;
    __try {
        __try {
            if(!RestoreNativeProjectileJournal(s->projectile) || !NativeProjectileJournalEmpty(s->projectile) || (s->rootHeld && !CanUnpin(s->root)))return false;
            for(const auto& b:s->bindings)if((b.partHeld && !CanUnpin(b.part)) || (b.gunHeld && !CanUnpin(b.gun)) || (b.makerHeld && !CanUnpin(b.maker)))return false;
            for(auto& b:s->bindings) {
                if(!DropStrong(*s,b.maker,&b.makerHeld) || !DropStrong(*s,b.gun,&b.gunHeld) || !DropStrong(*s,b.part,&b.partHeld))return false;
            }
            if(!DropStrong(*s,s->root,&s->rootHeld))return false;for(auto& b:s->bindings)b={};s->guns=0;s->retired=true;s->quiescing=true;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){s->faulted=s->cleanupUncertain=true;return false;}
    } __finally {s->restoring=false;}
}
bool Abort(const NativeLease& l) noexcept {
    if(auto* s=Find(l)){s->quiescing=true;if(s->restoring || s->cleanupUncertain || s->applying || (activeScope && activeScope->root==l.object))return false;s->head=s->count=0;}return Restore(l);
}
} // namespace boss_gun_net
bool InstallBossGunAcceptedProjectiles() noexcept {
    using namespace boss_gun_net;
    __try {
        if(!Ready())return false;
        const NativeGunFactoryProvider gun{kTag,OwnsGun,InvokeGun,FaultGun};
        const NativeUnitFactoryProvider unit{kTag,OwnsUnit,InvokeUnit,FaultUnit};
        if(!RegisterNativeGunFactoryProvider(gun) || !RegisterNativeUnitFactoryProvider(unit) || !RegisterNativeUnitFactoryCallsite(0x84C5A) ||
           !InstallNativeGunFactoryDispatcher() || !InstallNativeUnitFactoryDispatcher() || !NativeGunFactoryProviderReady(gun) ||
           !NativeUnitFactoryProviderReady(unit) || !NativeUnitFactoryCallsiteReady(0x84C5A))return false;
        const NativeActionExtension extension{kTag,Needs,Before,After,Capture,Owns,Apply,Quiesce,Empty,Abort,Restore};
        if(!RegisterNativeActionExtension(extension))return false;installed=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew::enemy_native
