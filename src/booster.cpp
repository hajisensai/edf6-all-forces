// The carrier's nozzle flames (空母喷口火焰): the stock transport's Booster objects on the carrier's four nozzles.
//
// The carrier is a V506_HELI shell wearing the V508 transport model scaled x1.6 (jet.cpp Role::carrier). The
// stock V508 lights its four nozzles with Booster objects its own vehicle class makes; the heli class never
// does, so the plugin makes the same four Boosters itself (docs/jet-model-re.md §8):
//   op_new 0x12D85B0(0x410); ctor 0x2CB810(obj, &param) (vtable 0x17A6D58, written at 0x2CB851); the ctor
//   attaches the booster to param+0x30 with 0x118AF20(parent, child) (child+0x38 = parent, child appended to
//   the parent's list) and hands a shared reference back in *(param+0x20); the stock caller (0x5E4CA0) then
//   registers it, 0x1195A20(mgr, &out) and 0x1197050(mgr, out[0]), and drops its own reference. (H)
//   Update (vtable slot 5, 0x2CBE30): copies the 64-byte matrix *(+0x3D8) to +0x60 each frame, so the matrix
//   storage outlives the booster (static here). The flame shows while +0x3EC or +0x3F0 is above a threshold;
//   +0x3F0 decays by +0x3E8 a frame, +0x3F4 counts down and zeroes +0x3EC at 0. (H)
// The plugin feeds each nozzle's bone world matrix (unit rows: the x1.6 model scale is in the sizes instead)
// and every frame sets +0x3EC = thrust share, +0x3F0 = 1, +0x3F4 = 3; a carrier gone for kStaleMs, or dead,
// has its boosters deleted (BoosterSweep, once a frame from jet.cpp JetReap: also once the last carrier is gone,
// when no carrier frame runs). (H/M) Which way the flame points relative to the bone is not verified in game (L).
#include "crew.h"
#include "jet_internal.h"   // FaultLog, BoosterSweep
#include "memory.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace crew {
namespace {
constexpr unsigned kOpNew=0x12D85B0,kCtor=0x2CB810,kRegister=0x1195A20,kRegister2=0x1197050,kUpdate=0x2CBE30;
constexpr unsigned kDelete=0x118A1B0,kVtable=0x17A6D58,kVtableLea=0x2CB851;
constexpr unsigned kParamVtable=0x17AE438,kConstA=0x1765B70,kConstB=0x17A8D80,kConstC=0x17A8D70,kConstD=0x1765B90;
constexpr std::size_t kObjectMgr=0x20B2958,kUpdateSlot=5,kSize=0x410;
constexpr std::size_t kLevel=0x3EC,kPulse=0x3F0,kHold=0x3F4,kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;
constexpr std::size_t kBoneWorld=0xB0;
constexpr float kFront[2]={56.0f,16.0f},kBack[2]={40.0f,12.0f};   // V508's 35/10 and 25/7.5, x1.6
constexpr int kMaxCarriers=8,kNozzles=4;
constexpr ULONGLONG kStaleMs=1000;

const unsigned char kOpNewSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xEB,0x0F,0x48,0x8B,0xCB,0xE8,0x47};
const unsigned char kCtorSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
const unsigned char kRegisterSig[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xD9};
const unsigned char kRegister2Sig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0xC1,0x48,0x8B,0x89,0xA8,0x0C,0x00,0x00,0x48,0x85};
const unsigned char kUpdateSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81,0xD8,0x03,0x00};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kVtableLeaSig[]={0x48,0x8D,0x05,0x00,0xB5,0x4D,0x01};

using OpNewFn=void*(*)(std::size_t);
using CtorFn=void*(*)(void*,void*);
using RegisterFn=void(*)(void*,void*);
using DeleteFn=void(*)(void*);
using CtrlFn=void(*)(void*);

