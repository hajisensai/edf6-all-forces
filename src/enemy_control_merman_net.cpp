// EDF.dll 678CCB46. Actual Merman GunBattery2 outcomes at 6D331D.
// Local MAB/battery state keeps ticking on guests; only the real Unit factory
// is suppressed. No process addresses or padded C++ records cross the wire.
#include "enemy_control_merman_net.h"
#include "enemy_control_action_native.h"
#include "enemy_control_gun_factory.h"
#include "enemy_control_projectile_native.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <limits>
#include <type_traits>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_MERMAN_NET_TEST
void* MermanNetTestFunction(unsigned) noexcept;
bool MermanNetTestDefinition(const void*,unsigned,std::uint64_t*) noexcept;
#endif
namespace merman_net {
using Result=enemy_control::ActionResult;
constexpr unsigned kVtable=0x17B5370,kSite=0x6D331D,kRoots=8,kParts=2,kMuzzles=128,kQueued=1024;
constexpr std::uint32_t kTag=0x31524D45u; // EMR1
constexpr unsigned kWireBytes=4+4+8+8+4+32*4+kProjectileWords*4+kProjectileBytes+2+12+64+4+3+9+8;
static_assert(kWireBytes<=512,"one bounded actual projectile per accepted packet");
struct Print {unsigned rva;unsigned char bytes[16];};
constexpr Print prints[]={
 {0x3AA2D0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
 {0x3AA430,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x60,0x48,0x8B,0x81,0x80,0x00,0x00}},
 {0x3A92A0,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9}},
 {0x3A9470,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
 {0x6D51C0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55}},
 {0x6D2640,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55}},
 {0x1194280,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x49,0x8B,0xF1,0x48,0x8B}},
 {0x784510,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x48,0x83,0xEC,0x20,0x48}},
 {0x7639B0,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
 {0x12D85B0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xEB,0x0F,0x48,0x8B,0xCB,0xE8,0x47}},
 {0x118A1B0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48}}
};
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Part {Ref owner{},battery{};std::uint64_t definition=0;bool ownerHeld=false,batteryHeld=false;};
enum class EventKind:unsigned char {birth=0,refresh=1,stop=2};
struct Event {EventKind kind=EventKind::birth;std::uint64_t serial=0,definition=0,spawn=0;unsigned part=0,muzzle=0,id=UINT32_MAX,nextCounter=0,group=0;bool generator=false;NativeProjectileSample sample{};};
struct Pending {Event event{};Ref bullet{};bool held=false,complete=false;};
struct Receipt {std::uint64_t serial=0,hash=0;};
struct Beam {
    alignas(16) float persistent[16]{};
    Ref ref{};std::uint64_t spawn=0,definition=0;unsigned part=0,muzzle=0,id=UINT32_MAX;
    bool held=false,stopped=false,published=false,ready=false,stepping=false,detached=false;
    unsigned char reserved[2]{};float input[16]{},output[16]{};unsigned char padding[12]{};
};
struct Stream {
    Ref root{};const NativeProfile* profile=nullptr;bool held=false,retired=false,authority=false,faulted=false,quiescing=false,executing=false,cleanupUncertain=false;
    Beam beams[kParts*kMuzzles]{};Event waiting{};std::uint64_t waitingHash=0;bool waitingHeld=false,waitingReady=false,aborting=false;unsigned beamDepth=0;NativeProjectileJournal journal{};Ref postBullet{};bool postHeld=false,postCommitted=false,postDeleted=false,postPending=false;Event postEvent{};std::uint64_t postHash=0;void* postNode=nullptr;Part parts[kParts]{};std::uint64_t next=1,applied=0;unsigned head=0,count=0;
    Pending queue[kQueued]{};Receipt receipts[kQueued]{};
};
static_assert(std::is_trivially_copyable_v<Stream>,"stream reset requires a value-only local ledger");
void ResetStream(Stream& s) noexcept {
    // Avoid two nested half-megabyte aggregate temporaries in Before/Bind on
    // the game's Windows thread stack. Only a fully released slot may reset.
    std::memset(&s,0,sizeof(s));s.next=1;
}
Stream streams[kRoots]{};unsigned char* admittedImage=nullptr;bool installed=false;struct UpdateScope {void* battery=nullptr;UpdateScope* previous=nullptr;};thread_local UpdateScope* updating=nullptr;
bool TrackBeam(Stream&,const Event&,Ref) noexcept;bool ActiveBeams(const Stream&) noexcept;bool ReleaseBeams(Stream&) noexcept;void ScanBeams(Stream&) noexcept;
using VoidFn=void(__fastcall*)(void*);
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_MERMAN_NET_TEST
    return reinterpret_cast<T>(MermanNetTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Shared(Ref r,std::size_t bytes) noexcept {
    return r.object && r.control && r.object==r.control+16 && Readable(r.control,16,true) &&
        At<LONG>(r.control,8)>0 && Readable(r.object,bytes,true);
}
bool Current(Ref r) noexcept {
    return r.object && Readable(r.object,0x1C30,true) && Readable(r.control,16,true) &&
        At<LONG>(r.control,8)>0 && At<const void*>(r.object,0x30)==r.control;
}
bool CanUnpin(Ref r) noexcept {
    if(!Readable(r.control,16,true) || At<LONG>(r.control,8)<=0)return false;
    const auto* table=At<void* const*>(r.control,0);return Readable(table,16) && table[0] && table[1];
}
bool Pin(Ref r) noexcept {
    if(!CanUnpin(r))return false;auto* uses=reinterpret_cast<volatile LONG*>(r.control+8);LONG value=*uses;
    while(value>0 && value<(std::numeric_limits<LONG>::max)()) {
        const LONG was=InterlockedCompareExchange(uses,value+1,value);if(was==value)return true;value=was;
    }
    return false;
}
void Unpin(Ref r) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8)))return;
    const auto* table=At<void* const*>(r.control,0);reinterpret_cast<VoidFn>(table[0])(r.control);
    if(!InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12)))reinterpret_cast<VoidFn>(table[1])(r.control);
}
bool DropStrong(Stream& s,Ref ref,bool* held) noexcept {
    if(!*held)return true;if(s.cleanupUncertain || !CanUnpin(ref))return false;
    // Ownership is cleared before entering a possibly final native destructor.
    // SEH then poisons the stream instead of decrementing this same ref twice.
    *held=false;s.cleanupUncertain=true;
    __try {Unpin(ref);s.cleanupUncertain=false;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){s.faulted=true;return false;}
}
bool FinishPostReference(Stream& s,bool rollback) noexcept {
    if(!s.postHeld)return !s.cleanupUncertain;
    if(s.cleanupUncertain)return false;
    __try {
        if(rollback && !s.postCommitted && !s.postDeleted) {
            s.cleanupUncertain=true;Fn<VoidFn>(0x118A1B0)(s.postBullet.object);s.cleanupUncertain=false;s.postDeleted=true;
        }
        if(!DropStrong(s,s.postBullet,&s.postHeld))return false;
        s.postBullet={};s.postCommitted=s.postDeleted=false;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){s.faulted=true;s.cleanupUncertain=true;return false;}
}
bool DiscardPendingPost(Stream& s) noexcept {
    if(s.cleanupUncertain || !FinishPostReference(s,false))return false;
    if(s.postNode) {
        auto* node=s.postNode;s.postNode=nullptr;s.cleanupUncertain=true;
        __try {Fn<void(__fastcall*)(void*,std::size_t)>(0x12D85EC)(node,0x20);s.cleanupUncertain=false;}
        __except(EXCEPTION_EXECUTE_HANDLER){s.faulted=true;return false;}
    }
    s.postPending=false;s.postEvent={};s.postHash=0;return true;
}
void DropWeak(unsigned char* control) {
    if(control && !InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+12)))
        reinterpret_cast<VoidFn>(At<void* const*>(control,0)[1])(control);
}
bool Family(const NativeLease& l) noexcept {
    if(!l.profile || l.profile->vtableRva!=kVtable || !l.profile->name || std::strcmp(l.profile->name,"EDF6_Merman"))return false;
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(l.profile==profiles+i)return true;
    return false;
}
NativeLease Lease(const Stream& s) noexcept {NativeLease l{};l.object=s.root.object;l.control=s.root.control;l.profile=s.profile;return l;}
Stream* Find(const NativeLease& l) noexcept {
    for(auto& s:streams)if((s.held || s.retired || s.cleanupUncertain || !NativeProjectileJournalEmpty(s.journal)) && s.root.object==l.object && s.root.control==l.control && s.profile==l.profile)return &s;
    return nullptr;
}
bool RoleLease(const void* descriptor,NativeLease* l,bool* authority) noexcept {
    if(!descriptor || !l || !authority || !Readable(descriptor,0x48))return false;
    l->object=At<unsigned char*>(descriptor,0x38);l->control=At<const void*>(descriptor,0x40);
    if(!Current({l->object,static_cast<unsigned char*>(const_cast<void*>(l->control))}) ||
       !NativeActionRole(l->object,authority))return false;
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(profiles[i].vtableRva==kVtable) {l->profile=profiles+i;break;}
    if(!Family(*l))return false;
    const auto* table=At<const void* const*>(l->object,0);
    return table && Readable(table-1,sizeof(void*)) && table[-1]==At<const void*>(image+kVtable-sizeof(void*),0);
}
// Hash the local immutable DSGO subtree. Overrides and cycles fail closed;
// expressions hash their VM bytes, never a current difficulty-dependent value.
struct Definition {
    const unsigned char* tree=nullptr;const unsigned char* raw=nullptr;
    unsigned nodes=0,visited=0,depth=0,path[32]{};std::size_t bytes=0;std::uint64_t hash=14695981039346656037ull;
};
bool Mix(Definition& d,const void* bytes,std::size_t count) noexcept {
    if(count>4*1024*1024-d.bytes || (count && !Readable(bytes,count)))return false;
    const auto* p=static_cast<const unsigned char*>(bytes);
    for(std::size_t i=0;i<count;++i){d.hash^=p[i];d.hash*=1099511628211ull;}d.bytes+=count;return true;
}
bool Number(Definition& d,unsigned n) noexcept{return Mix(d,&n,4);}
const unsigned char* Raw(const Definition& d,std::int64_t offset,std::size_t size) noexcept {
    if(offset<0 || offset>16*1024*1024 || size>16*1024*1024-static_cast<std::size_t>(offset))return nullptr;
    const auto address=reinterpret_cast<std::uintptr_t>(d.raw);
    if(address>UINTPTR_MAX-static_cast<std::uintptr_t>(offset))return nullptr;
    const auto* p=reinterpret_cast<const unsigned char*>(address+static_cast<std::uintptr_t>(offset));
    return Readable(p,size) ? p : nullptr;
}
std::int64_t Offset(const Definition& d,const unsigned char* p) noexcept {
    return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(p)-reinterpret_cast<std::uintptr_t>(d.raw));
}
bool Text(Definition& d,std::int64_t at) noexcept {
    unsigned count=0;for(;count<=1024;++count){const auto* p=Raw(d,at+count*2,2);if(!p)return false;if(!At<std::uint16_t>(p,0))break;}
    return count<=1024 && Number(d,count) && Mix(d,Raw(d,at,count*2),count*2);
}
bool Node(Definition& d,unsigned index) noexcept {
    if(index>=d.nodes || ++d.visited>4096 || d.depth==32)return false;
    for(unsigned i=0;i<d.depth;++i)if(d.path[i]==index)return false;
    const auto mapped=At<std::size_t>(d.tree,0x18);if(mapped>1048576)return false;
    if(index<mapped){const auto* map=At<const unsigned char*>(d.tree,8);if(!Readable(map,index*4+4) || At<unsigned>(map,index*4)!=UINT32_MAX)return false;}
    const auto* node=Raw(d,16+static_cast<std::int64_t>(index)*16,16);if(!node)return false;
    const auto type=At<unsigned>(node,8);if(type>4 || !Number(d,type))return false;
    if(!type)return Mix(d,node,8);
    const auto at=Offset(d,node)+At<std::int32_t>(node,0);
    if(type==1)return Text(d,at);
    const auto* block=Raw(d,at,type==3 ? 16u : 8u);if(!block)return false;
    if(type!=3){const auto n=At<unsigned>(block,0);return n<=1024*1024 && Number(d,n) && Mix(d,Raw(d,at+At<std::int32_t>(block,4),n),n);}
    const auto count=At<unsigned>(block,12),names=At<unsigned>(block,4);
    if(count>1024 || names>count || !Number(d,count))return false;
    const auto* children=Raw(d,at+At<std::int32_t>(block,8),count*4);
    const auto* keys=Raw(d,at+At<std::int32_t>(block,0),names*8);
    if((count && !children) || (names && !keys))return false;
    for(unsigned i=0;i<names;++i)if(At<unsigned>(keys+i*8,4)>=count)return false;
    d.path[d.depth++]=index;bool ok=true;
    for(unsigned i=0;i<count && ok;++i) {
        const unsigned char* key=nullptr;
        for(unsigned n=0;n<names;++n)if(At<unsigned>(keys+n*8,4)==i){if(key){ok=false;break;}key=keys+n*8;}
        if(ok)ok=Number(d,key ? 1u : 0u) && (!key || Text(d,Offset(d,key)+At<std::int32_t>(key,0))) && Node(d,At<unsigned>(children,i*4));
    }
    --d.depth;return ok;
}
bool DefinitionOf(const void* battery,unsigned part,std::uint64_t* value) noexcept {
#ifdef ENEMY_CONTROL_MERMAN_NET_TEST
    return MermanNetTestDefinition(battery,part,value);
#else
    Definition d{};d.tree=At<const unsigned char*>(battery,0x2A0);const auto index=At<unsigned>(battery,0x2A8);
    if(!Readable(d.tree,0x58))return false;d.raw=At<const unsigned char*>(d.tree,0x50);
    if(!Readable(d.raw,16) || At<unsigned>(d.raw,0)!=0x4F475344 || At<unsigned>(d.raw,4)!=16 || At<unsigned>(d.raw,12)!=16)return false;
    d.nodes=At<unsigned>(d.raw,8);constexpr char domain[]="EDF6.Merman.GunBattery2.1";
    if(!d.nodes || d.nodes>65536 || !Mix(d,domain,sizeof(domain)) || !Number(d,part) || !Node(d,index) || !d.hash)return false;
    *value=d.hash;return true;
#endif
}
bool Parts(Stream& s) noexcept {
    if(!Current(s.root))return false;
    for(unsigned i=0;i<kParts;++i) {
        const unsigned offset=0x1C10+i*16;Ref owner{At<unsigned char*>(s.root.object,offset),At<unsigned char*>(s.root.object,offset+8)};
        const std::size_t size=i ? 0x280u : 0x90u;
        if(!Shared(owner,size))return false;
        const unsigned pair=i ? 0xF0u : 0u;
        Ref battery{At<unsigned char*>(owner.object,pair),At<unsigned char*>(owner.object,pair+8)};
        if(!Shared(battery,0x6C0) || At<const void*>(battery.object,0x2B0)!=s.root.object ||
           At<const void*>(battery.object,0x2B8)!=s.root.control || !At<void*>(battery.object,0x2F0))return false;
        const auto count=At<std::size_t>(battery.object,0x288),capacity=At<std::size_t>(battery.object,0x280);
        if(!count || count>kMuzzles || count>capacity || !Readable(At<void*>(battery.object,0x278),count*0xD0,true))return false;
        if(const auto* generator=At<const unsigned char*>(battery.object,0x658))if(!Readable(generator,8) || At<unsigned>(generator,4)>255u || At<unsigned>(generator,0)>=0xFFFFFFu)return false;auto& p=s.parts[i];std::uint64_t digest=0;if(!DefinitionOf(battery.object,i,&digest))return false;
        if(p.ownerHeld || p.batteryHeld) {
            if(!p.ownerHeld || !p.batteryHeld || p.owner.object!=owner.object || p.owner.control!=owner.control ||
               p.battery.object!=battery.object || p.battery.control!=battery.control || p.definition!=digest)return false;
        } else {
            if(!Pin(owner))return false;p.owner=owner;p.ownerHeld=true;
            if(!Pin(battery))return false;p.battery=battery;p.batteryHeld=true;p.definition=digest;
        }
    }
    return true;
}
Stream* Bind(const NativeLease& l,bool authority) noexcept {
    if(!installed || image!=admittedImage || !Family(l) || !Current({l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))}))return nullptr;
    if(auto* s=Find(l))return !s->retired && s->authority==authority ? s : nullptr;
    for(auto& s:streams)if(!s.held && !s.executing && !s.beamDepth && !s.cleanupUncertain && NativeProjectileJournalEmpty(s.journal) && !s.postHeld) {
        ResetStream(s);const Ref ref{l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))};if(!Pin(ref))return nullptr;
        s.root=ref;s.profile=l.profile;s.held=true;s.authority=authority;return &s;
    }
    return nullptr;
}
unsigned char* Muzzle(const Part& p,unsigned ordinal) noexcept {
    if(!Shared(p.battery,0x6C0) || ordinal>=At<std::size_t>(p.battery.object,0x288))return nullptr;
    auto* first=At<unsigned char*>(p.battery.object,0x278);return Readable(first,(ordinal+1)*0xD0,true) ? first+ordinal*0xD0 : nullptr;
}
bool Before(const NativeLease& l,bool authority,Input*) noexcept {
    __try {
        if(NativeActionQuiescing(l.object)){if(auto* old=Find(l))old->quiescing=true;return true;}
        if(auto* old=Find(l))if(old->retired){if(old->executing || old->beamDepth)return false;ResetStream(*old);}
        auto* s=Find(l);if(!s)s=Bind(l,authority);if(s && s->authority)ScanBeams(*s);
        if(!s || s->authority!=authority || s->faulted || s->quiescing || s->executing || s->beamDepth || s->cleanupUncertain)return false;
        return s->postPending || (NativeProjectileJournalEmpty(s->journal) && !s->postHeld && Parts(*s));
    }
    __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
void Put(unsigned char*& p,std::uint64_t n,unsigned bytes) noexcept {for(unsigned i=0;i<bytes;++i)*p++=static_cast<unsigned char>(n>>(8*i));}
std::uint64_t Get(const unsigned char*& p,unsigned bytes) noexcept {std::uint64_t n=0;for(unsigned i=0;i<bytes;++i)n|=std::uint64_t(*p++)<<(8*i);return n;}
void Floats(unsigned char*& p,const float* v,unsigned count) noexcept {for(unsigned i=0;i<count;++i){std::uint32_t n=0;std::memcpy(&n,v+i,4);Put(p,n,4);}}
void Floats(const unsigned char*& p,float* v,unsigned count) noexcept {for(unsigned i=0;i<count;++i){const auto n=static_cast<std::uint32_t>(Get(p,4));std::memcpy(v+i,&n,4);}}
bool Valid(const Event& e) noexcept {
    if(!e.serial || !e.definition || !e.spawn || e.part>=kParts || e.muzzle>=kMuzzles)return false;
    if(e.kind==EventKind::birth)return e.spawn==e.serial && e.sample.hasMuzzle &&
        (e.generator ? (e.group<=255u && e.nextCounter<=0xFFFFFFu) : (!e.nextCounter && !e.group)) && ValidNativeProjectileSample(e.sample);
    if(e.spawn>=e.serial || e.generator || e.nextCounter || e.group)return false;
    if(e.kind==EventKind::refresh)return e.sample.hasMuzzle && ValidNativeProjectileSample(e.sample);
    return e.kind==EventKind::stop && e.sample.flags<=1;
}
std::size_t EventBytes(const Event& e) noexcept {return e.kind==EventKind::birth ? kWireBytes : e.kind==EventKind::refresh ? 164u : 37u;}
bool Encode(const Event& e,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;const auto size=EventBytes(e);
    if(!used || !bytes || capacity<size || !Readable(bytes,size,true) || !Valid(e))return false;
    auto* p=static_cast<unsigned char*>(bytes);Put(p,kTag,4);Put(p,2,1);Put(p,e.part,1);Put(p,e.muzzle,1);Put(p,static_cast<unsigned char>(e.kind),1);
    Put(p,e.serial,8);Put(p,e.definition,8);Put(p,e.id,4);Put(p,e.spawn,8);const auto& v=e.sample;
    if(e.kind==EventKind::stop)Put(p,v.flags,1);
    else if(e.kind==EventKind::refresh){Floats(p,v.matrix,16);Floats(p,v.muzzleMatrix,16);}
    else {
        Put(p,e.generator,1);Put(p,e.nextCounter,4);Put(p,e.group,4);
        Floats(p,v.matrix,16);Floats(p,v.muzzleMatrix,16);for(auto n:v.scalar)Put(p,n,4);for(auto n:v.bytes)*p++=n;
        Put(p,v.scalar178,2);Put(p,v.random,4);Put(p,v.shot,4);Put(p,v.flags,4);
        for(auto n:v.target)*p++=n;for(auto n:v.extra)*p++=n;Put(p,v.lock,4);Put(p,v.hasMuzzle,1);Put(p,v.extraObject,1);Put(p,v.resourceMask,1);
    }
    *used=static_cast<std::size_t>(p-static_cast<unsigned char*>(bytes));return *used==size;
}
bool Decode(const void* bytes,std::size_t size,Event* e) noexcept {
    if(!e || !bytes || size<36 || !Readable(bytes,size))return false;*e={};const auto* p=static_cast<const unsigned char*>(bytes);
    if(Get(p,4)!=kTag || Get(p,1)!=2)return false;e->part=static_cast<unsigned>(Get(p,1));e->muzzle=static_cast<unsigned>(Get(p,1));e->kind=static_cast<EventKind>(Get(p,1));
    if(e->kind!=EventKind::birth && e->kind!=EventKind::refresh && e->kind!=EventKind::stop)return false;
    if(size!=EventBytes(*e))return false;
    e->serial=Get(p,8);e->definition=Get(p,8);e->id=static_cast<unsigned>(Get(p,4));e->spawn=Get(p,8);auto& v=e->sample;
    if(e->kind==EventKind::stop)v.flags=static_cast<unsigned>(Get(p,1));
    else if(e->kind==EventKind::refresh){Floats(p,v.matrix,16);Floats(p,v.muzzleMatrix,16);v.hasMuzzle=true;}
    else {
        const auto generator=Get(p,1);if(generator>1)return false;e->generator=generator!=0;e->nextCounter=static_cast<unsigned>(Get(p,4));e->group=static_cast<unsigned>(Get(p,4));
        Floats(p,v.matrix,16);Floats(p,v.muzzleMatrix,16);for(auto& n:v.scalar)n=static_cast<unsigned>(Get(p,4));for(auto& n:v.bytes)n=*p++;
        v.scalar178=static_cast<std::uint16_t>(Get(p,2));v.random=static_cast<unsigned>(Get(p,4));v.shot=static_cast<unsigned>(Get(p,4));v.flags=static_cast<unsigned>(Get(p,4));
        for(auto& n:v.target)n=*p++;for(auto& n:v.extra)n=*p++;v.lock=static_cast<unsigned>(Get(p,4));
        const auto muzzle=Get(p,1),extra=Get(p,1);if(muzzle>1 || extra>1)return false;v.hasMuzzle=muzzle!=0;v.extraObject=extra!=0;v.resourceMask=static_cast<unsigned char>(Get(p,1));
    }
    return Valid(*e);
}
std::uint64_t Hash(const Event& e) noexcept {
    unsigned char bytes[512]{};std::size_t size=0;if(!Encode(e,bytes,sizeof(bytes),&size))return 0;
    std::uint64_t h=14695981039346656037ull;for(std::size_t i=0;i<size;++i){h^=bytes[i];h*=1099511628211ull;}return h;
}
bool Owns(const void* bytes,std::size_t size) noexcept {return bytes && size>=4 && Readable(bytes,4) && At<std::uint32_t>(bytes,0)==kTag;}
bool Needs(const NativeLease& l) noexcept {return Family(l);}

Beam* BeamAt(Stream& s,std::uint64_t spawn) noexcept {
    for(auto& b:s.beams)if(b.held && b.spawn==spawn)return &b;return nullptr;
}
bool BeamCurrent(const Beam& b) noexcept {
    return b.held && CanUnpin(b.ref) && Readable(b.ref.object,0xE78,true) &&
        At<const void*>(b.ref.object,0x30)==b.ref.control;
}
bool MatrixAt(const void* source,float out[16]) noexcept {
    if(!Readable(source,64))return false;NativeProjectileSample sample{};sample.hasMuzzle=true;
    std::memcpy(sample.matrix,source,64);std::memcpy(sample.muzzleMatrix,source,64);
    if(!ValidNativeProjectileSample(sample))return false;std::memcpy(out,source,64);return true;
}
bool TrackBeam(Stream& s,const Event& e,Ref ref) noexcept {
    if(At<unsigned>(ref.object,0xC30)!=1)return true;
    if(auto* old=BeamAt(s,e.spawn))return old->ref.object==ref.object && old->ref.control==ref.control;
    auto* muzzle=Muzzle(s.parts[e.part],e.muzzle);
    if(!muzzle || At<const void*>(ref.object,0xB20)!=muzzle+0x70 ||
       !Readable(At<const void*>(ref.object,0xD30),0x80,true) || (At<unsigned>(ref.object,0xC34)&0x4000u) ||
       At<const void*>(ref.object,0xAE8)!=s.root.object || At<const void*>(ref.object,0xAF0)!=s.root.control)return false;
    for(auto& b:s.beams)if(!b.held) {
        if(!Pin(ref))return false;b={};b.ref=ref;b.held=true;b.spawn=e.spawn;b.definition=e.definition;b.part=e.part;b.muzzle=e.muzzle;b.id=e.id;
        b.ready=!s.authority;std::memcpy(b.input,e.sample.muzzleMatrix,64);std::memcpy(b.output,e.sample.matrix,64);if(!s.authority){std::memcpy(b.persistent,e.sample.muzzleMatrix,64);crew::Put<float*>(ref.object,0xB20,b.persistent);b.detached=true;}return true;
    }
    return false;
}
bool ActiveBeams(const Stream& s) noexcept {for(const auto& b:s.beams)if(b.held && !b.stopped)return true;return false;}
bool RehomeBeam(Beam& b) noexcept {
    if(!b.detached)return true;
    if(!BeamCurrent(b) || !b.stopped || (!(At<unsigned>(b.ref.object,0xC34)&0x4000u) && !(b.ref.object[0x18]&4u)) ||
       At<const void*>(b.ref.object,0xB20)!=b.persistent)return false;
    auto* owned=At<float*>(b.ref.object,0xD30);
    if(!Readable(owned,64) || (reinterpret_cast<std::uintptr_t>(owned)&15u))return false;
    // Native D30 has the bullet's full lifetime. Rehome before a private Beam
    // slot can be released/reused; tail/getter retains no controller storage.
    crew::Put<float*>(b.ref.object,0xB20,owned);b.detached=false;return true;
}
bool ReleaseBeams(Stream& s) noexcept {
    if(ActiveBeams(s))return false;
    for(auto& b:s.beams)if(b.held){if(!RehomeBeam(b) || !DropStrong(s,b.ref,&b.held))return false;b={};}
    return true;
}
bool QueueBeam(Stream& s,Beam& b,EventKind kind,const float* input=nullptr,const float* output=nullptr,bool terminal=false) noexcept {
    Event e{};e.kind=kind;e.part=b.part;e.muzzle=b.muzzle;e.definition=b.definition;e.spawn=b.spawn;e.id=b.id;
    if(kind==EventKind::refresh){if(!input || !output)return false;e.sample.hasMuzzle=true;std::memcpy(e.sample.muzzleMatrix,input,64);std::memcpy(e.sample.matrix,output,64);}
    else e.sample.flags=terminal ? 1u : 0u;
    // Only the unsent adjacent tail can be replaced. Published serial/hash
    // pairs remain immutable, including when multiple muzzles interleave.
    if(kind==EventKind::refresh && s.count) {
        auto& prior=s.queue[(s.head+s.count-1)%kQueued];
        if(prior.complete && prior.event.kind==kind && prior.event.spawn==b.spawn){e.serial=prior.event.serial;if(!Valid(e))return false;prior.event=e;return true;}
    }
    if(s.count==kQueued || !s.next){s.faulted=true;return false;}
    e.serial=s.next;if(!Valid(e)){s.faulted=true;return false;}
    auto& p=s.queue[(s.head+s.count)%kQueued];p={};p.event=e;p.complete=true;++s.count;if(++s.next==0)s.faulted=true;return true;
}
void ScanBeams(Stream& s) noexcept {
    for(auto& b:s.beams)if(b.held && !b.stopped && b.ready) {
        if(b.stepping)continue;if(!BeamCurrent(b)){s.faulted=true;continue;}
        if(At<unsigned>(b.ref.object,0xC34)&0x4000u){if(s.aborting)b.stopped=true;else if(s.authority){if(QueueBeam(s,b,EventKind::stop))b.stopped=true;}continue;}if(b.ref.object[0x18]&4u) {
            if(s.authority && !s.aborting){if(!QueueBeam(s,b,EventKind::stop,nullptr,nullptr,true))continue;b.stopped=true;}
            else if(s.aborting)b.stopped=true;
        }
    }
}
bool BeamOwner(const void* bullet,Stream** stream,Beam** beam) noexcept {
    *stream=nullptr;*beam=nullptr;if(!Readable(bullet,0xAF8))return false;
    const auto* ctrl=At<const void*>(bullet,0x30);
    for(auto& s:streams)for(auto& b:s.beams)if(b.held && b.ref.object==bullet && b.ref.control==ctrl){*stream=&s;*beam=&b;return true;}
    alignas(16) unsigned char descriptor[0x48]{};
    crew::Put<const void*>(descriptor,0x38,At<const void*>(bullet,0xAE8));crew::Put<const void*>(descriptor,0x40,At<const void*>(bullet,0xAF0));
    NativeLease l{};bool authority=false;if(!RoleLease(descriptor,&l,&authority))return false;*stream=Find(l);return true;
}
bool OwnsBeam(const void* bullet) noexcept {
    __try {Stream* s=nullptr;Beam* b=nullptr;return installed && image==admittedImage && BeamOwner(bullet,&s,&b);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void BeamFault(const void* bullet) noexcept {
    __try {Stream* s=nullptr;Beam* b=nullptr;if(BeamOwner(bullet,&s,&b) && s)s->faulted=true;}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
bool RefreshAccepted(void* bullet) {
    if(!Readable(bullet,0xE78,true) || At<unsigned>(bullet,0xC30)!=1 || (At<unsigned>(bullet,0xC34)&0x4001u))return false;
    InvokeOriginalNativeBeamRefresh(bullet);return (At<unsigned>(bullet,0xC34)&0x6000u)==0x2000u;
}
void BeamRefresh(void* bullet) {
    Stream* s=nullptr;Beam* b=nullptr;if(!BeamOwner(bullet,&s,&b))return;
    if(!s || !b){if(s)s->faulted=true;return;}
    if(!s->authority || s->quiescing || s->aborting || s->faulted || s->retired || b->stopped)return;if(At<unsigned>(bullet,0xC34)&1u)return;if(At<unsigned>(bullet,0xC34)&0x4000u){ScanBeams(*s);return;}
    if(!BeamCurrent(*b) || !RefreshAccepted(bullet))s->faulted=true;
}
bool ConsumerGate(const void* bullet) noexcept {
    if(!At<std::uint64_t>(bullet,0xB38))return true;const auto* guard=At<const unsigned char*>(bullet,0xB30);
    return Readable(guard,12) && At<LONG>(guard,8)>0;
}
void BeamStep(void* bullet,void* dt) {
    Stream* s=nullptr;Beam* b=nullptr;if(!BeamOwner(bullet,&s,&b)){InvokeOriginalNativeBeamStep(bullet,dt);return;}
    if(s)++s->beamDepth;__try {
    if(!s || !b) {
        // A closing role must not revive an old untracked beam. Native stop
        // and tail retraction still execute through the real consumer.
        if(Readable(bullet,0xC38,true))crew::Put<unsigned>(bullet,0xC34,At<unsigned>(bullet,0xC34)&~0x2000u);
        InvokeOriginalNativeBeamStep(bullet,dt);return;
    }
    if(!BeamCurrent(*b) || b->stepping){s->faulted=true;return;}
    if(b->stopped){InvokeOriginalNativeBeamStep(bullet,dt);return;}
    if(b->ref.object[0x18]&4u){if(s->authority)ScanBeams(*s);else s->faulted=true;return;}
    b->stepping=true;
    __try {
        const auto before=At<unsigned>(bullet,0xC34);
        const bool wanted=s->waitingHeld && s->waiting.spawn==b->spawn;
        const bool stop=s->aborting || (s->authority && (s->quiescing || s->faulted)) || (!s->authority && wanted && s->waiting.kind==EventKind::stop);
        float input[16]{},output[16]{};bool consumed=false;
        if(At<unsigned>(bullet,0xC30)!=1){s->faulted=true;return;}if(before&0x4000u){if(s->authority || s->aborting){if(s->aborting || QueueBeam(*s,*b,EventKind::stop))b->stopped=true;}else s->faulted=true;InvokeOriginalNativeBeamStep(bullet,dt);return;}if((before&1u) && !stop){if(!s->authority)s->faulted=true;InvokeOriginalNativeBeamStep(bullet,dt);return;}
        if(stop)crew::Put<unsigned>(bullet,0xC34,before&~0x2000u);
        else if(!s->authority) {
            auto* matrix=At<float*>(bullet,0xB20);auto* muzzle=Muzzle(s->parts[b->part],b->muzzle);
            if(!muzzle || !b->detached || matrix!=b->persistent || !Readable(matrix,64,true)){s->faulted=true;return;}
            const auto* accepted=wanted && s->waiting.kind==EventKind::refresh ? s->waiting.sample.muzzleMatrix : b->input;
            std::memcpy(matrix,accepted,64);if(!RefreshAccepted(bullet)){s->faulted=true;return;}
        }
        const auto flags=At<unsigned>(bullet,0xC34);
        consumed=!stop && !(flags&1u) && (flags&0x6000u)==0x2000u && ConsumerGate(bullet) && MatrixAt(At<const void*>(bullet,0xB20),input);
        InvokeOriginalNativeBeamStep(bullet,dt);
        const auto after=At<unsigned>(bullet,0xC34);
        if(after&0x4000u) {if(b->stopped)return;
            if(s->authority || s->aborting) {
                if(!s->aborting && !QueueBeam(*s,*b,EventKind::stop))return;b->stopped=true;
            } else if(wanted && s->waiting.kind==EventKind::stop && stop) {b->stopped=true;s->waitingReady=true;}
            else s->faulted=true;
            return;
        }
        if(stop){s->faulted=true;return;}
        if(!consumed || !MatrixAt(At<const void*>(bullet,0xD30),output))return;
        if(s->authority) {
            if(!b->ready){s->faulted=true;return;}
            if(!b->published || std::memcmp(input,b->input,64) || std::memcmp(output,b->output,64)) {
                if(!QueueBeam(*s,*b,EventKind::refresh,input,output))return;b->published=true;
            }
            std::memcpy(b->input,input,64);std::memcpy(b->output,output,64);
        } else if(wanted && s->waiting.kind==EventKind::refresh) {
            if(std::memcmp(output,s->waiting.sample.matrix,64)){s->faulted=true;return;}
            std::memcpy(b->input,input,64);std::memcpy(b->output,output,64);s->waitingReady=true;
        }
    } __finally {b->stepping=false;}
    } __finally {if(s)--s->beamDepth;}
}
Result ApplyBeam(Stream& s,const Event& e,std::uint64_t hash) {
    auto* b=BeamAt(s,e.spawn);
    if(!b || !BeamCurrent(*b))return Result::pending;
    if(b->part!=e.part || b->muzzle!=e.muzzle || b->definition!=e.definition || b->id!=e.id || At<unsigned>(b->ref.object,0x128)!=e.id)return Result::failed;
    if(s.waitingHeld) {
        if(s.waiting.serial!=e.serial || s.waitingHash!=hash)return Result::failed;
        if(!s.waitingReady)return Result::pending;
    } else if(e.kind==EventKind::stop && e.sample.flags==1) {
        if(b->ref.object[0x18]&4u)return Result::failed;s.cleanupUncertain=true;Fn<VoidFn>(0x118A1B0)(b->ref.object);s.cleanupUncertain=false;
        if(!(b->ref.object[0x18]&4u))return Result::failed;b->stopped=true;
    } else {
        if(b->stopped || (At<unsigned>(b->ref.object,0xC34)&0x4001u) || (b->ref.object[0x18]&4u))return Result::failed;
        s.waiting=e;s.waitingHash=hash;s.waitingHeld=true;s.waitingReady=false;return Result::pending;
    }
    if(!NativeProjectileJournalEmpty(s.journal) || s.cleanupUncertain)return Result::failed;
    if(e.kind==EventKind::stop){if(!b->stopped || !RehomeBeam(*b) || !DropStrong(s,b->ref,&b->held))return Result::failed;*b={};}
    s.receipts[e.serial%kQueued]={e.serial,hash};s.applied=e.serial;s.waiting={};s.waitingHeld=s.waitingReady=false;s.waitingHash=0;return Result::applied;
}

bool OwnsFactory(const void*,const void* descriptor) noexcept {
    __try {NativeLease l{};bool authority=false;return installed && image==admittedImage && RoleLease(descriptor,&l,&authority);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Fault(const void*,const void* descriptor) noexcept {
    __try {NativeLease l{};bool authority=false;if(RoleLease(descriptor,&l,&authority))if(auto* s=Bind(l,authority))s->faulted=true;}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
void* Invoke(void* manager,const float* matrix,void* factory,void* descriptor) {
    NativeLease l{};bool authority=false;
    if(!RoleLease(descriptor,&l,&authority))return nullptr;
    // Ownership is independent of stream lifetime. A still-live closing role
    // must suppress even after this extension's Restore released its stream.
    if(NativeActionQuiescing(l.object))return nullptr;if(auto* old=Find(l))if(old->retired)return nullptr;
    // A guest's local battery still ticks while an accepted birth waits for
    // descriptor cleanup. Its suppressed fire attempt is not another outcome.
    if(!authority)return nullptr;
    auto* s=Bind(l,authority);if(!s || s->faulted || s->quiescing || s->cleanupUncertain || !NativeProjectileJournalEmpty(s->journal) || s->postHeld || !Parts(*s)){if(s)s->faulted=true;return nullptr;}
    bool beamCapacity=false;for(const auto& b:s->beams)if(!b.held)beamCapacity=true;if(!beamCapacity || s->count==kQueued || !s->next || s->executing){s->faulted=true;return nullptr;}
    Event event{};event.serial=s->next;event.spawn=event.serial;bool found=false;
    for(unsigned i=0;i<kParts;++i) {
        const auto& part=s->parts[i];if(At<void*>(part.battery.object,0x2F0)!=factory)continue;
        const auto count=At<std::size_t>(part.battery.object,0x288);
        for(unsigned n=0;n<count;++n)if(At<const void*>(descriptor,0x70)==Muzzle(part,n)+0x70) {
            if(found){s->faulted=true;return nullptr;}found=true;event.part=i;event.muzzle=n;event.definition=part.definition;
        }
    }
    if(!updating || updating->battery!=s->parts[event.part].battery.object){s->faulted=true;return nullptr;}if(!found || !CaptureNativeProjectileSample(l,s->parts[event.part].battery.object+0x300,matrix,descriptor,&event.sample) || !Valid(event)){s->faulted=true;return nullptr;}
    s->executing=true;void* bullet=nullptr;
    __try {bullet=InvokeOriginalNativeUnitFactory(manager,matrix,factory,descriptor);}
    __finally {s->executing=false;}
    if(!bullet)return nullptr;
    Ref ref{static_cast<unsigned char*>(bullet),At<unsigned char*>(bullet,0x30)};
    if(!Readable(bullet,0xC34,true) || !Pin(ref)){s->faulted=true;if(Readable(bullet,0x38)){s->cleanupUncertain=true;Fn<VoidFn>(0x118A1B0)(bullet);s->cleanupUncertain=false;}return nullptr;}
    if(!TrackBeam(*s,event,ref)){s->faulted=true;bool held=true;DropStrong(*s,ref,&held);return bullet;}auto& pending=s->queue[(s->head+s->count)%kQueued];pending.event=event;pending.bullet=ref;pending.held=true;++s->count;
    if(++s->next==0)s->faulted=true;return bullet;
}
void __fastcall ObserveUpdate(void* battery,void* context) {
    UpdateScope scope{battery,updating};updating=&scope;bool completed=false;
    __try {Fn<void(__fastcall*)(void*,void*)>(0x6D51C0)(battery,context);completed=true;}
    __finally {
        updating=scope.previous;
        for(auto& s:streams)if(s.held && s.authority)for(unsigned n=0;n<s.count;++n) {
            auto& pending=s.queue[(s.head+n)%kQueued];
            if(pending.complete || !pending.held || s.parts[pending.event.part].battery.object!=battery)continue;
            if(!completed || !CanUnpin(pending.bullet) || !Readable(pending.bullet.object,0x12C)){s.faulted=true;continue;}
            pending.event.id=At<unsigned>(pending.bullet.object,0x128);auto* generator=At<unsigned char*>(battery,0x658);if(generator){if(!Readable(generator,8)){s.faulted=true;continue;}pending.event.generator=true;pending.event.nextCounter=At<unsigned>(generator,0);pending.event.group=At<unsigned>(generator,4);if(pending.event.group>255u || pending.event.nextCounter>0xFFFFFFu){s.faulted=true;continue;}if(pending.event.id!=UINT32_MAX){if((pending.event.id&255u)!=pending.event.group || (pending.event.id>>8)>=pending.event.nextCounter){s.faulted=true;continue;}pending.event.nextCounter=(pending.event.id>>8)+1;}}for(auto& b:s.beams)if(b.held && b.spawn==pending.event.spawn){b.id=pending.event.id;b.ready=true;}pending.complete=true;
        }
    }
}
struct UpdatePatch {unsigned rva=0;bool owned=false;unsigned char bytes[5]{};};
UpdatePatch updatePatches[]={{0x3AA41C,false,{}},{0x3AA4AC,false,{}}};
bool UpdateHooks() noexcept {
#ifdef ENEMY_CONTROL_MERMAN_NET_TEST
    return true;
#else
    for(auto& patch:updatePatches) {
        if(patch.owned){if(!Readable(image+patch.rva,5) || std::memcmp(image+patch.rva,patch.bytes,5))return false;continue;}
        bool changed=false;if(!RedirectCall(image+patch.rva,image+0x6D51C0,reinterpret_cast<void*>(&ObserveUpdate),changed))return false;
        patch.owned=true;std::memcpy(patch.bytes,image+patch.rva,5);
    }
    return true;
#endif
}
bool Capture(const NativeLease& l,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;
    __try {
        auto* s=Find(l);if(!s)return Family(l);if(s->retired)return NativeProjectileJournalEmpty(s->journal) && !s->cleanupUncertain;if(!used || !s->authority || s->cleanupUncertain || s->beamDepth || s->executing || (s->faulted && !s->quiescing))return false;
        if(!s->count)return true;auto& p=s->queue[s->head];if(!p.complete)return true;
        // This runs after the original caller's weak caches, callbacks and
        // optional 7639B0 ID registration, not at the pre-return factory hook.
        if(p.event.kind==EventKind::birth && (!p.held || !CanUnpin(p.bullet) || !Readable(p.bullet.object,0x12C)))return false;

        if(!Encode(p.event,bytes,capacity,used))return false;
        if(!DropStrong(*s,p.bullet,&p.held))return false;if(p.event.kind==EventKind::stop)for(auto& b:s->beams)if(b.held && b.spawn==p.event.spawn){if(!RehomeBeam(b) || !DropStrong(*s,b.ref,&b.held))return false;b={};}p={};s->head=(s->head+1)%kQueued;--s->count;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return false;}
}
bool PostReady(const Part& p,unsigned char* muzzle,const Event& e) noexcept {
    if(!muzzle || !Readable(muzzle,0xD0,true))return false;
    auto* battery=p.battery.object;auto* generator=At<unsigned char*>(battery,0x658);if((generator!=nullptr)!=e.generator || (generator && (!Readable(generator,8,true) || At<unsigned>(generator,4)!=e.group)))return false;auto* head=At<unsigned char*>(battery,0x290);
    if(!Readable(head,16,true) || !Readable(At<void*>(head,8),sizeof(void*),true) || At<std::size_t>(battery,0x298)>=0x7FFFFFFFFFFFFFFull)return false;
    if(const auto* old=At<const unsigned char*>(muzzle,0xB8)){if(!Readable(old,16,true) || At<LONG>(old,12)<=0)return false;const auto* vt=At<void* const*>(old,0);if(!Readable(vt,16) || !vt[1])return false;}
    const auto* callback=At<const unsigned char*>(battery,0x650);
    if(callback){const auto* vt=At<void* const*>(callback,0);if(!Readable(vt,24) || !vt[2])return false;}
    return true;
}
bool IdDestinationReady(unsigned id) {
    if(id==UINT32_MAX)return true;
    if(!Readable(image+0x20B2AC8,8) || !At<void*>(image,0x20B2AC8))return false;
    void* ref[2]{};Fn<void*(__fastcall*)(void*,void*)>(0x784510)(At<void*>(image,0x20B2AC8),ref);bool ready=false;
    __try {ready=Readable(ref[0],16) && At<const void*>(ref[0],8)!=nullptr;}
    __finally {if(ref[1])Unpin({nullptr,static_cast<unsigned char*>(ref[1])});}
    return ready;
}
bool RegisterId(void* bullet,unsigned id) {
    if(id==UINT32_MAX)return At<unsigned>(bullet,0x128)==UINT32_MAX;
    const auto old=At<unsigned>(bullet,0x128);if(old!=UINT32_MAX)return old==id;
    void* ref[2]{};Fn<void*(__fastcall*)(void*,void*)>(0x784510)(At<void*>(image,0x20B2AC8),ref);
    bool ok=false;
    __try {if(ref[0]){Fn<void(__fastcall*)(void*,void*,unsigned,unsigned)>(0x7639B0)(ref[0],static_cast<unsigned char*>(bullet)+0x120,id&255u,id>>8);ok=At<unsigned>(bullet,0x128)==id;}}
    __finally {if(ref[1])Unpin({nullptr,static_cast<unsigned char*>(ref[1])});}
    return ok;
}
bool Post(Stream& s,const Event& e,void* bullet,void* allocated,bool* linked) {
    auto& p=s.parts[e.part];auto* battery=p.battery.object;auto* muzzle=Muzzle(p,e.muzzle);
    auto* control=At<unsigned char*>(bullet,0x30);if(!Readable(control,16,true) || At<LONG>(control,8)<=0)return false;
    if(At<unsigned>(bullet,0xC30)==1) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(control+12));auto* old=At<unsigned char*>(muzzle,0xB8);
        crew::Put<void*>(muzzle,0xB0,bullet);crew::Put<void*>(muzzle,0xB8,control);s.cleanupUncertain=true;DropWeak(old);s.cleanupUncertain=false;
    }
    auto* node=static_cast<unsigned char*>(allocated);auto* head=At<unsigned char*>(battery,0x290);auto* tail=At<unsigned char*>(head,8);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(control+12));
    crew::Put<void*>(node,0,head);crew::Put<void*>(node,8,tail);crew::Put<void*>(node,16,bullet);crew::Put<void*>(node,24,control);
    crew::Put<void*>(head,8,node);crew::Put<void*>(tail,0,node);crew::Put<std::size_t>(battery,0x298,At<std::size_t>(battery,0x298)+1);*linked=true;
    if(auto* callback=At<void*>(battery,0x650)) {
        auto* vt=At<void* const*>(callback,0);void* value=bullet;unsigned zero=0;
        reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(vt[2])(callback,&value,&zero);
    }
    if(!Current(s.root) || !RegisterId(bullet,e.id))return false;if(e.generator)crew::Put<unsigned>(At<void*>(battery,0x658),0,e.nextCounter);return true;
}
Result ContinuePendingPost(Stream& s) {
    if(!s.postPending || !s.postHeld || !s.postNode)return Result::failed;
    if(!RestoreNativeProjectileJournal(s.journal) || !NativeProjectileJournalEmpty(s.journal)) {
        if(s.journal.uncertain){s.faulted=true;return Result::failed;}return Result::pending;
    }
    if(!Parts(s))return Result::pending;
    const auto& e=s.postEvent;auto& p=s.parts[e.part];auto* muzzle=Muzzle(p,e.muzzle);
    if(!PostReady(p,muzzle,e) || !IdDestinationReady(e.id))return Result::pending;
    bool linked=false,completed=false;
    __try {
        if(!Post(s,e,s.postBullet.object,s.postNode,&linked)){s.faulted=true;return Result::failed;}
        s.postCommitted=true;if(!FinishPostReference(s,false)){s.faulted=true;return Result::failed;}
        s.receipts[e.serial%kQueued]={e.serial,s.postHash};s.applied=e.serial;
        s.postPending=false;s.postEvent={};s.postHash=0;completed=true;return Result::applied;
    } __finally {
        if(linked)s.postNode=nullptr;
        if(!completed && s.postHeld)FinishPostReference(s,true);
    }
}
Result Apply(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    __try {
        bool authority=false;if(!NativeActionRole(l.object,&authority) || authority)return Result::failed;
        Event e{};if(!Decode(bytes,size,&e))return Result::failed;auto* s=Find(l);if(s && s->authority)return Result::failed;if(s && s->retired){const auto& r=s->receipts[e.serial%kQueued];return r.serial==e.serial && r.hash==Hash(e) ? Result::applied : Result::failed;}if(!s)s=Bind(l,false);if(!s || s->faulted || s->aborting || s->executing || s->beamDepth || s->cleanupUncertain)return Result::failed;
        const auto hash=Hash(e);
        if(e.serial<=s->applied){const auto& r=s->receipts[e.serial%kQueued];return r.serial==e.serial && r.hash==hash ? Result::applied : Result::failed;}
        if(s->postPending) {
            if(e.serial!=s->postEvent.serial || hash!=s->postHash)return Result::failed;
            s->executing=true;__try {return ContinuePendingPost(*s);}__finally {s->executing=false;}
        }
        if(!NativeProjectileJournalEmpty(s->journal) || s->postHeld || s->postNode)return Result::failed;
        if(e.serial!=s->applied+1)return Result::failed;if(e.kind!=EventKind::birth){const auto result=ApplyBeam(*s,e,hash);if(result==Result::failed)s->faulted=true;return result;}
        if(!Parts(*s))return Result::pending;auto& p=s->parts[e.part];auto* muzzle=Muzzle(p,e.muzzle);
        if(p.definition!=e.definition)return Result::failed;
        if(!PostReady(p,muzzle,e) || !IdDestinationReady(e.id) || !CanReplayNativeProjectileSample(l,At<void*>(p.battery.object,0x2F0),p.battery.object+0x300,
            reinterpret_cast<float*>(muzzle+0x70),e.sample))return Result::pending;
        // Reserve the native weak-list node before any factory mutation.
        auto* node=Fn<void*(__fastcall*)(std::size_t)>(0x12D85B0)(0x20);if(!node)return Result::pending;
        s->postPending=true;s->postEvent=e;s->postHash=hash;s->postNode=node;s->executing=true;
        __try {
            const auto outcome=ReplayNativeProjectileOutcomeInPlace(s->journal,l,At<void*>(p.battery.object,0x2F0),p.battery.object+0x300,reinterpret_cast<float*>(muzzle+0x70),e.sample);
            auto* bullet=static_cast<unsigned char*>(outcome.created);if(!bullet){s->faulted=true;return Result::failed;}
            // Native creation is irreversible even when descriptor cleanup is
            // pending. Own and detach a persistent beam before that result can
            // be hidden behind the cleanup gate or the battery can be released.
            if(!Readable(bullet,0x38)){s->faulted=s->cleanupUncertain=true;return Result::failed;}
            s->postBullet={bullet,At<unsigned char*>(bullet,0x30)};s->postHeld=Pin(s->postBullet);
            if(!s->postHeld || !TrackBeam(*s,e,s->postBullet)){s->faulted=s->cleanupUncertain=true;return Result::failed;}
            if(!outcome.cleanupComplete){if(s->journal.uncertain){s->faulted=true;return Result::failed;}return Result::pending;}
            return ContinuePendingPost(*s);
        } __finally {s->executing=false;}
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l))s->faulted=true;return Result::failed;}
}
bool Quiesce(const NativeLease& l) noexcept {if(auto* s=Find(l)){s->quiescing=true;ScanBeams(*s);}return true;}
bool Empty(const NativeLease& l) noexcept {__try {auto* s=Find(l);if(!s)return true;if(s->count || s->executing || s->beamDepth || s->cleanupUncertain || ActiveBeams(*s) || s->waitingHeld || s->postPending)return false;return FinishPostReference(*s,false) && RestoreNativeProjectileJournal(s->journal) && NativeProjectileJournalEmpty(s->journal);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool Release(Stream& s) noexcept {
    if(s.retired)return !s.cleanupUncertain && NativeProjectileJournalEmpty(s.journal) && !s.postHeld && !s.postPending;if(s.executing || s.beamDepth || s.count || s.cleanupUncertain || ActiveBeams(s) || s.waitingHeld || s.postPending || !ReleaseBeams(s) || !DiscardPendingPost(s) || !RestoreNativeProjectileJournal(s.journal) || !NativeProjectileJournalEmpty(s.journal) || (s.held && !CanUnpin(s.root)))return false;
    for(const auto& p:s.parts)if((p.ownerHeld && !CanUnpin(p.owner)) || (p.batteryHeld && !CanUnpin(p.battery)))return false;
    for(auto& p:s.parts){if(!DropStrong(s,p.battery,&p.batteryHeld) || !DropStrong(s,p.owner,&p.ownerHeld))return false;p={};}
    if(!DropStrong(s,s.root,&s.held))return false;s.retired=true;return true;
}
bool Restore(const NativeLease& l) noexcept {__try {auto* s=Find(l);return !s || Release(*s);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool Abort(const NativeLease& l) noexcept {
    __try {auto* s=Find(l);if(!s)return true;s->quiescing=true;s->aborting=true;if(s->cleanupUncertain || s->executing || s->beamDepth || !RestoreNativeProjectileJournal(s->journal) || !NativeProjectileJournalEmpty(s->journal))return false;if(s->retired)return true;ScanBeams(*s);if(ActiveBeams(*s))return false;s->waiting={};s->waitingHeld=s->waitingReady=false;if(!ReleaseBeams(*s) || !DiscardPendingPost(*s))return false;
        for(unsigned i=0;i<s->count;++i){auto& p=s->queue[(s->head+i)%kQueued];if(p.held && !CanUnpin(p.bullet))return false;}
        for(unsigned i=0;i<s->count;++i){auto& p=s->queue[(s->head+i)%kQueued];if(!DropStrong(*s,p.bullet,&p.held))return false;if(p.event.kind==EventKind::stop)for(auto& b:s->beams)if(b.held && b.spawn==p.event.spawn){if(!RehomeBeam(b) || !DropStrong(*s,b.ref,&b.held))return false;b={};}p={};}s->count=s->head=0;return Release(*s);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace merman_net
bool InstallMermanAcceptedProjectiles() noexcept {
    using namespace merman_net;
    __try {
        if(!image || (admittedImage && image!=admittedImage))return false;
#ifndef ENEMY_CONTROL_MERMAN_NET_TEST
        for(const auto& sig:prints)if(!Readable(image+sig.rva,16) || std::memcmp(image+sig.rva,sig.bytes,16))return false;
#endif
        const NativeUnitFactoryProvider provider{kTag,OwnsFactory,Invoke,Fault};
        if(!RegisterNativeUnitFactoryProvider(provider) || !RegisterNativeUnitFactoryCallsite(kSite) || !InstallNativeUnitFactoryDispatcher() ||
           !NativeUnitFactoryProviderReady(provider) || !NativeUnitFactoryCallsiteReady(kSite))return false;
        const NativeBeamRefreshProvider refresh{kTag,OwnsBeam,BeamRefresh,BeamFault};const NativeBeamStepProvider step{kTag,OwnsBeam,BeamStep,BeamFault};if(!RegisterNativeBeamRefreshProvider(refresh) || !InstallNativeBeamRefreshDispatcher() || !NativeBeamRefreshProviderReady(refresh) || !RegisterNativeBeamStepProvider(step) || !InstallNativeBeamStepDispatcher() || !NativeBeamStepProviderReady(step))return false;
        const NativeActionExtension extension{kTag,Needs,Before,nullptr,Capture,Owns,Apply,Quiesce,Empty,Abort,Restore};
        if(!UpdateHooks() || !RegisterNativeActionExtension(extension))return false;admittedImage=image;installed=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew::enemy_native
