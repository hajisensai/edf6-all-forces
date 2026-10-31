// Supported EDF.dll main action contract. No process address or opaque CAS object crosses the wire.
#include "enemy_control_action_native.h"
#include "enemy_control_identity.h"
#include "enemy_control_deiroi_net.h"
#include "enemy_control_projectile_native.h"
#include "crew.h"
#include "memory.h"
#include "command_identity.h"
#include "support_soldier.h"
#include <cmath>
#include <cstring>

namespace crew::enemy_native {
// Child adapter owns these closing implementations alongside its typed codec.
bool QuiesceDeiroiAcceptedActions(const NativeLease&) noexcept;
bool DeiroiAcceptedActionsEmpty(const NativeLease&) noexcept;
bool AbortDeiroiAcceptedActions(const NativeLease&) noexcept;
#ifdef ENEMY_CONTROL_ACTION_TEST
void* ActionTestFunction(unsigned) noexcept;
#endif
namespace action_native {
using Result=enemy_control::ActionResult;
using Dispatch=void(__fastcall*)(void*,unsigned,void*,void*);
struct Desc {const void* callback=nullptr;std::int32_t adjustor=0,padding=0;};
static_assert(sizeof(Desc)==16,"native member callback descriptor");
enum class Family:std::uint8_t {none,deiroi,large,timeShip};
struct Contract {Family family;unsigned vtable,machine,context,machineVtable,dispatch,setter;};
constexpr Contract kContracts[]={
    {Family::deiroi,0x17AA648,0xEE0,0xE20,0x17AA600,0x31E790,0x321300},
    {Family::large,0x17AFEE0,0x13B0,0x1310,0x17AFEB0,0x367930,0x369F20},
    {Family::timeShip,0x17B93C0,0x10B0,0x1010,0x17B93A8,0x3EDF30,0x3F00D0},
};
// These are local semantic tables, never RVAs supplied by the sender. Deiroi's
// six constructor registrations have this exact ordinal order (320CB0).
constexpr unsigned kDeiroi[]={0x321FA0,0x322520,0x322590,0x3226E0,0x321DE0,0x322290};
constexpr unsigned kLarge[]={0x36CA40,0x36C9E0};
constexpr unsigned kTimeShip[]={0x3F1170,0x3F0C00,0x3F04D0,0x3F0300,0x3F0820,0x3F1250,
    0x3F1430,0x3F1C30,0x3F1FB0,0x3F02C0,0x3F2690,0x3F2920};
struct Sig {unsigned rva;unsigned char bytes[16];};
constexpr Sig kSigs[]={
// Filled from the supported image and independently checked by the native audit.
    {0x3F01F0u,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xB2,0x01,0x48,0x81,0xC1,0xA0,0x08}},
    {0x30C0F0u,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48}},
    {0x6C04B0u,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0xF6,0x81,0x1C,0x03,0x00,0x00}},
    {0x6ECCF0u,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48}},
    {0x3E9780u,{0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x30}},
    {0x321300u,{0x80,0xFA,0xFF,0x0F,0x84,0x7D,0x01,0x00,0x00,0x48,0x89,0x5C,0x24,0x08,0x48,0x89}},
    {0x369F20u,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x3F00D0u,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
    {0x31E790u,{0x48,0x8B,0x49,0x10,0x48,0x85,0xC9,0x74,0x07,0x48,0x8B,0x01,0x48,0xFF,0x60,0x08}},
    {0x367930u,{0x4C,0x8B,0x51,0x18,0x48,0x8B,0xC1,0x4D,0x85,0xD2,0x74,0x0B,0x48,0x63,0x49,0x20}},
    {0x3EDF30u,{0x4C,0x8B,0x51,0x18,0x48,0x8B,0xC1,0x4D,0x85,0xD2,0x74,0x0B,0x48,0x63,0x49,0x20}},
    {0x30C080u,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0x49}},
    {0x320CB0u,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x55}},
    {0x321FA0u,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x55,0x48,0x8D,0x6C,0x24,0xD0}},
    {0x322520u,{0x48,0x83,0xEC,0x38,0x83,0xFA,0x01,0x75,0x62,0x80,0xB9,0xA0,0x04,0x00,0x00,0x00}},
    {0x322590u,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x85,0xD2,0x75,0x3A,0x48,0x81,0xC1}},
    {0x3226E0u,{0x48,0x8B,0xC4,0x55,0x53,0x48,0x8D,0x68,0xA1,0x48,0x81,0xEC,0xD8,0x00,0x00,0x00}},
    {0x322290u,{0x40,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xF9,0x85,0xD2,0x0F,0x85,0xD1,0x01,0x00}},
    {0x36CA40u,{0x48,0x89,0x5C,0x24,0x18,0x55,0x48,0x8D,0x6C,0x24,0xE0,0x48,0x81,0xEC,0x20,0x01}},
    {0x36C9E0u,{0x48,0x83,0xEC,0x38,0x85,0xD2,0x75,0x0C,0x83,0x89,0xE0,0x0F,0x00,0x00,0x02,0x48}},
    {0x3F1170u,{0x40,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xF9,0x85,0xD2,0x75,0x64,0x33,0xC0,0x48}},
    {0x3F0C00u,{0x40,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xF9,0x85,0xD2,0x75,0x6F,0x89,0x91,0x68}},
    {0x3F04D0u,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x55}},
    {0x3F0300u,{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x81,0xEC,0x80,0x00,0x00,0x00,0x48,0x8B,0x05}},
    {0x3F0820u,{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x81,0xEC,0x80,0x00,0x00,0x00,0x48,0x8B,0x05}},
    {0x3F1250u,{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0x85,0xD2,0x0F,0x85,0xAF,0x00,0x00}},
    {0x3F1430u,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8D}},
    {0x3F1C30u,{0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x81,0xEC,0x80,0x00,0x00,0x00,0x48,0x8B,0x05}},
    {0x3F1FB0u,{0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48,0x8B,0x05}},
    {0x3F02C0u,{0x85,0xD2,0x75,0x1F,0xBA,0x03,0x00,0x00,0x00,0xC7,0x81,0x68,0x11,0x00,0x00,0x0F}},
    {0x3F2690u,{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x48,0x8B,0x05}},
    {0x3F2920u,{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48,0x8B,0x05}},
};
constexpr unsigned kBindings=16,kQueued=64,kWords=44,kStateBytes=26,kHeaderBytes=8,kBatch=21,kPhaseSampleFrames=2;
constexpr std::uint32_t kMagic=0x31414E45; // ENA1
// The root states' version byte: 2 (2026-10-31) since the Deiroi attack's target (words 15..22) is an object reference
// (net_object_ref.h: a player by its slot, never its NetId).
constexpr unsigned char kRootVersion=2;
struct State {
    Family family=Family::none;std::uint8_t semantic=0,serial=0;
    std::uint64_t cycle=0,entrySeed=0,variantSeed=0;bool entryKnown=false,fromDamage=false;float seconds=0;std::uint32_t frame=0;
    // Exact typed word map, canonical zeros for all unused words:
    // Deiroi 0..2 command/consume counters+saved ordinal, 3..4 selected leg,
    // 5..8 accepted world point, 9..12 spawn velocity, 13..14 native bools,
    // 15..22 the actual stomp target NativeID32 (its padding word20 is zero).
    // Large 0 roll gate, 1..4 step quaternion, 5..8 contact surface normal,
    // 9..12 desired rolling velocity F90, 13..16 smoothed velocity FA0.
    // TimeShip 0 gates, 1 delay frames, 2 reserved zero, 3..5 world target, 6 zero,
    // 7..22 skill curve matrix, 23 completed native Enable event, 24..39 true
    // host state0 frame, 40..43 host state0 quaternion (temporary arguments).
    std::uint32_t data[kWords]{};
};
struct Binding {
    const void* object=nullptr;const void* control=nullptr;const Contract* contract=nullptr;
    const void* original=nullptr;alignas(void*) const void* table[3]{};
    bool authority=false,patched=false,faulted=false,observed=false,applied=false,inside=false,temporaryGate=false,quiescing=false;
    std::uint32_t extensionMask=0;unsigned producer=0;
    std::uint32_t savedGate=0,nativeEffects=0,appliedEffects=0,entrySemantic=0;
    std::uint64_t entrySeed=0,variantSeed=0,entryVersion=0,observedEntry=0;bool entryKnown=false;
    float entryFrame[16]{},entryRotation[4]{};
    const void* stompTarget=nullptr;State last{},published{},remote{},finalState{};State queue[kQueued]{};
    bool finalPending=false,finalLost=false,damageExiting=false,entryFromDamage=false,localDamageReturn=false;
    unsigned localReturnSemantic=0;
    unsigned head=0,count=0;
    // Actor targets are held across native exits/entries, including an already
    // dead but still registered actor. An entered-release fault is journalled.
    ObjRef targetReferences[kBatch]{};std::uint32_t targetsHeld=0;bool targetsUncertain=false,mainApplying=false;
};
Binding bindings[kBindings]{};
constexpr unsigned kExtensions=16;
NativeActionExtension extensions[kExtensions]{};unsigned extensionCount=0;
std::uint32_t ExtensionMask(const NativeLease& l) noexcept {
    std::uint32_t mask=0;for(unsigned i=0;i<extensionCount;++i)if(extensions[i].Needs(l))mask|=1u<<i;return mask;
}
template<class T>T Fn(unsigned rva) noexcept {
#ifdef ENEMY_CONTROL_ACTION_TEST
    return reinterpret_cast<T>(ActionTestFunction(rva));
#else
    return reinterpret_cast<T>(image+rva);
#endif
}
const Contract* ContractOf(const NativeLease& lease) noexcept {
    if(!lease.profile || !Readable(lease.profile,sizeof(*lease.profile)))return nullptr;
    for(const auto& c:kContracts)if(lease.profile->vtableRva==c.vtable)return &c;
    return nullptr;
}
bool BoundIdentity(const NativeLease& l) noexcept {
    if(!l.object || !l.profile || !l.control || !Readable(l.control,16) || At<LONG>(l.control,8)<=0 ||
       !Readable(l.object,0x358) || At<const void*>(l.object,0x30)!=l.control)return false;
    std::size_t count=0;const auto profiles=Profiles(&count);
    for(std::size_t i=0;i<count;++i)if(l.profile==profiles+i)return true;
    return false;
}
bool Identity(const NativeLease& l) noexcept {
    return BoundIdentity(l) && !(l.object[0x18]&4) && !l.object[0x2E8];
}
bool Codes() noexcept {
    if(!image)return false;
    for(const auto& s:kSigs)if(!Readable(image+s.rva,sizeof(s.bytes)) || std::memcmp(image+s.rva,s.bytes,sizeof(s.bytes)))return false;
    return true;
}
bool SameControl(const Binding& b,const void* object) noexcept {
    return b.object==object && b.control && Readable(object,0x38) && At<const void*>(object,0x30)==b.control;
}
Binding* CurrentRole(const void* object) noexcept {
    for(auto& b:bindings)if(SameControl(b,object) && Readable(b.control,16) && At<LONG>(b.control,8)>0)return &b;
    return nullptr;
}
Binding* Find(const NativeLease& l) noexcept {
    for(auto& b:bindings)if(b.object==l.object && b.control==l.control)return &b;return nullptr;
}
unsigned Count(Family f) noexcept {return f==Family::deiroi ? 6u : f==Family::large ? 2u : f==Family::timeShip ? 12u : 0u;}
unsigned Callback(Family f,unsigned semantic) noexcept {
    if(semantic>=Count(f))return 0;
    return f==Family::deiroi ? kDeiroi[semantic] : f==Family::large ? kLarge[semantic] : kTimeShip[semantic];
}
// Inspect the actual selected callback. Deiroi's current shared wrapper retains
// the original per-object registered callbacks and control blocks.
bool Semantic(const Binding& b,unsigned* semantic,std::uint8_t* serial=nullptr) noexcept {
    auto* actor=static_cast<const unsigned char*>(b.object);const auto& c=*b.contract;
    if(!Readable(actor+c.context,c.family==Family::deiroi ? 0xB2 : 0x92) ||
       !Readable(actor+c.machine,0x30) || At<const void*>(actor+c.machine,8)!=actor+c.context)return false;
    const auto current=At<const void*>(actor+c.context,c.family==Family::deiroi ? 0x28 : 8);
    if(current!=actor+c.machine)return false;
    const void* callback=nullptr;
    if(c.family==Family::deiroi) {
        const auto rows=At<const unsigned char*>(actor+c.context,0x10);
        const auto count=At<std::uint64_t>(actor+c.context,0x20),capacity=At<std::uint64_t>(actor+c.context,0x18);
        if(count!=6 || count>capacity || !Readable(rows,6*16))return false;
        const auto selected=At<const void*>(actor+c.machine,0x10);
        for(unsigned i=0;i<6;++i) {
            const auto entry=At<const unsigned char*>(rows+i*16,0);
            const auto control=At<const unsigned char*>(rows+i*16,8);
            if(!Readable(entry,0x20) || !Readable(control,16) || At<LONG>(control,8)<=0 ||
               At<const void*>(entry,8)!=image+kDeiroi[i] || At<int>(entry,16)!=0 || At<const void*>(entry,24)!=actor)return false;
            if(entry==selected){callback=image+kDeiroi[i];*semantic=i;}
        }
        if(!callback)return false;
    } else {
        if(At<const void*>(actor+c.machine,0x10)!=actor || At<int>(actor+c.machine,0x20)!=0)return false;
        callback=At<const void*>(actor+c.machine,0x18);
        bool found=false;
        for(unsigned i=0;i<Count(c.family);++i)if(callback==image+Callback(c.family,i)){*semantic=i;found=true;break;}
        if(!found)return false; // damage callbacks with opaque data stay on the stock path
    }
    if(serial)*serial=At<std::uint8_t>(actor+c.context,c.family==Family::deiroi ? 0xB1 : 0x91);
    return true;
}
bool ControlledSemantic(const Binding& b,unsigned* semantic=nullptr) noexcept {
    unsigned value=0;if(!Semantic(b,&value))return false;
    if(b.contract->family==Family::deiroi && value==4)return false; // stock death lifecycle
    if(semantic)*semantic=value;return true;
}
bool SameRemotePhase(const Binding& b) noexcept {
    unsigned semantic=0;return b.applied && ControlledSemantic(b,&semantic) && semantic==b.remote.semantic;
}
bool RestoredTimeShip(const Binding& b) noexcept {
    const auto* actor=static_cast<const unsigned char*>(b.object);
    if(!Readable(actor+0xBBC,1) || !Readable(actor+0xD20,1) || !Readable(actor+0xDF8,0x18))return false;
    if((actor[0xBBC]&1) && !actor[0xD20])return false;
    const auto* records=At<const unsigned char*>(actor,0xDF8);const auto count=At<std::uint64_t>(actor,0xE08);
    if(!count || count>1024 || !Readable(records,static_cast<std::size_t>(count)*0xC0))return false;
    for(std::size_t i=0;i<count;++i)if(records[i*0xC0+0xA4]!=1)return false;
    return true;
}
void* __fastcall StompTarget(void* actor) {
    for(const auto& b:bindings)if(SameControl(b,actor) && b.inside && !b.authority && b.contract->family==Family::deiroi)
        return const_cast<void*>(b.stompTarget);
    return Fn<void*(__fastcall*)(void*)>(0x30C0F0)(actor);
}
bool StompHook() noexcept {
#ifdef ENEMY_CONTROL_ACTION_TEST
    return true;
#else
    static unsigned char* installed=nullptr;
    if(installed)return installed==image;
    bool changed=false;
    if(!RedirectCall(image+0x322458,image+0x30C0F0,reinterpret_cast<void*>(&StompTarget),changed))return false;
    if(!changed)return false;installed=image;return true;
#endif
}
bool TransientDamage(const Binding&) noexcept;
void __fastcall Gate(void* machine,unsigned operation,void* dt,void* parameter) {
    for(auto& b:bindings)if(b.patched && b.object && SameControl(b,b.object) && static_cast<const unsigned char*>(b.object)+b.contract->machine==machine) {
        // Accepted state0/state2 are always real native calls. Only autonomous
        // guest state1 is suppressed; it cannot run a phase event a second time
        // before the reliable accepted state reaches it. Stock damage/death paths remain live.
        if((!b.authority || b.quiescing) && !b.inside && operation==1 && ControlledSemantic(b)) {
            // All three admitted callers unconditionally add dt and increment
            // the action clock after dispatch1. Compensate that verified update
            // so a frozen action cannot silently skip its frame1 native event.
            auto* clock=static_cast<unsigned char*>(const_cast<void*>(b.object))+b.contract->context;
            if(Readable(clock,8,true) && Readable(dt,4) && std::isfinite(At<float>(dt,0))) {
                Put<float>(clock,0,At<float>(clock,0)-At<float>(dt,0));Put<std::uint32_t>(clock,4,At<std::uint32_t>(clock,4)-1u);
            }
            return;
        }
        if(operation==2)b.damageExiting=TransientDamage(b);
        unsigned entered=0;
        const bool fromDamage=operation==0 && b.damageExiting;
        if(operation==0){b.damageExiting=false;if(!b.authority && !b.inside && fromDamage && ControlledSemantic(b,&entered)){b.localDamageReturn=true;b.localReturnSemantic=entered;}}
        if(b.authority && operation==0 && !(b.quiescing && fromDamage) && ControlledSemantic(b,&entered) && Readable(b.object,0x498)) {
            b.entrySeed=At<std::uint64_t>(b.object,0x3E8);b.variantSeed=At<std::uint64_t>(b.object,0x490);
            b.entrySemantic=entered;b.entryKnown=true;b.entryFromDamage=fromDamage;++b.entryVersion;if(b.finalPending)b.finalLost=true;
            if(b.contract->family==Family::timeShip && Readable(b.object,0xFD0)) {
                std::memcpy(b.entryFrame,static_cast<const unsigned char*>(b.object)+0x60,64);
                std::memcpy(b.entryRotation,static_cast<const unsigned char*>(b.object)+0xFC0,16);
            }
        }
        unsigned semantic=0;const bool ship=b.authority && b.contract->family==Family::timeShip && Semantic(b,&semantic) && semantic==6;
        if(ship && operation==0)b.nativeEffects=0;
        const bool enable=ship && operation==1 && At<unsigned>(b.object,b.contract->context+4)==1;
        Fn<Dispatch>(b.contract->dispatch)(machine,operation,dt,parameter);
        // 3F16BD==frame1 -> 3F16C5 calls 3F01F0 unconditionally. Record the
        // completed native effect, rather than guessing from a later phase.
        if(enable)b.nativeEffects|=1u;
        return;
    }
}
Binding* Bind(const NativeLease& l,bool authority) noexcept {
    if(auto* b=Find(l))return b->authority==authority && !b->faulted && Identity(l) ? b : nullptr;
    const auto* c=ContractOf(l);
    if(!c) {
        const auto mask=ExtensionMask(l);if(!mask || !Identity(l))return nullptr;
        for(auto& b:bindings)if(!b.object){b={};b.object=l.object;b.control=l.control;b.authority=authority;b.extensionMask=mask;return &b;}
        return nullptr;
    }
    if(!Identity(l) || !Codes() || !Readable(l.object+c->machine,0x30,true) ||
        (c->family==Family::deiroi && !StompHook()))return nullptr;
    const auto* native=reinterpret_cast<const void* const*>(image+c->machineVtable);
    if(!Readable(native-1,3*sizeof(void*)) || native[1]!=image+c->dispatch || At<const void*>(l.object+c->machine,0)!=native)return nullptr;
    for(auto& b:bindings)if(!b.object) {
        b={};b.object=l.object;b.control=l.control;b.contract=c;b.authority=authority;
        b.extensionMask=ExtensionMask(l);b.original=native[1];b.table[0]=native[-1];b.table[1]=native[0];b.table[2]=reinterpret_cast<const void*>(&Gate);
        Put<const void*>(l.object+c->machine,0,b.table+1);b.patched=true;
        if(c->family==Family::timeShip && RestoredTimeShip(b))b.nativeEffects=1;
        return &b;
    }
    return nullptr;
}
std::uint32_t Bits(float value) noexcept {std::uint32_t result=0;std::memcpy(&result,&value,4);return result;}
float Real(std::uint32_t value) noexcept {float result=0;std::memcpy(&result,&value,4);return result;}
bool Finite(const State& s,unsigned first,unsigned count) noexcept {
    for(unsigned i=first;i<first+count;++i)if(!std::isfinite(Real(s.data[i])))return false;return true;
}
bool Valid(const State& s) noexcept {
    if(s.family<Family::deiroi || s.family>Family::timeShip || s.semantic>=Count(s.family) || !s.cycle ||
       !std::isfinite(s.seconds) || s.seconds<0 || s.seconds>1000000 || s.frame>100000000 || (s.fromDamage && !s.entryKnown))return false;
    if(s.family==Family::deiroi) {
        if(s.semantic==4 || s.data[2]>255 || (s.data[2]>=6 && s.data[2]!=255) || s.data[13]>1 || s.data[14]>1 ||
           !Finite(s,5,8))return false;
        // Words 15..22: the attack's target, an object reference (net_object_ref.h: a player by its slot, its tag in word 20).
        if(s.data[23] || !netref::Valid(reinterpret_cast<const unsigned char*>(s.data+15),true))return false;
        for(unsigned i=24;i<kWords;++i)if(s.data[i])return false;
        if(s.semantic==5){bool any=false;for(unsigned i=15;i<23;++i)any=any || s.data[i]!=0;if(!any)return false;}
        else for(unsigned i=15;i<23;++i)if(s.data[i])return false;
    } else if(s.family==Family::large) {
        if(s.data[0]&~2u || !Finite(s,1,16))return false;
        for(unsigned i=17;i<kWords;++i)if(s.data[i])return false;
    } else {
        if(s.data[0]&~15u || s.data[1]>100000000 || s.data[2] || s.data[6] || !Finite(s,3,3) || !Finite(s,7,16) || !Finite(s,24,20) || (s.data[23]&~1u) || (s.data[23] && s.semantic!=6))return false;
    }
    return true;
}
bool ReadState(Binding& b,State* out) noexcept {
    unsigned semantic=0;std::uint8_t serial=0;if(!ControlledSemantic(b,&semantic) || !Semantic(b,&semantic,&serial))return false;
    const auto* actor=static_cast<const unsigned char*>(b.object);State s{};
    s.family=b.contract->family;s.semantic=static_cast<std::uint8_t>(semantic);s.serial=serial;s.cycle=1;
    if(b.entryKnown && b.entrySemantic==semantic){s.entryKnown=true;s.fromDamage=b.entryFromDamage;s.entrySeed=b.entrySeed;s.variantSeed=b.variantSeed;}
    s.seconds=At<float>(actor,b.contract->context);s.frame=At<std::uint32_t>(actor,b.contract->context+4);
    if(s.family==Family::deiroi) {
        if(!Readable(actor+0x1A50,0x30) || !Readable(actor+0x1600,0x30))return false;
        s.data[0]=At<std::uint32_t>(actor,0x1A50);s.data[1]=At<std::uint32_t>(actor,0x1A54);s.data[2]=actor[0x1A58];
        s.data[3]=At<std::uint32_t>(actor,0x1A60);s.data[4]=At<std::uint32_t>(actor,0x1A64);
        std::memcpy(s.data+5,actor+0x1A70,16);std::memcpy(s.data+9,actor+0x1610,16);
        s.data[13]=actor[0x1600];s.data[14]=actor[0x1628];
        if(semantic==5) {
            const auto leg=At<std::uint64_t>(actor,0x1A60),count=At<std::uint64_t>(actor,0xAF8);
            const auto* rows=At<const unsigned char*>(actor,0xAE8);
            if(leg>=count || count>32 || !Readable(rows,static_cast<std::size_t>(count)*0x40))return false;
            const auto* controller=At<const unsigned char*>(rows+leg*0x40,0x28);
            if(!Readable(controller,0x150))return false;
            const auto* target=At<const unsigned char*>(controller,0x140);const auto* control=At<const unsigned char*>(controller,0x148);
            if(!Readable(control,16) || At<LONG>(control,8)<=0 || !Readable(target,0x38) || At<const void*>(target,0x30)!=control ||
               !EncodeNetRef(target,reinterpret_cast<unsigned char*>(s.data+15))) {
                if(!b.observed || b.last.semantic!=5)return false;std::memcpy(s.data+15,b.last.data+15,32);
            }
        }
    } else if(s.family==Family::large) {
        if(!Readable(actor+0xFE0,4) || !Readable(actor+0x1430,16) || !Readable(actor+0x1480,16) || !Readable(actor+0xF90,32))return false;
        s.data[0]=At<std::uint32_t>(actor,0xFE0)&2u;std::memcpy(s.data+1,actor+0x1430,16);std::memcpy(s.data+5,actor+0x1480,16);std::memcpy(s.data+9,actor+0xF90,32);
    } else {
        if(!Readable(actor+0x1168,4) || !Readable(actor+0x1410,0x68))return false;
        s.data[0]=At<std::uint32_t>(actor,0x1168);s.data[1]=At<std::uint32_t>(actor,0x1470);
        // 1410/1474 are deferred input requests consumed in the separate slot5
        // path 3EF610. They are not accepted outcomes and never cross this wire.
        std::memcpy(s.data+3,actor+0x1420,12);std::memcpy(s.data+7,actor+0x1430,64);if(semantic==6)s.data[23]=b.nativeEffects&1u;
        if(b.entryKnown && b.entrySemantic==semantic){std::memcpy(s.data+24,b.entryFrame,64);std::memcpy(s.data+40,b.entryRotation,16);}
        else {std::memcpy(s.data+24,actor+0x60,64);std::memcpy(s.data+40,actor+0xFC0,16);}
    }
    if(!Valid(s))return false;*out=s;return true;
}
bool StockOnly(const Binding& b) noexcept {
    const auto* actor=static_cast<const unsigned char*>(b.object);const auto& c=*b.contract;
    if(!Readable(actor+c.machine,0x20))return false;
    if(c.family==Family::deiroi){unsigned value=0;return Semantic(b,&value) && value==4;}
    const auto callback=At<const void*>(actor+c.machine,0x18);
    return c.family==Family::large ? callback==image+0x36C2D0 || callback==image+0x36CE90 : callback==image+0x3F2360;
}
bool TransientDamage(const Binding& b) noexcept {
    const auto* actor=static_cast<const unsigned char*>(b.object);const auto& c=*b.contract;
    if(c.family==Family::deiroi || !Readable(actor+c.machine,0x20))return false;
    const auto callback=At<const void*>(actor+c.machine,0x18);
    return callback==image+(c.family==Family::large ? 0x36CE90 : 0x3F2360);
}
bool Observe(Binding& b) noexcept {
    if(b.finalPending)return false;
    State s{};
    if(!ReadState(b,&s)) {
        // Native death/damage has its own authenticated native stream. Do not
        // turn an opaque native event into a null-parameter synthetic action.
        return StockOnly(b);
    }
    bool transition=!b.observed || b.entryVersion!=b.observedEntry || s.semantic!=b.last.semantic || s.serial!=b.last.serial ||
        s.frame<b.last.frame || s.seconds<b.last.seconds;
    s.cycle=b.observed ? b.last.cycle+(transition ? 1u : 0u) : 1u;
    if(!s.cycle)return false;
    // Real transitions are never coalesced. Within one accepted action only its
    // typed result/phase is sampled; state0 effects are not replayed by samples.
    const unsigned essential=s.family==Family::timeShip ? 7u : s.family==Family::deiroi ? 15u : 1u;
    const bool changed=transition || std::memcmp(s.data,b.last.data,essential*4) || s.data[23]!=b.last.data[23] || s.frame-b.published.frame>=kPhaseSampleFrames;
    if(changed) {
        if(b.count==kQueued) {
            // Preserve the exact accepted final record even when its normal
            // ring is full. Closing freezes future state1; this record is
            // drained after room appears, never resampled from a later state.
            b.finalState=s;b.finalPending=true;b.last=s;b.observed=true;b.observedEntry=b.entryVersion;return false;
        }
        b.queue[(b.head+b.count)%kQueued]=s;++b.count;b.published=s;
    }
    b.last=s;b.observed=true;b.observedEntry=b.entryVersion;return true;
}
void FlushFinal(Binding& b) noexcept {
    if(b.finalPending && b.count<kQueued){b.queue[(b.head+b.count)%kQueued]=b.finalState;++b.count;b.published=b.finalState;b.finalPending=false;}
}
void PutWire(unsigned char*& p,std::uint64_t value,unsigned size) noexcept {for(unsigned i=0;i<size;++i)*p++=static_cast<unsigned char>(value>>(i*8));}
std::uint64_t GetWire(const unsigned char*& p,unsigned size) noexcept {std::uint64_t value=0;for(unsigned i=0;i<size;++i)value|=static_cast<std::uint64_t>(*p++)<<(i*8);return value;}
bool SeedDelta(const State& s,const State* prior) noexcept {
    return !prior || s.entrySeed!=prior->entrySeed || s.variantSeed!=prior->variantSeed;
}
unsigned StateBytes(const State& s,const State* prior=nullptr) noexcept {
    unsigned size=kStateBytes+(SeedDelta(s,prior) ? 16u : 0u);for(auto word:s.data)if(word)size+=4;return size;
}
void Encode(const State& s,unsigned char*& p,const State* prior=nullptr) noexcept {
    const bool seed=SeedDelta(s,prior);
    *p++=static_cast<unsigned char>(s.family);*p++=s.semantic;*p++=s.serial;*p++=static_cast<unsigned char>((seed ? 1u : 0u)|(s.entryKnown ? 2u : 0u)|(s.fromDamage ? 4u : 0u));
    PutWire(p,s.cycle,8);PutWire(p,Bits(s.seconds),4);PutWire(p,s.frame,4);
    if(seed){PutWire(p,s.entrySeed,8);PutWire(p,s.variantSeed,8);}
    std::uint64_t mask=0;for(unsigned i=0;i<kWords;++i)if(s.data[i])mask|=1ull<<i;
    PutWire(p,mask,6);for(unsigned i=0;i<kWords;++i)if(mask&(1ull<<i))PutWire(p,s.data[i],4);
}
bool Decode(const unsigned char*& p,const unsigned char* end,State* out,const State* prior=nullptr) noexcept {
    if(static_cast<std::size_t>(end-p)<kStateBytes)return false;
    State s{};s.family=static_cast<Family>(*p++);s.semantic=*p++;s.serial=*p++;const unsigned flags=*p++;
    if(flags&~7u || (!prior && !(flags&1)))return false;s.entryKnown=(flags&2)!=0;s.fromDamage=(flags&4)!=0;
    s.cycle=GetWire(p,8);s.seconds=Real(static_cast<std::uint32_t>(GetWire(p,4)));s.frame=static_cast<std::uint32_t>(GetWire(p,4));
    if(flags&1) {
        if(end-p<16)return false;s.entrySeed=GetWire(p,8);s.variantSeed=GetWire(p,8);
        if(prior && !SeedDelta(s,prior))return false;
    } else {s.entrySeed=prior->entrySeed;s.variantSeed=prior->variantSeed;}
    if(end-p<6)return false;const auto mask=GetWire(p,6);if(mask>>kWords)return false;
    for(unsigned i=0;i<kWords;++i)if(mask&(1ull<<i)) {
        if(end-p<4)return false;s.data[i]=static_cast<std::uint32_t>(GetWire(p,4));if(!s.data[i])return false;
    }
    if(!Valid(s))return false;*out=s;return true;
}
bool Writable(Binding& b,const State& s) noexcept {
    auto* actor=static_cast<unsigned char*>(const_cast<void*>(b.object));
    if(!Readable(actor+b.contract->context,8,true) || !Readable(actor+b.contract->machine,0x30,true) ||
       (s.entryKnown && (!Readable(actor+0x3E8,8,true) || !Readable(actor+0x490,8,true))))return false;
    if(s.family==Family::deiroi) {
        if(!Readable(actor+0x1A50,0x30,true) || !Readable(actor+0x1600,0x30,true))return false;
        if(s.semantic==3 || s.semantic==5) {
            const std::uint64_t leg=static_cast<std::uint64_t>(s.data[3])|(static_cast<std::uint64_t>(s.data[4])<<32);
            if(!Readable(actor+0xAE8,0x18))return false;
            const auto count=At<std::uint64_t>(actor,0xAF8),capacity=At<std::uint64_t>(actor,0xAF0);
            const auto rows=At<const unsigned char*>(actor,0xAE8);
            if(leg>=count || count>32 || count>capacity || !Readable(rows,static_cast<std::size_t>(count)*0x40))return false;
        }
    } else if(s.family==Family::large) {
        if(!Readable(actor+0xFE0,4,true) || !Readable(actor+0x1430,16,true) || !Readable(actor+0x1480,16,true) || !Readable(actor+0xF90,32,true))return false;
    }
    else if(!Readable(actor+0x1168,4,true) || !Readable(actor+0x1410,0x68,true) ||
        !Readable(actor+0x60,64,true) || !Readable(actor+0xFC0,16,true))return false;
    return true;
}
void Parameters(Binding& b,const State& s) noexcept {
    auto* actor=static_cast<unsigned char*>(const_cast<void*>(b.object));
    if(s.family==Family::deiroi) {
        Put<std::uint32_t>(actor,0x1A50,s.data[0]);Put<std::uint32_t>(actor,0x1A54,s.data[1]);actor[0x1A58]=static_cast<unsigned char>(s.data[2]);
        std::memcpy(actor+0x1A60,s.data+3,8);std::memcpy(actor+0x1A70,s.data+5,16);std::memcpy(actor+0x1610,s.data+9,16);
        actor[0x1600]=static_cast<unsigned char>(s.data[13]);actor[0x1628]=static_cast<unsigned char>(s.data[14]);
    } else if(s.family==Family::large) {
        Put<std::uint32_t>(actor,0xFE0,(At<std::uint32_t>(actor,0xFE0)&~2u)|s.data[0]);
        std::memcpy(actor+0x1430,s.data+1,16);std::memcpy(actor+0x1480,s.data+5,16);std::memcpy(actor+0xF90,s.data+9,32);
    } else {
        Put<std::uint32_t>(actor,0x1168,s.data[0]);Put<std::uint32_t>(actor,0x1470,s.data[1]);Put<std::uint32_t>(actor,0x1474,0);
        std::memcpy(actor+0x1420,s.data+3,12);Put<float>(actor,0x142C,1);actor[0x1410]=0;std::memcpy(actor+0x1430,s.data+7,64);
    }
}
void Phase(Binding& b,const State& s) noexcept {
    auto* actor=static_cast<unsigned char*>(const_cast<void*>(b.object));Put<float>(actor,b.contract->context,s.seconds);Put<std::uint32_t>(actor,b.contract->context+4,s.frame);
}
bool Effect(Binding& b,const State& s) noexcept {
    if(s.family!=Family::timeShip || !(s.data[23]&~b.appliedEffects))return true;
    Fn<void(__fastcall*)(void*)>(0x3F01F0)(const_cast<void*>(b.object));
    if(!RestoredTimeShip(b))return false;b.appliedEffects|=s.data[23];return true;
}
bool Enter(Binding& b,const State& s) noexcept {
    auto* actor=static_cast<unsigned char*>(const_cast<void*>(b.object));
    if(s.entryKnown){Put<std::uint64_t>(actor,0x3E8,s.entrySeed);Put<std::uint64_t>(actor,0x490,s.variantSeed);}
    alignas(16) float frame[16]{},rotation[4]{};
    const bool context=s.family==Family::timeShip;
    if(context) {
        std::memcpy(frame,actor+0x60,64);std::memcpy(rotation,actor+0xFC0,16);
        // A typed state0 argument context, not a physical correction. TimeShip's
        // accepted windup compares this world frame/quaternion with its target.
        // Use the actual host entry context so it cannot locally short-circuit
        // to another state, then restore the caller's physical view in finally.
        std::memcpy(actor+0x60,s.data+24,64);std::memcpy(actor+0xFC0,s.data+40,16);
    }
    b.inside=true;
    __try {
        if(s.family==Family::deiroi)Fn<void(__fastcall*)(void*,unsigned,void*)>(b.contract->setter)(actor+b.contract->machine,s.semantic,nullptr);
        else {const Desc desc{image+Callback(s.family,s.semantic)};Fn<void(__fastcall*)(void*,const Desc*,void*)>(b.contract->setter)(actor+b.contract->machine,&desc,nullptr);}
    } __finally {if(context){std::memcpy(actor+0x60,frame,64);std::memcpy(actor+0xFC0,rotation,16);}b.inside=false;}
    unsigned actual=0;return Semantic(b,&actual) && actual==s.semantic;
}
bool ReleaseMainTargets(Binding& b) noexcept {
    if(b.targetsUncertain)return false;
    for(unsigned i=0;i<kBatch;++i)if(b.targetsHeld&(1u<<i)) {
        bool consumed=false;const auto r=b.targetReferences[i];
        const bool okay=ReleaseNativeProjectileLock(const_cast<void*>(r.obj),r.ctrl,&consumed);
        if(consumed){b.targetsHeld&=~(1u<<i);b.targetReferences[i]={};}
        if(!okay){if(consumed)b.targetsUncertain=true;return false;}
    }
    return true;
}
Result ApplyRootStates(Binding* b,const void* bytes,std::size_t size) {
        const auto* p=static_cast<const unsigned char*>(bytes);
        if(GetWire(p,4)!=kMagic || *p++!=kRootVersion)return Result::failed;
        const unsigned count=*p++;if(!count || count>kBatch || *p++ || *p++)return Result::failed;
        const auto* end=static_cast<const unsigned char*>(bytes)+size;
        State state[kBatch]{};ObjRef stomp[kBatch]{};State previous=b->remote;bool prior=b->applied;
        // Decode, validate ordering, identity and every typed destination before
        // the first setter or write. A pending result is mutation-free.
        for(unsigned i=0;i<count;++i) {
            if(!Decode(p,end,state+i,i ? state+i-1 : nullptr) || state[i].family!=b->contract->family)return Result::failed;
            const auto& s=state[i];
            if(prior && (s.cycle<previous.cycle || (s.cycle==previous.cycle &&
                (s.semantic!=previous.semantic || s.frame<previous.frame || s.seconds<previous.seconds))))return Result::failed;
            if(!Writable(*b,s))return Result::pending;
            if(s.family==Family::deiroi && s.semantic==5 && (!prior || s.cycle>previous.cycle)) {
                const auto* id=reinterpret_cast<const unsigned char*>(s.data+15);void* actor=nullptr;const void* control=nullptr;
                if(!ResolveNativeProjectileActor(id,&actor,&control))return Result::pending;
                // A known expired target cannot make 325350 accept an attack:
                // its null-target branch is a no-op, never an applied action.
                if(!actor || !control)return Result::failed;
                b->targetReferences[i]={actor,control};b->targetsHeld|=1u<<i;
                unsigned char actual[32]{};
                if(!EncodeNetRef(actor,actual) || std::memcmp(id,actual,32))return Result::pending;
                stomp[i]=b->targetReferences[i];
            }
            previous=s;prior=true;
        }
        if(p!=end)return Result::failed;
        unsigned initial=0;
        if(!ControlledSemantic(*b,&initial))return TransientDamage(*b) ? Result::pending : Result::failed;
        for(unsigned i=0;i<count;++i) {
            const auto& s=state[i];unsigned current=0;
            if(!Semantic(*b,&current))return Result::failed;
            const bool newCycle=b->applied && s.cycle>b->remote.cycle;
            const bool recovered=s.fromDamage && b->localDamageReturn && current==s.semantic && b->localReturnSemantic==s.semantic;
            const bool enter=!recovered && (current!=s.semantic || newCycle || (!b->applied && s.entryKnown));
            if(newCycle || (!b->applied && s.entryKnown))b->localDamageReturn=false;
            if(enter)b->appliedEffects=0;
            b->stompTarget=stomp[i].obj;
            Parameters(*b,s); // remote Deiroi entry consumes the actual host-selected leg, target identity and point
            if(enter && !Enter(*b,s)){b->stompTarget=nullptr;b->faulted=true;return Result::failed;}
            b->stompTarget=nullptr;
            // A same-cycle phase jump never calls state0 or replays an MAB event.
            // The host supplies the accepted native curve/gates/leg target, while
            // the local state1 gate prevents a second autonomous emission.
            Parameters(*b,s);Phase(*b,s);if(!Effect(*b,s)){b->faulted=true;return Result::failed;}b->remote=s;b->applied=true;
        }
        return Result::applied;
}
} // namespace action_native

