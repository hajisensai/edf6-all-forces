// Actual Monster Unit factories, after native MAB scheduling and spread/RNG.
// Only the two proven calls are relayed; their native post-create effects run.
#include "enemy_control_monster_net.h"
#include "enemy_control_monster_net_codec.h"
#include "enemy_control_monster_definition.h"
#include "enemy_control_gun_factory.h"
#include "enemy_control_action_native.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <limits>

namespace crew::enemy_native {
#ifdef ENEMY_CONTROL_MONSTER_NET_TEST
void* MonsterNetTestFunction(unsigned) noexcept;
#endif
namespace monster_net {
using Result=enemy_control::ActionResult;
using Event=monster_wire::Event;
constexpr unsigned kRoots=8,kQueued=4096,kPatterns=256;
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Receipt {std::uint64_t serial=0,hash=0;};
struct Pattern {const unsigned char* custom=nullptr;std::uint64_t hash=0;};
struct Stream {
    Ref root{};const NativeProfile* profile=nullptr;void* factory=nullptr;unsigned char family=0;
    bool held=false,authority=false,quiescing=false,faulted=false,lost=false,baseMade=false,uncertain=false;
    std::uint64_t digest=0,next=1,applied=0;unsigned head=0,count=0;std::uint64_t alignReserved=0;
    alignas(16) unsigned char base[0x1E0]{},scratch[0x1E0]{};alignas(16) float muzzle[16]{};
    void* orphanControl=nullptr;void* orphanPayload=nullptr;bool scratchMade=false;unsigned char scratchReserved[15]{};
    NativeProjectileJournal projectile{};
    Pattern patterns[kPatterns]{};unsigned patternCount=0,nativeCalls=0;
    // held retains the ledger; rootHeld is the one consumable strong reference.
    bool rootHeld=false,cleanupEntered=false;unsigned char patternReserved[6]{};
    Event queue[kQueued]{};Receipt receipts[kQueued]{};
};
struct Sig {unsigned rva;unsigned char bytes[16];};
constexpr Sig kSigs[]={
    {0x32EF10,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x57,0x48,0x83,0xEC}},
    {0x100C90,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48}},
    {0x111F570,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0xE2,0xFB,0x01,0x00,0x48,0x8D}},
    {0x11002D0,{0x48,0x8B,0xC4,0x55,0x53,0x41,0x55,0x48,0x8D,0x6C,0x24,0x80,0x48,0x81,0xEC,0x80}},
    {0x22F790,{0x48,0x83,0xEC,0x28,0x48,0x83,0x39,0x00,0x0F,0x84,0x2B,0x01,0x00,0x00,0x48,0x89}},
    {0x22F8E0,{0x48,0x83,0xEC,0x28,0x48,0x83,0x39,0x00,0x0F,0x84,0x8B,0x00,0x00,0x00,0x48,0x89}},
    {0x12D85B0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xEB,0x0F,0x48,0x8B,0xCB,0xE8,0x47}},
    {0x12D85EC,{0xE9,0xB7,0x0B,0x00,0x00,0xCC,0xCC,0xCC,0x48,0x83,0xEC,0x28,0x4D,0x8B,0x41,0x38}},
    {0x4ACE7C,{0x48,0x8B,0x0D,0xD5,0x5A,0xC0,0x01,0x48,0x8D,0x54,0x24,0x30,0xE8,0xF3,0x73,0xCE}},
    {0x4B2C25,{0x48,0x8B,0x0D,0x2C,0xFD,0xBF,0x01,0xE8,0x4F,0x16,0xCE,0x00,0x4C,0x8B,0xC0,0x48}},
    {0x4B298A,{0xB9,0xC0,0x00,0x00,0x00,0xE8,0x1C,0x5C,0xE2,0x00,0x48,0x8B,0xD8,0x48,0x89,0x45}},
    {0x4B29CE,{0x48,0x8D,0x4B,0x48,0xE8,0x99,0xCB,0xC6,0x00,0xC7,0x83,0x98,0x00,0x00,0x00,0x00}},
    {0x4B2B8E,{0x4D,0x8B,0x87,0x70,0x1C,0x00,0x00,0x48,0x8D,0x15,0x54,0x44,0xC8,0x01,0x49,0x8B}},
    {0x259D80,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0xB9,0x90,0x11,0x00,0x00,0xE8,0x1D}},
    {0x26EFC0,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0xB9,0x60,0x15,0x00,0x00,0xE8,0xDD}},
    {0x244CB0,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x48,0x83,0xEC,0x20,0x0F,0xB7,0x41,0x10}},
};
Stream streams[kRoots]{};unsigned char* admittedImage=nullptr;bool installed=false;
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_MONSTER_NET_TEST
    return reinterpret_cast<T>(MonsterNetTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
using VoidFn=void(__fastcall*)(void*);
using CopyFn=void*(__fastcall*)(void*,const void*);
unsigned Family(const NativeLease& l) noexcept {
    if(!l.profile || !Readable(l.profile,sizeof(*l.profile)) || l.profile->kind!=Kind::monster)return 0;
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(l.profile==profiles+i)return l.profile->vtableRva==0x17C5628 ? 1u : l.profile->vtableRva==0x17C5A60 ? 2u : 0u;
    return 0;
}
unsigned DescriptorOffset(unsigned family) noexcept {return family==1 ? 0x1B30u : 0x1A70u;}
unsigned FactoryVtable(unsigned family) noexcept {return family==1 ? 0x17A09B8u : 0x17A1F40u;}
Ref Reference(const NativeLease& l) noexcept {return {l.object,static_cast<unsigned char*>(const_cast<void*>(l.control))};}
bool Current(Ref r) noexcept {
    return r.object && Readable(r.object,0x1D20) && Readable(r.control,16,true) && At<LONG>(r.control,8)>0 &&
        At<const void*>(r.object,0x30)==r.control;
}
bool Pin(Ref r) noexcept {
    if(!Current(r))return false;auto* count=reinterpret_cast<volatile LONG*>(r.control+8);LONG was=*count;
    while(was>0 && was<(std::numeric_limits<LONG>::max)()) {const LONG actual=InterlockedCompareExchange(count,was+1,was);if(actual==was)return true;was=actual;}return false;
}
bool CanUnpin(Ref r) noexcept {
    if(!Readable(r.control,16,true) || At<LONG>(r.control,8)<=0 || At<LONG>(r.control,12)<=0)return false;
    const auto* table=At<void* const*>(r.control,0);return Readable(table,16) && table[0] && table[1];
}
void Unpin(Ref r) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+8)))return;
    const auto* table=At<void* const*>(r.control,0);reinterpret_cast<VoidFn>(table[0])(r.control);
    if(!InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.control+12)))reinterpret_cast<VoidFn>(table[1])(r.control);
}
NativeLease Lease(const Stream& s) noexcept {NativeLease l{};l.object=s.root.object;l.control=s.root.control;l.profile=s.profile;return l;}
Stream* Find(const NativeLease& l) noexcept {for(auto& s:streams)if(s.held && s.root.object==l.object && s.root.control==l.control && s.profile==l.profile)return &s;return nullptr;}
bool RoleLease(const void* descriptor,NativeLease* out,bool* authority) noexcept {
    if(!descriptor || !out || !authority)return false;
    std::size_t count=0;const auto* profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i) {
        const auto& p=profiles[i];if(p.vtableRva!=0x17C5628 && p.vtableRva!=0x17C5A60)continue;
        const unsigned family=p.vtableRva==0x17C5628 ? 1u : 2u;const auto address=reinterpret_cast<std::uintptr_t>(descriptor);
        if(address<DescriptorOffset(family))continue;auto* root=reinterpret_cast<unsigned char*>(address-DescriptorOffset(family));
        if(!Readable(root,0x38) || !NativeActionRole(root,authority))continue;
        const auto* table=At<const void* const*>(root,0);
        if(!table || !Readable(table-1,8) || !Readable(image+p.vtableRva-8,8) || table[-1]!=At<const void*>(image+p.vtableRva-8,0))continue;
        out->object=root;out->control=At<const void*>(root,0x30);out->profile=profiles+i;return Current(Reference(*out));
    }
    return false;
}
bool Factory(const NativeLease& l,unsigned family,void* factory) noexcept {
    return Current(Reference(l)) && factory && factory==At<void*>(l.object,DescriptorOffset(family)-0x10) && Readable(factory,8) &&
        At<const void*>(factory,0)==image+FactoryVtable(family) && Readable(image+FactoryVtable(family),24) &&
        At<const void*>(image+FactoryVtable(family),16)==image+(family==1 ? 0x259D80u : 0x26EFC0u);
}
bool ActualResources(const NativeLease& l,unsigned family,const void* descriptor) noexcept {
    if(family==1)return !At<const void*>(descriptor,0x180) && !At<const void*>(descriptor,0x190);
    const auto* material=At<const unsigned char*>(l.object,0x1C78);
    const auto* modelAsset=At<const void*>(l.object,0x1C70);const auto* model=At<const unsigned char*>(descriptor,0x190);
    if(At<const void*>(descriptor,0x180)!=material || At<const void*>(descriptor,0x188)!=(material ? At<const void*>(material,8) : nullptr) ||
       (model!=nullptr)!=(modelAsset!=nullptr))return false;
    if(!model)return At<const void*>(descriptor,0x198)==nullptr;
    if(!Readable(model,16) || At<LONG>(model,0)<=0 || At<LONG>(model,4)<=0)return false;
    const auto* payload=At<const unsigned char*>(model,8);
    return payload && Readable(payload,0xC0) && At<const void*>(descriptor,0x198)==payload && At<const void*>(payload,0)==modelAsset;
}
std::uint64_t Number(std::uint64_t hash,std::uint64_t value,unsigned size) noexcept {for(unsigned i=0;i<size;++i){hash^=static_cast<unsigned char>(value>>(i*8));hash*=1099511628211ull;}return hash;}
std::uint64_t Definition(const NativeLease& l,unsigned family,const void* descriptor) noexcept {
    if(!Readable(descriptor,0x1E0) || At<const void*>(descriptor,0)!=image+0x1769028)return 0;
    std::uint64_t hash=Number(1469598103934665603ull,l.profile->profileHash,4);hash=Number(hash,family,1);hash=Number(hash,FactoryVtable(family),4);
    for(const auto offset:kProjectileScalar32)if(offset!=0x90 && offset!=0x94 && offset!=0x98 && offset!=0x9C &&
        !(family==2 && (offset==0xB0 || offset==0xC0 || offset==0xE0)))hash=Number(hash,At<std::uint32_t>(descriptor,offset),4);
    for(const auto offset:kProjectileScalar8)hash=Number(hash,At<unsigned char>(descriptor,offset),1);
    hash=Number(hash,At<std::uint16_t>(descriptor,0x178),2);
    // Native variant1 is an SGO node pointer; variant2 is a DSGO tree handle.
    // Their typed source values are stable; their process addresses are not.
    for(unsigned offset:{0xE8u,0x1B0u})if(!(family==2 && offset==0xE8) && !monster_definition::HashVariant(static_cast<const unsigned char*>(descriptor)+offset,&hash))return 0;
    if(family==2){hash=Number(hash,At<const void*>(l.object,0x1C70) ? 1u : 0u,1);hash=Number(hash,At<const void*>(l.object,0x1C78) ? 1u : 0u,1);}
    return hash ? hash : 1;
}
std::uint64_t PatternHash(const unsigned char* custom) noexcept {
    alignas(16) unsigned char variant[32]{};Put<const void*>(variant,0,custom);Put<std::uint16_t>(variant,16,custom ? 1u : 0xFFFFu);
    std::uint64_t hash=1469598103934665603ull;return monster_definition::HashVariant(variant,&hash) ? hash : 0;
}
struct PatternScan {Stream* stream;const unsigned char* head;unsigned expected,seen=0;const unsigned char* nodes[kPatterns]{};};
bool ScanPattern(PatternScan& scan,const unsigned char* node,const unsigned char* parent,unsigned depth) noexcept {
    if(node==scan.head)return true;
    if(depth>=32 || scan.seen>=scan.expected || !Readable(node,0x70) || node[0x19] || At<const void*>(node,8)!=parent)return false;
    for(unsigned i=0;i<scan.seen;++i)if(scan.nodes[i]==node)return false;scan.nodes[scan.seen++]=node;
    if(!ScanPattern(scan,At<const unsigned char*>(node,0),node,depth+1))return false;
    auto& s=*scan.stream;const auto length=At<std::size_t>(node,0x30),capacity=At<std::size_t>(node,0x38);
    const auto* name=capacity<8 ? node+0x20 : At<const unsigned char*>(node,0x20);
    if(!length || length>256 || length>capacity || !Readable(name,length*2) || s.patternCount>=kPatterns)return false;
    s.digest=Number(s.digest,length,4);for(std::size_t i=0;i<length;++i)s.digest=Number(s.digest,At<std::uint16_t>(name,i*2),2);
    // +14 is the level-derived pattern (sent as the actual sample's +98).
    // The other fields are static count/interval/pellets/angle/speeds/flash.
    // +24 is padding; +28 is the local SGO node, hashed by typed value below.
    for(unsigned offset=0;offset<=0x20;offset+=4)if(offset!=0x14)s.digest=Number(s.digest,At<std::uint32_t>(node,0x40+offset),4);
    const auto* custom=At<const unsigned char*>(node,0x68);const auto hash=PatternHash(custom);if(!hash)return false;
    s.digest=Number(s.digest,hash,8);s.patterns[s.patternCount++]={custom,hash};
    return ScanPattern(scan,At<const unsigned char*>(node,0x10),node,depth+1);
}
bool Patterns(Stream& s,const NativeLease& l) noexcept {
    if(s.family!=2)return true;const auto count=At<std::size_t>(l.object,0x1D78);const auto* head=At<const unsigned char*>(l.object,0x1D70);
    if(!count || count>kPatterns || !Readable(head,0x20) || head[0x19]!=1)return false;
    s.digest=Number(s.digest,count,4);PatternScan scan{&s,head,static_cast<unsigned>(count)};
    return ScanPattern(scan,At<const unsigned char*>(head,8),head,0) && scan.seen==count && s.patternCount==count;
}
const Pattern* ActualPattern(const Stream& s,const void* descriptor) noexcept {
    const auto type=At<std::uint16_t>(descriptor,0xF8);if(type!=1 && type!=0xFFFF)return nullptr;
    const auto* custom=type==1 ? At<const unsigned char*>(descriptor,0xE8) : nullptr;
    for(unsigned i=0;i<s.patternCount;++i)if(s.patterns[i].custom==custom && PatternHash(custom)==s.patterns[i].hash)return s.patterns+i;
    return nullptr;
}
const Pattern* AcceptedPattern(const Stream& s,std::uint64_t digest) noexcept {
    for(unsigned i=0;i<s.patternCount;++i)if(Number(s.digest,s.patterns[i].hash,8)==digest && PatternHash(s.patterns[i].custom)==s.patterns[i].hash)return s.patterns+i;return nullptr;
}
bool Needs(const NativeLease& l) noexcept {return Family(l)!=0;}
bool Before(const NativeLease& l,bool authority,Input*) noexcept {
    __try {
        const unsigned family=Family(l);if(!installed || admittedImage!=image || !family || !Current(Reference(l)))return false;
        bool role=false;if(!NativeActionRole(l.object,&role) || role!=authority)return false;
        if(auto* s=Find(l))return s->authority==authority && s->rootHeld && !s->cleanupEntered && !s->uncertain && (!s->faulted || s->quiescing);
        for(auto& s:streams)if(!s.held) {
            if(!Pin(Reference(l)))return false;s.root=Reference(l);s.profile=l.profile;s.held=true;s.rootHeld=true;s.authority=authority;s.family=static_cast<unsigned char>(family);
            s.factory=At<void*>(l.object,DescriptorOffset(family)-0x10);if(!Factory(l,family,s.factory)){s.faulted=true;return false;}
            const auto* descriptor=l.object+DescriptorOffset(family);s.digest=Definition(l,family,descriptor);if(!s.digest || !Patterns(s,l)){s.faulted=true;return false;}
            // The original 504 clears per-shot model/material after every create.
            // Keep only an owned base; reconstruct a FRESH local model per replay.
            s.uncertain=true;Fn<CopyFn>(0x32EF10)(s.base,descriptor);s.baseMade=true;
            Fn<VoidFn>(0x22F790)(s.base+0x190);Fn<VoidFn>(0x22F8E0)(s.base+0x180);s.uncertain=false;return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){if(auto* s=Find(l)){s->faulted=true;s->uncertain=true;}return false;}
}
bool OwnsFactory(const void*,const void* descriptor) noexcept {
    __try {NativeLease l{};bool authority=false;return installed && image==admittedImage && RoleLease(descriptor,&l,&authority);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void FaultFactory(const void*,const void* descriptor) noexcept {
    __try {NativeLease l{};bool authority=false;if(RoleLease(descriptor,&l,&authority))if(auto* s=Find(l)){s->faulted=true;s->lost=true;}}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
void* Invoke(void* manager,const float* matrix,void* factory,void* descriptor) {
    NativeLease l{};bool authority=false;
    if(!RoleLease(descriptor,&l,&authority))return InvokeOriginalNativeUnitFactory(manager,matrix,factory,descriptor);
    auto* s=Find(l);if(!s || !s->rootHeld || s->cleanupEntered || s->uncertain || s->authority!=authority || s->faulted || s->quiescing || NativeActionQuiescing(l.object))return nullptr;
    if(!authority)return nullptr; // native caller still executes its post-create RNG/effects/cleanup
    __try {
        if(!Factory(l,s->family,factory) || factory!=s->factory || !Readable(image+0x20B2958,8) || manager!=At<void*>(image+0x20B2958,0)) {s->faulted=true;return nullptr;}
        Event e{};e.serial=s->next;e.digest=s->digest;e.family=s->family;
        if(s->family==2){const auto* pattern=ActualPattern(*s,descriptor);if(!pattern){s->faulted=true;return nullptr;}e.digest=Number(s->digest,pattern->hash,8);}
        // The admitted stock constructors/emission paths keep +70 null. A
        // nonnull value is retained by a projectile, so a stream-wide replay
        // matrix would give it the wrong lifetime and must never be substituted.
        if(!e.serial || At<const void*>(descriptor,0x70) || !ActualResources(l,s->family,descriptor) ||
           !CaptureNativeProjectileSample(l,descriptor,matrix,descriptor,&e.sample) || e.sample.hasMuzzle || !monster_wire::Valid(e)){s->faulted=true;return nullptr;}
        // Capture precedes the native call; acceptance is exclusively NONNULL.
        // The native manager has an optional post-create notification. A
        // synchronous close there must retain this stream until acceptance
        // has been journalled; it may not clear it underneath this call.
        ++s->nativeCalls;
        __try {
            void* result=InvokeOriginalNativeUnitFactory(manager,matrix,factory,descriptor);if(!result)return nullptr;
            if(s->count==kQueued || s->next==(std::numeric_limits<std::uint64_t>::max)()) {s->faulted=true;s->lost=true;return result;}
            s->queue[(s->head+s->count)%kQueued]=e;++s->count;++s->next;return result;
        } __finally {--s->nativeCalls;}
    } __except(EXCEPTION_EXECUTE_HANDLER){s->faulted=true;s->lost=true;return nullptr;}
}
// Reproduce only the supported native inline C0 model constructor. The payload
// uses native allocation/ref-count/destruction and local root resources.
bool Resources(Stream& stream,const NativeLease& l,unsigned family,void* descriptor,unsigned char mask) noexcept {
    if(family==1)return mask==0;
    const auto* model=At<const void*>(l.object,0x1C70);const auto* material=At<const unsigned char*>(l.object,0x1C78);
    const unsigned char expected=static_cast<unsigned char>((model ? 2 : 0)|(material ? 1 : 0));if(mask!=expected)return false;
    if(material && (!Readable(material,16,true) || At<LONG>(material,0)<=0 || At<LONG>(material,4)<=0 ||
       At<LONG>(material,0)==0x7FFFFFFF || At<LONG>(material,4)==0x7FFFFFFF))return false;
    if(model) {
        using AllocateFn=void*(__fastcall*)(std::size_t);using FreeFn=void(__fastcall*)(void*,std::size_t);
        auto* control=static_cast<unsigned char*>(Fn<AllocateFn>(0x12D85B0)(0x10));if(!control)return false;stream.orphanControl=control;
        auto* payload=static_cast<unsigned char*>(Fn<AllocateFn>(0x12D85B0)(0xC0));stream.orphanPayload=payload;
        if(!payload){Fn<FreeFn>(0x12D85EC)(control,0x10);stream.orphanControl=nullptr;return false;}
        std::memset(payload,0,0xC0);Fn<VoidFn>(0x111F570)(payload+0x48);
        for(unsigned offset:{0x70u,0x84u,0x98u,0xACu})Put<float>(payload,offset,1.0f);
        Put<LONG>(control,0,1);Put<LONG>(control,4,1);Put<void*>(control,8,payload);
        Put<void*>(descriptor,0x190,control);Put<void*>(descriptor,0x198,payload);
        stream.orphanControl=nullptr;stream.orphanPayload=nullptr;
        if(!Fn<bool(__fastcall*)(void*,void*,const void*)>(0x11002D0)(payload,image+0x2136FF0,model))return false;
    }
    if(material) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(material)));
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(material)+4));
        Put<const void*>(descriptor,0x180,material);Put<const void*>(descriptor,0x188,At<const void*>(material,8));
    }
    return true;
}
struct Unit {void* manager;void* factory;};
void* Create(void* opaque,const float* matrix,void* descriptor) noexcept {
    const auto& unit=*static_cast<Unit*>(opaque);return InvokeOriginalNativeUnitFactory(unit.manager,matrix,unit.factory,descriptor);
}
bool Capture(const NativeLease& l,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;auto* s=Find(l);if(!s || !s->rootHeld || s->cleanupEntered || !used || !s->authority || !Current(s->root) || (s->faulted && !s->quiescing))return false;
    if(!s->count)return !s->lost;
    if(!monster_wire::Encode(&s->queue[s->head],1,bytes,(capacity<512 ? capacity : 512),used))return false;
    s->head=(s->head+1)%kQueued;--s->count;return true;
}
bool Owns(const void* bytes,std::size_t size) noexcept {return bytes && size>=4 && size<=512 && At<std::uint32_t>(bytes,0)==monster_wire::kTag;}
Result Apply(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    auto* s=Find(l);Event event{};unsigned count=0;
    if(!s || !s->rootHeld || s->cleanupEntered || s->authority || s->faulted || !Current(s->root) || !monster_wire::Decode(bytes,size,&event,1,&count))return Result::failed;
    const auto hash=monster_wire::Hash(event);
    if(event.serial<=s->applied){const auto& r=s->receipts[event.serial%kQueued];return r.serial==event.serial && r.hash==hash ? Result::applied : Result::failed;}
    if(event.serial!=s->applied+1)return Result::pending;
    const auto* pattern=s->family==2 ? AcceptedPattern(*s,event.digest) : nullptr;
    if(event.family!=s->family || (s->family==1 ? event.digest!=s->digest : !pattern) || event.sample.hasMuzzle || !Factory(l,s->family,s->factory) || !s->baseMade || s->uncertain || !NativeProjectileJournalEmpty(s->projectile))return Result::failed;
    if(!Readable(image+0x20B2958,8) || !Readable(At<void*>(image+0x20B2958,0),0xCB4))return Result::pending;
    // Resolve every semantic ID before allocating the per-shot local model.
    // The owned base has no model/material; the full resource mask is checked
    // again after those local resources have actually been constructed.
    NativeProjectileSample references=event.sample;references.resourceMask=0;
    if(!CanPrepareNativeProjectileSample(l,s->base,s->muzzle,references))return Result::pending;
    if(s->scratchMade || s->orphanControl || s->orphanPayload)return Result::failed;
    auto* descriptor=s->scratch;Result result=Result::failed;
    __try {
        __try {
            s->uncertain=true;Fn<CopyFn>(0x32EF10)(descriptor,s->base);s->scratchMade=true;s->uncertain=false;
            if(pattern) {
                alignas(16) unsigned char variant[32]{};Put<const void*>(variant,0,pattern->custom);Put<std::uint16_t>(variant,16,pattern->custom ? 1u : 0xFFFFu);
                s->uncertain=true;Fn<void(__fastcall*)(void*,const void*)>(0x244CB0)(descriptor+0xE8,variant);s->uncertain=false;
            }
            if(!Resources(*s,l,s->family,descriptor,event.sample.resourceMask))return Result::failed;
            if(!CanPrepareNativeProjectileSample(l,descriptor,s->muzzle,event.sample))return Result::pending;
            Unit unit{At<void*>(image+0x20B2958,0),s->factory};
            if(!WithNativeProjectileSampleInPlace(s->projectile,l,descriptor,s->muzzle,event.sample,Create,&unit)){s->faulted=true;return Result::failed;}
            // Commit only after a real native Unit was registered. A retry never
            // reconstructs its model, repeats Factory, or restarts the MAB.
            s->applied=event.serial;s->receipts[event.serial%kQueued]={event.serial,hash};result=Result::applied;
        } __finally {if(s->scratchMade){s->uncertain=true;Fn<VoidFn>(0x100C90)(descriptor);s->scratchMade=false;s->uncertain=false;}}
    } __except(EXCEPTION_EXECUTE_HANDLER){s->faulted=true;s->uncertain=true;return Result::failed;}
    return result;
}
bool Quiesce(const NativeLease& l) noexcept {if(auto* s=Find(l))s->quiescing=true;return true;}
bool Empty(const NativeLease& l) noexcept {const auto* s=Find(l);return !s || (s->rootHeld && !s->cleanupEntered && !s->count && !s->nativeCalls && !s->lost && !s->uncertain && !s->scratchMade && !s->orphanControl && !s->orphanPayload && NativeProjectileJournalEmpty(s->projectile));}
bool Restore(const NativeLease& l) noexcept {
    auto* s=Find(l);if(!s)return true;if(!s->rootHeld || s->cleanupEntered || s->count || s->nativeCalls || s->lost || s->uncertain || s->scratchMade || s->orphanControl || s->orphanPayload || !CanUnpin(s->root))return false;
    s->cleanupEntered=true;
    __try {
        if(!RestoreNativeProjectileJournal(s->projectile)){s->cleanupEntered=false;return false;}
        if(s->baseMade){s->uncertain=true;Fn<VoidFn>(0x100C90)(s->base);s->baseMade=false;s->uncertain=false;}
        // Consume ownership BEFORE the decrement can invoke either destructor.
        // The ledger and its guards survive a native exception or callback.
        s->rootHeld=false;s->uncertain=true;
        Unpin(s->root);std::memset(s,0,sizeof(*s));s->next=1;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){s->faulted=true;s->uncertain=true;return false;}
}
bool Abort(const NativeLease& l) noexcept {if(auto* s=Find(l)){if(s->cleanupEntered || s->uncertain)return false;s->quiescing=true;s->count=0;s->lost=false;}return Restore(l);}
} // namespace monster_net
bool InstallMonsterAcceptedProjectiles() noexcept {
    using namespace monster_net;
    __try {
        if(!image)return false;
        if(admittedImage && admittedImage!=image)return false;
        if(!admittedImage)for(const auto& sig:kSigs)if(!Readable(image+sig.rva,16) || std::memcmp(image+sig.rva,sig.bytes,16))return false;
        const NativeUnitFactoryProvider provider{monster_wire::kTag,OwnsFactory,Invoke,FaultFactory};
        if(!RegisterNativeUnitFactoryProvider(provider) || !RegisterNativeUnitFactoryCallsite(0x4ACE88) || !RegisterNativeUnitFactoryCallsite(0x4B2C2C) ||
           !InstallNativeUnitFactoryDispatcher() || !NativeUnitFactoryProviderReady(provider) || !NativeUnitFactoryCallsiteReady(0x4ACE88) || !NativeUnitFactoryCallsiteReady(0x4B2C2C))return false;
        admittedImage=image;installed=true;
        const NativeActionExtension extension{monster_wire::kTag,Needs,Before,nullptr,Capture,Owns,Apply,Quiesce,Empty,Abort,Restore};
        return RegisterNativeActionExtension(extension);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew::enemy_native
