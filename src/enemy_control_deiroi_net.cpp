// Accepted Deiroi child lifecycle and real-shot stream. Native evidence is audited
// by tests/enemy_control_deiroi_net_native_audit.py. Game thread only.
#include "enemy_control_deiroi_net.h"
#include "enemy_control_deiroi_net_codec.h"
#include "enemy_control_deiroi.h"
#include "enemy_control_deiroi_entity_aim.h"
#include "enemy_control_action_native.h"
#include "enemy_control_gun_factory.h"
#include "enemy_control_projectile_native.h"
#include "enemy_control.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <limits>

namespace crew::enemy_native {
// Trusted bridge owns ordered part membership and weak native identities.
bool ResolveDeiroiControlledGun(const void*,NativeLease*,unsigned*) noexcept;
bool ResolveDeiroiControlledGunKey(const NativeLease&,unsigned,void**) noexcept;
bool ApplyDeiroiAcceptedControl(const NativeLease&,unsigned,unsigned,std::uint64_t,std::uint64_t,const float*) noexcept;
bool DeiroiExplicitTransition(const void*) noexcept;
bool ResolveDeiroiRetainedGunKey(const NativeLease&,unsigned,void**) noexcept;
bool ResolveDeiroiControlledHolder(const void*,NativeLease*,unsigned*) noexcept;
bool ResolveDeiroiControlledHolderKey(const NativeLease&,unsigned,void**,void**) noexcept;
bool ReadDeiroiMainGunCount(const NativeLease&,unsigned*) noexcept;
bool DeiroiGunManual(const NativeLease&,unsigned) noexcept;
bool DeiroiNativeTransitionActive(const NativeLease&) noexcept;
#ifdef ENEMY_CONTROL_DEIROI_NET_TEST
void* DeiroiNetTestFunction(unsigned) noexcept;
#endif
namespace deiroi_net {
using Result=enemy_control::ActionResult;
using ShotFn=void(__fastcall*)(void*,const float*,std::uint64_t*);
using StateFn=void(__fastcall*)(void*);
using StockShotFn=void(__fastcall*)(void*,void*,const float*,std::uint32_t);
using StockSetFn=void(__fastcall*)(void*,std::uint8_t,void*);
using StockSendFn=void(__fastcall*)(void*,const float*,std::uint32_t);
using TickFn=bool(__fastcall*)(void*);
struct Desc {const void* function;std::int32_t adjustor=0,padding=0;};
using NativeSetFn=void(__fastcall*)(void*,const Desc*,void*);
using MainSetFn=void(__fastcall*)(void*,const void*,void*);
using NetActiveFn=bool(__fastcall*)(void*);
constexpr unsigned kBatteryVtable=0x17AB118,kBindings=8,kQueued=4096,kContexts=320;
constexpr std::size_t kHolder=0xB90,kMachine=0xAD0;
struct Ref {unsigned char* object=nullptr;unsigned char* control=nullptr;};
struct Retained {Ref ref{};void* holder=nullptr;unsigned key=0;bool strong=false,baseline=false;unsigned char serial=0,phase=0;};
struct Receipt {std::uint64_t serial=0,hash=0;};
struct TargetPin {void* object=nullptr;const void* control=nullptr;void* actor=nullptr;const void* actorControl=nullptr;bool held=false,actorHeld=false,uncertain=false,actorUncertain=false;};
struct Stream {
    Ref root{};bool rootStrong=false,releaseUncertain=false;Retained parts[kContexts]{};Receipt receipts[kQueued]{};const NativeProfile* profile=nullptr;bool authority=false,offline=false,faulted=false,quiescing=false;
    TargetPin targets[deiroi_wire::kMaxBatch]{};
    std::uint64_t next=1,applied=0;unsigned head=0,count=0,reserved=0,nativeInFlight=0;
    deiroi_wire::Event queue[kQueued]{};
};
Stream streams[kBindings]{};
alignas(16) unsigned char rawDescriptors[kBindings][0x260]{};
unsigned char* installedImage=nullptr;bool ready=false,redirected[18]{};

template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_DEIROI_NET_TEST
    return reinterpret_cast<T>(DeiroiNetTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
bool Current(const Ref& ref,bool alive=true) noexcept {
    return ref.object && Readable(ref.control,16) && At<LONG>(ref.control,8)>0 &&
        Readable(ref.object,0x355) && At<const void*>(ref.object,0x30)==ref.control &&
        (!alive || (!(ref.object[0x18]&4) && !ref.object[0x2E8]));
}
Ref Reference(const void* object) noexcept {
    auto* body=static_cast<unsigned char*>(const_cast<void*>(object));
    return {body,Readable(body,0x38) ? At<unsigned char*>(body,0x30) : nullptr};
}
bool Keep(const Ref& ref) noexcept {
    if(!Current(ref,false) || !Readable(ref.control,16,true))return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(ref.control+12));return true;
}
bool Drop(Ref& ref) noexcept {
    if(!ref.control){ref={};return true;}
    if(!Readable(ref.control,16,true))return false;
    const auto table=At<void* const*>(ref.control,0);
    if(!Readable(table,16))return false;
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ref.control+12))==0)
        reinterpret_cast<void(__fastcall*)(void*)>(table[1])(ref.control);
    ref={};return true;
}
bool Strong(const Ref& ref) noexcept {
    if(!Current(ref,false) || !Readable(ref.control,16,true))return false;
    auto* uses=reinterpret_cast<volatile LONG*>(ref.control+8);LONG was=*uses;
    while(was>0 && was<(std::numeric_limits<LONG>::max)()) {
        const LONG actual=InterlockedCompareExchange(uses,was+1,was);
        if(actual==was)return true;was=actual;
    }
    return false;
}
void Unstrong(const Ref& ref) noexcept {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ref.control+8))!=0)return;
    const auto table=At<void* const*>(ref.control,0);
    reinterpret_cast<void(__fastcall*)(void*)>(table[0])(ref.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ref.control+12))==0)
        reinterpret_cast<void(__fastcall*)(void*)>(table[1])(ref.control);
}
unsigned FactoryKind(const void* holder) noexcept {
    if(!Readable(holder,0x28))return 0;
    const auto parameter=At<const unsigned char*>(holder,0x20);
    if(!Readable(parameter,0x250))return 0;
    const auto table=At<const void* const*>(parameter,0);
    if(!Readable(table,32))return 0;
    if(table[1]==image+0x6CF7D0 && table[2]==image+0x6CFEF0)return 1;
    if(table[1]==image+0x6CF910 && table[2]==image+0x6CFF00 && table[3]==image+0x6D0470 && Readable(parameter,0x4E0))return 2;
    return 0;
}
bool FactoryPath(const void* holder) noexcept {return FactoryKind(holder)!=0;}
bool ReleaseTarget(Stream& stream,TargetPin& target) noexcept {
    if(target.uncertain || target.actorUncertain)return false;
    // The lock is released first while its real owner is still strongly alive,
    // keeping the borrowed lock owner alive even during a native constructor callback.
    if(target.held) {
        bool consumed=false;const bool released=ReleaseNativeProjectileLock(target.object,target.control,&consumed);
        if(consumed)target.held=false;
        if(!released){if(consumed)target.uncertain=true;stream.faulted=true;return false;}
    }
    if(target.actorHeld) {
        bool consumed=false;const bool released=ReleaseNativeProjectileLock(target.actor,target.actorControl,&consumed);
        if(consumed)target.actorHeld=false;
        if(!released){if(consumed)target.actorUncertain=true;stream.faulted=true;return false;}
    }
    target={};return true;
}
bool ReleaseTargets(Stream& stream) noexcept {
    bool released=true;for(auto& target:stream.targets)if(!ReleaseTarget(stream,target))released=false;return released;
}
bool Target(Stream& stream,unsigned slot,const unsigned char id[32],std::uint32_t ordinal) noexcept {
    auto& target=stream.targets[slot];if(target.held || target.actorHeld || target.uncertain || target.actorUncertain)return false;
    const bool actorResolved=ResolveNativeProjectileActor(id,&target.actor,&target.actorControl);
    target.actorHeld=target.actorControl!=nullptr;if(!actorResolved)return false;
    const bool resolved=ResolveNativeProjectileLock(id,ordinal,&target.object,&target.control);
    target.held=target.control!=nullptr;return resolved;
}
bool IsDeiroi(const NativeLease& lease) noexcept {
    if(!lease.profile || !Readable(lease.profile,sizeof(*lease.profile)) ||
       lease.profile->kind!=Kind::deiroi || lease.profile->vtableRva!=0x17AA648)return false;
    std::size_t count=0;const auto profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(lease.profile==profiles+i)return true;
    return false;
}
Stream* Find(const NativeLease& lease) noexcept {
    for(auto& stream:streams)if(stream.root.object==lease.object && stream.root.control==lease.control)return &stream;
    return nullptr;
}
Stream* Bind(const NativeLease& lease,bool authority) noexcept {
    if(!ready || image!=installedImage || !IsDeiroi(lease))return nullptr;
    Ref ref{lease.object,static_cast<unsigned char*>(const_cast<void*>(lease.control))};
    if(!Current(ref,false))return nullptr;
    if(auto* stream=Find(lease))return stream->authority==authority ? stream : nullptr;
    for(auto& stream:streams) {
        if(stream.root.object && !stream.rootStrong && !stream.releaseUncertain && !stream.count &&
           Readable(stream.root.control,16) && At<LONG>(stream.root.control,8)==0) {
            bool retained=false;for(const auto& part:stream.parts)retained=retained || part.strong;
            if(retained || !Drop(stream.root))continue;stream={};
        }
        if(!stream.root.object && Keep(ref)) {
            stream.root=ref;stream.profile=lease.profile;stream.authority=authority;return &stream;
        }
    }
    return nullptr;
}
void Fault(Stream* stream) noexcept {if(stream)stream->faulted=true;}
// NativeActionRole supplies authenticated root ownership; the bridge independently
// proves exact child class, source SGO ordinal, parent and retained weak identity.
Stream* Bound(const void* child,NativeLease* lease,unsigned* key,bool* bound) noexcept {
    *bound=false;
    if(!ready || image!=installedImage || !Readable(child,0x5C8) ||
       At<const void*>(child,0)!=image+kBatteryVtable)return nullptr;
    const auto* owner=At<const void*>(child,0x5C0);bool authority=false;
    const bool active=NativeActionRole(owner,&authority);
    // Keep suppression during a faulted restoration until the retained stream is
    // explicitly released. A transient root gate failure must not enable guests.
    Stream* retained=nullptr;
    for(auto& stream:streams)if(stream.root.object==owner && Current(stream.root,false)) {retained=&stream;break;}
    if(!active && !retained)return nullptr;
    *bound=true;
    if(!ResolveDeiroiControlledGun(child,lease,key) || lease->object!=owner || !IsDeiroi(*lease)) {
        if(retained)for(const auto& part:retained->parts)if(part.strong && part.ref.object==child && Current(part.ref,false)) {
            *lease={};lease->object=retained->root.object;lease->control=retained->root.control;lease->profile=retained->profile;
            *key=part.key;return retained;
        }
        Fault(retained);return nullptr;
    }
    if(!active)authority=retained->authority;
    auto* stream=Bind(*lease,authority);
    if(!stream)Fault(retained);
    if(stream && !stream->rootStrong && !BindDeiroiAcceptedContext(*lease)){stream->faulted=true;return nullptr;}
    return stream;
}
Stream* BoundHolder(const void* holder,NativeLease* lease,unsigned* key,bool* bound,void** actor) noexcept {
    *bound=false;*actor=nullptr;if(!ready || image!=installedImage)return nullptr;
    if(ResolveDeiroiControlledHolder(holder,lease,key) && IsDeiroi(*lease)) {
        bool authority=false;
        if(NativeActionRole(lease->object,&authority)) {
            *bound=true;void* actualHolder=nullptr;
            if(!ResolveDeiroiControlledHolderKey(*lease,*key,actor,&actualHolder) || actualHolder!=holder)return nullptr;
            auto* stream=Bind(*lease,authority);
            if(stream && !stream->rootStrong && !BindDeiroiAcceptedContext(*lease)){stream->faulted=true;return nullptr;}
            return stream;
        }
    }
    for(auto& stream:streams)if(Current(stream.root,false))for(const auto& part:stream.parts)
        if(part.strong && part.holder==holder && Current(part.ref,false)) {
            *bound=true;*lease={};lease->object=stream.root.object;lease->control=stream.root.control;lease->profile=stream.profile;
            *key=part.key;*actor=part.ref.object;return &stream;
        }
    // Foot parts still fail closed on an active root even if strict membership
    // became temporarily unreadable. Unrelated holder families remain native.
    auto* child=static_cast<const unsigned char*>(holder)-kHolder;
    auto* stream=Bound(child,lease,key,bound);if(stream)*actor=const_cast<unsigned char*>(child);return stream;
}
bool Push(Stream& stream,deiroi_wire::Event event,bool committed=false) noexcept {
    if((stream.faulted && !committed) || !stream.next || stream.count==kQueued ||
       (!committed && stream.count+stream.reserved>=kQueued) || (committed && !stream.reserved)){stream.faulted=true;return false;}
    if(committed)--stream.reserved;
    event.serial=stream.next;
    if(!deiroi_wire::Valid(event)){stream.faulted=true;return false;}
    stream.queue[(stream.head+stream.count)%kQueued]=event;++stream.count;
    if(++stream.next==0)stream.faulted=true;
    return !stream.faulted;
}
constexpr unsigned actualOffsets[]={0x90,0x94,0x98,0x9C,0x1A0,0x1A4};
struct SampleScope {
    Stream* stream=nullptr;const void* holder=nullptr;void* parameter=nullptr;void* factory=nullptr;void* descriptor=nullptr;
    const deiroi_wire::Event* expected=nullptr;deiroi_wire::Event* captured=nullptr;TargetPin* target=nullptr;std::uint32_t actual[6]{},entityTeam=0,lockOrdinal=0;
    unsigned char targetActorId[32]{};unsigned calls=0;bool created=false,entity=false,manual=false,queued=false;
};
SampleScope Scope(Stream* stream,void* holder,const deiroi_wire::Event* expected=nullptr,TargetPin* target=nullptr) noexcept {
    const auto parameter=At<unsigned char*>(holder,0x20);SampleScope scope{};scope.stream=stream;scope.holder=holder;
    scope.parameter=parameter;scope.entity=FactoryKind(holder)==2;scope.factory=At<void*>(parameter,0x28);
    scope.descriptor=parameter+(scope.entity ? 0x280 : 0xA0);scope.expected=expected;scope.target=target;return scope;
}
thread_local SampleScope* sample=nullptr;
bool OwnsUnit(const void* factory,const void* descriptor) noexcept {return sample && sample->factory==factory && sample->descriptor==descriptor;}
void FaultUnit(const void*,const void*) noexcept {if(sample)Fault(sample->stream);}
void* InvokeUnit(void* manager,const float* matrix,void* factory,void* descriptor) {
    if(!OwnsUnit(factory,descriptor))return InvokeOriginalNativeUnitFactory(manager,matrix,factory,descriptor);
    if(sample->calls++){Fault(sample->stream);return nullptr;} // do not claim a nested missile ctor's own factory
    auto* stream=sample->stream;if(!stream){return nullptr;}
    auto* actual=static_cast<unsigned char*>(descriptor)+(sample->entity ? 0x80 : 0);
    if(!Readable(actual,0x1A8,true)){Fault(stream);return nullptr;}
    void* input=descriptor;std::uint32_t prior[6]{};for(unsigned i=0;i<6;++i)prior[i]=At<std::uint32_t>(actual,actualOffsets[i]);
    if(sample->entity) {
        if(!Readable(descriptor,sizeof(rawDescriptors[0]))){Fault(stream);return nullptr;}
        auto* rawDescriptor=rawDescriptors[stream-streams];std::memcpy(rawDescriptor,descriptor,sizeof(rawDescriptors[0]));input=rawDescriptor;actual=rawDescriptor+0x80;
        TargetPin* target=sample->target;
        if(!sample->expected) {
            int lock=-1;
            if(sample->manual) {
                if(!EnemyControlWeaponLockIdentity(stream->root.object,sample->targetActorId,&lock) || lock < -1){Fault(stream);return nullptr;}
                sample->lockOrdinal=lock<0 ? 0u : static_cast<std::uint32_t>(lock);
            } else if(!CaptureNativeProjectileLock(static_cast<unsigned char*>(descriptor)+0x50,sample->targetActorId,&sample->lockOrdinal)){Fault(stream);return nullptr;}
            if(!Target(*stream,0,sample->targetActorId,sample->lockOrdinal)){Fault(stream);return nullptr;}
            target=&stream->targets[0];sample->target=target;
        } else {
            std::memcpy(sample->targetActorId,sample->expected->targetActorId,32);sample->lockOrdinal=sample->expected->lockOrdinal;
        }
        if(!target){Fault(stream);return nullptr;}
        // RawIP is a borrowed synchronous constructor input. Native 265EA0 and
        // 247400 COPY its weak/resources into the true object. No clone destructor
        // is called, and the target strong pin spans that native constructor.
        Put<void*>(input,0x50,target->object);Put<const void*>(input,0x58,target->control);
        sample->entityTeam=sample->expected ? sample->expected->entityTeam : At<std::uint32_t>(descriptor,0x38);
        Put<std::uint32_t>(input,0x38,sample->entityTeam);
    }
    void* result=nullptr;
    __try {
        if(sample->expected)for(unsigned i=0;i<6;++i)Put<std::uint32_t>(actual,actualOffsets[i],sample->expected->actual[i]);
        for(unsigned i=0;i<6;++i)sample->actual[i]=At<std::uint32_t>(actual,actualOffsets[i]);
        float speed=0,power=0;std::memcpy(&speed,sample->actual+1,4);std::memcpy(&power,sample->actual+3,4);
        if(!std::isfinite(speed) || !std::isfinite(power)){Fault(stream);return nullptr;}
        result=InvokeOriginalNativeUnitFactory(manager,matrix,factory,input);
        sample->created=result!=nullptr;
        if(sample->created && sample->captured && !stream->offline) {
            auto& event=*sample->captured;std::memcpy(event.actual,sample->actual,sizeof(event.actual));
            event.entityTeam=sample->entityTeam;event.lockOrdinal=sample->lockOrdinal;std::memcpy(event.targetActorId,sample->targetActorId,32);
            // A native post-create callback can fail after a real object exists.
            // Commit immediately using the slot reserved BEFORE entering native.
            sample->queued=Push(*stream,event,true);
        }
    } __finally {
        if(sample->expected && !sample->entity)for(unsigned i=0;i<6;++i)Put<std::uint32_t>(actual,actualOffsets[i],prior[i]);
    }
    return result;
}
bool OwnsGun(const void* holder) noexcept {
    __try {
        for(const auto& stream:streams)if(Current(stream.root,false))for(const auto& part:stream.parts)
            if(part.strong && part.holder==holder && Current(part.ref,false))return true;
        NativeLease lease{};unsigned key=0;bool authority=false;
        return ResolveDeiroiControlledHolder(holder,&lease,&key) && IsDeiroi(lease) && NativeActionRole(lease.object,&authority);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void FaultGun(const void* holder) noexcept {
    __try {for(auto& stream:streams)for(const auto& part:stream.parts)if(part.strong && part.holder==holder){Fault(&stream);return;}}
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void __fastcall Shot(void* holder,const float* matrix,std::uint64_t* seed) {
    NativeLease lease{};unsigned key=0;bool bound=false;Stream* stream=nullptr;
    __try {
        void* actor=nullptr;stream=BoundHolder(holder,&lease,&key,&bound,&actor);
        auto* child=static_cast<unsigned char*>(actor);
        if(!bound){Fn<ShotFn>(0x6D05C0)(holder,matrix,seed);return;}
        if(!stream || stream->nativeInFlight || stream->faulted || stream->quiescing || !stream->authority)return;
        if(!Current(stream->root) || !Current(Reference(child)))return;
        if(stream->count+stream->reserved>=kQueued){stream->faulted=true;return;}
        if(!FactoryPath(holder) || !Readable(holder,0x98,true) || !Readable(matrix,64) || !Readable(seed,8,true)) {stream->faulted=true;return;}
        const auto parameter=At<unsigned char*>(holder,0x20);
        if(!Readable(parameter,0x98,true)){stream->faulted=true;return;}
        deiroi_wire::Event event{};event.kind=FactoryKind(holder)==2 ? deiroi_wire::Kind::entity : deiroi_wire::Kind::shot;event.key=key;event.serial=stream->next;
        // These compatibility words are native factory parameters (+90/+94),
        // not assumed random-number state; preserve the same save/restore as stock cmd6.
        event.shotIndex=At<std::uint32_t>(holder,0x34);event.seed=*seed;event.rng90=At<std::uint32_t>(parameter,0x90);event.rng94=At<std::uint32_t>(parameter,0x94);
        std::memcpy(event.matrix,matrix,sizeof(event.matrix));
        if(!deiroi_wire::Valid(event)){stream->faulted=true;return;}
        const Ref root=stream->root,part=Reference(child);
        if(!Strong(root)){stream->faulted=true;return;}
        bool held=false,entered=false;
        __try {
            held=Strong(part);if(!held){stream->faulted=true;return;}
            ++stream->nativeInFlight;entered=true;
            const auto before=At<std::uint32_t>(holder,0x34);
            SampleScope scope=Scope(stream,holder);scope.manual=DeiroiGunManual(lease,key);scope.captured=&event;
            const unsigned priorReserved=stream->reserved;if(!stream->offline)++stream->reserved;SampleScope* prior=sample;sample=&scope;
            __try {Fn<ShotFn>(0x6D05C0)(holder,matrix,seed);} __finally {
                sample=prior;if(stream->reserved>priorReserved)--stream->reserved;
                if(scope.entity && !ReleaseTargets(*stream))stream->faulted=true;
            }
            const auto after=At<std::uint32_t>(holder,0x34);
            // Native +34 advances even when the lower factory returns empty.
            // Only the observed 1194280 SceneObject result proves an emission.
            if(scope.created && (scope.calls!=1 || after!=before+1u || event.shotIndex!=before || (!stream->offline && !scope.queued)))stream->faulted=true;
            else if(scope.calls>1 || (after!=before && !scope.calls))stream->faulted=true;
        } __finally {__try {if(held)Unstrong(part);Unstrong(root);} __finally {if(entered)--stream->nativeInFlight;}}
    } __except(EXCEPTION_EXECUTE_HANDLER) {Fault(stream);}
}
void Phase(void* child,unsigned phase) {
    NativeLease lease{};unsigned key=0;bool bound=false;Stream* stream=nullptr;
    __try {
        stream=Bound(child,&lease,&key,&bound);
        if(!bound){Fn<StateFn>(0x32B790)(child);return;}
        // 32B790 is purely the native state sender/counters. Suppress it for
        // BOTH roles for the whole lease, avoiding late stock packets at exit.
        if(stream && stream->offline){Fn<StateFn>(0x32B790)(child);return;}
        if(!stream || stream->faulted || !stream->authority || DeiroiExplicitTransition(child) ||
           !Current(stream->root) || !Current(Reference(child)) || (stream->quiescing && phase!=5))return;
        if(!Readable(child,0x3F0)){stream->faulted=true;return;}
        Retained* context=nullptr;for(auto& part:stream->parts)if(part.strong && part.key==key && part.ref.object==child){context=&part;break;}
        if(!context){stream->faulted=true;return;}
        const auto serial=At<unsigned char>(child,0xAC1);
        if(context->baseline && context->serial==serial && context->phase==phase)return;
        deiroi_wire::Event event{};event.kind=deiroi_wire::Kind::control;event.key=key;event.phase=static_cast<std::uint8_t>(phase);
        event.seed=At<std::uint64_t>(child,0x3E8);
        if(Push(*stream,event)){context->baseline=true;context->serial=serial;context->phase=static_cast<unsigned char>(phase);}
    } __except(EXCEPTION_EXECUTE_HANDLER) {Fault(stream);}
}
void __fastcall Charge(void* child){Phase(child,2);}
void __fastcall Fire(void* child){Phase(child,3);}
void __fastcall Close(void* child){Phase(child,4);}
void __fastcall Idle(void* child){Phase(child,5);}
void __fastcall StockShot(void* holder,void* reader,const float* matrix,std::uint32_t seed) {
    NativeLease lease{};unsigned key=0;bool bound=false;
    __try {auto* stream=Bound(static_cast<unsigned char*>(holder)-kHolder,&lease,&key,&bound);if(bound){(void)stream;return;}}
    __except(EXCEPTION_EXECUTE_HANDLER) {return;}
    Fn<StockShotFn>(0x6CF6C0)(holder,reader,matrix,seed);
}
void __fastcall StockState(void* machine,std::uint8_t index,void* data) {
    NativeLease lease{};unsigned key=0;bool bound=false;
    __try {auto* stream=Bound(static_cast<unsigned char*>(machine)-kMachine,&lease,&key,&bound);if(bound){(void)stream;return;}}
    __except(EXCEPTION_EXECUTE_HANDLER) {return;}
    // The caller consumes its original native D20/D21 sequence AFTER this return.
    Fn<StockSetFn>(0x32C180)(machine,index,data);
}
bool __fastcall Tick(void* child) {
    NativeLease lease{};unsigned key=0;bool bound=false;
    __try {
        auto* stream=Bound(child,&lease,&key,&bound);
        if(bound && stream && Current(stream->root) && Current(Reference(child)) && !static_cast<unsigned char*>(child)[0x639]) {
            // Continue real state1 effect/elapsed updates even in native mode2.
            // Only the three autonomous TRANSITIONS below and the real factory
            // are filtered. Freezing all state1 would freeze holder+160 effects.
            if(!stream->authority)return false;
            if(stream->quiescing || stream->faulted)return true;
        }
        if(bound && !stream)return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return true;}
    return Fn<TickFn>(0x32BA80)(child);
}
void __fastcall Transition(void* machine,const Desc* state,void* data) {
    NativeLease lease{};unsigned key=0;bool bound=false;Stream* stream=nullptr;
    __try {
        auto* child=static_cast<unsigned char*>(machine)-kMachine;
        stream=Bound(child,&lease,&key,&bound);
        if(bound && stream && Current(stream->root) && Current(Reference(child)) && !child[0x639] &&
            (!stream->authority || stream->quiescing || stream->faulted))return;
        if(bound && !stream)return;
        if(stream){++stream->nativeInFlight;__try {Fn<NativeSetFn>(0x32C310)(machine,state,data);} __finally {--stream->nativeInFlight;}return;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {Fault(stream);return;}
    Fn<NativeSetFn>(0x32C310)(machine,state,data);
}
void __fastcall StockSend(void* child,const float* matrix,std::uint32_t seed) {
    NativeLease lease{};unsigned key=0;bool bound=false;
    __try {auto* stream=Bound(child,&lease,&key,&bound);if(bound){(void)stream;return;}}
    __except(EXCEPTION_EXECUTE_HANDLER) {return;}
    Fn<StockSendFn>(0x32B460)(child,matrix,seed);
}
void __fastcall MainTransition(void* machine,const void* callback,void* data) {
    NativeLease lease{};unsigned key=0;bool bound=false;Stream* stream=nullptr;void* actor=nullptr;
    __try {
        auto* holder=static_cast<unsigned char*>(machine)+0x50;
        stream=BoundHolder(holder,&lease,&key,&bound,&actor);
        if(!bound){Fn<MainSetFn>(0x3211C0)(machine,callback,data);return;}
        if(!stream || (key>>16)!=32)return;
        if(Current(stream->root) && (!stream->authority || stream->quiescing || stream->faulted))return;
        ++stream->nativeInFlight;
        __try {Fn<MainSetFn>(0x3211C0)(machine,callback,data);
        if(!stream->authority || stream->offline || !Current(stream->root) || stream->faulted)return;
        const unsigned phase=callback==image+0x31CB60 ? 6u : callback==image+0x31CDB0 ? 7u : 0u;
        if(!phase){stream->faulted=true;return;}
        deiroi_wire::Event event{};event.kind=deiroi_wire::Kind::control;event.key=key;event.phase=static_cast<unsigned char>(phase);
        event.seed=At<std::uint64_t>(actor,0x3E8);Push(*stream,event);
        } __finally {--stream->nativeInFlight;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {Fault(stream);}
}
bool __fastcall MainNetwork(void* netObject) {
    __try {
        auto* root=static_cast<unsigned char*>(netObject)-0x120;bool authority=false;
        if(NativeActionRole(root,&authority))return false;
        for(const auto& stream:streams)if(stream.root.object==root && Current(stream.root,false))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    return Fn<NetActiveFn>(0x7748F0)(netObject);
}
const NativeGunFactoryProvider gunProvider{0x31524744,&OwnsGun,&Shot,&FaultGun};
const NativeUnitFactoryProvider unitProvider{0x31554744,&OwnsUnit,&InvokeUnit,&FaultUnit};
constexpr unsigned sites[]={0x32BD98,0x32BB2C,0x32BE9D,0x32CFF8,0x32C6D8,0x32C7A4,0x32C9C0,0x32D164,0x32C6E2,0x32C7B2,0x32C9D3,0x32D172,0x32C821,0x32D084,0x32C736,0x31F148,0x31CD67,0x31F051};
constexpr unsigned targets[]={0x6CF6C0,0x32C180,0x32C180,0x32B460,0x32B790,0x32B790,0x32B790,0x32B790,0x32BA80,0x32BA80,0x32BA80,0x32BA80,0x32C310,0x32C310,0x32C310,0x3211C0,0x3211C0,0x7748F0};
constexpr unsigned char opcodes[]={0xE8,0xE8,0xE8,0xE8,0xE9,0xE9,0xE8,0xE9,0xE8,0xE8,0xE8,0xE8,0xE8,0xE8,0xE8,0xE8,0xE9,0xE8};
void* const hooks[]={reinterpret_cast<void*>(&StockShot),reinterpret_cast<void*>(&StockState),reinterpret_cast<void*>(&StockState),
    reinterpret_cast<void*>(&StockSend),reinterpret_cast<void*>(&Close),reinterpret_cast<void*>(&Charge),reinterpret_cast<void*>(&Fire),reinterpret_cast<void*>(&Idle),
    reinterpret_cast<void*>(&Tick),reinterpret_cast<void*>(&Tick),reinterpret_cast<void*>(&Tick),reinterpret_cast<void*>(&Tick),
    reinterpret_cast<void*>(&Transition),reinterpret_cast<void*>(&Transition),reinterpret_cast<void*>(&Transition),
    reinterpret_cast<void*>(&MainTransition),reinterpret_cast<void*>(&MainTransition),reinterpret_cast<void*>(&MainNetwork)};
bool Relay(unsigned index) noexcept {
    const auto* site=image+sites[index];if(!Readable(site,5) || site[0]!=opcodes[index])return false;
    const auto* destination=site+5+At<std::int32_t>(site,1);
    if(destination==hooks[index])return true;
    return Readable(destination,12) && destination[0]==0x48 && destination[1]==0xB8 && destination[10]==0xFF && destination[11]==0xE0 &&
        At<const void*>(destination,2)==hooks[index];
}
struct Signature {unsigned rva;unsigned char bytes[16];};
constexpr Signature signatures[]={
    {0x6CF7D0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x48,0x83,0xEC}},
    {0x6CFEF0,{0x48,0x8D,0x81,0xA0,0x00,0x00,0x00,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x6CF910,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x56}},
    {0x6CFF00,{0x48,0x8D,0x81,0x00,0x03,0x00,0x00,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x6D0470,{0xB0,0x01,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {0x6D05C0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x57,0x41,0x54,0x41}},
    {0x32B790,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x54,0x41,0x56}},
    {0x6CF6C0,{0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x45,0x8B}},
};
bool Preflight(const NativeLease& lease,const deiroi_wire::Event& event,Ref* child,void** holder) noexcept {
    void* object=nullptr;
    if(!ResolveDeiroiControlledHolderKey(lease,event.key,&object,holder))return false;
    auto* stream=Find(lease);if(!stream)return false;
    const Retained* retained=nullptr;
    for(const auto& part:stream->parts)if(part.strong && part.key==event.key && part.ref.object==object && part.holder==*holder){retained=&part;break;}
    if(!retained)return false;*child=retained->ref;
    if(!Current(*child,false) || !Readable(*holder,0x140,true) || !FactoryPath(*holder))return false;
    if(event.kind!=deiroi_wire::Kind::control && (FactoryKind(*holder)==2)!=(event.kind==deiroi_wire::Kind::entity))return false;
    if(event.kind!=deiroi_wire::Kind::control) {
        const auto parameter=At<unsigned char*>(*holder,0x20);
        if(!Readable(parameter,0x98,true))return false;
    }
    return true;
}
bool Emit(Stream& stream,void* holder,const deiroi_wire::Event& event,TargetPin* target) noexcept {const auto parameter=At<unsigned char*>(holder,0x20);
    const auto prior90=At<std::uint32_t>(parameter,0x90),prior94=At<std::uint32_t>(parameter,0x94);
    std::uint64_t seed=event.seed;bool committed=false;
    __try {
        Put<std::uint32_t>(parameter,0x90,event.rng90);Put<std::uint32_t>(parameter,0x94,event.rng94);
        // The factory distinguishes the first shot at 6D064D. A lease may start
        // in the middle of a real burst; preserve its actual source shot ordinal.
        Put<std::uint32_t>(holder,0x34,event.shotIndex);
        alignas(16) float matrix[16];std::memcpy(matrix,event.matrix,sizeof(matrix));
        // Direct real factory, never 6CFC60 (which refuses native remote mode>=2).
        SampleScope scope=Scope(&stream,holder,&event,target);SampleScope* prior=sample;sample=&scope;
        __try {Fn<ShotFn>(0x6D05C0)(holder,matrix,&seed);} __finally {sample=prior;}
        committed=scope.created && scope.calls==1 && At<std::uint32_t>(holder,0x34)==event.shotIndex+1u;
    } __finally {
        Put<std::uint32_t>(parameter,0x90,prior90);Put<std::uint32_t>(parameter,0x94,prior94);
    }
    return committed;
}
std::uint64_t Hash(const deiroi_wire::Event& event) noexcept {
    unsigned char bytes[deiroi_wire::kMaxBytes]{};std::size_t used=0;
    if(!deiroi_wire::Encode(&event,1,bytes,sizeof(bytes),&used))return 0;
    std::uint64_t hash=1469598103934665603ULL;
    for(std::size_t i=0;i<used;++i){hash^=bytes[i];hash*=1099511628211ULL;}
    return hash;
}
bool Releasable(const Ref& ref,bool strong) noexcept {
    if(!ref.control)return !strong;
    if(!Readable(ref.control,16,true) || At<LONG>(ref.control,12)<=0)return false;
    const auto table=At<void* const*>(ref.control,0);
    return Readable(table,16) && (!strong || At<LONG>(ref.control,8)>0);
}
bool ReleaseStrong(Stream& stream,const Ref& ref,bool& held) noexcept {
    if(!held)return true;
    // Keep the ledger until the decrement has actually happened. Native dispose
    // can theoretically fault after its final decrement; that uncertain callback
    // must never be invoked a second time by a restore retry.
    const LONG remaining=InterlockedDecrement(reinterpret_cast<volatile LONG*>(ref.control+8));held=false;
    if(remaining!=0)return remaining>0;
    stream.releaseUncertain=true;
    const auto table=At<void* const*>(ref.control,0);
    reinterpret_cast<void(__fastcall*)(void*)>(table[0])(ref.control);
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ref.control+12))==0)
        reinterpret_cast<void(__fastcall*)(void*)>(table[1])(ref.control);
    stream.releaseUncertain=false;return true;
}
bool SourceBusy(const Stream& stream) noexcept {
    NativeLease lease{};lease.object=stream.root.object;lease.control=stream.root.control;lease.profile=stream.profile;
    return DeiroiNativeTransitionActive(lease);
}
bool ReleaseContext(Stream& stream) noexcept {
    if(stream.nativeInFlight || SourceBusy(stream) || stream.releaseUncertain || !ReleaseTargets(stream))return false;
    // Validate EVERY retained control before the first mutation, including the
    // separate weak root pin, so a temporary read-only control is fully retryable.
    if(!Releasable(stream.root,stream.rootStrong))return false;
    for(const auto& part:stream.parts)if(part.strong && !Releasable(part.ref,true))return false;
    for(auto& part:stream.parts)if(part.strong) {
        if(!ReleaseStrong(stream,part.ref,part.strong))return false;part.ref={};part.holder=nullptr;
    }
    if(!ReleaseStrong(stream,stream.root,stream.rootStrong))return false;
    // Drop has its own weak-count destructor; preserve an uncertain ledger if
    // that final callback faults after decrementing rather than retrying it.
    if(!stream.root.control){stream.root={};return true;}
    const auto table=At<void* const*>(stream.root.control,0);
    const LONG weak=InterlockedDecrement(reinterpret_cast<volatile LONG*>(stream.root.control+12));
    if(weak==0){stream.releaseUncertain=true;reinterpret_cast<void(__fastcall*)(void*)>(table[1])(stream.root.control);stream.releaseUncertain=false;}
    if(weak<0){stream.releaseUncertain=true;return false;}
    stream.root={};return true;
}
} // namespace deiroi_net

bool BindDeiroiAcceptedContext(const NativeLease& lease) noexcept {
    using namespace deiroi_net;
    __try {
        bool authority=false,online=false;const bool role=NativeActionRole(lease.object,&authority);
        const bool offline=!role && EnemyControlNetworkContext(lease.object,&online) && !online;
        if(!role && !offline)return true;if(offline)authority=true;
        if(!DeiroiAcceptedActionsReady())return false;
        auto* stream=Bind(lease,authority);if(!stream || stream->faulted)return false;stream->offline=offline;
        if(!stream->rootStrong){if(!Strong(stream->root))return false;stream->rootStrong=true;}
        const unsigned count=DeiroiGunCount(lease);if(count>256)return false;
        for(unsigned index=0;index<count;++index) {
            unsigned key=0;void* child=nullptr;
            if(!DeiroiGunOrdinal(lease,index,&key) || !ResolveDeiroiControlledGunKey(lease,key,&child))return false;
            const Ref ref=Reference(child);Retained* free=nullptr;bool found=false;
            for(auto& part:stream->parts) {
                if(part.strong && part.key==key) {if(part.ref.object!=ref.object || part.ref.control!=ref.control)return false;found=true;break;}
                if(!part.strong && !free)free=&part;
            }
            if(found)continue;
            if(!free || !FactoryPath(ref.object+kHolder) || !Strong(ref))return false;
            free->ref=ref;free->holder=ref.object+kHolder;free->key=key;free->strong=true;
        }
        unsigned mainCount=0;if(!ReadDeiroiMainGunCount(lease,&mainCount) || mainCount>64)return false;
        for(unsigned index=0;index<mainCount;++index) {
            const unsigned key=(32u<<16)|index;void* actor=nullptr;void* holder=nullptr;
            if(!ResolveDeiroiControlledHolderKey(lease,key,&actor,&holder) || actor!=lease.object || !Readable(holder,0x140,true) || !FactoryPath(holder))return false;
            Retained* free=nullptr;bool found=false;
            for(auto& part:stream->parts) {
                if(part.strong && part.key==key){if(part.ref.object!=actor || part.holder!=holder)return false;found=true;break;}
                if(!part.strong && !free)free=&part;
            }
            if(found)continue;
            const Ref ref=Reference(actor);if(!free || !Strong(ref))return false;
            free->ref=ref;free->holder=holder;free->key=key;free->strong=true;
        }
        if(authority && !offline)for(auto& part:stream->parts)if(part.strong && !part.baseline && Current(part.ref) && !DeiroiExplicitTransition(part.ref.object)) {
            if((part.key>>16)==32) {
                const auto* row=static_cast<unsigned char*>(part.holder)-0x100;
                const auto callback=At<const void*>(row+0xB0,0x18);
                const unsigned phase=callback==image+0x31CB60 ? 6u : callback==image+0x31CDB0 ? 7u : 0u;
                if(!phase)return false;
                deiroi_wire::Event event{};event.kind=deiroi_wire::Kind::control;event.key=part.key;event.phase=static_cast<unsigned char>(phase);
                event.seed=At<std::uint64_t>(part.ref.object,0x3E8);if(!Push(*stream,event))return false;
                part.baseline=true;part.phase=event.phase;continue;
            }
            // Capture an already-running native burst before its first accepted
            // shot. Otherwise a mid-burst lease could emit bullets without ever
            // initializing the guest's real charge/fire/model effects.
            constexpr unsigned callbacks[]={0x32C750,0x32C880,0x32C6A0,0x32D0C0};
            for(unsigned i=0;i<4;++i) {
                Desc desc{image+callbacks[i]};
                if(!Fn<bool(__fastcall*)(void*,const Desc*)>(0x32A890)(part.ref.object+kMachine,&desc))continue;
                deiroi_wire::Event event{};event.kind=deiroi_wire::Kind::control;event.key=part.key;event.phase=static_cast<unsigned char>(i+2);
                event.seed=At<std::uint64_t>(part.ref.object,0x3E8);
                if(!Push(*stream,event))return false;
                part.baseline=true;part.serial=At<unsigned char>(part.ref.object,0xAC1);part.phase=event.phase;break;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {if(auto* stream=Find(lease))stream->faulted=true;return false;}
}
bool InstallDeiroiAcceptedActions() noexcept {
    using namespace deiroi_net;
    __try {
        if(ready)return DeiroiAcceptedActionsReady();
        if(!image || (installedImage && image!=installedImage))return false;
        for(const auto& signature:signatures)if(!Readable(image+signature.rva,16) ||
            std::memcmp(image+signature.rva,signature.bytes,16))return false;
        if(!RegisterNativeGunFactoryProvider(gunProvider) || !InstallNativeGunFactoryDispatcher() ||
           !RegisterNativeUnitFactoryProvider(unitProvider) || !RegisterNativeUnitFactoryCallsite(0x84C5A) || !InstallNativeUnitFactoryDispatcher())return false;
        for(unsigned i=0;i<18;++i)if(!redirected[i]) {
            bool changed=false;const bool ok=RedirectCall(image+sites[i],image+targets[i],hooks[i],changed);redirected[i]=changed;
            if(!ok)return false;
        }
        installedImage=image;ready=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool DeiroiAcceptedActionsReady() noexcept {
    using namespace deiroi_net;
    __try {
        if(!ready || !image || image!=installedImage || !DeiroiEntityFactoryAimReady() || !NativeGunFactoryProviderReady(gunProvider) ||
           !NativeUnitFactoryProviderReady(unitProvider) || !NativeUnitFactoryCallsiteReady(0x84C5A))return false;
        for(unsigned i=0;i<18;++i)if(!Relay(i))return false;
        for(const auto& signature:signatures)if(!Readable(image+signature.rva,16) || std::memcmp(image+signature.rva,signature.bytes,16))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool DeiroiAcceptedNativeInFlight(const NativeLease& lease) noexcept {
    const auto* stream=deiroi_net::Find(lease);return stream && stream->nativeInFlight!=0;
}
bool DeiroiAcceptedEntityFactoryScope(const void* nativeParameter) noexcept {
    using namespace deiroi_net;return sample && sample->entity && sample->parameter==nativeParameter && sample->stream &&
        (sample->expected || (sample->manual && !sample->stream->faulted && !sample->stream->quiescing));
}
bool OwnsDeiroiAcceptedAction(const void* bytes,std::size_t size) noexcept {
    __try {return bytes && size>=4 && Readable(bytes,4) && At<std::uint32_t>(bytes,0)==deiroi_wire::kMagic;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool QueueDeiroiAcceptedControl(const NativeLease& lease,unsigned key,unsigned phase,std::uint64_t sequence,
                               std::uint64_t seed,const float point[3]) noexcept {
    using namespace deiroi_net;
    __try {
        bool authority=false;if(phase>1 || !sequence || !point || !NativeActionRole(lease.object,&authority) || !authority)return false;
        auto* stream=Bind(lease,true);if(!stream || stream->quiescing || (phase<2 && !seed))return false;
        if(!stream->rootStrong){stream->faulted=true;return false;} // production Before/UpdateAim must bind before an explicit native transition
        deiroi_wire::Event event{};event.kind=deiroi_wire::Kind::control;event.key=key;event.phase=static_cast<std::uint8_t>(phase);
        event.commandSequence=sequence;event.seed=seed;std::memcpy(event.point,point,sizeof(event.point));
        if(!Push(*stream,event))return false;
        for(auto& part:stream->parts)if(part.strong && part.key==key) {
            part.baseline=true;part.serial=At<unsigned char>(part.ref.object,0xAC1);part.phase=phase==0 ? 2u : 5u;break;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {if(auto* stream=Find(lease))stream->faulted=true;return false;}
}
bool CaptureDeiroiAcceptedAction(const NativeLease& lease,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    using namespace deiroi_net;if(used)*used=0;if(!used || !bytes)return false;
    if(!IsDeiroi(lease))return true;
    __try {
        auto* stream=Find(lease);
        if(!stream) {
            bool authority=false;if(!NativeActionRole(lease.object,&authority) || !authority)return false;
            stream=Bind(lease,true);
        }
        if(stream && stream->offline)return true;
        if(!stream || !stream->authority || (stream->faulted && !stream->quiescing))return false;
        if(!stream->count)return true;
        deiroi_wire::Event batch[deiroi_wire::kMaxBatch]{};unsigned count=0;std::size_t required=8;
        const std::size_t limit=capacity<deiroi_wire::kMaxBytes ? capacity : deiroi_wire::kMaxBytes;
        while(count<deiroi_wire::kMaxBatch && count<stream->count) {
            const auto& event=stream->queue[(stream->head+count)%kQueued];const auto next=deiroi_wire::EventBytes(event);
            if(required+next>limit)break;batch[count++]=event;required+=next;
        }
        if(!count || !deiroi_wire::Encode(batch,count,bytes,limit,used))return false;
        stream->head=(stream->head+count)%kQueued;stream->count-=count;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {if(auto* stream=Find(lease))stream->faulted=true;return false;}
}
enemy_control::ActionResult ApplyDeiroiAcceptedAction(const NativeLease& lease,const void* bytes,std::size_t size) noexcept {
    using namespace deiroi_net;
    __try {
        bool authority=false;if(!IsDeiroi(lease) || !NativeActionRole(lease.object,&authority) || authority)return Result::failed;
        auto* stream=Bind(lease,false);if(!stream || stream->faulted)return Result::failed;
        if(stream->nativeInFlight)return Result::pending;
        if(!stream->rootStrong && !BindDeiroiAcceptedContext(lease))return Result::pending;
        deiroi_wire::Event events[deiroi_wire::kMaxBatch]{};unsigned count=0;
        if(!deiroi_wire::Decode(bytes,size,events,deiroi_wire::kMaxBatch,&count))return Result::failed;
        Ref parts[deiroi_wire::kMaxBatch]{};void* holders[deiroi_wire::kMaxBatch]{};bool held[deiroi_wire::kMaxBatch]{};const Ref root=stream->root;
        if(!Strong(root))return Result::pending;
        Result result=Result::applied;++stream->nativeInFlight;
        __try {
            std::uint64_t expected=stream->applied;
            for(unsigned i=0;i<count;++i) {
                if(events[i].serial<=stream->applied) {
                    const auto& receipt=stream->receipts[events[i].serial%kQueued];
                    if(receipt.serial!=events[i].serial || receipt.hash!=Hash(events[i])){result=Result::failed;break;}
                    continue; // reliable retry: exact already committed records never replay
                }
                if(events[i].serial!=expected+1u){result=Result::failed;break;}expected=events[i].serial;
                if(!Preflight(lease,events[i],parts+i,holders+i) || !Strong(parts[i])){result=Result::pending;break;}
                held[i]=true;
                if(events[i].kind==deiroi_wire::Kind::entity && !Target(*stream,i,events[i].targetActorId,events[i].lockOrdinal)){result=Result::pending;break;}
            }
            if(result==Result::applied)for(unsigned i=0;i<count;++i) {
                const auto& event=events[i];if(event.serial<=stream->applied)continue;
                bool accepted=false;
                if(event.kind!=deiroi_wire::Kind::control)accepted=Emit(*stream,holders[i],event,&stream->targets[i]);
                else accepted=ApplyDeiroiAcceptedControl(lease,event.key,event.phase,event.serial,event.seed,event.point);
                if(!accepted){stream->faulted=true;result=Result::failed;break;}
                stream->receipts[event.serial%kQueued]={event.serial,Hash(event)};stream->applied=event.serial;
            }
        } __finally {
            __try {
                for(unsigned i=0;i<count;++i)if(held[i])Unstrong(parts[i]);Unstrong(root);
                if(!ReleaseTargets(*stream)){stream->faulted=true;result=Result::failed;}
            } __finally {--stream->nativeInFlight;}
        }
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {if(auto* stream=Find(lease))stream->faulted=true;return Result::failed;}
}
bool QuiesceDeiroiAcceptedActions(const NativeLease& lease) noexcept {
    using namespace deiroi_net;
    __try {
        auto* stream=Find(lease);if(!stream)return true;
        stream->quiescing=true;if(stream->nativeInFlight || DeiroiNativeTransitionActive(lease))return false;
        ++stream->nativeInFlight;
        __try {
        using SetFn=void(__fastcall*)(void*,const Desc*,void*);
        using IsFn=bool(__fastcall*)(void*,const Desc*);
        constexpr unsigned active[]={0x32C750,0x32C880,0x32C6A0};
        for(auto& part:stream->parts)if(part.strong && Current(part.ref)) {
            if((part.key>>16)==32) {
                void* actor=nullptr;void* holder=nullptr;
                if(!ResolveDeiroiControlledHolderKey(lease,part.key,&actor,&holder) || actor!=part.ref.object || holder!=part.holder)return false;
                auto* machine=static_cast<unsigned char*>(holder)-0x50;
                if(At<const void*>(machine,0x18)==image+0x31CB60) {
                    Fn<MainSetFn>(0x3211C0)(machine,image+0x31CDB0,nullptr);
                    if(stream->authority && !stream->offline && !stream->faulted) {
                        deiroi_wire::Event event{};event.kind=deiroi_wire::Kind::control;event.key=part.key;event.phase=7;
                        event.seed=At<std::uint64_t>(actor,0x3E8);if(!Push(*stream,event))return false;
                    }
                }
                continue;
            }
            void* actual=nullptr;if(!ResolveDeiroiRetainedGunKey(lease,part.key,&actual) || actual!=part.ref.object)return false;
            bool running=false;
            for(unsigned callback:active){Desc state{image+callback};running=running || Fn<IsFn>(0x32A890)(part.ref.object+kMachine,&state);}
            if(running){Desc idle{image+0x32D0C0};Fn<SetFn>(0x32C310)(part.ref.object+kMachine,&idle,nullptr);}
        }
        return ReleaseTargets(*stream);
        } __finally {--stream->nativeInFlight;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {if(auto* stream=Find(lease))stream->faulted=true;return false;}
}
bool DeiroiAcceptedQuiescing(const void* root) noexcept {
    __try {for(const auto& stream:deiroi_net::streams)if(stream.root.object==root && (stream.quiescing || stream.faulted))return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return true;}
    return false;
}
bool DeiroiAcceptedActionsEmpty(const NativeLease& lease) noexcept {
    __try {const auto* stream=deiroi_net::Find(lease);if(!stream)return true;if(stream->count || stream->reserved || stream->nativeInFlight || SourceBusy(*stream) || stream->releaseUncertain)return false;
        for(const auto& target:stream->targets)if(target.held || target.actorHeld || target.uncertain || target.actorUncertain)return false;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool AbortDeiroiAcceptedActions(const NativeLease& lease) noexcept {
    using namespace deiroi_net;
    __try {
        if(!QuiesceDeiroiAcceptedActions(lease))return false;
        auto* stream=Find(lease);if(!stream)return true;
        // The caller has independently invalidated the old trusted local epoch;
        // this function is never an automatic fallback for an ordinary guest fault.
        stream->head=0;stream->count=0;
        if(!ReleaseContext(*stream))return false;*stream={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool RestoreDeiroiAcceptedActions(const NativeLease& lease) noexcept {
    using namespace deiroi_net;
    __try {
        auto* stream=Find(lease);if(!stream)return true;
        if(stream->nativeInFlight || DeiroiNativeTransitionActive(lease) || (stream->authority && stream->count))return false; // drain accepted real emissions before releasing their identity/gates
        if(!ReleaseContext(*stream))return false;*stream={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
} // namespace crew::enemy_native