bool RegisterNativeActionExtension(const NativeActionExtension& e) noexcept {
    using namespace action_native;
    if(!e.tag || !e.Needs || !e.Capture || !e.Owns || !e.Apply || !e.Quiesce || !e.Empty || !e.Abort || !e.Restore)return false;
    for(unsigned i=0;i<extensionCount;++i)if(extensions[i].tag==e.tag) {
        const auto& was=extensions[i];return was.Needs==e.Needs && was.Before==e.Before && was.After==e.After &&
            was.Capture==e.Capture && was.Owns==e.Owns && was.Apply==e.Apply && was.Quiesce==e.Quiesce &&
            was.Empty==e.Empty && was.Abort==e.Abort && was.Restore==e.Restore;
    }
    if(extensionCount==kExtensions)return false;extensions[extensionCount++]=e;return true;
}
bool NeedsNativeActions(const NativeLease& l) noexcept {
    __try {return action_native::ContractOf(l)!=nullptr || (l.object && l.control && action_native::Find(l)) || action_native::ExtensionMask(l)!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeActionRole(const void* root,bool* authority) noexcept {
    if(!root || !authority)return false;
    __try {if(auto* b=action_native::CurrentRole(root)){*authority=b->authority;return true;}}
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
bool NativeActionQuiescing(const void* root) noexcept {
    if(!root)return false;
    __try {if(auto* b=action_native::CurrentRole(root))return b->quiescing;}
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
bool BeforeNativeActionInput(const NativeLease& l,bool authority,Input* input) noexcept {
    using namespace action_native;if(!NeedsNativeActions(l))return true;if(!input)return false;
    __try {
        Binding* b=Bind(l,authority);if(!b)return false;b->extensionMask|=ExtensionMask(l);
        for(unsigned i=0;i<extensionCount;++i)if((b->extensionMask&(1u<<i)) && extensions[i].Before && !extensions[i].Before(l,authority,input))return false;
        if(!b->contract)return true;
        if(b->contract->family==Family::deiroi && !InstallDeiroiAcceptedActions())return false;
        if(!authority || b->quiescing) {
            const unsigned mask=b->contract->family==Family::deiroi ? 0x30u : b->contract->family==Family::large ? 3u : 0xBu;
            input->held&=~mask;input->pressed&=~mask;
            if(SameRemotePhase(*b) && !b->temporaryGate) {if(!Writable(*b,b->remote))return false;Phase(*b,b->remote);}
            // The TimeShip input itself has an inline capsule-destroyed transition
            // outside state1. Block that autonomous main transition for this call.
            if(b->contract->family==Family::timeShip) {
                if(!Readable(l.object+0x1168,4,true) || !Readable(l.object+0x1410,1,true) || !Readable(l.object+0x1474,4,true))return false;
                l.object[0x1410]=0;Put<std::uint32_t>(l.object,0x1474,0);
                b->savedGate=At<std::uint32_t>(l.object,0x1168);b->temporaryGate=true;
                Put<std::uint32_t>(l.object,0x1168,b->savedGate|12u);
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool AfterNativeActionInput(const NativeLease& l) noexcept {
    using namespace action_native;if(!NeedsNativeActions(l))return true;
    __try {
        Binding* b=Find(l);if(!b || b->faulted || !Identity(l))return false;
        if(b->temporaryGate) {
            if(!Readable(l.object+0x1168,4,true))return false;
            Put<std::uint32_t>(l.object,0x1168,b->savedGate);b->temporaryGate=false;
        }
        if(b->quiescing)return true;
        for(unsigned i=0;i<extensionCount;++i)if((b->extensionMask&(1u<<i)) && extensions[i].After && !extensions[i].After(l))return false;
        if(!b->contract)return true;
        if(b->authority){if(!Observe(*b)){b->faulted=true;return false;}}
        else if(SameRemotePhase(*b)) {if(!Writable(*b,b->remote))return false;Phase(*b,b->remote);}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool CaptureNativeAction(const NativeLease& l,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    using namespace action_native;if(used)*used=0;if(!used || !bytes || !NeedsNativeActions(l))return false;
    __try {
        Binding* b=Find(l);if(!b || !b->authority || (b->faulted && !b->quiescing) || !(b->quiescing ? BoundIdentity(l) : Identity(l)))return false;
        if(b->contract && !b->quiescing && !Observe(*b))return false;
        FlushFinal(*b);const std::size_t wireCapacity=capacity<512 ? capacity : 512;
        const unsigned sources=2+extensionCount;
        for(unsigned n=0;n<sources;++n) {
            const unsigned source=(b->producer+n)%sources;
            if(source==0) {
                if(!b->contract || !b->count)continue;
                const std::size_t limit=wireCapacity;unsigned count=0;std::size_t required=kHeaderBytes;
                while(count<b->count && count<kBatch) {
                    const unsigned next=StateBytes(b->queue[(b->head+count)%kQueued],count ? &b->queue[(b->head+count-1)%kQueued] : nullptr);
                    if(required+next>limit)break;required+=next;++count;
                }
                if(!count)return false;
                auto* p=static_cast<unsigned char*>(bytes);PutWire(p,kMagic,4);*p++=kRootVersion;*p++=static_cast<unsigned char>(count);*p++=0;*p++=0;
                for(unsigned i=0;i<count;++i)Encode(b->queue[(b->head+i)%kQueued],p,i ? &b->queue[(b->head+i-1)%kQueued] : nullptr);
                *used=required;b->head=(b->head+count)%kQueued;b->count-=count;FlushFinal(*b);
            } else if(source==1) {
                if(!b->contract || b->contract->family!=Family::deiroi)continue;
                if(!CaptureDeiroiAcceptedAction(l,bytes,wireCapacity,used))return false;
            } else {
                const unsigned i=source-2;if(!(b->extensionMask&(1u<<i)))continue;
                if(!extensions[i].Capture(l,bytes,wireCapacity,used))return false;
            }
            if(*used){if(*used>512 || *used>capacity)return false;b->producer=(source+1)%sources;return true;}
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
enemy_control::ActionResult ApplyNativeAction(const NativeLease& l,const void* bytes,std::size_t size) noexcept {
    using namespace action_native;if(!bytes || size<kHeaderBytes || size>512 || !NeedsNativeActions(l))return Result::failed;
    __try {
        Binding* b=Find(l);if(!b || b->authority || b->faulted || !(b->quiescing ? BoundIdentity(l) : Identity(l)))return Result::failed;
        if(b->temporaryGate)return Result::pending;
        if(OwnsDeiroiAcceptedAction(bytes,size))return b->contract && b->contract->family==Family::deiroi ? ApplyDeiroiAcceptedAction(l,bytes,size) : Result::failed;
        for(unsigned i=0;i<extensionCount;++i)if(extensions[i].Owns(bytes,size))return (b->extensionMask&(1u<<i)) ? extensions[i].Apply(l,bytes,size) : Result::failed;
        if(!b->contract || !Identity(l))return Result::failed; // cached projectile extensions decide their own retired-body safety
        if(b->mainApplying)return Result::pending;
        if(b->targetsHeld || b->targetsUncertain)return Result::failed;
        Result result=Result::failed;b->mainApplying=true;
        __try {result=ApplyRootStates(b,bytes,size);}
        __finally {
            b->stompTarget=nullptr;
            __try {if(!ReleaseMainTargets(*b)){b->faulted=true;result=Result::failed;}}
            __finally {b->mainApplying=false;}
        }
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {if(auto* b=Find(l))b->faulted=true;return Result::failed;}
}
bool QuiesceNativeActions(const NativeLease& l) noexcept {
    using namespace action_native;
    __try {
        Binding* b=Find(l);if(!b)return true;
        bool ok=true;
        if(!b->quiescing && b->authority && b->contract && Identity(l) && !b->finalPending && !Observe(*b)) {
            if(!b->finalPending)b->finalLost=true;
        }
        b->quiescing=true; // keeps Role; no new periodic root samples or main state1 triggers
        if(b->contract && b->contract->family==Family::deiroi && !QuiesceDeiroiAcceptedActions(l))ok=false;
        for(unsigned i=0;i<extensionCount;++i)if((b->extensionMask&(1u<<i)) && !extensions[i].Quiesce(l))ok=false;
        return ok && !b->finalLost;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeActionsEmpty(const NativeLease& l) noexcept {
    using namespace action_native;
    __try {
        Binding* b=Find(l);if(!b)return true;
        if(b->targetsHeld || b->targetsUncertain || b->mainApplying || b->count || b->finalPending || b->finalLost || (b->authority && b->entryVersion!=b->observedEntry))return false;
        if(b->contract && b->contract->family==Family::deiroi && !DeiroiAcceptedActionsEmpty(l))return false;
        for(unsigned i=0;i<extensionCount;++i)if((b->extensionMask&(1u<<i)) && !extensions[i].Empty(l))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool AbortNativeActions(const NativeLease& l) noexcept {
    using namespace action_native;
    __try {
        Binding* b=Find(l);if(!b)return true;b->quiescing=true;if(b->mainApplying)return false;b->head=b->count=0;b->finalPending=b->finalLost=false;b->observedEntry=b->entryVersion;
        bool ok=ReleaseMainTargets(*b);
        if(b->contract && b->contract->family==Family::deiroi && !AbortDeiroiAcceptedActions(l))ok=false;
        for(unsigned i=0;i<extensionCount;++i)if((b->extensionMask&(1u<<i)) && !extensions[i].Abort(l))ok=false;
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RestoreNativeActions(const NativeLease& l) noexcept {
    using namespace action_native;
    __try {
        Binding* b=Find(l);if(!b)return true;
        if(b->mainApplying || !ReleaseMainTargets(*b) || !NativeActionsEmpty(l))return false;
        if(b->contract && b->contract->family==Family::deiroi && !RestoreDeiroiAcceptedActions(l))return false;
        for(unsigned i=0;i<extensionCount;++i)if((b->extensionMask&(1u<<i)) && !extensions[i].Restore(l))return false;
        const auto identity=enemy_identity::Read(b->object,b->control);
        if(identity==enemy_identity::State::unknown)return false;
        if(identity==enemy_identity::State::gone){*b={};return true;}
        auto* actor=static_cast<unsigned char*>(const_cast<void*>(b->object));
        if(b->temporaryGate) {
            if(!Readable(actor+0x1168,4,true))return false;Put<std::uint32_t>(actor,0x1168,b->savedGate);b->temporaryGate=false;
        }
        if(b->patched && At<const void*>(actor+b->contract->machine,0)==b->table+1) {
            if(!Readable(actor+b->contract->machine,sizeof(void*),true))return false;
            Put<const void*>(actor+b->contract->machine,0,image+b->contract->machineVtable);
        }
        *b={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
} // namespace crew::enemy_native