struct Nozzle {
    alignas(16) float m[16];   // the matrix the booster copies each frame: never freed
    unsigned char* obj;
    void* ctrl;                // our weak reference
};
struct Carrier {
    const unsigned char* v;
    const void* ctrl;
    ULONGLONG seen;
    Nozzle n[kNozzles];
};
Carrier carriers[kMaxCarriers];
bool sigOk=false,broken=false;

bool Live(const Nozzle& z) noexcept {
    return z.obj && z.ctrl && Readable(z.obj,kSize) && Readable(z.ctrl,0x10) && At<const void*>(z.obj,0)==image+kVtable &&
           At<long>(z.ctrl,8)>0 && !(z.obj[kObjFlags]&kObjDeleted);
}

void DropWeak(void* ctrl) noexcept {
    if(!ctrl)return;
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+0xC),-1)==1)
        (*reinterpret_cast<CtrlFn* const*>(ctrl))[1](ctrl);
}

void DropShared(void* ctrl) noexcept {
    if(!ctrl)return;
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+8),-1)==1)
        (*reinterpret_cast<CtrlFn* const*>(ctrl))[0](ctrl);
    DropWeak(ctrl);
}

void Drop(Nozzle& z) noexcept {
    if(Live(z))reinterpret_cast<DeleteFn>(image+kDelete)(z.obj);
    DropWeak(z.ctrl);
    z.obj=nullptr;z.ctrl=nullptr;
}

int MakeFault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("FLAME the game faulted on a carrier booster (%08lX at EDF+%llX): flames are off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    broken=true;
    return EXCEPTION_EXECUTE_HANDLER;
}

// The stock caller's sequence (0x5E4CA0): new, construct, register, keep a weak reference.
void Make(Nozzle& z,const unsigned char* v,const float* size) noexcept {
    void* const mgr=At<void*>(image,kObjectMgr);
    if(!mgr)return;
    alignas(16) unsigned char p[0xB0]={};
    alignas(16) void* out[2]={};
    Put<const void*>(p,0x00,image+kParamVtable);
    Put<const void*>(p,0x08,z.m);
    Put<void*>(p,0x20,out);
    Put<const void*>(p,0x30,v);
    Put<const void*>(p,0x38,z.m);
    Put<float>(p,0x40,size[0]);
    Put<float>(p,0x44,size[1]);
    std::memcpy(p+0x50,image+kConstA,16);
    Put<int>(p,0x60,0);Put<float>(p,0x64,0.1f);Put<int>(p,0x68,1);Put<float>(p,0x6C,0.1f);
    std::memcpy(p+0x70,image+kConstB,16);
    std::memcpy(p+0x80,image+kConstC,16);
    Put<float>(p,0x94,5.0f);
    std::memcpy(p+0xA0,image+kConstD,16);
    unsigned char* const o=static_cast<unsigned char*>(reinterpret_cast<OpNewFn>(image+kOpNew)(kSize));
    if(!o)return;
    reinterpret_cast<CtorFn>(image+kCtor)(o,p);
    if(out[0]) {
        reinterpret_cast<RegisterFn>(image+kRegister)(mgr,out);
        reinterpret_cast<RegisterFn>(image+kRegister2)(mgr,out[0]);
    }
    if(At<const void*>(o,0)==image+kVtable && !(o[kObjFlags]&kObjDeleted)) {
        void* const ctrl=At<void*>(o,kSelfCtrl);
        if(ctrl) {
            _InterlockedIncrement(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+0xC));
            z.obj=o;z.ctrl=ctrl;
        }
    }
    DropShared(out[1]);
}

// The bone's world matrix with unit-length rows (the carrier's model scale stays out of the flame size).
void UnitMatrix(float* dst,const unsigned char* rec) noexcept {
    std::memcpy(dst,rec+kBoneWorld,64);
    for(int r=0;r<3;++r) {
        float* const row=dst+r*4;
        const float l=std::sqrt(row[0]*row[0]+row[1]*row[1]+row[2]*row[2]);
        if(l>1e-4f)for(int c=0;c<3;++c)row[c]/=l;
    }
}

