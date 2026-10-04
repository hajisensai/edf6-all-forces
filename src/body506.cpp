// The 506 body the plugin flies its own aircraft in (jet.cpp, subcarrier.cpp, playerjet.cpp).
//
// Which aircraft a 506 is comes from its SGO's mark in the speed gain k (veh+0x162C): the one table of the
// marks is here, so no two owners can claim the same one and no owner compares floats of its own.
// The 506's physics step (slot 57) is hooked once, here: after the stock step it hands the body to the step
// of the owner its mark names, which gives the velocity and spin to set.
#include "crew.h"
#include "memory.h"

namespace crew {
namespace {
constexpr unsigned kHeli506=0x17DB238,kPhysics506=0x61B710;
constexpr std::size_t kSlotPhysics=57,kSpeedGain=0x162C,kBody=0x1650;
constexpr unsigned kSetLinearVelocity=0x11B18F0,kSetAngularVelocity=0x11B1760;

// The marks (testrange/gen.py JETS, tools/make_jets.py, tools/make_sub.py write them into the SGOs):
// jets 7001-7099 (jet.cpp kKinds and kCarrierMarks name each), the submarine carrier 7101, the player
// jets 7201-7299 (playerjet.cpp kKinds). No stock 506 has a speed gain this large.
struct MarkRange { float first,last; PluginBody body; };
constexpr MarkRange kMarks[]={
    {7001.0f,7099.0f,PluginBody::jet},
    {7101.0f,7101.0f,PluginBody::sub},
    {7201.0f,7299.0f,PluginBody::playerJet},
};

using PhysicsFn=void(__fastcall*)(void*);
using SetVecFn=void(*)(void*,const float*);
using StepFn=bool(*)(unsigned char*,float*,float*) noexcept;
PhysicsFn nextPhysics=nullptr;
bool physicsOk=false;

const unsigned char kPhysicsSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8};   // push rbx; sub rsp,20h; mov rbx,rcx; call
// Both jump through the Havok world interface; only the slot differs (0xA8 linear, 0xB0 angular).
const unsigned char kSetLinSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xA8,0x00,0x00};
const unsigned char kSetAngSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xB0,0x00,0x00};

StepFn StepOf(PluginBody body) noexcept {
    switch(body) {
        case PluginBody::jet: return &JetBodyStep;
        case PluginBody::sub: return &SubBodyStep;
        case PluginBody::playerJet: return &PlayerJetBodyStep;
        default: return nullptr;
    }
}

void __fastcall PhysicsHook(void* vehicle) {
    nextPhysics(vehicle);
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        const StepFn step=StepOf(BodyOf(v));
        if(!step)return;
        alignas(16) float lin[4]{},ang[4]{};
        if(!step(v,lin,ang))return;
        const auto body=At<void*>(v,kBody);
        if(!body)return;
        reinterpret_cast<SetVecFn>(image+kSetLinearVelocity)(body,lin);
        reinterpret_cast<SetVecFn>(image+kSetAngularVelocity)(body,ang);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        static ULONGLONG at=0;
        const ULONGLONG now=GetTickCount64();
        if(now-at>5000){at=now;Log("BODY506 physics: fault in a body's step (vehicle %p)",vehicle);}
    }
}
}  // namespace

float BodyMark(const void* vehicle) noexcept {
    const auto v=static_cast<const unsigned char*>(vehicle);
    if(!Readable(v,kSpeedGain+4) || At<const unsigned char*>(v,0)!=image+kHeli506)return 0.0f;
    return At<float>(v,kSpeedGain);
}

PluginBody BodyOf(const void* vehicle) noexcept {
    const float k=BodyMark(vehicle);
    for(const auto& m:kMarks)if(k>=m.first && k<=m.last)return m.body;
    return PluginBody::none;
}

bool Body506Ok() noexcept { return physicsOk; }

bool InstallBody506() noexcept {
    __try {
        const bool sig=Matches(kPhysics506,kPhysicsSig,sizeof(kPhysicsSig)) && Matches(kSetLinearVelocity,kSetLinSig,sizeof(kSetLinSig)) &&
                       Matches(kSetAngularVelocity,kSetAngSig,sizeof(kSetAngSig));
        if(!sig){Log("BODY506 profile mismatch: the plugin's aircraft are off");return false;}
        const auto slot=reinterpret_cast<void**>(image+kHeli506)+kSlotPhysics;
        void* const current=*slot;
        if(current!=image+kPhysics506)Log("BODY506 physics: chaining onto %p (another plugin)",current);
        nextPhysics=reinterpret_cast<PhysicsFn>(current);
        physicsOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PhysicsHook));
        Log("HOOK body506 physics=%d",physicsOk);
        return physicsOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