Carrier* Find(const unsigned char* v,ULONGLONG ms) noexcept {
    const void* const ctrl=At<const void*>(v,kSelfCtrl);
    Carrier* slot=nullptr;
    for(auto& c:carriers) {
        if(c.v==v && c.ctrl==ctrl)return &c;
        if(!c.v && !slot)slot=&c;
    }
    if(!slot)return nullptr;
    slot->v=v;slot->ctrl=ctrl;slot->seen=ms;
    return slot;
}

void Sweep(ULONGLONG ms) noexcept {
    for(auto& c:carriers) {
        if(!c.v)continue;
        const bool gone=ms-c.seen>kStaleMs || !Readable(c.v,kTeam+4) || At<const void*>(c.v,kSelfCtrl)!=c.ctrl ||
                        (c.v[kObjFlags]&kObjDeleted);
        if(!gone)continue;
        for(auto& z:c.n)Drop(z);
        c.v=nullptr;c.ctrl=nullptr;
    }
}

void Frame(const unsigned char* v,unsigned char* const* recs,float intensity,ULONGLONG ms) noexcept {
    Carrier* const c=Find(v,ms);
    if(!c)return;
    c->seen=ms;
    for(int i=0;i<kNozzles;++i) {
        Nozzle& z=c->n[i];
        if(!recs[i] || !Readable(recs[i]+kBoneWorld,64))continue;
        UnitMatrix(z.m,recs[i]);
        if(!Live(z)) {
            DropWeak(z.ctrl);
            z.obj=nullptr;z.ctrl=nullptr;
            Make(z,v,i<2 ? kFront : kBack);
            if(!Live(z))continue;
        }
        Put<float>(z.obj,kLevel,intensity);
        Put<float>(z.obj,kPulse,1.0f);
        Put<int>(z.obj,kHold,3);
    }
}
}  // namespace

void CarrierFlames(const unsigned char* v,unsigned char* const* recs,float intensity,ULONGLONG ms) noexcept {
    if(!sigOk || broken || !v || !recs)return;
    __try { Frame(v,recs,intensity,ms); }
    __except(MakeFault(GetExceptionInformation())) {}
}

void BoosterSweep(ULONGLONG ms) noexcept {
    if(!sigOk || broken)return;
    __try { Sweep(ms); }
    __except(FaultLog("FLAME sweep",GetExceptionInformation())) {}
}

bool InstallBoosters() noexcept {
    __try {
        sigOk=Matches(kOpNew,kOpNewSig,sizeof(kOpNewSig)) && Matches(kCtor,kCtorSig,sizeof(kCtorSig)) &&
              Matches(kRegister,kRegisterSig,sizeof(kRegisterSig)) && Matches(kRegister2,kRegister2Sig,sizeof(kRegister2Sig)) &&
              Matches(kUpdate,kUpdateSig,sizeof(kUpdateSig)) && Matches(kDelete,kDeleteSig,sizeof(kDeleteSig)) &&
              Matches(kVtableLea,kVtableLeaSig,sizeof(kVtableLeaSig)) &&
              Readable(image+kVtable,(kUpdateSlot+1)*8) && At<const unsigned char*>(image,kVtable+kUpdateSlot*8)==image+kUpdate &&
              Readable(image+kParamVtable,8) && Readable(image+kConstA,16) && Readable(image+kConstB,16) &&
              Readable(image+kConstC,16) && Readable(image+kConstD,16);
        Log("HOOK carrier flames=%d%s",sigOk,sigOk ? "" : " (off: unexpected EDF.dll code)");
        return sigOk;
    } __except(FaultLog("FLAME install",GetExceptionInformation())){return false;}
}
// A new mission (mission.cpp MissionStart): the carriers and their boosters were the last mission's, gone with
// it: forgotten, not deleted. Each nozzle's weak reference is dropped: it kept the control block alive, so the
// block is there to drop it from (jet.cpp ResetJets), and keeping it would leak the block every mission.
void ResetBoosters() noexcept {
    for(auto& c:carriers) {
        for(auto& z:c.n)DropWeak(z.ctrl);
        c=Carrier{};
    }
}
}  // namespace crew
